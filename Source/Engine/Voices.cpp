#include "Voices.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace nd
{
namespace
{
void normalisePeak (std::vector<float>& v, float target = 0.95f)
{
    float pk = 0; for (auto x : v) pk = std::max (pk, std::abs (x));
    if (pk > 1.0e-9f) { const float g = target / pk; for (auto& x : v) x *= g; }
}

float shapeFn (int shape, double phase) // phase 0..1 -> 0..1
{
    phase -= std::floor (phase);
    switch (shape)
    {
        case 1:  return (float) (1.0 - phase);                 // Saw runter (Wuerg-Growl)
        case 2:  return (float) phase;                         // Saw hoch
        case 3:  return phase < 0.5 ? 1.0f : 0.15f;            // Square (Stotter)
        default: return (float) (0.5 - 0.5 * std::cos (kTwoPi * phase)); // weich
    }
}

void reverbInPlace (Stereo& s, float room, float wet, float dry, float damp, double sr)
{
    juce::Reverb rv;
    juce::Reverb::Parameters p;
    p.roomSize = room; p.wetLevel = wet; p.dryLevel = dry; p.damping = damp; p.width = 1.0f;
    rv.setParameters (p);
    rv.setSampleRate (sr);
    rv.processStereo (s.l.data(), s.r.data(), (int) s.size());
}
} // namespace

//============================================================================== Kick
std::vector<float> makeKick (const VoiceContext& c, double baseHz, float punch)
{
    const double sr = c.sr;
    const int n = (int) (0.42 * sr);
    std::vector<float> out ((size_t) n);
    Rng rng (c.seed * 3 + 1);
    Biquad clickHp; clickHp.set (Biquad::Type::HighPass, sr, 2200.0, 0.7);

    const double fStart = baseHz * (4.5 + 1.5 * punch);
    const double tauP = 0.018 + 0.012 * (1.0 - punch);
    const double tauA = 0.10 + 0.07 * (1.0 - c.aggression);
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double f = baseHz + (fStart - baseHz) * std::exp (-t / tauP);
        ph += kTwoPi * f / sr;
        const double att = std::min (1.0, t / 0.0006);
        double amp = att * std::exp (-t / tauA);
        if (t > 0.30) amp *= std::max (0.0, 1.0 - (t - 0.30) / 0.12);
        const float body = (float) (std::sin (ph) * amp);
        const float click = clickHp.process (rng.bip()) * (float) std::exp (-t / 0.0022)
                          + (float) (std::sin (kTwoPi * 1800.0 * t) * std::exp (-t / 0.004)) * 0.5f;
        float x = body + click * (0.25f + 0.35f * c.aggression);
        x = softClip (x, 1.4f + 2.2f * c.aggression);
        out[(size_t) i] = x;
    }
    normalisePeak (out);
    return out;
}

//============================================================================== Snare
std::vector<float> makeSnare (const VoiceContext& c, double bodyHz, bool ghost, uint64_t seed)
{
    const double sr = c.sr;
    const int n = (int) ((ghost ? 0.18 : 0.45) * sr);
    std::vector<float> out ((size_t) n);
    Rng rng (seed);
    Biquad hp, pk, crackBp, lpTop;
    hp.set (Biquad::Type::HighPass, sr, 1100.0, 0.7);
    pk.set (Biquad::Type::Peak, sr, 5200.0, 0.9, 6.0);
    crackBp.set (Biquad::Type::BandPass, sr, 3200.0, 1.2);
    lpTop.set (Biquad::Type::LowPass, sr, 9000.0 + 5000.0 * c.brightness, 0.7);

    const double tauN = ghost ? 0.045 : 0.12 + 0.05 * c.aggression;
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double f = bodyHz * (1.0 + 0.6 * std::exp (-t / 0.012));
        ph += kTwoPi * f / sr;
        const float body = (float) ((std::sin (ph) * 0.7 + std::sin (ph * 1.62) * 0.3) * std::exp (-t / (ghost ? 0.03 : 0.055)));
        const float nz = rng.bip();
        const float noise = lpTop.process (pk.process (hp.process (nz))) * (float) std::exp (-t / tauN);
        const float crack = crackBp.process (nz) * (float) std::exp (-t / 0.004) * 3.0f;
        float x = body * 0.55f + noise * 0.75f + crack * 0.4f;
        x = softClip (x * 1.3f, 1.8f + 3.0f * c.aggression);
        out[(size_t) i] = x;
    }
    normalisePeak (out);
    return out;
}

//============================================================================== Hats
Stereo makeHat (const VoiceContext& c, int variant, uint64_t seed)
{
    const double sr = c.sr;
    double len = 0.12, tau = 0.028;
    if (variant == HatOpen)   { len = 0.45; tau = 0.20; }
    if (variant == HatCrash)  { len = 2.4;  tau = 0.85; }
    if (variant == HatShaker) { len = 0.10; tau = 0.035; }
    const int n = (int) (len * sr);
    Stereo s; s.resize ((size_t) n);

    static const double metal[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    for (int ch = 0; ch < 2; ++ch)
    {
        Rng rng (seed * 7 + (uint64_t) ch * 1013 + (variant == HatCrash ? (uint64_t) ch : 0));
        SquareOsc osc[6];
        Biquad bp, hp, hp2;
        const double hpF = 6000.0 + 3000.0 * c.brightness;
        bp.set (Biquad::Type::BandPass, sr, variant == HatShaker ? 7000.0 : 9500.0, 0.9);
        hp.set (Biquad::Type::HighPass, sr, variant == HatCrash ? 4000.0 : hpF, 0.7);
        hp2.set (Biquad::Type::HighPass, sr, variant == HatCrash ? 4000.0 : hpF, 0.7);
        auto& dst = ch == 0 ? s.l : s.r;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            float m = 0;
            if (variant != HatShaker)
                for (int k = 0; k < 6; ++k) m += osc[k].next (metal[k] * 1.65, sr);
            const float nz = rng.bip();
            float x = bp.process (m * 0.12f) * 0.8f + nz * (variant == HatShaker ? 0.9f : 0.6f);
            x = hp2.process (hp.process (x));
            const double att = variant == HatShaker ? std::min (1.0, t / 0.004) : std::min (1.0, t / 0.0004);
            double env = att * std::exp (-t / tau);
            if (t > len * 0.85) env *= std::max (0.0, 1.0 - (t - len * 0.85) / (len * 0.15));
            dst[(size_t) i] = x * (float) env;
        }
        if (variant != HatCrash) break; // nur Crash ist echt stereo
    }
    if (variant != HatCrash) s.r = s.l;
    float pk = 0; for (size_t i = 0; i < s.size(); ++i) pk = std::max ({ pk, std::abs (s.l[i]), std::abs (s.r[i]) });
    if (pk > 0) { const float g = 0.9f / pk; for (size_t i = 0; i < s.size(); ++i) { s.l[i] *= g; s.r[i] *= g; } }
    return s;
}

//============================================================================== Neuro-Bass
void renderBassNote (const VoiceContext& c, const BassNote& n, double startSample, double lenSamples, Stereo& out)
{
    const double sr2 = c.sr * 2.0;
    const int64_t start = (int64_t) std::llround (startSample * 2.0);
    const int64_t gateLen = (int64_t) std::llround (lenSamples * 2.0);
    const double release = 0.014;
    const int64_t total = gateLen + (int64_t) (release * sr2 * 4);
    const int64_t outN = (int64_t) out.size();
    if (start >= outN) return;

    const double baseHz = midiToHz (n.midi, c.a4);
    const double beatHz = c.bpm / 60.0;
    const double lfoHz = n.rate * beatHz;
    const float ag = c.aggression;
    Rng rng (c.seed ^ (uint64_t) (n.step * 977.0) ^ (uint64_t) (n.var * 1.0e6));

    // Oszillatoren / Filter je Kanal
    SawOsc saws[5] = { SawOsc (0.0), SawOsc (0.31), SawOsc (0.57), SawOsc (0.83), SawOsc (0.12) };
    SquareOsc sq;
    Biquad lpL, lpR, hp1L, hp2L, hp1R, hp2R;
    Biquad f1L, f2L, f3L, f1R, f2R, f3R;
    Flanger flL, flR; flL.prepare (sr2); flR.prepare (sr2);
    hp1L.set (Biquad::Type::HighPass, sr2, 95.0, 0.7); hp2L = hp1L;
    hp1R = hp1L; hp2R = hp1L;

    const double detune = 0.10 + 0.20 * n.var;
    const double ratio = n.var < 0.33 ? 1.0 : (n.var < 0.66 ? 2.0 : 0.5);
    const double fbAmt = 0.15 + 0.45 * ag;
    const double iMax = 2.0 + 7.0 * ag, iMin = 0.3;
    double carPh = 0, modPh = 0, lastMod = 0;
    float envState = 0.0f;
    const double aCoef = 1.0 - std::exp (-1.0 / (0.002 * sr2));
    const double rCoef = 1.0 - std::exp (-1.0 / (release * sr2));
    const double formantShift = 0.9 + 0.2 * n.var;
    float artGain = 0.8f;

    // Vokal-Formanten: u -> a -> i
    static const double vow[3][3] = { { 325, 700, 2530 }, { 800, 1150, 2800 }, { 300, 2100, 2950 } };

    for (int64_t i = 0; i < total; ++i)
    {
        const int64_t idx = start + i;
        if (idx >= outN) break;
        if (idx < 0) continue;
        const double t = i / sr2;
        const double x01 = gateLen > 0 ? std::min (1.0, (double) i / (double) gateLen) : 1.0;
        const float gate = i < gateLen ? 1.0f : 0.0f;
        envState += (float) ((gate - envState) * (gate > envState ? aCoef : rCoef));
        if (i >= gateLen && envState < 1.0e-4f) break;

        double f = baseHz;
        const float sh = shapeFn (n.shape, lfoHz * t + n.var * 0.25);
        float yL = 0, yR = 0;

        switch (n.art)
        {
            case Art::Reese:
            {
                artGain = 0.75f;
                const double d = detune;
                const float s0 = saws[0].next (f * std::pow (2.0, -d / 12.0), sr2);
                const float s1 = saws[1].next (f * std::pow (2.0, -d / 36.0), sr2);
                const float s2 = saws[2].next (f * std::pow (2.0,  d / 36.0), sr2);
                const float s3 = saws[3].next (f * std::pow (2.0,  d / 12.0), sr2);
                const float cen = saws[4].next (f, sr2) * 0.6f;
                yL = s0 + s2 * 0.8f + cen;
                yR = s1 * 0.8f + s3 + cen;
                const double slow = 0.5 - 0.5 * std::cos (kTwoPi * lfoHz * 0.5 * t);
                const double cut = 220.0 + (600.0 + 2600.0 * c.brightness) * (0.25 + 0.45 * std::exp (-t / 0.09) + 0.45 * slow);
                if ((i & 15) == 0) { lpL.set (Biquad::Type::LowPass, sr2, cut, 1.1); lpR.set (Biquad::Type::LowPass, sr2, cut * 1.04, 1.1); }
                yL = lpL.process (yL * 0.4f); yR = lpR.process (yR * 0.4f);
                yL = flL.process (yL, sr2, 0.37, 0.4, 4.5, 0.65f, 0.5f);
                yR = flR.process (yR, sr2, 0.43, 0.4, 4.5, 0.65f, 0.5f);
                yL = softClip (yL * 1.6f, 1.5f + 3.0f * ag); yR = softClip (yR * 1.6f, 1.5f + 3.0f * ag);
                break;
            }
            case Art::Dive:
                f = baseHz * std::pow (2.0, (7.0 - 31.0 * std::pow (x01, 1.4)) / 12.0);
                [[fallthrough]];
            case Art::Growl:
            {
                artGain = n.art == Art::Dive ? 0.7f : 0.8f;
                const double idxFM = iMin + (iMax - iMin) * sh;
                modPh += f * ratio / sr2; if (modPh >= 1.0) modPh -= 1.0;
                const double mod = std::sin (kTwoPi * modPh + fbAmt * lastMod);
                lastMod = mod;
                carPh += f / sr2; if (carPh >= 1.0) carPh -= 1.0;
                const float car = (float) std::sin (kTwoPi * carPh + idxFM * mod);
                float x = car + saws[0].next (f, sr2) * 0.28f;
                x = wavefold (x * 0.75f, ag * 0.8f);
                if ((i & 15) == 0)
                {
                    const double v = sh * 2.0;
                    const int a = std::min (1, (int) v); const double fr = v - a;
                    double F[3];
                    for (int k = 0; k < 3; ++k) F[k] = (vow[a][k] + (vow[a + 1][k] - vow[a][k]) * fr) * formantShift;
                    f1L.set (Biquad::Type::BandPass, sr2, F[0] * 0.97, 6.0); f1R.set (Biquad::Type::BandPass, sr2, F[0] * 1.03, 6.0);
                    f2L.set (Biquad::Type::BandPass, sr2, F[1] * 0.97, 8.0); f2R.set (Biquad::Type::BandPass, sr2, F[1] * 1.03, 8.0);
                    f3L.set (Biquad::Type::BandPass, sr2, F[2] * 0.97, 10.0); f3R.set (Biquad::Type::BandPass, sr2, F[2] * 1.03, 10.0);
                }
                const float formL = f1L.process (x) + f2L.process (x) * 0.8f + f3L.process (x) * 0.5f;
                const float formR = f1R.process (x) + f2R.process (x) * 0.8f + f3R.process (x) * 0.5f;
                const float dry = x * 0.35f;
                yL = softClip (dry + formL * 2.6f, 2.0f + 4.0f * ag);
                yR = softClip (dry + formR * 2.6f, 2.0f + 4.0f * ag);
                break;
            }
            case Art::Stab:
            {
                artGain = 0.85f;
                const float s0 = saws[0].next (f * 1.003, sr2), s1 = saws[1].next (f * 0.997, sr2);
                modPh += f * 2.0 / sr2; if (modPh >= 1.0) modPh -= 1.0;
                carPh += f / sr2; if (carPh >= 1.0) carPh -= 1.0;
                const float fm = (float) std::sin (kTwoPi * carPh + (3.0 + 5.0 * ag) * std::exp (-t / 0.05) * std::sin (kTwoPi * modPh));
                const double cut = 400.0 + 6500.0 * std::exp (-t / 0.05);
                if ((i & 15) == 0) { lpL.set (Biquad::Type::LowPass, sr2, cut, 2.0); lpR.set (Biquad::Type::LowPass, sr2, cut * 1.05, 2.0); }
                const float amp = (float) std::exp (-t / 0.09);
                yL = softClip (lpL.process ((s0 + fm) * 0.5f) * 1.8f, 2.0f + 3.0f * ag) * amp;
                yR = softClip (lpR.process ((s1 + fm) * 0.5f) * 1.8f, 2.0f + 3.0f * ag) * amp;
                break;
            }
            case Art::Wobble:
            {
                artGain = 0.75f;
                const float a = saws[0].next (f, sr2) * 0.6f + sq.next (f * 0.5 * 2.0, sr2, 0.5) * 0.4f;
                const float b = saws[1].next (f * 1.006, sr2) * 0.6f;
                const double cut = 120.0 + 3800.0 * sh * sh;
                if ((i & 7) == 0) { lpL.set (Biquad::Type::LowPass, sr2, cut, 4.5); lpR.set (Biquad::Type::LowPass, sr2, cut * 1.03, 4.5); }
                yL = softClip (lpL.process ((a + b * 0.5f) * 0.5f) * 2.0f, 2.0f + 3.0f * ag);
                yR = softClip (lpR.process ((a * 0.8f + b) * 0.5f) * 2.0f, 2.0f + 3.0f * ag);
                break;
            }
        }

        yL = hp2L.process (hp1L.process (yL));
        yR = hp2R.process (hp1R.process (yR));
        out.l[(size_t) idx] += yL * envState * artGain;
        out.r[(size_t) idx] += yR * envState * artGain;
    }
    (void) rng;
}

//============================================================================== Sub
void renderSub (const VoiceContext& c, const std::vector<SubNote>& notes, double stepSamples, double offsetSamples, std::vector<float>& out)
{
    const double sr = c.sr;
    const int64_t N = (int64_t) out.size();
    std::vector<double> targetHz ((size_t) N, 0.0);
    std::vector<uint8_t> gate ((size_t) N, 0);
    for (const auto& n : notes)
    {
        const int64_t a = (int64_t) std::llround (offsetSamples + n.step * stepSamples);
        const int64_t b = std::min (N, (int64_t) std::llround (offsetSamples + (n.step + n.len) * stepSamples));
        const double hz = midiToHz (n.midi, c.a4);
        for (int64_t i = std::max<int64_t> (0, a); i < b; ++i) { targetHz[(size_t) i] = hz; gate[(size_t) i] = 1; }
    }
    double ph = 0, logF = std::log (midiToHz (bassRootMidi (c.key.tonic), c.a4));
    float env = 0;
    const double glide = 1.0 - std::exp (-1.0 / (0.025 * sr));
    const double aC = 1.0 - std::exp (-1.0 / (0.003 * sr));
    const double rC = 1.0 - std::exp (-1.0 / (0.022 * sr));
    Biquad lp1, lp2; lp1.set (Biquad::Type::LowPass, sr, 150.0, 0.7); lp2 = lp1;
    for (int64_t i = 0; i < N; ++i)
    {
        const bool on = gate[(size_t) i] != 0;
        if (on)
        {
            const double tl = std::log (targetHz[(size_t) i]);
            if (env < 0.02f) logF = tl; else logF += (tl - logF) * glide;
        }
        env += (float) (((on ? 1.0f : 0.0f) - env) * (on ? aC : rC));
        ph += std::exp (logF) / sr; if (ph >= 1.0) ph -= 1.0;
        float y = (float) std::sin (kTwoPi * ph);
        y = softClip (y * 1.25f, 1.4f);
        out[(size_t) i] = lp2.process (lp1.process (y)) * env;
    }
}

//============================================================================== FX
void addRiser (const VoiceContext& c, Stereo& out, double start, double len, double rootHz, uint64_t seed, float level)
{
    const double sr = c.sr;
    const int64_t s0 = (int64_t) start, n = (int64_t) len;
    Rng rL (seed), rR (seed + 99);
    Biquad bpL, bpR, lpS;
    SawOsc sawA (0.1), sawB (0.6);
    for (int64_t i = 0; i < n; ++i)
    {
        const int64_t idx = s0 + i;
        if (idx < 0 || idx >= (int64_t) out.size()) continue;
        const double x = (double) i / std::max<int64_t> (1, n);
        if ((i & 31) == 0)
        {
            const double fc = 300.0 * std::pow (8000.0 / 300.0, std::pow (x, 1.4));
            bpL.set (Biquad::Type::BandPass, sr, fc, 1.4);
            bpR.set (Biquad::Type::BandPass, sr, fc * 1.06, 1.4);
            lpS.set (Biquad::Type::LowPass, sr, 800.0 + 7000.0 * x, 0.9);
        }
        const double pf = rootHz * 2.0 * std::pow (2.0, 2.0 * x * x);
        const float tone = lpS.process ((sawA.next (pf, sr) + sawB.next (pf * 1.007, sr)) * 0.5f);
        const float amp = (float) std::pow (x, 2.0) * level;
        out.l[(size_t) idx] += (bpL.process (rL.bip()) * 1.6f + tone * 0.35f) * amp;
        out.r[(size_t) idx] += (bpR.process (rR.bip()) * 1.6f + tone * 0.35f) * amp;
    }
}

void addImpact (const VoiceContext& c, Stereo& out, double start, double rootHz, uint64_t seed, float level)
{
    const double sr = c.sr;
    const int n = (int) (3.0 * sr);
    Stereo tmp; tmp.resize ((size_t) n);
    Rng rng (seed);
    Biquad lp, hp; lp.set (Biquad::Type::LowPass, sr, 2500.0, 0.7); hp.set (Biquad::Type::HighPass, sr, 180.0, 0.7);
    // Rausch-Schlag -> Hall (ohne Tiefbass, damit Kick/Sub sauber bleiben)
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const float nz = hp.process (lp.process (rng.bip())) * (float) std::exp (-t / 0.22);
        tmp.l[(size_t) i] = nz; tmp.r[(size_t) i] = nz;
    }
    reverbInPlace (tmp, 0.9f, 0.5f, 0.8f, 0.35f, sr);
    // Boom (trocken, auf Grundton)
    double boom = rootHz; while (boom > 55.0) boom *= 0.5; while (boom < 30.0) boom *= 2.0;
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double f = boom * (1.0 + 1.5 * std::exp (-t / 0.12));
        ph += kTwoPi * f / sr;
        const float sub = (float) (std::sin (ph) * std::exp (-t / 0.45) * std::min (1.0, t / 0.001));
        const double fade = t > 2.0 ? std::max (0.0, 1.0 - (t - 2.0)) : 1.0;   // weich ausblenden
        tmp.l[(size_t) i] = (tmp.l[(size_t) i] + sub * 0.9f) * (float) fade;
        tmp.r[(size_t) i] = (tmp.r[(size_t) i] + sub * 0.9f) * (float) fade;
    }
    for (int i = 0; i < n; ++i)
    {
        const int64_t idx = (int64_t) start + i;
        if (idx < 0 || idx >= (int64_t) out.size()) continue;
        out.l[(size_t) idx] += tmp.l[(size_t) i] * level;
        out.r[(size_t) idx] += tmp.r[(size_t) i] * level;
    }
}

void addZap (const VoiceContext& c, Stereo& out, double start, double len, uint64_t seed, float level)
{
    const double sr = c.sr;
    Rng rng (seed);
    const float pan = rng.bip() * 0.7f;
    const int n = (int) len;
    double ph = 0, mph = 0;
    for (int i = 0; i < n; ++i)
    {
        const int64_t idx = (int64_t) start + i;
        if (idx < 0 || idx >= (int64_t) out.size()) continue;
        const double t = i / sr;
        const double f = 150.0 + 3200.0 * std::exp (-t * 14.0);
        ph += f / sr; mph += f * 1.5 / sr;
        const float env = (float) (std::min (1.0, t / 0.002) * (1.0 - (double) i / n));
        const float y = (float) std::sin (kTwoPi * ph + 2.5 * std::sin (kTwoPi * mph)) * env * level;
        out.l[(size_t) idx] += y * (1.0f - pan) * 0.7f;
        out.r[(size_t) idx] += y * (1.0f + pan) * 0.7f;
    }
}

void addReverseSwell (const VoiceContext& c, Stereo& out, double endSample, double rootHz, uint64_t seed, float level)
{
    const double sr = c.sr;
    const int n = (int) (1.6 * sr);
    Stereo tmp; tmp.resize ((size_t) n);
    Rng rng (seed);
    Biquad hp; hp.set (Biquad::Type::HighPass, sr, 400.0, 0.7);
    double ph = 0;
    double f = rootHz; while (f < 200.0) f *= 2.0;
    for (int i = 0; i < (int) (0.25 * sr); ++i)
    {
        const double t = i / sr;
        ph += f / sr;
        const float v = (hp.process (rng.bip()) * 0.7f + (float) std::sin (kTwoPi * ph) * 0.5f) * (float) std::exp (-t / 0.06);
        tmp.l[(size_t) i] = v; tmp.r[(size_t) i] = v;
    }
    reverbInPlace (tmp, 0.95f, 0.9f, 0.15f, 0.3f, sr);
    std::reverse (tmp.l.begin(), tmp.l.end());
    std::reverse (tmp.r.begin(), tmp.r.end());
    float pk = 0; for (int i = 0; i < n; ++i) pk = std::max ({ pk, std::abs (tmp.l[(size_t) i]), std::abs (tmp.r[(size_t) i]) });
    const float g = pk > 0 ? level / pk : 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const int64_t idx = (int64_t) endSample - n + i;
        if (idx < 0 || idx >= (int64_t) out.size()) continue;
        out.l[(size_t) idx] += tmp.l[(size_t) i] * g;
        out.r[(size_t) idx] += tmp.r[(size_t) i] * g;
    }
}

void addDownlifter (const VoiceContext& c, Stereo& out, double start, double len, uint64_t seed, float level)
{
    const double sr = c.sr;
    Rng rL (seed), rR (seed + 5);
    Biquad bpL, bpR;
    const int64_t n = (int64_t) len;
    for (int64_t i = 0; i < n; ++i)
    {
        const int64_t idx = (int64_t) start + i;
        if (idx < 0 || idx >= (int64_t) out.size()) continue;
        const double x = (double) i / std::max<int64_t> (1, n);
        if ((i & 31) == 0)
        {
            const double fc = 7000.0 * std::pow (200.0 / 7000.0, x);
            bpL.set (Biquad::Type::BandPass, sr, fc, 1.2);
            bpR.set (Biquad::Type::BandPass, sr, fc * 0.95, 1.2);
        }
        const float amp = (float) ((1.0 - x) * (1.0 - x)) * level * (float) std::min (1.0, x * 50.0);
        out.l[(size_t) idx] += bpL.process (rL.bip()) * 1.5f * amp;
        out.r[(size_t) idx] += bpR.process (rR.bip()) * 1.5f * amp;
    }
}
} // namespace nd
