#include "PluginProcessor.h"
#include "PluginEditor.h"

class OfflineRenderThread : public juce::ThreadWithProgressWindow
{
public:
    OfflineRenderThread(SteganographyProcessor& p, const juce::File& in, const juce::File& out)
        : juce::ThreadWithProgressWindow("Rendering Offline...", true, true),
          processor(p), inFile(in), outFile(out)
    {
    }

    void run() override
    {
        setProgress(-1.0);
        setStatusMessage("Initializing Audio Formats...");

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(inFile));
        if (reader == nullptr)
        {
            juce::MessageManager::callAsync([]() {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Could not open input audio file.");
            });
            return;
        }

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::OutputStream> outStream(new juce::FileOutputStream(outFile));
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
            outStream,
            juce::AudioFormatWriterOptions()
                .withSampleRate(reader->sampleRate)
                .withNumChannels((int)reader->numChannels)
                .withBitsPerSample(32)
                .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)
        ));

        if (writer == nullptr)
        {
            juce::MessageManager::callAsync([]() {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Could not create output audio file.");
            });
            return;
        }

        // Create temporary processor and copy settings
        SteganographyProcessor tempProcessor;
        
        // Ensure the internal sample rate and block size are set properly so getSampleRate() returns the correct value
        tempProcessor.setPlayConfigDetails(reader->numChannels, reader->numChannels, reader->sampleRate, 512);
        
        // Call prepareToPlay FIRST, because it calls maskBridge.allocate() which erases the mask!
        tempProcessor.prepareToPlay(reader->sampleRate, 512);
        
        tempProcessor.treeState.getParameter("targetFreq")->setValue(processor.treeState.getParameter("targetFreq")->getValue());
        tempProcessor.treeState.getParameter("intensity")->setValue(processor.treeState.getParameter("intensity")->getValue());
        tempProcessor.treeState.getParameter("duration")->setValue(processor.treeState.getParameter("duration")->getValue());
        tempProcessor.treeState.getParameter("blanking")->setValue(processor.treeState.getParameter("blanking")->getValue());
        
        // Now it is safe to inject the mask
        tempProcessor.setMaskDirectly(processor.maskBridge.getReadBuffer());

        // Ensure the buffer matches the processor's output channels to avoid memory corruption
        int numOutChans = tempProcessor.getTotalNumOutputChannels();
        juce::AudioBuffer<float> buffer(juce::jmax((int)reader->numChannels, numOutChans), 512);
        juce::MidiBuffer midi;

        int64 totalSamples = reader->lengthInSamples;
        int64 samplesProcessed = 0;

        while (samplesProcessed < totalSamples)
        {
            if (threadShouldExit())
                break;

            int numSamples = (int)juce::jmin((int64)512, totalSamples - samplesProcessed);
            
            buffer.clear();
            reader->read(&buffer, 0, numSamples, samplesProcessed, true, true);

            // Ensure we process an EVEN number of samples because WaveletProcessor downsamples by 2!
            // If numSamples is odd, we process one extra sample of silence (which was cleared above)
            int processSamples = numSamples;
            if (processSamples % 2 != 0)
                processSamples++;

            // Create a sub-buffer of exactly processSamples to avoid processing trailing garbage
            juce::AudioBuffer<float> processBuffer(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), processSamples);
            tempProcessor.processBlock(processBuffer, midi);

            // Write ONLY the exact numSamples back to the file
            writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);

            samplesProcessed += numSamples;
            setProgress(samplesProcessed / (double)totalSamples);
            setStatusMessage("Rendering... " + juce::String(juce::roundToInt((samplesProcessed / (double)totalSamples) * 100.0)) + "%");
        }
        
        tempProcessor.releaseResources();
        
        if (threadShouldExit())
        {
            writer.reset();
            outFile.deleteFile();
        }
    }

private:
    SteganographyProcessor& processor;
    juce::File inFile;
    juce::File outFile;
};

//==============================================================================
ManualOverlay::ManualOverlay()
{
    closeButton.setButtonText("X");
    closeButton.onClick = [this] { setVisible(false); };
    addAndMakeVisible(closeButton);
    
    manualText.setMultiLine(true);
    manualText.setReturnKeyStartsNewLine(true);
    manualText.setReadOnly(true);
    manualText.setScrollbarsShown(true);
    manualText.setCaretVisible(false);
    manualText.setPopupMenuEnabled(true);
    manualText.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    manualText.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    manualText.setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    
    juce::String manualContent = 
        "DWT STEGANOGRAPHY - PRO MANUAL\n\n"
        "Welcome to the cutting-edge fusion of Discrete Wavelet Transforms (Daubechies 4) and Neural Network contour extraction.\n\n"
        "1. INGESTION:\n"
        "   Drag and drop any JPG or transparent PNG into the left canvas.\n\n"
        "2. PROCESSING:\n"
        "   The Dual Sobel filter analyzes Luminance & Alpha channels to extract high-precision contours, "
        "   preserving the topology of complex typography and images perfectly.\n\n"
        "3. CONTROLS:\n"
        "   - INTENSITY: Drives the amplitude of the injected oscillators. Higher intensity creates denser visuals but adds audible noise.\n"
        "   - FREQ (Hz): The ceiling frequency of the image in the spectrum. Keep between 16kHz - 22kHz for stealth.\n"
        "   - LENGTH (s): The duration of one complete image cycle.\n"
        "   - BLANKING (s): Silence between cycles to separate images.\n\n"
        "4. ROUTING:\n"
        "   For best results, use a spectrum analyzer downstream (like FabFilter Pro-Q3) with high resolution and tilt set to 0.0dB/oct.\n\n"
        "==========================================================\n"
        "LEGAL LICENSES & ATTRIBUTIONS\n"
        "==========================================================\n\n" +
        steganography::OnnxWrapper::getOnnxRuntimeLegalNotice();

    manualText.setFont(juce::Font("Inter", 15.0f, juce::Font::plain));
    manualText.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    manualText.setText(manualContent, false);
    
    addAndMakeVisible(manualText);
}

void ManualOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.7f));
    
    juce::Rectangle<float> box = getLocalBounds().reduced(50).toFloat();
    g.setColour(juce::Colour(0xff181818));
    g.fillRoundedRectangle(box, 10.0f);
    
    g.setColour(juce::Colour(0xff333333));
    g.drawRoundedRectangle(box, 10.0f, 2.0f);
}

void ManualOverlay::resized()
{
    auto box = getLocalBounds().reduced(50);
    closeButton.setBounds(box.getRight() - 40, box.getY() + 10, 30, 30);
    manualText.setBounds(box.reduced(30).withTrimmedTop(30));
}

//==============================================================================
CanvasComponent::CanvasComponent(SteganographyProcessor& p)
    : processor(p), threadPool(1) // 1 Worker Thread for ML inference
{
    startTimerHz(30); // 30 FPS for loading animation
}

CanvasComponent::~CanvasComponent()
{
    threadPool.removeAllJobs(true, 2000);
}

void CanvasComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181818)); // Slightly darker background

    // Draw the generated neural mask if available, else draw the loaded image
    const auto& mask = processor.maskBridge.getReadBuffer();
    if (processor.hasValidMask.load())
    {
        // Instead of drawing the raw image, we draw the 320x320 neural mask with our custom gradient
        juce::Image maskImage(juce::Image::ARGB, 320, 320, true);
        {
            juce::Image::BitmapData data(maskImage, juce::Image::BitmapData::writeOnly);
            
            juce::Colour colourStart(0xffb51bc9); // Purple
            juce::Colour colourEnd(juce::Colours::darkorange);
            
            for (int y = 0; y < 320; ++y)
            {
                for (int x = 0; x < 320; ++x)
                {
                    float val = mask[y * 320 + x];
                    if (val > 0.05f)
                    {
                        juce::Colour pixelCol = colourStart.interpolatedWith(colourEnd, y / 320.0f);
                        data.setPixelColour(x, y, pixelCol.withAlpha(val));
                    }
                }
            }
        }
        
        g.drawImageWithin(maskImage, 10, 10, getWidth() - 20, getHeight() - 20, juce::RectanglePlacement::centred, false);
    }
    else if (currentImage.isValid())
    {
        g.drawImageWithin(currentImage, 10, 10, getWidth() - 20, getHeight() - 20, juce::RectanglePlacement::centred, false);
    }
    else
    {
        float cx = getWidth() / 2.0f;
        float cy = getHeight() / 2.0f;
        
        // Premium Plus Icon (drawn with two rounded rectangles)
        g.setColour(juce::Colour(0xffc222ff).withAlpha(0.3f)); // Subtle Purple tint
        g.fillRoundedRectangle(cx - 18.0f, cy - 35.0f - 2.5f, 36.0f, 5.0f, 2.5f); // Horizontal
        g.fillRoundedRectangle(cx - 2.5f, cy - 35.0f - 18.0f, 5.0f, 36.0f, 2.5f); // Vertical
        
        // Premium Typography
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("Drag & Drop Image", 0, (int)cy - 5, getWidth(), 20, juce::Justification::centred, true);
        
        g.setColour(juce::Colours::grey.withAlpha(0.6f));
        g.setFont(juce::Font(14.0f, juce::Font::plain));
        g.drawText("or click to browse files", 0, (int)cy + 18, getWidth(), 20, juce::Justification::centred, true);
    }
    
    // Neural Network Loading Animation Overlay
    if (isProcessingML)
    {
        g.fillAll(juce::Colours::black.withAlpha(0.6f));
        
        float cx = getWidth() / 2.0f;
        float cy = getHeight() / 2.0f;
        
        // Cyber-scanner line moving up and down
        float scanY = 10.0f + std::fmod(loadingAngle * 50.0f, (float)getHeight() - 20.0f);
        
        juce::ColourGradient grad(juce::Colour(0x00c222ff), 0, scanY - 20,
                                  juce::Colour(0xffc222ff), 0, scanY, false);
        grad.addColour(1.0f, juce::Colour(0x00c222ff)); // Fade out below
        
        g.setGradientFill(grad);
        g.fillRect(10.0f, scanY - 20.0f, (float)getWidth() - 20.0f, 40.0f);
        
        g.setColour(juce::Colour(0xffc222ff));
        g.drawLine(10.0f, scanY, (float)getWidth() - 10.0f, scanY, 2.0f);
        
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText("U2-NET EXTRACTING MASK...", 0, (int)cy - 10, getWidth(), 20, juce::Justification::centred, false);
    }
    
    // Drag & Drop Border
    if (isDragActive)
    {
        float alpha = 0.5f + 0.5f * std::sin(loadingAngle * 10.0f);
        g.setColour(juce::Colour(0xffc222ff).withAlpha(alpha));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 8.0f, 4.0f);
    }
    else
    {
        g.setColour(juce::Colour(0xff333333));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 8.0f, 2.0f);
    }
}

void CanvasComponent::resized()
{
}

void CanvasComponent::timerCallback()
{
    bool isNowProcessing = (threadPool.getNumJobs() > 0);
    
    if (isProcessingML != isNowProcessing)
    {
        isProcessingML = isNowProcessing;
        repaint(); // Force repaint when state changes
    }
    
    if (isProcessingML)
    {
        loadingAngle += 0.2f; // Spin speed
        repaint();
    }
}

void CanvasComponent::mouseDown(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select an image to inject...",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.png;*.jpg;*.jpeg");
        
    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file.existsAsFile())
        {
            auto img = juce::ImageFileFormat::loadFrom(file);
            if (img.isValid())
            {
                currentImage = img;
                repaint();
                threadPool.addJob(new MLInferenceJob(processor, currentImage.createCopy()), true);
            }
        }
    });
}

bool CanvasComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto file : files)
    {
        if (file.endsWithIgnoreCase(".png") || file.endsWithIgnoreCase(".jpg") || file.endsWithIgnoreCase(".jpeg"))
            return true;
    }
    return false;
}

void CanvasComponent::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(files, x, y);
    isDragActive = true;
    repaint();
}

void CanvasComponent::fileDragExit(const juce::StringArray& files)
{
    juce::ignoreUnused(files);
    isDragActive = false;
    repaint();
}

void CanvasComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x, y);
    isDragActive = false;
    for (auto file : files)
    {
        if (file.endsWithIgnoreCase(".png") || file.endsWithIgnoreCase(".jpg") || file.endsWithIgnoreCase(".jpeg"))
        {
            auto img = juce::ImageFileFormat::loadFrom(juce::File(file));
            if (img.isValid())
            {
                currentImage = img;
                repaint();
                
                // Launch inference on Worker Thread to prevent Audio/UI blocking
                threadPool.addJob(new MLInferenceJob(processor, currentImage.createCopy()), true);
                break;
            }
        }
    }
}

//==============================================================================
SteganographyEditor::SteganographyEditor (SteganographyProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), canvas(p), spectrogram(p)
{
    addAndMakeVisible(canvas);
    addAndMakeVisible(spectrogram);
    addAndMakeVisible(aboutButton);

    if (audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        addAndMakeVisible(offlineRenderButton);
        offlineRenderButton.onClick = [this]() { startOfflineRender(); };
    }

    setLookAndFeel(&premiumLookAndFeel);

    shadowEffect.setShadowProperties({juce::Colours::black.withAlpha(0.6f), 15, juce::Point<int>(0, 8)});
    canvas.setComponentEffect(&shadowEffect);
    spectrogram.setComponentEffect(&shadowEffect);

    auto setupSlider = [this](juce::Slider& slider, juce::Label& label, const juce::String& text, const juce::String& paramID, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attach) {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffc222ff));
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::darkorange);
        addAndMakeVisible(slider);
        
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(12.0f);
        label.setColour(juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible(label);
        
        attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.treeState, paramID, slider);
    };

    setupSlider(intensitySlider, intensityLabel, "Intensity", "intensity", intensityAttach);
    setupSlider(targetFreqSlider, targetFreqLabel, "Freq (Hz)", "targetFreq", targetFreqAttach);
    setupSlider(durationSlider, durationLabel, "Length (s)", "duration", durationAttach);
    setupSlider(blankingSlider, blankingLabel, "Blanking (s)", "blanking", blankingAttach);

    aboutButton.onClick = [this]() {
        manualOverlay.setVisible(true);
    };

    addAndMakeVisible(manualOverlay);
    manualOverlay.setVisible(false);

    presetComboBox.addItem("Stealth Watermark (Invisible)", 1);
    presetComboBox.addItem("Balanced Print (Standard)", 2);
    presetComboBox.addItem("High Definition (Maximum Clarity)", 3);
    presetComboBox.addItem("Dense Fill / Creative", 4);
    
    presetComboBox.setJustificationType(juce::Justification::centred);
    presetComboBox.setTextWhenNothingSelected("Select Factory Preset...");
    addAndMakeVisible(presetComboBox);
    
    presetComboBox.onChange = [this]() {
        int id = presetComboBox.getSelectedId();
        if (id == 1) {
            audioProcessor.treeState.getParameter("targetFreq")->setValueNotifyingHost(audioProcessor.treeState.getParameter("targetFreq")->convertTo0to1(19000.0f));
            audioProcessor.treeState.getParameter("intensity")->setValueNotifyingHost(audioProcessor.treeState.getParameter("intensity")->convertTo0to1(0.15f));
            audioProcessor.treeState.getParameter("duration")->setValueNotifyingHost(audioProcessor.treeState.getParameter("duration")->convertTo0to1(8.0f));
            audioProcessor.treeState.getParameter("blanking")->setValueNotifyingHost(audioProcessor.treeState.getParameter("blanking")->convertTo0to1(6.0f));
        } else if (id == 2) {
            audioProcessor.treeState.getParameter("targetFreq")->setValueNotifyingHost(audioProcessor.treeState.getParameter("targetFreq")->convertTo0to1(17000.0f));
            audioProcessor.treeState.getParameter("intensity")->setValueNotifyingHost(audioProcessor.treeState.getParameter("intensity")->convertTo0to1(0.40f));
            audioProcessor.treeState.getParameter("duration")->setValueNotifyingHost(audioProcessor.treeState.getParameter("duration")->convertTo0to1(4.0f));
            audioProcessor.treeState.getParameter("blanking")->setValueNotifyingHost(audioProcessor.treeState.getParameter("blanking")->convertTo0to1(3.0f));
        } else if (id == 3) {
            audioProcessor.treeState.getParameter("targetFreq")->setValueNotifyingHost(audioProcessor.treeState.getParameter("targetFreq")->convertTo0to1(16000.0f));
            audioProcessor.treeState.getParameter("intensity")->setValueNotifyingHost(audioProcessor.treeState.getParameter("intensity")->convertTo0to1(0.75f));
            audioProcessor.treeState.getParameter("duration")->setValueNotifyingHost(audioProcessor.treeState.getParameter("duration")->convertTo0to1(6.0f));
            audioProcessor.treeState.getParameter("blanking")->setValueNotifyingHost(audioProcessor.treeState.getParameter("blanking")->convertTo0to1(4.0f));
        } else if (id == 4) {
            audioProcessor.treeState.getParameter("targetFreq")->setValueNotifyingHost(audioProcessor.treeState.getParameter("targetFreq")->convertTo0to1(15000.0f));
            audioProcessor.treeState.getParameter("intensity")->setValueNotifyingHost(audioProcessor.treeState.getParameter("intensity")->convertTo0to1(1.0f));
            audioProcessor.treeState.getParameter("duration")->setValueNotifyingHost(audioProcessor.treeState.getParameter("duration")->convertTo0to1(1.5f));
            audioProcessor.treeState.getParameter("blanking")->setValueNotifyingHost(audioProcessor.treeState.getParameter("blanking")->convertTo0to1(1.0f));
        }
    };

    setSize (1000, 530); // Taller to accommodate top preset bar
}

SteganographyEditor::~SteganographyEditor()
{
    setLookAndFeel(nullptr);
}

void SteganographyEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient bgGrad(juce::Colour(0xff2a2a35), getWidth() / 2.0f, getHeight() / 2.0f,
                                juce::Colour(0xff0d0d12), 0.0f, 0.0f, true);
    g.setGradientFill(bgGrad);
    g.fillAll();
}

void SteganographyEditor::resized()
{
    auto area = getLocalBounds().reduced(10);
    
    // Top bar for Presets
    auto topArea = area.removeFromTop(30);
    presetComboBox.setBounds(topArea.withSizeKeepingCentre(400, 24));
    
    area.removeFromTop(10);
    
    // Bottom bar for About button and Offline Render
    auto bottomArea = area.removeFromBottom(25);
    aboutButton.setBounds(bottomArea.removeFromRight(150));
    
    if (audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        offlineRenderButton.setBounds(bottomArea.removeFromLeft(150));
    }
    
    area.removeFromBottom(10);
    
    // Controls panel
    auto controlsArea = area.removeFromBottom(90);
    int controlWidth = controlsArea.getWidth() / 4;
    
    auto layoutControl = [&](juce::Slider& s, juce::Label& l, juce::Rectangle<int> bounds) {
        l.setBounds(bounds.removeFromTop(20));
        s.setBounds(bounds);
    };
    
    layoutControl(intensitySlider, intensityLabel, controlsArea.removeFromLeft(controlWidth));
    layoutControl(targetFreqSlider, targetFreqLabel, controlsArea.removeFromLeft(controlWidth));
    layoutControl(durationSlider, durationLabel, controlsArea.removeFromLeft(controlWidth));
    layoutControl(blankingSlider, blankingLabel, controlsArea.removeFromLeft(controlWidth));
    
    area.removeFromBottom(15);
    
    // Split remaining area in half horizontally for Canvas and Spectrogram
    int halfWidth = area.getWidth() / 2;
    canvas.setBounds(area.removeFromLeft(halfWidth).reduced(5));
    spectrogram.setBounds(area.reduced(5));
    
    manualOverlay.setBounds(getLocalBounds());
}

void SteganographyEditor::startOfflineRender()
{
    if (!audioProcessor.hasValidMask.load())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Wait", "Please wait for the image analysis to complete before rendering.");
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser>("Select input audio file...", 
                                                      juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                      "*.wav;*.aif;*.aiff;*.flac");

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
        auto inFile = fc.getResult();
        if (inFile.existsAsFile())
        {
            fileChooser = std::make_unique<juce::FileChooser>("Save output audio file...", 
                                                              inFile.getParentDirectory().getChildFile(inFile.getFileNameWithoutExtension() + "_stego.wav"),
                                                              "*.wav");
                                                              
            auto saveFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;
            
            fileChooser->launchAsync(saveFlags, [this, inFile](const juce::FileChooser& fcSave) {
                auto outFile = fcSave.getResult();
                if (outFile != juce::File())
                {
                    auto* thread = new OfflineRenderThread(audioProcessor, inFile, outFile);
                    thread->launchThread();
                }
            });
        }
    });
}
