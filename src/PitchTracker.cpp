#include "PitchTracker.h"

namespace xyb
{

void PitchTracker::prepare (double sampleRate)
{
    decimationFactor = juce::jmax (1, (int) std::round (sampleRate / 4000.0));
    workingRate = sampleRate / decimationFactor;

    juce::dsp::ProcessSpec spec { sampleRate, 512, 1 };

    const auto antiAlias = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate,
                                                                            juce::jmin (1500.0, workingRate * 0.4),
                                                                            0.54);
    const auto antiAliasSecond = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate,
                                                                                  juce::jmin (1500.0, workingRate * 0.4),
                                                                                  1.31);

    decimationFilter[0].coefficients = antiAlias;
    decimationFilter[1].coefficients = antiAliasSecond;
    highPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 20.0, 0.7071);

    for (auto& filter : decimationFilter)
        filter.prepare (spec);

    highPass.prepare (spec);

    minimumLag = juce::jmax (2, (int) std::floor (workingRate / maximumFrequency));
    maximumLag = (int) std::ceil (workingRate / minimumFrequency);

    windowLength = juce::jmax (256, (int) std::round (workingRate * 0.128));
    hopLength = juce::jmax (32, windowLength / 4);

    historySize = juce::nextPowerOfTwo (windowLength + maximumLag + hopLength + 4);

    history.assign ((size_t) historySize, 0.0f);
    window.assign ((size_t) (windowLength + maximumLag + 1), 0.0f);
    difference.assign ((size_t) (maximumLag + 1), 0.0f);
    normalised.assign ((size_t) (maximumLag + 1), 0.0f);

    reset();
}

void PitchTracker::reset()
{
    for (auto& filter : decimationFilter)
        filter.reset();

    highPass.reset();

    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (window.begin(), window.end(), 0.0f);
    std::fill (difference.begin(), difference.end(), 0.0f);
    std::fill (normalised.begin(), normalised.end(), 0.0f);

    writeIndex = 0;
    decimationCounter = 0;
    hopCounter = 0;
    currentLag = 0;
    searching = false;
    runningSum = 0.0f;

    frequency = 0.0f;
    confidence = 0.0f;
    stability = 0.0f;
    smoothedFrequency = 0.0f;
    deviation = 0.0f;
    lastValidFrequency = 0.0f;
    silenceLevel = 0.0f;
}

void PitchTracker::process (const float* mono, int numSamples) noexcept
{
    int decimatedCount = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        auto sample = highPass.processSample (mono[i]);
        sample = decimationFilter[0].processSample (sample);
        sample = decimationFilter[1].processSample (sample);

        if (++decimationCounter >= decimationFactor)
        {
            decimationCounter = 0;
            history[(size_t) writeIndex] = sample;
            writeIndex = (writeIndex + 1) % historySize;
            ++decimatedCount;

            if (++hopCounter >= hopLength)
            {
                hopCounter = 0;
                startFrame();
            }
        }
    }

    if (searching)
    {
        const auto span = juce::jmax (1, maximumLag - minimumLag + 1);
        const auto budget = juce::jmax (4, (int) std::ceil ((float) span * 2.5f * (float) juce::jmax (1, decimatedCount)
                                                            / (float) hopLength));
        advanceSearch (budget);
    }
}

void PitchTracker::startFrame() noexcept
{
    const auto needed = windowLength + maximumLag;
    auto index = (writeIndex + historySize - needed) % historySize;

    float peak = 0.0f;

    for (int i = 0; i < needed; ++i)
    {
        const auto value = history[(size_t) index];
        window[(size_t) i] = value;
        peak = juce::jmax (peak, std::abs (value));
        index = (index + 1) % historySize;
    }

    silenceLevel = peak;
    currentLag = minimumLag;
    runningSum = 0.0f;
    searching = true;
}

void PitchTracker::advanceSearch (int lagBudget) noexcept
{
    const auto* data = window.data();

    while (searching && lagBudget-- > 0)
    {
        float sum = 0.0f;

        for (int i = 0; i < windowLength; ++i)
        {
            const auto delta = data[i] - data[i + currentLag];
            sum += delta * delta;
        }

        difference[(size_t) currentLag] = sum;
        runningSum += sum;
        normalised[(size_t) currentLag] = sum * (float) (currentLag - minimumLag + 1) / juce::jmax (runningSum, kTiny);

        if (++currentLag > maximumLag)
        {
            searching = false;
            finishFrame();
        }
    }
}

float PitchTracker::refineLag (int lagIndex) const noexcept
{
    if (lagIndex <= minimumLag || lagIndex >= maximumLag)
        return (float) lagIndex;

    const auto previous = difference[(size_t) (lagIndex - 1)];
    const auto centre = difference[(size_t) lagIndex];
    const auto next = difference[(size_t) (lagIndex + 1)];
    const auto denominator = 2.0f * (2.0f * centre - previous - next);

    if (std::abs (denominator) < kTiny)
        return (float) lagIndex;

    return (float) lagIndex + (next - previous) / denominator;
}

void PitchTracker::finishFrame() noexcept
{
    constexpr float acceptanceThreshold = 0.16f;

    int bestLag = -1;

    for (int lag = minimumLag + 1; lag < maximumLag; ++lag)
    {
        if (normalised[(size_t) lag] < acceptanceThreshold
            && normalised[(size_t) lag] <= normalised[(size_t) (lag - 1)]
            && normalised[(size_t) lag] <= normalised[(size_t) (lag + 1)])
        {
            bestLag = lag;
            break;
        }
    }

    if (bestLag < 0)
    {
        auto minimumValue = std::numeric_limits<float>::max();

        for (int lag = minimumLag; lag <= maximumLag; ++lag)
        {
            if (normalised[(size_t) lag] < minimumValue)
            {
                minimumValue = normalised[(size_t) lag];
                bestLag = lag;
            }
        }
    }

    if (bestLag < minimumLag)
    {
        confidence *= 0.5f;
        return;
    }

    const auto halfLag = bestLag / 2;

    if (halfLag >= minimumLag && normalised[(size_t) halfLag] < normalised[(size_t) bestLag] * 0.9f
        && normalised[(size_t) halfLag] < acceptanceThreshold)
        bestLag = halfLag;

    const auto refined = refineLag (bestLag);
    const auto candidate = (float) workingRate / juce::jmax (refined, 1.0f);
    const auto clarity = juce::jlimit (0.0f, 1.0f, 1.0f - normalised[(size_t) bestLag]);

    const auto levelGate = juce::jlimit (0.0f, 1.0f, (silenceLevel - 0.0004f) * 400.0f);
    auto frameConfidence = clarity * levelGate;

    if (candidate < minimumFrequency || candidate > maximumFrequency)
        frameConfidence *= 0.25f;

    if (lastValidFrequency > 0.0f)
    {
        const auto ratio = candidate / lastValidFrequency;
        const auto octaveJump = std::abs (std::log2 (juce::jmax (ratio, kTiny)));

        if (octaveJump > 0.35f)
            frameConfidence *= juce::jlimit (0.25f, 1.0f, 1.0f - (octaveJump - 0.35f));
    }

    const auto rise = frameConfidence > confidence ? 0.55f : 0.25f;
    confidence += (frameConfidence - confidence) * rise;

    if (frameConfidence > 0.35f)
    {
        const auto glide = frequency > 0.0f ? 0.5f : 1.0f;
        frequency += (candidate - frequency) * glide;
        lastValidFrequency = frequency;
    }
    else if (frequency > 0.0f)
    {
        frequency += (candidate - frequency) * 0.08f;
    }

    if (smoothedFrequency <= 0.0f)
        smoothedFrequency = frequency;

    smoothedFrequency += (frequency - smoothedFrequency) * 0.15f;

    const auto relativeDeviation = std::abs (frequency - smoothedFrequency) / juce::jmax (smoothedFrequency, 1.0f);
    deviation += (relativeDeviation - deviation) * 0.25f;
    stability = juce::jlimit (0.0f, 1.0f, 1.0f - deviation * 14.0f);
}

} // namespace xyb
