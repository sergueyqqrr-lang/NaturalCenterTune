#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/NctLookAndFeel.h"
#include "UI/PitchView.h"

/** Rotary knob with caption and value read-out. */
class ParamKnob : public juce::Component
{
public:
    ParamKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& caption,
               bool big, std::function<juce::String (double)> fmt);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::String title;
    bool isBig;
    std::function<juce::String (double)> format;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

class NaturalCenterTuneAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NaturalCenterTuneAudioProcessorEditor (NaturalCenterTuneAudioProcessor&);
    ~NaturalCenterTuneAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    NaturalCenterTuneAudioProcessor& proc;
    nctui::NctLookAndFeel laf;

    nctui::PitchView pitchView;
    nctui::CorrectionMeter meter;
    ParamKnob naturalness, strength, retune, humanize, formant;
    juce::ComboBox rootBox, scaleBox, modeBox;
    juce::TextButton detectButton { "Detect Key" };
    juce::ToggleButton bypassButton { "Bypass" };
    juce::Slider inGain { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider outGain { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::String keyText;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rootAtt, scaleAtt, modeAtt;
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAtt;
    juce::AudioProcessorValueTreeState::SliderAttachment inAtt, outAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NaturalCenterTuneAudioProcessorEditor)
};
