#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "NctLookAndFeel.h"
#include "../DSP/CorrectionEngine.h"
#include "../DSP/Scales.h"

namespace nctui
{
/** Compact pitch-curve display: input (grey), detected centre (white), corrected output (accent). */
class PitchView : public juce::Component
{
public:
    std::function<nct::CorrectionEngine*()> engineGetter;
    std::function<void (int& root, std::uint16_t& mask)> scaleGetter;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (r, 8.f);
        auto area = r.reduced (34.f, 8.f);

        constexpr int kPts = 400;
        float in[kPts], ce[kPts], out[kPts];
        for (int i = 0; i < kPts; ++i) in[i] = ce[i] = out[i] = nct::PitchHistory::kNone;
        if (auto* e = engineGetter ? engineGetter() : nullptr) e->history.snapshot (in, ce, out, kPts);

        // vertical range (eased)
        float lo = 1.0e9f, hi = -1.0e9f;
        for (int i = 0; i < kPts; ++i)
            for (float v : { in[i], out[i] })
                if (v > -500.f) { lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
        if (hi < lo) { lo = 57.f; hi = 69.f; }
        float span = juce::jmax (12.f, hi - lo + 4.f), mid = 0.5f * (hi + lo);
        viewMid  += (mid - viewMid) * 0.15f;
        viewSpan += (span - viewSpan) * 0.15f;
        const float vLo = viewMid - viewSpan * 0.5f, vHi = viewMid + viewSpan * 0.5f;
        auto yOf = [&] (float m) { return area.getBottom() - (m - vLo) / (vHi - vLo) * area.getHeight(); };
        auto xOf = [&] (int i) { return area.getX() + (float) i / (float) (kPts - 1) * area.getWidth(); };

        // scale lines
        int root = 0; std::uint16_t mask = 0x0FFF;
        if (scaleGetter) scaleGetter (root, mask);
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        g.setFont (11.f);
        for (int n = (int) std::floor (vLo); n <= (int) std::ceil (vHi); ++n)
        {
            const float y = yOf ((float) n);
            if (y < area.getY() - 1.f || y > area.getBottom() + 1.f) continue;
            const bool allowed = nct::isAllowed (mask, root, n);
            g.setColour (allowed ? accent.withAlpha (0.28f) : grid.withAlpha (0.35f));
            g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
            if (allowed)
            {
                g.setColour (n % 12 == root ? accent : dim);
                g.drawText (juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1),
                            juce::Rectangle<float> (2.f, y - 7.f, 32.f, 14.f), juce::Justification::centredRight);
            }
        }

        auto drawCurve = [&] (const float* d, juce::Colour c, float thick)
        {
            juce::Path p; bool pen = false;
            for (int i = 0; i < kPts; ++i)
            {
                if (d[i] < -500.f) { pen = false; continue; }
                const float x = xOf (i), y = juce::jlimit (area.getY(), area.getBottom(), yOf (d[i]));
                if (! pen) { p.startNewSubPath (x, y); pen = true; } else p.lineTo (x, y);
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        drawCurve (in,  dim.withAlpha (0.8f), 1.4f);
        drawCurve (ce,  text.withAlpha (0.55f), 1.2f);
        drawCurve (out, accent, 2.2f);
    }

private:
    float viewMid = 62.f, viewSpan = 14.f;
};

/** Horizontal correction meter (+-100 cents). */
class CorrectionMeter : public juce::Component
{
public:
    void setValue (float cents) { value += (cents - value) * 0.35f; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (r, 8.f);
        auto bar = r.reduced (14.f, 0.f).withTrimmedTop (r.getHeight() * 0.50f).withHeight (8.f).withY (r.getCentreY() + 2.f);
        g.setColour (grid);
        g.fillRoundedRectangle (bar, 4.f);
        const float cx = bar.getCentreX();
        const float v = juce::jlimit (-100.f, 100.f, value) / 100.f;
        const float xEnd = cx + v * bar.getWidth() * 0.5f;
        g.setColour (std::abs (value) < 10.f ? accent : accent2);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (cx, xEnd), bar.getY(), std::abs (xEnd - cx) + 1.f, bar.getHeight()), 4.f);
        g.setColour (text);
        g.fillRect (cx - 1.f, bar.getY() - 3.f, 2.f, bar.getHeight() + 6.f);

        g.setFont (12.f);
        g.setColour (dim);
        g.drawText ("CORRECTION", r.reduced (14.f, 6.f).removeFromTop (14.f), juce::Justification::centredLeft);
        g.setColour (text);
        g.drawText (juce::String (value, 0) + " cents", r.reduced (14.f, 6.f).removeFromTop (14.f), juce::Justification::centredRight);
    }

private:
    float value = 0.f;
};
} // namespace nctui
