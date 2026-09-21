// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp73 — THE SHAPER IS THE GLITCH CARD WITH THE LFO IN ITS SCREEN — page half (tp72's bars, re-aimed at the Glitch chassis + the lent LFO field).
//
//    node Tests/_tp73_gate.js
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
const PAGE=path.join(require('os').tmpdir(),'tp73.html'); fs.writeFileSync(PAGE,src);
let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:1200,height:900});
 await p.evaluateOnNewDocument(stubSrc+'\nstub(); window.__shpJson=[]; ["VOL","TIME","FILT","PAN","REP","DRIVE","PHASE","CRUSH"].forEach(function(L,i){ window.__params["FLOW_CHOP_"+L+"_ON"]=i?0:1; window.__params["FLOW_CHOP_"+L+"_DEPTH"]=1; window.__params["FLOW_CHOP_"+L+"_RATE"]=4/7; window.__params["FLOW_CHOP_"+L+"_MODE"]=(L==="DRIVE"?5/22:0); window.__params["FLOW_CHOP_"+L+"_TRIG"]=0; }); (function(){ const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>{ if(n==="setShaperJson") return (i,j)=>{ window.__shpJson.push([i,j]); return Promise.resolve(0); }; if(n==="getShaperJson") return ()=>Promise.resolve("{}"); return orig.getNativeFunction(n); } }); Object.defineProperty(window,"Juce",{configurable:true,get(){return wrap;},set(){}}); })();');
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,200)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>{ document.documentElement.setAttribute('data-theme','dark'); window.setActivePanel('syn'); }); await sleep(600);



 // ── [0] the card: the Glitch chassis, the LFO's field, the bar's chips ──
 const r0=await p.evaluate(async()=>{ const c=window.__flowCardOf('chop',1); c.open(); await new Promise(r=>setTimeout(r,700));
   const card=document.querySelector('.ti-card.shp-ext'); if(!card) return {err:'no .ti-card.shp-ext'};
   const tile=document.querySelector('.flow-mode[data-mode="chop"]'); const scr=card.querySelector('.screen');
   return { open:card.classList.contains('open'), title:card.querySelector('.h .tt').textContent, mix:!!card.querySelector('.h .mix'), width:Math.round(parseFloat(getComputedStyle(card).width)), scrH:Math.round(parseFloat(getComputedStyle(scr).height)),
     field:!!scr.querySelector('.field svg .sh-grid')&&!!scr.querySelector('.field svg .mv-stroke')&&!!scr.querySelector('.field svg .mv-fill')&&!!scr.querySelector('.field svg .mv-play')&&!!scr.querySelector('.field svg .mv-foll'),
     lent:!!window.__lfoField&&typeof window.__lfoField.svg==='function'&&window.__lfoField.pathD===undefined?false:true, chips:[...scr.querySelectorAll('.mv-ov.bot .mv-c')].map(e=>e.querySelector('.t').textContent), chevs:scr.querySelectorAll('.mv-ov.bot .mv-c svg.chev').length, icons:scr.querySelectorAll('.mv-ov.bot .mv-c .ic svg').length,
     tiles:card.querySelectorAll('.fx .fxb').length, cur:[...card.querySelectorAll('.fx .fxb')].map(b=>b.classList.contains('cur')).join(''), leds:card.querySelectorAll('.fleds i').length, tabs:[...card.querySelectorAll('.tab')].map(t=>t.textContent), chain:!!card.querySelector('.slotrow'),
     rows:[...card.querySelectorAll('.pane .gboxrow')].length, boxes:[...card.querySelectorAll('.pane.on .gbox .gbl')].map(e=>e.textContent),
     tilePaths:tile?tile.querySelectorAll('svg path').length:-1, tileCircles:tile?tile.querySelectorAll('svg circle').length:-1, tileTitle:tile?tile.getAttribute('title'):null, tileAnim:tile?!!tile.querySelector('animateTransform, animate'):false, tileSine:tile?!!tile.querySelector('path.shpSine'):false, tileClip:tile?!!tile.querySelector('clipPath'):false,
     caps:[...scr.querySelectorAll('div:not(.mv-ov):not(.mv-c):not(.field)')].map(e=>e.textContent.trim()).filter(t=>t&&!/Point|Gate|Ladder/.test(t)) }; });
 ok(!r0.err && r0.open && r0.title==='Shaper' && r0.mix && r0.width===316 && r0.scrH===96 && r0.field && r0.lent && r0.chips.join('|')==='Point|Gate|' && r0.chevs===3 && r0.icons===2 && r0.tiles===8 && r0.cur==='truefalsefalsefalsefalsefalsefalsefalse' && r0.leds===8 && r0.tabs.join('|')==='Shape|Target|Clock' && r0.chain && r0.rows===3 && r0.boxes.join('|')==='Volume|Cycle' && r0.caps.length===0,
    '🚨 [0] the Glitch chassis (316 wide, the 96 px screen, eight tiles with dots, Shape / Target / Clock, two boxes a row, the chain) with the LFO\'s field in the screen (sh-grid, mv-fill, mv-stroke, mv-play, mv-foll) and its bar: the brush chip, the shape chip (Gate, the LFO\'s glyph), the type chip hidden on Volume; no other caption', JSON.stringify(r0));
 ok(r0.tilePaths===1 && r0.tileCircles===0 && r0.tileTitle==='Shaper' && !r0.tileAnim && r0.tileSine && !r0.tileClip, '[0b] the tile\'s emblem is ONE full sine path between fixed ends — no clip, no SMIL, nothing else on it', JSON.stringify({paths:r0.tilePaths,circles:r0.tileCircles,title:r0.tileTitle,anim:r0.tileAnim,sine:r0.tileSine,clip:r0.tileClip}));
 const nm=await p.evaluate(()=>{ try{ return (function(){ const s=document.documentElement.outerHTML; return /FLOWNAME=\{arp:'Arp',drift:'Robin',chop:'Shaper'/.test(s); })(); }catch(e){ return false; } });
 ok(nm, '[0c] FLOWNAME.chop is Shaper (the Patcher, the browser and the pills say so)');

 // ── [1] drawn by the LFO's own renderer ──
 const r1=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const svg=()=>card.querySelector('.screen .field svg'); const eff=()=>svg().querySelector('.mv-stroke').getAttribute('d');
   const d0=eff(); const ptsVol=svg().querySelectorAll('.sh-nd').length, cdVol=svg().querySelectorAll('.sh-cd').length, hits=svg().querySelectorAll('.sh-hit').length;
   // byte-faithful: the field's stroke is the LFO's shPathD of the same points at the same geometry
   const S=window.__tiDice.chop.S; const pts=JSON.parse(S.v.pts0); const LF=window.__lfoField; const same=LF.pathD(pts,456,{H:152,T:13,B:111})===d0;
   const vb=svg().getAttribute('viewBox');
   card.querySelectorAll('.fx .fxb')[1].click(); await new Promise(r=>setTimeout(r,200));
   const d1=eff(); const ptsTime=svg().querySelectorAll('.sh-nd').length;
   const ys=(d1.match(/[ML](-?[\d.]+) (-?[\d.]+)/g)||[]).map(t=>+t.split(' ')[1]); const mono=ys.every((y,i)=>i===0||y<=ys[i-1]+0.01);
   const grid={v:svg().querySelectorAll('.sh-grid line').length, maj:svg().querySelectorAll('.sh-grid line.maj').length};
   const cs=getComputedStyle(svg().querySelector('.sh-grid line')); const gridInk={stroke:cs.stroke,w:cs.strokeWidth};
   card.querySelectorAll('.fx .fxb')[0].click(); await new Promise(r=>setTimeout(r,200));
   return {ptsVol,cdVol,hits,same,vb,ptsTime,mono,changed:d0!==d1,grid,gridInk}; });
 ok(r1.ptsVol===32 && r1.cdVol===16 && r1.hits===48 && r1.same && r1.vb==='0 0 456 152' && r1.ptsTime===2 && r1.mono && r1.grid.v===20 && r1.grid.maj===2 && /^rgba\(255, 255, 255, 0\.06[0-9]?\)$/.test(r1.gridInk.stroke) && r1.gridInk.w==='0.6px',
    '🚨 [1] the stroke IS the LFO\'s shPathD of the lane\'s points (byte-equal), the LFO\'s 456-unit field, its nodes / curve dots / hit targets, its grid ink (white .065, .6 wide); Volume boots as a 1/16 gate (32 nodes), Time as a unity ramp (2 nodes, monotone)', JSON.stringify(r1));

 // ── [2] the bar: the type chip = the rosters through the rack\'s browser; the shape chip = the LFO\'s dropdown ──
 const r2=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const T=card.querySelectorAll('.fx .fxb'); const w=(ms)=>new Promise(r=>setTimeout(r,ms));
   const chip=(n)=>card.querySelector('.screen .mv-c.ch-'+n+' .t').textContent, vis=(n)=>getComputedStyle(card.querySelector('.screen .mv-c.ch-'+n)).display!=='none';
   const out={}; const S=window.__tiDice.chop.S;
   let browsed=null; const orig=window.openTwoPaneBrowser; window.openTwoPaneBrowser=function(ev,cfg){ browsed=cfg; };
   T[2].click(); await w(150); out.filt=chip('type'); out.filtVis=vis('type'); card.querySelector('.screen .mv-c.ch-type').click(); await w(100); out.filtCats=browsed?browsed.cats.map(c=>c.label):null; out.filtN=browsed?browsed.cats.reduce((a,c)=>a+c.items.length,0):0;
   const acid=browsed&&browsed.cats.flatMap(c=>c.items).find(it=>it.name==='Acid 303'); if(acid) acid.pick(); await w(300); out.filtAfter=chip('type'); out.filtParam=window.__params['FLOW_CHOP_FILT_MODE'];
   T[5].click(); await w(150); out.drive=chip('type'); browsed=null; card.querySelector('.screen .mv-c.ch-type').click(); await w(100); out.driveCats=browsed?browsed.cats.map(c=>c.label):null;
   const hard=browsed&&browsed.cats.flatMap(c=>c.items).find(it=>it.name==='Hard Clip'); if(hard) hard.pick(); await w(300); out.driveAfter=chip('type'); out.driveParam=window.__params['FLOW_CHOP_DRIVE_MODE'];
   T[6].click(); await w(150); out.ph=chip('type'); browsed=null; card.querySelector('.screen .mv-c.ch-type').click(); await w(100); out.phCats=browsed?browsed.cats.map(c=>c.label):null;
   T[7].click(); await w(150); out.cr=chip('type'); browsed=null; card.querySelector('.screen .mv-c.ch-type').click(); await w(100); out.crCats=browsed?browsed.cats.map(c=>c.label):null;
   window.openTwoPaneBrowser=orig;
   T[1].click(); await w(150); out.timeShape=chip('shape'); out.timeTypeVis=vis('type');
   card.querySelector('.screen .mv-c.ch-shape').click(); await w(150); const m=document.querySelector('.mv-menu.open'); out.menuRows=m?[...m.querySelectorAll('div[data-i] span')].map(e=>e.textContent):null;
   const half=m&&[...m.querySelectorAll('div[data-i]')].find(d=>/Half/.test(d.textContent)); const dA=card.querySelector('.screen .field svg .mv-stroke').getAttribute('d'); if(half) half.click(); await w(250); out.timeAfter=chip('shape'); out.restamped=card.querySelector('.screen .field svg .mv-stroke').getAttribute('d')!==dA;
   T[0].click(); await w(150); out.volShape=chip('shape'); out.volTypeVis=vis('type');
   S.set('mode2',0); S.set('mode5',5); return out; });
 ok(r2.filt==='Ladder LP 24' && r2.filtVis && r2.filtCats && r2.filtCats[0]==='Ladder' && r2.filtN===118 && r2.filtAfter==='Acid 303' && Math.abs((r2.filtParam||0)-4/117)<0.002
    && r2.drive==='Soft Clip' && r2.driveCats && r2.driveCats.join()==='Analog,Clip,Diode,Fold,Shaper,Digital' && r2.driveAfter==='Hard Clip' && Math.abs((r2.driveParam||0)-6/22)<0.01
    && r2.ph==='Phaser' && r2.phCats && r2.phCats.join()==='Built in,Phasers,Flangers & combs' && r2.cr==='Bits + Rate' && r2.crCats && r2.crCats.join()==='Built in,The rack’s crushers,Digital'
    && r2.timeShape==='Unity' && !r2.timeTypeVis && r2.menuRows && r2.menuRows.join('|')==='Unity|Half|Stutter|Stutter ⅛|Reverse|Tape stop|Custom' && r2.timeAfter==='Half' && r2.restamped && r2.volShape==='Gate' && !r2.volTypeVis,
    '🚨 [2] the type chip opens the RACK\'S two-pane browser (the filter\'s 118 in the filter\'s categories, the distortion\'s six families, the phaser / crusher groups) and picking writes the MODE parameter; the shape chip opens the LFO\'s own dropdown (Time: the time presets) and restamps; Volume / Time carry no type chip', JSON.stringify(r2));

 // ── [3] brushes: the brush chip\'s dropdown; the LFO field takes the strokes ──
 const r3=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const field=card.querySelector('.screen .field'); const r=field.getBoundingClientRect(); const w=(ms)=>new Promise(r=>setTimeout(r,ms));
   const pick=async(name)=>{ card.querySelector('.screen .mv-c.ch-brush').click(); await w(120); const m=document.querySelector('.mv-menu.open'); const d=m&&[...m.querySelectorAll('div[data-i]')].find(x=>x.textContent.trim()===name); if(d) d.click(); await w(80); return !!d; };
   const sx=(x)=>r.left+r.width*x, sy=(y)=>r.top+r.height*(111-y*98)/152;
   const okLine=await pick('Line'); const chipT=card.querySelector('.screen .mv-c.ch-brush .t').textContent;
   const px=sx(0.30), py=sy(0.25);
   field.dispatchEvent(new PointerEvent('pointerdown',{clientX:px,clientY:py,bubbles:true,pointerId:1,button:0})); await w(30);
   field.dispatchEvent(new PointerEvent('pointerup',{clientX:px,clientY:py,bubbles:true,pointerId:1,button:0})); await w(200);
   const pts=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[0].pts;
   const inCell=pts.filter(q=>q[0]>=0.25-1e-6&&q[0]<=0.3125+1e-6); const flat25=inCell.length>=2&&inCell.every(q=>Math.abs(q[1]-0.25)<0.03);
   await pick('Point'); const n0=pts.length; const px2=sx(0.61), py2=sy(0.4);
   field.dispatchEvent(new PointerEvent('pointerdown',{clientX:px2,clientY:py2,bubbles:true,pointerId:2,button:0})); await w(30);
   field.dispatchEvent(new PointerEvent('pointerup',{clientX:px2,clientY:py2,bubbles:true,pointerId:2,button:0})); await w(250);
   const pts2=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[0].pts; const added=pts2.find(q=>Math.abs(q[0]-0.625)<1e-6);
   await pick('Free draw');
   field.dispatchEvent(new PointerEvent('pointerdown',{clientX:sx(0.05),clientY:sy(0.2),bubbles:true,pointerId:3,button:0}));
   for(let k=1;k<=40;k++){ const x=0.05+0.9*k/40; field.dispatchEvent(new PointerEvent('pointermove',{clientX:sx(x),clientY:sy(0.2+0.6*Math.abs(Math.sin(x*6))),bubbles:true,pointerId:3,button:0})); await w(4); }
   field.dispatchEvent(new PointerEvent('pointerup',{clientX:sx(0.95),clientY:sy(0.3),bubbles:true,pointerId:3,button:0})); await w(250);
   const pts3=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[0].pts; const shapeChip=card.querySelector('.screen .mv-c.ch-shape .t').textContent;
   await pick('Point');
   return {okLine,chipT,flat25,inCell:inCell.length,n0,n1:pts2.length,snapped:!!added,free:pts3.length,shapeChip,pushes:window.__shpJson.length}; });
 ok(r3.okLine && r3.chipT==='Line' && r3.flat25 && r3.n1===r3.n0+1 && r3.snapped && r3.free>=6 && r3.free<=60 && r3.shapeChip==='Custom',
    '[3] the brush chip\'s dropdown: Line stamps a flat at the pointer\'s height into its grid cell; Point adds a point and Snap lands it on the grid (0.625 = 10/16); Free draw keeps its breakpoints; the shape chip then reads Custom', JSON.stringify(r3));
 // ── [4] the push ──
 const r4=await p.evaluate(()=>{ const last=window.__shpJson[window.__shpJson.length-1]; const o=JSON.parse(last[1]); return {inst:last[0],lanes:o.lanes.length,sense:o.sense,keys:Object.keys(o.lanes[2]).sort().join(','),k:o.lanes[2].k.length,kf:o.lanes[2].k.join(','),pts:o.lanes.every(L=>L.pts.length>=2&&L.pts[0][0]===0&&L.pts[L.pts.length-1][0]===1)}; });
 ok(r4.inst===0 && r4.lanes===8 && r4.sense===0.5 && r4.keys==='blend,floor,grid,k,phase,pts,smooth,swing,tension' && r4.k===4 && r4.kf==='0.3,0,1,0' && r4.pts,
    '🚨 [4] every edit pushes setShaperJson(inst, {sense, lanes:[8 × {pts (pinned 0..1), smooth, phase, tension, floor, blend, swing, grid, k[4]}]}); the Filter lane\'s k boots Reso .3 / Drive 0 / Poles 24 dB / Tube', JSON.stringify(r4));

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
   const m=document.getElementById('syn-ctx-menu')||document.querySelector('.syn-menu, .pmenu.act, [class*="menu"].act'); const txt=m?m.textContent:''; const vis=m?getComputedStyle(m).display!=='none':false;
   window.__synShowMenu=orig;
   return {vis, shows, hasGrid:/Grid/.test(txt), level:/Level/.test(txt), snap:/Snap/.test(txt), flips:/Flip vertical/.test(txt)&&/Flip horizontal/.test(txt), random:/Random flat/.test(txt)&&/Random both/.test(txt), tools:/Point · edit/.test(txt)&&/Free draw/.test(txt)&&/Sine/.test(txt), wt:/Wavetable → Shape/.test(txt)&&/Oscillator D/.test(txt), stock:/Stairs/.test(txt)&&/Gate/.test(txt), clear:/Clear/.test(txt), noExtend:!/Extend/.test(txt)}; });
 ok(r6.vis && r6.shows===1 && r6.hasGrid && r6.level && r6.snap && r6.flips && r6.random && r6.tools && r6.wt && r6.stock && r6.clear && r6.noExtend,
    '🚨 [6] the right pointer-DOWN opens the LFO\'s menu ONCE (the gesture\'s contextmenu is swallowed): Grid + Level sliders, Snap, the flips, Random ×3, the eight tools, Wavetable → Shape (Osc A–D), the stock shapes, Clear — and no Extend', JSON.stringify(r6));


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
 ok(L9[0].title==='Volume' && L9[0].on && L9[0].knobs.join()==='Attack,Release' && L9[0].steps.join()==='Mode' && L9[0].stepVals.join()==='Gain'
    && L9[1].title==='Time' && L9[1].knobs.join()==='Fade,Glide' && L9[1].steps.join()==='Range'
    && L9[2].title==='Filter' && L9[2].knobs.join()==='Reso,Drive' && L9[2].steps.join()==='Poles,Char' && L9[2].stepVals.join()==='24 dB,Tube'
    && L9[3].title==='Pan' && L9[3].knobs.join()==='Width,Bass' && L9[3].steps.join()==='Law'
    && L9[4].title==='Repeat' && L9[4].knobs.join()==='Seam,Decay,Pitch' && L9[4].steps.join()==='Mode'
    && L9[5].title==='Drive' && L9[5].knobs.join()==='Tone,Makeup,Bias' && L9[5].steps.join()==='Char'
    && L9[6].title==='Phaser' && L9[6].knobs.join()==='Feedbk,Stereo,Drive' && !L9[6].rightShown
    && L9[7].title==='Crush' && L9[7].knobs.join()==='Bits,Rate,Tone' && !L9[7].rightShown
    && Math.abs(r9.k0-0.8)<1e-6 && Math.abs(r9.k2-1/3)<1e-6,
    '🚨 [9] the Target tab is the lane\'s back panel in the Glitch\'s boxes: the knobs on the left with On, the steps (dropdowns) on the right, per lane (Volume Attack/Release + Mode · Time Fade/Glide + Range · Filter Reso/Drive + Poles/Char · Pan Width/Bass + Law · Repeat Seam/Decay/Pitch + Mode · Drive Tone/Makeup/Bias + Char · Phaser / Crush knobs only); a knob or a step writes the lane\'s k', JSON.stringify({f:L9[2],k0:r9.k0,k2:r9.k2}));

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

 // ── [13] tp71b — the tile's sine rides the motion clock: one period per beat of the host BPM, winds up on MIDI, winds down
 //        onto its home (phase 0 — the same picture every time), and with Motion off it never moves ──
 const r13=await p.evaluate(async()=>{ try{ const c=window.__flowCardOf('chop',1); if(c&&c.close) c.close(); }catch(e){}
   const el=document.querySelector('#syn-panel .flow-mode[data-mode="chop"] path.shpSine'); if(!el) return {err:'no sine'};
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
    '[13] tp71b — the sine travels ~1 period per beat (120 vs 240 BPM: ~2x), winds up on MIDI, winds DOWN onto the same home picture, and with Motion off it stays on that picture', JSON.stringify(r13));

 ok(errs.length===0,'[14] the page threw nothing', errs.slice(0,3).join(' | ')||'clean');
 await b.close(); console.log('\n  '+pass+' passed, '+fail+' failed\n'); process.exit(fail?1:0);
})();
