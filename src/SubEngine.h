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

    float process (float monoLow, float fundamentalBand, float fundamentalMagnitude,
                   float fundamentalQuadrature) noexcept;

    float getReinforcementLevel() const noexcept { return reinforcementMeter.getValue(); }
    float getSynthesisLevel() const noexcept { return synthesisMeter.getValue(); }

private:
    TptSvf reinforcementBand;
    TptSvf outputLimitBand;

    EnvelopeFollower lowEnvelope;
    EnvelopeFollower reinforcementMeter;
    EnvelopeFollower synthesisMeter;
    DcBlocker dcBlocker;

    SmoothedScalar reinforcement, reconstruction, subharmonic, centre, oscillatorFrequency;

    QuadratureOscillator reconstructionOscillator;
    QuadratureOscillator subharmonicOscillator;
    float sampleRate = 44100.0f;


    float compressiveGain = 1.0f;
    float compressiveIncrement = 0.0f;
    float trackedFrequency = 55.0f;
};

} // namespace xyb
