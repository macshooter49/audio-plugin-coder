// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp101 — THE MOD MATRIX HAS NO ROUTE MAX (the page).
//    node Tests/_tp101_mod_routes_gate.js
//  Max: the header read "4 routes · 32 max"; he builds past 32. Renders the real page, opens the
//  MOD page and presses "+" (header) 30 times and "+ Add route" (the row) 30 times, with a native twin that remembers what setSynthMod
//  wrote and answers getSynthMod with it (the processor's round trip, so the mirror's prune runs
//  against the truth). Bars:
//    1  every press adds a row: 60 rows (the old page stopped at 32 — and, once all ten LFOs were in
//       use, the Add row proposed LFO 10 → Cutoff again and again and the dedupe ate it: HEAD stops at 10)
//    2  the count label is "60 routes" — no "max" anywhere in the header
//    3  "+ Add route" is still showing at 60
//    4  the engine was sent all 60 (setSynthMod's last payload), every (source, dest) pair distinct
//    5  they survive the 2.5 s getSynthMod poll (the mirror does not prune them)
//    6  a route added past 32 by a knob's own path (window.__mmApi.add) lands too
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path'),os=require('os');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const cpp=fs.readFileSync('Source/PluginEditor.cpp','utf8');
const i0=cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0=cpp.indexOf('html = html.replace ("</body>", heroOverlay',i0);
const ov=[...cpp.slice(i0,j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m=>m[1]).join('');
const html=fs.readFileSync(process.env.TP101_HTML||'Source/ui/public/index.html','utf8').replace('</body>',ov+'</body>');
const PAGE=path.join(os.tmpdir(),'tp101.html'); fs.writeFileSync(PAGE,html);
let pass=0,fail=0; const ok=(c,l,d)=>{ (c?pass++:fail++); console.log('  '+(c?'PASS':'FAIL')+'  '+l+(d?'\n        '+d:'')); };
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:1});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();'+`
   (function(){ window.__tp101Wire='[]'; const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>{
       if(n==='setSynthMod') return (js)=>{ window.__tp101Wire=String(js); return Promise.resolve(0); };
       if(n==='getSynthMod') return ()=>Promise.resolve(window.__tp101Wire);   /* NOT window.__synMod — that name is the matrix IIFE's own run-once flag */
       return orig.getNativeFunction(n); } });
     Object.defineProperty(window,'Juce',{configurable:true,get(){return wrap;},set(){}}); })();`);
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>{ const m=document.getElementById('mod-btn'); if(m) m.click(); }); await sleep(600);
 const N=60;
 // the first half through the header "+", the rest through the "+ Add route" row (the row only exists once a route does)
 for(let i=0;i<N;i++){ await p.evaluate((half)=>{ const a=half?document.getElementById('mm-add'):document.querySelector('#mm .tp-row.addr'); if(a) a.click(); }, i<N/2); }
 await sleep(300);
 const st=async()=>p.evaluate(()=>{ const rows=document.querySelectorAll('#mm-rows .tp-row:not(.addr)').length;
   const a=document.querySelector('#mm .tp-row.addr'); const cnt=document.getElementById('mm-count');
   let wire=[]; try{ wire=JSON.parse(window.__tp101Wire||'[]'); }catch(e){}
   const keys=new Set(wire.map(o=>o.s+'_'+o.d));
   return { rows, addShown: !!a && getComputedStyle(a).display!=='none', label: cnt?cnt.textContent:null,
            hdr: (document.querySelector('#mm .tb')||{}).textContent||'', wire: wire.length, distinct: keys.size,
            model: window.__mmApi&&window.__mmApi.list?window.__mmApi.list().length:-1 }; });
 const s1=await st();
 ok(s1.rows===N,'1  every press adds a row ('+N+' presses)', 'rows '+s1.rows+' · model '+s1.model);
 ok(s1.label===N+' routes' && !/max/i.test(s1.label||''),'2  the count label is "'+N+' routes", no max', 'label "'+s1.label+'"');
 ok(!/\bmax\b/i.test(s1.hdr),'2b no "max" anywhere in the MOD page toolbar', JSON.stringify((s1.hdr||'').replace(/\s+/g,' ').slice(0,120)));
 ok(s1.addShown,'3  "+ Add route" still shows at '+N);
 ok(s1.wire===N && s1.distinct===N,'4  the engine was sent all '+N+', every (source, dest) pair distinct', 'wire '+s1.wire+' · distinct '+s1.distinct);
 await sleep(3200);   // the 2.5 s getSynthMod poll + the 1.5 s idle prune window
 const s2=await st();
 ok(s2.rows===N && s2.wire===N,'5  they survive the getSynthMod poll (no prune)', 'rows '+s2.rows+' · wire '+s2.wire);
 const s3=await p.evaluate(()=>{ const api=window.__mmApi; if(!api||!api.add) return {err:'no __mmApi.add'};
   const r=api.add({env:3},64); let wire=[]; try{ wire=JSON.parse(window.__tp101Wire||'[]'); }catch(e){}
   return { added: !!r, wire: wire.length, has: wire.some(o=>o.s===102&&o.d===64) }; });
 await sleep(200);
 ok(s3.added && s3.wire===N+1 && s3.has,'6  a route past 32 from the knob path (Env 3 → Osc A Level) lands', JSON.stringify(s3));
 ok(errs.length===0,'no page errors', errs.slice(0,3).join(' | '));
 console.log('\n  '+pass+' passed, '+fail+' failed\n'); await b.close(); process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(2); });
