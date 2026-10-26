#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/NeuroLookAndFeel.h"
#include "UI/Panels.h"

class NeuroDropEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NeuroDropEditor (NeuroDropProcessor&);
    ~NeuroDropEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addCombo (juce::ComboBox& c, juce::Label& l, const juce::String& title, const char* paramId);
    void addKnob (juce::Slider& s, juce::Label& l, const juce::String& title, const char* paramId, juce::Colour accent);
    void addToggle (juce::ToggleButton& t, const juce::String& text, const char* paramId);
    juce::StringArray filesFor (int stem) const;
    juce::StringArray allFiles() const;

    NeuroDropProcessor& proc;
    ui::NeuroLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ui::AnalysisCard beforeCard, afterCard;

    juce::ComboBox styleBox, grooveBox, tempoBox, barsBox, preRollBox, keyBox, scaleBox, tuningBox;
    juce::Label styleLbl, grooveLbl, tempoLbl, barsLbl, preRollLbl, keyLbl, scaleLbl, tuningLbl;
    juce::Slider bpmSlider, intensityKnob, aggressionKnob, matchKnob;
    juce::Label bpmLbl, intensityLbl, aggressionLbl, matchLbl;
    juce::ToggleButton gridLockBtn, transOutBtn, loudMatchBtn, muteInputBtn;

    juce::OwnedArray<ui::StemStrip> strips;

    ui::WaveformView wave;
    juce::TextButton generateBtn, rerollBtn, previewBtn, contextBtn, folderBtn;
    ui::DragChip allChip { "ALLE STEMS IN CUBASE ZIEHEN", ui::col::accent };
    juce::Label infoLbl, summaryLbl;

    std::vector<std::unique_ptr<SA>> sAtt;
    std::vector<std::unique_ptr<CA>> cAtt;
    std::vector<std::unique_ptr<BA>> bAtt;

    std::shared_ptr<const RenderView> shownView;
    float pulsePhase = 0.0f;
    juce::Rectangle<int> dropPanel, stemPanel, bottomPanel, headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuroDropEditor)
};
