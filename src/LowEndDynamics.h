#pragma once

#include "DspUtility.h"

namespace xyb
{

class LowEndDynamics
{
public:
    void prepare (double sampleRate)
    {
        detector.prepare (sampleRate);
        detector.setTimes (12.0f, 90.0f);

        reference.prepare (sampleRate, 1400.0f);
        amountSmoother.prepare (sampleRate, 90.0f);

        attackCoeff = timeToCoefficient (20.0f, (float) sampleRate);
        releaseCoeff = timeToCoefficient (110.0f, (float) sampleRate);
        fastReleaseCoeff = timeToCoefficient (45.0f, (float) sampleRate);

        reset();
    }

    void reset()
    {
        detector.reset();
        reference.snapTo (0.0f);
        amountSmoother.snapTo (0.0f);
        reductionDb = 0.0f;
    }

    void setControls (float amount, float percussive) noexcept
    {
        amountSmoother.setTarget (juce::jlimit (0.0f, 1.0f, amount));
        percussiveBlend = juce::jlimit (0.0f, 1.0f, percussive);
    }

    float process (float sidechain) noexcept
    {
        const auto amount = amountSmoother.next();
        const auto level = detector.process (sidechain);

        reference.setTarget (level);
        const auto average = reference.next();

        const auto ratio = 1.0f + amount * 2.6f;
        const auto thresholdDb = juce::Decibels::gainToDecibels (juce::jmax (average, 2.0e-4f)) + 3.5f;
        const auto levelDb = juce::Decibels::gainToDecibels (juce::jmax (level, 1.0e-6f));
        const auto over = levelDb - thresholdDb;

        constexpr float knee = 9.0f;
        const auto slope = 1.0f - 1.0f / ratio;

        float targetReduction = 0.0f;

        if (over >= knee * 0.5f)
            targetReduction = over * slope;
        else if (over > -knee * 0.5f)
        {
            const auto kneeOffset = over + knee * 0.5f;
            targetReduction = slope * kneeOffset * kneeOffset / (2.0f * knee);
        }

        targetReduction = juce::jlimit (0.0f, 9.0f, targetReduction);

        const auto release = juce::jmap (percussiveBlend, releaseCoeff, fastReleaseCoeff);
        const auto coefficient = targetReduction > reductionDb ? attackCoeff : release;
        reductionDb = flushDenormal (targetReduction + coefficient * (reductionDb - targetReduction));

        return juce::Decibels::decibelsToGain (-reductionDb);
    }

    float getReductionDb() const noexcept { return reductionDb; }

private:
    EnvelopeFollower detector;
    SmoothedScalar reference, amountSmoother;

    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    float fastReleaseCoeff = 0.0f;
    float reductionDb = 0.0f;
    float percussiveBlend = 0.0f;
};

} // namespace xyb
