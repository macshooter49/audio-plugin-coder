// ════════════════════════════════════════════════════════════════════════════
//  ModalEngine offline proof (Pattern A — NOT in CMakeLists)
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Source/ModalEngine_test.cpp -o /tmp/md && /tmp/md
//
//  Proves the physical models actually behave like instruments:
//   • every family produces bounded, finite, non-silent audio
//   • strings ring at the played pitch (zero-crossing rate ≈ f0)
//   • percussive families decay; sustained (bow/wind) hold
//   • each of the 10 knobs measurably changes the sound (none dead)
//   • the voice frees itself once the tail is inaudible (CPU healer)
// ════════════════════════════════════════════════════════════════════════════
#include "ModalEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

using namespace tw;

static const double SR = 48000.0;
static int gPass = 0, gFail = 0;
static void check (bool ok, const std::string& name)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", name.c_str());
    if (ok) ++gPass; else ++gFail;
}

struct Stats { double rms=0, peak=0; bool finite=true; int zc=0; double centroid=0; double pitch=0; };

// autocorrelation pitch estimate (robust for rich harmonic tones, unlike zero-crossings)
static double acfPitch (const std::vector<float>& x, double sr, double fmin, double fmax)
{
    if (x.size() < 64) return 0.0;
    const int lagMin = std::max (2, (int) (sr / fmax)), lagMax = (int) (sr / fmin);
    double best = -1e18; int bl = lagMin;
    for (int lag = lagMin; lag <= lagMax && (size_t) lag < x.size(); ++lag)
    {
        double s = 0; for (size_t i = lag; i < x.size(); ++i) s += (double) x[i] * x[i - lag];
        if (s > best) { best = s; bl = lag; }
    }
    return sr / bl;
}

// Goertzel magnitude at frequency f over x (narrow-band energy probe for inharmonicity tests)
static double goertzel (const std::vector<float>& x, double f, double sr)
{
    const double w = 2.0 * M_PI * f / sr, coeff = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (float v : x) { const double s0 = (double) v + coeff * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt (std::max (0.0, s1*s1 + s2*s2 - coeff*s1*s2));
}

// actual measured frequency of partial n: scan Goertzel around n·f0 and return the peak bin
static double partialFreq (const std::vector<float>& x, double f0, int n, double sr)
{
    // scan only within ±half the spacing to the next harmonic so we never lock onto a NEIGHBOUR partial
    const double lo = n*f0*0.985, hi = n*f0*(1.0 + 0.45/(double)n);   // upper bound < (n+1)/n
    double bestF = lo, bestM = -1;
    for (double f = lo; f <= hi; f += 0.5) { const double m = goertzel (x, f, sr); if (m > bestM) { bestM = m; bestF = f; } }
    return bestF;
}

// render `secs` seconds; measure into a window [t0,t1] fraction of the run
static Stats run (ModalParams p, double hz, double secs, double t0=0.0, double t1=1.0,
                  bool releaseAt=false, double relFrac=0.5)
{
    ModalEngine e;
    e.prepare (SR, true);
    e.setParams (p);
    e.setPitchRatio (1.0);
    e.setPlayedHz (hz);
    e.noteOn (hz, 0x12345u, 0.9f);

    const int total = (int) (secs * SR);
    const int block = 128;
    const int a0 = (int) (t0 * total), a1 = (int) (t1 * total);
    const int relSamp = (int) (relFrac * total);

    Stats s; double e2 = 0; int cnt = 0; float prev = 0.f;
    std::vector<float> L(block), R(block), cap;
    int pos = 0;
    while (pos < total)
    {
        if (releaseAt && pos >= relSamp && pos - block < relSamp) e.noteOff();
        std::fill (L.begin(), L.end(), 0.f);
        std::fill (R.begin(), R.end(), 0.f);
        e.renderBlockAdd (L.data(), R.data(), block);
        for (int i = 0; i < block; ++i)
        {
            const float v = L[i];
            if (! std::isfinite (v)) s.finite = false;
            const int gp = pos + i;
            if (gp >= a0 && gp < a1)
            {
                e2 += (double) v * v; ++cnt;
                if (std::fabs (v) > s.peak) s.peak = std::fabs (v);
                if ((prev <= 0.f && v > 0.f)) ++s.zc;
                prev = v;
                cap.push_back (v);
            }
        }
        pos += block;
    }
    s.rms = cnt ? std::sqrt (e2 / cnt) : 0.0;
    const double winSecs = (double) (a1 - a0) / SR;
    s.centroid = winSecs > 0 ? (double) s.zc / winSecs : 0.0;   // ZC rate (HF proxy)
    s.pitch = acfPitch (cap, SR, hz * 0.45, hz * 2.2);          // true fundamental
    return s;
}

// render and return the raw mono capture over [t0,t1] (for spectral/inharmonicity probes)
static std::vector<float> capture (ModalParams p, double hz, double secs, double t0, double t1)
{
    ModalEngine e; e.prepare (SR, true); e.setParams (p); e.setPitchRatio (1.0);
    e.setPlayedHz (hz); e.noteOn (hz, 0x12345u, 0.9f);
    const int total = (int)(secs*SR), block = 128, a0 = (int)(t0*total), a1 = (int)(t1*total);
    std::vector<float> L(block), R(block), cap; int pos = 0;
    while (pos < total) { std::fill (L.begin(),L.end(),0.f); std::fill (R.begin(),R.end(),0.f);
        e.renderBlockAdd (L.data(), R.data(), block);
        for (int i=0;i<block;++i){ const int gp=pos+i; if (gp>=a0 && gp<a1) cap.push_back (L[i]); } pos += block; }
    return cap;
}

// peak |output| over `secs` for a given note + velocity (clipping probe)
static double peakOf (ModalParams p, double hz, float vel, double secs = 1.2)
{
    ModalEngine e; e.prepare (SR, true); e.setParams (p); e.setPitchRatio (1.0);
    e.setPlayedHz (hz); e.noteOn (hz, 0x1234u, vel);
    const int total = (int)(secs*SR), block = 128; std::vector<float> L(block), R(block); double pk = 0; int pos = 0;
    while (pos < total) { std::fill (L.begin(),L.end(),0.f); std::fill (R.begin(),R.end(),0.f);
        e.renderBlockAdd (L.data(), R.data(), block);
        for (int i=0;i<block;++i){ const double a = std::fabs (L[i]); if (a>pk) pk=a; } pos += block; }
    return pk;
}

int main()
{
    std::printf ("═══ ModalEngine proof ═══\n");
    const char* fam[9] = { "GRAND","PLUCK","BOW","FLUTE","REED","BRASS","BARS","BELLS","SKIN" };

    // ── 1. every family: bounded, finite, audible ──
    std::printf ("\n1. all families sound (bounded/finite/non-silent):\n");
    for (int f = 0; f < 9; ++f)
    {
        ModalParams p; p.family = f; p.form = 0; p.decay = 0.6f; p.breath = (f>=2&&f<=5)?0.6f:0.0f;
        const double hz = (f >= modal::BOW && f <= modal::BRASS) ? 220.0
                        : (f >= modal::BARS) ? 440.0 : 130.81;
        Stats s = run (p, hz, 1.2, 0.02, 0.5);
        const bool ok = s.finite && s.rms > 1e-4 && s.peak < 4.0;
        check (ok, std::string (fam[f]) + "  rms=" + std::to_string (s.rms).substr(0,6)
                   + " peak=" + std::to_string (s.peak).substr(0,5));
    }

    // ── 2. strings ring at the played pitch (ZC rate ≈ f0, within 6%) ──
    std::printf ("\n2. string pitch accuracy (a C is a C):\n");
    for (int f = 0; f <= modal::PLUCK; ++f)
        for (double hz : { 130.81, 261.63, 440.0 })
        {
            ModalParams p; p.family = f; p.decay = 0.7f; p.material = 0.6f; p.stretch = 0.5f; p.halo = 0.f;
            Stats s = run (p, hz, 0.6, 0.05, 0.45);
            const double err = std::fabs (s.pitch - hz) / hz;
            check (err < 0.03, std::string (fam[f]) + " " + std::to_string ((int) hz)
                   + "Hz → ACF≈" + std::to_string ((int) s.pitch) + "Hz (err "
                   + std::to_string (err*100).substr(0,4) + "%)");
        }

    // ── 3. DECAY knob: high decay → longer tail (late RMS bigger ratio) ──
    std::printf ("\n3. DECAY is night-and-day:\n");
    {
        ModalParams lo; lo.family = modal::BELLS; lo.decay = 0.2f;
        ModalParams hi; hi.family = modal::BELLS; hi.decay = 0.9f;
        Stats sLo = run (lo, 220.0, 3.0, 0.6, 1.0);      // measure the TAIL
        Stats sHi = run (hi, 220.0, 3.0, 0.6, 1.0);
        check (sHi.rms > sLo.rms * 4.0, "bell tail: decay0.9 rms=" + std::to_string(sHi.rms).substr(0,6)
               + " ≫ decay0.2 rms=" + std::to_string(sLo.rms).substr(0,6));
    }

    // ── 4. MATERIAL knob: metal (1.0) rings far longer than wood/felt (0.0) ──
    std::printf ("\n4. MATERIAL is night-and-day (metal rings, wood dies):\n");
    {
        ModalParams wood;  wood.family  = modal::BARS; wood.material = 0.05f; wood.decay = 0.7f;
        ModalParams metal; metal.family = modal::BARS; metal.material = 0.98f; metal.decay = 0.7f;
        Stats sW = run (wood,  330.0, 2.5, 0.5, 1.0);
        Stats sM = run (metal, 330.0, 2.5, 0.5, 1.0);
        check (sM.rms > sW.rms * 3.0, "bar tail: metal rms=" + std::to_string(sM.rms).substr(0,6)
               + " ≫ wood rms=" + std::to_string(sW.rms).substr(0,6));
    }

    // ── 5. STRETCH knob: harmonic vs inharmonic changes the mode-1 partner spacing ──
    std::printf ("\n5. STRETCH is night-and-day (inharmonicity morph):\n");
    {
        ModalParams a; a.family = modal::GRAND; a.stretch = 0.5f; a.decay = 0.7f;
        ModalParams b; b.family = modal::GRAND; b.stretch = 1.0f; b.decay = 0.7f;
        Stats sa = run (a, 65.4, 1.0, 0.1, 0.6);   // low note = audible dispersion
        Stats sb = run (b, 65.4, 1.0, 0.1, 0.6);
        // stretched strings sound brighter/detuned → measurable RMS or ZC difference
        const bool diff = std::fabs (sa.centroid - sb.centroid) > 2.0 || std::fabs (sa.rms - sb.rms) > 1e-4;
        check (diff && sb.finite, "grand stretch moves the spectrum (ZC " + std::to_string((int)sa.centroid)
               + "→" + std::to_string((int)sb.centroid) + ")");
    }

    // ── 6. BREATH knob: turns a struck bar into a sustained (bowed) tone ──
    std::printf ("\n6. BREATH sustains (plucked → bowed):\n");
    {
        ModalParams dry; dry.family = modal::BARS; dry.breath = 0.0f; dry.decay = 0.5f;
        ModalParams bow; bow.family = modal::BARS; bow.breath = 0.9f; bow.decay = 0.5f;
        Stats sd = run (dry, 330.0, 2.0, 0.7, 1.0);   // late window: dry has decayed, bowed still sings
        Stats sb = run (bow, 330.0, 2.0, 0.7, 1.0);
        check (sb.rms > sd.rms * 2.0, "bowed bar late rms=" + std::to_string(sb.rms).substr(0,6)
               + " ≫ struck rms=" + std::to_string(sd.rms).substr(0,6));
    }

    // ── 7. sustained families hold under drive, then release on note-off ──
    std::printf ("\n7. winds sustain then release:\n");
    {
        ModalParams p; p.family = modal::FLUTE; p.breath = 0.8f; p.decay = 0.5f;
        Stats sustain = run (p, 440.0, 1.5, 0.3, 0.45);              // mid-note, held
        Stats afterRel = run (p, 440.0, 1.5, 0.85, 1.0, true, 0.5);  // after note-off
        check (sustain.rms > 1e-3, "flute holds under breath (rms=" + std::to_string(sustain.rms).substr(0,6) + ")");
        check (afterRel.rms < sustain.rms, "flute releases after note-off");
    }

    // ── 8. voice frees itself (CPU healer) ──
    std::printf ("\n8. voice self-frees on silence:\n");
    {
        ModalEngine e; e.prepare (SR, true);
        ModalParams p; p.family = modal::SKIN; p.decay = 0.15f;
        e.setParams (p); e.setPlayedHz (110.0); e.noteOn (110.0, 7u, 0.9f);
        std::vector<float> L(128), R(128);
        int blocks = 0; bool freed = false;
        for (; blocks < (int)(SR/128*3); ++blocks)
        {
            std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            e.renderBlockAdd (L.data(), R.data(), 128);
            if (! e.activeForTesting()) { freed = true; break; }
        }
        check (freed, "drum voice freed after " + std::to_string (blocks*128*1000/(int)SR) + " ms");
    }

    // ── 9. all 45 instruments (9 families × 5 forms) render clean ──
    std::printf ("\n9. all 45 instruments render bounded+finite:\n");
    {
        int bad = 0;
        for (int f = 0; f < 9; ++f)
            for (int fm = 0; fm < 5; ++fm)
            {
                ModalParams p; p.family = f; p.form = fm;
                p.breath = (f>=2&&f<=5)?0.6f:0.0f; p.decay=0.6f;
                const double hz = (f>=2&&f<=5)?196.0:(f>=6?392.0:110.0);
                Stats s = run (p, hz, 0.8, 0.02, 0.5);
                if (! s.finite || s.peak > 4.5 || s.rms < 1e-5) { ++bad;
                    std::printf ("     weak/bad: %s form%d rms=%.5f peak=%.3f\n", fam[f], fm, s.rms, s.peak); }
            }
        check (bad == 0, std::to_string (45 - bad) + "/45 instruments clean");
    }

    // ── 10. sample-as-exciter: a dropped one-shot rings THROUGH the instrument ──
    std::printf ("\n10. sample-as-exciter (noise into guitars):\n");
    {
        // synthesize a short broadband "one-shot" (a filtered noise snap, 40ms)
        std::vector<float> snap ((size_t) (SR * 0.04));
        std::uint32_t r = 12345u; float z = 0.f;
        for (size_t i = 0; i < snap.size(); ++i)
        { r = r * 1664525u + 1013904223u; float n = (float) (r >> 8) * (1.f / 16777216.f) * 2.f - 1.f;
          z += 0.3f * (n - z); const float w = 1.f - (float) i / (float) snap.size();
          snap[i] = z * w * w; }
        // PLUCK family, SRC=Sample: the snap should excite the string and ring at the note pitch
        ModalEngine e; e.prepare (SR, true);
        ModalParams p; p.family = modal::PLUCK; p.source = 3; p.decay = 0.7f; p.material = 0.6f;
        e.setParams (p); e.setExciterSample (snap.data(), (int) snap.size(), SR);
        e.setPlayedHz (196.0); e.noteOn (196.0, 55u, 0.9f);
        std::vector<float> L(128), R(128), cap;
        for (int pos = 0; pos < (int) (SR * 0.6); pos += 128)
        { std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
          e.renderBlockAdd (L.data(), R.data(), 128);
          for (int i = 0; i < 128; ++i) if (pos + i > (int) (SR * 0.1)) cap.push_back (L[i]); }
        double e2 = 0; for (float v : cap) e2 += (double) v * v;
        const double rms = cap.size() ? std::sqrt (e2 / cap.size()) : 0.0;
        const double pitch = acfPitch (cap, SR, 196.0 * 0.45, 196.0 * 2.2);
        const double err = std::fabs (pitch - 196.0) / 196.0;
        check (rms > 1e-3 && err < 0.04, "snap sample → PLUCK rings at " + std::to_string ((int) pitch)
               + "Hz (target 196, rms=" + std::to_string (rms).substr(0,6) + ")");
    }

    // ── 11. exciter LOOP MODES: continuous excitation must SUSTAIN without runaway; follower valid ──
    std::printf ("\n11. exciter loop modes (sustain + follower, no runaway):\n");
    {
        std::vector<float> snap ((size_t) (SR * 0.05));   // 50ms broadband exciter
        std::uint32_t r = 999u; float z = 0.f;
        for (size_t i = 0; i < snap.size(); ++i)
        { r = r*1664525u+1013904223u; float n=(float)(r>>8)*(1.f/16777216.f)*2.f-1.f; z += 0.25f*(n-z); snap[i]=z; }
        const char* nm[4] = { "One-Shot", "Forward", "Reverse", "Ping-Pong" };
        for (int mode = 0; mode < 4; ++mode)
        {
            ModalEngine e; e.prepare (SR, true);
            ModalParams p; p.family = modal::BOW; p.source = 3; p.decay = 0.7f; p.breath = 0.5f; p.loopMode = mode;
            e.setParams (p); e.setExciterSample (snap.data(), (int) snap.size(), SR);
            e.setPlayedHz (220.0); e.noteOn (220.0, 60u, 0.9f);
            std::vector<float> L(128), R(128); double peak = 0; bool posOk = true, sawPos = false, finite = true;
            for (int pos = 0; pos < (int) (SR * 2.0); pos += 128)
            { std::fill (L.begin(),L.end(),0.f); std::fill (R.begin(),R.end(),0.f);
              e.renderBlockAdd (L.data(), R.data(), 128);
              for (int i=0;i<128;++i){ float v=L[i]; if(!std::isfinite(v)) finite=false; if(std::fabs(v)>peak) peak=std::fabs(v); }
              float fp = e.readPos01(); if (fp >= 0.f){ sawPos=true; if(fp<-0.001f||fp>1.001f) posOk=false; } }
            const bool ok = finite && peak < 8.0 && posOk && sawPos;   // bounded, no NaN, follower position valid
            check (ok, std::string(nm[mode]) + ": peak=" + std::to_string(peak).substr(0,5)
                       + (finite?"":" NONFINITE") + (posOk?"":" BADPOS") + (sawPos?"":" NOPOS"));
        }
    }

    // ── 12. Sample/Granular LOOP-CATCH: the follower must START at the sample beginning, play a
    //        forward LEAD-IN, and only once it reaches the purple box [0.30,0.60] loop THERE — Max's
    //        refinement ("start at the beginning THEN when it reaches the box, loop"). So: first
    //        readable pos ≈ 0 (NOT in the box), steady-state pos confined to [0.30,0.60]. ──
    std::printf ("\n12. loop-catch: lead-in from start → loop in box [0.30,0.60]:\n");
    {
        std::vector<float> snap ((size_t) (SR * 0.20));   // 200ms exciter so lead-in + sub-region are meaningful
        std::uint32_t r = 4242u; float z = 0.f;
        for (size_t i = 0; i < snap.size(); ++i)
        { r = r*1664525u+1013904223u; float n=(float)(r>>8)*(1.f/16777216.f)*2.f-1.f; z += 0.25f*(n-z); snap[i]=z; }
        const char* nm[3] = { "Forward", "Reverse", "Ping-Pong" };
        for (int mode = 1; mode <= 3; ++mode)
        {
            ModalEngine e; e.prepare (SR, true);
            ModalParams p; p.family = modal::BOW; p.source = 3; p.decay = 0.7f; p.breath = 0.5f;
            p.loopMode = mode; p.loopStart = 0.30f; p.loopEnd = 0.60f;
            e.setParams (p); e.setExciterSample (snap.data(), (int) snap.size(), SR);
            e.setPlayedHz (220.0); e.noteOn (220.0, 77u, 0.9f);
            float firstPos = -1.f, slo = 1.f, shi = 0.f; int nSteady = 0;
            const int total = (int) (SR * 1.2);
            for (int pos = 0; pos < total; pos += 64)
            { std::vector<float> L(64,0.f), R(64,0.f); e.renderBlockAdd (L.data(), R.data(), 64);
              float fp = e.readPos01(); if (fp < 0.f) continue;
              if (firstPos < 0.f) firstPos = fp;
              if (pos > (int) (SR * 0.5)) { ++nSteady; if (fp<slo) slo=fp; if (fp>shi) shi=fp; } }
            const bool leadIn = (firstPos >= 0.f && firstPos < 0.20f);                 // began at the START, not in the box
            const bool inBox  = (nSteady > 50) && (slo >= 0.30f - 0.02f) && (shi <= 0.60f + 0.02f);
            check (leadIn && inBox, std::string(nm[mode-1]) + ": start=" + std::to_string(firstPos).substr(0,4)
                       + " (want <0.20) steady=[" + std::to_string(slo).substr(0,4) + "," + std::to_string(shi).substr(0,4)
                       + "] (want in [0.30,0.60])" + (leadIn?"":" NO-LEADIN") + (inBox?"":" NOT-IN-BOX"));
        }
    }

    // ── 13. GRAND pitch LOCKED across STRETCH (a-C-is-a-C — Stretch no longer "detunes into a synth") ──
    std::printf ("\n13. GRAND pitch locked across STRETCH + register (anchoring fix):\n");
    for (double hz : { 130.81, 523.25 })                  // C3 and C5 (guards high-note dispersion-detune)
    {
        double pit[3]; const float st[3] = { 0.0f, 0.5f, 1.0f };
        for (int i = 0; i < 3; ++i)
        { ModalParams p; p.family = modal::GRAND; p.stretch = st[i]; p.decay = 0.7f; p.material = 0.5f;
          pit[i] = run (p, hz, 0.8, 0.05, 0.55).pitch; }
        double dmax = 0; for (int i = 0; i < 3; ++i) { double e = std::fabs (pit[i]-hz)/hz; if (e>dmax) dmax=e; }
        const double spread = std::fabs (pit[2]-pit[0]) / hz;
        check (dmax < 0.02 && spread < 0.012,
               std::to_string((int)hz) + "Hz stretch 0/0.5/1 = " + std::to_string((int)pit[0]) + "/"
               + std::to_string((int)pit[1]) + "/" + std::to_string((int)pit[2]) + "Hz (max err "
               + std::to_string(dmax*100).substr(0,4) + "%, spread " + std::to_string(spread*100).substr(0,4) + "%)");
    }

    // ── 14. GRAND felt hammer = a struck, pitched, bounded note (not a plucked/nylon noise-burst) ──
    std::printf ("\n14. GRAND felt hammer: struck, pitched, bounded:\n");
    {
        ModalParams p; p.family = modal::GRAND; p.stretch = 0.5f; p.decay = 0.62f; p.material = 0.5f; p.hard = 0.42f;
        Stats s = run (p, 130.81, 1.0, 0.02, 0.6);
        const double err = std::fabs (s.pitch - 130.81) / 130.81;
        const bool ok = s.finite && s.peak < 0.98 && s.rms > 1e-3 && err < 0.03;
        check (ok, "C3 struck: pitch≈" + std::to_string((int)s.pitch) + "Hz (err " + std::to_string(err*100).substr(0,4)
               + "%) peak=" + std::to_string(s.peak).substr(0,5) + " rms=" + std::to_string(s.rms).substr(0,6));
    }

    // ── 15. INHARMONICITY baked on at default + scales with STRETCH (the "it's a real piano, not a comb") ──
    std::printf ("\n15. GRAND inharmonicity present + scales with STRETCH:\n");
    {
        const double hz = 130.81;                         // C3
        ModalParams p0; p0.family = modal::GRAND; p0.stretch = 0.0f; p0.decay = 0.9f; p0.material = 0.6f;
        ModalParams p1; p1.family = modal::GRAND; p1.stretch = 1.0f; p1.decay = 0.9f; p1.material = 0.6f;
        auto cap0 = capture (p0, hz, 0.9, 0.06, 0.5);
        auto cap1 = capture (p1, hz, 0.9, 0.06, 0.5);
        // average sharpness over partials 4/6/8/10 = a robust "how inharmonic is it" score
        double s0 = 0, s1 = 0; const int P[4] = { 4, 6, 8, 10 };
        for (int n : P) { s0 += partialFreq (cap0, hz, n, SR)/(n*hz); s1 += partialFreq (cap1, hz, n, SR)/(n*hz); }
        s0 /= 4; s1 /= 4;
        // stretch 1.0 must be audibly inharmonic (partials sharp) AND clearly sharper than near-harmonic stretch 0
        const bool ok = (s1 > 1.006) && (s1 > s0 + 0.006);
        check (ok, "C3 mean partial sharpness (×harmonic): stretch0=" + std::to_string(s0).substr(0,6)
               + " stretch1=" + std::to_string(s1).substr(0,6));
    }

    // ── 16. GRAND stays CLEAN across EVERY knob extreme (no output-clip "synth distortion") — the worst
    //        case is max DECAY + max velocity + bright MATERIAL + POS comb, measured over a long ring so
    //        late resonant/beat crests are caught (that combo is what Max heard clip). ──
    std::printf ("\n16. GRAND clean at all knob extremes (no clipping):\n");
    {
        double worst = 0; std::string wcase;
        for (float hard : { 0.2f, 0.9f })
          for (float mat : { 0.5f, 1.0f })
            for (float pos : { 0.0f, 0.5f, 1.0f })
              for (double hz : { 65.41, 130.81, 261.63, 523.25 })
              { ModalParams p; p.family = modal::GRAND; p.decay = 1.0f; p.hard = hard; p.material = mat; p.pos = pos;
                const double pk = peakOf (p, hz, 1.0f, 4.0);   // v1.0, 4 s ring
                if (pk > worst) { worst = pk; wcase = "hard" + std::to_string(hard).substr(0,3) + " mat"
                                  + std::to_string(mat).substr(0,3) + " pos" + std::to_string(pos).substr(0,3)
                                  + " " + std::to_string((int)hz) + "Hz"; } }
        // a hard clip pins ~0.79 and softClip bends audibly past ~0.72 — keep the worst case well under
        check (worst < 0.72, "worst-case peak " + std::to_string(worst).substr(0,5) + " (" + wcase + ") clean");
    }

    // ── 17. GRAND coupled unison → two-stage decay (aftersound decays SLOWER than the prompt = "alive") ──
    std::printf ("\n17. GRAND two-stage decay (prompt over aftersound):\n");
    {
        ModalParams p; p.family = modal::GRAND; p.decay = 0.72f; p.material = 0.5f; p.hard = 0.4f; p.halo = 0.f;
        const double hz = 130.81;
        // prompt decay rate (A dominates) vs aftersound decay rate (slow B dominates), equal-width windows
        const double a = run (p, hz, 3.0, 0.02, 0.12).rms, b = run (p, hz, 3.0, 0.12, 0.22).rms;   // prompt pair
        const double c = run (p, hz, 3.0, 0.60, 0.70).rms, d = run (p, hz, 3.0, 0.70, 0.80).rms;   // aftersound pair
        const double rP = (b > 1e-9) ? a/b : 999.0, rA = (d > 1e-9) ? c/d : 0.0;
        // single exponential → rP == rA; a slow aftersound → prompt decays faster per window than aftersound
        check (rP > rA * 1.04 && d > 1e-5, "prompt decays faster than aftersound: rP " + std::to_string(rP).substr(0,4)
               + " > rA " + std::to_string(rA).substr(0,4));
    }

    // ── 18. GRAND DECAY is night-and-day (staccato ↔ long ring) ──
    std::printf ("\n18. GRAND DECAY night-and-day:\n");
    {
        ModalParams lo; lo.family = modal::GRAND; lo.decay = 0.10f; lo.hard = 0.4f;
        ModalParams hi; hi.family = modal::GRAND; hi.decay = 0.85f; hi.hard = 0.4f;
        const double tLo = run (lo, 130.81, 3.0, 0.5, 1.0).rms;   // tail window
        const double tHi = run (hi, 130.81, 3.0, 0.5, 1.0).rms;
        check (tHi > tLo * 5.0 && tHi > 1e-4, "tail: decay0.85 rms=" + std::to_string(tHi).substr(0,6)
               + " ≫ decay0.10 rms=" + std::to_string(tLo).substr(0,6));
    }

    // ── 19. GRAND MATERIAL night-and-day AND never nylon-dark (steel string at both ends) ──
    std::printf ("\n19. GRAND MATERIAL bright↔warm, never nylon:\n");
    {
        ModalParams dk; dk.family = modal::GRAND; dk.material = 0.0f; dk.decay = 0.6f; dk.hard = 0.5f;
        ModalParams br; br.family = modal::GRAND; br.material = 1.0f; br.decay = 0.6f; br.hard = 0.5f;
        const double hz = 130.81;
        auto capD = capture (dk, hz, 1.0, 0.04, 0.6);
        auto capB = capture (br, hz, 1.0, 0.04, 0.6);
        // upper/lower partial energy ratio — sensitive to brightness even when the fundamental dominates
        auto hilo = [&](std::vector<float>& c) {
            double lo = 0, hi = 0;
            for (int n = 1; n <= 3;  ++n) lo += goertzel (c, n*hz, SR);
            for (int n = 4; n <= 16; ++n) hi += goertzel (c, n*hz, SR);
            return hi / (lo + 1e-9); };
        const double rD = hilo (capD), rB = hilo (capB);
        // bright has clearly more upper-partial energy; warm still keeps STEEL uppers (ratio not near-zero = not nylon-dead)
        check (rB > rD * 1.6 && rD > 0.05, "upper/lower energy warm=" + std::to_string(rD).substr(0,5)
               + " → bright=" + std::to_string(rB).substr(0,5));
    }

    // ── 20. GRAND note-off DAMPER: the released tail is damped fast (key-up ≠ sustain pedal) ──
    std::printf ("\n20. GRAND note-off damper:\n");
    {
        ModalParams p; p.family = modal::GRAND; p.decay = 0.8f; p.hard = 0.4f;
        const double held = run (p, 130.81, 2.0, 0.75, 1.0).rms;               // note HELD → rings
        const double rel  = run (p, 130.81, 2.0, 0.75, 1.0, true, 0.5).rms;    // note-off at 50% → damped
        check (held > rel * 3.0 && held > 1e-4, "released tail rms=" + std::to_string(rel).substr(0,6)
               + " ≪ held rms=" + std::to_string(held).substr(0,6));
    }

    std::printf ("\n═══ %d passed, %d failed ═══\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
