const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 1 });
  const errs = []; p.on('pageerror', e => errs.push('PAGEERROR ' + e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.removeItem('tpLayout'); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); document.getElementById('osc-a-device').classList.remove('osc-off'); try { window.__fxrAdd('reverb'); } catch (e) {} document.getElementById('syn-btn').click(); });
  await sleep(1800); await p.evaluate(() => window.__tpSetView(20, 30, 0.8)); await sleep(300);
  const R = {};
  async function dragBy(sel, key) {
    const pt = await p.evaluate(s => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; }, sel); if (!pt) return 'no ' + sel;
    const before = await p.evaluate(k => { const n = window.__tpNodes().find(n => n.key === k); return [n.x, n.y]; }, key), cut0 = await p.evaluate(() => window.__tpParam('SYN_FILTER1_CUT'));
    await p.mouse.move(pt.x, pt.y); await p.mouse.down(); await p.mouse.move(pt.x + 30, pt.y + 20, { steps: 6 }); await p.mouse.up(); await sleep(150);
    const after = await p.evaluate(k => { const n = window.__tpNodes().find(n => n.key === k); return [n.x, n.y]; }, key), cut1 = await p.evaluate(() => window.__tpParam('SYN_FILTER1_CUT'));
    return { moved: [after[0] - before[0], after[1] - before[1]], cutChanged: Math.abs(cut1 - cut0) > 1e-6 };
  }
  R.filterDisplay = await dragBy('#tp-page .tp-node[data-key="filter"] .filt-bg', 'filter');
  R.envGrid = await dragBy('#tp-page .tp-node[data-key="env"] .env-canvas', 'env');
  R.lfoScope = await dragBy('#tp-page .tp-node[data-key="lfo"] .mv-scope', 'lfo');
  await p.evaluate(() => window.__tpFit()); await sleep(300);
  R.outScope = await dragBy('#tp-page .tp-node[data-key="out"] .tp-scope', 'out');
  R.fxCore = await dragBy('#tp-page .tp-node[data-kind="fx"] .fxr-core', (await p.evaluate(() => window.__tpNodes().find(n => /^fx-/.test(n.key)).key)));
  R.knobStillWorks = await p.evaluate(() => { const k = document.querySelector('#tp-page .tp-node[data-key="osc-a"] .knob'); const r = k.getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; }).then(async pt => { const b0 = await p.evaluate(() => window.__tpNodes().find(n => n.key === 'osc-a').x); await p.mouse.move(pt.x, pt.y); await p.mouse.down(); await p.mouse.move(pt.x, pt.y - 20, { steps: 4 }); await p.mouse.up(); await sleep(100); const b1 = await p.evaluate(() => window.__tpNodes().find(n => n.key === 'osc-a').x); return { nodeMoved: b1 !== b0 }; });
  await p.evaluate(() => window.__tpSetView(2000, 2000, 0.8)); await sleep(200); await p.mouse.click(400, 400); await sleep(250);
  R.menuOpen = await p.evaluate(() => ({ on: !!document.querySelector('#tp-page .tp-add.on'), bodyCls: document.body.classList.contains('tp-menu-open'), font: (e => e ? getComputedStyle(e).fontFamily.slice(0, 40) + ' ' + getComputedStyle(e).fontSize + ' ' + getComputedStyle(e).letterSpacing : null)(document.querySelector('#tp-page .tp-add .items .row')), qFont: getComputedStyle(document.body).fontFamily.slice(0, 40) }));
  R.fxDriverSeesCards = await p.evaluate(() => ({ cards: window.__fxrCardsAll(document.getElementById('fxr-rack')).length, off: window.__tiOff('fxr-rack') }));
  R.tpOnClock = await p.evaluate(() => typeof window.__tiFrameReg === 'function');
  R.errs = errs; console.log(JSON.stringify(R)); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
