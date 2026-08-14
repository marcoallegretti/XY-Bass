#include "XYPad.h"

namespace
{
constexpr float kPadInset = 18.0f;
constexpr float kLowFrequency = 30.0f;
constexpr float kHighFrequency = 2000.0f;

float frequencyToPosition (float frequency)
{
    const auto clamped = juce::jlimit (kLowFrequency, kHighFrequency, frequency);
    return std::log (clamped / kLowFrequency) / std::log (kHighFrequency / kLowFrequency);
}

float approach (float current, float target, float rate)
{
    return current + (target - current) * rate;
}
} // namespace

XYPad::XYPad (juce::RangedAudioParameter& xParameter,
              juce::RangedAudioParameter& yParameter,
              const xyb::EngineMeters& engineMeters)
    : meters (engineMeters),
      attachmentX (xParameter, [this] (float value) { valueX = value; repaint(); }),
      attachmentY (yParameter, [this] (float value) { valueY = value; repaint(); })
{
    attachmentX.sendInitialUpdate();
    attachmentY.sendInitialUpdate();
    setWantsKeyboardFocus (true);
    setTitle ("Bass character pad");
    setDescription ("Horizontal axis moves between sub weight and small speaker translation. "
                    "Vertical axis moves between clean and dirty processing.");
    setHelpText ("Drag to set the bass character. Arrow keys nudge, shift for fine steps, "
                 "double click returns to the centre.");
    startTimerHz (30);
}

XYPad::~XYPad()
{
    stopTimer();
}

juce::Rectangle<float> XYPad::getPadBounds() const
{
    return getLocalBounds().toFloat().reduced (kPadInset);
}

juce::Point<float> XYPad::positionToPoint (float x, float y) const
{
    const auto area = getPadBounds();
    return { area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight() };
}

void XYPad::resized()
{
    juce::Random random (0x23D5Fu);

    texture.clear();
    texture.reserve (220);

    for (int i = 0; i < 220; ++i)
    {
        TexturePoint point;
        point.x = random.nextFloat();
        point.y = random.nextFloat();
        point.length = 4.0f + random.nextFloat() * 12.0f;
        point.angle = (random.nextFloat() - 0.5f) * 1.4f;
        texture.push_back (point);
    }
}

void XYPad::timerCallback()
{
    const auto fundamental = meters.fundamental.load (std::memory_order_relaxed);

    if (fundamental > 20.0f)
        smoothedFundamental = approach (smoothedFundamental, fundamental, 0.25f);
    smoothedLowLevel = approach (smoothedLowLevel, meters.lowEnvelope.load (std::memory_order_relaxed), 0.3f);
    smoothedSubLevel = approach (smoothedSubLevel, meters.subGeneration.load (std::memory_order_relaxed), 0.3f);
    smoothedHarmonicLevel = approach (smoothedHarmonicLevel, meters.harmonicGeneration.load (std::memory_order_relaxed), 0.3f);
    smoothedDrive = approach (smoothedDrive, meters.drive.load (std::memory_order_relaxed), 0.2f);
    smoothedOutput = approach (smoothedOutput, meters.outputLevel.load (std::memory_order_relaxed), 0.3f);

    smoothedWeights[0] = approach (smoothedWeights[0], meters.harmonicTwo.load (std::memory_order_relaxed), 0.2f);
    smoothedWeights[1] = approach (smoothedWeights[1], meters.harmonicThree.load (std::memory_order_relaxed), 0.2f);
    smoothedWeights[2] = approach (smoothedWeights[2], meters.harmonicFour.load (std::memory_order_relaxed), 0.2f);
    smoothedWeights[3] = approach (smoothedWeights[3], meters.harmonicFive.load (std::memory_order_relaxed), 0.2f);

    wavePhase += smoothedFundamental / (30.0f * 9.0f);
    wavePhase -= std::floor (wavePhase);

    repaint();
}

void XYPad::paint (juce::Graphics& g)
{
    const auto area = getPadBounds();

    paintField (g, area);
    paintWaves (g, area);
    paintHarmonics (g, area);
    paintTexture (g, area);
    paintLabels (g, area);
    paintPuck (g, area);
}

void XYPad::paintField (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto cool = juce::Colour::fromFloatRGBA (0.09f, 0.13f, 0.20f, 1.0f);
    const auto deep = juce::Colour::fromFloatRGBA (0.10f, 0.10f, 0.24f, 1.0f);
    const auto warm = juce::Colour::fromFloatRGBA (0.24f, 0.11f, 0.11f, 1.0f);
    const auto ember = juce::Colour::fromFloatRGBA (0.30f, 0.16f, 0.07f, 1.0f);

    const auto bottom = deep.interpolatedWith (cool, valueX);
    const auto top = warm.interpolatedWith (ember, valueX);

    juce::ColourGradient gradient (top, area.getCentreX(), area.getY(), bottom, area.getCentreX(), area.getBottom(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (area, 8.0f);

    g.setColour (juce::Colours::white.withAlpha (0.05f));

    for (int i = 1; i < 4; ++i)
    {
        const auto proportion = (float) i / 4.0f;
        g.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * proportion),
                              area.getX(), area.getRight());
        g.drawVerticalLine (juce::roundToInt (area.getX() + area.getWidth() * proportion),
                            area.getY(), area.getBottom());
    }

    g.setColour (juce::Colours::white.withAlpha (focused ? 0.28f : 0.10f));
    g.drawRoundedRectangle (area, 8.0f, 1.0f);
}

void XYPad::paintWaves (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto energy = xyb::meterDisplay (smoothedLowLevel, -42.0f);

    if (energy < 0.01f)
        return;

    const auto generated = xyb::meterDisplay (smoothedSubLevel, -48.0f);
    const auto cycles = juce::jlimit (0.7f, 6.0f, smoothedFundamental / 22.0f);
    const auto amplitude = area.getHeight() * 0.14f * energy;

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area.toNearestInt());

    for (int layer = 0; layer < 3; ++layer)
    {
        juce::Path path;
        const auto offset = (float) layer * 0.22f;
        const auto scale = 1.0f - (float) layer * 0.28f;
        const auto centre = area.getCentreY() + (float) (layer - 1) * area.getHeight() * 0.16f;

        for (int i = 0; i <= 96; ++i)
        {
            const auto proportion = (float) i / 96.0f;
            const auto angle = juce::MathConstants<float>::twoPi * (proportion * cycles + wavePhase + offset);
            const auto y = centre + std::sin (angle) * amplitude * scale;
            const auto x = area.getX() + proportion * area.getWidth();

            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        const auto alpha = (0.30f - (float) layer * 0.08f) * (0.45f + 0.55f * generated);
        g.setColour (juce::Colour::fromFloatRGBA (0.55f, 0.72f, 1.0f, alpha));
        g.strokePath (path, juce::PathStrokeType (1.6f - (float) layer * 0.35f));
    }
}

void XYPad::paintHarmonics (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto level = xyb::meterDisplay (smoothedHarmonicLevel, -54.0f);

    if (level < 0.01f || smoothedFundamental < 20.0f)
        return;

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area.toNearestInt());

    for (size_t index = 0; index < smoothedWeights.size(); ++index)
    {
        const auto order = (float) (index + 2);
        const auto frequency = smoothedFundamental * order;
        const auto position = frequencyToPosition (frequency);
        const auto x = area.getX() + position * area.getWidth();
        const auto weight = juce::jlimit (0.0f, 1.0f, smoothedWeights[index]);
        const auto alpha = weight * level * 0.55f;

        if (alpha < 0.01f)
            continue;

        juce::ColourGradient gradient (juce::Colour::fromFloatRGBA (1.0f, 0.78f, 0.42f, alpha),
                                       x, area.getBottom(),
                                       juce::Colour::fromFloatRGBA (1.0f, 0.78f, 0.42f, 0.0f),
                                       x, area.getY(), false);
        g.setGradientFill (gradient);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, area.getY(), 2.0f, area.getHeight()));
    }
}

void XYPad::paintTexture (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto drive = juce::jlimit (0.0f, 1.0f, smoothedDrive);

    if (drive < 0.02f)
        return;

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area.toNearestInt());

    juce::Path strokes;

    for (const auto& point : texture)
    {
        const auto verticalWeight = point.y * point.y;

        if (verticalWeight * drive < 0.05f)
            continue;

        const auto x = area.getX() + point.x * area.getWidth();
        const auto y = area.getBottom() - point.y * area.getHeight();
        const auto length = point.length * (0.4f + 0.6f * drive);

        strokes.startNewSubPath (x, y);
        strokes.lineTo (x + std::cos (point.angle) * length, y + std::sin (point.angle) * length);
    }

    g.setColour (juce::Colour::fromFloatRGBA (1.0f, 0.62f, 0.36f, 0.16f * drive));
    g.strokePath (strokes, juce::PathStrokeType (1.0f));
}

void XYPad::paintLabels (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setFont (juce::FontOptions (10.5f).withStyle ("Bold"));
    g.setColour (juce::Colours::white.withAlpha (0.34f));

    const auto inset = 12.0f;

    g.drawText ("DIRTY", juce::Rectangle<float> (area.getX(), area.getY() + inset, area.getWidth(), 14.0f),
                juce::Justification::centred);
    g.drawText ("CLEAN", juce::Rectangle<float> (area.getX(), area.getBottom() - inset - 14.0f, area.getWidth(), 14.0f),
                juce::Justification::centred);
    g.drawText ("SUB", juce::Rectangle<float> (area.getX() + inset, area.getCentreY() - 7.0f, 100.0f, 14.0f),
                juce::Justification::centredLeft);
    g.drawText ("TRANSLATE", juce::Rectangle<float> (area.getRight() - inset - 100.0f, area.getCentreY() - 7.0f, 100.0f, 14.0f),
                juce::Justification::centredRight);
}

void XYPad::paintPuck (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto centre = positionToPoint (valueX, valueY);
    const auto level = xyb::meterDisplay (smoothedOutput, -42.0f);

    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawHorizontalLine (juce::roundToInt (centre.y), area.getX(), area.getRight());
    g.drawVerticalLine (juce::roundToInt (centre.x), area.getY(), area.getBottom());

    const auto glowRadius = 14.0f + 26.0f * level;
    juce::ColourGradient glow (juce::Colour::fromFloatRGBA (1.0f, 0.86f, 0.62f, 0.30f + 0.25f * level),
                               centre.x, centre.y,
                               juce::Colour::fromFloatRGBA (1.0f, 0.86f, 0.62f, 0.0f),
                               centre.x + glowRadius, centre.y, true);
    g.setGradientFill (glow);
    g.fillEllipse (juce::Rectangle<float> (glowRadius * 2.0f, glowRadius * 2.0f).withCentre (centre));

    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (centre));

    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (centre), 1.0f);
}

bool XYPad::keyPressed (const juce::KeyPress& key)
{
    const auto step = key.getModifiers().isShiftDown() ? 0.01f : 0.05f;

    if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::rightKey))
    {
        const auto delta = key.isKeyCode (juce::KeyPress::rightKey) ? step : -step;
        attachmentX.setValueAsCompleteGesture (juce::jlimit (0.0f, 1.0f, valueX + delta));
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::downKey))
    {
        const auto delta = key.isKeyCode (juce::KeyPress::upKey) ? step : -step;
        attachmentY.setValueAsCompleteGesture (juce::jlimit (0.0f, 1.0f, valueY + delta));
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::homeKey))
    {
        attachmentX.setValueAsCompleteGesture (0.5f);
        attachmentY.setValueAsCompleteGesture (0.5f);
        return true;
    }

    return false;
}

void XYPad::focusGained (FocusChangeType)
{
    focused = true;
    repaint();
}

void XYPad::focusLost (FocusChangeType)
{
    focused = false;
    repaint();
}

void XYPad::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    if (event.mods.isPopupMenu())
    {
        if (onContextMenu != nullptr)
            onContextMenu();

        return;
    }

    dragging = true;
    dragAnchor = event.position;
    dragOrigin = { valueX, valueY };

    attachmentX.beginGesture();
    attachmentY.beginGesture();

    if (! event.mods.isShiftDown())
        updateFromMouse (event);
}

void XYPad::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragging)
        return;

    updateFromMouse (event);
}

void XYPad::mouseUp (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);

    if (! dragging)
        return;

    dragging = false;
    attachmentX.endGesture();
    attachmentY.endGesture();
}

void XYPad::mouseDoubleClick (const juce::MouseEvent&)
{
    attachmentX.setValueAsCompleteGesture (0.5f);
    attachmentY.setValueAsCompleteGesture (0.5f);
}

void XYPad::updateFromMouse (const juce::MouseEvent& event)
{
    const auto area = getPadBounds();

    if (area.getWidth() <= 0.0f || area.getHeight() <= 0.0f)
        return;

    if (event.mods.isShiftDown())
    {
        const auto delta = event.position - dragAnchor;
        sendPosition (dragOrigin.x + delta.x / area.getWidth() * 0.25f,
                      dragOrigin.y - delta.y / area.getHeight() * 0.25f);
        return;
    }

    dragAnchor = event.position;
    dragOrigin = { juce::jlimit (0.0f, 1.0f, (event.position.x - area.getX()) / area.getWidth()),
                   juce::jlimit (0.0f, 1.0f, (area.getBottom() - event.position.y) / area.getHeight()) };

    sendPosition (dragOrigin.x, dragOrigin.y);
}

void XYPad::sendPosition (float newX, float newY)
{
    attachmentX.setValueAsPartOfGesture (juce::jlimit (0.0f, 1.0f, newX));
    attachmentY.setValueAsPartOfGesture (juce::jlimit (0.0f, 1.0f, newY));
}
