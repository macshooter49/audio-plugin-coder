#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  STELLATE V2 — Terrain's spectral shaper (global mode, sibling of ANNULUS).
//
//  V2 = the "actually spectral" rework, built from Max's one-shot session:
//
//  1) PITCH TRACK (the detune killer).  One-shots play at the SAMPLE's pitch,
//     not the key's — V1 locked the stack to the MIDI frequency, so mis-rooted
//     chops beat against it. V2 MEASURES the true fundamental: the MIDI note is
//     only the initial guess; tri-probe lock-in gradient walks fTrk onto the
//     real pitch (capture ±9 semitones, slew-limited). The stack is always in
//     tune WITH THE AUDIO. Menu offers TRACK (default) / HARD (strict MIDI).
//
//  2) CARVE + REPLACE (boost AND take away).  The lock-in bank measures each
//     source harmonic's amplitude AND phase, so V2 can phase-coherently
//     SUBTRACT the source's harmonics (REPLACE amount) and pour the shaped
//     stack in their place. At Mix 1 / Replace 1 the harmonic content is fully
//     swapped for the shape — unmistakably spectral. Transients pass through
//     (the 6–40 ms analyzer lag can't cancel attacks) so punch survives.
//
//  3) SOURCE-ENVELOPE PARTIALS (organ killer).  Every generated partial rides
//     the measured envelope of its OWN source harmonic (blended with global
//     drive), so the stack breathes exactly like the input — chops ripple,
//     pads bloom, nothing sits static.
//
//  4) STRICTLY HARMONIC — "not one bit of inharmonic".  Every shape is integer
//     ratios of fTrk only (plus pure sub-octaves in VEIL). Golden-ratio and
//     detuned-pair laws are gone. Harness audits every exported partial
//     against the integer grid.
//
//  5) THE SPECTRAL TOOLKIT.  TILT (harmonic lo↔hi filter — XY pad X),
//     SHINE (0..+2 octave lift for high artifacts — XY pad Y, default +1),
//     WIDTH (odd/even partials spread L/R), FEED (wet re-enters the analyzers:
//     harmonics-of-harmonics regeneration), QUALITY (the degradation: low =
//     slow smeary analyzers + fewer partials + quantized/steppy harmonic
//     envelopes — that "bit-rate" grit; high = glassy and fast).
//
//  TWELVE SHAPES (basics = public-domain Fourier series; rest = Terrain laws)
//    SINE     fundamental only            TRIANGLE odd, 1/n²
//    SQUARE   odd, 1/n                    SAW      all, 1/n
//    PRIME    prime-number harmonics      LATTICE  3-limit lattice (2^a·3^b)
//    VEIL     sub-octave undertones       HALO     pure-octave bloom above
//    EMBER    odd, tilt breathes w/level  CROWN    high harmonics 5..13 (shine)
//    PILLAR   octave+fifth power stack    GLACIER  full series, glacial smear
//
//  INTEGRATION: prepare(sr)/reset()/process(...) mirrors ResonatorNode; wet is
//  additive+carve so Mix 0 = bit-exact bypass. Viz: vizF/vizM/vizN/vizOut.
//  Offline-validated: StellateNode_test.cpp — see DROP doc for the count.
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
    static constexpr int kPart   = 16;   // max generated partials per voice
    static constexpr int kAna    = 12;   // measured source harmonics per voice (amp+phase)
    static constexpr int kShapes = 12;
    static constexpr int kViz    = 24;

    float vizF[kViz] = { 0 };
    float vizM[kViz] = { 0 };
    int   vizN       = 0;
    float vizOut     = 0.0f;

    void prepare (double sampleRate) noexcept
    {
        sr_ = (sampleRate > 8000.0 ? sampleRate : 48000.0);
        reset();
    }

    void reset() noexcept
    {
        for (int v = 0; v < kPoly; ++v) voices_[v].clear();
        mixSm_ = repSm_ = feedSm_ = widSm_ = qualSm_ = tiltSm_ = shineSm_ = 0.0f;
        qualSm_ = 0.8f; shineSm_ = 0.5f; tiltSm_ = 0.5f;
        primed_ = false;
        fbL_ = fbR_ = 0.0f; blockT_ = 0.0;
        vizN = 0; vizOut = 0.0f;
        for (int i = 0; i < kViz; ++i) { vizF[i] = 0.0f; vizM[i] = 0.0f; }
    }

    // shape 0..11 · mix/replace/feed/width/quality/tilt/shine 0..1 · trackHard 0=TRACK 1=HARD
    void process (int shape, float mix, float replace, float feed, float width,
                  float quality, float tilt, float shine, int trackHard,
                  const int* heldNotes, int nHeld, double sr,
                  float* L, float* R, int n) noexcept
    {
        if (n <= 0 || L == nullptr) return;
        if (R == nullptr) R = L;
        if (sr > 8000.0 && std::fabs (sr - sr_) > 1.0) sr_ = sr;

        shape = shape < 0 ? 0 : (shape >= kShapes ? kShapes - 1 : shape);
        const float pc = onePoleCoef (0.012f, n);
        mixSm_  += (clamp01 (mix)     - mixSm_ ) * pc;
        repSm_  += (clamp01 (replace) - repSm_ ) * pc;
        feedSm_ += (clamp01 (feed)    - feedSm_) * pc;
        widSm_  += (clamp01 (width)   - widSm_ ) * pc;
        qualSm_ += (clamp01 (quality) - qualSm_) * pc;
        tiltSm_ += (clamp01 (tilt)    - tiltSm_) * pc;
        shineSm_+= (clamp01 (shine)   - shineSm_) * pc;
        if (! primed_)   // first block after reset: snap the smoothers to the REAL params. The old
        {                // reset defaults (shine .5 etc.) otherwise glided on every start, and that
                         // spurious glide let transient octave content leak (broke PRIME/FEED).
            mixSm_ = clamp01 (mix); repSm_ = clamp01 (replace); feedSm_ = clamp01 (feed);
            widSm_ = clamp01 (width); qualSm_ = clamp01 (quality); tiltSm_ = clamp01 (tilt);
            shineSm_ = clamp01 (shine); primed_ = true;
        }
        blockT_ += (double) n / sr_;

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

        const bool anyLive = rebuildAll (shape);
        if (! anyLive) { vizN = 0; vizOut *= 0.85f; fbL_ = fbR_ = 0.0f; return; }

        // QUALITY morphs the whole engine's temporal grain:
        //   analyzer LP 40 ms → 6 ms · amp attack 12→3 ms / release 60→18 ms · envelope
        //   quantized to 9 → ∞ steps (the "bit-rate" degradation on the harmonic envelopes)
        const float q     = qualSm_;
        const float anaA  = 1.0f - std::exp (-1.0f / (lerp (0.040f, 0.006f, q) * (float) sr_));
        const float atkA  = 1.0f - std::exp (-1.0f / ((shape == kGlacier ? 0.120f : lerp (0.012f, 0.003f, q)) * (float) sr_));
        const float relA  = 1.0f - std::exp (-1.0f / ((shape == kGlacier ? 0.600f : lerp (0.060f, 0.018f, q)) * (float) sr_));
        const float fadeA = 1.0f - std::exp (-1.0f / (0.008f * (float) sr_));
        const float qSteps = (q < 0.72f) ? std::floor (lerp (9.0f, 96.0f, q / 0.72f)) : 0.0f;   // 0 = off
        const float fbG    = 0.62f * feedSm_;                       // regeneration, stability-clamped

        float wetPeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float dryIn = 0.5f * (L[i] + R[i]);
            const float anaIn = dryIn + fbG * fbPrev_;              // FEED: wet re-enters the ear
            float wl = 0.0f, wr = 0.0f, carve = 0.0f;

            for (int v = 0; v < kPoly; ++v)
            {
                Voice& vo = voices_[v]; if (vo.note < 0) continue;

                // ── lock-in bank: amplitude + phase of source harmonics at k·fTrk ──
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
                    // phase-coherent reconstruction of THIS source harmonic (for CARVE)
                    an.rec = 2.0f * (an.I * cw + an.Q * sw);
                    if (k == 0)      drive += an.A * an.A;
                    else if (k == 1) drive += 0.5f  * an.A * an.A;
                    else if (k == 2) drive += 0.25f * an.A * an.A;
                }
                drive = std::sqrt (drive);
                vo.driveSm += (drive > vo.driveSm ? atkA : relA) * (drive - vo.driveSm);

                if (! vo.active && vo.driveSm < 1.0e-4f)
                { if (++vo.idleN > (int) (0.05 * sr_)) vo.fade -= fadeA * vo.fade; }
                else vo.idleN = 0;
                if (vo.fade < 1.0e-3f) { vo.clear(); continue; }

                // ── CARVE: subtract the source's own harmonics (phase-locked) ──
                if (repSm_ > 1.0e-3f)
                {
                    float rc = 0.0f;
                    for (int k = 0; k < kAna; ++k) rc += vo.ana[k].rec;
                    carve += rc * vo.fade;
                }

                // ── REPLACE: the shape's partial bank, riding the source envelopes ──
                if (mixSm_ > 1.0e-4f || vo.wetTrace > 1.0e-6f)
                {
                    const float g = vo.norm * vo.fade;
                    float vl = 0.0f, vr = 0.0f;
                    for (int p = 0; p < vo.nPart; ++p)
                    {
                        Part& pt = vo.part[p];
                        pt.ph += pt.inc; if (pt.ph >= 1.0f) pt.ph -= 1.0f;
                        const float srcEnv = vo.ana[pt.src].A;                    // its OWN harmonic's envelope
                        float tgt = pt.w * (0.70f * srcEnv * 2.4f + 0.30f * vo.driveSm);
                        if (qSteps > 0.5f && tgt > 1.0e-6f)                       // DEGRADE: steppy envelopes
                            tgt = std::floor (tgt * qSteps) / qSteps;
                        pt.amp += (tgt > pt.amp ? atkA : relA) * (tgt - pt.amp);
                        if (pt.amp < 1.0e-6f) continue;
                        const float s = std::sin (6.2831853f * pt.ph) * pt.amp * g;
                        vl += s * pt.gl; vr += s * pt.gr;
                    }
                    wl += vl; wr += vr;
                    vo.wetTrace = std::fabs (vl) + std::fabs (vr);
                }
            }

            wl = fastTanh (wl); wr = fastTanh (wr);
            const float cv = fastTanh (carve);
            if (! (wl == wl)) wl = 0.0f;
            if (! (wr == wr)) wr = 0.0f;

            // out = dry − mix·replace·(source harmonics) + mix·(shaped stack)
            const float sub = mixSm_ * repSm_ * cv;
            L[i] = L[i] - sub + mixSm_ * wl;
            R[i] = R[i] - sub + mixSm_ * wr;
            fbPrev_ = 0.5f * (wl + wr);

            const float aw = std::fabs (wl) + std::fabs (wr);
            if (aw > wetPeak) wetPeak = aw;
        }

        // ── PITCH TRACK (block-rate FLL): the fundamental lock-in's I/Q pair rotates at
        //    exactly (f_audio − fTrk) Hz — the phase slope IS the frequency error. Measure
        //    it, step fTrk toward the true pitch (slewed, ±9 st capture around the MIDI
        //    note), and the whole stack lands on the AUDIO's pitch — mis-rooted one-shots
        //    included. HARD mode skips this and stays on the strict MIDI grid. ──
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

        vizOut += (clamp01 (wetPeak * mixSm_ * 1.4f) - vizOut) * 0.30f;
        exportViz();
        flushVoices();
        if (std::fabs (fbPrev_) < 1.0e-20f) fbPrev_ = 0.0f;
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    enum { kSine = 0, kTriangle, kSquare, kSaw, kPrime, kLattice, kVeil, kHalo,
           kEmber, kCrown, kPillar, kGlacier };

    struct Ana  { float ph = 0.0f, inc = 0.0f, I = 0.0f, Q = 0.0f, A = 0.0f, rec = 0.0f; };
    struct Part { float ph = 0.0f, inc = 0.0f, w = 0.0f, amp = 0.0f, gl = 0.7071f, gr = 0.7071f; int src = 0; };

    struct Voice
    {
        int   note = -1; bool active = false;
        float fMidi = 261.63f, trkSemi = 0.0f;        // fTrk = fMidi · 2^(trkSemi/12)
        float driveSm = 0.0f, norm = 1.0f, fade = 1.0f, wetTrace = 0.0f;
        int   nPart = 0, idleN = 0;
        Ana   ana[kAna];
        float prevPhi = 0.0f; bool havePhi = false;    // FLL phase reference (pitch tracking)
        int   fllHold = 0, errRun = 0; float errSgn = 0.0f;  // mono-FLL settle + consistent-error gate
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
            for (int p = 0; p < kPart; ++p) { part[p].amp = 0.0f; part[p].w = 0.0f; }
        }
    };

    // ── the twelve strictly-harmonic shape laws ──────────────────────────────
    // Ratios are INTEGER multiples of fTrk (VEIL: pure sub-octaves 1/2,1/4) —
    // "not one bit of inharmonic". TILT filters the harmonics lo↔hi, SHINE
    // lifts the whole stack 0..+2 octaves, WIDTH fans odd/even partials L/R.
    void buildVoice (Voice& vo, int shape) noexcept
    {
        const float f0 = vo.fTrk();
        const float ny = 0.45f * (float) sr_;
        // SHINE = whole-octave lift, CROSSFADED between adjacent integer octaves (×1/2/4) so the
        // stack is strictly harmonic at EVERY shine yet glides with no pitch jump. A plain
        // continuous 2^(shine·2) multiplier detuned every partial off the integer grid (inharmonic);
        // a round() jumped octaves mid-glide (disturbed FEED). At the octave points (shine 0→×1,
        // 0.5→×2) blend=0 → a single layer, byte-identical to the law-exact spectra.
        // [CC 2026-07-01 tuning fix — see DROP notes]
        const float s2    = shineSm_ * 2.0f;                        // 0..2 octaves
        const float loP   = std::floor (s2);
        const float octLo = std::exp2 (loP);                        // ×1, ×2 or ×4
        const float octHi = std::exp2 (loP + 1.0f);                 // the next octave up
        const float blend = s2 - loP;                               // 0..1 crossfade (0 at octave points)
        const float tiltA = (tiltSm_ - 0.5f) * 4.4f;                // lo ↔ hi harmonic filter
        int c = 0;
        auto emit = [&] (float f, float w, int srcIdx)               // one strictly-harmonic partial
        {
            if (c >= kPart || w < 1.0e-4f) return;
            if (f < 18.0f || f > ny) return;
            Part& p = vo.part[c];
            const float oldInc = p.inc;
            p.inc = (float) ((double) f / sr_);
            p.w   = w;
            p.src = srcIdx;
            const float pan = widSm_ * ((c & 1) ? -0.75f : 0.75f);   // WIDTH: fan odd/even
            const float th = (pan + 1.0f) * 0.78539816f;
            p.gl = std::cos (th); p.gr = std::sin (th);
            if (oldInc <= 0.0f) p.ph = 0.0f;
            ++c;
        };
        auto add = [&] (float ratio, float w)                        // ratio vs fTrk (pre-shine)
        {
            w *= std::pow (std::max (0.25f, ratio), tiltA);          // TILT in the harmonic domain
            if (w < 1.0e-4f) return;
            const int srcIdx = std::min (kAna - 1, std::max (0, (int) std::lround (ratio) - 1));
            emit (f0 * ratio * octLo, w * (1.0f - blend), srcIdx);   // lower octave layer (all we emit at octave points)
            if (blend > 1.0e-3f) emit (f0 * ratio * octHi, w * blend, srcIdx);  // upper layer — crossfade, still harmonic
        };
        switch (shape)
        {
            case kSine:     add (1, 1.0f); break;
            case kTriangle: for (int m = 1; m <= 11; m += 2) add ((float) m, 1.0f / ((float) m * (float) m)); break;
            case kSquare:   for (int m = 1; m <= 15; m += 2) add ((float) m, 1.0f / (float) m); break;
            case kSaw:      for (int m = 1; m <= 14; ++m)    add ((float) m, 1.0f / (float) m); break;
            case kPrime:
            { static const int pr[7] = { 1,2,3,5,7,11,13 };
              for (int j = 0; j < 7; ++j) add ((float) pr[j], 1.0f / (float) pr[j]);
              break; }
            case kLattice:                                           // 3-limit lattice 2^a·3^b
            { static const int lt[9] = { 1,2,3,4,6,8,9,12,16 };
              for (int j = 0; j < 9; ++j) add ((float) lt[j], std::pow (0.85f, (float) j));
              break; }
            case kVeil:                                              // sub-octave body (pure octaves down)
              add (1.0f, 1.0f); add (0.5f, 0.9f); add (0.25f, 0.7f); add (2.0f, 0.35f);
              break;
            case kHalo:                                              // pure-octave bloom above
            { static const int oc[5] = { 1,2,4,8,16 };
              for (int j = 0; j < 5; ++j)
              { const float lg = (float) j;                          // log2 of the octave
                add ((float) oc[j], std::exp (-(lg - 2.1f) * (lg - 2.1f) / 1.6f)); }
              break; }
            case kEmber:
            { const float s = 2.4f - 1.6f * std::min (1.0f, vo.driveSm * 2.8f);
              for (int m = 1; m <= 13; m += 2) add ((float) m, 1.0f / std::pow ((float) m, s));
              break; }
            case kCrown:                                             // the shine — high harmonics only
              for (int m = 5; m <= 13; ++m) add ((float) m, 1.0f / (float) (m - 2));
              break;
            case kPillar:                                            // octave+fifth power stack
            { static const int pl[8] = { 1,2,3,4,6,8,12,16 };
              for (int j = 0; j < 8; ++j) add ((float) pl[j], 1.0f / std::pow ((float) pl[j], 0.8f));
              break; }
            case kGlacier:
            default:
              for (int m = 1; m <= 10; ++m) add ((float) m, 1.0f / (float) m);
              break;
        }
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

    bool rebuildAll (int shape) noexcept
    {
        bool any = false;
        for (int v = 0; v < kPoly; ++v)
        { Voice& vo = voices_[v]; if (vo.note < 0) continue;
          buildVoice (vo, shape); any = true; }
        return any;
    }

    void exportViz() noexcept
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
        vizN = nT;
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
    static float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
    static float onePoleCoef (float sec, int n) noexcept
    { return 1.0f - std::exp (-(float) n / (sec * 48000.0f)); }
    static float fastTanh (float x) noexcept
    { if (x >  3.0f) return  1.0f; if (x < -3.0f) return -1.0f;
      const float x2 = x * x; return x * (27.0f + x2) / (27.0f + 9.0f * x2); }

    Voice  voices_[kPoly];
    double sr_ = 48000.0, blockT_ = 0.0;
    float  mixSm_ = 0.0f, repSm_ = 0.0f, feedSm_ = 0.0f, widSm_ = 0.0f;
    float  qualSm_ = 0.8f, tiltSm_ = 0.5f, shineSm_ = 0.5f;
    float  fbPrev_ = 0.0f, fbL_ = 0.0f, fbR_ = 0.0f;
    bool   primed_ = false;                 // first-block param prime (kills the reset-glide artifact)
};

} // namespace wc
