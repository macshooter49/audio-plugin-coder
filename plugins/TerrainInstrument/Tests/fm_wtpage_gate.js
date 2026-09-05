// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fm_wtpage_gate.js — fb586. THE WAVETABLE SET IS FM'S THIRD PAGE, AND IT IS FOUR, CENTRED.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/fm_wtpage_gate.js [page.html]
//
//  Max: "the four buttons need to be aligned correctly... make sure that the four buttons are
//  aligned symmetrically evenly in their center and not there's one missing space onto the right
//  or the left no missing spaces. They gotta be centered and filled out that space."
//
//  That sentence is three separate measurable claims, and this gate is all three:
//    · FOUR knobs, not five — Feedback is NOT here (the FM engine already owns a Feedback knob
//      on its first page, and two on one engine is the duplicate this project keeps killing)
//    · EVENLY spaced — the gaps between adjacent knob centres are equal
//    · CENTRED — the row's midpoint sits on the container's midpoint, so there is no dead column
//      hanging off either end
//
//  🚨 AND THE FIFTH BAR IS THE ONE THAT IS NOT ABOUT LAYOUT AT ALL. The four are the SAME DOM
//     elements the wavetable engine uses, not copies, because knobs auto-bind through
//     querySelectorAll('#syn-panel .knob[data-syn]') and the mod-matrix lookup keeps only the LAST
//     match — so a duplicated data-syn would put the ring and the mark on one copy and not the
//     other. This asserts there is exactly ONE element per parameter in the whole panel.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const puppeteer = require ('puppeteer-core');

const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');

// the JUCE bridge stub, lifted from probe_labelgap.js — without it the panel never lays out
// and every measurement below reads 0.00, which a naive gate happily calls "perfectly even".
const STUB = () => {
  window.__PMAP = {};
  const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
    getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
      if(/Json|JSON/.test(n))return r('{}'); r(0);}),
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
  await new Promise (r => setTimeout (r, 1000));

  const r = await p.evaluate (() => {
    const out = { err: null };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      if (! dev) { out.err = 'no osc device'; return out; }

      // put this oscillator on FM, then step to its THIRD set the way a click would
      dev.classList.add ('engine-fm');
      const arrow = dev.querySelector ('.fm-arrow') || dev.querySelector ('.uni-arrow');
      out.hasArrow = !! arrow;
      for (let i = 0; i < 2; ++i) if (arrow) arrow.click();
      out.setIdx  = dev.dataset.setIdx;
      out.wtPage  = dev.classList.contains ('fm-wtpage');

      const wrap = dev.querySelector ('.wt-knob-wrap');
      const row  = wrap && wrap.querySelector ('.osc-knobs.wt-pg1');
      out.wrapShown = !! (wrap && getComputedStyle (wrap).display !== 'none');
      out.fmShown   = !! (dev.querySelector ('.fm-knob-wrap')
                          && getComputedStyle (dev.querySelector ('.fm-knob-wrap')).display !== 'none');

      const knobs = row ? [...row.querySelectorAll ('.knob[data-syn]')] : [];
      out.visible = knobs.filter (k => getComputedStyle (k).display !== 'none')
                         .map (k => ({ syn: k.getAttribute ('data-syn'),
                                       label: (k.querySelector ('.knob-label') || {}).textContent || '',
                                       c: k.getBoundingClientRect().left + k.getBoundingClientRect().width / 2 }));
      out.hidden  = knobs.filter (k => getComputedStyle (k).display === 'none')
                         .map (k => k.getAttribute ('data-syn'));
      if (row) { const rb = row.getBoundingClientRect(); out.rowMid = rb.left + rb.width / 2; out.rowW = rb.width; }

      // one element per parameter, panel-wide
      const seen = {}; out.dupes = [];
      document.querySelectorAll ('#syn-panel .knob[data-syn]').forEach (k => {
        const s = k.getAttribute ('data-syn'); seen[s] = (seen[s] || 0) + 1; });
      Object.keys (seen).forEach (k => { if (seen[k] > 1) out.dupes.push (k + ' x' + seen[k]); });
    } catch (e) { out.err = String (e); }
    return out;
  });

  console.log (`\n══ fm_wtpage_gate — fb586 ══  ${PAGE}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }

  gate (r.wtPage && r.wrapShown && ! r.fmShown,
        '[1] FM\'s THIRD set IS the wavetable set',
        `setIdx=${r.setIdx} fm-wtpage=${r.wtPage} wt shown=${r.wrapShown} fm wrap shown=${r.fmShown}`);

  const labels = r.visible.map (v => v.label).join (' · ');
  gate (r.visible.length === 4,
        '[2] FOUR knobs, not five — Feedback is not duplicated onto FM',
        `${r.visible.length} visible: ${labels}   hidden: ${r.hidden.join (',') || '(none)'}`);

  // 🚨 A GATE THAT PASSES ON ZEROS IS WORSE THAN NO GATE. The first run of this file reported
  //    "gaps 0.00 / 0.00 / 0.00, spread 0.00" and called it perfectly even — the panel had simply
  //    never laid out, so every rect was empty. Nothing below is allowed to run until the geometry
  //    is real.
  const laidOut = r.rowW > 50 && r.visible.every (v => v.c > 0);
  gate (laidOut, '[0] THE PANEL ACTUALLY LAID OUT — the measurements below are real',
        `row width ${(r.rowW || 0).toFixed (1)} px, knob centres ${r.visible.map (v => v.c.toFixed (0)).join (',')}`);
  if (! laidOut) { console.log (`\n  ❌ geometry is degenerate — the layout bars cannot be trusted, not asserting them\n`);
                   await b.close(); process.exit (1); }

  const cs = r.visible.map (v => v.c);
  const gaps = cs.slice (1).map ((c, i) => c - cs[i]);
  const gmin = Math.min (...gaps), gmax = Math.max (...gaps);
  gate (gaps.length === 3 && (gmax - gmin) <= 1.0,
        '[3] EVENLY spaced — equal gaps between adjacent knobs',
        `gaps ${gaps.map (g => g.toFixed (2)).join (' / ')} px   spread ${(gmax - gmin).toFixed (2)} px`);

  const mid = (cs[0] + cs[cs.length - 1]) / 2;
  gate (Math.abs (mid - r.rowMid) <= 1.0,
        '[4] CENTRED — no dead column hanging off either end',
        `knob row midpoint ${mid.toFixed (2)} vs container midpoint ${r.rowMid.toFixed (2)} (off by ${Math.abs (mid - r.rowMid).toFixed (2)} px)`);

  gate (r.dupes.length === 0,
        '[5] ONE element per parameter — the set is SHARED, not copied',
        r.dupes.length ? ('duplicated: ' + r.dupes.join (', ')) : 'no data-syn appears twice anywhere in the panel');

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail === 0 ? 0 : 1);
})();
