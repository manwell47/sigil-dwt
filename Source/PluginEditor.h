#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SpectrogramComponent.h"
#include "PremiumLookAndFeel.h"

// Forward declaration
class SteganographyProcessor;

/**
 * @class MLInferenceJob
 * @brief juce::ThreadPoolJob that runs the ONNX inference on a background thread.
 */
class MLInferenceJob : public juce::ThreadPoolJob
{
public:
    MLInferenceJob(SteganographyProcessor& p, juce::Image img)
        : ThreadPoolJob("ONNX Inference Job"), processor(p), image(img)
    {
    }

    JobStatus runJob() override
    {
        processor.processImageInference(image);
        return jobHasFinished;
    }

private:
    SteganographyProcessor& processor;
    juce::Image image;
};

/**
 * @class ManualOverlay
 * @brief Overlay component for manual/instructions.
 */
class ManualOverlay : public juce::Component
{
public:
    ManualOverlay();
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    juce::TextButton closeButton;
    juce::TextEditor manualText;
};

/**
 * @class CanvasComponent
 * @brief Custom component for drag & drop and spectrogram preview.
 */
class CanvasComponent : public juce::Component, public juce::FileDragAndDropTarget, public juce::Timer
{
public:
    CanvasComponent(SteganographyProcessor& p);
    ~CanvasComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    SteganographyProcessor& processor;
    juce::Image currentImage;
    juce::ThreadPool threadPool;
    
    bool isProcessingML = false;
    bool isDragActive = false;
    float loadingAngle = 0.0f;
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CanvasComponent)
};

/**
 * @class SteganographyEditor
 * @brief Main VST3 Editor window.
 */
class SteganographyEditor  : public juce::AudioProcessorEditor
{
public:
    SteganographyEditor (SteganographyProcessor&);
    ~SteganographyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SteganographyProcessor& audioProcessor;
    CanvasComponent canvas;
    SpectrogramComponent spectrogram;
    juce::TextButton aboutButton{"About / Licenses"};
    
    // UI Controls
    juce::Slider intensitySlider;
    juce::Slider targetFreqSlider;
    juce::Slider durationSlider;
    juce::Slider blankingSlider;
    
    juce::Label intensityLabel;
    juce::Label targetFreqLabel;
    juce::Label durationLabel;
    juce::Label blankingLabel;
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> intensityAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> targetFreqAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> durationAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blankingAttach;
    
    juce::ComboBox presetComboBox;
    juce::TextButton offlineRenderButton{"Offline Render..."};
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    juce::DropShadowEffect shadowEffect;
    
    PremiumLookAndFeel premiumLookAndFeel;
    
    ManualOverlay manualOverlay;
    
    void startOfflineRender();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteganographyEditor)
};
