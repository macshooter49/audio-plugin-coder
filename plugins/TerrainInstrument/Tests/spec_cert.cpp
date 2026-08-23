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
        int inCpp = 0, inJs = 0;
        const size_t jsAt = htm.find ("const SPECTRAL_MODES");
        const std::string js = (jsAt == std::string::npos) ? std::string() : htm.substr (jsAt, 400);
        for (const char* n : names) { if (cpp.find (std::string ("\"") + n + "\"") != std::string::npos) ++inCpp;
                                      if (js.find (std::string ("'") + n + "'") != std::string::npos) ++inJs; }
        char b[160];
        snprintf (b, sizeof b, "%d/8 in the C++ StringArray, %d/8 in SPECTRAL_MODES", inCpp, inJs);
        gate (inCpp == 8 && inJs == 8, "N1  every SpectralMode name is authored in BOTH lists", b);

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

        // N5 — the window's RANGE is authored twice: once as a NormalisableRange in
        // createParameterLayout and once as three constants in fmtSynReadout, which has to undo the
        // same skew to print a harmonic number from a normalised knob. If they drift, the readout
        // says one harmonic and the DSP uses another — and both look entirely plausible.
        // read the number that FOLLOWS the key — strlen, not a hand-counted offset. The first
        // version of this gate used magic skips, mis-counted one by a character, and reported the
        // C++ low edge as 0. A gate that fails for its own reason is noise; it just happened to
        // fail loudly rather than pass quietly.
        auto num = [&] (const std::string& hay, const char* key) -> double {
            const size_t k = hay.find (key); if (k == std::string::npos) return -1.0;
            return std::atof (hay.c_str() + k + std::strlen (key)); };
        const double cLo = num (cpp, "NormalisableRange<float> rLo (");
        const double cHi = num (cpp, "NormalisableRange<float> rHi (1.0f, ");
        const double cCe = num (cpp, "rLo.setSkewForCentre (");
        const double jLo = num (htm, "const SPECWIN_LO = ");
        const double jHi = num (htm, "SPECWIN_HI = ");
        const double jCe = num (htm, "SPECWIN_CENTRE = ");
        snprintf (b, sizeof b, "C++ (%.0f..%.0f, centre %.0f)  JS (%.0f..%.0f, centre %.0f)", cLo, cHi, cCe, jLo, jHi, jCe);
        gate (cLo > 0 && jLo > 0 && cLo == jLo && cHi == jHi && cCe == jCe,
              "N5  the window's range+skew agrees across the DSP and the readout", b);
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

    // ── W — PARTIAL RANGE ─────────────────────────────────────────────────────────────────────
    {
        const tw::WavetableSpec base = tw::WavetableBank::specForPreset (1);
        const int kMaxH = tw::FrameSpec::kMaxHarmonics;

        // W1 the DEFAULT window is an exact identity — no existing preset may move by one bit.
        {
            bool allSame = true;
            for (int m = 1; m < (int) tw::SpectralMode::Count; ++m)
                for (float a : { 0.001f, 0.25f, 0.5f, 1.0f })
                    for (int pr : kHarmonicPresets)
                    { const tw::WavetableSpec s = tw::WavetableBank::specForPreset (pr);
                      allSame &= sameSpec (tw::SpectralMorph::apply (s, (tw::SpectralMode) m, a),
                                           tw::SpectralMorph::apply (s, (tw::SpectralMode) m, a, 1.0f, (float) kMaxH)); }
            gate (allSame, "W1  Lo=1 Hi=max is BIT-IDENTICAL to the unwindowed call", "8 modes x 4 amounts x 7 tables");
        }

        // W2 a HIGH edge really protects what is above it
        {
            const DryList dry = dryPartials (base.frames[8]);
            const tw::WavetableSpec wet = tw::SpectralMorph::apply (base, tw::SpectralMode::HarmonicStretch, 1.0f);
            const tw::WavetableSpec win = tw::SpectralMorph::apply (base, tw::SpectralMode::HarmonicStretch, 1.0f, 1.0f, 32.0f);
            int above = 0, moved = 0, below = 0, belowFull = 0;
            for (int i = 0; i < win.frames[8].numPartials && i < (int) dry.p.size(); ++i)
            {
                const float r = dry.p[(size_t) i].ratio;
                if (r > 35.0f) { ++above; if (win.frames[8].partials[(size_t) i].ratio != r) ++moved; }
                if (r < 30.0f && r >= 1.0f) { ++below;
                    if (std::abs (win.frames[8].partials[(size_t) i].ratio - wet.frames[8].partials[(size_t) i].ratio) < 1e-4f) ++belowFull; }
            }
            char b[200]; snprintf (b, sizeof b, "%d partials above the edge, %d moved; %d below, %d fully morphed", above, moved, below, belowFull);
            gate (above > 4 && moved == 0 && below > 4 && belowFull == below,
                  "W2  Hi=32 leaves r>35 untouched and morphs r<30 in full", b);
        }

        // W3 a LOW edge really protects what is below it
        {
            const DryList dry = dryPartials (base.frames[8]);
            const tw::WavetableSpec win = tw::SpectralMorph::apply (base, tw::SpectralMode::RandomAmplitudes, 1.0f, 64.0f, (float) kMaxH);
            int below = 0, moved = 0;
            for (int i = 0; i < win.frames[8].numPartials && i < (int) dry.p.size(); ++i)
            { const float r = dry.p[(size_t) i].ratio;
              if (r < 62.0f) { ++below; if (win.frames[8].partials[(size_t) i].amp != dry.p[(size_t) i].amp) ++moved; } }
            char b[160]; snprintf (b, sizeof b, "%d partials below the edge, %d moved", below, moved);
            gate (below > 8 && moved == 0, "W3  Lo=64 leaves r<62 untouched", b);
        }

        // W4 THE PHASOR TRAP. Disperse changes phase and NOTHING else, so a windowed Disperse must
        //    leave every amplitude exactly alone at EVERY window weight. Lerping the phasor instead
        //    of the angle is a comb filter with an infinite null at w = 0.5 — a partial cancelling
        //    against a rotated copy of itself. This gate is the only thing that can see it.
        {
            double worst = 0.0; int n = 0;
            for (float hiEdge : { 24.0f, 48.0f, 96.0f, 192.0f })
            {
                const DryList dry = dryPartials (base.frames[8]);
                const tw::WavetableSpec win = tw::SpectralMorph::apply (base, DISP, 1.0f, 1.0f, hiEdge);
                for (int i = 0; i < win.frames[8].numPartials && i < (int) dry.p.size(); ++i)
                { const double a0 = dry.p[(size_t) i].amp, a1 = win.frames[8].partials[(size_t) i].amp;
                  if (a0 <= 0.0) continue;
                  worst = std::max (worst, std::abs (a1 - a0) / a0); ++n; }
            }
            char b[160]; snprintf (b, sizeof b, "worst amplitude drift %.3g over %d partials x 4 edges", worst, n);
            gate (worst < 1e-6 && n > 200, "W4  a windowed PHASE-ONLY mode never touches an amplitude", b);
        }

        // W5 the window is CONTINUOUS across its edge (no click when Lo/Hi is modulated)
        {
            float prev = -1.0f; bool smooth = true; float worstJump = 0.0f;
            for (int k = 0; k <= 200; ++k)
            {
                const float hiEdge = 20.0f + (float) k * 0.2f;                 // sweep the edge across a partial
                const tw::WavetableSpec win = tw::SpectralMorph::apply (base, tw::SpectralMode::HarmonicStretch, 1.0f, 1.0f, hiEdge);
                float acc = 0.0f; for (int i = 0; i < win.frames[8].numPartials; ++i) acc += std::abs (win.frames[8].partials[(size_t) i].amp);
                if (prev >= 0.0f) { const float j = std::abs (acc - prev); worstJump = std::max (worstJump, j); if (j > 0.02f * std::max (1.0f, prev)) smooth = false; }
                prev = acc;
            }
            char b[160]; snprintf (b, sizeof b, "worst step over a 0.2-partial edge move: %.4f", worstJump);
            gate (smooth, "W5  sweeping the Hi edge is continuous (modulatable without clicks)", b);
        }
    }

    printf ("\n  PASS %d   FAIL %d\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
