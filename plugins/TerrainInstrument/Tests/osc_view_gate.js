// ══════════════════════════════════════════════════════════════════════════════════════════════
//  osc_view_gate.js — fb590: EVERY ENGINE KEEPS ITS OWN PICTURE, AND THE PICTURE NEVER LEAVES.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/osc_view_gate.js [page.html]
//    (pass the pre-fb590 page as the argument for the MUTATION run — bars 1 and 5 must go red)
//
//  Max: "I'm on sample and I'm on granular, and I still see a wave table... I want the visualizer
//  to stay the same in each engine category... Unison needs to be linked to their respective
//  engine icons, not the wave table."  and  "the resynth visualizer popping in and popping out...
//  it should just be static like everything else. like the LFO doesn't disappear."
//
//  THE BARS
//   0  THE PANEL ACTUALLY LAID OUT — nothing below is asserted on a page that never rendered
//   1  THE UNISON PAGE KEEPS THE ENGINE'S PICTURE — for Sample, Granular, Resynth and Modal the
//      .sample-view is shown, the wavetable .osc-display is not, and the picture sits ABOVE the
//      unison knob row (a flex `order` question, not just a visibility one)
//   2  AND THE WAVETABLE / FM ENGINES ARE UNCHANGED — they legitimately own .osc-display there
//   3  AND HARMONIC IS UNCHANGED — it was the one engine that already survived, via .harm-view
//   4  THE WATERFALL REFUSES A SAMPLE-FAMILY ENGINE IN THE LOOP, not just in the click. A flag
//      set while the osc was Wavetable/FM used to survive an engine change and redraw the moment
//      anything made the canvas visible again.
//   5  🚨 THE RESYNTH PICTURE SURVIVES A STALLED FEED. The feed stops whenever nothing moves (the
//      editor idle-skips byte-identical frames), and the 1500 ms watchdog used to WIPE — which is
//      why the picture looked MIDI-driven: playing was the only thing holding it up.
//   6  AND THE PARKED PICTURE STOPS COSTING FRAMES — a still image must not repaint at 60 fps.
//      Measured by counting clearRect calls on that exact canvas.
//   7  SWITCHING TO RESYNTH RE-REQUESTS ITS PAYLOAD — without it the view stays :not(.loaded) and
//      "Drop sample here" sits under the spectrogram forever.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const puppeteer = require ('puppeteer-core');

const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');

const STUB = () => {
  window.__VALS = {};
  // 🚨 ONE CACHED STATE PER PARAMETER ID, AND IT ACTUALLY NOTIFIES. A stub whose
  //    valueChangedEvent.addListener is a no-op silently disables every handler the page hangs
  //    off a parameter — the engine selector's paint() is one of them, so a change event moved
  //    the <select> and toggled NOTHING. That reads as a product bug and is a harness bug.
  const states = {};
  const mk = (id) => states[id] || (states[id] = (function () {
    const L = [];
    const notify = () => L.slice().forEach (f => { try { f (); } catch (e) {} });
    return {
      __listeners: L, __notify: notify,
      getScaledValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
      getNormalisedValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
      setScaledValue(v){ window.__VALS[id]=v; notify(); },
      setNormalisedValue(v){ window.__VALS[id]=v; notify(); },
      getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
      valueChangedEvent:{addListener(f){ L.push(f); return {remove(){ const i=L.indexOf(f); if(i>=0) L.splice(i,1); }}; },
                         removeListener(f){ const i=L.indexOf(f); if(i>=0) L.splice(i,1); }},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}};
  })());
  window.__stateFor = mk;
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
    getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
      if(/getWaterfallView/i.test(n))return r('{}');
      if(/Json|JSON|getOscWavetable|SamplePayload/i.test(n))return r('{}'); r(0);}),
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  // 🚨 AND THE PAGE MUST NOT TAKE THE STATE GETTERS BACK. index.html installs its own window.Juce
  //    after load; keeping only getNativeFunction (what the older gates do) left the PAGE holding
  //    its own slider states, so listeners registered there never saw our setNormalisedValue and
  //    the engine selector never repainted. Preserve the whole state surface so the stub really is
  //    the bridge for the duration of the test.
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction,
      getSliderState:mine.getSliderState,getToggleState:mine.getToggleState,getComboBoxState:mine.getComboBoxState});}});})();
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
  await new Promise (r => setTimeout (r, 2400));

  // ── bars 0-4 + 7 ───────────────────────────────────────────────────────────────────────────
  const r = await p.evaluate (() => {
    const out = { err: null, uni: [], other: [] };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      if (! dev) { out.err = 'no osc device'; return out; }
      const ENG = ['engine-sample','engine-granular','engine-geode','engine-harm','engine-modal','engine-fm'];
      const clearEng = () => ENG.forEach (c => dev.classList.remove (c));
      const sv   = dev.querySelector ('.sample-view');
      const fo   = dev.querySelector ('.front-only');
      const od   = dev.querySelector ('.osc-display');
      const hv   = dev.querySelector ('.harm-view');
      out.devW = dev.getBoundingClientRect().width;
      out.parts = { sv: !!sv, fo: !!fo, od: !!od, hv: !!hv };

      const vis = (el) => !!(el && getComputedStyle (el).display !== 'none' && el.getBoundingClientRect().height > 0);

      // bar 1 — the four sample-family engines on the unison page
      dev.classList.add ('uni-page');
      ['engine-sample','engine-granular','engine-geode','engine-modal'].forEach (c => {
        clearEng (); dev.classList.add (c);
        const svR = sv.getBoundingClientRect(), foR = fo.getBoundingClientRect();
        out.uni.push ({ eng: c, pic: vis (sv), scope: vis (od),
                        picAbove: svR.height > 0 && foR.height > 0 && svR.top < foR.top,
                        svTop: Math.round (svR.top), foTop: Math.round (foR.top) });
      });

      // bar 2/3 — wavetable (no class), FM, and Harmonic on the unison page
      [['wavetable',''], ['engine-fm','engine-fm'], ['engine-harm','engine-harm']].forEach (([nm, c]) => {
        clearEng (); if (c) dev.classList.add (c);
        out.other.push ({ eng: nm, scope: vis (od), pic: vis (sv),
                          harm: !!(hv && getComputedStyle (hv).display !== 'none') });
      });

      // bar 4 — the waterfall loop must consult the engine
      const W = window.wtWaterfall;
      out.wf = { has: !!W, canOn: [], drew: [] };
      if (W) {
        ['engine-sample','engine-granular','engine-geode','engine-modal'].forEach (c => {
          clearEng (); dev.classList.add (c);
          out.wf.canOn.push ({ eng: c, can: W.canWaterfall ('a') });
          // spy: force the flag on (as an engine switch would leave it) and run one loop tick
          W.on['a'] = true;
          const realDraw = W.draw; let n = 0; W.draw = function (o) { if (o === 'a') n++; };
          try { W.loop (); } catch (e) {}
          W.draw = realDraw; W.on['a'] = false;
          out.wf.drew.push ({ eng: c, drew: n });
        });
      }

      // bar 7 — switching TO Resynth must re-request the payload
      clearEng (); dev.classList.remove ('uni-page');
      const seen = []; const real = window.__terrainSampleRedraw;
      window.__terrainSampleRedraw = function (o) { seen.push (o); };
      // Engine index 3 = Resynth. ⚠️ SET THE SCALED VALUE, NOT THE NORMALISED ONE. For a choice
      // parameter the page reads getScaledValue() and treats it as the INDEX (idxNow clamps 0..6),
      // while pick() writes i/6 back. Feeding 3/6 here made the page read index ~1 = Sample, which
      // fired __terrainSampleRedraw from the isSamp branch and would have "passed" this bar for
      // entirely the wrong reason. Index 3 makes isSpec the ONLY branch that can fire it.
      const es = window.__stateFor && window.__stateFor ('SYN_OSC_A_ENGINE');
      out.selFound = !! es;
      if (es) { es.setScaledValue (3); }
      out.engClass = dev.classList.contains ('engine-geode');
      window.__terrainSampleRedraw = real;
      out.redrawOnGeode = seen.slice();
    } catch (e) { out.err = String (e) + (e && e.stack ? ' | ' + e.stack.split('\n')[1] : ''); }
    return out;
  });

  console.log (`\n══ osc_view_gate — fb590 ══  ${PAGE}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }

  const laidOut = r.devW > 100 && r.parts.sv && r.parts.fo && r.parts.od;
  gate (laidOut, '[0] THE PANEL ACTUALLY LAID OUT — the assertions below are real',
        `device ${(r.devW||0).toFixed (1)} px; parts ${JSON.stringify (r.parts)}`);
  if (! laidOut) { console.log ('\n  ❌ degenerate page — not asserting anything on it\n');
                   await b.close(); process.exit (1); }

  const badUni = r.uni.filter (u => ! u.pic || u.scope || ! u.picAbove);
  gate (badUni.length === 0,
        "[1] THE UNISON PAGE KEEPS THE ENGINE'S PICTURE",
        badUni.length ? ('broken: ' + JSON.stringify (badUni))
          : r.uni.map (u => `${u.eng.replace('engine-','')}: picture=${u.pic} wavetable-scope=${u.scope} above(${u.svTop}<${u.foTop})=${u.picAbove}`).join ('  ·  '));

  const wt = r.other.find (o => o.eng === 'wavetable'), fm = r.other.find (o => o.eng === 'engine-fm');
  gate (wt.scope && ! wt.pic && fm.scope && ! fm.pic,
        '[2] AND THE WAVETABLE / FM ENGINES ARE UNCHANGED on the unison page',
        `wavetable scope=${wt.scope} pic=${wt.pic} · fm scope=${fm.scope} pic=${fm.pic}`);

  const hm = r.other.find (o => o.eng === 'engine-harm');
  gate (hm.harm, '[3] AND HARMONIC IS UNCHANGED — still its own bars on the unison page',
        `harm-view shown=${hm.harm}`);

  const wfBad = (r.wf.canOn || []).filter (x => x.can).concat ((r.wf.drew || []).filter (x => x.drew > 0));
  gate (r.wf.has && wfBad.length === 0,
        '[4] THE WATERFALL REFUSES A SAMPLE-FAMILY ENGINE IN THE LOOP, not just the click',
        wfBad.length ? ('leaked: ' + JSON.stringify (wfBad))
          : 'sample/granular/geode/modal: canWaterfall=false and the loop drew 0 frames with on[a] forced true');

  gate (r.selFound && r.engClass && r.redrawOnGeode.length > 0,
        '[7] SWITCHING TO RESYNTH RE-REQUESTS ITS PAYLOAD',
        r.selFound ? `engine-geode class applied=${r.engClass}; __terrainSampleRedraw called for ${JSON.stringify (r.redrawOnGeode)}`
                   : 'SYN_OSC_A_ENGINE state not reachable');

  // ── bar 1b — THE GEOMETRY, driven the way a user drives it ────────────────────────────────
  //  🚨 CLASS-FORCING IS NOT ENOUGH FOR THIS ONE. initSetArrows watches the device's class list and
  //     resets the page index whenever the ENGINE key changes, so adding .uni-page by hand can be
  //     undone a microtask later. Bars above only assert CSS, which class-forcing settles honestly;
  //     LAYOUT has to be measured after the real interaction: set the engine PARAMETER, then click
  //     the page arrow until the unison page is actually up.
  const geo = await p.evaluate (async () => {
    const out = { rows: [], err: null };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      const sleep = (ms) => new Promise (r => setTimeout (r, ms));
      const ENGS = [['wavetable',0],['sample',1],['granular',2],['geode',3],['fm',4],['harm',5],['modal',6]];
      for (const [nm, idx] of ENGS) {
        window.__stateFor ('SYN_OSC_A_ENGINE').setScaledValue (idx);
        await sleep (260);
        for (let k = 0; k < 7 && ! dev.classList.contains ('uni-page'); ++k) {
          const a = dev.querySelector ('.uni-arrow, .gk-arrow, .fm-arrow, .harm-arrow, .modal-arrow, .geode-arrow, .bl-arrow');
          if (a) a.dispatchEvent (new MouseEvent ('click', { bubbles: true }));
          await sleep (170);
        }
        const vis = (e) => e && e.offsetParent !== null && getComputedStyle (e).display !== 'none';
        const R = (sel) => { const e = dev.querySelector (sel); if (! vis (e)) return null;
                             const r = e.getBoundingClientRect();
                             return { t: Math.round (r.top), b: Math.round (r.bottom), h: Math.round (r.height) }; };
        const pic = R ('.samp-disp') || R ('.osc-display');
        const row = R ('.uni-knob-wrap');
        out.rows.push ({ eng: nm, uni: dev.classList.contains ('uni-page'),
                         picH: pic ? pic.h : -1, rowT: row ? row.t : -1,
                         gap: (pic && row) ? (row.t - pic.b) : -999 });
      }
    } catch (e) { out.err = String (e); }
    return out;
  });

  if (geo.err) gate (false, '[1b] AND IT LANDS ON THE HOUSE GEOMETRY', 'harness: ' + geo.err);
  else {
    const ref = geo.rows.find (r => r.eng === 'wavetable');
    const off = geo.rows.filter (r => ! r.uni || r.picH !== ref.picH || r.rowT !== ref.rowT || r.gap !== ref.gap);
    gate (!! ref && ref.gap === 7 && off.length === 0,
          '[1b] AND IT LANDS ON THE HOUSE GEOMETRY — all seven engines identical',
          off.length ? ('off-spec: ' + JSON.stringify (off) + '  reference(wavetable)=' + JSON.stringify (ref))
            : `every engine: picture h=${ref.picH} at y=81, unison row y=${ref.rowT}, gap ${ref.gap} px`);
  }

  // ── bars 5 + 6 — the stalled feed ──────────────────────────────────────────────────────────
  const st = await p.evaluate (async () => {
    const out = { err: null };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      ['engine-sample','engine-granular','engine-harm','engine-modal','engine-fm','uni-page']
        .forEach (c => dev.classList.remove (c));
      dev.classList.add ('engine-geode');
      const cv = dev.querySelector ('.sample-view canvas.samp-spec');
      if (! cv) { out.err = 'no samp-spec canvas'; return out; }

      // a synthetic baked image — bakeLayer is synchronous (atob -> ImageData)
      const W = 24, H = 12; let s = '';
      for (let x = 0; x < W; x++) for (let y = 0; y < H; y++) s += String.fromCharCode (40 + ((x * 7 + y * 11) % 200));
      const b64 = btoa (s);
      if (! window.__geodeImage || ! window.__geodeSpectrum) { out.err = 'geode API missing'; return out; }
      window.__geodeImage ('a', 1, b64, b64, W, H);

      // count repaints of THIS canvas only
      let clears = 0;
      const proto = CanvasRenderingContext2D.prototype, realCR = proto.clearRect;
      proto.clearRect = function (...a) { if (this.canvas === cv) clears++; return realCR.apply (this, a); };

      const raf = () => new Promise (r => requestAnimationFrame (r));
      const nonBlank = () => { const c = cv.getContext ('2d');
        const d = c.getImageData (0, 0, cv.width, cv.height).data;
        let n = 0; for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++; return n; };

      // FED: drive the feed for a few frames
      for (let i = 0; i < 6; ++i) { window.__geodeSpectrum ({ a: { on: 1, p: 0.4 } }); await raf (); }
      out.painted = nonBlank ();
      const clearsAfterFed = clears;

      // STALL: stop feeding, and fast-forward the clock past the 1500 ms watchdog
      const realNow = performance.now.bind (performance);
      let skew = 0;
      performance.now = () => realNow () + skew;
      skew = 4000;
      for (let i = 0; i < 8; ++i) await raf ();          // let the watchdog fire and park
      out.survived = nonBlank ();                         // bar 5 — the image must still be there
      const clearsAtPark = clears;
      for (let i = 0; i < 30; ++i) await raf ();          // ~half a second of doing nothing
      out.clearsWhileParked = clears - clearsAtPark;      // bar 6 — must be 0
      out.clearsWhileFed = clearsAfterFed;
      out.stillThere = nonBlank ();

      performance.now = realNow; proto.clearRect = realCR;
    } catch (e) { out.err = String (e); }
    return out;
  });

  if (st.err) { gate (false, '[5] THE RESYNTH PICTURE SURVIVES A STALLED FEED', 'harness: ' + st.err);
                gate (false, '[6] AND THE PARKED PICTURE STOPS COSTING FRAMES', 'harness: ' + st.err); }
  else {
    gate (st.painted > 20 && st.survived > 20 && st.stillThere > 20,
          '[5] THE RESYNTH PICTURE SURVIVES A STALLED FEED',
          `lit pixels: ${st.painted} while fed → ${st.survived} after the 1500 ms watchdog → ${st.stillThere} half a second later`);
    gate (st.clearsWhileFed > 0 && st.clearsWhileParked === 0,
          '[6] AND THE PARKED PICTURE STOPS COSTING FRAMES',
          `repaints of this canvas: ${st.clearsWhileFed} while fed, ${st.clearsWhileParked} over ~30 frames parked`);
  }

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail === 0 ? 0 : 1);
})();
