// tp40 — PATCHER PARITY, headless (page 5, stubbed backend). Max: "test every single thing on the synth page and anything in
//   the patcher that isn't alike needs to be fixed": [1] E-H's + flips their back panel (they are clones of B and had no
//   wiring);  [2] an effect adopted onto the canvas still flips its back panel and changes type (the rack's handlers now
//   also listen on the canvas; devFor by data-dev);  [3] a cable from osc E into an effect lights the E pill (it said 'That
//   does not fit');  [4] the wheel over a knob is the knob's, over blank canvas it pans.      node Tests/_tp40_patcher_parity_gate.js
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
  window.__loads = []; window.__blends = []; window.__loaded = {}; window.__failPaths = {}; window.__natCount = {}; window.__params = {};
  const LIB = { path: '/lib', exists: true, total: 9, cats: { Drums: ['kick.wav', 'snare.wav', 'hat.wav', 'clap.wav'], '808': ['808a.wav', '808b.wav'], Keys: ['k1.wav', 'k2.wav'], Pad: ['p1.wav'] } };
  const nativeFn = (n) => (...a) => new Promise((r) => { window.__natCount[n] = (window.__natCount[n] || 0) + 1;
    if (n === 'setSynParam') { window.__params[String(a[0])] = +a[1]; try { get(String(a[0])).scaledValue = +a[1]; } catch (e) {} return r(0); }
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
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 140)));
  await p.goto('file://' + SRC + '?page=5', { waitUntil: 'load' }); await sleep(3000);
  await p.evaluate(() => { window.__P = (id) => (window.__params[id] != null ? window.__params[id] : window.Juce.getSliderState(id).getNormalisedValue()); try { window.__fxrAdd('reverb'); } catch (e) {} 'efgh'.split('').forEach(o => { try { window.Juce.getSliderState('SYN_OSC_' + o.toUpperCase() + '_ENABLE').setNormalisedValue(1); } catch (e) {} }); });
  await sleep(2500);
  // [1] E-H back panels
  const swaps = await p.evaluate(() => { const out = {}; 'efgh'.split('').forEach(o => { const d = document.getElementById('osc-' + o + '-device'), btn = document.getElementById('osc-' + o + '-swap-btn'); if (!d || !btn) { out[o] = 'missing'; return; } const was = d.classList.contains('swapped'); btn.click(); out[o] = d.classList.contains('swapped') !== was ? 'flips' : 'DEAD'; btn.click(); }); return out; });
  ok(['e', 'f', 'g', 'h'].every(o => swaps[o] === 'flips'), '[1] E, F, G, H: + flips the back panel', JSON.stringify(swaps));
  // [2] an effect on the canvas
  const fx = await p.evaluate(() => { const nodes = window.__tpLayout().nodes; const d = document.querySelector('#tp-page .fxr-dev'); if (!d) return { nodes, onCanvas: false }; const sw = d.querySelector('[data-act="swap"]'); const was = d.classList.contains('swapped'); sw.click(); const flips = d.classList.contains('swapped') !== was; sw.click();
    const sel = d.querySelector('select.fxr-type-native'); let typed = null; if (sel && sel.options.length > 1) { const before = window.__fxrDevs()[+d.dataset.dev].type; sel.selectedIndex = (sel.selectedIndex + 1) % sel.options.length; sel.dispatchEvent(new Event('change', { bubbles: true })); typed = window.__fxrDevs()[+d.dataset.dev].type !== before; }
    return { nodes, onCanvas: true, flips, typed }; });
  ok(fx.onCanvas && fx.flips && fx.typed, '[2] a reverb adopted onto the canvas: + flips its back panel, the type dropdown changes it', JSON.stringify(fx));
  // [3] cable E → reverb
  const cable = await p.evaluate(() => { const nb = window.__tpNodeByKey, e = nb('osc-e'), fxn = nb('fx-reverb-1'); if (!e || !fxn) return { have: { e: !!e, fx: !!fxn } };
    const outs = e.ports.filter(q => q.kind === 'out'); let oi = outs.findIndex(q => q.el && q.el.dataset.t === 'a'); if (oi < 0) oi = 0; const ins = (fxn.ports || []).filter(q => q.kind === 'in');
    const okE = window.__tpConnect(e, 'out', oi, fxn, 'in', 0); const route = (window.__fxrDevs()[0] || {}).route; return { have: { e: true, fx: true }, okE, route, srcE: window.__P('SYN_RVB_SRC_E') }; });
  ok(cable.okE === true && cable.route && cable.route[6] === 1 && cable.srcE === 1, '[3] a cable from osc E into the reverb lights the E pill (route[6], SYN_RVB_SRC_E = 1)', JSON.stringify(cable));
  // [4] the wheel
  const wheel = await p.evaluate(() => { const w = document.querySelector('#tp-page .tp-world'); const T = () => w.style.transform; const knob = document.querySelector('#tp-page .tp-node .tp-body .knob'); const page = document.getElementById('tp-page'); const r = page.getBoundingClientRect();
    const at = (el, x, y) => el.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, deltaY: 120, clientX: x, clientY: y }));
    const t0 = T(); if (knob) { const kr = knob.getBoundingClientRect(); at(knob, kr.left + kr.width / 2, kr.top + kr.height / 2); } const knobPans = T() !== t0;
    const t1 = T(); at(page, r.left + 6, r.bottom - 6); const blankPans = T() !== t1; return { hasKnob: !!knob, knobPans, blankPans }; });
  ok(wheel.hasKnob && !wheel.knobPans && wheel.blankPans, '[4] the wheel over a knob is the knob\'s (no pan); over blank canvas it pans', JSON.stringify(wheel));
  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log('\n' + pass + ' passed, ' + fail + ' failed'); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL', e); process.exit(1); });
