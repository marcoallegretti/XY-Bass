#include "PitchTracker.h"

namespace xyb
{

namespace
{

float squaredDistance (const float* a, const float* b, int count) noexcept
{
    auto i = 0;
    auto sum = 0.0f;

   #if JUCE_INTEL && JUCE_USE_SIMD && defined (__AVX__)
    auto accumulator = _mm256_setzero_ps();

    for (; i + 8 <= count; i += 8)
    {
        const auto delta = _mm256_sub_ps (_mm256_loadu_ps (a + i), _mm256_loadu_ps (b + i));
        accumulator = _mm256_add_ps (accumulator, _mm256_mul_ps (delta, delta));
    }

    float lanes[8];
    _mm256_storeu_ps (lanes, accumulator);

    for (auto lane : lanes)
        sum += lane;
   #elif JUCE_INTEL && JUCE_USE_SIMD
    auto accumulator = _mm_setzero_ps();

    for (; i + 4 <= count; i += 4)
    {
        const auto delta = _mm_sub_ps (_mm_loadu_ps (a + i), _mm_loadu_ps (b + i));
        accumulator = _mm_add_ps (accumulator, _mm_mul_ps (delta, delta));
    }

    float lanes[4];
    _mm_storeu_ps (lanes, accumulator);

    for (auto lane : lanes)
        sum += lane;
   #elif JUCE_ARM && JUCE_USE_SIMD
    auto accumulator = vdupq_n_f32 (0.0f);

    for (; i + 4 <= count; i += 4)
    {
        const auto delta = vsubq_f32 (vld1q_f32 (a + i), vld1q_f32 (b + i));
        accumulator = vaddq_f32 (accumulator, vmulq_f32 (delta, delta));
    }

    float lanes[4];
    vst1q_f32 (lanes, accumulator);

    for (auto lane : lanes)
        sum += lane;
   #endif

    for (; i < count; ++i)
    {
        const auto delta = a[i] - b[i];
        sum += delta * delta;
    }

    return sum;
}

} // namespace

void PitchTracker::prepare (double sampleRate)
{
    decimationFactor = juce::jmax (1, (int) std::round (sampleRate / 4000.0));
    workingRate = sampleRate / decimationFactor;

    const auto antiAlias = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate,
                                                                            juce::jmin (1500.0, workingRate * 0.4),
                                                                            0.54);
    const auto antiAliasSecond = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate,
                                                                                  juce::jmin (1500.0, workingRate * 0.4),
                                                                                  1.31);

    decimationFilter[0].setCoefficients (*antiAlias);
    decimationFilter[1].setCoefficients (*antiAliasSecond);
    highPass.setCoefficients (*juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 20.0, 0.7071));

    minimumLag = juce::jmax (2, (int) std::floor (workingRate / maximumFrequency));
    maximumLag = (int) std::ceil (workingRate / minimumFrequency);

    windowLength = juce::jmax (256, (int) std::round (workingRate * 0.128));
    hopLength = juce::jmax (24, windowLength / 8);

    historySize = juce::nextPowerOfTwo (windowLength + maximumLag + hopLength + 4);

    history.assign ((size_t) historySize, 0.0f);
    window.assign ((size_t) (windowLength + maximumLag + 1), 0.0f);
    difference.assign ((size_t) (maximumLag + 1), 0.0f);
    normalised.assign ((size_t) (maximumLag + 1), 0.0f);

    reset();
}

void PitchTracker::reset()
{
    for (auto& filter : decimationFilter)
        filter.reset();

    highPass.reset();

    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (window.begin(), window.end(), 0.0f);
    std::fill (difference.begin(), difference.end(), 0.0f);
    std::fill (normalised.begin(), normalised.end(), 0.0f);

    writeIndex = 0;
    decimationCounter = 0;
    hopCounter = 0;
    currentLag = 0;
    searching = false;
    runningSum = 0.0f;

    frequency = 0.0f;
    confidence = 0.0f;
    stability = 0.0f;
    tracking = 0.0f;
    smoothedFrequency = 0.0f;
    deviation = 0.0f;
    pendingFrequency = 0.0f;
    pendingCount = 0;
    silenceLevel = 0.0f;
}

void PitchTracker::process (const float* mono, int numSamples) noexcept
{
    int decimatedCount = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        auto sample = highPass.process (mono[i]);
        sample = decimationFilter[0].process (sample);
        sample = decimationFilter[1].process (sample);

        if (++decimationCounter >= decimationFactor)
        {
            decimationCounter = 0;
            history[(size_t) writeIndex] = sample;
            writeIndex = (writeIndex + 1) & (historySize - 1);
            ++decimatedCount;

            if (++hopCounter >= hopLength)
            {
                hopCounter = 0;
                startFrame();
            }
        }
    }

    if (searching)
    {
        const auto span = juce::jmax (1, maximumLag - minimumLag + 1);
        const auto budget = juce::jmax (4, (int) std::ceil ((float) span * 2.5f * (float) juce::jmax (1, decimatedCount)
                                                            / (float) hopLength));
        advanceSearch (budget);
    }
}

void PitchTracker::startFrame() noexcept
{
    const auto needed = windowLength + maximumLag;
    auto index = (writeIndex + historySize - needed) & (historySize - 1);

    float peak = 0.0f;

    for (int i = 0; i < needed; ++i)
    {
        const auto value = history[(size_t) index];
        window[(size_t) i] = value;
        peak = juce::jmax (peak, std::abs (value));
        index = (index + 1) & (historySize - 1);
    }

    silenceLevel = peak;
    currentLag = minimumLag;
    runningSum = 0.0f;
    searching = true;
}

void PitchTracker::advanceSearch (int lagBudget) noexcept
{
    const auto* data = window.data();

    while (searching && lagBudget-- > 0)
    {
        const auto sum = squaredDistance (data, data + currentLag, windowLength);

        difference[(size_t) currentLag] = sum;
        runningSum += sum;
        normalised[(size_t) currentLag] = sum * (float) (currentLag - minimumLag + 1) / juce::jmax (runningSum, kTiny);

        if (++currentLag > maximumLag)
        {
            searching = false;
            finishFrame();
        }
    }
}

float PitchTracker::refineLag (int lagIndex) const noexcept
{
    if (lagIndex <= minimumLag || lagIndex >= maximumLag)
        return (float) lagIndex;

    const auto previous = difference[(size_t) (lagIndex - 1)];
    const auto centre = difference[(size_t) lagIndex];
    const auto next = difference[(size_t) (lagIndex + 1)];
    const auto denominator = 2.0f * (2.0f * centre - previous - next);

    if (std::abs (denominator) < kTiny)
        return (float) lagIndex;

    return (float) lagIndex + (next - previous) / denominator;
}

void PitchTracker::finishFrame() noexcept
{
    constexpr float acceptanceThreshold = 0.16f;

    int bestLag = -1;

    for (int lag = minimumLag + 1; lag < maximumLag; ++lag)
    {
        if (normalised[(size_t) lag] < acceptanceThreshold
            && normalised[(size_t) lag] <= normalised[(size_t) (lag - 1)]
            && normalised[(size_t) lag] <= normalised[(size_t) (lag + 1)])
        {
            bestLag = lag;
            break;
        }
    }

    if (bestLag < 0)
    {
        auto minimumValue = std::numeric_limits<float>::max();

        for (int lag = minimumLag; lag <= maximumLag; ++lag)
        {
            if (normalised[(size_t) lag] < minimumValue)
            {
                minimumValue = normalised[(size_t) lag];
                bestLag = lag;
            }
        }
    }

    if (bestLag < minimumLag)
    {
        confidence *= 0.5f;
        return;
    }

    const auto halfLag = bestLag / 2;

    if (halfLag >= minimumLag && normalised[(size_t) halfLag] < normalised[(size_t) bestLag] * 0.9f
        && normalised[(size_t) halfLag] < acceptanceThreshold)
        bestLag = halfLag;

    const auto refined = refineLag (bestLag);
    const auto candidate = (float) workingRate / juce::jmax (refined, 1.0f);
    const auto clarity = juce::jlimit (0.0f, 1.0f, 1.0f - normalised[(size_t) bestLag]);
    const auto levelGate = juce::jlimit (0.0f, 1.0f, (silenceLevel - 0.0004f) * 400.0f);

    auto frameConfidence = clarity * levelGate;

    if (candidate < minimumFrequency || candidate > maximumFrequency)
        frameConfidence *= 0.25f;

    auto accepted = frameConfidence > 0.35f;

    if (frequency > 0.0f && frameConfidence > 0.1f)
    {
        const auto jump = std::abs (std::log2 (juce::jmax (candidate / frequency, kTiny)));

        if (jump > 0.30f)
        {
            const auto continuesPending = pendingFrequency > 0.0f
                                          && std::abs (std::log2 (juce::jmax (candidate / pendingFrequency, kTiny))) < 0.06f;

            pendingCount = continuesPending ? pendingCount + 1 : 1;
            pendingFrequency = candidate;

            if (pendingCount < 3)
            {
                frameConfidence *= 0.35f;
                accepted = false;
            }
        }
        else
        {
            pendingCount = 0;
            pendingFrequency = 0.0f;
        }
    }

    const auto rise = frameConfidence > confidence ? 0.55f : 0.25f;
    confidence += (frameConfidence - confidence) * rise;

    if (accepted)
    {
        const auto glide = frequency > 0.0f && pendingCount == 0 ? 0.55f : 1.0f;
        frequency += (candidate - frequency) * glide;
        pendingCount = 0;
        pendingFrequency = 0.0f;
    }

    if (smoothedFrequency <= 0.0f)
        smoothedFrequency = frequency;

    smoothedFrequency += (frequency - smoothedFrequency) * 0.15f;

    const auto relativeDeviation = std::abs (frequency - smoothedFrequency) / juce::jmax (smoothedFrequency, 1.0f);
    deviation += (relativeDeviation - deviation) * 0.25f;

    const auto follow = accepted ? 0.25f : 0.10f;
    tracking += ((accepted ? 1.0f : 0.0f) - tracking) * follow;

    stability = juce::jlimit (0.0f, 1.0f, (1.0f - deviation * 14.0f) * tracking);
}

} // namespace xyb
