// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb503 — HOW MANY COMPOSITING LAYERS IS THIS PAGE ASKING THE GPU TO HOLD?
//
//    NODE_PATH=<node_modules> CHROME_PATH=<chrome.exe> node Tests/ui_layers.js [--syn]
//
//  The trace says GPUTask is 87% of one core and Style/Layout/Paint are single digits, so the
//  cost is COMPOSITING, not our JS and not our DOM writes. Rate reduction has now been falsified
//  four separate ways (hero half-rate, full canvas change-gate, global rAF cap, C++ push rate) —
//  every one of them measured WORSE or neutral in the real plugin. So the cost is not
//  proportional to frames; it behaves like a fixed per-vsync price for compositing a very large
//  layer tree.
//
//  This counts that tree, and ranks the layers by area, so "reduce the layer tree" stops being a
//  slogan and becomes a list of elements. The one change that ever moved the number in the right
//  direction was removing backdrop-filter (110.4% -> 99.1%), which is exactly a layer-tree change.
const puppeteer = require('puppeteer-core');
const path = require('path');

const SYN = process.argv.includes('--syn');
const PAGE = path.join(__dirname, '..', 'Source', 'ui', 'public', 'index.html');

(async () => {
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: false,
    args: ['--no-sandbox', '--allow-file-access-from-files', '--window-size=1500,950'],
    defaultViewport: null,
  });
  const page = (await browser.pages())[0] || await browser.newPage();
  await page.goto('file:///' + PAGE.replace(/\\/g, '/'), { waitUntil: 'load', timeout: 60000 });
  await new Promise(r => setTimeout(r, 4000));
  if (SYN) {
    await page.evaluate(() => { const b = document.getElementById('syn-btn'); if (b) b.click(); });
    await new Promise(r => setTimeout(r, 3500));
  }

  const client = await page.target().createCDPSession();
  await client.send('DOM.enable');
  await client.send('LayerTree.enable');
  const layers = await new Promise(resolve => {
    client.on('LayerTree.layerTreeDidChange', e => resolve(e.layers || []));
    setTimeout(() => resolve([]), 8000);
  });

  console.log(`\n  COMPOSITING LAYERS — ${SYN ? 'SYNTH PAGE' : 'FRONT PAGE'}\n`);
  if (!layers.length) { console.log('    (no layer tree reported)\n'); await browser.close(); return; }

  let px = 0;
  const rows = layers.map(l => {
    const a = (l.width || 0) * (l.height || 0);
    px += a;
    return { id: l.layerId, w: l.width | 0, h: l.height | 0, area: a, paints: l.paintCount || 0 };
  }).sort((a, b) => b.area - a.area);

  console.log(`    LAYER COUNT : ${layers.length}`);
  console.log(`    TOTAL AREA  : ${(px / 1e6).toFixed(1)} Mpx  (a 1500x950 viewport is 1.4 Mpx —`);
  console.log(`                  every multiple of that is another full-screen surface the GPU`);
  console.log(`                  composites on EVERY vsync, whether or not it changed)\n`);
  console.log('    largest layers:      size            Mpx   paints');
  for (const r of rows.slice(0, 15))
    console.log(`      ${String(r.w + 'x' + r.h).padEnd(16)} ${(r.area / 1e6).toFixed(2).padStart(8)}  ${String(r.paints).padStart(7)}`);

  // how much of the page is asking for effects that force their own layer / a readback
  const css = await page.evaluate(() => {
    let bd = 0, sh = 0, tf = 0, wc = 0, fl = 0;
    document.querySelectorAll('*').forEach(el => {
      const s = getComputedStyle(el);
      if (s.backdropFilter && s.backdropFilter !== 'none') bd++;
      if (s.boxShadow && s.boxShadow !== 'none') sh++;
      if (s.transform && s.transform !== 'none') tf++;
      if (s.willChange && s.willChange !== 'auto') wc++;
      if (s.filter && s.filter !== 'none') fl++;
    });
    return { elements: document.querySelectorAll('*').length, bd, sh, tf, wc, fl,
             canvases: document.querySelectorAll('canvas').length };
  });
  console.log(`\n    LIVE ELEMENTS ${css.elements}   canvases ${css.canvases}`);
  console.log(`    backdrop-filter ${css.bd}   filter ${css.fl}   box-shadow ${css.sh}   transform ${css.tf}   will-change ${css.wc}\n`);
  await browser.close();
})().catch(e => { console.error(e); process.exit(1); });
