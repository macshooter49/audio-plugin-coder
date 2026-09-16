// tp13 — what the dice actually produces: filter 2 rate, filter-type spread, LFO targets, engines
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656 }); const errs = []; p.on('pageerror', e => errs.push(e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.removeItem('tpLayout'); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1400);
  await p.evaluate(() => { window.setActivePanel('tp'); }); await sleep(1600);
  const out = [];
  for (let i = 0; i < 10; i++) {
    const lvl = i < 5 ? 'wild' : 'heavy';
    await p.evaluate(l => { const L = window.__tpLayout(); L.dlevel = l; L.aim = 'anything'; window.__tpGenerate(); }, lvl); await sleep(2500);
    out.push(await p.evaluate(l => {
      const P = id => window.__tpParam(id), O = ['A', 'B', 'C', 'D'];
      const NF = (window.__fltRoster && window.__fltRoster.check) ? (window.__fltRoster.check() || {}).count : null;
      const routes = (window.__tiRoutes() || []).map(r => r.s + '>' + r.d);
      return { lvl: l, f1: +P('SYN_FILTER1_TYPE').toFixed(4), f2type: +P('SYN_FILTER2_TYPE').toFixed(4),
        f2mix: O.map(o => +P('SYN_OSC_' + o + '_F2MIX').toFixed(2)).join(','), f1mix: O.map(o => +P('SYN_OSC_' + o + '_F1MIX').toFixed(2)).join(','),
        eng: O.map(o => Math.round(P('SYN_OSC_' + o + '_ENGINE') * 6)).join(''), en: O.map(o => Math.round(P('SYN_OSC_' + o + '_ENABLE'))).join(''),
        nRoutes: routes.length, routes: routes.join(' '), rosterCount: NF };
    }, lvl));
  }
  console.log(JSON.stringify({ rolls: out, errs }, null, 0)); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
