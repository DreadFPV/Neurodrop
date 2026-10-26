// NeuroDropHostCheck – Integrationstest wie in einer DAW:
//  1) Prozessor direkt: Capture mit simuliertem Transport -> Generieren -> Kontext-Vorhoeren -> State
//  2) Editor-Screenshot (PNG)
//  3) Optional: gebautes VST3 laden und Audio durchschicken (Pfad als Argument)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
int failures = 0;
void check (bool ok, const String& what)
{
    std::cout << (ok ? "  [OK]   " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

struct FakePlayHead : AudioPlayHead
{
    double bpm = 145.0, ppq = 0.0; bool playing = true;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

// einfacher Psy-Loop (F# Moll, 432 Hz)
float psySample (double t)
{
    const double bpm = 145.0, beat = 60.0 / bpm, six = beat / 4.0, a4 = 432.0;
    const double tb = std::fmod (t, beat), ts = std::fmod (t, six);
    const int si = (int) (t / six);
    const double k = a4 * std::pow (2.0, (30 - 69) / 12.0);
    float x = (float) (std::sin (2 * MathConstants<double>::pi * (k * tb + k * 4 * 0.02 * (1 - std::exp (-tb / 0.02)))) * std::exp (-tb / 0.15)) * 0.7f;
    if (si % 4) x += (float) ((2.0 * std::fmod (k * 2 * t, 1.0) - 1.0) * std::exp (-ts / 0.05)) * 0.3f;
    static const int arp[] = { 0, 3, 7, 10, 12, 7, 3, 5 };
    const double lf = a4 * std::pow (2.0, (30 + 36 + arp[si % 8] - 69) / 12.0);
    x += (float) std::sin (2 * MathConstants<double>::pi * lf * t) * (float) std::exp (-ts / 0.08) * 0.2f;
    return x;
}

void runBlocks (AudioProcessor& p, FakePlayHead& ph, double sr, int block, double seconds, double& t, bool feedPsy, double* outEnergy = nullptr)
{
    AudioBuffer<float> buf (p.getTotalNumInputChannels() > 2 ? p.getTotalNumInputChannels() : 2, block);
    MidiBuffer midi;
    const int blocks = (int) (seconds * sr / block);
    double e = 0;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        if (feedPsy)
            for (int i = 0; i < block; ++i)
            {
                const float v = psySample (t + i / sr);
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
        p.processBlock (buf, midi);
        for (int i = 0; i < block; ++i) e += (double) buf.getSample (0, i) * buf.getSample (0, i);
        t += block / sr;
        ph.ppq += block / sr * ph.bpm / 60.0;
    }
    if (outEnergy) *outEnergy = e / std::max (1, blocks * block);
}
} // namespace

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI gui;
    const double sr = 48000.0; const int block = 512;
    const File outDir = File::getCurrentWorkingDirectory().getChildFile ("hostcheck-out");
    outDir.createDirectory();

    std::cout << "== Prozessor direkt ==" << std::endl;
    {
        NeuroDropProcessor proc;
        FakePlayHead ph; ph.ppq = 64.0; // Takt 17
        proc.setPlayHead (&ph);
        proc.setPlayConfigDetails (2, 2, sr, block);
        proc.prepareToPlay (sr, block);
        double t = 0;

        proc.startCapture (0);
        runBlocks (proc, ph, sr, block, 13.24, t, true);   // 8 Host-Takte
        proc.stopCapture();
        const auto b = proc.getSnapshot (0);
        std::cout << "  Vorher: " << b.describe() << " | Takt " << b.ppqStart / 4 + 1 << "-" << b.ppqEnd / 4 + 1 << std::endl;
        check (b.valid && b.keyTonic == 6 && b.keyMinor, "Capture erkennt F# Moll");
        check (std::abs (b.tuningA4 - 432.0f) < 1.0f, "Capture erkennt 432 Hz");
        check (std::abs (proc.getContextDropPpq() - 96.0) < 0.01, "Drop-Position = Takt 25 (ppq 96), ist " + String (proc.getContextDropPpq()));

        proc.generate (false);
        const auto t0 = Time::getMillisecondCounter();
        while (proc.isGenerating() && Time::getMillisecondCounter() - t0 < 60000) Thread::sleep (20);
        auto v = proc.getView();
        check (v != nullptr && v->files.ok(), "Render + Export ok");
        if (v)
        {
            std::cout << "  " << v->summary.replace ("\n", " / ") << std::endl;
            std::cout << "  Ordner: " << v->files.folder.getFullPathName() << std::endl;
            int n = 0; for (auto& f : v->files.stems) n += f.existsAsFile() ? 1 : 0;
            check (n == nd::NumStems, "6 Stem-Dateien geschrieben");
        }

        // Kontext-Vorhoeren: Transport zurueck auf Takt 21, Drop bei Takt 25 (ppq 96), Vorlauf 1 Takt
        proc.setContextPreview (true);
        check (proc.isContextArmed(), "Kontext-Vorhoeren scharf");
        ph.ppq = 80.0; t = 0;
        double eBefore = 0, eRiser = 0, eDrop = 0;
        runBlocks (proc, ph, sr, block, 60.0 / 145.0 * 4 * 2.9, t, true, &eBefore);   // bis kurz vor Vorlauf (Takt 23.9)
        check (! proc.isPreviewing(), "vor dem Vorlauf noch still");
        runBlocks (proc, ph, sr, block, 60.0 / 145.0 * 4 * 1.0, t, true, &eRiser);    // Vorlauf-Takt
        check (proc.isPreviewing(), "Vorschau startet im Vorlauf (Takt 24)");
        runBlocks (proc, ph, sr, block, 2.0, t, true, &eDrop);
        check (proc.isPreviewing() && eDrop > 1.0e-4, "Drop laeuft im Kontext");
        // Psy stumm? Ausgang darf kein reines Psy mehr sein -> Vergleich mit Psy-Energie
        std::cout << "  Energie: vor " << eBefore << " | Vorlauf " << eRiser << " | Drop " << eDrop << std::endl;
        ph.ppq = 10.0; runBlocks (proc, ph, sr, block, 0.1, t, false);
        check (! proc.isPreviewing(), "Ruecksprung stoppt Vorschau");
        proc.setContextPreview (false);

        // State
        MemoryBlock mb; proc.getStateInformation (mb);
        NeuroDropProcessor p2; p2.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto b2 = p2.getSnapshot (0);
        check (b2.valid && b2.keyTonic == b.keyTonic && std::abs (b2.ppqEnd - b.ppqEnd) < 1e-6, "Analyse im Projekt-State gespeichert");

        // Editor-Screenshot
        {
            std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
            ed->setVisible (true);
            for (int i = 0; i < 5; ++i) { MessageManager::getInstance()->runDispatchLoopUntil (60); }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            const File png = outDir.getChildFile ("neurodrop_ui.png");
            png.deleteFile();
            FileOutputStream os (png);
            PNGImageFormat().writeImageToStream (img, os);
            check (png.getSize() > 1000, "UI-Screenshot " + png.getFullPathName());
        }
        proc.setPlayHead (nullptr);
    }

    if (argc > 1)
    {
        std::cout << "\n== VST3 laden ==" << std::endl;
        VST3PluginFormat fmt;
        OwnedArray<PluginDescription> types;
        fmt.findAllTypesForFile (types, argv[1]);
        check (types.size() == 1, "VST3 gefunden: " + String (types.size()));
        if (! types.isEmpty())
        {
            std::cout << "  " << types[0]->name << " | " << types[0]->manufacturerName << " | " << types[0]->category << std::endl;
            String err;
            auto inst = fmt.createInstanceFromDescription (*types[0], sr, block, err);
            check (inst != nullptr, "Instanz erstellt " + err);
            if (inst)
            {
                check (inst->getBusCount (true) == 2, "2 Eingangsbusse (Main + Sidechain)");
                auto layout = inst->getBusesLayout();
                if (layout.inputBuses.size() > 1) layout.inputBuses.getReference (1) = AudioChannelSet::stereo();
                check (inst->setBusesLayout (layout), "Sidechain stereo aktivierbar");
                FakePlayHead ph; inst->setPlayHead (&ph);
                inst->prepareToPlay (sr, block);
                AudioBuffer<float> buf (4, block); MidiBuffer midi;
                bool finite = true;
                double t = 0;
                for (int b = 0; b < 200; ++b)
                {
                    for (int i = 0; i < block; ++i) { const float v = psySample (t + i / sr); for (int ch = 0; ch < 4; ++ch) buf.setSample (ch, i, v); }
                    inst->processBlock (buf, midi);
                    for (int i = 0; i < block; ++i) finite = finite && std::isfinite (buf.getSample (0, i));
                    t += block / sr; ph.ppq += block / sr * 145.0 / 60.0;
                }
                check (finite, "Audio laeuft durch (200 Bloecke)");
                MemoryBlock mb; inst->getStateInformation (mb);
                check (mb.getSize() > 100, "State " + String ((int) mb.getSize()) + " Bytes");
                if (auto* ed = inst->createEditorIfNeeded())
                {
                    check (ed->getWidth() > 500, "Editor oeffnet (" + String (ed->getWidth()) + "x" + String (ed->getHeight()) + ")");
                    delete ed;
                }
                inst->releaseResources();
            }
        }
    }

    std::cout << "\n" << (failures == 0 ? "HOSTCHECK OK" : String (failures) + " FEHLER") << std::endl;
    return failures == 0 ? 0 : 1;
}
