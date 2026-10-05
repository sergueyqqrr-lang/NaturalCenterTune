#pragma once
#include <array>
#include <cstdint>
#include <initializer_list>
#include "Common.h"

namespace nct
{
/** bit i of `mask` = semitone i above the root belongs to the scale. */
struct ScaleDef { const char* name; std::uint16_t mask; };

inline std::uint16_t scaleMask (std::initializer_list<int> degrees)
{
    unsigned m = 0;
    for (int d : degrees) m |= (1u << d);
    return (std::uint16_t) m;
}

inline const std::array<ScaleDef, 12>& scaleTable()
{
    static const std::array<ScaleDef, 12> table {{
        { "Chromatic",         0x0FFF },
        { "Major",             scaleMask ({ 0, 2, 4, 5, 7, 9, 11 }) },
        { "Natural Minor",     scaleMask ({ 0, 2, 3, 5, 7, 8, 10 }) },
        { "Harmonic Minor",    scaleMask ({ 0, 2, 3, 5, 7, 8, 11 }) },
        { "Melodic Minor",     scaleMask ({ 0, 2, 3, 5, 7, 9, 11 }) },
        { "Dorian",            scaleMask ({ 0, 2, 3, 5, 7, 9, 10 }) },
        { "Phrygian",          scaleMask ({ 0, 1, 3, 5, 7, 8, 10 }) },
        { "Lydian",            scaleMask ({ 0, 2, 4, 6, 7, 9, 11 }) },
        { "Mixolydian",        scaleMask ({ 0, 2, 4, 5, 7, 9, 10 }) },
        { "Major Pentatonic",  scaleMask ({ 0, 2, 4, 7, 9 }) },
        { "Minor Pentatonic",  scaleMask ({ 0, 3, 5, 7, 10 }) },
        { "Blues",             scaleMask ({ 0, 3, 5, 6, 7, 10 }) }
    }};
    return table;
}

inline bool isAllowed (std::uint16_t mask, int root, int midiNote)
{
    const int pc = ((midiNote - root) % 12 + 12) % 12;
    return ((mask >> pc) & 1) != 0;
}

/** Nearest scale note (integer MIDI) to a fractional MIDI value. */
inline int nearestAllowed (double midi, int root, std::uint16_t mask)
{
    const int base = (int) std::floor (midi + 0.5);
    int best = base;
    double bestD = 1.0e9;
    for (int n = base - 7; n <= base + 7; ++n)
    {
        if (! isAllowed (mask, root, n)) continue;
        const double d = std::fabs (midi - (double) n);
        if (d < bestD) { bestD = d; best = n; }
    }
    return best;
}
} // namespace nct
