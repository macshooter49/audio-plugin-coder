// tp35 — THE ARROW NEVER LEAVES ITS FOLDER. Headless, with the wavetable registry under the test's control:
//   [1] a folder is stepped in order when the registry answers at once;
//   [2] a registry that never answers is stood in for by the last complete answer (window.__wtCatsLast): still in order;
//   [3] no cache + no answer + an import the page cannot place: the press is a no-op (no jump to a factory table);
//   [4] a press that lands mid-step is kept: two presses = two steps, not one, not three.
//   node Tests/_tp35_wtstep_gate.js [index.html]
const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const SRC = process.argv[2] || (process.cwd() + '/Source/ui/public/index.html');
let pass = 0, fail = 0; const ok = (c, label, detail) => { if (c) { pass++; console.log('  PASS  ' + label); } else { fail++; console.log('  FAIL  ' + label + (detail ? '\n          ' + detail : '')); } };
const stub = () => {
  const CH = { SYN_OSC_A_WT_PRESET: 40 }; const states = new Map();
  const mk = (name, n) => { const props = { start: 0, end: (n ? n - 1 : 1), skew: 1, name, label: '', numSteps: n || 100, interval: n ? 1 : 0, parameterIndex: states.size };
    const st = { name, scaledValue: 0, properties: props, getScaledValue: () => st.scaledValue, setScaledValue(v){ st.scaledValue = v; },
      getNormalisedValue(){ return (st.scaledValue - props.start) / ((props.end - props.start) || 1); },
      setNormalisedValue(v){ st.scaledValue = n ? Math.round(v * (props.end - props.start)) : v; (st.__ls || []).forEach(f => { try { f(); } catch (e) {} }); },
      valueChangedEvent: { addListener(f){ (st.__ls = st.__ls || []).push(f); return { remove(){} }; }, removeListener(){} },
      propertiesChangedEvent: { addListener(){ return { remove(){} }; }, removeListener(){} },
      getChoiceIndex: () => st.scaledValue, setChoiceIndex(i){ st.scaledValue = i; }, getValue: () => false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} }; return st; };
  const get = (nm) => { if (!states.has(nm)) states.set(nm, mk(nm, CH[nm])); return states.get(nm); };
  window.__natives = []; window.__regDelay = 0; window.__regHang = false;
  window.__reg = { files: [], folders: [{ name: 'Abstract', items: ['Arrowhead', 'Blancmange', 'Butterfly', 'Cantor', 'Canyon', 'Circle'].map(n => ({ name: n, path: '/tmp/wt/Abstract/' + n + '.flac' })) }], factory: { cats: [] } };
  const nativeFn = (n) => (...a) => new Promise((r) => { window.__natives.push({ fn: n, args: a.map(String) });
    if (n === 'listWtImports') { if (window.__regHang) return; setTimeout(() => r(JSON.stringify(window.__reg)), window.__regDelay || 0); return; }
    if (n === 'listImports') { if (window.__regHang) return; return r('{"root":"","exists":true,"items":[]}'); }
    if (/getPresets/i.test(n)) return r('[]');
    if (/Json|JSON/.test(n)) return r('{}');
    r(0); });
  window.Juce = { getSliderState: get, getToggleState: get, getComboBoxState: get, getNativeFunction: nativeFn, backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable: true, get(){ return held; }, set(v){ held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction, getSliderState: mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend: window.Juce.backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '', __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
};
(async () => {
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const open = async () => { const p = await b.newPage(); await p.setViewport({ width: 1100, height: 800 }); await p.evaluateOnNewDocument(stub);
    p.on('pageerror', e => console.log('  pageerror', e.message.slice(0, 120)));
    await p.goto('file://' + SRC + '?page=1', { waitUntil: 'load' }); await sleep(1200); return p; };
  const cur = p => p.evaluate(() => ({ nm: window.wtWaterfall && window.wtWaterfall.importName ? window.wtWaterfall.importName.a : null, path: window.__wtPath.a, cached: !!(window.__wtCatsLast && window.__wtCatsLast.length) }));
  // put osc A on an import of the folder, the way the page's own import branch does (wtApplyItem is module-local)
  const put = (p, i) => p.evaluate((i) => { const it = window.__reg.folders[0].items[i]; const WT = window.wtWaterfall; if (!WT.importName) WT.importName = {};
    WT.imported.a = true; WT.importName.a = it.name; window.__wtPath.a = it.path; window.__wtFolder.a = 'Abstract'; }, i);
  const step = (p, d) => p.evaluate((d) => { window.wtStepPreset('a', d); }, d);

  let p = await open(); await put(p, 1);
  await step(p, 1); await sleep(600); let c = await cur(p);
  ok(c.nm === 'Butterfly', '[1] the folder steps in order when the registry answers at once (Blancmange -> Butterfly)', JSON.stringify(c));
  ok(c.cached, '[1] and the complete answer is remembered (window.__wtCatsLast)', JSON.stringify(c));
  await p.evaluate(() => { window.__regHang = true; });
  await step(p, 1); await sleep(900); c = await cur(p);
  ok(c.nm === 'Cantor', '[2] the registry never answers: the last complete answer stands in, still in order (Butterfly -> Cantor)', JSON.stringify(c));
  await step(p, -1); await sleep(900); c = await cur(p);
  ok(c.nm === 'Butterfly', '[2] and backwards (Cantor -> Butterfly)', JSON.stringify(c));
  await p.close();

  p = await open(); await p.evaluate(() => { window.__regHang = true; }); await put(p, 2);
  await step(p, 1); await sleep(2200); c = await cur(p);
  ok(c.nm === 'Butterfly' && /Butterfly/.test(c.path || ''), '[3] no cache, no answer: the press is a no-op — the import stays, nothing jumps to a factory table', JSON.stringify(c));
  await p.close();

  p = await open(); await put(p, 1); await p.evaluate(() => { window.__regDelay = 300; });
  await step(p, 1); await sleep(60); await step(p, 1); await sleep(1600); c = await cur(p);
  ok(c.nm === 'Cantor', '[4] two presses 60 ms apart while the registry takes 300 ms = two steps (Blancmange -> Cantor)', JSON.stringify(c));
  await p.close(); await b.close();
  console.log('\n' + pass + ' passed, ' + fail + ' failed'); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL', e); process.exit(1); });
