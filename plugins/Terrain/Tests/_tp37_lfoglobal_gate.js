// tp37 — THE GLOBAL LFO CLOCK, headless. Right-click the BPM · Hz pill on the LFO card → "Global clock"; while it is on
// every tab shows and drives ONE rate / division / sync (LFO_GLOBAL_*), the rate knob's mod destination is the global
// rate (5199), and off puts everything back.   node Tests/_tp37_lfoglobal_gate.js [index.html]
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
  const nativeFn = (n) => (...a) => new Promise((r) => { if (/Json|JSON/.test(n)) return r('{}'); if (/^get|^list|^scan/.test(n)) return r('[]'); r(0); });
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
  const S = (id) => p.evaluate((id) => window.Juce.getSliderState(id).getNormalisedValue(), id);
  const setS = (id, v) => p.evaluate((id, v) => window.Juce.getSliderState(id).setNormalisedValue(v), id, v);
  await setS('LFO1_RATE', 0.3); await setS('LFO2_RATE', 0.7);
  // the mod page holds the LFO card
  await p.evaluate(() => { const t = document.querySelector('[data-page="mod"], #tab-mod, .tab[data-tab="mod"]'); if (t) t.click(); });
  await sleep(400);
  const seg = await p.$('#mv-seg');
  ok(!!seg, 'the BPM · Hz pill (#mv-seg) is on the LFO card', errs.join(' | '));
  if (!seg) { await b.close(); console.log('\n' + pass + ' passed, ' + fail + ' failed'); process.exit(1); }
  const rightClick = async (sel) => { await p.evaluate((sel) => { const el = document.querySelector(sel); el.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2 })); }, sel); await sleep(150); };
  await rightClick('#mv-seg');
  const rows = await p.evaluate(() => [].map.call(document.querySelectorAll('#mod-engine .mv-menu div, .mv-menu div, .mvmenu div'), d => d.textContent.trim()));
  const menuRows = rows.length ? rows : await p.evaluate(() => { const m = [...document.querySelectorAll('div')].filter(d => /Global clock/.test(d.textContent) && d.children.length <= 4 && d.querySelectorAll('div').length <= 4); return m.length ? [].map.call(m[0].querySelectorAll('div'), x => x.textContent.trim()) : []; });
  ok(menuRows.some(r => /Global clock/.test(r)) && menuRows.some(r => /Global: BPM/.test(r)) && menuRows.some(r => /Global: Hz/.test(r)), 'right-click offers Global clock / Global: BPM / Global: Hz', JSON.stringify(menuRows));
  { const info = await p.evaluate(() => { const rows = [...document.querySelectorAll('div[data-i="0"]')].filter(d => /Global clock/.test(d.textContent)); const d = rows[rows.length - 1]; if (!d) return 'no row'; const m = d.parentElement; const info = 'rows=' + rows.length + ' menu=' + m.className + ' txt=' + d.textContent.trim(); d.dispatchEvent(new MouseEvent('mousedown', { bubbles: true })); d.dispatchEvent(new MouseEvent('mouseup', { bubbles: true })); d.click(); return info; }); if (process.env.DBG) console.log('   pick:', info); }
  await sleep(200);
  ok((await S('LFO_GLOBAL')) > 0.5, 'picking "Global clock" turns LFO_GLOBAL on');
  const pill = await p.evaluate(() => document.querySelector('#mv-bpm').textContent + '|' + document.querySelector('#mv-hz').textContent + '|' + document.querySelector('#mv-seg').className);
  ok(/G·BPM\|G·Hz\|.*glob/.test(pill), 'the pill reads G·BPM · G·Hz and glows', pill);
  // drag the rate: writes the GLOBAL rate, not LFO 1's
  const r1 = await S('LFO1_RATE'), g0 = await S('LFO_GLOBAL_RATE');
  const rt = await p.$('#mv-rate'); const rb = await rt.boundingBox();
  await p.mouse.move(rb.x + 10, rb.y + 5); await p.mouse.down(); await p.mouse.move(rb.x + 10, rb.y - 60, { steps: 6 }); await p.mouse.up(); await sleep(150);
  const g1 = await S('LFO_GLOBAL_RATE');
  ok(Math.abs((await S('LFO1_RATE')) - r1) < 1e-6 && g1 > g0 + 0.05, 'dragging the rate under the global clock moves LFO_GLOBAL_RATE and leaves LFO1_RATE alone', 'LFO1 ' + r1 + ' → ' + await S('LFO1_RATE') + '; global ' + g0 + ' → ' + g1);
  const hz1 = await p.evaluate(() => document.querySelector('#mv-rv').textContent);
  await p.evaluate(() => document.querySelector('.mv-tabs .t[data-tab="2"]').click()); await sleep(150);
  const hz2 = await p.evaluate(() => document.querySelector('#mv-rv').textContent);
  ok(hz1 === hz2, 'tab 2 shows the same Hz as tab 1 (one clock)', hz1 + ' vs ' + hz2);
  await p.evaluate(() => document.querySelector('#mv-bpm').click()); await sleep(150);
  ok((await S('LFO_GLOBAL_SYNC')) > 0.5 && (await S('LFO2_SYNC')) < 0.5, 'clicking BPM under the global clock sets LFO_GLOBAL_SYNC, not LFO 2\'s');
  const dest = await p.evaluate(() => { try { window.__openLfoCard(); } catch (e) {} const c = document.querySelector('#lfo-ext [data-mod-dest], .ti-card [data-mod-dest="5199"], [data-mod-dest="5199"]'); return c ? c.getAttribute('data-mod-dest') : null; });
  ok(dest === '5199', 'the rate knob\'s mod destination is the global rate (5199) while the clock is global', String(dest));
  await rightClick('#mv-seg');
  { const info = await p.evaluate(() => { const rows = [...document.querySelectorAll('div[data-i="0"]')].filter(d => /Global clock/.test(d.textContent)); const d = rows[rows.length - 1]; if (!d) return 'no row'; const m = d.parentElement; const info = 'rows=' + rows.length + ' menu=' + m.className + ' txt=' + d.textContent.trim(); d.dispatchEvent(new MouseEvent('mousedown', { bubbles: true })); d.dispatchEvent(new MouseEvent('mouseup', { bubbles: true })); d.click(); return info; }); if (process.env.DBG) console.log('   pick:', info); }
  await sleep(200);
  const pillOff = await p.evaluate(() => document.querySelector('#mv-bpm').textContent + '|' + document.querySelector('#mv-hz').textContent);
  ok((await S('LFO_GLOBAL')) < 0.5 && pillOff === 'BPM|Hz', 'picking it again turns the clock off and the pill reads BPM · Hz', pillOff);
  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log('\n' + pass + ' passed, ' + fail + ' failed'); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL', e); process.exit(1); });
