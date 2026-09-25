// ══════════════════════════════════════════════════════════════════════════════════════════════
//  _org_align_gate.js — tp104 THE ORGANICS VIEW SITS EXACTLY WHERE THE OTHER ENGINES SIT (Max's #1 principle).
//
//    node Tests/_org_align_gate.js [page.html] [out-dir]
//
//  Osc A runs the reference engine (Modal, then Wavetable), osc C runs Organics, at deviceScaleFactor 3, dark theme.
//  tp105 — the header is now measured against the TEXT INK (Tests/_org_ink.js, shared with the real-WebView harness
//  Tests/_org_real.js): the line = the middle of OSC's capitals; every text's own cap middle, and the ink centre of every
//  box, arrow and the dot, must sit on it. Round 1 compared the boxes to Modal's boxes — which carried the same 2 px
//  offset Max saw ("the A and + boxes sit lower than the text").
//   1  THE RED LINE: OSC · Organics · the articulation · ‹ the instrument › · [A] · [+] — every ink centre within ±0.5 px
//      of the line (CSS px), with the pill, without it, a 33-character name, and the swapped side (+ → ←)
//   2  FIXED POSITIONS: the pill appearing or changing moves nothing; the pill clears the ‹ by ≥ 4 px
//   3  THE KNOB ROW: five rings evenly spaced on Modal's exact centres, labels on Modal's label baseline, ‹ › on Modal's
//   4  THE PICTURE BOX is Modal's box and it is TRANSPARENT (no fill, no outline — Max: "same colour as everything else")
//   5  THE SAME LAW ON EVERY ENGINE: osc A under each of the eight engines, ink centres within ±0.5 px
//   6  THE TWINS: every family whose single drawing fills < 55 % of the box width is drawn as TWO; the pair is symmetric
//      about the box centre (outer margins equal ≤ 0.5 px), the gap stays in [6 %, 20 %] of the width, and the pair fills
//      ≥ 58 % of it; every twin's two halves are different drawings
//   7  tp107 THE HOUSE STYLE ("WHITE INSIDE", Max): the articulation pill's text wears exactly what the instrument name and
//      Modal's family/form pills wear (colour, weight, size, tracking) at rest, on hover and open — never the dim grey
//   8  tp107 PAGE 2 (Attack · Release · Sustain · Velocity · Image) sits on Modal's page-2 ring centres and label line, and
//      nothing on it moves while the values sweep (Attack 0 → 1, every readout)
//  Writes org-align.png (red lines through the measured ink) into the out-dir.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const fs = require('fs'), path = require('path');
const H = require('./_org_harness.js');
const INK = require('./_org_ink.js');
const PAGE = process.argv[2] || H.PAGE_DEFAULT;
const OUT = process.argv[3] || path.resolve(__dirname, '../Design/organics/impl-shots');
const DPR = 3;
let pass = 0, fail = 0;
const ok = (c, name, detail) => { c ? ++pass : ++fail; console.log(`  ${c ? 'PASS' : 'FAIL'}  ${name}${detail ? '\n        ' + detail : ''}`); };
const f2 = v => (v == null || isNaN(v) ? 'n/a' : (+v).toFixed(2));
const max = a => Math.max.apply(null, a), min = a => Math.min.apply(null, a);

// page-side: read the screenshot back and measure ink. Also returns the PNG with the red lines drawn in (lines[]).
async function measure(p, lines) {
  const shot = await p.screenshot({ encoding: 'base64' });
  return p.evaluate(async (b64, DPR, lines) => {
    const img = new Image(); img.src = 'data:image/png;base64,' + b64; await img.decode();
    const cv = document.createElement('canvas'); cv.width = img.width; cv.height = img.height;
    const cx = cv.getContext('2d'); cx.drawImage(img, 0, 0); const D = cx.getImageData(0, 0, cv.width, cv.height).data, W = cv.width;
    const lum = (x, y) => { const i = (y * W + x) * 4; return (D[i] + D[i + 1] + D[i + 2]) / 3; };
    function ink(r, th, pad) { th = th || 72; pad = pad == null ? 1 : pad;
      const x0 = Math.floor(r.left * DPR) - pad, x1 = Math.ceil(r.right * DPR) + pad, y0 = Math.floor(r.top * DPR) - pad, y1 = Math.ceil(r.bottom * DPR) + pad;
      const rows = []; let xl = 1e9, xr = -1;
      for (let y = y0; y < y1; y++) { let n = 0; for (let x = x0; x < x1; x++) if (lum(x, y) > th) { n++; if (x < xl) xl = x; if (x > xr) xr = x; } rows.push([y, n]); }
      const inkRows = rows.filter(q => q[1] > 0); if (!inkRows.length) return null;
      const mx = Math.max(...rows.map(q => q[1])), band = rows.filter(q => q[1] >= mx * 0.25);
      return { top: inkRows[0][0] / DPR, bot: (inkRows[inkRows.length - 1][0] + 1) / DPR, base: (band[band.length - 1][0] + 1) / DPR,
               cy: (inkRows[0][0] + inkRows[inkRows.length - 1][0] + 1) / 2 / DPR, cx: (xl + xr + 1) / 2 / DPR, left: xl / DPR, right: (xr + 1) / DPR }; }
    const vis = e => e && e.offsetParent !== null && getComputedStyle(e).display !== 'none' && getComputedStyle(e).visibility !== 'hidden' && e.getBoundingClientRect().width > 0;
    function dev(o) { const d = document.getElementById('osc-' + o + '-device'), R = d.getBoundingClientRect(), org = d.classList.contains('engine-organic');
      const item = (key, sel, th) => { const e = d.querySelector(sel); if (!vis(e)) return null; const q = ink(e.getBoundingClientRect(), th); if (!q) return null;
        return { key, base: q.base - R.top, cy: q.cy - R.top, cx: q.cx - R.left, left: q.left - R.left, right: q.right - R.left }; };
      const H = {};
      const put = (k, sel, th) => { const q = item(k, sel, th); if (q) H[k] = q; };
      put('osc', '.device-label'); put('engine', '.engine-display'); put('A', '.osc-letter'); put('plus', '.swap-btn');
      if (org) { put('pill', '.org-artic .samp-sel-disp'); put('prev', '.org-nav[data-dir="-1"]', 60); put('name', '.org-inst-wrap .preset-display'); put('next', '.org-nav[data-dir="1"]', 60); }
      else if (d.classList.contains('engine-modal')) { put('pill', '.samp-head .samp-sel.mode .samp-sel-disp'); put('name', '.md-fam-wrap:not(.md-form) .preset-display'); put('name2', '.md-fam-wrap.md-form .preset-display'); }
      else { const pw = d.querySelector('.device-corner-preset .preset-wrap:not(.hm-mode-wrap):not(.md-fam-wrap):not(.samp-name-wrap):not(.org-inst-wrap)');
        const navs = [...d.querySelectorAll('.device-corner-preset .wt-nav')].filter(vis);
        if (pw) { const q = ink(pw.querySelector('.preset-display').getBoundingClientRect()); if (q) H.name = { key: 'name', base: q.base - R.top, cy: q.cy - R.top, cx: q.cx - R.left }; }
        navs.forEach((n, i) => { const q = ink(n.getBoundingClientRect(), 60); if (q) H[i ? 'next' : 'prev'] = { key: i ? 'next' : 'prev', cy: q.cy - R.top, cx: q.cx - R.left, left: q.left - R.left, right: q.right - R.left }; }); }
      const wrap = org ? '.organic-knobs.organic-pg1' : '.modal-knobs.modal-pg1';
      const rings = [...d.querySelectorAll(wrap + ' .knob-ring')].filter(vis).map(e => { const r = e.getBoundingClientRect(), c = { x: r.left + r.width / 2, y: r.top + r.height / 2 }, s = r.width * 0.30;
        const k = ink({ left: c.x - s * 0.8, right: c.x + s * 0.8, top: c.y - s * 0.8, bottom: c.y + s * 0.8 }, 120, 0);   // ±0.24 w: inside the arc's inner edge (0.375 w)   // the number, inside the arc
        return { cx: c.x - R.left, cy: c.y - R.top, kdx: k ? k.cx - c.x : null, kdy: k ? k.cy - c.y : null, t: (e.querySelector('.kv') || {}).textContent }; });
      const labels = [...d.querySelectorAll(wrap + ' .knob-label')].filter(vis).map(e => { const q = ink(e.getBoundingClientRect()); return q ? q.base - R.top : null; });
      const wr = d.querySelector(org ? '.organic-knob-wrap' : '.modal-knob-wrap');
      const arrows = wr ? [...wr.querySelectorAll(':scope > [class*="-arrow"]')].filter(vis).map(e => { const q = ink(e.getBoundingClientRect(), 60); return q ? { cx: q.cx - R.left, cy: q.cy - R.top } : null; }) : [];
      const bx = d.querySelector('.sample-view .samp-disp').getBoundingClientRect();
      return { R: { left: R.left, top: R.top, w: R.width, h: R.height }, H, rings, labels, arrows, box: { x: bx.left - R.left, y: bx.top - R.top, w: bx.width, h: bx.height } }; }
    const out = { a: dev('a'), c: dev('c') };
    if (lines && lines.length) { cx.fillStyle = 'rgba(255,0,0,0.95)';
      lines.forEach(L => { if (L.h) cx.fillRect(Math.round(L.x * DPR), Math.round(L.y * DPR), Math.round(L.w * DPR), 1); else cx.fillRect(Math.round(L.x * DPR), Math.round(L.y * DPR), 1, Math.round(L.hh * DPR)); });
      out.png = cv.toDataURL('image/png').split(',')[1]; }
    return out;
  }, shot, DPR, lines || []);
}

// the header's ink centres (Tests/_org_ink.js) for the devices in ids (viewport px; divide by the page scale for CSS px)
async function inkLine(p, ids) {
  await p.evaluate(`window.__INKC=${INK.COLLECT.toString()};window.__INKA=${INK.ANALYZE.toString()};`);
  const g = await p.evaluate(ids => window.__INKC(ids), ids), shot = await p.screenshot({ encoding: 'base64' });
  const r = await p.evaluate((b64, g, d) => window.__INKA(b64, g, d), shot, g, DPR);
  const out = {}; ids.forEach(id => { out[id] = INK.centreline(r[id]); if (out[id]) out[id].R = g[id].R; }); return out; }
async function scaleOf(p, id) { return p.evaluate(id => { const d = document.getElementById(id); return d.getBoundingClientRect().height / d.offsetHeight; }, id); }
const worstCss = (c, sc) => c ? c.worst / sc : 99;
const listCss = (c, sc) => c ? c.items.map(q => q.key + (q.txt ? '(' + q.txt.slice(0, 10) + ')' : '') + ' ' + f2(q.d / sc)).join('  ') : 'no OSC ink';

(async () => {
  const { b, p, errs } = await H.launch({ page: PAGE, dpr: DPR, width: 1200, height: 820 });
  await H.enable(p, 'a'); await H.enable(p, 'c');
  await H.setEngine(p, 'a', 6); await H.setEngine(p, 'c', 7); await H.sleep(900);
  await p.evaluate(() => { const vals = [0.5, 0.75, 0.2, 0.93, 0.25]; ['DYNAMICS', 'TONE', 'BODY', 'VIBRATO', 'HUMAN'].forEach((s, i) => { const k = document.querySelector('.knob[data-syn="SYN_OSC_C_ORG_' + s + '"]'); if (k && k.__applyFill) k.__applyFill(vals[i]); });
    ['HARD', 'POS', 'DECAY', 'MATERIAL', 'BREATH'].forEach((s, i) => { const k = document.querySelector('.knob[data-syn="SYN_OSC_A_MODAL_' + s + '"]'); if (k && k.__applyFill) k.__applyFill([0.62, 0.4, 0.55, 0.3, 0.48][i]); }); });
  await H.sleep(300);
  const G = await measure(p);                                                   // Organics: Grand Piano (one articulation)
  await p.evaluate(() => window.__orgSetInstrument('c', 'vsco.violin.section')); await H.sleep(500);
  const V = await measure(p);                                                   // Organics: Violin Section (the pill shows)
  await p.evaluate(() => { const e = document.getElementById('osc-c-orginst-display'); e.textContent = 'Salamander Grand Piano V3 Concert';
    document.querySelector('#osc-c-device .org-artic .samp-sel-disp').textContent = 'Stacc'; }); await H.sleep(200);   // the longest name AND the widest short articulation
  const LONG = await measure(p);                                                // a name far longer than the slot
  await p.evaluate(() => window.__orgSetInstrument('c', 'vsco.violin.section')); await H.sleep(300);
  await H.setEngine(p, 'a', 0); await H.sleep(600);
  const WT = await measure(p);                                                  // the wavetable engine: the ‹ name › reference

  const o = G.c, m = G.a;
  if (process.env.ORG_DUMP) console.log(JSON.stringify({ V: V.c.H, L: LONG.c.H, R: V.c.R }, null, 0));
  // [1] the red line, Organics (osc C): Grand (no pill), Violin (the pill), a long name, the swapped side
  const SC = await scaleOf(p, 'osc-c-device'), C1 = [];
  await p.evaluate(() => window.__orgSetInstrument('c', 'salamander.grand')); await H.sleep(400); C1.push(['Grand', (await inkLine(p, ['osc-c-device']))['osc-c-device']]);
  await p.evaluate(() => window.__orgSetInstrument('c', 'vsco.violin.section')); await H.sleep(400); C1.push(['Violin + pill', (await inkLine(p, ['osc-c-device']))['osc-c-device']]);
  await p.evaluate(() => { document.getElementById('osc-c-orginst-display').textContent = 'Salamander Grand Piano V3 Concert'; }); await H.sleep(150);
  C1.push(['33-char name', (await inkLine(p, ['osc-c-device']))['osc-c-device']]);
  await p.evaluate(() => window.__orgSetInstrument('c', 'vsco.violin.section')); await H.sleep(300);
  await p.evaluate(() => document.querySelector('#osc-c-device .swap-btn').click()); await H.sleep(400); C1.push(['swapped (←)', (await inkLine(p, ['osc-c-device']))['osc-c-device']]);
  await p.evaluate(() => document.querySelector('#osc-c-device .swap-btn').click()); await H.sleep(400);
  const w1 = max(C1.map(q => worstCss(q[1], SC)));
  ok(w1 <= 0.5 && C1.every(q => q[1] && q[1].items.some(x => x.key === 'letter:box') && q[1].items.some(x => x.key === 'plus:box')),
     `[1] the red line: every ink centre of the Organics header within ±0.5 px of OSC's cap middle (worst ${f2(w1)} px)`, C1.map(q => q[0] + ': ' + listCss(q[1], SC)).join('\n        '));
  // [2] fixed positions + the pill's clearance
  const moved = max(['engine', 'osc', 'A', 'plus', 'next'].map(k => Math.abs(G.c.H[k].cx - V.c.H[k].cx)));
  const clear = [V.c, LONG.c].map(d => d.H.prev.left - d.H.pill.right);
  ok(moved <= 0.5 && min(clear) >= 4,
     `[2] the pill moves nothing (${f2(moved)} px) and clears the ‹ by ${clear.map(f2).join(' / ')} px (Violin / a 33-character name)`);
  // [3] knobs
  const gaps = o.rings.slice(1).map((r, i) => r.cx - o.rings[i].cx), gm = m.rings.slice(1).map((r, i) => r.cx - m.rings[i].cx);
  const dx = o.rings.map((r, i) => Math.abs(r.cx - m.rings[i].cx)), dy = o.rings.map((r, i) => Math.abs(r.cy - m.rings[i].cy));
  const ldy = o.labels.map((l, i) => Math.abs(l - m.labels[i]));
  const adx = o.arrows.map((a, i) => m.arrows[i] ? max([Math.abs(a.cx - m.arrows[i].cx), Math.abs(a.cy - m.arrows[i].cy)]) : 99);
  ok(max(gaps) - min(gaps) <= 0.5 && max(dx.concat(dy)) <= 0.5 && max(ldy) <= 0.5 && o.arrows.length === 2 && m.arrows.length === 2 && max(adx) <= 0.5,
     `[3] the knob row: gaps ${gaps.map(f2).join('/')} (Modal ${gm.map(f2).join('/')}); ring centres vs Modal Δ ≤ ${f2(max(dx.concat(dy)))}; label baselines Δ ≤ ${f2(max(ldy))}; ‹ › Δ ≤ ${f2(max(adx))} px`,
     'organics x ' + o.rings.map(r => f2(r.cx)).join(' ') + ' y ' + f2(o.rings[0].cy) + ' · labels ' + f2(o.labels[0]) + '\n        modal    x ' + m.rings.map(r => f2(r.cx)).join(' ') + ' y ' + f2(m.rings[0].cy) + ' · labels ' + f2(m.labels[0]));
  // [4] box + ring numbers
  const bxD = max(['x', 'y', 'w', 'h'].map(k => Math.abs(o.box[k] - m.box[k])));
  const kv = r => max([Math.abs(r.kdx || 0), Math.abs(r.kdy || 0)]);
  const kO = max(o.rings.map(kv)), kM = max(m.rings.map(kv));
  const bgC = await p.evaluate(() => { const e = document.querySelector('#osc-c-device .sample-view .samp-disp'), cs = getComputedStyle(e); return { bg: cs.backgroundColor, bw: cs.borderTopWidth, bs: cs.borderTopStyle, sh: cs.boxShadow, ol: cs.outlineStyle }; });
  const clearBox = /rgba\(0, 0, 0, 0\)|transparent/.test(bgC.bg) && (bgC.bs === 'none' || parseFloat(bgC.bw) === 0) && (bgC.sh === 'none') && (bgC.ol === 'none');
  ok(bxD <= 0.5 && o.rings.every(r => r.kdx != null) && kO <= Math.max(0.5, kM + 0.1) && clearBox,
     `[4] the picture box is Modal's (Δ ${f2(bxD)} px, ${f2(o.box.w)} × ${f2(o.box.h)}) and TRANSPARENT (${bgC.bg}, border ${bgC.bs}, shadow ${bgC.sh}); ring numbers centred: Organics ≤ ${f2(kO)} px (Modal ≤ ${f2(kM)} px)`,
     o.rings.map(r => `"${r.t}" ${f2(r.kdx)},${f2(r.kdy)}`).join(' · ') + ' | modal ' + m.rings.map(r => `"${r.t}" ${f2(r.kdx)},${f2(r.kdy)}`).join(' · '));

  // [7] tp107 — the house style of the header controls (Modal on A, Organics on C with the Violin's pill up)
  await H.setEngine(p, 'a', 6); await p.evaluate(() => window.__orgSetInstrument('c', 'vsco.violin.section')); await H.sleep(700);
  const sty = sel => p.evaluate(sel => { const e = document.querySelector(sel); if (!e) return null; const c = getComputedStyle(e);
    return { color: c.color, weight: c.fontWeight, size: c.fontSize, ls: c.letterSpacing, bg: c.backgroundColor }; }, sel);
  const hov = async (hoverSel, sel) => { const r = await p.evaluate(s => { const e = document.querySelector(s).getBoundingClientRect(); return { x: e.left + e.width / 2, y: e.top + e.height / 2 }; }, hoverSel);
    await p.mouse.move(r.x, r.y); await H.sleep(250); const st = await sty(sel); await p.mouse.move(2, 2); await H.sleep(250); return st; };
  const S7 = { pill: await sty('#osc-c-device .org-artic .samp-sel-disp'), name: await sty('#osc-c-device .org-inst-wrap .preset-display'),
               fam: await sty('#osc-a-device .md-fam-wrap:not(.md-form) .preset-display'), form: await sty('#osc-a-device .md-fam-wrap.md-form .preset-display') };
  S7.pillHover = await hov('#osc-c-device .org-artic', '#osc-c-device .org-artic .samp-sel-disp');
  S7.famHover = await hov('#osc-a-device .md-fam-wrap:not(.md-form)', '#osc-a-device .md-fam-wrap:not(.md-form) .preset-display');
  await p.evaluate(() => window.__orgOpenArtic('c')); await H.sleep(150); S7.pillOpen = await sty('#osc-c-device .org-artic .samp-sel-disp');
  await p.evaluate(() => document.body.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }))); await H.sleep(150);
  const same = (x, y, ks) => ks.every(k => x && y && x[k] === y[k]);
  const TXT = ['color', 'weight', 'size', 'ls'];
  ok(same(S7.pill, S7.name, TXT) && same(S7.pill, S7.fam, TXT) && same(S7.pill, S7.form, TXT) && same(S7.pillHover, S7.famHover, ['color', 'bg']) && same(S7.pillOpen, S7.famHover, ['color', 'bg']),
     `[7] the articulation pill wears the house header style — ${S7.pill && S7.pill.color} ${S7.pill && S7.pill.weight} ${S7.pill && S7.pill.size} / ${S7.pill && S7.pill.ls}, hover ${S7.pillHover && S7.pillHover.bg}, open ${S7.pillOpen && S7.pillOpen.bg}`,
     JSON.stringify(S7));
  // [8] tp107 — page 2 on Modal's page 2, and nothing moves while the values sweep
  const P2 = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms));
    const dm = document.getElementById('osc-a-device'), dc = document.getElementById('osc-c-device');
    dm.querySelector('.modal-knob-wrap').classList.add('pg2'); dc.querySelector('.organic-knob-wrap').classList.add('pg2'); await sleep(120);
    const geo = (d, pg) => { const R = d.getBoundingClientRect(), sc = R.height / d.offsetHeight;
      return { rings: [...d.querySelectorAll(pg + ' .knob-ring')].map(e => { const r = e.getBoundingClientRect(); return [(r.left + r.width / 2 - R.left) / sc, (r.top + r.height / 2 - R.top) / sc]; }),
               labels: [...d.querySelectorAll(pg + ' .knob-label')].map(e => { const r = e.getBoundingClientRect(); return (r.top + r.height / 2 - R.top) / sc; }) }; };
    const m = geo(dm, '.modal-pg2'), o = geo(dc, '.organic-pg2');
    const all = () => JSON.stringify([...dc.querySelectorAll('.organic-pg2 .knob, .organic-pg2 .knob-ring, .organic-arrow, .org-head, .org-inst-wrap, .org-nav, .osc-letter, .swap-btn')].map(e => { const r = e.getBoundingClientRect(); return [r.left, r.top, r.width, r.height].map(v => v.toFixed(2)); }));
    const k0 = all(), att = dc.querySelector('.knob[data-syn="SYN_OSC_C_ORG_ATTACK"]'); let worst = k0;
    const labs = [];
    for (const v of [0, 0.1, 0.25, 0.5, 0.62, 0.8, 0.97, 1]) { att.__applyFill(v); labs.push(window.__orgFmt('SYN_OSC_C_ORG_ATTACK', v, false)); await sleep(10); if (all() !== k0) worst = all(); }
    att.__applyFill(0.5);
    dm.querySelector('.modal-knob-wrap').classList.remove('pg2'); dc.querySelector('.organic-knob-wrap').classList.remove('pg2');
    return { m, o, still: worst === k0, labs, names: [...dc.querySelectorAll('.organic-pg2 .knob-label')].map(e => e.textContent) }; });
  const d8 = max(P2.o.rings.map((r, i) => max([Math.abs(r[0] - P2.m.rings[i][0]), Math.abs(r[1] - P2.m.rings[i][1])])));
  const l8 = max(P2.o.labels.map((l, i) => Math.abs(l - P2.m.labels[i])));
  ok(P2.o.rings.length === 5 && d8 <= 0.5 && l8 <= 0.5 && P2.still && P2.names.join('·') === 'Attack·Release·Sustain·Velocity·Image',
     `[8] page 2 (${P2.names.join(' · ')}) sits on Modal's page 2: ring centres Δ ≤ ${f2(d8)} px, labels Δ ≤ ${f2(l8)} px; nothing moves while Attack sweeps (${P2.still})`, P2.labs.join(' · '));

  // [5] every engine on osc A
  const SA = await scaleOf(p, 'osc-a-device'), E5 = [];
  for (let e = 0; e < 8; e++) { await H.setEngine(p, 'a', e); await H.sleep(500); E5.push([e, (await inkLine(p, ['osc-a-device']))['osc-a-device']]); }
  const w5 = max(E5.map(q => worstCss(q[1], SA)));
  ok(w5 <= 0.5, `[5] the same line on every engine (osc A, engines 0–7): worst ${f2(w5)} px`, E5.map(q => 'engine ' + q[0] + ': ' + listCss(q[1], SA)).join('\n        '));
  // [6] the twins
  const T = await p.evaluate(() => { const A = window.__orgArt, out = []; const W = 302.65, Hh = 65;
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg'); document.body.appendChild(svg);
    Object.keys(A.families).forEach(id => { const f = A.families[id], one = A.render(id, { w: W, h: Hh, single: true }); svg.innerHTML = one.svg; const bb = svg.getBBox();
      const r = { id, fill1: bb.width / W, twin: !!f.twin };
      if (f.twin) { const t = A.render(id, { w: W, h: Hh }); svg.innerHTML = t.svg; const gs = [...svg.querySelectorAll('.twm')].map(g => g.getBBox());
        r.l = gs[0].x; r.r = W - (gs[1].x + gs[1].width); r.gap = gs[1].x - (gs[0].x + gs[0].width); r.span = (gs[1].x + gs[1].width - gs[0].x) / W;
        r.inBox = gs.every(g => g.x >= -0.5 && g.x + g.width <= W + 0.5); r.differ = t.svg.split('class="twm"')[1] !== t.svg.split('class="twm"')[2]; }
      out.push(r); }); svg.remove(); return { W, rows: out, gmin: A.TWIN.gapMin, gmax: A.TWIN.gapMax }; });
  const miss = T.rows.filter(r => !r.twin && r.fill1 < 0.55), badT = T.rows.filter(r => r.twin && !(Math.abs(r.l - r.r) <= 0.5 && r.gap >= T.gmin * T.W - 0.5 && r.gap <= T.gmax * T.W + 0.5 && r.span >= 0.58 && r.inBox && r.differ));
  ok(!miss.length && !badT.length, `[6] twins: every family under 55 % single fill is drawn twice (${T.rows.filter(r => r.twin).length} twins), symmetric (outer margins ≤ 0.5 px apart), gap in [6, 20] %, span ≥ 58 %`,
     (miss.length ? 'SINGLE BUT SMALL: ' + miss.map(r => r.id + ' ' + Math.round(r.fill1 * 100) + '%').join(', ') + '\n        ' : '') + (badT.length ? 'BAD: ' + badT.map(r => r.id).join(', ') + '\n        ' : '') +
     T.rows.map(r => r.twin ? `${r.id} margins ${f2(r.l)}/${f2(r.r)} gap ${Math.round(r.gap / T.W * 100)}% span ${Math.round(r.span * 100)}%` : `${r.id} single ${Math.round(r.fill1 * 100)}%`).join(' · '));
  // the proof picture: Modal (A) and Organics (C, Violin), red lines through the measured ink
  await H.setEngine(p, 'a', 6); await H.sleep(600);
  const lines = [];
  const LINE = await inkLine(p, ['osc-a-device', 'osc-c-device']); LINE.a = LINE['osc-a-device']; LINE.c = LINE['osc-c-device'];
  const pre = await measure(p);
  [['a'], ['c']].forEach(([k]) => { const d = pre[k], R = d.R;
    const L = LINE[k]; if (L) lines.push({ h: 1, x: R.left - 8, y: L.line, w: R.w + 16 });            // THE red line: the header's ink centreline
    lines.push({ h: 1, x: R.left - 8, y: R.top + d.rings[0].cy, w: R.w + 16 });                        // the ring centres
    lines.push({ h: 1, x: R.left - 8, y: R.top + d.labels[0], w: R.w + 16 });                          // the label baseline
    d.rings.forEach(r => lines.push({ h: 0, x: R.left + r.cx, y: R.top + r.cy - 20, hh: 40 })); });
  const fin = await measure(p, lines);
  try { fs.mkdirSync(OUT, { recursive: true }); } catch (e) {}
  const a = fin.a.R, c = fin.c.R, x0 = Math.min(a.left, c.left) - 16, y0 = Math.min(a.top, c.top) - 12;
  const full = Buffer.from(fin.png, 'base64'); const tmp = path.join(OUT, '.org-align-full.png'); fs.writeFileSync(tmp, full);
  // crop through the browser (no image library in node): load the full PNG into a page canvas
  const crop = await p.evaluate(async (b64, x, y, w, h, DPR) => { const img = new Image(); img.src = 'data:image/png;base64,' + b64; await img.decode();
    const cv = document.createElement('canvas'); cv.width = Math.round(w * DPR); cv.height = Math.round(h * DPR); cv.getContext('2d').drawImage(img, -Math.round(x * DPR), -Math.round(y * DPR));
    return cv.toDataURL('image/png').split(',')[1]; }, fin.png, x0, y0, Math.max(a.left + a.w, c.left + c.w) - x0 + 16, Math.max(a.top + a.h, c.top + c.h) - y0 + 12, DPR);
  fs.writeFileSync(path.join(OUT, 'org-align.png'), Buffer.from(crop, 'base64')); fs.unlinkSync(tmp);
  console.log('  (proof image: ' + path.join(OUT, 'org-align.png') + ')');
  const real = errs.filter(e => !/formatOutput/.test(e));
  ok(real.length === 0, '[—] no page errors', real.join(' | '));
  await b.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL (crash)', e); process.exit(1); });
