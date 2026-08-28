#pragma once

namespace ParameterIDs
{
    constexpr char GRAIN_SIZE[]   = "GRAIN_SIZE";
    constexpr char DENSITY[]      = "DENSITY";
    constexpr char SPRAY[]        = "SPRAY";
    constexpr char PITCH[]        = "PITCH";
    constexpr char WANDER[]       = "DRIFT";  // Keep "DRIFT" string for DAW session backward compat
    constexpr char FREEZE[]       = "FREEZE";
    constexpr char GRAIN_FILTER[] = "GRAIN_FILTER";
    constexpr char LOOP_LENGTH[]   = "LOOP_LENGTH";
    constexpr char LOOP_FEEDBACK[] = "LOOP_FEEDBACK";
    constexpr char LOOP_DEGRADE[]  = "LOOP_DEGRADE";
    constexpr char LOOP_SPEED[]    = "LOOP_SPEED";
    constexpr char MIX[]          = "MIX";
    constexpr char WOW_FLUTTER[]  = "WOW_FLUTTER";  // Cassette wow/flutter
    constexpr char SATURATION[]   = "SATURATION";   // Cassette saturation
    constexpr char HISS[]         = "HISS";         // Cassette hiss
    // Wire-specific wow/sat/hiss — independent of Cassette so the two
    // machines can be tuned separately, especially when LINK is engaged.
    constexpr char WIRE_WOW[]        = "WIRE_WOW";
    constexpr char WIRE_SATURATION[] = "WIRE_SATURATION";
    constexpr char WIRE_HISS[]       = "WIRE_HISS";
    constexpr char TAPE_MACHINE[] = "TAPE_MACHINE";
    constexpr char OUTPUT_GAIN[]  = "OUTPUT_GAIN";
    constexpr char MASTER_MIX[]   = "MASTER_MIX";

    // Harmonic Sculptor (Studio machine v2.0 control surface)
    constexpr char STUDIO_SCULPT[] = "STUDIO_SCULPT";
    constexpr char STUDIO_WEAVE[]  = "STUDIO_WEAVE";
    constexpr char STUDIO_TILT[]   = "STUDIO_TILT";

    // Space reverb
    constexpr char SPACE_SIZE[]   = "SPACE_SIZE";
    constexpr char SPACE_DECAY[]  = "SPACE_DECAY";
    constexpr char SPACE_TONE[]   = "SPACE_TONE";
    constexpr char SPACE_MIX[]    = "SPACE_MIX";

    // Parametric EQ (v6) — 7 bands + HP/LP edge cuts
    constexpr char EQ_MASTER_BYPASS[] = "EQ_MASTER_BYPASS";
    constexpr char EQ_HP_FREQ[]       = "EQ_HP_FREQ";
    constexpr char EQ_HP_SLOPE[]      = "EQ_HP_SLOPE";
    constexpr char EQ_HP_BYPASS[]     = "EQ_HP_BYPASS";
    constexpr char EQ_LP_FREQ[]       = "EQ_LP_FREQ";
    constexpr char EQ_LP_SLOPE[]      = "EQ_LP_SLOPE";
    constexpr char EQ_LP_BYPASS[]     = "EQ_LP_BYPASS";

    constexpr char EQ_B1_FREQ[] = "EQ_B1_FREQ";  constexpr char EQ_B1_GAIN[] = "EQ_B1_GAIN";  constexpr char EQ_B1_Q[] = "EQ_B1_Q";  constexpr char EQ_B1_BYPASS[] = "EQ_B1_BYPASS";
    constexpr char EQ_B2_FREQ[] = "EQ_B2_FREQ";  constexpr char EQ_B2_GAIN[] = "EQ_B2_GAIN";  constexpr char EQ_B2_Q[] = "EQ_B2_Q";  constexpr char EQ_B2_BYPASS[] = "EQ_B2_BYPASS";
    constexpr char EQ_B3_FREQ[] = "EQ_B3_FREQ";  constexpr char EQ_B3_GAIN[] = "EQ_B3_GAIN";  constexpr char EQ_B3_Q[] = "EQ_B3_Q";  constexpr char EQ_B3_BYPASS[] = "EQ_B3_BYPASS";
    constexpr char EQ_B4_FREQ[] = "EQ_B4_FREQ";  constexpr char EQ_B4_GAIN[] = "EQ_B4_GAIN";  constexpr char EQ_B4_Q[] = "EQ_B4_Q";  constexpr char EQ_B4_BYPASS[] = "EQ_B4_BYPASS";
    constexpr char EQ_B5_FREQ[] = "EQ_B5_FREQ";  constexpr char EQ_B5_GAIN[] = "EQ_B5_GAIN";  constexpr char EQ_B5_Q[] = "EQ_B5_Q";  constexpr char EQ_B5_BYPASS[] = "EQ_B5_BYPASS";
    constexpr char EQ_B6_FREQ[] = "EQ_B6_FREQ";  constexpr char EQ_B6_GAIN[] = "EQ_B6_GAIN";  constexpr char EQ_B6_Q[] = "EQ_B6_Q";  constexpr char EQ_B6_BYPASS[] = "EQ_B6_BYPASS";
    constexpr char EQ_B7_FREQ[] = "EQ_B7_FREQ";  constexpr char EQ_B7_GAIN[] = "EQ_B7_GAIN";  constexpr char EQ_B7_Q[] = "EQ_B7_Q";  constexpr char EQ_B7_BYPASS[] = "EQ_B7_BYPASS";

    // Filter mode flags (UI-side hints for "band-1-acts-as-HP" / "band-7-acts-as-LP")
    // Audio is fully driven by the bell + HP/LP params above; these only restore
    // visual state on preset load / DAW project load.
    constexpr char EQ_B1_HP_MODE[] = "EQ_B1_HP_MODE";
    constexpr char EQ_B7_LP_MODE[] = "EQ_B7_LP_MODE";

    // MF-104S-style delay (v6) — 8 continuous + 5 choice
    constexpr char DLY_TIME[]      = "DLY_TIME";
    constexpr char DLY_FEEDBACK[]  = "DLY_FEEDBACK";
    constexpr char DLY_TONE[]      = "DLY_TONE";
    constexpr char DLY_CHARACTER[] = "DLY_CHARACTER";
    constexpr char DLY_MOD[]       = "DLY_MOD";
    constexpr char DLY_MOD_RATE[]  = "DLY_MOD_RATE";
    constexpr char DLY_MIX[]       = "DLY_MIX";
    constexpr char DLY_DUCK[]      = "DLY_DUCK";
    constexpr char DLY_FREEZE[]    = "DLY_FREEZE";
    constexpr char DLY_MOD_WAVE[]  = "DLY_MOD_WAVE";
    constexpr char DLY_SYNC[]      = "DLY_SYNC";
    constexpr char DLY_SYNC_DIV[]  = "DLY_SYNC_DIV";
    constexpr char DLY_PITCH[]     = "DLY_PITCH";
    constexpr char DLY_WIDTH[]     = "DLY_WIDTH";

    // Chorus (v6)
    constexpr char CHORUS_AMOUNT[]    = "CHORUS_AMOUNT";
    constexpr char CHORUS_WIDTH[]     = "CHORUS_WIDTH";
    constexpr char CHORUS_CHARACTER[] = "CHORUS_CHARACTER";

    // Sampler (Terrain Instrument additions — v0a)
    constexpr char ATTACK_MS[]        = "ATTACK_MS";
    constexpr char RELEASE_MS[]       = "RELEASE_MS";
    constexpr char ROOT_NOTE[]        = "ROOT_NOTE";
    constexpr char SLICE_MODE[]       = "SLICE_MODE";       // 0=PITCH, 1=SLICE
    constexpr char SLICE_SUB_MODE[]   = "SLICE_SUB_MODE";   // 0=CHOP (key→slice), 1=CHROMATIC (active slice pitched by key), 2=RANDOM (random no-repeat chop pitched by key)
    constexpr char SAMPLE_LOOP_MODE[] = "SAMPLE_LOOP_MODE"; // 0=ONE-SHOT, 1=FORWARD LOOP
    constexpr char CHOP_FADE_MS[]    = "CHOP_FADE_MS";     // Anti-click fade at slice start/end (0-50 ms, default 5 ms)

    // ── Synth section — Phase 1 (MPV) ────────────────────────────────────
    // See plugins/TerrainInstrument/Design/v1-syn-spec.md for the full
    // SYN_* namespace planned across Phases 1-8. Phase 1 ships only these
    // 12 params — enough to drive one PolyBLEP saw oscillator + ladder
    // filter + AMP ADSR voice.
    constexpr char SYN_OSC_A_ENGINE[]  = "SYN_OSC_A_ENGINE";   // choice 0..5 (only 0=WT/saw works in Phase 1)
    constexpr char SYN_OSC_A_OCT[]     = "SYN_OSC_A_OCT";      // int -3..+3
    constexpr char SYN_OSC_A_SEMI[]    = "SYN_OSC_A_SEMI";     // int -12..+12
    constexpr char SYN_OSC_A_CENT[]    = "SYN_OSC_A_CENT";     // float -100..+100
    constexpr char SYN_OSC_A_LEVEL[]   = "SYN_OSC_A_LEVEL";    // float 0..1
    constexpr char SYN_OSC_A_PAN[]     = "SYN_OSC_A_PAN";      // float -1..+1
    constexpr char SYN_OSC_A_ENABLE[]  = "SYN_OSC_A_ENABLE";   // bool — osc A ON/OFF (the white OSC letters); off = render fully skipped
    constexpr char SYN_OSC_A_MUTE[]    = "SYN_OSC_A_MUTE";     // bool — osc A silent
    constexpr char SYN_OSC_A_SOLO[]    = "SYN_OSC_A_SOLO";     // bool — solo osc A
    constexpr char SYN_FILTER1_CUT[]   = "SYN_FILTER1_CUT";    // float 20..20000 Hz (skewed)
    constexpr char SYN_FILTER1_RES[]   = "SYN_FILTER1_RES";    // float 0..1
    constexpr char SYN_FILTER1_KEYTRACK[] = "SYN_FILTER1_KEYTRACK";  // float 0..100 % — cutoff tracks note
    constexpr char SYN_FILTER2_KEYTRACK[] = "SYN_FILTER2_KEYTRACK";  // float 0..100 %
    constexpr char LFO1_RATE[]         = "LFO1_RATE";          // Batch 1 — synth LFO 1 free rate, 0.01..40 Hz
    constexpr char LFO1_DEPTH[]        = "LFO1_DEPTH";         // Batch 1 — L1 -> Filter 1 cutoff depth, -1..+1
    constexpr char LFO1_SHAPE[]        = "LFO1_SHAPE";         // Mod redesign — L1 shape (Sine..Random, 7 choices)
    constexpr char LFO1_SYNC[]         = "LFO1_SYNC";          // Mod redesign — L1 tempo-sync on/off (BPM vs HZ)
    constexpr char LFO1_DIV[]          = "LFO1_DIV";           // Mod redesign — L1 sync division index (1/4, 1/8, trip, dot…)
    // Mod redesign Stage 2 — LFOs 2..5 (same per-LFO param set as L1). All route to Filter 1 cutoff for now.
    constexpr char LFO2_RATE[]  = "LFO2_RATE";   constexpr char LFO2_DEPTH[] = "LFO2_DEPTH";
    constexpr char LFO2_SHAPE[] = "LFO2_SHAPE";  constexpr char LFO2_SYNC[]  = "LFO2_SYNC";   constexpr char LFO2_DIV[] = "LFO2_DIV";
    constexpr char LFO3_RATE[]  = "LFO3_RATE";   constexpr char LFO3_DEPTH[] = "LFO3_DEPTH";
    constexpr char LFO3_SHAPE[] = "LFO3_SHAPE";  constexpr char LFO3_SYNC[]  = "LFO3_SYNC";   constexpr char LFO3_DIV[] = "LFO3_DIV";
    constexpr char LFO4_RATE[]  = "LFO4_RATE";   constexpr char LFO4_DEPTH[] = "LFO4_DEPTH";
    constexpr char LFO4_SHAPE[] = "LFO4_SHAPE";  constexpr char LFO4_SYNC[]  = "LFO4_SYNC";   constexpr char LFO4_DIV[] = "LFO4_DIV";
    constexpr char LFO5_RATE[]  = "LFO5_RATE";   constexpr char LFO5_DEPTH[] = "LFO5_DEPTH";
    constexpr char LFO5_SHAPE[] = "LFO5_SHAPE";  constexpr char LFO5_SYNC[]  = "LFO5_SYNC";   constexpr char LFO5_DIV[] = "LFO5_DIV";
    // Mod matrix — LFOs 6..10 (identical per-LFO set; routed via the drag matrix).
    constexpr char LFO6_RATE[]  = "LFO6_RATE";   constexpr char LFO6_DEPTH[] = "LFO6_DEPTH";
    constexpr char LFO6_SHAPE[] = "LFO6_SHAPE";  constexpr char LFO6_SYNC[]  = "LFO6_SYNC";   constexpr char LFO6_DIV[] = "LFO6_DIV";
    constexpr char LFO7_RATE[]  = "LFO7_RATE";   constexpr char LFO7_DEPTH[] = "LFO7_DEPTH";
    constexpr char LFO7_SHAPE[] = "LFO7_SHAPE";  constexpr char LFO7_SYNC[]  = "LFO7_SYNC";   constexpr char LFO7_DIV[] = "LFO7_DIV";
    constexpr char LFO8_RATE[]  = "LFO8_RATE";   constexpr char LFO8_DEPTH[] = "LFO8_DEPTH";
    constexpr char LFO8_SHAPE[] = "LFO8_SHAPE";  constexpr char LFO8_SYNC[]  = "LFO8_SYNC";   constexpr char LFO8_DIV[] = "LFO8_DIV";
    constexpr char LFO9_RATE[]  = "LFO9_RATE";   constexpr char LFO9_DEPTH[] = "LFO9_DEPTH";
    constexpr char LFO9_SHAPE[] = "LFO9_SHAPE";  constexpr char LFO9_SYNC[]  = "LFO9_SYNC";   constexpr char LFO9_DIV[] = "LFO9_DIV";
    constexpr char LFO10_RATE[] = "LFO10_RATE";  constexpr char LFO10_DEPTH[]= "LFO10_DEPTH";
    constexpr char LFO10_SHAPE[]= "LFO10_SHAPE"; constexpr char LFO10_SYNC[] = "LFO10_SYNC";  constexpr char LFO10_DIV[]= "LFO10_DIV";
    // Mod redesign — per-LFO PHASE (slides the waveform L/R), 0..1
    constexpr char LFO1_PHASE[] = "LFO1_PHASE"; constexpr char LFO2_PHASE[] = "LFO2_PHASE"; constexpr char LFO3_PHASE[] = "LFO3_PHASE";
    constexpr char LFO4_PHASE[] = "LFO4_PHASE"; constexpr char LFO5_PHASE[] = "LFO5_PHASE";
    constexpr char LFO6_PHASE[] = "LFO6_PHASE"; constexpr char LFO7_PHASE[] = "LFO7_PHASE"; constexpr char LFO8_PHASE[] = "LFO8_PHASE";
    constexpr char LFO9_PHASE[] = "LFO9_PHASE"; constexpr char LFO10_PHASE[]= "LFO10_PHASE";
    // ── FLOW (performance engine) — mode + per-mode knobs (remember per mode) ──
    constexpr char FLOW_MODE[]     = "FLOW_MODE";        // choice: ARP, SEQ, GLITCH, DRIFT
    constexpr char FLOW_CHAIN_1[]  = "FLOW_CHAIN_1";     // fb131 — MODE CHAIN slots (click order = signal path;
    constexpr char FLOW_CHAIN_2[]  = "FLOW_CHAIN_2";     //   values mirror FLOW_MODE indices, 0 = empty slot;
    constexpr char FLOW_CHAIN_3[]  = "FLOW_CHAIN_3";     //   all-empty chain = legacy single FLOW_MODE)
    constexpr char FLOW_CHAIN_4[]  = "FLOW_CHAIN_4";
    constexpr char FLOW_ARP_LATCH[]= "FLOW_ARP_LATCH";   // bool — ARP latch (perf essential)
    constexpr char FLOW_ARP_RATE[] = "FLOW_ARP_RATE";  constexpr char FLOW_ARP_GATE[] = "FLOW_ARP_GATE";
    constexpr char FLOW_ARP_VARY[] = "FLOW_ARP_VARY";  constexpr char FLOW_ARP_TRAJ[] = "FLOW_ARP_TRAJ";
    constexpr char FLOW_ARP_MORPH[]= "FLOW_ARP_MORPH";
    constexpr char FLOW_SEQ_RATE[] = "FLOW_SEQ_RATE";  constexpr char FLOW_SEQ_GATE[] = "FLOW_SEQ_GATE";
    constexpr char FLOW_SEQ_VARY[] = "FLOW_SEQ_VARY";  constexpr char FLOW_SEQ_TRAJ[] = "FLOW_SEQ_TRAJ";
    constexpr char FLOW_SEQ_MORPH[]= "FLOW_SEQ_MORPH";
    constexpr char FLOW_GLI_RATE[] = "FLOW_GLI_RATE";  constexpr char FLOW_GLI_GATE[] = "FLOW_GLI_GATE";
    constexpr char FLOW_GLI_VARY[] = "FLOW_GLI_VARY";  constexpr char FLOW_GLI_TRAJ[] = "FLOW_GLI_TRAJ";
    constexpr char FLOW_GLI_MORPH[]= "FLOW_GLI_MORPH";
    constexpr char FLOW_DRF_RATE[] = "FLOW_DRF_RATE";  constexpr char FLOW_DRF_GATE[] = "FLOW_DRF_GATE";
    constexpr char FLOW_DRF_VARY[] = "FLOW_DRF_VARY";  constexpr char FLOW_DRF_TRAJ[] = "FLOW_DRF_TRAJ";
    constexpr char FLOW_DRF_MORPH[]= "FLOW_DRF_MORPH";
    constexpr char FLOW_CHOP_BLEND[]= "FLOW_CHOP_BLEND";  // float 0..1 — CHOP dry/wet (glass menu); default 0.60
    constexpr char FLOW_GLI_BLEND[] = "FLOW_GLI_BLEND";   // float 0..1 — GLITCH dry/wet (glass menu); default 0.60
    constexpr char FLOW_ARP_BLEND[] = "FLOW_ARP_BLEND";   // float 0..1 — ARP vs dry held-chord mix (glass menu); 1.0 = pure arp
    // ── FLOW · ARP extension card (fb105) — PLAY/MOTION scalars + 28 lane depth knobs ──
    constexpr char FLOW_ARP_DIR[]    = "FLOW_ARP_DIR";     // choice 0=Up 1=Down 2=Up-Dn 3=Random (card Direction; TRAJ>0 overrides)
    constexpr char FLOW_ARP_OCTR[]   = "FLOW_ARP_OCTR";    // choice 0..3 -> 1..4 octaves (card Octaves)
    constexpr char FLOW_ARP_SORTED[] = "FLOW_ARP_SORTED";  // bool — true=low->high, false=as-played
    constexpr char FLOW_ARP_SWING[]  = "FLOW_ARP_SWING";   constexpr char FLOW_ARP_MROLL[]  = "FLOW_ARP_MROLL";
    constexpr char FLOW_ARP_TIMBRE[] = "FLOW_ARP_TIMBRE";  constexpr char FLOW_ARP_GLIDE[]  = "FLOW_ARP_GLIDE";
    constexpr char FLOW_ARP_P_RANGE[]= "FLOW_ARP_P_RANGE"; constexpr char FLOW_ARP_P_CURVE[]= "FLOW_ARP_P_CURVE";
    constexpr char FLOW_ARP_P_QUANT[]= "FLOW_ARP_P_QUANT"; constexpr char FLOW_ARP_P_SLIDE[]= "FLOW_ARP_P_SLIDE";
    constexpr char FLOW_ARP_G_LEN[]  = "FLOW_ARP_G_LEN";   constexpr char FLOW_ARP_G_CURVE[]= "FLOW_ARP_G_CURVE";
    constexpr char FLOW_ARP_G_RAND[] = "FLOW_ARP_G_RAND";  constexpr char FLOW_ARP_G_SLIDE[]= "FLOW_ARP_G_SLIDE";
    constexpr char FLOW_ARP_V_RANGE[]= "FLOW_ARP_V_RANGE"; constexpr char FLOW_ARP_V_CURVE[]= "FLOW_ARP_V_CURVE";
    constexpr char FLOW_ARP_V_RAND[] = "FLOW_ARP_V_RAND";  constexpr char FLOW_ARP_V_FLOOR[]= "FLOW_ARP_V_FLOOR";
    constexpr char FLOW_ARP_O_RANGE[]= "FLOW_ARP_O_RANGE"; constexpr char FLOW_ARP_O_BIAS[] = "FLOW_ARP_O_BIAS";
    constexpr char FLOW_ARP_O_RAND[] = "FLOW_ARP_O_RAND";  constexpr char FLOW_ARP_O_SPREAD[]= "FLOW_ARP_O_SPREAD";
    constexpr char FLOW_ARP_R_COUNT[]= "FLOW_ARP_R_COUNT"; constexpr char FLOW_ARP_R_DECAY[]= "FLOW_ARP_R_DECAY";
    constexpr char FLOW_ARP_R_CURVE[]= "FLOW_ARP_R_CURVE"; constexpr char FLOW_ARP_R_AMT[]  = "FLOW_ARP_R_AMT";
    constexpr char FLOW_ARP_C_AMT[]  = "FLOW_ARP_C_AMT";   constexpr char FLOW_ARP_C_BIAS[] = "FLOW_ARP_C_BIAS";
    constexpr char FLOW_ARP_C_SEED[] = "FLOW_ARP_C_SEED";  constexpr char FLOW_ARP_C_DRIFT[]= "FLOW_ARP_C_DRIFT";
    constexpr char FLOW_ARP_W_DEPTH[]= "FLOW_ARP_W_DEPTH"; constexpr char FLOW_ARP_W_CURVE[]= "FLOW_ARP_W_CURVE";
    constexpr char FLOW_ARP_W_SLIDE[]= "FLOW_ARP_W_SLIDE"; constexpr char FLOW_ARP_W_RAND[] = "FLOW_ARP_W_RAND";
    // ── FLOW · CHOP extension card (fb106) — Ribbon scalars + 24 lane depth knobs ──
    constexpr char FLOW_CHOP_CATCH[]  = "FLOW_CHOP_CATCH";   // bool — Catch mode (vs Always)
    constexpr char FLOW_CHOP_SLICES[] = "FLOW_CHOP_SLICES";  // choice idx into {2,3,4,6,8,12,16}
    constexpr char FLOW_CHOP_LOOP[]   = "FLOW_CHOP_LOOP";    // choice idx into {2,4,6,8,10,12,16} cells
    constexpr char FLOW_CHOP_MODE[]   = "FLOW_CHOP_MODE";    // choice Step/Ping/Rand/Walk (TRAJ>0 overrides)
    constexpr char FLOW_CHOP_SCAN[]   = "FLOW_CHOP_SCAN";    constexpr char FLOW_CHOP_WANDER[] = "FLOW_CHOP_WANDER";
    constexpr char FLOW_CHOP_SPREAD[] = "FLOW_CHOP_SPREAD";  constexpr char FLOW_CHOP_SPEED[]  = "FLOW_CHOP_SPEED";
    constexpr char FLOW_CHOP_FREEZE[] = "FLOW_CHOP_FREEZE";  constexpr char FLOW_CHOP_COLLECT[]= "FLOW_CHOP_COLLECT";
    constexpr char FLOW_CHOP_RPTS[]   = "FLOW_CHOP_RPTS";    // choice 1..4 window repeats
    constexpr char FLOW_CHOP_STEPS[]  = "FLOW_CHOP_STEPS";   constexpr char FLOW_CHOP_DETUNE[] = "FLOW_CHOP_DETUNE";
    constexpr char FLOW_CHOP_WOW[]    = "FLOW_CHOP_WOW";     constexpr char FLOW_CHOP_SMOOTH[] = "FLOW_CHOP_SMOOTH";
    constexpr char FLOW_CHOP_FILTER[] = "FLOW_CHOP_FILTER";  // choice Off/Low/Mid/High (wet bus)
    constexpr char FLOW_CHOP_GRIT[]   = "FLOW_CHOP_GRIT";    constexpr char FLOW_CHOP_TRIM[]   = "FLOW_CHOP_TRIM";
    constexpr char FLOW_CHOP_O_SPREAD[]= "FLOW_CHOP_O_SPREAD"; constexpr char FLOW_CHOP_O_BIAS[] = "FLOW_CHOP_O_BIAS";
    constexpr char FLOW_CHOP_O_LOCK[]  = "FLOW_CHOP_O_LOCK";   constexpr char FLOW_CHOP_O_SEED[] = "FLOW_CHOP_O_SEED";
    constexpr char FLOW_CHOP_P_RANGE[] = "FLOW_CHOP_P_RANGE";  constexpr char FLOW_CHOP_P_STEPS[]= "FLOW_CHOP_P_STEPS";
    constexpr char FLOW_CHOP_P_GLIDE[] = "FLOW_CHOP_P_GLIDE";  constexpr char FLOW_CHOP_P_QUANT[]= "FLOW_CHOP_P_QUANT";
    constexpr char FLOW_CHOP_RV_ODDS[] = "FLOW_CHOP_RV_ODDS";  constexpr char FLOW_CHOP_RV_RUN[] = "FLOW_CHOP_RV_RUN";
    constexpr char FLOW_CHOP_RV_SPREAD[]="FLOW_CHOP_RV_SPREAD";constexpr char FLOW_CHOP_RV_SNAP[]= "FLOW_CHOP_RV_SNAP";
    constexpr char FLOW_CHOP_T_LEN[]   = "FLOW_CHOP_T_LEN";    constexpr char FLOW_CHOP_T_CURVE[]= "FLOW_CHOP_T_CURVE";
    constexpr char FLOW_CHOP_T_RAND[]  = "FLOW_CHOP_T_RAND";   constexpr char FLOW_CHOP_T_GATE[] = "FLOW_CHOP_T_GATE";
    constexpr char FLOW_CHOP_R_COUNT[] = "FLOW_CHOP_R_COUNT";  constexpr char FLOW_CHOP_R_DECAY[]= "FLOW_CHOP_R_DECAY";
    constexpr char FLOW_CHOP_R_CURVE[] = "FLOW_CHOP_R_CURVE";  constexpr char FLOW_CHOP_R_ODDS[] = "FLOW_CHOP_R_ODDS";
    constexpr char FLOW_CHOP_D_AMT[]   = "FLOW_CHOP_D_AMT";    constexpr char FLOW_CHOP_D_SIZE[] = "FLOW_CHOP_D_SIZE";
    constexpr char FLOW_CHOP_D_SPRAY[] = "FLOW_CHOP_D_SPRAY";  constexpr char FLOW_CHOP_D_TONE[] = "FLOW_CHOP_D_TONE";
    // ── FLOW · GLITCH extension card (fb115) — Monitor scalars + 32 per-effect knobs ──
    constexpr char FLOW_GLI_EN_REP[]  = "FLOW_GLI_EN_REP";   constexpr char FLOW_GLI_EN_REV[]  = "FLOW_GLI_EN_REV";
    constexpr char FLOW_GLI_EN_TAPE[] = "FLOW_GLI_EN_TAPE";  constexpr char FLOW_GLI_EN_GATE[] = "FLOW_GLI_EN_GATE";
    constexpr char FLOW_GLI_EN_PIT[]  = "FLOW_GLI_EN_PIT";   constexpr char FLOW_GLI_EN_CRSH[] = "FLOW_GLI_EN_CRSH";
    constexpr char FLOW_GLI_EN_FRZ[]  = "FLOW_GLI_EN_FRZ";   constexpr char FLOW_GLI_EN_SCT[]  = "FLOW_GLI_EN_SCT";
    constexpr char FLOW_GLI_DEJAVU[]  = "FLOW_GLI_DEJAVU";   constexpr char FLOW_GLI_DECAY[]   = "FLOW_GLI_DECAY";
    constexpr char FLOW_GLI_OUTMODE[] = "FLOW_GLI_OUTMODE";   constexpr char FLOW_GLI_PING[]    = "FLOW_GLI_PING";   // fb142 — Mix/Cut/Gate + per-fire stereo bounce
    constexpr char FLOW_GLI_DROP[]    = "FLOW_GLI_DROP";      constexpr char FLOW_GLI_BURST[]   = "FLOW_GLI_BURST";  // fb143 — hole fires (silence) + fires streak into clusters
    constexpr char FLOW_GLI_BEND[]    = "FLOW_GLI_BEND";     constexpr char FLOW_GLI_SEED[]    = "FLOW_GLI_SEED";    // 0 = Free, else 1..99
    constexpr char FLOW_GLI_HOLD[]    = "FLOW_GLI_HOLD";     // choice {1,2,3,4,6,8} steps
    constexpr char FLOW_GLI_LOOP[]    = "FLOW_GLI_LOOP";     // choice {2,4,8,12,16} pattern length
    constexpr char FLOW_GLI_QUANT[]   = "FLOW_GLI_QUANT";    // choice 1/4..1/32 (Roll punch-in grid)
    constexpr char FLOW_GLI_RELEASE[] = "FLOW_GLI_RELEASE";  // choice End/Now
    constexpr char FLOW_GLI_FILTER[]  = "FLOW_GLI_FILTER";   // choice Off/Low/Mid/High (wet bus)
    constexpr char FLOW_GLI_PAN[]     = "FLOW_GLI_PAN";      // choice L/C/R (wet bus)
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
    // fb125 — per-EFFECT Out routing (the Bus is dead): each effect's own Filter + Pan
    constexpr char FLOW_GLI_REP_FLT[]  = "FLOW_GLI_REP_FLT";   constexpr char FLOW_GLI_REP_PAN[]  = "FLOW_GLI_REP_PAN";
    constexpr char FLOW_GLI_REV_FLT[]  = "FLOW_GLI_REV_FLT";   constexpr char FLOW_GLI_REV_PAN[]  = "FLOW_GLI_REV_PAN";
    constexpr char FLOW_GLI_TAPE_FLT[] = "FLOW_GLI_TAPE_FLT";  constexpr char FLOW_GLI_TAPE_PAN[] = "FLOW_GLI_TAPE_PAN";
    constexpr char FLOW_GLI_GATE_FLT[] = "FLOW_GLI_GATE_FLT";  constexpr char FLOW_GLI_GATE_PAN[] = "FLOW_GLI_GATE_PAN";
    constexpr char FLOW_GLI_PIT_FLT[]  = "FLOW_GLI_PIT_FLT";   constexpr char FLOW_GLI_PIT_PAN[]  = "FLOW_GLI_PIT_PAN";
    constexpr char FLOW_GLI_CRSH_FLT[] = "FLOW_GLI_CRSH_FLT";  constexpr char FLOW_GLI_CRSH_PAN[] = "FLOW_GLI_CRSH_PAN";
    constexpr char FLOW_GLI_FRZ_FLT[]  = "FLOW_GLI_FRZ_FLT";   constexpr char FLOW_GLI_FRZ_PAN[]  = "FLOW_GLI_FRZ_PAN";
    constexpr char FLOW_GLI_SCT_FLT[]  = "FLOW_GLI_SCT_FLT";   constexpr char FLOW_GLI_SCT_PAN[]  = "FLOW_GLI_SCT_PAN";
    // ── FLOW · ROBIN extension card (fb122) — the Wheel: rotation brain scalars ──
    constexpr char FLOW_RBN_A[]      = "FLOW_RBN_A";       constexpr char FLOW_RBN_B[]      = "FLOW_RBN_B";
    constexpr char FLOW_RBN_C[]      = "FLOW_RBN_C";       constexpr char FLOW_RBN_D[]      = "FLOW_RBN_D";
    constexpr char FLOW_RBN_AFIRST[] = "FLOW_RBN_AFIRST";  constexpr char FLOW_RBN_RETRIG[] = "FLOW_RBN_RETRIG";
    constexpr char FLOW_RBN_MODE[]   = "FLOW_RBN_MODE";    // choice Cycle/Shuffle/Random/Pong
    constexpr char FLOW_RBN_LEGATO[] = "FLOW_RBN_LEGATO";  // choice Keep/New
    constexpr char FLOW_RBN_STEAL[]  = "FLOW_RBN_STEAL";   // choice Follow/Stay
    constexpr char FLOW_RBN_RELEASE[]= "FLOW_RBN_RELEASE"; // choice Hold/Free (default Free)
    constexpr char FLOW_RBN_TIMES[]  = "FLOW_RBN_TIMES";   // choice 1..4 notes per station
    constexpr char FLOW_RBN_RESET[]  = "FLOW_RBN_RESET";   // choice Free/Bar/Phrase
    constexpr char FLOW_RBN_RUN[]    = "FLOW_RBN_RUN";     // choice Forward/Backward
    constexpr char FLOW_RBN_O1[]     = "FLOW_RBN_O1";      constexpr char FLOW_RBN_O2[]     = "FLOW_RBN_O2";
    constexpr char FLOW_RBN_O3[]     = "FLOW_RBN_O3";      constexpr char FLOW_RBN_O4[]     = "FLOW_RBN_O4";
    constexpr char FLOW_RBN_VARY[]   = "FLOW_RBN_VARY";    constexpr char FLOW_RBN_DRIFT[]  = "FLOW_RBN_DRIFT";
    constexpr char FLOW_RBN_WOBBLE[] = "FLOW_RBN_WOBBLE";  constexpr char FLOW_RBN_LVL[]    = "FLOW_RBN_LVL";
    constexpr char FLOW_RBN_PAN[]    = "FLOW_RBN_PAN";     constexpr char FLOW_RBN_AFTER[]  = "FLOW_RBN_AFTER";
    constexpr char FLOW_RBN_GLIDE[]  = "FLOW_RBN_GLIDE";   constexpr char FLOW_RBN_OVERLAP[]= "FLOW_RBN_OVERLAP";
    constexpr char FLOW_RBN_FADE[]   = "FLOW_RBN_FADE";
    // ── ANNULUS resonator — global key-tracked physical-modeling node (ResonatorNode.h) ──
    constexpr char SYN_RESO_STRUCTURE[]  = "SYN_RESO_STRUCTURE";   // float 0..1 — harmonic↔material morph
    constexpr char SYN_RESO_BRIGHTNESS[] = "SYN_RESO_BRIGHTNESS";  // float 0..1 — mode count + spectral tilt
    constexpr char SYN_RESO_DAMPING[]    = "SYN_RESO_DAMPING";     // float 0..1 — ring length (0=long, 1=dead)
    constexpr char SYN_RESO_POSITION[]   = "SYN_RESO_POSITION";    // float 0..1 — pluck-point comb
    constexpr char SYN_RESO_MIX[]        = "SYN_RESO_MIX";         // float 0..1 — dry→fully resonated; default 0 (bypassed)
    constexpr char SYN_RESO_KEYTRACK[]   = "SYN_RESO_KEYTRACK";    // float 0..1 — fundamental tracks played note; default 1 (pitched)
    constexpr char SYN_RESO_MATERIAL[]   = "SYN_RESO_MATERIAL";    // choice: String/Bar/Drum/Metal
    constexpr char SYN_ENV_AMP_A[]     = "SYN_ENV_AMP_A";      // float ms (skewed)
    constexpr char SYN_ENV_AMP_D[]     = "SYN_ENV_AMP_D";      // float ms (skewed)
    constexpr char SYN_ENV_AMP_S[]     = "SYN_ENV_AMP_S";      // float 0..1
    constexpr char SYN_ENV_AMP_R[]     = "SYN_ENV_AMP_R";      // float ms (skewed)

    // ── Synth section — Batch 1 Filter (Phase 12+) ───────────────────────
    // SYN_FILTER_SLOT: which slot the UI is editing (0/1). Batch 1 shows
    //   slot 0 only; slot 1 is reserved namespace, inert this batch.
    // SYN_FILTER{1,2}_TYPE: enum 0..26 — see TerrainFilters.h Type enum.
    //   NONE = 26, rendered FIRST in dropdown but stored as last index so
    //   later additions don't reshuffle the enum.
    // SYN_FILTER{1,2}_DRV: 0..1 → 0..+24 dB into nonlinearity; output
    //   makeup gain = drive^-0.5 (~−3 dB per +6 dB drive). §4 of prompt.
    // SYN_FILTER{1,2}_ENV: BIPOLAR -1..+1, signed amount of the dedicated
    //   FLT envelope applied to cutoff in semitone space (±96 ST at ±1).
    constexpr char SYN_FILTER_SLOT[]   = "SYN_FILTER_SLOT";    // int 0..1
    constexpr char SYN_FILTER1_TYPE[]  = "SYN_FILTER1_TYPE";   // choice 0..26 (active: 0=Ladder LP·24, 4=Acid 303, 5=SVF LP, 26=NONE)
    constexpr char SYN_FILTER1_DRV[]   = "SYN_FILTER1_DRV";    // float 0..1
    constexpr char SYN_FILTER1_ENV[]   = "SYN_FILTER1_ENV";    // float -1..+1 (bipolar)
    constexpr char SYN_FILTER2_TYPE[]  = "SYN_FILTER2_TYPE";   // RESERVED (inert this batch)
    constexpr char SYN_FILTER2_CUT[]   = "SYN_FILTER2_CUT";    // RESERVED
    constexpr char SYN_FILTER2_RES[]   = "SYN_FILTER2_RES";    // RESERVED
    constexpr char SYN_FILTER2_DRV[]   = "SYN_FILTER2_DRV";    // RESERVED
    constexpr char SYN_FILTER2_ENV[]   = "SYN_FILTER2_ENV";    // RESERVED
    // Back-panel per-filter knobs: VEL = velocity→cutoff depth (0..1), PDRV = post-filter output
    // drive/saturation (0..1). (Env=SYN_FILTER*_ENV, Track=SYN_FILTER*_KEYTRACK, Mix=SYN_FILTER*_MIX.)
    constexpr char SYN_FILTER1_VEL[]   = "SYN_FILTER1_VEL";
    constexpr char SYN_FILTER2_VEL[]   = "SYN_FILTER2_VEL";
    constexpr char SYN_FILTER1_PDRV[]  = "SYN_FILTER1_PDRV";
    constexpr char SYN_FILTER2_PDRV[]  = "SYN_FILTER2_PDRV";
    // DRIVETYPE = post-filter drive waveshaper flavor (choice 0..5: Tube/Diode/Fold/Hard/Crush/Fuzz).
    constexpr char SYN_FILTER1_DRIVETYPE[] = "SYN_FILTER1_DRIVETYPE";
    constexpr char SYN_FILTER2_DRIVETYPE[] = "SYN_FILTER2_DRIVETYPE";
    // POLES = ladder slope (true = 24 dB/4-pole, false = 12 dB/2-pole tap).
    constexpr char SYN_FILTER1_POLES[] = "SYN_FILTER1_POLES";
    constexpr char SYN_FILTER2_POLES[] = "SYN_FILTER2_POLES";
    // SPREAD = filter stereo width (L/R cutoff offset, 0..1).
    constexpr char SYN_FILTER1_SPREAD[] = "SYN_FILTER1_SPREAD";
    constexpr char SYN_FILTER2_SPREAD[] = "SYN_FILTER2_SPREAD";
    // Per-filter wet/dry mix + routing between the two slots (independent filters).
    constexpr char SYN_FILTER1_MIX[]    = "SYN_FILTER1_MIX";    // float 0..1 (1 = fully filtered)
    constexpr char SYN_FILTER2_MIX[]    = "SYN_FILTER2_MIX";    // float 0..1
    constexpr char SYN_FILTER_ROUTING[] = "SYN_FILTER_ROUTING"; // choice 0=SERIES, 1=PARALLEL
    // Per-oscillator filter ROUTING masks — which sources feed each filter (bool, default ON = all
    // routed, so a fresh patch = today's behaviour). Order: A, B, C, D, Sub. Noise (N) reserved.
    constexpr char SYN_FILTER1_SRC_A[]  = "SYN_FILTER1_SRC_A";
    // fb79 — PER-OSC continuous filter sends (replace the binary A-D masks; each osc owns its own
    // F1/F2 amount, DEFAULT 0 = dry — Max: "nothing routed until we decide"). Sub/Noise stay binary.
    constexpr char SYN_OSC_A_F1MIX[]    = "SYN_OSC_A_F1MIX";    // float 0..1, default 0
    constexpr char SYN_OSC_B_F1MIX[]    = "SYN_OSC_B_F1MIX";
    constexpr char SYN_OSC_C_F1MIX[]    = "SYN_OSC_C_F1MIX";
    constexpr char SYN_OSC_D_F1MIX[]    = "SYN_OSC_D_F1MIX";
    constexpr char SYN_OSC_A_F2MIX[]    = "SYN_OSC_A_F2MIX";
    constexpr char SYN_OSC_B_F2MIX[]    = "SYN_OSC_B_F2MIX";
    constexpr char SYN_OSC_C_F2MIX[]    = "SYN_OSC_C_F2MIX";
    constexpr char SYN_OSC_D_F2MIX[]    = "SYN_OSC_D_F2MIX";
    constexpr char SYN_FILTER1_SRC_B[]  = "SYN_FILTER1_SRC_B";
    constexpr char SYN_FILTER1_SRC_C[]  = "SYN_FILTER1_SRC_C";
    constexpr char SYN_FILTER1_SRC_D[]  = "SYN_FILTER1_SRC_D";
    constexpr char SYN_FILTER1_SRC_SUB[]= "SYN_FILTER1_SRC_SUB";
    constexpr char SYN_FILTER2_SRC_A[]  = "SYN_FILTER2_SRC_A";
    constexpr char SYN_FILTER2_SRC_B[]  = "SYN_FILTER2_SRC_B";
    constexpr char SYN_FILTER2_SRC_C[]  = "SYN_FILTER2_SRC_C";
    constexpr char SYN_FILTER2_SRC_D[]  = "SYN_FILTER2_SRC_D";
    constexpr char SYN_FILTER2_SRC_SUB[]= "SYN_FILTER2_SRC_SUB";
    constexpr char SYN_FILTER1_SRC_NOISE[] = "SYN_FILTER1_SRC_NOISE";   // fb63 — route the Noise layer into Filter 1
    constexpr char SYN_FILTER2_SRC_NOISE[] = "SYN_FILTER2_SRC_NOISE";   // fb63 — route the Noise layer into Filter 2

    // ════════ FX RACK · REVERB (Hall first — fb276) ════════════════════════════════════════
    // setSynParam-only (no relays); choices read as INDEX. Per-osc route bits default OFF (no bleed).
    constexpr char SYN_RVB_TYPE[]      = "SYN_RVB_TYPE";       // choice: Hall..Convolution (9)
    constexpr char SYN_RVB_CHARACTER[] = "SYN_RVB_CHARACTER";  // choice: per-type voicing
    constexpr char SYN_RVB_MODMODE[]   = "SYN_RVB_MODMODE";    // choice: Off..Chaos
    constexpr char SYN_RVB_SIZE[]      = "SYN_RVB_SIZE";       // float 0..1 (front)
    constexpr char SYN_RVB_DECAY[]     = "SYN_RVB_DECAY";      // float 0..1 (front) -> RT60 0.3..20 s
    constexpr char SYN_RVB_TONE[]      = "SYN_RVB_TONE";       // float 0..1 (front) dark<->bright
    constexpr char SYN_RVB_MIX[]       = "SYN_RVB_MIX";        // float 0..1 (front) equal-power
    constexpr char SYN_RVB_PREDELAY[]  = "SYN_RVB_PREDELAY";   // float 0..1 -> 0..250 ms
    constexpr char SYN_RVB_DIFFUSE[]   = "SYN_RVB_DIFFUSE";    // float 0..1
    constexpr char SYN_RVB_MODDEPTH[]  = "SYN_RVB_MODDEPTH";   // float 0..1
    constexpr char SYN_RVB_MODRATE[]   = "SYN_RVB_MODRATE";    // float 0..1 -> 0.05..5 Hz
    constexpr char SYN_RVB_HIDAMP[]    = "SYN_RVB_HIDAMP";     // float 0..1 (High Damping)
    constexpr char SYN_RVB_LOWDECAY[]  = "SYN_RVB_LOWDECAY";   // float 0..1 -> 0.25..2x
    constexpr char SYN_RVB_LOWCUT[]    = "SYN_RVB_LOWCUT";     // float 0..1 -> 20..1000 Hz
    constexpr char SYN_RVB_WIDTH[]     = "SYN_RVB_WIDTH";      // float 0..1
    constexpr char SYN_RVB_SRC_A[]     = "SYN_RVB_SRC_A";      // bool, default OFF (per-osc send)
    constexpr char SYN_RVB_SRC_B[]     = "SYN_RVB_SRC_B";
    constexpr char SYN_RVB_SRC_C[]     = "SYN_RVB_SRC_C";
    constexpr char SYN_RVB_SRC_D[]     = "SYN_RVB_SRC_D";
    constexpr char SYN_RVB_SRC_SUB[]   = "SYN_RVB_SRC_SUB";
    constexpr char SYN_RVB_SRC_NOISE[] = "SYN_RVB_SRC_NOISE";
    constexpr char SYN_RVB_MOD[]       = "SYN_RVB_MOD";       // fb279 bool — front Mod toggle (modulation on/off), default ON (Hall/Digital/Plate)
    constexpr char SYN_RVB_FREEZE[]    = "SYN_RVB_FREEZE";    // fb279 bool — front Freeze toggle (infinite hold), default OFF
    constexpr char SYN_RVB_POWER[]     = "SYN_RVB_POWER";     // fb287 bool — device power (OFF = full bypass + routing disabled), default ON
    constexpr char SYN_RVB_DUCK[]      = "SYN_RVB_DUCK";      // fb287 bool — Room/Spring 2nd pill (ducking reverb), default OFF

    // ════════ FX RACK · DELAY (fb296) — parallel per-osc send, mirrors the reverb device.
    // 4 CHARACTER types (Digital/Tape/BBD/Diffuse) share one DelayEngine; Ping-Pong is a ROUTING pill.
    // 3 front params + Mix, 2 dropdowns + 8 back knobs, 6 route sources, 2 front pills, power. ════════
    constexpr char SYN_DLY_TYPE[]      = "SYN_DLY_TYPE";       // choice: Digital/Tape/BBD/Diffuse (4)
    constexpr char SYN_DLY_CHARACTER[] = "SYN_DLY_CHARACTER";  // choice: per-type sub-voicing
    constexpr char SYN_DLY_SYNCDIV[]   = "SYN_DLY_SYNCDIV";    // choice: Free/1-4/1-8/1-8T/1-8D/1-16 (time division)
    constexpr char SYN_DLY_TIME[]      = "SYN_DLY_TIME";       // float 0..1 (front) -> 1..2000 ms (or synced)
    constexpr char SYN_DLY_FEEDBACK[]  = "SYN_DLY_FEEDBACK";   // float 0..1 (front) -> 0..~110%
    constexpr char SYN_DLY_TONE[]      = "SYN_DLY_TONE";       // float 0..1 (front) bipolar tilt (0.5 neutral)
    constexpr char SYN_DLY_MIX[]       = "SYN_DLY_MIX";        // float 0..1 (front) equal-power (100% = fully wet)
    constexpr char SYN_DLY_LOWCUT[]    = "SYN_DLY_LOWCUT";     // float 0..1 -> 20..1000 Hz (in-loop HP)
    constexpr char SYN_DLY_HICUT[]     = "SYN_DLY_HICUT";      // float 0..1 -> 1.2k..18k Hz (in-loop LP)
    constexpr char SYN_DLY_SPREAD[]    = "SYN_DLY_SPREAD";     // float 0..1 -> L/R time offset
    constexpr char SYN_DLY_WIDTH[]     = "SYN_DLY_WIDTH";      // float 0..1 -> 0..1.6 M/S width
    constexpr char SYN_DLY_MODRATE[]   = "SYN_DLY_MODRATE";    // float 0..1 -> 0.05..8 Hz
    constexpr char SYN_DLY_MODDEPTH[]  = "SYN_DLY_MODDEPTH";   // float 0..1 (chorus / Diffuse smear)
    constexpr char SYN_DLY_WOW[]       = "SYN_DLY_WOW";        // float 0..1 (tape wow+flutter)
    constexpr char SYN_DLY_DUCK[]      = "SYN_DLY_DUCK";       // float 0..1 (input-ducking amount)
    constexpr char SYN_DLY_SRC_A[]     = "SYN_DLY_SRC_A";      // bool, default OFF (per-osc send)
    constexpr char SYN_DLY_SRC_B[]     = "SYN_DLY_SRC_B";
    constexpr char SYN_DLY_SRC_C[]     = "SYN_DLY_SRC_C";
    constexpr char SYN_DLY_SRC_D[]     = "SYN_DLY_SRC_D";
    constexpr char SYN_DLY_SRC_SUB[]   = "SYN_DLY_SRC_SUB";
    constexpr char SYN_DLY_SRC_NOISE[] = "SYN_DLY_SRC_NOISE";
    constexpr char SYN_DLY_SYNC[]      = "SYN_DLY_SYNC";       // bool — front Sync toggle (tempo sync on/off), default ON
    constexpr char SYN_DLY_PING[]      = "SYN_DLY_PING";       // bool — front Ping toggle (ping-pong routing), default OFF
    constexpr char SYN_DLY_POWER[]     = "SYN_DLY_POWER";      // bool — device power (OFF = full bypass + routing disabled), default ON
    constexpr char SYN_DLY_HQ[]        = "SYN_DLY_HQ";         // bool — High-Quality interpolation (cubic vs linear), default ON
    constexpr char SYN_DLY_TIME_R[]    = "SYN_DLY_TIME_R";     // fb306 — float 0..1 RIGHT time (free ms) when UNLINKED
    constexpr char SYN_DLY_SYNCDIV_R[] = "SYN_DLY_SYNCDIV_R";  // fb306 — choice: RIGHT sync division (same 20-entry list) when UNLINKED
    constexpr char SYN_DLY_LINK[]      = "SYN_DLY_LINK";       // fb306 — bool: L/R times LINKED (R follows L +Spread), default ON
    // ════════ FX RACK · DISTORTION — fb315. The 3rd FX device (the slot formerly called "Saturate").
    // ONE device, 23 modes in 6 FAMILIES; the BACK-8 is keyed to the FAMILY (6 param sets, not 23).
    // setSynParam-only; choices = INDEX; routes default OFF; POWER default OFF ⇒ byte-identical dry init.
    // Spec: Design/DISTORTION-BUILD-BIBLE.md ════════
    constexpr char SYN_DST_TYPE[]      = "SYN_DST_TYPE";       // choice(23): ANALOG(5) CLIP(4) DIODE(4) FOLD(3) SHAPER(4) DIGITAL(3)
    constexpr char SYN_DST_CHARACTER[] = "SYN_DST_CHARACTER";  // choice(8): per-MODE voicing (23 x 8 = 184). Must change PHYSICS, not EQ.
    constexpr char SYN_DST_QUALITY[]   = "SYN_DST_QUALITY";    // choice(4): Off/Standard/High/Ultra. A mode declares its FLOOR; this may only RAISE it.
    constexpr char SYN_DST_DRIVE[]     = "SYN_DST_DRIVE";      // float 0..1 -> driveDb = D_max * t^0.8 (CLIP D_max = +48 dB). NOT a linear multiplier.
    constexpr char SYN_DST_SIG[]       = "SYN_DST_SIG";        // float 0..1 — the SIGNATURE knob, relabelled per family:
                                                               //   ANALOG=Bias · CLIP=Knee · DIODE=Asym · FOLD=Symmetry · SHAPER=Morph · DIGITAL=Crush
    constexpr char SYN_DST_TONE[]      = "SYN_DST_TONE";       // float 0..1 — post tilt, 0.5 neutral
    constexpr char SYN_DST_MIX[]       = "SYN_DST_MIX";        // float 0..1 — 100% = FULLY WET (hard rule; send padded by kVoiceToFxPad)
    constexpr char SYN_DST_P1[]        = "SYN_DST_P1";         // back-8, relabelled per FAMILY (Model A). CLIP:
    constexpr char SYN_DST_P2[]        = "SYN_DST_P2";         //   P1 Low Cut(pre) · P2 Hi Cut(post) · P3 Emphasis(the pre/de PAIR)
    constexpr char SYN_DST_P3[]        = "SYN_DST_P3";         //   P4 Width(M/S DRIVE balance) · P5 Bias(+-1.0 FS) · P6 Gap(dead zone)
    constexpr char SYN_DST_P4[]        = "SYN_DST_P4";         //   P7 Punch(bipolar transient drive) · P8 Feedback(loop gain -> 0.95)
    constexpr char SYN_DST_P5[]        = "SYN_DST_P5";
    constexpr char SYN_DST_P6[]        = "SYN_DST_P6";
    constexpr char SYN_DST_P7[]        = "SYN_DST_P7";
    constexpr char SYN_DST_P8[]        = "SYN_DST_P8";
    constexpr char SYN_DST_SRC_A[]     = "SYN_DST_SRC_A";      // bool, default OFF (per-osc send; no pills lit = MAIN SEND)
    constexpr char SYN_DST_SRC_B[]     = "SYN_DST_SRC_B";
    constexpr char SYN_DST_SRC_C[]     = "SYN_DST_SRC_C";
    constexpr char SYN_DST_SRC_D[]     = "SYN_DST_SRC_D";
    constexpr char SYN_DST_SRC_SUB[]   = "SYN_DST_SRC_SUB";
    constexpr char SYN_DST_SRC_NOISE[] = "SYN_DST_SRC_NOISE";
    constexpr char SYN_DST_POWER[]     = "SYN_DST_POWER";      // bool — device power. DEFAULT OFF (dry init; on = main send). Gates routing too.
    constexpr char SYN_DST_AUTO[]      = "SYN_DST_AUTO";       // bool — output compensation. DEFAULT OFF: full normalisation is THE timidity culprit
                                                               // (Serum/Vital ship none). When ON it compensates only ~70% on a ~300 ms RMS tracker.
    constexpr char SYN_DST_PILL2[]     = "SYN_DST_PILL2";      // bool — the FAMILY-UNIQUE 2nd pill, default OFF:
                                                               //   ANALOG=Slam · CLIP=Wrap · DIODE=Octave · FOLD=Track · SHAPER=Sym · DIGITAL=Clean

    // ════════ FX RACK · CHAIN ORDER (fb307 — drag-to-reorder) ════════
    constexpr char SYN_FX_ORDER[]      = "SYN_FX_ORDER";       // fb307 — bool: false = Reverb→Delay (default serial order), true = Delay→Reverb
                                                               // ⚠️ fb315 — with a THIRD device this must become a 6-way permutation index
                                                               // (3 devices = 6 orders). Deferred until the distortion joins the drag chain;
                                                               // today it is inserted at a FIXED position (last). See bible §4.5.

    // Filter ADSR (the envelope that the bipolar ENV knob scales into cutoff)
    constexpr char SYN_ENV_FLT_A[]     = "SYN_ENV_FLT_A";      // float ms (skewed)
    constexpr char SYN_ENV_FLT_D[]     = "SYN_ENV_FLT_D";      // float ms (skewed)
    constexpr char SYN_ENV_FLT_S[]     = "SYN_ENV_FLT_S";      // float 0..1
    constexpr char SYN_ENV_FLT_R[]     = "SYN_ENV_FLT_R";      // float ms (skewed)

    // ── Envelope DAHDSR extensions (Batch 2/3): delay, hold, per-segment curves, loop ──
    constexpr char SYN_ENV_AMP_DLY[]   = "SYN_ENV_AMP_DLY";  // float ms (skewed)
    constexpr char SYN_ENV_AMP_H[]     = "SYN_ENV_AMP_H";   // float ms (skewed)
    constexpr char SYN_ENV_AMP_CA[]    = "SYN_ENV_AMP_CA";   // float -1..+1
    constexpr char SYN_ENV_AMP_CD[]    = "SYN_ENV_AMP_CD";   // float -1..+1
    constexpr char SYN_ENV_AMP_CR[]    = "SYN_ENV_AMP_CR";   // float -1..+1
    constexpr char SYN_ENV_AMP_LOOP[]  = "SYN_ENV_AMP_LOOP";  // bool
    constexpr char SYN_ENV_FLT_DLY[]   = "SYN_ENV_FLT_DLY";  // float ms (skewed)
    constexpr char SYN_ENV_FLT_H[]     = "SYN_ENV_FLT_H";   // float ms (skewed)
    constexpr char SYN_ENV_FLT_CA[]    = "SYN_ENV_FLT_CA";   // float -1..+1
    constexpr char SYN_ENV_FLT_CD[]    = "SYN_ENV_FLT_CD";   // float -1..+1
    constexpr char SYN_ENV_FLT_CR[]    = "SYN_ENV_FLT_CR";   // float -1..+1
    constexpr char SYN_ENV_FLT_LOOP[]  = "SYN_ENV_FLT_LOOP";  // bool
    // PITCH / MOD1 / MOD2 envelopes — full DAHDSR sets
    constexpr char SYN_ENV_PIT_DLY[]   = "SYN_ENV_PIT_DLY";  // float ms (skewed)
    constexpr char SYN_ENV_PIT_A[]     = "SYN_ENV_PIT_A";   // float ms (skewed)
    constexpr char SYN_ENV_PIT_H[]     = "SYN_ENV_PIT_H";   // float ms (skewed)
    constexpr char SYN_ENV_PIT_D[]     = "SYN_ENV_PIT_D";   // float ms (skewed)
    constexpr char SYN_ENV_PIT_S[]     = "SYN_ENV_PIT_S";   // float 0..1
    constexpr char SYN_ENV_PIT_R[]     = "SYN_ENV_PIT_R";   // float ms (skewed)
    constexpr char SYN_ENV_PIT_CA[]    = "SYN_ENV_PIT_CA";   // float -1..+1
    constexpr char SYN_ENV_PIT_CD[]    = "SYN_ENV_PIT_CD";   // float -1..+1
    constexpr char SYN_ENV_PIT_CR[]    = "SYN_ENV_PIT_CR";   // float -1..+1
    constexpr char SYN_ENV_PIT_DEPTH[] = "SYN_ENV_PIT_DEPTH";  // float -48..+48 semitones
    constexpr char SYN_ENV_PIT_LOOP[]  = "SYN_ENV_PIT_LOOP";  // bool
    constexpr char SYN_ENV_M1_DLY[]    = "SYN_ENV_M1_DLY";   // float ms (skewed)
    constexpr char SYN_ENV_M1_A[]      = "SYN_ENV_M1_A";   // float ms (skewed)
    constexpr char SYN_ENV_M1_H[]      = "SYN_ENV_M1_H";   // float ms (skewed)
    constexpr char SYN_ENV_M1_D[]      = "SYN_ENV_M1_D";   // float ms (skewed)
    constexpr char SYN_ENV_M1_S[]      = "SYN_ENV_M1_S";   // float 0..1
    constexpr char SYN_ENV_M1_R[]      = "SYN_ENV_M1_R";   // float ms (skewed)
    constexpr char SYN_ENV_M1_CA[]     = "SYN_ENV_M1_CA";   // float -1..+1
    constexpr char SYN_ENV_M1_CD[]     = "SYN_ENV_M1_CD";   // float -1..+1
    constexpr char SYN_ENV_M1_CR[]     = "SYN_ENV_M1_CR";   // float -1..+1
    constexpr char SYN_ENV_M1_LOOP[]   = "SYN_ENV_M1_LOOP";  // bool
    constexpr char SYN_ENV_M2_DLY[]    = "SYN_ENV_M2_DLY";   // float ms (skewed)
    constexpr char SYN_ENV_M2_A[]      = "SYN_ENV_M2_A";   // float ms (skewed)
    constexpr char SYN_ENV_M2_H[]      = "SYN_ENV_M2_H";   // float ms (skewed)
    constexpr char SYN_ENV_M2_D[]      = "SYN_ENV_M2_D";   // float ms (skewed)
    constexpr char SYN_ENV_M2_S[]      = "SYN_ENV_M2_S";   // float 0..1
    constexpr char SYN_ENV_M2_R[]      = "SYN_ENV_M2_R";   // float ms (skewed)
    constexpr char SYN_ENV_M2_CA[]     = "SYN_ENV_M2_CA";   // float -1..+1
    constexpr char SYN_ENV_M2_CD[]     = "SYN_ENV_M2_CD";   // float -1..+1
    constexpr char SYN_ENV_M2_CR[]     = "SYN_ENV_M2_CR";   // float -1..+1
    constexpr char SYN_ENV_M2_LOOP[]   = "SYN_ENV_M2_LOOP";  // bool

    // ── Per-envelope ROUTING (the "mini mod-matrix per envelope") ─────────────
    //   Envelope 1 = AMP and is HARDWIRED to amplitude (no routing — it's what
    //   makes a note sound). Envelopes 2–5 are FREE: each has ONE destination +
    //   a bipolar DEPTH (-1..+1), accumulated into the destination's effective
    //   value at the per-sample base->effective site (same pattern KEYTRACK/ROUTE
    //   use). This is the on-ramp to the master mod matrix — when it lands, every
    //   source/dest reuses this exact accumulation.
    //
    //   Envelope→generator map:  2=FLT  3=PITCH  4=MOD1  5=MOD2 (see SynthVoice).
    //   DEST choice index (matches SynthVoice::EnvDest + the WebUI menu order):
    //     0=Off 1=Amp 2=Filter 1 3=Filter 2 4=Filter 1+2 5=Mod 1 6=Mod 2 7=Pitch
    //   DEPTH is normalized -1..+1 and SCALED per-destination at apply time:
    //     Filter* → ±96 ST · Pitch → ±48 ST · Amp → ±100% gain · Mod* → ±100% bus.
    constexpr char SYN_ENV2_DEST[]     = "SYN_ENV2_DEST";   // choice 0..7  (default 2 = Filter 1)
    constexpr char SYN_ENV2_DEPTH[]    = "SYN_ENV2_DEPTH";  // float -1..+1 (default 0)
    constexpr char SYN_ENV3_DEST[]     = "SYN_ENV3_DEST";   // choice 0..7  (default 7 = Pitch)
    constexpr char SYN_ENV3_DEPTH[]    = "SYN_ENV3_DEPTH";  // float -1..+1 (default 0)
    constexpr char SYN_ENV4_DEST[]     = "SYN_ENV4_DEST";   // choice 0..7  (default 0 = Off)
    constexpr char SYN_ENV4_DEPTH[]    = "SYN_ENV4_DEPTH";  // float -1..+1 (default 0)
    constexpr char SYN_ENV5_DEST[]     = "SYN_ENV5_DEST";   // choice 0..7  (default 0 = Off)
    constexpr char SYN_ENV5_DEPTH[]    = "SYN_ENV5_DEPTH";  // float -1..+1 (default 0)

    // ── Synth section — Phase 2A (Wavetable foundation) ──────────────────
    constexpr char SYN_OSC_A_WT_PRESET[] = "SYN_OSC_A_WT_PRESET"; // choice 0..5 (Prophet/Jupiter/Moog/OB-X/CS-80/Juno)
    constexpr char SYN_OSC_A_WT_FRAME[]  = "SYN_OSC_A_WT_FRAME";  // float 0..1 frame position within wavetable

    // ── Synth section — Phase 2C (Warp modes: BEND / SYNC / FORMANT) ─────
    constexpr char SYN_OSC_A_WARP_MODE[]   = "SYN_OSC_A_WARP_MODE";   // choice(48) FROZEN — 0-10 shipped, 11-34 shapers, 35-47 reserved
    constexpr char SYN_OSC_A_WARP_AMOUNT[] = "SYN_OSC_A_WARP_AMOUNT"; // float 0..1
    constexpr char SYN_OSC_A_WARP2_MODE[]  = "SYN_OSC_A_WARP2_MODE";  // choice(48) FROZEN, chained slot 2 — same list as slot 1
    constexpr char SYN_OSC_A_WARP2_AMT[]   = "SYN_OSC_A_WARP2_AMT";   // float 0..1

    // ── Synth section — Phase 9 (OSC B chassis — full mirror of OSC A) ───
    constexpr char SYN_OSC_B_ENGINE[]      = "SYN_OSC_B_ENGINE";       // enum 0..5
    constexpr char SYN_OSC_B_OCT[]         = "SYN_OSC_B_OCT";          // int -3..+3
    constexpr char SYN_OSC_B_SEMI[]        = "SYN_OSC_B_SEMI";         // int -12..+12
    constexpr char SYN_OSC_B_CENT[]        = "SYN_OSC_B_CENT";         // float -100..+100
    constexpr char SYN_OSC_B_LEVEL[]       = "SYN_OSC_B_LEVEL";        // float 0..1
    constexpr char SYN_OSC_B_PAN[]         = "SYN_OSC_B_PAN";          // float -1..+1
    constexpr char SYN_OSC_B_ENABLE[]  = "SYN_OSC_B_ENABLE";   // bool — osc B ON/OFF (the white OSC letters); off = render fully skipped
    constexpr char SYN_OSC_B_MUTE[]        = "SYN_OSC_B_MUTE";         // bool — osc B silent
    constexpr char SYN_OSC_B_SOLO[]        = "SYN_OSC_B_SOLO";         // bool — solo osc B
    constexpr char SYN_OSC_B_WT_PRESET[]   = "SYN_OSC_B_WT_PRESET";    // choice 0..19
    constexpr char SYN_OSC_B_WT_FRAME[]    = "SYN_OSC_B_WT_FRAME";     // float 0..1
    constexpr char SYN_OSC_B_WARP_MODE[]   = "SYN_OSC_B_WARP_MODE";    // choice(48) FROZEN — see SYN_OSC_A_WARP_MODE
    constexpr char SYN_OSC_B_WARP_AMOUNT[] = "SYN_OSC_B_WARP_AMOUNT";  // float 0..1
    constexpr char SYN_OSC_B_WARP2_MODE[]  = "SYN_OSC_B_WARP2_MODE";   // choice(48) FROZEN, chained slot 2
    constexpr char SYN_OSC_B_WARP2_AMT[]   = "SYN_OSC_B_WARP2_AMT";    // float 0..1

    // ── Synth section — Phase 8a (Voice settings + flagship features) ────
    constexpr char SYN_VOICES[]   = "SYN_VOICES";    // int 1..16, polyphony cap (display-only this phase)
    constexpr char SYN_VEL_DEPTH[] = "SYN_VEL_DEPTH"; // fb260 — float 0..100 → velocity→amp depth (100=full dynamics, 0=flat)
    constexpr char SYN_UNISON[]   = "SYN_UNISON";    // int 1..8, voices stacked per note
    constexpr char SYN_SPREAD[]   = "SYN_SPREAD";    // float 0..100, % detune+pan width for unison stack
    constexpr char SYN_EROSION[]  = "SYN_EROSION";   // float 0..100, % analog per-voice drift amount
    constexpr char SYN_HORIZON[]  = "SYN_HORIZON";   // float -100..+100, keyboard-tracked timbre tilt
    // VOICING / PORTAMENTO (bottom-right panel)
    constexpr char SYN_PORTA[]        = "SYN_PORTA";         // float 0..100 % → glide time
    constexpr char SYN_GLIDE_CURVE[]  = "SYN_GLIDE_CURVE";   // float 0..100 % → slide shape (50=linear)
    constexpr char SYN_GLIDE_ALWAYS[] = "SYN_GLIDE_ALWAYS";  // bool — glide every note vs only when held
    constexpr char SYN_GLIDE_SCALED[] = "SYN_GLIDE_SCALED";  // bool — time-per-octave vs fixed total time
    constexpr char SYN_MONO[]         = "SYN_MONO";          // bool — single-voice, last-note priority
    constexpr char SYN_LEGATO[]       = "SYN_LEGATO";        // bool — overlapped notes retarget, no retrigger (mono)

    // ── Synth section — Phase 11a (wavetable front-panel rework foundation) ──
    // 6 new params per OSC × 2 OSCs = 12 new params total.
    // SPECTRAL_AMT, FOLD_AMT, FRAME_SPREAD are floats 0..1 (default 0).
    // SPECTRAL_TYPE, FOLD_SHAPE, INTERP_MODE are int choices with 1 option each
    // in Phase 11a ("NONE" / "LINEAR"). More options added in Phase 11c/d.
    // Only FRAME_SPREAD has audible DSP this phase — the others are persisted
    // placeholders so V1 presets stay forward-compatible.
    constexpr char SYN_OSC_A_SPECTRAL_TYPE[] = "SYN_OSC_A_SPECTRAL_TYPE";  // choice {0=NONE}
    constexpr char SYN_OSC_A_SPECTRAL_AMT[]  = "SYN_OSC_A_SPECTRAL_AMT";   // float 0..1
    // fb467 — the spectral morph's PARTIAL WINDOW. Lo/Hi are harmonic indices (1..512,
    // log-mapped, smoothstep edges). Defaults 1 and 512 = the whole spectrum = exactly the
    // behaviour before fb467, so no saved patch moves.
    constexpr char SYN_OSC_A_SPECTRAL_LO[]  = "SYN_OSC_A_SPECTRAL_LO";   // float 1..512 (log)
    constexpr char SYN_OSC_A_SPECTRAL_HI[]  = "SYN_OSC_A_SPECTRAL_HI";   // float 1..512 (log)
    constexpr char SYN_OSC_A_FOLD_SHAPE[]    = "SYN_OSC_A_FOLD_SHAPE";     // choice {0=LINEAR}
    constexpr char SYN_OSC_A_FOLD_AMT[]      = "SYN_OSC_A_FOLD_AMT";       // float 0..1
    constexpr char SYN_OSC_A_FRAME_SPREAD[]  = "SYN_OSC_A_FRAME_SPREAD";   // float 0..1 (per-sine WT frame spread; real DSP)
    constexpr char SYN_OSC_A_INTERP_MODE[]   = "SYN_OSC_A_INTERP_MODE";    // choice {0=LINEAR}
    constexpr char SYN_OSC_A_PHASE_MODE[]    = "SYN_OSC_A_PHASE_MODE";     // choice 0=RETRIG,1=FREE,2=RANDOM,3=SPREAD
    constexpr char SYN_OSC_A_WAVER[]         = "SYN_OSC_A_WAVER";          // float 0..100 % analog pitch-drift depth (OU)
    constexpr char SYN_OSC_A_KEYTRACK[]      = "SYN_OSC_A_KEYTRACK";       // float 0..100 % note->destination depth
    constexpr char SYN_OSC_A_KEYTRACK_DEST[] = "SYN_OSC_A_KEYTRACK_DEST";  // choice 0=FRAME,1=WARP,2=FOLD
    constexpr char SYN_OSC_A_ROUTE_SRC[]     = "SYN_OSC_A_ROUTE_SRC";      // choice 0=NOTE,1=VEL
    constexpr char SYN_OSC_A_ROUTE_DEST[]    = "SYN_OSC_A_ROUTE_DEST";     // choice 0=FRAME,1=WARP,2=FOLD,3=CUT1,4=CUT2
    constexpr char SYN_OSC_A_ROUTE_AMT[]     = "SYN_OSC_A_ROUTE_AMT";      // float -100..100 % bipolar amount
    constexpr char SYN_OSC_A_UNISON[]        = "SYN_OSC_A_UNISON";         // int 1..16 voices (per-OSC)
    constexpr char SYN_OSC_A_UDETUNE[]       = "SYN_OSC_A_UDETUNE";        // float 0..100 % pitch fan
    constexpr char SYN_OSC_A_UBLEND[]        = "SYN_OSC_A_UBLEND";         // float 0..100 % centre-vs-outer balance
    constexpr char SYN_OSC_A_UWIDTH[]        = "SYN_OSC_A_UWIDTH";         // fb522 — float -100..+100 % stereo spread, default +50 (WAS 0..100/+50; the denormalised value is unchanged, only the normalised half it sits in)

    constexpr char SYN_OSC_B_SPECTRAL_TYPE[] = "SYN_OSC_B_SPECTRAL_TYPE";  // choice {0=NONE}
    constexpr char SYN_OSC_B_SPECTRAL_AMT[]  = "SYN_OSC_B_SPECTRAL_AMT";   // float 0..1
    // fb467 — the spectral morph's PARTIAL WINDOW. Lo/Hi are harmonic indices (1..512,
    // log-mapped, smoothstep edges). Defaults 1 and 512 = the whole spectrum = exactly the
    // behaviour before fb467, so no saved patch moves.
    constexpr char SYN_OSC_B_SPECTRAL_LO[]  = "SYN_OSC_B_SPECTRAL_LO";   // float 1..512 (log)
    constexpr char SYN_OSC_B_SPECTRAL_HI[]  = "SYN_OSC_B_SPECTRAL_HI";   // float 1..512 (log)
    constexpr char SYN_OSC_B_FOLD_SHAPE[]    = "SYN_OSC_B_FOLD_SHAPE";     // choice {0=LINEAR}
    constexpr char SYN_OSC_B_FOLD_AMT[]      = "SYN_OSC_B_FOLD_AMT";       // float 0..1
    constexpr char SYN_OSC_B_FRAME_SPREAD[]  = "SYN_OSC_B_FRAME_SPREAD";   // float 0..1
    constexpr char SYN_OSC_B_INTERP_MODE[]   = "SYN_OSC_B_INTERP_MODE";    // choice {0=LINEAR}
    constexpr char SYN_OSC_B_PHASE_MODE[]    = "SYN_OSC_B_PHASE_MODE";     // choice 0=RETRIG,1=FREE,2=RANDOM,3=SPREAD
    constexpr char SYN_OSC_B_WAVER[]         = "SYN_OSC_B_WAVER";          // float 0..100 % analog pitch-drift depth (OU)
    constexpr char SYN_OSC_B_KEYTRACK[]      = "SYN_OSC_B_KEYTRACK";       // float 0..100 % note->destination depth
    constexpr char SYN_OSC_B_KEYTRACK_DEST[] = "SYN_OSC_B_KEYTRACK_DEST";  // choice 0=FRAME,1=WARP,2=FOLD
    constexpr char SYN_OSC_B_ROUTE_SRC[]     = "SYN_OSC_B_ROUTE_SRC";      // choice 0=NOTE,1=VEL
    constexpr char SYN_OSC_B_ROUTE_DEST[]    = "SYN_OSC_B_ROUTE_DEST";     // choice 0=FRAME,1=WARP,2=FOLD,3=CUT1,4=CUT2
    constexpr char SYN_OSC_B_ROUTE_AMT[]     = "SYN_OSC_B_ROUTE_AMT";      // float -100..100 % bipolar amount
    constexpr char SYN_OSC_B_UNISON[]        = "SYN_OSC_B_UNISON";         // int 1..16 voices (per-OSC)
    constexpr char SYN_OSC_B_UDETUNE[]       = "SYN_OSC_B_UDETUNE";        // float 0..100 % pitch fan
    constexpr char SYN_OSC_B_UBLEND[]        = "SYN_OSC_B_UBLEND";         // float 0..100 % centre-vs-outer balance
    constexpr char SYN_OSC_B_UWIDTH[]        = "SYN_OSC_B_UWIDTH";         // fb522 — float -100..+100 % stereo spread, default +50 (WAS 0..100/+50; the denormalised value is unchanged, only the normalised half it sits in)
    // ── OSC C + D chassis ids (4-osc) — mirror OSC B ──
    constexpr char SYN_OSC_C_ENGINE[]      = "SYN_OSC_C_ENGINE";       // enum 0..5
    constexpr char SYN_OSC_C_OCT[]         = "SYN_OSC_C_OCT";          // int -3..+3
    constexpr char SYN_OSC_C_SEMI[]        = "SYN_OSC_C_SEMI";         // int -12..+12
    constexpr char SYN_OSC_C_CENT[]        = "SYN_OSC_C_CENT";         // float -100..+100
    constexpr char SYN_OSC_C_LEVEL[]       = "SYN_OSC_C_LEVEL";        // float 0..1
    constexpr char SYN_OSC_C_PAN[]         = "SYN_OSC_C_PAN";          // float -1..+1
    constexpr char SYN_OSC_C_ENABLE[]  = "SYN_OSC_C_ENABLE";   // bool — osc C ON/OFF (the white OSC letters); off = render fully skipped
    constexpr char SYN_OSC_C_MUTE[]        = "SYN_OSC_C_MUTE";         // bool — osc C silent
    constexpr char SYN_OSC_C_SOLO[]        = "SYN_OSC_C_SOLO";         // bool — solo osc C
    constexpr char SYN_OSC_C_WT_PRESET[]   = "SYN_OSC_C_WT_PRESET";    // choice 0..19
    constexpr char SYN_OSC_C_WT_FRAME[]    = "SYN_OSC_C_WT_FRAME";     // float 0..1
    constexpr char SYN_OSC_C_WARP_MODE[]   = "SYN_OSC_C_WARP_MODE";    // choice(48) FROZEN — see SYN_OSC_A_WARP_MODE
    constexpr char SYN_OSC_C_WARP_AMOUNT[] = "SYN_OSC_C_WARP_AMOUNT";  // float 0..1
    constexpr char SYN_OSC_C_WARP2_MODE[]  = "SYN_OSC_C_WARP2_MODE";   // choice(48) FROZEN, chained slot 2
    constexpr char SYN_OSC_C_WARP2_AMT[]   = "SYN_OSC_C_WARP2_AMT";    // float 0..1
    constexpr char SYN_OSC_C_SPECTRAL_TYPE[] = "SYN_OSC_C_SPECTRAL_TYPE";  // choice {0=NONE}
    constexpr char SYN_OSC_C_SPECTRAL_AMT[]  = "SYN_OSC_C_SPECTRAL_AMT";   // float 0..1
    // fb467 — the spectral morph's PARTIAL WINDOW. Lo/Hi are harmonic indices (1..512,
    // log-mapped, smoothstep edges). Defaults 1 and 512 = the whole spectrum = exactly the
    // behaviour before fb467, so no saved patch moves.
    constexpr char SYN_OSC_C_SPECTRAL_LO[]  = "SYN_OSC_C_SPECTRAL_LO";   // float 1..512 (log)
    constexpr char SYN_OSC_C_SPECTRAL_HI[]  = "SYN_OSC_C_SPECTRAL_HI";   // float 1..512 (log)
    constexpr char SYN_OSC_C_FOLD_SHAPE[]    = "SYN_OSC_C_FOLD_SHAPE";     // choice {0=LINEAR}
    constexpr char SYN_OSC_C_FOLD_AMT[]      = "SYN_OSC_C_FOLD_AMT";       // float 0..1
    constexpr char SYN_OSC_C_FRAME_SPREAD[]  = "SYN_OSC_C_FRAME_SPREAD";   // float 0..1
    constexpr char SYN_OSC_C_INTERP_MODE[]   = "SYN_OSC_C_INTERP_MODE";    // choice {0=LINEAR}
    constexpr char SYN_OSC_C_PHASE_MODE[]    = "SYN_OSC_C_PHASE_MODE";     // choice 0=RETRIG,1=FREE,2=RANDOM,3=SPREAD
    constexpr char SYN_OSC_C_WAVER[]         = "SYN_OSC_C_WAVER";          // float 0..100 % analog pitch-drift depth (OU)
    constexpr char SYN_OSC_C_KEYTRACK[]      = "SYN_OSC_C_KEYTRACK";       // float 0..100 % note->destination depth
    constexpr char SYN_OSC_C_KEYTRACK_DEST[] = "SYN_OSC_C_KEYTRACK_DEST";  // choice 0=FRAME,1=WARP,2=FOLD
    constexpr char SYN_OSC_C_ROUTE_SRC[]     = "SYN_OSC_C_ROUTE_SRC";      // choice 0=NOTE,1=VEL
    constexpr char SYN_OSC_C_ROUTE_DEST[]    = "SYN_OSC_C_ROUTE_DEST";     // choice 0=FRAME,1=WARP,2=FOLD,3=CUT1,4=CUT2
    constexpr char SYN_OSC_C_ROUTE_AMT[]     = "SYN_OSC_C_ROUTE_AMT";      // float -100..100 % bipolar amount
    constexpr char SYN_OSC_C_UNISON[]        = "SYN_OSC_C_UNISON";         // int 1..16 voices (per-OSC)
    constexpr char SYN_OSC_C_UDETUNE[]       = "SYN_OSC_C_UDETUNE";        // float 0..100 % pitch fan
    constexpr char SYN_OSC_C_UBLEND[]        = "SYN_OSC_C_UBLEND";         // float 0..100 % centre-vs-outer balance
    constexpr char SYN_OSC_C_UWIDTH[]        = "SYN_OSC_C_UWIDTH";         // fb522 — float -100..+100 % stereo spread, default +50 (WAS 0..100/+50; the denormalised value is unchanged, only the normalised half it sits in)
    constexpr char SYN_OSC_D_ENGINE[]      = "SYN_OSC_D_ENGINE";       // enum 0..5
    constexpr char SYN_OSC_D_OCT[]         = "SYN_OSC_D_OCT";          // int -3..+3
    constexpr char SYN_OSC_D_SEMI[]        = "SYN_OSC_D_SEMI";         // int -12..+12
    constexpr char SYN_OSC_D_CENT[]        = "SYN_OSC_D_CENT";         // float -100..+100
    constexpr char SYN_OSC_D_LEVEL[]       = "SYN_OSC_D_LEVEL";        // float 0..1
    constexpr char SYN_OSC_D_PAN[]         = "SYN_OSC_D_PAN";          // float -1..+1
    constexpr char SYN_OSC_D_ENABLE[]  = "SYN_OSC_D_ENABLE";   // bool — osc D ON/OFF (the white OSC letters); off = render fully skipped
    constexpr char SYN_OSC_D_MUTE[]        = "SYN_OSC_D_MUTE";         // bool — osc D silent
    constexpr char SYN_OSC_D_SOLO[]        = "SYN_OSC_D_SOLO";         // bool — solo osc D
    constexpr char SYN_OSC_D_WT_PRESET[]   = "SYN_OSC_D_WT_PRESET";    // choice 0..19
    constexpr char SYN_OSC_D_WT_FRAME[]    = "SYN_OSC_D_WT_FRAME";     // float 0..1
    constexpr char SYN_OSC_D_WARP_MODE[]   = "SYN_OSC_D_WARP_MODE";    // choice(48) FROZEN — see SYN_OSC_A_WARP_MODE
    constexpr char SYN_OSC_D_WARP_AMOUNT[] = "SYN_OSC_D_WARP_AMOUNT";  // float 0..1
    constexpr char SYN_OSC_D_WARP2_MODE[]  = "SYN_OSC_D_WARP2_MODE";   // choice(48) FROZEN, chained slot 2
    constexpr char SYN_OSC_D_WARP2_AMT[]   = "SYN_OSC_D_WARP2_AMT";    // float 0..1
    constexpr char SYN_OSC_D_SPECTRAL_TYPE[] = "SYN_OSC_D_SPECTRAL_TYPE";  // choice {0=NONE}
    constexpr char SYN_OSC_D_SPECTRAL_AMT[]  = "SYN_OSC_D_SPECTRAL_AMT";   // float 0..1
    // fb467 — the spectral morph's PARTIAL WINDOW. Lo/Hi are harmonic indices (1..512,
    // log-mapped, smoothstep edges). Defaults 1 and 512 = the whole spectrum = exactly the
    // behaviour before fb467, so no saved patch moves.
    constexpr char SYN_OSC_D_SPECTRAL_LO[]  = "SYN_OSC_D_SPECTRAL_LO";   // float 1..512 (log)
    constexpr char SYN_OSC_D_SPECTRAL_HI[]  = "SYN_OSC_D_SPECTRAL_HI";   // float 1..512 (log)
    constexpr char SYN_OSC_D_FOLD_SHAPE[]    = "SYN_OSC_D_FOLD_SHAPE";     // choice {0=LINEAR}
    constexpr char SYN_OSC_D_FOLD_AMT[]      = "SYN_OSC_D_FOLD_AMT";       // float 0..1
    constexpr char SYN_OSC_D_FRAME_SPREAD[]  = "SYN_OSC_D_FRAME_SPREAD";   // float 0..1
    constexpr char SYN_OSC_D_INTERP_MODE[]   = "SYN_OSC_D_INTERP_MODE";    // choice {0=LINEAR}
    constexpr char SYN_OSC_D_PHASE_MODE[]    = "SYN_OSC_D_PHASE_MODE";     // choice 0=RETRIG,1=FREE,2=RANDOM,3=SPREAD
    constexpr char SYN_OSC_D_WAVER[]         = "SYN_OSC_D_WAVER";          // float 0..100 % analog pitch-drift depth (OU)
    constexpr char SYN_OSC_D_KEYTRACK[]      = "SYN_OSC_D_KEYTRACK";       // float 0..100 % note->destination depth
    constexpr char SYN_OSC_D_KEYTRACK_DEST[] = "SYN_OSC_D_KEYTRACK_DEST";  // choice 0=FRAME,1=WARP,2=FOLD
    constexpr char SYN_OSC_D_ROUTE_SRC[]     = "SYN_OSC_D_ROUTE_SRC";      // choice 0=NOTE,1=VEL
    constexpr char SYN_OSC_D_ROUTE_DEST[]    = "SYN_OSC_D_ROUTE_DEST";     // choice 0=FRAME,1=WARP,2=FOLD,3=CUT1,4=CUT2
    constexpr char SYN_OSC_D_ROUTE_AMT[]     = "SYN_OSC_D_ROUTE_AMT";      // float -100..100 % bipolar amount
    constexpr char SYN_OSC_D_UNISON[]        = "SYN_OSC_D_UNISON";         // int 1..16 voices (per-OSC)
    constexpr char SYN_OSC_D_UDETUNE[]       = "SYN_OSC_D_UDETUNE";        // float 0..100 % pitch fan
    constexpr char SYN_OSC_D_UBLEND[]        = "SYN_OSC_D_UBLEND";         // float 0..100 % centre-vs-outer balance
    constexpr char SYN_OSC_D_UWIDTH[]        = "SYN_OSC_D_UWIDTH";         // fb522 — float -100..+100 % stereo spread, default +50 (WAS 0..100/+50; the denormalised value is unchanged, only the normalised half it sits in)

    // ════════ fb522 · LANE P — THE OVERPASS PARAMS (unison range/warp/stack · warp VAR · phase) ════════
    //  Every one of these defaults to TODAY'S BEHAVIOUR, so a patch saved before this wave renders
    //  bit-identically until the user moves the knob. That is the whole point of the defaults below —
    //  do not "improve" one without a version-4 migration.
    //
    //  URANGE  float 5..4800 cents, default 50.0, EXPONENTIAL (cents = 5 * 960^t, a custom
    //          NormalisableRange lambda pair — NOT a JUCE power skew, which is a power curve on the
    //          LINEAR span and would not give equal cents-ratio per knob degree).
    //          🔑 default 50.0 EXACTLY reproduces SynthVoice.h's kUniMaxDetuneCents = 50.0f (verified
    //          at SynthVoice.h:6217), which is the hard-coded constant it replaces. t(50) =
    //          ln(10)/ln(960) = 0.335313 — see the note in createParameterLayout.
    //  UWARP   float -100..+100 %, default 0.0 — unison WARP SPREAD (fans warp amount across the
    //          unison sines). 0 = every sine gets the same warp = today.
    //  USTACK  choice(9), default 0 = off — the unison interval stack, measured from Serum 2's
    //          "A Uni Stack" (param id 1000025, serum2-curated.md §4). Option 0 is off, so default
    //          = today. ⚠️ fb470: a stack mode that falls into a default branch is SILENT — Lane V
    //          must switch exhaustively, never `default: break`.
    //  WVAR / W2VAR  float 0..100 %, default 0.0 — the SECOND DIMENSION of warp slots 1 and 2.
    //          These exist so the non-invertible ceiling raises (RM modulator drive, Rectify
    //          pre-gain, Mirror fold count) ride a NEW knob that defaults to 0 instead of changing
    //          the meaning of a stored WARP_AMOUNT. At 0 the render must be bit-identical.
    //  PHASE      float 0..360 degrees, default 0.0 — per-osc note-on phase offset. 0 = today.
    //  PHASE_AMT  float 0..100 %, default 100.0 — scales RANDOM's randomisation and SPREAD's fan.
    //          100 = full, which is what resolvePhase already does unscaled, so 100 = today.
    //  ⚠️ SYN_OSC_x_PHASE_MODE is NOT redeclared here — it already exists above (574/597/635/672)
    //     and its ID is FROZEN. fb522 changes only its REGISTERED DEFAULT, 2 (Random) -> 1 (Free),
    //     because the DSP has silently forced 1 since 2026-07-09 and every stored 2 in the library
    //     has never been heard. See migrateBlobToVersion3() in PluginProcessor.cpp.
    constexpr char SYN_OSC_A_URANGE[]    = "SYN_OSC_A_URANGE";     // float 5..4800 cents, def 50.0, exponential
    constexpr char SYN_OSC_A_UWARP[]     = "SYN_OSC_A_UWARP";      // float -100..+100 %, def 0.0 (inert)
    constexpr char SYN_OSC_A_USTACK[]    = "SYN_OSC_A_USTACK";     // choice(9), def 0 = off
    constexpr char SYN_OSC_A_WVAR[]      = "SYN_OSC_A_WVAR";       // float 0..100 %, def 0.0 — warp slot 1 second dimension
    constexpr char SYN_OSC_A_W2VAR[]     = "SYN_OSC_A_W2VAR";      // float 0..100 %, def 0.0 — warp slot 2 second dimension
    constexpr char SYN_OSC_A_PHASE[]     = "SYN_OSC_A_PHASE";      // float 0..360 deg, def 0.0
    constexpr char SYN_OSC_A_PHASE_AMT[] = "SYN_OSC_A_PHASE_AMT";  // float 0..100 %, def 100.0
    constexpr char SYN_OSC_B_URANGE[]    = "SYN_OSC_B_URANGE";     // float 5..4800 cents, def 50.0, exponential
    constexpr char SYN_OSC_B_UWARP[]     = "SYN_OSC_B_UWARP";      // float -100..+100 %, def 0.0 (inert)
    constexpr char SYN_OSC_B_USTACK[]    = "SYN_OSC_B_USTACK";     // choice(9), def 0 = off
    constexpr char SYN_OSC_B_WVAR[]      = "SYN_OSC_B_WVAR";       // float 0..100 %, def 0.0 — warp slot 1 second dimension
    constexpr char SYN_OSC_B_W2VAR[]     = "SYN_OSC_B_W2VAR";      // float 0..100 %, def 0.0 — warp slot 2 second dimension
    constexpr char SYN_OSC_B_PHASE[]     = "SYN_OSC_B_PHASE";      // float 0..360 deg, def 0.0
    constexpr char SYN_OSC_B_PHASE_AMT[] = "SYN_OSC_B_PHASE_AMT";  // float 0..100 %, def 100.0
    constexpr char SYN_OSC_C_URANGE[]    = "SYN_OSC_C_URANGE";     // float 5..4800 cents, def 50.0, exponential
    constexpr char SYN_OSC_C_UWARP[]     = "SYN_OSC_C_UWARP";      // float -100..+100 %, def 0.0 (inert)
    constexpr char SYN_OSC_C_USTACK[]    = "SYN_OSC_C_USTACK";     // choice(9), def 0 = off
    constexpr char SYN_OSC_C_WVAR[]      = "SYN_OSC_C_WVAR";       // float 0..100 %, def 0.0 — warp slot 1 second dimension
    constexpr char SYN_OSC_C_W2VAR[]     = "SYN_OSC_C_W2VAR";      // float 0..100 %, def 0.0 — warp slot 2 second dimension
    constexpr char SYN_OSC_C_PHASE[]     = "SYN_OSC_C_PHASE";      // float 0..360 deg, def 0.0
    constexpr char SYN_OSC_C_PHASE_AMT[] = "SYN_OSC_C_PHASE_AMT";  // float 0..100 %, def 100.0
    constexpr char SYN_OSC_D_URANGE[]    = "SYN_OSC_D_URANGE";     // float 5..4800 cents, def 50.0, exponential
    constexpr char SYN_OSC_D_UWARP[]     = "SYN_OSC_D_UWARP";      // float -100..+100 %, def 0.0 (inert)
    constexpr char SYN_OSC_D_USTACK[]    = "SYN_OSC_D_USTACK";     // choice(9), def 0 = off
    constexpr char SYN_OSC_D_WVAR[]      = "SYN_OSC_D_WVAR";       // float 0..100 %, def 0.0 — warp slot 1 second dimension
    constexpr char SYN_OSC_D_W2VAR[]     = "SYN_OSC_D_W2VAR";      // float 0..100 %, def 0.0 — warp slot 2 second dimension
    constexpr char SYN_OSC_D_PHASE[]     = "SYN_OSC_D_PHASE";      // float 0..360 deg, def 0.0
    constexpr char SYN_OSC_D_PHASE_AMT[] = "SYN_OSC_D_PHASE_AMT";  // float 0..100 %, def 100.0

    // ── SAMPLE-ENGINE-IDS — per-OSC Sample engine params (Opus, 2026-06-25) ──
    constexpr char SYN_OSC_A_SAMPLE_SCAN[]       = "SYN_OSC_A_SAMPLE_SCAN";       // float -1..+1 bipolar — playback rate+direction
    constexpr char SYN_OSC_A_SAMPLE_STRETCH[]    = "SYN_OSC_A_SAMPLE_STRETCH";    // float 0..1 — time-stretch (Warp), pitch held
    constexpr char SYN_OSC_A_SAMPLE_FORMANT[]    = "SYN_OSC_A_SAMPLE_FORMANT";    // float -1..+1 bipolar — formant shift
    constexpr char SYN_OSC_A_SAMPLE_SPRAY[]      = "SYN_OSC_A_SAMPLE_SPRAY";      // float 0..1 — per-note random start scatter
    constexpr char SYN_OSC_A_SAMPLE_XFADE[]      = "SYN_OSC_A_SAMPLE_XFADE";      // float 0..1 — equal-power loop crossfade
    constexpr char SYN_OSC_A_SAMPLE_START[]      = "SYN_OSC_A_SAMPLE_START";      // float 0..1 — region start
    constexpr char SYN_OSC_A_SAMPLE_END[]        = "SYN_OSC_A_SAMPLE_END";        // float 0..1 — region end
    constexpr char SYN_OSC_A_SAMPLE_LOOP_START[] = "SYN_OSC_A_SAMPLE_LOOP_START"; // float 0..1 — loop start (clamped in region)
    constexpr char SYN_OSC_A_SAMPLE_LOOP_END[]   = "SYN_OSC_A_SAMPLE_LOOP_END";   // float 0..1 — loop end (clamped in region)
    constexpr char SYN_OSC_A_SAMPLE_LOOP_MODE[]  = "SYN_OSC_A_SAMPLE_LOOP_MODE";  // choice 0..4 — OneShot/Fwd/Rev/PingPong/Tailed
    constexpr char SYN_OSC_A_SAMPLE_SNAP[]       = "SYN_OSC_A_SAMPLE_SNAP";       // choice 0..2 — Off/Zero-cross/Transient
    constexpr char SYN_OSC_A_SAMPLE_STRETCH_MODE[] = "SYN_OSC_A_SAMPLE_STRETCH_MODE"; // choice 0..2 — Tones/Beats/Texture
    constexpr char SYN_OSC_A_SAMPLE_FORMANT_MODE[] = "SYN_OSC_A_SAMPLE_FORMANT_MODE"; // choice 0..3 — Normal/Inverted/Cross/Tilt
    constexpr char SYN_OSC_A_SAMPLE_FADE_IN[]    = "SYN_OSC_A_SAMPLE_FADE_IN";    // float 0..1 — region fade in
    constexpr char SYN_OSC_A_SAMPLE_FADE_OUT[]   = "SYN_OSC_A_SAMPLE_FADE_OUT";   // float 0..1 — region fade out
    constexpr char SYN_OSC_A_SAMPLE_AIR[]        = "SYN_OSC_A_SAMPLE_AIR";        // float 0..1 — Chebyshev high-harmonic exciter (timbral air)
    constexpr char SYN_OSC_A_SAMPLE_WARP[]       = "SYN_OSC_A_SAMPLE_WARP";       // float 0..1 — sample warp shaper amount
    constexpr char SYN_OSC_A_SAMPLE_WARPMODE[]   = "SYN_OSC_A_SAMPLE_WARPMODE";   // choice 0..5 — Off/Sine Shaper/Rectify/Fold/Drive/Crush

    constexpr char SYN_OSC_B_SAMPLE_SCAN[]       = "SYN_OSC_B_SAMPLE_SCAN";       // float -1..+1 bipolar — playback rate+direction
    constexpr char SYN_OSC_B_SAMPLE_STRETCH[]    = "SYN_OSC_B_SAMPLE_STRETCH";    // float 0..1 — time-stretch (Warp), pitch held
    constexpr char SYN_OSC_B_SAMPLE_FORMANT[]    = "SYN_OSC_B_SAMPLE_FORMANT";    // float -1..+1 bipolar — formant shift
    constexpr char SYN_OSC_B_SAMPLE_SPRAY[]      = "SYN_OSC_B_SAMPLE_SPRAY";      // float 0..1 — per-note random start scatter
    constexpr char SYN_OSC_B_SAMPLE_XFADE[]      = "SYN_OSC_B_SAMPLE_XFADE";      // float 0..1 — equal-power loop crossfade
    constexpr char SYN_OSC_B_SAMPLE_START[]      = "SYN_OSC_B_SAMPLE_START";      // float 0..1 — region start
    constexpr char SYN_OSC_B_SAMPLE_END[]        = "SYN_OSC_B_SAMPLE_END";        // float 0..1 — region end
    constexpr char SYN_OSC_B_SAMPLE_LOOP_START[] = "SYN_OSC_B_SAMPLE_LOOP_START"; // float 0..1 — loop start (clamped in region)
    constexpr char SYN_OSC_B_SAMPLE_LOOP_END[]   = "SYN_OSC_B_SAMPLE_LOOP_END";   // float 0..1 — loop end (clamped in region)
    constexpr char SYN_OSC_B_SAMPLE_LOOP_MODE[]  = "SYN_OSC_B_SAMPLE_LOOP_MODE";  // choice 0..4 — OneShot/Fwd/Rev/PingPong/Tailed
    constexpr char SYN_OSC_B_SAMPLE_SNAP[]       = "SYN_OSC_B_SAMPLE_SNAP";       // choice 0..2 — Off/Zero-cross/Transient
    constexpr char SYN_OSC_B_SAMPLE_STRETCH_MODE[] = "SYN_OSC_B_SAMPLE_STRETCH_MODE"; // choice 0..2 — Tones/Beats/Texture
    constexpr char SYN_OSC_B_SAMPLE_FORMANT_MODE[] = "SYN_OSC_B_SAMPLE_FORMANT_MODE"; // choice 0..3 — Normal/Inverted/Cross/Tilt
    constexpr char SYN_OSC_B_SAMPLE_FADE_IN[]    = "SYN_OSC_B_SAMPLE_FADE_IN";    // float 0..1 — region fade in
    constexpr char SYN_OSC_B_SAMPLE_FADE_OUT[]   = "SYN_OSC_B_SAMPLE_FADE_OUT";   // float 0..1 — region fade out
    constexpr char SYN_OSC_B_SAMPLE_AIR[]        = "SYN_OSC_B_SAMPLE_AIR";        // float 0..1 — Chebyshev high-harmonic exciter (timbral air)
    constexpr char SYN_OSC_B_SAMPLE_WARP[]       = "SYN_OSC_B_SAMPLE_WARP";       // float 0..1 — sample warp shaper amount
    constexpr char SYN_OSC_B_SAMPLE_WARPMODE[]   = "SYN_OSC_B_SAMPLE_WARPMODE";   // choice 0..5 — Off/Sine Shaper/Rectify/Fold/Drive/Crush

    constexpr char SYN_OSC_C_SAMPLE_SCAN[]       = "SYN_OSC_C_SAMPLE_SCAN";       // float -1..+1 bipolar — playback rate+direction
    constexpr char SYN_OSC_C_SAMPLE_STRETCH[]    = "SYN_OSC_C_SAMPLE_STRETCH";    // float 0..1 — time-stretch (Warp), pitch held
    constexpr char SYN_OSC_C_SAMPLE_FORMANT[]    = "SYN_OSC_C_SAMPLE_FORMANT";    // float -1..+1 bipolar — formant shift
    constexpr char SYN_OSC_C_SAMPLE_SPRAY[]      = "SYN_OSC_C_SAMPLE_SPRAY";      // float 0..1 — per-note random start scatter
    constexpr char SYN_OSC_C_SAMPLE_XFADE[]      = "SYN_OSC_C_SAMPLE_XFADE";      // float 0..1 — equal-power loop crossfade
    constexpr char SYN_OSC_C_SAMPLE_START[]      = "SYN_OSC_C_SAMPLE_START";      // float 0..1 — region start
    constexpr char SYN_OSC_C_SAMPLE_END[]        = "SYN_OSC_C_SAMPLE_END";        // float 0..1 — region end
    constexpr char SYN_OSC_C_SAMPLE_LOOP_START[] = "SYN_OSC_C_SAMPLE_LOOP_START"; // float 0..1 — loop start (clamped in region)
    constexpr char SYN_OSC_C_SAMPLE_LOOP_END[]   = "SYN_OSC_C_SAMPLE_LOOP_END";   // float 0..1 — loop end (clamped in region)
    constexpr char SYN_OSC_C_SAMPLE_LOOP_MODE[]  = "SYN_OSC_C_SAMPLE_LOOP_MODE";  // choice 0..4 — OneShot/Fwd/Rev/PingPong/Tailed
    constexpr char SYN_OSC_C_SAMPLE_SNAP[]       = "SYN_OSC_C_SAMPLE_SNAP";       // choice 0..2 — Off/Zero-cross/Transient
    constexpr char SYN_OSC_C_SAMPLE_STRETCH_MODE[] = "SYN_OSC_C_SAMPLE_STRETCH_MODE"; // choice 0..2 — Tones/Beats/Texture
    constexpr char SYN_OSC_C_SAMPLE_FORMANT_MODE[] = "SYN_OSC_C_SAMPLE_FORMANT_MODE"; // choice 0..3 — Normal/Inverted/Cross/Tilt
    constexpr char SYN_OSC_C_SAMPLE_FADE_IN[]    = "SYN_OSC_C_SAMPLE_FADE_IN";    // float 0..1 — region fade in
    constexpr char SYN_OSC_C_SAMPLE_FADE_OUT[]   = "SYN_OSC_C_SAMPLE_FADE_OUT";   // float 0..1 — region fade out
    constexpr char SYN_OSC_C_SAMPLE_AIR[]        = "SYN_OSC_C_SAMPLE_AIR";        // float 0..1 — Chebyshev high-harmonic exciter (timbral air)
    constexpr char SYN_OSC_C_SAMPLE_WARP[]       = "SYN_OSC_C_SAMPLE_WARP";       // float 0..1 — sample warp shaper amount
    constexpr char SYN_OSC_C_SAMPLE_WARPMODE[]   = "SYN_OSC_C_SAMPLE_WARPMODE";   // choice 0..5 — Off/Sine Shaper/Rectify/Fold/Drive/Crush

    constexpr char SYN_OSC_D_SAMPLE_SCAN[]       = "SYN_OSC_D_SAMPLE_SCAN";       // float -1..+1 bipolar — playback rate+direction
    constexpr char SYN_OSC_D_SAMPLE_STRETCH[]    = "SYN_OSC_D_SAMPLE_STRETCH";    // float 0..1 — time-stretch (Warp), pitch held
    constexpr char SYN_OSC_D_SAMPLE_FORMANT[]    = "SYN_OSC_D_SAMPLE_FORMANT";    // float -1..+1 bipolar — formant shift
    constexpr char SYN_OSC_D_SAMPLE_SPRAY[]      = "SYN_OSC_D_SAMPLE_SPRAY";      // float 0..1 — per-note random start scatter
    constexpr char SYN_OSC_D_SAMPLE_XFADE[]      = "SYN_OSC_D_SAMPLE_XFADE";      // float 0..1 — equal-power loop crossfade
    constexpr char SYN_OSC_D_SAMPLE_START[]      = "SYN_OSC_D_SAMPLE_START";      // float 0..1 — region start
    constexpr char SYN_OSC_D_SAMPLE_END[]        = "SYN_OSC_D_SAMPLE_END";        // float 0..1 — region end
    constexpr char SYN_OSC_D_SAMPLE_LOOP_START[] = "SYN_OSC_D_SAMPLE_LOOP_START"; // float 0..1 — loop start (clamped in region)
    constexpr char SYN_OSC_D_SAMPLE_LOOP_END[]   = "SYN_OSC_D_SAMPLE_LOOP_END";   // float 0..1 — loop end (clamped in region)
    constexpr char SYN_OSC_D_SAMPLE_LOOP_MODE[]  = "SYN_OSC_D_SAMPLE_LOOP_MODE";  // choice 0..4 — OneShot/Fwd/Rev/PingPong/Tailed
    constexpr char SYN_OSC_D_SAMPLE_SNAP[]       = "SYN_OSC_D_SAMPLE_SNAP";       // choice 0..2 — Off/Zero-cross/Transient
    constexpr char SYN_OSC_D_SAMPLE_STRETCH_MODE[] = "SYN_OSC_D_SAMPLE_STRETCH_MODE"; // choice 0..2 — Tones/Beats/Texture
    constexpr char SYN_OSC_D_SAMPLE_FORMANT_MODE[] = "SYN_OSC_D_SAMPLE_FORMANT_MODE"; // choice 0..3 — Normal/Inverted/Cross/Tilt
    constexpr char SYN_OSC_D_SAMPLE_FADE_IN[]    = "SYN_OSC_D_SAMPLE_FADE_IN";    // float 0..1 — region fade in
    constexpr char SYN_OSC_D_SAMPLE_FADE_OUT[]   = "SYN_OSC_D_SAMPLE_FADE_OUT";   // float 0..1 — region fade out
    constexpr char SYN_OSC_D_SAMPLE_AIR[]        = "SYN_OSC_D_SAMPLE_AIR";        // float 0..1 — Chebyshev high-harmonic exciter (timbral air)
    constexpr char SYN_OSC_D_SAMPLE_WARP[]       = "SYN_OSC_D_SAMPLE_WARP";       // float 0..1 — sample warp shaper amount
    constexpr char SYN_OSC_D_SAMPLE_WARPMODE[]   = "SYN_OSC_D_SAMPLE_WARPMODE";   // choice 0..5 — Off/Sine Shaper/Rectify/Fold/Drive/Crush

    // ── GRAIN-ENGINE-IDS — per-OSC Granular engine params (2026-07-02) ──
    constexpr char SYN_OSC_A_GRAIN_SCAN[]    = "SYN_OSC_A_GRAIN_SCAN";     // float -1..+1 — read-head rate (0=freeze, <0=reverse)
    constexpr char SYN_OSC_A_GRAIN_DENSITY[] = "SYN_OSC_A_GRAIN_DENSITY";  // float 0..1 — grains/sec (log 1..200)
    constexpr char SYN_OSC_A_GRAIN_SIZE[]    = "SYN_OSC_A_GRAIN_SIZE";     // float 0..1 — grain length (log 2..500 ms)
    constexpr char SYN_OSC_A_GRAIN_SPRAY[]   = "SYN_OSC_A_GRAIN_SPRAY";    // float 0..1 — grain-birth position jitter
    constexpr char SYN_OSC_A_GRAIN_SHAPE[]   = "SYN_OSC_A_GRAIN_SHAPE";    // float 0..1 — window morph Tukey<->Gaussian
    constexpr char SYN_OSC_A_GRAIN_KEY[]     = "SYN_OSC_A_GRAIN_KEY";      // choice 0..6 — Off/Oct/5th/Chord/Maj/Min/Penta
    constexpr char SYN_OSC_B_GRAIN_SCAN[]    = "SYN_OSC_B_GRAIN_SCAN";
    constexpr char SYN_OSC_B_GRAIN_DENSITY[] = "SYN_OSC_B_GRAIN_DENSITY";
    constexpr char SYN_OSC_B_GRAIN_SIZE[]    = "SYN_OSC_B_GRAIN_SIZE";
    constexpr char SYN_OSC_B_GRAIN_SPRAY[]   = "SYN_OSC_B_GRAIN_SPRAY";
    constexpr char SYN_OSC_B_GRAIN_SHAPE[]   = "SYN_OSC_B_GRAIN_SHAPE";
    constexpr char SYN_OSC_B_GRAIN_KEY[]     = "SYN_OSC_B_GRAIN_KEY";
    constexpr char SYN_OSC_C_GRAIN_SCAN[]    = "SYN_OSC_C_GRAIN_SCAN";
    constexpr char SYN_OSC_C_GRAIN_DENSITY[] = "SYN_OSC_C_GRAIN_DENSITY";
    constexpr char SYN_OSC_C_GRAIN_SIZE[]    = "SYN_OSC_C_GRAIN_SIZE";
    constexpr char SYN_OSC_C_GRAIN_SPRAY[]   = "SYN_OSC_C_GRAIN_SPRAY";
    constexpr char SYN_OSC_C_GRAIN_SHAPE[]   = "SYN_OSC_C_GRAIN_SHAPE";
    constexpr char SYN_OSC_C_GRAIN_KEY[]     = "SYN_OSC_C_GRAIN_KEY";
    constexpr char SYN_OSC_D_GRAIN_SCAN[]    = "SYN_OSC_D_GRAIN_SCAN";
    constexpr char SYN_OSC_D_GRAIN_DENSITY[] = "SYN_OSC_D_GRAIN_DENSITY";
    constexpr char SYN_OSC_D_GRAIN_SIZE[]    = "SYN_OSC_D_GRAIN_SIZE";
    constexpr char SYN_OSC_D_GRAIN_SPRAY[]   = "SYN_OSC_D_GRAIN_SPRAY";
    constexpr char SYN_OSC_D_GRAIN_SHAPE[]   = "SYN_OSC_D_GRAIN_SHAPE";
    constexpr char SYN_OSC_D_GRAIN_KEY[]     = "SYN_OSC_D_GRAIN_KEY";

    // ── BLEND (per-osc one-shot blend/morph — OFFLINE bake knobs; audio thread never reads these) ──
    constexpr char SYN_OSC_A_BLEND_MORPH[]  = "SYN_OSC_A_BLEND_MORPH";
    constexpr char SYN_OSC_A_BLEND_ATTACK[] = "SYN_OSC_A_BLEND_ATTACK";
    constexpr char SYN_OSC_A_BLEND_BODY[]   = "SYN_OSC_A_BLEND_BODY";
    constexpr char SYN_OSC_A_BLEND_BREATH[] = "SYN_OSC_A_BLEND_BREATH";
    constexpr char SYN_OSC_A_BLEND_SCULPT[] = "SYN_OSC_A_BLEND_SCULPT";
    constexpr char SYN_OSC_A_BLEND_DICE[]   = "SYN_OSC_A_BLEND_DICE";
    constexpr char SYN_OSC_B_BLEND_MORPH[]  = "SYN_OSC_B_BLEND_MORPH";
    constexpr char SYN_OSC_B_BLEND_ATTACK[] = "SYN_OSC_B_BLEND_ATTACK";
    constexpr char SYN_OSC_B_BLEND_BODY[]   = "SYN_OSC_B_BLEND_BODY";
    constexpr char SYN_OSC_B_BLEND_BREATH[] = "SYN_OSC_B_BLEND_BREATH";
    constexpr char SYN_OSC_B_BLEND_SCULPT[] = "SYN_OSC_B_BLEND_SCULPT";
    constexpr char SYN_OSC_B_BLEND_DICE[]   = "SYN_OSC_B_BLEND_DICE";
    constexpr char SYN_OSC_C_BLEND_MORPH[]  = "SYN_OSC_C_BLEND_MORPH";
    constexpr char SYN_OSC_C_BLEND_ATTACK[] = "SYN_OSC_C_BLEND_ATTACK";
    constexpr char SYN_OSC_C_BLEND_BODY[]   = "SYN_OSC_C_BLEND_BODY";
    constexpr char SYN_OSC_C_BLEND_BREATH[] = "SYN_OSC_C_BLEND_BREATH";
    constexpr char SYN_OSC_C_BLEND_SCULPT[] = "SYN_OSC_C_BLEND_SCULPT";
    constexpr char SYN_OSC_C_BLEND_DICE[]   = "SYN_OSC_C_BLEND_DICE";
    constexpr char SYN_OSC_D_BLEND_MORPH[]  = "SYN_OSC_D_BLEND_MORPH";
    constexpr char SYN_OSC_D_BLEND_ATTACK[] = "SYN_OSC_D_BLEND_ATTACK";
    constexpr char SYN_OSC_D_BLEND_BODY[]   = "SYN_OSC_D_BLEND_BODY";
    constexpr char SYN_OSC_D_BLEND_BREATH[] = "SYN_OSC_D_BLEND_BREATH";
    constexpr char SYN_OSC_D_BLEND_SCULPT[] = "SYN_OSC_D_BLEND_SCULPT";
    constexpr char SYN_OSC_D_BLEND_DICE[]   = "SYN_OSC_D_BLEND_DICE";

    // ── FADE CURVES (Ableton-style curve diamond per region-fade edge; 0.5 = classic sin) ──
    constexpr char SYN_OSC_A_SAMPLE_FADEIN_CURVE[]  = "SYN_OSC_A_SAMPLE_FADEIN_CURVE";
    constexpr char SYN_OSC_A_SAMPLE_FADEOUT_CURVE[] = "SYN_OSC_A_SAMPLE_FADEOUT_CURVE";
    constexpr char SYN_OSC_B_SAMPLE_FADEIN_CURVE[]  = "SYN_OSC_B_SAMPLE_FADEIN_CURVE";
    constexpr char SYN_OSC_B_SAMPLE_FADEOUT_CURVE[] = "SYN_OSC_B_SAMPLE_FADEOUT_CURVE";
    constexpr char SYN_OSC_C_SAMPLE_FADEIN_CURVE[]  = "SYN_OSC_C_SAMPLE_FADEIN_CURVE";
    constexpr char SYN_OSC_C_SAMPLE_FADEOUT_CURVE[] = "SYN_OSC_C_SAMPLE_FADEOUT_CURVE";
    constexpr char SYN_OSC_D_SAMPLE_FADEIN_CURVE[]  = "SYN_OSC_D_SAMPLE_FADEIN_CURVE";
    constexpr char SYN_OSC_D_SAMPLE_FADEOUT_CURVE[] = "SYN_OSC_D_SAMPLE_FADEOUT_CURVE";

    // ── GRAIN-EXPANDED-IDS — the 6 page-2 functions beyond the default 6 (Life/Jump removed 2026-07-02;
    //    Air/Stretch now reuse the Sample osc's SYN_OSC_x_SAMPLE_AIR/_STRETCH/_STRETCH_MODE params) ──
    constexpr char SYN_OSC_A_GRAIN_POSITION[] = "SYN_OSC_A_GRAIN_POSITION"; // float 0..1 grain-birth anchor
    constexpr char SYN_OSC_A_GRAIN_PITCH[]    = "SYN_OSC_A_GRAIN_PITCH";    // float -24..24 st base transpose
    constexpr char SYN_OSC_A_GRAIN_PSPRAY[]   = "SYN_OSC_A_GRAIN_PSPRAY";   // float 0..1 per-grain pitch scatter
    constexpr char SYN_OSC_A_GRAIN_WIDTH[]    = "SYN_OSC_A_GRAIN_WIDTH";    // float 0..1 per-grain stereo spread
    constexpr char SYN_OSC_A_GRAIN_DIR[]      = "SYN_OSC_A_GRAIN_DIR";      // float -1..1 direction bias
    constexpr char SYN_OSC_A_GRAIN_SKEW[]     = "SYN_OSC_A_GRAIN_SKEW";     // float -1..1 window skew
    constexpr char SYN_OSC_B_GRAIN_POSITION[] = "SYN_OSC_B_GRAIN_POSITION";
    constexpr char SYN_OSC_B_GRAIN_PITCH[]    = "SYN_OSC_B_GRAIN_PITCH";
    constexpr char SYN_OSC_B_GRAIN_PSPRAY[]   = "SYN_OSC_B_GRAIN_PSPRAY";
    constexpr char SYN_OSC_B_GRAIN_WIDTH[]    = "SYN_OSC_B_GRAIN_WIDTH";
    constexpr char SYN_OSC_B_GRAIN_DIR[]      = "SYN_OSC_B_GRAIN_DIR";
    constexpr char SYN_OSC_B_GRAIN_SKEW[]     = "SYN_OSC_B_GRAIN_SKEW";
    constexpr char SYN_OSC_C_GRAIN_POSITION[] = "SYN_OSC_C_GRAIN_POSITION";
    constexpr char SYN_OSC_C_GRAIN_PITCH[]    = "SYN_OSC_C_GRAIN_PITCH";
    constexpr char SYN_OSC_C_GRAIN_PSPRAY[]   = "SYN_OSC_C_GRAIN_PSPRAY";
    constexpr char SYN_OSC_C_GRAIN_WIDTH[]    = "SYN_OSC_C_GRAIN_WIDTH";
    constexpr char SYN_OSC_C_GRAIN_DIR[]      = "SYN_OSC_C_GRAIN_DIR";
    constexpr char SYN_OSC_C_GRAIN_SKEW[]     = "SYN_OSC_C_GRAIN_SKEW";
    constexpr char SYN_OSC_D_GRAIN_POSITION[] = "SYN_OSC_D_GRAIN_POSITION";
    constexpr char SYN_OSC_D_GRAIN_PITCH[]    = "SYN_OSC_D_GRAIN_PITCH";
    constexpr char SYN_OSC_D_GRAIN_PSPRAY[]   = "SYN_OSC_D_GRAIN_PSPRAY";
    constexpr char SYN_OSC_D_GRAIN_WIDTH[]    = "SYN_OSC_D_GRAIN_WIDTH";
    constexpr char SYN_OSC_D_GRAIN_DIR[]      = "SYN_OSC_D_GRAIN_DIR";
    constexpr char SYN_OSC_D_GRAIN_SKEW[]     = "SYN_OSC_D_GRAIN_SKEW";

    // ── FM-ENGINE-IDS — per-OSC wavetable-carrier FM (2026-07-04). The osc's own
    //    wavetable IS the carrier (frame morph / blur / spectral still live); two sine
    //    modulators + DX-style averaged self-feedback on M1. Ratios keytrack via the
    //    carrier's phase increment. ──
    constexpr char SYN_OSC_A_FM_ALGO[]   = "SYN_OSC_A_FM_ALGO";     // choice 0..2 — Stack (M2→M1→carrier) / Split (M1,M2→carrier) / Ring (M2→M1; M1 rings the output)
    constexpr char SYN_OSC_A_FM_RATIO1[] = "SYN_OSC_A_FM_RATIO1";   // float 0.25..16 — M1 freq ratio
    constexpr char SYN_OSC_A_FM_DEPTH1[] = "SYN_OSC_A_FM_DEPTH1";   // float 0..1 — M1 index (squared taper)
    constexpr char SYN_OSC_A_FM_RATIO2[] = "SYN_OSC_A_FM_RATIO2";   // float 0.25..16 — M2 freq ratio
    constexpr char SYN_OSC_A_FM_DEPTH2[] = "SYN_OSC_A_FM_DEPTH2";   // float 0..1 — M2 index (squared taper)
    constexpr char SYN_OSC_A_FM_FB[]     = "SYN_OSC_A_FM_FB";       // float 0..1 — M1 self-feedback
    constexpr char SYN_OSC_B_FM_ALGO[]   = "SYN_OSC_B_FM_ALGO";
    constexpr char SYN_OSC_B_FM_RATIO1[] = "SYN_OSC_B_FM_RATIO1";
    constexpr char SYN_OSC_B_FM_DEPTH1[] = "SYN_OSC_B_FM_DEPTH1";
    constexpr char SYN_OSC_B_FM_RATIO2[] = "SYN_OSC_B_FM_RATIO2";
    constexpr char SYN_OSC_B_FM_DEPTH2[] = "SYN_OSC_B_FM_DEPTH2";
    constexpr char SYN_OSC_B_FM_FB[]     = "SYN_OSC_B_FM_FB";
    constexpr char SYN_OSC_C_FM_ALGO[]   = "SYN_OSC_C_FM_ALGO";
    constexpr char SYN_OSC_C_FM_RATIO1[] = "SYN_OSC_C_FM_RATIO1";
    constexpr char SYN_OSC_C_FM_DEPTH1[] = "SYN_OSC_C_FM_DEPTH1";
    constexpr char SYN_OSC_C_FM_RATIO2[] = "SYN_OSC_C_FM_RATIO2";
    constexpr char SYN_OSC_C_FM_DEPTH2[] = "SYN_OSC_C_FM_DEPTH2";
    constexpr char SYN_OSC_C_FM_FB[]     = "SYN_OSC_C_FM_FB";
    constexpr char SYN_OSC_D_FM_ALGO[]   = "SYN_OSC_D_FM_ALGO";
    constexpr char SYN_OSC_D_FM_RATIO1[] = "SYN_OSC_D_FM_RATIO1";
    constexpr char SYN_OSC_D_FM_DEPTH1[] = "SYN_OSC_D_FM_DEPTH1";
    constexpr char SYN_OSC_D_FM_RATIO2[] = "SYN_OSC_D_FM_RATIO2";
    constexpr char SYN_OSC_D_FM_DEPTH2[] = "SYN_OSC_D_FM_DEPTH2";
    constexpr char SYN_OSC_D_FM_FB[]     = "SYN_OSC_D_FM_FB";

    // ── FM WEATHERING SUITE — page-2 FM functions (2026-07-04). The elements acting
    //    on the FM: Strike (velocity index transient), Age (operator drift), Rust
    //    (inharmonic Hz offset), Gale (note-tracked noise FM), Bend (phase-distortion
    //    modulator shaping), Storm (mutual modulator cross-coupling → chaos). ──
    constexpr char SYN_OSC_A_FM_STRIKE[] = "SYN_OSC_A_FM_STRIKE";
    constexpr char SYN_OSC_A_FM_AGE[]    = "SYN_OSC_A_FM_AGE";
    constexpr char SYN_OSC_A_FM_RUST[]   = "SYN_OSC_A_FM_RUST";
    constexpr char SYN_OSC_A_FM_GALE[]   = "SYN_OSC_A_FM_GALE";
    constexpr char SYN_OSC_A_FM_BEND[]   = "SYN_OSC_A_FM_BEND";
    constexpr char SYN_OSC_A_FM_STORM[]  = "SYN_OSC_A_FM_STORM";
    constexpr char SYN_OSC_B_FM_STRIKE[] = "SYN_OSC_B_FM_STRIKE";
    constexpr char SYN_OSC_B_FM_AGE[]    = "SYN_OSC_B_FM_AGE";
    constexpr char SYN_OSC_B_FM_RUST[]   = "SYN_OSC_B_FM_RUST";
    constexpr char SYN_OSC_B_FM_GALE[]   = "SYN_OSC_B_FM_GALE";
    constexpr char SYN_OSC_B_FM_BEND[]   = "SYN_OSC_B_FM_BEND";
    constexpr char SYN_OSC_B_FM_STORM[]  = "SYN_OSC_B_FM_STORM";
    constexpr char SYN_OSC_C_FM_STRIKE[] = "SYN_OSC_C_FM_STRIKE";
    constexpr char SYN_OSC_C_FM_AGE[]    = "SYN_OSC_C_FM_AGE";
    constexpr char SYN_OSC_C_FM_RUST[]   = "SYN_OSC_C_FM_RUST";
    constexpr char SYN_OSC_C_FM_GALE[]   = "SYN_OSC_C_FM_GALE";
    constexpr char SYN_OSC_C_FM_BEND[]   = "SYN_OSC_C_FM_BEND";
    constexpr char SYN_OSC_C_FM_STORM[]  = "SYN_OSC_C_FM_STORM";
    constexpr char SYN_OSC_D_FM_STRIKE[] = "SYN_OSC_D_FM_STRIKE";
    constexpr char SYN_OSC_D_FM_AGE[]    = "SYN_OSC_D_FM_AGE";
    constexpr char SYN_OSC_D_FM_RUST[]   = "SYN_OSC_D_FM_RUST";
    constexpr char SYN_OSC_D_FM_GALE[]   = "SYN_OSC_D_FM_GALE";
    constexpr char SYN_OSC_D_FM_BEND[]   = "SYN_OSC_D_FM_BEND";
    constexpr char SYN_OSC_D_FM_STORM[]  = "SYN_OSC_D_FM_STORM";

    // ════════ GEODE-ENGINE-PARAMS — per-OSC RESYNTH oscillator (Engine::SPEC) ════════
    // Own namespace — MUST NOT collide with SYN_OSC_x_SPECTRAL_TYPE/AMT (the spectral FILTER).
    // The engine is UI-named "Resynth"; these ID strings keep the historical GEODE_* names for
    // preset stability. Meaning is REMAPPED (see PluginProcessor gather): CREEP=Scan, FOSSIL=Stretch,
    // POSITION=Start, SILT=Crush, DISTILL=Shape, HAZE=Drive, FRACTURE=Melt (temporal smear). BEDROCK RESERVED.
    // Page 1 (Play):   Scan · Stretch · Sieve · Cut · Shape · Drive
    // Page 2 (Sculpt): Quality · Formant · Tilt · Crush · Start · Melt
    // + Formant-Keep (bool) + Loop + Shape-Target + Cut-Mode + Drive-Mode + Sieve-Mode (choices). × 4 osc.
    constexpr char SYN_OSC_A_GEODE_POSITION[] = "SYN_OSC_A_GEODE_POSITION"; // 0..1 scrub
    constexpr char SYN_OSC_A_GEODE_FOSSIL[]   = "SYN_OSC_A_GEODE_FOSSIL";   // 0..1 freeze
    constexpr char SYN_OSC_A_GEODE_CREEP[]    = "SYN_OSC_A_GEODE_CREEP";    // 0..1 scan rate
    constexpr char SYN_OSC_A_GEODE_SILT[]     = "SYN_OSC_A_GEODE_SILT";     // 0..1 partials<->noise
    constexpr char SYN_OSC_A_GEODE_FORMANT[]  = "SYN_OSC_A_GEODE_FORMANT";  // 0..1 bipolar formant
    constexpr char SYN_OSC_A_GEODE_CUT[]      = "SYN_OSC_A_GEODE_CUT";      // 0..1 spectral LP (1=open)
    constexpr char SYN_OSC_A_GEODE_SIEVE[]    = "SYN_OSC_A_GEODE_SIEVE";    // 0..1 spectral gate
    constexpr char SYN_OSC_A_GEODE_DISTILL[]  = "SYN_OSC_A_GEODE_DISTILL";  // 0..1 trace to N loudest
    constexpr char SYN_OSC_A_GEODE_HAZE[]     = "SYN_OSC_A_GEODE_HAZE";     // 0..1 blur
    constexpr char SYN_OSC_A_GEODE_FRACTURE[] = "SYN_OSC_A_GEODE_FRACTURE"; // 0..1 MELT temporal smear (ID kept for preset safety)
    constexpr char SYN_OSC_A_GEODE_TILT[]     = "SYN_OSC_A_GEODE_TILT";     // 0..1 bipolar tilt
    constexpr char SYN_OSC_A_GEODE_QUALITY[]  = "SYN_OSC_A_GEODE_QUALITY";  // 0..1 partial budget
    constexpr char SYN_OSC_A_GEODE_FKEEP[]    = "SYN_OSC_A_GEODE_FKEEP";    // bool formant-preserve
    constexpr char SYN_OSC_A_GEODE_LOOP[]     = "SYN_OSC_A_GEODE_LOOP";     // choice 0..3
    constexpr char SYN_OSC_A_GEODE_BEDROCK[]  = "SYN_OSC_A_GEODE_BEDROCK";  // RESERVED (unused; tune via right-click semitone)
    constexpr char SYN_OSC_A_GEODE_SHAPE_TARGET[] = "SYN_OSC_A_GEODE_SHAPE_TARGET"; // choice 0=Sine 1=Square 2=Saw
    constexpr char SYN_OSC_A_GEODE_CUT_MODE[]     = "SYN_OSC_A_GEODE_CUT_MODE";     // choice 0=LP 1=HP
    constexpr char SYN_OSC_A_GEODE_DRIVE_MODE[]   = "SYN_OSC_A_GEODE_DRIVE_MODE";   // choice: Saturate Bloom Glint Moire Foldback Ember
    constexpr char SYN_OSC_A_GEODE_SIEVE_MODE[]   = "SYN_OSC_A_GEODE_SIEVE_MODE";   // choice: Floor Sparse Cloak Flicker Rake Parity
    constexpr char SYN_OSC_B_GEODE_POSITION[] = "SYN_OSC_B_GEODE_POSITION";
    constexpr char SYN_OSC_B_GEODE_FOSSIL[]   = "SYN_OSC_B_GEODE_FOSSIL";
    constexpr char SYN_OSC_B_GEODE_CREEP[]    = "SYN_OSC_B_GEODE_CREEP";
    constexpr char SYN_OSC_B_GEODE_SILT[]     = "SYN_OSC_B_GEODE_SILT";
    constexpr char SYN_OSC_B_GEODE_FORMANT[]  = "SYN_OSC_B_GEODE_FORMANT";
    constexpr char SYN_OSC_B_GEODE_CUT[]      = "SYN_OSC_B_GEODE_CUT";
    constexpr char SYN_OSC_B_GEODE_SIEVE[]    = "SYN_OSC_B_GEODE_SIEVE";
    constexpr char SYN_OSC_B_GEODE_DISTILL[]  = "SYN_OSC_B_GEODE_DISTILL";
    constexpr char SYN_OSC_B_GEODE_HAZE[]     = "SYN_OSC_B_GEODE_HAZE";
    constexpr char SYN_OSC_B_GEODE_FRACTURE[] = "SYN_OSC_B_GEODE_FRACTURE";
    constexpr char SYN_OSC_B_GEODE_TILT[]     = "SYN_OSC_B_GEODE_TILT";
    constexpr char SYN_OSC_B_GEODE_QUALITY[]  = "SYN_OSC_B_GEODE_QUALITY";
    constexpr char SYN_OSC_B_GEODE_FKEEP[]    = "SYN_OSC_B_GEODE_FKEEP";
    constexpr char SYN_OSC_B_GEODE_LOOP[]     = "SYN_OSC_B_GEODE_LOOP";
    constexpr char SYN_OSC_B_GEODE_BEDROCK[]  = "SYN_OSC_B_GEODE_BEDROCK";
    constexpr char SYN_OSC_B_GEODE_SHAPE_TARGET[] = "SYN_OSC_B_GEODE_SHAPE_TARGET";
    constexpr char SYN_OSC_B_GEODE_CUT_MODE[]     = "SYN_OSC_B_GEODE_CUT_MODE";
    constexpr char SYN_OSC_B_GEODE_DRIVE_MODE[]   = "SYN_OSC_B_GEODE_DRIVE_MODE";   // choice: Saturate Bloom Glint Moire Foldback Ember
    constexpr char SYN_OSC_B_GEODE_SIEVE_MODE[]   = "SYN_OSC_B_GEODE_SIEVE_MODE";   // choice: Floor Sparse Cloak Flicker Rake Parity
    constexpr char SYN_OSC_C_GEODE_POSITION[] = "SYN_OSC_C_GEODE_POSITION";
    constexpr char SYN_OSC_C_GEODE_FOSSIL[]   = "SYN_OSC_C_GEODE_FOSSIL";
    constexpr char SYN_OSC_C_GEODE_CREEP[]    = "SYN_OSC_C_GEODE_CREEP";
    constexpr char SYN_OSC_C_GEODE_SILT[]     = "SYN_OSC_C_GEODE_SILT";
    constexpr char SYN_OSC_C_GEODE_FORMANT[]  = "SYN_OSC_C_GEODE_FORMANT";
    constexpr char SYN_OSC_C_GEODE_CUT[]      = "SYN_OSC_C_GEODE_CUT";
    constexpr char SYN_OSC_C_GEODE_SIEVE[]    = "SYN_OSC_C_GEODE_SIEVE";
    constexpr char SYN_OSC_C_GEODE_DISTILL[]  = "SYN_OSC_C_GEODE_DISTILL";
    constexpr char SYN_OSC_C_GEODE_HAZE[]     = "SYN_OSC_C_GEODE_HAZE";
    constexpr char SYN_OSC_C_GEODE_FRACTURE[] = "SYN_OSC_C_GEODE_FRACTURE";
    constexpr char SYN_OSC_C_GEODE_TILT[]     = "SYN_OSC_C_GEODE_TILT";
    constexpr char SYN_OSC_C_GEODE_QUALITY[]  = "SYN_OSC_C_GEODE_QUALITY";
    constexpr char SYN_OSC_C_GEODE_FKEEP[]    = "SYN_OSC_C_GEODE_FKEEP";
    constexpr char SYN_OSC_C_GEODE_LOOP[]     = "SYN_OSC_C_GEODE_LOOP";
    constexpr char SYN_OSC_C_GEODE_BEDROCK[]  = "SYN_OSC_C_GEODE_BEDROCK";
    constexpr char SYN_OSC_C_GEODE_SHAPE_TARGET[] = "SYN_OSC_C_GEODE_SHAPE_TARGET";
    constexpr char SYN_OSC_C_GEODE_CUT_MODE[]     = "SYN_OSC_C_GEODE_CUT_MODE";
    constexpr char SYN_OSC_C_GEODE_DRIVE_MODE[]   = "SYN_OSC_C_GEODE_DRIVE_MODE";   // choice: Saturate Bloom Glint Moire Foldback Ember
    constexpr char SYN_OSC_C_GEODE_SIEVE_MODE[]   = "SYN_OSC_C_GEODE_SIEVE_MODE";   // choice: Floor Sparse Cloak Flicker Rake Parity
    constexpr char SYN_OSC_D_GEODE_POSITION[] = "SYN_OSC_D_GEODE_POSITION";
    constexpr char SYN_OSC_D_GEODE_FOSSIL[]   = "SYN_OSC_D_GEODE_FOSSIL";
    constexpr char SYN_OSC_D_GEODE_CREEP[]    = "SYN_OSC_D_GEODE_CREEP";
    constexpr char SYN_OSC_D_GEODE_SILT[]     = "SYN_OSC_D_GEODE_SILT";
    constexpr char SYN_OSC_D_GEODE_FORMANT[]  = "SYN_OSC_D_GEODE_FORMANT";
    constexpr char SYN_OSC_D_GEODE_CUT[]      = "SYN_OSC_D_GEODE_CUT";
    constexpr char SYN_OSC_D_GEODE_SIEVE[]    = "SYN_OSC_D_GEODE_SIEVE";
    constexpr char SYN_OSC_D_GEODE_DISTILL[]  = "SYN_OSC_D_GEODE_DISTILL";
    constexpr char SYN_OSC_D_GEODE_HAZE[]     = "SYN_OSC_D_GEODE_HAZE";
    constexpr char SYN_OSC_D_GEODE_FRACTURE[] = "SYN_OSC_D_GEODE_FRACTURE";
    constexpr char SYN_OSC_D_GEODE_TILT[]     = "SYN_OSC_D_GEODE_TILT";
    constexpr char SYN_OSC_D_GEODE_QUALITY[]  = "SYN_OSC_D_GEODE_QUALITY";
    constexpr char SYN_OSC_D_GEODE_FKEEP[]    = "SYN_OSC_D_GEODE_FKEEP";
    constexpr char SYN_OSC_D_GEODE_LOOP[]     = "SYN_OSC_D_GEODE_LOOP";
    constexpr char SYN_OSC_D_GEODE_BEDROCK[]  = "SYN_OSC_D_GEODE_BEDROCK";
    constexpr char SYN_OSC_D_GEODE_SHAPE_TARGET[] = "SYN_OSC_D_GEODE_SHAPE_TARGET";
    constexpr char SYN_OSC_D_GEODE_CUT_MODE[]     = "SYN_OSC_D_GEODE_CUT_MODE";
    constexpr char SYN_OSC_D_GEODE_DRIVE_MODE[]   = "SYN_OSC_D_GEODE_DRIVE_MODE";   // choice: Saturate Bloom Glint Moire Foldback Ember
    constexpr char SYN_OSC_D_GEODE_SIEVE_MODE[]   = "SYN_OSC_D_GEODE_SIEVE_MODE";   // choice: Floor Sparse Cloak Flicker Rake Parity

    // ════════ HARM-ENGINE-IDS — per-OSC HARMONIC additive oscillator (Engine::HARM, slot 5;
    // the never-exposed NOISE engine slot, ID frozen + meaning remapped — house precedent).
    // 2 mode rows (MAIN / SCULPT) + 12 knob laws; DSP in HarmonicEngine.h (2026-07-08). ════════
    constexpr char SYN_OSC_A_HARM_MODE[]         = "SYN_OSC_A_HARM_MODE";   // choice: Blade Neon Console Chant Bronze Hornet
    constexpr char SYN_OSC_A_HARM_SCULPT[]       = "SYN_OSC_A_HARM_SCULPT";   // choice: Keel Splay Cull Tide Terrace Clang
    constexpr char SYN_OSC_A_HARM_HUE[]          = "SYN_OSC_A_HARM_HUE";   // 0..1 main-family regime morph
    constexpr char SYN_OSC_A_HARM_COUNT[]        = "SYN_OSC_A_HARM_COUNT";   // 0..1 partial count (log 8..512)
    constexpr char SYN_OSC_A_HARM_LEAN[]         = "SYN_OSC_A_HARM_LEAN";   // 0..1 bipolar tilt (0.5 flat)
    constexpr char SYN_OSC_A_HARM_FAN[]          = "SYN_OSC_A_HARM_FAN";   // 0..1 stereo: split -> golden orbit
    constexpr char SYN_OSC_A_HARM_GRIT[]         = "SYN_OSC_A_HARM_GRIT";   // 0..1 analog life (banded OU)
    constexpr char SYN_OSC_A_HARM_BRAID[]        = "SYN_OSC_A_HARM_BRAID";   // 0..1 ensemble twin pairs
    constexpr char SYN_OSC_A_HARM_CARVE[]        = "SYN_OSC_A_HARM_CARVE";   // 0..1 sculpt regime depth
    constexpr char SYN_OSC_A_HARM_CHURN[]        = "SYN_OSC_A_HARM_CHURN";   // 0..1 motion rate (0.5 = 1x)
    constexpr char SYN_OSC_A_HARM_ROOT[]         = "SYN_OSC_A_HARM_ROOT";   // 0..1 fund guard -> sub ghost
    constexpr char SYN_OSC_A_HARM_SHINE[]        = "SYN_OSC_A_HARM_SHINE";   // 0..1 +oct detuned ghost
    constexpr char SYN_OSC_A_HARM_WILT[]         = "SYN_OSC_A_HARM_WILT";   // 0..1 time arrow (0.5 off)
    constexpr char SYN_OSC_A_HARM_FIZZ[]         = "SYN_OSC_A_HARM_FIZZ";   // 0..1 per-partial noise fur
    constexpr char SYN_OSC_B_HARM_MODE[]         = "SYN_OSC_B_HARM_MODE";
    constexpr char SYN_OSC_B_HARM_SCULPT[]       = "SYN_OSC_B_HARM_SCULPT";
    constexpr char SYN_OSC_B_HARM_HUE[]          = "SYN_OSC_B_HARM_HUE";
    constexpr char SYN_OSC_B_HARM_COUNT[]        = "SYN_OSC_B_HARM_COUNT";
    constexpr char SYN_OSC_B_HARM_LEAN[]         = "SYN_OSC_B_HARM_LEAN";
    constexpr char SYN_OSC_B_HARM_FAN[]          = "SYN_OSC_B_HARM_FAN";
    constexpr char SYN_OSC_B_HARM_GRIT[]         = "SYN_OSC_B_HARM_GRIT";
    constexpr char SYN_OSC_B_HARM_BRAID[]        = "SYN_OSC_B_HARM_BRAID";
    constexpr char SYN_OSC_B_HARM_CARVE[]        = "SYN_OSC_B_HARM_CARVE";
    constexpr char SYN_OSC_B_HARM_CHURN[]        = "SYN_OSC_B_HARM_CHURN";
    constexpr char SYN_OSC_B_HARM_ROOT[]         = "SYN_OSC_B_HARM_ROOT";
    constexpr char SYN_OSC_B_HARM_SHINE[]        = "SYN_OSC_B_HARM_SHINE";
    constexpr char SYN_OSC_B_HARM_WILT[]         = "SYN_OSC_B_HARM_WILT";
    constexpr char SYN_OSC_B_HARM_FIZZ[]         = "SYN_OSC_B_HARM_FIZZ";
    constexpr char SYN_OSC_C_HARM_MODE[]         = "SYN_OSC_C_HARM_MODE";
    constexpr char SYN_OSC_C_HARM_SCULPT[]       = "SYN_OSC_C_HARM_SCULPT";
    constexpr char SYN_OSC_C_HARM_HUE[]          = "SYN_OSC_C_HARM_HUE";
    constexpr char SYN_OSC_C_HARM_COUNT[]        = "SYN_OSC_C_HARM_COUNT";
    constexpr char SYN_OSC_C_HARM_LEAN[]         = "SYN_OSC_C_HARM_LEAN";
    constexpr char SYN_OSC_C_HARM_FAN[]          = "SYN_OSC_C_HARM_FAN";
    constexpr char SYN_OSC_C_HARM_GRIT[]         = "SYN_OSC_C_HARM_GRIT";
    constexpr char SYN_OSC_C_HARM_BRAID[]        = "SYN_OSC_C_HARM_BRAID";
    constexpr char SYN_OSC_C_HARM_CARVE[]        = "SYN_OSC_C_HARM_CARVE";
    constexpr char SYN_OSC_C_HARM_CHURN[]        = "SYN_OSC_C_HARM_CHURN";
    constexpr char SYN_OSC_C_HARM_ROOT[]         = "SYN_OSC_C_HARM_ROOT";
    constexpr char SYN_OSC_C_HARM_SHINE[]        = "SYN_OSC_C_HARM_SHINE";
    constexpr char SYN_OSC_C_HARM_WILT[]         = "SYN_OSC_C_HARM_WILT";
    constexpr char SYN_OSC_C_HARM_FIZZ[]         = "SYN_OSC_C_HARM_FIZZ";
    constexpr char SYN_OSC_D_HARM_MODE[]         = "SYN_OSC_D_HARM_MODE";
    constexpr char SYN_OSC_D_HARM_SCULPT[]       = "SYN_OSC_D_HARM_SCULPT";
    constexpr char SYN_OSC_D_HARM_HUE[]          = "SYN_OSC_D_HARM_HUE";
    constexpr char SYN_OSC_D_HARM_COUNT[]        = "SYN_OSC_D_HARM_COUNT";
    constexpr char SYN_OSC_D_HARM_LEAN[]         = "SYN_OSC_D_HARM_LEAN";
    constexpr char SYN_OSC_D_HARM_FAN[]          = "SYN_OSC_D_HARM_FAN";
    constexpr char SYN_OSC_D_HARM_GRIT[]         = "SYN_OSC_D_HARM_GRIT";
    constexpr char SYN_OSC_D_HARM_BRAID[]        = "SYN_OSC_D_HARM_BRAID";
    constexpr char SYN_OSC_D_HARM_CARVE[]        = "SYN_OSC_D_HARM_CARVE";
    constexpr char SYN_OSC_D_HARM_CHURN[]        = "SYN_OSC_D_HARM_CHURN";
    constexpr char SYN_OSC_D_HARM_ROOT[]         = "SYN_OSC_D_HARM_ROOT";
    constexpr char SYN_OSC_D_HARM_SHINE[]        = "SYN_OSC_D_HARM_SHINE";
    constexpr char SYN_OSC_D_HARM_WILT[]         = "SYN_OSC_D_HARM_WILT";
    constexpr char SYN_OSC_D_HARM_FIZZ[]         = "SYN_OSC_D_HARM_FIZZ";

    // ── UNIVERSAL OSC BOXES (2026-07-09) — COARSE + SUB on every oscillator ──
    // COARSE: continuous ±64 st pitch lane, NO snap — the smooth modulatable pitch
    // destination (Serum-2 CRS insight). Sums on top of Oct/Semi/Cent (cents lane).
    // SUB: single voice-anchored sub osc — Range(-1/-2/-3 oct) · Form(Sine/Tri/Sq/Saw)
    //      · Weight(level, exp-bias) · Heat(ADAA-tanh light drive). Default OFF.
    // (The retired PHASE_MODE + ROUTE_* ids above stay registered — IDs FROZEN — but
    //  their tiles are gone: phase is hardwired FREE, the Note→Frame route is dead.)
    constexpr char SYN_OSC_A_COARSE[]     = "SYN_OSC_A_COARSE";      // float -64..+64 st, default 0
    constexpr char SYN_OSC_B_COARSE[]     = "SYN_OSC_B_COARSE";
    constexpr char SYN_OSC_C_COARSE[]     = "SYN_OSC_C_COARSE";
    constexpr char SYN_OSC_D_COARSE[]     = "SYN_OSC_D_COARSE";
    constexpr char SYN_OSC_A_SUB_RANGE[]  = "SYN_OSC_A_SUB_RANGE";   // choice 0=-1oct 1=-2oct 2=-3oct
    constexpr char SYN_OSC_B_SUB_RANGE[]  = "SYN_OSC_B_SUB_RANGE";
    constexpr char SYN_OSC_C_SUB_RANGE[]  = "SYN_OSC_C_SUB_RANGE";
    constexpr char SYN_OSC_D_SUB_RANGE[]  = "SYN_OSC_D_SUB_RANGE";
    constexpr char SYN_OSC_A_SUB_FORM[]   = "SYN_OSC_A_SUB_FORM";    // choice 0=Sine 1=Tri 2=Square 3=Saw
    constexpr char SYN_OSC_B_SUB_FORM[]   = "SYN_OSC_B_SUB_FORM";
    constexpr char SYN_OSC_C_SUB_FORM[]   = "SYN_OSC_C_SUB_FORM";
    constexpr char SYN_OSC_D_SUB_FORM[]   = "SYN_OSC_D_SUB_FORM";
    constexpr char SYN_OSC_A_SUB_WEIGHT[] = "SYN_OSC_A_SUB_WEIGHT";  // float 0..1, default 0 (off)
    constexpr char SYN_OSC_B_SUB_WEIGHT[] = "SYN_OSC_B_SUB_WEIGHT";
    constexpr char SYN_OSC_C_SUB_WEIGHT[] = "SYN_OSC_C_SUB_WEIGHT";
    constexpr char SYN_OSC_D_SUB_WEIGHT[] = "SYN_OSC_D_SUB_WEIGHT";
    constexpr char SYN_OSC_A_SUB_HEAT[]   = "SYN_OSC_A_SUB_HEAT";    // float 0..1, default 0
    constexpr char SYN_OSC_B_SUB_HEAT[]   = "SYN_OSC_B_SUB_HEAT";
    constexpr char SYN_OSC_C_SUB_HEAT[]   = "SYN_OSC_C_SUB_HEAT";
    constexpr char SYN_OSC_D_SUB_HEAT[]   = "SYN_OSC_D_SUB_HEAT";

    // ════════ NOISE ENGINE (2026-07-15) — one shared noise source (center module), routed via N ════════
    // Written straight to APVTS via setSynParam (relay-bypass — we're past the ~700 WebSliderRelay ceiling).
    constexpr char SYN_NOISE_ON[]    = "SYN_NOISE_ON";      // bool — module enabled
    constexpr char SYN_NOISE_TYPE[]  = "SYN_NOISE_TYPE";    // choice — White/Pink/Brown/Geiger/Tape…/Vinyl…/Space…
    constexpr char SYN_NOISE_LEVEL[] = "SYN_NOISE_LEVEL";   // float 0..1
    constexpr char SYN_NOISE_PITCH[] = "SYN_NOISE_PITCH";   // float 0..1 (0.5 = nominal; tone/brightness for colors)
    constexpr char SYN_NOISE_PAN[]   = "SYN_NOISE_PAN";     // float 0..1 (0.5 = center)
    constexpr char SYN_NOISE_PLAYMODE[] = "SYN_NOISE_PLAYMODE";  // fb66 — choice: 0 Random (random start/note) · 1 Envelope (one-shot/note) · 2 Free (global always-running tape)
    constexpr char SYN_NOISE_WIDTH[] = "SYN_NOISE_WIDTH";        // fb69 — float 0..2 stereo width (M/S): 0 mono · 1 normal · 2 wide

    // ════════ MODAL-ENGINE-IDS — per-OSC physical modeling (Engine::MODAL, slot 6) ════════
    // 3 selectors (Family ×9 · Form ×5 · Source ×4) + 10 knobs, ×4 oscs = 52 params.
    constexpr char SYN_OSC_A_MODAL_FAMILY[]   = "SYN_OSC_A_MODAL_FAMILY";
    constexpr char SYN_OSC_A_MODAL_FORM[]     = "SYN_OSC_A_MODAL_FORM";
    constexpr char SYN_OSC_A_MODAL_SOURCE[]   = "SYN_OSC_A_MODAL_SOURCE";
    constexpr char SYN_OSC_A_MODAL_HARD[]     = "SYN_OSC_A_MODAL_HARD";
    constexpr char SYN_OSC_A_MODAL_POS[]      = "SYN_OSC_A_MODAL_POS";
    constexpr char SYN_OSC_A_MODAL_DECAY[]    = "SYN_OSC_A_MODAL_DECAY";
    constexpr char SYN_OSC_A_MODAL_MATERIAL[] = "SYN_OSC_A_MODAL_MATERIAL";
    constexpr char SYN_OSC_A_MODAL_BREATH[]   = "SYN_OSC_A_MODAL_BREATH";
    constexpr char SYN_OSC_A_MODAL_STRETCH[]  = "SYN_OSC_A_MODAL_STRETCH";
    constexpr char SYN_OSC_A_MODAL_BLOOM[]    = "SYN_OSC_A_MODAL_BLOOM";
    constexpr char SYN_OSC_A_MODAL_HALO[]     = "SYN_OSC_A_MODAL_HALO";
    constexpr char SYN_OSC_A_MODAL_AGE[]      = "SYN_OSC_A_MODAL_AGE";
    constexpr char SYN_OSC_A_MODAL_BODY[]     = "SYN_OSC_A_MODAL_BODY";
    constexpr char SYN_OSC_B_MODAL_FAMILY[]   = "SYN_OSC_B_MODAL_FAMILY";
    constexpr char SYN_OSC_B_MODAL_FORM[]     = "SYN_OSC_B_MODAL_FORM";
    constexpr char SYN_OSC_B_MODAL_SOURCE[]   = "SYN_OSC_B_MODAL_SOURCE";
    constexpr char SYN_OSC_B_MODAL_HARD[]     = "SYN_OSC_B_MODAL_HARD";
    constexpr char SYN_OSC_B_MODAL_POS[]      = "SYN_OSC_B_MODAL_POS";
    constexpr char SYN_OSC_B_MODAL_DECAY[]    = "SYN_OSC_B_MODAL_DECAY";
    constexpr char SYN_OSC_B_MODAL_MATERIAL[] = "SYN_OSC_B_MODAL_MATERIAL";
    constexpr char SYN_OSC_B_MODAL_BREATH[]   = "SYN_OSC_B_MODAL_BREATH";
    constexpr char SYN_OSC_B_MODAL_STRETCH[]  = "SYN_OSC_B_MODAL_STRETCH";
    constexpr char SYN_OSC_B_MODAL_BLOOM[]    = "SYN_OSC_B_MODAL_BLOOM";
    constexpr char SYN_OSC_B_MODAL_HALO[]     = "SYN_OSC_B_MODAL_HALO";
    constexpr char SYN_OSC_B_MODAL_AGE[]      = "SYN_OSC_B_MODAL_AGE";
    constexpr char SYN_OSC_B_MODAL_BODY[]     = "SYN_OSC_B_MODAL_BODY";
    constexpr char SYN_OSC_C_MODAL_FAMILY[]   = "SYN_OSC_C_MODAL_FAMILY";
    constexpr char SYN_OSC_C_MODAL_FORM[]     = "SYN_OSC_C_MODAL_FORM";
    constexpr char SYN_OSC_C_MODAL_SOURCE[]   = "SYN_OSC_C_MODAL_SOURCE";
    constexpr char SYN_OSC_C_MODAL_HARD[]     = "SYN_OSC_C_MODAL_HARD";
    constexpr char SYN_OSC_C_MODAL_POS[]      = "SYN_OSC_C_MODAL_POS";
    constexpr char SYN_OSC_C_MODAL_DECAY[]    = "SYN_OSC_C_MODAL_DECAY";
    constexpr char SYN_OSC_C_MODAL_MATERIAL[] = "SYN_OSC_C_MODAL_MATERIAL";
    constexpr char SYN_OSC_C_MODAL_BREATH[]   = "SYN_OSC_C_MODAL_BREATH";
    constexpr char SYN_OSC_C_MODAL_STRETCH[]  = "SYN_OSC_C_MODAL_STRETCH";
    constexpr char SYN_OSC_C_MODAL_BLOOM[]    = "SYN_OSC_C_MODAL_BLOOM";
    constexpr char SYN_OSC_C_MODAL_HALO[]     = "SYN_OSC_C_MODAL_HALO";
    constexpr char SYN_OSC_C_MODAL_AGE[]      = "SYN_OSC_C_MODAL_AGE";
    constexpr char SYN_OSC_C_MODAL_BODY[]     = "SYN_OSC_C_MODAL_BODY";
    constexpr char SYN_OSC_D_MODAL_FAMILY[]   = "SYN_OSC_D_MODAL_FAMILY";
    constexpr char SYN_OSC_D_MODAL_FORM[]     = "SYN_OSC_D_MODAL_FORM";
    constexpr char SYN_OSC_D_MODAL_SOURCE[]   = "SYN_OSC_D_MODAL_SOURCE";
    constexpr char SYN_OSC_D_MODAL_HARD[]     = "SYN_OSC_D_MODAL_HARD";
    constexpr char SYN_OSC_D_MODAL_POS[]      = "SYN_OSC_D_MODAL_POS";
    constexpr char SYN_OSC_D_MODAL_DECAY[]    = "SYN_OSC_D_MODAL_DECAY";
    constexpr char SYN_OSC_D_MODAL_MATERIAL[] = "SYN_OSC_D_MODAL_MATERIAL";
    constexpr char SYN_OSC_D_MODAL_BREATH[]   = "SYN_OSC_D_MODAL_BREATH";
    constexpr char SYN_OSC_D_MODAL_STRETCH[]  = "SYN_OSC_D_MODAL_STRETCH";
    constexpr char SYN_OSC_D_MODAL_BLOOM[]    = "SYN_OSC_D_MODAL_BLOOM";
    constexpr char SYN_OSC_D_MODAL_HALO[]     = "SYN_OSC_D_MODAL_HALO";
    constexpr char SYN_OSC_D_MODAL_AGE[]      = "SYN_OSC_D_MODAL_AGE";
    constexpr char SYN_OSC_D_MODAL_BODY[]     = "SYN_OSC_D_MODAL_BODY";

    // ── BLEND MODES (Serum-2-style cross-osc warp) — 4 slots (B1..B4) × 4 oscs, any-to-any.
    //    Each slot = MODE (Off/FM/PD/AM/RM/Sync/Warp/Dist/Filter) + SRC (Osc A..D/Sub/Noise/Self) + DEPTH 0..1.
    //    Fresh WSLOT_ prefix — do NOT reuse SYN_OSC_*_BLEND_* (that's the offline sample-baker). ──
    constexpr char SYN_OSC_A_WSLOT1_MODE[] = "SYN_OSC_A_WSLOT1_MODE";  constexpr char SYN_OSC_A_WSLOT1_SRC[] = "SYN_OSC_A_WSLOT1_SRC";  constexpr char SYN_OSC_A_WSLOT1_DEPTH[] = "SYN_OSC_A_WSLOT1_DEPTH";
    constexpr char SYN_OSC_A_WSLOT2_MODE[] = "SYN_OSC_A_WSLOT2_MODE";  constexpr char SYN_OSC_A_WSLOT2_SRC[] = "SYN_OSC_A_WSLOT2_SRC";  constexpr char SYN_OSC_A_WSLOT2_DEPTH[] = "SYN_OSC_A_WSLOT2_DEPTH";
    constexpr char SYN_OSC_A_WSLOT3_MODE[] = "SYN_OSC_A_WSLOT3_MODE";  constexpr char SYN_OSC_A_WSLOT3_SRC[] = "SYN_OSC_A_WSLOT3_SRC";  constexpr char SYN_OSC_A_WSLOT3_DEPTH[] = "SYN_OSC_A_WSLOT3_DEPTH";
    constexpr char SYN_OSC_A_WSLOT4_MODE[] = "SYN_OSC_A_WSLOT4_MODE";  constexpr char SYN_OSC_A_WSLOT4_SRC[] = "SYN_OSC_A_WSLOT4_SRC";  constexpr char SYN_OSC_A_WSLOT4_DEPTH[] = "SYN_OSC_A_WSLOT4_DEPTH";
    constexpr char SYN_OSC_B_WSLOT1_MODE[] = "SYN_OSC_B_WSLOT1_MODE";  constexpr char SYN_OSC_B_WSLOT1_SRC[] = "SYN_OSC_B_WSLOT1_SRC";  constexpr char SYN_OSC_B_WSLOT1_DEPTH[] = "SYN_OSC_B_WSLOT1_DEPTH";
    constexpr char SYN_OSC_B_WSLOT2_MODE[] = "SYN_OSC_B_WSLOT2_MODE";  constexpr char SYN_OSC_B_WSLOT2_SRC[] = "SYN_OSC_B_WSLOT2_SRC";  constexpr char SYN_OSC_B_WSLOT2_DEPTH[] = "SYN_OSC_B_WSLOT2_DEPTH";
    constexpr char SYN_OSC_B_WSLOT3_MODE[] = "SYN_OSC_B_WSLOT3_MODE";  constexpr char SYN_OSC_B_WSLOT3_SRC[] = "SYN_OSC_B_WSLOT3_SRC";  constexpr char SYN_OSC_B_WSLOT3_DEPTH[] = "SYN_OSC_B_WSLOT3_DEPTH";
    constexpr char SYN_OSC_B_WSLOT4_MODE[] = "SYN_OSC_B_WSLOT4_MODE";  constexpr char SYN_OSC_B_WSLOT4_SRC[] = "SYN_OSC_B_WSLOT4_SRC";  constexpr char SYN_OSC_B_WSLOT4_DEPTH[] = "SYN_OSC_B_WSLOT4_DEPTH";
    constexpr char SYN_OSC_C_WSLOT1_MODE[] = "SYN_OSC_C_WSLOT1_MODE";  constexpr char SYN_OSC_C_WSLOT1_SRC[] = "SYN_OSC_C_WSLOT1_SRC";  constexpr char SYN_OSC_C_WSLOT1_DEPTH[] = "SYN_OSC_C_WSLOT1_DEPTH";
    constexpr char SYN_OSC_C_WSLOT2_MODE[] = "SYN_OSC_C_WSLOT2_MODE";  constexpr char SYN_OSC_C_WSLOT2_SRC[] = "SYN_OSC_C_WSLOT2_SRC";  constexpr char SYN_OSC_C_WSLOT2_DEPTH[] = "SYN_OSC_C_WSLOT2_DEPTH";
    constexpr char SYN_OSC_C_WSLOT3_MODE[] = "SYN_OSC_C_WSLOT3_MODE";  constexpr char SYN_OSC_C_WSLOT3_SRC[] = "SYN_OSC_C_WSLOT3_SRC";  constexpr char SYN_OSC_C_WSLOT3_DEPTH[] = "SYN_OSC_C_WSLOT3_DEPTH";
    constexpr char SYN_OSC_C_WSLOT4_MODE[] = "SYN_OSC_C_WSLOT4_MODE";  constexpr char SYN_OSC_C_WSLOT4_SRC[] = "SYN_OSC_C_WSLOT4_SRC";  constexpr char SYN_OSC_C_WSLOT4_DEPTH[] = "SYN_OSC_C_WSLOT4_DEPTH";
    constexpr char SYN_OSC_D_WSLOT1_MODE[] = "SYN_OSC_D_WSLOT1_MODE";  constexpr char SYN_OSC_D_WSLOT1_SRC[] = "SYN_OSC_D_WSLOT1_SRC";  constexpr char SYN_OSC_D_WSLOT1_DEPTH[] = "SYN_OSC_D_WSLOT1_DEPTH";
    constexpr char SYN_OSC_D_WSLOT2_MODE[] = "SYN_OSC_D_WSLOT2_MODE";  constexpr char SYN_OSC_D_WSLOT2_SRC[] = "SYN_OSC_D_WSLOT2_SRC";  constexpr char SYN_OSC_D_WSLOT2_DEPTH[] = "SYN_OSC_D_WSLOT2_DEPTH";
    constexpr char SYN_OSC_D_WSLOT3_MODE[] = "SYN_OSC_D_WSLOT3_MODE";  constexpr char SYN_OSC_D_WSLOT3_SRC[] = "SYN_OSC_D_WSLOT3_SRC";  constexpr char SYN_OSC_D_WSLOT3_DEPTH[] = "SYN_OSC_D_WSLOT3_DEPTH";
    constexpr char SYN_OSC_D_WSLOT4_MODE[] = "SYN_OSC_D_WSLOT4_MODE";  constexpr char SYN_OSC_D_WSLOT4_SRC[] = "SYN_OSC_D_WSLOT4_SRC";  constexpr char SYN_OSC_D_WSLOT4_DEPTH[] = "SYN_OSC_D_WSLOT4_DEPTH";

    // ════════ fb346 — MULTI-INSTANCE FX (the dynamic chain) ════════
    // 🔑 THE HOST CONSTRAINT: JUCE/VST3/AU cache the parameter list when the plugin loads — params
    // can NEVER be created at runtime. So every extra device instance is PRE-ALLOCATED at startup
    // and merely CLAIMED by the + button. The user only ever sees a rack that grows.
    //
    // Instance 1 == the legacy SYN_DLY_* / SYN_DST_* / SYN_RVB_* blocks above, untouched, so all 66
    // factory presets and every user preset keep loading byte-identically. Instances 2..N are
    // generated with the prefix "SYN_DLY2_", "SYN_DLY3_", … (see createParameterLayout).
    //
    // Per instance, beyond the device's own params:
    //   _ACTIVE  bool  — is this instance IN the chain at all (the + adds / the × removes).
    //                    Distinct from _POWER, which is bypass (the power dot).
    //   _RANK    float — chain position. Sorting by a float rank is what makes drag-reorder legal:
    //                    a choice param's cardinality is fixed at birth (the fb342 law), so the old
    //                    6-way SYN_FX_ORDER could never grow past 3 devices. Rank has no cardinality.
    // ── fb377 — FILTER, chain kind 5. Instance 1 is "SYN_FLT_", 2..6 are "SYN_FLT2_".."SYN_FLT6_",
    //    exactly the pooling grammar every other device uses. Declared in PluginProcessor.cpp's
    //    FX block; listed here so the prefix is greppable and so nobody invents a second one.
    //    🔑 ENGINE is choice(94) over terrainFilterEngineNames() and is the ONLY engine selector
    //    the DSP reads; TYPE is the header pill's GROUP and is a UI display mirror, never read
    //    by C++. SYN_FLT_* does not collide with the synth panel's SYN_FILTER1_/SYN_FILTER2_.
    constexpr char SYN_FLT_ENGINE[] = "SYN_FLT_ENGINE";
    constexpr char SYN_FLT_TYPE[]   = "SYN_FLT_TYPE";
    constexpr char SYN_FLT_POWER[]  = "SYN_FLT_POWER";
    constexpr char SYN_FLT_ACTIVE[] = "SYN_FLT_ACTIVE";
    constexpr char SYN_FLT_RANK[]   = "SYN_FLT_RANK";

    // ── fb413 — CHORUS (kind 6), FLANGER (kind 7), PHASER (kind 8). Three separate devices
    //    (CONTRACT.md R1), each with the standard pooling grammar: instance 1 is "SYN_CHO_",
    //    2..6 are "SYN_CHO2_".."SYN_CHO6_". Declared in PluginProcessor.cpp's FX block; listed
    //    here so the prefixes are greppable and nobody invents a second set.
    //    ⚠️ SYN_CHO_* does NOT collide with the front page's own chorus (CHORUS_AMOUNT /
    //    CHORUS_WIDTH / CHORUS_CHARACTER above), which stays exactly where it is — its voicing
    //    is reused as the rack chorus's `Vintage` Type (R7), not moved.
    //    ⚠️ SYN_FLA_FEEDBACK IS BIPOLAR WITH 0.5 AS CENTRE. A unipolar 0 default wired to it is
    //    −99 % regeneration, not none (flanger ROSTER §3, and the reason it defaults to 0.5f).
    //    🔑 There is no SYNCDIV on any of the three: all three engines fold the sync division
    //    into the RATE knob over the house 20-entry list, so two selectors can never disagree.
    constexpr char SYN_CHO_TYPE[]   = "SYN_CHO_TYPE";    constexpr char SYN_CHO_POWER[]  = "SYN_CHO_POWER";
    constexpr char SYN_CHO_ACTIVE[] = "SYN_CHO_ACTIVE";  constexpr char SYN_CHO_RANK[]   = "SYN_CHO_RANK";
    constexpr char SYN_FLA_TYPE[]   = "SYN_FLA_TYPE";    constexpr char SYN_FLA_POWER[]  = "SYN_FLA_POWER";
    constexpr char SYN_FLA_ACTIVE[] = "SYN_FLA_ACTIVE";  constexpr char SYN_FLA_RANK[]   = "SYN_FLA_RANK";
    constexpr char SYN_PHA_TYPE[]   = "SYN_PHA_TYPE";    constexpr char SYN_PHA_POWER[]  = "SYN_PHA_POWER";
    constexpr char SYN_PHA_ACTIVE[] = "SYN_PHA_ACTIVE";  constexpr char SYN_PHA_RANK[]   = "SYN_PHA_RANK";

    // ── fb426 — THE fx4 FAMILY: Equalizer 9 · Widen 10 · Compress 11 · OTT 12.
    //    Same pooling grammar: instance 1 is "SYN_EQZ_", 2..6 are "SYN_EQZ2_".."SYN_EQZ6_".
    //    ⚠️ SYN_EQZ_* is deliberately NOT the synth panel's EQ_B* family. The two surfaces stay
    //    independently automatable and preset-able — no-doubles applies to param IDs too, and the
    //    panel EQ keeps its own engine (ParametricEQ.h) untouched.
    //    🔑 CARDINALITY IS FROZEN AT BIRTH (rack law C). All four *_TYPE are declared choice(16)
    //    with reserved padding and all four axis params choice(8), matching the fb413/fb418 house
    //    precedent (Chorus 16/8 live · Flanger 16/6 · Phaser 16/9 · Filter 16/10). Live counts
    //    today are EQ 7 · Widen 6 · Compress 8 · OTT 8 Types, and every engine clamps its own
    //    index, so the reserve costs nothing and is the only moment it can be bought.
    //    🔑 Read every choice as (int)*rawParam(id) — NEVER round(v*(N-1)). Normalising on a
    //    dropdown's option count instead of the PARAM's cardinality is fb373, which shipped
    //    `Cassette` playing `Studio` through four rounds of green measurement.
    constexpr char SYN_EQZ_TYPE[]   = "SYN_EQZ_TYPE";    constexpr char SYN_EQZ_POWER[]  = "SYN_EQZ_POWER";
    constexpr char SYN_EQZ_ACTIVE[] = "SYN_EQZ_ACTIVE";  constexpr char SYN_EQZ_RANK[]   = "SYN_EQZ_RANK";
    constexpr char SYN_WID_TYPE[]   = "SYN_WID_TYPE";    constexpr char SYN_WID_POWER[]  = "SYN_WID_POWER";
    constexpr char SYN_WID_ACTIVE[] = "SYN_WID_ACTIVE";  constexpr char SYN_WID_RANK[]   = "SYN_WID_RANK";
    constexpr char SYN_CMP_TYPE[]   = "SYN_CMP_TYPE";    constexpr char SYN_CMP_POWER[]  = "SYN_CMP_POWER";
    constexpr char SYN_CMP_ACTIVE[] = "SYN_CMP_ACTIVE";  constexpr char SYN_CMP_RANK[]   = "SYN_CMP_RANK";
    constexpr char SYN_OTT_TYPE[]   = "SYN_OTT_TYPE";    constexpr char SYN_OTT_POWER[]  = "SYN_OTT_POWER";
    constexpr char SYN_OTT_ACTIVE[] = "SYN_OTT_ACTIVE";  constexpr char SYN_OTT_RANK[]   = "SYN_OTT_RANK";
    // fb444 — BODE (kind 13). The layout builder composes these from string literals, so this
    // block exists to make the family greppable, not because anything links against it.
    //   TYPE CHAR ROUTE · SHIFT DIR FDBK MIX · FINE SPREAD TIME BLUR LOWKEEP DAMPING TOUCH DRIFT
    //   · GUARD SYNC · SRC_* · POWER ACTIVE RANK SEND
    constexpr char SYN_BOD_TYPE[]   = "SYN_BOD_TYPE";    constexpr char SYN_BOD_POWER[]  = "SYN_BOD_POWER";
    constexpr char SYN_BOD_ACTIVE[] = "SYN_BOD_ACTIVE";  constexpr char SYN_BOD_RANK[]   = "SYN_BOD_RANK";

    constexpr int  kFxInstances = 6;                  // per device type (1 legacy + 5 extra)
    constexpr char SYN_FX_RANK_SUFFIX[]   = "_RANK";
    constexpr char SYN_FX_ACTIVE_SUFFIX[] = "_ACTIVE";
    // Legacy instance-1 chain membership (the three shipped devices).
    constexpr char SYN_RVB_ACTIVE[] = "SYN_RVB_ACTIVE";   constexpr char SYN_RVB_RANK[] = "SYN_RVB_RANK";
    constexpr char SYN_DLY_ACTIVE[] = "SYN_DLY_ACTIVE";   constexpr char SYN_DLY_RANK[] = "SYN_DLY_RANK";
    constexpr char SYN_DST_ACTIVE[] = "SYN_DST_ACTIVE";   constexpr char SYN_DST_RANK[] = "SYN_DST_RANK";
}
