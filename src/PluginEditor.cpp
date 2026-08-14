#include "PluginEditor.h"

namespace
{
const juce::Colour kBackground { 0xff0f1116 };
const juce::Colour kPanel { 0xff161a22 };
const juce::Colour kAccent { 0xffe8a05a };
const juce::Colour kText { 0xffd8dde6 };

juce::String describeNote (float frequency)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const auto midi = juce::roundToInt (69.0 + 12.0 * std::log2 ((double) frequency / 440.0));
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}
} // namespace

XYBassLookAndFeel::XYBassLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kText.withAlpha (0.65f));
    setColour (juce::ToggleButton::textColourId, kText.withAlpha (0.75f));
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void XYBassLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPosProportional, float rotaryStartAngle,
                                          float rotaryEndAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const auto lineWidth = 2.4f;
    const auto arcRadius = radius - lineWidth;

    juce::Path background;
    background.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                              rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.strokePath (background, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (kAccent.withAlpha (0.85f));
    g.strokePath (value, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    const auto pointer = juce::Point<float> (centre.x + std::sin (angle) * (arcRadius - 4.0f),
                                             centre.y - std::cos (angle) * (arcRadius - 4.0f));

    g.setColour (kText);
    g.drawLine ({ centre.x + std::sin (angle) * (arcRadius * 0.45f),
                  centre.y - std::cos (angle) * (arcRadius * 0.45f),
                  pointer.x, pointer.y }, 1.6f);
}

void XYBassLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                          bool shouldDrawButtonAsHighlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const auto on = button.getToggleState();

    g.setColour (on ? kAccent.withAlpha (0.20f) : juce::Colours::white.withAlpha (0.05f));
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (on ? kAccent.withAlpha (0.75f)
                    : juce::Colours::white.withAlpha (shouldDrawButtonAsHighlighted ? 0.28f : 0.14f));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    g.setColour (on ? juce::Colours::white.withAlpha (0.92f) : kText.withAlpha (0.60f));
    g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred);
}

XYBassEditor::XYBassEditor (XYBassProcessor& owner)
    : juce::AudioProcessorEditor (owner),
      processor (owner),
      pad (*owner.getValueTreeState().getParameter (xyb::ids::positionX),
           *owner.getValueTreeState().getParameter (xyb::ids::positionY),
           owner.getMeters())
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (pad);
    pad.onContextMenu = [this] { showContextMenu(); };

    configureRotary (inputSlider, inputLabel, "INPUT");
    configureRotary (mixSlider, mixLabel, "MIX");
    configureRotary (outputSlider, outputLabel, "OUTPUT");

    for (auto* button : { &autoGainButton, &deltaButton, &bypassButton })
        addAndMakeVisible (button);

    auto& state = processor.getValueTreeState();
    inputAttachment = std::make_unique<SliderAttachment> (state, xyb::ids::input, inputSlider);
    mixAttachment = std::make_unique<SliderAttachment> (state, xyb::ids::mix, mixSlider);
    outputAttachment = std::make_unique<SliderAttachment> (state, xyb::ids::output, outputSlider);
    autoGainAttachment = std::make_unique<ButtonAttachment> (state, xyb::ids::autoGain, autoGainButton);
    deltaAttachment = std::make_unique<ButtonAttachment> (state, xyb::ids::delta, deltaButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (state, xyb::ids::bypass, bypassButton);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (600.0 / 640.0);
    setResizeLimits (520, 554, 1080, 1152);

    const auto& stored = state.state;
    setSize ((int) stored.getProperty ("editorWidth", 600), (int) stored.getProperty ("editorHeight", 640));

    startTimerHz (12);
}

XYBassEditor::~XYBassEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void XYBassEditor::configureRotary (juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 16);
    slider.setColour (juce::Slider::textBoxTextColourId, kText.withAlpha (0.8f));
    slider.setTitle (text);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
    addAndMakeVisible (label);
}

void XYBassEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    auto header = getLocalBounds().removeFromTop (44).toFloat();

    g.setColour (kText);
    g.setFont (juce::FontOptions (16.0f).withStyle ("Bold"));
    g.drawText ("XY BASS", header.reduced (18.0f, 0.0f), juce::Justification::centredLeft);

    g.setColour (kText.withAlpha (0.35f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("23DSP", header.reduced (18.0f, 0.0f).translated (66.0f, 1.0f), juce::Justification::centredLeft);

    if (readout.isNotEmpty())
    {
        g.setColour (kAccent.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        g.drawText (readout, header.reduced (18.0f, 0.0f), juce::Justification::centredRight);
    }

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (getLocalBounds().removeFromBottom (104).removeFromTop (1));
}

void XYBassEditor::resized()
{
    auto stored = processor.getValueTreeState().state;
    stored.setProperty ("editorWidth", getWidth(), nullptr);
    stored.setProperty ("editorHeight", getHeight(), nullptr);

    auto bounds = getLocalBounds();
    bounds.removeFromTop (44);

    auto footer = bounds.removeFromBottom (104);
    pad.setBounds (bounds.reduced (14, 4));

    footer.removeFromTop (4);
    auto row = footer.removeFromTop (92).reduced (14, 0);

    auto knobs = row.removeFromLeft (row.getWidth() * 3 / 5);
    const auto knobWidth = knobs.getWidth() / 3;

    auto placeKnob = [] (juce::Rectangle<int> area, juce::Slider& slider, juce::Label& label)
    {
        label.setBounds (area.removeFromTop (12));
        slider.setBounds (area.reduced (4, 0));
    };

    placeKnob (knobs.removeFromLeft (knobWidth), inputSlider, inputLabel);
    placeKnob (knobs.removeFromLeft (knobWidth), mixSlider, mixLabel);
    placeKnob (knobs, outputSlider, outputLabel);

    const auto buttonWidth = juce::jmin (168, row.getWidth() - 12);
    auto buttons = row.withSizeKeepingCentre (buttonWidth, row.getHeight() - 8);
    const auto buttonHeight = buttons.getHeight() / 3;

    autoGainButton.setBounds (buttons.removeFromTop (buttonHeight).reduced (0, 2));
    deltaButton.setBounds (buttons.removeFromTop (buttonHeight).reduced (0, 2));
    bypassButton.setBounds (buttons.reduced (0, 2));
}

void XYBassEditor::timerCallback()
{
    const auto& meters = processor.getMeters();
    const auto confidence = meters.confidence.load (std::memory_order_relaxed);
    const auto fundamental = meters.fundamental.load (std::memory_order_relaxed);

    juce::String next;

    if (confidence > 0.55f && fundamental > 20.0f)
        next = juce::String (juce::roundToInt (fundamental)) + " Hz  " + describeNote (fundamental);

    if (next != readout)
    {
        readout = next;
        repaint (getLocalBounds().removeFromTop (44));
    }
}

void XYBassEditor::showContextMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    menu.addItem (1, "Reset To Centre");
    menu.addSeparator();

    juce::PopupMenu presets;
    const auto& factory = xyb::getFactoryPresets();

    for (int i = 0; i < (int) factory.size(); ++i)
        presets.addItem (100 + i, factory[(size_t) i].name, true, processor.getCurrentProgram() == i);

    menu.addSubMenu ("Presets", presets);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pad),
                        [this] (int result)
                        {
                            if (result == 1)
                            {
                                auto& state = processor.getValueTreeState();

                                for (auto* id : { xyb::ids::positionX, xyb::ids::positionY })
                                    if (auto* parameter = state.getParameter (id))
                                    {
                                        parameter->beginChangeGesture();
                                        parameter->setValueNotifyingHost (0.5f);
                                        parameter->endChangeGesture();
                                    }
                            }
                            else if (result >= 100)
                            {
                                processor.setCurrentProgram (result - 100);
                            }
                        });
}
