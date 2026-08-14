#include "TestSignals.h"

#include <chrono>
#include <limits>
#include <iostream>

using namespace xyb::testing;

namespace
{

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

double measureRealtimeCost (double sampleRate, int blockSize, int numChannels, float x, float y, int seconds)
{
    xyb::BassEngine engine;
    engine.prepare (sampleRate, blockSize, numChannels);
    engine.setParameters (position (x, y));

    const auto totalSamples = (int) (sampleRate * seconds);
    auto buffer = makeBuffer (numChannels, totalSamples);
    fillFullMix (buffer, sampleRate, 0.35f);

    auto warmUp = makeBuffer (numChannels, blockSize * 64);
    fillFullMix (warmUp, sampleRate, 0.35f);
    render (engine, warmUp, blockSize);

    std::array<float*, 2> pointers { { nullptr, nullptr } };
    auto best = std::numeric_limits<double>::max();

    for (int repeat = 0; repeat < 5; ++repeat)
    {
        const auto start = std::chrono::steady_clock::now();

        for (int offset = 0; offset + blockSize <= totalSamples; offset += blockSize)
        {
            for (int channel = 0; channel < numChannels; ++channel)
                pointers[(size_t) channel] = buffer.getWritePointer (channel) + offset;

            Buffer view (pointers.data(), numChannels, blockSize);
            engine.process (view);
        }

        best = juce::jmin (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count());
    }

    return 100.0 * best / (double) seconds;
}

double measureReferenceCost (double sampleRate, int blockSize, int seconds)
{
    const auto totalSamples = (int) (sampleRate * seconds);
    auto buffer = makeBuffer (2, totalSamples);
    fillFullMix (buffer, sampleRate, 0.35f);

    std::array<std::array<xyb::Biquad, 8>, 2> filters;

    for (auto& channelFilters : filters)
    {
        auto index = 0;

        for (auto& filter : channelFilters)
        {
            filter.prepare (sampleRate);
            filter.setPeaking (80.0f * (float) (index + 1), 1.0f, 3.0f);
            ++index;
        }
    }

    auto best = std::numeric_limits<double>::max();

    for (int repeat = 0; repeat < 5; ++repeat)
    {
        const auto start = std::chrono::steady_clock::now();

        for (int offset = 0; offset + blockSize <= totalSamples; offset += blockSize)
            for (int channel = 0; channel < 2; ++channel)
            {
                auto* data = buffer.getWritePointer (channel) + offset;

                for (auto& filter : filters[(size_t) channel])
                    for (int i = 0; i < blockSize; ++i)
                        data[i] = filter.process (data[i]);
            }

        best = juce::jmin (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count());
    }

    return 100.0 * best / (double) seconds;
}

struct BlockCostProfile
{
    double median = 0.0;
    double upper = 0.0;
    double worst = 0.0;
};

BlockCostProfile measureBlockProfile (double sampleRate, int blockSize, float x, float y)
{
    xyb::BassEngine engine;
    engine.prepare (sampleRate, blockSize, 2);
    engine.setParameters (position (x, y));

    const auto totalSamples = (int) (sampleRate * 4.0);
    auto buffer = makeBuffer (2, totalSamples);
    fillFullMix (buffer, sampleRate, 0.35f);

    auto warmUp = makeBuffer (2, blockSize * 64);
    fillFullMix (warmUp, sampleRate, 0.35f);
    render (engine, warmUp, blockSize);

    std::array<float*, 2> pointers { { nullptr, nullptr } };
    std::vector<double> costs;

    for (int offset = 0; offset + blockSize <= totalSamples; offset += blockSize)
    {
        for (int channel = 0; channel < 2; ++channel)
            pointers[(size_t) channel] = buffer.getWritePointer (channel) + offset;

        Buffer view (pointers.data(), 2, blockSize);

        const auto start = std::chrono::steady_clock::now();
        engine.process (view);
        costs.push_back (std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count());
    }

    std::sort (costs.begin(), costs.end());

    BlockCostProfile profile;
    profile.median = costs[costs.size() / 2];
    profile.upper = costs[(size_t) ((double) costs.size() * 0.995)];
    profile.worst = costs.back();

    return profile;
}

void testSteadyStateCost()
{
    section ("processing cost");

    const auto cleanCentre = measureRealtimeCost (48000.0, 128, 2, 0.5f, 0.5f, 2);
    const auto dirtyCorner = measureRealtimeCost (48000.0, 128, 2, 1.0f, 1.0f, 2);
    const auto cleanSub = measureRealtimeCost (48000.0, 128, 2, 0.0f, 0.0f, 2);
    const auto monoCost = measureRealtimeCost (48000.0, 128, 1, 0.5f, 0.5f, 2);
    const auto highRate = measureRealtimeCost (96000.0, 128, 2, 0.5f, 0.5f, 2);
    const auto largeBlock = measureRealtimeCost (48000.0, 1024, 2, 0.5f, 0.5f, 2);

    report ("stereo centre at 48 kHz (% of one core)", cleanCentre);
    report ("stereo dirty translate at 48 kHz", dirtyCorner);
    report ("stereo clean sub at 48 kHz", cleanSub);
    report ("mono centre at 48 kHz", monoCost);
    report ("stereo centre at 96 kHz", highRate);
    report ("stereo centre with 1024 sample blocks", largeBlock);

    const auto reference = measureReferenceCost (48000.0, 128, 2);
    report ("reference eight biquad cascade", reference);
    report ("centre cost relative to reference", cleanCentre / reference);
    report ("dirty corner relative to reference", dirtyCorner / reference);
    report ("96 kHz relative to reference", highRate / reference);

    check (cleanCentre / reference < 14.0, "the engine costs a bounded multiple of a plain filter chain");
    check (dirtyCorner / reference < 16.0, "the most expensive pad position stays bounded");
    check (highRate / reference < 26.0, "a doubled sample rate stays bounded");
    check (monoCost < cleanCentre * 1.15, "mono does not cost more than stereo");
}

void testBlockCostConsistency()
{
    section ("block cost consistency");

    const auto small = measureBlockProfile (48000.0, 64, 0.5f, 0.5f);
    const auto typical = measureBlockProfile (48000.0, 256, 0.5f, 0.5f);

    const auto smallSpread = small.upper / juce::jmax (small.median, 1.0e-9);
    const auto typicalSpread = typical.upper / juce::jmax (typical.median, 1.0e-9);

    report ("64 sample blocks, 99.5th percentile over median", smallSpread);
    report ("256 sample blocks, 99.5th percentile over median", typicalSpread);
    report ("64 sample median as % of the block period", 100.0 * small.median / (64.0 / 48000.0));
    report ("256 sample median as % of the block period", 100.0 * typical.median / (256.0 / 48000.0));

    check (smallSpread < 8.0, "small blocks cost a consistent amount");
    check (typicalSpread < 5.0, "typical blocks cost a consistent amount");
    check (100.0 * typical.median / (256.0 / 48000.0) < 40.0,
           "a typical block leaves most of its deadline unused");
}

void testAnalysisAmortisation()
{
    section ("analysis amortisation");

    const auto blockSize = 64;
    const auto sampleRate = 48000.0;

    xyb::BassEngine engine;
    engine.prepare (sampleRate, blockSize, 2);
    engine.setParameters (position (0.5f, 0.5f));

    const auto totalSamples = (int) (sampleRate * 4.0);
    auto buffer = makeBuffer (2, totalSamples);
    fillSaw (buffer, 55.0, sampleRate, 0.2f);

    auto warmUp = makeBuffer (2, blockSize * 64);
    fillSaw (warmUp, 55.0, sampleRate, 0.2f);
    render (engine, warmUp, blockSize);

    std::vector<double> costs;
    std::array<float*, 2> pointers { { nullptr, nullptr } };

    for (int offset = 0; offset + blockSize <= totalSamples; offset += blockSize)
    {
        for (int channel = 0; channel < 2; ++channel)
            pointers[(size_t) channel] = buffer.getWritePointer (channel) + offset;

        Buffer view (pointers.data(), 2, blockSize);

        const auto start = std::chrono::steady_clock::now();
        engine.process (view);
        costs.push_back (std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count());
    }

    std::sort (costs.begin(), costs.end());

    const auto median = costs[costs.size() / 2];
    const auto upper = costs[(size_t) ((double) costs.size() * 0.99)];

    report ("cost spread, 99th percentile over median", upper / juce::jmax (median, 1.0e-9));

    check (upper < median * 6.0, "pitch analysis does not create per block cost spikes");
}

} // namespace

int main()
{
    testSteadyStateCost();
    testBlockCostConsistency();
    testAnalysisAmortisation();

    std::cout << std::endl
              << (failures == 0 ? "PASSED " : "FAILED ")
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
