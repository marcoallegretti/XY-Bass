#pragma once

#include <juce_dsp/juce_dsp.h>

namespace xyb
{

class HalfbandOversampler
{
public:
    void prepare (int numChannels, int maximumBlockSize, bool enabled)
    {
        channels = juce::jmax (1, numChannels);
        active = enabled;
        prepared = true;

        if (! active)
        {
            latency = 0;
            return;
        }

        const auto up = juce::dsp::FilterDesign<float>::designFIRLowpassHalfBandEquirippleMethod (0.05f, -90.0f);
        const auto down = juce::dsp::FilterDesign<float>::designFIRLowpassHalfBandEquirippleMethod (0.06f, -75.0f);

        upStage.design (*up);
        downStage.design (*down);

        latency = (int) ((up->getFilterOrder() + down->getFilterOrder()) / 4);
        jassert ((up->getFilterOrder() + down->getFilterOrder()) % 4 == 0);

        upsampled.setSize (channels, juce::jmax (1, maximumBlockSize) * 2);
        upHistory.setSize (channels, upStage.length * 2);
        downHistory.setSize (channels, downStage.length * 2);
        oddDelay.setSize (channels, downStage.oddLength);

        upPosition.assign ((size_t) channels, 0);
        downPosition.assign ((size_t) channels, 0);
        oddPosition.assign ((size_t) channels, 0);

        reset();
    }

    void reset() noexcept
    {
        upsampled.clear();
        upHistory.clear();
        downHistory.clear();
        oddDelay.clear();
    }

    bool isPrepared() const noexcept { return prepared; }
    int getLatencyInSamples() const noexcept { return latency; }

    juce::dsp::AudioBlock<float> processSamplesUp (const juce::dsp::AudioBlock<float>& input) noexcept
    {
        if (! active)
            return input;

        const auto numChannels = juce::jmin ((int) input.getNumChannels(), channels);
        const auto numSamples = juce::jmin ((int) input.getNumSamples(), upsampled.getNumSamples() / 2);
        const auto length = upStage.length;
        const auto half = upStage.half;
        const auto pairs = upStage.pairs;
        const auto* taps = upStage.taps.data();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* in = input.getChannelPointer ((size_t) channel);
            auto* out = upsampled.getWritePointer (channel);
            auto* history = upHistory.getWritePointer (channel);
            auto position = upPosition[(size_t) channel];

            for (int i = 0; i < numSamples; ++i)
            {
                position = position == 0 ? length - 1 : position - 1;

                const auto scaled = 2.0f * in[i];
                history[position] = scaled;
                history[position + length] = scaled;

                const auto* window = history + position;
                auto sum = 0.0f;

                for (int j = 0; j < pairs; ++j)
                    sum += (window[half - j] + window[j]) * taps[j];

                out[i << 1] = sum;
                out[(i << 1) + 1] = window[upStage.centreAge] * upStage.centreTap;
            }

            upPosition[(size_t) channel] = position;
        }

        return juce::dsp::AudioBlock<float> (upsampled).getSubsetChannelBlock (0, (size_t) numChannels)
                                                       .getSubBlock (0, (size_t) numSamples * 2);
    }

    void processSamplesDown (juce::dsp::AudioBlock<float>& output) noexcept
    {
        if (! active)
            return;

        const auto numChannels = juce::jmin ((int) output.getNumChannels(), channels);
        const auto numSamples = juce::jmin ((int) output.getNumSamples(), upsampled.getNumSamples() / 2);
        const auto length = downStage.length;
        const auto half = downStage.half;
        const auto pairs = downStage.pairs;
        const auto oddLength = downStage.oddLength;
        const auto* taps = downStage.taps.data();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* in = upsampled.getReadPointer (channel);
            auto* out = output.getChannelPointer ((size_t) channel);
            auto* history = downHistory.getWritePointer (channel);
            auto* odd = oddDelay.getWritePointer (channel);
            auto position = downPosition[(size_t) channel];
            auto oddIndex = oddPosition[(size_t) channel];

            for (int i = 0; i < numSamples; ++i)
            {
                position = position == 0 ? length - 1 : position - 1;

                const auto even = in[i << 1];
                history[position] = even;
                history[position + length] = even;

                const auto* window = history + position;
                auto sum = 0.0f;

                for (int j = 0; j < pairs; ++j)
                    sum += (window[half - j] + window[j]) * taps[j];

                sum += odd[oddIndex] * downStage.centreTap;
                odd[oddIndex] = in[(i << 1) + 1];
                out[i] = sum;

                oddIndex = oddIndex == 0 ? oddLength - 1 : oddIndex - 1;
            }

            downPosition[(size_t) channel] = position;
            oddPosition[(size_t) channel] = oddIndex;
        }
    }

private:
    struct Stage
    {
        void design (const juce::dsp::FIR::Coefficients<float>& coefficients)
        {
            const auto order = (int) coefficients.getFilterOrder();
            const auto* raw = coefficients.getRawCoefficients();

            jassert (order % 4 == 2);

            const auto halfOrder = order / 2;

            // JUCE shifts its state two places per step, so even tap j reads the input from
            // j steps ago and from halfOrder - j steps ago, without moving any data.
            half = halfOrder;
            length = halfOrder + 1;
            pairs = (halfOrder + 1) / 2;
            centreTap = raw[halfOrder];
            centreAge = halfOrder - (halfOrder + 1) / 2;
            oddLength = halfOrder / 2 + 1;

            taps.resize ((size_t) pairs);

            for (int j = 0; j < pairs; ++j)
                taps[(size_t) j] = raw[2 * j];
        }

        std::vector<float> taps;
        float centreTap = 0.0f;
        int half = 0;
        int length = 1;
        int pairs = 0;
        int centreAge = 0;
        int oddLength = 1;
    };

    Stage upStage, downStage;

    juce::AudioBuffer<float> upsampled, upHistory, downHistory, oddDelay;
    std::vector<int> upPosition, downPosition, oddPosition;

    int channels = 1;
    int latency = 0;
    bool active = false;
    bool prepared = false;
};

} // namespace xyb
