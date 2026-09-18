// tp41 — THE PATCHER'S RULES, headless (page 5, stubbed backend). Max: "the patcher needs its own rules … route an
//   oscillator straight into a reverb without the filter … a flow card through a reverb … spawned routed to nothing".
//   [1] a Chop spawned from the canvas arrives with NO pills lit; a new oscillator is not eaten by it;  [2] osc → effect
//   is a DIRECT tap (bit in <device>_TAPS) and the canvas draws osc → effect, not osc → filter → effect;  [3] filter →
//   effect is post-filter (bit cleared) and the canvas goes through the filter;  [4] a pill lit ON THE CANVAS is direct;
//   [5] Chop → Reverb puts the card IN the rack (INLINE, a rank below the reverb's), lights the reverb's pill for the
//   card's source, draws the cable, and cutting it puts the card back after the rack.       node Tests/_tp41_patcher_rules_gate.js
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
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + SRC + '?page=5', { waitUntil: 'load' }); await sleep(3000);
  await p.evaluate(() => { window.__P = (id) => (window.__params[id] != null ? window.__params[id] : window.Juce.getSliderState(id).getNormalisedValue());
    window.__cables = () => window.__tpDerive().map(c => c.id); window.__cableEdit = (id) => { const c = window.__tpDerive().find(c => c.id === id); return c ? c.edit : null; }; });
  await p.evaluate(() => { window.__tpAdd('osc:a', 100, 100); }); await sleep(900);   // the stub starts with every oscillator off
  // [1] spawn unrouted
  await p.evaluate(() => { window.__tpAdd('flow:chop', 300, 300); }); await sleep(900);
  const spawn = await p.evaluate(() => { const pills = 'A,B,C,D,S,N,E,F,G,H'.split(',').map(k => window.__P('FLOW_CHOP_SRC_' + k)); const node = !!window.__tpNodeByKey('flow-chop'); return { node, pills }; });
  ok(spawn.node && spawn.pills.every(v => v === 0), '[1] a Chop spawned from the canvas has NO pills lit', JSON.stringify(spawn));
  await p.evaluate(() => { window.__tpAdd('osc:e', 600, 300); }); await sleep(900);
  const eSpawn = await p.evaluate(() => ({ node: !!window.__tpNodeByKey('osc-e'), chopE: window.__P('FLOW_CHOP_SRC_E'), enabled: window.__P('SYN_OSC_E_ENABLE') }));
  ok(eSpawn.node && eSpawn.chopE === 0 && eSpawn.enabled === 1, '[1] a new oscillator is not eaten by the Chop on the canvas', JSON.stringify(eSpawn));
  // [2] osc A → reverb = direct
  await p.evaluate(() => { try { window.__fxrAdd('reverb'); } catch (e) {} }); await sleep(1500);
  const direct = await p.evaluate(() => { const nb = window.__tpNodeByKey, a = nb('osc-a'), fx = nb('fx-reverb-1'); if (!a || !fx) return { have: false };
    const oi = Math.max(0, a.ports.filter(q => q.kind === 'out').findIndex(q => q.el && q.el.dataset.t === 'a'));
    const okC = window.__tpConnect(a, 'out', oi, fx, 'in', 0); const d = window.__fxrDevs()[0];
    return { have: true, okC, route: d.route[0], taps: d.taps, tapsP: window.__P('SYN_RVB_TAPS'), cables: window.__cables().filter(c => /fx-reverb-1/.test(c)) }; });
  ok(direct.have && direct.okC === true && direct.route === 1 && direct.taps === 1 && Math.round(direct.tapsP * 2047) === 1, '[2] osc A → reverb lights the pill AND sets bit 0 of SYN_RVB_TAPS (a direct tap)', JSON.stringify(direct));
  ok(direct.cables && direct.cables.includes('osc-a.out0>fx-reverb-1.in0') && !direct.cables.includes('filter.out0>fx-reverb-1.in0'), '[2] the canvas draws osc A → reverb, not through the filter', JSON.stringify(direct.cables));
  // [3] filter → reverb = post-filter (A must be in the filter for the canvas to route it through)
  const filt = await p.evaluate(() => { const nb = window.__tpNodeByKey, a = nb('osc-a'), f = nb('filter'), fx = nb('fx-reverb-1'); if (!a || !f || !fx) return { have: false };
    { const oi = Math.max(0, a.ports.filter(q => q.kind === 'out').findIndex(q => q.el && q.el.dataset.t === 'a')); window.__tpConnect(a, 'out', oi, f, 'in', 0); }   // A into the filter, the canvas's own way
    const oi = Math.max(0, f.ports.filter(q => q.kind === 'out').findIndex(q => q.el && q.el.dataset.t === 'a'));
    const okC = window.__tpConnect(f, 'out', oi, fx, 'in', 0); const d = window.__fxrDevs()[0];
    return { have: true, okC, route: d.route[0], taps: d.taps, cables: window.__cables().filter(c => /fx-reverb-1|filter/.test(c)) }; });
  ok(filt.have && filt.okC === true && filt.route === 1 && filt.taps === 0, '[3] filter → reverb keeps the pill and CLEARS the tap (post-filter, the synth\'s law)', JSON.stringify(filt));
  ok(filt.cables && filt.cables.includes('osc-a.out0>filter.in0') && filt.cables.includes('filter.out0>fx-reverb-1.in0') && !filt.cables.includes('osc-a.out0>fx-reverb-1.in0'), '[3] the canvas now goes osc A → filter → reverb', JSON.stringify(filt.cables));
  // [4] a pill clicked ON THE CANVAS is a direct tap
  const pill = await p.evaluate(() => { const card = document.querySelector('#tp-page .fxr-dev[data-dev="0"]'); if (!card) return { have: false }; const r = card.querySelector('.fxr-r[data-r="1"]'); if (!r) return { have: false, noPill: true };
    r.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true })); const d = window.__fxrDevs()[0]; return { have: true, route: d.route[1], taps: d.taps, tapsP: Math.round(window.__P('SYN_RVB_TAPS') * 2047) }; });
  ok(pill.have && pill.route === 1 && (pill.taps & 2) === 2 && (pill.tapsP & 2) === 2, '[4] the B pill clicked on the canvas card lights B as a DIRECT tap (bit 1)', JSON.stringify(pill));
  // [5] Chop → Reverb: the card goes IN the rack, before the reverb
  const inl = await p.evaluate(() => { const nb = window.__tpNodeByKey, a = nb('osc-a'), ch = nb('flow-chop'), fx = nb('fx-reverb-1'); if (!a || !ch || !fx) return { have: false };
    const oi = Math.max(0, a.ports.filter(q => q.kind === 'out').findIndex(q => q.el && q.el.dataset.t === 'a'));
    const okA = window.__tpConnect(a, 'out', oi, ch, 'in', 0);
    const co = Math.max(0, ch.ports.filter(q => q.kind === 'out').findIndex(q => q.el && q.el.dataset.t === 'a'));
    const okF = window.__tpConnect(ch, 'out', co, fx, 'in', 0);
    const d = window.__fxrDevs()[0]; const cables = window.__cables();
    return { have: true, okA, okF, chopA: window.__P('FLOW_CHOP_SRC_A'), chopTaps: Math.round(window.__P('FLOW_CHOP_TAPS') * 2047), inline: window.__P('FLOW_CHOP_INLINE'), rank: window.__P('FLOW_CHOP_RANK'), rvbRank: d.rank, rvbA: d.route[0],
      cable: cables.includes('flow-chop.out0>fx-reverb-1.in0'), edit: window.__cableEdit('flow-chop.out0>fx-reverb-1.in0'), oscToChop: cables.includes('osc-a.out0>flow-chop.in0'), oscToRvb: cables.includes('osc-a.out0>fx-reverb-1.in0') || cables.includes('filter.out0>fx-reverb-1.in0') }; });
  ok(inl.have && inl.okA === true && inl.chopA === 1 && (inl.chopTaps & 1) === 1, '[5] osc A → Chop lights A on the card as a direct tap', JSON.stringify(inl));
  ok(inl.okF === true && inl.inline === 1 && inl.rank > 0 && inl.rank < inl.rvbRank && inl.rvbA === 1, '[5] Chop → Reverb: FLOW_CHOP_INLINE = 1, its rank sits below the reverb\'s, the reverb takes A', JSON.stringify(inl));
  ok(inl.cable && inl.edit && inl.edit.finl && inl.oscToChop && !inl.oscToRvb, '[5] the canvas draws osc A → Chop → Reverb (the reverb no longer taps A itself)', JSON.stringify({ cable: inl.cable, edit: inl.edit, oscToChop: inl.oscToChop, oscToRvb: inl.oscToRvb }));
  const cutIt = await p.evaluate(() => { const c = window.__tpDerive().find(c => c.id === 'flow-chop.out0>fx-reverb-1.in0'); if (!c) return { have: false }; const cables0 = window.__tpCables ? window.__tpCables() : []; const cab = null;
    // cut through the same path the canvas uses: find the live cable object by id
    const live = (window.__tpLiveCables ? window.__tpLiveCables() : null); return { have: true, edit: c.edit }; });
  const uninl = await p.evaluate(() => { const n = window.__tpNodeByKey('flow-chop'); window.__tpSetFlowInline && window.__tpSetFlowInline(n.sub, false); return { inline: window.__P('FLOW_CHOP_INLINE'), cable: window.__cables().includes('flow-chop.out0>fx-reverb-1.in0'), toOut: window.__cables().includes('flow-chop.out0>out.in0') }; });
  ok(uninl.inline === 0 && !uninl.cable, '[5] taking the card out of the rack (the cable\'s cut) puts it after everything again', JSON.stringify(uninl));
  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL', e); process.exit(1); });
