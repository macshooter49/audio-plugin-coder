const { webkit } = require('playwright-webkit');
(async () => {
  const b = await webkit.launch(); const p = await b.newPage({ viewport: { width: 820, height: 656 }, deviceScaleFactor: 2 });
  const errs = []; p.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await p.goto('file://' + process.argv[2] + '?page=1'); await p.waitForTimeout(2500);
  await p.evaluate(() => { try { localStorage.removeItem('tpLayout'); } catch (e) {} document.documentElement.setAttribute('data-theme', 'dark'); document.getElementById('osc-a-device').classList.remove('osc-off'); document.getElementById('syn-btn').click(); });
  await p.waitForTimeout(2500);
  const out = process.argv[3];
  for (const z of [1, 2]) {
    await p.evaluate(z => { const n = window.__tpNodes().find(n => n.key === 'osc-a'); window.__tpSetView(20 - n.x * z, 60 - n.y * z, z); }, z); await p.waitForTimeout(900);
    await p.screenshot({ path: out + '-wk-z' + z + '.png', clip: { x: 0, y: 40, width: 760, height: 360 } });
  }
  console.log(JSON.stringify({ ok: true, errs })); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
