// tp35 — what a ?card= page costs to boot (no backend): navigation timing + a CPU profile of the boot
const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  for (const q of ['?card=gli', '?page=1']) {
    const p = await b.newPage(); await p.setViewport({ width: 330, height: 430, deviceScaleFactor: 2 });
    const cdp = await p.target().createCDPSession(); await cdp.send('Profiler.enable'); await cdp.send('Profiler.setSamplingInterval', { interval: 500 });
    await cdp.send('Profiler.start');
    const t0 = Date.now();
    await p.goto('file://' + process.argv[2] + q, { waitUntil: 'load' });
    const tLoad = Date.now() - t0; await sleep(2500);
    const { profile } = await cdp.send('Profiler.stop');
    const nav = await p.evaluate(() => { const t = performance.timing; return { domLoading: t.domLoading - t.navigationStart, dcl: t.domContentLoadedEventEnd - t.navigationStart, load: t.loadEventEnd - t.navigationStart, nodes: document.getElementsByTagName('*').length, cardOnly: window.__cardOnly }; });
    // self time per function (top 14)
    const self = {}; const byId = {}; profile.nodes.forEach(n => { byId[n.id] = n; });
    const dt = profile.timeDeltas; let idx = 0; profile.samples.forEach((id, i) => { const n = byId[id]; const k = (n.callFrame.functionName || '(anon)') + ' @' + n.callFrame.lineNumber; self[k] = (self[k] || 0) + (dt[i] || 0); });
    const top = Object.entries(self).sort((a, b) => b[1] - a[1]).slice(0, 14).map(([k, v]) => (v / 1000).toFixed(0) + ' ms ' + k);
    console.log(q, 'wall-to-load', tLoad, 'ms', JSON.stringify(nav)); console.log('   ' + top.join('\n   '));
    await p.close();
  }
  await b.close();
})().catch(e => { console.log('FAIL', e); process.exit(1); });
