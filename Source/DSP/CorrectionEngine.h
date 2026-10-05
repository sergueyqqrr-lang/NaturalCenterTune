#pragma once
#include <array>
#include <atomic>
#include <memory>
#include "Common.h"
#include "Scales.h"
#include "PitchDetector.h"
#include "PitchCorrector.h"
#include "KeyDetector.h"
#include "PsolaSynth.h"

namespace nct
{
/** Lock-free (single writer / single reader, relaxed) history of the pitch curves for the GUI. */
struct PitchHistory
{
    static constexpr int N = 1024;
    static constexpr float kNone = -1000.f;
    std::array<std::atomic<float>, N> in, centre, out;
    std::atomic<int> head { 0 };
    PitchHistory() { for (int i = 0; i < N; ++i) { in[(size_t) i] = kNone; centre[(size_t) i] = kNone; out[(size_t) i] = kNone; } }

    void push (float i, float c, float o)
    {
        const int h = head.load (std::memory_order_relaxed);
        in[(size_t) h].store (i, std::memory_order_relaxed);
        centre[(size_t) h].store (c, std::memory_order_relaxed);
        out[(size_t) h].store (o, std::memory_order_relaxed);
        head.store ((h + 1) % N, std::memory_order_release);
    }

    /** Copies the last n points (oldest first). */
    void snapshot (float* i, float* c, float* o, int n) const
    {
        n = std::min (n, N);
        const int h = head.load (std::memory_order_acquire);
        for (int k = 0; k < n; ++k)
        {
            const size_t idx = (size_t) (((h - n + k) % N + N) % N);
            i[k] = in[idx].load (std::memory_order_relaxed);
            c[k] = centre[idx].load (std::memory_order_relaxed);
            o[k] = out[idx].load (std::memory_order_relaxed);
        }
    }
};

struct EngineParams
{
    float strength    = 1.0f;     // 0..1
    float naturalness = 0.85f;    // 0..1
    float retuneMs    = 30.0f;
    float humanize    = 0.0f;     // 0..1
    float formant     = 1.0f;     // 0..1 (1 = full formant preservation)
    int   root        = 0;
    std::uint16_t scaleMask = 0x0FFF;
};

/** Detector -> corrector -> PSOLA synthesiser, sample-by-sample. */
class CorrectionEngine
{
public:
    void prepare (double sampleRate, int numChannels, bool highQuality)
    {
        hq = highQuality;
        nCh = std::min (2, std::max (1, numChannels));

        double minHz, maxHz, targetFs, hopSec, segFrac, latFactor;
        int refine;
        if (! hq) { minHz = 90.0; maxHz = 1200.0; targetFs = 12000.0; hopSec = 0.0027;  refine = 2; segFrac = 0.35; latFactor = 2.3; }
        else      { minHz = 60.0; maxHz = 1400.0; targetFs = 16000.0; hopSec = 0.00135; refine = 3; segFrac = 0.50; latFactor = 2.6; }

        detector.prepare (sampleRate, minHz, maxHz, targetFs, hopSec, refine);
        const double frameRate = sampleRate / (double) detector.hopSamples();
        corrector.prepare (frameRate);
        key.prepare (frameRate);
        histEvery = std::max (1, (int) std::lround (frameRate / 100.0));

        PsolaSynth::Config c;
        c.sampleRate = sampleRate; c.numChannels = nCh;
        c.minPeriod = detector.minPeriod(); c.maxPeriod = detector.maxPeriod();
        c.latencyFactor = latFactor; c.segFrac = segFrac; c.searchFrac = 0.20;
        synth.prepare (c);
        histCounter = 0;
        meterCents.store (0.f);
    }

    void reset()
    {
        detector.reset(); corrector.reset(); synth.reset(); histCounter = 0;
        meterCents.store (0.f);
    }

    int latencySamples() const { return synth.latency(); }
    bool isHighQuality() const { return hq; }

    void setParams (const EngineParams& p)
    {
        params = p;
        cp.strength = p.strength; cp.naturalness = p.naturalness; cp.retuneMs = p.retuneMs;
        cp.humanize = p.humanize; cp.root = p.root; cp.mask = p.scaleMask;
        synth.setFormantPreserve (p.formant);
    }

    void processSample (const float* in, float* wet, float* dry)
    {
        const float mono = nCh == 2 ? 0.5f * (in[0] + in[1]) : in[0];
        if (detector.push (mono))
        {
            const PitchFrame& f = detector.frame();
            const CorrectorOut o = corrector.process (f, cp);

            CtlFrame cf;
            cf.voiced = f.voiced ? 1.f : 0.f;
            cf.period = (float) f.period;
            cf.ratio  = o.voiced ? (float) std::pow (2.0, o.shiftCents / 1200.0) : 1.f;
            synth.pushControl (cf, f.centerOffset);

            meterCents.store (o.voiced ? (float) o.shiftCents : 0.f, std::memory_order_relaxed);
            if (o.voiced && o.settled) key.feed (o.centerMidi);
            if (++histCounter >= histEvery)
            {
                histCounter = 0;
                if (o.voiced) history.push ((float) o.inMidi, (float) o.centerMidi, (float) (o.inMidi + o.shiftCents / 100.0));
                else          history.push (PitchHistory::kNone, PitchHistory::kNone, PitchHistory::kNone);
            }
        }
        synth.process (in, wet, dry);
    }

    // GUI-facing, thread-safe
    std::atomic<float> meterCents { 0.f };
    PitchHistory history;
    KeyDetector key;

private:
    bool hq = false;
    int nCh = 1, histEvery = 4, histCounter = 0;
    EngineParams params;
    CorrectorParams cp;
    PitchDetector detector;
    PitchCorrector corrector;
    PsolaSynth synth;
};
} // namespace nct
