#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  TerrainBodeFx — THE BODE FREQUENCY SHIFTER (rack device kind 13, fb444/fb445)
//
//  Harald Bode's 1964 single-sideband shifter, wrapped in the Echobode loop.
//  A frequency shifter adds a FIXED number of Hz to every partial. It is not a
//  pitch shifter: harmonic ratios are destroyed, which is the whole point — a
//  shifted 100/200/300 Hz becomes 105/205/305, which the ear hears as metallic,
//  bell-like, inharmonic. Put it inside a feedback delay and every repeat shifts
//  again: the barberpole that walks up (or down) forever.
//
//  ═════════════════════════════════════════════════════════════════════════════
//  fb445 — THE EIGHT TYPES ARE EIGHT MACHINES (this is the arc's whole point).
//
//  fb444 shipped `Params::type` as a FIELD THAT NOTHING READ. All eight names in
//  the dropdown ran one topology; the only thing that changed when you picked
//  "Chorale" was the label. That is the fb373 failure class one level up: the
//  path reached the engine, the engine just had one room in it. Max heard it:
//     "I want to hear them actually be different types ... if I choose a
//      different Bode type then I expect it to shift that frequency DIFFERENTLY
//      than what it shifted before."
//  So every Type now selects a different SHIFT MAPPING, a different TIME RANGE,
//  a different FEEDBACK CEILING and a different TOPOLOGY:
//
//   0 SHIFT       the clean SSB insert. ±5000 Hz, delay 0.02 ms…1 s, fb→0.995.
//                 The reference the direction law (§A) and the range law (§B)
//                 are measured on. The ONLY Type where Route 4/5 move the
//                 shifter (First / Last); everywhere else placement IS the type.
//   1 BARBERPOLE  the shifter locked INSIDE a short loop (1…300 ms) so every
//                 repeat shifts again — the staircase. ±2500 Hz per rung, so the
//                 ladder's REACH is unbounded (rung n sits at n·Δ). What makes it
//                 a barberpole rather than a staircase is a HALF-STEP interleave
//                 voice: a second shifter at Δ/2 reading the same tap, output
//                 only, so it fills the rungs in between and the rise reads as
//                 continuous instead of stepped. Feedback to 0.9985 = 667× peak.
//                 (A cut-only in-loop WINDOW -- a 90 Hz HP against Damping's LP,
//                  so rungs fade as they leave the band at the BOTTOM too -- was
//                  built, mutation-tested, measured INERT and deleted. It is a
//                  good idea for a PITCH-shifter barberpole and a wrong one here:
//                  an SSB shifter taken below zero REFLECTS through DC, it does
//                  not pile up on it, so there is nothing at the bottom to fade.
//                  See MUTATION.md.)
//   2 ECHOBODE    the pedal. The shifter sits in a LONG loop (30 ms…4 s, or the
//                 sync grid to 16 s) and THE WET IS THE TAP: you hear echoes,
//                 not an insert — echo 1 shifted once, echo 2 twice, echo n by
//                 n·Δ. ±5000 Hz, fb→0.995.
//   3 DETUNE      the range collapses to ±25 Hz with Fine widened to ±5 Hz, so
//                 sub-Hz is reachable BY HAND (the knob's own taper puts 0.9 Hz
//                 at 20 % of travel). The wet is CARRIER + SHIFTED COPY, which
//                 beats at |Δ| — not ring mod, which beats at 2Δ with no
//                 carrier. Short gentle loop (0.5…60 ms), fb capped at 0.75.
//                 Full stereo mirror by default ⇒ L beats up, R beats down.
//   4 RING        ring modulation FORCED: the sideband coefficient is clamped to
//                 ±0.5, so BOTH sidebands are always present and the carrier is
//                 always suppressed no matter where Direction sits — Direction
//                 becomes a sideband BALANCE TILT instead. Shift re-reads as the
//                 CARRIER, unipolar 0.1 Hz…5 kHz across the whole knob (0.1 Hz
//                 is a slow tremolo, 5 kHz is a klang). Blur re-reads as CARRIER
//                 BLEED: 0 = classic DSB-SC, 1 = AM/tremolo.
//   5 SPIRAL      TWO counter-shifted stores. Line A shifts +Δ, line B shifts
//                 −0.618·Δ (the golden ratio, so the rungs NEVER land on each
//                 other and the ladder never repeats), each feeding the other,
//                 both fed by the input. The sound climbs and falls at once —
//                 energy above AND below the input from the first pass — and
//                 drifts +0.382·Δ per round trip. A helix, not a staircase.
//                 ±3000 Hz, 5…700 ms, per-leg fb→0.995 (g² per round trip).
//   6 CHORALE     THREE shifters on one input: +Δ, −Δ/2, +1.618·Δ, summed with
//                 ROTATED per-channel weights (L .50/.30/.20, R .20/.50/.30) so
//                 the three voices sit in different places in the field. One
//                 tone in, an inharmonic choir out. ±2000 Hz, 2…400 ms, fb→0.96.
//   7 FREEZE      the loop at unity-ish: Feedback maps to 0.900…0.9995 (2000×),
//                 and the input is ATTENUATED into the loop as feedback rises
//                 (×(1−0.85·fb)) so a grabbed moment holds instead of piling up.
//                 Damping widens to 80 Hz…20 kHz to carve the held drone.
//                 (Damping also cuts the WET path outside the loop, on every
//                  Type — see setParams. That is what keeps it a live knob at
//                  zero feedback, where an in-loop damper does nothing at all.) The
//                 input-presence gate gets a HOLD: 260 ms at FULL loop gain
//                 after the input goes, then a 12 ms collapse.
//                 ⚠️ THE HONEST COMPROMISE — see THE FREE-RUN LAW below.
//
//  🔥 THE FREE-RUN LAW vs FREEZE. The house law (fb325) is absolute: 0.5 s note
//  → 1 s silence → tail < −70 dBFS with feedback maxed, and it is measured from
//  0.6 s after note-off. An INFINITE freeze cannot satisfy that, so this one is
//  finite and says so: the input-presence detector needs ~172 ms to fall through
//  its threshold, then Freeze holds full loop gain for 260 ms, then collapses in
//  12 ms — about 0.45 s of dead-flat sustain past note-off, versus ~0.19 s for
//  every other Type. THE HOLD LENGTH IS SET BY THE DEADLINE, NOT BY TASTE: at
//  360 ms the gate came back −65.7 dBFS, four dB over the law, and the tail was
//  not the gate at all (that is at 1e-6 by then) but the NIEMITALO ALLPASS CHAIN
//  ringing out after half a second of a loop pinned against the clip ceiling
//  (its slowest section decays with tau = 16.6 ms, and eight sections in series
//  is a long tail from a full-scale excitation). The hold has to end early enough
//  to leave the CHAIN room to die, not merely the gate. It is a GRAB, not an
//  eternity. bod_cert §T7 measures both numbers side by side and prints them so
//  the trade is on the record rather than in a comment.
//
//  ═════════════════════════════════════════════════════════════════════════════
//  fb445 — THE CEILING. "Make sure our ceiling is very, very, very high."
//    · In-loop drive (Character) went {1, 1.6, 2.6, 1.2, 4, 6.5, 2, 3.2} →
//      {1, 2, 3.5, 1.4, 6, 18, 9, 13}. Crush/Iron/Ash now crush INSIDE the loop.
//    · A POST-LOOP STAGE was added, voiced by Character and shaped
//         d = v · (1 + a·|v|)          a = {0, 2, 6, 1, 10, 40, 22, 30}
//      which is an EXPANDER with UNIT SLOPE AT ZERO (doubled when Guard is OFF) — it is +6 dB at bus level
//      on Crush and +0 dB on a whisper, so it raises the ceiling without
//      becoming a second gain stage the cert's unit-slope gate would catch.
//      It is odd (v·|v|), so it makes no DC.
//    · GUARD (the pill that fb444 declared and never read — a dead control) is
//      that stage's ceiling: ON = fastTanh, the wet can never leave ±1; OFF =
//      the raw expander, and the only thing left holding the output is the
//      in-loop clip. Guard OFF is where the device is allowed to be ugly.
//    · Feedback ceilings are per Type and go to 0.9995 (2000× = +66 dB).
//    · Touch throw 1200 → 3000 Hz; Drift wander ±6 → ±40 Hz.
//
//  ROUTE, fb445. Routes 4 and 5 ("Shift First" / "Shift Last") were BIT-IDENTICAL
//  in fb444 — both wrote the unshifted `u` and both returned `quad(u)`; the only
//  difference between the two branches was the order of two independent
//  statements. They are now genuinely different (First shifts the INPUT and
//  leaves the loop clean; Last leaves the loop clean and shifts the OUTPUT) and
//  are honored on Types 0/1/7, where placement is a choice: First shifts the input
//  and lets a clean loop repeat it; Last leaves the attack dry and shifts only what
//  came back out of the delay. (The obvious reading of Last -- shift the loop's
//  OUTPUT -- is route 4 up to a constant phase, because a shifter COMMUTES with a
//  delay; it measured 0.03 dB away, so it was thrown out.) Route 3 ("Wide") was
//  dead and is now a mid/side widen on the wet, applied BEFORE the Guard stage so
//  Guard still bounds it.
//
//  WHAT IS LIFTED VERBATIM from TerrainFilters.h's BodeShifter (filter engine 22):
//    · the Niemitalo 90-degree quadrature allpass pair (8 mults, +-0.7 deg)
//    · the 1-sample I-branch alignment delay  (mandatory — without it the pair
//      is not in quadrature and the image sideband comes back)
//    · the recursive quadrature oscillator + its every-512-samples renormalise
//
//  WHAT IS DELIBERATELY NOT LIFTED — and why (fb444 recon):
//    · THE SHIFT MAPPING. The filter-engine version derives the shift from a LOG
//      CUTOFF with zero at 632.5 Hz and caps at FMAX = 1000 Hz. Its next line,
//      `jlimit (-2000, +2000)`, reads like a +-2 kHz range and is DEAD CODE — the
//      formula can never reach it. Lifting that verbatim would have shipped a
//      fifth of the asked-for range with every knob position still "doing
//      something", which is the failure mode that hides. Here the control is a
//      true bipolar +-5000 Hz with an exponential taper and a centre detent.
//    · ONE SIDEBAND ONLY. `y = i*cos + q*sin` is one sideband; `i*cos - q*sin` is
//      the other; and `i*cos` alone is BOTH, which is ring modulation. So the
//      whole down <-> ring <-> up axis is one continuous coefficient:
//
//           y = iOut * cos  +  m * qOut * sin        m in [-1, +1]
//
//      m = +1 is UP and m = -1 is DOWN -- established by probe, not by argument
//      (see bod_cert gate 0, and the sign comment at setParams). m = +-1 costs
//      nothing over the old fixed form, and m = 0 gives ring mod for
//      free. The old engine spent a whole second filter slot (BODE_DOWN, engine
//      76) on what is a sign here.
//    · MONO. The old one is a mono effect run twice with identical parameters —
//      the filter path deliberately bypasses its own L/R cutoff spread for Bode.
//      Stereo here is a SHIFT MIRROR: R shifts by L * (1 - 2*spread), so the two
//      channels walk apart in opposite directions. That is the only thing that
//      makes this device stereo, so it defaults to full mirror.
//    · NO LOOP AT ALL. There is no delay line anywhere in the old engine, so no
//      Echobode, no barberpole, no repeats. That is most of this file.
//
//  🔥 THE LOOP-GAIN LAW (fb306-310, and the Flanger is the proven sibling):
//      loop gain = fb x (everything else inside the recirculating path).
//    A shifter in a feedback loop is the WORST case for this, because the loop's
//    content walks out of band on every pass and can pile up on a resonance the
//    shifter happens to walk into. Four placements, all copied from the shipped
//    flanger because it already survived this:
//      1. The soft clip is INSIDE the loop and is NOT optional — it is what makes
//         the thing BIBO-bounded. It is C1-continuous; the DelayEngine softClip
//         is NOT (it jumps 1.4 -> 0.885 at the knee) and importing that into a
//         loop designed to reach the knee imports a click generator.
//      2. The makeup divides INSIDE the loop, so sat'(0) == 1 exactly. Without
//         that, Drive is a second secret feedback control and the stability gate
//         is measuring a lie.
//      3. The input-presence gate multiplies the COEFFICIENT, never the output
//         (gating the output clicks), and is applied SQUARED — a linear release
//         latches audibly, the square dies.
//      4. In-loop filters are CUT-ONLY. Damping is a low-pass with |H| <= 1 at
//         every frequency. Any Character
//         that wants presence adds it OUTSIDE the loop (the post stage), or loop
//         gain exceeds 1 at highs and the thing diverges (the reverb hit +130 dB
//         doing exactly it).
//    Ceiling reference: a loop peaks at 1/(1-g). 0.9995 = 2000x = +66 dB.
//
//  CPU: ~20 mults/sample/channel for the shifter + the loop taps on the
//  single-shifter Types; Spiral doubles that and Chorale triples the shifter
//  (still one loop). NEVER oversampled — the shifter is a linear time-varying
//  operation with no harmonic generation of its own, so there is nothing to
//  alias. (The filter path runs it at 2x only because it shares
//  needsOversampling() with the ladder family; that is an artefact, not a
//  requirement.) The post stage DOES generate harmonics; it is a static odd
//  curve on a bus that is limited downstream, which is the house convention for
//  every other saturating rack device.
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace tw {

// ═════════════════════════════════════════════════════════════ local helpers
// Self-contained by law: this header pulls in no TerrainFilters.h, so the cert
// harness compiles it against the shim with nothing else in the include path.
namespace bode_detail {

inline float fastTanh (float x) noexcept
{
    if (x >  5.0f) return  1.0f;
    if (x < -5.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct DCBlock
{
    float xPrev = 0.0f, yPrev = 0.0f;
    void  reset() noexcept { xPrev = yPrev = 0.0f; }
    inline float process (float x) noexcept
    {
        const float y = x - xPrev + 0.995f * yPrev;
        xPrev = x; yPrev = y;
        return y;
    }
};

// 2nd-order allpass H(z) = (A - z^-2)/(1 - A z^-2), A = a^2  (Niemitalo).
struct AP2
{
    float A = 0.0f, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void  reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
    inline float process (float x) noexcept
    {
        const float y = A * (x + y2) - x2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// One-pole low-pass. CUT-ONLY by construction: g in [0,1), y converges to x.
struct LP1
{
    float g = 0.0f, z = 0.0f;
    void  reset() noexcept { z = 0.0f; }
    void  setHz (float hz, float fs) noexcept
    {
        const float f = std::max (10.0f, std::min (hz, 0.45f * fs));
        g = 1.0f - std::exp (-6.2831853f * f / fs);
        g = std::max (0.0f, std::min (1.0f, g));
    }
    inline float process (float x) noexcept { z += g * (x - z); return z; }
};

// Two one-poles in series. fb445 — the in-loop damping was ONE pole over
// 400 Hz..20 kHz and measured 0.06-0.18 dB of travel on six of the eight Types:
// dead, by the fb325 law. 12 dB/oct over 120 Hz..20 kHz is a control. Still
// CUT-ONLY by construction, which is what the loop-gain law requires of it.
struct LP2
{
    LP1 a, b;
    void  reset() noexcept { a.reset(); b.reset(); }
    void  setHz (float hz, float fs) noexcept { a.setHz (hz, fs); b.setHz (hz, fs); }
    inline float process (float x) noexcept { return b.process (a.process (x)); }
};

// One-pole high-pass built from the same state (for the Low Keep complement and
// for the Barberpole window). CUT-ONLY too: |H| <= 1 at every frequency.
struct HP1
{
    LP1 lp;
    void  reset() noexcept { lp.reset(); }
    void  setHz (float hz, float fs) noexcept { lp.setHz (hz, fs); }
    inline float process (float x) noexcept { return x - lp.process (x); }
};

// A Schroeder allpass, for in-loop diffusion (Blur).
struct APDelay
{
    std::vector<float> buf;
    int   n = 0, w = 0;
    float g = 0.5f;
    void  prepare (int len) { n = std::max (1, len); buf.assign ((size_t) n, 0.0f); w = 0; }
    void  reset()  noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline float process (float x) noexcept
    {
        if (buf.empty()) return x;
        const float d = buf[(size_t) w];
        const float v = x + g * d;
        buf[(size_t) w] = v;
        if (++w >= n) w = 0;
        return d - g * v;
    }
};

// C1-continuous soft clip. sat'(0) == 1, so the in-loop makeup below cannot
// raise the loop gain past the calibrated feedback coefficient.
inline float softClipC1 (float x, float k) noexcept
{
    return fastTanh (x * k) / std::max (1e-6f, k);
}

// A cheap, smooth random walk (Drift). Deterministic per instance: seeded from
// the instance index, never from a global, so two cards never move in lockstep.
struct SmoothRandom
{
    uint32_t s = 22222u;
    float    cur = 0.0f, tgt = 0.0f, inc = 0.0f;
    int      countdown = 1;
    void  seed (uint32_t v) noexcept { s = v | 1u; cur = tgt = inc = 0.0f; countdown = 1; }
    inline float next (int stepSamples) noexcept
    {
        if (--countdown <= 0)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            tgt = ((float) (s & 0xFFFFu) / 32768.0f) - 1.0f;      // -1..+1
            countdown = std::max (1, stepSamples);
            inc = (tgt - cur) / (float) countdown;
        }
        cur += inc;
        return cur;
    }
};

} // namespace bode_detail

// ═════════════════════════════════════════════════════ the quadrature shifter
// The Hilbert pair + oscillator, lifted, plus the sideband-blend coefficient.
struct BodeQuad
{
    bode_detail::AP2 iA[4], qA[4];
    float  iDelay = 0.0f;
    double osC = 1.0, osS = 0.0, oscCos = 1.0, oscSin = 0.0;
    int    renorm = 0;

    void reset() noexcept
    {
        // Niemitalo coefficients — a 90-degree pair to +-0.7 deg over the band.
        static const float kI[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        static const float kQ[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        for (int i = 0; i < 4; ++i)
        {
            iA[i].reset(); iA[i].A = kI[i] * kI[i];
            qA[i].reset(); qA[i].A = kQ[i] * kQ[i];
        }
        iDelay = 0.0f; osC = 1.0; osS = 0.0; renorm = 0;
    }

    // Set the shift in Hz. The oscillator PHASE is retained across calls, so a
    // shift change is phase-continuous (no click) — but note it does not glide
    // by itself, which is why the caller smooths the Hz value, not this.
    void setShiftHz (float hz, double fs) noexcept
    {
        const double w = 6.283185307179586 * (double) hz / std::max (1.0, fs);
        oscCos = std::cos (w);
        oscSin = std::sin (w);
    }

    // m = +1 -> one sideband · m = -1 -> the other · m = 0 -> BOTH = ring mod.
    inline float process (float x, float m) noexcept
    {
        float iSig = x;  for (int k = 0; k < 4; ++k) iSig = iA[k].process (iSig);
        const float iOut = iDelay; iDelay = iSig;     // the mandatory 1-sample align
        float qOut = x;  for (int k = 0; k < 4; ++k) qOut = qA[k].process (qOut);

        const double nC = oscCos * osC - oscSin * osS;
        osS = oscSin * osC + oscCos * osS;
        osC = nC;
        if (++renorm >= 512)                          // Gram-Schmidt-ish renormalise
        {
            renorm = 0;
            const double mm = 1.5 - 0.5 * (osC * osC + osS * osS);
            osC *= mm; osS *= mm;
        }
        return iOut * (float) osC + m * (qOut * (float) osS);
    }
};

// ═════════════════════════════════════════════════════════════════ the device
struct TerrainBodeFx
{
    // Rack Law C — cardinality is FROZEN AT BIRTH (fb373: a choice param
    // normalised on the wrong count silently hands you a different machine).
    // Declare the full width on day one and grow into it.
    static constexpr int   kNumTypes  = 8;
    static constexpr int   kNumChars  = 8;
    static constexpr int   kNumRoutes = 8;
    static constexpr float kShiftMax  = 5000.0f;    // Hz, each way, on Type 0. The ASK.
    static constexpr float kFineMax   = 2.0f;       // Hz vernier, each way (Detune widens it)
    static constexpr float kTouchMax  = 3000.0f;    // Hz of envelope throw (fb445: was 1200)
    static constexpr float kShiftCeil = 12000.0f;   // total clamp incl. Touch + Drift
    // fb444 — 0.95 WAS THE TIMIDITY, and mutation testing is what exposed it.
    //   Deleting the in-loop soft clip turned ZERO gates red: at 0.95, with the
    //   shift decorrelating the loop's content on every pass, this thing is
    //   bounded with no guard at all. A guard that cannot be missed is a guard
    //   that is not doing anything, and a maximum that cannot misbehave is not
    //   a maximum -- the fx3 arc took the phaser to 0.998 and named the old
    //   ceilings exactly this. At 0.995 the loop peaks at 200x, the clip becomes
    //   the thing that holds it, and the top of the knob genuinely screams.
    // fb445 — the ceiling is now PER TYPE and reaches 0.9995 (2000x) on Freeze.
    static constexpr float kFbMax     = 0.995f;     // 1/(1-g) = 200x = +46 dB
    static constexpr float kFbMaxHot  = 0.9995f;    // Freeze: 2000x = +66 dB

    struct Params
    {
        int   type = 0, chr = 0, route = 0;
        float shift = 0.5f;      // 0..1, 0.5 = no shift (centre detent) — Ring: unipolar carrier
        float dir   = 1.0f;      // 0 = down · 0.5 = ring · 1 = up  — Ring: sideband balance tilt
        float fdbk  = 0.0f;      // 0..1 -> 0..the Type's loop-gain ceiling
        float mix   = 1.0f;      // 0..1 equal-power
        float fine  = 0.5f;      // 0..1 bipolar, +-kFineMax Hz (Detune: +-5 Hz)
        float spread = 1.0f;     // 0..1 stereo shift mirror
        float time  = 0.45f;     // 0..1 -> the Type's delay range (or a sync division)
        float blur  = 0.0f;      // 0..1 continuous in-loop diffusion — Ring: carrier bleed
        float lowKeep = 0.0f;    // 0..1 -> off, 20 .. 2000 Hz
        float damping = 1.0f;    // 0..1 -> 400 Hz .. 20 kHz in-loop LP (Freeze: from 200 Hz)
        float touch = 0.5f;      // 0..1 bipolar env -> shift
        float drift = 0.0f;      // 0..1 instability
        bool  sync = false, guard = true;
        double bpm = 120.0;
    };

    // ── lifecycle ───────────────────────────────────────────────────────────
    void prepare (double sampleRate, int instanceIndex = 0)
    {
        fs_ = (float) std::max (8000.0, sampleRate);

        // fb444 — SIZE FOR THE SYNC CEILING, NOT THE FREE RANGE. The free Time
        // knob stops at 1 s, but "4 bar" at 60 BPM is 16 s. Size for 16.5 s or
        // the top four sync divisions truncate silently, with no error anywhere.
        int need = (int) std::ceil (16.5f * fs_) + 8;
        int sz = 1; while (sz < need) sz <<= 1;
        dl_[0].assign ((size_t) sz, 0.0f);
        dl_[1].assign ((size_t) sz, 0.0f);
        mask_ = sz - 1;

        // fb445 — SPIRAL's two counter-shifted stores. They are SHORT on purpose:
        // Spiral's free range tops out at 700 ms, and in sync its division is
        // HALVED until it fits rather than clamped, so it stays on the grid and
        // the knob never plateaus. 1.36 s of store, not 21 s: a second pair of
        // full-size lines would have cost 8 MB per rack instance for nothing.
        int needA = (int) std::ceil (1.10f * fs_) + 8;
        int szA = 1; while (szA < needA) szA <<= 1;
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 2; ++k) aux_[c][k].assign ((size_t) szA, 0.0f);
        auxMask_ = szA - 1;

        // Coprime-ish diffusion lengths, 5..35 ms, so the allpasses never align.
        static const float kBlurMs[4] = { 5.31f, 11.73f, 21.17f, 34.61f };
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k)
                blur_[c][k].prepare ((int) (kBlurMs[k] * 0.001f * fs_)
                                     + (c == 1 ? 7 : 0));       // L/R decorrelated

        drift_[0].seed (0x9E3779B9u + 2654435761u * (uint32_t) (instanceIndex + 1));
        drift_[1].seed (0x85EBCA6Bu + 2246822519u * (uint32_t) (instanceIndex + 1));
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 3; ++k) quad_[c][k].reset();
            std::fill (dl_[c].begin(), dl_[c].end(), 0.0f);
            for (int k = 0; k < 2; ++k)
            { std::fill (aux_[c][k].begin(), aux_[c][k].end(), 0.0f); aw_[c][k] = 0; }
            for (int k = 0; k < 4; ++k) blur_[c][k].reset();
            damp_[c].reset(); damp2_[c].reset(); lowLp_[c].reset(); tone_[c].reset();
            dcOut_[c].reset();
            w_[c] = 0;
        }
        env_ = 0.0f; gate_ = 0.0f; hold_ = 0;
        shiftSm_ = 0.0f; fbSm_ = 0.0f; timeSm_ = 0.0f; mixSm_ = -1.0f;
        mImage_ = 0.0f; mLoop_ = 0.0f;
    }

    // ── per-block parameter intake (NEVER per sample) ───────────────────────
    void setParams (const Params& p) noexcept
    {
        p_ = p;
        type_ = p.type < 0 ? 0 : (p.type >= kNumTypes ? kNumTypes - 1 : p.type);

        // ── THE SHIFT, IN REAL Hz, AND IT IS PER TYPE (fb445). This is the line
        //    Max was asking for: picking a different Type must shift the
        //    frequency DIFFERENTLY, not merely relabel the card. Bipolar with an
        //    exponential taper so the sub-Hz beating region near zero is
        //    reachable by hand and the ends still reach the Type's span:
        //    sgn(v) * ((span+1)^|v| - 1).
        static const float kSpan[kNumTypes]  = { 5000.0f, 2500.0f, 4000.0f,   25.0f,
                                                 5000.0f, 3000.0f, 2000.0f, 1500.0f };
        static const float kFineSp[kNumTypes] = { 2.0f, 2.0f, 2.0f, 5.0f, 2.0f, 2.0f, 2.0f, 2.0f };
        const float fineHz = (2.0f * clamp01 (p.fine) - 1.0f) * kFineSp[type_];
        if (type_ == 4)
            // RING — Shift IS the carrier, so it is unipolar and spends the whole
            // knob on 0.1 Hz (a slow tremolo) .. 5 kHz (a klang). A bipolar
            // carrier would waste half the travel on a mirror image of itself.
            shiftTarget_ = 0.1f * std::pow (50000.0f, clamp01 (p.shift)) + fineHz;
        else
        {
            const float v   = 2.0f * clamp01 (p.shift) - 1.0f;
            const float mag = std::pow (kSpan[type_] + 1.0f, std::fabs (v)) - 1.0f;
            shiftTarget_ = (v < 0.0f ? -mag : mag) + fineHz;
        }

        // ── the sideband blend. THE SIGN IS SETTLED BY THE CERT, NOT BY TASTE:
        //    gate 0 of bod_cert drives 1 kHz in with Direction = up and asserts
        //    the energy lands at 1100 Hz, not 900. A flipped sign builds clean,
        //    measures "different", and is a different instrument.
        //    y = i*cos + m*q*sin, so m = +1 shifts DOWN; up therefore needs -1.
        //    MEASURED, not reasoned: the first draft of this line wrote
        //    -(2b-1) on a pen-and-paper argument about which term carries which
        //    sideband, and gate 0 came back with 899 Hz where 1100 was wanted --
        //    a working shifter running backwards. The bible's own first draft
        //    made the identical mistake. The probe decides, every time.
        //    fb445 — on RING the coefficient is CLAMPED to +-0.5 so both
        //    sidebands are always present and the carrier is always suppressed:
        //    Direction stops being up/down and becomes a BALANCE TILT (about
        //    9.5 dB between the sidebands at either end of the knob).
        const float b = clamp01 (p.dir);
        mBlend_ = (type_ == 4) ? 0.5f * (2.0f * b - 1.0f) : (2.0f * b - 1.0f);
        // Ring mod (m = 0) splits the energy into two half-amplitude sidebands:
        // half the power of SSB, so +3 dB back at the centre, tapering to unity
        // at either end. Without this the middle of the knob is a level dip.
        ringComp_ = 1.0f + 0.41421356f * (1.0f - std::fabs (mBlend_));

        // ── the loop. THE CEILING IS PER TYPE (fb445): Detune is deliberately
        //    gentle, Barberpole and Freeze go to the edge. Freeze also has a
        //    FLOOR — its identity is a loop that is already nearly closed.
        static const float kFbCeil[kNumTypes]  = { 0.995f, 0.9985f, 0.995f, 0.750f,
                                                   0.920f, 0.995f,  0.960f, kFbMaxHot };
        static const float kFbFloor[kNumTypes] = { 0.0f, 0.0f, 0.0f, 0.0f,
                                                   0.0f, 0.0f, 0.0f, 0.900f };
        fbTarget_   = kFbFloor[type_] + (kFbCeil[type_] - kFbFloor[type_]) * clamp01 (p.fdbk);
        // FREEZE captures instead of piling up: as the loop closes, the door in
        // narrows. Without this the loop just saturates against the in-loop clip
        // and a "freeze" is a mush, not a held moment.
        inGain_     = (type_ == 7) ? (1.0f - 0.85f * clamp01 (p.fdbk)) : 1.0f;
        timeTarget_ = syncedTimeSamples (p);
        // fb444 — BLUR IS CONTINUOUS, NOT STEPPED. It was `floor(blur * 4)`
        //   allpasses, which made the first sixth of the knob dead travel: knob
        //   0 and knob 1/6 both resolved to ZERO sections and the cert measured
        //   them 0.000 dB apart. Switching sections in and out also jumps the
        //   loop length, because an allpass at g=0 is not transparent -- it is
        //   still a delay. So all four sections run all the time (their state
        //   stays warm) and the knob crossfades between the clean tap and the
        //   diffused one while opening g. No steps, no length jump, no plateau.
        //   fb445 — on RING the same knob re-reads as CARRIER BLEED, so the
        //   diffusion crossfade is parked and `ringDepth_` carries the travel.
        blurMix_    = (type_ == 4) ? 0.0f : clamp01 (p.blur);
        ringDepth_  = 1.0f - clamp01 (p.blur);
        blurG_      = 0.30f + 0.40f * clamp01 (p.blur);
        const float dLo = (type_ == 7) ?  80.0f : 120.0f;      // Freeze carves harder
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 4; ++k) blur_[c][k].g = blurG_;
            // CUT-ONLY, and the only filters inside the loop. fb444 — the top of
            // this range used to be 40 kHz, which is ABOVE the LP1 Nyquist clamp,
            // so the last third of the knob was one repeated value. It now ends
            // at 20 kHz: every position on the travel is a different filter.
            // fb445 — and it starts at 120 Hz through TWO poles, because one pole
            // from 400 Hz was a knob you could not hear on six of the eight Types.
            const float dHz = dLo * std::pow (20000.0f / dLo, clamp01 (p.damping));
            damp_[c].setHz (dHz, fs_);
            damp2_[c].setHz (dHz, fs_);
            // fb445 — AND A CUT OUTSIDE THE LOOP, ON THE SAME KNOB. An in-loop
            // damper does literally NOTHING at zero feedback, and at 0.6 it moved
            // 0.10 dB across its whole travel on six of the eight Types: a dead
            // knob by fb325 whichever way you measure it. Worse, widening the
            // in-loop range downward made it WORSE, because "kills the loop" and
            // "kills the loop harder" are the same sound -- the first two steps of
            // the travel measured 0.08 dB apart. So Damping keeps its in-loop
            // meaning AND takes the wet path with it. Outside the loop this cut
            // costs nothing in stability (the loop-gain law only binds inside),
            // and at Damping = 1 it clamps above Nyquist, so every gate that runs
            // at the default sees exactly the engine fb444 certified.
            tone_[c].setHz (std::max (300.0f, std::min (1.6f * dHz, 20000.0f)), fs_);
            lowLp_[c].setHz (lowKeepHz (p.lowKeep), fs_);
        }
        lowKeepOn_ = clamp01 (p.lowKeep) > 0.001f;

        // Drive comes from Character; it is applied INSIDE the loop and divided
        // straight back out, so sat'(0) == 1 and Character can never become a
        // second feedback control. The POST stage is outside the loop and cannot
        // touch loop gain either — it is an expander with unit slope at zero.
        clipK_ = characterDrive (p.chr);
        postA_ = characterPost  (p.chr);

        touchAmt_  = (2.0f * clamp01 (p.touch) - 1.0f) * kTouchMax;
        driftAmt_  = clamp01 (p.drift) * 40.0f;         // fb445: up to +-40 Hz of wander
        driftStep_ = std::max (1, (int) (fs_ / 0.3f / 64.0f));
        spread_    = 1.0f - 2.0f * clamp01 (p.spread);  // 1 = same, -1 = mirrored
        mixT_      = clamp01 (p.mix);
        if (mixSm_ < 0.0f) mixSm_ = mixT_;              // first block: no ramp
        routeCross_ = (p.route == 1);
        routeMono_  = (p.route == 2);
        routeWide_  = (p.route == 3);
        // fb445 — Route 4/5 were BIT-IDENTICAL and are now genuinely First/Last.
        // They are honored only where shift PLACEMENT is a choice: on Echobode,
        // Detune, Ring, Spiral and Chorale the placement IS the Type, so the
        // route falls through to Normal rather than shipping a control that
        // quietly dismantles the Type you just chose.
        const bool placeable = (type_ == 0 || type_ == 1 || type_ == 7);
        shiftFirst_ = placeable && (p.route == 4);
        shiftLast_  = placeable && (p.route == 5);

        guardOn_ = p.guard;
        // FREEZE relaxes the input-presence gate into a HOLD. Everything else
        // keeps fb444's release EXACTLY (0.0009 per sample), so §C's numbers on
        // the other seven Types are the same numbers.
        holdN_   = (type_ == 7) ? (int) (0.26f * fs_) : 0;
        relCoef_ = (type_ == 7) ? (1.0f - std::exp (-1.0f / (0.012f * fs_))) : 0.0009f;
    }

    // ── the audio ───────────────────────────────────────────────────────────
    inline void processStereo (float inL, float inR, float& outL, float& outR) noexcept
    {
        // Per-sample smoothing. The shift is the one that MUST glide: the
        // oscillator holds phase across a change, so a jump is click-free but
        // still a jump, and a jumped shift on a sustained note is audible as a
        // step. ~15 ms.
        const float aP = 1.0f - std::exp (-1.0f / (0.015f * fs_));
        const float aM = 1.0f - std::exp (-1.0f / (0.030f * fs_));

        // ── the input-presence gate. NOTHING FREE-RUNS: with feedback maxed and
        //    no input the tail must die, so the gate multiplies the COEFFICIENT
        //    (gating the output clicks) and is applied SQUARED (a linear release
        //    latches audibly; the square dies). fb445 — FREEZE inserts a HOLD
        //    before the release; every other Type has holdN_ == 0 and behaves
        //    exactly as fb444 did.
        const float rect = 0.5f * (std::fabs (inL) + std::fabs (inR));
        env_ += (rect > env_ ? 0.35f : 0.0006f) * (rect - env_);
        if (env_ > 2.2e-4f)      { gate_ += 0.02f * (1.0f - gate_); hold_ = holdN_; }
        else if (hold_ > 0)      { --hold_; }
        else                     { gate_ += relCoef_ * (0.0f - gate_); }
        const float gate2 = gate_ * gate_;

        shiftSm_ += aP * (shiftTarget_ - shiftSm_);
        fbSm_    += aP * (fbTarget_    - fbSm_);
        timeSm_  += aM * (timeTarget_  - timeSm_);
        mixSm_   += aM * (mixT_        - mixSm_);

        const float touch = touchAmt_ * env_ * env_ * 24.0f;
        const float gFb   = fbSm_ * gate2;                        // ← THE COEFFICIENT
        mLoop_ = gFb;

        float dry[2] = { inL, inR };
        float x[2]   = { inL, inR };

        if (routeMono_) { const float m = 0.5f * (x[0] + x[1]); x[0] = x[1] = m; }
        if (routeCross_) std::swap (x[0], x[1]);

        // ── Low Keep: the band below the crossover skips the whole device and
        //    is re-added un-shifted, so the bass stays anchored and never enters
        //    the loop. Minimum-phase by construction (one pole), never linear.
        float low[2] = { 0.0f, 0.0f };
        if (lowKeepOn_)
            for (int c = 0; c < 2; ++c) { low[c] = lowLp_[c].process (x[c]); x[c] -= low[c]; }

        float wet[2];
        for (int c = 0; c < 2; ++c)
        {
            const float dr  = driftAmt_ > 0.0f ? driftAmt_ * drift_[c].next (driftStep_) : 0.0f;
            const float mir = (c == 0 ? 1.0f : spread_);          // the stereo shift MIRROR
            float dHz = (shiftSm_ + touch + dr) * mir;
            dHz = dHz < -kShiftCeil ? -kShiftCeil : (dHz > kShiftCeil ? kShiftCeil : dHz);

            wet[c] = (type_ == 5) ? spiralChannel (c, x[c], dHz, gFb)
                                  : loopChannel   (c, x[c], dHz, gFb);
            wet[c] = tone_[c].process (wet[c]);        // Damping, outside the loop
        }

        // Route 3 — WIDE. fb444 declared it and never read it. It runs on the WET
        // only, and BEFORE the Guard stage, so Guard still bounds the result.
        if (routeWide_)
        {
            const float m = 0.5f * (wet[0] + wet[1]);
            const float s = 0.5f * (wet[0] - wet[1]);
            wet[0] = 0.70f * m + 1.70f * s;
            wet[1] = 0.70f * m - 1.70f * s;
        }

        for (int c = 0; c < 2; ++c)
            wet[c] = dcOut_[c].process (postShape (wet[c])) + low[c];

        // Equal-power mix, and 100 % is FULLY wet (dry residual below -60 dB).
        const float wg = std::sin (1.5707963f * mixSm_);
        const float dg = std::cos (1.5707963f * mixSm_);
        outL = dg * dry[0] + wg * wet[0];
        outR = dg * dry[1] + wg * wet[1];

        mImage_ = 0.5f * (std::fabs (wet[0]) + std::fabs (wet[1]));
    }

    // Block form. The rack's TW_FX4_APPLY macro drives every fx4 device as
    // processStereo(&l, &r, n) with n == 1 (the serial chain is per-sample), so
    // the device must offer that shape. One routine, both call sites.
    inline void processStereo (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i) processStereo (l[i], r[i], l[i], r[i]);
    }

    // ── meters. fb432: read the engine's OWN numbers, and the SIGN of them —
    //    a device whose mechanism never ran still measures "different".
    float meterShiftHz()  const noexcept { return shiftSm_; }   // signed, in Hz
    float meterLoopGain() const noexcept { return mLoop_;   }   // the real coefficient
    float meterImage()    const noexcept { return mImage_;  }
    float meterBlend()    const noexcept { return mBlend_;  }   // +1 up · 0 ring · -1 down
    // fb445 additions — the Type's own numbers, so a cert can prove the mapping
    // moved and not merely that "the spectrum changed".
    float meterGate()     const noexcept { return gate_;    }   // the presence gate, 0..1
    float meterDelayMs()  const noexcept { return 1000.0f * timeSm_ / fs_; }
    int   meterType()     const noexcept { return type_;    }

private:
    static float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    static float lowKeepHz (float k) noexcept
    {
        return clamp01 (k) <= 0.001f ? 20.0f : 20.0f * std::pow (100.0f, clamp01 (k));
    }

    // Character sets the in-loop drive. Unity at the clean end; the makeup is
    // divided back inside softClipC1, so this cannot move the loop gain.
    // fb445 — the top three were timid (6.5 / 2.0 / 3.2 on a bus that sits at
    // -26 dBFS barely bends). Crush, Iron and Ash now crush.
    static float characterDrive (int chr) noexcept
    {
        static const float k[kNumChars] = { 1.0f, 2.0f, 3.5f, 1.8f, 6.0f, 18.0f, 9.0f, 13.0f };
        return k[chr < 0 ? 0 : (chr >= kNumChars ? kNumChars - 1 : chr)];
    }

    // fb445 — THE POST-LOOP STAGE, the thing that raises the ceiling. It is an
    // EXPANDER, not a gain: d = v*(1 + a*|v|). Slope at zero is exactly 1 (so
    // the unit-slope gate stays honest and Character still cannot become a
    // second feedback control), but at the -26 dBFS bus it is already +6 dB on
    // Crush and it grows with level until Guard's tanh takes over.
    static float characterPost (int chr) noexcept
    {
        static const float a[kNumChars] = { 0.0f, 2.0f, 6.0f, 1.0f, 10.0f, 40.0f, 22.0f, 30.0f };
        return a[chr < 0 ? 0 : (chr >= kNumChars ? kNumChars - 1 : chr)];
    }

    // GUARD is this stage's ceiling, and fb444 read the field NOWHERE — the pill
    // on the card did nothing at all. ON: the expander runs at its voiced amount
    // and ends in a tanh, so the wet can never leave +-1. OFF: the expansion
    // DOUBLES and the tanh is gone, and the only thing left holding the output is
    // the in-loop clip. Guard OFF is where this device is allowed to be ugly.
    inline float postShape (float v) const noexcept
    {
        const float a = guardOn_ ? postA_ : 2.0f * postA_;
        const float d = v * (1.0f + a * std::fabs (v));
        return guardOn_ ? bode_detail::fastTanh (d) : d;
    }

    float syncedTimeSamples (const Params& p) const noexcept
    {
        // fb445 — THE DELAY RANGE IS PER TYPE. A barberpole staircase and an
        // Echobode repeat are not the same machine at a different knob position;
        // they live in different decades. Every range is a full exponential
        // sweep, so no Type has dead travel.
        static const float kLo[kNumTypes] = { 0.02f,   1.0f,   30.0f,  0.5f,
                                              0.05f,   5.0f,    2.0f, 40.0f };
        static const float kHi[kNumTypes] = { 1000.0f, 300.0f, 4000.0f, 60.0f,
                                               500.0f, 700.0f,  400.0f, 2000.0f };
        if (! p.sync)
        {
            // The short end is a comb, the middle a flange, then slap, then
            // discrete echoes: the knob keeps changing character across its
            // whole travel, inside whatever decade the Type lives in.
            const float ms = kLo[type_] * std::pow (kHi[type_] / kLo[type_], clamp01 (p.time));
            return std::max (1.0f, ms * 0.001f * fs_);
        }
        // The house 20-entry division list, folded into this same knob so two
        // selectors can never disagree (there is no SYNCDIV on any device).
        static const float kDiv[20] = { 16.0f, 12.0f, 8.0f, 6.0f, 4.0f, 3.0f, 2.0f, 1.5f,
                                        1.0f, 0.75f, 0.5f, 0.375f, 0.25f, 0.1875f, 0.125f,
                                        0.09375f, 0.0625f, 0.03125f, 0.015625f, 0.0078125f };
        const int  i    = (int) (clamp01 (p.time) * 19.0f + 0.5f);
        const float beats = kDiv[i < 0 ? 0 : (i > 19 ? 19 : i)];
        float secs  = beats * 60.0f / (float) std::max (20.0, p.bpm);
        if (type_ == 5)
        {
            // Spiral's stores are short. HALVE the division until it fits rather
            // than clamping it: a clamp makes the top divisions one repeated
            // value (fb444's Damping bug), a halving stays on the musical grid.
            const float mx = (float) (auxMask_ - 4) / fs_;
            while (secs > mx && secs > 0.002f) secs *= 0.5f;
        }
        return std::max (1.0f, std::min (secs, 16.4f) * fs_);
    }

    // ── the single-loop Types: 0 Shift · 1 Barberpole · 2 Echobode · 3 Detune ·
    //    4 Ring · 6 Chorale · 7 Freeze. One delay line, one to three shifters.
    inline float loopChannel (int c, float x, float dHz, float gFb) noexcept
    {
        const int   len  = std::max (1, std::min ((int) timeSm_, mask_ - 2));
        const float frac = timeSm_ - std::floor (timeSm_);
        const int   r0   = (w_[c] - len)     & mask_;
        const int   r1   = (w_[c] - len - 1) & mask_;
        float tap = dl_[c][(size_t) r0] + frac * (dl_[c][(size_t) r1] - dl_[c][(size_t) r0]);

        tap = damp_[c].process (tap);                         // cut-only, in-loop
        {                                                     // continuous diffusion
            float dif = tap;
            for (int k = 0; k < 4; ++k) dif = blur_[c][k].process (dif);
            tap += blurMix_ * (dif - tap);
        }
        quad_[c][0].setShiftHz (dHz, fs_);

        // Keep the Spiral stores flushing while another Type runs, so switching
        // back does not read seconds-old audio out of a cold line.
        aux_[c][0][(size_t) aw_[c][0]] = 0.0f; aw_[c][0] = (aw_[c][0] + 1) & auxMask_;
        aux_[c][1][(size_t) aw_[c][1]] = 0.0f; aw_[c][1] = (aw_[c][1] + 1) & auxMask_;

        float u, y, out, wr;

        if (shiftFirst_)          // Route 4 — shift the INPUT, the loop stays put
        {
            const float xs = quad_[c][0].process (x * inGain_, mBlend_) * ringComp_;
            u  = bode_detail::softClipC1 (xs + gFb * tap, clipK_);
            y  = u;  wr = u;
        }
        else if (shiftLast_)      // Route 5 — clean loop, and the shift reaches ONLY
        {                         // what has been delayed: dry attack, metallic tail.
            u  = bode_detail::softClipC1 (x * inGain_ + gFb * tap, clipK_);
            wr = u;               // (Shifting the whole loop OUTPUT instead is route 4
                                  //  up to a constant phase -- a shifter commutes with a
                                  //  delay -- and measured 0.03 dB from it. Thrown out.)
            y  = x * inGain_ + quad_[c][0].process (gFb * tap, mBlend_) * ringComp_;
        }
        else                      // the shifter INSIDE the loop — the barberpole
        {
            u  = bode_detail::softClipC1 (x * inGain_ + gFb * tap, clipK_);
            y  = quad_[c][0].process (u, mBlend_) * ringComp_;
            wr = y;
        }
        out = y;

        switch (type_)
        {
            case 1:               // BARBERPOLE — the half-step interleave voice
            {                     // (output only: it must not join the ladder)
                quad_[c][1].setShiftHz (0.5f * dHz, fs_);
                const float h = quad_[c][1].process (tap, mBlend_) * ringComp_;
                out = 0.80f * y + 0.40f * h;
                break;
            }
            case 2:               // ECHOBODE — the WET IS THE TAP. You hear the
                out = tap;        // echoes, each shifted one more time, not an
                break;            // insert. This is what makes it a pedal.

            case 3:               // DETUNE — carrier + shifted copy beats at |d|.
                out = 0.5f * (u + y);   // (ring mod would beat at 2d with no
                break;                  //  carrier: a different sound entirely)

            case 4:               // RING — Blur re-reads as CARRIER BLEED:
                out = (1.0f - ringDepth_) * u + ringDepth_ * y;
                break;            // depth 1 = DSB-SC · depth 0 = straight through

            case 6:               // CHORALE — three voices at related offsets,
            {                     // rotated per channel so they sit apart
                static const float kW[2][3] = { { 0.50f, 0.30f, 0.20f },
                                                { 0.20f, 0.50f, 0.30f } };
                quad_[c][1].setShiftHz (-0.5f   * dHz, fs_);
                quad_[c][2].setShiftHz ( 1.618f * dHz, fs_);
                const float v0 = y / ringComp_;                  // already computed
                const float v1 = quad_[c][1].process (u, mBlend_);
                const float v2 = quad_[c][2].process (u, mBlend_);
                out = ringComp_ * (kW[c][0] * v0 + kW[c][1] * v1 + kW[c][2] * v2);
                break;   // ...and only the PRIMARY voice recirculates (wr == y). Feeding
                         // the whole choir back smeared the loop's own identity until
                         // Damping measured 0.20 dB of travel across its whole knob --
                         // a dead control by the fb325 law, on a Type that reads fine.
            }
            default: break;       // 0 Shift · 7 Freeze — the direct shifted signal
        }

        dl_[c][(size_t) w_[c]] = wr;
        w_[c] = (w_[c] + 1) & mask_;
        return out;
    }

    // ── Type 5, SPIRAL. Two stores, counter-shifted, cross-fed. A climbs +d and
    //    hands its content to B; B falls -0.618*d and hands it back. Both are fed
    //    by the input, so there is energy above AND below the note from the first
    //    pass, and the golden ratio means the rungs never land on each other.
    inline float spiralChannel (int c, float x, float dHz, float gFb) noexcept
    {
        const float tA = std::min (timeSm_,          (float) (auxMask_ - 3));
        const float tB = std::min (timeSm_ * 0.70f,  (float) (auxMask_ - 3));
        float ta = auxRead (c, 0, tA);
        float tb = auxRead (c, 1, tB);

        ta = damp_[c].process (ta);
        {
            float dif = ta;
            for (int k = 0; k < 4; ++k) dif = blur_[c][k].process (dif);
            ta += blurMix_ * (dif - ta);
        }
        tb = damp2_[c].process (tb);

        quad_[c][0].setShiftHz ( dHz,              fs_);
        quad_[c][1].setShiftHz (-0.618034f * dHz,  fs_);

        const float uA = bode_detail::softClipC1 (x        + gFb * tb, clipK_);
        const float uB = bode_detail::softClipC1 (0.5f * x + gFb * ta, clipK_);
        const float yA = quad_[c][0].process (uA, mBlend_) * ringComp_;
        const float yB = quad_[c][1].process (uB, mBlend_) * ringComp_;

        aux_[c][0][(size_t) aw_[c][0]] = yA; aw_[c][0] = (aw_[c][0] + 1) & auxMask_;
        aux_[c][1][(size_t) aw_[c][1]] = yB; aw_[c][1] = (aw_[c][1] + 1) & auxMask_;

        // keep the main line flushing while Spiral runs (see loopChannel)
        dl_[c][(size_t) w_[c]] = 0.0f; w_[c] = (w_[c] + 1) & mask_;

        return 0.65f * (yA + yB);
    }

    inline float auxRead (int c, int k, float delaySamples) const noexcept
    {
        const int   len  = std::max (1, std::min ((int) delaySamples, auxMask_ - 2));
        const float frac = delaySamples - std::floor (delaySamples);
        const int   r0   = (aw_[c][k] - len)     & auxMask_;
        const int   r1   = (aw_[c][k] - len - 1) & auxMask_;
        return aux_[c][k][(size_t) r0]
             + frac * (aux_[c][k][(size_t) r1] - aux_[c][k][(size_t) r0]);
    }

    Params p_ {};
    float  fs_ = 48000.0f;
    int    type_ = 0;

    BodeQuad            quad_[2][3];
    std::vector<float>  dl_[2];
    int                 w_[2] { 0, 0 }, mask_ = 0;
    std::vector<float>  aux_[2][2];
    int                 aw_[2][2] { { 0, 0 }, { 0, 0 } };
    int                 auxMask_ = 0;
    bode_detail::APDelay blur_[2][4];
    bode_detail::LP2     damp_[2], damp2_[2];
    bode_detail::LP1     lowLp_[2], tone_[2];
    bode_detail::DCBlock dcOut_[2];
    bode_detail::SmoothRandom drift_[2];

    float shiftTarget_ = 0.0f, shiftSm_ = 0.0f;
    float fbTarget_ = 0.0f, fbSm_ = 0.0f;
    float timeTarget_ = 480.0f, timeSm_ = 480.0f;
    float mixT_ = 1.0f, mixSm_ = -1.0f;
    float mBlend_ = 1.0f, ringComp_ = 1.0f, ringDepth_ = 1.0f;
    float clipK_ = 1.0f, postA_ = 0.0f, blurG_ = 0.30f, blurMix_ = 0.0f;
    float touchAmt_ = 0.0f, driftAmt_ = 0.0f, spread_ = -1.0f, inGain_ = 1.0f;
    int   driftStep_ = 4000;
    int   holdN_ = 0, hold_ = 0;
    float relCoef_ = 0.0009f;
    bool  lowKeepOn_ = false, shiftFirst_ = false, shiftLast_ = false;
    bool  routeCross_ = false, routeMono_ = false, routeWide_ = false, guardOn_ = true;
    float env_ = 0.0f, gate_ = 0.0f, mImage_ = 0.0f, mLoop_ = 0.0f;
};

} // namespace tw
