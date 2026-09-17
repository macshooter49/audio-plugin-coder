// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp31 — THE FILTER CARD DRAWS THE MASTER-OUTPUT SPECTRUM.
//  Max: "I'm playing something and there isn't a background filter visualizer."
//  Root cause (measured in Tests/au_viz_feed.cpp): pre/post are tapped upstream of the FX rack,
//  so a routed oscillator never reaches them and the card drew a spectrum of silence.
//  The engine now also publishes `out` — the FINAL master buffer — and the card reads it.
//  This proves the page half end to end, in ink: a frame carrying ONLY `out` must light the
//  filter card's spectrum, exactly as a pre/post frame used to.
//    node Tests/_tp31_vizfeed.js <abs path to index.html> <screenshot dir>
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 1100, height: 800, deviceScaleFactor: 2 });
  const errs = []; p.on('pageerror', e => errs.push('PAGEERROR ' + e.message));
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1700);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark');
    const d = document.getElementById('osc-a-device'); if (d) d.classList.remove('osc-off'); });
  await sleep(700);
  // how much WHITE ink the filter canvas holds (the spectrum is rgba(236,232,242,..); the curve
  // is purple and is excluded by requiring the blue channel not to dominate)
  const ink = () => p.evaluate(() => {
    const cv = document.querySelector('#syn-panel .device.filter .filt-bg canvas');
    if (!cv || !cv.width) return -1;
    const g = cv.getContext('2d'); const im = g.getImageData(0, 0, cv.width, cv.height).data;
    let n = 0;
    for (let i = 0; i < im.length; i += 4) {
      const r = im[i], gg = im[i + 1], bl = im[i + 2], a = im[i + 3];
      if (a > 12 && r > 90 && gg > 90 && bl - r < 26) n++;   // whitish, not the purple curve
    }
    return n;
  });
  const feed = key => p.evaluate(k => {
    const N = 1024, SR = 48000, FFT = 2048;
    if (window.__tp31t) clearInterval(window.__tp31t);
    window.__tp31t = setInterval(() => {
      const arr = new Array(N).fill(0);
      for (let i = 0; i < N; i++) arr[i] = 0.004 + Math.random() * 0.002;
      for (let h = 1; h <= 16; h++) { const bin = Math.round(180 * h * FFT / SR); if (bin < N) arr[bin] = 0.09 / h; }
      const d = { sr: SR };
      if (k === 'out') d.out = arr; else { d.pre = arr; d.post = arr; }
      try { window.__terrainEqAnalyzer(d); } catch (e) {}
    }, 60);
  }, key);
  const stop = () => p.evaluate(() => { if (window.__tp31t) clearInterval(window.__tp31t); });

  const R = { errs };
  await sleep(600); R.ink_noFeed = await ink();
  await feed('out');  await sleep(1500); R.ink_outOnly  = await ink();
  await stop();
  await p.screenshot({ path: process.argv[3] + '/tp31-filter-out.png', clip: await p.evaluate(() => {
    const d = document.querySelector('#syn-panel .device.filter'); const r = d.getBoundingClientRect();
    return { x: r.x, y: r.y, width: r.width, height: r.height }; }) });
  // and a frame that carries only the EQ panel's pair must not blank the card
  await feed('post'); await sleep(1500); R.ink_postOnly = await ink();
  await stop();
  // ── and the MASTER EQ PANEL, whose pre/post pair tp31 deliberately left alone: open page 2,
  //    feed it a pre/post frame, and its own canvas must take ink. (It is the consumer whose
  //    __terrainEqAnalyzer entry point gained an early return for frames that carry no pair.)
  const eqInk = () => p.evaluate(() => {
    const cv = document.getElementById('eq-canvas'); if (!cv || !cv.width) return -1;
    const im = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height).data;
    let n = 0; for (let i = 0; i < im.length; i += 4) if (im[i + 3] > 12) n++; return n; });
  await p.evaluate(() => { try { window.setActivePanel('eq'); } catch (e) {} }); await sleep(900);
  R.eqInk_before = await eqInk();
  await feed('post'); await sleep(1500); R.eqInk_postFed = await eqInk();
  await stop();
  await feed('out');  await sleep(1200); R.eqInk_outOnly = await eqInk();   // must not blank it
  await stop();
  console.log(JSON.stringify(R, null, 1));
  await b.close();
})().catch(e => { console.log('FAIL', e.message); process.exit(1); });
