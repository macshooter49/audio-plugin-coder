const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const cpp=fs.readFileSync('Source/PluginEditor.cpp','utf8');
const i0=cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0=cpp.indexOf('html = html.replace ("</body>", heroOverlay',i0);
const ov=[...cpp.slice(i0,j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m=>m[1]).join('');
const html=fs.readFileSync('Source/ui/public/index.html','utf8').replace('</body>',ov+'</body>');
const PAGE=path.join(require('os').tmpdir(),'p59.html'); fs.writeFileSync(PAGE,html);
const fake=()=>{const N=1200,mn=[],mx=[];for(let i=0;i<N;i++){const e=.2+.7*Math.abs(Math.sin(i/23))*Math.abs(Math.sin(i/211));mx.push(e);mn.push(-e);}
 window.onSampleLoaded({filename:'D LOOP FOREST GUMP 140.wav',lengthSamples:192000,peaksMin:mn,peaksMax:mx,rootMidiNote:60});};
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:2});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();');
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>document.documentElement.setAttribute('data-theme','dark'));
 await p.evaluate(()=>document.getElementById('mix-btn').click()); await sleep(1300);
 await p.evaluate(fake); await sleep(700);
 // SLICE mode
 await p.evaluate(()=>{const q=document.querySelectorAll('#ti-mode-toggle .ti-mode-pill'); if(q[1]) q[1].dispatchEvent(new MouseEvent('click',{bubbles:true}));}); await sleep(600);
 // 16 real slices through the shipped door
 await p.evaluate(async()=>{
   const mk=()=>JSON.stringify({slices:Array.from({length:16},(_,i)=>({start:i*12000,end:(i+1)*12000,pitch:0,volume:1,attackMs:1,decayMs:0,sustainLevel:1,releaseMs:20,reverse:false,warpMode:0,stretchRatio:1}))});
   const bridge=window.Juce, orig=bridge.getNativeFunction;
   const wrapped=(n)=>((n==='getSlicesJson'||n==='gridSliceSlices')?(()=>Promise.resolve(mk())):orig(n));
   Object.defineProperty(window,'Juce',{configurable:true,value:Object.assign({},bridge,{getNativeFunction:wrapped})});
   const g=document.querySelector('#ti-grid-row .ti-grid-pill[data-n="16"]')||document.querySelector('#ti-grid-row .ti-grid-pill[data-n="8"]');
   if(g) g.dispatchEvent(new MouseEvent('click',{bubbles:true}));
   await new Promise(r=>setTimeout(r,900));
 }); await sleep(900);
 // ── force the leak Max is seeing: #controls visible while the chop page is open ──
 await p.evaluate(()=>{ const c=document.getElementById('controls'); if(c) c.style.display=''; }); await sleep(600);
 const geo=await p.evaluate(()=>{
   const r=s=>{const e=document.querySelector(s); if(!e) return null; const b=e.getBoundingClientRect();
     return {x:+b.x.toFixed(1),y:+b.y.toFixed(1),w:+b.width.toFixed(1),h:+b.height.toFixed(1),r:+b.right.toFixed(1)};};
   const pill=document.getElementById('ti-slices-btn'), lib=document.getElementById('ti-lib');
   const cnt=document.getElementById('ti-slices-count');
   return {
     pill:r('#ti-slices-btn'), count:r('#ti-slices-count'), countText:cnt?cnt.textContent:null,
     countShown:cnt?getComputedStyle(cnt).display!=='none':null,
     lib:r('#ti-lib'), bottomPills:r('#ti-bottom-pills'), cluster:r('#ti-bottom-right-cluster'),
     gap: (pill&&lib)? +(lib.getBoundingClientRect().left - pill.getBoundingClientRect().right).toFixed(1) : null,
     playPill:r('#ti-play-mode-toggle .ti-play-pill'),
     controlsShown: (()=>{const c=document.getElementById('controls'); return c?getComputedStyle(c).display:null;})(),
     sliceCount: (window.__tiChopSel?document.querySelectorAll('#ti-slice-overlays .ti-slice-body').length:null),
     stemStatus:(()=>{const e=document.getElementById('stem-status'); if(!e) return 'absent';
        const c=getComputedStyle(e), b=e.getBoundingClientRect();
        return {op:c.opacity, bg:c.backgroundColor, pos:c.position, txt:JSON.stringify(e.textContent),
                x:+b.x.toFixed(1),y:+b.y.toFixed(1),w:+b.width.toFixed(1),h:+b.height.toFixed(1)};})(),
     stemArea:r('#mix-stem-area'), stemBtn:r('#mix-stem-area .stem-buttons .stem-btn'),
     stemAll:r('#mix-stem-area .stem-all-row > button'), mixPanel:r('#mix-panel'),
     triggerCtx:r('#trigger-context'), layerDot:r('#trigger-context .layer-status-dot'),
     css:(()=>{const g=s=>{const e=document.querySelector(s); if(!e) return s+':absent'; const c=getComputedStyle(e);
        return s+'  w='+c.width+' maxw='+c.maxWidth+' flex='+c.flex+' pos='+c.position+' left='+c.left+' right='+c.right+' disp='+c.display;};
        return [g('#ti-bottom-right-cluster'),g('#ti-lib'),g('#ti-lib-name'),g('#ti-bottom-pills')];})(),
     markers: [...document.querySelectorAll('#ti-slice-overlays .ti-slice-marker')].slice(0,3).map(e=>{const b=e.getBoundingClientRect();
        const n=e.querySelector('*'); return {x:+b.x.toFixed(1),w:+b.width.toFixed(1),cls:e.className,txt:(e.textContent||'').trim()};}),
   };});
 console.log(JSON.stringify(geo,null,1));
 console.log('errs:',errs.slice(0,3));
 await p.screenshot({path:'/tmp/_p59_full.png'});
 await b.close();})();
