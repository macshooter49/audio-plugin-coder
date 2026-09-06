// ══════════════════════════════════════════════════════════════════════════════════════════════
//  idle_gesture_gate.js — fb591: THE RACK STAYS USABLE WITH NO MIDI AND NO SOUND.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/idle_gesture_gate.js [page.html]
//    (pass the pre-fb591 page to see bars 2/3/4 go red — the mutation run)
//
//  Max: "I can't drag the LFO, and I can't drag the multi-band areas. Whenever there is no MIDI
//  coming in, this is not how it's supposed to be... What if I want to have it on pause and simply
//  move the LFO or move the EQ? ... I should still be able to use them even if there's no MIDI
//  coming in or sound. It should never have to happen, never."
//
//  WHAT THIS MEASURES, and why it is element-independent: every widget that repaints from the
//  shared clock is a painter in the __tiFrameReg registry, and the registry runs them all in one
//  pass. So a probe painter registered here counts EXACTLY the frames every LFO / EQ / rack canvas
//  gets. No selector, no pixel reading, no guessing which canvas is which.
//
//  THE STATE UNDER TEST is the plugin AT REST: fb581's law is that once the page has heard the C++
//  lane it never invents its own clock again, because silence from a live lane means the plugin is
//  resting. The C++ keepalive stamps the lane alive WITHOUT dispatching, which this harness
//  reproduces exactly (window.__tiAlive on an interval, no __tiFrame).
//
//  THE BARS
//   0  THE LANE IS ALIVE AND THE PAGE IS AT REST — the precondition; without it nothing below means
//      anything, because a page that never heard the lane runs its own fallback clock and would
//      pass every bar for the wrong reason
//   1  AND IT REALLY IS ZERO AT REST — fb581/fb566 preserved: no frames while nobody touches it
//   2  🚨 A DRAG PAINTS WHILE IT LASTS — the bug: wakeBurst is THREE frames (0/90/350 ms) armed by
//      pointerdown, which is right for a click and useless for a drag, so the picture froze while
//      the value moved
//   3  A WHEEL PAINTS — a knob scrubbed by the wheel had NO wake at all before
//   4  A KEY PAINTS — arrow-key nudges likewise
//   5  AND IT STOPS WHEN THE HAND DOES — back to zero after release, or this would be the idle
//      cost fb566/fb567/fb581 exist to remove
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const puppeteer = require ('puppeteer-core');

const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');

const STUB = () => {
  window.__VALS = {};
  const states = {};
  const mk = (id) => states[id] || (states[id] = (function () {
    const L = []; const n = () => L.slice().forEach (f => { try { f (); } catch (e) {} });
    return { getScaledValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
      getNormalisedValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
      setScaledValue(v){ window.__VALS[id]=v; n(); }, setNormalisedValue(v){ window.__VALS[id]=v; n(); },
      getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
      valueChangedEvent:{addListener(f){L.push(f);return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}}; })());
  window.__stateFor = mk;
  window.Juce = { getSliderState:mk, getToggleState:mk, getComboBoxState:mk,
    getNativeFunction:(nm)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(nm))return r('[]');
      if(/getWaterfallView/i.test(nm))return r('{}');
      if(/Json|JSON|getOscWavetable|SamplePayload/i.test(nm))return r('{}'); r(0); }),
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}} };
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
  await p.setViewport ({ width: 1280, height: 900, deviceScaleFactor: 1 });
  await p.evaluateOnNewDocument (STUB);
  await p.goto ('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise (r => setTimeout (r, 2000));
  await p.evaluate (() => { const sp = document.getElementById ('syn-panel');
                            if (sp) sp.style.display = 'block'; window.dispatchEvent (new Event ('resize')); });
  await new Promise (r => setTimeout (r, 2200));

  // Become the plugin AT REST: one real dispatch so the page knows a lane exists (fb581), then
  // only the keepalive — which is exactly what the editor does while it idle-skips.
  await p.evaluate (() => {
    window.__PROBE = 0;
    window.__tiFrameReg ('__probe__', function () { window.__PROBE++; });
    try { window.__tiFrame && window.__tiFrame(); } catch (e) {}
    window.__KA = setInterval (() => { try { window.__tickT = Date.now(); window.__tiAlive && window.__tiAlive(); } catch (e) {} }, 500);
  });
  await new Promise (r => setTimeout (r, 3000));

  const pre = await p.evaluate (() => ({ laneSeen: !!(window.__tiLaneSeen && window.__tiLaneSeen()),
                                         hasReg: typeof window.__tiFrameReg === 'function' }));
  const count = async (ms, fn) => {
    await p.evaluate (() => { window.__PROBE = 0; });
    if (fn) await fn ();
    await new Promise (r => setTimeout (r, ms));
    return await p.evaluate (() => window.__PROBE);
  };

  const restFrames = await count (1000);
  console.log (`\n══ idle_gesture_gate — fb591 ══  ${PAGE}\n`);
  gate (pre.hasReg && pre.laneSeen,
        '[0] THE LANE IS ALIVE AND THE PAGE IS AT REST — the precondition',
        `registry present=${pre.hasReg}, __tiLaneSeen()=${pre.laneSeen} (false here would mean the page is running its OWN fallback clock and every bar below would pass for the wrong reason)`);
  gate (restFrames === 0, '[1] AND IT REALLY IS ZERO AT REST — fb581 / fb566 preserved',
        `${restFrames} painter frames in 1 s with nobody touching it`);

  // ── bar 2 — a 600 ms DRAG, the way a hand does it ─────────────────────────────────────────
  const dragFrames = await count (100, async () => {
    await p.mouse.move (300, 400); await p.mouse.down();
    for (let i = 1; i <= 12; ++i) { await p.mouse.move (300 + i * 6, 400 - i * 4); await new Promise (r => setTimeout (r, 45)); }
    await p.mouse.up();
  });
  gate (dragFrames >= 15,
        '[2] A DRAG PAINTS WHILE IT LASTS',
        `${dragFrames} painter frames across a ~600 ms drag  (the old gesture burst is THREE: 0 / 90 / 350 ms)`);

  await new Promise (r => setTimeout (r, 1200));
  const wheelFrames = await count (500, async () => {
    await p.mouse.move (300, 400);
    for (let i = 0; i < 6; ++i) { await p.mouse.wheel ({ deltaY: -40 }); await new Promise (r => setTimeout (r, 30)); }
  });
  gate (wheelFrames >= 5, '[3] A WHEEL PAINTS — a knob scrubbed by the wheel had no wake at all',
        `${wheelFrames} painter frames across a ~200 ms wheel gesture`);

  await new Promise (r => setTimeout (r, 1200));
  const keyFrames = await count (500, async () => {
    await p.keyboard.down ('ArrowUp'); await new Promise (r => setTimeout (r, 60)); await p.keyboard.up ('ArrowUp');
  });
  gate (keyFrames >= 3, '[4] A KEY PAINTS — arrow-key nudges likewise',
        `${keyFrames} painter frames after a key press`);

  // ── bar 5 — and it stops when the hand does ───────────────────────────────────────────────
  await new Promise (r => setTimeout (r, 1600));
  const afterFrames = await count (1200);
  gate (afterFrames === 0, '[5] AND IT STOPS WHEN THE HAND DOES — the idle cost stays zero',
        `${afterFrames} painter frames in 1.2 s after the last gesture settled`);

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail === 0 ? 0 : 1);
})();
