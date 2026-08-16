#pragma once

#include <JuceHeader.h>
#include "WaveletProcessor.h"
#include "LockFreeBridge.h"
#include "OnnxWrapper.h"

class SteganographyProcessor  : public juce::AudioProcessor
{
public:
    SteganographyProcessor();
    ~SteganographyProcessor() override;

    // APVTS (Parameters)
    juce::AudioProcessorValueTreeState treeState;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int index) override { juce::ignoreUnused(index); }
    const juce::String getProgramName (int index) override { juce::ignoreUnused(index); return {}; }
    void changeProgramName (int index, const juce::String& newName) override { juce::ignoreUnused(index, newName); }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    // ML Interaction
    steganography::LockFreeBridge maskBridge;
    std::atomic<bool> hasValidMask { false };
    
    // Call this from the Worker Thread
    void processImageInference(const juce::Image& image);
    
    // Spectrogram FIFO for the UI (Interleaved: Dry, Wet, Dry, Wet)
    juce::AbstractFifo spectrogramFifo { 65536 };
    std::vector<float> spectrogramData = std::vector<float>(65536, 0.0f);
    std::vector<float> spectrogramPushBuffer;

private:
    steganography::WaveletProcessor waveletProcessor;
    steganography::OnnxWrapper onnxWrapper;
    
    // DSP Buffers
    juce::AudioBuffer<float> approxBuffer;
    juce::AudioBuffer<float> detailBuffer;
    
    // Brickwall Limiter
    juce::dsp::Limiter<float> limiter;
    
    // Spectrogram Synthesis State
    std::array<float, 320> phases = { 0.0f };
    juce::LinearSmoothedValue<float> smoothedTargetFreq { 17000.0f };
    juce::LinearSmoothedValue<float> smoothedIntensity { 0.1f };

    // Injection State (Pacing & Blanking)
    int columnAdvanceCounter = 0;
    int currentColumn = 0;
    
    enum class InjectionState { Injecting, Blanking };
    InjectionState injectionState = InjectionState::Injecting;
    int stateCounter = 0;
    int getSamplesPerColumn() const;
    int getBlankingSamples() const;
    
    // Concurrency / Async Image Processing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteganographyProcessor)
};
