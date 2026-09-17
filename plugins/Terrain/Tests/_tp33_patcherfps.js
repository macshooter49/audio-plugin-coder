// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp33 — HOW FAST IS THE PATCHER, AND WHAT IS IT SPENDING THE FRAME ON?
//  Max: "the randomization gets super slow whenever we have a lot of nodes, around 15. It doesn't
//  lag on the synth section. When I zoom in things get smooth but zoomed out it breaks up."
//  Builds a canvas with N nodes, then measures the ACTUAL frame interval (rAF deltas) and the
//  self-time of the frame dispatcher, at a few zoom levels. Numbers, not opinions.
//    node Tests/_tp33_patcherfps.js <abs path to index.html> <screenshot dir>
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 1100, height: 760, deviceScaleFactor: 2 });
  const errs = []; p.on('pageerror', e => errs.push('PAGEERROR ' + e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.setItem('tpLayout', JSON.stringify({ v: 6, pos: {}, ext: {} })); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1700);
  await p.evaluate(() => {
    document.documentElement.setAttribute('data-theme', 'dark');
    ['a','b','c','d'].forEach(o => { const d = document.getElementById('osc-' + o + '-device'); if (d) d.classList.remove('osc-off'); });
    try {
      ['reverb','delay','saturate','granular','cho','fla','pha','eqz','bod'].forEach(k => window.__fxrAdd(k));
      window.__flowSetChain(['glitch','chop','arp']);
      const t = document.getElementById('tape-toggle'); if (t && t.classList.contains('off')) t.click();
    } catch (e) {}
  });
  await sleep(700); await p.evaluate(() => window.setActivePanel('tp')); await sleep(3000);
  await p.evaluate(() => window.__tpFit()); await sleep(800);

  // ⚠️ rAF cadence is useless headless — it ticks at a synthetic 60 Hz whatever the page costs.
  //    What matters is the WORK per frame, so take a real CPU profile and attribute self-time.
  // 🪤 HEADLESS HAS NO C++ PUSH LANE. The painters are driven by window.__tiFrame(), which the
  //    editor calls once per shipped frame (~60 Hz); with no editor only the 250 ms fallback lane
  //    fires, so an unmodified profile shows a page that is 90% idle and measures nothing. Drive
  //    the lane at 60 Hz here — that is what the plugin does.
  await p.evaluate(() => { window.__tp33drive = setInterval(() => { try { window.__tiFrame && window.__tiFrame(); } catch (e) {} }, 16); });
  const client = await p.target().createCDPSession();
  await client.send('Profiler.enable');
  const profile = async (label, z) => {
    if (z != null) await p.evaluate(zz => { const v = window.__tpView(); window.__tpSetView(v.x, v.y, zz); }, z);
    await sleep(900);
    await client.send('Profiler.setSamplingInterval', { interval: 200 });
    await client.send('Profiler.start');
    await sleep(4000);
    const { profile: pr } = await client.send('Profiler.stop');
    const self = new Map(); let total = 0;
    const byId = new Map(pr.nodes.map(n => [n.id, n]));
    for (const n of pr.nodes) {
      const hits = n.hitCount || 0; if (! hits) continue;
      total += hits;
      const f = n.callFrame;
      const nm = (f.functionName || '(anonymous)') + '  @' + (f.url || '').split('/').pop() + ':' + f.lineNumber;
      self.set(nm, (self.get(nm) || 0) + hits);
    }
    const top = [...self.entries()].sort((a, b) => b[1] - a[1]).slice(0, 8);
    const ms = (pr.endTime - pr.startTime) / 1000;
    const nodes = await p.evaluate(() => (window.__tpNodes ? window.__tpNodes().length : 0));
    const cv = await p.evaluate(() => { let px = 0, k = 0;
      document.querySelectorAll('#tp-page canvas').forEach(c => { if (c.width && c.height) { px += c.width * c.height; k++; } });
      return { k, mpx: +(px / 1e6).toFixed(2) }; });
    console.log(`\n  ${label}  (${nodes} nodes, ${cv.k} canvases, ${cv.mpx} Mpx)  busy ${(100 * total * 0.0002 / ms * 1000).toFixed(0)}% of the wall`);
    for (const [nm, h] of top)
      console.log(`      ${(100 * h / Math.max(1, total)).toFixed(1).padStart(5)}%  ${nm}`);
  };
  console.log('\n== tp33 - WHAT THE PATCHER SPENDS ITS FRAME ON ==');
  await profile('PATCHER, zoom 1.00', 1.0);
  await profile('PATCHER, zoom 0.40', 0.4);
  await p.evaluate(() => window.setActivePanel('syn')); await sleep(1500);
  await profile('THE SYNTH PAGE', null);
  console.log('\n  page errors:', errs.length ? errs : 'none', '\n');
  await b.close();
})().catch(e => { console.log('FAIL', e.message); process.exit(1); });
