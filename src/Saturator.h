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

struct ShaperControls
{
    float drive = 0.05f;
    float bias = 0.0f;
    float resting = 0.0f;
    float scale = 1.0f;
    float clipping = 0.0f;
    float push = 1.0f;
    float outputGain = 1.0f;
};

inline ShaperControls makeShaperControls (float driveAmount, float asymmetry, float clipping) noexcept
{
    ShaperControls controls;

    controls.drive = 0.05f + 3.6f * driveAmount * driveAmount;
    controls.bias = asymmetry * std::sqrt (controls.drive);
    controls.clipping = clipping;
    controls.push = 1.0f + 1.6f * clipping;

    const auto offset = std::tanh (controls.bias);
    controls.resting = offset;

    if (clipping > 0.0f)
        controls.resting += clipping * (softClip (offset * controls.push) - offset);

    controls.scale = 1.0f / juce::jmax (1.0f - offset * offset, 0.25f);

    return controls;
}

inline float shapeSample (float normalisedInput, const ShaperControls& controls) noexcept
{
    const auto bounded = juce::jlimit (-12.0f, 12.0f, normalisedInput);
    auto shaped = std::tanh (controls.drive * bounded + controls.bias);

    if (controls.clipping > 0.0f)
        shaped += controls.clipping * (softClip (shaped * controls.push) - shaped);

    return (shaped - controls.resting) * controls.scale;
}

} // namespace xyb
