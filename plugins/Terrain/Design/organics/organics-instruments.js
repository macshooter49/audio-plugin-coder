/* ═══════════════════════════════════════════════════════════════════════════════════════════════
   ORGANICS — instrument line-art set (Terrain oscillator engine #8, mockup)
   ───────────────────────────────────────────────────────────────────────────────────────────────
   Every picture is a tiny 3-D model (centimetres, real proportions) projected through ONE shared
   three-quarter orthographic camera and emitted as flat SVG — so every instrument is seen from the
   same eye, drawn with the same two line weights, at the same stroke width whatever its size.

   Hidden lines: each part is filled with the display's own background colour (--org-bg) and parts
   are painted back-to-front, so a part in front simply covers what is behind it. Inside one part,
   back-facing walls and the far half of a tube's end ring are never drawn (back-face rule).

   Output is generated ONCE per instrument switch (a few hundred path strings, < 2 ms). Nothing is
   re-projected per frame: the play reaction only flips classes / rewrites the d of the few strings
   that are actually ringing (see organics-mockup.html → Anim).

   API
     OrganicsArt.families            { id: { name, cat, build(K) } }
     OrganicsArt.render(id, {w,h})   → { svg: '<g>…</g>', meta }   (meta = animation anchors)
   ═══════════════════════════════════════════════════════════════════════════════════════════════ */
(function (root) {
  'use strict';
  const D2R = Math.PI / 180;
  const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const len = a => Math.hypot(a[0], a[1], a[2]);
  const nrm = a => { const l = len(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
  const lerp = (a, b, t) => a + (b - a) * t;
  const lerp3 = (a, b, t) => [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];

  /* ── camera: yaw about Y, then pitch (looking down). screen x right, y down; d = toward the eye ── */
  function Camera(yaw, pitch) {
    const cy = Math.cos(yaw * D2R), sy = Math.sin(yaw * D2R), cp = Math.cos(pitch * D2R), sp = Math.sin(pitch * D2R);
    return {
      P(p) { const x1 = p[0] * cy + p[2] * sy, z1 = -p[0] * sy + p[2] * cy; return [x1, -p[1] * cp + z1 * sp, z1 * cp + p[1] * sp]; },
      D(n) { const z1 = -n[0] * sy + n[2] * cy; return z1 * cp + n[1] * sp; },
      V: [-sy * cp, sp, cy * cp]
    };
  }

  /* ── 2-D / 3-D curve helpers ── */
  function crSeg(p0, p1, p2, p3, t) {           // centripetal-ish Catmull-Rom (uniform, fine at this scale)
    const t2 = t * t, t3 = t2 * t, o = [];
    for (let k = 0; k < p1.length; k++)
      o[k] = 0.5 * ((2 * p1[k]) + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3);
    return o;
  }
  function spline(ctrl, per, closed) {
    per = per || 8; const n = ctrl.length, out = [];
    const g = i => closed ? ctrl[(i + n) % n] : ctrl[Math.max(0, Math.min(n - 1, i))];
    const segs = closed ? n : n - 1;
    for (let i = 0; i < segs; i++) for (let s = 0; s < per; s++) out.push(crSeg(g(i - 1), g(i), g(i + 1), g(i + 2), s / per));
    if (!closed) out.push(ctrl[n - 1].slice());
    return out;
  }
  const arc2 = (cx, cy, r, a0, a1, n) => { const o = []; n = n || 16; for (let i = 0; i <= n; i++) { const a = (a0 + (a1 - a0) * i / n) * D2R; o.push([cx + r * Math.cos(a), cy + r * Math.sin(a)]); } return o; };
  const area2 = p => { let a = 0; for (let i = 0; i < p.length; i++) { const q = p[i], r = p[(i + 1) % p.length]; a += q[0] * r[1] - r[0] * q[1]; } return a / 2; };
  function mirrorZ(half) {            // half outline (x,z>=0) from nose to tail → full closed outline
    const o = half.slice(); for (let i = half.length - 2; i > 0; i--) o.push([half[i][0], -half[i][1]]); return o;
  }
  function offset2(poly, d) {         // inward offset of a closed CCW polygon (for purfling / inset lines)
    const n = poly.length, o = [];
    for (let i = 0; i < n; i++) {
      const a = poly[(i - 1 + n) % n], b = poly[i], c = poly[(i + 1) % n];
      let n1 = [-(b[1] - a[1]), b[0] - a[0]], n2 = [-(c[1] - b[1]), c[0] - b[0]];
      const l1 = Math.hypot(n1[0], n1[1]) || 1, l2 = Math.hypot(n2[0], n2[1]) || 1;
      const m = [n1[0] / l1 + n2[0] / l2, n1[1] / l1 + n2[1] / l2], ml = Math.hypot(m[0], m[1]) || 1;
      o.push([b[0] + m[0] / ml * d, b[1] + m[1] / ml * d]);
    }
    return o;
  }

  /* frames: map local (u,v,w) → world */
  function Frame(o, u, v, w) { return { o, u: nrm(u), v: nrm(v), w: nrm(w), at(a, b, c) { return add(add(add(this.o, mul(this.u, a)), mul(this.v, b)), mul(this.w, c)); } }; }
  const FLAT = (y) => Frame([0, y || 0, 0], [1, 0, 0], [0, 0, 1], [0, 1, 0]);       // outline in x/z, extrude up
  const SIDE = (z) => Frame([0, 0, z || 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]);       // outline in x/y, extrude toward eye
  const END = (x) => Frame([x || 0, 0, 0], [0, 0, 1], [0, 1, 0], [1, 0, 0]);        // outline in z/y, extrude along x
  function ring3(c, n, r, seg, a0) {
    n = nrm(n); let a = cross(n, [0, 1, 0]); if (len(a) < 1e-3) a = cross(n, [1, 0, 0]); a = nrm(a); const b = cross(n, a);
    const o = []; seg = seg || 28; a0 = a0 || 0;
    for (let i = 0; i < seg; i++) { const t = a0 + i / seg * Math.PI * 2; o.push(add(c, add(mul(a, r * Math.cos(t)), mul(b, r * Math.sin(t))))); }
    return o;
  }

  /* ═══ Scene / Kit ═══════════════════════════════════════════════════════════════════════════ */
  function Kit(yaw, pitch) {
    const cam = Camera(yaw, pitch);
    const parts = [];
    let cur = null;
    const K = {
      cam, V: cam.V, spline, arc2, mirrorZ, offset2, Frame, FLAT, SIDE, END, ring3, add, sub, mul, nrm, cross, dot, lerp3, len,
      /* a part = one paint layer. meta: {role, i, cls, anchor:[x,y,z], a:[..], b:[..]} */
      part(meta, fn) { const p = { fills: [], lines: [], meta: meta || {} }; const prev = cur; cur = p; parts.push(p); if (fn) fn(); cur = prev; return p; },
      _p() { if (!cur) { const p = { fills: [], lines: [], meta: {} }; parts.push(p); return p; } return cur; },
      line(pts3, w, closed) { this._p().lines.push({ p: pts3.map(cam.P), w: w || 'm', c: !!closed }); },
      fill(pts3) { this._p().fills.push(pts3.map(cam.P)); },
      /* decal ring (flat circle) */
      ring(c, n, r, w, seg) { const g = ring3(c, n, r, seg); this.line(g, w, true); return g; },
      disc(c, n, r, w) { const g = ring3(c, n, r, 28); this.fill(g); this.line(g, w || 'm', true); },
      /* extruded prism: outline (u,v) in frame, from w=0 to w=h */
      prism(outline, h, fr, o) {
        o = o || {}; const P = this._p(); let pts = outline.slice(); if (area2(pts) < 0) pts.reverse();
        const n = pts.length, T = [], B = [], vis = [], corner = [];
        for (const q of pts) { B.push(fr.at(q[0], q[1], 0)); T.push(fr.at(q[0], q[1], h)); }
        for (let i = 0; i < n; i++) {
          const a = pts[i], b = pts[(i + 1) % n], du = b[0] - a[0], dv = b[1] - a[1];
          const N = add(mul(fr.u, dv), mul(fr.v, -du));
          vis.push(cam.D(N) > 1e-6);
          const pa = pts[(i - 1 + n) % n], v1 = [a[0] - pa[0], a[1] - pa[1]], v2 = [du, dv];
          const cs = (v1[0] * v2[0] + v1[1] * v2[1]) / ((Math.hypot(v1[0], v1[1]) * Math.hypot(v2[0], v2[1])) || 1);
          corner.push(cs < Math.cos((o.corner || 28) * D2R));
        }
        const topVis = cam.D(fr.w) * (h > 0 ? 1 : -1) > 0;
        const capA = topVis ? T : B, capB = topVis ? B : T;
        const PA = capA.map(cam.P);
        P.fills.push(PA);
        for (let i = 0; i < n; i++) if (vis[i]) P.fills.push([B[i], B[(i + 1) % n], T[(i + 1) % n], T[i]].map(cam.P));
        const w = o.w || 'm';
        if (!o.noCap) P.lines.push({ p: PA, w: o.capW || w, c: true });
        // far cap: only the runs along visible walls
        let run = null;
        for (let k = 0; k <= n; k++) {
          const i = k % n;
          if (k < n && vis[i]) { if (!run) run = [capB[i]]; run.push(capB[(i + 1) % n]); }
          else if (run) { P.lines.push({ p: run.map(cam.P), w, c: false }); run = null; }
        }
        if (run) P.lines.push({ p: run.map(cam.P), w, c: false });
        for (let i = 0; i < n; i++) {
          const vp = vis[(i - 1 + n) % n], vc = vis[i];
          if (vp !== vc || (corner[i] && (vp || vc))) P.lines.push({ p: [T[i], B[i]].map(cam.P), w: (vp !== vc) ? w : (o.edgeW || w), c: false });
        }
        return { T, B };
      },
      box(x0, y0, z0, x1, y1, z1, o) { return this.prism([[x0, z0], [x1, z0], [x1, z1], [x0, z1]], y1 - y0, FLAT(y0), o); },
      /* swept tube along a 3-D path; r = number | fn(t 0..1). caps: [start,end] booleans */
      tube(path, r, o) {
        /* exact orthographic silhouette of a swept surface of revolution: on each ring (centre c, axis t,
           radius r, slope r'=dr/ds) the rim point at angle θ is on the outline where its normal ⟂ the eye:
           A·cosθ + B·sinθ = r'(t·v). Where no θ solves it the ring is seen from inside (a bell mouth). */
        o = o || {}; const P = this._p(); const n = path.length; const R = typeof r === 'function' ? r : () => r;
        const V = cam.V, L = [], Rr = [];
        const S = [0]; for (let i = 1; i < n; i++) S.push(S[i - 1] + len(sub(path[i], path[i - 1])));
        const tot = S[n - 1] || 1;
        let ref = null;
        for (let i = 0; i < n; i++) {
          const t = nrm(sub(path[Math.min(n - 1, i + 1)], path[Math.max(0, i - 1)]));
          let a = ref ? sub(ref, mul(t, dot(ref, t))) : cross(t, [0, 1, 0]); if (len(a) < 1e-4) a = cross(t, [1, 0, 0]); a = nrm(a); ref = a; const b = cross(t, a);
          const u = S[i] / tot, rr = R(u), u1 = Math.min(1, u + 1e-3), u0 = Math.max(0, u - 1e-3), rp = (R(u1) - R(u0)) / ((u1 - u0) * tot);
          const A = dot(a, V), B = dot(b, V), C = rp * dot(t, V), RR = Math.hypot(A, B);
          if (RR < 1e-9 || Math.abs(C) > RR) { L.push(null); Rr.push(null); continue; }
          const ph = Math.atan2(B, A), dth = Math.acos(C / RR);
          const pt = th => cam.P(add(path[i], add(mul(a, rr * Math.cos(th)), mul(b, rr * Math.sin(th)))));
          L.push(pt(ph + dth)); Rr.push(pt(ph - dth));
        }
        const runs = arr => { const out = []; let cur = []; arr.forEach(q => { if (q) cur.push(q); else if (cur.length) { out.push(cur); cur = []; } }); if (cur.length) out.push(cur); return out; };
        const Lc = L.filter(Boolean), Rc = Rr.filter(Boolean);
        if (Lc.length > 1) P.fills.push(Lc.concat(Rc.slice().reverse()));
        const w = o.w || 'm';
        runs(L).forEach(q => q.length > 1 && P.lines.push({ p: q, w, c: false })); runs(Rr).forEach(q => q.length > 1 && P.lines.push({ p: q, w, c: false }));
        const caps = o.caps || [true, true];
        [[0, 1], [n - 1, n - 2]].forEach(([i, j], k) => {
          if (!caps[k]) return;
          const t = nrm(sub(path[i], path[j])), rr = R(k ? 1 : 0), g = ring3(path[i], t, rr, 40), gp = g.map(cam.P);
          const facing = cam.D(t) > 0 || (o.open && o.open[k]);
          if (facing) { P.fills.push(gp); P.lines.push({ p: gp, w: (o.capW || w), c: true });
            if (o.inner && o.inner[k]) P.lines.push({ p: ring3(path[i], t, rr * o.inner[k], 40).map(cam.P), w: 'h', c: true }); }
          else { const cd = cam.P(path[i])[2]; const vis = gp.map(q => q[2] >= cd - 1e-9); const arc = [];
            const s0 = vis.findIndex((v, ii) => v && !vis[(ii - 1 + vis.length) % vis.length]);
            if (s0 >= 0) { for (let m = 0; m < gp.length; m++) { const ii = (s0 + m) % gp.length; if (!vis[ii]) break; arc.push(gp[ii]); } P.lines.push({ p: arc, w, c: false }); } }
        });
        return { L, R: Rr };
      },
      /* sphere outline (screen circle) + optional hairline equator */
      sphere(c, r, o) { const P = this._p(); const q = cam.P(c); const g = []; for (let i = 0; i < 36; i++) { const a = i / 36 * Math.PI * 2; g.push([q[0] + r * Math.cos(a), q[1] + r * Math.sin(a), q[2]]); }
        P.fills.push(g); P.lines.push({ p: g, w: (o && o.w) || 'm', c: true }); return q; },
      P: cam.P
    };
    K.parts = parts;
    return K;
  }

  /* ═══ render: fit into W×H, emit SVG ═══════════════════════════════════════════════════════ */
  const f2 = v => (Math.round(v * 100) / 100).toString();
  function pathD(p, closed, s, tx, ty) {
    let d = ''; for (let i = 0; i < p.length; i++) d += (i ? 'L' : 'M') + f2(p[i][0] * s + tx) + ' ' + f2(p[i][1] * s + ty);
    return d + (closed ? 'Z' : '');
  }
  function render(id, opt) {
    opt = opt || {}; const W = opt.w || 302, H = opt.h || 65; const fam = FAM[id]; if (!fam) return { svg: '', meta: {} };
    const K = Kit(fam.yaw != null ? fam.yaw : -32, fam.pitch != null ? fam.pitch : 24);
    const extra = fam.build(K) || {};
    let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    const acc = q => { if (q[0] < x0) x0 = q[0]; if (q[0] > x1) x1 = q[0]; if (q[1] < y0) y0 = q[1]; if (q[1] > y1) y1 = q[1]; };
    for (const p of K.parts) { if (p.meta.nofit) continue; p.fills.forEach(f => f.forEach(acc)); p.lines.forEach(l => l.p.forEach(acc)); }
    const px = opt.padX != null ? opt.padX : (fam.padX != null ? fam.padX : 8), py = opt.padY != null ? opt.padY : (fam.padY != null ? fam.padY : 5);
    const s = Math.min((W - 2 * px) / (x1 - x0), (H - 2 * py) / (y1 - y0));
    const tx = W / 2 - (x0 + x1) / 2 * s, ty = H / 2 - (y0 + y1) / 2 * s + (fam.dy || 0);
    const S = q => [f2(q[0] * s + tx), f2(q[1] * s + ty)];
    let out = '';
    const meta = { strings: [], emit: [], scale: s };
    for (const p of K.parts) {
      const m = p.meta; let cls = 'pt' + (m.role ? ' r-' + m.role : '') + (m.cls ? ' ' + m.cls : '');
      let attrs = ` class="${cls}"` + (m.i != null ? ` data-i="${m.i}"` : '');
      if (m.anchor) { const a = S(K.cam.P(m.anchor)); attrs += ` data-x="${a[0]}" data-y="${a[1]}"`; }
      let g = '';
      if (p.fills.length && !m.nofill) g += `<path class="f" d="${p.fills.map(f => pathD(f, true, s, tx, ty)).join('')}"/>`;
      const byW = {};
      for (const l of p.lines) (byW[l.w] = byW[l.w] || []).push(pathD(l.p, l.c, s, tx, ty));
      for (const w of ['x', 'h', 'k', 'm', 'b']) if (byW[w]) g += `<path class="${w}" d="${byW[w].join('')}"/>`;
      if (m.role === 'string') {           // vibrating segment: endpoints in screen space
        const a = S(K.cam.P(m.a)), b = S(K.cam.P(m.b));
        g += `<path class="${m.w || 'm'} vib" d="M${a[0]} ${a[1]}L${b[0]} ${b[1]}"/>`;
        attrs += ` data-ax="${a[0]}" data-ay="${a[1]}" data-bx="${b[0]}" data-by="${b[1]}"`;
      }
      out += `<g${attrs}>${g}</g>`;
    }
    (extra.emit || []).forEach(e => { const a = S(K.cam.P(e.at)); const d = K.cam.P(add(e.at, e.dir || [1, 0, 0])), c0 = K.cam.P(e.at); meta.emit.push({ x: +a[0], y: +a[1], ang: Math.atan2(d[1] - c0[1], d[0] - c0[0]), r: (e.r || 4) * s }); });
    meta.map = extra.map || null; meta.fade = !!fam.fade; meta.voice = fam.voice || 'key';
    return { svg: out, meta, fit: { s, tx, ty } };
  }

  /* ═══ helpers shared by several families ═══════════════════════════════════════════════════ */
  // piano-style keyboard: nWhite white keys from x0..x1 at key-top height y, front z0, length L.
  const BLACK = [1, 1, 0, 1, 1, 1, 0];       // after white key k (C D E F G A B) is there a black key?
  function keyboard(K, o) {
    const { x0, x1, y, z0, L, nWhite, startNote } = o; const kw = (x1 - x0) / nWhite, kh = o.h || 1.8;
    const whiteNotes = [0, 2, 4, 5, 7, 9, 11];
    const note0 = startNote || 36; const oct = Math.floor(note0 / 12); let degIdx = whiteNotes.indexOf(note0 % 12); if (degIdx < 0) degIdx = 0;
    K.part({ nofill: false }, () => K.fill([[x0, y, z0 - L], [x1, y, z0 - L], [x1, y, z0], [x1, y - kh, z0], [x0, y - kh, z0], [x0, y, z0]]));
    const keys = [];
    for (let k = 0; k < nWhite; k++) {
      const d = (degIdx + k) % 7, note = (oct + Math.floor((degIdx + k) / 7)) * 12 + whiteNotes[d];
      const xa = x0 + k * kw, xb = xa + kw;
      K.part({ role: 'key', i: note, cls: 'kw' }, () => {
        K.fill([[xa, y, z0 - L], [xb, y, z0 - L], [xb, y, z0], [xa, y, z0]]);
        K.fill([[xa, y, z0], [xb, y, z0], [xb, y - kh, z0], [xa, y - kh, z0]]);
        if (k) K.line([[xa, y, z0 - L], [xa, y, z0], [xa, y - kh, z0]], 'h');
        K.line([[xa, y, z0 - L], [xb, y, z0 - L], [xb, y, z0], [xb, y - kh, z0], [xa, y - kh, z0], [xa, y, z0], [xa, y, z0 - L]], 'k', false);
      });
      keys.push({ note, k, d });
    }
    K.part({}, () => { K.line([[x0, y, z0 - L], [x1, y, z0 - L]], 'h'); K.line([[x0, y, z0], [x1, y, z0]], 'm'); K.line([[x0, y - kh, z0], [x1, y - kh, z0]], 'm'); K.line([[x0, y, z0], [x0, y - kh, z0]], 'm'); K.line([[x1, y, z0], [x1, y - kh, z0]], 'm'); });
    for (const q of keys) {
      if (!BLACK[q.d] || q.k === nWhite - 1) continue;
      const cx = x0 + (q.k + 1) * kw, bw = kw * 0.54, bl = L * 0.6;
      K.part({ role: 'key', i: q.note + 1, cls: 'kb' }, () => K.box(cx - bw / 2, y, z0 - L, cx + bw / 2, y + kh * 0.55, z0 - L + bl, { w: 'm' }));
    }
  }

  /* ═══ FAMILIES ═════════════════════════════════════════════════════════════════════════════ */
  const FAM = {};

  /* ── BOWED STRINGS (violin / viola / cello / double bass): one parametric luthier model ── */
  function bowed(p) {
    return function (K) {
      const Lb = p.body, sc = Lb / 35.6;              // everything drawn at violin proportions, then stretched
      const X = v => v * sc, Zs = p.wide || 1;
      const half = [].concat(
        K.spline([[0, 0], [0.25, 2.8], [1.6, 5.4], [3.8, 7.3], [6.5, 8.2], [9, 8.2], [11.3, 7.6], [12.6, 7.0], [13.3, 7.15]], 6),
        K.spline([[13.3, 7.15], [13.9, 6.0], [15.0, 5.35], [16.6, 5.15], [18.3, 5.35], [19.5, 6.2], [20.3, 7.95]], 5).slice(1),
        K.spline([[20.3, 7.95], [21.6, 8.7], [24, 9.9], [27.5, 10.35], [31, 9.8], [33.8, 7.6], [35.2, 4.2], [35.6, 0]], 6).slice(1)
      ).map(q => [X(q[0]), q[1] * sc * Zs]);
      const out = K.mirrorZ(half);
      const rib = p.rib;
      // body (ribs + top); the endpin first (behind)
      if (p.endpin) K.part({}, () => K.tube([[X(35.6), rib * 0.5, 0], [X(35.6) + p.endpin, rib * 0.5 - p.endpin * 0.08, 0]], 0.28 * sc * 0.9, { caps: [false, true] }));
      K.part({}, () => {
        K.prism(out, rib, K.FLAT(0), { corner: 40 });
        K.line(K.offset2(out, 0.45 * sc).map(q => [q[0], rib + 0.05, q[1]]), 'h', true);      // purfling
      });
      const top = rib + 0.4 * sc;
      // f-holes
      K.part({}, () => {
        [1, -1].forEach(s => {
          const f = K.spline([[18.6, 3.05], [19.5, 3.35], [20.6, 3.9], [22.6, 4.05], [24.6, 4.15], [25.7, 4.75], [26.3, 5.35]], 5).map(q => [X(q[0]), top, s * q[1] * sc * Zs]);
          K.line(f, 'h'); K.ring(f[0], [0, 1, 0], 0.32 * sc, 'h', 10); K.ring(f[f.length - 1], [0, 1, 0], 0.36 * sc, 'h', 10);
        });
      });
      // tailpiece + chinrest
      K.part({}, () => {
        const tp = [[X(27.2), 1.6 * sc], [X(34.6), 1.05 * sc], [X(35.0), 0], [X(34.6), -1.05 * sc], [X(27.2), -1.6 * sc], [X(26.8), 0]];
        K.prism(tp, 0.45 * sc, K.FLAT(top + 0.5 * sc), { corner: 20 });
        if (p.chin) { const cr = K.spline([[30.5, -3], [33.2, -2.6], [34.6, -4.6], [33.5, -7.2], [30.5, -7.4], [29.4, -5.4]], 5, true).map(q => [X(q[0]), q[1] * sc]); K.prism(cr, 1.0 * sc, K.FLAT(rib), { corner: 60 }); }
      });
      // bridge — stands on the top, arched crown
      const bx = X(22.6), bh = 3.3 * sc * p.bridgeK, bw = 2.1 * sc * Zs;
      K.part({}, () => {
        const pr = [[-bw, 0], [-bw * 0.92, bh * 0.45], [-bw * 0.8, bh * 0.82], [-bw * 0.4, bh * 0.97], [0, bh], [bw * 0.4, bh * 0.97], [bw * 0.8, bh * 0.82], [bw * 0.92, bh * 0.45], [bw, 0], [bw * 0.35, 0], [bw * 0.3, bh * 0.2], [-bw * 0.3, bh * 0.2], [-bw * 0.35, 0]];
        K.prism(pr, 0.35 * sc, K.Frame([bx - 0.17 * sc, top, 0], [0, 0, 1], [0, 1, 0], [1, 0, 0]), { corner: 35 });
      });
      // neck + fingerboard + pegbox + scroll
      const nut = X(-13.0) - (p.neckExtra || 0), fbEnd = X(14.5), fbY = top + bh * 0.55;
      const pegEnd = nut - 7.4 * sc, scrollC = [pegEnd - 1.8 * sc, top + 0.2 * sc, 0];
      K.part({}, () => {   // pegbox (behind the pegs on the far side) + scroll
        K.prism([[nut + 0.4 * sc, fbY - 0.1 * sc], [pegEnd, fbY - 0.9 * sc], [pegEnd, top - 3.2 * sc], [nut + 0.4 * sc, top - 1.6 * sc]], 2.1 * sc, K.SIDE(-1.05 * sc), { corner: 20 });
      });
      K.part({}, () => {   // scroll volute
        const vol = []; for (let i = 0; i <= 60; i++) { const a = i / 60 * Math.PI * 4.2, r = 2.1 * sc * (1 - i / 75); vol.push([scrollC[0] + r * Math.cos(a) * 0.95, scrollC[1] + r * Math.sin(a), 1.25 * sc]); }
        const outer = []; for (let i = 0; i < 30; i++) { const a = i / 30 * Math.PI * 2; outer.push([2.15 * sc * Math.cos(a) * 0.95, 2.15 * sc * Math.sin(a)]); }
        K.prism(outer, 2.5 * sc, K.Frame([scrollC[0], scrollC[1], -1.25 * sc], [1, 0, 0], [0, 1, 0], [0, 0, 1]));
        K.line(vol, 'h');
      });
      // pegs (4): two each side, through the pegbox
      K.part({}, () => {
        [[nut - 1.6 * sc, 1], [nut - 3.3 * sc, -1], [nut - 4.9 * sc, 1], [nut - 6.4 * sc, -1]].forEach(([x, s]) => {
          const y = top - 0.6 * sc;
          K.tube([[x, y, 0], [x, y, s * 3.0 * sc]], 0.28 * sc, { caps: [false, true] });
          K.disc([x, y, s * 3.1 * sc], [1, 0, 0.35 * s], 0.95 * sc, 'm');
        });
      });
      K.part({}, () => {   // neck under the fingerboard + heel
        K.prism([[nut, -1.05 * sc], [X(0.6), -1.3 * sc], [X(0.6), 1.3 * sc], [nut, 1.05 * sc]], (fbY - top + 0.1 * sc) - 0.2 * sc, K.FLAT(top - 1.7 * sc), { corner: 20 });
      });
      K.part({}, () => {   // fingerboard
        K.prism([[nut, -1.15 * sc * Zs], [fbEnd, -2.1 * sc * Zs], [fbEnd, 2.1 * sc * Zs], [nut, 1.15 * sc * Zs]], 0.55 * sc, K.FLAT(fbY), { corner: 20 });
      });
      // strings: tail → bridge (static) + bridge → nut (vibrating)
      const sy = fbY + 0.55 * sc + 0.3 * sc;
      [-0.75, -0.25, 0.25, 0.75].forEach((z, i) => {
        const tail = [X(27.6), top + 1.0 * sc, z * 1.6 * sc * Zs], br = [bx, top + bh - 0.05 * sc * Math.abs(z) * 2, z * 2.3 * sc * Zs], nt = [nut + 0.2 * sc, sy, z * 1.35 * sc * Zs];
        K.part({ role: 'string', i, a: br, b: nt, w: 'h' }, () => K.line([tail, br], 'h'));
      });
      if (p.bow) K.part({}, () => {
        const zB = 11 * sc * Zs + 6, x0 = -8 * sc, x1 = X(35.6) + 4, cam = 1.6 * sc;
        const stick = K.spline([[x0, 0.9, zB], [(x0 + x1) / 2, 0.9 + cam * 0.4, zB], [x1, 0.9 + cam, zB]], 10);
        K.tube(stick, 0.34 * sc * 0.6, { caps: [true, true] });
        K.line([[x0 + 5, 0.2, zB + 0.6], [x1 - 1, 0.2, zB + 0.6]], 'h');
        K.box(x0 + 1.5, -0.6, zB - 0.9, x0 + 5.4, 1.2, zB + 0.9, { w: 'm' });
        K.prism([[x1 - 2.4, 0.0], [x1 - 0.2, 0.0], [x1, 2.4], [x1 - 0.8, 2.6]].map(q => [q[0], q[1]]), 0.6, K.SIDE(zB - 0.3), { corner: 20 });
      });
      return { map: { type: 'strings', open: p.open } };
    };
  }
  FAM.violin = { name: 'Violin', cat: 'Strings', voice: 'string', yaw: -14, pitch: 30, build: bowed({ body: 35.6, rib: 3.1, chin: true, bridgeK: 1, open: [55, 62, 69, 76] }) };
  FAM.cello = { name: 'Cello', cat: 'Strings', voice: 'string', yaw: -14, pitch: 30, build: bowed({ body: 75, rib: 13, bridgeK: 1.1, endpin: 26, neckExtra: 4, bow: true, open: [36, 43, 50, 57] }) };
  FAM.bass = { name: 'Double Bass', cat: 'Strings', voice: 'string', yaw: -14, pitch: 30, build: bowed({ body: 112, rib: 20, bridgeK: 1.2, endpin: 34, neckExtra: 10, wide: 1.06, bow: true, open: [28, 33, 38, 43] }) };

  /* pose: local (x along the instrument, y up, z toward the eye) → world, for instruments laid on a slant */
  function Pose(o, U, V) { U = nrm(U); V = nrm(V); const W = cross(U, V);
    return { p: q => add(o, add(add(mul(U, q[0]), mul(V, q[1])), mul(W, q[2]))), v: q => add(add(mul(U, q[0]), mul(V, q[1])), mul(W, q[2])),
      fr(q, u, v, w) { return Frame(this.p(q), this.v(u), this.v(v), this.v(w)); }, U, V, W }; }

  /* ── ACOUSTIC GUITAR (dreadnought) lying face-up, neck to the left ── */
  FAM.guitar = { name: 'Acoustic Guitar', cat: 'Plucked', voice: 'string', yaw: -14, pitch: 30, build(K) {
    const half = K.spline([[0, 0], [0.4, 5], [2.2, 9.5], [5.5, 12.8], [10, 14.4], [15, 14], [19, 12.6], [22.5, 12.2], [26.5, 13.8], [31, 17.4], [36, 19.6], [41, 19.4], [45.5, 17], [48.8, 12], [50.3, 6], [50.6, 0]], 5);
    const out = K.mirrorZ(half), D = 10.5;
    K.part({}, () => {
      K.prism(out, D, K.FLAT(0), { corner: 50 });
      K.line(K.offset2(out, 0.5).map(q => [q[0], D + 0.02, q[1]]), 'x', true);                       // binding
      K.ring([16.5, D, 0], [0, 1, 0], 5.0, 'm', 40); K.ring([16.5, D, 0], [0, 1, 0], 5.9, 'h', 40); K.ring([16.5, D, 0], [0, 1, 0], 6.5, 'h', 40);
      const pg = K.spline([[20.5, -5.2], [24, -7.8], [28.5, -9.6], [30, -12.4], [27.5, -14.2], [22.5, -13.2], [18, -10.8], [14.5, -8.3]], 5).map(q => [q[0], D + 0.02, q[1]]);
      K.line(pg, 'h');
    });
    // bridge (with wings) + saddle + pins
    K.part({}, () => {
      const br = K.spline([[37.2, -8.5], [38.6, -7.6], [38.6, 7.6], [37.2, 8.5], [40.6, 8.3], [40.3, 0], [40.6, -8.3]], 3, true);
      K.prism([[37.3, -8.6], [37.9, -7.2], [37.9, 7.2], [37.3, 8.6], [40.5, 8.6], [40.2, 6.5], [40.2, -6.5], [40.5, -8.6]], 0.9, K.FLAT(D), { corner: 20 });
      K.line([[38.25, D + 1.5, -3.8], [38.25, D + 1.5, 3.8]], 'm');
      for (let i = 0; i < 6; i++) K.ring([39.4, D + 0.92, -3.6 + i * 1.44], [0, 1, 0], 0.28, 'h', 8);
    });
    // headstock + tuners (behind the fingerboard)
    const nut = -37;
    K.part({}, () => {
      K.prism([[nut + 0.5, -2.5], [-50, -4.1], [-55.8, -3.6], [-56.3, 0], [-55.8, 3.6], [-50, 4.1], [nut + 0.5, 2.5]], 1.4, K.FLAT(D - 0.2), { corner: 20 });
      [-41.5, -45.8, -50.1].forEach(x => [1, -1].forEach(s => {
        K.ring([x, D + 1.2, s * 2.5], [0, 1, 0], 0.45, 'h', 10);
      }));
    });
    K.part({}, () => {
      [-41.5, -45.8, -50.1].forEach(x => [1, -1].forEach(s => {
        const edge = s * (2.9 + (-(x - nut)) * 0.083);
        K.tube([[x, D + 0.5, edge], [x, D + 0.5, edge + s * 2.2]], 0.35, { caps: [false, false] });
        K.disc([x, D + 0.5, edge + s * 2.9], [0, 0.2, s], 1.0, 'm');
      }));
    });
    // neck + fingerboard + frets
    const fb = D + 0.8;
    K.part({}, () => K.prism([[nut, -2.3], [0.5, -2.9], [0.5, 2.9], [nut, 2.3]], 2.2, K.FLAT(D - 1.6), { corner: 20 }));
    K.part({}, () => {
      K.prism([[nut, -2.35], [10.6, -2.95], [10.6, 2.95], [nut, 2.35]], 0.6, K.FLAT(D + 0.2), { corner: 20 });
      K.box(nut - 0.5, D + 0.2, -2.35, nut, fb + 0.35, 2.35, { w: 'h' });
      for (let n = 1; n <= 20; n++) { const x = nut + 64.5 * (1 - Math.pow(2, -n / 12)); if (x > 10.3) break; const w = 2.35 + (x - nut) / (10.6 - nut) * 0.6; K.line([[x, fb + 0.02, -w], [x, fb + 0.02, w]], 'h'); }
    });
    // strings: pins → saddle (static), saddle → nut (ringing)
    for (let i = 0; i < 6; i++) {
      const z = -3.6 + i * 1.44, zs = -3.4 + i * 1.36, zn = -1.85 + i * 0.74;
      const sad = [38.25, D + 1.55, zs], nt = [nut - 0.2, fb + 0.4, zn];
      K.part({ role: 'string', i, a: sad, b: nt, w: 'h' }, () => K.line([[39.4, D + 0.95, z], sad], 'h'));
    }
    return { map: { type: 'strings', open: [40, 45, 50, 55, 59, 64] } };
  } };

  /* ── GRAND PIANO — lid up, seen from the audience's curved side ── */
  function grand(p) { return function (K) {
    const W = p.w, Lr = p.len;
    const bent = K.spline(p.bent, 6);
    const outline = [[0, 0], [W, 0]].concat(bent).concat([[0, -Lr + 12]]);
    const y0 = p.legH, y1 = y0 + p.rim;
    // legs + lyre (under the case, drawn first)
    const leg = (x, z) => K.part({ nofit: p.cropLegs }, () => { K.tube([[x, y0, z], [x, 3, z]], t => p.legR * (1 - 0.3 * t), { caps: [false, true] }); if (p.caster) K.ring([x, 2, z], [0, 1, 0], p.legR * 0.8, 'h', 12); });
    p.legs.forEach(q => leg(q[0], q[1]));
    if (p.lyre) K.part({ nofit: p.cropLegs }, () => {
      const lx = W * 0.5;
      K.tube([[lx - 5, y0, -24], [lx - 5, 8, -24]], 1.0, { caps: [false, false] }); K.tube([[lx + 5, y0, -24], [lx + 5, 8, -24]], 1.0, { caps: [false, false] });
      K.box(lx - 12, 3, -28, lx + 12, 8.5, -18);
      [-6, 0, 6].forEach(dx => K.box(lx + dx - 1.2, 4.4, -18, lx + dx + 1.2, 5.6, -12, { w: 'm' }));
    });
    // the case: rim, soundboard/plate/strings on top
    K.part({}, () => {
      K.prism(outline, p.rim, K.FLAT(y0), { corner: 40 });
      const inner = K.offset2(outline.slice().reverse(), -p.rimT); // CW→ offset
      K.line(K.offset2(outline, p.rimT).map(q => [q[0], y1, q[1]]), 'h', true);
      // strings — bass (left) longest, run from the front to just inside the bentside
      const back = x => { let best = -Lr; for (let i = 0; i < bent.length - 1; i++) { const a = bent[i], b = bent[i + 1]; if ((a[0] - x) * (b[0] - x) <= 0 && a[0] !== b[0]) { const t = (x - a[0]) / (b[0] - a[0]); best = a[1] + (b[1] - a[1]) * t; } } return best; };
      K.__back = back;
      if (p.plate) K.line(K.spline([[6, -16], [W - 8, -16], [W - 12, -40], [W * 0.68, -68], [W * 0.46, -104], [W * 0.36, -140], [W * 0.2, -164], [8, -150]], 5).map(q => [q[0], y1 - 0.3, q[1]]), 'h');
    });
    for (let i = 0; i < p.strings; i++) {
      const x = 8 + i * (W - 16) / (p.strings - 1); const zb = Math.max(K.__back(x), -Lr + 16) + 9;
      K.part({ role: 'string', i, a: [x, y1 - 0.5, -14], b: [x + (i < p.strings * 0.3 ? 6 : 0), y1 - 0.5, zb], w: 'x' }, () => {});
    }
    // lid: hinged on the bass (left) side, treble side lifted
    const a = p.lid * Math.PI / 180, lidY = y1 + 0.3;
    const lidOut = outline.filter(q => q[1] <= -p.flap).concat([]); lidOut.unshift([W, -p.flap]); lidOut.unshift([0, -p.flap]);
    const lidPt = (x, z) => [x * Math.cos(a), lidY + x * Math.sin(a), z];
    if (p.lid > 0) {
      K.part({}, () => K.prism(lidOut.map(q => [q[0], q[1]]), 1.6, K.Frame([0, lidY, 0], [Math.cos(a), Math.sin(a), 0], [0, 0, 1], [-Math.sin(a), Math.cos(a), 0]), { corner: 40 }));
      K.part({}, () => { const px = W * 0.72, pz = -p.len * 0.42; K.tube([[px, y1, pz], lidPt(px * 0.98, pz)], 0.7, { caps: [false, false] }); });
    }
    // music desk
    if (p.desk) K.part({}, () => K.prism([[W * 0.26, y1], [W * 0.74, y1], [W * 0.72, y1 + p.desk], [W * 0.28, y1 + p.desk]], 1.2, K.Frame([0, 0, -7], [1, 0, 0], [0, 1, -0.25], [0, 0, 1]), { corner: 20 }));
    // keybed, cheeks, keys
    const kz = p.keyZ, ky = y0 + p.keyY;
    K.part({}, () => K.box(0, y0, 0, W, ky - 2.2, kz + 2));
    K.part({}, () => { K.box(0, y0, 0, p.cheek, ky + 4, kz + 2); });
    keyboard(K, { x0: p.cheek + 0.5, x1: W - p.cheek - 0.5, y: ky, z0: kz, L: 14, nWhite: p.keys, startNote: p.lowNote || 36, h: 2.2 });
    K.part({}, () => { K.box(W - p.cheek, y0, 0, W, ky + 4, kz + 2); });
    if (p.manual2) keyboard(K, { x0: p.cheek + 0.5, x1: W - p.cheek - 0.5, y: ky + 6, z0: kz - 13, L: 12, nWhite: p.keys, startNote: (p.lowNote || 36) + 12, h: 2 });
    return { map: { type: 'keys', strings: p.strings, lo: p.lowNote || 36, span: p.keys * 12 / 7 } };
  }; }
  const GRAND_P = ({
    w: 150, len: 196, legH: 62, rim: 30, rimT: 4, lid: 34, flap: 34, keyZ: 16, keyY: 13, cheek: 7, keys: 36, strings: 30, plate: true, desk: 24, lyre: true, legR: 3.4, caster: true,
    legs: [[9, -12], [141, -14], [38, -176]], cropLegs: true,
    bent: [[150, -40], [139, -78], [112, -108], [94, -136], [85, -162], [74, -183], [55, -195], [30, -197], [10, -191], [0, -181]] });
  FAM.grand = { name: 'Grand Piano', cat: 'Keys', voice: 'key', yaw: -66, pitch: 18, padY: 3, fade: true, build: grand(Object.assign({}, GRAND_P, { lid: 30, desk: 16 })) };

  /* ── FLUTE — laid on a slight slant ── */
  FAM.flute = { name: 'Flute', cat: 'Woodwind', voice: 'wind', yaw: -10, pitch: 26, padY: 12, build(K) {
    const P = Pose([0, 0, 0], [1, 0, -0.5], [0, 1, 0]), r = 0.95;
    const up = nrm([0, 1, 0.55]);
    K.part({}, () => K.tube([P.p([-2.2, 0, 0]), P.p([0.2, 0, 0])], 1.12, { caps: [true, false] }));        // crown
    K.part({}, () => { K.tube([P.p([0, 0, 0]), P.p([20.6, 0, 0])], r, { caps: [false, false] });
      K.disc(P.p([5.4, 0.9, 0.2]), P.v([0, 1, 0.1]), 1.25, 'm'); K.ring(P.p([5.4, 1.02, 0.25]), P.v([0, 1, 0.1]), 0.45, 'h', 14); });
    K.part({}, () => { K.tube([P.p([20.2, 0, 0]), P.p([60, 0, 0])], r, { caps: [false, false] }); K.ring(P.p([20.6, 0, 0]), P.U, 1.08, 'h', 24); });
    K.part({}, () => { K.tube([P.p([59.6, 0, 0]), P.p([67, 0, 0])], r, { caps: [false, true] }); K.ring(P.p([60, 0, 0]), P.U, 1.08, 'h', 24); });
    // mechanism rod on the near side + key cups on top
    K.part({}, () => { const ro = [0, 0.55, 1.15]; K.tube([P.p([25.5, ro[1], ro[2]]), P.p([58.5, ro[1], ro[2]])], 0.16, { caps: [true, true] }); K.tube([P.p([61, ro[1], ro[2]]), P.p([66, ro[1], ro[2]])], 0.16, { caps: [true, true] }); });
    [27.2, 30.6, 33.8, 37.4, 40.8, 44, 47.4, 50.8, 54, 57.2, 62.4, 65.2].forEach((x, i) => {
      K.part({ role: 'pad', i }, () => { const c = P.p([x, 0, 0]), n = P.v(up); const rr = i > 9 ? 0.8 : 0.72;
        K.line([P.p([x, 0.55, 1.15]), add(c, mul(n, r * 0.95))], 'h');
        K.tube([add(c, mul(n, r * 0.8)), add(c, mul(n, r + 0.3))], rr, { caps: [false, true], inner: [0, i < 5 && i % 2 === 0 ? 0.35 : 0] }); });
    });
    return { emit: [{ at: P.p([5.4, 1.3, 0]), dir: [0, 1, 0], r: 2 }, { at: P.p([67.3, 0, 0]), dir: P.U, r: 1.5 }], map: { type: 'pads', n: 12 } };
  } };

  /* ── CLARINET ── */
  FAM.clarinet = { name: 'Clarinet', cat: 'Woodwind', voice: 'wind', yaw: -10, pitch: 26, padY: 10, build(K) {
    const P = Pose([0, 0, 0], [1, 0, -0.5], [0, 1, 0]);
    const up = nrm([0, 1, 0.6]);
    const seg = (x0, x1, r0, r1, o) => K.part({}, () => K.tube([P.p([x0, 0, 0]), P.p([(x0 + x1) / 2, 0, 0]), P.p([x1, 0, 0])], t => lerp(r0, r1, t), o || { caps: [false, false] }));
    // mouthpiece (beak) + ligature
    K.part({}, () => { K.tube([P.p([-0.6, -0.45, 0]), P.p([2, -0.1, 0]), P.p([7.6, 0, 0])], t => 0.55 + 0.75 * Math.sqrt(t), { caps: [true, false] });
      K.line([P.p([-0.4, -0.95, 0.05]), P.p([6.2, -1.3, 0.2])], 'h'); });
    K.part({}, () => { K.tube([P.p([3.4, 0, 0]), P.p([5.4, 0, 0])], 1.42, { caps: [true, true] }); [3.9, 4.9].forEach(x => K.ring(P.p([x, 0, 0]), P.U, 1.46, 'h', 20)); });
    seg(7.4, 14.2, 1.5, 1.52, { caps: [true, false] });                                                     // barrel
    K.part({}, () => { K.tube([P.p([14.0, 0, 0]), P.p([14.9, 0, 0])], 1.62, { caps: [true, true] }); });     // ring
    seg(14.8, 37.2, 1.38, 1.38);
    K.part({}, () => { K.tube([P.p([36.9, 0, 0]), P.p([38.1, 0, 0])], 1.6, { caps: [true, true] }); });
    seg(38.0, 59.6, 1.38, 1.42);
    K.part({}, () => { K.tube([P.p([59.4, 0, 0]), P.p([60.4, 0, 0])], 1.66, { caps: [true, true] }); });
    K.part({}, () => K.tube(Array.from({ length: 12 }, (_, i) => P.p([60.3 + i, 0, 0])), t => 1.5 + 2.1 * Math.pow(t, 2.6), { caps: [false, true], open: [false, true], inner: [0, 0.8] }));
    // ring keys, register key, long levers
    K.part({}, () => { K.line([P.p([17, 0.8, 1.2]), P.p([35.5, 0.8, 1.2])], 'h'); K.line([P.p([39.5, 0.8, 1.2]), P.p([58.5, 0.8, 1.2])], 'h');
      K.line([P.p([44, 1.2, 0.9]), P.p([57.6, 1.25, 0.9])], 'h'); });
    [19.5, 23, 26.5, 30, 33.2, 41, 44.6, 48.2, 51.8, 55].forEach((x, i) => K.part({ role: 'pad', i }, () => {
      const c = add(P.p([x, 0, 0]), mul(P.v(up), 1.35));
      if (i === 4 || i === 9) K.tube([c, add(c, mul(P.v(up), 0.3))], 0.85, { caps: [false, true] });
      else { K.ring(c, P.v(up), 0.6, 'm', 16); K.ring(c, P.v(up), 0.28, 'h', 10); }
    }));
    return { emit: [{ at: P.p([72.5, 0, 0]), dir: P.U, r: 3.4 }], map: { type: 'pads', n: 10 } };
  } };

  /* ── ALTO SAX — lying on its side, bell up ── */
  FAM.sax = { name: 'Alto Sax', cat: 'Woodwind', voice: 'wind', yaw: -16, pitch: 22, padY: 4, build(K) {
    // built standing (y up), then turned so the body runs left→right: (x,y,z) → (-y, x, z)
    const RA = 0 * D2R, T = q => { const x = -q[1], y = q[0], z = q[2]; return [x, y * Math.cos(RA) - z * Math.sin(RA), y * Math.sin(RA) + z * Math.cos(RA)]; };
    const bodyPath = K.spline([[0, 0, 0], [0.2, -12, 0], [0.8, -26, 0], [1.4, -38, 0]], 6);
    const bow = []; for (let i = 0; i <= 14; i++) { const a = Math.PI + i / 14 * Math.PI; bow.push([5.3 + 4.0 * Math.cos(a), -38 + 4.6 * Math.sin(a), 0]); }
    const bell = K.spline([[9.3, -38, 0], [9.6, -31, 0], [10.0, -25, 0.4], [11.2, -19.5, 2.6]], 5);
    const neck = K.spline([[0, 0, 0], [-0.2, 3.5, 0], [1.4, 7.4, 0], [4.6, 9.6, 0], [8.4, 10.4, 0]], 4);
    const mp = [[8.3, 10.4, 0], [10.5, 10.7, 0], [13.6, 11.0, 0]];
    // far side keys first? — bell section is in front in standing pose; after the turn it sits above
    K.part({}, () => K.tube(bell.map(T), t => 3.3 + 0.5 * t + 2.6 * Math.pow(t, 4), { caps: [false, true], open: [false, true], inner: [0, 0.86] }));
    K.part({}, () => K.tube(bow.map(T), t => 3.2 + 0.1 * t, { caps: [false, false] }));
    K.part({}, () => { K.tube(bodyPath.map(T), t => 1.35 + 1.85 * t, { caps: [false, false] });
      [[-2, 0.9], [-5, 1.1], [-33, 3.2]].forEach(([y, r]) => K.ring(T([0.05 + (-y) / 38 * 1.4, y, 0]), T([0.04, -1, 0]), r + 0.12, 'h', 24)); });
    K.part({}, () => { K.tube(neck.map(T), t => 1.3 - 0.45 * t, { caps: [true, false] }); });
    K.part({}, () => { K.tube(mp.map(T), t => 0.85 + 0.25 * Math.sin(t * Math.PI), { caps: [false, true] }); K.ring(T([10.2, 10.7, 0]), T([1, 0.1, 0]), 1.1, 'h', 16); });
    // key cups on the near (+z) side of the body, growing toward the bow; rods alongside
    const cups = [[-6, 0.95], [-9.5, 1.1], [-13, 1.25], [-16.5, 1.4], [-20, 1.55], [-23.5, 1.7], [-27, 1.85], [-30.5, 2.0], [-34, 2.1]];
    K.part({}, () => { K.line(cups.map(([y]) => T([-0.2, y, 1.35 + (-y) / 38 * 1.85 + 0.9])), 'h'); });
    cups.forEach(([y, r], i) => K.part({ role: 'pad', i }, () => {
      const R = 1.35 + (-y) / 38 * 1.85, x = (-y) / 38 * 1.4 + 1.0;
      K.tube([T([x, y, R * 0.7]), T([x, y, R + 0.55])], r, { caps: [false, true] });
      if (i === 1 || i === 3 || i === 5 || i === 6) K.ring(T([x, y, R + 0.56]), [0, -Math.sin(RA), Math.cos(RA)], r * 0.45, 'h', 12);
    }));
    // low Bb/B cups on the bell (near side), guard
    [[9.6, -34, 1.5], [9.9, -29.5, 1.35]].forEach(([x, y, r], i) => K.part({ role: 'pad', i: 9 + i }, () => K.tube([T([x, y, 2.9]), T([x, y, 4.1])], r, { caps: [false, true] })));
    K.part({}, () => K.line(K.spline([[6.6, -36.5, 4.6], [7.2, -31, 4.9], [8.0, -26.5, 4.5]], 4).map(T), 'h'));
    return { emit: [{ at: T([11.2, -19.5, 2.6]), dir: T([0.2, 1, 0.3]), r: 5.9 }], map: { type: 'pads', n: 11 } };
  } };

  /* ── TRUMPET — side on, bell right ── */
  FAM.trumpet = { name: 'Trumpet', cat: 'Brass', voice: 'brass', yaw: -18, pitch: 20, padY: 5, build(K) {
    const lp = K.spline([[-1.5, 6.4, -1.7], [14, 6.0, -1.7], [33.5, 5.4, -1.7]], 6);
    const crook = []; for (let i = 0; i <= 10; i++) { const a = Math.PI / 2 - i / 10 * Math.PI; crook.push([33.5 + 1.6 * Math.cos(a), 3.8 + 1.6 * Math.sin(a), -1.7]); }
    K.part({}, () => { K.tube(lp.concat(crook.slice(1)).concat([[26, 2.2, -1.7]]), 0.5, { caps: [false, false] }); });
    // 3rd valve slide (near side, low)
    K.part({}, () => { const u = []; for (let i = 0; i <= 8; i++) { const a = Math.PI / 2 - i / 8 * Math.PI; u.push([30.5 + 1.0 * Math.cos(a), 0.2 + 1.0 * Math.sin(a), 0.9]); }
      K.tube([[25.2, 1.2, 0.9]].concat(u).concat([[25.2, -0.8, 0.9]]), 0.42, { caps: [false, false] }); K.ring([29.2, 1.2, 0.9], [1, 0, 0], 0.7, 'h', 12); });
    // bell section: bell → straight → bow → back to valve 1
    const bow = []; for (let i = 0; i <= 14; i++) { const a = Math.PI / 2 + i / 14 * Math.PI; bow.push([4 + 4 * Math.cos(a), 4 + 4 * Math.sin(a), 0]); }
    K.part({}, () => K.tube([[16, 8, 0], [10, 8, 0]].concat(bow).concat([[10, 0, 0], [16.4, 0, 0]]), 0.55, { caps: [false, false] }));
    // valves
    [17.6, 21.2, 24.8].forEach((x, i) => K.part({ role: 'valve', i, cls: 'valve' }, () => {
      K.tube([[x, -2.6, 0], [x, 10.3, 0]], 0.95, { caps: [true, false] });
      K.tube([[x, 10.2, 0], [x, 11.1, 0]], 1.08, { caps: [false, true] });
      K.tube([[x, 11.0, 0], [x, 13.1, 0]], 0.22, { caps: [false, false] });
      K.tube([[x, 13.0, 0], [x, 13.7, 0]], 0.95, { caps: [false, true] });
      K.ring([x, -1.2, 0], [0, 1, 0], 1.02, 'h', 16); K.ring([x, 8.8, 0], [0, 1, 0], 1.02, 'h', 16);
    }));
    K.part({}, () => { K.tube(Array.from({ length: 22 }, (_, i) => [26.2 + i * 1.12, 8, 0]), t => 0.58 + 5.6 * Math.pow(t, 5.5), { caps: [false, true], open: [false, true], inner: [0, 0.86] });
      K.ring([35, 8, 0], [1, 0, 0], 0.72, 'h', 14); });
    // mouthpiece
    K.part({}, () => { K.tube([[-1.4, 6.4, -1.7], [-6.8, 6.6, -1.7]], t => 0.5 + 0.25 * t, { caps: [false, false] }); K.tube([[-6.7, 6.6, -1.7], [-8.4, 6.65, -1.7]], t => 0.8 + 0.55 * t, { caps: [false, true], open: [false, true] }); });
    return { emit: [{ at: [50.8, 8, 0], dir: [1, 0, 0], r: 6.2 }], map: { type: 'valves' } };
  } };

  /* ── FRENCH HORN — coil facing the eye, bell sweeping right ── */
  FAM.horn = { name: 'French Horn', cat: 'Brass', voice: 'brass', yaw: -12, pitch: 18, padY: 4, build(K) {
    const circ = (cx, cy, cz, R, a0, a1) => { const o = []; const n = 72; for (let i = 0; i <= n; i++) { const a = (a0 + (a1 - a0) * i / n) * D2R; o.push([cx + R * Math.cos(a), cy + R * Math.sin(a), cz]); } return o; };
    // bell tail (behind the loops at the bottom right)
    const bell = K.spline([[1, -12.4, -1.2], [8.5, -12.8, -1.4], [15.5, -12.2, -3.0], [21, -11.4, -5.8], [25, -11.0, -8.4]], 6);
    K.part({}, () => K.tube(bell, t => 0.7 + 0.8 * t + 10.2 * Math.pow(t, 5), { caps: [false, true], open: [false, true], inner: [0, 0.9] }));
    K.part({}, () => K.tube(circ(0, 0, -1.2, 12.3, -80, 262), 0.62, { caps: [false, false] }));
    K.part({}, () => K.tube(circ(0.5, 0.3, 0.2, 10.8, -60, 280), 0.62, { caps: [false, false] }));
    K.part({}, () => K.tube(circ(1.0, 0.6, 1.6, 9.3, -40, 300), 0.6, { caps: [false, false] }));
    // valve cluster + levers
    [[2.4, 3.2], [5.2, 2.2], [7.6, 0.5], [9.3, -1.9]].forEach(([x, y], i) => K.part({ role: 'valve', i, cls: 'valve' }, () => {
      K.tube([[x, y, 1.2], [x, y, 4.2]], 1.35, { caps: [false, true] }); K.ring([x, y, 4.21], [0, 0, 1], 0.55, 'h', 12);
    }));
    K.part({}, () => { [[2.4, 3.2], [5.2, 2.2], [7.6, 0.5]].forEach(([x, y], i) => K.line([[x, y, 4.2], [-3.5 + i * 0.4, -3.5 - i * 1.6, 4.4], [-5.8 + i * 0.4, -4.1 - i * 1.6, 4.4]], 'h')); });
    K.part({}, () => { K.tube(K.spline([[-3, 7.8, 3.6], [-9, 9.6, 3.8], [-14.5, 11.2, 4.0]], 5), 0.45, { caps: [false, false] });
      K.tube([[-14.4, 11.2, 4.0], [-18.3, 12.3, 4.1]], t => 0.45 + 0.2 * t, { caps: [false, false] }); K.tube([[-18.2, 12.3, 4.1], [-19.6, 12.7, 4.1]], t => 0.7 + 0.4 * t, { caps: [false, true], open: [false, true] }); });
    return { emit: [{ at: [25, -11, -8.4], dir: [1, 0.1, -0.5], r: 11 }], map: { type: 'valves' } };
  } };

  /* ── ELECTRIC PIANO (Rhodes Stage 73) ── */
  FAM.rhodes = { name: 'Electric Piano', cat: 'Keys', voice: 'key', yaw: -20, pitch: 24, padY: 4, fade: true, build(K) {
    const W = 111, D = 56;
    [[7, -D + 7, -5, -6], [W - 7, -D + 7, 5, -6]].forEach(([x, z, dx, dz]) => K.part({ nofit: true }, () => K.tube([[x, 0, z], [x + dx, -62, z + dz]], 1.5, { caps: [false, true] })));
    K.part({ nofit: true }, () => { K.box(W * 0.46, -63, 10, W * 0.46 + 9, -60, 24); K.box(W * 0.46 + 2, -60, 12, W * 0.46 + 7, -58.6, 22, { w: 'h' }); });
    K.part({ nofit: true }, () => K.line(K.spline([[W * 0.46 + 4.5, -58.6, 12], [W * 0.5, -40, 2], [W * 0.5, -3, -2]], 6), 'h'));
    [[7, -7, -5, 6], [W - 7, -7, 5, 6]].forEach(([x, z, dx, dz]) => K.part({ nofit: true }, () => K.tube([[x, 0, z], [x + dx, -62, z + dz]], 1.5, { caps: [false, true] })));
    K.part({}, () => { K.box(0, 0, -D, W, 13, 3); });                                   // lower case
    K.part({}, () => {                                                                   // harp cover: rounded front
      const prof = [[-14, 13], [-14, 18.5], [-15.2, 21.6], [-17.8, 23.4], [-21, 24], [-D + 1.5, 24], [-D, 22.5], [-D, 13]];
      K.prism(prof.map(q => [q[0], q[1]]), W - 2, K.Frame([1, 0, 0], [0, 0, 1], [0, 1, 0], [1, 0, 0]), { corner: 30 });
      K.line([[W / 2 - 13, 22.6, -16.4], [W / 2 + 13, 22.6, -16.4], [W / 2 + 13, 19.4, -14.3], [W / 2 - 13, 19.4, -14.3]], 'h', true);      // name rail
      K.line([[4, 24.02, -26], [W - 4, 24.02, -26]], 'x');
    });
    K.part({}, () => { K.box(0, 0, -14, 6, 17, 3); });                                    // cheeks
    keyboard(K, { x0: 6.3, x1: W - 6.3, y: 13.6, z0: 2.4, L: 15, nWhite: 43, startNote: 28, h: 2.2 });
    K.part({}, () => { K.box(W - 6, 0, -14, W, 17, 3); [[1.6, 9], [1.6, 12.8]].forEach(([x, y]) => K.ring([x + 1.4, y + 2, 3.01], [0, 0, 1], 1.0, 'h', 14)); });
    return { map: { type: 'keys' } };
  } };

  /* ── CLAVINET — lid off: strings, pickups and the yarn damper visible ── */
  FAM.clav = { name: 'Clavinet', cat: 'Keys', voice: 'key', yaw: -22, pitch: 30, padY: 6, build(K) {
    const W = 92, D = 40;
    K.part({}, () => {
      K.box(0, 0, -D, W, 9, 2.5);
      
      K.line([[8, 9.02, -D + 3.5], [W - 5, 9.02, -D + 3.5]], 'h');
    });
    for (let i = 0; i < 30; i++) { const x = 9 + i * 2.6; K.part({ role: 'string', i, a: [x, 9.02, -14], b: [x + 1.2, 9.02, -D + 2.5], w: 'x' }, () => {}); }
    K.part({}, () => K.box(8, 9, -24, W - 6, 10.1, -22.4, { w: 'm' }));                    // pickup bars
    K.part({}, () => K.box(8, 9, -30.5, W - 6, 10.1, -28.9, { w: 'm' }));
    K.part({}, () => { K.line([[8, 9.3, -16.5], [W - 6, 9.3, -20]], 'h'); K.line([[8, 9.3, -17.4], [W - 6, 9.3, -20.9]], 'h'); });   // yarn
    K.part({}, () => K.box(0, 0, -13, 7.5, 11.2, 2.5));                                    // control cheek
    K.part({}, () => { [1.2, 3.1, 5.0].forEach(z => K.box(1.6, 11.2, -12 + (z - 1.2) * 0 + z - 1, 5.8, 11.9, -12 + z + 0.2, { w: 'h' })); });
    keyboard(K, { x0: 8, x1: W - 1.2, y: 9.6, z0: 2.3, L: 13, nWhite: 35, startNote: 29, h: 2.0 });
    return { map: { type: 'keys', strings: 30, lo: 29, span: 60 } };
  } };

  /* ── CHOIR — two staggered rows of singers ── */
  FAM.choir = { name: 'Choir', cat: 'Voices', voice: 'voice', yaw: -8, pitch: 14, padY: 3, build(K) {
    const prof = [[0, 4.2], [6, 4.7], [10.5, 5.25], [12.6, 5.45], [13.7, 4.9], [14.5, 3.6], [15.1, 2.1], [15.5, 1.5], [16.6, 1.45]];
    const rAt = y => { for (let i = 0; i < prof.length - 1; i++) if (y <= prof[i + 1][0]) { const t = (y - prof[i][0]) / (prof[i + 1][0] - prof[i][0]); const e = t * t * (3 - 2 * t); return prof[i][1] + (prof[i + 1][1] - prof[i][1]) * e; } return 1.45; };
    const singer = (x, y, z, k, idx) => {
      K.part({}, () => {
        const path = []; for (let i = 0; i <= 30; i++) path.push([x, y + 16.6 * k * i / 30, z]);
        K.tube(path, t => rAt(t * 16.6) * k, { caps: [true, false] });
        const f = K.nrm([0.25, 0, 1]);
        const at = (yy, dx) => K.add([x + dx * k, y + yy * k, z], K.mul(f, rAt(yy) * k * 0.97));
        K.line([at(14.1, -2.3), at(11.4, 0), at(14.1, 2.3)], 'h');
      });
      K.part({ role: 'voice', i: idx }, () => {
        K.sphere([x, y + 19.7 * k, z], 3.1 * k);
        K.part({ role: 'mouth', i: idx, anchor: K.add([x, y + 18.6 * k, z], K.mul(K.nrm([0.22, -0.05, 1]), 3.0 * k)), nofit: true }, () => {});
      });
    };
    let v = 0;
    for (let i = 0; i < 9; i++) singer(-46 + i * 11.5, 5.2, -12, 0.98, v++);
    for (let i = 0; i < 8; i++) singer(-40.25 + i * 11.5, 0, 0, 1.03, v++);
    return { map: { type: 'voices', n: 17 } };
  } };

  /* ── PIPE ORGAN — a facade of towers and flats ── */
  FAM.organ = { name: 'Pipe Organ', cat: 'Organ', voice: 'pipe', yaw: -12, pitch: 16, padY: 3, build(K) {
    const Wd = 66, base = 19;
    K.part({}, () => { K.box(-Wd, 0, -16, Wd, base - 3, 0); K.line([[-Wd + 3, 3, 0.01], [Wd - 3, 3, 0.01]], 'x'); });
    K.part({}, () => K.box(-Wd - 2, base - 3, -17.5, Wd + 2, base, 1.6));
    const pipes = [];
    const pipe = (x, z, r, H) => pipes.push({ x, z, r, H });
    const tower = (cx, cz, n, R, r, H0, dH) => { for (let i = 0; i < n; i++) { const a = Math.PI * (0.15 + 0.7 * i / (n - 1)); pipe(cx - R * Math.cos(a), cz + R * Math.sin(a) - R * 0.4, r, H0 - Math.abs(i - (n - 1) / 2) * dH); } };
    tower(0, -4, 5, 5.5, 1.75, 36, 1.6);
    [-1, 1].forEach(s => { for (let i = 0; i < 7; i++) pipe(s * (10.5 + i * 3.2), -9, 1.25, 29 - i * 2.4); });
    [-1, 1].forEach(s => tower(s * 40.5, -4, 3, 3.6, 1.6, 30, 1.2));
    [-1, 1].forEach(s => { for (let i = 0; i < 6; i++) pipe(s * (47.5 + i * 3.0), -9, 1.15, 22 - i * 1.9); });
    pipes.sort((a, b) => a.z - b.z || Math.abs(b.x) - Math.abs(a.x));
    const byH = pipes.slice().sort((a, b) => b.H - a.H); pipes.forEach(p => p.i = byH.indexOf(p));
    pipes.forEach(p => K.part({ role: 'pipe', i: p.i }, () => {
      const y0 = base, yf = base + 3.0, yt = base + p.H;
      K.tube([[p.x, y0, p.z], [p.x, yf, p.z]], t => p.r * (0.35 + 0.65 * t * t), { caps: [false, false] });
      K.tube([[p.x, yf, p.z], [p.x, yt, p.z]], p.r, { caps: [false, true] });
      const f = K.nrm([0.2, 0, 1]), side = K.nrm([1, 0, -0.2]);
      const m0 = K.add([p.x, yf + 0.2, p.z], K.mul(f, p.r)), mw = p.r * 0.55;
      K.line([K.add(m0, K.mul(side, -mw)), K.add(m0, [0, 1.1 * p.r, 0]), K.add(m0, K.mul(side, mw))], 'h');
    }));
    K.part({}, () => { K.box(-17, 0, 2, 17, 13, 13); K.box(-17, 13, 2, -14, 19, 13); K.box(14, 13, 2, 17, 19, 13);
      K.prism([[-14, 13], [14, 13], [14, 22], [-14, 22]], 1.0, K.Frame([0, 0, 2.2], [1, 0, 0], [0, 1, 0], [0, 0, 1]), {});
      [-16.2, 15.6].forEach(x => { for (let r = 0; r < 3; r++) K.ring([x + 0.3, 15.2 + r * 1.3, 13.01 - 0], [0, 0, 1], 0.45, 'h', 8); }); });
    keyboard(K, { x0: -13.8, x1: 13.8, y: 16.8, z0: 8.6, L: 5.4, nWhite: 17, startNote: 60, h: 1.0 });
    keyboard(K, { x0: -13.8, x1: 13.8, y: 14.6, z0: 12.8, L: 5.6, nWhite: 17, startNote: 48, h: 1.0 });
    return { map: { type: 'pipes', n: pipes.length } };
  } };

  /* ── HARP (lever harp, side-on) ── */
  FAM.harp = { name: 'Harp', cat: 'Plucked', voice: 'string', yaw: -14, pitch: 12, padY: 3, build(K) {
    const sb0 = [14, 9], sb1 = [70, 88];
    const neck = K.spline([[4, 106], [16, 110], [30, 104], [42, 95], [55, 94], [66, 92], [75, 88]], 6);
    const neckY = x => { for (let i = 0; i < neck.length - 1; i++) if ((neck[i][0] - x) * (neck[i + 1][0] - x) <= 0) { const t = (x - neck[i][0]) / ((neck[i + 1][0] - neck[i][0]) || 1); return neck[i][1] + t * (neck[i + 1][1] - neck[i][1]); } return 90; };
    const sbY = x => sb0[1] + (x - sb0[0]) * (sb1[1] - sb0[1]) / (sb1[0] - sb0[0]);
    K.part({}, () => {                                                                       // sound box: tapered, diagonal, with a rounded back
      const d = K.nrm([sb1[0] - sb0[0], sb1[1] - sb0[1], 0]), n = [-d[1], d[0], 0], Lb = Math.hypot(sb1[0] - sb0[0], sb1[1] - sb0[1]) + 4;
      K.prism([[0, -9], [0, 9], [Lb, 4.2], [Lb, -4.2]], -8, K.Frame([sb0[0] + 1, sb0[1] - 1.5, 0], d, [0, 0, 1], n), { corner: 20 });
      const o = [sb0[0] + 1, sb0[1] - 1.5, 0]; K.line([K.add(o, K.mul(d, 6)), K.add(o, K.mul(d, Lb - 3))], 'h');
      for (let i = 0; i < 4; i++) K.ring(K.add(K.add(o, K.mul(d, 14 + i * 16)), K.mul(n, -8.01)), n, 1.6 - i * 0.25, 'h', 14);
    });
    K.part({}, () => K.box(6, 0, -10, 28, 4, 10));
    for (let i = 0; i < 22; i++) { const x = 16 + i * 2.6; const yb = sbY(x) + 1.6, yt = neckY(x) - 2.6;
      K.part({ role: 'string', i, a: [x, yt, 0], b: [x, yb, 0], w: i % 7 === 3 ? 'm' : 'h' }, () => {}); }
    K.part({}, () => { K.tube(neck.map(q => [q[0], q[1], 0]), t => 2.7 - 0.9 * t, { caps: [false, true] });
      for (let i = 0; i < 22; i += 1) { const x = 16 + i * 2.6; K.ring([x, neckY(x) - 1.2, 2.6], [0, 0, 1], 0.32, 'h', 6); } });
    K.part({}, () => { const pil = K.spline([[8, 3, 0], [2.5, 30, 0], [1.2, 60, 0], [2.4, 88, 0], [4, 106, 0]], 6);
      K.tube(pil, t => 2.0 + 0.5 * Math.sin(t * Math.PI), { caps: [true, true] }); K.sphere([4.2, 108.5, 0], 2.6); });
    return { map: { type: 'strings', open: Array.from({ length: 22 }, (_, i) => 36 + Math.round(i * 12 / 7)) } };
  } };

  /* ── GLOCKENSPIEL ── */
  FAM.glock = { name: 'Glockenspiel', cat: 'Mallets', voice: 'bar', yaw: -18, pitch: 34, padY: 5, build(K) {
    const N = 15, W = 64, bw = 3.0;
    K.part({}, () => { K.box(0, 0, -30, W, 3.2, 0); K.line(K.offset2([[0, -30], [W, -30], [W, 0], [0, 0]].map(q => [q[0], q[1]]), -1.2).map(q => [q[0], 3.21, q[1]]), 'x', true); });
    K.part({}, () => { K.box(1.5, 3.2, -27, W - 1.5, 3.9, -25.6, { w: 'h' }); K.box(1.5, 3.2, -4.4, W - 1.5, 3.9, -3, { w: 'h' }); });
    const nat = [0, 2, 4, 5, 7, 9, 11];
    for (let k = 0; k < N; k++) {                                     // naturals, front row
      const x = 2.2 + k * (W - 4.4) / N, L = 13 - k * 0.32, note = 79 + Math.floor((k + 4) / 7) * 12 + nat[(k + 4) % 7] - 12;
      K.part({ role: 'bar', i: note }, () => { K.box(x + 0.35, 3.9, -3.2 - L * 0.5 - 6.5, x + bw - 0.35, 4.6, -3.2 - L * 0.5 - 6.5 + L); K.ring([x + bw / 2, 4.61, -6.8 - L * 0.5 + 0.5 + L - 1.4], [0, 1, 0], 0.22, 'h', 8); });
    }
    for (let k = 0; k < N - 1; k++) {                                 // accidentals, raised back row
      const d = (k + 4) % 7; if (d === 2 || d === 6) continue;
      const x = 2.2 + (k + 1) * (W - 4.4) / N - bw / 2 + 0.2, L = 10.2 - k * 0.25, note = 79 + Math.floor((k + 4) / 7) * 12 + nat[d] - 12 + 1;
      K.part({ role: 'bar', i: note }, () => K.box(x + 0.35, 5.6, -28, x + bw - 0.35, 6.3, -28 + L));
    }
    K.part({}, () => { K.tube([[W - 16, 8.2, 8], [W - 2, 7, -4]], 0.28, { caps: [true, false] }); K.sphere([W - 16.4, 8.2, 8.4], 1.0); });
    return { map: { type: 'bars' } };
  } };

  /* ── MARIMBA — rosewood bars over graded resonators ── */
  FAM.marimba = { name: 'Marimba', cat: 'Mallets', voice: 'bar', yaw: -18, pitch: 24, padY: 4, build(K) {
    const N = 29, W = 160, bw = W / N, top = 46;
    const nat = [0, 2, 4, 5, 7, 9, 11];
    const Lz = k => 34 - k * 0.55, zc = -20;
    const post = (x, z) => K.part({}, () => { K.tube([[x, top - 5, z], [x, 3, z]], 1.1, { caps: [false, false] }); K.ring([x, 1.6, z], [0, 0, 1], 1.4, 'h', 12); });
    const ends = x => { post(x, zc - 16); K.part({}, () => K.box(x - 1.2, top - 8.5, zc - 18, x + 1.2, top - 5, zc + 18)); K.part({}, () => K.box(x - 0.8, 7, zc - 17, x + 0.8, 9, zc + 17, { w: 'h' })); };
    ends(-3);
    K.part({}, () => K.box(-3, top - 5.2, zc - 13.5, W + 3, top - 3.4, zc - 11.5, { w: 'h' }));
    for (let k = 0; k < N - 1; k++) { const d = k % 7; if (d === 2 || d === 6) continue; const x = (k + 1) * bw - bw * 0.4, L = Lz(k) * 0.72, note = 48 + Math.floor(k / 7) * 12 + nat[d] + 1;
      K.part({ role: 'bar', i: note }, () => K.box(x, top + 0.2, zc - 16 - L * 0.3, x + bw * 0.8, top + 1.9, zc - 16 + L * 0.7)); }
    for (let k = 0; k < N; k++) { const x = k * bw + bw / 2, h = 30 - k * 0.85; K.part({}, () => K.tube([[x, top - 4, zc + 4], [x, top - 4 - h, zc + 4]], bw * 0.34, { caps: [false, true] })); }
    for (let k = 0; k < N; k++) { const x = k * bw + bw * 0.1, L = Lz(k), note = 48 + Math.floor(k / 7) * 12 + nat[k % 7];
      K.part({ role: 'bar', i: note }, () => K.box(x, top - 3.2, zc + 12 - L * 0.62, x + bw * 0.8, top - 1.4, zc + 12 + L * 0.38)); }
    K.part({}, () => K.box(-3, top - 5.2, zc + 16, W + 3, top - 3.4, zc + 18, { w: 'm' }));
    post(-3, zc + 17);
    ends(W + 3); post(W + 3, zc + 17);
    return { map: { type: 'bars' } };
  } };

  /* ── KALIMBA ── */
  FAM.kalimba = { name: 'Kalimba', cat: 'Mallets', voice: 'tine', yaw: -24, pitch: 40, padY: 4, build(K) {
    const out = K.spline([[-9, -8], [9, -8], [9.4, 5], [7.6, 8.6], [0, 9.3], [-7.6, 8.6], [-9.4, 5]], 6, true);
    K.part({}, () => { K.prism(out.map(q => [q[0], q[1]]), 3.2, K.FLAT(0), { corner: 60 }); K.ring([0, 3.21, 4.6], [0, 1, 0], 1.7, 'm', 24); });
    K.part({}, () => { K.tube([[-8, 3.9, -2.2], [8, 3.9, -2.2]], 0.45, { caps: [true, true] }); });
    K.part({}, () => { K.box(-7.8, 3.2, -6.4, 7.8, 4.2, -5.2); });
    const n = 17;
    for (let i = 0; i < n; i++) {
      const x = -7.2 + i * 0.9, c = Math.abs(i - (n - 1) / 2), tip = 6.6 - c * 0.52 - (i % 2) * 0.35;
      K.part({ role: 'string', i, a: [x, 4.55, -2.2], b: [x, 4.7, tip], w: 'm' }, () => K.line([[x, 4.35, -5.8], [x, 4.55, -2.2]], 'm'));
    }
    K.part({}, () => { K.tube([[-7.9, 4.8, -4.6], [7.9, 4.8, -4.6]], 0.3, { caps: [true, true] }); [-5, 0, 5].forEach(x => K.ring([x, 5.1, -4.6], [0, 1, 0], 0.32, 'h', 8)); });
    return { map: { type: 'tines', n } };
  } };

  /* ── MUSIC BOX — lid open, pinned cylinder + comb + crank ── */
  FAM.musicbox = { name: 'Music Box', cat: 'Mallets', voice: 'tine', yaw: -30, pitch: 30, padY: 4, build(K) {
    const W = 15, D = 9.5, H = 5, a = 112 * D2R;
    K.part({}, () => { K.prism([[0, 0], [W, 0], [W, D], [0, D]], -0.9, K.Frame([0, H + 0.1, -D], [1, 0, 0], [0, Math.sin(a), Math.cos(a)], [0, Math.cos(a), -Math.sin(a)]), { corner: 20 });
      K.line(K.offset2([[0, 0], [W, 0], [W, D], [0, D]], 1.0).map(q => K.add([q[0], H + 0.1, -D], K.add(K.mul([0, Math.sin(a), Math.cos(a)], q[1]), [0, 0, 0]))), 'h', true); });
    K.part({}, () => { K.box(0, 0, -D, W, H, 0); K.line(K.offset2([[0, -D], [W, -D], [W, 0], [0, 0]], 0.8).map(q => [q[0], H + 0.01, q[1]]), 'h', true); });
    K.part({}, () => K.box(2.4, H, -8, 12.4, H + 0.35, -1.6, { w: 'h' }));
    K.part({}, () => { K.box(3.0, H + 0.35, -7.2, 3.8, H + 2.9, -4.6, { w: 'h' }); });
    K.part({ role: 'drum' }, () => { K.tube([[3.8, H + 2.0, -5.9], [10.8, H + 2.0, -5.9]], 1.05, { caps: [true, true] });
      for (let i = 0; i < 30; i++) { const x = 4.2 + (i * 3.37) % 6.3, ang = ((i * 71) % 170 + 5) * D2R; const p = [x, H + 2.0 + 1.05 * Math.sin(ang), -5.9 + 1.05 * Math.cos(ang)]; if (p[2] > -6.2) K.line([p, K.add(p, [0, 0.16 * Math.sin(ang), 0.16 * Math.cos(ang)])], 'm'); } });
    K.part({}, () => { K.box(10.8, H + 0.35, -7.2, 11.6, H + 2.9, -4.6, { w: 'h' }); });
    K.part({}, () => K.box(3.4, H + 0.35, -3.6, 11.2, H + 1.1, -2.2));
    const n = 18;
    for (let i = 0; i < n; i++) { const x = 3.8 + i * 7.0 / (n - 1); K.part({ role: 'string', i, a: [x, H + 1.1, -3.5], b: [x, H + 1.12, -4.75], w: 'm' }, () => {}); }
    K.part({}, () => { K.tube([[W, H + 2.0, -5.9], [W + 1.8, H + 2.0, -5.9]], 0.2, { caps: [false, false] }); K.tube([[W + 1.8, H + 2.0, -5.9], [W + 1.8, H - 1.2, -5.2]], 0.24, { caps: [false, false] });
      K.tube([[W + 1.8, H - 1.2, -5.2], [W + 3.8, H - 1.2, -5.2]], 0.48, { caps: [false, true] }); });
    return { map: { type: 'tines', n } };
  } };

  /* ── HARPSICHORD — the grand's builder with a long straight-tailed case and two manuals ── */
  FAM.harpsi = { name: 'Harpsichord', cat: 'Keys', voice: 'key', yaw: -66, pitch: 18, padY: 3, fade: true, build: grand({
    w: 92, len: 232, legH: 64, rim: 20, rimT: 2.5, lid: 38, flap: 58, keyZ: 13, keyY: 9, cheek: 6, keys: 29, lowNote: 41, strings: 24, plate: false, desk: 0, lyre: false, legR: 1.9, caster: false, manual2: true, cropLegs: true,
    legs: [[6, -8], [86, -8], [6, -118], [62, -118], [8, -222], [34, -214]],
    bent: [[92, -34], [90, -66], [80, -110], [66, -158], [52, -200], [42, -226], [26, -232], [0, -232]] }) };

  /* ── UPRIGHT PIANO ── */
  FAM.upright = { name: 'Upright Piano', cat: 'Keys', voice: 'key', yaw: -30, pitch: 16, padY: 3, build(K) {
    const W = 148;
    K.part({}, () => { K.box(0, 0, -58, W, 124, -12); });
    K.part({}, () => { K.box(-1.5, 124, -60, W + 1.5, 127, -9.5);
      K.line([[10, 118, -11.99], [W - 10, 118, -11.99], [W - 10, 84, -11.99], [10, 84, -11.99]], 'h', true);
      K.line([[W * 0.3, 104, -11.98], [W * 0.7, 104, -11.98], [W * 0.7, 90, -11.98], [W * 0.3, 90, -11.98]], 'x', true);
      K.line([[10, 56, -11.99], [W - 10, 56, -11.99], [W - 10, 10, -11.99], [10, 10, -11.99]], 'h', true); });
    K.part({}, () => { [-7, 0, 7].forEach(dx => K.box(W / 2 + dx - 1.5, 3, -12, W / 2 + dx + 1.5, 4.4, -6, { w: 'm' })); });
    K.part({}, () => K.box(0, 62, -12, W, 70, 8));
    K.part({}, () => K.box(0, 62, -12, 7, 80, 10));
    keyboard(K, { x0: 7.4, x1: W - 7.4, y: 73, z0: 7.4, L: 14, nWhite: 36, startNote: 36, h: 2.2 });
    K.part({}, () => K.box(W - 7, 62, -12, W, 80, 10));
    K.part({}, () => { K.box(1, 0, -12, 8, 6, 6); K.box(W - 8, 0, -12, W - 1, 6, 6); });
    return { map: { type: 'keys' } };
  } };

  /* ── TONEWHEEL ORGAN (B-3 style): two manuals, drawbars, pedalboard ── */
  FAM.tonewheel = { name: 'Tonewheel Organ', cat: 'Organ', voice: 'key', yaw: -28, pitch: 20, padY: 3, fade: true, build(K) {
    const W = 124, y0 = 56;
    [[4, -56], [W - 4, -56]].forEach(([x, z]) => K.part({ nofit: true }, () => K.tube([[x, y0, z], [x, 2, z]], 2.2, { caps: [false, true] })));
    K.part({ nofit: true }, () => { K.box(18, 0, -30, W - 18, 3, 8); for (let i = 0; i < 13; i++) K.line([[22 + i * 6.6, 3.01, 6], [22 + i * 6.6, 3.01, -18]], 'h'); });
    [[4, -4], [W - 4, -4]].forEach(([x, z]) => K.part({ nofit: true }, () => K.tube([[x, y0, z], [x, 2, z]], 2.2, { caps: [false, true] })));
    K.part({}, () => { K.box(0, y0, -60, W, 80, 0); K.line([[6, y0 + 4, 0.01], [W - 6, y0 + 4, 0.01], [W - 6, 76, 0.01], [6, 76, 0.01]], 'h', true); });
    K.part({}, () => K.box(0, 80, -60, W, 104, -24));
    K.part({}, () => { [14, W - 38].forEach(x0 => { for (let i = 0; i < 9; i++) { const x = x0 + i * 2.7, L = [5, 7, 8, 4, 6, 3, 5, 2, 4][i]; K.tube([[x, 95, -24], [x, 95, -24 + L]], 0.5, { caps: [false, false] }); K.box(x - 0.9, 93.6, -24 + L, x + 0.9, 96.4, -22.8 + L, { w: 'm' }); } }); });
    keyboard(K, { x0: 9, x1: W - 9, y: 87, z0: -11, L: 12, nWhite: 35, startNote: 48, h: 1.8 });
    K.part({}, () => K.box(9, 80, -11, W - 9, 84.5, -9, { w: 'h' }));
    keyboard(K, { x0: 9, x1: W - 9, y: 82.5, z0: 3, L: 12, nWhite: 35, startNote: 36, h: 1.8 });
    K.part({}, () => { K.box(0, 80, -24, 8, 100, 3.5); K.box(W - 8, 80, -24, W, 100, 3.5); });
    K.part({}, () => K.box(-2, 104, -62, W + 2, 107, 5));
    return { map: { type: 'keys' } };
  } };

  /* ── KOTO — long zither, 13 strings over movable bridges ── */
  FAM.koto = { name: 'Koto', cat: 'Plucked', voice: 'string', yaw: -12, pitch: 32, padY: 10, build(K) {
    const Lk = 182, sec = [[-12, 0], [12, 0], [12.4, 4.6], [9, 7.2], [0, 8.2], [-9, 7.2], [-12.4, 4.6]];
    K.part({}, () => { K.prism(sec, Lk, K.Frame([0, 0, 0], [0, 0, 1], [0, 1, 0], [1, 0, 0]), { corner: 40 });
      K.line([[0, 8.25, 0], [Lk, 8.25, 0]], 'x'); });
    const topY = z => 8.2 - (Math.abs(z) / 12.4) * (Math.abs(z) / 12.4) * 3.2;
    K.part({}, () => { K.box(3, 0, -10, 7, 9.6, 10, { w: 'm' }); K.box(Lk - 16, 0, -10, Lk - 12, 9.4, 10, { w: 'm' }); });
    const strs = [];
    for (let i = 0; i < 13; i++) { const z = -8.4 + i * 1.4, bx = 62 + i * 6.8 + Math.sin(i * 0.9) * 4, y = topY(z) + 1.6;
      strs.push({ z, bx, y });
      K.part({}, () => K.line([[5, 9.8, z * 0.95], [bx, y + 1.3, z]], 'h')); }
    strs.forEach((q, i) => K.part({}, () => K.prism([[-1.3, 0], [1.3, 0], [0.35, 1.5], [-0.35, 1.5]], 0.5, K.Frame([q.bx - 0.25, topY(q.z), q.z], [0, 0, 1], [0, 1, 0], [1, 0, 0]), { corner: 20 })));
    strs.forEach((q, i) => K.part({ role: 'string', i, a: [q.bx, q.y + 1.3, q.z], b: [Lk - 14, 9.6, q.z * 0.95], w: 'h' }, () => {}));
    return { map: { type: 'strings', open: [50, 55, 57, 58, 62, 63, 67, 69, 70, 74, 75, 79, 81] } };
  } };


  /* standalone SVG (for handing a single drawing to the engine / a designer) */
  const CSS = '.f{fill:#10101F}.m{fill:none;stroke:#fff;stroke-opacity:.84;stroke-width:.8;stroke-linejoin:round;stroke-linecap:round}.h{fill:none;stroke:#fff;stroke-opacity:.4;stroke-width:.55;stroke-linejoin:round;stroke-linecap:round}.x{fill:none;stroke:#fff;stroke-opacity:.18;stroke-width:.5}.k{fill:none;stroke:none}';
  function svgString(id, o) { o = o || {}; const W = o.w || 302.65, H = o.h || 65, r = render(id, { w: W, h: H });
    const fade = r.meta.fade ? `<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset=".64" stop-color="#fff"/><stop offset=".98" stop-color="#fff" stop-opacity="0"/></linearGradient><mask id="fm"><rect width="${W}" height="${H}" fill="url(#g)"/></mask></defs>` : '';
    return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W * 2}" height="${H * 2}"><style>${CSS}</style><rect width="${W}" height="${H}" rx="8" fill="#10101F"/>${fade}<g${r.meta.fade ? ' mask="url(#fm)"' : ''}>${r.svg}</g></svg>`; }

  root.OrganicsArt = { families: FAM, render, svgString, Kit, keyboard };
})(typeof window !== 'undefined' ? window : globalThis);
