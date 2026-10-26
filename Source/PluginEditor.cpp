#include "PluginEditor.h"

using namespace juce;
using ui::u8;
namespace col = ui::col;

namespace
{
constexpr int kW = 1040, kH = 790;

void styleSmallLabel (Label& l, const String& t)
{
    l.setText (t, dontSendNotification);
    l.setFont (FontOptions (11.0f, Font::bold));
    l.setColour (Label::textColourId, col::muted);
    l.setJustificationType (Justification::centredLeft);
}
} // namespace

NeuroDropEditor::NeuroDropEditor (NeuroDropProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      beforeCard (u8 ("VORHER"), u8 ("Spiele in Cubase den Teil VOR dem Drop (Breakdown / Build-up) und klicke AUFNEHMEN. "
                                    "Tonart, Tuning, Kick, Spektrum und Lautheit werden erkannt und im Projekt gespeichert."), col::accent),
      afterCard (u8 ("NACHHER"), u8 ("Optional: Teil NACH dem Drop aufnehmen. Der Übergang zurück zu Psy wird darauf abgestimmt."), col::accent2)
{
    setLookAndFeel (&lnf);

    beforeCard.onCapture = [this] { if (proc.getCaptureSlot() == 0) proc.stopCapture(); else proc.startCapture (0); };
    afterCard.onCapture  = [this] { if (proc.getCaptureSlot() == 1) proc.stopCapture(); else proc.startCapture (1); };
    beforeCard.onClear = [this] { proc.clearSnapshot (0); };
    afterCard.onClear  = [this] { proc.clearSnapshot (1); };
    addAndMakeVisible (beforeCard);
    addAndMakeVisible (afterCard);

    addCombo (styleBox,   styleLbl,   "STIL",        ParamID::style);
    addCombo (grooveBox,  grooveLbl,  "GROOVE",      ParamID::groove);
    addCombo (tempoBox,   tempoLbl,   "TEMPO-MODUS", ParamID::tempoMode);
    addCombo (barsBox,    barsLbl,    "TAKTE (DROP)",ParamID::bars);
    addCombo (preRollBox, preRollLbl, "VORLAUF (HOST-TAKTE)", ParamID::preRoll);
    addCombo (keyBox,     keyLbl,     "TONART",      ParamID::key);
    addCombo (scaleBox,   scaleLbl,   "SKALA",       ParamID::scale);
    addCombo (tuningBox,  tuningLbl,  "TUNING",      ParamID::tuning);

    bpmSlider.setSliderStyle (Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (Slider::TextBoxRight, false, 64, 20);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    bpmSlider.setColour (Slider::textBoxTextColourId, col::accent2);
    addAndMakeVisible (bpmSlider);
    styleSmallLabel (bpmLbl, "ZIEL-BPM (DNB-TEMPO)");
    addAndMakeVisible (bpmLbl);
    sAtt.push_back (std::make_unique<SA> (proc.apvts, ParamID::targetBpm, bpmSlider));

    addKnob (intensityKnob,  intensityLbl,  u8 ("INTENSITÄT"), ParamID::intensity,  col::accent);
    addKnob (aggressionKnob, aggressionLbl, "AGGRESSION",      ParamID::aggression, col::warn);
    addKnob (matchKnob,      matchLbl,      "FIT-IN",          ParamID::match,      col::accent2);
    intensityKnob.setTooltip (u8 ("Dichte: Ghost-Snares, Hat-16tel, Bass-Noten, FX-Akzente"));
    aggressionKnob.setTooltip (u8 ("Härte: FM-Index, Distortion, Growl-Anteil"));
    matchKnob.setTooltip (u8 ("Wie stark Klangbalance (EQ) an den analysierten Psy-Track angeglichen wird"));

    addToggle (gridLockBtn,  "Grid-Lock",               ParamID::gridLock);
    addToggle (transOutBtn,  u8 ("Übergang zurück"),    ParamID::transOut);
    addToggle (loudMatchBtn, "Lautheit angleichen",     ParamID::loudMatch);
    addToggle (muteInputBtn, u8 ("Psy stumm beim Hören"), ParamID::muteInput);
    gridLockBtn.setTooltip (u8 ("BPM leicht anpassen, damit der Drop exakt auf Host-Takte (bzw. Beats) passt"));
    transOutBtn.setTooltip (u8 ("Letzte(r) Takt(e): Snare-Roll, Bass-Dive, Riser + Reverse-Swell zurück in den Psy-Teil"));

    for (int i = 0; i < nd::NumStems; ++i)
    {
        auto* s = strips.add (new ui::StemStrip (i));
        addAndMakeVisible (s);
        sAtt.push_back (std::make_unique<SA> (proc.apvts, ParamID::gain (i), s->gain));
        bAtt.push_back (std::make_unique<BA> (proc.apvts, ParamID::on (i), s->onBtn));
        s->chip.getFiles = [this, i] { return filesFor (i); };
        s->chip.setLabel (String (nd::stemName (i)) + ".wav ziehen");
    }

    addAndMakeVisible (wave);

    generateBtn.setButtonText ("GENERIEREN");
    generateBtn.getProperties().set ("primary", true);
    generateBtn.setTooltip (u8 ("Drop mit aktuellen Einstellungen rendern (gleicher Seed = gleiches Ergebnis)"));
    generateBtn.onClick = [this] { proc.generate (false); };
    rerollBtn.setButtonText (u8 ("NEU WÜRFELN"));
    rerollBtn.setTooltip (u8 ("Neuer Seed: andere Patterns, Motive und Sounds"));
    rerollBtn.onClick = [this] { proc.generate (true); };
    previewBtn.setButtonText (u8 ("VORHÖREN"));
    previewBtn.setClickingTogglesState (false);
    previewBtn.onClick = [this] { proc.setPreview (! proc.isPreviewing() || proc.isContextArmed()); };
    contextBtn.setButtonText ("IM KONTEXT");
    contextBtn.setTooltip (u8 ("Spielt den Drop synchron zu Cubase genau dort ab, wo die VORHER-Aufnahme endete. "
                               "Psy wird ab Drop-Start stummgeschaltet (Schalter 'Psy stumm')."));
    ui::setAccent (contextBtn, col::accent2);
    contextBtn.onClick = [this] { proc.setContextPreview (! proc.isContextArmed()); };
    folderBtn.setButtonText ("ORDNER");
    folderBtn.setTooltip ("Render-Ordner anzeigen");
    folderBtn.onClick = [this]
    {
        if (auto v = proc.getView(); v && v->files.folder.isDirectory()) v->files.folder.revealToUser();
        else nd::defaultRenderRoot().revealToUser();
    };
    for (auto* b : { &generateBtn, &rerollBtn, &previewBtn, &contextBtn, &folderBtn }) addAndMakeVisible (b);

    allChip.getFiles = [this] { return allFiles(); };
    addAndMakeVisible (allChip);

    infoLbl.setFont (FontOptions (13.0f));
    infoLbl.setColour (Label::textColourId, col::accent2);
    summaryLbl.setFont (FontOptions (12.0f));
    summaryLbl.setColour (Label::textColourId, col::muted);
    summaryLbl.setJustificationType (Justification::topLeft);
    addAndMakeVisible (infoLbl);
    addAndMakeVisible (summaryLbl);

    setSize (kW, kH);
    startTimerHz (24);
}

NeuroDropEditor::~NeuroDropEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void NeuroDropEditor::addCombo (ComboBox& c, Label& l, const String& title, const char* paramId)
{
    if (auto* p = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
        c.addItemList (p->choices, 1);
    addAndMakeVisible (c);
    styleSmallLabel (l, title);
    addAndMakeVisible (l);
    cAtt.push_back (std::make_unique<CA> (proc.apvts, paramId, c));
}

void NeuroDropEditor::addKnob (Slider& s, Label& l, const String& title, const char* paramId, Colour accent)
{
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    ui::setAccent (s, accent);
    addAndMakeVisible (s);
    styleSmallLabel (l, title);
    l.setJustificationType (Justification::centred);
    addAndMakeVisible (l);
    sAtt.push_back (std::make_unique<SA> (proc.apvts, paramId, s));
}

void NeuroDropEditor::addToggle (ToggleButton& t, const String& text, const char* paramId)
{
    t.setButtonText (text);
    addAndMakeVisible (t);
    bAtt.push_back (std::make_unique<BA> (proc.apvts, paramId, t));
}

StringArray NeuroDropEditor::filesFor (int stem) const
{
    StringArray a;
    if (shownView && shownView->files.ok())
    {
        const auto& f = shownView->files.stems[(size_t) stem];
        if (f.existsAsFile()) a.add (f.getFullPathName());
    }
    return a;
}

StringArray NeuroDropEditor::allFiles() const
{
    StringArray a;
    for (int i = 0; i < nd::NumStems; ++i) a.addArray (filesFor (i));
    return a;
}

//==============================================================================
void NeuroDropEditor::paint (Graphics& g)
{
    g.fillAll (col::bg);

    // Header
    auto h = headerArea;
    g.setColour (col::accent); g.setFont (FontOptions (26.0f, Font::bold));
    g.drawText ("NEURODROP", h.removeFromLeft (190).withTrimmedLeft (18), Justification::centredLeft);
    g.setColour (col::muted); g.setFont (FontOptions (12.5f));
    g.drawText (u8 ("DnB / Neurofunk Drop-Generator für Psy-Tracks  ·  justcreateit.ch"), h.removeFromLeft (420), Justification::centredLeft);

    // Status rechts: Quelle, Pegel, Host
    auto st = headerArea.withLeft (headerArea.getRight() - 420).reduced (16, 16);
    const bool sc = proc.isSidechainActive();
    const float lvl = jlimit (0.0f, 1.0f, (float) (Decibels::gainToDecibels (proc.getInputLevel(), -60.0f) + 60.0f) / 60.0f);
    auto meter = st.removeFromRight (90).toFloat().withSizeKeepingCentre (90, 8);
    g.setColour (col::panel2); g.fillRoundedRectangle (meter, 4.0f);
    g.setColour (lvl > 0.95f ? col::warn : col::accent); g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * lvl), 4.0f);
    st.removeFromRight (10);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.setColour (sc ? col::accent2 : col::muted);
    g.drawText (sc ? "SIDECHAIN" : "MAIN-INPUT", st.removeFromRight (90), Justification::centredRight);
    g.setColour (col::muted);
    const double bpm = proc.getHostBpm();
    g.drawText ((proc.isHostPlaying() ? u8 ("▶ ") : u8 ("■ ")) + (bpm > 0 ? String (bpm, 1) + " BPM" : String ("Host ?")), st, Justification::centredRight);

    // Panels
    for (auto r : { dropPanel, stemPanel, bottomPanel })
    {
        g.setColour (col::panel); g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (col::line); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }
    g.setColour (col::text); g.setFont (FontOptions (14.0f, Font::bold));
    g.drawText ("DROP", dropPanel.getX() + 16, dropPanel.getY() + 8, 200, 20, Justification::centredLeft);
    g.drawText ("STEMS", stemPanel.getX() + 16, stemPanel.getY() + 6, 200, 18, Justification::centredLeft);
    g.setColour (col::muted); g.setFont (FontOptions (11.5f));
    g.drawText (u8 ("Einzeln oder alle zusammen per Drag & Drop in Cubase – Dateistart = Drop-Start minus Vorlauf"),
                stemPanel.getX() + 80, stemPanel.getY() + 6, 600, 18, Justification::centredLeft);
}

void NeuroDropEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (60);
    r.reduce (16, 0);

    // Analyse-Karten
    auto cards = r.removeFromTop (178);
    beforeCard.setBounds (cards.removeFromLeft ((cards.getWidth() - 16) / 2));
    cards.removeFromLeft (16);
    afterCard.setBounds (cards);
    r.removeFromTop (12);

    // Drop-Panel
    dropPanel = r.removeFromTop (214);
    {
        auto p = dropPanel.reduced (16, 0).withTrimmedTop (34);
        auto knobs = p.removeFromRight (300);
        auto row1 = p.removeFromTop (54), row2 = p.removeFromTop (54);
        auto place = [] (Rectangle<int>& row, ComboBox& c, Label& l)
        {
            auto cell = row.removeFromLeft (150);
            row.removeFromLeft (14);
            l.setBounds (cell.removeFromTop (18));
            c.setBounds (cell.removeFromTop (30));
        };
        place (row1, styleBox, styleLbl);   place (row1, grooveBox, grooveLbl);
        place (row1, tempoBox, tempoLbl);   place (row1, barsBox, barsLbl);
        place (row2, preRollBox, preRollLbl); place (row2, keyBox, keyLbl);
        place (row2, scaleBox, scaleLbl);   place (row2, tuningBox, tuningLbl);

        auto row3 = p.removeFromTop (44);
        auto bpmCell = row3.removeFromLeft (310);
        bpmLbl.setBounds (bpmCell.removeFromTop (18));
        bpmSlider.setBounds (bpmCell.removeFromTop (24));
        row3.removeFromLeft (18);
        gridLockBtn.setBounds (row3.removeFromLeft (110).withTrimmedTop (16));
        transOutBtn.setBounds (row3.removeFromLeft (150).withTrimmedTop (16));
        infoLbl.setBounds (p.removeFromTop (24));

        knobs.removeFromTop (4);
        auto kRow = knobs.removeFromTop (110);
        for (auto pr : { std::pair<Slider*, Label*> { &intensityKnob, &intensityLbl }, { &aggressionKnob, &aggressionLbl }, { &matchKnob, &matchLbl } })
        {
            auto cell = kRow.removeFromLeft (100);
            pr.first->setBounds (cell.removeFromTop (86).reduced (6, 0));
            pr.second->setBounds (cell.removeFromTop (18));
        }
        knobs.removeFromTop (6);
        loudMatchBtn.setBounds (knobs.removeFromTop (26).withTrimmedLeft (20));
        muteInputBtn.setBounds (knobs.removeFromTop (26).withTrimmedLeft (20));
    }
    r.removeFromTop (12);

    // Stems
    stemPanel = r.removeFromTop (150);
    {
        auto p = stemPanel.reduced (10, 0).withTrimmedTop (26).withTrimmedBottom (6);
        const int w = p.getWidth() / nd::NumStems;
        for (auto* s : strips) s->setBounds (p.removeFromLeft (w));
    }
    r.removeFromTop (12);

    // Unten: Wellenform + Aktionen
    bottomPanel = r.removeFromTop (r.getHeight() - 12);
    {
        auto p = bottomPanel.reduced (14, 12);
        auto right = p.removeFromRight (320);
        p.removeFromRight (14);
        summaryLbl.setBounds (p.removeFromBottom (20));
        p.removeFromBottom (4);
        wave.setBounds (p);

        generateBtn.setBounds (right.removeFromTop (40));
        right.removeFromTop (8);
        auto row = right.removeFromTop (32);
        const int bw = (row.getWidth() - 16) / 3;
        rerollBtn.setBounds (row.removeFromLeft (bw)); row.removeFromLeft (8);
        previewBtn.setBounds (row.removeFromLeft (bw)); row.removeFromLeft (8);
        contextBtn.setBounds (row);
        right.removeFromTop (8);
        auto row2 = right.removeFromTop (34);
        folderBtn.setBounds (row2.removeFromLeft (bw));
        row2.removeFromLeft (8);
        allChip.setBounds (row2);
    }
}

//==============================================================================
void NeuroDropEditor::timerCallback()
{
    pulsePhase += 0.12f;
    const float pulse = 0.5f + 0.5f * std::sin (pulsePhase);
    const int slot = proc.getCaptureSlot();
    const auto live = slot >= 0 ? proc.getLiveSnapshot() : nd::AnalysisSnapshot();
    beforeCard.setState (slot == 0 ? live : proc.getSnapshot (0), slot == 0, pulse);
    afterCard.setState  (slot == 1 ? live : proc.getSnapshot (1), slot == 1, pulse);

    // Info-Zeile: Tempo-Plan + Original-Drop-Laenge
    auto plan = proc.previewTempoPlan();
    String info = plan.text;
    const auto b = proc.getSnapshot (0), a = proc.getSnapshot (1);
    if (b.valid && a.valid && b.ppqEnd >= 0 && a.ppqStart > b.ppqEnd)
        info << u8 ("   ·   Original-Drop: ") << String ((a.ppqStart - b.ppqEnd) / 4.0, 1) << " Host-Takte";
    infoLbl.setText (info, dontSendNotification);

    const bool busy = proc.isGenerating();
    generateBtn.setEnabled (! busy);
    rerollBtn.setEnabled (! busy);
    wave.setBusy (busy, proc.getProgress());

    auto v = proc.getView();
    if (v != shownView)
    {
        shownView = v;
        wave.setView (v);
        for (int i = 0; i < nd::NumStems; ++i)
            strips[i]->setPeaks (v ? &v->stemPeaks[(size_t) i] : nullptr, v && v->total > 0 ? (double) v->pre / v->total : 0.0);
        String s;
        if (v)
        {
            s = v->summary.replace ("\n", u8 ("   ·   "));
            s << u8 ("   ·   Seed ") << (int) proc.apvts.getRawParameterValue (ParamID::seed)->load();
            if (! v->files.ok()) s = u8 ("Export-Fehler: ") + v->files.error;
        }
        summaryLbl.setText (s, dontSendNotification);
        allChip.repaint();
        for (auto* st : strips) st->chip.repaint();
    }
    if (auto err = proc.getLastError(); err.isNotEmpty() && shownView == nullptr)
        summaryLbl.setText (err, dontSendNotification);

    const bool hasRender = v != nullptr;
    previewBtn.setEnabled (hasRender);
    previewBtn.setToggleState (proc.isPreviewing() && ! proc.isContextArmed(), dontSendNotification);
    previewBtn.setButtonText (proc.isPreviewing() && ! proc.isContextArmed() ? "STOP" : u8 ("VORHÖREN"));
    contextBtn.setEnabled (hasRender && (proc.canContextPreview() || proc.isContextArmed()));
    contextBtn.setToggleState (proc.isContextArmed(), dontSendNotification);
    folderBtn.setEnabled (true);
    wave.setPlayhead (proc.isPreviewing() ? proc.getPreviewPosition() : 0.0, proc.isPreviewing());

    repaint (headerArea);
}
