// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb635_state_gate.js — fb635: WHAT A PRESET LOAD TURNS ON, THE PAGE SHOWS ON (noise · filter routing).
//
//    node Tests/fb635_state_gate.js           # from plugins/Terrain (NODE_PATH=Tests/node_modules)
//
//  Max: "the noise and the filters look like they're turned off, but they're really on ... I load up a preset
//  and it's turned off and the settings are gone, but it doesn't show that audibly ... every time I turn the
//  filter on and turn it off it lets me know that the filter was actually on this whole time."
//
//  THE MECHANISM. Two modules read their state from getSynParam ONCE, at boot, and have no relays: the noise
//  (on · type · level · scan · pan · play mode · width) and the filter back panel's fst (the A/B/C/D/S/N
//  routing pills — which is what "on" means for a Terrain filter — plus poles, drive type, spread). A preset
//  load moved the parameters and the DSP; nothing re-read them, so the page kept the previous patch.
//
//  THE BARS (the load goes through the page's real door: the parameters move, then window.onPatchLoaded)
//   1  THE NOISE COMES ON — a patch with noise ON and another type: the pill is lit and the type name changed
//   2  THE FILTER ROUTING COMES ON — a patch routing Osc A and the Noise into filter 1: those two pills are lit,
//      the others dark
//   3  AND GOES OFF — the next patch turns both off: the noise pill is dark and every routing pill is dark
//      (a read, not a latch)
//
//  MUTATION CONTROL
//    SG_MUT=nohook   repull() no longer calls the two re-reads (the fb634 page)   → [1] [2] RED
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require(require('path').join(__dirname, 'node_modules', 'puppeteer-core'));
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE0 = path.join(ROOT, 'Source/ui/public/index.html');
const MUT = process.env.SG_MUT || '';
let pass = 0, fail = 0;
const chk = (ok, l, d) => { ok ? pass++ : fail++; console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${l}\n        ${d}`); };
const sleep = ms => new Promise(r => setTimeout(r, ms));
function page () {
  if (! MUT) return PAGE0;
  let s = fs.readFileSync(PAGE0, 'utf8');
  const a = s.indexOf("    T(() => window.__tiNoiseResync && window.__tiNoiseResync());"), b = s.indexOf("\n", s.indexOf("}, 450));", a));
  if (a < 0 || b < 0) { console.log('  MUTATION anchor missing'); process.exit(2); }
  s = s.slice(0, a) + s.slice(b + 1);
  const f = path.join(os.tmpdir(), 'fb635_state_' + MUT + '.html'); fs.writeFileSync(f, s); return f;
}
const WARP_N = (() => {   // the warp lane's cardinality, read from the C++ (all_menus.js's own extraction)
  const s = fs.readFileSync(path.join(ROOT, 'Source/PluginProcessor.cpp'), 'utf8');
  const m = /for \(int i = w\.size\(\); i < (\d+); \+\+i\) w\.add \("Reserved "/.exec(s);
  if (! m) throw new Error('warp cardinality not found in PluginProcessor.cpp');
  return +m[1];
})();
const STUB = (cfg) => {
  window.__emits = []; window.__natives = [];
  const CH = cfg.choice; const states = new Map();
  const mk = (name) => { const n = CH[name] || 0;
    const props = n ? { start:0, end:n-1, skew:1, name, label:'', numSteps:n, interval:1, parameterIndex:states.size }
                    : { start:0, end:1, skew:1, name, label:'', numSteps:100, interval:0, parameterIndex:states.size };
    const st = { get scaledValue(){ return st.norm; },   // the front page reads .scaledValue as a PROPERTY (state.outputGain) — without it init() throws
      name, norm:(name === 'SYN_BEND_RANGE' ? 2/24 : 0), properties:props,   // the bend range's registered default (2 st) — the only non-zero default the bars read back
      getScaledValue(){ return n ? Math.round(st.norm*(n-1)) : st.norm; },
      setScaledValue(v){ st.norm = n ? v/(n-1) : v; },
      getNormalisedValue(){ return st.norm; },
      setNormalisedValue(v){ st.norm = n ? Math.round(v*(n-1))/(n-1) : v;
        window.__emits.push({ name, norm:+(+v).toFixed(6) }); (st.__ls||[]).forEach(f => { try { f(); } catch(e){} }); },
      valueChangedEvent:{ addListener(f){ (st.__ls = st.__ls || []).push(f); return {remove(){}}; }, removeListener(){} },
      propertiesChangedEvent:{ addListener(){ return {remove(){}}; }, removeListener(){} },
      getChoiceIndex(){ return Math.round(st.norm*(n-1)); }, setChoiceIndex(i){ st.norm = i/(n-1); },
      getValue:()=>false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} };
    return st; };
  const get = (name) => { if (! states.has(name)) states.set(name, mk(name)); return states.get(name); };
  window.__stubState = get;
  const nativeFn = (nm) => (...a) => new Promise((r) => { window.__natives.push({ fn:nm, args:a.map(String) });
    if (/getPresets/i.test(nm)) return r('[]'); if (/getSynthMod$/.test(nm)) return r('[]');
    if (/^getSynParam$/.test(nm)) return r(String(get(String(a[0])).getNormalisedValue()));   // the relay-free read-back, from the same stub state the writes land in
    if (/Json|JSON/.test(nm)) return r('{}'); r(0); });
  window.Juce = { getSliderState:get, getToggleState:get, getComboBoxState:get, getNativeFunction:nativeFn,
                  backend:{ addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable:true,
    get(){ return held; }, set(v){ held = Object.assign({}, v||{}, { getNativeFunction:mine.getNativeFunction, getSliderState:mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend:window.Juce.backend, initialisationData:{ vendor:'', pluginName:'', pluginVersion:'',
    __juce__sliders:[], __juce__toggles:[], __juce__comboBoxes:[], __juce__functions:[] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
  window.__errStacks = []; window.addEventListener('error', (e) => { try { window.__errStacks.push(String((e.error && e.error.stack) || e.message).slice(0, 400)); } catch(x){} });
};


(async () => {
  console.log('══ fb635 STATE GATE — a preset load turns on what it turns on ══   mutation: ' + (MUT || '(none)'));
  const P = page();
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width: 820, height: 656, deviceScaleFactor: 1 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  const choice = {}; ['A','B','C','D'].forEach(o => { choice['SYN_OSC_' + o + '_WARP_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_WARP2_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_ENGINE'] = 7; });
  await pg.evaluateOnNewDocument(STUB, { choice });
  await pg.goto('file://' + P, { waitUntil: 'load', timeout: 60000 }); await sleep(2400);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late');
    document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block';
    try { document.getElementById('syn-btn').click(); } catch (e) {} dispatchEvent(new Event('resize')); });
  await sleep(1800);   // past the filter panel's own 1.5 s boot retries — only a load can move it from here
  const read = () => pg.evaluate(() => {
    const nm = document.getElementById('noise-mod'), nn = document.getElementById('noise-name');
    const pills = [...document.querySelectorAll('#flt-src-row .flt-src')].map(p => p.classList.contains('act') ? 1 : 0);
    return { noiseOn: nm ? ! nm.classList.contains('noise-off') : null, type: nn ? nn.textContent.trim() : null, pills: pills.join('') };
  });
  const load = (vals) => pg.evaluate(async (vals) => {
    for (const k in vals) { const st = window.__stubState(k); st.norm = vals[k]; }   // the processor's state moves (no relay event: these modules have none)
    window.onPatchLoaded({ name: 'Gate', bank: 'User', author: '', type: '', styles: '', note: '', fv: 3 });
    await new Promise(r => setTimeout(r, 900));
  }, vals);
  const s0 = await read();
  await load({ SYN_NOISE_ON: 1, SYN_NOISE_TYPE: 0.5, SYN_OSC_A_F1MIX: 1, SYN_FILTER1_SRC_NOISE: 1 });
  const s1 = await read();
  chk(s0.noiseOn === false && s1.noiseOn === true && s1.type !== s0.type, '[1] THE NOISE COMES ON — the pill lights and the type follows the patch',
    `boot: on=${s0.noiseOn} "${s0.type}" → after the load: on=${s1.noiseOn} "${s1.type}"`);
  chk(s0.pills === '000000' && s1.pills === '100001', '[2] THE FILTER ROUTING COMES ON — Osc A and the Noise lit on filter 1, the rest dark',
    `boot ${s0.pills} → after the load ${s1.pills} (want 100001, pills A B C D S N)`);
  await load({ SYN_NOISE_ON: 0, SYN_OSC_A_F1MIX: 0, SYN_FILTER1_SRC_NOISE: 0 });
  const s2 = await read();
  chk(s2.noiseOn === false && s2.pills === '000000', '[3] AND GOES OFF — the next patch turns both off, and the page follows',
    `on=${s2.noiseOn} · pills ${s2.pills}`);
  if (errs.length) console.log('  page errors: ' + errs.slice(0, 3).join(' | '));
  console.log(`\n  ${pass} passed, ${fail} FAILED`); await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('  CRASH ' + (e && e.stack || e)); process.exit(2); });
