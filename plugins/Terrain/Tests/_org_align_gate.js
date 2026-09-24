// ══════════════════════════════════════════════════════════════════════════════════════════════
//  _org_align_gate.js — tp104 THE ORGANICS VIEW SITS EXACTLY WHERE THE OTHER ENGINES SIT (Max's #1 principle).
//
//    node Tests/_org_align_gate.js [page.html] [out-dir]
//
//  Osc A runs the reference engine (Modal, then Wavetable), osc C runs Organics, at deviceScaleFactor 3, dark theme.
//  Everything is INK (rendered pixels read back from the screenshot), in CSS px relative to each device's own
//  top-left, so the devices compare directly. Text is compared by its BASELINE (the lowest row carrying ≥ 25 % of
//  the densest row: descenders are sparse and cannot move it; same size + same baseline = same centreline);
//  boxes ([A] [+]) and arrows (‹ ›) by their ink bbox centre.
//   1  THE HEADER TEXT IS ONE LINE: OSC · Organics · the articulation pill · the name share a baseline (≤ 0.5 px),
//      with and without the pill, and it is Modal's baseline (≤ 0.5 px)
//   2  THE BOXES [A] [+] sit on Modal's box line, and ‹ › sit on the name exactly as the wavetable engine's ‹ › do
//      (arrow centre − name baseline, ≤ 0.5 px). Nothing in the header moves when the pill appears, and the pill
//      never touches the ‹ (a long name included)
//   3  THE KNOB ROW: five rings evenly spaced (gaps equal ≤ 0.5 px) on Modal's exact centres (Δ ≤ 0.5 px), the
//      labels on Modal's label baseline, the ‹ › set arrows on Modal's
//   4  THE PICTURE BOX is Modal's box (≤ 0.5 px) and each ring's number is centred in its ring as Modal's are
//  Writes org-align.png (red lines through the measured ink) into the out-dir.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const fs = require('fs'), path = require('path');
const H = require('./_org_harness.js');
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

(async () => {
  const { b, p, errs } = await H.launch({ page: PAGE, dpr: DPR, width: 1200, height: 820 });
  await H.enable(p, 'a'); await H.enable(p, 'c');
  await H.setEngine(p, 'a', 6); await H.setEngine(p, 'c', 7); await H.sleep(900);
  await p.evaluate(() => { const vals = [0.5, 0.75, 0.2, 0.93, 0.25]; ['DYNAMICS', 'TONE', 'BODY', 'ATTACK', 'HUMAN'].forEach((s, i) => { const k = document.querySelector('.knob[data-syn="SYN_OSC_C_ORG_' + s + '"]'); if (k && k.__applyFill) k.__applyFill(vals[i]); });
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
  // [1] text baselines
  const tb = (Hd) => ['osc', 'engine', 'pill', 'name'].filter(k => Hd[k]).map(k => [k, Hd[k].base]);
  const spread = a => max(a.map(q => q[1])) - min(a.map(q => q[1]));
  const bG = tb(o.H), bV = tb(V.c.H), bM = tb(m.H).concat(m.H.name2 ? [['name2', m.H.name2.base]] : []);
  const mBase = m.H.osc.base;
  ok(spread(bG) <= 0.5 && spread(bV) <= 0.5 && max(bG.concat(bV).map(q => Math.abs(q[1] - mBase))) <= 0.5,
     `[1] the header text is ONE baseline: Organics spread ${f2(spread(bG))} (Grand) / ${f2(spread(bV))} (Violin, with the pill) px; off Modal's OSC baseline ≤ ${f2(max(bG.concat(bV).map(q => Math.abs(q[1] - mBase))))} px`,
     'organics(violin): ' + bV.map(q => q[0] + ' ' + f2(q[1])).join(' · ') + '\n        modal: ' + bM.map(q => q[0] + ' ' + f2(q[1])).join(' · ') + ` (Modal's own spread ${f2(spread(bM))})`);
  // [2] boxes, arrows, fixed positions, no collision
  const boxD = max(['A', 'plus'].map(k => Math.abs(o.H[k].cy - m.H[k].cy)));
  const arrO = [V.c.H.prev.cy - V.c.H.name.base, V.c.H.next.cy - V.c.H.name.base], arrW = [WT.a.H.prev.cy - WT.a.H.name.base, WT.a.H.next.cy - WT.a.H.name.base];
  const arrD = max([Math.abs(arrO[0] - arrW[0]), Math.abs(arrO[1] - arrW[1])]);
  const moved = max(['engine', 'osc', 'A', 'plus', 'next'].map(k => Math.abs(G.c.H[k].cx - V.c.H[k].cx)));
  const clear = [V.c, LONG.c].map(d => d.H.prev.left - d.H.pill.right);
  const boxesVsWT = max(['A', 'plus'].map(k => Math.abs(o.H[k].cy - WT.a.H[k].cy)));
  ok(boxD <= 0.5 && arrD <= 0.5 && moved <= 0.5 && min(clear) >= 4,
     `[2] [A] [+] on Modal's box line (Δ ${f2(boxD)} px); ‹ › sit on the name as the wavetable's do (Δ ${f2(arrD)} px); the pill moves nothing (${f2(moved)} px) and clears the ‹ by ${clear.map(f2).join(' / ')} px (Violin "Sus" / a 33-character name with "Stacc")`,
     `arrow − name baseline: Organics ${arrO.map(f2).join(', ')} · Wavetable ${arrW.map(f2).join(', ')}; boxes cy Organics ${f2(o.H.A.cy)}/${f2(o.H.plus.cy)} Modal ${f2(m.H.A.cy)}/${f2(m.H.plus.cy)} Wavetable ${f2(WT.a.H.A.cy)} (Δ ${f2(boxesVsWT)})`);
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
  ok(bxD <= 0.5 && o.rings.every(r => r.kdx != null) && kO <= Math.max(0.5, kM + 0.1),
     `[4] the picture box is Modal's (Δ ${f2(bxD)} px, ${f2(o.box.w)} × ${f2(o.box.h)}); ring numbers centred: Organics ≤ ${f2(kO)} px (Modal ≤ ${f2(kM)} px)`,
     o.rings.map(r => `"${r.t}" ${f2(r.kdx)},${f2(r.kdy)}`).join(' · ') + ' | modal ' + m.rings.map(r => `"${r.t}" ${f2(r.kdx)},${f2(r.kdy)}`).join(' · '));

  // the proof picture: Modal (A) and Organics (C, Violin), red lines through the measured ink
  await H.setEngine(p, 'a', 6); await H.sleep(600);
  const lines = [];
  const pre = await measure(p);
  [['a', mBase], ['c', null]].forEach(([k]) => { const d = pre[k], R = d.R;
    lines.push({ h: 1, x: R.left - 8, y: R.top + d.H.osc.base, w: R.w + 16 });                         // the header text baseline
    lines.push({ h: 1, x: R.left - 8, y: R.top + d.H.A.cy, w: R.w + 16 });                             // the box line
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
