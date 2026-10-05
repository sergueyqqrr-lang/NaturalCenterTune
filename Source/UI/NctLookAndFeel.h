#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace nctui
{
const juce::Colour bg      { 0xFF0E1217 };
const juce::Colour panel   { 0xFF161C24 };
const juce::Colour panel2  { 0xFF1D2530 };
const juce::Colour accent  { 0xFF37D6B5 };
const juce::Colour accent2 { 0xFFFFB454 };
const juce::Colour text    { 0xFFE6EDF3 };
const juce::Colour dim     { 0xFF8B98A5 };
const juce::Colour grid    { 0xFF2A3440 };

class NctLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NctLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::ComboBox::backgroundColourId, panel2);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, grid);
        setColour (juce::ComboBox::arrowColourId, accent);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::TextButton::buttonColourId, panel2);
        setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.85f));
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, bg);
        setColour (juce::Slider::thumbColourId, accent);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float arcR = radius - 3.f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool big = s.getProperties().getWithDefault ("big", false);
        const auto col = big ? accent2 : accent;

        // body
        g.setGradientFill (juce::ColourGradient (panel2.brighter (0.1f), c.x, c.y - radius, panel, c.x, c.y + radius, false));
        g.fillEllipse (c.x - radius + 6.f, c.y - radius + 6.f, (radius - 6.f) * 2.f, (radius - 6.f) * 2.f);

        // track
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.f, startAngle, endAngle, true);
        g.setColour (grid);
        g.strokePath (track, juce::PathStrokeType (big ? 6.f : 4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc
        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.f, startAngle, angle, true);
        if (big) { g.setColour (col.withAlpha (0.22f)); g.strokePath (val, juce::PathStrokeType (12.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        g.setColour (col);
        g.strokePath (val, juce::PathStrokeType (big ? 6.f : 4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // pointer
        juce::Path ptr;
        const float inner = radius * 0.30f, outer = radius * 0.62f;
        ptr.startNewSubPath (c.x + std::sin (angle) * inner, c.y - std::cos (angle) * inner);
        ptr.lineTo (c.x + std::sin (angle) * outer, c.y - std::cos (angle) * outer);
        g.setColour (text);
        g.strokePath (ptr, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        (void) w; (void) h;
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           const juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour (grid);
        g.fillRoundedRectangle ((float) x, cy - 2.f, (float) w, 4.f, 2.f);
        g.setColour (accent);
        g.fillRoundedRectangle ((float) x, cy - 2.f, pos - (float) x, 4.f, 2.f);
        g.setColour (text);
        g.fillEllipse (pos - 6.f, cy - 6.f, 12.f, 12.f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto col = b.getToggleState() ? findColour (juce::TextButton::buttonOnColourId) : findColour (juce::TextButton::buttonColourId);
        if (hover) col = col.brighter (0.12f);
        if (down)  col = col.darker (0.15f);
        g.setColour (col);
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (grid);
        g.drawRoundedRectangle (r, 6.f, 1.f);
    }
};
} // namespace nctui
