#pragma once

#include "SourceAnalyser.h"

namespace xyb
{

struct EngineTargets
{
    float subReinforcement = 0.0f;
    float subCentre = 55.0f;
    float subReconstruction = 0.0f;
    float subharmonic = 0.0f;

    float harmonicAmount = 0.0f;
    float harmonicTilt = 0.0f;
    float harmonicBrightness = 0.0f;
    float harmonicSpread = 0.0f;

    float drive = 0.0f;
    float asymmetry = 0.0f;
    float clipping = 0.0f;
    float fundamentalProtection = 0.0f;
    float transientDepth = 0.0f;

    float lowCompression = 0.0f;
    float mudControl = 0.0f;
    float fizzControl = 0.0f;

    float monoBoundary = 110.0f;
    float monoAmount = 0.0f;

    float bassCrossover = 150.0f;
    float subsonicCutoff = 16.0f;
};

EngineTargets computeTargets (float x, float y, const SourceFeatures& features);

} // namespace xyb
