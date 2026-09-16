const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 1 });
  const errs = []; p.on('pageerror', e => errs.push('PAGEERROR ' + e.message));
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); window.__setLog = []; const orig = window.__setSynParam; window.__setSynParam = function (id, v) { window.__setLog.push([id, v]); if (orig) try { orig(id, v); } catch (e) {} }; document.getElementById('syn-btn').click(); });
  await sleep(1600);
  const out = [];
  for (const lvl of ['wild', 'wild', 'wild', 'heavy', 'heavy', 'light']) {
    await p.evaluate(l => { const L = window.__tpLayout(); L.dlevel = l; L.aim = 'anything'; }, lvl);
    await p.evaluate(() => window.__tpGenerate()); await sleep(2400);
    out.push(await p.evaluate(l => {
      const P = id => window.__tpParam(id);
      const on = ['a', 'b', 'c', 'd'].filter(o => !document.getElementById('osc-' + o + '-device').classList.contains('osc-off'));
      const devs = window.__fxrDevs().filter(Boolean);
      const routes = window.__tiRoutes();
      const fxLo = 1000;   // fx mod dests are large numbers
      return { lvl: l, on: on.join(''), uniVoices: on.map(o => Math.round(P('SYN_OSC_' + o.toUpperCase() + '_UNISON') * 15 + 1)),
        fx: devs.map(d => d.core + ':' + d.type + ':' + (d.route || []).slice(0, 4).join('')), lastIsCmp: devs.length && devs[devs.length - 1].core === 'cmp',
        routes: routes.map(r => r.s + '>' + r.d + (r.depth != null ? '@' + (+r.depth).toFixed(2) : '')), envRoutes: routes.filter(r => /^env/.test(r.s)).length, fxRoutes: routes.filter(r => r.d > 200 && r.d !== 673 && r.d < 5000).length,
        f2: P('SYN_OSC_A_F2MIX') + P('SYN_OSC_B_F2MIX') + P('SYN_OSC_C_F2MIX') + P('SYN_OSC_D_F2MIX') > 0, lfoDepth1: P('LFO1_DEPTH'), chain: window.__flowChain().join(','), nodes: window.__tpNodes().length };
    }, lvl));
  }
  console.log(JSON.stringify({ out, errs }, null, 0).replace(/\},\{/g, '},\n{'));
  await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
