#pragma once
// =============================================================================
//  ResonatorNode.h  —  Terrain Instrument · "Annulus" physical-modeling resonator
//  Waves Crate
//
//  Header-only, NO JUCE (sibling of FlowChop.h). RT-safe in process()
//  (allocation only in prepare()). Self-contained + BUS-AGNOSTIC.
//
//  ── WHAT IT IS ───────────────────────────────────────────────────────────
//  A POLYPHONIC physical-modeling resonator with TWO synthesis cores, picked
//  per Material. Whatever you feed it (synth / sample / noise) excites the
//  model; each held note is tuned to its pitch and RINGS. It is also playable
//  standalone — every note-on injects a short excitation burst, so you can hit
//  a bell or a marimba with no input at all.
//
//    WAVEGUIDE  CORE  →  String · Pluck · Piano   (1-D string family)
//        A tuned delay-loop comb (Karplus-Strong / digital waveguide). The loop
//        rings: resonant gain ≈ 1/(1−fb). A dispersion all-pass cascade bends
//        the harmonic series into stiff/inharmonic partials (piano). Piano uses
//        TWO slightly-detuned coupled strings → beating + Weinreich double-decay.
//
//    MODAL  CORE  →  Bar · Metal · Drum            (2-D / percussion family)
//        A parallel bank of high-Q 2-pole resonators, one per physical mode:
//            y = g·x − c1·y1 − c2·y2 ,  c1 = −2R·cos ω ,  c2 = R²
//        Poles at R·e^{±jω}, R = exp(−ln(1000)/(T60·fs)). The mode ratios are the
//        REAL acoustics (free-free bar, Simpson minor-third bell, circular
//        membrane Bessel zeros). g is NOT bandpass-normalized — that is the old
//        "modal = dead EQ" bug. Input is scaled by (1−R) for level only, which
//        leaves the ring intact. A parallel bank of R<1 resonators driven by
//        bounded input is unconditionally BIBO-stable.
//
//  ── THE FOUR MACROS (mapped to BOTH cores) ────────────────────────────────
//    STRUCTURE   harmonic ↔ inharmonic. Waveguide: dispersion depth. Modal:
//                stiffness stretch f_k·√(1+B·k²) on top of the base ratios, so
//                it always morphs the timbre, never a no-op.
//    BRIGHTNESS  waveguide: loop low-pass (HF damping). modal: active mode count
//                + spectral tilt (how loud the upper modes are).
//    DAMPING     ring length. Waveguide: loop feedback. Modal: per-mode T60.
//                0 = long sustain, 1 = dead/short.
//    POSITION    where it is struck/picked. Waveguide: pickup comb. Modal:
//                strike-position mode weighting (nodes at the strike point go
//                quiet), like striking a bar at the center vs the end.
//
//  KEYTRACK  fundamental tracks the played note (1) or stays fixed (0).
//  MIX       equal-power dry→wet. Mix 0 is an EXACT bypass (passthrough).
//
//  SAFETY: waveguide loop gain < 1 → bounded; modal banks BIBO-stable. Output
//  owns its ceiling (DC-block → tanh → clamp → finite-scrub). No master limiter
//  after this node.
//
//  VIZ: vizOut = overall level; vizEnergy[4] = live 4-band spectral fingerprint
//  of the wet signal (low → high). The UI shape is driven by the SOUND.
//
//  Build test: g++ -std=c++17 Source/ResonatorNode_test.cpp -o /tmp/rn && /tmp/rn
// =============================================================================

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace wc
{

class ResonatorNode
{
public:
    static constexpr int kPoly      = 8;       // simultaneous resonating notes
    static constexpr int kMaxDelay  = 6400;    // ≈ 7.5 Hz min at 48k (covers all notes)
    static constexpr int kAP        = 6;       // dispersion all-pass stages (max; piano uses 6)
    static constexpr int kStrings   = 2;       // coupled strings per waveguide voice (piano = 2)
    static constexpr int kModes     = 9;       // modal resonators per voice (max)
    static constexpr int kMaterials = 6;       // String Pluck Piano Bar Metal Drum

    // material indices (engine contract — keep the UI StringArray in this order)
    enum Material { kString = 0, kPluck = 1, kPiano = 2, kBar = 3, kMetal = 4, kDrum = 5 };

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
        excSmooth_ = 0.0f;
        engaged_ = false;
        vb0_ = vb1_ = vb2_ = 0.0f;
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

        const float mc = onePoleCoef (0.020f, n);        // macro smoothing (no PITCH glide — pitch snaps)
        structSm_ += (clamp01 (structure)  - structSm_) * mc;
        brightSm_ += (clamp01 (brightness) - brightSm_) * mc;
        dampSm_   += (clamp01 (damping)    - dampSm_)   * mc;
        posSm_    += (clamp01 (position)   - posSm_)    * mc;

        const int   mat   = material < 0 ? 0 : (material >= kMaterials ? kMaterials - 1 : material);
        const bool  modal = (mat >= kBar);
        const float kt    = clamp01 (keytrack);
        const float baseHz = 130.81f;

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
            voices_[free].note = nn; voices_[free].active = true;
            // Max: KILL the note-on excitation burst — the per-fresh-voice pluck (waveguide) / mallet
            // "snare" (modal) that fired on the first strike of each new note (round-robin spike). The
            // resonator now rings ONLY the fed audio's harmonics, no internal strike. pitchEnv kept.
            // (CC stopgap patch on Opus's engine — Opus to fold "disable note-on burst" into next drop.)
            voices_[free].pluck = 0.0f; voices_[free].pitchEnv = 1.0f; voices_[free].strike = 0.0f;
        }

        // ── shared envelope coefficients ─────────────────────────────────────
        const float exCoef     = onePoleCoef (0.004f, 1);                 // excitation gate ramp
        // note-on excitation length per material: sharp pluck (string) → longer soft hammer/mallet
        float excMs;
        switch (mat) {
            case kString: excMs = 6.0f;  break;
            case kPluck:  excMs = 4.0f;  break;
            case kPiano:  excMs = 11.0f; break;   // felt hammer — rounder, longer contact
            case kDrum:   excMs = 8.0f;  break;
            case kMetal:  excMs = 12.0f; break;   // soft mallet
            default:      excMs = 9.0f;  break;   // bar
        }
        const float pluckDecay = std::exp (-1.0f / (0.001f * excMs * sr_));
        const float dryGain    = std::cos (mixSm_ * 1.57079633f);
        const float wetGain    = std::sin (mixSm_ * 1.57079633f);
        const float gainTgt    = 12.0f + 95.0f * mixSm_;                  // resonant drive vs Mix

        int ringing = 0;
        for (int v = 0; v < kPoly; ++v) if (voices_[v].note >= 0) ++ringing;
        const float outScale = 0.45f / std::sqrt ((float) std::max (1, ringing));

        if (modal) setupModal  (mat, kt, baseHz);
        else       setupWave   (mat, kt, baseHz, gainTgt);

        // ── 4-band viz crossover coefficients (~200 / 800 / 3000 Hz) ─────────
        const float va = onePoleHz (200.0f), vbb = onePoleHz (800.0f), vc = onePoleHz (3000.0f);

        bool  sawBad   = false;
        float outAccum = 0.0f;
        float bandAcc[4] = { 0, 0, 0, 0 };

        for (int i = 0; i < n; ++i)
        {
            const float dryL = L[i], dryR = R[i];
            const float in   = 0.5f * (dryL + dryR);
            const float dcY  = in - dcX_ + 0.995f * dcY_;   // DC-block the exciter
            dcX_ = in; dcY_ = dcY;
            // transient softener: String passes through (keeps its pluck); other materials
            // smooth the input so sharp transients don't fire the resonant pluck-spike.
            excSmooth_ += excSmoothCoef_ * (dcY - excSmooth_);
            const float drive = excSmooth_;

            float wet = 0.0f;
            for (int v = 0; v < kPoly; ++v)
            {
                Voice& vo = voices_[v];
                if (vo.note < 0) continue;

                const float tgt = vo.active ? 1.0f : 0.0f;
                vo.exGain += (tgt - vo.exGain) * exCoef;
                vo.pluck  *= pluckDecay;
                const float voNoise = vo.noise();

                float vout;
                if (modal) vout = renderModal (vo, drive, voNoise);
                else       vout = renderWave  (vo, drive, voNoise);

                wet += vout;
                vo.energy += vout * vout;
            }

            wet *= outScale;
            wet  = fastTanh (wet);
            if (! (wet == wet) || wet > 1.0e6f || wet < -1.0e6f) { wet = 0.0f; sawBad = true; }

            L[i] = clampUnit (dryGain * dryL + wetGain * wet);
            R[i] = clampUnit (dryGain * dryR + wetGain * wet);
            outAccum += wet * wet;

            // live 4-band spectral fingerprint of the wet signal
            vb0_ += va  * (wet  - vb0_);
            vb1_ += vbb * (wet  - vb1_);
            vb2_ += vc  * (wet  - vb2_);
            const float bL = vb0_, bLM = vb1_ - vb0_, bHM = vb2_ - vb1_, bH = wet - vb2_;
            bandAcc[0] += bL * bL; bandAcc[1] += bLM * bLM; bandAcc[2] += bHM * bHM; bandAcc[3] += bH * bH;
        }

        const float invN = 1.0f / (float) n;
        for (int v = 0; v < kPoly; ++v)
        {
            Voice& vo = voices_[v];
            if (vo.note < 0) continue;
            const float rms = std::sqrt (vo.energy * invN);
            vo.energy = rms;
            if (sawBad) vo.clearState();
            if (! vo.active && rms < 1.0e-4f) vo.clear();
        }

        // viz — overall level + the 4 spectral bands (normalized to the loudest band)
        const float lvl = clamp01 (std::sqrt (outAccum * invN) * 1.7f);
        vizOut += (lvl - vizOut) * 0.30f;
        float bmax = 1.0e-6f;
        float bnd[4];
        for (int b = 0; b < 4; ++b) { bnd[b] = std::sqrt (bandAcc[b] * invN); if (bnd[b] > bmax) bmax = bnd[b]; }
        for (int b = 0; b < 4; ++b)
        {
            const float e = clamp01 (bnd[b] / bmax * (0.25f + 0.75f * lvl));
            vizEnergy[b] += (e - vizEnergy[b]) * 0.30f;
        }
    }

private:
    // =========================================================================
    //  Voice
    // =========================================================================
    struct Str
    {
        float buf[kMaxDelay] = { 0 }; int widx = 0;
        float delay = 100.0f, lp = 0.0f, nlp = 0.0f;
        float apx[kAP] = { 0 }, apy[kAP] = { 0 };
        float fb = 0.0f, loopG = 0.5f, dispC = 0.0f, gain = 1.0f; int stages = 1;
        void clr() noexcept
        {
            std::memset (buf, 0, sizeof (buf)); widx = 0; lp = nlp = 0.0f;
            for (int j = 0; j < kAP; ++j) { apx[j] = apy[j] = 0.0f; }
        }
    };
    struct Mode { float y1 = 0, y2 = 0, c1 = 0, c2 = 0, gCont = 0, gKick = 0; void clr() noexcept { y1 = y2 = 0.0f; } };

    struct Voice
    {
        int   note = -1; bool active = false;
        float exGain = 0.0f, pluck = 0.0f, energy = 0.0f, pitchEnv = 1.0f, strike = 0.0f, mlp = 0.0f;
        Str   str[kStrings];
        int   nStr = 1;
        Mode  mode[kModes];
        int   nModes = 0;
        std::uint32_t rng = 0x1234567u;

        float noise() noexcept   // cheap white noise in [-1,1]
        { rng = rng * 1664525u + 1013904223u; return ((float) (rng >> 9) * (1.0f / 4194304.0f)) - 1.0f; }

        void clearState() noexcept
        { for (int s = 0; s < kStrings; ++s) str[s].clr(); for (int m = 0; m < kModes; ++m) mode[m].clr(); }
        void clear() noexcept
        { note = -1; active = false; exGain = pluck = energy = 0.0f; pitchEnv = 1.0f; strike = mlp = 0.0f; clearState(); }
    };

    // =========================================================================
    //  Material descriptors
    // =========================================================================
    struct WGmat { int nStr; float detune; int stages; float dispBase; float fbScale;
                   float brightBias; int excType; float noiseAmt; float noiseLP; float excMs; };
    // excType: 0 = sharp pluck (string), 1 = soft felt hammer (piano)

    static WGmat wgMat (int mat) noexcept
    {
        switch (mat)
        {
            //                nStr  detune   stg  disp   fbScl  brBias  exc          noise  nLP    excMs
            case kPluck: return { 1, 0.0f,     0, 0.00f, 0.90f,  0.18f, 0/*pluck*/,  2.80f, 0.55f,  4.0f };
            case kPiano: return { 2, 0.00042f, 2, 0.12f, 0.997f,-0.03f, 1/*hammer*/, 1.60f, 0.12f, 11.0f };
            default:     return { 1, 0.0f,     0, 0.00f, 1.00f,  0.05f, 0/*pluck*/,  1.80f, 0.30f,  6.0f }; // String
        }
    }

    struct MODmat { const float* ratio; const float* amp; const float* dec; int count;
                    float baseT60; bool pitchDrop; };

    static MODmat modMat (int mat) noexcept
    {
        // Bar / Marimba — free-free bar w/ tuned undercut (1 : 4 : 9.2 …)
        static const float barR[5] = { 1.0f, 4.0f, 9.2f, 16.7f, 25.0f };
        static const float barA[5] = { 1.0f, 0.45f, 0.22f, 0.10f, 0.05f };
        static const float barD[5] = { 1.0f, 0.35f, 0.20f, 0.12f, 0.08f };
        // Metal / Bell — Simpson true-harmonic, minor-third (tierce = 1.2) signature
        static const float belR[9] = { 0.5f, 1.0f, 1.2f, 1.5f, 2.0f, 2.5f, 3.0f, 4.2f, 5.4f };
        static const float belA[9] = { 0.40f, 1.0f, 0.90f, 0.55f, 0.70f, 0.45f, 0.50f, 0.35f, 0.25f };
        static const float belD[9] = { 1.0f, 0.95f, 0.92f, 0.62f, 0.48f, 0.36f, 0.30f, 0.20f, 0.15f };
        // Drum / Membrane — ideal circular membrane Bessel ratios (1 : 1.593 : 2.136 …)
        static const float drmR[9] = { 1.0f, 1.593f, 2.136f, 2.296f, 2.653f, 2.917f, 3.156f, 3.500f, 3.598f };
        static const float drmA[9] = { 1.0f, 0.80f, 0.60f, 0.50f, 0.40f, 0.35f, 0.30f, 0.25f, 0.20f };
        static const float drmD[9] = { 0.18f, 1.0f, 0.55f, 0.50f, 0.35f, 0.30f, 0.25f, 0.20f, 0.18f };

        switch (mat)
        {
            case kMetal: return { belR, belA, belD, 9, 4.0f, false };
            case kDrum:  return { drmR, drmA, drmD, 9, 0.6f, true  };
            default:     return { barR, barA, barD, 5, 1.6f, false }; // Bar
        }
    }

    // =========================================================================
    //  Per-block setup — WAVEGUIDE
    // =========================================================================
    void setupWave (int mat, float kt, float baseHz, float gainTgt) noexcept
    {
        const WGmat m = wgMat (mat);
        const float loopG = clamp01 (0.03f + 0.96f * std::pow (clamp01 (brightSm_ + m.brightBias), 1.4f));
        // Dispersion is now a FIXED, mild material property (piano stiffness only) — it no
        // longer rides on STRUCTURE, and the loop delay is compensated for its phase delay at
        // the played note so the FUNDAMENTAL is exactly on pitch (piano stretches only its
        // upper partials, as real strings do). String/Pluck use zero dispersion → pure harmonic.
        const float dispC  = -m.dispBase;
        const int   stages = m.stages;
        // DAMPING → loop feedback (ring time), wide range:
        const float fb     = std::min (0.9994f, m.fbScale * (0.760f + 0.239f * (1.0f - dampSm_)));

        // STRUCTURE (pitch-locked): brightens the excitation (attack harmonics) — the sustained
        // output richness is applied in renderWave. It NEVER changes pitch. Amplitude is stable
        // so String/Piano keep their body regardless of structure position.
        wgNoise_       = m.noiseAmt;
        wgNoiseLP_     = clamp01 (m.noiseLP * (0.55f + 0.90f * structSm_));  // structure = attack brightness
        wgPosFrac_     = 0.05f + 0.42f * posSm_;
        wgDrive_       = (1.0f - fb) * gainTgt;
        excSmoothCoef_ = (mat == kString) ? 1.00f : (mat == kPluck ? 0.90f : 0.55f); // String keeps its pluck

        for (int v = 0; v < kPoly; ++v)
        {
            Voice& vo = voices_[v];
            if (vo.note < 0) continue;
            vo.nStr   = m.nStr;
            vo.nModes = 0;
            const float nHz = noteToHz (vo.note);
            const float f0  = baseHz * std::pow (nHz / baseHz, kt);

            for (int s = 0; s < m.nStr; ++s)
            {
                Str& st = vo.str[s];
                const float det = (s == 0) ? 1.0f : (1.0f + m.detune);        // 2nd string sub-cent sharp → shimmer
                const float fS  = std::max (8.0f, f0 * det);
                const float w0  = 6.2831853f * fS / sr_;
                // exact tuning: total loop phase delay = delay-line + LP + Σ all-pass, all at w0
                float D = sr_ / fS;
                D -= phaseDelayLP (loopG, w0);
                D -= (float) stages * phaseDelayAP (dispC, w0);
                st.delay  = clampf (D, 4.0f, (float) (kMaxDelay - 4));
                st.loopG  = loopG;
                st.dispC  = dispC;
                st.stages = stages;
                st.fb     = (s == 0) ? fb : std::min (0.9994f, fb * 0.9997f);  // 2nd string slower → double-decay
                st.gain   = (m.nStr == 1) ? 1.0f : (s == 0 ? 0.62f : 0.50f);
            }
        }
    }

    float renderWave (Voice& vo, float drive, float vn) noexcept
    {
        // continuous input drive (already transient-smoothed for non-string materials)
        const float exc = drive * vo.exGain * wgDrive_ * (1.0f + 5.0f * vo.pluck);
        // note-on excitation burst — sharp pluck (string) OR soft felt hammer (piano, darker via
        // the per-string noise low-pass). STRUCTURE brightens it (in setup); amplitude is stable.
        const float burst = vn * vo.pluck * wgNoise_;
        float out = 0.0f;
        for (int s = 0; s < vo.nStr; ++s)
        {
            Str& st = vo.str[s];
            const float rd = readHermite (st.buf, kMaxDelay, st.widx, st.delay);
            st.lp += st.loopG * (rd - st.lp);
            float sig = st.lp;
            for (int j = 0; j < st.stages; ++j)
            {
                const float y = st.dispC * sig + st.apx[j] - st.dispC * st.apy[j];
                st.apx[j] = sig; st.apy[j] = y; sig = y;
            }
            st.nlp += wgNoiseLP_ * (burst - st.nlp);          // shaped burst into the loop input
            const float inSig = exc + st.nlp;
            st.buf[st.widx] = inSig + st.fb * sig;
            st.widx = (st.widx + 1 >= kMaxDelay) ? 0 : st.widx + 1;

            const float pick = readHermite (st.buf, kMaxDelay, st.widx, st.delay * wgPosFrac_);
            out += (sig - 0.85f * pick) * st.gain;
        }
        // STRUCTURE output drive: pitch-safe harmonic enrichment, UNITY at structure 0
        // (small-signal gain stays 1.0, peaks compress into harmonics as structure rises).
        const float g      = 1.0f + 3.0f * structSm_;
        const float driven = fastTanh (out * g) * (1.0f / g);
        out += structSm_ * (driven - out);
        return out;
    }

    // =========================================================================
    //  Per-block setup — MODAL
    // =========================================================================
    void setupModal (int mat, float kt, float baseHz) noexcept
    {
        const MODmat m = modMat (mat);
        // STRUCTURE (pitch-locked): PURITY → COMPLEXITY. Low structure emphasises the
        // fundamental + a couple of low modes (a clean, in-tune pitched tone — a C reads as a
        // C); high structure brings the full mode bank up to its true material amplitudes.
        // Mode FREQUENCIES never move (the ratios are the fixed material identity — no detune).
        const int   nActive    = clampi (2 + (int) std::lround (structSm_ * (float) (m.count - 2)), 2, m.count);
        const float fundBias   = 0.15f + 0.85f * structSm_;                 // low struct → fundamental-dominant (pure/in-tune); high → full bank
        const float brightExp  = -1.20f + 2.60f * brightSm_;                // BRIGHTNESS = spectral tilt (wide, audible)
        const float strikePos  = 0.08f + 0.42f * posSm_;
        const float dampScale  = 4.0f * std::pow (0.01f, dampSm_);          // T60 multiplier: 4.0 → 0.04
        const float dropCoef   = 1.0f - std::exp (-1.0f / (0.060f * sr_));  // ~60 ms pitch drop (drum)
        modalDrive_ = 1.50f + 4.50f * mixSm_;   // continuous input drive vs Mix (main soundscape path)
        modalKick_  = 2.20f;                     // soft mallet impulse (short low-passed pulse, no pluck-spike)
        malletCoef_ = 0.12f + 0.70f * brightSm_; // BRIGHTNESS shapes the mallet: dark=soft pad, bright=percussive/opens highs
        excSmoothCoef_ = (mat == kDrum) ? 0.34f : 0.24f;  // soften input transients → clean pad/soundscape

        for (int v = 0; v < kPoly; ++v)
        {
            Voice& vo = voices_[v];
            if (vo.note < 0) continue;
            vo.nStr   = 0;
            const float nHz = noteToHz (vo.note);
            const float f0  = baseHz * std::pow (nHz / baseHz, kt);

            if (m.pitchDrop) vo.pitchEnv += (1.0f - vo.pitchEnv) * dropCoef;  // glide toward 1.0
            else             vo.pitchEnv  = 1.0f;
            const float pdrop = m.pitchDrop ? vo.pitchEnv : 1.0f;

            int live = 0;
            for (int k = 0; k < nActive; ++k)
            {
                Mode& md = vo.mode[live];
                float fk = f0 * m.ratio[k] * pdrop;                          // fixed ratios → always in tune
                if (fk > 0.46f * sr_) continue;                              // SKIP (not clamp) out-of-range modes → every note plays clean
                if (fk < 20.0f) fk = 20.0f;
                const float w  = 6.2831853f * fk / sr_;

                float T60 = m.baseT60 * m.dec[k] * dampScale;
                // BRIGHTNESS also tilts DECAY: high modes die faster when dark (mellow body),
                // sustain when bright (shimmering body). Fundamental (ratio 1) is unaffected.
                const float oct    = std::log2 (m.ratio[k] > 1.0f ? m.ratio[k] : 1.0f);
                const float hfTilt = std::pow (0.30f + 0.70f * brightSm_, oct);
                T60 *= hfTilt;
                T60 = clampf (T60, 0.008f, 8.0f);
                float Rr = std::exp (-6.9077553f / (T60 * sr_));             // ln(1000) = 6.9078
                Rr = Rr > 0.99995f ? 0.99995f : Rr;

                const float sw = std::sin (w);                              // folds out the resonator's 1/sin(ω) gain
                md.c1 = -2.0f * Rr * std::cos (w);
                md.c2 = Rr * Rr;

                const float strikeW  = 0.12f + 0.88f * std::fabs (std::sin ((float) (k + 1) * 3.14159265f * strikePos));
                const float emphasis = std::pow (m.ratio[k], brightExp);     // BRIGHTNESS spectral tilt
                const float pure     = (k == 0) ? 1.0f : std::pow (fundBias, (float) k);  // STRUCTURE purity contour
                const float aEff     = m.amp[k] * strikeW * emphasis * pure * sw;
                md.gCont = aEff * (1.0f - Rr) * modalDrive_;                 // continuous drive (level-bounded)
                md.gKick = aEff * modalKick_;                               // soft mallet impulse (rings down over T60)
                ++live;
            }
            vo.nModes = live;
        }
    }

    float renderModal (Voice& vo, float drive, float vn) noexcept
    {
        const float excCont = drive * vo.exGain;                            // transient-smoothed input drive
        // soft mallet: a 1-sample impulse low-passed into a short, band-limited pulse. Total
        // energy is ~fixed regardless of the LP, so the peak is predictable and there is NO
        // resonant runaway (a long burst into high-Q modes rails). This removes the guitar-pluck
        // click from the modal materials while keeping a clean pitched onset.
        vo.mlp += malletCoef_ * ((vo.strike * (0.8f + 0.2f * vn)) - vo.mlp);
        vo.strike = 0.0f;                                                   // impulse consumed
        const float mallet = vo.mlp;
        float out = 0.0f;
        for (int k = 0; k < vo.nModes; ++k)
        {
            Mode& md = vo.mode[k];
            const float x = md.gCont * excCont + md.gKick * mallet;
            const float y = x - md.c1 * md.y1 - md.c2 * md.y2;
            md.y2 = md.y1; md.y1 = y;
            out += y;
        }
        return out;
    }

    // =========================================================================
    //  Helpers
    // =========================================================================
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

    // phase delay (in samples) of a one-pole low-pass  y=(1-g)y1+g x  at angular freq w
    static float phaseDelayLP (float g, float w) noexcept
    {
        const float p = 1.0f - g;
        const float d = std::atan2 (p * std::sin (w), 1.0f - p * std::cos (w));
        return d / (w > 1.0e-6f ? w : 1.0e-6f);
    }
    // phase delay (in samples) of a first-order all-pass  H=(a+z^-1)/(1+a z^-1)  at freq w
    static float phaseDelayAP (float a, float w) noexcept
    {
        const float sw = std::sin (w), cw = std::cos (w);
        const float ph = std::atan2 (-sw, a + cw) - std::atan2 (-a * sw, 1.0f + a * cw);
        return (-ph) / (w > 1.0e-6f ? w : 1.0e-6f);
    }

    static float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static int   clampi (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static float clampUnit (float v) noexcept { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
    static float fastTanh (float x) noexcept
    { if (x > 5.0f) return 1.0f; if (x < -5.0f) return -1.0f; const float x2 = x * x; return x * (27.0f + x2) / (27.0f + 9.0f * x2); }
    float onePoleCoef (float tau, int n) const noexcept
    { const float a = 1.0f - std::exp (-(float) n / (tau * sr_)); return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a); }
    float onePoleHz (float hz) const noexcept
    { const float a = 1.0f - std::exp (-6.2831853f * hz / sr_); return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a); }

    // state
    float sr_ = 48000.0f;
    Voice voices_[kPoly];
    float dcX_ = 0.0f, dcY_ = 0.0f;
    float structSm_ = 0.0f, brightSm_ = 0.0f, dampSm_ = 0.0f, posSm_ = 0.0f, mixSm_ = 0.0f;
    bool  engaged_ = false;

    // per-block scratch (waveguide)
    float wgDrive_ = 0.0f, wgNoise_ = 0.0f, wgNoiseLP_ = 0.3f, wgPosFrac_ = 0.2f;
    // per-block scratch (modal)
    float modalDrive_ = 1.0f, modalKick_ = 0.5f, malletCoef_ = 0.35f;
    // exciter transient softener (kills the resonant pluck-spike on non-string materials)
    float excSmooth_ = 0.0f, excSmoothCoef_ = 1.0f;
    // viz band-split state
    float vb0_ = 0.0f, vb1_ = 0.0f, vb2_ = 0.0f;
};

} // namespace wc
