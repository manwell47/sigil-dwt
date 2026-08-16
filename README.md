<div align="center">
  <br>
  <h1>🎛️ SIGIL DWT <br><sup>Audio Steganography</sup></h1>
  <p>
    <a href="https://raw.githubusercontent.com/manwell47/sigil-dwt/main/Releases/SIGIL_DWT_v1.0_Windows.zip"><img src="https://img.shields.io/badge/Download-v1.0_Windows-00e5ff?style=for-the-badge&logo=download" alt="Download Windows Release"></a>
    <img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg?style=for-the-badge&logo=c%2B%2B" alt="C++20">
    <img src="https://img.shields.io/badge/JUCE-8.0-4caf50.svg?style=for-the-badge&logo=c%2B%2B" alt="JUCE 8">
    <img src="https://img.shields.io/badge/Platform-Windows-0078d7.svg?style=for-the-badge&logo=windows" alt="Windows">
  </p>
  <p>
    <strong>A high-end, commercial-grade audio steganography VST3/Standalone plugin built with C++ and JUCE.</strong>
  </p>
  <br>
</div>

![Sigil DWT Screenshot](Assets/screenshot.png)

> **SIGIL DWT** allows you to inject visual data (images, typography, logos) directly into the spectral domain of an audio signal, making them visible in any downstream spectrum analyzer (such as **FabFilter Pro-Q3** or **iZotope RX**).

Designed with a strict mastering philosophy, SIGIL DWT operates within the safe high-frequency band (16 kHz - 22 kHz), prioritizing acoustic transparency over brute-force visual injection, thus preventing phase issues and low-end mud in your mixes.

---

## ✨ Features at a Glance

| 🎚️ Audio Engineering | 🖼️ Computer Vision | ⚡ Performance |
| :--- | :--- | :--- |
| **32-Bit Float Export**: Mathematically lossless WAV export. | **Dual Sobel C++ Filter**: Extracts topological contours from Alpha & Luminance channels. | **Lock-Free Concurrency**: Rock solid audio thread without glitching. |
| **Db4 Wavelets**: Daubechies 4 DWT for zero pre-ringing transients. | **ONNX / U2-Net**: Embedded AI salient object detection. | **Direct2D Acceleration**: 60fps Spectrogram rendering via GPU. |
| **Phase Coherence**: Zero phase-shift below 16 kHz. | **Transparency Safe**: Accurately renders PNG logos and text outlines. | **Background Worker**: Neural net inference running entirely off the audio thread. |

---

## 🚀 Key Technologies

### 1. Dual Sobel Filter (Luminance + Alpha)
Unlike naive image converters, SIGIL DWT uses a custom high-precision **Dual Sobel C++ Filter** that operates simultaneously on the Luminance channel and the Alpha channel. This ensures perfect topological extraction of both opaque photographs (JPEGs) and transparent typography/logos (PNGs) without flattening backgrounds or destroying delicate line work.

### 2. Discrete Wavelet Transform (Daubechies 4)
Instead of using standard FFT/IFFT overlap-add methods that can introduce smearing and pre-ringing artifacts, the audio engine utilizes **Daubechies 4 (Db4) Discrete Wavelet Transforms (DWT)**. This allows for precise subband isolation and modulation, enabling surgically clean frequency injection with ultra-fast transient response.

### 3. Neural Network Integration (ONNX Runtime / U2-Net)
The plugin comes with a fully embedded [ONNX Runtime](https://onnxruntime.ai/) environment, evaluating the **U2-Net** salient object detection neural network in a background thread. While currently bypassed for raw topological accuracy via the Sobel filter, the architecture is primed for hybrid AI-driven silhouette masking.

### 4. Lock-Free Concurrency & Direct2D Safe Rendering
To handle heavy real-time ML inference and 60fps Spectrogram rendering without glitching the audio thread, the architecture features a robust lock-free atomic bridge (single-producer/single-consumer double buffering) and deep image cloning (`createCopy()`) to prevent GPU-level deadlocks in Direct2D.

---

## 🎛️ Mastering-Grade Presets

The plugin offers a specialized preset architecture tailored for professional audio engineers:

*   **🕵️ Stealth Watermark:** Ultra-low intensity and long pacing at 19kHz. Zero added hiss, creating a faint but legally provable watermark for copyright protection.
*   **⚖️ Balanced Print:** The sweet spot at 17kHz. Legible typography acting psychoacoustically as analog high-end "air" or dither.
*   **💎 High Definition:** Maximizes visual aesthetics at 16kHz for promotional easter-eggs, at the cost of perceptible high-end hiss.
*   **🧱 Dense Fill:** Maximum density and rapid pacing for creative sound design transitions over noise synthesizers.

---

## 📥 Installation

Ready-to-use binaries are provided for Windows 10/11.

1. Head to the **[Releases Folder](https://github.com/manwell47/sigil-dwt/tree/main/Releases)** in this repository.
2. Download `SIGIL_DWT_v1.0_Windows.zip`.
3. Extract the ZIP and copy `DWT Steganography.vst3` to `C:\Program Files\Common Files\VST3`.
4. Alternatively, launch the `.exe` for standalone processing.

---

## 🛠️ Build Instructions

This project uses CMake and requires **Visual Studio 2022** on Windows (or equivalent on macOS). 
The ONNX Runtime is automatically downloaded via `FetchContent`.

```bash
# Clone the repository
git clone https://github.com/manwell47/sigil-dwt.git
cd sigil-dwt

# Create build directory
mkdir build
cd build

# Generate and compile
cmake ..
cmake --build . --config Release
```

The resulting VST3 plugin can be found in `build/SteganographyPlugin_artefacts/Release/VST3/`.

---

## 📄 License & Legal

*   Code authored for SIGIL DWT is provided under the **MIT License**.
*   **[ONNX Runtime](https://onnxruntime.ai/)**: MIT License (Microsoft Corporation).
*   **[JUCE Framework](https://juce.com/)**: GPLv3 / Commercial License (Raw Material Software).
