// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tape_alive_gate.js — fb576: A TAPE MACHINE NEVER DISAPPEARS — ON THE CARD AND ON THE FRONT — WITH THE FRAME CLOCK AT REST.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/tape_alive_gate.js [page.html]
//
//  Max (two screenshots): "cassette has disappeared, and on the front it's also disappeared... Does it have
//  something to do with everything stopping once MIDI just stops?... make sure that these never disappear and
//  self-heal or reload every time it tries to fuck up." And: "bring the studio visualizer down a little bit to
//  be in the middle."
//
//  THE HOST AT REST, SIMULATED: the C++ keepalive is stamped every 50 ms (window.__tiAlive) so the page's pushless
//  fallback never runs the painters — frames 0, as measured on the installed AU (a fresh open, no note: frames 0,
//  the rack's tape canvas 300×150 with 0 ink, the front's 360×139 with 0 ink). Every bar below holds with frames 0.
//
//  THE BARS
//   1  THE SIMULATION HOLDS — after the boot settle, a registered painter runs 0 times in 1.2 s.
//      (fb577 note: a gesture now deliberately wakes a repair frame, and boot fires ~120 transitionends,
//      so this bar measures a SETTLED page — "no frames without a reason", which is the actual law.)
//   2  THE FRONT MACHINE IS DRAWN AT BOOT — #tapeMechCanvas has ink.
//   3  A RESIZE AT REST REPAINTS — window._fxSizeFn() wipes the canvas; it has ink again at once.
//   4  A MACHINE CHANGE REPAINTS — the ▶ arrow's click handler changes the picture (a different ink bbox) at once.
//   5  A WIPED FRONT CANVAS HEALS — canvas.width = canvas.width (blank) → ink within 1.4 s (the watchdog).
//   6  THE RACK CARD IS DRAWN WHEN ADDED — __fxAdd('tape') → ink within 200 ms.
//   7  A WIPED CARD HEALS — blank → ink within 1.4 s.
//   8  THE STUDIO CARD SITS IN THE MIDDLE — the ink's centre within 3 % of h of the canvas centre (was −5.9 %).
//   9  NO IDLE COST — 2 s at rest, a full second after the last gesture: 0 frames, the tape painter at most once.
//  10  THE WATCHDOG'S SAMPLE READS INK on a drawn card and on the drawn front (its middle row/column).
//
//  PROOF THE BARS CAN FAIL:  TAPE_ALIVE_MUTATE=1 (no boot ticks, no observers)      → 6 red
//                            TAPE_ALIVE_MUTATE=2 (no direct redraw after a resize)  → 2, 3 red
//                            TAPE_ALIVE_MUTATE=3 (no watchdogs)                     → 5, 7 red
//                            TAPE_ALIVE_MUTATE=4 (the Studio card not moved)        → 8 red
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
  await pg.evaluateOnNewDocument(() => { setInterval(() => { try { window.__tiAlive && window.__tiAlive(); } catch (e) {} }, 50); });
  await pg.goto('file://' + P, { waitUntil:'load', timeout:60000 }); await sleep(1800);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late'); document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block'; try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await sleep(600);
  return { b, pg, errs };
}


const MUT = +(process.env.TAPE_ALIVE_MUTATE || 0);
function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split(f).length - 1; if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); } src = src.replace(f, t); console.log('   mutation ' + MUT + ' applied'); };
  if (MUT === 1) { sub("var pend=false; function soon(){ if(pend) return;", "var pend=false; function soon(){ return;"); sub("[300,900,2500].forEach(function(ms){ setTimeout(tick,ms); });", "[].forEach(function(ms){ setTimeout(tick,ms); });"); }
  if (MUT === 2) sub("      redrawTapeMech();   /* fb576 — the resize wiped it", "      /* redrawTapeMech(); */   /* fb576 — the resize wiped it");
  if (MUT === 3) { sub("if(r.width&&r.height&&!inked(cvs[i])){ tick(); return; }", "if(false){ tick(); return; }"); sub("c.offsetParent !== null && window.__tpeInked && !window.__tpeInked(c)) redrawTapeMech();", "false) redrawTapeMech();"); }
  if (MUT === 4) sub("ctx.translate(0,h*0.06);   /* fb576", "ctx.translate(0,0);   /* fb576");
  const p = path.join(os.tmpdir(), 'tape_alive_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}
(async () => {
  const P = mutatedPage();
  const { b, pg, errs } = await boot(P);
  console.log('\n══ fb576 — A TAPE MACHINE NEVER DISAPPEARS (the frame clock at rest) ══\n   page ' + P + (MUT ? '   MUTATION ' + MUT : '') + '\n');
  const INK = (sel) => pg.evaluate((sel) => { const cv = document.querySelector(sel); if (!cv) return { n: -1, why: 'no canvas' }; if (!cv.width) return { n: -1, why: 'size 0' };
    const d = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height).data; let x0 = 1e9, x1 = -1, y0 = 1e9, y1 = -1, n = 0;
    for (let y = 0; y < cv.height; y++) for (let x = 0; x < cv.width; x++) { if (d[(y * cv.width + x) * 4 + 3] > 8) { n++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; } }
    return { n, w: cv.width, h: cv.height, y0, y1, cy: n ? (y0 + y1) / 2 : null, dc: n ? ((y0 + y1) / 2 - cv.height / 2) / cv.height : null, sample: window.__tpeInked ? window.__tpeInked(cv) : null }; }, sel);
  const frames = () => pg.evaluate(() => window.__probeFrames | 0);
  await pg.evaluate(() => { window.__probeFrames = 0; window.__tiFrameReg('probe', () => { window.__probeFrames++; }); });
  // boot() opened the synth; the front page first
  await pg.evaluate(() => { try { document.getElementById('syn-btn').click(); } catch (e) {} }); await sleep(2500);   /* fb577 — let boot's ~120 transitionends and the guard's first heal settle */
  const s0 = await frames(); await sleep(1200); const s1 = await frames();
  chk(s1 - s0 === 0, '1  THE SIMULATION HOLDS: settled, a registered painter ran 0 times in 1.2 s (the host at rest)', 'frames ' + (s1 - s0));
  const F0 = await INK('#tapeMechCanvas');
  chk(F0.n > 0, '2  THE FRONT MACHINE IS DRAWN AT BOOT, with frames 0', JSON.stringify({ ink: F0.n, size: F0.w + 'x' + F0.h, frames: await frames() }));
  await pg.evaluate(() => { window._fxSizeFn(); }); await sleep(30); const F1 = await INK('#tapeMechCanvas');
  chk(F1.n > 0, '3  A RESIZE AT REST REPAINTS: _fxSizeFn() wipes it, the machine is back at once', JSON.stringify({ ink: F1.n, frames: await frames() }));
  await pg.evaluate(() => { document.getElementById('machNext').click(); }); await sleep(60); const F2 = await INK('#tapeMechCanvas');   /* the arrow's own click handler (a pointer click at its centre lands on the .mv-ov overlay in the headless layout) */
  chk(F2.n > 0 && (F2.y0 !== F1.y0 || F2.y1 !== F1.y1 || Math.abs(F2.n - F1.n) > 200), '4  A MACHINE CHANGE REPAINTS: the ▶ arrow\'s click handler draws a different picture at once', JSON.stringify({ before: [F1.y0, F1.y1, F1.n], after: [F2.y0, F2.y1, F2.n], frames: await frames() }));
  await pg.evaluate(() => { const c = document.getElementById('tapeMechCanvas'); c.width = c.width; }); const Fw = await INK('#tapeMechCanvas'); await sleep(1400); const F3 = await INK('#tapeMechCanvas');
  chk(Fw.n === 0 && F3.n > 0, '5  A WIPED FRONT CANVAS HEALS within 1.4 s (the watchdog)', JSON.stringify({ wiped: Fw.n, healed: F3.n, frames: await frames() }));
  // the synth page: the rack
  await pg.evaluate(() => { try { document.getElementById('syn-btn').click(); } catch (e) {} }); await sleep(400);
  await pg.evaluate(() => { window.__fxAdd('tape'); window.__fx4Tick(); }); await sleep(200);
  const R0 = await INK('#fxr-rack .fxr-core[data-core="tape"] canvas');
  chk(R0.n > 0, '6  THE RACK CARD IS DRAWN WHEN ADDED, within 200 ms, frames 0', JSON.stringify({ ink: R0.n, size: R0.w + 'x' + R0.h, frames: await frames() }));
  await pg.evaluate(() => { const c = document.querySelector('#fxr-rack .fxr-core[data-core="tape"] canvas'); c.width = c.width; }); const Rw = await INK('#fxr-rack .fxr-core[data-core="tape"] canvas'); await sleep(1400); const R1 = await INK('#fxr-rack .fxr-core[data-core="tape"] canvas');
  chk(Rw.n === 0 && R1.n > 0, '7  A WIPED CARD HEALS within 1.4 s (the watchdog)', JSON.stringify({ wiped: Rw.n, healed: R1.n, frames: await frames() }));
  chk(R1.n > 0 && Math.abs(R1.dc) <= 0.03, '8  THE STUDIO CARD SITS IN THE MIDDLE: the ink centre within 3 % of h of the canvas centre (was −5.9 %)', 'ink centre y ' + (R1.cy || 0).toFixed(0) + ' vs ' + (R1.h / 2).toFixed(0) + ' (' + ((R1.dc || 0) * 100).toFixed(1) + '% of h) · bbox y ' + R1.y0 + '..' + R1.y1 + ' of ' + R1.h);
  await sleep(1000);   /* fb577 — a full second past the last gesture: the burst is done */
  const q0 = await frames(), t0 = await pg.evaluate(() => window.__tpeTicks | 0); await sleep(2000);
  const q1 = await frames(), t1 = await pg.evaluate(() => window.__tpeTicks | 0);
  chk(q1 - q0 === 0 && t1 - t0 <= 1, '9  NO IDLE COST: 2 s at rest, a second past the last gesture → 0 frames, the tape painter ran at most once', 'frames ' + (q1 - q0) + ' · tape ticks ' + (t1 - t0));
  const Fs = await INK('#tapeMechCanvas');
  chk(R1.sample === true && Fs.sample === true, '10 THE WATCHDOG\'S SAMPLE READS INK on the drawn card and the drawn front', JSON.stringify({ card: R1.sample, front: Fs.sample }));
  if (errs.length) console.log('   page errors: ' + errs.join(' | '));
  console.log(`\n══ RESULT: ${pass} pass, ${fail} FAIL ══`); await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error('HARNESS ERROR', e); process.exit(2); });
