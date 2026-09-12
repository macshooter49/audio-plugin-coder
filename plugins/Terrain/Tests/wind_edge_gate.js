// ══════════════════════════════════════════════════════════════════════════════════════════════
//  wind_edge_gate.js — fb636 h3: THE MOTION LAW AT ITS EDGES (the four cases fb636 h1/h2's gates did not exercise).
//
//    NODE_PATH=<plugins/Terrain>/Tests/node_modules node Tests/wind_edge_gate.js [page.html]
//
//  Max (2026-09-12 02:34): "i want them to move regardless of on/off, just about the MIDI my g, and the ARP should not
//  have the dots still in motion when inactive, dots should be organized and stable not frozen on the screen like this
//  same for everything else." The laws: motion only while MIDI is live; wind-up 250 ms; a smooth wind-down that is never
//  backwards, never faster than playing, and lands EXACTLY on the home pose; ZERO frames at rest; a hand still repaints.
//
//  motion_law_gate's rig and stub (the lane heard once; while "playing" a shipped frame every 16 ms with the notes flag and
//  its stamp; at note-off ONE frame, then nothing; the four FLOW feeds answer {on:0} — a card out of the chain).
//
//  THE BARS
//   0  THE RIG — the motion clock, the four tiles, the readouts' boot values (ROBIN NOTES 0, GLITCH FX —)
//   1  🚨 A NOTE MID-ENDING NEVER JUMPS THE POSE (review finding 1) — three times (0.35 / 0.7 / 1.1 s after a stop, while
//      the four tiles are still winding down) a note comes back: no tile's rest weight r falls faster than the wind-up's
//      steepest (6/s: ≤ 0.12 a 16 ms frame), the ARP fan's drawn dash and stroke-opacity never step more than that allows,
//      and every loop clock stays continuous and forward. Before: r was re-derived from the speed and fell up to 0.39 in
//      one frame (the fan '18.23, 3.77' → '11.64, 10.36').
//   2  🚨 A CARD REOPENED AT REST IS HOME, AND STILL (review finding 2) — each FLOW card closed mid-play (all four) or
//      mid-ending (ARP, ROBIN), the note ends while it is closed, reopened at rest: the first frame is its home, the very
//      pre-note picture (readouts included), and it makes 0 picture changes in 3 s. Before: the owed ending resumed with
//      no MIDI (ARP 10 changes in 4 s, ROBIN 18, CHOP 11).
//   3  🚨 THE SYNTH PAGE SHOWN AGAIN AT REST (review finding 2) — #syn-panel hidden mid-play (the note ends while hidden)
//      or mid-ending, shown again at rest and tapped once: the DRV emblem's clock (em-drv) is home on the first frame, the
//      four emblems are the rest picture, and nothing paints past the hand's own 450 ms tail. Before: the DRV wave walked
//      on with no MIDI and em-drv re-armed the page tail from the hand's repaint (74 frames).
//   4  🚨 A SLOW CARD GETS HOME IN ≤ 2.5 s (review finding 3) — the ARP card at 1/1 and the GLITCH card on a 1/1 grid
//      (0.5 steps/s), stopped one or two steps in: the ending is ≤ 2.5 s (park T), forward only, and lands on the pre-note
//      picture. Before: T ≥ 1.5·D/rate with home a whole pattern away — 45 s.
//   5  🚨 THE READOUTS COME HOME (review finding 4) — ROBIN stopped while its NOTES readout shows 1-3 rests on 0, and GLITCH
//      stopped after a fire rests on FX '—' (the boot picture); each card's whole picture is the pre-note one.
//   6  AND THEN IT STOPS — the cards closed: 0 painter frames in 1.5 s, nothing winding
//   7  NO PAGE ERRORS
//
//  PROOF THE BARS CAN FAIL (EDGE_MUT=…):
//    rejump    r re-derived from the speed on a retrigger (the fb636 h1 line)            → 1
//    unseen    an unwatched ending is resumed (no snap in __tiWind / __tiEnv)             → 2 3
//    cardslow  a card's ending is not bounded (T = 1.5·D/rate)                            → 4
//    notes     ROBIN's NOTES readout is not homed                                         → 5
//    lastfx    GLITCH's FX readout is not homed                                           → 5
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require ('puppeteer-core');
const fs = require ('fs'), path = require ('path'), os = require ('os');
const ROOT = path.join (__dirname, '..');
const PAGE = process.argv[2] || path.join (ROOT, 'Source/ui/public/index.html');
const MUT = process.env.EDGE_MUT || '';
const sleep = (ms) => new Promise (r => setTimeout (r, ms));
const J = (x) => JSON.stringify (x);
let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail; console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync (PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split (f).length - 1;
    if (n !== 1) { console.error ('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice (0, 90)); process.exit (2); }
    src = src.replace (f, t); };
  if (MUT === 'rejump')        sub ("      var rT = 1 - sstep(st.m); if (rT < st.r) st.r = Math.max(rT, st.r - dt * 6);", "      st.r = Math.min(st.r, 1 - sstep(st.m));");
  else if (MUT === 'unseen')   { sub ("    if (st.lapse > 0.25 && (st.park || st.m > 0) && ! (st.w && was && MOT.offPass === MOT.pass)){", "    if (false){");
                                 sub ("st.snap = ! on && st.lapse > 0.25;", "st.snap = false;"); }
  else if (MUT === 'cardslow') sub ("      if (vmax > rate && T > 2.5) T = Math.max(2.5, 1.5 * D / vmax);\n", "");
  else if (MUT === 'notes')    sub ("if(sim.now===h0){ sim.homing=0; if(sim.notes!==0){ sim.notes=0; statPaint(); } }", "if(sim.now===h0){ sim.homing=0; }");
  else if (MUT === 'lastfx')   sub ("else if(sim.wound){ sim.wound=0; sim.last=null; sim.lastKey=null; rFx.textContent='—'; stepped=true; }", "else if(sim.wound){ sim.wound=0; }");
  else { console.error ('unknown EDGE_MUT ' + MUT); process.exit (2); }
  console.log ('  (mutation ' + MUT + ' landed)');
  const p = path.join (os.tmpdir (), 'wind_edge_mut_' + MUT + '.html'); fs.writeFileSync (p, src); return p;
}

/* motion_law_gate's stub: FLOW feeds {on:0} (a card out of the chain), the ten ROBIN cycle params (all four stations on, order
   A B C D, Cycle, A-First off — without them the stub's 0 empties the cycle), slider states that notify their listeners */
const STUB = () => {
  window.__SYN = { FLOW_RBN_A: 1, FLOW_RBN_B: 1, FLOW_RBN_C: 1, FLOW_RBN_D: 1, FLOW_RBN_O1: 0, FLOW_RBN_O2: 1 / 3, FLOW_RBN_O3: 2 / 3, FLOW_RBN_O4: 1,
                   FLOW_RBN_MODE: 0, FLOW_RBN_AFIRST: 0,
                   FLOW_GLI_EN_REP: 1 };   /* bar 5: GLITCH's Repeat on (its DSP default) — the stub's 0 turns every effect off, so it never fires */
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
    if (sp) { sp.classList.remove ('hidden'); sp.style.display = 'block'; } window.dispatchEvent (new Event ('resize')); });
  await sleep (2600);

  await p.evaluate (() => {
    const TL = ['arp', 'chop', 'glitch', 'drift'];
    window.__PROBE = 0; window.__REC = 0; window.__ROWS = []; window.__PTREC = 0; window.__PT = [];
    /* one row per painter pass: [t, then per tile (arp chop glitch drift) r · tau · park goal, ARP fan dash · stroke-opacity, live] */
    window.__row = function () { const r = [performance.now ()];
      for (const m of TL) { const s = window.__tiWindState ('tile-' + m); r.push (s ? s.r : NaN, s ? s.tau : NaN, s && s.park ? s.park.g : NaN); }
      const l = document.querySelector ('#syn-panel .flow-mode[data-mode="arp"] .arpLine');
      if (l) { const cs = getComputedStyle (l); r.push (parseFloat (cs.strokeDasharray), parseFloat (cs.strokeOpacity)); } else r.push (NaN, NaN);
      r.push (window.__notesActive ? 1 : 0); return r; };
    window.__tiFrameReg ('__probe__', function () { window.__PROBE++;
      if (window.__PTREC) window.__PT.push (performance.now ());
      if (window.__REC) window.__ROWS.push (window.__row ()); });
    try { window.__tiFrame && window.__tiFrame (); } catch (e) {}
    window.__KA = setInterval (() => { try { window.__tiAlive && window.__tiAlive (); } catch (e) {} }, 500);
    window.__PLAY = 0;
    window.__play = function () { window.__notesActive = 1; window.__notesActiveT = Date.now (); window.__tiFrame (); };
    window.__startPlay = function () { if (window.__PLAY) return; window.__PLAY = setInterval (window.__play, 16); window.__play (); };
    window.__stopPlay = function () { clearInterval (window.__PLAY); window.__PLAY = 0;
      window.__notesActive = 0; window.__notesActiveT = Date.now (); window.__tiFrame (); };
    window.__closeCards = function () { document.querySelectorAll ('.ti-card.open').forEach (e => { if (/(arp|gli|rbn|chop)-ext/.test (e.className)) e.classList.remove ('open'); }); };
    window.__raf2 = function () { return new Promise (r => requestAnimationFrame (() => requestAnimationFrame (r))); };
    window.__tap = function () { document.dispatchEvent (new PointerEvent ('pointerdown', { bubbles: true })); document.dispatchEvent (new PointerEvent ('pointerup', { bubbles: true })); };
    window.__showSyn = function (on) { const sp = document.getElementById ('syn-panel'); sp.classList.toggle ('hidden', ! on); sp.style.display = on ? 'block' : 'none'; };
    window.__emHash = function () { return [...document.querySelectorAll ('#syn-panel .fk-em canvas')].map (cv => { if (! cv.width) return 'none';
      const d = cv.getContext ('2d').getImageData (0, 0, cv.width, cv.height).data; let a = 2166136261 >>> 0, ink = 0;
      for (let i = 0; i < d.length; ++i) { a = Math.imul (a ^ d[i], 16777619) >>> 0; if ((i & 3) === 3 && d[i]) ++ink; } return a.toString (16) + '/' + ink; }).join (' '); };
    window.__emState = function () { const s = window.__tiWindState ('em-drv'); if (! s) return null;
      return { tau: +s.tau.toFixed (5), r: s.r, m: +(+s.m).toFixed (4), park: !! s.park, home: s.tau === 0 && ! s.park && s.m === 0 && s.r === 1 }; };
    /* the open FLOW card's display facts — motion_law_gate's, plus the two readouts (fb636 h3) */
    window.__cardSnap = function () { const k = [...document.querySelectorAll ('.ti-card.open')].filter (e => /(arp|gli|rbn|chop)-ext/.test (e.className)).pop ();
      if (! k) return null; const o = { card: (/(arp|gli|rbn|chop)-ext/.exec (k.className) || [])[1] }, pl = (sv) => sv && sv.querySelector ('line[stroke="#B794FF"]');
      if (o.card === 'arp') { const l = k.querySelector ('.hero svg line[stroke^="rgba(183,148,255"]'); o.x = l && l.getAttribute ('x1');
        o.led = [...k.querySelectorAll ('.leds i')].findIndex (i => i.classList.contains ('on')); o.home = o.x === '9' && o.led === 0; }
      else if (o.card === 'gli') { const l = pl (k.querySelector ('.graph svg')); o.x = l && l.getAttribute ('x1');
        o.foot = (k.querySelector ('.r-foot') || {}).textContent; o.st = (k.querySelector ('.r-st') || {}).textContent; o.fx = (k.querySelector ('.r-fx') || {}).textContent;
        o.home = o.x === '0.0' && o.foot === 'Slice 1 · 16' && o.st === 'Idle'; }
      else if (o.card === 'rbn') { const sv = k.querySelector ('.wheel svg'), l = pl (sv); o.now = (k.querySelector ('.s-now') || {}).textContent;
        o.notes = (k.querySelector ('.s-notes') || {}).textContent; o.arm = l ? l.getAttribute ('x2') + ',' + l.getAttribute ('y2') : null;
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
  const settle = async () => { const t0 = Date.now ();
    for (let i = 0; i < 160 && await p.evaluate (() => window.__tiWindBusy ()); i++) await sleep (50); return Date.now () - t0; };
  const snap = () => p.evaluate (() => window.__cardSnap ());
  const openCard = async (m) => { await p.evaluate ((m) => { window.__closeCards (); window.__openFlowCard (m); }, m); await p.evaluate (() => window.__raf2 ()); };

  console.log (`\n══ wind_edge_gate — fb636 h3 ══  ${PAGE}${MUT ? '   [MUTATION ' + MUT + ']' : ''}\n`);

  // ── 0 · THE RIG ────────────────────────────────────────────────────────────────────────────
  await openCard ('drift'); await sleep (400); const rb0 = await snap ();
  await openCard ('glitch'); await sleep (400); const gl0 = await snap ();
  await p.evaluate (() => window.__closeCards ());
  const rig = await p.evaluate (() => ({ wind: typeof window.__tiWind === 'function', env: typeof window.__tiEnv === 'function',
    tiles: document.querySelectorAll ('#syn-panel .flow-mode').length, em: document.querySelectorAll ('#syn-panel .fk-em canvas').length }));
  gate (rig.wind && rig.env && rig.tiles === 4 && rig.em === 4 && rb0 && rb0.notes === '0' && rb0.home && gl0 && gl0.fx === '—' && gl0.home,
        '[0] THE RIG — the motion clock, four tiles, four emblems, and the cards\' readouts at their boot values',
        `__tiWind ${rig.wind} · __tiEnv ${rig.env} · tiles ${rig.tiles} · emblems ${rig.em} · ROBIN NOTES ${rb0 && rb0.notes} (home ${rb0 && rb0.home}) · GLITCH FX ${gl0 && gl0.fx} (home ${gl0 && gl0.home})`);

  // ── 1 · A NOTE MID-ENDING NEVER JUMPS THE POSE ─────────────────────────────────────────────
  /* per pass: r may fall at most at the wind-up's steepest (6/s, + slack for the probe's clock), the fan's dash (5 + 17r) and
     stroke-opacity (1 − .62r) at most what that allows, and every tile clock moves forward at ≤ 1.1x (the landing fold excepted:
     a whole number of periods from the goal the clock just reached) */
  const PER = [1.9, 2.2, 9.6, 2.4], NAMES = ['arp', 'chop', 'glitch', 'drift'];
  const analyse = (rows) => { const o = { bad: [], maxRate: 0, maxDash: 0, maxSo: 0, n: rows.length };   /* maxDash / maxSo: per second */
    for (let i = 1; i < rows.length; ++i) { const a = rows[i - 1], c = rows[i], dt = Math.min (0.1, Math.max (0, (c[0] - a[0]) / 1000)), lim = 6.3 * dt + 0.02;
      for (let j = 0; j < 4; ++j) { const fall = a[1 + 3 * j] - c[1 + 3 * j], dtau = c[2 + 3 * j] - a[2 + 3 * j];
        if (dt > 0.004) o.maxRate = Math.max (o.maxRate, fall / dt);
        if (fall > lim) o.bad.push (`${NAMES[j]} r ${a[1 + 3 * j].toFixed (3)}→${c[1 + 3 * j].toFixed (3)} in ${(dt * 1000).toFixed (0)} ms`);
        if (dtau < -1e-9) { const g = a[3 + 3 * j], k = (g - c[2 + 3 * j]) / PER[j];
          if (! (isFinite (g) && Math.round (k) >= 1 && Math.abs (k - Math.round (k)) < 1e-6)) o.bad.push (`${NAMES[j]} clock back ${a[2 + 3 * j].toFixed (3)}→${c[2 + 3 * j].toFixed (3)}`); }
        else if (dtau > 1.1 * Math.max (dt, 1 / 60) + 0.004) o.bad.push (`${NAMES[j]} clock jumps +${dtau.toFixed (3)} in ${(dt * 1000).toFixed (0)} ms`); }
      const dd = Math.abs (c[13] - a[13]), ds = Math.abs (c[14] - a[14]);
      if (dt > 0.004) { o.maxDash = Math.max (o.maxDash, dd / dt); o.maxSo = Math.max (o.maxSo, ds / dt); }
      if (dd > 17 * lim + 0.02) o.bad.push (`fan dash ${a[13]}→${c[13]}`);
      if (ds > 0.62 * lim + 0.003) o.bad.push (`fan opacity ${a[14]}→${c[14]}`); }
    return o; };
  const re = [];
  for (const w of [350, 700, 1100]) {
    await settle ();
    await p.evaluate (() => { window.__ROWS = []; window.__REC = 1; window.__startPlay (); });
    await sleep (1400);
    const upRows = await p.evaluate (() => { window.__REC = 0; window.__stopPlay (); return window.__ROWS.slice (); });
    await sleep (w - 150);
    await p.evaluate (() => { window.__ROWS = []; window.__REC = 1; });
    await sleep (150);
    await p.evaluate (() => window.__startPlay ());
    await sleep (700);
    const reRows = await p.evaluate (() => { window.__REC = 0; window.__stopPlay (); return window.__ROWS.slice (); });
    const i0 = reRows.findIndex (x => x[15] === 1), before = i0 > 0 ? reRows[i0 - 1] : null;
    const mid = before ? [0, 1, 2, 3].filter (j => before[1 + 3 * j] > 0 && before[1 + 3 * j] < 1).length : 0;
    const jump = before ? Math.max (...[0, 1, 2, 3].map (j => before[1 + 3 * j] - reRows[i0][1 + 3 * j])) : NaN;
    re.push ({ w, up: analyse (upRows), re: analyse (reRows), mid, jump,
               rAt: before ? [0, 1, 2, 3].map (j => NAMES[j] + ' ' + before[1 + 3 * j].toFixed (2)).join (' ') : 'no retrigger frame' });
  }
  await settle ();
  const b1ok = re.every (x => x.mid >= 3 && ! x.re.bad.length && ! x.up.bad.length && x.re.n > 20);
  gate (b1ok, '[1] 🚨 A NOTE MID-ENDING NEVER JUMPS THE POSE — r falls no faster than the wind-up, the fan and the clocks stay continuous',
        re.map (x => `note back ${x.w} ms after the stop (rest weights then: ${x.rAt}; ${x.mid}/4 mid-ending) → largest one-frame fall of r ${(+x.jump).toFixed (3)} at the note, `
          + `peak fall rate ${x.re.maxRate.toFixed (2)}/s (wind-up from rest ${x.up.maxRate.toFixed (2)}/s; the law ≤ 6), fan dash ≤ ${x.re.maxDash.toFixed (1)}/s (wind-up ${x.up.maxDash.toFixed (1)}/s; the law ≤ 102), `
          + `opacity ≤ ${x.re.maxSo.toFixed (2)}/s (wind-up ${x.up.maxSo.toFixed (2)}/s; the law ≤ 3.72)${x.re.bad.length ? ' · BAD: ' + x.re.bad.slice (0, 4).join (' · ') : ''}${x.up.bad.length ? ' · wind-up BAD: ' + x.up.bad.slice (0, 3).join (' · ') : ''}`).join (' | '));

  // ── 2 · A CARD REOPENED AT REST IS HOME, AND STILL ─────────────────────────────────────────
  const reo = [];
  for (const [m, name, how] of [['arp', 'ARP', 'play'], ['glitch', 'GLITCH', 'play'], ['drift', 'ROBIN', 'play'], ['chop', 'CHOP', 'play'], ['arp', 'ARP', 'ending'], ['drift', 'ROBIN', 'ending']]) {
    await settle ();
    await openCard (m); await sleep (500);
    const pre = await snap ();
    await p.evaluate (() => window.__startPlay ());
    await sleep (900);
    /* away from home — and for the mid-ending case far enough that the walk is still owed 120 ms after the stop */
    try { await p.waitForFunction ((far) => { const c = window.__cardSnap (); if (! c || c.home) return false;
      return ! far || (c.card === 'arp' ? c.led >= 2 && c.led <= 9 : true); }, { polling: 30, timeout: 4000 }, how === 'ending'); } catch (e) {}
    let away;
    if (how === 'play') { away = await snap (); await p.evaluate (() => window.__closeCards ()); await sleep (60); await p.evaluate (() => window.__stopPlay ()); }
    else { await p.evaluate (() => window.__stopPlay ()); await sleep (120); away = await snap (); await p.evaluate (() => window.__closeCards ()); }
    await sleep (300); await settle (); await sleep (800);
    await p.evaluate ((m) => window.__openFlowCard (m), m); await p.evaluate (() => window.__raf2 ());
    const shots = [await snap ()]; for (let i = 0; i < 25; i++) { await sleep (120); shots.push (await snap ()); }
    const first = shots[0], changes = shots.filter ((s, i) => i && J (s) !== J (shots[i - 1])).length;
    const ok = !! (pre && away && first) && pre.home && ! away.home && first.home && changes === 0 && J (first) === J (pre);
    reo.push ({ ok, txt: `${name} closed mid-${how} (${away && ! away.home ? 'away from home' : 'AT HOME — the case is untested'}) → reopened at rest: first frame ${first && first.home ? 'home' : 'NOT home ' + J (first).slice (0, 120)}, `
      + `${changes} picture changes in 3 s, ${first && pre && J (first) === J (pre) ? 'the pre-note picture' : 'NOT the pre-note picture'}` });
  }
  await p.evaluate (() => window.__closeCards ());
  gate (reo.every (x => x.ok), '[2] 🚨 A CARD REOPENED AT REST IS HOME, AND STILL — an ending nobody watched is not resumed with no MIDI', reo.map (x => x.txt).join (' | '));

  // ── 3 · THE SYNTH PAGE SHOWN AGAIN AT REST ─────────────────────────────────────────────────
  const shw = [];
  for (const how of ['play', 'ending']) {
    await settle (); await sleep (300);
    const h0 = await p.evaluate (() => window.__emHash ()), e0 = await p.evaluate (() => window.__emState ());
    await p.evaluate (() => window.__startPlay ()); await sleep (1300);
    let owed;
    if (how === 'play') { await p.evaluate (() => window.__showSyn (false)); await sleep (40); await p.evaluate (() => window.__stopPlay ()); }
    else { await p.evaluate (() => window.__stopPlay ()); await sleep (100); owed = await p.evaluate (() => window.__emState ()); await p.evaluate (() => window.__showSyn (false)); }
    await sleep (300); await settle (); await sleep (2300);
    const stale = await p.evaluate (() => window.__emState ());
    await p.evaluate (() => { window.__PT = []; window.__PTREC = 1; window.__showSyn (true); });
    await sleep (60);
    const tapAt = await p.evaluate (() => { window.__tap (); return performance.now (); });
    await p.evaluate (() => window.__raf2 ());
    const first = await p.evaluate (() => ({ st: window.__emState (), hash: window.__emHash () }));
    await sleep (2500);
    const end = await p.evaluate (() => ({ st: window.__emState (), hash: window.__emHash (), pt: window.__PT.slice (), busy: window.__tiWindBusy () }));
    await p.evaluate (() => { window.__PTREC = 0; });
    const late = end.pt.filter (t => t > tapAt + 450 + 120).length, total = end.pt.length;
    /* mid-play: the note ends while hidden, so em-drv's last call was live and its ending is owed at the show — the case the
       review measured. Mid-ending: em-drv is parked when hidden; the page tail's cap (windSnap) lands a parked key it can no
       longer see if the cap comes within 2 s of its last call, so it may already be home at the show — either way the first
       frame must be home */
    const wasOwed = how === 'play' ? !! (stale && ! stale.home) : !! (owed && owed.park);
    const ok = !! (e0 && e0.home) && wasOwed && first.st && first.st.home && first.hash === h0 && end.st.home && end.hash === h0 && late === 0 && ! end.busy;
    shw.push ({ ok, txt: `hidden mid-${how} (em-drv ${how === 'ending' ? 'parked at the hide ' + !! (owed && owed.park) + ', ' + (stale && stale.home ? 'landed by the tail cap before the show' : 'still owed at the show') : 'owed at the show ' + J (stale)}) → shown at rest + one tap: `
      + `first frame em-drv ${first.st && first.st.home ? 'HOME' : 'NOT home ' + J (first.st)}, emblems ${first.hash === h0 ? 'the rest picture' : 'NOT the rest picture'}; `
      + `${total} painter frames in 2.5 s, ${late} after the hand's 450 ms tail · then ${end.hash === h0 ? 'the rest picture' : 'NOT the rest picture'}, still winding ${end.busy}` });
  }
  gate (shw.every (x => x.ok), '[3] 🚨 THE SYNTH PAGE SHOWN AGAIN AT REST — the DRV emblem is home on the first frame, and only the hand paints', shw.map (x => x.txt).join (' | '));

  // ── 4 · A SLOW CARD GETS HOME IN ≤ 2.5 s ───────────────────────────────────────────────────
  const slow = [];
  for (const [m, name, pid, key] of [['arp', 'ARP', 'FLOW_ARP_RATE', 'card-arp'], ['glitch', 'GLITCH', 'FLOW_GLI_RATE', 'card-gli']]) {
    await settle ();
    await p.evaluate ((pid) => window.Juce.getSliderState (pid).setNormalisedValue (0), pid);   /* rate index 0 = 1/1 */
    await openCard (m); await sleep (600);
    const pre = await snap ();
    await p.evaluate (() => window.__startPlay ());
    try { await p.waitForFunction (() => { const c = window.__cardSnap (); return c && ! c.home; }, { polling: 30, timeout: 6000 }); } catch (e) {}
    const tA = await p.evaluate ((k) => (window.__tiWindState (k) || {}).tau, key); await sleep (1000);
    const tB = await p.evaluate ((k) => (window.__tiWindState (k) || {}).tau, key);
    await p.evaluate (() => window.__stopPlay ());
    const t0 = Date.now (), seq = []; let h = null, maxT = 0;
    for (let i = 0; i < 160; i++) { const x = await p.evaluate ((k) => ({ c: window.__cardSnap (), T: ((window.__tiWindState (k) || {}).park || {}).T || 0 }), key);
      h = x.c; maxT = Math.max (maxT, x.T); if (h) seq.push (m === 'arp' ? h.led : +((/Slice (\d+)/.exec (h.foot || '') || [0, -1])[1]) - 1); if (h && h.home) break; await sleep (40); }
    const homeMs = Date.now () - t0;
    let fwd = true; for (let i = 1; i < seq.length; ++i) if (seq[i] < seq[i - 1] && ! (i === seq.length - 1 && seq[i] === 0)) fwd = false;
    await sleep (700); const h2 = await snap ();
    await p.evaluate ((pid) => window.Juce.getSliderState (pid).setNormalisedValue (0.5), pid);
    const speed = tB - tA;
    const ok = !! (pre && h && h2) && pre.home && speed > 0.3 && speed < 0.7 && h.home && homeMs <= 2900 && maxT > 0 && maxT <= 2.5 + 1e-9 && fwd && J (h2) === J (pre);
    slow.push ({ ok, txt: `${name} at 1/1 (played ${(+speed).toFixed (2)} steps/s) → ending T ${maxT.toFixed (2)} s, home ${h && h.home ? 'after ' + homeMs + ' ms' : 'NEVER in ' + homeMs + ' ms'}, `
      + `steps ${seq.filter ((v, i) => i === 0 || v !== seq[i - 1]).join (' ')} (${fwd ? 'forward' : 'BACKWARDS'}), ${J (h2) === J (pre) ? 'the pre-note picture' : 'NOT the pre-note picture'}` });
  }
  await p.evaluate (() => window.__closeCards ());
  gate (slow.every (x => x.ok), '[4] 🚨 A SLOW CARD GETS HOME IN ≤ 2.5 s — forward, and never faster than the element can play', slow.map (x => x.txt).join (' | '));

  // ── 5 · THE READOUTS COME HOME ─────────────────────────────────────────────────────────────
  const rdo = [];
  for (const [m, name, field, restV] of [['drift', 'ROBIN NOTES', 'notes', '0'], ['glitch', 'GLITCH FX', 'fx', '—']]) {
    await settle ();
    await openCard (m); await sleep (600);
    const pre = await snap ();
    await p.evaluate (() => window.__startPlay ());
    let saw = null;
    try { await p.waitForFunction ((f, v) => { const c = window.__cardSnap (); return c && c[f] !== v; }, { polling: 20, timeout: 8000 }, field, restV);
          await p.evaluate (() => window.__stopPlay ()); saw = (await snap ())[field]; } catch (e) { await p.evaluate (() => window.__stopPlay ()); }
    let h = null; const t0 = Date.now ();
    for (let i = 0; i < 120; i++) { h = await snap (); if (h && h.home) break; await sleep (50); }
    await sleep (800); const post = await snap ();
    const ok = !! (pre && post) && pre[field] === restV && saw != null && saw !== restV && post.home && post[field] === restV && J (post) === J (pre);
    rdo.push ({ ok, txt: `${name}: before the note '${pre && pre[field]}', stopped while it showed '${saw}', rested ${post && post.home ? 'home after ' + (Date.now () - t0 - 800) + ' ms' : 'NOT home'} showing '${post && post[field]}'`
      + ` · ${post && pre && J (post) === J (pre) ? 'the pre-note picture' : 'NOT the pre-note picture'}` });
  }
  await p.evaluate (() => window.__closeCards ());
  gate (rdo.every (x => x.ok), '[5] 🚨 THE READOUTS COME HOME — ROBIN NOTES 0 and GLITCH FX — after the note, as at boot', rdo.map (x => x.txt).join (' | '));

  // ── 6 · AND THEN IT STOPS ──────────────────────────────────────────────────────────────────
  await settle (); await sleep (300);
  const f6 = await p.evaluate (() => { window.__PROBE = 0; return 0; }); await sleep (1500);
  const r6 = await p.evaluate (() => ({ n: window.__PROBE, busy: window.__tiWindBusy () }));
  gate (r6.n === 0 && ! r6.busy, '[6] AND THEN IT STOPS — 0 painter frames at rest', `${r6.n} painter frames in 1.5 s at rest · still winding ${r6.busy}`);

  gate (errs.length === 0, '[7] NO PAGE ERRORS', errs.slice (0, 3).join (' | ') || 'clean');
  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close ();
  process.exit (fail === 0 ? 0 : 1);
}) ().catch (e => { console.error (e); process.exit (1); });
