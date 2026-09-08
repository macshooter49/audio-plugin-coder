// ══════════════════════════════════════════════════════════════════════════════════════════════
//  back_spectral_gate.js — fb594: THE SPECTRAL WINDOW SHOWS ONLY WHERE IT IS ALIVE.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/back_spectral_gate.js [page.html]
//
//  Max: "if there's no spectral for granular, then the low and the high button needs to get taken
//  away... FM we can keep because we have spectral for FM."
//
//  Lo/Hi reach the audio through ONE line (PluginProcessor.cpp:1615) whose output is read by
//  exactly two engine cases — Engine::WT (SynthVoice.h:4200) and Engine::FM (SynthVoice.h:4289).
//  On the other five the knobs are inert. Harmonic is dead INCLUDING its Table family, which is
//  the one that looks live (fb588's additive recipe IS a wavetable, but it bakes through
//  oscSourceSpec → specForPreset with no morph and no cut).
//
//  ⚠️ .back-only is display:none unless .swapped — a gate that forgets to add it measures an
//     invisible row and passes on nothing. Bar 0 refuses to run the rest until the row is real.
//
//  ⚠️ AND THE GRID IS THE POINT, NOT JUST THE KNOB. Hiding 2 of 7 columns without re-declaring
//     grid-template-columns leaves TWO DEAD COLUMNS on the right edge (the fb586 law). Bar 3
//     measures SPACING and MIDPOINT, because "the knob is gone" and "the row is right" are
//     different claims and only the second one is what Max asked for.
//
//  THE BARS
//   0  THE BACK ROW ACTUALLY LAID OUT — nothing below is asserted on an invisible row
//   1  THE THREE LIVE ENGINES KEEP ALL SEVEN — Wavetable and FM untouched; RESYNTH joined them (rs2-cut:
//      GeodeEngine.h reads Lo/Hi as its two coexisting cuts — see Tests/geode_cut_cert.cpp)
//   2  THE FOUR DEAD ENGINES SHOW FIVE — Lo/Hi gone on Sample/Granular/Harmonic/Modal
//   3  NO HOLE — even column spacing and a row midpoint on the container midpoint, every engine
//   4  THE PARAMS SURVIVE — the knobs are HIDDEN, never removed from the DOM (deleting them would
//      renumber mod destinations 1846..1853 and break every saved patch)
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const puppeteer = require ('puppeteer-core');
const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');

const STUB = () => {
  const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
    getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
      if(/Json|JSON/.test(n))return r('{}'); r(0);}),
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const m=window.Juce;let h=m;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return h;},set(v){h=Object.assign({},v||{},{getNativeFunction:m.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
};

let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail;
  console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

(async () => {
  const b = await puppeteer.launch ({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox','--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport ({ width: 820, height: 656, deviceScaleFactor: 2 });
  await p.evaluateOnNewDocument (STUB);
  await p.goto ('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise (r => setTimeout (r, 1600));
  await p.evaluate (() => { const sp = document.getElementById ('syn-panel');
                            if (sp) sp.style.display = 'block'; window.dispatchEvent (new Event ('resize')); });
  await new Promise (r => setTimeout (r, 2400));

  const r = await p.evaluate (() => {
    const out = { err: null, rows: [] };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      if (! dev) { out.err = 'no osc device'; return out; }
      dev.classList.add ('swapped');                       // .back-only is display:none without it
      const row = dev.querySelector ('.back-only > .osc-knobs');
      if (! row) { out.err = 'no back-only osc-knobs row'; return out; }

      // WT is the class-less default; the rest each carry one engine class
      const ENG = [ ['Wavetable',null], ['FM','engine-fm'], ['Sample','engine-sample'],
                    ['Granular','engine-granular'], ['Resynth','engine-geode'],
                    ['Harmonic','engine-harm'], ['Modal','engine-modal'] ];
      const ALL = ENG.map (e => e[1]).filter (Boolean);

      for (const [name, cls] of ENG) {
        ALL.forEach (c => dev.classList.remove (c));
        if (cls) dev.classList.add (cls);

        const all = [...row.querySelectorAll ('.knob[data-syn]')];
        const shown = all.filter (k => getComputedStyle (k).display !== 'none'
                                    && k.getBoundingClientRect().width > 0);
        const rr = row.getBoundingClientRect();
        const cx = shown.map (k => { const q = k.getBoundingClientRect(); return q.left + q.width/2; });
        // adjacent centre gaps — a dead column shows up as one gap unlike the others
        const gaps = []; for (let i = 1; i < cx.length; ++i) gaps.push (cx[i] - cx[i-1]);
        const gMin = gaps.length ? Math.min (...gaps) : 0, gMax = gaps.length ? Math.max (...gaps) : 0;
        // row midpoint vs container midpoint — a trailing hole pulls the ink left
        const mid = cx.length ? (cx[0] + cx[cx.length-1]) / 2 : 0;
        const cmid = rr.left + rr.width / 2;

        out.rows.push ({ name, cls, inDom: all.length, shown: shown.length,
                         rowW: +rr.width.toFixed (1),
                         gapSpread: +(gMax - gMin).toFixed (2),
                         midOff: +(mid - cmid).toFixed (2),
                         syns: shown.map (k => k.getAttribute ('data-syn').replace (/^SYN_OSC_[A-D]_/, '')) });
      }
    } catch (e) { out.err = String (e); }
    return out;
  });

  console.log (`\n══ back_spectral_gate — fb594 ══  ${PAGE}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }

  const laidOut = r.rows.length === 7 && r.rows[0].rowW > 100 && r.rows[0].inDom === 7;
  gate (laidOut, '[0] THE BACK ROW ACTUALLY LAID OUT — the bars below are real',
        `row width ${(r.rows[0]||{}).rowW} px, ${(r.rows[0]||{}).inDom} knobs in the DOM`);
  if (! laidOut) { console.log ('\n  ❌ degenerate row — refusing to assert on it\n');
                   await b.close(); process.exit (1); }

  const LIVE = ['Wavetable', 'FM', 'Resynth'];   // rs2-cut — Resynth reads Lo/Hi now (GeodeEngine.h LOW/HIGH)
  const live = r.rows.filter (x => LIVE.includes (x.name));
  const dead = r.rows.filter (x => ! LIVE.includes (x.name));

  gate (live.every (x => x.shown === 7),
        '[1] THE THREE LIVE ENGINES KEEP ALL SEVEN — Lo/Hi is real on WT, FM and Resynth (rs2-cut)',
        live.map (x => `${x.name} ${x.shown}`).join ('  ·  '));

  gate (dead.every (x => x.shown === 5 && ! x.syns.some (s => /SPECTRAL/.test (s))),
        '[2] THE FOUR DEAD ENGINES SHOW FIVE — no inert Lo/Hi anywhere',
        dead.map (x => `${x.name} ${x.shown}`).join ('  ·  '));

  const HOLE = 1.5;   // px — even columns land well inside this; one dead column is ~40px out
  gate (r.rows.every (x => x.gapSpread <= HOLE && Math.abs (x.midOff) <= HOLE),
        '[3] NO HOLE — even spacing and a centred row on every engine (the fb586 law)',
        r.rows.map (x => `${x.name} spread ${x.gapSpread} mid ${x.midOff}`).join ('  ·  '));

  gate (r.rows.every (x => x.inDom === 7),
        '[4] THE PARAMS SURVIVE — the knobs are hidden, never removed (mod dests 1846..1853)',
        r.rows.map (x => `${x.name} ${x.inDom} in DOM`).join ('  ·  '));

  console.log (`\n  ${fail ? '❌' : '✅'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail ? 1 : 0);
})();
