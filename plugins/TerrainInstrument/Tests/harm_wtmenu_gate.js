// ══════════════════════════════════════════════════════════════════════════════════════════════
//  harm_wtmenu_gate.js — fb588: THE HARMONIC ENGINE GETS **THE SAME MENU**, NOT A SECOND ONE.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/harm_wtmenu_gate.js [page.html]
//
//  Max: "implement our wave table presets and the same presets that we already have, THE SAME MENU
//  for harmonics mode."
//
//  "The same menu" is a claim about WIRING, and the project has a standing law about it:
//  ♻️ NO NEW CODE FOR EXISTING THINGS — RECYCLE. So the bar is not "a menu appears"; the bar is
//  that the element which appears is the VERY SAME #osc-<o>-preset-select the wavetable engine
//  uses, because openWtSelectMenu binds its glass two-pane browser (factory tables AND user
//  imports) to that exact id on a timer, engine-agnostically. Un-hiding it is the whole feature;
//  a second copy would be the failure.
//
//  THE BARS
//   0  THE PANEL ACTUALLY LAID OUT — nothing below may be asserted on a page that never rendered
//   1  A SEVENTH FAMILY is offered, and it is called Table
//   2  PICKING Table REVEALS THE WAVETABLE NAME + its stepper arrows
//   3  THE OTHER SIX STILL HIDE IT — this is a Table-only surface, not a new permanent header item
//   4  IT IS THE SAME ELEMENT — the revealed control IS #osc-a-preset-select (the id the glass
//      browser binds to), and that id exists exactly ONCE in the whole panel
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const puppeteer = require ('puppeteer-core');

const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');

// the JUCE bridge stub, lifted from fm_wtpage_gate.js — without it the panel never lays out and
// every measurement reads 0, which a naive gate happily calls a pass.
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
  await new Promise (r => setTimeout (r, 2400));   // the glass-menu binder runs on a 200/800/2000ms ladder

  const r = await p.evaluate (() => {
    const out = { err: null, perFamily: [] };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      if (! dev) { out.err = 'no osc device'; return out; }
      dev.classList.add ('engine-harm');

      const sel = dev.querySelector ('.hm-mode-select');
      if (! sel) { out.err = 'no harmonic family select'; return out; }
      out.options = [...sel.options].map (o => o.textContent);

      const wtSel = document.getElementById ('osc-a-preset-select');
      out.hasWtSel = !! wtSel;
      const wrap = wtSel ? wtSel.parentElement : null;
      out.idCount = document.querySelectorAll ('#syn-panel #osc-a-preset-select').length;

      const rect = dev.getBoundingClientRect();
      out.devW = rect.width;

      const shownFor = (idx) => {
        sel.value = String (idx);
        sel.dispatchEvent (new Event ('change', { bubbles: true }));
        const w = wrap ? getComputedStyle (wrap).display !== 'none' : false;
        const nav = dev.querySelector ('.wt-nav');
        return { fam: idx, wrapShown: w, hasClass: dev.classList.contains ('harm-table'),
                 navShown: nav ? getComputedStyle (nav).display !== 'none' : false };
      };
      for (let i = 0; i <= 6; ++i) out.perFamily.push (shownFor (i));

      // and the revealed wrap must be the one the glass browser is bound to
      out.boundIsSame = !! (wrap && wrap.contains (wtSel));
    } catch (e) { out.err = String (e); }
    return out;
  });

  console.log (`\n══ harm_wtmenu_gate — fb588 ══  ${PAGE}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }

  const laidOut = r.devW > 100 && r.hasWtSel;
  gate (laidOut, '[0] THE PANEL ACTUALLY LAID OUT — the assertions below are real',
        `osc device width ${(r.devW||0).toFixed (1)} px, #osc-a-preset-select present=${r.hasWtSel}`);
  if (! laidOut) { console.log ('\n  ❌ degenerate page — not asserting anything on it\n');
                   await b.close(); process.exit (1); }

  gate (r.options.length === 7 && r.options[6] === 'Table',
        '[1] A SEVENTH FAMILY, and it is called Table',
        `${r.options.length} options: ${r.options.join (' · ')}`);

  const tbl = r.perFamily[6];
  gate (tbl.wrapShown && tbl.hasClass && tbl.navShown,
        '[2] PICKING Table REVEALS THE WAVETABLE NAME + its stepper arrows',
        `harm-table class=${tbl.hasClass}  name shown=${tbl.wrapShown}  arrows shown=${tbl.navShown}`);

  const others = r.perFamily.slice (0, 6);
  gate (others.every (f => ! f.wrapShown && ! f.hasClass),
        '[3] THE OTHER SIX STILL HIDE IT — a Table-only surface',
        others.map (f => `${f.fam}:${f.wrapShown ? 'shown' : 'hidden'}`).join (' '));

  gate (r.boundIsSame && r.idCount === 1,
        '[4] IT IS THE SAME ELEMENT the glass browser binds to — recycled, not copied',
        `revealed wrap contains #osc-a-preset-select=${r.boundIsSame}, and that id appears ${r.idCount}x in the panel`);

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail === 0 ? 0 : 1);
})();
