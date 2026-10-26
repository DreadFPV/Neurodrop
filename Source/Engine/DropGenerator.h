#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <memory>
#include "Analysis.h"
#include "Patterns.h"

namespace nd
{
enum StemId { StemKick = 0, StemSnare, StemHats, StemNeuro, StemSub, StemFx, NumStems };
inline const char* stemName (int i)
{
    static const char* n[NumStems] = { "Kick", "Snare", "Hats", "Neuro", "Sub", "FX" };
    return n[i];
}

enum class TempoMode { DnbTempo = 0, HostTempo };

struct DropSettings
{
    Style     style = Style::Neurofunk;
    Groove    groove = Groove::TwoStep;
    TempoMode tempoMode = TempoMode::DnbTempo;
    double    targetBpm = 174.0;
    bool      gridLock = true;
    double    hostBpm = 145.0;
    int       bars = 8;
    int       preRollBars = 1;          // Host-Takte Riser vor dem Drop
    bool      transitionOut = true;

    int       keyOverride = -1;         // -1 = Auto, sonst 0..11
    int       scaleMode = 0;            // 0 Auto, 1 Moll, 2 Phrygisch, 3 Dur
    int       tuningMode = 0;           // 0 Auto, 1 440, 2 432

    float     intensity = 0.7f, aggression = 0.7f, match = 0.6f;
    bool      loudnessMatch = true;
    uint64_t  seed = 1;
    std::array<float, NumStems> stemGainDb {};
    std::array<bool, NumStems>  stemOn { true, true, true, true, true, true };

    AnalysisSnapshot before, after;
    double sampleRate = 48000.0;
};

struct TempoPlan
{
    double bpm = 174.0;
    double hostBars = 0.0;       // Laenge des Drops in Host-Takten
    bool   locked = false;
    juce::String text;
};

struct RenderResult
{
    double sampleRate = 48000.0, bpm = 174.0, hostBpm = 145.0, a4 = 440.0, hostBars = 0.0;
    int    bars = 8, preRollSamples = 0, dropSamples = 0, totalSamples = 0;
    Key    key, outKey;
    std::array<juce::AudioBuffer<float>, NumStems> stems;
    std::array<bool, NumStems> active {};
    juce::AudioBuffer<float> mix;
    float  mixRmsDb = -60.0f, mixPeakDb = -60.0f;
    juce::String summary, baseName;
};

TempoPlan planTempo (const DropSettings& s);

// Rendert alle Stems offline. progress(0..1) -> false = abbrechen (liefert nullptr)
std::unique_ptr<RenderResult> renderDrop (const DropSettings& s, const std::function<bool (float)>& progress = {});
} // namespace nd
