// ══════════════════════════════════════════════════════════════════════════════════════════════
//  modal_samples_gate.js — tp114: THE MODAL ENGINE'S FAMILY MENU HAS A "SAMPLES" DOOR, AND IT IS
//  THE SAMPLE BROWSER WE ALREADY HAVE.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/modal_samples_gate.js [page.html]
//
//  Max: "We gotta find a way for the Modal engine to use our sample browser. The Harmonic engine
//  already has that tree … where you choose Wavetable and it lets us choose from the Wavetable
//  browser. For the Modal engine add a tree entry that says Samples, and the browser is the sample
//  browser … We need to be able to add a folder, so we need to get to the browser."
//
//  ♻️ RECYCLED, NOT REBUILT: the entry is one more <option> in the SAME header <select> the nine
//  resonator families live in (.md-fam-select — the Harmonic family chip's twin), and it opens
//  window.openSampleBrowser — the Sample/Granular/Resynth browser (factory one-shots + user folders +
//  ＋ Import file-or-folder). A pick calls loadSampleByPath, the drop's own C++ entry point.
//
//  🚨 THE VIEWPORT IS THE SHIPPED ONE (fb454's law): 820 × 656.
//
//  THE BARS
//   0  THE PANEL LAID OUT and osc A is on Modal (engine-modal, the family chip shown)
//   1  the family menu lists the nine resonators BY THEIR CHIP NAMES (Ivory … Skin — the menu used
//      to say Grand … Bells while the chip said Ivory … Glass) and then Samples, last
//   2  choosing Samples opens THE SAMPLE BROWSER: one .tpb-panel whose folders are the factory
//      categories + the user's folder, fed by scanSampleFactory + listSampleImports
//   3  Samples is a DOOR, not a family: MODAL_FAMILY is not written, the chip still names the
//      resonator (Glass), the <select> reads 7 again
//   4  🚨 a pick loads into THIS osc through loadSampleByPath('a', <abs path>) — the same native the
//      Sample engine's browser calls — and the ENGINE param is NOT written (it stays Modal)
//   5  ADD FOLDER: the browser's ＋ calls pickSampleImport('a'); when the native reports the import
//      (onSampleImportsChanged) the browser reopens FOR OSC A with the new folder in it, and a pick
//      from that folder lands on A
//   6  E–H: the same door on osc E (bank B, the pool clone) loads into 'e'
//   7  DRAG-AND-DROP STILL WORKS on the Modal display: a file dropped on osc A's display goes to
//      loadSampleForOsc('a', …) and the engine stays Modal
//   8  no page errors
//
//  MUTATION CONTROL (fb421 — a gate that has never failed has never been tested):
//    MODAL_SAMPLES_MUTATE=1  the Samples branch removed from the change handler → [2] [3] [4] [5] [6] red
//    MODAL_SAMPLES_MUTATE=2  the Samples <option> removed from the markup      → [1]–[6] red
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE = process.argv[2] || path.join(ROOT, 'Source/ui/public/index.html');
const MUT = +(process.env.MODAL_SAMPLES_MUTATE || 0);
const VW = 820, VH = 656;
let pass = 0, fail = 0;
const chk = (ok, l, d) => { ok ? pass++ : fail++; console.log('  ' + (ok ? 'ok  ' : 'FAIL') + '  ' + l + (d !== undefined ? '   ' + (typeof d === 'string' ? d : JSON.stringify(d)) : '')); };

function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t, want) => { const n = src.split(f).length - 1;
    if (n !== (want || 1)) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); }
    src = src.split(f).join(t); console.log('  (mutation ' + MUT + ' landed: ' + n + ' site(s))'); };
  if (MUT === 1) sub("if (sel.value === 'samples') { paint ();", "if (false) { paint ();");
  if (MUT === 2) sub('<option value="samples">Samples</option>', '', 4);
  const p = path.join(os.tmpdir(), 'modal_samples_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}

// a recording bridge (the fb563 stub): every param write lands in __emits, every native call in __natives
const STUB = (cfg) => {
  window.__emits = []; window.__natives = [];
  window.__imp = { files: [], folders: [ { name: 'My Kit', path: '/Users/me/My Kit', kind: 'user',
                   items: [ { name: 'Tom', path: '/Users/me/My Kit/Tom.wav', rel: '' } ] } ] };
  const FACT = { path: '/Library/Terrain/Samples', cats: { Bell: [ 'Kalimba_C4.wav', 'Glass_Hit.wav' ], FX: [ 'Snap.wav' ] } };
  const CH = cfg.choice; const states = new Map();
  const mk = (name) => { const n = CH[name] || 0;
    const props = n ? { start:0, end:n-1, skew:1, name, label:'', numSteps:n, interval:1, parameterIndex:states.size }
                    : { start:0, end:1, skew:1, name, label:'', numSteps:100, interval:0, parameterIndex:states.size };
    const st = { name, norm:0, properties:props,
      getScaledValue(){ return n ? Math.round(st.norm*(n-1)) : st.norm; },
      setScaledValue(v){ st.norm = n ? v/(n-1) : v; },
      getNormalisedValue(){ return st.norm; },
      setNormalisedValue(v){ st.norm = n ? Math.round(v*(n-1))/(n-1) : v;
        window.__emits.push({ name, norm:+(+v).toFixed(6) }); (st.__ls||[]).forEach(f => { try { f(); } catch(e){} }); },
      valueChangedEvent:{ addListener(f){ (st.__ls = st.__ls || []).push(f); return {remove(){}}; }, removeListener(){} },
      propertiesChangedEvent:{ addListener(){ return {remove(){}}; }, removeListener(){} },
      getChoiceIndex(){ return Math.round(st.norm*(n-1)); }, setChoiceIndex(i){ st.norm = i/(n-1); },
      getValue:()=>false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} };
    Object.defineProperty(st, 'scaledValue', { get(){ return st.getScaledValue(); } });   // the front output slider reads the JUCE property form
    return st; };
  const get = (name) => { if (! states.has(name)) states.set(name, mk(name)); return states.get(name); };
  window.__stubState = get;
  const nativeFn = (nm) => (...a) => new Promise((r) => { window.__natives.push({ fn:nm, args:a.map(String) });
    if (nm === 'scanSampleFactory') return r(JSON.stringify(FACT));
    if (nm === 'listSampleImports') return r(JSON.stringify(window.__imp));
    if (/getPresets/i.test(nm)) return r('[]'); if (/getSynthMod$/.test(nm)) return r('[]');
    if (/^getSynParam$/.test(nm)) return r(String(get(String(a[0])).getNormalisedValue()));
    if (/^load/.test(nm)) return r('ok');
    if (/Json|JSON/.test(nm)) return r('{}'); r(0); });
  window.Juce = { getSliderState:get, getToggleState:get, getComboBoxState:get, getNativeFunction:nativeFn,
                  backend:{ addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable:true,
    get(){ return held; }, set(v){ held = Object.assign({}, v||{}, { getNativeFunction:mine.getNativeFunction, getSliderState:mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend:window.Juce.backend, initialisationData:{ vendor:'', pluginName:'', pluginVersion:'',
    __juce__sliders:[], __juce__toggles:[], __juce__comboBoxes:[], __juce__functions:[] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
};

const HELPERS = () => {
  window.__bPanes = () => [...document.querySelectorAll('.tpb-pane')];
  window.__bRows = (pane) => [...pane.children].map(d => ({ el:d, name:(d.querySelector('span[style*="flex:1"]')||d).textContent.trim() }));
  window.__bCats = () => { const p = window.__bPanes(); return p.length < 2 ? null : window.__bRows(p[0]).map(r => r.name); };
  window.__bPick = (cat, name) => { const p = window.__bPanes(); if (p.length < 2) return 'no browser';
    const c = window.__bRows(p[0]).find(r => r.name === cat); if (! c) return 'no cat ' + cat; c.el.click();
    const it = window.__bRows(window.__bPanes()[1]).find(r => r.name === name); if (! it) return 'no item ' + name; it.el.click(); return 'picked'; };
  window.__bClose = () => { try { if (window.__tpbClose) window.__tpbClose(); } catch(e){} document.querySelectorAll('.tpb-panel').forEach(p => p.remove()); };
  window.__famSel = (o) => document.querySelector('#syn-panel .md-fam-select[data-oscl="' + o + '"]');
  window.__choose = (o, v) => { const s = window.__famSel(o); if (! s) return 'no select'; s.value = v;
    s.dispatchEvent(new Event('change', { bubbles:true })); return 'changed'; };
  window.__calls = (fn) => window.__natives.filter(n => n.fn === fn).map(n => n.args);
  window.__writes = (re) => window.__emits.filter(e => re.test(e.name))   // A–D land in the stub; E–H go out through the pool's setSynParam
    .concat(window.__natives.filter(n => n.fn === 'setSynParam' && re.test(n.args[0])).map(n => ({ name:n.args[0], norm:+n.args[1] })));
};

(async () => {
  const P = mutatedPage();
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:VW, height:VH, deviceScaleFactor:2 });
  const errs = []; pg.on('pageerror', e => errs.push(String((e && e.stack) || e).slice(0, 400)));
  const choice = {};
  'ABCDEFGH'.split('').forEach(o => { choice['SYN_OSC_' + o + '_ENGINE'] = 12; choice['SYN_OSC_' + o + '_MODAL_FAMILY'] = 9;
                                      choice['SYN_OSC_' + o + '_MODAL_FORM'] = 5; });
  await pg.evaluateOnNewDocument(STUB, { choice }); await pg.evaluateOnNewDocument(HELPERS);
  await pg.goto('file://' + P, { waitUntil:'load', timeout:60000 }); await new Promise(r => setTimeout(r, 2400));
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late');
    document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); if (sp) sp.style.display = 'block';
    try { document.getElementById('syn-btn').click(); } catch (e) {}
    window.dispatchEvent(new Event('resize')); });
  await new Promise(r => setTimeout(r, 1500));
  // osc A and E on Modal (engine 6 of 12), family Glass (7) — through the params, so the page's own engine sync runs
  //  (window.Juce.getSliderState, not the stub directly: E–H ride the page's pool relay, tp20)
  await pg.evaluate(() => { ['A','E'].forEach(o => { window.Juce.getSliderState('SYN_OSC_' + o + '_ENGINE').setNormalisedValue(6/11);
                                                      window.Juce.getSliderState('SYN_OSC_' + o + '_MODAL_FAMILY').setNormalisedValue(7/8); }); });
  await new Promise(r => setTimeout(r, 900));

  console.log('\n══ modal_samples_gate — tp114 ══  ' + P + '\n');
  // [0]
  const s0 = await pg.evaluate(() => { const d = document.getElementById('osc-a-device'); const w = d && d.querySelector('.md-fam-wrap:not(.md-form)');
    const r = d ? d.getBoundingClientRect() : { width:0 };
    return { w:r.width, modal:!!(d && d.classList.contains('engine-modal')), chip:w ? getComputedStyle(w).display : null,
             name:(document.getElementById('osc-a-modalfam-display')||{}).textContent }; });
  chk(s0.w > 100 && s0.modal && s0.chip && s0.chip !== 'none', '0  the panel laid out; osc A is on Modal with its family chip shown', s0);
  // [1]
  const s1 = await pg.evaluate(() => { const s = window.__famSel('a'); return s ? [...s.options].map(o => o.textContent + '=' + o.value) : null; });
  const want1 = ['Ivory=0','Wire=1','Silk=2','Air=3','Rasp=4','Blaze=5','Timber=6','Glass=7','Skin=8','Samples=samples'];
  chk(JSON.stringify(s1) === JSON.stringify(want1), '1  the family menu: the nine resonators by their chip names, then Samples', s1);
  // [2] [3]
  const s2 = await pg.evaluate(async () => { window.__natives.length = 0; window.__emits.length = 0; window.__bClose();
    const r = window.__choose('a', 'samples'); await new Promise(q => setTimeout(q, 250));
    return { r, panels:document.querySelectorAll('.tpb-panel').length, cats:window.__bCats(),
             scans:window.__calls('scanSampleFactory').length, lists:window.__calls('listSampleImports').length,
             fam:window.__writes(/MODAL_FAMILY/), chip:document.getElementById('osc-a-modalfam-display').textContent, sel:window.__famSel('a').value,
             plus:!!document.querySelector('.tpb-panel [title="Import Sample"]') }; });
  chk(s2.panels === 1 && JSON.stringify(s2.cats) === JSON.stringify(['All','Bell','FX','My Kit']) && s2.scans >= 1 && s2.lists >= 1,
      '2  Samples opens THE SAMPLE BROWSER — factory categories + the user folder, fed by scanSampleFactory + listSampleImports', s2);
  chk(s2.panels === 1 && s2.fam.length === 0 && s2.chip === 'Glass' && s2.sel === '7',
      '3  Samples is a door: MODAL_FAMILY untouched, the chip still names the resonator, the select reads 7 again', { fam:s2.fam, chip:s2.chip, sel:s2.sel });
  // [4]
  const s4 = await pg.evaluate(async () => { window.__natives.length = 0; window.__emits.length = 0;
    const r = window.__bPick('Bell', 'Kalimba C4'); await new Promise(q => setTimeout(q, 100));
    return { r, loads:window.__calls('loadSampleByPath'), eng:window.__writes(/_ENGINE$/), fam:window.__writes(/MODAL_FAMILY/) }; });
  chk(s4.r === 'picked' && JSON.stringify(s4.loads) === JSON.stringify([['a','/Library/Terrain/Samples/Bell/Kalimba_C4.wav']]) && s4.eng.length === 0 && s4.fam.length === 0,
      '4  a pick → loadSampleByPath("a", <abs path>) — the drop’s own entry point — and the engine stays Modal', s4);
  // [5] add folder
  const s5 = await pg.evaluate(async () => { window.__bClose(); window.__natives.length = 0;
    window.__choose('a', 'samples'); await new Promise(q => setTimeout(q, 250));
    const plus = document.querySelector('.tpb-panel [title="Import Sample"]'); if (plus) plus.click();
    const picks = window.__calls('pickSampleImport');
    // the native adds the folder and reports it — exactly what pickSampleImport's completion does
    window.__imp.folders.push({ name:'Found Sounds', path:'/Users/me/Found Sounds', kind:'user',
                                items:[ { name:'Bottle', path:'/Users/me/Found Sounds/Bottle.wav', rel:'' } ] });
    window.__natives.length = 0; window.onSampleImportsChanged('a'); await new Promise(q => setTimeout(q, 250));
    const cats = window.__bCats(); const r = window.__bPick('Found Sounds', 'Bottle'); await new Promise(q => setTimeout(q, 100));
    return { plus:!!plus, picks, cats, r, loads:window.__calls('loadSampleByPath') }; });
  chk(s5.plus && JSON.stringify(s5.picks) === JSON.stringify([['a']]) && s5.cats && s5.cats.indexOf('Found Sounds') >= 0
      && JSON.stringify(s5.loads) === JSON.stringify([['a','/Users/me/Found Sounds/Bottle.wav']]),
      '5  ADD FOLDER: ＋ → pickSampleImport("a"); the import reopens the browser for A with the folder; its pick lands on A', s5);
  // [6] E
  const s6 = await pg.evaluate(async () => { window.__bClose(); window.__natives.length = 0; window.__emits.length = 0;
    const r = window.__choose('e', 'samples'); await new Promise(q => setTimeout(q, 250));
    const p = window.__bPick('FX', 'Snap'); await new Promise(q => setTimeout(q, 100));
    return { r, p, loads:window.__calls('loadSampleByPath'), eng:window.__writes(/_ENGINE$/), fam:window.__writes(/MODAL_FAMILY/),
             sel:(window.__famSel('e')||{}).value }; });
  chk(s6.r === 'changed' && JSON.stringify(s6.loads) === JSON.stringify([['e','/Library/Terrain/Samples/FX/Snap.wav']]) && s6.eng.length === 0 && s6.fam.length === 0 && s6.sel === '7',
      '6  E–H: the same door on osc E (bank B) loads into "e"', s6);
  // [7] drop
  const s7 = await pg.evaluate(async () => { window.__bClose(); window.__natives.length = 0; window.__emits.length = 0;
    const d = document.querySelector('#osc-a-device .sample-view .samp-disp'); if (! d) return { err:'no samp-disp' };
    const view = d.closest('.sample-view'); view.classList.remove('loaded');
    const dt = new DataTransfer(); dt.items.add(new File([new Uint8Array([82,73,70,70,0,0,0,0])], 'Clap.wav', { type:'audio/wav' }));
    d.dispatchEvent(new DragEvent('drop', { bubbles:true, cancelable:true, dataTransfer:dt }));
    await new Promise(q => setTimeout(q, 400));
    return { loads:window.__calls('loadSampleForOsc').map(a => a.slice(0, 2)), eng:window.__writes(/_ENGINE$/) }; });
  chk(JSON.stringify(s7.loads) === JSON.stringify([['a','Clap.wav']]) && s7.eng && s7.eng.length === 0,
      '7  drag-and-drop onto the Modal display still loads into A (loadSampleForOsc) and keeps Modal', s7);
  chk(errs.length === 0, '8  no page errors', errs.slice(0, 4));
  console.log('\n  ' + pass + ' passed, ' + fail + ' failed\n');
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error('HARNESS ERROR', e); process.exit(2); });
