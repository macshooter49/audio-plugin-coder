// ══════════════════════════════════════════════════════════════════════════════════════════════
//  canvas_alive_gate.js — fb577: NO CANVAS ANYWHERE IS BLANK WHEN THE FRAME CLOCK IS STOPPED.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/canvas_alive_gate.js [page.html]
//
//  Max, after fb576 fixed the tape machines: "make sure that this doesn't happen with anything else. We want
//  these to always be drawn on even though they can stop the animation loop. We still want the visual to pop
//  up and appear there... especially with this happening on an open release."
//
//  fb576 wired the two tape faces BY HAND. The hole is generic: a canvas is blank until a painter draws it,
//  painters run only on a shipped frame, and no frame is shipped at rest (fb566/fb567). MEASURED on the
//  installed AU (c27581a, no note, frames 0): after leaving the front page and coming back the HERO
//  #terrain-canvas 820x276 was blank, with spaceVisCanvas, the four Space knob icons, reso-cv, noise-viz-cv,
//  the filter device's five canvases and a new card's .fx-spec. Boot paints once (restore pushes still ship
//  frames), so nothing is blank AT boot — everything shown, re-shown or resized after it was.
//  This gate walks the whole UI at rest and asserts the picture is there, and that the cure costs no idle frame.
//
//  THE METHOD: the host at rest is simulated — the C++ keepalive stamped every 50 ms AND __tiQuiet pushed, which
//  is what arms the two rest latches (fb508 __heroDrawn, fb510 __fxRestDrawn); without the flag they never engage. So the pushless
//  fallback never runs the painters). At each place every VISIBLE canvas is sampled; then frames are FORCED
//  and it is sampled again. ink-at-rest 0 with ink-once-frames-run > 0 is the failure: the picture exists and
//  only the clock draws it. Blank both ways is a signal display with no signal — correct, and not chased.
//
//  THE BARS
//   1  THE SIMULATION HOLDS — settled, then no gesture for 2 s: 0 frames dispatched.
//   2  THE WHOLE UI AT REST — front page (FX · TAPE / SPACE / CHORUS, the LOOP sub-tab), the SYNTH, EQ, DELAY
//      and MOD panels, and one rack card of all 16 kinds: NOTHING blank at rest.
//   3  A WIPED CANVAS HEALS — a rack card canvas, the hero, and #output-icon-canvas (drawn ONCE at boot by a
//      plain function no painter calls: a heal redraws those by name) are back within 1.4 s of being wiped at rest.
//   4  A GESTURE WAKES FRAMES, AND THEN STOPS — one click dispatches 1..6 frames, and 1.5 s later no more.
//   5  A BLANK DISPLAY IS NOT CHASED — a canvas nobody paints is offered ONE repaint and then left alone:
//      over 3 s it draws at most 1 further frame. (This is what keeps idle at zero.)
//   6  STILL ZERO AT IDLE — after all of it, 2 s with no gesture: 0 frames.
//
//  PROOF THE BARS CAN FAIL:  CANVAS_ALIVE_MUTATE=1 (no gesture wake)        → 2, 4 red
//                            CANVAS_ALIVE_MUTATE=2 (no watchdog)            → 3 red
//                            CANVAS_ALIVE_MUTATE=3 (the watchdog chases)    → 5, 6 red
//                            CANVAS_ALIVE_MUTATE=4 (both rest latches kept)  → 2 red
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
  await pg.evaluateOnNewDocument(() => { setInterval(() => { try { window.__tiQuiet = 1; window.__tiAlive && window.__tiAlive(); } catch (e) {} }, 50); });
  await pg.goto('file://' + P, { waitUntil:'load', timeout:60000 }); await sleep(1800);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late'); document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block'; try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await sleep(600);
  return { b, pg, errs };
}



const MUT = +(process.env.CANVAS_ALIVE_MUTATE || 0);
const KINDS = ['reverb','delay','saturate','granular','tape','flt','cho','fla','pha','eqz','wid','cmp','ott','bod','utl','spl'];
function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split(f).length - 1; if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); } src = src.replace(f, t); console.log('   mutation ' + MUT + ' applied'); };
  if (MUT === 1) sub("function wakeCounted(why){ window.__tiWakes++; if (why) window.__tiWhy[why]++; wakeBurst(); }", "function wakeCounted(why){ window.__tiWakes++; if (why) window.__tiWhy[why]++; }");
  if (MUT === 2) sub(".forEach(function(fn){ if (typeof window[fn] === 'function') window[fn](); }); }catch(e){}\n        wake(); }", ".forEach(function(fn){ if (typeof window[fn] === 'function') window[fn](); }); }catch(e){}\n        }");   /* the watchdog finds the blank canvas and repairs nothing */
  if (MUT === 3) sub("if (cv.__tiOffer === key) continue;   /* offered already in this exact state: not chased again */", "if (false) continue;");
  if (MUT === 4) sub("wokeAt = t; window.__fxRestDrawn = false; window.__heroDrawn = false; runOnce(t);", "wokeAt = t; runOnce(t);");
  const p = path.join(os.tmpdir(), 'canvas_alive_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}
(async () => {
  const P = mutatedPage();
  const { b, pg, errs } = await boot(P);
  console.log('\n══ fb577 — NO CANVAS IS BLANK WHEN THE CLOCK IS STOPPED ══\n   page ' + P + (MUT ? '   MUTATION ' + MUT : '') + '\n');
  await pg.evaluate(() => { window.__probeFrames = 0; window.__tiFrameReg('probe', () => { window.__probeFrames++; }); });
  const frames = () => pg.evaluate(() => window.__probeFrames | 0);
  const SAMPLE = () => pg.evaluate(() => {
    const out = [];
    document.querySelectorAll('canvas').forEach((cv) => {
      if (cv.__gateOwn) return;
      if (! window.__tiCvVisible(cv)) return;
      let n = 0; try { const d = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height).data; for (let p = 3; p < d.length; p += 4) if (d[p] > 8) n++; } catch (e) { return; }
      let who = cv.id || '';
      if (! who) { const c = cv.className && cv.className.baseVal !== undefined ? cv.className.baseVal : cv.className; who = '.' + String(c || 'bare').trim().split(/\s+/)[0];
        const dev = cv.closest('.fxr-dev'); if (dev) who += '[' + (dev.getAttribute('data-type') || dev.getAttribute('data-dev') || '?') + ']';
        const host = cv.closest('[id]'); if (host && host.id) who += '<' + host.id + '>'; }
      out.push({ who, n });
    });
    return out;
  });
  const force = async (n) => { for (let i = 0; i < n; i++) { await pg.evaluate(() => { window.__tiRepaint && window.__tiRepaint(); }); await sleep(18); } };
  // 1 — the simulation
  await sleep(3000);   /* the boot settle: the watchdog offers a repaint to each canvas that booted blank, once */
  const f0 = await frames(); await sleep(2000); const f1 = await frames();
  chk(f1 - f0 === 0, '1  THE SIMULATION HOLDS: settled, then no gesture for 2 s → 0 frames dispatched', 'frames ' + (f1 - f0));
  // 2 — the whole UI
  const blanks = []; let places = 0, seen = 0;
  async function place (label, setup) {
    await setup(); await sleep(900); places++;
    const A = await SAMPLE(); seen += A.length;
    await force(20); await sleep(120);
    const B = new Map((await SAMPLE()).map(o => [o.who, o.n]));
    A.filter(o => o.n === 0 && B.get(o.who) > 0).forEach(o => blanks.push(label + ' :: ' + o.who + ' (0 → ' + B.get(o.who) + ')'));
  }
  const click = (sel) => pg.evaluate((s) => { const e = document.querySelector(s); if (e) e.click(); }, sel);
  await place('FRONT · TAPE', () => click('#syn-btn'));
  await place('FRONT · SPACE', () => click('#fxNavNext'));
  await place('FRONT · CHORUS', () => click('#fxNavNext'));
  await place('FRONT · LOOP', () => click('.tape-subtab[data-tab="loop"]'));
  await place('FRONT · FX again', () => click('.tape-subtab[data-tab="fx"]'));
  await place('SYNTH', () => click('#syn-btn'));
  await place('EQ', () => click('#eq-btn'));
  await place('DELAY', () => click('#delay-btn'));
  await place('MOD', () => click('#mod-btn'));
  await place('SYNTH again', () => click('#syn-btn'));
  for (const k of KINDS) await place('RACK ' + k, () => pg.evaluate((k) => { window.__fxAdd(k); window.__fx4Tick(); }, k));
  chk(blanks.length === 0, '2  THE WHOLE UI AT REST: nothing blank across ' + places + ' places (' + seen + ' visible-canvas samples: the front\'s 3 FX pages + LOOP, the SYNTH/EQ/DELAY/MOD panels, all 16 rack kinds)', blanks.length ? '\n        ' + blanks.join('\n        ') : 'zero blank');
  // 3 — self-heal.  A canvas the open panel COVERS is correctly not painted (fb487), so each wipe is
  //     done with its own surface actually on screen: the rack with the synth panel open, the hero with it shut.
  const setPanel = async (want) => { for (let i = 0; i < 3; i++) { const p = await pg.evaluate(() => (typeof currentActivePanel !== 'undefined' ? currentActivePanel : null)); if (p === want) return p; await click('#syn-btn'); await sleep(700); } return await pg.evaluate(() => (typeof currentActivePanel !== 'undefined' ? currentActivePanel : null)); };
  const pOpen = await setPanel('syn'); await sleep(400);
  const wipe = (sel) => pg.evaluate((s) => { const c = document.querySelector(s); if (c) c.width = c.width; }, sel);
  const inkOf = async (sel) => (await pg.evaluate((s) => { const c = document.querySelector(s); if (!c) return -1; try { const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0; for (let p = 3; p < d.length; p += 4) if (d[p] > 8) n++; return n; } catch (e) { return -1; } }, sel));
  await wipe('#fxr-rack .fxr-core[data-core="tape"] canvas'); const rw = await inkOf('#fxr-rack .fxr-core[data-core="tape"] canvas');
  await sleep(1400); const rh = await inkOf('#fxr-rack .fxr-core[data-core="tape"] canvas');
  const pShut = await setPanel(null); await sleep(400);
  await wipe('#terrain-canvas'); await wipe('#output-icon-canvas');   /* the hero is painted by a frame painter; the output icon is drawn ONCE at boot by a plain function — both must come back */
  const hw = await inkOf('#terrain-canvas'), ow = await inkOf('#output-icon-canvas');
  await sleep(1400); const hh = await inkOf('#terrain-canvas'), oh = await inkOf('#output-icon-canvas');
  chk(pOpen === 'syn' && pShut === null && rw === 0 && rh > 0 && hw === 0 && hh > 0 && ow === 0 && oh > 0, '3  A WIPED CANVAS HEALS within 1.4 s at rest: a rack card canvas (panel open), the hero, and the boot-drawn output icon (no painter owns it)', 'rack 0 → ' + rh + ' · hero 0 → ' + hh + ' · output icon 0 → ' + oh + ' · panels ' + pOpen + '/' + pShut);
  // 4 — a gesture wakes frames and then stops
  const g0 = await frames(); await click('#eq-btn'); await sleep(900); const g1 = await frames(); await sleep(1500); const g2 = await frames();
  chk(g1 - g0 >= 1 && g1 - g0 <= 6 && g2 - g1 === 0, '4  A GESTURE WAKES FRAMES, AND THEN STOPS: one click → 1..6 frames, then 1.5 s of silence', 'click ' + (g1 - g0) + ' frames, then ' + (g2 - g1));
  // 5 — a blank display nobody paints is offered ONE repaint, never chased
  await pg.evaluate(() => { const c = document.createElement('canvas'); c.__gateOwn = true; c.width = 120; c.height = 60; c.style.cssText = 'position:fixed;left:8px;top:8px;width:120px;height:60px;z-index:9;'; document.body.appendChild(c); window.__gateCv = c; });
  await sleep(1300); const h0 = await frames(); await sleep(3000); const h1 = await frames();
  chk(h1 - h0 <= 1, '5  A BLANK DISPLAY IS NOT CHASED: a canvas nobody paints is offered one repaint, then left alone (≤ 1 frame in 3 s)', 'frames in 3 s ' + (h1 - h0));
  await pg.evaluate(() => { if (window.__gateCv) window.__gateCv.remove(); });
  // 6 — still zero at idle
  await sleep(1200); const i0 = await frames(); await sleep(2000); const i1 = await frames();
  chk(i1 - i0 === 0, '6  STILL ZERO AT IDLE after all of it: 2 s with no gesture → 0 frames', 'frames ' + (i1 - i0));
  if (errs.length) console.log('   page errors: ' + errs.slice(0, 3).join(' | '));
  console.log(`\n══ RESULT: ${pass} pass, ${fail} FAIL ══`); await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error('HARNESS ERROR', e); process.exit(2); });
