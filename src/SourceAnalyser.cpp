#include "SourceAnalyser.h"

namespace xyb
{

void SourceAnalyser::prepare (double sampleRate)
{
    subProbe.prepare (sampleRate);
    subProbe.setQ (0.7071f);
    subProbe.setCutoff (60.0f);

    bassProbe.prepare (sampleRate);
    bassProbe.setQ (0.7071f);
    bassProbe.setCutoff (150.0f);

    characterProbe.prepare (sampleRate);
    characterProbe.setQ (0.7071f);
    characterProbe.setCutoff (500.0f);

    for (auto* envelope : { &subEnvelope, &bassEnvelope, &translateEnvelope, &characterEnvelope })
    {
        envelope->prepare (sampleRate);
        envelope->setTimes (12.0f, 180.0f);
    }

    fastEnvelope.prepare (sampleRate);
    fastEnvelope.setTimes (0.6f, 14.0f);

    slowEnvelope.prepare (sampleRate);
    slowEnvelope.setTimes (45.0f, 240.0f);

    peakEnvelope.prepare (sampleRate);
    peakEnvelope.setTimes (0.1f, 400.0f);

    rmsSmoother.prepare (sampleRate, 120.0f);
    crestSmoother.prepare (sampleRate, 250.0f);
    transientSmoother.prepare (sampleRate, 35.0f);
    correlationSmoother.prepare (sampleRate, 300.0f);
    mixSmoother.prepare (sampleRate, 450.0f);
    percussiveSmoother.prepare (sampleRate, 220.0f);
    percussiveRelease = timeToCoefficient (900.0f, (float) sampleRate);

    reset();
}

void SourceAnalyser::reset()
{
    subProbe.reset();
    bassProbe.reset();
    characterProbe.reset();

    for (auto* envelope : { &subEnvelope, &bassEnvelope, &translateEnvelope, &characterEnvelope,
                            &fastEnvelope, &slowEnvelope, &peakEnvelope })
        envelope->reset();

    rmsSmoother.snapTo (0.0f);
    crestSmoother.snapTo (1.0f);
    transientSmoother.snapTo (0.0f);
    correlationSmoother.snapTo (1.0f);
    mixSmoother.snapTo (0.0f);
    percussiveSmoother.snapTo (0.0f);

    correlationProduct = correlationLeft = correlationRight = 0.0;
    squaredSum = 0.0;
    stereoCount = 0;
    percussivePeak = 0.0f;

    features = {};
}

void SourceAnalyser::pushMono (float mono) noexcept
{
    const auto belowSub = subProbe.processLowPass (mono);
    const auto belowBass = bassProbe.processLowPass (mono);
    const auto belowCharacter = characterProbe.processLowPass (mono);

    const auto sub = belowSub;
    const auto bass = belowBass - belowSub;
    const auto translate = belowCharacter - belowBass;
    const auto character = mono - belowCharacter;

    subEnvelope.process (sub);
    bassEnvelope.process (bass);
    translateEnvelope.process (translate);
    characterEnvelope.process (character);

    const auto low = sub + bass;
    fastEnvelope.process (low);
    slowEnvelope.process (low);
    peakEnvelope.process (low);

    squaredSum += (double) low * (double) low;
}

void SourceAnalyser::pushStereo (float lowLeft, float lowRight) noexcept
{
    correlationProduct += (double) lowLeft * (double) lowRight;
    correlationLeft += (double) lowLeft * (double) lowLeft;
    correlationRight += (double) lowRight * (double) lowRight;
    ++stereoCount;
}

void SourceAnalyser::setPitch (float frequency, float confidence, float stability) noexcept
{
    features.fundamental = frequency;
    features.pitchConfidence = confidence;
    features.pitchStability = stability;
}

void SourceAnalyser::finishBlock (int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const auto rms = std::sqrt ((float) (squaredSum / (double) numSamples));
    squaredSum = 0.0;

    const auto sub = subEnvelope.getValue();
    const auto bass = bassEnvelope.getValue();
    const auto translate = translateEnvelope.getValue();
    const auto character = characterEnvelope.getValue();
    const auto total = sub + bass + translate + character + kTiny;

    features.subLevel = sub;
    features.bassLevel = bass;
    features.translateLevel = translate;
    features.characterLevel = character;
    features.fundamentalStrength = juce::jlimit (0.0f, 1.0f, (sub + bass) / total);

    rmsSmoother.setTarget (rms);
    features.level = rmsSmoother.advance (numSamples);

    const auto crest = peakEnvelope.getValue() / juce::jmax (features.level, 1.0e-5f);
    crestSmoother.setTarget (juce::jlimit (1.0f, 24.0f, crest));
    features.crest = crestSmoother.advance (numSamples);

    const auto attackRatio = fastEnvelope.getValue() / juce::jmax (slowEnvelope.getValue(), 1.0e-5f);
    const auto instantaneous = juce::jlimit (0.0f, 1.0f, (attackRatio - 1.05f) * 0.9f);
    transientSmoother.setTarget (instantaneous);
    features.transient = transientSmoother.advance (numSamples);

    const auto release = std::pow (percussiveRelease, (float) numSamples);
    percussivePeak = instantaneous > percussivePeak ? instantaneous
                                                   : instantaneous + release * (percussivePeak - instantaneous);

    if (stereoCount > 0)
    {
        const auto denominator = std::sqrt (correlationLeft * correlationRight) + 1.0e-12;
        const auto correlation = (float) juce::jlimit (-1.0, 1.0, correlationProduct / denominator);
        correlationSmoother.setTarget (correlation);
        correlationProduct = correlationLeft = correlationRight = 0.0;
        stereoCount = 0;
    }

    features.correlation = correlationSmoother.advance (numSamples);

    const auto spread = juce::jlimit (0.0f, 1.0f, (character / total - 0.10f) * 4.0f);
    const auto vagueness = 1.0f - features.pitchConfidence * features.pitchStability;
    mixSmoother.setTarget (juce::jlimit (0.0f, 1.0f, spread * (0.45f + 0.55f * vagueness)));
    features.mixLikeness = mixSmoother.advance (numSamples);

    const auto crestTerm = juce::jlimit (0.0f, 1.0f, (features.crest - 3.5f) * 0.14f);
    percussiveSmoother.setTarget (juce::jlimit (0.0f, 1.0f, percussivePeak * 1.1f + crestTerm * 0.45f));
    features.percussive = percussiveSmoother.advance (numSamples);

    features.tonal = juce::jlimit (0.0f, 1.0f, features.pitchConfidence * features.pitchStability
                                                   * (1.0f - 0.45f * features.percussive));

    const auto harmonicRatio = translate / juce::jmax (sub + bass, 1.0e-5f);
    features.existingSaturation = juce::jlimit (0.0f, 1.0f, (harmonicRatio - 0.25f) * 1.1f
                                                                * (1.0f - juce::jlimit (0.0f, 1.0f, (features.crest - 3.0f) * 0.2f)));
}

} // namespace xyb
