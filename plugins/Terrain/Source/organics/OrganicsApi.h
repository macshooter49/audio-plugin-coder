// OrganicsApi.h — THE ORGANICS ENGINE: the frozen contract (tp104, 2026-09-24).
//
// The ONLY header the integration (SynthVoice / PluginProcessor / PluginEditor) includes. The runtime
// agent implements it in OrganicsLibrary.h / OrganicEngine.h; until that lands, OrganicsStub.cpp
// (integration-owned) provides silent no-op bodies so the plugin links.
//
// Design: .ideas/organics-engine-design.md (§2 knobs, §3 day-1 features, §4.1 .torg v1 format,
// §5 CPU/memory, §7 integration). Contract notes: .ideas/organics-contract.md.
//
// Changing anything in this file after the fork needs the lead's sign-off: four worktrees build against it.
#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <functional>
#include <memory>

namespace tw
{
    /** The ten knobs, in DSP units (already de-normalised from the 0..1 APVTS values by the caller).
        Bipolar knobs are −1..+1 with 0 = off (APVTS 0.5). Defaults = the APVTS defaults. */
    struct OrganicParams
    {
        float dyn     = 0.0f;    // Dynamics  −1..+1  layer-selection velocity offset (±63), live crossfade
        float tone    = 0.0f;    // Tone      −1..+1  velocity-aware tilt, pivot max(700 Hz, f0), ±9 dB; on SPARSE spectra (bells, flute) a harmonic exciter on + and a deeper low-pass on − (tp108)
        float body    = 0.0f;    // Body      −1..+1  zone lookup shift ±6 st, repitched back (formant-like)
        float attack  = 0.0f;    // Attack    −1..+1  +1 Tight (air trim + lift) … 0 Natural … −1 a ~3 s swell (log fade + softer layer)
                                 //                   tp107: APVTS knob v → 1 − 2v (knob 0 = Tight, 0.5 = Natural, 1 = the swell)
        float human   = 0.25f;   // Human      0..1   per-note detune/level/start/tone/timing + fake RR
        float release = 0.5f;    // Release    0..1   note-off decay TIME: 20 ms · authored · max(12 s, 2×authored) + release-sample level
        float noise   = 0.5f;    // Noise      0..1   mechanical-noise regions, trig on|off (0 silent, 0.5 authored, 1 = +12 dB)
        float sustain = 0.0f;    // Sustain    0..1   crossfade into the compile-time tail loop; 1 = holds forever
        float velo    = 0.75f;   // Velocity   0..1   velocity→amplitude depth (and velocity→brightness for Tone)
        float image   = 1.0f;    // Image      0..1.5 mid/side width of the sample (APVTS 0..1 × 1.5; default 0.667 → 1.0)
        int   artic   = 0;       // Articulation index into map.json "artics" (0 when the instrument has one)

        // ── tp105 amendment (Max, 2026-09-24): Attack leaves the front panel (the amp envelope owns attack);
        //    VIBRATO takes its knob + mod slot. Release becomes a real release linked to the amp envelope.
        float vibrato   = 0.0f;  // Vibrato    0..1   depth 0..±50 cents (taper: musical in the first half, wild at 1), per note
        float vibRate   = 5.5f;  // back panel 3..9 Hz (rises ~+0.6 Hz with depth, like a player)
        float vibDelay  = 0.35f; // back panel 0..2 s  onset delay, then a 250 ms fade-in (natural vibrato)
        float ampAttack  = 0.0f; // the voice's amp-envelope attack  (s) — Gentle onsets follow it, no double fade
        float ampRelease = 0.0f; // the voice's amp-envelope release (s) — note-off fade = max(this, Release knob time)
        int   velCurve  = 1;     // back panel 0 Soft · 1 Linear (authored) · 2 Hard
        int   tuning    = 1;     // back panel 0 As recorded · 1 Equal (apply the compiler's per-region "tfix" cents)

        // ── tp114 (the safety limiter, organics::kLimiterCeilingDb): the LARGEST linear gain the voice applies to this block
        //    of the engine's output on its way out (osc Volume × pan × the amp envelope × the unison norm). The engine holds
        //    its output under −1 dBFS ÷ this. ≤ 0 = unknown → the default path (Volume 0.5, centre pan, envelope 1: −9.03 dB).
        float outGain   = 0.0f;
    };

    /** A compiled .torg instrument, loaded in RAM (int16 samples + the region table + the
        [artic][128 keys][128 vel] → region-span lookup). Immutable once built; shared by refcount
        across every oscillator, voice and both banks. */
    class OrganicInstrument;

    /** Message-thread API. Owns the cache (one instrument object per id, freed ~5 s after the last
        user lets go — the tp63 lazy-release precedent). Loads on a background thread. */
    class OrganicsLibrary
    {
    public:
        static OrganicsLibrary& get();

        /** The library folder: <terrainDataDir>/Organics, overridden by the TERRAIN_ORGANICS_DIR
            environment variable (tests, dev machines). Contains index.json, ids.json, one folder per id. */
        juce::File root() const;

        /** index.json as parsed (the browser reads ONLY this): [{id,name,family,category,tags,sizeMB,licence,credit}]. */
        juce::var index();

        /** Re-read index.json / ids.json from disk (after an install or a Settings rescan). */
        void rescan();

        /** Load (or share the cached) instrument; the callback runs on the MESSAGE thread, with
            nullptr when the id is unknown or the folder is missing/corrupt. Never throws, never blocks. */
        void request (const juce::String& id, std::function<void (std::shared_ptr<const OrganicInstrument>)> done);

        /** ids.json is append-only: an index is never reused. −1 / "" when unknown. */
        int          idToIndex  (const juce::String& id) const;
        juce::String indexToId  (int index) const;

        /** Diagnostics for the memory gate: instruments resident and their total bytes. */
        int     residentCount() const;
        int64_t residentBytes() const;
    };

    /** One per oscillator slot per voice. Unison "players" live INSIDE (up to 16 siblings), so the
        voice calls noteOn once per note with the player count. Everything below is audio-thread safe:
        no locks, no allocation after prepare(). */
    class OrganicEngine
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void reset() noexcept;

        /** Lock-free swap (the caller hands over a shared_ptr prepared on the message thread; the old
            one is released off the audio thread by the library's deferred-release queue). Sounding
            regions of the old instrument fade out over 5 ms. nullptr = silent. */
        void setInstrument (std::shared_ptr<const OrganicInstrument> inst) noexcept;

        /** note 0..127, vel 0..1. players 1..16 (unison "Ensemble": each its own RR pick, Human seed and
            k·7 ms·detune timing). detuneCents[players] = the unison spread already scaled by the caller
            (0.25× on this engine). seed = the voice's NoteOn random seed (Human = 0 → fully deterministic). */
        void noteOn (int note, float vel, int players, const float* detuneCents, uint32_t seed) noexcept;

        /** Key released. pedalDown = CC64 held (the note keeps sounding until the pedal lifts). */
        void noteOff (bool pedalDown) noexcept;
        void pedal (bool down) noexcept;

        /** Hard stop with the house 5 ms fade (voice steal / choke). */
        void kill() noexcept;

        /** ADDS n stereo frames into L/R (does not clear them). pitchCents = everything the voice
            already applies (bend, fine, coarse, mod, glide, MPE) relative to the played note. */
        void render (const OrganicParams& p, float pitchCents, float* L, float* R, int n) noexcept;

        /** True while any region (attack, release or noise) is still sounding. */
        bool  isActive()  const noexcept;
        /** Peak-ish output level of the last block (0..1), for the visualiser feed. */
        float readLevel() const noexcept;
        /** Offline bounce: 8-tap windowed sinc instead of 4-point Hermite. */
        void  setNonRealtime (bool b) noexcept;

        OrganicEngine();
        ~OrganicEngine();
        OrganicEngine (const OrganicEngine&) = delete;
        OrganicEngine& operator= (const OrganicEngine&) = delete;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    /** Frozen integration constants. */
    namespace organics
    {
        constexpr int kEngineIndex    = 7;      // Engine::ORGANIC; 8..11 reserved + hidden
        constexpr int kEngineChoices  = 12;     // "WT","SAMP","GRAN","SPEC","FM","HARM","MODAL","ORGANIC","R8".."R11"
        constexpr int kDestBase       = 5272;   // ModDest::OrganicBase = the old NumDests (ShaperDepthEnd)
        constexpr int kDestPerOsc     = 10;     // dest = kDestBase + osc(0..7)·10 + knob(0..9)
        constexpr int kDestEnd        = kDestBase + 8 * kDestPerOsc;   // 5352 (tp104's NumDests)
        constexpr int kAttackDestBase = kDestEnd;                      // tp107: Attack A–H = 5352 + osc(0..7); NumDests 5360
        constexpr int kMaxRegionsPerOsc = 48;   // players × layers × release, steal-fade beyond
        // tp113 — THE ORGANICS OUTPUT MAKEUP (Max: "Why are the Organics so quiet? … turn them up … each one damn near at the
        //  same level"). The library is calibrated in LIBRARY UNITS: every instrument's centre key at v100 = −24 LUFS with the
        //  engine at unity (Tools/organics/engine_calibrate.py), the level every crest in the library allowed under a −1 dBFS
        //  v127 peak. Through the real processor at the default osc Volume and master that came out at −36 LUFS, while the
        //  init patch (Wavetable) plays −15.8 LUFS and FM −17.2 on the same note: the Organics sat 20 dB under the synth.
        //  ONE flat gain at the engine's output (attack, release and noise regions alike — no balance change; no limiter,
        //  no clipper) puts the calibration point at −24 + 20 = −4 LUFS at the engine = −16 LUFS at the plugin output, the
        //  Wavetable's level. Tools/organics/*.py and Tests/organics_audit.cpp read THIS constant (engine-level bars =
        //  library bars + kOutputMakeupDb). The v127 peak trim keeps its per-key shape, so peaks move up by the same 20 dB.
        // tp114b — LOWERED TO +12 dB (Max, 2026-09-26, "option 2": keep the limiter, lower the makeup so it catches attack
        //  spikes instead of holding instruments' bodies down). The sweep (organics-library.md §12.1): at +20 the limiter took
        //  > 0.5 dB off 22 of 74 instruments' v100 calibration point (meatbass −6.2) and held v127 notes down for up to 1.4 s;
        //  +12 is the highest value where ≤ 2 instruments lose > 0.5 dB at v100 (1: kalimba −0.8). Every v127 key the library
        //  trims to its −1 dBFS bar still reaches the limiter by ~3 dB at the default osc (−1 + 12 − 9.03 = +2 dBFS), so
        //  sustained v127 notes are held ~3 dB (not attack-only — that needs ≤ +9 dB, 11 dB under the Wavetable). Level: the
        //  calibration point is −24 + 12 = −12 LUFS at the engine, Organics median −23.9 LUFS at the plugin output (WT −15.8).
        constexpr float kOutputMakeupDb = 12.0f;
        inline float outputMakeupGain() noexcept { return 3.98107170553497f; }   // = 10^(kOutputMakeupDb / 20)
        static_assert (kOutputMakeupDb == 12.0f, "outputMakeupGain() is the linear value of kOutputMakeupDb — change both");
        // tp114 — THE ORGANICS SAFETY LIMITER (OrganicEngine.cpp, PeakGuard). The +20 dB makeup traded headroom for level: at
        //  the plugin output (default osc Volume + master) 50 % of the v127 key × RR takes went over 0 dBFS (mbira +13.3). Max,
        //  2026-09-26: "yes build the organics limiter — let's hear how that would sound … I don't want quality or volume
        //  changed … find a way to make these NOT clip." That is an EXPLICIT exception to the house law (no limiter, no clipper)
        //  for THIS ONE STAGE ONLY: the Organics engine's own output, per engine (= per oscillator per voice), stereo-linked,
        //  zero added latency (it reads ahead inside the block it has already rendered), gain exactly 1.0f whenever nothing
        //  reaches the ceiling (bit-identical to tp113). Nothing else in Terrain limits or clips.
        //  The ceiling is set where Max hears it: −1 dBFS at the OSCILLATOR'S OUTPUT. Downstream of the engine the voice applies
        //  osc Volume × pan × the amp envelope (5 ms attack, 200 ms down to the 0.7 sustain by default) × the unison norm, then
        //  the voice's −6 dB pre-FX pad × the ×2 instrument makeup (they cancel) — a linear, band-flat path (Tests/
        //  organics_limiter.sh gain). The voice hands the engine that gain's largest value in each block (OrganicParams::
        //  outGain), so the engine-side ceiling follows the knob and the envelope: −1 dBFS ÷ outGain. Without it (the tests
        //  that drive the engine alone) the ceiling assumes the DEFAULT path at the envelope's top: Volume 0.5 (−6.02 dB) ×
        //  the centre pan law √½ (−3.01) = −9.03 dB → +8.03 dBFS at the engine. The master Output knob, the filters / FX, a
        //  second oscillator or a chord can still take the sum past 0 dBFS downstream — this stage only guarantees that ONE
        //  Organics oscillator of one voice, as it leaves the voice's mixer, does not.
        constexpr float kLimiterOutputCeilingDb = -1.0f;     // dBFS at the plugin output (sample peak; 4× ISP measured ≤ 0)
        constexpr float kDefaultPathHeadroomDb  = 9.0309f;   // −20·log10 (0.5 · √½): engine output − plugin output, envelope at 1
        constexpr float kLimiterCeilingDb       = kLimiterOutputCeilingDb + kDefaultPathHeadroomDb;   // engine units: +8.03 dBFS
        // knob order for the dest block AND the page: Dynamics Tone Body Vibrato Human | Release Noise Sustain Velocity Image
        // (tp105: knob 3 is VIBRATO. tp107: ATTACK is back on page 2 with its OWN dest block, kAttackDestBase + osc)
    }
}
