// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp30 — THE FLOW CARDS ARE CABLED PER OSCILLATOR (headless canvas probe).
//  Max: "every oscillator routes to that flow card ... I want the complete opposite. I cut the
//  cable to oscillator B and put it to audio out, or to another glitch card. And if something
//  isn't hooked up to anything it has to have no sound."
//  Proves on the real page that the CANVAS agrees with the engine:
//    1. every oscillator starts cabled to the first audio flow card (it may default to that);
//    2. cutting one oscillator's cable clears that card's pill and only that pill;
//    3. two cards split the oscillators — each osc lands on the card it is cabled to;
//    4. cutting the last cable an oscillator has writes SYN_OSC_x_OUT = 0 and leaves it with
//       NO cable at all — hooked up to nothing;
//    5. undo puts every one of those back.
//    node Tests/_tp30_flowroute.js <abs path to index.html> <screenshot dir>
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 1100, height: 760, deviceScaleFactor: 1 });
  const errs = []; p.on('pageerror', e => errs.push('PAGEERROR ' + e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.setItem('tpLayout', JSON.stringify({ v: 6, pos: {}, ext: {} })); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => {
    document.documentElement.setAttribute('data-theme', 'dark');
    ['a', 'b'].forEach(o => { const d = document.getElementById('osc-' + o + '-device'); if (d) d.classList.remove('osc-off'); });
    try { window.__flowSetChain(['glitch']); } catch (e) {}
  });
  await sleep(300); await p.evaluate(() => window.setActivePanel('tp')); await sleep(2200);
  const R = { errs };
  const cables = () => p.evaluate(() => window.__tpCables());
  const par = id => p.evaluate(i => window.__tpParam(i), id);

  // ── 1. the default: every oscillator is cabled INTO the glitch card ─────────────────────────
  R.default_cables = (await cables()).filter(c => c.indexOf('flow-glitch') >= 0);
  R.default_pillB = await par('FLOW_GLI_SRC_B');

  // ── 2. cut oscillator B's cable to the card ─────────────────────────────────────────────────
  const cutOne = (from, to) => p.evaluate((f, t) => {
    const id = window.__tpCables().find(c => c.indexOf(f) === 0 && c.indexOf(t) > 0);
    if (!id) return 'no cable ' + f + ' -> ' + t;
    const hit = document.querySelector('#tp-page .cable[data-id="' + CSS.escape(id) + '"] .hit');
    if (!hit) return 'no hit path for ' + id;
    hit.dispatchEvent(new MouseEvent('mousedown', { bubbles: true }));
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Backspace', bubbles: true }));
    return id;
  }, from, to);
  R.cutB = await cutOne('osc-b.', 'flow-glitch'); await sleep(400);
  R.afterCut_pillB = await par('FLOW_GLI_SRC_B');
  R.afterCut_pillA = await par('FLOW_GLI_SRC_A');
  R.afterCut_cables = (await cables()).filter(c => c.indexOf('osc-b') === 0);

  // ── 3. a SECOND card, added with every pill on: B is now claimed by it, A stays on card 1 ───
  await p.evaluate(() => { const ch = window.__flowChain(); ch.push('glitch2'); window.__flowSetChain(ch); });
  await sleep(900); await p.evaluate(() => window.__tpSync()); await sleep(400);
  let cb = await cables();
  R.two_cards_B = cb.filter(c => c.indexOf('osc-b') === 0);
  R.two_cards_A = cb.filter(c => c.indexOf('osc-a') === 0);

  // ── 4. cut B out of card 2 as well — it falls back to the dry path ──────────────────────────
  R.cutB2 = await cutOne('osc-b.', 'flow-glitch2'); await sleep(500);
  R.afterCut2_pill = await par('FLOW_GLI2_SRC_B');
  R.afterCut2_cables = (await cables()).filter(c => c.indexOf('osc-b') === 0);

  // ── 5. cut the LAST cable it has: hooked up to nothing ──────────────────────────────────────
  const dry = (await cables()).find(c => c.indexOf('osc-b') === 0);
  R.cutDry = dry ? await p.evaluate(id => {
    const hit = document.querySelector('#tp-page .cable[data-id="' + CSS.escape(id) + '"] .hit');
    if (!hit) return 'no hit path';
    hit.dispatchEvent(new MouseEvent('mousedown', { bubbles: true }));
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Backspace', bubbles: true }));
    return id;
  }, dry) : 'no dry cable to cut';
  await sleep(500);
  R.outParam = await par('SYN_OSC_B_OUT');
  R.afterOutCut = (await cables()).filter(c => c.indexOf('osc-b') === 0);

  // ── 6. undo puts it back ────────────────────────────────────────────────────────────────────
  await p.evaluate(() => window.__tpUndo()); await sleep(900);
  R.undo_outParam = await par('SYN_OSC_B_OUT');
  R.undo_cables = (await cables()).filter(c => c.indexOf('osc-b') === 0);

  await p.screenshot({ path: process.argv[3] + '/tp30-canvas.png' });
  console.log(JSON.stringify(R, null, 1));
  await b.close();
})().catch(e => { console.log('FAIL', e); process.exit(1); });
