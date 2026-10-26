#pragma once
// Analyse des laufenden Psy-Tracks (Sidechain oder Main-Input):
// Tonart, Tuning (432/440), Spektrum, Lautheit, Kick-/Bass-Grundton, Energie.
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <mutex>
#include "DSP.h"
#include "Music.h"

namespace nd
{
constexpr int kNumBands = 9;
// Bandgrenzen in Hz: [i] .. [i+1]
inline const std::array<float, kNumBands + 1>& bandEdges()
{
    static const std::array<float, kNumBands + 1> e { 20.f, 60.f, 120.f, 250.f, 500.f, 1000.f, 2500.f, 6000.f, 12000.f, 20000.f };
    return e;
}
inline const char* bandLabel (int i)
{
    static const char* l[kNumBands] = { "Sub", "Bass", "LoMid", "Mid", "Mid+", "HiMid", "Pres", "Hi", "Air" };
    return l[i];
}

struct AnalysisSnapshot
{
    bool   valid = false;
    double seconds = 0.0;             // analysierte (nicht stille) Dauer

    int    keyTonic = 5;
    bool   keyMinor = true;
    float  keyConfidence = 0.0f;      // 0..1
    float  tuningA4 = 440.0f;         // erkannte Referenz
    float  tuningCents = 0.0f;        // Abweichung zu 440 Hz

    float  rmsDb  = -60.0f;
    float  peakDb = -60.0f;
    float  centroidHz = 0.0f;         // Helligkeit
    float  kickHz = 0.0f;             // Grundton der Kick (0 = unbekannt)
    int    bassPc = -1;               // Pitch-Class des Basses (-1 = unbekannt)
    float  onsetRate = 0.0f;          // Kicks/Transienten pro Sekunde
    float  energy = 0.0f;             // 0..1
    std::array<float, kNumBands> bandsDb {};

    double hostBpm = 0.0;             // beim Aufnehmen
    double ppqStart = -1.0, ppqEnd = -1.0;

    Key    key() const { Key k; k.tonic = keyTonic; k.scale = keyMinor ? ScaleType::Minor : ScaleType::Major; return k; }
    float  brightness() const { return clampf ((centroidHz - 1200.0f) / 3500.0f, 0.0f, 1.0f); }

    juce::String describe() const;
    std::unique_ptr<juce::XmlElement> toXml (const juce::String& tag) const;
    static AnalysisSnapshot fromXml (const juce::XmlElement* xml);
};

//==============================================================================
// Sammelt Statistik ueber beliebig lange Audio-Strecken (mono).
class AnalysisAccumulator
{
public:
    AnalysisAccumulator();
    void prepare (double sampleRate);
    void reset();
    void process (const float* mono, int numSamples);
    AnalysisSnapshot result() const;
    bool hasData() const { return frames > 2; }

    // Komplett-Analyse eines Buffers (Tests / Spektrum-Abgleich)
    static AnalysisSnapshot analyse (const float* mono, int numSamples, double sampleRate);

private:
    static constexpr int fftOrder = 14;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = 4096;

    void analyseFrame();
    void timeDomain (float x);
    void measureKick (int64_t onsetSample);

    double sr = 48000.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, ring, fftData;
    int ringPos = 0, filled = 0, sinceHop = 0;

    // Spektrum
    std::array<double, kNumBands> bandPow {};
    int    frames = 0;
    double centroidSum = 0, centroidW = 0;
    std::array<double, 1200> pitchHist {};    // Cent innerhalb der Oktave (C = 0)
    double tuneX = 0, tuneY = 0;               // Kreismittel der Cent-Abweichung
    std::array<double, 12> bassPcHist {};

    // Zeitbereich
    Biquad lp1, lp2;
    double eShort = 0, eLong = 0;
    int64_t sampleIndex = 0, lastOnset = -1000000;
    std::vector<float> lowRing; int lowPos = 0;
    std::vector<int64_t> pendingOnsets;
    std::array<double, 200> kickHist {};       // 0.25-Halbton-Bins ab MIDI 20
    int onsets = 0;

    double sumSq = 0; int64_t loudSamples = 0; float peak = 0;
};

//==============================================================================
// Echtzeit-Variante: Audio-Thread schiebt Samples lock-free rein,
// ein Hintergrund-Thread rechnet.
class LiveAnalyzer : private juce::Thread
{
public:
    LiveAnalyzer();
    ~LiveAnalyzer() override;

    void begin (double sampleRate);
    AnalysisSnapshot end();                     // stoppt & liefert Endergebnis
    void push (const float* mono, int n);       // Audio-Thread
    AnalysisSnapshot snapshot();                // Zwischenstand (Message-Thread)
    bool running() const { return active.load(); }

private:
    void run() override;
    void drain();

    juce::AbstractFifo fifo { 1 << 19 };
    std::vector<float> fifoBuf;
    std::mutex lock;
    AnalysisAccumulator acc;
    std::atomic<bool> active { false };
};
} // namespace nd
