#include "SpectrogramComponent.h"

SpectrogramComponent::SpectrogramComponent(SteganographyProcessor& p)
    : processor(p),
      // Use 1000 pixels wide for high-res history, 512 height
      spectrogramImage(juce::Image::RGB, 1000, 512, true) 
{
    setOpaque(true);
    spectrogramImage.clear(spectrogramImage.getBounds(), juce::Colour(0xff121212));
    startTimerHz(60); 
}

SpectrogramComponent::~SpectrogramComponent()
{
    stopTimer();
}

void SpectrogramComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff202020)); // Background (should mostly be covered by the image)

    juce::Path clipPath;
    clipPath.addRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    g.reduceClipRegion(clipPath);

    int compW = getWidth();
    int compH = getHeight();
    int imgW = spectrogramImage.getWidth();
    int imgH = spectrogramImage.getHeight();
    
    // We want the newest data (at nextDrawX) to always be at the far right edge of the component.
    // So we draw exactly 'compW' columns from the image, ending at nextDrawX.
    if (nextDrawX >= compW)
    {
        // No wrap-around needed for the visible portion
        g.drawImage(spectrogramImage,
                    0, 0, compW, compH,
                    nextDrawX - compW, 0, compW, imgH);
    }
    else
    {
        // The visible portion wraps around the edges of the circular image buffer
        int widthFromRightEdge = compW - nextDrawX;
        
        // Oldest visible part (comes from the right edge of the image buffer)
        g.drawImage(spectrogramImage,
                    0, 0, widthFromRightEdge, compH,
                    imgW - widthFromRightEdge, 0, widthFromRightEdge, imgH);
                    
        // Newest visible part (comes from the left edge of the image buffer up to nextDrawX)
        if (nextDrawX > 0)
        {
            g.drawImage(spectrogramImage,
                        widthFromRightEdge, 0, nextDrawX, compH,
                        0, 0, nextDrawX, imgH);
        }
    }
    
    g.setColour(juce::Colours::grey);
    g.drawRect(getLocalBounds(), 1);
    
    // Draw Frequency Guidelines (17kHz and 22kHz)
    // We cap the display at 24kHz so the graffiti is large even at 96kHz+ sample rates.
    float maxDisplayedFreq = 24000.0f;
    
    float targetFreq = processor.treeState.getRawParameterValue("targetFreq")->load();
    float topFreq = targetFreq + 5000.0f;
    
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    float yBot = getHeight() * (1.0f - (targetFreq / maxDisplayedFreq));
    float yTop = getHeight() * (1.0f - (topFreq / maxDisplayedFreq));
    
    g.drawLine(0, yBot, getWidth(), yBot, 1.0f);
    g.drawLine(0, yTop, getWidth(), yTop, 1.0f);
    
    // Draw Sub-lines for 18, 19, 20, 21 kHz
    for (int f = (int)targetFreq + 1000; f < (int)topFreq; f += 1000)
    {
        float yf = getHeight() * (1.0f - ((float)f / maxDisplayedFreq));
        g.drawLine(0, yf, getWidth(), yf, 1.0f);
    }
    
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.setFont(12.0f);
    g.drawText(juce::String(topFreq / 1000.0f, 1) + " kHz", 5, (int)yTop - 15, 60, 15, juce::Justification::bottomLeft, false);
    g.drawText(juce::String(targetFreq / 1000.0f, 1) + " kHz", 5, (int)yBot + 2, 60, 15, juce::Justification::topLeft, false);
    
    // Draw Time Scale on X-axis
    if (samplesPerColumn > 0)
    {
        float sampleRate = processor.getSampleRate();
        if (sampleRate <= 0.0f) sampleRate = 44100.0f;
        
        float colsPerSec = sampleRate / (float)samplesPerColumn;
        
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.setFont(10.0f);
        
        int stepSeconds = 1;
        if (colsPerSec * stepSeconds < 40.0f) stepSeconds = 2;
        if (colsPerSec * stepSeconds < 40.0f) stepSeconds = 5;
        if (colsPerSec * stepSeconds < 40.0f) stepSeconds = 10;
        if (colsPerSec * stepSeconds < 40.0f) stepSeconds = 30;
        
        int xPos = compW;
        int seconds = 0;
        
        while (xPos > 0)
        {
            if (seconds > 0) // Don't draw 0s exactly on the edge
            {
                g.drawLine((float)xPos, (float)compH - 5.0f, (float)xPos, (float)compH, 1.0f);
                g.drawText("-" + juce::String(seconds) + "s", xPos - 20, compH - 20, 40, 15, juce::Justification::centredBottom, false);
            }
            xPos -= (int)(colsPerSec * stepSeconds);
            seconds += stepSeconds;
        }
    }
}

void SpectrogramComponent::resized()
{
}

void SpectrogramComponent::timerCallback()
{
    int numReady = processor.spectrogramFifo.getNumReady();
    if (numReady <= 0) return;

    // Dynamically calculate scrolling speed to guarantee 1:1 Aspect Ratio!
    // The graffiti spans whatever 'duration' the user set in APVTS.
    float targetDuration = processor.treeState.getRawParameterValue("duration")->load();
    float maxDisplayedFreq = 24000.0f;
    
    // We must use the component's height on the screen, not the image height!
    // The image gets vertically squashed (from 512 to compH) but X is mapped 1:1.
    float h = (float)getHeight();
    
    // Calculate the height of the 5kHz band in SCREEN pixels
    float h_screen = h * (5000.0f) / maxDisplayedFreq;
    if (h_screen < 1.0f) h_screen = 1.0f;
    
    // We want the width in SCREEN pixels to equal the height in SCREEN pixels (1:1 aspect ratio).
    // Since X is mapped 1:1 from image to screen, we scroll 'h_screen' pixels over 'targetDuration' seconds.
    float columnsPerSecond = h_screen / targetDuration;
    
    float sampleRate = processor.getSampleRate();
    if (sampleRate <= 0.0f) sampleRate = 44100.0f;
    
    samplesPerColumn = (int)(sampleRate / columnsPerSecond);
    if (samplesPerColumn < 1) samplesPerColumn = 1;

    int start1, block1, start2, block2;
    processor.spectrogramFifo.prepareToRead(numReady, start1, block1, start2, block2);
    
    auto pushSamples = [&](int start, int block) {
        for (int i = 0; i < block; i += 2)
        {
            if (start + i + 1 >= processor.spectrogramData.size()) break; // Safety
            
            float dry = processor.spectrogramData[(size_t)(start + i)];
            float wet = processor.spectrogramData[(size_t)(start + i + 1)];
            
            audioHistory[(size_t)historyIndex] = dry;
            audioHistory[(size_t)(historyIndex + 1)] = wet;
            historyIndex = (historyIndex + 2) % 65536;
            
            samplesSinceLastColumn++;
            if (samplesSinceLastColumn >= samplesPerColumn)
            {
                drawNextLineOfSpectrogram();
                samplesSinceLastColumn = 0;
            }
        }
    };
    
    if (block1 > 0) pushSamples(start1, block1);
    if (block2 > 0) pushSamples(start2, block2);
    
    processor.spectrogramFifo.finishedRead(block1 + block2);
}

void SpectrogramComponent::drawNextLineOfSpectrogram()
{
    // Extract the latest fftSize pairs into the first half of fftDataDry/Wet
    for (int i = 0; i < fftSize; ++i)
    {
        int readIdx = (historyIndex - (fftSize * 2) + (i * 2) + 65536) % 65536;
        fftDataDry[(size_t)i] = audioHistory[(size_t)readIdx];
        fftDataWet[(size_t)i] = audioHistory[(size_t)(readIdx + 1)];
    }
    std::fill(fftDataDry.begin() + fftSize, fftDataDry.end(), 0.0f);
    std::fill(fftDataWet.begin() + fftSize, fftDataWet.end(), 0.0f);
    
    // Apply window to the real signal
    window.multiplyWithWindowingTable(fftDataDry.data(), fftSize);
    window.multiplyWithWindowingTable(fftDataWet.data(), fftSize);
    
    // Perform Real-to-Complex FFT. The result is stored as interleaved Re/Im pairs.
    forwardFFT.performRealOnlyForwardTransform(fftDataDry.data(), true);
    forwardFFT.performRealOnlyForwardTransform(fftDataWet.data(), true);
    
    int h = spectrogramImage.getHeight();
    int numBins = fftSize / 2;
    
    float sampleRate = processor.getSampleRate();
    if (sampleRate <= 0) sampleRate = 44100.0f;
    
    float maxDisplayedFreq = 24000.0f;
    float binResolution = sampleRate / fftSize;
    
    for (int y = 0; y < h; ++y)
    {
        // Map Y pixel to frequency (linear)
        float proportion = 1.0f - ((float)y / (float)h);
        float freq = proportion * maxDisplayedFreq;
        
        // Map frequency to bin index
        float binIndexFloat = freq / binResolution;
        int binIndex = juce::jlimit(0, numBins - 1, (int)binIndexFloat);
        
        // Calculate magnitudes
        float reDry = fftDataDry[(size_t)(binIndex * 2)];
        float imDry = fftDataDry[(size_t)(binIndex * 2 + 1)];
        float magDry = std::sqrt(reDry * reDry + imDry * imDry);
        
        float reWet = fftDataWet[(size_t)(binIndex * 2)];
        float imWet = fftDataWet[(size_t)(binIndex * 2 + 1)];
        float magWet = std::sqrt(reWet * reWet + imWet * imWet);
        
        // Convert to Decibels
        float normDry = magDry / (fftSize / 2.0f);
        float normWet = (magWet * 30.0f) / (fftSize / 2.0f); // Boost Wet for visibility
        
        float dbDry = 20.0f * std::log10(normDry + 1e-6f);
        float dbWet = 20.0f * std::log10(normWet + 1e-6f);
        
        // Map dB to 0.0 - 1.0 scale
        float minDb = -85.0f;
        float maxDb = 0.0f;
        float levelDry = juce::jlimit(0.0f, 1.0f, (dbDry - minDb) / (maxDb - minDb));
        float levelWet = juce::jlimit(0.0f, 1.0f, (dbWet - minDb) / (maxDb - minDb));
        
        juce::Colour colorDry = getDryColor(levelDry);
        juce::Colour colorWet = getWetColor(levelWet);
        
        // Additive Blend
        juce::Colour finalColor(
            (juce::uint8)juce::jlimit(0, 255, colorDry.getRed() + colorWet.getRed()),
            (juce::uint8)juce::jlimit(0, 255, colorDry.getGreen() + colorWet.getGreen()),
            (juce::uint8)juce::jlimit(0, 255, colorDry.getBlue() + colorWet.getBlue())
        );
        
        spectrogramImage.setPixelAt(nextDrawX, y, finalColor);
    }
    
    nextDrawX = (nextDrawX + 1) % spectrogramImage.getWidth();
    repaint();
}

juce::Colour SpectrogramComponent::getDryColor(float amplitude) const
{
    // Neutral Grey/Blue
    if (amplitude <= 0.05f) return juce::Colour(0xff121212);
    return juce::Colour(0xff121212).interpolatedWith(juce::Colour(0xff6e829c), amplitude);
}

juce::Colour SpectrogramComponent::getWetColor(float amplitude) const
{
    // Vibrant Purple to Orange
    if (amplitude <= 0.05f) return juce::Colours::transparentBlack;
    if (amplitude < 0.5f)
        return juce::Colour(0x00000000).interpolatedWith(juce::Colour(0xffb51bc9), amplitude * 2.0f);
    return juce::Colour(0xffb51bc9).interpolatedWith(juce::Colours::darkorange, (amplitude - 0.5f) * 2.0f);
}
