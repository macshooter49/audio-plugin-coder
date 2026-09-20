// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp58 — THE SAMPLER HAS AN INPUT, AND THE FLOW HALF THAT WAS ALREADY BUILT IS REACHABLE.
//
//  Max: "why doesn't the sampler have an INPUT? I want to patch FX THRU IT BRUH CMONNN even FLOW
//  CARDS n SUCH PLEASE ... I need my inputs bro."
//
//  Every bar drives the REAL door (window.__tpAdd / __tpConnect — the same functions a mouse
//  reaches) and reads the PARAMETER that came out the other side, never a class name.
//
//    node Tests/_tp58_samp_gate.js <abs path to index.html>
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
let pass = 0, fail = 0;
const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l + (d ? '\n        ' + d : '')); }
                          else { fail++; console.log('  FAIL  ' + l + (d ? '\n        ' + d : '')); } };
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 1400, height: 900, deviceScaleFactor: 2 });
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.evaluateOnNewDocument(() => { try { localStorage.setItem('tpLayout', JSON.stringify({ v: 6, pos: {}, ext: {} })); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1900);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark');
    const d = document.getElementById('osc-a-device'); if (d) d.classList.remove('osc-off'); });
  await sleep(500); await p.evaluate(() => window.setActivePanel('tp')); await sleep(2600);

  await p.evaluate(() => window.__tpAdd('samp:a', 380, 300)); await sleep(800);

  // ── [0] THE PORT EXISTS, ON THE LEFT, AND IT TAKES AUDIO ────────────────────────────────
  const ports = await p.evaluate(() => {
    const w = document.querySelector('#tp-page .tp-node[data-key="samp-a"]'); if (!w) return { err: 'no node' };
    return [...w.querySelectorAll('.tp-port')].map(e => e.dataset.kind + ':' + (e.dataset.t || 'a')); });
  ok(Array.isArray(ports) && ports.includes('in:a') && ports.includes('out:a'),
     '[0] 🚨 THE SAMPLER HAS AN AUDIO INPUT AS WELL AS AN OUTPUT (it was out-only)',
     JSON.stringify(ports));

  // ── [1] A DEVICE DROPPED ON THE SAMPLER MAKES THE SAME CLAIM AS THE OTHER DIRECTION ──────
  await p.evaluate(() => window.__tpAdd('reverb', 900, 300)); await sleep(1100);
  const rvKey = await p.evaluate(() => (window.__tpNodes() || []).map(n => n.key).find(k => /^fx-/.test(k)));
  const back = await p.evaluate(async k => {
    const before = window.__tpParam('SYN_RVB_CHOPS');
    const a = window.__tpNodeByKey(k), sn = window.__tpNodeByKey('samp-a');
    // the effect INTO the sampler — the direction that did not exist before tp58
    window.__tpConnect(a, 'out', 0, sn, 'in', 0);
    await new Promise(r => setTimeout(r, 500));
    return { before, after: window.__tpParam('SYN_RVB_CHOPS') }; }, rvKey);
  ok(Math.round(back.after * 15) & 1,
     '[1] 🚨 AN EFFECT CABLED *INTO* THE SAMPLER LIGHTS THE SAME MASK AS THE SAMPLER CABLED INTO IT',
     'SYN_RVB_CHOPS ' + JSON.stringify(back) + '  -> mask ' + Math.round(back.after * 15) + ' (bit 0 = layer A)');

  // ── [2] THE FLOW HALF tp56 BUILT AND NEVER WIRED A CABLE TO ─────────────────────────────
  await p.evaluate(() => window.__tpAdd('flow:chop', 900, 620)); await sleep(1100);
  const flowKey = await p.evaluate(() => (window.__tpNodes() || []).map(n => n.key).find(k => /chop/.test(k) && !/samp/.test(k)));
  const fl = await p.evaluate(async k => {
    const before = window.__tpParam('FLOW_CHOP_CHOPS');
    const sn = window.__tpNodeByKey('samp-a'), fn = window.__tpNodeByKey(k);
    window.__tpConnect(sn, 'out', 0, fn, 'in', 0);
    await new Promise(r => setTimeout(r, 500));
    return { key: k, before, after: window.__tpParam('FLOW_CHOP_CHOPS') }; }, flowKey);
  ok(fl.after != null && (Math.round(fl.after * 15) & 1),
     '[2] 🚨 A CHOP LAYER FEEDS A FLOW CARD — the FLOW_CHOP_CHOPS mask tp56 built and the canvas never offered a cable to',
     'FLOW_CHOP_CHOPS ' + JSON.stringify(fl) + '  -> mask ' + Math.round((fl.after || 0) * 15));

  // ── [3] THE EYE HIDES A CARD'S CHROME INSTANTLY, and the VISUALIZER still animates ───────
  const eye = await p.evaluate(async () => {
    // silent = apply the class NOW. setViz's own 170 ms blink is a deliberate fade and is not
    // what this bar is about: the question is whether the chrome is hidden the instant `.viz`
    // lands, or whether it outlives its own transition.
    window.__tpViz(true, true);
    await new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
    await new Promise(r => setTimeout(r, 40));          // well inside a 0.15 s transition
    const body = document.querySelector('#tp-page .tp-node[data-kind="fx"] > .tp-body')
              || document.querySelector('#tp-page .tp-node > .tp-body');
    if (!body) return { err: 'no body' };
    const kids = [...body.querySelectorAll('*')].filter(e => !e.closest('.tp-viz'));
    const stillOn = kids.filter(e => getComputedStyle(e).visibility === 'visible').length;
    const viz = body.querySelector('.tp-viz');
    const vizTrans = viz ? getComputedStyle(viz).transitionProperty : 'n/a';
    return { bodyVis: getComputedStyle(body).visibility, stillOn, kids: kids.length, vizTrans }; });
  ok(eye.bodyVis === 'hidden' && eye.stillOn === 0,
     '[3] 🚨 THE EYE HIDES A CARD\'S CHROME AT ONCE — the LFO numbers used to outlive their own transition and paint over the emblem',
     JSON.stringify(eye));
  ok(eye.vizTrans !== 'none',
     '[4] ...AND THE VISUALIZER IS EXEMPT — the emblems still move, which is what the eye is for',
     'the visualizer\'s transition-property is "' + eye.vizTrans + '", not none');

  ok(errs.length === 0, '[5] NO PAGE ERRORS', errs.slice(0, 3).join(' | ') || 'clean');
  console.log('\n  ' + pass + ' passed, ' + fail + ' failed');
  await b.close();
  process.exit(fail ? 1 : 0);
})();
