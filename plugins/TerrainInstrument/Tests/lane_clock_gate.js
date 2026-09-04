// ══════════════════════════════════════════════════════════════════════════════════════════════
//  lane_clock_gate.js — fb581: THE PUSH LANE IS THE CLOCK. A QUIET LANE MEANS REST, NEVER "DRIVE YOURSELF".
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/lane_clock_gate.js [page.html]
//
//  Max: "could you at least put the LFO to rest instead of it free running again? we fixed this a while ago,
//  we had it fade in and out when midi wasnt being used... it always has to react and respond and be linked to
//  the midi signal. Whenever there's midi stoppage, there's nothing moving." And: a fresh instance sometimes
//  free-runs, then later behaves — "it has some sort of trigger to say hey you can keep going".
//
//  THE BUG, measured on the shipped page by driving the lane exactly as the plugin does:
//        lane alive, keepalive every 500 ms  →  10.3 fps      (should be 0)
//        lane QUIET (the plugin resting)     →  48.2 fps      (should be 0)
//        keepalive every 2000 ms             →  48.2 fps      (should be 0)
//  The page's pushless fallback exists for a page with NO lane — a browser preview, this gate suite, a card
//  window on a dead channel. It inferred "no lane" from SILENCE, and silence is what a resting plugin sounds
//  like. Two ways in: `lastPush` started at 0 (so a fresh instance free-ran until the C++ caught up), and the
//  keepalive is sent every 30 SKIPPED frames — 500 ms at the editor's 60 Hz, but 2 s the moment that timer is
//  slowed (the 15 Hz idle path, an occluded window, a busy message thread), which is past the 700 ms threshold.
//  One clock drives the LFO, the filter and the terrain randomiser alike, which is why all three misbehaved.
//
//  THE BARS
//   1  A SEEN LANE, GONE QUIET, IS REST — stamp the lane once, then 4 s of silence: 0 painter runs.
//   2  THE SLOW CADENCE IS REST TOO — a keepalive every 2 s (the 15 Hz path) for 4 s: 0 painter runs.
//   3  A HEALTHY REST IS STILL — the lane stamped every 500 ms for 3 s: 0 painter runs.
//   4  ONCE HEARD, NEVER UNHEARD — quiet, then a stamp, then quiet again: still 0.
//   5  A PUSH STILL PAINTS — __tiFrame() runs the painters (the lane is the clock, not a mute button).
//   6  A PAGE WITH NO LANE STILL DRIVES ITSELF — never stamped: >= 20 fps, so the browser preview and this
//      whole gate suite keep working.
//
//  PROOF THE BARS CAN FAIL:  LANE_CLOCK_MUTATE=1 (the fallback ignores laneSeen, i.e. pre-fb581) → 1,2,4 red
//                            LANE_CLOCK_MUTATE=2 (the fallback never runs at all)                → 6 red
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
async function boot (P) {   /* THIS gate's boot: the C++ keepalive is simulated (window.__tiAlive every 50 ms) so the page's pushless fallback never runs painters — the host at rest, exactly */
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'), headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:820, height:656, deviceScaleFactor:2 });   /* the host's own page box: 820 wide, 656 inner (fb570's vis_diag on the installed AU) */
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await pg.evaluateOnNewDocument(STUB);
  /* THE QUIET HOST, faithfully: the C++ stamps the keepalive (so the page's pushless fallback stands down)
     AND pushes __tiQuiet — which is what arms fb508's __heroDrawn and fb510's __fxRestDrawn rest latches.
     Without the flag those latches never engage and a gate cannot see the state Max is actually in. */
  await pg.goto('file://' + P, { waitUntil:'load', timeout:60000 }); await sleep(1800);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late'); document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block'; try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await sleep(600);
  return { b, pg, errs };
}




const MUT = +(process.env.LANE_CLOCK_MUTATE || 0);
function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split(f).length - 1; if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); } src = src.replace(f, t); console.log('   mutation ' + MUT + ' applied'); };
  if (MUT === 1) { sub("if (laneSeen || nowMs() - lastPush <= 700){ fbOn = false; return; }", "if (nowMs() - lastPush <= 700){ fbOn = false; return; }");
                   sub("    if (laneSeen) return;                                     /* fb581 — this page has a clock; a quiet one means REST */", ""); }
  if (MUT === 2) sub("    if (!fbOn && nowMs() - lastPush > 700){ fbOn = true; window.requestAnimationFrame(fbChain); }", "");
  const p = path.join(os.tmpdir(), 'lane_clock_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}
(async () => {
  const P = mutatedPage();
  const { b, pg, errs } = await boot(P);
  console.log('\n══ fb581 — THE LANE IS THE CLOCK ══\n   page ' + P + (MUT ? '   MUTATION ' + MUT : '') + '\n');
  const run = (fn) => pg.evaluate(fn);
  await run(() => { window.__probeRuns = 0; window.__tiFrameReg('probe', () => { window.__probeRuns++; }); });
  const phase = async (ms, keepaliveMs) => await pg.evaluate(async (ms, k) => {
    const sleep = t => new Promise(r => setTimeout(r, t));
    const a = window.__probeRuns, t0 = performance.now();
    let iv = null; if (k) iv = setInterval(() => { window.__tiQuiet = 1; window.__tiAlive && window.__tiAlive(); }, k);
    await sleep(ms); if (iv) clearInterval(iv);
    return { runs: window.__probeRuns - a, fps: +((window.__probeRuns - a) / ((performance.now() - t0) / 1000)).toFixed(1) };
  }, ms, keepaliveMs);
  // the plugin speaks from its first frame — stamp the lane, then let it settle
  await run(() => { window.__tiQuiet = 1; window.__tiAlive && window.__tiAlive(); });
  await sleep(1200);
  const seen = await run(() => (window.__tiLaneSeen ? window.__tiLaneSeen() : null));
  const quiet = await phase(4000, 0);
  chk(seen === true && quiet.runs === 0, '1  A SEEN LANE, GONE QUIET, IS REST: 4 s of silence after one stamp → 0 painter runs', 'lane seen ' + seen + ' · ' + quiet.runs + ' runs (' + quiet.fps + ' fps)');
  const slow = await phase(4000, 2000);
  chk(slow.runs === 0, '2  THE SLOW CADENCE IS REST TOO: a keepalive every 2 s (the 15 Hz path) → 0 painter runs', slow.runs + ' runs (' + slow.fps + ' fps)');
  const healthy = await phase(3000, 500);
  chk(healthy.runs === 0, '3  A HEALTHY REST IS STILL: the lane stamped every 500 ms → 0 painter runs', healthy.runs + ' runs (' + healthy.fps + ' fps)');
  await run(() => { window.__tiAlive && window.__tiAlive(); }); const again = await phase(3000, 0);
  chk(again.runs === 0, '4  ONCE HEARD, NEVER UNHEARD: quiet → a stamp → quiet again is still rest', again.runs + ' runs');
  const painted = await pg.evaluate(async () => { const a = window.__probeRuns; window.__tiFrame && window.__tiFrame();
    await new Promise(r => setTimeout(r, 200)); return window.__probeRuns - a; });
  chk(painted >= 1, '5  A PUSH STILL PAINTS: one __tiFrame() runs the painters', painted + ' run(s)');
  await b.close();
  // 6 — a second page that never hears the lane must still drive itself (browser preview, this suite)
  const two = await boot(P);
  await two.pg.evaluate(() => { window.__probeRuns = 0; window.__tiFrameReg('probe', () => { window.__probeRuns++; }); });
  const free = await two.pg.evaluate(async () => { const sleep = t => new Promise(r => setTimeout(r, t));
    const a = window.__probeRuns, t0 = performance.now(); await sleep(2500);
    return +((window.__probeRuns - a) / ((performance.now() - t0) / 1000)).toFixed(1); });
  chk(free >= 20, '6  A PAGE WITH NO LANE STILL DRIVES ITSELF: never stamped → >= 20 fps (the browser preview and this gate suite)', free + ' fps');
  if (errs.length) console.log('   page errors: ' + errs.slice(0, 3).join(' | '));
  console.log(`\n══ RESULT: ${pass} pass, ${fail} FAIL ══`); await two.b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error('HARNESS ERROR', e); process.exit(2); });
