#pragma once
// Arrangement-Logik: Grooves, Fills, Neuro-Motive (Call & Response), Sub-Linie.
#include <vector>
#include <cstdint>
#include "Music.h"

namespace nd
{
enum class Style  { Neurofunk = 0, Techstep, JumpUp, Roller };
enum class Groove { TwoStep = 0, Halftime, Rolling, Broken };
enum class Art    { Reese = 0, Growl, Stab, Dive, Wobble };

struct DrumHit  { double step; float vel; int variant; };   // step = 16tel ab Drop-Start
enum HatVariant { HatClosed = 0, HatOpen = 1, HatCrash = 2, HatShaker = 3 };

struct BassNote
{
    double step = 0, len = 1;  // in 16teln
    int    midi = 29;
    Art    art = Art::Growl;
    float  rate = 2.0f;        // LFO-Zyklen pro Beat
    int    shape = 0;          // 0 weich, 1 Saw runter, 2 Saw hoch, 3 Square
    float  var = 0.5f;         // Klangvariante
};

struct SubNote { double step, len; int midi; };

struct PatternParams
{
    Style  style = Style::Neurofunk;
    Groove groove = Groove::TwoStep;
    int    bars = 8;
    bool   transitionOut = true;
    float  intensity = 0.7f, aggression = 0.7f;
    Key    key;
    uint64_t seed = 1;
};

struct Arrangement
{
    int bars = 8;
    std::vector<DrumHit> kick, snare, hats;
    std::vector<BassNote> bass;
    std::vector<SubNote> sub;
    std::vector<double> zaps;            // FX-Akzente (Step-Positionen)
    double transitionStep = -1.0;        // Beginn Uebergang raus (-1 = keiner)
    int rootMidi = 29;
};

Arrangement buildArrangement (const PatternParams& p);
} // namespace nd
