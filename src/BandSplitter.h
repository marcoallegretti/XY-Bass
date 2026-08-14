#pragma once

#include "DspUtility.h"

namespace xyb
{

struct Bands
{
    float low = 0.0f;
    float mid = 0.0f;
    float character = 0.0f;
};

class BandSplitter
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        for (auto* filter : { &lowSplit, &characterSplit })
        {
            filter->prepare (spec);
            filter->setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        }

        lowAllpass.prepare (spec);
        lowAllpass.setType (juce::dsp::LinkwitzRileyFilterType::allpass);

        setCrossovers (150.0f, 500.0f);
        reset();
    }

    void reset()
    {
        lowSplit.reset();
        characterSplit.reset();
        lowAllpass.reset();
    }

    void setCrossovers (float lowHz, float characterHz)
    {
        lowSplit.setCutoffFrequency (lowHz);
        characterSplit.setCutoffFrequency (characterHz);
        lowAllpass.setCutoffFrequency (characterHz);
    }

    Bands process (int channel, float input) noexcept
    {
        float low = 0.0f, high = 0.0f;
        Bands bands;

        lowSplit.processSample (channel, input, low, high);
        bands.low = lowAllpass.processSample (channel, low);

        characterSplit.processSample (channel, high, low, high);
        bands.mid = low;
        bands.character = high;

        return bands;
    }

private:
    juce::dsp::LinkwitzRileyFilter<float> lowSplit, characterSplit, lowAllpass;
};

} // namespace xyb
