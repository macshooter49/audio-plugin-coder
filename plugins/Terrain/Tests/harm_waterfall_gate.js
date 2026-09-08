// ══════════════════════════════════════════════════════════════════════════════════════════════
//  harm_waterfall_gate.js — fb589: BARS STAY PRIMARY; THE WATERFALL APPEARS ON THE TABLE FAMILY.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/harm_waterfall_gate.js [page.html]
//
//  Max: "I think the additive mode should only be waterfall or something. Let's talk about it."
//  Settled: the BARS are the additive engine's native picture and stay primary; the WATERFALL
//  appears where there is actually a table axis to scan — the Table family.
//
//  THE BARS
//   0  THE PANEL ACTUALLY LAID OUT — nothing below is asserted on a page that never rendered
//   1  BARS ARE PRIMARY — on Table with the view not engaged, the bars are what you see
//   2  THE TABLE FAMILY CAN ENGAGE IT — a click swaps to the wavetable canvas and hides the bars
//   3  🚨 THE SIX PROCEDURAL FAMILIES REFUSE THE CLICK. This is a REGRESSION bar, not a new
//      feature: before fb589 the toggle had no engine guard, so clicking a Harmonic display
//      flipped wtWaterfall.on AND persisted wt3dView_ through setWaterfallView while .osc-wave
//      was display:none — nothing drew and the saved view silently rotted. The same was true of
//      Sample / Granular / Resynth / Modal.
//   4  LEAVING Table RETURNS THE BARS — and returning to Table brings the waterfall back, because
//      state persists and nothing turns off by itself
//   5  THE DEPTH AXIS FOLLOWS HUE — the additive bank has no WT Frame knob; HUE selects the frame
//   6  🚨 THE SIGNATURE CONVERGES. cachedSig and __wtDisp are POSITIONAL. fb589 added a 13th slot
//      (hm). If the two sides disagree the table does not go stale — it re-bakes at the maximum
//      rate forever (the fb467 note in index.html says exactly this), which is a CPU fire that
//      looks like everything working.
//
//  ── fb601 — THE CHURN BAND (bars 7..11) ──────────────────────────────────────────────────────
//  fb600 shipped the band with its arithmetic, its "free and invisible at churn 0" claim and its
//  agreement with the audio fold asserted in COMMENTS and measured NOWHERE:
//  `grep -rl "churnBand|__harmChurnBand|churnSpan|churnTrace" Tests/` returned nothing. This gate
//  passed before those bars existed only because its stub never fed a churn — absence of
//  coverage, not coverage. The rig is the fb599/hue_track one (a synthetic 16-frame stack on
//  HARM/Table with the waterfall engaged), and the feed is the real C++ contract,
//  window.__harmChurnBand / window.__harmChurnRate, indexed a·b·c·d.
//
//   7  THE FEED REACHES THE BAND AT ALL — churnTrace is instrumented, so "the band drew" is a
//      COUNT, not a belief. (HARD LAW: a detector that can silently no-op prints whether it fired.)
//   8  THE BAND IS ON THE CANVAS — pixels that CHANGED against the no-feed baseline, counted.
//   9  🚨 CHURN 0 IS FREE AND INVISIBLE — the canvas at __harmChurnBand = 0 is BYTE-IDENTICAL to
//      no feed at all (every RGBA byte), churnTrace fires ZERO times in both, and the idle
//      repaint count over a 700 ms window is unchanged. fb600 claims all three; nothing measured
//      any of them.
//  10  🚨 THE FOLD IS THE INTERVAL — churnSpan(1.0, 0.2) is [0.80, 1.00], NOT [0.80, 1.20]. The
//      picture is a DEPTH INTERVAL on a bounded axis; an unfolded one would draw the bank reading
//      frames that do not exist, at exactly the two Hue settings (0 and 1) where the audio folds.
//  11  🚨 THE UI FOLD AGREES WITH THE AUDIO FOLD — HarmonicEngine.h's own two lines
//      (`x = x < 0.f ? -x : x;` then `if (x > 1.f) x = 2.f - x;`) are READ OFF DISK, and
//      churnFold is numerically nulled against them over the whole excursion. The drift indicator
//      is then swept a full period and must never leave the band, and the band must be TIGHT
//      (both endpoints actually reached) — a band wider than the excursion is a lie in the other
//      direction.
//
//  MUTATION CONTROL (fb421):  CHURN_BAND_MUTATE=1  the fb600 fold reverted to a raw pos±hw  -> [10][11]
//                             CHURN_BAND_MUTATE=2  the churn-0 early-out removed             -> [9]
//                             CHURN_BAND_MUTATE=3  the band drawn but with zero alpha        -> [8]
// ══════════════════════════════════════════════════════════════════════════════════════════════
const fs   = require ('fs');
const path = require ('path');
const puppeteer = require ('puppeteer-core');

const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');

// the JUCE bridge stub, lifted from fm_wtpage_gate.js, with a per-id value map so the HUE-vs-
// WT_FRAME question in bar 5 can actually be told apart.
const STUB = () => {
  window.__VALS = {};
  const mk = (id) => ({
    getScaledValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
    setScaledValue(){},
    getNormalisedValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
    setNormalisedValue(v){ window.__VALS[id]=v; },
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.__NATIVE_CALLS = [];
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
    getNativeFunction:(n)=>(...a)=>{ window.__NATIVE_CALLS.push([n].concat(a));
      return new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
        if(/getWaterfallView/i.test(n))return r('{}');
        if(/Json|JSON|getOscWavetable/i.test(n))return r('{}'); r(0);});},
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
};

let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail;
  console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

(async () => {
  const b = await puppeteer.launch ({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport ({ width: 820, height: 656, deviceScaleFactor: 2 });
  await p.evaluateOnNewDocument (STUB);
  await p.goto ('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise (r => setTimeout (r, 1600));
  await p.evaluate (() => { const sp = document.getElementById ('syn-panel');
                            if (sp) sp.style.display = 'block'; window.dispatchEvent (new Event ('resize')); });
  await new Promise (r => setTimeout (r, 2600));   // the glass/canvas binders run on a 200/800/2000ms ladder

  const r = await p.evaluate (() => {
    const out = { err: null };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      const W = window.wtWaterfall;
      if (! dev) { out.err = 'no osc device'; return out; }
      if (! W)   { out.err = 'no wtWaterfall'; return out; }
      dev.classList.add ('engine-harm');
      const sel = dev.querySelector ('.hm-mode-select');
      if (! sel) { out.err = 'no family select'; return out; }
      const wave = dev.querySelector ('.osc-wave'), bars = dev.querySelector ('.harm-view');
      out.devW = dev.getBoundingClientRect().width;
      out.hasCanvas = !! document.getElementById ('osc-wave-a');

      const setFam = (i) => { sel.value = String (i); sel.dispatchEvent (new Event ('change', {bubbles:true})); };
      const shown = () => ({ wave: getComputedStyle (wave).display !== 'none',
                             bars: getComputedStyle (bars).display !== 'none',
                             wt3d: dev.classList.contains ('wt3d'), on: !! W.on['a'] });

      W.on['a'] = false; dev.classList.remove ('wt3d');
      setFam (6);
      out.tableBarsFirst = shown();                       // bar 1 — bars primary on Table

      W.toggle ('a');
      out.tableEngaged = shown();                         // bar 2 — Table can engage it

      // bar 3 — the six procedural families must REFUSE the click
      out.refused = [];
      for (let i = 0; i < 6; ++i) {
        setFam (i);
        W.on['a'] = false; dev.classList.remove ('wt3d');
        const before = !! W.on['a'];
        const nCalls = window.__NATIVE_CALLS.filter (c => c[0] === 'setWaterfallView').length;
        W.toggle ('a');
        const after = !! W.on['a'];
        const nCalls2 = window.__NATIVE_CALLS.filter (c => c[0] === 'setWaterfallView').length;
        out.refused.push ({ fam: i, changed: before !== after, persisted: nCalls2 > nCalls,
                            canW: W.canWaterfall ('a') });
      }

      // bar 4 — leave Table with the view engaged, bars must come back; return, waterfall returns
      setFam (6); W.on['a'] = false; dev.classList.remove ('wt3d'); W.toggle ('a');
      out.onTableEngaged = shown();
      setFam (2);                                          // leave Table (Console)
      out.leftTable = shown();
      setFam (6);                                          // come back
      out.returned = shown();

      // bar 5 — the depth axis is HUE on Table, WT_FRAME otherwise.
      // ⚠️ The PAGE installs its own window.Juce after load, so the evaluateOnNewDocument stub is
      //    no longer the one wtpos reads. Override getSliderState here, at assert time — the
      //    question this bar asks is purely "which parameter id does wtpos consult", so feeding
      //    those two ids distinguishable values is exactly the right probe.
      const VALS = { 'SYN_OSC_A_HARM_HUE': 0.83, 'SYN_OSC_A_WT_FRAME': 0.11 };
      out.probedIds = [];
      window.Juce.getSliderState = function (id) {
        out.probedIds.push (id);
        return { getNormalisedValue: () => (VALS[id] != null ? VALS[id] : 0.5),
                 getScaledValue:     () => (VALS[id] != null ? VALS[id] : 0.5),
                 setNormalisedValue () {}, setScaledValue () {},
                 valueChangedEvent: { addListener () { return { remove () {} }; }, removeListener () {} } }; };
      try { window.__wtFrameEff = [-1,-1,-1,-1]; } catch (e) {}
      setFam (6);
      out.probedIds = [];
      out.posOnTable  = W.wtpos ('a');
      out.idsOnTable  = out.probedIds.slice();
      setFam (1);
      out.probedIds = [];
      out.posOffTable = W.wtpos ('a');
      out.idsOffTable = out.probedIds.slice();

      // bar 6 — the positional signature must CONVERGE, not differ forever
      const payload = { wm:1, wa:0.2, w2m:2, w2a:0.3, fs:1, fa:0.4, sa:0.5, st:1,
                        bl:0.6, lo:0.1, hi:0.9, fm:1.25, hm:7.5 };
      W.cache['a'] = payload;
      window.__wtDisp = [[1,0.2,2,0.3,1,0.4,0.5,1,0.6,0.1,0.9,1.25,7.5],[],[],[]];
      out.shapeSig  = W.shapeSig ('a');
      out.cachedSig = W.cachedSig ('a');
      // and prove the bar bites: drop the 13th and they must diverge
      window.__wtDisp[0] = [1,0.2,2,0.3,1,0.4,0.5,1,0.6,0.1,0.9,1.25];
      out.shapeSigShort = W.shapeSig ('a');
    } catch (e) { out.err = String (e) + (e && e.stack ? ' | ' + e.stack.split('\n')[1] : ''); }
    return out;
  });

  console.log (`\n══ harm_waterfall_gate — fb589 ══  ${PAGE}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }

  const laidOut = r.devW > 100 && r.hasCanvas;
  gate (laidOut, '[0] THE PANEL ACTUALLY LAID OUT — the assertions below are real',
        `osc device width ${(r.devW||0).toFixed(1)} px, #osc-wave-a present=${r.hasCanvas}`);
  if (! laidOut) { console.log ('\n  ❌ degenerate page — not asserting anything on it\n');
                   await b.close(); process.exit (1); }

  const t1 = r.tableBarsFirst;
  gate (t1.bars && ! t1.wave,
        '[1] BARS ARE PRIMARY — Table with the view not engaged shows the bars',
        `bars=${t1.bars} waterfall canvas=${t1.wave} wt3d=${t1.wt3d}`);

  const t2 = r.tableEngaged;
  gate (t2.wave && ! t2.bars && t2.wt3d && t2.on,
        '[2] THE TABLE FAMILY CAN ENGAGE IT',
        `after one click: waterfall=${t2.wave} bars=${t2.bars} wt3d class=${t2.wt3d} on=${t2.on}`);

  /* fb599 — INVERTED ON PURPOSE. Max: "I only want to use tables for this ... I don't want the
     families anymore." The gather pins mainMode = 6 for every HARM oscillator, so there is no
     longer such a thing as a procedural family at run time: whatever index is STORED, the engine
     is on Table and the waterfall is its view. The old bar asserted the opposite and is exactly
     what tables-only retires. What must still hold is that a stored index cannot take the
     waterfall AWAY — a patch saved on Blade gets the same picture as one saved on Table. */
  const bad = r.refused.filter (f => ! f.canW);
  gate (bad.length === 0,
        '[3] EVERY STORED FAMILY GETS THE WATERFALL — tables-only, whatever the patch saved',
        bad.length ? ('refused: ' + JSON.stringify (bad))
                   : 'stored families 0-5 all report canWaterfall=true — the engine is on Table regardless');

  /* fb599 — there is no "leaving Table" any more, so the bar becomes its opposite: selecting a
     retired family must NOT drop the waterfall or bring the bars back. */
  gate (r.onTableEngaged.wave && r.leftTable.wave && ! r.leftTable.bars && r.returned.wave,
        '[4] A RETIRED FAMILY DOES NOT TAKE THE WATERFALL AWAY — the view never falls back to the bars',
        `on Table waterfall=${r.onTableEngaged.wave} → stored Console waterfall=${r.leftTable.wave}/bars=${r.leftTable.bars} → back ${r.returned.wave} (on stayed ${r.returned.on})`);

  /* fb599 — HUE on every stored family, because every one of them IS Table now. The additive bank
     has no WT Frame knob; reading SYN_OSC_x_WT_FRAME on a HARM oscillator would be the bug. */
  gate (Math.abs (r.posOnTable - 0.83) < 1e-6 && Math.abs (r.posOffTable - 0.83) < 1e-6,
        '[5] THE DEPTH AXIS FOLLOWS HUE — on every stored family, tables-only',
        `HUE=0.83 WT_FRAME=0.11 → stored Table wtpos=${r.posOnTable} (read ${(r.idsOnTable||[]).join(',') || 'nothing'}), stored Console wtpos=${r.posOffTable} (read ${(r.idsOffTable||[]).join(',') || 'nothing'})`);

  gate (r.shapeSig === r.cachedSig && r.shapeSig !== '' && r.shapeSigShort !== r.cachedSig,
        '[6] THE SIGNATURE CONVERGES — 13 slots, both sides, same order',
        `matched=${r.shapeSig === r.cachedSig} (13 slots); with the 13th dropped they diverge=${r.shapeSigShort !== r.cachedSig}`);

  // ════════════════════════════════════════════════════════════════════════════════════════════
  //  fb601 — THE CHURN BAND. Same page, same browser, same printer: reuse, do not duplicate.
  // ════════════════════════════════════════════════════════════════════════════════════════════
  //  THE AUDIO FOLD, READ OFF DISK. A harness that hardcodes the thing it is checking agreement
  //  with checks nothing (Tests/README.md's own warning about hardcoded rosters). If the engine's
  //  fold is ever rewritten these two regexes stop matching and bar [11] says so instead of
  //  quietly nulling against a stale copy.
  const ENG = path.resolve (path.dirname (PAGE), '..', '..', 'HarmonicEngine.h');
  let engSrc = ''; try { engSrc = fs.readFileSync (ENG, 'utf8'); } catch (e) {}
  const foldNeg = /x\s*=\s*x\s*<\s*0\.f\s*\?\s*-\s*x\s*:\s*x\s*;/.test (engSrc);
  const foldTop = /if\s*\(\s*x\s*>\s*1\.f\s*\)\s*x\s*=\s*2\.f\s*-\s*x\s*;/.test (engSrc);

  const CMUT = process.env.CHURN_BAND_MUTATE || '';
  const cb = await p.evaluate ((MUT) => {
    const o = { err: null, mut: MUT };
    try {
      const W = window.wtWaterfall, dev = document.querySelector ('#syn-panel .device.osc');
      const cv = document.getElementById ('osc-wave-a');
      if (! cv) { o.err = 'no #osc-wave-a'; return o; }

      // ── the rig, lifted from hue_track_gate: HARM/Table, waterfall engaged, a real stack ──
      dev.classList.add ('engine-harm');
      const sel = dev.querySelector ('.hm-mode-select');
      sel.value = '6'; sel.dispatchEvent (new Event ('change', { bubbles: true }));
      W.on['a'] = true; W.syncClass ('a');
      const KNOB = { 'SYN_OSC_A_HARM_HUE': 0.50 };
      window.Juce.getSliderState = (id) => ({
        getNormalisedValue: () => (KNOB[id] != null ? KNOB[id] : 0.5),
        getScaledValue:     () => (KNOB[id] != null ? KNOB[id] : 0.5),
        setNormalisedValue () {}, setScaledValue () {},
        valueChangedEvent: { addListener () { return { remove () {} }; }, removeListener () {} } });
      const N = 16, P = 160, D = new Float32Array (N * P);
      for (let f = 0; f < N; ++f)
        for (let i = 0; i < P; ++i) D[f * P + i] = 0.9 * Math.sin (2 * Math.PI * (1 + f) * i / (P - 1));
      W.cache['a'] = { n:N, p:P, nf:N, d:D }; W.prev['a'] = null; W.off['a'] = null;
      delete window.__wtDisp;                 // shapeSig '' ⇒ maybeRebake never fetches over our stack
      delete window.__harmHueEff;             // Hue comes from the knob above, held at 0.50
      o.isHarmTable = W.isHarmTable ('a');

      /* ── THE MUTATION SEAMS — a bar that has never failed has never been tested ────────────
         1  the fb600 fold reverted to the raw pos±hw the picture had before it
         2  the churn-0 early-out removed (the band drawn at whatever half-width arrives)
         3  the band drawn with zero alpha — "the code ran" without "the picture appeared" */
      //  1  the fold reverted to the raw pos±hw the picture had before fb600
      //  2  the C++ publishes kChurnDepth*kChurnDepthFloor (0.45*0.12 = 0.054) at churn 0 instead
      //     of 0 — the exact mistake the fb600 J3 gate exists to catch, since kChurnDepthFloor is
      //     the depth at knob 0+, NOT at 0. The band then paints on an untouched knob.
      //  3  the accent is fully transparent: churnTrace still fires, no pixel changes. "The code
      //     ran" and "the picture appeared" are different questions and bar [8] asks the second.
      window.__ZEROFEED = (MUT === '2') ? 0.45 * 0.12 : 0;
      if (MUT === '1') { W.churnSpan = (pos, hw) => [pos - hw, pos + hw]; W.churnFold = (x) => x; }
      if (MUT === '3') { W._accent = null; W.accent = () => 'rgba(0,0,0,0)'; }

      // ── INSTRUMENT churnTrace: "the band drew" becomes a COUNT, per the hard law ──────────
      if (! W.__fb601Wrapped) {
        const t0 = W.churnTrace.bind (W);
        W.churnTrace = function () { window.__TRACES = (window.__TRACES | 0) + 1; return t0.apply (null, arguments); };
        const d0 = W.draw.bind (W);
        W.draw = function (x) { window.__DRAWS = (window.__DRAWS | 0) + 1; return d0 (x); };
        W.__fb601Wrapped = 1;
      }
      const snap  = () => { const c = cv.getContext ('2d'); return Array.from (c.getImageData (0, 0, cv.width, cv.height).data); };
      const paint = () => { window.__TRACES = 0; W.off['a'] = null; W.draw ('a'); return { px: snap(), tr: window.__TRACES | 0 }; };
      const identical = (a, x) => { if (a.length !== x.length) return false;
                                    for (let i = 0; i < a.length; ++i) if (a[i] !== x[i]) return false; return true; };
      const changed = (a, x) => { let n = 0; for (let i = 0; i < a.length; i += 4)
                                    if (a[i] !== x[i] || a[i+1] !== x[i+1] || a[i+2] !== x[i+2] || a[i+3] !== x[i+3]) ++n;
                                  return n; };

      // A — NO FEED AT ALL (a browser preview, a pre-fb600 processor)
      delete window.__harmChurnBand; delete window.__harmChurnRate;
      const A = paint();
      // B — THE FEED IS PRESENT AND IT IS ZERO (churn knob at 0 on a live plugin)
      window.__harmChurnBand = [window.__ZEROFEED, 0, 0, 0]; window.__harmChurnRate = [0, 0, 0, 0];
      const B = paint();
      // C — A REAL BAND, rate 0 so the sweeping indicator is off and the frame is deterministic
      window.__harmChurnBand = [0.25, 0, 0, 0]; window.__harmChurnRate = [0, 0, 0, 0];
      const C = paint();
      // D — and with the drift indicator running
      window.__harmChurnRate = [2.0, 0, 0, 0];
      const Dd = paint();

      o.zeroIdentical = identical (A.px, B.px);
      o.zeroDiffBytes = (() => { let n = 0; for (let i = 0; i < A.px.length; ++i) if (A.px[i] !== B.px[i]) ++n; return n; })();
      o.trace  = { noFeed: A.tr, zero: B.tr, band: C.tr, drift: Dd.tr };
      o.litZero = changed (A.px, B.px);
      o.litBand = changed (A.px, C.px);
      o.litDrift = changed (C.px, Dd.px);
      o.pixels  = A.px.length / 4;

      // ── THE FOLD, as an INTERVAL ─────────────────────────────────────────────────────────
      const rnd = (v) => v.map (x => +x.toFixed (6));
      o.span = { top:  rnd (W.churnSpan (1.00, 0.20)),
                 bot:  rnd (W.churnSpan (0.00, 0.20)),
                 mid:  rnd (W.churnSpan (0.50, 0.20)),
                 near: rnd (W.churnSpan (0.95, 0.20)),
                 low:  rnd (W.churnSpan (0.05, 0.20)),
                 wide: rnd (W.churnSpan (0.50, 0.90)) };

      // ── churnFold vs the audio's own two lines, over the whole excursion ─────────────────
      const ref = (x) => { if (x < 0) x = -x; if (x > 1) x = 2 - x; return Math.max (0, Math.min (1, x)); };
      let fw = 0, fa = 0;
      for (let i = 0; i <= 800; ++i) { const x = -0.5 + i * (2.0 / 800);
        const d = Math.abs (W.churnFold (x) - ref (x)); if (d > fw) { fw = d; fa = x; } }
      o.foldWorst = fw; o.foldAt = fa;

      // ── the drift indicator never leaves the band, and the band is TIGHT ─────────────────
      //    churnBand sweeps pz = churnFold(pos + bw*sin(...)), so sample a full period.
      //    CONTAINMENT is checked on the REAL formula (a 720-step sine period). TIGHTNESS is
      //    checked on s = sin(...) swept linearly over its own range [-1,1] PLUS the two exact
      //    s where the excursion touches 0 and 1 — a sine grid lands near, not on, those points
      //    and reads a 3.3e-3 "slack" that is the sampling and not the band.
      let out = 0, worstSlack = 0, cases = 0;
      for (const pos of [0.00, 0.05, 0.25, 0.50, 0.75, 0.95, 1.00])
        for (const bw of [0.05, 0.20, 0.45, 0.90]) {
          const sp = W.churnSpan (pos, bw); ++cases;
          for (let k = 0; k <= 720; ++k) {                                     // the indicator, as shipped
            const pz = W.churnFold (pos + bw * Math.sin (2 * Math.PI * k / 720));
            if (pz < sp[0] - 1e-9 || pz > sp[1] + 1e-9) ++out; }
          let lo = 1e9, hi = -1e9;
          const ss = [];
          for (let k = 0; k <= 4000; ++k) ss.push (-1 + 2 * k / 4000);
          for (const edge of [0, 1]) { const sc = (edge - pos) / bw; if (sc >= -1 && sc <= 1) ss.push (sc); }
          for (const sv of ss) { const pz = W.churnFold (pos + bw * sv); if (pz < lo) lo = pz; if (pz > hi) hi = pz; }
          worstSlack = Math.max (worstSlack, Math.abs (lo - sp[0]), Math.abs (hi - sp[1]));
        }
      o.driftOut = out; o.bandSlack = worstSlack; o.driftCases = cases;
    } catch (e) { o.err = String (e) + (e && e.stack ? ' | ' + e.stack.split ('\n')[1] : ''); }
    return o;
  }, CMUT);

  // ── the idle repaint contract: two equal windows with the loop running, feed vs no feed ────
  const idleWindow = async (mode) => {
    await p.evaluate ((m) => { const W = window.wtWaterfall;
      if (m === 'none') { delete window.__harmChurnBand; delete window.__harmChurnRate; }
      else { window.__harmChurnBand = [window.__ZEROFEED || 0, 0, 0, 0]; window.__harmChurnRate = [0, 0, 0, 0]; }
      window.__DRAWS = 0; window.__TRACES = 0; W.on['a'] = true; W.syncClass ('a'); W.kick(); }, mode);
    await new Promise (r2 => setTimeout (r2, 700));
    return p.evaluate (() => ({ draws: window.__DRAWS | 0, traces: window.__TRACES | 0 }));
  };
  const idleNone = cb.err ? { draws: -1, traces: -1 } : await idleWindow ('none');
  const idleZero = cb.err ? { draws: -1, traces: -1 } : await idleWindow ('zero');

  if (cb.err) { gate (false, '[7] THE CHURN RIG', 'harness error: ' + cb.err); }
  else {
    gate (cb.isHarmTable && cb.trace.band > 0 && cb.pixels > 10000,
          '[7] THE FEED REACHES THE BAND — instrumented, so this is a COUNT and not a belief',
          `isHarmTable=${cb.isHarmTable}, canvas ${cb.pixels} px; churnTrace calls per repaint — `
        + `no feed ${cb.trace.noFeed} · churn 0 ${cb.trace.zero} · band 0.25 ${cb.trace.band} · +drift ${cb.trace.drift}`);

    gate (cb.litBand > 400,
          '[8] THE BAND IS ON THE CANVAS — lit pixels counted, not eyeballed',
          `__harmChurnBand=0.25 changes ${cb.litBand} pixels vs the no-feed baseline `
        + `(${(100 * cb.litBand / cb.pixels).toFixed (2)} % of the surface); the drift indicator adds `
        + `${cb.litDrift} more. Need > 400.`);

    gate (cb.zeroIdentical && cb.trace.zero === 0 && cb.trace.noFeed === 0
          && idleNone.draws > 0 && Math.abs (idleZero.draws - idleNone.draws) <= 2 && idleZero.traces === 0,
          '[9] 🚨 CHURN 0 IS FREE AND INVISIBLE — byte-identical to no feed at all',
          `canvas bytes differing between "no feed" and "__harmChurnBand=0": ${cb.zeroDiffBytes} of ${cb.pixels * 4} `
        + `(pixels ${cb.litZero}); churnTrace 0/0; idle repaints in 700 ms — no feed ${idleNone.draws}, `
        + `churn-0 feed ${idleZero.draws} (Δ ${idleZero.draws - idleNone.draws}, tolerance ±2), band draws in the `
        + `churn-0 window ${idleZero.traces}`);

    const eq = (a, x) => a.length === x.length && a.every ((v, i) => Math.abs (v - x[i]) < 1e-6);
    gate (eq (cb.span.top, [0.8, 1.0]) && eq (cb.span.bot, [0.0, 0.2]) && eq (cb.span.mid, [0.3, 0.7])
          && eq (cb.span.near, [0.75, 1.0]) && eq (cb.span.low, [0.0, 0.25]) && eq (cb.span.wide, [0.0, 1.0]),
          '[10] 🚨 THE FOLD IS THE INTERVAL — churnSpan(1.0, 0.2) = [0.80, 1.00], NOT [0.80, 1.20]',
          `pos 1.00 → [${cb.span.top}] · 0.95 → [${cb.span.near}] · 0.50 → [${cb.span.mid}] · `
        + `0.05 → [${cb.span.low}] · 0.00 → [${cb.span.bot}] · hw 0.90 at mid → [${cb.span.wide}]`);

    gate (foldNeg && foldTop && cb.foldWorst < 1e-9 && cb.driftOut === 0 && cb.bandSlack < 1e-6,
          '[11] 🚨 THE UI FOLD AGREES WITH THE AUDIO FOLD — read off HarmonicEngine.h, not copied',
          `HarmonicEngine.h carries "x = x < 0.f ? -x : x"=${foldNeg} and "if (x > 1.f) x = 2.f - x"=${foldTop}; `
        + `worst |churnFold − that| over −0.5..1.5 = ${cb.foldWorst.toExponential (2)} (at x=${(+cb.foldAt).toFixed (4)}); `
        + `drift samples outside the band over ${cb.driftCases} (pos, half-width) cases: ${cb.driftOut}; `
        + `worst band slack (the band must be TIGHT, not merely containing) ${cb.bandSlack.toExponential (2)}`);
  }

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail === 0 ? 0 : 1);
})();
