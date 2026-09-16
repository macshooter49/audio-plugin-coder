// tp14 — RANDOMIZE CHAIN: purely effects. It must rebuild the rack and touch NOTHING else.
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 900, height: 700 }); const errs = []; p.on('pageerror', e => errs.push(e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.clear(); } catch (e) {} });   // the synth page, not whatever page the last session left behind
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { if (window.setActivePanel) window.setActivePanel('syn'); else document.getElementById('syn-btn').click(); window.__setLog = []; const o = window.__setSynParam; window.__setSynParam = function (id, v) { window.__setLog.push(id); return o.apply(this, arguments); }; });
  await sleep(700);
  // tp16 — the row above the rack is GONE: both rolls live in the Patcher's dice menu now (Tests/_tp16_menu.js proves the menu).
  const R = { rowGone: await p.evaluate(() => !document.getElementById('fxr-dice')), rolls: [] };
  for (const lvl of ['light', 'medium', 'heavy', 'wild', 'crazy']) {
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
  R.errs = errs; console.log(JSON.stringify(R, null, 1)); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
