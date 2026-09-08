// fxmod_underline — THE LIVING UNDERLINE ON EVERY FX EXTENSION-CARD CONTROL (lane B).
//
// Max: the synth page has the living underline — a streak of light under a modulated control
// showing the live LFO/env position and depth. Every mod-assigned control on the FX rack's
// cards (front dials AND the back/extension panels) must carry the same grammar: mark on
// assign, gone on delete, comet rides the live feed, depth drag with the shared attenuator,
// X delete via the shared route list — and THE ATTENUATOR AND ITS X ARE NEVER CLIPPED OR
// COVERED (a named Max complaint, fb455's bottom-edge cousin included).
//
//   NODE_PATH=<scratchpad>/node_modules node Tests/fxmod_underline.js [page.html]
//
// 🚨 THE VIEWPORT IS THE SHIPPED ONE (fb454's law): 820 × 656. Taller and every containment
// bar passes vacuously.
//
// THE BARS
//   1  COVERAGE — all 16 kinds × both faces: every [data-mod-dest] cell paints a mark on
//      assign (184 surfaces when every kind is racked), and the marks come DOWN on delete.
//   2  LIVE — the comet tracks an injected __modViz LFO feed (and an ENV feed) through
//      __mvLfoValAt: position scales with depth, direction follows the value.
//   3  ANCHOR LAW — the depth territory obeys (1−d)·knob (the ~9162 law): span.left =
//      (1−d)·knobNorm·barWidth, span.width = d·barWidth, knobNorm from the rack's own model.
//   4  DEPTH DRAG + X — a vertical drag on the mark changes the route's depth (live in
//      __tiRoutes), and the route list's ✕ really deletes (route gone, mark gone).
//   5  CONTAINMENT — the attenuator meter, its readout head, and the route list are fully
//      inside the viewport at the extremes (left edge / right edge / bottom back row), AND
//      the meter stays ADJACENT to the visible part of a clip-trimmed mark instead of
//      popping at the full word box that scrolled out of view.
//   6  LIFECYCLE — the mark survives a real grip-drag reorder (elements are re-created:
//      fb374 live-node law), lands on the right card with two instances racked, and a
//      removed device prunes its marks.
//   7  CENSUS — painting marks adds ZERO setInterval registrations and does not grow the
//      frame rate: no per-mark self-loops (fb511/fb505 law).
//
// PROOF THE BARS CAN FAIL (fb421 — a gate that has never failed has never been tested):
//   · bar 5's straddle case FAILS against the pre-fix page (meter 27 px past the visible
//     mark; recorded in the lane report).
//   · FXUL_MUTATE=1  injects `.sm-ul{display:none!important}` → bars 1/2/3/4 fail.
//   · FXUL_MUTATE=2  blinds the fb455 clamp's __vh (returns the 656 design height while the
//     real window is 560) → bar 5's bottom-row containment fails, which is exactly the
//     pre-fb455 bug reproduced.
//   · FXUL_MUTATE=3  strips data-mod-dest from the re-rendered cards after the reorder →
//     bar 6 fails (the fb374 shape: a cache keyed to a dead node).
//   · FXUL_MUTATE=4  makes each added route start a naive 60 Hz setInterval → bar 7 fails.
const puppeteer = require('puppeteer-core');
const P = process.argv[2] || process.env.FXUL_PAGE ||
  require('path').join(__dirname,'..')+'/Source/ui/public/index.html';
const MUT = +(process.env.FXUL_MUTATE||0);
const KINDS = ['reverb','delay','saturate','granular','tape','flt','cho','fla','pha','eqz','wid','cmp','ott','bod','utl','spl'];
const VW = 820, VH = (MUT===2?560:656), DSF = 2;

let pass = 0, fail = 0;
function chk(ok, label, detail){ if (ok) { pass++; console.log('  ok    ' + label + (detail ? '   ' + detail : '')); }
  else { fail++; console.log('  FAIL  ' + label + (detail ? '   ' + detail : '')); } }

const STUB = () => {
  window.__PMAP = {};
  const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
    getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
      if(/Json|JSON/.test(n))return r('{}'); r(0);}),
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
  /* synthetic PointerEvents carry no active pointer; the ul drag calls setPointerCapture */
  Element.prototype.setPointerCapture = function(){};
  Element.prototype.releasePointerCapture = function(){};
  /* census instrumentation (bar 7) */
  window.__censusSI = 0; const oSI = window.setInterval;
  window.setInterval = function(){ window.__censusSI++; return oSI.apply(window, arguments); };
  /* mutations (see header) */
  const MUT = +(window.__FXUL_MUTATE||0);
  if (MUT === 2)
    window.addEventListener('load', () => { window.__vh = function(){ return 656; }; });
};

/* find the visible mark sitting on a cell's word (helper evaluated in-page) */
const HELPERS = () => {
  window.__ulFind = function(cell){
    if (!cell) return null;
    const lb = cell.querySelector('.fxr-lab') || cell;
    const rg = document.createRange(); rg.selectNodeContents(lb);
    const r = rg.getBoundingClientRect(); if (!r.width) return null;
    return [...document.querySelectorAll('.sm-ul')].filter(x => x.style.display !== 'none')
      .find(x => { const q = x.getBoundingClientRect();
        return Math.abs(q.left - r.left) < 5 && Math.abs(q.top - (r.bottom - 1)) < 5; }) || null;
  };
  window.__ulLfoFeed = function(v){
    const a = new Array(10).fill(0); a[0] = v;
    window.__modViz(null, a, null); window.__mvLfoValTPrev = Date.now() - 33;
  };
  window.__ulEnvFeed = function(v){
    const e = [v, -1, -1, -1, -1]; window.__modViz(e, null, null);
  };
  window.__ulScrollTo = function(card, mode){
    const clip = document.querySelector('.fxr-clip'), cr = clip.getBoundingClientRect();
    if (mode === 1) clip.scrollLeft = card.offsetLeft - clip.offsetLeft - (cr.width - card.offsetWidth) + 8;
    else clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;
  };
};

(async () => {
  const b = await puppeteer.launch({executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files']});
  const pg = await b.newPage(); await pg.setViewport({width:VW, height:VH, deviceScaleFactor:DSF});
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0,160)));
  await pg.evaluateOnNewDocument(`window.__FXUL_MUTATE=${MUT};`);
  await pg.evaluateOnNewDocument(STUB);
  await pg.evaluateOnNewDocument(HELPERS);
  await pg.goto('file://'+P, {waitUntil:'load', timeout:60000}); await new Promise(r => setTimeout(r, 1600));
  await pg.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
  await new Promise(r => setTimeout(r, 800));
  if (MUT === 1) await pg.evaluate(() => { const s = document.createElement('style');
    s.textContent = '.sm-ul{display:none!important}'; document.head.appendChild(s); });
  if (MUT === 4) await pg.evaluate(() => { const o = window.__tiAddRoute;
    window.__tiAddRoute = function(){ setInterval(function(){}, 16); return o.apply(this, arguments); }; });
  await pg.evaluate((K) => { for (const k of K) try { window.__fxAdd(k); } catch(e){} try { window.__fx4Tick(); } catch(e){} }, KINDS);
  await new Promise(r => setTimeout(r, 1200));

  const vh = await pg.evaluate(() => window.__vh()), vw = await pg.evaluate(() => window.__vw());
  console.log('\n══ THE LIVING UNDERLINE ON THE FX RACK — ' + (MUT ? ('MUTATION '+MUT) : 'the shipped page') + ' ══');
  console.log('   page ' + P);
  console.log('   viewport ' + VW + '×' + VH + '   __vw()=' + vw + ' __vh()=' + vh + '\n');
  if (vh > 700) console.log('   ⚠️  __vh() > 700 — containment bars would pass vacuously.\n');

  // ── 1  COVERAGE: all kinds, both faces ─────────────────────────────────────
  {
    let totalDests = 0, totalMarks = 0, misses = [];
    for (let i = 0; i < KINDS.length; i++) {
      for (const back of [false, true]) {
        const res = await pg.evaluate((kind, back) => {
          window.__tiPruneFxRoutes(0, 1e9);
          const D = window.__fxrDevs(); const di = D.findIndex(x => x.core === kind); if (di < 0) return {err:'no dev'};
          const card = document.querySelectorAll('#syn-panel .fxr-dev')[di]; if (!card) return {err:'no card'};
          const sw = card.querySelector('.fxr-swap');
          if (sw && card.classList.contains('swapped') !== back) sw.click();
          if (back && !card.querySelector('.fxr-bk-knob')) return {skip:1};
          window.__ulScrollTo(card, 0);
          const dests = [...card.querySelectorAll((back?'.fxr-bk-knob':'.fxr-knob')+'[data-mod-dest]')]
            .map(c => +c.getAttribute('data-mod-dest'));
          for (const d of dests) window.__tiAddRoute(0, 1, d);
          window.__selMod = {lfo:1};
          return {dests};
        }, KINDS[i], back);
        if (res.err) { misses.push(KINDS[i]+(back?'/back: ':'/front: ')+res.err); continue; }
        if (res.skip) continue;
        await new Promise(r => setTimeout(r, 350));
        const got = await pg.evaluate((kind, dests) => {
          const D = window.__fxrDevs(); const di = D.findIndex(x => x.core === kind);
          const card = document.querySelectorAll('#syn-panel .fxr-dev')[di];
          let n = 0; const miss = [];
          for (const d of dests) { const c = card.querySelector('[data-mod-dest="'+d+'"]');
            if (c && window.__ulFind(c)) n++; else miss.push(d); }
          window.__tiPruneFxRoutes(0, 1e9);
          return {n, miss};
        }, KINDS[i], res.dests);
        totalDests += res.dests.length; totalMarks += got.n;
        if (got.miss.length) misses.push(KINDS[i]+(back?'/back ':'/front ')+'missing '+JSON.stringify(got.miss));
      }
    }
    chk(totalDests > 0 && totalMarks === totalDests && !misses.length,
      'COVERAGE — every routable cell on every kind/face paints a mark on assign',
      'marks ' + totalMarks + '/' + totalDests + (misses.length ? '  ' + misses.join(' | ') : ''));
    await new Promise(r => setTimeout(r, 350));
    const downAll = await pg.evaluate(() =>
      [...document.querySelectorAll('.sm-ul')].filter(x => x.style.display !== 'none').length);
    chk(downAll === 0, 'COVERAGE — every mark comes DOWN on delete', 'marks still up: ' + downAll);
  }

  // ── 2  LIVE: the comet tracks the injected feeds ───────────────────────────
  {
    const o = await pg.evaluate(async () => {
      window.__tiPruneFxRoutes(0, 1e9);
      const D = window.__fxrDevs(); const di = D.findIndex(x => x.core === 'wid');
      const card = document.querySelectorAll('#syn-panel .fxr-dev')[di];
      if (card.classList.contains('swapped')) card.querySelector('.fxr-swap').click();
      window.__ulScrollTo(card, 0);
      const cell = card.querySelector('.fxr-knob[data-mod-dest]');
      const d = +cell.getAttribute('data-mod-dest');
      window.__tiAddRoute(0, 1, d); window.__selMod = {lfo:1};
      const read = () => { const u = window.__ulFind(cell); if (!u) return null;
        const cm = u.children[2];
        return {gw: u.offsetWidth, on: cm.style.display !== 'none',
          l: parseFloat(cm.style.left)||0, w: parseFloat(cm.style.width)||0}; };
      window.__ulLfoFeed(-1); await new Promise(r => setTimeout(r, 250)); const lo = read();
      window.__ulLfoFeed(1);  await new Promise(r => setTimeout(r, 250)); const hi = read();
      const dep = (window.__tiRoutes().find(r => r.d === d)||{}).v || 0.5;
      window.__tiPruneFxRoutes(0, 1e9);
      return {lo, hi, dep};
    });
    const ok = o.lo && o.hi && o.lo.on && o.hi.on &&
      ((o.hi.l + o.hi.w) - (o.lo.l + o.lo.w)) > 0.5 * o.dep * o.lo.gw;
    chk(ok, 'LIVE — the comet follows an injected LFO feed across the mark',
      o.lo && o.hi ? ('head moved ' + ((o.hi.l+o.hi.w)-(o.lo.l+o.lo.w)).toFixed(1) + 'px of ' +
        (o.dep*o.lo.gw).toFixed(1) + 'px territory') : 'no mark/comet');
    const e = await pg.evaluate(async () => {
      window.__tiPruneFxRoutes(0, 1e9);
      const D = window.__fxrDevs(); const di = D.findIndex(x => x.core === 'cho');
      const card = document.querySelectorAll('#syn-panel .fxr-dev')[di];
      if (card.classList.contains('swapped')) card.querySelector('.fxr-swap').click();
      window.__ulScrollTo(card, 0);
      const cell = card.querySelector('.fxr-knob[data-mod-dest]');
      const d = +cell.getAttribute('data-mod-dest');
      window.__tiAddRoute(1, 0, d); window.__selMod = {env:1};
      const read = () => { const u = window.__ulFind(cell); if (!u) return null;
        const cm = u.children[2];
        return {sel: u.classList.contains('sel'), on: cm.style.display !== 'none',
          l: parseFloat(cm.style.left)||0, w: parseFloat(cm.style.width)||0}; };
      window.__ulEnvFeed(0.05); await new Promise(r => setTimeout(r, 250)); const lo = read();
      window.__ulEnvFeed(0.95); await new Promise(r => setTimeout(r, 250)); const hi = read();
      window.__tiPruneFxRoutes(0, 1e9);
      return {lo, hi};
    });
    chk(e.lo && e.hi && e.lo.sel && e.hi.on && ((e.hi.l+e.hi.w) > (e.lo.l+e.lo.w)),
      'LIVE — the comet follows an injected ENV feed (selected = purple)',
      e.lo && e.hi ? ('sel=' + e.lo.sel + ' head ' + (e.lo.l+e.lo.w).toFixed(1) + ' → ' + (e.hi.l+e.hi.w).toFixed(1)) : 'no mark/comet');
  }

  // ── 3  ANCHOR LAW: territory at (1−d)·knob ─────────────────────────────────
  {
    const o = await pg.evaluate(async () => {
      window.__tiPruneFxRoutes(0, 1e9);
      const D = window.__fxrDevs(); const di = D.findIndex(x => x.core === 'ott');
      const card = document.querySelectorAll('#syn-panel .fxr-dev')[di];
      if (!card.classList.contains('swapped')) card.querySelector('.fxr-swap').click();
      window.__ulScrollTo(card, 0);
      const cell = card.querySelector('.fxr-bk-knob[data-mod-dest]');
      const d = +cell.getAttribute('data-mod-dest');
      window.__tiAddRoute(0, 1, d); window.__selMod = {lfo:1};
      await new Promise(r => setTimeout(r, 350));
      const u = window.__ulFind(cell); if (!u) return {err:'no mark'};
      const sp = u.children[1], gw = u.offsetWidth;
      const kn = window.__fxModKnobNorm(d);
      const dep = (window.__tiRoutes().find(r => r.d === d)||{}).v;
      const out = {gw, kn, dep, l: parseFloat(sp.style.left), w: parseFloat(sp.style.width),
        expL: (1-Math.abs(dep))*kn*gw, expW: Math.max(0.02,Math.abs(dep))*gw};
      window.__tiPruneFxRoutes(0, 1e9);
      return out;
    });
    chk(!o.err && Math.abs(o.l - o.expL) < 1.5 && Math.abs(o.w - o.expW) < 1.5,
      'ANCHOR LAW — span at (1−d)·knob·width, width d·width (the ~9162 law)',
      o.err || ('span ' + o.l.toFixed(1) + '/' + o.w.toFixed(1) + ' expected ' +
        o.expL.toFixed(1) + '/' + o.expW.toFixed(1) + '  (knob ' + o.kn + ', depth ' + o.dep + ')'));
  }

  // ── 4  DEPTH DRAG + THE X ──────────────────────────────────────────────────
  {
    const o = await pg.evaluate(async () => {
      window.__tiPruneFxRoutes(0, 1e9);
      const D = window.__fxrDevs(); const di = D.findIndex(x => x.core === 'bod');
      const card = document.querySelectorAll('#syn-panel .fxr-dev')[di];
      if (!card.classList.contains('swapped')) card.querySelector('.fxr-swap').click();
      window.__ulScrollTo(card, 0);
      const cell = card.querySelector('.fxr-bk-knob[data-mod-dest]');
      const d = +cell.getAttribute('data-mod-dest');
      window.__tiAddRoute(0, 1, d); window.__selMod = {lfo:1};
      await new Promise(r => setTimeout(r, 350));
      const u = window.__ulFind(cell); if (!u) return {err:'no mark'};
      const ur = u.getBoundingClientRect();
      const d0 = (window.__tiRoutes().find(r => r.d === d)||{}).v;
      const sp0 = {l: parseFloat(u.children[1].style.left), w: parseFloat(u.children[1].style.width)};
      const cx = ur.left + ur.width/2;
      u.dispatchEvent(new PointerEvent('pointerdown', {bubbles:true, clientX:cx, clientY:ur.top+3, pointerId:21}));
      await new Promise(r => setTimeout(r, 60));
      const attOn = document.querySelector('.sm-att').classList.contains('on');
      document.dispatchEvent(new PointerEvent('pointermove', {bubbles:true, clientX:cx, clientY:ur.top+3-30, pointerId:21}));
      await new Promise(r => setTimeout(r, 120));
      const d1 = (window.__tiRoutes().find(r => r.d === d)||{}).v;
      const sp1 = {l: parseFloat(u.children[1].style.left), w: parseFloat(u.children[1].style.width)};
      document.dispatchEvent(new PointerEvent('pointerup', {bubbles:true, clientX:cx, clientY:ur.top-27, pointerId:21}));
      await new Promise(r => setTimeout(r, 120));
      return {d0, d1, sp0, sp1, attOn, dest:d};
    });
    chk(!o.err && o.attOn && o.d1 > o.d0 + 0.15 && (o.sp1.w > o.sp0.w + 1 || o.sp1.l < o.sp0.l - 1),
      'DEPTH DRAG — 30px up ⇒ depth rises (attenuator shown) and the territory grows',
      o.err || ('depth ' + o.d0 + ' → ' + (+o.d1).toFixed(2) + '  span ' + o.sp0.l.toFixed(1) + '/' + o.sp0.w.toFixed(1) +
        ' → ' + o.sp1.l.toFixed(1) + '/' + o.sp1.w.toFixed(1) + '  att=' + o.attOn));
    const x = o.err ? {err:'skipped — '+o.err} : await pg.evaluate(async (dest) => {
      const cell = document.querySelector('#syn-panel [data-mod-dest="'+dest+'"]');
      const u = window.__ulFind(cell); if (!u) return {err:'no mark'};
      u.dispatchEvent(new MouseEvent('mouseenter', {bubbles:false}));
      await new Promise(r => setTimeout(r, 60));
      const m = document.querySelector('.sm-routes'); if (!m) return {err:'no list'};
      const rx = m.querySelector('.rx'); if (!rx) return {err:'no X'};
      rx.click();
      await new Promise(r => setTimeout(r, 350));
      const gone = !window.__tiRoutes().some(r => r.d === dest);
      const markGone = !window.__ulFind(cell);
      window.__tiPruneFxRoutes(0, 1e9);
      return {gone, markGone};
    }, o.dest);
    chk(!x.err && x.gone && x.markGone, 'THE X — the route list ✕ deletes the route and drops the mark',
      x.err || ('route gone=' + x.gone + ' mark gone=' + x.markGone));
  }

  // ── 5  CONTAINMENT at the extremes ─────────────────────────────────────────
  {
    const CASES = [['LEFT edge, front row','reverb',false,0], ['RIGHT edge, front row','spl',false,1],
                   ['LEFT edge, back BOTTOM row','delay',true,0], ['RIGHT edge, back BOTTOM row','spl',true,1]];
    for (const [name, kind, back, mode] of CASES) {
      const o = await pg.evaluate(async (kind, back, mode) => {
        window.__tiPruneFxRoutes(0, 1e9);
        const D = window.__fxrDevs(); const di = D.findIndex(x => x.core === kind); if (di<0) return {err:'no dev'};
        const card = document.querySelectorAll('#syn-panel .fxr-dev')[di];
        const sw = card.querySelector('.fxr-swap');
        if (sw && card.classList.contains('swapped') !== back) sw.click();
        window.__ulScrollTo(card, mode);
        const cr = document.querySelector('.fxr-clip').getBoundingClientRect();
        let cand = [...card.querySelectorAll((back?'.fxr-bk-knob':'.fxr-knob')+'[data-mod-dest]')]
          .map(k => ({k, r:k.getBoundingClientRect()}))
          .filter(o => o.r.width && o.r.left >= cr.left && o.r.right <= cr.right);
        if (!cand.length) return {err:'no visible knob'};
        if (back) { const bot = Math.max(...cand.map(o=>o.r.bottom)); cand = cand.filter(o=>o.r.bottom>bot-2); }
        const pick = (mode===1) ? cand[cand.length-1] : cand[0];
        const d = +pick.k.getAttribute('data-mod-dest');
        window.__tiAddRoute(0, 1, d); window.__selMod = {lfo:1};
        await new Promise(r => setTimeout(r, 350));
        const u = window.__ulFind(pick.k); if (!u) return {err:'no mark'};
        const ur = u.getBoundingClientRect();
        u.dispatchEvent(new PointerEvent('pointerdown', {bubbles:true, clientX:ur.left+ur.width/2, clientY:ur.top+3, pointerId:31}));
        await new Promise(r => setTimeout(r, 80));
        const att = document.querySelector('.sm-att'), on = att.classList.contains('on');
        const ar = on ? att.getBoundingClientRect() : null;
        const vv = on ? att.querySelector('.vv').getBoundingClientRect() : null;
        document.dispatchEvent(new PointerEvent('pointerup', {bubbles:true, pointerId:31}));
        /* the route list too: its box (and so its ✕ column) must be on screen */
        u.dispatchEvent(new MouseEvent('mouseenter', {bubbles:false}));
        await new Promise(r => setTimeout(r, 60));
        const m = document.querySelector('.sm-routes');
        const mr = m ? m.getBoundingClientRect() : null;
        if (m) m.remove();
        window.__tiPruneFxRoutes(0, 1e9);
        /* GROUND TRUTH, not __vh(): mutation 2 blinds the clamp's __vh, and a gate that
           reads the same lie would pass right through the bug it exists to catch */
        const z = window.__zoomFix || 1;
        const vwl = window.innerWidth / z, vhl = window.innerHeight / z;
        const inside = q => q && q.left >= 0 && q.top >= 0 && q.right <= vwl + 0.01 && q.bottom <= vhl + 0.01;
        return {on, att: ar && [ar.left,ar.top,ar.right,ar.bottom].map(x=>+x.toFixed(1)),
          attIn: inside(ar), vvIn: inside(vv), listIn: inside(mr),
          list: mr && [mr.left,mr.top,mr.right,mr.bottom].map(x=>+x.toFixed(1))};
      }, kind, back, mode);
      chk(!o.err && o.on && o.attIn && o.vvIn && o.listIn,
        'CONTAINMENT — ' + name + ': meter + readout + route list fully on screen',
        o.err || ('att ' + JSON.stringify(o.att) + ' in=' + o.attIn + ' vv=' + o.vvIn +
          '  list ' + JSON.stringify(o.list) + ' in=' + o.listIn));
    }
    /* the STRADDLE: a word trimmed by the rack clip — the meter must hug the VISIBLE mark,
       never pop at the full word box that scrolled out of view. */
    const s = await pg.evaluate(async () => {
      window.__tiPruneFxRoutes(0, 1e9);
      const clip = document.querySelector('.fxr-clip'), cr = clip.getBoundingClientRect();
      const D = window.__fxrDevs(); const di = D.findIndex(x => x.core === 'ott');
      const card = document.querySelectorAll('#syn-panel .fxr-dev')[di];
      if (!card.classList.contains('swapped')) card.querySelector('.fxr-swap').click();
      const cells = [...card.querySelectorAll('.fxr-bk-knob[data-mod-dest]')];
      const lbOf = c => c.querySelector('.fxr-lab');
      cells.sort((a,b) => lbOf(b).getBoundingClientRect().right - lbOf(a).getBoundingClientRect().right);
      const cell = cells[0], d = +cell.getAttribute('data-mod-dest');
      const rg = document.createRange(); rg.selectNodeContents(lbOf(cell));
      let r = rg.getBoundingClientRect();
      /* put the word 20 px PAST the clip's right edge — trimmed, part still visible.
         scrollLeft moves content LEFT, so the delta that leaves the word's right edge at
         clip.right + 20 is (wordRight − clipRight) − 20 */
      clip.scrollLeft += (r.right - cr.right) - 20;
      await new Promise(rr => setTimeout(rr, 120));
      r = rg.getBoundingClientRect();
      window.__tiAddRoute(0, 1, d); window.__selMod = {lfo:1};
      await new Promise(rr => setTimeout(rr, 350));
      const u = window.__ulFind(cell); if (!u) return {err:'no mark'};
      const ur = u.getBoundingClientRect();
      const visR = Math.min(ur.right, cr.right);
      if (ur.right <= cr.right + 10) return {err:'word did not straddle (right ' + ur.right.toFixed(1) + ' clip ' + cr.right.toFixed(1) + ')'};
      u.dispatchEvent(new PointerEvent('pointerdown', {bubbles:true, clientX:visR-3, clientY:ur.top+3, pointerId:32}));
      await new Promise(rr => setTimeout(rr, 80));
      const att = document.querySelector('.sm-att'), on = att.classList.contains('on');
      const ar = on ? att.getBoundingClientRect() : null;
      document.dispatchEvent(new PointerEvent('pointerup', {bubbles:true, pointerId:32}));
      window.__tiPruneFxRoutes(0, 1e9);
      return {on, wordR:+ur.right.toFixed(1), clipR:+cr.right.toFixed(1), visR:+visR.toFixed(1),
        att: ar && [ar.left,ar.top,ar.right,ar.bottom].map(x=>+x.toFixed(1)),
        gap: ar ? +(ar.left - visR).toFixed(1) : null};
    });
    chk(!s.err && s.on && s.gap != null && s.gap >= 0 && s.gap <= 15,
      'CONTAINMENT — trimmed mark: the meter hugs the VISIBLE edge (gap 0..15px)',
      s.err || ('word right ' + s.wordR + ' clip right ' + s.clipR + ' visible ' + s.visR +
        '  meter ' + JSON.stringify(s.att) + '  gap ' + s.gap + 'px'));
  }

  // ── 6  LIFECYCLE: reorder, second instance, remove ─────────────────────────
  {
    const o = await pg.evaluate(async (MUT) => {
      window.__tiPruneFxRoutes(0, 1e9);
      const d = window.__fxModDest('wid', 1, 0);
      let cell = document.querySelector('#syn-panel [data-mod-dest="'+d+'"]');
      const card = cell.closest('.fxr-dev');
      window.__ulScrollTo(card, 0);
      window.__tiAddRoute(0, 1, d); window.__selMod = {lfo:1};
      await new Promise(r => setTimeout(r, 350));
      if (card.classList.contains('swapped')) card.querySelector('.fxr-swap').click();
      const before = !!window.__ulFind(cell);
      const elBefore = cell;
      /* the real grip drag, to the head of the rack */
      const grip = card.querySelector('.fxr-grip');
      const gr = grip.getBoundingClientRect();
      grip.dispatchEvent(new PointerEvent('pointerdown', {bubbles:true, clientX:gr.left+2, clientY:gr.top+2, pointerId:41}));
      document.querySelector('.fxr-clip').scrollLeft = 0;
      await new Promise(r => setTimeout(r, 60));
      const first = document.querySelectorAll('#syn-panel .fxr-dev')[0].getBoundingClientRect();
      document.dispatchEvent(new PointerEvent('pointermove', {bubbles:true, clientX:first.left+4, clientY:first.top+10, pointerId:41}));
      await new Promise(r => setTimeout(r, 60));
      document.dispatchEvent(new PointerEvent('pointerup', {bubbles:true, clientX:first.left+4, clientY:first.top+10, pointerId:41}));
      await new Promise(r => setTimeout(r, 500));
      if (MUT === 3) document.querySelectorAll('#syn-panel .fxr-dev [data-mod-dest]')
        .forEach(n => n.removeAttribute('data-mod-dest'));
      cell = document.querySelector('#syn-panel [data-mod-dest="'+d+'"]');
      const recreated = cell !== elBefore;
      if (cell) window.__ulScrollTo(cell.closest('.fxr-dev'), 0);
      await new Promise(r => setTimeout(r, 350));
      const after = cell ? !!window.__ulFind(cell) : false;
      const idx = window.__fxrDevs().findIndex(x => x.core === 'wid');
      return {before, recreated, after, idx};
    }, MUT);
    chk(o.before && o.recreated && o.after && o.idx === 0,
      'LIFECYCLE — the mark survives a real grip-drag reorder (elements re-created)',
      'before=' + o.before + ' recreated=' + o.recreated + ' after=' + o.after + ' newIdx=' + o.idx);
    const m2 = await pg.evaluate(async () => {
      window.__tiPruneFxRoutes(0, 1e9);
      window.__fxAdd('reverb'); try { window.__fx4Tick(); } catch(e){}
      await new Promise(r => setTimeout(r, 500));
      const d2 = window.__fxModDest('reverb', 2, 0), d1 = window.__fxModDest('reverb', 1, 0);
      const c2 = document.querySelector('#syn-panel [data-mod-dest="'+d2+'"]');
      if (!c2) return {err:'no inst-2 card'};
      window.__ulScrollTo(c2.closest('.fxr-dev'), 0);
      window.__tiAddRoute(0, 1, d2); window.__selMod = {lfo:1};
      await new Promise(r => setTimeout(r, 350));
      const on2 = !!window.__ulFind(c2);
      const c1 = document.querySelector('#syn-panel [data-mod-dest="'+d1+'"]');
      const on1 = c1 ? !!window.__ulFind(c1) : false;
      /* remove the second instance: its routes must prune, its mark must drop */
      const idx = [...document.querySelectorAll('#syn-panel .fxr-dev')].indexOf(c2.closest('.fxr-dev'));
      const xBtn = document.querySelectorAll('#syn-panel .fxr-dev')[idx].querySelector('[data-act="x"]');
      xBtn.click();
      await new Promise(r => setTimeout(r, 500));
      const routeGone = !window.__tiRoutes().some(r => r.d === d2);
      const up = [...document.querySelectorAll('.sm-ul')].filter(x => x.style.display !== 'none').length;
      window.__tiPruneFxRoutes(0, 1e9);
      return {on2, on1, routeGone, up};
    });
    chk(!m2.err && m2.on2 && !m2.on1 && m2.routeGone && m2.up === 0,
      'LIFECYCLE — instance 2 marks its own card; removing the card prunes route + mark',
      m2.err || ('inst2=' + m2.on2 + ' inst1=' + m2.on1 + ' pruned=' + m2.routeGone + ' marksLeft=' + m2.up));
  }

  // ── 7  CENSUS: marks cost no timers and no extra frames ────────────────────
  {
    const o = await pg.evaluate(async () => {
      window.__tiPruneFxRoutes(0, 1e9);
      const rate = async () => { let n = 0; const t0 = performance.now();
        await new Promise(res => { const c = () => { n++; if (performance.now()-t0 < 800) requestAnimationFrame(c); else res(); };
          requestAnimationFrame(c); });
        return n / 0.8; };
      const si0 = window.__censusSI; const r0 = await rate();
      const D = window.__fxrDevs();
      for (const core of ['reverb','delay','wid','ott']) {
        for (let k = 0; k < 12; k++) { const d = window.__fxModDest(core, 1, k);
          if (d != null) window.__tiAddRoute(0, 1, d); } }
      window.__selMod = {lfo:1};
      await new Promise(r => setTimeout(r, 1200));
      const si1 = window.__censusSI; const r1 = await rate();
      const routes = window.__tiRoutes().length;
      window.__tiPruneFxRoutes(0, 1e9);
      return {si0, si1, r0: +r0.toFixed(0), r1: +r1.toFixed(0), routes};
    });
    chk(o.si1 === o.si0 && o.r1 < o.r0 * 1.3 + 5,
      'CENSUS — ' + o.routes + ' routes painted: zero new setInterval, frame rate flat',
      'setInterval ' + o.si0 + ' → ' + o.si1 + '   rAF ' + o.r0 + '/s → ' + o.r1 + '/s');
  }

  if (errs.length) console.log('\n   page errors: ' + errs.slice(0,4).join(' | '));
  console.log('\n  PASS ' + pass + '   FAIL ' + fail);
  await b.close();
  process.exit(fail ? 1 : 0);
})().catch(e => { console.error('FAILED', e.stack||e.message); process.exit(1); });
