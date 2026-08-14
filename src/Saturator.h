#pragma once

#include "DspUtility.h"

namespace xyb
{

inline float softClip (float x, float knee = 0.66f) noexcept
{
    const auto magnitude = std::abs (x);

    if (magnitude <= knee)
        return x;

    const auto excess = magnitude - knee;
    const auto limited = knee + (1.0f - knee) * std::tanh (excess / (1.0f - knee));
    return x < 0.0f ? -limited : limited;
}

inline float shapeSample (float normalisedInput, float drive, float asymmetry, float clipping) noexcept
{
    const auto bounded = juce::jlimit (-12.0f, 12.0f, normalisedInput);
    const auto bias = asymmetry * std::sqrt (drive);
    const auto offset = std::tanh (bias);

    auto shaped = std::tanh (drive * bounded + bias);
    auto resting = offset;

    if (clipping > 0.0f)
    {
        const auto push = 1.0f + 1.6f * clipping;
        shaped += clipping * (softClip (shaped * push) - shaped);
        resting += clipping * (softClip (resting * push) - resting);
    }

    return (shaped - resting) / juce::jmax (1.0f - offset * offset, 0.25f);
}

} // namespace xyb
