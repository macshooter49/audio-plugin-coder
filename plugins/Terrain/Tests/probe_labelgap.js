// fb454 — THE KNOB → LABEL GAP, by measurement (Max: "I didn't know the letters were that far from
// the knobs... it's like really abnormally far... look at the wavetable position, look at these
// buttons with their letters — you see how they're perfectly aligned under the knob, not too far
// from it... I would like you to do an audit across the board when it comes to the effects and have
// it similar to like the wavetable. For the front and the back panel I want those letters to be
// raised up, of course to the exact length that we have for everything else across the board. The
// effects channel is the only thing that does this.")
//
//   NODE_PATH=<scratchpad>/node_modules node Tests/probe_labelgap.js [page.html]
//
// 🚨 WHAT IS MEASURED, AND WHY IT IS NOT THE BOX. Every dial in this plugin draws a 270° arc with a
// 90° gap at the BOTTOM, so a dial's element box carries dead air under its ink — and that air GROWS
// with the dial: 3.93 px on the synth panel's 24 px ring, 5.67 on the rack's 29 px front dial, 6.26
// on its 32 px back dial, 7.82 on Utility's 40 px one. Equalising `gap` would therefore have left
// four different-looking gaps. So the number is ARC INK → GLYPH INK, read off the RENDERED PNG (the
// fb453 idiom): the lowest painted row inside the dial's box, and the topmost painted row of the
// word beneath it. deviceScaleFactor 4 — deliberately finer than the shipped 2 — so a 0.5 px bar is
// a real bar and not a rounding artefact.
//
// 🚨 THE ANSWER IS QUANTISED TO 1 px. Chrome snaps a glyph baseline to a whole CSS pixel, so sweeping
// the offset in 0.25 px steps moves the measured gap in 1.0 px steps (11.25 → 10.25 → 9.25 → 8.25),
// and HALF-pixel offsets split the back panel's two grid rows a whole pixel apart. Do not re-tune
// these in tenths; re-run the sweep.
//
// THE FIVE BARS:
//   1  THE CANONICAL — the oscillator's own row (WT Pos · Warp · Spectral · Fold · Blur), the row Max
//      pointed at. It is the reference, and it must be found; every other bar is measured against it.
//   2  FRONT — the rack's front knobs, as a family, within 0.5 px of the canonical (and no single
//      knob more than the 1 px raster quantum away).
//   3  BACK — the same for the back knobs, and 3.1 for Utility's dropdown-less 40 px face.
//   4  NOTHING BUT THE WORD MOVED — the compensated shrink: for each family gap + label margin-top +
//      padding-bottom still equals the pre-fb454 gap, so the wrapper's height is unchanged; and
//      fb451's surviving law (the pill row's centre on the dial's centre) still holds on every card.
//   5  fb451's PILL-BOTTOM ALIGNMENT IS RETIRED — the word's ink now sits ABOVE the bottom of the
//      A/B/C/D/S/N row it used to line up with. Max: "that's just not what I [want] anymore."
const puppeteer = require('puppeteer-core');

const P = process.argv[2] || process.env.FX4_UI_PAGE ||
  require('path').join(__dirname,'..')+'/Source/ui/public/index.html';
const KINDS = ['reverb','delay','saturate','granular','tape','flt','cho','fla','pha','eqz','wid','cmp','ott','bod','utl','spl'];
const VW = 820, VH = 656, DSF = 4;      // the shipped window (kBaseW × kBaseH − the capture strip); zoom == 1
// 🚨 THE STATISTIC IS THE FAMILY MEAN, and that is not a bar being lowered to fit. Inside one family
// the per-knob spread is 0.25 px of GLYPH rasterisation (a W and an S do not light the same top row),
// and the canonical itself spans 8.75..9.00 for exactly that reason. "The gap" is the family's mean;
// the per-knob bar is the 1 px quantum, so no single knob can sit a whole landing away unnoticed.
const TOL = 0.5;                        // px, bars 2 + 3 — family mean vs the canonical mean
const TOL_EACH = 1.0;                   // px, bars 2 + 3 — no single knob a whole landing off
const PILL_TOL = 1.0;                   // px, bar 4 — fb451's own number, re-measured at 1560 by probe_centerline, is −0.1
const PRE = {front: 4, back: 5, nodd: 7};   // the gaps this replaced; gap + marginTop + paddingBottom must still sum to them

let bars = [];
function bar(n, ok, label, detail){ bars.push({n, ok, label, detail});
  console.log('  ' + (ok ? 'PASS' : 'FAIL') + '  [' + n + '] ' + label + (detail ? '\n           ' + detail : '')); }
const stat = (a) => { const g = a.slice().sort((x,y)=>x-y);
  return {n:g.length, min:g[0], med:g[Math.floor(g.length/2)], max:g[g.length-1], mean:g.reduce((x,y)=>x+y,0)/g.length}; };
const fmt = (s) => s ? ('min/med/max ' + s.min.toFixed(2)+' / '+s.med.toFixed(2)+' / '+s.max.toFixed(2)+
  '   mean ' + s.mean.toFixed(2) + '  (n='+s.n+')') : '—';

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

/* every knob that is FULLY on screen right now, with the two rects the PNG pass needs */
const HARVEST = (kind) => {
  const ink = (el) => { try { const r = document.createRange(); r.selectNodeContents(el);
      const b = r.getBoundingClientRect(); if (b.width) return b; } catch(e){} return el.getBoundingClientRect(); };
  const vis = (r) => r.width > 0 && r.height > 0 && r.top >= 0 && r.bottom <= innerHeight && r.left >= 2 && r.right <= innerWidth - 2;
  const out = [];
  const add = (fam, who, dEl, lEl, wrap) => { const R = dEl.getBoundingClientRect(), L = ink(lEl);
    if (!vis(R) || !vis(L)) return;
    const cs = getComputedStyle(wrap), ls = getComputedStyle(lEl);
    out.push({fam, who, dial:{l:R.left,r:R.right,t:R.top,b:R.bottom}, lab:{l:L.left,r:L.right,t:L.top,b:L.bottom},
      box: {gap:parseFloat(cs.rowGap)||0, pad:parseFloat(cs.paddingBottom)||0, mt:parseFloat(ls.marginTop)||0,
            h:+wrap.getBoundingClientRect().height.toFixed(3), dialW:+R.width.toFixed(2)}}); };
  if (kind === 'syn') {
    document.querySelectorAll('#syn-panel .knob').forEach(k => { const ring = k.querySelector('.knob-ring'), lab = k.querySelector('.knob-label');
      if (!ring || !lab) return;
      const wt = /SYN_OSC_A_(WT_FRAME|WARP_AMOUNT|SPECTRAL_AMT|FOLD_AMT|FRAME_SPREAD)$/.test(k.getAttribute('data-syn')||'');
      add(wt ? 'canon' : 'syn.other', (k.getAttribute('data-syn')||'?')+'/'+lab.textContent, ring, lab, k); });
    return out; }
  const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === kind); if (i < 0) return out;
  const card = document.querySelectorAll('#syn-panel .fxr-dev')[i]; if (!card) return out;
  card.querySelectorAll('.fxr-knob').forEach(k => { const d = k.querySelector('.fxr-dial'), l = k.querySelector('.fxr-lab');
    if (d && l) add('front', kind+'/'+l.textContent, d, l, k); });
  card.querySelectorAll('.fxr-bk-knob').forEach(k => { const d = k.querySelector('.fxr-dial'), l = k.querySelector('.fxr-lab');
    if (d && l) add(k.closest('.fxr-bk-nodd') ? 'nodd' : 'back', kind+'/'+l.textContent, d, l, k); });
  return out; };

/* the PNG pass: lowest painted row inside the dial box, topmost painted row of the word below it.
   bg is the median of the dial box's four CORNERS — a 270° arc inscribed in a square never reaches
   them, on any card colour (the Utility face is rgb(52,50,77), the rest rgb(26,26,46)). */
const READ = (u, items, D) => new Promise(res => { const im = new Image();
  im.onload = () => { const c = document.createElement('canvas'); c.width = im.width; c.height = im.height;
    const x = c.getContext('2d', {willReadFrequently:true}); x.drawImage(im, 0, 0);
    const dat = x.getImageData(0, 0, c.width, c.height).data, W = c.width;
    const S = (X,Y) => { const i = ((Y*W)+X)*4; return dat[i]+dat[i+1]+dat[i+2]; };
    res(items.map(it => {
      const dl=Math.round(it.dial.l*D), dr=Math.round(it.dial.r*D), dt=Math.round(it.dial.t*D), db=Math.round(it.dial.b*D);
      const cor=[S(dl+1,dt+1),S(dr-2,dt+1),S(dl+1,db-2),S(dr-2,db-2)].sort((a,b)=>a-b);
      const bg=(cor[1]+cor[2])/2, TH=28;                    // fb453's threshold, verbatim
      let arc=null; for (let Y=db-1; Y>=dt; Y--) { let h=false;
        for (let X=dl; X<dr; X++) if (Math.abs(S(X,Y)-bg)>TH) { h=true; break; } if (h) { arc=(Y+1)/D; break; } }
      const ll=Math.round(it.lab.l*D), lr=Math.round(it.lab.r*D), lb=Math.round(it.lab.b*D)+6;
      let gly=null; for (let Y=db; Y<=lb; Y++) { let h=false;
        for (let X=ll; X<lr; X++) if (Math.abs(S(X,Y)-bg)>TH) { h=true; break; } if (h) { gly=Y/D; break; } }
      return {fam:it.fam, who:it.who, box:it.box, arcBot:arc, glyTop:gly,
              gap:(arc!=null&&gly!=null)?+(gly-arc).toFixed(3):null}; })); };
  im.onerror = () => res([]); im.src = u; });

(async () => {
  const b = await puppeteer.launch({executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files']});
  const pg = await b.newPage(); await pg.setViewport({width:VW, height:VH, deviceScaleFactor:DSF});
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0,160)));
  await pg.evaluateOnNewDocument(STUB);
  await pg.goto('file://'+P, {waitUntil:'load', timeout:60000}); await new Promise(r => setTimeout(r, 1600));
  await pg.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
  await new Promise(r => setTimeout(r, 1000));
  const zoom = await pg.evaluate(() => +(getComputedStyle(document.documentElement).zoom||1));

  console.log('\n══ fb454 — THE KNOB → LABEL GAP AUDIT ══');
  console.log('   page ' + P);
  console.log('   viewport ' + VW + '×' + VH + '  deviceScaleFactor ' + DSF + '  page zoom ' + zoom +
              '   (measuring at ' + (1/DSF).toFixed(2) + ' px resolution)\n');

  const rows = [];
  { const items = await pg.evaluate(HARVEST, 'syn');
    const shot = await pg.screenshot({encoding:'base64', clip:{x:0,y:0,width:VW,height:VH}});
    rows.push(...await pg.evaluate(READ, 'data:image/png;base64,'+shot, items, DSF)); }
  await pg.evaluate((K) => { for (const k of K) try { window.__fxAdd(k); } catch(e){} try { window.__fx4Tick(); } catch(e){} }, KINDS);
  await new Promise(r => setTimeout(r, 900));
  for (const open of [false, true]) {
    await pg.evaluate((open) => { document.querySelectorAll('#syn-panel .fxr-dev').forEach(c => { const sw = c.querySelector('.fxr-swap');
      if (sw && c.classList.contains('swapped') !== open) sw.click(); }); }, open);
    await new Promise(r => setTimeout(r, 500));
    for (const kind of KINDS) {
      const ok = await pg.evaluate((kind) => { const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === kind); if (i < 0) return false;
        const card = document.querySelectorAll('#syn-panel .fxr-dev')[i]; if (!card) return false;
        const clip = document.querySelector('.fxr-clip'); clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8; return true; }, kind);
      if (!ok) continue; await new Promise(r => setTimeout(r, 160));
      const items = await pg.evaluate(HARVEST, kind); if (!items.length) continue;
      const shot = await pg.screenshot({encoding:'base64', clip:{x:0,y:0,width:VW,height:VH}});
      rows.push(...await pg.evaluate(READ, 'data:image/png;base64,'+shot, items, DSF));
    }
  }
  const F = (n) => rows.filter(r => r.fam === n && r.gap != null);
  const G = (n) => F(n).map(r => r.gap);

  // ── 1  the canonical ────────────────────────────────────────────────────────────────────────
  const canon = F('canon');
  const cs = canon.length ? stat(G('canon')) : null;
  bar(1, !!cs && cs.n >= 5, 'THE CANONICAL — the oscillator row Max pointed at',
      cs ? ('arc ink → glyph ink  ' + fmt(cs) + '\n           ' +
            canon.map(r => r.who.split('/')[1]+'='+r.gap.toFixed(2)).join('  ') +
            '\n           the rest of the synth panel, for context: ' + fmt(stat(G('syn.other')))) : 'not found');
  const CANON = cs ? cs.mean : 9;

  // ── 2/3  the rack ───────────────────────────────────────────────────────────────────────────
  const famBar = (n, fam, label) => { const a = F(fam); const s = a.length ? stat(G(fam)) : null;
    const dMean = s ? Math.abs(s.mean - CANON) : 99;
    const bad = a.filter(r => Math.abs(r.gap - CANON) > TOL_EACH).sort((x,y) => Math.abs(y.gap-CANON) - Math.abs(x.gap-CANON));
    bar(n, !!s && dMean <= TOL && !bad.length,
        label + ' is within ' + TOL + ' px of the canonical (' + CANON.toFixed(2) + ')',
        s ? (fmt(s) + '\n           Δ mean ' + (s.mean-CANON>=0?'+':'') + (s.mean-CANON).toFixed(2) + ' px' +
             '   worst single knob ' + Math.max(...a.map(r => Math.abs(r.gap-CANON))).toFixed(2) + ' px (bar ' + TOL_EACH + ')' +
             (bad.length ? '\n           OFF: ' + bad.slice(0,6).map(r => r.who+'='+r.gap.toFixed(2)).join('  ') : '')) : 'no knobs measured'); };
  famBar(2, 'front', 'FRONT — the rack front knobs');
  famBar(3, 'back',  'BACK — the rack back knobs');
  famBar(3.1, 'nodd', "UTILITY's dropdown-less back (40 px dials)");

  // ── 4  nothing but the word moved ───────────────────────────────────────────────────────────
  const comp = [];
  for (const [fam, pre] of Object.entries(PRE)) { const a = F(fam); if (!a.length) { comp.push(fam+': not measured'); continue; }
    const bx = a[0].box, sum = +(bx.gap + bx.mt + bx.pad).toFixed(3);
    comp.push(fam + ': gap ' + bx.gap + ' + margin ' + bx.mt + ' + pad ' + bx.pad + ' = ' + sum + ' (was ' + pre + ')' + (Math.abs(sum-pre) > 0.01 ? '  ✗' : ''));
    if (Math.abs(sum - pre) > 0.01) comp.push('  !! the wrapper changed height — the dial, the row and the card move with it'); }
  const compOK = !comp.some(t => /✗|not measured/.test(t));
  const chassis = [], rise = [];
  await pg.evaluate(() => { document.querySelectorAll('#syn-panel .fxr-dev').forEach(c => { const sw = c.querySelector('.fxr-swap');
    if (sw && c.classList.contains('swapped')) sw.click(); }); });
  await new Promise(r => setTimeout(r, 400));
  for (const kind of KINDS) {                       /* one card at a time: at 820 wide only ~2 fit */
    const got = await pg.evaluate((kind) => { const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === kind); if (i < 0) return null;
      const card = document.querySelectorAll('#syn-panel .fxr-dev')[i]; if (!card) return null;
      const clip = document.querySelector('.fxr-clip'); clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;
      const d = card.querySelector('.fxr-knobs .fxr-dial'), p = card.querySelector('.fxr-pills .fxr-pill'),
            l = card.querySelector('.fxr-knobs .fxr-lab'), rt = card.querySelector('.fxr-route');
      if (!d || !p || !l) return null;
      const Dr = d.getBoundingClientRect(), Pr = p.getBoundingClientRect();
      const rg = document.createRange(); rg.selectNodeContents(l); const Lr = rg.getBoundingClientRect();
      const Rr = rt ? rt.getBoundingClientRect() : null;
      if (!Dr.width || !Pr.width || !Lr.width) return null;
      return {core:kind, pill:+((Dr.top+Dr.bottom)/2 - (Pr.top+Pr.bottom)/2).toFixed(2),
              rise: Rr && Rr.width ? +(Rr.bottom - Lr.bottom).toFixed(2) : null}; }, kind);
    if (!got) continue; chassis.push({core:got.core, d:got.pill});
    if (got.rise != null) rise.push({core:got.core, d:got.rise}); }
  const worstPill = chassis.reduce((m,r) => Math.abs(r.d) > Math.abs(m.d) ? r : m, {core:'—', d:0});
  bar(4, compOK && Math.abs(worstPill.d) <= PILL_TOL,
      'NOTHING BUT THE WORD MOVED — the compensated shrink, and fb451 still holds',
      comp.join('\n           ') + '\n           fb451, the law that SURVIVES: the pill row centres on the dial centre — worst ' +
      worstPill.d.toFixed(2) + ' px (' + worstPill.core + ', ' + chassis.length + ' cards, bar ' + PILL_TOL + ')');

  // ── 5  fb451's pill-BOTTOM alignment is retired ─────────────────────────────────────────────
  const minRise = rise.length ? Math.min(...rise.map(r => r.d)) : -1;
  bar(5, rise.length >= KINDS.length - 1 && minRise > 0,
      "fb451's pill-bottom alignment is RETIRED — the word's ink clears the A/B/C/D/S/N row",
      rise.length ? ('the route row\'s bottom is now ' + minRise.toFixed(2) + '..' + Math.max(...rise.map(r=>r.d)).toFixed(2) +
        ' px BELOW the word\'s ink on ' + rise.length + ' cards (it used to end level with it)') : 'no cards measured');

  if (errs.length) console.log('\n   page errors: ' + errs.slice(0,3).join(' | '));
  const bad = bars.filter(x => !x.ok);
  console.log('\n  ' + (bad.length ? '❌ ' + bad.length + ' of ' + bars.length + ' bars FAIL' : '✅ all ' + bars.length + ' bars PASS'));
  await b.close();
  process.exit(bad.length ? 1 : 0);
})().catch(e => { console.error('FAILED', e.stack||e.message); process.exit(1); });
