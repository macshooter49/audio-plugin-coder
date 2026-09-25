// OrganicsLibrary.h — THE ORGANICS ENGINE: the compiled instrument (.torg v1 in RAM) + library internals.
//
// OrganicsApi.h (frozen) declares OrganicsLibrary and an opaque OrganicInstrument. This header defines the
// instrument for the runtime (OrganicEngine.cpp) and the tests. The integration never needs it.
//
// .torg v1 semantics the runtime implements (design §4.1 + contract §3):
//   • region.end, region.le, region.tailLe are EXCLUSIVE frame indices (test.sine: le − ls = 23851 = 65 cycles).
//   • rr = [position (0-based), seq length]; rand = [lo, hi) of a per-note uniform draw.
//   • Velocity crossfade = SFZ xfin/xfout: the region is audible over [lv, hv]; it fades IN (equal power)
//     over [lv, xfLo] and OUT over [xfHi, hv]. xfLo == lv and xfHi == hv mean "no authored band".
//     The compiler writes contiguous layers (xfLo/xfHi == lv/hv), so where two layers of a zone butt together
//     (hv + 1 == next lv) the runtime adds an equal-power band at the seam reaching 30 % into each layer (1.5..8
//     velocities per side): a layer change is never a hard switch, and the live Dynamics sweep rides the same band.
//   • cents = the region's tune (added to playback pitch). Level = gainDb × gainNorm × velCurve(vel), gainNorm
//     linear (the compiler already folded in the 50 % natural-loudness restore, design §2.3).
//   • kind "release" AND kind "noise" regions trigger at NOTE-OFF (key-off / damper / release noise); release
//     follows the Release knob, noise the Noise knob; rt_decay (dB per held second) applies to both.
//   • pan −100..100 · xf = frames before le, blended with the same frames before ls · "markers" ignored.
#pragma once

#include "OrganicsApi.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace tw
{
    namespace org
    {
        enum class Kind : uint8_t { Attack = 0, Release = 1, Noise = 2 };
        enum class Loop : uint8_t { NoLoop = 0, OneShot = 1, Continuous = 2, Sustain = 3 };

        constexpr int kPad     = 8;      // zero frames before AND after every sample (Hermite ±2, Sinc8 −3..+4)
        constexpr int kEnvWin  = 256;    // envelope-table window (sample frames)
        constexpr int kMaxKinds = 3;

        /** One decoded sample: int16, interleaved, kPad zero frames each side (no clamps in the reader). */
        struct Sample
        {
            std::vector<int16_t> pcm;
            int     channels   = 1;
            int64_t frames     = 0;
            double  sampleRate = 48000.0;
            std::vector<float> envDb;                 // RMS dB (full-scale 0 dB) per kEnvWin frames — Sustain + retire

            const int16_t* data() const noexcept { return pcm.data() + (size_t) (kPad * channels); }
            float envAt (double frame) const noexcept
            {
                if (envDb.empty()) return -140.0f;
                const double x = frame * (1.0 / kEnvWin) - 0.5;           // table values sit at window centres
                const int last = (int) envDb.size() - 1;
                if (x <= 0.0) return envDb[0];
                const int w = (int) x;
                if (w >= last) return envDb[(size_t) last];
                const float f = (float) (x - (double) w);
                return envDb[(size_t) w] + f * (envDb[(size_t) w + 1] - envDb[(size_t) w]);
            }
        };

        struct Region
        {
            // ── authored (map.json) ──
            int   artic = 0, smp = 0;
            Kind  kind = Kind::Attack;
            int   lk = 0, hk = 127, lv = 1, hv = 127, xfLo = 1, xfHi = 127, root = 60;
            float cents = 0, gainDb = 0, gainNorm = 1, pan = 0;
            int64_t start = 0, end = 0, onset = 0;
            Loop  loop = Loop::NoLoop;
            int64_t ls = 0, le = 0, xf = 0, tailLs = 0, tailLe = 0;
            int   rrPos = 0, rrLen = 1;
            float randLo = 0, randHi = 1;
            int   grp = 0, offBy = 0;
            bool  offFast = false;
            float envA = 0, envH = 0, envD = 0, envS = 1, envR = 0.25f;
            float rtDecay = 0;
            bool  trigOn = false;                     // tp105: kind "noise" + "trig":"on" → starts WITH the note (default "off" = at note-off)
            float tfix = 0;                           // tp105: cents, the compiler's measured deviation from 12-TET (applied when Tuning = Equal)
            float velCurve[128] {};                   // amp_velcurve evaluated at each velocity 0..127

            // ── derived at load ──
            float fiLo = 0, fiHi = 0, foLo = 0, foHi = 0;   // continuous-velocity gain window (see header note)
            float staticGain = 1;                     // dB→lin(gainDb) · gainNorm
            int   grpIdx = -1, offByIdx = -1;         // dense choke-group indices (−1 = none)
            bool  isRR = false;                       // part of a real round-robin set
            float refDb = -120, tailSlopeDb = 0;      // Sustain: level 150 ms after onset; dB/s decay at the tail loop
            bool  decaying() const noexcept { return loop == Loop::NoLoop || loop == Loop::OneShot; }
            bool  hasTail()  const noexcept { return tailLe > tailLs + 16; }
            bool  looping()  const noexcept { return (loop == Loop::Continuous || loop == Loop::Sustain) && le > ls + 1; }
        };

        struct Span { uint32_t first = 0; uint32_t count = 0; };
    }

    class OrganicInstrument
    {
    public:
        juce::String id, name, family, category, credit;
        juce::StringArray artics;
        bool hasNoise = false, hasRelease = false;
        int  polyMax = 32;
        int  numArtics = 1;

        std::vector<org::Sample> samples;
        std::vector<org::Region> regions;

        /** [artic][kind][128 keys][128 vel] → the candidate regions (all layers in reach, all RR variants). */
        const org::Span& span (int artic, org::Kind k, int key, int vel) const noexcept
        {
            return spans[(((size_t) artic * org::kMaxKinds + (size_t) k) * 128 + (size_t) key) * 128 + (size_t) vel];
        }
        const uint16_t* list (const org::Span& s) const noexcept { return lists.data() + s.first; }
        bool keyHasRR (int artic, int key) const noexcept { return rrKeys[(size_t) artic * 128 + (size_t) key] != 0; }
        /** tp105 NO-SILENCE law: the nearest key (any velocity) that HAS an attack region in this articulation —
            the key itself when mapped; outside the authored range the edge zone plays, repitched to the played note. */
        int  mappedKey (int artic, int key) const noexcept
        {
            key = key < 0 ? 0 : (key > 127 ? 127 : key);
            return nearKey.empty() ? key : (int) nearKey[(size_t) artic * 128 + (size_t) key];
        }

        int64_t bytes = 0;               // RAM held (samples + tables)
        int     numGroups = 0;           // dense choke-group count

        // ── shared performance state: lock-free, touched on the audio thread at note-on ──
        // Round robin is a property of the KEY on the instrument (a repeated note alternates even when a
        // different voice plays it), so it lives here, not in the per-voice engine.
        std::atomic<uint32_t>* rrSeq() const noexcept      { return rrSeq_.get(); }      // [artic*128 + key]
        std::atomic<int32_t>*  rrLast() const noexcept     { return rrLast_.get(); }     // last random pick (region)
        std::atomic<int32_t>*  fakeLast() const noexcept   { return fakeLast_.get(); }   // last fake-RR choice per key
        /** tp107: the last noise VARIANT per [(artic·2 + trigOn)·128 + key] (−1 = none yet) — the round-robin's no-repeat memory,
            on the instrument like rrLast (a repeated note alternates its noise even when another voice plays it). */
        std::atomic<int32_t>*  noiseLast() const noexcept  { return noiseLast_.get(); }
        std::atomic<uint32_t>* groupEpoch() const noexcept { return groupEpoch_.get(); } // choke-group trigger count
        /** Back to the loaded state (the determinism gate; a preset reload). Not for the audio thread mid-note. */
        void resetPerformanceState() const noexcept;

        /** Parse map.json + decode samples. nullptr (and *error set) on any missing/corrupt input. */
        static std::shared_ptr<OrganicInstrument> loadFromFolder (const juce::File& folder, juce::String* error = nullptr);

        std::vector<org::Span>  spans;
        std::vector<uint16_t>   lists;
        std::vector<uint8_t>    rrKeys;
        std::vector<uint8_t>    nearKey;        // [artic*128 + key] → nearest key with an attack region (tp105)
    private:
        std::unique_ptr<std::atomic<uint32_t>[]> rrSeq_, groupEpoch_;
        std::unique_ptr<std::atomic<int32_t>[]>  rrLast_, fakeLast_, noiseLast_;
        friend struct OrganicLoader;
    };

    namespace org
    {
        /** Audio-thread safe: hands an instrument reference to the library, which drops it on its own thread.
            Returns false only if the fixed queue is full (the caller keeps it and retries next block). */
        bool deferRelease (std::shared_ptr<const OrganicInstrument>& p) noexcept;
        /** Library thread / tests: destroy everything queued. */
        void drainDeferredReleases();
    }
}
