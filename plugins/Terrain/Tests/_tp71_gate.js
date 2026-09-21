// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp71 — THE TERRAIN SHAPER, page half.
//
//    node Tests/_tp71_gate.js
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
//  [7] presets: the factory list has the shapes; recalling one stamps its lanes
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const src=fs.readFileSync('Source/ui/public/index.html','utf8');
const PAGE=path.join(require('os').tmpdir(),'tp71.html'); fs.writeFileSync(PAGE,src);
let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:1200,height:900});
 await p.evaluateOnNewDocument(stubSrc+'\nstub(); window.__shpJson=[]; ["VOL","TIME","FILT","PAN","REP","DRIVE","PHASE","CRUSH"].forEach(function(L,i){ window.__params["FLOW_CHOP_"+L+"_ON"]=i?0:1; window.__params["FLOW_CHOP_"+L+"_DEPTH"]=1; window.__params["FLOW_CHOP_"+L+"_RATE"]=4/7; window.__params["FLOW_CHOP_"+L+"_MODE"]=0; }); (function(){ const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>{ if(n==="setShaperJson") return (i,j)=>{ window.__shpJson.push([i,j]); return Promise.resolve(0); }; if(n==="getShaperJson") return ()=>Promise.resolve("{}"); return orig.getNativeFunction(n); } }); Object.defineProperty(window,"Juce",{configurable:true,get(){return wrap;},set(){}}); })();');
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,200)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>{ document.documentElement.setAttribute('data-theme','dark'); window.setActivePanel('syn'); }); await sleep(600);

 // ── [0] the card ──
 const r0=await p.evaluate(async()=>{ const c=window.__flowCardOf('chop',1); c.open(); await new Promise(r=>setTimeout(r,700));
   const card=document.querySelector('.ti-card.shp-ext'); if(!card) return {err:'no .ti-card.shp-ext'};
   const tile=document.querySelector('.flow-mode[data-mode="chop"]');
   return { open:card.classList.contains('open'), title:card.querySelector('.h .tt').textContent, mix:!!card.querySelector('.h .mix'), screen:!!card.querySelector('.screen svg'),
     tiles:card.querySelectorAll('.fx .fxb').length, leds:card.querySelectorAll('.fleds i').length, tabs:[...card.querySelectorAll('.tab')].map(t=>t.textContent), chain:!!card.querySelector('.foot, .chain, .slots'),
     tilePaths:tile?tile.querySelectorAll('svg path').length:-1, tileCircles:tile?tile.querySelectorAll('svg circle').length:-1, tileTitle:tile?tile.getAttribute('title'):null, tileAnim:tile?!!tile.querySelector('animateTransform, animate'):false, tileSine:tile?!!tile.querySelector('path.shpSine'):false, tileClip:tile?!!tile.querySelector('clipPath'):false,
     name:(window.__tpNodeName?'':'') }; });
 ok(!r0.err && r0.open && r0.title==='Shaper' && r0.mix && r0.screen && r0.tiles===8 && r0.leds===8 && r0.tabs.join('|')==='Shape|Target|Clock',
    '[0] the Chop slot builds the SHAPER: title, Mix header, the screen, eight targets with dots, Shape / Target / Clock', JSON.stringify(r0));
 ok(r0.tilePaths===1 && r0.tileCircles===0 && r0.tileTitle==='Shaper' && !r0.tileAnim && r0.tileSine && !r0.tileClip, '[0b] the tile\'s emblem is ONE full sine path between fixed ends — no clip, no SMIL (tp71b: the motion clock moves it), nothing else on it', JSON.stringify({paths:r0.tilePaths,circles:r0.tileCircles,title:r0.tileTitle,anim:r0.tileAnim,sine:r0.tileSine,clip:r0.tileClip}));
 const nm=await p.evaluate(()=>{ try{ return (function(){ const s=document.documentElement.outerHTML; return /FLOWNAME=\{arp:'Arp',drift:'Robin',chop:'Shaper'/.test(s); })(); }catch(e){ return false; } });
 ok(nm, '[0c] FLOWNAME.chop is Shaper (the Patcher, the browser and the pills say so)');

 // ── [1] drawn from breakpoints ──
 const r1=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const d0=card.querySelector('.screen path.line').getAttribute('d');
   const ptsVol=card.querySelectorAll('.screen .pts circle').length;
   card.querySelectorAll('.fx .fxb')[1].click(); await new Promise(r=>setTimeout(r,200));
   const d1=card.querySelector('.screen path.line').getAttribute('d'); const ptsTime=card.querySelectorAll('.screen .pts circle').length; const ghosts=card.querySelectorAll('.screen .ghosts path').length;
   const ys=(d1.match(/[ML](-?[\d.]+) (-?[\d.]+)/g)||[]).map(t=>+t.split(' ')[1]); const mono=ys.every((y,i)=>i===0||y<=ys[i-1]+0.01);
   card.querySelectorAll('.fx .fxb')[0].click(); await new Promise(r=>setTimeout(r,200));
   return {ptsVol,ptsTime,ghosts,d0len:d0.length,mono,changed:d0!==d1}; });
 ok(r1.ptsVol===32 && r1.ptsTime===2 && r1.mono && r1.ghosts===1,
    '🚨 [1] Volume boots as a 1/16 gate (32 breakpoints: 16 flats), Time as a unity ramp (2 points, the line rises monotonically); the lit Volume lane ghosts behind Time', JSON.stringify(r1));

 // ── [2] the selector strip ──
 const r2=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const pn=card.querySelector('.screen .sel .pn'), pa=card.querySelectorAll('.screen .sel .pa');
   const out={};
   card.querySelectorAll('.fx .fxb')[2].click(); await new Promise(r=>setTimeout(r,150)); out.filt0=pn.textContent; pa[1].click(); await new Promise(r=>setTimeout(r,150)); out.filt1=pn.textContent; out.modeParam=window.__params['FLOW_CHOP_FILT_MODE'];
   card.querySelectorAll('.fx .fxb')[1].click(); await new Promise(r=>setTimeout(r,150)); out.time0=pn.textContent; const dA=card.querySelector('.screen path.line').getAttribute('d'); pa[1].click(); await new Promise(r=>setTimeout(r,150)); out.time1=pn.textContent; const dB=card.querySelector('.screen path.line').getAttribute('d'); out.restamped=dA!==dB;
   card.querySelectorAll('.fx .fxb')[0].click(); await new Promise(r=>setTimeout(r,150)); out.vol=pn.textContent;
   return out; });
 ok(r2.filt0==='Low' && r2.filt1==='High' && Math.abs((r2.modeParam||0)-1/3)<0.02 && /Unity|Half/i.test(r2.time0) && r2.restamped && /Sine|Triangle|Saw|Square|Gate|Stairs|Flat/i.test(r2.vol),
    '[2] the selector strip: Filter walks Low → High and writes FLOW_CHOP_FILT_MODE; Time walks its presets and restamps the lane; Volume shows the stock shapes', JSON.stringify(r2));

 // ── [3] brushes ──
 const r3=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const svg=card.querySelector('.screen svg'); const r=svg.getBoundingClientRect();
   const tools=card.querySelectorAll('.brushes .fxb'); const lineBtn=[...tools].find(t=>t.textContent==='Line'), pointBtn=[...tools].find(t=>t.textContent==='Point');
   // stamp a flat at 25 % height into the cell under x = 0.30 of the screen
   lineBtn.click(); await new Promise(r=>setTimeout(r,60));
   const sx=(x)=>r.left+r.width*(6+x*(288-12))/288, sy=(y)=>r.top+r.height*(8+(1-y)*(150-8-26))/150;   /* screen units → client, through the zoomed rect */
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
   return {flat25,inCell:inCell.length,n0,n1:pts2.length,snapped:!!added,pushes:window.__shpJson.length}; });
 ok(r3.flat25 && r3.n1===r3.n0+1 && r3.snapped, '[3] the Line brush stamps a flat at the pointer\'s height into its grid cell; the Point tool adds a point and Snap lands it on the grid (0.625 = 10/16)', JSON.stringify(r3));

 // ── [4] the push ──
 const r4=await p.evaluate(()=>{ const last=window.__shpJson[window.__shpJson.length-1]; const o=JSON.parse(last[1]); return {inst:last[0],lanes:o.lanes.length,keys:Object.keys(o.lanes[2]).sort().join(','),k:o.lanes[2].k.length,pts:o.lanes.every(L=>L.pts.length>=2&&L.pts[0][0]===0&&L.pts[L.pts.length-1][0]===1)}; });
 ok(r4.inst===0 && r4.lanes===8 && r4.keys==='blend,floor,grid,k,phase,pts,smooth,swing,tension' && r4.k===4 && r4.pts,
    '🚨 [4] every edit pushes setShaperJson(inst, {lanes:[8 × {pts (pinned 0..1), smooth, phase, tension, floor, blend, swing, grid, k[4]}]})', JSON.stringify(r4));

 // ── [5] parameters + the dice ──
 const r5=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const t=card.querySelectorAll('.fx .fxb'); const out={};
   const S=window.__tiDice.chop.S; out.on0=S.v.on0; out.dep0=S.v.depth0; out.rate0=S.v.rate0;
   t[3].click(); await new Promise(r=>setTimeout(r,80)); t[3].click(); await new Promise(r=>setTimeout(r,500)); out.on3=window.__params['FLOW_CHOP_PAN_ON']; out.led3=card.querySelectorAll('.fleds i')[3].classList.contains('on');
   t[3].click(); await new Promise(r=>setTimeout(r,80)); t[3].click(); await new Promise(r=>setTimeout(r,500)); out.on3b=window.__params['FLOW_CHOP_PAN_ON'];
   const before=JSON.stringify(window.__tiDice.chop.S.v); window.__tiDiceMode('chop',true); await new Promise(r=>setTimeout(r,80)); out.diceMoved=JSON.stringify(window.__tiDice.chop.S.v)!==before;
   t[0].click(); return out; });
 ok(r5.on0===1 && r5.dep0===1 && r5.rate0===4 && r5.on3===1 && r5.led3 && r5.on3b===0 && !r5.diceMoved,
    '[5] On / Depth / Rate ride FLOW_CHOP_<LANE>_ parameters (Volume on, depth 1, rate 1 bar); a double-tap on the lit Pan tile toggles its lane on and off; the dice moves nothing', JSON.stringify(r5));

 // ── [6] the menu ──
 const r6=await p.evaluate(async()=>{ const card=document.querySelector('.ti-card.shp-ext'); const svg=card.querySelector('.screen svg'); const r=svg.getBoundingClientRect();
   svg.dispatchEvent(new MouseEvent('contextmenu',{clientX:r.left+80,clientY:r.top+60,bubbles:true,cancelable:true})); await new Promise(r=>setTimeout(r,300));
   const m=document.getElementById('syn-ctx-menu')||document.querySelector('.syn-menu, .pmenu.act, [class*="menu"].act'); const txt=m?m.textContent:''; const vis=m?getComputedStyle(m).display!=='none':false;
   return {vis, hasGrid:/Grid/.test(txt), snap:/Snap/.test(txt), flips:/Flip vertical/.test(txt)&&/Flip horizontal/.test(txt), random:/Random flat/.test(txt), tools:/Point · edit/.test(txt)&&/Free draw/.test(txt)&&/Sine/.test(txt), stock:/Stairs/.test(txt)&&/Gate/.test(txt), clear:/Clear/.test(txt)}; });
 ok(r6.vis && r6.hasGrid && r6.snap && r6.flips && r6.random && r6.tools && r6.stock && r6.clear, '[6] right-click on the screen opens the LFO\'s menu: Grid slider, Snap, the flips, Random, the eight tools, the stock shapes, Clear', JSON.stringify(r6));

 // ── [7] presets ──
 const r7=await p.evaluate(async()=>{ try{ window.__synHideMenu&&window.__synHideMenu(); }catch(e){} const card=document.querySelector('.ti-card.shp-ext'); const S=window.__tiDice.chop.S;
   const before=S.v.pts1; const pill=card.querySelector('.h .pset .pn'); pill.click(); await new Promise(r=>setTimeout(r,300));
   const items=[...document.querySelectorAll('.pmenu .pi .nm')].map(e=>e.textContent); const half=[...document.querySelectorAll('.pmenu .pi')].find(e=>/Stutter 1\/16/.test(e.textContent));   /* bar [2] already walked Time onto Half time — recall a different one */
   if(half) half.click(); await new Promise(r=>setTimeout(r,300));
   return {items:items.slice(0,12), stamped:S.v.pts1!==before && S.v.on1===1 && S.v.lane===1, lane:S.v.lane, on1:S.v.on1, changed:S.v.pts1!==before, menuStill:!!document.querySelector('.pmenu')}; });
 ok(r7.items.some(x=>/Gate 16ths/.test(x)) && r7.items.some(x=>/Stutter 1\/16/.test(x)) && r7.items.some(x=>/Tape stop/.test(x)) && r7.stamped, '[7] the house preset menu lists the factory shapes; Stutter 1/16 recalls onto the Time lane and lights it', JSON.stringify(r7));

 // ── [9] tp71b — the tile's sine rides the motion clock: one period per beat of the host BPM, winds up on MIDI, winds down
 //        onto its home (phase 0 — the same picture every time), and with Motion off it never moves ──
 const r9=await p.evaluate(async()=>{ try{ const c=window.__flowCardOf('chop',1); if(c&&c.close) c.close(); }catch(e){}
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
 ok(!r9.err && r9.moved && r9.v120>0.8 && r9.v120<1.6 && r9.v240>3.2 && r9.v240<4.8 && r9.landed && r9.homeAgain && r9.offStill,   /* 0.72 s at 120 BPM less the 0.25 s wind-up ≈ 1.2 periods; 0.96 s at 240 BPM ≈ 3.84 */
    '[9] tp71b — the sine travels ~1 period per beat (120 vs 240 BPM: ~2x), winds up on MIDI, winds DOWN onto the same home picture, and with Motion off it stays on that picture', JSON.stringify(r9));

 ok(errs.length===0,'[8] the page threw nothing', errs.slice(0,3).join(' | ')||'clean');
 await b.close(); console.log('\n  '+pass+' passed, '+fail+' failed\n'); process.exit(fail?1:0);
})();
