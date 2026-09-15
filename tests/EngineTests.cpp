#include "BassEngine.h"
#include "TestSuites.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

#if JUCE_WINDOWS
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#elif JUCE_MAC
 #include <pthread.h>

extern "C"
{
    typedef void (malloc_logger_t) (uint32_t type, uintptr_t zone, uintptr_t size, uintptr_t extra,
                                    uintptr_t result, uint32_t skippedFrames);
    extern malloc_logger_t* malloc_logger;
}
#endif

namespace
{

std::atomic<int> allocationCount { 0 };
std::atomic<bool> allocationTracking { false };

void escape (const void* pointer) noexcept
{
    static std::atomic<const void*> sink { nullptr };
    sink.store (pointer, std::memory_order_relaxed);
}

void countAllocation() noexcept
{
    if (allocationTracking.load (std::memory_order_relaxed))
        allocationCount.fetch_add (1, std::memory_order_relaxed);
}

#if JUCE_WINDOWS

void* (*realMalloc) (size_t) = std::malloc;
void* (*realCalloc) (size_t, size_t) = std::calloc;
void* (*realRealloc) (void*, size_t) = std::realloc;

void* trackedMalloc (size_t size) { countAllocation(); return realMalloc (size); }
void* trackedCalloc (size_t count, size_t size) { countAllocation(); return realCalloc (count, size); }
void* trackedRealloc (void* pointer, size_t size) { countAllocation(); return realRealloc (pointer, size); }

void redirectImport (const char* name, void* replacement, void** original)
{
    auto* base = (BYTE*) GetModuleHandleW (nullptr);
    auto* dos = (IMAGE_DOS_HEADER*) base;
    auto* nt = (IMAGE_NT_HEADERS*) (base + dos->e_lfanew);
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    if (directory.Size == 0)
        return;

    for (auto* descriptor = (IMAGE_IMPORT_DESCRIPTOR*) (base + directory.VirtualAddress);
         descriptor->Name != 0; ++descriptor)
    {
        const auto namesRva = descriptor->OriginalFirstThunk != 0 ? descriptor->OriginalFirstThunk
                                                                  : descriptor->FirstThunk;
        auto* names = (IMAGE_THUNK_DATA*) (base + namesRva);
        auto* addresses = (IMAGE_THUNK_DATA*) (base + descriptor->FirstThunk);

        for (; names->u1.AddressOfData != 0; ++names, ++addresses)
        {
            if ((names->u1.Ordinal & IMAGE_ORDINAL_FLAG) != 0)
                continue;

            const auto* import = (IMAGE_IMPORT_BY_NAME*) (base + names->u1.AddressOfData);

            if (std::strcmp (import->Name, name) != 0)
                continue;

            DWORD previous = 0;

            if (VirtualProtect (&addresses->u1.Function, sizeof (void*), PAGE_READWRITE, &previous))
            {
                *original = (void*) addresses->u1.Function;
                addresses->u1.Function = (ULONGLONG) (uintptr_t) replacement;
                VirtualProtect (&addresses->u1.Function, sizeof (void*), previous, &previous);
            }
        }
    }
}

void installAllocationHooks()
{
    redirectImport ("malloc", (void*) trackedMalloc, (void**) &realMalloc);
    redirectImport ("calloc", (void*) trackedCalloc, (void**) &realCalloc);
    redirectImport ("realloc", (void*) trackedRealloc, (void**) &realRealloc);
}

#elif JUCE_MAC

pthread_t hookedThread {};

void logAllocation (uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t)
{
    constexpr uint32_t allocateEvent = 2;

    // libmalloc reports every thread in the process; only the thread under test counts.
    if ((type & allocateEvent) != 0 && pthread_equal (pthread_self(), hookedThread))
        countAllocation();
}

void installAllocationHooks()
{
    hookedThread = pthread_self();
    malloc_logger = logAllocation;
}

#else

void installAllocationHooks() {}

#endif

int failures = 0;
int checks = 0;

void check (bool condition, const juce::String& description)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::cout << "  FAIL  " << description << std::endl;
    }
}

void report (const juce::String& description, double value)
{
    std::cout << "        " << description << " = " << juce::String (value, 3) << std::endl;
}

void section (const juce::String& name)
{
    std::cout << name << std::endl;
}

using Buffer = juce::AudioBuffer<float>;

Buffer makeBuffer (int numChannels, int numSamples)
{
    Buffer buffer (numChannels, numSamples);
    buffer.clear();
    return buffer;
}

void fillSine (Buffer& buffer, double frequency, double sampleRate, float amplitude)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / sampleRate);
    }
}

void fillNotePattern (Buffer& buffer, double frequency, double sampleRate, float amplitude, double noteSeconds)
{
    const auto noteSamples = (int) (noteSeconds * sampleRate);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);
        double phase = 0.0;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto positionInNote = i % noteSamples;
            const auto normalised = (double) positionInNote / (double) noteSamples;
            const auto envelope = normalised < 0.75 ? std::exp (-normalised * 4.0) : 0.0;

            if (positionInNote == 0)
                phase = 0.0;

            data[i] = amplitude * (float) (envelope * std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * frequency / sampleRate;
        }
    }
}

void fillKick (Buffer& buffer, double sampleRate, float amplitude)
{
    const auto period = (int) (0.5 * sampleRate);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);
        double phase = 0.0;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto position = i % period;
            const auto seconds = (double) position / sampleRate;
            const auto envelope = std::exp (-seconds * 14.0);
            const auto sweep = 120.0 * std::exp (-seconds * 32.0) + 48.0;

            if (position == 0)
                phase = 0.0;

            data[i] = amplitude * (float) (envelope * std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * sweep / sampleRate;
        }
    }
}

void fillNoise (Buffer& buffer, float amplitude, int seed)
{
    juce::Random random (seed);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = amplitude * (random.nextFloat() * 2.0f - 1.0f);
    }
}

void render (xyb::BassEngine& engine, Buffer& buffer, int blockSize)
{
    const auto numChannels = buffer.getNumChannels();
    float* pointers[2] = { nullptr, nullptr };

    for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
    {
        const auto count = juce::jmin (blockSize, buffer.getNumSamples() - start);

        for (int channel = 0; channel < numChannels; ++channel)
            pointers[channel] = buffer.getWritePointer (channel) + start;

        Buffer view (pointers, numChannels, count);
        engine.process (view);
    }
}

xyb::BassEngine::Parameters position (float x, float y)
{
    xyb::BassEngine::Parameters parameters;
    parameters.x = x;
    parameters.y = y;
    return parameters;
}

double magnitudeAt (const float* data, int numSamples, double frequency, double sampleRate)
{
    double real = 0.0, imaginary = 0.0, window = 0.0;
    const auto omega = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto hann = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (double) (numSamples - 1));
        real += hann * data[i] * std::cos (omega * i);
        imaginary -= hann * data[i] * std::sin (omega * i);
        window += hann;
    }

    return 2.0 * std::sqrt (real * real + imaginary * imaginary) / juce::jmax (window, 1.0);
}

double rms (const float* data, int numSamples)
{
    double sum = 0.0;

    for (int i = 0; i < numSamples; ++i)
        sum += (double) data[i] * (double) data[i];

    return std::sqrt (sum / juce::jmax (1, numSamples));
}

double peak (const Buffer& buffer)
{
    double result = 0.0;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            result = juce::jmax (result, (double) std::abs (data[i]));
    }

    return result;
}

double pinnedFraction (const Buffer& buffer, float threshold)
{
    const auto total = buffer.getNumSamples() * buffer.getNumChannels();

    if (total == 0)
        return 0.0;

    int pinned = 0;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (std::abs (data[i]) > threshold)
                ++pinned;
    }

    return (double) pinned / (double) total;
}

bool isFinite (const Buffer& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! (std::abs (data[i]) < 1.0e6f))
                return false;
    }

    return true;
}

void highPassFourthOrder (Buffer& buffer, double frequency, double sampleRate)
{
    const std::array<float, 2> qualities { { 0.5412f, 1.3066f } };

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (auto q : qualities)
        {
            xyb::Biquad filter;
            filter.prepare (sampleRate);
            filter.setHighPass ((float) frequency, q);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                data[i] = filter.process (data[i]);
        }
    }
}

void lowShelfBoost (Buffer& buffer, double frequency, double sampleRate, float gainDb)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        xyb::TptSvf filter;
        filter.prepare (sampleRate);
        filter.setQ (0.7071f);
        filter.setCutoff ((float) frequency);

        const auto gain = juce::Decibels::decibelsToGain (gainDb) - 1.0f;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] += filter.processLowPass (data[i]) * gain;
    }
}

std::vector<float> envelopeOf (const Buffer& buffer, double sampleRate)
{
    std::vector<float> result ((size_t) buffer.getNumSamples());
    xyb::EnvelopeFollower follower;
    follower.prepare (sampleRate);
    follower.setTimes (5.0f, 120.0f);

    const auto* data = buffer.getReadPointer (0);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        result[(size_t) i] = follower.process (data[i]);

    return result;
}

double measureAliasing (float x, float y, double sampleRate, double frequency, float mix = 1.0f, float amplitude = 0.25f)
{
    xyb::BassEngine engine;
    engine.prepare (sampleRate, 256, 1);

    auto parameters = position (x, y);
    parameters.mix = mix;
    engine.setParameters (parameters);

    constexpr int order = 15;
    constexpr int size = 1 << order;

    auto buffer = makeBuffer (1, (int) (sampleRate * 2.0) + size);
    fillSine (buffer, frequency, sampleRate, amplitude);
    render (engine, buffer, 256);

    std::vector<float> data ((size_t) size * 2, 0.0f);
    const auto* source = buffer.getReadPointer (0) + (int) (sampleRate * 2.0);

    for (int i = 0; i < size; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * i / (double) (size - 1);
        const auto window = 0.35875 - 0.48829 * std::cos (phase) + 0.14128 * std::cos (2.0 * phase)
                            - 0.01168 * std::cos (3.0 * phase);
        data[(size_t) i] = source[i] * (float) window;
    }

    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (data.data());

    const auto binWidth = sampleRate / (double) size;
    double harmonicPower = 0.0;
    double strayPower = 0.0;

    for (int bin = (int) (25.0 / binWidth); bin < size / 2; ++bin)
    {
        const auto binFrequency = bin * binWidth;
        const auto grid = frequency * 0.5;
        const auto nearest = std::round (binFrequency / grid);
        const auto distance = std::abs (binFrequency - nearest * grid);
        const auto power = (double) data[(size_t) bin] * (double) data[(size_t) bin];

        if (nearest >= 1.0 && distance < binWidth * 8.0)
            harmonicPower += power;
        else
            strayPower += power;
    }

    return 10.0 * std::log10 (juce::jmax (strayPower, 1.0e-30) / juce::jmax (harmonicPower, 1.0e-30));
}

double modulationDepthDb (const std::vector<float>& envelope)
{
    auto sorted = envelope;
    std::sort (sorted.begin(), sorted.end());

    const auto low = sorted[(size_t) ((double) sorted.size() * 0.15)];
    const auto high = sorted[(size_t) ((double) sorted.size() * 0.95)];

    return 20.0 * std::log10 (juce::jmax ((double) high, 1.0e-12) / juce::jmax ((double) low, 1.0e-12));
}

double correlationOf (const std::vector<float>& a, const std::vector<float>& b)
{
    const auto count = juce::jmin (a.size(), b.size());
    double meanA = 0.0, meanB = 0.0;

    for (size_t i = 0; i < count; ++i)
    {
        meanA += a[i];
        meanB += b[i];
    }

    meanA /= (double) count;
    meanB /= (double) count;

    double product = 0.0, varianceA = 0.0, varianceB = 0.0;

    for (size_t i = 0; i < count; ++i)
    {
        const auto da = a[i] - meanA;
        const auto db = b[i] - meanB;
        product += da * db;
        varianceA += da * da;
        varianceB += db * db;
    }

    return product / std::sqrt (juce::jmax (varianceA * varianceB, 1.0e-18));
}

struct HarmonicProfile
{
    double fundamental = 0.0;
    double second = 0.0;
    double third = 0.0;
    double fourth = 0.0;
    double fifth = 0.0;
    double density = 0.0;
    double highBand = 0.0;
};

HarmonicProfile measureHarmonics (float x, float y, double sampleRate, double frequency, float amplitude = 0.25f)
{
    xyb::BassEngine engine;
    engine.prepare (sampleRate, 256, 2);
    engine.setParameters (position (x, y));

    const auto totalSamples = (int) (sampleRate * 3.0);
    auto buffer = makeBuffer (2, totalSamples);
    fillSine (buffer, frequency, sampleRate, amplitude);
    render (engine, buffer, 256);

    const auto analysisStart = (int) (sampleRate * 2.0);
    const auto analysisLength = (int) (sampleRate * 1.0);
    const auto* data = buffer.getReadPointer (0) + analysisStart;

    HarmonicProfile profile;
    profile.fundamental = magnitudeAt (data, analysisLength, frequency, sampleRate);
    profile.second = magnitudeAt (data, analysisLength, frequency * 2.0, sampleRate);
    profile.third = magnitudeAt (data, analysisLength, frequency * 3.0, sampleRate);
    profile.fourth = magnitudeAt (data, analysisLength, frequency * 4.0, sampleRate);
    profile.fifth = magnitudeAt (data, analysisLength, frequency * 5.0, sampleRate);

    for (int order = 6; order <= 12; ++order)
    {
        const auto magnitude = magnitudeAt (data, analysisLength, frequency * order, sampleRate);
        profile.density += magnitude * magnitude;
    }

    profile.density = std::sqrt (profile.density);

    for (int order = 40; order < 200; ++order)
        profile.highBand = juce::jmax (profile.highBand,
                                       magnitudeAt (data, analysisLength, frequency * order, sampleRate));

    return profile;
}

double relativeDb (double value, double reference)
{
    return 20.0 * std::log10 (juce::jmax (value, 1.0e-12) / juce::jmax (reference, 1.0e-12));
}

void testShaperCharacter()
{
    section ("waveshaper character");

    const auto sampleRate = 48000.0;
    const auto length = 48000;

    auto measure = [&] (float driveAmount, float asymmetry, float clipping, int order)
    {
        std::vector<float> data ((size_t) length);
        const auto controls = xyb::makeShaperControls (driveAmount, asymmetry, clipping);
        xyb::DcBlocker blocker;
        blocker.prepare (sampleRate, 5.0f);

        for (int i = 0; i < length; ++i)
        {
            const auto input = 1.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 50.0 * i / sampleRate);
            data[(size_t) i] = blocker.process (xyb::shapeSample (input, controls));
        }

        return magnitudeAt (data.data(), length, 50.0 * order, sampleRate);
    };

    const auto fundamental = measure (1.0f, 0.18f, 0.0f, 1);
    const auto second = measure (1.0f, 0.18f, 0.0f, 2);
    const auto third = measure (1.0f, 0.18f, 0.0f, 3);

    report ("shaper h2", relativeDb (second, fundamental));
    report ("shaper h3", relativeDb (third, fundamental));

    const auto clippedFundamental = measure (1.0f, 0.18f, 1.0f, 1);
    const auto clippedSecond = measure (1.0f, 0.18f, 1.0f, 2);
    report ("clipped h2", relativeDb (clippedSecond, clippedFundamental));

    const auto symmetricSecond = measure (1.0f, 0.0f, 1.0f, 2);
    const auto symmetricFundamental = measure (1.0f, 0.0f, 1.0f, 1);
    report ("symmetric h2", relativeDb (symmetricSecond, symmetricFundamental));

    check (relativeDb (second, fundamental) > -30.0, "asymmetric drive generates even harmonics");
    check (relativeDb (clippedSecond, clippedFundamental) > -30.0, "clipping preserves the asymmetric character");
    check (relativeDb (symmetricSecond, symmetricFundamental) < -60.0, "symmetric drive stays odd");
}

void testBandReconstruction()
{
    section ("band reconstruction");

    auto worstError = 0.0;
    auto worstShape = 0.0;
    auto latencyMatches = true;

    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto length = (int) sampleRate * 2;
        constexpr int period = 128;

        xyb::BandSplitter splitter;
        splitter.prepare (sampleRate, 1);
        const auto latency = splitter.getLatencySamples();
        latencyMatches = latencyMatches && std::abs ((double) latency / sampleRate - 0.0085) < 0.001;

        auto source = makeBuffer (1, length);
        fillNoise (source, 0.5f, 3);
        auto bands = makeBuffer (3, length);

        // The crossover sweeps its whole range, as pitch tracking would move it.
        for (int start = 0; start < length; start += period)
        {
            const auto sweep = 0.5 + 0.5 * std::sin ((double) start * 0.0007);
            splitter.setCrossover (xyb::BandSplitter::lowestCrossover
                                   + (float) sweep * (xyb::BandSplitter::highestCrossover - xyb::BandSplitter::lowestCrossover));
            splitter.process (0, source.getReadPointer (0) + start, bands.getWritePointer (0) + start,
                              bands.getWritePointer (1) + start, bands.getWritePointer (2) + start,
                              juce::jmin (period, length - start));
        }

        for (int i = latency; i < length; ++i)
        {
            const auto summed = (double) bands.getSample (0, i) + bands.getSample (1, i) + bands.getSample (2, i);
            worstError = juce::jmax (worstError, std::abs (summed - source.getSample (0, i - latency)) / 0.5);
        }

        // Each band keeps the magnitude of a fourth order Linkwitz-Riley split wherever that
        // magnitude is above -30 dB.
        for (const auto crossover : { xyb::BandSplitter::lowestCrossover, xyb::BandSplitter::highestCrossover })
        {
            for (auto frequency = 20.0; frequency < 4000.0; frequency *= std::pow (2.0, 1.0 / 4.0))
            {
                xyb::BandSplitter probe;
                probe.prepare (sampleRate, 1);
                probe.setCrossover (crossover);

                const auto span = (int) (sampleRate * 0.5);
                auto tone = makeBuffer (1, span * 2);
                fillSine (tone, frequency, sampleRate, 0.5f);
                auto split = makeBuffer (3, span * 2);
                probe.process (0, tone.getReadPointer (0), split.getWritePointer (0), split.getWritePointer (1),
                               split.getWritePointer (2), span * 2);

                const auto lowRatio = std::pow (frequency / crossover, 4.0);
                const auto upperRatio = std::pow (frequency / xyb::BandSplitter::characterCrossover, 4.0);
                const auto lowTarget = 1.0 / (1.0 + lowRatio);
                const auto characterTarget = upperRatio / (1.0 + upperRatio);

                for (const auto& [band, target] : { std::pair<int, double> { 0, lowTarget }, { 2, characterTarget } })
                {
                    if (target < juce::Decibels::decibelsToGain (-30.0))
                        continue;

                    const auto measured = magnitudeAt (split.getReadPointer (band) + span, span, frequency, sampleRate) / 0.5;
                    worstShape = juce::jmax (worstShape, std::abs (relativeDb (measured, target)));
                }
            }
        }
    }

    report ("worst reconstruction error (dB)", juce::Decibels::gainToDecibels (worstError, -200.0));
    report ("worst band shape error against Linkwitz-Riley (dB)", worstShape);

    check (worstError < 1.0e-6, "the bands sum to the delayed input while the crossover moves");
    check (worstShape < 1.0, "the linear phase bands keep their fourth order magnitudes");
    check (latencyMatches, "the split delay is the same length of time at every rate");
}

void testFilterReplacements()
{
    section ("filter replacements");

    juce::Random random (0xf17e);
    auto worstBiquad = 0.0;

    const auto designs = { juce::dsp::IIR::Coefficients<double>::makeLowPass (48000.0, 1500.0, 0.54),
                           juce::dsp::IIR::Coefficients<double>::makeLowPass (44100.0, 1470.0, 1.31),
                           juce::dsp::IIR::Coefficients<double>::makeHighPass (96000.0, 20.0, 0.7071),
                           juce::dsp::IIR::Coefficients<double>::makeHighPass (768000.0, 14.0, 0.5412) };

    for (const auto& design : designs)
    {
        juce::dsp::IIR::Filter<double> reference (design);
        xyb::Biquad candidate;
        candidate.setCoefficients (*design);

        for (int i = 0; i < 48000; ++i)
        {
            const auto input = random.nextFloat() * 2.0f - 1.0f;
            const auto difference = std::abs (reference.processSample ((double) input) - (double) candidate.process (input));
            worstBiquad = juce::jmax (worstBiquad, difference);
        }
    }

    report ("worst biquad difference from JUCE (dB)", juce::Decibels::gainToDecibels (worstBiquad, -200.0));

    check (worstBiquad < 1.0e-6, "the biquad reproduces JUCE's IIR filter");
}

void testHalfbandOversampler()
{
    section ("halfband oversampler");

    constexpr int maximumBlock = 512;
    const int blockSizes[] = { 1, 7, 64, 512, 33, 256, 5, 511, 128, 2 };

    auto worstUp = 0.0;
    auto worstDown = 0.0;
    auto worstPassThrough = 0.0;
    auto latencyMatches = true;

    for (int channels = 1; channels <= 2; ++channels)
    {
        for (int stages = 0; stages <= 1; ++stages)
        {
            juce::dsp::Oversampling<float> reference ((size_t) channels, (size_t) stages,
                                                      juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                      true, true);
            reference.initProcessing ((size_t) maximumBlock);

            xyb::HalfbandOversampler candidate;
            candidate.prepare (channels, maximumBlock, stages > 0);

            latencyMatches = latencyMatches
                             && candidate.getLatencyInSamples() == juce::roundToInt (reference.getLatencyInSamples());

            juce::Random random (0x5eed + channels * 10 + stages);
            const auto total = 20000;

            Buffer referenceSignal (channels, total), candidateSignal (channels, total);

            for (int channel = 0; channel < channels; ++channel)
                for (int i = 0; i < total; ++i)
                {
                    const auto value = 0.6f * std::sin (0.013f * (float) i * (float) (channel + 1))
                                       + 0.4f * (random.nextFloat() * 2.0f - 1.0f);
                    referenceSignal.setSample (channel, i, value);
                    candidateSignal.setSample (channel, i, value);
                }

            auto offset = 0;
            auto index = 0;

            while (offset < total)
            {
                if (offset >= total / 2 && offset - blockSizes[(size_t) ((index + 9) % 10)] < total / 2)
                {
                    reference.reset();
                    candidate.reset();
                }

                const auto count = juce::jmin (blockSizes[(size_t) (index++ % 10)], total - offset);

                juce::dsp::AudioBlock<float> referenceBlock (referenceSignal);
                juce::dsp::AudioBlock<float> candidateBlock (candidateSignal);
                referenceBlock = referenceBlock.getSubBlock ((size_t) offset, (size_t) count);
                candidateBlock = candidateBlock.getSubBlock ((size_t) offset, (size_t) count);

                auto referenceUp = reference.processSamplesUp (referenceBlock);
                auto candidateUp = candidate.processSamplesUp (candidateBlock);

                for (size_t channel = 0; channel < (size_t) channels; ++channel)
                {
                    auto* expected = referenceUp.getChannelPointer (channel);
                    auto* actual = candidateUp.getChannelPointer (channel);

                    for (size_t i = 0; i < referenceUp.getNumSamples(); ++i)
                    {
                        worstUp = juce::jmax (worstUp, (double) std::abs (expected[i] - actual[i]));
                        expected[i] = std::tanh (3.0f * expected[i]);
                        actual[i] = std::tanh (3.0f * actual[i]);
                    }
                }

                reference.processSamplesDown (referenceBlock);
                candidate.processSamplesDown (candidateBlock);

                for (size_t channel = 0; channel < (size_t) channels; ++channel)
                    for (size_t i = 0; i < (size_t) count; ++i)
                    {
                        const auto difference = (double) std::abs (referenceBlock.getSample ((int) channel, (int) i)
                                                                   - candidateBlock.getSample ((int) channel, (int) i));

                        if (stages > 0)
                            worstDown = juce::jmax (worstDown, difference);
                        else
                            worstPassThrough = juce::jmax (worstPassThrough, difference);
                    }

                offset += count;
            }
        }
    }

    report ("worst upsampled difference from JUCE (dB)", juce::Decibels::gainToDecibels (worstUp, -200.0));
    report ("worst downsampled difference from JUCE (dB)", juce::Decibels::gainToDecibels (worstDown, -200.0));
    report ("worst pass-through difference from JUCE (dB)", juce::Decibels::gainToDecibels (worstPassThrough, -200.0));

    check (latencyMatches, "the oversampler reports the same latency as JUCE");
    check (worstUp < 1.0e-6, "upsampling reproduces JUCE's halfband filter");
    check (worstDown < 1.0e-6, "downsampling reproduces JUCE's halfband filter");
    check (juce::exactlyEqual (worstPassThrough, 0.0), "above 100 kHz the signal passes through untouched");
}

void testDegenerateSetup()
{
    section ("degenerate preparation");

    {
        xyb::BassEngine engine;
        auto buffer = makeBuffer (2, 256);
        fillKick (buffer, 48000.0, 0.5f);
        engine.process (buffer);

        check (isFinite (buffer), "an unprepared engine leaves the buffer finite");
    }

    for (auto rate : { 0.0, -48000.0, 1.0 })
    {
        xyb::BassEngine engine;
        engine.prepare (rate, 256, 2);
        engine.setParameters (position (0.5f, 0.5f));

        auto buffer = makeBuffer (2, 4096);
        fillKick (buffer, 48000.0, 0.5f);
        render (engine, buffer, 256);

        check (isFinite (buffer), "preparing at rate " + juce::String (rate) + " stays finite");
        check (peak (buffer) < 1.05, "preparing at rate " + juce::String (rate) + " stays bounded");
    }

    for (auto rate : { 88200.0, 176400.0 })
    {
        xyb::BassEngine engine;
        engine.prepare (rate, 256, 2);
        engine.setParameters (position (0.5f, 0.9f));

        auto buffer = makeBuffer (2, (int) (rate * 0.5));
        fillKick (buffer, rate, 0.7f);
        render (engine, buffer, 256);

        check (isFinite (buffer), "finite output at " + juce::String (rate) + " Hz");
        check (peak (buffer) > 0.02, "the engine produces output at " + juce::String (rate) + " Hz");
        check (pinnedFraction (buffer, 0.95f) < 0.01,
               "the output ceiling stays out of the way at " + juce::String (rate) + " Hz");
    }
}

void testStability()
{
    section ("stability across the pad");

    const std::array<double, 5> rates { { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 } };

    for (auto sampleRate : rates)
    {
        for (int channels = 1; channels <= 2; ++channels)
        {
            for (int gridX = 0; gridX <= 4; ++gridX)
            {
                for (int gridY = 0; gridY <= 4; ++gridY)
                {
                    xyb::BassEngine engine;
                    engine.prepare (sampleRate, 128, channels);
                    engine.setParameters (position ((float) gridX * 0.25f, (float) gridY * 0.25f));

                    auto buffer = makeBuffer (channels, (int) (sampleRate * 0.5));
                    fillKick (buffer, sampleRate, 0.7f);
                    render (engine, buffer, 128);

                    const auto where = juce::String (sampleRate) + " Hz, position "
                                       + juce::String (gridX) + "/" + juce::String (gridY);

                    check (isFinite (buffer), "finite output at " + where);
                    check (peak (buffer) > 0.02, "the engine produces output at " + where);
                    check (pinnedFraction (buffer, 0.95f) < 0.01,
                           "the output ceiling stays out of the way at " + where);
                }
            }
        }
    }
}

void testHighSampleRateFilters()
{
    section ("filters at high sample rates");

    auto loudest = 0.0;
    auto finite = true;

    for (const auto sampleRate : { 176400.0, 192000.0, 352800.0, 384000.0, 768000.0 })
    {
        for (const auto x : { 0.0f, 0.1f, 0.5f })
        {
            xyb::BassEngine engine;
            engine.prepare (sampleRate, 512, 2);
            engine.setParameters (position (x, 0.3f));

            // A sub-bass tone is where the lowest cutoffs sit closest to the unit circle.
            auto buffer = makeBuffer (2, (int) (sampleRate * 5.0));
            fillSine (buffer, 45.0, sampleRate, juce::Decibels::decibelsToGain (-10.0f));
            render (engine, buffer, 512);

            finite = finite && isFinite (buffer);
            loudest = juce::jmax (loudest, peak (buffer));
        }
    }

    report ("loudest output from a -10 dBFS tone (dB)", juce::Decibels::gainToDecibels (loudest, -200.0));
    check (finite, "high sample rates stay finite");
    check (loudest < juce::Decibels::decibelsToGain (-3.0), "low cutoffs stay stable up to 768 kHz");
}

void testLongHighSampleRateRuns()
{
    section ("long runs at high sample rates");

    auto worstPeak = 0.0;
    auto worstPinned = 0.0;
    auto finite = true;
    auto dropouts = 0;

    for (const auto sampleRate : { 192000.0, 384000.0, 768000.0 })
    {
        for (const auto& [x, y] : { std::pair<float, float> { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.5f, 0.5f } })
        {
            xyb::BassEngine engine;
            engine.prepare (sampleRate, 1024, 2);
            engine.setParameters (position (x, y));

            // Ten seconds of kick over a bass note, long enough for slow filter instability to build.
            auto buffer = makeBuffer (2, (int) (sampleRate * 10.0));
            fillKick (buffer, sampleRate, 0.5f);

            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.addSample (channel, i, 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 55.0 * i / sampleRate));

            render (engine, buffer, 1024);

            finite = finite && isFinite (buffer);
            worstPeak = juce::jmax (worstPeak, peak (buffer));
            worstPinned = juce::jmax (worstPinned, pinnedFraction (buffer, 0.95f));

            // The engine clears its output when it resets itself, which shows up as a run of zeros.
            auto zeros = 0;
            const auto* data = buffer.getReadPointer (0);

            for (int i = (int) sampleRate; i < buffer.getNumSamples(); ++i)
            {
                zeros = juce::exactlyEqual (data[i], 0.0f) ? zeros + 1 : 0;

                if (zeros == 64)
                    ++dropouts;
            }
        }
    }

    report ("loudest sample over ten second runs", worstPeak);
    report ("largest share of samples at the ceiling", worstPinned);
    report ("self resets during the runs", dropouts);

    check (finite, "long runs up to 768 kHz stay finite");
    check (worstPeak <= 1.0 && worstPinned < 0.01, "long runs up to 768 kHz stay off the ceiling");
    check (dropouts == 0, "long runs up to 768 kHz never reset themselves");
}

void testSilenceAndDenormals()
{
    section ("silence and denormal decay");

    xyb::BassEngine engine;
    engine.prepare (48000.0, 256, 2);
    engine.setParameters (position (0.5f, 0.9f));

    auto loud = makeBuffer (2, 48000);
    fillSine (loud, 48.0, 48000.0, 0.8f);
    render (engine, loud, 256);

    auto silence = makeBuffer (2, 48000 * 5);
    render (engine, silence, 256);

    const auto decaySamples = 48000 * 4;
    Buffer tail (2, silence.getNumSamples() - decaySamples);

    for (int channel = 0; channel < 2; ++channel)
        tail.copyFrom (channel, 0, silence, channel, decaySamples, tail.getNumSamples());

    report ("silence tail peak", peak (tail));
    check (peak (tail) < 1.0e-12, "output decays away after silence");

    xyb::BassEngine fresh;
    fresh.prepare (48000.0, 256, 2);
    fresh.setParameters (position (0.5f, 0.9f));

    auto pureSilence = makeBuffer (2, 48000);
    render (fresh, pureSilence, 256);
    check (juce::exactlyEqual (peak (pureSilence), 0.0), "silence into a reset engine produces exact zero");

    xyb::BassEngine decaying;
    decaying.prepare (48000.0, 256, 2);
    decaying.setParameters (position (1.0f, 1.0f));

    auto burst = makeBuffer (2, 48000);
    fillSine (burst, 55.0, 48000.0, 0.9f);
    render (decaying, burst, 256);

    auto longTail = makeBuffer (2, 48000 * 12);
    render (decaying, longTail, 256);

    auto subnormal = 0;

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < longTail.getNumSamples(); ++i)
            if (std::fpclassify (longTail.getSample (channel, i)) == FP_SUBNORMAL)
                ++subnormal;

    report ("subnormal samples in a long decay", subnormal);
    check (subnormal == 0, "a long decay never produces subnormal samples");
}

void testDcRejection()
{
    section ("dc rejection");

    // A source offset reaches the output as it does through the dry path, so only the dc the
    // processing adds is measured, once the low end dynamics have settled.
    auto addedOffset = [] (float offset, float toneAmplitude)
    {
        xyb::BassEngine engine;
        engine.prepare (48000.0, 256, 2);
        engine.setParameters (position (0.0f, 1.0f));

        auto buffer = makeBuffer (2, 48000 * 4);
        fillSine (buffer, 55.0, 48000.0, toneAmplitude);

        for (int channel = 0; channel < 2; ++channel)
            juce::FloatVectorOperations::add (buffer.getWritePointer (channel), offset, buffer.getNumSamples());

        render (engine, buffer, 256);

        double sum = 0.0;
        const auto* data = buffer.getReadPointer (0) + 48000 * 2;

        for (int i = 0; i < 48000 * 2; ++i)
            sum += data[i];

        return std::abs (sum / (48000.0 * 2.0) - offset);
    };

    const auto onOffset = addedOffset (0.5f, 0.0f);
    const auto fromTone = addedOffset (0.0f, 0.5f);

    report ("dc added to an offset source", onOffset);
    report ("dc left by asymmetric saturation", fromTone);
    check (onOffset < 0.002, "the processing adds no dc to an offset source");
    check (fromTone < 0.002, "dc from asymmetric saturation is removed");
}

void testDryPathAlignment()
{
    section ("dry path alignment and reported latency");

    xyb::BassEngine engine;
    engine.prepare (48000.0, 128, 2);

    auto parameters = position (0.8f, 0.7f);
    parameters.mix = 0.0f;
    engine.setParameters (parameters);

    const auto latency = engine.getLatencySamples();
    report ("reported latency", latency);

    auto buffer = makeBuffer (2, 48000);
    fillNoise (buffer, 0.3f, 42);
    Buffer reference (buffer);

    render (engine, buffer, 128);

    double worst = 0.0;

    for (int i = 24000; i < buffer.getNumSamples(); ++i)
        worst = juce::jmax (worst, (double) std::abs (buffer.getSample (0, i)
                                                      - reference.getSample (0, i - latency)));

    report ("dry alignment error", worst);
    check (worst < 1.0e-5, "mix at zero returns the input delayed by the reported latency");

    xyb::BassEngine hotEngine;
    hotEngine.prepare (48000.0, 128, 2);
    hotEngine.setParameters (parameters);

    auto hot = makeBuffer (2, 48000);
    fillNoise (hot, 0.985f, 91);
    Buffer hotReference (hot);

    render (hotEngine, hot, 128);

    double hotWorst = 0.0;
    double hotPeak = 0.0;

    for (int i = 24000; i < hot.getNumSamples(); ++i)
    {
        hotPeak = juce::jmax (hotPeak, (double) std::abs (hotReference.getSample (0, i)));
        hotWorst = juce::jmax (hotWorst, (double) std::abs (hot.getSample (0, i)
                                                            - hotReference.getSample (0, i - latency)));
    }

    report ("hot source peak", hotPeak);
    report ("dry alignment error on a hot source", hotWorst);
    check (hotPeak > 0.9, "the hot source actually exceeds the ceiling knee");
    check (hotWorst < 1.0e-5, "mix at zero stays transparent on material above the ceiling knee");
}

void testOversizedBlocks()
{
    section ("oversized host blocks");

    auto reference = makeBuffer (2, 48000);
    fillKick (reference, 48000.0, 0.4f);

    Buffer chunked (reference);
    Buffer oversized (reference);

    {
        xyb::BassEngine engine;
        engine.prepare (48000.0, 256, 2);
        engine.setParameters (position (0.4f, 0.6f));
        render (engine, chunked, 256);
    }

    {
        xyb::BassEngine engine;
        engine.prepare (48000.0, 256, 2);
        engine.setParameters (position (0.4f, 0.6f));
        render (engine, oversized, 4096);
    }

    double worst = 0.0;

    for (int i = 0; i < reference.getNumSamples(); ++i)
        worst = juce::jmax (worst, (double) std::abs (chunked.getSample (0, i) - oversized.getSample (0, i)));

    report ("oversized block difference", worst);
    check (worst < 1.0e-6, "a block larger than the prepared size is processed in full");
}

double steadyGainDb (float x, float y, float mix, bool autoGain, bool delta, double frequency)
{
    constexpr double sampleRate = 48000.0;
    constexpr float amplitude = 0.05f;

    xyb::BassEngine engine;
    engine.prepare (sampleRate, 256, 2);

    auto parameters = position (x, y);
    parameters.mix = mix;
    parameters.autoGain = autoGain;
    parameters.delta = delta;
    engine.setParameters (parameters);

    auto buffer = makeBuffer (2, (int) (sampleRate * 1.5));
    fillSine (buffer, frequency, sampleRate, amplitude);
    render (engine, buffer, 256);

    const auto span = (int) (sampleRate * 0.5);
    return relativeDb (magnitudeAt (buffer.getReadPointer (0) + buffer.getNumSamples() - span, span, frequency, sampleRate),
                       amplitude);
}

void testPartialMix()
{
    section ("partial mix");

    struct Setting
    {
        float x, y;
        bool autoGain;
        std::vector<float> mixes;
        const char* name;
    };

    const Setting settings[] = { { 0.5f, 0.5f, false, { 0.25f, 0.5f, 0.75f }, "centre" },
                                 { 0.5f, 0.0f, false, { 0.25f, 0.5f, 0.75f }, "clean middle" },
                                 { 1.0f, 0.5f, false, { 0.25f, 0.5f, 0.75f }, "translate" },
                                 { 0.44f, 0.14f, true, { 0.6f }, "gentle mixbus" } };

    auto worst = 1000.0;
    auto worstFrequency = 0.0;

    for (const auto& setting : settings)
    {
        auto settingWorst = 1000.0;

        for (auto frequency = 40.0; frequency <= 4000.0; frequency *= std::pow (2.0, 1.0 / 3.0))
        {
            const auto dry = steadyGainDb (setting.x, setting.y, 0.0f, setting.autoGain, false, frequency);
            const auto wet = steadyGainDb (setting.x, setting.y, 1.0f, setting.autoGain, false, frequency);

            for (const auto mix : setting.mixes)
            {
                const auto margin = steadyGainDb (setting.x, setting.y, mix, setting.autoGain, false, frequency)
                                    - juce::jmin (dry, wet);

                settingWorst = juce::jmin (settingWorst, margin);

                if (margin < worst)
                {
                    worst = margin;
                    worstFrequency = frequency;
                }
            }
        }

        report (juce::String ("deepest partial mix dip below both ends, ") + setting.name + " (dB)", settingWorst);
    }

    report ("frequency of the deepest dip", worstFrequency);
    check (worst > -1.0, "a partial mix never cancels below both the dry and the processed response");
}

void testDeltaMonitoring()
{
    section ("delta monitoring");

    auto loudest = -1000.0;

    // At clean positions nothing is processed this far above the bass bands, so anything delta
    // carries there would be dry signal leaking through misaligned bands.
    for (const auto& [x, y] : { std::pair<float, float> { 0.5f, 0.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f } })
    {
        for (const auto frequency : { 800.0, 1500.0 })
        {
            const auto level = steadyGainDb (x, y, 1.0f, true, true, frequency);
            report ("delta at " + juce::String (frequency, 0) + " Hz, position " + juce::String (x, 2) + "/"
                        + juce::String (y, 2) + " (dB)", level);
            loudest = juce::jmax (loudest, level);
        }
    }

    check (loudest < -40.0, "delta carries none of the dry signal above the processed bands");
}

void testCleanSettingsAtHighLevel()
{
    section ("clean settings at high level");

    auto thirdHarmonicDb = [] (float levelDb, double frequency, bool cancelling)
    {
        constexpr double sampleRate = 48000.0;

        xyb::BassEngine engine;
        engine.prepare (sampleRate, 256, 2);
        engine.setParameters (position (0.0f, 0.0f));

        auto buffer = makeBuffer (2, (int) sampleRate * 3);
        fillSine (buffer, frequency, sampleRate, juce::Decibels::decibelsToGain (levelDb));

        if (cancelling)
            buffer.applyGain (1, 0, buffer.getNumSamples(), -1.0f);

        render (engine, buffer, 256);

        const auto span = (int) sampleRate;
        const auto* data = buffer.getReadPointer (0) + buffer.getNumSamples() - span;
        return relativeDb (magnitudeAt (data, span, frequency * 3.0, sampleRate),
                           magnitudeAt (data, span, frequency, sampleRate));
    };

    auto worstTone = -1000.0;
    auto worstCollapse = -1000.0;

    for (const auto frequency : { 100.0, 150.0 })
    {
        const auto tone = thirdHarmonicDb (-3.0f, frequency, false);

        // Folding a cancelling low end to mono removes most of the signal, so the difference from
        // dry is nearly as loud as the input while the output itself stays well below full scale.
        const auto collapse = thirdHarmonicDb (-1.0f, frequency, true);

        report ("third harmonic of a -3 dBFS " + juce::String (frequency, 0) + " Hz tone (dBc)", tone);
        report ("third harmonic of a -1 dBFS cancelling " + juce::String (frequency, 0) + " Hz tone (dBc)", collapse);

        worstTone = juce::jmax (worstTone, tone);
        worstCollapse = juce::jmax (worstCollapse, collapse);
    }

    check (worstTone < -40.0, "clean sub keeps a loud tone free of third harmonic distortion");
    check (worstCollapse < -40.0, "a large difference from dry is not distorted on its way to the output");
}

void testCeilingContinuity()
{
    section ("output ceiling continuity");

    constexpr double sampleRate = 48000.0;
    constexpr int rampSamples = 48000;
    constexpr float extent = 1.2f;
    constexpr float quietDb = -12.0f;

    auto renderRamp = [] (float outputGainDb)
    {
        xyb::BassEngine engine;
        engine.prepare (sampleRate, 256, 2);

        auto parameters = position (0.5f, 0.0f);
        parameters.outputGainDb = outputGainDb;
        engine.setParameters (parameters);

        // A slow triangle through the ceiling on both sides.
        auto buffer = makeBuffer (2, rampSamples * 5);
        const auto increment = 2.0f * extent / (float) rampSamples;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto phase = (i + rampSamples / 2) % (2 * rampSamples);
            const auto value = phase < rampSamples ? -extent + increment * (float) phase
                                                   : extent - increment * (float) (phase - rampSamples);

            for (int channel = 0; channel < 2; ++channel)
                buffer.setSample (channel, i, value);
        }

        render (engine, buffer, 256);
        return buffer;
    };

    // Output gain comes after all processing, so the quiet render scaled back up is exactly the
    // signal the ceiling receives in the loud one, without ever reaching the ceiling itself.
    const auto limited = renderRamp (0.0f);
    const auto quiet = renderRamp (quietDb);
    const auto restore = 1.0 / juce::Decibels::decibelsToGain ((double) quietDb);

    auto steepest = 0.0;
    auto reversals = 0;
    auto loudest = 0.0;

    for (int i = rampSamples; i < limited.getNumSamples(); ++i)
    {
        const auto signalStep = ((double) quiet.getSample (0, i) - quiet.getSample (0, i - 1)) * restore;
        const auto limitedStep = (double) limited.getSample (0, i) - limited.getSample (0, i - 1);

        steepest = juce::jmax (steepest, std::abs (limitedStep) - std::abs (signalStep));
        loudest = juce::jmax (loudest, (double) std::abs (limited.getSample (0, i)));

        if (std::abs (signalStep) > 1.0e-6 && limitedStep * signalStep < 0.0)
            ++reversals;
    }

    report ("largest step beyond the limited signal's own step", steepest);
    report ("steps against the limited signal", reversals);
    report ("loudest output sample", loudest);

    check (steepest < 1.0e-6, "the ceiling never steps further than the signal it limits");
    check (reversals == 0, "the ceiling moves with the signal it limits");
    check (loudest > 0.9 && loudest <= 1.0, "the ramp reaches the ceiling and stays within full scale");
}

void testDeltaToggleIsSmooth()
{
    section ("delta toggle");

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    const auto length = (int) sampleRate * 3;
    const auto toggleAt = blockSize * 280;

    auto worstRatio = 0.0;

    for (const auto startInDelta : { false, true })
    {
        xyb::BassEngine engine;
        engine.prepare (sampleRate, blockSize, 2);

        auto buffer = makeBuffer (2, length);
        fillSine (buffer, 55.0, sampleRate, 0.4f);

        float* pointers[2] = { nullptr, nullptr };

        for (int start = 0; start < length; start += blockSize)
        {
            auto parameters = position (0.5f, 0.5f);
            parameters.delta = (start < toggleAt) == startInDelta;
            engine.setParameters (parameters);

            for (int channel = 0; channel < 2; ++channel)
                pointers[channel] = buffer.getWritePointer (channel) + start;

            Buffer view (pointers, 2, juce::jmin (blockSize, length - start));
            engine.process (view);
        }

        auto stepBetween = [&] (int from, int to)
        {
            auto result = 0.0;

            for (int i = from; i < to; ++i)
                result = juce::jmax (result, (double) std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1)));

            return result;
        };

        const auto around = stepBetween (toggleAt, toggleAt + 4800);
        const auto steady = juce::jmax (stepBetween (toggleAt - 9600, toggleAt), stepBetween (toggleAt + 24000, length));
        worstRatio = juce::jmax (worstRatio, around / juce::jmax (steady, 1.0e-9));
    }

    report ("largest step around the toggle against the steady signal's", worstRatio);
    check (worstRatio < 2.0, "switching delta fades rather than clicks");
}

void testMonoStereoConsistency()
{
    section ("mono and stereo consistency");

    auto stereo = makeBuffer (2, 48000);
    fillNotePattern (stereo, 60.0, 48000.0, 0.4f, 0.4);

    xyb::BassEngine engine;
    engine.prepare (48000.0, 256, 2);
    engine.setParameters (position (0.7f, 0.6f));
    render (engine, stereo, 256);

    double worst = 0.0;

    for (int i = 0; i < stereo.getNumSamples(); ++i)
        worst = juce::jmax (worst, (double) std::abs (stereo.getSample (0, i) - stereo.getSample (1, i)));

    report ("channel divergence", worst);
    check (worst < 1.0e-6, "correlated stereo input stays symmetric");

    auto mono = makeBuffer (1, 48000);
    fillNotePattern (mono, 60.0, 48000.0, 0.4f, 0.4);

    xyb::BassEngine monoEngine;
    monoEngine.prepare (48000.0, 256, 1);
    monoEngine.setParameters (position (0.7f, 0.6f));
    render (monoEngine, mono, 256);

    check (isFinite (mono), "mono rendering stays finite");

    const auto stereoLevel = rms (stereo.getReadPointer (0), stereo.getNumSamples());
    const auto monoLevel = rms (mono.getReadPointer (0), mono.getNumSamples());
    report ("mono/stereo level ratio", monoLevel / juce::jmax (stereoLevel, 1.0e-9));
    check (std::abs (relativeDb (monoLevel, stereoLevel)) < 1.5,
           "mono and stereo render at a comparable level");
}

void testHarmonicStructure()
{
    section ("harmonic structure by corner");

    const auto cleanSub = measureHarmonics (0.0f, 0.0f, 48000.0, 50.0);
    const auto cleanTranslate = measureHarmonics (1.0f, 0.0f, 48000.0, 50.0);
    const auto dirtySub = measureHarmonics (0.0f, 1.0f, 48000.0, 50.0);
    const auto dirtyTranslate = measureHarmonics (1.0f, 1.0f, 48000.0, 50.0);
    const auto centre = measureHarmonics (0.5f, 0.5f, 48000.0, 50.0);

    auto describe = [] (const juce::String& name, const HarmonicProfile& profile)
    {
        report (name + " h2", relativeDb (profile.second, profile.fundamental));
        report (name + " h3", relativeDb (profile.third, profile.fundamental));
        report (name + " h5", relativeDb (profile.fifth, profile.fundamental));
        report (name + " h6-h12", relativeDb (profile.density, profile.fundamental));
        report (name + " top", relativeDb (profile.highBand, profile.fundamental));
    };

    describe ("clean sub", cleanSub);
    describe ("clean translate", cleanTranslate);
    describe ("dirty sub", dirtySub);
    describe ("dirty translate", dirtyTranslate);
    describe ("centre", centre);

    check (relativeDb (cleanSub.second, cleanSub.fundamental) < -34.0,
           "clean sub keeps second harmonic low");
    check (relativeDb (cleanSub.third, cleanSub.fundamental) < -34.0,
           "clean sub keeps third harmonic low");

    check (relativeDb (cleanTranslate.second, cleanTranslate.fundamental) > -26.0,
           "clean translate generates a second harmonic");
    check (relativeDb (cleanTranslate.third, cleanTranslate.fundamental) > -32.0,
           "clean translate generates a third harmonic");
    check (relativeDb (cleanTranslate.highBand, cleanTranslate.fundamental) < -60.0,
           "clean translate stays free of broadband distortion");

    check (relativeDb (dirtySub.second, dirtySub.fundamental)
               > relativeDb (cleanSub.second, cleanSub.fundamental) + 10.0,
           "dirty sub adds low order harmonics");

    check (relativeDb (dirtyTranslate.density, dirtyTranslate.fundamental)
               > relativeDb (cleanTranslate.density, cleanTranslate.fundamental) + 6.0,
           "dirty translate increases harmonic density");

    check (relativeDb (centre.second, centre.fundamental) < relativeDb (cleanTranslate.second, cleanTranslate.fundamental),
           "centre is gentler than full translate");
    check (relativeDb (centre.second, centre.fundamental) > relativeDb (cleanSub.second, cleanSub.fundamental),
           "centre is not neutral");

    // The same corners six decibels below full scale, where the ceiling and the dynamics start to work.
    constexpr float loud = 0.5f;
    const auto loudCleanSub = measureHarmonics (0.0f, 0.0f, 48000.0, 100.0, loud);
    const auto loudCleanTranslate = measureHarmonics (1.0f, 0.0f, 48000.0, 50.0, loud);
    const auto loudDirtySub = measureHarmonics (0.0f, 1.0f, 48000.0, 50.0, loud);
    const auto loudDirtyTranslate = measureHarmonics (1.0f, 1.0f, 48000.0, 50.0, loud);

    describe ("loud clean sub at 100 Hz", loudCleanSub);
    describe ("loud clean translate", loudCleanTranslate);
    describe ("loud dirty sub", loudDirtySub);
    describe ("loud dirty translate", loudDirtyTranslate);

    check (relativeDb (loudCleanSub.second, loudCleanSub.fundamental) < -34.0
               && relativeDb (loudCleanSub.third, loudCleanSub.fundamental) < -34.0,
           "clean sub keeps low order harmonics low on a loud tone");
    check (relativeDb (loudCleanTranslate.second, loudCleanTranslate.fundamental) > -26.0
               && relativeDb (loudCleanTranslate.third, loudCleanTranslate.fundamental) > -32.0,
           "clean translate still generates its second and third harmonics on a loud tone");
    check (relativeDb (loudCleanTranslate.highBand, loudCleanTranslate.fundamental) < -60.0,
           "clean translate stays free of broadband distortion on a loud tone");
    check (relativeDb (loudDirtySub.second, loudDirtySub.fundamental)
               > relativeDb (loudCleanSub.second, loudCleanSub.fundamental) + 10.0,
           "dirty sub adds low order harmonics on a loud tone");
    check (relativeDb (loudDirtyTranslate.density, loudDirtyTranslate.fundamental)
               > relativeDb (loudCleanTranslate.density, loudCleanTranslate.fundamental) + 6.0,
           "dirty translate increases harmonic density on a loud tone");
}

struct KickShape
{
    double attack = 0.0;
    double energy = 0.0;
    double body = 0.0;
};

KickShape measureKickShape (const Buffer& buffer, double sampleRate, int latency)
{
    const auto period = (int) (0.5 * sampleRate);
    const auto attackSpan = (int) (0.008 * sampleRate);
    const auto energySpan = (int) (0.02 * sampleRate);
    const auto bodyStart = (int) (0.03 * sampleRate);
    const auto bodySpan = (int) (0.09 * sampleRate);

    KickShape shape;
    int hits = 0;

    for (int start = period + latency; start + period <= buffer.getNumSamples(); start += period)
    {
        double localAttack = 0.0;

        for (int i = 0; i < attackSpan; ++i)
            localAttack = juce::jmax (localAttack, (double) std::abs (buffer.getSample (0, start + i)));

        shape.attack += localAttack;
        shape.energy += rms (buffer.getReadPointer (0) + start, energySpan);
        shape.body += rms (buffer.getReadPointer (0) + start + bodyStart, bodySpan);
        ++hits;
    }

    if (hits > 0)
    {
        shape.attack /= hits;
        shape.energy /= hits;
        shape.body /= hits;
    }

    return shape;
}

void testTransientRetention()
{
    section ("transient retention");

    const auto sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 4.0);

    auto source = makeBuffer (2, length);
    fillKick (source, sampleRate, 0.3f);

    const auto reference = measureKickShape (source, sampleRate, 0);

    auto measure = [&] (float y)
    {
        Buffer processed (source);

        xyb::BassEngine engine;
        engine.prepare (sampleRate, 256, 2);
        auto parameters = position (0.35f, y);
        parameters.autoGain = false;
        engine.setParameters (parameters);
        render (engine, processed, 256);

        const auto shape = measureKickShape (processed, sampleRate, engine.getLatencySamples());

        report ("attack peak at " + juce::String (y, 2), relativeDb (shape.attack, reference.attack));
        report ("attack energy at " + juce::String (y, 2), relativeDb (shape.energy, reference.energy));
        report ("body gain at " + juce::String (y, 2), relativeDb (shape.body, reference.body));

        check (relativeDb (shape.attack, reference.attack) > -4.0,
               "the attack peak survives at y = " + juce::String (y, 2));

        return relativeDb (shape.energy, reference.energy);
    };

    const auto clean = measure (0.0f);
    const auto moderate = measure (0.5f);
    const auto strong = measure (0.75f);
    const auto extreme = measure (1.0f);

    check (clean > -2.0, "clean settings leave the kick attack alone");
    check (moderate > clean - 1.0, "moderate dirt does not squash the kick attack");
    check (strong > clean - 1.5, "strong dirt does not squash the kick attack");
    check (extreme > clean - 3.0, "extreme dirt softens but does not destroy the attack");
}

void testHighFrequencyClicksLeaveTheBassAlone()
{
    section ("clicks above the bass");

    constexpr double sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 3.0);

    // Two millisecond 3 kHz clicks, like a closed hi-hat, over a steady bass note. Their edges are
    // smoothed so that the clicks themselves put no energy into the bass region.
    auto fill = [&] (Buffer& buffer, bool clicks)
    {
        for (int i = 0; i < length; ++i)
        {
            auto value = 0.15 * std::sin (juce::MathConstants<double>::twoPi * 55.0 * i / sampleRate);
            const auto position = i % 6000;

            if (clicks && position < 96)
                value += 0.3 * std::sin (juce::MathConstants<double>::twoPi * 3000.0 * position / sampleRate)
                         * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * position / 96.0));

            for (int channel = 0; channel < 2; ++channel)
                buffer.setSample (channel, i, (float) value);
        }
    };

    auto lowPassed = [&] (const Buffer& buffer)
    {
        std::array<xyb::TptSvf, 2> filters;

        for (auto& filter : filters)
        {
            filter.prepare (sampleRate);
            filter.setQ (0.7071f);
            filter.setCutoff (400.0f);
        }

        std::vector<float> result ((size_t) length);

        for (int i = 0; i < length; ++i)
            result[(size_t) i] = filters[1].processLowPass (filters[0].processLowPass (buffer.getSample (0, i)));

        return result;
    };

    auto worst = -1000.0;

    for (const auto y : { 0.4f, 0.6f, 0.8f, 1.0f })
    {
        auto withClicks = makeBuffer (2, length);
        auto plain = makeBuffer (2, length);
        fill (withClicks, true);
        fill (plain, false);

        for (auto* buffer : { &withClicks, &plain })
        {
            xyb::BassEngine engine;
            engine.prepare (sampleRate, 256, 2);
            auto parameters = position (0.3f, y);
            parameters.autoGain = false;
            engine.setParameters (parameters);
            render (engine, *buffer, 256);
        }

        const auto clicked = lowPassed (withClicks);
        const auto reference = lowPassed (plain);
        auto deviation = 0.0;

        for (int i = (int) sampleRate; i < length; ++i)
            deviation = juce::jmax (deviation, (double) std::abs (clicked[(size_t) i] - reference[(size_t) i]));

        const auto level = rms (reference.data() + (int) sampleRate, length - (int) sampleRate) * std::sqrt (2.0);
        const auto relative = relativeDb (deviation, level);

        report ("bass deviation caused by clicks at y = " + juce::String (y, 1) + " (dB)", relative);
        worst = juce::jmax (worst, relative);
    }

    check (worst < -30.0, "clicks above the bass do not modulate how the bass is driven");
}

void testSubReinforcement()
{
    section ("sub reinforcement");

    xyb::BassEngine engine;
    engine.prepare (48000.0, 256, 2);
    engine.setParameters (position (0.0f, 0.0f));

    auto buffer = makeBuffer (2, 48000 * 3);
    fillSine (buffer, 45.0, 48000.0, 0.2f);
    Buffer reference (buffer);

    render (engine, buffer, 256);

    const auto start = 48000 * 2;
    const auto length = 48000;

    const auto processedLevel = magnitudeAt (buffer.getReadPointer (0) + start, length, 45.0, 48000.0);
    const auto referenceLevel = magnitudeAt (reference.getReadPointer (0) + start, length, 45.0, 48000.0);
    const auto gain = relativeDb (processedLevel, referenceLevel);

    report ("fundamental gain at clean sub", gain);
    check (gain > 1.5, "clean sub reinforces the fundamental");
    check (gain < 12.0, "clean sub reinforcement stays controlled");
}

void testSmallSpeakerTranslation()
{
    section ("small speaker translation");

    const auto sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 4.0);

    auto original = makeBuffer (2, length);
    fillNotePattern (original, 42.0, sampleRate, 0.45f, 0.5);

    Buffer boosted (original);
    Buffer translated (original);

    lowShelfBoost (boosted, 90.0, sampleRate, 6.0f);

    {
        xyb::BassEngine engine;
        engine.prepare (sampleRate, 256, 2);
        auto parameters = position (0.95f, 0.25f);
        parameters.autoGain = false;
        engine.setParameters (parameters);
        render (engine, translated, 256);
    }

    const auto sourceEnvelope = envelopeOf (original, sampleRate);

    Buffer originalSmall (original);
    Buffer boostedSmall (boosted);
    Buffer translatedSmall (translated);

    for (auto* buffer : { &originalSmall, &boostedSmall, &translatedSmall })
        highPassFourthOrder (*buffer, 120.0, sampleRate);

    const auto offset = (int) sampleRate;
    const auto span = length - offset;

    const auto originalLevel = rms (originalSmall.getReadPointer (0) + offset, span);
    const auto boostedLevel = rms (boostedSmall.getReadPointer (0) + offset, span);
    const auto translatedLevel = rms (translatedSmall.getReadPointer (0) + offset, span);

    report ("original above 120 Hz", relativeDb (originalLevel, 1.0));
    report ("boosted above 120 Hz", relativeDb (boostedLevel, 1.0));
    report ("translated above 120 Hz", relativeDb (translatedLevel, 1.0));
    report ("translation advantage", relativeDb (translatedLevel, originalLevel));

    check (relativeDb (translatedLevel, originalLevel) > 8.0,
           "translation survives a 120 Hz high pass far better than the source");
    check (relativeDb (translatedLevel, boostedLevel) > 6.0,
           "translation beats a plain low frequency boost on small speakers");

    const auto sourceModulation = modulationDepthDb (sourceEnvelope);
    const auto translatedEnvelope = envelopeOf (translatedSmall, sampleRate);
    const auto translatedRhythm = correlationOf (sourceEnvelope, translatedEnvelope);
    const auto translatedModulation = modulationDepthDb (translatedEnvelope);

    report ("source modulation depth", sourceModulation);
    report ("translated modulation depth", translatedModulation);
    report ("rhythm correlation of translation", translatedRhythm);

    check (translatedRhythm > 0.6, "translated content follows the shape of the original notes");
    check (translatedModulation > 18.0, "translated content is modulated by the notes, not a drone");
    check (std::abs (translatedModulation - sourceModulation) < 6.0,
           "the generated content is neither flattened nor exaggerated against the source dynamics");
}

void testAliasing()
{
    section ("alias rejection");

    const auto floorLevel = measureAliasing (0.5f, 0.5f, 48000.0, 137.0, 0.0f);
    const auto cleanTranslate = measureAliasing (1.0f, 0.0f, 48000.0, 137.0);
    const auto dirtyTranslate = measureAliasing (1.0f, 1.0f, 48000.0, 137.0);
    const auto dirtySub = measureAliasing (0.0f, 1.0f, 48000.0, 137.0);
    const auto highDirty = measureAliasing (1.0f, 1.0f, 48000.0, 287.0);
    const auto highDirtySub = measureAliasing (0.2f, 1.0f, 48000.0, 287.0);
    const auto baseRateDirty = measureAliasing (1.0f, 1.0f, 44100.0, 287.0);
    const auto baseRateDirtySub = measureAliasing (0.2f, 1.0f, 44100.0, 287.0);
    const auto topNoteDirty = measureAliasing (1.0f, 1.0f, 44100.0, 392.0);

    report ("measurement floor", floorLevel);
    report ("clean translate stray energy", cleanTranslate);
    report ("dirty translate stray energy", dirtyTranslate);
    report ("dirty sub stray energy", dirtySub);
    report ("high note dirty translate stray energy", highDirty);
    report ("high note dirty sub stray energy", highDirtySub);

    check (cleanTranslate < -62.0, "clean translate produces no measurable alias products");
    check (dirtyTranslate < -52.0, "dirty translate keeps alias products far below the harmonics");
    check (dirtySub < -52.0, "dirty sub keeps alias products far below the harmonics");
    check (highDirty < -48.0, "a high bass note still keeps alias products far below the harmonics");
    check (highDirtySub < -48.0, "a high saturated note keeps alias products far below the harmonics");

    report ("44.1 kHz dirty translate stray energy", baseRateDirty);
    report ("44.1 kHz dirty sub stray energy", baseRateDirtySub);
    report ("44.1 kHz top note stray energy", topNoteDirty);

    check (baseRateDirty < -48.0, "the lowest supported rate keeps alias products far below the harmonics");
    check (baseRateDirtySub < -48.0, "the lowest supported rate keeps sub alias products down");
    check (topNoteDirty < -46.0, "the top of the tracked range stays clean at the lowest supported rate");

    const auto loudDirtyTranslate = measureAliasing (1.0f, 1.0f, 48000.0, 137.0, 1.0f, 0.7f);
    const auto loudDirtySub = measureAliasing (0.0f, 1.0f, 48000.0, 137.0, 1.0f, 0.7f);
    const auto loudHighNote = measureAliasing (1.0f, 1.0f, 44100.0, 287.0, 1.0f, 0.7f);

    report ("-3 dBFS dirty translate stray energy", loudDirtyTranslate);
    report ("-3 dBFS dirty sub stray energy", loudDirtySub);
    report ("-3 dBFS high note at 44.1 kHz stray energy", loudHighNote);

    check (loudDirtyTranslate < -52.0, "a loud tone at dirty translate keeps alias products far below the harmonics");
    check (loudDirtySub < -52.0, "a loud tone at dirty sub keeps alias products far below the harmonics");
    check (loudHighNote < -46.0, "a loud high note at the lowest supported rate keeps alias products down");
}

void testSampleRateConsistency()
{
    section ("sample rate consistency");

    const std::array<double, 4> rates { { 44100.0, 48000.0, 96000.0, 192000.0 } };
    std::vector<double> ratios;

    for (auto sampleRate : rates)
    {
        const auto profile = measureHarmonics (0.9f, 0.35f, sampleRate, 50.0);
        const auto ratio = relativeDb (profile.second, profile.fundamental);
        ratios.push_back (ratio);
        report ("h2 at " + juce::String (sampleRate), ratio);
    }

    const auto minimum = *std::min_element (ratios.begin(), ratios.end());
    const auto maximum = *std::max_element (ratios.begin(), ratios.end());

    report ("h2 spread", maximum - minimum);
    check (maximum - minimum < 3.0, "harmonic balance is consistent across sample rates");
}

void testAutomationSmoothing()
{
    section ("automation smoothing");

    const auto sampleRate = 48000.0;
    const auto blockSize = 64;
    const auto length = (int) (sampleRate * 3.0);

    // mode 0 holds a position, mode 1 sweeps the whole pad four times a second and mode 2 jumps
    // to a random position every block, the worst a host can automate.
    auto renderAutomation = [&] (int mode, float fixedX, float fixedY)
    {
        xyb::BassEngine engine;
        engine.prepare (sampleRate, blockSize, 2);
        engine.setParameters (position (fixedX, fixedY));

        auto buffer = makeBuffer (2, length);
        fillSine (buffer, 50.0, sampleRate, 0.25f);

        juce::Random random (0xa070);
        float* pointers[2] = { nullptr, nullptr };

        for (int start = 0; start < length; start += blockSize)
        {
            const auto count = juce::jmin (blockSize, length - start);
            const auto seconds = (double) start / sampleRate;
            const auto sweep = (float) std::abs (2.0 * (seconds * 4.0 - std::floor (seconds * 4.0)) - 1.0);

            if (mode == 1)
                engine.setParameters (position (sweep, 1.0f - sweep));
            else if (mode == 2)
                engine.setParameters (position (random.nextFloat(), random.nextFloat()));

            for (int channel = 0; channel < 2; ++channel)
                pointers[channel] = buffer.getWritePointer (channel) + start;

            Buffer view (pointers, 2, count);
            engine.process (view);
        }

        return buffer;
    };

    // A click stands out against the steps around it, however loud the processed tone is.
    auto largestJump = [&] (const Buffer& buffer)
    {
        constexpr int neighbourhood = 96;
        const auto* data = buffer.getReadPointer (0);
        auto worst = 0.0;
        auto around = 0.0;

        for (int i = (int) sampleRate - neighbourhood; i <= (int) sampleRate + neighbourhood; ++i)
            around += std::abs ((double) data[i] - data[i - 1]);

        for (int i = (int) sampleRate; i < length - neighbourhood - 1; ++i)
        {
            const auto step = std::abs ((double) data[i] - data[i - 1]);
            worst = juce::jmax (worst, step * (2.0 * neighbourhood + 1.0) / juce::jmax (around, 1.0e-9));

            around += std::abs ((double) data[i + neighbourhood + 1] - data[i + neighbourhood])
                      - std::abs ((double) data[i - neighbourhood] - data[i - neighbourhood - 1]);
        }

        return worst;
    };

    // Zipper noise spreads far above the harmonics a 50 Hz tone gains at these settings.
    auto energyAbove = [&] (const Buffer& buffer, double frequency)
    {
        std::array<xyb::Biquad, 2> filters;

        for (size_t stage = 0; stage < filters.size(); ++stage)
        {
            filters[stage].prepare (sampleRate);
            filters[stage].setHighPass ((float) frequency, stage == 0 ? 0.5412f : 1.3066f);
        }

        double sum = 0.0;
        const auto* data = buffer.getReadPointer (0);

        for (int i = 0; i < length; ++i)
        {
            const auto value = (double) filters[1].process (filters[0].process (data[i]));

            if (i >= (int) sampleRate)
                sum += value * value;
        }

        return std::sqrt (sum / (double) (length - (int) sampleRate));
    };

    double staticJump = 0.0;
    double diagonalNoise = 0.0;
    double padNoise = 0.0;

    for (int gridX = 0; gridX <= 4; ++gridX)
    {
        for (int gridY = 0; gridY <= 4; ++gridY)
        {
            const auto rendered = renderAutomation (0, (float) gridX * 0.25f, (float) gridY * 0.25f);
            const auto noise = energyAbove (rendered, 6000.0);

            staticJump = juce::jmax (staticJump, largestJump (rendered));
            padNoise = juce::jmax (padNoise, noise);

            if (gridX + gridY == 4)
                diagonalNoise = juce::jmax (diagonalNoise, noise);
        }
    }

    const auto swept = renderAutomation (1, 0.5f, 0.5f);
    const auto jumping = renderAutomation (2, 0.5f, 0.5f);

    const auto sweptNoise = relativeDb (energyAbove (swept, 6000.0), diagonalNoise);
    const auto jumpingNoise = relativeDb (energyAbove (jumping, 6000.0), padNoise);

    report ("largest isolated step at a fixed position", staticJump);
    report ("largest isolated step under a fast sweep", largestJump (swept));
    report ("largest isolated step under per block jumps", largestJump (jumping));
    report ("energy above 6 kHz under a fast sweep, against its path held still (dB)", sweptNoise);
    report ("energy above 6 kHz under per block jumps, against the pad held still (dB)", jumpingNoise);

    check (largestJump (swept) < staticJump * 1.35 && largestJump (jumping) < staticJump * 1.35,
           "fast automation introduces no stepping");
    check (sweptNoise < 3.0 && jumpingNoise < 3.0, "fast automation adds no zipper noise");
}

void testDeterminism()
{
    section ("deterministic recall");

    auto renderOnce = [] (Buffer& buffer)
    {
        xyb::BassEngine engine;
        engine.prepare (48000.0, 128, 2);
        auto parameters = position (0.35f, 0.65f);
        parameters.mix = 0.8f;
        engine.setParameters (parameters);
        render (engine, buffer, 128);
    };

    auto first = makeBuffer (2, 48000);
    fillKick (first, 48000.0, 0.6f);
    Buffer second (first);

    renderOnce (first);
    renderOnce (second);

    double worst = 0.0;

    for (int i = 0; i < first.getNumSamples(); ++i)
        worst = juce::jmax (worst, (double) std::abs (first.getSample (0, i) - second.getSample (0, i)));

    check (juce::exactlyEqual (worst, 0.0), "identical settings produce identical output");
}

void testAutoGain()
{
    section ("auto gain");

    const auto sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 6.0);

    auto measure = [&] (bool enabled)
    {
        xyb::BassEngine engine;
        engine.prepare (sampleRate, 256, 2);
        auto parameters = position (0.85f, 0.9f);
        parameters.autoGain = enabled;
        engine.setParameters (parameters);

        auto buffer = makeBuffer (2, length);
        fillNoise (buffer, 0.12f, 7);
        lowShelfBoost (buffer, 120.0, sampleRate, 8.0f);

        Buffer source (buffer);
        render (engine, buffer, 256);

        const auto offset = (int) (sampleRate * 3.0);
        const auto span = length - offset;

        xyb::Biquad weighting;
        weighting.prepare (sampleRate);
        weighting.setHighPass (150.0f, 0.7071f);

        std::vector<float> processed ((size_t) span), reference ((size_t) span);

        for (int i = 0; i < span; ++i)
            processed[(size_t) i] = weighting.process (buffer.getSample (0, offset + i));

        weighting.reset();

        for (int i = 0; i < span; ++i)
            reference[(size_t) i] = weighting.process (source.getSample (0, offset + i));

        return relativeDb (rms (processed.data(), span), rms (reference.data(), span));
    };

    const auto uncompensated = measure (false);
    const auto compensated = measure (true);

    report ("weighted change without auto gain", uncompensated);
    report ("weighted change with auto gain", compensated);

    check (std::abs (compensated) < 1.5, "auto gain matches perceived loudness");
    check (std::abs (compensated) < std::abs (uncompensated) + 0.01, "auto gain improves the match");
}

void testHotInput()
{
    section ("hot and pathological input");

    xyb::BassEngine engine;
    engine.prepare (48000.0, 256, 2);
    auto parameters = position (0.4f, 1.0f);
    parameters.inputGainDb = 18.0f;
    engine.setParameters (parameters);

    auto buffer = makeBuffer (2, 48000 * 2);
    fillSine (buffer, 40.0, 48000.0, 1.0f);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        buffer.setSample (0, i, juce::jlimit (-0.7f, 0.7f, buffer.getSample (0, i)) * 1.4f);
        buffer.setSample (1, i, -buffer.getSample (0, i));
    }

    render (engine, buffer, 256);

    report ("clipped input output peak", peak (buffer));
    check (isFinite (buffer), "clipped anti-correlated input stays finite");
    check (peak (buffer) < 1.05, "output ceiling holds with extreme input");
}

struct Corner
{
    float x;
    float y;
    const char* name;
};

const Corner corners[] = { { 0.5f, 0.5f, "centre" }, { 0.0f, 0.0f, "clean sub" }, { 1.0f, 1.0f, "dirty translate" } };

xyb::BassEngine::Parameters parametersFor (const Corner& corner, float mix = 1.0f)
{
    auto parameters = position (corner.x, corner.y);
    parameters.mix = mix;
    return parameters;
}

void addInto (Buffer& target, const Buffer& source)
{
    for (int channel = 0; channel < target.getNumChannels(); ++channel)
        target.addFrom (channel, 0, source, channel, 0, target.getNumSamples());
}

void renderSchedule (xyb::BassEngine& engine, Buffer& buffer, const std::vector<int>& schedule)
{
    float* pointers[2] = { nullptr, nullptr };
    auto offset = 0;
    auto index = 0;

    while (offset < buffer.getNumSamples())
    {
        const auto count = juce::jmin (schedule[(size_t) index++ % schedule.size()], buffer.getNumSamples() - offset);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            pointers[channel] = buffer.getWritePointer (channel) + offset;

        Buffer view (pointers, buffer.getNumChannels(), count);
        engine.process (view);
        offset += count;
    }
}

double peakBetween (const Buffer& buffer, int start, int end)
{
    auto result = 0.0;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = juce::jmax (0, start); i < juce::jmin (end, buffer.getNumSamples()); ++i)
            result = juce::jmax (result, (double) std::abs (buffer.getSample (channel, i)));

    return result;
}

void testDryPathIsSampleExact()
{
    section ("dry path at mix zero");

    auto worst = 0.0;
    auto latencyReported = true;

    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        for (const auto& corner : corners)
        {
            xyb::BassEngine engine;
            engine.setParameters (parametersFor (corner, 0.0f));
            engine.prepare (sampleRate, 256, 2);

            auto buffer = makeBuffer (2, (int) sampleRate);
            fillKick (buffer, sampleRate, 0.4f);
            auto noise = makeBuffer (2, buffer.getNumSamples());
            fillNoise (noise, 0.2f, 7);
            addInto (buffer, noise);

            Buffer input (buffer);
            render (engine, buffer, 256);

            const auto latency = engine.getLatencySamples();
            latencyReported = latencyReported && latency > 0;

            for (int channel = 0; channel < 2; ++channel)
                for (int i = latency; i < buffer.getNumSamples(); ++i)
                    worst = juce::jmax (worst, (double) std::abs (buffer.getSample (channel, i)
                                                                  - input.getSample (channel, i - latency)));
        }
    }

    report ("worst dry path difference (dB)", juce::Decibels::gainToDecibels (worst, -200.0));
    check (latencyReported, "the oversampled path reports its latency");
    check (juce::exactlyEqual (worst, 0.0), "mix at zero returns the input sample for sample at every corner and rate");
}

void testSilenceIsSilent()
{
    section ("silence in, silence out");

    auto loudest = 0.0;

    for (const auto sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        for (const auto& corner : corners)
        {
            xyb::BassEngine engine;
            engine.setParameters (parametersFor (corner));
            engine.prepare (sampleRate, 256, 2);

            auto buffer = makeBuffer (2, (int) (sampleRate * 2.0));
            render (engine, buffer, 256);
            loudest = juce::jmax (loudest, peak (buffer));
        }
    }

    report ("loudest sample from silence (dB)", juce::Decibels::gainToDecibels (loudest, -200.0));
    check (juce::exactlyEqual (loudest, 0.0), "silence in gives exact silence out at every corner and rate");
}

void testBlockScheduleIndependence()
{
    section ("host block schedule");

    const std::vector<std::vector<int>> schedules { { 32 }, { 64 }, { 512 }, { 1, 7, 64, 333, 512, 2048, 5, 256 } };
    auto worst = 0.0;

    for (const auto sampleRate : { 44100.0, 48000.0 })
    {
        for (const auto& corner : corners)
        {
            auto source = makeBuffer (2, (int) (sampleRate * 3.0));
            fillKick (source, sampleRate, 0.45f);
            auto tone = makeBuffer (2, source.getNumSamples());
            fillSine (tone, 55.0, sampleRate, 0.25f);
            addInto (source, tone);

            Buffer reference (source);

            {
                xyb::BassEngine engine;
                engine.setParameters (parametersFor (corner));
                engine.prepare (sampleRate, 2048, 2);
                render (engine, reference, 256);
            }

            for (const auto& schedule : schedules)
            {
                Buffer rendered (source);
                xyb::BassEngine engine;
                engine.setParameters (parametersFor (corner));
                engine.prepare (sampleRate, 2048, 2);
                renderSchedule (engine, rendered, schedule);

                for (int channel = 0; channel < 2; ++channel)
                    for (int i = 0; i < rendered.getNumSamples(); ++i)
                        worst = juce::jmax (worst, (double) std::abs (rendered.getSample (channel, i)
                                                                      - reference.getSample (channel, i)));
            }
        }
    }

    report ("worst difference between block schedules (dB)", juce::Decibels::gainToDecibels (worst, -200.0));
    check (juce::exactlyEqual (worst, 0.0), "output is identical for every host block schedule");
}

void testStartupHasNoLevelBurst()
{
    section ("startup level");

    auto worstBurst = -1000.0;

    for (const auto& corner : corners)
    {
        xyb::BassEngine engine;
        engine.setParameters (parametersFor (corner));
        engine.prepare (48000.0, 256, 2);

        auto buffer = makeBuffer (2, 48000 * 3);
        fillSine (buffer, 55.0, 48000.0, 0.3f);
        render (engine, buffer, 256);

        const auto latency = engine.getLatencySamples();
        const auto early = peakBetween (buffer, latency, latency + 4800);
        const auto steady = peakBetween (buffer, 96000, 144000);

        worstBurst = juce::jmax (worstBurst, relativeDb (early, steady));
    }

    report ("worst early peak over settled peak (dB)", worstBurst);
    check (worstBurst < 3.0, "the first hundred milliseconds stay near the settled level");

    auto worstGainBurst = -1000.0;

    for (const auto& [inputDb, outputDb] : { std::pair<float, float> { 0.0f, -18.0f }, { -18.0f, 0.0f }, { 12.0f, -12.0f } })
    {
        xyb::BassEngine engine;
        auto parameters = parametersFor (corners[0]);
        parameters.inputGainDb = inputDb;
        parameters.outputGainDb = outputDb;
        engine.setParameters (parameters);
        engine.prepare (48000.0, 256, 2);

        auto buffer = makeBuffer (2, 48000 * 3);
        fillSine (buffer, 55.0, 48000.0, 0.1f);
        render (engine, buffer, 256);

        const auto latency = engine.getLatencySamples();
        const auto early = peakBetween (buffer, latency, latency + 1440);
        const auto steady = peakBetween (buffer, 96000, 144000);

        worstGainBurst = juce::jmax (worstGainBurst, relativeDb (early, steady));
    }

    report ("worst peak in the first 30 ms with input or output gain set (dB)", worstGainBurst);
    check (worstGainBurst < 3.0, "input and output gain apply from the first sample after preparing");

    auto lowest = 1000.0;
    auto highest = -1000.0;

    for (int offset = 0; offset < 256; offset += 8)
    {
        xyb::BassEngine engine;
        engine.setParameters (parametersFor (corners[1]));
        engine.prepare (48000.0, 256, 2);

        auto buffer = makeBuffer (2, 48000 * 3);
        auto tone = makeBuffer (2, buffer.getNumSamples() - offset);
        fillSine (tone, 45.0, 48000.0, 0.2f);

        for (int channel = 0; channel < 2; ++channel)
            buffer.copyFrom (channel, offset, tone, channel, 0, tone.getNumSamples());

        Buffer reference (buffer);
        render (engine, buffer, 256);

        const auto gain = relativeDb (magnitudeAt (buffer.getReadPointer (0) + 96000, 48000, 45.0, 48000.0),
                                      magnitudeAt (reference.getReadPointer (0) + 96000, 48000, 45.0, 48000.0));
        lowest = juce::jmin (lowest, gain);
        highest = juce::jmax (highest, gain);
    }

    report ("settled gain spread across onset positions (dB)", highest - lowest);
    check (highest - lowest < 1.0, "the settled level does not depend on where the first note lands");
}

void testColdStartLevel()
{
    section ("level from a cold start");

    auto worstShortfall = 0.0;

    for (const auto& [corner, frequency] : { std::pair<Corner, double> { { 0.0f, 0.3f, "sub" }, 45.0 },
                                             { { 0.0f, 0.0f, "clean sub" }, 60.0 },
                                             { { 0.5f, 0.5f, "centre" }, 55.0 } })
    {
        xyb::BassEngine engine;
        engine.setParameters (parametersFor (corner));
        engine.prepare (48000.0, 256, 2);

        // An offline render starts on the note, so its first moments are heard exactly as rendered.
        auto buffer = makeBuffer (2, 48000 * 4);
        fillSine (buffer, frequency, 48000.0, 0.2f);
        render (engine, buffer, 256);

        const auto early = peakBetween (buffer, 9600, 19200);
        const auto settled = peakBetween (buffer, 144000, 192000);
        const auto shortfall = relativeDb (settled, early);

        report (juce::String ("level from 200 to 400 ms below the settled level at ") + corner.name + " (dB)", shortfall);
        worstShortfall = juce::jmax (worstShortfall, shortfall);
    }

    check (worstShortfall < 1.5, "a fresh render reaches its settled level within a few hundred milliseconds");
}

void testRecoveryFromAbsurdInput()
{
    section ("recovery from absurd input");

    constexpr double sampleRate = 48000.0;
    const auto spikeAt = (int) sampleRate;
    const auto length = (int) sampleRate * 3;

    auto source = makeBuffer (2, length);
    fillSine (source, 55.0, sampleRate, 0.2f);

    Buffer reference (source);

    {
        xyb::BassEngine engine;
        engine.prepare (sampleRate, 256, 2);
        engine.setParameters (position (0.5f, 0.5f));
        render (engine, reference, 256);
    }

    const auto referencePeak = peakBetween (reference, spikeAt, length);
    auto worstOvershoot = -1000.0;
    auto worstShortfall = -1000.0;
    auto finite = true;

    for (const auto spike : { 1.0e3f, 1.0e8f, 1.0e20f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN() })
    {
        Buffer buffer (source);

        for (int channel = 0; channel < 2; ++channel)
            buffer.setSample (channel, spikeAt, spike);

        xyb::BassEngine engine;
        engine.prepare (sampleRate, 256, 2);
        engine.setParameters (position (0.5f, 0.5f));
        render (engine, buffer, 256);

        finite = finite && isFinite (buffer);

        // Every 100 ms window from 100 ms after the sample onwards must be back near the level of
        // a render that never saw it.
        for (int start = spikeAt + (int) (0.1 * sampleRate); start + 4800 <= length; start += 4800)
        {
            const auto level = relativeDb (peakBetween (buffer, start, start + 4800), referencePeak);
            worstOvershoot = juce::jmax (worstOvershoot, level);
            worstShortfall = juce::jmax (worstShortfall, -level);
        }
    }

    report ("loudest window after an absurd sample, against clean playback (dB)", worstOvershoot);
    report ("quietest window after an absurd sample, against clean playback (dB)", -worstShortfall);

    check (finite, "absurd input never reaches the output as a non-finite value");
    check (worstOvershoot < 1.5, "the output recovers from an absurd sample within 100 ms");
    check (worstShortfall < 6.0, "the output resumes playing after an absurd sample");
}

void testImpulseDecay()
{
    section ("impulse decay");

    auto worst = 0.0;

    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        for (const auto& corner : corners)
        {
            xyb::BassEngine engine;
            engine.setParameters (parametersFor (corner));
            engine.prepare (sampleRate, 256, 2);

            auto buffer = makeBuffer (2, (int) (sampleRate * 3.0));
            const auto impulseAt = (int) (sampleRate * 0.1);

            for (int channel = 0; channel < 2; ++channel)
                buffer.setSample (channel, impulseAt, 0.5f);

            render (engine, buffer, 256);
            worst = juce::jmax (worst, peakBetween (buffer, impulseAt + (int) sampleRate, buffer.getNumSamples()));
        }
    }

    report ("loudest sample one second after an impulse (dB)", juce::Decibels::gainToDecibels (worst, -200.0));
    check (worst < 1.0e-5, "an impulse decays away instead of ringing");
}

void testRealtimeSafety()
{
    section ("real time safety");

    xyb::BassEngine engine;
    engine.prepare (48000.0, 128, 2);
    engine.setParameters (position (0.6f, 0.6f));

    auto warmUp = makeBuffer (2, 48000);
    fillKick (warmUp, 48000.0, 0.5f);
    render (engine, warmUp, 128);

    auto buffer = makeBuffer (2, 48000);
    fillKick (buffer, 48000.0, 0.5f);

    float* pointers[2] = { nullptr, nullptr };

    allocationCount.store (0);
    allocationTracking.store (true);

    for (int start = 0; start + 128 <= buffer.getNumSamples(); start += 128)
    {
        engine.setParameters (position (0.6f + 0.001f * (float) (start % 100), 0.6f));

        for (int channel = 0; channel < 2; ++channel)
            pointers[channel] = buffer.getWritePointer (channel) + start;

        Buffer view (pointers, 2, 128);
        engine.process (view);
    }

    allocationTracking.store (false);

    report ("allocations during processing", allocationCount.load());
    check (allocationCount.load() == 0, "the audio path performs no allocation");

    auto measure = [] (auto&& action)
    {
        allocationCount.store (0);
        allocationTracking.store (true);
        action();
        allocationTracking.store (false);
        return allocationCount.load();
    };

    const auto viaOperatorNew = measure ([]
    {
        std::vector<float> probe (256, 1.0f);
        escape (probe.data());
    });

    const auto viaSystemAllocator = measure ([]
    {
        juce::AudioBuffer<float> probe (2, 256);
        probe.clear();
        escape (probe.getWritePointer (0));
    });

    report ("allocations seen for a vector", viaOperatorNew);
    report ("allocations seen for an audio buffer", viaSystemAllocator);

    check (viaOperatorNew > 0, "the allocation counter sees a standard allocation");
    check (viaSystemAllocator > 0, "the allocation counter sees an audio buffer being sized");
}

} // namespace

void* operator new (size_t size)
{
    countAllocation();
    return std::malloc (size == 0 ? 1 : size);
}

void* operator new[] (size_t size)
{
    countAllocation();
    return std::malloc (size == 0 ? 1 : size);
}

#if defined (__linux__)

extern "C" void* __real_malloc (size_t);
extern "C" void* __real_calloc (size_t, size_t);
extern "C" void* __real_realloc (void*, size_t);
extern "C" void* __wrap_malloc (size_t);
extern "C" void* __wrap_calloc (size_t, size_t);
extern "C" void* __wrap_realloc (void*, size_t);

extern "C" void* __wrap_malloc (size_t size)
{
    countAllocation();
    return __real_malloc (size);
}

extern "C" void* __wrap_calloc (size_t count, size_t size)
{
    countAllocation();
    return __real_calloc (count, size);
}

extern "C" void* __wrap_realloc (void* pointer, size_t size)
{
    countAllocation();
    return __real_realloc (pointer, size);
}

#endif

void operator delete (void* pointer) noexcept { std::free (pointer); }
void operator delete[] (void* pointer) noexcept { std::free (pointer); }
void operator delete (void* pointer, size_t) noexcept { std::free (pointer); }
void operator delete[] (void* pointer, size_t) noexcept { std::free (pointer); }

int runEngineTests()
{
    installAllocationHooks();

    testShaperCharacter();
    testBandReconstruction();
    testStability();
    testHighSampleRateFilters();
    testLongHighSampleRateRuns();
    testDegenerateSetup();
    testHalfbandOversampler();
    testFilterReplacements();
    testSilenceAndDenormals();
    testDcRejection();
    testDryPathAlignment();
    testOversizedBlocks();
    testPartialMix();
    testDeltaMonitoring();
    testCleanSettingsAtHighLevel();
    testCeilingContinuity();
    testDeltaToggleIsSmooth();
    testMonoStereoConsistency();
    testHarmonicStructure();
    testSubReinforcement();
    testHighFrequencyClicksLeaveTheBassAlone();
    testTransientRetention();
    testSmallSpeakerTranslation();
    testAliasing();
    testSampleRateConsistency();
    testAutomationSmoothing();
    testDeterminism();
    testAutoGain();
    testHotInput();
    testDryPathIsSampleExact();
    testSilenceIsSilent();
    testBlockScheduleIndependence();
    testStartupHasNoLevelBurst();
    testColdStartLevel();
    testRecoveryFromAbsurdInput();
    testImpulseDecay();
    testRealtimeSafety();

    std::cout << std::endl
              << (failures == 0 ? "PASSED " : "FAILED ")
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
