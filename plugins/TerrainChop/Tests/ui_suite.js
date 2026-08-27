// Terrain Chop — ui_suite: the Ribbon-card page proves its wiring before it ships.
//
//   cd plugins/TerrainChop && NODE_PATH=<scratchpad>/node_modules node Tests/ui_suite.js [page.html]
//
// WHAT IS PROVEN (each gate has a recorded failure — missing-page run + the two mutants):
//   1  BIND — every card control maps to its FLOW_CHOP_* / FLOW_SEQ_RATE param id, AND the
//      page actually requested each id from the slider registry (verify the PATH, fb373).
//      The MIX header's FLOW_CHOP_BLEND is bound too.
//   2  NO DOUBLES — within any visible pane no label appears twice (incl. box labels + tabs).
//   3  READOUT LAW — value rails ≤5 glyphs, no Hz/dB units on any face.
//   4  THE FEED MOVES THE RIBBON — a synthetic getChopFeed frame injected into the poll path
//      drives the active slice; moving the feed's position moves the purple playhead.
//   5  DBLCLICK RESET — a dragged Smooth slider resets to its documented default (0.15).
//   6  WIPE + THE RADIO LAW — Wipe jolts the memory sim (the chopWipe path), and
//      Catch/Always behave as one radio pair (Catch on ⇒ Always off).
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
  console.log('\n══ Terrain Chop — ui_suite ══');
  console.log('   page ' + P);
  if (!fs.existsSync(P)) { chk(false, 'the page exists', P + ' is missing'); console.log('\n  PASS ' + pass + '   FAIL ' + fail); process.exit(1); }

  const b = await puppeteer.launch({ executablePath: (process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width: VW, height: VH, deviceScaleFactor: DSF });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 200)));
  await pg.goto('file://' + P, { waitUntil: 'load', timeout: 60000 });
  let booted = true;
  try { await pg.waitForFunction('window.__chopProbe && window.__cardEl', { timeout: 8000 }); }
  catch (e) { booted = false; }
  chk(booted, 'the card boots (probe surface + card element present)', errs[0] || '');
  if (!booted) { console.log('\n  page errors: ' + errs.slice(0, 3).join(' | ')); console.log('\n  PASS ' + pass + '   FAIL ' + fail); await b.close(); process.exit(1); }

  // ── 1  BIND — the map AND the path ────────────────────────────────────────────
  const bind = await pg.evaluate(() => {
    const B = window.__chopProbe.BIND, reg = window.__sliderRegistry;
    const out = { total: 0, seq: 0, badId: [], unbound: [], mix: !!(reg && reg.has('FLOW_CHOP_BLEND')) };
    for (const k in B) { out.total++; const pid = B[k][0];
      if (/^FLOW_SEQ_/.test(pid)) out.seq++;
      else if (!/^FLOW_CHOP_[A-Z0-9_]+$/.test(pid)) out.badId.push(k + '->' + pid);
      if (!reg || !reg.has(pid)) out.unbound.push(pid); }
    return out; });
  chk(bind.total >= 42 && !bind.badId.length && bind.seq >= 1,
      'BIND — every control maps to a FLOW_CHOP_* id (+ FLOW_SEQ_RATE for Time)',
      bind.total + ' controls, ' + bind.seq + ' FLOW_SEQ_*' + (bind.badId.length ? '  bad: ' + bind.badId.join(',') : ''));
  chk(!bind.unbound.length, 'BIND — the page REQUESTED every id (registry probe, fb373)',
      bind.unbound.length ? 'unbound: ' + bind.unbound.slice(0, 5).join(',') : 'all ' + bind.total + ' in registry');
  chk(bind.mix, 'BIND — the MIX header rides FLOW_CHOP_BLEND');

  // ── 2/3  no-doubles + readout law, per visible pane ───────────────────────────
  for (const tab of ['slice', 'scan', 'shape']) {
    await pg.evaluate(t => { document.querySelector('.chop-ext .tab[data-p="' + t + '"]').click(); }, tab);
    await sleep(120);
    const o = await pg.evaluate(() => {
      const card = window.__cardEl, vis = el => !!(el.offsetParent);
      /* controls among controls, containers among containers (the glitch suite's
         Clock-box precedent: a box may name its own control). */
      const ctl = [...card.querySelectorAll('.lb')].filter(vis).map(e => e.textContent.trim()).filter(Boolean);
      const box = [...card.querySelectorAll('.gbl,.tab')].filter(vis).map(e => e.textContent.trim()).filter(Boolean);
      const dup = ctl.filter((l, i) => ctl.indexOf(l) !== i).concat(box.filter((l, i) => box.indexOf(l) !== i));
      const vls = [...card.querySelectorAll('.vl')].filter(vis).map(e => e.textContent.trim()).filter(Boolean);
      const badVl = vls.filter(v => v.length > 5 || /Hz|dB/i.test(v));
      return { dup, badVl };
    });
    chk(!o.dup.length, 'NO DOUBLES — pane "' + tab + '"', o.dup.length ? 'dupes: ' + o.dup.join(',') : '');
    chk(!o.badVl.length, 'READOUT LAW — pane "' + tab + '" value rails ≤5 glyphs, no Hz/dB', o.badVl.join(',') || '');
  }
  await pg.evaluate(() => { document.querySelector('.chop-ext .tab[data-p="slice"]').click(); });

  // ── 4  the synthetic feed drives the ribbon ──────────────────────────────────
  // init slices index 4 -> SLICEL[4] = 8 slices; feed.s rides sub.frac
  await pg.evaluate(() => { window.__chopFeedSynthetic = { on: 1, s: 2.5, pl: 1, a: 1 }; });
  await sleep(400);
  const r1 = await pg.evaluate(() => {
    const sim = window.__chopProbe.sim;
    const m = window.__cardEl.querySelector('.ribbon svg').innerHTML.match(/line x1="([\d.]+)" y1="4"/);
    return { sub: sim.sub, x: parseFloat((m || [])[1] || '-1') };
  });
  chk(r1.sub === 2 && r1.x >= 0, 'FEED — the active slice follows the injected frame', 'sub=' + r1.sub + ' x=' + r1.x);
  await pg.evaluate(() => { window.__chopFeedSynthetic.s = 6.4; });
  await sleep(300);
  const r2 = await pg.evaluate(() => {
    const sim = window.__chopProbe.sim;
    const m = window.__cardEl.querySelector('.ribbon svg').innerHTML.match(/line x1="([\d.]+)" y1="4"/);
    return { sub: sim.sub, x: parseFloat((m || [])[1] || '-1') };
  });
  chk(r2.sub === 6 && r2.x > r1.x, 'FEED — moving the feed moves the purple playhead',
      'sub ' + r1.sub + '->' + r2.sub + '  x ' + r1.x + '->' + r2.x);
  await pg.evaluate(() => { window.__chopFeedSynthetic = null; });

  // ── 5  dblclick reset (Shape pane · Smooth, default 0.15) ────────────────────
  await pg.evaluate(() => { document.querySelector('.chop-ext .tab[data-p="shape"]').click(); });
  await sleep(150);
  const kb = await pg.evaluate(() => {
    const c = window.__cardEl.querySelector('[data-ti-key="smooth"] .vsl').getBoundingClientRect();
    return { x: c.left + c.width / 2, y: c.top + c.height / 2 };
  });
  await pg.mouse.move(kb.x, kb.y); await pg.mouse.down();
  for (let i = 1; i <= 6; i++) await pg.mouse.move(kb.x, kb.y - i * 8);
  await pg.mouse.up();
  const dragged = await pg.evaluate(() => window.__chopProbe.S.v.smooth);
  chk(dragged > 0.4, 'RESET — the drag moved Smooth first', 'smooth=' + (+dragged).toFixed(3));
  await sleep(550);   // the 400 ms drag-suppression window must pass (fb-law)
  /* dispatched dblclick — the house precedent (eq_ui/fx4_ui): CDP suppresses the
     synthesized dblclick after a canceled pointerdown; real WKWebView input fires it. */
  await pg.evaluate(() => { window.__cardEl.querySelector('[data-ti-key="smooth"] .vsl')
    .dispatchEvent(new MouseEvent('dblclick', { bubbles: true, cancelable: true })); });
  await sleep(120);
  const reset = await pg.evaluate(() => window.__chopProbe.S.v.smooth);
  chk(Math.abs(reset - 0.15) < 1e-9, 'RESET — dblclick lands the documented default (0.15)', 'smooth=' + reset);

  // ── 6  Wipe + the Catch/Always radio law ─────────────────────────────────────
  await pg.evaluate(() => { document.querySelector('.chop-ext .tab[data-p="scan"]').click(); });
  await sleep(150);
  const w = await pg.evaluate(() => {
    const sim = window.__chopProbe.sim, before = sim.off;
    const togs = [...window.__cardEl.querySelectorAll('.tog')];
    const wipe = togs.find(t => t.textContent.trim() === 'Wipe'); if (wipe) wipe.click();
    return { before, after: sim.off };
  });
  /* 996.5 not 997: sim.off is an accumulated float — (x+997)-x can round a hair under 997 */
  chk(w.after - w.before >= 996.5, 'WIPE — the wipe path jolts the memory (chopWipe route)',
      'off ' + w.before.toFixed(1) + '->' + w.after.toFixed(1));
  const radio = await pg.evaluate(() => {
    const S = window.__chopProbe.S;
    S.set('catch', 1); const a = { c: S.v['catch'], al: S.v.always };
    S.set('always', 1); const b2 = { c: S.v['catch'], al: S.v.always };
    return { a, b: b2 };
  });
  chk(radio.a.c === 1 && radio.a.al === 0 && radio.b.c === 0 && radio.b.al === 1,
      'RADIO LAW — Catch/Always are one pair', JSON.stringify(radio));

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
