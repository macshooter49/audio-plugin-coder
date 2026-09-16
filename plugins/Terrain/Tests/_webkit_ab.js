const { webkit } = require('playwright-webkit');
(async () => {
  const html = process.argv[2], out = process.argv[3];
  const b = await webkit.launch(); const R = {};
  for (const mode of ['transform', 'zoom', 'noanim']) {
    const p = await b.newPage({ viewport: { width: 820, height: 656 }, deviceScaleFactor: 1 });
    await p.goto('file://' + html + '?page=1'); await p.waitForTimeout(2200);
    await p.evaluate(() => { try { localStorage.removeItem('tpLayout'); } catch (e) {} document.documentElement.setAttribute('data-theme', 'dark'); document.getElementById('osc-a-device').classList.remove('osc-off'); document.getElementById('syn-btn').click(); });
    await p.waitForTimeout(2200);
    await p.evaluate(mode => { const z = 2, n = window.__tpNodes().find(n => n.key === 'osc-a'); window.__tpSetView(20 - n.x * z, 60 - n.y * z, z);
      if (mode === 'noanim') document.querySelectorAll('#tp-page .tp-node').forEach(w => { w.style.animation = 'none'; w.classList.remove('tp-in'); });
      if (mode === 'zoom') { const w = document.querySelector('#tp-page .tp-world'); const m = /translate\(([-\d.]+)px,\s*([-\d.]+)px\)\s*scale\(([\d.]+)\)/.exec(w.style.transform); w.style.transform = 'translate(' + m[1] + 'px,' + m[2] + 'px)'; w.querySelector('.tp-zoom').style.zoom = m[3]; } }, mode);
    await p.waitForTimeout(900);
    await p.screenshot({ path: out + '-ab-' + mode + '.png', clip: { x: 20, y: 60, width: 640, height: 140 } });
    await p.close();
  }
  console.log('ok'); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
