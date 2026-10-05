#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout NaturalCenterTuneAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    StringArray roots { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    StringArray scales;
    for (const auto& s : nct::scaleTable()) scales.add (s.name);

    auto pct = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.f, 100.f, 0.1f), def,
                                                      AudioParameterFloatAttributes().withLabel ("%"));
    };

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::root, 1 },  "Root Note", roots, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, 1 }, "Scale",     scales, 0));
    p.push_back (pct (ids::strength,    "Strength",    100.f));
    p.push_back (pct (ids::naturalness, "Naturalness", 85.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::retune, 1 }, "Retune Speed",
                 NormalisableRange<float> (1.f, 500.f, 0.1f, 0.4f), 30.f, AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (pct (ids::humanize, "Humanize", 0.f));
    p.push_back (pct (ids::formant,  "Formant Preservation", 100.f));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::mode, 1 }, "Mode", StringArray { "Real-time", "High Quality" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::inGain, 1 },  "Input Gain",
                 NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f, AudioParameterFloatAttributes().withLabel ("dB")));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::outGain, 1 }, "Output Gain",
                 NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f, AudioParameterFloatAttributes().withLabel ("dB")));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    return { p.begin(), p.end() };
}

NaturalCenterTuneAudioProcessor::NaturalCenterTuneAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NaturalCenterTune", createLayout())
{
    engines[0] = std::make_unique<nct::CorrectionEngine>();
    engines[1] = std::make_unique<nct::CorrectionEngine>();

    pRoot = apvts.getRawParameterValue (ids::root);      pScale = apvts.getRawParameterValue (ids::scale);
    pStrength = apvts.getRawParameterValue (ids::strength); pNat = apvts.getRawParameterValue (ids::naturalness);
    pRetune = apvts.getRawParameterValue (ids::retune);  pHuman = apvts.getRawParameterValue (ids::humanize);
    pFormant = apvts.getRawParameterValue (ids::formant); pMode = apvts.getRawParameterValue (ids::mode);
    pIn = apvts.getRawParameterValue (ids::inGain);      pOut = apvts.getRawParameterValue (ids::outGain);
    pBypass = apvts.getRawParameterValue (ids::bypass);
}

bool NaturalCenterTuneAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

nct::EngineParams NaturalCenterTuneAudioProcessor::readParams() const
{
    nct::EngineParams e;
    e.strength    = pStrength->load() * 0.01f;
    e.naturalness = pNat->load() * 0.01f;
    e.retuneMs    = pRetune->load();
    e.humanize    = pHuman->load() * 0.01f;
    e.formant     = pFormant->load() * 0.01f;
    e.root        = juce::jlimit (0, 11, (int) pRoot->load());
    e.scaleMask   = nct::scaleTable()[(size_t) juce::jlimit (0, 11, (int) pScale->load())].mask;
    return e;
}

void NaturalCenterTuneAudioProcessor::prepareToPlay (double sr, int)
{
    currentSampleRate = sr;
    numCh = juce::jmax (1, juce::jmin (2, getTotalNumInputChannels()));
    for (auto& e : engines) e->prepare (sr, numCh, &e == &engines[1]);

    processedMode = juce::jlimit (0, 1, (int) pMode->load());
    activeMode.store (processedMode);
    engines[(size_t) processedMode]->reset();
    setLatencySamples (engines[(size_t) processedMode]->latencySamples());

    inGainSm.reset (sr, 0.02);  inGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pIn->load()));
    outGainSm.reset (sr, 0.02); outGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    bypassSm.reset (sr, 0.015); bypassSm.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 1.f : 0.f);
}

void NaturalCenterTuneAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (engines[(size_t) activeMode.load()]->latencySamples());
}

void NaturalCenterTuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nCh = juce::jmin (buffer.getNumChannels(), numCh);
    const int n = buffer.getNumSamples();
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (nCh == 0) return;

    const int mode = juce::jlimit (0, 1, (int) pMode->load());
    if (mode != processedMode)
    {
        processedMode = mode;
        engines[(size_t) mode]->reset();
        activeMode.store (mode);
        triggerAsyncUpdate();                       // latency changes -> tell the host from the message thread
    }

    auto& eng = *engines[(size_t) processedMode];
    eng.setParams (readParams());

    inGainSm.setTargetValue (juce::Decibels::decibelsToGain (pIn->load()));
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    bypassSm.setTargetValue (pBypass->load() > 0.5f ? 1.f : 0.f);

    float* chan[2] = { buffer.getWritePointer (0), nCh > 1 ? buffer.getWritePointer (1) : nullptr };
    for (int i = 0; i < n; ++i)
    {
        const float ig = inGainSm.getNextValue(), og = outGainSm.getNextValue(), by = bypassSm.getNextValue();
        float in[2] = { chan[0][i] * ig, nCh > 1 ? chan[1][i] * ig : 0.f };
        float wet[2] = { 0.f, 0.f }, dry[2] = { 0.f, 0.f };
        eng.processSample (in, wet, dry);
        for (int c = 0; c < nCh; ++c)
            chan[c][i] = (wet[c] + by * (dry[c] - wet[c])) * og;      // bypass keeps the same latency
    }
}

bool NaturalCenterTuneAudioProcessor::applyDetectedKey()
{
    auto& k = activeEngine().key;
    if (! k.hasResult()) return false;
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    set (ids::root, (float) k.getRoot());
    set (ids::scale, k.isMinor() ? 2.f : 1.f);      // Natural Minor / Major
    return true;
}

juce::AudioProcessorEditor* NaturalCenterTuneAudioProcessor::createEditor() { return new NaturalCenterTuneAudioProcessorEditor (*this); }

void NaturalCenterTuneAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void NaturalCenterTuneAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NaturalCenterTuneAudioProcessor(); }
