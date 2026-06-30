#pragma once
// =============================================================================
//  ResonatorNode.h  —  Terrain Instrument · "Annulus" physical-modeling resonator
//  Waves Crate
//
//  Header-only, NO JUCE (sibling of FlowChop.h). RT-safe in process()
//  (allocation only in prepare()). Self-contained + BUS-AGNOSTIC.
//
//  WHAT IT IS  ·  a POLYPHONIC tuned-WAVEGUIDE resonator bank = "comb filter ×100".
//  Each held note gets a digital waveguide: a delay line tuned to the note (L =
//  fs/f0) with a high-feedback loop. The loop RINGS — resonant gain ≈ 1/(1−fb),
//  so fb≈0.999 → ~1000× resonant emphasis at the note's harmonic series. Whatever
//  you feed it (synth/sample/noise) pumps that comb → it resonates the input's
//  harmonics into a sustained, pitched, physically-modelled soundscape.
//
//  This is NOT a modal bandpass bank (that was the old version — gain-normalised
//  resonators = a flat EQ that can't ring). A feedback waveguide is the canonical
//  Karplus-Strong / plucked-string / comb model and it actually RINGS.
//
//  PER-NOTE WAVEGUIDE LOOP (one per voice):
//     rd  = delay.read(L)                       // fractional (Hermite)
//     lp  = loopLowpass(rd)                      // BRIGHTNESS: HF damping in the loop
//     s   = dispersionAllpassChain(lp)           // STRUCTURE/MATERIAL: harmonic→inharmonic
//     delay.write( excite + fb*s )               // DAMPING: fb = ring time
//     out = s − posDepth·delay.read(L·posFrac)   // POSITION: pickup comb (timbre, not volume)
//
//  MATERIALS change the loop's dispersion + decay character:
//     String = harmonic (no dispersion, long ring)   Bar = stiff (moderate dispersion)
//     Drum   = membrane (high dispersion, fast decay) Metal = inharmonic (high dispersion, long)
//  STRUCTURE adds inharmonic stiffness on top of ANY material, so it always morphs.
//
//  SAFETY: loop gain = fb·|loopLP|·1(allpass) < 1 → bounded; output owns its ceiling
//  (DC-block → tanh → clamp → finite-scrub). There is no master limiter after this.
//
//  Build test: g++ -std=c++17 Source/ResonatorNode_test.cpp -o /tmp/rn && /tmp/rn
// =============================================================================

#include <cmath>
#include <algorithm>

namespace wc
{

class ResonatorNode
{
public:
    static constexpr int kPoly      = 8;       // simultaneous resonating notes
    static constexpr int kMaxDelay  = 6400;    // ≈ 7.5 Hz min at 48k (covers all notes)
    static constexpr int kAP        = 4;       // dispersion all-pass stages (max)
    static constexpr int kMaterials = 4;

    float vizEnergy[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float vizOut       = 0.0f;

    void prepare (double sampleRate) noexcept
    {
        sr_ = (sampleRate > 0.0 ? (float) sampleRate : 48000.0f);
        reset();
    }

    void reset() noexcept
    {
        for (int v = 0; v < kPoly; ++v) voices_[v].clear();
        dcX_ = dcY_ = 0.0f;
        structSm_ = brightSm_ = dampSm_ = posSm_ = 0.0f;
        mixSm_ = 0.0f;
        engaged_ = false;
        for (int b = 0; b < 4; ++b) vizEnergy[b] = 0.0f;
        vizOut = 0.0f;
    }

    void process (float structure, float brightness, float damping, float position,
                  int material, float mix, float keytrack,
                  const int* heldNotes, int nHeld,
                  double sampleRate, float* L, float* R, int n) noexcept
    {
        if (n <= 0 || L == nullptr) return;
        if (sampleRate > 0.0) sr_ = (float) sampleRate;
        if (R == nullptr) R = L;

        const float mixT    = clamp01 (mix);
        const float mixCoef = onePoleCoef (0.010f, n);
        mixSm_ += (mixT - mixSm_) * mixCoef;

        if (mixT < 1.0e-4f && mixSm_ < 1.0e-4f)         // true bypass at Mix 0 (exact passthrough)
        {
            if (engaged_) reset();
            engaged_ = false;
            for (int b = 0; b < 4; ++b) vizEnergy[b] *= 0.85f;
            vizOut *= 0.85f;
            return;
        }
        engaged_ = true;

        const float mc = onePoleCoef (0.020f, n);        // macro smoothing (no PITCH glide — pitch is snapped)
        structSm_ += (clamp01 (structure)  - structSm_) * mc;
        brightSm_ += (clamp01 (brightness) - brightSm_) * mc;
        dampSm_   += (clamp01 (damping)    - dampSm_)   * mc;
        posSm_    += (clamp01 (position)   - posSm_)    * mc;

        const int   mat = material < 0 ? 0 : (material >= kMaterials ? kMaterials - 1 : material);
        const float kt  = clamp01 (keytrack);

        // ── material character ───────────────────────────────────────────────
        float dispBase; int nStages; float fbScale; float brightBias;
        materialParams (mat, dispBase, nStages, fbScale, brightBias);

        // ── voice management (one snapped voice per held note) ────────────────
        for (int v = 0; v < kPoly; ++v)
            if (voices_[v].note >= 0) voices_[v].active = isHeld (voices_[v].note, heldNotes, nHeld);
        const int nh = nHeld < 0 ? 0 : nHeld;
        for (int h = 0; h < nh; ++h)
        {
            const int nn = heldNotes[h];
            if (nn < 0 || nn > 127) continue;
            int vi = -1;
            for (int v = 0; v < kPoly; ++v) if (voices_[v].note == nn) { vi = v; break; }
            if (vi >= 0) { voices_[vi].active = true; continue; }
            int free = -1;
            for (int v = 0; v < kPoly; ++v) if (voices_[v].note < 0) { free = v; break; }
            if (free < 0) { float lo = 1.0e30f; for (int v = 0; v < kPoly; ++v) if (voices_[v].energy < lo) { lo = voices_[v].energy; free = v; } }
            voices_[free].clear();
            voices_[free].note = nn; voices_[free].active = true; voices_[free].pluck = 1.0f;
        }

        // ── per-block loop coefficients ───────────────────────────────────────
        const float baseHz   = 130.81f;
        // BRIGHTNESS → loop low-pass coefficient (1 = pass highs/bright, small = dark)
        const float loopG    = clamp01 (0.03f + (0.96f) * std::pow (clamp01 (brightSm_ + brightBias), 1.4f));
        // STRUCTURE/MATERIAL → dispersion all-pass coefficient (negative = stiffness/inharmonicity)
        const float dispC    = -std::min (0.92f, dispBase + 0.58f * structSm_);
        const int   stages   = std::min (kAP, nStages + (structSm_ > 0.55f ? 1 : 0));
        // DAMPING → loop feedback (ring time). damp0 = ~0.999 (huge), damp1 = ~0.85 (short).
        const float fb       = std::min (0.9994f, fbScale * (0.850f + 0.149f * (1.0f - dampSm_)));
        // POSITION → pickup-comb fraction (which harmonics emphasised — timbral, not volume)
        const float posFrac  = 0.06f + 0.44f * posSm_;
        const float posDepth = 0.85f;
        // resonant-gain target from Mix; drive compensates (1−fb) so loudness tracks Mix, not Damping
        const float gainTgt  = 12.0f + 95.0f * mixSm_;
        const float pluckBoost = 5.0f;
        const float exCoef   = onePoleCoef (0.004f, 1);   // fast excitation ramp (click-free)

        int ringing = 0;
        for (int v = 0; v < kPoly; ++v) if (voices_[v].note >= 0) ++ringing;
        const float outScale = 0.45f / std::sqrt ((float) std::max (1, ringing));
        const float pluckDecay = std::exp (-1.0f / (0.010f * sr_));   // ~10 ms pluck transient

        // per-voice block setup
        for (int v = 0; v < kPoly; ++v)
        {
            Voice& vc = voices_[v];
            if (vc.note < 0) continue;
            const float nHz = noteToHz (vc.note);
            const float f0  = baseHz * std::pow (nHz / baseHz, kt);
            float D = sr_ / std::max (8.0f, f0);
            D -= 1.0f + (float) stages * 0.5f;            // crude loop-delay compensation (tuning)
            vc.delay = clampf (D, 4.0f, (float) (kMaxDelay - 4));
            vc.fb = fb; vc.loopG = loopG; vc.dispC = dispC; vc.stages = stages;
        }

        const float dryGain = std::cos (mixSm_ * 1.57079633f);
        const float wetGain = std::sin (mixSm_ * 1.57079633f);
        bool sawBad = false;
        float outAccum = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float dryL = L[i], dryR = R[i];
            float in = 0.5f * (dryL + dryR);
            const float dcY = in - dcX_ + 0.995f * dcY_;   // DC block the exciter
            dcX_ = in; dcY_ = dcY;

            float wet = 0.0f;
            for (int v = 0; v < kPoly; ++v)
            {
                Voice& vc = voices_[v];
                if (vc.note < 0) continue;

                const float tgt = vc.active ? 1.0f : 0.0f;
                vc.exGain += (tgt - vc.exGain) * exCoef;
                vc.pluck  *= pluckDecay;

                const float rd = readHermite (vc.buf, kMaxDelay, vc.widx, vc.delay);
                vc.lp += vc.loopG * (rd - vc.lp);          // loop low-pass (brightness/damping of highs)
                float s = vc.lp;
                for (int j = 0; j < vc.stages; ++j)        // dispersion all-pass chain (inharmonicity)
                {
                    const float y = vc.dispC * s + vc.apx[j] - vc.dispC * vc.apy[j];
                    vc.apx[j] = s; vc.apy[j] = y; s = y;
                }
                const float drive = (1.0f - vc.fb) * gainTgt;
                const float ex = dcY * vc.exGain * drive * (1.0f + pluckBoost * vc.pluck);
                vc.buf[vc.widx] = ex + vc.fb * s;
                vc.widx = (vc.widx + 1 >= kMaxDelay) ? 0 : vc.widx + 1;

                const float pick = readHermite (vc.buf, kMaxDelay, vc.widx, vc.delay * posFrac);
                const float vo = s - posDepth * pick;      // POSITION pickup comb
                wet += vo;
                vc.energy += vo * vo;
            }

            wet *= outScale;
            wet = fastTanh (wet);
            if (! (wet == wet) || wet > 1.0e6f || wet < -1.0e6f) { wet = 0.0f; sawBad = true; }

            L[i] = clampUnit (dryGain * dryL + wetGain * wet);
            R[i] = clampUnit (dryGain * dryR + wetGain * wet);
            outAccum += wet * wet;
        }

        const float invN = 1.0f / (float) n;
        for (int v = 0; v < kPoly; ++v)
        {
            Voice& vc = voices_[v];
            if (vc.note < 0) continue;
            const float rms = std::sqrt (vc.energy * invN);
            vc.energy = rms;
            if (sawBad) vc.clearState();
            if (! vc.active && rms < 1.0e-4f) vc.clear();
        }

        // viz — overall level + 4 bands (low→high voices/material spread)
        const float lvl = clamp01 (std::sqrt (outAccum * invN) * 1.7f);
        vizOut += (lvl - vizOut) * 0.30f;
        float band[4] = { 0, 0, 0, 0 }; float tot = 1.0e-6f;
        for (int v = 0; v < kPoly; ++v) if (voices_[v].note >= 0)
        { const int b = std::min (3, voices_[v].note / 32); band[b] += voices_[v].energy; tot += voices_[v].energy; }
        for (int b = 0; b < 4; ++b) { const float e = clamp01 (band[b] / tot * lvl * 3.0f); vizEnergy[b] += (e - vizEnergy[b]) * 0.30f; }
    }

private:
    struct Voice
    {
        int   note = -1; bool active = false;
        float exGain = 0.0f, pluck = 0.0f, energy = 0.0f;
        float buf[kMaxDelay] = { 0 }; int widx = 0;
        float lp = 0.0f;
        float apx[kAP] = { 0 }, apy[kAP] = { 0 };
        float delay = 100.0f, fb = 0.0f, loopG = 0.5f, dispC = 0.0f; int stages = 1;
        void clearState() noexcept { for (int k = 0; k < kMaxDelay; ++k) buf[k] = 0.0f; lp = 0.0f; for (int j = 0; j < kAP; ++j) { apx[j] = apy[j] = 0.0f; } }
        void clear() noexcept { note = -1; active = false; exGain = pluck = energy = 0.0f; widx = 0; clearState(); }
    };

    static void materialParams (int mat, float& dispBase, int& nStages, float& fbScale, float& brightBias) noexcept
    {
        switch (mat)
        {
            case 1: dispBase = 0.30f; nStages = 2; fbScale = 0.94f; brightBias = -0.05f; break;  // Bar (stiff)
            case 2: dispBase = 0.55f; nStages = 3; fbScale = 0.80f; brightBias = -0.15f; break;  // Drum (membrane, fast)
            case 3: dispBase = 0.45f; nStages = 4; fbScale = 1.00f; brightBias =  0.10f; break;  // Metal (inharmonic, long)
            default: dispBase = 0.00f; nStages = 1; fbScale = 1.00f; brightBias = 0.05f; break;   // String (harmonic)
        }
    }

    static float readHermite (const float* buf, int size, int widx, float D) noexcept
    {
        float rp = (float) widx - D;
        while (rp < 0.0f) rp += (float) size;
        while (rp >= (float) size) rp -= (float) size;
        const int i1 = (int) rp; const float f = rp - (float) i1;
        const int i0 = (i1 - 1 + size) % size;
        const int i2 = (i1 + 1) % size;
        const int i3 = (i1 + 2) % size;
        const float y0 = buf[i0], y1 = buf[i1 % size], y2 = buf[i2], y3 = buf[i3];
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    static bool isHeld (int note, const int* held, int nHeld) noexcept
    { for (int h = 0; h < nHeld; ++h) if (held[h] == note) return true; return false; }
    static float noteToHz (int note) noexcept { return 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f); }
    static float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static float clampUnit (float v) noexcept { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
    static float fastTanh (float x) noexcept
    { if (x > 5.0f) return 1.0f; if (x < -5.0f) return -1.0f; const float x2 = x * x; return x * (27.0f + x2) / (27.0f + 9.0f * x2); }
    float onePoleCoef (float tau, int n) const noexcept
    { const float a = 1.0f - std::exp (-(float) n / (tau * sr_)); return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a); }

    float sr_ = 48000.0f;
    Voice voices_[kPoly];
    float dcX_ = 0.0f, dcY_ = 0.0f;
    float structSm_ = 0.0f, brightSm_ = 0.0f, dampSm_ = 0.0f, posSm_ = 0.0f, mixSm_ = 0.0f;
    bool  engaged_ = false;
};

} // namespace wc
