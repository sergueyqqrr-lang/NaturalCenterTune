#pragma once
#include <vector>
#include "Common.h"

namespace nct
{
struct PitchFrame
{
    bool   voiced     = false;
    double period     = 0.0;   // full-rate samples
    double midi       = 0.0;   // fractional MIDI note (A4 = 69)
    float  confidence = 0.f;   // 0..1
    float  levelDb    = -120.f;
    double centerOffset = 0.0; // samples between the newest input sample and the centre of the measurement
};

/**
    Real-time monophonic pitch tracker.

    Stage 1  : FIR decimation to ~12-16 kHz.
    Stage 2  : YIN (difference function + cumulative mean normalisation + parabolic interpolation).
    Stage 3  : refinement of the period at the FULL sample rate (integer search around the YIN
               estimate + parabolic interpolation) -> sub-cent accuracy on stable tones.
    Stage 4  : voicing hysteresis, octave-jump folding, 3-point median.
*/
class PitchDetector
{
public:
    void prepare (double sampleRate, double minHz, double maxHz, double targetFs,
                  double hopSeconds, int refineMultiplier)
    {
        sr = sampleRate;
        D = std::max (1, (int) std::lround (sr / targetFs));
        fsd = sr / (double) D;
        hop = std::max (1, (int) std::lround (hopSeconds * sr / (double) D)) * D;
        minPeriodS = sr / maxHz;
        maxPeriodS = sr / minHz;
        tauMinD = std::max (2, (int) std::floor (fsd / maxHz));
        tauMaxD = (int) std::ceil (fsd / minHz) + 2;
        Wd = 2 * tauMaxD;
        refMul = std::max (1, refineMultiplier);

        taps = (D == 1) ? 1 : 8 * D + 1;
        groupDelay = (taps - 1) * 0.5;
        fir.assign ((size_t) taps, 0.f);
        if (D == 1) fir[0] = 1.f;
        else
        {
            const double fc = 0.45 / (double) D;
            double sum = 0.0;
            for (int i = 0; i < taps; ++i)
            {
                const double x = (double) i - groupDelay;
                const double s = std::fabs (x) < 1.0e-9 ? 2.0 * fc : std::sin (2.0 * kPi * fc * x) / (kPi * x);
                const double win = 0.5 * (1.0 - std::cos (2.0 * kPi * (double) i / (double) (taps - 1)));
                fir[(size_t) i] = (float) (s * win);
                sum += s * win;
            }
            for (auto& h : fir) h = (float) (h / sum);
        }

        ringSize = nextPow2 (8192 + 4 * Wd * D);
        ringMask = (int64) ringSize - 1;
        ring.assign ((size_t) ringSize, 0.f);
        dring.assign ((size_t) Wd, 0.f);
        w.assign ((size_t) Wd, 0.f);
        d.assign ((size_t) tauMaxD + 2, 0.f);
        cmnd.assign ((size_t) tauMaxD + 2, 1.f);
        dF.assign ((size_t) (2 * D + 8), 0.0);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.f);
        std::fill (dring.begin(), dring.end(), 0.f);
        n = 0; dHead = 0; dCount = 0;
        hasLast = false; foldCount = 0; unvoicedRun = 0; medCount = 0; wasVoiced = false;
        frame_ = PitchFrame();
    }

    int    hopSamples()  const { return hop; }
    double frameOffset() const { return groupDelay + 0.5 * (double) (Wd - 1) * (double) D; }
    double minPeriod()   const { return minPeriodS; }
    double maxPeriod()   const { return maxPeriodS; }
    const PitchFrame& frame() const { return frame_; }

    /** Feed one mono sample. Returns true when a new analysis frame is available. */
    bool push (float x)
    {
        ring[(size_t) (n & ringMask)] = x;
        ++n;
        if (n % D != 0) return false;

        double acc = 0.0;
        for (int i = 0; i < taps; ++i)
            acc += (double) fir[(size_t) i] * (double) ring[(size_t) ((n - 1 - i) & ringMask)];
        dring[(size_t) dHead] = (float) acc;
        dHead = (dHead + 1) % Wd;
        ++dCount;

        if (n % hop != 0 || dCount < Wd) return false;
        analyse();
        return true;
    }

private:
    void analyse()
    {
        for (int k = 0; k < Wd; ++k) w[(size_t) k] = dring[(size_t) ((dHead + k) % Wd)];

        double e = 0.0;
        for (int k = 0; k < Wd; ++k) e += (double) w[(size_t) k] * (double) w[(size_t) k];
        const double rms = std::sqrt (e / (double) Wd);
        frame_ = PitchFrame();
        frame_.levelDb = (float) (20.0 * std::log10 (rms + 1.0e-9));
        frame_.centerOffset = frameOffset();
        measOffset = frameOffset();

        if (frame_.levelDb < gateDb) { finish (false, 0.0, 0.0, 0.f); return; }

        // ---- YIN difference function --------------------------------------------------
        const int J = tauMaxD;
        d[0] = 0.f;
        for (int tau = 1; tau <= tauMaxD; ++tau)
        {
            double s = 0.0;
            const float* a = w.data();
            const float* b = w.data() + tau;
            for (int j = 0; j < J; ++j) { const double df = (double) a[j] - (double) b[j]; s += df * df; }
            d[(size_t) tau] = (float) s;
        }
        cmnd[0] = 1.f;
        double run = 0.0;
        for (int tau = 1; tau <= tauMaxD; ++tau)
        {
            run += d[(size_t) tau];
            cmnd[(size_t) tau] = run > 1.0e-12 ? (float) ((double) d[(size_t) tau] * tau / run) : 1.f;
        }

        int best = -1;
        for (int t = tauMinD; t < tauMaxD; ++t)
        {
            if (cmnd[(size_t) t] < 0.18f)
            {
                while (t + 1 < tauMaxD && cmnd[(size_t) t + 1] < cmnd[(size_t) t]) ++t;
                best = t; break;
            }
        }
        if (best < 0)
        {
            float mv = 1.0e9f;
            for (int t = tauMinD; t < tauMaxD; ++t) if (cmnd[(size_t) t] < mv) { mv = cmnd[(size_t) t]; best = t; }
        }
        const float dmin = cmnd[(size_t) best];
        const bool voicedNow = dmin < (wasVoiced ? 0.38f : 0.22f);
        if (! voicedNow) { finish (false, 0.0, 0.0, 0.f); return; }

        double tauf = (double) best;
        if (best > 1 && best < tauMaxD)
        {
            const double a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
            const double den = a - 2.0 * b + c;
            if (std::fabs (den) > 1.0e-12) tauf += clampd (0.5 * (a - c) / den, -1.0, 1.0);
        }

        // ---- full-rate refinement -------------------------------------------------------
        const double Tc = tauf * (double) D;
        int lo = std::max ((int) std::floor (minPeriodS * 0.9), (int) std::floor (Tc) - D - 1);
        int hi = (int) std::ceil (Tc) + D + 1;
        hi = std::min (hi, lo + (int) dF.size() - 1);
        const int Jf = (int) clampd ((double) refMul * Tc, 64.0, 2048.0);
        measOffset = 0.5 * ((double) Jf + Tc) + 8.0;   // centre of the two compared segments + FIR/decimation lag margin
        const int64 e0 = n - 1;
        int bi = lo; double bv = 1.0e300;
        for (int tau = lo; tau <= hi; ++tau)
        {
            double s = 0.0;
            for (int j = 0; j < Jf; ++j)
            {
                const double a = ring[(size_t) ((e0 - j) & ringMask)];
                const double b = ring[(size_t) ((e0 - j - tau) & ringMask)];
                s += (a - b) * (a - b);
            }
            dF[(size_t) (tau - lo)] = s;
            if (s < bv) { bv = s; bi = tau; }
        }
        double period = (double) bi;
        if (bi > lo && bi < hi)
        {
            const double a = dF[(size_t) (bi - lo - 1)], b = dF[(size_t) (bi - lo)], c = dF[(size_t) (bi - lo + 1)];
            const double den = a - 2.0 * b + c;
            if (std::fabs (den) > 1.0e-18) period += clampd (0.5 * (a - c) / den, -1.0, 1.0);
        }

        if (period < minPeriodS * 0.9 || period > maxPeriodS * 1.1) { finish (false, 0.0, 0.0, 0.f); return; }
        finish (true, period, hzToMidi (sr / period), 1.f - dmin);
    }

    void finish (bool voiced, double period, double midi, float conf)
    {
        frame_.confidence = conf;
        wasVoiced = voiced;
        if (! voiced)
        {
            if (++unvoicedRun > 10) hasLast = false;
            medCount = 0;
            frame_.voiced = false;
            return;
        }
        unvoicedRun = 0;

        // octave-jump folding (pitch class is preserved, period follows)
        bool folded = false;
        if (hasLast && foldCount < 6)
        {
            const double dm = midi - lastMidi;
            if (std::fabs (dm - 12.0) < 1.5 && period * 2.0 <= maxPeriodS * 1.05) { midi -= 12.0; period *= 2.0; folded = true; }
            else if (std::fabs (dm + 12.0) < 1.5 && period * 0.5 >= minPeriodS * 0.95) { midi += 12.0; period *= 0.5; folded = true; }
        }
        foldCount = folded ? foldCount + 1 : 0;
        hasLast = true; lastMidi = midi;

        // 3-point median on (midi, period)
        for (int i = 0; i < 2; ++i) { medM[i] = medM[i + 1]; medT[i] = medT[i + 1]; }
        medM[2] = midi; medT[2] = period;
        medCount = std::min (3, medCount + 1);
        if (medCount == 3)
        {
            int idx[3] = { 0, 1, 2 };
            std::sort (idx, idx + 3, [this] (int a, int b) { return medM[a] < medM[b]; });
            midi = medM[idx[1]]; period = medT[idx[1]];
        }
        frame_.voiced = true; frame_.midi = midi; frame_.period = period;
        frame_.centerOffset = measOffset + (medCount == 3 ? (double) hop : 0.0);   // median-of-3 is centred one hop back
    }

    double sr = 48000.0, fsd = 12000.0, minPeriodS = 40.0, maxPeriodS = 533.0;
    int D = 4, hop = 128, tauMinD = 10, tauMaxD = 136, Wd = 272, refMul = 2, taps = 33, ringSize = 0;
    double groupDelay = 0.0;
    float gateDb = -62.f;
    int64 n = 0, ringMask = 0;
    int dHead = 0, dCount = 0;
    std::vector<float> fir, ring, dring, w, d, cmnd;
    std::vector<double> dF;
    PitchFrame frame_;
    bool wasVoiced = false, hasLast = false;
    double lastMidi = 0.0, measOffset = 0.0;
    int foldCount = 0, unvoicedRun = 0, medCount = 0;
    double medM[3] = { 0, 0, 0 }, medT[3] = { 0, 0, 0 };
};
} // namespace nct
