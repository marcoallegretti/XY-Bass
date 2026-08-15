#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "BassEngine.h"
#include "ui/SeriesLookAndFeel.h"

class XYPad : public juce::Component,
              private juce::Timer
{
public:
    XYPad (juce::RangedAudioParameter& xParameter,
           juce::RangedAudioParameter& yParameter,
           const xyb::EngineMeters& engineMeters,
           const xyui::Theme& themeToUse);

    ~XYPad() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

    std::function<void()> onContextMenu;

private:
    void timerCallback() override;
    void updateFromMouse (const juce::MouseEvent& event);
    void sendPosition (float newX, float newY);
    juce::Rectangle<float> getScreenBounds() const;
    juce::Point<float> positionToPoint (float valueX, float valueY) const;

    void renderChrome();
    void paintWaves (juce::Graphics& g, juce::Rectangle<float> area);
    void paintHarmonics (juce::Graphics& g, juce::Rectangle<float> area);
    void paintTexture (juce::Graphics& g, juce::Rectangle<float> area);
    void paintPuck (juce::Graphics& g, juce::Rectangle<float> area);

    const xyb::EngineMeters& meters;
    const xyui::Theme& theme;

    juce::ParameterAttachment attachmentX, attachmentY;

    juce::Image chrome;
    float chromeScale = 1.0f;

    float valueX = 0.5f;
    float valueY = 0.5f;

    float wavePhase = 0.0f;
    float smoothedFundamental = 55.0f;
    float smoothedLowLevel = 0.0f;
    float smoothedHarmonicLevel = 0.0f;
    float smoothedSubLevel = 0.0f;
    float smoothedDrive = 0.0f;
    float smoothedOutput = 0.0f;
    std::array<float, 4> smoothedWeights { { 0.0f, 0.0f, 0.0f, 0.0f } };

    bool focused = false;
    bool dragging = false;

    juce::Point<float> dragAnchor;
    juce::Point<float> dragOrigin;

    struct TexturePoint
    {
        float x;
        float y;
        float length;
        float angle;
    };

    std::vector<TexturePoint> texture;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XYPad)
};
