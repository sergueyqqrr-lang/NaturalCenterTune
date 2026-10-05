#pragma once
#include <array>
#include <vector>
#include "Common.h"

namespace nct
{
struct CtlFrame
{
    float voiced = 0.f;   // 0 / 1
    float period = 0.f;   // input period in samples
    float ratio  = 1.f;   // f_out / f_in
};

/** Time-stamped track of control frames (one per analysis hop). Times are input-stream sample indices. */
class ControlTrack
{
public:
    void clear() { count = 0; }

    /** `newestIndex` = index of the newest input sample when the frame was computed. */
    void push (const CtlFrame& f, double newestIndex, double centerOffset)
    {
        double t = newestIndex - centerOffset;
        if (count > 0) t = std::max (t, times[(size_t) ((count - 1) & kMask)] + 1.0);
        ring[(size_t) (count & kMask)] = f;
        times[(size_t) (count & kMask)] = t;
        ++count;
    }

    /** Control values at input-time `t`. */
    CtlFrame lookup (double t) const
    {
        CtlFrame none;
        if (count == 0) return none;
        int64 i = count - 1;
        if (t >= times[(size_t) (i & kMask)]) return ring[(size_t) (i & kMask)];
        const int64 limit = std::max<int64> (0, count - 512);
        while (i > limit && times[(size_t) (i & kMask)] > t) --i;
        if (times[(size_t) (i & kMask)] > t) return none;                   // earlier than the first frame
        const double t0 = times[(size_t) (i & kMask)], t1 = times[(size_t) ((i + 1) & kMask)];
        const double fr = t1 > t0 ? clampd ((t - t0) / (t1 - t0), 0.0, 1.0) : 0.0;
        const CtlFrame& a = ring[(size_t) (i & kMask)];
        const CtlFrame& b = ring[(size_t) ((i + 1) & kMask)];
        const bool va = a.voiced > 0.5f, vb = b.voiced > 0.5f;
        CtlFrame r;
        r.voiced = (fr < 0.5 ? va : vb) ? 1.f : 0.f;
        if (va && vb)
        {
            r.period = a.period + (float) fr * (b.period - a.period);
            r.ratio  = a.ratio  + (float) fr * (b.ratio  - a.ratio);
        }
        else if (vb) { r.period = b.period; r.ratio = b.ratio; }
        else         { r.period = a.period; r.ratio = a.ratio; }
        return r;
    }

private:
    static constexpr int kSize = 4096;
    static constexpr int64 kMask = kSize - 1;
    std::array<CtlFrame, kSize> ring {};
    std::array<double, kSize> times {};
    int64 count = 0;
};

/**
    Pitch-synchronous overlap-add (PSOLA) shifter with a time-varying shift ratio.

    * Analysis marks are tracked by waveform similarity (normalised cross-correlation between consecutive
      pitch cycles, sub-sample parabolic refinement) on the mono sum, shared by all channels.
    * Synthesis marks are placed at spacing Tm / ratio (Tm = local analysis period).
    * Every grain is a 2-period Hann window read with cubic Hermite interpolation at a fractional position.
      Because grains are copied from the real waveform, the spectral envelope (formants) is preserved
      for any ratio; `formantPreserve < 1` re-introduces envelope shifting by resampling the grains.
    * Overlap-add is normalised by the accumulated window weight; where there is no voiced coverage the
      delayed dry signal is cross-faded in, so unvoiced sounds (s, t, breath) are never touched.
*/
class PsolaSynth
{
public:
    struct Config
    {
        double sampleRate = 48000.0;
        int    numChannels = 1;
        double minPeriod = 40.0, maxPeriod = 533.0;
        double latencyFactor = 2.3;   // latency = factor * maxPeriod
        double segFrac = 0.35;        // correlation half-segment (fraction of the period)
        double searchFrac = 0.20;     // mark search radius (fraction of the period)
    };

    void prepare (const Config& c)
    {
        cfg = c;
        nCh = std::min (2, std::max (1, c.numChannels));
        latency_ = (int) std::ceil (c.latencyFactor * c.maxPeriod) + 16;
        inSize = nextPow2 (latency_ + (int) (6.0 * c.maxPeriod) + 4096);
        inMask = (int64) inSize - 1;
        olaSize = nextPow2 ((int) (6.0 * c.maxPeriod) + 256);
        olaMask = (int64) olaSize - 1;
        for (int ch = 0; ch < 2; ++ch)
        {
            inRing[ch].assign ((size_t) inSize, 0.f);
            ola[ch].assign ((size_t) olaSize, 0.f);
        }
        mono.assign ((size_t) inSize, 0.f);
        cov.assign ((size_t) olaSize, 0.f);
        corr.assign ((size_t) (2 * (int) (c.searchFrac * c.maxPeriod * 1.2) + 16), 0.f);
        reset();
    }

    int latency() const { return latency_; }
    void setFormantPreserve (float fp) { formant = clampd ((double) fp, 0.0, 1.0); }
    /** Call right after the sample that completed an analysis frame has been fed to the detector. */
    void pushControl (const CtlFrame& f, double centerOffset) { track.push (f, (double) N, centerOffset); }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch) { std::fill (inRing[ch].begin(), inRing[ch].end(), 0.f); std::fill (ola[ch].begin(), ola[ch].end(), 0.f); }
        std::fill (mono.begin(), mono.end(), 0.f);
        std::fill (cov.begin(), cov.end(), 0.f);
        track.clear();
        N = 0; active = false; markCount = 0; cur = 0; ts = 0.0;
    }

    /** One sample frame in -> wet (corrected) and dry (latency-aligned) out. */
    void process (const float* in, float* wet, float* dry)
    {
        float m = 0.f;
        for (int ch = 0; ch < nCh; ++ch) { inRing[ch][(size_t) (N & inMask)] = in[ch]; m += in[ch]; }
        mono[(size_t) (N & inMask)] = m / (float) nCh;
        ++N;

        const int64 q = N - 1 - (int64) latency_;
        schedule (q);

        const size_t oi = (size_t) (q & olaMask);
        const float cv = cov[oi];
        double beta = clampd ((double) cv / 0.85, 0.0, 1.0);
        beta = beta * beta * (3.0 - 2.0 * beta);
        const float invc = 1.0f / std::max (cv, 0.2f);
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float d = q >= 0 ? inRing[ch][(size_t) (q & inMask)] : 0.f;
            const float w = ola[ch][oi] * invc;
            dry[ch] = d;
            wet[ch] = (float) (beta * (double) w + (1.0 - beta) * (double) d);
            ola[ch][oi] = 0.f;
        }
        cov[oi] = 0.f;
    }

private:
    static constexpr int kMarks = 128;
    static constexpr int64 kMM = kMarks - 1;

    float monoAt (int64 i) const { return (i < 0 || i >= N) ? 0.f : mono[(size_t) (i & inMask)]; }
    float chAt (int ch, int64 i) const { return (i < 0 || i >= N) ? 0.f : inRing[ch][(size_t) (i & inMask)]; }

    float readHermite (int ch, double pos) const
    {
        const int64 i = (int64) std::floor (pos);
        const float f = (float) (pos - (double) i);
        const float y0 = chAt (ch, i - 1), y1 = chAt (ch, i), y2 = chAt (ch, i + 1), y3 = chAt (ch, i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    void schedule (int64 q)
    {
        for (int guard = 0; guard < 6; ++guard)
        {
            if (! active && ! tryStart (q)) return;
            if (! placeGrain (q)) return;
        }
    }

    void endRun() { active = false; markCount = 0; cur = 0; }

    bool tryStart (int64 q)
    {
        const CtlFrame probe = track.lookup ((double) q + 0.5 * cfg.maxPeriod);
        if (probe.voiced < 0.5f || probe.period < (float) cfg.minPeriod) return false;
        const double T = clampd ((double) probe.period, cfg.minPeriod, cfg.maxPeriod);
        const double centre = (double) q + 1.2 * T;
        if (track.lookup (centre).voiced < 0.5f) return false;

        const int64 lo = (int64) std::floor (centre - 0.5 * T), hi = (int64) std::ceil (centre + 0.5 * T);
        if (hi + 2 > N - 1) return false;
        int64 best = lo; float bv = monoAt (lo);
        for (int64 i = lo + 1; i <= hi; ++i) { const float v = monoAt (i); if (v > bv) { bv = v; best = i; } }
        double frac = 0.0;
        if (best > lo && best < hi)
        {
            const double a = monoAt (best - 1), b = monoAt (best), c = monoAt (best + 1);
            const double den = a - 2.0 * b + c;
            if (std::fabs (den) > 1.0e-12) frac = clampd (0.5 * (a - c) / den, -0.5, 0.5);
        }
        marks[0] = (double) best + frac; markCount = 1; cur = 0; ts = marks[0]; active = true;
        return true;
    }

    /** Appends the next analysis mark (waveform-similarity tracking). Returns false if input is not yet available. */
    bool generateMark()
    {
        const double last = marks[(size_t) ((markCount - 1) & kMM)];
        double Tc = (double) track.lookup (last).period;
        if (Tc < cfg.minPeriod) Tc = (markCount >= 2) ? last - marks[(size_t) ((markCount - 2) & kMM)] : cfg.minPeriod;
        Tc = clampd (Tc, cfg.minPeriod, cfg.maxPeriod);

        const int64 r = (int64) std::llround (last);
        const int Tn = (int) std::llround (Tc);
        const int R  = std::max (2, (int) (cfg.searchFrac * Tc));
        const int hs = std::max (6, (int) (cfg.segFrac * Tc));
        if (r + Tn + R + hs + 2 > N - 1) return false;

        double eRef = 0.0;
        for (int j = -hs; j <= hs; ++j) { const double v = monoAt (r + j); eRef += v * v; }

        double next = last + Tc;
        if (eRef > 1.0e-9)
        {
            const int nc = 2 * R + 1;
            int bi = 0; float bc = -2.f;
            for (int k = 0; k < nc; ++k)
            {
                const int64 p = r + Tn - R + k;
                double num = 0.0, ep = 0.0;
                for (int j = -hs; j <= hs; ++j)
                {
                    const double a = monoAt (r + j), b = monoAt (p + j);
                    num += a * b; ep += b * b;
                }
                const float c = (float) (num / std::sqrt (eRef * ep + 1.0e-12));
                corr[(size_t) k] = c;
                if (c > bc) { bc = c; bi = k; }
            }
            double frac = 0.0;
            if (bi > 0 && bi < nc - 1)
            {
                const double a = corr[(size_t) bi - 1], b = corr[(size_t) bi], c = corr[(size_t) bi + 1];
                const double den = a - 2.0 * b + c;
                if (std::fabs (den) > 1.0e-12) frac = clampd (0.5 * (a - c) / den, -0.5, 0.5);
            }
            const double period = (double) (Tn - R + bi) + frac;     // measured relative to round(last)
            if (period > 0.6 * Tc && period < 1.4 * Tc) next = last + period;
        }
        marks[(size_t) (markCount & kMM)] = next;
        ++markCount;
        return true;
    }

    bool placeGrain (int64 q)
    {
        const CtlFrame c = track.lookup (ts);
        if (c.voiced < 0.5f || c.period < (float) cfg.minPeriod) { endRun(); return false; }

        const double ratio = clampd ((double) c.ratio, 0.5, 2.0);
        const double rho   = std::pow (ratio, 1.0 - formant);      // grain resampling factor
        const double Tp    = clampd ((double) c.period, cfg.minPeriod, cfg.maxPeriod);
        if (ts - Tp / rho > (double) q) return false;                // not due yet

        for (int it = 0; it < 64; ++it)
        {
            const double cm = marks[(size_t) (cur & kMM)];
            if (ts - cm <= 0.5 * Tp) break;
            if (cur + 1 >= markCount && ! generateMark()) return false;   // wait for more input
            if (std::fabs (marks[(size_t) ((cur + 1) & kMM)] - ts) < std::fabs (cm - ts)) ++cur; else break;
        }

        const double ta = marks[(size_t) (cur & kMM)];
        double Tm = Tp;
        if (cur >= 1)
        {
            const double bt = ta - marks[(size_t) ((cur - 1) & kMM)];
            if (bt > 0.6 * Tp && bt < 1.5 * Tp) Tm = bt;
        }
        const double Tout = Tm / ratio;
        const double Hout = Tm / rho;
        const double g    = Tout / Hout;

        int64 oS = (int64) std::ceil (ts - Hout), oE = (int64) std::floor (ts + Hout);
        oS = std::max (oS, q);
        oE = std::min (oE, q + (int64) olaSize - 2);
        for (int64 o = oS; o <= oE; ++o)
        {
            const double u = (double) o - ts;
            const float  w = (float) (g * 0.5 * (1.0 + std::cos (kPi * u / Hout)));
            const double src = ta + u * rho;
            const size_t oi = (size_t) (o & olaMask);
            for (int ch = 0; ch < nCh; ++ch) ola[ch][oi] += w * readHermite (ch, src);
            cov[oi] += w;
        }
        ts += Tout;
        return true;
    }

    Config cfg;
    int nCh = 1, latency_ = 0, inSize = 0, olaSize = 0;
    int64 inMask = 0, olaMask = 0, N = 0;
    std::vector<float> inRing[2], ola[2], mono, cov, corr;
    ControlTrack track;
    double formant = 1.0;
    bool active = false;
    std::array<double, kMarks> marks {};
    int64 markCount = 0, cur = 0;
    double ts = 0.0;
};
} // namespace nct
