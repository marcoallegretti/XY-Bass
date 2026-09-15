#pragma once

#include "DspUtility.h"

namespace xyb
{

// Splits the input into low, mid and character bands that sum exactly to the input delayed by
// getLatencySamples(). Every band is linear phase with that one delay, so processed bands can be
// recombined with a delayed dry signal at any balance without combing. The low-passes give each
// band the magnitude of a fourth order Linkwitz-Riley split and run at a decimated rate, since
// nothing they pass needs more.
class BandSplitter
{
public:
    static constexpr float lowestCrossover = 135.0f;
    static constexpr float highestCrossover = 260.0f;
    static constexpr float characterCrossover = 500.0f;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;

    void setCrossover (float lowHz) noexcept;

    void process (int channel, const float* input, float* low, float* mid, float* character,
                  int numSamples) noexcept;

    int getLatencySamples() const noexcept { return latency; }

private:
    struct ChannelState
    {
        juce::AudioBuffer<float> inputHistory, decimatedHistory, lowHistory, characterHistory, delayLine;
        int inputPosition = 0;
        int decimatedPosition = 0;
        int interpolatedPosition = 0;
        int delayPosition = 0;
        int phase = 0;
    };

    std::vector<ChannelState> channels;

    std::vector<float> decimator;
    std::vector<float> interpolator;
    std::vector<float> lowBank;
    std::vector<float> lowKernel;
    std::vector<float> characterKernel;

    int factor = 1;
    int decimatorHalf = 0;
    int kernelHalf = 0;
    int interpolatorTaps = 1;
    int bankSize = 1;
    int latency = 0;
    float bankPosition = -1.0f;
};

} // namespace xyb
