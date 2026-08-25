#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cmath>

class ModulationEngine
{
public:
    //==============================================================================
    // Parameter indices — must match JS MODULATABLE order
    enum ParamIndex
    {
        pGrainSize = 0,
        pDensity,
        pSpray,
        pPitch,
        pFreeze,
        pWander,
        pGrainFilter,
        pMix,
        pWowFlutter,
        pSaturation,
        pHiss,
        pLoopFeedback,
        pLoopDegrade,
        pLoopSpeed,
        pSpaceSize,
        pSpaceDecay,
        pSpaceTone,
        pSpaceMix,
        // Parametric EQ — 7 bands × 3 (freq/gain/Q) + HP/LP freq = 23 mod targets
        pEqB1Freq, pEqB1Gain, pEqB1Q,
        pEqB2Freq, pEqB2Gain, pEqB2Q,
        pEqB3Freq, pEqB3Gain, pEqB3Q,
        pEqB4Freq, pEqB4Gain, pEqB4Q,
        pEqB5Freq, pEqB5Gain, pEqB5Q,
        pEqB6Freq, pEqB6Gain, pEqB6Q,
        pEqB7Freq, pEqB7Gain, pEqB7Q,
        pEqHpFreq, pEqLpFreq,
        pStudioSculpt,
        pStudioWeave,
        pStudioTilt,
        pDlyTime,
        pDlyFeedback,
        pDlyTone,
        pDlyCharacter,
        pDlyMod,
        pDlyModRate,
        pDlyMix,
        pDlyDuck,
        pDlyFreeze,
        pChorusAmount,
        pChorusWidth,
        pChorusCharacter,
        // Wire machine — appended at END to preserve all existing saved-project indices
        pWireWow,
        pWireSaturation,
        pWireHiss,
        // Scan mode targets — appended at END to preserve all existing saved-project indices
        pActiveChopScanRate,
        pActiveChopScanWindow,
        pNumParams
    };

    //==============================================================================
    // Config structures (shared between UI and audio via double-buffer)

    struct LFOConfig
    {
        int shape = 0;        // 0-6: sine, tri, square, sawUp, sawDown, S&H, smoothRandom
        float rate = 1.0f;    // Hz (free mode)
        bool sync = false;
        int syncIdx = 5;      // 0-8: 8bar..1/32
        float depth = 1.0f;   // 0-1
        float phase = 0.0f;   // 0-1 phase offset
        int polarity = 0;     // 0=bi, 1=uni+, 2=uni-
    };

    struct Assignment
    {
        int source = 0;       // 0-2=LFO, 3=XY_X, 4=XY_Y
        int target = 0;       // ParamIndex
        float depth = 0.5f;   // 0-1
        bool enabled = false;
    };

    static constexpr int NUM_LFOS = 3;
    static constexpr int MAX_ASSIGNMENTS = 32;

    struct Config
    {
        LFOConfig lfos[NUM_LFOS] = {
            { 0, 1.0f,  false, 5, 1.0f, 0.0f, 0 },
            { 0, 0.5f,  false, 5, 1.0f, 0.0f, 0 },
            { 0, 0.25f, false, 5, 1.0f, 0.0f, 0 },
        };
        Assignment assignments[MAX_ASSIGNMENTS] = {};
        int numAssignments = 0;
    };

    //==============================================================================
    // Parameter range definitions (APVTS raw units)

    static constexpr float paramMin[pNumParams] = {
        5.0f, 1.0f, 0.0f, -12.0f, 0.0f, 0.0f, 0.0f, 0.0f,    // grainSize..mix
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,                    // wowFlutter..loopSpeed
        0.0f, 0.0f, 0.0f, 0.0f,                                  // spaceSize..spaceMix
        // Parametric EQ (23 entries)
        20.0f, -24.0f, 0.1f,   // B1: freq, gain, Q
        20.0f, -24.0f, 0.1f,   // B2
        20.0f, -24.0f, 0.1f,   // B3
        20.0f, -24.0f, 0.1f,   // B4
        20.0f, -24.0f, 0.1f,   // B5
        20.0f, -24.0f, 0.1f,   // B6
        20.0f, -24.0f, 0.1f,   // B7
        20.0f, 20.0f,           // HP, LP freq
        0.0f, 0.0f, -100.0f,                                       // studioSculpt, studioWeave, studioTilt
        // MF-104S delay (8 continuous + 1 freeze gate: time, feedback, tone, character, mod, modRate, mix, duck, freeze)
        5.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.0f,
        // Chorus (3 entries: amount, width, character)
        0.0f, 0.0f, 0.0f,
        // Wire machine (3 entries: wireWow, wireSaturation, wireHiss) — 0-100 like cassette
        0.0f, 0.0f, 0.0f,
        // Scan mode (2 entries: activeChopScanRate, activeChopScanWindow)
        0.1f, 0.05f
    };
    static constexpr float paramMax[pNumParams] = {
        500.0f, 100.0f, 100.0f, 12.0f, 100.0f, 100.0f, 100.0f, 100.0f,  // grainSize..mix
        100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 9.0f,                    // wowFlutter..loopSpeed
        100.0f, 100.0f, 100.0f, 100.0f,                                    // spaceSize..spaceMix
        // Parametric EQ (23 entries)
        20000.0f, 24.0f, 30.0f,   // B1: freq, gain, Q
        20000.0f, 24.0f, 30.0f,   // B2
        20000.0f, 24.0f, 30.0f,   // B3
        20000.0f, 24.0f, 30.0f,   // B4
        20000.0f, 24.0f, 30.0f,   // B5
        20000.0f, 24.0f, 30.0f,   // B6
        20000.0f, 24.0f, 30.0f,   // B7
        20000.0f, 20000.0f,        // HP, LP freq
        100.0f, 100.0f, 100.0f,                                              // studioSculpt, studioWeave, studioTilt
        1500.0f, 1.10f, 1.0f, 1.0f, 1.0f, 8.0f, 1.0f, 1.0f, 1.0f,
        // Chorus (3 entries: amount, width, character)
        1.0f, 1.0f, 1.0f,
        // Wire machine (3 entries: wireWow, wireSaturation, wireHiss) — 0-100 like cassette
        100.0f, 100.0f, 100.0f,
        // Scan mode (2 entries: activeChopScanRate, activeChopScanWindow)
        8.0f, 1.0f
    };

    //==============================================================================
    // Prepare (call from prepareToPlay)

    void prepare (double sr)
    {
        sampleRate = sr;
        // fb142-lfo — output slew coefficient (~2.5ms one-pole, the fb126 comb law:
        // a modulated value must GLIDE, never snap). Reset snaps: first sample after
        // prepare takes the raw value (RESET == LOAD-DEFAULT).
        slewK = 1.0f - std::exp (-1.0f / (0.0025f * (float) (sr > 0.0 ? sr : 44100.0)));
        slewInit = false;
        for (int i = 0; i < NUM_LFOS; i++)
        {
            lfoState[i] = {};
            lfoValues[i] = 0.0f;
            lfoOutputsAtomic[i].store (0.0f, std::memory_order_relaxed);
            lfoPhasesAtomic[i].store (0.0f, std::memory_order_relaxed);
        }
        for (int i = 0; i < pNumParams; i++)
            offsets[i] = 0.0f;
    }

    //==============================================================================
    // UI thread: push new config

    void updateConfig (const Config& cfg)
    {
        pendingConfig = cfg;
        configPending.store (true, std::memory_order_release);
    }

    //==============================================================================
    // Audio thread: pick up pending config at block start

    void beginBlock (float bpm)
    {
        if (configPending.load (std::memory_order_acquire))
        {
            activeConfig = pendingConfig;
            configPending.store (false, std::memory_order_relaxed);
        }
        currentBpm = bpm;

        for (int i = 0; i < NUM_LFOS; i++)
        {
            const auto& lfo = activeConfig.lfos[i];
            float hz = lfo.sync ? syncDivisionToHz (lfo.syncIdx, bpm) : lfo.rate;
            phaseInc[i] = static_cast<float> (hz / sampleRate);
        }
    }

    //==============================================================================
    // Audio thread: set XY pad values (call before processSample loop)

    void setXY (float x, float y)
    {
        xyX = x;
        xyY = y;
    }

    //==============================================================================
    // Audio thread: advance LFOs and compute offsets (call once per sample)

    void processSample()
    {
        // fb499 — CLEAR ONLY WHAT WAS WRITTEN. This ran `offsets[i] = 0` across all pNumParams
        // (~240 floats, ~960 bytes) EVERY SAMPLE — 48,000 times a second — in the master FX
        // loop, which itself runs unconditionally whether or not a single effect is powered on.
        // Measured on Windows: per-sample work is 4.5% of one core at idle, the largest single
        // bucket of the whole DSP cost, and this clear is inside it.
        //
        // offsets[] is written in EXACTLY one place — the enabled-assignment loop below — so
        // clearing the targets written LAST call is exactly equivalent and costs O(assignments)
        // instead of O(pNumParams). With no modulation assigned (the default patch) that is
        // ZERO work instead of 240 stores.
        //
        // Self-correcting by construction: an entry never written stays 0 forever, and when an
        // assignment is removed or disabled its target is still cleared here first, on the call
        // after it last wrote — so a stale offset can never stick. That is why the dirty list is
        // rebuilt from what was actually written rather than from the current config.
        for (int k = 0; k < numDirty_; ++k)
            offsets[dirtyIdx_[k]] = 0.0f;
        numDirty_ = 0;

        for (int i = 0; i < NUM_LFOS; i++)
        {
            const auto& lfo = activeConfig.lfos[i];
            auto& st = lfoState[i];

            float prevPhase = st.phase;
            st.phase += phaseInc[i];
            if (st.phase >= 1.0f)
                st.phase -= 1.0f;

            bool wrapped = (st.phase < prevPhase);

            float raw;
            if (lfo.shape == 5) // S&H
            {
                if (wrapped)
                    st.holdVal = nextRandom();
                raw = st.holdVal;
            }
            else if (lfo.shape == 6) // Smooth random
            {
                if (wrapped)
                {
                    st.prevRand = st.nextRand;
                    st.nextRand = nextRandom();
                }
                float t = st.phase;
                raw = st.prevRand + (st.nextRand - st.prevRand) * (t * t * (3.0f - 2.0f * t));
            }
            else
            {
                float p = std::fmod (st.phase + lfo.phase, 1.0f);
                raw = lfoWaveform (lfo.shape, p);
            }

            float polarized = applyPolarity (raw, lfo.polarity);
            // fb142-lfo — GLIDE, never snap: ~2.5ms one-pole on the output turns the
            // stepped shapes' jumps (square edge, saw wrap, S&H re-roll) into short
            // fades so modulated FX params stop clicking; sine/tri pass ~unchanged.
            const float target = polarized * lfo.depth;
            lfoValues[i] = slewInit ? lfoValues[i] + slewK * (target - lfoValues[i])
                                    : target;   // first sample after prepare snaps

            lfoOutputsAtomic[i].store (lfoValues[i], std::memory_order_relaxed);
            lfoPhasesAtomic[i].store (st.phase, std::memory_order_relaxed);
        }
        slewInit = true;   // fb142-lfo — all three snapped once; glide from here on

        float xySourceX = (xyX - 0.5f) * 2.0f;
        float xySourceY = (xyY - 0.5f) * 2.0f;

        for (int a = 0; a < activeConfig.numAssignments; a++)
        {
            const auto& asgn = activeConfig.assignments[a];
            if (! asgn.enabled) continue;
            if (asgn.target < 0 || asgn.target >= pNumParams) continue;

            float sourceVal = 0.0f;
            if (asgn.source < NUM_LFOS)
                sourceVal = lfoValues[asgn.source];
            else if (asgn.source == 3)
                sourceVal = xySourceX;
            else if (asgn.source == 4)
                sourceVal = xySourceY;

            float range = paramMax[asgn.target] - paramMin[asgn.target];
            float scaledDepth = asgn.depth * asgn.depth; // quadratic curve: visible at low values, staged at mid
            // fb499 — record the target so the next call clears exactly this entry. Duplicates
            // are harmless (two assignments may share a target; clearing twice is idempotent),
            // and the list can never overflow: it gains at most one entry per assignment and
            // holds MAX_ASSIGNMENTS.
            if (numDirty_ < (int) (sizeof (dirtyIdx_) / sizeof (dirtyIdx_[0])))
                dirtyIdx_[numDirty_++] = asgn.target;
            offsets[asgn.target] += sourceVal * scaledDepth * range;
        }
    }

    //==============================================================================
    // Audio thread: get modulated value for a parameter

    float getModulatedValue (int paramIdx, float baseValue) const
    {
        if (paramIdx < 0 || paramIdx >= pNumParams)
            return baseValue;
        float modulated = baseValue + offsets[paramIdx];
        return std::max (paramMin[paramIdx], std::min (paramMax[paramIdx], modulated));
    }

    //==============================================================================
    // Diagnostic: number of active (enabled) assignments in current config

    int getNumActiveAssignments() const
    {
        int count = 0;
        for (int a = 0; a < activeConfig.numAssignments; a++)
            if (activeConfig.assignments[a].enabled)
                count++;
        return count;
    }

    //==============================================================================
    // Audio thread: get raw offset for a parameter

    float getOffset (int paramIdx) const
    {
        if (paramIdx < 0 || paramIdx >= pNumParams)
            return 0.0f;
        return offsets[paramIdx];
    }

    //==============================================================================
    // JSON serialization (compatible with existing modStateJson format)

    static Config parseJSON (const juce::String& json)
    {
        Config cfg;
        auto parsed = juce::JSON::parse (json);
        if (parsed.isVoid()) return cfg;

        if (auto* lfosArray = parsed.getProperty ("lfos", {}).getArray())
        {
            for (int i = 0; i < NUM_LFOS && i < lfosArray->size(); i++)
            {
                const auto& l = (*lfosArray)[i];
                auto& lfo = cfg.lfos[i];
                lfo.shape    = static_cast<int> (l.getProperty ("shape", 0));
                lfo.rate     = static_cast<float> (l.getProperty ("rate", 1.0));
                lfo.sync     = static_cast<bool> (l.getProperty ("sync", false));
                lfo.syncIdx  = static_cast<int> (l.getProperty ("syncIdx", 5));
                lfo.depth    = static_cast<float> (l.getProperty ("depth", 1.0));
                lfo.phase    = static_cast<float> (l.getProperty ("phase", 0.0));
                lfo.polarity = polarityFromString (l.getProperty ("polarity", "bi").toString());
            }
        }

        if (auto* assignArray = parsed.getProperty ("assignments", {}).getArray())
        {
            cfg.numAssignments = std::min (static_cast<int> (assignArray->size()), MAX_ASSIGNMENTS);
            for (int i = 0; i < cfg.numAssignments; i++)
            {
                const auto& a = (*assignArray)[i];
                auto& asgn = cfg.assignments[i];
                asgn.source  = static_cast<int> (a.getProperty ("source", 0));
                asgn.target  = paramNameToIndex (a.getProperty ("target", "").toString());
                asgn.depth   = static_cast<float> (a.getProperty ("depth", 0.5));
                asgn.enabled = static_cast<bool> (a.getProperty ("enabled", false));
            }
        }

        return cfg;
    }

    static juce::String toJSON (const Config& cfg)
    {
        auto* root = new juce::DynamicObject();

        juce::Array<juce::var> lfosArr;
        for (int i = 0; i < NUM_LFOS; i++)
        {
            auto* lfoObj = new juce::DynamicObject();
            const auto& lfo = cfg.lfos[i];
            lfoObj->setProperty ("shape",    lfo.shape);
            lfoObj->setProperty ("rate",     static_cast<double> (lfo.rate));
            lfoObj->setProperty ("sync",     lfo.sync);
            lfoObj->setProperty ("syncIdx",  lfo.syncIdx);
            lfoObj->setProperty ("depth",    static_cast<double> (lfo.depth));
            lfoObj->setProperty ("phase",    static_cast<double> (lfo.phase));
            lfoObj->setProperty ("polarity", polarityToString (lfo.polarity));
            lfosArr.add (juce::var (lfoObj));
        }
        root->setProperty ("lfos", lfosArr);

        juce::Array<juce::var> assignArr;
        for (int i = 0; i < cfg.numAssignments; i++)
        {
            auto* aObj = new juce::DynamicObject();
            const auto& a = cfg.assignments[i];
            aObj->setProperty ("source",  a.source);
            aObj->setProperty ("target",  paramIndexToName (a.target));
            aObj->setProperty ("depth",   static_cast<double> (a.depth));
            aObj->setProperty ("enabled", a.enabled);
            assignArr.add (juce::var (aObj));
        }
        root->setProperty ("assignments", assignArr);

        return juce::JSON::toString (juce::var (root));
    }

    //==============================================================================
    // Atomic outputs for UI thread reading

    std::atomic<float> lfoOutputsAtomic[NUM_LFOS] = {};
    std::atomic<float> lfoPhasesAtomic[NUM_LFOS] = {};

private:
    //==============================================================================
    struct LFOState
    {
        float phase = 0.0f;
        float prevRand = 0.0f;
        float nextRand = 0.0f;
        float holdVal = 0.0f;
    };

    Config pendingConfig;
    Config activeConfig;
    std::atomic<bool> configPending { false };

    LFOState lfoState[NUM_LFOS] = {};
    float lfoValues[NUM_LFOS] = {};
    float offsets[pNumParams] = {};
    // fb499 — the targets written by the last processSample(), so the next one clears exactly
    // those instead of the whole offsets table. Sized to the assignment ceiling because at most
    // one entry is appended per enabled assignment.
    int   dirtyIdx_[MAX_ASSIGNMENTS] = {};
    int   numDirty_ = 0;
    float phaseInc[NUM_LFOS] = {};
    float xyX = 0.5f, xyY = 0.5f;
    float currentBpm = 120.0f;
    double sampleRate = 44100.0;
    float slewK = 0.00903f;    // fb142-lfo — per-sample output-slew coefficient (~2.5ms @ 44.1k)
    bool  slewInit = false;    // fb142-lfo — false until the first post-prepare sample (snap once)

    uint32_t rngState = 12345;

    float nextRandom()
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return static_cast<float> (rngState & 0x7FFFFFFF) / static_cast<float> (0x7FFFFFFF) * 2.0f - 1.0f;
    }

    //==============================================================================
    static float lfoWaveform (int shape, float p)
    {
        switch (shape)
        {
            case 0: return std::sin (p * 6.283185307f);
            case 1: return 1.0f - 4.0f * std::abs (p - 0.5f);
            case 2: return p < 0.5f ? 1.0f : -1.0f;
            case 3: return 2.0f * p - 1.0f;
            case 4: return 1.0f - 2.0f * p;
            default: return 0.0f;
        }
    }

    static float applyPolarity (float val, int polarity)
    {
        if (polarity == 1) return val *  0.5f + 0.5f;
        if (polarity == 2) return val * -0.5f + 0.5f;
        return val;
    }

    //==============================================================================
    static float syncDivisionToHz (int idx, float bpm)
    {
        static constexpr float beatsPerDiv[9] = {
            32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f
        };
        if (idx < 0 || idx > 8) idx = 5;
        if (bpm < 20.0f) bpm = 20.0f;
        return (bpm / 60.0f) / beatsPerDiv[idx];
    }

    //==============================================================================
    static int paramNameToIndex (const juce::String& name)
    {
        static const char* names[pNumParams] = {
            "grainSize", "density", "spray", "pitch", "freeze", "wander",
            "grainFilter", "mix", "wowFlutter", "saturation", "hiss",
            "loopFeedback", "loopDegrade", "loopSpeed",
            "spaceSize", "spaceDecay", "spaceTone", "spaceMix",
            "eqB1Freq", "eqB1Gain", "eqB1Q",
            "eqB2Freq", "eqB2Gain", "eqB2Q",
            "eqB3Freq", "eqB3Gain", "eqB3Q",
            "eqB4Freq", "eqB4Gain", "eqB4Q",
            "eqB5Freq", "eqB5Gain", "eqB5Q",
            "eqB6Freq", "eqB6Gain", "eqB6Q",
            "eqB7Freq", "eqB7Gain", "eqB7Q",
            "eqHpFreq", "eqLpFreq",
            "studioSculpt", "studioWeave", "studioTilt",
            "dlyTime", "dlyFeedback", "dlyTone", "dlyCharacter",
            "dlyMod", "dlyModRate", "dlyMix", "dlyDuck", "dlyFreeze",
            "chorusAmount", "chorusWidth", "chorusCharacter",
            "wireWow", "wireSaturation", "wireHiss",
            "activeChopScanRate", "activeChopScanWindow"
        };
        for (int i = 0; i < pNumParams; i++)
            if (name == names[i]) return i;
        return 0;
    }

    static juce::String paramIndexToName (int idx)
    {
        static const char* names[pNumParams] = {
            "grainSize", "density", "spray", "pitch", "freeze", "wander",
            "grainFilter", "mix", "wowFlutter", "saturation", "hiss",
            "loopFeedback", "loopDegrade", "loopSpeed",
            "spaceSize", "spaceDecay", "spaceTone", "spaceMix",
            "eqB1Freq", "eqB1Gain", "eqB1Q",
            "eqB2Freq", "eqB2Gain", "eqB2Q",
            "eqB3Freq", "eqB3Gain", "eqB3Q",
            "eqB4Freq", "eqB4Gain", "eqB4Q",
            "eqB5Freq", "eqB5Gain", "eqB5Q",
            "eqB6Freq", "eqB6Gain", "eqB6Q",
            "eqB7Freq", "eqB7Gain", "eqB7Q",
            "eqHpFreq", "eqLpFreq",
            "studioSculpt", "studioWeave", "studioTilt",
            "dlyTime", "dlyFeedback", "dlyTone", "dlyCharacter",
            "dlyMod", "dlyModRate", "dlyMix", "dlyDuck", "dlyFreeze",
            "chorusAmount", "chorusWidth", "chorusCharacter",
            "wireWow", "wireSaturation", "wireHiss",
            "activeChopScanRate", "activeChopScanWindow"
        };
        if (idx >= 0 && idx < pNumParams) return names[idx];
        return "grainSize";
    }

    static int polarityFromString (const juce::String& s)
    {
        if (s == "uni+") return 1;
        if (s == "uni-") return 2;
        return 0;
    }

    static juce::String polarityToString (int p)
    {
        if (p == 1) return "uni+";
        if (p == 2) return "uni-";
        return "bi";
    }
};
