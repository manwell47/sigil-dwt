#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>

namespace steganography
{

/**
 * @class WaveletProcessor
 * @brief RT-Safe SIMD-optimized Daubechies 4 (Db4) DWT/IDWT Processor.
 * 
 * Clean Room implementation inspired by rafat/wavelib (BSD-3), refactored to C++17.
 * All memory is pre-allocated in prepareToPlay. processBlock is 100% allocation-free.
 */
class WaveletProcessor
{
public:
    WaveletProcessor();
    ~WaveletProcessor() = default;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock, int numChannels);
    
    // Executes DWT, separating into Approx (low) and Detail (high) subbands
    void processDWT(const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& approx, juce::AudioBuffer<float>& detail);
    
    // Executes IDWT, synthesizing from Approx and Detail back into an audio buffer
    void processIDWT(const juce::AudioBuffer<float>& approx, const juce::AudioBuffer<float>& detail, juce::AudioBuffer<float>& output);

private:
    // Db4 Filter Coefficients (8 taps)
    static constexpr int FilterLength = 8;
    std::array<float, FilterLength> h_dec; // Low-pass decomposition
    std::array<float, FilterLength> g_dec; // High-pass decomposition
    std::array<float, FilterLength> h_rec; // Low-pass reconstruction
    std::array<float, FilterLength> g_rec; // High-pass reconstruction

    // Pre-allocated state buffers to avoid allocations in audio thread
    std::vector<std::vector<float>> state_dec; 
    std::vector<std::vector<float>> state_rec_approx;
    std::vector<std::vector<float>> state_rec_detail;
    
    int maxSamples = 0;
    int channels = 0;
    
    // SIMD-optimized convolution for a single block
    void convoluteAndDownsample(const float* input, float* approxOut, float* detailOut, int numSamples, int channel);
    void upsampleAndConvolute(const float* approxIn, const float* detailIn, float* output, int numSamples, int channel);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveletProcessor)
};

} // namespace steganography
