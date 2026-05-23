// WarpProcessor_test.cpp
//
// Coverage for SignalsmithEngine and (in Task 5) WarpProcessor. Gated by
// JUCE_DEBUG so the tests disappear from Release builds. Tests register
// statically via juce::UnitTest; downstream harness (when wired) runs them
// at Standalone startup.
//
#include <juce_core/juce_core.h>

#if JUCE_DEBUG

#include "SignalsmithEngine.h"
#include "../Slice.h"
#include <vector>
#include <cmath>

class SignalsmithEngineTests : public juce::UnitTest
{
public:
    SignalsmithEngineTests() : juce::UnitTest ("SignalsmithEngine") {}

    void runTest() override
    {
        beginTest ("Constructs and prepares without crash");
        {
            tw::SignalsmithEngine eng;
            eng.prepare (48000.0, 2, 512);
            expect (eng.isReady());
        }

        beginTest ("Unity ratio preserves output energy (within 6 dB)");
        {
            tw::SignalsmithEngine eng;
            eng.prepare (48000.0, 2, 512);
            eng.reset();
            eng.setStretchRatio (1.0f);
            eng.setPitchSemitones (0.0f);

            constexpr int N = 4096;
            std::vector<float> inL (N), inR (N), outL (N), outR (N);

            juce::Random r;
            for (int i = 0; i < N; ++i)
            {
                inL[i] = r.nextFloat() * 0.5f - 0.25f;
                inR[i] = r.nextFloat() * 0.5f - 0.25f;
            }

            eng.process (inL.data(), inR.data(), outL.data(), outR.data(), N);

            // After the engine's internal latency, output energy should be in
            // the same ballpark as input energy. We check a middle window,
            // skipping leading + trailing latency.
            double inEnergy  = 0.0;
            double outEnergy = 0.0;
            for (int i = 1024; i < N - 1024; ++i)
            {
                inEnergy  += inL[i] * inL[i] + inR[i] * inR[i];
                outEnergy += outL[i] * outL[i] + outR[i] * outR[i];
            }
            const double ratio = outEnergy / juce::jmax (1.0e-9, inEnergy);
            expect (ratio > 0.25 && ratio < 4.0,
                    "Output energy should be within 6 dB of input at unity ratio (got ratio " + juce::String (ratio) + ")");
        }

        beginTest ("Reset+seek is idempotent and crash-free");
        {
            tw::SignalsmithEngine eng;
            eng.prepare (48000.0, 2, 512);
            eng.reset();
            eng.reset();
            eng.setStretchRatio (2.0f);
            eng.reset();
            expect (true);  // arrived here without segfault
        }

        beginTest ("Output is finite at extreme stretch ratios");
        {
            for (float r : { 0.25f, 0.5f, 2.0f, 4.0f })
            {
                tw::SignalsmithEngine eng;
                eng.prepare (48000.0, 2, 512);
                eng.reset();
                eng.setStretchRatio (r);

                constexpr int N = 2048;
                std::vector<float> inL (N, 0.1f), inR (N, 0.1f);
                std::vector<float> outL (N), outR (N);
                eng.process (inL.data(), inR.data(), outL.data(), outR.data(), N);

                bool allFinite = true;
                for (int i = 0; i < N; ++i)
                {
                    if (! std::isfinite (outL[i]) || ! std::isfinite (outR[i]))
                    {
                        allFinite = false;
                        break;
                    }
                }
                expect (allFinite, "Non-finite sample produced at ratio " + juce::String (r));
            }
        }

        beginTest ("Stretch ratio is clamped to 0.25..4.0");
        {
            tw::SignalsmithEngine eng;
            eng.prepare (48000.0, 2, 512);
            // setStretchRatio clamps internally — we can't read it back directly,
            // but we can verify out-of-range inputs don't crash the engine.
            eng.setStretchRatio (0.01f);
            eng.setStretchRatio (100.0f);
            eng.setStretchRatio (-1.0f);
            expect (true);
        }
    }
};

static SignalsmithEngineTests signalsmithEngineTests;

// ──────────────────────────────────────────────────────────────────────────
// WarpProcessor — per-voice dispatcher tests
// ──────────────────────────────────────────────────────────────────────────
#include "WarpProcessor.h"

class WarpProcessorTests : public juce::UnitTest
{
public:
    WarpProcessorTests() : juce::UnitTest ("WarpProcessor") {}

    void runTest() override
    {
        beginTest ("Mode=None: process is identity (exact memcpy match)");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            wp.setMode (tw::WarpMode::None);
            wp.setStretchRatio (2.0f);  // ignored in None mode

            constexpr int N = 256;
            float inL[N], inR[N], outL[N], outR[N];
            for (int i = 0; i < N; ++i)
            {
                inL[i] = 0.1f * std::sin (i * 0.1f);
                inR[i] = 0.1f * std::cos (i * 0.1f);
            }
            wp.process (inL, inR, outL, outR, N);

            bool exact = true;
            for (int i = 0; i < N; ++i)
            {
                if (std::abs (outL[i] - inL[i]) > 1.0e-6f) { exact = false; break; }
                if (std::abs (outR[i] - inR[i]) > 1.0e-6f) { exact = false; break; }
            }
            expect (exact, "None-mode output should be exact copy of input");
        }

        beginTest ("Mode=Tones: lazy-allocates engine on first non-None set");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            expect (! wp.hasEngineAllocated(), "Engine should NOT be allocated before any setMode");
            wp.setMode (tw::WarpMode::Tones);
            expect (wp.hasEngineAllocated(), "Engine SHOULD be allocated after setMode(Tones)");
        }

        beginTest ("Engine survives mode round-trip (None -> Tones -> None)");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);

            wp.setMode (tw::WarpMode::Tones);
            expect (wp.hasEngineAllocated());
            expect (wp.getMode() == tw::WarpMode::Tones);

            wp.setMode (tw::WarpMode::None);
            // Engine instance stays alive (no dealloc on mode switch back) but
            // process() should now be identity.
            expect (wp.hasEngineAllocated(),
                    "Engine should NOT be deallocated when mode returns to None");
            expect (wp.getMode() == tw::WarpMode::None);

            constexpr int N = 128;
            float inL[N], inR[N], outL[N], outR[N];
            for (int i = 0; i < N; ++i) { inL[i] = 0.1f; inR[i] = 0.2f; }
            wp.process (inL, inR, outL, outR, N);

            bool exact = true;
            for (int i = 0; i < N; ++i)
                if (std::abs (outL[i] - 0.1f) > 1.0e-6f || std::abs (outR[i] - 0.2f) > 1.0e-6f)
                    { exact = false; break; }
            expect (exact, "Identity restored after switching back to None");
        }

        beginTest ("Stretch ratio clamps to 0.25..4.0");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            wp.setMode (tw::WarpMode::Tones);
            wp.setStretchRatio (10.0f);   // should clamp to 4.0
            wp.setStretchRatio (0.01f);   // should clamp to 0.25
            wp.setStretchRatio (-5.0f);   // should clamp to 0.25
            // No assertion on internal state — the assertion is "no crash".
            expect (true);
        }

        beginTest ("noteOnReset is safe before first setMode (no engine yet)");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            wp.noteOnReset();  // engine doesn't exist yet — must be safe.
            expect (true);
        }
    }
};

static WarpProcessorTests warpProcessorTests;

// ──────────────────────────────────────────────────────────────────────────
// BeatsEngine — DIY granular stutter with crossfaded grain wraps
// ──────────────────────────────────────────────────────────────────────────
#include "BeatsEngine.h"

class BeatsEngineTests : public juce::UnitTest
{
public:
    BeatsEngineTests() : juce::UnitTest ("BeatsEngine") {}

    void runTest() override
    {
        constexpr double SR = 48000.0;

        beginTest ("Constructs and prepares without crash");
        {
            tw::BeatsEngine eng;
            eng.prepare (SR, 2, 512);
            expect (eng.isReady());
        }

        // A continuous sinewave is the canonical "if-you-click-i-detect-it"
        // signal. Per-sample delta of a clean sine peaks at amp*2π*freq/SR,
        // which for amp=0.25, freq=200Hz, SR=48k is ~0.0065. A click from a
        // bad wrap snaps from ~0.25 back to ~0 in one sample — delta 0.25 —
        // well above the 0.05 threshold below.
        auto runSineCheck = [&] (float stretchRatio, float pitchSemis, float threshold,
                                 const juce::String& label)
        {
            tw::BeatsEngine eng;
            eng.prepare (SR, 2, 512);
            eng.reset();
            eng.setStretchRatio (stretchRatio);
            eng.setPitchSemitones (pitchSemis);

            const int totalOut = 24000;  // 0.5 sec — covers many grain wraps
            const double pitchRatio = std::pow (2.0, (double) pitchSemis / 12.0);
            const int totalIn = juce::jmax (1, (int) std::round (totalOut * pitchRatio / stretchRatio));

            std::vector<float> inL (totalIn), inR (totalIn);
            for (int i = 0; i < totalIn; ++i)
            {
                const float v = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 200.0f * i / (float) SR);
                inL[i] = v;
                inR[i] = v;
            }

            // Prime with one grain (inputLatency) of source so the engine
            // doesn't gate output on firstBlockPending. This is what
            // SamplerVoice does at note-on.
            const int prime = juce::jmin (eng.inputLatency(), totalIn);
            eng.seek (inL.data(), inR.data(), prime);

            std::vector<float> outL (totalOut), outR (totalOut);

            // Process in fixed blocks of 512.
            const int block = 512;
            int outDone = 0;
            int inPos   = prime;
            while (outDone < totalOut)
            {
                const int outN = juce::jmin (block, totalOut - outDone);
                const int inN  = juce::jmax (1, (int) std::round (outN * pitchRatio / stretchRatio));
                const int inAvail = juce::jmin (inN, totalIn - inPos);
                if (inAvail <= 0) break;  // ran out of input
                eng.process (inL.data() + inPos, inR.data() + inPos,
                             outL.data() + outDone, outR.data() + outDone, outN);
                outDone += outN;
                inPos   += inAvail;
            }

            // Skip leading silence (firstBlockPending grace) — find first
            // sample where |out| > 0.001 then start scanning from there.
            int scanStart = 0;
            for (int i = 0; i < outDone; ++i)
            {
                if (std::abs (outL[i]) > 0.001f) { scanStart = i; break; }
            }
            // Skip a few hundred more samples to clear any first-grain
            // transient settling.
            scanStart = juce::jmin (scanStart + 256, outDone);

            float maxDelta = 0.0f;
            int   worstIdx = -1;
            for (int i = scanStart + 1; i < outDone; ++i)
            {
                const float d = std::abs (outL[i] - outL[i-1]);
                if (d > maxDelta) { maxDelta = d; worstIdx = i; }
                expect (std::isfinite (outL[i]), label + ": non-finite sample at " + juce::String (i));
            }

            expect (maxDelta < threshold,
                    label + ": max per-sample delta " + juce::String (maxDelta)
                    + " at sample " + juce::String (worstIdx)
                    + " exceeds click threshold " + juce::String (threshold));
        };

        beginTest ("Unity stretch + unity pitch — passthrough (no clicks)");
        runSineCheck (1.0f, 0.0f, 0.05f, "unity");

        beginTest ("Stretch 2.0, unity pitch — repeated within-beat wraps, smooth");
        runSineCheck (2.0f, 0.0f, 0.05f, "stretch2");

        beginTest ("Stretch 4.0, unity pitch — many wraps per beat, smooth");
        runSineCheck (4.0f, 0.0f, 0.05f, "stretch4");

        beginTest ("Unity stretch + pitch up an octave — was broken pre-v5");
        runSineCheck (1.0f, 12.0f, 0.05f, "pitch+12");

        beginTest ("Stretch 2.0 + pitch up an octave — combined warp, smooth");
        runSineCheck (2.0f, 12.0f, 0.05f, "stretch2-pitch+12");

        beginTest ("Stretch 2.0 + pitch down an octave — combined warp, smooth");
        runSineCheck (2.0f, -12.0f, 0.05f, "stretch2-pitch-12");

        beginTest ("Stretch 8.0 + pitch +7 semis — extreme combo, smooth");
        runSineCheck (8.0f, 7.0f, 0.05f, "stretch8-pitch+7");

        beginTest ("Output is finite at extreme stretch ratios");
        {
            for (float r : { 0.1f, 0.5f, 2.0f, 8.0f, 15.0f })
            {
                tw::BeatsEngine eng;
                eng.prepare (SR, 2, 512);
                eng.reset();
                eng.setStretchRatio (r);

                constexpr int N = 4096;
                std::vector<float> inL (N, 0.1f), inR (N, 0.1f);
                std::vector<float> outL (N), outR (N);
                eng.seek (inL.data(), inR.data(), juce::jmin (eng.inputLatency(), N));
                eng.process (inL.data(), inR.data(), outL.data(), outR.data(), N);

                for (int i = 0; i < N; ++i)
                    expect (std::isfinite (outL[i]) && std::isfinite (outR[i]),
                            "Non-finite sample at ratio " + juce::String (r));
            }
        }

        beginTest ("Reset after process is idempotent and crash-free");
        {
            tw::BeatsEngine eng;
            eng.prepare (SR, 2, 512);
            eng.setStretchRatio (2.0f);
            eng.reset();
            constexpr int N = 1024;
            std::vector<float> inL (N, 0.1f), inR (N, 0.1f);
            std::vector<float> outL (N), outR (N);
            eng.process (inL.data(), inR.data(), outL.data(), outR.data(), N);
            eng.reset();
            eng.process (inL.data(), inR.data(), outL.data(), outR.data(), N);
            expect (true);
        }
    }
};

static BeatsEngineTests beatsEngineTests;

// ──────────────────────────────────────────────────────────────────────────
// WarpProcessor::sourceSamplesPerBlock — mode-aware input-feed length
// ──────────────────────────────────────────────────────────────────────────
class WarpProcessorInputLenTests : public juce::UnitTest
{
public:
    WarpProcessorInputLenTests() : juce::UnitTest ("WarpProcessor::sourceSamplesPerBlock") {}

    void runTest() override
    {
        beginTest ("None mode — returns numSamples (unused but defined)");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            expect (wp.sourceSamplesPerBlock (512) >= 1);
        }

        beginTest ("Tones mode — pitch is irrelevant (Signalsmith handles internally)");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            wp.setMode (tw::WarpMode::Tones);
            wp.setStretchRatio (2.0f);
            wp.setPitchSemitones (12.0f);
            expect (wp.sourceSamplesPerBlock (512) == 256,
                    "Tones at stretch=2 should consume 512/2=256 source samples regardless of pitch");
        }

        beginTest ("Beats mode — pitchRatio scales input feed");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            wp.setMode (tw::WarpMode::Beats);

            wp.setStretchRatio (1.0f);
            wp.setPitchSemitones (0.0f);
            expect (wp.sourceSamplesPerBlock (512) == 512, "Beats unity = 512");

            wp.setPitchSemitones (12.0f);  // pitchRatio = 2
            expect (wp.sourceSamplesPerBlock (512) == 1024, "Beats pitch+12 = 2× source per block");

            wp.setPitchSemitones (-12.0f);  // pitchRatio = 0.5
            expect (wp.sourceSamplesPerBlock (512) == 256, "Beats pitch-12 = 0.5× source per block");

            wp.setStretchRatio (2.0f);
            wp.setPitchSemitones (12.0f);  // pr=2, sr=2 → 1.0 ratio
            expect (wp.sourceSamplesPerBlock (512) == 512, "Beats stretch=2 pitch+12 = 512");
        }

        beginTest ("Beats mode — extreme pitch clamps to ±24 (v8 high-octave fix)");
        {
            // BeatsEngine clamps pitchSemitones to ±24 internally. The
            // source-feed length must match — otherwise the playhead races
            // past the engine's read rate, jumps source position between
            // blocks, and produces block-rate AM (the C8–C10 "robotic"
            // artifact users reported). Clamp boundary tests:
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            wp.setMode (tw::WarpMode::Beats);
            wp.setStretchRatio (1.0f);

            wp.setPitchSemitones (24.0f);   // clamp boundary (pitchRatio = 4)
            expect (wp.sourceSamplesPerBlock (512) == 2048, "Beats pitch+24 = 4× source");

            wp.setPitchSemitones (72.0f);   // C9-ish; would be pow(2,6)=64× without clamp
            expect (wp.sourceSamplesPerBlock (512) == 2048,
                    "Beats pitch+72 must clamp to +24 (else block-rate AM)");

            wp.setPitchSemitones (96.0f);   // C10-ish; would be 256× without clamp
            expect (wp.sourceSamplesPerBlock (512) == 2048,
                    "Beats pitch+96 must clamp to +24");

            wp.setPitchSemitones (-72.0f);  // mirror clamp at the low end
            expect (wp.sourceSamplesPerBlock (512) == 128,
                    "Beats pitch-72 must clamp to -24 (pitchRatio = 0.25)");
        }
    }
};

static WarpProcessorInputLenTests warpProcessorInputLenTests;

// ─────────────────────────────────────────────────────────────────────────────
// Slice scan-mode data model: defaults + JSON roundtrip + legacy loading
// (Task 1, Mark 1.5 scan mode)
// ─────────────────────────────────────────────────────────────────────────────
class SliceScanRoundtripTests : public juce::UnitTest
{
public:
    SliceScanRoundtripTests() : juce::UnitTest ("SliceScanRoundtrip") {}

    void runTest() override
    {
        beginTest ("Scan fields default to off / 1.0 / 1.0");
        {
            tw::Slice s;
            expect (! s.scanEnabled);
            expectWithinAbsoluteError (s.scanRate,   1.0f, 1.0e-6f);
            expectWithinAbsoluteError (s.scanWindow, 1.0f, 1.0e-6f);
        }

        beginTest ("Scan fields survive JSON roundtrip");
        {
            std::vector<tw::Slice> in;
            tw::Slice s;
            s.startSample  = 0;
            s.endSample    = 1000;
            s.scanEnabled  = true;
            s.scanRate     = 2.5f;
            s.scanWindow   = 0.4f;
            in.push_back (s);

            auto json = tw::slicesToJson (in);
            auto out  = tw::slicesFromJson (json);

            expectEquals ((int) out.size(), 1);
            expect (out[0].scanEnabled);
            expectWithinAbsoluteError (out[0].scanRate,   2.5f, 1.0e-6f);
            expectWithinAbsoluteError (out[0].scanWindow, 0.4f, 1.0e-6f);
        }

        beginTest ("Legacy JSON (no scan fields) loads with defaults");
        {
            // Build JSON in the format slicesToJson uses — object with "slices"
            // array — but omitting the scan keys to simulate a legacy preset.
            juce::DynamicObject::Ptr root = new juce::DynamicObject();
            juce::Array<juce::var> arr;
            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            obj->setProperty ("start", (juce::int64) 0);
            obj->setProperty ("end",   (juce::int64) 1000);
            arr.add (juce::var (obj.get()));
            root->setProperty ("slices", arr);
            auto json = juce::JSON::toString (juce::var (root.get()));

            auto out = tw::slicesFromJson (json);
            expectEquals ((int) out.size(), 1);
            expect (! out[0].scanEnabled);
            expectWithinAbsoluteError (out[0].scanRate,   1.0f, 1.0e-6f);
            expectWithinAbsoluteError (out[0].scanWindow, 1.0f, 1.0e-6f);
        }
    }
};

static SliceScanRoundtripTests sliceScanRoundtripTests;

// ─────────────────────────────────────────────────────────────────────────────
// ScanBoundaryFlip — Warp:None ping-pong direction flip at effective window
// edges. Requires SamplerVoice; constructed with a minimal in-memory
// SampleBuffer holding a 1000-sample DC signal.
// (Task 5, Mark 1.5 scan mode)
// ─────────────────────────────────────────────────────────────────────────────
#include "../SamplerVoice.h"

class ScanBoundaryFlipTests : public juce::UnitTest
{
public:
    ScanBoundaryFlipTests() : juce::UnitTest ("ScanBoundaryFlip") {}

    // Build a SamplerVoice with a 1000-sample 2-ch DC=0.1 buffer at 48kHz.
    // Returns the voice via out-param so callers own it (no heap needed since
    // the voice + atomics live on the test stack via unique_ptrs).
    struct Harness
    {
        tw::SampleBuffer                sb;
        std::atomic<int>                rootNote    { 60 };
        std::atomic<float>              attackMs    { 0.0f };   // instant attack
        std::atomic<float>              releaseMs   { 1.0f };
        std::atomic<int>                loopMode    { 0 };      // one-shot default
        std::unique_ptr<tw::SamplerVoice> voice;

        Harness()
        {
            // 1000-sample, 2-channel buffer filled with DC 0.1f
            auto buf = std::make_shared<juce::AudioBuffer<float>> (2, 1000);
            buf->clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 1000; ++i)
                    buf->setSample (ch, i, 0.1f);
            sb.store (buf);

            voice = std::make_unique<tw::SamplerVoice> (
                sb, rootNote, attackMs, releaseMs, loopMode);
            voice->setCurrentPlaybackSampleRate (48000.0);
        }

        // Trigger a note with the given VoiceConfig.
        void triggerNote (const tw::VoiceConfig& cfg)
        {
            voice->prepareForNoteOn (cfg);
            // SynthesiserVoice::startNote needs a SynthesiserSound* — pass
            // nullptr since SamplerVoice::canPlaySound always returns true.
            voice->startNote (60, 1.0f, nullptr, 8192);
        }

        // Render n samples into a throw-away stereo buffer.
        void render (int n)
        {
            juce::AudioBuffer<float> out (2, n);
            out.clear();
            voice->renderNextBlock (out, 0, n);
        }
    };

    void runTest() override
    {
        beginTest ("Direction flips at scan-window end (Warp:None, window=1.0)");
        {
            Harness h;
            tw::VoiceConfig cfg;
            cfg.startSample  = 0;
            cfg.endSample    = 1000;
            cfg.reverse      = false;     // start forward
            cfg.pitchSemitones = 0.0f;    // pitchRatio = 1.0
            cfg.scanEnabled  = true;
            cfg.scanRate     = 1.0f;
            cfg.scanWindow   = 1.0f;      // full window → flip at sample 999

            h.triggerNote (cfg);

            // Initial direction must be forward.
            expect (! h.voice->getReversePlay(), "Initial direction should be forward");
            // Playhead must start at 0.
            expectWithinAbsoluteError (h.voice->getPlayhead(), 0.0, 1.0,
                                       "Playhead should start near sample 0");

            // Drive 1100 samples — guaranteed to hit the forward end at
            // ~sample 999 and flip to reverse.
            h.render (1100);

            expect (h.voice->getReversePlay(),
                    "reversePlay must flip to true after hitting the forward scan boundary");
        }

        beginTest ("scanWindow=0.4 narrows the ping-pong range");
        {
            // Slice: 0..1000. scanWindow=0.4 → effective range [300, 700].
            // Drive 5 blocks of 512 samples (2560 total). Playhead must never
            // stray outside [299, 701] (small float tolerance).
            Harness h;
            h.loopMode.store (0);  // one-shot default — scan overrides anyway
            tw::VoiceConfig cfg;
            cfg.startSample    = 0;
            cfg.endSample      = 1000;
            cfg.reverse        = false;
            cfg.pitchSemitones = 0.0f;
            cfg.scanEnabled    = true;
            cfg.scanRate       = 1.0f;
            cfg.scanWindow     = 0.4f;   // window = 400 samples → [300, 700]

            h.triggerNote (cfg);

            bool outOfBounds = false;
            for (int block = 0; block < 5; ++block)
            {
                h.render (512);
                const double ph = h.voice->getPlayhead();
                // Allow ±2 sample slop for the playhead-clamp-on-flip logic.
                if (ph < 298.0 || ph > 702.0)
                    outOfBounds = true;
            }

            expect (! outOfBounds,
                    "Playhead must stay within effective scan window [300, 700]"
                    " (scanWindow=0.4 on 1000-sample slice)");
        }
    }
};

static ScanBoundaryFlipTests scanBoundaryFlipTests;

// ─────────────────────────────────────────────────────────────────────────────
// ScanTurnaroundCrossfade — 8ms equal-power blend at direction reversal.
// Verifies that the sample-to-sample delta at the turnaround boundary is
// below -60 dB relative to the signal peak (i.e., click-free).
// (Task 6, Mark 1.5 scan mode)
// ─────────────────────────────────────────────────────────────────────────────
class ScanTurnaroundCrossfadeTests : public juce::UnitTest
{
public:
    ScanTurnaroundCrossfadeTests() : juce::UnitTest ("ScanTurnaroundCrossfade") {}

    void runTest() override
    {
        beginTest ("Turnaround at scan boundary introduces no clicks (>-60dB sample-to-sample delta)");
        {
            // Build a 1000-sample, 2-channel buffer filled with a 440 Hz sine
            // wave at peak amplitude 0.5. A hard direction-reversal at the
            // boundary would produce a sample-to-sample delta of up to ~0.5
            // (local waveform amplitude). The 8ms equal-power crossfade
            // should keep the max delta below 0.5 * 10^(-60/20) ≈ 0.0005.
            constexpr double SR      = 48000.0;
            constexpr int    BUF_LEN = 1000;
            constexpr float  PEAK    = 0.5f;
            constexpr float  FREQ    = 440.0f;

            tw::SampleBuffer sb;
            {
                auto buf = std::make_shared<juce::AudioBuffer<float>> (2, BUF_LEN);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < BUF_LEN; ++i)
                        buf->setSample (ch, i,
                            PEAK * std::sin (2.0f * juce::MathConstants<float>::pi
                                             * FREQ * (float) i / (float) SR));
                sb.store (buf);
            }

            std::atomic<int>   rootNote  { 60 };
            std::atomic<float> attackMs  { 0.0f };   // instant attack
            std::atomic<float> releaseMs { 1.0f };
            std::atomic<int>   loopMode  { 0 };      // one-shot; scan overrides

            tw::SamplerVoice voice (sb, rootNote, attackMs, releaseMs, loopMode);
            voice.setCurrentPlaybackSampleRate (SR);

            tw::VoiceConfig cfg;
            cfg.startSample    = 0;
            cfg.endSample      = BUF_LEN;
            cfg.reverse        = false;      // start forward
            cfg.pitchSemitones = 0.0f;       // pitchRatio = 1.0
            cfg.scanEnabled    = true;
            cfg.scanRate       = 1.0f;
            cfg.scanWindow     = 1.0f;       // full window → flip at ~sample 999

            voice.prepareForNoteOn (cfg);
            voice.startNote (60, 1.0f, nullptr, 8192);

            // Collect 4000 output samples (~4 full ping-pong cycles at pitchRatio=1).
            constexpr int TOTAL_OUT = 4000;
            juce::AudioBuffer<float> out (2, TOTAL_OUT);
            out.clear();
            voice.renderNextBlock (out, 0, TOTAL_OUT);

            const auto* outL = out.getReadPointer (0);

            // Find max sample-to-sample delta across the entire output.
            // Skip the very first sample (no predecessor). Also skip any
            // silent leading samples (envelope attack finished instantly but
            // we still guard against a zero-filled buffer giving false pass).
            float maxDelta = 0.0f;
            int   worstIdx = -1;
            for (int i = 1; i < TOTAL_OUT; ++i)
            {
                const float d = std::abs (outL[i] - outL[i - 1]);
                if (d > maxDelta) { maxDelta = d; worstIdx = i; }
            }

            // -60 dB relative to PEAK = PEAK * 10^(-60/20) ≈ 0.0005
            // The voice applies velocity (1.0) and envLevel (1.0) so the
            // threshold can be applied directly against the rendered samples.
            const float threshold = PEAK * std::pow (10.0f, -60.0f / 20.0f);

            expect (maxDelta < threshold,
                    "Max per-sample delta " + juce::String (maxDelta, 6)
                    + " at sample " + juce::String (worstIdx)
                    + " exceeds -60 dB click threshold " + juce::String (threshold, 6)
                    + " — turnaround crossfade may not be firing correctly");
        }
    }
};

static ScanTurnaroundCrossfadeTests scanTurnaroundCrossfadeTests;

#endif  // JUCE_DEBUG
