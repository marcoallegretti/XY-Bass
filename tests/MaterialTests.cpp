#include "TestSignals.h"
#include "TestSuites.h"

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

double bandEnergy (const std::vector<float>& mono, double sampleRate, double lowHz, double highHz)
{
    xyb::Biquad highPassOne, highPassTwo, lowPassOne, lowPassTwo;

    for (auto* filter : { &highPassOne, &highPassTwo, &lowPassOne, &lowPassTwo })
        filter->prepare (sampleRate);

    highPassOne.setHighPass ((float) lowHz, 0.5412f);
    highPassTwo.setHighPass ((float) lowHz, 1.3066f);

    std::array<xyb::TptSvf, 2> lowPass;

    for (auto& filter : lowPass)
    {
        filter.prepare (sampleRate);
        filter.setQ (0.7071f);
        filter.setCutoff ((float) highHz);
    }

    double sum = 0.0;
    const auto skip = (int) (sampleRate * 0.25);

    for (size_t i = 0; i < mono.size(); ++i)
    {
        auto value = highPassTwo.process (highPassOne.process (mono[i]));
        value = lowPass[1].processLowPass (lowPass[0].processLowPass (value));

        if ((int) i >= skip)
            sum += (double) value * (double) value;
    }

    return std::sqrt (sum / juce::jmax (1, (int) mono.size() - skip));
}

std::vector<float> midOf (const Buffer& buffer)
{
    std::vector<float> result ((size_t) buffer.getNumSamples());

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        result[(size_t) i] = 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i));

    return result;
}

std::vector<float> sideOf (const Buffer& buffer)
{
    std::vector<float> result ((size_t) buffer.getNumSamples());

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        result[(size_t) i] = 0.5f * (buffer.getSample (0, i) - buffer.getSample (1, i));

    return result;
}

Buffer process (const Buffer& source, float x, float y, double sampleRate, bool autoGain = true)
{
    Buffer copy (source);
    xyb::BassEngine engine;
    engine.prepare (sampleRate, 256, copy.getNumChannels());

    auto parameters = position (x, y);
    parameters.autoGain = autoGain;
    engine.setParameters (parameters);
    render (engine, copy, 256);

    return copy;
}

void testMonoCompatibility()
{
    section ("mono compatibility");

    const auto sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 4.0);

    auto wide = makeBuffer (2, length);
    fillWideBass (wide, 55.0, sampleRate, 0.3f);

    const auto dryMonoEnergy = bandEnergy (midOf (wide), sampleRate, 30.0, 150.0);
    double worst = 1.0e9;

    for (int gridX = 0; gridX <= 2; ++gridX)
    {
        for (int gridY = 0; gridY <= 2; ++gridY)
        {
            const auto processed = process (wide, (float) gridX * 0.5f, (float) gridY * 0.5f, sampleRate, false);
            const auto monoEnergy = bandEnergy (midOf (processed), sampleRate, 30.0, 150.0);
            const auto gain = relativeDb (monoEnergy, dryMonoEnergy);

            worst = juce::jmin (worst, gain);
            report ("mono low gain at " + juce::String (gridX) + "/" + juce::String (gridY), gain);
        }
    }

    check (worst > -0.5, "no pad position loses low end when the mix is summed to mono");

    auto inverted = makeBuffer (2, length);
    fillSine (inverted, 50.0, sampleRate, 0.3f);

    for (int i = 0; i < length; ++i)
        inverted.setSample (1, i, -inverted.getSample (0, i));

    const auto invertedDry = bandEnergy (midOf (inverted), sampleRate, 30.0, 150.0);
    const auto invertedWet = bandEnergy (midOf (process (inverted, 0.0f, 0.3f, sampleRate, false)),
                                         sampleRate, 30.0, 150.0);

    report ("cancelling source mono energy before", relativeDb (invertedDry, 1.0));
    report ("cancelling source mono energy after", relativeDb (invertedWet, 1.0));

    check (invertedWet > invertedDry * 4.0, "a phase inverted low end is recovered rather than left cancelling");
}

void testStereoPreservation()
{
    section ("stereo preservation");

    const auto sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 4.0);

    auto measure = [&] (double spread)
    {
        auto source = makeBuffer (2, length);
        fillWideBass (source, 55.0, sampleRate, 0.3f, spread);

        for (int i = 0; i < length; ++i)
        {
            const auto angle = juce::MathConstants<double>::twoPi * i / sampleRate;
            const auto wideUpper = 0.12f * (float) std::sin (angle * 640.0);
            source.setSample (0, i, source.getSample (0, i) + wideUpper);
            source.setSample (1, i, source.getSample (1, i) - wideUpper);
        }

        const auto processed = process (source, 0.2f, 0.3f, sampleRate, false);

        const auto lowChange = relativeDb (bandEnergy (sideOf (processed), sampleRate, 25.0, 80.0),
                                           bandEnergy (sideOf (source), sampleRate, 25.0, 80.0));
        const auto upperChange = relativeDb (bandEnergy (sideOf (processed), sampleRate, 400.0, 2000.0),
                                             bandEnergy (sideOf (source), sampleRate, 400.0, 2000.0));

        return std::make_pair (lowChange, upperChange);
    };

    const auto mild = measure (0.25);
    const auto extreme = measure (0.92);

    report ("mild width, side change below 80 Hz", mild.first);
    report ("mild width, side change above 400 Hz", mild.second);
    report ("cancelling width, side change below 80 Hz", extreme.first);
    report ("cancelling width, side change above 400 Hz", extreme.second);

    check (mild.second > -1.0, "width above the bass region is left alone on usable material");
    check (extreme.second > -1.0, "width above the bass region is left alone on cancelling material");
    check (mild.first < -1.0, "a wide low end is tightened");
    check (mild.first > -10.0, "a usable stereo low end is not collapsed");
    check (extreme.first < -12.0, "a cancelling low end is collapsed toward the centre");
}

void testSpectralBalance()
{
    section ("spectral balance");

    const auto sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 5.0);

    auto source = makeBuffer (2, length);
    fillSaw (source, 55.0, sampleRate, 0.18f);
    addSine (source, 190.0, sampleRate, 0.1f);
    addSine (source, 520.0, sampleRate, 0.06f);

    const auto dryAnchor = bandEnergy (midOf (source), sampleRate, 45.0, 90.0);
    const auto dryMud = bandEnergy (midOf (source), sampleRate, 150.0, 260.0);
    const auto dryFizz = bandEnergy (midOf (source), sampleRate, 380.0, 700.0);

    const auto processed = process (source, 0.15f, 0.55f, sampleRate, false);

    const auto wetAnchor = bandEnergy (midOf (processed), sampleRate, 45.0, 90.0);
    const auto wetMud = bandEnergy (midOf (processed), sampleRate, 150.0, 260.0);
    const auto wetFizz = bandEnergy (midOf (processed), sampleRate, 380.0, 700.0);

    const auto mudChange = relativeDb (wetMud / juce::jmax (wetAnchor, 1.0e-9),
                                       dryMud / juce::jmax (dryAnchor, 1.0e-9));
    const auto fizzChange = relativeDb (wetFizz / juce::jmax (wetAnchor, 1.0e-9),
                                        dryFizz / juce::jmax (dryAnchor, 1.0e-9));

    report ("anchor gain", relativeDb (wetAnchor, dryAnchor));
    report ("mud balance change", mudChange);
    report ("upper mid balance change", fizzChange);

    check (relativeDb (wetAnchor, dryAnchor) > 1.0, "the low end is actually reinforced");
    check (mudChange < 3.0, "reinforcement does not drag the mud region up with it");
    check (fizzChange < 4.0, "generated content does not pile into the upper mids");
}

void testLevelMatching()
{
    section ("level matched enhancement");

    const auto sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 6.0);

    struct Material
    {
        const char* name;
        std::function<void (Buffer&)> fill;
    };

    const std::array<Material, 3> materials { {
        { "full mix", [&] (Buffer& b) { fillFullMix (b, sampleRate, 0.35f); } },
        { "808", [&] (Buffer& b) { fillEightOhEight (b, 45.0, sampleRate, 0.4f, 0.5); } },
        { "electric bass", [&] (Buffer& b) { fillSaw (b, 65.4, sampleRate, 0.18f); } }
    } };

    for (const auto& material : materials)
    {
        auto source = makeBuffer (2, length);
        material.fill (source);

        const auto processed = process (source, 0.3f, 0.45f, sampleRate, true);

        const auto dryMono = midOf (source);
        const auto wetMono = midOf (processed);

        const auto dryBroadband = bandEnergy (dryMono, sampleRate, 200.0, 12000.0);
        const auto wetBroadband = bandEnergy (wetMono, sampleRate, 200.0, 12000.0);
        const auto dryLow = bandEnergy (dryMono, sampleRate, 30.0, 120.0);
        const auto wetLow = bandEnergy (wetMono, sampleRate, 30.0, 120.0);

        const auto broadbandChange = relativeDb (wetBroadband, dryBroadband);
        const auto lowChange = relativeDb (wetLow, dryLow);

        report (juce::String (material.name) + " broadband change", broadbandChange);
        report (juce::String (material.name) + " low end change", lowChange);

        check (lowChange > 0.5, juce::String (material.name) + " still gains low end");

        if (dryBroadband > dryLow * 0.1)
            check (std::abs (broadbandChange) < 2.5,
                   juce::String (material.name) + " keeps overall loudness comparable");
    }
}

void testPathologicalMaterial()
{
    section ("pathological material");

    const auto sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 3.0);

    struct Material
    {
        const char* name;
        std::function<void (Buffer&)> fill;
    };

    const std::array<Material, 8> materials { {
        { "sweep", [&] (Buffer& b) { fillSweep (b, 20.0, 4000.0, sampleRate, 0.4f); } },
        { "square", [&] (Buffer& b) { fillSquare (b, 41.2, sampleRate, 0.3f); } },
        { "808", [&] (Buffer& b) { fillEightOhEight (b, 38.0, sampleRate, 0.6f, 0.4); } },
        { "two notes", [&] (Buffer& b) { fillSaw (b, 55.0, sampleRate, 0.2f);
                                         Buffer other = makeBuffer (b.getNumChannels(), b.getNumSamples());
                                         fillSaw (other, 73.4, sampleRate, 0.2f);
                                         for (int c = 0; c < b.getNumChannels(); ++c)
                                             b.addFrom (c, 0, other, c, 0, b.getNumSamples()); } },
        { "already distorted", [&] (Buffer& b) { fillSaturatedSine (b, 45.0, sampleRate, 0.5f, 8.0f); } },
        { "clipped", [&] (Buffer& b) { fillSine (b, 40.0, sampleRate, 1.0f);
                                       for (int c = 0; c < b.getNumChannels(); ++c)
                                           for (int i = 0; i < b.getNumSamples(); ++i)
                                               b.setSample (c, i, juce::jlimit (-0.5f, 0.5f, b.getSample (c, i)) * 2.0f); } },
        { "noisy kick", [&] (Buffer& b) { fillKick (b, sampleRate, 0.5f);
                                          Buffer noise = makeBuffer (b.getNumChannels(), b.getNumSamples());
                                          fillNoise (noise, 0.08f, 77);
                                          for (int c = 0; c < b.getNumChannels(); ++c)
                                              b.addFrom (c, 0, noise, c, 0, b.getNumSamples()); } },
        { "very quiet", [&] (Buffer& b) { fillNotePattern (b, 55.0, sampleRate, 0.0015f, 0.5); } }
    } };

    for (const auto& material : materials)
    {
        auto source = makeBuffer (2, length);
        material.fill (source);

        double worstPeak = 0.0;
        bool finite = true;

        for (int gridX = 0; gridX <= 2; ++gridX)
        {
            for (int gridY = 0; gridY <= 2; ++gridY)
            {
                const auto processed = process (source, (float) gridX * 0.5f, (float) gridY * 0.5f, sampleRate);

                for (int channel = 0; channel < processed.getNumChannels(); ++channel)
                {
                    const auto* data = processed.getReadPointer (channel);

                    for (int i = 0; i < processed.getNumSamples(); ++i)
                    {
                        if (! (std::abs (data[i]) < 1.0e6f))
                            finite = false;

                        worstPeak = juce::jmax (worstPeak, (double) std::abs (data[i]));
                    }
                }
            }
        }

        report (juce::String (material.name) + " peak", worstPeak);
        check (finite, juce::String (material.name) + " stays finite across the pad");
        check (worstPeak < 1.05, juce::String (material.name) + " respects the output ceiling");
    }
}

struct MeterSnapshot
{
    float lowEnvelope = 0.0f;
    float subGeneration = 0.0f;
    float harmonicGeneration = 0.0f;
    float drive = 0.0f;
    float outputLevel = 0.0f;
    float confidence = 0.0f;
    float fundamental = 0.0f;
};

MeterSnapshot captureMeters (const Buffer& source, float x, float y, double sampleRate)
{
    Buffer copy (source);
    xyb::BassEngine engine;
    engine.prepare (sampleRate, 256, copy.getNumChannels());
    engine.setParameters (position (x, y));
    render (engine, copy, 256);

    const auto& meters = engine.getMeters();

    return { meters.lowEnvelope.load(), meters.subGeneration.load(), meters.harmonicGeneration.load(),
             meters.drive.load(), meters.outputLevel.load(), meters.confidence.load(),
             meters.fundamental.load() };
}

void testMeterRanges()
{
    section ("interface meters");

    const auto sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 3.0);

    auto source = makeBuffer (2, length);
    fillSaw (source, 55.0, sampleRate, 0.18f);

    const auto centre = captureMeters (source, 0.5f, 0.5f, sampleRate);
    const auto sub = captureMeters (source, 0.0f, 0.2f, sampleRate);
    const auto translate = captureMeters (source, 1.0f, 0.2f, sampleRate);
    const auto dirty = captureMeters (source, 0.5f, 1.0f, sampleRate);

    const auto lowReading = xyb::meterDisplay (centre.lowEnvelope, -42.0f);
    const auto subReading = xyb::meterDisplay (sub.subGeneration, -48.0f);
    const auto harmonicReading = xyb::meterDisplay (translate.harmonicGeneration, -54.0f);
    const auto outputReading = xyb::meterDisplay (centre.outputLevel, -42.0f);

    report ("low energy reading", lowReading);
    report ("sub generation reading", subReading);
    report ("harmonic generation reading", harmonicReading);
    report ("output level reading", outputReading);
    report ("drive reading at clean", sub.drive);
    report ("drive reading at dirty", dirty.drive);
    report ("tracked fundamental", centre.fundamental);
    report ("confidence", centre.confidence);

    auto quiet = makeBuffer (2, length);
    fillSaw (quiet, 55.0, sampleRate, 0.006f);

    const auto quietCentre = captureMeters (quiet, 0.5f, 0.5f, sampleRate);
    const auto quietReading = xyb::meterDisplay (quietCentre.lowEnvelope, -42.0f);
    report ("low energy reading on a quiet source", quietReading);

    check (lowReading > 0.15f && lowReading < 1.0f, "the low energy display is readable and not pinned");
    check (quietReading < lowReading - 0.2f, "the display tracks level rather than sitting at full scale");
    check (subReading > 0.15f && subReading < 1.0f, "the sub generation display is readable and not pinned");
    check (harmonicReading > 0.15f && harmonicReading < 1.0f,
           "the harmonic display is readable and not pinned");
    check (outputReading > 0.15f && outputReading < 1.0f, "the output display is readable and not pinned");

    check (translate.harmonicGeneration > sub.harmonicGeneration * 5.0f,
           "the harmonic display only lights up when harmonics are generated");
    check (sub.subGeneration > translate.subGeneration,
           "the sub display follows the sub side of the pad");
    check (dirty.drive > sub.drive + 0.3f, "the drive display follows the dirty axis");
    check (std::abs (centre.fundamental - 55.0f) < 1.0f, "the readout reports the true fundamental");
    check (centre.confidence > 0.55f, "the readout only appears when the tracker is confident");
}

void testReconstructionStability()
{
    section ("reconstruction stability");

    const auto sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 8.0);

    auto source = makeBuffer (2, length);
    addSine (source, 110.0, sampleRate, 0.09f);
    addSine (source, 220.0, sampleRate, 0.24f);
    addSine (source, 330.0, sampleRate, 0.20f);
    addSine (source, 440.0, sampleRate, 0.14f);

    const auto processed = process (source, 0.0f, 0.2f, sampleRate, false);
    const auto mono = midOf (processed);

    xyb::TptSvf probe;
    probe.prepare (sampleRate);
    probe.setQ (3.0f);
    probe.setCutoff (110.0f);

    xyb::EnvelopeFollower follower;
    follower.prepare (sampleRate);
    follower.setTimes (30.0f, 30.0f);

    std::vector<float> envelope;
    const auto settle = (int) (sampleRate * 3.0);

    for (int i = 0; i < length; ++i)
    {
        const auto band = probe.processBandPass (mono[(size_t) i]);
        const auto level = follower.process (band);

        if (i >= settle)
            envelope.push_back (level);
    }

    std::vector<float> sorted (envelope);
    std::sort (sorted.begin(), sorted.end());

    const auto low = sorted[(size_t) ((double) sorted.size() * 0.05)];
    const auto high = sorted[(size_t) ((double) sorted.size() * 0.95)];
    const auto swing = relativeDb (high, juce::jmax ((double) low, 1.0e-9));

    const auto dryFundamental = magnitudeAt (midOf (source).data() + settle, length - settle, 110.0, sampleRate);
    const auto wetFundamental = magnitudeAt (mono.data() + settle, length - settle, 110.0, sampleRate);

    report ("reconstruction gain at the fundamental", relativeDb (wetFundamental, dryFundamental));
    report ("fundamental envelope swing", swing);

    check (relativeDb (wetFundamental, dryFundamental) > 1.0,
           "reconstruction actually reinforces a weak fundamental");
    check (swing < 3.0, "the reinforced fundamental does not beat against the source");

    auto rooted = makeBuffer (2, (int) (sampleRate * 5.0));
    addSine (rooted, 45.0, sampleRate, 0.07f);
    addSine (rooted, 90.0, sampleRate, 0.24f);
    addSine (rooted, 135.0, sampleRate, 0.20f);
    addSine (rooted, 180.0, sampleRate, 0.14f);

    Buffer inverted (rooted);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < inverted.getNumSamples(); ++i)
            inverted.setSample (channel, i, -inverted.getSample (channel, i));

    const auto positive = process (rooted, 0.0f, 0.0f, sampleRate, false);
    const auto negative = process (inverted, 0.0f, 0.0f, sampleRate, false);

    const auto from = (int) (sampleRate * 3.0);
    double asymmetryError = 0.0;
    double reference = 0.0;

    for (int i = from; i < positive.getNumSamples(); ++i)
    {
        reference = juce::jmax (reference, (double) std::abs (positive.getSample (0, i)));
        asymmetryError = juce::jmax (asymmetryError, (double) std::abs (positive.getSample (0, i)
                                                                        + negative.getSample (0, i)));
    }

    report ("polarity asymmetry", relativeDb (asymmetryError, juce::jmax (reference, 1.0e-9)));

    check (relativeDb (asymmetryError, juce::jmax (reference, 1.0e-9)) < -40.0,
           "all generated low end follows the polarity of the source");
}

void testDcAndSubsonic()
{
    section ("dc and subsonic rejection");

    const auto sampleRate = 48000.0;
    const auto length = (int) (sampleRate * 4.0);

    auto source = makeBuffer (2, length);
    fillSine (source, 55.0, sampleRate, 0.3f);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < length; ++i)
            source.setSample (channel, i, source.getSample (channel, i) + 0.25f
                                              + 0.15f * (float) std::sin (juce::MathConstants<double>::twoPi * 6.0 * i / sampleRate));

    const auto processed = process (source, 0.3f, 0.6f, sampleRate, false);
    const auto mono = midOf (processed);
    const auto dryMono = midOf (source);

    double offset = 0.0;
    const auto start = (int) (sampleRate * 2.0);

    for (int i = start; i < length; ++i)
        offset += mono[(size_t) i];

    offset = std::abs (offset / (double) (length - start));

    const auto span = length - start;
    const auto drySubsonic = magnitudeAt (dryMono.data() + start, span, 6.0, sampleRate);
    const auto wetSubsonic = magnitudeAt (mono.data() + start, span, 6.0, sampleRate);
    const auto dryMusical = magnitudeAt (dryMono.data() + start, span, 55.0, sampleRate);
    const auto wetMusical = magnitudeAt (mono.data() + start, span, 55.0, sampleRate);

    report ("residual dc", offset);
    report ("infrasonic attenuation", relativeDb (wetSubsonic, drySubsonic));
    report ("musical band change", relativeDb (wetMusical, dryMusical));

    check (offset < 0.002, "a dc offset in the source is removed");
    check (relativeDb (wetSubsonic, drySubsonic) < -25.0, "infrasonic content is strongly attenuated");
    check (relativeDb (wetMusical, dryMusical) > -1.0, "the subsonic filter leaves the musical band alone");
}

} // namespace

int runMaterialTests()
{
    testMonoCompatibility();
    testStereoPreservation();
    testSpectralBalance();
    testLevelMatching();
    testReconstructionStability();
    testMeterRanges();
    testPathologicalMaterial();
    testDcAndSubsonic();

    std::cout << std::endl
              << (failures == 0 ? "PASSED " : "FAILED ")
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
