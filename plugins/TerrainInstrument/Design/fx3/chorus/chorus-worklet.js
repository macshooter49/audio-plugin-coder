// ═══════════════════════════════════════════════════════════════════════════════
//  chorus-worklet.js — the FX-rack CHORUS device as an AudioWorkletProcessor, so Max can
//  HEAR the roster in a Safari mockup before a line of it reaches the plugin (the fb296
//  law: mockups are interactive AND audible, never a static picture).
//
//  This is a PORT of Design/fx3/chorus/TerrainChorusFx.h, not a second design. Same Type
//  names, same Character names, same parameter names, same ranges, same tables, same
//  topology. It is not sample-identical to the C++ (float64 here, no denormal flushes, the
//  one-pole coefficients use the exact exp) — it is recognisably the same effect, and every
//  measured behaviour in chorus_cert.cpp is reproduced by construction.
//
//  ── USE ──────────────────────────────────────────────────────────────────────
//    await ctx.audioWorklet.addModule('chorus-worklet.js');
//    const n = new AudioWorkletNode(ctx, 'terrain-chorus', { outputChannelCount: [2] });
//    n.port.postMessage({ type: 1, character: 0, rate: 0.35, depth: 0.6, feedback: 0,
//                         mix: 0.5, b1: 0.5, b2: 0, b3: 0.7, b4: 0.25,
//                         b5: 0, b6: 0.5, b7: 0, b8: 1, tempoSync: false, bpm: 120 });
//  Post any subset of the fields; anything omitted keeps its current value. The processor
//  posts {lfo, lvl, notch[], depthNow} back at ~60 Hz for the card's Voice Orbits.
//
//  ⚠️ THE ONE THING TO KNOW IF YOU READ THE BIBLE FIRST: its §3.5 micro-shift recipe (a
//  fixed 40 ms crossfade period with a tiny r*T excursion) SHIFTS NOTHING — a sawtooth-reset
//  delay can only put energy on the lines f +- k/T, so a 2.5 Hz wanted shift against 25 Hz
//  line spacing rounds to k = 0. The free parameter is the ramp SPAN; the period follows as
//  span/r. Measured after the fix: 6 c -> +5.97, 50 c -> +50.03 cents.
// ═══════════════════════════════════════════════════════════════════════════════

const TYPE_NAMES = ['Vintage', 'June', 'Pedal', 'Trio', 'Ensemble', 'Micro', 'Wow', 'Dark'];

const CHAR_NAMES = [
  ['Classic', 'Slow', 'Fast', 'Deep', 'Wide 106', 'Locked', 'Thick', 'Hiss'],
  ['I', 'II', 'I Plus II', 'Manual', 'Aged', 'Clean', 'Wide 106', 'Deep'],
  ['Chorus', 'Vibrato', 'Grit', 'Slow Amp', 'Fast Amp', 'Wet Flip', 'Warm', 'Thin'],
  ['Preset', 'Manual', 'Enhance', 'Sides', 'Centre', 'Syrup', 'Rack 86', 'Glassy'],
  ['Solina', 'RS 202', 'Choir', 'Random', 'Dark Wine', 'Brass', 'Slow Tide', 'Phase Wide'],
  ['Studio I', 'Studio II', 'Wander', 'Dual Mono', 'Layers', 'Tape Head', 'Chorale', 'Subtle'],
  ['Cassette', 'Vinyl 33', 'Vinyl 45', 'Dictaphone', 'Pro Reel', 'Dying Deck', 'Underwater', 'Breeze'],
  ['Standard', 'Double', 'Murk', 'Pumped', 'Hissy', 'Cheap', 'Slap Wide', 'Regen Box']
];

// the 20-entry sync list, identical in all three fx3 devices
const DIV_NAMES = ['Free', '4 bar', '2 bar', '1 bar', '1/2', '1/2D', '1/2T', '1/4', '1/4D', '1/4T',
                   '1/8', '1/8D', '1/8T', '1/16', '1/16D', '1/16T', '1/32', '1/64', '1/128', '1/256'];
const DIV_BEATS = [0, 16, 8, 4, 2, 3, 1.3333, 1, 1.5, 0.6667, 0.5, 0.75, 0.3333, 0.25, 0.375,
                   0.1667, 0.125, 0.0625, 0.03125, 0.015625];

// phaseMode: 0 Vintage · 1 June · 2 Pedal · 3 Trio · 4 Micro · 5 Ensemble · 6 Wow · 7 Dark
const SPEC = [
  //         taps mono baseLo baseHi wave depthMs stag fastHz fInt fKnob trk pol detC drift fbMax trim pm pre grit comp
  { taps: 1, mono: 0, lo: 3.0,  hi: 24.0, wave: 1, depth: 5.10, stag: 0.05, fastHz: 5.50, fInt: 0.00, fKnob: 0.50, trk: 0.0, pol: 1, detC: 0,  drift: 0.00, fbMax: 0.992, trim: 0.90, pm: 0, pre: 0, grit: 1.0, comp: 1.0 },
  { taps: 1, mono: 0, lo: 1.0,  hi: 12.0, wave: 0, depth: 2.95, stag: 0.06, fastHz: 6.00, fInt: 0.00, fKnob: 0.50, trk: 0.0, pol: 1, detC: 0,  drift: 0.00, fbMax: 0.992, trim: 0.91, pm: 1, pre: 0, grit: 1.0, comp: 1.0 },
  { taps: 1, mono: 1, lo: 1.7,  hi: 14.7, wave: 2, depth: 3.50, stag: 0.00, fastHz: 6.50, fInt: 0.00, fKnob: 0.40, trk: 0.15, pol: 1, detC: 0, drift: 0.00, fbMax: 0.992, trim: 0.97, pm: 2, pre: 1, grit: 1.0, comp: 1.0 },
  { taps: 3, mono: 1, lo: 3.0,  hi: 21.3, wave: 1, depth: 4.00, stag: 0.08, fastHz: 4.50, fInt: 0.10, fKnob: 0.60, trk: 0.0, pol: 1, detC: 0,  drift: 0.00, fbMax: 0.992, trim: 1.45, pm: 3, pre: 0, grit: 1.0, comp: 1.0 },
  { taps: 3, mono: 1, lo: 2.5,  hi: 14.4, wave: 0, depth: 1.80, stag: 0.07, fastHz: 6.25, fInt: 0.25, fKnob: 0.60, trk: 0.0, pol: 1, detC: 0,  drift: 0.00, fbMax: 0.992, trim: 1.46, pm: 5, pre: 0, grit: 1.0, comp: 1.0 },
  { taps: 1, mono: 0, lo: 4.0,  hi: 20.0, wave: 1, depth: 1.50, stag: 0.00, fastHz: 6.00, fInt: 0.00, fKnob: 0.30, trk: 0.0, pol: 1, detC: 6,  drift: 0.00, fbMax: 0.96, trim: 1.09, pm: 4, pre: 0, grit: 1.0, comp: 1.0 },
  { taps: 1, mono: 0, lo: 2.5,  hi: 19.6, wave: 0, depth: 3.20, stag: 0.06, fastHz: 7.00, fInt: 0.00, fKnob: 0.50, trk: 0.2, pol: 1, detC: 0,  drift: 0.35, fbMax: 0.992, trim: 0.91, pm: 6, pre: 0, grit: 1.0, comp: 1.0 },
  { taps: 1, mono: 0, lo: 6.0,  hi: 40.0, wave: 0, depth: 6.00, stag: 0.31, fastHz: 5.00, fInt: 0.00, fKnob: 0.50, trk: 2.2, pol: 3, detC: 0,  drift: 0.00, fbMax: 0.992, trim: 0.84, pm: 7, pre: 0, grit: 1.0, comp: 1.5 }
];

const WET_ONLY = 1, WET_FLIP_R = 2, EXTRA_TAP = 4, STACK_OFF = 8,
      FORCE_LPTRI = 16, RANDOM_PHASE = 32, DUAL_MONO = 64, EXTRA_LAYER = 128;

// [rateHz, depthMul, baseMul, hf, comp, noiseDb, gritMul, fastMul, driftMul, x1, x2, x3, lvl, flags]
// lvl is MEASURED level makeup (10^(-dB/20) on a bus-level chord): a Character changes the
// TONE, never the LEVEL. Without it the roster spanned 7.5 dB on Dark alone.
const CHAR = [
  [ // Vintage — x1 = R clock ratio (the legacy 1.07 that defeats the pi offset)
    [1.13, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.070, 0.0, 1.00, 0.996, 0],
    [0.40, 1.15, 1.00, 0.42, 1.20, -58, 1.20, 0.80, 1.00, 1.070, 0.0, 1.00, 0.998, 0],
    [1.50, 0.85, 1.00, 1.90, 0.80, -62, 0.85, 1.20, 1.00, 1.070, 0.0, 1.00, 1.012, 0],
    [0.75, 1.80, 1.25, 0.90, 1.00, -60, 1.00, 1.00, 1.00, 1.070, 0.0, 1.00, 1.003, 0],
    [1.13, 1.00, 1.00, 1.10, 1.00, -60, 1.00, 1.00, 1.00, 1.140, 0.0, 1.00, 1.006, 0],
    [1.13, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.000, 0.0, 1.00, 0.999, 0],
    [0.90, 1.30, 1.15, 0.75, 1.30, -58, 1.15, 1.00, 1.00, 1.020, 0.0, 1.00, 0.965, 0],
    [1.13, 1.00, 1.00, 0.80, 1.60, -44, 1.10, 1.00, 1.00, 1.070, 0.0, 1.00, 0.912, 0]
  ],
  [ // June — x1 = R clock ratio (1.000 = the true one-clock antiphase)
    [0.513, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.000, 0.0, 1.00, 0.997, 0],
    [0.863, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.000, 0.0, 1.00, 0.996, 0],
    [9.750, 0.11, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.000, 0.0, 1.00, 1.039, FORCE_LPTRI],
    [0.000, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.000, 0.0, 1.00, 1.002, 0],
    [0.600, 1.00, 1.00, 0.45, 2.00, -54, 1.30, 1.00, 1.00, 1.000, 0.0, 1.00, 0.854, 0],
    [0.600, 1.00, 1.00, 2.60, 0.00, -200, 0.00, 1.00, 1.00, 1.000, 0.0, 1.00, 1.045, 0],
    [0.600, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.020, 0.0, 1.00, 0.998, 0],
    [0.450, 1.80, 2.00, 0.90, 1.00, -60, 1.00, 1.00, 1.00, 1.000, 0.0, 1.00, 0.991, 0]
  ],
  [ // Pedal — x1 = wet HP floor Hz (Thin)
    [0.40, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 0.0, 0.0, 1.00, 0.947, 0],
    [0.90, 1.30, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 0.0, 0.0, 1.00, 0.947, WET_ONLY],
    [0.40, 1.00, 1.00, 0.90, 1.20, -58, 4.00, 1.00, 1.00, 0.0, 0.0, 1.00, 1.064, 0],
    [0.80, 1.10, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 0.0, 0.0, 1.00, 0.947, 0],
    [6.50, 0.22, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 0.0, 0.0, 1.00, 0.947, 0],
    [0.40, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 0.0, 0.0, 1.00, 0.942, WET_FLIP_R],
    [0.40, 1.00, 1.00, 0.35, 1.30, -58, 1.10, 1.00, 1.00, 0.0, 0.0, 1.00, 0.945, 0],
    [0.40, 1.00, 1.00, 1.60, 0.80, -62, 0.90, 1.00, 1.00, 300.0, 0.0, 1.00, 1.014, 0]
  ],
  [ // Trio — x1 = centre-tap gain, x2 = side-tap gain
    [0.35, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 0.707, 1.00, 1.0, 0.998, 0],
    [0.00, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 0.50, 1.00, 0.707, 1.00, 1.0, 0.980, 0],
    [0.35, 1.10, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 0.707, 1.00, 1.0, 1.316, EXTRA_TAP],
    [0.35, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 0.250, 1.00, 1.0, 0.787, 0],
    [0.35, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.000, 0.50, 1.0, 1.088, 0],
    [0.28, 1.50, 1.00, 0.42, 1.40, -58, 1.20, 1.00, 1.00, 0.707, 1.00, 1.0, 0.996, 0],
    [0.35, 1.00, 1.00, 0.85, 1.50, -48, 1.10, 1.00, 1.00, 0.707, 1.00, 1.0, 0.990, 0],
    [0.50, 0.70, 1.00, 2.60, 0.00, -200, 0.00, 1.00, 1.00, 0.707, 1.00, 1.0, 0.875, 0]
  ],
  [ // Ensemble — x1 = fast-bank rate multiplier, x2 = tap spread (1 = 120 deg)
    [0.66, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.00, 1.00, 1.0, 0.983, 0],
    [0.66, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.60, 1.00, 1.00, 1.00, 1.0, 1.044, 0],
    [0.60, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.00, 1.00, 1.0, 1.066, EXTRA_TAP],
    [0.66, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.00, 1.00, 1.0, 0.925, RANDOM_PHASE],
    [0.66, 1.00, 1.00, 0.30, 1.30, -54, 1.20, 1.00, 1.00, 1.00, 1.00, 1.0, 1.005, 0],
    [0.66, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 2.00, 1.00, 1.0, 0.984, 0],
    [0.33, 1.30, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.00, 1.00, 1.0, 1.069, 0],
    [0.66, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.00, 0.75, 1.0, 0.942, 0]
  ],
  [ // Micro — x1 = forced head count (>=3.5 -> 4), x2 = crossfade hardness, x3 = detune mul
    [0.0, 1.00, 1.00, 1.60, 0.60, -60, 0.30, 1.00, 1.00, 0.0, 0.00, 1.00, 0.999, 0],
    [0.0, 1.40, 1.35, 0.70, 0.80, -58, 0.60, 1.00, 1.00, 0.0, 0.00, 1.00, 1.001, 0],
    [0.0, 3.00, 1.00, 1.00, 0.60, -58, 0.40, 1.00, 1.50, 0.0, 1.00, 1.00, 0.903, 0],
    [0.0, 1.00, 1.00, 1.60, 0.60, -60, 0.30, 1.00, 1.00, 0.0, 0.00, 1.00, 1.009, DUAL_MONO],
    [0.0, 1.00, 1.00, 1.60, 0.60, -60, 0.30, 1.00, 1.00, 4.0, 0.00, 1.30, 1.504, EXTRA_LAYER],
    [0.0, 1.20, 1.00, 0.45, 1.40, -56, 1.20, 1.00, 1.50, 0.0, 0.30, 1.00, 0.959, 0],
    [0.0, 1.60, 1.20, 1.20, 0.60, -60, 0.30, 1.00, 1.00, 4.0, 0.00, 1.00, 1.645, 0],
    [0.0, 0.60, 0.60, 1.60, 0.40, -62, 0.20, 1.00, 1.00, 0.0, 0.00, 0.40, 1.043, 0]
  ],
  [ // Wow — x1 = locked revolution warp Hz (0 = none), x2 = its depth (ms)
    [0.0, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 0.00, 0.0, 1.0, 1.004, 0],
    [0.0, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 0.55, 1.4, 1.0, 1.002, 0],
    [0.0, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 0.75, 1.4, 1.0, 0.997, 0],
    [0.0, 1.00, 1.00, 0.30, 1.20, -50, 1.30, 1.00, 3.00, 0.00, 0.0, 1.0, 1.018, 0],
    [0.0, 0.70, 1.00, 1.30, 0.90, -62, 0.90, 2.00, 0.30, 0.00, 0.0, 1.0, 1.016, 0],
    [0.0, 1.00, 1.00, 0.80, 2.00, -52, 1.20, 1.00, 4.00, 0.00, 0.0, 1.0, 0.842, 0],
    [0.0, 1.50, 1.00, 0.18, 1.20, -58, 1.20, 1.00, 1.00, 0.00, 0.0, 1.0, 1.067, 0],
    [0.0, 1.00, 1.00, 1.10, 1.00, -60, 1.00, 1.00, 1.20, 0.00, 0.0, 1.0, 1.022, STACK_OFF]
  ],
  [ // Dark — x1 = R stagger, x2 = clock jitter (ms), x3 = feedback floor
    [0.0, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.31, 0.0, 0.00, 0.999, 0],
    [0.0, 0.40, 1.80, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.31, 0.0, 0.00, 1.004, 0],
    [0.0, 1.00, 1.00, 0.35, 1.00, -60, 1.20, 1.00, 1.00, 1.31, 0.0, 0.00, 1.037, 0],
    [0.0, 1.00, 1.00, 1.00, 2.50, -58, 1.00, 1.00, 1.00, 1.31, 0.0, 0.00, 0.443, 0],
    [0.0, 1.00, 1.00, 1.00, 1.00, -44, 1.00, 1.00, 1.00, 1.31, 0.0, 0.00, 0.999, 0],
    [0.0, 1.00, 1.00, 0.90, 1.00, -58, 1.10, 1.00, 1.00, 1.31, 0.4, 0.00, 1.053, 0],
    [0.0, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.60, 0.0, 0.00, 0.999, 0],
    [0.0, 1.00, 1.00, 1.00, 1.00, -60, 1.00, 1.00, 1.00, 1.31, 0.0, 0.45, 0.819, 0]
  ]
];

const clamp = (v, lo, hi) => (v < lo ? lo : (v > hi ? hi : v));
const clamp01 = v => clamp(v, 0, 1);

class TerrainChorus extends AudioWorkletProcessor {
  constructor() {
    super();
    this.fs = sampleRate;

    // one stereo buffer, sized from the WORST READ (base 64 ms on Dark + span + excursion)
    let need = Math.ceil(0.140 * this.fs) + 8, sz = 1024;
    while (sz < need) sz <<= 1;
    this.N = sz; this.mask = sz - 1;
    this.bufL = new Float64Array(sz); this.bufR = new Float64Array(sz); this.wr = 0;

    this.smK = 1 - Math.exp(-1 / (0.015 * this.fs));
    this.envK = 1 - Math.exp(-1 / (0.020 * this.fs));
    this.cmpK = 1 - Math.exp(-1 / (0.005 * this.fs));
    this.lvlK = 1 - Math.exp(-1 / (0.060 * this.fs));
    this.dipDn = 1 - Math.exp(-1 / (0.008 * this.fs));
    this.dipUp = 1 - Math.exp(-1 / (0.045 * this.fs));
    this.srUp = 1 - Math.exp(-1 / (0.040 * this.fs));
    this.srDn = 1 - Math.exp(-1 / (0.400 * this.fs));
    this.cmpGlide = 1 - Math.exp(-1 / (0.040 * this.fs));
    this.dcR = 1 - (2 * Math.PI * 12 / this.fs);
    this.peA = this.onePole(3000);

    this.p = { type: 1, character: 0, rate: 0.35, depth: 0.6, feedback: 0, mix: 0.5,
               b1: 0.5, b2: 0, b3: 0.7, b4: 0.25, b5: 0, b6: 0.5, b7: 0, b8: 1,
               tempoSync: false, bpm: 120 };
    this.type = 1; this.char = 0; this.pendT = -1; this.pendC = -1; this.seeded = false;

    this.mph = 0; this.sph = 0; this.fph = 0; this.vph = 0; this.wph = [0, 0.25, 0.5];
    this.triZ = new Float64Array(8);
    this.sr = []; for (let i = 0; i < 8; ++i) this.sr.push({ st: 0, tg: 0, ph: i * 0.137 });
    this.rng = 0x2545F491;

    this.reconZ = [0, 0]; this.recon2 = [0, 0]; this.recon3 = [0, 0];
    this.nzZ = [0, 0]; this.lkZ = [0, 0]; this.lkW = [0, 0];
    this.deZ = [0, 0]; this.peZ = [0, 0];
    this.cmpEnv = [0, 0]; this.expEnv = [0, 0]; this.fbTap = [0, 0];
    this.dcX = [0, 0]; this.dcY = [0, 0]; this.dcXo = [0, 0]; this.dcYo = [0, 0];
    this.q = [0, 0];
    this.envIn = 0; this.lvlSm = 0; this.dip = 1;

    this.tap = [[], []];
    for (let c = 0; c < 2; ++c)
      for (let t = 0; t < 4; ++t)
        this.tap[c].push({ phOff: 0, phCoef: 0, gain: 0, baseMul: 1, baseCoef: 0, addCoefMs: 0, skew: 0 });
    this.nTap = 1; this.sameGeom = false;

    this.viz = { lfo: 0, lvl: 0, notch: [0, 0, 0, 0, 0, 0, 0, 0], depthNow: 0 };
    this.vizCount = 0;

    this.port.onmessage = e => {
      const d = e.data || {};
      if (d.query === 'roster') { this.port.postMessage({ roster: { TYPE_NAMES, CHAR_NAMES, DIV_NAMES } }); return; }
      for (const k in d) if (k in this.p) this.p[k] = d[k];
      const t = clamp(this.p.type | 0, 0, 7), c = clamp(this.p.character | 0, 0, 7);
      if (t !== this.type || c !== this.char) {
        if (!this.seeded) { this.type = t; this.char = c; } else { this.pendT = t; this.pendC = c; }
      }
      this.recalc();
    };
    this.recalc();
  }

  onePole(hz) {
    if (hz <= 0) return 0;
    if (hz >= this.fs * 0.49) return 1;
    return 1 - Math.exp(-2 * Math.PI * hz / this.fs);
  }
  rand11() { this.rng = (Math.imul(this.rng, 1664525) + 1013904223) | 0; return this.rng / 2147483648; }

  // TerrainChorus.h:129-143 — the house 4-point Hermite kernel
  readH(b, d) {
    let rp = this.wr - d; if (rp < 0) rp += this.N;
    const i0 = rp | 0, f = rp - i0, m = this.mask;
    const ym1 = b[(i0 - 1 + this.N) & m], y0 = b[i0 & m], y1 = b[(i0 + 1) & m], y2 = b[(i0 + 2) & m];
    const c1 = 0.5 * (y1 - ym1);
    const c2 = ym1 - 2.5 * y0 + 2 * y1 - 0.5 * y2;
    const c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
  }

  recalc() {
    const T = SPEC[this.type], C = CHAR[this.type][this.char], p = this.p;

    const r01 = clamp01(p.rate);
    this.rkTg = r01;
    let hz;
    if (p.tempoSync) {
      const idx = clamp(Math.round(r01 * 19), 0, 19), beats = DIV_BEATS[idx];
      const bpm = p.bpm > 1 ? p.bpm : 120;
      hz = beats > 0 ? bpm / (60 * beats) : 0.02 * Math.pow(2000, r01);
    } else hz = 0.02 * Math.pow(2000, r01);
    this.rateTg = clamp(C[0] > 0 ? C[0] * Math.pow(2, (r01 - 0.5) * 2) : hz, 0.01, 140);

    // the Type reshapes the knob's MAPPING; it never writes a parameter
    this.baseTg = T.lo * Math.pow(T.hi / T.lo, clamp01(p.b1)) * C[2];
    { const x = clamp01(p.depth);   // fb397 — classic below 0.6, monstrous above (see the .h)
      this.depthTg = (x <= 0.60) ? x : (0.60 + (x - 0.60) / 0.40 * (5.0 - 0.60)); }
    this.widthTg = clamp01(p.b3);
    this.flutTg = clamp01(p.b4);
    this.colorTg = clamp01(p.b6);
    this.phaseTg = clamp01(p.b8);
    this.mixTg = clamp01(p.mix);
    this.excMs = T.depth * C[1];

    // FLOOR reshaping: a Type whose engine IS one of these knobs stays alive at the default
    this.detTg = (T.detC + clamp01(p.b2) * (50 - T.detC)) * (T.pm === 4 ? C[11] : 1);
    this.driftTg = (T.drift + clamp01(p.b5) * (1 - T.drift)) * C[8];
    const fbFloor = T.pm === 7 ? C[11] : 0;
    this.fbTg = clamp(fbFloor + clamp01(p.feedback) * (T.fbMax - fbFloor), 0, T.fbMax);
    this.lkTg = Math.max(20 * Math.pow(50, clamp01(p.b7)), T.pm === 2 ? C[9] : 0);

    this.gritSpan = 7 * T.grit * C[6];
    this.colBase = 1800 * C[3];
    this.compK = T.comp * C[4];
    this.noiseAmp = C[5] > -150 ? 0.05 * Math.pow(10, C[5] / 20) : 0;
    this.fastHz = T.fastHz * (T.pm === 5 ? C[9] : 1);
    this.skew = (T.pm === 0 || T.pm === 1) ? C[9] : 1;
    this.skewOn = Math.abs(this.skew - 1) > 1e-6;

    let nT = clamp(T.taps + ((C[13] & EXTRA_TAP) ? 1 : 0), 1, 4);
    for (let c = 0; c < 2; ++c)
      for (let t = 0; t < 4; ++t)
        this.tap[c][t] = { phOff: 0, phCoef: 0, gain: 0, baseMul: 1, baseCoef: 0, addCoefMs: 0, skew: 0 };

    const set = (c, t, o) => { this.tap[c][t] = Object.assign(this.tap[c][t], o); };
    switch (T.pm) {
      case 0:  // Vintage — R rides the SKEWED clock
        nT = 1;
        set(0, 0, { phOff: 0, phCoef: 0,   gain: 1, baseMul: 1 });
        set(1, 0, { phOff: 0, phCoef: 0.5, gain: 1, baseMul: 1 + T.stag, skew: 1 });
        break;
      case 1: case 6: case 7:  // June / Wow / Dark — one clock, R offset by Phase
        nT = 1;
        set(0, 0, { gain: 1, baseMul: 1 });
        set(1, 0, { phCoef: 0.5, gain: 1, baseMul: T.pm === 7 ? C[9] : 1 + T.stag });
        break;
      case 2:  // Pedal — ONE line read TWICE, co-phase. Phase = the read offset.
        nT = 1;
        set(0, 0, { gain: 1, baseMul: 1 });
        set(1, 0, { gain: 1, baseMul: 1, addCoefMs: 1.2 });
        break;
      case 3: {  // Trio — 3 (or 4) taps PANNED. Phase = the tap spread.
        const pan = [[1, 0], [0.7071, 0.7071], [0, 1], [0.7071, 0.7071]];
        for (let t = 0; t < nT; ++t) {
          const midTap = (t === 1 || t === 3), g = (midTap ? C[9] : C[10]) / nT, pc = t / nT;
          set(0, t, { phCoef: pc, gain: g * pan[t][0], baseMul: 1 + T.stag * t });
          set(1, t, { phCoef: pc, gain: g * pan[t][1], baseMul: 1 + T.stag * t });
        }
        break;
      }
      case 5:  // Ensemble — the SAME taps SUMMED to both, R rotated by Phase
        for (let t = 0; t < nT; ++t) {
          const o = C[10] * t / nT, g = 1 / nT;
          set(0, t, { phOff: o, gain: g, baseMul: 1 + T.stag * t });
          set(1, t, { phOff: o, phCoef: 0.5, gain: g, baseMul: 1 + T.stag * t });
        }
        break;
      case 4:  // Micro — no LFO. Phase = the L/R stagger ratio.
        nT = (C[13] & EXTRA_LAYER) ? 2 : 1;
        set(0, 0, { gain: 1, baseMul: 1 });
        set(1, 0, { gain: 1, baseMul: 1, baseCoef: 0.5 });
        if (nT === 2) {
          set(0, 1, { gain: 0.25, baseMul: 1.18 });
          set(1, 1, { gain: 0.25, baseMul: 1.18, baseCoef: 0.5 });
        }
        break;
    }
    this.nTap = nT;

    // a Character re-balances the taps, it never changes the LEVEL
    for (let c = 0; c < 2; ++c) {
      let s = 0; for (let t = 0; t < nT; ++t) s += Math.abs(this.tap[c][t].gain);
      if (s > 1e-6) for (let t = 0; t < nT; ++t) this.tap[c][t].gain /= s;
    }
    // read-sharing is legal only when the two channels' tap GEOMETRY matches
    this.sameGeom = true;
    for (let t = 0; t < nT && this.sameGeom; ++t) {
      const a = this.tap[0][t], b = this.tap[1][t];
      if (a.phOff !== b.phOff || a.phCoef !== b.phCoef || a.baseMul !== b.baseMul ||
          a.baseCoef !== b.baseCoef || a.addCoefMs !== b.addCoefMs || a.skew !== b.skew)
        this.sameGeom = false;
    }
  }

  tri(p) { p -= Math.floor(p); return p < 0.5 ? (4 * p - 1) : (3 - 4 * p); }
  sin1(p) { return Math.sin(2 * Math.PI * p); }
  shape(wave, p, idx, forceLp) {
    if (wave === 1 && !forceLp) return this.sin1(p);
    const raw = wave === 1 ? this.sin1(p) : this.tri(p);
    const k = (wave === 2 || forceLp) ? 3 : 12;
    const a = this.onePole(Math.max(0.05, k * this.rateSm));
    this.triZ[idx] += a * (raw - this.triZ[idx]);
    return this.triZ[idx];
  }
  // the Raffel/Smith BBD poly with its +1/8 DC constant removed analytically
  poly(x) { x = clamp(x, -1.6, 1.6); return x - x * x * 0.125 - x * x * x / 18; }
  companderGain(env, e) { return clamp(Math.pow(Math.max(env, 1e-5) * 20, e), 0.25, 4); }
  softClip(x) { return (x > 1.4 || x < -1.4) ? Math.tanh(x) : x; }
  tickSR(s, rateHz, asym) {
    s.ph += rateHz / this.fs;
    if (s.ph >= 1) { s.ph -= 1; s.tg = this.rand11(); }
    if (asym) s.st += (s.tg > s.st ? this.srUp : this.srDn) * (s.tg - s.st);
    else s.st += this.srUp * 0.35 * (s.tg - s.st);
  }

  tapDelay(c, t, baseSamp, excSamp, fastSamp, driftSamp, T, C, limHi) {
    const tc = this.tap[c][t];
    let d = baseSamp * (tc.baseMul + tc.baseCoef * this.phaseSm) + tc.addCoefMs * this.phaseSm * 0.001 * this.fs;
    const o = tc.phOff + tc.phCoef * this.phaseSm;
    const idx = (c * 4 + t) & 7;

    if (T.pm === 4) {
      d += excSamp * this.sr[idx].st;                       // Micro: Depth = a slow wander
    } else if (T.pm === 6) {
      let stack = 0;
      if (!(C[13] & STACK_OFF))
        stack = (2 * this.tri(this.wph[0] + o) + 0.8 * this.sin1(this.wph[1] + o)
                 + 0.4 * this.sin1(this.wph[2] + o)) / 3.2;
      if (C[9] > 0.01) stack += (C[10] / 3.2) * this.sin1(this.vph + o);
      d += excSamp * stack;
      if (fastSamp > 1e-4) d += fastSamp * this.sin1(this.fph + o);
      d += driftSamp * this.sr[idx].st;
    } else {
      const w = (C[13] & RANDOM_PHASE) ? this.sr[idx].st
                                       : this.shape(T.wave, (tc.skew ? this.sph : this.mph) + o, idx,
                                                    (C[13] & FORCE_LPTRI) !== 0);
      d += excSamp * w;
      if (fastSamp > 1e-4) d += fastSamp * this.sin1(this.fph + o);
      d += driftSamp * this.sr[idx].st;
      if (T.pm === 7 && C[10] > 0.01) d += C[10] * 0.001 * this.fs * (0.5 * this.rand11());
    }
    return clamp(d, 2, limHi);
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const inp = inputs[0];
    const n = out[0].length;
    const inL = (inp && inp[0]) ? inp[0] : new Float32Array(n);
    const inR = (inp && inp[1]) ? inp[1] : inL;

    if (!this.seeded) {
      this.rateSm = this.rateTg; this.rkSm = this.rkTg; this.baseSm = this.baseTg;
      this.depthSm = this.depthTg; this.widthSm = this.widthTg; this.flutSm = this.flutTg;
      this.colorSm = this.colorTg; this.phaseSm = this.phaseTg; this.detSm = this.detTg;
      this.driftSm = this.driftTg; this.fbSm = this.fbTg; this.lkSm = this.lkTg;
      this.mixSm = this.mixTg; this.compSm = this.compK;
      this.seeded = true;
    }

    let T = SPEC[this.type], C = CHAR[this.type][this.char];
    const limHi = this.N - 6;

    for (let i = 0; i < n; ++i) {
      // fade-swap-recover
      if (this.pendT >= 0) {
        this.dip += this.dipDn * (0.02 - this.dip);
        if (this.dip < 0.05) {
          this.type = this.pendT; this.char = this.pendC; this.pendT = -1; this.pendC = -1;
          this.recalc();
          // snap at the dip floor: at -34 dB a delay jump is inaudible, and gliding while
          // the wet fades back in makes the recovery ride a moving comb
          this.rateSm = this.rateTg; this.rkSm = this.rkTg; this.baseSm = this.baseTg;
          this.depthSm = this.depthTg; this.widthSm = this.widthTg; this.flutSm = this.flutTg;
          this.colorSm = this.colorTg; this.phaseSm = this.phaseTg; this.detSm = this.detTg;
          this.driftSm = this.driftTg; this.fbSm = this.fbTg; this.lkSm = this.lkTg;
          this.compSm = this.compK;
          T = SPEC[this.type]; C = CHAR[this.type][this.char];
        }
      } else this.dip += this.dipUp * (1 - this.dip);

      const k = this.smK;
      this.rateSm += k * (this.rateTg - this.rateSm);
      this.rkSm += k * (this.rkTg - this.rkSm);
      this.baseSm += k * (this.baseTg - this.baseSm);
      this.depthSm += k * (this.depthTg - this.depthSm);
      this.widthSm += k * (this.widthTg - this.widthSm);
      this.flutSm += k * (this.flutTg - this.flutSm);
      this.colorSm += k * (this.colorTg - this.colorSm);
      this.phaseSm += k * (this.phaseTg - this.phaseSm);
      this.detSm += k * (this.detTg - this.detSm);
      this.driftSm += k * (this.driftTg - this.driftSm);
      this.fbSm += k * (this.fbTg - this.fbSm);
      this.lkSm += k * (this.lkTg - this.lkSm);
      this.mixSm += k * (this.mixTg - this.mixSm);

      const xL = inL[i], xR = inR[i];

      // nothing free-runs
      const rect = 0.5 * (Math.abs(xL) + Math.abs(xR));
      this.envIn += this.envK * (rect - this.envIn);
      const gate = this.envIn / (this.envIn + 0.003);

      const rHz = this.rateSm;
      this.mph += rHz / this.fs; if (this.mph >= 1) this.mph -= 1;
      if (this.skewOn) { this.sph += (rHz * this.skew) / this.fs; if (this.sph >= 1) this.sph -= 1; }
      else this.sph = this.mph;
      this.fph += this.fastHz / this.fs; if (this.fph >= 1) this.fph -= 1;
      if (T.pm === 6) {
        const kk = rHz / 0.5;
        this.wph[0] += 0.6 * kk / this.fs; if (this.wph[0] >= 1) this.wph[0] -= 1;
        this.wph[1] += 2.2 * kk / this.fs; if (this.wph[1] >= 1) this.wph[1] -= 1;
        this.wph[2] += 7.0 * kk / this.fs; if (this.wph[2] >= 1) this.wph[2] -= 1;
        this.vph += C[9] / this.fs; if (this.vph >= 1) this.vph -= 1;
      }
      const srRate = 1.6 + 3 * this.driftSm;
      for (let s = 0; s < 8; ++s) this.tickSR(this.sr[s], srRate, T.pm === 6);

      // the line input — compander COMPRESS
      let lineL = T.mono ? 0.5 * (xL + xR) : xL;
      let lineR = T.mono ? lineL : xR;
      if (T.pre) {
        this.peZ[0] += this.peA * (lineL - this.peZ[0]); lineL = 2 * lineL - this.peZ[0];
        if (T.mono) lineR = lineL;
        else { this.peZ[1] += this.peA * (lineR - this.peZ[1]); lineR = 2 * lineR - this.peZ[1]; }
      }
      this.compSm += this.cmpGlide * (this.compK - this.compSm);
      if (this.compSm > 0.001) {
        this.cmpEnv[0] += this.cmpK * (Math.abs(lineL) - this.cmpEnv[0]);
        lineL *= this.companderGain(this.cmpEnv[0], -0.5 * this.compSm);
        if (T.mono) lineR = lineL;
        else { this.cmpEnv[1] += this.cmpK * (Math.abs(lineR) - this.cmpEnv[1]);
               lineR *= this.companderGain(this.cmpEnv[1], -0.5 * this.compSm); }
      }

      const excSamp = this.depthSm * this.excMs * 0.001 * this.fs;
      const fastSamp = (T.fInt + this.flutSm * T.fKnob) * C[7] * 0.001 * this.fs;
      const driftSamp = this.driftSm * 2.5 * 0.001 * this.fs;
      const baseSamp = this.baseSm * 0.001 * this.fs;

      // ── the micro-shift reader. The free parameter is the ramp SPAN; the crossfade
      //    period FOLLOWS as span/r. (The bible's fixed 40 ms period shifts nothing.)
      const cents = this.detSm;
      let heads = 1, spanSamp = 0, hard = 0;
      const rc = [0, 0], dirc = [1, -1];
      if (cents > 0.02) {
        let spanMs = (T.pm === 4) ? (45 - 25 * this.rkSm) : 32;
        if (T.pm === 4) spanMs *= (1 + 0.12 * this.flutSm * this.tri(this.fph));
        spanMs *= Math.min(1, cents * 0.125);
        spanSamp = spanMs * 0.001 * this.fs;
        heads = (C[9] >= 3.5 || cents > 25) ? 4 : 2;
        hard = (T.pm === 4) ? C[10] : 0;
        const dual = (T.pm === 4 && (C[13] & DUAL_MONO)) !== 0;
        rc[0] = Math.pow(2, cents / 1200) - 1;
        rc[1] = dual ? rc[0] : (1 - Math.pow(2, -cents / 1200));
        dirc[0] = 1; dirc[1] = dual ? 1 : -1;
        for (let c = 0; c < 2; ++c) { this.q[c] += rc[c] / Math.max(1, spanSamp); if (this.q[c] >= 1) this.q[c] -= 1; }
      }

      const nT = this.nTap;
      const dT = [[0, 0, 0, 0], [0, 0, 0, 0]];
      const nGeom = this.sameGeom ? 1 : 2;
      for (let c = 0; c < nGeom; ++c)
        for (let t = 0; t < nT; ++t)
          dT[c][t] = this.tapDelay(c, t, baseSamp, excSamp, fastSamp, driftSamp, T, C, limHi);
      if (this.sameGeom) for (let t = 0; t < nT; ++t) dT[1][t] = dT[0][t];
      const dFirst = [dT[0][0], dT[1][0]];

      const g = [[1, 0, 0, 0], [1, 0, 0, 0]], ginv = [1, 1];
      if (heads > 1)
        for (let c = 0; c < 2; ++c) {
          let gs = 0;
          for (let h = 0; h < heads; ++h) {
            let qh = this.q[c] + h / heads; if (qh >= 1) qh -= 1;
            let w = 0.5 - 0.5 * Math.cos(2 * Math.PI * qh);
            if (hard > 0.001) w = w * (1 - hard) + hard * (w * w * w);
            g[c][h] = w; gs += w;
          }
          ginv[c] = 1 / Math.max(1e-6, gs);
        }

      const wet = [0, 0];
      const shareReads = this.sameGeom && T.mono && heads === 1;
      if (shareReads) {
        const v = [0, 0, 0, 0];
        for (let t = 0; t < nT; ++t) v[t] = this.readH(this.bufL, dT[0][t]);
        for (let c = 0; c < 2; ++c) { let a = 0; for (let t = 0; t < nT; ++t) a += this.tap[c][t].gain * v[t]; wet[c] = a; }
      } else {
        for (let c = 0; c < 2; ++c) {
          const buf = (T.mono || c === 0) ? this.bufL : this.bufR;
          let acc = 0;
          if (heads === 1) {
            for (let t = 0; t < nT; ++t) acc += this.tap[c][t].gain * this.readH(buf, dT[c][t]);
          } else {
            for (let h = 0; h < heads; ++h) {
              let qh = this.q[c] + h / heads; if (qh >= 1) qh -= 1;
              const off = dirc[c] > 0 ? spanSamp * (1 - qh) : spanSamp * qh;
              let s = 0;
              for (let t = 0; t < nT; ++t) s += this.tap[c][t].gain * this.readH(buf, clamp(dT[c][t] + off, 2, limHi));
              acc += g[c][h] * ginv[c] * s;
            }
          }
          wet[c] = acc;
        }
      }

      // ── the colour chain, then THE FEEDBACK TAP (before the expander and the de-emph)
      const gritK = 1 + (1 - this.colorSm) * this.gritSpan, gritI = 1 / gritK;
      const colHz = clamp(this.colBase * Math.pow(2, this.colorSm * 3.1521), 700, 18000);
      const lkA = this.lkSm > 22 ? this.onePole(this.lkSm) : 0;

      for (let c = 0; c < 2; ++c) {
        let w = wet[c];
        if (gritK > 1.02) w = this.poly(w * gritK) * gritI;

        let hz = colHz;
        if (T.trk > 0.001) hz = clamp(hz * Math.pow(baseSamp / Math.max(2, dFirst[c]), T.trk), 700, 18000);
        const a = this.onePole(T.pol === 3 ? hz * 1.9616 : hz);
        this.reconZ[c] += a * (w - this.reconZ[c]); w = this.reconZ[c];
        if (T.pol === 3) {
          this.recon2[c] += a * (w - this.recon2[c]); w = this.recon2[c];
          this.recon3[c] += a * (w - this.recon3[c]); w = this.recon3[c];
        }
        const y = w - this.dcX[c] + this.dcR * this.dcY[c];
        this.dcX[c] = w; this.dcY[c] = y; w = y;

        this.fbTap[c] = w;                                  // ← THE TAP POINT

        if (T.pre) {
          const u = (w + this.deZ[c] * (1 - this.peA)) / (2 - this.peA);
          this.deZ[c] += this.peA * (u - this.deZ[c]); w = u;
        }
        if (this.compSm > 0.001) {
          this.expEnv[c] += this.cmpK * (Math.abs(w) - this.expEnv[c]);
          w *= this.companderGain(this.expEnv[c], 0.5 * this.compSm);
        }
        if (this.noiseAmp > 0) {
          this.nzZ[c] += a * (this.rand11() - this.nzZ[c]);
          w += this.nzZ[c] * this.noiseAmp * gate * 2;
        }
        const y2 = w - this.dcXo[c] + this.dcR * this.dcYo[c];
        this.dcXo[c] = w; this.dcYo[c] = y2;
        wet[c] = y2;
      }

      // write (feedback env-gated)
      {
        const f = this.fbSm * gate;
        if (T.mono) {
          const m = 0.5 * (this.fbTap[0] + this.fbTap[1]);
          const nw = this.softClip(lineL + f * m);
          this.bufL[this.wr] = nw; this.bufR[this.wr] = nw;
        } else {
          this.bufL[this.wr] = this.softClip(lineL + f * this.fbTap[0]);
          this.bufR[this.wr] = this.softClip(lineR + f * this.fbTap[1]);
        }
        this.wr = (this.wr + 1) & this.mask;
      }

      // Low Keep — a REAL band split: the low band bypasses the effect and only its WIDTH
      // follows Mix. At Mix 100 the bass is still there, dry and centred.
      let loL = 0, loR = 0;
      if (lkA > 0) {
        this.lkZ[0] += lkA * (xL - this.lkZ[0]); loL = this.lkZ[0];
        this.lkZ[1] += lkA * (xR - this.lkZ[1]); loR = this.lkZ[1];
        this.lkW[0] += lkA * (wet[0] - this.lkW[0]); wet[0] -= this.lkW[0];
        this.lkW[1] += lkA * (wet[1] - this.lkW[1]); wet[1] -= this.lkW[1];
      }

      if (C[13] & WET_FLIP_R) wet[1] = -wet[1];              // Pedal / Wet Flip ⚠ mono-hostile
      {
        const M = 0.5 * (wet[0] + wet[1]);
        const Sd = 0.5 * (wet[0] - wet[1]) * (this.widthSm * 1.6);
        wet[0] = M + Sd; wet[1] = M - Sd;
      }

      const trim = T.trim * C[12] * this.dip;
      const wL = wet[0] * trim, wR = wet[1] * trim;

      const m = (C[13] & WET_ONLY) ? 1 : this.mixSm;
      const dg = Math.cos(m * Math.PI / 2), wg = Math.sin(m * Math.PI / 2);
      const loM = 0.5 * (loL + loR);
      out[0][i] = xL * dg + wL * wg + loL * (1 - dg) + (loM - loL) * m;
      out[1][i] = xR * dg + wR * wg + loR * (1 - dg) + (loM - loR) * m;

      this.lvlSm += this.lvlK * (0.5 * (Math.abs(wL) + Math.abs(wR)) - this.lvlSm);
      this.viz.lvl = Math.min(1, this.lvlSm * 14);
      this.viz.lfo = (T.pm === 4) ? (2 * this.q[0] - 1) : (T.wave === 1 ? this.sin1(this.mph) : this.tri(this.mph));
      this.viz.depthNow = this.depthSm * this.excMs;
      let nz = 0;
      for (let c = 0; c < 2 && nz < 8; ++c)
        for (let t = 0; t < nT && nz < 8; ++t) this.viz.notch[nz++] = 0.5 * this.fs / Math.max(2, dT[c][t]);
      for (let z = nz; z < 8; ++z) this.viz.notch[z] = 0;
    }

    // ~60 Hz telemetry for the card (Voice Orbits)
    this.vizCount += n;
    if (this.vizCount >= this.fs / 60) {
      this.vizCount = 0;
      this.port.postMessage({ viz: { lfo: this.viz.lfo, lvl: this.viz.lvl,
                                     notch: this.viz.notch.slice(), depthNow: this.viz.depthNow,
                                     type: TYPE_NAMES[this.type], character: CHAR_NAMES[this.type][this.char] } });
    }
    return true;
  }
}

registerProcessor('terrain-chorus', TerrainChorus);
