#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "NeuroLookAndFeel.h"
#include "../Engine/Analysis.h"

struct RenderView;

namespace ui
{
//============================================================================== Analyse-Karte
class AnalysisCard : public juce::Component
{
public:
    AnalysisCard (juce::String title, juce::String hint, juce::Colour accent);

    void setState (const nd::AnalysisSnapshot& s, bool capturing, float pulse);
    std::function<void()> onCapture, onClear;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title, hint;
    juce::Colour accent;
    juce::TextButton captureBtn, clearBtn;
    nd::AnalysisSnapshot snap;
    bool capturing = false;
    float pulse = 0.0f;
};

//============================================================================== Drag-Chip
// Zieht Dateien per nativem Drag & Drop in Cubase.
class DragChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragChip (juce::String label, juce::Colour c);
    std::function<juce::StringArray()> getFiles;
    void setLabel (const juce::String& l) { label = l; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }

private:
    juce::String label;
    juce::Colour colour;
    bool hover = false, dragging = false;
};

//============================================================================== Stem-Spur
class StemStrip : public juce::Component
{
public:
    StemStrip (int index);
    juce::ToggleButton onBtn;
    juce::Slider gain;
    DragChip chip;

    void setPeaks (const std::vector<float>* p, double prePortion) { peaks = p ? *p : std::vector<float>(); pre = prePortion; repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int index;
    std::vector<float> peaks;
    double pre = 0.0;
    juce::Rectangle<int> laneArea;
};

//============================================================================== Wellenform
class WaveformView : public juce::Component
{
public:
    void setView (std::shared_ptr<const RenderView> v) { view = std::move (v); repaint(); }
    void setPlayhead (double pos01, bool playing) { if (std::abs (pos01 - play) > 1.0e-6 || playing != isPlaying) { play = pos01; isPlaying = playing; repaint(); } }
    void setBusy (bool b, float p) { if (b != busy || std::abs (p - prog) > 0.004f) { busy = b; prog = p; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    std::shared_ptr<const RenderView> view;
    double play = 0.0; bool isPlaying = false;
    bool busy = false; float prog = 0.0f;
};
} // namespace ui
