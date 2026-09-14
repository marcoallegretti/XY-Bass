#pragma once

#include <juce_dsp/juce_dsp.h>

namespace xyb
{

inline constexpr float kTiny = 1.0e-12f;

inline float timeToCoefficient (float milliseconds, float sampleRate) noexcept
{
    if (milliseconds <= 0.0f)
        return 0.0f;

    return std::exp (-1.0f / (0.001f * milliseconds * sampleRate));
}

class EnvelopeFollower
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (float) newSampleRate;
        updateCoefficients();
        reset();
    }

    void setTimes (float attackMs, float releaseMs) noexcept
    {
        attackTime = attackMs;
        releaseTime = releaseMs;
        updateCoefficients();
    }

    void reset() noexcept { envelope = 0.0f; }

    float process (float input) noexcept
    {
        const auto rectified = std::abs (input);
        const auto coefficient = rectified > envelope ? attackCoeff : releaseCoeff;
        envelope = rectified + coefficient * (envelope - rectified);
        return envelope;
    }

    float getValue() const noexcept { return envelope; }

private:
    void updateCoefficients() noexcept
    {
        attackCoeff = timeToCoefficient (attackTime, sampleRate);
        releaseCoeff = timeToCoefficient (releaseTime, sampleRate);
    }

    float sampleRate = 44100.0f;
    float attackTime = 5.0f;
    float releaseTime = 50.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    float envelope = 0.0f;
};

class SmoothedScalar
{
public:
    void prepare (double newSampleRate, float timeMs) noexcept
    {
        sampleRate = (float) newSampleRate;
        setTime (timeMs);
    }

    void setTime (float timeMs) noexcept
    {
        coefficient = timeToCoefficient (timeMs, sampleRate);
    }

    void setTarget (float newTarget) noexcept { target = newTarget; }
    void snapTo (float value) noexcept { target = current = value; }

    float next() noexcept
    {
        current = target + coefficient * (current - target);
        return current;
    }

    float advance (int steps) noexcept
    {
        const auto blockCoefficient = std::pow (coefficient, (float) juce::jmax (1, steps));
        current = target + blockCoefficient * (current - target);
        return current;
    }

    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }

private:
    float sampleRate = 44100.0f;
    float coefficient = 0.0f;
    float current = 0.0f;
    float target = 0.0f;
};

class QuadratureOscillator
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (float) newSampleRate;
        reset();
    }

    void reset() noexcept
    {
        real = 1.0f;
        imaginary = 0.0f;
    }

    void setFrequency (float frequencyHz) noexcept
    {
        const auto limited = juce::jlimit (0.0f, sampleRate * 0.49f, frequencyHz);
        const auto increment = juce::MathConstants<float>::twoPi * limited / sampleRate;
        cosineStep = std::cos (increment);
        sineStep = std::sin (increment);
    }

    void advance() noexcept
    {
        const auto nextReal = real * cosineStep - imaginary * sineStep;
        const auto nextImaginary = real * sineStep + imaginary * cosineStep;
        const auto correction = 1.5f - 0.5f * (nextReal * nextReal + nextImaginary * nextImaginary);

        real = nextReal * correction;
        imaginary = nextImaginary * correction;
    }

    void nudge (float radians) noexcept
    {
        const auto nextReal = real - radians * imaginary;
        imaginary = imaginary + radians * real;
        real = nextReal;
    }

    float sine() const noexcept { return imaginary; }
    float cosine() const noexcept { return real; }

private:
    float sampleRate = 44100.0f;
    float cosineStep = 1.0f;
    float sineStep = 0.0f;
    float real = 1.0f;
    float imaginary = 0.0f;
};

class DcBlocker
{
public:
    void prepare (double sampleRate, float cutoffHz = 5.0f) noexcept
    {
        pole = 1.0f - juce::MathConstants<float>::twoPi * cutoffHz / (float) sampleRate;
        pole = juce::jlimit (0.9f, 0.99999f, pole);
        reset();
    }

    void reset() noexcept { lastInput = lastOutput = 0.0f; }

    float process (float input) noexcept
    {
        const auto output = input - lastInput + pole * lastOutput;
        lastInput = input;
        lastOutput = output;
        return lastOutput;
    }

private:
    float pole = 0.999f;
    float lastInput = 0.0f;
    float lastOutput = 0.0f;
};

class TptSvf
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (float) newSampleRate;
        setCutoff (cutoff);
        reset();
    }

    void setCutoff (float frequencyHz) noexcept
    {
        cutoff = juce::jlimit (5.0f, sampleRate * 0.45f, frequencyHz);
        g = std::tan (juce::MathConstants<float>::pi * cutoff / sampleRate);
        updateDenominator();
    }

    void setQ (float newQ) noexcept
    {
        q = juce::jmax (0.05f, newQ);
        twoR = 1.0f / q;
        updateDenominator();
    }

    void reset() noexcept { s1 = s2 = 0.0f; }

    void process (float input, float& lowOut, float& bandOut, float& highOut) noexcept
    {
        highOut = denominator * (input - (twoR + g) * s1 - s2);
        bandOut = g * highOut + s1;
        s1 = g * highOut + bandOut;
        lowOut = g * bandOut + s2;
        s2 = g * bandOut + lowOut;
    }

    float processBandPass (float input) noexcept
    {
        float low = 0.0f, band = 0.0f, high = 0.0f;
        process (input, low, band, high);
        return band * twoR;
    }

    float processLowPass (float input) noexcept
    {
        float low = 0.0f, band = 0.0f, high = 0.0f;
        process (input, low, band, high);
        return low;
    }

    float processHighPass (float input) noexcept
    {
        float low = 0.0f, band = 0.0f, high = 0.0f;
        process (input, low, band, high);
        return high;
    }

    float getCutoff() const noexcept { return cutoff; }
    float getNormalisedBandwidth() const noexcept { return twoR; }

private:
    void updateDenominator() noexcept
    {
        denominator = 1.0f / (1.0f + twoR * g + g * g);
    }

    float sampleRate = 44100.0f;
    float cutoff = 1000.0f;
    float q = 0.7071f;
    float twoR = 1.4142f;
    float g = 0.1f;
    float denominator = 1.0f;
    float s1 = 0.0f;
    float s2 = 0.0f;
};

class Biquad
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (float) newSampleRate;
        reset();
    }

    void reset() noexcept { z1 = z2 = 0.0f; }

    void setBypass() noexcept
    {
        b0 = 1.0f;
        b1 = b2 = a1 = a2 = 0.0f;
    }

    void setPeaking (float frequency, float q, float gainDb) noexcept
    {
        const auto a = std::pow (10.0f, gainDb * 0.025f);
        const auto w0 = angular (frequency);
        const auto cosine = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0f * juce::jmax (0.05f, q));

        normalise (1.0f + alpha * a, -2.0f * cosine, 1.0f - alpha * a,
                   1.0f + alpha / a, -2.0f * cosine, 1.0f - alpha / a);
    }

    void setHighShelf (float frequency, float q, float gainDb) noexcept
    {
        const auto a = std::pow (10.0f, gainDb * 0.025f);
        const auto w0 = angular (frequency);
        const auto cosine = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0f * juce::jmax (0.05f, q));
        const auto root = 2.0f * std::sqrt (a) * alpha;

        normalise (a * ((a + 1.0f) + (a - 1.0f) * cosine + root),
                   -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine),
                   a * ((a + 1.0f) + (a - 1.0f) * cosine - root),
                   (a + 1.0f) - (a - 1.0f) * cosine + root,
                   2.0f * ((a - 1.0f) - (a + 1.0f) * cosine),
                   (a + 1.0f) - (a - 1.0f) * cosine - root);
    }

    void setHighPass (float frequency, float q) noexcept
    {
        const auto w0 = angular (frequency);
        const auto cosine = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0f * juce::jmax (0.05f, q));

        normalise ((1.0f + cosine) * 0.5f, -(1.0f + cosine), (1.0f + cosine) * 0.5f,
                   1.0f + alpha, -2.0f * cosine, 1.0f - alpha);
    }

    float process (float input) noexcept
    {
        const auto output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }

private:
    float angular (float frequency) const noexcept
    {
        const auto limited = juce::jlimit (5.0f, sampleRate * 0.48f, frequency);
        return juce::MathConstants<float>::twoPi * limited / sampleRate;
    }

    void normalise (float nb0, float nb1, float nb2, float na0, float na1, float na2) noexcept
    {
        const auto inverse = 1.0f / na0;
        b0 = nb0 * inverse;
        b1 = nb1 * inverse;
        b2 = nb2 * inverse;
        a1 = na1 * inverse;
        a2 = na2 * inverse;
    }

    float sampleRate = 44100.0f;
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

class DelayBuffer
{
public:
    void prepare (int numChannels, int maxDelaySamples)
    {
        capacity = juce::jmax (2, maxDelaySamples + 1);
        buffer.setSize (numChannels, capacity);
        reset();
    }

    void reset() noexcept
    {
        buffer.clear();
        writeIndex = 0;
    }

    void setDelay (int samples) noexcept
    {
        delay = juce::jlimit (0, capacity - 1, samples);
    }

    void process (juce::AudioBuffer<float>& target, int samplesToProcess) noexcept
    {
        const auto numChannels = juce::jmin (target.getNumChannels(), buffer.getNumChannels());
        const auto numSamples = juce::jlimit (0, target.getNumSamples(), samplesToProcess);

        if (delay == 0 || numSamples == 0)
            return;

        auto index = writeIndex;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = target.getWritePointer (channel);
            auto* line = buffer.getWritePointer (channel);
            index = writeIndex;

            auto readIndex = index - delay;

            if (readIndex < 0)
                readIndex += capacity;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto delayed = line[readIndex];
                line[index] = data[i];
                data[i] = delayed;

                if (++index == capacity)
                    index = 0;

                if (++readIndex == capacity)
                    readIndex = 0;
            }
        }

        writeIndex = index;
    }

private:
    juce::AudioBuffer<float> buffer;
    int capacity = 2;
    int delay = 0;
    int writeIndex = 0;
};

} // namespace xyb
