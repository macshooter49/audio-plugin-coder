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
  await pg.goto('file://' + PAGE, { waitUntil:'load', timeout:60000 }); await sleep(2400);
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

  console.log('\n== fb628 gate: assign -> play -> remove -> play (docked window, push-mode painter) ==\n');

  // tab is LFO 1 by default; check which tab the panel is on
  const tab = await pg.evaluate(() => window.__lfoCurTab ? window.__lfoCurTab() : null);
  console.log('   LFO panel tab:', tab);
  const LI = (tab || 1) - 1;

  // A: notes, no route -> must stay parked (fb628)
  const a = await pg.evaluate(async (LI) => window.__lp.sweep(30, LI), LI);
  chk(a.end.idle && a.spread < 0.01, 'A  no route + notes: parked (mv-idle, head does not move)', JSON.stringify({ spread:a.spread, idle:a.end.idle, routed:a.routed }));

  // B: assign LFO n -> Warp (the osc A warp2 amount value pill) through the matrix's own adder, play -> animates
  const dest = await pg.evaluate(() => { const el = document.querySelector('#osc-a-warp2-val'); const g = window.__ctlDestAt(el); return g ? g.dest : null; });
  console.log('   Warp dest =', dest);
  const bR = await pg.evaluate(async (LI, dest) => { window.__tiAddRoute(0, LI + 1, dest); await new Promise(r => setTimeout(r, 50)); return window.__lp.sweep(30, LI); }, LI, dest);
  chk(!bR.end.idle && bR.spread > 5 && bR.end.o > 0.9, 'B  route added + notes: animates (not idle, head sweeps, visible)', JSON.stringify({ spread:bR.spread, idle:bR.end.idle, o:bR.end.o, routed:bR.routed, routes:bR.routes }));

  // C: REMOVE via the real UI path: right-click the Warp pill -> ctl menu -> route row -> the ✕ (ctl-rx)
  const cRm = await pg.evaluate(async () => {
    const el = document.querySelector('#osc-a-warp2-val'); const r = el.getBoundingClientRect();
    el.dispatchEvent(new MouseEvent('contextmenu', { bubbles:true, cancelable:true, clientX:r.left + 4, clientY:r.top + 4 }));
    await new Promise(r => setTimeout(r, 120));
    const x = document.querySelector('.syn-ctx-route .ctl-rx');
    if (! x) return { clicked:false, menu: !!document.querySelector('.syn-ctx-menu'), rows: document.querySelectorAll('.syn-ctx-route').length };
    x.dispatchEvent(new PointerEvent('pointerdown', { bubbles:true })); x.dispatchEvent(new PointerEvent('pointerup', { bubbles:true }));
    x.click();
    await new Promise(r => setTimeout(r, 30));
    return { clicked:true, routesNow: window.__tiRoutes(), matrix: window.__matrix, routed: window.__tiLfoRouted(1) };
  });
  console.log('   remove via ctl-menu ✕:', JSON.stringify(cRm));
  if (! cRm.clicked) { await pg.evaluate((dest) => window.__tiPruneFxRoutes(dest, dest + 1), dest); console.log('   (fell back to __tiPruneFxRoutes — same splice+push)'); }
  await sleep(600);   // gesture tail (450 ms) expires, relay restore lands
  const c = await pg.evaluate(async (LI) => window.__lp.sweep(40, LI), LI);
  chk(c.end.idle && c.spread < 0.01 && c.routed === false, 'C  route REMOVED + notes: parks again (idle, head still, __tiLfoRouted false)', JSON.stringify({ spread:c.spread, idle:c.end.idle, o:c.end.o, routed:c.routed, routes:c.routes, matrix: await pg.evaluate(() => window.__matrix) }));

  // D: gesture fall-through — a key held (keydown auto-repeat) while notes sound and NO route
  const d = await pg.evaluate(async (LI) => {
    let mn = 1e9, mx = -1e9, h = null;
    for (let i = 0; i < 40; i++) { document.dispatchEvent(new KeyboardEvent('keydown', { key:'a', bubbles:true })); await window.__lp.push((i % 20) / 20, 1, LI); h = window.__lp.head(); mn = Math.min(mn, h.cx); mx = Math.max(mx, h.cx); }
    const ic = document.querySelector('#mv-shape .ic'); return { spread: mx - mn, end: h, emblemBucket: ic && ic.__b }; }, LI);
  chk(d.spread < 0.01, 'D  no route + notes + keydown stream: head does not move (else the fb591 gesture fall-through paints the DSP phase)', JSON.stringify(d));

  // E: the removal happens in ANOTHER window (popped card / curve editor): the C++ matrix goes empty, the docked mirror is only told to re-read
  await pg.evaluate(async (LI, dest) => { window.__tiAddRoute(0, LI + 1, dest); await new Promise(r => setTimeout(r, 50)); }, LI, dest);
  await sleep(1600);   // fb524's guard: the docked mirror prunes only once the local hand has been idle 1.5 s (the 2.5 s poll is the real cadence)
  const e0 = await pg.evaluate(() => ({ routed: window.__tiLfoRouted(1), matrix: window.__matrix }));
  await pg.evaluate(async () => { window.__matrix = '[]'; window.__tiModRestore(); await new Promise(r => setTimeout(r, 100)); window.__tiModRestore(); await new Promise(r => setTimeout(r, 100)); });
  const e = await pg.evaluate(async (LI) => window.__lp.sweep(40, LI), LI);
  chk(e.routed === false && e.end.idle, 'E  route removed ELSEWHERE (C++ matrix now [], docked told to re-read twice): docked __tiLfoRouted false + parked',
      JSON.stringify({ before:e0, after:{ routed:e.routed, idle:e.end.idle, spread:e.spread, routes:e.routes } }));

  // F: a getSynthMod reply in flight across the removal (relay/poll race): restore() issued, then removal, then the stale reply lands
  await pg.evaluate(async (dest) => { window.__relay = false; window.__tiPruneFxRoutes(dest, dest + 1); await new Promise(r => setTimeout(r, 50)); window.__relay = true; }, dest);
  await pg.evaluate(async (LI, dest) => { window.__tiAddRoute(0, LI + 1, dest); await new Promise(r => setTimeout(r, 50)); }, LI, dest);
  const f = await pg.evaluate(async (LI, dest) => {
    window.__relay = false;
    window.__tiModRestore();                         // getSynthMod snapshots the matrix WITH the route (like a C++ reply already on its way back)
    window.__tiPruneFxRoutes(dest, dest + 1);        // removal lands before the reply is applied
    await new Promise(r => setTimeout(r, 100));      // the stale reply is merged
    window.__relay = true;
    const s = await window.__lp.sweep(30, LI);
    return { routed:s.routed, idle:s.end.idle, spread:s.spread, routes:s.routes, matrix: window.__matrix }; }, LI, dest);
  chk(f.routed === false && f.idle, 'F  getSynthMod reply in flight across the removal: the docked mirror must not resurrect the route', JSON.stringify(f));

  console.log('\n   page errors: ' + errs.length + (errs.length ? '\n   ' + errs.join('\n   ') : ''));
  console.log('== RESULT: ' + pass + ' pass, ' + fail + ' FAIL ==');
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
