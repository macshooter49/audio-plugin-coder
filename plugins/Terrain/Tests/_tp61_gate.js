// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp61 — MAX'S 2026-09-20 LIST, THROUGH THE SHIPPED DOORS.
//
//  Every bar here drives the PAGE's own function, never a re-implementation of it. The two that
//  have burned this project twice — a gate that dispatched a synthetic KeyboardEvent the plugin
//  never sends, and a gate that assigned the object it was meant to be measuring — are the reason
//  [0] calls window.__tpConnect (the canvas's real cable gate) and [6] reads getComputedStyle on
//  a live element rather than grepping the stylesheet text.
//
//    node Tests/_tp61_gate.js
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const cpp=fs.readFileSync('Source/PluginEditor.cpp','utf8');
const i0=cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0=cpp.indexOf('html = html.replace ("</body>", heroOverlay',i0);
const ov=[...cpp.slice(i0,j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m=>m[1]).join('');
const html=fs.readFileSync('Source/ui/public/index.html','utf8').replace('</body>',ov+'</body>');
const PAGE=path.join(require('os').tmpdir(),'tp61.html'); fs.writeFileSync(PAGE,html);

let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));}
                    else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };
const fake=()=>{const N=1200,mn=[],mx=[];for(let i=0;i<N;i++){const e=.2+.7*Math.abs(Math.sin(i/23));mx.push(e);mn.push(-e);}
 window.onSampleLoaded({filename:'BELL ONE SHOT 01.wav',lengthSamples:48000,peaksMin:mn,peaksMax:mx,rootMidiNote:60});};

(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:2});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();');
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>document.documentElement.setAttribute('data-theme','dark'));

 // ══ THE PATCHER ══════════════════════════════════════════════════════════════════════════════
 await p.evaluate(()=>window.setActivePanel('tp')); await sleep(2200);

 // ── [0] 🚨 A GLITCH FED BY A SAMPLER GOES INTO A DELAY (the "attach it to an oscillator" toast) ──
 const r0=await p.evaluate(async()=>{
   const T=window.__tpTest||{}; const toasts=[]; const ot=window.__tiToast;
   window.__tiToast=function(m){ toasts.push(String(m)); if(ot) try{ot(m);}catch(e){} };
   // put a Glitch on the canvas and cable Sampler A into it, through the canvas's own gate
   window.__flowSetChain&&window.__flowSetChain((window.__flowChain?window.__flowChain():[]).concat(['glitch']));
   await new Promise(r=>setTimeout(r,700));
   const N=window.__tpRaw?window.__tpRaw():null;
   const byKey=k=>N?N.find(n=>n.key===k):null;
   let sa=byKey('samp-a'); if(!sa&&window.__tpAdoptSamp) sa=window.__tpAdoptSamp('a');
   await new Promise(r=>setTimeout(r,500));
   const nodes=window.__tpRaw?window.__tpRaw():[];
   const g=nodes.find(n=>n.kind==='flow'&&n.sub==='glitch');
   const s=nodes.find(n=>n.kind==='samp'&&n.sub==='a');
   if(!g||!s) return {err:'no glitch or sampler node', kinds:nodes.map(n=>n.kind+':'+(n.sub||n.idx))};
   window.__tpConnect(s,'out',0,g,'in',0);          // Sampler A → Glitch
   await new Promise(r=>setTimeout(r,400));
   const chopsOnGlitch=window.__tpFlowChops?window.__tpFlowChops('glitch'):-1;
   // now a Delay, and the Glitch into it
   let devs=window.__fxrDevs?window.__fxrDevs():[];
   if(!devs.some(d=>d&&d.core==='delay')){ window.__fxrAdd&&window.__fxrAdd('delay'); await new Promise(r=>setTimeout(r,900)); }
   window.__tpSyncPresence&&window.__tpSyncPresence(); await new Promise(r=>setTimeout(r,700));
   devs=window.__fxrDevs?window.__fxrDevs():[];
   const di=devs.findIndex(d=>d&&d.core==='delay');
   const after=window.__tpRaw().find(n=>n.kind==='fx'&&n.idx===di);
   if(di<0||!after) return {err:'no delay device on the canvas', cores:devs.map(d=>d&&d.core), fxNodes:window.__tpRaw().filter(n=>n.kind==='fx').map(n=>n.idx)};
   toasts.length=0;
   window.__tpConnect(g,'out',0,after,'in',0);      // Glitch → Delay
   await new Promise(r=>setTimeout(r,400));
   const dv=(window.__fxrDevs?window.__fxrDevs():[])[di];
   return { chopsOnGlitch, toasts:toasts.slice(), inline:window.__tpFlowInline?window.__tpFlowInline('glitch'):null,
            delayChops: (window.__tpChopsOf&&dv)?window.__tpChopsOf(dv):-1 };
 });
 ok(!r0.err && r0.chopsOnGlitch===1 && r0.delayChops===1 && !r0.toasts.some(t=>/oscillator/i.test(t)),
    '🚨 [0] SAMPLER A → GLITCH → DELAY IS ONE CHAIN (tp41 refused it: "Cable an oscillator into Glitch first")',
    JSON.stringify(r0));

 // ── [1] the Sampler has an OUT and no IN ──
 const r1=await p.evaluate(()=>{ const n=(window.__tpRaw?window.__tpRaw():[]).find(x=>x.kind==='samp');
   if(!n) return null; const k=n.ports.map(q=>q.kind); return {ins:k.filter(x=>x==='in').length, outs:k.filter(x=>x==='out').length}; });
 ok(r1 && r1.ins===0 && r1.outs===1, '[1] the Sampler module has an output and NO input (Max reversed tp58)', JSON.stringify(r1));

 // ── [2] a CABLED sampler survives a layout with no samp flag at all ──
 const r2=await p.evaluate(async()=>{
   const L=window.__tpLayout?window.__tpLayout():null; if(L&&L.samp) delete L.samp;   // the localStorage flag is gone
   const n=(window.__tpRaw?window.__tpRaw():[]).find(x=>x.kind==='samp');
   if(n&&window.__tpReturnSamp) window.__tpReturnSamp(n);                              // take it off the canvas
   await new Promise(r=>setTimeout(r,300));
   window.__tpSyncPresence&&window.__tpSyncPresence();                                 // the pass that re-adopts
   await new Promise(r=>setTimeout(r,300));
   return !!(window.__tpRaw?window.__tpRaw():[]).find(x=>x.kind==='samp'&&x.sub==='a');
 });
 ok(r2===true, '[2] a Sampler whose CABLES are in the patch comes back with no layout flag (Max: "they keep disappearing")');

 // ── [3] E–H carry the same right-click menu A–D do ──
 const r3=await p.evaluate(()=>{
   const out={};
   ['a','b','e','f','g','h'].forEach(L=>{
     const d=document.getElementById('osc-'+L+'-device'); if(!d){ out[L]='no device'; return; }
     const disp=d.querySelector('.osc-display'); if(!disp){ out[L]='no display'; return; }
     let opened=false; const m=document.querySelector('.oscq-pop');
     disp.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:200,clientY:200}));
     const mm=document.querySelector('.oscq-pop');
     opened=!!(mm&&mm.classList.contains('open')&&mm.querySelector('.oq-sm-btn'));
     out[L]=opened; if(mm) mm.classList.remove('open');
   });
   return out; });
 ok(['e','f','g','h'].every(L=>r3[L]===true), '🚨 [3] E F G H open the oscillator menu (mute / solo) — the regex said [A-D]', JSON.stringify(r3));

 // ── [4] exclusive solo reaches the second bank ──
 const src4=fs.readFileSync('Source/ui/public/index.html','utf8');
 const i4=src4.indexOf("if (suf === 'SOLO' && nv >= 0.5)");
 ok(i4>0 && /\['A','B','C','D','E','F','G','H'\]/.test(src4.slice(i4,i4+260)),
    '[4] exclusive solo un-solos all EIGHT oscillators, not the first four');

 // ══ THE CHOP PAGE ════════════════════════════════════════════════════════════════════════════
 await p.evaluate(()=>{ const m=document.getElementById('mix-btn'); if(m) m.click(); }); await sleep(1400);
 await p.evaluate(fake); await sleep(800);
 // sixteen real chops, the state he screenshots
 await p.evaluate(async()=>{
   const mk=()=>JSON.stringify({slices:Array.from({length:16},(_,i)=>({start:i*3000,end:(i+1)*3000,pitch:0,volume:1,attackMs:1,decayMs:0,sustainLevel:1,releaseMs:20,reverse:false,warpMode:0,stretchRatio:1}))});
   const br=window.Juce, orig=br.getNativeFunction;
   const wrapped=(n)=>((n==='getSlicesJson'||n==='gridSliceSlices')?(()=>Promise.resolve(mk())):orig(n));
   Object.defineProperty(window,'Juce',{configurable:true,value:Object.assign({},br,{getNativeFunction:wrapped})});
   const q=document.querySelectorAll('#ti-mode-toggle .ti-mode-pill'); if(q[1]) q[1].dispatchEvent(new MouseEvent('click',{bubbles:true}));
   await new Promise(r=>setTimeout(r,500));
   const g=document.querySelector('#ti-grid-row .ti-grid-pill[data-n="16"]')||document.querySelector('#ti-grid-row .ti-grid-pill[data-n="8"]');
   if(g) g.dispatchEvent(new MouseEvent('click',{bubbles:true}));
   await new Promise(r=>setTimeout(r,900));
 }); await sleep(800);

 // ── [5] the library cluster clears the Slices pill, with the count badge SHOWING ──
 const r5=await p.evaluate(()=>{ const R=e=>{const r=e.getBoundingClientRect();return{x:+r.x.toFixed(1),r:+r.right.toFixed(1),w:+r.width.toFixed(1)};};
   const lib=document.getElementById('ti-lib'), sl=document.getElementById('ti-slices-btn');
   const cnt=document.querySelector('#ti-slices-btn .ti-slices-count');
   return {lib:R(lib), slices:R(sl), gap:+(lib.getBoundingClientRect().left-sl.getBoundingClientRect().right).toFixed(1),
           countShown:!!(cnt&&cnt.getClientRects().length), countText:cnt?cnt.textContent:null}; });
 ok(r5.countShown && r5.gap>=12, '[5] the library cluster clears the Slices pill by '+r5.gap+' px with 16 chops loaded', JSON.stringify(r5));

 // ── [6] 🚨 NOTHING IS FILLED AND NOTHING GLOWS, IN THE STATES THAT SHOW IT ──
 const r6=await p.evaluate(()=>{
   const purple=s=>{ if(/gradient/.test(s||'')) return /(139,\s*92|167,\s*139|124,\s*58|183,\s*148|8[bB]5[cC]|[aA]78[bB])/.test(s);
     const m=/rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?\)/.exec(s||''); if(!m) return false;
     const r=+m[1],g=+m[2],bl=+m[3],a=m[4]===undefined?1:+m[4]; return a>0.03 && bl>r && r>g && bl>70; };
   // force every state Max named
   const pads=[...document.querySelectorAll('.ti-layer-pad')];
   pads.forEach(e=>{ e.classList.add('playing'); });
   if(pads[0]) pads[0].classList.add('active');
   const sb=document.getElementById('ti-slices-btn'); if(sb){ sb.classList.add('sel'); }
   const cnt=document.querySelector('#ti-slices-btn .ti-slices-count'); if(cnt) cnt.classList.add('sel');
   [...document.querySelectorAll('#ti-slice-overlays .ti-slice-body')].forEach(e=>e.classList.add('sel'));
   const drawer=document.getElementById('ti-slices-btn'); if(drawer) drawer.dispatchEvent(new MouseEvent('click',{bubbles:true}));
   [...document.querySelectorAll('.ti-grid-pill,.ti-submode-pill')].forEach(e=>e.classList.add('active'));
   const bad=[], glow=[];
   /* the SHAPES he named: "boxes" — every pill, pad, button and chop body on the chop engine and
      in its menus. A meter's fill and a tooltip's drop shadow are not boxes and are not in scope. */
   const SHAPES='.ti-mode-pill,.ti-play-pill,.ti-layer-pad,#ti-arm,#ti-slices-btn,#ti-slices-btn *,'
     +'.ti-grid-pill,.ti-submode-pill,.ti-action-btn,.ti-lib-nav,'
     +'#ti-slice-overlays .ti-slice-body,#ti-chop-panel .ov-mode,#ti-chop-panel .scan-pill,#ti-chop-panel .ov-all,'
     +'#mix-panel .trigger-pill,#mix-panel .trigger-btn,#mix-panel .trigger-toggle,#mix-panel .mix-strip-btn,'
     +'#mix-panel .stem-buttons > button,#mix-panel .stem-all-row > button';
   document.querySelectorAll(SHAPES).forEach(e=>{
     const r=e.getBoundingClientRect(); if(r.width<4||r.height<4) return;
     const cs=getComputedStyle(e); const id=(e.id?'#'+e.id:'')+'.'+[...e.classList].join('.');
     const bg=(cs.backgroundImage&&cs.backgroundImage!=='none')?cs.backgroundImage:cs.backgroundColor;
     if(purple(bg)) bad.push(id+' :: '+bg.slice(0,60));
     if(cs.boxShadow&&cs.boxShadow!=='none'&&!/inset/.test(cs.boxShadow)&&purple(cs.boxShadow)) glow.push(id+' :: '+cs.boxShadow.slice(0,60));
   });
   const cc=cnt?getComputedStyle(cnt).color:null;
   return {bad:[...new Set(bad)], glow:[...new Set(glow)], countColor:cc}; });
 ok(r6.bad.length===0 && r6.glow.length===0,
    '🚨 [6] no purple-FILLED box and no purple glow in ANY selected / playing / open state',
    'fills '+JSON.stringify(r6.bad)+'  glows '+JSON.stringify(r6.glow));
 ok(r6.countColor==='rgb(255, 255, 255)', '[7] the Slices count stays WHITE while a selection is live', String(r6.countColor));

 // ── [8] the ALL pill is a pill, and it is nowhere near the X ──
 const r8=await p.evaluate(async()=>{
   const bodies=[...document.querySelectorAll('#ti-slice-overlays .ti-slice-body')];
   if(bodies[2]) bodies[2].dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:300,clientY:300}));
   await new Promise(r=>setTimeout(r,500));
   const a=document.getElementById('ti-chop-all'), x=document.getElementById('ti-chop-close');
   if(!a||!x) return {err:'no ALL or no close'};
   const ra=a.getBoundingClientRect(), rx=x.getBoundingClientRect();
   const overlap=!(ra.right<=rx.left||ra.left>=rx.right||ra.bottom<=rx.top||ra.top>=rx.bottom);
   const nm=document.querySelector('#ti-chop-panel .ov-head .name');
   const nu=document.getElementById('ti-chop-num');
   const hdr={ text:nm?nm.textContent:null, numTxt:nu?nu.textContent:null,
               numColor:nu?getComputedStyle(nu).color:null, nameColor:nm?getComputedStyle(nm).color:null,
               numWeight:nu?getComputedStyle(nu).fontWeight:null, pillTxt:a.textContent };
   const off=document.querySelector('#ti-chop-panel .ov-mode');
   const alignDx=+(ra.left-off.getBoundingClientRect().left).toFixed(2);
   const smaller=ra.height < off.getBoundingClientRect().height;
   const cs=getComputedStyle(a);
   a.dispatchEvent(new MouseEvent('click',{bubbles:true}));
   await new Promise(r=>setTimeout(r,400));
   const on=getComputedStyle(a);
   const pn=document.getElementById('ti-chop-panel');
   return {hdr, overlap, alignDx, smaller, h:+ra.height.toFixed(1), gap:+(rx.left-ra.right).toFixed(1), radius:cs.borderRadius, bgRest:cs.backgroundColor,
           bgOn:on.backgroundColor, borderOn:on.borderColor, colorOn:on.color,
           multi:!!(pn&&pn.classList.contains('multi'))}; });
 ok(!r8.err && r8.overlap===false && r8.multi===true && /^rgba\(0, 0, 0, 0\)$/.test(r8.bgOn||''),
    '[8] the ALL pill is clear of the X, and clicking it selects every chop with an OUTLINE, not a fill',
    JSON.stringify(r8));
 /* tp61b — Max: "I want ALL a little smaller and right over the left box side of the Off box. It
    needs to line up from bottom to top." One vertical line, and it must stay one. */
 ok(!r8.err && Math.abs(r8.alignDx)<0.51 && r8.smaller===true,
    '[8b] and its LEFT EDGE lands on the Off pill\'s, smaller than it', 'dx '+r8.alignDx+' px, height '+r8.h);
 /* tp61b — Max: "make the 'all 16' white, no purple on purple, and forget all capital letters —
    use proper stuff, thin white preferably. Same with the All button, no capitals lol." */
 const H=r8.hdr||{};
 const caps=t=>/[A-Z]{2,}/.test(String(t||''));
 const white=c=>{ const m=/rgba?\((\d+),\s*(\d+),\s*(\d+)/.exec(c||''); return !!m && +m[1]>235 && +m[2]>235 && +m[3]>235; };
 ok(!caps(H.text) && !caps(H.pillTxt) && white(H.numColor) && white(H.nameColor) && +H.numWeight<=300,
    '[8c] the header and the pill are proper case, thin, and WHITE — no purple on purple, no shouting',
    JSON.stringify(H));
 /* tp61b — Max: "why does 'Chop All 16' look weird. Just make it say 'All Chops' whenever all is
    selected, and just say Chop 7 etc. when one is selected." And a PARTIAL selection is neither. */
 const r8d=await p.evaluate(async()=>{
   /* the two halves are separate spans with a CSS gap between them (margin-left on .num), so
      textContent has no space in it — read them as the phrase they draw. */
   const nm=()=>[...document.querySelectorAll('#ti-chop-panel .ov-head .name > span')].map(e=>e.textContent.trim()).join(' ');
   const out={};
   window.__tiChopSel && window.__tiChopSelectAll && window.__tiChopSelectAll();
   await new Promise(r=>setTimeout(r,300)); out.all=nm();
   window.__tiChopSetSel ? window.__tiChopSetSel([0,1,2,3,4]) : null;
   await new Promise(r=>setTimeout(r,300)); out.five=nm();
   const bodies=[...document.querySelectorAll('#ti-slice-overlays .ti-slice-body')];
   if(bodies[6]) bodies[6].dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:300,clientY:300}));
   await new Promise(r=>setTimeout(r,500)); out.one=nm();
   return out; });
 ok(r8d.all==='All Chops' && r8d.one==='Chop 7' && r8d.five==='5 Chops',
    '[8d] the header reads "All Chops", "Chop 7", and "5 Chops" for a partial selection — one phrase, not a formula',
    JSON.stringify(r8d));

 // ── [9] capture OFF greys the stems out ──
 const r9=await p.evaluate(async()=>{
   /* ⚠️ getComputedStyle returns a LIVE object: read the numbers OUT of it before the class goes,
      or the bar measures the state it just undid. (It did, on the first run of this file.) */
   const a=document.getElementById('mix-stem-area'), tr=document.getElementById('mix-trigger-area');
   document.body.classList.add('ti-capture-off'); await new Promise(r=>setTimeout(r,600));   /* the status line FADES in (tp57) — read it settled, not mid-transition */
   const btn=a.querySelector('.stem-buttons'), row=a.querySelector('.stem-all-row'), st=a.querySelector('.stem-status');
   const offOp=+getComputedStyle(btn).opacity, offPe=getComputedStyle(btn).pointerEvents,
         offRow=+getComputedStyle(row).opacity, trOp=+getComputedStyle(tr).opacity,
         says=getComputedStyle(st,'::after').content||'', stOp=+getComputedStyle(st).opacity;
   document.body.classList.remove('ti-capture-off'); await new Promise(r=>setTimeout(r,200));
   const onOp=+getComputedStyle(btn).opacity, onPe=getComputedStyle(btn).pointerEvents;
   return {off:{op:offOp, pe:offPe, row:offRow}, triggerOp:trOp, says, stOp, on:{op:onOp, pe:onPe}}; });
 /* tp63 — REVERSED: no caption. Max: "take away where it says capture is off no stems — forget captions and
    breadcrumbs." The grey is the message. The bar now requires the caption to be ABSENT. */
 ok(r9.off.op<0.5 && r9.off.row<0.5 && r9.off.pe==='none' && r9.triggerOp===1 && r9.on.op===1 && r9.on.pe!=='none'
    && !/Capture is off/.test(r9.says),
    '[9] capture off greys the stem BUTTONS with NO caption, and leaves the layer page alone', JSON.stringify(r9));

 ok(errs.length===0, '[10] the page threw nothing', errs.join(' | '));
 console.log('\n  '+pass+' passed, '+fail+' failed\n');
 await b.close(); process.exit(fail?1:0);})();
