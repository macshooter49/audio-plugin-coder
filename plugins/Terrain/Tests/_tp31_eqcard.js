// tp31 — reproduce Max's Equalizer card headlessly and look at what it actually draws,
// with and without a live analyzer feed. Evidence, not squinting at a screenshot.
const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 1100, height: 800, deviceScaleFactor: 3 });
  const errs = []; p.on('pageerror', e => errs.push('PAGEERROR ' + e.message));
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1600);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark');
    const d = document.getElementById('osc-a-device'); if (d) d.classList.remove('osc-off');
    try { window.__fxrAdd('eqz'); } catch (e) {} });
  await sleep(900);
  // Max's card: British type, Slant -19, Air +28, Amount 150%, Mix 93, every route pill lit
  await p.evaluate(() => { const S = (id, v) => { try { window.__setSynParam(id, v); } catch (e) {} };
    S('SYN_EQZ_SLANT', 0.31); S('SYN_EQZ_AIR', 0.64); S('SYN_EQZ_AMOUNT', 0.75); S('SYN_EQZ_MIX', 0.93);
    ['A','B','C','D','SUB','NOISE'].forEach(k => S('SYN_EQZ_SRC_' + k, 1));
    try { if (window.__fxrRender) window.__fxrRender(); } catch (e) {} });
  await sleep(1200);
  const shot=async n=>{ const c=await p.$('#fxr-rack .fxr-dev'); if(c) await c.screenshot({path:process.argv[3]+'/'+n}); };
  const R = { errs };
  R.hasBins_before = await p.evaluate(() => !!(window.__fltHasBins && window.__fltHasBins()));
  await shot('tp31-eq-nofeed.png');
  // what the core actually contains
  R.core = await p.evaluate(() => { const c = document.querySelector('#fxr-rack .fxr-core'); if (!c) return null;
    const out = {}; c.querySelectorAll('*').forEach(e => { const k = e.tagName.toLowerCase() + '.' + (e.getAttribute('class') || '');
      out[k] = (out[k] || 0) + 1; }); return out; });
  // now feed a realistic spectrum: a pluck — harmonics of 220 Hz over a low noise floor
  // Max's card is POWERED OFF (a hollow ring, not a filled dot). Render that state too.
  await p.evaluate(() => { try { window.__setSynParam('SYN_EQZ_POWER', 0); const d = (window.__fxrDevs ? window.__fxrDevs() : [])[0];
    if (d) { d.on = false; d.pw = false; } if (window.__fxrRender) window.__fxrRender(); } catch (e) {} });
  await sleep(700);
  await shot('tp31-eq-off.png');
  await p.evaluate(() => {
    const N = 1024, SR = 48000, FFT = 2048;
    window.__tp31feed = setInterval(() => {
      const post = new Array(N).fill(0);
      for (let i = 0; i < N; i++) post[i] = 0;   // the C++ prints a bare 0 below 0.00005 - a dense/quiet patch arrives like this
      for (let h = 1; h <= 14; h++) { const bin = Math.round(220 * h * FFT / SR); if (bin < N) post[bin] = 0.05 / h; }
      try { window.__terrainEqAnalyzer({ pre: post, post: post, sr: SR }); } catch (e) {}
    }, 66);
  });
  await sleep(1600);
  R.hasBins_after = await p.evaluate(() => !!(window.__fltHasBins && window.__fltHasBins()));
  await shot('tp31-eq-fed.png');
  console.log(JSON.stringify(R, null, 1));
  await b.close();
})().catch(e => { console.log('FAIL', e.message); process.exit(1); });
