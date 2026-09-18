// tp39 — THE RANDOMIZATION MENU, headless with a stubbed backend and a fake one-shot library:
//   [1] the canvas has no dice tool; right-click on the header dice opens the browser's sheet titled Randomize with
//       Aim / Roll / Reach / CPU cap chip rows;  [2] Drums + crazy: every Sample/Resynth oscillator the roll enables is
//       loaded from Drums or 808 (verified one-shots), a drum blend lands now and then;  [3] a verify failure retries
//       another file, and three failures put the oscillator back on a wavetable;  [4] several aims: the rolls draw from
//       them only;  [5] blocks: Effects unticked = the rack is not touched, Oscillators unticked = ENABLE untouched;
//   [6] crazy rolls draw CUSTOM LFO shapes through __lfoDrawRandom.     node Tests/_tp39_dicemenu_gate.js [index.html]
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
  const p = await b.newPage(); await p.setViewport({ width: 1200, height: 800 }); await p.evaluateOnNewDocument(stub);
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 140)));
  await p.goto('file://' + SRC + '?page=1', { waitUntil: 'load' }); await sleep(3200);
  const S = (id) => p.evaluate((id) => (window.__params[id] != null ? window.__params[id] : window.Juce.getSliderState(id).getNormalisedValue()), id);
  await p.evaluate(() => { window.__P = (id) => (window.__params[id] != null ? window.__params[id] : window.Juce.getSliderState(id).getNormalisedValue()); });
  // [1] one dice, the sheet
  const noTool = await p.evaluate(() => !document.querySelector('.tp-tools .g[data-t="dice"]'));
  await p.evaluate(() => { const d = document.getElementById('dice-btn'); d.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2 })); }); await sleep(300);
  const sheet = await p.evaluate(() => { const sh = document.getElementById('tp-sheet'); return { on: sh && sh.classList.contains('on'), title: sh ? (sh.querySelector('.tp-hd') || {}).textContent : '', rows: ['dc-aim', 'dc-blk', 'dc-lvl', 'dc-cap'].map(id => (document.getElementById(id) || { children: [] }).children.length) }; });
  ok(noTool, '[1] the canvas toolbar has no dice of its own');
  ok(sheet.on && sheet.title === 'Randomize' && sheet.rows.every(n => n >= 5), '[1] right-click on the header dice opens the Randomize sheet with Aim / Roll / Reach / CPU cap chips', JSON.stringify(sheet));
  // Drums + crazy through the chips, Roll
  await p.evaluate(() => { document.querySelector('#dc-aim [data-a="drums"]').click(); document.querySelector('#dc-lvl [data-l="crazy"]').click(); document.querySelector('#dc-cap [data-c="off"]').click(); document.getElementById('tp-sh-ok').click(); });
  await sleep(4200);
  const aims = await p.evaluate(() => window.__tpDiceAims());
  ok(aims.length === 1 && aims[0] === 'drums' && (await p.evaluate(() => window.__tpDiceLevel())) === 'crazy', '[2] the sheet saved Drums + crazy and rolled', JSON.stringify(aims));
  const drum = await p.evaluate(() => { const out = { on: [], loads: window.__loads.slice(), blends: window.__blends.slice(), last: window.__diceLastOneShot || {} };
    'abcdefgh'.split('').forEach(o => { const X = 'SYN_OSC_' + o.toUpperCase() + '_'; if (window.__P(X + 'ENABLE') > 0.5) out.on.push({ o, eng: Math.round(window.__P(X + 'ENGINE') * 6) }); }); return out; });
  const sampleOscs = drum.on.filter(x => x.eng === 1 || x.eng === 2 || x.eng === 3);
  ok(drum.on.length >= 2 && sampleOscs.length === drum.on.length, '[2] a drums roll turns on 2-4 oscillators, every one a sample-family engine', JSON.stringify(drum.on));
  ok(sampleOscs.every(x => drum.last[x.o] && /\/(Drums|808)\//.test(drum.last[x.o])), '[2] every one of them was handed a Drums or 808 one-shot', JSON.stringify(drum.last));
  // [3] verify + retry: make the first pick fail
  let retried = false, fellBack = false;
  for (let k = 0; k < 4 && !(retried && fellBack); k++) {
    await p.evaluate(() => { window.__loads = []; window.__loaded = {}; window.__failPaths = {}; window.__natCount = {}; });
    await p.evaluate(() => { window.__tpDice(); }); await sleep(400);
    const first = await p.evaluate(() => window.__loads.slice());
    if (!first.length) continue;
    const o = first[0][0];
    // undo the stub's 'loaded' for this osc so the verify fails, and fail everything the osc will try
    await p.evaluate((o) => { delete window.__loaded[o]; window.__failAll = o; const nf = window.Juce.getNativeFunction; }, o);
    await p.evaluate((o) => { window.__loaded = {}; for (const c of ['Drums', '808']) for (const f of ['kick.wav', 'snare.wav', 'hat.wav', 'clap.wav', '808a.wav', '808b.wav']) window.__failPaths['/lib/' + c + '/' + f] = 1; }, o);
    await sleep(5200);
    const loads = await p.evaluate((o) => window.__loads.filter(l => l[0] === o).length, o);
    const eng = await S('SYN_OSC_' + o.toUpperCase() + '_ENGINE');
    if (loads >= 2) retried = true; if (eng < 0.01) fellBack = true;
  }
  ok(retried, '[3] a one-shot that does not land is retried with another file');
  ok(fellBack, '[3] and after three misses the oscillator goes back to a wavetable (ENGINE 0)');
  await p.evaluate(() => { window.__failPaths = {}; });
  // [4] several aims
  await p.evaluate(() => { window.__tpDiceAims(['keys', 'pads']); window.__tpDiceLevelSet('medium'); });
  const seen = new Set(); for (let k = 0; k < 8; k++) { await p.evaluate(() => window.__tpDice()); await sleep(500); seen.add(await p.evaluate(() => window.__tpDiceLastAim)); }
  ok([...seen].every(a => a === 'keys' || a === 'pads') && seen.size === 2, '[4] with Keys + Pads ticked the rolls draw from those two only', JSON.stringify([...seen]));
  // [5] blocks
  await p.evaluate(() => { window.__tpDiceAims(['keys']); window.__tpDiceBlocks({ osc: 1, flt: 1, env: 1, fx: 0, mod: 1 }); window.__fxrAddCount = 0; const oa = window.__fxrAdd; window.__fxrAdd = function(){ window.__fxrAddCount++; return oa.apply(this, arguments); }; });
  await p.evaluate(() => window.__tpDice()); await sleep(600);
  const fxAdds = await p.evaluate(() => window.__fxrAddCount);
  ok(fxAdds === 0, '[5] Effects unticked: the rack is not rebuilt (no __fxrAdd)', 'adds=' + fxAdds);
  const enBefore = await p.evaluate(() => 'abcdefgh'.split('').map(o => window.__P('SYN_OSC_' + o.toUpperCase() + '_ENABLE')));
  await p.evaluate(() => { window.__tpDiceBlocks({ osc: 0, flt: 1, env: 1, fx: 1, mod: 1 }); });
  for (let k = 0; k < 3; k++) { await p.evaluate(() => window.__tpDice()); await sleep(500); }
  const enAfter = await p.evaluate(() => 'abcdefgh'.split('').map(o => window.__P('SYN_OSC_' + o.toUpperCase() + '_ENABLE')));
  ok(JSON.stringify(enBefore) === JSON.stringify(enAfter), '[5] Oscillators unticked: three rolls leave every ENABLE as it was', enBefore + ' vs ' + enAfter);
  // [6] drawn LFOs
  await p.evaluate(() => { window.__tpDiceBlocks({ osc: 1, flt: 1, env: 1, fx: 1, mod: 1 }); window.__tpDiceAims(['pads']); window.__tpDiceLevelSet('crazy'); window.__drawCalls = []; const od = window.__lfoDrawRandom; window.__lfoDrawRandom = function(n, k, gv, gh, sn){ window.__drawCalls.push([n, k, gv, gh]); return od.apply(this, arguments); }; });
  let drew = 0, customOk = true;
  for (let k = 0; k < 3; k++) { await p.evaluate(() => { window.__drawCalls = []; window.__tpDice(); }); await sleep(600);
    const calls = await p.evaluate(() => window.__drawCalls.slice()); drew += calls.length;
    for (const c of calls) { const sh = await S('LFO' + c[0] + '_SHAPE'); if (Math.abs(sh - 0.7) > 0.02) customOk = false; if (c[2] < 8) customOk = false; } }
  ok(drew >= 3 && customOk, '[6] crazy rolls draw CUSTOM LFO shapes with the grid turned up (' + drew + ' drawings over 3 rolls)');
  // [7] the sheet over the PATCHER page (the browser's sheet lives in the synth page's DOM — it must still show on page 5)
  const p5 = await b.newPage(); await p5.setViewport({ width: 1200, height: 800 }); await p5.evaluateOnNewDocument(stub);
  await p5.goto('file://' + SRC + '?page=5', { waitUntil: 'load' }); await sleep(2500);
  const vis5 = await p5.evaluate(() => { const d = document.getElementById('dice-btn'); d.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2 }));
    const sh = document.getElementById('tp-sheet'); const card = sh && sh.querySelector('.tp-card'); const r = card ? card.getBoundingClientRect() : { width: 0, height: 0 }; const cs = card ? getComputedStyle(card) : {};
    return { on: !!(sh && sh.classList.contains('on')), w: Math.round(r.width), h: Math.round(r.height), vis: cs.visibility, disp: cs.display, op: cs.opacity, tools: !!document.querySelector('.tp-tools'), rows: (document.getElementById('dc-aim') || { children: [] }).children.length }; });
  ok(vis5.on && vis5.w > 200 && vis5.h > 100 && vis5.vis !== 'hidden' && vis5.disp !== 'none' && +vis5.op > 0.5 && vis5.rows >= 5, '[7] on the Patcher page the same sheet opens and is visible', JSON.stringify(vis5));
  await p5.close();
  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log('\n' + pass + ' passed, ' + fail + ' failed'); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL', e); process.exit(1); });
