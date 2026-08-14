#include "BassEngine.h"

namespace xyb
{

static constexpr float kCharacterCrossover = 500.0f;
static constexpr float kNormalisationReference = 0.2f;
static constexpr float kNormalisationExponent = 0.7f;

void BassEngine::prepare (double sampleRate, int maximumBlockSize, int numChannels)
{
    preparedChannels = juce::jlimit (1, 2, numChannels);
    preparedBlockSize = juce::jmax (16, maximumBlockSize);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) preparedBlockSize, (juce::uint32) preparedChannels };

    splitter.prepare (spec);
    pitchTracker.prepare (sampleRate);
    analyser.prepare (sampleRate);
    subEngine.prepare (sampleRate);
    translateEngine.prepare (sampleRate);
    lowEndDynamics.prepare (sampleRate);
    spectralBalance.prepare (sampleRate, preparedChannels);
    autoGain.prepare (sampleRate);

    const auto stages = sampleRate <= 50000.0 ? 2 : (sampleRate <= 100000.0 ? 1 : 0);
    oversamplingShift = stages;

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) preparedChannels, (size_t) stages,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);

    oversampler->initProcessing ((size_t) preparedBlockSize);
    latencySamples = juce::roundToInt (oversampler->getLatencyInSamples());

    dryBuffer.setSize (preparedChannels, preparedBlockSize);
    saturationBuffer.setSize (preparedChannels, preparedBlockSize);
    parallelBuffer.setSize (preparedChannels, preparedBlockSize);
    controlBuffer.setSize (4, preparedBlockSize);
    monoBuffer.setSize (1, preparedBlockSize);

    dryDelay.prepare (preparedChannels, latencySamples + 8);
    parallelDelay.prepare (preparedChannels, latencySamples + 8);
    bypassDelay.prepare (preparedChannels, latencySamples + 8);

    dryDelay.setDelay (latencySamples);
    parallelDelay.setDelay (latencySamples);
    bypassDelay.setDelay (latencySamples);

    for (auto& filter : monoBoundaryFilter)
    {
        filter.prepare (sampleRate);
        filter.setQ (0.7071f);
        filter.setCutoff (110.0f);
    }

    for (auto& filter : subsonicFilter)
    {
        filter.prepare (sampleRate);
        filter.setHighPass (16.0f, 0.62f);
    }

    for (auto& blocker : outputDcBlocker)
        blocker.prepare (sampleRate, 4.0f);

    for (auto& envelope : channelLowEnvelope)
    {
        envelope.prepare (sampleRate);
        envelope.setTimes (25.0f, 220.0f);
    }

    saturationEnvelope.prepare (sampleRate);
    saturationEnvelope.setTimes (14.0f, 190.0f);

    outputEnvelope.prepare (sampleRate);
    outputEnvelope.setTimes (2.0f, 140.0f);

    transientFast.prepare (sampleRate);
    transientFast.setTimes (0.5f, 11.0f);

    transientSlow.prepare (sampleRate);
    transientSlow.setTimes (55.0f, 260.0f);

    inputGain.prepare (sampleRate, 25.0f);
    outputGain.prepare (sampleRate, 25.0f);
    mixAmount.prepare (sampleRate, 30.0f);
    monoAmount.prepare (sampleRate, 180.0f);
    driveControl.prepare (sampleRate, 35.0f);
    asymmetryControl.prepare (sampleRate, 60.0f);
    clippingControl.prepare (sampleRate, 60.0f);
    protectionControl.prepare (sampleRate, 90.0f);
    normalisationLevel.prepare (sampleRate, 45.0f);
    bassCrossoverControl.prepare (sampleRate, 220.0f);
    monoBoundaryControl.prepare (sampleRate, 260.0f);
    subsonicControl.prepare (sampleRate, 300.0f);
    autoGainSmoother.prepare (sampleRate, 60.0f);
    transientDepthControl.prepare (sampleRate, 120.0f);

    reset();
}

void BassEngine::reset()
{
    splitter.reset();
    pitchTracker.reset();
    analyser.reset();
    subEngine.reset();
    translateEngine.reset();
    lowEndDynamics.reset();
    spectralBalance.reset();
    autoGain.reset();

    if (oversampler != nullptr)
        oversampler->reset();

    dryBuffer.clear();
    saturationBuffer.clear();
    parallelBuffer.clear();
    controlBuffer.clear();
    monoBuffer.clear();

    dryDelay.reset();
    parallelDelay.reset();
    bypassDelay.reset();

    for (auto& filter : monoBoundaryFilter)
        filter.reset();

    for (auto& filter : subsonicFilter)
        filter.reset();

    for (auto& blocker : outputDcBlocker)
        blocker.reset();

    for (auto& envelope : channelLowEnvelope)
        envelope.reset();

    saturationEnvelope.reset();
    outputEnvelope.reset();
    transientFast.reset();
    transientSlow.reset();

    inputGain.snapTo (1.0f);
    outputGain.snapTo (1.0f);
    mixAmount.snapTo (parameters.mix);
    monoAmount.snapTo (0.4f);
    driveControl.snapTo (0.0f);
    asymmetryControl.snapTo (0.0f);
    clippingControl.snapTo (0.0f);
    protectionControl.snapTo (0.0f);
    normalisationLevel.snapTo (kNormalisationReference);
    bassCrossoverControl.snapTo (150.0f);
    monoBoundaryControl.snapTo (110.0f);
    subsonicControl.snapTo (16.0f);
    autoGainSmoother.snapTo (1.0f);
    transientDepthControl.snapTo (0.0f);

    smoothedSubsonic = 16.0f;
}

void BassEngine::updateControls (int numSamples)
{
    const auto& features = analyser.getFeatures();
    targets = computeTargets (parameters.x, parameters.y, features);

    inputGain.setTarget (juce::Decibels::decibelsToGain (parameters.inputGainDb));
    outputGain.setTarget (juce::Decibels::decibelsToGain (parameters.outputGainDb));
    mixAmount.setTarget (juce::jlimit (0.0f, 1.0f, parameters.mix));

    bassCrossoverControl.setTarget (targets.bassCrossover);
    monoBoundaryControl.setTarget (targets.monoBoundary);
    subsonicControl.setTarget (targets.subsonicCutoff);
    monoAmount.setTarget (targets.monoAmount);

    driveControl.setTarget (targets.drive);
    asymmetryControl.setTarget (targets.asymmetry);
    clippingControl.setTarget (targets.clipping);
    protectionControl.setTarget (targets.fundamentalProtection);
    transientDepthControl.setTarget (targets.transientDepth);

    splitter.setCrossovers (bassCrossoverControl.advance (numSamples), kCharacterCrossover);

    const auto boundary = monoBoundaryControl.advance (numSamples);

    for (auto& filter : monoBoundaryFilter)
        filter.setCutoff (boundary);

    const auto subsonic = subsonicControl.advance (numSamples);

    if (std::abs (subsonic - smoothedSubsonic) > 0.05f)
    {
        smoothedSubsonic = subsonic;

        for (auto& filter : subsonicFilter)
            filter.setHighPass (subsonic, 0.62f);
    }

    const auto selectivity = juce::jlimit (0.0f, 1.0f, features.tonal * (1.0f - 0.6f * features.percussive));

    subEngine.setControls (targets.subReinforcement, targets.subCentre, targets.subReconstruction,
                           targets.subharmonic, features.fundamental, selectivity);
    subEngine.updateBlock (numSamples);

    translateEngine.setControls (targets.harmonicAmount, targets.harmonicTilt, targets.harmonicBrightness,
                                 features.fundamental, features.pitchConfidence, selectivity);
    translateEngine.updateBlock (numSamples);

    lowEndDynamics.setControls (targets.lowCompression, features.percussive);
    spectralBalance.setControls (targets.mudControl, targets.fizzControl);

    const auto envelope = juce::jmax (saturationEnvelope.getValue(), 1.0e-5f);
    const auto normalised = kNormalisationReference
                            * std::pow (envelope / kNormalisationReference, kNormalisationExponent);
    normalisationLevel.setTarget (juce::jlimit (0.02f, 3.0f, normalised));

    autoGain.setEnabled (parameters.autoGain && ! parameters.delta);
}

void BassEngine::process (juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= preparedBlockSize)
    {
        processChunk (buffer);
        return;
    }

    std::array<float*, 2> pointers { { nullptr, nullptr } };

    for (int start = 0; start < numSamples; start += preparedBlockSize)
    {
        const auto count = juce::jmin (preparedBlockSize, numSamples - start);

        for (int channel = 0; channel < juce::jmin (numChannels, 2); ++channel)
            pointers[(size_t) channel] = buffer.getWritePointer (channel) + start;

        juce::AudioBuffer<float> view (pointers.data(), juce::jmin (numChannels, 2), count);
        processChunk (view);
    }
}

void BassEngine::processChunk (juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);
    const auto numSamples = juce::jmin (buffer.getNumSamples(), preparedBlockSize);

    if (numChannels <= 0 || numSamples <= 0)
        return;

    updateControls (numSamples);

    auto* mono = monoBuffer.getWritePointer (0);
    auto* drive = controlBuffer.getWritePointer (0);
    auto* asymmetry = controlBuffer.getWritePointer (1);
    auto* clipping = controlBuffer.getWritePointer (2);
    auto* postGain = controlBuffer.getWritePointer (3);

    const auto channelScale = 1.0f / (float) numChannels;
    const auto stereo = numChannels > 1;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = inputGain.next();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);
            data[i] *= gain;
            dryBuffer.getWritePointer (channel)[i] = data[i];
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto narrowing = monoAmount.next();

        float channelValue[2] = { 0.0f, 0.0f };

        for (int channel = 0; channel < numChannels; ++channel)
            channelValue[channel] = buffer.getReadPointer (channel)[i];

        const auto sourceLowLeft = monoBoundaryFilter[0].processLowPass (channelValue[0]);
        const auto sourceLowRight = stereo ? monoBoundaryFilter[1].processLowPass (channelValue[1])
                                           : sourceLowLeft;

        if (stereo)
        {
            const auto side = 0.5f * (sourceLowLeft - sourceLowRight) * narrowing;
            channelValue[0] -= side;
            channelValue[1] += side;
        }

        float lowBand[2] = { 0.0f, 0.0f };
        float midBand[2] = { 0.0f, 0.0f };
        float characterBand[2] = { 0.0f, 0.0f };

        float monoLow = 0.0f;
        float monoInput = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto bands = splitter.process (channel, channelValue[channel]);
            lowBand[channel] = bands.low;
            midBand[channel] = bands.mid;
            characterBand[channel] = bands.character;

            monoLow += bands.low;
            monoInput += channelValue[channel];
        }

        monoLow *= channelScale;
        monoInput *= channelScale;

        analyser.pushMono (monoInput);
        analyser.pushStereo (sourceLowLeft, sourceLowRight);

        const auto leftLevel = channelLowEnvelope[0].process (sourceLowLeft);
        const auto rightLevel = stereo ? channelLowEnvelope[1].process (sourceLowRight) : leftLevel;

        mono[i] = monoInput;

        const auto fundamentalBand = translateEngine.extractFundamental (monoLow);
        const auto subBus = subEngine.process (monoLow, fundamentalBand);
        const auto harmonicBus = translateEngine.process (fundamentalBand, monoLow);

        const auto attack = transientFast.process (monoInput);
        const auto sustain = transientSlow.process (monoInput);
        const auto transientIndex = juce::jlimit (0.0f, 1.0f,
                                                  (attack / juce::jmax (sustain, 1.0e-5f) - 1.02f) * 1.2f);
        const auto transientScale = 1.0f - transientDepthControl.next() * transientIndex;

        const auto compression = lowEndDynamics.process (monoLow + subBus);
        const auto dynamicsGain = 1.0f + (compression - 1.0f) * transientScale;
        const auto protection = protectionControl.next();

        const auto balance = (leftLevel - rightLevel) / juce::jmax (leftLevel + rightLevel, 1.0e-5f);
        const auto spread = juce::jlimit (-0.85f, 0.85f, balance * targets.harmonicSpread);

        float saturationMono = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto pan = stereo ? (channel == 0 ? 1.0f + spread : 1.0f - spread) : 1.0f;
            const auto lowOut = (lowBand[channel] + subBus) * dynamicsGain;
            const auto midOut = midBand[channel] + harmonicBus * pan;

            const auto saturationInput = lowOut * (1.0f - protection) + midOut;
            saturationBuffer.getWritePointer (channel)[i] = saturationInput;
            parallelBuffer.getWritePointer (channel)[i] = lowOut * protection + characterBand[channel];

            saturationMono += saturationInput;
        }

        saturationMono *= channelScale;
        saturationEnvelope.process (saturationMono);

        const auto normalisation = normalisationLevel.next();
        const auto preGain = 1.0f / juce::jmax (normalisation, 1.0e-3f);

        const auto driveAmount = driveControl.next() * transientScale;
        const auto driveValue = 0.05f + 3.6f * driveAmount * driveAmount;

        drive[i] = driveValue;
        asymmetry[i] = asymmetryControl.next();
        clipping[i] = clippingControl.next() * transientScale;
        postGain[i] = normalisation * (1.0f + 0.5f * driveAmount)
                      / juce::jmax (std::tanh (driveValue), 1.0e-3f);

        for (int channel = 0; channel < numChannels; ++channel)
            saturationBuffer.getWritePointer (channel)[i] *= preGain;
    }

    pitchTracker.process (mono, numSamples);
    analyser.setPitch (pitchTracker.getFrequency(), pitchTracker.getConfidence(), pitchTracker.getStability());
    analyser.finishBlock (numSamples);

    {
        juce::dsp::AudioBlock<float> block (saturationBuffer.getArrayOfWritePointers(),
                                            (size_t) numChannels, 0, (size_t) numSamples);
        auto upsampled = oversampler->processSamplesUp (block);

        const auto oversampledSamples = (int) upsampled.getNumSamples();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = upsampled.getChannelPointer ((size_t) channel);

            for (int i = 0; i < oversampledSamples; ++i)
            {
                const auto baseIndex = juce::jmin (numSamples - 1, i >> oversamplingShift);
                data[i] = shapeSample (data[i], drive[baseIndex], asymmetry[baseIndex], clipping[baseIndex]);
            }
        }

        oversampler->processSamplesDown (block);
    }

    parallelDelay.process (parallelBuffer);
    dryDelay.process (dryBuffer);

    bool invalid = false;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto mixValue = mixAmount.next();
        const auto outputValue = outputGain.next();
        autoGainSmoother.setTarget (autoGain.getGain());
        const auto autoGainValue = autoGainSmoother.next();

        float wetMono = 0.0f;
        float dryMono = 0.0f;
        float wet[2] = { 0.0f, 0.0f };

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto value = saturationBuffer.getReadPointer (channel)[i] * postGain[i]
                         + parallelBuffer.getReadPointer (channel)[i];

            value = subsonicFilter[(size_t) channel].process (value);
            value = spectralBalance.process (channel, value);
            value = outputDcBlocker[(size_t) channel].process (value);

            wet[channel] = value;
            wetMono += value;
            dryMono += dryBuffer.getReadPointer (channel)[i];
        }

        wetMono *= channelScale;
        dryMono *= channelScale;

        spectralBalance.analyseDry (dryMono);
        spectralBalance.analyseWet (wetMono);
        autoGain.analyse (dryMono, wetMono);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto dry = dryBuffer.getReadPointer (channel)[i];
            const auto processed = wet[channel] * autoGainValue;

            auto result = (processed - dry) * mixValue;

            if (! parameters.delta)
                result += dry;

            result = softClip (result * outputValue, 0.9f);

            if (! (std::abs (result) < 1.0e6f))
            {
                result = 0.0f;
                invalid = true;
            }

            buffer.getWritePointer (channel)[i] = result;
        }

        outputEnvelope.process (wetMono);
    }

    spectralBalance.updateBlock (numSamples);
    autoGain.updateBlock (numSamples);

    for (int channel = numChannels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    if (invalid)
    {
        buffer.clear();
        reset();
    }

    publishMeters();
}

void BassEngine::processBypassed (juce::AudioBuffer<float>& buffer)
{
    bypassDelay.process (buffer);
}

void BassEngine::publishMeters()
{
    const auto& features = analyser.getFeatures();

    meters.fundamental.store (features.fundamental, std::memory_order_relaxed);
    meters.confidence.store (features.pitchConfidence, std::memory_order_relaxed);
    meters.lowEnvelope.store (features.subLevel + features.bassLevel, std::memory_order_relaxed);
    meters.subGeneration.store (subEngine.getReinforcementLevel() + subEngine.getSynthesisLevel(),
                                std::memory_order_relaxed);
    meters.harmonicGeneration.store (translateEngine.getOutputLevel(), std::memory_order_relaxed);
    meters.drive.store (driveControl.getCurrent(), std::memory_order_relaxed);
    meters.outputLevel.store (outputEnvelope.getValue(), std::memory_order_relaxed);
    meters.harmonicTwo.store (translateEngine.getHarmonicWeight (0), std::memory_order_relaxed);
    meters.harmonicThree.store (translateEngine.getHarmonicWeight (1), std::memory_order_relaxed);
    meters.harmonicFour.store (translateEngine.getHarmonicWeight (2), std::memory_order_relaxed);
    meters.harmonicFive.store (translateEngine.getHarmonicWeight (3), std::memory_order_relaxed);
}

} // namespace xyb
