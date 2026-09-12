#include "TestSignals.h"

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

struct TrackerResult
{
    float frequency = 0.0f;
    float confidence = 0.0f;
    float stability = 0.0f;
};

TrackerResult trackBuffer (const Buffer& buffer, double sampleRate, int blockSize = 256)
{
    xyb::PitchTracker tracker;
    tracker.prepare (sampleRate);

    for (int start = 0; start + blockSize <= buffer.getNumSamples(); start += blockSize)
        tracker.process (buffer.getReadPointer (0) + start, blockSize);

    return { tracker.getFrequency(), tracker.getConfidence(), tracker.getStability() };
}

double centsBetween (double measured, double expected)
{
    return 1200.0 * std::log2 (juce::jmax (measured, 1.0e-6) / expected);
}

void testPitchAccuracy()
{
    section ("pitch accuracy");

    const auto sampleRate = 48000.0;
    const std::array<double, 8> frequencies { { 27.5, 32.7, 41.2, 55.0, 82.4, 110.0, 164.8, 246.9 } };

    double worstSine = 0.0;
    double worstSaw = 0.0;
    double lowestConfidence = 1.0;

    for (auto frequency : frequencies)
    {
        auto sine = makeBuffer (1, (int) (sampleRate * 1.6));
        fillSine (sine, frequency, sampleRate, 0.3f);
        const auto sineResult = trackBuffer (sine, sampleRate);

        auto saw = makeBuffer (1, (int) (sampleRate * 1.6));
        fillSaw (saw, frequency, sampleRate, 0.22f);
        const auto sawResult = trackBuffer (saw, sampleRate);

        const auto sineError = std::abs (centsBetween (sineResult.frequency, frequency));
        const auto sawError = std::abs (centsBetween (sawResult.frequency, frequency));

        worstSine = juce::jmax (worstSine, sineError);
        worstSaw = juce::jmax (worstSaw, sawError);
        lowestConfidence = juce::jmin (lowestConfidence, (double) juce::jmin (sineResult.confidence,
                                                                             sawResult.confidence));

        report (juce::String (frequency, 1) + " Hz sine error (cents)", sineError);
        report (juce::String (frequency, 1) + " Hz saw error (cents)", sawError);
    }

    report ("lowest confidence on tonal material", lowestConfidence);

    check (worstSine < 12.0, "sine pitch is tracked within a fifth of a semitone");
    check (worstSaw < 12.0, "harmonic-rich pitch is tracked within a fifth of a semitone");
    check (lowestConfidence > 0.6, "tonal material reports high confidence");
}

void testMissingFundamental()
{
    section ("missing fundamental tracking");

    const auto sampleRate = 48000.0;
    auto buffer = makeBuffer (1, (int) (sampleRate * 1.6));
    fillMissingFundamental (buffer, 45.0, sampleRate, 0.35f);

    const auto result = trackBuffer (buffer, sampleRate);
    const auto error = std::abs (centsBetween (result.frequency, 45.0));

    report ("tracked frequency", result.frequency);
    report ("error (cents)", error);
    report ("confidence", result.confidence);

    check (error < 30.0, "a suppressed fundamental is still inferred from its harmonics");
    check (result.confidence > 0.5, "harmonic evidence produces usable confidence");
}

void testUnpitchedMaterial()
{
    section ("unpitched and ambiguous material");

    const auto sampleRate = 48000.0;

    auto noise = makeBuffer (1, (int) (sampleRate * 1.6));
    fillNoise (noise, 0.3f, 12);
    const auto noiseResult = trackBuffer (noise, sampleRate);

    auto silence = makeBuffer (1, (int) (sampleRate * 1.0));
    const auto silenceResult = trackBuffer (silence, sampleRate);

    auto chord = makeBuffer (1, (int) (sampleRate * 1.6));
    fillSine (chord, 55.0, sampleRate, 0.25f);
    addSine (chord, 82.4, sampleRate, 0.25f);
    addSine (chord, 98.0, sampleRate, 0.22f);
    const auto chordResult = trackBuffer (chord, sampleRate);

    auto mix = makeBuffer (1, (int) (sampleRate * 2.5));
    fillFullMix (mix, sampleRate, 0.45f);
    const auto mixResult = trackBuffer (mix, sampleRate);

    report ("noise confidence", noiseResult.confidence);
    report ("silence confidence", silenceResult.confidence);
    report ("chord confidence", chordResult.confidence);
    report ("full mix confidence", mixResult.confidence);

    check (noiseResult.confidence < 0.35, "noise is not mistaken for a pitch");
    check (silenceResult.confidence < 0.05, "silence reports no pitch");
    check (chordResult.confidence < 0.8, "stacked notes reduce confidence");
    check (mixResult.confidence < 0.85, "a dense mix reduces confidence");
}

void testNoteTransitions()
{
    section ("note transitions");

    const auto sampleRate = 48000.0;
    const auto blockSize = 256;
    const auto noteSamples = (int) (sampleRate * 1.2);

    auto buffer = makeBuffer (1, noteSamples * 2);

    for (int i = 0; i < noteSamples; ++i)
        buffer.setSample (0, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 55.0 * i / sampleRate));

    for (int i = 0; i < noteSamples; ++i)
        buffer.setSample (0, noteSamples + i,
                          0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 82.41 * i / sampleRate));

    xyb::PitchTracker tracker;
    tracker.prepare (sampleRate);

    int settleSample = -1;

    for (int start = 0; start + blockSize <= buffer.getNumSamples(); start += blockSize)
    {
        tracker.process (buffer.getReadPointer (0) + start, blockSize);

        if (start >= noteSamples && settleSample < 0
            && std::abs (centsBetween (tracker.getFrequency(), 82.41)) < 25.0)
            settleSample = start - noteSamples;
    }

    const auto settleMs = settleSample < 0 ? 1.0e9 : 1000.0 * settleSample / sampleRate;

    report ("time to lock the new note (ms)", settleMs);
    report ("final frequency", tracker.getFrequency());

    check (settleSample >= 0, "the tracker follows a note change");
    check (settleMs < 220.0, "the tracker locks a new note quickly enough for musical use");
}

void testSourceCharacterisation()
{
    section ("source characterisation");

    const auto sampleRate = 48000.0;

    auto measure = [&] (const Buffer& source)
    {
        Buffer copy (source);
        xyb::BassEngine engine;
        engine.prepare (sampleRate, 256, copy.getNumChannels());
        engine.setParameters (position (0.5f, 0.5f));
        render (engine, copy, 256);
        return engine.getFeatures();
    };

    auto kick = makeBuffer (2, (int) (sampleRate * 3.0));
    fillKick (kick, sampleRate, 0.45f);

    auto bass = makeBuffer (2, (int) (sampleRate * 3.0));
    fillSaw (bass, 55.0, sampleRate, 0.2f);

    auto mix = makeBuffer (2, (int) (sampleRate * 3.0));
    fillFullMix (mix, sampleRate, 0.4f);

    auto clean = makeBuffer (2, (int) (sampleRate * 3.0));
    fillSaturatedSine (clean, 55.0, sampleRate, 0.25f, 0.05f);

    auto distorted = makeBuffer (2, (int) (sampleRate * 3.0));
    fillSaturatedSine (distorted, 55.0, sampleRate, 0.25f, 6.0f);

    const auto kickFeatures = measure (kick);
    const auto bassFeatures = measure (bass);
    const auto mixFeatures = measure (mix);
    const auto cleanFeatures = measure (clean);
    const auto distortedFeatures = measure (distorted);

    report ("kick percussive", kickFeatures.percussive);
    report ("bass percussive", bassFeatures.percussive);
    report ("kick tonal", kickFeatures.tonal);
    report ("bass tonal", bassFeatures.tonal);
    report ("bass mix likeness", bassFeatures.mixLikeness);
    report ("full mix likeness", mixFeatures.mixLikeness);
    report ("clean tone existing saturation", cleanFeatures.existingSaturation);
    report ("driven tone existing saturation", distortedFeatures.existingSaturation);

    check (kickFeatures.percussive > 0.6, "a kick reads as percussive");
    check (bassFeatures.percussive < 0.3, "a sustained bass does not read as percussive");
    check (bassFeatures.tonal > 0.6, "a sustained bass reads as tonal");
    check (mixFeatures.mixLikeness > 0.4, "a full mix is recognised as dense material");
    check (bassFeatures.mixLikeness < 0.2, "an isolated bass is not treated as a mix");
    check (distortedFeatures.existingSaturation > cleanFeatures.existingSaturation + 0.15,
           "already saturated material is recognised");
}

void testAdaptiveRestraint()
{
    section ("adaptive restraint");

    const auto sampleRate = 48000.0;

    auto measure = [&] (const Buffer& source, float x, float y)
    {
        Buffer copy (source);
        xyb::BassEngine engine;
        engine.prepare (sampleRate, 256, copy.getNumChannels());
        engine.setParameters (position (x, y));
        render (engine, copy, 256);
        return engine.getTargets();
    };

    auto bass = makeBuffer (2, (int) (sampleRate * 3.0));
    fillSaw (bass, 55.0, sampleRate, 0.2f);

    auto mix = makeBuffer (2, (int) (sampleRate * 3.0));
    fillFullMix (mix, sampleRate, 0.4f);

    const auto bassTargets = measure (bass, 0.85f, 0.85f);
    const auto mixTargets = measure (mix, 0.85f, 0.85f);

    report ("harmonic amount on isolated bass", bassTargets.harmonicAmount);
    report ("harmonic amount on full mix", mixTargets.harmonicAmount);
    report ("drive on isolated bass", bassTargets.drive);
    report ("drive on full mix", mixTargets.drive);
    report ("reconstruction on full mix", mixTargets.subReconstruction);
    report ("subharmonic on full mix", mixTargets.subharmonic);

    check (mixTargets.harmonicAmount < bassTargets.harmonicAmount * 0.8,
           "harmonic synthesis is held back on dense material");
    check (mixTargets.drive < bassTargets.drive * 0.85, "drive is held back on dense material");
    check (mixTargets.subReconstruction < 0.05, "a dense mix never triggers fundamental reconstruction");
    check (mixTargets.subharmonic < 0.01, "a dense mix never triggers subharmonic generation");
}

void testReconstructionEngagement()
{
    section ("fundamental reconstruction engagement");

    const auto sampleRate = 48000.0;

    auto measure = [&] (const Buffer& source)
    {
        Buffer copy (source);
        xyb::BassEngine engine;
        engine.prepare (sampleRate, 256, copy.getNumChannels());
        engine.setParameters (position (0.0f, 0.2f));
        render (engine, copy, 256);
        return engine.getTargets();
    };

    auto thin = makeBuffer (2, (int) (sampleRate * 3.0));
    fillMissingFundamental (thin, 55.0, sampleRate, 0.3f);

    auto solid = makeBuffer (2, (int) (sampleRate * 3.0));
    fillSine (solid, 55.0, sampleRate, 0.3f);

    const auto thinTargets = measure (thin);
    const auto solidTargets = measure (solid);

    report ("reconstruction on a thin source", thinTargets.subReconstruction);
    report ("reconstruction on a solid source", solidTargets.subReconstruction);

    check (thinTargets.subReconstruction > 0.15, "a weak fundamental invites reconstruction");
    check (solidTargets.subReconstruction < 0.05, "a strong fundamental is reinforced, not replaced");

    Buffer processed (thin);
    xyb::BassEngine engine;
    engine.prepare (sampleRate, 256, 2);
    engine.setParameters (position (0.0f, 0.2f));
    render (engine, processed, 256);

    const auto start = (int) (sampleRate * 2.0);
    const auto span = (int) sampleRate;
    const auto before = magnitudeAt (thin.getReadPointer (0) + start, span, 55.0, sampleRate);
    const auto after = magnitudeAt (processed.getReadPointer (0) + start, span, 55.0, sampleRate);

    report ("reconstructed fundamental gain", relativeDb (after, juce::jmax (before, 1.0e-6)));
    check (after > before * 4.0, "reconstruction actually restores energy at the missing fundamental");
}

void testSubharmonicGating()
{
    section ("subharmonic gating");

    const auto sampleRate = 48000.0;

    auto measure = [&] (const Buffer& source, float x)
    {
        Buffer copy (source);
        xyb::BassEngine engine;
        engine.prepare (sampleRate, 256, copy.getNumChannels());
        engine.setParameters (position (x, 0.2f));
        render (engine, copy, 256);
        return engine.getTargets().subharmonic;
    };

    auto steady = makeBuffer (2, (int) (sampleRate * 4.0));
    fillSaw (steady, 98.0, sampleRate, 0.2f);

    auto percussive = makeBuffer (2, (int) (sampleRate * 4.0));
    fillKick (percussive, sampleRate, 0.45f);

    auto tooLow = makeBuffer (2, (int) (sampleRate * 4.0));
    fillSaw (tooLow, 41.0, sampleRate, 0.2f);

    const auto steadyAmount = measure (steady, 0.0f);
    const auto translateAmount = measure (steady, 1.0f);
    const auto percussiveAmount = measure (percussive, 0.0f);
    const auto lowAmount = measure (tooLow, 0.0f);

    report ("steady note at sub extreme", steadyAmount);
    report ("steady note at translate extreme", translateAmount);
    report ("kick at sub extreme", percussiveAmount);
    report ("low note at sub extreme", lowAmount);

    check (steadyAmount > 0.05, "a stable mid-bass note can reach the subharmonic stage");
    check (translateAmount < 0.01, "the subharmonic stage stays out of the translate side");
    check (percussiveAmount < 0.02, "percussive material never triggers subharmonics");
    check (lowAmount < 0.01, "notes that would fall below the useful range are excluded");
}

} // namespace

void testStabilityReportsTracking()
{
    section ("stability reflects tracking, not stillness");

    const auto sampleRate = 48000.0;
    constexpr int blockSize = 256;

    xyb::PitchTracker tracker;
    tracker.prepare (sampleRate);

    auto tone = makeBuffer (1, (int) (sampleRate * 1.5));
    fillSine (tone, 82.4, sampleRate, 0.5f);

    for (int start = 0; start + blockSize <= tone.getNumSamples(); start += blockSize)
        tracker.process (tone.getReadPointer (0) + start, blockSize);

    const auto lockedStability = tracker.getStability();
    const auto lockedFrequency = tracker.getFrequency();

    auto noise = makeBuffer (1, (int) (sampleRate * 1.5));
    fillNoise (noise, 0.5f, 9182);

    for (int start = 0; start + blockSize <= noise.getNumSamples(); start += blockSize)
        tracker.process (noise.getReadPointer (0) + start, blockSize);

    const auto stuckStability = tracker.getStability();

    report ("stability while locked to a tone", lockedStability);
    report ("frequency while locked", lockedFrequency);
    report ("stability once the tone is replaced by noise", stuckStability);

    check (lockedStability > 0.75, "a tracked tone reports high stability");
    check (stuckStability < 0.5, "a tracker that has stopped following the source reports low stability");
}

int main()
{
    testPitchAccuracy();
    testMissingFundamental();
    testUnpitchedMaterial();
    testNoteTransitions();
    testSourceCharacterisation();
    testAdaptiveRestraint();
    testReconstructionEngagement();
    testSubharmonicGating();
    testStabilityReportsTracking();

    std::cout << std::endl
              << (failures == 0 ? "PASSED " : "FAILED ")
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
