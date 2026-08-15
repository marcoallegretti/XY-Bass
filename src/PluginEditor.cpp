#include "PluginEditor.h"

namespace
{
juce::String describeNote (float frequency)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const auto midi = juce::roundToInt (69.0 + 12.0 * std::log2 ((double) frequency / 440.0));
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}
} // namespace

XYBassEditor::XYBassEditor (XYBassProcessor& owner)
    : juce::AudioProcessorEditor (owner),
      processor (owner),
      theme (xyui::seriesTheme().withAccent (juce::Colour (0xffe0954a))),
      lookAndFeel (theme),
      pad (*owner.getValueTreeState().getParameter (xyb::ids::positionX),
           *owner.getValueTreeState().getParameter (xyb::ids::positionY),
           owner.getMeters(), theme)
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
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 17);
    slider.setTitle (text);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (9.5f).withStyle ("Bold"));
    label.setColour (juce::Label::textColourId, theme.textDim);
    addAndMakeVisible (label);
}

void XYBassEditor::renderChassis()
{
    const auto bounds = getLocalBounds();

    if (bounds.isEmpty())
        return;

    chassisScale = juce::jlimit (1.0f, 2.0f, juce::Component::getApproximateScaleFactorForComponent (this));

    chassis = juce::Image (juce::Image::ARGB,
                           juce::roundToInt ((float) bounds.getWidth() * chassisScale),
                           juce::roundToInt ((float) bounds.getHeight() * chassisScale), true);

    juce::Graphics g (chassis);
    g.addTransform (juce::AffineTransform::scale (chassisScale));

    auto face = bounds.toFloat();
    xyui::surface::drawBrushedFace (g, face, theme, 0.0f);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRect (face, 1.0f);

    if (! shelfBounds.isEmpty())
        xyui::surface::drawRaised (g, shelfBounds, theme, theme.corner + 1.0f);

    const auto header = face.removeFromTop (46.0f).reduced (18.0f, 0.0f);
    const auto titleFont = juce::Font (juce::FontOptions (17.0f).withStyle ("Bold"));

    g.setFont (titleFont);
    xyui::surface::drawEngravedText (g, "XY BASS", header, juce::Justification::centredLeft, theme, theme.text);

    const auto titleWidth = juce::GlyphArrangement::getStringWidth (titleFont, "XY BASS");

    g.setFont (juce::FontOptions (8.5f).withStyle ("Bold"));
    xyui::surface::drawEngravedText (g, "23DSP",
                                     header.withTrimmedLeft (titleWidth + 9.0f).translated (0.0f, 2.0f),
                                     juce::Justification::centredLeft, theme, theme.textDim.withAlpha (0.75f));

    if (! readoutBounds.isEmpty())
        xyui::surface::drawRecess (g, readoutBounds, theme, 4.0f, theme.screen);

    const auto screwRadius = 4.0f;
    const auto inset = 11.0f;
    const std::array<juce::Point<float>, 4> screws {
        { { inset, inset },
          { (float) bounds.getWidth() - inset, inset },
          { inset, (float) bounds.getHeight() - inset },
          { (float) bounds.getWidth() - inset, (float) bounds.getHeight() - inset } }
    };

    auto angle = 0.4f;

    for (const auto& position : screws)
    {
        xyui::surface::drawScrew (g, position, screwRadius, theme, angle);
        angle += 1.1f;
    }
}

void XYBassEditor::paint (juce::Graphics& g)
{
    if (chassis.isNull())
        renderChassis();

    g.drawImageTransformed (chassis, juce::AffineTransform::scale (1.0f / chassisScale));

    if (readoutBounds.isEmpty())
        return;

    if (readout.isNotEmpty())
    {
        g.setFont (juce::FontOptions (10.5f).withStyle ("Bold"));
        g.setColour (theme.accent.withAlpha (0.92f));
        g.drawText (readout, readoutBounds, juce::Justification::centred);
    }
    else
    {
        g.setFont (juce::FontOptions (10.5f).withStyle ("Bold"));
        g.setColour (theme.accent.withAlpha (0.09f));
        g.drawText ("---", readoutBounds, juce::Justification::centred);
    }
}

void XYBassEditor::resized()
{
    auto stored = processor.getValueTreeState().state;
    stored.setProperty ("editorWidth", getWidth(), nullptr);
    stored.setProperty ("editorHeight", getHeight(), nullptr);

    auto bounds = getLocalBounds();
    const auto header = bounds.removeFromTop (46);

    readoutBounds = header.toFloat().reduced (18.0f, 13.0f).removeFromRight (104.0f);

    auto footer = bounds.removeFromBottom (108);
    pad.setBounds (bounds.reduced (16, 2));

    shelfBounds = footer.toFloat().reduced (16.0f, 6.0f);

    auto row = shelfBounds.toNearestInt().reduced (14, 9);
    auto knobs = row.removeFromLeft (row.getWidth() * 3 / 5);
    const auto knobWidth = knobs.getWidth() / 3;

    auto placeKnob = [] (juce::Rectangle<int> area, juce::Slider& slider, juce::Label& label)
    {
        label.setBounds (area.removeFromTop (11));
        slider.setBounds (area.reduced (3, 0));
    };

    placeKnob (knobs.removeFromLeft (knobWidth), inputSlider, inputLabel);
    placeKnob (knobs.removeFromLeft (knobWidth), mixSlider, mixLabel);
    placeKnob (knobs, outputSlider, outputLabel);

    const auto buttonWidth = juce::jmin (156, row.getWidth() - 10);
    auto buttons = row.withSizeKeepingCentre (buttonWidth, row.getHeight() - 2);
    const auto buttonHeight = buttons.getHeight() / 3;

    autoGainButton.setBounds (buttons.removeFromTop (buttonHeight).reduced (0, 2));
    deltaButton.setBounds (buttons.removeFromTop (buttonHeight).reduced (0, 2));
    bypassButton.setBounds (buttons.reduced (0, 2));

    renderChassis();
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
        repaint (readoutBounds.toNearestInt().expanded (2));
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
