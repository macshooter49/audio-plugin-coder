// ══════════════════════════════════════════════════════════════════════════════════════════════
//  crv_ptmod_gate.js — fb573: AN LFO ON A POINT OF A CONNECTION CURVE IS REAL, AND THE POINT MOVES.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/crv_ptmod_gate.js [page.html]
//
//  Max (2026-09-02): "I see that we have a way to modulate the points, like LFO... but it's not doing
//  anything. It's not moving up and down, and it's not doing anything audially... let's just make sure
//  that the curves can have LFO on them."
//
//  BEFORE: the per-point menu wrote {ys,ya} into the mod host's closure and nothing serialised it (the
//  wire carried c only), so the DSP never knew; and even on the Distortion, where C++ re-bakes per
//  block, the point never moved on screen. NOW: the route carries `p` (points + mods) beside `c`, the
//  audio thread re-bakes the slot from the LFO bank (mod_src_cert bar 14 hears it), and a frame
//  painter moves the modded handle and the ink by the same LFO value the C++ consumed.
//
//  THE BARS (main page, a recording Juce stub)
//   1  THE WIRE — modulate point 2 by LFO 2: the next setSynthMod carries c (129) AND p with {ys:2, ya:.5}
//   2  REOPEN — close and reopen the editor: the point still carries its mod (the route keeps its points)
//   3  THE POINT MOVES — an LFO 2 value of +1 pushed through the frame feed lifts the modded handle and
//      rewrites the ink; −1 lowers it; a stale feed HOLDS the last value (the bank parks on it — fb566 — and so
//      does the DSP's table); the mid-segment tension dot rides the live segment
//   4  OFF — clearing the mod drops p from the wire and a straight base drops c too (the fb554 law)
//   5  zero page errors
//
//  PROOF THE BARS CAN FAIL:  CRV_PTMOD_MUTATE=1 (the painter is never registered) → bar 3 red
//                            CRV_PTMOD_MUTATE=2 (the encoder forgets p)             → bars 1, 2 red
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE = process.argv[2] || path.join(ROOT, 'Source/ui/public/index.html');
const MUT = +(process.env.CRV_PTMOD_MUTATE || 0);
let pass = 0, fail = 0;
const chk = (ok, l, d) => { if (ok) { pass++; console.log('  ok    ' + l + (d ? '   ' + d : '')); } else { fail++; console.log('  FAIL  ' + l + (d ? '   ' + d : '')); } };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split(f).length - 1; if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); } src = src.replace(f, t); console.log('  (mutation ' + MUT + ' landed: 1 site)'); };
  if (MUT === 1) sub("  if(window.__tiFrameReg) window.__tiFrameReg('crv-ptmod', crvPtModTick);", "");
  if (MUT === 2) sub("      if(a.curve&&a.pts&&window.__crvUtil) o.p=window.__crvUtil.packPts(a.pts);", "");
  const p = path.join(os.tmpdir(), 'crv_ptmod_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}
const STUB = () => {
  window.__natives = []; window.__store = { mod: '[]' };
  const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  const nativeFn = (nm) => (...a) => new Promise((r) => { window.__natives.push({ fn:nm, args:a.map(String) });
    if (/getPresets/i.test(nm)) return r('[]'); if (nm === 'getSynthMod') return r(window.__store.mod); if (nm === 'setSynthMod') { window.__store.mod = String(a[0]); return r(0); }
    if (/Json|JSON/.test(nm)) return r('{}'); r(0); });
  window.Juce = { getSliderState:mk, getToggleState:mk, getComboBoxState:mk, getNativeFunction:nativeFn, backend:{ addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable:true, get(){ return held; }, set(v){ held = Object.assign({}, v||{}, { getNativeFunction:mine.getNativeFunction }); } }); })();
  window.__JUCE__ = { backend:window.Juce.backend, initialisationData:{ vendor:'', pluginName:'', pluginVersion:'', __juce__sliders:[], __juce__toggles:[], __juce__comboBoxes:[], __juce__functions:[] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
};
(async () => {
  const P = mutatedPage();
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'), headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:820, height:656, deviceScaleFactor:2 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await pg.evaluateOnNewDocument(STUB);
  await pg.goto('file://' + P, { waitUntil:'load', timeout:60000 }); await sleep(2200);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late'); document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block'; try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await sleep(500);
  console.log('\n══ fb573 — AN LFO ON A POINT OF A CONNECTION CURVE ══');
  console.log('   page ' + P + (MUT ? '   MUTATION ' + MUT : '') + '\n');
  // 1 · the wire
  const s1 = await pg.evaluate(async () => {
    window.__tiAddRoute(0, 1, 64); await new Promise(r => setTimeout(r, 150));
    window.__tiCurveEdit(0); await new Promise(r => setTimeout(r, 200));
    window.__crvSetPts([[0, 0, 0], [0.5, 0.5, 0], [1, 1, 0]]); await new Promise(r => setTimeout(r, 200));
    window.__natives.length = 0;
    const okM = window.__crvPtMod(1, 2, 0.5); await new Promise(r => setTimeout(r, 200));
    const sm = window.__natives.filter(n => n.fn === 'setSynthMod').pop(); let route = null; try { route = JSON.parse(sm.args[0])[0]; } catch(e) {}
    return { okM, sent: !!sm, c: route && route.c ? String(route.c).split(',').length : 0, p: route && route.p ? route.p : null, tiC: window.__tiRoutes()[0].c }; });
  chk(s1.okM && s1.sent && s1.c === 129 && s1.p && s1.p.length === 3 && s1.p[1][3] && s1.p[1][3].ys === 2 && Math.abs(s1.p[1][3].ya - 0.5) < 1e-6,
      '1  THE WIRE: modulating point 2 by LFO 2 sends c (129) and p with {ys:2, ya:0.5}', 'c ' + s1.c + '  p ' + JSON.stringify(s1.p));
  // 2 · reopen keeps the mod
  const s2 = await pg.evaluate(async () => { window.__crvClose(); await new Promise(r => setTimeout(r, 100)); window.__tiCurveEdit(0); await new Promise(r => setTimeout(r, 200));
    const P2 = window.__crvPtsFull(); return { n: P2 ? P2.length : 0, m: P2 && P2[1] && P2[1][3] ? P2[1][3] : null }; });
  chk(s2.n === 3 && s2.m && s2.m.ys === 2, '2  REOPEN: the editor reopens from the route\'s points, the mod still on point 2', JSON.stringify(s2.m));
  // 3 · the point moves with the feed
  const s3 = await pg.evaluate(async () => {
    const raf = () => new Promise(r => requestAnimationFrame(() => r()));
    const nd = () => { const e = document.querySelector('.crv-ext svg .sh-nd[data-i="1"]'); return e ? +e.getAttribute('cy') : null; };
    const ink = () => { const e = document.querySelector('.crv-ext svg .crv-ink'); return e ? e.getAttribute('d') : ''; };
    const push = async (v) => { const L = [0,0,0,0,0,0,0,0,0,0]; L[1] = v; const Pp = [0,0,0,0,0,0,0,0,0,0]; window.__notesActive = 1; window.__notesActiveT = Date.now();
      // pushed TWICE 40 ms apart: __mvLfoValAt eases between consecutive pushes over the measured push interval (fb498),
      // so a single push samples mid-ease; two equal pushes make the eased value the value. The REGISTERED painter
      // must move the point (mutation 1 unregisters it).
      for (let k = 0; k < 2; k++) { window.__modViz([], L, Pp); if (window.__tiFrame) window.__tiFrame(); await raf(); await raf(); await new Promise(r => setTimeout(r, 40)); }
      if (window.__tiFrame) window.__tiFrame(); await raf(); await raf(); };
    const base = nd(), baseInk = ink(); const cd0 = document.querySelector('.crv-ext svg .sh-cd[data-i="1"]'); const cdBase = cd0 ? +cd0.getAttribute('cy') : null;   // the tension dot's OWN base
    await push(1.0); const up = nd(), upInk = ink();
    await push(-1.0); const dn = nd();
    await new Promise(r => setTimeout(r, 400)); if (window.__tiFrame) window.__tiFrame(); await raf(); await raf(); await raf();
    const held = nd();   // the feed is stale: the bank parks on −1 (fb566) and so does the DSP's table — the page HOLDS it
    const cd = document.querySelector('.crv-ext svg .sh-cd[data-i="1"]'); const cdY = cd ? +cd.getAttribute('cy') : null;
    return { base, up, dn, held, cdY, cdBase, inkMoved: upInk !== baseInk }; });
  chk(s3.base != null && s3.up < s3.base - 20 && s3.dn > s3.base + 20 && Math.abs(s3.held - s3.dn) < 0.6 && s3.inkMoved && s3.cdY != null && s3.cdBase != null && s3.cdY > s3.cdBase + 20,
      '3  THE POINT MOVES: LFO 2 at +1 lifts the modded handle and the ink, −1 lowers it, a stale feed HOLDS the parked value (as the DSP does), the tension dot rides the live segment',
      'cy base ' + s3.base + ' → +1 ' + s3.up + ' → −1 ' + s3.dn + ' → stale ' + s3.held + '  ink moved ' + s3.inkMoved + '  midpoint cy ' + s3.cdBase + ' → ' + s3.cdY);
  // 4 · off
  const s4 = await pg.evaluate(async () => { window.__natives.length = 0; window.__crvPtMod(1, 0); await new Promise(r => setTimeout(r, 200));
    const sm = window.__natives.filter(n => n.fn === 'setSynthMod').pop(); let route = null; try { route = JSON.parse(sm.args[0])[0]; } catch(e) {}
    return { hasP: !!(route && route.p), hasC: !!(route && route.c), tiC: window.__tiRoutes()[0].c }; });
  chk(!s4.hasP && !s4.hasC && s4.tiC === 0, '4  OFF: clearing the mod drops p, and the straight base drops c (a straight connection carries no curve)', 'p ' + s4.hasP + '  c ' + s4.hasC);
  chk(errs.length === 0, '5  zero page errors', errs.length ? errs.join(' | ') : '');
  console.log('\n══ RESULT: ' + pass + ' pass, ' + fail + ' FAIL ══\n');
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
