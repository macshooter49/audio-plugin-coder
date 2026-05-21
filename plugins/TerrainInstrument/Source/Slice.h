// Slice.h
// Header-only data model + helpers for the Terrain Instrument slicer.
//
// A Slice describes a region of the loaded sample plus its per-region
// playback knobs (reverse, pitch offset). The processor holds an immutable
// snapshot of the slice list as a shared_ptr<const vector<Slice>> swapped
// atomically from the UI thread; audio threads (TerrainSynth::noteOn) read
// via std::atomic_load — no locks on the audio path.
//
// Two helpers ship alongside the struct because they live entirely in
// sample-buffer-space and don't depend on any other plugin state:
//   - findNearestZeroCrossing: snap a sample index to the nearest zero
//     crossing within a search radius (used when slices are created or
//     resized so they don't click on playback).
//   - detectTransients: lightweight onset detector used by the AUTO button.
//
// Both are deterministic and fast enough to run on the message thread for
// reasonable sample lengths (< 2 minutes). For longer samples (e.g. user's
// "10-minute one shot" dream) callers should run them on a background
// thread; the editor wires this for AUTO.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

namespace tw
{
    /** Per-chop warp mode. Default None preserves current resample behavior;
     *  Beats/Tones/Texture activate the warp engines (Phase 1 ships Tones only). */
    enum class WarpMode : uint8_t
    {
        None    = 0,
        Beats   = 1,
        Tones   = 2,
        Texture = 3
    };

    /** A single playable region of the loaded sample, plus its per-region knobs. */
    struct Slice
    {
        juce::int64 startSample      = 0;       // inclusive
        juce::int64 endSample        = 0;       // exclusive
        bool        reverse          = false;
        float       pitchOffsetSemis = 0.0f;    // -12..+12 (one octave each way — wider ranges sound chipmunky)
        WarpMode    warpMode         = WarpMode::None;   // per-chop warp engine selection
        float       stretchRatio     = 1.0f;             // clamped 0.1..15.0; ignored when warpMode == None

        juce::int64 length() const noexcept { return endSample - startSample; }
    };

    /** Atomically-swappable snapshot type used between UI and audio threads. */
    using SliceList     = std::vector<Slice>;
    using SliceListPtr  = std::shared_ptr<const SliceList>;

    // ──────────────────────────────────────────────────────────────────────
    // JSON serialization — matches the schema the WebView expects.
    // ──────────────────────────────────────────────────────────────────────
    inline juce::String slicesToJson (const SliceList& slices)
    {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        juce::Array<juce::var> arr;
        for (const auto& s : slices)
        {
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("start",        (juce::int64) s.startSample);
            o->setProperty ("end",          (juce::int64) s.endSample);
            o->setProperty ("reverse",      s.reverse);
            o->setProperty ("pitch",        (double) s.pitchOffsetSemis);
            o->setProperty ("warpMode",     (int) s.warpMode);
            o->setProperty ("stretchRatio", (double) s.stretchRatio);
            arr.add (juce::var (o.get()));
        }
        obj->setProperty ("slices", arr);
        return juce::JSON::toString (juce::var (obj.get()), true /*allOnOneLine*/);
    }

    inline SliceList slicesFromJson (const juce::String& json)
    {
        SliceList out;
        auto v = juce::JSON::parse (json);
        if (! v.isObject()) return out;
        auto arr = v.getProperty ("slices", juce::var());
        if (! arr.isArray()) return out;
        for (int i = 0; i < arr.size(); ++i)
        {
            auto e = arr[i];
            if (! e.isObject()) continue;
            Slice s;
            s.startSample      = (juce::int64) (long long) e.getProperty ("start", 0);
            s.endSample        = (juce::int64) (long long) e.getProperty ("end",   0);
            s.reverse          =                       (bool) e.getProperty ("reverse", false);
            s.pitchOffsetSemis = (float) (double)            e.getProperty ("pitch",   0.0);

            // Warp fields default to None / 1.0 when absent — old presets are
            // safe to load.
            const int wmRaw    = (int) e.getProperty ("warpMode", 0);
            s.warpMode         = (wmRaw >= 0 && wmRaw <= 3)
                                    ? static_cast<WarpMode> (wmRaw)
                                    : WarpMode::None;
            s.stretchRatio     = juce::jlimit (0.1f, 15.0f,
                                    (float) (double) e.getProperty ("stretchRatio", 1.0));

            if (s.endSample > s.startSample)
                out.push_back (s);
        }
        return out;
    }

    // ──────────────────────────────────────────────────────────────────────
    // Zero-crossing snap: find the nearest sample index where channel-0 of
    // the buffer crosses zero, within ±radius of the target. Returns target
    // unchanged if no crossing found (e.g., signal is silent).
    // ──────────────────────────────────────────────────────────────────────
    inline juce::int64 findNearestZeroCrossing (const juce::AudioBuffer<float>& buf,
                                                juce::int64 target,
                                                int radius = 256) noexcept
    {
        if (buf.getNumSamples() < 2) return target;
        const auto* data = buf.getReadPointer (0);
        const juce::int64 lastIdx = buf.getNumSamples() - 2;
        target = juce::jlimit ((juce::int64) 0, lastIdx, target);

        const juce::int64 lo = juce::jmax ((juce::int64) 0, target - radius);
        const juce::int64 hi = juce::jmin (lastIdx,        target + radius);

        juce::int64 best = target;
        int         bestDist = std::numeric_limits<int>::max();
        for (juce::int64 i = lo; i < hi; ++i)
        {
            // Sign change between data[i] and data[i+1] => zero crossing.
            if ((data[i] <= 0.0f && data[i + 1] >= 0.0f) ||
                (data[i] >= 0.0f && data[i + 1] <= 0.0f))
            {
                int d = (int) std::abs (i - target);
                if (d < bestDist) { bestDist = d; best = i; }
            }
        }
        return best;
    }

    // ──────────────────────────────────────────────────────────────────────
    // Equal-grid slicing: split the sample into N slices of equal length,
    // optionally snapping each boundary to the nearest zero crossing.
    // ──────────────────────────────────────────────────────────────────────
    inline SliceList makeGridSlices (const juce::AudioBuffer<float>& buf,
                                     int numSlices,
                                     bool snapToZeroCross = true)
    {
        SliceList out;
        const juce::int64 total = buf.getNumSamples();
        if (total < 2 || numSlices < 1) return out;

        for (int i = 0; i < numSlices; ++i)
        {
            juce::int64 start = (juce::int64) ((double) total * (double) i       / (double) numSlices);
            juce::int64 end   = (juce::int64) ((double) total * (double) (i + 1) / (double) numSlices);
            if (snapToZeroCross && i > 0)
                start = findNearestZeroCrossing (buf, start, 512);
            if (snapToZeroCross && i + 1 < numSlices)
                end   = findNearestZeroCrossing (buf, end,   512);

            // Last slice always ends at total to avoid leaving a tail unassigned.
            if (i + 1 == numSlices) end = total;

            if (end > start) out.push_back ({ start, end, false, 0.0f });
        }
        return out;
    }

    // ──────────────────────────────────────────────────────────────────────
    // Transient detection (amplitude-based, lightweight).
    //
    // Approach:
    //   1. Compute a fast-attack / slow-release envelope follower.
    //   2. Onset Detection Function (ODF) = max(0, env[n] - env[n - hop]).
    //   3. Threshold ODF against (mean + sensitivity * stddev).
    //   4. Peak-pick with a minimum-distance-between-peaks constraint.
    //   5. Snap each marker to the nearest zero crossing.
    //
    // Inputs:
    //   buf            : sample buffer (mono is sufficient — uses channel 0).
    //   sampleRate     : native sample rate of the buffer.
    //   sensitivity    : 0..1, higher = more peaks detected.
    //   minSliceLenMs  : minimum allowed length between consecutive markers.
    //
    // Output: a SliceList covering the entire sample. First slice always
    // starts at 0; subsequent slice start = previous slice end = onset
    // location. If no onsets are found at the requested sensitivity, returns
    // a single slice spanning the whole buffer.
    // ──────────────────────────────────────────────────────────────────────
    inline SliceList detectTransients (const juce::AudioBuffer<float>& buf,
                                       double sampleRate,
                                       float sensitivity = 0.5f,
                                       float minSliceLenMs = 60.0f)
    {
        SliceList out;
        const int total = buf.getNumSamples();
        if (total < 64 || sampleRate <= 0.0)
        {
            if (total > 0) out.push_back ({ 0, total, false, 0.0f });
            return out;
        }

        const auto* L = buf.getReadPointer (0);
        const auto* R = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : L;

        // 1) Envelope follower (peak-track). Time constants approximated by
        //    one-pole coefficients.
        const double attackMs  = 1.0;
        const double releaseMs = 25.0;
        const float  attackCoef  = (float) std::exp (-1.0 / (attackMs  * 0.001 * sampleRate));
        const float  releaseCoef = (float) std::exp (-1.0 / (releaseMs * 0.001 * sampleRate));

        std::vector<float> env;
        env.reserve ((size_t) total);
        float e = 0.0f;
        for (int i = 0; i < total; ++i)
        {
            const float mag = 0.5f * (std::abs (L[i]) + std::abs (R[i]));
            const float c   = mag > e ? attackCoef : releaseCoef;
            e = mag + c * (e - mag);
            env.push_back (e);
        }

        // 2) ODF — positive jumps in env at a fixed hop size (~2ms).
        const int hop = juce::jmax (32, (int) (sampleRate * 0.002));
        std::vector<float> odf ((size_t) total, 0.0f);
        for (int i = hop; i < total; ++i)
        {
            const float d = env[(size_t) i] - env[(size_t) (i - hop)];
            odf[(size_t) i] = d > 0.0f ? d : 0.0f;
        }

        // 3) Threshold = mean + sensitivity-shaped stddev factor.
        //    sensitivity in 0..1 maps to a threshold multiplier 2.5..0.4
        //    (lower threshold = more peaks).
        double mean = 0.0;
        for (auto v : odf) mean += v;
        mean /= (double) odf.size();
        double var = 0.0;
        for (auto v : odf) var += (v - mean) * (v - mean);
        var /= (double) odf.size();
        const double stddev = std::sqrt (var);
        const double thrMul = juce::jmap ((double) sensitivity, 0.0, 1.0, 2.5, 0.4);
        const float threshold = (float) (mean + thrMul * stddev);

        // 4) Peak-pick: local max above threshold, min-distance enforced.
        const int minDist = juce::jmax (1, (int) (minSliceLenMs * 0.001 * sampleRate));
        std::vector<int> markers;
        markers.push_back (0);
        for (int i = hop + 4; i < total - 4; ++i)
        {
            if (odf[(size_t) i] < threshold) continue;
            // Local maximum check (5-sample window).
            bool isPeak = true;
            for (int k = -3; k <= 3; ++k)
            {
                if (k == 0) continue;
                if (odf[(size_t) (i + k)] > odf[(size_t) i]) { isPeak = false; break; }
            }
            if (! isPeak) continue;
            // Min-distance from previous marker.
            if (i - markers.back() < minDist) continue;
            markers.push_back (i);
        }

        // 5) Snap markers (except the first, which is already at 0) to nearest
        //    zero crossing for click-free slice playback.
        for (size_t m = 1; m < markers.size(); ++m)
            markers[m] = (int) findNearestZeroCrossing (buf, markers[m], 384);

        // Build the slice list.
        for (size_t m = 0; m < markers.size(); ++m)
        {
            const juce::int64 start = (juce::int64) markers[m];
            const juce::int64 end   = (m + 1 < markers.size())
                                      ? (juce::int64) markers[m + 1]
                                      : (juce::int64) total;
            if (end > start)
                out.push_back ({ start, end, false, 0.0f });
        }

        // Safety: if for some reason we ended up with nothing, return the
        // whole sample as one slice.
        if (out.empty())
            out.push_back ({ 0, (juce::int64) total, false, 0.0f });

        return out;
    }
}
