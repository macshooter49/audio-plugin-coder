// ─────────────────────────────────────────────────────────────────────────────
// bod_cert — the certification harness for the BODE frequency shifter (kind 13).
//   clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/bod_cert.cpp -o bod_cert
//
// THE ORDER OF THESE GATES IS THE POINT.
//   §A runs FIRST and it is DIRECTION. A frequency shifter with the sideband
//   inverted is a working frequency shifter going the other way: it passes every
//   level gate, every click gate, every spectrum-changed gate, and it is not the
//   instrument that was asked for. The build bible's own first draft had this
//   formula swapped and only a time-domain probe caught it (fb444 recon). So the
//   very first number this harness prints is WHERE THE ENERGY WENT.
//
//   §B is RANGE, for the same reason: the engine this device grew out of caps at
//   +-1000 Hz behind a clamp that reads +-2000, and every knob position still
//   "does something" at a fifth of the asked-for range.
//
// fb441 — CERT MUST SEED BEFORE IT MEASURES. A fresh engine snaps to its first
// block's parameters and hides every steady-state bug; each measurement below
// runs the engine for a seed interval FIRST and only then captures.
// ─────────────────────────────────────────────────────────────────────────────
#include "TerrainBodeFx.h"
#include <cstdio>
#include <cstring>
#include <complex>
#include <string>
#include <vector>
#include <cmath>

static int gPass = 0, gFail = 0;
static void gate (const char* what, bool ok, const std::string& d)
{
    if (ok) { ++gPass; std::printf ("  ok    %-56s %s\n", what, d.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-56s %s\n", what, d.c_str()); }
}

// ── a small radix-2 FFT ──────────────────────────────────────────────────────
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
        const double ang = -2.0 * M_PI / (double) len;
        std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

static constexpr int   NFFT = 32768;
static constexpr float FS   = 48000.0f;
static constexpr float BUS  = 0.05f;      // the rack bus sits near -26 dBFS

struct Spec { std::vector<double> mag; double binHz; };

// Run the engine and return the magnitude spectrum of its output.
// seedSec: how long to run BEFORE capturing (fb441 — never measure a cold engine).
static Spec runTone (tw::TerrainBodeFx& e, float toneHz, float amp,
                     double seedSec = 0.35, int chan = 0)
{
    const int seedN = (int) (seedSec * FS);
    float l, r;
    double ph = 0.0;
    const double inc = 2.0 * M_PI * (double) toneHz / FS;
    for (int i = 0; i < seedN; ++i)
    {
        const float s = amp * (float) std::sin (ph); ph += inc;
        e.processStereo (s, s, l, r);
    }
    std::vector<std::complex<double>> buf ((size_t) NFFT);
    for (int i = 0; i < NFFT; ++i)
    {
        const float s = amp * (float) std::sin (ph); ph += inc;
        e.processStereo (s, s, l, r);
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1));   // Hann
        buf[(size_t) i] = (chan == 0 ? l : r) * w;
    }
    fft (buf);
    Spec sp; sp.binHz = (double) FS / NFFT;
    sp.mag.resize ((size_t) NFFT / 2);
    for (int i = 0; i < NFFT / 2; ++i) sp.mag[(size_t) i] = std::abs (buf[(size_t) i]);
    return sp;
}

static double energyNear (const Spec& s, double hz, double widthHz = 25.0)
{
    const int lo = (int) std::max (0.0, (hz - widthHz) / s.binHz);
    const int hi = (int) std::min ((double) s.mag.size() - 1, (hz + widthHz) / s.binHz);
    double e = 0.0;
    for (int i = lo; i <= hi; ++i) e += s.mag[(size_t) i] * s.mag[(size_t) i];
    return e;
}
static double peakHz (const Spec& s)
{
    size_t best = 1; double bv = 0.0;
    for (size_t i = 1; i < s.mag.size(); ++i) if (s.mag[i] > bv) { bv = s.mag[i]; best = i; }
    return (double) best * s.binHz;
}
static double db (double x) { return 20.0 * std::log10 (std::max (1e-12, x)); }

static tw::TerrainBodeFx::Params base()
{
    tw::TerrainBodeFx::Params p;      // defaults: no shift, no feedback, full wet
    p.shift = 0.5f; p.dir = 1.0f; p.fdbk = 0.0f; p.mix = 1.0f;
    p.fine = 0.5f; p.spread = 1.0f; p.time = 0.45f; p.blur = 0.0f;
    p.lowKeep = 0.0f; p.damping = 1.0f; p.touch = 0.5f; p.drift = 0.0f;
    return p;
}
// Turn a wanted shift in Hz into the knob position that produces it.
static float knobForHz (float hz)
{
    const float v = (hz < 0.0f ? -1.0f : 1.0f)
                  * std::log (std::fabs (hz) + 1.0f) / std::log (tw::TerrainBodeFx::kShiftMax + 1.0f);
    return 0.5f * (v + 1.0f);
}

int main()
{
    std::printf ("\n══ bod_cert — BODE frequency shifter (kind 13) ══\n");

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§A — DIRECTION. Run first. A flipped sideband is a different instrument.\n");
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = knobForHz (100.0f); p.dir = 1.0f;   // UP
        e.setParams (p);
        auto s = runTone (e, 1000.0f, BUS);
        const double up = energyNear (s, 1100.0), dn = energyNear (s, 900.0);
        char d[192];
        std::snprintf (d, sizeof d, "1 kHz + 100 Hz, dir=up -> peak %.0f Hz  ·  1100 is %.1f dB over 900",
                       peakHz (s), db (std::sqrt (up)) - db (std::sqrt (dn)));
        gate ("dir=UP puts a 1 kHz tone at 1100 Hz, NOT 900", up > dn * 100.0, d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = knobForHz (100.0f); p.dir = 0.0f;   // DOWN
        e.setParams (p);
        auto s = runTone (e, 1000.0f, BUS);
        const double up = energyNear (s, 1100.0), dn = energyNear (s, 900.0);
        char d[192];
        std::snprintf (d, sizeof d, "dir=down -> peak %.0f Hz  ·  900 is %.1f dB over 1100",
                       peakHz (s), db (std::sqrt (dn)) - db (std::sqrt (up)));
        gate ("dir=DOWN mirrors it to 900 Hz", dn > up * 100.0, d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = knobForHz (100.0f); p.dir = 0.5f;   // RING
        e.setParams (p);
        auto s = runTone (e, 1000.0f, BUS);
        const double up = energyNear (s, 1100.0), dn = energyNear (s, 900.0);
        const double car = energyNear (s, 1000.0);
        char d[192];
        std::snprintf (d, sizeof d, "both sidebands within %.1f dB, carrier suppressed %.1f dB",
                       std::fabs (db (std::sqrt (up)) - db (std::sqrt (dn))),
                       db (std::sqrt (car)) - db (std::sqrt (up)));
        gate ("dir=CENTRE is ring mod: BOTH sidebands, no carrier",
              std::fabs (db (std::sqrt (up)) - db (std::sqrt (dn))) < 1.5
              && car < up * 0.25, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§B — RANGE. The engine this grew from caps at 1000 Hz behind a clamp reading 2000.\n");
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = 1.0f; p.dir = 1.0f; p.fine = 0.5f;
        e.setParams (p);
        auto s = runTone (e, 1000.0f, BUS);
        const double pk = peakHz (s);
        char d[192];
        std::snprintf (d, sizeof d, "knob at 100%%: 1 kHz -> %.0f Hz (want 6000)  ·  engine meter says %+.1f Hz",
                       pk, e.meterShiftHz());
        gate ("full travel really is +5000 Hz (not the inherited 1000)",
              pk > 5900.0 && pk < 6100.0, d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = 0.0f; e.setParams (p);
        auto s = runTone (e, 6000.0f, BUS);
        const double pk = peakHz (s);
        char d[192];
        std::snprintf (d, sizeof d, "knob at 0%%: 6 kHz -> %.0f Hz (want 1000)  ·  meter %+.1f Hz",
                       pk, e.meterShiftHz());
        gate ("and -5000 Hz the other way", pk > 940.0 && pk < 1060.0, d);
    }
    {
        // The negative control: this is the gate that would have caught a
        // verbatim lift of the old mapping.
        const float knob = 1.0f;
        const float vOld = 2.0f * knob - 1.0f;
        const float oldHz = std::pow (1001.0f, std::fabs (vOld)) - 1.0f;
        char d[192];
        std::snprintf (d, sizeof d, "the OLD FMAX=1000 mapping reaches only %.0f Hz at full travel", oldHz);
        gate ("... and the gate FAILS on the inherited mapping (negative control)",
              oldHz < 5900.0f, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§C — NOTHING FREE-RUNS. Feedback maxed, input removed, the tail must die.\n");
    for (int chr = 0; chr < tw::TerrainBodeFx::kNumChars; ++chr)
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.fdbk = 1.0f; p.chr = chr; p.shift = knobForHz (37.0f);
        p.time = 0.35f; p.damping = 1.0f;
        e.setParams (p);
        float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 220.0 / FS;
        for (int i = 0; i < (int) (0.5 * FS); ++i)
        { const float s = BUS * (float) std::sin (ph); ph += inc; e.processStereo (s, s, l, r); }
        double pk = 0.0;
        for (int i = 0; i < (int) (1.0 * FS); ++i)
        { e.processStereo (0.0f, 0.0f, l, r);
          if (i > (int) (0.6 * FS)) pk = std::max (pk, (double) std::max (std::fabs (l), std::fabs (r))); }
        char d[160]; std::snprintf (d, sizeof d, "char %d: tail after 1 s of silence = %.1f dBFS", chr, db (pk));
        gate ("0.5 s note -> 1 s silence, tail below -70 dBFS", pk < 3.16e-4, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§D — LOOP STABILITY. A shifter in a loop walks its content out of band.\n");
    {
        bool bad = false; double worst = 0.0; int badT = -1, badC = -1;
        for (int t = 0; t < tw::TerrainBodeFx::kNumTypes && ! bad; ++t)
            for (int c = 0; c < tw::TerrainBodeFx::kNumChars && ! bad; ++c)
            {
                tw::TerrainBodeFx e; e.prepare (FS);
                auto p = base(); p.type = t; p.chr = c; p.fdbk = 1.0f;
                p.shift = knobForHz (211.0f); p.time = 0.30f; p.blur = 0.7f;
                e.setParams (p);
                float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 110.0 / FS;
                for (int i = 0; i < (int) (12.0 * FS); ++i)
                {
                    const float s = BUS * (float) std::sin (ph); ph += inc;
                    e.processStereo (s, s, l, r);
                    const double m = std::max (std::fabs (l), std::fabs (r));
                    if (! std::isfinite (m) || m > 12.0) { bad = true; badT = t; badC = c; }
                    worst = std::max (worst, m);
                }
            }
        char d[176];
        std::snprintf (d, sizeof d, "12 s at max feedback x %d types x %d chars: worst peak %.2f%s",
                       tw::TerrainBodeFx::kNumTypes, tw::TerrainBodeFx::kNumChars, worst,
                       bad ? "  <- DIVERGED" : "");
        gate ("never diverges, never NaNs, on any Type x Character", ! bad, d);
        (void) badT; (void) badC;
    }
    {
        // The in-loop makeup must have unit slope at zero, or Character is a
        // second, secret feedback control.
        double worst = 0.0;
        for (int c = 0; c < tw::TerrainBodeFx::kNumChars; ++c)
        {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.chr = c; p.fdbk = 0.0f; p.shift = 0.5f; p.mix = 1.0f;
            e.setParams (p);
            float l, r; double sum = 0.0; int n = 0;
            for (int i = 0; i < (int) (0.4 * FS); ++i)
            {
                const float s = 1e-3f * (float) std::sin (2.0 * M_PI * 300.0 * i / FS);
                e.processStereo (s, s, l, r);
                if (i > (int) (0.2 * FS)) { sum += std::fabs (l); ++n; }
            }
            const double g = (sum / std::max (1, n)) / (1e-3 * 0.6366);
            worst = std::max (worst, std::fabs (db (g)));
        }
        char d[160];
        std::snprintf (d, sizeof d, "worst small-signal gain deviation across Characters: %.2f dB", worst);
        gate ("Character has unit slope at zero (it cannot move loop gain)", worst < 1.5, d);
    }

    {
        // fb444 — THE CLIP GATE, CORRECTED. The first version of this section
        // asked "does it diverge", and mutation testing proved that question can
        // never fail: max loop gain is 0.95, Blur is allpass (unity) and Damping
        // is cut-only, so the loop is provably BIBO-stable with NO clip at all.
        // Deleting the soft clip turned zero gates red -- the gate was blind
        // because it was gating the wrong property. What the in-loop clip really
        // buys is a BOUNDED OUTPUT when the input is hot: 1/(1-0.95) = 20x, and
        // 20x of a hot signal is +26 dBFS out of a rack device. So that is what
        // is gated now, at an input level the bus can actually see on a peak.
        // fb425 — SWEEP THE MATRIX, DO NOT SAMPLE IT. The first version of this
        // gate pinned Blur at 0.4 and mutation testing still came back 0 RED. The
        // reason was the gate, not the guard: diffusion spreads the loop's energy
        // in TIME, so at blur 0.4 an unclipped loop peaks at 1.95 and slips under
        // a 2.0 bar -- while the SAME loop at blur 0 reaches 9.4. One arbitrary
        // knob position hid a 14 dB defect. The matrix is finite; sweep it.
        double worst = 0.0; bool bad = false;
        float wBlur = 0.0f, wTime = 0.0f; int wChr = 0;
        for (int c = 0; c < tw::TerrainBodeFx::kNumChars && ! bad; ++c)
        for (float bl : { 0.0f, 0.5f, 1.0f })
        for (float tm : { 0.20f, 0.45f, 0.70f })
        {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.chr = c; p.fdbk = 1.0f; p.shift = knobForHz (23.0f);
            p.time = tm; p.blur = bl;
            e.setParams (p);
            float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 90.0 / FS;
            for (int i = 0; i < (int) (5.0 * FS); ++i)
            {
                const float sIn = 0.7f * (float) std::sin (ph); ph += inc;   // HOT
                e.processStereo (sIn, sIn, l, r);
                const double m = std::max (std::fabs (l), std::fabs (r));
                if (! std::isfinite (m)) bad = true;
                if (m > worst) { worst = m; wBlur = bl; wTime = tm; wChr = c; }
            }
        }
        char d[200];
        std::snprintf (d, sizeof d,
                       "72 cells swept · worst %.2f (%.1f dBFS) at blur %.1f / time %.2f / char %d",
                       worst, db (worst), wBlur, wTime, wChr);
        gate ("the in-loop clip keeps a HOT input bounded (not just stable)",
              ! bad && worst < 2.0, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§E — THE MIX LAW, THE STEREO MIRROR, AND LOW KEEP.\n");
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.mix = 0.0f; p.shift = knobForHz (300.0f); e.setParams (p);
        float l, r; double worst = 0.0;
        for (int i = 0; i < (int) (0.5 * FS); ++i)
        {
            const float s = BUS * (float) std::sin (2.0 * M_PI * 440.0 * i / FS);
            e.processStereo (s, s, l, r);
            if (i > (int) (0.3 * FS)) worst = std::max (worst, (double) std::fabs (l - s));
        }
        char d[160]; std::snprintf (d, sizeof d, "worst |out - in| at Mix 0 = %.2e", worst);
        gate ("Mix 0 passes the dry through untouched", worst < 1e-6, d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = knobForHz (150.0f); p.dir = 1.0f; p.spread = 1.0f;
        e.setParams (p);
        auto sL = runTone (e, 1000.0f, BUS, 0.35, 0);
        tw::TerrainBodeFx e2; e2.prepare (FS); e2.setParams (p);
        auto sR = runTone (e2, 1000.0f, BUS, 0.35, 1);
        const double pl = peakHz (sL), pr = peakHz (sR);
        char d[176];
        std::snprintf (d, sizeof d, "L lands %.0f Hz, R lands %.0f Hz (mirror: one up, one down)", pl, pr);
        gate ("full Spread mirrors the shift: L and R go OPPOSITE ways",
              (pl > 1100.0 && pr < 900.0) || (pl < 900.0 && pr > 1100.0), d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.spread = 0.5f; p.shift = knobForHz (150.0f); e.setParams (p);
        auto sL = runTone (e, 1000.0f, BUS, 0.35, 0);
        tw::TerrainBodeFx e2; e2.prepare (FS); e2.setParams (p);
        auto sR = runTone (e2, 1000.0f, BUS, 0.35, 1);
        char d[176];
        std::snprintf (d, sizeof d, "L %.0f Hz · R %.0f Hz (R should sit near the unshifted tone)",
                       peakHz (sL), peakHz (sR));
        gate ("Spread at centre leaves R unshifted (the axis really is continuous)",
              std::fabs (peakHz (sR) - 1000.0) < 60.0, d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = knobForHz (400.0f); p.lowKeep = 0.5f; e.setParams (p);
        auto s = runTone (e, 60.0f, BUS);
        char d[176];
        std::snprintf (d, sizeof d, "60 Hz tone with Low Keep at 200 Hz -> peak stays at %.0f Hz", peakHz (s));
        gate ("Low Keep really does bypass the shifter below its corner",
              std::fabs (peakHz (s) - 60.0) < 25.0, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§F — EVERY KNOB KEEPS MOVING (no plateaus, no dead travel).\n");
    {
        // fb444 — THE METRIC, NOT THE BAR. The first version of this section
        // summed |output| and declared Blur and Damping dead. Both were fine:
        //   · Blur is a chain of ALLPASSES, which preserve energy by construction.
        //     A gross-energy metric is blind to them however much they smear.
        //   · Damping is a 700 Hz..40 kHz in-loop low-pass and the probe tone was
        //     330 Hz -- the whole knob sat above the only content present.
        // So: excite with BROADBAND noise (something for every knob to act on) and
        // compare MAGNITUDE SPECTRA between settings. That is what an ear does.
        auto fingerprint = [] (tw::TerrainBodeFx& e) -> std::vector<double>
        {
            float l, r; uint32_t rng = 2463534242u;
            for (int i = 0; i < (int) (0.45 * FS); ++i)          // seed to steady state
            {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                const float n = BUS * (((float) (rng & 0xFFFFu) / 32768.0f) - 1.0f);
                e.processStereo (n, n, l, r);
            }
            std::vector<std::complex<double>> buf ((size_t) NFFT);
            for (int i = 0; i < NFFT; ++i)                       // keep exciting: the
            {                                                    // in-loop parts only
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;   // shape a LIVE loop
                const float n = BUS * (((float) (rng & 0xFFFFu) / 32768.0f) - 1.0f);
                e.processStereo (n, n, l, r);
                const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1));
                buf[(size_t) i] = l * w;
            }
            fft (buf);
            std::vector<double> out ((size_t) (NFFT / 2));
            for (int i = 0; i < NFFT / 2; ++i) out[(size_t) i] = db (std::abs (buf[(size_t) i]));
            return out;
        };
        auto spectralDist = [] (const std::vector<double>& a, const std::vector<double>& b)
        {
            double acc = 0.0; int n = 0;
            const int lo = (int) (40.0 / ((double) FS / NFFT));
            const int hi = (int) (16000.0 / ((double) FS / NFFT));
            for (int i = lo; i < hi && i < (int) a.size(); ++i)
            { acc += std::fabs (a[(size_t) i] - b[(size_t) i]); ++n; }
            return acc / std::max (1, n);
        };

        struct K { const char* nm; float tw::TerrainBodeFx::Params::* fld; };
        const K ks[] = { { "Shift",     &tw::TerrainBodeFx::Params::shift },
                         { "Direction", &tw::TerrainBodeFx::Params::dir },
                         { "Fdbk",      &tw::TerrainBodeFx::Params::fdbk },
                         { "Time",      &tw::TerrainBodeFx::Params::time },
                         { "Blur",      &tw::TerrainBodeFx::Params::blur },
                         { "Damping",   &tw::TerrainBodeFx::Params::damping },
                         { "Drift",     &tw::TerrainBodeFx::Params::drift } };
        for (const auto& k : ks)
        {
            std::vector<double> prev; double minStep = 1e9;
            for (int i = 0; i <= 6; ++i)
            {
                tw::TerrainBodeFx e; e.prepare (FS);
                auto p = base();
                p.fdbk = 0.60f; p.shift = knobForHz (40.0f); p.time = 0.45f;
                p.*(k.fld) = (float) i / 6.0f;
                e.setParams (p);
                auto f = fingerprint (e);
                if (! prev.empty()) minStep = std::min (minStep, spectralDist (prev, f));
                prev = f;
            }
            char d[176];
            std::snprintf (d, sizeof d, "%-9s smallest spectral step across 6 = %.3f dB", k.nm, minStep);
            gate ((std::string (k.nm) + " transforms across its whole travel").c_str(),
                  minStep > 0.25, d);
        }
    }

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
