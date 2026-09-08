// ══════════════════════════════════════════════════════════════════════════════════════════════
//  lfo_park.js — fb567: THE LFO PLAYHEAD FADES OUT IN SILENCE, NEVER CREEPS, AND THE PAINTER RESTS.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/lfo_park.js [page.html]
//
//  Max (2026-09-02): "The LFO pauses, and then it just keeps going and it doesn't stop... The LFO
//  should never pause. It should fade out. The line and the dot should fade out and go away only
//  if there's no MIDI being played... Stop the animation loop when the MIDI is not inside and
//  there's nothing being played... Put the animation loop back whenever we are playing."
//
//  THE BUG. fb566 parked the DSP LFO in silence; the panel's painter (lfoFrame) still carried
//  fb217's fallback — "stale feed → the old simulation", ph += dt*effHz(). fb511 makes the feed go
//  stale at idle BY DESIGN (the modViz segment is dropped 1.5 s after the output falls silent), so
//  the moment the truth feed stopped the page's own clock took over and the dot kept sweeping — a
//  free-run the DSP was no longer doing. "It pauses, then it just keeps going."
//
//  THE BARS. The truth feed and the frame dispatcher are driven exactly as the C++ frame drives
//  them: window.__notesActive = <sounding>, __modViz(env, val, phase), then __tiFrame(); 60 Hz.
//   1  LIVE — while notes sound, the dot and the line ride the pushed phase: they move with it,
//      monotonically, share one x, and are fully visible.
//   2  NO CREEP — notes end, pushes stop, the dispatcher keeps firing (other bytes still change):
//      the dot does not move by a pixel across one full second, feed fresh or stale.
//   3  FADE — within 600 ms of the notes ending the line and the dot are at opacity 0.
//   4  REST — with the head faded, 60 dispatches touch NEITHER element (0 attribute mutations):
//      the painter is asleep, not repainting an invisible head.
//   5  RESUME — the first push with notes back brings the head to opacity 1 and it tracks again.
//   6  rAF MODE — a page with no frame dispatcher (the popped card's world) runs its own rAF
//      clock; in silence that loop STOPS (0 mutations over 700 ms) and a fresh push with notes
//      restarts it.
//   7  NO REGRESSION — zero page errors on the main page; lfo_val_smooth.js's extraction anchors
//      still exist in the source.
//   8  DEAD FEED — a popped card whose editor has closed receives nothing: the flag's freshness stamp
//      expires, the head parks and the loop stops; the next push with notes revives it.
//
//   9  FADE SHAPE (fb570) — the fade-out runs on ITS OWN .35 s curve: 150 ms after the notes end the dot
//      and the line are still > 20 % visible, the computed transition-duration in idle is 0.35s, and the
//      head is gone by 700 ms. Bar 3 alone could not tell a 120 ms fade from a 350 ms one — which is how
//      fb567 shipped a fade-out that lost its `transition` to the base rule by specificity (Max: "it just
//      static clicks out... it's supposed to fade away").
//  10  BREATH RESTS (fb570) — in silence the curve's breathing (mvBreathe) is PAUSED, not removed: no
//      blink of the line at the park edge, nothing in the panel moves; it breathes again on the first note.
//
//  PROOF THE BARS CAN FAIL: against 9356ab0's page bars 2, 3, 4 and 6 are red; against d493db0..8e6b551
//  (fb567-569) bars 9 and 10 are red.
//    LFO_PARK_MUTATE=1  the pre-fb567 painter: simulation back, no rest       → bars 2, 4 red
//    LFO_PARK_MUTATE=2  the idle class is never applied                         → bars 3, 4 red
//    LFO_PARK_MUTATE=3  the fb567 idle selectors (lose the cascade)              → bars 9, 10 red
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE = process.argv[2] || path.join(ROOT, 'Source/ui/public/index.html');
const MUT = +(process.env.LFO_PARK_MUTATE || 0);
const VW = 820, VH = 656;
let pass = 0, fail = 0;
const chk = (ok, l, d) => { if (ok) { pass++; console.log('  ok    ' + l + (d ? '   ' + d : '')); }
                            else { fail++; console.log('  FAIL  ' + l + (d ? '   ' + d : '')); } };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split(f).length - 1;
    if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); }
    src = src.replace(f, t); console.log('  (mutation ' + MUT + ' landed: 1 site)'); };
  if (MUT === 1) { sub("else if(!window.Juce&&!window.__JUCE__){ ph+=dt*effHz();", "else { ph+=dt*effHz();");
                   sub("if(!live){ if(__lfoIdle) return; __lfoIdle=true;", "if(!live){ if(false) return; __lfoIdle=true;"); }
  if (MUT === 2) sub("function lfoIdle(on){", "function lfoIdle(on){ return;");
  if (MUT === 3) { sub("#mod-engine.mv-idle .mv-play,#mod-engine.mv-idle .mv-foll,.lfo-ext.mv-idle .card-scope .mv-play,.lfo-ext.mv-idle .card-scope .mv-foll,.mv-ext.mv-idle .es .mv-play,.mv-ext.mv-idle .es .mv-foll{opacity:0;transition:opacity .35s ease;}",
                        ".mv-idle .mv-play,.mv-idle .mv-foll{opacity:0;transition:opacity .35s ease;}");
                   sub("#mod-engine.mv-idle .mv-stroke,.mv-ext.mv-idle .es .mv-stroke{animation-play-state:paused;}", ".mv-idle .mv-stroke{animation:none;}"); }
  const p = path.join(os.tmpdir(), 'lfo_park_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}

// the fb563 recording stub, VERBATIM (its choice cardinalities keep the page's boot error-free)
const WARP_N = (() => {   // the warp lane's cardinality, read from the C++ (all_menus.js's own extraction)
  const s = fs.readFileSync(path.join(ROOT, 'Source/PluginProcessor.cpp'), 'utf8');
  const m = /for \(int i = w\.size\(\); i < (\d+); \+\+i\) w\.add \("Reserved "/.exec(s);
  if (! m) throw new Error('warp cardinality not found in PluginProcessor.cpp');
  return +m[1];
})();
const STUB = (cfg) => {
  window.__emits = []; window.__natives = [];
  const CH = cfg.choice; const states = new Map();
  const mk = (name) => { const n = CH[name] || 0;
    const props = n ? { start:0, end:n-1, skew:1, name, label:'', numSteps:n, interval:1, parameterIndex:states.size }
                    : { start:0, end:1, skew:1, name, label:'', numSteps:100, interval:0, parameterIndex:states.size };
    const st = { get scaledValue(){ return st.norm; },   // the front page reads .scaledValue as a PROPERTY (state.outputGain) — without it init() throws
      name, norm:(name === 'SYN_BEND_RANGE' ? 2/24 : 0), properties:props,   // the bend range's registered default (2 st) — the only non-zero default the bars read back
      getScaledValue(){ return n ? Math.round(st.norm*(n-1)) : st.norm; },
      setScaledValue(v){ st.norm = n ? v/(n-1) : v; },
      getNormalisedValue(){ return st.norm; },
      setNormalisedValue(v){ st.norm = n ? Math.round(v*(n-1))/(n-1) : v;
        window.__emits.push({ name, norm:+(+v).toFixed(6) }); (st.__ls||[]).forEach(f => { try { f(); } catch(e){} }); },
      valueChangedEvent:{ addListener(f){ (st.__ls = st.__ls || []).push(f); return {remove(){}}; }, removeListener(){} },
      propertiesChangedEvent:{ addListener(){ return {remove(){}}; }, removeListener(){} },
      getChoiceIndex(){ return Math.round(st.norm*(n-1)); }, setChoiceIndex(i){ st.norm = i/(n-1); },
      getValue:()=>false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} };
    return st; };
  const get = (name) => { if (! states.has(name)) states.set(name, mk(name)); return states.get(name); };
  window.__stubState = get;
  const nativeFn = (nm) => (...a) => new Promise((r) => { window.__natives.push({ fn:nm, args:a.map(String) });
    if (/getPresets/i.test(nm)) return r('[]'); if (/getSynthMod$/.test(nm)) return r('[]');
    if (/^getSynParam$/.test(nm)) return r(String(get(String(a[0])).getNormalisedValue()));   // the relay-free read-back, from the same stub state the writes land in
    if (/Json|JSON/.test(nm)) return r('{}'); r(0); });
  window.Juce = { getSliderState:get, getToggleState:get, getComboBoxState:get, getNativeFunction:nativeFn,
                  backend:{ addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable:true,
    get(){ return held; }, set(v){ held = Object.assign({}, v||{}, { getNativeFunction:mine.getNativeFunction, getSliderState:mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend:window.Juce.backend, initialisationData:{ vendor:'', pluginName:'', pluginVersion:'',
    __juce__sliders:[], __juce__toggles:[], __juce__comboBoxes:[], __juce__functions:[] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
  window.__errStacks = []; window.addEventListener('error', (e) => { try { window.__errStacks.push(String((e.error && e.error.stack) || e.message).slice(0, 400)); } catch(x){} });
};

// in-page helpers: the C++ frame's exact door (sounding flag → __modViz → __tiFrame), and readers
const HELPERS = () => {
  const raf = () => new Promise(r => requestAnimationFrame(() => r()));
  const Z = () => [0,0,0,0,0,0,0,0,0,0];
  window.__lp = {
    async push (ph, notes, fire) {   // one C++ frame: the sounding flag, the truth feed, the dispatcher
      window.__notesActive = notes ? 1 : 0; window.__notesActiveT = Date.now();   // the flag and its freshness stamp, as the C++ writes them
      const P = Z(); P[0] = ph; const L = Z(); L[0] = 0.5;
      window.__modViz([], L, P);
      if (fire !== false && window.__tiFrame) window.__tiFrame();
      await raf(); await raf();
    },
    async fire () { window.__notesActiveT = Date.now(); if (window.__tiFrame) window.__tiFrame(); await raf(); },   // a shipped frame whose bytes changed elsewhere (every shipped frame re-stamps the flag)
    head () {
      const fd = document.querySelector('#mod-engine #mv-fd'), pl = document.querySelector('#mod-engine #mv-ph');
      if (! fd || ! pl) return null;
      return { cx: +fd.getAttribute('cx'), x1: +pl.getAttribute('x1'),
               o: +getComputedStyle(fd).opacity, ol: +getComputedStyle(pl).opacity };
    },
    watch () {   // count attribute writes on the two head elements until stop()
      const fd = document.querySelector('#mod-engine #mv-fd'), pl = document.querySelector('#mod-engine #mv-ph');
      let n = 0; const mo = new MutationObserver(ms => { n += ms.length; });
      mo.observe(fd, { attributes:true }); mo.observe(pl, { attributes:true });
      window.__lpStop = () => { mo.disconnect(); return n; };
    }
  };
};

async function boot (pg, P, rafMode) {
  if (rafMode) await pg.evaluateOnNewDocument(() => {   // no dispatcher at all: the LFO module boots on its own rAF clock
    Object.defineProperty(window, '__tiFrameReg', { get(){ return undefined; }, set(){}, configurable:true }); });
  const choice = {}; ['A','B','C','D'].forEach(o => { choice['SYN_OSC_' + o + '_WARP_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_WARP2_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_ENGINE'] = 7; });
  await pg.evaluateOnNewDocument(STUB, { choice }); await pg.evaluateOnNewDocument(HELPERS);
  await pg.goto('file://' + P, { waitUntil:'load', timeout:60000 }); await sleep(2400);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late');
    document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block';
    try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await sleep(900);
}

(async () => {
  const P = mutatedPage();
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:VW, height:VH, deviceScaleFactor:2 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await boot(pg, P, false);

  console.log('\n══ fb567 — THE LFO PLAYHEAD PARKS, FADES AND RESTS ══');
  console.log('   page ' + P + (MUT ? '   MUTATION ' + MUT : '') + '\n');

  const h0 = await pg.evaluate(() => window.__lp.head());
  chk(!! h0 && h0.o < 0.01, '0  the LFO panel rendered its playhead line and dot — HIDDEN, since no note has sounded', JSON.stringify(h0));
  if (! h0) { console.log('\n══ RESULT: ' + pass + ' pass, ' + fail + ' FAIL ══'); await b.close(); process.exit(1); }

  // ── 1 · LIVE ────────────────────────────────────────────────────────────────────────────
  const live = await pg.evaluate(async () => { const out = [];
    await window.__lp.push(0.25, 1); await new Promise(r => setTimeout(r, 250));   // the head starts hidden (no note at boot); the fade-in is .12 s
    for (const ph of [0.25, 0.5, 0.75]) { await window.__lp.push(ph, 1); out.push(window.__lp.head()); } return out; });
  const mono = live[0].cx < live[1].cx && live[1].cx < live[2].cx;
  const sameX = live.every(h => Math.abs(h.cx - h.x1) < 0.6);
  const vis = live.every(h => h.o > 0.99 && h.ol > 0.99);
  chk(mono && sameX && vis, '1  LIVE: the dot and the line ride the pushed phase, one x, fully visible',
      live.map(h => h.cx.toFixed(1)).join(' → ') + '  opacity ' + live[2].o + '/' + live[2].ol);

  // ── 2 · NO CREEP + 3 · FADE ─────────────────────────────────────────────────────────────
  // notes end: two more frames carry the held phase (the DSP holds, the feed is still fresh),
  // then pushes stop and the feed goes stale — while the dispatcher keeps firing for a second.
  const creep = await pg.evaluate(async () => {
    await window.__lp.push(0.75, 0); await window.__lp.push(0.75, 0);
    const t0 = performance.now(), cx0 = window.__lp.head().cx; let maxD = 0, fadeAt = -1, n = 0;
    while (performance.now() - t0 < 1000) {
      await window.__lp.fire(); n++;
      const h = window.__lp.head(); maxD = Math.max(maxD, Math.abs(h.cx - cx0));
      if (fadeAt < 0 && h.o < 0.01 && h.ol < 0.01) fadeAt = performance.now() - t0;
      await new Promise(r => setTimeout(r, 12));
    }
    return { cx0, maxD, fadeAt, n, end: window.__lp.head() }; });
  chk(creep.maxD < 0.01, '2  NO CREEP: notes ended, pushes stopped, ' + creep.n + ' dispatches — the dot did not move',
      'moved ' + creep.maxD.toFixed(1) + ' px from x=' + creep.cx0.toFixed(1));
  chk(creep.fadeAt >= 0 && creep.fadeAt < 600, '3  FADE: the line and the dot reach opacity 0 within 600 ms of the notes ending',
      creep.fadeAt < 0 ? 'never faded (opacity ' + creep.end.o + '/' + creep.end.ol + ')' : creep.fadeAt.toFixed(0) + ' ms');

  // ── 4 · REST ────────────────────────────────────────────────────────────────────────────
  const rest = await pg.evaluate(async () => {
    await new Promise(r => setTimeout(r, 200));   // past the fade
    window.__lp.watch();
    for (let i = 0; i < 60; i++) { await window.__lp.fire(); }
    return window.__lpStop(); });
  chk(rest === 0, '4  REST: 60 dispatches in silence touch neither the line nor the dot', rest + ' attribute writes');

  // ── 5 · RESUME ──────────────────────────────────────────────────────────────────────────
  const res = await pg.evaluate(async () => {
    await window.__lp.push(0.3, 1); const a = window.__lp.head();
    const t0 = performance.now(); let o = a.o;
    while (performance.now() - t0 < 500 && o < 0.99) { await window.__lp.push(0.3, 1); o = window.__lp.head().o; }
    const at = performance.now() - t0;
    await window.__lp.push(0.6, 1); const c = window.__lp.head();
    return { a, at, o, c }; });
  chk(res.o > 0.99 && res.c.cx > res.a.cx && Math.abs(res.c.cx - res.c.x1) < 0.6,
      '5  RESUME: notes back — opacity 1 within 500 ms and the head tracks the phase again',
      'opacity ' + res.o + ' after ' + res.at.toFixed(0) + ' ms; x ' + res.a.cx.toFixed(1) + ' → ' + res.c.cx.toFixed(1));

  // ── 9 · FADE SHAPE + 10 · BREATH RESTS (fb570) ──────────────────────────────────────────
  // The park edge is driven exactly as bar 2 drives it, but the clock starts BEFORE the dispatcher
  // fires so the samples are timed from the frame that carries the flag off, as Max's eye is.
  const fade = await pg.evaluate(async () => {
    const raf = () => new Promise(r => requestAnimationFrame(() => r()));
    await window.__lp.push(0.3, 1); await window.__lp.push(0.4, 1); await new Promise(r => setTimeout(r, 250));
    const fd = document.querySelector('#mod-engine #mv-fd'), pl = document.querySelector('#mod-engine #mv-ph');
    const stroke = document.querySelector('#mod-engine .mv-stroke'), root = document.getElementById('mod-engine');
    const o0 = +getComputedStyle(fd).opacity, ps0 = stroke ? getComputedStyle(stroke).animationPlayState : '';
    window.__notesActive = 0; window.__notesActiveT = Date.now();
    const P = [0.4,0,0,0,0,0,0,0,0,0], L = [0.5,0,0,0,0,0,0,0,0,0]; window.__modViz([], L, P);
    const t0 = performance.now(); if (window.__tiFrame) window.__tiFrame();
    const samples = []; let dur = '', idleAt = -1, ps1 = '';
    while (performance.now() - t0 < 700) {
      await raf(); const t = performance.now() - t0;
      if (idleAt < 0 && root.classList.contains('mv-idle')) { idleAt = t; dur = getComputedStyle(fd).transitionDuration; ps1 = stroke ? getComputedStyle(stroke).animationPlayState : ''; }
      samples.push([t, +getComputedStyle(fd).opacity, +getComputedStyle(pl).opacity]);
    }
    const at = (ms) => { let b = samples[0]; for (const s of samples) if (Math.abs(s[0] - ms) < Math.abs(b[0] - ms)) b = s; return b; };
    // and the first note brings the breath back
    await window.__lp.push(0.5, 1); await raf(); const ps2 = stroke ? getComputedStyle(stroke).animationPlayState : '';
    return { o0, ps0, idleAt, dur, ps1, ps2, s150: at(150), s300: at(300), end: samples[samples.length - 1], n: samples.length }; });
  chk(fade.o0 > 0.99 && fade.idleAt >= 0 && fade.dur === '0.35s' && fade.s150[1] > 0.2 && fade.s150[2] > 0.2 && fade.end[1] < 0.01 && fade.end[2] < 0.01,
      '9  FADE SHAPE: the fade-out runs on the .35 s curve — still > 20 % visible at 150 ms, transition 0.35s, gone by 700 ms',
      'idle at ' + fade.idleAt.toFixed(0) + ' ms, transition ' + (fade.dur || '?') + ', opacity @150 ms ' + fade.s150[1].toFixed(2) + '/' + fade.s150[2].toFixed(2)
      + ', @300 ms ' + fade.s300[1].toFixed(2) + ', end ' + fade.end[1].toFixed(2) + ' (' + fade.n + ' samples)');
  chk(fade.ps0 === 'running' && fade.ps1 === 'paused' && fade.ps2 === 'running',
      '10 BREATH RESTS: the curve\'s breathing is PAUSED in silence (no blink) and runs again on the first note',
      'live ' + fade.ps0 + ' → idle ' + fade.ps1 + ' → note ' + fade.ps2);

  // ── 7 · NO REGRESSION (main page) ───────────────────────────────────────────────────────
  chk(errs.length === 0, '7  zero page errors on the main page', errs.length ? errs.join(' | ') : '');
  const src = fs.readFileSync(PAGE, 'utf8');
  chk(src.indexOf('window.__modViz=function(e,l,p){') > 0 && src.indexOf('window.__mvLfoValAt=function(i){') > 0
      && src.indexOf('return isFinite(out)?out:cur[i]; };') > 0, '7  lfo_val_smooth.js\'s extraction anchors still exist');

  // ── 6 · rAF MODE (no dispatcher: the popped card\'s clock) ──────────────────────────────
  const pg2 = await b.newPage(); await pg2.setViewport({ width:VW, height:VH, deviceScaleFactor:2 });
  const errs2 = []; pg2.on('pageerror', e => errs2.push(String(e).slice(0, 160)));
  await boot(pg2, P, true);
  const rafm = await pg2.evaluate(async () => {
    if (! window.__lp.head()) return { none:true };
    const raf = () => new Promise(r => requestAnimationFrame(() => r()));
    // live: the loop follows pushes (no __tiFrame exists here)
    await window.__lp.push(0.25, 1, false); await raf(); const a = window.__lp.head();
    await window.__lp.push(0.6, 1, false); await raf(); await raf(); const c = window.__lp.head();
    // silence: notes end, pushes stop → the loop must stop
    await window.__lp.push(0.6, 0, false); await raf(); await raf();
    await new Promise(r => setTimeout(r, 900));
    const o = window.__lp.head();
    window.__lp.watch(); await new Promise(r => setTimeout(r, 700)); const idleWrites = window.__lpStop();
    // a fresh push with notes restarts it
    await window.__lp.push(0.4, 1, false);
    window.__lp.watch(); await new Promise(r => setTimeout(r, 400)); const backWrites = window.__lpStop();
    const e = window.__lp.head();
    // DEAD FEED (the popped card after its editor closed): live, then NOTHING arrives — no flag, no phase, no
    // stamp — while notes may well be sounding unseen. No truth → the head parks and the loop stops.
    await window.__lp.push(0.3, 1, false); await raf(); const d0 = window.__lp.head();
    await new Promise(r => setTimeout(r, 1200));
    const d1 = window.__lp.head();
    window.__lp.watch(); await new Promise(r => setTimeout(r, 500)); const deadWrites = window.__lpStop();
    await window.__lp.push(0.7, 1, false); await new Promise(r => setTimeout(r, 300)); const d2 = window.__lp.head();
    return { a, c, o, idleWrites, backWrites, e, d0, d1, deadWrites, d2 }; });
  if (rafm.none) chk(false, '6  rAF MODE: the LFO panel rendered without a dispatcher');
  else {
    chk(rafm.c.cx > rafm.a.cx, '6  rAF MODE: live, the loop follows the pushed phase', rafm.a.cx.toFixed(1) + ' → ' + rafm.c.cx.toFixed(1));
    chk(rafm.o.o < 0.01 && rafm.idleWrites === 0, '6  rAF MODE: in silence the head is faded and the rAF loop has STOPPED (0 writes in 700 ms)',
        'opacity ' + rafm.o.o + ', ' + rafm.idleWrites + ' writes');
    chk(rafm.backWrites > 0 && rafm.e.o > 0.99, '6  rAF MODE: a push with notes restarts the loop and the head returns',
        rafm.backWrites + ' writes, opacity ' + rafm.e.o);
    chk(rafm.d0.o > 0.99 && rafm.d1.o < 0.01 && rafm.deadWrites === 0 && rafm.d2.o > 0.99,
        '8  DEAD FEED: live, then nothing arrives for 1.2 s — the head parks and the loop stops; the next push revives it',
        'opacity ' + rafm.d0.o + ' → ' + rafm.d1.o + ' (' + rafm.deadWrites + ' writes) → ' + rafm.d2.o);
  }
  if (errs2.length) console.log('     (rAF-mode page errors, informational: ' + errs2.join(' | ') + ')');

  console.log('\n══ RESULT: ' + pass + ' pass, ' + fail + ' FAIL ══\n');
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
