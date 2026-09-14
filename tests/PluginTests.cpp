#include "PluginProcessor.h"

#include <iostream>
#include <vector>

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

void report (const juce::String& description, double value)
{
    std::cout << "        " << description << " = " << juce::String (value, 6) << std::endl;
}

void section (const juce::String& name)
{
    std::cout << name << std::endl;
}

int processorPresetCount()
{
    return (int) xyb::getFactoryPresets().size();
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

    check (juce::exactlyEqual (state.getParameter (xyb::ids::positionX)->getValue(), 0.5f), "x starts centred");
    check (juce::exactlyEqual (state.getParameter (xyb::ids::positionY)->getValue(), 0.5f), "y starts centred");
    check (juce::exactlyEqual (state.getParameter (xyb::ids::mix)->getValue(), 1.0f), "mix starts fully wet");
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
    check (juce::exactlyEqual (valueOf (target, xyb::ids::delta), valueOf (sourceState, xyb::ids::delta)),
           "delta survives a state round trip");
    check (juce::exactlyEqual (valueOf (target, xyb::ids::autoGain), valueOf (sourceState, xyb::ids::autoGain)),
           "auto gain survives a state round trip");
    check (destination.getCurrentProgram() == source.getCurrentProgram(),
           "the selected program survives a state round trip");

    XYBassProcessor untouched;
    untouched.setStateInformation (block.getData(), 3);
    check (juce::exactlyEqual (untouched.getValueTreeState().getParameter (xyb::ids::positionX)->getValue(), 0.5f),
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

void renderSchedule (XYBassProcessor& processor, juce::AudioBuffer<float>& buffer,
                     const std::vector<int>& schedule)
{
    juce::MidiBuffer midi;
    float* pointers[2] = { nullptr, nullptr };
    const auto numChannels = buffer.getNumChannels();

    auto step = (size_t) 0;

    for (int start = 0; start < buffer.getNumSamples();)
    {
        const auto want = schedule[step % schedule.size()];
        const auto count = juce::jmin (want, buffer.getNumSamples() - start);
        ++step;

        if (count <= 0)
            break;

        for (int channel = 0; channel < numChannels; ++channel)
            pointers[channel] = buffer.getWritePointer (channel) + start;

        juce::AudioBuffer<float> view (pointers, numChannels, count);
        processor.processBlock (view, midi);
        start += count;
    }
}

void testVariableBlockSizes()
{
    section ("variable host block sizes");

    const auto sampleRate = 48000.0;
    const auto prepared = 512;
    const auto length = prepared * 40;

    juce::AudioBuffer<float> source (2, length);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < length; ++i)
            source.setSample (channel, i, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                    * 55.0 * i / sampleRate));

    auto renderWith = [&] (const std::vector<int>& schedule, float mix, bool bypassed)
    {
        XYBassProcessor processor;
        processor.setPlayConfigDetails (2, 2, sampleRate, prepared);
        processor.getValueTreeState().getParameter (xyb::ids::mix)
                 ->setValueNotifyingHost (mix);
        processor.getValueTreeState().getParameter (xyb::ids::bypass)
                 ->setValueNotifyingHost (bypassed ? 1.0f : 0.0f);
        processor.prepareToPlay (sampleRate, prepared);
        processor.prepareToPlay (sampleRate, prepared);

        juce::AudioBuffer<float> buffer (source);
        renderSchedule (processor, buffer, schedule);
        return buffer;
    };

    const std::vector<int> uniform { prepared };
    const std::vector<int> ragged { 512, 512, 200, 64, 512, 333, 512, 1, 512 };
    const std::vector<int> oversized { 2048, 1024 };

    auto worstDifference = [&] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                                int from)
    {
        double worst = 0.0;

        for (int i = from; i < a.getNumSamples(); ++i)
            worst = juce::jmax (worst, (double) std::abs (a.getSample (0, i) - b.getSample (0, i)));

        return worst;
    };

    const auto dryReference = renderWith (uniform, 0.0f, false);
    const auto dryRagged = renderWith (ragged, 0.0f, false);
    const auto dryOversized = renderWith (oversized, 0.0f, false);

    const auto dryRaggedError = worstDifference (dryReference, dryRagged, prepared * 4);
    const auto dryOversizedError = worstDifference (dryReference, dryOversized, prepared * 4);

    report ("dry path error with ragged blocks", dryRaggedError);
    report ("dry path error with oversized blocks", dryOversizedError);

    check (juce::exactlyEqual (dryRaggedError, 0.0), "the delay compensated dry path is independent of the block schedule");
    check (juce::exactlyEqual (dryOversizedError, 0.0), "the delay compensated dry path is independent of oversized blocks");

    const auto reference = renderWith (uniform, 1.0f, false);
    const auto raggedWet = renderWith (ragged, 1.0f, false);
    const auto oversizedWet = renderWith (oversized, 1.0f, false);

    const auto raggedError = worstDifference (reference, raggedWet, prepared * 4);
    const auto oversizedError = worstDifference (reference, oversizedWet, prepared * 4);

    report ("wet output error with ragged blocks", raggedError);
    report ("wet output error with oversized blocks", oversizedError);

    check (juce::exactlyEqual (oversizedError, 0.0), "the wet path is unaffected by oversized host blocks");
    check (juce::exactlyEqual (raggedError, 0.0), "the wet path is independent of the host block schedule");

    int latency = 0;

    {
        XYBassProcessor probe;
        probe.setPlayConfigDetails (2, 2, sampleRate, prepared);
        probe.prepareToPlay (sampleRate, prepared);
        latency = probe.getLatencySamples();
        probe.releaseResources();
    }

    report ("reported latency", latency);

    auto checkBypassAlignment = [&] (const std::vector<int>& schedule, const juce::String& name)
    {
        const auto rendered = renderWith (schedule, 1.0f, true);
        double worst = 0.0;

        for (int i = prepared * 4; i < length; ++i)
            worst = juce::jmax (worst, (double) std::abs (rendered.getSample (0, i)
                                                          - source.getSample (0, i - latency)));

        report ("bypass alignment error, " + name, worst);
        check (worst < 1.0e-5, "bypass returns the delayed input with " + name);
    };

    checkBypassAlignment (uniform, "uniform blocks");
    checkBypassAlignment (ragged, "ragged blocks");
    checkBypassAlignment (oversized, "oversized blocks");
}

void testEditorLifecycle()
{
    section ("editor lifecycle");

    XYBassProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    auto* editor = processor.createEditorAndMakeActive();
    check (editor != nullptr, "the processor creates an editor");

    if (editor != nullptr)
    {
        check (editor->getWidth() > 0 && editor->getHeight() > 0, "the editor opens with a usable size");

        editor->setSize (520, 554);
        editor->setSize (900, 960);

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        buffer.clear();

        for (int pass = 0; pass < 8; ++pass)
            processor.processBlock (buffer, midi);

        check (editor->getWidth() >= 520, "the editor honours its minimum width");
    }

    processor.editorBeingDeleted (editor);
    delete editor;

    auto* second = processor.createEditorAndMakeActive();
    check (second != nullptr, "the editor can be reopened");
    processor.editorBeingDeleted (second);
    delete second;

    processor.releaseResources();
}

void testFactoryPresets()
{
    section ("factory preset behaviour");

    const auto sampleRate = 48000.0;
    const auto blockSize = 256;
    const auto length = blockSize * 220;

    juce::AudioBuffer<float> source (2, length);

    for (int channel = 0; channel < 2; ++channel)
    {
        double phase = 0.0;

        for (int i = 0; i < length; ++i)
        {
            const auto position = i % (int) (sampleRate * 0.5);
            const auto seconds = (double) position / sampleRate;
            const auto envelope = std::exp (-seconds * 3.0);

            if (position == 0)
                phase = 0.0;

            phase += juce::MathConstants<double>::twoPi * 49.0 / sampleRate;
            source.setSample (channel, i, 0.3f * (float) (envelope * std::sin (phase)));
        }
    }

    double quietest = 1.0e9;
    double loudest = 0.0;

    for (int index = 0; index < processorPresetCount(); ++index)
    {
        XYBassProcessor processor;
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
        processor.setCurrentProgram (index);

        juce::AudioBuffer<float> buffer (source);
        juce::MidiBuffer midi;
        float* pointers[2] = { nullptr, nullptr };

        for (int start = 0; start + blockSize <= length; start += blockSize)
        {
            for (int channel = 0; channel < 2; ++channel)
                pointers[channel] = buffer.getWritePointer (channel) + start;

            juce::AudioBuffer<float> view (pointers, 2, blockSize);
            processor.processBlock (view, midi);
        }

        double peak = 0.0;
        double sum = 0.0;
        const auto analysed = length / 2;

        for (int i = analysed; i < length; ++i)
        {
            const auto value = (double) buffer.getSample (0, i);
            peak = juce::jmax (peak, std::abs (value));
            sum += value * value;
        }

        const auto level = std::sqrt (sum / (double) (length - analysed));
        quietest = juce::jmin (quietest, level);
        loudest = juce::jmax (loudest, level);

        check (peak < 1.05, juce::String ("preset ") + processor.getProgramName (index) + " respects the ceiling");
        check (level > 0.01, juce::String ("preset ") + processor.getProgramName (index) + " produces output");
        check (std::isfinite (level), juce::String ("preset ") + processor.getProgramName (index) + " stays finite");

        processor.releaseResources();
    }

    std::cout << "        preset level spread = "
              << juce::String (20.0 * std::log10 (loudest / juce::jmax (quietest, 1.0e-9)), 2) << std::endl;

    check (20.0 * std::log10 (loudest / juce::jmax (quietest, 1.0e-9)) < 9.0,
           "the factory presets sit within a comparable loudness range");
}

} // namespace

int runPluginTests()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    testParameterLayout();
    testBusLayouts();
    testStateRoundTrip();
    testPresetRecall();
    testProcessingContract();
    testVariableBlockSizes();
    testFactoryPresets();
    testEditorLifecycle();

    std::cout << std::endl
              << (failures == 0 ? "PASSED " : "FAILED ")
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
