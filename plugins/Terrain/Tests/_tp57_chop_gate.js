// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp57 — MAX'S CHOP PASS, MEASURED.
//  Every bar drives the real control and reads what it SENT or what the box actually measures.
//    node Tests/_tp57_chop_gate.js
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
const fs = require('fs'), path = require('path');
const sim = fs.readFileSync(process.cwd() + '/Tests/_ui_lockin_sim.js', 'utf8');
const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments'));
const cpp = fs.readFileSync('Source/PluginEditor.cpp', 'utf8');
const i0 = cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0 = cpp.indexOf('html = html.replace ("</body>", heroOverlay', i0);
const ov = [...cpp.slice(i0, j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m => m[1]).join('');
const html = fs.readFileSync('Source/ui/public/index.html', 'utf8').replace('</body>', ov + '</body>');
const PAGE = path.join(require('os').tmpdir(), 'tp57_chop_gate.html'); fs.writeFileSync(PAGE, html);

let pass = 0, fail = 0;
const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l + (d ? '\n        ' + d : '')); }
                          else { fail++; console.log('  FAIL  ' + l + (d ? '\n        ' + d : '')); } };
const spy = () => { const bridge = window.Juce, orig = bridge.getNativeFunction; window.__natLog = [];
  const wrapped = (n) => { const f = orig(n); return function () {
    window.__natLog.push([n].concat([].slice.call(arguments))); return f.apply(null, arguments); }; };
  let held = Object.assign({}, bridge, { getNativeFunction: wrapped });
  Object.defineProperty(window, 'Juce', { configurable: true, get() { return held; },
    set(v) { held = Object.assign({}, v || {}, { getNativeFunction: wrapped, getSliderState: bridge.getSliderState }); } }); };
const fakeSample = () => { const N = 900, mn = [], mx = [];
  for (let i = 0; i < N; i++) { const e = 0.35 + 0.5 * Math.abs(Math.sin(i / 70)); mx.push(e); mn.push(-e); }
  window.onSampleLoaded({ filename: 'Pad 138.wav', lengthSamples: 220500, peaksMin: mn, peaksMax: mx, rootMidiNote: 60 }); };

(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 });
  await p.evaluateOnNewDocument(stubSrc + '\nstub();\n(' + spy.toString() + ')();');
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + PAGE, { waitUntil: 'load' }); await sleep(2500);
  await p.evaluate(() => document.documentElement.setAttribute('data-theme', 'dark'));
  await p.evaluate(() => document.getElementById('mix-btn').click()); await sleep(1500);
  await p.evaluate(fakeSample); await sleep(800);
  await p.evaluate(() => { const q = document.querySelectorAll('#ti-mode-toggle .ti-mode-pill'); if (q[1]) q[1].dispatchEvent(new MouseEvent('click', { bubbles: true })); }); await sleep(900);

  // ── [0] THE BPM IS THE BIG WHITE ULTRA-THIN NUMBER ───────────────────────────────────────
  const bpm = await p.evaluate(() => { const v = document.querySelector('.ti-bpm-value'); const cs = getComputedStyle(v);
    return { size: parseFloat(cs.fontSize), weight: cs.fontWeight, colour: cs.color, num: cs.fontVariantNumeric,
             text: v.textContent }; });
  ok(bpm.size >= 14 && +bpm.weight <= 300 && /255,\s*255,\s*255/.test(bpm.colour) && /tabular/.test(bpm.num),
     '[0] THE BPM IS BIG, WHITE AND ULTRA-THIN — it was 10 px, weight 700 and PURPLE', JSON.stringify(bpm));

  // ── [1] 🚨 THE LOCK IS BACK AND IT MOVES THE PARAMETER ───────────────────────────────────
  await p.evaluate(() => { window.__natLog.length = 0; });
  const lock = await p.evaluate(async () => {
    const lk = document.getElementById('ti-bpm-lock');
    if (!lk) return { err: 'no lock' };
    const shown = getComputedStyle(lk).display !== 'none' && lk.getBoundingClientRect().width > 4;
    lk.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    await new Promise(r => setTimeout(r, 200));
    const sent = window.__natLog.filter(e => e[0] === 'setTiBpmLock');
    return { shown, on: lk.classList.contains('on'), sent: sent.map(e => e[1]),
             colour: getComputedStyle(lk).color };
  });
  ok(lock.shown && lock.on === true && lock.sent.length === 1 && lock.sent[0] === 1,
     '[1] 🚨 THE LOCK IS VISIBLE AND A CLICK SETS TI_BPM_LOCK — tp51 had hidden it because it did nothing',
     JSON.stringify(lock));
  ok(/183,\s*148,\s*255/.test(lock.colour), '[1b] AND IT GOES PURPLE WHEN IT IS ON', lock.colour);

  // ── [2] ARM NEVER SAYS "ARMED" ───────────────────────────────────────────────────────────
  const arm = await p.evaluate(async () => {
    const a = document.getElementById('ti-arm'); if (!a) return { err: 'no arm' };
    const before = a.querySelector('.t').textContent;
    a.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    await new Promise(r => setTimeout(r, 250));
    return { before, after: a.querySelector('.t').textContent, on: a.classList.contains('on'),
             dot: getComputedStyle(a.querySelector('.dot')).backgroundColor,
             trans: getComputedStyle(a).transitionProperty };
  });
  ok(arm.before === 'Arm' && arm.after === 'Arm' && arm.on === true && /183,\s*148,\s*255/.test(arm.dot),
     '[2] ARM KEEPS ITS WORD — the dot and the outline carry the state, and both fade', JSON.stringify(arm));

  // ── [3] THE SLICE COUNT IS WHITE AND ULTRA-THIN ──────────────────────────────────────────
  /* ⚠️ THE SIM HAS NO SLICER. The grid pill asks C++ for the new list through `getSlicesJson`,
     and the stub answers '[]' to every get — so a bar that only clicks the pill measures an empty
     count and says nothing about its treatment. The stub is taught to answer with eight real
     slices instead, and the pill is clicked for real; everything downstream is the shipped path. */
  const slices = await p.evaluate(async () => {
    /* the shape applySlicesJson actually parses: { slices: [...] }, as a STRING */
    const mk = () => JSON.stringify({ slices: Array.from({ length: 8 }, (_, i) => ({
      start: i * 12000, end: (i + 1) * 12000, pitch: 0, volume: 1, attackMs: 1, decayMs: 0,
      sustainLevel: 1, releaseMs: 20, reverse: false, warpMode: 0, stretchRatio: 1 })) });
    const bridge = window.Juce, orig = bridge.getNativeFunction;
    /* the grid pill's own door is `gridSliceSlices`; `getSlicesJson` is the layer-switch pull */
    const wrapped = (n) => ((n === 'getSlicesJson' || n === 'gridSliceSlices') ? (() => Promise.resolve(mk())) : orig(n));
    Object.defineProperty(window, 'Juce', { configurable: true, value: Object.assign({}, bridge, { getNativeFunction: wrapped }) });
    const g = document.querySelector('#ti-grid-row .ti-grid-pill[data-n="8"]');
    if (g) g.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    await new Promise(r => setTimeout(r, 700));
    const c = document.getElementById('ti-slices-count'); const cs = getComputedStyle(c);
    return { text: c.textContent, weight: cs.fontWeight, colour: cs.color, shown: cs.display !== 'none' };
  });
  ok(slices.shown && slices.text === '8' && /255,\s*255,\s*255/.test(slices.colour) && +slices.weight <= 300,
     '[3] THE SLICE COUNT IS A WHITE ULTRA-THIN NUMBER — it was purple and 700', JSON.stringify(slices));

  // ── [4] ONE TYPEFACE ACROSS THE PAGE ─────────────────────────────────────────────────────
  //  Max: "I think we're literally not using the same fonts here, so double check for everything."
  const fonts = await p.evaluate(() => {
    const fams = {};
    document.querySelectorAll('#hero *, #mix-panel *, #ti-chop-panel *, #ti-slicer-drawer *').forEach(e => {
      if (!e.textContent || !e.getBoundingClientRect().width) return;
      const f = getComputedStyle(e).fontFamily.split(',')[0].replace(/["']/g, '').trim();
      fams[f] = (fams[f] || 0) + 1; });
    return fams;
  });
  const famNames = Object.keys(fonts);
  ok(famNames.length === 1 && /apple-system/.test(famNames[0]),
     '[4] EVERY VISIBLE THING ON THE PAGE NAMES ONE FAMILY', JSON.stringify(fonts));

  // ── [5] 🚨 NOTHING SITS ON AN EDGE ───────────────────────────────────────────────────────
  //  Max: "make sure that none of these numbers or texts inside the boxes are too close to the
  //  bottom or the top, to the left or the right ... the slices boxes be pissing me off."
  //  Measured as the gap between a pill's own box and the ink inside it, on all four sides.
  const crowd = await p.evaluate(() => {
    const sel = '.ti-mode-pill, .ti-play-pill, .ti-layer-pad, #ti-arm, #ti-slices-btn, #mix-panel .trigger-pill, #mix-panel .layer-status-dot';
    const bad = [];
    document.querySelectorAll(sel).forEach(el => {
      const r = el.getBoundingClientRect(); if (r.width < 4 || r.height < 4) return;
      const rng = document.createRange(); rng.selectNodeContents(el);
      const t = rng.getBoundingClientRect(); if (!t.width || !t.height) return;
      const g = { l: t.left - r.left, r: r.right - t.right, t: t.top - r.top, b: r.bottom - t.bottom };
      const min = Math.min(g.l, g.r, g.t, g.b);
      // vertical symmetry matters as much as the margin: a digit 3 px from the top and 1 from the
      // bottom is the thing he is pointing at
      const skew = Math.abs(g.t - g.b);
      if (min < 1.5 || skew > 2.0) bad.push({ el: (el.id || el.className).slice(0, 26), txt: el.textContent.trim().slice(0, 10),
        g: [+g.l.toFixed(1), +g.r.toFixed(1), +g.t.toFixed(1), +g.b.toFixed(1)] });
    });
    return bad;
  });
  ok(crowd.length === 0, '[5] 🚨 NO LABEL TOUCHES ITS BOX — every pill has margin on all four sides and sits level',
     crowd.length ? JSON.stringify(crowd.slice(0, 5)) : 'every pill checked, none crowded');

  // ── [6] RR IS ROBIN ──────────────────────────────────────────────────────────────────────
  const words = await p.evaluate(() => [...document.querySelectorAll('#trigger-pills .trigger-pill')].map(e => e.textContent.trim()));
  ok(words.join('/') === 'Layer/Robin/Random/Keytrack/Velocity', '[6] RR IS "ROBIN"', JSON.stringify(words));

  // ── [7] 🚨 THE TRIGGER PANE FILLS, AND ITS TOP MATCHES ITS BOTTOM ───────────────────────
  const fillM = await p.evaluate(() => {
    const box = (s) => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect();
      return { t: r.top, b: r.bottom, l: r.left, r: r.right, w: r.width, h: r.height }; };
    const area = box('#mix-trigger-area'), pills = box('#trigger-pills'), morph = box('#morph-track');
    const stem = box('#mix-stem-area'), sBtn = box('.stem-buttons');
    return { padTop: +(pills.t - area.t).toFixed(1), padBot: +(area.b - morph.b).toFixed(1),
             triggerW: +pills.w.toFixed(1), stemW: +sBtn.w.toFixed(1),
             trigL: +pills.l.toFixed(1), stemL: +sBtn.l.toFixed(1) };
  });
  ok(Math.abs(fillM.padTop - fillM.padBot) <= 3,
     '[7] 🚨 THE TOP OF THE TRIGGER PANE MATCHES ITS BOTTOM — no dead space under the slider',
     'top ' + fillM.padTop + ' px, bottom ' + fillM.padBot + ' px');
  ok(Math.abs(fillM.triggerW - fillM.stemW) < 1 && Math.abs(fillM.trigL - fillM.stemL) < 1,
     '[8] THE STEM PANE\'S BOXES MATCH THE TRIGGER PANE\'S — same left edge, same width', JSON.stringify(fillM));

  // ── [9] 🚨 THE MORPH SLIDER IS UNDER THE POINTER AND REACHES BOTH ENDS ──────────────────
  const morph = await p.evaluate(() => {
    const track = document.getElementById('morph-track'), h = document.getElementById('morph-handle');
    const tr = track.getBoundingClientRect();
    const at = (frac) => { track.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, clientX: tr.left + tr.width * frac, clientY: tr.top + 1 }));
      document.dispatchEvent(new MouseEvent('mouseup', { bubbles: true }));
      const hr = h.getBoundingClientRect();
      return { want: +(tr.left + tr.width * frac).toFixed(1), got: +((hr.left + hr.right) / 2).toFixed(1) }; };
    return { left: at(0), mid: at(0.5), right: at(1) };
  });
  const off = [morph.left, morph.mid, morph.right].map(o => Math.abs(o.got - o.want));
  ok(Math.max(...off) <= 2.0,
     '[9] 🚨 THE HANDLE IS WHERE THE POINTER IS, AT BOTH ENDS AND THE MIDDLE — it was up to an eighth of the track away and could never reach either end',
     'offsets ' + off.map(v => v.toFixed(1)).join(' / ') + ' px');

  // ── [10] EXPORT AND REVEAL ARE EMBLEMS THAT STILL SAY WHAT THEY ARE ─────────────────────
  const emb = await p.evaluate(() => [...document.querySelectorAll('.stem-all-row > button')].map(e => ({
    svg: !!e.querySelector('svg'), text: e.textContent.trim(), title: e.title })));
  ok(emb.length === 2 && emb.every(e => e.svg && e.text === '' && e.title.length > 4),
     '[10] EXPORT ALL 4 AND REVEAL FOLDER ARE EMBLEMS, and a hover still says which is which', JSON.stringify(emb));

  // ── [11] THE STEM STATUS CLEARS ITSELF ──────────────────────────────────────────────────
  const status = await p.evaluate(async () => {
    const el = document.getElementById('stem-status');
    // reach the real setter through a drag start, the message that used to stick forever
    const btn = document.querySelector('.stem-buttons > button');
    btn.dispatchEvent(new MouseEvent('dragstart', { bubbles: true }));
    await new Promise(r => setTimeout(r, 120));
    const during = el.textContent.trim();
    await new Promise(r => setTimeout(r, 3200));
    return { during, after: el.textContent.trim(), op: getComputedStyle(el).opacity };
  });
  ok(status.after === '' || +status.op < 0.05,
     '[11] THE STEM LINE GOES AWAY — "it just stays there" was a message with no clearing path at all',
     JSON.stringify(status));

  // ── [12] THE STRIP KNOBS ARE THE HOUSE ARC, WITH THEIR NUMBER INSIDE ───────────────────
  const knobs = await p.evaluate(() => {
    const k = [...document.querySelectorAll('.mix-strip[data-layer="0"] .mix-strip-knob')];
    return k.map(e => ({ fn: e.dataset.fn, svg: !!e.querySelector('svg.kr-svg'), arc: !!e.querySelector('.kr-v'),
      val: (e.querySelector('.kv') || {}).textContent || null,
      bg: getComputedStyle(e).backgroundImage, mask: getComputedStyle(e).webkitMaskImage || 'none' }));
  });
  ok(knobs.length === 3 && knobs.every(k => k.svg && k.arc && k.val !== null && k.val !== ''
       && !/conic/.test(k.bg) && !/gradient/.test(k.mask)),
     '[12] 🚨 THE KNOBS ARE THE SYNTH PAGE\'S SVG ARC AND CARRY THEIR VALUE — no conic gradient, no radial mask (that banding IS the "tearing")',
     JSON.stringify(knobs.map(k => k.fn + '=' + k.val)));

  // ── [13] 🚨 THE CROSSFADE REACHES THE ENGINE ───────────────────────────────────────────
  //  Max: "whenever you press the slices mode, double check the fader, the crossfade. I feel like
  //  it doesn't work."  Drive the real input the way a hand does and read what it SENT.
  await p.evaluate(() => { window.__natLog.length = 0; });
  const fade = await p.evaluate(async () => {
    const btn = document.getElementById('ti-slices-btn');
    if (btn) btn.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    await new Promise(r => setTimeout(r, 400));
    const sl = document.getElementById('ti-fade-slider');
    if (!sl) return { err: 'no fade slider' };
    const r = sl.getBoundingClientRect();
    const vis = getComputedStyle(sl).display !== 'none' && r.width > 10;
    sl.value = '22.5';
    sl.dispatchEvent(new Event('input', { bubbles: true }));
    await new Promise(x => setTimeout(x, 200));
    const sent = window.__natLog.filter(e => e[0] === 'setChopFadeMs');
    const lab = document.getElementById('ti-fade-value');
    return { vis, w: Math.round(r.width), h: Math.round(r.height), sent: sent.map(e => e[1]),
             label: lab ? lab.textContent.trim() : null, pe: getComputedStyle(sl).pointerEvents };
  });
  ok(!fade.err && fade.vis && fade.sent.length >= 1 && Math.abs(fade.sent[fade.sent.length - 1] - 22.5) < 0.01
     && fade.pe !== 'none',
     '[13] 🚨 THE SLICE DRAWER\'S CROSSFADE MOVES AND REACHES setChopFadeMs', JSON.stringify(fade));

  // ── [14] THE ADSR READS SECONDS ONCE IT IS SECONDS ─────────────────────────────────────
  const secs = await p.evaluate(() => {
    const f = window.__tiFmtMs;
    return f ? { a: f(300), b: f(1380), c: f(5000) } : null;
  });
  ok(secs && secs.a === '300 ms' && secs.b === '1.38 s' && secs.c === '5.00 s',
     '[14] THE CHOP MENU\'S ADSR READS SECONDS OVER A SECOND — the synth page\'s own rule ("Dec 1.38s / Rel 300ms")',
     JSON.stringify(secs));

  // ── [15] 🚨 CTRL/CMD+A SELECTS EVERY CHOP, AND THE MENU EDITS ALL OF THEM ───────────────
  //  Max: "I want to press Ctrl+A and it selects all of my chops, 4 to 32 ... then I'm able to
  //  right click them and it's the SAME right click menu ... it's a GLOBAL ADSR, and this is good
  //  because I would like to put my release at zero most of the time."
  //  🚨 THE BAR THAT MATTERS IS THE SECOND HALF. Selecting is easy to fake; the thing that makes
  //  it useful is that ONE drag in the menu writes to all eight chops, and that is what is read.
  const sel = await p.evaluate(async () => {
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'a', metaKey: true, bubbles: true }));
    await new Promise(r => setTimeout(r, 250));
    const s = window.__tiChopSel ? window.__tiChopSel() : null;
    const lit = document.querySelectorAll('#ti-slice-overlays .ti-slice-body.sel').length;
    return { sel: s, lit };
  });
  ok(sel.sel && sel.sel.length === 8 && sel.lit === 8,
     '[15] 🚨 CMD+A SELECTS EVERY CHOP, AND EVERY ONE OF THEM IS LIT', JSON.stringify(sel));

  const global = await p.evaluate(async () => {
    // right-click chop 3 — inside the selection, so it stays
    const body = document.querySelector('#ti-slice-overlays .ti-slice-body[data-idx="3"]');
    body.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true }));
    await new Promise(r => setTimeout(r, 500));
    const still = window.__tiChopSel().length;
    /* tp61b — the header was "Chop" + a counter slot ("ALL 8"); Max: "why does 'Chop All 16' look
       weird — just make it say 'All Chops' whenever all is selected." It is one phrase across two
       spans now, so read the phrase. WHAT THIS BAR PROTECTS IS UNCHANGED: with a global selection
       live, the header must NOT look like the single-chop header, or a release of 0 reaches eight
       chops with nothing on screen saying so. */
    const head = [...document.querySelectorAll('#ti-chop-panel .ov-head .name > span')]
                   .map(e => e.textContent.trim()).join(' ');
    window.__natLog.length = 0;
    // one drag on the RELEASE row — the thing he actually wants at zero
    const row = [...document.querySelectorAll('#ti-chop-panel .ov-ad-row')].find(r => r.dataset.h === 'R');
    if (!row) return { err: 'no release row', rows: [...document.querySelectorAll('#ti-chop-panel .ov-ad-row')].map(r => r.dataset.h) };
    /* ⚠️ the listener is on .ov-ad-track, not the row — aiming at the row dispatches into a
       parent that has no handler and reads as "nothing happened" */
    const tr = row.querySelector('.ov-ad-track');
    if (!tr) return { err: 'no track in the release row' };
    const rr = tr.getBoundingClientRect();
    tr.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, button: 0, clientX: rr.left + 1, clientY: rr.top + rr.height / 2 }));
    document.dispatchEvent(new MouseEvent('mouseup', { bubbles: true }));
    await new Promise(r => setTimeout(r, 250));
    const calls = window.__natLog.filter(e => /^setSlice/.test(e[0]));
    const idxs = [...new Set(calls.map(e => e[1]))].sort((a, b) => a - b);
    return { still, head, names: [...new Set(calls.map(e => e[0]))], idxs };
  });
  ok(global.still === 8 && /^(All|8) Chops$/.test(global.head || '') && global.idxs.length === 8,
     '[16] 🚨 ONE DRAG IN THE MENU REACHES ALL EIGHT CHOPS, and the header says so',
     JSON.stringify(global));

  const cleared = await p.evaluate(async () => {
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    await new Promise(r => setTimeout(r, 200));
    return { sel: window.__tiChopSel().length, lit: document.querySelectorAll('#ti-slice-overlays .ti-slice-body.sel').length };
  });
  ok(cleared.sel === 0 && cleared.lit === 0, '[17] ESCAPE DROPS THE SELECTION', JSON.stringify(cleared));

  // ── [18] 🚨 DELETE DELETES ALL OF THEM, HIGHEST INDEX FIRST ────────────────────────────
  //  Max: "I could either press delete to delete them."  deleteSlice RE-INDEXES what is left, so
  //  walking upward would delete the wrong chops from the second one on — 0,1,2… would remove
  //  chop 0, then what used to be chop 2, then what used to be chop 4. The order is the bar.
  const del = await p.evaluate(async () => {
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'a', metaKey: true, bubbles: true }));
    await new Promise(r => setTimeout(r, 250));
    window.__natLog.length = 0;
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Delete', bubbles: true }));
    await new Promise(r => setTimeout(r, 350));
    const calls = window.__natLog.filter(e => e[0] === 'deleteSlice').map(e => e[1]);
    return { calls, sel: window.__tiChopSel().length,
             open: document.getElementById('ti-chop-panel').classList.contains('open') };
  });
  const descending = del.calls.every((v, i, a) => i === 0 || a[i - 1] > v);
  ok(del.calls.length === 8 && descending && del.sel === 0,
     '[18] 🚨 DELETE REMOVES EVERY SELECTED CHOP, HIGHEST INDEX FIRST (deleteSlice re-indexes what is left)',
     JSON.stringify(del));

  ok(errs.length === 0, '[19] THE PAGE THREW NOTHING', errs.join(' | '));
  console.log('\n  ' + pass + ' passed, ' + fail + ' failed\n');
  await b.close(); process.exit(fail ? 1 : 0);
})();
