// ══════════════════════════════════════════════════════════════════════════════════════════════
//  spec_cert.cpp — fb467 THE SPECTRAL OVERPASS: Disperse · Partial Range · the partial cap
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/spec_cert.cpp -o /tmp/spec_cert \
//            -framework Accelerate
//    (run from plugins/TerrainInstrument — the NAME gates read Source/ off disk)
//
//  MUTATIONS (fb421/fb453 — a gate that has never failed has never been tested). Each of these
//  must turn a SPECIFIC gate red, and the harness names which:
//    -DSPEC_MUT_NO_WINDOW      the window blend is skipped          -> W2 W3 W4
//    -DSPEC_MUT_FIXED_C        Disperse ignores the band top        -> D3
//    -DSPEC_MUT_DISPERSE_AMP   Disperse also touches amplitude      -> D2 W4
//    -DSPEC_MUT_PHASOR_LERP    the window lerps the PHASOR          -> W4
//  A mutation is applied by the harness to its OWN copy of the law where it can be, and by a
//  -D on the real header where it cannot; see MUTATION.md notes in the commit.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "SpectralMorph.h"
#include "SynthModConfig.h"
#include "WavetableBank.h"
#include "Shapers.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

static int gPass = 0, gFail = 0;
static void ok (const char* n, const char* d = "")   { ++gPass; printf ("  ok    %-58s %s\n", n, d); }
static void bad (const char* n, const char* d = "")  { ++gFail; printf ("  FAIL  %-58s %s\n", n, d); }
static void gate (bool c, const char* n, const char* d = "") { c ? ok (n, d) : bad (n, d); }

static const int NPTS = 2048;

// ── helpers ───────────────────────────────────────────────────────────────────────────────────
static float specPeakRatio (const tw::WavetableSpec& s)     // the band top, recomputed independently
{
    float mx = 0.0f;
    for (int f = 0; f < 16; ++f) { const tw::FrameSpec& fs = s.frames[(size_t) f];
        if (fs.numPartials > 0) for (int i = 0; i < fs.numPartials; ++i) mx = std::max (mx, std::abs (fs.partials[(size_t) i].amp));
        else                    for (int h = 1; h <= fs.numHarmonics; ++h) mx = std::max (mx, std::abs (fs.amplitudes[(size_t) (h-1)])); }
    const float thr = mx * 1.0e-3f; float top = 1.0f;
    for (int f = 0; f < 16; ++f) { const tw::FrameSpec& fs = s.frames[(size_t) f];
        if (fs.numPartials > 0) { for (int i = 0; i < fs.numPartials; ++i) if (std::abs (fs.partials[(size_t) i].amp) > thr) top = std::max (top, fs.partials[(size_t) i].ratio); }
        else                    { for (int h = 1; h <= fs.numHarmonics; ++h) if (std::abs (fs.amplitudes[(size_t) (h-1)]) > thr) top = std::max (top, (float) h); } }
    return top;
}

static bool sameSpec (const tw::WavetableSpec& a, const tw::WavetableSpec& b)
{
    for (int f = 0; f < 16; ++f)
    { const tw::FrameSpec& x = a.frames[(size_t) f]; const tw::FrameSpec& y = b.frames[(size_t) f];
      if (x.numPartials != y.numPartials || x.numHarmonics != y.numHarmonics) return false;
      for (int i = 0; i < x.numPartials; ++i)
        if (x.partials[(size_t) i].ratio != y.partials[(size_t) i].ratio
         || x.partials[(size_t) i].amp   != y.partials[(size_t) i].amp
         || x.partials[(size_t) i].phase != y.partials[(size_t) i].phase) return false;
      for (int h = 1; h <= x.numHarmonics; ++h)
        if (x.amplitudes[(size_t)(h-1)] != y.amplitudes[(size_t)(h-1)]
         || x.phases    [(size_t)(h-1)] != y.phases    [(size_t)(h-1)]) return false; }
    return true;
}

// The dry partial list, derived HERE and not by the code under test. apply(amount 0) short-circuits
// to the base spec, which is in the HARMONIC representation — every window gate that used it as its
// "dry" reference iterated over ZERO partials and passed on an empty set (caught 2026-08-23; the
// gates print their sample count for exactly this reason).
struct DryList { std::vector<tw::FrameSpec::Partial> p; };
static DryList dryPartials (const tw::FrameSpec& f)
{
    DryList d;
    if (f.numPartials > 0)
    { for (int i = 0; i < f.numPartials && (int) d.p.size() < tw::FrameSpec::kMaxPartials; ++i)
        if (f.partials[(size_t) i].amp != 0.0f) d.p.push_back (f.partials[(size_t) i]); }
    else
    { for (int h = 1; h <= f.numHarmonics && (int) d.p.size() < tw::FrameSpec::kMaxPartials; ++h)
        if (f.amplitudes[(size_t) (h-1)] != 0.0f)
            d.p.push_back (tw::FrameSpec::Partial { (float) h, f.amplitudes[(size_t)(h-1)], f.phases[(size_t)(h-1)] }); }
    return d;
}

// one cycle of the built table, frame 8, mip 0
static void cycle (const tw::Wavetable& wt, std::vector<double>& out)
{
    out.resize ((size_t) NPTS);
    for (int i = 0; i < NPTS; ++i) out[(size_t) i] = wt.lookup (0, 8.0f/15.0f, (float) i / (float) NPTS);
}
static void harmonics (const std::vector<double>& x, std::vector<double>& mag, int HN)
{
    mag.assign ((size_t) HN + 1, 0.0);
    for (int h = 1; h <= HN; ++h) { double re = 0.0, im = 0.0;
        for (int n = 0; n < NPTS; ++n) { const double a = 2.0*3.14159265358979*h*n/NPTS; re += x[(size_t)n]*std::cos(a); im -= x[(size_t)n]*std::sin(a); }
        mag[(size_t)h] = 2.0*std::sqrt(re*re+im*im)/NPTS; }
}
static void foldReal (const std::vector<double>& in, std::vector<double>& out, float amt)
{
    tw::shapers::FoldState st {};
    out.assign (in.size(), 0.0);
    for (int pass = 0; pass < 2; ++pass)                       // fb460 — ADAA carries state: warm-up lap
        for (size_t i = 0; i < in.size(); ++i)
        { const float o = tw::shapers::applyFoldADAA ((float) in[i], 0, amt, st); if (pass) out[i] = o; }
}

// ══════════════════════════════════════════════════════════════════════════════════════════════
int main (int argc, char** argv)
{
    const std::string root = (argc > 1) ? argv[1] : "Source/";
    tw::WavetableBank bank;
    printf ("\n══ fb467 — SPECTRAL OVERPASS: Disperse · Partial Range ══\n\n");

    const int kHarmonicPresets[] = { 4, 1, 2, 9, 20, 24, 29 };   // integer-harmonic tables
    const auto DISP = tw::SpectralMode::Disperse;

    // ── N — THE NAME GATES (fb373: setChoiceValue normalises by the JS array's LENGTH, so a
    //        mode list that disagrees with the param's cardinality silently selects the WRONG mode.
    //        fb-"gate all your labels": a name authored twice is a name that will drift.) ────────
    {
        auto slurp = [&] (const char* rel) { std::ifstream f (root + rel); std::string s ((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); return s; };
        const std::string cpp = slurp ("PluginProcessor.cpp");
        const std::string htm = slurp ("ui/public/index.html");
        gate (! cpp.empty() && ! htm.empty(), "N0  the two files the name gates read are readable", cpp.empty()||htm.empty() ? "pass Source/ as argv[1]" : "");

        // every name in the enum must appear in BOTH lists
        const char* names[] = { "Harmonic Stretch", "Inharmonic Stretch", "Vocode", "Smear",
                                "Random Amps", "Data Compress", "Spectral Phaser", "Disperse" };
        const int nNames = (int) (sizeof (names) / sizeof (names[0]));
        int inCpp = 0, inJs = 0;
        const size_t jsAt = htm.find ("const SPECTRAL_MODES");
        const std::string js = (jsAt == std::string::npos) ? std::string() : htm.substr (jsAt, 400);
        for (const char* n : names) { if (cpp.find (std::string ("\"") + n + "\"") != std::string::npos) ++inCpp;
                                      if (js.find (std::string ("'") + n + "'") != std::string::npos) ++inJs; }
        char b[160];
        snprintf (b, sizeof b, "%d/%d in the C++ StringArray, %d/%d in SPECTRAL_MODES", inCpp, nNames, inJs, nNames);
        gate (inCpp == nNames && inJs == nNames, "N1  every SpectralMode name is authored in BOTH lists", b);

        // the JS array's LENGTH is what setChoiceValue divides by — it must equal the enum count
        int commas = 0; for (size_t i = 0; i < js.size() && js[i] != ']'; ++i) if (js[i] == ',') ++commas;
        snprintf (b, sizeof b, "SPECTRAL_MODES has %d entries, enum Count = %d", commas + 1, (int) tw::SpectralMode::Count);
        gate (commas + 1 == (int) tw::SpectralMode::Count, "N2  SPECTRAL_MODES length == the param's cardinality", b);

        // N3 — THE DESTINATION NUMBERS, COMPARED RATHER THAN COMMENTED. index.html's KNOBDEST
        // hardcodes the window's base ints; SynthModConfig.h has a static_assert whose MESSAGE says
        // so. A message is not a check: the assert fires if the C++ moves, and stays silent if the
        // JS does. Saved projects store dest ints, so a drift here silently re-points every window
        // route in every existing project at something else.
        auto jsInt = [&] (const char* key) -> int {
            const size_t k = htm.find (key); if (k == std::string::npos) return -1;
            const size_t c = htm.find (':', k); if (c == std::string::npos) return -1;
            return std::atoi (htm.c_str() + c + 1); };
        const int jsLo = jsInt ("'SPECTRAL_LO':"), jsHi = jsInt ("'SPECTRAL_HI':");
        snprintf (b, sizeof b, "KNOBDEST says Lo=%d Hi=%d; the enum says Lo=%d Hi=%d",
                  jsLo, jsHi, (int) wc::ModDest::SpecLoA, (int) wc::ModDest::SpecHiA);
        gate (jsLo == (int) wc::ModDest::SpecLoA && jsHi == (int) wc::ModDest::SpecHiA,
              "N3  the UI's destination ints MATCH the C++ enum", b);

        // N4 — and the rack block's own bound, which the JS derives geometrically. If the C++
        // appended anything BELOW FxModEnd, these two would disagree and every saved rack route
        // would shift.
        const size_t fb = htm.find ("var FXMOD_BASE=");
        const int jsBase = (fb == std::string::npos) ? -1 : std::atoi (htm.c_str() + fb + 15);
        snprintf (b, sizeof b, "JS FXMOD_BASE=%d + 16*6*12 = %d; C++ FxModEnd = %d",
                  jsBase, jsBase + 1152, (int) wc::ModDest::FxModEnd);
        gate (jsBase == (int) wc::ModDest::FxModBase && jsBase + 1152 == (int) wc::ModDest::FxModEnd,
              "N4  the rack block's bound agrees across C++ and JS", b);

        // N5 — the CUT's corner mapping is authored twice: once as constants in SpectralMorph.h and
        // once in fmtSynReadout, which has to reproduce the same curve to print a harmonic from a
        // normalised knob. If they drift the readout names one corner while the DSP uses another,
        // and both look entirely plausible.
        auto num = [&] (const std::string& hay, const char* key) -> double {
            const size_t k = hay.find (key); if (k == std::string::npos) return -1.0;
            return std::atof (hay.c_str() + k + std::strlen (key)); };
        const double jLoB = num (htm, "const SPECCUT_LO_BASE = "), jLoO = num (htm, "SPECCUT_LO_OCT = ");
        const double jHiB = num (htm, "SPECCUT_HI_BASE = "),       jHiO = num (htm, "SPECCUT_HI_OCT = ");
        snprintf (b, sizeof b, "C++ (%.2f, %.0f / %.2f, %.0f)  JS (%.2f, %.0f / %.2f, %.0f)",
                  (double) tw::SpectralMorph::kCutLoBase, (double) tw::SpectralMorph::kCutLoOct,
                  (double) tw::SpectralMorph::kCutHiBase, (double) tw::SpectralMorph::kCutHiOct,
                  jLoB, jLoO, jHiB, jHiO);
        gate (jLoB == (double) tw::SpectralMorph::kCutLoBase && jLoO == (double) tw::SpectralMorph::kCutLoOct
           && jHiB == (double) tw::SpectralMorph::kCutHiBase && jHiO == (double) tw::SpectralMorph::kCutHiOct,
              "N5  the cut's corner mapping agrees across the DSP and the readout", b);
    }

    // ── D — DISPERSE ──────────────────────────────────────────────────────────────────────────
    {
        const tw::WavetableSpec base = tw::WavetableBank::specForPreset (1);

        // D1 identity at amount 0 (constraint #5: the approach to 0 must be continuous AND exact)
        gate (sameSpec (tw::SpectralMorph::apply (base, DISP, 0.0f), base),
              "D1  Disperse at amount 0 is the EXACT identity");

        // D2 magnitude-exact THROUGH the whole render path — the snap, 34 mips, peak normalise.
        //    This is the property that makes Disperse the one mode that cannot dull anything.
        {
            double worst = -400.0; int tested = 0;
            for (int pr : kHarmonicPresets)
            {
                const tw::WavetableSpec b = tw::WavetableBank::specForPreset (pr);
                tw::Wavetable a, c; a.buildFromSpec (b);
                c.buildFromSpec (tw::SpectralMorph::apply (b, DISP, 1.0f));
                const tw::WavetableSpec sa = a.toSpec(), sc = c.toSpec();
                const int hi = (int) std::min (512.0f, specPeakRatio (b));
                double ea = 0.0, ec = 0.0, pk = 0.0;
                for (int h = 1; h <= hi; ++h) { const double x = sa.frames[8].amplitudes[(size_t)(h-1)], y = sc.frames[8].amplitudes[(size_t)(h-1)];
                                                ea += x*x; ec += y*y; pk = std::max (pk, x); }
                const double g = ec > 0.0 ? std::sqrt (ea/ec) : 1.0;      // peak-normalisation is a global scale
                for (int h = 1; h <= hi; ++h)
                { const double x = sa.frames[8].amplitudes[(size_t)(h-1)];
                  if (x < pk * 1e-4) continue;
                  const double y = sc.frames[8].amplitudes[(size_t)(h-1)] * g;
                  worst = std::max (worst, 20.0*std::log10 (std::max (1e-30, std::abs (x-y)) / pk)); ++tested; }
            }
            char b[160]; snprintf (b, sizeof b, "worst %+.1f dBr over %d bins, 7 tables", worst, tested);
            gate (worst < -100.0, "D2  Disperse is MAGNITUDE-EXACT through the render path", b);
        }

        // D3 the band top is the unit: the same knob = the same CYCLES of spread on any table.
        //    (A fixed coefficient disperses a 511-harmonic table 21x harder — fb467 measured that
        //     and the crest curve came out non-monotone. This gate is what pins the fix.)
        {
            double lo = 1e30, hi = -1e30; char b[200] = "";
            for (int pr : kHarmonicPresets)
            {
                const tw::WavetableSpec s = tw::WavetableBank::specForPreset (pr);
                const tw::WavetableSpec d = tw::SpectralMorph::apply (s, DISP, 1.0f);
                const float H = specPeakRatio (s);
                if (H < 4.0f) continue;
                // recover c from the top harmonic's phase shift, then D = c*(H-1)/pi
                int    ti = -1; float tr = 0.0f;
                for (int i = 0; i < d.frames[8].numPartials; ++i)
                    if (d.frames[8].partials[(size_t) i].ratio > tr && d.frames[8].partials[(size_t) i].ratio <= H)
                    { tr = d.frames[8].partials[(size_t) i].ratio; ti = i; }
                if (ti < 0) continue;
                // the dry phase of the same partial
                const tw::WavetableSpec z = tw::SpectralMorph::apply (s, DISP, 0.0f);
                float dry = 0.0f;
                if (z.frames[8].numPartials > 0) dry = z.frames[8].partials[(size_t) ti].phase;
                else                             dry = z.frames[8].phases[(size_t) ((int) tr - 1)];
                const double dphi = d.frames[8].partials[(size_t) ti].phase - dry;
                const double c    = dphi / std::max (1e-9, (double) (tr - 1.0f) * (tr - 1.0f));
                const double D    = c * (H - 1.0) / 3.14159265358979;
                lo = std::min (lo, D); hi = std::max (hi, D);
            }
            snprintf (b, sizeof b, "cycles of spread at amount 1: %.3f .. %.3f (want 4.00 on every table)", lo, hi);
            gate (lo > 3.6 && hi < 4.4, "D3  the SAME knob = the SAME cycles of spread on every table", b);
        }

        // D4 the knob TRAVELS everywhere — measured through the shipped fold, because a phase-only
        //    op is inaudible on a bare steady oscillator (fb282/fb283: 102% "divergence", 0.02 dB
        //    of real magnitude change, Max heard nothing). Every 0.1 must move the OUTPUT.
        // D5 the FLOOR gate that D4's ceiling needs (fb462): at amount 0 the same metric must be 0.
        {
            double worstStep = 1e30; const char* worstAt = "";
            static char nm[64];
            for (int pr : { 4, 1, 9, 29 })
            {
                const tw::WavetableSpec s = tw::WavetableBank::specForPreset (pr);
                std::vector<double> prev;
                for (int k = 0; k <= 10; ++k)
                {
                    tw::Wavetable wt; wt.buildFromSpec (tw::SpectralMorph::apply (s, DISP, (float) k * 0.1f));
                    std::vector<double> c, y, m; cycle (wt, c); foldReal (c, y, 0.5f); harmonics (y, m, 256);
                    if (! prev.empty())
                    { double dd = 0.0, tt = 0.0;
                      for (int h = 1; h <= 256; ++h) { dd += std::abs (m[(size_t)h] - prev[(size_t)h]); tt += prev[(size_t)h]; }
                      const double step = 20.0*std::log10 (std::max (1e-12, dd) / std::max (1e-12, tt));
                      if (step < worstStep) { worstStep = step; snprintf (nm, sizeof nm, "preset %d, a=%.1f->%.1f", pr, (k-1)*0.1, k*0.1); worstAt = nm; } }
                    prev = m;
                }
            }
            char b[200]; snprintf (b, sizeof b, "weakest step %+.2f dBr (%s)", worstStep, worstAt);
            gate (worstStep > -25.0, "D4  every 0.1 of the knob moves the FOLDED output", b);

            // the floor: amount 0 vs amount 0 must be a dead zero on the same metric
            const tw::WavetableSpec s = tw::WavetableBank::specForPreset (1);
            tw::Wavetable a, z; a.buildFromSpec (tw::SpectralMorph::apply (s, DISP, 0.0f)); z.buildFromSpec (s);
            std::vector<double> ca, cz, ya, yz, ma, mz;
            cycle (a, ca); cycle (z, cz); foldReal (ca, ya, 0.5f); foldReal (cz, yz, 0.5f);
            harmonics (ya, ma, 256); harmonics (yz, mz, 256);
            double dd = 0.0, tt = 0.0; for (int h = 1; h <= 256; ++h) { dd += std::abs (ma[(size_t)h]-mz[(size_t)h]); tt += mz[(size_t)h]; }
            const double fl = 20.0*std::log10 (std::max (1e-12, dd) / std::max (1e-12, tt));
            snprintf (b, sizeof b, "amount 0 on the SAME metric: %+.1f dBr", fl);
            gate (fl < -100.0, "D5  and the metric reads DEAD at amount 0 (the floor D4 needs)", b);
        }
    }

    // ── H — THE SPECTRAL CUTS, on their own knobs ─────────────────────────────────────────────
    //   fb472: Lo/Hi are a fourth-order Butterworth cut in HARMONIC NUMBER, so the corner rides the
    //   note — Serum 2's spectral Lo/Hi markers are in Hz and do not track. They apply with NO morph
    //   mode selected, which is the whole reason they live on knobs instead of in the mode list.
    {
        // ⚠️ ProphetSaw, NOT a triangle: a triangle carries ODD HARMONICS ONLY, and probing even
        //    ones made an earlier version of this gate read "+0.00 dB, no change" at every frequency
        //    while the filter was exactly right. The probes below are taken from the table.
        const tw::WavetableSpec base = tw::WavetableBank::specForPreset (4);
        const auto NONE = tw::SpectralMode::None;

        gate (sameSpec (tw::SpectralMorph::apply (base, NONE, 0.0f, 0.0f, 1.0f), base),
              "H1  both cuts OFF, no mode: the EXACT identity");

        auto ampAt = [&] (const tw::WavetableSpec& sp, int h) -> double {
            const tw::FrameSpec& f = sp.frames[8];
            if (f.numPartials > 0)
            { for (int i = 0; i < f.numPartials; ++i)
                if ((int) std::lround (f.partials[(size_t) i].ratio) == h) return std::abs (f.partials[(size_t) i].amp); return 0.0; }
            return (h >= 1 && h <= f.numHarmonics) ? std::abs (f.amplitudes[(size_t) (h-1)]) : 0.0; };

        // the dry reference, through the SAME path (a cut just barely on, then read the partials)
        const tw::WavetableSpec dry = tw::SpectralMorph::apply (base, NONE, 0.0f, 1.0e-7f, 1.0f);
        int topH = 1; for (int h = 1; h <= 512; ++h) if (ampAt (dry, h) > 1e-9) topH = h;
        const int hLo = 2, hC = 8, hHi = std::min (32, topH);

        // the knob position whose corner lands on a given harmonic — computed, never guessed
        auto tLo = [] (double rc) { return (float) (std::log2 (rc / (double) tw::SpectralMorph::kCutLoBase) / (double) tw::SpectralMorph::kCutLoOct); };
        auto tHi = [] (double rc) { return (float) (std::log2 (rc / (double) tw::SpectralMorph::kCutHiBase) / (double) tw::SpectralMorph::kCutHiOct); };
        auto meas = [&] (float lo01, float hi01, int h) {
            const tw::WavetableSpec c = tw::SpectralMorph::apply (base, NONE, 0.0f, lo01, hi01);
            const double d = ampAt (dry, h), v = ampAt (c, h);
            return (d > 1e-9) ? 20.0 * std::log10 (std::max (1e-12, v / d)) : 1e9; };

        {   const float t = tLo (8.0);
            const double r2 = meas (t, 1.0f, hLo), r8 = meas (t, 1.0f, hC), rT = meas (t, 1.0f, hHi);
            const bool have = (r2 < 1e8 && r8 < 1e8 && rT < 1e8);
            char b[230]; snprintf (b, sizeof b, "top h%d · h%d %+.1f dB · h%d (the corner) %+.2f dB · h%d %+.3f dB%s",
                                   topH, hLo, r2, hC, r8, hHi, rT, have ? "" : "  [NO REFERENCE]");
            gate (have && r2 < -40.0 && std::abs (r8 + 3.01) < 0.15 && rT > -0.6,
                  "H2  LOW CUT, with NO morph mode: -3.01 dB at the corner, 24 dB/oct below", b); }
        {   const float t = tHi (8.0);
            const double r2 = meas (0.0f, t, hLo), r8 = meas (0.0f, t, hC), rT = meas (0.0f, t, hHi);
            const bool have = (r2 < 1e8 && r8 < 1e8 && rT < 1e8);
            char b[230]; snprintf (b, sizeof b, "h%d %+.3f dB · h%d (the corner) %+.2f dB · h%d %+.1f dB%s",
                                   hLo, r2, hC, r8, hHi, rT, have ? "" : "  [NO REFERENCE]");
            gate (have && r2 > -0.6 && std::abs (r8 + 3.01) < 0.15 && rT < -40.0,
                  "H3  HIGH CUT mirrors it, same order", b); }
        {   // AMPLITUDE-ONLY, so it composes with Disperse (phase-only) and every other mode
            const tw::WavetableSpec c = tw::SpectralMorph::apply (base, NONE, 0.0f, tLo (8.0), tHi (24.0));
            const DryList d0 = dryPartials (base.frames[8]);
            double wR = 0.0, wP = 0.0; int n2 = 0;
            for (int i = 0; i < c.frames[8].numPartials && i < (int) d0.p.size(); ++i)
            { wR = std::max (wR, (double) std::abs (c.frames[8].partials[(size_t) i].ratio - d0.p[(size_t) i].ratio));
              wP = std::max (wP, (double) std::abs (c.frames[8].partials[(size_t) i].phase - d0.p[(size_t) i].phase)); ++n2; }
            char b[190]; snprintf (b, sizeof b, "worst ratio drift %.3g, worst phase drift %.3g over %d partials", wR, wP, n2);
            gate (wR == 0.0 && wP == 0.0 && n2 > 20, "H4  a cut touches AMPLITUDE only", b); }
        {   // and it STACKS with a mode rather than replacing it
            const tw::WavetableSpec m  = tw::SpectralMorph::apply (base, tw::SpectralMode::HarmonicStretch, 1.0f);
            const tw::WavetableSpec mc = tw::SpectralMorph::apply (base, tw::SpectralMode::HarmonicStretch, 1.0f, tLo (8.0), 1.0f);
            double lowM = 0.0, lowMC = 0.0;
            for (int i = 0; i < m.frames[8].numPartials; ++i)
                if (m.frames[8].partials[(size_t) i].ratio < 6.0f) lowM += std::abs (m.frames[8].partials[(size_t) i].amp);
            for (int i = 0; i < mc.frames[8].numPartials; ++i)
                if (mc.frames[8].partials[(size_t) i].ratio < 6.0f) lowMC += std::abs (mc.frames[8].partials[(size_t) i].amp);
            char b[190]; snprintf (b, sizeof b, "low-partial sum %.4f -> %.4f with the cut on", lowM, lowMC);
            gate (lowM > 1e-6 && lowMC < lowM * 0.5, "H5  a cut STACKS with a morph mode, it does not replace one", b); }
    }

    // ── W — THE PARTIAL WINDOW IS RETIRED ─────────────────────────────────────────────────────
    //   fb467 put a smoothstep WINDOW on these two knobs — which partials the morph was allowed to
    //   touch. fb472 replaced it with an actual cut, at Max's word: "not a new spectral, not two new
    //   spectrum modes... wire in the low and the high cut in the back channel where it says low and
    //   high, because I thought that's what it was for." W1-W5 measured a mechanism that no longer
    //   exists; H1-H5 above measure the one that replaced it, on the same two knobs and the same two
    //   mod destinations. Recorded rather than deleted quietly, per the porting law.

    printf ("\n  PASS %d   FAIL %d\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
