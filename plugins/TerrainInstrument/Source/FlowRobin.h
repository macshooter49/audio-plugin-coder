#pragma once
// =============================================================================
//  FlowRobin.h  —  Terrain Instrument · FLOW mode ROBIN (the rotation brain)
//  Waves Crate
//
//  Header-only, NO JUCE. Runs on the audio thread only (called from
//  UnisonSynth::noteOn under the synth lock + ticked per block).
//
//  WHAT IT IS
//  ROBIN (FLOW mode 4) makes each note sound ONE oscillator, rotating through
//  the stations A/B/C/D like a sampler's round robin. This class is the BRAIN:
//  the Wheel card's every control decides *which* station a note lands on and
//  *how* it lands (per-station level/pan character, per-hit variation, timing
//  wobble, station glide, handover fades). The SynthVoice applies the hit; the
//  voice-side osc gating (robinGate) is unchanged from the original fb-era
//  round robin.
//
//  ── DICE DISCIPLINE (fb111 law) ──────────────────────────────────────────────
//  No mutable RNG. Every decision is a stateless arpHash01(key, index, PURPOSE):
//    · per-STATION character (Level/Pan humanize) hashes the station id — the
//      same station always leans the same way (that's what "character" means);
//    · per-HIT dice (Vary, Wobble, Shuffle bags, Random picks) hash a
//      monotonically increasing hit counter — deterministic, replayable.
//  Two instances fed the same notes make identical decisions (proved offline).
//
//  Build test: g++ -std=c++17 Source/FlowRobin_test.cpp -o /tmp/fr && /tmp/fr
// =============================================================================

#include "FlowArp.h"
#include <cmath>

namespace wc
{

// Wheel-card parameters (fb122). Defaults = the NEUTRAL DEFAULTS LAW:
// a textbook A→B→C→D cycle, one note per station, zero humanize —
// every knob starts at silence-of-effect so the first touch teaches it.
struct RobinExtParams
{
    bool  bank[4]   = { true, true, true, true };  // which stations take turns
    bool  aFirst    = true;    // after silence, the phrase restarts on the first station
    bool  retrig    = false;   // legato/mono notes restart the sound (no silent retargets)
    int   mode      = 0;       // 0 Cycle · 1 Shuffle · 2 Random · 3 Pong
    int   legato    = 0;       // 0 Keep (tied notes keep their station) · 1 New (advance)
    int   steal     = 0;       // 0 Follow (stolen notes advance) · 1 Stay (reuse the stolen station)
    int   release   = 1;       // 0 Hold (a re-pressed ringing key reuses its station) · 1 Free
    int   times     = 1;       // 1..4 notes each station plays before moving on
    int   reset     = 0;       // 0 Free · 1 Bar (restart each bar) · 2 Phrase (restart after silence)
    bool  backward  = false;   // run the order in reverse
    int   order[4]  = { 0, 1, 2, 3 };              // rotation order (a permutation)
    float vary = 0.f, wobble = 0.f, lvl = 0.f, pan = 0.f;
    float after = 2.0f;        // seconds of silence before the phrase resets
    float glide = 0.f, overlap = 0.f, fade = 0.f;  // station handover feel
};

// everything a note-on needs to land: consumed by SynthVoice::startNote
struct RobinHit
{
    int    station   = -1;     // -1 = no gating (robin inactive / degenerate)
    float  vel       = 1.0f;   // velocity scale (Vary + per-station Level)
    float  ampL      = 1.0f, ampR = 1.0f;          // per-station Pan (unity at center)
    int    delaySamp = 0;      // Wobble: humanized late start
    double glideFrom = -1.0;   // previous pitch to slide in from (-1 = none)
    float  glideSec  = 0.f;
    bool   changed   = false;  // the station differs from the previous hit
};

class FlowRobin
{
public:
    void prepare (double sampleRate) noexcept
    { sr_ = sampleRate > 0.0 ? sampleRate : 44100.0; reset(); }

    void reset() noexcept
    {
        now_ = -1; hitsOnCur_ = 0; hitN_ = 0; bagN_ = 0; bagLen_ = 0;
        pong_ = 1; wraps_ = 0; heldCount_ = 0; silentSec_ = 999.0; lastBar_ = -1;
        barResetPending_ = false; prevPitch_ = -1.0;
        staged_ = RobinHit(); hasStaged_ = false;
        for (int i = 0; i < 128; ++i) { noteStation_[i] = -1; noteRing_[i] = 0.f; }
    }

    void setActive (bool on) noexcept { if (on != on_) { on_ = on; if (on) silentSec_ = 999.0; } }
    void setExt (const RobinExtParams& p) noexcept { ext_ = p; if (ext_.times < 1) ext_.times = 1; }
    void setAudible (bool a, bool b, bool c, bool d) noexcept
    { aud_[0] = a; aud_[1] = b; aud_[2] = c; aud_[3] = d; }

    bool active()   const noexcept { return on_; }
    bool retrigOn() const noexcept { return on_ && ext_.retrig; }

    // per block, BEFORE the synth renders (silence clock + bar-reset detection)
    void tick (double ppq, int numSamples, bool playing) noexcept
    {
        if (! on_) return;
        const double dt = (double) numSamples / sr_;
        if (heldCount_ <= 0) silentSec_ += dt; else silentSec_ = 0.0;
        for (int i = 0; i < 128; ++i) if (noteRing_[i] > 0.f) noteRing_[i] -= (float) dt;
        if (ext_.reset == 1 && playing)
        {
            const long long bar = (long long) std::floor (ppq / 4.0);
            if (lastBar_ >= 0 && bar != lastBar_) barResetPending_ = true;
            lastBar_ = bar;
        }
        else lastBar_ = -1;
    }

    // ── note events (audio thread, under the synth lock) ─────────────────────
    void onNoteOn (int note, bool stole, int stolenStation, bool countHeld = true) noexcept
    {
        if (countHeld) ++heldCount_;
        if (! on_) { hasStaged_ = false; return; }

        const int n = cycleList (list_);
        const bool wasSilent = silentSec_ >= (double) (ext_.after > 0.05f ? ext_.after : 0.05f);
        silentSec_ = 0.0;               // a note IS activity — staccato can't fake silence
        RobinHit h;
        if (n <= 0) { stage (h, note); return; }               // nothing audible: no gating

        int st;
        const bool silentReset = wasSilent;
        if (stole && ext_.steal == 1 && stolenStation >= 0 && inList (stolenStation, n))
            st = stolenStation;                                 // Stay: the wheel holds still
        else if (ext_.release == 0 && note >= 0 && note < 128
                 && noteStation_[note] >= 0 && noteRing_[note] > 0.f && inList (noteStation_[note], n))
            st = noteStation_[note];                            // Hold: a ringing key keeps its station
        else if ((silentReset && ext_.aFirst) || (barResetPending_ && ext_.reset == 1)
                 || (silentReset && ext_.reset == 2))
        {
            st = list_[0];                                      // the phrase restarts
            bagLen_ = 0; pong_ = 1;
        }
        else if (now_ < 0 || ! inList (now_, n))
            st = list_[0];
        else if (hitsOnCur_ < ext_.times)
            st = now_;                                          // Times: this station plays again
        else
            st = advance (n);
        barResetPending_ = false;

        h.station = st;
        h.changed = (st != now_ && now_ >= 0);
        if (st != now_ || now_ < 0) hitsOnCur_ = 1; else ++hitsOnCur_;
        now_ = st;

        // humanize — station character is STABLE (hash of the station id),
        // per-hit dice roll off the hit counter (deterministic, fb111)
        const uint32_t su = (uint32_t) st;
        const uint32_t hn = (uint32_t) (hitN_++ & 0xffffffff);
        if (ext_.lvl > 0.001f || ext_.vary > 0.001f)
            h.vel = (1.f - ext_.lvl * 0.35f * arpHash01 (kSeed, su, 0x11u))
                  * (1.f - ext_.vary * 0.30f * arpHash01 (kSeed ^ 0x51EDu, hn, 0x22u));
        if (ext_.pan > 0.001f || ext_.vary > 0.001f)
        {
            float p = (arpHash01 (kSeed, su, 0x33u) - 0.5f) * ext_.pan * 0.9f
                    + (arpHash01 (kSeed ^ 0x51EDu, hn, 0x44u) - 0.5f) * ext_.vary * 0.2f;
            if (p >  0.5f) p =  0.5f;
            if (p < -0.5f) p = -0.5f;
            if (p >= 0.f) { h.ampL = 1.f - p * 0.8f; h.ampR = 1.f; }
            else          { h.ampL = 1.f; h.ampR = 1.f + p * 0.8f; }
        }
        if (ext_.wobble > 0.001f)
            h.delaySamp = (int) (arpHash01 (kSeed ^ 0x51EDu, hn, 0x55u)
                                 * ext_.wobble * 0.020f * (float) sr_);   // ≤ 20 ms — feel, not flam
        if (h.changed && ext_.glide > 0.001f && prevPitch_ >= 0.0)
        {
            h.glideFrom = prevPitch_;
            h.glideSec  = 0.04f + ext_.glide * 0.30f;
        }
        stage (h, note);
    }

    void onNoteOff (int note) noexcept
    {
        if (heldCount_ > 0) --heldCount_;
        silentSec_ = 0.0;                                      // silence starts at the RELEASE
        if (note >= 0 && note < 128) noteRing_[note] = 6.f;    // Hold window: ~a long release
    }
    void allOff() noexcept { heldCount_ = 0; }

    // Legato retarget (mono): Keep = nothing; New = the wheel advances and the
    // sounding voice crossfades its oscs to the new station (gates smooth).
    int onLegatoRetarget (int note, bool countHeld = true) noexcept
    {
        if (countHeld) { ++heldCount_; silentSec_ = 0.0; }      // a legato press is activity too
        if (! on_ || ext_.legato != 1) { prevPitch_ = (double) note; return -1; }
        const int n = cycleList (list_);
        if (n <= 1) { prevPitch_ = (double) note; return -1; }
        const int st = advance (n);
        now_ = st; hitsOnCur_ = 0; prevPitch_ = (double) note;
        return st;
    }

    const RobinHit& peekHit() const noexcept { return staged_; }   // UnisonSynth reads, voice consumes
    long long hitCount() const noexcept { return hitN_; }          // feed: pulse on new hits
    RobinHit takeHit() noexcept
    {
        if (! hasStaged_) return RobinHit();
        hasStaged_ = false;
        return staged_;
    }

    // handover feel for UnisonSynth (fade the old station's ringing tails)
    float handoverFadeSec()  const noexcept { return ext_.fade > 0.001f ? (0.35f - ext_.fade * 0.32f) : 0.f; }
    int   handoverWaitSamp() const noexcept { return (int) (ext_.overlap * 0.45f * (float) sr_); }
    bool  fadeEnabled()      const noexcept { return on_ && ext_.fade > 0.001f; }

    // ── viz (the Wheel feed) ─────────────────────────────────────────────────
    int nowStation() const noexcept { return now_; }
    int notesOnCur() const noexcept { return hitsOnCur_ < 1 ? 1 : hitsOnCur_; }
    long long wrapCount() const noexcept { return wraps_; }
    int cycleMaskViz() const noexcept
    { int m = 0; for (int k = 0; k < 4; ++k) if (ext_.bank[k] && aud_[k]) m |= (1 << k); return m; }
    int nextStation() noexcept
    {
        int tmp[4]; const int n = cycleList (tmp);
        if (n <= 0) return -1;
        if (now_ < 0) return tmp[0];
        if (ext_.mode == 2) return -1;                          // Random: unknowable
        if (ext_.mode == 1) return bagLen_ > 0 ? bag_[bagLen_ - 1] : -1;
        int i = 0; for (; i < n; ++i) if (tmp[i] == now_) break;
        if (i >= n) return tmp[0];
        if (ext_.mode == 3)
        {
            int j = i + pong_;
            if (j >= n || j < 0) j = i - pong_;
            return tmp[j < 0 ? 0 : (j >= n ? n - 1 : j)];
        }
        const int d = ext_.backward ? -1 : 1;
        return tmp[(i + d + n) % n];
    }

private:
    static constexpr uint32_t kSeed = 0x2B0B1235u;

    void stage (RobinHit& h, int note) noexcept
    {
        staged_ = h; hasStaged_ = true;
        if (note >= 0 && note < 128 && h.station >= 0) noteStation_[note] = h.station;
        prevPitch_ = (double) note;
    }

    // the cycle = order[] filtered by (bank ∧ audible); falls back to audible-only
    int cycleList (int* out) const noexcept
    {
        // dedupe: host automation can momentarily make order[] non-permutative
        bool seen[4] = { false, false, false, false };
        int n = 0;
        for (int i = 0; i < 4; ++i)
        {
            const int st = ext_.order[i] & 3;
            if (! seen[st] && ext_.bank[st] && aud_[st]) { seen[st] = true; out[n++] = st; }
        }
        if (n == 0)
        {
            for (int i = 0; i < 4; ++i) seen[i] = false;
            for (int i = 0; i < 4; ++i)
            { const int st = ext_.order[i] & 3; if (! seen[st] && aud_[st]) { seen[st] = true; out[n++] = st; } }
        }
        return n;
    }
    bool inList (int st, int n) const noexcept
    { for (int i = 0; i < n; ++i) if (list_[i] == st) return true; (void) n; return false; }

    int advance (int n) noexcept
    {
        if (n == 1) { ++wraps_; return list_[0]; }
        int i = 0; for (; i < n; ++i) if (list_[i] == now_) break;
        if (i >= n) i = 0;
        switch (ext_.mode)
        {
            case 1:                                             // Shuffle: hash bag, anti-repeat
            {
                if (bagLen_ <= 0)
                {
                    for (int k = 0; k < n; ++k) bag_[k] = list_[k];
                    bagLen_ = n;
                    const uint32_t bn = (uint32_t) (bagN_++ & 0xffffffff);
                    for (int k = n - 1; k > 0; --k)
                    {
                        const int r = (int) (arpHash01 (kSeed ^ 0xBA6Bu, bn, (uint32_t) k) * (float) (k + 1));
                        const int rr = r > k ? k : r;
                        const int t = bag_[k]; bag_[k] = bag_[rr]; bag_[rr] = t;
                    }
                    if (bag_[bagLen_ - 1] == now_ && bagLen_ > 1)   // no immediate repeat
                    { const int t = bag_[bagLen_ - 1]; bag_[bagLen_ - 1] = bag_[0]; bag_[0] = t; }
                    ++wraps_;
                }
                return bag_[--bagLen_];
            }
            case 2:                                             // Random: in-cycle, no repeat
            {
                const uint32_t hn = (uint32_t) (hitN_ & 0xffffffff);
                int pick = (int) (arpHash01 (kSeed ^ 0xD1CEu, hn, 0x66u) * (float) n);
                if (pick >= n) pick = n - 1;
                if (list_[pick] == now_) pick = (pick + 1) % n;
                if (((hitN_ + 1) % (long long) n) == 0) ++wraps_;
                return list_[pick];
            }
            case 3:                                             // Pong: bounce at the ends
            {
                int j = i + pong_;
                if (j >= n) { pong_ = -1; j = n - 2; ++wraps_; }
                else if (j < 0) { pong_ = 1; j = 1; }
                if (j < 0) j = 0; if (j >= n) j = n - 1;
                return list_[j];
            }
            default:                                            // Cycle
            {
                const int d = ext_.backward ? -1 : 1;
                const int j = (i + d + n) % n;
                if ((! ext_.backward && j <= i) || (ext_.backward && j >= i)) ++wraps_;
                return list_[j];
            }
        }
    }

    RobinExtParams ext_;
    bool   on_ = false;
    bool   aud_[4] = { true, false, false, false };
    double sr_ = 44100.0;
    int    now_ = -1, hitsOnCur_ = 0;
    long long hitN_ = 0, bagN_ = 0, wraps_ = 0;
    int    bag_[4] = { 0 }; int bagLen_ = 0;
    int    pong_ = 1;
    int    list_[4] = { 0 };
    int    heldCount_ = 0;
    double silentSec_ = 999.0;
    long long lastBar_ = -1; bool barResetPending_ = false;
    double prevPitch_ = -1.0;
    RobinHit staged_; bool hasStaged_ = false;
    int    noteStation_[128]; float noteRing_[128] = { 0.f };
};

} // namespace wc
