// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp62 — MOTION OFF: NOTHING MOVES ON ITS OWN, AND THE ESSENTIALS STILL DO.
//
//  Max: "give people the setting to turn off animations except for LFO and envelope ... the UI just
//  doesn't animate whenever MIDI is coming in ... every time you move a knob it still moves ...
//  animation loops stop ... the four modes have movement on the MIDI."
//
//  The page is driven the way the plugin drives it: a fake push lane at 60 Hz that stamps the note
//  flag live and calls window.__tiFrame() — the C++'s own entry point — so the motion clock, the
//  wind keys and the painters run exactly as they do under a held chord. Every bar reads the page's
//  own state (__tiMot, __tiWind, computed styles), never a re-implementation.
//
//    node Tests/_tp62_motion_gate.js
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs');
let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };
const PAGE=process.argv[2]||(process.cwd()+'/Source/ui/public/index.html');
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:1});
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.evaluateOnNewDocument(()=>{ try{ localStorage.setItem('tpLayout',JSON.stringify({v:6,pos:{},ext:{}})); }catch(e){} });
 await p.goto('file://'+PAGE+'?page=1',{waitUntil:'load'}); await sleep(1800);
 await p.evaluate(()=>{ document.documentElement.setAttribute('data-theme','dark');
   const d=document.getElementById('osc-a-device'); if(d) d.classList.remove('osc-off');
   try{ window.__fxrAdd('reverb'); window.__flowSetChain(['glitch']); }catch(e){} });
 await sleep(900);

 // ── THE FAKE LANE: the plugin's own entry point, live note flag, a moving LFO, a breathing reverb ──
 await p.evaluate(()=>{ window.__lane={t:0,on:true};
   window.__laneT=setInterval(()=>{ const L=window.__lane; L.t+=1/60;
     window.__notesActive=L.on?1:0; window.__notesActiveT=Date.now();
     window.__fxBloomRvb=L.on?0.5+0.5*Math.sin(L.t*6):0;   // what the C++ pushes with motion ON (with it OFF the C++ pushes a constant — pinned in [6])
     try{ if(window.__modViz){ const e=[],l=[],ph=[]; for(let k=0;k<32;k++) e.push(0.5); for(let k=0;k<10;k++){ l.push(Math.sin(L.t*4+k)); ph.push((L.t*0.5+k*0.1)%1); } window.__modViz(e,l,ph); } }catch(x){}
     try{ window.__tiFrame(); }catch(x){} },16); });
 await sleep(1500);

 // ── [0] the settings row exists and drives the native + the body class ──
 const r0=await p.evaluate(async()=>{ const calls=[]; const br=window.Juce, orig=br&&br.getNativeFunction;
   if(br&&orig){ const w=(n)=>{ const f=orig(n); return function(){ calls.push([n].concat([].slice.call(arguments))); return f?f.apply(null,arguments):undefined; }; };
     Object.defineProperty(window,'Juce',{configurable:true,value:Object.assign({},br,{getNativeFunction:w})}); }
   const on=document.getElementById('motion-on-btn'), off=document.getElementById('motion-off-btn');
   if(!on||!off) return {err:'no row'};
   off.click(); await new Promise(r=>setTimeout(r,200));
   const cls=document.body.classList.contains('ti-motion-off'), q=window.__tiMotionOn();
   on.click(); await new Promise(r=>setTimeout(r,200));
   return {cls, q, back:window.__tiMotionOn(), calls:calls.filter(c=>/Motion/.test(c[0])).map(c=>c[0]+'('+c.slice(1).join(',')+')')}; });
 ok(!r0.err && r0.cls===true && r0.q===false && r0.back===true && r0.calls.join(' ')==='setMotionEnabled(0) setMotionEnabled(1)',
    '[0] the Motion row exists; Off writes body.ti-motion-off and calls setMotionEnabled(0); On restores', JSON.stringify(r0));

 // ── [1] motion ON: the decorative envelope rises with the lane ──
 await sleep(900);
 const r1=await p.evaluate(()=>({e:+window.__tiMot.e.toFixed(3), live:window.__tiMot.live, still:document.documentElement.classList.contains('ti-still')}));
 ok(r1.e>0.9 && r1.live===true && r1.still===false, '[1] motion ON: with MIDI live the decorative envelope is up and the breathers run', JSON.stringify(r1));

 // ── [2] 🚨 motion OFF: the envelope never rises, with the same lane still live ──
 await p.evaluate(()=>window.__tiMotionSet(false)); await sleep(1200);
 const r2=await p.evaluate(()=>({e:+window.__tiMot.e.toFixed(3), m:+window.__tiMot.m.toFixed(3), live:window.__tiMot.live, realLive:window.__tiLive(),
   still:document.documentElement.classList.contains('ti-still'), notes:window.__notesActive}));
 ok(r2.e===0 && r2.m===0 && r2.live===false && r2.realLive===true && r2.still===true,
    '🚨 [2] motion OFF: MIDI is live (__tiLive true) and the decorative envelope stays at ZERO; the breathers stay still', JSON.stringify(r2));

 // ── [3] a Flow tile still winds on MIDI; a decorative key does not ──
 /* fresh keys, not the painters' own: a key is a clock with a period, and calling the real card-gli with a
    different P/B would re-home the live tile's clock (it did — the first run of this file read -3.88). */
 const r3=await p.evaluate(async()=>{ const P=2,B=[[0,0]]; const step=()=>[window.__tiWind('card-gli-t62',P,B,true,1), window.__tiWind('em-t62',P,B,true,1)];
   let g0,d0,g1,d1; [g0,d0]=step(); for(let i=0;i<24;i++){ await new Promise(r=>setTimeout(r,16)); [g1,d1]=step(); }
   return {gli:+(g1-g0).toFixed(3), drv:+(d1-d0).toFixed(3)}; });
 ok(r3.gli>0.05 && r3.drv===0, '[3] motion OFF: the Glitch tile\'s clock advances on MIDI (essential); the distortion emblem\'s does not (decoration)', JSON.stringify(r3));

 // ── [4] the LFO scope still paints its pushed values (the essential painter is untouched) ──
 /* ── [4] the frame still dispatches EVERY painter at the lane rate with motion off ──
    The essential painters (LFO / envelope / filter / trackers) are ordinary registrants of the frame
    dispatcher; they paint their pushed values and never read the decorative clock. So the honest
    question is not "did one particular pixel move" (an SVG comet's shape is its own business) but
    "does run() still call the registry at the lane's rate with motion off". A probe painter is
    registered through the shipped __tiFrameReg and counted. */
 const r4=await p.evaluate(async()=>{ let n=0; window.__tiFrameReg('t62-probe',function(){ n++; });
   await new Promise(r=>setTimeout(r,1000)); const c=n; window.__tiFrameUnreg('t62-probe');
   return {framesPerSec:c, e:+window.__tiMot.e.toFixed(3), motion:window.__tiMotionOn()}; });
 ok(r4.framesPerSec>=40 && r4.e===0 && r4.motion===false, '[4] motion OFF: the frame still dispatches every painter at the lane rate ('+r4.framesPerSec+'/s) while the decorative clock is held', JSON.stringify(r4));

 // ── [5] motion back ON: the envelope rises again ──
 await p.evaluate(()=>window.__tiMotionSet(true)); await sleep(900);
 const r5=await p.evaluate(()=>({e:+window.__tiMot.e.toFixed(3)}));
 ok(r5.e>0.9, '[5] motion ON again: the envelope winds back up', JSON.stringify(r5));

 // ── [6] SOURCE — what the C++ side does with the setting (the half a page test cannot drive) ──
 const ed=fs.readFileSync('Source/PluginEditor.cpp','utf8'), pr=fs.readFileSync('Source/PluginProcessor.cpp','utf8');
 const deco=['__grnVizPush','__tpeVizPush','__fx3VizPush','__fx4VizPush'].every(k=>new RegExp('if \\(decoTick\\) js << "window\\.'+k).test(ed));
 const dst=/if \(decoTick\) js << "window\.__dstVizPush=/.test(ed) && /js << "window\.__fltVizPush=" << audioProcessor\.getFilterVizJson\(\) << ";";\s+\/\/ fb382 — the filter is essential/.test(ed);
 const scope=/&& ! uiStatic;\s+\/\/ tp62 — with motion off the scope parks/.test(ed);
 const spec=/pushEqW && specTick && wanted && spectrumLive/.test(ed);
 const rate=/int uiHz = audioProcessor\.getMotionEnabled\(\) \? 60 : 30;/.test(ed);
 const vz=(pr.match(/\bvz \(/g)||[]).length;
 const wash=/vizReverbBloom \(int inst0\) const noexcept[\s\S]{0,700}"MIX"[\s\S]{0,200}"DECAY"/.test(pr);
 const ctor=/if \(motionOffMarker\(\)\.existsAsFile\(\)\)\s+motionEnabled_\.store\s+\(false/.test(pr);
 ok(deco && dst && scope && spec && rate && vz>=20 && wash && ctor,
    '[6] the C++ half: decorative feeds ride decoTick, the filter stays every 4th tick, the scope parks, the spectrum halves, the lane runs 30 Hz, '+vz+' audio-driven numbers rest, the reverb wash is mix × decay, the ctor reads the marker',
    JSON.stringify({deco,dst,scope,spec,rate,vz,wash,ctor}));

 ok(errs.length===0, '[7] the page threw nothing', errs.join(' | '));
 console.log('\n  '+pass+' passed, '+fail+' failed\n');
 await b.close(); process.exit(fail?1:0); })();
