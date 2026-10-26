// NeuroDropTest – prueft Analyse (Tonart/Tuning/Kick) und rendert Beispiel-Drops.
// Aufruf: NeuroDropTest [Ausgabeordner]
#include <juce_core/juce_core.h>
#include "Engine/Analysis.h"
#include "Engine/DropGenerator.h"
#include "Engine/StemExporter.h"

using namespace nd;

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [OK]   " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

// Synthetischer Psy-Loop: Kick + Rolling-Bass (16tel-Offbeats) + Lead-Arpeggio + Hats
std::vector<float> makePsyLoop (double sr, int tonicPc, double a4, double bpm, double seconds)
{
    const int n = (int) (sr * seconds);
    std::vector<float> out ((size_t) n, 0.0f);
    const double beat = 60.0 / bpm, six = beat / 4.0;
    const int rootMidi = 24 + tonicPc + (tonicPc < 4 ? 12 : 0);       // ~ E1..D#2
    const double kickHz = midiToHz (rootMidi, a4);
    const double bassHz = midiToHz (rootMidi + 12, a4);
    const int arp[] = { 0, 3, 7, 10, 12, 7, 3, 5, 0, 3, 7, 8, 12, 10, 7, 2 };
    Rng rng (5);
    Biquad hp; hp.set (Biquad::Type::HighPass, sr, 8000.0, 0.7);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double tb = std::fmod (t, beat);
        const int sixIdx = (int) (t / six);
        const double ts = std::fmod (t, six);
        // Kick
        const double kf = kickHz + kickHz * 4.0 * std::exp (-tb / 0.02);
        const double kph = kTwoPi * (kickHz * tb + kickHz * 4.0 * 0.02 * (1.0 - std::exp (-tb / 0.02)));
        (void) kf;
        float x = (float) (std::sin (kph) * std::exp (-tb / 0.15)) * 0.8f;
        // Bass auf 16tel 2,3,4
        if (sixIdx % 4 != 0)
        {
            const double ph = std::fmod (bassHz * t, 1.0);
            x += (float) ((2.0 * ph - 1.0) * std::exp (-ts / 0.05)) * 0.35f;
        }
        // Lead-Arpeggio (2 Oktaven hoeher)
        const int note = rootMidi + 36 + arp[sixIdx % 16];
        const double lf = midiToHz (note, a4);
        x += (float) (std::sin (kTwoPi * lf * t) + 0.4 * std::sin (kTwoPi * 2 * lf * t)) * (float) std::exp (-ts / 0.08) * 0.18f;
        // Hats
        if (sixIdx % 2 == 1) x += hp.process (rng.bip()) * (float) std::exp (-ts / 0.02) * 0.15f;
        out[(size_t) i] = x;
    }
    return out;
}

bool bufferFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i))) return false;
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory().getChildFile ("test-renders");
    outDir.createDirectory();
    const double sr = 48000.0;

    std::cout << "== Analyse-Tests ==" << std::endl;
    struct Case { int pc; double a4; };
    AnalysisSnapshot psyFs;
    for (auto c : { Case { 6, 432.0 }, Case { 9, 440.0 }, Case { 2, 432.0 } })
    {
        auto loop = makePsyLoop (sr, c.pc, c.a4, 145.0, 20.0);
        auto a = AnalysisAccumulator::analyse (loop.data(), (int) loop.size(), sr);
        std::cout << "  Erwartet " << noteName (c.pc) << " Moll @ " << c.a4 << " Hz  ->  " << a.describe()
                  << " | Konfidenz " << a.keyConfidence << " | Bass-PC " << a.bassPc << std::endl;
        check (a.valid, "Analyse gueltig");
        check (a.keyTonic == c.pc && a.keyMinor, "Tonart erkannt");
        check (std::abs (a.tuningA4 - c.a4) < 1.0, "Tuning erkannt");
        const double kExp = midiToHz (24 + c.pc + (c.pc < 4 ? 12 : 0), c.a4);
        check (a.kickHz > 0 && std::abs (std::log2 (a.kickHz / kExp)) * 12.0 < 1.25, "Kick-Grundton (+/-1.25 HT), erwartet " + juce::String (kExp, 1) + " Hz");
        if (c.pc == 6) psyFs = a;
    }

    std::cout << "\n== Tempo-Plan ==" << std::endl;
    for (int bars : { 1, 2, 4, 6, 8, 16, 32 })
    {
        DropSettings s; s.hostBpm = 145.0; s.bars = bars;
        auto p = planTempo (s);
        std::cout << "  " << p.text << std::endl;
        check (p.bpm >= 150.0 && p.bpm <= 190.0, "BPM im DnB-Bereich");
    }

    std::cout << "\n== Render-Tests ==" << std::endl;
    struct R { Style st; Groove gr; TempoMode tm; int bars; uint64_t seed; };
    const R renders[] = {
        { Style::Neurofunk, Groove::TwoStep,  TempoMode::DnbTempo,  8, 1001 },
        { Style::Techstep,  Groove::Rolling,  TempoMode::DnbTempo,  4, 2002 },
        { Style::JumpUp,    Groove::TwoStep,  TempoMode::DnbTempo,  6, 3003 },
        { Style::Roller,    Groove::Broken,   TempoMode::DnbTempo, 16, 4004 },
        { Style::Neurofunk, Groove::Halftime, TempoMode::HostTempo, 2, 5005 },
        { Style::Neurofunk, Groove::TwoStep,  TempoMode::DnbTempo,  1, 6006 },
    };
    for (const auto& rc : renders)
    {
        DropSettings s;
        s.style = rc.st; s.groove = rc.gr; s.tempoMode = rc.tm; s.bars = rc.bars; s.seed = rc.seed;
        s.hostBpm = 145.0; s.sampleRate = sr; s.before = psyFs; s.after = psyFs;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto r = renderDrop (s);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        check (r != nullptr, "Render ok");
        if (! r) continue;
        std::cout << "  " << r->summary.replace ("\n", " / ") << std::endl;
        std::cout << "    Dauer " << juce::String (r->totalSamples / sr, 2) << " s, gerendert in " << juce::String (ms, 0)
                  << " ms, Mix RMS " << juce::String (r->mixRmsDb, 1) << " dB, Peak " << juce::String (r->mixPeakDb, 2) << " dBFS" << std::endl;
        bool finite = bufferFinite (r->mix);
        for (auto& b : r->stems) finite = finite && bufferFinite (b);
        check (finite, "keine NaN/Inf");
        check (r->mixPeakDb <= -0.9f, "Summe unter -1 dBFS (Limiter)");
        const double expectDrop = r->bars * 4.0 * 60.0 / r->bpm * sr;
        check (std::abs (r->dropSamples - expectDrop) < 2.0, "Drop-Laenge exakt");
        check (r->key.tonic == 6, "Tonart aus Analyse uebernommen (F#)");
        for (int i = 0; i < NumStems; ++i)
            check (r->stems[(size_t) i].getMagnitude (0, r->totalSamples) > 0.001f, juce::String ("Stem hat Signal: ") + stemName (i));
        auto ex = exportStems (*r, outDir);
        check (ex.ok(), "Export " + ex.folder.getFileName() + (ex.ok() ? juce::String() : " -> " + ex.error));
    }

    std::cout << "\n" << (failures == 0 ? "ALLE TESTS OK" : juce::String (failures) + " FEHLER") << std::endl;
    return failures == 0 ? 0 : 1;
}
