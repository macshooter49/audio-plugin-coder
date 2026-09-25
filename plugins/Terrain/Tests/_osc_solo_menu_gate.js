// ══════════════════════════════════════════════════════════════════════════════════════════════
//  _osc_solo_menu_gate.js — tp107 SOLO / MUTE FROM THE OSC RIGHT-CLICK MENU ON ALL EIGHT OSCILLATORS.
//  node Tests/_osc_solo_menu_gate.js [page.html]      (headless Chrome, the _org_harness stub)
//
//  Max: "solo doesn't work on EFGH oscillators on the Patcher, please make sure we can solo, and mute everything."
//  The audio half is Tests/osc_solo_mute_cert.sh (the real processor). This is the page half: what the menu writes.
//   1  A–D on the synth page: SOLO / MUTE write SYN_OSC_x_SOLO / _MUTE (the relay state) and paint lit
//   2  E–H adopted onto the Patcher canvas: SOLO / MUTE reach the processor through the pool (setSynParam native),
//      toggling on AND off, on every engine they are tested on (WT, FM, ORGANIC)
//   3  a plain click on SOLO is exclusive (Max 2026-07-09) ACROSS BOTH BANKS: soloing G un-solos A and E
//   4  Shift-click adds to the solo set (multiple solos combine in the DSP)
//   5  the menu READS BACK the processor's state for E–H (a muted E restored from a session opens lit)
// ══════════════════════════════════════════════════════════════════════════════════════════════
const H = require('./_org_harness.js');
const PAGE = process.argv[2] || H.PAGE_DEFAULT;
let pass = 0, fail = 0;
const ok = (c, name, detail) => { c ? ++pass : ++fail; console.log(`  ${c ? 'PASS' : 'FAIL'}  ${name}${detail ? '\n        ' + detail : ''}`); };

// open osc o's right-click menu on its visible picture, press SOLO or MUTE (optionally with Shift); report the writes
const press = (p, o, which, shift) => p.evaluate((o, which, shift) => {
  document.querySelectorAll('.samp-menu.open').forEach(m => m.classList.remove('open'));
  const d = document.getElementById('osc-' + o + '-device'); if (!d) return { o, err: 'no device' };
  const vis = e => !!(e && e.getClientRects().length && getComputedStyle(e).display !== 'none' && getComputedStyle(e).visibility !== 'hidden');
  const a = d.querySelector('.sample-view .samp-disp'), b = d.querySelector('.osc-display'), t = vis(a) ? a : (vis(b) ? b : null);
  if (!t) return { o, err: 'no visible picture' };
  const r = t.getBoundingClientRect(), x = r.left + r.width * 0.5, y = r.top + r.height * 0.5, hit = document.elementFromPoint(x, y) || t;
  hit.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2, clientX: x, clientY: y }));
  const m = [...document.querySelectorAll('.samp-menu.open')].pop(); if (!m) return { o, err: 'no menu' };
  const btn = [...m.querySelectorAll('.oq-sm-btn')].find(z => z.textContent === which); if (!btn) return { o, err: 'no ' + which };
  const litBefore = btn.classList.contains('on');
  window.__natLog.length = 0;
  btn.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, cancelable: true, shiftKey: !!shift }));
  const log = window.__natLog.filter(e => /_(SOLO|MUTE)$/.test(e[0])).map(e => e[0].replace('SYN_OSC_', '') + '=' + e[1]);
  const id = 'SYN_OSC_' + o.toUpperCase() + '_' + which;
  return { o, litBefore, litAfter: btn.classList.contains('on'), state: window.Juce.getSliderState(id).getNormalisedValue(), log };
}, o, which, !!shift);

(async () => {
  // ── 1 · A–D on the synth page ──
  let { b, p, errs } = await H.launch({ page: PAGE });
  for (const o of 'abcd') await H.enable(p, o);
  const showSlot = (o) => p.evaluate(o => { const me = document.getElementById('osc-' + o + '-device'), slot = me.closest('.osc-slot'); if (!slot || !me.classList.contains('osc-hidden')) return o;
    const cur = slot.querySelector('.device.osc:not(.osc-hidden)'), l = cur && cur.querySelector('.osc-letter'); if (l) l.click(); return !me.classList.contains('osc-hidden'); }, o);
  const r1 = [];
  for (const o of 'abcd') { await showSlot(o); await H.sleep(200);
    const m1 = await press(p, o, 'MUTE'), m0 = await press(p, o, 'MUTE'), s1 = await press(p, o, 'SOLO'), s0 = await press(p, o, 'SOLO');
    r1.push({ o, m1, m0, s1, s0 }); }
  const bad1 = r1.filter(r => !(r.m1.state === 1 && r.m1.litAfter && r.m0.state === 0 && !r.m0.litAfter && r.s1.state === 1 && r.s1.litAfter && r.s0.state === 0));
  ok(!bad1.length, `[1] A–D (synth page): SOLO and MUTE write their own relay param, on and off, and paint (${4 - bad1.length}/4)`, bad1.map(r => JSON.stringify(r)).join('\n        '));
  await b.close();

  // ── 2..5 · E–H on the Patcher canvas ──
  ({ b, p, errs } = await H.launch({ page: PAGE, noShow: true, query: '?page=5', settle: 3000, width: 1300, height: 860 }));
  await p.evaluate(() => { 'efgh'.split('').forEach(o => { try { window.Juce.getSliderState('SYN_OSC_' + o.toUpperCase() + '_ENABLE').setNormalisedValue(1); } catch (e) {} }); if (window.__tpOpen) window.__tpOpen(); });
  await H.sleep(1800);
  const r2 = [];
  for (const e of [0, 4, 7]) { for (const o of 'efgh') await H.setEngine(p, o, e); await H.sleep(600);
    for (const o of 'efgh') { await p.evaluate(o => { const n = document.getElementById('osc-' + o + '-device'); if (n) n.scrollIntoView({ block: 'center', inline: 'center' }); }, o); await H.sleep(120);
      const canvas = await p.evaluate(o => !!document.getElementById('osc-' + o + '-device').closest('#tp-page'), o);
      const m1 = await press(p, o, 'MUTE'), m0 = await press(p, o, 'MUTE'), s1 = await press(p, o, 'SOLO'), s0 = await press(p, o, 'SOLO');
      r2.push({ e, o, canvas, m1, m0, s1, s0 }); } }
  const L = o => o.toUpperCase();
  const bad2 = r2.filter(r => !(r.canvas
    && r.m1.log.includes(L(r.o) + '_MUTE=1') && r.m1.litAfter && r.m0.log.includes(L(r.o) + '_MUTE=0') && !r.m0.litAfter
    && r.s1.log.includes(L(r.o) + '_SOLO=1') && r.s1.litAfter && r.s0.log.includes(L(r.o) + '_SOLO=0')));
  ok(!bad2.length, `[2] E–H adopted on the Patcher × WT / FM / ORGANIC: SOLO and MUTE reach setSynParam(SYN_OSC_x_…) on and off (${r2.length - bad2.length}/${r2.length})`,
     bad2.map(r => JSON.stringify(r)).join('\n        '));

  // 3 · exclusive across both banks: A (relay) + E (pool) soloed, then a plain click on G
  const ex = await (async () => {
    await p.evaluate(() => { window.Juce.getSliderState('SYN_OSC_A_SOLO').setNormalisedValue(1); window.Juce.getSliderState('SYN_OSC_E_SOLO').setNormalisedValue(1); });
    const g = await press(p, 'g', 'SOLO');
    const st = await p.evaluate(() => ['A', 'E', 'G'].map(X => window.Juce.getSliderState('SYN_OSC_' + X + '_SOLO').getNormalisedValue()));
    return { g, st };
  })();
  ok(ex.st[0] === 0 && ex.st[1] === 0 && ex.st[2] === 1 && ex.g.log.includes('E_SOLO=0'), '[3] a plain SOLO click is exclusive across both banks (G on → A and E off)', JSON.stringify(ex));

  // 4 · Shift-click adds
  const add = await (async () => {
    const h = await press(p, 'h', 'SOLO', true);
    const st = await p.evaluate(() => ['G', 'H'].map(X => window.Juce.getSliderState('SYN_OSC_' + X + '_SOLO').getNormalisedValue()));
    return { h, st };
  })();
  ok(add.st[0] === 1 && add.st[1] === 1 && !add.h.log.some(x => x === 'G_SOLO=0'), '[4] Shift-click SOLO adds to the set (G and H both soloed)', JSON.stringify(add));
  await p.evaluate(() => ['G', 'H'].forEach(X => window.Juce.getSliderState('SYN_OSC_' + X + '_SOLO').setNormalisedValue(0)));
  await b.close();

  // 5 · read-back: the processor holds F muted (a restored session) before the page ever touched F's state
  ({ b, p, errs } = await H.launch({ page: PAGE, noShow: true, query: '?page=5', settle: 3000, width: 1300, height: 860,
                                     pre: () => { window.__preMute = true; } }));
  await p.evaluate(() => { window.__params['SYN_OSC_F_MUTE'] = 1; 'efgh'.split('').forEach(o => { try { window.Juce.getSliderState('SYN_OSC_' + o.toUpperCase() + '_ENABLE').setNormalisedValue(1); } catch (e) {} }); if (window.__tpOpen) window.__tpOpen(); });
  await H.sleep(2600);   // ≥ one pool poll (700 ms) + the adoption
  await p.evaluate(() => { const n = document.getElementById('osc-f-device'); if (n) n.scrollIntoView({ block: 'center', inline: 'center' }); });
  await H.sleep(150);
  const rb = await press(p, 'f', 'MUTE');
  ok(rb.litBefore === true && rb.log.includes('F_MUTE=0'), '[5] E–H read back: a muted F (processor state) opens lit, and the click un-mutes it', JSON.stringify(rb));
  const real = errs.filter(e => !/formatOutput/.test(e));
  ok(real.length === 0, '[—] no page errors', real.join(' | '));
  await b.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
