#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace xyb;

namespace
{
juce::String formatPercentage (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " %";
}

juce::String formatDecibels (float value, int)
{
    return juce::String (value, 1) + " dB";
}

juce::String formatPosition (float value, int)
{
    return juce::String (juce::roundToInt (value * 100.0f));
}

float parsePosition (const juce::String& text)
{
    return juce::jlimit (0.0f, 1.0f, text.getFloatValue() * 0.01f);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout XYBassProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ids::positionX, 1 }, "Sub / Translate", Range { 0.0f, 1.0f, 0.0001f }, 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatPosition)
                                             .withValueFromStringFunction (parsePosition)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ids::positionY, 1 }, "Clean / Dirty", Range { 0.0f, 1.0f, 0.0001f }, 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatPosition)
                                             .withValueFromStringFunction (parsePosition)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ids::input, 1 }, "Input", Range { -18.0f, 18.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatDecibels)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ids::output, 1 }, "Output", Range { -18.0f, 18.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatDecibels)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ids::mix, 1 }, "Mix", Range { 0.0f, 100.0f, 0.1f }, 100.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatPercentage)));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::autoGain, 1 }, "Auto Gain", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::delta, 1 }, "Delta", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::bypass, 1 }, "Bypass", false));

    return layout;
}

XYBassProcessor::XYBassProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "XYBASS", createLayout())
{
    xParameter = dynamic_cast<juce::AudioParameterFloat*> (parameters.getParameter (ids::positionX));
    yParameter = dynamic_cast<juce::AudioParameterFloat*> (parameters.getParameter (ids::positionY));
    inputParameter = dynamic_cast<juce::AudioParameterFloat*> (parameters.getParameter (ids::input));
    outputParameter = dynamic_cast<juce::AudioParameterFloat*> (parameters.getParameter (ids::output));
    mixParameter = dynamic_cast<juce::AudioParameterFloat*> (parameters.getParameter (ids::mix));
    autoGainParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (ids::autoGain));
    deltaParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (ids::delta));
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (ids::bypass));
}

void XYBassProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto channels = juce::jmax (1, getTotalNumOutputChannels());
    const auto rate = sampleRate > 0.0 ? sampleRate : 44100.0;

    engine.prepare (rate, samplesPerBlock, channels);
    setLatencySamples (engine.getLatencySamples());

    bypassBuffer.setSize (channels, engine.getPreparedBlockSize());
    bypassBuffer.clear();

    bypassRamp.reset (rate, 0.02);
    bypassRamp.setCurrentAndTargetValue (bypassParameter->get() ? 1.0f : 0.0f);

    pullParameters();
}

void XYBassProcessor::releaseResources()
{
    engine.reset();
}

bool XYBassProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == output;
}

void XYBassProcessor::pullParameters()
{
    xyb::BassEngine::Parameters values;
    values.x = xParameter->get();
    values.y = yParameter->get();
    values.inputGainDb = inputParameter->get();
    values.outputGainDb = outputParameter->get();
    values.mix = mixParameter->get() * 0.01f;
    values.autoGain = autoGainParameter->get();
    values.delta = deltaParameter->get();

    engine.setParameters (values);
}

void XYBassProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = juce::jmin (buffer.getNumChannels(), bypassBuffer.getNumChannels());
    const auto numSamples = buffer.getNumSamples();

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);

    pullParameters();

    const auto chunkSize = bypassBuffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0 || chunkSize <= 0)
        return;

    bypassRamp.setTargetValue (bypassParameter->get() ? 1.0f : 0.0f);

    std::array<float*, 2> pointers { { nullptr, nullptr } };

    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const auto count = juce::jmin (chunkSize, numSamples - start);

        for (int channel = 0; channel < numChannels; ++channel)
            pointers[(size_t) channel] = buffer.getWritePointer (channel) + start;

        juce::AudioBuffer<float> view (pointers.data(), numChannels, count);

        for (int channel = 0; channel < numChannels; ++channel)
            bypassBuffer.copyFrom (channel, 0, view, channel, 0, count);

        engine.processBypassed (bypassBuffer, count);
        engine.process (view);

        if (bypassRamp.isSmoothing() || bypassRamp.getCurrentValue() > 0.0f)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto blend = bypassRamp.getNextValue();

                for (int channel = 0; channel < numChannels; ++channel)
                {
                    auto* data = view.getWritePointer (channel);
                    data[i] += (bypassBuffer.getReadPointer (channel)[i] - data[i]) * blend;
                }
            }
        }
    }
}

void XYBassProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    engine.processBypassed (buffer, buffer.getNumSamples());
}

int XYBassProcessor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

int XYBassProcessor::getCurrentProgram()
{
    return currentProgram;
}

void XYBassProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    currentProgram = index;
    const auto& preset = presets[(size_t) index];

    xParameter->beginChangeGesture();
    xParameter->setValueNotifyingHost (preset.x);
    xParameter->endChangeGesture();

    yParameter->beginChangeGesture();
    yParameter->setValueNotifyingHost (preset.y);
    yParameter->endChangeGesture();

    inputParameter->beginChangeGesture();
    inputParameter->setValueNotifyingHost (inputParameter->convertTo0to1 (preset.input));
    inputParameter->endChangeGesture();

    mixParameter->beginChangeGesture();
    mixParameter->setValueNotifyingHost (mixParameter->convertTo0to1 (preset.mix));
    mixParameter->endChangeGesture();

    outputParameter->beginChangeGesture();
    outputParameter->setValueNotifyingHost (outputParameter->convertTo0to1 (preset.output));
    outputParameter->endChangeGesture();

    autoGainParameter->beginChangeGesture();
    autoGainParameter->setValueNotifyingHost (preset.autoGain ? 1.0f : 0.0f);
    autoGainParameter->endChangeGesture();
}

const juce::String XYBassProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return {};

    return presets[(size_t) index].name;
}

void XYBassProcessor::changeProgramName (int, const juce::String&)
{
}

void XYBassProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("stateVersion", 1, nullptr);
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void XYBassProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    if ((int) state.getProperty ("stateVersion", 1) > 1)
        return;

    currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
    parameters.replaceState (state);

    // A switch keeps the raw value a host wrote, and replaceState skips parameters whose
    // snapped value is unchanged, so an off-grid value would otherwise survive the restore.
    for (auto* id : { ids::autoGain, ids::delta, ids::bypass })
    {
        auto* parameter = parameters.getParameter (id);
        const auto target = parameter->convertTo0to1 (parameter->convertFrom0to1 (parameter->getValue()));

        if (! juce::exactlyEqual (parameter->getValue(), target))
            parameter->setValueNotifyingHost (target);
    }

    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}.withProgramChanged (true));
}

juce::AudioProcessorEditor* XYBassProcessor::createEditor()
{
    return new XYBassEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new XYBassProcessor();
}
