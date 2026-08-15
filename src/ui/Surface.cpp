#include "Surface.h"

namespace xyui::surface
{

void drawInnerShadow (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                      juce::Colour colour, float radius, juce::Point<int> offset)
{
    juce::Path opening;
    opening.addRoundedRectangle (bounds, corner);

    juce::Path surround;
    surround.setUsingNonZeroWinding (false);
    surround.addRectangle (bounds.expanded (radius * 4.0f));
    surround.addPath (opening);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (opening);
    juce::DropShadow (colour, juce::roundToInt (radius), offset).drawForPath (g, surround);
}

void drawBrushedFace (juce::Graphics& g, juce::Rectangle<float> bounds, const Theme& theme, float corner)
{
    juce::ColourGradient body (theme.plateHigh, bounds.getCentreX(), bounds.getY(),
                               theme.plateLow, bounds.getCentreX(), bounds.getBottom(), false);
    body.addColour (0.42, theme.plate);
    g.setGradientFill (body);
    g.fillRoundedRectangle (bounds, corner);

    juce::Path clip;
    clip.addRoundedRectangle (bounds, corner);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (clip);

    juce::Random random (0x5eed17);

    for (float y = bounds.getY(); y < bounds.getBottom(); y += 2.0f)
    {
        const auto alpha = 0.012f + random.nextFloat() * 0.026f;
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.fillRect (bounds.getX(), y, bounds.getWidth(), 1.0f);
        g.setColour (juce::Colours::black.withAlpha (alpha * 0.8f));
        g.fillRect (bounds.getX(), y + 1.0f, bounds.getWidth(), 1.0f);
    }

    juce::ColourGradient vignette (juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getCentreY(),
                                   juce::Colours::black.withAlpha (0.34f), bounds.getX(), bounds.getBottom(), true);
    g.setGradientFill (vignette);
    g.fillRect (bounds);
}

void drawRaised (juce::Graphics& g, juce::Rectangle<float> bounds, const Theme& theme, float corner,
                 float pressed)
{
    const auto lift = 1.0f - juce::jlimit (0.0f, 1.0f, pressed);

    juce::Path shape;
    shape.addRoundedRectangle (bounds, corner);

    if (lift > 0.05f)
    {
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f * lift),
                          juce::roundToInt (theme.relief * lift), { 0, juce::roundToInt (3.0f * lift) })
            .drawForPath (g, shape);
    }

    juce::ColourGradient face (theme.plateHigh.brighter (0.05f * lift), bounds.getCentreX(), bounds.getY(),
                               theme.plateLow, bounds.getCentreX(), bounds.getBottom(), false);

    if (pressed > 0.05f)
        face = juce::ColourGradient (theme.plateLow, bounds.getCentreX(), bounds.getY(),
                                     theme.plate, bounds.getCentreX(), bounds.getBottom(), false);

    g.setGradientFill (face);
    g.fillRoundedRectangle (bounds, corner);

    if (pressed > 0.05f)
        drawInnerShadow (g, bounds, corner, juce::Colours::black.withAlpha (0.6f), 5.0f, { 0, 2 });

    g.setColour (juce::Colours::white.withAlpha (0.11f * lift));
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, theme.bevel);

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawRoundedRectangle (bounds.reduced (-0.2f), corner + 0.4f, 1.0f);
}

void drawRecess (juce::Graphics& g, juce::Rectangle<float> bounds, const Theme& theme, float corner,
                 juce::Colour fill)
{
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);

    drawInnerShadow (g, bounds, corner, juce::Colours::black.withAlpha (0.85f), 8.0f, { 0, 3 });

    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawRoundedRectangle (bounds.reduced (0.5f).translated (0.0f, 1.0f), corner, 1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

    juce::ignoreUnused (theme);
}

void drawScreen (juce::Graphics& g, juce::Rectangle<float> bounds, const Theme& theme, float corner)
{
    juce::ColourGradient well (theme.screen.darker (0.25f), bounds.getCentreX(), bounds.getY(),
                               theme.screen, bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill (well);
    g.fillRoundedRectangle (bounds, corner);

    drawInnerShadow (g, bounds, corner, juce::Colours::black.withAlpha (0.9f), 11.0f, { 0, 4 });
}

void drawGlass (juce::Graphics& g, juce::Rectangle<float> bounds, float corner)
{
    juce::Path clip;
    clip.addRoundedRectangle (bounds, corner);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (clip);

    const auto band = bounds.withHeight (bounds.getHeight() * 0.16f);

    g.setGradientFill ({ juce::Colours::white.withAlpha (0.030f), band.getCentreX(), band.getY(),
                         juce::Colours::transparentWhite, band.getCentreX(), band.getBottom(), false });
    g.fillRect (band);

    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), corner, 1.0f);
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, const Theme& theme, float angle)
{
    const auto bounds = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    juce::Path shape;
    shape.addEllipse (bounds);
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 3, { 0, 1 }).drawForPath (g, shape);

    g.setGradientFill ({ theme.metal.brighter (0.25f), bounds.getX(), bounds.getY(),
                         theme.metalLow, bounds.getRight(), bounds.getBottom(), false });
    g.fillEllipse (bounds);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (bounds.reduced (0.4f), 1.0f);

    juce::Path slot;
    slot.addRoundedRectangle (centre.x - radius * 0.62f, centre.y - radius * 0.13f,
                              radius * 1.24f, radius * 0.26f, radius * 0.1f);
    slot.applyTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));

    g.setColour (juce::Colours::black.withAlpha (0.62f));
    g.fillPath (slot);

    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillPath (slot, juce::AffineTransform::translation (0.0f, 0.7f));
}

void drawEngravedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> bounds,
                       juce::Justification justification, const Theme& theme, juce::Colour tint)
{
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawText (text, bounds.translated (0.0f, 1.0f), justification);

    g.setColour (tint.withMultipliedAlpha (0.95f));
    g.drawText (text, bounds, justification);

    juce::ignoreUnused (theme);
}

void drawKnobBody (juce::Graphics& g, juce::Rectangle<float> bounds, const Theme& theme, float angle,
                   float hover)
{
    const auto centre = bounds.getCentre();
    const auto radius = bounds.getWidth() * 0.5f;

    juce::Path shape;
    shape.addEllipse (bounds);
    juce::DropShadow (juce::Colours::black.withAlpha (0.62f), juce::roundToInt (radius * 0.36f),
                      { 0, juce::roundToInt (radius * 0.16f) })
        .drawForPath (g, shape);

    g.setGradientFill ({ theme.metalLow.darker (0.4f), centre.x, bounds.getY(),
                         theme.metalLow, centre.x, bounds.getBottom(), false });
    g.fillEllipse (bounds);

    const auto cap = bounds.reduced (radius * 0.13f);

    g.setGradientFill ({ theme.metalHigh.brighter (0.1f + hover * 0.15f), cap.getX() + cap.getWidth() * 0.2f,
                         cap.getY(), theme.metalLow.brighter (0.05f), cap.getRight(), cap.getBottom(), false });
    g.fillEllipse (cap);

    const auto knurlOuter = radius * 0.99f;
    const auto knurlInner = radius * 0.88f;

    for (int i = 0; i < 48; ++i)
    {
        const auto tick = juce::MathConstants<float>::twoPi * (float) i / 48.0f;
        const auto shade = 0.5f + 0.5f * std::cos (tick + 2.4f);

        g.setColour (juce::Colours::black.withAlpha (0.30f * shade + 0.06f));
        g.drawLine (centre.x + std::sin (tick) * knurlInner, centre.y - std::cos (tick) * knurlInner,
                    centre.x + std::sin (tick) * knurlOuter, centre.y - std::cos (tick) * knurlOuter, 1.4f);
    }

    const auto dish = cap.reduced (radius * 0.16f);

    g.setGradientFill ({ theme.metalLow.brighter (0.18f), dish.getCentreX(), dish.getY(),
                         theme.metalLow.darker (0.25f), dish.getCentreX(), dish.getBottom(), false });
    g.fillEllipse (dish);

    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (dish.reduced (0.5f), 1.0f);

    juce::Path pointer;
    const auto pointerLength = radius * 0.70f;
    pointer.addRoundedRectangle (-radius * 0.055f, -pointerLength, radius * 0.11f, pointerLength * 0.66f,
                                 radius * 0.055f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillPath (pointer, juce::AffineTransform::translation (0.0f, 1.2f));

    g.setColour (theme.text.brighter (0.35f));
    g.fillPath (pointer);

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (bounds.reduced (0.6f), 1.2f);
}

void drawIndicatorLamp (juce::Graphics& g, juce::Rectangle<float> bounds, const Theme& theme, float brightness)
{
    const auto lit = juce::jlimit (0.0f, 1.0f, brightness);

    g.setColour (theme.recessDeep);
    g.fillEllipse (bounds);

    if (lit > 0.01f)
    {
        const auto glow = bounds.expanded (bounds.getWidth() * 1.1f * lit);
        g.setGradientFill ({ theme.accentGlow.withAlpha (0.42f * lit), bounds.getCentreX(), bounds.getCentreY(),
                             juce::Colours::transparentBlack, glow.getRight(), bounds.getCentreY(), true });
        g.fillEllipse (glow);
    }

    g.setColour (theme.accent.withAlpha (0.22f + 0.78f * lit));
    g.fillEllipse (bounds.reduced (bounds.getWidth() * 0.18f));

    g.setColour (juce::Colours::white.withAlpha (0.35f * lit));
    g.fillEllipse (bounds.reduced (bounds.getWidth() * 0.34f).translated (0.0f, -bounds.getHeight() * 0.08f));

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (bounds.reduced (0.4f), 1.0f);
}

juce::Image renderChassis (juce::Rectangle<int> bounds, const Theme& theme, float scale)
{
    juce::Image image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (bounds.getWidth() * scale)),
                       juce::jmax (1, juce::roundToInt (bounds.getHeight() * scale)), true);

    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (scale));

    drawBrushedFace (g, bounds.toFloat(), theme, 0.0f);

    return image;
}

} // namespace xyui::surface
