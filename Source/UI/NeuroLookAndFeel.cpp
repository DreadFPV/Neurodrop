#include "NeuroLookAndFeel.h"

namespace ui
{
using namespace juce;

static Colour accentOf (Component& c)
{
    const auto v = c.getProperties()["accent"];
    return v.isVoid() ? col::accent : Colour ((uint32) (int64) v);
}

NeuroLookAndFeel::NeuroLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, col::bg);
    setColour (ComboBox::backgroundColourId, col::panel2);
    setColour (ComboBox::textColourId, col::text);
    setColour (ComboBox::outlineColourId, col::line);
    setColour (ComboBox::arrowColourId, col::accent);
    setColour (PopupMenu::backgroundColourId, col::panel2);
    setColour (PopupMenu::textColourId, col::text);
    setColour (PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.2f));
    setColour (PopupMenu::highlightedTextColourId, col::accent);
    setColour (Label::textColourId, col::text);
    setColour (Slider::textBoxTextColourId, col::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (TextButton::textColourOffId, col::text);
    setColour (TextButton::textColourOnId, col::bg);
    setColour (ToggleButton::textColourId, col::text);
    setColour (TooltipWindow::backgroundColourId, col::panel2);
    setColour (TooltipWindow::textColourId, col::text);
    setColour (TooltipWindow::outlineColourId, col::line);
}

void NeuroLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float ang = start + pos * (end - start);
    const auto accent = accentOf (s);

    Path track; track.addCentredArc (c.x, c.y, r - 3, r - 3, 0, start, end, true);
    g.setColour (col::line); g.strokePath (track, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));

    Path val; val.addCentredArc (c.x, c.y, r - 3, r - 3, 0, start, ang, true);
    g.setColour (accent.withAlpha (0.25f)); g.strokePath (val, PathStrokeType (10.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (accent); g.strokePath (val, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));

    g.setColour (col::panel2); g.fillEllipse (Rectangle<float> (r * 1.25f, r * 1.25f).withCentre (c));
    g.setColour (col::line); g.drawEllipse (Rectangle<float> (r * 1.25f, r * 1.25f).withCentre (c), 1.0f);
    const auto tip = c.getPointOnCircumference (r * 0.5f, ang);
    g.setColour (col::text); g.drawLine ({ c.getPointOnCircumference (r * 0.15f, ang), tip }, 2.5f);
}

void NeuroLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         Slider::SliderStyle style, Slider& s)
{
    if (style != Slider::LinearHorizontal) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }
    const auto accent = accentOf (s);
    const float cy = y + h * 0.5f;
    const auto track = Rectangle<float> ((float) x, cy - 2.5f, (float) w, 5.0f);
    g.setColour (col::line); g.fillRoundedRectangle (track, 2.5f);
    g.setColour (accent); g.fillRoundedRectangle (track.withRight (pos), 2.5f);
    g.setColour (col::text); g.fillEllipse (Rectangle<float> (12.0f, 12.0f).withCentre ({ pos, cy }));
    g.setColour (accent); g.drawEllipse (Rectangle<float> (12.0f, 12.0f).withCentre ({ pos, cy }), 2.0f);
}

void NeuroLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const auto accent = accentOf (b);
    const bool primary = b.getProperties()["primary"];
    Colour fill = on || primary ? accent : col::panel2;
    if (! b.isEnabled()) fill = col::panel2.withAlpha (0.5f);
    if (over && b.isEnabled()) fill = fill.brighter (0.12f);
    if (down) fill = fill.darker (0.2f);
    g.setColour (fill); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on || primary ? accent.brighter (0.3f) : col::line); g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void NeuroLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    const bool on = b.getToggleState() || (bool) b.getProperties()["primary"];
    g.setColour (! b.isEnabled() ? col::muted.withAlpha (0.6f) : (on ? col::bg : col::text));
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), Justification::centred, 1);
}

void NeuroLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const auto sw = Rectangle<float> (r.getX() + 2, r.getCentreY() - 8, 30, 16);
    const bool on = b.getToggleState();
    const auto accent = accentOf (b);
    g.setColour (on ? accent.withAlpha (0.9f) : col::line.brighter (over ? 0.2f : 0.0f));
    g.fillRoundedRectangle (sw, 8.0f);
    g.setColour (on ? col::bg : col::muted);
    g.fillEllipse (Rectangle<float> (12, 12).withCentre ({ on ? sw.getRight() - 8 : sw.getX() + 8, sw.getCentreY() }));
    g.setColour (b.isEnabled() ? col::text : col::muted);
    g.setFont (FontOptions (13.0f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (40), Justification::centredLeft, 1);
}

void NeuroLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& c)
{
    const auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (col::panel2); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (c.hasKeyboardFocus (true) ? col::accent : col::line); g.drawRoundedRectangle (r, 6.0f, 1.0f);
    Path arrow;
    const float ax = w - 16.0f, ay = h * 0.5f;
    arrow.addTriangle (ax - 5, ay - 2.5f, ax + 5, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (col::accent); g.fillPath (arrow);
}

void NeuroLookAndFeel::positionComboBoxText (ComboBox& c, Label& l)
{
    l.setBounds (8, 1, c.getWidth() - 30, c.getHeight() - 2);
    l.setFont (getComboBoxFont (c));
}

Font NeuroLookAndFeel::getComboBoxFont (ComboBox&)       { return FontOptions (14.0f); }
Font NeuroLookAndFeel::getTextButtonFont (TextButton&, int h) { return FontOptions (jmin (15.0f, h * 0.45f), Font::bold); }
Font NeuroLookAndFeel::getPopupMenuFont()                { return FontOptions (14.0f); }

void NeuroLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (col::panel2);
    g.setColour (col::line); g.drawRect (0, 0, w, h);
}

Label* NeuroLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (FontOptions (12.0f));
    l->setColour (Label::textColourId, col::muted);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::outlineWhenEditingColourId, col::accent);
    l->setColour (TextEditor::backgroundColourId, col::panel2);
    l->setColour (TextEditor::textColourId, col::text);
    l->setColour (TextEditor::highlightColourId, col::accent.withAlpha (0.3f));
    return l;
}
} // namespace ui
