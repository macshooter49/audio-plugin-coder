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
#include <initializer_list>

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

    // ══════════════════════════════════════════════════════════════════════════════════════════
    //  tp58 — THE THIRD READING: LISTEN TO IT.
    //
    //  Max: "the audio in fact does NOT stretch to the bpm whenever I have it on global LOCK."
    //  Half of that was the DSP (WarpProcessor's note-on pole — see Tests/bpmlock_cert.cpp); the
    //  other half is here.  A name and a length only answer for files that were labelled or
    //  exported to an exact bar count.  "Drum Loop.wav", a loop with a tail, a chopped-out break —
    //  the first two readings say nothing, `detect` returned 0, 0 means do not stretch, and the
    //  lock did nothing at all with no way to tell WHY.
    //
    //  So: an onset-strength envelope and an autocorrelation over it, which is the standard
    //  beat-period estimator and the one that does not need a note grid or an FFT.
    //    · frame the signal at 5 ms and take each frame's RMS
    //    · onset strength = the POSITIVE part of the log-energy rise (a drop is not an onset), so
    //      the measure is level-independent — a quiet loop and a loud one score the same
    //    · autocorrelate the mean-removed envelope over lags of 50..220 BPM
    //    · score each lag with its HARMONICS (lag, 2·lag, 3·lag).  A beat that repeats every
    //      period also repeats every two and three, so the comb pulls the answer to the true beat
    //      instead of the half-tempo reading a bare autocorrelation peak usually wins with.
    //    · fold to the octave nearest the session, for the same reason stretchTo folds
    //
    //  🚨 IT STILL RETURNS 0 WHEN IT DOES NOT KNOW.  `confidence` is the winning score over the
    //  mean score; below kMinConfidence the material has no periodicity worth calling a tempo (a
    //  pad, a one-shot, a vocal phrase) and a number here would be a guess wearing an answer's
    //  clothes.  The whole point of the 0 is that an unlabelled one-shot never enters a stretcher.
    // ══════════════════════════════════════════════════════════════════════════════════════════
    //  THE ESTIMATOR, PRECISELY:
    //    · frame at 5 ms; onset strength = the POSITIVE log-energy rise (a fall is not an onset),
    //      so the measure is level-independent — a quiet loop and a loud one score the same
    //    · REFUSE unless the envelope carries at least kMinOnsets separated peaks. This is what
    //      keeps a pad out: a sustained note has ONE rise and then nothing, and a single spike
    //      autocorrelates beautifully with itself at every lag. The first cut of this scored a
    //      110 Hz pad at "100 BPM, confidence 65" for exactly that reason.
    //    · mean-remove, unit-norm, then r(lag) = the NORMALISED autocorrelation, so r lives in
    //      [-1, 1] and a confidence reads as a correlation instead of an unbounded ratio
    //    · weight r by a LOG-NORMAL TEMPO PRIOR centred on the session (Ellis's dynamic-programming
    //      beat tracker uses the same device). This is what settles the octave — a 90 BPM loop
    //      correlates just as well at 45 and 180, and "nearest the tempo you are working at" is
    //      both the honest tie-break and the one the user means. It replaces a post-hoc fold, which
    //      could only pick between octaves AFTER a wrong one had already won the argmax.
    //    · confidence is the UNWEIGHTED r at the winning lag: how periodic it actually is, with the
    //      prior's thumb off the scale.
    //
    //  🚨 IT STILL RETURNS 0 WHEN IT DOES NOT KNOW. Below kMinConfidence the material has no
    //  periodicity worth calling a tempo, and a number here would be a guess wearing an answer's
    //  clothes. The whole point of the 0 is that an unlabelled one-shot never enters a stretcher.
    // ══════════════════════════════════════════════════════════════════════════════════════════
    inline constexpr double kMinConfidence = 0.30;   // normalised autocorrelation at the beat
    inline constexpr int    kMinOnsets     = 4;      // fewer than four rises is not a groove
    inline constexpr double kPriorOctaves  = 0.90;   // the tempo prior's width, in octaves

    /** Beat tempo heard IN the audio, or 0. `mono` may be either channel or a sum — level does
     *  not matter. `confidenceOut` (optional) receives the normalised autocorrelation at the
     *  winning lag, in [-1, 1]. */
    inline double fromAudio (const float* mono, int numSamples, double sampleRate,
                             double referenceBpm, double* confidenceOut = nullptr) noexcept
    {
        if (confidenceOut != nullptr) *confidenceOut = 0.0;
        if (mono == nullptr || numSamples <= 0 || ! (sampleRate > 1000.0)) return 0.0;

        const int hop = (int) (sampleRate * 0.005);                  // 5 ms frames
        if (hop <= 0) return 0.0;
        const double fps = sampleRate / (double) hop;

        // The estimator needs room for at least three periods of the SLOWEST tempo it may report.
        const int minFrames = (int) (3.0 * 60.0 / kMinBpm * fps);
        int frames = numSamples / hop;
        if (frames < minFrames) return 0.0;

        const int kMaxFrames = 6000;                                 // 30 s at 200 fps
        const int F = (frames < kMaxFrames) ? frames : kMaxFrames;

        static thread_local double env[6000];                        // off the stack; message thread only
        double prevLog = 0.0;
        for (int f = 0; f < F; ++f)
        {
            double e = 0.0;
            const float* p = mono + (std::size_t) f * (std::size_t) hop;
            for (int i = 0; i < hop; ++i) e += (double) p[i] * (double) p[i];
            const double lg = std::log (e / hop + 1.0e-12);
            env[f] = (f == 0) ? 0.0 : ((lg - prevLog) > 0.0 ? (lg - prevLog) : 0.0);
            prevLog = lg;
        }

        // ── ⚠️ SMOOTH THE ENVELOPE, OR THE COMB CANNOT FIND ITS OWN HARMONICS ────────────────
        //  An onset envelope built this way is ONE FRAME WIDE, which makes the autocorrelation
        //  razor-sharp — and a beat lag is almost never a whole number of frames. At 117 BPM the
        //  beat is 102.56 frames, so the comb asks for 2·103 = 206 when the real two-beat lag is
        //  205.1, and MISSES: measured, r(205) = 0.902 while r(206) = 0.127. The comb then scored
        //  the true beat at 0.58 and handed the argmax to a 1.5-beat lag that happened to land on
        //  the grid — a 117 BPM loop read 77.9 and a 90 read 60, both exactly 2/3.
        //  A 5-frame (25 ms) triangular blur widens every peak enough that a harmonic survives
        //  the rounding, which is why every published onset-envelope tempo estimator smooths here.
        {
            static thread_local double sm[6000];
            const double k[5] = { 0.1, 0.2, 0.4, 0.2, 0.1 };
            for (int f = 0; f < F; ++f)
            {
                double a = 0.0;
                for (int d = -2; d <= 2; ++d)
                { const int j = f + d; if (j >= 0 && j < F) a += env[j] * k[d + 2]; }
                sm[f] = a;
            }
            for (int f = 0; f < F; ++f) env[f] = sm[f];
        }

        // ── enough separated rises to be a groove at all? ─────────────────────────────────────
        double mx = 0.0; for (int f = 0; f < F; ++f) if (env[f] > mx) mx = env[f];
        if (! (mx > 1.0e-6)) return 0.0;
        int onsets = 0, lastOn = -1000;
        const int refractory = (int) (fps * 0.05);                   // 50 ms — two hits, not one
        for (int f = 0; f < F; ++f)
            if (env[f] > 0.40 * mx && f - lastOn > refractory) { ++onsets; lastOn = f; }
        if (onsets < kMinOnsets) return 0.0;

        // ── mean-remove and unit-norm, so r(0) == 1 ──────────────────────────────────────────
        double mean = 0.0; for (int f = 0; f < F; ++f) mean += env[f];
        mean /= (double) F;
        double nrm = 0.0; for (int f = 0; f < F; ++f) { env[f] -= mean; nrm += env[f] * env[f]; }
        if (! (nrm > 1.0e-12)) return 0.0;
        const double inv = 1.0 / nrm;

        const int loL = (int) (60.0 / kMaxBpm * fps);
        const int hiL = (int) (60.0 / kMinBpm * fps);
        const double ref = (referenceBpm >= kMinBpm && referenceBpm <= kMaxBpm) ? referenceBpm : 120.0;

        // ⚠️ THE OVERLAP COMPENSATION IS CLAMPED, AND THAT CLAMP IS LOAD-BEARING. A lag keeps
        // only F-lag terms, so a raw sum favours SHORT lags and needs scaling back — but the
        // scale F/(F-lag) runs away as the lag approaches F. The first cut let the THIRD comb
        // harmonic reach 3·lag ≈ F, where the factor blew up and handed the argmax to whatever
        // lag put a harmonic near the end of the file: a 117 BPM loop read 77.9 (exactly 2/3 of
        // it) and a 90 read 60. Cap the factor, and never comb a harmonic past the half-length.
        auto r = [&] (int lag) -> double
        {
            if (lag <= 0 || lag >= F / 2) return 0.0;
            double c = 0.0;
            for (int i = 0; i + lag < F; ++i) c += env[i] * env[i + lag];
            double comp = (double) F / (double) (F - lag);
            if (comp > 2.0) comp = 2.0;
            return c * inv * comp;
        };

        double bestW = -1.0e9, bestR = 0.0; int bestLag = 0;
        for (int lag = loL; lag <= hiL && lag < F / 3; ++lag)
        {
            const double bpm = 60.0 * fps / (double) lag;
            const double rr  = r (lag) + 0.5 * r (2 * lag) + 0.25 * r (3 * lag);
            const double lo  = std::log2 (bpm / ref) / kPriorOctaves;
            const double w   = rr * std::exp (-0.5 * lo * lo);       // the tempo prior
            if (w > bestW) { bestW = w; bestLag = lag; bestR = r (lag); }
        }
        if (bestLag <= 0) return 0.0;
        if (confidenceOut != nullptr) *confidenceOut = bestR;
        if (bestR < kMinConfidence) return 0.0;

        const double bpm = 60.0 * fps / (double) bestLag;
        return (bpm >= kMinBpm && bpm <= kMaxBpm) ? bpm : 0.0;
    }

    /** The reading the plugin uses: the name first, the length second, 0 when neither speaks. */
    inline double detect (const char* name, double lengthSeconds, double referenceBpm) noexcept
    {
        const double named = fromName (name);
        if (named > 0.0) return named;
        return fromLength (lengthSeconds, referenceBpm);
    }

    /** tp58 — the full reading, with the audio in hand.
     *
     *  ORDER, AND WHY:
     *    1. THE NAME. A number a human typed beats anything inferred.  Unchanged.
     *    2. THE AUDIO, SNAPPED TO THE LENGTH.  The ear finds the beat on material a bar count
     *       cannot explain; the bar count, when it agrees, is EXACT where the estimator is only
     *       within a fraction of a BPM.  So when a whole-bar candidate lands within `kSnap` of
     *       what we heard, that candidate is the answer — best of both, and it is why a clean
     *       4-bar export locks dead on rather than to 116.87.
     *    3. THE LENGTH ALONE, when the audio has nothing periodic to say.
     *  and 0 when none of the three does. */
    inline constexpr double kSnapFraction = 0.02;      // 2 % — a bar count this close IS the tempo

    inline double detectFull (const char* name, double lengthSeconds, double referenceBpm,
                              const float* mono, int numSamples, double sampleRate) noexcept
    {
        const double named = fromName (name);
        if (named > 0.0) return named;

        const double byLen = fromLength (lengthSeconds, referenceBpm);
        double conf = 0.0;
        const double heard = fromAudio (mono, numSamples, sampleRate, referenceBpm, &conf);
        if (heard > 0.0)
        {
            if (byLen > 0.0 && std::fabs (byLen - heard) <= heard * kSnapFraction) return byLen;
            return heard;
        }
        return byLen;
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
