#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectrogramComponent : public juce::Component, public juce::Timer
{
public:
    SpectrogramComponent(SteganographyProcessor& p);
    ~SpectrogramComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    void drawNextLineOfSpectrogram();
    juce::Colour getDryColor(float amplitude) const;
    juce::Colour getWetColor(float amplitude) const;

    SteganographyProcessor& processor;
    
    // FFT Constants
    static constexpr auto fftOrder = 12; // 4096 size for very high frequency resolution
    static constexpr auto fftSize = 1 << fftOrder;
    
    juce::dsp::FFT forwardFFT { fftOrder };
    juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann };
    
    std::vector<float> audioHistory = std::vector<float>(65536, 0.0f);
    std::vector<float> fftDataDry = std::vector<float>((size_t)fftSize * 2, 0.0f);
    std::vector<float> fftDataWet = std::vector<float>((size_t)fftSize * 2, 0.0f);
    int historyIndex = 0;
    
    int samplesSinceLastColumn = 0;
    int samplesPerColumn = 128; // ~344 columns per second at 44.1kHz (fast scrolling)
    
    juce::Image spectrogramImage;
    int nextDrawX = 0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrogramComponent)
};
