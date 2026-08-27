// =============================================================================
//  Terrain Glitch — PluginProcessor.cpp
//  Waves Crate
//
//  Everything here is a faithful transplant from Terrain Instrument:
//    · parameter registration  — PluginProcessor.cpp ~4946-5052 (paste-derived)
//    · prepare                 — glitch.prepare(sr, 4.0) + the fb125 pointer cache (:6861-6874)
//    · the glitch stage        — PluginProcessor.cpp 11608-11688 (glitchStage), with
//                                flowKnob(id, dest) -> plain APVTS read (no mod matrix in v1)
//    · the feed JSON           — getGliFeedJson (:12834-12867), byte-shape identical
//  The FLOW chain/mode gating is Terrain-only: here the engine ALWAYS runs.
// =============================================================================

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
TerrainGlitchAudioProcessor::TerrainGlitchAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TerrainGlitchAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Terrain's addFlowKnob (PluginProcessor.cpp :3924): 0..1 float, verbatim range shape
    auto addFlowKnob = [&] (const char* id, const char* name, float def) {
        layout.add (std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float>(0.0f, 1.0f), def)); };

    addFlowKnob (ParameterIDs::FLOW_GLI_BLEND, "Glitch Blend",1.00f); // GLITCH dry/wet — fb131: default 100% wet (Max's law)
    addFlowKnob (ParameterIDs::FLOW_GLI_RATE,"Glitch Rate",0.6111f);  addFlowKnob (ParameterIDs::FLOW_GLI_GATE,"Glitch Gate",0.55f);   // fb115: grid default = 1/16 (TIME IS TRUTHFUL)
    addFlowKnob (ParameterIDs::FLOW_GLI_VARY,"Glitch Vary",0.50f);  addFlowKnob (ParameterIDs::FLOW_GLI_TRAJ,"Glitch Traj",0.00f);  // VARY = fire CHANCE; 0 = never fires (silent), 0.5 = glitches out of the box
    addFlowKnob (ParameterIDs::FLOW_GLI_MORPH,"Glitch Morph",0.00f);
    // ── fb115 GLITCH Monitor card: 8 tile switches + masters + 32 per-effect knobs ──
    {
        auto addGliBool = [&] (const char* id, const char* name, bool def) {
            layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, name, def)); };
        addGliBool (ParameterIDs::FLOW_GLI_EN_REP, "Glitch Repeat On",  true);   // neutral law: REP only
        addGliBool (ParameterIDs::FLOW_GLI_EN_REV, "Glitch Reverse On", false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_TAPE,"Glitch Tape On",   false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_GATE,"Glitch Gate FX On",false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_PIT, "Glitch Pitch On",  false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_CRSH,"Glitch Crush On",  false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_FRZ, "Glitch Freeze On", false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_SCT, "Glitch Scatter On",false);
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_HOLD, 1 }, "Glitch Hold",
            juce::StringArray { "1", "2", "3", "4", "6", "8" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_LOOP, 1 }, "Glitch Loop",
            juce::StringArray { "2", "4", "8", "12", "16" }, 2));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_QUANT, 1 }, "Glitch Quant",
            juce::StringArray { "1/4", "1/8", "1/16", "1/32" }, 2));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_RELEASE, 1 }, "Glitch Release",
            juce::StringArray { "End", "Now" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_FILTER, 1 }, "Glitch Filter",
            juce::StringArray { "Off", "Low", "Mid", "High" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_PAN, 1 }, "Glitch Pan",
            juce::StringArray { "L", "C", "R" }, 1));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_SYNC, 1 }, "Glitch Clock",
            juce::StringArray { "Free", "Sync" }, 1));
    }
    addFlowKnob (ParameterIDs::FLOW_GLI_DEJAVU,"Glitch Deja Vu",0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_DECAY,"Glitch Decay",0.00f);
    // fb142 — the Fire pane's two fronts: OUT MODE (Beat Repeat's Mix/Insert/Gate law)
    // + PING (each fire bounces across the stereo field). Defaults = bit-exact old behavior.
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_GLI_OUTMODE, 1 }, "Glitch Out Mode",
        juce::StringArray { "Mix", "Cut", "Gate" }, 0));
    addFlowKnob (ParameterIDs::FLOW_GLI_PING,"Glitch Ping",0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_DROP,"Glitch Drop",0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_BURST,"Glitch Burst",0.00f);   // fb143
    addFlowKnob (ParameterIDs::FLOW_GLI_BEND,  "Glitch Bend",  0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_SEED, "Glitch Seed", 0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_REP_SIZE,  "Glitch Repeat Size", 1.00f); addFlowKnob (ParameterIDs::FLOW_GLI_REP_SPEED, "Glitch Repeat Speed",0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_REP_FADE,  "Glitch Repeat Fade", 0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_REP_VARY,  "Glitch Repeat Vary", 0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_REV_LEN,   "Glitch Rev Length",  1.00f); addFlowKnob (ParameterIDs::FLOW_GLI_REV_FADE,  "Glitch Rev Fade",    0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_REV_SPRD,  "Glitch Rev Spread",  0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_REV_SNAP,  "Glitch Rev Snap",    1.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_TAPE_CURVE,"Glitch Tape Curve",  0.70f); addFlowKnob (ParameterIDs::FLOW_GLI_TAPE_TIME, "Glitch Tape Time",   0.50f);
    addFlowKnob (ParameterIDs::FLOW_GLI_TAPE_DEPTH,"Glitch Tape Depth",  1.00f); addFlowKnob (ParameterIDs::FLOW_GLI_TAPE_SPIN, "Glitch Tape Spin",   0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_GATE_RATE, "Glitch Gate Rate",   0.14f); addFlowKnob (ParameterIDs::FLOW_GLI_GATE_SHAPE,"Glitch Gate Shape",  0.50f);
    addFlowKnob (ParameterIDs::FLOW_GLI_GATE_NUDGE,"Glitch Gate Nudge",  0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_GATE_AMT,  "Glitch Gate Amount", 1.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_PIT_SHIFT, "Glitch Pitch Shift", 0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_PIT_WALK,  "Glitch Pitch Walk",  0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_PIT_GLIDE, "Glitch Pitch Glide", 0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_PIT_JUMP,  "Glitch Pitch Jump",  0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_CRSH_BITS, "Glitch Crush Bits",  0.67f); addFlowKnob (ParameterIDs::FLOW_GLI_CRSH_RATE, "Glitch Crush Rate",  0.31f);
    addFlowKnob (ParameterIDs::FLOW_GLI_CRSH_TONE, "Glitch Crush Tone",  1.00f); addFlowKnob (ParameterIDs::FLOW_GLI_CRSH_AMT,  "Glitch Crush Amount",1.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_FRZ_SIZE,  "Glitch Freeze Size", 0.19f); addFlowKnob (ParameterIDs::FLOW_GLI_FRZ_SPRAY, "Glitch Freeze Spray",0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_FRZ_SHINE, "Glitch Freeze Shine",0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_FRZ_MELT,  "Glitch Freeze Melt", 0.30f);
    addFlowKnob (ParameterIDs::FLOW_GLI_SCT_SIZE,  "Glitch Scatter Size",0.26f); addFlowKnob (ParameterIDs::FLOW_GLI_SCT_AMT,   "Glitch Scatter Amount",1.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_SCT_VARY,  "Glitch Scatter Vary",0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_SCT_WIDTH, "Glitch Scatter Width",0.00f);
    // fb125/fb127 — glitch per-effect Out routing (Filter + Pan + Trig + Grid), the
    // exact loop Terrain runs (PluginProcessor.cpp :5028-5052) — ids by concatenation.
    {
        static const char* fx[8]  = { "REP", "REV", "TAPE", "GATE", "PIT", "CRSH", "FRZ", "SCT" };
        static const char* nm[8]  = { "Repeat", "Rev", "Tape", "Gate FX", "Pitch", "Crush", "Freeze", "Scatter" };
        for (int i = 0; i < 8; ++i)
        {
            layout.add (std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { juce::String ("FLOW_GLI_") + fx[i] + "_FLT", 1 },
                juce::String ("Glitch ") + nm[i] + " Filter",
                juce::StringArray { "Off", "Low", "Mid", "High" }, 0));
            layout.add (std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { juce::String ("FLOW_GLI_") + fx[i] + "_PAN", 1 },
                juce::String ("Glitch ") + nm[i] + " Pan",
                juce::StringArray { "L", "C", "R" }, 1));
            layout.add (std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { juce::String ("FLOW_GLI_") + fx[i] + "_TRG", 1 },
                juce::String ("Glitch ") + nm[i] + " Trig",
                juce::StringArray { "Sync", "Free", "Roll" }, 0));
            layout.add (std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { juce::String ("FLOW_GLI_") + fx[i] + "_GRID", 1 },
                juce::String ("Glitch ") + nm[i] + " Grid",
                juce::StringArray { "Main", "1/1", "1/2.", "1/2", "1/4.", "1/2T", "1/4", "1/8.", "1/4T",
                                    "1/8", "1/16.", "1/8T", "1/16", "1/32.", "1/16T", "1/32", "1/32T",
                                    "1/64", "1/128", "1/256" }, 0));
        }
    }
    return layout;
}

//==============================================================================
void TerrainGlitchAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    glitch.prepare (sampleRate, 4.0);   // FLOW · GLITCH capture ring (4 s) — Terrain :6861
    {   // fb125 — glitch per-effect Out routing: resolve the 32 raw pointers ONCE (RT-safe reads)
        static const char* fxIds[8] = { "REP", "REV", "TAPE", "GATE", "PIT", "CRSH", "FRZ", "SCT" };
        for (int fi = 0; fi < 8; ++fi)
        {
            gliFxFltP_[fi] = apvts.getRawParameterValue (juce::String ("FLOW_GLI_") + fxIds[fi] + "_FLT");
            gliFxPanP_[fi] = apvts.getRawParameterValue (juce::String ("FLOW_GLI_") + fxIds[fi] + "_PAN");
            gliFxTrgP_[fi] = apvts.getRawParameterValue (juce::String ("FLOW_GLI_") + fxIds[fi] + "_TRG");
            gliFxGrdP_[fi] = apvts.getRawParameterValue (juce::String ("FLOW_GLI_") + fxIds[fi] + "_GRID");
        }
    }
}

bool TerrainGlitchAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    // mono-in handled like Terrain (R = L); stereo-in is the native path
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

//==============================================================================
void TerrainGlitchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);   // MIDI passes through untouched (v2 headroom: note-driven Roll)

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // mono-in like Terrain: R = L before the stereo engine reads the buffer
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    // ── playhead (Terrain :9025-9032 shape; fallback bpm 120, playing = false —
    //    the engine free-runs internally when not playing: the fb78 law, already inside it)
    double flowBpm = 120.0, flowPpq = 0.0; bool flowPlaying = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())         flowBpm     = *b;
            if (auto q = pos->getPpqPosition()) flowPpq     = *q;
            flowPlaying = pos->getIsPlaying();
        }
    }
    currentBPM.store ((float) flowBpm);
    flowPlayingViz_.store (flowPlaying ? 1 : 0, std::memory_order_relaxed);   // fb137 — the feed carries "pl"

    // ── the glitchStage transplant (Terrain PluginProcessor.cpp 11608-11688).
    //    flowKnob(id, dest) -> plain APVTS read of id (no mod matrix in v1); the
    //    engine ALWAYS runs — the FLOW chain gate is Terrain-only.
    auto flowBase = [&] (const char* id) { return juce::jlimit (0.0f, 1.0f, rawParam (id)->load()); };
    auto flowKnob = [&] (const char* id) { return flowBase (id); };

    const float gRate  = flowKnob (ParameterIDs::FLOW_GLI_RATE);
    const float gGate  = flowKnob (ParameterIDs::FLOW_GLI_GATE);
    const float gVary  = flowKnob (ParameterIDs::FLOW_GLI_VARY);
    const float gTraj  = flowKnob (ParameterIDs::FLOW_GLI_TRAJ);
    const float gMorph = flowKnob (ParameterIDs::FLOW_GLI_MORPH);
    glitch.setMix (flowKnob (ParameterIDs::FLOW_GLI_BLEND));   // dry/wet (card MIX header)
    glitch.setOutMode ((int) *rawParam (ParameterIDs::FLOW_GLI_OUTMODE));   // fb142 — Mix/Cut/Gate
    glitch.setPing (flowKnob (ParameterIDs::FLOW_GLI_PING));                // fb142 — stereo bounce

    // ── fb115 extension card: every Monitor control, read per block ──
    {
        static constexpr float kHoldL[6] = { 1, 2, 3, 4, 6, 8 };
        static constexpr int   kLoopG[5] = { 2, 4, 8, 12, 16 };
        wc::GlitchExtParams X;
        X.en[0] = *rawParam (ParameterIDs::FLOW_GLI_EN_REP)  > 0.5f;
        X.en[1] = *rawParam (ParameterIDs::FLOW_GLI_EN_REV)  > 0.5f;
        X.en[2] = *rawParam (ParameterIDs::FLOW_GLI_EN_TAPE) > 0.5f;
        X.en[3] = *rawParam (ParameterIDs::FLOW_GLI_EN_GATE) > 0.5f;
        X.en[4] = *rawParam (ParameterIDs::FLOW_GLI_EN_PIT)  > 0.5f;
        X.en[5] = *rawParam (ParameterIDs::FLOW_GLI_EN_CRSH) > 0.5f;
        X.en[6] = *rawParam (ParameterIDs::FLOW_GLI_EN_FRZ)  > 0.5f;
        X.en[7] = *rawParam (ParameterIDs::FLOW_GLI_EN_SCT)  > 0.5f;
        X.dejavu = flowBase (ParameterIDs::FLOW_GLI_DEJAVU);
        X.decay  = flowKnob (ParameterIDs::FLOW_GLI_DECAY);
        X.drop   = flowKnob (ParameterIDs::FLOW_GLI_DROP);               // fb143 — hole fires
        X.burst  = flowKnob (ParameterIDs::FLOW_GLI_BURST);              // fb143 — fires streak
        X.bend   = flowKnob (ParameterIDs::FLOW_GLI_BEND);
        X.seed   = (int) std::lround (flowBase (ParameterIDs::FLOW_GLI_SEED) * 99.0f);
        X.holdSteps  = kHoldL[juce::jlimit (0, 5, (int) *rawParam (ParameterIDs::FLOW_GLI_HOLD))];
        X.loopLen    = kLoopG[juce::jlimit (0, 4, (int) *rawParam (ParameterIDs::FLOW_GLI_LOOP))];
        X.quantIdx   = (int) *rawParam (ParameterIDs::FLOW_GLI_QUANT);
        X.releaseNow = (int) *rawParam (ParameterIDs::FLOW_GLI_RELEASE) == 1;
        // fb125 — per-effect Out routing (pointers cached at prepare; FLOW_GLI_FILTER/PAN
        // retired but stay registered for old sessions)
        for (int fi = 0; fi < 8; ++fi)
        {
            X.fxFlt[fi] = gliFxFltP_[fi] != nullptr ? (int) gliFxFltP_[fi]->load() : 0;
            X.fxPan[fi] = gliFxPanP_[fi] != nullptr ? (int) gliFxPanP_[fi]->load() : 1;
            X.fxTrig[fi] = gliFxTrgP_[fi] != nullptr ? (int) gliFxTrgP_[fi]->load() : 0;
            X.fxGrid[fi] = gliFxGrdP_[fi] != nullptr ? (int) gliFxGrdP_[fi]->load() : 0;
        }
        X.sync       = (int) *rawParam (ParameterIDs::FLOW_GLI_SYNC) == 1;
        X.repSize  = flowKnob (ParameterIDs::FLOW_GLI_REP_SIZE);   X.repSpeed = flowKnob (ParameterIDs::FLOW_GLI_REP_SPEED);
        X.repFade  = flowKnob (ParameterIDs::FLOW_GLI_REP_FADE);   X.repVary  = flowKnob (ParameterIDs::FLOW_GLI_REP_VARY);
        X.revLen   = flowKnob (ParameterIDs::FLOW_GLI_REV_LEN);    X.revFade  = flowKnob (ParameterIDs::FLOW_GLI_REV_FADE);
        X.revSprd  = flowKnob (ParameterIDs::FLOW_GLI_REV_SPRD);   X.revSnap  = flowKnob (ParameterIDs::FLOW_GLI_REV_SNAP);
        X.tapeCurve= flowKnob (ParameterIDs::FLOW_GLI_TAPE_CURVE); X.tapeTime = flowKnob (ParameterIDs::FLOW_GLI_TAPE_TIME);
        X.tapeDepth= flowKnob (ParameterIDs::FLOW_GLI_TAPE_DEPTH); X.tapeSpin = flowKnob (ParameterIDs::FLOW_GLI_TAPE_SPIN);
        X.gateRate = flowKnob (ParameterIDs::FLOW_GLI_GATE_RATE);  X.gateShape= flowKnob (ParameterIDs::FLOW_GLI_GATE_SHAPE);
        X.gateNudge= flowKnob (ParameterIDs::FLOW_GLI_GATE_NUDGE); X.gateAmt  = flowKnob (ParameterIDs::FLOW_GLI_GATE_AMT);
        X.pitShift = flowKnob (ParameterIDs::FLOW_GLI_PIT_SHIFT);  X.pitWalk  = flowKnob (ParameterIDs::FLOW_GLI_PIT_WALK);
        X.pitGlide = flowKnob (ParameterIDs::FLOW_GLI_PIT_GLIDE);  X.pitJump  = flowKnob (ParameterIDs::FLOW_GLI_PIT_JUMP);
        X.crshBits = flowKnob (ParameterIDs::FLOW_GLI_CRSH_BITS);  X.crshRate = flowKnob (ParameterIDs::FLOW_GLI_CRSH_RATE);
        X.crshTone = flowKnob (ParameterIDs::FLOW_GLI_CRSH_TONE);  X.crshAmt  = flowKnob (ParameterIDs::FLOW_GLI_CRSH_AMT);
        X.frzSize  = flowKnob (ParameterIDs::FLOW_GLI_FRZ_SIZE);   X.frzSpray = flowKnob (ParameterIDs::FLOW_GLI_FRZ_SPRAY);
        X.frzShine = flowKnob (ParameterIDs::FLOW_GLI_FRZ_SHINE);  X.frzMelt  = flowKnob (ParameterIDs::FLOW_GLI_FRZ_MELT);
        X.sctSize  = flowKnob (ParameterIDs::FLOW_GLI_SCT_SIZE);   X.sctAmt   = flowKnob (ParameterIDs::FLOW_GLI_SCT_AMT);
        X.sctVary  = flowKnob (ParameterIDs::FLOW_GLI_SCT_VARY);   X.sctWidth = flowKnob (ParameterIDs::FLOW_GLI_SCT_WIDTH);
        glitch.setExt (X);
        if (gliRollReq_.exchange (false)) glitch.rollNow();   // Roll button (UI native, quantized)
    }

    float* gl = buffer.getWritePointer (0);
    float* gr = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : gl;
    glitch.process (gRate, gGate, gVary, gTraj, gMorph,
                    flowPpq, flowBpm, getSampleRate(), gl, gr, numSamples, flowPlaying);

    // live Monitor feed (UI rAF-polls getGliFeed)
    gliVizStepF_.store (glitch.vizStepF16(),          std::memory_order_relaxed);
    gliVizLoopF_.store (glitch.vizLoopSlotF(),        std::memory_order_relaxed);
    gliVizFx_.store    (glitch.vizFx(),               std::memory_order_relaxed);
    gliVizFireS_.store (glitch.vizFireStep16(),       std::memory_order_relaxed);
    gliVizHold_.store  (glitch.vizHoldSteps(),        std::memory_order_relaxed);
    gliVizWet_.store   (glitch.wetLevelViz(),         std::memory_order_relaxed);
    gliVizCount_.store ((int) glitch.vizFireCount(),  std::memory_order_relaxed);
    gliVizActive_.store(glitch.isActive() ? 1 : 0,    std::memory_order_relaxed);
    gliVizOut_.store   (glitch.outLevel(),            std::memory_order_relaxed);
    for (int vi = 0; vi < 16; ++vi) gliVizLvl_[vi].store (glitch.stepLevel (vi), std::memory_order_relaxed);
}

//==============================================================================
juce::String TerrainGlitchAudioProcessor::getGliFeedJson() const
{
    // fb115 — Monitor snapshot: step16 playhead, loop slot, firing fx + start/hold,
    // wet, fires, seed, bpm, mode, and the 16-slot input level history for the bars.
    // Byte-shape = Terrain's getGliFeedJson; "m" is pinned to 3 (GLITCH) and "on"
    // to 1 — in this plugin the engine is always the chain.
    const float sf = gliVizStepF_.load (std::memory_order_relaxed);
    const float lf = gliVizLoopF_.load (std::memory_order_relaxed);
    const float fs = gliVizFireS_.load (std::memory_order_relaxed);
    const float hl = gliVizHold_.load  (std::memory_order_relaxed);
    const float wt = gliVizWet_.load   (std::memory_order_relaxed);
    juce::String j ("{\"s\":");
    j << juce::String (std::isfinite (sf) ? sf : 0.0f, 3)
      << ",\"ls\"" << ":" << juce::String (std::isfinite (lf) ? lf : 0.0f, 3)
      << ",\"f\""  << ":" << gliVizFx_.load (std::memory_order_relaxed)
      << ",\"fs\"" << ":" << juce::String (std::isfinite (fs) ? fs : 0.0f, 2)
      << ",\"hl\"" << ":" << juce::String (std::isfinite (hl) ? hl : 1.0f, 1)
      << ",\"w\""  << ":" << juce::String (std::isfinite (wt) ? wt : 0.0f, 3)
      << ",\"a\""  << ":" << gliVizActive_.load (std::memory_order_relaxed)
      << ",\"c\""  << ":" << gliVizCount_.load (std::memory_order_relaxed)
      << ",\"sd\"" << ":" << (int) std::lround (apvts.getRawParameterValue (ParameterIDs::FLOW_GLI_SEED)->load() * 99.0f)
      << ",\"b\""  << ":" << juce::String (juce::jlimit (1.0f, 999.0f, currentBPM.load()), 2)
      << ",\"m\""  << ":" << 3
      << ",\"on\":" << 1
      << ",\"pl\":" << flowPlayingViz_.load (std::memory_order_relaxed)
      << ",\"ol\"" << ":" << juce::String (juce::jlimit (0.0f, 1.5f, gliVizOut_.load (std::memory_order_relaxed)), 3)
      << ",\"lv\":[";
    for (int i = 0; i < 16; ++i)
    {
        const float l = gliVizLvl_[i].load (std::memory_order_relaxed);
        j << (i ? "," : "") << juce::jlimit (0, 99, (int) std::lround (std::sqrt (l > 0.f ? l : 0.f) * 125.0f));
    }
    j << "]}";
    return j;
}

//==============================================================================
void TerrainGlitchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // fb137 + the state-persists law: the card's slots+chain travel WITH the session
    state.setProperty ("tgCardStateGli", getCardStateJson ("gli"), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TerrainGlitchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            setCardStateJson ("gli", state.getProperty ("tgCardStateGli", juce::String()).toString());
            apvts.replaceState (state);
        }
}

//==============================================================================
juce::AudioProcessorEditor* TerrainGlitchAudioProcessor::createEditor()
{
    return new TerrainGlitchAudioProcessorEditor (*this);
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TerrainGlitchAudioProcessor();
}
