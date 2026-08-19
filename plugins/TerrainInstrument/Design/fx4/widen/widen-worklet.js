// widen-worklet.js — fb420. An AudioWorklet port of TerrainWidenFx.h so Max can HEAR the
// WIDEN device in a Safari mockup before any of it is wired into the plugin.
//
//   Not sample-identical to the C++ (no fade-swap-recover, no compander, single-precision
//   maths in a different order) — recognisably the SAME effect, with the SAME Type,
//   Character, Field and parameter names, the SAME laws, and the SAME ceilings.
//
//   Load:   await ctx.audioWorklet.addModule('widen-worklet.js');
//           const n = new AudioWorkletNode(ctx, 'terrain-widen', {outputChannelCount:[2]});
//   Drive:  n.port.postMessage({ type:0, character:0, field:0,
//                               amount:0.35, width:0.5, rate:0.35, mix:0.5,
//                               p1:0.5, p2:0.85, p3:0.5, p4:0, p5:0, p6:0.5, p7:0, p8:0.5,
//                               retrig:false });
//   Read:   n.port.onmessage = e => { /* e.data = {corr, voicePan[8], voiceCents[8],
//                                                  widthNow, lvl} — the same Viz */ };
//
// THE TWO THINGS THAT MUST SURVIVE THE PORT, because they are the device:
//   1. `Twin` uses a TRIANGLE in antiphase, cross-mixed with OPPOSITE POLARITY. A triangle
//      has constant |slope|, so the detune HOLDS at ±c and only flips sign at the apexes.
//      Swap it for a sine (Character `Wobble`) and you can hear the detune start breathing —
//      that A/B is the whole point of the Type.
//   2. The CONSTANT-CENTS LAW. The knob is cents; the depth is solved for the rate:
//         A = (2^(c/1200) − 1) / (2π f)   [sine]      / (4 f)   [triangle]
//      Sweep `Rate` with `Amount` held and the detune does not change. Serum's does.

const TYPES  = ['Stack', 'Twin', 'Shift', 'Double', 'Blur', 'Bands'];
const FIELDS = ['Direct', 'Alternate', 'Orbit', 'Swap', 'Side Only', 'Collapse'];
const CHARS  = [
  ['JP Classic','Even Fan','Analog Drift','Tight','Wide Fan','Octave Bloom','Sub Anchor','Three Phase'],
  ['Duo','Quad','Mode Two','Mode Three','No Compander','Dark BBD','Wobble','Hex'],
  ['Silk','Punch','Warble','Fifth Up','Down Double','Wide Slap','Gritty','Octave Pair'],
  ['Vocal','Wide Room','Tape ADT','Tight Inst','Loose Crowd','Static Twins','Slapback','Seasick'],
  ['Smooth Six','Deep Twelve','Velvet','Low Anchor','Air Only','Seed B','Seed C','Counter'],
  ['Coarse','Fine','Tilted','Rotor Slow','Rotor Fast','Guard','Low Split','Hard Split']
];

// ── the Type table, mirroring SPEC[] in the header ───────────────────────────
//    family 0 = voice crowd · 1 = antiphase pair · 2 = allpass · 3 = band tree
//    mod    0 = sine LFO    · 1 = triangle       · 2 = static  · 3 = random walk
const SPEC = [
  { family:0, mod:0, maxCents:130, curve:1.60, rateMul:1.00, fbMax:0.90, trim:1.00 }, // Stack
  { family:1, mod:1, maxCents: 28, curve:1.35, rateMul:1.00, fbMax:0.85, trim:1.39 }, // Twin
  { family:0, mod:2, maxCents:110, curve:1.40, rateMul:1.00, fbMax:0.85, trim:0.98 }, // Shift
  { family:0, mod:3, maxCents: 62, curve:1.30, rateMul:0.55, fbMax:0.88, trim:0.96 }, // Double
  { family:2, mod:0, maxCents:  0, curve:1.00, rateMul:0.30, fbMax:0.72, trim:1.00 }, // Blur
  { family:3, mod:0, maxCents:  0, curve:1.00, rateMul:0.60, fbMax:0.80, trim:0.72 }  // Bands
];

// CharSpec: [baseMul, centsMul, rateMul, wanderAdd, spanMul, x1, x2, x3, lvl, flags]
const F_EVENFAN=1, F_OCTTOP=2, F_SUB=4, F_3PH=8, F_SINE=16, F_COMPOFF=32,
      F_DARK=64, F_ALLNEG=128, F_STATIC=256, F_NEGB=512, F_TILT=1024, F_LIN=2048;
const CHAR = [
 [[1.00,1.00,1.00,0.00,1.0, 0.00,0.00,1.00, 1.000, 0],
  [1.00,1.00,1.00,0.00,1.0, 0.00,0.00,1.00, 1.000, F_EVENFAN],
  [1.00,1.00,1.00,0.35,1.0, 0.15,0.00,1.00, 1.000, 0],
  [0.45,1.00,1.35,0.00,1.0, 0.00,0.00,1.00, 1.000, 0],
  [2.20,1.00,0.70,0.00,1.0, 0.00,0.00,1.00, 0.955, 0],
  [1.00,1.00,1.00,0.00,1.0, 0.00,0.25,1.00, 0.955, F_OCTTOP],
  [1.00,1.00,1.00,0.00,1.0, 0.00,0.35,1.00, 0.940, F_SUB],
  [1.00,1.00,1.00,0.00,1.0, 0.00,0.00,1.00, 0.975, F_3PH]],
 [[1.00,1.00,1.00,0.00,1.0, 1.0,1.00,0.0, 1.000, 0],
  [1.00,1.00,1.00,0.00,1.0, 2.0,1.00,0.0, 0.900, 0],
  [0.50,1.00,2.20,0.00,1.0, 1.0,1.00,0.0, 1.000, 0],
  [1.00,2.00,1.00,0.00,1.0, 1.0,1.00,0.0, 1.000, 0],
  [1.00,1.00,1.00,0.00,1.0, 1.0,1.00,0.0, 0.871, F_COMPOFF],
  [1.00,1.00,1.00,0.00,1.0, 1.0,1.00,4.0, 1.040, F_DARK],
  [1.00,2.50,1.00,0.00,1.0, 1.0,1.00,0.0, 1.000, F_SINE],
  [1.00,1.00,1.00,0.00,1.0, 3.0,1.55,0.0, 1.330, 0]],
 [[1.00,1.00,1.00,0.00,1.00,    0,0.00,1.00, 1.000, 0],
  [1.00,1.00,1.00,0.00,0.40,    0,0.00,1.00, 1.000, 0],
  [1.00,1.00,1.00,0.55,1.00,    0,0.00,1.00, 1.000, 0],
  [1.00,1.00,1.00,0.00,1.00,  700,0.18,1.00, 0.975, 0],
  [1.00,1.00,1.00,0.00,1.00,    0,0.00,1.00, 1.000, F_ALLNEG],
  [1.00,1.00,1.00,0.00,1.00,    0,0.00,2.30, 0.985, 0],
  [1.00,1.00,1.00,0.00,0.27,    0,0.00,1.00, 1.000, F_LIN],
  [1.00,1.00,1.00,0.00,1.00, 1200,0.25,1.00, 0.965, 0]],
 [[1.00,1.00,1.00,0.00,1.0, 1.00, 0.0,0.0, 1.000, 0],
  [1.35,1.00,0.70,0.00,1.0, 0.50, 0.0,0.0, 0.985, 0],
  [1.00,2.00,0.60,0.00,1.0, 0.55, 0.0,0.0, 0.985, 0],
  [0.50,1.00,1.00,0.00,1.0, 1.00, 0.0,0.0, 1.000, 0],
  [1.80,1.60,1.00,0.00,1.0, 1.00, 0.0,0.0, 0.975, 0],
  [1.00,1.00,1.00,0.00,1.0, 1.00, 0.0,8.0, 1.000, F_STATIC],
  [1.00,1.00,1.00,0.00,1.0, 1.00,31.0,0.0, 0.990, 0],
  [1.00,2.50,1.30,0.00,1.0, 1.00, 0.0,0.0, 0.960, 0]],
 [[1,1,1.00,0.00,1, 3.0, 180, 5600, 1.000, 0],
  [1,1,1.00,0.00,1, 6.0, 180, 5600, 1.000, 0],
  [1,1,1.00,0.40,1, 4.0,  90, 9000, 1.000, 0],
  [1,1,1.00,0.00,1, 3.0, 500, 6500, 1.000, 0],
  [1,1,1.00,0.00,1, 3.0,2000,10000, 1.000, 0],
  [1,1,1.00,0.00,1, 3.0, 240, 7000, 1.000, 0],
  [1,1,1.00,0.00,1, 3.0, 140, 4200, 1.000, 0],
  [1,1,1.00,0.00,1, 4.0, 180, 5600, 1.000, F_NEGB]],
 [[1,1,1.00,0.00,1, 1.00,140,1.00, 1.000, 0],
  [1,1,1.00,0.00,1, 2.00,140,1.00, 1.000, 0],
  [1,1,1.00,0.00,1, 1.00,140,1.00, 1.000, F_TILT],
  [1,1,0.30,0.00,1, 1.00,140,1.00, 1.000, 0],
  [1,1,4.00,0.00,1, 1.00,140,1.00, 1.000, 0],
  [1,1,1.00,0.00,1, 1.00,140,1.00, 1.000, 0],
  [1,1,1.00,0.00,1, 1.00, 50,1.00, 1.000, 0],
  [1,1,1.00,0.00,1, 1.00,140,0.50, 0.960, 0]]
];

const JP    = [0, 0.34, -0.34, 1.04, -1.12, 1.77, -2.02, 2.40];   // measured JP-8000 fan, cents/100
const EVEN  = [0, 0.40, -0.40, 0.80, -0.80, 1.60, -1.60, 2.40];
const BASE  = [1.5, 9.7, 13.1, 17.3, 21.9, 11.3, 15.7, 24.1];     // ms
const SLAP  = [2.0, 8.0, 12.5, 17.0, 22.0, 6.0, 15.0, 26.0];
const DBL   = [17, 29, 41, 53, 23, 35, 47, 61];
const RHO   = [1.00, 1.07, 0.93, 1.13, 0.89, 1.19, 0.83, 1.23];
const MAXV = 8, MAXAP = 24, MAXB = 16;

const clamp01 = v => v < 0 ? 0 : (v > 1 ? 1 : v);
const clampf  = (v, a, b) => v < a ? a : (v > b ? b : v);

class TerrainWiden extends AudioWorkletProcessor {
  constructor () {
    super();
    this.fs = sampleRate;
    this.n  = 1; while (this.n < Math.ceil(0.32 * this.fs) + 8) this.n <<= 1;
    this.mask = this.n - 1;
    this.bL = new Float32Array(this.n);
    this.bR = new Float32Array(this.n);
    this.wr = 0;

    this.p = { type:0, character:0, field:0, amount:0.35, width:0.5, rate:0.35, mix:0.5,
               p1:0.5, p2:0.85, p3:0.5, p4:0, p5:0, p6:0.5, p7:0, p8:0.5, retrig:false };
    this.lastRetrig = false;

    const K = ms => 1 - Math.exp(-1 / (ms * 0.001 * this.fs));
    this.smK = K(18); this.envK = K(20); this.lvlK = K(60); this.corrK = K(50);
    this.vfK = K(30); this.dipDn = K(5); this.dipUp = K(40); this.apexK = K(1.5);
    this.cmpK = K(5);
    this.dcR = 1 - (2 * Math.PI * 10 / this.fs);

    // smoothed scalars
    this.sm = { amt:0.35, wid:0.5, rate:0.3, mix:0.5, spr:0.85, off:1, wan:0, lk:0, ton:0, fb:0, bal:0.5 };
    this.tg = Object.assign({}, this.sm);
    this.tonL = 1; this.tonH = 1; this.tonLT = 1; this.tonHT = 1;
    this.dip = 1; this.pend = -1; this.seeded = false;

    // per-voice
    const A = () => new Float32Array(MAXV);
    this.baseS=A(); this.depS=A(); this.gain=A(); this.pan=A(); this.pl=A(); this.pr=A();
    this.baseG=A(); this.depG=A(); this.gainG=A(); this.panG=A(); this.plG=A(); this.prG=A();
    this.achC=A(); this.statC=A(); this.ratio=A(); this.rho=A(); this.rEff=A();
    this.ph=A(); this.triZ=A(); this.q=A(); this.res=A(); this.vfd=A(); this.prevD=A().fill(-1);
    this.wk = []; this.wkp = [];
    for (let v = 0; v < MAXV; ++v) {
      this.ph[v] = (v / MAXV + 0.037 * ((v * 7) % 5)) % 1;
      this.vfd[v] = v === 0 ? 1 : 0;
      this.wk.push({ st:0, tg:0, ph:0.113 * (v + 1) });
      this.wkp.push({ st:0, tg:0, ph:0.071 * (v + 3) });
    }
    this.apA = []; this.apB = [];
    for (let k = 0; k < MAXAP; ++k) { this.apA.push({x:0,y:0}); this.apB.push({x:0,y:0}); }
    this.apCa = new Float32Array(MAXAP); this.apCb = new Float32Array(MAXAP);
    this.apCaT = new Float32Array(MAXAP); this.apCbT = new Float32Array(MAXAP);
    this.bA = new Float32Array(MAXB); this.bz = new Float32Array(MAXB);
    this.lkZ=[0,0]; this.toneZ=[0,0]; this.fbZ=[0,0]; this.fbSt=[0,0]; this.dcX=[0,0]; this.dcY=[0,0];
    this.peZ=[0,0]; this.darkZ=[0,0]; this.cmpE=[0,0]; this.altZ=[0,0]; this.bassZ=[0,0];
    this.envIn=0; this.lvlSm=0; this.cLL=1e-9; this.cRR=1e-9; this.cLR=0;
    this.orbit=0; this.rotPh=0; this.ph2=0; this.rng=0x9E3779B9>>>0; this.vizTick=0;
    this.nV=6; this.nPair=1; this.nAP=18; this.nB=6;
    this.xkT=0.5; this.xkG=0.5; this.vNorm=1; this.vNormG=1; this.cTrim=1; this.walkHz=1;
    this.bandHard=false; this.bandCap=1;
    this.peA0 = this.oneP(700); this.bassA = this.oneP(150); this.altA = this.oneP(700);
    this.toneA = this.oneP(900); this.fbDampA = this.oneP(7000); this.lkA = 0; this.darkA = 0;

    this.viz = { corr:1, voicePan:new Array(8).fill(0), voiceCents:new Array(8).fill(0),
                 widthNow:0, lvl:0 };
    this.frame = 0;
    this.port.onmessage = e => {
      const d = e.data || {};
      if (d.type !== undefined && (d.type !== this.p.type || d.character !== this.p.character ||
          d.field !== this.p.field) && this.seeded) this.pend = 1;
      Object.assign(this.p, d);
      if (this.p.retrig && !this.lastRetrig) this.fireRetrig();
      this.lastRetrig = !!this.p.retrig;
      this.recalc();
    };
    this.recalc();
  }
  static get parameterDescriptors () { return []; }

  oneP (hz) { return 1 - Math.exp(-2 * Math.PI * clampf(hz, 1, 0.45 * this.fs) / this.fs); }
  apCoef (hz) { const t = Math.tan(Math.PI * clampf(hz, 20, 0.45 * this.fs) / this.fs);
                return clampf((t - 1) / (t + 1), -0.97, 0.97); }
  rnd () { this.rng = (Math.imul(this.rng, 1664525) + 1013904223) >>> 0; return this.rng / 4294967296; }
  static hash (k) { let s = (Math.imul(k, 2654435761) ^ 0x85EBCA6B) >>> 0;
                    s ^= s >>> 15; s = Math.imul(s, 2246822519) >>> 0; s ^= s >>> 13;
                    return (s >>> 0) / 4294967296; }

  fireRetrig () {
    for (let v = 0; v < MAXV; ++v) { this.res[v] = Math.sin(2 * Math.PI * this.ph[v]); this.ph[v] = 0; this.q[v] = 0; }
  }

  // ── everything derived, PER BLOCK — never per sample ────────────────────
  recalc () {
    const p = this.p, t = clampf(p.type|0, 0, 5), c = clampf(p.character|0, 0, 7);
    const T = SPEC[t], C = CHAR[t][c];
    this.T = T; this.C = C; this.typ = t; this.chr = c; this.fld = clampf(p.field|0, 0, 5);

    this.tg.rate = clampf(0.03 * Math.pow(14 / 0.03, clamp01(p.rate)), 0.02, 14);
    this.tg.amt = clamp01(p.amount); this.tg.wid = clamp01(p.width); this.tg.mix = clamp01(p.mix);
    this.tg.spr = Math.pow(clamp01(p.p2), 0.65);
    this.tg.off = 0.25 * Math.pow(16, clamp01(p.p3));          // unity at the knob centre
    this.tg.wan = Math.pow(clamp01(p.p4), 0.70) + C[3];
    this.tg.lk  = clamp01(p.p5);
    this.tg.ton = 2 * clamp01(p.p6) - 1;
    this.tg.fb  = T.fbMax * Math.pow(clamp01(p.p7), 0.35);     // taper in dB of BUILD-UP
    this.tg.bal = clamp01(p.p8);
    this.cTrim = C[8];
    this.tonLT = Math.pow(2, this.tg.ton *  2);                // ±12 dB tilt
    this.tonHT = Math.pow(2, this.tg.ton * -2);
    this.lkA = this.tg.lk < 0.02 ? 0 : this.oneP(40 * Math.pow(500 / 40, (this.tg.lk - 0.02) / 0.98));
    this.darkA = (C[7] > 0.5 && t === 1) ? this.oneP(C[7] * 1000) : 0;
    this.walkHz = (0.4 + 2.4 * clamp01(p.rate)) * C[2] * C[5];

    // VOICES = 3..8 copies. The floor is 3 because centre + ONE mover is a chorus.
    this.nV = clampf(3 + Math.round(clamp01(p.p1) * 5), 3, 8);

    const rHz = Math.max(0.02, this.tg.rate * T.rateMul * C[2]);
    const amtC = Math.pow(this.tg.amt, T.curve) * T.maxCents * C[1];
    const fan = (C[9] & F_EVENFAN) ? EVEN : JP;
    let fanMax = 1e-6; for (let v = 0; v < this.nV; ++v) fanMax = Math.max(fanMax, Math.abs(fan[v]));

    const b = this.tg.bal;
    const gC = clampf((-0.55366 * b + 0.99785) * Math.sqrt(Math.max(0, 1 - b)), 0, 1.2);
    const gS = clampf((-0.73764 * b * b + 1.2841 * b + 0.044372), 0, 1.4) / Math.sqrt(Math.max(1, this.nV - 1));

    let pw = 0;
    for (let v = 0; v < MAXV; ++v) {
      const fn = clampf(fan[v] / fanMax, -1, 1), w = Math.abs(fn);
      let cc = 0;
      if (T.mod === 0)      cc = amtC * w;
      else if (T.mod === 1) cc = amtC;
      else                  cc = amtC * (v === 0 ? (T.mod === 3 ? 0.35 : 0) : (0.45 + 0.55 * w));

      // 🔑 THE CONSTANT-CENTS LAW — the knob IS cents, the depth is solved for the rate
      this.rEff[v] = rHz * (T.mod === 3 ? C[2] : RHO[v]);
      let A = 0;
      if (T.mod === 0 || T.mod === 3) A = (Math.pow(2, cc / 1200) - 1) / (2 * Math.PI * this.rEff[v]);
      else if (T.mod === 1) { this.rEff[v] = rHz; A = (Math.pow(2, cc / 1200) - 1) / (4 * rHz); }
      else this.rEff[v] = rHz;
      A = Math.min(A, 0.110);
      this.depS[v] = A * this.fs;

      let baseMs = T.mod === 2 ? SLAP[v] : (T.mod === 3 ? DBL[v] : BASE[v]);
      if (T.mod === 1) baseMs = 5 * (1 + 0.6 * (v >> 1));
      baseMs *= C[0];
      if (T.mod === 3 && v === 0) baseMs += C[6];
      if (T.mod === 2) baseMs = Math.max(2, baseMs * C[7] - 0.5 * 30 * C[4]);
      // the base GROWS to fit the cents, then Offset scales the result
      let bs = Math.max(baseMs * 0.001 * this.fs, this.depS[v] / 0.85 + 0.0015 * this.fs);
      bs *= this.tg.off;
      this.baseS[v] = clampf(bs, 0.0015 * this.fs, 0.130 * this.fs);
      const Aeff = Math.min(this.depS[v] / this.fs, 0.85 * this.baseS[v] / this.fs);
      this.achC[v] = (T.mod === 1) ? 1200 * Math.log2(1 + 4 * this.rEff[v] * Aeff)
                                   : 1200 * Math.log2(1 + 2 * Math.PI * this.rEff[v] * Aeff);
      this.depS[v] = Aeff * this.fs;

      let sc = 0;
      if (T.mod === 2 && v > 0) {
        sc = cc * (fn >= 0 ? 1 : -1);
        if (C[9] & F_ALLNEG) sc = -Math.abs(cc) * (0.6 + 0.4 * (v & 1));
        if (C[5] > 1 && v >= this.nV - 2) sc = C[5];
      }
      if ((C[9] & F_STATIC) && v > 0) sc = C[7] * ((v & 1) ? 1 : -1);
      if ((C[9] & F_OCTTOP) && v >= this.nV - 2 && v > 0) sc = 1200;
      if ((C[9] & F_SUB) && v === 1) sc = -1200;
      this.statC[v] = sc; this.ratio[v] = Math.pow(2, sc / 1200);

      // the CENTS fan normalises to the live voices; the PAN LADDER does not
      const lad = 0.42 + 0.58 * (v - 1) / 6;
      let pan = v === 0 ? 0 : (fn >= 0 ? 1 : -1) * lad * this.tg.spr;
      if ((C[9] & F_SUB) && v === 1) pan = 0;
      this.pan[v] = pan;
      this.pl[v] = Math.sqrt(0.5 * (1 - pan)); this.pr[v] = Math.sqrt(0.5 * (1 + pan));

      let g = v === 0 ? gC : gS;
      if (T.mod === 3) g *= Math.pow(10, -1.5 * v / 20);
      if (((C[9] & F_OCTTOP) && v >= this.nV - 2 && v > 0) || ((C[9] & F_SUB) && v === 1) ||
          (C[5] > 1 && T.mod === 2 && v >= this.nV - 2 && v > 0)) g *= (C[6] > 0 ? C[6] : 0.25);
      this.gain[v] = v < this.nV ? g : 0;
      if (v < this.nV) pw += g * g;
      this.rho[v] = RHO[v] * (1 + (T.mod === 0 ? C[5] * (TerrainWiden.hash(v) - 0.5) * 2 : 0));
      if (C[9] & F_3PH) this.rho[v] = 1;
    }
    this.vNorm = Math.SQRT2 / Math.sqrt(Math.max(0.25, pw));

    this.nPair = clampf(((this.nV + 1) >> 1) + (C[5] | 0) - 1, 1, 4);
    this.xkT = clampf((0.25 + 0.40 * this.tg.amt) * C[6], 0, 1.05);

    this.nAP = clampf(Math.round(C[5] * this.nV), 2, MAXAP);
    if (t === 4) {
      const lo = C[6] * (1 - 0.75 * this.tg.amt), hi = C[7];
      for (let k = 0; k < this.nAP; ++k) {
        const u = this.nAP > 1 ? k / (this.nAP - 1) : 0.5;
        const f0 = lo * Math.pow(hi / lo, u), sg = 0.5 + TerrainWiden.hash(k * 3 + 1);
        const div = 0.30 * this.tg.amt;
        let fa = f0 * Math.pow(2,  div * sg), fb = f0 * Math.pow(2, -div * sg);
        fa *= 1 + 0.25 * this.tg.wan * (TerrainWiden.hash(k * 5 + 2) - 0.5);
        fb *= 1 + 0.25 * this.tg.wan * (TerrainWiden.hash(k * 5 + 3) - 0.5);
        this.apCaT[k] = this.apCoef(clampf(fa * this.tg.off, 20, 0.45 * this.fs));
        this.apCbT[k] = this.apCoef(clampf(fb * this.tg.off, 20, 0.45 * this.fs)) * ((C[9] & F_NEGB) ? -1 : 1);
      }
    }
    this.nB = clampf(Math.round(C[5] * this.nV), 2, MAXB);
    if (t === 5) {
      this.nB = clampf(this.nB + (this.nB & 1), 2, MAXB);         // an odd count is not complementary
      const lo = C[6] * this.tg.off, hi = 11000 * this.tg.off;
      const gsh = 0.5 * Math.sin(2 * Math.PI * this.rotPh) / this.nB;
      for (let k = 0; k < this.nB - 1; ++k)
        this.bA[k] = this.oneP(clampf(lo * Math.pow(hi / lo, (k + 1) / this.nB + gsh), 20, 0.45 * this.fs));
      this.bandHard = C[7] < 0.99;
      this.bandCap = (this.chr === 5) ? 0.42 : 1;
    }
  }

  readH (buf, d) {
    d = clampf(d, 1, this.n - 6);
    const i = d | 0, f = d - i, p = this.wr - i, m = this.mask;
    const ym1 = buf[(p + 1) & m], y0 = buf[p & m], y1 = buf[(p - 1) & m], y2 = buf[(p - 2) & m];
    const c1 = 0.5 * (y1 - ym1);
    const c2 = ym1 - 2.5 * y0 + 2 * y1 - 0.5 * y2;
    const c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
  }
  readL (buf, d) { d = clampf(d, 1, this.n - 6); const i = d | 0, f = d - i, p = this.wr - i, m = this.mask;
                   const y0 = buf[p & m], y1 = buf[(p - 1) & m]; return y0 + f * (y1 - y0); }
  tickWalk (w, hz) { w.ph += hz / this.fs;
                     if (w.ph >= 1) { w.ph -= 1; w.tg = 2 * this.rnd() - 1; }
                     w.st += (w.tg - w.st) * clampf(hz * 6 / this.fs, 1e-5, 0.5); }
  static compG (env, e) { const r = Math.max(env, 1e-6) / 0.0125; return r <= 1 ? 1 : Math.pow(r, e); }

  process (inputs, outputs) {
    const inp = inputs[0], out = outputs[0];
    if (!out || out.length < 2) return true;
    const oL = out[0], oR = out[1];
    const iL = (inp && inp[0]) ? inp[0] : new Float32Array(oL.length);
    const iR = (inp && inp[1]) ? inp[1] : iL;
    const N = oL.length, sm = this.sm, tg = this.tg, T = this.T, C = this.C;

    if (!this.seeded) {
      Object.assign(sm, tg);
      for (let v = 0; v < MAXV; ++v) { this.vfd[v] = v < this.nV ? 1 : 0;
        this.baseG[v]=this.baseS[v]; this.depG[v]=this.depS[v]; this.panG[v]=this.pan[v];
        this.gainG[v]=this.gain[v]; this.plG[v]=this.pl[v]; this.prG[v]=this.pr[v]; }
      this.vNormG = this.vNorm; this.xkG = this.xkT; this.tonL = this.tonLT; this.tonH = this.tonHT;
      this.seeded = true;
    }

    for (let i = 0; i < N; ++i) {
      // fade-swap-recover — a Field change can swap `Side Only` for `Collapse`, an 11x
      // level change, so the dip floor is -46 dB and not -26.
      if (this.pend > 0) { this.dip += this.dipDn * (0 - this.dip);
                           if (this.dip < 0.005) { this.pend = -1; this.recalc();
                             for (let v = 0; v < MAXV; ++v) { this.vfd[v] = v < this.nV ? 1 : 0;
                               this.baseG[v]=this.baseS[v]; this.depG[v]=this.depS[v];
                               this.panG[v]=this.pan[v]; this.gainG[v]=this.gain[v];
                               this.plG[v]=this.pl[v]; this.prG[v]=this.pr[v]; }
                             this.vNormG=this.vNorm; this.xkG=this.xkT;
                             for (let k=0;k<MAXAP;++k){this.apA[k].x=this.apA[k].y=0;this.apB[k].x=this.apB[k].y=0;}
                             for (let k=0;k<MAXB;++k) this.bz[k]=0; } }
      else this.dip += this.dipUp * (1 - this.dip);

      for (const k in sm) sm[k] += this.smK * (tg[k] - sm[k]);
      this.tonL += this.smK * (this.tonLT - this.tonL);
      this.tonH += this.smK * (this.tonHT - this.tonH);
      for (let v = 0; v < MAXV; ++v) {
        this.baseG[v] += this.smK * (this.baseS[v] - this.baseG[v]);
        this.depG[v]  += this.smK * (this.depS[v]  - this.depG[v]);
        this.panG[v]  += this.smK * (this.pan[v]   - this.panG[v]);
        this.gainG[v] += this.smK * (this.gain[v]  - this.gainG[v]);
        this.plG[v]   += this.smK * (this.pl[v]    - this.plG[v]);
        this.prG[v]   += this.smK * (this.pr[v]    - this.prG[v]);
      }
      this.vNormG += this.smK * (this.vNorm - this.vNormG);
      this.xkG    += this.smK * (this.xkT   - this.xkG);
      this.vizTick = (this.vizTick + 1) & 63;

      const inL = iL[i], inR = iR[i];
      const rect = 0.5 * (Math.abs(inL) + Math.abs(inR));
      this.envIn += this.envK * (rect - this.envIn);
      const gate = this.envIn / (this.envIn + 0.003);

      // LOW KEEP: split first, re-join last, immune to Width AND to Field
      let hiL = inL, hiR = inR, loM = 0;
      if (this.lkA > 0) { this.lkZ[0] += this.lkA * (inL - this.lkZ[0]);
                          this.lkZ[1] += this.lkA * (inR - this.lkZ[1]);
                          hiL = inL - this.lkZ[0]; hiR = inR - this.lkZ[1];
                          loM = 0.5 * (this.lkZ[0] + this.lkZ[1]); }

      let fbL = 0, fbR = 0;
      if (sm.fb > 1e-4) for (let c = 0; c < 2; ++c) {
        let s = Math.tanh(this.fbSt[c] * 1.4) * 0.714286;
        this.fbZ[c] += this.fbDampA * (s - this.fbZ[c]); s = this.fbZ[c];
        const y = s - this.dcX[c] + this.dcR * this.dcY[c];
        this.dcX[c] = s; this.dcY[c] = y;
        if (c === 0) fbL = y * sm.fb * gate; else fbR = y * sm.fb * gate;
      }
      const lineL = hiL + fbL, lineR = hiR + fbR;
      this.bL[this.wr] = lineL; this.bR[this.wr] = lineR;

      let wetL = 0, wetR = 0;
      const limHi = this.n - 8;
      if (T.family === 0)      [wetL, wetR] = this.voices(limHi);
      else if (T.family === 1) [wetL, wetR] = this.twin(limHi);
      else if (T.family === 2) [wetL, wetR] = this.blur(lineL, lineR);
      else                     [wetL, wetR] = this.bands(lineL, lineR);
      this.wr = (this.wr + 1) & this.mask;

      if (Math.abs(this.tonL - 1) > 0.002) {
        this.toneZ[0] += this.toneA * (wetL - this.toneZ[0]);
        this.toneZ[1] += this.toneA * (wetR - this.toneZ[1]);
        wetL = this.toneZ[0] * this.tonL + (wetL - this.toneZ[0]) * this.tonH;
        wetR = this.toneZ[1] * this.tonL + (wetR - this.toneZ[1]) * this.tonH;
      }
      this.fbSt[0] = wetL; this.fbSt[1] = wetR;      // the tap is OUTSIDE Field and Width

      // FIELD — the placement matrix
      switch (this.fld) {
        case 1: { this.altZ[0] += this.altA * (wetL - this.altZ[0]);
                  this.altZ[1] += this.altA * (wetR - this.altZ[1]);
                  const l0 = this.altZ[0], l1 = this.altZ[1];
                  const h0 = wetL - l0, h1 = wetR - l1; wetL = l0 + h1; wetR = l1 + h0; break; }
        case 2: { this.orbit += (sm.rate * 0.20) / this.fs; if (this.orbit >= 1) this.orbit -= 1;
                  const th = 2 * Math.PI * this.orbit, cs = Math.cos(th), sn = Math.sin(th);
                  const a = wetL * cs - wetR * sn, b2 = wetL * sn + wetR * cs; wetL = a; wetR = b2; break; }
        case 3: { const t2 = wetL; wetL = wetR; wetR = t2; break; }
        case 4: { const s = 0.5 * (wetL - wetR); wetL = s; wetR = -s; break; }
        case 5: { const m = 0.5 * (wetL + wetR); wetL = m; wetR = m; break; }
      }

      // WIDTH — equal-power M/S rotation. 0 = mono · 0.5 = EXACTLY neutral · 1.0 = SIDE ONLY
      { const m = 0.5 * (wetL + wetR), s = 0.5 * (wetL - wetR), th = sm.wid * Math.PI / 2;
        const mm = m * Math.SQRT2 * Math.cos(th), ss = s * Math.SQRT2 * Math.sin(th);
        wetL = mm + ss; wetR = mm - ss; }
      wetL += loM; wetR += loM;

      const trim = T.trim * this.cTrim * this.dip;
      const wL = wetL * trim, wR = wetR * trim;
      const dg = Math.cos(sm.mix * Math.PI / 2), wg = Math.sin(sm.mix * Math.PI / 2);
      const yL = inL * dg + wL * wg, yR = inR * dg + wR * wg;
      oL[i] = yL; oR[i] = yR;

      this.cLL += this.corrK * (yL * yL - this.cLL);
      this.cRR += this.corrK * (yR * yR - this.cRR);
      this.cLR += this.corrK * (yL * yR - this.cLR);
      this.lvlSm += this.lvlK * (0.5 * (Math.abs(wL) + Math.abs(wR)) - this.lvlSm);
      { const mm = Math.abs(0.5 * (wL + wR)), ss = Math.abs(0.5 * (wL - wR));
        this.viz.widthNow = ss / Math.max(1e-9, mm + ss); }
    }

    this.viz.corr = this.cLR / Math.sqrt(Math.max(1e-12, this.cLL * this.cRR));
    this.viz.lvl  = Math.min(1, this.lvlSm * 14);
    for (let v = 0; v < 8; ++v) this.viz.voicePan[v] = this.panG[v];
    if ((++this.frame % 4) === 0) this.port.postMessage(this.viz);   // ~60 Hz push
    return true;
  }

  // ── the voice crowd: Stack (sine) · Shift (static granular) · Double (walk) ──
  voices (limHi) {
    const T = this.T, C = this.C, sm = this.sm;
    const rHz = Math.max(0.02, sm.rate * T.rateMul * C[2]);
    let aL = 0, aR = 0;
    for (let v = 0; v < MAXV; ++v) {
      const tgt = v < this.nV ? 1 : 0;
      this.vfd[v] += this.vfK * (tgt - this.vfd[v]);
      if (this.vfd[v] < 1e-4 && !tgt) { this.viz.voiceCents[v] = 0; continue; }
      let d = this.baseG[v];
      if (T.mod === 0) {
        const r = (C[9] & F_3PH) ? rHz * 0.75 : rHz * this.rho[v];
        this.ph[v] += r / this.fs; if (this.ph[v] >= 1) this.ph[v] -= 1;
        let s = Math.sin(2 * Math.PI * (this.ph[v] + ((C[9] & F_3PH) ? (v % 3) / 3 : 0)));
        if (C[9] & F_3PH) { this.ph2 += (6.1 * Math.sqrt(Math.max(0.05, rHz))) / this.fs;
                            if (this.ph2 >= 1) this.ph2 -= 1;
                            s = s * 0.88 + 0.12 * Math.sin(2 * Math.PI * (this.ph2 + (v % 3) / 3)); }
        if (this.res[v] !== 0) { const st = 0.5 / Math.max(1, this.depG[v]);
          this.res[v] -= this.res[v] > 0 ? Math.min(this.res[v], st) : Math.max(this.res[v], -st); }
        s += this.res[v];                       // ADD: it cancels the old offset, not doubles it
        d += this.depG[v] * s;
      } else if (T.mod === 3) {
        this.tickWalk(this.wk[v], this.walkHz);
        d += this.depG[v] * ((C[9] & F_STATIC) ? 0 : this.wk[v].st);
      }
      if (sm.wan > 1e-3 && T.mod !== 3) { this.tickWalk(this.wk[v], 0.35 + 1.2 * sm.wan);
                                          d += sm.wan * 0.012 * this.fs * this.wk[v].st; }
      d = clampf(d, 2, limHi);
      if (this.prevD[v] < 0) this.prevD[v] = d;
      if (this.vizTick === 0) {
        this.viz.voiceCents[v] = (T.mod === 2) ? this.statC[v]
          : 1200 * Math.log2(clampf(1 - (d - this.prevD[v]) / 64, 0.25, 4)) + this.statC[v];
        this.prevD[v] = d;
      }
      const buf = (v & 1) === 0 ? this.bL : this.bR;
      let y;
      if (T.mod === 2 && v > 0 && Math.abs(this.statC[v]) > 0.02) {
        const span = 30 * C[4] * 0.001 * this.fs, r = this.ratio[v] - 1;
        this.q[v] += r / Math.max(1, span);
        if (this.q[v] >= 1) this.q[v] -= 1; else if (this.q[v] < 0) this.q[v] += 1;
        const w0 = 0.5 - 0.5 * Math.cos(2 * Math.PI * this.q[v]);
        let q1 = this.q[v] + 0.5; if (q1 >= 1) q1 -= 1;
        const o0 = r > 0 ? span * (1 - this.q[v]) : span * this.q[v];
        const o1 = r > 0 ? span * (1 - q1) : span * q1;
        const rd = (C[9] & F_LIN) ? this.readL.bind(this) : this.readH.bind(this);
        y = w0 * rd(buf, clampf(d + o0, 2, limHi)) + (1 - w0) * rd(buf, clampf(d + o1, 2, limHi));
      } else y = this.readH(buf, d);
      const g = this.gainG[v] * this.vfd[v] * this.vNormG;
      let gl = this.plG[v], gr = this.prG[v];
      if (sm.wan > 1e-3) { this.tickWalk(this.wkp[v], 0.22 + 0.75 * sm.wan);
        const pwd = sm.wan * 0.55 * this.wkp[v].st;
        gl = clampf(gl - pwd, 0, 1.35); gr = clampf(gr + pwd, 0, 1.35); }
      aL += g * gl * y; aR += g * gr * y;
    }
    return [aL, aR];
  }

  // ── TWIN: the SDD-320. TRIANGLE in antiphase + INVERTED cross-mix.
  //    The compander is AFTER the delay on purpose — see the header comment in the C++:
  //    writing companded audio into the shared ring makes a Type swap a STEP in the stored
  //    signal, which comes out one delay-time later, long after any dip has recovered.
  twin (limHi) {
    const T = this.T, C = this.C, sm = this.sm;
    const rHz = Math.max(0.02, sm.rate * T.rateMul * C[2]);
    let wl = 0, wr = 0;
    const np = clampf(this.nPair, 1, 4), gn = 1 / Math.sqrt(np);
    for (let p = 0; p < np; ++p) {
      this.ph[p] += (rHz * (1 + 0.13 * p)) / this.fs; if (this.ph[p] >= 1) this.ph[p] -= 1;
      const x = this.ph[p];
      let m = (C[9] & F_SINE) ? Math.sin(2 * Math.PI * x)
                              : (x < 0.5 ? 4 * x - 1 : 3 - 4 * x);       // ← THE TRIANGLE
      this.triZ[p] += this.apexK * (m - this.triZ[p]); m = this.triZ[p]; // 1.5 ms apex round ONLY
      const inv = (p & 1) ? -1 : 1;
      const dL = clampf(this.baseG[p * 2] + this.depG[p * 2] * m * inv, 2, limHi);
      const dR = clampf(this.baseG[p * 2] - this.depG[p * 2] * m * inv, 2, limHi);
      wl += gn * this.readH(this.bL, dL); wr += gn * this.readH(this.bR, dR);
      if (p === 0) {
        if (this.prevD[0] < 0) { this.prevD[0] = dL; this.prevD[1] = dR; }
        if (this.vizTick === 0) {
          this.viz.voiceCents[0] = 1200 * Math.log2(clampf(1 - (dL - this.prevD[0]) / 64, 0.25, 4));
          this.viz.voiceCents[1] = 1200 * Math.log2(clampf(1 - (dR - this.prevD[1]) / 64, 0.25, 4));
          this.prevD[0] = dL; this.prevD[1] = dR;
        }
        for (let z = 2; z < 8; ++z) this.viz.voiceCents[z] = 0;
      }
    }
    const k = this.xkG;
    let cl = wl - k * wr, cr = wr - k * wl;            // ← INVERTED CROSS-MIX
    if (this.darkA > 0) { this.darkZ[0] += this.darkA * (cl - this.darkZ[0]);
                          this.darkZ[1] += this.darkA * (cr - this.darkZ[1]);
                          cl = this.darkZ[0]; cr = this.darkZ[1]; }
    if (!(C[9] & F_COMPOFF)) {
      this.cmpE[0] += this.cmpK * (Math.abs(cl) - this.cmpE[0]);
      this.cmpE[1] += this.cmpK * (Math.abs(cr) - this.cmpE[1]);
      cl *= TerrainWiden.compG(this.cmpE[0], -0.5); cr *= TerrainWiden.compG(this.cmpE[1], -0.5);
      this.peZ[0] += this.peA0 * (cl - this.peZ[0]); this.peZ[1] += this.peA0 * (cr - this.peZ[1]);
      cl = cl * 0.72 + this.peZ[0] * 0.56; cr = cr * 0.72 + this.peZ[1] * 0.56;
    }
    let mm = 0.5 * (cl + cr); const ss = 0.5 * (cl - cr);
    this.bassZ[0] += this.bassA * (mm - this.bassZ[0]);
    mm += 0.26 * this.bassZ[0];                        // the documented dry-bass compensation
    this.viz.voicePan[0] = -0.85; this.viz.voicePan[1] = 0.85;
    return [mm + ss, mm - ss];
  }

  // ── BLUR: two allpass cascades. |H| = 1 for any |c| < 1, so per-channel magnitude is
  //    EXACTLY flat at every setting — only the phase differs.
  blur (lineL, lineR) {
    const m = 0.5 * (lineL + lineR), s0 = 0.5 * (lineL - lineR);
    let a = m, b = m;
    for (let k = 0; k < this.nAP; ++k) {
      this.apCa[k] += this.smK * (this.apCaT[k] - this.apCa[k]);
      this.apCb[k] += this.smK * (this.apCbT[k] - this.apCb[k]);
      let st = this.apA[k], c = this.apCa[k];
      let y = c * a + st.x - c * st.y; st.x = a; st.y = y; a = y;
      st = this.apB[k]; c = this.apCb[k];
      y = c * b + st.x - c * st.y; st.x = b; st.y = y; b = y;
    }
    const th = (0.35 + 0.65 * this.sm.bal) * Math.PI / 2;
    const ga = Math.sin(th), gm = Math.cos(th);
    a = a * ga + m * gm; b = b * ga + m * gm;
    for (let v = 0; v < 8; ++v) { this.viz.voicePan[v] = v < this.nV ? ((v & 1) ? 0.9 : -0.9) * this.sm.spr : 0;
                                  this.viz.voiceCents[v] = 0; }
    return [a + s0, b - s0];
  }

  // ── BANDS: a one-pole crossover TREE. lp + (x − lp) = x exactly, and the band gains
  //    (1 + g) and (1 − g) sum to 2 for ANY g — so the mono fold is the input, bit for bit,
  //    even past g = 1 where the quiet channel's gain goes NEGATIVE.
  bands (lineL, lineR) {
    const C = this.C, m = 0.5 * (lineL + lineR), s0 = 0.5 * (lineL - lineR);
    this.rotPh += (this.sm.rate * 0.25 * C[2]) / this.fs; if (this.rotPh >= 1) this.rotPh -= 1;
    const con = clampf((this.bandHard ? Math.sqrt(this.sm.amt) : this.sm.amt) * 1.8 * this.bandCap, 0, 1.8);
    const nrm = 1 / Math.sqrt(1 + con * con);
    let rest = m, al = 0, ar = 0;
    for (let k = 0; k < this.nB; ++k) {
      let band;
      if (k < this.nB - 1) { this.bz[k] += this.bA[k] * (rest - this.bz[k]); band = this.bz[k]; rest -= band; }
      else band = rest;
      let sk = (k & 1) ? -1 : 1;
      if (C[9] & F_TILT) sk *= 1 + 0.18 * ((k & 1) ? 1 : -1);
      al += band * (1 + con * sk); ar += band * (1 - con * sk);
    }
    al *= nrm; ar *= nrm;
    const th = (0.35 + 0.65 * this.sm.bal) * Math.PI / 2;
    const ga = Math.sin(th), gm = Math.cos(th);
    for (let v = 0; v < 8; ++v) { this.viz.voicePan[v] = v < this.nB ? ((v & 1) ? -1 : 1) * clamp01(con / 1.8) * this.sm.spr : 0;
                                  this.viz.voiceCents[v] = 0; }
    return [al * ga + m * gm + s0, ar * ga + m * gm - s0];
  }
}

registerProcessor('terrain-widen', TerrainWiden);
