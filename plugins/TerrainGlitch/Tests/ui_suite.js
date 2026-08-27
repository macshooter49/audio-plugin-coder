// Terrain Glitch — ui_suite: the Monitor-card page proves its wiring before it ships.
//
//   cd plugins/TerrainGlitch && NODE_PATH=<scratchpad>/node_modules node Tests/ui_suite.js [page.html]
//
// WHAT IS PROVEN (each gate has a recorded failure — missing-page run + the two mutants):
//   1  BIND — every card control maps to its FLOW_GLI_* param id, AND the page actually
//      requested each id from the slider registry (verify the PATH, fb373 — the map alone
//      proves nothing). The MIX header's FLOW_GLI_BLEND is bound too.
//   2  NO DOUBLES — within any visible pane no label appears twice (incl. box labels + tabs).
//   3  READOUT LAW — knob faces are bare numbers: value rails ≤5 glyphs, no Hz/dB units;
//      the Chance big face ≤4 glyphs (% allowed).
//   4  THE FEED MOVES THE MONITOR — a synthetic getGliFeed frame injected into the poll path
//      drives the playhead/status/LEDs; moving the feed's slice moves the purple playhead.
//   5  DBLCLICK RESET — a dragged Decay knob resets to its documented default (0) on
//      double-click (after the 400 ms drag-suppression window).
//   6  PRESET BANK — stepping the header preset arrow loads a factory preset and the knobs
//      MOVE (state + the painted face both change).
const puppeteer = require('puppeteer-core');
const path = require('path');
const fs = require('fs');

const P = process.argv[2] || path.join(__dirname, '..', 'Source', 'ui', 'public', 'index.html');
const VW = 480, VH = 900, DSF = 2;
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

let pass = 0, fail = 0;
function chk(ok, label, detail){
  if (ok) { pass++; console.log('  ok    ' + label + (detail ? '   ' + detail : '')); }
  else    { fail++; console.log('  FAIL  ' + label + (detail ? '   ' + detail : '')); }
}

(async () => {
  console.log('\n══ Terrain Glitch — ui_suite ══');
  console.log('   page ' + P);
  if (!fs.existsSync(P)) { chk(false, 'the page exists', P + ' is missing'); console.log('\n  PASS ' + pass + '   FAIL ' + fail); process.exit(1); }

  const b = await puppeteer.launch({ executablePath: (process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width: VW, height: VH, deviceScaleFactor: DSF });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 200)));
  await pg.goto('file://' + P, { waitUntil: 'load', timeout: 60000 });
  let booted = true;
  try { await pg.waitForFunction('window.__gliProbe && window.__cardEl', { timeout: 8000 }); }
  catch (e) { booted = false; }
  chk(booted, 'the card boots (probe surface + card element present)', errs[0] || '');
  if (!booted) { console.log('\n  page errors: ' + errs.slice(0, 3).join(' | ')); console.log('\n  PASS ' + pass + '   FAIL ' + fail); await b.close(); process.exit(1); }

  // ── 1  BIND — the map AND the path ────────────────────────────────────────────
  const bind = await pg.evaluate(() => {
    const B = window.__gliProbe.BIND, reg = window.__sliderRegistry;
    const out = { total: 0, badId: [], unbound: [], mix: !!(reg && reg.has('FLOW_GLI_BLEND')) };
    for (const k in B) { out.total++; const pid = B[k][0];
      if (!/^FLOW_GLI_[A-Z0-9_]+$/.test(pid)) out.badId.push(k + '->' + pid);
      if (!reg || !reg.has(pid)) out.unbound.push(pid); }
    return out; });
  chk(bind.total >= 78 && !bind.badId.length, 'BIND — every control maps to a FLOW_GLI_* id',
      bind.total + ' controls' + (bind.badId.length ? '  bad: ' + bind.badId.join(',') : ''));
  chk(!bind.unbound.length, 'BIND — the page REQUESTED every id (registry probe, fb373)',
      bind.unbound.length ? 'unbound: ' + bind.unbound.slice(0, 5).join(',') : 'all ' + bind.total + ' in registry');
  chk(bind.mix, 'BIND — the MIX header rides FLOW_GLI_BLEND');

  // ── 2/3  no-doubles + readout law, per visible pane ───────────────────────────
  for (const tab of ['fire', 'fx', 'motion']) {
    await pg.evaluate(t => { document.querySelector('.gli-ext .tab[data-p="' + t + '"]').click(); }, tab);
    await sleep(120);
    const o = await pg.evaluate(() => {
      const card = window.__cardEl, vis = el => !!(el.offsetParent);
      /* controls among controls, containers among containers: Terrain's shipped Motion
         pane has a Clock GROUP BOX holding the Clock slider — a box may name its own
         control; two CONTROLS (or two boxes/tabs) sharing a name is the violation. */
      const ctl = [...card.querySelectorAll('.lb')].filter(vis).map(e => e.textContent.trim()).filter(Boolean);
      const box = [...card.querySelectorAll('.gbl,.tab')].filter(vis).map(e => e.textContent.trim()).filter(Boolean);
      const dup = ctl.filter((l, i) => ctl.indexOf(l) !== i).concat(box.filter((l, i) => box.indexOf(l) !== i));
      const vls = [...card.querySelectorAll('.vl')].filter(vis).map(e => e.textContent.trim()).filter(Boolean);
      const badVl = vls.filter(v => v.length > 5 || /Hz|dB/i.test(v));
      const big = card.querySelector('.big'), bigTxt = (big && big.offsetParent) ? big.textContent.trim() : '';
      return { dup, badVl, bigTxt };
    });
    chk(!o.dup.length, 'NO DOUBLES — pane "' + tab + '"', o.dup.length ? 'dupes: ' + o.dup.join(',') : '');
    chk(!o.badVl.length, 'READOUT LAW — pane "' + tab + '" value rails ≤5 glyphs, no Hz/dB', o.badVl.join(',') || '');
    if (o.bigTxt) chk(o.bigTxt.length <= 4, 'READOUT LAW — big face ≤4 glyphs', '"' + o.bigTxt + '"');
  }
  await pg.evaluate(() => { document.querySelector('.gli-ext .tab[data-p="fire"]').click(); });

  // ── 4  the synthetic feed drives the monitor ─────────────────────────────────
  await pg.evaluate(() => { window.__gliFeedSynthetic = { on: 1, s: 3.2, f: 0, fs: 3, hl: 2,
    lv: new Array(16).fill(60), c: 1, ls: 3.2, pl: 1, a: 1, ol: 0.9 }; });
  await sleep(400);
  const f1 = await pg.evaluate(() => {
    const card = window.__cardEl, sim = window.__gliProbe.sim;
    const st = card.querySelector('.r-st').textContent, fx = card.querySelector('.r-fx').textContent;
    const lit = [...card.querySelectorAll('.matrix i')].filter(i => i.className === 'lv' || i.className === 'pk').length;
    const line = (card.querySelector('.graph svg').innerHTML.match(/line x1="([\d.]+)" y1="6"/) || [])[1];
    return { slice: sim.slice, st, fx, lit, x: parseFloat(line || '-1') };
  });
  chk(f1.slice === 3 && f1.st === 'Firing' && f1.fx === 'Repeat' && f1.lit > 0,
      'FEED — slice/status/FX/meter follow the injected frame',
      'slice=' + f1.slice + ' st=' + f1.st + ' fx=' + f1.fx + ' lit=' + f1.lit);
  await pg.evaluate(() => { window.__gliFeedSynthetic.s = 11.7; window.__gliFeedSynthetic.ls = 11.7; });
  await sleep(300);
  const f2 = await pg.evaluate(() => {
    const card = window.__cardEl, sim = window.__gliProbe.sim;
    const line = (card.querySelector('.graph svg').innerHTML.match(/line x1="([\d.]+)" y1="6"/) || [])[1];
    return { slice: sim.slice, x: parseFloat(line || '-1') };
  });
  chk(f2.slice === 11 && f2.x > f1.x, 'FEED — moving the feed moves the purple playhead',
      'slice ' + f1.slice + '->' + f2.slice + '  x ' + f1.x + '->' + f2.x);
  await pg.evaluate(() => { window.__gliFeedSynthetic = null; });

  // ── 5  dblclick reset (Decay, default 0) ─────────────────────────────────────
  const kb = await pg.evaluate(() => {
    const c = window.__cardEl.querySelector('[data-ti-key="decay"] svg').getBoundingClientRect();
    return { x: c.left + c.width / 2, y: c.top + c.height / 2 };
  });
  await pg.mouse.move(kb.x, kb.y); await pg.mouse.down();
  for (let i = 1; i <= 6; i++) await pg.mouse.move(kb.x, kb.y - i * 10);
  await pg.mouse.up();
  const dragged = await pg.evaluate(() => window.__gliProbe.S.v.decay);
  chk(dragged > 0.2, 'RESET — the drag moved Decay first', 'decay=' + (+dragged).toFixed(3));
  await sleep(550);   // the 400 ms drag-suppression window must pass (fb-law)
  /* dispatched dblclick — the house precedent (eq_ui/fx4_ui): CDP suppresses the
     synthesized dblclick after a canceled pointerdown; real WKWebView input fires it. */
  await pg.evaluate(() => { window.__cardEl.querySelector('[data-ti-key="decay"] svg')
    .dispatchEvent(new MouseEvent('dblclick', { bubbles: true, cancelable: true })); });
  await sleep(120);
  const reset = await pg.evaluate(() => window.__gliProbe.S.v.decay);
  chk(reset === 0, 'RESET — dblclick lands the documented default (0)', 'decay=' + reset);

  // ── 6  the preset bank moves the knobs ───────────────────────────────────────
  const p0 = await pg.evaluate(() => ({ name: window.__cardEl.querySelector('.pset .pn').textContent,
    chance: window.__gliProbe.S.v.chance, face: window.__cardEl.querySelector('.big .bn').textContent }));
  await pg.evaluate(() => { window.__cardEl.querySelectorAll('.pset .pa')[1].click(); });
  await sleep(200);
  const p1 = await pg.evaluate(() => ({ name: window.__cardEl.querySelector('.pset .pn').textContent,
    chance: window.__gliProbe.S.v.chance, face: window.__cardEl.querySelector('.big .bn').textContent }));
  chk(p1.name !== p0.name && p1.chance !== p0.chance && p1.face !== p0.face,
      'PRESETS — stepping the bank loads a preset and the knobs move',
      p0.name + '->' + p1.name + '  chance ' + p0.chance + '->' + p1.chance + '  face ' + p0.face + '->' + p1.face);

  // natural size (the C++ lanes size their editors from this)
  const nat = await pg.evaluate(() => { const r = window.__cardEl.getBoundingClientRect();
    return { w: Math.ceil(r.width), h: Math.ceil(r.height) }; });
  console.log('\n  natural card size: ' + nat.w + ' x ' + nat.h + ' px (zoom 1.2 default)');

  if (errs.length) console.log('\n  page errors: ' + errs.slice(0, 3).join(' | '));
  chk(!errs.length, 'no page errors', errs[0] || '');
  console.log('\n  PASS ' + pass + '   FAIL ' + fail);
  await b.close();
  process.exit(fail ? 1 : 0);
})().catch(e => { console.error('FAILED', e.stack || e.message); process.exit(1); });
