#include "SubEngine.h"

namespace xyb
{

void SubEngine::prepare (double newSampleRate)
{
    sampleRate = (float) newSampleRate;

    reinforcementBand.prepare (newSampleRate);
    reinforcementBand.setQ (0.9f);
    reinforcementBand.setCutoff (55.0f);

    subharmonicShaper.prepare (newSampleRate);
    subharmonicShaper.setQ (0.68f);
    subharmonicShaper.setCutoff (48.0f);

    outputLimitBand.prepare (newSampleRate);
    outputLimitBand.setQ (0.7071f);
    outputLimitBand.setCutoff (130.0f);

    lowEnvelope.prepare (newSampleRate);
    lowEnvelope.setTimes (8.0f, 140.0f);

    fundamentalEnvelope.prepare (newSampleRate);
    fundamentalEnvelope.setTimes (4.0f, 90.0f);

    for (auto* meter : { &reinforcementMeter, &synthesisMeter })
    {
        meter->prepare (newSampleRate);
        meter->setTimes (6.0f, 160.0f);
    }

    dcBlocker.prepare (newSampleRate, 8.0f);

    reinforcement.prepare (newSampleRate, 45.0f);
    reconstruction.prepare (newSampleRate, 260.0f);
    subharmonic.prepare (newSampleRate, 420.0f);
    centre.prepare (newSampleRate, 160.0f);
    oscillatorFrequency.prepare (newSampleRate, 90.0f);

    reset();
}

void SubEngine::reset()
{
    reinforcementBand.reset();
    subharmonicShaper.reset();
    outputLimitBand.reset();
    lowEnvelope.reset();
    fundamentalEnvelope.reset();
    reinforcementMeter.reset();
    synthesisMeter.reset();
    dcBlocker.reset();

    reinforcement.snapTo (0.0f);
    reconstruction.snapTo (0.0f);
    subharmonic.snapTo (0.0f);
    centre.snapTo (55.0f);
    oscillatorFrequency.snapTo (55.0f);

    phase = 0.0;
    dividerState = 1.0f;
    previousFundamental = 0.0f;
    dividerHold = 0;
    compressiveGain = 1.0f;
    compressiveIncrement = 0.0f;
    trackedFrequency = 55.0f;
}

void SubEngine::setControls (float reinforcementAmount, float centreHz, float reconstructionAmount,
                             float subharmonicAmount, float fundamentalHz, float selectivity) noexcept
{
    reinforcement.setTarget (juce::jlimit (0.0f, 1.0f, reinforcementAmount));
    reconstruction.setTarget (juce::jlimit (0.0f, 1.0f, reconstructionAmount));
    subharmonic.setTarget (juce::jlimit (0.0f, 1.0f, subharmonicAmount));
    centre.setTarget (juce::jlimit (28.0f, 110.0f, centreHz));

    trackedFrequency = juce::jlimit (25.0f, 300.0f, fundamentalHz > 0.0f ? fundamentalHz : 55.0f);

    oscillatorFrequency.setTarget (trackedFrequency);
    reinforcementBand.setQ (juce::jlimit (0.6f, 2.0f, 0.7f + selectivity * 0.9f));
}

void SubEngine::updateBlock (int numSamples) noexcept
{
    const auto envelope = juce::jmax (lowEnvelope.getValue(), 1.0e-5f);
    const auto target = juce::jlimit (0.35f, 1.6f, std::pow (0.16f / envelope, 0.35f));
    compressiveIncrement = (target - compressiveGain) / (float) juce::jmax (1, numSamples);
}

float SubEngine::process (float monoLow, float fundamentalBand) noexcept
{
    const auto reinforcementAmount = reinforcement.next();
    const auto reconstructionAmount = reconstruction.next();
    const auto subharmonicAmount = subharmonic.next();
    const auto centreHz = centre.next();

    compressiveGain += compressiveIncrement;
    reinforcementBand.setCutoff (centreHz);

    const auto lowLevel = lowEnvelope.process (monoLow);
    const auto fundamentalLevel = fundamentalEnvelope.process (fundamentalBand);

    const auto reinforced = reinforcementBand.processBandPass (monoLow)
                            * reinforcementAmount * 1.35f * compressiveGain;

    float synthesised = 0.0f;

    if (reconstructionAmount > 1.0e-4f)
    {
        const auto frequency = oscillatorFrequency.next();
        phase += (double) frequency / (double) sampleRate;

        if (phase >= 1.0)
            phase -= 1.0;

        const auto oscillator = std::sin ((float) (phase * juce::MathConstants<double>::twoPi));
        synthesised += oscillator * lowLevel * reconstructionAmount * 1.5f * compressiveGain;
    }
    else
    {
        oscillatorFrequency.next();
        phase = 0.0;
    }

    if (subharmonicAmount > 1.0e-4f)
    {
        const auto threshold = juce::jmax (fundamentalLevel * 0.18f, 1.0e-5f);

        if (dividerHold > 0)
            --dividerHold;

        if (dividerHold == 0 && previousFundamental <= threshold && fundamentalBand > threshold)
        {
            dividerState = -dividerState;
            dividerHold = (int) (sampleRate * 0.45f / trackedFrequency);
        }

        previousFundamental = fundamentalBand;

        subharmonicShaper.setCutoff (juce::jlimit (20.0f, 110.0f, trackedFrequency * 0.62f));
        const auto smoothed = subharmonicShaper.processLowPass (dividerState);
        synthesised += smoothed * fundamentalLevel * subharmonicAmount * 1.1f;
    }
    else
    {
        previousFundamental = fundamentalBand;
    }

    const auto generated = dcBlocker.process (reinforced + synthesised);
    const auto bounded = outputLimitBand.processLowPass (generated);

    reinforcementMeter.process (reinforced);
    synthesisMeter.process (synthesised);

    return bounded;
}

} // namespace xyb
