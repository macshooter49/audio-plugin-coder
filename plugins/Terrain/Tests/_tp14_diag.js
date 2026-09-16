const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 900, height: 700 }); const errs = []; p.on('pageerror', e => errs.push(e.message));
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); document.getElementById('syn-btn').click(); }); await sleep(1200);
  const box = await p.evaluate(() => {
    const m = s => { const e = document.querySelector(s); if (!e) return s + ': MISSING'; const r = e.getBoundingClientRect(), c = getComputedStyle(e);
      return `${s}: ${Math.round(r.width)}x${Math.round(r.height)} @${Math.round(r.left)},${Math.round(r.top)} display=${c.display} vis=${c.visibility} flex=${c.flex}`; };
    return ['#syn-panel', '.ribbon-row.rr-new', '.rr-left', '#fxr-dice', '#fxr-dice .fxr-dice-go', '.fxr-clip', '#fxr-rack'].map(m);
  });
  await p.evaluate(() => { window.__setLog = []; const o = window.__setSynParam; window.__setSynParam = function (id, v) { window.__setLog.push(id + '=' + (+v).toFixed(2)); return o.apply(this, arguments); }; window.__fxrDice('wild'); });
  await sleep(900);
  const cmp = await p.evaluate(() => [...new Set(window.__setLog.filter(x => /^SYN_CMP/.test(x)))]);
  await p.screenshot({ path: process.argv[3] + '/tp14-row.png', clip: { x: 0, y: 330, width: 660, height: 300 } });
  console.log(box.join('\n')); console.log('CMP writes:', cmp.join(' ')); console.log('errs', errs.slice(0, 3));
  await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
