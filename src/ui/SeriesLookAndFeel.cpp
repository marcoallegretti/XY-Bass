#include "SeriesLookAndFeel.h"

namespace xyui
{

SeriesLookAndFeel::SeriesLookAndFeel (Theme themeToUse)
    : theme (themeToUse)
{
    setColour (juce::Label::textColourId, theme.textDim);
    setColour (juce::Slider::textBoxTextColourId, theme.accent);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, theme.accent.withAlpha (0.28f));
    setColour (juce::ToggleButton::textColourId, theme.text);
    setColour (juce::PopupMenu::backgroundColourId, theme.plate);
    setColour (juce::PopupMenu::textColourId, theme.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme.accent.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::CaretComponent::caretColourId, theme.accent);
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::highlightColourId, theme.accent.withAlpha (0.28f));
    setColour (juce::TextEditor::textColourId, theme.accent);
}

void SeriesLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPosProportional, float rotaryStartAngle,
                                          float rotaryEndAngle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    const auto dial = juce::Rectangle<float> (size, size).withCentre (area.getCentre()).reduced (size * 0.14f);
    const auto centre = dial.getCentre();
    const auto trackRadius = size * 0.5f - 1.5f;
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    juce::Path trackShape;
    trackShape.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                              rotaryStartAngle, rotaryEndAngle, true);

    g.setColour (theme.recessDeep);
    g.strokePath (trackShape, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.strokePath (trackShape, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    if (std::abs (angle - rotaryStartAngle) > 0.01f)
    {
        juce::Path lit;
        lit.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f, rotaryStartAngle, angle, true);

        g.setColour (theme.accent.withAlpha (0.85f));
        g.strokePath (lit, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        g.setColour (theme.accentGlow.withAlpha (0.28f));
        g.strokePath (lit, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    surface::drawKnobBody (g, dial, theme, angle, slider.isMouseOverOrDragging() ? 1.0f : 0.0f);
}

void SeriesLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                          bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const auto on = button.getToggleState();
    const auto corner = juce::jmin (5.0f, bounds.getHeight() * 0.32f);

    surface::drawRaised (g, bounds, theme, corner, on || down ? 1.0f : 0.0f);

    auto content = bounds.reduced (7.0f, 0.0f);
    const auto lampSize = juce::jmin (7.0f, bounds.getHeight() * 0.36f);
    const auto lamp = juce::Rectangle<float> (lampSize, lampSize)
                          .withCentre ({ content.getX() + lampSize * 0.5f, content.getCentreY() });

    surface::drawIndicatorLamp (g, lamp, theme, on ? 1.0f : 0.0f);

    content.removeFromLeft (lampSize + 6.0f);

    g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
    surface::drawEngravedText (g, button.getButtonText(), content, juce::Justification::centredLeft, theme,
                               on ? theme.text.brighter (0.25f)
                                  : theme.textDim.withAlpha (highlighted ? 0.95f : 0.8f));
}

juce::Label* SeriesLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setJustificationType (juce::Justification::centred);
    label->setFont (juce::FontOptions (10.5f));
    return label;
}

void SeriesLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    const auto bounds = label.getLocalBounds().toFloat();

    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
    {
        surface::drawRecess (g, bounds.reduced (1.0f), theme, 3.0f, theme.screen);

        if (label.isBeingEdited())
            return;

        g.setFont (label.getFont());
        g.setColour (theme.accent.withAlpha (0.9f));
        g.drawText (label.getText(), bounds, juce::Justification::centred, false);
        return;
    }

    if (label.isBeingEdited())
        return;

    g.setFont (label.getFont());
    surface::drawEngravedText (g, label.getText(), bounds, label.getJustificationType(), theme,
                               label.findColour (juce::Label::textColourId));
}

void SeriesLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    surface::drawBrushedFace (g, bounds, theme, 5.0f);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawRoundedRectangle (bounds.reduced (1.5f), 4.0f, 1.0f);
}

void SeriesLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                           bool isSeparator, bool isActive, bool isHighlighted,
                                           bool isTicked, bool hasSubMenu, const juce::String& text,
                                           const juce::String& shortcutKeyText,
                                           const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        const auto line = area.toFloat().reduced (8.0f, 0.0f).withHeight (1.0f).withY (area.toFloat().getCentreY());
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (line);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (line.translated (0.0f, 1.0f));
        return;
    }

    auto bounds = area.toFloat().reduced (3.0f, 1.0f);

    if (isHighlighted && isActive)
    {
        g.setColour (theme.accent.withAlpha (0.18f));
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (theme.accent.withAlpha (0.5f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);
    }

    auto textArea = bounds.reduced (10.0f, 0.0f);

    if (isTicked)
    {
        const auto lamp = juce::Rectangle<float> (6.0f, 6.0f)
                              .withCentre ({ textArea.getX() + 3.0f, textArea.getCentreY() });
        surface::drawIndicatorLamp (g, lamp, theme, 1.0f);
    }

    textArea.removeFromLeft (14.0f);

    g.setFont (getPopupMenuFont());
    g.setColour ((textColour != nullptr ? *textColour : theme.text)
                     .withMultipliedAlpha (isActive ? 1.0f : 0.45f));
    g.drawText (text, textArea, juce::Justification::centredLeft, true);

    if (hasSubMenu)
    {
        juce::Path arrow;
        const auto arrowArea = bounds.removeFromRight (16.0f).reduced (5.0f, bounds.getHeight() * 0.3f);
        arrow.addTriangle (arrowArea.getX(), arrowArea.getY(), arrowArea.getRight(), arrowArea.getCentreY(),
                           arrowArea.getX(), arrowArea.getBottom());
        g.setColour (theme.textDim);
        g.fillPath (arrow);
    }

    juce::ignoreUnused (shortcutKeyText, icon);
}

juce::Font SeriesLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (12.0f));
}

} // namespace xyui
