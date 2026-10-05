#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nct
{
using int64 = std::int64_t;
constexpr double kPi = 3.14159265358979323846;

inline int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }
inline double hzToMidi (double hz) { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
inline double midiToHz (double m)  { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
inline double clampd (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double smoothstep01 (double x) { x = clampd (x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); }

/** Tiny deterministic PRNG (audio-thread safe, no allocation). */
struct XorShift
{
    std::uint32_t s = 0x9E3779B9u;
    float next01()  { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xFFFFFF) / 16777216.0f; }
    float nextPM1() { return next01() * 2.0f - 1.0f; }
};
} // namespace nct
