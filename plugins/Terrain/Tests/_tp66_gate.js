// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp66 — ONE WAVEFORM, AND THE AUDIO PICTURES KEEP MOVING WITH MOTION OFF.
//
//    node Tests/_tp66_gate.js          (look at it too: node Tests/_shot66.js <dir> → w66-hero/osc/noise.png)
//
//  [0] the renderer exists (__tiWave / __tiWaveGeom / __tiWaveSvgHtml) and every sample painter calls it;
//      none of the old palettes survive (the OSC slot's purple glow, the hero's 245,243,255, the noise's .06)
//  [1] 🚨 the same peaks through two boxes of the same size = the same pixels (one law, bit for bit)
//  [2] 🚨 decimation, not nearest-bin: a spike in every other bin is a full-height column everywhere
//  [3] a chop tile is scaled by the whole sample's peak (peakRef), never its own
//  [4] the hero and an OSC slot both paint through the renderer (drawn, not just declared)
//  [5] motion off: the oscilloscope is not parked (editor text), the noise wave view follows `act` unscaled
//      (page text) and its follower is on the canvas with the motion envelope at zero
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const cpp=fs.readFileSync('Source/PluginEditor.cpp','utf8');
const i0=cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0=cpp.indexOf('html = html.replace ("</body>", heroOverlay',i0);
const ov=[...cpp.slice(i0,j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m=>m[1]).join('');
const src=fs.readFileSync('Source/ui/public/index.html','utf8');
const html=src.replace('</body>',ov+'</body>');
const PAGE=path.join(require('os').tmpdir(),'tp66.html'); fs.writeFileSync(PAGE,html);
let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };

// ── [0] by text ──
const t0={ def:/window\.__tiWave = function \(ctx, w, h, min, max, o\)/.test(src) && /window\.__tiWaveGeom = function/.test(src) && /window\.__tiWaveSvgHtml = function/.test(src),
  osc:/window\.__tiWave \(x, w, h, pmin, pmax, \{\}\);/.test(src), noise:/window\.__tiWave \(x, w, h, wavePeaks\.min, wavePeaks\.max, \{ ampFrac: 0\.80 \}\);/.test(src),
  conv:/svg\.innerHTML=window\.__tiWaveSvgHtml\(mn,mx,W,H,\{ampFrac:0\.88\}\);/.test(src),
  hero:/window\.__tiWave\(ctx, rect\.width, rect\.height, state\.peaksMin, state\.peaksMax, \{ ampFrac: 0\.70, progress:/.test(ov),
  chop:/window\.__tiWave\(ctx, visualWidthCss, visualHeightCss, peaksMin, peaksMax,\s*\{ from: pStart, to: pEnd, ampFrac: 0\.70, peakRef: window\.__tiWavePeak\(peaksMin, peaksMax\), baseline: false \}\);/.test(ov),
  oldOsc:!/x\.fillStyle = 'rgba\(183,148,255,0\.07\)'; x\.fill \(\);/.test(src) && !/shadowBlur = 2\.5;   \/\/ LINE LAW: matches \.mv-stroke/.test(src) && !/function sampDraw \(cv, data\)/.test(src),
  oldHero:!/rgba\(245, 243, 255, 0\.10\)/.test(ov) && !/rgba\(245, 243, 255, 0\.92\)/.test(ov),
  oldNoise:!/x\.fillStyle = 'rgba\(255,255,255,0\.06\)'; x\.fill \(\);/.test(src) && !/stroke="rgba\(255,255,255,0\.82\)" stroke-width="1\.1"/.test(src) };
ok(Object.values(t0).every(Boolean), '[0] the renderer exists and the five sample painters (hero, chop tile, OSC slot, noise wave, conv IR) call it; the old palettes are gone', JSON.stringify(t0));

(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:960,height:800,deviceScaleFactor:2});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();'+`
   (function(){ const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>{ if(window.__fakeNat&&window.__fakeNat[n]) return (...a)=>Promise.resolve(window.__fakeNat[n](...a)); return orig.getNativeFunction(n); } });
     Object.defineProperty(window,'Juce',{configurable:true,get(){return wrap;},set(){}}); })();
   window.__params.SYN_NOISE_ON=1;   /* the noise view reads its ON once, at boot (getP) — the follower head only draws while on */
   /* and its viz MODE once, at boot: 2 = the wave view; the sample's envelope and the follower come from these */
   (function(){ const N=220, mn=[], mx=[]; for(let i=0;i<N;i++){ const e=0.6*Math.exp(-i/70); mx.push(e); mn.push(-e); }
     window.__fakeNat={ getNoiseVizMode:()=>2, getNoiseWavePeaks:()=>JSON.stringify({min:mn,max:mx}), getNoiseFollow:()=>0.37, getNoiseViz:()=>1 }; })();`);
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>document.documentElement.setAttribute('data-theme','dark'));

 // the sample every bar shares: a plucked note, 1600 min/max bins
 await p.evaluate(()=>{ const N=1600,SR=48000,len=SR*1.2,per=Math.floor(len/N),mn=[],mx=[]; let seed=7; const rnd=()=>{ seed=(seed*1103515245+12345)&0x7fffffff; return seed/0x7fffffff*2-1; };
   for(let bn=0;bn<N;bn++){ let lo=0,hi=0; for(let k=0;k<per;k+=4){ const t=(bn*per+k)/SR, env=Math.exp(-t*2.6)*(t<0.004?t/0.004:1);
     const v=env*(0.55*Math.sin(2*Math.PI*220*t)+0.28*Math.sin(2*Math.PI*440*t+0.3)+0.14*Math.sin(2*Math.PI*660*t))+0.05*env*rnd(); if(v<lo) lo=v; if(v>hi) hi=v; } mn.push(lo); mx.push(hi); }
   window.__pk={mn,mx}; });

 // ── [1] one law, bit for bit ──
 const r1=await p.evaluate(()=>{ const mk=()=>{ const c=document.createElement('canvas'); c.width=800; c.height=200; const x=c.getContext('2d'); x.fillStyle='#1A1A2E'; x.fillRect(0,0,800,200); return c; };
   const a=mk(), b=mk(); window.__tiWave(a.getContext('2d'),800,200,window.__pk.mn,window.__pk.mx,{}); window.__tiWave(b.getContext('2d'),800,200,window.__pk.mn,window.__pk.mx,{});
   const da=a.getContext('2d').getImageData(0,0,800,200).data, db=b.getContext('2d').getImageData(0,0,800,200).data; let diff=0, ink=0; for(let i=0;i<da.length;i+=4){ if(da[i]!==db[i]||da[i+1]!==db[i+1]||da[i+2]!==db[i+2]) diff++; if(da[i]>120) ink++; }
   return {diff,ink}; });
 ok(r1.diff===0 && r1.ink>2000, '🚨 [1] the same peaks through two boxes of the same size are the same pixels', JSON.stringify(r1));

 // ── [2] decimation ──
 const r2=await p.evaluate(()=>{ const N=1600,mn=[],mx=[]; for(let i=0;i<N;i++){ const s=(i&1)?1:0; mx.push(s); mn.push(-s); }
   const g=window.__tiWaveGeom(mn,mx,800,200,{}); let lo=1e9; for(let c=0;c<g.cols;c++) lo=Math.min(lo,g.top[c]); return {cols:g.cols,lowestColumn:+lo.toFixed(3),scale:g.scale}; });
 ok(r2.lowestColumn>0.95, '🚨 [2] a spike in every other bin is a full-height column EVERYWHERE — the bins that land in a column are folded (min of mins, max of maxes), never sampled nearest-bin', JSON.stringify(r2));

 // ── [3] a chop is the hero's picture ──
 const r3=await p.evaluate(()=>{ const mn=window.__pk.mn, mx=window.__pk.mx; const whole=window.__tiWaveGeom(mn,mx,800,200,{ampFrac:0.7}); const quiet=window.__tiWaveGeom(mn,mx,200,200,{from:1200,to:1600,ampFrac:0.7,peakRef:window.__tiWavePeak(mn,mx)}); const own=window.__tiWaveGeom(mn,mx,200,200,{from:1200,to:1600,ampFrac:0.7});
   return {wholeScale:+whole.scale.toFixed(3), tileScale:+quiet.scale.toFixed(3), ownScale:+own.scale.toFixed(3), tileAmpEqualsWhole:Math.abs(quiet.amp-whole.amp)<1e-6}; });
 ok(r3.tileAmpEqualsWhole && r3.ownScale>=r3.tileScale, '[3] a chop tile scaled by the WHOLE sample\'s peak draws at the hero\'s amplitude (its own peak would pump a quiet tail up)', JSON.stringify(r3));

 // ── [4] the hero and an OSC slot are drawn through it ──
 const r4=await p.evaluate(async()=>{ let calls=0; const orig=window.__tiWave; window.__tiWave=function(){ calls++; return orig.apply(this,arguments); };
   document.getElementById('mix-btn').click(); await new Promise(r=>setTimeout(r,800));
   window.onSampleLoaded({filename:'PLUCK 220.wav',sampleRate:48000,lengthSamples:57600,numChannels:2,peaksMin:window.__pk.mn,peaksMax:window.__pk.mx}); await new Promise(r=>setTimeout(r,500));
   const hc=document.getElementById('waveform-canvas'); const hx=hc.getContext('2d'); const hd=hx.getImageData(0,0,hc.width,Math.max(1,hc.height)).data; let hInk=0; for(let i=0;i<hd.length;i+=4) if(hd[i+3]>60&&hd[i]>120) hInk++;
   const heroCalls=calls;
   window.setActivePanel('syn'); await new Promise(r=>setTimeout(r,600));
   try{ const d=document.getElementById('osc-a-device'); d.classList.remove('osc-off'); d.classList.add('engine-sample'); window.__setSynParam('SYN_OSC_A_ENGINE', 2/7); }catch(e){}   /* the slot shows for the Sample engine */
   await new Promise(r=>setTimeout(r,300));
   window.onOscSampleLoaded('a',JSON.stringify({filename:'PLUCK 220.wav',sampleRate:48000,lengthSamples:57600,numChannels:2,peaksMin:window.__pk.mn,peaksMax:window.__pk.mx})); await new Promise(r=>setTimeout(r,700));
   const oc=document.querySelector('#osc-a-device canvas.samp-wave'); let oInk=0; if(oc&&oc.width){ const od=oc.getContext('2d').getImageData(0,0,oc.width,oc.height).data; for(let i=0;i<od.length;i+=4) if(od[i+3]>60&&od[i]>120) oInk++; }
   window.__tiWave=orig; return {heroCalls,calls,hInk,oInk,hero:hc.width+'x'+hc.height,osc:oc?oc.width+'x'+oc.height:null}; });
 ok(r4.heroCalls>=1 && r4.calls>r4.heroCalls && r4.hInk>500 && r4.oInk>100, '[4] the Chop hero and the OSC · Sample slot both PAINT through the renderer (ink on both canvases, the renderer called for each)', JSON.stringify(r4));

 // ── [5] motion off: the audio pictures keep moving ──
 const t5={ scope:/oscScopeActive\.load\(std::memory_order_relaxed\) && ! feedStale;\n\s+\/\/ tp66 — the scope is AUDIO/.test(cpp) && !/&& ! uiStatic;   \/\/ tp62 — with motion off the scope parks/.test(cpp),
   noiseAct:/var actD = \(vizMode === 2\) \? act : act \* __me;/.test(src) && /if \(__rest\) \{ if \(vizMode !== 2\) act = 0;/.test(src) };
 ok(t5.scope && t5.noiseAct, '[5] motion off: the oscilloscope is not parked by the editor, and the noise wave view follows `act` unscaled by the motion envelope (the particle cloud still rests)', JSON.stringify(t5));
 const r5=await p.evaluate(async()=>{ document.body.classList.add('ti-motion-off'); try{ if(window.__tiMot){ window.__tiMot.e=0; window.__tiMot.m=0; window.__tiMot.live=false; } }catch(e){}
   const N=220,src=window.__pk,mn=[],mx=[]; for(let c=0;c<N;c++){ const b0=Math.floor(c*1600/N),b1=Math.floor((c+1)*1600/N); let lo=0,hi=0; for(let k=b0;k<b1;k++){ if(src.mn[k]<lo) lo=src.mn[k]; if(src.mx[k]>hi) hi=src.mx[k]; } mn.push(lo); mx.push(hi); }
   window.__fakeNat={ getNoiseVizMode:()=>2, getNoiseWavePeaks:()=>JSON.stringify({min:mn,max:mx}), getNoiseFollow:()=>0.37, getNoiseViz:()=>1 };
   const cv=document.getElementById('noise-viz-cv'); if(!cv) return {err:'no canvas'}; await new Promise(r=>setTimeout(r,2200));
   const w=cv.width,h=cv.height,d=cv.getContext('2d').getImageData(0,0,w,h).data;
   /* the follower spans mid ± amp·1.06 — rows in the top tenth of the box are inside the head and OUTSIDE the wave body at x = 37 % (the pluck's tail) */
   const col=x=>{ let s=0; for(let y=Math.round(h*0.06);y<Math.round(h*0.14);y++){ const i=(y*w+Math.round(x))*4; s+=d[i+3]; } return s; };
   const px=0.37*w, at=col(px), near=Math.max(col(px-8),col(px+8)); let ink=0; for(let i=3;i<d.length;i+=4) if(d[i]>40) ink++;
   document.body.classList.remove('ti-motion-off'); return {w,h,ink,atFollower:at,beside:near,polls:{viz:window.__natCount.getNoiseViz|0,follow:window.__natCount.getNoiseFollow|0}}; });
 ok(!r5.err && r5.ink>200 && r5.atFollower>r5.beside+40, '[5b] with the motion envelope at ZERO the noise wave view still shows its sample AND its follower head (the column at the head is brighter than its neighbours)', JSON.stringify(r5));

 ok(errs.length===0,'[6] the page threw nothing', errs.slice(0,3).join(' | ')||'clean');
 await b.close(); console.log('\n  '+pass+' passed, '+fail+' failed\n'); process.exit(fail?1:0);
})();
