// tp43 — E-H PARITY AUDIT (page 5, stubbed backend). Max: "E F G H … the same control, the same wavetable, same
//   everything I should be able to click on the arrow of my wavetable". Drives oscillator B and oscillator E through the
//   same actions and prints what each one WROTE, so the missing wiring shows as a diff.   node Tests/_tp43_eh_audit.js
const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const SRC = process.argv[2] || (process.cwd() + '/Source/ui/public/index.html');
let pass = 0, fail = 0; const ok = (c, label, detail) => { if (c) { pass++; console.log('  PASS  ' + label); } else { fail++; console.log('  FAIL  ' + label + (detail ? '\n          ' + detail : '')); } };
const stub = () => {
  const states = new Map();
  const mk = (name) => { const props = { start: 0, end: 1, skew: 1, name, label: '', numSteps: 100, interval: 0, parameterIndex: states.size };
    const st = { name, scaledValue: 0, properties: props, getScaledValue: () => st.scaledValue, setScaledValue(v){ st.scaledValue = v; },
      getNormalisedValue(){ return st.scaledValue; }, setNormalisedValue(v){ st.scaledValue = v; (st.__ls || []).forEach(f => { try { f(); } catch (e) {} }); },
      valueChangedEvent: { addListener(f){ (st.__ls = st.__ls || []).push(f); return { remove(){} }; }, removeListener(){} },
      propertiesChangedEvent: { addListener(){ return { remove(){} }; }, removeListener(){} },
      getChoiceIndex: () => 0, setChoiceIndex(){}, getValue: () => false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} }; return st; };
  const get = (nm) => { if (!states.has(nm)) states.set(nm, mk(nm)); return states.get(nm); };
  window.__loads = []; window.__blends = []; window.__loaded = {}; window.__failPaths = {}; window.__natCount = {}; window.__params = { SYN_NOISE_OUT: 1, SYN_NOISE2_OUT: 1 };   /* the noise cables' shipped default: connected */
  const LIB = { path: '/lib', exists: true, total: 9, cats: { Drums: ['kick.wav', 'snare.wav', 'hat.wav', 'clap.wav'], '808': ['808a.wav', '808b.wav'], Keys: ['k1.wav', 'k2.wav'], Pad: ['p1.wav'] } };
  const nativeFn = (n) => (...a) => new Promise((r) => { window.__natCount[n] = (window.__natCount[n] || 0) + 1;
    if (n === 'setSynParam') { window.__params[String(a[0])] = +a[1]; try { get(String(a[0])).scaledValue = +a[1]; } catch (e) {} return r(0); }
    if (n === 'getSynParam') { const id = String(a[0]); return r(window.__params[id] != null ? window.__params[id] : get(id).scaledValue); }   /* tp42 — the strips read themselves back through this */
    if (n === 'getSynParams') { return r(String(a[0]).split(',').map(id => (window.__params[id] != null ? window.__params[id] : get(id).scaledValue)).join(',')); }   /* the pool relay's batched poll */
    if (n === 'scanSampleFactory') return r(JSON.stringify(LIB));
    if (n === 'loadSampleByPath') { window.__loads.push([String(a[0]), String(a[1])]); if (!window.__failPaths[String(a[1])]) window.__loaded[String(a[0])] = String(a[1]); return r('ok'); }
    if (n === 'blendOscSampleByPath') { window.__blends.push([String(a[0]), String(a[1])]); return r('ok'); }
    if (n === 'getOscSamplePayload') return r(window.__loaded[String(a[0])] ? '[0.1,0.2,0.3]' : '');
    if (n === 'getSynthLfoShapes') return r('{"shapes":[]}');
    if (/Json|JSON/.test(n)) return r('{}'); if (/^get|^list|^scan/.test(n)) return r('[]'); r(0); });
  window.Juce = { getSliderState: get, getToggleState: get, getComboBoxState: get, getNativeFunction: nativeFn, backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable: true, get(){ return held; }, set(v){ held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction, getSliderState: mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend: window.Juce.backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '', __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
};
(async () => {
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 1300, height: 900 }); await p.evaluateOnNewDocument(stub);
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + SRC + '?page=5', { waitUntil: 'load' }); await sleep(3000);
  await p.evaluate(() => { window.__tpAdd('osc:b', 100, 100); window.__tpAdd('osc:e', 100, 400); }); await sleep(2500);
  const audit = await p.evaluate(async () => {
    const sleep = ms => new Promise(r => setTimeout(r, ms));
    const writes = () => Object.keys(window.__params);
    const since = (before) => writes().filter(k => !before.includes(k));
    const md = (el, opts) => el && el.dispatchEvent(new MouseEvent('mousedown', Object.assign({ bubbles: true, cancelable: true, button: 0 }, opts || {})));
    const mu = (el, opts) => el && el.dispatchEvent(new MouseEvent('mouseup', Object.assign({ bubbles: true, cancelable: true, button: 0 }, opts || {})));
    const out = {};
    for (const o of ['b', 'e']) {
      const O = o.toUpperCase(); const dev = document.getElementById('osc-' + o + '-device'); const R = { present: !!dev };
      if (!dev) { out[o] = R; continue; }
      const onCanvas = !!dev.closest('#tp-page'); R.onCanvas = onCanvas;
      // 1. engine select
      { const sel = document.getElementById('osc-' + o + '-engine-select'); const before = writes(); if (sel) { sel.value = '1'; sel.dispatchEvent(new Event('change', { bubbles: true })); await sleep(300); } R.engineWrites = since(before); R.engineDisp = (document.getElementById('osc-' + o + '-engine-display') || {}).textContent; sel && (sel.value = '0', sel.dispatchEvent(new Event('change', { bubbles: true }))); await sleep(300); }
      // 2. wavetable arrows
      { const pd = document.getElementById('osc-' + o + '-preset-display'); const pw = pd ? pd.parentElement : null; const navs = pw ? [...pw.parentNode.querySelectorAll('.wt-nav')] : []; R.wtArrows = navs.length;
        const before = writes(); const calls = []; const orig = window.wtStepPreset; window.wtStepPreset = function (oo, d) { calls.push([oo, d]); return orig.apply(this, arguments); };
        if (navs[1]) md(navs[1]); await sleep(900); window.wtStepPreset = orig; R.wtStepCalls = calls; R.wtStepWrites = since(before); R.presetText = pd ? pd.textContent : null; }
      // 3. the wavetable glass menu
      { const pd = document.getElementById('osc-' + o + '-preset-display'); const pw = pd ? pd.parentElement : null; md(pw); await sleep(400); const menu = document.querySelector('.pmenu.open, .syn-ctx-menu.act, .tpb, .wt-glass.open, [class*="glass"][class*="open"]'); R.wtMenuOpen = !!menu; document.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true })); document.body.click(); await sleep(200); }
      // 4. swap
      { const btn = document.getElementById('osc-' + o + '-swap-btn'); const wasBack = dev.classList.contains('show-back') || dev.classList.contains('flipped'); btn && btn.click(); await sleep(250); R.swapFlips = (dev.classList.contains('show-back') || dev.classList.contains('flipped')) !== wasBack; R.backClasses = dev.className; btn && btn.click(); await sleep(200); }
      // 5. front knobs: which data-syn knobs exist, and do they write on a wheel
      { const knobs = [...dev.querySelectorAll('.knob[data-syn]')]; R.knobs = knobs.length; const before = writes(); knobs.slice(0, 6).forEach(k => k.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, deltaY: -120 }))); await sleep(300); R.knobWheelWrites = since(before).length; }
      // 6. back-panel selects
      { const ids = ['interp-mode', 'keytrack-dest', 'fold-shape', 'spectral-type', 'warp2-mode']; R.backSel = {}; for (const id of ids) { const el = document.getElementById('osc-' + o + '-' + id); if (!el) { R.backSel[id] = 'missing'; continue; } const before = writes(); try { if (el.tagName === 'SELECT') { el.selectedIndex = (el.selectedIndex + 1) % Math.max(1, el.options.length); el.dispatchEvent(new Event('change', { bubbles: true })); } else { md(el); mu(el); } } catch (e) {} await sleep(200); R.backSel[id] = since(before).length ? 'writes' : 'silent'; } }
      // 7. sample name click (sample engine)
      { const sn = document.getElementById('osc-' + o + '-sampname-display'); md(sn); mu(sn); await sleep(400); R.sampBrowser = !!document.querySelector('.tpb, .samp-browser.open, [class*="browser"][class*="open"]'); document.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true })); await sleep(150); }
      // 8. blend/unison/sub arrows present + wired (a click flips a page)
      { R.arrows = {}; for (const c of ['uni-arrow', 'bl-arrow', 'fm-arrow', 'harm-arrow', 'modal-arrow', 'geode-arrow', 'gk-arrow']) { const el = dev.querySelector('.' + c); if (!el) { R.arrows[c] = 'missing'; continue; } const cls = dev.className; el.click(); await sleep(150); R.arrows[c] = dev.className !== cls ? 'flips' : 'no-change'; el.click(); await sleep(100); } }
      out[o] = R;
    }
    return out;
  });
  console.log(JSON.stringify(audit, null, 1)); console.log('page errors:', errs.length, errs.slice(0, 5).join(' | '));
  await b.close();
})().catch(e => { console.log('FAIL', e); process.exit(1); });
