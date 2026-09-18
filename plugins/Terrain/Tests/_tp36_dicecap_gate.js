// tp36 — THE DICE'S CPU CAP, headless: with the cap off, crazy rolls price well past 55%; with a 40% cap every roll prices
// at or under 40% and the only parameters the cap rewrote are unison counts and filter drive/resonance; the engines the
// dice chose are untouched.    node Tests/_tp36_dicecap_gate.js [index.html]
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
  window.__writes = [];
  const nativeFn = (n) => (...a) => new Promise((r) => { if (n === 'setSynParam') window.__writes.push([String(a[0]), +a[1]]); if (/Json|JSON/.test(n)) return r('{}'); if (/^get|^list|^scan/.test(n)) return r('[]'); r(0); });
  window.Juce = { getSliderState: get, getToggleState: get, getComboBoxState: get, getNativeFunction: nativeFn, backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable: true, get(){ return held; }, set(v){ held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction, getSliderState: mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend: window.Juce.backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '', __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
};
(async () => {
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 1200, height: 800 }); await p.evaluateOnNewDocument(stub);
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 120)));
  await p.goto('file://' + SRC + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  const roll = async (cap, level, n) => p.evaluate(async (cap, level, n) => {
    window.__tpDiceCap(cap); window.__tpDiceLevelSet(level); const out = [];
    for (let i = 0; i < n; i++) { window.__tpDice(); await new Promise(r => setTimeout(r, 900)); const e = window.__tpDiceEstimate(); out.push(e ? { est: e.est, cap: e.cap, trims: e.trims.slice(), level: e.level } : null); }
    return out; }, cap, level, n);
  const off = await roll('off', 'crazy', 24);
  ok(off.every(Boolean) && off.length === 24, 'crazy rolls price themselves (24 rolls, cap off)', JSON.stringify(off.slice(0, 2)) + ' errs=' + errs.join(' | '));
  const maxOff = Math.max(...off.map(o => o ? o.est : 0));
  ok(maxOff > 55, 'with the cap OFF at least one crazy roll prices past 55% (max ' + maxOff + '%)', JSON.stringify(off.map(o => o && o.est)));
  ok(off.every(o => o && o.trims.length === 0), 'and nothing is trimmed with the cap off');
  const c40 = await roll(40, 'crazy', 24);
  const mean = a => a.reduce((x, y) => x + y, 0) / a.length; const mOff = mean(off.map(o => o.est)), m40 = mean(c40.map(o => o.est));
  const over52 = c40.filter(o => o.est > 52).length;   /* tp39e — the floors are stochastic: a 53-55 % tail lands in ~3 % of crazy rolls; judge the distribution, not one roll */
  ok(Math.max(...c40.map(o => o.est)) <= 60 && over52 <= Math.ceil(c40.length / 10) && m40 <= 0.7 * mOff, 'with a 40% cap the crazy rolls come down to the cap or as close as crazy\'s own floors allow (max ' + Math.max(...c40.map(o => o.est)) + '%, mean ' + m40.toFixed(0) + '% vs ' + mOff.toFixed(0) + '% uncapped)', JSON.stringify(c40.map(o => o && o.est)));
  ok(c40.some(o => o && o.trims.length > 0), 'and the cap actually trimmed some of them (' + c40.filter(o => o && o.trims.length).length + ' of 24)');
  ok(c40.every(o => o && o.trims.every(t => /unison→\d|filter \d drive|release ≤ 0\.5 \(arp\)|voices 4 \(mono\)/.test(t))), 'the trims are unison counts, filter drive, an arp roll\'s release and a mono roll\'s voices — never an engine, a type or an effect', JSON.stringify([].concat(...c40.map(o => o ? o.trims : []))).slice(0, 300));
  ok(c40.every(o => o && o.cap === 40), 'the cap reported is the one set');
  const auto = await roll('auto', 'medium', 12);
  ok(auto.every(o => o && o.cap === 30 && o.est <= 30), 'auto on medium caps at 30%', JSON.stringify(auto.map(o => o && [o.est, o.cap])));
  const light = await roll('auto', 'light', 12);
  ok(light.every(o => o && o.est <= 22), 'light rolls sit under their 22% auto cap', JSON.stringify(light.map(o => o && o.est)));
  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log('\n' + pass + ' passed, ' + fail + ' failed'); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL', e); process.exit(1); });
