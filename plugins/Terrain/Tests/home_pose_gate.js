// ══════════════════════════════════════════════════════════════════════════════════════════════
//  home_pose_gate.js — fb636 h2: EVERY OTHER ANIMATED ELEMENT RESTS ON ONE DESIGNED HOME POSE.
//
//    NODE_PATH=Tests/node_modules node Tests/home_pose_gate.js [page.html]
//    HOME_SHOTS=<dir>   also write the boot / mid-play / rest screenshots there (h2_*.png)
//
//  Max, 2026-09-12 02:34: "the ARP should not have the dots still in motion when inactive, dots should be organized and
//  stable not frozen on the screen like this same for everything else."  motion_law_gate pins the four FLOW tiles and
//  cards; this gate pins "everything else": each element must MOVE while a note sounds, and after the note stops — at
//  three different points of its motion — and the wind-down has landed, it must be back on ONE picture, exactly: the
//  picture it booted with (its designed home). Byte-identical canvases, the same attributes, CSS breaths paused ON
//  their 0 % keyframe. "Anywhere it happened to stop" is the failure.
//
//  THE STATE UNDER TEST is motion_law_gate's: the C++ ships a frame every 16 ms while notes sound (window.__notesActive=1
//  + its stamp, the LFO truth feed, __tiFrame), then ONE frame with __notesActive=0 and nothing after (idle-skip).
//  Everything after that is the page's own clock. The filter is set to SHIMMER (an animated type, MAG_ANIM), a 3D
//  waterfall with a churn band is on, and the rack holds a Delay, a Saturate (.dst-curve) and a Granular (.grn-wave).
//
//  THE BARS
//   0  THE RIG — every element below is present, visible and inked; the page boots still (0 frames at rest)
//   1  FILTER EMBLEMS — they move while a note sounds; at rest all four are the boot pixels (the DRV wave finishes its
//      cycle onto phase 0 instead of holding the phase motion time stopped on)
//   2  FILTER CURVE (Shimmer, an animated type) — it moves; at rest it is the boot curve: no travelling highlight parked
//      somewhere, the animated shape back on its t = 0 form
//   3  TOPO FIELD — it drifts; at rest it is the boot contour map (not the map motion time stopped on)
//   4  NOISE CLOUD — it drifts and breathes; at rest every particle is on its base spot, breath 1: the boot cloud
//   5  WATERFALL CHURN — the drift line sweeps; at rest it lies on the band's centre (phase 0): the boot picture
//   6  DELAY PULSE — it sweeps; at rest it is hidden at the source with no tap flashing; a hand at rest leaves it there
//   7  LFO CURVE BREATH + SHAPE EMBLEM — the curve breathes while a note sounds; at rest it is PAUSED exactly on its .9
//      pose (the 3.2 s boundary). The little shape emblem scrolls with the LFO phase while playing; at rest the path it
//      shows is its phase-0 home, the boot emblem — not the phase the LFO parked on
//   8  CSS BREATHERS — #env-line, .mod-btn.has-routes, .dst-curve, .grn-wave: breathing while a note sounds; at rest each
//      is paused exactly on its 0 % keyframe (within 0.5 ms of the boundary) with the boot style
//   9  A HAND AT REST DRAWS HOME, AND THEN NOTHING RUNS — a pointer gesture at rest repaints every canvas onto the same
//      home pixels (fb591 — a hand still repaints; fb577 — nothing blank), then 0 painter frames; no page errors
//
//  PROOF THE BARS CAN FAIL (HOME_MUT=…):
//    drv      the DRV emblem back on motion time (it rests on the stop phase)                → 1
//    sweep    the curve's travelling highlight not faded by the envelope                    → 2
//    magtau   the animated filter types hold the phase motion time stopped on               → 2
//    topo     the field drawn at motion time, no home map                                   → 3
//    noise    the cloud's offsets not settled at the landing frame                          → 4
//    churn    the drift line holds its phase (no settle onto the centre)                    → 5
//    dly      the delay pulse back on the wall clock                                        → 6
//    lfobr    the LFO curve paused mid-breath at the park edge (the fb570 rule)             → 7
//    lfoic    the LFO shape emblem left on the phase the LFO parked on                      → 7
//    brsnap   no boundary snap: the breaths held a frame past their pose                    → 7 8
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require ('puppeteer-core');
const fs = require ('fs'), path = require ('path'), os = require ('os');
const ROOT = path.join (__dirname, '..');
const PAGE = process.argv[2] || path.join (ROOT, 'Source/ui/public/index.html');
const MUT = process.env.HOME_MUT || '';
const SHOTS = MUT ? '' : (process.env.HOME_SHOTS || '');
const sleep = (ms) => new Promise (r => setTimeout (r, ms));
let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail; console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync (PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split (f).length - 1;
    if (n !== 1) { console.error ('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice (0, 90)); process.exit (2); }
    src = src.replace (f, t); };
  if (MUT === 'drv')         sub ("tD=(M&&window.__tiWind)?window.__tiWind('em-drv',2*Math.PI/3.2,[[0,0]],M.live):t;", "tD=t;");
  else if (MUT === 'sweep')  sub ("var __hE=curE; if(__hE>0){", "var __hE=1; if(__hE>0){");
  else if (MUT === 'magtau') { sub ("if(d>0) curTF+=d; if(M.m<=0&&!M.live) curTF=0; }", "if(d>0) curTF+=d; }");
                               sub ("var __rw=(MAG_ANIM[type]===1)?1-curE:0;", "var __rw=0;"); }
  else if (MUT === 'topo')   sub ("      var g=topoGrid();", "      var g=sampleGrid(46,34,t);");
  else if (MUT === 'noise')  sub ("var __M2 = window.__tiMot, __me = __M2 ? __M2.e : 1, __rest = !! (__M2 && __M2.m <= 0 && ! __M2.live);",
                                  "var __M2 = window.__tiMot, __me = __M2 ? ((__M2.m > 0 || __M2.live) ? __M2.e : 1) : 1, __rest = false;");
  else if (MUT === 'churn')  { sub ("        if(__M&&__M.m<=0&&!__M.live){ wtWaterfall.chPh[o]=0; __ce=0; }\n", "        if(false){}\n");
                               sub ("Math.sin(2*Math.PI*wtWaterfall.chPh[o])*__ce);", "Math.sin(2*Math.PI*wtWaterfall.chPh[o]));"); }
  else if (MUT === 'dly')    sub ("if(__Md&&__Md.m<=0&&!__Md.live){ st.pulseT=0; __de=0; } else st.pulseT+=dt*__de;", "st.pulseT+=dt; __de=1;");
  else if (MUT === 'lfobr')  { sub ("#mod-engine.mv-idle .mv-stroke:not(.mv-br-run),.mv-ext.mv-idle .es .mv-stroke:not(.mv-br-run){animation-play-state:paused;}",
                                    "#mod-engine.mv-idle .mv-stroke,.mv-ext.mv-idle .es .mv-stroke{animation-play-state:paused;}");
                               sub ("}catch(e){} mvBreath(on); }", "}catch(e){} }"); }
  else if (MUT === 'lfoic')  sub ("sic.innerHTML=iconLive(st.shape, ph);", "sic.innerHTML=icon(st.shape, ph);");
  else if (MUT === 'brsnap') sub ("if (d > 0 && a.currentTime != null) a.currentTime = Math.round(a.currentTime / d) * d; }", "if (false) {} }");
  else { console.error ('unknown HOME_MUT ' + MUT); process.exit (2); }
  console.log ('  (mutation ' + MUT + ' landed)');
  const p = path.join (os.tmpdir (), 'home_pose_mut_' + MUT + '.html'); fs.writeFileSync (p, src); return p;
}

// motion_law_gate's stub, VERBATIM (idle_gesture_gate's, plus the ROBIN cycle and the FLOW feeds)
const STUB = () => {
  window.__SYN = { FLOW_RBN_A: 1, FLOW_RBN_B: 1, FLOW_RBN_C: 1, FLOW_RBN_D: 1, FLOW_RBN_O1: 0, FLOW_RBN_O2: 1 / 3, FLOW_RBN_O3: 2 / 3, FLOW_RBN_O4: 1,
                   FLOW_RBN_MODE: 0, FLOW_RBN_AFIRST: 0 };
  window.__VALS = {}; window.__GESTN = 0;
  window.__FEEDS = { getArpFeed: '{"on":0}', getGliFeed: '{"on":0}', getRbnFeed: '{"on":0}', getChopFeed: '{"on":0}' };
  const states = {};
  const mk = (id) => states[id] || (states[id] = (function () {
    const L = []; const n = () => L.slice().forEach (f => { try { f (); } catch (e) {} });
    return { get scaledValue(){ return window.__VALS[id]!=null?window.__VALS[id]:0.5; },
      getScaledValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
      getNormalisedValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
      setScaledValue(v){ window.__VALS[id]=v; n(); }, setNormalisedValue(v){ window.__VALS[id]=v; n(); },
      getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
      valueChangedEvent:{addListener(f){L.push(f);return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}}; })());
  window.Juce = { getSliderState:mk, getToggleState:mk, getComboBoxState:mk,
    getNativeFunction:(nm)=>(...a)=>new Promise(r=>{ if(nm==='uiGesture') window.__GESTN++;
      if(window.__FEEDS&&window.__FEEDS[nm]!=null)return r(window.__FEEDS[nm]);
      if(nm==='getSynParam'&&window.__SYN&&window.__SYN[a[0]]!=null)return r(window.__SYN[a[0]]);
      if(/getPresets/i.test(nm))return r('[]');
      if(/getWaterfallView/i.test(nm))return r('{}');
      if(/Json|JSON|getOscWavetable|SamplePayload/i.test(nm))return r('{}'); r(0); }),
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}} };
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction,
      getSliderState:mine.getSliderState,getToggleState:mine.getToggleState,getComboBoxState:mine.getComboBoxState});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
};

(async () => {
  const b = await puppeteer.launch ({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage ();
  await p.setViewport ({ width: 820, height: 656, deviceScaleFactor: 1 });
  const errs = []; p.on ('pageerror', e => errs.push ((String (e) + ' @ ' + String ((e && e.stack) || '').split ('\n').slice (1, 3).join (' | ')).slice (0, 300)));
  await p.evaluateOnNewDocument (STUB);
  await p.goto ('file://' + mutatedPage (), { waitUntil: 'load', timeout: 60000 });
  await sleep (2000);
  await p.evaluate (() => { const sp = document.getElementById ('syn-panel');
    if (sp) { sp.classList.remove ('hidden'); sp.style.display = 'block'; }
    try { document.getElementById ('syn-btn').click (); } catch (e) {} window.dispatchEvent (new Event ('resize')); });
  await sleep (2600);

  // ── the rig ──────────────────────────────────────────────────────────────────────────────
  const rig = await p.evaluate (() => { const o = {};
    /* the LFO plays an ASSIGNED LFO (lfo_park's rig: this stub has no matrix, and an unrouted LFO rests on purpose, fb628) */
    window.__tiLfoRouted = function () { return true; };
    /* the filter on SHIMMER — an ANIMATED type (MAG_ANIM): its curve moves with the motion clock. Through the slider state,
       so its listeners run (the header names the engine) exactly as a host write would. */
    try { window.Juce.getSliderState ('SYN_FILTER1_TYPE').setNormalisedValue (12 / 117); } catch (e) { window.__VALS['SYN_FILTER1_TYPE'] = 12 / 117; }
    /* the LFO on SINE: the stub's 0.5 default lands on S&H, whose panel draws no breathing curve. The shape's listener
       re-renders the panel (pull + render), as a host write would. */
    ['LFO1_SHAPE', 'SYN_LFO1_SHAPE'].forEach (id => { try { window.Juce.getSliderState (id).setNormalisedValue (0); } catch (e) {} });
    o.lfoSvg = [...document.querySelectorAll ('#mod-engine svg path, #mod-engine svg polyline')].map (e => e.getAttribute ('class') || e.tagName).slice (0, 8).join (',');
    /* a 3D waterfall with a churn band (motion_law_gate bar 8's rig) */
    try { const W = window.wtWaterfall, dev = document.querySelector ('#syn-panel .device.osc');
      dev.classList.remove ('engine-sample', 'engine-granular', 'engine-geode', 'engine-modal', 'engine-fm', 'swapped'); dev.classList.add ('engine-harm');
      const sel = dev.querySelector ('.hm-mode-select'); if (sel) { sel.value = '6'; sel.dispatchEvent (new Event ('change', { bubbles: true })); }
      const N = 16, P = 160, D = new Float32Array (N * P);
      for (let f = 0; f < N; ++f) for (let i = 0; i < P; ++i) D[f * P + i] = 0.9 * Math.sin (2 * Math.PI * (1 + f) * i / (P - 1));
      W.cache['a'] = { n: N, p: P, nf: N, d: D }; W.prev['a'] = null; W.off['a'] = null;
      delete window.__wtDisp; delete window.__harmHueEff;
      window.__harmChurnBand = [0.2, 0, 0, 0]; window.__harmChurnRate = [1.5, 0, 0, 0];
      W.on['a'] = true; W.syncClass ('a'); W.kick (); } catch (e) { o.wfErr = String (e); }
    /* the rack: a Delay (the pulse), a Saturate (.dst-curve) and a Granular (.grn-wave) */
    try { ['delay', 'saturate', 'granular'].forEach (k => window.__fxAdd (k)); window.__fx4Tick && window.__fx4Tick (); } catch (e) { o.rackErr = String (e); }
    /* the header's mod button breathes only with routes, which this stub cannot make: a probe button wears the same
       classes, so the same CSS rule and the same breather code run on it */
    const mb = document.createElement ('button'); mb.id = 'h2-modbtn'; mb.className = 'mod-btn has-routes'; mb.textContent = 'H2';
    mb.style.cssText = 'position:absolute;left:520px;top:13px;'; (document.getElementById ('header') || document.body).appendChild (mb);
    /* the probe painter, the keepalive, and the C++'s play / stop (the LFO truth feed rides every play frame) */
    window.__PROBE = 0; window.__tiFrameReg ('__h2probe__', function () { window.__PROBE++; });
    window.__KA = setInterval (() => { try { window.__tiAlive && window.__tiAlive (); } catch (e) {} }, 500);
    window.__PLAY = 0; window.__LPH = 0;
    window.__play = function () { window.__notesActive = 1; window.__notesActiveT = Date.now ();
      window.__LPH = (window.__LPH + 0.012) % 1; const Z = () => [0, 0, 0, 0, 0, 0, 0, 0, 0, 0], P = Z (), L = Z (); P[0] = window.__LPH; L[0] = 0.5;
      try { window.__modViz ([], L, P); } catch (e) {} window.__tiFrame (); };
    window.__startPlay = function () { if (window.__PLAY) return; window.__PLAY = setInterval (window.__play, 16); window.__play (); };
    window.__stopPlay = function () { clearInterval (window.__PLAY); window.__PLAY = 0;
      window.__notesActive = 0; window.__notesActiveT = Date.now (); window.__tiFrame (); };
    /* every element's drawn state */
    window.__h2hash = (cv) => { if (! cv || ! cv.width) return 'none'; const d = cv.getContext ('2d').getImageData (0, 0, cv.width, cv.height).data;
      let a = 2166136261 >>> 0, ink = 0; for (let i = 0; i < d.length; ++i) { a = Math.imul (a ^ d[i], 16777619) >>> 0; if ((i & 3) === 3 && d[i]) ++ink; } return a.toString (16) + '/' + ink; };
    window.__h2snap = () => { const H = window.__h2hash, q = (s) => document.querySelector (s);
      const an = (el, name) => { if (! el) return null; const a = el.getAnimations ().find (x => x.animationName === name); if (! a) return { none: true };
        const d = +a.effect.getTiming ().duration, ct = +a.currentTime, m = ((ct % d) + d) % d, cs = getComputedStyle (el);
        return { st: a.playState, ct: +ct.toFixed (3), off: +Math.min (m, d - m).toFixed (3), op: +(+cs.opacity).toFixed (4), bs: cs.boxShadow }; };
      const dly = q ('#fxr-rack .fxr-core[data-core="delay"]'), pl = dly && dly.querySelector ('.dly-pulse'), bl = dly && dly.querySelector ('.fxr-dly-bloom');
      return { em: [...document.querySelectorAll ('#syn-panel .fk-em canvas')].map (H), flt: H (q ('#filter-device .filt-bg canvas')),
        topo: H (q ('#reso-cv')), noise: H (q ('#noise-viz-cv')), wf: H (q ('#osc-wave-a')),
        wfPh: window.wtWaterfall ? +(+window.wtWaterfall.chPh.a).toFixed (6) : null,
        dly: pl ? { x: pl.getAttribute ('x1'), op: pl.getAttribute ('opacity'), taps: [...dly.querySelectorAll ('.dtap')].map (t => t.getAttribute ('opacity')).join (' '), bloom: bl ? bl.getAttribute ('opacity') : null } : null,
        lfo: an (q ('#mod-engine .mv-stroke'), 'mvBreathe'),
        /* the shape emblem's VISIBLE path(s): what the eye gets (a dissolve's hidden layer is not the picture) */
        lfoIc: [...document.querySelectorAll ('#mod-engine #mv-shape .ic path')].filter (e => +getComputedStyle (e).opacity > 0.5).map (e => e.getAttribute ('d')).join ('|'),
        br: { env: an (q ('#syn-panel #env-line'), 'envBreathe'), mod: an (q ('#h2-modbtn'), 'mod-btn-breathe'),
              dst: an (q ('#syn-panel .fxr-core .dst-curve'), 'mvBreathe'), grn: an (q ('#syn-panel .fxr-core .grn-wave'), 'mvBreathe') } }; };
    o.vis = ['reso-cv', 'noise-viz-cv', 'fxr-rack', 'mod-engine'].filter (id => window.__tiOff && window.__tiOff (id));
    const cv = document.getElementById ('osc-wave-a'); o.wfVis = !! (cv && cv.offsetParent !== null) && !! (window.wtWaterfall && window.wtWaterfall.isHarmTable ('a'));
    o.flt = window.__fltRoster ? window.__fltRoster.index (12 / 117) : -1;
    return o; });
  await p.evaluate (() => { window.__tiRepaint && window.__tiRepaint (); });
  await sleep (1500);
  const count = async (ms) => { await p.evaluate (() => { window.__PROBE = 0; }); await sleep (ms); return p.evaluate (() => window.__PROBE); };
  /* wait until nothing winds (≤ 8 s: the longest home tail is ~4.7 s), then past the breathers' ending (≤ 3.6 s) */
  const settle = async () => { for (let i = 0; i < 160 && await p.evaluate (() => window.__tiWindBusy () || (window.__tiMot && window.__tiMot.m > 0)); i++) await sleep (50);
    await sleep (3900); };
  const snap = () => p.evaluate (() => window.__h2snap ());
  const shot = async (name) => { if (SHOTS) { fs.mkdirSync (SHOTS, { recursive: true }); await p.screenshot ({ path: path.join (SHOTS, 'h2_' + name + '.png') }); } };

  console.log (`\n══ home_pose_gate — fb636 h2 ══  ${PAGE}${MUT ? '   [MUTATION ' + MUT + ']' : ''}\n`);
  const bootF = await count (1000);
  const HOME = await snap ();
  await shot ('boot_home');
  const inked = (h) => typeof h === 'string' && h !== 'none' && ! /\/0$/.test (h);
  const brPresent = Object.keys (HOME.br).filter (k => HOME.br[k] && ! HOME.br[k].none);
  gate (bootF === 0 && ! rig.vis.length && rig.wfVis && rig.flt === 12 && HOME.em.length === 4 && HOME.em.every (inked) && inked (HOME.flt) && inked (HOME.topo)
        && inked (HOME.noise) && inked (HOME.wf) && !! HOME.dly && HOME.lfo && ! HOME.lfo.none && brPresent.length === 4 && ! rig.wfErr && ! rig.rackErr,
        '[0] THE RIG — every element present, visible and inked; the page boots still',
        `${bootF} painter frames in 1 s at boot · hidden: ${rig.vis.join (',') || 'none'} · waterfall live ${rig.wfVis} · filter type index ${rig.flt} (12 = Shimmer) · `
      + `emblems ${HOME.em.length} · inks flt ${HOME.flt} topo ${HOME.topo} noise ${HOME.noise} wf ${HOME.wf} · delay pulse ${JSON.stringify (HOME.dly)} · `
      + `LFO breath ${JSON.stringify (HOME.lfo)} · breathers ${brPresent.join (',')}${rig.wfErr ? ' · wf ' + rig.wfErr : ''}${rig.rackErr ? ' · rack ' + rig.rackErr : ''}`);

  // ── three bursts of different lengths → silence → the landing ─────────────────────────────────────────────────
  const PLAYS = [], RESTS = [];
  for (const [i, ms] of [[1, 1300], [2, 2150], [3, 2950]]) {
    await p.evaluate (() => window.__startPlay ());
    await sleep (900); PLAYS.push (await snap ()); await shot ('play' + i);
    await sleep (ms - 900);
    await p.evaluate (() => window.__stopPlay ());
    await settle ();
    const r = await snap (); await shot ('rest_after_stop' + i);
    await p.evaluate (() => { window.__tiRepaint && window.__tiRepaint (); }); await sleep (300);
    RESTS.push ({ r, rr: await snap () }); }

  const J = JSON.stringify;
  const moved = (f) => PLAYS.some (s => J (f (s)) !== J (f (HOME)));
  const restHome = (f) => RESTS.every (x => J (f (x.r)) === J (f (HOME)) && J (f (x.rr)) === J (f (HOME)));
  const restTxt = (f) => RESTS.map ((x, i) => 'stop ' + (i + 1) + ' ' + (J (f (x.r)) === J (f (HOME)) ? '=' : '≠ ' + J (f (x.r)).slice (0, 60)) + (J (f (x.rr)) === J (f (HOME)) ? '' : ' (repaint ≠)')).join (' · ');
  const row = (n, name, f, what) => gate (moved (f) && restHome (f), n + ' ' + name,
    `moves while a note sounds ${moved (f)} · home ${J (f (HOME)).slice (0, 70)} · ${restTxt (f)}${what ? ' · ' + what : ''}`);

  row ('[1]', 'FILTER EMBLEMS — at rest all four are the boot pixels (the DRV wave lands on phase 0)', s => s.em);
  row ('[2]', 'FILTER CURVE (Shimmer, animated) — at rest the boot curve: no parked highlight, the t = 0 shape', s => s.flt);
  row ('[3]', 'TOPO FIELD — at rest the boot contour map', s => s.topo);
  row ('[4]', 'NOISE CLOUD — at rest every particle on its base spot, breath 1: the boot cloud', s => s.noise);
  row ('[5]', 'WATERFALL CHURN — at rest the drift line on the band\'s centre, phase 0', s => [s.wf, s.wfPh], 'phases at rest ' + RESTS.map (x => x.r.wfPh).join (' '));

  // ── 6 · the delay pulse, and a hand at rest ──────────────────────────────────────────────────────────────────
  const dlyMoved = PLAYS.some (s => s.dly && (s.dly.x !== HOME.dly.x || s.dly.op !== HOME.dly.op));
  const dlyHome = RESTS.every (x => J (x.r.dly) === J (HOME.dly) && J (x.rr.dly) === J (HOME.dly));
  await p.evaluate (() => { window.__PROBE = 0; document.dispatchEvent (new PointerEvent ('pointerdown', { bubbles: true })); });
  await sleep (400);
  await p.evaluate (() => document.dispatchEvent (new PointerEvent ('pointerup', { bubbles: true })));
  await sleep (1200);
  const handF = await p.evaluate (() => window.__PROBE);
  const HAND = await snap ();
  gate (dlyMoved && dlyHome && J (HAND.dly) === J (HOME.dly) && HOME.dly.op === '0.00' && HOME.dly.x === '6.5',
        '[6] DELAY PULSE — it sweeps while a note sounds; at rest hidden at the source, no tap flashing, and a hand leaves it there',
        `home ${J (HOME.dly)} · playing ${PLAYS.map (s => s.dly && s.dly.x + '@' + s.dly.op).join (' ')} · ${RESTS.map ((x, i) => 'stop ' + (i + 1) + (J (x.r.dly) === J (HOME.dly) ? ' =' : ' ≠ ' + J (x.r.dly))).join (' · ')} · `
      + `after a hand at rest (${handF} frames) ${J (HAND.dly) === J (HOME.dly) ? '=' : '≠ ' + J (HAND.dly)}`);

  // ── 7 · the LFO curve's breath ─────────────────────────────────────────────────────────────────────────────────
  const onPose = (a, op) => !! a && a.st === 'paused' && a.off < 0.5 && (op == null || Math.abs (a.op - op) < 0.002);
  const lfoRan = PLAYS.some (s => s.lfo && s.lfo.st === 'running');
  const lfoRest = RESTS.every (x => onPose (x.r.lfo, 0.9) && onPose (x.rr.lfo, 0.9));
  const icMoved = PLAYS.some (s => s.lfoIc !== HOME.lfoIc), icHome = !! HOME.lfoIc && RESTS.every (x => x.r.lfoIc === HOME.lfoIc && x.rr.lfoIc === HOME.lfoIc);
  gate (lfoRan && lfoRest && onPose (HOME.lfo, 0.9) && icMoved && icHome,
        '[7] LFO CURVE BREATH + SHAPE EMBLEM — the curve paused EXACTLY on its .9 pose, the emblem back on its phase-0 home',
        `boot ${J (HOME.lfo)} · playing ${PLAYS.map (s => s.lfo && s.lfo.st).join (' ')} · rests ${RESTS.map (x => x.r.lfo && (x.r.lfo.st + ' ' + x.r.lfo.off + ' ms off, op ' + x.r.lfo.op)).join (' · ')} · `
      + `emblem: scrolls while playing ${icMoved} · at rest ${RESTS.map ((x, i) => 'stop ' + (i + 1) + (x.r.lfoIc === HOME.lfoIc ? ' =' : ' ≠ ' + String (x.r.lfoIc).slice (0, 40))).join (' · ')} (home ${String (HOME.lfoIc).slice (0, 40)}…)`);

  // ── 8 · the CSS breathers ──────────────────────────────────────────────────────────────────────────────────────
  const BK = ['env', 'mod', 'dst', 'grn'];
  const brRan = BK.every (k => PLAYS.some (s => s.br[k] && s.br[k].st === 'running'));
  const brRest = BK.every (k => onPose (HOME.br[k]) && RESTS.every (x => onPose (x.r.br[k]) && x.r.br[k].op === HOME.br[k].op && x.r.br[k].bs === HOME.br[k].bs));
  gate (brRan && brRest, '[8] CSS BREATHERS — each breathes while a note sounds and rests paused EXACTLY on its 0 % keyframe',
        BK.map (k => `${k}: home ${HOME.br[k] && HOME.br[k].op}${k === 'mod' ? ' / ' + (HOME.br[k] && HOME.br[k].bs) : ''} · playing ${PLAYS.map (s => s.br[k] && s.br[k].st).join (' ')} · rests `
          + RESTS.map (x => x.r.br[k] && (x.r.br[k].st + ' ' + x.r.br[k].off + ' ms off' + (x.r.br[k].op === HOME.br[k].op && x.r.br[k].bs === HOME.br[k].bs ? '' : ' STYLE ' + x.r.br[k].op + ' ' + x.r.br[k].bs))).join (', ')).join (' | '));

  // ── 9 · a hand at rest draws home; then nothing runs ───────────────────────────────────────────────────────────
  const canv = (s) => [s.em, s.flt, s.topo, s.noise, s.wf];
  const afterF = await count (1000);
  gate (handF > 0 && J (canv (HAND)) === J (canv (HOME)) && afterF === 0 && errs.length === 0,
        '[9] A HAND AT REST DRAWS HOME, AND THEN NOTHING RUNS — no page errors',
        `a gesture at rest painted ${handF} frames, every canvas ${J (canv (HAND)) === J (canv (HOME)) ? 'on the home pixels' : 'NOT home: ' + J (canv (HAND)).slice (0, 120)} · `
      + `${afterF} painter frames in the next 1 s · page errors ${errs.length ? errs.slice (0, 2).join (' | ') : 0}`);

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close ();
  process.exit (fail === 0 ? 0 : 1);
}) ().catch (e => { console.error (e); process.exit (1); });
