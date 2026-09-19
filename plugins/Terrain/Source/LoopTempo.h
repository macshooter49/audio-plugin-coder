#pragma once
// ══════════════════════════════════════════════════════════════════════════════════════════════
//  LoopTempo.h — tp57 · Terrain · WHAT TEMPO IS THIS SAMPLE?
//
//  Max: "we had a lock icon next to the BPM ... if something is locked onto that BPM then it has to
//  be stretched to the BPM so everything can stay in time. If I have multiple drum loops that I'm
//  chopping up, the drum loops wouldn't stay out of whack."
//
//  To stretch a loop to the host you first have to know the loop's OWN tempo, and a sample file does
//  not tell you. Two readings, in this order, because that is the order of their reliability:
//
//    1. THE NAME. Producers label loops with their tempo and the libraries ship them that way —
//       "Pad 138.wav", "174 dnb break.wav", "Loop 4 Bars 120bpm.wav". A number a human wrote is
//       better evidence than anything we can infer from the audio.
//    2. THE LENGTH. A loop is almost always a whole number of BARS. Given the length in seconds,
//       each candidate bar count implies a tempo; the one that lands nearest the host's own tempo is
//       the answer. This is the standard heuristic and it is right far more often than it is wrong
//       on material that IS a loop — and on material that is not (a one-shot, a vocal phrase) it is
//       wrong harmlessly, because the lock is a switch the user chose to flip.
//
//  🚨 IT RETURNS 0 WHEN IT DOES NOT KNOW, AND 0 MEANS DO NOT STRETCH. A guess dressed as an answer
//  would put every unlabelled one-shot through a time-stretcher for no reason.
//
//  JUCE-free on purpose, so Tests/looptempo_cert.cpp measures THE SHIPPED CODE (the Vibrato.h law).
//  Build the gate: clang++ -std=c++17 -O2 -I Source Tests/looptempo_cert.cpp -o /tmp/ltcert
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cmath>
#include <cstddef>

namespace tw { namespace looptempo
{
    // The window a musical tempo can live in. Outside it a number in a file name is a catalogue
    // number, a sample rate or a drum machine ("808 Kick", "909 Clap"), never a tempo.
    inline constexpr double kMinBpm = 50.0;
    inline constexpr double kMaxBpm = 220.0;

    /** Tempo written into a file name, or 0.
     *  Scans every run of digits. A run immediately followed by "bpm" (any case, an optional space
     *  or '_' or '-' between) wins outright. Otherwise the LAST in-range run wins, because the
     *  tempo is conventionally the tail of the name ("Loop 4 Bars 120") and a leading number is
     *  more often an index. Decimal tempos ("128.5") are read whole. */
    inline double fromName (const char* name) noexcept
    {
        if (name == nullptr) return 0.0;
        double best = 0.0;
        for (std::size_t i = 0; name[i] != '\0'; )
        {
            if (name[i] < '0' || name[i] > '9') { ++i; continue; }
            std::size_t j = i; double v = 0.0;
            while (name[j] >= '0' && name[j] <= '9') { v = v * 10.0 + (name[j] - '0'); ++j; }
            if (name[j] == '.' && name[j + 1] >= '0' && name[j + 1] <= '9')
            {
                std::size_t d = j + 1; double f = 0.0, sc = 0.1;
                while (name[d] >= '0' && name[d] <= '9') { f += (name[d] - '0') * sc; sc *= 0.1; ++d; }
                v += f; j = d;
            }
            // "bpm" right after it — allow one separator
            std::size_t k = j;
            if (name[k] == ' ' || name[k] == '_' || name[k] == '-') ++k;
            const bool tagged = (name[k] == 'b' || name[k] == 'B')
                             && (name[k+1] == 'p' || name[k+1] == 'P')
                             && (name[k+2] == 'm' || name[k+2] == 'M');
            if (v >= kMinBpm && v <= kMaxBpm)
            {
                if (tagged) return v;     // a labelled tempo is the end of the argument
                best = v;                 // otherwise keep the last one we saw
            }
            i = j;
        }
        return best;
    }

    /** Tempo implied by the LENGTH, assuming the file is a whole number of 4/4 bars, or 0.
     *  `referenceBpm` breaks the tie between candidates (the host's tempo — the user is asking for
     *  this loop to sit with everything else, so the reading nearest their session is the one they
     *  meant). Returns 0 for a length that no plausible bar count can explain. */
    inline double fromLength (double lengthSeconds, double referenceBpm) noexcept
    {
        if (! (lengthSeconds > 0.05) || ! (lengthSeconds < 600.0)) return 0.0;
        const double ref = (referenceBpm >= kMinBpm && referenceBpm <= kMaxBpm) ? referenceBpm : 120.0;
        static constexpr int kBars[] = { 1, 2, 4, 8, 16, 32 };
        double best = 0.0, bestErr = 1.0e9;
        for (int bars : kBars)
        {
            const double bpm = (bars * 4) * 60.0 / lengthSeconds;
            if (bpm < kMinBpm || bpm > kMaxBpm) continue;
            // compare in OCTAVES of tempo, so 90 vs 180 is one unit away from 120 either side and
            // the nearest reading wins on musical distance rather than arithmetic distance
            const double err = std::fabs (std::log2 (bpm / ref));
            if (err < bestErr) { bestErr = err; best = bpm; }
        }
        return best;
    }

    /** The reading the plugin uses: the name first, the length second, 0 when neither speaks. */
    inline double detect (const char* name, double lengthSeconds, double referenceBpm) noexcept
    {
        const double named = fromName (name);
        if (named > 0.0) return named;
        return fromLength (lengthSeconds, referenceBpm);
    }

    /** How much longer the sample has to play to sit at `hostBpm`. 1.0 = leave it alone.
     *  A 138 BPM loop in a 120 BPM session has to last 138/120 = 1.15x as long. */
    inline double stretchTo (double sourceBpm, double hostBpm) noexcept
    {
        if (! (sourceBpm > 1.0) || ! (hostBpm > 1.0)) return 1.0;
        double r = sourceBpm / hostBpm;
        // ⚠️ FOLD INTO THE HALF/DOUBLE WINDOW. A 174 BPM break in a 90 BPM session is not meant to
        // crawl at 1.93x — it is a double-time break and the musician hears it as 87. Without this
        // the lock would "fix" the tempo by destroying the part, which is the opposite of the ask.
        while (r > 1.45) r *= 0.5;
        while (r < 0.69) r *= 2.0;
        return r;
    }
}}
