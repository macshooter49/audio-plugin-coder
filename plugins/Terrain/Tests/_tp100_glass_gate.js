// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp100 — ONE GLASS TOP TO BOTTOM, LOOKED AT (Max: "stop relying on numbers, look at it").
//    node Tests/_tp100_glass_gate.js
//  For every glass page (MOD matrix, Settings, preset browser) it renders the real page, then
//  SAMPLES PIXELS from the rendered header band and the rendered page body, and reads the colour
//  the page sent to the native CAPTURE strip (setStripGround / setBrowserGlass — the strip is a
//  JUCE component, so this is exactly what it paints). All three must be the same colour.
//  Writes a screenshot per page to $TMPDIR/tp100_<page>.png for a human look.
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
const PAGE=path.join(os.tmpdir(),'tp100.html'); fs.writeFileSync(PAGE,html);
let pass=0,fail=0; const ok=(c,l,d)=>{ (c?pass++:fail++); console.log('  '+(c?'PASS':'FAIL')+'  '+l+(d?'\n        '+d:'')); };
const hex=s=>{ s=String(s).replace(/^#?(ff)?/i,''); if(s.length>6) s=s.slice(-6); return [0,2,4].map(i=>parseInt(s.substr(i,2),16)); };
const dist=(a,b)=>Math.max(Math.abs(a[0]-b[0]),Math.abs(a[1]-b[1]),Math.abs(a[2]-b[2]));
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:1});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();'+`
   (function(){ window.__stripLog=[]; const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>{
       if(n==='setStripGround'||n==='setBrowserGlass') return (...a)=>{ window.__stripLog.push([n].concat(a)); return Promise.resolve(); };
       return orig.getNativeFunction(n); } });
     Object.defineProperty(window,'Juce',{configurable:true,get(){return wrap;},set(){}}); })();`);
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>document.documentElement.setAttribute('data-theme','dark'));
 // strip colour the native component would paint right now: browser glass wins when on, else the ground
 const stripNow=()=>p.evaluate(()=>{ const L=window.__stripLog; let bg=null, gr=null;
   for(const e of L){ if(e[0]==='setBrowserGlass') bg=e; if(e[0]==='setStripGround') gr=e; }
   if(bg && +bg[1]===1) return String(bg[2]); return gr?String(gr[gr.length-1]):null; });
 async function sample(name){
   await p.evaluate(()=>{ if(window.__tiStripKick) window.__tiStripKick(); }); await sleep(1300);
   const shot=path.join(os.tmpdir(),'tp100_'+name+'.png'); await p.screenshot({path:shot});
   // sample pixels by drawing the screenshot into a canvas (what a human sees)
   const px=await p.evaluate(async(src,pts)=>{ const im=new Image(); im.src=src; await im.decode(); const c=document.createElement('canvas'); c.width=im.width; c.height=im.height;
       const x=c.getContext('2d'); x.drawImage(im,0,0); return pts.map(([a,b])=>Array.from(x.getImageData(a,b,1,1).data).slice(0,3)); },
     'data:image/png;base64,'+fs.readFileSync(shot).toString('base64'),[[250,10],[570,10],[60,610],[760,600]]);
   return {px, strip: await stripNow(), shot};
 }
 const pages=[
   ['mod',      async()=>{ await p.evaluate(()=>{ const m=document.getElementById('mod-btn'); if(m) m.click(); }); }],
   ['settings', async()=>{ await p.evaluate(()=>{ const s=document.getElementById('syn-btn'); if(s) s.click(); }); await sleep(400); await p.evaluate(()=>window.__st&&window.__st.open(true)); }],
   ['browser',  async()=>{ await p.evaluate(()=>{ window.__st&&window.__st.open(false); if(window.__openPresetBrowser) window.__openPresetBrowser(); }); }],
 ];
 for(const [name,open] of pages){
   await open(); await sleep(900);
   const r=await sample(name);
   const [h1,h2,b1,b2]=r.px, s=r.strip?hex(r.strip):null;
   const d_hb=Math.max(dist(h1,b1),dist(h2,b2)), d_hs=s?dist(h1,s):999;
   ok(d_hb<=6, `[${name}] header band and page body are the SAME glass (max Δ ${d_hb})`, `header ${JSON.stringify(h1)} ${JSON.stringify(h2)} · body ${JSON.stringify(b1)} ${JSON.stringify(b2)}`);
   ok(s && d_hs<=6, `[${name}] the native CAPTURE strip is sent that same colour (Δ ${d_hs})`, `strip #${r.strip} = ${JSON.stringify(s)} · shot ${r.shot}`);
 }
 ok(errs.length===0,'no page errors',errs.join(' | '));
 console.log(`\n  ${pass} passed, ${fail} failed`); await b.close(); process.exit(fail?1:0);
})();
