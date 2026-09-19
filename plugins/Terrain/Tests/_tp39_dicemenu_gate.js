// tp39 — THE RANDOMIZATION MENU, headless with a stubbed backend and a fake one-shot library:
//   [1] the canvas has no dice tool; right-click on the header dice opens the browser's sheet titled Randomize with
//       Aim / Roll / Reach / CPU cap chip rows;  [2] Drums + crazy: every Sample/Resynth oscillator the roll enables is
//       loaded from Drums or 808 (verified one-shots), a drum blend lands now and then;  [3] a verify failure retries
//       another file, and three failures put the oscillator back on a wavetable;  [4] several aims: the rolls draw from
//       them only;  [5] blocks: Effects unticked = the rack is not touched, Oscillators unticked = ENABLE untouched;
//   [6] crazy rolls draw CUSTOM LFO shapes through __lfoDrawRandom;  [8] ticks commit live;  [9] the dice pressed through the open
//       sheet closes it and rolls with the ticks (tp39c);  [10] Flow cards is a block apart from Modulation;  [11] ONLY semantics (tp39d): styles = the whole preset, ticked blocks = only those;  [12] reverb/delay mod knobs pinned;
//   [13] Arp Latch forced OFF + the dots drawn (tp39f);  [14] one LFO per flow card, Robin included, never Latch.     node Tests/_tp39_dicemenu_gate.js [index.html]
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
  const seen = new Set(); for (let k = 0; k < 12; k++) {   /* 12: two aims all landing the same side is 0.05 %, not 0.8 % */ await p.evaluate(() => window.__tpDice()); await sleep(500); seen.add(await p.evaluate(() => window.__tpDiceLastAim)); }
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
  // [8] tp39c — ticks commit the moment they are made (no Roll needed)
  await p.evaluate(() => { window.__tpDiceAims([]); window.__tpDiceBlocks({}); const d = document.getElementById('dice-btn'); d.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2 })); }); await sleep(300);
  await p.evaluate(() => { const c = (id, attr, v) => { const b = [...document.querySelectorAll('#' + id + ' .tp-chip')].find(x => x.dataset[attr] === v); if (b) b.click(); }; c('dc-aim', 'a', 'bass'); c('dc-blk', 'b', 'fx'); c('dc-lvl', 'l', 'wild'); c('dc-cap', 'c', 'off'); });
  const live = await p.evaluate(() => { const L = window.__tpLayout(); return { aims: L.aims, fx: L.blocks.fx, flow: L.blocks.flow, lvl: L.dlevel, cap: L.cpuCap, on: document.getElementById('tp-sheet').classList.contains('on') }; });
  ok(live.on && JSON.stringify(live.aims) === '["bass"]' && live.fx === 1 && live.flow === 0 && live.lvl === 'wild' && live.cap === 'off', '[8] a tick is a setting the moment it is made (sheet still open; Only: Effects)', JSON.stringify(live));
  // [9] tp39c — the dice pressed THROUGH the open sheet: the sheet closes and the roll goes with the ticks
  const pressed = await p.evaluate(async () => { const sh = document.getElementById('tp-sheet'), db = document.getElementById('dice-btn'); const r = db.getBoundingClientRect();
    window.__fxrAddCount = 0; const oa = window.__fxrAdd; window.__fxrAdd = function(){ window.__fxrAddCount++; return oa ? oa.apply(this, arguments) : undefined; };
    const before = window.__P('SYN_ENV_AMP_D'); window.__tpDiceLastAim = null;
    sh.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 }));
    await new Promise(res => setTimeout(res, 700));
    return { closed: !sh.classList.contains('on'), aim: window.__tpDiceLastAim, fxAdds: window.__fxrAddCount, envMoved: window.__P('SYN_ENV_AMP_D') !== before }; });
  ok(pressed.closed && pressed.aim === 'bass' && pressed.fxAdds > 0 && !pressed.envMoved, '[9] pressing the dice through the sheet closes it and rolls with the ticks (Bass, Only: Effects → effects rebuilt, envelopes untouched)', JSON.stringify(pressed));
  // [10] tp39c — flow cards are their own block
  await p.evaluate(() => { window.__tpDiceAims(['keys']); window.__n = { setChain: 0, rmRoute: 0 }; const os = window.__flowSetChain, orr = window.__tiRemoveRoute; window.__flowSetChain = function(){ window.__n.setChain++; return os ? os.apply(this, arguments) : undefined; }; window.__tiRemoveRoute = function(){ window.__n.rmRoute++; return orr ? orr.apply(this, arguments) : undefined; }; });
  await p.evaluate(() => { window.__n = { setChain: 0, rmRoute: 0 }; window.__tpDiceBlocks({ osc: 1, flt: 1, env: 1, fx: 1, mod: 1, flow: 0 }); window.__tpDice(); }); await sleep(600);
  const flowOff = await p.evaluate(() => window.__n);
  await p.evaluate(() => { window.__n = { setChain: 0, rmRoute: 0 }; window.__tpDiceBlocks({ osc: 1, flt: 1, env: 1, fx: 1, mod: 0, flow: 1 }); window.__tiAddRoute(0, 1, 0); window.__tpDice(); }); await sleep(600);
  const modOff = await p.evaluate(() => ({ n: window.__n, routes: (window.__tiRoutes ? window.__tiRoutes() : []).length }));
  ok(flowOff.setChain === 0 && modOff.n.setChain === 0, '[10] tp48 — the roll never deals the flow chain, ticked or not (Max: "remove the flow cards out of the global randomization")', JSON.stringify({ flowOff, modOff }));
  ok(modOff.n.rmRoute === 0 && modOff.routes >= 1, '[10] Modulation unticked: no route is removed (the one added before the roll survives)', JSON.stringify(modOff));
  const chips6 = await p.evaluate(() => { const d = document.getElementById('dice-btn'); d.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2 })); const c = [...document.querySelectorAll('#dc-blk .tp-chip')].map(b => b.textContent); window.__tpCloseSheet(); return c; });
  ok(chips6.length === 5 && !chips6.includes('Flow cards') && chips6.includes('Modulation'), '[10] tp48 — the Roll row has five chips and no Flow cards', chips6.join(','));
  // [11] tp39d — Max: "keys n pads = everything including FX and modulation; on effects = only FX; FX + modulation = new fx + mod"
  const wrapCounts = () => p.evaluate(() => { window.__n = { setChain: 0, rmRoute: 0, fxAdd: 0 }; if (!window.__wrapped) { window.__wrapped = 1; const os = window.__flowSetChain, orr = window.__tiRemoveRoute, oa = window.__fxrAdd;
    window.__flowSetChain = function(){ window.__n.setChain++; return os ? os.apply(this, arguments) : undefined; }; window.__tiRemoveRoute = function(){ window.__n.rmRoute++; return orr ? orr.apply(this, arguments) : undefined; }; window.__fxrAdd = function(){ window.__n.fxAdd++; return oa ? oa.apply(this, arguments) : undefined; }; } });
  const rollWith = async (aims, blocks) => { await wrapCounts(); await p.evaluate((aims, blocks) => { window.__tpDiceAims(aims); window.__tpDiceBlocks(blocks); window.__tiAddRoute(0, 1, 0); window.__envBefore = window.__P('SYN_ENV_FLT_D'); window.__enBefore = 'abcdefgh'.split('').map(o => window.__P('SYN_OSC_' + o.toUpperCase() + '_ENABLE')).join(); window.__tpDice(); }, aims, blocks); await sleep(700);
    return p.evaluate(() => Object.assign({}, window.__n, { envMoved: window.__P('SYN_ENV_FLT_D') !== window.__envBefore, enMoved: 'abcdefgh'.split('').map(o => window.__P('SYN_OSC_' + o.toUpperCase() + '_ENABLE')).join() !== window.__enBefore, aim: window.__tpDiceLastAim })); };
  let whole = null; for (let k = 0; k < 4 && !(whole && whole.enMoved); k++) whole = await rollWith(['keys', 'pads'], {});
  ok(whole.fxAdd > 0 && whole.setChain === 0 && whole.rmRoute > 0 && whole.envMoved && whole.enMoved && (whole.aim === 'keys' || whole.aim === 'pads'), '[11] Keys + Pads with nothing in Only: the WHOLE preset (oscillators, envelopes, effects, modulation) in that style — and never the flow chain (tp48)', JSON.stringify(whole));
  const fxOnly = await rollWith(['keys', 'pads'], { fx: 1 });
  ok(fxOnly.fxAdd > 0 && fxOnly.setChain === 0 && fxOnly.rmRoute === 0 && !fxOnly.envMoved && !fxOnly.enMoved, '[11] Only: Effects → just the effects (no oscillator, envelope, route or flow change)', JSON.stringify(fxOnly));
  const fxMod = await rollWith(['keys', 'pads'], { fx: 1, mod: 1 });
  ok(fxMod.fxAdd > 0 && fxMod.rmRoute > 0 && fxMod.setChain === 0 && !fxMod.envMoved && !fxMod.enMoved, '[11] Only: Effects + Modulation → new effects + new routes, flow cards and the rest untouched', JSON.stringify(fxMod));
  const rowLbl = await p.evaluate(() => { const d = document.getElementById('dice-btn'); d.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2 })); const ks = [...document.querySelectorAll('#tp-sheet .tp-f .k')].map(k => k.textContent); window.__tpCloseSheet(); return ks; });
  ok(rowLbl.includes('Only') && rowLbl.includes('Aim'), '[11] the sheet rows read Aim / Only / Reach / CPU cap', rowLbl.join(','));
  // [12] tp39e — the reverb / delay Mod Rate + Mod Depth (and the reverb Mod Mode) are pinned to defaults and never LFO targets
  await p.evaluate(() => { window.__tpDiceAims(['pads']); window.__tpDiceBlocks({}); window.__tpDiceLevelSet('crazy'); window.__routeDests = []; const oa = window.__tiAddRoute; window.__tiAddRoute = function(a, b, d){ window.__routeDests.push(d); return oa ? oa.apply(this, arguments) : undefined; }; });
  const PIN = { SYN_RVB_MODDEPTH: 0.25, SYN_RVB_MODRATE: 0.30, SYN_DLY_MODRATE: 0.40, SYN_DLY_MODDEPTH: 0.0 };
  let pinBad = [], hitDest = 0, sawRvb = 0, sawDly = 0, modeMoved = 0;
  for (let k = 0; k < 8; k++) { await p.evaluate(() => { window.__routeDests = []; delete window.__params.SYN_RVB_MODMODE; window.__tpDice(); }); await sleep(600);
    const r = await p.evaluate((PIN) => { const devs = window.__fxrDevs ? window.__fxrDevs() : []; const out = { bad: [], hit: 0, rvb: 0, dly: 0, mode: 0 };
      devs.forEach(d => { if (d.core !== 'reverb' && d.core !== 'delay') return; if (d.core === 'reverb') out.rvb++; else out.dly++;
        (d.back.knobs || []).forEach((kn, i) => { if (!/^(Mod Rate|Mod Depth)$/i.test(kn[0]) && !/_(MODRATE|MODDEPTH)$/.test(kn[2])) return; const v = window.__params[kn[2]]; if (v != null && Math.abs(v - PIN[kn[2]]) > 0.001) out.bad.push(kn[2] + '=' + v.toFixed(2));
          const dest = window.__fxModDest(d.core, d.inst, 4 + i); if (window.__routeDests.indexOf(dest) >= 0) out.hit++; }); });
      devs.forEach(d => { if (d.core === 'reverb' && d.back && d.back.d2 && /^(Mod Mode|Voicing|Motion)$/i.test(d.back.d2.k || '') && window.__params.SYN_RVB_MODMODE != null && Math.abs(window.__params.SYN_RVB_MODMODE - (2 / 5)) > 0.001) out.mode++; }); return out; }, PIN);
    pinBad = pinBad.concat(r.bad); hitDest += r.hit; sawRvb += r.rvb; sawDly += r.dly; modeMoved += r.mode; }
  ok(sawRvb >= 3 && sawDly >= 1 && pinBad.length === 0, '[12] over 8 crazy pad rolls (' + sawRvb + ' reverbs, ' + sawDly + ' delays) Mod Rate / Mod Depth stay at the defaults', pinBad.join(' '));
  ok(hitDest === 0, '[12] and no LFO / envelope route ever lands on them', 'hits=' + hitDest);
  ok(modeMoved === 0, '[12] the reverb Mod Mode dropdown is not rolled either', 'moved=' + modeMoved);
  // [13] tp39f — the arp's LATCH is never rolled: the card dice turns it OFF
  const latch = await p.evaluate(async () => { try { window.Juce.getSliderState('FLOW_ARP_LATCH').setNormalisedValue(1); } catch (e) {} window.__tiDiceMode('arp', true); await new Promise(r => setTimeout(r, 200)); return { latch: window.__P('FLOW_ARP_LATCH'), hasLanes: !!(window.__tiArpLanes && window.__tiArpLanes[0]) }; });
  ok(latch.latch === 0 && latch.hasLanes, '[13] the card dice forces Arp Latch OFF and the arp exposes its lanes', JSON.stringify(latch));
  // [13] the dots are drawn: a whole crazy roll whose chain holds an arp leaves a non-flat pitch lane
  // tp48 — the global roll no longer touches the arp: with an arp in the chain, a crazy whole roll leaves its dots exactly as they were
  let lanes = null; { await p.evaluate(() => { window.__flowSetChain(['arp']); window.__tpDiceAims(['plucks']); window.__tpDiceBlocks({}); window.__tpDiceLevelSet('crazy'); window.__tiArpLanes[0].set({ pitch: [3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3] }); window.__tpDice(); }); await sleep(700);
    lanes = await p.evaluate(() => { const ch = window.__flowChain ? window.__flowChain() : []; const L = window.__tiArpLanes[0].get(); return { hasArp: ch.some(m => /^arp/.test(m)), chain: ch, flat: L.pitch.every(v => v === 3), gateVar: new Set(L.gate.map(v => v.toFixed(2))).size }; }); }
  ok(lanes && lanes.hasArp && lanes.flat, '[13] tp48 — a crazy whole roll with an arp in the chain leaves the arp\'s dots alone (still flat) and the arp in the chain', JSON.stringify(lanes));
  // [14] tp39f — every flow card in the chain gets an LFO of its own, Round Robin included
  const perCard = await p.evaluate(async () => { window.__flowSetChain(['arp', 'glitch', 'drift']); window.__tpDiceAims(['keys']); window.__tpDiceBlocks({ mod: 1 }); window.__tpDice(); await new Promise(r => setTimeout(r, 700));
    const routes = window.__tiRoutes ? window.__tiRoutes() : []; const kd = (n) => window.__tpKnobDest(n);
    const sets = { arp: ['BLEND','GATE','GLIDE','MORPH'].map(k => kd('FLOW_ARP_' + k)), glitch: ['BLEND','DECAY','DEJAVU','BURST'].map(k => kd('FLOW_GLI_' + k)), drift: ['VARY','DRIFT','WOBBLE','GLIDE'].map(k => kd('FLOW_RBN_' + k)) };
    const hits = {}; Object.keys(sets).forEach(c => { hits[c] = routes.filter(r => sets[c].indexOf(r.d) >= 0).length; }); const latchD = kd('FLOW_ARP_LATCH');
    return { chain: window.__flowChain(), hits, routes: routes.length, latchRouted: latchD != null && routes.some(r => r.d === latchD) }; });
  ok(perCard.hits.arp === 0 && perCard.hits.glitch === 0 && perCard.hits.drift === 0 && !perCard.latchRouted && perCard.routes > 0 && perCard.chain.length === 3, '[14] tp48 — Only: Modulation with arp + glitch + Robin in the chain: the chain stays and NO route lands on a flow card (Max: "let me do the flow cards")', JSON.stringify(perCard));
  // [15] tp39f — the chop card's TIME never rolls a bar-long grid (index < 5 = 1/1 … 1/2T) nor the 1/128-1/256 buzz
  const chopTimes = await p.evaluate(() => { const out = []; for (let k = 0; k < 40; k++) { window.__tiDiceMode('chop', true); const d = window.__tiDice && window.__tiDice.chop; out.push(d ? d.S.v.time : -1); } return out; });
  ok(chopTimes.every(v => v >= 5 && v <= 16), '[15] 40 chop card rolls: TIME stays within 1/4 … 1/64', JSON.stringify(chopTimes));
  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log('\n' + pass + ' passed, ' + fail + ' failed'); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL', e); process.exit(1); });
