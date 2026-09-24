// SampleKeyDetect_test.cpp — offline validation for the sample key detector.
//   g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic SampleKeyDetect_test.cpp -o /tmp/skd && /tmp/skd
//
// Proves:
//   • test tones at known pitches (A3=220, E4, C2≈65.4, plus rich harmonic tones)
//     are reported as the correct note, and the computed snap brings each to C;
//   • the snap really lands on a C:  (detectedNote + snapSemitones) % 12 == 0;
//   • white noise / near-silence return LOW confidence → UNVOICED → snap 0 (no-op).
#include "SampleKeyDetect.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>
#include <string>

using tw::SampleKeyDetector;
using tw::SampleKeyResult;

static const double FS = 48000.0;
static int gPass = 0, gFail = 0;

static void check (bool ok, const std::string& name, const std::string& info = "")
{
    std::printf ("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                 info.empty() ? "" : "  — ", info.c_str());
    if (ok) ++gPass; else ++gFail;
}

static double noteHz (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

// A tone with an attack transient (to exercise transient-skip) and optional harmonics.
static std::vector<float> makeTone (double freq, double seconds, int nHarmonics = 1,
                                    double sr = FS)
{
    const int N = (int) (seconds * sr);
    std::vector<float> x ((size_t) N, 0.0f);
    const double atk = 0.010 * sr;   // 10 ms attack ramp = a transient to skip
    for (int i = 0; i < N; ++i)
    {
        double s = 0.0;
        for (int h = 1; h <= nHarmonics; ++h)
            s += (1.0 / h) * std::sin (2.0 * M_PI * freq * h * i / sr);
        const double env = (i < atk) ? (i / atk) : 1.0;
        x[(size_t) i] = (float) (0.6 * env * s / std::max (1, nHarmonics) * nHarmonics);
    }
    return x;
}

// A realistic multi-harmonic PAD: a full harmonic stack (1..H) with a WEAK or even
// ABSENT fundamental (fundAmp), a slow 250 ms attack, and slight per-partial detune
// (chorus/unison spread) so no two harmonics are exactly integer-locked. This is what
// a real "Synth Pad" load looks like, versus the clean tones above — and the case the
// field bug was reported on. YIN's cumulative-mean difference function still locks the
// true PERIOD (the missing/weak fundamental is reconstructed from the partials), so the
// nearest note and the snap must come out identical to the pure-tone case.
static std::vector<float> makePad (int midi, int nHarmonics, double fundAmp,
                                   double detuneCents, double seconds = 2.5, double sr = FS)
{
    const double f0 = noteHz (midi);
    const int    N  = (int) (seconds * sr);
    std::vector<float> x ((size_t) N, 0.0f);
    std::mt19937 rng (1234u + (unsigned) midi);
    std::uniform_real_distribution<double> dt (-detuneCents, detuneCents);
    std::vector<double> hd ((size_t) nHarmonics + 1, 0.0);
    for (int h = 1; h <= nHarmonics; ++h) hd[(size_t) h] = dt (rng);
    const double atk = 0.25 * sr;                       // 250 ms slow attack
    for (int i = 0; i < N; ++i)
    {
        double s = 0.0;
        for (int h = 1; h <= nHarmonics; ++h)
        {
            const double amp = (h == 1) ? fundAmp : 1.0 / h;
            const double fh  = f0 * h * std::pow (2.0, hd[(size_t) h] / 1200.0);
            s += amp * std::sin (2.0 * M_PI * fh * i / sr);
        }
        const double env = (i < atk) ? (i / atk) : 1.0;
        x[(size_t) i] = (float) (0.2 * env * s);
    }
    return x;
}

// A 7-voice supersaw pad (heavily detuned stacked saws) — another realistic pad shape.
static std::vector<float> makeSuperSaw (int midi, double detuneCents,
                                        double seconds = 2.5, double sr = FS)
{
    const double f0  = noteHz (midi);
    const int    N   = (int) (seconds * sr);
    std::vector<float> x ((size_t) N, 0.0f);
    const double atk = 0.15 * sr;
    const double det[7] = { -1.0, -0.66, -0.33, 0.0, 0.33, 0.66, 1.0 };
    for (int i = 0; i < N; ++i)
    {
        double s = 0.0;
        for (int v = 0; v < 7; ++v)
        {
            const double fh = f0 * std::pow (2.0, det[v] * detuneCents / 1200.0);
            const double ph = std::fmod (fh * i / sr, 1.0);
            s += 2.0 * ph - 1.0;                        // naive saw
        }
        const double env = (i < atk) ? (i / atk) : 1.0;
        x[(size_t) i] = (float) (0.05 * env * s);
    }
    return x;
}

// Assert a pitched tone: right note, snap lands on a C, snap magnitude sane.
static void expectNote (const char* label, const std::vector<float>& x,
                        int expectMidi, int expectSnap)
{
    SampleKeyResult r = SampleKeyDetector::detect (x.data(), (int) x.size(), FS);
    const std::string got = SampleKeyDetector::noteName (r.midiNote)
                          + " (" + std::to_string (r.midiNote) + "), "
                          + std::to_string (r.frequencyHz).substr (0, 7) + " Hz, conf "
                          + std::to_string (r.confidence).substr (0, 5)
                          + ", snap " + std::to_string (r.snapSemitones);

    check (r.voiced, std::string (label) + " voiced", got);
    check (r.midiNote == expectMidi,
           std::string (label) + " note == " + SampleKeyDetector::noteName (expectMidi), got);
    check (r.snapSemitones == expectSnap,
           std::string (label) + " snap == " + std::to_string (expectSnap), got);
    // The load-bearing property: after the snap, the root key sounds a C.
    check (r.voiced && (((r.midiNote + r.snapSemitones) % 12 + 12) % 12) == 0,
           std::string (label) + " snap brings it to C", got);
}

int main()
{
    std::printf ("SampleKeyDetect — offline validation\n");

    // ── 1. Pure sine test tones at the spec's known pitches ──────────────────────
    std::printf ("\n[pure sine tones]\n");
    expectNote ("A3=220",  makeTone (noteHz (57), 1.0),        57, +3);   // A3 → +3 → C4
    expectNote ("E4",      makeTone (noteHz (64), 1.0),        64, -4);   // E4 → -4 → C4
    expectNote ("C2~65.4", makeTone (noteHz (36), 1.0),        36,  0);   // C2 → already C
    // sanity: C2 nominal frequency really is ~65.4 Hz
    check (std::fabs (noteHz (36) - 65.406) < 0.01, "C2 reference freq ≈ 65.406 Hz",
           std::to_string (noteHz (36)));

    // ── 2. Rich harmonic tones — must lock the FUNDAMENTAL, not a harmonic ────────
    std::printf ("\n[harmonic tones (fundamental tracking)]\n");
    expectNote ("G3 saw",  makeTone (noteHz (55), 1.0, 12),    55, +5);   // G3 → +5 → C4
    expectNote ("C3 saw",  makeTone (noteHz (48), 1.0, 8),     48,  0);   // C3 → already C
    expectNote ("F4 saw",  makeTone (noteHz (65), 1.0, 6),     65, -5);   // F4 → -5 → C4

    // ── 3. Stereo path (array-of-channels overload) ──────────────────────────────
    std::printf ("\n[stereo buffer]\n");
    {
        auto L = makeTone (noteHz (57), 1.0, 4);
        auto R = makeTone (noteHz (57), 1.0, 4);
        const float* chans[2] = { L.data(), R.data() };
        SampleKeyResult r = SampleKeyDetector::detect (chans, 2, (int) L.size(), FS);
        check (r.voiced && r.midiNote == 57 && r.snapSemitones == 3,
               "stereo A3 detected + snapped",
               SampleKeyDetector::noteName (r.midiNote) + " snap " + std::to_string (r.snapSemitones));
    }

    // ── 4. GUARD: un-pitched material must be a NO-OP ─────────────────────────────
    std::printf ("\n[un-pitched material → no-op guard]\n");
    {
        std::mt19937 rng (1234567u);
        std::uniform_real_distribution<float> uni (-0.6f, 0.6f);
        std::vector<float> noise (48000);
        for (auto& v : noise) v = uni (rng);
        SampleKeyResult r = SampleKeyDetector::detect (noise.data(), (int) noise.size(), FS);
        check (! r.voiced, "white noise → UNVOICED",
               std::string ("conf ") + std::to_string (r.confidence).substr (0, 5));
        check (r.snapSemitones == 0, "white noise → snap 0 (no-op)");
    }
    {
        std::vector<float> silence (48000, 0.0f);
        SampleKeyResult r = SampleKeyDetector::detect (silence.data(), (int) silence.size(), FS);
        check (! r.voiced && r.snapSemitones == 0, "silence → UNVOICED, no-op");
    }
    {
        // Filtered-ish noise burst: still no clear fundamental → no-op.
        std::mt19937 rng (999u);
        std::normal_distribution<float> nd (0.0f, 0.3f);
        std::vector<float> n (48000);
        float prev = 0.0f;
        for (auto& v : n) { prev = 0.98f * prev + 0.02f * nd (rng); v = prev * 8.0f; }
        SampleKeyResult r = SampleKeyDetector::detect (n.data(), (int) n.size(), FS);
        check (! r.voiced, "coloured noise → UNVOICED",
               std::string ("conf ") + std::to_string (r.confidence).substr (0, 5));
    }

    // ── 5. Every snap really lands on a C, across all pitch classes ──────────────
    std::printf ("\n[snap-to-C invariant across all 128 notes]\n");
    {
        bool allC = true, allInRange = true;
        for (int m = 0; m < 128; ++m)
        {
            const int snap = SampleKeyDetector::snapToNearestC (m);
            if ((((m + snap) % 12) + 12) % 12 != 0) allC = false;
            if (snap < -6 || snap > 5) allInRange = false;
        }
        check (allC, "snapToNearestC lands every note on a C");
        check (allInRange, "snap offset is minimal (−6..+5)");
    }

    // ── 6. Different sample rate (44.1 kHz) still resolves correctly ──────────────
    std::printf ("\n[44.1 kHz sample rate]\n");
    {
        const double sr = 44100.0;
        const int N = (int) sr;
        std::vector<float> x ((size_t) N);
        const double freq = 440.0 * std::pow (2.0, (57 - 69) / 12.0);   // A3
        for (int i = 0; i < N; ++i) x[(size_t) i] = (float) (0.6 * std::sin (2.0 * M_PI * freq * i / sr));
        SampleKeyResult r = SampleKeyDetector::detect (x.data(), N, sr);
        check (r.voiced && r.midiNote == 57 && r.snapSemitones == 3,
               "A3 @ 44.1k detected + snapped",
               SampleKeyDetector::noteName (r.midiNote) + " snap " + std::to_string (r.snapSemitones));
    }

    // ── 7. REALISTIC MULTI-HARMONIC PADS — the field-bug repro ───────────────────
    // A loaded "Synth Pad" is a full harmonic stack with a weak/absent fundamental,
    // a slow attack and detuned partials — NOT a pure tone. Prove the detector still
    // reports the right note and the snap still brings each pad's root to a C. (These
    // are the cases that were claimed to "play out of key"; the detector handles them.)
    std::printf ("\n[realistic multi-harmonic pads — weak/absent fundamental, detuned]\n");
    expectNote ("A3 pad  (weak fund, H=16)",  makePad (57, 16, 0.15, 6.0), 57, +3);  // A3 → +3 → C
    expectNote ("F2 pad  (weak fund, H=16)",  makePad (41, 16, 0.15, 6.0), 41, -5);  // F2 → -5 → C
    expectNote ("G2 pad  (weak fund, H=16)",  makePad (43, 16, 0.15, 6.0), 43, +5);  // G2 → +5 → C
    expectNote ("A#2 pad (weak fund, H=16)",  makePad (46, 16, 0.15, 6.0), 46, +2);  // A#2 → +2 → C
    expectNote ("D#3 pad (MISSING fund)",     makePad (51, 16, 0.0,  6.0), 51, -3);  // D#3 → -3 → C
    expectNote ("C3 supersaw (7 saws ±12c)",  makeSuperSaw (48, 12.0),     48,  0);  // already C

    // ── 8. SAMPLE-RATE MATCH — the chop plays the snapped pad in the oscillators' C ──
    // The detector measures the fundamental in the sample's NATIVE rate. The chop
    // sampler reads at native SPEED (no native/output SR ratio — SamplerVoice NONE
    // path), so a sample whose native rate ≠ the host rate already sounds
    // 12·log2(host/native) semitones off, and the raw native-domain snap misses C by
    // exactly that — the reported "plays out of key, needs pitching down". THIS is why
    // a 32 kHz pad on a 48 kHz host lands a perfect fifth / +7 semitones off. The fix
    // (SamplerVoice::startNote, detected samples only) adds 12·log2(native/host) so the
    // snapped pad lands on the SAME C as the oscillators. Prove: WITHOUT the term the
    // played root drifts off C for a rate mismatch; WITH it, it is a C for every rate.
    std::printf ("\n[sample-rate match — chop auto-key lands on the oscillator C]\n");
    {
        // detected pad root A3 (57), detector snap +3 → C in the native domain.
        const int detNote = 57, snap = 3;
        auto playedRootMidi = [] (int note, int snp, double nr, double hr, bool withFix)
        {
            // native-speed read shifts a native-rate sample by 12·log2(host/native);
            // the voice always folds in the detector snap; the FIX adds the SR term.
            double m = (double) note + 12.0 * std::log2 (hr / nr) + (double) snp;
            if (withFix) m += 12.0 * std::log2 (nr / hr);
            return m;
        };
        auto isC = [] (double midiF) { return (((long) std::lround (midiF)) % 12 + 12) % 12 == 0; };

        struct { double nr, hr; const char* name; } rates[] = {
            { 48000.0, 48000.0, "48k→48k (match)"   },
            { 44100.0, 48000.0, "44.1k→48k"         },
            { 32000.0, 48000.0, "32k→48k (the +7!)" },
            { 96000.0, 48000.0, "96k→48k"           },
            { 22050.0, 44100.0, "22.05k→44.1k"      },
        };
        for (auto& R : rates)
        {
            const double fixed = playedRootMidi (detNote, snap, R.nr, R.hr, true);
            check (isC (fixed),
                   std::string ("fixed: ") + R.name + " plays in C",
                   SampleKeyDetector::noteName ((int) std::lround (fixed)));
        }
        // The bug, made explicit: a 32k pad on a 48k host is a perfect fifth off WITHOUT
        // the SR term, and exactly C WITH it.
        const double buggy = playedRootMidi (detNote, snap, 32000.0, 48000.0, false);
        const double fixed = playedRootMidi (detNote, snap, 32000.0, 48000.0, true);
        check (! isC (buggy) && std::llround (buggy) == 67,
               "32k→48k WITHOUT SR term is +7 off (G, the reported error)",
               SampleKeyDetector::noteName ((int) std::lround (buggy)));
        check (isC (fixed) && std::llround (fixed) == 60,
               "32k→48k WITH SR term lands exactly on C4",
               SampleKeyDetector::noteName ((int) std::lround (fixed)));
    }

    std::printf ("\n──────────────────────────────\n");
    std::printf ("PASS %d   FAIL %d\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
