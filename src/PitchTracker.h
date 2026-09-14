#pragma once

#include "DspUtility.h"

namespace xyb
{

class PitchTracker
{
public:
    void prepare (double sampleRate);
    void reset();

    void process (const float* mono, int numSamples) noexcept;

    float getFrequency() const noexcept { return frequency; }
    float getConfidence() const noexcept { return confidence; }
    float getStability() const noexcept { return stability; }

    static constexpr float minimumFrequency = 24.0f;
    static constexpr float maximumFrequency = 320.0f;

private:
    void startFrame() noexcept;
    void advanceSearch (int lagBudget) noexcept;
    void finishFrame() noexcept;
    float refineLag (int lagIndex) const noexcept;

    Biquad decimationFilter[2];
    Biquad highPass;

    std::vector<float> history;
    std::vector<float> window;
    std::vector<float> difference;
    std::vector<float> normalised;

    double workingRate = 4000.0;
    int decimationFactor = 12;
    int decimationCounter = 0;

    int historySize = 0;
    int writeIndex = 0;
    int windowLength = 512;
    int hopLength = 128;
    int hopCounter = 0;
    int lagsPerStep = 1;

    int minimumLag = 13;
    int maximumLag = 167;
    int currentLag = 0;
    bool searching = false;
    float runningSum = 0.0f;

    float frequency = 0.0f;
    float confidence = 0.0f;
    float stability = 0.0f;
    float tracking = 0.0f;
    float smoothedFrequency = 0.0f;
    float deviation = 0.0f;
    float pendingFrequency = 0.0f;
    int pendingCount = 0;
    float silenceLevel = 0.0f;
};

} // namespace xyb
