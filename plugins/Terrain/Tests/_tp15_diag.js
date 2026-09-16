const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 900, height: 700 });
  const errs = []; p.on('pageerror', e => errs.push('PAGEERROR: ' + e.message));
  p.on('console', m => { if (m.type() === 'error') errs.push('CONSOLE: ' + m.text().slice(0, 200)); });
  await p.evaluateOnNewDocument(() => { try { localStorage.clear(); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1800);
  const g = await p.evaluate(() => ['__fxrDice', '__modDice', '__fxrDiceLvl', '__tiAddSrc', '__fxrDevs', '__fxrAdd', '__synShowMenu', '__knobDest']
    .map(k => k + '=' + (typeof window[k])).join(' '));
  console.log('globals:', g); console.log(errs.slice(0, 8).join('\n') || 'no errors captured');
  await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
