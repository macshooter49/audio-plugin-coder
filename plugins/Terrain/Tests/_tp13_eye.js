// tp13 — the eye: what is EMPTY when the emblem view is on (Max: "the LFOs and shit disappear")
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 900, height: 700, deviceScaleFactor: 2 }); const errs = []; p.on('pageerror', e => errs.push(e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.removeItem('tpLayout'); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1400);
  await p.evaluate(() => { document.getElementById('osc-a-device').classList.remove('osc-off'); try { window.__fxrAdd('reverb'); window.__fxrAdd('delay'); window.__flowSetChain(['arp', 'glitch']); } catch (e) {} window.setActivePanel('tp'); });
  await sleep(2000); await p.evaluate(() => window.__tpFit()); await sleep(400);
  await p.evaluate(() => window.__tpViz(true)); await sleep(1200);
  const R = await p.evaluate(() => [...document.querySelectorAll('#tp-page .tp-node')].map(w => {
    const k = w.dataset.key, v = w.querySelector('.tp-viz');
    if (!v) return { k, viz: null };
    const r = v.getBoundingClientRect(), wr = w.getBoundingClientRect();
    const paths = [...v.querySelectorAll('path')].filter(pp => (pp.getAttribute('d') || '').length > 8).length;
    const cvs = [...v.querySelectorAll('canvas')].concat(v.tagName === 'CANVAS' ? [v] : []);
    const ink = cvs.map(c => { try { const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 3; i < d.length; i += 32) if (d[i] > 8) n++; return n; } catch (e) { return 'err'; } });
    const cs = getComputedStyle(v);
    return { k, cls: (v.className.baseVal != null ? v.className.baseVal : v.className).toString().slice(0, 26), w: Math.round(r.width), h: Math.round(r.height),
      insideNode: r.left >= wr.left - 2 && r.right <= wr.right + 2 && r.top >= wr.top - 2 && r.bottom <= wr.bottom + 2,
      vis: cs.visibility, op: cs.opacity, paths, ink };
  }));
  await p.screenshot({ path: process.argv[3] + '/tp13-eye.png' });
  console.log(JSON.stringify({ nodes: R, errs }, null, 1)); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
