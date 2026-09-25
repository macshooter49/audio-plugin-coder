// ══════════════════════════════════════════════════════════════════════════════════════════════
//  _org_menu_gate.js — tp105 THE RIGHT-CLICK MENU ON EVERY OSCILLATOR, EVERY ENGINE (Max: "I can't right-click it and
//  mute it, turn it down, solo it — like the waveform"). node Tests/_org_menu_gate.js [page.html]
//
//  tp104 swallowed the contextmenu on the Organics display (it sits in the sample family's box, whose own menu is the
//  sample editor's), so nothing opened. For every oscillator A–H (E–H adopted onto the Patcher canvas, tp40) and every
//  engine 0–7, a right-click on the visible picture must open a menu that carries SOLO and MUTE and the Volume row:
//   1  A–D on the synth page, all 8 engines
//   2  E–H on the Patcher canvas, all 8 engines
//   3  on Organics the menu is THE WAVEFORM'S menu (the osc quick menu), not the sample editor's (no "Trim to loop")
//   4  its SOLO really writes SYN_OSC_x_SOLO (the osc's own parameter)
//   5  the Patcher adopts, returns and re-adopts an Organics osc: instrument, picture and the ‹ › handlers survive
// ══════════════════════════════════════════════════════════════════════════════════════════════
const H = require('./_org_harness.js');
const PAGE = process.argv[2] || H.PAGE_DEFAULT;
let pass = 0, fail = 0;
const ok = (c, name, detail) => { c ? ++pass : ++fail; console.log(`  ${c ? 'PASS' : 'FAIL'}  ${name}${detail ? '\n        ' + detail : ''}`); };

const probe = (p, o) => p.evaluate((o) => {
  document.querySelectorAll('.samp-menu.open').forEach(m => m.classList.remove('open'));
  const d = document.getElementById('osc-' + o + '-device'); if (!d) return { o, err: 'no device' };
  const vis = e => !!(e && e.getClientRects().length && getComputedStyle(e).display !== 'none' && getComputedStyle(e).visibility !== 'hidden');
  const a = d.querySelector('.sample-view .samp-disp'), b = d.querySelector('.osc-display'), t = vis(a) ? a : (vis(b) ? b : null);
  if (!t) return { o, err: 'no visible picture' };
  const r = t.getBoundingClientRect(), x = r.left + r.width * 0.5, y = r.top + r.height * 0.5, hit = document.elementFromPoint(x, y) || t;
  hit.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2, clientX: x, clientY: y }));
  const m = [...document.querySelectorAll('.samp-menu.open')].pop();
  const txt = m ? m.textContent : '';
  return { o, eng: (d.querySelector('.engine-display') || {}).textContent, pic: t.className.split(' ')[0], open: !!m, solo: /SOLO/.test(txt), mute: /MUTE/.test(txt), vol: /Volume/.test(txt), trim: /Trim to loop/.test(txt), quick: !!(m && m.classList.contains('oscq-pop')) };
}, o);

(async () => {
  let { b, p, errs } = await H.launch({ page: PAGE });
  const rows = [];
  for (const o of 'abcd') await H.enable(p, o);
  const showSlot = (o) => p.evaluate(o => { const me = document.getElementById('osc-' + o + '-device'), slot = me.closest('.osc-slot'); if (!slot || !me.classList.contains('osc-hidden')) return o;
    const cur = slot.querySelector('.device.osc:not(.osc-hidden)'), l = cur && cur.querySelector('.osc-letter'); if (l) l.click(); return !me.classList.contains('osc-hidden'); }, o);   /* the slot's letter swaps A⇄B, C⇄D */
  for (let e = 0; e < 8; e++) { for (const o of 'abcd') await H.setEngine(p, o, e); await H.sleep(500);
    for (const o of 'abcd') { await showSlot(o); await H.sleep(250); rows.push(Object.assign({ e }, await probe(p, o))); } await showSlot('a'); await showSlot('c'); await H.sleep(200); }
  const bad1 = rows.filter(r => !(r.open && r.solo && r.mute && r.vol));
  ok(!bad1.length, `[1] A–D × 8 engines: the right-click opens SOLO · MUTE · Volume (${rows.length - bad1.length}/${rows.length})`, bad1.map(r => `${r.o}/${r.e} ${JSON.stringify(r)}`).join('\n        '));
  const org = rows.filter(r => r.e === 7);
  // [4] the solo button writes the parameter
  const solo = await p.evaluate(() => { const m = document.querySelector('.samp-menu.open'); if (m) m.classList.remove('open');
    const d = document.getElementById('osc-a-device'), t = d.querySelector('.sample-view .samp-disp'), r = t.getBoundingClientRect();
    t.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2, clientX: r.left + 20, clientY: r.top + 20 }));
    const btn = [...document.querySelectorAll('.samp-menu.open .oq-sm-btn')].find(x => x.textContent === 'SOLO'); window.__natLog.length = 0;
    const before = window.Juce.getSliderState('SYN_OSC_A_SOLO').getNormalisedValue();
    if (btn) { btn.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true })); btn.dispatchEvent(new MouseEvent('mousedown', { bubbles: true })); btn.click(); }
    const after = window.Juce.getSliderState('SYN_OSC_A_SOLO').getNormalisedValue(); return { btn: !!btn, before, after }; });
  await b.close();
  // [2] E–H on the Patcher
  ({ b, p, errs } = await H.launch({ page: PAGE, noShow: true, query: '?page=5', settle: 3000, width: 1300, height: 860 }));
  await p.evaluate(() => { 'efgh'.split('').forEach(o => { try { window.Juce.getSliderState('SYN_OSC_' + o.toUpperCase() + '_ENABLE').setNormalisedValue(1); } catch (e) {} }); if (window.__tpOpen) window.__tpOpen(); });
  await H.sleep(1800);
  const rows2 = [];
  for (let e = 0; e < 8; e++) { for (const o of 'efgh') await H.setEngine(p, o, e); await H.sleep(600);
    for (const o of 'efgh') { await p.evaluate(o => { const n = document.getElementById('osc-' + o + '-device'); if (n) n.scrollIntoView({ block: 'center', inline: 'center' }); }, o); await H.sleep(120);
      rows2.push(Object.assign({ e, canvas: await p.evaluate(o => !!document.getElementById('osc-' + o + '-device').closest('#tp-page'), o) }, await probe(p, o))); } }
  const bad2 = rows2.filter(r => !(r.canvas && r.open && r.solo && r.mute && r.vol));
  ok(!bad2.length, `[2] E–H on the Patcher × 8 engines: the right-click opens SOLO · MUTE · Volume (${rows2.length - bad2.length}/${rows2.length})`, bad2.map(r => `${r.o}/${r.e} ${JSON.stringify(r)}`).join('\n        '));
  // [5] adopt → return → adopt keeps the instrument, the picture and the handlers (tp40: adopted DOM loses container-bound handlers)
  const rt = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms));
    await window.__orgSetInstrument('e', 'vsco.violin.section'); await sleep(300);
    const nm = () => (document.getElementById('osc-e-orginst-display') || {}).textContent, parts = () => document.querySelectorAll('#osc-e-device .org-viz .pt').length;
    const a = { name: nm(), parts: parts(), canvas: !!document.getElementById('osc-e-device').closest('#tp-page') };
    window.__tpClose(); await sleep(900); const b = { name: nm(), parts: parts(), canvas: !!document.getElementById('osc-e-device').closest('#tp-page') };
    window.__tpOpen(); await sleep(1500); const c = { name: nm(), parts: parts(), canvas: !!document.getElementById('osc-e-device').closest('#tp-page') };
    const nx = document.querySelector('#osc-e-device .org-nav[data-dir="1"]'); nx.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, button: 0 })); await sleep(500);
    const d = { name: nm(), parts: parts() }; return { a, b, c, d }; });
  ok(rt.a.canvas && !rt.b.canvas && rt.c.canvas && rt.a.name === 'Violin Section' && rt.b.name === 'Violin Section' && rt.c.name === 'Violin Section' && rt.c.parts > 0 && rt.d.name === 'Cello' && rt.d.parts > 0,
     '[5] the Patcher adopts, returns and re-adopts an Organics osc: the instrument and picture stay, and ‹ › still step on the canvas', JSON.stringify(rt));
  const org2 = org.concat(rows2.filter(r => r.e === 7));
  ok(org2.every(r => r.quick && !r.trim), '[3] on Organics it is the waveform\'s own menu (the osc quick menu), not the sample editor\'s', org2.map(r => `${r.o}: ${r.quick ? 'quick' : 'OTHER'}${r.trim ? ' +trim' : ''}`).join(' '));
  ok(solo.btn && solo.after !== solo.before, '[4] its SOLO writes SYN_OSC_A_SOLO', JSON.stringify(solo));
  const real = errs.filter(e => !/formatOutput/.test(e));
  ok(real.length === 0, '[—] no page errors', real.join(' | '));
  await b.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL (crash)', e); process.exit(1); });
