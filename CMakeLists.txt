cmake_minimum_required(VERSION 3.21)

project(SteganographyPlugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add JUCE as a submodule
add_subdirectory(JUCE)

# Define the JUCE Plugin
juce_add_plugin(SteganographyPlugin
    COMPANY_NAME "Steinberg/Antigravity"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Stbg
    PLUGIN_CODE Steg
    FORMATS VST3 Standalone
    PRODUCT_NAME "DWT Steganography"
)

juce_generate_juce_header(SteganographyPlugin)

# Include ONNX Runtime using FetchContent
include(FetchContent)

if(WIN32)
    set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.17.1/onnxruntime-win-x64-1.17.1.zip")
elseif(APPLE)
    set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.17.1/onnxruntime-osx-universal2-1.17.1.tgz")
endif()

FetchContent_Declare(
    onnxruntime
    URL ${ONNX_URL}
)
FetchContent_MakeAvailable(onnxruntime)

# We will include wavelib source directly since it's C code
# Let's add the subdirectories for our own source code
file(GLOB_RECURSE SourceFiles "Source/*.cpp" "Source/*.h" "Source/*.c")
# Add wavelib sources directly to our target to allow LTO
file(GLOB_RECURSE WavelibFiles "wavelib/src/*.c" "wavelib/header/*.h")

target_sources(SteganographyPlugin
    PRIVATE
        ${SourceFiles}
        ${WavelibFiles}
)

# Compiler flags removed for cross-platform compatibility. JUCE handles SIMD.

# Suppress VST2 replacement error
target_compile_definitions(SteganographyPlugin PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)

# Link JUCE and ONNX Runtime
target_link_libraries(SteganographyPlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
)

# ONNX Runtime platform-specific linking
if(WIN32)
    target_link_libraries(SteganographyPlugin PRIVATE ${onnxruntime_SOURCE_DIR}/lib/onnxruntime.lib)
elseif(APPLE)
    target_link_libraries(SteganographyPlugin PRIVATE ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.dylib)
endif()

target_include_directories(SteganographyPlugin
    PRIVATE
        Source
        wavelib/header
        ${onnxruntime_SOURCE_DIR}/include
)

# Add juce_add_binary_data later when we have the onnx model
juce_add_binary_data(BinaryData SOURCES "Assets/model.onnx")
target_link_libraries(SteganographyPlugin PRIVATE BinaryData)
