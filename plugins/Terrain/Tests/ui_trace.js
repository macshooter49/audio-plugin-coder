// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb503 — WHERE DOES THE WEBVIEW'S TIME ACTUALLY GO?
//
//    NODE_PATH=<node_modules> CHROME_PATH=<chrome.exe> node Tests/ui_trace.js [--syn] [--seconds=10]
//
//  fb501 measured that opening the window costs ~190% of a core with ~174% of it inside WebView2.
//  fb502 then proved the obvious lever WRONG: cutting canvas draw calls (hero half-rate, and a
//  full change-gate over knobs/icons) made it MEASURABLY WORSE, counterbalanced, both times. So
//  "too many draws" is not the mechanism and guessing again is a waste of a build cycle.
//
//  This asks the renderer directly. It records a devtools timeline trace and reports SELF time
//  per event, bucketed the way DevTools does: Scripting / Style / Layout / Paint+Raster /
//  Composite / GPU. Self time (a node's duration minus its children's) is the honest measure —
//  raw durations double-count, because FireAnimationFrame contains the Layout it forces.
//
//  Read it like this:
//    Style+Layout dominant   -> the per-frame DOM writes (style.left, setAttribute('d'), textContent)
//    Paint/Raster dominant   -> too much AREA being repainted, or expensive effects (blur/shadow)
//    Composite/GPU dominant  -> too many layers, or layers being re-created every frame
//  Chrome is WebView2's engine, so this measures the same renderer — and headful, so rAF is
//  vsync-paced instead of the 2000-5000 fps a headless run free-runs at.
const puppeteer = require('puppeteer-core');
const path = require('path');

const SYN = process.argv.includes('--syn');
const SECS = Number((process.argv.find(a => a.startsWith('--seconds=')) || '').split('=')[1] || 10);
const PAGE = path.join(__dirname, '..', 'Source', 'ui', 'public', 'index.html');

const BUCKET = {
  Scripting: ['FunctionCall', 'EvaluateScript', 'TimerFire', 'FireAnimationFrame', 'FireIdleCallback',
              'RunMicrotasks', 'MajorGC', 'MinorGC', 'GCEvent', 'V8.Execute'],
  Style:     ['UpdateLayoutTree', 'ScheduleStyleRecalculation', 'InvalidateLayout', 'RecalculateStyles'],
  Layout:    ['Layout', 'UpdateLayerTree', 'HitTest', 'PrePaint'],
  Paint:     ['Paint', 'PaintImage', 'Rasterize', 'RasterTask', 'DecodeImage', 'ResizeImage',
              'Draw LazyPixelRef', 'Decode LazyPixelRef'],
  Composite: ['CompositeLayers', 'Commit', 'ProxyImpl::BeginMainFrame', 'ActivateLayerTree',
              'DrawFrame', 'BeginFrame', 'NeedsBeginFrameChanged'],
  GPU:       ['GPUTask'],
};
const nameToBucket = {};
for (const [b, names] of Object.entries(BUCKET)) for (const n of names) nameToBucket[n] = b;

(async () => {
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: false,                       // vsync-paced rAF; headless free-runs and distorts everything
    args: ['--no-sandbox', '--allow-file-access-from-files', '--window-size=1500,950'],
    defaultViewport: null,
  });
  const page = (await browser.pages())[0] || await browser.newPage();

  // --cap<N> : coalesce EVERY rAF callback onto one shared clock capped at N fps.
  // fb502 halved ONE canvas's redraw rate and CPU went UP, because the frame was still produced
  // and the whole page still recomposited. If the cost is per-FRAME rather than per-DRAW, then
  // capping the rate GLOBALLY -- so fewer frames exist at all -- is the only thing that can help.
  // Injected here rather than in index.html so the hypothesis costs seconds, not a 5-minute build.
  const capArg = process.argv.find(a => a.startsWith('--cap'));
  if (capArg) {
    const fps = Number(capArg.slice(5)) || 30;
    await page.evaluateOnNewDocument((fpsIn) => {
      const raf = window.requestAnimationFrame.bind(window);
      const minGap = 1000 / fpsIn;
      let q = [], scheduled = false, last = -1e9;
      function pump(ts) {
        scheduled = false;
        if (ts - last < minGap) { scheduled = true; raf(pump); return; }
        last = ts;
        const cbs = q; q = [];
        for (let i = 0; i < cbs.length; i++) { try { cbs[i](ts); } catch (e) {} }
      }
      window.requestAnimationFrame = function (cb) {
        q.push(cb);
        if (!scheduled) { scheduled = true; raf(pump); }
        return q.length;
      };
      window.cancelAnimationFrame = function () {};
    }, fps);
    console.log(`  [rAF globally capped to ${fps} fps]`);
  }

  await page.goto('file:///' + PAGE.replace(/\\/g, '/'), { waitUntil: 'load', timeout: 60000 });
  await new Promise(r => setTimeout(r, 4000));

  if (SYN) {
    await page.evaluate(() => { const b = document.getElementById('syn-btn'); if (b) b.click(); });
    await new Promise(r => setTimeout(r, 3500));
  }
  const panel = await page.evaluate(() =>
    (typeof currentActivePanel !== 'undefined' && currentActivePanel) ? String(currentActivePanel) : '(front page)');

  const TRACE_FILE = require('path').join(require('os').tmpdir(), 'terrain-ui-trace.json');
  await page.tracing.start({
    path: TRACE_FILE,
    categories: ['devtools.timeline', 'disabled-by-default-devtools.timeline', 'blink.user_timing'],
  });
  await new Promise(r => setTimeout(r, SECS * 1000));
  await page.tracing.stop();
  await browser.close();

  let raw = require('fs').readFileSync(TRACE_FILE, 'utf8');
  if (raw.charCodeAt(0) === 0xFEFF) raw = raw.slice(1);   // the trace file is written with a BOM
  const trace = JSON.parse(raw);
  const evs = (trace.traceEvents || []).filter(e => e.ph === 'X' && typeof e.dur === 'number' && e.dur > 0);

  // self time = duration minus the duration of nested children, computed per thread
  const byThread = new Map();
  for (const e of evs) {
    const k = e.pid + ':' + e.tid;
    if (!byThread.has(k)) byThread.set(k, []);
    byThread.get(k).push(e);
  }
  const self = {};
  let wallUs = 0, tMin = Infinity, tMax = -Infinity;
  for (const list of byThread.values()) {
    list.sort((a, b) => (a.ts - b.ts) || (b.dur - a.dur));
    const stack = [];
    for (const e of list) {
      while (stack.length && stack[stack.length - 1]._end <= e.ts) stack.pop();
      const p = stack[stack.length - 1];
      if (p) p._self -= e.dur;
      e._end = e.ts + e.dur; e._self = e.dur;
      stack.push(e);
      if (e.ts < tMin) tMin = e.ts;
      if (e._end > tMax) tMax = e._end;
    }
    for (const e of list) self[e.name] = (self[e.name] || 0) + Math.max(0, e._self);
  }
  wallUs = (tMax - tMin) || (SECS * 1e6);

  const buckets = {};
  const other = [];
  for (const [name, us] of Object.entries(self)) {
    const b = nameToBucket[name];
    if (b) buckets[b] = (buckets[b] || 0) + us;
    else other.push([name, us]);
  }

  console.log(`\n  UI TRACE — ${SYN ? 'SYNTH PAGE' : 'FRONT PAGE'} (currentActivePanel = ${panel}), ${SECS}s, headful\n`);
  console.log(`    (self time per bucket, as % of one core over the traced window)\n`);
  const rows = Object.entries(buckets).sort((a, b) => b[1] - a[1]);
  let tot = 0;
  for (const [b, us] of rows) { tot += us; console.log(`    ${b.padEnd(12)} ${(us / 1000).toFixed(0).padStart(8)} ms   ${(100 * us / wallUs).toFixed(1).padStart(6)}%`); }
  console.log(`    ${'TOTAL'.padEnd(12)} ${(tot / 1000).toFixed(0).padStart(8)} ms   ${(100 * tot / wallUs).toFixed(1).padStart(6)}%`);

  other.sort((a, b) => b[1] - a[1]);
  const bigOther = other.filter(([, us]) => us / wallUs > 0.01).slice(0, 12);
  if (bigOther.length) {
    console.log(`\n    unbucketed events over 1% (name -> % of one core):`);
    for (const [n, us] of bigOther) console.log(`      ${n.slice(0, 44).padEnd(44)} ${(100 * us / wallUs).toFixed(1).padStart(6)}%`);
  }

  console.log(`\n    TOP INDIVIDUAL EVENTS`);
  Object.entries(self).sort((a, b) => b[1] - a[1]).slice(0, 14)
    .forEach(([n, us]) => console.log(`      ${n.slice(0, 44).padEnd(44)} ${(100 * us / wallUs).toFixed(1).padStart(6)}%`));
  console.log('');
})().catch(e => { console.error(e); process.exit(1); });
