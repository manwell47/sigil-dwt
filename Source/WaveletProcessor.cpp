#include "WaveletProcessor.h"
#include <cmath>
#include <algorithm>

namespace steganography
{

WaveletProcessor::WaveletProcessor()
{
    // Db4 Decomposition Low-Pass (h_dec)
    h_dec = { 0.230377813308855f,  0.714846570552542f,  0.630880767929590f, -0.027983769416984f,
             -0.187034811718881f,  0.030841381835987f,  0.032883011666983f, -0.010597401784997f };

    // Db4 Decomposition High-Pass (g_dec) - QMF property: g[n] = (-1)^n * h[N-1-n]
    for (int i = 0; i < FilterLength; ++i) {
        g_dec[i] = (i % 2 == 0 ? 1.0f : -1.0f) * h_dec[FilterLength - 1 - i];
    }

    // Db4 Reconstruction Low-Pass (h_rec) - h_rec[n] = h_dec[N-1-n]
    for (int i = 0; i < FilterLength; ++i) {
        h_rec[i] = h_dec[FilterLength - 1 - i];
    }

    // Db4 Reconstruction High-Pass (g_rec) - g_rec[n] = g_dec[N-1-n]
    for (int i = 0; i < FilterLength; ++i) {
        g_rec[i] = g_dec[FilterLength - 1 - i];
    }
}

void WaveletProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock, int numChannels)
{
    juce::ignoreUnused(sampleRate);
    
    maxSamples = maximumExpectedSamplesPerBlock;
    channels = numChannels;

    // Pre-allocate state vectors to ensure RT-Safe execution (no allocations in processBlock)
    // We need (FilterLength - 1) history samples for continuous convolution
    state_dec.assign(channels, std::vector<float>(FilterLength - 1, 0.0f));
    state_rec_approx.assign(channels, std::vector<float>(FilterLength - 1, 0.0f));
    state_rec_detail.assign(channels, std::vector<float>(FilterLength - 1, 0.0f));
}

void WaveletProcessor::processDWT(const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& approx, juce::AudioBuffer<float>& detail)
{
    jassert(input.getNumSamples() <= maxSamples);
    int numSamples = input.getNumSamples();
    
    // Ensure output buffers have the correct size (half the input size due to decimation)
    int halfSize = numSamples / 2;
    approx.setSize(input.getNumChannels(), halfSize, false, false, true);
    detail.setSize(input.getNumChannels(), halfSize, false, false, true);

    for (int ch = 0; ch < input.getNumChannels(); ++ch)
    {
        convoluteAndDownsample(input.getReadPointer(ch), approx.getWritePointer(ch), detail.getWritePointer(ch), numSamples, ch);
    }
}

void WaveletProcessor::processIDWT(const juce::AudioBuffer<float>& approx, const juce::AudioBuffer<float>& detail, juce::AudioBuffer<float>& output)
{
    int halfSize = approx.getNumSamples();
    int numSamples = halfSize * 2;
    
    output.setSize(approx.getNumChannels(), numSamples, false, false, true);
    
    for (int ch = 0; ch < approx.getNumChannels(); ++ch)
    {
        upsampleAndConvolute(approx.getReadPointer(ch), detail.getReadPointer(ch), output.getWritePointer(ch), numSamples, ch);
    }
}

void WaveletProcessor::convoluteAndDownsample(const float* input, float* approxOut, float* detailOut, int numSamples, int channel)
{
    // C++17 RT-Safe Convolution with decimation by 2
    auto& state = state_dec[channel];
    
    // For vectorization: compilers (MSVC AVX2 / Clang) can unroll the inner loop 
    // because FilterLength is a small compile-time constant (8).
    for (int i = 0; i < numSamples; i += 2)
    {
        float sumApprox = 0.0f;
        float sumDetail = 0.0f;
        
        // FIR Convolution
        for (int k = 0; k < FilterLength; ++k)
        {
            // Circular buffer logic or history checking
            float sample = 0.0f;
            int idx = i - k;
            
            if (idx >= 0)
                sample = input[idx];
            else
                sample = state[state.size() + idx]; // Access history
            
            sumApprox += sample * h_dec[k];
            sumDetail += sample * g_dec[k];
        }
        
        approxOut[i / 2] = sumApprox;
        detailOut[i / 2] = sumDetail;
    }
    
    // Update state for next block
    if (numSamples >= FilterLength - 1)
    {
        for (int k = 0; k < FilterLength - 1; ++k) {
            state[k] = input[numSamples - (FilterLength - 1) + k];
        }
    }
    else
    {
        // Handle small blocks (shift state) - rare in standard DAWs but safe
        std::rotate(state.begin(), state.end() - numSamples, state.end());
        for (int k = 0; k < numSamples; ++k) {
            state[state.size() - numSamples + k] = input[k];
        }
    }
}

void WaveletProcessor::upsampleAndConvolute(const float* approxIn, const float* detailIn, float* output, int numSamples, int channel)
{
    auto& state_approx = state_rec_approx[channel];
    auto& state_detail = state_rec_detail[channel];

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;
        
        for (int k = 0; k < FilterLength; ++k)
        {
            float approxSample = 0.0f;
            float detailSample = 0.0f;
            
            // Upsampling: insert zeros between samples
            int idx = i - k;
            if (idx % 2 == 0) // Only non-zero samples (even indices after zero-padding logic relative to output)
            {
                int inputIdx = idx / 2;
                if (inputIdx >= 0 && inputIdx < numSamples / 2)
                {
                    approxSample = approxIn[inputIdx];
                    detailSample = detailIn[inputIdx];
                }
                else if (inputIdx < 0)
                {
                    int stateIdx = state_approx.size() + inputIdx;
                    approxSample = state_approx[stateIdx];
                    detailSample = state_detail[stateIdx];
                }
            }
            
            sum += approxSample * h_rec[k] + detailSample * g_rec[k];
        }
        
        output[i] = sum;
    }
    
    // Update state
    int halfSize = numSamples / 2;
    if (halfSize >= FilterLength - 1)
    {
        for (int k = 0; k < FilterLength - 1; ++k) {
            state_approx[k] = approxIn[halfSize - (FilterLength - 1) + k];
            state_detail[k] = detailIn[halfSize - (FilterLength - 1) + k];
        }
    }
    else
    {
        std::rotate(state_approx.begin(), state_approx.end() - halfSize, state_approx.end());
        std::rotate(state_detail.begin(), state_detail.end() - halfSize, state_detail.end());
        for (int k = 0; k < halfSize; ++k) {
            state_approx[state_approx.size() - halfSize + k] = approxIn[k];
            state_detail[state_detail.size() - halfSize + k] = detailIn[k];
        }
    }
}

} // namespace steganography
