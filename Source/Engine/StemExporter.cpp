#include "StemExporter.h"

namespace nd
{
juce::File defaultRenderRoot()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("NeuroDrop").getChildFile ("Renders");
}

namespace
{
juce::String writeWav (const juce::AudioBuffer<float>& b, double sr, const juce::File& f)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    if (static_cast<juce::FileOutputStream*> (os.get())->failedToOpen()) return "Datei nicht beschreibbar: " + f.getFullPathName();
    juce::WavAudioFormat wav;
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (24);
    auto writer = wav.createWriterFor (os, opts);
    if (writer == nullptr) return "WAV-Writer konnte nicht erstellt werden";
    if (! writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples())) return "Schreibfehler: " + f.getFileName();
    return {};
}
} // namespace

ExportResult exportStems (const RenderResult& r, const juce::File& root, bool writeMix)
{
    ExportResult res;
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S");
    res.folder = root.getChildFile (stamp + "_" + r.baseName);
    if (! res.folder.createDirectory()) { res.error = "Ordner konnte nicht erstellt werden: " + res.folder.getFullPathName(); return res; }

    for (int i = 0; i < NumStems; ++i)
    {
        if (! r.active[(size_t) i]) continue;
        const auto f = res.folder.getChildFile (r.baseName + "_" + juce::String (i + 1).paddedLeft ('0', 2) + "_" + stemName (i) + ".wav");
        const auto err = writeWav (r.stems[(size_t) i], r.sampleRate, f);
        if (err.isNotEmpty()) { res.error = err; return res; }
        res.stems[(size_t) i] = f;
    }
    if (writeMix)
    {
        res.mix = res.folder.getChildFile (r.baseName + "_00_Mix.wav");
        const auto err = writeWav (r.mix, r.sampleRate, res.mix);
        if (err.isNotEmpty()) { res.error = err; return res; }
    }
    // Info-Datei mit allen Eckdaten
    juce::String info;
    info << "NeuroDrop Render\n" << r.summary << "\n\n"
         << "Samplerate: " << r.sampleRate << " Hz\n"
         << "Vorlauf (Riser): " << r.preRollSamples << " Samples\n"
         << "Drop: " << r.dropSamples << " Samples (" << juce::String (r.dropSamples / r.sampleRate, 3) << " s)\n"
         << "Gesamt: " << r.totalSamples << " Samples\n"
         << "Mix RMS: " << juce::String (r.mixRmsDb, 1) << " dB | Peak: " << juce::String (r.mixPeakDb, 1) << " dBFS\n\n"
         << "Platzierung in Cubase: Dateistart = Drop-Start minus Vorlauf.\n"
         << "Dateien enthalten keine Tempo-Infos -> Musical Mode bleibt aus, DnB-Tempo bleibt erhalten.\n";
    res.folder.getChildFile ("info.txt").replaceWithText (info);
    return res;
}
} // namespace nd
