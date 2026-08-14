#pragma once

#include "BassEngine.h"

namespace xyb::testing
{

using Buffer = juce::AudioBuffer<float>;

inline Buffer makeBuffer (int numChannels, int numSamples)
{
    Buffer buffer (numChannels, numSamples);
    buffer.clear();
    return buffer;
}

inline void addSine (Buffer& buffer, double frequency, double sampleRate, float amplitude, double phaseOffset = 0.0)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] += amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / sampleRate
                                                     + phaseOffset);
    }
}

inline void fillSine (Buffer& buffer, double frequency, double sampleRate, float amplitude)
{
    buffer.clear();
    addSine (buffer, frequency, sampleRate, amplitude);
}

inline void fillSweep (Buffer& buffer, double startHz, double endHz, double sampleRate, float amplitude)
{
    const auto length = (double) buffer.getNumSamples();
    double phase = 0.0;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto proportion = (double) i / length;
        const auto frequency = startHz * std::pow (endHz / startHz, proportion);
        phase += juce::MathConstants<double>::twoPi * frequency / sampleRate;
        const auto value = amplitude * (float) std::sin (phase);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (channel, i, value);
    }
}

inline void fillSaw (Buffer& buffer, double frequency, double sampleRate, float amplitude, int partials = 24)
{
    buffer.clear();

    for (int partial = 1; partial <= partials; ++partial)
    {
        const auto harmonic = frequency * partial;

        if (harmonic > sampleRate * 0.45)
            break;

        addSine (buffer, harmonic, sampleRate, amplitude / (float) partial);
    }
}

inline void fillSquare (Buffer& buffer, double frequency, double sampleRate, float amplitude, int partials = 24)
{
    buffer.clear();

    for (int partial = 1; partial <= partials; partial += 2)
    {
        const auto harmonic = frequency * partial;

        if (harmonic > sampleRate * 0.45)
            break;

        addSine (buffer, harmonic, sampleRate, amplitude / (float) partial);
    }
}

inline void fillSaturatedSine (Buffer& buffer, double frequency, double sampleRate, float amplitude, float drive)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto value = std::sin (juce::MathConstants<double>::twoPi * frequency * i / sampleRate);
            data[i] = amplitude * std::tanh (drive * (float) value) / std::tanh (drive);
        }
    }
}

inline void fillMissingFundamental (Buffer& buffer, double frequency, double sampleRate, float amplitude)
{
    buffer.clear();

    for (int partial = 2; partial <= 6; ++partial)
        addSine (buffer, frequency * partial, sampleRate, amplitude / (float) partial);
}

inline void fillEightOhEight (Buffer& buffer, double frequency, double sampleRate, float amplitude, double noteSeconds)
{
    const auto noteSamples = juce::jmax (1, (int) (noteSeconds * sampleRate));
    double phase = 0.0;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto position = i % noteSamples;
        const auto seconds = (double) position / sampleRate;
        const auto envelope = std::exp (-seconds * 2.4) * juce::jmin (1.0, seconds * 900.0);
        const auto pitchDrop = 1.0 + 0.55 * std::exp (-seconds * 45.0);

        if (position == 0)
            phase = 0.0;

        phase += juce::MathConstants<double>::twoPi * frequency * pitchDrop / sampleRate;
        const auto value = amplitude * (float) (envelope * std::tanh (1.6 * std::sin (phase)));

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (channel, i, value);
    }
}

inline void fillKick (Buffer& buffer, double sampleRate, float amplitude, double periodSeconds = 0.5)
{
    const auto period = juce::jmax (1, (int) (periodSeconds * sampleRate));
    double phase = 0.0;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto position = i % period;
        const auto seconds = (double) position / sampleRate;
        const auto envelope = std::exp (-seconds * 14.0);
        const auto sweep = 120.0 * std::exp (-seconds * 32.0) + 48.0;

        if (position == 0)
            phase = 0.0;

        phase += juce::MathConstants<double>::twoPi * sweep / sampleRate;
        const auto value = amplitude * (float) (envelope * std::sin (phase));

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (channel, i, value);
    }
}

inline void fillNotePattern (Buffer& buffer, double frequency, double sampleRate, float amplitude, double noteSeconds)
{
    const auto noteSamples = juce::jmax (1, (int) (noteSeconds * sampleRate));
    double phase = 0.0;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto position = i % noteSamples;
        const auto normalised = (double) position / (double) noteSamples;
        const auto envelope = normalised < 0.75 ? std::exp (-normalised * 4.0) : 0.0;

        if (position == 0)
            phase = 0.0;

        phase += juce::MathConstants<double>::twoPi * frequency / sampleRate;
        const auto value = amplitude * (float) (envelope * std::sin (phase));

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (channel, i, value);
    }
}

inline void fillNoise (Buffer& buffer, float amplitude, int seed)
{
    juce::Random random (seed);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = amplitude * (random.nextFloat() * 2.0f - 1.0f);
    }
}

inline void fillFullMix (Buffer& buffer, double sampleRate, float amplitude)
{
    fillKick (buffer, sampleRate, amplitude * 0.55, 0.5);

    Buffer bass = makeBuffer (buffer.getNumChannels(), buffer.getNumSamples());
    fillNotePattern (bass, 61.7, sampleRate, amplitude * 0.45, 0.25);

    Buffer counter = makeBuffer (buffer.getNumChannels(), buffer.getNumSamples());
    fillNotePattern (counter, 92.5, sampleRate, amplitude * 0.3, 0.375);

    Buffer harmony = makeBuffer (buffer.getNumChannels(), buffer.getNumSamples());
    addSine (harmony, 233.1, sampleRate, amplitude * 0.16);
    addSine (harmony, 349.2, sampleRate, amplitude * 0.13);
    addSine (harmony, 523.3, sampleRate, amplitude * 0.11);
    addSine (harmony, 784.0, sampleRate, amplitude * 0.08);
    addSine (harmony, 1174.7, sampleRate, amplitude * 0.05);

    Buffer hats = makeBuffer (buffer.getNumChannels(), buffer.getNumSamples());
    fillNoise (hats, amplitude * 0.5, 4242);

    const auto hatPeriod = juce::jmax (1, (int) (0.125 * sampleRate));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);
        const auto* hat = hats.getReadPointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto seconds = (double) (i % hatPeriod) / sampleRate;
            const auto envelope = std::exp (-seconds * 90.0);
            data[i] += bass.getSample (channel, i) + counter.getSample (channel, i)
                       + harmony.getSample (channel, i)
                       + hat[i] * (float) envelope * 0.35f;
        }
    }
}

inline void fillWideBass (Buffer& buffer, double frequency, double sampleRate, float amplitude,
                          double spread = 0.82)
{
    buffer.clear();

    if (buffer.getNumChannels() < 2)
    {
        addSine (buffer, frequency, sampleRate, amplitude);
        return;
    }

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto angle = juce::MathConstants<double>::twoPi * frequency * i / sampleRate;
        buffer.setSample (0, i, amplitude * (float) std::sin (angle));
        buffer.setSample (1, i, amplitude * (float) std::sin (angle + juce::MathConstants<double>::pi * spread));
    }
}

inline void render (BassEngine& engine, Buffer& buffer, int blockSize)
{
    const auto numChannels = juce::jmin (2, buffer.getNumChannels());
    std::array<float*, 2> pointers { { nullptr, nullptr } };

    for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
    {
        const auto count = juce::jmin (blockSize, buffer.getNumSamples() - start);

        for (int channel = 0; channel < numChannels; ++channel)
            pointers[(size_t) channel] = buffer.getWritePointer (channel) + start;

        Buffer view (pointers.data(), numChannels, count);
        engine.process (view);
    }
}

inline BassEngine::Parameters position (float x, float y)
{
    BassEngine::Parameters parameters;
    parameters.x = x;
    parameters.y = y;
    return parameters;
}

inline double rms (const float* data, int numSamples)
{
    double sum = 0.0;

    for (int i = 0; i < numSamples; ++i)
        sum += (double) data[i] * (double) data[i];

    return std::sqrt (sum / juce::jmax (1, numSamples));
}

inline double magnitudeAt (const float* data, int numSamples, double frequency, double sampleRate)
{
    double real = 0.0, imaginary = 0.0, window = 0.0;
    const auto omega = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto hann = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (double) (numSamples - 1));
        real += hann * data[i] * std::cos (omega * i);
        imaginary -= hann * data[i] * std::sin (omega * i);
        window += hann;
    }

    return 2.0 * std::sqrt (real * real + imaginary * imaginary) / juce::jmax (window, 1.0);
}

inline double relativeDb (double value, double reference)
{
    return 20.0 * std::log10 (juce::jmax (value, 1.0e-12) / juce::jmax (reference, 1.0e-12));
}

} // namespace xyb::testing
