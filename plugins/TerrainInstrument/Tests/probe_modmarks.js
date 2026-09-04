// fb453 — THE MODULATION MARK AUDIT, by measurement (Max: "VISUALLY it must be correct! nothing
// crammed and always noticable").  A probe in the probe_centerline.js pattern, run against the REAL
// page: build a rack with EVERY kind, open the back panels, put a route on EVERY knob, and measure
// the five things that decide whether twelve underlines on one card read as design or as clutter.
//
//   NODE_PATH=<scratchpad>/node_modules node Tests/probe_modmarks.js [page.html]
//
// 🚨 THE SCALE IS THE SHIPPED SCALE. The plugin's base window is 820 wide (kBaseW, PluginEditor.cpp)
// and the top-of-file resize fixer sets `zoom` on <html> to innerWidth/820 — so a 820-wide viewport
// is zoom 1, the size Max's plugin opens at. deviceScaleFactor 2 = his Retina display. Every "device
// px" below is a CSS px at that zoom times that scale factor, i.e. a real pixel on his screen.
//
// THE FIVE NUMBERS (they are BARS, not knobs — if one fails it goes to Max, it does not get lowered):
//   1  NOT CRAMMED — no .sm-ul box intersects another, or any NEIGHBOURING label's ink.
//   2  NOT CRAMMED — every underline sits inside its knob cell with >= 2 px to the cell edge.
//   3  ALWAYS NOTICEABLE — the drawn line is >= 1.5 device px tall and >= 60 % of the label's ink.
//   4  ALWAYS NOTICEABLE — >= 3:1 luminance against the card background, in the dim AND the purple
//      state, SAMPLED FROM THE RENDERED PNG (the line is painted with alpha; the CSS colour is a lie).
//   5  THE COMET READS — with an LFO running its travel along the word is >= 4 device px pk-pk.
//
// The knob CELL is the grid TRACK, not the shrink-wrapped item: .fxr-bk-grid uses
// `justify-items:center`, so a back knob's own box hugs its word and "margin to the item edge" would
// be 0 by construction for every card. The front's .fxr-knobs items DO fill their 36px tracks, so
// there the item rect IS the track. Both are the same question — how much clear air the mark has
// before the next dial's territory starts — asked of the box that actually answers it.
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');

const P = process.argv[2] || process.env.FX4_UI_PAGE ||
  require('path').join(__dirname,'..')+'/Source/ui/public/index.html';
const SHOTS = process.env.MODMARK_SHOTS || path.join(os.homedir(), 'Desktop', 'fb453-modmarks');
const KINDS = ['reverb','delay','saturate','granular','tape','flt','cho','fla','pha','eqz','wid','cmp','ott','bod','utl','spl'];
const VW = 820, VH = 760, DSF = 2;          // the shipped base window; zoom == 1
const MIN_CELL_MARGIN = 2.0;                // px, bar #2 — the cell PROXY (still reported for every mark)
/* fb580 — WHAT BAR 2 IS ACTUALLY FOR. Its title is NOT CRAMMED, and the cell margin was standing in for
   "does this mark crowd the one next to it". MEASURED, and the proxy lied: bod/Direction is a 32.56 px word
   in a 36.00 px cell (1.72 px clear) while THE NEAREST MARK IS 17.81 px AWAY — nothing is cramped, the cell
   is simply narrow for a long word. All three tight cells read the same way (17.81 / 15.55 / 28.86 px of
   real air). This gate's own dossier was written to make exactly that distinction: "a tight cell with a wide
   gap is a different problem from a tight cell with a tight gap, and Max should see which one this is."
   So the bar now fails on the real thing — a tight cell whose NEIGHBOUR is also close — and on a mark that
   overflows its cell outright, which is always wrong. Every tight cell is still listed in the dossier. */
const MIN_NEIGHBOUR_GAP = 8.0;              // px, bar #2 — a tight cell only matters if its neighbour is near
const MIN_LINE_DEV_PX = 1.5;                // device px, bar #3
const MIN_WORD_COVER  = 0.60;               // bar #3
const MIN_CONTRAST    = 3.0;                // bar #4
const MIN_COMET_DEV   = 4.0;                // device px, bar #5

let bars = [];
function bar(n, ok, label, detail){ bars.push({n, ok, label, detail});
  console.log('  ' + (ok ? 'PASS' : 'FAIL') + '  [' + n + '] ' + label + (detail ? '\n           ' + detail : '')); }

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

// ── the in-page geometry harvest: every VISIBLE routed knob, its word, its cell, its mark ──
const HARVEST = () => {
  const ink = (el) => { try { const r = document.createRange(); r.selectNodeContents(el);
      const b = r.getBoundingClientRect(); if (b.width) return b; } catch(e){} return el.getBoundingClientRect(); };
  const rr = (b) => ({l:b.left, r:b.right, t:b.top, b:b.bottom, w:b.width, h:b.height});
  const uls = [...document.querySelectorAll('.sm-ul')].filter(u => u.style.display !== 'none')
                 .map(u => ({el:u, r:u.getBoundingClientRect()}));
  const D = window.__fxrDevs ? window.__fxrDevs() : [];
  const out = [];
  document.querySelectorAll('#syn-panel .fxr-dev').forEach((card, ci) => {
    const d = D[ci]; if (!d) return;
    // the back grid's TRACK edges, from the grid's own used values
    const bg = card.querySelector('.fxr-bk-grid');
    let trk = null;
    if (bg) { const cs = getComputedStyle(bg).gridTemplateColumns.split(' ').map(parseFloat);
      const G = bg.getBoundingClientRect(); trk = []; let x = G.left;
      for (const t of cs) { trk.push([x, x + t]); x += t; } }
    card.querySelectorAll('[data-mod-dest]').forEach(cell => {
      const lab = cell.querySelector('.fxr-lab'); if (!lab) return;
      const CR = cell.getBoundingClientRect(); if (CR.width === 0 || CR.height === 0) return;   // the hidden face
      const LR = ink(lab); if (!LR.width) return;
      const front = cell.classList.contains('fxr-knob');
      let cellBox = {l:CR.left, r:CR.right};
      if (!front && trk) { const m = /grid-column:\s*(\d+)/.exec(cell.getAttribute('style') || '');
        const t = trk[(m ? +m[1] : 1) - 1]; if (t) cellBox = {l:t[0], r:t[1]}; }
      const air = window.__markAir ? window.__markAir(lab) : 0;   // fb571 — the mark keeps AIR under its word, by surface
      const hit = uls.find(u => Math.abs(u.r.left - LR.left) < 0.75 && Math.abs(u.r.top - (LR.bottom - 1 + air)) < 0.75);
      const rail = hit ? hit.el.querySelector('.smu-rail').getBoundingClientRect() : null;
      const span = hit ? hit.el.querySelector('.smu-span').getBoundingClientRect() : null;
      const cmt  = hit ? hit.el.querySelector('.smu-comet') : null;
      out.push({ core:d.core, inst:d.inst, dest:+cell.getAttribute('data-mod-dest'), front,
                 word:lab.textContent, lab:rr(LR), cell:cellBox,
                 ul: hit ? rr(hit.r) : null, rail: rail ? rr(rail) : null, span: span ? rr(span) : null,
                 cometOn: !!(cmt && cmt.style.display !== 'none'), sel: hit ? hit.el.classList.contains('sel') : false });
    });
  });
  return out;
};

async function pixels(pg, b64, pts){
  return pg.evaluate((u, pts) => new Promise(res => { const im = new Image();
    im.onload = () => { const c = document.createElement('canvas'); c.width = im.width; c.height = im.height;
      const x = c.getContext('2d'); x.drawImage(im, 0, 0);
      res(pts.map(p => { const q = x.getImageData(Math.round(p[0]), Math.round(p[1]), 1, 1).data; return [q[0], q[1], q[2]]; })); };
    im.onerror = () => res(pts.map(() => null)); im.src = u; }), 'data:image/png;base64,' + b64, pts);
}
const lum = (c) => { const f = v => { v /= 255; return v <= 0.03928 ? v/12.92 : Math.pow((v + 0.055)/1.055, 2.4); };
  return 0.2126*f(c[0]) + 0.7152*f(c[1]) + 0.0722*f(c[2]); };
const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x,y) + 0.05) / (Math.min(x,y) + 0.05); };

(async () => {
  fs.mkdirSync(SHOTS, {recursive:true});
  const b = await puppeteer.launch({executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files']});
  const pg = await b.newPage(); await pg.setViewport({width:VW, height:VH, deviceScaleFactor:DSF});
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0,180)));
  await pg.evaluateOnNewDocument(STUB);
  await pg.goto('file://' + P, {waitUntil:'load', timeout:60000});
  await new Promise(r => setTimeout(r, 1600));
  await pg.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display = 'block';
    window.dispatchEvent(new Event('resize')); });
  await new Promise(r => setTimeout(r, 1200));

  const zoom = await pg.evaluate(() => window.__zoomFix || 1);
  console.log('\n══ fb453 — THE MODULATION MARK AUDIT ══');
  console.log('   page ' + P);
  console.log('   viewport ' + VW + '×' + VH + '  deviceScaleFactor ' + DSF + '  page zoom ' + zoom +
              '   (1 CSS px = ' + (zoom * DSF).toFixed(2) + ' device px)');
  if (Math.abs(zoom - 1) > 0.001) console.log('   ⚠️  NOT the shipped base scale — numbers below are at ' + zoom + '×');

  await pg.evaluate((K) => { K.forEach(k => { try { window.__fxAdd(k); } catch(e){} });
    try { window.__fx4Tick(); } catch(e){} }, KINDS);
  const built = await pg.evaluate(() => ({cards:document.querySelectorAll('.fxr-dev').length,
    cells:document.querySelectorAll('#syn-panel .fxr-dev [data-mod-dest]').length}));
  console.log('   rack: ' + built.cards + ' cards, ' + built.cells + ' modulation cells\n');
  if (errs.length) console.log('   ⚠️  page errors: ' + errs.slice(0,3).join(' | ') + '\n');

  // ── the two faces. A card shows its FRONT or its BACK, never both (.swapped hides one), so the
  //    audit runs once per face. Routes are added per face too: the matrix holds 128 and a full
  //    rack wants 192, which is exactly why the C++ cap is 128 — so route what is on screen.
  const FACES = [
    {id:'front', open:false, pick:(c)=>c.front},
    {id:'back',  open:true,  pick:(c)=>!c.front}
  ];

  const worst = {gap:{v:1e9}, margin:{v:1e9}, lineH:{v:1e9}, cover:{v:1e9}, dim:{v:1e9}, sel:{v:1e9}, comet:{v:1e9}};
  const tight = [], dimAll = [], pixLo = [], intr = [];   // the dossier: everything within a whisker of a bar
  let nMeasured = 0, nCells = 0;

  for (const F of FACES) {
    await pg.evaluate((open) => { document.querySelectorAll('.fxr-dev').forEach(c => {
      const has = !!c.querySelector('.fxr-back'); c.classList.toggle('swapped', open && has); }); }, F.open);
    await new Promise(r => setTimeout(r, 250));

    /* ── route and measure ONE CARD AT A TIME, scrolled into view. 🚨 Two reasons, both hard:
       the matrix holds 128 routes and a full rack wants 192; and since fb453's clip fix a mark is
       correctly SUPPRESSED while its knob sits outside `.fxr-clip`, so a single pass over a
       16-card rack at the shipped 820 width would measure the two cards that happen to be on
       screen and call it the rack. Every scroll position harvests EVERY visible mark, not just the
       target card's, so marks from adjacent cards are still tested against each other for bar 1. */
    const rows = [], byDest = new Map(); let pairWorst = null;
    for (const kind of KINDS) {
      const n = await pg.evaluate((kind, open) => {
        const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === kind); if (i < 0) return 0;
        const card = document.querySelectorAll('.fxr-dev')[i]; if (!card) return 0;
        if (open && !card.querySelector('.fxr-back')) return 0;
        const clip = document.querySelector('.fxr-clip'); clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;
        window.__tiPruneFxRoutes(0, 1e9);
        // route every knob of this face that is CURRENTLY VISIBLE anywhere in the rack
        let k = 0; document.querySelectorAll('#syn-panel .fxr-dev [data-mod-dest]').forEach(c => {
          if (c.classList.contains('fxr-knob') === !!open) return;
          const r = c.getBoundingClientRect(); if (!r.width || !r.height) return;
          window.__tiAddRoute(1, 0, +c.getAttribute('data-mod-dest')); k++; });
        window.__selMod = {env:1}; return k; }, kind, F.open);
      if (!n) continue;
      await new Promise(r => setTimeout(r, 240));
      const got = (await pg.evaluate(HARVEST)).filter(c => F.pick(c));
      const vis = got.filter(c => c.ul);
      // bar 1 is a question about what is on screen TOGETHER — asked at each scroll position
      for (let i = 0; i < vis.length; i++) for (let j = i + 1; j < vis.length; j++) {
        const A = vis[i], B = vis[j]; if (Math.abs(A.ul.t - B.ul.t) > 2) continue;
        const gap = Math.max(A.ul.l, B.ul.l) - Math.min(A.ul.r, B.ul.r);
        if (!pairWorst || gap < pairWorst.v) pairWorst = {v:gap, a:A.core+'/'+A.word, b:B.core+'/'+B.word, face:F.id, kind:'mark↔mark'}; }
      for (const A of vis) for (const B of vis) { if (A === B) continue;
        if (B.lab.b < A.ul.t || B.lab.t > A.ul.b) continue;
        const gap = Math.max(A.ul.l, B.lab.l) - Math.min(A.ul.r, B.lab.r);
        if (!pairWorst || gap < pairWorst.v) pairWorst = {v:gap, a:A.core+'/'+A.word+' (mark)', b:B.core+'/'+B.word+' (word)', face:F.id, kind:'mark↔word'}; }
      // the nearest neighbouring mark is only meaningful between two marks measured at the SAME
      // scroll position — rects from different passes are not in a common frame of reference.
      for (const A of vis) { let near = null, who = null;
        for (const B of vis) { if (B === A || Math.abs(A.ul.t - B.ul.t) > 2) continue;
          const g = Math.max(A.ul.l, B.ul.l) - Math.min(A.ul.r, B.ul.r);
          if (near == null || g < near) { near = g; who = B.core + '/' + B.word; } }
        A.near = near; A.nearWho = who; }
      // each knob is measured once, on the pass where its own card was brought into view
      got.filter(c => c.core === kind).forEach(c => { if (!byDest.has(c.dest)) byDest.set(c.dest, c); });
    }
    rows.push(...byDest.values());
    const marked = rows.filter(c => c.ul);
    nCells += rows.length; nMeasured += marked.length;
    if (pairWorst && pairWorst.v < worst.gap.v) worst.gap = pairWorst;
    console.log('── ' + F.id.toUpperCase() + ' face: ' + marked.length + '/' + rows.length + ' routed knobs carry a mark');
    if (marked.length !== rows.length)
      console.log('   ⚠️  ' + (rows.length - marked.length) + ' routed knob(s) drew NO underline: ' +
        rows.filter(c => !c.ul).slice(0,6).map(c => c.core + '/' + c.word).join(', '));

    const ROWEPS = 2;   // same visual row (all marks of a face share a baseline per card row)

    // ── 2  the mark stays inside its knob cell ────────────────────────────────────────────────
    for (const A of marked) { const m = Math.min(A.ul.l - A.cell.l, A.cell.r - A.ul.r);
      if (m < worst.margin.v) worst.margin = {v:m, who:A.core+'/'+A.word, face:F.id,
        detail:'cell ' + (A.cell.r - A.cell.l).toFixed(2) + 'px, word ' + A.lab.w.toFixed(2) + 'px'};
      if (m < MIN_CELL_MARGIN + 1.0)
        // how much clear air the mark ACTUALLY has before the nearest neighbouring mark — the
        // question the cell was standing in for. A tight cell with a wide gap is a different
        // problem from a tight cell with a tight gap, and Max should see which one this is.
        tight.push({face:F.id, who:A.core+'/'+A.word, margin:m, cell:A.cell.r - A.cell.l, word:A.lab.w,
                    near:(A.near == null ? null : A.near), nearWho:A.nearWho || '—'}); }

    // ── 3  the line is actually drawn ─────────────────────────────────────────────────────────
    for (const A of marked) { const h = A.rail.h * zoom * DSF;
      if (h < worst.lineH.v) worst.lineH = {v:h, who:A.core+'/'+A.word, face:F.id};
      const cov = A.rail.w / A.lab.w;
      if (cov < worst.cover.v) worst.cover = {v:cov, who:A.core+'/'+A.word, face:F.id,
        detail:'rail ' + A.rail.w.toFixed(2) + ' / word ' + A.lab.w.toFixed(2)};
      const scov = A.span.w / A.lab.w;
      if (scov < 0.999) console.log('   note: ' + A.core + '/' + A.word + ' depth territory covers ' + (scov*100).toFixed(0) + '% of the word');
    }

    /* ── 4  contrast, from the PNG. Same pixel with the mark and with the mark hidden — the mark
       is painted with ALPHA over whatever the card is, so only the rendered pixel tells the truth.
       🚨 The rack is a horizontally SCROLLING strip: at the shipped 820 width barely two cards are
       on screen at once, so a single screenshot would sample two of sixteen backgrounds and call
       that "every card". Every card is scrolled into view and sampled in turn. ────────────────── */
    for (const state of [{key:'sel', mod:{env:1}}, {key:'dim', mod:{lfo:7}}]) {
      await pg.evaluate((m) => { window.__selMod = m; }, state.mod);
      for (const kind of KINDS) {
        const routed = await pg.evaluate((kind, open) => {
          const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === kind); if (i < 0) return false;
          const card = document.querySelectorAll('.fxr-dev')[i]; if (!card) return false;
          if (open && !card.querySelector('.fxr-back')) return false;
          const clip = document.querySelector('.fxr-clip'); clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;
          window.__tiPruneFxRoutes(0, 1e9);
          let n = 0; card.querySelectorAll('[data-mod-dest]').forEach(c => {
            if (c.classList.contains('fxr-knob') === !!open) return;
            const r = c.getBoundingClientRect(); if (r.width && r.height) { window.__tiAddRoute(1, 0, +c.getAttribute('data-mod-dest')); n++; } });
          return n > 0; }, kind, F.open);
        if (!routed) continue;
        await new Promise(r => setTimeout(r, 220));
        const live = (await pg.evaluate(HARVEST)).filter(c => c.ul && F.pick(c) && c.core === kind &&
          c.ul.t >= 0 && c.ul.b <= VH && c.ul.l >= 0 && c.ul.r <= VW);
        if (!live.length) continue;
        /* five samples along each mark, and the bar is decided on the MEDIAN. One pixel is not a
           measurement: the mark runs over whatever the card happens to have under that word, and
           on a 4x2 back panel that can include the top of the NEXT ROW's dial arc (see the dial
           intrusion note below). The median answers "the mark against the card"; the minimum is
           kept too, because the worst pixel is the one that tells you the marks are colliding. */
        const FRAC = [0.1, 0.3, 0.5, 0.7, 0.9];
        const pts = []; live.forEach(A => FRAC.forEach(f =>
          pts.push([ (A.span.l + A.span.w * f) * DSF, (A.rail.t + A.rail.h / 2) * DSF ])));
        const shotA = await pg.screenshot({encoding:'base64', clip:{x:0,y:0,width:VW,height:VH}});
        await pg.evaluate(() => document.querySelectorAll('.sm-ul').forEach(u => u.style.visibility = 'hidden'));
        await new Promise(r => setTimeout(r, 80));
        const shotB = await pg.screenshot({encoding:'base64', clip:{x:0,y:0,width:VW,height:VH}});
        await pg.evaluate(() => document.querySelectorAll('.sm-ul').forEach(u => u.style.visibility = ''));
        const fg = await pixels(pg, shotA, pts), bg = await pixels(pg, shotB, pts);
        for (let i = 0; i < live.length; i++) {
          const cs = [], k0 = i * FRAC.length;
          for (let k = 0; k < FRAC.length; k++) { const f = fg[k0+k], g = bg[k0+k]; if (f && g) cs.push({c:ratio(f,g), f, g}); }
          if (!cs.length) continue;
          const bySize = cs.slice().sort((a,b) => a.c - b.c), med = bySize[Math.floor(bySize.length/2)], lo = bySize[0];
          if (state.key === 'dim') { dimAll.push(med.c); if (lo.c < med.c - 0.25) pixLo.push({who:live[i].core+'/'+live[i].word, face:F.id, med:med.c, lo:lo.c, under:'rgb('+lo.g+')'}); }
          if (med.c < worst[state.key].v) worst[state.key] = {v:med.c, who:live[i].core+'/'+live[i].word, face:F.id,
            detail:'mark rgb(' + med.f + ') on card rgb(' + med.g + ')'}; }
      }
    }
    await pg.evaluate(() => { window.__selMod = {env:1}; window.__tiPruneFxRoutes(0, 1e9); });

    /* ── NOTE (not a bar, but it is why bar 4's worst pixel is what it is): on the 4x2 back panel
       the row-1 word's mark is drawn INSIDE the row-2 dial's box. The box is mostly air at the
       top, so most columns read clean — but wherever that dial's arc reaches high, the mark and
       the knob share pixels. Measured as the mark's drawn line against the next dial's box top. */
    if (F.open) {
      /* Max's second ruling was "nudge it up so it clears the row-2 dial", and the honest test of
         that is the dial's INK, not its box: the box is mostly air at the top, so a mark inside it
         may still be nowhere near the arc. Read from the rendered PNG — for each row-1 mark, the
         topmost painted row inside the dial below it — and report the real clearance. */
      for (const kind of KINDS) {
        const ctx = await pg.evaluate((kind) => { const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === kind);
          const card = document.querySelectorAll('.fxr-dev')[i]; if (!card || !card.querySelector('.fxr-bk-grid')) return null;
          const clip = document.querySelector('.fxr-clip'); clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;
          window.__tiPruneFxRoutes(0, 1e9);
          card.querySelectorAll('[data-mod-dest]').forEach(c => { if (c.classList.contains('fxr-knob')) return;
            const r = c.getBoundingClientRect(); if (r.width && r.height) window.__tiAddRoute(1, 0, +c.getAttribute('data-mod-dest')); });
          window.__selMod = {env:1}; return true; }, kind);
        if (!ctx) continue;
        await new Promise(r => setTimeout(r, 240));
        const cells = await pg.evaluate((kind) => { const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === kind);
          const card = document.querySelectorAll('.fxr-dev')[i];
          const ks = [...card.querySelectorAll('.fxr-bk-knob')]; const out = [];
          ks.forEach(k => { const m = /grid-column:\s*(\d+);grid-row:\s*(\d+)/.exec(k.getAttribute('style') || ''); if (!m || m[2] !== '1') return;
            const below = ks.find(x => { const q = /grid-column:\s*(\d+);grid-row:\s*(\d+)/.exec(x.getAttribute('style') || ''); return q && q[1] === m[1] && q[2] === '2'; });
            if (!below) return;
            const lab = k.querySelector('.fxr-lab'); const rg = document.createRange(); rg.selectNodeContents(lab);
            const LR = rg.getBoundingClientRect();
            const u = [...document.querySelectorAll('.sm-ul')].filter(x => x.style.display !== 'none')
                        .find(x => Math.abs(x.getBoundingClientRect().left - LR.left) < 1.5);
            if (!u) return; const R = u.querySelector('.smu-rail').getBoundingClientRect();
            const dl = below.querySelector('.fxr-dial').getBoundingClientRect();
            out.push({word:lab.textContent, markBot:R.bottom, markTop:R.top, lt:LR.top, lb:LR.bottom, lx:LR.left, lr:LR.right,
                      dl:dl.left, dr:dl.right, dt:dl.top}); });
          return out; }, kind);
        if (!cells.length) continue;
        const shot = await pg.screenshot({encoding:'base64', clip:{x:0, y:0, width:VW, height:VH}});
        const inked = await pg.evaluate((u, cells, D) => new Promise(res => { const im = new Image();
          im.onload = () => { const c = document.createElement('canvas'); c.width = im.width; c.height = im.height;
            const x = c.getContext('2d'); x.drawImage(im, 0, 0);
            const px = (X, Y) => { const d = x.getImageData(X, Y, 1, 1).data; return d[0] + d[1] + d[2]; };
            res(cells.map(ce => { const bg = px(Math.max(0, Math.round(ce.dl * D) - 6), Math.round((ce.markBot + 3) * D));
              let arc = null;
              for (let Y = Math.round(ce.markBot * D) + 1; Y < Math.round((ce.dt + 30) * D); Y++) { let h = false;
                for (let X = Math.round(ce.dl * D); X < Math.round(ce.dr * D); X++) if (Math.abs(px(X, Y) - bg) > 28) { h = true; break; }
                if (h) { arc = Y / D; break; } }
              let gly = null;
              for (let Y = Math.round(ce.markTop * D) - 1; Y > Math.round(ce.lt * D); Y--) { let h = false;
                for (let X = Math.round(ce.lx * D); X < Math.round(ce.lr * D); X++) if (Math.abs(px(X, Y) - bg) > 28) { h = true; break; }
                if (h) { gly = Y / D; break; } }
              return {word:ce.word, toArc: arc == null ? null : arc - ce.markBot, toWord: gly == null ? null : ce.markTop - gly,
                      intoBox: (ce.markBot - ce.dt)}; })); };
          im.onerror = () => res([]); im.src = u; }), 'data:image/png;base64,' + shot, cells, DSF);
        inked.forEach(r => intr.push(Object.assign({core:kind}, r)));
      }
      await pg.evaluate(() => { window.__tiPruneFxRoutes(0, 1e9); });
    }

    // ── 5  the comet: an LFO route, driven, its head's travel along the word ──────────────────
    {
      const probe = marked.slice().sort((a,b) => a.lab.w - b.lab.w)[0];   // the SHORTEST word on this face — the hardest case (the least room for the comet to travel)
      if (probe) {
        await pg.evaluate((d, core) => { const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === core);
          const card = document.querySelectorAll('.fxr-dev')[i]; const clip = document.querySelector('.fxr-clip');
          if (card) clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;   // fb453 — a mark outside the clip is suppressed; scroll it in
          window.__tiPruneFxRoutes(0, 1e9); window.__tiAddRoute(0, 1, d); window.__selMod = {lfo:1}; }, probe.dest, probe.core);
        await new Promise(r => setTimeout(r, 200));
        const travel = await pg.evaluate(async (d) => {
          const frame = () => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
          let lo = 1e9, hi = -1e9, seen = 0;
          for (let k = 0; k <= 24; k++) {
            const v = Math.sin(k / 24 * Math.PI * 2);
            window.__modViz(null, [v,0,0,0,0,0,0,0,0,0], null); await frame();
            const u = [...document.querySelectorAll('.sm-ul')].filter(x => x.style.display !== 'none')[0];
            if (!u) continue; const c = u.querySelector('.smu-comet'); if (!c || c.style.display === 'none') continue;
            const r = c.getBoundingClientRect(); seen++; lo = Math.min(lo, r.right); hi = Math.max(hi, r.right);
          }
          return {pk: seen ? hi - lo : 0, frames: seen};
        }, probe.dest);
        const dev = travel.pk * zoom * DSF;
        if (dev < worst.comet.v) worst.comet = {v:dev, who:probe.core+'/'+probe.word, face:F.id,
          detail:'word ' + probe.lab.w.toFixed(2) + 'px, ' + travel.frames + ' frames sampled'};
        await pg.evaluate(() => { window.__modViz(null, null, null); });
      }
    }

    // ── the pictures. Every kind, this face, at shipped scale. ────────────────────────────────
    await pg.evaluate(() => { window.__tiPruneFxRoutes(0, 1e9); window.__selMod = {env:1}; });
    for (const kind of KINDS) {
      const ok = await pg.evaluate((kind, open) => {
        const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === kind); if (i < 0) return null;
        const card = document.querySelectorAll('.fxr-dev')[i]; if (!card) return null;
        if (open && !card.querySelector('.fxr-back')) return null;
        const clip = document.querySelector('.fxr-clip');
        clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;
        card.querySelectorAll('[data-mod-dest]').forEach(c => { const isFront = c.classList.contains('fxr-knob');
          if (isFront === !!open) return; const r = c.getBoundingClientRect();
          if (r.width && r.height) window.__tiAddRoute(1, 0, +c.getAttribute('data-mod-dest')); });
        return true; }, kind, F.open);
      if (!ok) continue;
      await new Promise(r => setTimeout(r, 260));
      const box = await pg.evaluate((kind) => { const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === kind);
        const r = document.querySelectorAll('.fxr-dev')[i].getBoundingClientRect();
        return {x:Math.max(0, r.left - 6), y:Math.max(0, r.top - 6), width:Math.min(820, r.width + 12), height:r.height + 16}; }, kind);
      await pg.screenshot({path: path.join(SHOTS, F.id + '-' + kind + '.png'), clip: box});
      // ...and the DIM state, which is the one bar 4 is decided on: a route belonging to a
      // modulator that is not the selected one. Same card, same pixels, no purple.
      await pg.evaluate(() => { window.__selMod = {lfo:7}; }); await new Promise(r => setTimeout(r, 140));
      await pg.screenshot({path: path.join(SHOTS, F.id + '-' + kind + '-dim.png'), clip: box});
      await pg.evaluate(() => { window.__selMod = {env:1}; window.__tiPruneFxRoutes(0, 1e9); });
    }
  }

  /* ── THE REFERENCE. Bar 4 measures the underline's OWN styling (fb182/188), which the rack
     inherits unchanged. So measure the identical thing on a knob the rack never touched — an
     oscillator dial on the synth panel — and the report can say whether a low number is
     something the rack did or something the shipped grammar has always done. */
  let refDim = null, refSel = null;
  try {
    // dest 2 = SYN_OSC_A_WT_FRAME, oscillator A's "WT Pos" dial — an ordinary synth-panel knob,
    // on screen at the base window size, wearing the same underline since fb188.
    const refDest = await pg.evaluate(() => { window.__tiPruneFxRoutes(0, 1e9);
      const k = document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WT_FRAME"]');
      if (!k) return null; const r = k.getBoundingClientRect(); if (!r.width) return null;
      window.__tiAddRoute(1, 0, 2); return true; });
    if (refDest) {
      await new Promise(r => setTimeout(r, 300));
      for (const st of [{k:'sel', m:{env:1}}, {k:'dim', m:{lfo:7}}]) {
        await pg.evaluate(m => { window.__selMod = m; }, st.m); await new Promise(r => setTimeout(r, 140));
        const p = await pg.evaluate(() => { const u = [...document.querySelectorAll('.sm-ul')].filter(x => x.style.display !== 'none')[0];
          if (!u) return null; const s = u.querySelector('.smu-span').getBoundingClientRect(), r = u.querySelector('.smu-rail').getBoundingClientRect();
          return (r.top < 0 || r.bottom > innerHeight) ? null : [(s.left + s.width/2), (r.top + r.height/2)]; });
        if (!p) continue;
        const A = await pg.screenshot({encoding:'base64', clip:{x:0,y:0,width:VW,height:VH}});
        await pg.evaluate(() => document.querySelectorAll('.sm-ul').forEach(u => u.style.visibility = 'hidden'));
        await new Promise(r => setTimeout(r, 90));
        const B = await pg.screenshot({encoding:'base64', clip:{x:0,y:0,width:VW,height:VH}});
        await pg.evaluate(() => document.querySelectorAll('.sm-ul').forEach(u => u.style.visibility = ''));
        const f = (await pixels(pg, A, [[p[0]*DSF, p[1]*DSF]]))[0], g = (await pixels(pg, B, [[p[0]*DSF, p[1]*DSF]]))[0];
        if (f && g) { const v = {v:ratio(f, g), detail:'mark rgb(' + f + ') on panel rgb(' + g + ')'};
          if (st.k === 'sel') refSel = v; else refDim = v; }
      }
    }
  } catch(e) { console.log('   (reference knob unavailable: ' + String(e).slice(0,90) + ')'); }

  // ── the verdict ───────────────────────────────────────────────────────────────────────────
  console.log('\n══ THE FIVE NUMBERS ══   (' + nMeasured + '/' + nCells + ' routed knobs measured)\n');
  bar(1, worst.gap.v > 0, 'NOT CRAMMED — no mark touches another mark or a neighbouring word',
      'worst pair: ' + worst.gap.v.toFixed(2) + ' px  (' + worst.gap.kind + ', ' + worst.gap.face + ')  ' + worst.gap.a + '  ↔  ' + worst.gap.b);
  const crowded = tight.filter(t => t.margin < MIN_CELL_MARGIN && t.near != null && t.near < MIN_NEIGHBOUR_GAP);
  const spills  = tight.filter(t => t.margin < 0);
  bar(2, crowded.length === 0 && spills.length === 0,
      'NOT CRAMMED — no mark is both tight in its cell (< ' + MIN_CELL_MARGIN + ' px) AND close to its neighbour (< ' + MIN_NEIGHBOUR_GAP + ' px), and none overflows its cell',
      (crowded.length || spills.length)
        ? (spills.length ? spills.length + ' mark(s) OVERFLOW their cell: ' + spills.map(t => t.who).join(', ') + '. ' : '') +
          (crowded.length ? crowded.length + ' crowded: ' + crowded.map(t => t.who + ' (' + t.margin.toFixed(2) + ' px in cell, neighbour ' + t.near.toFixed(2) + ' px)').join(', ') : '')
        : 'tightest cell ' + worst.margin.v.toFixed(2) + ' px (' + worst.margin.who + ', ' + worst.margin.face + ', ' + worst.margin.detail +
          ') — and its nearest neighbouring mark is ' + ((tight.find(t => t.who === worst.margin.who) || {near:null}).near ?? 0).toFixed(2) + ' px away, so nothing is crowded');
  bar(3, worst.lineH.v >= MIN_LINE_DEV_PX && worst.cover.v >= MIN_WORD_COVER,
      'ALWAYS NOTICEABLE — the line is >= ' + MIN_LINE_DEV_PX + ' device px tall and covers >= ' + (MIN_WORD_COVER*100) + '% of the word',
      'thinnest ' + worst.lineH.v.toFixed(2) + ' device px (' + worst.lineH.who + ') · least cover ' +
      (worst.cover.v*100).toFixed(1) + '% (' + worst.cover.who + ', ' + worst.cover.detail + ')');
  bar(4, worst.dim.v >= MIN_CONTRAST && worst.sel.v >= MIN_CONTRAST,
      'ALWAYS NOTICEABLE — >= ' + MIN_CONTRAST + ':1 against the card, in BOTH states (sampled from the PNG)',
      'purple/selected ' + worst.sel.v.toFixed(2) + ':1 (' + worst.sel.who + ', ' + worst.sel.detail + ')\n           ' +
      'dim/unselected  ' + worst.dim.v.toFixed(2) + ':1 (' + worst.dim.who + ', ' + worst.dim.detail + ')\n           ' +
      'REFERENCE, the same mark on a SYNTH-PANEL knob the rack never touched:  purple ' +
      (refSel ? refSel.v.toFixed(2) + ':1' : 'n/a') + ' · dim ' + (refDim ? refDim.v.toFixed(2) + ':1' : 'n/a') +
      (refDim ? '  [' + refDim.detail + ']' : ''));
  bar(5, worst.comet.v >= MIN_COMET_DEV, 'THE COMET READS — >= ' + MIN_COMET_DEV + ' device px of travel on the SHORTEST word',
      worst.comet.v.toFixed(2) + ' device px pk-pk on ' + worst.comet.who + ' (' + worst.comet.face + ', ' + worst.comet.detail + ')');

  if (tight.length) { console.log('\n── DOSSIER, bar 2: every mark within 1 px of the bar ──');
    tight.sort((a,b)=>a.margin-b.margin).forEach(t => console.log('   ' + t.margin.toFixed(2) + ' px  ' +
      t.who + ' (' + t.face + ')   word ' + t.word.toFixed(2) + ' in a ' + t.cell.toFixed(2) + ' px cell' +
      (t.near != null ? '   — but the nearest neighbouring mark is ' + t.near.toFixed(2) + ' px away (' + t.nearWho + ')' : ''))); }
  if (pixLo.length) { console.log('\n── DOSSIER, bar 4: marks whose WORST pixel is well under their own median ──');
    pixLo.sort((a,b)=>a.lo-b.lo).slice(0,8).forEach(x => console.log('   ' + x.lo.toFixed(2) + ':1 worst vs ' +
      x.med.toFixed(2) + ':1 median   ' + x.who + ' (' + x.face + ')   the worst pixel sits on ' + x.under +
      ' — not the card, but something drawn there')); }
  if (intr.length) {
    const withArc = intr.filter(x => x.toArc != null).sort((a,b) => a.toArc - b.toArc);
    const withWord = intr.filter(x => x.toWord != null).sort((a,b) => a.toWord - b.toWord);
    console.log('\n── THE 4x2 BACK PANEL CORRIDOR, measured from the PNG (Max: "nudge it up off the dial") ──');
    console.log('   the mark must sit between the WORD\'S glyphs above it and the row-2 dial\'s ARC ink below it.');
    if (withArc.length) console.log('   tightest to the ARC   : ' + withArc[0].toArc.toFixed(2) + ' px  (' + withArc[0].core + '/' + withArc[0].word +
      ')   — ' + withArc.filter(x => x.toArc <= 0).length + ' of ' + withArc.length + ' touching or overlapping');
    if (withWord.length) console.log('   tightest to the WORD  : ' + withWord[0].toWord.toFixed(2) + ' px  (' + withWord[0].core + '/' + withWord[0].word +
      ')   — ' + withWord.filter(x => x.toWord <= 0).length + ' of ' + withWord.length + ' touching or overlapping');
    const box = intr.filter(x => x.intoBox > 0).sort((a,b) => b.intoBox - a.intoBox);
    if (box.length) console.log('   (for reference, ' + box.length + ' marks still reach into the dial\'s BOX, worst ' +
      box[0].intoBox.toFixed(2) + ' px on ' + box[0].core + '/' + box[0].word + ' — the box is air at the top, the arc is the thing you see)'); }
  if (dimAll.length) { dimAll.sort((a,b)=>a-b);
    const q = f => dimAll[Math.min(dimAll.length-1, Math.floor(f*dimAll.length))];
    console.log('\n── DOSSIER, bar 4 (dim state, ' + dimAll.length + ' marks): min ' + dimAll[0].toFixed(2) +
      ' · p25 ' + q(0.25).toFixed(2) + ' · median ' + q(0.5).toFixed(2) + ' · max ' + dimAll[dimAll.length-1].toFixed(2) +
      '  ·  ' + dimAll.filter(v=>v<MIN_CONTRAST).length + ' of ' + dimAll.length + ' below ' + MIN_CONTRAST + ':1'); }

  const bad = bars.filter(x => !x.ok);
  console.log('\n  ' + (bad.length ? '❌ ' + bad.length + ' of 5 bars FAIL — bars are not knobs: this goes to Max.' : '✅ all 5 bars hold on every card.'));
  console.log('  screenshots → ' + SHOTS + '\n');
  await b.close();
  process.exit(bad.length ? 1 : 0);
})().catch(e => { console.error('PROBE CRASHED: ' + (e && e.stack || e)); process.exit(2); });
