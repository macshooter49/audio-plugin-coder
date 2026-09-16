// measure only — no roll, so it runs against the pre-change page too
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 900, height: 700 });
  await p.evaluateOnNewDocument(() => { try { localStorage.clear(); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); if (window.setActivePanel) window.setActivePanel('syn'); else document.getElementById('syn-btn').click(); });
  await sleep(1400);
  const out = await p.evaluate(() => {
    const m = s => { const e = document.querySelector(s); if (!e) return s + ': MISSING'; const r = e.getBoundingClientRect();
      return `${s}: ${Math.round(r.width)}x${Math.round(r.height)} @${Math.round(r.left)},${Math.round(r.top)}`; };
    return { rows: ['#syn-panel', '.ribbon-row.rr-new', '.rr-left', '#fxr-dice', '.fxr-clip', '#fxr-rack'].map(m), panel: document.getElementById('syn-panel').className };
  });
  if (process.argv[4]) await p.screenshot({ path: process.argv[4], clip: { x: 0, y: 380, width: 660, height: 300 } });
  console.log(out.rows.join('\n')); console.log('panel class:', out.panel);
  await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
