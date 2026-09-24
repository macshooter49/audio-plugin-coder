// ══════════════════════════════════════════════════════════════════════════════════════════════
//  _org_harness.js — the shared headless rig for the Organics gates (tp104). Not a gate itself.
//
//  The stub is _ui_lockin_sim.js's (the page's own bridge surface), with the two things a CHOICE
//  parameter needs to behave like the real backend after Agent C's merge:
//    · SYN_OSC_x_ENGINE reports 12 choices (relay properties: interval 1, numSteps 12, parameterIndex
//      ≥ 0) and getScaledValue() is the INDEX, exactly as a JUCE relay answers;
//    · the getParamCardinality native answers 12 for the engine, so the E–H pool states agree.
//  The Organics natives are ABSENT (__juce__functions is empty), so the page runs on its own built-in
//  mock (window.__orgMock) — the path it takes until the C++ lands. A gate may seed the mock's
//  per-oscillator state with window.__orgMockSeed = { 0: { id, artic }, … } before load.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require('path');
const puppeteer = require('puppeteer-core');
const CHROME = process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const PAGE_DEFAULT = path.resolve(__dirname, '../Source/ui/public/index.html');
const sleep = ms => new Promise(r => setTimeout(r, ms));

const stub = () => {
  const states = new Map();
  const CARD = (nm) => (/^SYN_OSC_[A-H]_ENGINE$/.test(nm) ? 12 : 0);
  const mk = (name) => {
    const card = CARD(name);
    const props = { start: 0, end: card ? card - 1 : 1, skew: 1, name, label: '', numSteps: card || 100, interval: card ? 1 : 0, parameterIndex: states.size };
    const st = { name, norm: 0, properties: props,
      getScaledValue: () => card ? Math.round(st.norm * (card - 1)) : st.norm,
      setScaledValue(v){ st.norm = card ? v / (card - 1) : v; st.__fire(); },
      getNormalisedValue(){ return st.norm; },
      setNormalisedValue(v){ st.norm = Math.max(0, Math.min(1, +v || 0)); window.__params[name] = st.norm; st.__fire(); },
      __fire(){ (st.__ls || []).slice().forEach(f => { try { f(); } catch (e) {} }); },
      valueChangedEvent: { addListener(f){ (st.__ls = st.__ls || []).push(f); return { remove(){} }; }, removeListener(){} },
      propertiesChangedEvent: { addListener(){ return { remove(){} }; }, removeListener(){} },
      getChoiceIndex: () => 0, setChoiceIndex(){}, getValue: () => false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} };
    return st; };
  const get = (nm) => { if (!states.has(nm)) states.set(nm, mk(nm)); return states.get(nm); };
  window.__stubState = get;
  window.__natCount = {}; window.__params = { SYN_NOISE_OUT: 1, SYN_NOISE2_OUT: 1 }; window.__natLog = [];
  const LIB = { path: '/lib', exists: true, total: 9, cats: { Drums: ['kick.wav', 'snare.wav'], Keys: ['k1.wav', 'k2.wav'], Pad: ['p1.wav'] } };
  const nativeFn = (n) => (...a) => new Promise((r) => { window.__natCount[n] = (window.__natCount[n] || 0) + 1;
    if (n === 'setSynParam') { const id = String(a[0]); window.__params[id] = +a[1]; window.__natLog.push([id, +a[1]]); try { get(id).norm = +a[1]; } catch (e) {} return r(0); }
    if (n === 'getSynParam') { const id = String(a[0]); return r(window.__params[id] != null ? window.__params[id] : get(id).norm); }
    if (n === 'getSynParams') { return r(String(a[0]).split(',').map(id => (window.__params[id] != null ? window.__params[id] : get(id).norm)).join(',')); }
    if (n === 'getParamCardinality') return r(CARD(String(a[0])));
    if (n === 'scanSampleFactory') return r(JSON.stringify(LIB));
    if (n === 'getSynthLfoShapes') return r('{"shapes":[]}');
    if (/Json|JSON/.test(n)) return r('{}'); if (/^get|^list|^scan/.test(n)) return r('[]'); r(0); });
  window.Juce = { getSliderState: get, getToggleState: get, getComboBoxState: get, getNativeFunction: nativeFn, backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable: true, get(){ return held; }, set(v){ held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction, getSliderState: mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend: window.Juce.backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '', __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
};

async function launch(opt) {
  opt = opt || {};
  const b = await puppeteer.launch({ executablePath: CHROME, headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport({ width: opt.width || 1200, height: opt.height || 820, deviceScaleFactor: opt.dpr || 1 });
  await p.evaluateOnNewDocument(stub);
  if (opt.seed) await p.evaluateOnNewDocument((s) => { window.__orgMockSeed = s; }, opt.seed);
  if (opt.pre) await p.evaluateOnNewDocument(opt.pre);
  const errs = []; p.on('pageerror', e => errs.push(String(e.message || e).slice(0, 200) + ' @ ' + String((e.stack || '').split('\n')[1] || '').trim().slice(0, 120)));
  await p.goto('file://' + (opt.page || PAGE_DEFAULT) + (opt.query || ''), { waitUntil: 'load', timeout: 60000 });
  await sleep(opt.settle || 2200);
  if (opt.theme !== 'light') await p.evaluate(() => { try { if (typeof setTheme === 'function') setTheme('dark'); else document.documentElement.dataset.theme = 'dark'; } catch (e) {} });
  if (!opt.noShow) await p.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) { sp.classList.remove('hidden'); sp.style.display = 'block'; } window.dispatchEvent(new Event('resize')); });
  await sleep(300);
  return { b, p, errs };
}
// switch an oscillator's engine the way a user does: the header <select>, then its change event
async function setEngine(p, o, idx) {
  return p.evaluate((o, idx) => { const sel = document.getElementById('osc-' + o + '-engine-select'); if (!sel) return 'no select';
    sel.value = String(idx); sel.dispatchEvent(new Event('change', { bubbles: true }));
    const d = document.getElementById('osc-' + o + '-device'); return d ? d.className : 'no device'; }, o, idx);
}
// turn an oscillator ON (its ENABLE param) — an off osc is drawn at reduced opacity
async function enable(p, o, on) {
  return p.evaluate((o, on) => { const st = window.Juce.getSliderState('SYN_OSC_' + o.toUpperCase() + '_ENABLE'); if (st) st.setNormalisedValue(on === false ? 0 : 1);
    const d = document.getElementById('osc-' + o + '-device'); return d ? !d.classList.contains('osc-off') : null; }, o, on);
}
module.exports = { stub, launch, setEngine, enable, sleep, PAGE_DEFAULT };
