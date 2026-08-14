#pragma once

#include "DspUtility.h"

namespace xyb
{

class SubEngine
{
public:
    void prepare (double sampleRate);
    void reset();

    void setControls (float reinforcementAmount, float centreHz, float reconstructionAmount,
                      float subharmonicAmount, float fundamentalHz, float selectivity) noexcept;

    void updateBlock (int numSamples) noexcept;

    float process (float monoLow, float fundamentalBand) noexcept;

    float getReinforcementLevel() const noexcept { return reinforcementMeter.getValue(); }
    float getSynthesisLevel() const noexcept { return synthesisMeter.getValue(); }

private:
    TptSvf reinforcementBand;
    TptSvf subharmonicShaper;
    TptSvf outputLimitBand;

    EnvelopeFollower lowEnvelope;
    EnvelopeFollower fundamentalEnvelope;
    EnvelopeFollower reinforcementMeter;
    EnvelopeFollower synthesisMeter;
    DcBlocker dcBlocker;

    SmoothedScalar reinforcement, reconstruction, subharmonic, centre, oscillatorFrequency;

    double phase = 0.0;
    float sampleRate = 44100.0f;

    float dividerState = 1.0f;
    float previousFundamental = 0.0f;
    int dividerHold = 0;

    float compressiveGain = 1.0f;
    float compressiveIncrement = 0.0f;
    float trackedFrequency = 55.0f;
};

} // namespace xyb
