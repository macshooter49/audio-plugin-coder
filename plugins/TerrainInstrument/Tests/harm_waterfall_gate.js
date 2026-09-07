// ══════════════════════════════════════════════════════════════════════════════════════════════
//  harm_waterfall_gate.js — fb589: BARS STAY PRIMARY; THE WATERFALL APPEARS ON THE TABLE FAMILY.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/harm_waterfall_gate.js [page.html]
//
//  Max: "I think the additive mode should only be waterfall or something. Let's talk about it."
//  Settled: the BARS are the additive engine's native picture and stay primary; the WATERFALL
//  appears where there is actually a table axis to scan — the Table family.
//
//  THE BARS
//   0  THE PANEL ACTUALLY LAID OUT — nothing below is asserted on a page that never rendered
//   1  BARS ARE PRIMARY — on Table with the view not engaged, the bars are what you see
//   2  THE TABLE FAMILY CAN ENGAGE IT — a click swaps to the wavetable canvas and hides the bars
//   3  🚨 THE SIX PROCEDURAL FAMILIES REFUSE THE CLICK. This is a REGRESSION bar, not a new
//      feature: before fb589 the toggle had no engine guard, so clicking a Harmonic display
//      flipped wtWaterfall.on AND persisted wt3dView_ through setWaterfallView while .osc-wave
//      was display:none — nothing drew and the saved view silently rotted. The same was true of
//      Sample / Granular / Resynth / Modal.
//   4  LEAVING Table RETURNS THE BARS — and returning to Table brings the waterfall back, because
//      state persists and nothing turns off by itself
//   5  THE DEPTH AXIS FOLLOWS HUE — the additive bank has no WT Frame knob; HUE selects the frame
//   6  🚨 THE SIGNATURE CONVERGES. cachedSig and __wtDisp are POSITIONAL. fb589 added a 13th slot
//      (hm). If the two sides disagree the table does not go stale — it re-bakes at the maximum
//      rate forever (the fb467 note in index.html says exactly this), which is a CPU fire that
//      looks like everything working.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const puppeteer = require ('puppeteer-core');

const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');

// the JUCE bridge stub, lifted from fm_wtpage_gate.js, with a per-id value map so the HUE-vs-
// WT_FRAME question in bar 5 can actually be told apart.
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
  await new Promise (r => setTimeout (r, 2600));   // the glass/canvas binders run on a 200/800/2000ms ladder

  const r = await p.evaluate (() => {
    const out = { err: null };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      const W = window.wtWaterfall;
      if (! dev) { out.err = 'no osc device'; return out; }
      if (! W)   { out.err = 'no wtWaterfall'; return out; }
      dev.classList.add ('engine-harm');
      const sel = dev.querySelector ('.hm-mode-select');
      if (! sel) { out.err = 'no family select'; return out; }
      const wave = dev.querySelector ('.osc-wave'), bars = dev.querySelector ('.harm-view');
      out.devW = dev.getBoundingClientRect().width;
      out.hasCanvas = !! document.getElementById ('osc-wave-a');

      const setFam = (i) => { sel.value = String (i); sel.dispatchEvent (new Event ('change', {bubbles:true})); };
      const shown = () => ({ wave: getComputedStyle (wave).display !== 'none',
                             bars: getComputedStyle (bars).display !== 'none',
                             wt3d: dev.classList.contains ('wt3d'), on: !! W.on['a'] });

      W.on['a'] = false; dev.classList.remove ('wt3d');
      setFam (6);
      out.tableBarsFirst = shown();                       // bar 1 — bars primary on Table

      W.toggle ('a');
      out.tableEngaged = shown();                         // bar 2 — Table can engage it

      // bar 3 — the six procedural families must REFUSE the click
      out.refused = [];
      for (let i = 0; i < 6; ++i) {
        setFam (i);
        W.on['a'] = false; dev.classList.remove ('wt3d');
        const before = !! W.on['a'];
        const nCalls = window.__NATIVE_CALLS.filter (c => c[0] === 'setWaterfallView').length;
        W.toggle ('a');
        const after = !! W.on['a'];
        const nCalls2 = window.__NATIVE_CALLS.filter (c => c[0] === 'setWaterfallView').length;
        out.refused.push ({ fam: i, changed: before !== after, persisted: nCalls2 > nCalls,
                            canW: W.canWaterfall ('a') });
      }

      // bar 4 — leave Table with the view engaged, bars must come back; return, waterfall returns
      setFam (6); W.on['a'] = false; dev.classList.remove ('wt3d'); W.toggle ('a');
      out.onTableEngaged = shown();
      setFam (2);                                          // leave Table (Console)
      out.leftTable = shown();
      setFam (6);                                          // come back
      out.returned = shown();

      // bar 5 — the depth axis is HUE on Table, WT_FRAME otherwise.
      // ⚠️ The PAGE installs its own window.Juce after load, so the evaluateOnNewDocument stub is
      //    no longer the one wtpos reads. Override getSliderState here, at assert time — the
      //    question this bar asks is purely "which parameter id does wtpos consult", so feeding
      //    those two ids distinguishable values is exactly the right probe.
      const VALS = { 'SYN_OSC_A_HARM_HUE': 0.83, 'SYN_OSC_A_WT_FRAME': 0.11 };
      out.probedIds = [];
      window.Juce.getSliderState = function (id) {
        out.probedIds.push (id);
        return { getNormalisedValue: () => (VALS[id] != null ? VALS[id] : 0.5),
                 getScaledValue:     () => (VALS[id] != null ? VALS[id] : 0.5),
                 setNormalisedValue () {}, setScaledValue () {},
                 valueChangedEvent: { addListener () { return { remove () {} }; }, removeListener () {} } }; };
      try { window.__wtFrameEff = [-1,-1,-1,-1]; } catch (e) {}
      setFam (6);
      out.probedIds = [];
      out.posOnTable  = W.wtpos ('a');
      out.idsOnTable  = out.probedIds.slice();
      setFam (1);
      out.probedIds = [];
      out.posOffTable = W.wtpos ('a');
      out.idsOffTable = out.probedIds.slice();

      // bar 6 — the positional signature must CONVERGE, not differ forever
      const payload = { wm:1, wa:0.2, w2m:2, w2a:0.3, fs:1, fa:0.4, sa:0.5, st:1,
                        bl:0.6, lo:0.1, hi:0.9, fm:1.25, hm:7.5 };
      W.cache['a'] = payload;
      window.__wtDisp = [[1,0.2,2,0.3,1,0.4,0.5,1,0.6,0.1,0.9,1.25,7.5],[],[],[]];
      out.shapeSig  = W.shapeSig ('a');
      out.cachedSig = W.cachedSig ('a');
      // and prove the bar bites: drop the 13th and they must diverge
      window.__wtDisp[0] = [1,0.2,2,0.3,1,0.4,0.5,1,0.6,0.1,0.9,1.25];
      out.shapeSigShort = W.shapeSig ('a');
    } catch (e) { out.err = String (e) + (e && e.stack ? ' | ' + e.stack.split('\n')[1] : ''); }
    return out;
  });

  console.log (`\n══ harm_waterfall_gate — fb589 ══  ${PAGE}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }

  const laidOut = r.devW > 100 && r.hasCanvas;
  gate (laidOut, '[0] THE PANEL ACTUALLY LAID OUT — the assertions below are real',
        `osc device width ${(r.devW||0).toFixed(1)} px, #osc-wave-a present=${r.hasCanvas}`);
  if (! laidOut) { console.log ('\n  ❌ degenerate page — not asserting anything on it\n');
                   await b.close(); process.exit (1); }

  const t1 = r.tableBarsFirst;
  gate (t1.bars && ! t1.wave,
        '[1] BARS ARE PRIMARY — Table with the view not engaged shows the bars',
        `bars=${t1.bars} waterfall canvas=${t1.wave} wt3d=${t1.wt3d}`);

  const t2 = r.tableEngaged;
  gate (t2.wave && ! t2.bars && t2.wt3d && t2.on,
        '[2] THE TABLE FAMILY CAN ENGAGE IT',
        `after one click: waterfall=${t2.wave} bars=${t2.bars} wt3d class=${t2.wt3d} on=${t2.on}`);

  /* fb599 — INVERTED ON PURPOSE. Max: "I only want to use tables for this ... I don't want the
     families anymore." The gather pins mainMode = 6 for every HARM oscillator, so there is no
     longer such a thing as a procedural family at run time: whatever index is STORED, the engine
     is on Table and the waterfall is its view. The old bar asserted the opposite and is exactly
     what tables-only retires. What must still hold is that a stored index cannot take the
     waterfall AWAY — a patch saved on Blade gets the same picture as one saved on Table. */
  const bad = r.refused.filter (f => ! f.canW);
  gate (bad.length === 0,
        '[3] EVERY STORED FAMILY GETS THE WATERFALL — tables-only, whatever the patch saved',
        bad.length ? ('refused: ' + JSON.stringify (bad))
                   : 'stored families 0-5 all report canWaterfall=true — the engine is on Table regardless');

  /* fb599 — there is no "leaving Table" any more, so the bar becomes its opposite: selecting a
     retired family must NOT drop the waterfall or bring the bars back. */
  gate (r.onTableEngaged.wave && r.leftTable.wave && ! r.leftTable.bars && r.returned.wave,
        '[4] A RETIRED FAMILY DOES NOT TAKE THE WATERFALL AWAY — the view never falls back to the bars',
        `on Table waterfall=${r.onTableEngaged.wave} → stored Console waterfall=${r.leftTable.wave}/bars=${r.leftTable.bars} → back ${r.returned.wave} (on stayed ${r.returned.on})`);

  /* fb599 — HUE on every stored family, because every one of them IS Table now. The additive bank
     has no WT Frame knob; reading SYN_OSC_x_WT_FRAME on a HARM oscillator would be the bug. */
  gate (Math.abs (r.posOnTable - 0.83) < 1e-6 && Math.abs (r.posOffTable - 0.83) < 1e-6,
        '[5] THE DEPTH AXIS FOLLOWS HUE — on every stored family, tables-only',
        `HUE=0.83 WT_FRAME=0.11 → stored Table wtpos=${r.posOnTable} (read ${(r.idsOnTable||[]).join(',') || 'nothing'}), stored Console wtpos=${r.posOffTable} (read ${(r.idsOffTable||[]).join(',') || 'nothing'})`);

  gate (r.shapeSig === r.cachedSig && r.shapeSig !== '' && r.shapeSigShort !== r.cachedSig,
        '[6] THE SIGNATURE CONVERGES — 13 slots, both sides, same order',
        `matched=${r.shapeSig === r.cachedSig} (13 slots); with the 13th dropped they diverge=${r.shapeSigShort !== r.cachedSig}`);

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail === 0 ? 0 : 1);
})();
