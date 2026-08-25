// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb502 — WHICH CANVASES ARE ACTUALLY COSTING US?
//
//    NODE_PATH=<node_modules> CHROME_PATH=<chrome.exe> node Tests/canvas_profile.js [page.html]
//
//  fb501 measured, on Windows, that opening the editor costs ~190% of a core and that 86% of it
//  is the page animating itself — WebView2's gpu-process alone burns 110% rasterising canvas
//  work. What it could NOT say is WHICH canvases, or whether they are even on screen. Gating 85
//  rAF sites by hand on a guess is how you break a viz and learn nothing, so this ranks them
//  first.
//
//  It wraps CanvasRenderingContext2D so every draw call is counted per canvas, then reports each
//  canvas by redraw rate, pixel area, and — the load-bearing column — whether it is VISIBLE.
//  That is what decides the fix:
//     mostly OFF-SCREEN redraws  -> visibility gating (cheap, safe, generic)
//     mostly ON-SCREEN redraws   -> visibility gating buys little; the win must come from
//                                    not redrawing UNCHANGED content
//
//  Chrome is WebView2's engine, so this measures the same renderer. It does NOT see the
//  gpu-process cost (no profiler does, from inside) — but draws x pixels is what CREATES that
//  cost, so the ranking is the right proxy.
const puppeteer = require('puppeteer-core');
const path = require('path');

const PAGE = process.argv[2] || path.join(__dirname, '..', 'Source', 'ui', 'public', 'index.html');
const SECONDS = Number(process.env.CV_SECONDS || 10);

(async () => {
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new',
    args: ['--no-sandbox', '--allow-file-access-from-files', '--window-size=1400,900'],
  });
  const page = await browser.newPage();
  await page.setViewport({ width: 1400, height: 900 });

  // Install the counters BEFORE any page script runs, so no canvas escapes them.
  await page.evaluateOnNewDocument(() => {
    window.__cvSeq = 0;
    window.__cvList = [];
    window.__rafTicks = 0;
    const origRaf = window.requestAnimationFrame;
    window.requestAnimationFrame = function (fn) {
      return origRaf.call(window, function (t) { window.__rafTicks++; return fn(t); });
    };
    const OPS = ['clearRect', 'fillRect', 'stroke', 'fill', 'drawImage', 'putImageData',
                 'strokeText', 'fillText', 'strokeRect', 'arc', 'lineTo', 'moveTo', 'beginPath'];
    // clearRect is the honest "a full redraw started" marker; the rest measure how heavy it is.
    const origGet = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function (type) {
      const ctx = origGet.apply(this, arguments);
      try {
        if (ctx && String(type).indexOf('2d') === 0 && !ctx.__cvWrapped) {
          ctx.__cvWrapped = true;
          const cv = this;
          if (cv.__cvId == null) {
            cv.__cvId = ++window.__cvSeq;
            window.__cvList.push(cv);
            cv.__cvOps = 0; cv.__cvClears = 0;
          }
          for (const m of OPS) {
            const orig = ctx[m];
            if (typeof orig !== 'function') continue;
            ctx[m] = function () {
              cv.__cvOps++;
              if (m === 'clearRect') cv.__cvClears++;
              return orig.apply(this, arguments);
            };
          }
        }
      } catch (e) {}
      return ctx;
    };
  });

  const url = 'file:///' + PAGE.replace(/\\/g, '/');
  await page.goto(url, { waitUntil: 'load', timeout: 60000 });
  await new Promise(r => setTimeout(r, 4000));          // let the UI settle and loops spin up

  // CV_CLICK lets the profile target a specific page. The front page and the synth page have
  // completely different costs: fb487 already gates the whole hero/meters/icons master loop
  // behind `currentActivePanel`, so profiling the default page says nothing about the synth
  // page — which is the one Max reports as still bad.
  if (process.env.CV_CLICK) {
    await page.evaluate(sel => { const b = document.querySelector(sel); if (b) b.click(); }, process.env.CV_CLICK);
    await new Promise(r => setTimeout(r, 3500));
    const panel = await page.evaluate(() => (typeof currentActivePanel !== 'undefined' && currentActivePanel) ? String(currentActivePanel) : '(none)');
    console.log(`\n  clicked ${process.env.CV_CLICK} -> currentActivePanel = ${panel}`);
  }

  await page.evaluate(() => {
    window.__cvList.forEach(cv => { cv.__cvOps0 = cv.__cvOps; cv.__cvClears0 = cv.__cvClears; });
    window.__raf0 = window.__rafTicks;
    window.__t0 = performance.now();
  });

  await new Promise(r => setTimeout(r, SECONDS * 1000));

  const data = await page.evaluate(() => {
    const secs = (performance.now() - window.__t0) / 1000;
    const vw = window.innerWidth, vh = window.innerHeight;
    const rows = window.__cvList.map(cv => {
      const r = cv.getBoundingClientRect();
      const onScreen = r.width > 0 && r.height > 0 && r.bottom > 0 && r.right > 0 && r.top < vh && r.left < vw;
      const displayed = cv.offsetParent !== null || getComputedStyle(cv).position === 'fixed';
      let id = cv.id || '';
      if (!id) { let p = cv.parentElement, hops = 0; while (p && !id && hops++ < 4) { id = p.id || (p.className && String(p.className).split(' ')[0]) || ''; p = p.parentElement; } }
      return {
        id: id || ('canvas#' + cv.__cvId),
        w: cv.width, h: cv.height,
        cssW: Math.round(r.width), cssH: Math.round(r.height),
        visible: !!(onScreen && displayed),
        clears: cv.__cvClears - (cv.__cvClears0 || 0),
        ops: cv.__cvOps - (cv.__cvOps0 || 0),
      };
    });
    return { secs, rafTicks: window.__rafTicks - window.__raf0, rows, total: window.__cvList.length };
  });

  const fps = data.rafTicks / data.secs;
  console.log(`\n  CANVAS REDRAW PROFILE — ${data.secs.toFixed(1)}s, ${data.total} canvases, ` +
              `rAF ran at ${fps.toFixed(0)} fps\n`);
  console.log('    (redraws/s is clearRect rate; px/s = redraws/s x backing-store pixels)\n');

  const rows = data.rows.map(r => ({
    ...r,
    rps: r.clears / data.secs,
    ops: r.ops / data.secs,
    pxs: (r.clears / data.secs) * r.w * r.h,
  })).sort((a, b) => b.pxs - a.pxs);

  console.log('    VIS  id                                  size        redraws/s   ops/s      Mpx/s');
  let visPx = 0, hidPx = 0, visN = 0, hidN = 0;
  for (const r of rows) {
    if (r.rps < 0.05 && r.ops < 1) continue;
    if (r.visible) { visPx += r.pxs; visN++; } else { hidPx += r.pxs; hidN++; }
    console.log(`    ${r.visible ? ' ON' : 'off'}  ${r.id.slice(0, 34).padEnd(34)}  ${String(r.w + 'x' + r.h).padEnd(10)}  ` +
                `${r.rps.toFixed(1).padStart(8)}  ${r.ops.toFixed(0).padStart(8)}  ${(r.pxs / 1e6).toFixed(1).padStart(9)}`);
  }
  const tot = visPx + hidPx;
  console.log(`\n    ON-SCREEN : ${visN} canvases, ${(visPx / 1e6).toFixed(0)} Mpx/s  (${tot ? (100 * visPx / tot).toFixed(0) : 0}%)`);
  console.log(`    OFF-SCREEN: ${hidN} canvases, ${(hidPx / 1e6).toFixed(0)} Mpx/s  (${tot ? (100 * hidPx / tot).toFixed(0) : 0}%)`);
  console.log(`\n    => ${hidPx > visPx ? 'VISIBILITY gating is the win.' : 'Most redraw is ON SCREEN — visibility gating alone will NOT be enough;'}`);
  if (hidPx <= visPx) console.log('       the win has to come from not redrawing UNCHANGED content.\n');
  else console.log('');

  await browser.close();
})().catch(e => { console.error(e); process.exit(1); });
