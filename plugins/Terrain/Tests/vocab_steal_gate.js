// ══ fb631 — THE INLINE VOCAB FIELD SURVIVES THE FOCUS STEAL. Adopted from the fb631 survey's repro.
//    Max: "I try to click the plus button of the styles and it just deletes." Focusing the field armed the
//    host-key bridge; the C++ editArm grabbed keyboard focus; WebKit blurred the field; the old blur
//    handler saw an empty value and re-rendered the chips — the field was gone before a key could land.
//    VS_MUT=blurkill re-installs that behaviour from outside and must go RED.
// Repro for the preset-browser vocab "+" flows. Boots like Tests/preset_surfaces_gate.js.
//   node vocab_repro.js            plain (editArm is a no-op, like the gate)
//   STEAL=1 node vocab_repro.js    editArm(1) blurs the active element (what the C++ grabKeyboardFocus does in WKWebView)
const path = require('path');
const puppeteer = require('/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/Terrain/Tests/node_modules/puppeteer-core');
const PAGE = '/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/Terrain/Source/ui/public/index.html';
const STEAL = true;   // the real WKWebView steal, always
const MUT = process.env.VS_MUT || '';

const STUB = (STEAL) => {
  window.__gateCalls = []; window.__panels = []; window.__log = [];
  const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  const P = (bank, name, type, styles, c, factory) => ({ name, bank, author: factory ? 'Waves Crate' : 'Max', type, styles, note: factory ? 'Mod wheel opens the filter.' : '', fv: 1,
    carries: { wt: c[0], smp: c[1], ir: c[2], flow: c[3], lfo: c[4], nodes: c[5] }, path: '/tmp/Banks/' + bank + '/' + name + '.terrain', mtime: 1, factory: !!factory });
  window.__CAT = { banks: [
    { name: 'Terra', author: 'Waves Crate', version: '1', factory: true, dir: '/f/Terra', presets: [
      P('Terra', 'Glacier', 'Pad', 'Glassy,Evolving', [2,0,0,0,2,0], true), P('Terra', 'Cirrus', 'Pad', 'Bright,Wide', [1,0,0,0,1,0], true),
      P('Terra', 'Tectonic', 'Bass', 'Dark,Punchy', [1,0,0,1,0,0], true), P('Terra', 'Anvil', 'Lead', 'Metallic,Punchy', [1,0,0,2,0,0], true) ] },
    { name: 'User', author: 'Max', version: '1', factory: false, dir: '/tmp/Banks/User', presets: [
      P('User', 'Slatt', 'Bass', 'Dark,Dirty', [1,0,0,1,0,0], false), P('User', 'Monte Cristo', 'Keys', 'Warm,Clean', [1,0,0,0,0,0], false) ] } ],
    caps: { banks: 2, presets: 6, unreadable: 0, maxBanks: 200, maxPresets: 5000 }, userRoot: '/tmp/Banks', factoryRoot: '/f' };
  let curMeta = { name: 'Glacier', bank: 'Terra', author: 'Waves Crate', type: 'Pad', styles: 'Glassy,Evolving', note: 'Mod wheel opens the filter.', fv: 1 };
  const nf = (n) => (...a) => new Promise (r => {
    window.__gateCalls.push ({ fn: n, args: a.map (String) });
    if (n === 'editArm') { if (STEAL && +a[0] === 1) { const ae = document.activeElement; if (ae && ae !== document.body) { window.__log.push('editArm(1) steals focus from ' + (ae.id || ae.className)); ae.blur(); } } return r('ok'); }
    if (n === 'listPresets')   return r (JSON.stringify (window.__CAT));
    if (n === 'getPresetMeta') return r (JSON.stringify (curMeta));
    if (n === 'setPresetMeta') { try { curMeta = JSON.parse (a[0]); } catch (e) {} return r ('ok'); }
    if (n === 'savePresetToBank') { let m = {}; try { m = JSON.parse (a[1]); } catch (e) {} const bank = String (a[0]);
      let b = window.__CAT.banks.find (x => x.name === bank); if (! b) { b = { name: bank, author: m.author || '', version: '1', factory: false, dir: '/tmp/Banks/' + bank, presets: [] }; window.__CAT.banks.push (b); }
      const row = P (bank, m.name, m.type, m.styles, [0,0,0,0,0,0], false); row.author = m.author; row.note = m.note || '';
      const at = b.presets.findIndex (x => x.name === m.name); if (at >= 0) b.presets[at] = row; else b.presets.push (row);
      curMeta = Object.assign ({}, m, { bank }); return r (row.path); }
    if (n === 'updatePresetMeta') { let m = {}; try { m = JSON.parse (a[1]); } catch (e) {} for (const b of window.__CAT.banks) for (const p of b.presets) if (p.path === String(a[0])) Object.assign(p, m); return r('ok'); }
    if (n === 'getFavourites') return r ('{}');
    if (n === 'getVocab')      return r ('');
    if (/^(setFavourite|setVocab|createBank|renameBank|deleteBank|exportBank|exportPreset|exportPresetFile|importPack)$/.test (n)) return r ('ok');
    if (/getPresets/i.test (n)) return r ('[]');
    if (/Json|JSON|Names|Lanes|Shapes|Envs|Mod$/.test (n)) return r ('');
    r (0); });
  window.Juce = { getSliderState: mk, getToggleState: mk, getComboBoxState: mk, getNativeFunction: nf,
    backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
  window.prompt = () => { window.__panels.push ('prompt'); return null; };
  window.confirm = () => { window.__panels.push ('confirm'); return false; };
  window.alert = () => { window.__panels.push ('alert'); };
};

(async () => {
  console.log('== vocab repro · STEAL=' + STEAL);
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 1 });
  const errors = []; p.on('pageerror', e => errors.push(String(e.message || e).slice(0, 160)));
  await p.evaluateOnNewDocument(STUB, STEAL);
  /* the control: the pre-fb631 blur→destroy, re-installed from outside */
  if (MUT === 'blurkill') await p.evaluateOnNewDocument(() => { document.addEventListener('focusout', e => { const t = e.target; if (t && t.classList && t.classList.contains('tp-in') && t.placeholder && /^New (style|type)$/.test(t.placeholder) && !t.value) setTimeout(() => t.remove(), 0); }, true); });
  await p.goto('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  const wait = (ms) => new Promise(r => setTimeout(r, ms));
  await wait(1600);
  await p.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) { sp.classList.remove('hidden'); sp.style.display = 'block'; } window.dispatchEvent(new Event('resize')); });
  await wait(2000);
  const calls = (fn) => p.evaluate(fn => window.__gateCalls.filter(c => c.fn === fn), fn);
  const log = () => p.evaluate(() => { const l = window.__log.slice(); window.__log.length = 0; return l; });
  const S = () => p.evaluate(() => ({ types: window.__tiPresets.state.types.slice(), styles: window.__tiPresets.state.styles.slice(), sel: window.__tiPresets.state.sel }));

  // ───── A. SAVE SHEET inline "+" ─────
  console.log('\n--- A. save sheet: + on Style, type "Gritty", Enter');
  await p.click('#preset-save-btn'); await wait(120);
  await p.evaluate(() => document.querySelector('#tp-ctx .pi[data-a=saveas]').click()); await wait(200);
  console.log(' sheet on:', await p.evaluate(() => document.getElementById('tp-sheet').classList.contains('on')), ' active:', await p.evaluate(() => document.activeElement && (document.activeElement.id || document.activeElement.tagName)));
  console.log(' style chips before:', await p.evaluate(() => [...document.querySelectorAll('#tp-sv-style .tp-chip')].map(e => e.textContent.trim() + (e.classList.contains('active') ? '*' : '')).join(' ')));
  await p.click('#tp-sv-style .tp-chip.add'); await wait(10);
  console.log(' t+10ms  input present:', await p.evaluate(() => !!document.querySelector('#tp-sv-style input')), ' active:', await p.evaluate(() => document.activeElement && (document.activeElement.id || document.activeElement.tagName + '.' + document.activeElement.className)));
  await wait(80);
  console.log(' t+90ms  input present:', await p.evaluate(() => !!document.querySelector('#tp-sv-style input')), ' active:', await p.evaluate(() => document.activeElement && (document.activeElement.id || document.activeElement.tagName + '.' + document.activeElement.className)), ' log:', JSON.stringify(await log()));
  const survived = await p.evaluate(() => !!document.querySelector('#tp-sheet input.tp-in[placeholder="New style"]'));
  console.log((survived ? '  PASS  ' : '  FAIL  ') + '[1] THE INLINE "+" FIELD SURVIVES THE FOCUS STEAL — it is still in the sheet 90 ms after the C++ took keyboard focus' + (MUT ? '   (control: ' + MUT + ')' : ''));
  process.exitCode = survived ? 0 : 1;
  const present = await p.evaluate(() => !!document.querySelector('#tp-sv-style input'));
  if (present) {
    await p.keyboard.type('Gritty'); await wait(30);
    console.log(' typed value:', await p.evaluate(() => { const i = document.querySelector('#tp-sv-style input'); return i ? i.value : '(input gone)'; }));
    await p.keyboard.press('Enter'); await wait(120);
  } else {
    console.log(' (input vanished before typing — clicking + again and typing anyway)');
    await p.keyboard.type('Gritty'); await wait(30);
    await p.keyboard.press('Enter'); await wait(120);
  }
  console.log(' style chips after:', await p.evaluate(() => [...document.querySelectorAll('#tp-sv-style .tp-chip')].map(e => e.textContent.trim() + (e.classList.contains('active') ? '*' : '')).join(' ')));
  console.log(' input still present:', await p.evaluate(() => !!document.querySelector('#tp-sv-style input')));
  console.log(' S.styles:', (await S()).styles.join(','));
  console.log(' setVocab calls:', JSON.stringify(await calls('setVocab')));
  console.log(' toast:', await p.evaluate(() => document.getElementById('tp-toast').textContent));
  console.log(' log:', JSON.stringify(await log()));
  // blur-on-removal check in this engine
  const blurOnRemove = await p.evaluate(() => new Promise(res => { const i = document.createElement('input'); document.body.appendChild(i); i.focus(); let fired = false; i.onblur = () => { fired = true; }; i.remove(); setTimeout(() => res(fired), 50); }));
  console.log(' [engine] blur fires when focused input is removed:', blurOnRemove);

  // A2: + on Type then click + on Style (does the second click land?)
  console.log('\n--- A2. save sheet: + on Type (leave empty), then click + on Style');
  await p.click('#tp-sv-type .tp-chip.add'); await wait(80);
  console.log(' type input present:', await p.evaluate(() => !!document.querySelector('#tp-sv-type input')));
  await p.click('#tp-sv-style .tp-chip.add'); await wait(80);
  console.log(' after clicking style +: type input:', await p.evaluate(() => !!document.querySelector('#tp-sv-type input')), ' style input:', await p.evaluate(() => !!document.querySelector('#tp-sv-style input')));
  await p.keyboard.press('Escape'); await wait(50);
  console.log(' sheet still on after Escape in inline input:', await p.evaluate(() => document.getElementById('tp-sheet').classList.contains('on')));
  await p.evaluate(() => { const c = document.getElementById('tp-sh-cancel'); if (c) c.click(); }); await wait(100);

  // ───── B. INSPECTOR "+" (addVocab sheet) ─────
  console.log('\n--- B. inspector: select Slatt (user, Bass · Dark,Dirty), + on Style, type "Gritty", Enter');
  await p.click('#preset-name'); await wait(140); await p.click('#tp-q-browse'); await wait(260);
  await p.evaluate(() => { const r = [...document.querySelectorAll('#tp-rows .tp-row')].find(x => x.querySelector('.c-nm .t').textContent.trim().endsWith('Slatt')); r.click(); }); await wait(140);
  console.log(' sel:', (await S()).sel, ' strip before:', await p.evaluate(() => [...document.querySelectorAll('#tp-chips-style .tp-chip')].map(e => e.textContent.trim() + (e.classList.contains('active') ? '*' : '')).join(' ')));
  await p.click('#tp-chips-style .tp-chip.add'); await wait(80);
  console.log(' sheet on:', await p.evaluate(() => document.getElementById('tp-sheet').classList.contains('on')), ' title:', await p.evaluate(() => (document.querySelector('#tp-sheet .tp-hd') || {}).textContent), ' active:', await p.evaluate(() => document.activeElement && (document.activeElement.id || document.activeElement.tagName)), ' log:', JSON.stringify(await log()));
  await p.evaluate(() => { const i = document.getElementById('tp-vn'); if (i) i.focus(); });
  await p.keyboard.type('Gritty'); await wait(30);
  console.log(' typed value:', await p.evaluate(() => (document.getElementById('tp-vn') || {}).value));
  await p.keyboard.press('Enter'); await wait(200);
  console.log(' sheet on after Enter:', await p.evaluate(() => document.getElementById('tp-sheet').classList.contains('on')));
  console.log(' S.styles:', (await S()).styles.join(','));
  console.log(' Slatt styles:', await p.evaluate(() => window.__tiPresets.state.presets[window.__tiPresets.state.sel].styles.join(',')));
  console.log(' strip after:', await p.evaluate(() => [...document.querySelectorAll('#tp-chips-style .tp-chip')].map(e => e.textContent.trim() + (e.classList.contains('active') ? '*' : '')).join(' ')));
  console.log(' updatePresetMeta:', JSON.stringify(await calls('updatePresetMeta')));
  console.log(' toast:', await p.evaluate(() => document.getElementById('tp-toast').textContent));

  // B2: inspector type "+", typing the type it ALREADY has
  console.log('\n--- B2. inspector: + on Type, type "bass" (Slatt already IS Bass), Enter');
  console.log(' Slatt type before:', await p.evaluate(() => window.__tiPresets.state.presets[window.__tiPresets.state.sel].type));
  await p.click('#tp-chips-type .tp-chip.add'); await wait(80);
  await p.evaluate(() => { const i = document.getElementById('tp-vn'); if (i) i.focus(); });
  await p.keyboard.type('bass'); await p.keyboard.press('Enter'); await wait(200);
  console.log(' Slatt type after:', JSON.stringify(await p.evaluate(() => window.__tiPresets.state.presets[window.__tiPresets.state.sel].type)));
  console.log(' type strip:', await p.evaluate(() => [...document.querySelectorAll('#tp-chips-type .tp-chip')].map(e => e.textContent.trim() + (e.classList.contains('active') ? '*' : '')).join(' ')));

  // ───── C. SEARCH box styling ─────
  console.log('\n--- C. search box');
  const sq = async () => p.evaluate(() => { const i = document.getElementById('tp-search'), w = i.parentElement; const cs = getComputedStyle(i), b = getComputedStyle(w, '::before'), a = getComputedStyle(w, '::after');
    const ir = i.getBoundingClientRect(), wr = w.getBoundingClientRect();
    return { focused: document.activeElement === i, border: cs.border, outline: cs.outline, boxShadow: cs.boxShadow, background: cs.backgroundColor, paddingLeft: cs.paddingLeft, inputRect: [ir.left|0, ir.top|0, ir.width|0, ir.height|0].join('x'), wrapRect: [wr.left|0, wr.top|0, wr.width|0, wr.height|0].join('x'),
      before: { left: b.left, top: b.top, width: b.width, height: b.height, border: b.border }, after: { left: a.left, top: a.top, width: a.width, height: a.height } }; });
  console.log(' unfocused:', JSON.stringify(await sq()));
  await p.click('#tp-search'); await p.keyboard.type('gl'); await wait(60);
  console.log(' focused+typing:', JSON.stringify(await sq()));
  await p.evaluate(() => { const s = document.getElementById('tp-search'); s.value = ''; s.dispatchEvent(new Event('input')); s.blur(); });

  // ───── D. CHIP OVERFLOW ─────
  console.log('\n--- D. chip overflow: push a 20-char word and measure');
  const ov = await p.evaluate(() => { const st = window.__tiPresets.state; st.styles.push('WWWWWWWWWWWWWWWWWWWW'); st.styles.push('Supercalifragilistic');
    // rebuild via a rescan-free path: call the same renderer the page uses
    const strip = document.querySelector('#tp-chips-style .strip');
    // trigger buildChips through a re-select
    const r = [...document.querySelectorAll('#tp-rows .tp-row')].find(x => x.querySelector('.c-nm .t').textContent.trim().endsWith('Monte Cristo')); r.click();
    const chips = [...document.querySelectorAll('#tp-chips-style .tp-chip')].map(e => ({ t: e.textContent.trim(), w: e.getBoundingClientRect().width|0, ow: e.offsetWidth, sw: e.scrollWidth, cs: getComputedStyle(e).overflow, ws: getComputedStyle(e).whiteSpace, to: getComputedStyle(e).textOverflow, minw: getComputedStyle(e).minWidth, maxw: getComputedStyle(e).maxWidth }));
    const sc = document.getElementById('tp-iscroll'), ins = document.querySelector('#tp-b .insp');
    return { stripClient: strip.clientWidth, stripScroll: strip.scrollWidth, iscrollClient: sc.clientWidth, iscrollScroll: sc.scrollWidth, iscrollOverflowX: getComputedStyle(sc).overflowX, inspWidth: ins.clientWidth, inspOverflow: getComputedStyle(ins).overflow, long: chips.filter(c => c.t.length >= 20) }; });
  console.log(' ', JSON.stringify(ov));
  // and in the save sheet's .chp
  await p.click('#preset-save-btn'); await wait(120);
  await p.evaluate(() => document.querySelector('#tp-ctx .pi[data-a=saveas]').click()); await wait(200);
  const ov2 = await p.evaluate(() => { const box = document.getElementById('tp-sv-style'), card = document.querySelector('#tp-sheet .tp-card'), body = document.querySelector('#tp-sheet .tp-sbody');
    const chips = [...box.querySelectorAll('.tp-chip')].filter(e => e.textContent.trim().length >= 20).map(e => ({ t: e.textContent.trim(), w: e.getBoundingClientRect().width|0, sw: e.scrollWidth }));
    return { boxClient: box.clientWidth, boxScroll: box.scrollWidth, bodyClient: body.clientWidth, bodyScroll: body.scrollWidth, cardW: card.clientWidth, long: chips }; });
  console.log('  save sheet:', JSON.stringify(ov2));
  await p.screenshot({ path: '/private/tmp/claude-501/-Users-macshooter/907270d8-8f90-41f2-aeb0-e49397ad03b7/scratchpad/save_sheet_overflow.png' });
  await p.evaluate(() => { const c = document.getElementById('tp-sh-cancel'); if (c) c.click(); }); await wait(100);
  await p.click('#tp-search'); await p.keyboard.type('gl'); await wait(60);
  await p.screenshot({ path: '/private/tmp/claude-501/-Users-macshooter/907270d8-8f90-41f2-aeb0-e49397ad03b7/scratchpad/search_focus.png', clip: { x: 0, y: 44, width: 420, height: 40 } });
  await p.screenshot({ path: '/private/tmp/claude-501/-Users-macshooter/907270d8-8f90-41f2-aeb0-e49397ad03b7/scratchpad/browser_full.png' });

  console.log('\n page errors:', errors.join(' | ') || 'none');
  await b.close();
})().catch(e => { console.error('REPRO CRASH', e); process.exit(1); });
