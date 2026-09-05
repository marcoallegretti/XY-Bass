#include "SubEngine.h"

namespace xyb
{

namespace
{
constexpr float kLockGain = 0.19f;
}

void SubEngine::prepare (double newSampleRate)
{
    sampleRate = (float) newSampleRate;

    reinforcementBand.prepare (newSampleRate);
    reinforcementBand.setQ (0.9f);
    reinforcementBand.setCutoff (55.0f);

    outputLimitBand.prepare (newSampleRate);
    outputLimitBand.setQ (0.7071f);
    outputLimitBand.setCutoff (130.0f);

    lowEnvelope.prepare (newSampleRate);
    lowEnvelope.setTimes (8.0f, 140.0f);

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

    reconstructionOscillator.prepare (newSampleRate);
    subharmonicOscillator.prepare (newSampleRate);

    reset();
}

void SubEngine::reset()
{
    reinforcementBand.reset();
    outputLimitBand.reset();
    lowEnvelope.reset();
    reinforcementMeter.reset();
    synthesisMeter.reset();
    dcBlocker.reset();

    reinforcement.snapTo (0.0f);
    reconstruction.snapTo (0.0f);
    subharmonic.snapTo (0.0f);
    centre.snapTo (55.0f);
    oscillatorFrequency.snapTo (55.0f);

    reconstructionOscillator.reset();
    reconstructionOscillator.setFrequency (55.0f);
    subharmonicOscillator.reset();
    subharmonicOscillator.setFrequency (27.5f);
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

    reinforcementBand.setCutoff (centre.advance (numSamples));

    const auto frequency = oscillatorFrequency.advance (numSamples);
    reconstructionOscillator.setFrequency (frequency);
    subharmonicOscillator.setFrequency (frequency * 0.5f);
}

float SubEngine::process (float monoLow, float fundamentalBand, float fundamentalMagnitude,
                          float fundamentalQuadrature) noexcept
{
    const auto reinforcementAmount = reinforcement.next();
    const auto reconstructionAmount = reconstruction.next();
    const auto subharmonicAmount = subharmonic.next();
    compressiveGain += compressiveIncrement;

    const auto lowLevel = lowEnvelope.process (monoLow);
    const auto fundamentalLevel = fundamentalMagnitude;

    const auto reinforced = reinforcementBand.processBandPass (monoLow)
                            * reinforcementAmount * 1.35f * compressiveGain;

    float synthesised = 0.0f;

    if (reconstructionAmount > 1.0e-4f)
    {
        reconstructionOscillator.advance();

        const auto oscillator = reconstructionOscillator.sine();
        const auto inverse = 1.0f / juce::jmax (fundamentalMagnitude, 1.0e-6f);
        const auto lock = juce::jlimit (0.0f, 1.0f, fundamentalMagnitude * 260.0f);
        const auto error = fundamentalBand * inverse * reconstructionOscillator.cosine()
                           - fundamentalQuadrature * inverse * oscillator;

        reconstructionOscillator.nudge (kLockGain * lock * error);

        synthesised += oscillator * lowLevel * reconstructionAmount * 1.5f * compressiveGain;
    }
    else
    {
        reconstructionOscillator.reset();
    }

    if (subharmonicAmount > 1.0e-4f)
    {
        subharmonicOscillator.advance();
        synthesised += subharmonicOscillator.sine() * fundamentalLevel * subharmonicAmount * 0.9f;
    }
    else
    {
        subharmonicOscillator.reset();
    }

    const auto generated = dcBlocker.process (reinforced + synthesised);
    const auto bounded = outputLimitBand.processLowPass (generated);

    reinforcementMeter.process (reinforced);
    synthesisMeter.process (synthesised);

    return bounded;
}

} // namespace xyb
