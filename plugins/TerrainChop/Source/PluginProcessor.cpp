// =============================================================================
//  Terrain Chop — processor. See PluginProcessor.h for the transplant contract.
//  Waves Crate
// =============================================================================

#include "PluginProcessor.h"
#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Parameter layout — VERBATIM from Terrain's createParameterLayout()
//  (TerrainInstrument PluginProcessor.cpp:3924-3933 macros/blend and
//  :4907-4945 the fb106 CHOP extension card). Paste-derived, never re-typed:
//  the card UI binds by these exact id strings, ranges and defaults.
// ─────────────────────────────────────────────────────────────────────────────
juce::AudioProcessorValueTreeState::ParameterLayout TerrainChopAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFlowKnob = [&] (const char* id, const char* name, float def) {
        layout.add (std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float>(0.0f, 1.0f), def)); };

    // mode-2 macros — IDs stay FLOW_SEQ_* (preset-stable) but now drive CHOP (Rate/Gate/Vary/Style/Morph)
    addFlowKnob (ParameterIDs::FLOW_SEQ_RATE,"Chop Rate",0.6111f);  addFlowKnob (ParameterIDs::FLOW_SEQ_GATE,"Chop Gate",0.55f);   // fb107: chop grid default = 1/16
    addFlowKnob (ParameterIDs::FLOW_SEQ_VARY,"Chop Vary",0.00f);  addFlowKnob (ParameterIDs::FLOW_SEQ_TRAJ,"Chop Style",0.00f);
    addFlowKnob (ParameterIDs::FLOW_SEQ_MORPH,"Chop Morph",0.00f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_BLEND,"Chop Blend",1.00f);   // dry/wet — fb131: default 100% wet (Max: "when I select it, the mix is at 100")

    // ── CHOP extension card (fb106): Ribbon scalars + 24 lane depth knobs.
    // Same law as fb105: setSynParam-only, choices read as INDEX.
    layout.add (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_CATCH, 1 }, "Chop Catch", false));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_SLICES, 1 }, "Chop Slices",
        juce::StringArray { "2", "3", "4", "6", "8", "12", "16" }, 4));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_LOOP, 1 }, "Chop Loop",
        juce::StringArray { "2", "4", "6", "8", "10", "12", "16" }, 3));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_MODE, 1 }, "Chop Order Mode",
        juce::StringArray { "Step", "Ping", "Rand", "Walk" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_RPTS, 1 }, "Chop Repeats",
        juce::StringArray { "1", "2", "3", "4" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_FILTER, 1 }, "Chop Filter",
        juce::StringArray { "Off", "Low", "Mid", "High" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_FREEZE, 1 }, "Chop Freeze", false));
    layout.add (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_COLLECT, 1 }, "Chop Collect", false));
    addFlowKnob (ParameterIDs::FLOW_CHOP_SCAN,  "Chop Scan",  1.00f);  addFlowKnob (ParameterIDs::FLOW_CHOP_WANDER,"Chop Wander",0.00f);   // fb107: Scan = Now (live chop, not a fixed delay)
    addFlowKnob (ParameterIDs::FLOW_CHOP_SPREAD,"Chop Spread",0.00f);  addFlowKnob (ParameterIDs::FLOW_CHOP_SPEED, "Chop Speed", 0.00f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_STEPS, "Chop Steps", 0.00f);  addFlowKnob (ParameterIDs::FLOW_CHOP_DETUNE,"Chop Detune",0.00f);   // fb107 neutral bus
    addFlowKnob (ParameterIDs::FLOW_CHOP_WOW,   "Chop Wow",   0.00f);  addFlowKnob (ParameterIDs::FLOW_CHOP_SMOOTH,"Chop Smooth",0.15f);   // smooth .15 = legacy fade ×1.0
    addFlowKnob (ParameterIDs::FLOW_CHOP_GRIT,  "Chop Grit",  0.00f);  addFlowKnob (ParameterIDs::FLOW_CHOP_TRIM,  "Chop Trim",  0.50f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_O_SPREAD,"Chop Order Spread",0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_O_BIAS,"Chop Order Bias",0.50f);   // fb110: scatter opt-in
    addFlowKnob (ParameterIDs::FLOW_CHOP_O_LOCK,  "Chop Order Lock",  0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_O_SEED,"Chop Order Seed",0.44f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_P_RANGE, "Chop Pitch Range", 0.33f); addFlowKnob (ParameterIDs::FLOW_CHOP_P_STEPS,"Chop Pitch Steps",0.00f);   // fb111: semitone interval + every-Nth pattern
    addFlowKnob (ParameterIDs::FLOW_CHOP_P_GLIDE, "Chop Pitch Glide", 0.15f); addFlowKnob (ParameterIDs::FLOW_CHOP_P_QUANT,"Chop Pitch Fall",0.00f);   // fb111: deterministic rise/fall, no dice
    addFlowKnob (ParameterIDs::FLOW_CHOP_RV_ODDS, "Chop Rev Odds",    0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_RV_RUN, "Chop Rev Run",  0.25f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_RV_SPREAD,"Chop Rev Spread", 0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_RV_SNAP,"Chop Rev Snap", 0.60f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_T_LEN,   "Chop Trim Length", 0.875f); addFlowKnob (ParameterIDs::FLOW_CHOP_T_CURVE,"Chop Trim Curve",0.30f);   // ×1.0 neutral
    addFlowKnob (ParameterIDs::FLOW_CHOP_T_RAND,  "Chop Trim Random", 0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_T_GATE, "Chop Trim Gate", 0.00f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_R_COUNT, "Chop Repeat Count",0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_R_DECAY,"Chop Repeat Decay",0.40f);   // fb113: Count=1 neutral, the doer
    addFlowKnob (ParameterIDs::FLOW_CHOP_R_CURVE, "Chop Repeat Curve",0.50f); addFlowKnob (ParameterIDs::FLOW_CHOP_R_ODDS, "Chop Repeat Odds",1.00f);   // fb113: Odds carves DOWN from All — Count is instantly audible
    addFlowKnob (ParameterIDs::FLOW_CHOP_D_AMT,   "Chop Drop Amount", 0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_D_SIZE, "Chop Drop Size", 0.40f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_D_SPRAY, "Chop Drop Spray",  0.10f); addFlowKnob (ParameterIDs::FLOW_CHOP_D_TONE, "Chop Drop Tone", 0.50f);

    return layout;
}

// ─────────────────────────────────────────────────────────────────────────────
TerrainChopAudioProcessor::TerrainChopAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    // resolve the raw-pointer cache ONCE (params exist from construction)
    auto rp = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    P.seqRate  = rp (ParameterIDs::FLOW_SEQ_RATE);    P.seqGate  = rp (ParameterIDs::FLOW_SEQ_GATE);
    P.seqVary  = rp (ParameterIDs::FLOW_SEQ_VARY);    P.seqTraj  = rp (ParameterIDs::FLOW_SEQ_TRAJ);
    P.seqMorph = rp (ParameterIDs::FLOW_SEQ_MORPH);
    P.blend    = rp (ParameterIDs::FLOW_CHOP_BLEND);
    P.ctch     = rp (ParameterIDs::FLOW_CHOP_CATCH);  P.slices   = rp (ParameterIDs::FLOW_CHOP_SLICES);
    P.loop     = rp (ParameterIDs::FLOW_CHOP_LOOP);   P.mode     = rp (ParameterIDs::FLOW_CHOP_MODE);
    P.rpts     = rp (ParameterIDs::FLOW_CHOP_RPTS);   P.filter   = rp (ParameterIDs::FLOW_CHOP_FILTER);
    P.freeze   = rp (ParameterIDs::FLOW_CHOP_FREEZE); P.collect  = rp (ParameterIDs::FLOW_CHOP_COLLECT);
    P.scan     = rp (ParameterIDs::FLOW_CHOP_SCAN);   P.wander   = rp (ParameterIDs::FLOW_CHOP_WANDER);
    P.spread   = rp (ParameterIDs::FLOW_CHOP_SPREAD); P.speed    = rp (ParameterIDs::FLOW_CHOP_SPEED);
    P.steps    = rp (ParameterIDs::FLOW_CHOP_STEPS);  P.detune   = rp (ParameterIDs::FLOW_CHOP_DETUNE);
    P.wow      = rp (ParameterIDs::FLOW_CHOP_WOW);    P.smooth   = rp (ParameterIDs::FLOW_CHOP_SMOOTH);
    P.grit     = rp (ParameterIDs::FLOW_CHOP_GRIT);   P.trim     = rp (ParameterIDs::FLOW_CHOP_TRIM);
    P.oSpread  = rp (ParameterIDs::FLOW_CHOP_O_SPREAD); P.oBias  = rp (ParameterIDs::FLOW_CHOP_O_BIAS);
    P.oLock    = rp (ParameterIDs::FLOW_CHOP_O_LOCK);   P.oSeed  = rp (ParameterIDs::FLOW_CHOP_O_SEED);
    P.pRange   = rp (ParameterIDs::FLOW_CHOP_P_RANGE);  P.pSteps = rp (ParameterIDs::FLOW_CHOP_P_STEPS);
    P.pGlide   = rp (ParameterIDs::FLOW_CHOP_P_GLIDE);  P.pQuant = rp (ParameterIDs::FLOW_CHOP_P_QUANT);
    P.rvOdds   = rp (ParameterIDs::FLOW_CHOP_RV_ODDS);  P.rvRun  = rp (ParameterIDs::FLOW_CHOP_RV_RUN);
    P.rvSpread = rp (ParameterIDs::FLOW_CHOP_RV_SPREAD);P.rvSnap = rp (ParameterIDs::FLOW_CHOP_RV_SNAP);
    P.tLen     = rp (ParameterIDs::FLOW_CHOP_T_LEN);    P.tCurve = rp (ParameterIDs::FLOW_CHOP_T_CURVE);
    P.tRand    = rp (ParameterIDs::FLOW_CHOP_T_RAND);   P.tGate  = rp (ParameterIDs::FLOW_CHOP_T_GATE);
    P.rCount   = rp (ParameterIDs::FLOW_CHOP_R_COUNT);  P.rDecay = rp (ParameterIDs::FLOW_CHOP_R_DECAY);
    P.rCurve   = rp (ParameterIDs::FLOW_CHOP_R_CURVE);  P.rOdds  = rp (ParameterIDs::FLOW_CHOP_R_ODDS);
    P.dAmt     = rp (ParameterIDs::FLOW_CHOP_D_AMT);    P.dSize  = rp (ParameterIDs::FLOW_CHOP_D_SIZE);
    P.dSpray   = rp (ParameterIDs::FLOW_CHOP_D_SPRAY);  P.dTone  = rp (ParameterIDs::FLOW_CHOP_D_TONE);
}

void TerrainChopAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    // Terrain :6860 — fb106: 8 s capture ring so the Ribbon's 16-cell memory
    // holds at slow rates.
    chop_.prepare (sampleRate, 8.0);
}

bool TerrainChopAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

// ─────────────────────────────────────────────────────────────────────────────
//  processBlock — the chopStage transplant (Terrain PluginProcessor.cpp
//  :11546-11603). The engine ALWAYS runs here (no FLOW chain), and it is an
//  audio INSERT: it re-grooves the host's buffer IN PLACE, click-free.
// ─────────────────────────────────────────────────────────────────────────────
void TerrainChopAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // ── transport from the host (fallback bpm 120, playing = false — the engine
    //    free-runs internally when not playing; fb78 law, already inside it)
    double flowBpm = 120.0, flowPpq = 0.0; bool flowPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())         flowBpm = *b;
            if (auto q = pos->getPpqPosition()) flowPpq = *q;
            flowPlaying = pos->getIsPlaying();
        }
    currentBPM.store ((float) flowBpm, std::memory_order_relaxed);
    flowPlayingViz_.store (flowPlaying ? 1 : 0, std::memory_order_relaxed);

    // ── held-note stack (Terrain's resoHeld_ scan, verbatim): read-only — MIDI
    //    passes through untouched. note-on adds, note-off removes, all-off clears.
    for (const auto meta : midiMessages)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int nn = m.getNoteNumber();
            bool dup = false;
            for (int j = 0; j < heldN_; ++j) if (heldNotes_[j] == nn) { dup = true; break; }
            if (! dup && heldN_ < (int) (sizeof (heldNotes_) / sizeof (int)))
                heldNotes_[heldN_++] = nn;
        }
        else if (m.isNoteOff())
        {
            const int nn = m.getNoteNumber();
            for (int j = 0; j < heldN_; ++j)
                if (heldNotes_[j] == nn) { heldNotes_[j] = heldNotes_[--heldN_]; break; }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            heldN_ = 0;
        }
    }

    // ── the chopStage: flowKnob(id, dest) → plain APVTS read (no mod matrix in v1)
    auto knob = [] (std::atomic<float>* p) { return juce::jlimit (0.0f, 1.0f, p->load()); };

    const float cRate  = knob (P.seqRate);
    const float cGate  = knob (P.seqGate);
    const float cVary  = knob (P.seqVary);
    const float cTraj  = knob (P.seqTraj);
    const float cMorph = knob (P.seqMorph);
    chop_.setMix (knob (P.blend));   // dry/wet; default 1.0 (fb131 — Max's law)

    // ── fb106 extension card: every Ribbon control, read per block ──
    {
        static constexpr int kSliceL[7] = { 2, 3, 4, 6, 8, 12, 16 };
        static constexpr int kLoopL[7]  = { 2, 4, 6, 8, 10, 12, 16 };
        wc::FlowChop::ChopExtParams X;
        X.slices    = kSliceL[juce::jlimit (0, 6, (int) P.slices->load())];
        X.loopCells = kLoopL [juce::jlimit (0, 6, (int) P.loop->load())];
        X.modeOrder = (int) P.mode->load();
        X.rpts      = 1 + (int) P.rpts->load();
        X.filter    = (int) P.filter->load();
        X.freeze    = P.freeze->load()  > 0.5f;
        X.collect   = P.collect->load() > 0.5f;
        X.scan   = knob (P.scan);     X.wander = knob (P.wander);
        X.spread = knob (P.spread);   X.speed  = knob (P.speed);
        X.steps  = knob (P.steps);    X.detune = knob (P.detune);
        X.wow    = knob (P.wow);      X.smooth = knob (P.smooth);
        X.grit   = knob (P.grit);     X.trim   = knob (P.trim);
        X.oSpread= knob (P.oSpread);  X.oBias  = knob (P.oBias);
        X.oLock  = knob (P.oLock);    X.oSeed  = knob (P.oSeed);
        X.pRange = knob (P.pRange);   X.pSteps = knob (P.pSteps);
        X.pGlide = knob (P.pGlide);   X.pQuant = knob (P.pQuant);
        X.rvOdds = knob (P.rvOdds);   X.rvRun  = knob (P.rvRun);
        X.rvSpread=knob (P.rvSpread); X.rvSnap = knob (P.rvSnap);
        X.tLen   = knob (P.tLen);     X.tCurve = knob (P.tCurve);
        X.tRand  = knob (P.tRand);    X.tGate  = knob (P.tGate);
        X.rCount = knob (P.rCount);   X.rDecay = knob (P.rDecay);
        X.rCurve = knob (P.rCurve);   X.rOdds  = knob (P.rOdds);
        X.dAmt   = knob (P.dAmt);     X.dSize  = knob (P.dSize);
        X.dSpray = knob (P.dSpray);   X.dTone  = knob (P.dTone);
        chop_.setExt (X);
        chop_.setMode (P.ctch->load() > 0.5f ? wc::ChopMode::Catch
                                             : wc::ChopMode::AlwaysOn);
        chop_.setCatchHeld (heldN_ > 0);                    // CATCH rides the real held keys
        if (heldN_ > 0) chop_.noteOnRoot (heldNotes_[heldN_ - 1]);
        if (chopWipeReq_.exchange (false)) chop_.wipe();    // Wipe button (UI native)
    }

    float* cl = buffer.getWritePointer (0);
    float* cr = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : cl;
    chop_.process (cRate, cGate, cVary, cTraj, cMorph,
                   flowPpq, flowBpm, getSampleRate(), cl, cr, numSamples, flowPlaying);

    // live Ribbon feed (UI rAF-polls getChopFeed) — Terrain :11599-11603
    chopVizStepF_.store (chop_.vizStepF(),             std::memory_order_relaxed);
    chopVizCount_.store ((int) chop_.vizFireCount(),   std::memory_order_relaxed);
    chopVizSlice_.store (chop_.lastSliceIndex(),       std::memory_order_relaxed);
    chopVizWet_.store   (chop_.wetLevel(),             std::memory_order_relaxed);
    chopVizActive_.store(chop_.isActive() ? 1 : 0,     std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────────────────
//  getChopFeedJson — Terrain's EXACT byte shape (PluginProcessor.cpp:12817).
//  "m" is Terrain's FLOW_MODE index — here CHOP is the whole plugin, so it is
//  the constant 2; "on" (chain membership) is the constant 1: the engine
//  always runs. The card's feed.on===1 gate therefore always takes the live
//  branch, which is the point.
// ─────────────────────────────────────────────────────────────────────────────
juce::String TerrainChopAudioProcessor::getChopFeedJson() const
{
    const float sf = chopVizStepF_.load (std::memory_order_relaxed);
    const float wt = chopVizWet_.load (std::memory_order_relaxed);
    juce::String j ("{\"s\":");
    j << juce::String (std::isfinite (sf) ? sf : 0.0f, 3)
      << ",\"c\""  << ":" << chopVizCount_.load (std::memory_order_relaxed)
      << ",\"sl\"" << ":" << chopVizSlice_.load (std::memory_order_relaxed)
      << ",\"a\""  << ":" << chopVizActive_.load (std::memory_order_relaxed)
      << ",\"w\""  << ":" << juce::String (std::isfinite (wt) ? wt : 0.0f, 3)
      << ",\"b\""  << ":" << juce::String (juce::jlimit (1.0f, 999.0f, currentBPM.load()), 2)
      << ",\"m\""  << ":" << 2
      << ",\"on\":" << 1
      << ",\"pl\":" << flowPlayingViz_.load (std::memory_order_relaxed) << "}";
    return j;
}

// ─────────────────────────────────────────────────────────────────────────────
void TerrainChopAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // fb137 — the card's slots+chain travel with the session (TerrainGlitch's shape)
    state.setProperty ("tcCardStateChop", getCardStateJson ("chop"), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TerrainChopAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto vt = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (vt);
            setCardStateJson ("chop", vt.getProperty ("tcCardStateChop", juce::String()).toString());
        }
}

juce::AudioProcessorEditor* TerrainChopAudioProcessor::createEditor()
{
    return new TerrainChopAudioProcessorEditor (*this);
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TerrainChopAudioProcessor();
}
