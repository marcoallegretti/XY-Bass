#include "BandSplitter.h"

namespace xyb
{

namespace
{

constexpr double kWorkingRate = 12000.0;
constexpr double kSpanSeconds = 0.008;
constexpr double kKernelBeta = 2.0;
constexpr double kResamplerPassband = 2000.0;
constexpr double kResamplerRejectionDb = 70.0;
constexpr int kBankStepsPerOctave = 12;

std::vector<double> kaiserWindow (int size, double beta)
{
    std::vector<double> window ((size_t) size);
    juce::dsp::WindowingFunction<double>::fillWindowingTables (window.data(), window.size(),
                                                               juce::dsp::WindowingFunction<double>::kaiser,
                                                               false, beta);
    return window;
}

// The first half and the centre of a symmetric low-pass whose zero-phase amplitude approximates
// 1 / (1 + (f / cutoff)^4), sampled from the closed form impulse response of that magnitude.
std::vector<float> designLowPass (double cutoff, double rate, int half)
{
    const auto window = kaiserWindow (2 * half + 1, kKernelBeta);
    const auto scale = juce::MathConstants<double>::sqrt2 * juce::MathConstants<double>::pi * cutoff / rate;

    std::vector<double> taps ((size_t) half + 1);
    auto sum = 0.0;

    for (int j = 0; j <= half; ++j)
    {
        const auto argument = scale * (double) (half - j);
        taps[(size_t) j] = std::exp (-argument) * (std::cos (argument) + std::sin (argument)) * window[(size_t) j];
        sum += (j == half ? 1.0 : 2.0) * taps[(size_t) j];
    }

    std::vector<float> result ((size_t) half + 1);

    for (size_t j = 0; j < taps.size(); ++j)
        result[j] = (float) (taps[j] / sum);

    return result;
}

// A windowed sinc cut off at the decimated Nyquist frequency, flat to the resampler passband.
std::vector<double> designResampler (double rate, int decimation)
{
    if (decimation <= 1)
        return { 1.0 };

    const auto transition = rate / (double) decimation - 2.0 * kResamplerPassband;
    const auto order = (int) std::ceil ((kResamplerRejectionDb - 7.95)
                                        / (2.285 * juce::MathConstants<double>::twoPi * transition / rate));
    const auto half = (order + 1) / 2;
    const auto window = kaiserWindow (2 * half + 1, 0.1102 * (kResamplerRejectionDb - 8.7));

    std::vector<double> taps ((size_t) (2 * half + 1));
    auto sum = 0.0;

    for (int j = 0; j <= 2 * half; ++j)
    {
        const auto position = juce::MathConstants<double>::pi * (double) (j - half) / (double) decimation;
        const auto sinc = j == half ? 1.0 : std::sin (position) / position;
        taps[(size_t) j] = sinc * window[(size_t) j];
        sum += taps[(size_t) j];
    }

    for (auto& tap : taps)
        tap /= sum;

    return taps;
}

inline float symmetricSum (const float* taps, const float* window, int half) noexcept
{
    const auto last = 2 * half;
    auto sum = taps[half] * window[half];

    for (int j = 0; j < half; ++j)
        sum += taps[j] * (window[j] + window[last - j]);

    return sum;
}

inline float innerProduct (const float* taps, const float* window, int count) noexcept
{
    auto sum = 0.0f;

    for (int j = 0; j < count; ++j)
        sum += taps[j] * window[j];

    return sum;
}

// Each ring is stored twice over, so its newest length samples are always contiguous.
inline void push (float* ring, int length, int& position, float value) noexcept
{
    position = position == 0 ? length - 1 : position - 1;
    ring[position] = value;
    ring[position + length] = value;
}

} // namespace

void BandSplitter::prepare (double sampleRate, int numChannels)
{
    const auto rate = juce::jmax (1.0, sampleRate);

    factor = juce::jmax (1, (int) std::round (rate / kWorkingRate));
    const auto workingRate = rate / (double) factor;

    const auto resampler = designResampler (rate, factor);
    decimatorHalf = (int) resampler.size() / 2;

    decimator.resize ((size_t) decimatorHalf + 1);

    for (int j = 0; j <= decimatorHalf; ++j)
        decimator[(size_t) j] = (float) resampler[(size_t) j];

    // One polyphase branch per output phase; the gain of the zeros stuffed between the
    // decimated samples is restored here.
    interpolatorTaps = ((int) resampler.size() + factor - 1) / factor;
    interpolator.assign ((size_t) (factor * interpolatorTaps), 0.0f);

    for (int phase = 0; phase < factor; ++phase)
        for (int tap = 0; tap < interpolatorTaps; ++tap)
            if (const auto index = phase + tap * factor; index < (int) resampler.size())
                interpolator[(size_t) (phase * interpolatorTaps + tap)] = (float) (resampler[(size_t) index] * factor);

    kernelHalf = juce::jmax (1, (int) std::round (kSpanSeconds * workingRate));

    bankSize = juce::jmax (2, 1 + (int) std::ceil (kBankStepsPerOctave * std::log2 (highestCrossover / lowestCrossover)));
    lowBank.resize ((size_t) (bankSize * (kernelHalf + 1)));

    for (int index = 0; index < bankSize; ++index)
    {
        const auto cutoff = lowestCrossover * std::pow (2.0, (double) index / kBankStepsPerOctave);
        const auto kernel = designLowPass (cutoff, workingRate, kernelHalf);
        std::copy (kernel.begin(), kernel.end(), lowBank.begin() + index * (kernelHalf + 1));
    }

    lowKernel.resize ((size_t) kernelHalf + 1);
    characterKernel = designLowPass (characterCrossover, workingRate, kernelHalf);

    latency = 2 * decimatorHalf + factor * kernelHalf;

    channels.resize ((size_t) juce::jlimit (1, 2, numChannels));

    for (auto& state : channels)
    {
        state.inputHistory.setSize (1, 2 * (2 * decimatorHalf + 1));
        state.decimatedHistory.setSize (1, 2 * (2 * kernelHalf + 1));
        state.lowHistory.setSize (1, 2 * interpolatorTaps);
        state.characterHistory.setSize (1, 2 * interpolatorTaps);
        state.delayLine.setSize (1, latency);
    }

    bankPosition = -1.0f;
    setCrossover (150.0f);
    reset();
}

void BandSplitter::reset() noexcept
{
    for (auto& state : channels)
    {
        for (auto* ring : { &state.inputHistory, &state.decimatedHistory, &state.lowHistory,
                            &state.characterHistory, &state.delayLine })
            ring->clear();

        state.inputPosition = state.decimatedPosition = state.interpolatedPosition = 0;
        state.delayPosition = 0;
        state.phase = 0;
    }
}

void BandSplitter::setCrossover (float lowHz) noexcept
{
    if (lowBank.empty())
        return;

    const auto octaves = std::log2 (juce::jlimit (lowestCrossover, highestCrossover, lowHz) / lowestCrossover);
    const auto position = juce::jlimit (0.0f, (float) (bankSize - 1), (float) kBankStepsPerOctave * octaves);

    if (juce::exactlyEqual (position, bankPosition))
        return;

    bankPosition = position;

    // Neighbouring kernels share the same delay, so blending them stays linear phase and the
    // subtraction that forms the upper bands keeps the split exact while the crossover moves.
    const auto index = juce::jmin ((int) position, bankSize - 2);
    const auto fraction = position - (float) index;
    const auto* lower = lowBank.data() + index * (kernelHalf + 1);
    const auto* upper = lower + (kernelHalf + 1);

    for (int j = 0; j <= kernelHalf; ++j)
        lowKernel[(size_t) j] = lower[j] + fraction * (upper[j] - lower[j]);
}

void BandSplitter::process (int channel, const float* input, float* low, float* mid, float* character,
                            int numSamples) noexcept
{
    if (! juce::isPositiveAndBelow (channel, (int) channels.size()))
        return;

    auto& state = channels[(size_t) channel];

    auto* inputRing = state.inputHistory.getWritePointer (0);
    auto* decimatedRing = state.decimatedHistory.getWritePointer (0);
    auto* lowRing = state.lowHistory.getWritePointer (0);
    auto* characterRing = state.characterHistory.getWritePointer (0);
    auto* delayLine = state.delayLine.getWritePointer (0);

    const auto inputLength = 2 * decimatorHalf + 1;
    const auto decimatedLength = 2 * kernelHalf + 1;
    const auto delayLength = state.delayLine.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        push (inputRing, inputLength, state.inputPosition, input[i]);

        if (state.phase == 0)
        {
            const auto decimated = factor > 1 ? symmetricSum (decimator.data(), inputRing + state.inputPosition, decimatorHalf)
                                              : input[i];

            push (decimatedRing, decimatedLength, state.decimatedPosition, decimated);

            const auto* recent = decimatedRing + state.decimatedPosition;
            const auto lowValue = symmetricSum (lowKernel.data(), recent, kernelHalf);
            const auto characterValue = symmetricSum (characterKernel.data(), recent, kernelHalf);

            // Both low-pass outputs share one position, as their rings have the same length.
            auto position = state.interpolatedPosition;
            push (lowRing, interpolatorTaps, position, lowValue);
            push (characterRing, interpolatorTaps, state.interpolatedPosition, characterValue);
        }

        const auto* taps = interpolator.data() + state.phase * interpolatorTaps;
        const auto lowPassed = innerProduct (taps, lowRing + state.interpolatedPosition, interpolatorTaps);
        const auto characterPassed = innerProduct (taps, characterRing + state.interpolatedPosition, interpolatorTaps);

        state.phase = state.phase + 1 == factor ? 0 : state.phase + 1;

        const auto delayed = delayLine[state.delayPosition];
        delayLine[state.delayPosition] = input[i];
        state.delayPosition = state.delayPosition + 1 == delayLength ? 0 : state.delayPosition + 1;

        low[i] = lowPassed;
        mid[i] = characterPassed - lowPassed;
        character[i] = delayed - characterPassed;
    }
}

} // namespace xyb
