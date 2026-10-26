#pragma once
#include <cmath>
#include <string>
#include <array>

namespace nd
{
inline const char* noteName (int pc)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

enum class ScaleType { Minor = 0, Phrygian, Major };

struct Key
{
    int tonic = 5;                    // 0 = C ... 11 = B   (Default F-Moll – klassisch fuer DnB)
    ScaleType scale = ScaleType::Minor;

    std::string name() const
    {
        std::string s = noteName (tonic);
        switch (scale)
        {
            case ScaleType::Minor:    s += " Moll"; break;
            case ScaleType::Phrygian: s += " Phrygisch"; break;
            case ScaleType::Major:    s += " Dur"; break;
        }
        return s;
    }

    // Halbton-Intervalle der Skala
    const std::array<int, 7>& intervals() const
    {
        static const std::array<int, 7> minor    { 0, 2, 3, 5, 7, 8, 10 };
        static const std::array<int, 7> phrygian { 0, 1, 3, 5, 7, 8, 10 };
        static const std::array<int, 7> major    { 0, 2, 4, 5, 7, 9, 11 };
        return scale == ScaleType::Major ? major : (scale == ScaleType::Phrygian ? phrygian : minor);
    }
    bool contains (int semitone) const
    {
        const int s = ((semitone % 12) + 12) % 12;
        for (auto i : intervals()) if (i == s) return true;
        return false;
    }
};

inline double midiToHz (double midi, double a4 = 440.0) { return a4 * std::pow (2.0, (midi - 69.0) / 12.0); }
inline double hzToMidi (double hz, double a4 = 440.0)   { return 69.0 + 12.0 * std::log2 (hz / a4); }

// Grundton-MIDI-Note im Bassregister (E1..D#2), damit Sub & Bass immer im Sweet-Spot liegen
inline int bassRootMidi (int tonicPc)
{
    int m = 24 + ((tonicPc % 12) + 12) % 12;   // C1 = 24
    if (m < 28) m += 12;                       // unter E1 -> eine Oktave hoch
    return m;
}
} // namespace nd
