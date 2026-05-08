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
    constexpr char WOW_FLUTTER[]  = "WOW_FLUTTER";
    constexpr char SATURATION[]   = "SATURATION";
    constexpr char HISS[]         = "HISS";
    constexpr char TAPE_MACHINE[] = "TAPE_MACHINE";
    constexpr char OUTPUT_GAIN[]  = "OUTPUT_GAIN";
    constexpr char MASTER_MIX[]   = "MASTER_MIX";

    // Space reverb
    constexpr char SPACE_SIZE[]   = "SPACE_SIZE";
    constexpr char SPACE_DECAY[]  = "SPACE_DECAY";
    constexpr char SPACE_TONE[]   = "SPACE_TONE";
    constexpr char SPACE_MIX[]    = "SPACE_MIX";

    // 3-band EQ
    constexpr char EQ_LOW_FREQ[]  = "EQ_LOW_FREQ";
    constexpr char EQ_LOW_GAIN[]  = "EQ_LOW_GAIN";
    constexpr char EQ_MID_FREQ[]  = "EQ_MID_FREQ";
    constexpr char EQ_MID_GAIN[]  = "EQ_MID_GAIN";
    constexpr char EQ_HIGH_FREQ[] = "EQ_HIGH_FREQ";
    constexpr char EQ_HIGH_GAIN[] = "EQ_HIGH_GAIN";

    // Sampler (Terrain Instrument additions — v0a)
    constexpr char ATTACK_MS[]    = "ATTACK_MS";
    constexpr char RELEASE_MS[]   = "RELEASE_MS";
    constexpr char ROOT_NOTE[]    = "ROOT_NOTE";
    constexpr char SLICE_MODE[]   = "SLICE_MODE";  // 0=PITCH, 1=SLICE (SLICE inert in v0a)
}
