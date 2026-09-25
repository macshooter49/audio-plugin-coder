// tp107 — the review pictures (not a gate). node Tests/_org_shots_tp107.js [page.html] [out-dir]
// header-vs-modal.png / header-vs-wavetable.png (Organics' header, articulation pill up, above the other engine's),
// page2.png (Attack · Release · Sustain · Velocity · Image beside Modal's page 2), back-panel.png (Rate · Delay · Curve · Tuning ·
// Noise beside Modal's back), browser.png (the Organics browser: house categories, name + ▶ rows).
const fs = require('fs'), path = require('path');
const H = require('./_org_harness.js');
const PAGE = process.argv[2] || H.PAGE_DEFAULT;
const OUT = process.argv[3] || path.resolve(__dirname, '../Design/organics/impl-shots/tp107');
fs.mkdirSync(OUT, { recursive: true });
const box = (p, sels, pad, hh) => p.evaluate((sels, pad, hh) => { const rs = sels.map(s => document.querySelector(s)).filter(Boolean).map(e => e.getBoundingClientRect());
  const x = Math.min(...rs.map(r => r.left)) - pad, y = Math.min(...rs.map(r => r.top)) - pad;
  return { x, y, width: Math.max(...rs.map(r => r.right)) - x + pad, height: hh ? hh : Math.max(...rs.map(r => r.bottom)) - y + pad }; }, sels, pad == null ? 10 : pad, hh || 0);
// two header strips stacked (osc A's top 36 px over osc C's)
async function heads(p, file) {
  const shots = [];
  for (const o of 'ac') shots.push(await p.screenshot({ encoding: 'base64', clip: await p.evaluate(o => { const r = document.getElementById('osc-' + o + '-device').getBoundingClientRect(); return { x: r.left - 6, y: r.top - 3, width: r.width + 12, height: 38 }; }, o) }));
  const png = await p.evaluate(async (imgs) => { const I = await Promise.all(imgs.map(async b => { const i = new Image(); i.src = 'data:image/png;base64,' + b; await i.decode(); return i; }));
    const c = document.createElement('canvas'); c.width = Math.max(...I.map(i => i.width)); c.height = I.reduce((a, i) => a + i.height, 0); const x = c.getContext('2d'); let y = 0; I.forEach(im => { x.drawImage(im, 0, y); y += im.height; }); return c.toDataURL('image/png').split(',')[1]; }, shots);
  fs.writeFileSync(path.join(OUT, file), Buffer.from(png, 'base64'));
}
(async () => {
  const { b, p } = await H.launch({ page: PAGE, dpr: 2 });
  for (const o of 'ac') await H.enable(p, o);
  await H.setEngine(p, 'a', 7); await H.setEngine(p, 'c', 6); await H.sleep(800);
  await p.evaluate(() => window.__orgSetInstrument('a', 'vsco.violin.section')); await H.sleep(500);
  await p.evaluate(() => window.__orgSetArtic('a', 3)); await H.sleep(200);   // Tremolo → "Trem"
  await heads(p, 'header-vs-modal.png');
  await H.setEngine(p, 'c', 0); await H.sleep(600); await heads(p, 'header-vs-wavetable.png');
  await H.setEngine(p, 'c', 6); await H.sleep(600);
  // page 2, both engines
  await p.evaluate(() => { document.querySelector('#osc-a-device .organic-knob-wrap').classList.add('pg2'); document.querySelector('#osc-c-device .modal-knob-wrap').classList.add('pg2'); });
  await p.evaluate(() => { const k = document.querySelector('.knob[data-syn="SYN_OSC_A_ORG_ATTACK"]'); if (k && k.__applyFill) k.__applyFill(0.62); }); await H.sleep(300);
  await p.screenshot({ path: path.join(OUT, 'page2.png'), clip: await box(p, ['#osc-a-device', '#osc-c-device'], 8) });
  await p.evaluate(() => { document.querySelector('#osc-a-device .organic-knob-wrap').classList.remove('pg2'); document.querySelector('#osc-c-device .modal-knob-wrap').classList.remove('pg2'); });
  // the back panel, Organics (A) beside Modal (C)
  for (const o of 'ac') await p.evaluate(o => document.querySelector('#osc-' + o + '-device .swap-btn').click(), o);
  await H.sleep(500); await p.screenshot({ path: path.join(OUT, 'back-panel.png'), clip: await box(p, ['#osc-a-device', '#osc-c-device'], 8) });
  for (const o of 'ac') await p.evaluate(o => document.querySelector('#osc-' + o + '-device .swap-btn').click(), o);
  await H.sleep(400);
  // the browser, on Strings (the current instrument's category), headphone on
  await p.evaluate(() => window.__orgOpenBrowser('a', { clientX: 260, clientY: 140, target: document.body })); await H.sleep(600);
  await p.screenshot({ path: path.join(OUT, 'browser.png'), clip: await box(p, ['.tpb-panel'], 10) });
  await b.close();
  console.log('wrote', OUT);
})().catch(e => { console.log('crash', e); process.exit(1); });
