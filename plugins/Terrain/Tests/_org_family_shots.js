// tp105 — every Organics family, photographed in osc A (not a gate). node Tests/_org_family_shots.js [page.html] [out-dir] [fam,fam,…]
// Writes fam-<id>.png (the osc at rest), fam-<id>-play.png (a chord on it, so a twin shows both halves reacting) and
// families.png (all of them in one grid) into Design/organics/impl-shots/ (or out-dir).
const fs = require('fs'), path = require('path');
const H = require('./_org_harness.js');
const PAGE = process.argv[2] || H.PAGE_DEFAULT;
const OUT = process.argv[3] || path.resolve(__dirname, '../Design/organics/impl-shots');
const ONLY = process.argv[4] ? process.argv[4].split(',') : null;
fs.mkdirSync(OUT, { recursive: true });
(async () => {
  const { b, p, errs } = await H.launch({ page: PAGE, dpr: 3, width: 1200, height: 820 });
  await H.enable(p, 'a'); await H.setEngine(p, 'a', 7); await H.sleep(900);
  await p.evaluate(() => window.__orgSetInstrument('a', 'salamander.grand')); await H.sleep(400);
  const fams = ONLY || await p.evaluate(() => Object.keys(window.__orgArt.families));
  const clip = await p.evaluate(() => { const r = document.querySelector('#osc-a-device').getBoundingClientRect(), q = document.querySelector('#osc-a-device .sample-view .samp-disp').getBoundingClientRect(); return { x: r.left - 6, y: r.top - 4, width: r.width + 12, height: q.bottom - r.top + 8 }; });
  const tiles = [];
  for (const f of fams) {
    const n = await p.evaluate(f => window.__orgForceFam('a', f), f); await H.sleep(250);
    const file = path.join(OUT, 'fam-' + f + '.png'); await p.screenshot({ path: file, clip }); tiles.push(file);
    const vars = await p.evaluate(() => window.__orgVars('a'));
    const notes = vars.split != null ? [vars.split - 7, vars.split - 3, vars.split + 2, vars.split + 5] : [60, 64, 67, 72];
    await p.evaluate(ns => window.__orgMock.emitViz({ osc: 0, notes: ns.map(n => ({ n, lvl: .9 })), pedal: 0 }), notes); await H.sleep(120);
    await p.screenshot({ path: path.join(OUT, 'fam-' + f + '-play.png'), clip });
    await p.evaluate(() => window.__orgMock.emitViz({ osc: 0, notes: [], pedal: 0 })); await H.sleep(500);
    console.log(f, n === 2 ? 'TWIN' : 'single', JSON.stringify(vars.vars.map(v => [v.kind, v.lo, v.hi, v.box.map(x => +x.toFixed(1))])), 'split', vars.split);
  }
  // the grid
  const cols = 3, imgs = tiles.map(t => fs.readFileSync(t).toString('base64'));
  const grid = await p.evaluate(async (imgs, cols) => { const I = await Promise.all(imgs.map(async b => { const i = new Image(); i.src = 'data:image/png;base64,' + b; await i.decode(); return i; }));
    const w = I[0].width, h = I[0].height, c = document.createElement('canvas'); c.width = w * cols; c.height = h * Math.ceil(I.length / cols);
    const x = c.getContext('2d'); x.fillStyle = '#1a1a2e'; x.fillRect(0, 0, c.width, c.height); I.forEach((im, k) => x.drawImage(im, (k % cols) * w, Math.floor(k / cols) * h));
    return c.toDataURL('image/png').split(',')[1]; }, imgs, cols);
  fs.writeFileSync(path.join(OUT, ONLY ? 'families-part.png' : 'families.png'), Buffer.from(grid, 'base64'));
  if (errs.length) console.log('page errors:', errs.join(' | '));
  await b.close();
})().catch(e => { console.log('crash', e); process.exit(1); });
