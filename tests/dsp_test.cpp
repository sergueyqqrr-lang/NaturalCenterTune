// Stand-alone verification of the DSP core (no JUCE needed):  g++ -std=c++17 -O2 tests/dsp_test.cpp -o dsp_test
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
#include "../Source/DSP/CorrectionEngine.h"

using namespace nct;

static std::vector<float> synthVoice (double sr, double seconds, double (*centsFn)(double), double baseHz)
{
    const size_t n = (size_t) (sr * seconds);
    std::vector<float> x (n);
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const double f0 = baseHz * std::pow (2.0, centsFn (t) / 1200.0);
        phase += 2.0 * kPi * f0 / sr;
        double s = 0.0;
        for (int h = 1; h <= 30; ++h)
        {
            const double fh = f0 * h; if (fh > sr * 0.45) break;
            const double fm = 1.0 + 3.0 * std::exp (-std::pow ((fh - 800.0) / 250.0, 2)) + 2.0 * std::exp (-std::pow ((fh - 1900.0) / 350.0, 2));
            s += (fm / (double) h) * std::sin ((double) h * phase);
        }
        x[i] = (float) (0.12 * s);
    }
    return x;
}

static std::vector<float> run (const std::vector<float>& in, double sr, bool hq, const EngineParams& p, int& lat, std::vector<float>* dryOut = nullptr)
{
    CorrectionEngine e; e.prepare (sr, 1, hq); e.setParams (p); lat = e.latencySamples();
    std::vector<float> out (in.size()), dry (in.size());
    for (size_t i = 0; i < in.size(); ++i) { float a = in[i], w = 0, d = 0; e.processSample (&a, &w, &d); out[i] = w; dry[i] = d; }
    if (dryOut) *dryOut = dry;
    return out;
}

struct Stats { double mean, sd; int n; };

static Stats measure (const std::vector<float>& x, double sr, double t0, double t1, double* meanPeriodOut = nullptr)
{
    PitchDetector d; d.prepare (sr, 60, 1400, 16000, 0.00135, 3);
    std::vector<double> m;
    for (size_t i = 0; i < x.size(); ++i)
        if (d.push (x[i]) && (double) i / sr >= t0 && (double) i / sr <= t1 && d.frame().voiced) m.push_back (d.frame().midi);
    Stats s { 0, 0, (int) m.size() };
    if (m.empty()) return s;
    for (double v : m) s.mean += v; s.mean /= (double) m.size();
    for (double v : m) s.sd += (v - s.mean) * (v - s.mean); s.sd = std::sqrt (s.sd / (double) m.size());
    (void) meanPeriodOut;
    return s;
}

static double vibSharp (double t) { return 30.0 + 40.0 * std::sin (2.0 * kPi * 5.5 * t); }   // +30c sharp, +-40c vibrato
static double steady   (double)   { return 0.0; }
static double steps    (double t) { return t < 2.0 ? 25.0 : (t < 4.0 ? 200.0 - 30.0 : 400.0 + 20.0); }  // A4+25c -> B4-30c -> C#5+20c

int main()
{
    const double sr = 48000.0;
    int fails = 0;
    auto check = [&] (bool ok, const char* msg) { std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", msg); if (! ok) ++fails; };

    for (int hq = 0; hq < 2; ++hq)
    {
        std::printf ("=== %s ===\n", hq ? "HIGH QUALITY" : "REAL-TIME");
        auto voice = synthVoice (sr, 6.0, vibSharp, 440.0);
        int lat = 0;
        EngineParams p; p.strength = 1.f; p.naturalness = 1.f; p.retuneMs = 20.f; p.humanize = 0.f; p.formant = 1.f;

        auto out = run (voice, sr, hq != 0, p, lat);
        std::printf ("  latency: %d samples (%.1f ms)\n", lat, 1000.0 * lat / sr);
        auto sin_ = measure (voice, sr, 2.5, 5.0);
        auto sout = measure (out, sr, 2.5 + (double) lat / sr, 5.0 + (double) lat / sr);
        std::printf ("  input : mean %.3f st (A4 = 69.000), sd %.1f cents\n", sin_.mean, sin_.sd * 100);
        std::printf ("  output: mean %.3f st, sd %.1f cents\n", sout.mean, sout.sd * 100);
        check (std::fabs (sout.mean - 69.0) < 0.04, "centre pulled to A4 (|err| < 4 cents) with 30c sharp input");
        check (std::fabs (sout.sd / sin_.sd - 1.0) < 0.15, "vibrato depth preserved (+-15%) at Naturalness 100%");

        p.naturalness = 0.f; p.retuneMs = 2.f;     // fast retune + naturalness 0 = classic hard-tune
        out = run (voice, sr, hq != 0, p, lat);
        sout = measure (out, sr, 2.5 + (double) lat / sr, 5.0 + (double) lat / sr);
        std::printf ("  nat=0 : mean %.3f st, sd %.1f cents\n", sout.mean, sout.sd * 100);
        check (sout.sd * 100 < 0.35 * sin_.sd * 100, "Naturalness 0% flattens vibrato (hard-tune behaviour)");

        p.naturalness = 1.f; p.strength = 0.f; p.retuneMs = 20.f;
        std::vector<float> dry;
        auto steadyV = synthVoice (sr, 4.0, steady, 330.0);
        out = run (steadyV, sr, hq != 0, p, lat, &dry);
        double num = 0, den = 0;
        for (size_t i = (size_t) (1.0 * sr); i < out.size() - 100; ++i) { const double d = out[i] - dry[i]; num += d * d; den += (double) dry[i] * dry[i]; }
        const double snr = 10 * std::log10 (den / (num + 1e-20));
        std::printf ("  strength=0 transparency (steady tone) vs delayed dry: %.1f dB\n", snr);
        check (snr > 20.0, "Strength 0% is transparent (PSOLA identity)");

        // formant preservation: steady vowel 25 cents flat of E4 corrected up; formant-band energy must not move
        {
            auto vow = synthVoice (sr, 4.0, [] (double) { return -45.0; }, 330.0);   // 45 cents flat
            p.strength = 1.f; p.naturalness = 1.f; p.retuneMs = 10.f;
            auto o2 = run (vow, sr, hq != 0, p, lat);
            auto bandDb = [&] (const std::vector<float>& x, size_t off, double f1, double f2)
            {
                const int Nf = 8192; double acc = 0, tot = 1e-12;
                std::vector<double> re (Nf), im (Nf);
                for (int i = 0; i < Nf; ++i) { re[(size_t) i] = x[off + (size_t) i] * (0.5 - 0.5 * std::cos (2 * kPi * i / Nf)); im[(size_t) i] = 0; }
                for (int len = 2; len <= Nf; len <<= 1)   // naive iterative FFT
                {
                    // bit reversal is done below once; here do standard DIT after reorder
                    (void) len;
                }
                // simple DFT on the needed bins only (band energy via Goertzel-like direct sums)
                for (int k = 1; k < Nf / 2; ++k)
                {
                    const double f = k * sr / Nf; double r = 0, ii = 0;
                    const double w = 2 * kPi * k / Nf;
                    // sparse evaluation: only every 4th bin to keep the test fast
                    if (k % 4) continue;
                    for (int i = 0; i < Nf; ++i) { r += re[(size_t) i] * std::cos (w * i); ii -= re[(size_t) i] * std::sin (w * i); }
                    const double e2 = r * r + ii * ii; tot += e2; if (f >= f1 && f <= f2) acc += e2;
                }
                return 10 * std::log10 (acc / tot);
            };
            const size_t off = (size_t) (1.5 * sr);
            const double bi = bandDb (vow, off, 600, 1100), bo = bandDb (o2, off + (size_t) lat, 600, 1100);
            std::printf ("  formant: band 600-1100 Hz share  in %.2f dB / out %.2f dB (shift = +45 cents)\n", bi, bo);
            check (std::fabs (bi - bo) < 0.8, "formant band energy unchanged by the shift (|d| < 0.8 dB)");
        }

        // step test
        auto st = synthVoice (sr, 6.0, steps, 440.0);
        p.strength = 1.f; p.naturalness = 1.f; p.retuneMs = 15.f;
        out = run (st, sr, hq != 0, p, lat);
        const double shift = (double) lat / sr;
        auto a = measure (out, sr, 1.0 + shift, 1.9 + shift), b = measure (out, sr, 3.0 + shift, 3.9 + shift), c = measure (out, sr, 5.0 + shift, 5.9 + shift);
        std::printf ("  steps  : %.3f (69) | %.3f (71) | %.3f (73)\n", a.mean, b.mean, c.mean);
        check (std::fabs (a.mean - 69) < 0.05 && std::fabs (b.mean - 71) < 0.05 && std::fabs (c.mean - 73) < 0.05, "each note snapped to nearest chromatic note");

        // noise: should pass untouched
        std::mt19937 rng (1); std::normal_distribution<float> nd (0.f, 0.05f);
        std::vector<float> noise (96000); for (auto& v : noise) v = nd (rng);
        out = run (noise, sr, hq != 0, p, lat, &dry);
        double dn = 0, dd = 0; for (size_t i = 5000; i < noise.size(); ++i) { const double d = out[i] - dry[i]; dn += d * d; dd += (double) dry[i] * dry[i]; }
        std::printf ("  noise  : error vs dry = %.1f dB\n", 10 * std::log10 (dn / dd + 1e-20));
        check (dn / dd < 0.05, "unvoiced material is passed through (>13 dB below)");

        // finite & level
        bool fin = true; double eo = 0, ei = 0; for (size_t i = 0; i < out.size(); ++i) if (! std::isfinite (out[i])) fin = false;
        out = run (voice, sr, hq != 0, p, lat);
        for (size_t i = (size_t) sr; i < out.size(); ++i) { eo += out[i] * out[i]; ei += voice[i - (size_t) lat] * voice[i - (size_t) lat]; }
        std::printf ("  level  : out/in RMS = %.3f\n", std::sqrt (eo / ei));
        check (fin && std::fabs (std::sqrt (eo / ei) - 1.0) < 0.12, "no NaN, output level within 1 dB");

        // CPU
        auto t0 = std::chrono::steady_clock::now();
        run (voice, sr, hq != 0, p, lat);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  CPU    : %.1f%% of one core (mono, 6 s of audio in %.3f s)\n", 100.0 * sec / 6.0, sec);
    }
    std::printf ("\n%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", fails);
    return fails ? 1 : 0;
}
