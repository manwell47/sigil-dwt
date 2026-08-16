#pragma once

#include <JuceHeader.h>
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>

namespace steganography
{

/**
 * @class OnnxWrapper
 * @brief C++17 wrapper for ONNX Runtime to execute Edge Detection (e.g., DexiNed, U2-Net).
 * 
 * Takes a juce::Image, resizes it, converts to NCHW normalized tensor, runs inference,
 * and returns a flattened std::vector<float> mask.
 * 
 * LEGAL NOTICE (MIT License for ONNX Runtime):
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 */
class OnnxWrapper
{
public:
    OnnxWrapper();
    ~OnnxWrapper() = default;

    /**
     * @brief Loads the ONNX model from a file path or BinaryData (in memory).
     * @param modelData Pointer to the model in memory
     * @param dataSize Size of the model in bytes
     * @return true if loaded successfully
     */
    bool loadModel(const void* modelData, size_t dataSize);

    /**
     * @brief Processes an image through the neural network.
     * @param image The input juce::Image (will be resized internally)
     * @return Flattened std::vector<float> representing the 2D edge map mask.
     */
    std::vector<float> processImage(const juce::Image& image);

    // Provide the legal notice text for the "About" window of the VST3
    static juce::String getOnnxRuntimeLegalNotice();

private:
    Ort::Env env;
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memoryInfo;

    // Model specific parameters (e.g., DexiNed / U2Net usually expect 256x256 or 512x512, u2netp is 320x320)
    static constexpr int InputWidth = 320;
    static constexpr int InputHeight = 320;
    static constexpr int Channels = 3; // RGB

    std::vector<float> preprocessImage(const juce::Image& image);
    
    // Extracted from the session
    std::string inputNodeName;
    std::string outputNodeName;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OnnxWrapper)
};

} // namespace steganography
