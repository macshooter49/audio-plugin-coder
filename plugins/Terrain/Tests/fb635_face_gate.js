// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb635_face_gate.js — fb635: WHATEVER WAS HIDDEN AT LOAD TIME PAINTS WHEN IT IS SHOWN.
//
//    node Tests/fb635_face_gate.js [page.html]     # from plugins/Terrain (NODE_PATH=Tests/node_modules)
//
//  The B/D skeptic's law, measured (fb635 ver3): a preset loaded while a face has no box — the back side of
//  a slot, or the whole synth page (a new instance lands on the front page) — must draw the moment it is
//  shown. Two painters: the sample family's canvas (fb635's ResizeObserver) and the 3D WAVETABLE WATERFALL,
//  whose rAF loop parks when nothing visible draws and never re-armed on a reveal (drawOsc returned early).
//
//  THE BARS (a NOTIFYING stub; the C++ afterPatchLoad order; the boot PERSIST-RESYNC burst outlasted first)
//   1  THE WATERFALL ON A HIDDEN B DRAWS ON REVEAL — 3D only on B, reveal by B's letter: ≥ 15 inked rows
//   2  THE WATERFALL AFTER A HIDDEN SYNTH PAGE — page hidden at load, then opened: osc A's waterfall ≥ 15 rows
//      (row EXTENT, not raw ink: a stale 2D flatline has plenty of ink and 7 rows)
//   3  THE ONE-SHOT AFTER A HIDDEN SYNTH PAGE — the same load with a one-shot on A: A's canvas carries ink
//
//  MUTATION CONTROLS (source cuts)
//    FG_MUT=nokick   drawOsc's waterfall kick AND __oscFaceHeal's kick removed   → [1] [2] RED
//    FG_MUT=noro     the sample canvas ResizeObserver removed                    → [3] RED
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const fs = require ('fs'), os = require ('os');
const puppeteer = require (path.join (__dirname, 'node_modules', 'puppeteer-core'));
const PAGE0 = path.resolve (process.argv[2] || path.join (__dirname, '..', 'Source', 'ui', 'public', 'index.html'));
const MUT = process.env.FG_MUT || '';
let pass = 0, fail = 0;
const gate = (key, ok, name, detail) => { ok ? ++pass : ++fail; console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };
function page () {
  if (! MUT) return PAGE0;
  let s = fs.readFileSync (PAGE0, 'utf8');
  const sub = (a, b) => { const n = s.split (a).length - 1; if (n !== 1) { console.log ('  MUTATION anchor matched ' + n + 'x: ' + a.slice (0, 70)); process.exit (2); } s = s.replace (a, b); };
  const cut = (a, b) => { const i = s.indexOf (a), j = s.indexOf (b, i); if (i < 0 || j < 0 || s.indexOf (a, i + 1) >= 0) { console.log ('  MUTATION cut anchor missing: ' + a.slice (0, 70)); process.exit (2); } s = s.slice (0, i) + s.slice (j); };
  if (MUT === 'nokick') { sub ("if (wtWaterfall.on[o]) { wtWaterfall.kick(); return; }", "if (wtWaterfall.on[o]) return;");
                          sub ("var W = window.wtWaterfall; if (W && W.on && W.on[o] && W.kick) W.kick ();", ""); }
  if (MUT === 'noro') cut ("        /* fb635 — THE PICTURE HEALS ITSELF WHEN IT BECOMES VISIBLE.", "        SAMP[osc] = { st: st, data: data,");
  const f = path.join (os.tmpdir (), 'fb635_face_' + MUT + '.html'); fs.writeFileSync (f, s); return f;
}
const PAGE = page ();
const STUB = (MUT) => {
  window.__VALS = { SYN_OSC_A_ENGINE: 0, SYN_OSC_B_ENGINE: 0, SYN_OSC_C_ENGINE: 0, SYN_OSC_D_ENGINE: 0 };   // a fresh instance: 4 x Wavetable
  window.__PAY = {}; window.__VIEWS = {}; window.__WT = null; window.__calls = [];
  const states = {};
  const mk = (id) => states[id] || (states[id] = (function () {
    const L = [];
    const notify = () => L.slice().forEach (f => { try { f (); } catch (e) {} });
    return { __listeners: L, __notify: notify, get scaledValue () { return (window.__VALS[id] != null ? window.__VALS[id] : 0.5); },
      getScaledValue: () => (window.__VALS[id] != null ? window.__VALS[id] : 0.5),
      getNormalisedValue: () => (window.__VALS[id] != null ? window.__VALS[id] : 0.5),
      setScaledValue (v) { window.__VALS[id] = v; notify (); }, setNormalisedValue (v) { window.__VALS[id] = v; notify (); },
      getChoiceIndex: () => 0, setChoiceIndex () {}, getValue: () => false, setValue () {},
      valueChangedEvent: { addListener (f) { L.push (f); return { remove () { const i = L.indexOf (f); if (i >= 0) L.splice (i, 1); } }; },
                           removeListener (f) { const i = L.indexOf (f); if (i >= 0) L.splice (i, 1); } },
      propertiesChangedEvent: { addListener () { return { remove () {} }; }, removeListener () {} },
      properties: { start: 0, end: 1, interval: 0, name: '', label: '', numSteps: 100, choices: [], parameterIndex: 0 } };
  }) ());
  window.__states = states; window.__stateFor = mk;
  window.Juce = { getSliderState: mk, getToggleState: mk, getComboBoxState: mk,
    getNativeFunction: (n) => (...a) => new Promise (r => {
      window.__calls.push (n + '(' + a.map (String).join (',') + ')');
      if (n === 'getOscSamplePayload') { const o = String (a[0] || 'a'); return r (window.__PAY[o] ? JSON.stringify (window.__PAY[o]) : ''); }   // C++: cached JSON or ""
      if (n === 'getWaterfallView') return r (JSON.stringify (window.__VIEWS));
      if (n === 'getOscWavetable') return r (window.__WT ? JSON.stringify (window.__WT) : '');
      if (/getPresets/i.test (n)) return r ('[]');
      if (/Json|JSON/i.test (n)) return r ('');
      r (0); }),
    backend: { addEventListener () {}, removeEventListener () {}, emitEvent () {} } };
  (function () { const mine = window.Juce; let held = mine; Object.defineProperty (window, 'Juce', { configurable: true,
    get () { return held; }, set (v) { held = Object.assign ({}, v || {}, { getNativeFunction: mine.getNativeFunction,
      getSliderState: mine.getSliderState, getToggleState: mine.getToggleState, getComboBoxState: mine.getComboBoxState }); } }); }) ();
  window.__JUCE__ = { backend: window.Juce.backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '',
    __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  // in-page probes
  window.__ink = (cv) => { if (! cv || ! cv.width || ! cv.height) return 0; let d; try { d = cv.getContext ('2d').getImageData (0, 0, cv.width, cv.height).data; } catch (e) { return -1; }
    let n = 0; for (let i = 3; i < d.length; i += 4) if (d[i] > 16) n++; return n; };
  window.__probe = (o) => { const dev = document.getElementById ('osc-' + o + '-device'); const v = dev && dev.querySelector ('.sample-view');
    const cv = v && v.querySelector ('canvas.samp-wave'), sp = v && v.querySelector ('canvas.samp-spec'), wv = document.getElementById ('osc-wave-' + o);
    const r = v ? v.getBoundingClientRect () : { width: 0, height: 0 }; const nm = document.getElementById ('osc-' + o + '-sampname-display');
    return { shown: !! (dev && ! dev.classList.contains ('osc-hidden')), viewBox: Math.round (r.width) + 'x' + Math.round (r.height),
      loaded: !! (v && v.classList.contains ('loaded')), name: nm ? nm.textContent.trim () : null,
      cvPx: cv ? cv.width + 'x' + cv.height : '-', ink: window.__ink (cv), specInk: window.__ink (sp), waveInk: window.__ink (wv),
      eng: dev ? ['engine-sample', 'engine-granular', 'engine-geode', 'engine-modal', 'engine-fm', 'engine-harm'].filter (c => dev.classList.contains (c)).join (',') || 'wt' : '?' }; };
  // THE C++ afterPatchLoad, in order: relays move the knobs -> resyncAfterReattach pushes every cached
  // osc payload -> clears for the empty slots -> onPatchLoaded(meta)  (PluginEditor.cpp afterPatchLoad)
  window.__loadPreset = (fx) => {
    Object.keys (fx.vals).forEach (id => { window.__VALS[id] = fx.vals[id]; });
    Object.keys (window.__states).forEach (id => { if (id in fx.vals) try { window.__states[id].__notify (); } catch (e) {} });
    window.__PAY = fx.payloads; window.__VIEWS = fx.views || {};
    ['a', 'b', 'c', 'd'].forEach (o => { if (fx.payloads[o]) window.onOscSampleLoaded (o, fx.payloads[o]); });
    ['a', 'b', 'c', 'd'].forEach (o => { if (! fx.payloads[o]) window.onOscSampleCleared (o); });
    window.onPatchLoaded (JSON.stringify (fx.meta));
  };
};

const wait = (ms) => new Promise (r => setTimeout (r, ms));
async function boot (b, hide) {
  const p = await b.newPage ();
  await p.setViewport ({ width: 820, height: 656, deviceScaleFactor: 1 });
  const errs = []; p.on ('pageerror', e => errs.push (String (e.message || e).slice (0, 140)));
  await p.evaluateOnNewDocument (STUB, MUT);
  await p.goto ('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await wait (1500);
  if (! hide) await p.evaluate (() => { const sp = document.getElementById ('syn-panel'); if (sp) { sp.classList.remove ('hidden'); sp.style.display = 'block'; } window.dispatchEvent (new Event ('resize')); });
  await wait (9300);   // outlast the boot PERSIST-RESYNC burst (last shot 9000 ms) so the ONLY delivery is the preset-load push
  p.__errs = errs; return p;
}
// click the REAL letter box of the currently shown device in a slot (the user's click)
async function flip (p, fromOsc) {
  const sel = '#osc-' + fromOsc + '-device .osc-letter';
  try { await p.click (sel); } catch (e) { await p.evaluate (s => document.querySelector (s).click (), sel); }
  await wait (450);
}

(async () => {
  console.log ('══ fb635 FACE GATE — what was hidden at load paints when shown ══   mutation: ' + (MUT || '(none)'));
  const b = await puppeteer.launch ({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const n = 8, pp = 64, d = []; for (let k = 0; k < n; k++) for (let i = 0; i < pp; i++) d.push (Math.sin (2 * Math.PI * i / pp) * (0.4 + 0.07 * k));
  const TBL = { n, p: pp, d, sc: 1 };
  const rows = (p, o) => p.evaluate (o => { const cv = document.getElementById ('osc-wave-' + o); if (! cv || ! cv.width) return { rows: 0, ink: 0 };
      const x = cv.getContext ('2d').getImageData (0, 0, cv.width, cv.height).data; let r = 0, ink = 0;
      for (let y = 0; y < cv.height; y++) { let any = false; for (let xx = 0; xx < cv.width; xx++) { if (x[(y * cv.width + xx) * 4 + 3] > 16) { any = true; ink++; } } if (any) r++; }
      return { rows: r, ink }; }, o);
  const setPanel = (p, on) => p.evaluate (on => { const sp = document.getElementById ('syn-panel'); sp.style.display = ''; setActivePanel (on ? 'syn' : null); }, on);
  const RMIN = 15;
  // [1] 3D only on hidden B, revealed by B's letter
  { const p = await boot (b, false);
    await p.evaluate (t => { window.__WT = t; window.__VIEWS = { b: true }; window.onPatchLoaded (JSON.stringify ({ name: 'Reed', bank: 'User' })); }, TBL);
    await wait (1200); await flip (p, 'a'); await wait (300);
    const r = await rows (p, 'b');
    gate ('1', r.rows >= RMIN, '[1] THE WATERFALL ON A HIDDEN B DRAWS ON REVEAL — 3D only on B, B revealed by its letter', `rows ${r.rows} (want ≥ ${RMIN}) ink ${r.ink}`);
    await p.close (); }
  // [2] + [3] the synth page hidden at load, then opened
  { const p = await boot (b, false);
    await setPanel (p, false);
    const N = 400, mn = [], mx = []; for (let i = 0; i < N; i++) { const e = Math.exp (-i / 90) * (0.55 + 0.4 * Math.sin (i * 0.37)); mn.push (-Math.abs (e)); mx.push (Math.abs (e)); }
    await p.evaluate ((t, mn, mx) => {
      window.__WT = t; window.__VIEWS = { a: true };
      window.__loadPreset ({ vals: { SYN_OSC_C_ENGINE: 1 },   /* this stub serves the choice INDEX (Sample = 1) */ payloads: { c: { peaksMin: mn, peaksMax: mx, filename: 'Vbo - rhodes.wav', numSamples: 96000, sampleRate: 48000 } },
                            views: { a: true }, meta: { name: 'X', bank: 'User' } }); }, TBL, mn, mx);
    await wait (1200); await setPanel (p, true); await wait (700);
    const r = await rows (p, 'a'); const c = await p.evaluate (() => { const cv = document.querySelector ('#osc-c-device .sample-view canvas.samp-wave'); return cv ? window.__ink (cv) : -9; });
    gate ('2', r.rows >= RMIN, '[2] THE WATERFALL AFTER A HIDDEN SYNTH PAGE — the page hidden at load, then opened: osc A draws', `rows ${r.rows} (want ≥ ${RMIN}) ink ${r.ink}`);
    gate ('3', c > 200, '[3] THE ONE-SHOT AFTER A HIDDEN SYNTH PAGE — the same load put a one-shot on C: its canvas carries ink', `ink ${c}`);
    if (p.__errs.length) console.log ('  page errors: ' + p.__errs.slice (0, 2).join (' | '));
    await p.close (); }
  await b.close ();
  console.log (`\n  ${pass} passed, ${fail} FAILED`); process.exit (fail ? 1 : 0);
}) ().catch (e => { console.log ('  CRASH ' + (e && e.stack || e)); process.exit (2); });
