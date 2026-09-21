// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp72 — THE TERRAIN SHAPER, TO THE BRIEF — page half. (tp71's gate, re-aimed at the rebuilt card, plus the new bars.)
//
//    node Tests/_tp72_gate.js
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
const PAGE=path.join(require('os').tmpdir(),'tp72.html'); fs.writeFileSync(PAGE,src);
let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:1200,height:900});
 await p.evaluateOnNewDocument(stubSrc+'\nstub(); window.__shpJson=[]; ["VOL","TIME","FILT","PAN","REP","DRIVE","PHASE","CRUSH"].forEach(function(L,i){ window.__params["FLOW_CHOP_"+L+"_ON"]=i?0:1; window.__params["FLOW_CHOP_"+L+"_DEPTH"]=1; window.__params["FLOW_CHOP_"+L+"_RATE"]=4/7; window.__params["FLOW_CHOP_"+L+"_MODE"]=(L==="DRIVE"?5/22:0); window.__params["FLOW_CHOP_"+L+"_TRIG"]=0; }); (function(){ const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>{ if(n==="setShaperJson") return (i,j)=>{ window.__shpJson.push([i,j]); return Promise.resolve(0); }; if(n==="getShaperJson") return ()=>Promise.resolve("{}"); return orig.getNativeFunction(n); } }); Object.defineProperty(window,"Juce",{configurable:true,get(){return wrap;},set(){}}); })();');
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,200)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>{ document.documentElement.setAttribute('data-theme','dark'); window.setActivePanel('syn'); }); await sleep(600);


 // ── [0] the card ──
 const r0=await p.evaluate(async()=>{ const c=window.__flowCardOf('chop',1); c.open(); await new Promise(r=>setTimeout(r,700));
   const card=document.querySelector('.ti-card.shp-ext'); if(!card) return {err:'no .ti-card.shp-ext'};
   const tile=document.querySelector('.flow-mode[data-mode="chop"]');
   return { open:card.classList.contains('open'), title:card.querySelector('.h .tt').textContent, mix:!!card.querySelector('.h .mix'), screen:!!card.querySelector('.screen svg'),
     width:Math.round(parseFloat(getComputedStyle(card).width)), tiles:card.querySelectorAll('.fx .fxb').length, leds:card.querySelectorAll('.fleds i').length, tabs:[...card.querySelectorAll('.tab')].map(t=>t.textContent), chain:!!card.querySelector('.foot, .chain, .slots, .slotrow'),
     tilePaths:tile?tile.querySelectorAll('svg path').length:-1, tileCircles:tile?tile.querySelectorAll('svg circle').length:-1, tileTitle:tile?tile.getAttribute('title'):null, tileAnim:tile?!!tile.querySelector('animateTransform, animate'):false, tileSine:tile?!!tile.querySelector('path.shpSine'):false, tileClip:tile?!!tile.querySelector('clipPath'):false,
     lnCap:card.querySelector('.screen .ln').textContent, caps:[...card.querySelectorAll('.screen div')].map(e=>e.textContent.trim()).filter(Boolean) }; });
 ok(!r0.err && r0.open && r0.title==='Shaper' && r0.mix && r0.screen && r0.tiles===8 && r0.leds===8 && r0.tabs.join('|')==='Shape|Target|Clock' && r0.width===462 && r0.lnCap==='Volume' && !r0.caps.some(t=>/LANE|RATE|GRID|DRAW|DOUBLE/i.test(t)),
    '[0] the Chop slot builds the SHAPER at the mockup\'s width (462): title, Mix header, the screen, eight targets with dots, Shape / Target / Clock; the screen carries the lane\'s name and no other caption', JSON.stringify(r0));
 ok(r0.tilePaths===1 && r0.tileCircles===0 && r0.tileTitle==='Shaper' && !r0.tileAnim && r0.tileSine && !r0.tileClip, '[0b] the tile\'s emblem is ONE full sine path between fixed ends — no clip, no SMIL, nothing else on it', JSON.stringify({paths:r0.tilePaths,circles:r0.tileCircles,title:r0.tileTitle,anim:r0.tileAnim,sine:r0.tileSine,clip:r0.tileClip}));
 const nm=await p.evaluate(()=>{ try{ return (function(){ const s=document.documentElement.outerHTML; return /FLOWNAME=\{arp:'Arp',drift:'Robin',chop:'Shaper'/.test(s); })(); }catch(e){ return false; } });
 ok(nm, '[0c] FLOWNAME.chop is Shaper (the Patcher, the browser and the pills say so)');

 // ── [1] drawn from breakpoints, on the LFO's field ──
 const r1=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const eff=()=>card.querySelector('.screen path.eff').getAttribute('d');
   const d0=eff(); const ptsVol=card.querySelectorAll('.screen .sh-nd').length, cdVol=card.querySelectorAll('.screen .sh-cd').length;
   card.querySelectorAll('.fx .fxb')[1].click(); await new Promise(r=>setTimeout(r,200));
   const d1=eff(); const ptsTime=card.querySelectorAll('.screen .sh-nd').length; const ghosts=card.querySelectorAll('.screen .ghosts path').length;
   const ys=(d1.match(/[ML](-?[\d.]+) (-?[\d.]+)/g)||[]).map(t=>+t.split(' ')[1]); const mono=ys.every((y,i)=>i===0||y<=ys[i-1]+0.01);
   const grid={v:card.querySelectorAll('.screen .sh-grid line').length, maj:card.querySelectorAll('.screen .sh-grid line.maj').length, fill:!!card.querySelector('.screen .mv-fill').getAttribute('d'), foll:!!card.querySelector('.screen .mv-foll')&&!!card.querySelector('.screen .mv-play')};
   card.querySelectorAll('.fx .fxb')[0].click(); await new Promise(r=>setTimeout(r,200));
   return {ptsVol,cdVol,ptsTime,ghosts,d0len:d0.length,mono,changed:d0!==d1,grid}; });
 ok(r1.ptsVol===32 && r1.ptsTime===2 && r1.mono && r1.ghosts===1 && r1.grid.v>=15 && r1.grid.maj>=1 && r1.grid.fill && r1.grid.foll,
    '🚨 [1] Volume boots as a 1/16 gate (32 nodes), Time as a unity ramp (2 nodes, the line rises monotonically); the lit Volume lane ghosts behind Time; the field is the LFO\'s (sh-grid lines with a maj, mv-fill, mv-play + mv-foll)', JSON.stringify(r1));

 // ── [2] the selector strip: the rosters ──
 const r2=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const pn=card.querySelector('.screen .sel .pn'), pa=card.querySelectorAll('.screen .sel .pa'); const T=card.querySelectorAll('.fx .fxb');
   const out={}; const w=(ms)=>new Promise(r=>setTimeout(r,ms));
   T[2].click(); await w(150); out.filt0=pn.textContent; pa[1].click(); await w(150); out.filt1=pn.textContent; out.filtParam=window.__params['FLOW_CHOP_FILT_MODE']; pa[0].click(); await w(100);
   T[5].click(); await w(150); out.drive0=pn.textContent; pa[1].click(); await w(150); out.drive1=pn.textContent; out.driveParam=window.__params['FLOW_CHOP_DRIVE_MODE']; pa[0].click(); await w(100);
   T[6].click(); await w(150); out.ph0=pn.textContent; pa[1].click(); await w(100); pa[1].click(); await w(150); out.ph2=pn.textContent; out.phParam=window.__params['FLOW_CHOP_PHASE_MODE']; pa[0].click(); pa[0].click(); await w(100);
   T[7].click(); await w(150); out.cr0=pn.textContent; for(let k=0;k<6;k++) pa[1].click(); await w(150); out.cr6=pn.textContent; for(let k=0;k<6;k++) pa[0].click(); await w(100);
   T[1].click(); await w(150); out.time0=pn.textContent; const dA=card.querySelector('.screen path.eff').getAttribute('d'); pa[1].click(); await w(150); out.time1=pn.textContent; const dB=card.querySelector('.screen path.eff').getAttribute('d'); out.restamped=dA!==dB; pa[0].click(); await w(100);
   T[0].click(); await w(150); out.vol=pn.textContent;
   return out; });
 ok(r2.filt0==='Ladder LP 24' && r2.filt1==='Ladder LP 12' && Math.abs((r2.filtParam||0)-1/117)<0.002 && r2.drive0==='Soft Clip' && r2.drive1==='Hard Clip' && Math.abs((r2.driveParam||0)-6/22)<0.01
    && r2.ph0==='Phaser' && r2.ph2==='Phaser 4P' && Math.abs((r2.phParam||0)-2/27)<0.01 && r2.cr0==='Bits + Rate' && r2.cr6==='Radio' && /Unity/i.test(r2.time0) && r2.restamped && /Sine|Tri|Saw|Square|Gate|Stairs|Flat/i.test(r2.vol),
    '🚨 [2] the selector strip is the rosters: Filter walks Ladder LP 24 → Ladder LP 12 (FLOW_CHOP_FILT_MODE = 1/117), Drive Soft Clip → Hard Clip, Phaser → Phaser 4P (the rack\'s), Crush → Radio; Time walks its presets and restamps; Volume shows the stock shapes', JSON.stringify(r2));

 // ── [3] brushes: pills in the Brush box ──
 const r3=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const svg=card.querySelector('.screen svg'); const r=svg.getBoundingClientRect();
   const pills=card.querySelectorAll('.gbox.brush .pill'); const lineBtn=[...pills].find(t=>t.textContent==='Line'), pointBtn=[...pills].find(t=>t.textContent==='Point'), snap=[...pills].find(t=>t.textContent==='Snap');
   lineBtn.click(); await new Promise(r=>setTimeout(r,60)); const lineLit=lineBtn.classList.contains('on'), snapLit=snap.classList.contains('on');
   const sx=(x)=>r.left+r.width*(8+x*(434-16))/434, sy=(y)=>r.top+r.height*(10+(1-y)*(150-10-24))/150;
   const px=sx(0.30), py=sy(0.25);
   svg.dispatchEvent(new PointerEvent('pointerdown',{clientX:px,clientY:py,bubbles:true,pointerId:1,button:0})); await new Promise(r=>setTimeout(r,30));
   svg.dispatchEvent(new PointerEvent('pointerup',{clientX:px,clientY:py,bubbles:true,pointerId:1,button:0})); await new Promise(r=>setTimeout(r,200));
   const pts=JSON.parse(window.__shpJson.length?window.__shpJson[window.__shpJson.length-1][1]:'{"lanes":[]}').lanes[0].pts;
   const inCell=pts.filter(q=>q[0]>=0.25-1e-6&&q[0]<=0.3125+1e-6); const flat25=inCell.length>=2&&inCell.every(q=>Math.abs(q[1]-0.25)<0.03);
   pointBtn.click(); await new Promise(r=>setTimeout(r,60));
   const n0=pts.length; const px2=sx(0.61), py2=sy(0.4);
   svg.dispatchEvent(new PointerEvent('pointerdown',{clientX:px2,clientY:py2,bubbles:true,pointerId:2,button:0})); await new Promise(r=>setTimeout(r,30));
   svg.dispatchEvent(new PointerEvent('pointerup',{clientX:px2,clientY:py2,bubbles:true,pointerId:2,button:0})); await new Promise(r=>setTimeout(r,250));
   const pts2=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[0].pts; const added=pts2.find(q=>Math.abs(q[0]-0.625)<1e-6);
   // Free draws a line and keeps only its breakpoints
   const freeBtn=[...pills].find(t=>t.textContent==='Free'); freeBtn.click(); await new Promise(r=>setTimeout(r,60));
   svg.dispatchEvent(new PointerEvent('pointerdown',{clientX:sx(0.05),clientY:sy(0.2),bubbles:true,pointerId:3,button:0}));
   for(let k=1;k<=40;k++){ const x=0.05+0.9*k/40; svg.dispatchEvent(new PointerEvent('pointermove',{clientX:sx(x),clientY:sy(0.2+0.6*Math.abs(Math.sin(x*6))),bubbles:true,pointerId:3,button:0})); await new Promise(r=>setTimeout(r,4)); }
   svg.dispatchEvent(new PointerEvent('pointerup',{clientX:sx(0.95),clientY:sy(0.3),bubbles:true,pointerId:3,button:0})); await new Promise(r=>setTimeout(r,250));
   const pts3=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[0].pts;
   pointBtn.click();
   return {lineLit,snapLit,flat25,inCell:inCell.length,n0,n1:pts2.length,snapped:!!added,free:pts3.length,pushes:window.__shpJson.length}; });
 ok(r3.lineLit && r3.snapLit && r3.flat25 && r3.n1===r3.n0+1 && r3.snapped && r3.free>=6 && r3.free<=60,
    '[3] the Brush pills: Line stamps a flat at the pointer\'s height into its grid cell; Point adds a point and Snap lands it on the grid (0.625 = 10/16); Free draws a curve and keeps its breakpoints (6..60 of them)', JSON.stringify(r3));

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
 const r6=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const svg=card.querySelector('.screen svg'); const r=svg.getBoundingClientRect();
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
   const before=S.v.pts2; const pill=card.querySelector('.h .pset .pn'); pill.click(); await new Promise(r=>setTimeout(r,300));
   const items=[...document.querySelectorAll('.pmenu .pi .nm')].map(e=>e.textContent); const acid=[...document.querySelectorAll('.pmenu .pi')].find(e=>/Acid sweep/.test(e.textContent));
   if(acid) acid.click(); await new Promise(r=>setTimeout(r,500));
   return {items:items.slice(0,20), stamped:S.v.pts2!==before && S.v.on2===1 && S.v.lane===2 && S.v.on0===0, mode2:S.v.mode2, param:window.__params['FLOW_CHOP_FILT_MODE'], sel:card.querySelector('.screen .sel .pn').textContent}; });
 ok(r7.items.some(x=>/Gate 16ths/.test(x)) && r7.items.some(x=>/Acid sweep/.test(x)) && r7.items.some(x=>/Tape scrub/.test(x)) && r7.items.some(x=>/Radio stairs/.test(x)) && r7.stamped && r7.mode2===4 && Math.abs((r7.param||0)-4/117)<0.002 && r7.sel==='Acid 303',
    '[7] the house preset menu lists the factory shapes; Acid sweep recalls onto the Filter lane, lights it, and sets its type to Acid 303 (the parameter and the selector agree)', JSON.stringify(r7));

 // ── [8] 🚨 the lane's knobs MOVE THE LINE ──
 const r8=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const S=window.__tiDice.chop.S; const w=(ms)=>new Promise(r=>setTimeout(r,ms));
   card.querySelectorAll('.fx .fxb')[3].click(); await w(150);   // Pan: a sine (bar [7] left Filter on a saw, whose rest smoothing bleeds across the wrap — a real deviation)
   const eff=()=>card.querySelector('.screen path.eff').getAttribute('d'), raw=()=>card.querySelector('.screen path.raw'), nodes=()=>[...card.querySelectorAll('.screen .sh-nd')].map(n=>n.getAttribute('cx')+','+n.getAttribute('cy')).join('|');
   const ys=(d)=>(d.match(/[ML](-?[\d.]+) (-?[\d.]+)/g)||[]).map(t=>+t.split(' ')[1]);
   const d0=eff(), n0=nodes(), rawHidden0=getComputedStyle(raw()).display==='none';
   S.set('tension3',0.9); await w(60); const dT=eff(); const rawShown=getComputedStyle(raw()).display!=='none'&&!!raw().getAttribute('d');
   S.set('tension3',0.5); S.set('phase3',0.25); await w(60); const dP=eff();
   S.set('phase3',0); S.set('floor3',0.4); await w(60); const dF=eff(); const minF=Math.max(...ys(dF));   // svg y grows downward: a floor RAISES the lowest point = lowers the max y
   S.set('floor3',0); S.set('smooth3',0.9); await w(60); const dS=eff();
   S.set('smooth3',0.25); S.set('swing3',0.7); await w(60); const dW=eff();
   S.set('swing3',0); await w(60); const dBack=eff(), nBack=nodes(), rawHidden1=getComputedStyle(raw()).display==='none';
   card.querySelectorAll('.fx .fxb')[0].click();
   return {rawHidden0, tension:dT!==d0, rawShown, phase:dP!==d0, floor:dF!==d0, floorRaised:minF<Math.max(...ys(d0))-8, smooth:dS!==d0, swing:dW!==d0, back:dBack===d0, nodesStill:n0===nBack, rawHidden1}; });
 ok(r8.rawHidden0 && r8.tension && r8.rawShown && r8.phase && r8.floor && r8.floorRaised && r8.smooth && r8.swing && r8.back && r8.nodesStill && r8.rawHidden1,
    '🚨 [8] Tension, Phase, Floor, Smooth and Swing each move the drawn line (Floor lifts its bottom); the breakpoints stay where they were drawn with the raw line dashed behind; at rest the line is the breakpoints again', JSON.stringify(r8));

 // ── [9] the Target tab — every lane's back panel ──
 const r9=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const w=(ms)=>new Promise(r=>setTimeout(r,ms)); const T=card.querySelectorAll('.fx .fxb');
   [...card.querySelectorAll('.tab')].find(t=>t.textContent==='Target').click(); await w(100);
   const box=card.querySelector('.pane[data-p="target"] .gbox'); const out={};
   for(let i=0;i<8;i++){ T[i].click(); await w(120); out[i]={ title:box.querySelector('.gbl').textContent, on:!!box.querySelector('.tog'), knobs:[...box.querySelectorAll('.cell.k .lb')].map(e=>e.textContent), pills:[...box.querySelectorAll('.pills .pill')].map(e=>e.textContent) }; }
   // a Target knob writes the blob: Filter Reso → k0 of lane 2; the Poles pill → k2
   T[2].click(); await w(120); const S=window.__tiDice.chop.S; S.set('k0_2',0.8); await w(200); const k=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[2].k;
   const poles=[...box.querySelectorAll('.pills .pill')].find(e=>e.textContent==='12 dB'); poles.click(); await w(200); const k2=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[2].k;
   S.set('k0_2',0.3); S.set('k2_2',1); T[0].click(); [...card.querySelectorAll('.tab')].find(t=>t.textContent==='Shape').click();
   return {lanes:out, k0:k[0], k2:k2[2]}; });
 const L9=r9.lanes;
 ok(L9[0].title==='Volume' && L9[0].on && L9[0].knobs.join()==='Attack,Release' && L9[0].pills.join()==='Gain,Duck'
    && L9[1].title==='Time' && L9[1].knobs.join()==='Fade,Glide' && L9[1].pills.join()==='1 cycle,½ cycle,2 cycles'
    && L9[2].title==='Filter' && L9[2].knobs.join()==='Reso,Drive' && L9[2].pills.join()==='6 dB,12 dB,18 dB,24 dB,Tube,Diode,Fold,Hard,Crush,Fuzz'
    && L9[3].title==='Pan' && L9[3].knobs.join()==='Width,Bass mono' && L9[3].pills.join()==='Power,Linear,Width'
    && L9[4].title==='Repeat' && L9[4].knobs.join()==='Seam,Decay,Pitch' && L9[4].pills.join()==='Slice,Reverse'
    && L9[5].title==='Drive' && L9[5].knobs.join()==='Tone,Makeup,Character,Bias'
    && L9[6].title==='Phaser' && L9[6].knobs.join()==='Feedback,Stereo,Drive'
    && L9[7].title==='Crush' && L9[7].knobs.join()==='Bits,Rate,Tone'
    && Math.abs(r9.k0-0.8)<1e-6 && Math.abs(r9.k2-1/3)<1e-6,
    '🚨 [9] the Target tab is the lane\'s back panel, every lane filled (Volume Attack/Release + Gain/Duck · Time Fade/Glide + Range · Filter Reso/Drive + Poles + Character · Pan Width/Bass mono + Law · Repeat Seam/Decay/Pitch + Slice/Reverse · Drive Tone/Makeup/Character/Bias · Phaser Feedback/Stereo/Drive · Crush Bits/Rate/Tone); a knob or pill writes the lane\'s k into the blob', JSON.stringify({f:L9[2],k0:r9.k0,k2:r9.k2}));

 // ── [10] the Clock tab: Rate / Grid / Trigger pill rows in titled boxes; a lit pill = purple outline, white ink, no glow ──
 const r10=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const w=(ms)=>new Promise(r=>setTimeout(r,ms));
   [...card.querySelectorAll('.tab')].find(t=>t.textContent==='Clock').click(); await w(100);
   const pane=card.querySelector('.pane[data-p="clock"]'); const boxes=[...pane.querySelectorAll('.gbox')].map(b=>({t:b.querySelector('.gbl').textContent,n:b.querySelectorAll('.pill').length,knobs:[...b.querySelectorAll('.cell.k .lb')].map(e=>e.textContent)}));
   const rate=[...pane.querySelectorAll('.gbox.rate .pill')]; rate[1].click(); await w(400); const rateParam=window.__params['FLOW_CHOP_VOL_RATE']; const litEl=pane.querySelector('.gbox.rate .pill.on'); const lit=!!litEl&&litEl.textContent==='1/8', cs0=getComputedStyle(litEl||rate[1]), cs={borderColor:cs0.borderColor,color:cs0.color,boxShadow:cs0.boxShadow};   /* a SNAPSHOT: CSSStyleDeclaration is live and the pill is unlit again below */
   const trig=[...pane.querySelectorAll('.gbox.trig .pill')]; trig[3].click(); await w(400); const trigParam=window.__params['FLOW_CHOP_VOL_TRIG']; const trigLit=trig[3].classList.contains('on');
   const grid=[...pane.querySelectorAll('.gbox.gridb .pill')]; grid[4].click(); await w(300); const gridLines=card.querySelectorAll('.screen .sh-grid line').length; const gridJson=JSON.parse(window.__shpJson[window.__shpJson.length-1][1]).lanes[0].grid;
   rate[4].click(); trig[0].click(); grid[6].click(); await w(300); [...card.querySelectorAll('.tab')].find(t=>t.textContent==='Shape').click();
   return {boxes, rateParam, lit, border:cs.borderColor, color:cs.color, shadow:cs.boxShadow, trigParam, trigLit, gridLines, gridJson}; });
 ok(r10.boxes.length===3 && r10.boxes[0].t==='Rate' && r10.boxes[0].n===8 && r10.boxes[1].t==='Grid' && r10.boxes[1].n===9 && r10.boxes[2].t==='Trigger' && r10.boxes[2].n===4 && r10.boxes[2].knobs.join()==='Sense'
    && Math.abs((r10.rateParam||0)-1/7)<0.01 && r10.lit && r10.border==='rgb(183, 148, 255)' && r10.color==='rgb(255, 255, 255)' && r10.shadow==='none' && Math.abs((r10.trigParam||0)-1)<0.01 && r10.trigLit && r10.gridJson===8 && r10.gridLines===7+5,
    '[10] the Clock tab: Rate (8 pills) · Grid (9) · Trigger (4 + Sense) in titled boxes; 1/8 writes FLOW_CHOP_VOL_RATE and lights purple-outline / white-ink / no glow; MIDI writes FLOW_CHOP_VOL_TRIG; a Grid pill redraws the field (1/8 = 7 lines + 5 level lines) and pushes grid 8', JSON.stringify(r10));

 // ── [11] the follower ──
 const r11=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const w=(ms)=>new Promise(r=>setTimeout(r,ms)); const scr=card.querySelector('.screen'); const head=card.querySelector('.screen .mv-play'), dot=card.querySelector('.screen .mv-foll');
   const F={ph:[0.10,0,0,0,0,0,0,0],v:[1,0,0,0,0,0,0,0],ln:[1,0,0,0,0,0,0,0],tr:[0,0,0,0,0,0,0,0],b:120,on:1,pl:1}; window.__flowFeedPush={arp:[],gli:[],chop:[F]};
   await w(120); const idle0=scr.classList.contains('idle'); const xs=[]; for(let k=0;k<18;k++){ await w(16); xs.push(+head.getAttribute('x1')); }
   const x0=xs[0], x1=xs[xs.length-1]; const mono=xs.every((x,i)=>i===0||x>=xs[i-1]-0.01); const moved=x1>x0+2;   // 0.29 s at 120 BPM over a bar = 0.145 of the width = ~60 px
   F.ph[0]=0.5; window.__flowFeedPush={arp:[],gli:[],chop:[Object.assign({},F)]}; await w(60); const xJump=+head.getAttribute('x1'); const X=(x)=>8+x*(434-16);
   const dotOnCurve=Math.abs(+dot.getAttribute('cx')-xJump)<0.6;
   window.__flowFeedPush={arp:[],gli:[],chop:[{on:0}]}; await w(120); const idle1=scr.classList.contains('idle');
   return {idle0, x0:+x0.toFixed(1), x1:+x1.toFixed(1), mono, moved, xJump:+xJump.toFixed(1), near:Math.abs(xJump-X(0.5))<40, dotOnCurve, idle1}; });
 ok(!r11.idle0 && r11.mono && r11.moved && r11.near && r11.dotOnCurve && r11.idle1,
    '🚨 [11] the follower rides the push and ADVANCES on the tempo between pushes (never backwards, never parked), snaps to a jump (0.10 → 0.50), the dot rides the head on the curve, and the head fades when the lane is idle', JSON.stringify(r11));

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
