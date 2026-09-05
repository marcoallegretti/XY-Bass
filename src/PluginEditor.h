#pragma once

#include "PluginProcessor.h"
#include "XYPad.h"
#include "ui/SeriesLookAndFeel.h"

class XYBassEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit XYBassEditor (XYBassProcessor&);
    ~XYBassEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showContextMenu();
    void configureRotary (juce::Slider&, juce::Label&, const juce::String& text,
                          const juce::String& parameterId, const juce::String& tip);
    void renderChassis();

    XYBassProcessor& processor;
    xyui::Theme theme;
    xyui::SeriesLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 620 };

    XYPad pad;

    juce::Image chassis;
    float chassisScale = 1.0f;

    juce::Rectangle<float> readoutBounds;
    juce::Rectangle<float> meterBounds;
    juce::Rectangle<float> shelfBounds;

    juce::Slider inputSlider, mixSlider, outputSlider;
    juce::Label inputLabel, mixLabel, outputLabel;
    juce::ToggleButton autoGainButton { "AUTO GAIN" };
    juce::ToggleButton deltaButton { "DELTA" };
    juce::ToggleButton bypassButton { "BYPASS" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> inputAttachment, mixAttachment, outputAttachment;
    std::unique_ptr<ButtonAttachment> autoGainAttachment, deltaAttachment, bypassAttachment;

    juce::String readout;
    float meterLevel = 0.0f;
    float clipLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XYBassEditor)
};
