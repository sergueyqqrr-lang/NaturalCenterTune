#pragma once
#include <array>
#include <atomic>
#include "Common.h"

namespace nct
{
/** Krumhansl-Schmuckler key finder fed with the note centres of the voice. Thread-safe results. */
class KeyDetector
{
public:
    void prepare (double frameRateHz) { fr = frameRateHz; decay = std::exp (-1.0 / (20.0 * fr)); reset(); }

    void reset()
    {
        hist.fill (0.0); total = 0.0; counter = 0;
        root.store (0); minor.store (false); confidence.store (0.f); valid.store (false);
    }

    /** Call once per voiced, settled frame. */
    void feed (double centerMidi)
    {
        for (auto& h : hist) h *= decay;
        total *= decay;
        const int pc = ((int) std::lround (centerMidi) % 12 + 12) % 12;
        hist[(size_t) pc] += 1.0;
        total += 1.0;
        if (++counter % 256 == 0) analyse();
    }

    int   getRoot() const       { return root.load(); }
    bool  isMinor() const       { return minor.load(); }
    float getConfidence() const { return confidence.load(); }
    bool  hasResult() const     { return valid.load(); }

private:
    void analyse()
    {
        if (total < 3.0 * fr) return;             // need ~3 s of voiced material
        static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double min_[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        double mh = 0.0; for (double h : hist) mh += h; mh /= 12.0;
        double bestC = -2.0; int bestR = 0; bool bestMinor = false;
        for (int mode = 0; mode < 2; ++mode)
        {
            const double* prof = mode == 0 ? maj : min_;
            double mp = 0.0; for (int i = 0; i < 12; ++i) mp += prof[i]; mp /= 12.0;
            for (int r = 0; r < 12; ++r)
            {
                double num = 0.0, dx = 0.0, dy = 0.0;
                for (int i = 0; i < 12; ++i)
                {
                    const double x = hist[(size_t) ((r + i) % 12)] - mh, y = prof[i] - mp;
                    num += x * y; dx += x * x; dy += y * y;
                }
                const double c = num / std::sqrt (dx * dy + 1.0e-12);
                if (c > bestC) { bestC = c; bestR = r; bestMinor = (mode == 1); }
            }
        }
        root.store (bestR); minor.store (bestMinor); confidence.store ((float) bestC); valid.store (true);
    }

    std::array<double, 12> hist {};
    double fr = 375.0, decay = 0.99993, total = 0.0;
    int counter = 0;
    std::atomic<int> root { 0 };
    std::atomic<bool> minor { false }, valid { false };
    std::atomic<float> confidence { 0.f };
};
} // namespace nct
