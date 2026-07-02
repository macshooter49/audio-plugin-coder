#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  STELLATE V4 — Terrain's spectral shaper (global mode, sibling of ANNULUS).
//
//  1) PITCH TRACK (the detune killer).  One-shots play at the SAMPLE's pitch,
//     not the key's. The MIDI note is only the initial guess: a phase-slope FLL
//     on the fundamental lock-in walks fTrk onto the real pitch (mono, settled,
//     consistent-error gated — [CC 2026-07-01 tuning fix] preserved verbatim).
//     POLY voices ease to equal temperament so chords stay consonant.
//
//  2) FULL REPLACEMENT (the Contura/Botanica architecture).  No mix. ENGAGED
//     means the output IS the resynthesis, auto-gained to the input's level;
//     Bypass is the only off (bit-exact, 15 ms click-free, analyzers stay warm).
//     AIR dials the RESIDUAL back in (dry − phase-locked reconstruction =
//     transients/breath/noise). MOTION = per-partial organic undulation.
//
//  3) CHARACTER SHAPES (V4 — "night and day").  Twelve strictly-harmonic laws,
//     each with its own TRAITS (attack/release feel, per-harmonic decay,
//     envelope gating, ensemble layers, built-in degradation, drive coupling):
//       SINE      pure fundamental — watery botanica pluck
//       TRIANGLE  odd 1/n² — soft glass
//       SQUARE    odd 1/n — hollow reed
//       SAW       full 1/n — the classic
//       HYPER     dual-layer ensemble saw — supersaw-chorus THICK (no detune:
//                 twin phase-offset layers + deep counter-panned motion)
//       PLUCK     transient-gated, highs decay fast — turns pads into plucks
//       EMBER     odd, spectral tilt breathes with input level
//       VEIL      pure sub-octaves — the deep body
//       CROWN     harmonics 7–16 only, glassy long ring — bellish shine
//       RADIO     band-passed + built-in frame-drops/quantize — digital
//                 breakage, packet-loss lo-fi
//       RAZOR     RISING spectrum (w ∝ m^+), drive-coupled bite — chainsaw
//       GLACIER   full series under glacial smear — frozen pad
//
//  4) SPECTRAL TOOLKIT.  TILT (harmonic filter, XY-X) · LOWPASS + HIGHPASS
//     (true SPECTRAL filters — 8th-order in the harmonic domain; LP is XY-Y,
//     the ear-saver) · SHINE (integer-octave crossfade lift, menu) · WIDTH
//     (per-partial AUTO-PAN chorus motion, not a static split) · FEED (a real
//     DELAY-regeneration loop: 110 ms damped tanh feedback — echoes of
//     harmonics re-enter the analyzers and bloom) · QUALITY (digitalis
//     degradation: slow smeared analyzers + frame SAMPLE-&-HOLD + hard
//     envelope quantize + packet-loss dropouts at the floor).
//
//  5) ENGINE-SIDE RESTING PREVIEW (canonicalized per CC's note): when nothing
//     is live the engine itself publishes the current shape's law (with TILT/
//     SHINE/LP/HP applied) into viz — vizLive=false — so the star is always
//     present and morphs with the pad, no UI templates to re-patch.
//
//  INTEGRATION: prepare(sr)/reset()/process(...) mirrors ResonatorNode.
//  Viz: vizF/vizM/vizN/vizOut + vizLive. Offline: StellateNode_test.cpp.
// ═══════════════════════════════════════════════════════════════════════════════

#include <cmath>
#include <cstring>
#include <cstdint>
#include <algorithm>

namespace wc
{

class StellateNode
{
public:
    static constexpr int kPoly   = 8;
    static constexpr int kPart   = 16;
    static constexpr int kAna    = 12;
    static constexpr int kShapes = 12;
    static constexpr int kViz    = 24;
    static constexpr int kFbMax  = 12000;      // 250 ms @ 48 k — FEED delay line

    float vizF[kViz] = { 0 };
    float vizM[kViz] = { 0 };
    int   vizN       = 0;
    float vizOut     = 0.0f;
    bool  vizLive    = false;                  // false ⇒ resting preview (engine-side)

    void prepare (double sampleRate) noexcept
    {
        sr_ = (sampleRate > 8000.0 ? sampleRate : 48000.0);
        reset();
    }

    void reset() noexcept
    {
        for (int v = 0; v < kPoly; ++v) voices_[v].clear();
        engSm_ = airSm_ = feedSm_ = widSm_ = 0.0f;
        qualSm_ = 0.8f; shineSm_ = 0.5f; tiltSm_ = 0.5f; motSm_ = 0.35f;
        lpSm_ = 1.0f; hpSm_ = 0.0f;
        autoG_ = 1.0f; fbPrev_ = 0.0f; blockT_ = 0.0; blkN_ = 0;
        std::memset (fbBuf_, 0, sizeof (fbBuf_)); fbW_ = 0; fbLp_ = 0.0f;
        primed_ = false;
        vizN = 0; vizOut = 0.0f; vizLive = false;
        for (int i = 0; i < kViz; ++i) { vizF[i] = 0.0f; vizM[i] = 0.0f; }
    }

    // V4 — engaged 0/1 · air/motion/feed/width/quality/tilt/shine/lp/hp 0..1 ·
    //      trackHard 0=TRACK 1=HARD.  lp default 1 (open), hp default 0 (open).
    void process (int shape, int engaged, float air, float motion, float feed, float width,
                  float quality, float tilt, float shine, float lp, float hp, int trackHard,
                  const int* heldNotes, int nHeld, double sr,
                  float* L, float* R, int n) noexcept
    {
        if (n <= 0 || L == nullptr) return;
        if (R == nullptr) R = L;
        if (sr > 8000.0 && std::fabs (sr - sr_) > 1.0) sr_ = sr;

        shape = shape < 0 ? 0 : (shape >= kShapes ? kShapes - 1 : shape);
        const float engT = engaged ? 1.0f : 0.0f;
        const float pc = onePoleCoef (0.012f, n);
        engSm_  += (engT              - engSm_ ) * pc;
        airSm_  += (clamp01 (air)     - airSm_ ) * pc;
        motSm_  += (clamp01 (motion)  - motSm_ ) * pc;
        feedSm_ += (clamp01 (feed)    - feedSm_) * pc;
        widSm_  += (clamp01 (width)   - widSm_ ) * pc;
        qualSm_ += (clamp01 (quality) - qualSm_) * pc;
        tiltSm_ += (clamp01 (tilt)    - tiltSm_) * pc;
        shineSm_+= (clamp01 (shine)   - shineSm_) * pc;
        lpSm_   += (clamp01 (lp)      - lpSm_  ) * pc;
        hpSm_   += (clamp01 (hp)      - hpSm_  ) * pc;
        if (! primed_)   // first block after reset: snap the smoothers to the REAL params. The old
        {                // reset defaults (shine .5 etc.) otherwise glided on every start, and that
                         // spurious glide let transient octave content leak (broke PRIME/FEED).
            engSm_ = engT; airSm_ = clamp01 (air); motSm_ = clamp01 (motion); feedSm_ = clamp01 (feed);
            widSm_ = clamp01 (width); qualSm_ = clamp01 (quality); tiltSm_ = clamp01 (tilt);
            shineSm_ = clamp01 (shine); lpSm_ = clamp01 (lp); hpSm_ = clamp01 (hp); primed_ = true;
        }
        blockT_ += (double) n / sr_; ++blkN_;

        // ── voice ↔ held-note bookkeeping ──
        for (int v = 0; v < kPoly; ++v)
        {
            Voice& vo = voices_[v]; if (vo.note < 0) continue;
            bool still = false;
            for (int h = 0; h < nHeld; ++h) if (heldNotes[h] == vo.note) { still = true; break; }
            vo.active = still;
        }
        for (int h = 0; h < nHeld; ++h)
        {
            const int nn = heldNotes[h]; if (nn < 0 || nn > 127) continue;
            int vi = -1;
            for (int v = 0; v < kPoly; ++v) if (voices_[v].note == nn) { vi = v; break; }
            if (vi >= 0) { voices_[vi].active = true; continue; }
            int free = -1;
            for (int v = 0; v < kPoly; ++v) if (voices_[v].note < 0) { free = v; break; }
            if (free < 0)
            { float lo = 1.0e30f; for (int v = 0; v < kPoly; ++v)
                if (! voices_[v].active && voices_[v].driveSm < lo) { lo = voices_[v].driveSm; free = v; } }
            if (free < 0)
            { float lo = 1.0e30f; for (int v = 0; v < kPoly; ++v)
                if (voices_[v].driveSm < lo) { lo = voices_[v].driveSm; free = v; } }
            Voice& vo = voices_[free];
            const bool renote = (vo.note != nn);
            vo.note = nn; vo.active = true; vo.fade = 1.0f; vo.idleN = 0;
            vo.fMidi = 440.0f * std::pow (2.0f, (float) (nn - 69) / 12.0f);
            if (renote) vo.beginNote (sr_);
        }

        const bool anyLive = anyVoice();
        if (! anyLive)
        {
            vizOut *= 0.85f;
            publishPreview (shape);            // the star rests on the engine's own law
            return;                            // buffers untouched (bit-exact when idle)
        }

        const Traits& tr = traits (shape);

        // QUALITY morphs the temporal grain — V4 "digitalis": analyzer LP 60→6 ms, frame
        // SAMPLE-&-HOLD (targets freeze up to 6 blocks), envelope quantize down to 5 steps,
        // and packet-loss partial DROPOUTS at the floor. Shapes can bias their own grit.
        const float q      = qualSm_;
        const float anaA   = 1.0f - std::exp (-1.0f / (lerp (0.060f, 0.006f, q) * (float) sr_));
        const float atkSec = (shape == kGlacier ? 0.120f : lerp (0.012f, 0.003f, q) * tr.atkMul);
        const float relSec = (shape == kGlacier ? 0.600f : lerp (0.060f, 0.018f, q) * tr.relMul);
        const float atkA   = 1.0f - std::exp (-1.0f / (atkSec * (float) sr_));
        const float fadeA  = 1.0f - std::exp (-1.0f / (0.008f * (float) sr_));
        float qSteps = (q < 0.72f) ? std::floor (lerp (5.0f, 96.0f, q / 0.72f)) : 0.0f;
        if (tr.stepsCap > 0.5f) qSteps = (qSteps < 0.5f) ? tr.stepsCap : std::min (qSteps, tr.stepsCap);
        const int   hold    = 1 + (int) ((1.0f - q) * 5.0f + 0.5f);       // S&H frame length (blocks)
        const bool  freshTg = (blkN_ % hold) == 0;                        // recompute targets this frame?
        const float dropP   = std::min (0.6f, (1.0f - q) * (1.0f - q) * 0.45f + tr.dropBase);
        const float fbG     = 0.45f * feedSm_;                     // loop gain < 1 by construction
        const int   fbTap   = std::min (kFbMax - 2, (int) (0.110 * sr_)); // 110 ms delay regeneration
        const float fbDampA = 1.0f - std::exp (-6.2831853f * 3500.0f / (float) sr_);

        // ── BLOCK PRE-PASS: rebuild laws, then per-partial targets / motion / auto-pan /
        //    per-harmonic release / S&H / dropouts — everything but the sines. ──
        {
            const float blkSec = (float) n / (float) sr_;
            const float motDep = std::min (0.85f, motSm_ * 0.55f * tr.motBoost);
            const float widEff = std::max (widSm_, tr.widFloor);
            for (int v = 0; v < kPoly; ++v)
            {
                Voice& vo = voices_[v]; if (vo.note < 0) continue;
                buildVoice (vo, shape, tr);
                for (int p = 0; p < vo.nPart; ++p)
                {
                    Part& pt = vo.part[p];
                    // MOTION: organic swell + ≤2 c harmonic-centered shimmer
                    pt.uPh += pt.uRt * blkSec; if (pt.uPh >= 1.0f) pt.uPh -= 1.0f;
                    pt.mAmp = 1.0f + motDep * std::sin (6.2831853f * pt.uPh);
                    pt.mInc = 1.0f + motSm_ * 0.00115f * std::sin (6.2831853f * (pt.uPh * 1.61f + 0.37f));
                    // WIDTH: per-partial AUTO-PAN (chorus motion; dual layers counter-pan)
                    pt.pPh += pt.pRt * blkSec; if (pt.pPh >= 1.0f) pt.pPh -= 1.0f;
                    const float pan = widEff * 0.85f * std::sin (6.2831853f * pt.pPh) * (pt.mirror ? -1.0f : 1.0f);
                    const float th = (clampf (pan, -1.0f, 1.0f) + 1.0f) * 0.78539816f;
                    pt.gl = std::cos (th); pt.gr = std::sin (th);
                    // TARGET (frame-rate; S&H holds it on degraded frames)
                    if (freshTg || ! pt.hasT)
                    {
                        const float srcEnv = vo.ana[pt.src].A;
                        float tgt = pt.w * (0.70f * powq (srcEnv * 2.4f, tr.envExp) + tr.driveMix * vo.driveSm);
                        tgt *= pt.mAmp;
                        if (qSteps > 0.5f && tgt > 1.0e-6f) tgt = std::floor (tgt * qSteps) / qSteps;
                        if (dropP > 0.001f && hash01 ((float) ((blkN_ / hold) * 131 + p * 17 + vo.note)) < dropP)
                            tgt = 0.0f;                                   // packet loss — digital breakage
                        pt.tgtB = tgt; pt.hasT = true;
                    }
                    // per-harmonic release (PLUCK: highs die fast; CROWN: long glassy ring)
                    const float rp = relSec / (1.0f + tr.relPerHarm * std::max (0.0f, pt.h - 1.0f));
                    pt.relC = 1.0f - std::exp (-1.0f / (std::max (0.0015f, rp) * (float) sr_));
                }
            }
        }

        float dryE = 0.0f, wetE = 0.0f, wetPeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float dryL = L[i], dryR = R[i];
            const float dryIn = 0.5f * (dryL + dryR);
            // FEED = a real DELAY regeneration: the wet from 110 ms ago, damped and tanh-driven,
            // re-enters the analyzers — echoes of harmonics beget harmonics (bounded by the tanh).
            const float dl = fbBuf_[(fbW_ - fbTap + kFbMax) % kFbMax];
            const float anaIn = dryIn + fbG * fastTanh (dl * 1.2f);
            float wl = 0.0f, wr = 0.0f, recSum = 0.0f;

            for (int v = 0; v < kPoly; ++v)
            {
                Voice& vo = voices_[v]; if (vo.note < 0) continue;

                float drive = 0.0f;
                for (int k = 0; k < kAna; ++k)
                {
                    Ana& an = vo.ana[k];
                    an.ph += an.inc; if (an.ph >= 1.0f) an.ph -= 1.0f;
                    const float w = 6.2831853f * an.ph;
                    const float cw = std::cos (w), sw = std::sin (w);
                    an.I += anaA * (anaIn * cw - an.I);
                    an.Q += anaA * (anaIn * sw - an.Q);
                    an.A  = 2.0f * std::sqrt (an.I * an.I + an.Q * an.Q);
                    an.rec = 2.0f * (an.I * cw + an.Q * sw);
                    if (k == 0)      drive += an.A * an.A;
                    else if (k == 1) drive += 0.5f  * an.A * an.A;
                    else if (k == 2) drive += 0.25f * an.A * an.A;
                }
                drive = std::sqrt (drive);
                vo.driveSm += (drive > vo.driveSm ? atkA : vo.part[0].relC) * (drive - vo.driveSm);

                if (! vo.active && vo.driveSm < 1.0e-4f)
                { if (++vo.idleN > (int) (0.05 * sr_)) vo.fade -= fadeA * vo.fade; }
                else vo.idleN = 0;
                if (vo.fade < 1.0e-3f) { vo.clear(); continue; }

                if (engSm_ > 1.0e-4f)
                {
                    float rc = 0.0f;
                    for (int k = 0; k < kAna; ++k) rc += vo.ana[k].rec;
                    recSum += rc * vo.fade;
                }

                if (engSm_ > 1.0e-4f || vo.wetTrace > 1.0e-6f)
                {
                    const float g = vo.norm * vo.fade;
                    float vl = 0.0f, vr = 0.0f;
                    for (int p = 0; p < vo.nPart; ++p)
                    {
                        Part& pt = vo.part[p];
                        pt.ph += pt.inc * pt.mInc; if (pt.ph >= 1.0f) pt.ph -= 1.0f;
                        pt.amp += (pt.tgtB > pt.amp ? atkA : pt.relC) * (pt.tgtB - pt.amp);
                        if (pt.amp < 1.0e-6f) continue;
                        const float s = std::sin (6.2831853f * pt.ph) * pt.amp * g;
                        vl += s * pt.gl; vr += s * pt.gr;
                    }
                    wl += vl; wr += vr;
                    vo.wetTrace = std::fabs (vl) + std::fabs (vr);
                }
            }

            wl = softLimit (wl); wr = softLimit (wr);
            if (! (wl == wl)) wl = 0.0f;
            if (! (wr == wr)) wr = 0.0f;

            const float wetL  = wl * autoG_, wetR = wr * autoG_;
            const float resid = dryIn - recSum;
            L[i] = dryL + engSm_ * ((wetL + airSm_ * resid) - dryL);
            R[i] = dryR + engSm_ * ((wetR + airSm_ * resid) - dryR);

            const float wm = 0.5f * (wl + wr);
            fbLp_ += fbDampA * (wm - fbLp_);                      // damped write into the delay loop
            fbBuf_[fbW_] = fbLp_; fbW_ = (fbW_ + 1) % kFbMax;

            dryE += dryIn * dryIn; wetE += wm * wm;
            const float aw = std::fabs (wetL) + std::fabs (wetR);
            if (aw > wetPeak) wetPeak = aw;
        }

        // [CC 2026-07-01 tuning fix] The per-voice FLL cannot separate a CHORD's overlapping
        // partials — in polyphony (or with vibrato/pitch-drift input) each voice chased phase
        // noise and walked off ET, so the shared harmonics that make a chord consonant stopped
        // lining up = gross dissonance (measured 57c RMS on a clean triad; HARD measured 0.1c).
        // Fix: POLY locks every voice to equal temperament (consonant, matches HARD). The FLL —
        // the one-shot detune corrector — runs ONLY for a single sustained MONO voice, and only
        // after it has settled past the attack AND the frequency error is CONSISTENT (rejecting
        // the transient phase noise that made V2 diverge).
        if (trackHard == 0)
        {
            int nAct = 0; for (int v = 0; v < kPoly; ++v) if (voices_[v].note >= 0 && voices_[v].active) ++nAct;
            const float dt     = (float) n / (float) sr_;
            const float easeET = 1.0f - std::exp (-dt / 0.10f);          // poly: glide held voices to ET
            for (int v = 0; v < kPoly; ++v)
            {
                Voice& vo = voices_[v]; if (vo.note < 0) continue;
                if (nAct > 1)                                            // POLY → equal temperament
                {
                    if (std::fabs (vo.trkSemi) > 1.0e-4f) { vo.trkSemi -= vo.trkSemi * easeET; vo.retune (sr_); }
                    vo.havePhi = false; vo.fllHold = 0; vo.errRun = 0; continue;
                }
                if (! vo.active) { vo.havePhi = false; continue; }       // released tail → hold pitch
                // ── MONO active voice: robust one-shot pitch lock ──
                const Ana& an = vo.ana[0];
                if (an.A < 3.0e-3f) { vo.havePhi = false; vo.fllHold = 0; vo.errRun = 0; continue; }
                if (vo.fllHold < 6) { ++vo.fllHold; vo.prevPhi = std::atan2 (an.Q, an.I); vo.havePhi = true; continue; }
                const float phi = std::atan2 (an.Q, an.I);
                if (vo.havePhi)
                {
                    float d = phi - vo.prevPhi;
                    while (d >  3.14159265f) d -= 6.2831853f;
                    while (d < -3.14159265f) d += 6.2831853f;
                    const float fErr = -d / (6.2831853f * dt);           // Hz above (+) / below (−) fTrk
                    if (std::fabs (fErr) > 0.10f)
                    {
                        const float sgn = (fErr > 0.0f) ? 1.0f : -1.0f;
                        if (sgn == vo.errSgn) ++vo.errRun; else { vo.errSgn = sgn; vo.errRun = 1; }
                        if (vo.errRun >= 3)                              // consistent = a real pitch offset
                        {
                            const float ft = vo.fTrk();
                            float stStep = 12.0f * std::log2 (std::max (1.0e-3f, (ft + fErr) / ft));
                            stStep = std::max (-0.6f, std::min (0.6f, stStep)) * 0.4f;   // gentle slew
                            vo.trkSemi = std::max (-9.0f, std::min (9.0f, vo.trkSemi + stStep));
                            vo.retune (sr_);
                            vo.havePhi = false; vo.errRun = 0;           // reference broken by retune
                            continue;
                        }
                    }
                    else vo.errRun = 0;
                }
                vo.prevPhi = phi; vo.havePhi = true;
            }
        }

        // auto-gain: slow LEVELER (~0.6 s) — tracks program level, never the motion
        if (dryE > 1.0e-7f && wetE > 1.0e-7f)
        {
            float gT = 0.85f * std::sqrt (dryE / wetE);            // target just UNDER the dry level
            gT = gT < 0.30f ? 0.30f : (gT > 3.0f ? 3.0f : gT);
            autoG_ += (gT - autoG_) * 0.012f;
        }
        vizOut += (clamp01 (wetPeak * engSm_ * 1.4f) - vizOut) * 0.30f;
        exportViz (shape);
        flushVoices();
        if (std::fabs (fbLp_) < 1.0e-20f) fbLp_ = 0.0f;
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    enum { kSine = 0, kTriangle, kSquare, kSaw, kHyper, kPluck, kEmber, kVeil,
           kCrown, kRadio, kRazor, kGlacier };

    // per-shape CHARACTER — this is what makes them night-and-day
    struct Traits
    {
        float atkMul = 1, relMul = 1, relPerHarm = 0;   // envelope feel (× the quality-morphed base)
        float envExp = 1, driveMix = 0.30f;             // source-envelope gating vs global drive
        float motBoost = 1, widFloor = 0;               // ensemble motion / minimum auto-pan
        float dropBase = 0, stepsCap = 0;               // built-in digital breakage (RADIO)
        bool  dual = false;                             // twin phase-offset counter-panned layers
    };
    static const Traits& traits (int s) noexcept
    {
        static const Traits T[kShapes] = {
            /*SINE*/    { 1,1,0,      1,0.30f, 1.2f,0,    0,0,  false },
            /*TRIANGLE*/{ 1,1,0,      1,0.30f, 1,0,       0,0,  false },
            /*SQUARE*/  { 1,1,0,      1,0.30f, 1,0,       0,0,  false },
            /*SAW*/     { 1,1,0,      1,0.30f, 1,0,       0,0,  false },
            /*HYPER*/   { 1,1.2f,0,   1,0.30f, 2.4f,0.55f,0,0,  true  },
            /*PLUCK*/   { 0.5f,0.45f,0.55f, 1.7f,0.12f, 1,0, 0,0, false },
            /*EMBER*/   { 1,1,0,      1,0.30f, 1,0,       0,0,  false },
            /*VEIL*/    { 1,1.2f,0,   1,0.30f, 1.3f,0,    0,0,  false },
            /*CROWN*/   { 1,1.5f,0,   1,0.30f, 1.7f,0.15f,0,0,  false },
            /*RADIO*/   { 0.8f,0.7f,0,1,0.30f, 1,0,   0.22f,9,  false },
            /*RAZOR*/   { 0.4f,0.8f,0,1,0.34f, 1,0.10f,  0,24,  false },
            /*GLACIER*/ { 1,1,0,      1,0.30f, 1,0,       0,0,  false } };
        return T[s < 0 ? 0 : (s >= kShapes ? kShapes - 1 : s)];
    }

    struct Ana  { float ph = 0, inc = 0, I = 0, Q = 0, A = 0, rec = 0; };
    struct Part { float ph = 0, inc = 0, w = 0, amp = 0, gl = 0.7071f, gr = 0.7071f;
                  int   src = 0; float h = 1;                    // effective harmonic number (LP/HP, release)
                  float uPh = 0, uRt = 0.6f, mAmp = 1, mInc = 1; // MOTION
                  float pPh = 0, pRt = 0.2f; bool mirror = false;// WIDTH auto-pan
                  float tgtB = 0, relC = 0.01f; bool hasT = false; };

    struct Voice
    {
        int   note = -1; bool active = false;
        float fMidi = 261.63f, trkSemi = 0.0f;
        float driveSm = 0.0f, norm = 1.0f, fade = 1.0f, wetTrace = 0.0f;
        int   nPart = 0, idleN = 0;
        Ana   ana[kAna];
        float prevPhi = 0.0f; bool havePhi = false;
        int   fllHold = 0, errRun = 0; float errSgn = 0.0f;
        Part  part[kPart];

        float fTrk() const noexcept { return fMidi * std::pow (2.0f, trkSemi / 12.0f); }
        void retune (double sr) noexcept
        {
            const float f = fTrk();
            for (int k = 0; k < kAna; ++k) ana[k].inc = (float) ((double) f * (k + 1) / sr);
        }
        void beginNote (double sr) noexcept
        {
            trkSemi = 0.0f; havePhi = false; fllHold = 0; errRun = 0; errSgn = 0.0f;
            for (int k = 0; k < kAna; ++k) { ana[k].I = ana[k].Q = ana[k].A = 0.0f; }
            retune (sr);
            driveSm = 0.0f; wetTrace = 0.0f;
        }
        void clear() noexcept
        {
            note = -1; active = false; driveSm = 0.0f; fade = 1.0f; wetTrace = 0.0f;
            nPart = 0; idleN = 0; trkSemi = 0.0f; havePhi = false; fllHold = 0; errRun = 0; errSgn = 0.0f;
            for (int k = 0; k < kAna; ++k) { ana[k].I = ana[k].Q = ana[k].A = ana[k].rec = 0.0f; }
            for (int p = 0; p < kPart; ++p) { part[p].amp = 0.0f; part[p].w = 0.0f; part[p].hasT = false; }
        }
    };

    // ── the twelve strictly-harmonic LAWS (ratio, weight) — shared by voices AND preview ──
    template <class F>
    void buildLaw (int shape, float driveSm, F&& put) const noexcept
    {
        switch (shape)
        {
            case kSine:     put (1.0f, 1.0f); break;
            case kTriangle: for (int m = 1; m <= 11; m += 2) put ((float) m, 1.0f / ((float) m * (float) m)); break;
            case kSquare:   for (int m = 1; m <= 15; m += 2) put ((float) m, 1.0f / (float) m); break;
            case kSaw:      for (int m = 1; m <= 14; ++m)    put ((float) m, 1.0f / (float) m); break;
            case kHyper:    for (int m = 1; m <= 8;  ++m)    put ((float) m, 1.0f / std::pow ((float) m, 0.75f)); break;
            case kPluck:    for (int m = 1; m <= 12; ++m)    put ((float) m, 1.0f / std::pow ((float) m, 0.55f)); break;
            case kEmber:
            { const float s = 2.4f - 1.6f * std::min (1.0f, driveSm * 2.8f);
              for (int m = 1; m <= 13; m += 2) put ((float) m, 1.0f / std::pow ((float) m, s));
              break; }
            case kVeil:     put (1.0f, 1.0f); put (0.5f, 0.9f); put (0.25f, 0.7f); put (2.0f, 0.35f); break;
            case kCrown:
                for (int m = 7; m <= 16; ++m) put ((float) m, ((m & 1) ? 1.15f : 0.70f) / (float) (m - 5));
                break;
            case kRadio:
                for (int m = 2; m <= 9;  ++m) put ((float) m, 1.0f / (1.0f + std::fabs ((float) m - 5.0f) * 0.35f));
                break;
            case kRazor:
            { const float bite = 0.30f + 0.25f * std::min (1.0f, driveSm * 2.8f);   // drive-coupled rise
              for (int m = 1; m <= 16; ++m) put ((float) m, std::pow ((float) m, bite) * ((m & 1) ? 1.0f : 0.72f));
              break; }
            case kGlacier:
            default:        for (int m = 1; m <= 10; ++m)    put ((float) m, 1.0f / (float) m); break;
        }
    }

    // TILT + SPECTRAL LP/HP, applied in the harmonic domain (h = effective harmonic number).
    float spectralWeight (float h) const noexcept
    {
        const float tiltA = (tiltSm_ - 0.5f) * 4.4f;
        float w = std::pow (std::max (0.25f, h), tiltA);
        const float hLP = std::pow (48.0f, lpSm_);                       // 1 → 48: LOWPASS opens up
        const float hHP = 0.5f * std::pow (24.0f, hpSm_);                // 0.5 → 12: HIGHPASS guts lows
        const float rl = h / hLP; const float rl4 = rl * rl * rl * rl;
        w /= (1.0f + rl4 * rl4);                                         // 8th-order spectral lowpass
        const float rh = hHP / std::max (0.05f, h); const float rh4 = rh * rh * rh * rh;
        w /= (1.0f + rh4 * rh4);                                         // 8th-order spectral highpass
        return w;
    }

    void buildVoice (Voice& vo, int shape, const Traits& tr) noexcept
    {
        const float f0 = vo.fTrk();
        const float ny = 0.45f * (float) sr_;
        // SHINE = whole-octave lift, CROSSFADED between adjacent integer octaves (×1/2/4) so the
        // stack is strictly harmonic at EVERY shine yet glides with no pitch jump. A plain
        // continuous 2^(shine·2) multiplier detuned every partial off the integer grid (inharmonic);
        // a round() jumped octaves mid-glide (disturbed FEED). At the octave points (shine 0→×1,
        // 0.5→×2) blend=0 → a single layer, byte-identical to the law-exact spectra.
        // [CC 2026-07-01 tuning fix — see DROP notes]
        const float s2    = shineSm_ * 2.0f;
        const float loP   = std::floor (s2);
        const float octLo = std::exp2 (loP);
        const float octHi = std::exp2 (loP + 1.0f);
        const float blend = s2 - loP;
        int c = 0;
        auto emit = [&] (float f, float w, int srcIdx, float hEff, bool mirror)
        {
            if (c >= kPart || w < 1.0e-4f) return;
            if (f < 18.0f || f > ny) return;
            Part& p = vo.part[c];
            const float oldInc = p.inc;
            p.inc = (float) ((double) f / sr_);
            p.w   = w;
            p.src = srcIdx; p.h = hEff; p.mirror = mirror;
            if (oldInc <= 0.0f)
            {
                p.ph = mirror ? 0.25f : 0.0f;                             // HYPER twin: quadrature offset
                const float seed = (float) (c * 13 + vo.note);
                p.uPh = frac01 (std::sin (seed * 12.9898f) * 43758.5f);
                p.uRt = 0.35f + 1.25f * frac01 (std::sin (seed * 78.233f) * 12543.1f);
                p.pPh = frac01 (std::sin (seed * 39.425f) * 26743.7f);
                p.pRt = 0.35f + 1.00f * frac01 (std::sin (seed * 91.113f) * 9137.3f);
                p.hasT = false;
            }
            ++c;
        };
        auto add = [&] (float ratio, float w)
        {
            const int srcIdx = std::min (kAna - 1, std::max (0, (int) std::lround (ratio) - 1));
            const float wLo = w * spectralWeight (ratio * octLo) * (1.0f - blend);
            const float wHi = w * spectralWeight (ratio * octHi) * blend;
            if (tr.dual)
            {
                emit (f0 * ratio * octLo, wLo * 0.62f, srcIdx, ratio * octLo, false);
                emit (f0 * ratio * octLo, wLo * 0.62f, srcIdx, ratio * octLo, true);
                if (blend > 1.0e-3f)
                    emit (f0 * ratio * octHi, wHi * 0.62f, srcIdx, ratio * octHi, ((c & 1) != 0));
            }
            else
            {
                emit (f0 * ratio * octLo, wLo, srcIdx, ratio * octLo, false);
                if (blend > 1.0e-3f) emit (f0 * ratio * octHi, wHi, srcIdx, ratio * octHi, false);
            }
        };
        buildLaw (shape, vo.driveSm, add);
        for (int p = c; p < vo.nPart; ++p) vo.part[p].w = 0.0f;
        if (c > vo.nPart) vo.nPart = c;
        else if (c < vo.nPart)
        { bool quiet = true;
          for (int p = c; p < vo.nPart; ++p) if (vo.part[p].amp > 1.0e-5f) { quiet = false; break; }
          if (quiet) vo.nPart = c; }
        else vo.nPart = c;

        float ss = 0.0f; for (int p = 0; p < c; ++p) ss += vo.part[p].w * vo.part[p].w;
        vo.norm = 1.10f / std::sqrt (ss > 1.0e-9f ? ss : 1.0f);
    }

    bool anyVoice() const noexcept
    { for (int v = 0; v < kPoly; ++v) if (voices_[v].note >= 0) return true; return false; }

    // ── ENGINE-SIDE RESTING PREVIEW: the current law, TILT/SHINE/LP/HP applied, at C3 ──
    void publishPreview (int shape) noexcept
    {
        const float f0 = 130.81f;
        const float s2 = shineSm_ * 2.0f, loP = std::floor (s2);
        const float octLo = std::exp2 (loP), octHi = std::exp2 (loP + 1.0f), blend = s2 - loP;
        float tf[kViz], tm[kViz]; int c = 0; float mx = 1.0e-9f;
        auto put = [&] (float ratio, float w)
        {
            auto lay = [&] (float oct, float bw)
            { if (c >= kViz || bw < 1.0e-4f) return;
              const float wq = w * bw * spectralWeight (ratio * oct);
              if (wq < 1.0e-4f) return;
              tf[c] = f0 * ratio * oct; tm[c] = wq; if (wq > mx) mx = wq; ++c; };
            lay (octLo, 1.0f - blend);
            if (blend > 1.0e-3f) lay (octHi, blend);
        };
        buildLaw (shape, 0.5f, put);
        vizN = c; vizLive = false;
        for (int q2 = 0; q2 < c; ++q2) { vizF[q2] = tf[q2]; vizM[q2] = clamp01 (0.85f * tm[q2] / mx); }
    }

    void exportViz (int shape) noexcept
    {
        struct E { float f, m; };
        E top[kViz]; int nT = 0;
        for (int v = 0; v < kPoly; ++v)
        { const Voice& vo = voices_[v]; if (vo.note < 0) continue;
          for (int p = 0; p < vo.nPart; ++p)
          { const float m = vo.part[p].amp * vo.norm * vo.fade;
            if (m < 2.0e-4f) continue;
            const float f = vo.part[p].inc * (float) sr_;
            if (nT < kViz) { top[nT].f = f; top[nT].m = m; ++nT; }
            else { int lo = 0; for (int q2 = 1; q2 < kViz; ++q2) if (top[q2].m < top[lo].m) lo = q2;
                   if (m > top[lo].m) { top[lo].f = f; top[lo].m = m; } } } }
        if (nT == 0) { publishPreview (shape); return; }                 // quiet ⇒ the star rests on the law
        vizN = nT; vizLive = true;
        for (int q2 = 0; q2 < nT; ++q2) { vizF[q2] = top[q2].f; vizM[q2] = clamp01 (top[q2].m * 1.6f); }
    }

    void flushVoices() noexcept
    {
        for (int v = 0; v < kPoly; ++v)
        { Voice& vo = voices_[v]; if (vo.note < 0) continue;
          for (int k = 0; k < kAna; ++k)
          { if (std::fabs (vo.ana[k].I) < 1.0e-20f) vo.ana[k].I = 0.0f;
            if (std::fabs (vo.ana[k].Q) < 1.0e-20f) vo.ana[k].Q = 0.0f; }
          for (int p = 0; p < vo.nPart; ++p)
            if (vo.part[p].amp < 1.0e-12f) vo.part[p].amp = 0.0f; }
    }

    static float clamp01 (float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
    static float clampf (float x, float a, float b) noexcept { return x < a ? a : (x > b ? b : x); }
    static float frac01 (float x) noexcept { return x - std::floor (x); }
    static float hash01 (float x) noexcept { return frac01 (std::sin (x * 12.9898f) * 43758.5453f); }
    static float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
    static float powq (float x, float e) noexcept
    { return (e == 1.0f) ? x : std::pow (std::max (0.0f, x), e); }
    static float onePoleCoef (float sec, int n) noexcept
    { return 1.0f - std::exp (-(float) n / (sec * 48000.0f)); }
    static float fastTanh (float x) noexcept
    { if (x >  3.0f) return  1.0f; if (x < -3.0f) return -1.0f;
      const float x2 = x * x; return x * (27.0f + x2) / (27.0f + 9.0f * x2); }
    // Safety limiter that is IDENTITY below ±0.75 — the resynthesis must stay spectrally
    // pure at working levels (an always-on tanh added odd-harmonic distortion); only the
    // overload region blends into tanh (C1-continuous).
    static float softLimit (float x) noexcept
    {
        const float t = 0.75f, ax = x < 0.0f ? -x : x;
        if (ax <= t) return x;
        const float y = t + (1.0f - t) * fastTanh ((ax - t) / (1.0f - t));
        return x < 0.0f ? -y : y;
    }

    Voice   voices_[kPoly];
    double  sr_ = 48000.0, blockT_ = 0.0;
    long    blkN_ = 0;
    float   engSm_ = 0.0f, airSm_ = 0.0f, motSm_ = 0.35f, feedSm_ = 0.0f, widSm_ = 0.0f, autoG_ = 1.0f;
    float   qualSm_ = 0.8f, tiltSm_ = 0.5f, shineSm_ = 0.5f, lpSm_ = 1.0f, hpSm_ = 0.0f;
    float   fbPrev_ = 0.0f;
    float   fbBuf_[kFbMax] = { 0 }; int fbW_ = 0; float fbLp_ = 0.0f;
    bool    primed_ = false;
};

} // namespace wc
