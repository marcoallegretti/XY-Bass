#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace xyb
{

struct Preset
{
    const char* name;
    float x;
    float y;
    float mix;
    float output;
    bool autoGain;
};

const std::vector<Preset>& getFactoryPresets();

} // namespace xyb
