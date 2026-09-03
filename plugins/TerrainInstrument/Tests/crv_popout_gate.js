// ══════════════════════════════════════════════════════════════════════════════════════════════
//  crv_popout_gate.js — fb570: EVERY CURVE HOST POPS OUT, ONE EDITOR RETARGETS, DOCK COMES HOME.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/crv_popout_gate.js [page.html]
//
//  Max (2026-09-02): "every card that we have in Terrain pops out, so it just makes sense for the
//  curve edit to pop out as well... I just really want them to pop out. That's it. I want them to work."
//
//  WHAT WAS WRONG. The curve card popped out through the generic fb82 door, which carries a 3-letter id
//  and nothing else; the new window's only boot was the Distortion host, so fb560 refused every guest
//  (warp slot, mod connection) with a toast. And while a curve floated, opening a different one raised
//  a docked twin beside it — the floating window was never told.
//
//  THE SHAPE NOW. The main view parks the host's IDENTITY on the processor (setCardState 'crv' →
//  {key:'dst'} · {key:'warp',osc,slot} · {key:'mod',s,d}) BEFORE popOutCard; the card page reads it
//  (getCardState) and boots that host through its own door; opening any curve while one floats
//  RETARGETS the window (setCardState + retargetCard → __crvBootHost in the popped page); Dock reads the
//  same identity back. Each guest re-reads its truth from C++, so no points ever travel.
//
//  THE BARS (main page, a recording Juce stub):
//   1  a warp curve opens docked as 'warp'
//   2  ⧉ on it: NO refusal toast; setCardState('crv', {warp,a,0}) lands BEFORE popOutCard('crv'); the
//      docked copy hides; __poppedCards.crv is set
//   3  while it floats, opening a MOD curve retargets: setCardState({mod,s,d}) + retargetCard('crv'),
//      and NO docked twin opens
//   4  Dock (the C++ path: __cardWinGone then __redockCard) reopens the host it left with (mod), then a
//      warp identity, then the Distortion — each by identity alone
//   5  zero page errors
//  (card-only page, ?card=crv, the stub answering getCardState with an identity):
//   6  a WARP identity boots the warp host: pinned card, 'OSC A · WARP 1' in the title, points fitted
//   7  a point edit there CAPTURES: setSynParam(SYN_OSC_A_WARP_MODE → 37) and setWarpDrawCurve('a',0,csv)
//      were both called — the card window has no relays, so this proves getParamCardinality's cache
//   8  exactly ONE 300 ms follow-poll is alive (the fb562 law), and the Distortion lanes stayed asleep
//      (no getDistortionCurveViz / getDistortionCurves polling for a guest)
//   9  a MOD identity boots the mod host ('LFO 1 — Curve') and a point edit lands in setSynthMod with a
//      129-sample curve on the route
//  10  no identity at all = the Distortion host, as before
//  (the review round — three root causes, each with a bar that failed before its fix:)
//  2b  re-opening the SAME floating host only re-fronts the window (no re-park, no reboot)
//  11  a MOD identity on a route with NO surface on the card page (LFO 3 Depth while tab 1 is active) still
//      boots, and its edit carries the WHOLE matrix — the route it cannot draw is not deleted from the wire
//  12  a warp identity whose slot answers 'no mode' cannot mount: the card page CLOSES ITS WINDOW (never a blank
//      always-on-top window with no ✕)
//  13  a popped mod curve whose route is removed from the patch closes its window too
//  16  the main view merges a curve the wire carries (the C++ relay's JS half) and Dock opens on it
//
//  PROOF THE BARS CAN FAIL:
//    CRV_MUTATE=1  the fb560 refusal toast back in the ⧉ door           → bar 2 red
//    CRV_MUTATE=2  the identity is not parked before the pop            → bar 2 red
//    CRV_MUTATE=3  the fb559 guard (Distortion refocus only)            → bar 3 red (a docked twin)
//    CRV_MUTATE=4  __paramCardinality without the card cache             → bar 7 red (a silent no-op)
//    CRV_MUTATE=5  the mirror's `if(!el)return;` back (routes without a surface not mirrored) → bar 11 red
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE = process.argv[2] || path.join(ROOT, 'Source/ui/public/index.html');
const MUT = +(process.env.CRV_MUTATE || 0);
let pass = 0, fail = 0;
const chk = (ok, l, d) => { if (ok) { pass++; console.log('  ok    ' + l + (d ? '   ' + d : '')); }
                            else { fail++; console.log('  FAIL  ' + l + (d ? '   ' + d : '')); } };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split(f).length - 1;
    if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); }
    src = src.replace(f, t); console.log('  (mutation ' + MUT + ' landed: 1 site)'); };
  if (MUT === 1) sub("      if(id==='crv'){ /* fb570 — every host pops out (fb560's Distortion-only refusal is retired): park the identity first */",
                     "      if(id==='crv'&&window.__crvHostKey&&window.__crvHostKey()!=='dst'){ if(window.__crvToast) window.__crvToast('This curve lives in the patch \\u2014 only the Distortion curve pops out'); return; }\n      if(id==='crv'){");
  if (MUT === 2) sub("        var scs=NF('setCardState'); if(scs){ try{ var sj=JSON.stringify(window.__crvHostSpec?window.__crvHostSpec():{key:'dst'}); scs('crv', sj); if(window.__crvParked) window.__crvParked(sj); }catch(e3){} } }\n      var r=card.getBoundingClientRect();\n      try{ f(id,Math.round(r.left),Math.round(r.top),Math.round(r.width),Math.round(r.height)); }catch(e2){ return; }",
                     "        }\n      var r=card.getBoundingClientRect();\n      try{ f(id,Math.round(r.left),Math.round(r.top),Math.round(r.width),Math.round(r.height)); }catch(e2){ return; }");
  if (MUT === 3) sub("    if(!window.__cardOnly&&window.__poppedCards&&window.__poppedCards.crv){\n      HOST=host||null; sel=-1;",
                     "    if(!host&&!window.__cardOnly&&window.__poppedCards&&window.__poppedCards.crv){\n      HOST=host||null; sel=-1;");
  if (MUT === 4) sub("        var cc = window.__cardinalityCache; if (cc && cc[name] > 1) return cc[name] | 0;", "");
  if (MUT === 5) sub("         mark, never a missing route. */\n      var ex=findRoute(S,dest);", "         mark, never a missing route. */\n      if(!el)return;\n      var ex=findRoute(S,dest);");
  const p = path.join(os.tmpdir(), 'crv_popout_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}

// the fb563 recording stub (lfo_park.js's), extended with the natives the curve editor's guests read
const WARP_N = (() => {
  const s = fs.readFileSync(path.join(ROOT, 'Source/PluginProcessor.cpp'), 'utf8');
  const m = /for \(int i = w\.size\(\); i < (\d+); \+\+i\) w\.add \("Reserved "/.exec(s);
  if (! m) throw new Error('warp cardinality not found in PluginProcessor.cpp');
  return +m[1];
})();
const STUB = (cfg) => {
  window.__emits = []; window.__natives = []; window.__store = { card: {}, mod: '[]' };
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
  // a phase-map fixture the warp host can fit: a gentle monotone curve, no wraps
  const warpFixture = (osc, slot) => { const pts = []; for (let i = 0; i < 129; i++) pts.push(+Math.pow(i/128, 1.6).toFixed(5));
    return JSON.stringify({ mode:7, slot:slot|0, amt:0.6, var:0, kind:'phase', pure:true, rate:1, x0:0, x1:1, pts }); };
  const nativeFn = (nm) => (...a) => new Promise((r) => { window.__natives.push({ fn:nm, args:a.map(String), t:window.__natives.length });   // t = call ORDER (performance.now() is coarsened to 100 µs; two calls can share a stamp)
    if (/getPresets/i.test(nm)) return r('[]');
    if (nm === 'getSynthMod') return r(window.__store.mod);
    if (nm === 'setSynthMod') { window.__store.mod = String(a[0]); return r(0); }
    if (nm === 'getWarpCurve') return r(window.__warpNone ? '{"mode":0,"slot":0,"amt":0,"var":0,"kind":"none","pts":[]}' : warpFixture(a[0], a[1]));
    if (nm === 'setCardState') { window.__store.card[String(a[0])] = String(a[1]); return r(0); }
    if (nm === 'getCardState') return r(window.__store.card[String(a[0])] || '');
    if (nm === 'getParamCardinality') return r(/_WARP2?_MODE$/.test(String(a[0])) ? cfg.warpN : 0);
    if (nm === 'getPoppedCards') return r('');
    if (nm === 'getSynParam') return r(String(get(String(a[0])).getNormalisedValue()));
    if (/Json|JSON/.test(nm)) return r('{}'); r(0); });
  window.Juce = { getSliderState:get, getToggleState:get, getComboBoxState:get, getNativeFunction:nativeFn,
                  backend:{ addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable:true,
    get(){ return held; }, set(v){ held = Object.assign({}, v||{}, { getNativeFunction:mine.getNativeFunction, getSliderState:mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend:window.Juce.backend, initialisationData:{ vendor:'', pluginName:'', pluginVersion:'',
    __juce__sliders:[], __juce__toggles:[], __juce__comboBoxes:[], __juce__functions:[] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
  // the fb562 interval census: which 300 ms polls are alive
  const live = {}, SI = window.setInterval, CI = window.clearInterval;
  window.setInterval = function(f, ms){ const id = SI.apply(window, arguments); if (ms === 300) live[id] = 1; return id; };
  window.clearInterval = function(id){ if (live[id]) delete live[id]; return CI.apply(window, arguments); };
  window.__polls300 = () => Object.keys(live).length;
};

async function newPage (b, P, query, seed) {
  const pg = await b.newPage(); await pg.setViewport({ width:820, height:656, deviceScaleFactor:2 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 200)));
  // a CARD WINDOW has no relays (fb82): the card-only pages get NO choice cardinalities, so __paramCardinality's
  // relay read answers 0 there exactly as in the plugin, and only the getParamCardinality cache can carry a capture
  const choice = {}; if (! query) ['A','B','C','D'].forEach(o => { choice['SYN_OSC_' + o + '_WARP_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_WARP2_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_ENGINE'] = 7; });
  await pg.evaluateOnNewDocument(STUB, { choice, warpN: WARP_N });
  if (seed) await pg.evaluateOnNewDocument(seed.fn, seed.arg);
  await pg.goto('file://' + P + (query || ''), { waitUntil:'load', timeout:60000 }); await sleep(2400);
  return { pg, errs };
}
const natives = (pg, fn) => pg.evaluate((f) => window.__natives.filter(n => n.fn === f), fn);

(async () => {
  const P = mutatedPage();
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  console.log('\n══ fb570 — EVERY CURVE HOST POPS OUT · ONE EDITOR RETARGETS · DOCK COMES HOME ══');
  console.log('   page ' + P + (MUT ? '   MUTATION ' + MUT : '') + '\n');

  // ═══ MAIN PAGE ═══
  const { pg, errs } = await newPage(b, P, '');
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late');
    document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block';
    try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await sleep(600);

  // 1 · a warp curve opens docked
  await pg.evaluate(() => { window.__openWarpExt('a', 0, { clientX:400, clientY:300, target:document.body }); });
  await sleep(900);
  const s1 = await pg.evaluate(() => { const st = window.__crvOpenState(); const c = document.querySelector('.crv-ext');
    return { st, open: !!(c && c.classList.contains('open')), spec: JSON.stringify(window.__crvHostSpec()) }; });
  chk(s1.open && s1.st.key === 'warp' && /OSC A/.test(s1.st.title) && /WARP 1/.test(s1.st.title) && s1.st.pts > 2,
      '1  a warp curve opens docked as the warp host', s1.st.title + '  pts ' + s1.st.pts + '  spec ' + s1.spec);

  // 2 · ⧉ pops it: no toast, identity parked before the pop, docked copy hidden
  await pg.evaluate(() => { window.__natives.length = 0; window.__toastSeen = '';
    const t = document.querySelector('.mv-toast'); if (t) { new MutationObserver(() => { if (/only the Distortion/.test(t.textContent)) window.__toastSeen = t.textContent; }).observe(t, { childList:true, characterData:true, subtree:true }); }
    document.querySelector('.crv-ext .pop').click(); });
  await sleep(400);
  const s2 = await pg.evaluate(() => { const c = document.querySelector('.crv-ext'); const N = window.__natives;
    const sc = N.find(n => n.fn === 'setCardState' && n.args[0] === 'crv'), po = N.find(n => n.fn === 'popOutCard' && n.args[0] === 'crv');
    const toast = document.querySelector('.mv-toast'); const tt = toast ? toast.textContent : '';
    return { sc: sc ? sc.args[1] : null, scT: sc ? sc.t : -1, po: !!po, poT: po ? po.t : -1, open: !!(c && c.classList.contains('open')),
             popped: !!(window.__poppedCards && window.__poppedCards.crv), toast: window.__toastSeen || (/only the Distortion/.test(tt) ? tt : '') }; });
  let spec2 = null; try { spec2 = JSON.parse(s2.sc || 'null'); } catch(e) {}
  chk(!s2.toast && spec2 && spec2.key === 'warp' && spec2.osc === 'a' && spec2.slot === 0 && s2.po && s2.scT < s2.poT && !s2.open && s2.popped,
      '2  ⧉: no refusal, setCardState({warp,a,0}) BEFORE popOutCard, docked copy hidden, __poppedCards.crv set',
      (s2.toast ? 'TOAST "' + s2.toast.slice(0, 50) + '" ' : '') + 'identity ' + s2.sc + '  pop ' + s2.po + '  order ' + (s2.scT < s2.poT ? 'ok' : 'WRONG') + '  docked open ' + s2.open);

  // 2b · the SAME floating host again: a re-front, never a re-park / reboot
  await pg.evaluate(() => { window.__natives.length = 0; window.__openWarpExt('a', 0, { clientX:400, clientY:300, target:document.body }); });
  await sleep(700);
  const s2b = await pg.evaluate(() => { const N = window.__natives; return { ping: N.filter(n => n.fn === 'popOutCard' && n.args[0] === 'crv' && n.args[3] === '0').length,
    sc: N.filter(n => n.fn === 'setCardState' && n.args[0] === 'crv').length, rt: N.filter(n => n.fn === 'retargetCard').length, open: !!document.querySelector('.crv-ext.open') }; });
  chk(s2b.ping >= 1 && s2b.sc === 0 && s2b.rt === 0 && !s2b.open, '2b the SAME floating host re-opened: one re-front ping, no re-park, no retarget, no docked twin',
      'pings ' + s2b.ping + '  setCardState ' + s2b.sc + '  retargetCard ' + s2b.rt);

  // 3 · retarget: a MOD curve opened while floating switches the window, no docked twin
  await pg.evaluate(() => { window.__natives.length = 0; window.__tiAddRoute(0, 1, 64); });
  await sleep(300);
  await pg.evaluate(() => { window.__tiCurveEdit(0); });
  await sleep(400);
  const s3 = await pg.evaluate(() => { const c = document.querySelector('.crv-ext'); const N = window.__natives;
    const sc = N.filter(n => n.fn === 'setCardState' && n.args[0] === 'crv').pop(), rt = N.find(n => n.fn === 'retargetCard');
    return { sc: sc ? sc.args[1] : null, rt: rt ? rt.args[0] : null, open: !!(c && c.classList.contains('open')), key: window.__crvHostKey(), routes: JSON.stringify(window.__tiRoutes()) }; });
  let spec3 = null; try { spec3 = JSON.parse(s3.sc || 'null'); } catch(e) {}
  chk(spec3 && spec3.key === 'mod' && spec3.d === 64 && s3.rt === 'crv' && !s3.open && s3.key === 'mod',
      '3  RETARGET: a mod curve opened while floating → setCardState({mod,s,64}) + retargetCard, NO docked twin',
      'identity ' + s3.sc + '  retarget ' + s3.rt + '  docked twin open ' + s3.open + '  host ' + s3.key);

  // 4 · Dock comes home to the host it left with — by identity alone
  await pg.evaluate(() => { window.__cardWinGone('crv'); window.__redockCard('crv'); });
  await sleep(700);
  const s4a = await pg.evaluate(() => { const c = document.querySelector('.crv-ext'); const st = window.__crvOpenState(); return { open: !!(c && c.classList.contains('open')), st }; });
  await pg.evaluate(() => { window.__crvClose(); window.__store.card.crv = JSON.stringify({ key:'warp', osc:'a', slot:1 }); window.__redockCard('crv'); });
  await sleep(1000);
  const s4b = await pg.evaluate(() => { const c = document.querySelector('.crv-ext'); const st = window.__crvOpenState(); return { open: !!(c && c.classList.contains('open')), st, left: c ? parseFloat(c.style.left) : -1, top: c ? parseFloat(c.style.top) : -1 }; });
  await pg.evaluate(() => { window.__crvClose(); window.__store.card.crv = JSON.stringify({ key:'dst' }); window.__redockCard('crv'); });
  await sleep(700);
  const s4c = await pg.evaluate(() => { const c = document.querySelector('.crv-ext'); const st = window.__crvOpenState(); return { open: !!(c && c.classList.contains('open')), st }; });
  chk(s4a.open && s4a.st.key === 'mod' && /LFO 1/.test(s4a.st.title)
      && s4b.open && s4b.st.key === 'warp' && /WARP 2/.test(s4b.st.title)
      && s4c.open && s4c.st.key === 'dst',
      '4  DOCK: reopens the mod host it left with, then a warp identity (slot 2), then the Distortion',
      '"' + s4a.st.title + '" → "' + s4b.st.title + '" → "' + s4c.st.title + '"');
  chk(s4b.left > 8, '4b DOCK of a warp host lands the docked card where the editor places it (a body anchor put it at left 8)', 'left ' + s4b.left + ' top ' + s4b.top + ' (top clamps to 8 in this short viewport)');

  // 16 · the wire carries a curve this page never drew (the popped window wrote it): one kick merges it, Dock opens on it
  const s16 = await pg.evaluate(async () => {
    window.__crvClose();
    const c = []; for (let i = 0; i < 129; i++) c.push(Math.pow(i / 128, 2).toFixed(4));
    window.__store.mod = JSON.stringify([{ s:0, d:64, v:0.5, c:c.join(',') }]);
    window.__tiModRestore();   // what the C++ relay evaluates when synModVersion_ moves
    await new Promise(r => setTimeout(r, 500));
    const routes = window.__tiRoutes();
    window.__tiCurveEditKey(0, 64);
    await new Promise(r => setTimeout(r, 500));
    return { c: routes[0] ? routes[0].c : -1, st: window.__crvOpenState() }; });
  chk(s16.c === 129 && s16.st.open && s16.st.key === 'mod' && s16.st.pts > 2,
      '16 RELAY: a curve the wire carries lands in this page\'s routes on one kick, and the editor opens on it', 'route c=' + s16.c + '  editor pts ' + s16.st.pts);

  chk(errs.length === 0, '5  zero page errors on the main page', errs.length ? errs.join(' | ') : '');

  // ═══ CARD-ONLY PAGE · a WARP identity ═══
  const seedWarp = { fn: (j) => { window.addEventListener('DOMContentLoaded', () => { window.__store.card.crv = j; }); }, arg: JSON.stringify({ key:'warp', osc:'a', slot:0 }) };
  const c1 = await newPage(b, P, '?card=crv', seedWarp);
  await sleep(1600);
  const s6 = await c1.pg.evaluate(() => { const c = document.querySelector('.ti-popcard.crv-ext'); const st = window.__crvOpenState();
    return { pinned: !!c, open: !!(c && c.classList.contains('open')), st, cardOnly: window.__cardOnly, gcs: window.__natives.filter(n => n.fn === 'getCardState').length,
             gpc: window.__natives.filter(n => n.fn === 'getParamCardinality').map(n => n.args[0]).join(','), polls: window.__polls300() }; });
  chk(s6.cardOnly === 'crv' && s6.pinned && s6.open && s6.st.key === 'warp' && /OSC A/.test(s6.st.title) && /WARP 1/.test(s6.st.title) && s6.st.pts > 2 && s6.gcs >= 1 && s6.polls === 1,
      '6  CARD: a warp identity boots the warp host — pinned, titled, points fitted, exactly ONE 300 ms follow-poll', s6.st.title + '  pts ' + s6.st.pts + '  getCardState×' + s6.gcs + '  cardinality asked for ' + (s6.gpc || 'nothing') + '  polls ' + s6.polls);

  // 7 · a point edit in the popped window CAPTURES (mode → 37, the curve → setWarpDrawCurve)
  await c1.pg.evaluate(() => { window.__natives.length = 0;
    window.__crvSetPts([[0, 0, 0], [0.25, 0.55, 0.3], [0.6, 0.7, 0], [1, 1, 0]]); });
  await sleep(500);
  const s7 = await c1.pg.evaluate(() => { const N = window.__natives;
    const sp = N.find(n => n.fn === 'setSynParam' && n.args[0] === 'SYN_OSC_A_WARP_MODE'), wd = N.find(n => n.fn === 'setWarpDrawCurve');
    return { mode: sp ? +sp.args[1] : -1, wd: wd ? { osc: wd.args[0], slot: wd.args[1], n: wd.args[2].split(',').length } : null, card: window.__paramCardinality('SYN_OSC_A_WARP_MODE') }; });
  chk(s7.wd && s7.wd.osc === 'a' && (+s7.wd.slot) === 0 && s7.wd.n === 129 && s7.mode >= 0 && Math.abs(s7.mode - 37 / (s7.card - 1)) < 1e-6,
      '7  CARD: a point edit CAPTURES — SYN_OSC_A_WARP_MODE → Draw (37) through the card\'s cardinality, and setWarpDrawCurve(a, 0, 129 samples)',
      'cardinality ' + s7.card + '  mode norm ' + (s7.mode >= 0 ? s7.mode.toFixed(4) : 'NOT WRITTEN') + '  curve ' + (s7.wd ? s7.wd.n + ' samples' : 'NOT SENT'));

  // 8 · one 300 ms poll, and the Distortion lanes asleep for a guest
  await c1.pg.evaluate(() => { window.__natives.length = 0; });
  await sleep(900);
  const s8 = await c1.pg.evaluate(() => ({ polls: window.__polls300(), dst: window.__natives.filter(n => /^getDistortionCurve/.test(n.fn)).length }));
  chk(s8.polls === 0 && s8.dst === 0, '8  CARD: after the capture the follow-poll STOOD DOWN (the ink is yours), and the Distortion lanes made no calls for a guest', s8.polls + ' poll(s), ' + s8.dst + ' Distortion reads in 900 ms');
  if (c1.errs.length) console.log('     (warp card page errors, informational: ' + c1.errs.join(' | ') + ')');

  // ═══ CARD-ONLY PAGE · a MOD identity ═══
  const seedMod = { fn: (j) => { window.addEventListener('DOMContentLoaded', () => { window.__store.card.crv = j; window.__store.mod = JSON.stringify([{ s:0, d:64, v:0.5 }]); }); }, arg: JSON.stringify({ key:'mod', s:0, d:64 }) };
  const c2 = await newPage(b, P, '?card=crv', seedMod);
  await sleep(1600);
  const s9a = await c2.pg.evaluate(() => { const c = document.querySelector('.ti-popcard.crv-ext'); const st = window.__crvOpenState(); return { open: !!(c && c.classList.contains('open')), st }; });
  await c2.pg.evaluate(() => { window.__natives.length = 0; window.__crvSetPts([[0, 0, 0], [0.5, 0.8, 0], [1, 1, 0]]); });
  await sleep(500);
  const s9b = await c2.pg.evaluate(() => { const N = window.__natives; const sm = N.filter(n => n.fn === 'setSynthMod').pop();
    let c = 0; try { const arr = JSON.parse(sm.args[0]); const r = arr.find(o => (o.d|0) === 64); c = r && r.c ? String(r.c).split(',').length : 0; } catch(e) {}
    return { sent: !!sm, c }; });
  chk(s9a.open && s9a.st.key === 'mod' && /LFO 1/.test(s9a.st.title) && s9b.sent && s9b.c === 129,
      '9  CARD: a mod identity boots the mod host and a point edit lands in setSynthMod with a 129-sample curve on the route',
      '"' + s9a.st.title + '"  setSynthMod ' + (s9b.sent ? 'sent, c=' + s9b.c : 'NOT SENT'));
  if (c2.errs.length) console.log('     (mod card page errors, informational: ' + c2.errs.join(' | ') + ')');

  // ═══ CARD-ONLY PAGE · a MOD identity on a route with NO surface here (LFO 3 Depth, tab 1 active) ═══
  const seedMod14 = { fn: (j) => { window.addEventListener('DOMContentLoaded', () => { window.__store.card.crv = j; window.__store.mod = JSON.stringify([{ s:0, d:64, v:0.5 }, { s:0, d:14, v:0.5 }]); }); }, arg: JSON.stringify({ key:'mod', s:0, d:14 }) };
  const c4 = await newPage(b, P, '?card=crv', seedMod14);
  await sleep(1800);
  const s11a = await c4.pg.evaluate(() => { const c = document.querySelector('.ti-popcard.crv-ext'); const st = window.__crvOpenState();
    return { open: !!(c && c.classList.contains('open')), st, routes: window.__tiRoutes().map(r => r.d).sort().join(','), surface: !!document.querySelector('#mod-engine .mv-tabs .t.act[data-tab="3"]') }; });
  await c4.pg.evaluate(() => { window.__natives.length = 0; window.__crvSetPts([[0, 0, 0], [0.5, 0.2, 0], [1, 1, 0]]); });
  await sleep(500);
  const s11b = await c4.pg.evaluate(() => { const N = window.__natives; const sm = N.filter(n => n.fn === 'setSynthMod').pop();
    let dests = [], c14 = 0; try { const arr = JSON.parse(sm.args[0]); dests = arr.map(o => o.d|0).sort(); const r = arr.find(o => (o.d|0) === 14); c14 = r && r.c ? String(r.c).split(',').length : 0; } catch(e) {}
    return { sent: !!sm, dests: dests.join(','), c14 }; });
  chk(s11a.open && s11a.st.key === 'mod' && s11a.routes === '14,64' && !s11a.surface && s11b.sent && s11b.dests === '14,64' && s11b.c14 === 129,
      '11 CARD: a mod identity on a route with NO surface here boots, and its edit carries the WHOLE matrix (the undrawable route survives)',
      'mirror ' + s11a.routes + '  surface for 14: ' + s11a.surface + '  wrote dests ' + (s11b.dests || 'NOTHING') + '  c on 14 = ' + s11b.c14);
  if (c4.errs.length) console.log('     (mod-14 card page errors, informational: ' + c4.errs.join(' | ') + ')');

  // ═══ CARD-ONLY PAGE · a warp identity whose slot has NO mode: the window closes itself ═══
  const seedNone = { fn: (j) => { window.addEventListener('DOMContentLoaded', () => { window.__store.card.crv = j; window.__warpNone = 1; }); }, arg: JSON.stringify({ key:'warp', osc:'a', slot:0 }) };
  const c5 = await newPage(b, P, '?card=crv', seedNone);
  await sleep(3600);
  const s12 = await c5.pg.evaluate(() => ({ closed: window.__natives.filter(n => n.fn === 'closeCardWindow').length, open: !!document.querySelector('.crv-ext.open') }));
  chk(s12.closed >= 1 && !s12.open, '12 CARD: a host that cannot mount (slot answers no mode) → the card page CLOSES ITS WINDOW within 3.5 s', 'closeCardWindow×' + s12.closed + '  card open ' + s12.open);

  // ═══ CARD-ONLY PAGE · a popped mod curve whose route is removed from the patch: the window closes ═══
  const c6 = await newPage(b, P, '?card=crv', seedMod);
  await sleep(1800);
  const s13a = await c6.pg.evaluate(() => ({ open: !!document.querySelector('.crv-ext.open'), key: window.__crvHostKey() }));
  await c6.pg.evaluate(() => { window.__natives.length = 0; window.__store.mod = '[]'; window.__tiModRestore(); });
  await sleep(2200);   // the 1.5 s local-edit stand-down, then the prune and the close
  await c6.pg.evaluate(() => { window.__tiModRestore(); }); await sleep(600);
  const s13b = await c6.pg.evaluate(() => ({ closed: window.__natives.filter(n => n.fn === 'closeCardWindow').length }));
  chk(s13a.open && s13a.key === 'mod' && s13b.closed === 1, '13 CARD: the popped mod curve\'s route is removed from the patch → the window closes, asked exactly once (never a blank window)', 'was open as ' + s13a.key + '  closeCardWindow×' + s13b.closed);

  // ═══ CARD-ONLY PAGE · no identity = the Distortion ═══
  const c3 = await newPage(b, P, '?card=crv');
  await sleep(1600);
  const s10 = await c3.pg.evaluate(() => { const c = document.querySelector('.ti-popcard.crv-ext'); const st = window.__crvOpenState(); return { open: !!(c && c.classList.contains('open')), st }; });
  chk(s10.open && s10.st.key === 'dst', '10 CARD: no identity boots the Distortion host, as before', '"' + s10.st.title + '"');

  console.log('\n══ RESULT: ' + pass + ' pass, ' + fail + ' FAIL ══\n');
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
