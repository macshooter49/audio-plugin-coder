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
//  fb601 REWRITE OF BARS [2] AND [3]. "The same menu" was the fb588 question and it is settled;
//  the question that replaced it is WHICH PARAM the one menu writes to. Harmonics now selects its
//  own table through SYN_OSC_x_HARM_TABLE while the Wavetable engine keeps SYN_OSC_x_WT_PRESET —
//  ONE <select>, one glass browser, one stepper, and a branch in __synTablePid. The old [2]/[3]
//  ("picking Table reveals the name" / "every stored family shows the name") had collapsed into
//  each other under fb599's tables-only rule and asserted the same visibility twice; that claim
//  is kept, once, as [5]. The freed slots ask the question that can actually be got wrong:
//  a header that writes the OTHER engine's table is a silent, saved-state-corrupting bug.
//
//  ⚠️ IF THE PROCESSOR DOES NOT REGISTER SYN_OSC_x_HARM_TABLE YET, [2] REPORTS **PENDING** AND
//  DOES NOT FAIL. Source/PluginProcessor.cpp and Source/ParameterIDs.h are read off disk to
//  decide, and the line says which of them was searched and what was found — because "this gate
//  is red for a reason nobody can see" is the same zero-information red bar the project bans.
//
//  THE BARS
//   0  THE PANEL ACTUALLY LAID OUT — nothing below may be asserted on a page that never rendered
//   1  A SEVENTH FAMILY is offered, and it is called Table
//   2  🚨 THE HARMONICS HEADER ADDRESSES ITS OWN TABLE — on an engine-harm osc the one header
//      <select> writes SYN_OSC_A_HARM_TABLE, and SYN_OSC_A_WT_PRESET is neither written nor moved.
//      (PENDING, not FAIL, on a build where that param does not exist yet.)
//   3  🚨 AND THE WAVETABLE ENGINE IS UNTOUCHED — the SAME element on an osc that is not
//      engine-harm still writes SYN_OSC_A_WT_PRESET. The branch is per-engine, not a takeover.
//   4  IT IS THE SAME ELEMENT — the revealed control IS #osc-a-preset-select (the id the glass
//      browser binds to), and that id exists exactly ONCE in the whole panel
//   5  EVERY STORED FAMILY SHOWS THE NAME + the ‹ › stepper (fb599 tables-only; before fb588 a
//      HARM oscillator whose stored family was not Table had no name and no arrows at all)
//
//  MUTATION CONTROL (fb421):
//      WTMENU_MUTATE=1   force the "HARM_TABLE is registered" arm even when the processor has no
//                        such param — the way to exercise bar [2]'s live assertion before the
//                        C++ lands (it then fails, correctly: the page has nothing to write to)
//      WTMENU_MUTATE=2   __synTablePid always answers WT_PRESET (the pre-fb601 header) -> [2] RED
//      WTMENU_MUTATE=3   force the PENDING arm on a build that DOES register it — the way to see
//                        what a future session running this against an older processor will read
// ══════════════════════════════════════════════════════════════════════════════════════════════
const fs   = require ('fs');
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

let pass = 0, fail = 0, pending = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail;
  console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

/* fb601 — IS SYN_OSC_x_HARM_TABLE A REAL PARAM ON THIS BUILD? Read off disk, never guessed: a
   harness that assumes the processor's roster is the eleventh site (Tests/README.md's own warning
   about all_menus.js). Both files are searched so a split registration is still found. */
const SRC = (rel) => { try { return fs.readFileSync (path.resolve (path.dirname (PAGE), '..', '..', rel), 'utf8'); }
                       catch (e) { return ''; } };
const IDS_FILE = SRC ('ParameterIDs.hpp') ? 'ParameterIDs.hpp' : 'ParameterIDs.h';   // .hpp in this tree
const PROC = SRC ('PluginProcessor.cpp'), PIDS = SRC (IDS_FILE);
const MUT  = process.env.WTMENU_MUTATE || '';
const inProc = /HARM_TABLE/.test (PROC), inIds = /HARM_TABLE/.test (PIDS);
const REGISTERED = (MUT === '3') ? false : ((MUT === '1') || inProc || inIds);
const WHERE = `PluginProcessor.cpp ${PROC ? (inProc ? 'HAS it' : 'no') : '(unreadable)'} · `
            + `${IDS_FILE} ${PIDS ? (inIds ? 'HAS it' : 'no') : '(unreadable)'}`
            + (MUT === '1' ? '  [WTMENU_MUTATE=1 forcing the live arm]' : '')
            + (MUT === '3' ? '  [WTMENU_MUTATE=3 forcing the PENDING arm]' : '');

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

  const r = await p.evaluate ((REGISTERED, MUT) => {
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

      // ══ fb601 — WHICH PARAM DOES THE ONE HEADER WRITE TO? ═══════════════════════════════
      //  The page installs its OWN window.Juce after load, so the evaluateOnNewDocument stub is
      //  not the one the write path reads. __synTableState consults window.Juce.getSliderState
      //  at WRITE time (not at wire time), so replacing it here is exactly the right probe — and
      //  it is also how "this build has no such param" is simulated honestly: an id in DENY gets
      //  a null SliderState, which is precisely what a real pre-fb601 processor hands back.
      out.hasRedirect = (typeof window.__synTablePid === 'function');
      const HARM_IDS = ['A','B','C','D'].map (L => 'SYN_OSC_' + L + '_HARM_TABLE');
      const DENY = new Set (REGISTERED ? [] : HARM_IDS);
      const VALS = {}; const WRITES = [];
      window.Juce.getSliderState = (id) => {
        if (DENY.has (id)) return null;                       // the param does not exist on this build
        return { getNormalisedValue: () => (VALS[id] != null ? VALS[id] : 0),
                 getScaledValue:     () => (VALS[id] != null ? VALS[id] : 0),
                 setNormalisedValue (v) { VALS[id] = v; WRITES.push ([id, +v]); },
                 setScaledValue     (v) { VALS[id] = v; WRITES.push ([id, +v]); },
                 getChoiceIndex: () => 0, setChoiceIndex () {}, getValue: () => false, setValue () {},
                 properties: { start:0, end:1, interval:0, name:'', label:'', numSteps:100, choices:[], parameterIndex:0 },
                 propertiesChangedEvent: { addListener () { return { remove () {} }; }, removeListener () {} },
                 valueChangedEvent:      { addListener () { return { remove () {} }; }, removeListener () {} } }; };
      if (MUT === '2' && window.__synTablePid)
        window.__synTablePid = (o) => 'SYN_OSC_' + String (o).toUpperCase() + '_WT_PRESET';

      //  __synTablePid reads #osc-<o>-device by id, so the class goes on THAT element (the
      //  bars above use the first .device.osc, which is the same node on this page — asserted).
      const devById = document.getElementById ('osc-a-device');
      out.devIsSame = (devById === dev);
      const pick = (idx) => { wtSel.value = String (idx);
                              wtSel.dispatchEvent (new Event ('change', { bubbles: true })); };

      // (a) HARMONICS — the header must address SYN_OSC_A_HARM_TABLE
      if (devById) devById.classList.add ('engine-harm');
      dev.classList.add ('engine-harm');
      out.pidHarm = window.__synTablePid ? window.__synTablePid ('a') : '(no __synTablePid)';
      VALS['SYN_OSC_A_WT_PRESET'] = 0.25;                      // a distinctive resting value
      WRITES.length = 0; pick (3);
      out.writesHarm = WRITES.slice();
      out.wtAfterHarm = VALS['SYN_OSC_A_WT_PRESET'];

      // (b) WAVETABLE — the same element, the same click, the other param
      if (devById) devById.classList.remove ('engine-harm');
      dev.classList.remove ('engine-harm');
      out.pidWt = window.__synTablePid ? window.__synTablePid ('a') : '(no __synTablePid)';
      WRITES.length = 0; pick (5);
      out.writesWt = WRITES.slice();
      out.harmAfterWt = VALS['SYN_OSC_A_HARM_TABLE'];

      dev.classList.add ('engine-harm');
      if (devById) devById.classList.add ('engine-harm');
      sel.value = '6'; sel.dispatchEvent (new Event ('change', { bubbles: true }));
    } catch (e) { out.err = String (e) + (e && e.stack ? ' | ' + e.stack.split ('\n')[1] : ''); }
    return out;
  }, REGISTERED, MUT);

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

  // ── [2] fb601 — the Harmonics header addresses its OWN table ──────────────────────────────
  const wroteHarm = (r.writesHarm || []).filter (w => w[0] === 'SYN_OSC_A_HARM_TABLE');
  const wroteWtOnHarm = (r.writesHarm || []).filter (w => w[0] === 'SYN_OSC_A_WT_PRESET');
  const d2 = `__synTablePid('a') on engine-harm = ${r.pidHarm}; the header write landed on `
           + `[${(r.writesHarm || []).map (w => w[0] + '=' + w[1].toFixed (4)).join (', ') || 'NOTHING'}]; `
           + `SYN_OSC_A_WT_PRESET still ${r.wtAfterHarm} (parked at 0.25 before the click). `
           + `Processor roster: ${WHERE}`;
  if (! REGISTERED) {
    ++pending;
    console.log (`  PEND  [2] 🚨 THE HARMONICS HEADER ADDRESSES ITS OWN TABLE — SYN_OSC_x_HARM_TABLE\n`
               + `        ⏳ NOT ASSERTED: this build's processor does not register HARM_TABLE yet.\n`
               + `        ${WHERE}\n`
               + `        The page ${r.hasRedirect ? 'DOES' : 'does NOT'} carry the __synTablePid redirect, and with the\n`
               + `        param absent it correctly falls back to WT_PRESET: ${d2}\n`
               + `        Re-run with WTMENU_MUTATE=1 to force the live assertion.`);
  } else {
    gate (r.hasRedirect && r.pidHarm === 'SYN_OSC_A_HARM_TABLE'
          && wroteHarm.length === 1 && wroteWtOnHarm.length === 0
          && Math.abs ((r.wtAfterHarm != null ? r.wtAfterHarm : -1) - 0.25) < 1e-9,
          '[2] 🚨 THE HARMONICS HEADER ADDRESSES ITS OWN TABLE — SYN_OSC_x_HARM_TABLE', d2);
  }

  // ── [3] fb601 — and the wavetable engine is untouched ─────────────────────────────────────
  const wroteWt = (r.writesWt || []).filter (w => w[0] === 'SYN_OSC_A_WT_PRESET');
  const wroteHarmOnWt = (r.writesWt || []).filter (w => w[0] === 'SYN_OSC_A_HARM_TABLE');
  gate (r.devIsSame && r.pidWt === 'SYN_OSC_A_WT_PRESET' && wroteWt.length === 1 && wroteHarmOnWt.length === 0,
        '[3] 🚨 AND THE WAVETABLE ENGINE IS UNTOUCHED — the SAME element, the other param',
        `#osc-a-device is the device the other bars use=${r.devIsSame}; __synTablePid('a') off engine-harm = `
      + `${r.pidWt}; the write landed on [${(r.writesWt || []).map (w => w[0] + '=' + w[1].toFixed (4)).join (', ') || 'NOTHING'}]`
      + `; SYN_OSC_A_HARM_TABLE after it = ${r.harmAfterWt === undefined ? 'never written' : r.harmAfterWt}`);

  gate (r.boundIsSame && r.idCount === 1,
        '[4] IT IS THE SAME ELEMENT the glass browser binds to — recycled, not copied',
        `revealed wrap contains #osc-a-preset-select=${r.boundIsSame}, and that id appears ${r.idCount}x in the panel`);

  /* fb599 — TABLES-ONLY. fb588 made the wavetable name a Table-ONLY surface because six other
     families shared the header; they are retired, the gather pins mainMode = 6, and the name is
     the only thing in that corner. ⚠️ A HARM oscillator whose STORED family is not Table used to
     have NO NAME AND NO ARROWS AT ALL (measured at boot on the shipped page); this bar is what
     catches that coming back. fb601 merged the old [2] into it — under tables-only they were the
     same assertion made twice. */
  const all7 = r.perFamily;
  gate (all7.every (f => f.wrapShown && f.hasClass) && all7[6].navShown,
        '[5] EVERY STORED FAMILY SHOWS THE NAME AND THE ‹ › STEPPER — the header is the wavetable header now',
        all7.map (f => `${f.fam}:${f.wrapShown ? 'shown' : 'HIDDEN'}`).join (' ')
      + `  ·  arrows on Table=${all7[6].navShown}`);

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed`
             + (pending ? `, ${pending} PENDING (not asserted — see above)` : '') + `\n`);
  await b.close();
  process.exit (fail === 0 ? 0 : 1);
})();
