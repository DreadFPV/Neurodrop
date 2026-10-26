#include "Analysis.h"

namespace nd
{
//==============================================================================
juce::String AnalysisSnapshot::describe() const
{
    if (! valid) return "Keine Analyse";
    juce::String s;
    s << noteName (keyTonic) << (keyMinor ? " Moll" : " Dur")
      << "  |  A4 " << juce::String (tuningA4, 1) << " Hz"
      << "  |  " << juce::String (rmsDb, 1) << " dB RMS";
    if (kickHz > 0) s << "  |  Kick " << juce::String (kickHz, 1) << " Hz";
    return s;
}

std::unique_ptr<juce::XmlElement> AnalysisSnapshot::toXml (const juce::String& tag) const
{
    auto x = std::make_unique<juce::XmlElement> (tag);
    x->setAttribute ("valid", valid);
    x->setAttribute ("seconds", seconds);
    x->setAttribute ("keyTonic", keyTonic);
    x->setAttribute ("keyMinor", keyMinor);
    x->setAttribute ("keyConf", (double) keyConfidence);
    x->setAttribute ("a4", (double) tuningA4);
    x->setAttribute ("cents", (double) tuningCents);
    x->setAttribute ("rms", (double) rmsDb);
    x->setAttribute ("peak", (double) peakDb);
    x->setAttribute ("centroid", (double) centroidHz);
    x->setAttribute ("kickHz", (double) kickHz);
    x->setAttribute ("bassPc", bassPc);
    x->setAttribute ("onsetRate", (double) onsetRate);
    x->setAttribute ("energy", (double) energy);
    x->setAttribute ("bpm", hostBpm);
    x->setAttribute ("ppqStart", ppqStart);
    x->setAttribute ("ppqEnd", ppqEnd);
    juce::StringArray b;
    for (auto v : bandsDb) b.add (juce::String (v, 2));
    x->setAttribute ("bands", b.joinIntoString (","));
    return x;
}

AnalysisSnapshot AnalysisSnapshot::fromXml (const juce::XmlElement* x)
{
    AnalysisSnapshot s;
    if (x == nullptr) return s;
    s.valid        = x->getBoolAttribute ("valid", false);
    s.seconds      = x->getDoubleAttribute ("seconds");
    s.keyTonic     = x->getIntAttribute ("keyTonic", 5);
    s.keyMinor     = x->getBoolAttribute ("keyMinor", true);
    s.keyConfidence= (float) x->getDoubleAttribute ("keyConf");
    s.tuningA4     = (float) x->getDoubleAttribute ("a4", 440.0);
    s.tuningCents  = (float) x->getDoubleAttribute ("cents");
    s.rmsDb        = (float) x->getDoubleAttribute ("rms", -60.0);
    s.peakDb       = (float) x->getDoubleAttribute ("peak", -60.0);
    s.centroidHz   = (float) x->getDoubleAttribute ("centroid");
    s.kickHz       = (float) x->getDoubleAttribute ("kickHz");
    s.bassPc       = x->getIntAttribute ("bassPc", -1);
    s.onsetRate    = (float) x->getDoubleAttribute ("onsetRate");
    s.energy       = (float) x->getDoubleAttribute ("energy");
    s.hostBpm      = x->getDoubleAttribute ("bpm");
    s.ppqStart     = x->getDoubleAttribute ("ppqStart", -1.0);
    s.ppqEnd       = x->getDoubleAttribute ("ppqEnd", -1.0);
    auto parts = juce::StringArray::fromTokens (x->getStringAttribute ("bands"), ",", "");
    for (int i = 0; i < kNumBands && i < parts.size(); ++i) s.bandsDb[(size_t) i] = parts[i].getFloatValue();
    return s;
}

//==============================================================================
AnalysisAccumulator::AnalysisAccumulator()
{
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / (fftSize - 1)));
    ring.assign ((size_t) fftSize, 0.0f);
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    prepare (48000.0);
}

void AnalysisAccumulator::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    lowRing.assign ((size_t) (sr * 0.3) + 8, 0.0f);
    reset();
}

void AnalysisAccumulator::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringPos = filled = sinceHop = 0;
    bandPow.fill (0); frames = 0; centroidSum = centroidW = 0;
    pitchHist.fill (0); tuneX = tuneY = 0; bassPcHist.fill (0);
    lp1.set (Biquad::Type::LowPass, sr, 150.0, 0.707); lp1.reset();
    lp2.set (Biquad::Type::LowPass, sr, 150.0, 0.707); lp2.reset();
    eShort = eLong = 0; sampleIndex = 0; lastOnset = -1000000;
    std::fill (lowRing.begin(), lowRing.end(), 0.0f); lowPos = 0;
    pendingOnsets.clear(); kickHist.fill (0); onsets = 0;
    sumSq = 0; loudSamples = 0; peak = 0;
}

void AnalysisAccumulator::process (const float* mono, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float x = mono[i];
        ring[(size_t) ringPos] = x;
        ringPos = (ringPos + 1) % fftSize;
        if (filled < fftSize) ++filled;

        const float ax = std::abs (x);
        if (ax > 1.0e-4f) { sumSq += (double) x * x; ++loudSamples; }
        peak = std::max (peak, ax);

        timeDomain (x);

        if (++sinceHop >= hopSize && filled >= fftSize)
        {
            sinceHop = 0;
            analyseFrame();
        }
    }
}

void AnalysisAccumulator::timeDomain (float x)
{
    const float l = lp2.process (lp1.process (x));
    lowRing[(size_t) lowPos] = l;
    lowPos = (lowPos + 1) % (int) lowRing.size();

    const double x2 = (double) l * l;
    const double aS = 1.0 - std::exp (-1.0 / (0.004 * sr));
    const double aL = 1.0 - std::exp (-1.0 / (0.200 * sr));
    eShort += (x2 - eShort) * aS;
    eLong  += (x2 - eLong)  * aL;

    if (eShort > 3.0 * eLong && eShort > 1.0e-4 && sampleIndex - lastOnset > (int64_t) (0.12 * sr))
    {
        lastOnset = sampleIndex;
        ++onsets;
        pendingOnsets.push_back (sampleIndex);
    }

    const int64_t wait = (int64_t) (0.105 * sr);
    while (! pendingOnsets.empty() && sampleIndex - pendingOnsets.front() >= wait)
    {
        measureKick (pendingOnsets.front());
        pendingOnsets.erase (pendingOnsets.begin());
    }
    ++sampleIndex;
}

void AnalysisAccumulator::measureKick (int64_t onset)
{
    // Kick-Tuning = Frequenz im Ausklang. Gemessen wird die letzte volle Schwingung
    // zwischen 20 und 98 ms nach dem Onset (danach setzt bei Psy der Offbeat-Bass ein).
    const int L = (int) (0.078 * sr);
    const int64_t start = onset + (int64_t) (0.02 * sr);
    const int size = (int) lowRing.size();
    std::vector<float> seg ((size_t) L);
    for (int i = 0; i < L; ++i)
    {
        const int64_t s = start + i;
        int idx = lowPos - (int) (sampleIndex - s) - 1;
        while (idx < 0) idx += size;
        seg[(size_t) i] = lowRing[(size_t) (idx % size)];
    }
    double energy = 0; for (auto v : seg) energy += (double) v * v;
    if (energy / L < 1.0e-7) return;

    // positive Nulldurchgaenge (linear interpoliert)
    std::vector<double> zc;
    for (int i = 1; i < L; ++i)
        if (seg[(size_t) i - 1] < 0.0f && seg[(size_t) i] >= 0.0f)
        {
            const double a = seg[(size_t) i - 1], b = seg[(size_t) i];
            zc.push_back ((i - 1) + (-a) / (b - a));
        }
    if (zc.size() < 2) return;
    const double period = zc[zc.size() - 1] - zc[zc.size() - 2];
    if (period <= 0) return;
    const double hz = sr / period;
    if (hz < 30.0 || hz > 130.0) return;
    const double midi = hzToMidi (hz, 440.0);
    const int bin = (int) std::lround ((midi - 20.0) * 4.0);
    if (bin >= 0 && bin < (int) kickHist.size()) kickHist[(size_t) bin] += 1.0;
}

void AnalysisAccumulator::analyseFrame()
{
    // Rahmen aus Ringpuffer (aelteste Samples zuerst) + Fenster
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = ring[(size_t) ((ringPos + i) % fftSize)] * window[(size_t) i];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

    double frameEnergy = 0;
    for (int i = 0; i < fftSize; ++i) frameEnergy += (double) fftData[(size_t) i] * fftData[(size_t) i];
    if (frameEnergy / fftSize < 1.0e-7) return; // Stille ignorieren

    fft->performFrequencyOnlyForwardTransform (fftData.data(), true);
    const int nb = fftSize / 2;
    const double binHz = sr / fftSize;
    const double norm = 2.0 / (fftSize * 0.5);  // Hann-Summe = N/2
    for (int k = 0; k <= nb; ++k) fftData[(size_t) k] = (float) (fftData[(size_t) k] * norm);

    // Baender + Schwerpunkt
    const auto& e = bandEdges();
    for (int k = 1; k < nb; ++k)
    {
        const double f = k * binHz;
        const double m = fftData[(size_t) k];
        const double p = m * m;
        for (int b = 0; b < kNumBands; ++b)
            if (f >= e[(size_t) b] && f < e[(size_t) b + 1]) { bandPow[(size_t) b] += p; break; }
        if (f >= 100.0 && f <= 16000.0) { centroidSum += f * m; centroidW += m; }
    }

    // Tonhoehen-Peaks
    const int kLo = (int) (55.0 / binHz), kHi = std::min (nb - 2, (int) (2000.0 / binHz));
    float frameMax = 0;
    for (int k = kLo; k <= kHi; ++k) frameMax = std::max (frameMax, fftData[(size_t) k]);
    if (frameMax > 0)
    {
        for (int k = std::max (2, kLo); k <= kHi; ++k)
        {
            const float m = fftData[(size_t) k];
            if (m < frameMax * 0.08f || m <= fftData[(size_t) k - 1] || m < fftData[(size_t) k + 1]) continue;
            const double a = std::log (fftData[(size_t) k - 1] + 1e-12), b = std::log (m + 1e-12), c = std::log (fftData[(size_t) k + 1] + 1e-12);
            const double den = a - 2 * b + c;
            const double p = std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
            const double f = (k + p) * binHz;
            if (f < 50.0) continue;
            const double cents = hzToMidi (f, 440.0) * 100.0;
            const double w = std::sqrt ((double) m / frameMax);
            double inOct = std::fmod (cents, 1200.0); if (inOct < 0) inOct += 1200.0;
            pitchHist[(size_t) std::min (1199, (int) inOct)] += w;
            if (f >= 150.0)
            {
                const double dev = cents - 100.0 * std::round (cents / 100.0);
                const double ang = kTwoPi * dev / 100.0;
                tuneX += w * std::cos (ang); tuneY += w * std::sin (ang);
            }
        }
    }

    // Bass-Grundton (40..140 Hz)
    {
        const int bLo = (int) (40.0 / binHz), bHi = (int) (140.0 / binHz);
        int best = -1; float bm = 0;
        for (int k = bLo; k <= bHi; ++k) if (fftData[(size_t) k] > bm) { bm = fftData[(size_t) k]; best = k; }
        if (best > bLo && best < bHi)
        {
            const double a = std::log (fftData[(size_t) best - 1] + 1e-12), b = std::log (bm + 1e-12), c = std::log (fftData[(size_t) best + 1] + 1e-12);
            const double den = a - 2 * b + c;
            const double p = std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
            const double f = (best + p) * binHz;
            const int midi = (int) std::lround (hzToMidi (f, 440.0));
            bassPcHist[(size_t) (((midi % 12) + 12) % 12)] += bm;
        }
    }
    ++frames;
}

AnalysisSnapshot AnalysisAccumulator::result() const
{
    AnalysisSnapshot s;
    if (frames < 3) return s;
    s.valid = true;
    s.seconds = (double) loudSamples / sr;

    for (int b = 0; b < kNumBands; ++b)
        s.bandsDb[(size_t) b] = (float) (10.0 * std::log10 (bandPow[(size_t) b] / frames + 1.0e-12));
    s.centroidHz = centroidW > 0 ? (float) (centroidSum / centroidW) : 0.0f;

    // Tuning
    const double offset = (std::abs (tuneX) + std::abs (tuneY) > 0.0) ? std::atan2 (tuneY, tuneX) * 100.0 / kTwoPi : 0.0;
    double a4 = 440.0 * std::pow (2.0, offset / 1200.0);
    if (std::abs (a4 - 432.0) < 1.6) a4 = 432.0;
    else if (std::abs (a4 - 440.0) < 1.6) a4 = 440.0;
    s.tuningA4 = (float) a4;
    s.tuningCents = (float) offset;

    // Chroma (tuning-korrigiert)
    std::array<double, 12> chroma {};
    for (int c = 0; c < 1200; ++c)
    {
        const int pc = (((int) std::lround ((c - offset) / 100.0)) % 12 + 12) % 12;
        chroma[(size_t) pc] += pitchHist[(size_t) c];
    }
    static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    auto corr = [&] (const double* prof, int tonic)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) ((i + tonic) % 12)]; my += prof[i]; }
        mx /= 12; my /= 12;
        double num = 0, dx = 0, dy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = chroma[(size_t) ((i + tonic) % 12)] - mx, y = prof[i] - my;
            num += x * y; dx += x * x; dy += y * y;
        }
        return num / std::sqrt (dx * dy + 1e-12);
    };

    // Bass-Grundton (stark gewichtet bei Psy: der Bass sitzt fast immer auf dem Grundton)
    double bassSum = 0, bassMax = 0; int bassArg = -1;
    for (int i = 0; i < 12; ++i) { bassSum += bassPcHist[(size_t) i]; if (bassPcHist[(size_t) i] > bassMax) { bassMax = bassPcHist[(size_t) i]; bassArg = i; } }
    const bool bassConfident = bassSum > 0 && bassMax / bassSum > 0.3;
    s.bassPc = bassConfident ? bassArg : -1;

    double best = -2, second = -2; int bestT = 0; bool bestMinor = true;
    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m)
        {
            double c = corr (m ? mnr : maj, t);
            if (bassConfident && t == bassArg) c += 0.12;
            if (m) c += 0.02; // leichte Moll-Praeferenz (Psy/DnB)
            if (c > best) { second = best; best = c; bestT = t; bestMinor = m != 0; }
            else if (c > second) second = c;
        }
    s.keyTonic = bestT;
    s.keyMinor = bestMinor;
    s.keyConfidence = clampf ((float) ((best - second) / 0.15 + 0.3 * std::max (0.0, best)), 0.0f, 1.0f);

    // Kick
    double kBest = 0; int kArg = -1;
    for (int i = 1; i + 1 < (int) kickHist.size(); ++i)
    {
        const double v = kickHist[(size_t) i - 1] * 0.5 + kickHist[(size_t) i] + kickHist[(size_t) i + 1] * 0.5;
        if (v > kBest) { kBest = v; kArg = i; }
    }
    s.kickHz = kArg >= 0 ? (float) midiToHz (20.0 + kArg * 0.25, 440.0) : 0.0f;

    // Pegel & Energie
    s.rmsDb  = loudSamples > 0 ? (float) (10.0 * std::log10 (sumSq / (double) loudSamples + 1e-12)) : -60.0f;
    s.peakDb = (float) gainToDb (peak);
    const double secs = std::max (0.1, (double) sampleIndex / sr);
    s.onsetRate = (float) (onsets / secs);
    s.energy = clampf (0.55f * clampf ((s.rmsDb + 24.0f) / 16.0f, 0.0f, 1.0f)
                     + 0.25f * clampf (s.onsetRate / 4.0f, 0.0f, 1.0f)
                     + 0.20f * s.brightness(), 0.0f, 1.0f);
    return s;
}

AnalysisSnapshot AnalysisAccumulator::analyse (const float* mono, int numSamples, double sampleRate)
{
    AnalysisAccumulator a;
    a.prepare (sampleRate);
    a.process (mono, numSamples);
    return a.result();
}

//==============================================================================
LiveAnalyzer::LiveAnalyzer() : juce::Thread ("NeuroDrop Analyse")
{
    fifoBuf.assign ((size_t) fifo.getTotalSize(), 0.0f);
}

LiveAnalyzer::~LiveAnalyzer()
{
    active = false;
    stopThread (2000);
}

void LiveAnalyzer::begin (double sampleRate)
{
    stopThread (2000);
    {
        std::lock_guard<std::mutex> g (lock);
        acc.prepare (sampleRate);
    }
    fifo.reset();
    active = true;
    startThread (juce::Thread::Priority::low);
}

AnalysisSnapshot LiveAnalyzer::end()
{
    active = false;
    stopThread (2000);
    drain();
    std::lock_guard<std::mutex> g (lock);
    return acc.result();
}

void LiveAnalyzer::push (const float* mono, int n)
{
    if (! active.load()) return;
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    if (n1 > 0) std::copy (mono, mono + n1, fifoBuf.begin() + s1);
    if (n2 > 0) std::copy (mono + n1, mono + n1 + n2, fifoBuf.begin() + s2);
    fifo.finishedWrite (n1 + n2);   // bei vollem FIFO werden Samples verworfen
}

AnalysisSnapshot LiveAnalyzer::snapshot()
{
    std::lock_guard<std::mutex> g (lock);
    return acc.result();
}

void LiveAnalyzer::drain()
{
    for (;;)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0) break;
        int s1, n1, s2, n2;
        fifo.prepareToRead (ready, s1, n1, s2, n2);
        {
            std::lock_guard<std::mutex> g (lock);
            if (n1 > 0) acc.process (fifoBuf.data() + s1, n1);
            if (n2 > 0) acc.process (fifoBuf.data() + s2, n2);
        }
        fifo.finishedRead (n1 + n2);
    }
}

void LiveAnalyzer::run()
{
    while (! threadShouldExit())
    {
        drain();
        wait (20);
    }
}
} // namespace nd
