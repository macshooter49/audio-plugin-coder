const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 });
  const errs = []; p.on('pageerror', e => errs.push('PAGEERROR ' + e.message));
  const html = process.argv[2], out = process.argv[3];
  await p.evaluateOnNewDocument(() => { try { localStorage.removeItem('tpLayout'); } catch (e) {} });
  await p.goto('file://' + html + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); ['b','c','d'].forEach(o => document.getElementById('osc-' + o + '-device').classList.add('osc-off')); document.getElementById('osc-a-device').classList.remove('osc-off'); try { window.__fxrAdd('reverb'); window.__fxrAdd('delay'); const dv = window.__fxrDevs(); const last = dv[dv.length - 1]; last.on = false; window.__fxrRender(); } catch (e) {} try { window.__flowSetChain(['arp', 'glitch']); } catch (e) {} document.getElementById('tape-toggle').click(); document.getElementById('loop-toggle').click(); });
  await sleep(600);
  const snap = () => p.evaluate(() => {
    const sels = ['#syn-panel .device.flow', '#flow-modes', '#flow-modes .flow-mode[data-mode="arp"]', '#flow-modes .flow-mode[data-mode="chop"]', '#flow-modes .flow-mode[data-mode="drift"]', '#flow-modes .flow-mode[data-mode="glitch"]', '#flow-device .flow-vis', '#flow-device .vis-stage', '#reso-cv', '#syn-panel .device.mod', '#syn-panel .device.envs', '#filter-device', '#osc-a-device', '#osc-a-device .osc-display', '#syn-panel .voice-meta', '#fxr-rack', '#fxPageTape', '#tape-loop-content', '#tape-reel-canvas', '.env-canvas', '.filt-bg', '.mv-scope', '#tapeMechCanvas', '.tape-viz-row'];
    const o = {}; sels.forEach(s => { const e = document.querySelector(s); if (!e) { o[s] = null; return; } const r = e.getBoundingClientRect(); o[s] = { r: [r.x, r.y, r.width, r.height].map(v => Math.round(v)).join(','), st: e.getAttribute('style') || '', cls: e.className && e.className.baseVal != null ? e.className.baseVal : String(e.className), disp: getComputedStyle(e).display, parent: e.parentElement ? (e.parentElement.id || e.parentElement.className) : '' }; });
    o.tileOrder = [...document.querySelectorAll('#flow-modes > *')].map(t => t.getAttribute('data-mode') || t.tagName).join(',');
    o.modesChildren = document.getElementById('flow-modes').children.length;
    o.waterfall = JSON.stringify(window.wtWaterfall ? window.wtWaterfall.on : null);
    o.fxOn = (window.__fxrDevs ? window.__fxrDevs() : []).filter(Boolean).map(d => d.core + ':' + d.on).join(',');
    return o;
  });
  const A = await snap(); await p.screenshot({ path: out + '-home-before.png' });
  // round trip 1: open, viz on, extend a flow, viz off, close
  await p.evaluate(() => document.getElementById('syn-btn').click()); await sleep(1500);
  await p.evaluate(() => window.__tpViz(true)); await sleep(500);
  await p.evaluate(() => window.__tpExt('flow-glitch', true)); await sleep(400);
  await p.evaluate(() => window.__tpViz(false)); await sleep(300);
  await p.evaluate(() => document.getElementById('syn-btn').click()); await sleep(900);
  const B = await snap(); await p.screenshot({ path: out + '-home-after1.png' });
  // round trip 2: plain open/close
  await p.evaluate(() => document.getElementById('syn-btn').click()); await sleep(1500);
  await p.evaluate(() => document.getElementById('syn-btn').click()); await sleep(900);
  const C = await snap(); await p.screenshot({ path: out + '-home-after2.png' });
  const diff = (X, Y) => { const d = {}; Object.keys(X).forEach(k => { if (JSON.stringify(X[k]) !== JSON.stringify(Y[k])) d[k] = { before: X[k], after: Y[k] }; }); return d; };
  console.log(JSON.stringify({ diff1: Object.keys(diff(A, B)), diff2: Object.keys(diff(A, C)), wf: [A.waterfall, C.waterfall], fx: [A.fxOn, C.fxOn], errs }, null, 1));
  await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
