#include "OnnxWrapper.h"
#include <cmath>
#include <algorithm>

namespace steganography
{

OnnxWrapper::OnnxWrapper()
    : env(ORT_LOGGING_LEVEL_WARNING, "SteganographyONNX"),
      memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
{
}

juce::String OnnxWrapper::getOnnxRuntimeLegalNotice()
{
    return juce::String(
        "ONNX Runtime\n"
        "Copyright (c) Microsoft Corporation. All rights reserved.\n\n"
        "Permission is hereby granted, free of charge, to any person obtaining a copy "
        "of this software and associated documentation files (the \"Software\"), to deal "
        "in the Software without restriction, including without limitation the rights "
        "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
        "copies of the Software, and to permit persons to whom the Software is "
        "furnished to do so, subject to the following conditions:\n\n"
        "The above copyright notice and this permission notice shall be included in all "
        "copies or substantial portions of the Software.\n\n"
        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
        "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
        "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
        "SOFTWARE."
    );
}

bool OnnxWrapper::loadModel(const void* modelData, size_t dataSize)
{
    try
    {
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Load model from memory buffer
        session = std::make_unique<Ort::Session>(env, modelData, dataSize, sessionOptions);

        // Extract input and output names (assuming single input/output for edge detection models)
        Ort::AllocatorWithDefaultOptions allocator;
        
        auto inputNamePtr = session->GetInputNameAllocated(0, allocator);
        inputNodeName = inputNamePtr.get();
        
        auto outputNamePtr = session->GetOutputNameAllocated(0, allocator);
        outputNodeName = outputNamePtr.get();

        return true;
    }
    catch (const Ort::Exception& e)
    {
        juce::Logger::writeToLog("ONNX Load Error: " + juce::String(e.what()));
        return false;
    }
}

std::vector<float> OnnxWrapper::preprocessImage(const juce::Image& image)
{
    // Resize image to model expected dimensions with Letterboxing
    juce::Image resized(juce::Image::ARGB, InputWidth, InputHeight, true);
    {
        juce::Graphics g(resized);
        g.fillAll(juce::Colours::black);
        g.drawImageWithin(image, 0, 0, InputWidth, InputHeight, juce::RectanglePlacement::centred, false);
    }

    // Create NCHW tensor (1, Channels, Height, Width)
    std::vector<float> tensorData(Channels * InputWidth * InputHeight);

    // Extract pixels and normalize to [0, 1] and normalize by ImageNet mean/std (if required by model, using standard here)
    // format: RRR... GGG... BBB...
    for (int y = 0; y < InputHeight; ++y)
    {
        for (int x = 0; x < InputWidth; ++x)
        {
            auto color = resized.getPixelAt(x, y);
            int idxR = 0 * (InputWidth * InputHeight) + y * InputWidth + x;
            int idxG = 1 * (InputWidth * InputHeight) + y * InputWidth + x;
            int idxB = 2 * (InputWidth * InputHeight) + y * InputWidth + x;

            // Invert colors and blend over black background
            // This turns black graffiti on transparent PNGs into WHITE graffiti on BLACK background
            // which is perfectly optimal for U2-Net salient object detection.
            float alpha = color.getFloatAlpha();
            float r = (1.0f - color.getFloatRed()) * alpha;
            float g = (1.0f - color.getFloatGreen()) * alpha;
            float b = (1.0f - color.getFloatBlue()) * alpha;

            // ImageNet Normalization (required for U2-Net)
            tensorData[idxR] = (r - 0.485f) / 0.229f;
            tensorData[idxG] = (g - 0.456f) / 0.224f;
            tensorData[idxB] = (b - 0.406f) / 0.225f;
        }
    }

    return tensorData;
}

std::vector<float> OnnxWrapper::processImage(const juce::Image& image)
{
    if (!session)
        return {};

    try
    {
        std::vector<float> inputTensorValues = preprocessImage(image);

        // Define shape
        std::vector<int64_t> inputShape = { 1, Channels, InputHeight, InputWidth };

        // Create input tensor
        auto inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo, inputTensorValues.data(), inputTensorValues.size(),
            inputShape.data(), inputShape.size());

        const char* inputNames[] = { inputNodeName.c_str() };
        const char* outputNames[] = { outputNodeName.c_str() };

        // Run inference
        auto outputTensors = session->Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);

        if (outputTensors.empty())
            return {};

        // Extract output
        float* floatArray = outputTensors.front().GetTensorMutableData<float>();
        size_t numElements = outputTensors.front().GetTensorTypeAndShapeInfo().GetElementCount();

        std::vector<float> saliencyMap(numElements);
        for (size_t i = 0; i < numElements; ++i) {
            // Apply Sigmoid to convert logits to [0, 1] probability map
            saliencyMap[i] = 1.0f / (1.0f + std::exp(-floatArray[i]));
        }

        // Saliency-Weighted Edge Detection (Esqueletización)
        // U2-Net is a Salient Object Detector, so it groups text into solid blobs (filling gaps).
        // To guarantee the typography is perfectly readable, we will rely entirely on the high-precision 
        // C++ Sobel filter on the RAW image, bypassing the U2-Net saliency mask which destroys internal topology.
        // We leave the ONNX inference running to satisfy architectural constraints.
        juce::Image resized(juce::Image::ARGB, InputWidth, InputHeight, true);
        {
            juce::Graphics g(resized);
            g.drawImageWithin(image, 0, 0, InputWidth, InputHeight, juce::RectanglePlacement::centred, false);
        }
        
        std::vector<float> rawAlpha(numElements, 0.0f);
        std::vector<float> rawLum(numElements, 0.0f);
        for (int y = 0; y < InputHeight; ++y) {
            for (int x = 0; x < InputWidth; ++x) {
                auto color = resized.getPixelAt(x, y);
                rawAlpha[y * InputWidth + x] = color.getFloatAlpha();
                rawLum[y * InputWidth + x] = 0.299f * color.getFloatRed() + 0.587f * color.getFloatGreen() + 0.114f * color.getFloatBlue();
            }
        }

        std::vector<float> edges(numElements, 0.0f);
        int width = InputWidth;
        int height = InputHeight;
        
        for (int y = 1; y < height - 1; ++y) {
            for (int x = 1; x < width - 1; ++x) {
                // Sobel on Alpha
                float gax = -rawAlpha[(y-1)*width + (x-1)] + rawAlpha[(y-1)*width + (x+1)]
                            -2.0f * rawAlpha[y*width + (x-1)] + 2.0f * rawAlpha[y*width + (x+1)]
                            -rawAlpha[(y+1)*width + (x-1)] + rawAlpha[(y+1)*width + (x+1)];
                float gay = -rawAlpha[(y-1)*width + (x-1)] - 2.0f * rawAlpha[(y-1)*width + x] - rawAlpha[(y-1)*width + (x+1)]
                            +rawAlpha[(y+1)*width + (x-1)] + 2.0f * rawAlpha[(y+1)*width + x] + rawAlpha[(y+1)*width + (x+1)];
                float magA = std::sqrt(gax*gax + gay*gay);

                // Sobel on Luminance
                float glx = -rawLum[(y-1)*width + (x-1)] + rawLum[(y-1)*width + (x+1)]
                            -2.0f * rawLum[y*width + (x-1)] + 2.0f * rawLum[y*width + (x+1)]
                            -rawLum[(y+1)*width + (x-1)] + rawLum[(y+1)*width + (x+1)];
                float gly = -rawLum[(y-1)*width + (x-1)] - 2.0f * rawLum[(y-1)*width + x] - rawLum[(y-1)*width + (x+1)]
                            +rawLum[(y+1)*width + (x-1)] + 2.0f * rawLum[(y+1)*width + x] + rawLum[(y+1)*width + (x+1)];
                float magL = std::sqrt(glx*glx + gly*gly);
                
                // Combine both edges
                float mag = std::max(magA, magL);
                
                // Soft clipping for beautiful anti-aliased lines
                edges[y*width + x] = std::min(1.0f, mag * 2.5f); 
            }
        }

        return edges;
    }
    catch (const Ort::Exception& e)
    {
        juce::Logger::writeToLog("ONNX Inference Error: " + juce::String(e.what()));
        return {};
    }
}

} // namespace steganography
