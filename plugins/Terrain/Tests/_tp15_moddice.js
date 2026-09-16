// tp15 — RANDOMIZE MODULATION: envelopes, LFOs, macros and routes from every family. Purely modulation.
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 900, height: 700 }); const errs = []; p.on('pageerror', e => errs.push(e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.clear(); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { if (window.setActivePanel) window.setActivePanel('syn'); try { window.__fxrAdd('reverb'); window.__fxrAdd('cmp'); } catch (e) {}
    window.__setLog = []; const o = window.__setSynParam; window.__setSynParam = function (id, v) { window.__setLog.push(id); return o.apply(this, arguments); }; });
  await sleep(900);
  const R = { row: await p.evaluate(() => { const r = document.getElementById('fxr-dice'); const m = r && r.querySelector('[data-act="mroll"]'), l = r && r.querySelector('[data-act="mlvl"]');
    return { hasMod: !!m, text: m ? m.textContent.trim() : null, lvl: l ? l.textContent : null }; }), rolls: [] };
  for (const lvl of ['light', 'medium', 'heavy', 'wild', 'crazy']) {
    await p.evaluate(l => { window.__setLog.length = 0; window.__modDice(l); }, lvl); await sleep(700);
    R.rolls.push(await p.evaluate(l => {
      const ids = window.__setLog, routes = window.__tiRoutes() || [];
      const fams = {}; routes.forEach(r => { const f = r.s.replace(/\d+$/, ''); fams[f] = (fams[f] || 0) + 1; });
      const cmp = (window.__fxrDevs() || []).find(d => d.core === 'cmp');
      const cmpDests = []; if (cmp && window.__fxModDest) for (let k = 0; k < 12; k++) { const d = window.__fxModDest('cmp', cmp.inst, k); if (d != null) cmpDests.push(d); }
      const pitchish = routes.filter(r => r.d === 5 || (r.d >= 1854 && r.d <= 1877) || (r.d >= 42 && r.d <= 45));
      return { lvl: l, routes: routes.length, fams, depths: [Math.min(...routes.map(r => +r.v)).toFixed(2), Math.max(...routes.map(r => +r.v)).toFixed(2)],
        onCmp: routes.filter(r => cmpDests.indexOf(r.d) >= 0).length, pitchRoutes: pitchish.length,
        wroteForeign: ids.filter(id => !/^LFO\d|^SYN_ENV_|^SYN_MACRO_/.test(id)).slice(0, 6), wrote: ids.length };
    }, lvl));
  }
  R.errs = errs; console.log(JSON.stringify(R, null, 1)); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
