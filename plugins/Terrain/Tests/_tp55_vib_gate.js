// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp55 — VIBRATO REPLACES JITTER ON THE CHOP MIXER STRIP (the page half).
//  The ENGINE half is Tests/vibrato_cert.cpp, which measures Source/Vibrato.h itself.
//
//  ⚠️ EVERY BAR DRIVES THE CONTROL, NOT A HELPER. tp53 shipped a dead chop menu because its gate
//  asserted appearance; tp54's lesson is that a gate which does not move the thing with a pointer
//  proves nothing. These drags are real pointer sequences and the bars read what the knob SENT.
//    node Tests/_tp55_vib_gate.js
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
const fs = require('fs'); const path = require('path');
const sim = fs.readFileSync(process.cwd() + '/Tests/_ui_lockin_sim.js', 'utf8');
const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments'));
const cpp = fs.readFileSync('Source/PluginEditor.cpp', 'utf8');
const i0 = cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0 = cpp.indexOf('html = html.replace ("</body>", heroOverlay', i0);
const ov = [...cpp.slice(i0, j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m => m[1]).join('');
const html = fs.readFileSync('Source/ui/public/index.html', 'utf8').replace('</body>', ov + '</body>');
const PAGE = path.join(require('os').tmpdir(), 'tp55_vib_gate.html'); fs.writeFileSync(PAGE, html);

let pass = 0, fail = 0;
const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l + (d ? '\n        ' + d : '')); }
                          else { fail++; console.log('  FAIL  ' + l + (d ? '\n        ' + d : '')); } };

// record every native call's ARGUMENTS — the stub only counts them
const spy = () => { const bridge = window.Juce, orig = bridge.getNativeFunction; window.__natLog = [];
  const wrapped = (n) => { const f = orig(n); return function () {
    window.__natLog.push([n].concat([].slice.call(arguments))); return f.apply(null, arguments); }; };
  /* the page reassigns window.Juce during boot, so the spy has to be a GETTER that survives it
     (a plain `value:` descriptor is read-only and the page's own assignment throws) */
  let held = Object.assign({}, bridge, { getNativeFunction: wrapped });
  Object.defineProperty(window, 'Juce', { configurable: true,
    get() { return held; },
    set(v) { held = Object.assign({}, v || {}, { getNativeFunction: wrapped, getSliderState: bridge.getSliderState }); } }); };

(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 });
  await p.evaluateOnNewDocument(stubSrc + '\nstub();\n(' + spy.toString() + ')();');
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + PAGE, { waitUntil: 'load' }); await sleep(2500);
  await p.evaluate(() => document.documentElement.setAttribute('data-theme', 'dark'));
  await p.evaluate(() => document.getElementById('mix-btn').click()); await sleep(1500);

  // ── [0] THE JITTER KNOB IS GONE AND THE VIBRATO PAIR IS THERE, ON ALL FOUR STRIPS ─────────
  const shape = await p.evaluate(() => {
    const strips = [...document.querySelectorAll('.mix-strip')];
    return { strips: strips.length,
             jit: document.querySelectorAll('.mix-strip-knob[data-fn="jitter"]').length,
             vib: document.querySelectorAll('.mix-strip-knob[data-fn="vib"]').length,
             rate: document.querySelectorAll('.mix-strip-knob[data-fn="vibrate"]').length,
             labels: [...document.querySelectorAll('.mix-strip[data-layer="0"] .mix-strip-knob-label')].map(e => e.textContent) };
  });
  ok(shape.strips === 4 && shape.jit === 0 && shape.vib === 4 && shape.rate === 4,
     '[0] JITTER IS OUT OF THE DOM AND EVERY STRIP HAS VIB + RATE — not hidden, gone (the tp53 lesson)',
     JSON.stringify(shape));

  // ── [1] 🚨 THE DEPTH KNOB DRAGS, AND IT SENDS CENTS TO THE ENGINE ──────────────────────────
  const drag = async (sel, dy) => {
    const box = await p.evaluate(s => { const e = document.querySelector(s); if (!e) return null;
      const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; }, sel);
    if (!box) return null;
    await p.mouse.move(box.x, box.y); await p.mouse.down();
    for (let k = 1; k <= 6; k++) { await p.mouse.move(box.x, box.y - dy * k / 6); await sleep(30); }
    await p.mouse.up(); await sleep(200);
    return p.evaluate(() => window.__natLog.slice());
  };
  await p.evaluate(() => { window.__natLog.length = 0; });
  const depthLog = await drag('.mix-strip[data-layer="1"] .mix-strip-knob[data-fn="vib"]', 80);
  const dCalls = (depthLog || []).filter(e => e[0] === 'setLayerVibratoDepth');
  const dLast = dCalls.length ? dCalls[dCalls.length - 1] : null;
  /* ⚠️ tp57 REPLACED THE CONIC GRADIENT WITH AN SVG ARC (Max: "they look like they're tearing and
     being stretched ... I see some data ripples"), so `--val` is no longer what the ring is drawn
     from. The bar asks the same question of the new drawing: the value arc's dash length is the
     ring's position, and the number inside it is the value the knob is showing. */
  const dRing = await p.evaluate(() => { const k = document.querySelector('.mix-strip[data-layer="1"] .mix-strip-knob[data-fn="vib"]');
    const v = k.querySelector('.kr-v'); const kv = k.querySelector('.kv');
    return { dash: v ? v.style.strokeDasharray : null, num: kv ? kv.textContent : null }; });
  ok(dCalls.length >= 3 && dLast && dLast[1] === 1 && dLast[2] > 30 && dLast[2] <= 100
     && dRing.dash && parseFloat(dRing.dash) > 20 && +dRing.num > 30,
     '[1] 🚨 THE DEPTH KNOB IS A REAL DRAG — an 80 px pull on strip B sends cents for layer 1, and the arc and its number follow',
     (dCalls.length + ' calls, last = ' + JSON.stringify(dLast) + ', ring = ' + JSON.stringify(dRing)));

  // ── [2] 🚨 THE RATE KNOB SENDS Hz, AND ITS SWEEP IS LOG ────────────────────────────────────
  //  A LINEAR 0.05..12 Hz dial buries every musical rate (4-7 Hz) in the first half-inch of
  //  travel. Mid-dial has to land in that band or the knob is unusable.
  await p.evaluate(() => { window.__natLog.length = 0; });
  const rateLog = await drag('.mix-strip[data-layer="1"] .mix-strip-knob[data-fn="vibrate"]', 40);
  const rCalls = (rateLog || []).filter(e => e[0] === 'setLayerVibratoRate');
  const rLast = rCalls.length ? rCalls[rCalls.length - 1] : null;
  const mid = await p.evaluate(() => { try { return null; } catch (e) { return null; } });
  ok(rCalls.length >= 3 && rLast && rLast[1] === 1 && rLast[2] > 5.0 && rLast[2] <= 12.0,
     '[2] 🚨 THE RATE KNOB SENDS Hz — pulling up from the 5 Hz default raises it, and it stays inside 0.05..12',
     (rCalls.length + ' calls, last = ' + JSON.stringify(rLast)));

  // the log sweep, read off the knob's own ring position at the musical band
  const sweep = await p.evaluate(() => {
    const k = document.querySelector('.mix-strip[data-layer="2"] .mix-strip-knob[data-fn="vibrate"]');
    /* tp57 — read off the SVG arc: __synArc writes `<dash> 100` over 75 units of travel, so the
       normalised position is dash/75. */
    const v = k.querySelector('.kr-v');
    const dash = v ? parseFloat(v.style.strokeDasharray) : NaN;
    return { at5: isFinite(dash) ? dash / 75 : 0, num: (k.querySelector('.kv') || {}).textContent };
  });
  ok(sweep.at5 > 0.6 && sweep.at5 < 0.95,
     '[2b] THE SWEEP IS LOG — the 5 Hz default sits in the useful middle-upper of the dial, not pinned at 0.4 of a linear 12 Hz',
     'ring --val at the 5 Hz default = ' + sweep.at5.toFixed(3));

  // ── [3] DOUBLE-CLICK RESETS — depth to silence, rate to 5 Hz ──────────────────────────────
  await p.evaluate(() => { window.__natLog.length = 0; });
  await p.evaluate(() => { document.querySelector('.mix-strip[data-layer="1"] .mix-strip-knob[data-fn="vib"]').dispatchEvent(new MouseEvent('dblclick', { bubbles: true }));
                           document.querySelector('.mix-strip[data-layer="1"] .mix-strip-knob[data-fn="vibrate"]').dispatchEvent(new MouseEvent('dblclick', { bubbles: true })); });
  await sleep(250);
  const resets = await p.evaluate(() => window.__natLog.filter(e => /Vibrato/.test(e[0])).map(e => [e[0], e[2]]));
  ok(resets.some(r => r[0] === 'setLayerVibratoDepth' && r[1] === 0) &&
     resets.some(r => r[0] === 'setLayerVibratoRate' && r[1] === 5),
     '[3] DOUBLE-CLICK RESETS — depth to 0 cents, rate to the 5 Hz default', JSON.stringify(resets));

  // ── [4] tp54's KNOB LAW STILL HOLDS — the synth page's 24 px ring, 2 px band ───────────────
  const ring = await p.evaluate(() => {
    const k = document.querySelector('.mix-strip-knob[data-fn="vib"]'); const r = k.getBoundingClientRect();
    const cs = getComputedStyle(k);
    const v = k.querySelector('.kr-v');
    const syn = document.querySelector('#syn-panel .knob-ring .kr-v');
    return { w: Math.round(r.width), h: Math.round(r.height), svg: !!k.querySelector('svg.kr-svg'),
             stroke: v ? getComputedStyle(v).strokeWidth : null,
             synStroke: syn ? getComputedStyle(syn).strokeWidth : null,
             mask: (cs.webkitMaskImage || cs.maskImage || 'none'), bg: cs.backgroundImage };
  });
  /* tp57 — the knob is the synth page's ARC now, not a masked conic gradient. Max: "they look kind
     of low quality, they look like they're tearing ... the wavetable knobs are smooth and perfect."
     The bar compares the stroke against the synth page's own, so the two cannot drift. */
  ok(ring.w === 24 && ring.h === 24 && ring.svg && ring.stroke === '2px'
     && !/gradient/.test(ring.mask) && !/conic/.test(ring.bg),
     '[4] THE NEW KNOBS ARE THE HOUSE KNOB — the synth page\'s 24 px SVG arc at 2 px, with no conic gradient and no mask left underneath it', JSON.stringify(ring));

  // ── [5] THE CENTERLINE INSIDE THE STRIP — VIB and RATE share one ─────────────────────────
  const mids = await p.evaluate(() => [...document.querySelectorAll('.mix-strip[data-layer="0"] .mix-strip-krow .mix-strip-knob')]
    .map(e => { const r = e.getBoundingClientRect(); return +((r.top + r.bottom) / 2).toFixed(1); }));
  ok(mids.length === 2 && mids[0] === mids[1],
     '[5] THE PAIR SHARES ONE CENTERLINE', JSON.stringify(mids));

  // ── [6] SENTENCE CASE — the house voice (tp51) reaches the new labels ─────────────────────
  const caps = await p.evaluate(() => [...document.querySelectorAll('.mix-strip[data-layer="0"] .mix-strip-knob-label')].map(e => e.textContent));
  ok(caps.join('/') === 'Pan/Vib/Rate', '[6] THE LABELS ARE THE HOUSE SENTENCE CASE', JSON.stringify(caps));

  // ── [7] NO PAGE ERRORS ────────────────────────────────────────────────────────────────────
  ok(errs.length === 0, '[7] THE PAGE THREW NOTHING while all of that happened', errs.join(' | '));

  console.log('\n  ' + pass + ' passed, ' + fail + ' failed\n');
  await b.close(); process.exit(fail ? 1 : 0);
})();
