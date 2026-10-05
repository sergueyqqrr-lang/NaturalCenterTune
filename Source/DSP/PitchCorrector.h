#pragma once
#include <vector>
#include "Common.h"
#include "Scales.h"
#include "PitchDetector.h"

namespace nct
{
struct CorrectorParams
{
    float strength    = 1.0f;     // 0..1   amount of correction applied
    float naturalness = 0.85f;    // 0..1   1 = vibrato / expression fully preserved
    float retuneMs    = 30.0f;    // time constant of the centre -> target movement
    float humanize    = 0.0f;     // 0..1
    int   root        = 0;        // 0..11
    std::uint16_t mask = 0x0FFF;  // allowed scale degrees
};

struct CorrectorOut
{
    bool   voiced      = false;
    bool   settled     = false;   // note old enough for key statistics
    double shiftCents  = 0.0;     // pitch shift to apply (cents)
    double inMidi      = 0.0;
    double centerMidi  = 0.0;
    double targetMidi  = 0.0;
};

/**
    Separates the pitch contour into
        centre      : slow component = what the singer "means" (vibrato-free mean of the note)
        expression  : p - centre     = vibrato, scoops, micro-variations
    and builds the correction ONLY from the centre:

        desired = (target - centre)  -  (1 - naturalness) * (p - centre)

    naturalness = 1 : output = target + expression      (pure centre correction)
    naturalness = 0 : output = target                   (classic hard tune)

    Centre estimation (causal, per note):
        two cascaded moving averages of 150 ms and 220 ms. Their sinc zeros sit at 6.7 Hz and 4.5 Hz,
        so 4-7 Hz vibrato is attenuated by > 30 dB without having to know the vibrato rate.
    Note segmentation:
        * unvoiced gaps start a new note,
        * a sustained one-sided departure of the short-term mean from the centre (larger than
          1.8x the estimated vibrato amplitude) freezes the correction and, if it persists, splits the note.
*/
class PitchCorrector
{
public:
    void prepare (double frameRateHz)
    {
        fr = frameRateHz; dt = 1.0 / fr;
        W1 = std::max (3, (int) std::lround (0.150 * fr));
        W2 = std::max (3, (int) std::lround (0.220 * fr));
        Ns = std::max (2, (int) std::lround (0.040 * fr));
        kv = 1.0 - std::exp (-dt / 0.150);
        P.assign (kRing, 0.0); C1.assign (kRing, 0.0); cumP.assign (kRing, 0.0); cumC.assign (kRing, 0.0);
        reset();
    }

    void reset()
    {
        std::fill (P.begin(), P.end(), 0.0); std::fill (C1.begin(), C1.end(), 0.0);
        std::fill (cumP.begin(), cumP.end(), 0.0); std::fill (cumC.begin(), cumC.end(), 0.0);
        idx = 0; noteStart = 0; rampStart = 0; haveNote = false; wasVoiced = false;
        unvoicedFrames = 100000; center = 0.0; target = 0; targetValid = false;
        s = 0.0; lastDesired = 0.0; v2 = 0.04; departFrames = 0; departSign = 0;
        bias = 0.0; drift = 0.0; driftTarget = 0.0; driftTimer = 0.0;
    }

    CorrectorOut process (const PitchFrame& f, const CorrectorParams& prm)
    {
        CorrectorOut o;
        humanizeCache = prm.humanize;
        if (! f.voiced)
        {
            ++unvoicedFrames;
            wasVoiced = false;
            return o;
        }

        const double p = f.midi;
        if (! wasVoiced)
        {
            const bool continues = haveNote && (double) unvoicedFrames * dt < 0.15 && std::fabs (p - center) < 1.0;
            if (! continues) startNote (p, true);
            unvoicedFrames = 0;
            wasVoiced = true;
        }

        // ---- push sample -------------------------------------------------------------
        P[slot (idx)] = p;
        cumP[slot (idx + 1)] = cumP[slot (idx)] + p;

        // ---- departure / note split detection (uses centre of the previous frame) -----
        const int64 age = idx - noteStart;
        bool splitNow = false;
        if (age >= 2)
        {
            const int64 a3 = std::max<int64> (noteStart, idx - Ns + 1);
            const double mShort = (cumP[slot (idx + 1)] - cumP[slot (a3)]) / (double) (idx + 1 - a3);
            const double sd = mShort - center;
            const double thr = std::max (0.6, 1.8 * std::sqrt (2.0 * v2));
            const int sgn = sd > 0.0 ? 1 : -1;
            if (std::fabs (sd) > thr)
            {
                if (departFrames > 0 && sgn != departSign) departFrames = 1;
                else ++departFrames;
                departSign = sgn;
            }
            else if (std::fabs (sd) < 0.5 * thr) departFrames = 0;

            if ((double) departFrames * dt >= 0.11)
            {
                const int back = (int) std::lround (0.75 * (double) departFrames);
                noteStart = std::max<int64> (noteStart + 1, idx - back);
                departFrames = 0; v2 = 0.04; targetValid = false;
                bias = prm.humanize * 0.10 * (double) rng.nextPM1();
                splitNow = true;
            }
        }
        const bool departing = (double) departFrames * dt >= 0.05;

        // ---- centre ---------------------------------------------------------------------
        for (int64 j = splitNow ? noteStart : idx; j <= idx; ++j) computeC1 (j);
        {
            const int64 a2 = std::max<int64> (noteStart, idx - W2 + 1);
            center = (cumC[slot (idx + 1)] - cumC[slot (a2)]) / (double) (idx + 1 - a2);
        }
        const double dev = p - center;
        if (! departing) v2 += kv * (dev * dev - v2);

        // ---- target note (hysteresis) ---------------------------------------------------
        if (! departing)
        {
            const int cand = nearestAllowed (center, prm.root, prm.mask);
            if (! targetValid || ! isAllowed (prm.mask, prm.root, target)) { target = cand; targetValid = true; }
            else if (cand != target && std::fabs (center - (double) cand) + 0.15 < std::fabs (center - (double) target))
                target = cand;
        }

        // ---- humanize ----------------------------------------------------------------
        driftTimer -= dt;
        if (driftTimer <= 0.0) { driftTarget = (double) rng.nextPM1(); driftTimer = 0.35 + 0.3 * (double) rng.next01(); }
        drift += (driftTarget - drift) * (1.0 - std::exp (-dt / 0.25));
        const double human = bias + (double) prm.humanize * 0.06 * drift;

        // ---- desired shift (semitones) ----------------------------------------------------
        double desired = lastDesired;
        if (! departing)
        {
            desired = ((double) target + human) - center - (1.0 - (double) prm.naturalness) * dev;
            desired = clampd (desired, -6.0, 6.0);
            lastDesired = desired;
        }

        // onset ramp: the centre estimate needs a few frames to become reliable
        const double ramp = smoothstep01 ((double) (idx - rampStart + 1) * dt / 0.08);
        const double tau  = std::max (0.0005, (double) prm.retuneMs * 0.001);
        s += (1.0 - std::exp (-dt / tau)) * (ramp * desired - s);

        o.voiced     = true;
        o.settled    = (double) (idx - noteStart) * dt > 0.15;
        o.inMidi     = p;
        o.centerMidi = center;
        o.targetMidi = (double) target;
        o.shiftCents = 100.0 * clampd ((double) prm.strength * s, -6.0, 6.0);
        ++idx;
        return o;
    }

private:
    static constexpr int kRing = 4096;
    static constexpr int64 kMask = kRing - 1;
    static size_t slot (int64 i) { return (size_t) (i & kMask); }

    void startNote (double p, bool newRun)
    {
        noteStart = idx; rampStart = idx; center = p; targetValid = false; haveNote = true;
        v2 = 0.04; departFrames = 0; lastDesired = 0.0;
        if (newRun) s = 0.0;
        bias = (double) humanizeCache * 0.10 * (double) rng.nextPM1();
        cumC[slot (idx + 1)] = cumC[slot (idx)];
    }

    void computeC1 (int64 j)
    {
        const int64 a = std::max<int64> (noteStart, j - W1 + 1);
        const double c1 = (cumP[slot (j + 1)] - cumP[slot (a)]) / (double) (j + 1 - a);
        C1[slot (j)] = c1;
        cumC[slot (j + 1)] = cumC[slot (j)] + c1;
    }

public:
    float humanizeCache = 0.f;   // set by the engine before process() (used for per-note bias)

private:
    double fr = 375.0, dt = 1.0 / 375.0, kv = 0.02;
    int W1 = 56, W2 = 82, Ns = 15;
    std::vector<double> P, C1, cumP, cumC;
    int64 idx = 0, noteStart = 0, rampStart = 0;
    bool haveNote = false, wasVoiced = false, targetValid = false;
    int64 unvoicedFrames = 0;
    double center = 0.0, s = 0.0, lastDesired = 0.0, v2 = 0.04;
    int target = 0, departFrames = 0, departSign = 0;
    double bias = 0.0, drift = 0.0, driftTarget = 0.0, driftTimer = 0.0;
    XorShift rng;
};
} // namespace nct
