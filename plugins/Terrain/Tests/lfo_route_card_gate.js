// Reproduction: fb628 __tiLfoRouted gate after a route is REMOVED.
// Boots the real index.html (docked window, push-mode painter) with a STATEFUL matrix stub:
//   setSynthMod stores the JSON, getSynthMod returns it (snapshot at call time, like the C++),
//   and every setSynthMod kicks window.__tiModRestore() a tick later (the fb570 editor relay).
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path');
const ROOT = '/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/Terrain';
const PAGE = path.join(ROOT, 'Source/ui/public/index.html');
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
let pass = 0, fail = 0;
const chk = (ok, l, d) => { if (ok) { pass++; console.log('  ok    ' + l + (d ? '   ' + d : '')); } else { fail++; console.log('  FAIL  ' + l + (d ? '   ' + d : '')); } };

const WARP_N = (() => { const s = fs.readFileSync(path.join(ROOT, 'Source/PluginProcessor.cpp'), 'utf8');
  const m = /for \(int i = w\.size\(\); i < (\d+); \+\+i\) w\.add \("Reserved "/.exec(s); return m ? +m[1] : 8; })();

const STUB = (cfg) => {
  window.__emits = []; window.__natives = []; window.__matrix = '[]'; window.__setLog = []; window.__relay = true;
  const CH = cfg.choice; const states = new Map();
  const mk = (name) => { const n = CH[name] || 0;
    const props = n ? { start:0, end:n-1, skew:1, name, label:'', numSteps:n, interval:1, parameterIndex:states.size }
                    : { start:0, end:1, skew:1, name, label:'', numSteps:100, interval:0, parameterIndex:states.size };
    const st = { get scaledValue(){ return st.norm; }, name, norm:(name === 'SYN_BEND_RANGE' ? 2/24 : 0), properties:props,
      getScaledValue(){ return n ? Math.round(st.norm*(n-1)) : st.norm; }, setScaledValue(v){ st.norm = n ? v/(n-1) : v; },
      getNormalisedValue(){ return st.norm; },
      setNormalisedValue(v){ st.norm = n ? Math.round(v*(n-1))/(n-1) : v; window.__emits.push({ name, norm:+(+v).toFixed(6) }); (st.__ls||[]).forEach(f => { try { f(); } catch(e){} }); },
      valueChangedEvent:{ addListener(f){ (st.__ls = st.__ls || []).push(f); return {remove(){}}; }, removeListener(){} },
      propertiesChangedEvent:{ addListener(){ return {remove(){}}; }, removeListener(){} },
      getChoiceIndex(){ return Math.round(st.norm*(n-1)); }, setChoiceIndex(i){ st.norm = i/(n-1); },
      getValue:()=>false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} };
    return st; };
  const get = (name) => { if (! states.has(name)) states.set(name, mk(name)); return states.get(name); };
  window.__stubState = get;
  const nativeFn = (nm) => (...a) => new Promise((r) => { window.__natives.push({ fn:nm, args:a.map(String) });
    if (/getPresets/i.test(nm)) return r('[]');
    if (/^getSynthMod$/.test(nm)) { const snap = window.__matrix; return r(snap); }           // snapshot at call time, like the C++ message order
    if (/^setSynthMod$/.test(nm)) { window.__matrix = String(a[0]); window.__setLog.push(String(a[0]));
      if (window.__relay) setTimeout(() => { try { window.__tiModRestore && window.__tiModRestore(); } catch(e){} }, 0);   // the fb570 relay kick
      return r(0); }
    if (/^getSynParam$/.test(nm)) return r(String(get(String(a[0])).getNormalisedValue()));
    if (/Json|JSON/.test(nm)) return r('{}'); r(0); });
  window.Juce = { getSliderState:get, getToggleState:get, getComboBoxState:get, getNativeFunction:nativeFn,
                  backend:{ addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable:true,
    get(){ return held; }, set(v){ held = Object.assign({}, v||{}, { getNativeFunction:mine.getNativeFunction, getSliderState:mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend:window.Juce.backend, initialisationData:{ vendor:'', pluginName:'', pluginVersion:'', __juce__sliders:[], __juce__toggles:[], __juce__comboBoxes:[], __juce__functions:[] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
  window.__errStacks = []; window.addEventListener('error', (e) => { try { window.__errStacks.push(String((e.error && e.error.stack) || e.message).slice(0, 400)); } catch(x){} });
};

const HELPERS = () => {
  const raf = () => new Promise(r => requestAnimationFrame(() => r()));
  const Z = () => [0,0,0,0,0,0,0,0,0,0];
  window.__lp = {
    async push (ph, notes, lfoIdx) {   // one C++ frame for LFO (lfoIdx+1)
      window.__notesActive = notes ? 1 : 0; window.__notesActiveT = Date.now();
      const P = Z(); P[lfoIdx||0] = ph; const L = Z(); L[lfoIdx||0] = 0.5;
      window.__modViz([], L, P);
      if (window.__tiFrame) window.__tiFrame();
      await raf(); await raf();
    },
    head () {
      const fd = document.querySelector('#mod-engine #mv-fd'), pl = document.querySelector('#mod-engine #mv-ph');
      if (! fd || ! pl) return null;
      return { cx: +fd.getAttribute('cx'), x1: +pl.getAttribute('x1'), o: +getComputedStyle(fd).opacity, ol: +getComputedStyle(pl).opacity,
               idle: document.getElementById('mod-engine').classList.contains('mv-idle') };
    },
    async sweep (n, lfoIdx) {   // n frames with advancing phase; returns min/max cx, final head, routed flag
      let mn = 1e9, mx = -1e9, h = null;
      for (let i = 0; i < n; i++) { await window.__lp.push((i % 20) / 20, 1, lfoIdx); h = window.__lp.head(); mn = Math.min(mn, h.cx); mx = Math.max(mx, h.cx); }
      return { spread: mx - mn, end: h, routed: window.__tiLfoRouted ? window.__tiLfoRouted((lfoIdx||0) + 1) : 'absent', routes: window.__tiRoutes() };
    }
  };
};

async function boot (pg) {
  const choice = {}; ['A','B','C','D'].forEach(o => { choice['SYN_OSC_' + o + '_WARP_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_WARP2_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_ENGINE'] = 7; });
  await pg.evaluateOnNewDocument(STUB, { choice }); await pg.evaluateOnNewDocument(HELPERS);
  await pg.goto('file://' + PAGE + '?card=lfo', { waitUntil:'load', timeout:60000 }); await sleep(2400);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late');
    document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block';
    try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await sleep(900);
}

(async () => {
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:820, height:656, deviceScaleFactor:2 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  pg.on('console', m => { if (/REPRO/.test(m.text())) console.log('   ' + m.text()); });
  await boot(pg);


  console.log('\n== CARD MODE (__cardOnly=lfo, rAF painter, mirror prunes?) ==\n');
  const info = await pg.evaluate(() => ({ cardOnly: window.__cardOnly, hasRouted: typeof window.__tiLfoRouted, hasEngine: !!document.getElementById('mod-engine'), rafReg: typeof window.__tiFrameReg }));
  console.log('   ', JSON.stringify(info));
  // the C++ matrix holds LFO1 -> 300 (assigned in the docked window); the card is kicked to mirror it
  const s1 = await pg.evaluate(async () => { window.__matrix = '[{"s":0,"d":300,"v":0.5}]'; window.__tiModRestore(); await new Promise(r => setTimeout(r, 150));
    return { routed: window.__tiLfoRouted(1), routes: window.__tiRoutes() }; });
  chk(s1.routed === true, 'C1 card mirror picked up the docked route', JSON.stringify(s1));
  const s2 = await pg.evaluate(async () => window.__lp.sweep(30, 0));
  chk(!s2.end.idle && s2.spread > 5, 'C2 card animates while routed + notes', JSON.stringify({ spread:s2.spread, idle:s2.end.idle, o:s2.end.o }));
  // the docked window removes it: C++ matrix -> [], relay kicks the card
  const s3 = await pg.evaluate(async () => { window.__matrix = '[]'; window.__tiModRestore(); await new Promise(r => setTimeout(r, 150));
    return { routed: window.__tiLfoRouted(1), routes: window.__tiRoutes() }; });
  chk(s3.routed === false, 'C3 card mirror PRUNED the removed route (live map, card-only delete pass)', JSON.stringify(s3));
  const s4 = await pg.evaluate(async () => window.__lp.sweep(30, 0));
  chk(s4.end.idle && s4.spread < 0.01, 'C4 card parks after the docked removal', JSON.stringify({ spread:s4.spread, idle:s4.end.idle, o:s4.end.o, routed:s4.routed }));
  console.log('\n   page errors: ' + errs.length + (errs.length ? '\n   ' + errs.join('\n   ') : ''));
  console.log('== RESULT: ' + pass + ' pass, ' + fail + ' FAIL ==');
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
