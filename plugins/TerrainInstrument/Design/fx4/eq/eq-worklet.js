// ═══════════════════════════════════════════════════════════════════════════════
//  eq-worklet.js — fb420. The FX-rack EQUALIZER (chain kind 9) as an
//  AudioWorkletProcessor, so Max can HEAR it in a Safari mockup before integration.
//
//  Same Type / Character / Focus names, same 12 params, same laws as
//  TerrainEqualizerFx.h. NOT sample-identical to the C++ (JS has no fade-swap dip and
//  the design runs every 64 samples instead of 32) but recognisably the same device:
//  the same matched-biquad design math, the same Q laws, the same +-30 dB x Amount 200 %
//  ceiling, the same Chisel notch morph and the same Dynamic ride.
//
//  USE:
//    await ctx.audioWorklet.addModule('eq-worklet.js');
//    const eq = new AudioWorkletNode(ctx, 'terrain-eq', { numberOfInputs: 1,
//                                                         outputChannelCount: [2] });
//    eq.port.postMessage({ type: 3, character: 0, axis: 0,
//                          f1: 0.5, f2: 0.5, f3: 0.5, mix: 1,
//                          b1: 0.5, b2: 0.5, b3: 0.5, b4: 0.5,
//                          b5: 0.5, b6: 0.5, b7: 0.5, b8: 0.5 });
//    eq.port.onmessage = e => drawCurve(e.data.curve);   // 96 log bins, 20 Hz - 20 kHz
//
//  EVERY DEFAULT IS 0.5 AND 0.5 IS NEUTRAL, so the device boots flat and transparent.
// ═══════════════════════════════════════════════════════════════════════════════

const TYPES = ['Surgical', 'British', 'American', 'Passive', 'Open', 'Dynamic', 'Chisel'];
const FOCUS = ['Stereo', 'Mid', 'Side', 'Left', 'Right'];
const TRAIT = ['Pinch', 'Slope', 'Taper', 'Dip', 'Silk', 'Pivot', 'Sting'];
const BACK  = ['Low Hz', 'Low', 'Body Hz', 'Body', 'Bite Hz', 'Bite', 'Reach', 'Trait'];
const FRONT = ['Slant', 'Air', 'Amount', 'Mix'];
// fb425 — the two words the card prints ABOVE the back dropdowns. They are published
// labels like any other, so they live in a table the drift gate can read, not in the mockup's
// markup where nothing checks them. Order matches the panel: dropdown 1, then dropdown 2.
const DROPDOWN = ['Character', 'Focus'];

const CHARS = [
  ['Plain','Tight','Broad','Steep','Scoop','Deep Pivot','Bright Pivot','Four Bells'],
  ['Desk','Big Knob','Ahead','Iron Top','Sub Iron','Steep Iron','Full Swing','Mid Rise'],
  ['Proportional','Lasers','Mellow','Floor Lift','Boost Only','Cut Only','Shelf Ride','Bolt'],
  ['Baseline','Close Cut','Far Cut','Both Ends','Bell Top','Slow Top','Deep Atten','Revival'],
  ['Gloss','Very Wide','Two Shelves','Twin Shelf','Deep Reach','Soft Knee','Hard Knee','Bell Air'],
  ['Program Ride','Quick','Lazy','Wideband','Upward','Hard Window','Soft Window','Peak Keep'],
  ['Resonator','Scalpel','Triple Notch','Gain Peak','Shallow','Handset','Sub Kill','Tin']
];

// LOW · BODY · BITE · AIR   —   kind: 0 bell · 1 low shelf · 2 high shelf
//                                      3 low shelf 1-pole · 4 high shelf 1-pole · 5 SLANT (fixed pivot)
const TSPEC = [
  { q: [0.90,1.00,1.00,0.90], k: [1,0,0,2], law: 0, soften: 0.00 },  // Surgical
  { q: [0.80,1.00,1.00,0.80], k: [1,0,0,2], law: 1, soften: 0.10 },  // British
  { q: [1.00,1.00,1.00,1.00], k: [1,0,0,2], law: 2, soften: 0.00 },  // American
  { q: [0.90,0.80,0.70,0.80], k: [1,0,0,2], law: 3, soften: 0.00 },  // Passive
  { q: [0.45,0.40,0.40,0.50], k: [1,0,0,4], law: 4, soften: 0.00 },  // Open  (AIR is 6 dB/oct)
  { q: [0.90,1.00,1.00,0.90], k: [1,0,0,2], law: 5, soften: 0.00 },  // Dynamic
  { q: [1.00,1.00,1.00,1.00], k: [1,0,0,2], law: 6, soften: 0.00 }   // Chisel
];

// qm, fm, gm per band · kd (-1 = type default, 5 = steep) · pivotHz · cutQ · cutG · e1,e2,e3
// 🔑 fb423 — NO `nm` FIELD. This factory used to carry a name, which made CSPEC a SECOND
// name table beside CHARS above — and it had already drifted 12 names behind it while
// being dead code (only CHARS is ever published, at pushCurve()). That is the same
// two-table geometry the C++ header deleted at fb422 (`Fixed Top` vs `Iron Top`).
// A table that cannot exist cannot drift: the name lives in CHARS, the physics lives here.
const C = (qm, fm, gm, kd, pivot, cutQ, cutG, e1, e2, e3) =>
  ({ qm, fm, gm, kd, pivot, cutQ, cutG, e1, e2, e3 });
const O = [1,1,1,1], N4 = [-1,-1,-1,-1];
const CSPEC = [
 [ C(O,O,O, N4, 700, 1,1, 0,0,0),
   C([2.6,2.6,2.6,2.6],O,O, N4, 700, 1,1, 0,0,0),
   C([0.35,0.35,0.35,0.35],O,O, N4, 700, 1,1, 0,0,0),
   C(O,O,O, [5,-1,-1,5], 700, 1,1, 0,0,0),
   C(O,O,O, N4, 700, 3.5,1, 0,0,0),
   C(O,O,O, N4, 150, 1,1, 0,0,0),
   C(O,O,O, N4, 3000, 1,1, 0,0,0),
   C(O,O,O, [0,-1,-1,0], 700, 1,1, 0,0,0) ],
 [ C(O,O,O, N4, 700, 1,1, 1,1,0),
   C([1,0.5,0.5,1],O,O, N4, 700, 1,1, 1,1,0),
   C([1,2.2,2.4,1],[1,1.5,1.2,1],O, N4, 700, 1,1, 1,1,0),
   C([1,1,1,1.9],[1,1,1,0.78],[1,1,1,1.1], N4, 700, 1,1, 1.6,1,0),
   C([1.6,1,1,1],[0.42,1,1,1],O, N4, 700, 1,1, 1.3,1,0),
   C(O,O,O, [5,-1,-1,5], 700, 1,1, 1,1,0),
   C(O,O,O, N4, 700, 1,1, 0.45,0,0),
   C([1,0.65,1,1],[1,1.9,1,1],[1,1.25,1,1], N4, 700, 1,1, 1,1,0) ],
 [ C(O,O,O, N4, 700, 1,1, 0,0,0),
   C([1.6,1.6,1.6,1.6],O,O, N4, 700, 1,1, 1.2,0,0),
   C([0.7,0.7,0.7,0.7],O,O, N4, 700, 1,1, -0.55,0,0),
   C([2.2,2.2,2.2,2.2],O,O, N4, 700, 1,1, -0.30,0,0),
   C(O,O,O, N4, 700, 1,1, 0,1,0),
   C(O,O,O, N4, 700, 1,1, 0,2,0),
   C([1.4,1,1,1.4],O,O, N4, 700, 1,1, 0,0,1),
   C([2.2,2.2,2.2,2.2],O,O, N4, 700, 1,1, 2.0,0,0) ],
 [ C(O,O,O, N4, 700, 1,1, 2.2,0,0),
   C(O,O,O, N4, 700, 1,1, 1.4,0,0),
   C(O,O,O, N4, 700, 1,1, 4.4,0,0),
   C(O,O,O, N4, 700, 1,1, 2.2,1,0),
   C([1,1,1,0.70],O,O, [-1,-1,-1,0], 700, 1,1, 2.2,0,0),
   C(O,O,O, [-1,-1,-1,4], 700, 1,1, 2.2,0,0),
   C(O,O,O, N4, 700, 1,1.45, 2.2,0,0),
   C(O,O,O, N4, 700, 1,1, 2.2,2,0) ],
 [ C(O,O,O, N4, 700, 1,1, 1.0,22,0),
   C([0.55,0.55,0.55,0.55],O,O, N4, 700, 1,1, 1.0,22,0),
   C(O,O,O, [-1,1,-1,-1], 700, 1,1, 1.0,22,0),
   C(O,O,O, N4, 700, 1,1, -1.0,22,0),
   C([1,1,1,1.5],O,O, N4, 700, 1,1, -2.0,22,0),
   C(O,O,O, N4, 700, 1,1, 1.0,12,0),
   C(O,O,O, N4, 700, 1,1, 1.0,90,0),
   C([1,1,1,0.50],O,O, [-1,-1,-1,0], 700, 1,1, 1.0,22,0) ],
 [ C(O,O,O, N4, 700, 1,1, 1.0,0,14),
   C(O,O,O, N4, 700, 1,1, 0.22,0,14),
   C(O,O,O, N4, 700, 1,1, 5.0,0,14),
   C(O,O,O, N4, 700, 1,1, 1.0,2,14),
   C(O,O,O, N4, 700, 1,1, 1.0,1,14),
   C(O,O,O, N4, 700, 1,1, 1.0,0,2.5),
   C(O,O,O, N4, 700, 1,1, 1.0,0,34),
   C(O,O,O, N4, 700, 1,1, 1.0,3,14) ],
 [ C(O,O,O, N4, 700, 1,1, -18,1.0,0),
   C([2.5,2.5,2.5,2.5],O,O, N4, 700, 1,1, -12,1.0,0),
   C(O,[1,2.0,4.0,1],O, N4, 700, 1,1, -14,1.0,0),
   C(O,O,O, N4, 700, 1,1, -18,2.6,0),
   C(O,O,O, N4, 700, 1,1, -90,1.0,0),
   C(O,[2.5,1,1,0.35],O, [0,-1,-1,0], 700, 1,1, -18,1.0,0),
   C([3,1,1,1],[0.35,1,1,1],O, N4, 700, 1,1, -10,1.0,0),
   C([4,4,4,4],O,O, N4, 700, 1,1, -18,1.8,0) ]
];

const BAND_LO = [20, 100, 700, 6000], BAND_HI = [500, 3000, 14000, 40000];
const DB_SPAN = 30, SLANT_SPAN = 24, AMOUNT_MAX = 2;
const Q_MIN = 0.05, Q_MAX = 90, G_CEIL = 72, G_FLOOR = -90;
const DESIGN_BLK = 64, DET_CAL = 14, MAX_RING_SEC = 3;
const CURVE_BINS = 96;

const clamp = (v, a, b) => v < a ? a : (v > b ? b : v);
const bandHz = (b, t) => BAND_LO[b] * Math.pow(BAND_HI[b] / BAND_LO[b], clamp(t, 0, 1));
const curveBinHz = i => 20 * Math.pow(10, 3 * i / (CURVE_BINS - 1));

// ── the analog prototype every design below is trying to be ──────────────────
function protoMag2(kind, f, f0, Q, gDb) {
  if (Math.abs(gDb) < 1e-12) return 1;
  if (Q < 1e-4) Q = 1e-4;
  const A = Math.pow(10, gDb / 40), W = f / Math.max(1e-6, f0), W2 = W * W;
  if (kind === 0) {
    const t = (1 - W2) * (1 - W2);
    return (t + Math.pow(A * W / Q, 2)) / Math.max(1e-300, t + Math.pow(W / (A * Q), 2));
  }
  if (kind === 1) {
    const n = (A - W2) * (A - W2) + A * W2 / (Q * Q);
    const d = (1 - A * W2) * (1 - A * W2) + A * W2 / (Q * Q);
    return A * A * n / Math.max(1e-300, d);
  }
  if (kind === 2) {
    const n = (1 - A * W2) * (1 - A * W2) + A * W2 / (Q * Q);
    const d = (A - W2) * (A - W2) + A * W2 / (Q * Q);
    return A * A * n / Math.max(1e-300, d);
  }
  if (kind === 3) return A * A * (W2 + A * A) / Math.max(1e-300, W2 + 1 / (A * A));
  if (kind === 5) { const g = Math.pow(10, gDb / 20); return (W2 * g * g + 1 / (g * g)) / (W2 + 1); }
  return A * A * A * A * (W2 + 1 / (A * A)) / Math.max(1e-300, W2 + A * A);
}

// |H|^2 in the phi basis (phi = sin^2(w/2)). NOT the textbook cosine form: that one loses
// eight digits to cancellation at low frequency and makes the drawn curve lie by 60 dB.
function magSq(c, phi) {
  const sb = c[0] + c[1] + c[2], sa = 1 + c[3] + c[4];
  const n = sb * sb - 4 * (c[0] * c[1] + 4 * c[0] * c[2] + c[1] * c[2]) * phi + 16 * c[0] * c[2] * phi * phi;
  const d = sa * sa - 4 * (c[3] + 4 * c[4] + c[3] * c[4]) * phi + 16 * c[4] * phi * phi;
  return d > 1e-300 ? Math.max(0, n) / d : 1e300;
}

// ── the designs. Coeffs are [b0, b1, b2, a1, a2]. ───────────────────────────
function designRbj(kind, f0, Q, gDb, fs) {
  if (Math.abs(gDb) < 1e-12) return [1, 0, 0, 0, 0];
  if (Q < 1e-4) Q = 1e-4;
  const A = Math.pow(10, gDb / 40), w = 2 * Math.PI * clamp(f0, 1, 0.45 * fs) / fs;
  const cw = Math.cos(w), al = Math.sin(w) / (2 * Q), sA = Math.sqrt(A);
  let b0, b1, b2, a0, a1, a2;
  if (kind === 0) { b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; }
  else if (kind === 1) {
    b0 = A * ((A + 1) - (A - 1) * cw + 2 * sA * al); b1 = 2 * A * ((A - 1) - (A + 1) * cw);
    b2 = A * ((A + 1) - (A - 1) * cw - 2 * sA * al); a0 = (A + 1) + (A - 1) * cw + 2 * sA * al;
    a1 = -2 * ((A - 1) + (A + 1) * cw);             a2 = (A + 1) + (A - 1) * cw - 2 * sA * al;
  } else {
    b0 = A * ((A + 1) + (A - 1) * cw + 2 * sA * al); b1 = -2 * A * ((A - 1) + (A + 1) * cw);
    b2 = A * ((A + 1) + (A - 1) * cw - 2 * sA * al); a0 = (A + 1) - (A - 1) * cw + 2 * sA * al;
    a1 = 2 * ((A - 1) - (A + 1) * cw);              a2 = (A + 1) - (A - 1) * cw - 2 * sA * al;
  }
  return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
}

// impulse-invariant pole/zero pair, in the form that does not overflow at q*w > 710
function iiPair(f, q, fs) {
  const fc = clamp(f, 0.02, 0.47 * fs), w = 2 * Math.PI * fc / fs;
  const c2 = Math.exp(-2 * q * w);
  let c1;
  if (q <= 1) c1 = -2 * Math.exp(-q * w) * Math.cos(Math.sqrt(1 - q * q) * w);
  else { const sq = Math.sqrt(q * q - 1); c1 = -(Math.exp(-(q - sq) * w) + Math.exp(-(q + sq) * w)); }
  return [isFinite(c1) ? c1 : 0, isFinite(c2) ? c2 : 0];
}

// the shelf-Q ceiling: a resonance whose pole pair sits beyond Nyquist cannot exist in a
// minimum-phase digital filter, so taper Q toward 0.7 instead of faking a band-edge bump.
function usableQ(kind, f0, Q, gDb, fs) {
  if (kind === 0 || kind >= 3) return Q;
  const A = Math.pow(10, gDb / 40);
  const fp = kind === 1 ? f0 / Math.sqrt(A) : f0 * Math.sqrt(A);
  const r = fp / (0.5 * fs);
  if (r <= 0.70) return Q;
  let t = Math.min(1, (r - 0.70) / 0.60); t = t * t * (3 - 2 * t);
  return Q + (0.70 - Q) * t;
}

const PROBE_FR  = [0.0004,0.0016,0.0055,0.016,0.040,0.075,0.120,0.190,0.270,0.340,0.405,0.455];
const PROBE_PHI = PROBE_FR.map(f => { const s = Math.sin(Math.PI * f); return s * s; });

function designMatched(kind, f0, Q, gDb, fs) {
  if (Math.abs(gDb) < 1e-12) return [1, 0, 0, 0, 0];
  if (Q < 1e-4) Q = 1e-4;
  const A = Math.pow(10, gDb / 40);
  let fp, qp, fz, qz;
  if (kind === 0) { fp = f0; qp = 1 / (2 * A * Q); fz = f0; qz = A / (2 * Q); }
  else if (kind === 1) { fp = f0 / Math.sqrt(A); fz = f0 * Math.sqrt(A); qp = qz = 1 / (2 * Q); }
  else { fp = f0 * Math.sqrt(A); fz = f0 / Math.sqrt(A); qp = qz = 1 / (2 * Q); }
  const [a1, a2] = iiPair(fp, qp, fs);

  // (A) closed-form three-point magnitude fit
  let A1 = null;
  {
    const fFit = Math.min(f0, 0.40 * fs);
    const G0 = protoMag2(kind, 0, f0, Q, gDb), G1 = protoMag2(kind, fFit, f0, Q, gDb);
    const GN = protoMag2(kind, 0.5 * fs, f0, Q, gDb);
    const sp = Math.sin(Math.PI * fFit / fs), phi = sp * sp;
    const sumA = 1 + a1 + a2, difA = 1 - a1 + a2;
    const R1 = sumA * Math.sqrt(G0), R2 = difA * Math.sqrt(GN);
    const bb1 = 0.5 * (R1 - R2), S = 0.5 * (R1 + R2);
    const PA = sumA * sumA - 4 * (a1 + 4 * a2 + a1 * a2) * phi + 16 * a2 * phi * phi;
    const den = 16 * phi * (phi - 1);
    if (den < -1e-10) {
      const W = (G1 * PA - R1 * R1 + 4 * phi * bb1 * S) / den, disc = S * S - 4 * W;
      if (disc >= 0) { const r = Math.sqrt(disc), b0 = 0.5 * (S + r);
                       const c = [b0, bb1, S - b0, a1, a2];
                       if (c.every(isFinite)) A1 = c; }
    }
  }
  // (B)/(D) matched pole-zero, anchored at DC and at Nyquist
  const cands = [];
  if (A1) cands.push(A1);
  {
    const [z1, z2] = iiPair(fz, qz, fs);
    for (let e = 0; e < 2; ++e) {
      const anchor = e === 0 ? 0 : 0.5 * fs;
      const tgt = protoMag2(kind, anchor, f0, Q, gDb);
      const nb = e === 0 ? (1 + z1 + z2) : (1 - z1 + z2);
      const na = e === 0 ? (1 + a1 + a2) : (1 - a1 + a2);
      if (Math.abs(nb) < 1e-12 || Math.abs(na) < 1e-12) continue;
      const k = Math.sqrt(tgt) * na / nb, c = [k, k * z1, k * z2, a1, a2];
      if (c.every(isFinite)) cands.push(c);
    }
  }
  cands.push(designRbj(kind, f0, Q, gDb, fs));                        // (C) plain bilinear
  let best = cands[0], bestE = Infinity;
  for (const c of cands) {
    let e = 0;
    for (let i = 0; i < 12; ++i) {
      const t = protoMag2(kind, PROBE_FR[i] * fs, f0, Q, gDb);
      const r = Math.max(1e-300, magSq(c, PROBE_PHI[i]));
      e += r / t + t / r;
    }
    if (e < bestE) { bestE = e; best = c; }
  }
  return best;
}

// 1-pole shelves as gLo*LP + gHi*HP. The m^2(1+K/m)/(1+Km) form is algebraically the same
// and numerically lethal: a 96 dB slant loses its whole low end to cancellation.
// 🚨 fb422 — the prewarped corner G is the primitive, because the SLANT places its corner
// by arithmetic and an atan/tan round trip would smear the pivot.
// The one-pole pole cap: a1 = (G-1)/(G+1), so |a1| -> 1 as G leaves the window in which the
// decay is under MAX_RING_SEC. Same rMax as the biquad ring cap.
function onePoleGmax(fs) { const r = Math.exp(-6.907755 / (MAX_RING_SEC * fs)); return (1 + r) / (1 - r); }
function designShelf1G(G, gLoDb, gHiDb, fs) {
  const gMax = onePoleGmax(fs);
  G = clamp(G, 1 / gMax, gMax);
  const gL = Math.pow(10, gLoDb / 20), gH = Math.pow(10, gHiDb / 20), d = 1 + G;
  if (gLoDb === 0 && gHiDb === 0) return [1, (G - 1) / d, 0, (G - 1) / d, 0];
  return [(gL * G + gH) / d, (gL * G - gH) / d, 0, (G - 1) / d, 0];
}
function designShelf1(f0, gLoDb, gHiDb, fs) {
  return designShelf1G(Math.tan(Math.PI * clamp(f0, 1, 0.49 * fs) / fs), gLoDb, gHiDb, fs);
}
// 🚨 fb422 — THE SLANT. It was designShelf1(pivot, -g, +g), whose 0 dB crossing sits at
// pivot/10^(g/20): the pivot SLID 700 Hz -> 2.8 Hz as the knob opened and 120 Hz travelled
// -4.4 dB and then back UP to +8.8 dB. Put the CORNER where the crossing has to land:
// G_corner = G_pivot * 10^(g/20), in the prewarped domain, so the pivot is exact digitally.
function designSlant(fPivot, gDb, fs) {
  const Gp = Math.tan(Math.PI * clamp(fPivot, 1, 0.49 * fs) / fs);
  return designShelf1G(Gp * Math.pow(10, gDb / 20), -gDb, gDb, fs);
}
function designOnePole(kind, f0, gDb, fs) {
  if (kind === 5) return designSlant(f0, gDb, fs);
  if (kind === 4) return designShelf1(f0, 0, gDb, fs);
  return designShelf1(f0, gDb, 0, fs);
}

// nothing in this device may ring longer than MAX_RING_SEC
function limitRing(c, fs) {
  const rMax = Math.exp(-6.907755 / (MAX_RING_SEC * fs)), r2 = rMax * rMax;
  if (c[4] > r2) { const sc = Math.sqrt(r2 / c[4]); c[3] *= sc; c[4] = r2; }
  return c;
}
function designBand(kind, f0, Q, gDb, fs) {
  if (Math.abs(gDb) < 1e-12) return [1, 0, 0, 0, 0];
  if (kind >= 3) return designOnePole(kind, f0, gDb, fs);
  const fLo = fs / 48, fHi = fs / 24;
  if (f0 <= fLo) return limitRing(designRbj(kind, f0, Q, gDb, fs), fs);
  if (f0 >= fHi) return limitRing(designMatched(kind, f0, Q, gDb, fs), fs);
  let t = (Math.log(f0) - Math.log(fLo)) / (Math.log(fHi) - Math.log(fLo));
  t = t * t * (3 - 2 * t);
  const a = designRbj(kind, f0, Q, gDb, fs), b = designMatched(kind, f0, Q, gDb, fs);
  return limitRing(a.map((v, i) => v + t * (b[i] - v)), fs);
}

const widthMul = s => s <= 0.5 ? 0.25 * Math.pow(4, s * 2) : Math.pow(40, (s - 0.5) * 2);

// ═══════════════════════════════════════════════════════════════════════════════
class TerrainEq extends AudioWorkletProcessor {
  constructor() {
    super();
    this.fs = sampleRate;
    this.p = { type: 0, character: 0, axis: 0, f1: .5, f2: .5, f3: .5, mix: 1,
               b1: .5, b2: .5, b3: .5, b4: .5, b5: .5, b6: .5, b7: .5, b8: .5,
               x1: .5, x2: .5, x3: .5, x4: .5, x5: .5, x6: .5, x7: .5, x8: .5, xon1: 0, xon2: 0, xon3: 0, xon4: 0,
               q1: .5, q2: .5, q3: .5, q4: .5, q5: .5, q6: .5, q7: .5, q8: .5 };   // fb438 — free bells · fb441 — per-band Q
    this.tg = new Float64Array(27).fill(0.5);   // fb441 — + 8 per-band Q
    this.sm = new Float64Array(27).fill(0.5);
    this.xOn = [false,false,false,false];
    // fb422: Amount .010 -> .020. It multiplies all four band gains (+-60 dB of authority) and
    // had the SHORTEST tau in the table; an instant Amount write measured 1.63 dB of wet-gain
    // change in ONE sample. The number scales as 1/tau, which is what proves it is a smoothing
    // fault; `Trait` does not move with tau at all, because that one is stored resonator energy.
    const tau = [.020,.012,.020,.012,.020,.012,.020,.020,.015,.012,.020, .020,.012,.020,.012,.020,.012,.020,.012, .015,.015,.015,.015,.015,.015,.015,.015];   // + the free bells (fb438) + per-band Q (fb441)
    this.k = tau.map(t => 1 - Math.exp(-(DESIGN_BLK / this.fs) / t));
    this.mixSm = 1; this.mixTg = 1;
    this.mixK = 1 - Math.exp(-1 / (0.010 * this.fs));
    // 13 stages: 4 bands x 2 + slant + 4 free bells (fb438). cur/tgt/inc coefficients + 2 channels of state.
    this.st = [];
    for (let i = 0; i < 13; ++i)
      this.st.push({ on: false, kind: 0, f: 1000, q: 1, g: 0, lf: -1, lq: -1, lg: 1e9,
                     cur: [1,0,0,0,0], tgt: [1,0,0,0,0], inc: [0,0,0,0,0],
                     z1: [0,0], z2: [0,0] });
    this.act = []; this.ctr = 0;
    this.det = { d1: [0,0,0,0], d2: [0,0,0,0], env: [0,0,0,0], envW: 0,
                 a1: [.5,.5,.5,.5], a2: [0,0,0,0], a3: [0,0,0,0],
                 atk: [.01,.01,.01,.01], rel: [.001,.001,.001,.001] };
    this.ride = [1,1,1,1];
    this.nodeHz = [100,550,3100,15500,632,632,632,632]; this.nodeDb = [0,0,0,0,0,0,0,0]; this.nodeOn = [1,1,1,1,0,0,0,0];   // fb438 — 4 roles + 4 free
    this.slantDb = 0; this.lvl = 0;
    this.lvlK = 1 - Math.exp(-1 / (0.060 * this.fs));
    this.phi = new Float64Array(CURVE_BINS);
    for (let i = 0; i < CURVE_BINS; ++i) {
      const s = Math.sin(Math.PI * curveBinHz(i) / this.fs); this.phi[i] = s * s;
    }
    this.curve = new Float32Array(CURVE_BINS);
    this.pushCtr = 0; this.pushEvery = Math.max(1, Math.floor(this.fs / 30 / 128));
    this.port.onmessage = e => this.setParams(e.data);
    this.resolve(); this.designAll(true);
  }
  static get parameterDescriptors() { return []; }

  setParams(q) {
    Object.assign(this.p, q);
    const p = this.p;
    this.tg[0] = clamp(p.b1,0,1); this.tg[1] = clamp(p.b2,0,1);
    this.tg[2] = clamp(p.b3,0,1); this.tg[3] = clamp(p.b4,0,1);
    this.tg[4] = clamp(p.b5,0,1); this.tg[5] = clamp(p.b6,0,1);
    this.tg[6] = clamp(p.b7,0,1); this.tg[7] = clamp(p.b8,0,1);
    this.tg[8] = clamp(p.f1,0,1); this.tg[9] = clamp(p.f2,0,1); this.tg[10] = clamp(p.f3,0,1);
    for (let k = 0; k < 8; ++k) this.tg[11 + k] = clamp(p['x' + (k + 1)], 0, 1);     // fb438 — free bells
    for (let k = 0; k < 4; ++k) this.xOn[k] = !!p['xon' + (k + 1)];
    for (let k = 0; k < 8; ++k) this.tg[19 + k] = clamp(p['q' + (k + 1)], 0, 1);      // fb441 — per-band Q
    this.mixTg = clamp(p.mix,0,1);
  }

  resolve() {
    const T = TSPEC[clamp(this.p.type|0, 0, 6)];
    const CH = CSPEC[clamp(this.p.type|0, 0, 6)][clamp(this.p.character|0, 0, 7)];
    const sm = this.sm, amount = sm[10] * AMOUNT_MAX, shape = sm[7];
    const f = [bandHz(0, sm[0]) * CH.fm[0], bandHz(1, sm[2]) * CH.fm[1],
               bandHz(2, sm[4]) * CH.fm[2], bandHz(3, sm[6]) * CH.fm[3]];
    const g = [(sm[1]*2-1) * DB_SPAN * CH.gm[0], (sm[3]*2-1) * DB_SPAN * CH.gm[1],
               (sm[5]*2-1) * DB_SPAN * CH.gm[2], (sm[9]*2-1) * DB_SPAN * CH.gm[3]];
    for (let b = 0; b < 4; ++b) if (g[b] < 0) g[b] *= CH.cutG;
    const kd = [], steep = [];
    for (let b = 0; b < 4; ++b) {
      let k = CH.kd[b] >= 0 ? CH.kd[b] : T.k[b];
      steep[b] = (k === 5); if (steep[b]) k = T.k[b];
      kd[b] = k;
    }
    // the Type's GAIN law, before Amount so Amount is always a clean x2
    if (T.law === 1) for (let b = 0; b < 4; ++b) {
      const u = Math.abs(g[b]) / DB_SPAN; g[b] *= 1 - T.soften * CH.e2 * u * u;
    } else if (T.law === 4) {
      const knee = CH.e2;
      for (let b = 0; b < 4; ++b) g[b] = knee * Math.tanh(g[b] / knee);
      g[3] *= 1.33;
    } else if (T.law === 6) {
      for (let b = 0; b < 4; ++b) if (g[b] < CH.e1) {
        const sl = (G_FLOOR - CH.e1) / (-DB_SPAN - CH.e1);
        g[b] = CH.e1 + (g[b] - CH.e1) * sl;
      }
    } else if (T.law === 3) {
      for (let b = 0; b < 4; ++b) if (g[b] < 0) g[b] *= 1.15;
    }
    for (let b = 0; b < 4; ++b) g[b] = clamp(g[b] * amount, -96, G_CEIL);

    if (T.law === 5) {                                   // the Dynamic ride
      const Th = -26 + (shape * 2 - 1) * 20, Wd = Math.max(0.5, CH.e3), inv = CH.e2 === 1;
      for (let b = 0; b < 4; ++b) {
        const eDb = 20 * Math.log10(this.det.env[b] + 1e-12) + DET_CAL;
        let u = clamp((eDb - Th) / Wd + 0.5, 0, 1); u = u * u * (3 - 2 * u);
        const up = g[b] > 0;
        const r = (up !== inv) ? 1 - u : u;
        this.ride[b] = r; g[b] *= r;
      }
      for (let b = 0; b < 4; ++b) {
        const fc = clamp(f[b], 20, 0.45 * this.fs), gg = Math.tan(Math.PI * fc / this.fs);
        const kk = 1 / Math.max(0.3, 0.8 * T.q[b] * CH.qm[b]);
        this.det.a1[b] = 1 / (1 + gg * (gg + kk));
        this.det.a2[b] = gg * this.det.a1[b];
        this.det.a3[b] = gg * this.det.a2[b];
        const atk = Math.max(0.002, 2 / fc) * Math.max(0.05, CH.e1);
        let rel = 8 * atk; if (CH.e2 === 3) rel *= 10;
        this.det.atk[b] = 1 - Math.exp(-1 / (atk * this.fs));
        this.det.rel[b] = 1 - Math.exp(-1 / (rel * this.fs));
      }
    } else this.ride = [1,1,1,1];

    // the Q LAW — Q is never a knob
    const q = [T.q[0]*CH.qm[0], T.q[1]*CH.qm[1], T.q[2]*CH.qm[2], T.q[3]*CH.qm[3]];
    if (T.law === 0) { const w = widthMul(shape); for (let b = 0; b < 4; ++b) q[b] *= w; }
    else if (T.law === 1) { const sq = 0.5 + shape * 4 * CH.e1;
      for (let b = 0; b < 4; ++b) if (kd[b] !== 0) q[b] = sq * CH.qm[b]; }
    else if (T.law === 2) {
      const ex = clamp(0.4 + shape * 2.6 + CH.e1, 0.15, 6);
      for (let b = 0; b < 4; ++b) {
        const shelf = kd[b] !== 0;
        if (shelf && CH.e3 < 0.5) continue;
        const x = Math.min(Math.abs(g[b]) / DB_SPAN, 2);
        const apply = CH.e2 === 0 || (CH.e2 === 1 && g[b] > 0) || (CH.e2 === 2 && g[b] < 0);
        const base = shelf ? 0.70 : 0.55, span = shelf ? 2.5 : 13;
        q[b] = (apply ? base + span * Math.pow(x, ex) : base) * CH.qm[b];
      }
    }
    else if (T.law === 3) q[2] = 0.70 * CH.qm[2];
    else if (T.law === 6) { const ring = 2 * Math.pow(32, shape);
      for (let b = 0; b < 4; ++b)
        q[b] = ring * (1 + CH.e2 * Math.min(Math.abs(g[b]), 60) / 12) * CH.qm[b]; }
    for (let b = 0; b < 4; ++b) {
      q[b] *= Math.pow(2, (this.sm[19 + b] - 0.5) * 6);                                  // fb441 — the node's own width (x1 by default)
      if (g[b] < 0) q[b] *= CH.cutQ;
      q[b] = clamp(q[b], Q_MIN, Q_MAX);
      q[b] = usableQ(kd[b], f[b], q[b], g[b], this.fs);
    }

    for (let b = 0; b < 4; ++b) {
      const s0 = this.st[b * 2], s1 = this.st[b * 2 + 1];
      const live = Math.abs(g[b]) > 1e-4;
      s0.on = live; s0.kind = kd[b]; s0.f = f[b]; s0.q = q[b]; s0.g = g[b];
      s1.on = false;
      if (live && steep[b] && (kd[b] === 1 || kd[b] === 2)) {
        s0.g = g[b] * 0.5; s0.q = clamp(q[b] * 0.765, Q_MIN, Q_MAX);
        s1.on = true; s1.kind = kd[b]; s1.f = f[b]; s1.g = g[b] * 0.5;
        s1.q = clamp(q[b] * 1.848, Q_MIN, Q_MAX);
      } else if (T.law === 3 && b === 0 && g[0] > 0.01) {
        // 🔑 THE PULTEC TRICK, from ONE knob: the boost also engages an attenuation shelf
        const dip = shape * 1.2;
        s1.on = true; s1.kind = CH.e2 === 2 ? 0 : 1;
        s1.f = clamp(f[0] * CH.e1, 20, 2000); s1.q = CH.e2 === 2 ? 1.0 : 0.9;
        s1.g = -0.7 * g[0] * dip;
      } else if (T.law === 3 && b === 3 && CH.e2 === 1 && live) {
        s1.on = true; s1.kind = 2; s1.f = clamp(f[3] / CH.e1, 1000, 40000);
        s1.q = 0.8; s1.g = -0.6 * g[3] * shape * 1.2;
      } else if (T.law === 4 && b === 3 && live && shape > 0.001) {
        s1.on = true; s1.kind = kd[3];
        s1.f = clamp(f[3] * Math.pow(2, CH.e1), 1000, 60000);
        s1.q = q[3]; s1.g = g[3] * 0.8 * shape;
      }
      s0.g = clamp(s0.g, -96, G_CEIL); s1.g = clamp(s1.g, -96, G_CEIL);
      if (Math.abs(s1.g) < 1e-4) s1.on = false;
      this.nodeHz[b] = f[b]; this.nodeDb[b] = g[b];
    }
    // fb438 — THE FREE BELLS (stages 9..12): full-range constant-Q bells, surgical by design
    for (let k = 0; k < 4; ++k) {
      const S = this.st[9 + k], tF = sm[11 + 2 * k], tG = sm[12 + 2 * k];
      const fHz = clamp(20 * Math.pow(1000, tF), 20, 0.45 * this.fs);
      const gDb = clamp((tG * 2 - 1) * DB_SPAN * amount, -96, G_CEIL);
      const live = this.xOn[k] && Math.abs(gDb) > 1e-4;
      let qf = clamp((T.law === 0 ? widthMul(shape) : 1.0) * Math.pow(2, (this.sm[23 + k] - 0.5) * 6), Q_MIN, Q_MAX);   // fb441 — x the node's own width
      qf = usableQ(0, fHz, qf, gDb, this.fs);
      S.on = live; S.kind = 0; S.f = fHz; S.q = qf; S.g = gDb;
      this.nodeHz[4 + k] = fHz; this.nodeDb[4 + k] = this.xOn[k] ? gDb : 0; this.nodeOn[4 + k] = this.xOn[k] ? 1 : 0;
    }
    this.slantDb = (sm[8] * 2 - 1) * SLANT_SPAN * amount;
    const ts = this.st[8];
    ts.on = Math.abs(this.slantDb) > 1e-4;
    ts.kind = 5; ts.f = CH.pivot; ts.q = 0.707; ts.g = this.slantDb;
  }

  designAll(snap) {
    const inv = 1 / DESIGN_BLK;
    this.act.length = 0;
    for (let i = 0; i < 13; ++i) {
      const S = this.st[i];
      if (!S.on) {
        if (S.lg !== 1e9) { S.tgt = [1,0,0,0,0]; S.lf = -1; S.lq = -1; S.lg = 1e9; }
      } else if (S.f !== S.lf || S.q !== S.lq || S.g !== S.lg) {
        S.tgt = S.kind >= 3 ? designOnePole(S.kind, S.f, S.g, this.fs)
                            : designBand(S.kind, S.f, S.q, S.g, this.fs);
        S.lf = S.f; S.lq = S.q; S.lg = S.g;
      }
      if (snap) { S.cur = S.tgt.slice(); S.inc = [0,0,0,0,0]; }
      else for (let k = 0; k < 5; ++k) S.inc[k] = (S.tgt[k] - S.cur[k]) * inv;
      for (let c = 0; c < 2; ++c) {
        if (Math.abs(S.z1[c]) < 1e-18) S.z1[c] = 0;
        if (Math.abs(S.z2[c]) < 1e-18) S.z2[c] = 0;
        if (!isFinite(S.z1[c]) || !isFinite(S.z2[c])) { S.z1[c] = 0; S.z2[c] = 0; }
      }
      const idle = S.cur[0] === 1 && S.cur[1] === 0 && S.cur[2] === 0 && S.cur[3] === 0
                && S.cur[4] === 0 && S.z1[0] === 0 && S.z2[0] === 0 && S.z1[1] === 0 && S.z2[1] === 0;
      if (!S.on && idle) continue;
      this.act.push(i);
    }
    const ti = this.act.indexOf(8);            // the slant is the baseline: it runs FIRST
    if (ti > 0) { this.act.splice(ti, 1); this.act.unshift(8); }
  }

  detectorStep(x) {
    const CH = CSPEC[clamp(this.p.type|0,0,6)][clamp(this.p.character|0,0,7)], d = this.det;
    if (CH.e2 === 2) {
      const r = Math.abs(x);
      d.envW += (r > d.envW ? d.atk[0] : d.rel[0]) * (r - d.envW);
      if (d.envW < 1e-20) d.envW = 0;
      for (let b = 0; b < 4; ++b) d.env[b] = d.envW;
      return;
    }
    for (let b = 0; b < 4; ++b) {
      const v3 = x - d.d2[b];
      const v1 = d.a1[b] * d.d1[b] + d.a2[b] * v3;
      const v2 = d.d2[b] + d.a2[b] * d.d1[b] + d.a3[b] * v3;
      d.d1[b] = 2 * v1 - d.d1[b]; d.d2[b] = 2 * v2 - d.d2[b];
      const r = Math.abs(v1);
      d.env[b] += (r > d.env[b] ? d.atk[b] : d.rel[b]) * (r - d.env[b]);
      if (d.env[b] < 1e-20) d.env[b] = 0;
    }
  }

  pushCurve() {
    for (let i = 0; i < CURVE_BINS; ++i) {
      let acc = 0;
      for (const s of this.act) acc += 10 * Math.log10(Math.max(1e-30, magSq(this.st[s].cur, this.phi[i])));
      this.curve[i] = clamp(acc, -120, 80);
    }
    this.port.postMessage({ curve: Array.from(this.curve), nodeHz: this.nodeHz.slice(),
                            nodeDb: this.nodeDb.slice(), nodeOn: this.nodeOn.slice(), lvl: clamp(this.lvl * 6, 0, 1),
                            type: TYPES[this.p.type|0], trait: TRAIT[this.p.type|0],
                            character: CHARS[this.p.type|0][this.p.character|0],
                            focus: FOCUS[this.p.axis|0],
                            dropdownHeads: DROPDOWN.slice() });
  }

  process(inputs, outputs) {
    const inp = inputs[0], out = outputs[0];
    if (!out || out.length === 0) return true;
    const n = out[0].length;
    const iL = inp && inp[0] ? inp[0] : new Float32Array(n);
    const iR = inp && inp[1] ? inp[1] : iL;
    const oL = out[0], oR = out[1] || out[0];
    const S2 = 0.70710678118654752;
    const focus = clamp(this.p.axis|0, 0, 4);
    const dyn = TSPEC[clamp(this.p.type|0,0,6)].law === 5;

    let i = 0;
    while (i < n) {
      if (this.ctr <= 0) {
        for (let k = 0; k < 19; ++k) this.sm[k] += this.k[k] * (this.tg[k] - this.sm[k]);
        this.resolve(); this.designAll(false);
        this.ctr = DESIGN_BLK;
      }
      const nn = Math.min(n - i, this.ctr);
      this.ctr -= nn;
      for (let k = 0; k < nn; ++k, ++i) {
        for (const s of this.act) {
          const S = this.st[s];
          S.cur[0] += S.inc[0]; S.cur[1] += S.inc[1]; S.cur[2] += S.inc[2];
          S.cur[3] += S.inc[3]; S.cur[4] += S.inc[4];
        }
        const dl = iL[i], dr = iR[i];
        let a, b;
        if (focus === 1) { a = (dl + dr) * S2; b = (dl - dr) * S2; }
        else if (focus === 2) { a = (dl - dr) * S2; b = (dl + dr) * S2; }
        else if (focus === 4) { a = dr; b = dl; }
        else { a = dl; b = dr; }
        const both = focus === 0;
        for (const s of this.act) {
          const S = this.st[s], c = S.cur;
          let x = a, y = c[0] * x + S.z1[0];
          S.z1[0] = c[1] * x - c[3] * y + S.z2[0];
          S.z2[0] = c[2] * x - c[4] * y; a = y;
          if (both) { x = b; y = c[0] * x + S.z1[1];
                      S.z1[1] = c[1] * x - c[3] * y + S.z2[1];
                      S.z2[1] = c[2] * x - c[4] * y; b = y; }
        }
        let wl, wr;
        if (focus === 1) { wl = (a + b) * S2; wr = (a - b) * S2; }
        else if (focus === 2) { wl = (b + a) * S2; wr = (b - a) * S2; }
        else if (focus === 4) { wl = b; wr = a; }
        else { wl = a; wr = b; }
        this.mixSm += this.mixK * (this.mixTg - this.mixSm);
        const mw = this.mixSm, md = 1 - mw;                 // LINEAR, never equal-power
        const ol = dl * md + wl * mw, orr = dr * md + wr * mw;
        oL[i] = ol; if (oR !== oL) oR[i] = orr;
        const pk = Math.max(Math.abs(ol), Math.abs(orr));
        this.lvl += this.lvlK * (pk - this.lvl);
        if (dyn) this.detectorStep(0.5 * (dl + dr));
      }
    }
    if (++this.pushCtr >= this.pushEvery) { this.pushCtr = 0; this.pushCurve(); }
    return true;
  }
}
registerProcessor('terrain-eq', TerrainEq);
