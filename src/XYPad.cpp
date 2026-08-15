#include "XYPad.h"

namespace
{
constexpr float kBezel = 13.0f;
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
              const xyb::EngineMeters& engineMeters,
              const xyui::Theme& themeToUse)
    : meters (engineMeters),
      theme (themeToUse),
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

juce::Rectangle<float> XYPad::getScreenBounds() const
{
    return getLocalBounds().toFloat().reduced (kBezel);
}

juce::Point<float> XYPad::positionToPoint (float x, float y) const
{
    const auto area = getScreenBounds().reduced (10.0f);
    return { area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight() };
}

void XYPad::resized()
{
    juce::Random random (0x23D5Fu);

    texture.clear();
    texture.reserve (260);

    for (int i = 0; i < 260; ++i)
        texture.push_back ({ random.nextFloat(), random.nextFloat(),
                             4.0f + random.nextFloat() * 13.0f,
                             (random.nextFloat() - 0.5f) * 1.5f });

    renderChrome();
}

void XYPad::renderChrome()
{
    const auto bounds = getLocalBounds();

    if (bounds.isEmpty())
        return;

    chromeScale = juce::jlimit (1.0f, 2.0f, juce::Component::getApproximateScaleFactorForComponent (this));

    chrome = juce::Image (juce::Image::ARGB,
                          juce::roundToInt ((float) bounds.getWidth() * chromeScale),
                          juce::roundToInt ((float) bounds.getHeight() * chromeScale), true);

    juce::Graphics g (chrome);
    g.addTransform (juce::AffineTransform::scale (chromeScale));

    xyui::surface::drawRaised (g, bounds.toFloat(), theme, theme.corner + 3.0f);

    const auto screen = getScreenBounds();
    xyui::surface::drawScreen (g, screen, theme, theme.corner);

    juce::Path clip;
    clip.addRoundedRectangle (screen, theme.corner);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (clip);

        for (int i = 1; i < 4; ++i)
        {
            const auto proportion = (float) i / 4.0f;
            const auto lineX = screen.getX() + screen.getWidth() * proportion;
            const auto lineY = screen.getY() + screen.getHeight() * proportion;

            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (lineX, screen.getY(), 1.0f, screen.getHeight());
            g.fillRect (screen.getX(), lineY, screen.getWidth(), 1.0f);

            g.setColour (juce::Colours::white.withAlpha (0.045f));
            g.fillRect (lineX + 1.0f, screen.getY(), 1.0f, screen.getHeight());
            g.fillRect (screen.getX(), lineY + 1.0f, screen.getWidth(), 1.0f);
        }
    }

    for (int i = 0; i <= 8; ++i)
    {
        const auto proportion = (float) i / 8.0f;
        const auto length = i % 2 == 0 ? 5.0f : 3.0f;
        const auto tickX = screen.getX() + screen.getWidth() * proportion;
        const auto tickY = screen.getY() + screen.getHeight() * proportion;

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (tickX, screen.getY() - length - 2.0f, 1.0f, length);
        g.fillRect (tickX, screen.getBottom() + 2.0f, 1.0f, length);
        g.fillRect (screen.getX() - length - 2.0f, tickY, length, 1.0f);
        g.fillRect (screen.getRight() + 2.0f, tickY, length, 1.0f);

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (tickX + 1.0f, screen.getY() - length - 2.0f, 1.0f, length);
        g.fillRect (tickX + 1.0f, screen.getBottom() + 2.0f, 1.0f, length);
        g.fillRect (screen.getX() - length - 2.0f, tickY + 1.0f, length, 1.0f);
        g.fillRect (screen.getRight() + 2.0f, tickY + 1.0f, length, 1.0f);
    }

    g.setFont (juce::FontOptions (9.5f).withStyle ("Bold"));

    const auto inset = 14.0f;
    xyui::surface::drawEngravedText (g, "DIRTY",
                                     { screen.getX(), screen.getY() + inset, screen.getWidth(), 12.0f },
                                     juce::Justification::centred, theme, theme.textDim.withAlpha (0.75f));
    xyui::surface::drawEngravedText (g, "CLEAN",
                                     { screen.getX(), screen.getBottom() - inset - 12.0f, screen.getWidth(), 12.0f },
                                     juce::Justification::centred, theme, theme.textDim.withAlpha (0.75f));
    xyui::surface::drawEngravedText (g, "SUB",
                                     { screen.getX() + inset, screen.getCentreY() - 6.0f, 110.0f, 12.0f },
                                     juce::Justification::centredLeft, theme, theme.textDim.withAlpha (0.75f));
    xyui::surface::drawEngravedText (g, "TRANSLATE",
                                     { screen.getRight() - inset - 110.0f, screen.getCentreY() - 6.0f, 110.0f, 12.0f },
                                     juce::Justification::centredRight, theme, theme.textDim.withAlpha (0.75f));
}

void XYPad::timerCallback()
{
    const auto fundamental = meters.fundamental.load (std::memory_order_relaxed);

    if (fundamental > 20.0f)
        smoothedFundamental = approach (smoothedFundamental, fundamental, 0.25f);

    smoothedLowLevel = approach (smoothedLowLevel, meters.lowEnvelope.load (std::memory_order_relaxed), 0.3f);
    smoothedSubLevel = approach (smoothedSubLevel, meters.subGeneration.load (std::memory_order_relaxed), 0.3f);
    smoothedHarmonicLevel = approach (smoothedHarmonicLevel,
                                      meters.harmonicGeneration.load (std::memory_order_relaxed), 0.3f);
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
    if (chrome.isNull())
        renderChrome();

    g.drawImageTransformed (chrome, juce::AffineTransform::scale (1.0f / chromeScale));

    const auto screen = getScreenBounds();

    juce::Path clip;
    clip.addRoundedRectangle (screen, theme.corner);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (clip);

        const auto puck = positionToPoint (valueX, valueY);
        const auto bloom = juce::jmax (screen.getWidth(), screen.getHeight()) * 0.34f;

        g.setGradientFill ({ theme.accent.withAlpha (0.055f + 0.035f * valueY), puck.x, puck.y,
                             juce::Colours::transparentBlack, puck.x + bloom, puck.y, true });
        g.fillRect (screen);

        paintWaves (g, screen);
        paintHarmonics (g, screen);
        paintTexture (g, screen);
    }

    paintPuck (g, screen);

    xyui::surface::drawGlass (g, screen, theme.corner);

    if (focused)
    {
        g.setColour (theme.accent.withAlpha (0.45f));
        g.drawRoundedRectangle (screen.reduced (1.0f), theme.corner, 1.2f);
    }
}

void XYPad::paintWaves (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto energy = xyb::meterDisplay (smoothedLowLevel, -42.0f);

    if (energy < 0.02f)
        return;

    const auto generated = xyb::meterDisplay (smoothedSubLevel, -48.0f);
    const auto cycles = juce::jlimit (0.7f, 6.0f, smoothedFundamental / 22.0f);
    const auto amplitude = area.getHeight() * 0.15f * energy;

    for (int layer = 0; layer < 3; ++layer)
    {
        juce::Path path;
        const auto offset = (float) layer * 0.22f;
        const auto scale = 1.0f - (float) layer * 0.28f;
        const auto centre = area.getCentreY() + (float) (layer - 1) * area.getHeight() * 0.17f;

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

        const auto alpha = (0.34f - (float) layer * 0.09f) * (0.4f + 0.6f * generated);

        g.setColour (juce::Colour::fromFloatRGBA (0.42f, 0.62f, 0.95f, alpha * 0.35f));
        g.strokePath (path, juce::PathStrokeType (3.4f - (float) layer * 0.6f));

        g.setColour (juce::Colour::fromFloatRGBA (0.62f, 0.80f, 1.0f, alpha));
        g.strokePath (path, juce::PathStrokeType (1.3f - (float) layer * 0.22f));
    }
}

void XYPad::paintHarmonics (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto level = xyb::meterDisplay (smoothedHarmonicLevel, -54.0f);

    if (level < 0.02f || smoothedFundamental < 20.0f)
        return;

    for (size_t index = 0; index < smoothedWeights.size(); ++index)
    {
        const auto order = (float) (index + 2);
        const auto position = frequencyToPosition (smoothedFundamental * order);
        const auto x = area.getX() + position * area.getWidth();
        const auto weight = juce::jlimit (0.0f, 1.0f, smoothedWeights[index]);
        const auto alpha = weight * level * 0.6f;

        if (alpha < 0.01f)
            continue;

        g.setGradientFill ({ theme.accentGlow.withAlpha (alpha), x, area.getBottom(),
                             juce::Colours::transparentBlack, x, area.getY(), false });
        g.fillRect (juce::Rectangle<float> (x - 1.5f, area.getY(), 3.0f, area.getHeight()));

        g.setGradientFill ({ theme.accentGlow.withAlpha (juce::jmin (1.0f, alpha * 1.6f)), x, area.getBottom(),
                             juce::Colours::transparentBlack, x, area.getCentreY(), false });
        g.fillRect (juce::Rectangle<float> (x - 0.5f, area.getY(), 1.0f, area.getHeight()));
    }
}

void XYPad::paintTexture (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto drive = juce::jlimit (0.0f, 1.0f, smoothedDrive);

    if (drive < 0.02f)
        return;

    std::array<juce::Path, 3> buckets;

    for (const auto& point : texture)
    {
        const auto verticalWeight = point.y * point.y * point.y;
        const auto intensity = verticalWeight * drive;

        if (intensity < 0.05f)
            continue;

        const auto x = area.getX() + point.x * area.getWidth();
        const auto y = area.getBottom() - point.y * area.getHeight();
        const auto length = point.length * (0.35f + 0.65f * drive) * (0.4f + 0.6f * verticalWeight);
        const auto bucket = (size_t) juce::jlimit (0, 2, (int) (intensity * 3.0f));

        buckets[bucket].startNewSubPath (x, y);
        buckets[bucket].lineTo (x + std::cos (point.angle) * length * 1.5f,
                                y + std::sin (point.angle) * length * 0.45f);
    }

    for (size_t bucket = 0; bucket < buckets.size(); ++bucket)
    {
        if (buckets[bucket].isEmpty())
            continue;

        const auto alpha = (0.09f + 0.16f * (float) bucket) * drive;
        g.setColour (theme.accentGlow.withAlpha (alpha));
        g.strokePath (buckets[bucket], juce::PathStrokeType (1.0f + 0.25f * (float) bucket));
    }
}

void XYPad::paintPuck (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto centre = positionToPoint (valueX, valueY);
    const auto level = xyb::meterDisplay (smoothedOutput, -42.0f);
    const auto radius = 13.0f;

    {
        juce::Path clip;
        clip.addRoundedRectangle (area, theme.corner);

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (clip);

        g.setColour (juce::Colours::white.withAlpha (0.055f));
        g.drawHorizontalLine (juce::roundToInt (centre.y), area.getX(), area.getRight());
        g.drawVerticalLine (juce::roundToInt (centre.x), area.getY(), area.getBottom());
    }

    const auto body = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    juce::Path shape;
    shape.addEllipse (body);
    juce::DropShadow (juce::Colours::black.withAlpha (0.72f), 12, { 0, 4 }).drawForPath (g, shape);

    g.setGradientFill ({ theme.metalHigh, body.getX() + body.getWidth() * 0.25f, body.getY(),
                         theme.metalLow, body.getRight(), body.getBottom(), false });
    g.fillEllipse (body);

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    const auto dish = body.reduced (radius * 0.3f);
    g.setGradientFill ({ theme.metalLow.darker (0.3f), dish.getCentreX(), dish.getY(),
                         theme.metal, dish.getCentreX(), dish.getBottom(), false });
    g.fillEllipse (dish);

    xyui::surface::drawIndicatorLamp (g, body.reduced (radius * 0.62f), theme, 0.25f + 0.75f * level);

    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawEllipse (body.reduced (1.4f), 1.0f);
}

void XYPad::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        if (onContextMenu != nullptr)
            onContextMenu();

        return;
    }

    grabKeyboardFocus();

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
    if (dragging)
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

void XYPad::updateFromMouse (const juce::MouseEvent& event)
{
    const auto area = getScreenBounds().reduced (10.0f);

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
