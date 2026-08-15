#pragma once

#include "Theme.h"

namespace xyui::surface
{

void drawInnerShadow (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                      juce::Colour colour, float radius, juce::Point<int> offset);

void drawBrushedFace (juce::Graphics&, juce::Rectangle<float> bounds, const Theme&, float corner);

void drawRaised (juce::Graphics&, juce::Rectangle<float> bounds, const Theme&, float corner,
                 float pressed = 0.0f);

void drawRecess (juce::Graphics&, juce::Rectangle<float> bounds, const Theme&, float corner,
                 juce::Colour fill);

void drawScreen (juce::Graphics&, juce::Rectangle<float> bounds, const Theme&, float corner);

void drawGlass (juce::Graphics&, juce::Rectangle<float> bounds, float corner);

void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, const Theme&, float angle);

void drawEngravedText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> bounds,
                       juce::Justification, const Theme&, juce::Colour tint);

void drawKnobBody (juce::Graphics&, juce::Rectangle<float> bounds, const Theme&, float angle,
                   float hover);

void drawIndicatorLamp (juce::Graphics&, juce::Rectangle<float> bounds, const Theme&, float brightness);

juce::Image renderChassis (juce::Rectangle<int> bounds, const Theme&, float scale);

} // namespace xyui::surface
