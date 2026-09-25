// tp105 — the round-2 review pictures (not a gate). node Tests/_org_shots_tp105.js [page.html] [out-dir]
// back-panel.png (Organics' own back row beside Modal's), menu.png (the right-click menu on the Organics picture),
// patcher-node.png / patcher-eye.png (an Organics twin on the Patcher canvas, controls and the eye), synth-page.png.
const fs = require('fs'), path = require('path');
const H = require('./_org_harness.js');
const PAGE = process.argv[2] || H.PAGE_DEFAULT;
const OUT = process.argv[3] || path.resolve(__dirname, '../Design/organics/impl-shots/tp105');
fs.mkdirSync(OUT, { recursive: true });
const box = (p, sels, pad) => p.evaluate((sels, pad) => { const rs = sels.map(s => document.querySelector(s)).filter(Boolean).map(e => e.getBoundingClientRect());
  const x = Math.min(...rs.map(r => r.left)) - pad, y = Math.min(...rs.map(r => r.top)) - pad; return { x, y, width: Math.max(...rs.map(r => r.right)) - x + pad, height: Math.max(...rs.map(r => r.bottom)) - y + pad }; }, sels, pad || 10);
(async () => {
  let { b, p } = await H.launch({ page: PAGE, dpr: 2 });
  for (const o of 'ac') await H.enable(p, o);
  await H.setEngine(p, 'a', 7); await H.setEngine(p, 'c', 6); await H.sleep(800);
  await p.evaluate(() => window.__orgSetInstrument('a', 'vcsl.vibraphone')); await H.sleep(400);
  await p.screenshot({ path: path.join(OUT, 'synth-page.png') });
  // the right-click menu
  await p.evaluate(() => { const t = document.querySelector('#osc-a-device .sample-view .samp-disp'), r = t.getBoundingClientRect();
    t.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2, clientX: r.left + r.width * 0.6, clientY: r.top + r.height * 0.5 })); });
  await H.sleep(300); await p.screenshot({ path: path.join(OUT, 'menu.png'), clip: await box(p, ['#osc-a-device', '.samp-menu.open'], 12) });
  await p.evaluate(() => document.querySelectorAll('.samp-menu.open').forEach(m => m.classList.remove('open')));
  // the back panel, Organics (A) beside Modal (C)
  await p.evaluate(() => window.__orgSetInstrument('a', 'vsco.violin.section')); await H.sleep(300);
  for (const o of 'ac') await p.evaluate(o => document.querySelector('#osc-' + o + '-device .swap-btn').click(), o);
  await H.sleep(500); await p.screenshot({ path: path.join(OUT, 'back-panel.png'), clip: await box(p, ['#osc-a-device', '#osc-c-device'], 10) });
  await p.evaluate(() => { const pl = document.querySelector('#osc-a-device .org-pills [data-org$="_VCURVE"]'); pl.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, cancelable: true, button: 0 })); });
  await H.sleep(300); await p.screenshot({ path: path.join(OUT, 'back-panel-curve-menu.png'), clip: await box(p, ['#osc-a-device', '.org-artic-menu'], 10) });
  await b.close();
  // the Patcher: E on the canvas, an Organics twin, its controls and the eye
  ({ b, p } = await H.launch({ page: PAGE, dpr: 2, width: 1300, height: 860, noShow: true, query: '?page=5', settle: 3000 }));
  await p.evaluate(() => { window.Juce.getSliderState('SYN_OSC_E_ENABLE').setNormalisedValue(1); if (window.__tpOpen) window.__tpOpen(); });
  await H.sleep(1800); await H.setEngine(p, 'e', 7); await H.sleep(700);
  await p.evaluate(() => window.__orgSetInstrument('e', 'vcsl.vibraphone')); await H.sleep(500);
  await p.evaluate(() => { const n = document.getElementById('osc-e-device'); if (n) n.scrollIntoView({ block: 'center', inline: 'center' }); }); await H.sleep(300);
  const nodeBox = () => p.evaluate(() => { const r = document.getElementById('osc-e-device').closest('.tp-node').getBoundingClientRect(); return { x: r.left - 16, y: r.top - 16, width: r.width + 32, height: r.height + 32 }; });
  await p.screenshot({ path: path.join(OUT, 'patcher-node.png'), clip: await nodeBox() });
  await p.evaluate(() => window.__tpViz(true)); await H.sleep(900);
  await p.screenshot({ path: path.join(OUT, 'patcher-eye.png'), clip: await nodeBox() });
  await b.close();
  console.log('wrote', OUT);
})().catch(e => { console.log('crash', e); process.exit(1); });
