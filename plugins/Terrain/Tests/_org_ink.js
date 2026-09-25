// ══════════════════════════════════════════════════════════════════════════════════════════════
//  _org_ink.js — tp105 THE HEADER CENTERLINE, measured on INK (Max: "draw a red line through the middle —
//  everything needs to run through the middle"). Shared by the headless gate (_org_align_gate.js) and the
//  real-WebView harness (mac_org_ui.mm + _org_real.js): the SAME page-side code runs in both, on a screenshot
//  (headless: puppeteer; real: WKWebView takeSnapshot, fed back to the page as base64).
//
//  The line: the OSC label's own ink — the middle between its cap top and its baseline (all capitals, so that is
//  the cap-height centre). Every element of the header is put on the same footing:
//    · text (the engine name, a pill, the instrument/preset name): baseline − capH·(its font-size / OSC's)/2 — the
//      middle of ITS cap height, read from its own ink baseline (descenders and ascenders cannot move it);
//    · boxes ([A] [+] [S] [N]), the ‹ › arrows and the separator dot: the middle of their ink bounding box.
//  Law: every centre within ±0.5 px of the line.
// ══════════════════════════════════════════════════════════════════════════════════════════════

// page side: the header's visible pieces of every oscillator named in `ids` (CSS px, viewport coordinates)
const COLLECT = function (ids) {
  const vis = e => { if (!e || !e.getClientRects().length) return false; const cs = getComputedStyle(e); if (cs.display === 'none' || cs.visibility === 'hidden' || +cs.opacity < 0.05) return false;
    let a = e; while (a && a !== document.body) { const c = getComputedStyle(a); if (c.display === 'none' || c.visibility === 'hidden' || +c.opacity < 0.05) return false; a = a.parentElement; } const r = e.getBoundingClientRect(); return r.width > 0.5 && r.height > 0.5; };
  const textRect = e => { const rg = document.createRange(); rg.selectNodeContents(e); const rs = [...rg.getClientRects()].filter(r => r.width > 0.3); if (!rs.length) return null;
    const r = { left: Math.min(...rs.map(q => q.left)), right: Math.max(...rs.map(q => q.right)), top: Math.min(...rs.map(q => q.top)), bottom: Math.max(...rs.map(q => q.bottom)) };
    const b = e.getBoundingClientRect(); return { left: Math.max(r.left, b.left), right: Math.min(r.right, b.right), top: r.top, bottom: r.bottom }; };
  const out = {};
  ids.forEach(id => { const d = document.getElementById(id); if (!d) return; const items = [];
    const R = d.getBoundingClientRect();
    const lab = d.querySelector('.device-label-row .device-label');
    if (lab && vis(lab)) { const tn = [...lab.childNodes].find(n => n.nodeType === 3 && n.textContent.trim()); if (tn) { const rg = document.createRange(); rg.selectNodeContents(tn); const r = rg.getBoundingClientRect();
      items.push({ key: 'osc', kind: 'text', fs: parseFloat(getComputedStyle(lab).fontSize), r: { left: r.left, right: r.right, top: r.top, bottom: r.bottom } }); }
      const sep = lab.querySelector('.lbl-sep'); if (vis(sep)) { const r = sep.getBoundingClientRect(); items.push({ key: 'dot', kind: 'box', r: { left: r.left - 1, right: r.right + 1, top: r.top - 2, bottom: r.bottom + 2 } }); } }
    const SEL = '.engine-display, .samp-sel-disp, .preset-display, .org-nav, .wt-nav, .samp-nav, .osc-letter, .swap-btn, .sn-chip';
    d.querySelectorAll('.device-label-row, .device-corner-preset').forEach(box => box.querySelectorAll(SEL).forEach(e => { if (!vis(e)) return;
      const isBox = e.matches('.osc-letter, .swap-btn, .sn-chip'), isNav = e.matches('.org-nav, .wt-nav, .samp-nav');
      const cls = e.matches('.engine-display') ? 'engine' : isBox ? (e.matches('.osc-letter') ? 'letter' : e.matches('.swap-btn') ? 'plus' : 'chip') : isNav ? 'nav' : e.matches('.samp-sel-disp') ? 'pill' : 'name';
      let r = isBox || isNav ? e.getBoundingClientRect() : textRect(e); if (!r) return;
      if (isNav) { const t = textRect(e) || r; r = { left: t.left - 0.5, right: t.right + 0.5, top: r.top - 1, bottom: r.bottom + 1 }; }
      items.push({ key: cls, kind: isBox || isNav ? 'box' : 'text', frame: isBox, fs: parseFloat(getComputedStyle(e).fontSize), txt: (e.textContent || '').trim().slice(0, 24), r: { left: r.left, right: r.right, top: r.top, bottom: r.bottom } }); }));
    out[id] = { R: { left: R.left, top: R.top, width: R.width, height: R.height }, items }; });
  return out;
};

// page side: the ink inside each item's rect, read from a screenshot (base64 PNG) at `dpr` device px per CSS px
const ANALYZE = async function (b64, groups, dpr) {
  const img = new Image(); img.src = 'data:image/png;base64,' + b64; await img.decode();
  const cv = document.createElement('canvas'); cv.width = img.width; cv.height = img.height;
  const cx = cv.getContext('2d', { willReadFrequently: true }); cx.drawImage(img, 0, 0); const D = cx.getImageData(0, 0, cv.width, cv.height).data, W = cv.width, Hh = cv.height;
  const lum = (x, y) => { if (x < 0 || y < 0 || x >= W || y >= Hh) return 0; const i = (y * W + x) * 4; return (D[i] + D[i + 1] + D[i + 2]) / 3; };
  const res = {};
  Object.keys(groups).forEach(id => { res[id] = groups[id].items.map(it => {
    const r = it.r, pad = 2, x0 = Math.floor(r.left * dpr) - pad, x1 = Math.ceil(r.right * dpr) + pad, y0 = Math.floor(r.top * dpr) - pad, y1 = Math.ceil(r.bottom * dpr) + pad;
    // the ground: the median of the rect's border pixels; the ink threshold sits a third of the way to the brightest pixel
    const edge = []; for (let x = x0; x < x1; x++) { edge.push(lum(x, y0), lum(x, y1 - 1)); } for (let y = y0; y < y1; y++) { edge.push(lum(x0, y), lum(x1 - 1, y)); }
    edge.sort((a, b) => a - b); const bg = edge[edge.length >> 1]; let mx = 0; for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) mx = Math.max(mx, lum(x, y));
    const th = bg + Math.max(12, (mx - bg) * 0.35);
    const rows = []; let xl = 1e9, xr = -1;
    for (let y = y0; y < y1; y++) { let n = 0; for (let x = x0; x < x1; x++) { const v = lum(x, y); if (v > th) { n += Math.min(1, (v - th) / Math.max(1, mx - th) * 2 + 0.2); if (x < xl) xl = x; if (x > xr) xr = x; } } rows.push([y, n]); }
    const inkRows = rows.filter(q => q[1] > 0); if (!inkRows.length) return Object.assign({}, it, { none: true });
    const m = Math.max(...rows.map(q => q[1])), band = rows.filter(q => q[1] >= m * 0.25);
    let fr = null;
    if (it.frame) {   // a box: its FRAME (any ink over the ground: the 1 px border) and its GLYPH (the bright ink inside, the border excluded)
      let ft = -1, fb = -1; for (let y = y0; y < y1; y++) { let hit = false; for (let x = x0; x < x1; x++) if (lum(x, y) > bg + 10) { hit = true; break; } if (hit) { if (ft < 0) ft = y; fb = y; } }
      const ix0 = Math.round(x0 + (x1 - x0) * 0.24), ix1 = Math.round(x1 - (x1 - x0) * 0.24), iy0 = Math.round(y0 + (y1 - y0) * 0.24), iy1 = Math.round(y1 - (y1 - y0) * 0.24);
      let gt = -1, gb = -1; for (let y = iy0; y < iy1; y++) { let hit = false; for (let x = ix0; x < ix1; x++) if (lum(x, y) > th) { hit = true; break; } if (hit) { if (gt < 0) gt = y; gb = y; } }
      fr = { ftop: ft / dpr, fbot: (fb + 1) / dpr, gtop: gt < 0 ? null : gt / dpr, gbot: gb < 0 ? null : (gb + 1) / dpr }; }
    return Object.assign({}, it, fr || {}, { top: inkRows[0][0] / dpr, bot: (inkRows[inkRows.length - 1][0] + 1) / dpr, base: (band[band.length - 1][0] + 1) / dpr,
      left: xl / dpr, right: (xr + 1) / dpr, bg, mx }); }); });
  return res;
};

// node side: the line and every element's distance from it
function centreline(items) {
  const osc = items.find(q => q.key === 'osc' && !q.none); if (!osc) return null;
  const capH = osc.base - osc.top, line = (osc.top + osc.base) / 2, out = [];
  items.forEach(q => { if (q.none) return;
    if (q.frame) { const f = (q.ftop + q.fbot) / 2; out.push({ key: q.key + ':box', c: f, d: f - line, top: q.ftop, bot: q.fbot, left: q.left, right: q.right });
      if (q.gtop != null) { const g = (q.gtop + q.gbot) / 2; out.push({ key: q.key + ':glyph', c: g, d: g - line, top: q.gtop, bot: q.gbot, left: q.left, right: q.right }); } return; }
    const c = q.kind === 'text' ? q.base - capH * ((q.fs || osc.fs) / osc.fs) / 2 : (q.top + q.bot) / 2;
    out.push({ key: q.key, txt: q.txt, c, d: c - line, base: q.base, top: q.top, bot: q.bot, left: q.left, right: q.right }); });
  return { line, capH, items: out, worst: Math.max(...out.map(q => Math.abs(q.d))) };
}
module.exports = { COLLECT, ANALYZE, centreline };
