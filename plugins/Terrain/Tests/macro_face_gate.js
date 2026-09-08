// ══════════════════════════════════════════════════════════════════════════════════════════════
//  macro_face_gate.js — fb575: THE MACRO KNOB'S FACE RIDES THE FRAME CLOCK UNDER THE WHEEL.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/macro_face_gate.js [page.html]
//
//  Max: "whenever I press MIDI Learn and I move my mod wheel on the Macros, it's still choppy... smooth, just
//  like how we're clicking and dragging it."  MEASURED BEFORE: 8 face repaints in a 1 s sweep fed at 60 fps
//  (a 150 ms poll, max gap 160 ms); a drag repaints on every pointer move.
//
//  THE BAR
//   1  THE FACE FOLLOWS THE FEED — the editor feed ships 60 frames in one second carrying the knob 0 → 1
//      (window.__mvMacroBase + __tiFrame, exactly what the C++ pushes); the face repaints >= 45 times, never
//      pausing more than 40 ms, and ends at 100.
//   2  NOT WHILE A HAND IS ON IT — during a drag the feed does not repaint the face (the drag paints it).
//
//  PROOF THE BAR CAN FAIL:  MACRO_FACE_MUTATE=1 (no frame painter, the poll alone) → 1 red
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE = process.argv[2] || path.join(ROOT, 'Source/ui/public/index.html');
let pass = 0, fail = 0;
const chk = (ok, l, d) => { if (ok) { pass++; console.log('  ok    ' + l + (d ? '   ' + d : '')); } else { fail++; console.log('  FAIL  ' + l + (d ? '   ' + d : '')); } };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const STUB = () => { const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]'); if(/Json|JSON/.test(n))return r('{}'); r(0);}),backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',__juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}}; };
async function boot (P) {
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'), headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:820, height:656, deviceScaleFactor:2 });   /* the host's own page box: 820 wide, 656 inner (fb570's vis_diag on the installed AU) */
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await pg.evaluateOnNewDocument(STUB);
  await pg.goto('file://' + P, { waitUntil:'load', timeout:60000 }); await sleep(1800);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late'); document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block'; try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await sleep(600);
  return { b, pg, errs };
}


const MUT = +(process.env.MACRO_FACE_MUTATE || 0);
function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split(f).length - 1; if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); } src = src.replace(f, t); console.log('   mutation ' + MUT + ' applied'); };
  if (MUT === 1) sub("if(window.__tiFrameReg) window.__tiFrameReg('vm-face',", "if(false) window.__tiFrameReg('vm-face',");
  const p = path.join(os.tmpdir(), 'macro_face_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}
(async () => {
  const P = mutatedPage();
  const { b, pg, errs } = await boot(P);
  console.log('\n══ fb575 — THE MACRO FACE RIDES THE FRAME CLOCK ══\n   page ' + P + (MUT ? '   MUTATION ' + MUT : '') + '\n');
  await pg.click('#syn-panel .vm-macros-btn'); await sleep(300);
  const r = await pg.evaluate(async () => {
    const sleep = ms => new Promise(r => setTimeout(r, ms));
    const mk = document.querySelector('#syn-panel .vm-macro .vm-mk'); let paints = 0; const stamps = [];
    new MutationObserver(() => { paints++; stamps.push(performance.now()); }).observe(mk, { childList: true, subtree: true, attributes: true });
    let k = 0;
    await new Promise(done => { const iv = setInterval(() => { k++; const v = Math.min(1, k / 60); window.__mvMacroBase = [v, 0, 0, 0, 0, 0, 0, 0, 0]; if (window.__tiFrame) window.__tiFrame(); if (k >= 60) { clearInterval(iv); setTimeout(done, 200); } }, 16); });
    const gaps = stamps.slice(1).map((t, i) => t - stamps[i]);
    return { paints, maxGap: gaps.length ? Math.max(...gaps) : 9999, face: +mk.getAttribute('data-v') };
  });
  chk(r.paints >= 45 && r.maxGap <= 40 && r.face === 100, '1  THE FACE FOLLOWS THE FEED: 60 shipped frames in 1 s → >= 45 repaints, no gap over 40 ms, ends at 100', `${r.paints} repaints · max gap ${r.maxGap.toFixed(0)} ms · face ${r.face}`);
  const d = await pg.evaluate(async () => {
    const sleep = ms => new Promise(r => setTimeout(r, ms));
    const mk = document.querySelector('#syn-panel .vm-macro .vm-mk'); const r = mk.getBoundingClientRect();
    mk.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, button: 0, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2, pointerId: 1 }));
    let paints = 0; const mo = new MutationObserver(() => paints++); mo.observe(mk, { childList: true, subtree: true, attributes: true });
    for (let k = 1; k <= 20; k++) { window.__mvMacroBase = [0.5 + k / 100, 0, 0, 0, 0, 0, 0, 0, 0]; if (window.__tiFrame) window.__tiFrame(); await sleep(16); }
    await sleep(60); mo.disconnect(); document.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 1 })); return { paints };
  });
  chk(d.paints === 0, '2  NOT WHILE A HAND IS ON IT: 20 fed frames during a drag repaint the face 0 times (the drag paints it)', `${d.paints} repaints`);
  if (errs.length) console.log('   page errors: ' + errs.join(' | '));
  console.log(`\n══ RESULT: ${pass} pass, ${fail} FAIL ══`); await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error('HARNESS ERROR', e); process.exit(2); });
