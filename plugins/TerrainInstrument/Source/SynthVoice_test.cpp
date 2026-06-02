// SynthVoice_test.cpp — Phase 1 MPV unit tests
#include "SynthVoice.h"
#include <juce_core/juce_core.h>

class SynthSoundTests : public juce::UnitTest
{
public:
    SynthSoundTests() : juce::UnitTest ("SynthSound", "TerrainInstrument") {}

    void runTest() override
    {
        beginTest ("SynthSound accepts all notes and channels");
        {
            tw::SynthSound s;
            expect (s.appliesToNote (0));
            expect (s.appliesToNote (60));
            expect (s.appliesToNote (127));
            expect (s.appliesToChannel (1));
            expect (s.appliesToChannel (16));
        }
    }
};

class SynthVoiceTests : public juce::UnitTest
{
public:
    SynthVoiceTests() : juce::UnitTest ("SynthVoice", "TerrainInstrument") {}

    void runTest() override
    {
        beginTest ("SynthVoice accepts SynthSound");
        {
            tw::SynthVoice v;
            tw::SynthSound s;
            expect (v.canPlaySound (&s), "SynthVoice should accept SynthSound");
            expect (! v.canPlaySound (nullptr), "should reject nullptr sound");
        }

        beginTest ("PolyBLEP saw produces non-zero output at 440 Hz");
        {
            tw::SynthVoice v;
            v.setCurrentPlaybackSampleRate (48000.0);
            v.prepareToPlay (48000.0, /*samplesPerBlock*/ 1024, /*numChannels*/ 2);

            // Trigger A4 = MIDI 69 (~440 Hz). We bypass the Synthesiser
            // dispatch and call startNote/renderNextBlock directly.
            tw::SynthSound sentinel;
            v.startNote (69, 1.0f, &sentinel, 8192);

            juce::AudioBuffer<float> buf (2, 512);
            buf.clear();
            v.renderNextBlock (buf, 0, 512);

            // RMS must be non-zero — oscillator is running.
            const float rmsL = buf.getRMSLevel (0, 0, 512);
            expect (rmsL > 0.01f, "saw should produce audible RMS");

            // Count zero crossings to confirm fundamental near 440 Hz.
            // 440 Hz at 48 kHz = 1 cycle / ~109 samples = 2 crossings / cycle
            // → 512 samples ≈ 4.7 cycles → ~9 crossings expected.
            const auto* L = buf.getReadPointer (0);
            int zc = 0;
            for (int i = 1; i < 512; ++i)
                if ((L[i] >= 0.0f) != (L[i - 1] >= 0.0f)) ++zc;
            expect (zc >= 7 && zc <= 12, juce::String ("zero crossings ") + juce::String (zc));
        }

        beginTest ("AMP ADSR: attack ramps RMS upward, release decays it");
        {
            tw::SynthVoice v;
            v.setCurrentPlaybackSampleRate (48000.0);
            v.prepareToPlay (48000.0, /*samplesPerBlock*/ 1024, /*numChannels*/ 2);
            // Long attack (50 ms) + long release (100 ms) so we can sample the curve.
            v.setAmpEnvelopeParameters (50.0f, 1.0f, 1.0f, 100.0f);
            tw::SynthSound s;
            v.startNote (69, 1.0f, &s, 8192);

            juce::AudioBuffer<float> early (2, 256);  // first ~5 ms (mid-attack)
            juce::AudioBuffer<float> mid   (2, 256);  // after attack completes
            early.clear(); mid.clear();
            v.renderNextBlock (early, 0, 256);
            // Skip forward 60 ms (2880 samples) to clear the attack.
            juce::AudioBuffer<float> skip (2, 2880);
            skip.clear();
            v.renderNextBlock (skip, 0, 2880);
            v.renderNextBlock (mid,  0, 256);

            const float earlyRMS = early.getRMSLevel (0, 0, 256);
            const float midRMS   = mid  .getRMSLevel (0, 0, 256);
            expect (midRMS > earlyRMS * 1.5f,
                    juce::String ("mid RMS (") + juce::String (midRMS)
                    + ") should be substantially > early RMS (" + juce::String (earlyRMS) + ")");

            // Release: stopNote with allowTailOff. RMS should drop after 50 ms.
            v.stopNote (1.0f, true);
            juce::AudioBuffer<float> postRelease (2, 256);
            // Skip 50 ms of release time first.
            juce::AudioBuffer<float> rel (2, 2400);
            rel.clear(); postRelease.clear();
            v.renderNextBlock (rel, 0, 2400);
            v.renderNextBlock (postRelease, 0, 256);
            const float releaseRMS = postRelease.getRMSLevel (0, 0, 256);
            expect (releaseRMS < midRMS * 0.6f,
                    juce::String ("release RMS (") + juce::String (releaseRMS)
                    + ") should be < 60% of mid RMS (" + juce::String (midRMS) + ")");
        }

        beginTest ("LadderFilter cuts highs: low cutoff yields lower RMS than open");
        {
            // Render a saw with filter wide open vs filter at 200 Hz.
            auto renderRMS = [] (float cutoffHz)
            {
                tw::SynthVoice v;
                v.setCurrentPlaybackSampleRate (48000.0);
                v.prepareToPlay (48000.0, 1024, 2);
                v.setAmpEnvelopeParameters (1.0f, 1.0f, 1.0f, 1.0f);
                v.setFilterParameters (cutoffHz, 0.0f);
                v.setLevel (1.0f);
                v.setPan   (0.0f);
                tw::SynthSound s;
                v.startNote (81, 1.0f, &s, 8192);  // A5 = 880 Hz saw, lots of highs
                juce::AudioBuffer<float> buf (2, 1024);
                buf.clear();
                v.renderNextBlock (buf, 0, 1024);
                return buf.getRMSLevel (0, 0, 1024);
            };
            const float openRMS  = renderRMS (20000.0f);
            const float closedRMS = renderRMS (200.0f);
            expect (openRMS > 0.05f, "open filter RMS should be substantial");
            expect (closedRMS < openRMS * 0.5f,
                    juce::String ("closed (") + juce::String (closedRMS)
                    + ") should be < half of open (" + juce::String (openRMS) + ")");
        }

        beginTest ("Pan: pan=-1 puts energy in L only; pan=+1 in R only");
        {
            auto rms = [] (float pan, int channel)
            {
                tw::SynthVoice v;
                v.setCurrentPlaybackSampleRate (48000.0);
                v.prepareToPlay (48000.0, 512, 2);
                v.setAmpEnvelopeParameters (1.0f, 1.0f, 1.0f, 1.0f);
                v.setFilterParameters (20000.0f, 0.0f);
                v.setLevel (1.0f);
                v.setPan   (pan);
                tw::SynthSound s;
                v.startNote (69, 1.0f, &s, 8192);
                juce::AudioBuffer<float> buf (2, 512);
                buf.clear();
                v.renderNextBlock (buf, 0, 512);
                return buf.getRMSLevel (channel, 0, 512);
            };
            expect (rms (-1.0f, 0) > 0.05f, "pan=-1 L should be loud");
            expect (rms (-1.0f, 1) < 0.005f, "pan=-1 R should be near silent");
            expect (rms (+1.0f, 1) > 0.05f, "pan=+1 R should be loud");
            expect (rms (+1.0f, 0) < 0.005f, "pan=+1 L should be near silent");
        }
    }
};

static SynthSoundTests sSynthSoundTests;
static SynthVoiceTests sSynthVoiceTests;
