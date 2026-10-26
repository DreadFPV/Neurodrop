#pragma once
// Kleine, header-only DSP-Bausteine fuer das Offline-Rendering.
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>

namespace nd
{
constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }
inline double dbToGain (double db)                { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g)                 { return g > 1.0e-9 ? 20.0 * std::log10 (g) : -180.0; }
inline float lerp (float a, float b, float t)     { return a + (b - a) * t; }

//---------------------------------------------------------------------------- Zufall
// Deterministischer, schneller Zufall (xorshift) – gleicher Seed = gleicher Drop.
struct Rng
{
    uint64_t s;
    explicit Rng (uint64_t seed = 1) : s (seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) { if (s == 0) s = 1; next(); next(); }
    uint64_t next()        { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double   uni()         { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); } // 0..1
    float    bip()         { return (float) (uni() * 2.0 - 1.0); }                            // -1..1
    bool     chance (double p) { return uni() < p; }
    int      range (int lo, int hiInclusive) { return lo + (int) (uni() * (hiInclusive - lo + 1)) % (hiInclusive - lo + 1); }
    template <typename T> const T& pick (const std::vector<T>& v) { return v[(size_t) range (0, (int) v.size() - 1)]; }
    // Gewichtete Auswahl
    int weighted (const std::vector<double>& w)
    {
        double sum = 0; for (auto x : w) sum += x;
        double r = uni() * sum;
        for (size_t i = 0; i < w.size(); ++i) { if (r < w[i]) return (int) i; r -= w[i]; }
        return (int) w.size() - 1;
    }
};

//---------------------------------------------------------------------------- Biquad (RBJ)
struct Biquad
{
    enum class Type { LowPass, HighPass, BandPass, Peak, LowShelf, HighShelf };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    void set (Type t, double sr, double freq, double q, double gainDb = 0.0)
    {
        freq = std::clamp (freq, 5.0, sr * 0.49);
        q    = std::max (q, 0.05);
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w0 = kTwoPi * freq / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (t)
        {
            case Type::LowPass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::HighPass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::BandPass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::Peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case Type::LowShelf:
            {
                const double sa = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + sa); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sa);
                A0 = (A + 1) + (A - 1) * cw + sa;       A1 = -2 * ((A - 1) + (A + 1) * cw);    A2 = (A + 1) + (A - 1) * cw - sa;
                break;
            }
            case Type::HighShelf:
            {
                const double sa = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + sa); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sa);
                A0 = (A + 1) - (A - 1) * cw + sa;       A1 = 2 * ((A - 1) - (A + 1) * cw);      A2 = (A + 1) - (A - 1) * cw - sa;
                break;
            }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    inline float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

//---------------------------------------------------------------------------- Oszillatoren
inline double polyBlep (double t, double dt)
{
    if (t < dt)       { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

struct SawOsc
{
    double phase = 0.0;
    explicit SawOsc (double startPhase = 0.0) : phase (startPhase) {}
    inline float next (double freq, double sr)
    {
        const double dt = std::min (0.5, freq / sr);
        double v = 2.0 * phase - 1.0 - polyBlep (phase, dt);
        phase += dt; if (phase >= 1.0) phase -= 1.0;
        return (float) v;
    }
};

struct SquareOsc
{
    double phase = 0.0;
    inline float next (double freq, double sr, double pw = 0.5)
    {
        const double dt = std::min (0.5, freq / sr);
        double v = phase < pw ? 1.0 : -1.0;
        v += polyBlep (phase, dt);
        double t2 = phase - pw; if (t2 < 0) t2 += 1.0;
        v -= polyBlep (t2, dt);
        phase += dt; if (phase >= 1.0) phase -= 1.0;
        return (float) v;
    }
};

//---------------------------------------------------------------------------- Saettigung
inline float softClip (float x, float drive)
{
    const float d = std::max (0.01f, drive);
    return std::tanh (x * d) / std::tanh (d);
}
inline float wavefold (float x, float amount) { return std::sin (x * (1.0f + amount * 3.0f) * 1.5707963f); }

//---------------------------------------------------------------------------- Flanger / Comb
struct Flanger
{
    std::vector<float> buf;
    int w = 0;
    double phase = 0.0;
    float fb = 0.0f;
    void prepare (double sr) { buf.assign ((size_t) (sr * 0.02) + 4, 0.0f); w = 0; }
    inline float process (float x, double sr, double rateHz, double minMs, double maxMs, float feedback, float mix)
    {
        const double lfo = 0.5 - 0.5 * std::cos (kTwoPi * phase);
        phase += rateHz / sr; if (phase >= 1.0) phase -= 1.0;
        const double d = (minMs + (maxMs - minMs) * lfo) * 0.001 * sr;
        const int n = (int) buf.size();
        double rp = (double) w - d; while (rp < 0) rp += n;
        const int i0 = (int) rp; const int i1 = (i0 + 1) % n; const double fr = rp - i0;
        const float delayed = (float) (buf[(size_t) i0] * (1.0 - fr) + buf[(size_t) i1] * fr);
        buf[(size_t) w] = x + delayed * feedback;
        w = (w + 1) % n;
        return x * (1.0f - mix) + delayed * mix;
    }
};

//---------------------------------------------------------------------------- Halfband-Dezimierung (2x -> 1x)
// Windowed-Sinc-Tiefpass + Downsampling fuer oversampled gerenderte Signale.
inline std::vector<float> decimate2x (const std::vector<float>& in)
{
    constexpr int taps = 63;
    static const std::vector<float> h = []
    {
        std::vector<float> c ((size_t) taps);
        const int M = taps - 1; double sum = 0;
        for (int i = 0; i < taps; ++i)
        {
            const double n = i - M / 2.0;
            const double fc = 0.225; // relativ zur 2x-Rate (=0.45 Nyquist der Zielrate)
            const double sinc = std::abs (n) < 1.0e-9 ? 2 * fc : std::sin (kTwoPi * fc * n) / (kPi * n);
            const double win = 0.42 - 0.5 * std::cos (kTwoPi * i / M) + 0.08 * std::cos (2 * kTwoPi * i / M); // Blackman
            c[(size_t) i] = (float) (sinc * win); sum += c[(size_t) i];
        }
        for (auto& v : c) v = (float) (v / sum);
        return c;
    }();
    std::vector<float> out (in.size() / 2, 0.0f);
    const int N = (int) in.size();
    for (size_t o = 0; o < out.size(); ++o)
    {
        const int c = (int) o * 2;
        float acc = 0.0f;
        for (int k = 0; k < taps; ++k)
        {
            const int idx = c + k - taps / 2;
            if (idx >= 0 && idx < N) acc += in[(size_t) idx] * h[(size_t) k];
        }
        out[o] = acc;
    }
    return out;
}
} // namespace nd
