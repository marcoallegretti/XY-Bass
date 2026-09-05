#pragma once

#include "AutoGain.h"
#include "BandSplitter.h"
#include "LowEndDynamics.h"
#include "PitchTracker.h"
#include "Saturator.h"
#include "SourceAnalyser.h"
#include "SpectralBalance.h"
#include "SubEngine.h"
#include "TranslateEngine.h"
#include "XYMapping.h"

namespace xyb
{

inline float meterDisplay (float level, float floorDb) noexcept
{
    const auto decibels = juce::Decibels::gainToDecibels (level, floorDb);
    return juce::jlimit (0.0f, 1.0f, (decibels - floorDb) / -floorDb);
}

struct EngineMeters
{
    std::atomic<float> fundamental { 0.0f };
    std::atomic<float> confidence { 0.0f };
    std::atomic<float> lowEnvelope { 0.0f };
    std::atomic<float> subGeneration { 0.0f };
    std::atomic<float> harmonicGeneration { 0.0f };
    std::atomic<float> drive { 0.0f };
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<float> ceiling { 0.0f };
    std::atomic<float> harmonicTwo { 0.0f };
    std::atomic<float> harmonicThree { 0.0f };
    std::atomic<float> harmonicFour { 0.0f };
    std::atomic<float> harmonicFive { 0.0f };
};

class BassEngine
{
public:
    struct Parameters
    {
        float x = 0.5f;
        float y = 0.5f;
        float inputGainDb = 0.0f;
        float outputGainDb = 0.0f;
        float mix = 1.0f;
        bool autoGain = true;
        bool delta = false;
    };

    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void reset();

    void setParameters (const Parameters& newParameters) noexcept { parameters = newParameters; }
    const Parameters& getParameters() const noexcept { return parameters; }

    void process (juce::AudioBuffer<float>& buffer);
    void processChunk (juce::AudioBuffer<float>& buffer);
    void processBypassed (juce::AudioBuffer<float>& buffer, int numSamples);

    int getLatencySamples() const noexcept { return latencySamples; }
    int getPreparedBlockSize() const noexcept { return preparedBlockSize; }
    const EngineMeters& getMeters() const noexcept { return meters; }
    const SourceFeatures& getFeatures() const noexcept { return analyser.getFeatures(); }
    const EngineTargets& getTargets() const noexcept { return targets; }

private:
    void updateControls (int numSamples);
    void publishMeters();

    Parameters parameters;
    EngineTargets targets;
    EngineMeters meters;

    BandSplitter splitter;
    PitchTracker pitchTracker;
    SourceAnalyser analyser;
    SubEngine subEngine;
    TranslateEngine translateEngine;
    LowEndDynamics lowEndDynamics;
    SpectralBalance spectralBalance;
    AutoGain autoGain;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    juce::AudioBuffer<float> dryBuffer, saturationBuffer, parallelBuffer, monoBuffer;
    std::vector<ShaperControls> shaperControls;
    DelayBuffer dryDelay, parallelDelay, bypassDelay;

    std::array<std::array<Biquad, 2>, 2> subsonicFilter;
    std::array<DcBlocker, 2> outputDcBlocker;
    std::array<EnvelopeFollower, 2> channelLowEnvelope;

    EnvelopeFollower saturationEnvelope;
    EnvelopeFollower outputEnvelope;
    EnvelopeFollower transientFast;
    EnvelopeFollower transientSlow;

    SmoothedScalar inputGain, outputGain, mixAmount, monoAmount, driveControl;
    SmoothedScalar asymmetryControl, clippingControl, protectionControl, normalisationLevel;
    SmoothedScalar bassCrossoverControl, subsonicControl;
    SmoothedScalar autoGainSmoother, transientDepthControl, coreWeight, spreadControl;

    int preparedChannels = 2;
    int preparedBlockSize = 512;
    int latencySamples = 0;
    int oversamplingShift = 0;

    float smoothedSubsonic = 16.0f;
    float ceilingHold = 0.0f;
    double currentSampleRate = 48000.0;
};

} // namespace xyb
