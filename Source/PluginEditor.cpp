#include "PluginEditor.h"

using namespace nctui;

// ------------------------------------------------------------------------------ ParamKnob
ParamKnob::ParamKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& caption,
                      bool big, std::function<juce::String (double)> fmt)
    : title (caption), isBig (big), format (std::move (fmt)), attachment (apvts, id, slider)
{
    slider.getProperties().set ("big", big);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (180);
    slider.setDoubleClickReturnValue (true, slider.getValue());
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (isBig ? 22 : 18);
    r.removeFromBottom (isBig ? 22 : 18);
    slider.setBounds (r);
}

void ParamKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (isBig ? accent2 : dim);
    g.setFont (isBig ? 15.f : 12.f);
    g.drawText (title.toUpperCase(), r.removeFromTop (isBig ? 22 : 18), juce::Justification::centred);
    g.setColour (text);
    g.setFont (isBig ? 15.f : 13.f);
    g.drawText (format ? format (slider.getValue()) : juce::String (slider.getValue(), 1), r.removeFromBottom (isBig ? 22 : 18), juce::Justification::centred);
}

// ------------------------------------------------------------------------------ Editor
NaturalCenterTuneAudioProcessorEditor::NaturalCenterTuneAudioProcessorEditor (NaturalCenterTuneAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      naturalness (p.apvts, ids::naturalness, "Naturalness", true,  [] (double v) { return juce::String ((int) std::lround (v)) + " %"; }),
      strength    (p.apvts, ids::strength,    "Strength",    false, [] (double v) { return juce::String ((int) std::lround (v)) + " %"; }),
      retune      (p.apvts, ids::retune,      "Retune",      false, [] (double v) { return juce::String (v, v < 10.0 ? 1 : 0) + " ms"; }),
      humanize    (p.apvts, ids::humanize,    "Humanize",    false, [] (double v) { return juce::String ((int) std::lround (v)) + " %"; }),
      formant     (p.apvts, ids::formant,     "Formant",     false, [] (double v) { return juce::String ((int) std::lround (v)) + " %"; }),
      bypassAtt (p.apvts, ids::bypass, bypassButton), inAtt (p.apvts, ids::inGain, inGain), outAtt (p.apvts, ids::outGain, outGain)
{
    setLookAndFeel (&laf);
    setSize (760, 520);

    for (const auto& n : { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }) rootBox.addItem (n, rootBox.getNumItems() + 1);
    for (const auto& s : nct::scaleTable()) scaleBox.addItem (s.name, scaleBox.getNumItems() + 1);
    modeBox.addItem ("Real-time", 1);
    modeBox.addItem ("High Quality", 2);
    // attachments are created AFTER the items exist so the initial parameter value selects the right entry
    rootAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids::root,  rootBox);
    scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids::scale, scaleBox);
    modeAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids::mode,  modeBox);

    pitchView.engineGetter = [this] { return &proc.activeEngine(); };
    pitchView.scaleGetter = [this] (int& root, std::uint16_t& mask)
    {
        root = (int) *proc.apvts.getRawParameterValue (ids::root);
        mask = nct::scaleTable()[(size_t) juce::jlimit (0, 11, (int) *proc.apvts.getRawParameterValue (ids::scale))].mask;
    };

    detectButton.onClick = [this]
    {
        if (proc.applyDetectedKey()) keyText = "Key applied";
        else keyText = "Sing a few seconds first";
        repaint();
    };
    bypassButton.setColour (juce::ToggleButton::textColourId, text);
    bypassButton.setColour (juce::ToggleButton::tickColourId, accent);

    for (auto* c : std::initializer_list<juce::Component*> { &pitchView, &meter, &naturalness, &strength, &retune, &humanize, &formant,
                                                              &rootBox, &scaleBox, &modeBox, &detectButton, &bypassButton, &inGain, &outGain })
        addAndMakeVisible (c);

    inGain.setDoubleClickReturnValue (true, 0.0);
    outGain.setDoubleClickReturnValue (true, 0.0);
    startTimerHz (30);
}

NaturalCenterTuneAudioProcessorEditor::~NaturalCenterTuneAudioProcessorEditor() { setLookAndFeel (nullptr); }

void NaturalCenterTuneAudioProcessorEditor::timerCallback()
{
    pitchView.repaint();
    meter.setValue (proc.activeEngine().meterCents.load (std::memory_order_relaxed));
    auto& k = proc.activeEngine().key;
    if (k.hasResult())
    {
        static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto s = juce::String ("Detected: ") + n[k.getRoot() % 12] + (k.isMinor() ? " minor" : " major")
                     + " (" + juce::String ((int) std::lround (juce::jlimit (0.f, 1.f, k.getConfidence()) * 100.f)) + "%)";
        if (s != keyText && ! keyText.startsWith ("Key applied")) { keyText = s; repaint (detectButton.getBounds().expanded (200, 6)); }
    }
}

void NaturalCenterTuneAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (text);
    g.setFont (22.f);
    g.drawText ("NaturalCenter Tune", 20, 12, 320, 30, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (12.f);
    g.drawText ("CENTER-ONLY PITCH CORRECTION", 20, 38, 320, 14, juce::Justification::centredLeft);

    auto b = detectButton.getBounds();
    g.setColour (dim);
    g.setFont (12.f);
    g.drawText (keyText, b.getRight() + 10, b.getY(), 240, b.getHeight(), juce::Justification::centredLeft);

    g.setColour (dim);
    g.drawText ("INPUT",  inGain.getX(),  inGain.getBottom() + 2, inGain.getWidth(),  14, juce::Justification::centred);
    g.drawText ("OUTPUT", outGain.getX(), outGain.getBottom() + 2, outGain.getWidth(), 14, juce::Justification::centred);
    g.setColour (panel);
    g.fillRoundedRectangle (naturalness.getBounds().toFloat().expanded (4.f), 10.f);
}

void NaturalCenterTuneAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto top = r.removeFromTop (44);
    bypassButton.setBounds (top.removeFromRight (80).reduced (0, 8));
    top.removeFromRight (8);
    modeBox.setBounds (top.removeFromRight (140).reduced (0, 8));

    r.removeFromTop (6);
    pitchView.setBounds (r.removeFromTop (170));
    r.removeFromTop (8);
    meter.setBounds (r.removeFromTop (46));
    r.removeFromTop (10);

    auto knobs = r.removeFromTop (190);
    auto big = knobs.removeFromLeft (220);
    naturalness.setBounds (big.reduced (6));
    knobs.removeFromLeft (8);
    const int w = knobs.getWidth() / 4;
    strength.setBounds (knobs.removeFromLeft (w).reduced (4, 24));
    retune.setBounds   (knobs.removeFromLeft (w).reduced (4, 24));
    humanize.setBounds (knobs.removeFromLeft (w).reduced (4, 24));
    formant.setBounds  (knobs.reduced (4, 24));

    r.removeFromTop (6);
    auto bottom = r;
    rootBox.setBounds (bottom.removeFromLeft (70).withHeight (28));
    bottom.removeFromLeft (8);
    scaleBox.setBounds (bottom.removeFromLeft (160).withHeight (28));
    bottom.removeFromLeft (14);
    detectButton.setBounds (bottom.removeFromLeft (100).withHeight (28));
    bottom.removeFromLeft (260);
    auto gains = bottom;
    inGain.setBounds  (gains.removeFromLeft (gains.getWidth() / 2).reduced (6, 0).withHeight (28));
    outGain.setBounds (gains.reduced (6, 0).withHeight (28));
}
