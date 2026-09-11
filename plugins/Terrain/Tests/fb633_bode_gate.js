// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb633_bode_gate.js — fb633: THE BODE CARD — the live layer that vanished, the node that trailed,
//  the streams that scattered, the canvas that nothing chased.
//
//    NODE_PATH=Tests/node_modules node Tests/fb633_bode_gate.js          # from plugins/Terrain
//
//  Max: "the Bode visualization can disappear sometimes — make sure it always self-heals … under the
//  header, that line or those dots, there's multiple, there only needs to be one … bugged out every
//  time there's modulation to the Shift."
//
//  The page runs headless with the rack gates' stub (Tests/fxmod_move.js), a Bode is added with
//  window.__fxAdd('bod'), the MAIN filter's analyzer is fed through the real C++ entry point
//  window.__terrainEqAnalyzer({pre,post,sr}) in the real wire format (four decimals, a bare 0 for a
//  silent bin — PluginEditor.cpp's APF), Shift is modulated through window.__fxModEff the way the
//  engine publishes it (fb457), and the frames are ticked with window.__fx4Tick. Every bar reads the
//  VISIBLE canvas' pixels, never the model.
//
//  THE BARS
//   1  A TONE IS NOT SILENCE — a 1 kHz sine at -40 dBFS draws the live layer and no ladder; at
//      -70 dBFS the wire carries zeros and the ladder stands (no signal, no ink — fb156 kept)
//   2  ONE NODE, ON THE CANVAS — 300 frames of a Shift LFO: the SVG node and span paint nothing,
//      the canvas carries exactly ONE node-sized blob at rail height, and .bod-n@cx still tracks the
//      modulation (the fb457 probe is untouched)
//   3  A THROW CANNOT BLANK THE CARD — the spectrum helper is sabotaged for ten frames: the last
//      good picture stays on screen, __fx4Errs counts every throw, the card draws again on restore
//   4  THE STREAMS WAIT FOR A STILL SHIFT — under the LFO the canvas inks the live layer + the node
//      only; with the shift at rest the ghosts and streams return; a far shift never piles at 20 Hz
//   5  A BLANK CANVAS IS CHASED AT REST — the visible canvas is wiped twice with the clock idle; the
//      1 Hz watchdog repaints it both times (the old once-latch let the second stay blank)
//
//  MUTATION CONTROLS (each flips one bar to the OLD behaviour, so a correct page goes RED)
//    BG_MUT=mean    [1] expects the -40 dBFS sine to be suppressed (the mean-only level term)
//    BG_MUT=svg     [2] expects the SVG node to paint (the trail-prone node)
//    BG_MUT=blank   [3] expects the card to go blank on a throw (clear-then-draw)
//    BG_MUT=jitter  [4] expects ghosts + streams under the LFO (the scatter)
//    BG_MUT=once    [5] expects the second wipe to stay blank (the once-latch)
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require('path');
const puppeteer = require(require('path').join(__dirname, 'node_modules', 'puppeteer-core'));
const P = path.join(__dirname, '..', 'Source', 'ui', 'public', 'index.html');
const MUT = process.env.BG_MUT || '';
let pass = 0, fail = 0;
const chk = (ok, label, detail) => { ok ? ++pass : ++fail; console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${label}\n        ${detail}`); };

(async () => {
  console.log('══ fb633 BODE GATE — the live layer, the node, the streams, the canvas ══   mutation: ' + (MUT || '(none)'));
  const b = await puppeteer.launch({ executablePath: (process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width: 1560, height: 1200, deviceScaleFactor: 2 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await pg.evaluateOnNewDocument(() => {
    window.__PMAP = {};
    const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
      getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
      valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
    window.Juce = { getSliderState: mk, getToggleState: mk, getComboBoxState: mk,
      getNativeFunction: (n) => (...a) => new Promise(r => { if (/getPresets/i.test(n)) return r('[]');
        if (/Json|JSON/.test(n)) return r('{}'); if (n === 'getSynParam') return r((window.__PMAP[a[0]] != null) ? window.__PMAP[a[0]] : 0); r(0); }),
      backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
    (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable: true,
      get(){ return held; }, set(v){ held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction }); } }); })();
    window.__JUCE__ = { backend: window.Juce.backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '',
      __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  });
  await pg.goto('file://' + P, { waitUntil: 'load', timeout: 60000 });
  await new Promise(r => setTimeout(r, 1600));
  await pg.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display = 'block'; window.dispatchEvent(new Event('resize')); });
  await new Promise(r => setTimeout(r, 1200));
  const wait = ms => new Promise(r => setTimeout(r, ms));

  // ── the card, the feed, the probes ────────────────────────────────────────────────────────
  const boot = await pg.evaluate(() => {
    try { if (!document.querySelector('.fxr-core[data-core="bod"]')) window.__fxAdd('bod'); } catch (e) { return { err: String(e) }; }
    const core = document.querySelector('.fxr-core[data-core="bod"]'); if (!core) return { err: 'no bode core' };
    const FFT = 4096, SR = 48000, NB = FFT / 2;
    // the real wire: a Hann main lobe per partial, full scale 0.25 (PluginEditor.cpp 1/FFT_SIZE), four
    // decimals, a bare 0 for silence — the APF lambda, mirrored
    const apf = v => (v < 0.00005 ? 0 : +v.toFixed(4));
    window.__G = { core, SR,
      feed(parts) { const post = new Array(NB).fill(0);
        for (const [f, dB] of parts) { const bb = f * FFT / SR, A = 0.25 * Math.pow(10, dB / 20);
          for (let k = Math.floor(bb) - 3; k <= Math.ceil(bb) + 3; k++) { if (k < 0 || k >= NB) continue;
            const d = Math.abs(k - bb), w = d <= 2 ? 0.5 * (1 + Math.cos(Math.PI * d / 2)) : 0; post[k] = Math.max(post[k], A * w); } }
        for (let i = 0; i < NB; i++) post[i] = apf(post[i]);
        window.__terrainEqAnalyzer({ pre: post.slice(), post, sr: SR }); },
      cv() { return window.__G.core.querySelector('canvas.fx-spec'); },
      ink() { const cv = this.cv(); if (!cv || !cv.width) return { tot: 0, purple: 0, white: 0, ladder: 0, railBlobs: 0, leftPurple: 0, w: 0, h: 0 };
        const W = cv.width, H = cv.height, d = cv.getContext('2d').getImageData(0, 0, W, H).data;
        let tot = 0, purple = 0, white = 0, leftPurple = 0, ladder = 0;
        const isP = i => d[i + 3] > 8 && d[i + 2] > d[i] + 25, isW = i => d[i + 3] > 8 && Math.abs(d[i] - d[i + 2]) < 12 && d[i] > 120;
        const yLad = Math.round(H * 0.3);   // the ladder's rungs live in the lower half; the node (also white) sits on the rail above
        for (let i = 0; i < d.length; i += 4) { if (d[i + 3] > 8) { tot++; if (isP(i)) purple++; else if (isW(i)) { white++; if (((i >> 2) / W | 0) > yLad) ladder++; } } }
        // the rail band: y = 10/78 of the height ± 4 units; count WHITE runs along the band's centre row
        const sy = H / 78, y0 = Math.round(10 * sy), rows = [y0 - Math.round(2 * sy), y0, y0 + Math.round(2 * sy)];
        let railBlobs = 0; { let inRun = false; for (let x = 0; x < W; x++) { let on = false; for (const y of rows) { if (y < 0 || y >= H) continue; const i = 4 * (y * W + x); if (isW(i)) { on = true; break; } }
          if (on && !inRun) { railBlobs++; inRun = true; } else if (!on) inRun = false; } }
        // the 20 Hz column: x = 6.5/226 of the width, ±2 px — purple ink there is a clamped stream pile
        const sx = W / 226, x20 = Math.round(6.5 * sx); for (let x = Math.max(0, x20 - 2); x <= x20 + 2; x++) for (let y = 0; y < H; y++) if (isP(4 * (y * W + x))) leftPurple++;
        return { tot, purple, white, ladder, railBlobs, leftPurple, w: W, h: H }; },
      dest: window.__fxModDest ? window.__fxModDest('bod', 1, 0) : null,
      dev() { const cards = [...document.querySelectorAll('.fxr-dev')]; const i = cards.findIndex(c => c.querySelector('.fxr-core[data-core="bod"]')); return (window.__fxrDevs ? window.__fxrDevs()[i] : null); },
      tick(n, lfo) { for (let k = 0; k < n; k++) { if (lfo) { window.__fxModEff = {}; window.__fxModEff[this.dest] = 0.5 + 0.45 * Math.sin(2 * Math.PI * 0.5 * (k / 60)); } window.__fx4Tick(); } },
      cx() { const n = this.core.querySelector('.bod-n'); return n ? +n.getAttribute('cx') : NaN; },
      svgPaints() { const n = this.core.querySelector('.bod-n'), s = this.core.querySelector('.bod-span'); const o = e => e ? +getComputedStyle(e).opacity : -1; return { node: o(n), span: o(s) }; },
    };
    const r = core.getBoundingClientRect();
    return { rect: [Math.round(r.width), Math.round(r.height)], dest: window.__G.dest, fxAdd: typeof window.__fxAdd, tick: typeof window.__fx4Tick };
  });
  if (boot.err) { console.log('  CRASH ' + boot.err); await b.close(); process.exit(2); }
  console.log('  card: core ' + boot.rect.join('×') + ' · Shift dest ' + boot.dest);

  // ── [1] a tone is not silence ─────────────────────────────────────────────────────────────
  const lv = await pg.evaluate(() => { const G = window.__G, out = {};
    G.feed([[1000, -40]]); G.tick(60); out.quiet = G.ink();
    G.feed([[1000, -70]]); G.tick(80); out.silent = G.ink();
    G.feed([[1000, -40]]); G.tick(60); out.back = G.ink();
    return out; });
  const wantQuietDrawn = MUT !== 'mean';
  chk((lv.quiet.purple > 150) === wantQuietDrawn && (wantQuietDrawn ? lv.quiet.ladder < 40 : true) && lv.silent.purple < 20 && lv.silent.ladder > 100 && lv.back.purple > 150,
    '[1] A TONE IS NOT SILENCE — a 1 kHz sine at -40 dBFS draws the live layer with no ladder; at -70 dBFS the wire is zeros and the ladder stands',
    `-40 dBFS: purple ${lv.quiet.purple} ladder ${lv.quiet.ladder} · -70 dBFS: purple ${lv.silent.purple} ladder ${lv.silent.ladder} · back: purple ${lv.back.purple}` + (wantQuietDrawn ? '' : '   (control: expects -40 dBFS suppressed)'));

  // ── [2] one node, on the canvas ───────────────────────────────────────────────────────────
  const nd = await pg.evaluate(() => { const G = window.__G;
    G.feed([[1000, -40]]); G.tick(300, true); const a = { ink: G.ink(), cx: G.cx(), paints: G.svgPaints() };
    window.__fxModEff = {}; window.__fxModEff[G.dest] = 0.10; G.tick(40, false); const cxLo = G.cx();
    window.__fxModEff = {}; window.__fxModEff[G.dest] = 0.90; G.tick(40, false); const cxHi = G.cx();
    window.__fxModEff = undefined; G.tick(10, false);
    return { a, cxLo, cxHi }; });
  const wantSvgSilent = MUT !== 'svg';
  chk((nd.a.paints.node === 0 && nd.a.paints.span === 0) === wantSvgSilent && nd.a.ink.railBlobs === 1 && Math.abs(nd.cxHi - nd.cxLo) > 50,
    '[2] ONE NODE, ON THE CANVAS — the SVG node and span paint nothing, the rail carries exactly one blob, and .bod-n@cx still tracks the modulation',
    `svg opacity node ${nd.a.paints.node} span ${nd.a.paints.span} · rail blobs ${nd.a.ink.railBlobs} · cx 0.10→${nd.cxLo.toFixed(1)} 0.90→${nd.cxHi.toFixed(1)}` + (wantSvgSilent ? '' : '   (control: expects the SVG to paint)'));

  // ── [3] a throw cannot blank the card ─────────────────────────────────────────────────────
  const th = await pg.evaluate(() => { const G = window.__G;
    G.feed([[1000, -20]]); G.tick(40); const before = G.ink(); const e0 = window.__fx4Errs | 0;
    const SY = window.__fltSpecY; window.__fltSpecY = undefined; G.tick(10); const during = G.ink(); const e1 = window.__fx4Errs | 0;
    window.__fltSpecY = SY; G.tick(5); const after = G.ink();
    return { before, during, after, errs: e1 - e0 }; });
  const wantKept = MUT !== 'blank';
  chk((th.during.tot === th.before.tot && th.during.tot > 0) === wantKept && th.errs === 10 && th.after.tot > 0,
    '[3] A THROW CANNOT BLANK THE CARD — ten sabotaged frames keep the last good picture, every throw is counted, the card draws again on restore',
    `ink before ${th.before.tot} during ${th.during.tot} after ${th.after.tot} · throws counted ${th.errs}` + (wantKept ? '' : '   (control: expects a blank card)'));

  // ── [4] the streams wait for a still shift ────────────────────────────────────────────────
  const st = await pg.evaluate(() => { const G = window.__G, proto = CanvasRenderingContext2D.prototype, orig = proto.fill;
    let fills = 0; proto.fill = function () { fills++; return orig.apply(this, arguments); };
    const per = (n, lfo) => { const h = {}; for (let k = 0; k < n; k++) { fills = 0; if (lfo) { window.__fxModEff = {}; window.__fxModEff[G.dest] = 0.5 + 0.45 * Math.sin(2 * Math.PI * 0.5 * (k / 60)); } window.__fx4Tick(); h[fills] = (h[fills] | 0) + 1; } return h; };
    G.feed([[1000, -20]]);
    per(120, true); const lfo = per(120, true);                      // the first 120 let strA decay, the second 120 are measured
    window.__fxModEff = {}; window.__fxModEff[G.dest] = 0.85; per(120, false); const still = per(60, false);   // a far, still shift: the copies return
    // a FAR shift DOWN with a dead feed: the ladder's streams must not pile at 20 Hz
    const dv = (window.__fxrDevs ? window.__fxrDevs() : []).find(d => d && d.core === 'bod'); if (dv) { dv.knobs[1].v = 0; dv.knobs[0].v = 95; }
    window.__fxModEff = undefined; G.feed([[1000, -90]]); per(150, false); const dead = G.ink();
    if (dv) { dv.knobs[1].v = 100; dv.knobs[0].v = 50; }
    proto.fill = orig; return { lfo, still, dead }; });
  const maxKey = h => Math.max(...Object.keys(h).map(Number)), minKey = h => Math.min(...Object.keys(h).map(Number));
  const wantRest = MUT !== 'jitter';
  chk((maxKey(st.lfo) <= 2) === wantRest && minKey(st.still) >= 5 && st.dead.leftPurple === 0 && st.dead.ladder > 50,
    '[4] THE STREAMS WAIT FOR A STILL SHIFT — under the LFO: live + node only; at rest the ghosts and streams return; a far shift never piles at 20 Hz',
    `fills/frame under LFO ${JSON.stringify(st.lfo)} · still ${JSON.stringify(st.still)} · dead feed far shift: purple at 20 Hz ${st.dead.leftPurple}, ladder ${st.dead.ladder}` + (wantRest ? '' : '   (control: expects the scatter)'));

  // ── [5] a blank canvas is chased at rest ──────────────────────────────────────────────────
  await pg.evaluate(() => { const G = window.__G; window.__fxModEff = undefined; G.feed([[1000, -90]]); G.tick(60); });
  const wipe = async () => { await pg.evaluate(() => { const cv = window.__G.cv(); cv.width = cv.width; }); await wait(2600); return pg.evaluate(() => window.__G.ink().tot); };
  const i1 = await wipe(), i2 = await wipe();
  const wantChase = MUT !== 'once';
  chk(i1 > 0 && (i2 > 0) === wantChase,
    '[5] A BLANK CANVAS IS CHASED AT REST — wiped twice with the clock idle, the watchdog repaints it both times',
    `ink after wipe 1: ${i1} · after wipe 2: ${i2}` + (wantChase ? '' : '   (control: expects the second to stay blank)'));

  chk(errs.length === 0, 'the page ran with no errors', errs.length ? errs.slice(0, 2).join(' | ') : '0 page errors');
  console.log(`\n  ${pass} passed, ${fail} FAILED`);
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('  CRASH ' + (e && e.stack || e)); process.exit(2); });
