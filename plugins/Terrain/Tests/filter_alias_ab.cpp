// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp21 — THE FILTER'S 2× : IS IT BUYING ANYTHING?  The A/B that must pass before any CPU fix.
//
//  26 of the 118 filter types run their whole core TWICE per sample (fb603's 2× half-band). That
//  exists to tame the ALIASING a nonlinear core makes when it is driven. e47f140 measured the price
//  (Ladder +5.26 % of a core at an 8-note chord vs SVF +0.64) and left a lead: at DRIVE 0 the Ladder
//  measures 0.10 % THD — near linear, so the 2× may be buying nothing there.
//
//  THIS FILE IS THE GATE, not the fix. It drives the REAL tw::filters::FilterSlot through the REAL
//  tw::HalfBandUp2x / Down2x path (the same expressions SynthVoice's sample loop runs) and asks one
//  question per (type, drive, resonance) cell: HOW MUCH ALIASING DOES THE BASE-RATE PATH MAKE, and
//  how much does the 2× path remove? A cell where both are at the noise floor is a cell where the
//  2× is free to drop. A cell where they differ is a cell that must keep it.
//
//  METHOD. A pure sine at an EXACT FFT bin (f0 = 1500·fs/N) goes in. A nonlinear filter answers with
//  harmonics at k·f0 — also exact bins, so a rectangular window leaks nothing. Every harmonic above
//  Nyquist folds to a bin that is NOT a multiple of 1500. So:
//      harmonic energy = bins 1500·m below Nyquist      (the sound)
//      alias energy    = every other bin above bin 8    (the artefact)
//  and ALIAS dB = 10·log10(alias / harmonic). −90 dB is silence; −40 dB is audible grit on a sine.
//  TONE dB compares the two paths' harmonic magnitudes: how different the cell SOUNDS, aliasing
//  aside. It is the second half of the question — a switch nobody can hear needs both to be small.
//
//    clang++ -O2 -std=c++20 <the plugin's JUCE defines + includes> Tests/filter_alias_ab.cpp -o /tmp/fab
//    /tmp/fab            → the grid, one line per cell
//    /tmp/fab csv        → the same as CSV for a threshold fit
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "../Source/SynthVoice.h"      // tw::HalfBandUp2x / HalfBandDown2x — the REAL converters
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <complex>
#include <string>

static constexpr double SR   = 48000.0;
static constexpr int    N    = 32768;          // FFT length; bin = 1.4648 Hz
static constexpr int    KF0  = 1500;           // f0 = bin 1500 = 2197.27 Hz (exact bin ⇒ zero leakage)
static constexpr int    SETTLE = 16384;

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

/** One run of the REAL filter. `os` picks the path SynthVoice picks: base rate, or upsample →
 *  the core twice → decimate, with the coefficient rate doubled exactly as the voice doubles it. */
static void runPath (tw::filters::Type type, float cutHz, float res, float drv, bool os, std::vector<double>& out)
{
    tw::filters::FilterSlot slot;
    slot.prepare (SR);
    slot.setType (type);
    slot.setParams (cutHz, res, drv, os ? SR * 2.0 : SR);
    tw::HalfBandUp2x  up;
    tw::HalfBandDown2x dn;
    const double w = 2.0 * 3.14159265358979323846 * (double) KF0 / (double) N;
    out.assign (N, 0.0);
    for (int i = 0; i < SETTLE + N; ++i)
    {
        const float x = 0.5f * (float) std::sin (w * (double) i);
        float y;
        if (os)
        {
            float e, o;
            up.process (x, e, o);
            float eL = e, eR = e; slot.processStereo (eL, eR);
            float oL = o, oR = o; slot.processStereo (oL, oR);
            y = dn.process (eL, oL);
        }
        else { float l = x, r = x; slot.processStereo (l, r); y = l; }
        if (i >= SETTLE) out[(size_t) (i - SETTLE)] = (double) y;
    }
}

struct Spec { double harm; double alias; std::vector<double> hmag; std::vector<std::complex<double>> bins; };

static Spec analyse (const std::vector<double>& x)
{
    std::vector<std::complex<double>> a (N);
    for (int i = 0; i < N; ++i) a[(size_t) i] = { x[(size_t) i], 0.0 };
    fft (a);
    Spec s { 0.0, 0.0, {} };
    std::vector<char> isHarm ((size_t) N / 2, 0);
    for (int m = 1; m * KF0 < N / 2; ++m)
        for (int d = -1; d <= 1; ++d)                      // ±1 bin: the core's own slow drift
            if (m * KF0 + d > 0 && m * KF0 + d < N / 2) isHarm[(size_t) (m * KF0 + d)] = 1;
    for (int m = 1; m * KF0 < N / 2; ++m)
    {
        double e = 0.0;
        for (int d = -1; d <= 1; ++d) e += std::norm (a[(size_t) (m * KF0 + d)]);
        s.hmag.push_back (e); s.harm += e;
    }
    for (int b = 8; b < N / 2; ++b) if (! isHarm[(size_t) b]) s.alias += std::norm (a[(size_t) b]);
    s.bins.assign (a.begin(), a.begin() + N / 2);   // tp21 — kept for the DIRECT null between the two paths
    return s;
}

static double db (double num, double den) { return 10.0 * std::log10 ((num + 1e-30) / (den + 1e-30)); }

struct Cell { const char* name; tw::filters::Type type; };

int main (int argc, char** argv)
{
    const bool csv = argc > 1 && ! std::strcmp (argv[1], "csv");
    const Cell types[] = {
        { "Ladder LP24", tw::filters::Type::LADDER_LP24 },
        { "Diode LP",    tw::filters::Type::DIODE_LP    },
        { "Acid 303",    tw::filters::Type::ACID_303    },
        { "Waveshaper",  tw::filters::Type::WAVESHAPER  },
        { "Ring Mod",    tw::filters::Type::RING_MOD    },   // inharmonic by design — read its NULL, never its alias
        { "Bode Shift",  tw::filters::Type::BODE_SHIFT  },   // ditto (a frequency shifter makes no harmonics)
        { "Xpander HP6", tw::filters::Type::XPD_HP6     },
        { "SVF LP (ctl)",tw::filters::Type::SVF_LP      },   // never oversampled — the control
    };
    const float drives[] = { 0.0f, 0.10f, 0.20f, 0.30f, 0.50f, 0.75f, 1.0f };
    const float resos[]  = { 0.0f, 0.50f, 0.90f };
    const float cuts[]   = { 8000.0f, 2000.0f };

    if (csv) printf ("type,cut,res,drive,baseAliasDb,osAliasDb,aliasGainDb,nullDb,gainMatchedDb,levelOffsetDb\n");
    else printf ("\n══ tp21 — DOES THE 2× BUY ANYTHING?  sine %.1f Hz, the real FilterSlot, the real half-band ══\n"
                 "   ALIAS = artefact vs sound (−90 = silence, −40 = audible grit) · GAIN = what the 2× removes\n"
                 "   NULL  = how far apart the two paths SOUND (magnitude only; the 2x 2.2-sample delay excluded)\n"
                 "   GAINMATCHED = the same, after the best single level trim (low = the paths differ only in LEVEL)\n"
                 "   each cell: BASE alias / 2x alias / NULL / GAINMATCHED, all dB\n",
                 (double) KF0 * SR / (double) N);

    for (const auto& t : types)
    {
        if (! csv) printf ("\n%s\n", t.name);
        for (float cut : cuts)
        {
            if (! csv) printf ("  cut %5.0f Hz\n", (double) cut);
            for (float res : resos)
            {
                if (! csv) printf ("    res %.2f   drive:", (double) res);
                for (float drv : drives)
                {
                    std::vector<double> b, o;
                    runPath (t.type, cut, res, drv, false, b);
                    runPath (t.type, cut, res, drv, true,  o);
                    const Spec sb = analyse (b), so = analyse (o);
                    const double bA = db (sb.alias, sb.harm), oA = db (so.alias, so.harm);
                    // tp21 — THE DIRECT NULL. The harmonic/alias split above cannot judge a RING MOD or a
                    //  BODE SHIFT: those are inharmonic BY DESIGN, so their whole output reads as "alias".
                    //  This measure asks the only question that always means something — how far apart do
                    //  the two paths actually sound? — and it is also the switch's own audibility.
                    //  MAGNITUDES, not complex values. The 2x round trip carries ~2.2 samples of group
                    //  delay (fb603 chose the all-pass form to keep it that small), so a COMPLEX null
                    //  measures that delay and drowns everything else - every cell read about -1 dB, which
                    //  is the delay, not a tone difference. What "does it sound the same" actually asks is
                    //  whether the two paths put the same ENERGY in the same places. The delay is real and
                    //  it is handled where it belongs: at the switch, never by pretending the paths align.
                    double dif = 0.0, ref = 0.0, cross = 0.0;
                    for (size_t k = 8; k < sb.bins.size() && k < so.bins.size(); ++k)
                    { const double mb = std::abs (sb.bins[k]), mo = std::abs (so.bins[k]);
                      dif += (mb - mo) * (mb - mo); ref += mo * mo; cross += mb * mo; }
                    const double tdb = db (dif, ref);
                    //  GAINMATCHED: the same difference after the best single scalar is divided out. If a
                    //  cell's NULL is a flat LEVEL offset between the paths, this collapses to the noise
                    //  floor and the offset is one multiply to fix. If it stays high, the two paths differ
                    //  in SHAPE, and no trim can reconcile them - that is the finding that decides the fix.
                    const double gfit = cross / (ref + 1e-30);
                    double resid = 0.0;
                    for (size_t k = 8; k < sb.bins.size() && k < so.bins.size(); ++k)
                    { const double mb = std::abs (sb.bins[k]), mo = std::abs (so.bins[k]);
                      resid += (mb - gfit * mo) * (mb - gfit * mo); }
                    const double gdb = db (resid, ref), gainDb = 20.0 * std::log10 (gfit + 1e-30);
                    if (csv) printf ("%s,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", t.name, (double) cut, (double) res, (double) drv, bA, oA, bA - oA, tdb, gdb, gainDb);
                    else     printf ("  %.2f:%6.1f/%6.1f/%6.1f/%6.1f", (double) drv, bA, oA, tdb, gdb);
                }
                if (! csv) printf ("\n");
            }
        }
    }
    if (! csv) printf ("\n   (a cell whose BASE and 2x alias agree is a cell the 2× cannot justify;\n"
                       "    a cell whose NULL is low is one where dropping it changes nothing anybody hears)\n\n");
    return 0;
}
