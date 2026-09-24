// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp77 — ONE MIDLINE, ONE CARD COLOUR, A REAL SINE, THE LFO'S GRID BOTH WAYS. Was tp76: one line, one grid, the LFO's grammar (tp74's bars re-aimed + the new ones). Was tp74: the taller screen, the small native-dropdown bar, no On, three knobs a target, ghost lanes, fluid free draw, no caps (tp73's bars re-aimed + the new ones).
//
//    node Tests/_tp77_gate.js
//
//  [0] the Chop slot builds the Shaper: the card shell (title Shaper, the Mix header), the screen (svg), eight
//      target tiles with status dots, three tabs, the chain foot; the tile's emblem is one sine path; FLOWNAME says Shaper
//  [1] 🚨 the screen is drawn from breakpoints with the LFO's law: the Volume lane boots as a 1/16 gate (16 flats),
//      the Time lane as a unity ramp; selecting a target switches the screen, the ghosts of lit lanes stay behind
//  [2] the selector strip: Filter shows its types and the arrows walk them (writing the MODE parameter); Time shows
//      its presets and picking one restamps the lane; other lanes show the stock shapes
//  [3] the brushes: Line stamps a flat into a grid cell, the Point tool adds a point, snap holds it on the grid
//  [4] 🚨 every edit pushes the lanes JSON to the processor (setShaperJson, 8 lanes, pts + knobs), debounced
//  [5] On / Depth / Rate / Mode are parameters through the shim (FLOW_CHOP_<LANE>_ON …); double-tapping a lit tile
//      toggles its lane; the dice never rolls this card
//  [6] the right-click menu is the LFO's: Grid slider, Snap, Flips, Random, the eight tools, the stock shapes
//  [7] presets: the factory list has the shapes; recalling one stamps its lanes and their types
//  [8] 🚨 the lane's knobs MOVE THE LINE: tension, phase, floor, smooth, swing each change the drawn shape, and the
//      breakpoints stay where they were drawn (the raw line shows dashed behind)
//  [9] the Target tab is the lane's back panel: every lane's knobs and mode pills are there, and each one lives in the DSP
//  [10] the Clock tab: Rate / Grid / Trigger as pill rows in titled boxes; a lit pill is purple outline, white ink, no glow
//  [11] the follower advances on the tempo between pushes, never runs backwards, snaps on a jump, fades in silence
//  [12] the card is the mockup's width (462) and the screen is the LFO's field (its grid classes, stroke, nodes, follower)
//  [13] tp71b — the tile's sine on the motion clock
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const src=fs.readFileSync('Source/ui/public/index.html','utf8');
const PAGE=path.join(require('os').tmpdir(),'tp77.html'); fs.writeFileSync(PAGE,src);
let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:1200,height:900});
 await p.evaluateOnNewDocument(stubSrc+'\nstub(); window.__shpJson=[]; ["VOL","TIME","FILT","PAN","REP","DRIVE","PHASE","CRUSH"].forEach(function(L,i){ window.__params["FLOW_CHOP_"+L+"_ON"]=i?0:1; window.__params["FLOW_CHOP_"+L+"_DEPTH"]=1; window.__params["FLOW_CHOP_"+L+"_RATE"]=4/7; window.__params["FLOW_CHOP_"+L+"_MODE"]=(L==="DRIVE"?5/22:0); window.__params["FLOW_CHOP_"+L+"_TRIG"]=0; }); (function(){ const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>{ if(n==="setShaperJson") return (i,j)=>{ window.__shpJson.push([i,j]); return Promise.resolve(0); }; if(n==="getShaperJson") return ()=>Promise.resolve("{}"); return orig.getNativeFunction(n); } }); Object.defineProperty(window,"Juce",{configurable:true,get(){return wrap;},set(){}}); })();');
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,200)));   /* armed BEFORE goto: a syntax error in a script block lands here at parse time (tp75: a // comment swallowed a brace and the frame dispatcher never defined __tiFrameReg — the run before this line was moved read 15/17 with no error listed) */
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>{ document.documentElement.setAttribute('data-theme','dark'); window.setActivePanel('syn'); }); await sleep(600);



 // ── [0] the card: the Glitch chassis, the LFO's field, the bar's chips ──
 const r0=await p.evaluate(async()=>{ const c=window.__flowCardOf('chop',1); c.open(); await new Promise(r=>setTimeout(r,700));
   const card=document.querySelector('.ti-card.shp-ext'); if(!card) return {err:'no .ti-card.shp-ext'};
   const tile=document.querySelector('.flow-mode[data-mode="chop"]'); const scr=card.querySelector('.screen');
   return { boot:(typeof window.__tiFrameReg==='function')&&(typeof window.__tiWind==='function')&&!!window.__FLT_ENGINES&&window.__FLT_ENGINES.length>100, open:card.classList.contains('open'), title:card.querySelector('.h .tt').textContent, mix:!!card.querySelector('.h .mix'), width:Math.round(parseFloat(getComputedStyle(card).width)), scrH:Math.round(parseFloat(getComputedStyle(scr).height)),
     field:!!scr.querySelector('.field svg .sh-grid')&&!!scr.querySelector('.field svg .mv-stroke')&&!!scr.querySelector('.field svg .mv-fill')&&!!scr.querySelector('.field svg .mv-play')&&!!scr.querySelector('.field svg .mv-foll'),
     lent:!!window.__lfoField&&typeof window.__lfoField.svg==='function', chips:[...scr.querySelectorAll('.mv-ov.bot .mv-c')].map(e=>e.querySelector('.t').textContent), chevs:scr.querySelectorAll('.mv-ov.bot .mv-c svg.chev').length, icons:scr.querySelectorAll('.mv-ov.bot .mv-c .ic').length, selects:scr.querySelectorAll('.mv-ov.bot .mv-c select').length, chipPx:Math.round(parseFloat(getComputedStyle(scr.querySelector('.mv-c .t')).fontSize)*10)/10, barH:Math.round(parseFloat(getComputedStyle(scr.querySelector('.mv-ov.bot')).height)), togs:card.querySelectorAll('.pane .tog').length, tabCaps:getComputedStyle(card.querySelector('.tab')).textTransform, ttCaps:getComputedStyle(card.querySelector('.h .tt')).textTransform, lbCaps:getComputedStyle(card.querySelector('.cell .lb')).textTransform, tileCaps:getComputedStyle(card.querySelector('.fx .fxb')).textTransform, curInk:getComputedStyle(card.querySelector('.fx .fxb.cur')).color,
     tiles:card.querySelectorAll('.fx .fxb').length, cur:[...card.querySelectorAll('.fx .fxb')].map(b=>b.classList.contains('cur')).join(''), leds:card.querySelectorAll('.fleds i').length, tabs:[...card.querySelectorAll('.tab')].map(t=>t.textContent), chain:!!card.querySelector('.slotrow'),
     rows:[...card.querySelectorAll('.pane .gboxrow')].length, boxes:[...card.querySelectorAll('.pane.on .gbox .gbl')].map(e=>e.textContent),
     tilePaths:tile?tile.querySelectorAll('svg path').length:-1, tileCircles:tile?tile.querySelectorAll('svg circle').length:-1, tileTitle:tile?tile.getAttribute('title'):null, tileAnim:tile?!!tile.querySelector('animateTransform, animate'):false, tileSine:tile?!!tile.querySelector('path.shpWave'):false, tileClip:tile?!!tile.querySelector('clipPath'):false,
     caps:[...scr.querySelectorAll('div:not(.mv-ov):not(.mv-c):not(.field)')].map(e=>e.textContent.trim()).filter(t=>t&&!/Point|Gate|Ladder/.test(t)) }; });
 ok(!r0.err && r0.boot && r0.open && r0.title==='Shaper' && r0.mix && r0.width===316 && r0.scrH===124 && r0.field && r0.lent && r0.chips.join('|')==='Point|Sine|' && r0.chevs===3   /* tp102 — the type chip is a dropdown too (chevron, no arrows) */ && r0.icons===0 && r0.selects===3 && r0.chipPx<=7.5 && r0.barH<=18 && r0.togs===0 && r0.tabCaps==='none' && r0.ttCaps==='none' && r0.lbCaps==='none' && r0.tileCaps==='uppercase' && r0.curInk==='rgb(91, 33, 182)' && r0.tiles===8 && r0.cur==='truefalsefalsefalsefalsefalsefalsefalse' && r0.leds===8 && r0.tabs.join('|')==='Shape|Target|Clock' && r0.chain && r0.rows===3 && r0.boxes.join('|')==='Volume|Cycle' && r0.caps.length===0,
    '🚨 [0] the page BOOTED whole (the frame dispatcher, the wind clock and the filter roster are all there — a parse error in any script block fails here); the Glitch chassis (316 wide, the screen now 124 px, eight tiles with dots, Shape / Target / Clock, two boxes a row, the chain) with the LFO\'s field in the screen and a small bar (≤ 7.5 px chips, ≤ 18 px tall, three NATIVE dropdowns, no glyphs); no On toggle anywhere; no caps but the tiles; the lit + selected Volume tile is white-filled with dark ink', JSON.stringify(r0));
 ok(r0.tilePaths===1 && r0.tileCircles===0 && r0.tileTitle==='Shaper' && !r0.tileAnim && r0.tileSine && !r0.tileClip, '[0b] the tile\'s emblem is ONE sawtooth path between fixed ends — no clip, no SMIL, nothing else on it', JSON.stringify({paths:r0.tilePaths,circles:r0.tileCircles,title:r0.tileTitle,anim:r0.tileAnim,sine:r0.tileSine,clip:r0.tileClip}));
 const nm=await p.evaluate(()=>{ try{ return (function(){ const s=document.documentElement.outerHTML; return /FLOWNAME=\{arp:'Arp',drift:'Robin',chop:'Shaper'/.test(s); })(); }catch(e){ return false; } });
 ok(nm, '[0c] FLOWNAME.chop is Shaper (the Patcher, the browser and the pills say so)');

 // ── [1] drawn by the LFO's own renderer ──
 const r1=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const svg=()=>card.querySelector('.screen .field svg'); const eff=()=>svg().querySelector('.mv-stroke').getAttribute('d');
   const d0=eff(); const ptsVol=svg().querySelectorAll('.sh-nd').length, cdVol=svg().querySelectorAll('.sh-cd').length, hits=svg().querySelectorAll('.sh-hit').length;
   // byte-faithful: the field's stroke is the LFO's shPathD of the same points at the same geometry
   const S=window.__tiDice.chop.S; const pts=JSON.parse(S.v.pts0); const LF=window.__lfoField; const same=LF.pathD(pts,456,{H:196,T:10,B:168})===d0;
   const vb=svg().getAttribute('viewBox');
   card.querySelectorAll('.fx .fxb')[1].click(); await new Promise(r=>setTimeout(r,200));
   const d1=eff(); const ptsTime=svg().querySelectorAll('.sh-nd').length;
   const ys=(d1.match(/[ML](-?[\d.]+) (-?[\d.]+)/g)||[]).map(t=>+t.split(' ')[1]); const mono=ys.every((y,i)=>i===0||y<=ys[i-1]+0.01);
   const grid={v:svg().querySelectorAll('.sh-grid line').length, maj:svg().querySelectorAll('.sh-grid line.maj').length};
   const cs=getComputedStyle(svg().querySelector('.sh-grid line')); const gridInk={stroke:cs.stroke,w:cs.strokeWidth};
   card.querySelectorAll('.fx .fxb')[0].click(); await new Promise(r=>setTimeout(r,200));
   return {ptsVol,cdVol,hits,same,vb,ptsTime,mono,changed:d0!==d1,grid,gridInk}; });
 ok(r1.ptsVol===9 && r1.cdVol===8 && r1.hits===17 && r1.same && r1.vb==='0 0 456 196' && r1.ptsTime===2 && r1.mono && r1.grid.v===20 && r1.grid.maj===2 && /^rgba\(255, 255, 255, 0\.06[0-9]?\)$/.test(r1.gridInk.stroke) && r1.gridInk.w==='0.6px',
    '🚨 [1] the stroke IS the LFO\'s shPathD of the lane\'s points (byte-equal), the LFO\'s 456 × 196 field, its nodes / curve dots / hit targets, its grid ink (white .065, .6 wide); Volume boots as a SINE (tp79 — 9 nodes, Max: \'I don\\\'t want to see that gate\'), Time as a unity ramp (2 nodes, monotone)', JSON.stringify(r1));

 // ── [2] the bar: the type chip = the rosters through the rack\'s browser; the shape chip = the LFO\'s dropdown ──
 const r2=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const T=card.querySelectorAll('.fx .fxb'); const w=(ms)=>new Promise(r=>setTimeout(r,ms));
   const chip=(n)=>card.querySelector('.screen .mv-c.ch-'+n+' .t').textContent, vis=(n)=>getComputedStyle(card.querySelector('.screen .mv-c.ch-'+n)).display!=='none';
   const out={}; const S=window.__tiDice.chop.S;
   let browsed=null; const orig=window.openTwoPaneBrowser; window.openTwoPaneBrowser=function(ev,cfg){ browsed=cfg; };
   T[2].click(); await w(150); out.filt=chip('type'); out.filtVis=vis('type'); card.querySelector('.screen .mv-c.ch-type').click(); await w(100); out.filtCats=browsed?browsed.cats.map(c=>c.label):null; out.filtN=browsed?browsed.cats.reduce((a,c)=>a+c.items.length,0):0;
   const acid=browsed&&browsed.cats.flatMap(c=>c.items).find(it=>it.name==='Acid 303'); if(acid) acid.pick(); await w(300); out.filtAfter=chip('type'); out.filtParam=window.__params['FLOW_CHOP_FILT_MODE'];
   const sel=(n)=>card.querySelector('.screen .mv-c.ch-'+n+' select'); const opts=(n)=>[...sel(n).options].map(o=>o.textContent);
   T[5].click(); await w(150); out.drive=chip('type'); out.driveOpts=opts('type').length; sel('type').value='6'; sel('type').dispatchEvent(new Event('change')); await w(300); out.driveAfter=chip('type'); out.driveParam=window.__params['FLOW_CHOP_DRIVE_MODE'];
   T[6].click(); await w(150); out.ph=chip('type'); out.phOpts=opts('type').length;
   T[7].click(); await w(150); out.cr=chip('type'); out.crOpts=opts('type').length; out.filtSelHidden=false; T[2].click(); await w(150); out.filtSelHidden=getComputedStyle(sel('type')).display==='none';
   window.openTwoPaneBrowser=orig;
   T[1].click(); await w(150); out.timeShape=chip('shape'); out.timeTypeVis=vis('type'); out.menuRows=opts('shape');
   const dA=card.querySelector('.screen .field svg .mv-stroke').getAttribute('d'); sel('shape').value='1'; sel('shape').dispatchEvent(new Event('change')); await w(250); out.timeAfter=chip('shape'); out.restamped=card.querySelector('.screen .field svg .mv-stroke').getAttribute('d')!==dA;
   T[0].click(); await w(150); out.volShape=chip('shape'); out.volTypeVis=vis('type');
   S.set('mode2',0); S.set('mode5',5); return out; });
 ok(r2.filt==='Ladder LP 24' && r2.filtVis && r2.filtCats && r2.filtCats[0]==='Ladder' && r2.filtN===118 && r2.filtAfter==='Acid 303' && Math.abs((r2.filtParam||0)-4/117)<0.002
    && r2.drive==='Soft Clip' && r2.driveOpts===23 && r2.driveAfter==='Hard Clip' && Math.abs((r2.driveParam||0)-6/22)<0.01
    && r2.ph==='Phaser' && r2.phOpts===28 && r2.cr==='Bits + Rate' && r2.crOpts===10 && r2.filtSelHidden
    && r2.timeShape==='Unity' && !r2.timeTypeVis && r2.menuRows && r2.menuRows.indexOf('Unity')===0 && r2.menuRows.indexOf('Half time')===1 && r2.menuRows.indexOf('Custom')===r2.menuRows.length-1 && r2.menuRows.length>=12 && r2.timeAfter==='Half time' && r2.restamped && r2.volShape==='Sine' && !r2.volTypeVis,
    '🚨 [2] the Filter\'s type chip opens the RACK\'S two-pane browser (118 in the filter\'s categories) and picking writes the MODE parameter; Drive (23) / Phaser (28) / Crush (10) are native dropdowns writing theirs; the shape chip is a native dropdown (Time: the presets + Custom) and restamps; Volume / Time carry no type chip', JSON.stringify(r2));

 // ── [3] brushes: the brush chip\'s dropdown; the LFO field takes the strokes ──
 const r3=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const field=card.querySelector('.screen .field'); const r=field.getBoundingClientRect(); const w=(ms)=>new Promise(r=>setTimeout(r,ms));
   const pick=async(name)=>{ const se=card.querySelector('.screen .mv-c.ch-brush select'); const o=[...se.options].find(x=>x.textContent===name); if(!o) return false; se.value=o.value; se.dispatchEvent(new Event('change')); await w(80); return true; };
   const sx=(x)=>r.left+r.width*x, sy=(y)=>r.top+r.height*(168-y*158)/196;
   const okLine=await pick('Line'); const chipT=card.querySelector('.screen .mv-c.ch-brush .t').textContent;
   const px=sx(0.30), py=sy(0.25);
   field.dispatchEvent(new PointerEvent('pointerdown',{clientX:px,clientY:py,bubbles:true,pointerId:1,button:0})); await w(30);
   field.dispatchEvent(new PointerEvent('pointerup',{clientX:px,clientY:py,bubbles:true,pointerId:1,button:0})); await w(200);
   const pts=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[0].pts;
   const inCell=pts.filter(q=>q[0]>=0.25-1e-6&&q[0]<=0.3125+1e-6); const flat25=inCell.length>=2&&inCell.every(q=>Math.abs(q[1]-0.25)<0.03);
   await pick('Point'); const n0=pts.length; const px2=sx(0.61), py2=sy(0.4); const pushes0=window.__shpJson.length;
   field.dispatchEvent(new PointerEvent('pointerdown',{clientX:px2,clientY:py2,bubbles:true,pointerId:2,button:0})); await w(30);
   field.dispatchEvent(new PointerEvent('pointerup',{clientX:px2,clientY:py2,bubbles:true,pointerId:2,button:0})); await w(250);
   const clickAdded=window.__shpJson.length!==pushes0;   /* the LFO's law: a plain click on empty ground adds NOTHING */
   field.dispatchEvent(new MouseEvent('dblclick',{clientX:px2,clientY:py2,bubbles:true,cancelable:true})); await w(250);
   const pts2=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[0].pts; const added=pts2.find(q=>Math.abs(q[0]-0.625)<1e-6);
   await pick('Free draw');
   field.dispatchEvent(new PointerEvent('pointerdown',{clientX:sx(0.05),clientY:sy(0.2),bubbles:true,pointerId:3,button:0}));
   for(let k=1;k<=40;k++){ const x=0.05+0.9*k/40; field.dispatchEvent(new PointerEvent('pointermove',{clientX:sx(x),clientY:sy(0.2+0.6*Math.abs(Math.sin(x*6))),bubbles:true,pointerId:3,button:0})); await w(4); }
   field.dispatchEvent(new PointerEvent('pointerup',{clientX:sx(0.95),clientY:sy(0.3),bubbles:true,pointerId:3,button:0})); await w(250);
   const pts3=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[0].pts; const shapeChip=card.querySelector('.screen .mv-c.ch-shape .t').textContent;
   const inStroke=pts3.filter(q=>q[0]>0.06&&q[0]<0.94); const unquantised=inStroke.filter(q=>Math.abs(q[0]*128-Math.round(q[0]*128))>0.02).length; const faithful=inStroke.every(q=>Math.abs(q[1]-(0.2+0.6*Math.abs(Math.sin(q[0]*6))))<0.06);
   await pick('Point');
   return {okLine,chipT,flat25,inCell:inCell.length,n0,n1:pts2.length,clickAdded,snapped:!!added,free:pts3.length,inStroke:inStroke.length,unquantised,faithful,shapeChip,pushes:window.__shpJson.length}; });
 ok(r3.okLine && r3.chipT==='Line' && r3.flat25 && !r3.clickAdded && r3.n1===r3.n0+1 && r3.snapped && r3.free>=20 && r3.unquantised>=10 && r3.faithful && r3.shapeChip==='Custom',
    '[3] the brush dropdown: Line stamps a flat at the pointer\'s height into its grid cell; with Point a plain click adds NOTHING and a DOUBLE-click adds a point that Snap lands on the grid (0.625 = 10/16); Free draw is water — the stroke\'s points sit at their own x (not on any grid) and on the curve that was drawn, and only the collinear ones go; the shape chip then reads Custom', JSON.stringify(r3));
 // ── [4] the push ──
 const r4=await p.evaluate(()=>{ const last=window.__shpJson[window.__shpJson.length-1]; const o=JSON.parse(last[1]); return {inst:last[0],lanes:o.lanes.length,sense:o.sense,keys:Object.keys(o.lanes[2]).sort().join(','),k:o.lanes[2].k.length,kf:o.lanes[2].k.join(','),pts:o.lanes.every(L=>L.pts.length>=2&&L.pts[0][0]===0&&L.pts[L.pts.length-1][0]===1)}; });
 ok(r4.inst===0 && r4.lanes===18 && r4.sense===0.5 && r4.keys==='blend,floor,grid,k,phase,pts,smooth,swing,tension' && r4.k===6 && r4.kf==='0.3,0,1,0,0,0' && r4.pts,
    '🚨 [4] every edit pushes setShaperJson(inst, {sense, lanes:[18 × {pts (pinned 0..1), smooth, phase, tension, floor, blend, swing, grid, k[6]}]}); the Filter lane\'s k boots Reso .3 / Drive 0 / Poles 24 dB / Tube / Spread 0 / Punch 0', JSON.stringify(r4));

 // ── [5] parameters + the dice ──
 const r5=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const t=card.querySelectorAll('.fx .fxb'); const out={};
   const S=window.__tiDice.chop.S; out.on0=S.v.on0; out.dep0=S.v.depth0; out.rate0=S.v.rate0;
   t[3].click(); await new Promise(r=>setTimeout(r,80)); t[3].click(); await new Promise(r=>setTimeout(r,500)); out.on3=window.__params['FLOW_CHOP_PAN_ON']; out.led3=card.querySelectorAll('.fleds i')[3].classList.contains('on');
   t[3].click(); await new Promise(r=>setTimeout(r,80)); t[3].click(); await new Promise(r=>setTimeout(r,500)); out.on3b=window.__params['FLOW_CHOP_PAN_ON'];
   const before=JSON.stringify(window.__tiDice.chop.S.v); window.__tiDiceMode('chop',true); await new Promise(r=>setTimeout(r,80)); out.diceMoved=JSON.stringify(window.__tiDice.chop.S.v)!==before;
   t[0].click(); return out; });
 ok(r5.on0===1 && r5.dep0===1 && r5.rate0===4 && r5.on3===1 && r5.led3 && r5.on3b===0 && !r5.diceMoved,
    '[5] On / Depth / Rate ride FLOW_CHOP_<LANE>_ parameters (Volume on, depth 1, rate 1 bar); a double-tap on the lit Pan tile toggles its lane on and off; the dice moves nothing', JSON.stringify(r5));

 // ── [6] the menu — on the right pointer-DOWN (macOS opens menus there), the contextmenu of the same gesture swallowed ──
 const r6=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const svg=card.querySelector('.screen .field'); const r=svg.getBoundingClientRect();
   let shows=0; const orig=window.__synShowMenu; window.__synShowMenu=function(){ shows++; return orig.apply(this,arguments); };
   svg.dispatchEvent(new PointerEvent('pointerdown',{clientX:r.left+80,clientY:r.top+60,bubbles:true,cancelable:true,pointerId:9,button:2}));
   svg.dispatchEvent(new MouseEvent('contextmenu',{clientX:r.left+80,clientY:r.top+60,bubbles:true,cancelable:true})); await new Promise(r=>setTimeout(r,300));
   const m=document.getElementById('syn-ctx-menu'); const txt=m?m.textContent:''; const vis=m?getComputedStyle(m).display!=='none':false;
   const inBody=m&&m.parentElement===document.body, mr=m.getBoundingClientRect(); const cr=card.getBoundingClientRect(); const overlap=mr.left<cr.right&&mr.right>cr.left&&mr.top<cr.bottom&&mr.bottom>cr.top;
   const rowEl=[...m.querySelectorAll('*')].find(el=>/Flip vertical/.test(el.textContent)&&el.children.length===0)||m; const rr=rowEl.getBoundingClientRect(); const hit=document.elementFromPoint(rr.left+rr.width/2, rr.top+rr.height/2); const onTop=!!(hit&&m.contains(hit));
   const rows=m.querySelectorAll('.syn-ctx-item, [role="menuitem"]').length||m.children.length;
   window.__synShowMenu=orig; try{ window.__synHideMenu(); }catch(e){} await new Promise(r=>setTimeout(r,60)); const home=m.parentElement&&m.parentElement.id==='syn-panel';
   return {vis, shows, inBody, overlap, onTop, rows, home, hasGrid:/Grid/.test(txt), level:/Level/.test(txt), snap:/Snap/.test(txt), flips:/Flip vertical/.test(txt)&&/Flip horizontal/.test(txt), random:/Random flat/.test(txt)&&/Random both/.test(txt), noTools:!/Point · edit/.test(txt)&&!/Free draw/.test(txt), wt:/Wavetable → Shape/.test(txt)&&/Oscillator D/.test(txt), noStock:!/Stairs/.test(txt), clear:/Clear/.test(txt), noExtend:!/Extend/.test(txt)}; });
 ok(r6.vis && r6.shows===1 && r6.inBody && r6.overlap && r6.onTop && r6.home && r6.rows<20 && r6.hasGrid && r6.level && r6.snap && r6.flips && r6.random && r6.noTools && r6.wt && r6.noStock && r6.clear && r6.noExtend,
    '🚨 [6] the right pointer-DOWN opens the menu ONCE, and it paints ON TOP of the card (portaled to the body while a card floats — elementFromPoint over the card lands in the menu; home in the panel after it closes), trimmed: Grid + Level, Snap, the flips, Random ×3, Wavetable → Shape, Clear — no tools, no stock list, no Extend', JSON.stringify(r6));


 // ── [7] presets ──
 const r7=await p.evaluate(async()=>{ try{ window.__synHideMenu&&window.__synHideMenu(); }catch(e){} const card=document.querySelector('.ti-card.shp-ext'); const S=window.__tiDice.chop.S;
   const pill=card.querySelector('.h .pset .pn'); pill.click(); await new Promise(r=>setTimeout(r,300));
   const items=[...document.querySelectorAll('.pmenu .pi .nm')].map(e=>e.textContent); const acid=[...document.querySelectorAll('.pmenu .pi')].find(e=>/Acid sweep/.test(e.textContent));
   if(acid) acid.click(); await new Promise(r=>setTimeout(r,500));
   return {items:items.slice(0,20), lane:S.v.lane, on2:S.v.on2, on0:S.v.on0, mode2:S.v.mode2, param:window.__params['FLOW_CHOP_FILT_MODE'], type:card.querySelector('.screen .mv-c.ch-type .t').textContent, shape:card.querySelector('.screen .mv-c.ch-shape .t').textContent}; });
 ok(r7.items.some(x=>/Gate 16ths/.test(x)) && r7.items.some(x=>/Acid sweep/.test(x)) && r7.items.some(x=>/Tape scrub/.test(x)) && r7.lane===2 && r7.on2===1 && r7.on0===0 && r7.mode2===4 && Math.abs((r7.param||0)-4/117)<0.002 && r7.type==='Acid 303' && r7.shape==='Saw',
    '[7] the house preset menu lists the factory shapes; Acid sweep recalls onto the Filter lane, lights it, sets Acid 303 (parameter, type chip) and the Saw (shape chip)', JSON.stringify(r7));

 // ── [8] 🚨 the lane\'s knobs MOVE THE LINE, and the nodes ride it ──
 const r8=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const S=window.__tiDice.chop.S; const w=(ms)=>new Promise(r=>setTimeout(r,ms));
   card.querySelectorAll('.fx .fxb')[3].click(); await w(150);   // Pan: a sine
   const svg=()=>card.querySelector('.screen .field svg'); const eff=()=>svg().querySelector('.mv-stroke').getAttribute('d'); const nodes=()=>[...svg().querySelectorAll('.sh-nd')].map(n=>n.getAttribute('cx')+','+n.getAttribute('cy')).join('|');
   const ys=(d)=>(d.match(/[ML](-?[\d.]+) (-?[\d.]+)/g)||[]).map(t=>+t.split(' ')[1]);
   const onLine=()=>{ const d=eff(); const pts=(d.match(/[ML](-?[\d.]+) (-?[\d.]+)/g)||[]).map(t=>t.slice(1).split(' ').map(Number)); return [...svg().querySelectorAll('.sh-nd')].every(n=>{ const cx=+n.getAttribute('cx'), cy=+n.getAttribute('cy'); return pts.some(q=>Math.abs(q[0]-cx)<0.6&&Math.abs(q[1]-cy)<0.6); }); };
   const d0=eff(), n0=nodes();
   S.set('tension3',0.9); await w(60); const dT=eff(), nT=nodes(), onT=onLine();
   S.set('tension3',0.5); S.set('phase3',0.25); await w(60); const dP=eff(), onP=onLine();
   S.set('phase3',0); S.set('floor3',0.4); await w(60); const dF=eff(); const maxF=Math.max(...ys(dF)); const onF=onLine();
   S.set('floor3',0); S.set('swing3',0.7); await w(60); const dW=eff(), onW=onLine();
   S.set('swing3',0); await w(60); const dBack=eff(), nBack=nodes();
   card.querySelectorAll('.fx .fxb')[0].click();
   return {tension:dT!==d0, tensionNodesMoved:nT!==n0, onT, phase:dP!==d0, onP, floor:dF!==d0, floorRaised:maxF<Math.max(...ys(d0))-8, onF, swing:dW!==d0, onW, back:dBack===d0, nodesBack:n0===nBack}; });
 ok(r8.tension && r8.onT && r8.phase && r8.onP && r8.floor && r8.floorRaised && r8.onF && r8.swing && r8.onW && r8.back && r8.nodesBack,
    '🚨 [8] Tension, Phase, Floor and Swing each move the drawn line and every node stays ON the line (the LFO draws the points the DSP reads); at rest the line and the nodes are back', JSON.stringify(r8));

 // ── [9] the Target tab — every lane\'s back panel, in the Glitch\'s two boxes ──
 const r9=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const w=(ms)=>new Promise(r=>setTimeout(r,ms)); const T=card.querySelectorAll('.fx .fxb');
   [...card.querySelectorAll('.tab')].find(t=>t.textContent==='Target').click(); await w(100);
   const row=card.querySelector('.pane[data-p="target"] .gboxrow'); const L=row.querySelectorAll('.gbox')[0], R=row.querySelectorAll('.gbox')[1]; const out={};
   for(let i=0;i<8;i++){ T[i].click(); await w(120); out[i]={ title:L.querySelector('.gbl').textContent, on:!!L.querySelector('.tog'), knobs:[...L.querySelectorAll('.cell.k .lb')].map(e=>e.textContent), steps:[...R.querySelectorAll('.cell .lb')].map(e=>e.textContent), stepVals:[...R.querySelectorAll('.step em')].map(e=>e.textContent), rightShown:getComputedStyle(R).display!=='none' }; }
   T[2].click(); await w(120); const S=window.__tiDice.chop.S; S.set('k0_2',0.8); await w(200); const k=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[2].k;
   const sel=R.querySelectorAll('.step select')[0]; sel.value='1'; sel.dispatchEvent(new Event('change')); await w(250); const k2=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[2].k;
   S.set('k0_2',0.3); S.set('k2_2',1); S.set('sk_k2_2',3); T[0].click(); [...card.querySelectorAll('.tab')].find(t=>t.textContent==='Shape').click();
   return {lanes:out, k0:k[0], k2:k2[2]}; });
 const L9=r9.lanes;
 ok(L9[0].title==='Volume' && !L9[0].on && L9[0].knobs.join()==='Attack,Release,Punch,Hold' && L9[0].steps.join()==='Mode' && L9[0].stepVals.join()==='Gain'
    && L9[1].title==='Time' && L9[1].knobs.join()==='Fade,Glide,Range,Tone' && !L9[1].rightShown
    && L9[2].title==='Filter' && L9[2].knobs.join()==='Reso,Drive,Spread,Punch' && L9[2].steps.join()==='Poles,Char' && L9[2].stepVals.join()==='24 dB,Tube'
    && L9[3].title==='Pan' && L9[3].knobs.join()==='Width,Bass,Haas,Tilt' && L9[3].steps.join()==='Law'
    && L9[4].title==='Repeat' && L9[4].knobs.join()==='Seam,Decay,Pitch,Tone' && L9[4].steps.join()==='Mode'
    && L9[5].title==='Drive' && L9[5].knobs.join()==='Tone,Makeup,Bias,Knee' && L9[5].steps.join()==='Char'
    && L9[6].title==='Phaser' && L9[6].knobs.join()==='Feedbk,Stereo,Drive,Centre' && !L9[6].rightShown
    && L9[7].title==='Crush' && L9[7].knobs.join()==='Bits,Rate,Tone,Stereo' && !L9[7].rightShown
    && [0,1,2,3,4,5,6,7].every(function(q){ return L9[q].knobs.length===4; })   /* L9 is keyed by index, not an array */
     && Math.abs(r9.k0-0.8)<1e-6 && Math.abs(r9.k2-1/3)<1e-6,
    '🚨 [9] the Target tab: FOUR knobs on EVERY lane (tp81 — Hold · Tone · Punch · Tilt · Tone · Knee · Centre · Stereo; Filter Punch and Drive Knee were already in the rack engines and had never been reached, the other six are new DSP and FlowShaper_test T27 proves each audible), no On (Volume Attack/Release/Punch + Mode · Time Fade/Glide/Range · Filter Reso/Drive/Spread + Poles/Char · Pan Width/Bass/Haas + Law · Repeat Seam/Decay/Pitch + Mode · Drive Tone/Makeup/Bias + Char · Phaser Feedbk/Stereo/Drive · Crush Bits/Rate/Tone); a knob or a step writes the lane\'s k', JSON.stringify({f:L9[2],k0:r9.k0,k2:r9.k2}));

 // ── [10] the Clock tab: Rate / Grid / Trigger as the Glitch\'s OUT-style dropdowns; Sense beside them ──
 const r10=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const w=(ms)=>new Promise(r=>setTimeout(r,ms));
   [...card.querySelectorAll('.tab')].find(t=>t.textContent==='Clock').click(); await w(100);
   const pane=card.querySelector('.pane[data-p="clock"]'); const boxes=[...pane.querySelectorAll('.gbox')].map(b=>({t:b.querySelector('.gbl').textContent,steps:[...b.querySelectorAll('.cell .lb')].map(e=>e.textContent),vals:[...b.querySelectorAll('.step em')].map(e=>e.textContent),knobs:[...b.querySelectorAll('.cell.k .lb')].map(e=>e.textContent)}));
   const sels=pane.querySelectorAll('.gbox .step select'); sels[0].value='1'; sels[0].dispatchEvent(new Event('change')); await w(400); const rateParam=window.__params['FLOW_CHOP_VOL_RATE'];
   sels[2].value='3'; sels[2].dispatchEvent(new Event('change')); await w(400); const trigParam=window.__params['FLOW_CHOP_VOL_TRIG'];
   sels[1].value='4'; sels[1].dispatchEvent(new Event('change')); await w(300); const gridLines=card.querySelectorAll('.screen .field svg .sh-grid line').length; const gridJson=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[0].grid;
   sels[0].value='4'; sels[0].dispatchEvent(new Event('change')); sels[2].value='0'; sels[2].dispatchEvent(new Event('change')); sels[1].value='6'; sels[1].dispatchEvent(new Event('change')); await w(300); [...card.querySelectorAll('.tab')].find(t=>t.textContent==='Shape').click();
   return {boxes, rateParam, trigParam, gridLines, gridJson}; });
 ok(r10.boxes.length===2 && r10.boxes[0].t==='Clock' && r10.boxes[0].steps.join()==='Rate,Grid,Trigger' && r10.boxes[0].vals.join()==='1 bar,1/16,Sync' && r10.boxes[1].t==='Trigger' && r10.boxes[1].knobs.join()==='Sense'
    && Math.abs((r10.rateParam||0)-1/7)<0.01 && Math.abs((r10.trigParam||0)-1)<0.01 && r10.gridJson===8 && r10.gridLines===7+5,
    '[10] the Clock tab: Rate · Grid · Trigger dropdowns in the Clock box (1 bar / 1/16 / Sync at rest), Sense in the Trigger box; 1/8 writes FLOW_CHOP_VOL_RATE, MIDI writes FLOW_CHOP_VOL_TRIG, a Grid pick redraws the field (1/8 = 7 + 5 lines) and pushes grid 8', JSON.stringify(r10));

 // ── [11] the follower ──
 const r11=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const w=(ms)=>new Promise(r=>setTimeout(r,ms)); const scr=card.querySelector('.screen'); const head=()=>scr.querySelector('.field .mv-play'), dot=()=>scr.querySelector('.field .mv-foll');
   const F={ph:[0.10,0,0,0,0,0,0,0],v:[1,0,0,0,0,0,0,0],ln:[1,0,0,0,0,0,0,0],tr:[0,0,0,0,0,0,0,0],b:120,on:1,pl:1}; window.__flowFeedPush={arp:[],gli:[],chop:[F]};
   await w(120); const idle0=scr.classList.contains('idle'); const xs=[]; for(let k=0;k<18;k++){ await w(16); xs.push(+head().getAttribute('x1')); }
   const x0=xs[0], x1=xs[xs.length-1]; const mono=xs.every((x,i)=>i===0||x>=xs[i-1]-0.01); const moved=x1>x0+2;
   F.ph[0]=0.5; window.__flowFeedPush={arp:[],gli:[],chop:[Object.assign({},F)]}; await w(60); const xJump=+head().getAttribute('x1');
   const dotOnHead=Math.abs(+dot().getAttribute('cx')-xJump)<0.6;
   window.__flowFeedPush={arp:[],gli:[],chop:[{on:0}]}; await w(120); const idle1=scr.classList.contains('idle');
   return {idle0, x0:+x0.toFixed(1), x1:+x1.toFixed(1), mono, moved, xJump:+xJump.toFixed(1), near:Math.abs(xJump-228)<40, dotOnHead, idle1}; });
 ok(!r11.idle0 && r11.mono && r11.moved && r11.near && r11.dotOnHead && r11.idle1,
    '🚨 [11] the follower (the LFO\'s head and dot) rides the push and ADVANCES on the tempo between pushes (never backwards), snaps to a jump (0.10 → 0.50), the dot rides the head, and both fade when the lane is idle', JSON.stringify(r11));



 // ── [15] the LFO's grammar: click selects, band-select takes many, the group drags as one, double-click on a node deletes ──
 const r15=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const S=window.__tiDice.chop.S; const w=(ms)=>new Promise(r=>setTimeout(r,ms)); const field=card.querySelector('.screen .field'); const r=field.getBoundingClientRect();
   card.querySelectorAll('.fx .fxb')[3].click(); await w(150);   // Pan: a 3-point sine
   S.set('pts3',JSON.stringify([[0,0,0],[.25,.6,0],[.5,1,0],[.75,.6,0],[1,0,0]])); await w(100); const sx=(x)=>r.left+r.width*x, sy=(y)=>r.top+r.height*(168-y*158)/196;
   const nodes=()=>card.querySelectorAll('.screen .field svg .sh-nd').length, selN=()=>card.querySelectorAll('.screen .field svg .sh-nd.sel').length;
   // a plain click on a node SELECTS it (no move)
   field.dispatchEvent(new PointerEvent('pointerdown',{clientX:sx(.5),clientY:sy(1),bubbles:true,pointerId:1,button:0})); await w(30); field.dispatchEvent(new PointerEvent('pointerup',{clientX:sx(.5),clientY:sy(1),bubbles:true,pointerId:1,button:0})); await w(100);
   const sel1=selN(); const ptsA=JSON.parse(S.v.pts3);
   // the rubber-band over the two middle-left points
   field.dispatchEvent(new PointerEvent('pointerdown',{clientX:sx(.15),clientY:sy(1.02),bubbles:true,pointerId:2,button:0})); await w(20);
   for(let k=1;k<=8;k++){ field.dispatchEvent(new PointerEvent('pointermove',{clientX:sx(.15+.4*k/8),clientY:sy(1.02-0.6*k/8),bubbles:true,pointerId:2,button:0})); await w(12); }
   const boxLive=!!card.querySelector('.screen .field svg .sh-selbox'); field.dispatchEvent(new PointerEvent('pointerup',{clientX:sx(.55),clientY:sy(.42),bubbles:true,pointerId:2,button:0})); await w(100);
   const selBand=selN(), boxGone=!card.querySelector('.screen .field svg .sh-selbox');
   // drag one of the selected: the WHOLE selection rides
   field.dispatchEvent(new PointerEvent('pointerdown',{clientX:sx(.25),clientY:sy(.6),bubbles:true,pointerId:3,button:0})); await w(20);
   for(let k=1;k<=6;k++){ field.dispatchEvent(new PointerEvent('pointermove',{clientX:sx(.25),clientY:sy(.6-0.3*k/6),bubbles:true,pointerId:3,button:0})); await w(12); }
   field.dispatchEvent(new PointerEvent('pointerup',{clientX:sx(.25),clientY:sy(.3),bubbles:true,pointerId:3,button:0})); await w(250);
   const ptsB=JSON.parse(S.v.pts3); const moved=[1,2].every(k=>ptsB[k][1]<ptsA[k][1]-0.2), others=[0,3,4].every(k=>Math.abs(ptsB[k][1]-ptsA[k][1])<1e-6);
   // double-click a node: gone
   const n0=nodes(); field.dispatchEvent(new MouseEvent('dblclick',{clientX:sx(.75),clientY:sy(.6),bubbles:true,cancelable:true})); await w(250); const n1=nodes();
   card.querySelectorAll('.fx .fxb')[0].click(); await w(100);
   return {sel1, boxLive, selBand, boxGone, moved, others, n0, n1}; });
 ok(r15.sel1===1 && r15.boxLive && r15.selBand===2 && r15.boxGone && r15.moved && r15.others && r15.n1===r15.n0-1,
    '🚨 [15] the LFO\'s grammar: a click SELECTS a node, dragging empty ground rubber-bands a selection (2 of 5), grabbing one selected node drags the whole selection (the other three stay), a double-click on a node deletes it', JSON.stringify(r15));

 // ── [16] a lit lane FILLS white (the LFO / Glitch button); tp77 — with nothing around it (bar [34]) ──
 const r16=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const w=(ms)=>new Promise(r=>setTimeout(r,ms)); const t=card.querySelectorAll('.fx .fxb');
   const cs0=getComputedStyle(t[0]); const on0={bg:cs0.backgroundColor,ink:cs0.color}; const cs3=getComputedStyle(t[3]); const off3={bg:cs3.backgroundColor};
   t[3].click(); await w(80); t[3].click(); await w(400); const cs3b=getComputedStyle(t[3]); const on3={bg:cs3b.backgroundColor,ink:cs3b.color,border:cs3b.borderColor,cur:t[3].classList.contains('cur'),on:t[3].classList.contains('on')};
   t[3].click(); await w(80); t[3].click(); await w(400); const cs3c=getComputedStyle(t[3]); const off3b={bg:cs3c.backgroundColor}; t[0].click();
   return {on0, off3, on3, off3b}; });
 ok(/rgba\(255, 255, 255, 0\.9/.test(r16.on0.bg) && r16.off3.bg==='rgba(0, 0, 0, 0)' && /rgba\(255, 255, 255, 0\.9/.test(r16.on3.bg) && r16.on3.on && r16.on3.cur && r16.off3b.bg==='rgba(0, 0, 0, 0)',
    '[16] Volume (on) is filled white at boot; a double-tap lights Pan white (tp77: PLAIN white — the ring went, see [34]); another double-tap empties it', JSON.stringify(r16));

 // ── [17] ONE LINE, ONE GRID: the LFO pane and the LFO card draw the Shaper\'s line (≈1.07 px), the white .065 grid, the small nodes ──
 const r17=await p.evaluate(()=>{ const zf=parseFloat(getComputedStyle(document.documentElement).zoom)||1;
   const pane=document.querySelector('#mod-engine .mv-scope svg'); const shp=document.querySelector('.ti-card.shp-ext .screen .field svg');
   const px=(svg,sel,prop)=>{ const el=svg&&svg.querySelector(sel); if(!el) return null; const v=parseFloat(getComputedStyle(el)[prop]); const vb=svg.getAttribute('viewBox').split(' '); const scale=svg.getBoundingClientRect().width/zf/parseFloat(vb[2]); return +(v*scale).toFixed(2); };
   const gridInk=(svg)=>{ const el=svg&&svg.querySelector('.sh-grid line, .mv-grid line'); return el?getComputedStyle(el).stroke:null; };
   return { paneStroke:px(pane,'.mv-stroke','strokeWidth'), shpStroke:px(shp,'.mv-stroke','strokeWidth'), paneGrid:gridInk(pane), shpGrid:gridInk(shp), paneNode:px(pane,'.sh-nd','r'), shpNode:px(shp,'.sh-nd','r'), paneHasNodes:!!(pane&&pane.querySelector('.sh-nd')) }; });
 ok(r17.paneStroke!=null && r17.shpStroke!=null && Math.abs(r17.paneStroke-r17.shpStroke)<0.25 && /rgba\(255, 255, 255, 0\.06/.test(r17.paneGrid) && /rgba\(255, 255, 255, 0\.06/.test(r17.shpGrid) && (!r17.paneHasNodes || Math.abs(r17.paneNode-r17.shpNode)<0.4),
    '🚨 [17] ONE LINE, ONE GRID: the LFO pane\'s stroke lands within a quarter pixel of the Shaper\'s, both grids are the white .065 ink, the nodes match', JSON.stringify(r17));
 // ── [12] the other lit lanes ghost behind the line ──
 const r12=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const S=window.__tiDice.chop.S; const w=(ms)=>new Promise(r=>setTimeout(r,ms));
   S.set('on2',0); await w(80); const n0=card.querySelectorAll('.screen .field svg .sh-ghost').length; S.set('on3',1); await w(80); const n1=card.querySelectorAll('.screen .field svg .sh-ghost').length; const g=card.querySelector('.screen .field svg .sh-ghost');
   const cs0=g?getComputedStyle(g):null, cs=cs0?{stroke:cs0.stroke,fill:cs0.fill}:null;   /* a SNAPSHOT — the style is live and the field is rebuilt below */ const before=g&&g.previousElementSibling&&g.previousElementSibling.classList.contains('mv-fill'), under=g&&g.nextElementSibling&&g.nextElementSibling.classList.contains('mv-stroke');
   S.set('on3',0); await w(80); const n2=card.querySelectorAll('.screen .field svg .sh-ghost').length;
   return {n0,n1,n2,faint:cs?cs.stroke:null,fill:cs?cs.fill:null,before,under}; });
 ok(r12.n0===0 && r12.n1===1 && r12.n2===0 && /rgba\(236, 232, 242, 0\.1[5-9]\)/.test(r12.faint) && r12.fill==='none' && r12.before && r12.under,
    '[12] a second lit lane draws as a faint ghost behind the line (after the fill, before the stroke) and leaves when it is unlit', JSON.stringify(r12));
 // ── [13] tp71b — the tile's sine rides the motion clock: one period per beat of the host BPM, winds up on MIDI, winds down
 //        onto its home (phase 0 — the same picture every time), and with Motion off it never moves ──
 const r13=await p.evaluate(async()=>{ try{ const c=window.__flowCardOf('chop',1); if(c&&c.close) c.close(); }catch(e){}
   const el=document.querySelector('#syn-panel .flow-mode[data-mode="chop"] path.shpWave'); if(!el) return {err:'no sine'};
   const home=el.getAttribute('d'); const S=()=>window.__tiWindState('tile-chop');
   const sl=(ms)=>new Promise(r=>setTimeout(r,ms)); const frame=()=>{ try{ window.__tiFrame(); }catch(e){} };
   const run=async(live,frames,bpm)=>{ window.__hostBpm=bpm; for(let i=0;i<frames;i++){ window.__notesActive=live?1:0; window.__notesActiveT=live?Date.now():(Date.now()-2000); frame(); await sl(16); } };
   window.__tiMotionSet(true);
   const t0=S()?S().tau:0; await run(true,45,120); const t1=S().tau; const dMoving=el.getAttribute('d');   // 0.72 s live at 120 BPM: the wind-up (0.25 s) then ~1 period/beat → ≈ 0.9..1.2 periods
   await run(true,60,240); const t2=S().tau;   // a second at 240 BPM travels ≈ twice as far per second
   await run(false,120,240); await sl(50); frame(); await sl(20); const st=Object.assign({},S()); const dRest=el.getAttribute('d');   /* a SNAPSHOT: K[key] is live and the page's own lane keeps calling */   // 2 s with the sound gone: the ending lands on an integer phase
   window.__tiMotionSet(false); await sl(100); const dOff0=el.getAttribute('d'); await run(true,45,120); const dOff1=el.getAttribute('d'); const tOff=S().tau;
   window.__notesActive=0; window.__tiMotionSet(true);
   return { st:{tau:st.tau,m:st.m,park:!!st.park,r:st.r,on:st.on}, moved:dMoving!==home, v120:+(t1-t0).toFixed(2), v240:+(t2-t1).toFixed(2), landed:Math.abs(st.tau-Math.round(st.tau))<1e-6 && st.m===0 && !st.park, homeAgain:dRest===home,
            offStill:dOff0===home && dOff1===home && Math.abs(tOff-Math.round(tOff))<1e-6 }; });
 ok(!r13.err && r13.moved && r13.v120>0.8 && r13.v120<1.6 && r13.v240>3.2 && r13.v240<4.8 && r13.landed && r13.homeAgain && r13.offStill,   /* 0.72 s at 120 BPM less the 0.25 s wind-up ≈ 1.2 periods; 0.96 s at 240 BPM ≈ 3.84 */
    '[13] the sawtooth travels ~1 period per beat (120 vs 240 BPM: ~2x), winds up on MIDI, winds DOWN onto the same home picture, and with Motion off it stays on that picture', JSON.stringify(r13));


 // ══ tp77 ══════════════════════════════════════════════════════════════════════════════════════
 // ── [25] THE BAR STANDS STILL AND SITS ON ONE MIDLINE. Max: "every time I try to click on point or
 //        custom, it gets lower … draw a line through the center of that bottom — it needs to go through
 //        the middle of them both." Two faults, one bar: the chips' centres never agreed with each other
 //        or with the bar's, and focusing a chip's <select> SCROLLED the overflow:hidden screen (the
 //        select hung 3 px past its floor), which walked the whole bar up the picture. ──
 const q25=await p.evaluate(async()=>{ const c=window.__flowCardOf('chop',1); c.open(); await new Promise(r=>setTimeout(r,500));
   const card=document.querySelector('.ti-card.shp-ext.open'); card.style.left='6px'; card.style.top='6px';   /* tp77 — PARK THE CARD ON SCREEN. At its default zoom the card measures ~560 x 800 device px (body zoom 1.46 x card zoom 1.2) and opens near x 880, so its right third hung off a 1200 px viewport: a drawn stroke simply stopped at the edge (measured 59 of 91 moves reaching the field, the rest landing on <html>) and every gesture bar was silently short. */
   await new Promise(r=>setTimeout(r,150));
   const scr=card.querySelector('.screen'), bar=scr.querySelector('.mv-ov.bot');
   const vis=[...bar.querySelectorAll('.mv-c')].filter(e=>getComputedStyle(e).display!=='none');
   const mid=e=>{ const q=e.getBoundingClientRect(); return q.top+q.height/2; };
   const barMid=mid(bar), before=vis.map(mid), beforeL=vis.map(e=>e.getBoundingClientRect().left);
   for(const e of vis){ const se=e.querySelector('select'); if(se) se.focus(); }
   await new Promise(r=>setTimeout(r,150));
   const after=vis.map(mid), afterL=vis.map(e=>e.getBoundingClientRect().left);
   return { n:vis.length, spread:+(Math.max(...before)-Math.min(...before)).toFixed(2),
            offBar:+Math.max(...before.map(v=>Math.abs(v-barMid))).toFixed(2),
            walk:+Math.max(...after.map((v,i)=>Math.abs(v-before[i])), ...afterL.map((v,i)=>Math.abs(v-beforeL[i]))).toFixed(2),
            scroll:+(scr.scrollTop+scr.scrollLeft).toFixed(3) }; });
 ok(q25.n>=2 && q25.spread<=0.5 && q25.offBar<=0.5 && q25.walk<=0.5 && q25.scroll===0,
    '[25] 🚨 every chip in the bar sits on ONE midline — the bar’s own — and focusing a chip’s dropdown neither scrolls the screen nor moves a chip by half a pixel', JSON.stringify(q25));

 // ── [26] THE MIDDLE CHIP IS CENTRED IN THE BAR, not merely between the other two. Max: "sign is not in
 //        the middle." space-between only centres it when both outer words weigh the same, and 'Point'
 //        against 'Ladder LP 24' never did — so the bar is a three-column grid with equal outer columns. ──
 const q26=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext.open');
   const tiles=[...card.querySelectorAll('.fx .fxb')]; tiles[2].click(); await new Promise(r=>setTimeout(r,350));   // the Filter lane: the long right-hand word
   const scr=card.querySelector('.screen'), bar=scr.querySelector('.mv-ov.bot');
   const cx=e=>{ const q=e.getBoundingClientRect(); return q.left+q.width/2; };
   const shape=bar.querySelector('.ch-shape'), type=bar.querySelector('.ch-type'), brush=bar.querySelector('.ch-brush');
   return { off:+Math.abs(cx(shape)-cx(bar)).toFixed(2), typeVis:getComputedStyle(type).display!=='none',
            words:[brush.querySelector('.t').textContent, shape.querySelector('.t').textContent, type.querySelector('.t').textContent] }; });
 ok(q26.typeVis && q26.off<=0.5 && q26.words[2].length>q26.words[0].length,
    '[26] the middle chip is centred in the bar to half a pixel even with a long word on the right', JSON.stringify(q26));

 // ── [27] tp102 — THE TYPE CHIP IS A DROPDOWN (Max: "the arrows on the clips n such are WAY TOO big, can we just have a
 //        dropdown menu"). No ‹ ›; a chevron like its neighbours, on the bar's line. On a roster lane (Drive) a click opens
 //        the house .pmenu glass (not a native list); a pick changes the chip and closes the menu. ──
 const q27=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext.open');
   const drv=[...card.querySelectorAll('.fxb')].find(b=>b.textContent.trim().toUpperCase()==='DRIVE'); if(drv) drv.click();
   await new Promise(r=>setTimeout(r,250));
   const type=card.querySelector('.ch-type'), t=type.querySelector('.t'), chev=type.querySelector('svg.chev');
   const arrows=type.querySelectorAll('.ar').length;
   const midOf=e=>{ const q=e.getBoundingClientRect(); return q.top+q.height/2; };
   const lined=chev?Math.abs(midOf(chev)-midOf(t)):99;
   const before=t.textContent; type.click(); await new Promise(r=>setTimeout(r,200));
   const m=[...document.querySelectorAll('body > .pmenu')].pop(); const rows=m?[...m.querySelectorAll('.pi')]:[];
   const pick=rows.find(r=>!r.classList.contains('cur')); if(pick) pick.click(); await new Promise(r=>setTimeout(r,200));
   const after=t.textContent, closed=!!m&&!m.isConnected;   /* THIS menu is gone (other cards keep their own .pmenu parked) */
   return { arrows, chev:!!chev, lined:+lined.toFixed(2), menu:!!m, rows:rows.length, before, after, changed:after!==before, closed }; });
 ok(!q27.err && q27.arrows===0 && q27.chev && q27.lined<=1 && q27.menu && q27.rows>1 && q27.changed && q27.closed,
    '[27] the type chip is a dropdown: no arrows, a chevron on its line, a click opens the house glass list, a pick changes the type and closes it', JSON.stringify(q27));

 // ── [28] A SINE IS A SINE. Max: "that's not a fucking sine wave, that's a triangle spike wave with a
 //        couple curvature." The segment law is an exponential bend, which cannot draw a cosine across
 //        three points — nine fitted points can. Measured against (1-cos 2πx)/2 at 64 probes. ──
 const q28=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext.open');
   const tiles=[...card.querySelectorAll('.fx .fxb')]; tiles[0].click(); await new Promise(r=>setTimeout(r,300));
   const ss=card.querySelector('.ch-shape select'); const o=[...ss.querySelectorAll('option')]; const k=o.findIndex(q=>/^Sine$/i.test(q.textContent));
   if(k<0) return {err:'no Sine row'};
   ss.value=o[k].value; ss.dispatchEvent(new Event('change',{bubbles:true})); await new Promise(r=>setTimeout(r,300));
   const d=card.querySelector('.screen .mv-stroke').getAttribute('d');
   const nums=d.match(/-?[\d.]+/g).map(Number); const xs=[],ys=[];
   for(let i=0;i+1<nums.length;i+=2){ xs.push(nums[i]); ys.push(nums[i+1]); }
   const x0=Math.min(...xs), x1=Math.max(...xs), yTop=Math.min(...ys), yBot=Math.max(...ys);
   const at=x=>{ const u=x0+(x1-x0)*x; let i=0; while(i<xs.length-2&&xs[i+1]<=u)i++; const w=xs[i+1]-xs[i]||1e-9; return ys[i]+(ys[i+1]-ys[i])*((u-xs[i])/w); };
   let worst=0, H=yBot-yTop;
   for(let i=1;i<64;i++){ const x=i/64; const want=yBot-(1-Math.cos(2*Math.PI*x))/2*H; worst=Math.max(worst,Math.abs(at(x)-want)/H); }
   return { nodes:card.querySelectorAll('.sh-nd').length, worst:+worst.toFixed(4), label:card.querySelector('.ch-shape .t').textContent }; });
 ok(!q28.err && q28.label==='Sine' && q28.nodes===9 && q28.worst<=0.02,
    '[28] 🚨 the Sine shape IS a sine — nine fitted breakpoints hold the drawn line within 2 % of full height of (1-cos 2πx)/2 at 64 probes', JSON.stringify(q28));

 // ── [29] THE LFO'S GRID RULES, BOTH AXES. Max: "the shaper doesn't follow the same grid rules as the
 //        LFO — the LFO's grid rules are very smart … make our grid smart too." The LFO's shSnap lands a
 //        dragged point on the time cells AND the level lines; the Shaper only ever snapped x. ──
 const q29=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext.open');
   const ss=card.querySelector('.ch-shape select'); const o=[...ss.querySelectorAll('option')]; const k=o.findIndex(q=>/^Triangle$|^Tri$/i.test(q.textContent));
   if(k>=0){ ss.value=o[k].value; ss.dispatchEvent(new Event('change',{bubbles:true})); await new Promise(r=>setTimeout(r,250)); }
   const st=(window.__tiDice&&window.__tiDice.chop)?window.__tiDice.chop.S.v:null; return { snap: st?st.snap:null }; });
 const q29b=await (async()=>{
   const f=await p.evaluate(()=>{ const card=document.querySelector('.ti-card.shp-ext.open'); const q=card.querySelector('.screen .field').getBoundingClientRect();
     return {x:q.left,y:q.top,w:q.width,h:q.height}; });
   // grab the MIDDLE node and drop it at a level that is NOT on a line, then read where it landed
   const before=await p.evaluate(()=>{ const c=document.querySelector('.ti-card.shp-ext.open'); const n=[...c.querySelectorAll('.sh-nd')]; const k=Math.floor(n.length/2);
     const b=n[k].getBoundingClientRect(); return {k, x:b.left+b.width/2, y:b.top+b.height/2, n:n.length}; });
   await p.mouse.move(before.x,before.y); await p.mouse.down();
   await p.mouse.move(before.x+2,before.y+2); await p.mouse.move(f.x+f.w*0.5, f.y+f.h*0.37); await p.mouse.up();
   await sleep(300);
   return await p.evaluate(()=>{ const c=document.querySelector('.ti-card.shp-ext.open'); const st=(window.__tiDice&&window.__tiDice.chop)?window.__tiDice.chop.S.v:null; if(!st) return {err:'no state'};
     const pts=JSON.parse(st['pts0']); const L=[1,2,3,4,6,8][st.lgrid|0]||4, G=[2,3,4,6,8,12,16,24,32][st.grid0|0]||16;
     const off=pts.slice(1,-1).map(q=>Math.min(Math.abs(q[1]*L-Math.round(q[1]*L))/L, 1));
     const offX=pts.slice(1,-1).map(q=>Math.abs(q[0]*G-Math.round(q[0]*G))/G);
     return { snap:st.snap, L, G, worstY:+Math.max(0,...off).toFixed(4), worstX:+Math.max(0,...offX).toFixed(4) }; });
 })();
 ok(!q29b.err && q29b.snap===1 && q29b.worstY<=0.002 && q29b.worstX<=0.002,
    '[29] 🚨 with Snap on, a dragged node lands on a LEVEL line as well as a time cell — the LFO’s shSnap on both axes', JSON.stringify(q29b));

 // ── [30] NO CLIFF AT THE SEAM OF A FREE-DRAW STROKE. Max: "it starts off good, but then there'd be
 //        like a point in time in which it has this weird line … it starts to turn into like a triangle."
 //        A stroke that begins inside the field left the OLD shape's breakpoint standing an arbitrary hair
 //        outside the swept span, so the line fell off a wall to reach it (measured, before this: a stroke
 //        starting 4 px in over a gate opened `M0.0 10.0 L3.6 98.0` — full height across 0.8 % of the
 //        width). The sweep now eats FREE_EAT = 0.02 past each of its own ends, so the bridge from the old
 //        shape to the new stroke is a ramp at least that wide and can never be a wall again.
 //        The base is a GATE on purpose: its own edges are the steepest thing a lane can legitimately hold
 //        (dx = 1e-4), so the test ignores those and hunts the in-between — a segment too narrow to be a
 //        ramp and too wide to be one of the gate's own risers. Before the fix that is exactly the seam. ──
 const q30=await (async()=>{
   await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext.open');
     const tiles=[...card.querySelectorAll('.fx .fxb')]; tiles[0].click(); await new Promise(r=>setTimeout(r,250));
     const ss=card.querySelector('.ch-shape select'); const o=[...ss.querySelectorAll('option')]; const k=o.findIndex(q=>/Gate 1\/16/i.test(q.textContent));
     if(k>=0){ ss.value=o[k].value; ss.dispatchEvent(new Event('change',{bubbles:true})); await new Promise(r=>setTimeout(r,250)); }
     const bs=card.querySelector('.ch-brush select'); const bk=[...bs.options].findIndex(q=>/free/i.test(q.textContent));
     bs.value=String(bk); bs.dispatchEvent(new Event('change',{bubbles:true})); await new Promise(r=>setTimeout(r,250)); });
   const f=await p.evaluate(()=>{ const q=document.querySelector('.ti-card.shp-ext.open .screen .field').getBoundingClientRect(); return {x:q.left,y:q.top,w:q.width,h:q.height}; });
   /* the stroke starts a HAIR past the gate's 5/16 riser (0.3125) and stops a hair before its 11/16 one
      (0.6875), so on each side the nearest surviving breakpoint is under a thousandth of the width away.
      That is the geometry the cliff needed: without the eaten margin the line has to climb the gate's full
      height across ~0.0008 of the width. A stroke that starts in open space cannot show the fault. */
   const X0=0.3133, X1=0.6867, N=60, path=[];
   for(let i=0;i<=N;i++){ const t=i/N; path.push([f.x+f.w*(X0+(X1-X0)*t), f.y+f.h*(0.45-0.18*Math.sin(t*Math.PI))]); }
   await p.mouse.move(path[0][0],path[0][1]); await p.mouse.down();
   for(const q of path) await p.mouse.move(q[0],q[1]);
   await p.mouse.up(); await sleep(350);
   return await p.evaluate(()=>{ const st=(window.__tiDice&&window.__tiDice.chop)?window.__tiDice.chop.S.v:null; if(!st) return {err:'no state'};
     const pts=JSON.parse(st['pts0']); let wall=0, at=-1, riser=0;
     for(let i=0;i+1<pts.length;i++){ const dx=pts[i+1][0]-pts[i][0], dy=Math.abs(pts[i+1][1]-pts[i][1]);
       if(dx<=3e-4){ riser=Math.max(riser,dy); continue; }        // the gate's own instant edge — legitimate
       if(dx<0.018 && dy>wall){ wall=dy; at=+pts[i][0].toFixed(4); } }   // too narrow to be the 0.02 bridge
     const drew=pts.some(q=>q[0]>0.34&&q[0]<0.66&&Math.abs(q[1]-0.5)<0.35);
     return { n:pts.length, wall:+wall.toFixed(3), at, riser:+riser.toFixed(3), drew }; });
 })();
 ok(!q30.err && q30.n>20 && q30.drew && q30.riser>0.9 && q30.wall<=0.25,
    '[30] 🚨 a stroke that starts and ends mid-shape leaves NO wall at either seam: the gate keeps its own instant risers, and nothing between 3e-4 and the 0.02 bridge climbs more than a quarter of full height', JSON.stringify(q30));

 // ── [31] A SECOND RANDOM IS A SECOND SHAPE. Max: "whenever I select random curve or random wave … it's
 //        not random, it's the same." A native <select> fires no change when you pick the row already
 //        selected, so the random rows now leave it with nothing selected. ──
 const q31=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext.open');
   const bs=card.querySelector('.ch-brush select'); bs.value='0'; bs.dispatchEvent(new Event('change',{bubbles:true}));
   const ss=card.querySelector('.ch-shape select'); const o=[...ss.querySelectorAll('option')]; const k=o.findIndex(q=>/random curve/i.test(q.textContent));
   if(k<0) return {err:'no Random curve row'};
   const grab=()=>card.querySelector('.screen .mv-stroke').getAttribute('d');
   ss.value=o[k].value; ss.dispatchEvent(new Event('change',{bubbles:true})); await new Promise(r=>setTimeout(r,250));
   const a=grab(), idx=ss.selectedIndex, word=card.querySelector('.ch-shape .t').textContent;
   ss.value=o[k].value; ss.dispatchEvent(new Event('change',{bubbles:true})); await new Promise(r=>setTimeout(r,250));
   return { differ:a!==grab(), deselected:idx===-1, word }; });
 ok(!q31.err && q31.differ && q31.deselected && /random/i.test(q31.word),
    '[31] Random deals a NEW shape every time it is picked — the row leaves the select deselected while the chip still reads its name', JSON.stringify(q31));

 // ── [32] ONE CARD COLOUR, DOCKED AND POPPED. Max: "our card is a couple of shades lighter … I need all
 //        the cards to be the exact same colour." The card was translucent over a 22 px blur, so its
 //        colour was whatever sat behind it; the popped window has always been forced opaque. ──
 const q32=await p.evaluate(()=>{ const card=document.querySelector('.ti-card.shp-ext.open'); const cs=getComputedStyle(card);
   const popped=[...document.styleSheets].flatMap(sh=>{ try{ return [...sh.cssRules]; }catch(e){ return []; } })
     .filter(r=>r.selectorText&&/html\.card-only \.ti-popcard/.test(r.selectorText)).map(r=>r.style.backgroundImage).filter(Boolean)[0]||'';
   const norm=v=>v.replace(/\s+/g,'');
   const others=['.arp-ext','.gli-ext','.rbn-ext','.lfo-ext'].map(k=>{ const e=document.querySelector('.ti-card'+k); return e?norm(getComputedStyle(e).backgroundImage):null; });
   return { bg:norm(cs.backgroundImage), popped:norm(popped), blur:cs.backdropFilter||cs.webkitBackdropFilter,
            alpha:/rgba\([^)]*,\s*0?\.\d+\)/.test(cs.backgroundImage), sameAsOthers:others.filter(Boolean).every(v=>v===norm(cs.backgroundImage)) }; });
 ok(q32.bg===q32.popped && (q32.blur==='none'||!q32.blur) && !q32.alpha && q32.sameAsOthers,
    '[32] 🚨 the docked card wears the SAME opaque paint as the popped window — no translucency, no backdrop blur, and every card family reads the same value', JSON.stringify(q32));

 // ── [33] THE HEADER: no purple pip, no grip glyph, the title title-case and its first letter standing on
 //        the same left rule as the screen and the tiles below it. Max: "I don't want that purple button …
 //        move Shaper to the very top left … the beginning of S lines up with all this stuff." ──
 const q33=await p.evaluate(async()=>{
   /* the other families are built lazily — open each one so there is a header to read, then close it */
   for(const k of ['arp','glitch','drift']){ try{ const c=window.__flowCardOf(k,1); if(c&&c.open){ c.open(); await new Promise(r=>setTimeout(r,180)); c.close(); } }catch(e){} }   /* the kinds __flowCardOf answers to: 'glitch' and 'drift', not the pid spellings gli / rbn */
   await new Promise(r=>setTimeout(r,150));
   const card=document.querySelector('.ti-card.shp-ext'), h=card.querySelector('.h');
   const tt=h.querySelector('.tt'), scr=card.querySelector('.screen'), fx=card.querySelector('.fx');
   card.classList.add('open');
   return { pip:!!h.querySelector('.dot'), grip:!!h.querySelector('.g'), txt:tt.textContent,
            caps:getComputedStyle(tt).textTransform, first:h.firstElementChild.className,
            dx:+Math.abs(tt.getBoundingClientRect().left-scr.getBoundingClientRect().left).toFixed(2),
            dfx:+Math.abs(tt.getBoundingClientRect().left-fx.getBoundingClientRect().left).toFixed(2),
            /* tp77 — Max named three: "capital glitch … capital ARP … capital Robin". Every family is built
               from TIC.shell with a title-case name, so this reads what each one actually PAINTS. */
            family:['.arp-ext','.gli-ext','.rbn-ext','.shp-ext'].map(k=>{ const e=document.querySelector('.ti-card'+k); if(!e) return null;
              const t2=e.querySelector('.h .tt'); return t2?[t2.textContent, getComputedStyle(t2).textTransform, !!e.querySelector('.h .dot'), !!e.querySelector('.h .g')].join(','):null; }).filter(Boolean) }; });
 ok(!q33.pip && !q33.grip && q33.txt==='Shaper' && q33.caps==='none' && q33.first==='tt' && q33.dx<=1 && q33.dfx<=1
   && q33.family.length>=3 && q33.family.every(v=>/^[A-Z][a-z]/.test(v) && v.indexOf(',none,false,false')>0),
    '[33] the header is the title first — no purple pip, no grip — title-case, its first letter on the same left rule as the screen and the tile row; Arp / Glitch / Robin read the same way', JSON.stringify(q33));

 // ── [34] FILLED MEANS FILLED. Max: "forget that pinkish purple outline whenever it's filled … when it's
 //        NOT filled, then yeah, we got that purple outline, white inside." ──
 const q34=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext.open');
   const tiles=[...card.querySelectorAll('.fx .fxb')];
   const read=e=>{ const c=getComputedStyle(e); return {bg:c.backgroundColor, bd:c.borderColor, sh:c.boxShadow, ink:c.color}; };
   const lit=tiles.find(e=>e.classList.contains('on')&&e.classList.contains('cur'));
   const litOnly=tiles.find(e=>e.classList.contains('on')&&!e.classList.contains('cur'))||null;
   tiles[3].click(); await new Promise(r=>setTimeout(r,300));   // Pan: selected, NOT lit
   const selOnly=read(tiles[3]);
   /* tp77 — Max: "the shaper has these purple dots on the outline … I just want it to be pure white."
      The ring belongs to the SELECTED node alone, so read an ordinary one and a .sel one apart. */
   const nd=card.querySelector('.screen .sh-nd'); const ndc=nd?getComputedStyle(nd):null;
   const paneNd=(()=>{ const probe=document.createElement('div'); probe.id='mod-engine'; probe.style.cssText='position:absolute;left:-9999px';
     probe.innerHTML='<svg><circle class="sh-nd"/><circle class="sh-nd sel"/></svg>'; document.body.appendChild(probe);
     const a=getComputedStyle(probe.querySelector('.sh-nd')).stroke, b=getComputedStyle(probe.querySelector('.sh-nd.sel')).stroke;
     probe.remove(); return {plain:a, sel:b}; })();
   return { lit:lit?read(lit):null, selOnly, litHasRing:lit?(read(lit).sh!=='none'):null,
            ndStroke:ndc?ndc.stroke:null, ndFill:ndc?ndc.fill:null, paneNd }; });
 ok(q34.lit && /rgba\(255, 255, 255/.test(q34.lit.bg) && q34.lit.bd==='rgba(0, 0, 0, 0)' && q34.lit.sh==='none'
    && q34.selOnly.bd==='rgb(183, 148, 255)' && q34.selOnly.ink==='rgb(255, 255, 255)'
    && !/183, 148, 255/.test(q34.ndStroke||'') && !/183, 148, 255/.test(q34.paneNd.plain) && /255, 255, 255/.test(q34.paneNd.sel),
    '[34] a LIT tile is plain white — no border, no ring; an unlit SELECTED tile keeps the purple outline with white ink; and the breakpoint dots are pure white in BOTH renderers (the purple is the selected node\'s alone)', JSON.stringify(q34));

 // ══ tp79 ══════════════════════════════════════════════════════════════════════════════════════
 // ── [35] 🚨 A TILE IS A POSITION IN THE CHAIN. Max: "these are in a chain obviously, so these can
 //        actually be per-routable … we right click on the buttons … I right click on Time, boom …
 //        I want to put Bode second, I want to put Pan third." Eight kinds in eight positions, so a
 //        kind is never duplicated and every pick SWAPS two positions ("I just put it in second").
 //        The order has to reach the DSP, so this reads it back out of setShaperJson's own blob. ──
 const q35=await p.evaluate(async()=>{ const c=window.__flowCardOf('chop',1); c.open(); await new Promise(r=>setTimeout(r,600));
   const card=document.querySelector('.ti-card.shp-ext.open');
   const names=()=>[...card.querySelectorAll('.fx .fxb')].map(e=>e.textContent.trim());
   const kinds=()=>[...card.querySelectorAll('.fx .fxb')].map(e=>e.getAttribute('data-l'));
   const blob=()=>{ const j=window.__shpJson; return j&&j.length?JSON.parse(j[j.length-1][1]).slot:null; };
   const before=names(), kBefore=kinds(), slotBefore=blob();
   /* right-click position 1 (the second tile) and place the lane that currently sits at position 5 */
   const t=[...card.querySelectorAll('.fx .fxb')];
   t[1].dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:300,clientY:300}));
   await new Promise(r=>setTimeout(r,350));
   const menu=document.querySelector('#syn-ctx-menu.act, body > .syn-ctx-menu.act, .syn-ctx-menu.act');
   if(!menu) return {err:'no menu on a tile right-click'};
   const rows=[...menu.querySelectorAll('.syn-ctx-item')].filter(e=>e.textContent.trim().length);
   const labels=rows.map(e=>e.textContent.trim());
   const want=before[5];   /* the SHORT name on the tile; the menu rows carry the long one */
   const target=rows.find(e=>{ const x=e.textContent.trim().toLowerCase(); return x.startsWith(want.toLowerCase()); });
   if(!target) return {err:'no row for '+want, labels};
   target.click(); await new Promise(r=>setTimeout(r,450));
   const after=names(), kAfter=kinds(), slotAfter=blob();
   return { before, after, kBefore, kAfter, slotBefore, slotAfter, labels,
            swapped: after[1]===before[5] && after[5]===before[1],
            othersHeld: before.every((n,i)=>(i===1||i===5)?true:after[i]===n) }; });
 ok(!q35.err && q35.swapped && q35.othersHeld && Array.isArray(q35.slotAfter) && q35.slotAfter.length===8
    && q35.slotAfter.join(',')===q35.kAfter.join(',') && q35.slotBefore.join(',')==='0,1,2,3,4,5,6,7'
    && new Set(q35.slotAfter).size===8,
    '[35] 🚨 right-clicking a tile offers the eight lanes; picking one SWAPS the two positions, every other position holds, and the new chain reaches the processor in setShaperJson\'s slot list (still a permutation)', JSON.stringify(q35).slice(0,600));

 // ── [36] tp83 — THE CHAIN IS EIGHT PARAMETERS NOW (FLOW_CHOP_SLOT1..8), one per position, mirrored into
 //        the card's state like every other parameter. It used to be a comma string in the blob, which meant the
 //        host could not automate the order and no offline harness could PLACE a kind — which is exactly what
 //        left the borrowed effects uncertifiable. A list that is not eight distinct kinds still reads as the
 //        tile order rather than indexing anything. ──
 const q36=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext.open');
   const S=window.__tiDice.chop.S;
   const kinds=()=>[...card.querySelectorAll('.fx .fxb')].map(e=>+e.getAttribute('data-l'));
   const snap=JSON.stringify(S.v);                       // what a preset stores
   const live=kinds().join(',');
   const carried=[0,1,2,3,4,5,6,7].every(q=>('slotk'+q) in JSON.parse(snap));
   const param=!!(window.__params && ('FLOW_CHOP_SLOT1' in window.__params));
   S.set('slotk1',S.v.slotk0); await new Promise(r=>setTimeout(r,250)); const dup=kinds().join(',');   // the same kind twice
   /* ⚠️ a position can no longer HOLD an out-of-range kind: the parameter clamps it to the roster's last one
      before the chain ever sees it. So what this proves is that the clamp leaves the chain valid — eight
      distinct kinds, all in range — not that the fallback fires, because nothing can make it fire from here. */
   S.set('slotk1',99);         await new Promise(r=>setTimeout(r,250)); const over=kinds();
   const o=JSON.parse(snap); for(const k in o) S.set(k,o[k]); await new Promise(r=>setTimeout(r,300));
   return { live, restored:kinds().join(','), dup, over, carried, param }; });
 const okOver = Array.isArray(q36.over) && q36.over.length===8 && new Set(q36.over).size===8 && q36.over.every(v=>v>=0&&v<18);
 ok(q36.carried && q36.restored===q36.live && q36.dup==='0,1,2,3,4,5,6,7' && okOver,
    '[36] the chain is eight parameters the card mirrors (so presets and the host both carry it); a duplicated position reads as the tile order, and an out-of-range one is clamped to a real kind before the chain sees it', JSON.stringify(q36));

 // ── [37] tp79 — the boot Max asked for: nothing lit, and the shape is a sine, not that gate ──
 const q37=await p.evaluate(()=>{ const card=document.querySelector('.ti-card.shp-ext');
   const src=document.documentElement.outerHTML;
   return { initOn:/init\['on'\+i\]=0;/.test(src), defVol:/var DEFSHAPE=\{vol:'sine'/.test(src),
            ribbon:getComputedStyle(card.querySelector('.gbl')).color }; });
 ok(q37.initOn && q37.defVol && /255, 255, 255/.test(q37.ribbon) && !/0\.5[0-9]?\)/.test(q37.ribbon),
    '[37] a fresh card lights NO lane and opens on a sine, and the box ribbons are white on their purple chip', JSON.stringify(q37));

 // ── [38] tp84 — 🚨 THE NOISE LANE BROWSES THE LIBRARY, NOT A DROPDOWN. Max: "noise needs to have the
 //        browser of all 200+ sounds we have." Every noise-sample native already took a trailing instance
 //        (tp43, so Noise 2 could share Noise 1's library), so this lane reaches the SAME library by passing
 //        its own — instances 3.. are the Shapers. The thirteen algorithmic colours are the first category,
 //        and the factory categories and imports follow when the natives answer. ──
 const q38=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext.open');
   const tiles=[...card.querySelectorAll('.fx .fxb')];
   /* put Noise in a chain position first — it is not in the default eight */
   window.__tiDice.chop.S.set('slotk7',16); await new Promise(r=>setTimeout(r,300));
   const t=[...card.querySelectorAll('.fx .fxb')]; t[7].click(); await new Promise(r=>setTimeout(r,350));
   const chip=card.querySelector('.ch-type');
   const isBrowser=chip.classList.contains('browser');
   const selHidden=getComputedStyle(chip.querySelector('select')).display==='none';
   const word=chip.querySelector('.t').textContent;
   chip.dispatchEvent(new MouseEvent('click',{bubbles:true,cancelable:true,clientX:300,clientY:300}));
   await new Promise(r=>setTimeout(r,600));
   const pane=document.querySelector('.tpb, .tpb-wrap, .two-pane, [class*="tpb"]');
   const txt=pane?pane.textContent:'';
   return { isBrowser, selHidden, word, opened:!!pane,
            colours:/Colours/.test(txt), white:/White Noise/.test(txt), vinyl:/Dirty Vinyl/.test(txt),
            tiles:tiles.length }; });
 ok(q38.isBrowser && q38.selHidden && q38.opened && q38.colours && q38.white && q38.vinyl,
    '[38] 🚨 the Noise lane\'s type chip opens the two-pane LIBRARY browser (its native dropdown hidden, as the Filter\'s is), with the thirteen colours as its first category', JSON.stringify(q38));

 // ── [39] tp86 — THE STOP GUIDELINE IS DRAWN ON THE TIME LANE, AND IT MOVES WITH RANGE ──────────
 //  The shape is the time OFFSET now, so "which angle is stopped" is something you have to be able to SEE —
 //  the tutorial leans on that grey line for every gesture in it. rate = 1 + slope × Range, so the stopped
 //  gradient falls 1/Range across the cycle: at Range ×1 it crosses the whole box, at ×2 it stops half way.
 //  It must not appear on any other lane, where the vertical does not mean time at all.
 const q39=await p.evaluate(async()=>{
   const card=document.querySelector('.ti-card.shp-ext'); const S=window.__tiDice.chop.S;
   const w=(ms)=>new Promise(r=>setTimeout(r,ms));
   const guide=()=>{ const f=card.querySelector('.screen .field svg'); if(!f) return null;
     const l=[...f.querySelectorAll('line')].find(e=>e.getAttribute('stroke-dasharray')==='3 3');
     if(!l) return null;
     return { x1:+l.getAttribute('x1'), y1:+l.getAttribute('y1'), x2:+l.getAttribute('x2'), y2:+l.getAttribute('y2'),
              hatch:!!f.querySelector('path[fill^="url(#shtg"]') }; };
   S.set('lane',1); await w(300);
   S.set('k2_1',0.5); await w(300); const atOne=guide();      // Range ×1
   S.set('k2_1',0.75); await w(300); const atTwo=guide();     // Range ×2 — the stop gradient halves
   S.set('lane',0); await w(300); const onVol=guide();
   S.set('lane',1); S.set('k2_1',0.5); await w(200);
   return {atOne,atTwo,onVol};
 });
 const g1=q39.atOne, g2=q39.atTwo;
 ok(!!g1 && g1.hatch && g1.y2>g1.y1 && !!g2 && (g2.y2-g2.y1) < (g1.y2-g1.y1)*0.75 && !q39.onVol,
    '🚨 [39] the TIME lane draws the STOP guideline — the gradient that reads 0%, Cableguys\' grey line — and cross-hatches the buffer it cannot reach yet; the line SHALLOWS as Range doubles, and no other lane has one',
    JSON.stringify({rangeX1:g1,rangeX2:g2,volume:q39.onVol}));

 ok(errs.length===0,'[14] the page threw nothing', errs.slice(0,3).join(' | ')||'clean');
 await b.close(); console.log('\n  '+pass+' passed, '+fail+' failed\n'); process.exit(fail?1:0);
})();
