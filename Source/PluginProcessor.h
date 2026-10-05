#pragma once
#include <array>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/CorrectionEngine.h"

namespace ids
{
inline constexpr const char* root        = "root";
inline constexpr const char* scale       = "scale";
inline constexpr const char* strength    = "strength";
inline constexpr const char* naturalness = "naturalness";
inline constexpr const char* retune      = "retune";
inline constexpr const char* humanize    = "humanize";
inline constexpr const char* formant     = "formant";
inline constexpr const char* mode        = "mode";
inline constexpr const char* inGain      = "ingain";
inline constexpr const char* outGain     = "outgain";
inline constexpr const char* bypass      = "bypass";
}

class NaturalCenterTuneAudioProcessor : public juce::AudioProcessor,
                                        private juce::AsyncUpdater
{
public:
    NaturalCenterTuneAudioProcessor();
    ~NaturalCenterTuneAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NaturalCenter Tune"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- GUI helpers --------------------------------------------------------------------
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    nct::CorrectionEngine& activeEngine() { return *engines[(size_t) juce::jlimit (0, 1, activeMode.load())]; }

    /** Applies the key found by the analyser to the Root / Scale parameters (message thread). */
    bool applyDetectedKey();

    juce::AudioProcessorValueTreeState apvts;

private:
    void handleAsyncUpdate() override;
    nct::EngineParams readParams() const;

    std::array<std::unique_ptr<nct::CorrectionEngine>, 2> engines;   // [0] real-time, [1] high quality
    std::atomic<int> activeMode { 0 };
    int processedMode = 0, numCh = 2;
    double currentSampleRate = 44100.0;

    std::atomic<float>* pRoot = nullptr; std::atomic<float>* pScale = nullptr; std::atomic<float>* pStrength = nullptr;
    std::atomic<float>* pNat = nullptr;  std::atomic<float>* pRetune = nullptr; std::atomic<float>* pHuman = nullptr;
    std::atomic<float>* pFormant = nullptr; std::atomic<float>* pMode = nullptr; std::atomic<float>* pIn = nullptr;
    std::atomic<float>* pOut = nullptr; std::atomic<float>* pBypass = nullptr;

    juce::SmoothedValue<float> inGainSm, outGainSm, bypassSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NaturalCenterTuneAudioProcessor)
};
