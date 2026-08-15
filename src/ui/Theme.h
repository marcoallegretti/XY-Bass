#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace xyui
{

struct Theme
{
    juce::Colour plate { 0xff2b2f36 };
    juce::Colour plateHigh { 0xff3d434d };
    juce::Colour plateLow { 0xff1c1f24 };
    juce::Colour shelf { 0xff32373f };

    juce::Colour recess { 0xff15171b };
    juce::Colour recessDeep { 0xff0a0b0e };
    juce::Colour screen { 0xff0e1015 };

    juce::Colour metal { 0xff5c636e };
    juce::Colour metalHigh { 0xff9aa2af };
    juce::Colour metalLow { 0xff2f333a };

    juce::Colour text { 0xffc2c9d4 };
    juce::Colour textDim { 0xff767d89 };
    juce::Colour engrave { 0xff101216 };

    juce::Colour accent { 0xffe0954a };
    juce::Colour accentGlow { 0xffffc184 };

    float corner = 9.0f;
    float bevel = 1.3f;
    float relief = 8.0f;

    Theme withAccent (juce::Colour newAccent) const
    {
        auto copy = *this;
        copy.accent = newAccent;
        copy.accentGlow = newAccent.brighter (0.45f);
        return copy;
    }
};

inline const Theme& seriesTheme()
{
    static const Theme theme;
    return theme;
}

} // namespace xyui
