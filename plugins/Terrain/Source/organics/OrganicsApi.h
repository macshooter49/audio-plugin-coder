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
        float tone    = 0.0f;    // Tone      −1..+1  velocity-aware tilt, pivot 700 Hz, ±9 dB
        float body    = 0.0f;    // Body      −1..+1  zone lookup shift ±6 st, repitched back (formant-like)
        float attack  = 0.0f;    // Attack    −1..+1  Gentle (fade + softer layer) … Natural … Tight (air trim + lift)
        float human   = 0.25f;   // Human      0..1   per-note detune/level/start/tone/timing + fake RR
        float release = 0.5f;    // Release    0..1   tp105: note-off decay TIME 20 ms..10 s (taper) + release-sample level; 1 = ≥10 s
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
        constexpr int kDestEnd        = kDestBase + 8 * kDestPerOsc;   // 5352 = the new NumDests
        constexpr int kMaxRegionsPerOsc = 48;   // players × layers × release, steal-fade beyond
        // knob order for the dest block AND the page: Dynamics Tone Body Attack Human | Release Noise Sustain Velocity Image
    }
}
