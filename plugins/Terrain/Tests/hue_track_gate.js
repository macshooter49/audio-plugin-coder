// ══════════════════════════════════════════════════════════════════════════════════════════════
//  hue_track_gate.js — AN LFO ON HUE MUST MOVE THE PURPLE POSITION LINE.
//
//    NODE_PATH=<scratchpad>/node_modules node hue_track_gate.js [page.html]
//
//  Max: "Whenever I put an LFO on Hue, aka my wavetable position, why isn't it moving? The LFO
//  should be moving the actual visualizer of my wavetable position, my purple line."
//
//  The wavetable engine's waterfall draws its purple frame line at wtWaterfall.wtpos(), and on the
//  WAVETABLE path that reads window.__wtFrameEff — the EFFECTIVE (post-matrix) frame the loudest
//  voice is actually reading, pushed from C++ at 60 Hz (fb457, PluginEditor.cpp:6144). On HARM/
//  Table wtpos() returns EARLY on the RAW parameter (index.html:36588-36590), so nothing the mod
//  matrix does can ever reach the picture.
//
//  THE MEASUREMENT IS THE PICTURE, NOT THE VARIABLE: the purple ink is found by scanning the
//  canvas pixels (purple-400 #B794FF is the only non-grey ink on the surface) and reduced to a
//  centroid x. An LFO is driven for 48 frames with the KNOB HELD STILL, and the spread of that
//  centroid over the sweep is the number.
//
//  THE BARS
//   0  THE PANEL LAID OUT AND THE PURPLE LINE IS REALLY ON THE CANVAS — nothing below is asserted
//      on an empty canvas (the fm_wtpage_gate law: a gate that measures nothing reports 0.00 and
//      calls it perfect).
//   1  🚨 THE LINE FOLLOWS THE MODULATED HUE — centroid x must sweep with the LFO while the knob
//      is held at 0.50. This is the bar that is RED on the shipped page.
//   2  AND IT REALLY IS THE LFO — the x it lands on for a given modulated hue matches the x the
//      same value produces when the KNOB is dragged there (same painter, same purple, no second
//      indicator invented).
//   3  IDLE IS UNCHANGED — with no feed (or -1 = "nothing modulating"), wtpos falls back to the
//      knob exactly as it did, so a hand-turned Hue still moves the line and fb589's own gate
//      (harm_waterfall_gate bar 5) keeps passing.
//   4  THE WAVETABLE PATH IS UNTOUCHED — off Table, wtpos still rides __wtFrameEff.
//
//  REPORTED, NOT GATED: the re-bake storm. hm (harmDisplaySignature) carries HUE, but the bake
//  overwrites rp.hue with the ROW index, so a moving Hue makes the two staleness signatures
//  differ forever and the 16-row additive stack re-bakes at the REBAKE_MS floor. Counted here.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const puppeteer = require ('puppeteer-core');

const PAGE = path.resolve (process.argv[2] ? process.argv[2] : path.join (__dirname, '..', 'Source', 'ui', 'public', 'index.html')   /* fb599 — the real page by default, like every other gate */);

const STUB = () => {
  window.__VALS = {};
  const mk = (id) => ({
    getScaledValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
    setScaledValue(){},
    getNormalisedValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
    setNormalisedValue(v){ window.__VALS[id]=v; },
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.__NATIVE_CALLS = [];
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
    getNativeFunction:(n)=>(...a)=>{ window.__NATIVE_CALLS.push([n].concat(a));
      return new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
        if(/getWaterfallView/i.test(n))return r('{}');
        if(/Json|JSON|getOscWavetable/i.test(n))return r('{}'); r(0);});},
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
};

let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail;
  console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

(async () => {
  const b = await puppeteer.launch ({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport ({ width: 820, height: 656, deviceScaleFactor: 2 });
  await p.evaluateOnNewDocument (STUB);
  await p.goto ('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise (r => setTimeout (r, 1600));
  await p.evaluate (() => { const sp = document.getElementById ('syn-panel');
                            if (sp) sp.style.display = 'block'; window.dispatchEvent (new Event ('resize')); });
  await new Promise (r => setTimeout (r, 2600));

  const MUT = process.env.HUE_TRACK_MUTATE || '';
  const r = await p.evaluate ((MUT) => {
    const out = { err: null, mut: MUT };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      const W = window.wtWaterfall;
      if (! dev) { out.err = 'no osc device'; return out; }
      if (! W)   { out.err = 'no wtWaterfall'; return out; }

      // ── the rig: OSC A on HARM / Table with the waterfall engaged ────────────────────────
      dev.classList.add ('engine-harm');
      const sel = dev.querySelector ('.hm-mode-select');
      if (! sel) { out.err = 'no family select'; return out; }
      const setFam = (i) => { sel.value = String (i); sel.dispatchEvent (new Event ('change', {bubbles:true})); };
      setFam (6);
      W.on['a'] = false; dev.classList.remove ('wt3d');
      W.toggle ('a');
      out.isHarmTable = W.isHarmTable ('a');

      // the page installs its OWN window.Juce after load, so wtpos reads THIS one (the fb589
      // gate's note). The KNOB is held at 0.50 for the whole sweep — a still hand.
      const KNOB = { 'SYN_OSC_A_HARM_HUE': 0.50, 'SYN_OSC_A_WT_FRAME': 0.11 };
      out.probed = [];
      window.Juce.getSliderState = function (id) {
        out.probed.push (id);
        return { getNormalisedValue: () => (KNOB[id] != null ? KNOB[id] : 0.5),
                 getScaledValue:     () => (KNOB[id] != null ? KNOB[id] : 0.5),
                 setNormalisedValue () {}, setScaledValue () {},
                 valueChangedEvent: { addListener () { return { remove () {} }; }, removeListener () {} } }; };

      // ── a synthetic 16-frame stack, shaped like the C++ payload (n/p/nf/d) ───────────────
      const N = 16, P = 160, D = new Float32Array (N * P);
      for (let f = 0; f < N; ++f)
        for (let i = 0; i < P; ++i)
          D[f * P + i] = 0.9 * Math.sin (2 * Math.PI * (1 + f) * i / (P - 1));
      const TBL = { n:N, p:P, nf:N, d:D, wm:0,wa:0,w2m:0,w2a:0,fs:0,fa:0,sa:0,st:0,bl:0,lo:0,hi:1,fm:0,hm:7.5 };
      W.cache['a'] = TBL; W.prev['a'] = null; W.off['a'] = null;
      window.__wtDisp = [[0,0,0,0,0,0,0,0,0,0,1,0,7.5],[],[],[]];   // hm MATCHES → no rebake churn

      const cv = document.getElementById ('osc-wave-a');
      if (! cv) { out.err = 'no #osc-wave-a'; return out; }

      // ── THE PICTURE: centroid x of the purple ink, in CSS px ─────────────────────────────
      //  purple-400 (#B794FF) is the only non-grey ink on the surface (the stack is white).
      const purpleX = () => {
        const ctx = cv.getContext ('2d');
        const W2 = cv.width, H2 = cv.height;
        if (! W2 || ! H2) return null;
        const im = ctx.getImageData (0, 0, W2, H2).data;
        let sx = 0, sy = 0, n = 0, minx = 1e9, maxx = -1e9;
        for (let y = 0; y < H2; ++y)
          for (let x = 0; x < W2; ++x) {
            const q = (y * W2 + x) * 4, R = im[q], G = im[q+1], B = im[q+2], A = im[q+3];
            if (A > 40 && B - G > 40 && B > 150) { sx += x; sy += y; ++n;
              if (x < minx) minx = x; if (x > maxx) maxx = x; } }
        const dpr = window.DPR || window.devicePixelRatio || 1;
        return n ? { n, cx: sx / n / dpr, cy: sy / n / dpr, minx: minx / dpr, maxx: maxx / dpr } : { n:0 };
      };

      /* ── THE MUTATION SEAMS — a gate that cannot fail is not a gate ──────────────────────
         1  THE PRE-FIX wtpos: the effective read deleted, the raw knob only. This is literally
            the shipped line, re-installed on the fixed page — bar 1 must go red.
         2  THE FEED NEVER ARRIVES (C++ half missing, JS half present): nothing is published.
         3  THE FEED IS ALWAYS -1 (the idle contract mistaken for the live one). */
      const MUT1 = (MUT === '1'), MUT2 = (MUT === '2'), MUT3 = (MUT === '3');
      if (MUT1) W.wtpos = function (o) {
        if (W.isHarmTable (o)) { try { const hs = window.Juce.getSliderState ('SYN_OSC_' + o.toUpperCase() + '_HARM_HUE');
          return hs ? Math.max (0, Math.min (1, hs.getNormalisedValue())) : 0; } catch (e) { return 0; } }
        try { const e2 = window.__wtFrameEff, i = {a:0,b:1,c:2,d:3}[String(o).toLowerCase()];
              if (e2 && i != null && e2[i] >= 0) return Math.max (0, Math.min (1, e2[i])); } catch (e) {}
        try { const ss = window.Juce.getSliderState ('SYN_OSC_' + o.toUpperCase() + '_WT_FRAME');
              return ss ? Math.max (0, Math.min (1, ss.getNormalisedValue())) : 0; } catch (e) { return 0; } };

      // ── THE SWEEP: an LFO on Hue, 48 frames, knob still ──────────────────────────────────
      const FR = 48;
      out.fetchBefore = (window.__NATIVE_CALLS || []).filter (c => c[0] === 'getOscWavetable').length;
      out.sweep = [];
      for (let k = 0; k < FR; ++k) {
        const hue = 0.5 + 0.48 * Math.sin (2 * Math.PI * k / FR);         // the modulated value
        if (MUT2) delete window.__harmHueEff;                              // mutation 2 — no feed at all
        else window.__harmHueEff = [MUT3 ? -1 : hue, -1, -1, -1];          // the C++ 60 Hz push (fb457's twin)
        W.off['a'] = null;                                                // force a clean repaint
        W.draw ('a');
        const m = purpleX();
        out.sweep.push ({ hue: +hue.toFixed (4), pos: +W.wtpos('a').toFixed (4),
                          cx: m.n ? +m.cx.toFixed (2) : null, minx: m.n ? +m.minx.toFixed (2) : null,
                          cy: m.n ? +m.cy.toFixed (2) : null, ink: m.n });
      }
      out.fetchAfter = (window.__NATIVE_CALLS || []).filter (c => c[0] === 'getOscWavetable').length;

      /* ── REPORTED: THE RE-BAKE STORM. harmDisplaySignature (PluginProcessor.h:700-708) carries
            p.hue (twice: directly, and inside p.tableSig), but getOscWavetableJson OVERWRITES
            rp.hue with the ROW index for all 16 rows — so a moving Hue changes the staleness
            signature without changing one sample of the baked stack. shapeSig (the 60 Hz push)
            and cachedSig (the payload) then differ forever, which is the fb467 failure mode:
            not a stale table, a table that re-bakes at the REBAKE_MS floor for good. */
      const fetch0 = (window.__NATIVE_CALLS || []).filter (c => c[0] === 'getOscWavetable').length;
      W.busy['a'] = false; W.lastReq['a'] = 0;
      for (let k = 0; k < FR; ++k) {
        const hue = 0.5 + 0.48 * Math.sin (2 * Math.PI * k / FR);
        window.__wtDisp[0][12] = 7.5 + hue * 13.1;      // hm moves exactly as the shipped C++ makes it move
        W.maybeRebake ('a', k * 16.7);                  // the loop's own call, at 60 fps
        W.busy['a'] = false;                            // the reply lands before the next frame
      }
      out.rebakeStorm = (window.__NATIVE_CALLS || []).filter (c => c[0] === 'getOscWavetable').length - fetch0;
      window.__wtDisp[0][12] = 7.5;

      // ── bar 2: the SAME positions reached by the KNOB, for the same painter ──────────────
      delete window.__harmHueEff;
      out.byKnob = [];
      for (const v of [0.05, 0.35, 0.65, 0.95]) {
        KNOB['SYN_OSC_A_HARM_HUE'] = v;
        W.off['a'] = null; W.draw ('a');
        const m = purpleX();
        out.byKnob.push ({ v, pos: +W.wtpos('a').toFixed(4), cx: m.n ? +m.cx.toFixed(2) : null });
      }
      KNOB['SYN_OSC_A_HARM_HUE'] = 0.50;

      // by the FEED, at the same four values
      out.byFeed = [];
      for (const v of [0.05, 0.35, 0.65, 0.95]) {
        window.__harmHueEff = [v, -1, -1, -1];
        W.off['a'] = null; W.draw ('a');
        const m = purpleX();
        out.byFeed.push ({ v, pos: +W.wtpos('a').toFixed(4), cx: m.n ? +m.cx.toFixed(2) : null });
      }

      // ── bar 3: idle contract — no feed, and a -1 feed, both fall back to the knob ────────
      KNOB['SYN_OSC_A_HARM_HUE'] = 0.83;
      delete window.__harmHueEff;              out.idleNoFeed = W.wtpos ('a');
      window.__harmHueEff = [-1,-1,-1,-1];     out.idleDead   = W.wtpos ('a');
      KNOB['SYN_OSC_A_HARM_HUE'] = 0.50;

      // ── bar 4: THE WAVETABLE ENGINE's own line is untouched ──────────────────────────────
      //  fb599 — this used to select family 1 to get "off Table". Tables-only retired that: the
      //  painter now writes .harm-table for every stored family, so a HARM oscillator is ALWAYS on
      //  Table and setFam(1) left isHarmTable() true — the bar was testing nothing. What it is
      //  really for is that the new __harmHueEff lane must not hijack the WAVETABLE engine's line,
      //  so take the oscillator off the harmonic engine outright, which is the real case.
      const _dev = document.querySelector ('#syn-panel .device.osc');
      const _wasHarm = _dev.classList.contains ('engine-harm');
      _dev.classList.remove ('engine-harm');
      window.__wtFrameEff = [0.42, -1, -1, -1];
      window.__harmHueEff = [0.99, -1, -1, -1];
      out.offTableEff = W.wtpos ('a');
      window.__wtFrameEff = [-1, -1, -1, -1];
      out.offTableKnob = W.wtpos ('a');        // -1 → the WT_FRAME knob (0.11)
      if (_wasHarm) _dev.classList.add ('engine-harm');
      setFam (6);
    } catch (e) { out.err = String (e) + (e && e.stack ? ' | ' + e.stack.split('\n')[1] : ''); }
    return out;
  }, MUT);

  /* ── REPORTED: DOES THE PAINTER EVEN TICK AT REST? ──────────────────────────────────────
     "Fed but never repainted" (the fb591 gesture-clock / fb598 freshness family) is the other
     way this bug could have been built, so it is measured rather than assumed: the waterfall is
     left alone for 600 ms with no input at all and its own rAF loop is counted. */
  await p.evaluate (() => {
    const W = window.wtWaterfall, dev = document.querySelector ('#syn-panel .device.osc');
    dev.classList.add ('engine-harm');
    const sel = dev.querySelector ('.hm-mode-select');
    sel.value = '6'; sel.dispatchEvent (new Event ('change', {bubbles:true}));
    W.on['a'] = true; W.syncClass ('a');
    window.__DRAWS = 0; const d0 = W.draw.bind (W);
    W.draw = function (o) { ++window.__DRAWS; return d0 (o); };
    W.kick();
  });
  await new Promise (r => setTimeout (r, 600));
  const idleDraws = await p.evaluate (() => window.__DRAWS | 0);

  console.log (`\n══ hue_track_gate — an LFO on Hue must move the purple line ══\n   ${PAGE}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }

  const ink = r.sweep.filter (s => s.ink > 0).length;
  const laidOut = r.isHarmTable && ink === r.sweep.length && r.sweep[0].ink > 20;
  gate (laidOut, '[0] THE RIG IS REAL — HARM/Table, waterfall engaged, purple ink on the canvas',
        `isHarmTable=${r.isHarmTable}, frames with purple ink ${ink}/${r.sweep.length}, ink pixels frame 0 = ${r.sweep[0].ink}`);
  if (! laidOut) { console.log ('\n  ❌ degenerate page — not asserting anything on it\n');
                   await b.close(); process.exit (1); }

  const cxs = r.sweep.map (s => s.cx), mins = r.sweep.map (s => s.minx);
  const spread    = Math.max (...cxs)  - Math.min (...cxs);
  const spreadMin = Math.max (...mins) - Math.min (...mins);
  const posSpread = Math.max (...r.sweep.map (s => s.pos)) - Math.min (...r.sweep.map (s => s.pos));
  gate (spread > 8 && spreadMin > 20,
        '[1] 🚨 THE PURPLE LINE FOLLOWS THE MODULATED HUE (knob held at 0.50)',
        `hue swept ${r.sweep[0].hue}..${Math.max(...r.sweep.map(s=>s.hue))} → wtpos spread ${posSpread.toFixed(4)}; `
      + `purple centroid x spread ${spread.toFixed(2)} px, left-edge x spread ${spreadMin.toFixed(2)} px `
      + `(need > 8 / > 20)`);

  const dx = r.byKnob.map ((k, i) => Math.abs (k.cx - r.byFeed[i].cx));
  gate (dx.every (d => d < 0.75),
        '[2] IT IS THE SAME INDICATOR — the feed lands where the knob lands',
        r.byKnob.map ((k, i) => `${k.v}: knob x=${k.cx} feed x=${r.byFeed[i].cx}`).join (' · ')
      + ` — max |Δ| ${Math.max (...dx).toFixed (2)} px`);

  gate (Math.abs (r.idleNoFeed - 0.83) < 1e-6 && Math.abs (r.idleDead - 0.83) < 1e-6,
        '[3] IDLE IS UNCHANGED — no feed, or a -1 feed, falls back to the knob',
        `knob 0.83 → wtpos with no feed = ${r.idleNoFeed}, with a -1 feed = ${r.idleDead}`);

  gate (Math.abs (r.offTableEff - 0.42) < 1e-6 && Math.abs (r.offTableKnob - 0.11) < 1e-6,
        '[4] THE WAVETABLE ENGINE IS UNTOUCHED — its line still rides __wtFrameEff, never the hue lane',
        `__wtFrameEff 0.42 → ${r.offTableEff} (a 0.99 hue feed ignored); -1 → the WT Frame knob ${r.offTableKnob}`);

  console.log (`\n  — reported, not gated —`);
  console.log (`    re-bakes during the 48-frame sweep with hm HELD STILL: ${r.fetchAfter - r.fetchBefore} getOscWavetable calls`);
  console.log (`    re-bakes over 48 frames (0.8 s) with hm MOVING as the shipped signature moves it: `
             + `${r.rebakeStorm} — the 16-row additive bake, at the 16 ms floor, for a stack the Hue cannot change`);
  console.log (`    the painter at rest: ${idleDraws} draw() calls in 600 ms with no input `
             + `(${(idleDraws/0.6).toFixed(0)}/s) — the waterfall's own rAF loop, so "it is never repainted" is NOT the bug`);
  console.log (`    first/mid/last frame: ` + [0, 12, 24].map (i =>
      `hue ${r.sweep[i].hue} → pos ${r.sweep[i].pos}, x ${r.sweep[i].cx}, y ${r.sweep[i].cy}`).join ('  |  '));

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail === 0 ? 0 : 1);
})();
