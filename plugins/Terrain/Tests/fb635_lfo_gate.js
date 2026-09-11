// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb635_lfo_gate.js — fb635: THE LFO PATH COMES BACK AS THE PATH, NOT A TRIANGLE.
//
//    node Tests/fb635_lfo_gate.js             # from plugins/Terrain (NODE_PATH=Tests/node_modules)
//
//  Max: "It doesn't remember my path. As you see I have the path here but it's a triangle. I want my path or
//  my drawing to always stay wherever it's at."
//
//  THE MECHANISM. __tiPullLfoShapes (a preset load) empties the shapes and renders at once; the LFO's shGet
//  seeds a TRIANGLE (seedFor(1)) into the empty tab; the async pull then lands the saved points and repainted
//  only when the shape was Custom (7). The shape parameter reaches the page first (a relay), so a Path (8)
//  tab kept drawing the seed.
//
//  THE BARS (the saved shape served by getSynthLfoShapes, 30 ms late like the bridge)
//   1  A PATH TAB DRAWS ITS SAVED PATH — shape param at Path, then the preset-load pull: the drawn path has the
//      saved points, in order (6 of them), not the 3-point seed
//   2  A CUSTOM TAB DRAWS ITS SAVED SHAPE — the same load with the shape at Custom: the curve passes the saved
//      points (the path that always worked, kept working)
//   3  THE PULL PUSHES NOTHING BACK — neither load wrote setSynthLfoShapes (the page never overwrote the DSP)
//   4  A HOST RESTORE THROUGH THE C++'s DOOR — onPatchLoaded(meta,'host'): the restored path draws, the next LFO2 edit
//      pushes LFO1 back as the Path (6 pts, pm 1), and no Loaded toast
//
//  MUTATION CONTROLS
//    LG_MUT=custom-only   the pull repaints only for Custom again (the fb634 page)   → [1] [4] RED
//    LG_MUT=hosttoast     a host restore shows the "Loaded" toast again               → [4] RED
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require(require('path').join(__dirname, 'node_modules', 'puppeteer-core'));
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE0 = path.join(ROOT, 'Source/ui/public/index.html');
const MUT = process.env.LG_MUT || '';
let pass = 0, fail = 0;
const chk = (ok, l, d) => { ok ? pass++ : fail++; console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${l}\n        ${d}`); };
const sleep = ms => new Promise(r => setTimeout(r, ms));
function page () {
  if (! MUT) return PAGE0;
  let s = fs.readFileSync(PAGE0, 'utf8');
  if (MUT === 'hosttoast') { const h = "    if (how !== 'host') toast(S.cur.name ? `Loaded ";
    if (s.split(h).length !== 2) { console.log('  MUTATION anchor not unique'); process.exit(2); }
    s = s.replace(h, "    toast(S.cur.name ? `Loaded "); const f = path.join(os.tmpdir(), 'fb635_lfo_hosttoast.html'); fs.writeFileSync(f, s); return f; }
  const a = "if(st.shape===7||st.shape===8||force)render(); }catch(e2){} }); }catch(e){} }   /* fb635 — Custom AND Path";
  if (s.split(a).length !== 2) { console.log('  MUTATION anchor not unique'); process.exit(2); }
  s = s.replace(a, "if(st.shape===7)render(); }catch(e2){} }); }catch(e){} }   /* fb635 — Custom AND Path");
  const f = path.join(os.tmpdir(), 'fb635_lfo_' + MUT + '.html'); fs.writeFileSync(f, s); return f;
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
    if (/getPresets/i.test(nm)) return r('[]'); if (/^getSynthLfoShapes$/.test(nm)) return setTimeout(() => r(window.__lfoShapesPayload || '{"shapes":[]}'), 30); if (/getSynthMod$/.test(nm)) return r('[]');
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


const PATH_PTS = [[0.1,0.2,0],[0.3,0.85,0],[0.55,0.3,0],[0.8,0.9,0],[0.9,0.15,0],[0.45,0.05,0]];   // a drawn loop — nothing like a triangle
const CUST_PTS = [[0,0.1,0],[0.2,0.95,0],[0.4,0.4,0],[0.7,0.8,0],[1,0.1,0]];
(async () => {
  console.log('══ fb635 LFO GATE — the Path comes back as the path ══   mutation: ' + (MUT || '(none)'));
  const P = page();
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width: 820, height: 656, deviceScaleFactor: 1 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  const choice = {}; ['A','B','C','D'].forEach(o => { choice['SYN_OSC_' + o + '_WARP_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_WARP2_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_ENGINE'] = 7; });
  for (let n = 1; n <= 10; n++) choice['LFO' + n + '_SHAPE'] = 11;
  await pg.evaluateOnNewDocument(STUB, { choice });
  await pg.goto('file://' + P, { waitUntil: 'load', timeout: 60000 }); await sleep(2400);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late');
    document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block';
    try { document.getElementById('syn-btn').click(); } catch (e) {} dispatchEvent(new Event('resize')); });
  await sleep(900);
  const load = (shape, entry) => pg.evaluate(async (shape, entry) => {
    window.__natives.length = 0;
    window.__lfoShapesPayload = JSON.stringify({ shapes: [entry] });
    window.__stubState('LFO1_SHAPE').setNormalisedValue(shape / 10);   // the relay lands first, as on a real load
    await new Promise(r => setTimeout(r, 60));
    window.__tiPullLfoShapes();                                          // then repull()'s call
    await new Promise(r => setTimeout(r, 400));
    const pth = document.querySelector('#mod-engine .mv-path'), stk = document.querySelector('#mod-engine .mv-stroke');
    const pill = (document.querySelector('#mod-engine #mv-shape') || {}).textContent || '';
    const pushes = window.__natives.filter(c => c.fn === 'setSynthLfoShapes').length;
    return { path: pth ? pth.getAttribute('d') : null, stroke: stk ? stk.getAttribute('d') : null, pill: pill.trim(), pushes };
  }, shape, entry);
  const pts = d => (d || '').trim().split(/\s*[ML]\s*/).filter(Boolean).map(t => t.trim().split(/\s+/).map(Number));
  // [1] PATH
  const r1 = await load(8, { n: 1, pts: PATH_PTS, gh: 8, gv: 8, sn: 0, nm: 'Path', pm: 1 });
  const p1 = pts(r1.path);
  const xs = p1.map(q => q[0]); const want = PATH_PTS.map(q => q[0]);
  const order = xs.length === 6 && xs.every((x, i) => i === 0 || Math.sign(x - xs[i - 1]) === Math.sign(want[i] - want[i - 1]));
  chk(r1.path != null && p1.length === 6 && order, '[1] A PATH TAB DRAWS ITS SAVED PATH — six saved points in their drawn order, not the 3-point triangle seed',
    `pill "${r1.pill}" · drawn points ${p1.length} ${JSON.stringify(p1.map(q => q.map(v => Math.round(v))))}`);
  // [2] CUSTOM
  const r2 = await load(7, { n: 1, pts: CUST_PTS, gh: 8, gv: 8, sn: 0, nm: 'Custom' });
  const s2 = pts(r2.stroke);
  const ys = s2.map(q => q[1]); const lo = Math.min(...ys), hi = Math.max(...ys);
  const peaks = s2.filter((q, i) => i > 0 && i < s2.length - 1 && q[1] < s2[i - 1][1] && q[1] <= s2[i + 1][1]).length;   // local minima in screen y = the curve's peaks (segment-exact: straight segments are one point each)
  chk(r2.stroke != null && s2.length >= 5 && peaks >= 2 && hi - lo > 20, '[2] A CUSTOM TAB DRAWS ITS SAVED SHAPE — two peaks, full swing (a triangle has one)',
    `pill "${r2.pill}" · ${s2.length} samples · peaks ${peaks} · swing ${Math.round(hi - lo)} px`);
  chk(r1.pushes === 0 && r2.pushes === 0, '[3] THE PULL PUSHES NOTHING BACK — no setSynthLfoShapes during either load', `pushes ${r1.pushes} / ${r2.pushes}`);

  // [4] a host restore through the C++'s door (the editor timer's onPatchLoaded(meta,'host'))
  const r4 = await pg.evaluate(async (P1) => {
    window.__natives.length = 0;
    window.__lfoShapesPayload = JSON.stringify({ shapes: [{ n: 1, pts: P1, gh: 8, gv: 8, sn: 0, nm: 'Path', pm: 1 }] });
    window.__stubState('LFO1_SHAPE').setNormalisedValue(0.8);            // the relays land (the host restored the state)
    await new Promise(r => setTimeout(r, 80));
    const toasts0 = document.querySelectorAll('.tp-toast, #tp-toast.on').length;
    window.onPatchLoaded({ name: 'Seal Team Six', bank: 'User', author: '', type: '', styles: '', note: '', fv: 4 }, 'host');   // TerrainUiCore::timerCallback → afterPatchLoad(true)
    await new Promise(r => setTimeout(r, 500));
    const pth = document.querySelector('#mod-engine .mv-path'); const drawn = pth ? pth.getAttribute('d') : null;
    // an edit on LFO2 (hop to tab 2, pick Saw — a seed, which pushes) sends EVERY tab — LFO1 must go out as the path
    const t2 = document.querySelector('#mod-engine .mv-tabs .t[data-tab="2"]'); if (t2) t2.click();
    await new Promise(r => setTimeout(r, 120));
    const sb = document.querySelector('#mod-engine #mv-shape'); if (sb) sb.click();
    const row = [...document.querySelectorAll('.mv-menu.open div[data-i]')].find(d => d.textContent.trim() === 'Saw'); if (row) row.click();   // seedTo → shPush: every tab goes out
    await new Promise(r => setTimeout(r, 400));
    const push = window.__natives.filter(c => c.fn === 'setSynthLfoShapes').pop();
    let l1 = null; try { l1 = JSON.parse(push.args[0]).shapes.find(e => e.n === 1); } catch (e) {}
    const toastTxt = (document.getElementById('tp-toast') || {}).textContent || '';
    const t1 = document.querySelector('#mod-engine .mv-tabs .t[data-tab="1"]'); if (t1) t1.click();
    return { drawn, pushed: !! push, l1n: l1 ? l1.pts.length : -1, l1pm: l1 ? (l1.pm || 0) : -1, toastTxt: toastTxt.trim().slice(0, 60) };
  }, PATH_PTS);
  const d4 = pts(r4.drawn);
  chk(d4.length === 6 && r4.pushed && r4.l1n === 6 && r4.l1pm === 1 && ! /Loaded/.test(r4.toastTxt),
    "[4] A HOST RESTORE THROUGH THE C++'s DOOR — the restored path draws; the next LFO2 edit pushes LFO1 back as the Path; no Loaded toast",
    `drawn ${d4.length} pts · LFO2 edit pushed=${r4.pushed} LFO1 ${r4.l1n} pts pm=${r4.l1pm} · toast "${r4.toastTxt}"`);
  if (errs.length) console.log('  page errors: ' + errs.slice(0, 3).join(' | '));
  console.log(`\n  ${pass} passed, ${fail} FAILED`); await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('  CRASH ' + (e && e.stack || e)); process.exit(2); });
