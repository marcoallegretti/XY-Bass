#pragma once

#include "DspUtility.h"

namespace xyb
{

struct Bands
{
    float low = 0.0f;
    float mid = 0.0f;
    float character = 0.0f;
};

class BandSplitter
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        jassert (spec.numChannels <= states.size());

        setCrossovers (150.0f, 500.0f);
        reset();
    }

    void reset()
    {
        for (auto& state : states)
            state = {};
    }

    void setCrossovers (float lowHz, float characterHz)
    {
        lowSplit.setCutoff (sampleRate, lowHz);
        characterSplit.setCutoff (sampleRate, characterHz);
    }

    Bands process (int channel, float input) noexcept
    {
        auto& state = states[(size_t) channel];
        float low = 0.0f, high = 0.0f;
        Bands bands;

        lowSplit.split (state.low, input, low, high);
        bands.low = characterSplit.allpass (state.lowAllpass, low);
        characterSplit.split (state.character, high, bands.mid, bands.character);

        return bands;
    }

private:
    struct Section
    {
        float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
    };

    struct Crossover
    {
        void setCutoff (double newSampleRate, float frequency) noexcept
        {
            if (juce::exactlyEqual (frequency, cutoff) && juce::exactlyEqual (newSampleRate, rate))
                return;

            cutoff = frequency;
            rate = newSampleRate;

            g = (float) std::tan (juce::MathConstants<double>::pi * cutoff / rate);
            r2 = (float) std::sqrt (2.0);
            h = (float) (1.0 / (1.0 + r2 * g + g * g));
            damping = r2 + g;
        }

        void split (Section& state, float input, float& lowOut, float& highOut) const noexcept
        {
            const auto yH = (input - damping * state.s1 - state.s2) * h;

            const auto yB = g * yH + state.s1;
            state.s1 = g * yH + yB;

            const auto yL = g * yB + state.s2;
            state.s2 = g * yB + yL;

            const auto yH2 = (yL - damping * state.s3 - state.s4) * h;

            const auto yB2 = g * yH2 + state.s3;
            state.s3 = g * yH2 + yB2;

            const auto yL2 = g * yB2 + state.s4;
            state.s4 = g * yB2 + yL2;

            lowOut = yL2;
            highOut = yL - r2 * yB + yH - yL2;
        }

        float allpass (Section& state, float input) const noexcept
        {
            const auto yH = (input - damping * state.s1 - state.s2) * h;

            const auto yB = g * yH + state.s1;
            state.s1 = g * yH + yB;

            const auto yL = g * yB + state.s2;
            state.s2 = g * yB + yL;

            return yL - r2 * yB + yH;
        }

        double rate = 0.0;
        float cutoff = -1.0f;
        float g = 0.0f, r2 = 0.0f, h = 0.0f, damping = 0.0f;
    };

    struct ChannelState
    {
        Section low, lowAllpass, character;
    };

    Crossover lowSplit, characterSplit;
    std::array<ChannelState, 2> states {};
    double sampleRate = 44100.0;
};

} // namespace xyb
