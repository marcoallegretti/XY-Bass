#include "SpectralBalance.h"

namespace xyb
{

void SpectralBalance::prepare (double newSampleRate, int numChannels)
{
    sampleRate = (float) newSampleRate;

    dryProbe.prepare (newSampleRate);
    wetProbe.prepare (newSampleRate);

    mudFilters.resize ((size_t) juce::jmax (1, numChannels));
    fizzFilters.resize ((size_t) juce::jmax (1, numChannels));

    for (auto& filter : mudFilters)
        filter.prepare (newSampleRate);

    for (auto& filter : fizzFilters)
        filter.prepare (newSampleRate);

    mudControl.prepare (newSampleRate, 120.0f);
    fizzControl.prepare (newSampleRate, 120.0f);
    mudGain.prepare (newSampleRate, 260.0f);
    fizzGain.prepare (newSampleRate, 260.0f);

    reset();
}

void SpectralBalance::reset()
{
    dryProbe.reset();
    wetProbe.reset();

    for (auto& filter : mudFilters)
    {
        filter.reset();
        filter.setBypass();
    }

    for (auto& filter : fizzFilters)
    {
        filter.reset();
        filter.setBypass();
    }

    mudControl.snapTo (0.0f);
    fizzControl.snapTo (0.0f);
    mudGain.snapTo (0.0f);
    fizzGain.snapTo (0.0f);

    mudGainDb = 0.0f;
    fizzGainDb = 0.0f;
}

void SpectralBalance::setControls (float mudAmount, float fizzAmount) noexcept
{
    mudControl.setTarget (juce::jlimit (0.0f, 1.0f, mudAmount));
    fizzControl.setTarget (juce::jlimit (0.0f, 1.0f, fizzAmount));
}

void SpectralBalance::analyseDry (float mono) noexcept
{
    dryProbe.push (mono);
}

void SpectralBalance::analyseWet (float mono) noexcept
{
    wetProbe.push (mono);
}

void SpectralBalance::updateBlock (int numSamples) noexcept
{
    const auto mudAmount = mudControl.advance (numSamples);
    const auto fizzAmount = fizzControl.advance (numSamples);

    const auto dryAnchor = juce::jmax (dryProbe.anchorLevel.getValue(), 1.0e-6f);
    const auto wetAnchor = juce::jmax (wetProbe.anchorLevel.getValue(), 1.0e-6f);

    const auto dryMudRatio = dryProbe.mudLevel.getValue() / dryAnchor;
    const auto wetMudRatio = wetProbe.mudLevel.getValue() / wetAnchor;
    const auto dryFizzRatio = dryProbe.fizzLevel.getValue() / dryAnchor;
    const auto wetFizzRatio = wetProbe.fizzLevel.getValue() / wetAnchor;

    const auto mudExcess = juce::Decibels::gainToDecibels (juce::jmax (wetMudRatio, 1.0e-6f))
                           - juce::Decibels::gainToDecibels (juce::jmax (dryMudRatio, 1.0e-6f));
    const auto fizzExcess = juce::Decibels::gainToDecibels (juce::jmax (wetFizzRatio, 1.0e-6f))
                            - juce::Decibels::gainToDecibels (juce::jmax (dryFizzRatio, 1.0e-6f));

    const auto activity = juce::jlimit (0.0f, 1.0f, (wetAnchor + wetProbe.mudLevel.getValue()) * 120.0f);

    mudGain.setTarget (juce::jlimit (-4.0f, 0.0f, -juce::jmax (0.0f, mudExcess - 0.8f) * 0.75f * mudAmount * activity));
    fizzGain.setTarget (juce::jlimit (-4.5f, 0.0f, -juce::jmax (0.0f, fizzExcess - 1.0f) * 0.8f * fizzAmount * activity));

    mudGainDb = mudGain.advance (numSamples);
    fizzGainDb = fizzGain.advance (numSamples);

    for (auto& filter : mudFilters)
    {
        if (mudGainDb < -0.02f)
            filter.setPeaking (190.0f, 1.05f, mudGainDb);
        else
            filter.setBypass();
    }

    for (auto& filter : fizzFilters)
    {
        if (fizzGainDb < -0.02f)
            filter.setPeaking (520.0f, 0.95f, fizzGainDb);
        else
            filter.setBypass();
    }
}

float SpectralBalance::process (int channel, float input) noexcept
{
    const auto index = (size_t) juce::jlimit (0, (int) mudFilters.size() - 1, channel);
    return fizzFilters[index].process (mudFilters[index].process (input));
}

} // namespace xyb
