#include "Presets.h"

namespace xyb
{

const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets {
        { "Balanced Bass",   0.50f, 0.50f, 0.0f, 100.0f,  0.0f, true },
        { "Clean Weight",    0.18f, 0.16f, 0.0f, 100.0f,  0.0f, true },
        { "Club Sub",        0.06f, 0.34f, 0.0f, 100.0f,  0.0f, true },
        { "Phone Translation", 0.92f, 0.22f, 0.0f, 100.0f, 0.0f, true },
        { "808 Bigger",      0.30f, 0.46f, 0.0f, 100.0f,  0.0f, true },
        { "Kick Body",       0.34f, 0.28f, 0.0f, 100.0f,  0.0f, true },
        { "Techno Hammer",   0.22f, 0.80f, 0.0f, 100.0f,  0.0f, true },
        { "Bass Growl",      0.64f, 0.74f, 0.0f, 100.0f,  0.0f, true },
        { "Small Speaker",   0.86f, 0.40f, 0.0f, 100.0f,  0.0f, true },
        { "Gentle Mixbus",   0.44f, 0.14f, 0.0f,  60.0f,  0.0f, true },
        { "Dirty Rumble",    0.12f, 0.90f, 0.0f, 100.0f, -1.0f, true },
        { "Heavy But Clean", 0.28f, 0.07f, 0.0f, 100.0f,  0.0f, true },
        { "Translate Hard",  1.00f, 0.64f, 0.0f, 100.0f,  0.0f, true }
    };

    return presets;
}

} // namespace xyb
