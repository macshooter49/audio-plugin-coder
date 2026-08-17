// ─────────────────────────────────────────────────────────────────────────────
// phaser-worklet.js — the SAME algorithm as TerrainPhaserFx.h, as an AudioWorkletProcessor,
// so Max can HEAR the device in Safari before a line of it is integrated (the house
// mockup-is-audible law, fb296).
//
// Same Type names, same Character names, same parameter names, same ranges. It is not
// sample-identical to the C++ (no tan LUT — JS Math.tan is cheap enough at these rates,
// and the Barber bank updates every 16 samples exactly as the C++ does) but it is the same
// machine: the cascade, the loop, the stagger laws, the motion sources and the mix trim
// are ported line for line.
//
//   USAGE
//     await ctx.audioWorklet.addModule('phaser-worklet.js');
//     const node = new AudioWorkletNode(ctx, 'terrain-phaser', {outputChannelCount:[2]});
//     node.port.postMessage({ type:3, character:1, rate:0.9, depth:0.6, feedback:0.5,
//                             mix:0.6, b1:0.5, b2:0.5, b3:0.5, b4:0.3, b5:0.5,
//                             b6:0.1, b7:0.0, b8:0.2, tempoSync:false, bpm:120 });
//     node.port.onmessage = e => drawCard(e.data);   // {lfo, lvl, notch[8], depthNow}
//
//   PARAMETER MAP (identical to the C++ Params)
//     FRONT : rate · depth · feedback        BACK : b1 Center · b2 Stages · b3 Spread ·
//     mix   : 0 = dry, 1 = fully wet                b4 Stereo · b5 Touch · b6 Lag ·
//                                                   b7 Floor  · b8 Color
// ─────────────────────────────────────────────────────────────────────────────

const TYPE_NAMES = ['Ninety', 'Stone', 'Duo', 'Twelve', 'Kraut', 'Vibe', 'Barber', 'Envy', 'Steps'];

const CHAR_NAMES = [
  ['Script 74', 'Block 78', 'Two Stage', 'Eight Stage', 'Slow Lamp', 'Sine Sweep', 'Wide Stagger', 'Negative'],
  ['Color Off', 'Color On', 'Deep Sweep', 'Two Loop Stages', 'Hot OTA', 'Cold OTA', 'Six Stage', 'Inverted'],
  ['Series 1:1.33', 'Series 3:4', 'Parallel 1:1.33', 'Parallel Golden', 'Counter', 'Wide Duo', 'Slow B', 'Cross Feed'],
  ['Full Range', 'Hi Range', 'Six Pole', 'Sixteen Pole', 'Resonant', 'Hollow', 'Aux Out', 'Fast Hollow'],
  ['Slow Bulbs', 'Fast Bulbs', 'Hard Skew', 'Reverse Skew', 'Twelve Bulb', 'Four Bulb', 'Hot Loop', 'Cold Loop'],
  ['Chorus Lamp', 'Vibrato Lamp', 'Cold Bulb', 'Hot Bulb', 'Eight Cap', 'Even Caps', 'Wide Caps', 'Vibrato Deep'],
  ['Rise 8', 'Rise 12', 'Fall 8', 'Fall 12', 'Rise Wide', 'Fall Narrow', 'Sharp Notch', 'Deep Rise'],
  ['Fast Grab', 'Slow Swell', 'Transient', 'Smooth Follow', 'Four Stage', 'Ten Stage', 'Quack', 'Sink'],
  ['Random 8', 'Random Wide', 'Ladder Up', 'Ladder Down', 'Pendulum', 'Register', 'Drunk', 'Trance Gate']
];

// loSt, hiSt, envBase(octaves)
const TYPE_SPEC = [
  [2, 10, 0], [2, 10, 0], [4, 16, 0], [6, 16, 0], [4, 16, 0],
  [4, 16, 0], [4, 12, 0], [2, 12, 2.6], [2, 12, 0]
];

// stageBias, lfo, topo, alt, fbBias, scatter, staggerMul, lagMul, ratioB, apBlend,
// depthMul, loopDrv, skew, qMul, stPhase
const C = (a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) =>
  ({ sb: a, lfo: b, topo: c, alt: d, fb: e, sc: f, stg: g, lag: h, rb: i, ap: j, dm: k, ld: l, sk: m, qm: n, sp: o });

const CHAR_SPEC = [
  [ // Ninety — 4 identical JFET stages, triangle. Spread 0 = identical breaks = the 5.83:1 law.
    C( 0,0,0,0,  0.00,0.06,1.00,1.00, 1.0,0.5,1.0,1.0,  0.0,1.0,0.0),
    C( 0,0,0,0,  0.40,0.06,1.00,1.00, 1.0,0.5,1.0,1.0,  0.0,1.0,0.0),
    C(-2,0,0,0,  0.00,0.06,1.00,1.00, 1.0,0.5,1.0,1.0,  0.0,1.0,0.0),
    C( 4,0,0,0,  0.25,0.06,1.00,1.00, 1.0,0.5,1.0,1.0,  0.0,1.0,0.0),
    C( 0,0,0,0,  0.10,0.06,1.00,8.00, 1.0,0.5,1.0,1.0,  0.0,1.0,0.0),
    C( 0,1,0,0,  0.15,0.06,1.00,1.00, 1.0,0.5,1.0,1.0,  0.0,1.0,0.0),
    C( 0,0,0,0,  0.15,0.06,2.40,1.00, 1.0,0.5,1.0,1.0,  0.0,1.0,0.0),
    C( 0,0,0,0, -0.45,0.06,1.00,1.00, 1.0,0.5,1.0,1.0,  0.0,1.0,0.0)],
  [ // Stone — OTA cascade + a dedicated EXTRA all-pass in the FEEDBACK path
    C( 0,2,1,0,  0.00,0.05,1.00,1.00, 1.0,0.5,1.00,2.0, 0.0,1.0,0.0),
    C( 0,2,1,0,  0.55,0.05,1.00,1.00, 1.0,0.5,1.40,2.5, 0.0,1.0,0.0),
    C( 0,2,1,0,  0.30,0.05,1.00,1.00, 1.0,0.5,1.90,1.0, 0.0,1.0,0.0),
    C( 0,2,2,0,  0.45,0.05,1.00,1.00, 1.0,0.5,1.00,1.0, 0.0,1.0,0.0),
    C( 0,2,1,0,  0.40,0.05,1.00,1.00, 1.0,0.5,1.00,4.0, 0.0,1.0,0.0),
    C( 0,2,1,0,  0.75,0.05,1.00,2.50, 1.0,0.5,1.00,1.0, 0.0,1.0,0.0),
    C( 2,2,1,0,  0.30,0.05,1.00,1.00, 1.0,0.5,1.00,1.0, 0.0,1.0,0.0),
    C( 0,2,1,0, -0.60,0.05,1.00,1.00, 1.0,0.5,1.00,1.5, 0.0,1.0,0.0)],
  [ // Duo — TWO cascades, TWO sweep generators, ONE accumulator
    C( 0,1,4,0,  0.00,0.05,1.00,1.00, 1.3333,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,1,4,0,  0.30,0.05,1.00,1.00, 0.7500,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,1,5,0,  0.25,0.05,1.00,1.00, 1.3333,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,1,5,0,  0.25,0.05,1.00,1.00, 1.6180,0.5,1.4,1.0, 0.0,1.0,0.0),
    C( 0,1,4,0,  0.35,0.05,1.00,1.00,-1.0000,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,1,6,0,  0.25,0.05,1.00,1.00, 1.3333,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,1,4,0,  0.30,0.05,1.00,1.00, 0.2500,0.5,1.6,1.0, 0.0,1.0,0.0),
    C( 0,1,7,0,  0.55,0.05,1.00,1.00, 1.5000,0.5,1.0,1.0, 0.0,1.0,0.0)],
  [ // Twelve — 12 poles = 6 notches; the only Type whose Rate reaches AUDIO RATE
    C( 0,1,0,0,  0.00,0.03,1.00,1.00,  1.0,0.5,1.00,1.0, 0.0,1.0,0.0),
    C( 0,1,0,0,  0.35,0.03,1.00,1.00, 12.5,0.5,0.55,1.0, 0.0,1.0,0.0),
    C(-6,1,0,0,  0.20,0.03,1.00,1.00,  1.0,0.5,1.00,1.0, 0.0,1.0,0.0),
    C( 6,1,0,0,  0.30,0.03,1.00,1.00,  1.0,0.5,1.00,1.0, 0.0,1.0,0.0),
    C( 0,1,0,0,  0.80,0.03,1.00,1.00,  1.0,0.5,1.00,1.0, 0.0,1.0,0.0),
    C( 0,1,0,0, -0.65,0.03,1.00,1.00,  1.0,0.5,1.00,1.0, 0.0,1.0,0.0),
    C( 0,1,0,0,  0.30,0.03,1.00,1.00,  1.0,0.5,1.00,1.0, 0.0,1.0,0.5),
    C( 0,1,0,0, -0.50,0.03,1.00,1.00, 12.5,0.5,0.55,1.0, 0.0,1.0,0.0)],
  [ // Kraut — LDR duty-warped sweep, lamp lag, NONLINEAR filter in the loop
    C( 0,3,3,0,  0.00,0.06,1.00,3.00, 1.0,0.5,1.0,1.5,  0.50,1.0,0.0),
    C( 0,3,3,0,  0.45,0.06,1.00,0.30, 1.0,0.5,1.0,1.5,  0.30,1.0,0.0),
    C( 0,3,3,0,  0.35,0.06,1.00,1.00, 1.0,0.5,1.0,1.5,  2.00,1.0,0.0),
    C( 0,3,3,0,  0.35,0.06,1.00,1.00, 1.0,0.5,1.0,1.5, -2.00,1.0,0.0),
    C( 4,3,3,0,  0.30,0.06,1.00,1.00, 1.0,0.5,1.0,1.5,  0.50,1.0,0.0),
    C(-4,3,3,0,  0.30,0.06,1.00,1.00, 1.0,0.5,1.0,1.5,  0.50,1.0,0.0),
    C( 0,3,3,0,  0.60,0.06,1.00,1.00, 1.0,0.5,1.0,4.5,  0.90,1.0,0.0),
    C( 0,3,0,0, -0.55,0.06,1.00,1.80, 1.0,0.5,1.0,1.0,  0.50,1.0,0.0)],
  [ // Vibe — the four measured Uni-Vibe capacitors (breaks proportional to 1/C)
    C( 0,0,0,0,  0.00,0.03,1.00, 3.00, 1.0,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,0,0,0,  0.00,0.03,1.00, 3.00, 1.0,1.0,1.0,1.0, 0.0,1.0,0.0),
    C( 0,0,0,0,  0.20,0.03,1.00,12.00, 1.0,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,0,0,0,  0.20,0.03,1.00, 0.12, 1.0,0.5,1.4,1.0, 0.0,1.0,0.0),
    C( 4,0,0,0,  0.20,0.03,1.00, 2.00, 1.0,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,0,0,0,  0.25,0.03,0.30, 2.00, 1.0,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,0,0,0,  0.25,0.03,1.75, 2.00, 1.0,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,0,0,0,  0.00,0.03,1.00, 0.50, 1.0,1.0,1.7,1.0, 0.0,1.0,0.0)],
  [ // Barber — DAFx-15 Method 1 notch bank. apBlend 1.0: the bank IS the wet.
    C( 0,4,8,0, 0,0,1.00,1.0,1.0,1.0,1.0,1.0, 0.0,1.00,0.0),
    C( 4,4,8,0, 0,0,1.00,1.0,1.0,1.0,1.0,1.0, 0.0,1.00,0.0),
    C( 0,4,8,1, 0,0,1.00,1.0,1.0,1.0,1.0,1.0, 0.0,1.00,0.0),
    C( 4,4,8,1, 0,0,1.00,1.0,1.0,1.0,1.0,1.0, 0.0,1.00,0.0),
    C( 0,4,8,0, 0,0,1.60,1.0,1.0,1.0,1.0,1.0, 0.0,1.00,0.0),
    C( 0,4,8,1, 0,0,0.55,1.0,1.0,1.0,1.0,1.0, 0.0,1.00,0.0),
    C( 0,4,8,0, 0,0,0.80,1.0,1.0,1.0,1.0,1.0, 0.0,3.00,0.0),
    C(-2,4,8,0, 0,0,1.20,1.0,1.0,1.0,1.0,1.0, 0.0,0.45,0.0)],
  [ // Envy — the motion source IS the circuit. `sk` carries env->feedback for Quack.
    C( 0,0,0,0,  0.00,0.05,1.00,0.20, 1.0,0.5,1.0,1.0, 0.00,1.0,0.0),
    C( 0,0,0,0,  0.30,0.05,1.00,3.50, 1.0,0.5,1.0,1.0, 0.00,1.0,0.0),
    C( 0,0,0,2,  0.40,0.05,1.00,0.50, 1.0,0.5,1.0,1.0, 0.00,1.0,0.0),
    C( 0,0,0,1,  0.30,0.05,2.20,1.00, 1.0,0.5,1.0,1.0, 0.00,1.0,0.0),
    C(-4,0,0,0,  0.30,0.05,1.00,1.00, 1.0,0.5,1.0,1.0, 0.00,1.0,0.0),
    C( 4,0,0,0,  0.30,0.05,1.00,1.00, 1.0,0.5,1.0,1.0, 0.00,1.0,0.0),
    C( 0,0,0,0,  0.55,0.05,1.00,0.25, 1.0,0.5,1.0,1.0, 0.35,1.0,0.0),
    C( 0,0,0,0, -0.60,0.05,1.00,1.20, 1.0,0.5,1.0,1.0, 0.00,1.0,0.0)],
  [ // Steps — sample & hold clocked by Rate; Lag is the glide between holds
    C( 0,5,0,0,  0.00,0.05,1.00,1.0,1.0,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,5,0,1,  0.30,0.05,1.00,1.0,1.0,0.5,1.5,1.0, 0.0,1.0,0.0),
    C( 0,5,0,2,  0.35,0.05,1.00,1.0,1.0,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,5,0,3,  0.35,0.05,1.00,1.0,1.0,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,5,0,4,  0.35,0.05,1.00,1.0,1.0,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,5,0,5,  0.40,0.05,1.00,1.0,1.0,0.5,1.0,1.0, 0.0,1.0,0.0),
    C( 0,5,0,6,  0.35,0.05,1.00,1.0,1.0,0.5,0.8,1.0, 0.0,1.0,0.0),
    C( 0,5,0,7,  0.50,0.05,1.00,1.0,1.0,0.5,1.4,1.0, 0.0,1.0,0.0)]
];

// the 20-entry sync list, cloned WHOLE including "Free" at index 0
const DIV_BEATS = [0, 16, 8, 4, 2, 3, 1.3333, 1, 1.5, 0.6667, 0.5, 0.75,
                   0.3333, 0.25, 0.375, 0.1667, 0.125, 0.0625, 0.03125, 0.015625];
const DIV_NAMES = ['Free', '4 bar', '2 bar', '1 bar', '1/2', '1/2D', '1/2T', '1/4', '1/4D', '1/4T',
                   '1/8', '1/8D', '1/8T', '1/16', '1/16D', '1/16T', '1/32', '1/64', '1/128', '1/256'];

const MAX_ST = 16, MAX_BANK = 12;
const clamp = (v, lo, hi) => (v < lo ? lo : (v > hi ? hi : v));
const c01 = v => clamp(v, 0, 1);
const wrap1 = v => v - Math.floor(v);
const fastTanh = x => {
  if (x > 5) return 1; if (x < -5) return -1;
  const x2 = x * x; return x * (27 + x2) / (27 + 9 * x2);
};

// one first-order all-pass:  y = g*(x - y1) + x1   (TerrainFilters.h:878, verbatim)
class Unit {
  constructor() {
    this.g = new Float64Array(MAX_ST); this.x1 = new Float64Array(MAX_ST); this.y1 = new Float64Array(MAX_ST);
    this.lg = new Float64Array(2); this.lx1 = new Float64Array(2); this.ly1 = new Float64Array(2);
    this.s1 = 0; this.s2 = 0;                       // Kraut's nonlinear loop filter
    this.fbS = 0; this.hpS = 0; this.lpS = 0;
  }
  reset() {
    this.x1.fill(0); this.y1.fill(0); this.lx1.fill(0); this.ly1.fill(0);
    this.s1 = this.s2 = this.fbS = this.hpS = this.lpS = 0;
  }
}

class TerrainPhaser extends AudioWorkletProcessor {
  static get parameterDescriptors() { return []; }

  constructor() {
    super();
    this.fs = sampleRate;
    this.p = { type: 0, character: 0, rate: 0.35, depth: 0.5, feedback: 0.0, mix: 0.5,
               b1: 0.5, b2: 0.5, b3: 0.5, b4: 0.5, b5: 0.5, b6: 0.5, b7: 0.5, b8: 0.5,
               tempoSync: false, bpm: 120 };
    this.uA = [new Unit(), new Unit()];
    this.uB = [new Unit(), new Unit()];
    this.bank = [this.newBank(), this.newBank()];
    this.stageOct = new Float64Array(MAX_ST);
    this.notchNu = new Float64Array(8);
    this.nNotch = 0; this.notchAdj = 0;
    this.stages = 4; this.topo = 0; this.lfoShape = 0; this.alt = 0;
    this.apBlend = 0.5; this.skew = 0; this.ratioB = 1;
    this.phase = 0; this.inc = 0; this.incTgt = 0;
    this.lagL = 0; this.lagR = 0; this.lagA = 0.01;
    this.shL = 0; this.shR = 0; this.shPrevL = 0; this.shPrevR = 0;
    this.stepIdx = [0, 0]; this.stepDir = [1, 1]; this.reg = [0xA3, 0x5C]; this.rngS = 0x1234567;
    this.env = 0; this.envFast = 0; this.envSlow = 0; this.envAtk = 0.01; this.envRel = 0.001;
    this.lvlSm = 0; this.lvlA = 1 - Math.exp(-1 / (this.fs * 0.030));
    this.hpA = 1 - Math.exp(-6.2831853 * 10 / this.fs);
    this.dipDn = 1 - Math.exp(-1 / (this.fs * 0.004));
    this.dipUp = 1 - Math.exp(-1 / (this.fs * 0.100));
    this.dip = 1; this.pending = true;
    this.bankCtr = 0; this.vizCtr = 0; this.vizEvery = Math.max(32, (this.fs / 60) | 0);
    this.viz = { lfo: 0, lvl: 0, notch: new Array(8).fill(0), depthNow: 0,
                 typeName: TYPE_NAMES[0], charName: CHAR_NAMES[0][0], rateHz: 0.7, stages: 4 };
    this.apply();
    this.port.onmessage = e => { Object.assign(this.p, e.data || {}); this.apply(); };
  }

  newBank() {
    return { b0: new Float64Array(MAX_BANK).fill(1), b1: new Float64Array(MAX_BANK),
             b2: new Float64Array(MAX_BANK), a1: new Float64Array(MAX_BANK), a2: new Float64Array(MAX_BANK),
             x1: new Float64Array(MAX_BANK), x2: new Float64Array(MAX_BANK),
             y1: new Float64Array(MAX_BANK), y2: new Float64Array(MAX_BANK),
             pos: new Float64Array(MAX_BANK),
             reset() { this.x1.fill(0); this.x2.fill(0); this.y1.fill(0); this.y2.fill(0); this.pos.fill(0); } };
  }

  // ── per block: everything transcendental that is not per sample
  apply() {
    const p = this.p;
    p.type = clamp(p.type | 0, 0, 8); p.character = clamp(p.character | 0, 0, 7);
    const cs = CHAR_SPEC[p.type][p.character];
    const ts = TYPE_SPEC[p.type];

    const lo = clamp(ts[0] + cs.sb, 2, MAX_ST), hi = clamp(ts[1] + cs.sb, 2, MAX_ST);
    let st = Math.round(lo + c01(p.b2) * (hi - lo));
    st = clamp(st, 2, MAX_ST);
    if (p.type === 6) st = clamp(st, 4, MAX_BANK);

    // topology class is STAGED and committed at the dip floor
    this.tStages = st; this.tTopo = cs.topo; this.tLfo = cs.lfo; this.tAlt = cs.alt;
    this.tApBlend = cs.ap; this.tSkew = cs.sk; this.tRatioB = cs.rb;
    const key = (p.type * 97 + p.character) * 32 + st;
    if (key !== this.planKey) { this.pendingKey = key; this.pending = true; }

    const sp = c01(p.b3);
    this.spreadOct = Math.log2(1 + 3 * sp * sp) * cs.stg;
    this.vibeScale = (0.35 + 1.30 * sp) * cs.stg;
    this.barberIv = (0.35 + 1.35 * sp) * cs.stg;
    this.scatter = cs.sc;
    if (!this.pending) this.buildStagger();

    this.floorHz = 20 * Math.pow(50, c01(p.b7));
    const centerHz = 40 * Math.pow(225, c01(p.b1));
    this.floorOct = Math.log2(this.floorHz);
    this.octMax = Math.log2(0.45 * this.fs);
    this.centerOct = Math.log2(centerHz);

    this.depthOct = 4.5 * Math.pow(c01(p.depth), 0.8) * cs.dm;
    if (p.type === 6) this.depthOct = 0;              // Barber: Depth is NOTCH DEPTH
    this.touchOct = ((c01(p.b5) - 0.5) * 2) * 8;
    this.envBaseOct = ts[2];

    const mag = Math.abs(cs.fb), sgn = cs.fb < 0 ? -1 : 1;
    this.fbK = clamp(sgn * (mag + c01(p.feedback) * (0.95 - mag)), -0.95, 0.95);
    this.envToFb = (p.type === 7) ? cs.sk : 0;

    const col = c01(p.b8);
    let colHz = 18000 * Math.pow(800 / 18000, col);
    if (p.type === 1) colHz *= 0.35;                  // the OTA's own bandwidth
    this.colorA = 1 - Math.exp(-6.2831853 * colHz / this.fs);
    this.loopDrv = Math.max(1, cs.ld * (1 + col * 15));
    this.invLoopDrv = 1 / this.loopDrv;
    this.loopG = this.gAt(clamp(centerHz * 0.7, 20, 0.45 * this.fs));

    const lagS = (0.004 + 0.196 * c01(p.b6)) * cs.lag;
    this.lagA = 1 - Math.exp(-1 / (this.fs * Math.max(5e-4, lagS)));
    const atkS = (0.001 + 0.059 * c01(p.b6)) * cs.lag;
    const relS = (0.030 + 0.570 * c01(p.b6)) * cs.lag;
    this.envAtk = 1 - Math.exp(-1 / (this.fs * Math.max(2e-4, atkS)));
    this.envRel = 1 - Math.exp(-1 / (this.fs * Math.max(5e-3, relS)));

    const stw = c01(p.b4);
    this.stPhase = wrap1(stw * 0.44 + cs.sp);
    this.stSplit = stw * 0.42;

    let hz;
    if (p.tempoSync) {
      const idx = clamp(Math.round(c01(p.rate) * 19), 0, 19);
      const beats = DIV_BEATS[idx];
      hz = beats > 0 ? (p.bpm / 60) / beats : 0.7;
    } else hz = 0.01 * Math.pow(2000, c01(p.rate));
    if (p.type === 3) hz *= cs.rb;                    // Hi Range -> 250 Hz
    this.rateHz = hz; this.incTgt = hz / this.fs;

    this.barberDir = (cs.alt === 1) ? -1 : 1;
    this.barberQ = (2 + 22 * c01(p.feedback)) * cs.qm;
    this.barberLmax = -(4 + 66 * c01(p.depth) * cs.dm);

    this.mixDry = Math.cos(c01(p.mix) * 1.5707963);
    this.mixWet = Math.sin(c01(p.mix) * 1.5707963);
    this.updateComp();
  }

  // correlation-aware trim on the equal-power crossfade: a phaser's wet is (1-apBlend) dry by
  // construction, so a naive equal-power blend rings +1 dB at mid-mix and -3 dB at Mix 100.
  updateComp() {
    const a = this.mixDry + this.mixWet * (1 - this.apBlend), b = this.mixWet * this.apBlend;
    this.mixComp = 1 / Math.sqrt(Math.max(1e-6, a * a + b * b));
  }

  gAt(hz) {
    const t = Math.tan(Math.PI * clamp(hz, 10, 0.4995 * this.fs) / this.fs);
    return (t - 1) / (t + 1);
  }
  gAtOct(oct) { return this.gAt(Math.pow(2, oct)); }

  commit() {
    this.planKey = this.pendingKey; this.pending = false;
    this.stages = this.tStages; this.topo = this.tTopo; this.lfoShape = this.tLfo;
    this.alt = this.tAlt; this.apBlend = this.tApBlend; this.skew = this.tSkew; this.ratioB = this.tRatioB;
    this.buildStagger();
    this.updateComp();
    for (let c = 0; c < 2; ++c) { this.uA[c].reset(); this.uB[c].reset(); this.bank[c].reset(); }
  }

  buildStagger() {
    const duo = this.topo >= 4 && this.topo <= 7;
    const n = duo ? Math.max(2, this.stages >> 1) : this.stages;
    let s = (0x9E3779B9 ^ (this.p.type * 131 + this.p.character * 17)) >>> 0;
    const nx = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return (s >>> 8) / 8388608 - 1; };
    if (this.p.type === 5) {
      // log2 of 0.616 / 0.042 / 19.66 / 1.966 — the measured Uni-Vibe cap ratios (breaks ∝ 1/C)
      const capOct = [-0.6989, -4.5735, 4.2973, 0.9752];
      for (let k = 0; k < n; ++k)
        this.stageOct[k] = capOct[k & 3] * this.vibeScale + 0.5 * (k >> 2) + this.scatter * nx();
    } else {
      for (let k = 0; k < n; ++k)
        this.stageOct[k] = (k - (n - 1) * 0.5) * this.spreadOct + this.scatter * nx();
    }
    for (let k = n; k < MAX_ST; ++k) this.stageOct[k] = 0;
    this.solveNotches(n);
    this.notchAdj = (this.topo === 8 || this.nNotch === 0) ? 0 : -Math.log2(this.notchNu[0]);
  }

  // Phi(f) = sum -2*atan(f/f_k); a notch sits where Phi = -(2m+1)*pi. Scaling every f_k by a
  // common factor scales the notches identically, so nu_m = f_notch/fc are constants of the plan.
  solveNotches(n) {
    this.nNotch = Math.min(8, n >> 1);
    for (let m = 0; m < this.nNotch; ++m) {
      const target = -(2 * m + 1) * Math.PI;
      let lo = -18, hi = 18;
      for (let it = 0; it < 44; ++it) {
        const mid = 0.5 * (lo + hi), nu = Math.pow(2, mid);
        let phi = 0;
        for (let k = 0; k < n; ++k) phi += -2 * Math.atan(nu / Math.pow(2, this.stageOct[k]));
        if (phi > target) lo = mid; else hi = mid;
      }
      this.notchNu[m] = Math.pow(2, 0.5 * (lo + hi));
    }
    for (let m = this.nNotch; m < 8; ++m) this.notchNu[m] = 0;
  }

  // ── motion ────────────────────────────────────────────────────────────────
  tri(ph) { return ph < 0.5 ? (-1 + 4 * ph) : (3 - 4 * ph); }
  // the LDR/lamp warp moves the PEAK (the duty cycle), it does not reshape each half:
  // at skew +1 the rise takes 80 % of the cycle and the fall 20 %.
  warp(u, s) { const q = Math.pow(2, 2 * s); return u / (u + q * (1 - u) + 1e-9); }
  rnd() { this.rngS = (Math.imul(this.rngS, 1664525) + 1013904223) >>> 0; return (this.rngS >>> 8) / 8388608 - 1; }
  quant(v, n) { return -1 + 2 * Math.floor(clamp(v * 0.5 + 0.5, 0, 0.9999) * n) / (n - 1); }

  sampleHold(ph, right) {
    const i = right ? 1 : 0;
    const prev = right ? this.shPrevR : this.shPrevL;
    let hold = right ? this.shR : this.shL;
    if (ph < prev) {
      switch (this.alt) {
        case 0: hold = this.quant(this.rnd(), 8); break;
        case 1: hold = this.quant(this.rnd(), 16); break;
        case 2: this.stepIdx[i] = (this.stepIdx[i] + 1) & 7; hold = -1 + 2 * this.stepIdx[i] / 7; break;
        case 3: this.stepIdx[i] = (this.stepIdx[i] + 7) & 7; hold = -1 + 2 * this.stepIdx[i] / 7; break;
        case 4: this.stepIdx[i] += this.stepDir[i];
                if (this.stepIdx[i] >= 7) { this.stepIdx[i] = 7; this.stepDir[i] = -1; }
                if (this.stepIdx[i] <= 0) { this.stepIdx[i] = 0; this.stepDir[i] = 1; }
                hold = -1 + 2 * this.stepIdx[i] / 7; break;
        case 5: { const bit = ((this.reg[i] >> 7) ^ (this.rnd() > 0.875 ? 1 : 0)) & 1;
                  this.reg[i] = ((this.reg[i] << 1) | bit) & 0xFF;
                  hold = -1 + 2 * this.reg[i] / 255; } break;
        case 6: hold = clamp(hold + (this.rnd() > 0 ? 0.2857 : -0.2857), -1, 1); break;
        default: hold = hold > 0 ? -1 : 1; break;
      }
    }
    if (right) { this.shR = hold; this.shPrevR = ph; } else { this.shL = hold; this.shPrevL = ph; }
    return hold;
  }

  lfoValue(ph, right) {
    switch (this.lfoShape) {
      case 1: return Math.sin(6.2831853 * ph);
      case 2: { const t = this.tri(ph); return (t - 0.15 * t * t * t) * 1.176; }
      case 3: return this.tri(this.warp(ph, this.skew));
      case 4: return 2 * ph - 1;
      case 5: return this.sampleHold(ph, right);
      default: return this.tri(ph);
    }
  }

  // ── one cascade + its loop. Returns the cascade output; updates the loop state.
  unit(u, x, n, oct, k, fbTap, loopExtra, nlLoop) {
    let v = x + k * fbTap;
    for (let i = 0; i < n; ++i) {
      u.g[i] = this.gAtOct(oct + this.stageOct[i]);
      const y = u.g[i] * (v - u.y1[i]) + u.x1[i];
      u.x1[i] = v; u.y1[i] = y; v = y;
    }
    let t = v;
    for (let i = 0; i < loopExtra; ++i) {
      u.lg[i] = this.loopG;
      const y = u.lg[i] * (t - u.ly1[i]) + u.lx1[i];
      u.lx1[i] = t; u.ly1[i] = y; t = y;
    }
    if (nlLoop) {                                        // Kraut's nonlinear loop filter
      const g = 0.10, R = 0.55;
      const hp = (t - (2 * R + g) * u.s1 - u.s2) / (1 + 2 * R * g + g * g);
      const bp = g * hp + u.s1; u.s1 = g * hp + bp;
      const lp = g * bp + u.s2; u.s2 = g * bp + lp;
      t = fastTanh(lp * 1.6) * 0.625;
    }
    u.lpS += this.colorA * (t - u.lpS); t = u.lpS;                          // in-loop LP
    if (this.loopDrv > 1.0001) t = fastTanh(t * this.loopDrv) * this.invLoopDrv;  // makeup INSIDE
    u.hpS += this.hpA * (t - u.hpS);
    u.fbS = t - u.hpS;                                                      // 10 Hz AC couple
    return v;
  }

  // returns the PHASER OUTPUT (the summed signal). Series and cross-feed must sum phasor B
  // against phasor A's OUTPUT, not against the device input, or their nulls are not nulls.
  runTopology(ch, x, oct, nA, k, lag) {
    const a = this.uA[ch], b = this.uB[ch];
    const extra = this.topo === 1 ? 1 : (this.topo === 2 ? 2 : 0);
    const nl = this.topo === 3;
    const ap = this.apBlend;

    if (this.topo < 4) {
      const v = this.unit(a, x, nA, oct, k, a.fbS, extra, nl);
      return x + ap * (v - x);
    }
    let lagB;
    if (this.ratioB < 0) lagB = -lag;
    else lagB = this.lfoValue(wrap1(this.phase * this.ratioB + (ch ? this.stPhase : 0)), ch !== 0);
    const octMin = Math.min(this.floorOct + this.notchAdj, this.octMax - 0.5);
    const octB = clamp(this.centerOct + this.depthOct * lagB + (ch ? -this.stSplit : this.stSplit),
                       octMin, this.octMax);

    if (this.topo === 5) {                                   // parallel — the combs ADD
      const vA = this.unit(a, x, nA, oct, k, a.fbS, 0, false);
      const vB = this.unit(b, x, nA, octB, k, b.fbS, 0, false);
      return x + ap * (0.5 * (vA + vB) - x);
    }
    if (this.topo === 6) {                                   // wide — phasor B on the RIGHT only
      const vA = this.unit(a, x, nA, oct, k, a.fbS, 0, false);
      const yA = x + ap * (vA - x);
      if (ch === 0) return yA;
      const vB = this.unit(b, yA, nA, octB, k, b.fbS, 0, false);
      return yA + ap * (vB - yA);
    }
    if (this.topo === 7) {                                   // cross-feed; clamp the PRODUCT
      const kx = clamp(k, -0.92, 0.92);
      const vA = this.unit(a, x, nA, oct, kx, b.fbS, 0, false);
      const yA = x + ap * (vA - x);
      const vB = this.unit(b, yA, nA, octB, kx, a.fbS, 0, false);
      return yA + ap * (vB - yA);
    }
    const vA = this.unit(a, x, nA, oct, k, a.fbS, 0, false);  // series — the combs MULTIPLY
    const yA = x + ap * (vA - x);
    const vB = this.unit(b, yA, nA, octB, k, b.fbS, 0, false);
    return yA + ap * (vB - yA);
  }

  // ── the Barber notch bank (DAFx-15 Method 1)
  updateBank(octL, octR) {
    const M = clamp(this.stages, 4, MAX_BANK);
    const f0 = [Math.pow(2, octL), Math.pow(2, octR)];
    const u = [(this.barberDir > 0 ? this.phase : 1 - this.phase) * M,
               (this.barberDir > 0 ? wrap1(this.phase + this.stPhase)
                                   : 1 - wrap1(this.phase + this.stPhase)) * M];
    for (let ch = 0; ch < 2; ++ch) {
      const bk = this.bank[ch];
      for (let m = 0; m < M; ++m) {
        let pos = (m + u[ch]) % M; if (pos < 0) pos += M;
        // THE WRAP: the window is exactly 0 dB at the edges, so the section is an identity there,
        // and a biquad with b == a outputs its input exactly when y1 == x1 and y2 == x2. Handing
        // the wrapping section its own input history makes the jump click-free by construction.
        // (Direction-aware: `pos < prev` is only a wrap when the ladder ascends.)
        if (Math.abs(pos - bk.pos[m]) > 0.5 * M) { bk.y1[m] = bk.x1[m]; bk.y2[m] = bk.x2[m]; }
        bk.pos[m] = pos;
        const fc = clamp(f0[ch] * Math.pow(2, pos * this.barberIv), 20, 0.45 * this.fs);
        const gDb = this.barberLmax * 0.5 * (1 - Math.cos(6.2831853 * pos / M));
        const G = Math.pow(10, gDb / 20);
        const w0 = 6.2831853 * fc / this.fs;
        const beta = Math.tan(Math.min(1.50, (w0 / Math.max(0.5, this.barberQ)) * 0.5));
        const d = 1 + beta;
        bk.b0[m] = (1 + G * beta) / d;
        bk.b1[m] = -2 * Math.cos(w0) / d;
        bk.b2[m] = (1 - G * beta) / d;
        bk.a1[m] = bk.b1[m];
        bk.a2[m] = (1 - beta) / d;
      }
      for (let m = M; m < MAX_BANK; ++m) { bk.b0[m] = 1; bk.b1[m] = bk.b2[m] = bk.a1[m] = bk.a2[m] = 0; }
    }
  }
  bankProcess(ch, x) {
    const bk = this.bank[ch];
    for (let m = 0; m < MAX_BANK; ++m) {
      const y = bk.b0[m] * x + bk.b1[m] * bk.x1[m] + bk.b2[m] * bk.x2[m]
              - bk.a1[m] * bk.y1[m] - bk.a2[m] * bk.y2[m];
      bk.x2[m] = bk.x1[m]; bk.x1[m] = x; bk.y2[m] = bk.y1[m]; bk.y1[m] = y; x = y;
    }
    return x;
  }

  process(inputs, outputs) {
    const inp = inputs[0], out = outputs[0];
    if (!out || out.length === 0) return true;
    const n = out[0].length;
    const inL = (inp && inp[0]) ? inp[0] : new Float32Array(n);
    const inR = (inp && inp[1]) ? inp[1] : inL;
    const oL = out[0], oR = out[1] || out[0];

    if (this.pending && this.planKey === undefined) this.commit();

    for (let i = 0; i < n; ++i) {
      const xL = inL[i], xR = inR[i];

      const nA = (this.topo >= 4 && this.topo <= 7) ? Math.max(2, this.stages >> 1) : this.stages;
      const octMin = Math.min(this.floorOct + this.notchAdj, this.octMax - 0.5);

      if (this.pending) { this.dip += (0.006 - this.dip) * this.dipDn; if (this.dip < 0.05) this.commit(); }
      else this.dip += (1 - this.dip) * this.dipUp;

      this.inc += (this.incTgt - this.inc) * 0.0015;
      this.phase += this.inc; if (this.phase >= 1) this.phase -= 1;

      const rect = Math.max(Math.abs(xL), Math.abs(xR));
      this.env += (rect > this.env ? this.envAtk : this.envRel) * (rect - this.env);
      let det = this.env;
      if (this.p.type === 7 && this.alt === 1) { this.envSlow += 0.0006 * (rect - this.envSlow); det = this.envSlow; }
      else if (this.p.type === 7 && this.alt === 2) {
        this.envFast += 0.02 * (rect - this.envFast);
        this.envSlow += 0.0012 * (rect - this.envSlow);
        det = Math.max(0, this.envFast - this.envSlow) * 2.5;
      }
      const gE = det * 10, env01 = gE / (1 + gE);   // soft knee: it always moves, never plateaus

      const rawL = this.lfoValue(this.phase, false);
      const rawR = this.lfoValue(wrap1(this.phase + this.stPhase), true);
      this.lagL += (rawL - this.lagL) * this.lagA;
      this.lagR += (rawR - this.lagR) * this.lagA;

      const envOct = (this.envBaseOct + this.touchOct) * env01;
      const octL = clamp(this.centerOct + this.depthOct * this.lagL + envOct + this.stSplit, octMin, this.octMax);
      const octR = clamp(this.centerOct + this.depthOct * this.lagR + envOct - this.stSplit, octMin, this.octMax);
      const kNow = clamp(this.fbK + this.envToFb * env01 * (this.fbK < 0 ? -1 : 1), -0.95, 0.95);

      let pL, pR;
      if (this.topo === 8) {
        if (--this.bankCtr <= 0) { this.bankCtr = 16; this.updateBank(octL, octR); }
        pL = xL + this.apBlend * (this.bankProcess(0, xL) - xL);
        pR = xR + this.apBlend * (this.bankProcess(1, xR) - xR);
      } else {
        pL = this.runTopology(0, xL, octL, nA, kNow, this.lagL);
        pR = this.runTopology(1, xR, octR, nA, kNow, this.lagR);
      }
      pL = 4 * fastTanh(0.25 * pL);
      pR = 4 * fastTanh(0.25 * pR);
      pL = xL + (pL - xL) * this.dip;
      pR = xR + (pR - xR) * this.dip;

      oL[i] = this.mixComp * (this.mixDry * xL + this.mixWet * pL);
      oR[i] = this.mixComp * (this.mixDry * xR + this.mixWet * pR);

      this.lvlSm += this.lvlA * (Math.max(Math.abs(pL - xL), Math.abs(pR - xR)) - this.lvlSm);

      if (++this.vizCtr >= this.vizEvery) {
        this.vizCtr = 0;
        const fc = Math.pow(2, octL);
        const v = this.viz;
        v.lfo = clamp(this.lagL, -1, 1);
        v.lvl = clamp(this.lvlSm * 8, 0, 1);
        v.depthNow = this.depthOct;
        v.rateHz = this.rateHz; v.stages = this.stages;
        v.typeName = TYPE_NAMES[this.p.type]; v.charName = CHAR_NAMES[this.p.type][this.p.character];
        if (this.topo === 8) {
          const M = clamp(this.stages, 4, MAX_BANK);
          for (let m = 0; m < 8; ++m)
            v.notch[m] = m < M ? clamp(fc * Math.pow(2, this.bank[0].pos[m] * this.barberIv), 20, 0.45 * this.fs) : 0;
        } else {
          for (let m = 0; m < 8; ++m)
            v.notch[m] = m < this.nNotch ? clamp(fc * this.notchNu[m], 20, 0.45 * this.fs) : 0;
        }
        this.port.postMessage({ lfo: v.lfo, lvl: v.lvl, notch: v.notch.slice(), depthNow: v.depthNow,
                                rateHz: v.rateHz, stages: v.stages,
                                typeName: v.typeName, charName: v.charName });
      }
    }
    return true;
  }
}

registerProcessor('terrain-phaser', TerrainPhaser);
