// ─────────────────────────────────────────────────────────────────────────────
//  flanger-worklet.js — the Terrain FLANGER as an AudioWorkletProcessor, so the
//  device can be HEARD in a Safari mockup before any C++ is integrated.
//
//  Same algorithm, same Type / Character / parameter names, same laws as
//  TerrainFlangerFx.h. It is NOT sample-identical to the C++ (the drift RNG differs
//  and the transcendentals are the JS library ones) — it is recognisably the same
//  effect, which is what the mockup is for.
//
//  USE:
//     await ctx.audioWorklet.addModule('flanger-worklet.js');
//     const flg = new AudioWorkletNode(ctx, 'terrain-flanger', {numberOfInputs:1,
//                    numberOfOutputs:1, outputChannelCount:[2]});
//     flg.port.postMessage({ type: 0, character: 0, tempoSync: false, bpm: 120 });
//     flg.parameters.get('depth').value = 0.8;      // etc.
//
//  Types      : Tape Zero · Jet · BBD · Endless · Envelope · Step
//  Front      : Rate · Depth · Feedback (BIPOLAR, 0.5 = centre) · Mix
//  Back (b1-8): Manual · Spread · Width · Damping · Shape · Bounce · Tail · Low Cut
// ─────────────────────────────────────────────────────────────────────────────

const TYPE_NAMES = ['Tape Zero', 'Jet', 'BBD', 'Endless', 'Envelope', 'Step'];

const CHAR_NAMES = [
  ['Sub', 'Add', 'Worn Deck', 'Servo', 'Wide Zero', 'Deep Zero', 'Drifting Zero', 'Counter Reel'],
  ['Silver', 'Compact', 'Deep Sweep', 'Hollow', 'Screamer', 'Drop', 'Thin Air', 'Twin Jet'],
  ['Mistress', 'Deluxe', 'Dark Bucket', 'Squash', 'Matrix', 'Short Bucket', 'Long Bucket', 'Grind'],
  ['Rise', 'Fall', 'Rise Deep', 'Fall Deep', 'Double Helix', 'Stacked Rise', 'Soft Rise', 'Tight Rise'],
  ['Up', 'Down', 'Snap', 'Slow Swell', 'Duck Zero', 'Hold', 'Wide Touch', 'Deep Touch'],
  ['Random', 'Stair Up', 'Stair Down', 'Pendulum', 'Ratchet', 'Drunk', 'Wide Steps', 'Glide']
];

// the house 20-entry sync list — identical in all three fx3 devices
const DIV_NAMES = ['Free', '4 bar', '2 bar', '1 bar', '1/2', '1/2.', '1/2T', '1/4', '1/4.', '1/4T',
                   '1/8', '1/8.', '1/8T', '1/16', '1/16.', '1/16T', '1/32', '1/64', '1/128', '1/256'];
const DIV_BEATS = [0, 16, 8, 4, 2, 3, 1.3333, 1, 1.5, 0.6667, 0.5, 0.75, 0.3333, 0.25, 0.375,
                   0.1667, 0.125, 0.0625, 0.03125, 0.015625];

// flags
const F_BBD = 1, F_COUNTER_LR = 2, F_MATRIX = 4, F_DUCK_ZERO = 8,
      F_HOLD = 16, F_SPLIT_ENV = 32, F_COUNTER = 64, F_BIASDRIFT = 128, F_TWINTAP = 256;

// The Character table. Every field is a MECHANISM constant, never an EQ curve.
// pol spanMul dampMul shapeAdd dwell driftMul bounceZ clipK reconMul pumpMul atkMul
// baseMul tapRatio fbCap dir flags pat
const S = (pol, span, damp, sh, dw, dr, bz, ck, rc, pu, ak, bs, tr, fc, di, fl, pat) =>
  ({ pol, span, damp, sh, dw, dr, bz, ck, rc, pu, ak, bs, tr, fc, di, fl, pat });

const SPEC = [
  [ // Tape Zero
    S(-1, 1.00, 1.00, 0.00, 1.35, 1.0, 0.50, 0.70, 1, 1, 1, 1.00, 0, 0.92, +1, 0, 0),
    S(+1, 1.00, 1.00, 0.00, 1.35, 1.0, 0.50, 0.70, 1, 1, 1, 1.00, 0, 0.92, +1, 0, 0),
    S(-1, 1.00, 0.72, 0.15, 1.15, 2.4, 0.45, 0.60, 1, 1, 1, 1.00, 0, 0.90, +1, 0, 0),
    S(-1, 1.00, 1.00, 0.00, 1.50, 1.2, 0.18, 0.70, 1, 1, 1, 1.00, 0, 0.92, +1, 0, 0),
    S(-1, 1.00, 1.00, 0.00, 1.35, 1.0, 0.50, 0.70, 1, 1, 1, 1.00, 0, 0.92, +1, F_COUNTER_LR, 0),
    S(-1, 1.18, 1.00, 0.00, 2.00, 1.0, 0.55, 0.70, 1, 1, 1, 1.00, 0, 0.92, +1, 0, 0),
    S(-1, 1.00, 0.90, 0.05, 1.35, 1.8, 0.50, 0.70, 1, 1, 1, 1.00, 0, 0.92, +1, F_BIASDRIFT, 0),
    S(-1, 1.00, 1.00, 0.00, 1.35, 1.0, 0.50, 0.70, 1, 1, 1, 1.00, 0, 0.92, +1, F_COUNTER, 0)],
  [ // Jet
    S(+1, 1.00, 1.00, 0.00, 1, 0.6, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.995, +1, 0, 0),
    S(+1, 0.86, 0.62, -0.50, 1, 0.6, 0.5, 0.62, 1, 1, 1, 0.85, 0, 0.995, +1, 0, 0),
    S(+1, 1.50, 0.80, 0.10, 1, 0.7, 0.5, 0.70, 1, 1, 1, 1.20, 0, 0.995, +1, 0, 0),
    S(-1, 1.00, 0.90, 0.00, 1, 0.6, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.995, +1, 0, 0),
    S(+1, 1.00, 0.75, 0.00, 1, 0.6, 0.5, 0.22, 1, 1, 1, 1.00, 0, 0.995, +1, 0, 0),
    S(+1, 1.15, 0.90, 0.50, 1, 0.8, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.995, +1, 0, 0),
    S(+1, 1.00, 1.35, 0.00, 1, 0.6, 0.5, 0.70, 1, 1, 1, 0.55, 0, 0.995, +1, 0, 0),
    S(+1, 1.00, 0.985, 0.15, 1, 0.6, 0.5, 0.70, 1, 1, 1, 1.00, 1.53, 0.985, +1, F_TWINTAP, 0)],
  [ // BBD
    S(+1, 1.00, 0.70, 0, 1, 0.9, 0.5, 0.66, 1.00, 1.00, 1.00, 1.00, 0, 0.995, +1, F_BBD, 0),
    S(+1, 1.00, 0.90, 0, 1, 0.8, 0.5, 0.70, 1.60, 0.45, 1.60, 1.00, 0, 0.995, +1, F_BBD, 0),
    S(+1, 1.00, 0.40, 0, 1, 1.0, 0.5, 0.62, 0.40, 1.10, 1.00, 1.10, 0, 0.995, +1, F_BBD, 0),
    S(+1, 1.00, 0.70, 0, 1, 1.0, 0.5, 0.60, 0.985, 2.10, 0.25, 1.00, 0, 0.995, +1, F_BBD, 0),
    S(+1, 1.00, 0.70, 0, 1, 0.6, 0.5, 0.66, 1.00, 1.00, 1.00, 1.00, 0, 0.995, +1, F_BBD | F_MATRIX, 0),
    S(+1, 0.80, 1.10, 0, 1, 0.9, 0.5, 0.66, 1.75, 0.85, 1.20, 0.45, 0, 0.995, +1, F_BBD, 0),
    S(+1, 1.10, 0.55, 0, 1, 1.1, 0.5, 0.66, 0.62, 1.20, 0.90, 2.00, 0, 0.995, +1, F_BBD, 0),
    S(-1, 1.00, 0.60, 0, 1, 1.0, 0.5, 0.40, 0.85, 1.55, 0.50, 1.00, 0, 0.995, +1, F_BBD, 0)],
  [ // Endless
    S(+1, 1.00, 1.00, 0, 1, 0.4, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.985, +1, 0, 0),
    S(+1, 1.00, 1.00, 0, 1, 0.4, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.985, -1, 0, 0),
    S(+1, 1.55, 1.00, 0, 1, 0.4, 0.5, 0.70, 1, 1, 1, 1.55, 0, 0.985, +1, 0, 0),
    S(+1, 1.55, 1.00, 0, 1, 0.4, 0.5, 0.70, 1, 1, 1, 1.55, 0, 0.985, -1, 0, 0),
    S(+1, 1.00, 1.00, 0, 1, 0.4, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.985, +1, F_COUNTER_LR, 0),
    S(+1, 1.00, 0.985, 0, 1, 0.4, 0.5, 0.70, 1, 1, 1, 1.00, 2.00, 0.90, +1, F_TWINTAP, 0),
    S(-1, 1.00, 1.00, 0, 1, 0.4, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.985, +1, 0, 0),
    S(+1, 0.75, 1.15, 0, 1, 0.4, 0.5, 0.70, 1, 1, 1, 0.60, 0, 0.985, +1, 0, 0)],
  [ // Envelope
    S(+1, 1.00, 0.985, 0, 1.00, 0.4, 0.5, 0.70, 1, 1, 1.00, 1.00, 0, 0.985, +1, 0, 0),
    S(+1, 1.00, 0.985, 0, 1.00, 0.4, 0.5, 0.70, 1, 1, 1.00, 1.00, 0, 0.985, -1, 0, 0),
    S(+1, 1.10, 0.985, 0, 2.20, 0.4, 0.5, 0.70, 1, 1, 0.12, 1.00, 0, 0.985, +1, 0, 0),
    S(+1, 1.00, 0.985, 0, 0.55, 0.4, 0.5, 0.70, 1, 1, 6.00, 1.00, 0, 0.985, +1, 0, 0),
    S(-1, 1.00, 0.985, 0, 1.00, 0.5, 0.5, 0.70, 1, 1, 1.00, 1.00, 0, 0.85, +1, F_DUCK_ZERO, 0),
    S(+1, 1.00, 0.985, 0, 1.00, 0.4, 0.5, 0.70, 1, 1, 0.50, 1.00, 0, 0.985, +1, F_HOLD, 0),
    S(+1, 1.00, 0.985, 0, 1.00, 0.4, 0.5, 0.70, 1, 1, 1.00, 1.00, 0, 0.985, +1, F_SPLIT_ENV, 0),
    S(+1, 1.70, 0.985, 0, 1.00, 0.4, 0.5, 0.70, 1, 1, 1.00, 1.20, 0, 0.985, +1, 0, 0)],
  [ // Step
    S(+1, 1.00, 0.985, 0, 1, 0.3, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.98, +1, 0, 0),
    S(+1, 1.00, 0.985, 0, 1, 0.3, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.98, +1, 0, 1),
    S(+1, 1.00, 0.985, 0, 1, 0.3, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.98, +1, 0, 2),
    S(+1, 1.00, 0.985, 0, 1, 0.3, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.98, +1, 0, 3),
    S(+1, 1.15, 0.85, 0, 1, 0.3, 0.5, 0.66, 1, 1, 1, 1.00, 0, 0.98, +1, 0, 4),
    S(+1, 1.00, 0.985, 0, 1, 0.3, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.98, +1, 0, 5),
    S(+1, 1.00, 0.985, 0, 1, 0.3, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.98, +1, F_COUNTER_LR, 6),
    S(+1, 1.00, 0.985, 0, 1, 0.3, 0.5, 0.70, 1, 1, 1, 1.00, 0, 0.98, +1, 0, 7)]
];

const clamp = (v, a, b) => (v < a ? a : (v > b ? b : v));

// ── the tape drift stack: the SmoothRandom triple at 0.6 / 2.2 / 7 Hz ────────
class Drift {
  constructor (fs, seed) {
    this.r = [0.6, 2.2, 7.0]; this.sc = [2.5, 8.0, 24.0];
    this.st = [0, 0, 0]; this.tg = [0, 0, 0]; this.ph = [0, 0, 0];
    this.inc = this.r.map(x => x / fs);
    this.sm = this.sc.map(x => Math.min(0.12, 1 - Math.exp(-2 * Math.PI * x / fs)));
    this.rng = seed >>> 0 || 1; this.held = 0; this.out = 0; this.dec = 0;
  }
  reset () { this.st = [0, 0, 0]; this.tg = [0, 0, 0]; this.ph = [0, 0, 0]; this.held = 0; this.out = 0; this.dec = 0; }
  next () {
    if (--this.dec <= 0) {
      this.dec = 8;
      const w = [0.62, 0.26, 0.12]; let o = 0;
      for (let i = 0; i < 3; ++i) {
        this.ph[i] += this.inc[i] * 8;
        if (this.ph[i] >= 1) {
          this.ph[i] -= 1;
          this.rng = (Math.imul(this.rng, 1664525) + 1013904223) >>> 0;
          this.tg[i] = ((this.rng | 0) / 2147483648);
        }
        this.st[i] += this.sm[i] * 8 * (this.tg[i] - this.st[i]);
        o += w[i] * this.st[i];
      }
      this.held = o;
    }
    this.out += 0.06 * (this.held - this.out);
    return this.out;
  }
}

class Chan {
  constructor (size) {
    this.buf = new Float32Array(size);   // recirculating
    this.dry = new Float32Array(size);   // the CLEAN reference line
    this.clear();
  }
  clear () {
    this.buf.fill(0); this.dry.fill(0);
    this.dampZ = 0; this.lowZ = 0; this.dcX = 0; this.dcY = 0; this.wLowZ = 0;
    this.rec = [0, 0, 0, 0]; this.rc2 = [0, 0];
    this.envC = 0; this.envE = 0; this.dLagZ = 0; this.dRefZ = 0;
  }
}

class TerrainFlanger extends AudioWorkletProcessor {
  static get parameterDescriptors () {
    return [
      // FRONT
      { name: 'rate',     defaultValue: 0.35,  minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'depth',    defaultValue: 0.55,  minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      // ⚠️ BIPOLAR. 0.5 is the CENTRE (no feedback). 0 = -97 % (hollow), 1 = +97 % (jet).
      { name: 'feedback', defaultValue: 0.5,   minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'mix',      defaultValue: 0.5,   minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      // BACK 8
      { name: 'manual',   defaultValue: 0.5,   minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'spread',   defaultValue: 0.35,  minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'width',    defaultValue: 0.625, minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'damping',  defaultValue: 0.35,  minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'shape',    defaultValue: 0.5,   minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'bounce',   defaultValue: 0.20,  minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'tail',     defaultValue: 0.35,  minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'lowcut',   defaultValue: 0.12,  minValue: 0, maxValue: 1, automationRate: 'k-rate' }
    ];
  }

  constructor () {
    super();
    this.fs = sampleRate;
    let need = Math.ceil(0.062 * this.fs) + 64, sz = 64;
    while (sz < need) sz <<= 1;
    this.mask = sz - 1; this.wr = 0;
    this.ch = [new Chan(sz), new Chan(sz)];
    this.driftL = new Drift(this.fs, 0x9E3779B9);
    this.driftR = new Drift(this.fs, 0x85EBCA6B);
    this.biasDrift = new Drift(this.fs, 0xC2B2AE35);

    this.kSm = 1 - Math.exp(-1 / (0.015 * this.fs));
    this.kFast = 1 - Math.exp(-1 / (0.003 * this.fs));
    this.dipDn = 1 - Math.exp(-1 / (0.003 * this.fs));
    this.dipUp = 1 - Math.exp(-1 / (0.040 * this.fs));

    this.type = 0; this.chr = 0; this.pendT = -1; this.pendC = -1;
    this.tempoSync = false; this.bpm = 120; this.dip = 1; this.primed = false;

    // smoothed / target pairs
    this.man = 1.4; this.manT = 1.4; this.bia = 0; this.biaT = 0;
    this.dep = 0.55; this.depT = 0.55; this.fb = 0; this.fbT = 0;
    this.spr = 0.17; this.sprT = 0.17; this.wid = 1; this.widT = 1;
    this.dmp = 0.5; this.dmpT = 0.5; this.dLag = 0.8; this.dLagT = 0.8;
    this.low = 0.01; this.lowT = 0.01; this.bnc = 0.2; this.bncT = 0.2;
    this.shp = 0.5; this.shpT = 0.5; this.comb = 1; this.combT = 1;
    this.dryG = 0.707; this.dryGT = 0.707; this.wetG = 0.707; this.wetGT = 0.707;
    this.nrm = 0.707; this.nrmT = 0.707;

    this.ph = 0; this.sawPh = 0; this.inc = 0; this.bs = 0; this.bv = 0;
    this.envIn = 0; this.gate = 0; this.envF = [0, 0]; this.holdPk = 0;
    this.envAtk = 0.01; this.gateRel = 0.001; this.tailSec = 0.3;
    this.stepPh = 0; this.stepCur = 0; this.stepTgt = 0; this.stepCurR = 0; this.stepTgtR = 0;
    this.stepIdx = 0; this.stepK = 0; this.stepDir = 1; this.stepN = 8;
    this.stepGl = 0.01; this.stepRateMul = 1; this.stepRng = 0x2545F491;
    this.reconC = 0.5; this.cpAtk = 0.05; this.cpRel = 0.002; this.exAtk = 0.01; this.exRel = 0.001;
    this.vizLfo = 0; this.vizLvl = 0; this.vizComb = 0;

    this.port.onmessage = e => {
      const d = e.data || {};
      if (d.type !== undefined) this.type = clamp(d.type | 0, 0, 5);
      if (d.character !== undefined) this.chr = clamp(d.character | 0, 0, 7);
      if (d.tempoSync !== undefined) this.tempoSync = !!d.tempoSync;
      if (d.bpm !== undefined) this.bpm = d.bpm > 1 ? d.bpm : 120;
      if (d.query === 'names') this.port.postMessage({ types: TYPE_NAMES, chars: CHAR_NAMES, divs: DIV_NAMES });
    };
  }

  // ── LFO shape: TRIANGLE -> SINE -> soft RAMP. That order matters: a triangle's
  //    sweep speed is CONSTANT and a sine's is not, so sine->tri->ramp folds back on
  //    itself and puts a plateau in the middle of the knob.
  shapeMorph (p, s) {
    const sn = Math.sin(2 * Math.PI * p);
    const tr = p < 0.25 ? 4 * p : (p < 0.75 ? 2 - 4 * p : 4 * p - 4);
    // the "ramp" returns over 8 % of the cycle: a hard saw reset on a delay READ
    // POSITION is a click; an 8 % return is a tape drop.
    const rp = p < 0.92 ? (2 * p / 0.92 - 1) : (1 - 2 * (p - 0.92) / 0.08);
    return s < 0.5 ? tr + (s * 2) * (sn - tr) : sn + ((s - 0.5) * 2) * (rp - sn);
  }

  // C1-continuous bounded soft clip
  softClip (x, k) {
    const C = 1.30, a = Math.abs(x);
    if (a <= k) return x;
    return Math.sign(x) * (k + (C - k) * Math.tanh((a - k) / (C - k)));
  }
  softLim (x, L) {
    const h = 0.5 * L, a = Math.abs(x);
    if (a <= h) return x;
    return Math.sign(x) * (h + h * Math.tanh((a - h) / h));
  }

  // 4-point cubic Hermite (Catmull-Rom). d >= 2 enforced.
  readAt (b, d) {
    const lim = this.mask - 3;
    if (d < 2) d = 2; else if (d > lim) d = lim;
    const di = d | 0, fr = d - di, m = this.mask, w = this.wr;
    const ym1 = b[(w - di + 1) & m], y0 = b[(w - di) & m],
          y1 = b[(w - di - 1) & m], y2 = b[(w - di - 2) & m];
    const c1 = 0.5 * (y1 - ym1);
    const c2 = ym1 - 2.5 * y0 + 2 * y1 - 0.5 * y2;
    const c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
    return ((c3 * fr + c2) * fr + c1) * fr + y0;
  }

  onePole (hz) {
    if (hz <= 0) return 0;
    if (hz >= this.fs * 0.49) return 1;
    return 1 - Math.exp(-2 * Math.PI * hz / this.fs);
  }

  cook (c, p) {
    let hz;
    if (this.tempoSync) {
      const idx = 1 + Math.round(clamp(p.rate, 0, 1) * 18);
      hz = (this.bpm / 60) / Math.max(1e-4, DIV_BEATS[idx]);
    } else hz = 0.02 * Math.pow(1000, clamp(p.rate, 0, 1));      // 0.02 -> 20 Hz, log
    if (c.fl & F_MATRIX) hz *= 0.10;                              // the Filter Matrix freeze
    this.rateHz = hz; this.inc = hz / this.fs; this.sawInc = hz / this.fs;

    this.manT = 0.1 * Math.pow(200, clamp(p.manual, 0, 1)) * c.bs;   // ms
    this.biaT = (clamp(p.manual, 0, 1) - 0.5) * 15;                  // Tape Zero: Zero Bias, ms
    this.depT = clamp(p.depth, 0, 1);
    { const t = (clamp(p.feedback, 0, 1) - 0.5) * 2;                 // bipolar, t^1.5 per side
      const a = Math.pow(Math.abs(t), 1.5) * c.fc;
      this.fbT = t < 0 ? -a : a; }
    this.sprT = clamp(p.spread, 0, 1) * 0.5;                          // 0..180 deg as turns
    this.widT = clamp(p.width, 0, 1) * 1.6;
    this.shpT = clamp(clamp(p.shape, 0, 1) + c.sh, 0, 1);
    this.bncT = clamp(p.bounce, 0, 1);
    // Damping: TWO corners off one knob (the delay path AND the recirculation)
    { const t = clamp(p.damping, 0, 1);
      this.dmpT = this.onePole(clamp(20000 * Math.pow(0.025, t) * c.damp, 200, 0.45 * this.fs));
      this.dLagT = this.onePole(clamp(20000 * Math.pow(0.060, t) * c.damp, 250, 0.45 * this.fs)); }
    this.lowT = this.onePole(clamp(20 * Math.pow(50, clamp(p.lowcut, 0, 1)), 5, 4000));
    this.tailSec = 0.060 * Math.pow(50, clamp(p.tail, 0, 1));
    this.gateRel = 1 - Math.exp(-1 / (this.tailSec * this.fs));
    { const atk = 0.060 * Math.pow(1 / 60, clamp(p.rate, 0, 1)) * c.ak;
      this.envAtk = 1 - Math.exp(-1 / (clamp(atk, 0.0002, 0.5) * this.fs)); }

    this.combT = this.type === 3 ? (0.05 + 0.985 * this.depT) : 1;     // Endless: Depth IS notch depth
    this.nrmT = 1 / Math.sqrt(1 + this.combT * this.combT);
    this.dryGT = Math.cos(clamp(p.mix, 0, 1) * Math.PI / 2);
    this.wetGT = Math.sin(clamp(p.mix, 0, 1) * Math.PI / 2);

    this.stepN = 2 + Math.round(clamp(p.shape, 0, 1) * 22);
    { const period = 1 / Math.max(0.02, hz);
      const gl = Math.max(0.005, (c.pat === 7 ? 0.60 : 0.15) * period);
      this.stepGl = 1 - Math.exp(-1 / (gl * this.fs)); }

    const tc = Math.max(0.05, c.ak);
    this.cpAtk = 1 - Math.exp(-1 / (0.004 * tc * this.fs));
    this.cpRel = 1 - Math.exp(-1 / (0.120 * tc * this.fs));
    this.exAtk = 1 - Math.exp(-1 / (0.025 * tc * this.fs));
    this.exRel = 1 - Math.exp(-1 / (0.400 * tc * this.fs));
    this.maxD = this.mask - 8;
  }

  snap () {
    this.man = this.manT; this.bia = this.biaT; this.dep = this.depT; this.fb = this.fbT;
    this.spr = this.sprT; this.wid = this.widT; this.dmp = this.dmpT; this.dLag = this.dLagT;
    this.low = this.lowT; this.bnc = this.bncT; this.shp = this.shpT; this.comb = this.combT;
    this.dryG = this.dryGT; this.wetG = this.wetGT; this.nrm = this.nrmT;
  }

  advanceStep (c) {
    const N = Math.max(2, this.stepN);
    this.stepRateMul = 1;
    const rnd = () => { this.stepRng = (Math.imul(this.stepRng, 1664525) + 1013904223) >>> 0;
                        return (this.stepRng >>> 9) % N; };
    switch (c.pat) {
      case 1: this.stepIdx = (this.stepIdx + 1) % N; break;
      case 2: this.stepIdx = (this.stepIdx + N - 1) % N; break;
      case 3: this.stepIdx += this.stepDir;
              if (this.stepIdx >= N - 1) { this.stepIdx = N - 1; this.stepDir = -1; }
              if (this.stepIdx <= 0) { this.stepIdx = 0; this.stepDir = 1; } break;
      case 4: { this.stepIdx = rnd();
                const mul = [0.5, 0.5, 1.0, 2.0];
                this.stepK = (this.stepK + 1) & 3; this.stepRateMul = 1 / mul[this.stepK]; } break;
      case 5: { this.stepRng = (Math.imul(this.stepRng, 1664525) + 1013904223) >>> 0;
                this.stepIdx += ((this.stepRng >>> 13) & 1) ? 1 : -1;
                this.stepIdx = clamp(this.stepIdx, 0, N - 1); } break;
      default: this.stepIdx = rnd(); break;
    }
    const u = N > 1 ? this.stepIdx / (N - 1) : 0.5;
    this.stepTgt = u * 2 - 1;
    const ir = (this.stepIdx + (N >> 1)) % N;
    this.stepTgtR = (N > 1 ? ir / (N - 1) : 0.5) * 2 - 1;
  }

  envMap (e, c) {
    // knee AT the measured bus: -38 dBFS floor, -14 dBFS full sweep
    const lo = 0.0126, hi = 0.20;
    const v = clamp(Math.log(Math.max(e, 1e-7) / lo) / Math.log(hi / lo), 0, 1);
    const shaped = Math.pow(v, 0.35 + 2.6 * this.shp * c.dw);
    return (c.fl & F_DUCK_ZERO) ? 1 - shaped : shaped;   // direction is applied ONCE, in geometry
  }

  geometry (c, mod, chan, g) {
    const msToS = 0.001 * this.fs;
    const dr = (chan === 0 ? this.driftL : this.driftR).next() * this.bnc * c.dr;
    g.m = this.comb;

    if (this.type === 0 || (c.fl & F_DUCK_ZERO)) {
      // the two-deck through-zero machine. Delta sweeps LINEARLY around 0 (an
      // exponential sweep can never REACH it), with a "zero dwell" exponent that
      // decelerates the crossing - what a thumb on a reel physically does.
      const span = 7.5 * this.dep * c.span;
      let bias = this.type === 0 ? this.bia : 0;
      if (c.fl & F_BIASDRIFT) bias += this.biasDrift.next() * 4.5 * this.bnc;
      const am = Math.abs(mod);
      const shaped = am > 1e-6 ? Math.sign(mod) * Math.pow(am, c.dw) : 0;
      const dMs = this.softLim(bias + span * shaped + dr * 1.20, 15.6);
      let refMs = 8;
      if (c.fl & F_COUNTER) refMs = 8 - 0.35 * span * shaped;
      g.ref = Math.max(2, refMs * msToS);
      g.d1 = Math.max(2, (refMs + dMs) * msToS);
      g.w1 = 1; g.d2 = g.d1; g.w2 = 0;
      g.comb = g.d1 - g.ref;
    } else if (this.type === 3) {
      // DAFx-15 synchronized dual comb: two sawtooth-swept combs 180 deg apart, each
      // windowed by a raised cosine that is ZERO at its own reset, so the crossfade
      // hides every wrap. Dmin = 0.55*Dmax is the paper's load-bearing rule.
      const Dmax = this.man * (1 + 0.6 * this.dep) * c.span * msToS;
      const Dmin = 0.55 * Dmax;
      // notch frequency = k/D, so the notches RISE when the delay FALLS
      let p1 = c.di > 0 ? (1 - this.sawPh) : this.sawPh;
      let p2 = p1 + 0.5; if (p2 >= 1) p2 -= 1;
      if (chan === 1) {
        if (c.fl & F_COUNTER_LR) { p1 = 1 - p1; p2 = 1 - p2; }
        p1 += this.spr; if (p1 >= 1) p1 -= 1;
        p2 += this.spr; if (p2 >= 1) p2 -= 1;
      }
      const D1 = (Dmin + (Dmax - Dmin) * p1) * (1 + 0.02 * dr);
      const D2 = (Dmin + (Dmax - Dmin) * p2) * (1 + 0.02 * dr);
      g.w1 = 0.5 - 0.5 * Math.cos(2 * Math.PI * p1);
      g.w2 = 1 - g.w1;
      g.ref = 2; g.d1 = Math.max(2, 2 + D1); g.d2 = Math.max(2, 2 + D2);
      if (c.fl & F_TWINTAP) { g.d2 = Math.max(2, 2 + D1 * c.tr); g.w1 = 0.6; g.w2 = 0.4; }
      g.comb = g.w1 * D1 + g.w2 * (g.d2 - 2);
    } else {
      // the single-deck exponential comb. Delay is swept in OCTAVES, not ms: an
      // exponential sweep reads as a linear pitch dive (the A/DA sound). 2.66 octaves
      // at Depth 100 = 40:1, against an industry norm of 20:1.
      const oct = this.softLim(2.66 * this.dep * c.span * mod * c.di + dr * 0.30, 6.4);
      const tMs = clamp(this.man * Math.pow(2, oct), 0.045, 42);
      const D = tMs * msToS;
      g.ref = 2; g.d1 = Math.max(2, 2 + D); g.w1 = 1;
      if (c.fl & F_TWINTAP) { g.d2 = Math.max(2, 2 + D * c.tr); g.w1 = 0.62; g.w2 = 0.38; }
      else { g.d2 = g.d1; g.w2 = 0; }
      g.comb = D;
    }
    // the feedback tap is the comb SPACING itself, so the resonant peaks land exactly
    // on the feedforward series (k/D) instead of drifting off it.
    g.fb = clamp(Math.abs(g.comb), 0.30 * msToS, Math.min(42 * msToS, this.maxD));
    g.ref = clamp(g.ref, 2, this.maxD);
    g.d1 = clamp(g.d1, 2, this.maxD);
    g.d2 = clamp(g.d2, 2, this.maxD);
  }

  deck (chan, g, c) {
    const s = this.ch[chan];
    // ⚠️ THE REFERENCE DECK READS THE CLEAN LINE, NOT THE LOOP. If both decks read the
    //    recirculating buffer, the zero and the pole coincide at negative feedback and
    //    cancel - the +/- Feedback flip then flattens the comb instead of MOVING it.
    let ref = this.readAt(s.dry, g.ref);
    let lag = this.readAt(s.buf, g.d1) * g.w1;
    if (g.w2 > 0) lag += this.readAt(s.buf, g.d2) * g.w2;

    if (c.fl & F_BBD) {
      // the bucket brigade's reconstruction filter, corner TRACKING the delay: the BBD
      // clock rate goes as 1/delay, so a long delay is a slow clock is a dark output.
      const ms = Math.abs(g.comb) * 1000 / this.fs;
      const t01 = clamp(Math.log2(clamp(ms, 0.5, 20) * 2) * 0.187936, 0, 1);
      const hz = 11000 * Math.pow(2, t01 * -2.874) * c.rc;
      const x = 2 * Math.PI * clamp(hz, 300, 0.45 * this.fs) / this.fs;
      this.reconC = x / (1 + x);
      for (let i = 0; i < 4; ++i) { s.rec[i] += this.reconC * (lag - s.rec[i]); lag = s.rec[i]; }
      lag = this.expandOut(s, lag, c.pu);
    }
    // DAMPING on the DELAY PATH. Tape Zero is TWO MATCHED DECKS: filtering only one of
    // them leaves a high-passed residue at Delta = 0 and destroys the null.
    const matched = this.type === 0 || (c.fl & F_DUCK_ZERO);
    s.dLagZ += this.dLag * (lag - s.dLagZ); lag = s.dLagZ;
    if (matched) { s.dRefZ += this.dLag * (ref - s.dRefZ); ref = s.dRefZ; }

    let w = (ref + c.pol * g.m * lag) * this.nrm;
    if (c.fl & F_BBD) { for (let i = 0; i < 2; ++i) { s.rc2[i] += this.reconC * 0.55 * (w - s.rc2[i]); w = s.rc2[i]; } }
    s.wLowZ += this.low * (w - s.wLowZ);
    return w - s.wLowZ;
  }

  compressIn (s, x, pump) {
    const a = Math.abs(x);
    s.envC += (a > s.envC ? this.cpAtk : this.cpRel) * (a - s.envC);
    const gc = 1 / (1 + 13 * pump * s.envC), k = 2.2;
    return Math.tanh(k * x * gc) / k;
  }
  expandOut (s, y, pump) {
    const a = Math.abs(y);
    s.envE += (a > s.envE ? this.exAtk : this.exRel) * (a - s.envE);
    const ge = Math.min(4, 1 + 13 * pump * s.envE), k = 2.2;
    return Math.sinh(clamp(k * y, -3, 3)) / k * ge;
  }

  loop (chan, g, c) {
    const s = this.ch[chan];
    let v = this.readAt(s.buf, g.fb);
    if (c.fl & F_BBD) v = this.expandOut(s, v, c.pu * 0.5);
    s.dampZ += this.dmp * (v - s.dampZ); v = s.dampZ;              // in-loop LP
    s.lowZ += this.low * (v - s.lowZ); v -= s.lowZ;                // in-loop HP
    // in-loop DC blocker: asymmetric program + regeneration integrates a DC pedestal
    // and the soft clip then rectifies it.
    const r = 1 - (2 * Math.PI * 5 / this.fs);
    const y = v - s.dcX + r * s.dcY;
    s.dcX = v; s.dcY = y;
    // a Character whose only difference is a lower clip knee is a NO-OP at a -26 dBFS
    // bus (the loop never reaches the knee). Driving INTO the knee is what makes
    // "it distorts before it runs away" audible at real program level.
    const k = 0.70 / c.ck, drv = k * k;
    return this.softClip(y * drv, c.ck) / drv;
  }

  process (inputs, outputs, params) {
    const out = outputs[0];
    const inp = inputs[0];
    const n = out[0].length;
    const inL = (inp && inp[0]) ? inp[0] : new Float32Array(n);
    const inR = (inp && inp[1]) ? inp[1] : inL;
    const outL = out[0], outR = out[1] || out[0];

    const p = {
      rate: params.rate[0], depth: params.depth[0], feedback: params.feedback[0], mix: params.mix[0],
      manual: params.manual[0], spread: params.spread[0], width: params.width[0],
      damping: params.damping[0], shape: params.shape[0], bounce: params.bounce[0],
      tail: params.tail[0], lowcut: params.lowcut[0]
    };

    // type / character swap: dip to 2 %, swap at the floor, recover. The ring is NOT
    // cleared (that would guillotine the tail); the STATE is re-seated.
    if ((this.type !== this.curT || this.chr !== this.curC) && this.curT !== undefined) {
      this.pendT = this.type; this.pendC = this.chr;
    }
    if (this.curT === undefined) { this.curT = this.type; this.curC = this.chr; }
    let c = SPEC[this.curT][this.curC];
    const savedType = this.type; this.type = this.curT;
    this.cook(c, p);
    if (!this.primed) { this.snap(); this.primed = true; }

    const gL = {}, gR = {};
    let peak = 0, lastComb = 0, lastMod = 0;

    for (let i = 0; i < n; ++i) {
      if (this.pendT >= 0) {
        this.dip += (0.02 - this.dip) * this.dipDn;
        if (this.dip < 0.05) {
          this.curT = this.pendT; this.curC = this.pendC; this.pendT = -1; this.pendC = -1;
          for (const s of this.ch) { s.dampZ = s.lowZ = s.dcX = s.dcY = s.wLowZ = 0;
                                     s.rec = [0,0,0,0]; s.rc2 = [0,0]; s.envC = s.envE = 0;
                                     s.dLagZ = s.dRefZ = 0; }
          this.bs = 0; this.bv = 0; this.envF = [0, 0]; this.holdPk = 0;
          c = SPEC[this.curT][this.curC];
          this.type = this.curT; this.cook(c, p);
          this.man = this.manT; this.bia = this.biaT; this.dep = this.depT; this.comb = this.combT;
        }
      } else this.dip += (1 - this.dip) * this.dipUp;

      // per-sample smoothers (the no-clicks law)
      this.man += this.kSm * (this.manT - this.man);
      this.bia += this.kSm * (this.biaT - this.bia);
      this.dep += this.kSm * (this.depT - this.dep);
      this.fb  += this.kSm * (this.fbT - this.fb);
      this.spr += this.kSm * (this.sprT - this.spr);
      this.wid += this.kSm * (this.widT - this.wid);
      this.dmp += this.kSm * (this.dmpT - this.dmp);
      this.dLag += this.kSm * (this.dLagT - this.dLag);
      this.low += this.kSm * (this.lowT - this.low);
      this.bnc += this.kSm * (this.bncT - this.bnc);
      this.shp += this.kSm * (this.shpT - this.shp);
      this.comb += this.kSm * (this.combT - this.comb);
      this.dryG += this.kSm * (this.dryGT - this.dryG);
      this.wetG += this.kSm * (this.wetGT - this.wetG);
      this.nrm += this.kSm * (this.nrmT - this.nrm);

      const xl = inL[i], xr = inR[i];

      // input presence: the feedback gate, applied SQUARED (a linear release latches)
      const rect = Math.max(Math.abs(xl), Math.abs(xr));
      this.envIn += (rect > this.envIn ? this.kFast : this.gateRel) * (rect - this.envIn);
      const g1 = Math.min(1, this.envIn * 150);
      this.gate = g1 * g1;

      // ── the modulator. ONE master clock; L/R offsets are DERIVED at read time.
      this.ph += this.inc; if (this.ph >= 1) this.ph -= 1;
      this.sawPh += this.sawInc; if (this.sawPh >= 1) this.sawPh -= 1;
      let mL, mR;
      if (this.curT === 4) {                       // Envelope
        const relC = 1 - Math.exp(-1 / (this.tailSec * this.fs));
        const aR = (c.fl & F_SPLIT_ENV) ? this.envAtk * 0.22 : this.envAtk;
        this.envF[0] += ((Math.abs(xl) > this.envF[0]) ? this.envAtk : relC) * (Math.abs(xl) - this.envF[0]);
        this.envF[1] += ((Math.abs(xr) > this.envF[1]) ? aR : relC) * (Math.abs(xr) - this.envF[1]);
        let e0 = this.envF[0], e1 = this.envF[1];
        if (c.fl & F_HOLD) { const pk = Math.max(e0, e1);
                             this.holdPk = Math.max(pk, this.holdPk - this.holdPk * relC * 0.25);
                             e0 = e1 = this.holdPk; }
        mL = this.envMap(e0, c);
        mR = (c.fl & F_SPLIT_ENV) ? this.envMap(e1, c) : mL;
      } else if (this.curT === 5) {                // Step
        this.stepPh += this.inc * this.stepRateMul;
        if (this.stepPh >= 1) { this.stepPh -= 1; this.advanceStep(c); }
        this.stepCur += this.stepGl * (this.stepTgt - this.stepCur);
        this.stepCurR += this.stepGl * (this.stepTgtR - this.stepCurR);
        mL = this.stepCur; mR = (c.fl & F_COUNTER_LR) ? this.stepCurR : this.stepCur;
      } else {
        let pR = this.ph + this.spr; if (pR >= 1) pR -= 1;
        let a = this.shapeMorph(this.ph, this.shp);
        const b = this.shapeMorph(pR, this.shp);
        if (this.curT === 0) {
          // servo BOUNCE: a damped spring on the sweep target. The overshoot on every
          // reversal is the Eventide FL-201 capstan model, blended in by Bounce so
          // Bounce 0 is genuinely OFF.
          const wn = 2 * Math.PI * 1.8, z = c.bz - 0.30 * this.bnc;
          const acc = wn * wn * (a - this.bs) - 2 * z * wn * this.bv;
          this.bv += acc / this.fs; this.bs += this.bv / this.fs;
          a = a + this.bnc * (this.bs - a);
        }
        mL = a; mR = (c.fl & F_COUNTER_LR) ? -b : b;
      }

      this.geometry(c, mL, 0, gL);
      this.geometry(c, mR, 1, gR);
      const wL = this.deck(0, gL, c), wR = this.deck(1, gR, c);
      const fL = this.loop(0, gL, c), fR = this.loop(1, gR, c);
      const gFb = this.fb * this.gate;                 // the COEFFICIENT is gated, not the output

      let nl = this.softClip(xl + gFb * fL, c.ck);
      let nr = this.softClip(xr + gFb * fR, c.ck);
      if (c.fl & F_BBD) { nl = this.compressIn(this.ch[0], nl, c.pu);
                          nr = this.compressIn(this.ch[1], nr, c.pu); }
      this.ch[0].buf[this.wr] = nl; this.ch[1].buf[this.wr] = nr;
      this.ch[0].dry[this.wr] = xl; this.ch[1].dry[this.wr] = xr;   // the CLEAN reference line
      this.wr = (this.wr + 1) & this.mask;

      // M/S width on the WET only, never in the loop
      const M = 0.5 * (wL + wR), Sd = 0.5 * (wL - wR) * this.wid;
      const wl = (M + Sd) * this.dip, wr2 = (M - Sd) * this.dip;
      peak = Math.max(peak, Math.abs(wl));
      lastComb = gL.comb; lastMod = mL;

      outL[i] = this.dryG * xl + this.wetG * wl;
      outR[i] = this.dryG * xr + this.wetG * wr2;
    }

    this.type = savedType;
    this.vizLfo = clamp(lastMod, -1, 1);
    this.vizLvl = Math.min(1, peak);
    this.vizComb = Math.abs(lastComb) * 1000 / this.fs;
    if ((this.frame = (this.frame | 0) + 1) % 6 === 0) {
      // 60 Hz-ish push: the needle, the level, the comb position, and the notch series
      const sub = SPEC[this.curT][this.curC].pol < 0;
      const notch = [];
      for (let k = 0; k < 8; ++k) {
        const f = this.vizComb > 1e-4
          ? (sub ? (k + 1) * 1000 / this.vizComb : (2 * k + 1) * 500 / this.vizComb) : 0;
        notch.push(f > 20 && f < 20000 ? f : 0);
      }
      this.port.postMessage({ lfo: this.vizLfo, lvl: this.vizLvl, depthNow: this.vizComb, notch });
    }
    return true;
  }
}

registerProcessor('terrain-flanger', TerrainFlanger);
