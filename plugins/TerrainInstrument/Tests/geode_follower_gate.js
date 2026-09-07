// ══════════════════════════════════════════════════════════════════════════════════════════════
//  geode_follower_gate.js — fb598: THE RESYNTH FOLLOWER IS A MIDI VISUAL — NEVER A STATIC LINE.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/geode_follower_gate.js [page.html]
//
//  Max (2026-09-06): "there's a static white line always stuck in the middle, a white follower. I'm not
//  playing anything ... it's stuck and won't move ... whenever MIDI stops going in everything just
//  freezes, but this doesn't unfreeze ... the follower is simply just a MIDI response visual and it
//  wouldn't be of use if it was just a static line in the middle."
//
//  THE MECHANISM (both halves measured):
//   · C++ — SynthVoice.h:6273-6309: the release fast-kill (level < 1e-4 while in Release) arms an 8 ms
//     fade, then frees the slot WITHOUT ampEnv_.reset() (the steal path at 5803 does reset). The env is
//     never ticked again (3043 returns once playing_ is false), so isAmpEnvActive() stays true, and the
//     follower gather (PluginProcessor.cpp:10995-11007) publishes that dead voice's geode read head
//     forever. A pluck (sustain 0) parks EVERY note; a long release parks at ≥ ~2.3 s. env_park_cert.cpp.
//   · Page — updateSampleFollower keeps the last list in __geodeFollow[o] and on the .samp-ph pool; the
//     Resynth canvas painter draws that list on every paint. The frame goes byte-identical → the editor
//     idle-skips → nothing ever arrives to clear it (index.html, the SAMPLE-FOLLOWER push + the geode IIFE).
//
//  THE HOST IS SIMULATED FAITHFULLY, page-side: a 60 Hz tick builds the same frame string the editor
//  ships (notes flag + stamp, four updateSampleFollower pushes, __geodeSpectrum, __tiFrame), hashes it,
//  idle-skips a byte-identical frame and stamps the keepalive (__tickT / __tiAlive) every 30 skips —
//  exactly PluginEditor.cpp's fb483/fb511 ship block. So "a parked voice" is one whose list never
//  changes: the page then hears only keepalives, which is what Max's plugin does at rest.
//
//  THE BARS
//   0  LAID OUT + THE PICTURE IS PAINTED — the Resynth canvas has its baked spectrogram at rest (fb577/fb590)
//   1  REST ⇒ NO HEAD — no voice ever sounded: 0 head pixels on the canvas, no .samp-ph line on
//   2  SOUNDING ⇒ HEAD PRESENT AND MOVING — a voice scans: a full-height line, and it travels
//   3  STOP ⇒ GONE — the C++ reports no voice: the head is gone within 30 frames (the .4 s fade included)
//   4  A STALLED FEED ⇒ GONE, PICTURE STAYS — the shipped-C++ failure (a list that never changes, then only
//      keepalives): the head must be gone within 1.5 s, and the spectrogram must still be there
//   5  THE FEED RETURNS ⇒ THE HEAD RETURNS — a new note after the stall draws its head again within 10 frames
//   4b THE SAME STALL WITH rAF DEAD — fb577's trap (WebKit suspends rAF on a hidden/occluded page): the clear must
//      reach the canvas directly, and the picture must stay
//   6  ZERO FRAMES AT REST — the clears dispatched no painter frame: the frame clock stayed parked
//
//  RED ON THE SHIPPED PAGE: bar 4 (the head survives a stall forever). Mutations on the fixed page:
//      GEODE_FOLLOWER_MUTATE=1  the watchdog is removed                → 4 red
//      GEODE_FOLLOWER_MUTATE=2  the watchdog clears but never repaints → 4b red (rAF dead: nothing else paints)
//      GEODE_FOLLOWER_MUTATE=3  the repaint wipes instead of painting  → 4 red (the picture half), 0 intact
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require ('puppeteer-core');
const fs = require ('fs'), path = require ('path'), os = require ('os');
const ROOT = path.join (__dirname, '..');
const PAGE0 = process.argv[2] || path.join (ROOT, 'Source/ui/public/index.html');
let pass = 0, fail = 0;
const chk = (ok, l, d) => { if (ok) { pass++; console.log ('  ok    ' + l + (d ? '   ' + d : '')); } else { fail++; console.log ('  FAIL  ' + l + (d ? '   ' + d : '')); } };
const sleep = (ms) => new Promise (r => setTimeout (r, ms));
const STUB = () => { const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]'); if(/Json|JSON/.test(n))return r('{}'); r(0);}),backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',__juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}}; };

const MUT = +(process.env.GEODE_FOLLOWER_MUTATE || 0);
function mutatedPage () {
  if (! MUT) return PAGE0;
  let src = fs.readFileSync (PAGE0, 'utf8');
  const sub = (f, t) => { const n = src.split (f).length - 1; if (n !== 1) { console.error ('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice (0, 90)); process.exit (2); } src = src.replace (f, t); console.log ('   mutation ' + MUT + ' applied'); };
  if (MUT === 1) sub ("          if (! window.__followT || Date.now () - window.__followT < 700) return;", "          return;");
  if (MUT === 2) sub ("            if (window.__geodeFollowRepaint) window.__geodeFollowRepaint (o);", "");
  if (MUT === 3) sub ("    try { var s = st[o]; if (! s || ! s.on || (! s.ghost && ! s.bright)) return; paint (o); } catch (e) {}", "    try { var s = st[o]; if (! s || ! s.on) return; wipe (o); } catch (e) {}");
  const p = path.join (os.tmpdir(), 'geode_follower_mut' + MUT + '.html'); fs.writeFileSync (p, src); return p;
}

/* THE HOST, page-side — the editor's ship block (PluginEditor.cpp fb483 idle-skip + fb511 keepalive) at 60 Hz */
const HOST = () => {
  window.__host = (function () {
    var H = { on: false, list: { a: [], b: [], c: [], d: [] }, spec: null, notes: 0, sent: 0, skips: 0, keep: 0, lastJs: null, pre: null };
    function f4 (v) { return (Math.round (v * 10000) / 10000).toFixed (4); }
    function build () {
      var js = 'window.__notesActive=' + H.notes + ';window.__notesActiveT=Date.now();';
      ['a', 'b', 'c', 'd'].forEach (function (o) { var L = H.list[o] || [], parts = [];
        for (var i = 0; i + 1 < L.length; i += 2) parts.push ((L[i] | 0) + ',' + f4 (+L[i + 1]));
        js += "try{if(window.updateSampleFollower){window.updateSampleFollower('" + o + "',[" + parts.join (',') + "]);}}catch(e){}"; });
      if (H.spec) js += 'try{window.__geodeSpectrum&&window.__geodeSpectrum(' + H.spec + ');}catch(e){}';
      js += ';window.__tiFrame&&window.__tiFrame();';
      return js;
    }
    H.tick = function () {
      if (H.pre) H.pre ();
      var js = build ();
      if (js === H.lastJs) { if (++H.skips >= 30) { H.skips = 0; H.keep++; window.__tickT = performance.now (); window.__tiAlive && window.__tiAlive (); } return; }
      H.lastJs = js; H.skips = 0; H.sent++;
      try { (new Function (js)) (); } catch (e) { H.err = String (e); }
    };
    setInterval (function () { if (H.on) H.tick (); }, 1000 / 60);
    return H;
  }) ();
};

async function boot (P) {
  const b = await puppeteer.launch ({ executablePath: (process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'), headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const pg = await b.newPage (); await pg.setViewport ({ width: 820, height: 656, deviceScaleFactor: 2 });
  const errs = []; pg.on ('pageerror', e => errs.push (String (e).slice (0, 160)));
  await pg.evaluateOnNewDocument (STUB);
  await pg.evaluateOnNewDocument (HOST);
  await pg.goto ('file://' + P, { waitUntil: 'load', timeout: 60000 }); await sleep (1800);
  await pg.evaluate (() => { document.documentElement.classList.remove ('card-only-late'); document.querySelectorAll ('.ti-preboot').forEach (e => e.classList.remove ('ti-preboot'));
    const sp = document.getElementById ('syn-panel'); sp.classList.remove ('hidden'); sp.style.display = 'block'; try { document.getElementById ('syn-btn').click (); } catch (e) {} dispatchEvent (new Event ('resize')); });
  await sleep (600);
  return { b, pg, errs };
}

(async () => {
  const P = mutatedPage ();
  const { b, pg, errs } = await boot (P);
  console.log ('\n══ fb598 — THE RESYNTH FOLLOWER IS A MIDI VISUAL ══\n   page ' + P + (MUT ? '   MUTATION ' + MUT : '') + '\n');

  // osc A → Resynth, its view loaded, a synthetic spectrogram baked into the BOTTOM 31 % of the canvas
  // (bins 0..19 of 64, bin 0 = low = bottom), so any ink in the TOP 60 % can only be a follower line.
  await pg.evaluate (() => {
    const dev = document.getElementById ('osc-a-device'); ['engine-sample', 'engine-granular', 'engine-harm', 'engine-modal', 'engine-fm', 'uni-page', 'swapped'].forEach (c => dev.classList.remove (c)); dev.classList.add ('engine-geode');
    const view = document.querySelector ('#syn-panel .sample-view[data-osc="a"]'); view.classList.add ('loaded'); view.classList.add ('loopoff');
    let bytes = ''; for (let x = 0; x < 128; x++) for (let y = 0; y < 64; y++) bytes += String.fromCharCode (y < 20 ? 200 : 0);
    const b64 = btoa (bytes);
    window.__geodeImage ('a', 1, b64, b64, 128, 64);
    const e = []; for (let i = 0; i < 96; i++) e.push ('0.2500');
    window.__host.spec = '{a:{on:1,p:0.3000,e:[' + e.join (',') + ']},b:{on:0},c:{on:0},d:{on:0}}';
    window.__probeFrames = 0; window.__tiFrameReg ('probe', () => { window.__probeFrames++; });
    window.__host.on = true;
  });
  await sleep (700);

  const SAMPLE = () => pg.evaluate (() => {
    const out = { w: 0, h: 0, head: 0, headX: -1, pic: 0, domOn: 0, domOp: 0, sent: window.__host.sent, keep: window.__host.keep, frames: window.__probeFrames | 0, err: window.__host.err || null };
    const cv = document.querySelector ('#syn-panel .sample-view[data-osc="a"] canvas.samp-spec'); if (! cv) return out;
    out.w = cv.width; out.h = cv.height;
    const d = cv.getContext ('2d').getImageData (0, 0, cv.width, cv.height).data;
    const topH = Math.floor (cv.height * 0.6); let sx = 0;
    for (let y = 0; y < cv.height; y++) for (let x = 0; x < cv.width; x++) { const a = d[(y * cv.width + x) * 4 + 3]; if (a <= 8) continue;
      if (y < topH) { out.head++; sx += x; } else out.pic++; }
    if (out.head) out.headX = +(sx / out.head / cv.width).toFixed (3);
    document.querySelectorAll ('#syn-panel .sample-view[data-osc="a"] .samp-ph').forEach (L => { if (L.classList.contains ('on')) out.domOn++; out.domOp = Math.max (out.domOp, parseFloat (getComputedStyle (L).opacity) || 0); });
    return out;
  });
  const host = (fn) => pg.evaluate (fn);

  // 0 — laid out, the picture is there at rest
  const s0 = await SAMPLE ();
  chk (s0.w > 100 && s0.h > 40 && s0.pic > 500 && ! s0.err, '0  LAID OUT + THE PICTURE IS PAINTED at rest', `canvas ${s0.w}x${s0.h}, picture ${s0.pic} px lit, host frames ${s0.sent}` + (s0.err ? ' host err ' + s0.err : ''));

  // 1 — rest: no voice has ever sounded (the list has been [] since boot; the frame idle-skips, keepalives only)
  await sleep (1500);
  const s1 = await SAMPLE ();
  chk (s1.head === 0 && s1.domOn === 0 && s1.domOp < 0.05, '1  REST ⇒ NO HEAD — no voice ever: 0 head px, no line on', `head ${s1.head} px, .samp-ph on ${s1.domOn} (opacity ${s1.domOp}), keepalives ${s1.keep}`);

  // 2 — a voice sounds and scans: the head is there and it moves
  await host (() => { window.__host.notes = 1; let p = 0.10; window.__host.pre = () => { p = Math.min (0.95, p + 0.004); window.__host.list.a = [3, p]; }; });
  await sleep (400); const s2a = await SAMPLE (); await sleep (600); const s2b = await SAMPLE ();
  const fullCol = (s) => s.head >= Math.floor (s0.h * 0.6) * 0.8;   // ≥ 80 % of the top band's rows in ONE column = a full-height line
  chk (fullCol (s2a) && fullCol (s2b) && s2a.domOn === 1 && s2b.domOp > 0.9 && Math.abs (s2b.headX - s2a.headX) >= 0.10,
       '2  SOUNDING ⇒ HEAD PRESENT AND MOVING', `head ${s2a.head} → ${s2b.head} px at x ${s2a.headX} → ${s2b.headX} of width; .samp-ph on ${s2b.domOn} opacity ${s2b.domOp}; host frames ${s2b.sent - s1.sent} in 1 s`);

  // 3 — the voice stops honestly (fixed C++: the amp env resets, the gather drops it, the list is [])
  await host (() => { window.__host.pre = null; window.__host.list.a = []; window.__host.notes = 0; });
  await sleep (500);   // 30 frames — the .samp-ph fade is .4 s
  const s3 = await SAMPLE ();
  chk (s3.head === 0 && s3.domOn === 0 && s3.domOp < 0.05, '3  STOP ⇒ GONE within 30 frames (the .4 s fade included)', `head ${s3.head} px, .samp-ph on ${s3.domOn} opacity ${s3.domOp}`);

  // 4 — THE SHIPPED FAILURE: a voice the C++ believes is still sounding (env parked in Release — SynthVoice.h:6304-6309)
  //     with a read head that never moves: the frame goes byte-identical, the editor idle-skips, only keepalives arrive.
  await host (() => { let p = 0.30; window.__host.notes = 1; window.__host.pre = () => { p = Math.min (0.50, p + 0.004); window.__host.list.a = [3, p]; }; });
  await sleep (1200);   // the head walks 0.30 → 0.50 and then STOPS — from here every frame is byte-identical
  const s4a = await SAMPLE ();
  const f4a = s4a.frames, sent4a = s4a.sent;
  await sleep (2000);
  const s4b = await SAMPLE ();
  const stalled = (s4b.sent === sent4a) && (s4b.keep > s4a.keep);
  chk (stalled && s4b.head === 0 && s4b.domOn === 0 && s4b.domOp < 0.05 && s4b.pic >= s0.pic * 0.95,
       '4  A STALLED FEED ⇒ GONE, PICTURE STAYS — the head is cleared within 1.5 s of the last push; the spectrogram is intact',
       `feed: ${s4b.sent - sent4a} frames, ${s4b.keep - s4a.keep} keepalives in 2 s (stalled=${stalled}); head ${s4a.head} → ${s4b.head} px at x ${s4a.headX}; .samp-ph on ${s4b.domOn} opacity ${s4b.domOp}; picture ${s4b.pic} px (was ${s0.pic})`);

  // 5 — the feed returns: a new note draws its head again
  await host (() => { let p = 0.60; window.__host.pre = () => { p = Math.min (0.90, p + 0.004); window.__host.list.a = [5, p]; }; });
  await sleep (170);   // 10 frames
  const s5 = await SAMPLE ();
  chk (fullCol (s5) && s5.domOn === 1, '5  THE FEED RETURNS ⇒ THE HEAD RETURNS within 10 frames', `head ${s5.head} px at x ${s5.headX}; .samp-ph on ${s5.domOn}`);

  // 4b — the same stall with rAF DEAD (fb577's trap: WebKit suspends requestAnimationFrame on a hidden/occluded
  //      page, and the Resynth canvas's own draw loop rides rAF) — the clear must reach the canvas DIRECTLY.
  await host (() => { let p = 0.10; window.__host.pre = () => { p = Math.min (0.30, p + 0.004); window.__host.list.a = [7, p]; }; });
  await sleep (1200);   // the head walks 0.10 → 0.30 and stops
  await host (() => { window.__rafReal = window.requestAnimationFrame; window.requestAnimationFrame = function () { return 0; }; });
  const s4c = await SAMPLE ();
  await sleep (2000);
  const s4d = await SAMPLE ();
  await host (() => { window.requestAnimationFrame = window.__rafReal; });
  chk (s4d.sent === s4c.sent && s4d.head === 0 && s4d.domOn === 0 && s4d.pic >= s0.pic * 0.95,
       '4b THE SAME STALL WITH rAF DEAD (fb577) — the clear paints the canvas directly; the picture stays',
       `feed: ${s4d.sent - s4c.sent} frames in 2 s; head ${s4c.head} → ${s4d.head} px at x ${s4c.headX}; .samp-ph on ${s4d.domOn}; picture ${s4d.pic} px (was ${s0.pic})`);
  await sleep (200);

  // 6 — the clear cost no painter frame: during the stalls the frame clock stayed parked
  chk (s4b.frames - f4a === 0 && s4d.frames - s4c.frames === 0, '6  ZERO FRAMES AT REST — the clears dispatched no painter frame during the two 2 s stalls', `painter frames ${s4b.frames - f4a} / ${s4d.frames - s4c.frames}`);

  if (errs.length) console.log ('\n  page errors: ' + errs.join (' | '));
  console.log (`\n  ${fail ? '❌' : '✅'} ${pass} passed, ${fail} failed\n`);
  await b.close ();
  process.exit (fail ? 1 : 0);
}) ();
