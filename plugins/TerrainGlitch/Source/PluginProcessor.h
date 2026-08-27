#pragma once
// =============================================================================
//  Terrain Glitch — PluginProcessor.h
//  Waves Crate
//
//  The fb115 Monitor card as a standalone FX plugin (VST3 / AU / Standalone).
//  DSP = Terrain's FlowGlitch.h, INCLUDED from plugins/TerrainInstrument/Source
//  (the engine-include law: one truth, never a copy). The processor is a faithful
//  transplant of Terrain's glitchStage (PluginProcessor.cpp 11540-11700) with the
//  mod matrix removed: flowKnob(id, dest) becomes a plain APVTS read of id,
//  flowBase(id) likewise. The FLOW chain/mode gating is Terrain-only — here the
//  engine ALWAYS runs (this plugin IS the glitch).
//
//  MIDI: pass-through. Glitch consumes no MIDI in v1 (v2 headroom: note-driven
//  Roll / per-note fires).
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "FlowGlitch.h"         // FLOW · GLITCH engine — Terrain's, via include path
#include <atomic>
#include <map>

// ── Parameter IDs — VERBATIM from Terrain's ParameterIDs.hpp (paste-derived).
//    The card UI binds by these exact strings; never re-type from memory.
namespace ParameterIDs
{
    constexpr char FLOW_GLI_RATE[] = "FLOW_GLI_RATE";  constexpr char FLOW_GLI_GATE[] = "FLOW_GLI_GATE";
    constexpr char FLOW_GLI_VARY[] = "FLOW_GLI_VARY";  constexpr char FLOW_GLI_TRAJ[] = "FLOW_GLI_TRAJ";
    constexpr char FLOW_GLI_MORPH[]= "FLOW_GLI_MORPH";
    constexpr char FLOW_GLI_BLEND[] = "FLOW_GLI_BLEND";   // float 0..1 — GLITCH dry/wet (card MIX header)
    constexpr char FLOW_GLI_EN_REP[]  = "FLOW_GLI_EN_REP";   constexpr char FLOW_GLI_EN_REV[]  = "FLOW_GLI_EN_REV";
    constexpr char FLOW_GLI_EN_TAPE[] = "FLOW_GLI_EN_TAPE";  constexpr char FLOW_GLI_EN_GATE[] = "FLOW_GLI_EN_GATE";
    constexpr char FLOW_GLI_EN_PIT[]  = "FLOW_GLI_EN_PIT";   constexpr char FLOW_GLI_EN_CRSH[] = "FLOW_GLI_EN_CRSH";
    constexpr char FLOW_GLI_EN_FRZ[]  = "FLOW_GLI_EN_FRZ";   constexpr char FLOW_GLI_EN_SCT[]  = "FLOW_GLI_EN_SCT";
    constexpr char FLOW_GLI_DEJAVU[]  = "FLOW_GLI_DEJAVU";   constexpr char FLOW_GLI_DECAY[]   = "FLOW_GLI_DECAY";
    constexpr char FLOW_GLI_OUTMODE[] = "FLOW_GLI_OUTMODE";   constexpr char FLOW_GLI_PING[]    = "FLOW_GLI_PING";   // fb142 — Mix/Cut/Gate + per-fire stereo bounce
    constexpr char FLOW_GLI_DROP[]    = "FLOW_GLI_DROP";      constexpr char FLOW_GLI_BURST[]   = "FLOW_GLI_BURST";  // fb143 — hole fires + fires streak into clusters
    constexpr char FLOW_GLI_BEND[]    = "FLOW_GLI_BEND";     constexpr char FLOW_GLI_SEED[]    = "FLOW_GLI_SEED";    // 0 = Free, else 1..99
    constexpr char FLOW_GLI_HOLD[]    = "FLOW_GLI_HOLD";     // choice {1,2,3,4,6,8} steps
    constexpr char FLOW_GLI_LOOP[]    = "FLOW_GLI_LOOP";     // choice {2,4,8,12,16} pattern length
    constexpr char FLOW_GLI_QUANT[]   = "FLOW_GLI_QUANT";    // choice 1/4..1/32 (Roll punch-in grid)
    constexpr char FLOW_GLI_RELEASE[] = "FLOW_GLI_RELEASE";  // choice End/Now
    constexpr char FLOW_GLI_FILTER[]  = "FLOW_GLI_FILTER";   // choice Off/Low/Mid/High (legacy wet bus — retired fb125, registered for old sessions)
    constexpr char FLOW_GLI_PAN[]     = "FLOW_GLI_PAN";      // choice L/C/R (legacy wet bus — retired fb125, registered for old sessions)
    constexpr char FLOW_GLI_SYNC[]    = "FLOW_GLI_SYNC";     // choice Free/Sync (clock)
    constexpr char FLOW_GLI_REP_SIZE[]  = "FLOW_GLI_REP_SIZE";  constexpr char FLOW_GLI_REP_SPEED[] = "FLOW_GLI_REP_SPEED";
    constexpr char FLOW_GLI_REP_FADE[]  = "FLOW_GLI_REP_FADE";  constexpr char FLOW_GLI_REP_VARY[]  = "FLOW_GLI_REP_VARY";
    constexpr char FLOW_GLI_REV_LEN[]   = "FLOW_GLI_REV_LEN";   constexpr char FLOW_GLI_REV_FADE[]  = "FLOW_GLI_REV_FADE";
    constexpr char FLOW_GLI_REV_SPRD[]  = "FLOW_GLI_REV_SPRD";  constexpr char FLOW_GLI_REV_SNAP[]  = "FLOW_GLI_REV_SNAP";
    constexpr char FLOW_GLI_TAPE_CURVE[]= "FLOW_GLI_TAPE_CURVE";constexpr char FLOW_GLI_TAPE_TIME[] = "FLOW_GLI_TAPE_TIME";
    constexpr char FLOW_GLI_TAPE_DEPTH[]= "FLOW_GLI_TAPE_DEPTH";constexpr char FLOW_GLI_TAPE_SPIN[] = "FLOW_GLI_TAPE_SPIN";
    constexpr char FLOW_GLI_GATE_RATE[] = "FLOW_GLI_GATE_RATE"; constexpr char FLOW_GLI_GATE_SHAPE[]= "FLOW_GLI_GATE_SHAPE";
    constexpr char FLOW_GLI_GATE_NUDGE[]= "FLOW_GLI_GATE_NUDGE";constexpr char FLOW_GLI_GATE_AMT[]  = "FLOW_GLI_GATE_AMT";
    constexpr char FLOW_GLI_PIT_SHIFT[] = "FLOW_GLI_PIT_SHIFT"; constexpr char FLOW_GLI_PIT_WALK[]  = "FLOW_GLI_PIT_WALK";
    constexpr char FLOW_GLI_PIT_GLIDE[] = "FLOW_GLI_PIT_GLIDE"; constexpr char FLOW_GLI_PIT_JUMP[]  = "FLOW_GLI_PIT_JUMP";
    constexpr char FLOW_GLI_CRSH_BITS[] = "FLOW_GLI_CRSH_BITS"; constexpr char FLOW_GLI_CRSH_RATE[] = "FLOW_GLI_CRSH_RATE";
    constexpr char FLOW_GLI_CRSH_TONE[] = "FLOW_GLI_CRSH_TONE"; constexpr char FLOW_GLI_CRSH_AMT[]  = "FLOW_GLI_CRSH_AMT";
    constexpr char FLOW_GLI_FRZ_SIZE[]  = "FLOW_GLI_FRZ_SIZE";  constexpr char FLOW_GLI_FRZ_SPRAY[] = "FLOW_GLI_FRZ_SPRAY";
    constexpr char FLOW_GLI_FRZ_SHINE[] = "FLOW_GLI_FRZ_SHINE"; constexpr char FLOW_GLI_FRZ_MELT[]  = "FLOW_GLI_FRZ_MELT";
    constexpr char FLOW_GLI_SCT_SIZE[]  = "FLOW_GLI_SCT_SIZE";  constexpr char FLOW_GLI_SCT_AMT[]   = "FLOW_GLI_SCT_AMT";
    constexpr char FLOW_GLI_SCT_VARY[]  = "FLOW_GLI_SCT_VARY";  constexpr char FLOW_GLI_SCT_WIDTH[] = "FLOW_GLI_SCT_WIDTH";
    // fb125/fb127 — per-effect Out routing (the gliFx*P_ caches read these by
    // string concatenation: "FLOW_GLI_" + {REP,REV,TAPE,GATE,PIT,CRSH,FRZ,SCT} +
    // {_FLT,_PAN,_TRG,_GRID}) — registered in a loop, exactly like Terrain.
}

//==============================================================================
class TerrainGlitchAudioProcessor : public juce::AudioProcessor
{
public:
    TerrainGlitchAudioProcessor();
    ~TerrainGlitchAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }     // pass-through (v2 headroom)
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // fb115 — Monitor playhead/fire/levels snapshot (rAF-polled by the card via
    // the getGliFeed native). JSON byte-shape = Terrain's getGliFeedJson.
    juce::String getGliFeedJson() const;

    // fb115 — Roll button → audio thread (quantized punch-in), same shape as
    // Terrain's requestGliRoll / gliRollReq_.
    void requestGliRoll() noexcept { gliRollReq_.store (true); }

    // fb137 — the card's slots+chain live in the PROCESSOR (one truth across
    // editor open/close), Terrain PluginProcessor.h :751-757 shape. Here it is
    // additionally PERSISTED with the plugin state (the state-persists law).
    void setCardStateJson (const juce::String& card, const juce::String& json)
    { const juce::ScopedLock sl (cardStateLock_); cardStates_[card] = json; }
    juce::String getCardStateJson (const juce::String& card) const
    { const juce::ScopedLock sl (cardStateLock_); const auto it = cardStates_.find (card);
      return it != cardStates_.end() ? it->second : juce::String(); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    wc::FlowGlitch glitch;      // FLOW · GLITCH engine — always runs (no chain gate here)

    // ── live Monitor feed atomics (Terrain PluginProcessor.h 1117-1124, verbatim) ──
    std::atomic<float>            gliVizStepF_ { 0.0f }, gliVizLoopF_ { 0.0f }, gliVizFireS_ { 0.0f },
                                  gliVizHold_ { 1.0f }, gliVizWet_ { 0.0f };
    std::atomic<int>              gliVizFx_ { -1 }, gliVizCount_ { 0 }, gliVizActive_ { 0 };
    std::atomic<float>            gliVizLvl_[16] {};
    std::atomic<float>            gliVizOut_ { 0.0f };   // fb124 — speaker meter level
    std::atomic<float>*           gliFxFltP_[8] { nullptr }, * gliFxPanP_[8] { nullptr },
                                * gliFxTrgP_[8] { nullptr }, * gliFxGrdP_[8] { nullptr };   // fb125/127 — cached at prepare
    std::atomic<bool>             gliRollReq_ { false };

    std::atomic<float> currentBPM { 120.f };  // populated from playhead (fallback 120)
    std::atomic<int>   flowPlayingViz_ { 0 }; // fb137 — transport state for the feed ("pl")

    mutable juce::CriticalSection cardStateLock_;
    std::map<juce::String, juce::String> cardStates_;

    // ── rawParam — Terrain's pointer-keyed memo (PluginProcessor.h fb495):
    //    PLAIN loads on the hot path (a barrier is free on x86 and costly on ARM);
    //    insert publishes value, release fence, then key. Ids are string literals,
    //    so the pointer IS the key.
    struct RawCache
    {
        static constexpr size_t kSlots = 512, kMask = kSlots - 1;
        const void*         keys[kSlots] {};
        std::atomic<float>* vals[kSlots] {};
    };
    mutable std::unique_ptr<RawCache> rawCache_ { new RawCache() };
    std::atomic<float>* rawParam (const char* id) const
    {
        auto& c = *rawCache_;
        const uintptr_t k = (uintptr_t) id;
        size_t h = (size_t) (((k >> 3) * 11400714819323198485ull) >> 55) & RawCache::kMask;
        for (int probe = 0; probe < 64; ++probe)
        {
            const void* key = c.keys[h];                       // plain load
            if (key == (const void*) id)
            {
                if (auto* v = c.vals[h]) return v;              // null only mid-insert -> slow path
                break;
            }
            if (key == nullptr) break;                         // empty slot: this is where it goes
            h = (h + 1) & RawCache::kMask;
        }
        auto* p = const_cast<juce::AudioProcessorValueTreeState&> (apvts).getRawParameterValue (id);
        c.vals[h] = p;
        std::atomic_thread_fence (std::memory_order_release);
        c.keys[h] = (const void*) id;
        return p;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainGlitchAudioProcessor)
};
