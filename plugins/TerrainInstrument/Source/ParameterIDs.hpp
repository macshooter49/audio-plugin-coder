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
    constexpr char ATTACK_MS[]    = "ATTACK_MS";
    constexpr char RELEASE_MS[]   = "RELEASE_MS";
    constexpr char ROOT_NOTE[]    = "ROOT_NOTE";
    constexpr char SLICE_MODE[]   = "SLICE_MODE";  // 0=PITCH, 1=SLICE (SLICE inert in v0a)
}
