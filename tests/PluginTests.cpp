#include "PluginProcessor.h"

#include <iostream>

namespace
{

int failures = 0;
int checks = 0;

void check (bool condition, const juce::String& description)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::cout << "  FAIL  " << description << std::endl;
    }
}

void section (const juce::String& name)
{
    std::cout << name << std::endl;
}

float valueOf (juce::AudioProcessorValueTreeState& state, const char* id)
{
    return state.getParameter (id)->getValue();
}

void testParameterLayout()
{
    section ("parameter layout");

    XYBassProcessor processor;
    auto& state = processor.getValueTreeState();

    for (auto* id : { xyb::ids::positionX, xyb::ids::positionY, xyb::ids::input, xyb::ids::output,
                      xyb::ids::mix, xyb::ids::autoGain, xyb::ids::delta, xyb::ids::bypass })
        check (state.getParameter (id) != nullptr, juce::String ("parameter ") + id + " exists");

    check (processor.getBypassParameter() != nullptr, "the host sees a bypass parameter");
    check (processor.getParameters().size() == 8, "the plugin exposes exactly the intended parameters");

    check (state.getParameter (xyb::ids::positionX)->getValue() == 0.5f, "x starts centred");
    check (state.getParameter (xyb::ids::positionY)->getValue() == 0.5f, "y starts centred");
    check (state.getParameter (xyb::ids::mix)->getValue() == 1.0f, "mix starts fully wet");
}

void testBusLayouts()
{
    section ("bus layouts");

    XYBassProcessor processor;

    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());

    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add (juce::AudioChannelSet::mono());
    mono.outputBuses.add (juce::AudioChannelSet::mono());

    juce::AudioProcessor::BusesLayout mismatched;
    mismatched.inputBuses.add (juce::AudioChannelSet::mono());
    mismatched.outputBuses.add (juce::AudioChannelSet::stereo());

    check (processor.checkBusesLayoutSupported (stereo), "stereo is supported");
    check (processor.checkBusesLayoutSupported (mono), "mono is supported");
    check (! processor.checkBusesLayoutSupported (mismatched), "mismatched layouts are rejected");
}

void testStateRoundTrip()
{
    section ("state round trip");

    XYBassProcessor source;
    auto& sourceState = source.getValueTreeState();

    sourceState.getParameter (xyb::ids::positionX)->setValueNotifyingHost (0.23f);
    sourceState.getParameter (xyb::ids::positionY)->setValueNotifyingHost (0.81f);
    sourceState.getParameter (xyb::ids::mix)->setValueNotifyingHost (0.42f);
    sourceState.getParameter (xyb::ids::delta)->setValueNotifyingHost (1.0f);
    sourceState.getParameter (xyb::ids::autoGain)->setValueNotifyingHost (0.0f);
    source.setCurrentProgram (5);

    juce::MemoryBlock block;
    source.getStateInformation (block);

    XYBassProcessor destination;
    destination.setStateInformation (block.getData(), (int) block.getSize());

    auto& target = destination.getValueTreeState();

    check (std::abs (valueOf (target, xyb::ids::positionY) - valueOf (sourceState, xyb::ids::positionY)) < 1.0e-6f,
           "y survives a state round trip");
    check (std::abs (valueOf (target, xyb::ids::mix) - valueOf (sourceState, xyb::ids::mix)) < 1.0e-6f,
           "mix survives a state round trip");
    check (valueOf (target, xyb::ids::delta) == valueOf (sourceState, xyb::ids::delta),
           "delta survives a state round trip");
    check (valueOf (target, xyb::ids::autoGain) == valueOf (sourceState, xyb::ids::autoGain),
           "auto gain survives a state round trip");
    check (destination.getCurrentProgram() == source.getCurrentProgram(),
           "the selected program survives a state round trip");

    XYBassProcessor untouched;
    untouched.setStateInformation (block.getData(), 3);
    check (untouched.getValueTreeState().getParameter (xyb::ids::positionX)->getValue() == 0.5f,
           "a truncated state is ignored");
}

void testPresetRecall()
{
    section ("preset recall");

    XYBassProcessor processor;
    const auto& presets = xyb::getFactoryPresets();

    check (processor.getNumPrograms() == (int) presets.size(), "every factory preset is exposed");

    for (int index = 0; index < processor.getNumPrograms(); ++index)
    {
        processor.setCurrentProgram (index);
        const auto& preset = presets[(size_t) index];
        auto& state = processor.getValueTreeState();

        check (std::abs (state.getParameter (xyb::ids::positionX)->getValue() - preset.x) < 1.0e-5f,
               juce::String ("preset ") + preset.name + " recalls its x position");
        check (std::abs (state.getParameter (xyb::ids::positionY)->getValue() - preset.y) < 1.0e-5f,
               juce::String ("preset ") + preset.name + " recalls its y position");
        check (processor.getProgramName (index) == juce::String (preset.name),
               juce::String ("preset ") + preset.name + " reports its name");
    }
}

void testProcessingContract()
{
    section ("processing contract");

    XYBassProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    check (processor.getLatencySamples() > 0, "the plugin reports its latency");
    check (! processor.acceptsMidi() && ! processor.producesMidi(), "no midi is claimed");

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 512; ++i)
            buffer.setSample (channel, i, 0.3f * std::sin (juce::MathConstants<float>::twoPi * 50.0f * (float) i / 48000.0f));

    juce::AudioBuffer<float> reference (buffer);

    for (int pass = 0; pass < 40; ++pass)
        processor.processBlock (buffer, midi);

    check (buffer.getMagnitude (0, 512) < 2.0f, "the processed output stays bounded");

    processor.getValueTreeState().getParameter (xyb::ids::bypass)->setValueNotifyingHost (1.0f);

    for (int pass = 0; pass < 60; ++pass)
    {
        buffer.makeCopyOf (reference);
        processor.processBlock (buffer, midi);
    }

    const auto latency = processor.getLatencySamples();
    double worst = 0.0;

    for (int i = latency; i < 512; ++i)
        worst = juce::jmax (worst, (double) std::abs (buffer.getSample (0, i) - reference.getSample (0, i - latency)));

    check (worst < 1.0e-5, "bypass returns the input delayed by the reported latency");

    processor.releaseResources();
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    testParameterLayout();
    testBusLayouts();
    testStateRoundTrip();
    testPresetRecall();
    testProcessingContract();

    std::cout << std::endl
              << (failures == 0 ? "PASSED " : "FAILED ")
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
