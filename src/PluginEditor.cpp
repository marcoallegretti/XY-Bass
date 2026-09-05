#include "PluginEditor.h"

namespace
{
juce::String describeNote (float frequency)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const auto midi = juce::roundToInt (69.0 + 12.0 * std::log2 ((double) frequency / 440.0));
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}
juce::String aboutText()
{
   #ifdef JucePlugin_VersionString
    const juce::String version { " " JucePlugin_VersionString };
   #else
    const juce::String version;
   #endif

    return "XY Bass" + version + "\n23DSP\n\n"
           "Licensed under the GNU Affero General Public License, version 3.\n"
           "This program comes with absolutely no warranty.\n"
           "The complete source code is available under the terms of that licence.";
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

    configureRotary (inputSlider, inputLabel, "INPUT", xyb::ids::input,
                     "Level into the processing. Double-click to reset.");
    configureRotary (mixSlider, mixLabel, "MIX", xyb::ids::mix,
                     "Blend of the generated low end against the dry signal.");
    configureRotary (outputSlider, outputLabel, "OUTPUT", xyb::ids::output,
                     "Level after processing.");

    autoGainButton.setTooltip ("Match the processed level to the input level.");
    deltaButton.setTooltip ("Listen to the generated low end on its own.");
    bypassButton.setTooltip ("Pass the input through unprocessed, latency compensated.");

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

    startTimerHz (24);
}

XYBassEditor::~XYBassEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void XYBassEditor::configureRotary (juce::Slider& slider, juce::Label& label, const juce::String& text,
                                    const juce::String& parameterId, const juce::String& tip)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 17);
    slider.setTitle (text);
    slider.setTooltip (tip);

    if (auto* parameter = processor.getValueTreeState().getParameter (parameterId))
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

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

    if (! meterBounds.isEmpty())
        xyui::surface::drawRecess (g, meterBounds.withTrimmedRight (13.0f), theme, 3.0f, theme.screen);

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

    if (! meterBounds.isEmpty())
    {
        const auto track = meterBounds.withTrimmedRight (13.0f).reduced (2.0f);

        if (meterLevel > 0.001f)
        {
            const auto filled = track.withWidth (track.getWidth() * meterLevel);
            g.setGradientFill ({ theme.accent.withAlpha (0.55f), track.getX(), track.getCentreY(),
                                 theme.accentGlow, track.getRight(), track.getCentreY(), false });
            g.fillRoundedRectangle (filled, 1.5f);
        }

        const auto lamp = juce::Rectangle<float> (7.0f, 7.0f)
                              .withCentre ({ meterBounds.getRight() - 3.5f, meterBounds.getCentreY() });
        xyui::surface::drawIndicatorLamp (g, lamp, theme, clipLevel);
    }

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

    if ((int) stored.getProperty ("editorWidth", 0) != getWidth()
        || (int) stored.getProperty ("editorHeight", 0) != getHeight())
    {
        stored.setProperty ("editorWidth", getWidth(), nullptr);
        stored.setProperty ("editorHeight", getHeight(), nullptr);
    }

    auto bounds = getLocalBounds();
    const auto header = bounds.removeFromTop (46);

    auto headerRight = header.toFloat().reduced (18.0f, 13.0f);
    readoutBounds = headerRight.removeFromRight (104.0f);
    headerRight.removeFromRight (10.0f);
    meterBounds = headerRight.removeFromRight (92.0f).withSizeKeepingCentre (92.0f, 9.0f);

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

    const auto engaged = (bypassButton.getToggleState() ? 0.0f : 1.0f)
                         * (float) (mixSlider.getValue() * 0.01);
    const auto level = xyb::meterDisplay (meters.outputLevel.load (std::memory_order_relaxed), -42.0f)
                       * engaged;
    const auto clip = meters.ceiling.load (std::memory_order_relaxed) * engaged;

    if (std::abs (level - meterLevel) > 0.004f || std::abs (clip - clipLevel) > 0.004f)
    {
        meterLevel = level;
        clipLevel = clip;
        repaint (meterBounds.toNearestInt().expanded (3));
    }

    pad.setEngagement (engaged);
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
    menu.addSeparator();
    menu.addItem (2, "About");

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
                            else if (result == 2)
                            {
                                juce::NativeMessageBox::showAsync (
                                    juce::MessageBoxOptions()
                                        .withIconType (juce::MessageBoxIconType::NoIcon)
                                        .withTitle ("XY Bass")
                                        .withMessage (aboutText())
                                        .withButton ("Close")
                                        .withAssociatedComponent (this),
                                    nullptr);
                            }
                            else if (result >= 100)
                            {
                                processor.setCurrentProgram (result - 100);
                            }
                        });
}
