#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <mutex>
#include "Engine/Analysis.h"
#include "Engine/DropGenerator.h"
#include "Engine/StemExporter.h"

namespace ParamID
{
    inline constexpr const char* style      = "style";
    inline constexpr const char* groove     = "groove";
    inline constexpr const char* tempoMode  = "tempoMode";
    inline constexpr const char* targetBpm  = "targetBpm";
    inline constexpr const char* gridLock   = "gridLock";
    inline constexpr const char* bars       = "bars";
    inline constexpr const char* preRoll    = "preRoll";
    inline constexpr const char* transOut   = "transOut";
    inline constexpr const char* key        = "key";
    inline constexpr const char* scale      = "scale";
    inline constexpr const char* tuning     = "tuning";
    inline constexpr const char* intensity  = "intensity";
    inline constexpr const char* aggression = "aggression";
    inline constexpr const char* match      = "match";
    inline constexpr const char* loudMatch  = "loudMatch";
    inline constexpr const char* muteInput  = "muteInput";
    inline constexpr const char* seed       = "seed";
    inline juce::String gain (int i) { return "gain" + juce::String (i); }
    inline juce::String on (int i)   { return "on" + juce::String (i); }
}

// Werte der Auswahl-Parameter
inline const juce::StringArray& barChoices()    { static juce::StringArray a { "1", "2", "4", "6", "8", "16", "32" }; return a; }
inline const juce::StringArray& preRollChoices(){ static juce::StringArray a { "0", "1", "2", "4" }; return a; }

// Kompakte Ansicht des letzten Renders fuer die UI
struct RenderView
{
    std::vector<float> mixPeaks;                               // Huellkurve 0..1
    std::array<std::vector<float>, nd::NumStems> stemPeaks;
    std::array<bool, nd::NumStems> active {};
    int pre = 0, total = 0, bars = 0;
    double bpm = 0, hostBpm = 0, sampleRate = 48000, hostBars = 0;
    juce::String summary;
    nd::ExportResult files;
};

class NeuroDropProcessor : public juce::AudioProcessor
{
public:
    NeuroDropProcessor();
    ~NeuroDropProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NeuroDrop"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    //------------------------------------------------------------------ Analyse
    void startCapture (int slot);              // 0 = Vorher, 1 = Nachher
    void stopCapture();
    int  getCaptureSlot() const { return captureSlot.load(); }
    nd::AnalysisSnapshot getSnapshot (int slot) const;
    nd::AnalysisSnapshot getLiveSnapshot();
    void clearSnapshot (int slot);

    //------------------------------------------------------------------ Generator
    void generate (bool newSeed);
    bool  isGenerating() const { return generating.load(); }
    float getProgress() const  { return progress.load(); }
    std::shared_ptr<const RenderView> getView() const;
    juce::String getLastError() const;
    nd::TempoPlan previewTempoPlan() const;     // fuer die Info-Zeile
    double effectiveHostBpm() const;

    //------------------------------------------------------------------ Vorhoeren
    void setPreview (bool on);
    bool isPreviewing() const { return previewing.load(); }
    void setContextPreview (bool armed);
    bool isContextArmed() const { return contextArmed.load(); }
    bool canContextPreview() const;
    double getContextDropPpq() const;
    double getPreviewPosition() const;          // 0..1

    //------------------------------------------------------------------ Status
    double getHostBpm() const   { return hostBpm.load(); }
    bool   isHostPlaying() const { return hostPlaying.load(); }
    double getHostPpq() const   { return hostPpq.load(); }
    float  getInputLevel() const { return inputLevel.load(); }
    bool   isSidechainActive() const { return sidechainActive.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    nd::DropSettings buildSettings() const;
    void renderJob (nd::DropSettings s);
    void updateDropPpq();
    std::atomic<bool> shuttingDown { false };
    std::atomic<double> dropPpqCache { -1.0 };

    // Analyse
    nd::LiveAnalyzer live;
    std::atomic<int> captureSlot { -1 };
    std::atomic<double> capPpqStart { -1.0 }, capPpqEnd { -1.0 }, capBpm { 0.0 };
    mutable std::mutex snapLock;
    std::array<nd::AnalysisSnapshot, 2> snapshots;
    std::vector<float> monoScratch;

    // Render
    juce::ThreadPool pool { 1 };
    std::atomic<bool> generating { false };
    std::atomic<float> progress { 0.0f };
    mutable std::mutex viewLock;
    std::shared_ptr<const RenderView> view;
    juce::String lastError;

    juce::SpinLock previewLock;
    std::unique_ptr<nd::RenderResult> current;   // nur unter previewLock tauschen
    std::atomic<bool> previewing { false }, contextArmed { false }, contextRunning { false };
    std::atomic<int64_t> previewPos { 0 };
    std::atomic<int> previewTotal { 0 };

    // Host
    std::atomic<double> hostBpm { 0.0 }, hostPpq { 0.0 };
    std::atomic<bool> hostPlaying { false }, sidechainActive { false };
    std::atomic<float> inputLevel { 0.0f };
    double currentSr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuroDropProcessor)
};
