#pragma once

#include "DspUtility.h"

namespace xyb
{

class AutoGain
{
public:
    void prepare (double sampleRate)
    {
        const std::array<float, 2> qualities { { 0.5412f, 1.3066f } };

        for (auto* path : { &dryWeighting, &wetWeighting })
        {
            for (size_t stage = 0; stage < path->size(); ++stage)
            {
                (*path)[stage].prepare (sampleRate);
                (*path)[stage].setHighPass (150.0f, qualities[stage]);
            }
        }

        for (auto* filter : { &dryShelf, &wetShelf })
        {
            filter->prepare (sampleRate);
            filter->setHighShelf (1800.0f, 0.7071f, 4.0f);
        }

        drySquared.prepare (sampleRate, 320.0f);
        wetSquared.prepare (sampleRate, 320.0f);
        output.prepare (sampleRate, 240.0f);

        reset();
    }

    void reset()
    {
        for (auto* path : { &dryWeighting, &wetWeighting })
            for (auto& filter : *path)
                filter.reset();

        for (auto* filter : { &dryShelf, &wetShelf })
            filter->reset();

        drySquared.snapTo (0.0f);
        wetSquared.snapTo (0.0f);
        output.snapTo (1.0f);

        dryAccumulator = wetAccumulator = fullAccumulator = 0.0;
        accumulatedSamples = 0;
        currentGain = 1.0f;
    }

    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }

    void analyse (float dry, float wet) noexcept
    {
        auto weightedDry = dry;
        auto weightedWet = wet;

        for (size_t stage = 0; stage < dryWeighting.size(); ++stage)
        {
            weightedDry = dryWeighting[stage].process (weightedDry);
            weightedWet = wetWeighting[stage].process (weightedWet);
        }

        weightedDry = dryShelf.process (weightedDry);
        weightedWet = wetShelf.process (weightedWet);

        dryAccumulator += (double) weightedDry * (double) weightedDry;
        wetAccumulator += (double) weightedWet * (double) weightedWet;
        fullAccumulator += (double) dry * (double) dry;
        ++accumulatedSamples;
    }

    void updateBlock (int numSamples) noexcept
    {
        auto measurable = false;

        if (accumulatedSamples > 0)
        {
            const auto scale = 1.0 / (double) accumulatedSamples;
            const auto dryPower = dryAccumulator * scale;
            const auto wetPower = wetAccumulator * scale;
            const auto fullPower = fullAccumulator * scale;

            measurable = dryPower > 1.0e-9 && wetPower > 1.0e-9 && dryPower > fullPower * 0.03;

            if (measurable)
            {
                drySquared.setTarget ((float) dryPower);
                wetSquared.setTarget ((float) wetPower);
            }

            dryAccumulator = wetAccumulator = fullAccumulator = 0.0;
            accumulatedSamples = 0;
        }

        const auto dryPower = drySquared.advance (numSamples);
        const auto wetPower = wetSquared.advance (numSamples);

        if (! enabled)
            output.setTarget (1.0f);
        else if (measurable && dryPower > 1.0e-10f && wetPower > 1.0e-10f)
            output.setTarget (juce::jlimit (0.5f, 2.0f, std::sqrt (dryPower / wetPower)));

        currentGain = output.advance (numSamples);
    }

    float getGain() const noexcept { return currentGain; }

private:
    std::array<Biquad, 2> dryWeighting, wetWeighting;
    Biquad dryShelf, wetShelf;
    SmoothedScalar drySquared, wetSquared, output;

    double dryAccumulator = 0.0;
    double wetAccumulator = 0.0;
    double fullAccumulator = 0.0;
    int accumulatedSamples = 0;

    float currentGain = 1.0f;
    bool enabled = true;
};

} // namespace xyb
