// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp56 — THE CHOP SAMPLERS ARE REAL MODULES ON THE PATCHER (the page half).
//  Max: "these aren't the sample oscillator engine, these are sample CHOPPED engines" — a browser
//  sub-category Chop with Sampler A/B/C/D on the canvas, routable into the rack.
//
//  Every bar drives the real door (window.__tpAdd / __tpConnect / __tpCut are the same functions
//  the mouse calls) and then reads the PARAMETER the engine will act on — not a class name, not a
//  cable's appearance. tp54's lesson: a gate that asserts looks proves nothing.
//    node Tests/_tp56_chopsamp_gate.js <abs path to index.html>
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
let pass = 0, fail = 0;
const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l + (d ? '\n        ' + d : '')); }
                          else { fail++; console.log('  FAIL  ' + l + (d ? '\n        ' + d : '')); } };

(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 1400, height: 900, deviceScaleFactor: 2 });
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.evaluateOnNewDocument(() => { try { localStorage.setItem('tpLayout', JSON.stringify({ v: 6, pos: {}, ext: {} })); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1900);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark');
    const d = document.getElementById('osc-a-device'); if (d) d.classList.remove('osc-off'); });
  await sleep(500); await p.evaluate(() => window.setActivePanel('tp')); await sleep(2600);

  // ── [0] THE BROWSER HAS A CHOP SHELF WITH FOUR SAMPLERS ───────────────────────────────────
  const cat = await p.evaluate(() => {
    const all = window.__tpCatalog ? window.__tpCatalog() : [];
    return { chop: all.filter(i => i.cat === 'Chop').map(i => i.k + '|' + i.n),
             cats: [...new Set(all.map(i => i.cat))] };
  });
  ok(cat.chop.length === 4 && cat.chop[0] === 'samp:a|Sampler A' && cat.chop[3] === 'samp:d|Sampler D',
     '[0] THE BROWSER OFFERS Chop → Sampler A/B/C/D', JSON.stringify(cat.chop));

  // ── [1] ADDING ONE PUTS A REAL NODE ON THE CANVAS, WITH AN AUDIO OUT AND NO MIDI IN ───────
  await p.evaluate(() => window.__tpAdd('samp:b', 300, 300)); await sleep(900);
  const node = await p.evaluate(() => {
    const n = window.__tpNodeByKey('samp-b'); if (!n) return { err: 'no node' };
    const w = document.querySelector('#tp-page .tp-node[data-key="samp-b"]');
    return { kind: n.kind, sub: n.sub, name: (window.__tpNodeByKey && n) ? null : null,
             ports: n.ports.map(q => q.kind + ':' + (q.el.dataset.t || '')),
             letter: w ? (w.querySelector('.tp-samp-l') || {}).textContent : null,
             inDom: !!w, box: w ? [Math.round(w.getBoundingClientRect().width), Math.round(w.getBoundingClientRect().height)] : null };
  });
  /*  ⚠️ tp58 CHANGED THIS LAW ON PURPOSE, and the bar moved with it rather than being deleted.
      tp56 asserted `ports === 'out:a'` — a Sampler was a SOURCE and nothing else. Max: "why
      doesn't the sampler have an INPUT? I want to patch FX THRU IT BRUH CMONNN even FLOW CARDS n
      SUCH." A cable is a relationship, and "Sampler A through the Distortion" is the same
      relationship whichever end you drag from; with no input it could only ever be drawn one way.
      WHAT THE ORIGINAL BAR WAS ACTUALLY PROTECTING STILL HOLDS AND IS STILL CHECKED: the input is
      AUDIO ('a'), never MIDI ('n'). A chop layer's notes come from the Chop page's own keys, not
      from the Patcher's MIDI node, and a note cable must still find nowhere to land here. */
  ok(node.kind === 'samp' && node.sub === 'b' && node.inDom && node.letter === 'B'
     && node.ports.slice().sort().join(',') === 'in:a,out:a',
     '[1] Sampler B IS A MODULE — an AUDIO in and an AUDIO out, and NO MIDI in (its notes come from the Chop page\'s own keys)', JSON.stringify(node));

  // ── [2] UNCABLED, IT DRAWS TO OUT — the Chop page\'s own mixer path ────────────────────────
  const dry = await p.evaluate(() => (window.__tpDerive() || []).filter(c => c.from && c.from.key === 'samp-b').map(c => c.to.key + (c.edit && c.edit.chopDry ? ' (dry)' : '')));
  ok(dry.length === 1 && dry[0] === 'out (dry)',
     '[2] AN UNCABLED SAMPLER GOES TO OUT — nothing claims it, so the Chop mixer carries it exactly as before', JSON.stringify(dry));

  // ── [3] 🚨 CABLING IT INTO AN EFFECT WRITES THAT DEVICE'S _CHOPS BIT ──────────────────────
  //  This is the bar that matters: the cable has to move the PARAMETER the DSP reads, and it has
  //  to be the right BIT (layer B is bit 1 — a fix that lit bit 0 would still "make a cable").
  await p.evaluate(() => window.__tpAdd('reverb', 900, 300)); await sleep(1100);
  const fxKey = await p.evaluate(() => { const n = (window.__tpDerive() || []) && null;
    const keys = [...document.querySelectorAll('#tp-page .tp-node')].map(w => w.dataset.key);
    return keys.filter(k => /^fx-reverb/.test(k))[0] || null; });
  const wired = await p.evaluate((fk) => {
    const a = window.__tpNodeByKey('samp-b'), bn = window.__tpNodeByKey(fk);
    if (!a || !bn) return { err: 'nodes', fk };
    const okc = window.__tpConnect(a, 'out', 0, bn, 'in', 0);
    const d = (window.__fxrDevs ? window.__fxrDevs() : [])[bn.idx];
    const id = d && d.tapsP ? String(d.tapsP).replace(/_TAPS$/, '_CHOPS') : null;
    /* ⚠️ READ IT THE WAY THE CANVAS WRITES IT. The Patcher's params go through
       window.__synSliderShim, not window.Juce.getSliderState — reading the wrong one showed 0 for a
       mask that was correctly set, which is a gate failing for a reason that has nothing to do with
       the code under test. __tpParam is the canvas's own reader. */
    const raw = window.__tpParam(id);
    return { okc, id, bits: raw == null ? null : Math.round(raw * 15) };
  }, fxKey);
  ok(wired.okc === true && wired.id === 'SYN_RVB_CHOPS' && wired.bits === 2,
     '[3] 🚨 THE CABLE WRITES SYN_RVB_CHOPS — and it is BIT 1, because the module is layer B, not layer A',
     JSON.stringify(wired));

  // ── [4] AND THE CABLE IS DRAWN FROM THE SAMPLER TO THAT DEVICE, not to Out ────────────────
  const drawn = await p.evaluate(() => (window.__tpDerive() || []).filter(c => c.from && c.from.key === 'samp-b')
    .map(c => ({ to: c.to.key, edit: c.edit && c.edit.chop ? ('chop L=' + c.edit.chop.L) : (c.edit && c.edit.chopDry ? 'dry' : null) })));
  ok(drawn.length === 1 && /^fx-reverb/.test(drawn[0].to) && drawn[0].edit === 'chop L=b',
     '[4] THE CANVAS AGREES WITH THE ENGINE — one cable, Sampler B → Reverb, and none to Out', JSON.stringify(drawn));

  // ── [5] A SECOND SAMPLER ON THE SAME DEVICE SETS ITS OWN BIT, not a replacement ───────────
  await p.evaluate(() => window.__tpAdd('samp:d', 300, 480)); await sleep(800);
  const two = await p.evaluate((fk) => {
    const a = window.__tpNodeByKey('samp-d'), bn = window.__tpNodeByKey(fk);
    window.__tpConnect(a, 'out', 0, bn, 'in', 0);
    const d = (window.__fxrDevs ? window.__fxrDevs() : [])[bn.idx];
    const id = String(d.tapsP).replace(/_TAPS$/, '_CHOPS');
    return Math.round(window.__tpParam(id) * 15);
  }, fxKey);
  ok(two === 10, '[5] TWO SAMPLERS, TWO BITS — B (bit 1) and D (bit 3) is 0b1010 = 10, not a replacement', 'mask = ' + two);

  // ── [6] CUTTING ONE CABLE CLEARS ONLY ITS BIT ─────────────────────────────────────────────
  const afterCut = await p.evaluate((fk) => {
    const c = (window.__tpDerive() || []).filter(x => x.from && x.from.key === 'samp-b' && x.edit && x.edit.chop)[0];
    if (!c) return { err: 'no cable' };
    const done = window.__tpCut(c.id);
    const bn = window.__tpNodeByKey(fk), d = (window.__fxrDevs ? window.__fxrDevs() : [])[bn.idx];
    const id = String(d.tapsP).replace(/_TAPS$/, '_CHOPS');
    return { done, bits: Math.round(window.__tpParam(id) * 15) };
  }, fxKey);
  ok(afterCut.done === true && afterCut.bits === 8,
     '[6] CUTTING SAMPLER B\'s CABLE LEAVES D\'s — 0b1000 = 8', JSON.stringify(afterCut));

  // ── [7] CABLING TO OUT DROPS EVERY CLAIM ──────────────────────────────────────────────────
  const toOut = await p.evaluate((fk) => {
    const a = window.__tpNodeByKey('samp-d'), o = window.__tpNodeByKey('out');
    window.__tpConnect(a, 'out', 0, o, 'in', 0);
    const bn = window.__tpNodeByKey(fk), d = (window.__fxrDevs ? window.__fxrDevs() : [])[bn.idx];
    const id = String(d.tapsP).replace(/_TAPS$/, '_CHOPS');
    return Math.round(window.__tpParam(id) * 15);
  }, fxKey);
  ok(toOut === 0, '[7] CABLING A SAMPLER TO OUT DROPS EVERY CLAIM — back to the Chop page\'s own mixer', 'mask = ' + toOut);

  // ── [8] TAKING THE MODULE OFF THE CANVAS ALSO DROPS ITS CLAIMS ────────────────────────────
  const removed = await p.evaluate((fk) => {
    const a = window.__tpNodeByKey('samp-b'), bn = window.__tpNodeByKey(fk);
    window.__tpConnect(a, 'out', 0, bn, 'in', 0);
    const d = (window.__fxrDevs ? window.__fxrDevs() : [])[bn.idx];
    const id = String(d.tapsP).replace(/_TAPS$/, '_CHOPS');
    const before = Math.round(window.__tpParam(id) * 15);
    window.__tpRemove('samp-b');
    return { before, after: Math.round(window.__tpParam(id) * 15),
             gone: !window.__tpNodeByKey('samp-b') };
  }, fxKey);
  await sleep(500);
  ok(removed.before === 2 && removed.after === 0 && removed.gone,
     '[8] REMOVING THE MODULE HANDS THE LAYER BACK — a routed layer that lost its module would be routed into nothing and go SILENT', JSON.stringify(removed));

  // ── [9] IT SURVIVES A CLOSE AND RE-OPEN ───────────────────────────────────────────────────
  const persisted = await p.evaluate(() => { const l = window.__tpLayout(); return l && l.samp ? Object.keys(l.samp).filter(k => l.samp[k]) : []; });
  ok(persisted.length === 1 && persisted[0] === 'd',
     '[9] THE PATCH REMEMBERS WHICH SAMPLERS ARE ON THE CANVAS', JSON.stringify(persisted));

  ok(errs.length === 0, '[10] THE PAGE THREW NOTHING', errs.join(' | '));
  console.log('\n  ' + pass + ' passed, ' + fail + ' failed\n');
  await b.close(); process.exit(fail ? 1 : 0);
})();
