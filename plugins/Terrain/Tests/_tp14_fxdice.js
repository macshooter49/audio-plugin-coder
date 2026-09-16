// tp14 — RANDOMIZE CHAIN: purely effects. It must rebuild the rack and touch NOTHING else.
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 900, height: 700 }); const errs = []; p.on('pageerror', e => errs.push(e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.clear(); } catch (e) {} });   // the synth page, not whatever page the last session left behind
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { if (window.setActivePanel) window.setActivePanel('syn'); else document.getElementById('syn-btn').click(); window.__setLog = []; const o = window.__setSynParam; window.__setSynParam = function (id, v) { window.__setLog.push(id); return o.apply(this, arguments); }; });
  await sleep(700);
  const R = { row: await p.evaluate(() => { const r = document.getElementById('fxr-dice'); if (!r) return null; const c = r.getBoundingClientRect(), rk = document.querySelector('.fxr-clip').getBoundingClientRect();
    return { visible: c.height > 0 && c.width > 0, aboveChain: c.bottom <= rk.top + 1, label: (r.querySelector('.fxr-dice-lvl') || {}).textContent, text: (r.querySelector('.fxr-dice-t') || {}).textContent }; }), rolls: [] };
  for (const lvl of ['light', 'medium', 'heavy', 'wild']) {
    const before = await p.evaluate(() => ({ routes: (window.__tiRoutes() || []).length }));
    await p.evaluate(l => { window.__setLog.length = 0; window.__fxrDice(l); }, lvl); await sleep(900);
    R.rolls.push(await p.evaluate((l, bf) => {
      const D = window.__fxrDevs() || [], ids = window.__setLog;
      const foreign = ids.filter(id => /^SYN_OSC_|^SYN_FILTER\d|^SYN_ENV|^LFO\d/.test(id));   // a device's own Phase knob (chorus/flanger) is NOT foreign
      const cmp = D.find(d => d.core === 'cmp');
      const cmpTouched = cmp ? ids.filter(id => /^SYN_CMP/.test(id)).length : 0;
      return { lvl: l, chain: D.map(d => d.core + ':' + d.type).join(' > '), n: D.length, lastIsCmp: D.length ? D[D.length - 1].core === 'cmp' : false,
        wrote: ids.length, foreign: foreign.slice(0, 5), cmpTouched, routesDelta: (window.__tiRoutes() || []).length - bf.routes,
        routed: D.map(d => (d.route || []).join('')).join(' ') };
    }, lvl, before));
  }
  // the picker
  R.menu = await p.evaluate(() => { const el = document.querySelector('#fxr-dice .fxr-dice-lvl'); const r = el.getBoundingClientRect();
    el.dispatchEvent(new MouseEvent('click', { bubbles: true, clientX: r.left + 4, clientY: r.top + 4 }));
    const m = document.querySelector('.syn-ctx-menu.act'); return m ? [...m.querySelectorAll('.syn-ctx-item')].map(x => x.textContent.trim()).join(',') : 'no menu'; });
  R.errs = errs; console.log(JSON.stringify(R, null, 1)); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
