// does a card still fit under the new row? card bottom vs the clip's bottom, before and after
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 900, height: 700 });
  await p.evaluateOnNewDocument(() => { try { localStorage.clear(); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); if (window.setActivePanel) window.setActivePanel('syn'); try { window.__fxrAdd('reverb'); window.__fxrAdd('delay'); } catch (e) {} });
  await sleep(1200);
  const r = await p.evaluate(() => { const c = document.querySelector('.fxr-dev'), cl = document.querySelector('.fxr-clip');
    if (!c || !cl) return 'no card'; const a = c.getBoundingClientRect(), b2 = cl.getBoundingClientRect();
    const knob = c.querySelector('.fxr-knobs'), k = knob ? knob.getBoundingClientRect() : null;
    return { card: Math.round(a.height), clip: Math.round(b2.height), overflow: Math.round(a.bottom - b2.bottom), knobsBottomInside: k ? (k.bottom <= b2.bottom + 0.5) : 'n/a' }; });
  console.log(JSON.stringify(r)); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
