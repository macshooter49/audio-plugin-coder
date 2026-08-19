// ─────────────────────────────────────────────────────────────────────────────
// compress-worklet.js — the COMPRESS device (chain kind 11) as an AudioWorkletProcessor,
// so Max can HEAR it in a Safari mockup before any integration (the fb296 law).
//
// Same Types, same Characters, same knob names, same laws as TerrainCompressFx.h. NOT
// sample-identical to the C++ — the Character tables here carry the load-bearing rows only —
// but recognisably the same machine: the same dBp threshold system, the same slope-form gain
// computer (so ∞:1 and OverEasy's negative zone are the SAME formula), the same ballistic
// shapes, the same GR-gated colour.
//
// Usage:
//   await ctx.audioWorklet.addModule('compress-worklet.js');
//   const n = new AudioWorkletNode(ctx, 'terrain-compress', { numberOfInputs: 1,
//                                                             outputChannelCount: [2] });
//   n.port.postMessage({ type: 3, character: 0, push: 0.55, ratio: 0.85, lift: 0.3, mix: 1,
//                        b1: 0.4, b2: 0.5, b3: 0.25, b4: 0, b5: 0.5, b6: 0, b7: 1, b8: 0.3,
//                        axis: 0, autoMakeup: false });
//   n.port.onmessage = e => draw(e.data);   // { grDb, inDb, outDb, knee[32], lvl }
// ─────────────────────────────────────────────────────────────────────────────

// ═════ LABELS — MIRRORED FROM TerrainCompressFx.h, WHICH IS THE SOURCE OF TRUTH ═════
// (frontNames / backNames / dropdownNames / pillName / typeNames / charNames / detectNames).
// If these ever disagree with the header, the header wins and this file is the bug.
const DEVICE = 'Compress';
const FRONT  = ['Push', 'Ratio', 'Lift', 'Mix'];
const BACK   = ['Attack', 'Release', 'Round', 'Hear Cut', 'Edge', 'Cling', 'Tie', 'Burn'];
const DROPS  = ['Character', 'Detect'];
const PILL   = 'Auto';
const TYPES = ['Exact', 'Bus', 'FET 76', 'Opto', 'Vari-Mu', 'OverEasy', 'Ride', 'Limit'];
const DETECT = ['Native', 'Peak', 'Average', 'Patient', 'Spike'];
const CHARS = [
  ['Precise', 'Soft Touch', 'Loose Grip', 'Blunt', 'Deep Release', 'Line Attack', 'Poise', 'Judder'],
  ['Quad Bus', 'Hand Set', 'Two Easy', 'Ten Punchy', 'Fast City', 'Big Desk', 'Pump Bus', 'No Diode'],
  ['Blackface', 'Blue Stripe', 'All Buttons', 'Twenty Lock', 'Loose Four', 'Broken Bias', 'Waiting Fet', 'Two Pass'],
  ['Cell Classic', 'Fresh Cell', 'Tired Cell', 'Quick Cell', 'Even Pools', 'Crystal', 'Tube Stage', 'Bright Ears'],
  ['Studio 670', 'Time One', 'Time Four', 'Auto Peaks', 'Long Haul', 'Push Pull', 'Lateral', 'Triode Soft'],
  ['Over Easy', 'Hard 160', 'Infinity', 'Infinity Plus', 'Slow Window', 'Crush RMS', 'Decilinear', 'Anti'],
  ['Level Rider', 'Deep Floor', 'Only Up', 'Only Down', 'Fast Clamp', 'Slow Iron', 'Bright Bias', 'Vocal Sit'],
  ['Clean Wall', 'Soft Ceiling', 'Hard Stop', 'Pump Limit', 'Loud War', 'Clip Guard', 'Springy', 'Porous'],
];

// ── the bus law: 0 dBp = −26.02 dBFS = a single note on the Terrain FX bus ──
const BUS_NOM = 0.05, BUS_DB = -26.0206, DET_LIFT = 20.0;

// atkLo atkHi relLo relHi  fb  det  knLo knHi sMax sCap force relShape up clip heat  grK
const TSPEC = [
  { a: [0.05, 300], r: [5, 2500],  fb: 0, det: 'peak', kn: [0, 30], sMax: 1, sCap: 1,     force: 0,   rel: 'exp',   up: 0, clip: 0, heat: 'gentle', grK: 0 },
  { a: [0.01, 30],  r: [100, 1200],fb: 0, det: 'peak', kn: [0, 12], sMax: 1, sCap: 1,     force: 0,   rel: 'adapt', up: 0, clip: 0, heat: 'gentle', grK: 0 },
  { a: [0.02, 0.8], r: [50, 1100], fb: 1, det: 'peak', kn: [0, 12], sMax: 1, sCap: 1,     force: 0,   rel: 'exp',   up: 0, clip: 0, heat: 'fet',    grK: 0 },
  { a: [2, 50],     r: [40, 200],  fb: 1, det: 'rms10',kn: [6, 20], sMax: 1, sCap: 0.833, force: 0,   rel: 'opto',  up: 0, clip: 0, heat: 'asym',   grK: 0 },
  { a: [0.2, 50],   r: [200, 25000],fb: 1,det: 'rms5', kn: [2, 18], sMax: 1, sCap: 1,     force: 0,   rel: 'exp',   up: 0, clip: 0, heat: 'asym',   grK: 0.0556 },
  { a: [1, 80],     r: [40, 2000], fb: 0, det: 'rmsw', kn: [6, 24], sMax: 2, sCap: 2,     force: 0,   rel: 'exp',   up: 0, clip: 0, heat: 'deci',   grK: 0 },
  { a: [0.5, 100],  r: [20, 1000], fb: 0, det: 'rms10',kn: [0, 18], sMax: 1, sCap: 1,     force: 0,   rel: 'exp',   up: 1, clip: 0, heat: 'gentle', grK: 0 },
  { a: [0.1, 5],    r: [20, 500],  fb: 0, det: 'spike',kn: [0, 6],  sMax: 1, sCap: 1,     force: 0.9, rel: 'exp',   up: 0, clip: 1, heat: 'gentle', grK: 0 },
];

// Character rows: only the fields that differ from the default are listed.
const D = {};   // the neutral Character
const CSPEC = [
  // R6/fb418: NO Character sets `det` any more — `Detect` owns detection outright. The two
  // rows that used to (`RMS Ears`, `Spike Ears`) are now `Loose Grip` (the slope is capped at
  // 2.5:1 with 8 dB of extra knee) and `Blunt` (the curve applied twice at half slope).
  [D, { kneeAuto: 1 }, { kn: 8, sMul: 0.6, sCap: 0.6 }, { twoPass: 1 }, { deepRel: 1 }, { lineAtk: 1 },
      { rel: 'damped', zeta: 1.0 }, { rel: 'damped', zeta: 0.42 }],
  [{ rel: 'dual', kn: 4, link: 1 }, { rel: 'exp', kn: 4, link: 1, rW: [2, 0.667] },
   { rel: 'dual', kn: 6, sMul: 0.5, sCap: 0.5, thr: 2, link: 1 },
   { rel: 'adapt', kn: 2, sFloor: 0.75, link: 1 }, { rel: 'adapt', kn: 2, aMul: 0.03, link: 1 },
   { rel: 'dual', kn: 4, aMul: 2.2, heatF: 0.65, link: 1 }, { rel: 'exp', kn: 4, rMul: 0.12, link: 1 },
   { rel: 'exp', link: 1 }],
  [{ heatF: 0.20 }, { heatF: 0.55, asym: 1 }, { kn: 10, heatF: 0.45, rel: 'damped', zeta: 0.6, plateau: 1 },
   { aMul: 0.15, sMul: 4, sCap: 0.95, sFloor: 0.95, thr: -4, heatF: 0.30 },
   { sMul: 0.75, sCap: 0.75, heatF: 0.15, deepRel: 1 }, { heatF: 0.65, asym: 1 },
   { aW: [20, 20], heatF: 0.20 }, { heatF: 0.20, twoPass: 1 }],
  [{ tilt: 0.35 }, { tilt: 0.35, rMul: 0.6, mem: 0.22 }, { tilt: 0.35, mem: 3.0 },
   { tilt: 0.35, aMul: 0.25, rMul: 0.25, mem: 0.45 }, { tilt: 0.35, evenPool: 1 },
   { tilt: 0.35, aMul: 4.0, rMul: 1.8, kn: 14, mem: 1.6 }, { tilt: 0.35, heatF: 0.40 }, { tilt: 3.2 }],
  [{ heatF: 0.25 }, { heatF: 0.25, aW: [1, 0.1], rW: [0.5, 0.036] }, { heatF: 0.25, aW: [4, 0.4], rW: [7.5, 0.6] },
   { heatF: 0.25, rel: 'dual' }, { heatF: 0.25, rW: [25, 1], rel: 'dual' }, { heatF: 0.60, asym: 1 },
   { heatF: 0.25, msDet: 1 }, { heatF: 0.10, kn: 8, sCap: 0.75, grK: -0.0556 }],
  [{ kn: 6 }, { kn: -24, sCap: 1 }, { kn: 6, sMul: 1.43, sCap: 1 }, { kn: 6, sMul: 1.35, sCap: 2 },
   { kn: 6, rmsW: 9.0, rMul: 1.8 }, { kn: 6, rmsW: 0.10 }, { kn: 6, heatF: 0.40 },
   { kn: 6, sMul: 2, sCap: 2, sFloor: 2 }],
  [D, { upS: 1.35, upT: 10, upCap: 36 }, { upS: 1.2, upCap: 30, upOnly: 1 }, { dnOnly: 1 },
   { aMul: 0.06, rMul: 0.25 }, { aMul: 2, rMul: 4 }, { tilt: 3.2, upS: 1.15 },
   { rMul: 0.5, tilt: -2, hcMin: 260, upT: -6 }],
  [D, { kn: 6 }, { aMul: 0.02 }, { rW: [1, 4], thr: -6 }, { autoFull: 1 }, { thr: -3, clipOn: 1 },
   { rMul: 2.5, deepRel: 1 }, { leaky: 1 }],
];

const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const db2lin = d => Math.pow(10, d / 20);
const lin2db = x => 20 * Math.log10(Math.max(x, 1e-20));
const expMap = (t, lo, hi) => lo * Math.pow(hi / lo, clamp(t, 0, 1));
const coefTau = (tau, fs) => (tau <= 1e-7 ? 1 : 1 - Math.exp(-1 / (fs * tau)));
const softClip = x => (x > 1.4 || x < -1.4 ? Math.tanh(x) : x);
const fastTanh = x => (x > 5 ? 1 : x < -5 ? -1 : (x * (27 + x * x)) / (27 + 9 * x * x));

/** the downward gain computer, SLOPE form — s = 1−1/R, so s = 1 is ∞:1 and s = 2 is −1:1 */
function grDown(xdb, T, s, W) {
  const d = xdb - T;
  if (W > 1e-4) {
    if (2 * d < -W) return 0;
    if (2 * d <= W) { const u = d + 0.5 * W; return (s * u * u) / (2 * W); }
  } else if (d <= 0) return 0;
  return s * d;
}
function liftUp(xdb, Tup, sUp, cap) {
  const u = Tup - xdb;
  return u <= 0 ? 0 : Math.min(sUp * u, cap);
}
/** the floor gate — upward compression that dies with the note, not a comparator */
function floorGate(xdb, F, ramp) {
  const t = clamp((xdb - F) / ramp, 0, 1);
  return t * t * (3 - 2 * t);
}

class TerrainCompress extends AudioWorkletProcessor {
  constructor() {
    super();
    this.fs = sampleRate;
    this.p = { type: 0, character: 0, axis: 0, push: 0.2, ratio: 0.5, lift: 0.25, mix: 1,
               b1: 0.61, b2: 0.63, b3: 0.25, b4: 0, b5: 0.5, b6: 0, b7: 1, b8: 0,
               autoMakeup: false };
    this.st = [0, 1].map(() => ({
      hc: 0, tilt: 0, ms: 0, dcx: 0, dcy: 0, fb: 0, fbs: 0, hold: 0, holdN: 0,
      gr: 0, grF: 0, grS: 0, mem: 0, v2: 0, y2: 0, pf: 0, ps: 0, latch: 0, latchH: 0,
    }));
    this.gl = { T: 9, s: 0.5, W: 6, tie: 1, heat: 0, mix: 1, lift: 0, mk: 0, hc: 0 };
    this.knee = new Float32Array(32);
    this.lvl = 0; this.vizN = 0;
    this.port.onmessage = e => { Object.assign(this.p, e.data); this.resolve(); };
    this.resolve();
  }

  resolve() {
    const p = this.p, fs = this.fs;
    const t = clamp(p.type | 0, 0, 7), c = clamp(p.character | 0, 0, 7);
    const ts = TSPEC[t], cs = Object.assign({}, CSPEC[t][c]);
    this.ts = ts; this.cs = cs;

    // Push → threshold in dBp. push 0 = +9 dBp (above the chord: zero GR). push 1 = −39 dBp.
    this.T = 9 - 48 * Math.pow(clamp(p.push, 0, 1), 0.9) + (cs.thr || 0);

    // Ratio → SLOPE. s = t^0.85 reaches 1.0 (= ∞:1) at knob 1.0, exactly.
    const rk = clamp(p.ratio, 0, 1);
    let s = ts.sMax > 1.5
      ? (rk <= 0.85 ? Math.pow(rk / 0.85, 0.85) : 1 + (rk - 0.85) / 0.15)
      : Math.pow(rk, 0.85);
    s *= cs.sMul !== undefined ? cs.sMul : 1;
    if (ts.force > 0) s = ts.force + (1 - ts.force) * Math.pow(rk, 0.85);
    if (cs.sFloor) s = Math.max(s, cs.sFloor);
    this.s = clamp(s, 0, cs.sCap || ts.sCap);

    this.W = clamp(ts.kn[0] + (ts.kn[1] - ts.kn[0]) * clamp(p.b3, 0, 1) + (cs.kn || 0), 0, 36);

    const aW = cs.aW || [1, 1], rW = cs.rW || [1, 1];
    this.atkMs = Math.max(expMap(p.b1, ts.a[0] * aW[0], ts.a[1] * aW[1]) * (cs.aMul || 1), 1000 / fs);
    this.relMs = Math.max(expMap(p.b2, ts.r[0] * rW[0], ts.r[1] * rW[1]) * (cs.rMul || 1), 1000 / fs);
    this.aA = coefTau(this.atkMs / 1000, fs);
    this.aR = coefTau(this.relMs / 1000, fs);
    this.aAf = coefTau((this.atkMs / 1000) * 0.2, fs);
    this.aRf = coefTau(0.15, fs); this.aRs = coefTau(2.5, fs); this.aSlowA = coefTau(0.15, fs);
    this.aOptoA = coefTau(this.atkMs / 1000, fs);
    this.aOptoF = coefTau(this.relMs / 1000, fs);
    this.w2 = 6.2831853 / Math.max(0.001, this.relMs / 1000) / fs;
    this.zeta = cs.zeta !== undefined ? cs.zeta : 0.7;
    this.memA = coefTau(10, fs); this.mkA = coefTau(0.3, fs); this.lvlA = coefTau(0.03, fs);

    const axisDet = ['auto', 'peak', 'rms10', 'rms50', 'spike'][clamp(p.axis | 0, 0, 4)];
    this.det = axisDet === 'auto' ? (cs.det || ts.det) : axisDet;
    let win = 10;
    if (this.det === 'rms5') win = 5; else if (this.det === 'rms50') win = 50;
    else if (this.det === 'rmsw') win = expMap(p.b1, 1, 80) * (cs.rmsW || 1);
    this.aRms = coefTau(win / 1000, fs);
    this.holdLen = (fs * 0.005) | 0;
    this.aFb = coefTau(0.0001, fs);
    this.isFb = ts.fb === 1;

    let hcHz = p.b4 <= 0.002 ? 0 : expMap(p.b4, 20, 500);
    if (cs.hcMin) hcHz = Math.max(hcHz, cs.hcMin);
    this.hcA = hcHz <= 0 ? 0 : 1 - Math.exp((-6.2831853 * hcHz) / fs);
    this.tilt = cs.tilt || 0;
    this.tiltA = 1 - Math.exp((-6.2831853 * 2000) / fs);

    this.edge = (clamp(p.b5, 0, 1) - 0.5) * 2;
    this.latchN = (fs * clamp(p.b6, 0, 1) * 0.25) | 0;
    this.tie = cs.link !== undefined ? cs.link : clamp(p.b7, 0, 1);
    this.heat = clamp(Math.max(clamp(p.b8, 0, 1), cs.heatF || 0), 0, 1);
    this.heatKind = ts.heat;
    this.asym = ts.heat === 'asym' || !!cs.asym;
    this.liftDb = clamp(p.lift, 0, 1) * 24;
    this.mixT = clamp(p.mix, 0, 1);

    this.upOn = ts.up === 1 && !cs.dnOnly;
    this.dnOn = ts.up !== 1 || !cs.upOnly;
    this.tUp = this.T - 6 + (cs.upT || 0);
    this.sUp = Math.pow(rk, 0.85) * 0.95 * (cs.upS || 1);
    this.upCap = cs.upCap || 24;

    this.mk = (p.autoMakeup || cs.autoFull)
      ? Math.min(24, (cs.autoFull ? 1 : 0.7) * grDown(6, this.T, this.s, this.W)) : 0;

    this.varMuK = (cs.grK || 0) + ts.grK;
    this.relShape = cs.rel || ts.rel;
    this.deepRel = !!cs.deepRel; this.kneeAuto = !!cs.kneeAuto; this.plateau = !!cs.plateau;
    this.twoPass = !!cs.twoPass; this.msDet = !!cs.msDet; this.leaky = !!cs.leaky;
    this.lineAtk = !!cs.lineAtk; this.optoMem = cs.mem || 1;
    this.optoMixF = cs.evenPool ? 0.7 : 0.55;
    this.clipOn = ts.clip === 1 || !!cs.clipOn;
    this.clipLin = Math.max(1e-9, db2lin(this.T + this.liftDb + this.mk + 1 + BUS_DB) / 1.4);

    for (let i = 0; i < 32; ++i) {
      const x = -60 + (72 * i) / 31;
      let o = x - (this.dnOn ? grDown(x, this.T, this.s, this.W) : 0);
      if (this.upOn) o += liftUp(x, this.tUp, this.sUp, this.upCap) * floorGate(x, -55, 12);
      this.knee[i] = o + this.liftDb + this.mk;
    }
    if (this.gl.T === 9 && this.gl.s === 0.5) {
      this.gl = { T: this.T, s: this.s, W: this.W, tie: this.tie, heat: this.heat,
                  mix: this.mixT, lift: this.liftDb, mk: this.mk, hc: this.hcA };
    }
  }

  colour(y, k, grDb, st) {
    if (k <= 1e-5) return y;
    // the makeup is INSIDE: `comp` restores 70 % of the current reduction so the gain element
    // distorts MORE as it works harder (as hardware does), and the stage's slope at zero is
    // exactly 1, so Heat can never move the overall gain — only the curvature.
    const comp = db2lin(grDb * 0.7), inv = BUS_NOM / comp, u = (y * comp) / BUS_NOM;
    let sv;
    switch (this.heatKind) {
      case 'fet':  sv = fastTanh(u * 4) * 0.25; break;
      case 'asym': sv = fastTanh(u * 2.5 + 0.9 * u * Math.abs(u)) * 0.4; break;
      case 'deci': sv = Math.sign(u) * Math.log1p(Math.abs(u) * 3) / 3; break;
      default:     sv = fastTanh(u * 1.2) / 1.2; break;
    }
    let v = y + k * (sv * inv - y);
    if (this.asym) { const o = v - st.dcx + 0.995 * st.dcy; st.dcx = v; st.dcy = o; v = o; }
    return v;
  }

  process(inputs, outputs) {
    const inp = inputs[0], out = outputs[0];
    if (!out || out.length === 0) return true;
    const n = out[0].length;
    const iL = inp && inp[0] ? inp[0] : new Float32Array(n);
    const iR = inp && inp[1] ? inp[1] : iL;
    const oL = out[0], oR = out[1] || out[0];
    const gA = coefTau(0.02, this.fs), gl = this.gl, S = this.st;
    const g = [1, 1], grOut = [0, 0];

    for (let i = 0; i < n; ++i) {
      const dryL = iL[i], dryR = iR[i];
      gl.T += (this.T - gl.T) * gA;  gl.s += (this.s - gl.s) * gA;
      gl.W += (this.W - gl.W) * gA;  gl.tie += (this.tie - gl.tie) * gA;
      gl.heat += (this.heat - gl.heat) * gA;  gl.lift += (this.liftDb - gl.lift) * gA;
      gl.hc += (this.hcA - gl.hc) * gA;  gl.mk += (this.mk - gl.mk) * this.mkA;
      gl.mix += (this.mixT - gl.mix) * coefTau(0.01, this.fs);

      // detector source: FEEDBACK topologies tap the output, one sample late, PRE-colour
      let dl = this.isFb ? S[0].fb : dryL, dr = this.isFb ? S[1].fb : dryR;
      if (this.msDet) { const m = 0.7071068 * (dl + dr), sd = 0.7071068 * (dl - dr); dl = m; dr = sd; }
      if (gl.hc > 0) {
        S[0].hc += (dl - S[0].hc) * gl.hc; dl -= S[0].hc;
        S[1].hc += (dr - S[1].hc) * gl.hc; dr -= S[1].hc;
      }
      if (this.tilt !== 0) {
        S[0].tilt += (dl - S[0].tilt) * this.tiltA; dl += this.tilt * S[0].tilt;
        S[1].tilt += (dr - S[1].tilt) * this.tiltA; dr += this.tilt * S[1].tilt;
      }
      let a0 = Math.abs(dl), a1 = Math.abs(dr);
      if (this.det === 'spike') {
        for (let c = 0; c < 2; ++c) {
          const st = S[c], a = c === 0 ? a0 : a1;
          if (a >= st.hold) { st.hold = a; st.holdN = this.holdLen; }
          else if (--st.holdN <= 0) { st.hold = a; st.holdN = 0; }
        }
        a0 = S[0].hold; a1 = S[1].hold;
      } else if (this.det !== 'peak') {
        S[0].ms += (a0 * a0 - S[0].ms) * this.aRms; a0 = Math.sqrt(Math.max(0, S[0].ms));
        S[1].ms += (a1 * a1 - S[1].ms) * this.aRms; a1 = Math.sqrt(Math.max(0, S[1].ms));
      }
      // the mandatory 0.1 ms one-pole on a FEEDBACK tap (on the RECTIFIED value, not the signal)
      if (this.isFb) { a0 = S[0].fbs += (a0 - S[0].fbs) * this.aFb; a1 = S[1].fbs += (a1 - S[1].fbs) * this.aFb; }

      const mx = Math.max(a0, a1);
      const d0 = gl.tie * mx + (1 - gl.tie) * a0, d1 = gl.tie * mx + (1 - gl.tie) * a1;

      for (let c = 0; c < 2; ++c) {
        const st = S[c];
        let xG = lin2db((c === 0 ? d0 : d1) * DET_LIFT);   // → dBp
        let pTr = 0;
        if (this.edge !== 0) {
          const m = Math.abs(c === 0 ? dryL : dryR);
          st.pf += (m - st.pf) * (m > st.pf ? 0.0064 : 0.00026);
          st.ps += (m - st.ps) * 0.000065;
          pTr = clamp((st.pf - st.ps) / BUS_NOM, 0, 1);
          if (this.edge > 0) xG -= 24 * pTr * this.edge;
        }
        let W = gl.W;
        if (this.kneeAuto && st.gr < 12) W += 16 * (1 - st.gr / 12);
        let s = gl.s;
        if (this.varMuK > 0) s = Math.min(1, s * (1 + st.gr * this.varMuK));
        if (this.plateau) s = clamp(0.917 + 0.033 * Math.sin(st.gr * 0.9) + st.gr * 0.004, 0, 0.95);
        let grT = this.dnOn ? grDown(xG, gl.T, s, W) : 0;
        if (this.leaky && grT > 0) { const ov = xG - gl.T; if (ov > 0) grT = Math.min(grT, ov * 0.833); }
        if (this.twoPass) grT = 2 * grDown(xG - 0.5 * grT, gl.T, s * 0.5, W);
        if (this.edge < 0) grT += 24 * pTr * -this.edge;
        grT = clamp(grT, 0, 60);

        let aA = this.aA, aR = this.aR;
        if (this.relShape === 'dual') {
          st.grF += (grT - st.grF) * (grT > st.grF ? aA : this.aRf);
          st.grS += (grT - st.grS) * (grT > st.grS ? this.aSlowA : this.aRs);
          st.gr = Math.max(st.grF, st.grS);
        } else if (this.relShape === 'opto') {
          const tauS = (0.5 + 4.5 * Math.min(1, st.mem / 6)) * this.optoMem;
          const aS = coefTau(tauS, this.fs);
          st.grF += (grT - st.grF) * (grT > st.grF ? this.aOptoA : this.aOptoF);
          st.grS += (grT - st.grS) * (grT > st.grS ? this.aOptoA : aS);
          st.gr = this.optoMixF * st.grF + (1 - this.optoMixF) * st.grS;
          st.mem += (st.gr - st.mem) * this.memA;
        } else if (this.relShape === 'damped') {
          const a = Math.min(0.4, this.w2);
          st.v2 += a * (grT - st.y2) - 2 * this.zeta * a * st.v2;
          st.y2 += a * st.v2;
          st.gr = clamp(st.y2, 0, 60);
        } else {
          if (this.relShape === 'adapt' && grT > st.gr) {
            const u = Math.min(1, (grT - st.gr) / 12);
            aA = this.aA + (this.aAf - this.aA) * u;         // the diode: big overs attack FAST
          }
          if (this.deepRel && st.gr > 0) aR = this.aR / (1 + st.gr / 12);
          if (this.lineAtk && grT > st.gr) st.gr = Math.min(grT, st.gr + (1000 / (this.atkMs * this.fs)) * 12);
          else st.gr += (grT - st.gr) * (grT > st.gr ? aA : aR);
        }
        if (this.latchN > 0) {
          if (grT >= st.latchH - 1e-6) { st.latchH = grT; st.latch = this.latchN; }
          else if (st.latch > 0) { --st.latch; if (st.gr < st.latchH) st.gr = st.latchH; }
          else st.latchH = grT;
        }
        st.gr = clamp(st.gr, 0, 60);
        let up = 0;
        if (this.upOn) up = liftUp(xG, this.tUp, this.sUp, this.upCap) * floorGate(xG, -55, 12);
        grOut[c] = st.gr - up;
        g[c] = db2lin(-st.gr + up + gl.lift + gl.mk);
      }

      let yl = dryL * g[0], yr = dryR * g[1];
      if (this.isFb) { S[0].fb = yl; S[1].fb = yr; }
      if (gl.heat > 1e-4) {
        yl = this.colour(yl, gl.heat * Math.min(1, S[0].gr / 12), S[0].gr, S[0]);
        yr = this.colour(yr, gl.heat * Math.min(1, S[1].gr / 12), S[1].gr, S[1]);
      }
      if (this.clipOn) {
        const ic = 1 / this.clipLin;
        yl = this.clipLin * softClip(yl * ic); yr = this.clipLin * softClip(yr * ic);
      }
      // Mix 100 % = fully wet, zero dry — an exactly linear crossfade against the UNTOUCHED dry
      oL[i] = dryL + (yl - dryL) * gl.mix;
      oR[i] = dryR + (yr - dryR) * gl.mix;

      const pk = Math.max(Math.abs(oL[i]), Math.abs(oR[i]));
      this.lvl += (pk - this.lvl) * this.lvlA;
      if (++this.vizN >= (this.fs / 60) | 0) {
        this.vizN = 0;
        this.port.postMessage({
          grDb: 0.5 * (grOut[0] + grOut[1]),
          inDb: lin2db(Math.max(Math.abs(dryL), Math.abs(dryR)) * DET_LIFT),
          outDb: lin2db(pk * DET_LIFT),
          knee: Array.from(this.knee),
          lvl: clamp((this.lvl / BUS_NOM) * 0.5, 0, 1),
          typeName: TYPES[this.p.type | 0], charName: CHARS[this.p.type | 0][this.p.character | 0],
          detectName: DETECT[this.p.axis | 0],
          ratio: this.s >= 0.99995 ? Infinity : 1 / (1 - this.s),
          attackMs: this.atkMs, releaseMs: this.relMs,
        });
      }
    }
    return true;
  }
}

registerProcessor('terrain-compress', TerrainCompress);
