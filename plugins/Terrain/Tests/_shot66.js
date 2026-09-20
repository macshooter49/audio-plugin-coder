// tp66 — LOOK AT IT: the hero, an OSC slot and the noise wave view, the same synthetic sample, one style
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const cpp=fs.readFileSync('Source/PluginEditor.cpp','utf8');
const i0=cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0=cpp.indexOf('html = html.replace ("</body>", heroOverlay',i0);
const ov=[...cpp.slice(i0,j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m=>m[1]).join('');
const html=fs.readFileSync('Source/ui/public/index.html','utf8').replace('</body>',ov+'</body>');
const PAGE=path.join(require('os').tmpdir(),'s66.html'); fs.writeFileSync(PAGE,html);
const OUT=process.argv[2]||'/tmp';
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:960,height:800,deviceScaleFactor:2});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();'+`
   (function(){ const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>{ if(window.__fakeNat&&window.__fakeNat[n]) return (...a)=>Promise.resolve(window.__fakeNat[n](...a)); return orig.getNativeFunction(n); } });
     Object.defineProperty(window,'Juce',{configurable:true,get(){return wrap;},set(){}}); })();`);
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>document.documentElement.setAttribute('data-theme','dark'));
 // a REAL-looking sample: a plucked note (decaying harmonic burst with a little noise), 1600 min/max bins
 const peaks=await p.evaluate(()=>{ const N=1600, SR=48000, len=SR*1.2, per=Math.floor(len/N), mn=[],mx=[]; let seed=7; const rnd=()=>{ seed=(seed*1103515245+12345)&0x7fffffff; return seed/0x7fffffff*2-1; };
   for(let bnum=0;bnum<N;bnum++){ let lo=0,hi=0; for(let k=0;k<per;k+=4){ const t=(bnum*per+k)/SR; const env=Math.exp(-t*2.6)*(t<0.004?t/0.004:1);
     const v=env*(0.55*Math.sin(2*Math.PI*220*t)+0.28*Math.sin(2*Math.PI*440*t+0.3)+0.14*Math.sin(2*Math.PI*660*t)+0.06*Math.sin(2*Math.PI*1320*t))+0.05*env*rnd(); if(v<lo) lo=v; if(v>hi) hi=v; } mn.push(lo); mx.push(hi); }
   window.__pk={mn,mx}; return {n:N}; });
 // 1. the hero
 await p.evaluate(()=>document.getElementById('mix-btn').click()); await sleep(900);
 await p.evaluate(()=>{ window.onSampleLoaded({filename:'PLUCK 220.wav',sampleRate:48000,lengthSamples:57600,numChannels:2,peaksMin:window.__pk.mn,peaksMax:window.__pk.mx}); }); await sleep(600);
 const hero=await p.$('#hero'); await hero.screenshot({path:OUT+'/w66-hero.png'});
 // 2. an OSC · Sample slot (osc A on the Sample engine)
 await p.evaluate(()=>window.setActivePanel('syn')); await sleep(800);
 await p.evaluate(()=>{ try{ const d=document.getElementById('osc-a-device'); d.classList.remove('osc-off'); d.classList.add('engine-sample'); }catch(e){} try{ window.__setSynParam('SYN_OSC_A_ENGINE', 2/7); }catch(e){}
   window.onOscSampleLoaded('a',JSON.stringify({filename:'PLUCK 220.wav',sampleRate:48000,lengthSamples:57600,numChannels:2,peaksMin:window.__pk.mn,peaksMax:window.__pk.mx})); }); await sleep(900);
 const osc=await p.$('#osc-a-device'); if(osc) await osc.screenshot({path:OUT+'/w66-osc.png'});
 // 3. the noise wave view (220 columns of the same sample)
 await p.evaluate(()=>{ const N=220,src=window.__pk,mn=[],mx=[]; for(let c=0;c<N;c++){ const b0=Math.floor(c*1600/N),b1=Math.floor((c+1)*1600/N); let lo=0,hi=0; for(let k=b0;k<b1;k++){ if(src.mn[k]<lo) lo=src.mn[k]; if(src.mx[k]>hi) hi=src.mx[k]; } mn.push(lo); mx.push(hi); }
   window.__fakeNat={ getNoiseWavePeaks:()=>JSON.stringify({min:mn,max:mx}), getNoiseFollow:()=>0.37 }; });
 const nz=await p.evaluate(async()=>{ const cv=document.getElementById('noise-viz-cv'); if(!cv) return 'no canvas'; cv.dispatchEvent(new MouseEvent('click',{bubbles:true})); await new Promise(r=>setTimeout(r,1500)); return 'ok'; });
 const nzEl=await p.$('.noise-viz'); if(nzEl) await nzEl.screenshot({path:OUT+'/w66-noise.png'});
 console.log('shots', nz); await b.close(); })();
