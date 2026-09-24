// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp101 — THE CHOP ENGINE PASS, THROUGH THE SHIPPED DOORS (the heroOverlay booted into the page).
//
//    node Tests/_tp101_chop_gate.js          (from plugins/Terrain; screenshots land in $TMPDIR)
//
//  [0] the Slices counts run 4 / 8 / 16 / 24 / 32 / 64, and the row fits its drawer
//  [1] 🚨 clicking a count in PITCH mode switches to SLICE mode (setSliceMode(1) + the grid)
//  [2] 🚨 a fresh (inheriting) chop's panel SHOWS the pitch-mode setup: A / D / S / R, fine tune,
//      the transpose and the volume — not zeros and struct defaults
//  [3] the Motion row is called Ping-pong and still toggles the same native
//  [4] the Fine row edits the EFFECTIVE pitch and stores the chop's OWN offset under the base
//  [5] 🚨 PITCH MODE PLAYHEADS: one line per sounding voice, drawn on the waveform, gone on release
//  [6] ...and none in SLICE mode (the chops keep their glow)
//  [7] no page errors
//  [8]-[11] the sweep: stretch modes, reverse, double-click markers, the chop glow tracker, loop points
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path'),os=require('os');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const cpp=fs.readFileSync('Source/PluginEditor.cpp','utf8');
const i0=cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0=cpp.indexOf('html = html.replace ("</body>", heroOverlay',i0);
const ov=[...cpp.slice(i0,j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m=>m[1]).join('');
const html=fs.readFileSync('Source/ui/public/index.html','utf8').replace('</body>',ov+'</body>');
const PAGE=path.join(os.tmpdir(),'tp101_chop.html'); fs.writeFileSync(PAGE,html);
const SHOT=n=>path.join(os.tmpdir(),'tp101_'+n+'.png');
let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };

const LEN=220500;
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:2});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();'+`
   (function(){ const orig=window.Juce; window.__natLog=[];
     const get=(n)=>{ const f=(window.__fakeNat&&window.__fakeNat[n])?((...a)=>Promise.resolve(window.__fakeNat[n](...a))):orig.getNativeFunction(n);
                      return function(){ window.__natLog.push([n].concat([].slice.call(arguments))); return f.apply(null,arguments); }; };
     const wrap=Object.assign({},orig,{ getNativeFunction:get });
     Object.defineProperty(window,'Juce',{configurable:true,get(){return wrap;},set(){}}); })();`);
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>document.documentElement.setAttribute('data-theme','dark'));
 await p.evaluate(()=>document.getElementById('mix-btn').click()); await sleep(1400);

 // The engine side, answered by the gate: a loaded one-shot, PITCH mode, and Max's pitch-mode setup.
 await p.evaluate((LEN)=>{
   window.__mode=0;
   const inh=(n)=>{ const s=[]; for(let i=0;i<n;i++) s.push({start:Math.round(LEN*i/n),end:Math.round(LEN*(i+1)/n),reverse:false,pitch:0,warpMode:0,stretchRatio:1,
                     attackMs:-1,releaseMs:-1,decayMs:-1,sustainLevel:-1,volume:-1,scanEnabled:false,scanRate:1,scanWindow:1}); return JSON.stringify({slices:s}); };
   window.__slicesJson=JSON.stringify({slices:[]});
   window.__fakeNat={
     getLayerHasSample:()=>true,
     getSliceMode:()=>window.__mode,
     setSliceMode:(m)=>{ window.__mode=m; },
     getSlicesJson:()=>window.__slicesJson,
     gridSliceSlices:(n)=>{ window.__slicesJson=inh(n); return window.__slicesJson; },
     getPitchSliceJson:()=>JSON.stringify({startSample:0,endSample:LEN,reverse:false,pitch:-2.37,warpMode:0,stretchRatio:1,
                                          attackMs:12,releaseMs:60,decayMs:150,sustainLevel:0.4,volume:0.6,scanEnabled:false,scanRate:1,scanWindow:1}),
     getPitchPlayheads:()=>window.__ph||[],
   };
   if(window.__tiChopRepull) window.__tiChopRepull();   /* pulls the pitch-mode setup through getPitchSliceJson */
 },LEN); await sleep(700);
 await p.evaluate((LEN)=>{   /* the sample AFTER the recall: a recall into a layer re-reads its picture */
   const N=900,mn=[],mx=[]; for(let i=0;i<N;i++){ const e=0.35+0.5*Math.abs(Math.sin(i/70)); mx.push(e); mn.push(-e); }
   window.onSampleLoaded({filename:'Pad 138.wav',lengthSamples:LEN,peaksMin:mn,peaksMax:mx,rootMidiNote:60});
 },LEN); await sleep(900);

 // ── [0] the counts ──
 await p.evaluate(()=>document.getElementById('ti-slices-btn').dispatchEvent(new MouseEvent('click',{bubbles:true}))); await sleep(600);
 const r0=await p.evaluate(()=>{ const d=document.getElementById('ti-slicer-drawer'), row=document.getElementById('ti-grid-row');
   const pills=[...row.querySelectorAll('.ti-grid-pill')]; const dr=d.getBoundingClientRect(), last=pills[pills.length-1].getBoundingClientRect();
   const tops=new Set(pills.map(e=>Math.round(e.getBoundingClientRect().top)));
   return {n:pills.map(e=>e.dataset.n+'/'+e.textContent).join(' '), inside:last.right<=dr.right-4, oneRow:tops.size===1, open:d.classList.contains('open')}; });
 ok(r0.n==='4/4 8/8 16/16 24/24 32/32 64/64' && r0.inside && r0.oneRow && r0.open, '[0] the Slices counts run to 64 and the row fits its drawer on one line', JSON.stringify(r0));
 await p.screenshot({path:SHOT('drawer'),clip:await p.evaluate(()=>{ const r=document.getElementById('ti-slicer-drawer').getBoundingClientRect(); return {x:r.left-10,y:r.top-10,width:r.width+20,height:r.height+60}; })});

 // ── [1] a count switches to SLICE ──
 await p.evaluate(()=>{ window.__natLog.length=0; });
 const r1=await p.evaluate(async()=>{
   const before=document.querySelector('#ti-mode-toggle .ti-mode-pill.active').dataset.mode;
   document.querySelector('#ti-grid-row .ti-grid-pill[data-n="8"]').dispatchEvent(new MouseEvent('click',{bubbles:true}));
   await new Promise(r=>setTimeout(r,500));
   return {before, after:document.querySelector('#ti-mode-toggle .ti-mode-pill.active').dataset.mode,
     calls:window.__natLog.filter(e=>/^(setSliceMode|gridSliceSlices)$/.test(e[0])).map(e=>e[0]+'('+e.slice(1).join(',')+')'),
     bodies:document.querySelectorAll('#ti-slice-overlays .ti-slice-body').length,
     count:(document.getElementById('ti-slices-count')||{}).textContent,
     active:(document.querySelector('#ti-grid-row .ti-grid-pill.active')||{}).textContent }; });
 ok(r1.before==='PITCH' && r1.after==='SLICE' && r1.calls.join(' ')==='setSliceMode(1) gridSliceSlices(8)' && r1.bodies===8 && r1.count==='8' && r1.active==='8',
    '🚨 [1] clicking 8 in PITCH mode switches to SLICE mode and cuts eight chops', JSON.stringify(r1));

 // ── [2] the inheriting chop SHOWS the pitch-mode setup ──
 await p.evaluate(()=>document.getElementById('ti-slices-btn').dispatchEvent(new MouseEvent('click',{bubbles:true}))); await sleep(300);
 const body=await p.evaluate(()=>{ const e=document.querySelectorAll('#ti-slice-overlays .ti-slice-body')[3]; const r=e.getBoundingClientRect(); return {x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2)}; });
 await p.mouse.click(body.x,body.y,{button:'right'}); await sleep(800);
 const r2=await p.evaluate(()=>{ const pn=document.getElementById('ti-chop-panel');
   return {open:pn.classList.contains('open'), idx:pn.dataset.targetIdx,
     rows:[...pn.querySelectorAll('.ov-ad-row')].map(r=>r.dataset.h+'='+r.querySelector('.ov-ad-val').textContent).join(' '),
     tiles:[...pn.querySelectorAll('.ov-ctrl .ov-val')].map(e=>e.textContent).join(' ')}; });
 ok(r2.open && r2.idx==='3' && r2.rows==='A=12 ms D=150 ms S=40% R=60 ms F=-37 ¢' && /^60% -2 ST /.test(r2.tiles+' '),
    '🚨 [2] a fresh chop\'s panel SHOWS what it inherits — A 12 ms, D 150 ms, S 40 %, R 60 ms, fine -37 ¢, -2 ST, volume 60 % (it read +0 ST / 0 ¢)',
    JSON.stringify(r2));
 await p.screenshot({path:SHOT('panel'),clip:await p.evaluate(()=>{ const r=document.getElementById('ti-chop-panel').getBoundingClientRect(); return {x:r.left-6,y:r.top-6,width:r.width+12,height:r.height+12}; })});

 // ── [3] Ping-pong ──
 await p.evaluate(()=>{ window.__natLog.length=0; });
 const r3=await p.evaluate(async()=>{ const lab=document.querySelector('#ti-chop-panel .motion-label').textContent, pill=document.getElementById('scan-pill');
   const t0=pill.textContent; pill.dispatchEvent(new MouseEvent('click',{bubbles:true})); await new Promise(r=>setTimeout(r,150));
   const t1=pill.textContent; pill.dispatchEvent(new MouseEvent('click',{bubbles:true})); await new Promise(r=>setTimeout(r,150));
   return {lab,t0,t1,t2:pill.textContent,calls:window.__natLog.filter(e=>e[0]==='setSliceScanEnabled').map(e=>e.slice(1).join(','))}; });
 ok(r3.lab==='Ping-pong' && r3.t0==='Off' && r3.t1==='On' && r3.t2==='Off' && r3.calls.join(' ')==='3,true 3,false',
    '[3] the Motion row reads Ping-pong and still drives setSliceScanEnabled', JSON.stringify(r3));

 // ── [4] Fine edits the effective pitch, stores the own offset ──
 await p.evaluate(()=>{ window.__natLog.length=0; });
 const ft=await p.evaluate(()=>{ const t=document.querySelector('#ti-chop-panel .ov-ad-row[data-h="F"] .ov-ad-track'); const r=t.getBoundingClientRect(); return {x:r.left,y:Math.round(r.top+r.height/2),w:r.width}; });
 await p.mouse.click(Math.round(ft.x+ft.w*0.9),ft.y); await sleep(300);
 const r4=await p.evaluate(()=>{ const pn=document.getElementById('ti-chop-panel');
   const sent=window.__natLog.filter(e=>e[0]==='setSlicePitch').map(e=>[e[1],e[2]]);
   return {sent, cents:pn.querySelector('.ov-ad-row[data-h="F"] .ov-ad-val').textContent, semis:pn.querySelector('.ov-ctrl[data-ctrl="pitch"] .ov-val').textContent}; });
 const own=r4.sent.length?r4.sent[r4.sent.length-1]:[null,null];
 const cents=parseInt(r4.cents,10);
 ok(own[0]===3 && Math.abs(own[1]-(-2+cents/100+2.37))<0.011 && cents>=35 && cents<=45 && r4.semis==='-2 ST',
    '[4] the Fine row moves the EFFECTIVE cents (-2 ST stays) and stores own = effective - pitch-mode base',
    JSON.stringify(r4));
 await p.evaluate(()=>document.getElementById('ti-chop-close').dispatchEvent(new MouseEvent('click',{bubbles:true}))); await sleep(300);

 // ══ [8]-[12] THE REGRESSION SWEEP — the slicer's everyday doors still reach their natives ══
 await p.evaluate(()=>{ window.__tiForceActive=true; window.dispatchEvent(new Event('tiactive'));
   window.__fakeNat.addMarkerAt=(pos)=>window.__slicesJson;   /* the list is the engine's answer; leave it be */
   window.__fakeNat.getSliceGlowLevels=()=>[0,0,0,0.9,0,0,0,0]; window.__natLog.length=0; });
 const b3=await p.evaluate(()=>{ const e=document.querySelectorAll('#ti-slice-overlays .ti-slice-body')[3]; const r=e.getBoundingClientRect(); return {x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2)}; });
 await p.mouse.click(b3.x,b3.y,{button:'right'}); await sleep(700);
 const r8=await p.evaluate(async()=>{ const pn=document.getElementById('ti-chop-panel'), W=()=>new Promise(r=>setTimeout(r,250));
   pn.querySelector('.ov-mode[data-mode="1"]').dispatchEvent(new MouseEvent('click',{bubbles:true})); await W();
   const warp=pn.getAttribute('data-warp'), stretchShown=getComputedStyle(pn.querySelector('.ov-ctrl[data-ctrl="stretch"]')).display!=='none';
   pn.querySelector('.ov-mode[data-mode="0"]').dispatchEvent(new MouseEvent('click',{bubbles:true})); await W();
   pn.querySelector('.ov-act[data-act="rev"]').dispatchEvent(new MouseEvent('click',{bubbles:true})); await W();
   const revTag=!!document.querySelectorAll('#ti-slice-overlays .ti-slice-body')[3].querySelector('.ti-slice-rev-letter');
   pn.querySelector('.ov-act[data-act="rev"]').dispatchEvent(new MouseEvent('click',{bubbles:true})); await W();
   return {warp, stretchShown, revTag, calls:window.__natLog.filter(e=>/^setSlice(WarpMode|Reverse)$/.test(e[0])).map(e=>e[0].replace('setSlice','')+'('+e.slice(1).join(',')+')').join(' ')}; });
 ok(r8.warp==='beats' && r8.stretchShown && r8.revTag && r8.calls==='WarpMode(3,1) WarpMode(3,0) Reverse(3,true) Reverse(3,false)',
    '[8] stretch modes + reverse: Beats shows the stretch tile, Reverse tags the chop, both reach the engine', JSON.stringify(r8));
 await p.evaluate(()=>document.getElementById('ti-chop-close').dispatchEvent(new MouseEvent('click',{bubbles:true}))); await sleep(300);
 // double-click a chop body = a new marker AT the pointer (the cumulative-layout mapping)
 await p.evaluate(()=>{ window.__natLog.length=0; });
 const b5=await p.evaluate(()=>{ const e=document.querySelectorAll('#ti-slice-overlays .ti-slice-body')[5]; const r=e.getBoundingClientRect(); return {x:Math.round(r.left+r.width*0.5),y:Math.round(r.top+r.height/2)}; });
 await p.mouse.click(b5.x,b5.y,{count:2}); await sleep(400);
 const r9=await p.evaluate((LEN)=>{ const m=window.__natLog.filter(e=>e[0]==='addMarkerAt'); return {n:m.length, pos:m.length?m[m.length-1][1]:null, want:Math.round(LEN*5.5/8)}; },LEN);
 ok(r9.n===1 && Math.abs(r9.pos-r9.want)<LEN*0.02, '[9] double-click on a chop adds a marker where the pointer is (addMarkerAt)', JSON.stringify(r9));
 // the chop engine's MIDI tracker: a sounding chop glows
 await sleep(300);
 const r10=await p.evaluate(()=>[...document.querySelectorAll('#ti-slice-overlays .ti-slice-body')].map(b=>b.style.getPropertyValue('--glow-alpha')).join(','));
 ok(r10==='0.000,0.000,0.000,0.900,0.000,0.000,0.000,0.000', '[10] the chop engine\'s MIDI tracker: the sounding chop glows (getSliceGlowLevels → --glow-alpha)', r10);
 await p.evaluate(()=>{ window.__fakeNat.getSliceGlowLevels=()=>[0,0,0,0,0,0,0,0]; });
 // the pitch-mode loop points (IN / OUT) still drag into the engine
 await p.evaluate(()=>{ const q=document.querySelectorAll('#ti-mode-toggle .ti-mode-pill'); q[0].dispatchEvent(new MouseEvent('click',{bubbles:true})); window.__natLog.length=0; }); await sleep(500);
 const em=await p.evaluate(()=>{ const e=document.querySelector('.ti-pitch-bound-marker.ti-pitch-bound-end'); if(!e) return null; const r=e.getBoundingClientRect(); return {x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2),W:document.getElementById('ti-slice-overlays').clientWidth}; });
 if(em){ em.x=Math.min(em.x,818); await p.mouse.move(em.x,em.y); await p.mouse.down(); await p.mouse.move(em.x-Math.round(em.W*0.25),em.y,{steps:6}); await p.mouse.up(); await sleep(300); }
 const r11=await p.evaluate((LEN)=>{ const m=window.__natLog.filter(e=>e[0]==='setPitchSliceBounds'); const last=m[m.length-1]||[];
   return {n:m.length, start:last[1], end:last[2], frac:last[2]/LEN}; },LEN);
 ok(!!em && r11.n>0 && r11.start===0 && r11.frac>0.70 && r11.frac<0.80, '[11] the pitch-mode OUT point drags into setPitchSliceBounds (the loop points)', JSON.stringify(r11));
 // put OUT back so the playhead bars see the whole sample

 // ── [5] pitch-mode playheads ──
 await p.evaluate(()=>{ const q=document.querySelectorAll('#ti-mode-toggle .ti-mode-pill'); q[0].dispatchEvent(new MouseEvent('click',{bubbles:true})); }); await sleep(500);
 /* a note sounding is what keeps the page awake in the plugin (__tiRest reads __notesActive); the gate says so directly */
 await p.evaluate(()=>{ window.__tiForceActive=true; window.dispatchEvent(new Event('tiactive')); window.__ph=[5,0.30,0.0, 9,0.70,0.0]; }); await sleep(500);
 const r5=await p.evaluate(()=>{ const L=(window.__tiPitchPlayheads?window.__tiPitchPlayheads():[]);
   const c=document.getElementById('ti-scan-viz-canvas'), g=c.getContext('2d'), w=c.width, h=c.height;
   const colAlpha=(fr)=>{ const x=Math.round(fr*w); let m=0; for(let dx=-3;dx<=3;dx++){ const d=g.getImageData(Math.max(0,Math.min(w-1,x+dx)),Math.round(h/2),1,1).data; m=Math.max(m,d[3]); } return m; };
   return {lines:L.map(e=>e.key+'@'+e.pos.toFixed(3)+'/'+e.op.toFixed(2)), a30:colAlpha(0.30), a70:colAlpha(0.70), a50:colAlpha(0.50)}; });
 ok(r5.lines.length===2 && r5.a30>100 && r5.a70>100 && r5.a50===0, '🚨 [5] PITCH MODE: one moving playhead line per sounding voice, painted on the waveform at its read point', JSON.stringify(r5));
 await p.screenshot({path:SHOT('playheads'),clip:await p.evaluate(()=>{ const r=document.getElementById('hero').getBoundingClientRect(); return {x:r.left,y:r.top,width:r.width,height:r.height}; })});
 // glide: a voice moving at 0.5 sample/s is seen moving between polls
 // a voice MOVING at 0.5 of the sample per second: the line follows it (v9 has been released)
 await p.evaluate(()=>{ const t0=performance.now(); window.__fakeNat.getPitchPlayheads=()=>[5,0.30+0.5*(performance.now()-t0)/1000,0.5]; }); await sleep(400);
 const r5b=await p.evaluate(()=>(window.__tiPitchPlayheads?window.__tiPitchPlayheads():[]).map(e=>e.key+'@'+e.pos.toFixed(3)+'/'+e.target));
 await p.evaluate(()=>{ window.__fakeNat.getPitchPlayheads=()=>window.__ph||[]; });
 await p.evaluate(()=>{ window.__ph=[]; }); await sleep(600);
 const r5c=await p.evaluate(()=>{ const c=document.getElementById('ti-scan-viz-canvas'), g=c.getContext('2d');
   const d=g.getImageData(0,0,c.width,c.height).data; let lit=0; for(let i=3;i<d.length;i+=4) if(d[i]>0) lit++; return {left:(window.__tiPitchPlayheads?window.__tiPitchPlayheads():[]).length, lit}; });
 const v5=r5b.find(s=>/^v5@/.test(s)), v5pos=v5?parseFloat(v5.split('@')[1]):-1;
 ok(r5b.length===1 && v5pos>0.47 && v5pos<0.54 && r5c.left===0 && r5c.lit===0,
    '[5] the line FOLLOWS a moving voice (0.30 → ~0.50 in 400 ms), a released voice\'s line is gone, and the canvas is left clear', JSON.stringify({during:r5b, after:r5c}));

 // ── [6] not in SLICE mode ──
 await p.evaluate(()=>{ const q=document.querySelectorAll('#ti-mode-toggle .ti-mode-pill'); q[1].dispatchEvent(new MouseEvent('click',{bubbles:true})); window.__ph=[5,0.30,0.0]; }); await sleep(500);
 const r6=await p.evaluate(()=>({lines:(window.__tiPitchPlayheads?window.__tiPitchPlayheads():[]).length, bodies:document.querySelectorAll('#ti-slice-overlays .ti-slice-body').length}));
 ok(r6.lines===0 && r6.bodies===8, '[6] SLICE mode draws no pitch playheads — the chops keep their glow', JSON.stringify(r6));

 ok(errs.length===0,'[7] no page errors',errs.join(' | '));
 console.log('\n'+pass+' passed, '+fail+' failed'); console.log('shots: '+['drawer','panel','playheads'].map(SHOT).join('  '));
 await b.close(); process.exit(fail?1:0);
})();
