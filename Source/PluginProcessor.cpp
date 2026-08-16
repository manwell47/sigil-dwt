#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

SteganographyProcessor::SteganographyProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ), treeState(*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
    // The mask size depends on the neural network output (U2-Net is 320x320 = 102400)
    maskBridge.allocate(320 * 320);
    
    // Load ONNX model from BinaryData
    onnxWrapper.loadModel(BinaryData::model_onnx, BinaryData::model_onnxSize);
}

juce::AudioProcessorValueTreeState::ParameterLayout SteganographyProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>("intensity", "Intensity", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("targetFreq", "Target Freq", 15000.0f, 20000.0f, 17000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("duration", "Duration (s)", 1.0f, 10.0f, 4.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("blanking", "Blanking (s)", 0.0f, 10.0f, 6.0f));

    return { params.begin(), params.end() };
}

int SteganographyProcessor::getSamplesPerColumn() const
{
    float duration = treeState.getRawParameterValue("duration")->load();
    int samplesPerColumn = (int)((getSampleRate() * duration) / 320.0f);
    return std::max(1, samplesPerColumn);
}

int SteganographyProcessor::getBlankingSamples() const
{
    float blanking = treeState.getRawParameterValue("blanking")->load();
    return (int)(getSampleRate() * blanking);
}

SteganographyProcessor::~SteganographyProcessor()
{
}

void SteganographyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Initialize DSP Modules
    waveletProcessor.prepareToPlay(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();
    
    limiter.prepare(spec);
    limiter.setThreshold(-0.5f); // Brickwall limit slightly below 0dB
    limiter.setRelease(10.0f);
    
    spectrogramPushBuffer.resize(samplesPerBlock * 2, 0.0f);
    
    // Setup Oscillator Bank for Spectrogram Synthesis
    float targetFreq = treeState.getRawParameterValue("targetFreq")->load();
    smoothedTargetFreq.reset(sampleRate, 0.05); // 50ms smooth
    smoothedTargetFreq.setCurrentAndTargetValue(targetFreq);
    
    smoothedIntensity.reset(sampleRate, 0.05);
    smoothedIntensity.setCurrentAndTargetValue(treeState.getRawParameterValue("intensity")->load());
    
    for (int y = 0; y < 320; ++y)
    {
        phases[y] = juce::Random::getSystemRandom().nextFloat() * juce::MathConstants<float>::twoPi;
    }
    
    currentColumn = 0;
    columnAdvanceCounter = 0;
    stateCounter = 0;
    injectionState = InjectionState::Injecting;
}

void SteganographyProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SteganographyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    #endif

    return true;
  #endif
}
#endif

void SteganographyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // 1. DWT Analysis
    waveletProcessor.processDWT(buffer, approxBuffer, detailBuffer);
    
    // Get High-Frequency Energy envelope for Steganographic Modulation
    float detailEnergy = 0.0f;
    if (detailBuffer.getNumChannels() > 0) {
        auto* detailRead = detailBuffer.getReadPointer(0);
        for (int i = 0; i < detailBuffer.getNumSamples(); ++i) {
            detailEnergy += std::abs(detailRead[i]);
        }
        detailEnergy /= detailBuffer.getNumSamples();
    }
    float envelope = detailEnergy + 0.05f; // floor

    // 2. IDWT Synthesis
    // Reconstruct the audio perfectly (no aliasing from image injection)
    waveletProcessor.processIDWT(approxBuffer, detailBuffer, buffer);
    
    // Compensate for Orthogonal Wavelet Perfect Reconstruction gain (+6dB)
    buffer.applyGain(0.5f);
    
    // 3. Image Injection (Full Sample Rate)
    // Synthesize the image at the native sample rate and add directly to the final buffer.
    // This entirely bypasses the Db4 aliasing issue.
    const auto& mask = maskBridge.getReadBuffer();
    int numSamples = buffer.getNumSamples();
    
    // Pacing (Time-Stretching) dinámico e independiente del tamaño de bloque.
    int samplesPerColumn = getSamplesPerColumn();
    float muStep = 1.0f / (float)samplesPerColumn;
    
    smoothedTargetFreq.setTargetValue(treeState.getRawParameterValue("targetFreq")->load());
    smoothedIntensity.setTargetValue(treeState.getRawParameterValue("intensity")->load());
    
    for (int i = 0; i < numSamples; ++i)
    {
        float drySample = buffer.getSample(0, i);
        float wetSample = 0.0f;
        
        float currentTargetFreq = smoothedTargetFreq.getNextValue();
        float currentIntensity = smoothedIntensity.getNextValue();
        
        if (!mask.empty())
        {
            if (injectionState == InjectionState::Injecting)
            {
                if (++columnAdvanceCounter >= samplesPerColumn)
                {
                    columnAdvanceCounter = 0;
                    currentColumn++;
                    if (currentColumn >= 320)
                    {
                        currentColumn = 0;
                        injectionState = InjectionState::Blanking;
                        stateCounter = 0;
                    }
                }

                if (injectionState == InjectionState::Injecting)
                {
                    int nextColumn = (currentColumn + 1) % 320;
                    float mu = columnAdvanceCounter * muStep;
                    
                    float synth = 0.0f;
                    float activePower = 0.0f;
                    
                    for (int y = 0; y < 320; ++y)
                    {
                        float p1 = mask[y * 320 + currentColumn];
                        float p2 = mask[y * 320 + nextColumn];
                        float pixel = p1 + mu * (p2 - p1);
                        
                        if (pixel > 0.01f)
                        {
                            synth += pixel * std::sin(phases[y]);
                            activePower += pixel * pixel;
                        }
                        
                        // Advance phases dynamically based on TargetFreq
                        float finalFreq = currentTargetFreq + 5000.0f - (5000.0f * y / 319.0f);
                        float phaseInc = juce::MathConstants<float>::twoPi * finalFreq / getSampleRate();
                        phases[y] += phaseInc;
                        if (phases[y] >= juce::MathConstants<float>::twoPi)
                            phases[y] -= juce::MathConstants<float>::twoPi;
                    }
                    
                    if (activePower > 1.0f) {
                        synth = synth / std::sqrt(activePower);
                    }
                    
                    // We multiply by 0.5f to give some headroom, and then by the APVTS intensity
                    wetSample = synth * envelope * currentIntensity * 0.5f;
                }
            }
            else if (injectionState == InjectionState::Blanking)
            {
                // Blanking space (silence for the watermark)
                if (++stateCounter >= getBlankingSamples())
                {
                    stateCounter = 0;
                    injectionState = InjectionState::Injecting;
                }
                
                // Still advance phases to prevent discontinuities when resuming
                for (int y = 0; y < 320; ++y)
                {
                    float finalFreq = currentTargetFreq + 5000.0f - (5000.0f * y / 319.0f);
                    float phaseInc = juce::MathConstants<float>::twoPi * finalFreq / getSampleRate();
                    phases[y] += phaseInc;
                    if (phases[y] >= juce::MathConstants<float>::twoPi)
                        phases[y] -= juce::MathConstants<float>::twoPi;
                }
            }
        }
        
        // Additive energy injection + Envelope Modulation
        for (int ch = 0; ch < totalNumOutputChannels; ++ch)
        {
            auto* writePtr = buffer.getWritePointer(ch);
            writePtr[i] += wetSample;
        }
        
        spectrogramPushBuffer[(size_t)(i * 2)] = drySample;
        spectrogramPushBuffer[(size_t)(i * 2 + 1)] = wetSample;
    }
    
    // 4. Brickwall Limiter
    juce::dsp::AudioBlock<float> audioBlock(buffer);
    juce::dsp::ProcessContextReplacing<float> context(audioBlock);
    limiter.process(context);

    // 5. Push Interleaved Dry/Wet to Spectrogram FIFO
    if (numSamples > 0)
    {
        int freeSpace = spectrogramFifo.getFreeSpace();
        freeSpace &= ~1; // Ensure we only write complete pairs (even number of floats)
        
        int itemsToWrite = juce::jmin(numSamples * 2, freeSpace);
        if (itemsToWrite > 0)
        {
            int start1, block1, start2, block2;
            spectrogramFifo.prepareToWrite(itemsToWrite, start1, block1, start2, block2);
            
            if (block1 > 0)
                std::copy_n(spectrogramPushBuffer.data(), block1, spectrogramData.begin() + start1);
            if (block2 > 0)
                std::copy_n(spectrogramPushBuffer.data() + block1, block2, spectrogramData.begin() + start2);
                
            spectrogramFifo.finishedWrite(block1 + block2);
        }
    }
}

void SteganographyProcessor::processImageInference(const juce::Image& image)
{
    // Called by the Worker Thread
    auto mask = onnxWrapper.processImage(image);
    if (!mask.empty())
    {
        auto& writeBuffer = maskBridge.getWriteBuffer();
        if (writeBuffer.size() >= mask.size())
        {
            std::copy(mask.begin(), mask.end(), writeBuffer.begin());
            maskBridge.swapWriteBuffer();
            hasValidMask.store(true);
        }
    }
}

juce::AudioProcessorEditor* SteganographyProcessor::createEditor()
{
    return new SteganographyEditor (*this);
}

void SteganographyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ignoreUnused(destData);
}

void SteganographyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused(data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SteganographyProcessor();
}
