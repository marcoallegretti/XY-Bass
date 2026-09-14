#pragma once

#include "BassEngine.h"
#include "PluginParameters.h"
#include "Presets.h"

class XYBassProcessor : public juce::AudioProcessor
{
public:
    XYBassProcessor();
    ~XYBassProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.25; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }
    const xyb::EngineMeters& getMeters() const noexcept { return engine.getMeters(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pullParameters();

    juce::AudioProcessorValueTreeState parameters;
    xyb::BassEngine engine;

    juce::AudioParameterFloat* xParameter = nullptr;
    juce::AudioParameterFloat* yParameter = nullptr;
    juce::AudioParameterFloat* inputParameter = nullptr;
    juce::AudioParameterFloat* outputParameter = nullptr;
    juce::AudioParameterFloat* mixParameter = nullptr;
    juce::AudioParameterBool* autoGainParameter = nullptr;
    juce::AudioParameterBool* deltaParameter = nullptr;
    juce::AudioParameterBool* bypassParameter = nullptr;

    juce::AudioBuffer<float> bypassBuffer;
    juce::SmoothedValue<float> bypassRamp;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XYBassProcessor)
};
