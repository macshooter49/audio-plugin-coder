#pragma once
#include <map>
// =============================================================================
//  Terrain Chop — the fb106 Ribbon card as a standalone MusicEffect plugin.
//  Waves Crate
//
//  The DSP is Terrain's FlowChop.h, INCLUDED from the TerrainInstrument tree
//  (ENGINE-INCLUDE LAW: never copied — one truth). This processor is a faithful
//  transplant of Terrain's chopStage drive lambda (TerrainInstrument
//  PluginProcessor.cpp:11546-11603) with two deliberate differences:
//    · flowKnob(id, dest) / flowBase(id) become plain APVTS reads (no mod
//      matrix in v1),
//    · the FLOW chain/mode gating is Terrain-only — here the engine ALWAYS runs
//      (it free-runs internally when the host transport is stopped; fb78 law,
//      already inside FlowChop).
//  MIDI passes through untouched; a held-note stack (Terrain's resoHeld_ scan,
//  verbatim) feeds setCatchHeld/noteOnRoot so CATCH mode works on a drum loop
//  from the host's note lane — the reason this is an aumf, not an aufx.
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include "ParameterIDs.hpp"   // Terrain's id strings — the card UI binds by these exact ids
#include "FlowChop.h"         // Terrain's engine (header-only, no JUCE)

class TerrainChopAudioProcessor : public juce::AudioProcessor
{
public:
    TerrainChopAudioProcessor();
    ~TerrainChopAudioProcessor() override = default;

    // ── AudioProcessor ──────────────────────────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override               { return true; }

    const juce::String getName() const override   { return JucePlugin_Name; }
    bool acceptsMidi() const override             { return true;  }   // CATCH rides the host note lane
    bool producesMidi() const override            { return false; }
    bool isMidiEffect() const override            { return false; }
    double getTailLengthSeconds() const override  { return 0.0; }

    int getNumPrograms() override                 { return 1; }
    int getCurrentProgram() override              { return 0; }
    void setCurrentProgram (int) override         {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ── UI bridge (PluginEditor natives) ────────────────────────────────────
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    juce::String getChopFeedJson() const;                                // fb106: Ribbon playhead/slice/wet snapshot
    void requestChopWipe() noexcept { chopWipeReq_.store (true); }       // Wipe button → audio thread

    // fb137 — the card's slots+chain live in the PROCESSOR (one truth across editor
    // open/close), persisted with the plugin state — cloned from TerrainGlitch.
    void setCardStateJson (const juce::String& card, const juce::String& json)
    { const juce::ScopedLock sl (cardStateLock_); cardStates_[card] = json; }
    juce::String getCardStateJson (const juce::String& card) const
    { const juce::ScopedLock sl (cardStateLock_); const auto it = cardStates_.find (card);
      return it != cardStates_.end() ? it->second : juce::String(); }

private:
    mutable juce::CriticalSection cardStateLock_;   // fb137 — guards cardStates_
    std::map<juce::String, juce::String> cardStates_;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    wc::FlowChop chop_;

    // Raw-pointer cache, resolved ONCE in the ctor (params exist from
    // construction). The audio thread never does a string lookup — the same
    // reason Terrain built its RawCache (FL subdivides its buffer: ~980
    // processBlock calls/sec at a 512 setting, hard law).
    struct Raw
    {
        std::atomic<float>* seqRate  = nullptr; std::atomic<float>* seqGate  = nullptr;
        std::atomic<float>* seqVary  = nullptr; std::atomic<float>* seqTraj  = nullptr;
        std::atomic<float>* seqMorph = nullptr;
        std::atomic<float>* blend    = nullptr;
        std::atomic<float>* ctch     = nullptr; std::atomic<float>* slices   = nullptr;
        std::atomic<float>* loop     = nullptr; std::atomic<float>* mode     = nullptr;
        std::atomic<float>* rpts     = nullptr; std::atomic<float>* filter   = nullptr;
        std::atomic<float>* freeze   = nullptr; std::atomic<float>* collect  = nullptr;
        std::atomic<float>* scan     = nullptr; std::atomic<float>* wander   = nullptr;
        std::atomic<float>* spread   = nullptr; std::atomic<float>* speed    = nullptr;
        std::atomic<float>* steps    = nullptr; std::atomic<float>* detune   = nullptr;
        std::atomic<float>* wow      = nullptr; std::atomic<float>* smooth   = nullptr;
        std::atomic<float>* grit     = nullptr; std::atomic<float>* trim     = nullptr;
        std::atomic<float>* oSpread  = nullptr; std::atomic<float>* oBias    = nullptr;
        std::atomic<float>* oLock    = nullptr; std::atomic<float>* oSeed    = nullptr;
        std::atomic<float>* pRange   = nullptr; std::atomic<float>* pSteps   = nullptr;
        std::atomic<float>* pGlide   = nullptr; std::atomic<float>* pQuant   = nullptr;
        std::atomic<float>* rvOdds   = nullptr; std::atomic<float>* rvRun    = nullptr;
        std::atomic<float>* rvSpread = nullptr; std::atomic<float>* rvSnap   = nullptr;
        std::atomic<float>* tLen     = nullptr; std::atomic<float>* tCurve   = nullptr;
        std::atomic<float>* tRand    = nullptr; std::atomic<float>* tGate    = nullptr;
        std::atomic<float>* rCount   = nullptr; std::atomic<float>* rDecay   = nullptr;
        std::atomic<float>* rCurve   = nullptr; std::atomic<float>* rOdds    = nullptr;
        std::atomic<float>* dAmt     = nullptr; std::atomic<float>* dSize    = nullptr;
        std::atomic<float>* dSpray   = nullptr; std::atomic<float>* dTone    = nullptr;
    };
    Raw P;

    // held MIDI notes (Terrain's resoHeld_ pattern — audio-thread only)
    int heldNotes_[16] {};
    int heldN_ = 0;

    // live Ribbon feed (audio thread stores, getChopFeedJson reads — fb106)
    std::atomic<float> chopVizStepF_ { 0.0f }, chopVizWet_ { 0.0f };
    std::atomic<int>   chopVizCount_ { 0 }, chopVizSlice_ { 0 }, chopVizActive_ { 0 };
    std::atomic<bool>  chopWipeReq_ { false };
    std::atomic<int>   flowPlayingViz_ { 0 };
    std::atomic<float> currentBPM { 120.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainChopAudioProcessor)
};
