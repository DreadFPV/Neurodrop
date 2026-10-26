#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
inline juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
inline void setAccent (juce::Component& c, juce::Colour a) { c.getProperties().set ("accent", (juce::int64) a.getARGB()); }

namespace col
{
    const juce::Colour bg      { 0xff0b0d12 };
    const juce::Colour panel   { 0xff141821 };
    const juce::Colour panel2  { 0xff1b202b };
    const juce::Colour line    { 0xff2a3140 };
    const juce::Colour text    { 0xffe6e9ef };
    const juce::Colour muted   { 0xff8790a3 };
    const juce::Colour accent  { 0xff39ff88 };   // Neuro-Gruen
    const juce::Colour accent2 { 0xff29d3ff };   // Cyan
    const juce::Colour warn    { 0xffff3b5c };
    inline juce::Colour stem (int i)
    {
        static const juce::uint32 c[] = { 0xffff5c5c, 0xffffb347, 0xffffe066, 0xff39ff88, 0xff29d3ff, 0xffc38bff };
        return juce::Colour (c[i % 6]);
    }
}

class NeuroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeuroLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};
} // namespace ui
