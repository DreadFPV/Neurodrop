#include "Panels.h"
#include "../PluginProcessor.h"

namespace ui
{
using namespace juce;

//============================================================================== AnalysisCard
AnalysisCard::AnalysisCard (String t, String h, Colour a) : title (std::move (t)), hint (std::move (h)), accent (a)
{
    captureBtn.setButtonText ("AUFNEHMEN");
    captureBtn.setTooltip (u8 ("Spiele die Stelle in Cubase ab und klicke hier – erneut klicken zum Stoppen."));
    setAccent (captureBtn, col::warn);
    captureBtn.onClick = [this] { if (onCapture) onCapture(); };
    clearBtn.setButtonText (u8 ("LÖSCHEN"));
    clearBtn.onClick = [this] { if (onClear) onClear(); };
    addAndMakeVisible (captureBtn);
    addAndMakeVisible (clearBtn);
}

void AnalysisCard::setState (const nd::AnalysisSnapshot& s, bool cap, float p)
{
    snap = s; capturing = cap; pulse = p;
    captureBtn.setButtonText (cap ? "STOP" : "AUFNEHMEN");
    captureBtn.setToggleState (cap, dontSendNotification);
    clearBtn.setEnabled (s.valid && ! cap);
    repaint();
}

void AnalysisCard::resized()
{
    auto r = getLocalBounds().reduced (14, 12);
    auto top = r.removeFromTop (28);
    clearBtn.setBounds (top.removeFromRight (78));
    top.removeFromRight (8);
    captureBtn.setBounds (top.removeFromRight (110));
}

void AnalysisCard::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (col::panel); g.fillRoundedRectangle (r, 10.0f);
    const Colour border = capturing ? col::warn.withAlpha (0.45f + 0.55f * pulse) : (snap.valid ? accent.withAlpha (0.6f) : col::line);
    g.setColour (border); g.drawRoundedRectangle (r, 10.0f, capturing ? 2.0f : 1.0f);

    auto area = getLocalBounds().reduced (16, 12);
    auto head = area.removeFromTop (28);
    g.setColour (accent); g.setFont (FontOptions (15.0f, Font::bold));
    g.drawText (title, head.removeFromLeft (200), Justification::centredLeft);
    if (capturing)
    {
        g.setColour (col::warn.withAlpha (0.5f + 0.5f * pulse));
        g.fillEllipse (Rectangle<float> (9, 9).withCentre ({ (float) head.getX() + 6.0f, (float) head.getCentreY() }));
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText ("LIVE", head.withTrimmedLeft (16), Justification::centredLeft);
    }

    auto bandsArea = area.removeFromRight (150).reduced (0, 6);
    area.removeFromTop (4);

    if (! snap.valid)
    {
        g.setColour (col::muted); g.setFont (FontOptions (13.0f));
        g.drawFittedText (capturing ? u8 ("Analysiere … Stelle weiter abspielen.") : hint, area.removeFromTop (60), Justification::topLeft, 4);
        return;
    }

    g.setColour (col::text); g.setFont (FontOptions (30.0f, Font::bold));
    g.drawText (String (nd::noteName (snap.keyTonic)) + (snap.keyMinor ? " Moll" : " Dur"), area.removeFromTop (38), Justification::centredLeft);

    g.setFont (FontOptions (13.0f));
    auto line = [&] (const String& s, Colour c) { g.setColour (c); g.drawText (s, area.removeFromTop (19), Justification::centredLeft); };
    String l1 = "A4 " + String (snap.tuningA4, 1) + " Hz";
    if (snap.kickHz > 0) l1 << u8 ("  ·  Kick ") << String (snap.kickHz, 1) << " Hz";
    if (snap.bassPc >= 0) l1 << u8 ("  ·  Bass ") << nd::noteName (snap.bassPc);
    line (l1, col::text);
    line ("RMS " + String (snap.rmsDb, 1) + u8 (" dB  ·  Energie ") + String (roundToInt (snap.energy * 100)) + u8 ("%  ·  Sicherheit ")
          + String (roundToInt (snap.keyConfidence * 100)) + "%", col::muted);
    String l3;
    if (snap.ppqStart >= 0 && snap.ppqEnd > snap.ppqStart)
        l3 << "Takt " << (int) (snap.ppqStart / 4.0) + 1 << u8 ("–") << (int) std::ceil (snap.ppqEnd / 4.0) + 1 << u8 ("  ·  ");
    if (snap.hostBpm > 0) l3 << String (snap.hostBpm, 1) << u8 (" BPM  ·  ");
    l3 << String (snap.seconds, 1) << " s analysiert";
    line (l3, col::muted);

    // Spektrum-Balken
    float mx = -200; for (auto v : snap.bandsDb) mx = jmax (mx, v);
    const int nb = nd::kNumBands;
    const float bw = bandsArea.getWidth() / (float) nb;
    auto barsArea = bandsArea.toFloat().withTrimmedBottom (14);
    for (int i = 0; i < nb; ++i)
    {
        const float v = jlimit (0.0f, 1.0f, 1.0f - (mx - snap.bandsDb[(size_t) i]) / 40.0f);
        auto bar = Rectangle<float> (barsArea.getX() + i * bw + 2, barsArea.getY(), bw - 4, barsArea.getHeight());
        g.setColour (col::panel2); g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (accent.withAlpha (0.35f + 0.65f * v));
        g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * v), 2.0f);
    }
    g.setColour (col::muted); g.setFont (FontOptions (10.0f));
    g.drawText ("Sub", bandsArea.removeFromBottom (12).removeFromLeft (40), Justification::left);
    g.drawText ("Air", Rectangle<int> (bandsArea.getRight() - 40, bandsArea.getBottom(), 40, 12), Justification::right);
}

//============================================================================== DragChip
DragChip::DragChip (String l, Colour c) : label (std::move (l)), colour (c)
{
    setMouseCursor (MouseCursor::DraggingHandCursor);
    setTooltip (u8 ("Gedrückt halten und in Cubase auf eine Spur ziehen"));
}

void DragChip::paint (Graphics& g)
{
    const bool has = getFiles && ! getFiles().isEmpty();
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (has ? colour.withAlpha (hover ? 0.35f : 0.2f) : col::panel2);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (has ? colour : col::line);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.2f);
    // Pfeil-Icon
    const float cx = r.getX() + 16, cy = r.getCentreY();
    Path p; p.startNewSubPath (cx, cy - 6); p.lineTo (cx, cy + 5);
    p.startNewSubPath (cx - 4, cy + 1); p.lineTo (cx, cy + 5); p.lineTo (cx + 4, cy + 1);
    g.setColour (has ? colour : col::muted);
    g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (has ? col::text : col::muted);
    g.setFont (FontOptions (12.5f, Font::bold));
    g.drawFittedText (label, getLocalBounds().withTrimmedLeft (28).withTrimmedRight (8), Justification::centredLeft, 1);
}

void DragChip::mouseDrag (const MouseEvent& e)
{
    if (dragging || ! getFiles || e.getDistanceFromDragStart() < 4) return;
    auto files = getFiles();
    if (files.isEmpty()) return;
    dragging = true;
    DragAndDropContainer::performExternalDragDropOfFiles (files, false, this, [this] { dragging = false; });
}

//============================================================================== StemStrip
StemStrip::StemStrip (int i) : chip ("ziehen", col::stem (i)), index (i)
{
    onBtn.setButtonText (nd::stemName (i));
    setAccent (onBtn, col::stem (i));
    gain.setSliderStyle (Slider::LinearHorizontal);
    gain.setTextBoxStyle (Slider::TextBoxRight, false, 52, 18);
    gain.setTextValueSuffix (" dB");
    gain.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    gain.setColour (Slider::textBoxTextColourId, col::muted);
    setAccent (gain, col::stem (i));
    addAndMakeVisible (onBtn);
    addAndMakeVisible (gain);
    addAndMakeVisible (chip);
}

void StemStrip::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    onBtn.setBounds (r.removeFromTop (24));
    laneArea = r.removeFromTop (26).reduced (0, 2);
    gain.setBounds (r.removeFromTop (24));
    r.removeFromTop (2);
    chip.setBounds (r.removeFromTop (24));
}

void StemStrip::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (col::panel2.withAlpha (0.6f)); g.fillRoundedRectangle (r, 8.0f);
    const bool on = onBtn.getToggleState();
    auto lane = laneArea.toFloat();
    g.setColour (col::bg); g.fillRoundedRectangle (lane, 3.0f);
    if (peaks.empty()) return;
    const float mid = lane.getCentreY(), half = lane.getHeight() * 0.45f;
    float mx = 1.0e-6f; for (auto p : peaks) mx = jmax (mx, p);
    Path env; env.startNewSubPath (lane.getX(), mid);
    const int n = (int) peaks.size();
    for (int i = 0; i < n; ++i) env.lineTo (lane.getX() + lane.getWidth() * i / (float) (n - 1), mid - half * peaks[(size_t) i] / mx);
    for (int i = n - 1; i >= 0; --i) env.lineTo (lane.getX() + lane.getWidth() * i / (float) (n - 1), mid + half * peaks[(size_t) i] / mx);
    env.closeSubPath();
    g.setColour (col::stem (index).withAlpha (on ? 0.85f : 0.2f));
    g.fillPath (env);
    if (pre > 0)
    {
        g.setColour (col::bg.withAlpha (0.5f));
        g.fillRect (lane.withWidth (lane.getWidth() * (float) pre));
    }
}

//============================================================================== WaveformView
void WaveformView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::bg); g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::line); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    auto a = r.reduced (8.0f, 14.0f);

    if (view == nullptr || view->total <= 0)
    {
        g.setColour (col::muted); g.setFont (FontOptions (14.0f));
        g.drawFittedText (u8 ("Noch kein Drop.\n1) VORHER aufnehmen   2) optional NACHHER   3) GENERIEREN"), getLocalBounds(), Justification::centred, 3);
    }
    else
    {
        const auto& v = *view;
        const float preX = a.getX() + a.getWidth() * (float) v.pre / (float) v.total;
        if (v.pre > 0)
        {
            g.setColour (col::accent2.withAlpha (0.07f));
            g.fillRect (Rectangle<float> (a.getX(), r.getY() + 1, preX - a.getX(), r.getHeight() - 2));
            g.setColour (col::accent2.withAlpha (0.8f)); g.setFont (FontOptions (10.0f, Font::bold));
            g.drawText ("VORLAUF", Rectangle<float> (a.getX() + 2, r.getY() + 1, 80, 12), Justification::left);
        }
        // Taktlinien
        const double barLen = (double) (v.total - v.pre) / jmax (1, v.bars);
        for (int b = 0; b <= v.bars; ++b)
        {
            const float x = a.getX() + a.getWidth() * (float) ((v.pre + b * barLen) / v.total);
            const bool strong = b % 4 == 0;
            g.setColour (strong ? col::line.brighter (0.4f) : col::line);
            g.drawVerticalLine ((int) x, r.getY() + 2, r.getBottom() - 2);
            if (strong && b < v.bars && v.bars <= 16)
            {
                g.setColour (col::muted); g.setFont (FontOptions (10.0f));
                g.drawText (String (b + 1), Rectangle<float> (x + 3, r.getY() + 1, 30, 12), Justification::left);
            }
        }
        // Huellkurve Mix
        const auto& p = v.mixPeaks;
        const int n = (int) p.size();
        if (n > 1)
        {
            const float mid = a.getCentreY(), half = a.getHeight() * 0.5f;
            Path env; env.startNewSubPath (a.getX(), mid);
            for (int i = 0; i < n; ++i) env.lineTo (a.getX() + a.getWidth() * i / (float) (n - 1), mid - half * jmin (1.0f, p[(size_t) i] * 1.1f));
            for (int i = n - 1; i >= 0; --i) env.lineTo (a.getX() + a.getWidth() * i / (float) (n - 1), mid + half * jmin (1.0f, p[(size_t) i] * 1.1f));
            env.closeSubPath();
            g.setGradientFill (ColourGradient (col::accent, a.getX(), mid, col::accent2, a.getRight(), mid, false));
            g.fillPath (env);
        }
        g.setColour (col::accent2); g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (u8 ("→ PSY"), Rectangle<float> (r.getRight() - 50, r.getY() + 1, 46, 12), Justification::right);

        if (isPlaying || play > 0.0)
        {
            const float x = a.getX() + a.getWidth() * (float) play;
            g.setColour (col::text); g.drawLine (x, r.getY() + 2, x, r.getBottom() - 2, 1.5f);
        }
    }

    if (busy)
    {
        g.setColour (col::bg.withAlpha (0.75f)); g.fillRoundedRectangle (r, 8.0f);
        auto bar = r.withSizeKeepingCentre (r.getWidth() * 0.6f, 8.0f);
        g.setColour (col::line); g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (col::accent); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * prog), 4.0f);
        g.setColour (col::text); g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText (u8 ("Rendere Drop … ") + String (roundToInt (prog * 100)) + "%", bar.translated (0, -22).withHeight (18), Justification::centred);
    }
}
} // namespace ui
