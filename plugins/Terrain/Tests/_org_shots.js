// tp104 — the Organics screenshots for review (not a gate). node Tests/_org_shots.js [page.html] [out-dir]
// Writes into Design/organics/impl-shots/: the synth page with Organics on osc A (Grand Piano, Violin Section, Vibraphone),
// page 2, the unison ("Ensemble") page, the browser open, playing reactions, the Patcher with an Organics osc, the gallery.
const fs = require('fs'), path = require('path');
const H = require('./_org_harness.js');
const PAGE = process.argv[2] || H.PAGE_DEFAULT;
const OUT = process.argv[3] || path.resolve(__dirname, '../Design/organics/impl-shots');
fs.mkdirSync(OUT, { recursive: true });
const clipOf = (p, sel, pad) => p.evaluate((sel, pad) => { const r = document.querySelector(sel).getBoundingClientRect(); return { x: r.left - pad, y: r.top - pad, width: r.width + 2 * pad, height: r.height + 2 * pad }; }, sel, pad || 12);
(async () => {
  let { b, p } = await H.launch({ page: PAGE, dpr: 3, width: 1200, height: 820 });
  for (const o of 'ac') await H.enable(p, o);
  await H.setEngine(p, 'a', 7); await H.setEngine(p, 'c', 6); await H.sleep(900);
  const dev = '#osc-a-device';
  const shot = async (name, sel, pad) => { await H.sleep(350); await p.screenshot({ path: path.join(OUT, name), clip: await clipOf(p, sel || dev, pad) }); console.log(name); };
  await p.evaluate(() => { [['DYNAMICS', .62], ['TONE', .56], ['BODY', .5], ['ATTACK', .42], ['HUMAN', .25], ['RELEASE', .5], ['NOISE', .5], ['SUSTAIN', 0], ['VELOCITY', .75], ['IMAGE', .667]]
    .forEach(([k, v]) => { const e = document.querySelector('.knob[data-syn="SYN_OSC_A_ORG_' + k + '"]'); if (e && e.__applyFill) e.__applyFill(v); }); });
  await shot('synth-grand.png');
  await p.screenshot({ path: path.join(OUT, 'synth-page.png') }); console.log('synth-page.png');
  await p.evaluate(() => window.__orgSetInstrument('a', 'vsco.violin.section')); await shot('synth-violin.png');
  await p.evaluate(() => window.__orgSetInstrument('a', 'vcsl.vibraphone')); await shot('synth-vibraphone.png');
  await p.evaluate(() => window.__orgSetInstrument('a', 'vsco.cello'));
  await p.evaluate(() => document.querySelector('#osc-a-device .organic-knob-wrap > .organic-arrow:not(.set-prev)').click()); await shot('page2-cello.png');
  await p.evaluate(() => document.querySelector('#osc-a-device .organic-knob-wrap > .organic-arrow:not(.set-prev)').click()); await shot('unison-ensemble-cello.png');
  await p.evaluate(() => document.querySelector('#osc-a-device .uni-knob-wrap > .uni-arrow:not(.set-prev)').click());
  // reactions: a chord on the grand, a bowed note on the violin, the flute breathing
  for (const [id, notes, name] of [['salamander.grand', [60, 64, 67, 72], 'react-grand.png'], ['vsco.violin.section', [69], 'react-violin.png'], ['vsco.flute', [72], 'react-flute.png'], ['vcsl.pipe.organ', [48, 55, 60], 'react-organ.png']]) {
    await p.evaluate(id => window.__orgSetInstrument('a', id), id); await H.sleep(300);
    await p.evaluate(ns => window.__orgMock.emitViz({ osc: 0, notes: ns.map(n => ({ n, lvl: .9 })), pedal: 0 }), notes); await H.sleep(260);
    await p.screenshot({ path: path.join(OUT, name), clip: await clipOf(p, dev + ' .samp-disp', 6) }); console.log(name);
    await p.evaluate(() => window.__orgMock.emitViz({ osc: 0, notes: [], pedal: 0 })); await H.sleep(600); }
  await p.evaluate(() => window.__orgSetInstrument('a', 'salamander.grand')); await H.sleep(300);
  await p.evaluate(() => { const w = document.querySelector('#osc-a-device .org-inst-wrap'), r = w.getBoundingClientRect();
    w.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, button: 0, clientX: r.left + 4, clientY: r.bottom })); });
  await H.sleep(500);
  await p.evaluate(() => { const panel = document.querySelector('.tpb-panel'); const c = [...panel.querySelectorAll('.tpb-pane')[0].children].find(x => /^Strings/.test(x.textContent.replace('•', '').trim())); if (c) c.click(); });
  await H.sleep(300);
  await p.screenshot({ path: path.join(OUT, 'browser.png'), clip: await p.evaluate(() => { const a = document.querySelector('#osc-a-device').getBoundingClientRect(), q = document.querySelector('.tpb-panel').getBoundingClientRect();
    const x = Math.min(a.left, q.left) - 10, y = Math.min(a.top, q.top) - 10; return { x, y, width: Math.max(a.right, q.right) - x + 10, height: Math.max(a.bottom, q.bottom) - y + 10 }; }) }); console.log('browser.png');
  await b.close();
  // the Patcher: E on the canvas, running Organics
  ({ b, p } = await H.launch({ page: PAGE, dpr: 2, width: 1300, height: 860, noShow: true, query: '?page=5', settle: 3000 }));
  await p.evaluate(() => { window.Juce.getSliderState('SYN_OSC_E_ENABLE').setNormalisedValue(1); if (window.__tpOpen) window.__tpOpen(); });
  await H.sleep(1800); await H.setEngine(p, 'e', 7); await H.sleep(700);
  await p.evaluate(() => window.__orgSetInstrument('e', 'vsco.trumpet')); await H.sleep(500);
  await p.evaluate(() => { const n = document.getElementById('osc-e-device'); if (n) n.scrollIntoView({ block: 'center', inline: 'center' }); }); await H.sleep(300);
  await p.screenshot({ path: path.join(OUT, 'patcher-full.png') }); console.log('patcher-full.png');
  await p.screenshot({ path: path.join(OUT, 'patcher-osc-e.png'), clip: await clipOf(p, '#osc-e-device', 24) }); console.log('patcher-osc-e.png');
  await b.close();
})().catch(e => { console.log('crash', e); process.exit(1); });
