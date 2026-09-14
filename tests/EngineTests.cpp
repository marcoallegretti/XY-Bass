#include "BassEngine.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

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

double measureAliasing (float x, float y, double sampleRate, double frequency, float mix = 1.0f)
{
    xyb::BassEngine engine;
    engine.prepare (sampleRate, 256, 1);

    auto parameters = position (x, y);
    parameters.mix = mix;
    engine.setParameters (parameters);

    constexpr int order = 15;
    constexpr int size = 1 << order;

    auto buffer = makeBuffer (1, (int) (sampleRate * 2.0) + size);
    fillSine (buffer, frequency, sampleRate, 0.25f);
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

HarmonicProfile measureHarmonics (float x, float y, double sampleRate, double frequency)
{
    xyb::BassEngine engine;
    engine.prepare (sampleRate, 256, 2);
    engine.setParameters (position (x, y));

    const auto totalSamples = (int) (sampleRate * 3.0);
    auto buffer = makeBuffer (2, totalSamples);
    fillSine (buffer, frequency, sampleRate, 0.25f);
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

    const auto sampleRate = 48000.0;
    const auto length = 1 << 15;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) length, 1 };
    double worst = 0.0;

    for (auto frequency : { 25.0, 40.0, 60.0, 100.0, 150.0, 220.0, 350.0, 500.0, 800.0, 2000.0, 8000.0 })
    {
        xyb::BandSplitter splitter;
        splitter.prepare (spec);
        splitter.setCrossovers (150.0f, 500.0f);

        auto source = makeBuffer (1, length);
        fillSine (source, frequency, sampleRate, 0.25f);

        std::vector<float> summed ((size_t) length);

        for (int i = 0; i < length; ++i)
        {
            const auto bands = splitter.process (0, source.getSample (0, i));
            summed[(size_t) i] = bands.low + bands.mid + bands.character;
        }

        const auto skip = 8192;
        const auto periods = std::floor ((double) (length - skip) * frequency / sampleRate);
        const auto span = (int) std::floor (periods * sampleRate / frequency);
        const auto reference = rms (source.getReadPointer (0) + skip, span);
        const auto reconstructed = rms (summed.data() + skip, span);
        worst = juce::jmax (worst, std::abs (relativeDb (reconstructed, reference)));
    }

    report ("worst reconstruction error", worst);
    check (worst < 0.05, "the band split recombines to a flat magnitude response");
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

    const std::array<double, 4> rates { { 44100.0, 48000.0, 96000.0, 192000.0 } };

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
    check (peak (pureSilence) == 0.0, "silence into a reset engine produces exact zero");
}

void testDcRejection()
{
    section ("dc rejection");

    xyb::BassEngine engine;
    engine.prepare (48000.0, 256, 2);
    engine.setParameters (position (0.0f, 1.0f));

    auto buffer = makeBuffer (2, 48000 * 2);

    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), 0.5f, buffer.getNumSamples());

    render (engine, buffer, 256);

    double sum = 0.0;
    const auto* data = buffer.getReadPointer (0) + 48000;

    for (int i = 0; i < 48000; ++i)
        sum += data[i];

    const auto offset = std::abs (sum / 48000.0);
    report ("residual dc", offset);
    check (offset < 0.002, "dc offset is removed");
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

void testDeltaMatchesDifference()
{
    section ("delta monitoring");

    auto source = makeBuffer (2, 48000);
    fillNotePattern (source, 55.0, 48000.0, 0.5f, 0.5);

    Buffer processed (source);
    Buffer delta (source);

    {
        xyb::BassEngine engine;
        engine.prepare (48000.0, 256, 2);
        auto parameters = position (0.75f, 0.55f);
        parameters.autoGain = false;
        engine.setParameters (parameters);
        render (engine, processed, 256);
    }

    {
        xyb::BassEngine engine;
        engine.prepare (48000.0, 256, 2);
        auto parameters = position (0.75f, 0.55f);
        parameters.autoGain = false;
        parameters.delta = true;
        engine.setParameters (parameters);
        render (engine, delta, 256);
    }

    xyb::BassEngine reference;
    reference.prepare (48000.0, 256, 2);
    const auto latency = reference.getLatencySamples();

    double worst = 0.0;

    for (int i = latency + 512; i < source.getNumSamples(); ++i)
    {
        const auto expected = processed.getSample (0, i) - source.getSample (0, i - latency);
        worst = juce::jmax (worst, (double) std::abs (delta.getSample (0, i) - expected));
    }

    report ("delta error", worst);
    check (worst < 1.0e-4, "delta equals processed minus aligned dry");
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
    const auto length = (int) (sampleRate * 2.5);

    auto measureSlew = [&] (int mode, float fixedX, float fixedY)
    {
        xyb::BassEngine engine;
        engine.prepare (sampleRate, blockSize, 2);
        engine.setParameters (position (fixedX, fixedY));

        auto buffer = makeBuffer (2, length);
        fillSine (buffer, 50.0, sampleRate, 0.25f);

        float* pointers[2] = { nullptr, nullptr };
        int blockIndex = 0;

        for (int start = 0; start < length; start += blockSize)
        {
            const auto count = juce::jmin (blockSize, length - start);

            const auto phase = (float) blockIndex * 0.02f;
            const auto sweptX = mode == 1 ? 0.5f + 0.5f * std::sin (phase) : fixedX;
            const auto sweptY = mode == 1 ? 0.5f + 0.5f * std::sin (phase * 1.7f) : fixedY;
            engine.setParameters (position (sweptX, sweptY));

            for (int channel = 0; channel < 2; ++channel)
                pointers[channel] = buffer.getWritePointer (channel) + start;

            Buffer view (pointers, 2, count);
            engine.process (view);
            ++blockIndex;
        }

        double worst = 0.0;
        const auto* data = buffer.getReadPointer (0);

        for (int i = (int) sampleRate; i < length; ++i)
            worst = juce::jmax (worst, (double) std::abs (data[i] - data[i - 1]));

        return worst;
    };

    double staticWorst = 0.0;

    for (int grid = 0; grid <= 4; ++grid)
    {
        const auto value = (float) grid * 0.25f;
        staticWorst = juce::jmax (staticWorst, measureSlew (0, value, 0.5f));
        staticWorst = juce::jmax (staticWorst, measureSlew (0, 0.5f, value));
    }

    const auto automatedSlew = measureSlew (1, 0.5f, 0.5f);

    report ("worst static slew", staticWorst);
    report ("automated slew", automatedSlew);
    check (automatedSlew < staticWorst * 1.35,
           "continuous automation introduces no stepping");
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

    check (worst == 0.0, "identical settings produce identical output");
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

int main()
{
    installAllocationHooks();

    testShaperCharacter();
    testBandReconstruction();
    testStability();
    testDegenerateSetup();
    testSilenceAndDenormals();
    testDcRejection();
    testDryPathAlignment();
    testOversizedBlocks();
    testDeltaMatchesDifference();
    testMonoStereoConsistency();
    testHarmonicStructure();
    testSubReinforcement();
    testTransientRetention();
    testSmallSpeakerTranslation();
    testAliasing();
    testSampleRateConsistency();
    testAutomationSmoothing();
    testDeterminism();
    testAutoGain();
    testHotInput();
    testRealtimeSafety();

    std::cout << std::endl
              << (failures == 0 ? "PASSED " : "FAILED ")
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
