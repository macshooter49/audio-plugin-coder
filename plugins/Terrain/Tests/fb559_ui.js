// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb559_ui.js — fb558/fb559. THE PILL LAW AND THE EXTEND EMBLEM.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/fb559_ui.js [page.html]
//
//  Max: "every word and every letter is the same size, and if it's too big then it's just dot dot
//  dot. I don't want any words to be bigger, I don't want them to be smaller... the pill should
//  stay exactly where it's at. It does that thing where it plays with the padding again."
//
//  ⚠️ WHAT THE FIRST DRAFT OF THIS GATE GOT WRONG, TWICE — both worth keeping:
//   1. It measured a HIDDEN panel and reported 0px for everything, i.e. "nothing moves" (fb531).
//      Every bar now refuses to report if the pill has no width.
//   2. It gated "does the name OVERFLOW", which was never the bug: the name rendered FLUSH — 0.00px
//      of clearance from a 1px stroke — and a word touching its own frame is what reads as escaped.
//      The bar is CLEARANCE now, across the four zooms the editor actually runs at.
//
//  PROOF THE BARS CAN FAIL (fb421 — a gate that has never failed has never been tested):
//    · .warp2-mode-value back to max-width:100%          -> bar 1 reds (0px clearance)
//    · its reserve made absurd (nothing ever ellipses)   -> bar 1 reds twice
//    · .uni-stack-sizer given back overflow:hidden       -> bar 2 reds (the cell resizes, the row moves)
//    · the sizer put back INSIDE the pill (pre-fb578)     -> bar 2 reds (the pill is 66px at "Off", not 30)
//    · the warp picker asks for a WORD instead of the emblem -> bar 3 reds
// ══════════════════════════════════════════════════════════════════════════════════════════════
// fb559 — THE PILL LAW + THE EXTEND EMBLEM, measured in the real page.
const puppeteer=require('puppeteer-core');
const P=process.argv[2]||require('path').join(__dirname,'..')+'/Source/ui/public/index.html';
let FAIL=0; const bad=m=>{FAIL++; return '  ✗ '+m;};
(async()=>{
  const b=await puppeteer.launch({executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:820,height:656,deviceScaleFactor:2});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,160)));
  await pg.goto('file://'+P,{waitUntil:'networkidle0'});
  await new Promise(r=>setTimeout(r,2500));

  // ── BAR 1 · the WARP 2 mode name: same box, never touching it, dots when it must ──
  //   MEASURED FIRST, then gated. The name never overflowed — it rendered FLUSH against the 1px
  //   stroke (0.00px clear for "Sine Shaper"), which is what "it leaves the box" actually is.
  //   So the bar is CLEARANCE, at every zoom the editor runs at, not "does it overflow".
  const r1 = await pg.evaluate(()=>{
    document.documentElement.classList.remove('card-only-late');
    document.querySelectorAll('.ti-preboot').forEach(e=>e.classList.remove('ti-preboot'));
    const sp=document.getElementById('syn-panel'); if(sp){ sp.classList.remove('hidden'); sp.style.display='block'; }
    const btn=document.getElementById('syn-btn'); if(btn) btn.click();
    const dev=document.getElementById('osc-a-device'); if(dev) dev.classList.add('swapped');
    const el=document.getElementById('osc-a-warp2-mode'); if(!el) return {err:'no warp2 mode value'};
    const pill=el.closest('.sel'), pl=document.getElementById('plugin')||document.body;
    const names=(window.WARP_MODES||[]).filter(n=>!/^Reserved/.test(n));
    let worst=1e9, worstAt='', ell=0, ellNeeded=0;
    const widths=new Set(), lefts=new Set();
    for(const z of [1,1.35,1.902,2.5]){ pl.style.zoom=z;
      for(const n of names){ el.textContent=n;
        const er=el.getBoundingClientRect(), pr=pill.getBoundingClientRect();
        const clear=Math.min(er.left-(pr.left+1), (pr.right-1)-er.right)/z;   // back to CSS px
        if(clear<worst){ worst=clear; worstAt=n+' @zoom '+z; }
        if(el.scrollWidth>el.clientWidth) ell++;
        if(z===1){ widths.add(+(pr.width).toFixed(2)); lefts.add(+(pr.left).toFixed(2)); } } }
    pl.style.zoom=1;
    // how many names are physically too wide for the box at zoom 1
    for(const n of names){ el.textContent=n; if(el.scrollWidth>el.clientWidth) ellNeeded++; }
    if([...widths][0]<=0) return {err:'the pill measured 0px wide — the panel is still hidden, so this gate proves nothing'};
    return {n:names.length, worst:+worst.toFixed(2), worstAt, ellNeeded,
            widths:[...widths], lefts:[...lefts]};
  });
  console.log('BAR 1 — the warp-mode name, all '+r1.n+' live names x 4 editor zooms');
  if(r1.err){ console.log(bad(r1.err)); }else{
  console.log(r1.worst>=2 ? '  ✓ never touches its border: worst clearance '+r1.worst+'px ('+r1.worstAt+')'
              : bad('the name sits '+r1.worst+'px from the stroke ('+r1.worstAt+') — it reads as escaped'));
  console.log(r1.ellNeeded>0 ? '  ✓ the names too long for the box get dots ('+r1.ellNeeded+' of them)'
              : bad('no name ever ellipses — the reserve is too generous to be doing anything'));
  console.log(r1.widths.length===1 ? '  ✓ one pill width for every name: '+r1.widths[0]+'px'
              : bad('the pill resizes: '+JSON.stringify(r1.widths)));
  console.log(r1.lefts.length===1 ? '  ✓ it never moves: left '+r1.lefts[0]+'px'
              : bad('the pill MOVES: '+JSON.stringify(r1.lefts))); }

  // ── BAR 2 · the UNISON STACK cell never moves; the PILL is the Voices pill's size ─────────────
  //   fb558 froze the PILL. fb578 lets it hug its own text instead, because the frozen pill was
  //   permanently as wide as "Center-24" (50.3px of text -> a ~66px pill) beside a 30px Voices pill —
  //   and this row is `repeat(6, minmax(0,1fr))`, so that 66px was SPILLING ~11px into the Voices and
  //   Range columns either side. Max, with a screenshot at "Off": "remove the pill from stack, or resize
  //   the pill because it looks too big next to the other buttons." fb558's law is untouched and is
  //   gated here at the layer that actually owns it — the column, its neighbours and the row hold one
  //   rect across all nine options — while the pill matches the Voices pill at "Off" and fits its column.
  const r2 = await pg.evaluate(()=>{
    // the unison row is page 3 of the FRONT, so the back view has to go and the page shown
    const dev=document.getElementById('osc-a-device'); if(dev) dev.classList.remove('swapped');
    document.querySelectorAll('#osc-a-device .front-only, #osc-a-device .uni-knob-wrap').forEach(e=>e.style.display='flex');
    const wrap=document.querySelector('#syn-panel .uni-stack[data-osc="a"]'); if(!wrap) return {err:'no uni-stack'};
    const pill=wrap.querySelector('.uni-stack-pill'), val=wrap.querySelector('.uni-stack-val');
    const vPill=document.querySelector('#syn-panel .uni-voices[data-osc="a"] .uni-voices-pill')
             || document.querySelector('#syn-panel .uni-voices-pill');
    const nbr=wrap.parentElement.querySelector('.knob[data-syn$="_URANGE"]');   /* the neighbour that fb558 watched get shoved */
    const opts=[...wrap.querySelectorAll('select option')].map(o=>o.textContent);
    const out=[]; let overflow=0, offW=null;
    for(const o of opts){ val.textContent=o;
      const cr=wrap.getBoundingClientRect(), pr=pill.getBoundingClientRect(), vr=val.getBoundingClientRect();
      const nr=nbr?nbr.getBoundingClientRect():{left:0};
      out.push({o, cellW:+cr.width.toFixed(2), cellL:+cr.left.toFixed(2), nbrL:+nr.left.toFixed(2),
                pillW:+pr.width.toFixed(2), off:+((pr.left+pr.width/2)-(cr.left+cr.width/2)).toFixed(2)});
      if(o==='Off'){ offW=+pr.width.toFixed(2); out[out.length-1].fits = (pr.left>=cr.left-0.5 && pr.right<=cr.right+0.5); }
      if(vr.right>pr.right+0.51||vr.left<pr.left-0.51) overflow++; }
    if(out.length && out[0].cellW<=0) return {err:'the stack cell measured 0px wide — the panel is still hidden'};
    const off=out.find(x=>x.o==='Off');
    return {opts:opts.length, offFits:!!(off&&off.fits), widest:Math.max(...out.map(x=>x.pillW)),
            cellW:[...new Set(out.map(x=>x.cellW))], cellL:[...new Set(out.map(x=>x.cellL))],
            nbrL:[...new Set(out.map(x=>x.nbrL))], worstOff:Math.max(...out.map(x=>Math.abs(x.off))),
            offW, voicesW:vPill?+vPill.getBoundingClientRect().width.toFixed(2):null,
            pillW:[...new Set(out.map(x=>x.pillW))].sort((a,b)=>a-b), overflow};
  });
  console.log('\nBAR 2 — the unison STACK cell + pill, all '+r2.opts+' options');
  if(r2.err){ console.log(bad(r2.err)); }else{
  console.log(r2.cellW&&r2.cellW.length===1 && r2.cellL&&r2.cellL.length===1
              ? '  ✓ the CELL never resizes or moves: '+r2.cellW[0]+'px at left '+r2.cellL[0]+'px'
              : bad('the cell moves or resizes: w '+JSON.stringify(r2.cellW)+' left '+JSON.stringify(r2.cellL)));
  console.log(r2.nbrL&&r2.nbrL.length===1 ? '  ✓ the neighbour (Range) never moves: left '+r2.nbrL[0]+'px'
              : bad('the neighbour MOVES: '+JSON.stringify(r2.nbrL)));
  console.log(r2.worstOff<=0.6 ? '  ✓ the pill stays centred in the cell (worst '+r2.worstOff+'px)'
              : bad('the pill drifts off centre: '+r2.worstOff+'px'));
  console.log((r2.offW!=null&&r2.voicesW!=null&&Math.abs(r2.offW-r2.voicesW)<=0.6)
              ? '  ✓ at "Off" the pill IS the Voices pill: '+r2.offW+'px vs '+r2.voicesW+'px  (was ~66px — fb578)'
              : bad('at "Off" the pill is '+r2.offW+'px against the Voices pill\'s '+r2.voicesW+'px'));
  console.log(r2.offFits ? '  ✓ at "Off" the pill sits INSIDE its column — it no longer spills into Voices and Range'
              : bad('at "Off" the pill still overflows its column'));
  console.log(r2.overflow===0 ? '  ✓ every option renders in full — nothing is clipped ('+r2.pillW[0]+'..'+r2.pillW[r2.pillW.length-1]+'px)'
              : bad(r2.overflow+' options clipped'));
  console.log('    (note: the longest options still exceed the '+r2.cellW[0]+'px column — widest pill '+r2.widest+'px — exactly as they did before fb578; shortening those nine names is a separate call for Max)'); }

  // ── BAR 3 · the EXTEND emblem sits next to the ✕, at the ✕'s size ─────────────
  const r3 = await pg.evaluate(()=>{
    /* through the SHIPPED door: __warpPicker is what a right-click on a warp pill calls, and it
       is where the EXTEND action is configured. Driving openTwoPaneBrowser directly would test
       the renderer and leave the wiring — the half that actually broke — untested. */
    if(!window.__warpPicker) return {err:'no warp picker'};
    window.__openWarpExt = window.__openWarpExt || function(){};
    window.__paramCardinality = window.__paramCardinality || function(){ return 48; };
    const r = window.__warpPicker({clientX:200,clientY:200}, 'OSC A — WARP', 7, function(){}, 'SYN_OSC_A_WARP_MODE');
    const panel=[...document.querySelectorAll('div')].reverse().find(d=>d.style.zIndex==='2147483646');
    if(!panel) return {err:'panel not found'};
    const svgs=[...panel.querySelectorAll('svg')];
    const head=panel.firstChild;
    const right=[...head.children].find(c=>c.style.gap==='11px');
    const kids=right?[...right.children]:[];
    const box=e=>{const r=e.getBoundingClientRect();return {w:+r.width.toFixed(1),h:+r.height.toFixed(1),x:+r.left.toFixed(1)};};
    return { nRight:kids.length, boxes:kids.map(box),
             words:head.textContent.trim(),
             hasExtPath:kids.some(k=>/M15 3h6v6/.test(k.innerHTML)),
             xIsLast:/M5 5l14 14/.test(kids.length?kids[kids.length-1].innerHTML:''),
             rows:(function(){ const i=panel.querySelector('input.tpb-srch');
                     return !!(i && /[0-9]+ warp modes/.test(i.placeholder) && i.closest('.tpb-srow')); })() };
  });
  console.log('\nBAR 3 — the EXTEND emblem in the warp browser header');
  if(r3.err){ console.log(bad(r3.err)); }
  else {
    console.log(r3.hasExtPath ? '  ✓ the emblem is in the right-hand cluster' : bad('no emblem in the header'));
    console.log(r3.xIsLast ? '  ✓ the ✕ is still the last thing on the row' : bad('the ✕ is not last'));
    const same = r3.boxes.length>=2 && Math.abs(r3.boxes[r3.boxes.length-2].w - r3.boxes[r3.boxes.length-1].w)<1.2;
    console.log(same ? '  ✓ emblem and ✕ are the same size: '+JSON.stringify(r3.boxes.slice(-2)) : bad('sizes differ: '+JSON.stringify(r3.boxes)));
    console.log(!/EXTEND/i.test(r3.words) ? '  ✓ the word EXTEND is gone from the header' : bad('the word EXTEND is still there'));
    console.log(r3.rows ? '  ✓ the search row is untouched' : bad('the search row moved'));
  }
  console.log('\npage errors: '+(errs.length?errs.join(' | '):'none'));
  if(errs.length) FAIL++;
  console.log(FAIL? '\n  '+FAIL+' FAILED' : '\n  ALL BARS GREEN');
  await b.close(); process.exit(FAIL?1:0);
})();
