// tpfx — TERRAIN FX'S PAGE, headless (?fx=1, a stubbed backend holding the FX build's DEFAULTS: Reverb in the chain,
//   powered, fed by the Audio In). The same index.html the synth ships; every FX branch keys on window.__TFX.
//   [1] it boots ON the Patcher, and the synth page cannot be reached (the SYN pill is the FX pill; CHOP / DLY / EQ hidden)
//   [2] the canvas: Audio In + the Reverb + Out, cabled Audio In → Reverb → Out; no MIDI In, oscillator, noise, filter,
//       voice or sampler B–D
//   [3] the add browser (right-click on blank canvas): shelves All · Flow · Tape · Effects; Glitch + Shaper + effects,
//       nothing that sounds on its own, nothing that plays notes
//   [4] the dice rolls effects + modulation only: every card it builds is fed by the Audio In (_CHOPS), no oscillator /
//       filter / noise / voice parameter written, no mod route aimed at one
//   [5] Settings: no MIDI & Controllers page, no voice ceiling, no factory library rows
//   [6] MOD opens and the FX pill comes back to the Patcher
//   [7] no page errors
//        node Tests/_tpfx_page_gate.js [path/to/index.html]
const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const SRC = process.argv[2] || (process.cwd() + '/Source/ui/public/index.html');
let pass = 0, fail = 0; const ok = (c, label, detail) => { if (c) { pass++; console.log('  PASS  ' + label); } else { fail++; console.log('  FAIL  ' + label); } if (detail) console.log('          ' + String(detail).slice(0, 900)); };
const stub = () => {
  // the FX build's defaults (normalised): the Reverb is in the chain, on, and claims chop layer A = the Audio In
  const P = window.__params = { SYN_RVB_ACTIVE: 1, SYN_RVB_POWER: 1, SYN_RVB_CHOPS: 1 / 15, SYN_RVB_RANK: 0.1 };
  window.__writes = []; window.__modJson = [];
  const states = new Map();
  const mk = (name) => { const props = { start: 0, end: 1, skew: 1, name, label: '', numSteps: 100, interval: 0, parameterIndex: states.size };
    const st = { name, get scaledValue(){ return P[name] != null ? P[name] : 0; }, set scaledValue(v){ P[name] = v; }, properties: props, getScaledValue(){ return st.scaledValue; }, setScaledValue(v){ st.scaledValue = v; },
      getNormalisedValue(){ return st.scaledValue; }, setNormalisedValue(v){ st.scaledValue = v; (st.__ls || []).forEach(f => { try { f(); } catch (e) {} }); },
      valueChangedEvent: { addListener(f){ (st.__ls = st.__ls || []).push(f); return { remove(){} }; }, removeListener(){} },
      propertiesChangedEvent: { addListener(){ return { remove(){} }; }, removeListener(){} },
      getChoiceIndex: () => 0, setChoiceIndex(){}, getValue: () => !!P[name], setValue(v){ P[name] = v ? 1 : 0; }, sliderDragStarted(){}, sliderDragEnded(){} }; return st; };
  const get = (nm) => { if (!states.has(nm)) states.set(nm, mk(nm)); return states.get(nm); };
  const nativeFn = (n) => (...a) => new Promise((r) => {
    if (n === 'setSynParam') { P[String(a[0])] = +a[1]; window.__writes.push(String(a[0])); return r(0); }
    if (n === 'getSynParam') return r(P[String(a[0])] != null ? P[String(a[0])] : 0);
    if (n === 'getSynParams') return r(String(a[0] || '').split(',').map(id => (P[id.trim()] != null ? P[id.trim()] : 0)).join(','));
    if (n === 'setSynthModMatrix') { window.__modJson.push(String(a[0])); return r(0); }
    if (n === 'getLayerPeakLevels') return r([{ l: 0.4, r: 0.4 }, {}, {}, {}]);
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
  await p.evaluateOnNewDocument(() => { try { localStorage.removeItem('tpLayout'); } catch (e) {} });
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + SRC + '?fx=1', { waitUntil: 'load' }); await sleep(3500);
  try { await p.evaluate(() => { if (window.__fxrRestoreChain) window.__fxrRestoreChain(); }); } catch (e) {}
  await sleep(1500);
  try { await p.evaluate(() => { if (window.__tpSync) window.__tpSync(); }); } catch (e) {}
  await sleep(800);

  // [1] boot + lock
  const boot = await p.evaluate(() => {
    const vis = (id) => { const e = document.getElementById(id); return !!e && getComputedStyle(e).display !== 'none'; };
    const r = { tfx: document.documentElement.classList.contains('tfx'), tpOpen: document.body.classList.contains('tp-open'), brand: (document.querySelector('#header .brand-name') || {}).textContent,
      pill: (document.getElementById('syn-btn') || {}).textContent, chop: vis('mix-btn'), dly: vis('delay-btn'), eq: vis('eq-btn') };
    document.getElementById('syn-btn').click(); r.afterPill = document.body.classList.contains('tp-open');
    try { setActivePanel('syn'); } catch (e) {} r.afterSyn = document.body.classList.contains('tp-open'); return r; });
  ok(boot.tfx && boot.tpOpen && boot.afterPill && boot.afterSyn && boot.pill === 'FX' && boot.brand === 'Terrain FX' && !boot.chop && !boot.dly && !boot.eq,
     '[1] boots on the Patcher and stays there (FX pill, brand "Terrain FX", no CHOP / DLY / EQ, the synth page maps back to the Patcher)', JSON.stringify(boot));

  // [2] nodes + cables
  const canvas = await p.evaluate(() => { const keys = window.__tpNodes().map(n => n.key); const cabs = window.__tpCables();
    const nm = (document.querySelector('#tp-page .tp-samp[data-samp="a"] .tp-samp-n') || {}).textContent;
    return { keys, cabs, nm }; });
  const bad = canvas.keys.filter(k => /^(midi|osc-|noise|filter|voice|samp-[bcd])/.test(k));
  const inToRvb = canvas.cabs.some(c => /^samp-a\.out0>fx-reverb-1\.in0$/.test(c)), rvbToOut = canvas.cabs.some(c => /^fx-reverb-1\.out0>(out|tape|tapeloop)\.in0$/.test(c));
  ok(canvas.keys.includes('samp-a') && canvas.keys.includes('out') && canvas.keys.includes('fx-reverb-1') && !bad.length && inToRvb && rvbToOut && canvas.nm === 'Audio In',
     '[2] the default patch on the canvas: Audio In → Reverb → Out, and nothing of the synth', JSON.stringify({ keys: canvas.keys, bad, cables: canvas.cabs, name: canvas.nm }));

  // [3] the add browser
  const add = await p.evaluate(async () => {
    const page = document.getElementById('tp-page'), r = page.getBoundingClientRect();
    const x = r.right - 40, y = r.top + 60;
    page.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: x, clientY: y, button: 2 }));
    await new Promise(z => setTimeout(z, 200));
    const m = page.querySelector('.tp-add'); if (!m) return { none: true };
    const cats = [...m.querySelectorAll('.cats .row')].map(e => e.textContent.trim());
    const items = [...m.querySelectorAll('.items .row')].map(e => e.textContent.trim());
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    return { cats, items }; });
  const addBad = (add.items || []).filter(t => /Oscillator|Sampler|Noise|Voices|Arp|Robin|Drift/.test(t));
  ok(add.cats && add.cats.join(',') === 'All,Flow,Tape,Effects' && add.items.some(t => /Glitch/.test(t)) && add.items.some(t => /Shaper/.test(t)) && add.items.some(t => /Reverb/.test(t)) && !addBad.length,
     '[3] the add browser: All · Flow · Tape · Effects — Glitch, Shaper, the effects; no sound sources, no note cards', JSON.stringify({ cats: add.cats, bad: addBad, n: (add.items || []).length, items: add.items }));

  // [4] the dice
  await p.evaluate(() => { window.__writes = []; window.__modJson = []; document.getElementById('dice-btn').click(); });
  await sleep(2600);
  const dice = await p.evaluate(() => {
    const w = [...new Set(window.__writes)];
    const synthW = w.filter(id => /^SYN_OSC_|^SYN_FILTER|^SYN_NOISE|^SYN_SUB|^SYN_VOICE|^SYN_UNI|^SYN_GLIDE/.test(id));
    const devs = (window.__fxrDevs ? window.__fxrDevs() : []).filter(Boolean);
    const chops = devs.map(d => d.core + ':' + Math.round((window.__params[String(d.tapsP).replace(/_TAPS$/, '_CHOPS')] || 0) * 15));
    const routes = window.__tiRoutes ? window.__tiRoutes() : [];
    const fxDests = new Set(); (window.__fxrDevs ? window.__fxrDevs() : []).filter(Boolean).forEach(d => { for (let k = 0; k < 12; k++) { const x = window.__fxModDest ? window.__fxModDest(d.core, d.inst, k) : null; if (x != null) fxDests.add(x); } });
    const modulatorDest = (d) => [1887, 1888, 1889].includes(d) || (d >= 12 && d < 22) || (d >= 673 && d < 693) || (d >= 481 && d < 511) || (d >= (window.__MACRO_DEST || 1878) && d < (window.__MACRO_DEST || 1878) + 9);
    const oscDests = routes.filter(r => !fxDests.has(r.d) && !modulatorDest(r.d));
    const srcBad = routes.filter(r => /^(vel|key|whl|at|bend)$/.test(r.s));
    return { nWrites: w.length, synthW, devs: devs.map(d => d.core), chops, nRoutes: routes.length, oscDests, srcBad }; });
  ok(dice.devs.length >= 2 && dice.nRoutes > 0 && dice.chops.every(s => /:1$/.test(s)) && !dice.synthW.length && !dice.oscDests.length && !dice.srcBad.length,
     '[4] the dice: a new chain fed by the Audio In, new modulation — no oscillator / filter / noise / voice write, every route on an effect card or a modulator, no note source',
     JSON.stringify(dice));

  // [5] settings
  const st = await p.evaluate(async () => { try { if (window.__st && window.__st.open) window.__st.open(true); } catch (e) {}
    await new Promise(z => setTimeout(z, 400));
    const ov = document.getElementById('st-overlay'); const txt = ov ? ov.innerText : '';
    const r = { open: !!ov && ov.classList.contains('on'), midiPage: /MIDI & Controllers/.test(txt), voice: /Voice ceiling/.test(txt), factory: /Factory library/.test(txt), perf: /Performance/.test(txt), iface: /Interface/.test(txt) };
    try { window.__st.open(false); } catch (e) {} return r; });
  ok(st.open && !st.midiPage && !st.voice && !st.factory && st.perf && st.iface, '[5] Settings: no MIDI & Controllers, no voice ceiling, no factory library; Performance and Interface stay', JSON.stringify(st));

  // [6] MOD and back
  const nav = await p.evaluate(async () => { document.getElementById('mod-btn').click(); await new Promise(z => setTimeout(z, 300));
    const onMod = document.getElementById('mod-panel').classList.contains('open') && !document.body.classList.contains('tp-open');
    document.getElementById('syn-btn').click(); await new Promise(z => setTimeout(z, 600));
    return { onMod, back: document.body.classList.contains('tp-open'), audioIn: !!(window.__tpNodeByKey && window.__tpNodeByKey('samp-a')) }; });
  ok(nav.onMod && nav.back && nav.audioIn, '[6] MOD opens; the FX pill brings the Patcher back with the Audio In', JSON.stringify(nav));

  ok(errs.length === 0, '[7] no page errors', errs.join(' | '));
  await b.close(); console.log('\n' + pass + ' passed, ' + fail + ' failed'); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL', e); process.exit(1); });
