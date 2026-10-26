#include "Patterns.h"
#include "DSP.h"
#include <map>

namespace nd
{
namespace
{
struct Hit { double step; float vel; };

struct DrumBar
{
    std::vector<Hit> kick, snare;
    std::vector<DrumHit> hats;
};

std::vector<int> mainSnareSteps (Groove g)
{
    return g == Groove::Halftime ? std::vector<int> { 8 } : std::vector<int> { 4, 12 };
}

// Grundgerüst Kick/Snare je Groove + Variante
DrumBar baseDrums (Groove g, int variant)
{
    DrumBar b;
    switch (g)
    {
        case Groove::TwoStep:
        case Groove::Rolling:
            b.snare = { { 4, 1.0f }, { 12, 1.0f } };
            if (variant == 0)      b.kick = { { 0, 1.0f }, { 10, 0.95f } };
            else if (variant == 1) b.kick = { { 0, 1.0f }, { 7, 0.75f }, { 10, 0.95f } };
            else                   b.kick = { { 0, 1.0f }, { 10, 0.9f }, { 11, 0.7f } };
            break;
        case Groove::Halftime:
            b.snare = { { 8, 1.0f } };
            if (variant == 0)      b.kick = { { 0, 1.0f }, { 10, 0.85f } };
            else if (variant == 1) b.kick = { { 0, 1.0f }, { 3, 0.7f }, { 10, 0.85f } };
            else                   b.kick = { { 0, 1.0f }, { 11, 0.85f }, { 14, 0.65f } };
            break;
        case Groove::Broken:
            if (variant == 0)      { b.kick = { { 0, 1.0f }, { 3, 0.8f }, { 10, 0.9f } };            b.snare = { { 4, 1.0f }, { 12, 1.0f } }; }
            else if (variant == 1) { b.kick = { { 0, 1.0f }, { 7, 0.8f }, { 10, 0.9f }, { 14, 0.65f } }; b.snare = { { 4, 1.0f }, { 12, 1.0f } }; }
            else                   { b.kick = { { 0, 1.0f }, { 10, 0.9f }, { 11, 0.75f } };           b.snare = { { 4, 1.0f }, { 9, 0.55f }, { 13, 1.0f } }; }
            break;
    }
    return b;
}

void addGhostsAndHats (DrumBar& b, const PatternParams& p, Rng& rng)
{
    const auto mains = mainSnareSteps (p.groove);
    auto isMain = [&] (int s) { for (auto m : mains) if (m == s) return true; return false; };

    // Ghost-Snares
    const double ghostProb = (0.12 + 0.45 * p.intensity) * (p.groove == Groove::Rolling ? 1.4 : 1.0);
    for (int s : { 2, 7, 9, 14, 15 })
        if (! isMain (s) && rng.chance (ghostProb * 0.6))
            b.snare.push_back ({ (double) s, 0.18f + 0.15f * (float) rng.uni() });

    // Hats
    const bool all16 = p.groove == Groove::Rolling || p.style == Style::Roller;
    const int openStep = rng.chance (0.5) ? 14 : 6;
    const bool useOpen = rng.chance (0.45);
    for (int s = 0; s < 16; ++s)
    {
        const bool eighth = (s % 2) == 0;
        const bool off    = (s % 4) == 2;
        float vel = 0.0f;
        if (eighth) vel = off ? 0.85f : 0.45f;
        else if (all16) vel = 0.30f + 0.15f * (float) rng.uni();
        else if (rng.chance (p.intensity * 0.55)) vel = 0.25f + 0.15f * (float) rng.uni();
        if (vel <= 0) continue;
        int variant = HatClosed;
        if (useOpen && s == openStep) { variant = HatOpen; vel = 0.7f; }
        b.hats.push_back ({ (double) s, vel * (0.92f + 0.08f * (float) rng.uni()), variant });
    }
    if (p.style == Style::Roller || (p.style == Style::Techstep && p.intensity > 0.6f))
        for (int s = 0; s < 16; ++s)
            b.hats.push_back ({ (double) s, 0.22f + 0.08f * (float) ((s % 2) == 1), HatShaker });
}

void addFill (DrumBar& b, Rng& rng, Groove g)
{
    const int type = rng.range (0, 3);
    auto rm = [&] (std::vector<Hit>& v, double from) { v.erase (std::remove_if (v.begin(), v.end(), [&] (const Hit& h) { return h.step >= from && h.vel < 0.9f; }), v.end()); };
    rm (b.snare, 13);
    switch (type)
    {
        case 0: b.snare.push_back ({ 14, 0.55f }); b.snare.push_back ({ 15, 0.8f }); break;
        case 1: b.snare.push_back ({ 11, 0.45f }); b.snare.push_back ({ 13, 0.6f }); b.snare.push_back ({ 14, 0.7f }); b.snare.push_back ({ 15, 0.85f }); break;
        case 2: b.kick.push_back ({ 14, 0.8f }); b.snare.push_back ({ 15, 0.85f }); break;
        default:
            for (double s = 13.0; s < 16.0; s += 0.5) b.snare.push_back ({ s, 0.35f + 0.2f * (float) (s - 13.0) });
            break;
    }
    if (g == Groove::Halftime) b.snare.push_back ({ 12, 0.5f });
}

DrumBar transitionBar (const PatternParams& p)
{
    DrumBar b;
    b.kick = { { 0, 1.0f } };
    const bool half = p.groove == Groove::Halftime;
    b.snare = half ? std::vector<Hit> { { 8, 1.0f } } : std::vector<Hit> { { 4, 1.0f } };
    const double rollStart = half ? 10.0 : 8.0;
    for (double s = rollStart; s < 12.0; s += 1.0) b.snare.push_back ({ s, 0.35f + 0.05f * (float) (s - rollStart) });
    for (double s = 12.0; s < 16.0; s += 0.5)     b.snare.push_back ({ s, 0.55f + 0.11f * (float) (s - 12.0) });
    for (int s = 0; s < 8; ++s) b.hats.push_back ({ (double) s, (s % 4) == 2 ? 0.85f : 0.45f, HatClosed });
    return b;
}

//------------------------------------------------------------------------------ Bass-Motive
struct Motif { std::vector<BassNote> notes; };

int fitToScale (const Key& k, int off)
{
    if (k.contains (off)) return off;
    if (k.contains (off - 1)) return off - 1;
    if (k.contains (off + 1)) return off + 1;
    return 0;
}

int pickOffset (const PatternParams& p, Rng& rng)
{
    std::vector<int> offs; std::vector<double> w;
    const bool spicy = p.key.scale == ScaleType::Phrygian || p.aggression > 0.6f;
    switch (p.style)
    {
        case Style::Neurofunk: offs = { 0, 12, -2, 7, 3, 1, 8, 5 }; w = { .50, .10, .08, .10, .07, spicy ? .06 : 0.0, .05, .04 }; break;
        case Style::Techstep:  offs = { 0, 3, 5, 7, -2, 1 };        w = { .60, .10, .08, .10, .10, spicy ? .05 : 0.0 }; break;
        case Style::JumpUp:    offs = { 0, 12, 7, 10, 3 };          w = { .50, .20, .15, .10, .05 }; break;
        case Style::Roller:    offs = { 0, 7, 3, 10, -2 };          w = { .60, .15, .10, .10, .05 }; break;
    }
    return fitToScale (p.key, offs[(size_t) rng.weighted (w)]);
}

Art pickArt (const PatternParams& p, Rng& rng)
{
    std::vector<double> w; // Reese, Growl, Stab, Dive, Wobble
    switch (p.style)
    {
        case Style::Neurofunk: w = { .18, .45 + .1 * p.aggression, .22, .0, .10 }; break;
        case Style::Techstep:  w = { .60, .25, .15, .0, .0 }; break;
        case Style::JumpUp:    w = { .05, .20, .25, .0, .50 }; break;
        case Style::Roller:    w = { .70, .25, .05, .0, .0 }; break;
    }
    return (Art) rng.weighted (w);
}

float pickRate (const PatternParams& p, Art a, Rng& rng)
{
    if (a == Art::Reese) return rng.chance (0.5) ? 0.5f : 1.0f;
    switch (p.style)
    {
        case Style::Neurofunk: { const float r[] = { 2, 4, 3, 8 };  return r[rng.weighted ({ .3, .4, .15, .15 })]; }
        case Style::JumpUp:    { const float r[] = { 2, 4, 1 };     return r[rng.weighted ({ .5, .3, .2 })]; }
        case Style::Techstep:  { const float r[] = { 1, 2, 0.5f };  return r[rng.weighted ({ .4, .4, .2 })]; }
        case Style::Roller:    { const float r[] = { 1, 2 };        return r[rng.weighted ({ .5, .5 })]; }
    }
    return 2.0f;
}

Motif makeMotif (const PatternParams& p, Rng& rng, int root)
{
    Motif m;
    bool gate[16] {};
    const auto mains = mainSnareSteps (p.groove);
    auto isSnare = [&] (int s) { for (auto x : mains) if (x == s) return true; return false; };

    if (p.style == Style::JumpUp)
    {
        for (int s = 0; s < 16; s += 2) gate[s] = rng.chance (0.35 + 0.35 * p.intensity);
        gate[0] = true;
        for (int s = 0; s < 16; s += 2) if (gate[s] && rng.chance (0.5) && s + 1 < 16) gate[s + 1] = true;
    }
    else
    {
        double density = 0.45 + 0.3 * p.intensity;
        if (p.style == Style::Techstep || p.style == Style::Roller) density = 0.75 + 0.2 * p.intensity;
        for (int s = 0; s < 16; ++s)
        {
            double pr = density;
            if (isSnare (s)) pr *= (p.style == Style::Neurofunk ? 0.3 : 0.6);
            gate[s] = rng.chance (pr);
        }
        gate[0] = rng.chance (0.85) || p.style != Style::Neurofunk;
    }

    // Laeufe zu Noten zusammenfassen
    const int maxRun = (p.style == Style::Techstep || p.style == Style::Roller) ? 8 : 4;
    int s = 0;
    bool first = true;
    while (s < 16)
    {
        if (! gate[s]) { ++s; continue; }
        int e = s;
        while (e + 1 < 16 && gate[e + 1] && (e + 1 - s) < maxRun && ! (p.style == Style::Neurofunk && rng.chance (0.25))) ++e;
        BassNote n;
        n.step = s;
        n.len  = (double) (e - s + 1) - (rng.chance (0.4) ? 0.25 : 0.0);
        n.art  = pickArt (p, rng);
        n.midi = root + (first ? 0 : pickOffset (p, rng));
        n.rate = pickRate (p, n.art, rng);
        n.shape = rng.range (0, 3);
        n.var  = (float) rng.uni();
        if (n.art == Art::Stab) n.len = std::min (n.len, 1.0);
        m.notes.push_back (n);
        first = false;
        s = e + 1;
    }
    return m;
}

Motif variation (const Motif& a, const PatternParams& p, Rng& rng)
{
    Motif v = a;
    if (v.notes.empty()) return v;
    auto& n = v.notes.back();
    n.art  = pickArt (p, rng);
    n.rate = pickRate (p, n.art, rng);
    if (rng.chance (0.5)) n.midi = a.notes.front().midi + pickOffset (p, rng);
    n.var = (float) rng.uni();
    return v;
}
} // namespace

//==============================================================================
Arrangement buildArrangement (const PatternParams& p)
{
    Arrangement arr;
    arr.bars = std::max (1, p.bars);
    Rng rng (p.seed);

    const int root = bassRootMidi (p.key.tonic);
    arr.rootMidi = root;

    // Drum-Motive (wiederholen sich – wichtig fuer Groove)
    const int varA = rng.range (0, 2);
    int varB = rng.range (0, 2); if (varB == varA) varB = (varA + 1) % 3;
    DrumBar drumA = baseDrums (p.groove, varA);
    DrumBar drumB = baseDrums (p.groove, varB);
    { Rng r2 (p.seed * 31 + 7); addGhostsAndHats (drumA, p, r2); }
    { Rng r2 (p.seed * 53 + 11); addGhostsAndHats (drumB, p, r2); }

    // Bass-Motive
    Motif A  = makeMotif (p, rng, root);
    Motif A2 = variation (A, p, rng);
    Motif B  = makeMotif (p, rng, root);
    Motif A3 = variation (A, p, rng);

    for (int bar = 0; bar < arr.bars; ++bar)
    {
        const double base = bar * 16.0;
        const int pos = bar % 4;
        const bool final = bar == arr.bars - 1;
        const bool phraseEnd = pos == 3 || final;
        const bool transition = final && p.transitionOut;

        DrumBar d;
        if (transition)          d = transitionBar (p);
        else
        {
            d = (pos == 3) ? drumB : drumA;
            if (phraseEnd) { Rng rf (p.seed * 97 + (uint64_t) bar); addFill (d, rf, p.groove); }
        }
        for (auto& h : d.kick)  arr.kick.push_back  ({ base + h.step, h.vel, 0 });
        for (auto& h : d.snare) arr.snare.push_back ({ base + h.step, h.vel, h.vel < 0.5f ? 1 : 0 });
        for (auto& h : d.hats)  arr.hats.push_back  ({ base + h.step, h.vel, h.variant });
        if (bar == 0 || (bar % 8 == 0 && ! transition))
            arr.hats.push_back ({ base, 0.9f, HatCrash });

        // Bass
        const Motif& m = (pos == 0) ? A : (pos == 1 ? A2 : (pos == 2 ? ((bar / 4) % 2 ? A3 : A) : B));
        const double cut = transition ? 8.0 : 16.0;
        for (auto n : m.notes)
        {
            if (n.step >= cut) continue;
            n.len = std::min (n.len, cut - n.step);
            if (phraseEnd && ! transition && n.step + n.len > 13.0 && n.step >= 8.0 && rng.chance (0.5))
            {
                n.art = Art::Dive; n.len = std::max (1.5, 16.0 - n.step - 0.5);
            }
            n.step += base;
            arr.bass.push_back (n);
        }
        if (transition)
        {
            BassNote dive; dive.step = base + 8.0; dive.len = 6.0; dive.midi = root + 12;
            dive.art = Art::Dive; dive.rate = 4.0f; dive.var = (float) rng.uni();
            arr.bass.push_back (dive);
            arr.transitionStep = base + (arr.bars >= 8 ? -16.0 : 0.0);
            if (arr.transitionStep < 0) arr.transitionStep = base;
        }

        // FX-Zaps am Phrasenende
        if (phraseEnd && ! final && p.intensity > 0.35f && rng.chance (0.65))
            arr.zaps.push_back (base + (rng.chance (0.5) ? 14.0 : 15.0));
    }

    // Sub-Linie aus der Bass-Linie ableiten
    const bool legato = p.style == Style::Techstep || p.style == Style::Roller;
    for (size_t i = 0; i < arr.bass.size(); ++i)
    {
        const auto& n = arr.bass[i];
        if (n.art == Art::Dive) continue;
        int off = n.midi - root;
        if (off >= 12) off -= 12;
        if (off > 7 || off < -2) off = 0;
        SubNote s { n.step, std::max (1.0, n.len), root + off };
        if (legato && i + 1 < arr.bass.size())
        {
            const double gap = arr.bass[i + 1].step - (n.step + n.len);
            if (gap > 0 && gap <= 2.0 && arr.bass[i + 1].art != Art::Dive) s.len = arr.bass[i + 1].step - n.step;
        }
        arr.sub.push_back (s);
    }
    return arr;
}
} // namespace nd
