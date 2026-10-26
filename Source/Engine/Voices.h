#pragma once
// Klangerzeugung: Drums, Neuro-Bass (Reese/Growl/Stab/Dive/Wobble), Sub, FX.
#include <vector>
#include "DSP.h"
#include "Music.h"
#include "Patterns.h"

namespace nd
{
struct VoiceContext
{
    double sr = 48000.0;
    double bpm = 174.0;
    double a4 = 440.0;
    Key    key;
    float  aggression = 0.7f;
    float  intensity  = 0.7f;
    float  brightness = 0.5f;   // aus Analyse (0 dunkel .. 1 hell)
    uint64_t seed = 1;
};

struct Stereo { std::vector<float> l, r; void resize (size_t n) { l.assign (n, 0.0f); r.assign (n, 0.0f); } size_t size() const { return l.size(); } };

// One-Shots
std::vector<float> makeKick  (const VoiceContext& c, double baseHz, float punch);
std::vector<float> makeSnare (const VoiceContext& c, double bodyHz, bool ghost, uint64_t seed);
Stereo             makeHat   (const VoiceContext& c, int variant, uint64_t seed);

// Neuro-Bass: schreibt in 2x-oversampled Stereo-Puffer (Laenge = 2 * Ziel-Samples)
void renderBassNote (const VoiceContext& c, const BassNote& n, double startSample, double lenSamples, Stereo& out2x);

// Sub-Linie (mono)
void renderSub (const VoiceContext& c, const std::vector<SubNote>& notes, double stepSamples, double offsetSamples, std::vector<float>& out);

// FX
void addRiser       (const VoiceContext& c, Stereo& out, double start, double len, double rootHz, uint64_t seed, float level);
void addImpact      (const VoiceContext& c, Stereo& out, double start, double rootHz, uint64_t seed, float level);
void addZap         (const VoiceContext& c, Stereo& out, double start, double len, uint64_t seed, float level);
void addReverseSwell(const VoiceContext& c, Stereo& out, double endSample, double rootHz, uint64_t seed, float level);
void addDownlifter  (const VoiceContext& c, Stereo& out, double start, double len, uint64_t seed, float level);
} // namespace nd
