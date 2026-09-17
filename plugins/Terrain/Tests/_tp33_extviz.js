// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp33 — AN EXTENDED FLOW CARD MUST STAY INSIDE ITS NODE IN EYE MODE.
//  Max: "whenever we extend the glitch flow card and we put emblem mode on, the actual UI pops
//  out of the emblem mode so it's under the cover, so it comes over the cover and it looks
//  terrible."  The eye's vizFit() calls unclip(), which walks up from the picture and sets
//  overflow:visible on every ancestor so a scaled picture is not cropped — on a flow card that
//  releases the whole extended UI to spill over its neighbours.
//    node Tests/_tp33_extviz.js <abs path to index.html> <screenshot dir>
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 1200, height: 820, deviceScaleFactor: 2 });
  const errs = []; p.on('pageerror', e => errs.push('PAGEERROR ' + e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.setItem('tpLayout', JSON.stringify({ v: 6, pos: {}, ext: { 'flow-glitch': 1 } })); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1700);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark');
    const d = document.getElementById('osc-a-device'); if (d) d.classList.remove('osc-off');
    try { window.__flowSetChain(['glitch']); } catch (e) {} });
  await sleep(600); await p.evaluate(() => window.setActivePanel('tp')); await sleep(2600);
  await p.evaluate(() => window.__tpExt('flow-glitch', true)); await sleep(1400);

  // how far does the card's ink reach outside the node box it is supposed to live in?
  const spill = () => p.evaluate(() => {
    const w = [...document.querySelectorAll('#tp-page .tp-node')].find(n => (n.dataset.key || '') === 'flow-glitch')
           || [...document.querySelectorAll('#tp-page .tp-node')].find(n => n.querySelector('.ti-card'));
    if (!w) return { err: 'no flow node' };
    const nr = w.getBoundingClientRect();
    let worst = 0, who = '';
    w.querySelectorAll('*').forEach(e => {
      const r = e.getBoundingClientRect(); if (!r.width || !r.height) return;
      const out = Math.max(nr.left - r.left, r.right - nr.right, nr.top - r.top, r.bottom - nr.bottom);
      if (out > worst) { worst = out; who = e.className && e.className.baseVal !== undefined ? e.className.baseVal : (e.className || e.tagName); }
    });
    // and how many ancestors of the picture had their clipping removed
    let opened = 0;
    w.querySelectorAll('*').forEach(e => { if (e.style && e.style.overflow === 'visible') opened++; });
    const openedWho = []; w.querySelectorAll('*').forEach(e => { if (e.style && e.style.overflow === 'visible')
      openedWho.push(String(e.className.baseVal !== undefined ? e.className.baseVal : e.className).slice(0, 30)); });
    // the chain from the picture up to the node, with each box's computed overflow
    const viz = w.querySelector('.tp-viz'); const chain = [];
    if (viz) { let e = viz; while (e && e !== w.parentElement) {
      chain.push(String(e.className.baseVal !== undefined ? e.className.baseVal : e.className).slice(0, 26)
                 + '{' + getComputedStyle(e).overflow + (e.style.overflow ? '/inline:' + e.style.overflow : '') + '}');
      e = e.parentElement; } }
    const vr = viz ? viz.getBoundingClientRect() : null;
    return { node: [Math.round(nr.width), Math.round(nr.height)], spill: Math.round(worst), who: String(who).slice(0, 46),
             opened, openedWho, chain, vizBox: vr ? [Math.round(vr.width), Math.round(vr.height)] : null,
             vzTransform: viz ? (viz.style.getPropertyValue('--vz') || '') : '' };
  });
  const R = { errs };
  R.normal = await spill();
  await p.evaluate(() => window.__tpViz(true)); await sleep(1600);
  R.eyeMode = await spill();
  await p.screenshot({ path: process.argv[3] + '/tp33-ext-eye.png' });
  await p.evaluate(() => window.__tpViz(false)); await sleep(1400);
  R.backToNormal = await spill();
  console.log(JSON.stringify(R, null, 1));
  await b.close();
})().catch(e => { console.log('FAIL', e.message); process.exit(1); });
