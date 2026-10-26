#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
std::vector<float> makePeaks (const AudioBuffer<float>& b, int buckets)
{
    std::vector<float> p ((size_t) buckets, 0.0f);
    const int n = b.getNumSamples();
    if (n <= 0) return p;
    for (int k = 0; k < buckets; ++k)
    {
        const int a = (int) ((int64) k * n / buckets), e = std::max (a + 1, (int) ((int64) (k + 1) * n / buckets));
        float m = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* d = b.getReadPointer (ch);
            for (int i = a; i < e && i < n; ++i) m = std::max (m, std::abs (d[i]));
        }
        p[(size_t) k] = m;
    }
    return p;
}
} // namespace

//==============================================================================
NeuroDropProcessor::NeuroDropProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "NeuroDrop", createLayout())
{
}

NeuroDropProcessor::~NeuroDropProcessor()
{
    shuttingDown = true;
    pool.removeAllJobs (true, 15000);
    if (captureSlot.load() >= 0) live.end();
}

AudioProcessorValueTreeState::ParameterLayout NeuroDropProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    auto choice = [&] (const char* id, const char* name, StringArray items, int def)
    { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    auto flt = [&] (const String& id, const String& name, float lo, float hi, float step, float def)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, step), def)); };
    auto bln = [&] (const String& id, const String& name, bool def)
    { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    choice (ParamID::style,     "Stil",        { "Neurofunk", "Techstep", "Jump-Up", "Roller" }, 0);
    choice (ParamID::groove,    "Groove",      { "Two-Step", "Halftime", "Rolling", "Broken" }, 0);
    choice (ParamID::tempoMode, "Tempo-Modus", { "DnB-Tempo", "Host-Tempo (Halftime)" }, 0);
    flt    (ParamID::targetBpm, "Ziel-BPM", 160.0f, 180.0f, 0.5f, 174.0f);
    bln    (ParamID::gridLock,  "Grid-Lock", true);
    choice (ParamID::bars,      "Takte", barChoices(), 4);
    choice (ParamID::preRoll,   "Vorlauf (Host-Takte)", preRollChoices(), 1);
    bln    (ParamID::transOut,  "Uebergang raus", true);
    choice (ParamID::key,       "Tonart", { "Auto", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0);
    choice (ParamID::scale,     "Skala", { "Auto", "Moll", "Phrygisch", "Dur" }, 0);
    choice (ParamID::tuning,    "Tuning", { "Auto", "440 Hz", "432 Hz" }, 0);
    flt    (ParamID::intensity, "Intensitaet", 0.0f, 1.0f, 0.01f, 0.7f);
    flt    (ParamID::aggression,"Aggression",  0.0f, 1.0f, 0.01f, 0.7f);
    flt    (ParamID::match,     "Fit-in",      0.0f, 1.0f, 0.01f, 0.6f);
    bln    (ParamID::loudMatch, "Lautheit angleichen", true);
    bln    (ParamID::muteInput, "Eingang stumm beim Vorhoeren", true);
    l.add (std::make_unique<AudioParameterInt> (ParameterID { ParamID::seed, 1 }, "Seed", 1, 999999, 1234));
    for (int i = 0; i < nd::NumStems; ++i)
    {
        flt (ParamID::gain (i), String (nd::stemName (i)) + " Pegel", -24.0f, 6.0f, 0.1f, 0.0f);
        bln (ParamID::on (i),   String (nd::stemName (i)) + " an", true);
    }
    return l;
}

bool NeuroDropProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != out) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != AudioChannelSet::mono() && sc != AudioChannelSet::stereo()) return false;
    }
    return true;
}

void NeuroDropProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSr = sampleRate > 0 ? sampleRate : 48000.0;
    monoScratch.assign ((size_t) std::max (512, samplesPerBlock), 0.0f);
}

//==============================================================================
void NeuroDropProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);

    // Host-Infos
    double bpm = 0, ppq = 0; bool playing = false, ppqValid = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { ppq = *p; ppqValid = true; }
        }
    if (bpm > 0) hostBpm = bpm;
    hostPlaying = playing;
    if (ppqValid) hostPpq = ppq;
    const double sr = getSampleRate() > 0 ? getSampleRate() : currentSr;

    // Analyse-Quelle: Sidechain wenn verbunden & Signal, sonst Main-Input
    AudioBuffer<float> sc;
    bool scActive = false;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0)
        {
            sc = getBusBuffer (buffer, true, 1);
            scActive = sc.getNumChannels() > 0 && sc.getMagnitude (0, n) > 1.0e-5f;
        }
    sidechainActive = scActive;
    const AudioBuffer<float>& src = scActive ? sc : mainIn;
    const float lvl = src.getNumChannels() > 0 ? src.getMagnitude (0, n) : 0.0f;
    inputLevel = std::max (lvl, inputLevel.load() * 0.85f);

    if (captureSlot.load() >= 0 && src.getNumChannels() > 0)
    {
        const int chans = src.getNumChannels();
        const int chunk = (int) monoScratch.size();
        for (int off = 0; off < n; off += chunk)
        {
            const int m = std::min (chunk, n - off);
            for (int i = 0; i < m; ++i)
            {
                float v = 0;
                for (int ch = 0; ch < chans; ++ch) v += src.getSample (ch, off + i);
                monoScratch[(size_t) i] = v / (float) chans;
            }
            live.push (monoScratch.data(), m);
        }
        if (playing && ppqValid && bpm > 0)
        {
            if (capPpqStart.load() < 0) capPpqStart = ppq;
            capPpqEnd = ppq + n * bpm / (60.0 * sr);
            capBpm = bpm;
        }
    }

    // Vorhoeren
    const SpinLock::ScopedTryLockType lock (previewLock);
    if (! lock.isLocked() || current == nullptr) return;

    const int total = current->totalSamples, pre = current->preRollSamples;
    int startOffset = 0;
    bool contextMode = contextArmed.load();
    if (contextMode)
    {
        const double dropPpq = getContextDropPpq();
        if (playing && ppqValid && bpm > 0 && dropPpq >= 0)
        {
            const double ppqPerSample = bpm / (60.0 * sr);
            const double target = dropPpq - (pre / sr) * bpm / 60.0;
            const double ppqEnd = ppq + n * ppqPerSample;
            if (! contextRunning.load())
            {
                if (target >= ppq && target < ppqEnd)
                {
                    startOffset = (int) ((target - ppq) / ppqPerSample);
                    previewPos = 0; contextRunning = true; previewing = true;
                }
                else if (ppq > target && (ppq - target) / ppqPerSample < total)
                {
                    previewPos = (int64_t) ((ppq - target) / ppqPerSample); contextRunning = true; previewing = true;
                }
            }
            else if (ppq < target - 0.25)
            {
                contextRunning = false; previewing = false;
            }
        }
        else if (contextRunning.load())
        {
            contextRunning = false; previewing = false;
        }
    }

    if (! previewing.load()) return;

    const bool muteWanted = apvts.getRawParameterValue (ParamID::muteInput)->load() > 0.5f;
    const int outCh = mainOut.getNumChannels();
    int64_t pos = previewPos.load();
    const float* mL = current->mix.getReadPointer (0);
    const float* mR = current->mix.getReadPointer (1);
    for (int i = startOffset; i < n; ++i)
    {
        if (pos >= total) break;
        const bool mute = muteWanted && (contextMode ? pos >= pre : true);
        const float l = mL[pos], r = mR[pos];
        if (outCh == 1)
        {
            float* o = mainOut.getWritePointer (0);
            o[i] = (mute ? 0.0f : o[i]) + 0.5f * (l + r);
        }
        else if (outCh >= 2)
        {
            float* oL = mainOut.getWritePointer (0); float* oR = mainOut.getWritePointer (1);
            oL[i] = (mute ? 0.0f : oL[i]) + l;
            oR[i] = (mute ? 0.0f : oR[i]) + r;
        }
        ++pos;
    }
    previewPos = pos;
    if (pos >= total) { previewing = false; if (! contextMode) previewPos = 0; }
}

//==============================================================================
void NeuroDropProcessor::startCapture (int slot)
{
    if (captureSlot.load() >= 0) stopCapture();
    capPpqStart = -1.0; capPpqEnd = -1.0; capBpm = 0.0;
    live.begin (getSampleRate() > 0 ? getSampleRate() : currentSr);
    captureSlot = jlimit (0, 1, slot);
}

void NeuroDropProcessor::stopCapture()
{
    const int slot = captureSlot.exchange (-1);
    if (slot < 0) return;
    auto s = live.end();
    s.hostBpm = capBpm.load() > 0 ? capBpm.load() : hostBpm.load();
    s.ppqStart = capPpqStart.load();
    s.ppqEnd = capPpqEnd.load();
    {
        std::lock_guard<std::mutex> g (snapLock);
        if (s.valid) snapshots[(size_t) slot] = s;
    }
    updateDropPpq();
}

nd::AnalysisSnapshot NeuroDropProcessor::getSnapshot (int slot) const
{
    std::lock_guard<std::mutex> g (snapLock);
    return snapshots[(size_t) jlimit (0, 1, slot)];
}

nd::AnalysisSnapshot NeuroDropProcessor::getLiveSnapshot()
{
    auto s = live.snapshot();
    s.hostBpm = capBpm.load(); s.ppqStart = capPpqStart.load(); s.ppqEnd = capPpqEnd.load();
    return s;
}

void NeuroDropProcessor::clearSnapshot (int slot)
{
    {
        std::lock_guard<std::mutex> g (snapLock);
        snapshots[(size_t) jlimit (0, 1, slot)] = {};
    }
    updateDropPpq();
}

void NeuroDropProcessor::updateDropPpq()
{
    const auto b = getSnapshot (0);
    dropPpqCache = (b.valid && b.ppqEnd >= 0.0) ? std::round (b.ppqEnd / 4.0) * 4.0 : -1.0;
}

//==============================================================================
double NeuroDropProcessor::effectiveHostBpm() const
{
    if (hostBpm.load() > 30.0) return hostBpm.load();
    const auto b = getSnapshot (0);
    if (b.hostBpm > 30.0) return b.hostBpm;
    const auto a = getSnapshot (1);
    if (a.hostBpm > 30.0) return a.hostBpm;
    return 145.0;
}

nd::DropSettings NeuroDropProcessor::buildSettings() const
{
    auto raw = [&] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto rawS = [&] (const String& id) { return apvts.getRawParameterValue (id)->load(); };
    nd::DropSettings s;
    s.style      = (nd::Style) (int) raw (ParamID::style);
    s.groove     = (nd::Groove) (int) raw (ParamID::groove);
    s.tempoMode  = (nd::TempoMode) (int) raw (ParamID::tempoMode);
    s.targetBpm  = raw (ParamID::targetBpm);
    s.gridLock   = raw (ParamID::gridLock) > 0.5f;
    s.hostBpm    = effectiveHostBpm();
    s.bars       = barChoices()[(int) raw (ParamID::bars)].getIntValue();
    s.preRollBars= preRollChoices()[(int) raw (ParamID::preRoll)].getIntValue();
    s.transitionOut = raw (ParamID::transOut) > 0.5f;
    s.keyOverride = (int) raw (ParamID::key) - 1;
    s.scaleMode  = (int) raw (ParamID::scale);
    s.tuningMode = (int) raw (ParamID::tuning);
    s.intensity  = raw (ParamID::intensity);
    s.aggression = raw (ParamID::aggression);
    s.match      = raw (ParamID::match);
    s.loudnessMatch = raw (ParamID::loudMatch) > 0.5f;
    s.seed       = (uint64_t) raw (ParamID::seed);
    for (int i = 0; i < nd::NumStems; ++i)
    {
        s.stemGainDb[(size_t) i] = rawS (ParamID::gain (i));
        s.stemOn[(size_t) i] = rawS (ParamID::on (i)) > 0.5f;
    }
    s.before = getSnapshot (0);
    s.after = getSnapshot (1);
    s.sampleRate = getSampleRate() > 0 ? getSampleRate() : currentSr;
    return s;
}

nd::TempoPlan NeuroDropProcessor::previewTempoPlan() const { return nd::planTempo (buildSettings()); }

void NeuroDropProcessor::generate (bool newSeed)
{
    if (generating.exchange (true)) return;
    if (newSeed)
        if (auto* p = apvts.getParameter (ParamID::seed))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) Random::getSystemRandom().nextInt ({ 1, 999999 })));
    progress = 0.0f;
    auto s = buildSettings();
    pool.addJob ([this, s] { renderJob (s); });
}

void NeuroDropProcessor::renderJob (nd::DropSettings s)
{
    auto r = nd::renderDrop (s, [this] (float p) { progress = p * 0.92f; return ! shuttingDown.load(); });
    if (r == nullptr)
    {
        { std::lock_guard<std::mutex> g (viewLock); lastError = "Rendern abgebrochen"; }
        generating = false;
        return;
    }
    auto ex = nd::exportStems (*r, nd::defaultRenderRoot());

    auto v = std::make_shared<RenderView>();
    v->mixPeaks = makePeaks (r->mix, 1200);
    for (int i = 0; i < nd::NumStems; ++i) { v->stemPeaks[(size_t) i] = makePeaks (r->stems[(size_t) i], 600); v->active[(size_t) i] = r->active[(size_t) i]; }
    v->pre = r->preRollSamples; v->total = r->totalSamples; v->bars = r->bars;
    v->bpm = r->bpm; v->hostBpm = r->hostBpm; v->sampleRate = r->sampleRate; v->hostBars = r->hostBars;
    v->summary = r->summary; v->files = ex;

    previewing = false; contextRunning = false; previewPos = 0;
    std::unique_ptr<nd::RenderResult> old;
    {
        const SpinLock::ScopedLockType l (previewLock);
        old = std::move (current);
        current = std::move (r);
        previewTotal = current->totalSamples;
    }
    old.reset();
    {
        std::lock_guard<std::mutex> g (viewLock);
        view = v;
        lastError = ex.ok() ? String() : ex.error;
    }
    progress = 1.0f;
    generating = false;
}

std::shared_ptr<const RenderView> NeuroDropProcessor::getView() const
{
    std::lock_guard<std::mutex> g (viewLock);
    return view;
}

String NeuroDropProcessor::getLastError() const
{
    std::lock_guard<std::mutex> g (viewLock);
    return lastError;
}

//==============================================================================
void NeuroDropProcessor::setPreview (bool on)
{
    contextArmed = false; contextRunning = false;
    previewPos = 0;
    previewing = on && previewTotal.load() > 0;
}

void NeuroDropProcessor::setContextPreview (bool armed)
{
    previewing = false; contextRunning = false; previewPos = 0;
    contextArmed = armed && canContextPreview();
}

bool NeuroDropProcessor::canContextPreview() const
{
    return previewTotal.load() > 0 && getContextDropPpq() >= 0.0;
}

double NeuroDropProcessor::getContextDropPpq() const
{
    return dropPpqCache.load();
}

double NeuroDropProcessor::getPreviewPosition() const
{
    const int t = previewTotal.load();
    return t > 0 ? jlimit (0.0, 1.0, (double) previewPos.load() / t) : 0.0;
}

//==============================================================================
void NeuroDropProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<XmlElement> xml (state.createXml());
    if (xml == nullptr) return;
    xml->addChildElement (getSnapshot (0).toXml ("AnalysisBefore").release());
    xml->addChildElement (getSnapshot (1).toXml ("AnalysisAfter").release());
    copyXmlToBinary (*xml, destData);
}

void NeuroDropProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    {
        std::lock_guard<std::mutex> g (snapLock);
        snapshots[0] = nd::AnalysisSnapshot::fromXml (xml->getChildByName ("AnalysisBefore"));
        snapshots[1] = nd::AnalysisSnapshot::fromXml (xml->getChildByName ("AnalysisAfter"));
    }
    updateDropPpq();
    if (auto* c = xml->getChildByName ("AnalysisBefore")) xml->removeChildElement (c, true);
    if (auto* c = xml->getChildByName ("AnalysisAfter"))  xml->removeChildElement (c, true);
    apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* NeuroDropProcessor::createEditor() { return new NeuroDropEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NeuroDropProcessor(); }
