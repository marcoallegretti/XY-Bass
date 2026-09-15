#include "BassEngine.h"

namespace xyb
{

static constexpr float kNormalisationReference = 0.2f;
static constexpr float kNormalisationExponent = 0.7f;
static constexpr float kCeilingKnee = 0.9f;
static constexpr float kAbsurdLevel = 1000.0f;

void BassEngine::prepare (double newSampleRate, int maximumBlockSize, int numChannels)
{
    const auto sampleRate = juce::jlimit (8000.0, 768000.0,
                                          newSampleRate > 0.0 ? newSampleRate : 44100.0);

    currentSampleRate = sampleRate;
    preparedChannels = juce::jlimit (1, 2, numChannels);
    preparedBlockSize = juce::jmax (16, maximumBlockSize);
    controlPeriod = juce::jlimit (16, 1024, juce::nextPowerOfTwo (juce::roundToInt (sampleRate / 187.5)));

    splitter.prepare (sampleRate, preparedChannels);
    pitchTracker.prepare (sampleRate);
    analyser.prepare (sampleRate);
    subEngine.prepare (sampleRate);
    translateEngine.prepare (sampleRate);
    lowEndDynamics.prepare (sampleRate);
    spectralBalance.prepare (sampleRate, preparedChannels);
    autoGain.prepare (sampleRate);

    const auto stages = sampleRate <= 100000.0 ? 1 : 0;
    oversamplingShift = stages;

    oversampler.prepare (preparedChannels, controlPeriod, stages > 0);

    // The bands arrive delayed by the splitter, so only the saturation path's extra delay
    // separates them from the recombined wet signal.
    const auto oversamplerLatency = oversampler.getLatencyInSamples();
    latencySamples = oversamplerLatency + splitter.getLatencySamples();

    dryBuffer.setSize (preparedChannels, controlPeriod);
    lowBuffer.setSize (preparedChannels, controlPeriod);
    midBuffer.setSize (preparedChannels, controlPeriod);
    characterBuffer.setSize (preparedChannels, controlPeriod);
    saturationBuffer.setSize (preparedChannels, controlPeriod);
    linearBuffer.setSize (preparedChannels, controlPeriod);
    parallelBuffer.setSize (preparedChannels, controlPeriod);
    shaperControls.assign ((size_t) controlPeriod, ShaperControls {});
    monoBuffer.setSize (1, controlPeriod);

    dryDelay.prepare (preparedChannels, latencySamples + 8);
    linearDelay.prepare (preparedChannels, oversamplerLatency + 8);
    parallelDelay.prepare (preparedChannels, oversamplerLatency + 8);
    bypassDelay.prepare (preparedChannels, latencySamples + 8);

    dryDelay.setDelay (latencySamples);
    linearDelay.setDelay (oversamplerLatency);
    parallelDelay.setDelay (oversamplerLatency);
    bypassDelay.setDelay (latencySamples);

    for (auto& channelFilters : subsonicFilter)
    {
        channelFilters[0].prepare (sampleRate);
        channelFilters[0].setHighPass (16.0f, 0.5412f);
        channelFilters[1].prepare (sampleRate);
        channelFilters[1].setHighPass (16.0f, 1.3066f);
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
    deltaStep = (float) (1.0 / (0.02 * sampleRate));
    monoAmount.prepare (sampleRate, 180.0f);
    driveControl.prepare (sampleRate, 35.0f);
    asymmetryControl.prepare (sampleRate, 60.0f);
    clippingControl.prepare (sampleRate, 60.0f);
    protectionControl.prepare (sampleRate, 90.0f);
    normalisationLevel.prepare (sampleRate, 45.0f);
    bassCrossoverControl.prepare (sampleRate, 220.0f);
    subsonicControl.prepare (sampleRate, 300.0f);
    autoGainSmoother.prepare (sampleRate, 60.0f);
    coreWeight.prepare (sampleRate, 250.0f);
    transientDepthControl.prepare (sampleRate, 120.0f);
    spreadControl.prepare (sampleRate, 45.0f);

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

    oversampler.reset();

    dryBuffer.clear();
    lowBuffer.clear();
    midBuffer.clear();
    characterBuffer.clear();
    saturationBuffer.clear();
    linearBuffer.clear();
    parallelBuffer.clear();
    std::fill (shaperControls.begin(), shaperControls.end(), ShaperControls {});
    monoBuffer.clear();

    dryDelay.reset();
    linearDelay.reset();
    parallelDelay.reset();
    bypassDelay.reset();

    for (auto& channelFilters : subsonicFilter)
        for (auto& filter : channelFilters)
            filter.reset();

    for (auto& blocker : outputDcBlocker)
        blocker.reset();

    for (auto& envelope : channelLowEnvelope)
        envelope.reset();

    saturationEnvelope.reset();
    outputEnvelope.reset();
    transientFast.reset();
    transientSlow.reset();

    inputGain.snapTo (juce::Decibels::decibelsToGain (parameters.inputGainDb));
    outputGain.snapTo (juce::Decibels::decibelsToGain (parameters.outputGainDb));
    mixAmount.snapTo (juce::jlimit (0.0f, 1.0f, parameters.mix));
    deltaBlend = parameters.delta ? 1.0f : 0.0f;
    monoAmount.snapTo (0.4f);
    driveControl.snapTo (0.0f);
    asymmetryControl.snapTo (0.0f);
    clippingControl.snapTo (0.0f);
    protectionControl.snapTo (0.0f);
    normalisationLevel.snapTo (kNormalisationReference);
    bassCrossoverControl.snapTo (150.0f);
    subsonicControl.snapTo (16.0f);
    autoGainSmoother.snapTo (1.0f);
    transientDepthControl.snapTo (0.0f);
    spreadControl.snapTo (0.0f);
    coreWeight.snapTo (0.0f);

    smoothedSubsonic = 16.0f;
    coldStart = true;
    ceilingHold = 0.0f;
    periodCeilingActive = false;
    periodPosition = 0;
}

void BassEngine::updateControls (int numSamples)
{
    const auto& features = analyser.getFeatures();
    targets = computeTargets (parameters.x, parameters.y, features);

    inputGain.setTarget (juce::Decibels::decibelsToGain (parameters.inputGainDb));
    outputGain.setTarget (juce::Decibels::decibelsToGain (parameters.outputGainDb));
    mixAmount.setTarget (juce::jlimit (0.0f, 1.0f, parameters.mix));

    bassCrossoverControl.setTarget (targets.bassCrossover);
    subsonicControl.setTarget (targets.subsonicCutoff);
    monoAmount.setTarget (targets.monoAmount);

    driveControl.setTarget (targets.drive);
    asymmetryControl.setTarget (targets.asymmetry);
    clippingControl.setTarget (targets.clipping);
    protectionControl.setTarget (targets.fundamentalProtection);
    transientDepthControl.setTarget (targets.transientDepth);
    spreadControl.setTarget (targets.harmonicSpread);
    coreWeight.setTarget (juce::jlimit (0.0f, 1.0f, -features.correlation));

    // After a reset the controls start at their first targets rather than gliding in from
    // arbitrary resting values, which an offline render would hear at its start.
    if (std::exchange (coldStart, false))
        for (auto* control : { &bassCrossoverControl, &subsonicControl, &monoAmount, &driveControl, &asymmetryControl,
                               &clippingControl, &protectionControl, &transientDepthControl, &spreadControl, &coreWeight })
            control->snapTo (control->getTarget());

    splitter.setCrossover (bassCrossoverControl.advance (numSamples));

    const auto subsonic = subsonicControl.advance (numSamples);

    if (std::abs (subsonic - smoothedSubsonic) > 0.05f)
    {
        smoothedSubsonic = subsonic;

        for (auto& channelFilters : subsonicFilter)
        {
            channelFilters[0].setHighPass (subsonic, 0.5412f);
            channelFilters[1].setHighPass (subsonic, 1.3066f);
        }
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
    juce::ScopedNoDenormals noDenormals;

    if (! oversampler.isPrepared())
        return;

    const auto numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const auto numSamples = buffer.getNumSamples();
    std::array<float*, 2> pointers { { nullptr, nullptr } };

    // Controls advance at fixed sample positions carried across calls, so every host
    // block schedule renders the same output.
    for (int start = 0; start < numSamples;)
    {
        if (periodPosition == 0)
            updateControls (controlPeriod);

        const auto count = juce::jmin (controlPeriod - periodPosition, numSamples - start);

        for (int channel = 0; channel < numChannels; ++channel)
            pointers[(size_t) channel] = buffer.getWritePointer (channel) + start;

        juce::AudioBuffer<float> view (pointers.data(), numChannels, count);
        start += count;

        if (! processChunk (view))
        {
            reset();
            continue;
        }

        periodPosition += count;

        if (periodPosition == controlPeriod)
        {
            finishPeriod();
            periodPosition = 0;
        }
    }

    for (int channel = numChannels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);
}

void BassEngine::finishPeriod()
{
    analyser.setPitch (pitchTracker.getFrequency(), pitchTracker.getConfidence(), pitchTracker.getStability());
    analyser.finishBlock (controlPeriod);

    spectralBalance.updateBlock (controlPeriod);
    autoGain.updateBlock (controlPeriod);

    const auto holdSamples = (float) juce::jmax (1, (int) (0.35 * currentSampleRate));
    ceilingHold = periodCeilingActive ? 1.0f : juce::jmax (0.0f, ceilingHold - (float) controlPeriod / holdSamples);
    periodCeilingActive = false;

    publishMeters();
}

bool BassEngine::processChunk (juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);
    const auto numSamples = juce::jmin (buffer.getNumSamples(), controlPeriod);

    if (numChannels <= 0 || numSamples <= 0)
        return true;

    auto* mono = monoBuffer.getWritePointer (0);

    const auto channelScale = 1.0f / (float) numChannels;
    const auto stereo = numChannels > 1;

    auto absurd = false;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = inputGain.next();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);
            data[i] *= gain;
            dryBuffer.getWritePointer (channel)[i] = data[i];
            absurd = absurd || ! (std::abs (data[i]) < kAbsurdLevel);
        }
    }

    // A sample 60 dB over full scale, or not a number at all, would charge every envelope and
    // filter for seconds, so the engine starts again rather than processing it.
    if (absurd)
    {
        buffer.clear();
        return false;
    }

    for (int channel = 0; channel < numChannels; ++channel)
        splitter.process (channel, buffer.getReadPointer (channel), lowBuffer.getWritePointer (channel),
                          midBuffer.getWritePointer (channel), characterBuffer.getWritePointer (channel), numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto narrowing = monoAmount.next();
        const auto polarity = coreWeight.next();

        float channelValue[2] = { 0.0f, 0.0f };

        for (int channel = 0; channel < numChannels; ++channel)
            channelValue[channel] = buffer.getReadPointer (channel)[i];

        float lowBand[2] = { 0.0f, 0.0f };
        float midBand[2] = { 0.0f, 0.0f };
        float characterBand[2] = { 0.0f, 0.0f };

        for (int channel = 0; channel < numChannels; ++channel)
        {
            lowBand[channel] = lowBuffer.getReadPointer (channel)[i];
            midBand[channel] = midBuffer.getReadPointer (channel)[i];
            characterBand[channel] = characterBuffer.getReadPointer (channel)[i];
        }

        const auto monoLow = stereo ? 0.5f * ((1.0f + polarity) * lowBand[0] + (1.0f - polarity) * lowBand[1])
                                    : lowBand[0];
        const auto monoInput = stereo ? 0.5f * ((1.0f + polarity) * channelValue[0]
                                                + (1.0f - polarity) * channelValue[1])
                                      : channelValue[0];

        analyser.pushMono (monoInput);
        analyser.pushStereo (lowBand[0], stereo ? lowBand[1] : lowBand[0]);

        const auto leftLevel = channelLowEnvelope[0].process (lowBand[0]);
        const auto rightLevel = stereo ? channelLowEnvelope[1].process (lowBand[1]) : leftLevel;

        if (stereo)
        {
            const auto centre = 0.5f * (lowBand[0] + lowBand[1]);
            const auto side = 0.5f * (lowBand[0] - lowBand[1]) * (1.0f - narrowing);
            lowBand[0] = centre + side;
            lowBand[1] = centre - side;
        }

        mono[i] = monoInput;

        const auto fundamentalBand = translateEngine.extractFundamental (monoLow);
        const auto subBus = subEngine.process (monoLow, fundamentalBand,
                                              translateEngine.getFundamentalMagnitude(),
                                              translateEngine.getFundamentalQuadrature());
        const auto harmonicBus = translateEngine.process (fundamentalBand);

        // Transients are read from the low band, so hi-hats cannot modulate the bass drive, and
        // the drive eases off as the attack reaches the shaper rather than before it.
        const auto attack = transientFast.process (monoLow);
        const auto sustain = transientSlow.process (monoLow);
        const auto transientIndex = juce::jlimit (0.0f, 1.0f,
                                                  (attack / juce::jmax (sustain, 1.0e-5f) - 1.02f) * 1.2f);
        const auto transientScale = 1.0f - transientDepthControl.next() * transientIndex;

        const auto compression = lowEndDynamics.process (monoLow + subBus);
        const auto dynamicsGain = 1.0f + (compression - 1.0f) * transientScale;
        const auto protection = protectionControl.next();

        const auto balance = (leftLevel - rightLevel) / juce::jmax (leftLevel + rightLevel, 1.0e-5f);
        const auto spread = juce::jlimit (-0.85f, 0.85f, balance * spreadControl.next());

        float saturationMono = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto pan = stereo ? (channel == 0 ? 1.0f + spread : 1.0f - spread) : 1.0f;
            const auto lowOut = (lowBand[channel] + subBus) * dynamicsGain;
            const auto midOut = midBand[channel] + harmonicBus * pan;

            const auto saturationInput = lowOut * (1.0f - protection) + midOut;
            saturationBuffer.getWritePointer (channel)[i] = saturationInput;
            linearBuffer.getWritePointer (channel)[i] = saturationInput;
            parallelBuffer.getWritePointer (channel)[i] = lowOut * protection + characterBand[channel];

            saturationMono += saturationInput;
        }

        saturationMono *= channelScale;
        saturationEnvelope.process (saturationMono);

        const auto normalisation = normalisationLevel.next();
        const auto preGain = 1.0f / juce::jmax (normalisation, 1.0e-3f);

        const auto driveAmount = driveControl.next() * transientScale;
        auto& controls = shaperControls[(size_t) i];

        controls = makeShaperControls (driveAmount, asymmetryControl.next(),
                                       clippingControl.next() * transientScale);
        controls.outputGain = normalisation * (1.0f + 0.5f * driveAmount)
                              / juce::jmax (std::tanh (controls.drive), 1.0e-3f);

        for (int channel = 0; channel < numChannels; ++channel)
            saturationBuffer.getWritePointer (channel)[i] *= preGain;
    }

    pitchTracker.process (mono, numSamples);

    {
        juce::dsp::AudioBlock<float> block (saturationBuffer.getArrayOfWritePointers(),
                                            (size_t) numChannels, 0, (size_t) numSamples);
        auto upsampled = oversampler.processSamplesUp (block);

        const auto factor = 1 << oversamplingShift;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = upsampled.getChannelPointer ((size_t) channel);

            for (int base = 0; base < numSamples; ++base)
            {
                const auto controls = shaperControls[(size_t) base];
                auto* slice = data + (base << oversamplingShift);

                for (int step = 0; step < factor; ++step)
                    slice[step] = shapeSample (slice[step], controls);
            }
        }

        oversampler.processSamplesDown (block);
    }

    linearDelay.process (linearBuffer, numSamples);
    parallelDelay.process (parallelBuffer, numSamples);
    dryDelay.process (dryBuffer, numSamples);

    bool invalid = false;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto mixValue = mixAmount.next();
        const auto outputValue = outputGain.next();
        autoGainSmoother.setTarget (autoGain.getGain());
        const auto autoGainValue = autoGainSmoother.next();

        // A linear fade lands exactly on either mode, so Mix at zero stays exact once it ends.
        deltaBlend = juce::jlimit (0.0f, 1.0f, deltaBlend + (parameters.delta ? deltaStep : -deltaStep));
        const auto dryShare = 1.0f - deltaBlend;

        float wetMono = 0.0f;
        float dryMono = 0.0f;
        float wet[2] = { 0.0f, 0.0f };

        for (int channel = 0; channel < numChannels; ++channel)
        {
            // Only what the saturator changes passes the subsonic filter, where its dc and
            // infrasonic products are; the bands it carries stay aligned with the dry path.
            const auto linear = linearBuffer.getReadPointer (channel)[i];
            auto shaped = saturationBuffer.getReadPointer (channel)[i] * shaperControls[(size_t) i].outputGain - linear;
            shaped = subsonicFilter[(size_t) channel][1].process (subsonicFilter[(size_t) channel][0].process (shaped));

            auto value = linear + shaped + parallelBuffer.getReadPointer (channel)[i];

            value = spectralBalance.process (channel, value);

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

            // Every path shares the dry delay, so the difference holds only what the processing
            // adds or removes. Blocking its dc rather than the output's leaves Mix at zero untouched.
            const auto difference = outputDcBlocker[(size_t) channel].process (processed - dry);

            // The ceiling bends the processed signal before Mix blends it with dry: it has no step,
            // Mix at zero never meets it, and a full scale input cannot blend past full scale.
            const auto levelled = (dry + difference) * outputValue;
            const auto limited = softClip (levelled, kCeilingKnee);
            const auto contribution = (limited - dry * outputValue) * mixValue;

            periodCeilingActive = periodCeilingActive || std::abs (levelled) > kCeilingKnee;

            auto result = dry * outputValue * dryShare + contribution;

            if (! (std::abs (result) < 1.0e6f))
            {
                result = 0.0f;
                invalid = true;
            }

            buffer.getWritePointer (channel)[i] = result;
        }

        outputEnvelope.process (wetMono);
    }

    for (int channel = numChannels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    if (invalid)
    {
        buffer.clear();
        return false;
    }

    return true;
}

double BassEngine::getTailSeconds() const noexcept
{
    // The slowest measured decay to -60 dB after a sustained low note, across the pad and the
    // factory presets, was 0.84 s, at full dirt on the sub side.
    constexpr double decaySeconds = 1.0;
    return decaySeconds + (double) latencySamples / currentSampleRate;
}

void BassEngine::processBypassed (juce::AudioBuffer<float>& buffer, int numSamples)
{
    bypassDelay.process (buffer, numSamples);
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
    meters.ceiling.store (ceilingHold, std::memory_order_relaxed);
    meters.harmonicTwo.store (translateEngine.getHarmonicWeight (0), std::memory_order_relaxed);
    meters.harmonicThree.store (translateEngine.getHarmonicWeight (1), std::memory_order_relaxed);
    meters.harmonicFour.store (translateEngine.getHarmonicWeight (2), std::memory_order_relaxed);
    meters.harmonicFive.store (translateEngine.getHarmonicWeight (3), std::memory_order_relaxed);
}

} // namespace xyb
