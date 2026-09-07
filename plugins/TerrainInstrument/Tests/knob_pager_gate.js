// ══════════════════════════════════════════════════════════════════════════════════════════════
//  knob_pager_gate.js — rs2: EVERY KNOB ROW HAS ‹ AND ›, MIRRORED ABOUT THE ROW, AND NOT ONE
//                       KNOB MOVES.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/knob_pager_gate.js [page.html] [--baseline knobs.json]
//
//  Max: "I need a set of arrows, not just one arrow to the right. An arrow to the left ... every
//  engine needs to have a left and a right arrow ... spaced out properly and both in the middle,
//  both centered ... no changes on the button size or the button spacing, it's all perfect ...
//  universal so I can move between the sets."
//
//  Like harm_header_gate.js (fb595) this measures nothing but rectangles — and, because a CSS-border
//  chevron has no text node to Range-measure, it also reads the ‹ › GLYPH INK back out of a
//  screenshot (png.js) and checks the ink is mirrored and on the row's centreline.
//  8 engine cases (Wavetable, Sample, Sample+blend, Granular, Resynth, Harmonic, Modal, FM) × every
//  set the pager can show = 23 rows; every row measured.
//
//  THE BARS
//   0  THE PANEL ACTUALLY LAID OUT — 8 cases, 23 rows, every row has a live wrap
//   1  TWO ARROWS ON EVERY SET — exactly one ‹ and one › visible on every row of every engine
//      (RED on the shipped page: one › per row, no ‹ anywhere)
//   2  THE SAME ARROW — ‹ is › cloned: same box, same chevron, same opacity, same y, on every row
//   3  MIRRORED ABOUT THE ROW — the pair's centre IS the knob grid's centre (≤ 0.25 px); the ink of
//      ‹ is the mirror image of the ink of › (≤ 0.75 px) and both sit on the knobs' centreline
//   4  NOT ONE KNOB MOVES — every knob rect on every row is identical with the ‹ hidden (the
//      ‹ costs the grid zero pixels); with --baseline, identical to the recorded page as well
//   5  ‹ FROM SET 1 LANDS ON THE LAST SET — and one more ‹ steps back one more; › from the last
//      set wraps to set 1 (the fb538 cycle, unchanged)
//   6  THE CYCLE IS UNCHANGED — WT 2, Sample 2 (3 with blend), Granular/Resynth/Harmonic/Modal 3,
//      FM 4, unison always last
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path'), fs = require ('fs');
const puppeteer = require ('puppeteer-core');
const png = require (fs.existsSync (path.join (__dirname, 'png.js')) ? path.join (__dirname, 'png.js') : path.join (__dirname, '../Tests/png.js'));
const args = process.argv.slice (2);
const PAGE = (args.find (a => ! a.startsWith ('--')) ) || path.resolve (__dirname, '../Source/ui/public/index.html');
const BASE = args.includes ('--baseline') ? args[args.indexOf ('--baseline') + 1] : null;
const DUMP = args.includes ('--dump') ? args[args.indexOf ('--dump') + 1] : null;

const STUB = () => {
  const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
    getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
      if(/Json|JSON/.test(n))return r('{}'); r(0);}),
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const m=window.Juce;let h=m;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return h;},set(v){h=Object.assign({},v||{},{getNativeFunction:m.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
};

let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail;
  console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

// ── in-page helpers (stringified into evaluate) ─────────────────────────────────────────────
const HELPERS = `
  const dev = document.querySelector ('#syn-panel .device.osc');
  const devR = () => dev.getBoundingClientRect();
  const R = (el) => { const q = el.getBoundingClientRect(), d = devR();
    return { x: +(q.left - d.left).toFixed (2), y: +(q.top - d.top).toFixed (2), w: +q.width.toFixed (2), h: +q.height.toFixed (2) }; };
  const ABS = (el) => { const q = el.getBoundingClientRect(); return { x: q.left, y: q.top, w: q.width, h: q.height }; };
  const vis = (el) => { const cs = getComputedStyle (el);
    if (cs.display === 'none' || cs.visibility === 'hidden' || parseFloat (cs.opacity) === 0) return false;
    const q = el.getBoundingClientRect(); return q.width > 0 && q.height > 0; };
  const visDeep = (el) => { for (let e = el; e && e !== dev; e = e.parentElement) if (getComputedStyle (e).display === 'none') return false; return vis (el); };
  const ARROW = '.uni-arrow, .gk-arrow, .fm-arrow, .harm-arrow, .modal-arrow, .geode-arrow, .bl-arrow';
  const WRAPS = '.wt-knob-wrap, .uni-knob-wrap, .fm-knob-wrap, .harm-knob-wrap, .modal-knob-wrap, .samp-knob-wrap, .gran-knob-wrap, .geode-knob-wrap';
  const ENG = ['engine-sample','engine-granular','engine-geode','engine-harm','engine-modal','engine-fm'];
  const CASES = [ { name: 'Wavetable', cls: null, expect: 2 }, { name: 'Sample', cls: 'engine-sample', expect: 2 }, { name: 'Sample+blend', cls: 'engine-sample', blend: true, expect: 3 },
                  { name: 'Granular', cls: 'engine-granular', expect: 3 }, { name: 'Resynth', cls: 'engine-geode', expect: 3 }, { name: 'Harmonic', cls: 'engine-harm', expect: 3 },
                  { name: 'Modal', cls: 'engine-modal', expect: 3 }, { name: 'FM', cls: 'engine-fm', expect: 4 } ];
  const put = (id, t) => { const e = document.getElementById (id); if (e) e.textContent = t; };
  const setEngine = (c) => { ENG.forEach (k => dev.classList.remove (k)); dev.classList.remove ('harm-table'); dev.classList.remove ('blend-on');
    if (c.cls) dev.classList.add (c.cls); if (c.blend) dev.classList.add ('blend-on'); put ('osc-a-engine-display', c.name.replace ('+blend', '')); };
  const arrowsNow = () => [...dev.querySelectorAll (ARROW)].filter (visDeep);
  const fwdNow = () => arrowsNow().filter (a => ! a.classList.contains ('set-prev'))[0];
  const prevNow = () => arrowsNow().filter (a => a.classList.contains ('set-prev'))[0];
  const home = () => { let g = 0; while (dev.dataset.setIdx !== '0' && g++ < 8) { const f = fwdNow(); if (! f) break; f.click(); } };
  const snapshot = () => {
    const wraps = [...dev.querySelectorAll (WRAPS)].filter (visDeep); const w = wraps[0]; if (! w) return { wraps: wraps.length };
    const grid = [...w.querySelectorAll (':scope > .osc-knobs')].filter (visDeep)[0];
    const knobs = grid ? [...grid.children].filter (visDeep).map (k => ({ id: k.dataset.syn || (k.className || '').split (/\\s+/)[0], ...R (k) })) : [];
    const arrows = [...w.querySelectorAll (ARROW)].filter (visDeep).map (a => { const cs = getComputedStyle (a, '::before');
      return { cls: a.className, prev: a.classList.contains ('set-prev'), ...R (a), abs: ABS (a), rot: cs.transform, bm: cs.margin, bw: cs.borderRightWidth + '/' + cs.borderTopWidth, gw: cs.width + 'x' + cs.height, op: getComputedStyle (a).opacity }; });
    return { wraps: wraps.length, wrap: (w.className || '').split (/\\s+/)[0], wrapR: R (w), gridR: grid ? R (grid) : null, knobs, arrows, idx: dev.dataset.setIdx, uni: dev.classList.contains ('uni-page') };
  };
`;

(async () => {
  const b = await puppeteer.launch ({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox','--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport ({ width: 820, height: 656, deviceScaleFactor: 2 });
  await p.evaluateOnNewDocument (STUB);
  await p.goto ('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise (r => setTimeout (r, 1600));
  await p.evaluate (() => { const sp = document.getElementById ('syn-panel');
                            if (sp) sp.style.display = 'block'; window.dispatchEvent (new Event ('resize')); });
  await new Promise (r => setTimeout (r, 2400));

  // ── phase 1: rectangles, every engine, every set ───────────────────────────────────────────
  const r = await p.evaluate (new Function (HELPERS + `
    const out = { err: null, engines: [] };
    try {
      if (! dev) { out.err = 'no osc device'; return out; }
      out.devW = devR().width;
      for (const c of CASES) {
        setEngine (c); home ();
        const e = { name: c.name, expect: c.expect, pages: [] };
        let guard = 0;
        do { e.pages.push (snapshot ()); const f = fwdNow(); if (! f) { e.noFwd = true; break; } f.click(); }
        while (dev.dataset.setIdx !== '0' && ++guard < 8);
        e.pageCount = e.pages.length;
        e.uniLast = e.pages.every ((s, i) => s.uni === (i === e.pages.length - 1));
        const pv = prevNow(); e.hasPrev = !! pv;
        if (pv) { pv.click(); e.prevLandsOn = +dev.dataset.setIdx; const pv2 = prevNow(); if (pv2) pv2.click(); e.prevTwice = +dev.dataset.setIdx; home (); }
        // THE DECIDING PROPERTY: hide every ‹ and re-measure every row's knobs
        const prevs = [...dev.querySelectorAll ('.set-prev')];
        const before = e.pages.map (s => JSON.stringify (s.knobs));
        prevs.forEach (a => a.style.display = 'none');
        const after = []; for (let i = 0; i < e.pages.length; ++i) { after.push (JSON.stringify (snapshot ().knobs)); const f = fwdNow(); if (f) f.click(); }
        prevs.forEach (a => a.style.display = '');
        e.knobsIdenticalWithoutPrev = before.every ((s, i) => s === after[i]);
        home ();
        out.engines.push (e);
      }
    } catch (e) { out.err = String (e && e.stack || e); }
    return out;`));

  console.log (`\n══ knob_pager_gate — rs2 ══  ${PAGE}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }
  const rows = []; r.engines.forEach (e => e.pages.forEach ((s, i) => rows.push ({ eng: e.name, i, ...s })));
  const laidOut = r.devW > 100 && r.engines.length === 8 && rows.length === 23 && rows.every (s => s.wraps === 1 && s.gridR);
  gate (laidOut, '[0] THE PANEL ACTUALLY LAID OUT — 8 engine cases, 23 knob rows, one live wrap each',
        `osc width ${(r.devW||0).toFixed (1)} px, ${r.engines.length} cases, ${rows.length} rows (${r.engines.map (e => e.name + ':' + e.pageCount).join (' ')})`);
  if (! laidOut) { console.log ('\n  ❌ degenerate page — not asserting anything on it\n'); await b.close(); process.exit (1); }

  // [1] two arrows on every row
  const bad1 = rows.filter (s => s.arrows.filter (a => a.prev).length !== 1 || s.arrows.filter (a => ! a.prev).length !== 1);
  gate (bad1.length === 0, '[1] TWO ARROWS ON EVERY SET — exactly one ‹ and one › on every row of every engine',
        bad1.length ? `${bad1.length}/23 rows wrong: e.g. ${bad1[0].eng} set ${bad1[0].i + 1} has ${bad1[0].arrows.filter (a => a.prev).length} ‹ and ${bad1[0].arrows.filter (a => ! a.prev).length} › (${bad1[0].arrows.map (a => a.cls).join (', ') || 'none'})`
                    : `23/23 rows carry ‹ and ›`);
  if (bad1.length) { console.log (`\n  ❌ ${pass} passed, ${fail} failed — no ‹ to measure; the remaining bars need the pair\n`); await b.close(); process.exit (1); }

  // [2] the same arrow
  const L = (s) => s.arrows.find (a => a.prev), Rr = (s) => s.arrows.find (a => ! a.prev);
  const bad2 = rows.filter (s => { const l = L (s), q = Rr (s);
    return l.w !== q.w || l.h !== q.h || l.y !== q.y || l.op !== q.op || l.bw !== q.bw || l.gw !== q.gw
        || l.cls.replace (/\s*set-prev\s*/, ' ').trim() !== q.cls.trim() && ! (s.wrap === 'samp-knob-wrap'); });
  gate (bad2.length === 0, '[2] THE SAME ARROW — ‹ is › cloned: same box, chevron, opacity and y on every row',
        bad2.length ? `${bad2.length} rows differ: e.g. ${bad2[0].eng} set ${bad2[0].i + 1}: ‹ ${L (bad2[0]).w}×${L (bad2[0]).h} y${L (bad2[0]).y} op${L (bad2[0]).op} ${L (bad2[0]).bw} vs › ${Rr (bad2[0]).w}×${Rr (bad2[0]).h} y${Rr (bad2[0]).y} op${Rr (bad2[0]).op} ${Rr (bad2[0]).bw}`
                    : `every row: ${L (rows[0]).w}×${L (rows[0]).h} boxes, chevron ${L (rows[0]).gw} border ${L (rows[0]).bw}, y=${L (rows[0]).y}, opacity ${[...new Set (rows.map (s => L (s).op))].join ('/')}`);

  // ── phase 2: the INK — screenshot both arrows on every row ──────────────────────────────────
  const inks = [];
  for (const c of r.engines) {
    for (let i = 0; i < c.pageCount; ++i) {
      const st = await p.evaluate (new Function (HELPERS + `
        const c = CASES.find (x => x.name === ${JSON.stringify (c.name)}); setEngine (c); home ();
        for (let k = 0; k < ${i}; ++k) { const f = fwdNow(); if (f) f.click(); }
        const s = snapshot (); return { arrows: s.arrows.map (a => ({ prev: a.prev, abs: a.abs, box: { x: a.x, y: a.y, w: a.w, h: a.h } })), grid: s.gridR, knobs: s.knobs };`));
      const row = { eng: c.name, i, grid: st.grid, ink: {} };
      for (const a of st.arrows) {
        const clip = { x: a.abs.x, y: a.abs.y, width: a.abs.w, height: a.abs.h };
        const buf = await p.screenshot ({ clip, encoding: 'binary' });
        const img = png.decode (Buffer.from (buf)); const k = png.ink (img);
        const dpr = img.w / a.abs.w;
        row.ink[a.prev ? 'L' : 'R'] = k ? { x0: k.x0 / dpr, x1: k.x1 / dpr, y0: k.y0 / dpr, y1: k.y1 / dpr, cx: k.cx / dpr, cy: k.cy / dpr, bg: k.bg, box: a.box } : null;
      }
      inks.push (row);
    }
  }
  if (DUMP) fs.writeFileSync (DUMP, JSON.stringify ({ rows, inks }, null, 1));

  // [3] mirrored about the row: boxes AND ink
  const cx = (q) => q.x + q.w / 2, cy = (q) => q.y + q.h / 2;
  const bad3 = [];
  rows.forEach ((s, n) => { const l = L (s), q = Rr (s), g = s.gridR, k = inks[n].ink;
    const pairMid = (cx (l) + cx (q)) / 2, gmid = cx (g);
    const why = [];
    if (Math.abs (pairMid - gmid) > 0.25) why.push (`pair centre ${pairMid.toFixed (2)} vs grid centre ${gmid.toFixed (2)}`);
    if (! k.L || ! k.R) why.push ('no ink read');
    else {
      // ink measured inside each 12×20 box; mirror: L.x0 from its left edge == box.w − R.x1 (from the right edge)
      const dxL = k.L.x0, dxR = k.R.box.w - k.R.x1, wL = k.L.x1 - k.L.x0, wR = k.R.x1 - k.R.x0;
      if (Math.abs (dxL - dxR) > 0.75) why.push (`ink inset ‹ ${dxL.toFixed (2)} vs › ${dxR.toFixed (2)}`);
      if (Math.abs (wL - wR) > 0.75 || Math.abs ((k.L.y1 - k.L.y0) - (k.R.y1 - k.R.y0)) > 0.75) why.push (`ink size ‹ ${wL.toFixed (1)}×${(k.L.y1 - k.L.y0).toFixed (1)} vs › ${wR.toFixed (1)}×${(k.R.y1 - k.R.y0).toFixed (1)}`);
      const rowMid = cy (g), yL = k.L.box.y + (k.L.y0 + k.L.y1) / 2, yR = k.R.box.y + (k.R.y0 + k.R.y1) / 2;
      if (Math.abs (yL - rowMid) > 1.0 || Math.abs (yR - rowMid) > 1.0) why.push (`ink centre y ‹ ${yL.toFixed (2)} › ${yR.toFixed (2)} vs row centre ${rowMid.toFixed (2)}`);
      // and the ink of the ‹ mirrors the ink of the › about the GRID centre
      const inkLc = k.L.box.x + (k.L.x0 + k.L.x1) / 2, inkRc = k.R.box.x + (k.R.x0 + k.R.x1) / 2;
      if (Math.abs ((inkLc + inkRc) / 2 - gmid) > 0.75) why.push (`ink pair centre ${((inkLc + inkRc) / 2).toFixed (2)} vs grid centre ${gmid.toFixed (2)}`);
    }
    if (why.length) bad3.push (`${s.eng} set ${s.i + 1}: ${why.join ('; ')}`); });
  const k0 = inks[0].ink, g0 = rows[0].gridR;
  gate (bad3.length === 0, '[3] MIRRORED ABOUT THE ROW — box pair centred on the grid, ink mirrored, ink on the knobs\' centreline',
        bad3.length ? `${bad3.length} rows: e.g. ${bad3[0]}` :
        `every row: ‹ box [${L (rows[0]).x}..${L (rows[0]).x + L (rows[0]).w}] › [${Rr (rows[0]).x}..${Rr (rows[0]).x + Rr (rows[0]).w}] about grid centre ${cx (g0).toFixed (2)}; ink ‹ inset ${k0.L.x0.toFixed (2)} / › inset ${(k0.R.box.w - k0.R.x1).toFixed (2)} px, ink ${(k0.L.x1 - k0.L.x0).toFixed (1)}×${(k0.L.y1 - k0.L.y0).toFixed (1)} px, ink centre y ${(k0.L.box.y + (k0.L.y0 + k0.L.y1) / 2).toFixed (2)} vs row ${cy (g0).toFixed (2)}`);

  // [4] not one knob moves
  const base = BASE ? JSON.parse (fs.readFileSync (BASE, 'utf8')) : null;
  let baseOK = true, baseNote = 'no --baseline given';
  if (base) { const bk = base.engines ? base.engines.flatMap (e => e.pages.map (s => JSON.stringify (s.knobs))) : base;
    const now = rows.map (s => JSON.stringify (s.knobs)); baseOK = bk.length === now.length && bk.every ((s, i) => s === now[i]);
    baseNote = baseOK ? `all ${now.length} rows byte-identical to the baseline` : `differs from the baseline on ${now.filter ((s, i) => s !== bk[i]).length} rows`; }
  const bad4 = r.engines.filter (e => ! e.knobsIdenticalWithoutPrev);
  const kx = rows.find (s => s.eng === 'Resynth').knobs.map (k => k.x).join (' ');
  gate (bad4.length === 0 && baseOK, '[4] NOT ONE KNOB MOVES — every knob rect identical with the ‹ hidden, on every row of every engine',
        bad4.length ? `knobs moved on: ${bad4.map (e => e.name).join (', ')}` : `identical on all 8 cases (Resynth knob x: ${kx}); ${baseNote}`);

  // [5] ‹ from set 1 lands on the last set
  const bad5 = r.engines.filter (e => ! (e.hasPrev && e.prevLandsOn === e.pageCount - 1 && e.prevTwice === ((e.pageCount - 2) + e.pageCount) % e.pageCount));
  gate (bad5.length === 0, '[5] ‹ FROM SET 1 LANDS ON THE LAST SET — and one more ‹ steps back one more',
        bad5.length ? `wrong on ${bad5.map (e => `${e.name} (‹→${e.prevLandsOn} of ${e.pageCount}, ‹‹→${e.prevTwice})`).join (', ')}`
                    : r.engines.map (e => `${e.name} ‹→set ${e.prevLandsOn + 1}/${e.pageCount}`).join (' · '));

  // [6] the cycle is unchanged
  const bad6 = r.engines.filter (e => e.pageCount !== e.expect || ! e.uniLast);
  gate (bad6.length === 0, '[6] THE CYCLE IS UNCHANGED — WT 2, Sample 2/3, Granular·Resynth·Harmonic·Modal 3, FM 4, unison last',
        bad6.length ? `wrong: ${bad6.map (e => `${e.name} ${e.pageCount} sets (want ${e.expect}) uniLast=${e.uniLast}`).join (', ')}`
                    : r.engines.map (e => `${e.name}:${e.pageCount}`).join (' '));

  console.log (`\n  ${fail ? '❌' : '✅'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail ? 1 : 0);
})();
