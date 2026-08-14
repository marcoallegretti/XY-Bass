#pragma once

#include "DspUtility.h"

namespace xyb
{

struct SourceFeatures
{
    float fundamental = 0.0f;
    float pitchConfidence = 0.0f;
    float pitchStability = 0.0f;

    float subLevel = 0.0f;
    float bassLevel = 0.0f;
    float translateLevel = 0.0f;
    float characterLevel = 0.0f;

    float fundamentalStrength = 0.0f;
    float crest = 1.0f;
    float transient = 0.0f;
    float correlation = 1.0f;
    float level = 0.0f;
    float mixLikeness = 0.0f;
    float percussive = 0.0f;
    float tonal = 0.0f;
    float existingSaturation = 0.0f;
};

class SourceAnalyser
{
public:
    void prepare (double sampleRate);
    void reset();

    void pushMono (float mono) noexcept;
    void pushStereo (float lowLeft, float lowRight) noexcept;
    void setPitch (float frequency, float confidence, float stability) noexcept;
    void finishBlock (int numSamples) noexcept;

    const SourceFeatures& getFeatures() const noexcept { return features; }

private:
    TptSvf subProbe, bassProbe, characterProbe;
    EnvelopeFollower subEnvelope, bassEnvelope, translateEnvelope, characterEnvelope;
    EnvelopeFollower fastEnvelope, slowEnvelope, peakEnvelope;

    SmoothedScalar rmsSmoother, crestSmoother, transientSmoother, correlationSmoother;
    SmoothedScalar mixSmoother, percussiveSmoother;

    double correlationProduct = 0.0;
    double correlationLeft = 0.0;
    double correlationRight = 0.0;
    double squaredSum = 0.0;
    float percussivePeak = 0.0f;
    float percussiveRelease = 0.0f;
    int stereoCount = 0;

    SourceFeatures features;
};

} // namespace xyb
