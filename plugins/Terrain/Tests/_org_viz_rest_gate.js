// ══════════════════════════════════════════════════════════════════════════════════════════════
//  _org_viz_rest_gate.js — tp104 THE ORGANICS PICTURE COSTS NOTHING AT REST (fb581/fb590 law), headless.
//
//    node Tests/_org_viz_rest_gate.js [page.html]
//
//  The painter is window.__orgAnim (frames = ticks that actually wrote the DOM). Mock organicViz snapshots are
//  streamed exactly as the contract describes them: {osc, notes:[{n,lvl}], pedal} at 15 Hz while notes sound.
//   1  AT REST: 0 painter frames over 2 s, and nothing armed (no rAF, no timer)
//   2  WHILE NOTES SOUND: ≤ 30 fps, per family type (bowed strings, plucked, winds with breath arcs, keys)
//      — and the picture actually reacted (keys lit, strings rang, arcs left the bell)
//   3  AFTER THE LAST EVENT: the painter parks within 500 ms
//   4  ONE INSTRUMENT BUILD ≤ 3 ms (render + DOM insert, uncached), every one of the 23 families, warm
// ══════════════════════════════════════════════════════════════════════════════════════════════
const H = require('./_org_harness.js');
const PAGE = process.argv[2] || H.PAGE_DEFAULT;
let pass = 0, fail = 0;
const ok = (c, name, detail) => { c ? ++pass : ++fail; console.log(`  ${c ? 'PASS' : 'FAIL'}  ${name}${detail ? '\n        ' + detail : ''}`); };

(async () => {
  const { b, p, errs } = await H.launch({ page: PAGE });
  await H.enable(p, 'a'); await H.setEngine(p, 'a', 7); await H.sleep(900);

  // [1] rest
  const rest = await p.evaluate(async () => { const A = window.__orgAnim, f0 = A.frames; await new Promise(r => setTimeout(r, 2000));
    return { frames: A.frames - f0, raf: A.raf, tmr: A.tmr, ringing: A.ringing.size, arcs: A.arcs.length }; });
  ok(rest.frames === 0 && !rest.raf && !rest.tmr, '[1] at rest: 0 painter frames over 2 s, nothing armed', JSON.stringify(rest));

  // [2] + [3] per instrument: stream 2 s of snapshots at 15 Hz (a moving chord), then the empty snapshot
  const runs = [];
  for (const [id, sel] of [['vsco.violin.section', '.r-string.ring'], ['freepats.nylon', '.r-string.ring'], ['vsco.flute', '.wave'], ['vsco.trumpet', '.r-valve.on'], ['salamander.grand', '.r-key.on']]) {
    const r = await p.evaluate(async (id, sel) => { const sleep = ms => new Promise(r => setTimeout(r, ms)); await window.__orgSetInstrument('a', id); await sleep(300);
      const A = window.__orgAnim, f0 = A.frames, t0 = performance.now(); let seen = 0;
      for (let k = 0; k < 30; k++) { const base = 55 + (k >> 2) * 2;   // a chord that moves every 4 frames (new notes on, old ones off)
        window.__orgMock.emitViz({ osc: 0, notes: [{ n: base, lvl: .8 }, { n: base + 4, lvl: .6 }, { n: base + 7, lvl: .7 }], pedal: 0 });
        if (document.querySelector('#osc-a-device .org-viz ' + sel)) seen++;
        await sleep(1000 / 15); }
      const t1 = performance.now(), frames = A.frames - f0;
      const st = A.stamps.filter(x => x >= t0 && x <= t1), fps = st.length > 1 ? (st.length - 1) / ((st[st.length - 1] - st[0]) / 1000) : 0;   // frame RATE = intervals / span
      window.__orgMock.emitViz({ osc: 0, notes: [], pedal: 0 });   // the last snapshot: nothing sounds
      const tLast = performance.now(); let lastFrame = A.frames, lastAt = tLast;
      while (performance.now() - tLast < 1500) { await sleep(10); if (A.frames !== lastFrame) { lastFrame = A.frames; lastAt = performance.now(); } }
      return { id, fps: +fps.toFixed(1), frames, reacted: seen, parkMs: Math.round(lastAt - tLast), armed: !!(A.raf || A.tmr) }; }, id, sel);
    runs.push(r);
  }
  ok(runs.every(r => r.fps <= 30 && r.reacted > 0) && runs.some(r => r.frames > 0),
     '[2] while notes sound: ≤ 30 painter fps, and the picture reacts (strings ring, arcs leave the bell, valves press, keys light)',
     runs.map(r => `${r.id}: ${r.fps} fps (${r.frames}) reacted ${r.reacted}/30`).join(' · '));
  ok(runs.every(r => r.parkMs <= 500 && !r.armed), '[3] the painter parks within 500 ms of the last event',
     runs.map(r => `${r.id}: ${r.parkMs} ms`).join(' · '));

  // [4] build cost
  const bench = await p.evaluate(() => { const fams = Object.keys(window.__orgArt.families); fams.forEach(f => window.__orgBench('a', f));   // warm (JIT)
    const t = {}; fams.forEach(f => { const s = []; for (let k = 0; k < 7; k++) s.push(window.__orgBench('a', f)); s.sort((x, y) => x - y); t[f] = +s[3].toFixed(2); }); return t; });   // the median of 7 (a GC pause is not the build)
  const worst = Object.entries(bench).sort((a, b) => b[1] - a[1]);
  ok(worst[0][1] <= 3 && Object.keys(bench).length === 23, `[4] one instrument build ≤ 3 ms (median of 7, warm), all 23 families (worst ${worst[0][0]} ${worst[0][1]} ms)`,
     worst.map(([k, v]) => k + ' ' + v).join(' · '));
  ok(errs.filter(e => !/formatOutput/.test(e)).length === 0, '[—] no page errors', errs.join(' | '));
  await b.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL (crash)', e); process.exit(1); });
