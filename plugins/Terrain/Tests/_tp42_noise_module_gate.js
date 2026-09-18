// tp42 — NOISE AS A MODULE, headless (page 5, stubbed backend). Max: "the noise engine as a duplicatable module, with
//   cables, in the Oscillators tab". [1] the add browser lists Noise and Noise 2 under Oscillators; [2] spawning Noise 2
//   switches SYN_NOISE2_ON on, lands a node with an audio out port, its clone strip carries bank-1 mod dests, a MIDI cable
//   reaches it; [3] Noise 2 → reverb lights the eleventh pill (route[10], SYN_RVB_SRC_N2) as a direct tap (bit 10) and the
//   canvas draws noise2 → reverb; [4] the rack card shows the N2 pill; [5] the strip's own power pill switches Noise 2 off and
//   the node leaves; [6] Noise (1) spawns and cables the same way.                node Tests/_tp42_noise_module_gate.js
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
  await p.evaluate(() => { window.__P = (id) => (window.__params[id] != null ? window.__params[id] : window.Juce.getSliderState(id).getNormalisedValue());
    window.__cables = () => window.__tpDerive().map(c => c.id); window.__tpAdd('osc:a', 100, 100); });
  await sleep(900);
  // [1] the browser
  const cat = await p.evaluate(() => { const c = window.__tpCatalog(); return { menu: true, cats: [...new Set(c.map(x => x.cat))], items: c.filter(x => x.cat === 'Oscillators').map(x => x.n) }; });
  ok(cat.menu && cat.items && cat.items.includes('Noise') && cat.items.includes('Noise 2'), '[1] the add browser lists Noise and Noise 2 under Oscillators', JSON.stringify(cat));
  // [2] spawn Noise 2
  await p.evaluate(() => { window.__tpAdd('noise:2', 300, 500); }); await sleep(1200);
  const sp = await p.evaluate(() => { const n = window.__tpNodeByKey('noise2'); if (!n) return { node: false }; const el = document.getElementById('n2-noise-mod'); const inNode = !!(el && n.body && n.body.contains(el));
    const outs = n.ports.filter(q => q.kind === 'out' && q.el && q.el.dataset.t === 'a').length; const ins = n.ports.filter(q => q.kind === 'in').length;
    const dests = el ? [...el.querySelectorAll('.noise-knobs .knob')].map(k => +k.getAttribute('data-mod-dest')) : []; const mods = n.mods ? n.mods.length : 0;
    return { node: true, on: window.__P('SYN_NOISE2_ON'), inNode, outs, ins, dests, mods, midi: window.__cables().includes('midi.out0>noise2.in0'), toOut: window.__cables().includes('noise2.out0>out.in0'), name: (window.__tpNodes().find(x => x.key === 'noise2') || {}).key }; });
  ok(sp.node && sp.on === 1 && sp.inNode && sp.outs === 1 && sp.ins === 1, '[2] Noise 2 spawns: SYN_NOISE2_ON = 1, the clone strip sits in the node, an audio out and a note in', JSON.stringify(sp));
  ok(sp.dests && sp.dests.length === 3 && sp.dests[0] === 1962 && sp.mods === 3, '[2] its three knobs are bank-1 mod dests (1962..1964) with mod ports', JSON.stringify({ dests: sp.dests, mods: sp.mods }));
  ok(sp.midi && sp.toOut, '[2] the canvas draws MIDI → Noise 2 → Out', JSON.stringify({ midi: sp.midi, toOut: sp.toOut }));
  // [3] Noise 2 → reverb
  await p.evaluate(() => { try { window.__fxrAdd('reverb'); } catch (e) {} }); await sleep(1500);
  const cab = await p.evaluate(() => { const nb = window.__tpNodeByKey, n2 = nb('noise2'), fx = nb('fx-reverb-1'); if (!n2 || !fx) return { have: false };
    const oi = Math.max(0, n2.ports.filter(q => q.kind === 'out').findIndex(q => q.el && q.el.dataset.t === 'a')); const okC = window.__tpConnect(n2, 'out', oi, fx, 'in', 0); const d = window.__fxrDevs()[0];
    return { have: true, okC, route10: d.route[10], rp10: d.rp[10], srcN2: window.__P('SYN_RVB_SRC_N2'), taps: d.taps, cable: window.__cables().includes('noise2.out0>fx-reverb-1.in0'), toOut: window.__cables().includes('noise2.out0>out.in0') }; });
  ok(cab.have && cab.okC === true && cab.route10 === 1 && cab.rp10 === 'SYN_RVB_SRC_N2' && cab.srcN2 === 1, '[3] Noise 2 → reverb lights the eleventh pill (SYN_RVB_SRC_N2 = 1)', JSON.stringify(cab));
  ok(cab.have && ((cab.taps >> 10) & 1) === 1, '[3] … as a DIRECT tap (bit 10 of SYN_RVB_TAPS)', JSON.stringify({ taps: cab.taps }));
  ok(cab.cable && !cab.toOut, '[3] the canvas draws Noise 2 → reverb and no longer Noise 2 → Out', JSON.stringify({ cable: cab.cable, toOut: cab.toOut }));
  // [4] the rack card's pill
  const pill = await p.evaluate(() => { const card = document.querySelector('#tp-page .fxr-dev[data-dev="0"]'); const r = card ? card.querySelector('.fxr-r[data-r="10"]') : null; return { have: !!card, pill: !!r, lit: !!(r && r.classList.contains('fxr-on')), label: r ? r.textContent.trim() : null }; });
  ok(pill.have && pill.pill && pill.lit && pill.label === 'N2', '[4] the reverb card carries an N2 pill and it is lit', JSON.stringify(pill));
  // [5] the strip's power pill
  const pw = await p.evaluate(() => { const el = document.getElementById('n2-noise-pow'); if (!el) return { have: false }; const prev = document.getElementById('n2-noise-prev'); const before = document.getElementById('n2-noise-mod').classList.contains('noise-off'); el.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, button: 0 })); return { have: true, on: window.__P('SYN_NOISE2_ON'), wired: !!(prev && prev._nw), offBefore: before, offAfter: document.getElementById('n2-noise-mod').classList.contains('noise-off') }; });
  await sleep(1200);
  const gone = await p.evaluate(() => ({ node: !!window.__tpNodeByKey('noise2'), home: !!document.querySelector('#noise2-home #n2-noise-mod') }));
  ok(pw.have && pw.on === 0 && !gone.node && gone.home, '[5] the strip\'s own power pill switches Noise 2 off, the node leaves and the strip goes home', JSON.stringify({ pw, gone }));
  // [6] Noise (1)
  await p.evaluate(() => { window.__tpAdd('noise:1', 300, 700); }); await sleep(1200);
  const n1 = await p.evaluate(() => { const nb = window.__tpNodeByKey, n = nb('noise'), fx = nb('fx-reverb-1'); if (!n || !fx) return { have: false }; const oi = Math.max(0, n.ports.filter(q => q.kind === 'out').findIndex(q => q.el && q.el.dataset.t === 'a'));
    const okC = window.__tpConnect(n, 'out', oi, fx, 'in', 0); const d = window.__fxrDevs()[0]; return { have: true, on: window.__P('SYN_NOISE_ON'), okC, route5: d.route[5], srcN: window.__P('SYN_RVB_SRC_NOISE'), cable: window.__cables().includes('noise.out0>fx-reverb-1.in0') }; });
  ok(n1.have && n1.on === 1 && n1.okC === true && n1.route5 === 1 && n1.srcN === 1 && n1.cable, '[6] Noise (1) spawns and cables into the reverb (route[5], SYN_RVB_SRC_NOISE)', JSON.stringify(n1));
  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL', e); process.exit(1); });
