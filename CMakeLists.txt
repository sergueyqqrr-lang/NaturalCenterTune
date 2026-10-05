cmake_minimum_required(VERSION 3.22)
project(NaturalCenterTune VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

option(NCT_BUILD_TESTS "Build the stand-alone DSP test" OFF)
set(AAX_SDK_PATH "" CACHE PATH "Path to the AAX SDK (leave empty to skip the AAX format)")
set(JUCE_DIR_LOCAL "" CACHE PATH "Use a local JUCE checkout instead of downloading it")

# ------------------------------------------------------------------ JUCE
if(JUCE_DIR_LOCAL)
    add_subdirectory(${JUCE_DIR_LOCAL} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------ formats
set(NCT_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND NCT_FORMATS AU)
endif()
if(AAX_SDK_PATH)
    juce_set_aax_sdk_path(${AAX_SDK_PATH})
    list(APPEND NCT_FORMATS AAX)
endif()

juce_add_plugin(NaturalCenterTune
    COMPANY_NAME                "NaturalCenter Audio"
    PLUGIN_MANUFACTURER_CODE    Ncau
    PLUGIN_CODE                 Nctn
    FORMATS                     ${NCT_FORMATS}
    PRODUCT_NAME                "NaturalCenter Tune"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx "Pitch Shift"
    AU_MAIN_TYPE                kAudioUnitType_Effect
    BUNDLE_ID                   com.naturalcenter.tune)

target_sources(NaturalCenterTune PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/UI/NctLookAndFeel.h
    Source/UI/PitchView.h
    Source/DSP/Common.h
    Source/DSP/Scales.h
    Source/DSP/PitchDetector.h
    Source/DSP/PitchCorrector.h
    Source/DSP/KeyDetector.h
    Source/DSP/PsolaSynth.h
    Source/DSP/CorrectionEngine.h)

target_include_directories(NaturalCenterTune PRIVATE Source)

target_compile_definitions(NaturalCenterTune PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(NaturalCenterTune
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------ DSP test (no JUCE)
if(NCT_BUILD_TESTS)
    add_executable(nct_dsp_test tests/dsp_test.cpp)
    target_include_directories(nct_dsp_test PRIVATE Source)
    if(NOT MSVC)
        target_compile_options(nct_dsp_test PRIVATE -O2)
    endif()
endif()
