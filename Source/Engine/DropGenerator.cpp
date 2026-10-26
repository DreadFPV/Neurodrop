#include "DropGenerator.h"
#include "Voices.h"
#include <deque>

namespace nd
{
//============================================================================== Tempo
TempoPlan planTempo (const DropSettings& s)
{
    TempoPlan p;
    const double host = s.hostBpm > 30.0 ? s.hostBpm : 145.0;
    const int bars = std::max (1, s.bars);

    if (s.tempoMode == TempoMode::HostTempo)
    {
        p.bpm = host; p.hostBars = bars; p.locked = true;
        p.text = juce::String (bars) + " Takte @ " + juce::String (host, 1) + " BPM (Host-Tempo, Halftime-Feel)";
        return p;
    }

    const double target = juce::jlimit (150.0, 190.0, s.targetBpm);
    const double hb = bars * host / target;
    p.bpm = target; p.hostBars = hb;

    if (s.gridLock)
    {
        double bestBpm = 0; int bestM = 0; bool barLock = false;
        for (int M : { (int) std::floor (hb), (int) std::ceil (hb) })
        {
            if (M < 1) continue;
            const double b = host * bars / M;
            if (b >= 158.0 && b <= 186.0 && (bestBpm <= 0 || std::abs (b - target) < std::abs (bestBpm - target))) { bestBpm = b; bestM = M; barLock = true; }
        }
        if (! barLock)
            for (int B : { (int) std::floor (hb * 4.0), (int) std::ceil (hb * 4.0) })
            {
                if (B < 1) continue;
                const double b = host * bars * 4.0 / B;
                if (b >= 158.0 && b <= 186.0 && (bestBpm <= 0 || std::abs (b - target) < std::abs (bestBpm - target))) { bestBpm = b; bestM = B; }
            }
        if (bestBpm > 0)
        {
            p.bpm = bestBpm; p.locked = true;
            if (barLock)
            {
                p.hostBars = bestM;
                p.text = juce::String (bars) + " DnB-Takte @ " + juce::String (bestBpm, 2) + " BPM = genau " + juce::String (bestM) + " Host-Takte";
            }
            else
            {
                p.hostBars = bestM / 4.0;
                p.text = juce::String (bars) + " DnB-Takte @ " + juce::String (bestBpm, 2) + " BPM = genau " + juce::String (bestM / 4) + " Takte + "
                       + juce::String (bestM % 4) + " Beats im Host";
            }
            return p;
        }
    }
    p.text = juce::String (bars) + " DnB-Takte @ " + juce::String (target, 1) + " BPM = " + juce::String (hb, 2) + " Host-Takte (nicht im Raster)";
    return p;
}

//============================================================================== Helfer
namespace
{
void placeMono (Stereo& dst, const std::vector<float>& src, double pos, float gainL, float gainR)
{
    const int64_t p = (int64_t) std::llround (pos);
    const int64_t N = (int64_t) dst.size();
    for (size_t i = 0; i < src.size(); ++i)
    {
        const int64_t idx = p + (int64_t) i;
        if (idx < 0) continue;
        if (idx >= N) break;
        dst.l[(size_t) idx] += src[i] * gainL;
        dst.r[(size_t) idx] += src[i] * gainR;
    }
}

void placeStereo (Stereo& dst, const Stereo& src, double pos, float gL, float gR)
{
    const int64_t p = (int64_t) std::llround (pos);
    const int64_t N = (int64_t) dst.size();
    for (size_t i = 0; i < src.size(); ++i)
    {
        const int64_t idx = p + (int64_t) i;
        if (idx < 0) continue;
        if (idx >= N) break;
        dst.l[(size_t) idx] += src.l[i] * gL;
        dst.r[(size_t) idx] += src.r[i] * gR;
    }
}

std::pair<float, float> panGains (float pan) // -1..1, konstante Leistung
{
    const double a = (pan + 1.0) * 0.25 * kPi;
    return { (float) (std::cos (a) * 1.41421356), (float) (std::sin (a) * 1.41421356) };
}

void toBuffer (const Stereo& s, juce::AudioBuffer<float>& b)
{
    b.setSize (2, (int) s.size(), false, true, false);
    std::copy (s.l.begin(), s.l.end(), b.getWritePointer (0));
    std::copy (s.r.begin(), s.r.end(), b.getWritePointer (1));
}

float bufferPeak (const juce::AudioBuffer<float>& b) { return b.getNumSamples() > 0 ? b.getMagnitude (0, b.getNumSamples()) : 0.0f; }

double regionRmsDb (const juce::AudioBuffer<float>& b, int from)
{
    double sum = 0; int64_t n = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* d = b.getReadPointer (ch);
        for (int i = from; i < b.getNumSamples(); ++i) { sum += (double) d[i] * d[i]; ++n; }
    }
    return n > 0 ? 10.0 * std::log10 (sum / n + 1e-12) : -120.0;
}

double nearestScaleHz (const Key& k, double a4, double targetHz, int midiLo, int midiHi, bool rootOrFifthOnly)
{
    double best = 0, bestDist = 1e9;
    for (int m = midiLo; m <= midiHi; ++m)
    {
        const int rel = ((m - k.tonic) % 12 + 12) % 12;
        if (rootOrFifthOnly ? (rel != 0 && rel != 7) : ! k.contains (rel)) continue;
        const double hz = midiToHz (m, a4);
        const double d = std::abs (std::log2 (hz / targetHz));
        if (d < bestDist) { bestDist = d; best = hz; }
    }
    return best > 0 ? best : targetHz;
}

double groupDb (const std::array<float, kNumBands>& b, const std::vector<int>& idx)
{
    double p = 0; int n = 0;
    for (int i : idx) { p += std::pow (10.0, b[(size_t) i] / 10.0); ++n; }
    return 10.0 * std::log10 (p / std::max (1, n) + 1e-12);
}
} // namespace

//============================================================================== Render
std::unique_ptr<RenderResult> renderDrop (const DropSettings& s, const std::function<bool (float)>& progress)
{
    auto step = [&] (float v) { return progress ? progress (v) : true; };

    auto r = std::make_unique<RenderResult>();
    const TempoPlan plan = planTempo (s);
    const double sr = s.sampleRate > 0 ? s.sampleRate : 48000.0;
    const double host = s.hostBpm > 30.0 ? s.hostBpm : 145.0;
    const AnalysisSnapshot& ref = s.before.valid ? s.before : s.after;

    // Tonart & Tuning
    Key key;
    key.tonic = s.keyOverride >= 0 ? s.keyOverride : (ref.valid ? ref.keyTonic : 5);
    switch (s.scaleMode)
    {
        case 1:  key.scale = ScaleType::Minor; break;
        case 2:  key.scale = ScaleType::Phrygian; break;
        case 3:  key.scale = ScaleType::Major; break;
        default: key.scale = (ref.valid && ! ref.keyMinor) ? ScaleType::Major : ScaleType::Minor; break;
    }
    double a4 = 440.0;
    if (s.tuningMode == 1) a4 = 440.0;
    else if (s.tuningMode == 2) a4 = 432.0;
    else if (ref.valid) a4 = ref.tuningA4;

    Key outKey = key;
    if (s.after.valid) { outKey.tonic = s.after.keyTonic; outKey.scale = s.after.keyMinor ? ScaleType::Minor : ScaleType::Major; }

    r->sampleRate = sr; r->bpm = plan.bpm; r->hostBpm = host; r->a4 = a4; r->hostBars = plan.hostBars;
    r->bars = std::max (1, s.bars); r->key = key; r->outKey = outKey;

    PatternParams pp;
    pp.style = s.style; pp.groove = s.groove; pp.bars = r->bars; pp.transitionOut = s.transitionOut;
    pp.intensity = s.intensity; pp.aggression = s.aggression; pp.key = key; pp.seed = s.seed;
    const Arrangement arr = buildArrangement (pp);

    const double stepSamples = sr * 60.0 / plan.bpm / 4.0;
    const int pre = (int) std::llround (std::max (0, s.preRollBars) * 4.0 * 60.0 / host * sr);
    const int drop = (int) std::llround (r->bars * 16.0 * stepSamples);
    const int total = pre + drop;
    r->preRollSamples = pre; r->dropSamples = drop; r->totalSamples = total;

    VoiceContext vc;
    vc.sr = sr; vc.bpm = plan.bpm; vc.a4 = a4; vc.key = key;
    vc.aggression = s.aggression; vc.intensity = s.intensity;
    vc.brightness = ref.valid ? ref.brightness() : 0.5f;
    vc.seed = s.seed;

    std::array<Stereo, NumStems> st;
    for (auto& x : st) x.resize ((size_t) total);
    auto posOf = [&] (double stepPos) { return pre + stepPos * stepSamples; };

    if (! step (0.05f)) return nullptr;

    //---------------------------------------------------------------- Kick + Ducking
    const double kickTarget = (ref.valid && ref.kickHz > 30.0f) ? ref.kickHz : midiToHz (bassRootMidi (key.tonic), a4);
    const double kickHz = nearestScaleHz (key, a4, juce::jlimit (38.0, 68.0, kickTarget), 24, 47, true);
    const auto kick = makeKick (vc, kickHz, 0.5f + 0.5f * s.aggression);
    std::vector<float> duck ((size_t) total, 1.0f);
    for (const auto& h : arr.kick)
    {
        const double p = posOf (h.step);
        placeMono (st[StemKick], kick, p, h.vel, h.vel);
        const int64_t p0 = (int64_t) p;
        const int64_t len = (int64_t) (0.25 * sr);
        for (int64_t i = 0; i < len; ++i)
        {
            const int64_t idx = p0 + i; if (idx < 0 || idx >= total) continue;
            const double t = i / sr;
            const double att = std::min (1.0, t / 0.004);
            const float d = (float) (1.0 - h.vel * att * std::exp (-t / 0.075));
            duck[(size_t) idx] = std::min (duck[(size_t) idx], d);
        }
    }
    // Ducking-Kurve glaetten (vorwaerts + rueckwaerts, ~2 ms) -> keine Klicks im Sub
    {
        const float c = (float) (1.0 - std::exp (-1.0 / (0.002 * sr)));
        float y = 1.0f;
        for (int i = 0; i < total; ++i)          { y += (duck[(size_t) i] - y) * c; duck[(size_t) i] = y; }
        y = 1.0f;
        for (int i = total - 1; i >= 0; --i)     { y += (duck[(size_t) i] - y) * c; duck[(size_t) i] = y; }
    }
    if (! step (0.15f)) return nullptr;

    //---------------------------------------------------------------- Snare
    const double snareHz = nearestScaleHz (key, a4, 200.0, 48, 62, false);
    const auto snMain  = makeSnare (vc, snareHz, false, s.seed + 11);
    const auto snGhost = makeSnare (vc, snareHz, true,  s.seed + 12);
    for (const auto& h : arr.snare)
    {
        const auto& src = h.variant == 1 ? snGhost : snMain;
        const float pan = h.variant == 1 ? 0.15f : 0.0f;
        auto g = panGains (pan);
        placeMono (st[StemSnare], src, posOf (h.step), h.vel * g.first, h.vel * g.second);
    }
    {
        juce::Reverb rv; juce::Reverb::Parameters p;
        p.roomSize = 0.35f; p.damping = 0.5f; p.wetLevel = 0.14f; p.dryLevel = 1.0f; p.width = 1.0f;
        rv.setParameters (p); rv.setSampleRate (sr);
        rv.processStereo (st[StemSnare].l.data(), st[StemSnare].r.data(), total);
    }
    if (! step (0.22f)) return nullptr;

    //---------------------------------------------------------------- Hats
    {
        std::vector<Stereo> closed;
        for (int i = 0; i < 4; ++i) closed.push_back (makeHat (vc, HatClosed, s.seed * 13 + (uint64_t) i));
        const Stereo open = makeHat (vc, HatOpen, s.seed + 31);
        const Stereo crash = makeHat (vc, HatCrash, s.seed + 32);
        const Stereo shaker = makeHat (vc, HatShaker, s.seed + 33);
        int rr = 0;
        for (const auto& h : arr.hats)
        {
            const double p = posOf (h.step);
            switch (h.variant)
            {
                case HatOpen:   placeStereo (st[StemHats], open, p, h.vel, h.vel); break;
                case HatCrash:  placeStereo (st[StemHats], crash, p, h.vel * 0.8f, h.vel * 0.8f); break;
                case HatShaker: { auto g = panGains (((int) h.step % 2) ? 0.3f : -0.3f); placeStereo (st[StemHats], shaker, p, h.vel * g.first, h.vel * g.second); break; }
                default:        { auto g = panGains ((rr % 2) ? 0.12f : -0.12f); placeStereo (st[StemHats], closed[(size_t) (rr++ % 4)], p, h.vel * g.first, h.vel * g.second); break; }
            }
        }
    }
    if (! step (0.30f)) return nullptr;

    //---------------------------------------------------------------- Neuro (2x oversampled)
    {
        Stereo n2; n2.resize ((size_t) total * 2);
        const size_t count = arr.bass.size();
        for (size_t i = 0; i < count; ++i)
        {
            const auto& n = arr.bass[i];
            renderBassNote (vc, n, posOf (n.step), n.len * stepSamples, n2);
            if (! step (0.30f + 0.40f * (float) (i + 1) / (float) std::max<size_t> (1, count))) return nullptr;
        }
        auto L = decimate2x (n2.l);
        auto R = decimate2x (n2.r);
        auto& dst = st[StemNeuro];
        for (int i = 0; i < total && i < (int) L.size(); ++i)
        {
            const float d = 1.0f - (1.0f - duck[(size_t) i]) * 0.35f;
            dst.l[(size_t) i] = softClip (L[(size_t) i] * 0.9f, 1.3f) * d;
            dst.r[(size_t) i] = softClip (R[(size_t) i] * 0.9f, 1.3f) * d;
        }
    }
    if (! step (0.72f)) return nullptr;

    //---------------------------------------------------------------- Sub
    {
        std::vector<float> sub ((size_t) total, 0.0f);
        renderSub (vc, arr.sub, stepSamples, pre, sub);
        for (int i = 0; i < total; ++i)
        {
            const float d = 1.0f - (1.0f - duck[(size_t) i]) * 0.85f;
            st[StemSub].l[(size_t) i] = sub[(size_t) i] * d;
            st[StemSub].r[(size_t) i] = sub[(size_t) i] * d;
        }
    }
    if (! step (0.78f)) return nullptr;

    //---------------------------------------------------------------- FX
    {
        const double rootHz = midiToHz (bassRootMidi (key.tonic) + 12, a4);
        const double outRootHz = midiToHz (bassRootMidi (outKey.tonic) + 12, a4);
        if (pre > 0) addRiser (vc, st[StemFx], 0.0, pre, rootHz, s.seed + 21, 0.85f);
        addImpact (vc, st[StemFx], pre, rootHz, s.seed + 22, 0.9f);
        addDownlifter (vc, st[StemFx], pre, std::min ((double) drop, stepSamples * 16.0), s.seed + 25, 0.35f);
        for (size_t i = 0; i < arr.zaps.size(); ++i)
            addZap (vc, st[StemFx], posOf (arr.zaps[i]), stepSamples * 2.0, s.seed + 40 + i, 0.55f);
        if (arr.transitionStep >= 0.0)
        {
            const double start = posOf (arr.transitionStep);
            addRiser (vc, st[StemFx], start, total - start, outRootHz, s.seed + 23, 0.7f);
            addReverseSwell (vc, st[StemFx], total, outRootHz, s.seed + 24, 0.8f);
        }
    }
    if (! step (0.84f)) return nullptr;

    //---------------------------------------------------------------- Stem-Pegel
    static const float defaultDb[NumStems] = { 0.0f, 0.5f, -7.0f, -2.5f, -4.0f, -7.0f };
    for (int i = 0; i < NumStems; ++i)
    {
        toBuffer (st[(size_t) i], r->stems[(size_t) i]);
        st[(size_t) i].l.clear(); st[(size_t) i].l.shrink_to_fit();
        st[(size_t) i].r.clear(); st[(size_t) i].r.shrink_to_fit();
        auto& b = r->stems[(size_t) i];
        r->active[(size_t) i] = s.stemOn[(size_t) i];
        if (! s.stemOn[(size_t) i]) { b.clear(); continue; }
        const float pk = bufferPeak (b);
        if (pk > 1.0e-6f) b.applyGain (0.9f / pk * (float) dbToGain (defaultDb[i] + s.stemGainDb[(size_t) i]));
    }

    auto buildMix = [&]
    {
        r->mix.setSize (2, total, false, true, false);
        r->mix.clear();
        for (int i = 0; i < NumStems; ++i)
            for (int ch = 0; ch < 2; ++ch)
                r->mix.addFrom (ch, 0, r->stems[(size_t) i], ch, 0, total);
    };
    buildMix();

    //---------------------------------------------------------------- Fit-in EQ (Spektrum wie der Psy-Track)
    if (ref.valid && s.match > 0.01f && drop > (int) sr / 4)
    {
        std::vector<float> mono ((size_t) drop);
        for (int i = 0; i < drop; ++i) mono[(size_t) i] = 0.5f * (r->mix.getSample (0, pre + i) + r->mix.getSample (1, pre + i));
        const auto gen = AnalysisAccumulator::analyse (mono.data(), drop, sr);
        if (gen.valid)
        {
            const std::vector<std::vector<int>> groups { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7, 8 } };
            double refG[4], genG[4], refM = 0, genM = 0;
            for (int g = 0; g < 4; ++g) { refG[g] = groupDb (ref.bandsDb, groups[(size_t) g]); genG[g] = groupDb (gen.bandsDb, groups[(size_t) g]); refM += refG[g] / 4; genM += genG[g] / 4; }
            float gains[4];
            for (int g = 0; g < 4; ++g)
                gains[g] = (float) juce::jlimit (-5.0, 5.0, ((refG[g] - refM) - (genG[g] - genM)) * s.match * 0.6);

            for (int i = 0; i < NumStems; ++i)
            {
                if (! r->active[(size_t) i]) continue;
                for (int ch = 0; ch < 2; ++ch)
                {
                    Biquad f0, f1, f2, f3;
                    f0.set (Biquad::Type::LowShelf, sr, 110.0, 0.7, gains[0]);
                    f1.set (Biquad::Type::Peak, sr, 350.0, 0.8, gains[1]);
                    f2.set (Biquad::Type::Peak, sr, 1600.0, 0.8, gains[2]);
                    f3.set (Biquad::Type::HighShelf, sr, 5500.0, 0.7, gains[3]);
                    float* d = r->stems[(size_t) i].getWritePointer (ch);
                    for (int k = 0; k < total; ++k) d[k] = f3.process (f2.process (f1.process (f0.process (d[k]))));
                }
            }
            buildMix();
        }
    }
    if (! step (0.90f)) return nullptr;

    //---------------------------------------------------------------- Lautheit + gemeinsamer Limiter
    {
        const double target = juce::jlimit (-16.0, -7.0, (s.loudnessMatch && ref.valid) ? (double) ref.rmsDb : -11.0);
        const double rms = regionRmsDb (r->mix, pre);
        const float G = (float) dbToGain (juce::jlimit (-24.0, 9.0, target - rms));
        const float ceiling = 0.891f; // -1 dBFS
        const int W = std::max (1, (int) (0.002 * sr));
        const double rel = 1.0 - std::exp (-1.0 / (0.08 * sr));

        std::vector<float> d ((size_t) total), g ((size_t) total);
        for (int i = 0; i < total; ++i)
        {
            const float pk = std::max (std::abs (r->mix.getSample (0, i)), std::abs (r->mix.getSample (1, i))) * G;
            d[(size_t) i] = pk > ceiling ? ceiling / pk : 1.0f;
        }
        // gleitendes Minimum nach vorne (Lookahead W)
        std::vector<float> m ((size_t) total);
        std::deque<int> q;
        for (int i = total - 1; i >= 0; --i)
        {
            while (! q.empty() && d[(size_t) q.back()] >= d[(size_t) i]) q.pop_back();
            q.push_back (i);
            while (q.front() > i + W) q.pop_front();
            m[(size_t) i] = d[(size_t) q.front()];
        }
        float rr = 1.0f;
        for (int i = 0; i < total; ++i)
        {
            rr = std::min (m[(size_t) i], rr + (1.0f - rr) * (float) rel);
            g[(size_t) i] = rr;
        }
        // gleitender Mittelwert (bleibt <= benoetigter Gain)
        double acc = 0;
        std::vector<float> a ((size_t) total);
        for (int i = 0; i < total; ++i)
        {
            acc += g[(size_t) i];
            if (i >= W) acc -= g[(size_t) (i - W)];
            a[(size_t) i] = (float) (acc / std::min (i + 1, W)) * G;
        }
        const int fade = std::min (total, (int) (0.003 * sr));
        for (int i = 0; i < NumStems; ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = r->stems[(size_t) i].getWritePointer (ch);
                for (int k = 0; k < total; ++k) x[k] *= a[(size_t) k];
                for (int k = 0; k < fade; ++k) x[total - 1 - k] *= (float) k / (float) fade;
            }
        buildMix();
        r->mixRmsDb = (float) regionRmsDb (r->mix, pre);
        r->mixPeakDb = (float) gainToDb (bufferPeak (r->mix));
    }

    static const char* styleNames[] = { "Neurofunk", "Techstep", "Jump-Up", "Roller" };
    static const char* grooveNames[] = { "Two-Step", "Halftime", "Rolling", "Broken" };
    r->summary = juce::String (styleNames[(int) s.style]) + " | " + grooveNames[(int) s.groove] + " | " + key.name()
               + " | A4 " + juce::String (a4, 1) + " Hz\n" + plan.text;
    if (pre > 0) r->summary << "\nVorlauf: " << s.preRollBars << " Host-Takt(e) Riser vor dem Drop";

    juce::String keyShort = juce::String (noteName (key.tonic)).replace ("#", "s") + (key.scale == ScaleType::Major ? "" : "m");
    r->baseName = "NeuroDrop_" + keyShort + "_" + juce::String (plan.bpm, 1).replace (".", "-") + "bpm_"
                + juce::String (r->bars) + "bars_s" + juce::String ((juce::int64) s.seed);

    step (1.0f);
    return r;
}
} // namespace nd
