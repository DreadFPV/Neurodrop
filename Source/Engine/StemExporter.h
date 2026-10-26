#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "DropGenerator.h"

namespace nd
{
struct ExportResult
{
    juce::File folder;
    std::array<juce::File, NumStems> stems;   // leer = Stem deaktiviert
    juce::File mix;
    juce::String error;
    bool ok() const { return error.isEmpty(); }
};

// Standard-Ordner: Dokumente/NeuroDrop/Renders
juce::File defaultRenderRoot();

// Schreibt alle aktiven Stems (24 Bit WAV) + Summe in einen neuen Unterordner.
// Keine Tempo-Metadaten -> Cubase importiert die Dateien ohne Musical-Mode/Stretching.
ExportResult exportStems (const RenderResult& r, const juce::File& root, bool writeMix = true);
} // namespace nd
