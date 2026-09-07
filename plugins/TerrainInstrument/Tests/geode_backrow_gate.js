// ══════════════════════════════════════════════════════════════════════════════════════════════
//  geode_backrow_gate.js — rs2-cut: LP + HP TO THE BACK. THE FRONT IS FIVE, THE BACK IS SEVEN.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/geode_backrow_gate.js [page.html]
//
//  Max: "take away the low pass and the high pass filter and put it in the back ... this IS spectral so
//  they need a low and a high pass filter ... these two need to coexist and sculpt the sound, and we can't
//  do that if we're right-clicking and moving back and forth between the two ... so the first panel is
//  going to have FIVE."
//
//  That is four measurable claims, and this gate is all four:
//    · RESYNTH page 1 shows FIVE — Scan · Stretch · Sieve · Shape · Drive; the Cut knob is hidden, not removed
//    · those five are EVENLY spaced and CENTRED (the fb586 grid-hole law: the grid is re-declared, so no
//      dead sixth column hangs off the right)
//    · RESYNTH's BACK row shows SEVEN, Low and High present, evenly spaced and centred (fb594 hid them on
//      engine-geode because nothing read them; GeodeEngine.h reads them now)
//    · the Cut's right-click LP/HP menu and its label-follow code are GONE from the page source
//
//  ⚠️ .back-only is display:none unless .swapped, and .geode-knob-wrap is display:none unless
//     .engine-geode:not(.swapped) — bar 0 refuses to assert on a row that never laid out.
//
//  THE BARS
//   0  THE PANEL ACTUALLY LAID OUT — nothing below is asserted on an invisible row
//   1  RESYNTH PAGE 1 IS FIVE — Scan Stretch Sieve Shape Drive shown, Cut hidden, six in the DOM
//   2  NO HOLE ON PAGE 1 — even spacing and a centred row (the fb586 law)
//   3  PAGE 2 IS STILL SIX — untouched, even, centred
//   4  RESYNTH'S BACK ROW IS SEVEN — Low and High present, even, centred
//   5  THE CUT MENU AND LABEL-FOLLOW ARE GONE — no KNOB_MENUS entry, no "LABEL follows its LP/HP mode"
//   6  THE PARAMS SURVIVE — GEODE_CUT is still in the DOM, and every data-syn is unique panel-wide
// ══════════════════════════════════════════════════════════════════════════════════════════════
const fs   = require ('fs');
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
    const out = { err: null };
    const measure = (row) => {
      const all = [...row.querySelectorAll ('.knob[data-syn]')];
      const shown = all.filter (k => getComputedStyle (k).display !== 'none' && k.getBoundingClientRect().width > 0);
      const rr = row.getBoundingClientRect();
      const cx = shown.map (k => { const q = k.getBoundingClientRect(); return q.left + q.width/2; });
      const gaps = []; for (let i = 1; i < cx.length; ++i) gaps.push (cx[i] - cx[i-1]);
      const gMin = gaps.length ? Math.min (...gaps) : 0, gMax = gaps.length ? Math.max (...gaps) : 0;
      const mid = cx.length ? (cx[0] + cx[cx.length-1]) / 2 : 0;
      return { inDom: all.length, shown: shown.length, rowW: +rr.width.toFixed (1),
               gapSpread: +(gMax - gMin).toFixed (2), midOff: +(mid - (rr.left + rr.width / 2)).toFixed (2),
               labels: shown.map (k => ((k.querySelector ('.knob-label') || {}).textContent || '').trim()),
               syns: shown.map (k => k.getAttribute ('data-syn').replace (/^SYN_OSC_[A-D]_/, '')),
               hidden: all.filter (k => ! shown.includes (k)).map (k => k.getAttribute ('data-syn').replace (/^SYN_OSC_[A-D]_/, '')) };
    };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      if (! dev) { out.err = 'no osc device'; return out; }
      ['engine-fm','engine-sample','engine-granular','engine-harm','engine-modal','swapped','uni-page'].forEach (c => dev.classList.remove (c));
      dev.classList.add ('engine-geode');                  // put this oscillator on RESYNTH

      const wrap = dev.querySelector ('.geode-knob-wrap');
      const pg1  = wrap && wrap.querySelector ('.geode-pg1');
      const pg2  = wrap && wrap.querySelector ('.geode-pg2');
      if (! wrap || ! pg1 || ! pg2) { out.err = 'no geode knob wrap / pages'; return out; }
      out.wrapShown = getComputedStyle (wrap).display !== 'none';
      wrap.classList.remove ('pg2');
      out.pg1 = measure (pg1);
      wrap.classList.add ('pg2');                          // the arrow's class, set directly (see initSetArrows apply())
      out.pg2 = measure (pg2);
      wrap.classList.remove ('pg2');

      dev.classList.add ('swapped');                       // .back-only is display:none without it
      const back = dev.querySelector ('.back-only > .osc-knobs');
      if (! back) { out.err = 'no back-only osc-knobs row'; return out; }
      out.back = measure (back);
      dev.classList.remove ('swapped');

      // one element per parameter, panel-wide; and the retired knob is still there
      const seen = {}; out.dupes = [];
      document.querySelectorAll ('#syn-panel .knob[data-syn]').forEach (k => {
        const s = k.getAttribute ('data-syn'); seen[s] = (seen[s] || 0) + 1; });
      Object.keys (seen).forEach (k => { if (seen[k] > 1) out.dupes.push (k + ' x' + seen[k]); });
      out.cutInDom = ['A','B','C','D'].every (O => !! document.querySelector ('#syn-panel .knob[data-syn="SYN_OSC_' + O + '_GEODE_CUT"]'));
    } catch (e) { out.err = String (e); }
    return out;
  });

  console.log (`\n══ geode_backrow_gate — rs2-cut ══  ${PAGE}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }

  const laidOut = r.wrapShown && r.pg1.rowW > 100 && r.back.rowW > 100 && r.pg1.shown > 0 && r.back.shown > 0;
  gate (laidOut, '[0] THE PANEL ACTUALLY LAID OUT — the bars below are real',
        `geode wrap shown=${r.wrapShown}, page-1 row ${r.pg1.rowW} px (${r.pg1.shown} shown), back row ${r.back.rowW} px (${r.back.shown} shown)`);
  if (! laidOut) { console.log ('\n  ❌ degenerate layout — refusing to assert on it\n'); await b.close(); process.exit (1); }

  const HOLE = 1.5;   // px — even columns land well inside this; one dead column is ~45px out
  const WANT1 = ['Scan','Stretch','Sieve','Shape','Drive'];
  gate (r.pg1.shown === 5 && r.pg1.inDom === 6 && JSON.stringify (r.pg1.labels) === JSON.stringify (WANT1) && r.pg1.hidden.includes ('GEODE_CUT'),
        '[1] RESYNTH PAGE 1 IS FIVE — Scan · Stretch · Sieve · Shape · Drive; the Cut knob hidden, not removed',
        `shown ${r.pg1.labels.join (' · ')} (${r.pg1.shown} of ${r.pg1.inDom} in DOM), hidden ${r.pg1.hidden.join (',') || 'none'}`);
  gate (r.pg1.gapSpread <= HOLE && Math.abs (r.pg1.midOff) <= HOLE,
        '[2] NO HOLE ON PAGE 1 — even spacing and a centred row (the fb586 law)',
        `gap spread ${r.pg1.gapSpread} px, midpoint off ${r.pg1.midOff} px`);
  gate (r.pg2.shown === 6 && r.pg2.gapSpread <= HOLE && Math.abs (r.pg2.midOff) <= HOLE,
        '[3] PAGE 2 IS STILL SIX — untouched, even, centred',
        `${r.pg2.labels.join (' · ')} · spread ${r.pg2.gapSpread} mid ${r.pg2.midOff}`);
  gate (r.back.shown === 7 && r.back.syns.includes ('SPECTRAL_LO') && r.back.syns.includes ('SPECTRAL_HI')
        && r.back.gapSpread <= HOLE && Math.abs (r.back.midOff) <= HOLE,
        '[4] RESYNTH\'S BACK ROW IS SEVEN — Low and High present, even, centred',
        `${r.back.syns.join (' · ')} (${r.back.shown}) · spread ${r.back.gapSpread} mid ${r.back.midOff}`);

  const src = fs.readFileSync (PAGE, 'utf8');
  const menuLeft = (src.match (/'SYN_OSC_[A-D]_GEODE_CUT'\s*:\s*\{/g) || []).length;
  const followLeft = /LABEL follows its LP\/HP mode/.test (src) || /_GEODE_CUT_MODE'\)/.test (src);
  gate (menuLeft === 0 && ! followLeft,
        '[5] THE CUT MENU AND LABEL-FOLLOW ARE GONE — no right-click LP/HP, no label that follows a mode',
        `KNOB_MENUS Cut entries ${menuLeft}, label-follow present ${followLeft}`);
  gate (r.cutInDom && r.dupes.length === 0,
        '[6] THE PARAMS SURVIVE — GEODE_CUT is still in the DOM (hidden), every data-syn unique panel-wide',
        `Cut in DOM ${r.cutInDom}, duplicates ${r.dupes.join (', ') || 'none'}`);

  console.log (`\n  ${fail ? '❌' : '✅'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail ? 1 : 0);
})();
