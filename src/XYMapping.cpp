#include "XYMapping.h"

namespace xyb
{

static float shapeAxis (float value, float exponent) noexcept
{
    return std::pow (juce::jlimit (0.0f, 1.0f, value), exponent);
}

EngineTargets computeTargets (float x, float y, const SourceFeatures& features)
{
    EngineTargets targets;

    x = juce::jlimit (0.0f, 1.0f, x);
    y = juce::jlimit (0.0f, 1.0f, y);

    const auto subSide = shapeAxis (1.0f - x, 1.55f);
    const auto translateSide = shapeAxis (x, 1.55f);
    const auto dirt = shapeAxis (y, 1.7f);

    const auto restraint = 1.0f - 0.42f * features.mixLikeness;
    const auto tonal = features.tonal;
    const auto weakFundamental = juce::jlimit (0.0f, 1.0f, (0.62f - features.fundamentalStrength) * 2.6f);

    const auto pitch = features.fundamental > 0.0f ? features.fundamental : 55.0f;
    const auto trackedPitch = juce::jlimit (25.0f, 300.0f, pitch);
    const auto pitchTrust = juce::jlimit (0.0f, 1.0f, features.pitchConfidence * 1.25f);

    targets.subReinforcement = subSide * restraint * (0.55f + 0.45f * features.fundamentalStrength);
    targets.subCentre = juce::jlimit (32.0f, 95.0f,
                                      juce::jmap (pitchTrust, 58.0f, juce::jlimit (32.0f, 95.0f, trackedPitch))
                                          + 12.0f * features.percussive);

    targets.subReconstruction = subSide * weakFundamental * tonal * (1.0f - features.mixLikeness)
                                * juce::jlimit (0.0f, 1.0f, (trackedPitch - 28.0f) * 0.02f);

    const auto subharmonicWindow = juce::jlimit (0.0f, 1.0f, (trackedPitch - 58.0f) * 0.05f)
                                   * juce::jlimit (0.0f, 1.0f, (185.0f - trackedPitch) * 0.04f);
    const auto subharmonicGate = juce::jlimit (0.0f, 1.0f, (features.pitchConfidence - 0.82f) * 5.5f)
                                 * juce::jlimit (0.0f, 1.0f, (features.pitchStability - 0.75f) * 4.0f)
                                 * (1.0f - features.mixLikeness) * (1.0f - features.percussive);
    targets.subharmonic = subSide * subSide * subharmonicWindow * subharmonicGate * 0.55f;

    targets.harmonicAmount = translateSide * restraint * (1.0f - 0.35f * features.existingSaturation);
    targets.harmonicTilt = juce::jlimit (0.0f, 1.0f, 0.18f + 0.62f * translateSide + 0.28f * dirt);
    targets.harmonicBrightness = juce::jlimit (0.0f, 1.0f, 0.25f + 0.5f * translateSide + 0.35f * dirt
                                                               - 0.3f * features.mixLikeness);
    targets.harmonicSpread = juce::jlimit (0.0f, 1.0f, 0.25f + 0.45f * translateSide);

    targets.drive = dirt * restraint * (1.0f - 0.3f * features.existingSaturation);
    targets.asymmetry = 0.18f * dirt * (0.45f + 0.55f * subSide);
    targets.clipping = juce::jlimit (0.0f, 1.0f, (y - 0.62f) * 2.63f) * restraint;
    targets.fundamentalProtection = juce::jlimit (0.0f, 0.8f, translateSide * (0.35f + 0.45f * dirt));
    targets.transientDepth = juce::jlimit (0.0f, 0.90f, (0.92f - 0.10f * dirt)
                                                            * (0.35f + 0.65f * features.percussive));

    targets.lowCompression = juce::jlimit (0.0f, 1.0f, 0.18f + 0.45f * dirt + 0.3f * subSide
                                                          + 0.25f * targets.subReinforcement);
    targets.mudControl = juce::jlimit (0.0f, 1.0f, 0.65f * targets.subReinforcement + 0.3f * dirt
                                                       + 0.4f * features.mixLikeness * targets.harmonicAmount);
    targets.fizzControl = juce::jlimit (0.0f, 1.0f, 0.75f * translateSide * (0.35f + 0.65f * dirt)
                                                       + 0.35f * features.mixLikeness);

    targets.monoAmount = juce::jlimit (0.0f, 1.0f, 0.30f + 0.30f * subSide
                                                       + 0.45f * juce::jlimit (0.0f, 1.0f, 0.6f - features.correlation));

    targets.bassCrossover = juce::jlimit (135.0f, 260.0f, juce::jmap (pitchTrust, 165.0f, trackedPitch * 2.2f));
    targets.subsonicCutoff = juce::jlimit (14.0f, 26.0f, 14.0f + 11.0f * translateSide);

    return targets;
}

} // namespace xyb
