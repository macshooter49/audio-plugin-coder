// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp58 — MAX'S LIST, MEASURED. Every bar drives the real control and reads what it SENT or what
//  the box actually measures.
//    node Tests/_tp58_chop_gate.js
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
const PAGE = path.join(require('os').tmpdir(), 'tp58_chop_gate.html'); fs.writeFileSync(PAGE, html);

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
  window.onSampleLoaded({ filename: 'Drum Loop.wav', lengthSamples: 220500, peaksMin: mn, peaksMax: mx, rootMidiNote: 60 }); };

(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 });
  await p.evaluateOnNewDocument(stubSrc + '\nstub();\n(' + spy.toString() + ')();');
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + PAGE, { waitUntil: 'load' }); await sleep(2500);
  await p.evaluate(() => document.documentElement.setAttribute('data-theme', 'dark'));

  // ── [0] THE EMPTY CHOP HERO: no wireframe, and the sample oscillator's own words ─────────
  await p.evaluate(() => document.getElementById('mix-btn').click()); await sleep(1300);
  const empty = await p.evaluate(() => {
    const h = document.getElementById('hero'), c = document.getElementById('terrain-canvas'),
          l = document.getElementById('ti-empty-label');
    return { heroCls: h ? h.className : null,
             meshOpacity: c ? +getComputedStyle(c).opacity : null,
             text: l ? l.textContent : null,
             transform: l ? getComputedStyle(l).textTransform : null,
             weight: l ? getComputedStyle(l).fontWeight : null }; });
  ok(empty.meshOpacity === 0 && /Drop sample here/.test(empty.text || '')
     && !/DRAG SAMPLE/i.test(empty.text || '') && empty.transform === 'none',
     '[0] THE EMPTY HERO HAS NO WHITE GRID, and its prompt is the sample oscillator\'s — not shouted',
     JSON.stringify(empty));

  await p.evaluate(fakeSample); await sleep(800);

  // ── [1] THE STEM A/B/C/D ARE THE LAYER MENU'S SIZE ───────────────────────────────────────
  const sz = await p.evaluate(() => {
    const r = e => { const b = e.getBoundingClientRect(); return { w: +b.width.toFixed(2), h: +b.height.toFixed(2) }; };
    const lay = [...document.querySelectorAll('#trigger-context .layer-status-dot')].filter(e => e.offsetParent).map(r);
    const stm = [...document.querySelectorAll('#mix-stem-area .stem-buttons .stem-btn')].map(r);
    return { lay, stm }; });
  const dW = Math.abs((sz.lay[0] || {}).w - (sz.stm[0] || {}).w), dH = Math.abs((sz.lay[0] || {}).h - (sz.stm[0] || {}).h);
  ok(sz.lay.length === 4 && sz.stm.length === 4 && dW <= 0.6 && dH <= 1.0,
     '[1] 🚨 THE STEM MENU\'S A/B/C/D ARE THE LAYER MENU\'S OWN SIZE (they were ten pixels shorter)',
     'layer ' + JSON.stringify(sz.lay[0]) + '  stem ' + JSON.stringify(sz.stm[0]) + '  Δw ' + dW.toFixed(2) + '  Δh ' + dH.toFixed(2));

  // ── [2] THE STEM PANE'S BOTTOM SPACE MATCHES ITS TOP ─────────────────────────────────────
  const sp = await p.evaluate(() => {
    const a = document.getElementById('mix-stem-area'); if (!a) return null;
    const ab = a.getBoundingClientRect();
    const rows = [...a.children].filter(e => { const c = getComputedStyle(e);
      return c.display !== 'none' && c.position !== 'absolute' && e.getBoundingClientRect().height > 1; })
      .map(e => e.getBoundingClientRect());
    if (!rows.length) return null;
    return { top: +(rows[0].top - ab.top).toFixed(2),
             bottom: +(ab.bottom - rows[rows.length - 1].bottom).toFixed(2),
             rows: rows.length }; });
  ok(sp && Math.abs(sp.top - sp.bottom) <= 1.5,
     '[2] THE STEM PANE\'S BOTTOM SPACE MATCHES ITS TOP — an empty status line was reserving 20 px',
     JSON.stringify(sp));

  // ── [3] THE BPM READOUT SHOWS THE LOOP'S OWN TEMPO WHEN LOCKED, AND IT IS EDITABLE ───────
  const lock = await p.evaluate(async () => {
    const lk = document.getElementById('ti-bpm-lock'); if (!lk) return { err: 'no lock' };
    lk.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    await new Promise(r => setTimeout(r, 500));
    const src = document.getElementById('ti-bpm-src'), ar = document.getElementById('ti-bpm-arrow');
    const shown = src && getComputedStyle(src).display !== 'none';
    if (shown) src.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    await new Promise(r => setTimeout(r, 200));
    const inp = document.getElementById('ti-bpm-src-in');
    if (inp) { inp.value = '117'; inp.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true })); }
    await new Promise(r => setTimeout(r, 300));
    const sent = (window.__natLog || []).filter(c => c[0] === 'setLayerSourceBpm');
    return { shown, arrow: ar && getComputedStyle(ar).display !== 'none', opened: !!inp, sent }; });
  ok(lock.shown && lock.arrow && lock.opened && lock.sent.length && +lock.sent[lock.sent.length - 1][2] === 117,
     '[3] 🚨 THE LOCK SHOWS THE LOOP\'S OWN TEMPO → THE SESSION\'S, and typing one reaches the engine',
     JSON.stringify(lock));

  // ── [3b] AND TOGGLING THE LOCK MOVES NOTHING ────────────────────────────────────────────
  //  ⚠️ THIS IS A REGRESSION I SHIPPED AND CAUGHT IN A SCREENSHOT, SO IT GETS A BAR. The bottom
  //  right cluster is RIGHT-ANCHORED: every pixel the readout gains, the sample library loses on
  //  its left. Adding `117 →` to it pushed the library's ‹ arrow into the Slices pill — the exact
  //  collision tp54's bar [2] exists to prevent. The box now reserves the locked width.
  const move = await p.evaluate(async () => {
    const g = () => { const r = s => { const e = document.querySelector(s); if (!e) return null;
        const b = e.getBoundingClientRect(); return [+b.x.toFixed(1), +b.width.toFixed(1)]; };
      return { bpm: r('.ti-bpm-display'), lib: r('#ti-lib'), nav: r('#ti-lib .ti-lib-nav') }; };
    const lk = document.getElementById('ti-bpm-lock');
    const on = g(); lk.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    await new Promise(r => setTimeout(r, 500));
    const off = g(); lk.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    await new Promise(r => setTimeout(r, 500));
    return { on, off, same: JSON.stringify(on) === JSON.stringify(off) }; });
  ok(move.same,
     '[3b] TOGGLING THE LOCK MOVES NOTHING — the readout reserves its locked width, so the sample library does not slide',
     'locked ' + JSON.stringify(move.on) + '  unlocked ' + JSON.stringify(move.off));

  // ── [4] NO CHOP RESIDUE ON THE SYNTH PAGE ────────────────────────────────────────────────
  await p.evaluate(() => { const q = document.querySelectorAll('#ti-mode-toggle .ti-mode-pill'); if (q[1]) q[1].dispatchEvent(new MouseEvent('click', { bubbles: true })); });
  await sleep(500);
  await p.evaluate(() => { try { window.applySynPanelOpen ? window.applySynPanelOpen(true) : window.setActivePanel('syn'); } catch (e) { document.body.classList.add('ti-syn-open'); } });
  await sleep(60);
  const ghosts = await p.evaluate(() => {
    const ids = ['ti-slices-btn','ti-slices-wrap','ti-bottom-pills','ti-mode-toggle','ti-arm','ti-lib','ti-root-picker','ti-bottom-right-cluster'];
    return ids.filter(id => { const e = document.getElementById(id); if (!e) return false;
      const c = getComputedStyle(e), b = e.getBoundingClientRect();
      return c.visibility !== 'hidden' && c.display !== 'none' && +c.opacity > 0.01 && b.width > 0; }); });
  ok(ghosts.length === 0,
     '[4] 🚨 SWITCHING PAGES LEAVES NO CHOP RESIDUE — 60 ms after the switch, nothing of the chop page is still painting',
     ghosts.length ? 'still visible: ' + ghosts.join(', ') : 'clean at 60 ms (the Slices pill used to survive its own 0.15 s transition)');

  // ── [5] A COVERED PAGE ANIMATES NOTHING ──────────────────────────────────────────────────
  const noAnim = await p.evaluate(() => {
    const e = document.getElementById('ti-slices-btn'); if (!e) return { err: 'absent' };
    const c = getComputedStyle(e);
    return { transition: c.transitionProperty + ' ' + c.transitionDuration, vis: c.visibility }; });
  ok(noAnim.vis === 'hidden' && /none|0s/.test(noAnim.transition),
     '[5] THE RULE, NOT THE PILL — a covered subtree has its transitions off, so the next pill that writes `transition: .2s` cannot bring the ghost back',
     JSON.stringify(noAnim));

  ok(errs.length === 0, '[6] NO PAGE ERRORS', errs.slice(0, 3).join(' | ') || 'clean');
  console.log('\n  ' + pass + ' passed, ' + fail + ' failed');
  await b.close();
  process.exit(fail ? 1 : 0);
})();
