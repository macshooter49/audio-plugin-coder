// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tapemodes_cert.cpp — tp60 · FIVE TAPE MODES, AND NO TWO OF THEM SOUND ALIKE.
//
//  Max: "where are the rest of my TAPE MODES??? we literally had 5 total and now it's three,
//  where are the other two? and why is REEL the same visualizer as the STUDIO?"
//
//  He counted right. Before tp43a the Patcher's Tape shelf listed FIVE — the three GLOBAL
//  machines (Reel · Porta · Wire) plus the routed card's two types (Studio · Cassette). tp43a did
//  what he had asked for ("these tape modes need to stop being global … actually follow the
//  patcher's cables") by deleting the three global entries and moving Reel onto the card. Two
//  casualties: PORTA and WIRE existed only as global machine names and went with them.
//
//  🚨 THEY CANNOT COME BACK AS ALIASES. Porta WAS the CassetteMachine under another name and Wire
//  WAS the WireMachine under another name; re-listing them would put two pairs of identical
//  entries in the browser — which is the other half of the same complaint. So each gets its own
//  TRANSPORT, which is where fb368 established the machines actually separate.
//
//  THE BAR IS THE PAIRWISE SPECTRAL DISTANCE, all ten pairs, on the SHIPPED engine. fb368's own
//  measurement is the yardstick: the raw machines differ by 12.18 dB and only 7.66 survived the
//  transport before it was voiced per machine, which it called "a ripple, not a character".
//  So: every pair must clear kMinSep, and the two NEW modes must be at least that far from the
//  mode they share a machine class with — Porta vs Cassette and Wire vs Studio are the pairs a
//  lazy implementation would fail.
//
//  ⚠️ AND TYPES 0..2 MUST NOT MOVE. The roster was born with eight slots (fb365) so Porta and
//  Wire fill 3 and 4 without renumbering; bar [2] proves the first three are bit-identical to
//  voiceFor(machineFor(t)), which is what they resolved to before this change.
//
//    ./Tests/tapemodes_gate.sh
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_audio_basics/juce_audio_basics.h>
#include "TapeFxEngine.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <string>
#include <cstdlib>
namespace juce { extern const char* const juce_compilationDate = __DATE__; extern const char* const juce_compilationTime = __TIME__; }

static const double SR = 48000.0;
static const int    N  = 32768;          // ~0.68 s of analysis
static const int    WARM = 24000;        // half a second for the transport to settle
static const int    NB = 40;             // log-spaced analysis bands, 30 Hz .. 18 kHz
static const double kMinSep = 3.0;       // dB of spectral distance — below this it is a ripple

static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& d = "")
{ if (ok) { ++npass; printf ("  PASS  %s\n        %s\n", what, d.c_str()); }
  else    { ++nfail; printf ("  FAIL  %s\n        %s\n", what, d.c_str()); } }

static const char* NAME[5] = { "Studio", "Cassette", "Reel", "Porta", "Wire" };

// deterministic broadband excitation — the same bits into every machine
static float noiseAt (int i) noexcept
{
    unsigned s = (unsigned) i * 1664525u + 1013904223u;
    s ^= s >> 15; s *= 2246822519u; s ^= s >> 13;
    return ((float) (s & 0xFFFFFF) / 8388608.0f - 1.0f) * 0.30f;
}

// log-magnitude spectrum in NB bands, by direct DFT at each band centre (no FFT dependency)
static void spectrum (const std::vector<float>& x, double out[NB])
{
    for (int b = 0; b < NB; ++b)
    {
        const double f = 30.0 * std::pow (18000.0 / 30.0, (double) b / (NB - 1));
        const double w = 2.0 * juce::MathConstants<double>::pi * f / SR;
        double re = 0.0, im = 0.0;
        for (int n = 0; n < (int) x.size(); ++n)
        {
            // Hann window so a band reads its own energy, not the next one's leakage
            const double win = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / (double) (x.size() - 1));
            const double v = (double) x[(size_t) n] * win;
            re += v * std::cos (w * n); im -= v * std::sin (w * n);
        }
        const double mag = std::sqrt (re * re + im * im) / (double) x.size();
        out[b] = 20.0 * std::log10 (mag + 1.0e-9);
    }
}

// what fb368 calls spectral distance: RMS difference of the two log spectra, level-matched first
static double distance (const double a[NB], const double b[NB])
{
    double ma = 0.0, mb = 0.0;
    for (int i = 0; i < NB; ++i) { ma += a[i]; mb += b[i]; }
    ma /= NB; mb /= NB;
    double s = 0.0;
    for (int i = 0; i < NB; ++i) { const double d = (a[i] - ma) - (b[i] - mb); s += d * d; }
    return std::sqrt (s / NB);
}

static void render (int type, std::vector<float>& out)
{
    tw::TapeFxEngine e;
    e.prepare (SR);
    e.reset();
    tw::TapeFxEngine::Params p;
    p.type = type;
    p.mix = 1.0f;            // FULLY wet — the machine, nothing but the machine
    p.delayOn = false;       // the echo is a separate argument; this bar is the transport
    p.drive = 0.35f; p.age = 0.40f; p.flutter = 0.55f; p.bump = 0.70f; p.width = 0.0f;
    e.setParams (p);
    out.assign ((size_t) N, 0.0f);
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < WARM; ++i) e.process (noiseAt (i), noiseAt (i), l, r);
    for (int i = 0; i < N; ++i)
    { e.process (noiseAt (WARM + i), noiseAt (WARM + i), l, r); out[(size_t) i] = 0.5f * (l + r); }
}

int main()
{
    /* 🚨 SEED THE MACHINES DETERMINISTICALLY OR THIS CERT MEASURES NOISE. Every tape machine
       seeds its wow/flutter/drift streams from its OWN ADDRESS (TapeMachines.h: `rng.seed
       (tw::seedAddr (this, 0x5EEDu))`), which is right for the instrument and useless here — the
       first cut of this file reported Wire/Studio at 8.94 dB and then 5.59 dB after a change to
       PORTA, a mode neither of them shares anything with. fb636 built the switch for exactly
       this; without it the numbers below move on their own and no bar means anything. */
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);
    printf ("tp60 — FIVE TAPE MODES ON THE SHIPPED TapeFxEngine (deterministic seeds)\n\n");

    // ── [0] every type produces audio at all ─────────────────────────────────────────────────
    std::vector<float> sig[5];
    double sp[5][NB];
    {
        std::string d; bool alive = true;
        for (int t = 0; t < 5; ++t)
        {
            render (t, sig[t]);
            double rms = 0.0; for (float v : sig[t]) rms += (double) v * v;
            rms = std::sqrt (rms / (double) sig[t].size());
            bool finite = true; for (float v : sig[t]) if (! std::isfinite (v)) { finite = false; break; }
            if (! (rms > 1.0e-4) || ! finite) alive = false;
            d += std::string (NAME[t]) + " " + std::to_string (20.0 * std::log10 (rms + 1e-12)).substr (0, 6) + " dB  ";
            spectrum (sig[t], sp[t]);
        }
        chk (alive, "[0] ALL FIVE TYPES RENDER FINITE AUDIO", d);
    }

    // ── [1] 🚨 NO TWO MODES SOUND ALIKE ──────────────────────────────────────────────────────
    {
        double worst = 1.0e9; std::string wn, all;
        for (int a = 0; a < 5; ++a) for (int b = a + 1; b < 5; ++b)
        {
            const double dd = distance (sp[a], sp[b]);
            char buf[96];
            std::snprintf (buf, sizeof buf, "%s/%s %.2f  ", NAME[a], NAME[b], dd);
            all += buf;
            if (dd < worst) { worst = dd; wn = std::string (NAME[a]) + "/" + NAME[b]; }
        }
        chk (worst >= kMinSep,
             "[1] 🚨 ALL TEN PAIRS ARE FURTHER APART THAN A RIPPLE",
             all + " | closest " + wn + " at " + std::to_string (worst).substr (0, 5) + " dB (bar " + std::to_string (kMinSep).substr (0, 4) + ")");
    }

    // ── [2] 🚨 THE NEW MODES CLEAR THE BAR THE SHIPPED ONES SET ──────────────────────────────
    //  Porta is CassetteMachine and Wire is WireMachine — the same classes Cassette and Studio
    //  use. If the transport were still machine-keyed these two numbers would be 0.00.
    //  🔑 THE BAR IS SELF-CALIBRATING, not a number I picked: whatever the CLOSEST pair among the
    //  three SHIPPED modes is, the two new ones must be at least that far from everything. A mode
    //  that is harder to tell apart than Cassette and Reel already are is not a fifth machine.
    {
        double shipWorst = 1.0e9; std::string sw;
        for (int a = 0; a < 3; ++a) for (int b = a + 1; b < 3; ++b)
        { const double dd = distance (sp[a], sp[b]);
          if (dd < shipWorst) { shipWorst = dd; sw = std::string (NAME[a]) + "/" + NAME[b]; } }
        double newWorst = 1.0e9; std::string nw;
        for (int t = 3; t < 5; ++t) for (int o = 0; o < 5; ++o)
        { if (o == t) continue; const double dd = distance (sp[t], sp[o]);
          if (dd < newWorst) { newWorst = dd; nw = std::string (NAME[t]) + "/" + NAME[o]; } }
        chk (newWorst >= shipWorst,
             "[2] 🚨 PORTA AND WIRE ARE AT LEAST AS DISTINCT AS THE SHIPPED THREE ALREADY ARE",
             "closest shipped pair " + sw + " " + std::to_string (shipWorst).substr (0, 5)
             + " dB  ·  closest involving a NEW mode " + nw + " " + std::to_string (newWorst).substr (0, 5)
             + " dB  (Porta/Cassette and Wire/Studio are the pairs an alias would fail)");
    }

    // ── [3] TYPES 0..2 DID NOT MOVE ──────────────────────────────────────────────────────────
    {
        bool same = true; std::string d;
        for (int t = 0; t < 3; ++t)
        {
            const auto& byType = tw::TapeFxEngine::voiceForType (t);
            const auto& byMach = tw::TapeFxEngine::voiceFor (tw::TapeFxEngine::machineFor (t));
            const bool eq = (&byType == &byMach);
            if (! eq) same = false;
            d += std::string (NAME[t]) + (eq ? " ok  " : " MOVED  ");
        }
        chk (same, "[3] STUDIO / CASSETTE / REEL RESOLVE TO EXACTLY THE VOICE THEY ALWAYS DID — no saved patch changes",
             d + "(Porta and Wire fill reserved slots 3 and 4, appended, never reordered)");
    }

    // ── [4] the machine map is what the comment says ─────────────────────────────────────────
    {
        const bool ok = tw::TapeFxEngine::machineFor (0) == 2
                     && tw::TapeFxEngine::machineFor (1) == 1
                     && tw::TapeFxEngine::machineFor (2) == 0
                     && tw::TapeFxEngine::machineFor (3) == 1
                     && tw::TapeFxEngine::machineFor (4) == 2;
        chk (ok, "[4] THE MACHINE MAP IS Studio→Wire · Cassette→Cassette · Reel→Studio · Porta→Cassette · Wire→Wire",
             "0:" + std::to_string (tw::TapeFxEngine::machineFor (0)) + " 1:" + std::to_string (tw::TapeFxEngine::machineFor (1))
             + " 2:" + std::to_string (tw::TapeFxEngine::machineFor (2)) + " 3:" + std::to_string (tw::TapeFxEngine::machineFor (3))
             + " 4:" + std::to_string (tw::TapeFxEngine::machineFor (4)));
    }

    printf ("\n  %d passed, %d failed\n", npass, nfail);
    return nfail ? 1 : 0;
}
