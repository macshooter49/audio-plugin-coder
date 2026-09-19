// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp55 — THE EYE: an extended flow card's emblem ANIMATES, and the sampler's picture goes
//  EDGE TO EDGE.  node Tests/_tp55_eye_gate.js <abs path to index.html>
//
//  Max on the first: "I just want the regular plain white ANIMATED emblem."  tp54b gave him the
//  emblem back but as a CLONE made once at adopt time — it can never move, because the frame
//  dispatcher paints the LIVE tile's own child elements. ⚠️ tp54b's probe could not have caught
//  that: it dispatched a 'click' on the eye tool, and THE TOOLBAR LISTENS ON mousedown — the eye
//  was never open, so it shipped on a class name instead of on what the eye showed. This gate
//  asserts #tp-page actually carries .viz before it measures anything.
//
//  ⚠️ AND THE TILES ONLY MOVE WHILE MIDI IS SOUNDING (fb636 h1 — "the MIDI is the only thing that
//  moves them"). A motion bar that does not hold window.__tiLive open measures a resting page and
//  reads FALSE for code that works.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
let pass = 0, fail = 0;
const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l + (d ? '\n        ' + d : '')); }
                          else { fail++; console.log('  FAIL  ' + l + (d ? '\n        ' + d : '')); } };
// a fingerprint of every inline style the tile animator writes
const snap = () => { const out = [];
  document.querySelectorAll('#tp-page .flow-mode, #tp-page .tp-vglyph').forEach(t =>
    t.querySelectorAll('.arpNote,.arpLine,.arpHold,.sq,.rbnWind').forEach(e => out.push(e.getAttribute('style') || '')));
  return out.join('|'); };

(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 1200, height: 820, deviceScaleFactor: 2 });
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 150)));
  await p.evaluateOnNewDocument(() => { try { localStorage.setItem('tpLayout', JSON.stringify({ v: 6, pos: {}, ext: {} })); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1800);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark');
    const d = document.getElementById('osc-a-device'); if (d) d.classList.remove('osc-off');
    try { window.__flowSetChain(['arp']); } catch (e) {} });
  await sleep(700); await p.evaluate(() => window.setActivePanel('tp')); await sleep(2600);
  await p.evaluate(() => { try { window.__tpViz(true); } catch (e) {} }); await sleep(1400);
  await p.evaluate(() => { window.__tiLive = () => true; }); await sleep(900);

  const read = () => p.evaluate(() => {
    const pg = document.getElementById('tp-page');
    const w = [...document.querySelectorAll('#tp-page .tp-node')].find(n => (n.dataset.key || '').startsWith('flow-'));
    const t = w && w.querySelector('.flow-mode'), g = w && w.querySelector('.tp-vglyph');
    const nb = w ? w.getBoundingClientRect() : null, tb = t ? t.getBoundingClientRect() : null;
    return { eye: !!(pg && pg.classList.contains('viz')),
             tileViz: t ? t.classList.contains('tp-viz') : null,
             tileShown: t ? getComputedStyle(t).display !== 'none' : null,
             glyphViz: g ? g.classList.contains('tp-viz') : null,
             fills: (nb && tb) ? (Math.abs(tb.width - nb.width) < 2 && Math.abs(tb.height - nb.height) < 2) : null,
             node: nb ? [Math.round(nb.width), Math.round(nb.height)] : null,
             tile: tb ? [Math.round(tb.width), Math.round(tb.height)] : null };
  });
  const moves = async () => { const a = await p.evaluate(snap); await sleep(1300); return a !== (await p.evaluate(snap)); };

  // ── [0] THE EYE IS ACTUALLY OPEN (the bar tp54b's probe was missing) ──────────────────────
  const s0 = await read();
  ok(s0.eye === true, '[0] THE EYE IS OPEN — asserted before anything else is measured', JSON.stringify(s0));

  // ── [1] a plain flow card in the eye: the LIVE tile, and it moves ─────────────────────────
  ok(s0.tileViz === true && s0.glyphViz === false && s0.fills === true,
     '[1] A PLAIN FLOW CARD SHOWS ITS LIVE TILE, filling the node', JSON.stringify(s0));
  ok(await moves(), '[1b] AND IT ANIMATES while MIDI is sounding');

  // ── [2] 🚨 EXTENDED — the same live tile, not a still clone ───────────────────────────────
  await p.evaluate(() => { try { window.__tpExt('flow-arp', true); } catch (e) {} }); await sleep(1800);
  const s2 = await read();
  ok(s2.tileViz === true && s2.tileShown === true && s2.glyphViz === false && s2.fills === true,
     '[2] 🚨 AN EXTENDED FLOW CARD SHOWS THE LIVE TILE TOO — tp54b showed a clone here, which is why it could not move',
     JSON.stringify(s2));
  ok(await moves(), '[2b] 🚨 AND THE EXTENDED CARD\'S EMBLEM ANIMATES — the thing Max actually asked for');

  // ── [3] tp33 STAYS GREEN — the extended card must not spill out of its node in the eye ────
  const spill = await p.evaluate(() => {
    const w = [...document.querySelectorAll('#tp-page .tp-node')].find(n => (n.dataset.key || '') === 'flow-arp');
    if (!w) return -1; const nr = w.getBoundingClientRect(); let worst = 0;
    w.querySelectorAll('.ti-card, .ti-card *').forEach(e => { const r = e.getBoundingClientRect();
      if (!r.width || !r.height) return;
      worst = Math.max(worst, nr.left - r.left, r.right - nr.right, nr.top - r.top, r.bottom - nr.bottom); });
    return Math.round(worst);
  });
  ok(spill <= 2, '[3] tp33 HOLDS — the extended card\'s ink stays inside its node box', 'worst overhang ' + spill + ' px');

  // ── [4] 🚨 THE SAMPLER'S PICTURE, EDGE TO EDGE ────────────────────────────────────────────
  //  MEASURED BEFORE tp55: a 235 x 51 strip in a 258 x 127 module — 25 px of dead ground over it
  //  and 52 px under. Everything else in the eye is SCALED, and scaling is right when the picture
  //  has an aspect ratio to protect; a WAVEFORM has none.
  await p.evaluate(() => { const d = document.getElementById('osc-a-device'); if (!d) return;
    d.classList.add('engine-sample');
    try { if (window.__terrainSampleRedraw) window.__terrainSampleRedraw('a'); } catch (e) {} });
  await sleep(1000);
  await p.evaluate(() => { try { window.__tpViz(false); } catch (e) {} }); await sleep(700);
  await p.evaluate(() => { try { window.__tpViz(true); } catch (e) {} }); await sleep(1400);
  const samp = await p.evaluate(() => {
    const w = [...document.querySelectorAll('#tp-page .tp-node')].find(n => (n.dataset.key || '') === 'osc-a');
    if (!w) return { err: 'no osc-a node' };
    const v = w.querySelector('.tp-viz'); if (!v) return { err: 'no viz' };
    const nr = w.getBoundingClientRect(), r = v.getBoundingClientRect();
    return { cls: v.className, node: [Math.round(nr.width), Math.round(nr.height)], viz: [Math.round(r.width), Math.round(r.height)],
             gaps: [Math.round(r.top - nr.top), Math.round(nr.bottom - r.bottom), Math.round(r.left - nr.left), Math.round(nr.right - r.right)] };
  });
  const gmax = samp.gaps ? Math.max(...samp.gaps.map(Math.abs)) : 999;
  ok(/samp-disp/.test(samp.cls || '') && gmax <= 2,
     '[4] 🚨 THE SAMPLER\'S PICTURE FILLS ITS MODULE — top, bottom and both sides (was 25 over / 52 under)',
     JSON.stringify(samp));

  // ── [5] AND LEAVING THE EYE GIVES IT BACK ─────────────────────────────────────────────────
  await p.evaluate(() => { try { window.__tpViz(false); } catch (e) {} }); await sleep(1100);
  const back = await p.evaluate(() => {
    const w = [...document.querySelectorAll('#tp-page .tp-node')].find(n => (n.dataset.key || '') === 'osc-a');
    const v = w && w.querySelector('.samp-disp'); if (!v) return { err: 'gone' };
    return { w: v.style.width || '(none)', h: v.style.height || '(none)', vz: v.style.getPropertyValue('--vz') || '(none)' };
  });
  ok(back.w === '(none)' && back.h === '(none)' && back.vz === '(none)',
     '[5] LEAVING THE EYE HANDS THE SAMPLER ITS OWN BOX BACK — no inline size left behind', JSON.stringify(back));

  // ── [6] the wavetable's picture keeps tp47's centred fit ──────────────────────────────────
  await p.evaluate(() => { const d = document.getElementById('osc-a-device'); if (d) d.classList.remove('engine-sample'); });
  await sleep(500);
  await p.evaluate(() => { try { window.__tpViz(true); } catch (e) {} }); await sleep(1300);
  const wt = await p.evaluate(() => {
    const w = [...document.querySelectorAll('#tp-page .tp-node')].find(n => (n.dataset.key || '') === 'osc-a');
    const v = w && w.querySelector('.tp-viz'); if (!v) return { err: 'no viz' };
    const nr = w.getBoundingClientRect(), r = v.getBoundingClientRect();
    return { cls: v.className, fills: Math.abs(r.height - nr.height) < 3 };
  });
  ok(/osc-display/.test(wt.cls || '') && wt.fills === false,
     '[6] THE WAVETABLE\'S PICTURE IS UNTOUCHED — tp47 settled that one ("everything else looks fucked up")', JSON.stringify(wt));

  ok(errs.length === 0, '[7] THE PAGE THREW NOTHING', errs.join(' | '));
  console.log('\n  ' + pass + ' passed, ' + fail + ' failed\n');
  await b.close(); process.exit(fail ? 1 : 0);
})();
