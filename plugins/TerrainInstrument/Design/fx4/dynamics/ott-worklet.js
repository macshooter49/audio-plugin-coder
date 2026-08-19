// ─────────────────────────────────────────────────────────────────────────────
// ott-worklet.js — the OTT device (chain kind 12) as an AudioWorkletProcessor, so Max can HEAR
// three-band up+down compression in a Safari mockup before any integration (the fb296 law).
//
// Same Types, same Characters, same knob names, same laws as TerrainOttFx.h — including the two
// that are easy to get wrong and impossible to hear until you do:
//   · THE MIX LAW. The dry path goes through the SAME AP2(f_lo)→AP2(f_hi) cascade the band tree
//     imposes, so wet and dry differ by gain alone and Mix never combs. LP4 + HP4 = AP2 exactly.
//   · THE FLOOR GATE. Below −78 dBFS the upward gain smoothstep-ramps back to unity over 12 dB,
//     so the effect DIES WITH THE NOTE instead of resurrecting the noise floor for ever.
//
// Usage:
//   await ctx.audioWorklet.addModule('ott-worklet.js');
//   const n = new AudioWorkletNode(ctx, 'terrain-ott', { numberOfInputs: 1,
//                                                        outputChannelCount: [2] });
//   n.port.postMessage({ type: 0, character: 0, axis: 0, amount: 0.5, speed: 0.5,
//                        topLift: 0.25, mix: 1, b1: 0.4689, b2: 0.4406, b3: 0.667,
//                        b4: 0.667, b5: 0.5, b6: 0.5, b7: 0.5, b8: 0.5, crest: false });
//   n.port.onmessage = e => drawJaws(e.data);   // { grDb[3] SIGNED, xoverHz[2], bandDb[3], lvl }
// ─────────────────────────────────────────────────────────────────────────────

// ═════ LABELS — MIRRORED FROM TerrainOttFx.h, WHICH IS THE SOURCE OF TRUTH ═════
const DEVICE = 'OTT';
const FRONT  = ['Amount', 'Chase', 'Top Lift', 'Mix'];
const BACK   = ['Low Cross', 'High Cross', 'Raise', 'Press', 'Grip', 'Bass', 'Mids', 'Treble'];
const DROPS  = ['Character', 'Stereo'];
const PILL   = 'Crest';
const TYPES = ['Over Top', 'Gentle', 'Heavy', 'Sheen', 'Bass Safe', 'Surge', 'Two Band', 'Stagger'];
const STEREO = ['Linked', 'Free Pair', 'Mid-Side'];
const CHARS = [
  ['Straight Up', 'Sharp Ears', 'Long Ears', 'Wide Corner', 'One Detector', 'Slow Low', 'Twice Deep', 'Full Crest'],
  ['Round Corner', 'Slow Hands', 'Long Window', 'Half Slopes', 'Long Tail', 'Soft Top', 'Even Bands', 'Barely There'],
  ['Welded Shut', 'Band Clip', 'No Clip', 'Deeper Jaws', 'Fast Grind', 'Peak Grab', 'Wall Ears', 'Total Squeeze'],
  ['Top Sheet', 'Higher Split', 'Lower Split', 'Glass Ceiling', 'Slow Shimmer', 'Fast Shimmer', 'Dark Source', 'Sheen Wall'],
  ['Anchor Low', 'Mono Low', 'Slower Low', 'Low Ceiling', 'Reese Guard', 'Free Low', 'Wide Corner Low', 'Tight Low'],
  ['Tail Riser', 'Deep Riser', 'Tied Rise', 'Fast Riser', 'Capped Riser', 'Top Riser', 'Mean Ears', 'Riser Wall'],
  ['Body Sparkle', 'Low Split', 'High Split', 'Hard Body', 'Soft Body', 'Sparkle Wall', 'Slow Pair', 'Fast Pair'],
  ['Time Spread', 'Wider Spread', 'Narrow Spread', 'Reverse Spread', 'Slow Anchor', 'Fast Top', 'Deep Spread', 'Spread Wall'],
];

const BUS_NOM = 0.05, FLOOR_DB = -78, FLOOR_RAMP = 12, MAX_MULT = 400;

// Thresholds re-derived against a MEASURED band envelope on the Terrain bus, not ported: a
// straight −20 dB shift of Vital's defaults gives 19/26/29 dB of GR here and lands 10.7 dB
// short of unity. See FINDINGS §2.
const TSPEC = [
  { xlo: 1, xhi: 1,    n: 3, tdn: [-40, -31, -40], tup: [-45, -37, -46], sdn: [0.90, 0.857, 1.0], sup: [0.8, 0.8, 0.8],  mk: [12, 14, 13], atk: [2.8, 1.4, 0.7],  rel: [40, 28, 15],  knee: 2,  det: 0, clip: 999, lowUpOff: 0, lowMono: 0 },
  { xlo: 1, xhi: 1,    n: 3, tdn: [-46, -37, -46], tup: [-51, -43, -52], sdn: [0.63, 0.60, 0.70], sup: [0.56, 0.56, 0.56], mk: [10, 10, 8], atk: [11.2, 5.6, 2.8], rel: [160, 112, 60], knee: 12, det: 1, clip: 999, lowUpOff: 0, lowMono: 0 },
  { xlo: 1, xhi: 1,    n: 3, tdn: [-46, -37, -46], tup: [-51, -43, -52], sdn: [1.0, 1.0, 1.0],   sup: [0.9, 0.9, 0.9],   mk: [21, 24, 20], atk: [2.0, 1.0, 0.5],  rel: [30, 20, 11],  knee: 0,  det: 0, clip: 6,   lowUpOff: 0, lowMono: 0 },
  { xlo: 1, xhi: 0.55, n: 3, tdn: [-40, -31, -26], tup: [-45, -37, -28], sdn: [0.90, 0.907, 1.0], sup: [0.8, 0.8, 0.9],  mk: [12, 14, 2],  atk: [2.8, 1.4, 0.35], rel: [40, 28, 8],   knee: 2,  det: 0, clip: 999, lowUpOff: 0, lowMono: 0 },
  { xlo: 1, xhi: 1,    n: 3, tdn: [-36, -31, -40], tup: [-45, -37, -46], sdn: [0.75, 0.857, 1.0], sup: [0.0, 0.8, 0.8],  mk: [7, 14, 13],  atk: [10, 1.4, 0.7],   rel: [120, 28, 15], knee: 2,  det: 0, clip: 999, lowUpOff: 1, lowMono: 1 },
  { xlo: 1, xhi: 1,    n: 3, tdn: [-40, -31, -40], tup: [-44, -39, -44], sdn: [0, 0, 0],         sup: [0.85, 0.85, 0.85], mk: [0, 0, 0],  atk: [5.6, 2.8, 1.4],  rel: [80, 56, 30],  knee: 2,  det: 0, clip: 999, lowUpOff: 0, lowMono: 0 },
  { xlo: 1, xhi: 1,    n: 2, tdn: [-32, -34, 0],   tup: [-38, -40, 0],   sdn: [0.85, 1.0, 0],    sup: [0.8, 0.85, 0],    mk: [13, 14, 0],  atk: [2.0, 0.8, 1.0],  rel: [34, 18, 1],   knee: 2,  det: 0, clip: 999, lowUpOff: 0, lowMono: 0 },
  { xlo: 1, xhi: 1,    n: 3, tdn: [-40, -31, -40], tup: [-45, -37, -46], sdn: [0.90, 0.857, 1.0], sup: [0.8, 0.8, 0.8],  mk: [12, 14, 8],  atk: [25, 1.4, 0.15],  rel: [400, 28, 4],  knee: 2,  det: 0, clip: 999, lowUpOff: 0, lowMono: 0 },
];

// Character rows carry only what differs. det 3 = instant-attack PEAK ears.
const CSPEC = [
  [{}, { det: 3 }, { det: 2 }, { knee: 24 }, { bandLink: 1 }, { spread: 2.2 }, { tdnOff: -6, tupOff: 6 },
   { aMul: 0.35, rMul: 0.6, tupOff: 4, upCap: 30, upHold: 1 }],
  [{}, { rMul: 4.5 }, { det: 2 }, { dnMul: 0.5, upMul: 0.5 }, { deepRel: 1 }, { hiTilt: -9 },
   { spread: 0 }, { dnMul: 0.35, upMul: 0.35, tdnOff: 6, tupOff: -6 }],
  [{}, { clip: 3 }, { clip: 999 }, { tdnOff: -8, tupOff: 8, upCap: 30, clip: 6 },
   { aMul: 0.25, rMul: 0.25 }, { det: 3 }, { bandLink: 1 },
   { dnMul: 1.2, upMul: 1.2, tdnOff: -4, tupOff: 4, upCap: 30, clip: 2 }],
  [{}, { xhiMul: 2.6 }, { xhiMul: 0.6 }, { tupOff: -10, knee: 0, upCap: 10, clip: 4 },
   { hiTimeMul: 4 }, { hiTimeMul: 0.25 }, { hiTilt: 6, upCap: 30 }, { upMul: 1.25, tupOff: 8, upCap: 30 }],
  [{ lowMono: 1 }, { lowMono: 1, spread: 1.9 }, { lowMono: 1, rMul: 2 },
   { lowMono: 1, tdnOff: -4, knee: 0 }, { lowMono: 1, dnMul: 1.25, xloMul: 1.7 },
   { lowMono: -1, lowUp: 1 }, { lowMono: 1, knee: 26 }, { lowMono: 1, aMul: 0.4, rMul: 0.4, xloMul: 0.6 }],
  [{}, { upMul: 1.15, tupOff: 6, upCap: 36 }, { bandLink: 1 }, { aMul: 0.25, rMul: 0.25 },
   { upCap: 12 }, { hiTimeMul: 0.3, upMul: 1.2, tupOff: 4, upCap: 30, xhiMul: 0.7 },
   { det: 2 }, { upMul: 1.12, tupOff: 10, upCap: 36 }],
  [{}, { xloMul: 0.45 }, { xloMul: 2.2 }, { dnMul: 1.2, knee: 0 }, { dnMul: 0.6, knee: 14 },
   { upMul: 1.2, tupOff: 6, knee: 0, upCap: 30, clip: 4 }, { aMul: 2.5, rMul: 2.5 },
   { aMul: 0.2, rMul: 0.2 }],
  [{}, { spread: 2.1 }, { spread: 0.5 }, { spread: -1 }, { xloMul: 1.8, lowMono: 1 },
   { hiTimeMul: 0.25 }, { tdnOff: -6, tupOff: 6, upCap: 30 },
   { spread: 1.3, upMul: 1.1, tdnOff: -3, tupOff: 5, knee: 0, upCap: 30, clip: 5 }],
];

const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const db2lin = d => Math.pow(10, d / 20);
const ms2db = e => 10 * Math.log10(Math.max(e, 1e-20));
const expMap = (t, lo, hi) => lo * Math.pow(hi / lo, clamp(t, 0, 1));
const coefTau = (tau, fs) => (tau <= 1e-7 ? 1 : 1 - Math.exp(-1 / (fs * tau)));

function grDown(xdb, T, s, W) {
  const d = xdb - T;
  if (W > 1e-4) {
    if (2 * d < -W) return 0;
    if (2 * d <= W) { const u = d + 0.5 * W; return (s * u * u) / (2 * W); }
  } else if (d <= 0) return 0;
  return s * d;
}
function liftUp(xdb, Tup, sUp, cap) { const u = Tup - xdb; return u <= 0 ? 0 : Math.min(sUp * u, cap); }
function floorGate(xdb) {
  const t = clamp((xdb - FLOOR_DB) / FLOOR_RAMP, 0, 1);
  return t * t * (3 - 2 * t);
}

/** Simper TPT SVF, one tick, every tap. LP4 + HP4 = AP2 exactly, which is what makes the Mix
 *  law provable rather than hoped for. */
class Svf {
  constructor() { this.ic1 = 0; this.ic2 = 0; this.g = 0; this.k = Math.SQRT2; this.a1 = 1; this.a2 = 0; this.a3 = 0; }
  set(fc, fs) {
    const f = clamp(fc, 5, 0.245 * fs);
    this.g = Math.tan((Math.PI * f) / fs); this.k = Math.SQRT2;
    const den = 1 + this.g * (this.g + this.k);
    this.a1 = 1 / den; this.a2 = this.g * this.a1; this.a3 = this.g * this.a2;
  }
  tick(v0) {
    const v3 = v0 - this.ic2;
    const v1 = this.a1 * this.ic1 + this.a2 * v3;
    const v2 = this.ic2 + this.a2 * this.ic1 + this.a3 * v3;
    this.ic1 = 2 * v1 - this.ic1; this.ic2 = 2 * v2 - this.ic2;
    return [v2, v0 - this.k * v1 - v2, v1];        // lp, hp, raw bp
  }
  ap(v0) { const t = this.tick(v0); return v0 - 2 * this.k * t[2]; }
}
class LR4 {
  constructor() { this.s1 = new Svf(); this.a = new Svf(); this.b = new Svf(); }
  set(fc, fs) { this.s1.set(fc, fs); this.a.set(fc, fs); this.b.set(fc, fs); }
  split(x) { const t = this.s1.tick(x); return [this.a.tick(t[0])[0], this.b.tick(t[1])[1]]; }
}

class TerrainOtt extends AudioWorkletProcessor {
  constructor() {
    super();
    this.fs = sampleRate;
    this.p = { type: 0, character: 0, axis: 0, amount: 0.5, speed: 0.5, topLift: 0.25, mix: 1,
               b1: 0.4689, b2: 0.4406, b3: 0.667, b4: 0.667, b5: 0.5, b6: 0.5, b7: 0.5, b8: 0.5,
               crest: false };
    this.splitLo = [new LR4(), new LR4()]; this.splitHi = [new LR4(), new LR4()];
    this.alignLow = [new Svf(), new Svf()];
    this.dryLo = [new Svf(), new Svf()]; this.dryHi = [new Svf(), new Svf()];
    this.envDn = [[0, 0, 0], [0, 0, 0]]; this.envUp = [[0, 0, 0], [0, 0, 0]];
    this.pre = [[0, 0, 0], [0, 0, 0]]; this.gDnPrev = [[0, 0, 0], [0, 0, 0]];
    this.bf = [0, 0]; this.bs = [0, 0]; this.crestG = [1, 1]; this.crestN = [0, 0];
    this.tdn = [0, 0, 0]; this.tup = [0, 0, 0]; this.sdn = [0, 0, 0]; this.sup = [0, 0, 0]; this.mkDb = [0, 0, 0];
    this.gl = null; this.dip = 1; this.lvl = 0; this.vizN = 0; this.prevBands = 3;
    this.grAcc = [0, 0, 0]; this.lvAcc = [0, 0, 0]; this.accN = 0;
    this.port.onmessage = e => { Object.assign(this.p, e.data); this.resolve(); };
    this.resolve();
    this.gl = { tdn: this.tdnT.slice(), tup: this.tupT.slice(), sdn: this.sdnT.slice(),
                sup: this.supT.slice(), mk: this.mkT.slice(), mix: this.mixT, xlo: this.xloT, xhi: this.xhiT };
    this.applyX(this.xloT, this.xhiT);
  }

  resolve() {
    const p = this.p, fs = this.fs;
    const t = clamp(p.type | 0, 0, 7), c = clamp(p.character | 0, 0, 7);
    const ts = TSPEC[t], cs = CSPEC[t][c];
    this.n = ts.n; this.stereo = clamp(p.axis | 0, 0, 2);
    this.det = cs.det !== undefined ? cs.det : ts.det;
    this.bandLink = !!cs.bandLink;
    this.lowMono = cs.lowMono ? cs.lowMono > 0 : ts.lowMono === 1;
    this.lowUpOff = cs.lowUp ? cs.lowUp < 0 : ts.lowUpOff === 1;
    this.upHold = !!cs.upHold || !!p.crest;
    this.deepRel = !!cs.deepRel;
    this.knee = cs.knee !== undefined ? cs.knee : ts.knee;
    this.clipHd = cs.clip !== undefined ? cs.clip : ts.clip;

    let xlo = expMap(p.b1, this.n === 2 ? 150 : 30, this.n === 2 ? 2000 : 300) * ts.xlo * (cs.xloMul || 1);
    let xhi = expMap(p.b2, 1000, 8000) * ts.xhi * (cs.xhiMul || 1);
    const topA = clamp(p.topLift, 0, 1);
    xhi *= 1 - 0.25 * topA;
    xlo = clamp(xlo, 25, 0.2 * fs);
    xhi = clamp(xhi, 4 * xlo, 0.4 * fs);
    this.xloT = xlo; this.xhiT = xhi;

    const tMul = Math.pow(10, (0.5 - clamp(p.speed, 0, 1)) * 2.6);
    const amt = clamp(p.amount, 0, 1);
    const lo01 = amt <= 0.5 ? Math.pow(amt * 2, 1.2) : 1;
    const u = amt > 0.5 ? (amt - 0.5) * 2 : 0;
    const raise = clamp(p.b3, 0, 1) * 1.5, press = clamp(p.b4, 0, 1) * 1.5;
    const grip = (clamp(p.b5, 0, 1) - 0.5) * 36;
    const trim = [(clamp(p.b6, 0, 1) - 0.5) * 24, (clamp(p.b7, 0, 1) - 0.5) * 24, (clamp(p.b8, 0, 1) - 0.5) * 24];

    this.tdnT = [0, 0, 0]; this.tupT = [0, 0, 0]; this.sdnT = [0, 0, 0]; this.supT = [0, 0, 0];
    this.mkT = [0, 0, 0]; this.cap = [0, 0, 0]; this.aAtk = [0, 0, 0]; this.aRel = [0, 0, 0];
    this.atkMs = [0, 0, 0]; this.relMs = [0, 0, 0];
    for (let b = 0; b < 3; ++b) {
      let tdn = ts.tdn[b] + (cs.tdnOff || 0) - grip;
      let tup = ts.tup[b] + (cs.tupOff || 0) - grip;
      let sdn = ts.sdn[b] * (cs.dnMul || 1) * press * lo01;
      let sup = ts.sup[b] * (cs.upMul || 1) * raise * lo01;
      let mk = ts.mk[b] * lo01;                       // makeup follows the amount, or Amount 0
      if (b === this.n - 1) {                          // is a +13 dB gain stage and Mix combs
        tup += 12 * topA; sup *= 1 + 1.2 * topA; mk += 4 * topA;
      }
      if (u > 0) {
        sdn = sdn + (1.0 - sdn) * u;
        sup = sup + (0.95 - sup) * u;
        tdn -= 6 * u;
        tup = tup + (tdn - tup) * u;                   // the floor RISES to MEET the ceiling
        mk += 3 * u;
      }
      this.tdnT[b] = tdn; this.tupT[b] = Math.min(tup, tdn);
      this.sdnT[b] = clamp(sdn, 0, 1); this.supT[b] = clamp(sup, 0, 0.95);
      this.mkT[b] = mk + trim[b]; this.cap[b] = cs.upCap || 24;
      const sp = cs.spread !== undefined ? cs.spread : 1;
      let aMs = ts.atk[1] * Math.pow(ts.atk[b] / ts.atk[1], sp) * (cs.aMul || 1) * tMul;
      let rMs = ts.rel[1] * Math.pow(ts.rel[b] / ts.rel[1], sp) * (cs.rMul || 1) * tMul;
      if (b === this.n - 1) { aMs *= cs.hiTimeMul || 1; rMs *= cs.hiTimeMul || 1; }
      const nA = Math.max(5, (aMs / 1000) * fs), nR = Math.max(5, (rMs / 1000) * fs);
      this.aAtk[b] = 1 / (nA + 1); this.aRel[b] = 1 / (nR + 1);
      this.atkMs[b] = (nA * 1000) / fs; this.relMs[b] = (nR * 1000) / fs;
    }
    this.hiTilt = cs.hiTilt || 0;
    this.aPre = coefTau(this.det === 1 ? 0.025 : 0.06, fs);
    this.mixT = clamp(p.mix, 0, 1);
    this.sOff = this.stereo === 2 ? -6 : 0;
    this.aGl = coefTau(0.015, fs); this.dipUp = coefTau(0.03, fs); this.lvlA = coefTau(0.03, fs);
    this.bAtk = coefTau(0.003, fs); this.bRel = coefTau(0.12, fs);
    this.crestHold = (fs * 0.01) | 0; this.crestRel = coefTau(0.005, fs);
    if (this.gl && this.n !== this.prevBands) this.dip = 0;   // the tree changed: 4 ms dip
    this.prevBands = this.n;
  }

  applyX(xl, xh) {
    for (let c = 0; c < 2; ++c) {
      this.splitLo[c].set(xl, this.fs); this.dryLo[c].set(xl, this.fs);
      if (this.n === 3) { this.splitHi[c].set(xh, this.fs); this.alignLow[c].set(xh, this.fs); this.dryHi[c].set(xh, this.fs); }
    }
  }

  process(inputs, outputs) {
    const inp = inputs[0], out = outputs[0];
    if (!out || out.length === 0) return true;
    const n = out[0].length, NB = this.n, gl = this.gl;
    const iL = inp && inp[0] ? inp[0] : new Float32Array(n);
    const iR = inp && inp[1] ? inp[1] : iL;
    const oL = out[0], oR = out[1] || out[0];
    const xg = coefTau(0.03, this.fs);
    gl.xlo += (this.xloT - gl.xlo) * xg; gl.xhi += (this.xhiT - gl.xhi) * xg;
    this.applyX(gl.xlo, gl.xhi);
    const band = [[0, 0, 0], [0, 0, 0]], x2b = [[0, 0], [0, 0], [0, 0]];

    for (let i = 0; i < n; ++i) {
      const inL = iL[i], inR = iR[i];
      gl.mix += (this.mixT - gl.mix) * coefTau(0.01, this.fs);

      // the PHASE-MATCHED dry — the same two allpasses the band tree imposes
      let dL = this.dryLo[0].ap(inL), dR = this.dryLo[1].ap(inR);
      if (NB === 3) { dL = this.dryHi[0].ap(dL); dR = this.dryHi[1].ap(dR); }

      let c0 = inL, c1 = inR;
      if (this.stereo === 2) { const m = 0.7071068 * (inL + inR), sd = 0.7071068 * (inL - inR); c0 = m; c1 = sd; }
      for (let c = 0; c < 2; ++c) {
        const x = c === 0 ? c0 : c1;
        const [lo, rest] = this.splitLo[c].split(x);
        if (NB === 3) {
          const [mid, hi] = this.splitHi[c].split(rest);
          band[c][0] = this.alignLow[c].ap(lo);   // the low band eats the SAME AP2(f_hi)
          band[c][1] = mid; band[c][2] = hi;
        } else { band[c][0] = lo; band[c][1] = rest; band[c][2] = 0; }
      }

      if (this.upHold) {
        for (let c = 0; c < 2; ++c) {
          const m = Math.abs(c === 0 ? inL : inR);
          this.bf[c] += (m - this.bf[c]) * (m > this.bf[c] ? this.bAtk : this.bRel);
          this.bs[c] += (m - this.bs[c]) * this.bRel * 0.25;
          if (this.bf[c] > 4 * this.bs[c] + 1e-6) { this.crestN[c] = this.crestHold; this.crestG[c] = 0; }
          else if (this.crestN[c] > 0) --this.crestN[c];
          else this.crestG[c] += (1 - this.crestG[c]) * this.crestRel;
        }
      }

      for (let b = 0; b < NB; ++b) {
        gl.tdn[b] += (this.tdnT[b] - gl.tdn[b]) * this.aGl;
        gl.tup[b] += (this.tupT[b] - gl.tup[b]) * this.aGl;
        gl.sdn[b] += (this.sdnT[b] - gl.sdn[b]) * this.aGl;
        gl.sup[b] += (this.supT[b] - gl.sup[b]) * this.aGl;
        gl.mk[b]  += (this.mkT[b]  - gl.mk[b])  * this.aGl;
        for (let c = 0; c < 2; ++c) {
          let sg = band[c][b];
          if (b === NB - 1 && this.hiTilt !== 0) sg *= db2lin(this.hiTilt);
          x2b[b][c] = sg * sg;
        }
        if (b === 0 && this.lowMono) { const m = 0.5 * (x2b[b][0] + x2b[b][1]); x2b[b][0] = x2b[b][1] = m; }
        if (this.stereo === 0) { const m = Math.max(x2b[b][0], x2b[b][1]); x2b[b][0] = x2b[b][1] = m; }
      }
      if (this.bandLink) {
        for (let c = 0; c < 2; ++c) {
          let m = x2b[0][c];
          for (let b = 1; b < NB; ++b) m = Math.max(m, x2b[b][c]);
          for (let b = 0; b < NB; ++b) x2b[b][c] = m;
        }
      }

      let wL = 0, wR = 0;
      for (let b = 0; b < NB; ++b) {
        for (let c = 0; c < 2; ++c) {
          let e = x2b[b][c] + 1e-20;
          if (this.det === 1 || this.det === 2) { this.pre[c][b] += (e - this.pre[c][b]) * this.aPre; e = this.pre[c][b]; }
          const off = c === 1 ? this.sOff : 0;
          const Tdn = gl.tdn[b] + off, Tup = gl.tup[b] + off;
          const tdn2 = db2lin(2 * Tdn), tup2 = db2lin(2 * Tup);
          const aA = this.det === 3 ? 1 : this.aAtk[b];
          let aR = this.aRel[b];
          if (this.deepRel) aR *= 1 / (1 + (0.5 * Math.min(24, this.gDnPrev[c][b])) / 6);
          let eD = this.envDn[c][b], eU = this.envUp[c][b];
          eD += (e - eD) * (e > eD ? aA : aR); eU += (e - eU) * (e > eU ? aA : aR);
          this.envDn[c][b] = eD; this.envUp[c][b] = eU;
          // Vital's two CLAMPED envelopes: the up-env sits AT its threshold while the band is
          // loud, so the tail-bloom arrives one release after the note drops, not 8.
          const Ldn = ms2db(eD > tdn2 ? eD : tdn2);
          const Lup = ms2db(eU < tup2 ? eU : tup2);
          const gDn = grDown(Ldn, Tdn, gl.sdn[b], this.knee);
          this.gDnPrev[c][b] = gDn;
          let gUp = 0;
          if (!(b === 0 && this.lowUpOff)) {
            gUp = liftUp(Lup, Tup, gl.sup[b], this.cap[b]) * floorGate(Lup);
            if (this.upHold) gUp *= this.crestG[c];
          }
          let g = db2lin(-gDn + gUp + gl.mk[b]);
          if (g > MAX_MULT) g = MAX_MULT;
          let y = band[c][b] * g;
          if (this.clipHd < 900) {
            const lim = db2lin(Tdn + this.clipHd + gl.mk[b]);
            const tt = y / lim;
            y = tt > -1.5 && tt < 1.5 ? lim * (tt - (tt * tt * tt) / 6.75) : lim * Math.sign(tt);
          }
          if (c === 0) { wL += y; this.grAcc[b] += gDn - gUp; this.lvAcc[b] += ms2db(eD); }
          else wR += y;
        }
      }
      if (this.stereo === 2) { const l = 0.7071068 * (wL + wR), r = 0.7071068 * (wL - wR); wL = l; wR = r; }
      if (this.dip < 0.99999) { this.dip += (1 - this.dip) * this.dipUp; wL *= this.dip; wR *= this.dip; }

      oL[i] = dL + (wL - dL) * gl.mix;      // linear crossfade: wet and dry are CORRELATED here
      oR[i] = dR + (wR - dR) * gl.mix;

      const pk = Math.max(Math.abs(oL[i]), Math.abs(oR[i]));
      this.lvl += (pk - this.lvl) * this.lvlA;
      ++this.accN;
      if (++this.vizN >= (this.fs / 60) | 0) {
        const inv = 1 / Math.max(1, this.accN);
        const gr = [0, 0, 0], bd = [0, 0, 0];
        for (let b = 0; b < 3; ++b) {
          gr[b] = b < NB ? this.grAcc[b] * inv : 0;      // SIGNED: −ve = upward LIFT
          bd[b] = b < NB ? this.lvAcc[b] * inv : -120;
          this.grAcc[b] = 0; this.lvAcc[b] = 0;
        }
        this.accN = 0; this.vizN = 0;
        this.port.postMessage({
          grDb: gr, xoverHz: [gl.xlo, NB === 3 ? gl.xhi : 0], bandDb: bd,
          lvl: clamp((this.lvl / BUS_NOM) * 0.5, 0, 1), bands: NB,
          typeName: TYPES[this.p.type | 0], charName: CHARS[this.p.type | 0][this.p.character | 0],
          stereoName: STEREO[this.stereo],
          attackMs: this.atkMs.slice(), releaseMs: this.relMs.slice(),
        });
      }
    }
    return true;
  }
}

registerProcessor('terrain-ott', TerrainOtt);
