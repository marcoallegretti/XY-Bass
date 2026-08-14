#include "TranslateEngine.h"

namespace xyb
{

void TranslateEngine::prepare (double newSampleRate)
{
    sampleRate = (float) newSampleRate;

    narrowExtractor.prepare (newSampleRate);
    narrowExtractor.setQ (2.4f);
    narrowExtractor.setCutoff (55.0f);

    wideExtractor.prepare (newSampleRate);
    wideExtractor.setQ (0.55f);
    wideExtractor.setCutoff (85.0f);

    highPass.prepare (newSampleRate);
    highPass.setQ (0.7071f);
    highPass.setCutoff (110.0f);

    lowPass.prepare (newSampleRate);
    lowPass.setQ (0.62f);
    lowPass.setCutoff (1200.0f);

    narrowEnvelope.prepare (newSampleRate);
    narrowEnvelope.setTimes (3.0f, 65.0f);

    wideEnvelope.prepare (newSampleRate);
    wideEnvelope.setTimes (3.0f, 65.0f);

    outputMeter.prepare (newSampleRate);
    outputMeter.setTimes (6.0f, 160.0f);

    dcBlocker.prepare (newSampleRate, 12.0f);

    amountSmoother.prepare (newSampleRate, 40.0f);
    confidenceSmoother.prepare (newSampleRate, 320.0f);
    tiltSmoother.prepare (newSampleRate, 120.0f);
    brightnessSmoother.prepare (newSampleRate, 120.0f);
    frequencySmoother.prepare (newSampleRate, 110.0f);

    reset();
}

void TranslateEngine::reset()
{
    narrowExtractor.reset();
    wideExtractor.reset();
    highPass.reset();
    lowPass.reset();
    narrowEnvelope.reset();
    wideEnvelope.reset();
    outputMeter.reset();
    dcBlocker.reset();

    amountSmoother.snapTo (0.0f);
    confidenceSmoother.snapTo (0.0f);
    tiltSmoother.snapTo (0.3f);
    brightnessSmoother.snapTo (0.3f);

    harmonicWeights = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    weightIncrement = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    frequencySmoother.snapTo (55.0f);

    trackedFrequency = 55.0f;
    levelCompensation = 1.0f;
    compensationIncrement = 0.0f;
    weightNormalisation = 1.0f;
    normalisationIncrement = 0.0f;
}

void TranslateEngine::setControls (float amount, float tilt, float brightness, float fundamentalHz,
                                   float confidence, float selectivity) noexcept
{
    amountSmoother.setTarget (juce::jlimit (0.0f, 1.0f, amount));
    tiltSmoother.setTarget (juce::jlimit (0.0f, 1.0f, tilt));
    brightnessSmoother.setTarget (juce::jlimit (0.0f, 1.0f, brightness));
    confidenceSmoother.setTarget (juce::jlimit (0.0f, 1.0f, confidence));

    frequencySmoother.setTarget (juce::jlimit (25.0f, 300.0f, fundamentalHz > 0.0f ? fundamentalHz : 55.0f));
    narrowExtractor.setQ (juce::jlimit (0.9f, 3.2f, 1.0f + selectivity * 2.2f));
}

void TranslateEngine::refreshWeights (int numSamples) noexcept
{
    const auto tilt = tiltSmoother.getCurrent();
    const auto brightness = brightnessSmoother.getCurrent();
    const auto knee = 340.0f + 420.0f * tilt + 260.0f * brightness;

    const std::array<float, 4> base { { 1.0f,
                                        0.44f + 0.30f * tilt,
                                        0.14f + 0.26f * tilt,
                                        0.07f + 0.20f * tilt } };

    const auto scale = 1.0f / (float) juce::jmax (1, numSamples);
    float energy = 0.0f;

    for (size_t index = 0; index < harmonicWeights.size(); ++index)
    {
        const auto order = (float) (index + 2);
        const auto frequency = trackedFrequency * order;
        const auto ratio = frequency / knee;
        const auto rolloff = 1.0f / (1.0f + ratio * ratio);
        const auto target = base[index] * rolloff;

        weightIncrement[index] = (target - harmonicWeights[index]) * scale;
        energy += target * target;
    }

    const auto target = 1.0f / std::sqrt (juce::jmax (energy, 0.02f));
    normalisationIncrement = (target - weightNormalisation) * scale;
}

void TranslateEngine::updateBlock (int numSamples) noexcept
{
    trackedFrequency = frequencySmoother.advance (numSamples);
    refreshWeights (numSamples);

    const auto envelope = juce::jmax (narrowEnvelope.getValue(), wideEnvelope.getValue());
    const auto target = juce::jlimit (0.5f, 2.2f, std::pow (juce::jmax (envelope, 1.0e-4f) / 0.2f, -0.15f));
    compensationIncrement = (target - levelCompensation) / (float) juce::jmax (1, numSamples);

    narrowExtractor.setCutoff (trackedFrequency);
    wideExtractor.setCutoff (juce::jlimit (45.0f, 130.0f, trackedFrequency * 1.25f));
    highPass.setCutoff (juce::jlimit (60.0f, 260.0f, trackedFrequency * 1.55f));

    const auto brightness = brightnessSmoother.getCurrent();
    lowPass.setCutoff (juce::jlimit (600.0f, 3200.0f, 780.0f + 1900.0f * brightness));
}

float TranslateEngine::extractFundamental (float monoLow) noexcept
{
    return narrowExtractor.processBandPass (monoLow);
}

float TranslateEngine::process (float fundamentalBand, float monoLow) noexcept
{
    const auto amount = amountSmoother.next();
    const auto confidence = confidenceSmoother.next();
    tiltSmoother.next();
    brightnessSmoother.next();

    for (size_t index = 0; index < harmonicWeights.size(); ++index)
        harmonicWeights[index] += weightIncrement[index];

    weightNormalisation += normalisationIncrement;
    levelCompensation += compensationIncrement;

    if (amount <= 1.0e-5f)
    {
        narrowEnvelope.process (fundamentalBand);
        wideEnvelope.process (wideExtractor.processBandPass (monoLow));
        highPass.processHighPass (0.0f);
        lowPass.processLowPass (0.0f);
        dcBlocker.process (0.0f);
        outputMeter.process (0.0f);
        return 0.0f;
    }

    const auto narrowLevel = narrowEnvelope.process (fundamentalBand);
    const auto wideBand = wideExtractor.processBandPass (monoLow);
    const auto wideLevel = wideEnvelope.process (wideBand);

    const auto normalisedNarrow = juce::jlimit (-1.0f, 1.0f, fundamentalBand / juce::jmax (narrowLevel, 1.0e-5f));
    const auto normalisedWide = juce::jlimit (-1.0f, 1.0f, wideBand / juce::jmax (wideLevel, 1.0e-5f));

    const auto squared = normalisedNarrow * normalisedNarrow;
    const auto cubed = squared * normalisedNarrow;
    const auto fourth = squared * squared;
    const auto fifth = fourth * normalisedNarrow;

    const auto second = 2.0f * squared - 1.0f;
    const auto third = 4.0f * cubed - 3.0f * normalisedNarrow;
    const auto fourthOrder = 8.0f * fourth - 8.0f * squared + 1.0f;
    const auto fifthOrder = 16.0f * fifth - 20.0f * cubed + 5.0f * normalisedNarrow;

    const auto tracked = (harmonicWeights[0] * second + harmonicWeights[1] * third
                          + harmonicWeights[2] * fourthOrder + harmonicWeights[3] * fifthOrder)
                         * weightNormalisation;

    const auto driven = std::tanh (2.6f * normalisedWide);
    const auto broadband = (driven - normalisedWide * 0.86f) * 1.9f
                           + (normalisedWide * normalisedWide - 0.5f) * 0.55f;

    const auto blend = juce::jlimit (0.0f, 1.0f, confidence * 1.35f);
    const auto envelope = blend * narrowLevel + (1.0f - blend) * wideLevel;
    const auto generated = (blend * tracked + (1.0f - blend) * broadband) * envelope * levelCompensation * amount;

    auto shaped = highPass.processHighPass (dcBlocker.process (generated));
    shaped = lowPass.processLowPass (shaped);

    outputMeter.process (shaped);
    return shaped * 1.05f;
}

} // namespace xyb
