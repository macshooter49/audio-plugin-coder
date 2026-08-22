// fb454 — THE ROUTE LIST MUST NEVER COVER THE CONTROL IT BELONGS TO.
//
// Max: "on the oscillator wavetable... that word says LFO, and I'm able to click it and delete it if
// I want to, it comes at the bottom so it doesn't get in the way of our attenuation. Unfortunately,
// on the front panel because it's so small, our attenuator pops on top. You know where it says LFO 1,
// delete... for the widen, it's like right on top of the mix. So every time I try to attenuate it and
// turn the mix to 100%... I can't attenuate it because it's gonna just keep popping up that extra
// menu." — "make sure that the attenuation, I'm able to use it. It doesn't get in the way of where it
// says click to X, it doesn't get in the way of anything."
//
//   NODE_PATH=<scratchpad>/node_modules node Tests/fxmod_menu.js [page.html]
//
// 🚨 THE VIEWPORT IS THE SHIPPED ONE AND THAT IS THE WHOLE POINT. kBaseW × kBaseH is 820 × 672 and
// the capture strip takes 16 of it, so the WebView is 820 × 656 — and the FX rack sits on its bottom
// edge. Run this at the 760-tall viewport the other rack probes use and every bar passes vacuously:
// there is room below the word, the .pmenu clamp never fires, and the bug does not exist. It only
// exists in the last hundred pixels of the real window.
//
// WHAT IS PROVEN, on a rack card at the LEFT edge, at the RIGHT edge, and mid-SCROLL, front face and
// back face, with one, two and three routes on the knob (the list grows a row at a time, and it was
// the two-row list that Max screenshotted):
//   1  THE WAVETABLE IS UNCHANGED — on a synth-panel knob the list still opens BELOW the word.
//   2  THE LIST MISSES THE KNOB — zero overlap with the destination cell, its dial, or its 7 px
//      .sm-ul drag strip (the strip IS the attenuator: you set depth by dragging it).
//   3  THE ATTENUATOR'S LANE IS CLEAR — the list also misses where showAtt drops the 8 px meter,
//      ul.right + 7, because "it doesn't get in the way of anything" includes that.
//   4  NOTHING IS CLIPPED — the list is fully inside the window, which is the rule the .pmenu clamp
//      existed to keep in the first place.
const puppeteer = require('puppeteer-core');

const P = process.argv[2] || process.env.FX4_UI_PAGE ||
  '/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Source/ui/public/index.html';
const KINDS = ['reverb','delay','saturate','granular','tape','flt','cho','fla','pha','eqz','wid','cmp','ott','bod','utl','spl'];
const VW = 820, VH = 656, DSF = 2;      // the shipped WebView box; page zoom == 1
const ATT_W = 8, ATT_DX = 7;            // .sm-att, and showAtt's offset from the strip's right edge

let pass = 0, fail = 0; const hits = [];
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
};

/* put N routes on one knob of one card at one scroll position, hover its mark, and report every
   overlap the list makes with the things the user has to be able to touch. */
const RUN = (kind, mode, back, nroutes) => {
  window.__tiPruneFxRoutes(0, 1e9);
  const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === kind); if (i < 0) return {err:'no card'};
  const card = document.querySelectorAll('#syn-panel .fxr-dev')[i]; if (!card) return {err:'no card'};
  const sw = card.querySelector('.fxr-swap');
  if (sw && card.classList.contains('swapped') !== back) sw.click();
  if (back && !card.querySelector('.fxr-bk-knob')) return {err:'no back face'};
  const clip = document.querySelector('.fxr-clip'), cr = clip.getBoundingClientRect();
  if (mode === 0)      clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;
  else if (mode === 1) clip.scrollLeft = card.offsetLeft - clip.offsetLeft - (cr.width - card.offsetWidth) + 8;
  else                 clip.scrollLeft = card.offsetLeft - clip.offsetLeft - cr.width / 2;
  const sel = back ? '.fxr-bk-knob[data-mod-dest]' : '.fxr-knob[data-mod-dest]';
  let cand = [...card.querySelectorAll(sel)].map(k => ({k, r:k.getBoundingClientRect()}))
               .filter(o => o.r.width && o.r.left >= cr.left && o.r.right <= cr.right);
  if (back) { const bot = Math.max(...cand.map(o => o.r.bottom));      // the LOWEST row — the hard case
              cand = cand.filter(o => o.r.bottom > bot - 2); }
  if (!cand.length) return {err:'no visible knob'};
  const pick = (mode === 1) ? cand[cand.length-1] : cand[0];
  const dest = +pick.k.getAttribute('data-mod-dest');
  for (let n = 0; n < nroutes; n++) window.__tiAddRoute(n+1, 0, dest);
  window.__selMod = {env:1};
  return {dest, word:(pick.k.querySelector('.fxr-lab')||{}).textContent};
};
/* `sel` overrides the destination lookup: a synth-panel knob is bound through elForDest(), not
   through a data-mod-dest attribute, so bar 1 names its element directly. */
const READ = (dest, ATT_W, ATT_DX, sel) => {
  const ov = (a, b) => { const w = Math.min(a.right,b.right) - Math.max(a.left,b.left),
                               h = Math.min(a.bottom,b.bottom) - Math.max(a.top,b.top);
    return (w > 0 && h > 0) ? +(w*h).toFixed(1) : 0; };
  const cell = document.querySelector(sel || ('#syn-panel [data-mod-dest="' + dest + '"]'));
  if (!cell) return {err:'cell gone'};
  const CR = cell.getBoundingClientRect();
  const u = [...document.querySelectorAll('.sm-ul')].filter(x => x.style.display !== 'none')
              .find(x => { const r = x.getBoundingClientRect();
                return r.left >= CR.left - 4 && r.right <= CR.right + 4 && r.top >= CR.top && r.top <= CR.bottom + 10; });
  if (!u) return {err:'no mark'};
  const UR = u.getBoundingClientRect();
  u.dispatchEvent(new MouseEvent('mouseenter', {bubbles:false}));
  const m = document.querySelector('.sm-routes'); if (!m) return {err:'no list'};
  const MR = m.getBoundingClientRect();
  const dialEl = cell.querySelector('.fxr-dial') || cell.querySelector('.knob-ring');
  const DR = dialEl ? dialEl.getBoundingClientRect() : CR;
  const ATT = {left:UR.right + ATT_DX, right:UR.right + ATT_DX + ATT_W, top:UR.top - 25, bottom:UR.top - 25 + 50};
  const out = {rows:m.children.length, mw:+MR.width.toFixed(1), mh:+MR.height.toFixed(1),
    menu:[+MR.left.toFixed(1),+MR.top.toFixed(1),+MR.right.toFixed(1),+MR.bottom.toFixed(1)],
    cell:[+CR.left.toFixed(1),+CR.top.toFixed(1),+CR.right.toFixed(1),+CR.bottom.toFixed(1)],
    ul:[+UR.left.toFixed(1),+UR.top.toFixed(1),+UR.right.toFixed(1),+UR.bottom.toFixed(1)],
    hitCell:ov(MR,CR), hitDial:ov(MR,DR), hitUl:ov(MR,UR), hitAtt:ov(MR,ATT),
    below: MR.top >= UR.bottom - 0.01,
    beside: MR.right <= CR.left + 0.01 || MR.left >= CR.right - 0.01,
    onScreen: MR.left >= 0 && MR.top >= 0 && MR.right <= window.__vw() + 0.01 && MR.bottom <= window.__vh() + 0.01};
  m.remove();
  return out;
};

(async () => {
  const b = await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files']});
  const pg = await b.newPage(); await pg.setViewport({width:VW, height:VH, deviceScaleFactor:DSF});
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0,160)));
  await pg.evaluateOnNewDocument(STUB);
  await pg.goto('file://'+P, {waitUntil:'load', timeout:60000}); await new Promise(r => setTimeout(r, 1600));
  await pg.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
  await new Promise(r => setTimeout(r, 1000));

  const vh = await pg.evaluate(() => window.__vh()), vw = await pg.evaluate(() => window.__vw());
  console.log('\n══ fb454 — THE ROUTE LIST vs THE KNOB IT BELONGS TO ══');
  console.log('   page ' + P);
  console.log('   viewport ' + VW + '×' + VH + '   __vw()=' + vw + ' __vh()=' + vh + '   (the shipped WebView box)\n');
  if (vh > 700) console.log('   ⚠️  __vh() is bigger than the shipped 656 — these bars would pass vacuously.\n');

  // ── 1  the wavetable, the behaviour Max likes: the list opens BELOW ──────────────────────────
  {
    const WT = '#syn-panel .knob[data-syn="SYN_OSC_A_WT_FRAME"]';
    const dest = await pg.evaluate((WT) => { window.__tiPruneFxRoutes(0, 1e9);
      const k = document.querySelector(WT); if (!k) return null;
      /* dest 2 = SYN_OSC_A_WT_FRAME, the "WT Pos" dial Max screenshotted — bound through
         elForDest(), so it carries no data-mod-dest attribute (probe_modmarks uses the same 2). */
      window.__tiAddRoute(1, 0, 2); window.__tiAddRoute(2, 0, 2);
      window.__selMod = {env:1}; return 2; }, WT);
    if (dest == null) chk(false, 'the wavetable knob is on screen', 'not found — bar 1 cannot run');
    else { await new Promise(r => setTimeout(r, 320));
      const o = await pg.evaluate(READ, dest, ATT_W, ATT_DX, WT);
      chk(!o.err && o.below && !o.hitCell && !o.hitDial && !o.hitUl && o.onScreen,
        'THE WAVETABLE IS UNCHANGED — the list still opens BELOW the word, touching nothing',
        o.err || ('rows=' + o.rows + ' menu ' + JSON.stringify(o.menu) + ' below=' + o.below +
                  ' hits cell/dial/mark ' + o.hitCell + '/' + o.hitDial + '/' + o.hitUl)); }
    await pg.evaluate(() => window.__tiPruneFxRoutes(0, 1e9));
  }

  await pg.evaluate((K) => { for (const k of K) try { window.__fxAdd(k); } catch(e){} try { window.__fx4Tick(); } catch(e){} }, KINDS);
  await new Promise(r => setTimeout(r, 900));

  // ── 2/3/4  the rack, three card positions × two faces × one-to-three routes ──────────────────
  const POS = [['at the LEFT edge','wid',0], ['at the RIGHT edge','wid',1], ['mid-SCROLL','pha',2]];
  for (const [name, kind, mode] of POS) {
    for (const back of [false, true]) {
      for (const nroutes of [1, 2, 3]) {
        const info = await pg.evaluate(RUN, kind, mode, back, nroutes);
        if (info.err) { chk(false, kind + ' ' + (back ? 'BACK' : 'FRONT') + ' ' + name + ', ' + nroutes + ' route(s)', info.err); continue; }
        await new Promise(r => setTimeout(r, 300));
        const o = await pg.evaluate(READ, info.dest, ATT_W, ATT_DX);
        const clean = !o.err && !o.hitCell && !o.hitDial && !o.hitUl && !o.hitAtt && o.onScreen;
        if (!o.err) hits.push({who:kind + '/' + info.word, face:(back?'back':'front'), pos:name, n:nroutes,
          cell:o.hitCell, dial:o.hitDial, ul:o.hitUl, att:o.hitAtt, where:(o.below?'below':(o.beside?'beside':'ON TOP')), on:o.onScreen});
        chk(clean, (back ? 'BACK ' : 'FRONT') + '  ' + kind + '/' + info.word + '  ' + name + ', ' + nroutes + ' route(s)',
          o.err || ('list ' + o.mw + '×' + o.mh + (o.below ? ' BELOW' : (o.beside ? ' BESIDE' : ' ON TOP OF IT')) +
                    '  overlap cell/dial/mark/meter = ' + o.hitCell + '/' + o.hitDial + '/' + o.hitUl + '/' + o.hitAtt + ' px²' +
                    (o.onScreen ? '' : '  ⚠️ CLIPPED ' + JSON.stringify(o.menu))));
      }
    }
  }
  await pg.evaluate(() => window.__tiPruneFxRoutes(0, 1e9));

  console.log('\n── THE OVERLAP TABLE (px², every case) ──');
  console.log('   ' + 'card/word'.padEnd(16) + 'face  position           n  cell   dial   mark   meter  placed');
  hits.forEach(h => console.log('   ' + h.who.padEnd(16) + h.face.padEnd(6) + h.pos.padEnd(19) +
    String(h.n).padEnd(3) + String(h.cell).padEnd(7) + String(h.dial).padEnd(7) + String(h.ul).padEnd(7) +
    String(h.att).padEnd(7) + h.where + (h.on ? '' : '  CLIPPED')));

  if (errs.length) console.log('\n   page errors: ' + errs.slice(0,3).join(' | '));
  console.log('\n  PASS ' + pass + '   FAIL ' + fail);
  await b.close();
  process.exit(fail ? 1 : 0);
})().catch(e => { console.error('FAILED', e.stack||e.message); process.exit(1); });
