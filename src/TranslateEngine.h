#pragma once

#include "DspUtility.h"

namespace xyb
{

class TranslateEngine
{
public:
    void prepare (double sampleRate);
    void reset();

    void setControls (float amount, float tilt, float brightness, float fundamentalHz,
                      float confidence, float selectivity) noexcept;

    void updateBlock (int numSamples) noexcept;

    float extractFundamental (float monoLow) noexcept;
    float process (float fundamentalBand) noexcept;

    float getHarmonicWeight (int index) const noexcept { return harmonicWeights[(size_t) index]; }
    float getOutputLevel() const noexcept { return outputMeter.getValue(); }
    float getFundamentalMagnitude() const noexcept { return narrowMagnitude; }
    float getFundamentalQuadrature() const noexcept { return narrowQuadrature; }

private:
    void refreshWeights (int numSamples) noexcept;

    TptSvf narrowExtractor;
    TptSvf wideExtractor;
    TptSvf highPass;
    TptSvf lowPass;

    EnvelopeFollower narrowEnvelope;
    EnvelopeFollower wideEnvelope;
    EnvelopeFollower outputMeter;
    DcBlocker dcBlocker;

    SmoothedScalar amountSmoother, confidenceSmoother, tiltSmoother, brightnessSmoother;

    std::array<float, 4> harmonicWeights { { 0.0f, 0.0f, 0.0f, 0.0f } };
    std::array<float, 4> weightIncrement { { 0.0f, 0.0f, 0.0f, 0.0f } };

    SmoothedScalar frequencySmoother;

    float sampleRate = 44100.0f;
    float trackedFrequency = 55.0f;
    float levelCompensation = 1.0f;
    float compensationIncrement = 0.0f;
    float weightNormalisation = 1.0f;
    float normalisationIncrement = 0.0f;
    float narrowMagnitude = 0.0f;
    float narrowQuadrature = 0.0f;
    float wideMagnitude = 0.0f;
    float wideBand = 0.0f;
};

} // namespace xyb
