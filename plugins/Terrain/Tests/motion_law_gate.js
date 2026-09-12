// ══════════════════════════════════════════════════════════════════════════════════════════════
//  motion_law_gate.js — fb636: EVERY ANIMATION WINDS UP, WINDS DOWN, AND ONLY MOVES WHILE MIDI SOUNDS.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/motion_law_gate.js [page.html]
//
//  Max: "the four modes that we have, the flow cards — I want those to have an ending animation and a
//  startup animation. The startup animation is when the MIDI is being brought in and it plays, it starts
//  to move. The ending animation — to stop it from just staying plain static still, 'cause it might be in
//  the middle of a frame and just the MIDI stops and it just pauses, I don't want that... make this a hard
//  rule... a wind up and a wind down and then it stops. That's the Serum 2 method. Same with the filter,
//  knobs and everything. Anything that's moving... but we want to keep all the paint there."
//
//  THE STATE UNDER TEST is the plugin exactly as the C++ drives the page: the lane is heard once (fb581),
//  then only the keepalive at rest; while "playing", a shipped frame every 16 ms carries
//  window.__notesActive=1 + its stamp and ends in __tiFrame(); when the notes end, ONE frame carries
//  __notesActive=0 and then NOTHING ships (idle-skip). Every clock after that is the page's own.
//
//  THE BARS
//   0  THE RIG — the motion clock exists, the lane is heard, the page boots still (html.ti-still)
//   1  AT REST NOTHING MOVES AND NOTHING RUNS — 0 painter frames; the tiles' SMIL is paused and holds its
//      time; no CSS animation runs on any tile node; ALL FOUR tiles are drawn on their designed HOME pose
//      (fb636 h1 — boot IS home): ARP six dots at op .75 / scale 1 with the fan solid at .38 · CHOP four dots at
//      .6 with the ball docked on the big step · GLITCH the dice in the top-left cell · ROBIN level, wings up,
//      no wind streaks
//   2  THE WIND-UP — the first frame after the note moves at a fraction of speed and the envelope climbs to
//      full over ~250 ms (not a jump to full speed)
//   3  ALL FOUR TILES MOVE WHILE A NOTE SOUNDS, LIT OR NOT (fb636 h1 — Max, 02:34: "i want them to move
//      regardless of on/off, just about the MIDI") — the lit ARP and the three UNLIT tiles all run their loops
//      at full speed (rest weight 0) and ARP fires its pulses
//   4  🚨 THE ENDING — stopped MID-FLASH, with no frame shipped after the note-off: the tail keeps painting,
//      no tile's loop time ever runs backwards (fb636 h3: the ONLY step back excused is the landing fold — the clock
//      folding from the goal it just reached into its first period, a WHOLE number of the tile's periods; the old
//      excuse, any drop over 1 s that landed below 10, let a real rewind through) or faster than it played, ARP leaves the edge at about the speed
//      it had and decelerates to a stop, every tile lands within the longest home tail (≤ 5.2 s) and ALL FOUR
//      are then EXACTLY on their home pose — not anywhere inside a band, not a frozen mid-motion frame
//   5  AND THEN IT STOPS — zero painter frames afterwards, nothing still winding, and the tail never told
//      the C++ anything (0 uiGesture calls): idle stays idle (fb567)
//   6  🚨 A BREATHER FINISHES ITS BREATH — #env-line stopped at the PEAK of its breath is STILL BREATHING 250 ms later
//      (it finishes the breath, ~1.5 s of it) and then comes to rest paused on its 0 %/100 % pose (opacity .90), not
//      frozen mid-breath (animation-play-state alone does that). fb636 h2: the cap now lands any straggler exactly on
//      the pose, so the end state alone can no longer tell a finished breath from a frozen one that snapped at 3.6 s —
//      the mid-ending sample can
//   7  A REPAINT AT REST DRAWS THE SAME PICTURE — the four filter emblems and the topo landscape, repainted
//      twice 700 ms apart at rest, are byte-identical (on the wall clock every wake drew a new pose)
//   8  THE WATERFALL RESTS — a 3D view with a churn band: parked at rest (0 draws), the drift sweeps while
//      a note plays, then winds down, holds its phase and the loop parks again
//   9  THE SEQUENCER IS UNTOUCHED — the four FLOW cards' chain steps (sc.advance / sc.restart, which write
//      parameters through setSnap), their feed poll and TIC.runLoop are byte-identical to fb635
//  10  THE RESYNTH SCAN RESTS — the editor passes dt 0 to computeDisplayEnvelope while quiet, its image
//      heartbeat counts only while awake, and a reattach re-arms it (wiring tripwire)
//  11  NO PAGE ERRORS
//  12  🚨 HOME IS ONE PICTURE (fb636 h1 — Max: "dots should be organized and stable not frozen on the screen like
//      this") — stopped at three different points of the loops, every tile rests on the SAME pixels (a per-tile
//      screenshot hash) and the same drawn state, and that picture is the boot picture
//  13  THE FOUR FLOW CARDS FOLLOW THE MIDI (fb636 h1) — each card out of the chain, with a feed (the plugin's case):
//      at rest it is still on its home; while a note sounds its display moves (ARP playhead, GLITCH monitor
//      playhead, ROBIN wheel, CHOP playhead + memory ribbon); when the sound ends it walks forward onto its home
//      (ARP step 1 · GLITCH slice 1, Idle, no fire overlay · ROBIN the cycle's first station · CHOP slice 1 at the
//      window's edge with the ribbon waveform back on offset 0) — the very picture it showed before the note
//
//  PROOF THE BARS CAN FAIL (MOTION_MUT=…):
//    noend      the loop freezes where it is when the sound stops (no ending)        → 4 12
//    notail     no page-side tail after the last frame                              → 4
//    nowindup   motion starts at full speed                                         → 2
//    litonly    only the LIT tile moves again (the fb636 rule Max reversed)          → 3
//    anyband    the old rest bands (any pose inside a band) instead of ONE home      → 4 12
//    norest     the rest weight never climbs: each tile freezes on its keyframe pose → 4 12
//    cardrest   the ARP card's clock ignores the MIDI (rests while a note sounds)    → 13
//    cardhome   the CHOP ribbon freezes mid-scroll instead of going home             → 13
//    brfreeze   breathers paused at the edge, mid-breath (the old idiom)            → 6 (paused 250 ms after the stop)
//    wallclock  the filter emblems back on the wall clock                           → 7
//    wfloop     the waterfall loop draws and re-arms forever again                  → 8
//    rewind     (fb636 h3) each ending starts 1.3 s BACK (not a whole period) and eases home from there → 4
//               (under the old fold excuse this passed: the drop is over 1 s and lands below 10)
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require ('puppeteer-core');
const fs = require ('fs'), path = require ('path'), os = require ('os'), crypto = require ('crypto');
const ROOT = path.join (__dirname, '..');
const PAGE = process.argv[2] || path.join (ROOT, 'Source/ui/public/index.html');
const MUT = process.env.MOTION_MUT || '';
const sleep = (ms) => new Promise (r => setTimeout (r, ms));
/* THE DESIGNED HOME POSES (fb636 h1) — what each tile must show at rest, EXACTLY. homeMiss(__tileSnap()) lists what is not
   home ([] = all four tiles home). The browser serialises scale(1.000) as scale(1) and '22.00 0.00' as '22, 0'. */
const near = (a, b, e) => Math.abs (+a - b) <= e;
const scaleOf = (t) => +((/scale\(([-\d.]+)\)/.exec (t || '') || [0, NaN])[1]);
const TILE_KEYS = ['arp', 'chop', 'glitch', 'drift'];
function homeMiss (s) {
  const m = [], A = s.arp, C = s.chop, G = s.glitch, R = s.drift, J = JSON.stringify;
  if (! A || A.notes.length !== 3 || ! A.notes.every (n => near (n[0], 0.75, 0.0005) && near (scaleOf (n[1]), 1, 0.0005))) m.push ('ARP notes ' + J (A && A.notes));
  if (! A || ! A.holds.length || ! A.holds.every (h => near (h, 0.75, 0.0005))) m.push ('ARP holds ' + J (A && A.holds));
  if (! A || A.lines.length !== 3 || ! A.lines.every (l => /^22(\.0+)?,? 0(\.0+)?$/.test (l.da) && /^0(px)?$/.test (l.off) && near (l.so, 0.38, 0.0005))) m.push ('ARP fan ' + J (A && A.lines));
  if (! C || C.sqs.length !== 4 || ! C.sqs.every (q => near (q, 0.6, 0.0005))) m.push ('CHOP dots ' + J (C && C.sqs));
  if (! C || ! C.ball || ! near (C.ball[0], 33, 0.01) || ! near (C.ball[1], 1, 0.001)) m.push ('CHOP ball ' + J (C && C.ball));
  if (! G || ! G.cell || ! near (G.cell[0], 11, 0.001) || ! near (G.cell[1], 11, 0.001)) m.push ('GLITCH cell ' + J (G && G.cell));
  if (! R || ! R.bob || ! near (R.bob[0], 0, 0.001) || ! near (R.bob[1], 0, 0.01) || ! (R.wingY < 18.5)) m.push ('ROBIN bird bob ' + J (R && R.bob) + ' wing top y ' + (R && R.wingY) + ' (up 17.2 / down 20)');
  if (! R || R.wind.length !== 2 || ! R.wind.every (w => w === '0')) m.push ('ROBIN wind ' + J (R && R.wind));
  for (const k of TILE_KEYS) if (! s[k] || s[k].r !== 1 || s[k].parked) m.push (k + ' rest weight ' + (s[k] && s[k].r) + (s[k] && s[k].parked ? ' (still winding)' : ''));
  return m;
}
/* the drawn state without the clock (a home band may rest at several SMIL times that all draw the same picture) */
const pose = (s) => JSON.stringify (TILE_KEYS.map (k => { const x = Object.assign ({}, s[k]); delete x.tau; delete x.ct; delete x.css; delete x.paused; return x; }));
let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail;
  console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync (PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split (f).length - 1;
    if (n !== 1) { console.error ('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice (0, 90)); process.exit (2); }
    src = src.replace (f, t); };
  if (MUT === 'noend')     sub ("    if (! st.park){\n      if (st.m <= 0) return st.tau;", "    if (! st.park){ st.m = 0; return st.tau;\n      if (st.m <= 0) return st.tau;");
  else if (MUT === 'notail')   sub ("  function tailKick(edge){ var t = nowMs();", "  function tailKick(edge){ return; var t = nowMs();");
  else if (MUT === 'nowindup') { sub ("    MOT.m = live ? Math.min(1, MOT.m + dt / 0.25)", "    MOT.m = live ? 1");
                                 sub ("      st.m = Math.min(1, st.m + dt / 0.25); st.tau += dt * rate * sstep(st.m);", "      st.m = 1; st.tau += dt * rate * sstep(st.m);"); }
  else if (MUT === 'litonly')  sub ("T.B, live), r = K[key].r;", "T.B, live && tl.el.classList.contains('act')), r = K[key].r;");
  else if (MUT === 'anyband')  sub ("chop: { P: 2.2, B: [[1.2, 1.52]] },\n                glitch: { P: 9.6, B: [[0, 0.6], [2.2, 2.65], [4.63, 4.95], [7.7, 8.03]] }, drift: { P: 2.4, B: [[1.8, 1.8]] } };",
                                    "chop: { P: 2.2, B: [[1.892, 2.2], [1.188, 1.54]] },\n                glitch: { P: 9.6, B: [[0, 9.6]] }, drift: { P: 2.4, B: [[0, 2.4]] } };");
  else if (MUT === 'norest')   { sub ("    st.r = k.r0 + (1 - k.r0) * sstep(s);\n", "    st.r = k.r0;\n");
                                 sub ("    st.tau = f; st.park = null; st.m = 0; st.r = 1; }", "    st.tau = f; st.park = null; st.m = 0; }"); }
  else if (MUT === 'cardrest') sub ("window.__tiWind('card-arp',N,[[0,0]],window.__tiLive(),sps,true", "window.__tiWind('card-arp',N,[[0,0]],false,sps,true");
  else if (MUT === 'cardhome') { sub ("if(sim.rw>0)v+=(waveAt(x*.09)-v)*sim.rw;", "");
                                 sub ("sim.rw=1-ribE; if(ribE<=0) sim.off=0;", "sim.rw=1-ribE;"); }
  else if (MUT === 'brfreeze') sub ("    brEnding = true;\n", "    brStill(true); brEnding = true;\n");
  else if (MUT === 'wallclock') { sub ("var __M=window.__tiMot, t=__M?__M.tau:ts/1000; curT=t;", "var __M=window.__tiMot, t=ts/1000; curT=t;");
                                  /* fb636 h2 — the DRV emblem left motion time for its own loop clock (it lands on phase 0), and CUT/RES/ENV
                                     are blended out entirely at rest, so the wall clock must reach the DRV clock too or bar 7 cannot go red */
                                  sub ("window.__tiWind('em-drv',2*Math.PI/3.2,[[0,0]],M.live)", "(performance.now()/1000)"); }
  else if (MUT === 'wfloop')   { sub ("          if(force||moved||busy||drift){ wtWaterfall.lastSig[o]=sig;", "          if(true){ wtWaterfall.lastSig[o]=sig;");
                                 sub ("          if(moved||busy||drift||hand||wtWaterfall.shapeSig(o)!==wtWaterfall.cachedSig(o)) any=true; } }", "          any=true; } }"); }
  else if (MUT === 'rewind')   sub ("      var v0 = rate * Math.max(0.05, sstep(st.m)), g = nextRest(st.tau + v0 * 0.35, P, B), D = g - st.tau;",
                                    "      st.tau -= 1.3; var v0 = rate * Math.max(0.05, sstep(st.m)), g = nextRest(st.tau + v0 * 0.35, P, B), D = g - st.tau;");
  else { console.error ('unknown MOTION_MUT ' + MUT); process.exit (2); }
  console.log ('  (mutation ' + MUT + ' landed)');
  const p = path.join (os.tmpdir (), 'motion_law_mut_' + MUT + '.html'); fs.writeFileSync (p, src); return p;
}

// idle_gesture_gate's stub, plus a count of the one native the gesture clock may call (gTell → uiGesture)
const STUB = () => {
  /* bar 13's ROBIN card needs a real cycle: its controls poll getSynParam, which this stub answers 0 — the four FLOW_RBN_A–D
     bank bools read OFF and the cycle is EMPTY (nothing to turn). So getSynParam answers these ten, and only these: all four
     stations on, order A B C D, Cycle mode, A-First off (the first turn after a rest is then not onto the station it shows). */
  window.__SYN = { FLOW_RBN_A: 1, FLOW_RBN_B: 1, FLOW_RBN_C: 1, FLOW_RBN_D: 1, FLOW_RBN_O1: 0, FLOW_RBN_O2: 1 / 3, FLOW_RBN_O3: 2 / 3, FLOW_RBN_O4: 1,
                   FLOW_RBN_MODE: 0, FLOW_RBN_AFIRST: 0 };
  window.__VALS = {}; window.__GESTN = 0;
  window.__FEEDS = { getArpFeed: '{"on":0}', getGliFeed: '{"on":0}', getRbnFeed: '{"on":0}', getChopFeed: '{"on":0}' };   /* fb636 h1 — bar 13: a card out of the chain */
  const states = {};
  const mk = (id) => states[id] || (states[id] = (function () {
    const L = []; const n = () => L.slice().forEach (f => { try { f (); } catch (e) {} });
    return { get scaledValue(){ return window.__VALS[id]!=null?window.__VALS[id]:0.5; },   /* the output strip reads .scaledValue at boot — without it the stub throws a page error that is the harness's, not the page's */
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
    if (sp) { sp.classList.remove ('hidden'); sp.style.display = 'block'; } window.dispatchEvent (new Event ('resize')); });
  await sleep (2600);

  // ── the rig: the lane heard once, the keepalive, a probe painter, the C++'s play/stop, ARP alone lit ──
  await p.evaluate (() => {
    window.__PROBE = 0; window.__REC = 0; window.__TAU = []; window.__MAXOP = 0;
    document.querySelectorAll ('#syn-panel .flow-mode').forEach (t => t.classList.toggle ('act', t.getAttribute ('data-mode') === 'arp'));
    window.__tiFrameReg ('__probe__', function () { window.__PROBE++;
      const s = window.__tiWindState && window.__tiWindState ('tile-arp');
      const n = document.querySelector ('#syn-panel .flow-mode[data-mode="arp"] .arpNote');
      if (n) window.__MAXOP = Math.max (window.__MAXOP, +n.style.opacity || 0);
      if (window.__REC) { const k = (m) => { const q = window.__tiWindState && window.__tiWindState ('tile-' + m); return q ? q.tau : NaN; };
        /* fb636 h3 — columns 6-9: each tile's ending goal (park.g), so bar 4 can tell the landing fold from a rewind */
        const g = (m) => { const q = window.__tiWindState && window.__tiWindState ('tile-' + m); return q && q.park ? q.park.g : NaN; };
        window.__TAU.push ([performance.now (), s ? s.tau : NaN, window.__tiMot ? window.__tiMot.e : NaN, k ('chop'), k ('glitch'), k ('drift'), g ('arp'), g ('chop'), g ('glitch'), g ('drift')]); } });
    try { window.__tiFrame && window.__tiFrame (); } catch (e) {}
    window.__KA = setInterval (() => { try { window.__tiAlive && window.__tiAlive (); } catch (e) {} }, 500);
    window.__PLAY = 0;
    window.__play = function () { window.__notesActive = 1; window.__notesActiveT = Date.now (); window.__tiFrame (); };
    window.__startPlay = function () { if (window.__PLAY) return; window.__PLAY = setInterval (window.__play, 16); window.__play (); };
    window.__stopPlay = function () { clearInterval (window.__PLAY); window.__PLAY = 0;
      window.__notesActive = 0; window.__notesActiveT = Date.now (); window.__tiFrame (); window.__STOPT = performance.now (); };
    /* every tile's drawn state: inline styles, and the SMIL's ANIMATED values (the ball, the dice, the bird) */
    window.__tileSnap = function () { const o = {};
      const tr = (el) => { if (! el) return null; const L = el.transform.animVal; if (! L.numberOfItems) return [0, 0]; const q = L.getItem (0).matrix; return [+q.e.toFixed (4), +q.f.toFixed (4)]; };
      document.querySelectorAll ('#syn-panel .flow-mode').forEach (t => { const m = t.getAttribute ('data-mode'), sv = t.querySelector ('svg');
        let css = 0; t.querySelectorAll ('*').forEach (x => { try { css += x.getAnimations ().filter (a => a.playState === 'running').length; } catch (e) {} });
        const st = window.__tiWindState ('tile-' + m) || { tau: NaN, r: NaN };
        const x = { paused: sv && sv.animationsPaused ? sv.animationsPaused () : null,
                    ct: sv && sv.getCurrentTime ? +sv.getCurrentTime ().toFixed (4) : null, css, tau: st.tau, r: st.r, parked: !! st.park,
                    notes: [...t.querySelectorAll ('.arpNote')].map (n => [n.style.opacity, n.style.transform]),
                    holds: [...t.querySelectorAll ('.arpHold')].map (n => n.style.opacity),
                    lines: [...t.querySelectorAll ('.arpLine')].map (n => ({ da: n.style.strokeDasharray, off: n.style.strokeDashoffset, so: n.style.strokeOpacity })),
                    sqs: [...t.querySelectorAll ('.sq')].map (n => n.style.opacity),
                    wind: [...t.querySelectorAll ('.rbnWind')].map (n => n.style.strokeOpacity) };
        const ball = t.querySelector ('.seqBall'); if (ball) x.ball = [+ball.cx.animVal.value.toFixed (3), +(+getComputedStyle (ball).opacity).toFixed (3)];
        if (m === 'glitch') x.cell = tr (sv.querySelector (':scope > g'));
        if (m === 'drift') { const g = sv.querySelector (':scope > g'), w = g && g.querySelector ('path'); x.bob = tr (g); x.wingY = w ? +w.getBBox ().y.toFixed (3) : NaN; }
        o[m] = x; });
      return o; };
    /* the open FLOW card's display facts (bar 13) */
    window.__cardSnap = function () { const k = [...document.querySelectorAll ('.ti-card.open')].filter (e => /(arp|gli|rbn|chop)-ext/.test (e.className)).pop ();
      if (! k) return null; const o = { card: (/(arp|gli|rbn|chop)-ext/.exec (k.className) || [])[1] }, pl = (sv) => sv && sv.querySelector ('line[stroke="#B794FF"]');
      if (o.card === 'arp') { const l = k.querySelector ('.hero svg line[stroke^="rgba(183,148,255"]'); o.x = l && l.getAttribute ('x1');
        o.led = [...k.querySelectorAll ('.leds i')].findIndex (i => i.classList.contains ('on')); o.home = o.x === '9' && o.led === 0; }
      else if (o.card === 'gli') { const l = pl (k.querySelector ('.graph svg')); o.x = l && l.getAttribute ('x1');
        o.foot = (k.querySelector ('.r-foot') || {}).textContent; o.st = (k.querySelector ('.r-st') || {}).textContent;
        o.fx = (k.querySelector ('.r-fx') || {}).textContent;   /* fb636 h3 — the 'last FX' readout is part of the picture */
        o.home = o.x === '0.0' && o.foot === 'Slice 1 · 16' && o.st === 'Idle'; }
      else if (o.card === 'rbn') { const sv = k.querySelector ('.wheel svg'), l = pl (sv); o.now = (k.querySelector ('.s-now') || {}).textContent;
        o.notes = (k.querySelector ('.s-notes') || {}).textContent;   /* fb636 h3 — the NOTES readout is part of the picture */
        o.arm = l ? l.getAttribute ('x2') + ',' + l.getAttribute ('y2') : null;
        const ring = sv && [...sv.querySelectorAll ('circle')].find (c => /^rgba\(183,148,255/.test (c.getAttribute ('stroke') || '')); o.ring = ring && ring.getAttribute ('r');
        o.home = o.now === 'A' && o.arm === '95.0,29.5' && o.ring === '14.5'; }
      else { const sv = k.querySelector ('.ribbon svg'), l = pl (sv), e = sv && sv.querySelector ('line[stroke-dasharray="3 2"]');
        o.x = l && l.getAttribute ('x1'); o.wl = e && e.getAttribute ('x1');
        o.dot = sv ? [...sv.querySelectorAll ('circle')].findIndex (c => c.getAttribute ('fill') === '#B794FF') : -2;
        o.wave = sv ? [...sv.querySelectorAll ('path')].map (p => p.getAttribute ('d')).join ('|') : '';
        o.home = !! o.x && o.x === o.wl && o.dot === 0; }
      return o; };
  });
  await sleep (2500);
  const count = async (ms) => { await p.evaluate (() => { window.__PROBE = 0; }); await sleep (ms); return p.evaluate (() => window.__PROBE); };
  /* wait until nothing is winding (≤ 8 s: the longest home tail is ~4.7 s) → ms waited */
  const settle = async () => { const t0 = Date.now ();
    for (let i = 0; i < 160 && await p.evaluate (() => window.__tiWindBusy ()); i++) await sleep (50); return Date.now () - t0; };
  /* the four tiles as PIXELS: one screenshot hash per tile (the tiles sit inside the 820x656 viewport) */
  const tileHash = async () => { const o = {};
    for (const m of TILE_KEYS) { const h = await p.$ (`#syn-panel .flow-mode[data-mode="${m}"]`);
      o[m] = h ? crypto.createHash ('sha1').update (await h.screenshot ({ captureBeyondViewport: false })).digest ('hex').slice (0, 12) : 'none'; }
    return o; };

  console.log (`\n══ motion_law_gate — fb636 ══  ${PAGE}${MUT ? '   [MUTATION ' + MUT + ']' : ''}\n`);
  const rig = await p.evaluate (() => ({ mot: !! window.__tiMot, wind: typeof window.__tiWind === 'function',
    live: typeof window.__tiLive === 'function', lane: !! (window.__tiLaneSeen && window.__tiLaneSeen ()),
    still: document.documentElement.classList.contains ('ti-still'), tiles: document.querySelectorAll ('#syn-panel .flow-mode').length }));
  gate (rig.mot && rig.wind && rig.live && rig.lane && rig.still && rig.tiles === 4, '[0] THE RIG — the motion clock, the lane heard, the page boots still',
        `__tiMot ${rig.mot} · __tiWind ${rig.wind} · __tiLive ${rig.live} · lane seen ${rig.lane} · html.ti-still ${rig.still} · tiles ${rig.tiles}`);

  // ── 1 · REST ────────────────────────────────────────────────────────────────────────────────
  const s0 = await p.evaluate (() => window.__tileSnap ());
  const restF = await count (1000);
  const s1 = await p.evaluate (() => window.__tileSnap ());
  const unl = ['chop', 'glitch', 'drift'];
  const smilHeld = unl.every (m => s0[m] && s0[m].paused === true && s0[m].ct === s1[m].ct);
  const cssNone = Object.keys (s1).every (m => s1[m].css === 0);
  const miss1 = homeMiss (s1), HOME = pose (s1), HASH0 = await tileHash ();
  gate (restF === 0 && smilHeld && cssNone && ! miss1.length, '[1] AT REST NOTHING MOVES AND NOTHING RUNS — and all four tiles sit on their HOME pose',
        `${restF} painter frames in 1 s · SMIL paused+held (chop/glitch/robin) ${smilHeld} [${unl.map (m => s0[m] && (s0[m].paused + '@' + s0[m].ct + '→' + s1[m].ct)).join (' ')}] · `
      + `running CSS animations on tile nodes ${Object.keys (s1).map (m => m + ':' + s1[m].css).join (' ')} · home: ${miss1.length ? 'NOT — ' + miss1.join (' · ') : 'all four (ARP .75 + solid fan · CHOP .6 + docked ball · GLITCH top-left · ROBIN level, wings up, no wind)'}`);

  // ── 2 + 3 · WIND-UP, ONLY THE LIT TILE ─────────────────────────────────────────────────────
  await p.evaluate (() => { window.__TAU = []; window.__MAXOP = 0; window.__REC = 1; window.__startPlay (); });
  await sleep (1500);
  const up = await p.evaluate (() => { window.__REC = 0; return { tau: window.__TAU.slice (), maxop: window.__MAXOP }; });
  const s2 = await p.evaluate (() => window.__tileSnap ());
  const T0 = up.tau.length ? up.tau[0][0] : 0;
  const eFirst = up.tau.length ? up.tau[0][2] : NaN;
  const tFull = (up.tau.find (x => x[2] >= 0.999) || [NaN])[0] - T0;
  let mono = true; for (let i = 1; i < up.tau.length; ++i) if (up.tau[i][2] < up.tau[i - 1][2] - 1e-9) mono = false;
  const speedIn = (a, b) => { const w = up.tau.filter (x => x[0] - T0 >= a && x[0] - T0 <= b); if (w.length < 2) return NaN;
    return (w[w.length - 1][1] - w[0][1]) / ((w[w.length - 1][0] - w[0][0]) / 1000); };
  const vEarly = speedIn (0, 70), vFull = speedIn (500, 1400);
  gate (eFirst < 0.1 && mono && tFull > 150 && tFull < 450 && vEarly < 0.45 * vFull,
        '[2] THE WIND-UP — it starts to move, then comes up to speed',
        `envelope on the first frame ${(+eFirst).toFixed (3)} · climbs monotonically ${mono} · full at ${(+tFull).toFixed (0)} ms · `
      + `ARP loop speed in the first 70 ms ${(+vEarly).toFixed (2)} vs ${(+vFull).toFixed (2)} at full (s of loop per s)`);
  const ran = {}; TILE_KEYS.forEach (m => { ran[m] = s2[m].tau - s1[m].tau; });
  const allRan = TILE_KEYS.every (m => ran[m] > 0.9 && s2[m].r === 0);
  const unlitMoved = unl.every (m => s2[m].ct !== s1[m].ct && ! s2[m].lit);
  const litOnlyArp = await p.evaluate (() => [...document.querySelectorAll ('#syn-panel .flow-mode')].map (t => t.getAttribute ('data-mode') + (t.classList.contains ('act') ? '*' : '')).join (' '));
  gate (allRan && unlitMoved && up.maxop > 0.8, '[3] ALL FOUR TILES MOVE WHILE A NOTE SOUNDS, LIT OR NOT',
        `lit (*): ${litOnlyArp} · loop run in 1.5 s: ${TILE_KEYS.map (m => m + ' ' + ran[m].toFixed (3) + ' s (rest weight ' + s2[m].r + ')').join (' · ')} · `
      + `unlit SMIL times ${unl.map (m => m + ' ' + s1[m].ct + '→' + s2[m].ct).join (' · ')} · ARP fired (peak pulse op ${up.maxop.toFixed (2)})`);

  // ── 4 + 5 · THE ENDING — stop mid-flash; after the note-off frame nothing ships ─────────────
  /* fb636 h3 — record the last LIVE frames too: the ending's first pass is a step FROM the last live frame, and a rewind there
     (MOTION_MUT=rewind) is invisible to a record that starts at the stop */
  await p.evaluate (() => { window.__TAU = []; window.__REC = 1; });
  let midFlash = true;
  try { await p.waitForFunction (() => { const s = window.__tiWindState ('tile-arp'); const r = ((s.tau % 1.9) + 1.9) % 1.9; return r > 0.08 && r < 0.30; },
                                 { polling: 'raf', timeout: 4000 }); } catch (e) { midFlash = false; }
  const liveRef = await p.evaluate (() => { const n = document.querySelector ('#syn-panel .flow-mode[data-mode="arp"] .arpNote'); return n ? +n.style.opacity : NaN; });
  const preStop = await p.evaluate (() => ['arp', 'chop', 'glitch', 'drift'].map (m => +(window.__tiWindState ('tile-' + m).tau).toFixed (3)));
  await p.evaluate (() => { window.__TAU = window.__TAU.slice (-12); window.__GESTN = 0; window.__REC = 1; window.__stopPlay (); });
  const waited = await settle ();
  await sleep (100);
  const dn = await p.evaluate (() => ({ tau: window.__TAU.slice (), stopT: window.__STOPT, busy: window.__tiWindBusy (), gest: window.__GESTN,
                                        snap: window.__tileSnap () }));
  await p.evaluate (() => { window.__REC = 0; });
  const tail = dn.tau.filter (x => x[0] > dn.stopT + 40);
  /* per tile clock (columns: arp 1, chop 3, glitch 4, drift 5): never backwards, never faster than it played, and when it landed */
  const COL = { arp: 1, chop: 3, glitch: 4, drift: 5 }, GCOL = { arp: 6, chop: 7, glitch: 8, drift: 9 }, PER = { arp: 1.9, chop: 2.2, glitch: 9.6, drift: 2.4 }, per = {};
  for (const m of TILE_KEYS) { const c = COL[m]; let back = false, vmax = 0, vFirst = NaN, vLast = NaN, lastMove = dn.stopT, folds = [];
    for (let i = 1; i < dn.tau.length; ++i) {
      const dtau = dn.tau[i][c] - dn.tau[i - 1][c], dt = (dn.tau[i][0] - dn.tau[i - 1][0]) / 1000;
      /* the landing FOLD is the only step back excused (fb636 h3): the pass that lands reaches its goal g exactly and folds
         it into the first period, so the drop — measured from that goal (the probe samples after the fold) — is a WHOLE
         number of the tile's periods: |drop/P − round(drop/P)| < 1e-6. Any other drop is a rewind. */
      if (dtau < -1e-9) { const g = dn.tau[i - 1][GCOL[m]], drop = g - dn.tau[i][c], k = drop / PER[m];
        if (isFinite (g) && g >= dn.tau[i - 1][c] - 1e-9 && Math.round (k) >= 1 && Math.abs (k - Math.round (k)) < 1e-6) folds.push (drop.toFixed (4)); else back = true; }
      if (Math.abs (dtau) > 1e-9) lastMove = dn.tau[i][0];
      if (dt > 0.004 && dtau >= 0) { const v = dtau / dt; if (v > vmax) vmax = v; if (isNaN (vFirst) && dn.tau[i][0] > dn.stopT) vFirst = v; if (dtau > 1e-7) vLast = v; } }
    per[m] = { back, vmax, vFirst, vLast, landMs: lastMove - dn.stopT, folds }; }
  const A4 = per.arp, miss4 = homeMiss (dn.snap);
  const clocksOk = TILE_KEYS.every (m => ! per[m].back && per[m].vmax <= 1.1 && per[m].landMs <= 5200);
  gate (midFlash && tail.length > 10 && clocksOk && A4.vFirst > 0.4 && A4.vLast < 0.35 && ! miss4.length && ! dn.busy,
        '[4] 🚨 THE ENDING — stopped mid-flash, every tile finishes its loop onto its HOME pose, then stops',
        `stopped mid-flash ${midFlash} (a pulse at op ${(+liveRef).toFixed (2)}; stop points ${TILE_KEYS.map ((m, i) => m + ' ' + preStop[i]).join (' ')}) · tail frames after the last shipped frame ${tail.length} · `
      + TILE_KEYS.map (m => `${m}: backwards ${per[m].back} (folds ${per[m].folds.join ('/') || 'none'} = whole periods of ${PER[m]}), peak ${per[m].vmax.toFixed (2)}x, landed at ${per[m].landMs.toFixed (0)} ms`).join (' · ')
      + ` (≤ 1.1x — never faster than it played — and ≤ 5200 ms) · ARP leaves the edge at ${(+A4.vFirst).toFixed (2)}x, last step ${(+A4.vLast).toFixed (3)}x · `
      + `settled after ${waited} ms · home: ${miss4.length ? 'NOT — ' + miss4.join (' · ') : 'all four EXACTLY'} · still winding ${dn.busy}`);
  const afterF = await count (1500);
  const busy2 = await p.evaluate (() => window.__tiWindBusy ());
  gate (afterF === 0 && ! busy2 && dn.gest === 0, '[5] AND THEN IT STOPS — zero frames, and the tail never woke the C++',
        `${afterF} painter frames in the 1.5 s after the ending · still winding ${busy2} · uiGesture calls during the wind-down ${dn.gest}`);

  // ── 12 · HOME IS ONE PICTURE — three different stop points, the same pixels, and the boot's ──────────────────
  const stops = [{ at: preStop.join (' '), hash: await tileHash (), pose: pose (dn.snap), miss: miss4 }];
  for (const [lo, hi, ms] of [[0.85, 1.05, 900], [1.45, 1.70, 1700]]) {
    await p.evaluate (() => window.__startPlay ()); await sleep (ms);
    try { await p.waitForFunction ((lo, hi) => { const s = window.__tiWindState ('tile-arp'); const r = ((s.tau % 1.9) + 1.9) % 1.9; return r > lo && r < hi; },
                                   { polling: 'raf', timeout: 4000 }, lo, hi); } catch (e) {}
    const at = await p.evaluate (() => ['arp', 'chop', 'glitch', 'drift'].map (m => +(window.__tiWindState ('tile-' + m).tau).toFixed (3)).join (' '));
    await p.evaluate (() => window.__stopPlay ()); await settle (); await sleep (150);
    const sn = await p.evaluate (() => window.__tileSnap ());
    stops.push ({ at, hash: await tileHash (), pose: pose (sn), miss: homeMiss (sn) }); }
  const sameHash = stops.every (s => TILE_KEYS.every (m => s.hash[m] === HASH0[m]));
  const samePose = stops.every (s => s.pose === HOME && ! s.miss.length);
  const distinct = new Set (stops.map (s => s.at)).size === stops.length;
  gate (sameHash && samePose && distinct, '[12] 🚨 HOME IS ONE PICTURE — three different stop points, the same pixels as the boot',
        `boot ${TILE_KEYS.map (m => m + ' ' + HASH0[m]).join (' ')} · ` + stops.map ((s, i) => `stop ${'ABC'[i]} (clocks ${s.at}) → ${TILE_KEYS.map (m => s.hash[m] === HASH0[m] ? m + ' =' : m + ' ' + s.hash[m] + ' ≠').join (' ')}${s.pose === HOME ? '' : ' · drawn state differs'}${s.miss.length ? ' · NOT home: ' + s.miss.join (' · ') : ''}`).join (' | '));

  // ── 6 · A BREATHER FINISHES ITS BREATH ─────────────────────────────────────────────────────
  const br0 = await p.evaluate (() => { const el = document.getElementById ('env-line'); if (! el) return { err: 'no #env-line' };
    const a = el.getAnimations ().find (x => x.animationName === 'envBreathe'); return { has: !! a, state: a ? a.playState : null }; });
  let br = { err: br0.err || (br0.has ? null : 'no envBreathe animation on #env-line (not rendered?)') };
  if (! br.err) {
    await p.evaluate (() => window.__startPlay ());
    try { await p.waitForFunction (() => { const a = document.getElementById ('env-line').getAnimations ().find (x => x.animationName === 'envBreathe');
                                           const ph = (a.currentTime || 0) % 3400; return a.playState === 'running' && ph > 1450 && ph < 1950; },
                                   { polling: 'raf', timeout: 6000 }); } catch (e) { br.err = 'never reached the peak of the breath while playing'; }
    const atStop = await p.evaluate (() => { const el = document.getElementById ('env-line'); const a = el.getAnimations ().find (x => x.animationName === 'envBreathe');
      window.__stopPlay (); return { ph: (a.currentTime || 0) % 3400, op: +getComputedStyle (el).opacity }; });
    await sleep (250);
    const mid = await p.evaluate (() => { const el = document.getElementById ('env-line'); const a = el.getAnimations ().find (x => x.animationName === 'envBreathe');
      return { state: a ? a.playState : null, op: +getComputedStyle (el).opacity }; });
    await sleep (3750);
    const r = await p.evaluate (() => { const el = document.getElementById ('env-line'); const a = el.getAnimations ().find (x => x.animationName === 'envBreathe');
      return { state: a ? a.playState : null, ph: a ? (a.currentTime || 0) % 3400 : NaN, op: +getComputedStyle (el).opacity,
               still: document.documentElement.classList.contains ('ti-still') }; });
    br = Object.assign (br, { atStop, mid, r });
  }
  if (br.err) gate (false, '[6] 🚨 A BREATHER FINISHES ITS BREATH', br.err);
  else { const d = Math.min (br.r.ph, 3400 - br.r.ph);
    gate (br.mid.state === 'running' && br.r.state === 'paused' && br.r.still && d < 200 && Math.abs (br.r.op - 0.9) < 0.006,
          '[6] 🚨 A BREATHER FINISHES ITS BREATH — #env-line breathes on after the stop, then rests on its pose, not mid-breath',
          `stopped at ${br.atStop.ph.toFixed (0)} ms of 3400 (opacity ${br.atStop.op.toFixed (3)}) → 250 ms later ${br.mid.state} (opacity ${br.mid.op.toFixed (3)}) → ${br.r.state} at ${br.r.ph.toFixed (0)} ms `
        + `(${d.toFixed (0)} ms from the pose), opacity ${br.r.op.toFixed (3)} (rest .900), html.ti-still ${br.r.still}`); }

  await settle ();   /* fb636 h1 — bar 6's note ends a wind-down of all four tiles (a home can be ~4.7 s away): start from rest */
  // ── 7 · A REPAINT AT REST DRAWS THE SAME PICTURE ───────────────────────────────────────────
  const hashes = async () => p.evaluate (() => { window.__tiRepaint && window.__tiRepaint ();
    const cvs = [...document.querySelectorAll ('#syn-panel .fk-em canvas')], topo = document.querySelector ('#syn-panel .reso-cv');
    const h = (cv) => { if (! cv || ! cv.width) return 'none'; const d = cv.getContext ('2d').getImageData (0, 0, cv.width, cv.height).data;
      let a = 2166136261 >>> 0, ink = 0; for (let i = 0; i < d.length; ++i) { a = Math.imul (a ^ d[i], 16777619) >>> 0; if ((i & 3) === 3 && d[i]) ++ink; } return a.toString (16) + '/' + ink; };
    return { em: cvs.map (h), topo: h (topo) }; });
  const hA = await hashes (); await sleep (700); const hB = await hashes ();
  const emSame = hA.em.length === 4 && hA.em.every ((x, i) => x === hB.em[i] && ! /\/0$/.test (x));
  const topoSame = hA.topo === hB.topo && hA.topo !== 'none';
  gate (emSame && topoSame, '[7] A REPAINT AT REST DRAWS THE SAME PICTURE — emblems and topo',
        `emblems ${hA.em.join (' ')} → ${hB.em.join (' ')} · topo ${hA.topo} → ${hB.topo}  (hash/inked px; inked 0 would be a blank, not a pose)`);

  // ── 8 · THE WATERFALL RESTS ────────────────────────────────────────────────────────────────
  const wfSetup = await p.evaluate (() => { const o = {};
    try { const W = window.wtWaterfall, dev = document.querySelector ('#syn-panel .device.osc'), cv = document.getElementById ('osc-wave-a');
      if (! W || ! dev || ! cv) return { err: 'no waterfall / device / #osc-wave-a' };
      dev.classList.remove ('engine-sample', 'engine-granular', 'engine-geode', 'engine-modal', 'engine-fm', 'swapped');   /* the stub's engine index lands on Sample, which hides the picture */
      dev.classList.add ('engine-harm'); const sel = dev.querySelector ('.hm-mode-select');
      if (sel) { sel.value = '6'; sel.dispatchEvent (new Event ('change', { bubbles: true })); }
      const N = 16, P = 160, D = new Float32Array (N * P);
      for (let f = 0; f < N; ++f) for (let i = 0; i < P; ++i) D[f * P + i] = 0.9 * Math.sin (2 * Math.PI * (1 + f) * i / (P - 1));
      W.cache['a'] = { n: N, p: P, nf: N, d: D }; W.prev['a'] = null; W.off['a'] = null;
      delete window.__wtDisp; delete window.__harmHueEff;
      window.__harmChurnBand = [0.2, 0, 0, 0]; window.__harmChurnRate = [1.5, 0, 0, 0];
      W.on['a'] = true; W.syncClass ('a');
      if (! W.__mlWrapped) { const d0 = W.draw.bind (W); W.draw = function (x) { window.__WFD = (window.__WFD | 0) + 1; return d0 (x); }; W.__mlWrapped = 1; }
      o.harm = W.isHarmTable ('a'); o.vis = cv.offsetParent !== null;
      if (! o.vis) { let n = cv; while (n && n !== document.body && getComputedStyle (n).display !== 'none') n = n.parentElement;
        o.hider = (n ? (n.tagName + '#' + n.id + '.' + String (n.className).replace (/\s+/g, '.')).slice (0, 120) : 'none') + ' in device ' + String (dev.className).slice (0, 160); }
      W.kick (); } catch (e) { o.err = String (e); }
    return o; });
  let wf = { err: wfSetup.err || ((wfSetup.harm && wfSetup.vis) ? null : `rig not live (harm-table ${wfSetup.harm}, canvas visible ${wfSetup.vis}, hidden by ${wfSetup.hider})`) };
  if (! wf.err) {
    await sleep (400);
    const draws = async (ms) => { const ph0 = await p.evaluate (() => { window.__WFD = 0; return window.wtWaterfall.chPh.a; }); await sleep (ms);
      return p.evaluate ((q) => ({ n: window.__WFD | 0, raf: window.wtWaterfall.raf | 0, ph: window.wtWaterfall.chPh.a, ph0: q }), ph0); };
    wf.rest = await draws (1000);
    await p.evaluate (() => window.__startPlay ());
    wf.play = await draws (1200);
    const ph1 = wf.play.ph;
    await p.evaluate (() => window.__stopPlay ());
    await sleep (1800);
    await settle ();   /* fb636 h1 — the four tiles' wind-down (up to ~4.7 s) runs painter passes, and each pass nudges the waterfall's
                          rAF (it draws nothing): the waterfall's rest is measured once that tail has ended */
    wf.after = await draws (1000);
    wf.phMoved = Math.abs (ph1 - wf.rest.ph) > 0.05;
  }
  if (wf.err) gate (false, '[8] THE WATERFALL RESTS', wf.err);
  else gate (wf.rest.n === 0 && ! wf.rest.raf && wf.play.n >= 30 && wf.phMoved && wf.after.n === 0 && ! wf.after.raf && wf.after.ph === wf.after.ph0,
             '[8] THE WATERFALL RESTS — parked at rest, the churn drift winds with the notes, then parks again',
             `at rest ${wf.rest.n} draws/s (loop armed ${!! wf.rest.raf}) · playing ${wf.play.n} draws in 1.2 s, drift phase ${(+wf.rest.ph).toFixed (3)} → ${(+wf.play.ph).toFixed (3)} · `
           + `after the wind-down ${wf.after.n} draws/s (loop armed ${!! wf.after.raf}), drift held ${(+wf.after.ph0).toFixed (4)} → ${(+wf.after.ph).toFixed (4)}`);

  // ── 13 · THE FOUR FLOW CARDS FOLLOW THE MIDI — out of the chain, with a feed ({on:0}: the plugin's case) ──────
  const cards = [];
  for (const [m, name] of [['arp', 'ARP'], ['glitch', 'GLITCH'], ['drift', 'ROBIN'], ['chop', 'CHOP']]) {
    await settle ();
    await p.evaluate ((m) => { document.querySelectorAll ('.ti-card.open').forEach (e => { if (/(arp|gli|rbn|chop)-ext/.test (e.className)) e.classList.remove ('open'); });
                              window.__openFlowCard (m); }, m);
    await sleep (700);
    const c0 = await p.evaluate (() => window.__cardSnap ()); await sleep (600); const c1 = await p.evaluate (() => window.__cardSnap ());
    await p.evaluate (() => window.__startPlay ());
    let moves = 0, last = JSON.stringify (c1);
    for (let i = 0; i < 16; i++) { await sleep (150); const x = JSON.stringify (await p.evaluate (() => window.__cardSnap ())); if (x !== last) moves++; last = x; }
    /* stop AWAY from home, so the ending is always exercised (≤ 3 s more of play) */
    try { await p.waitForFunction (() => { const c = window.__cardSnap (); return c && ! c.home; }, { polling: 50, timeout: 3000 }); } catch (e) {}
    const atStop = await p.evaluate (() => window.__cardSnap ());
    await p.evaluate (() => window.__stopPlay ());
    const t0 = Date.now (); let h = null;
    for (let i = 0; i < 140; i++) { h = await p.evaluate (() => window.__cardSnap ()); if (h && h.home) break; await sleep (50); }
    const homeMs = Date.now () - t0;
    await sleep (700); const h2 = await p.evaluate (() => window.__cardSnap ());
    const J = (x) => JSON.stringify (x);
    const ok = !! (c0 && c1 && h && h2 && atStop) && J (c0) === J (c1) && c1.home && moves >= 3 && ! atStop.home && h.home && homeMs <= 7000 && J (h2) === J (c1);
    cards.push ({ name, ok, txt: `${name} ${ok ? 'OK' : 'FAIL'}: at rest ${c1 && c1.home ? 'home' : 'NOT home'}${J (c0) === J (c1) ? ' and still' : ' and MOVING'} · `
      + `${moves} changes in 2.4 s of play · stopped ${atStop && ! atStop.home ? 'away from home' : 'AT home (the ending untested)'} · home ${h && h.home ? 'after ' + homeMs + ' ms' : 'NEVER (7 s)'} · then ${J (h2) === J (c1) ? 'the same picture as before the note' : 'NOT the pre-note picture: ' + J (h2).slice (0, 160) + ' vs ' + J (c1).slice (0, 160)}` });
  }
  await p.evaluate (() => { document.querySelectorAll ('.ti-card.open').forEach (e => { if (/(arp|gli|rbn|chop)-ext/.test (e.className)) e.classList.remove ('open'); }); });
  gate (cards.length === 4 && cards.every (c => c.ok), '[13] THE FOUR FLOW CARDS FOLLOW THE MIDI — still at rest, moving while a note sounds, home again after',
        cards.map (c => c.txt).join (' | '));

  // ── 9 · THE SEQUENCER IS UNTOUCHED (static — the chain steps write parameters) ─────────────
  const src = fs.readFileSync (PAGE, 'utf8');
  const SEQ = [
    'if(go===1&&sc.__pl!==1)sc.restart(); sc.__pl=go;',
    'if(go===1&&feed.a&&stp<prevStp) sc.advance();',
    'if(go===1&&feed.ls<prevLs-0.5) sc.advance();',
    'if(feed.wr>prevWr){ if(prevWr>0&&go===1)sc.advance(); prevWr=feed.wr; }',
    'if(go===1&&sub<prevSub) sc.advance();',
    'if(sim.slice===0&&!feed) sc.advance();',
    'if(m===0){ sim.sub=(sim.sub+1)%n; if(sim.sub===0&&!feed)sc.advance(); }',
    "        function frame(t){ if(!card.classList.contains('open')){ on=false; return; }\n          if(!lastT)lastT=t; var dt=Math.min(.05,(t-lastT)/1000); lastT=t;\n          try{ tick(dt,t/1000); }catch(e){}\n          requestAnimationFrame(frame); }",
    'if(feedFn&&!feedBusy){ feedBusy=true;', 'if(gliFeedFn&&!feedBusy){ feedBusy=true;', 'if(rbnFeedFn&&!feedBusy){ feedBusy=true;', 'if(chopFeedFn&&!feedBusy){ feedBusy=true;' ];
  const seqCount = SEQ.map (s => src.split (s).length - 1);
  gate (seqCount[0] === 4 && seqCount.slice (1).every (n => n === 1), '[9] THE SEQUENCER IS UNTOUCHED — chain steps, feed polls and the card loop, byte for byte',
        SEQ.map ((s, i) => seqCount[i] + '× ' + s.split ('\n')[0].slice (0, 46)).join (' · '));

  // ── 10 · THE RESYNTH SCAN RESTS (the editor's wiring) ──────────────────────────────────────
  let cpp = ''; try { cpp = fs.readFileSync (path.join (ROOT, 'Source/PluginEditor.cpp'), 'utf8'); } catch (e) {}
  const w1 = cpp.includes ('const float dt = uiQuiet ? 0.0f : 1.0f / 60.0f;');
  const w2 = cpp.includes ('if ((! uiQuiet || geodeImgHeartbeat_[o] < 16) && geodeImgHeartbeat_[o] > 0) --geodeImgHeartbeat_[o];');   /* the ~5 s count waits while quiet; a reattach's small staggered count still runs down */
  const w3 = /void TerrainUiCore::resyncAfterReattach\(\)\s*\{[\s\S]{0,1200}geodeImgHeartbeat_\[o\] = o \* 3;/.test (cpp);   /* re-armed, staggered 0/3/6/9 ticks */
  const w4 = (cpp.match (/computeDisplayEnvelope \(bins, NB, dt, pos\)/g) || []).length === 1;
  gate (w1 && w2 && w3 && w4, '[10] THE RESYNTH SCAN RESTS — dt 0 while quiet, the heartbeat counts only while awake, a reattach re-arms it',
        `dt gated ${w1} · heartbeat gated ${w2} · reattach re-arms ${w3} · the scan call reads that dt ${w4}`);

  gate (errs.length === 0, '[11] NO PAGE ERRORS', errs.slice (0, 3).join (' | ') || 'clean');

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close ();
  process.exit (fail === 0 ? 0 : 1);
}) ().catch (e => { console.error (e); process.exit (1); });
