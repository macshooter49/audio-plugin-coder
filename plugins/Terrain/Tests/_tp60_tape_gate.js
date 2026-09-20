// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp60 — FIVE TAPE MODES IN THE BROWSER, AND FIVE DIFFERENT PICTURES.
//
//  Max: "where are the rest of my TAPE MODES??? we literally had 5 total and now it's three,
//  where are the other two? and why is REEl the same visualizer as the STUDIO?"
//
//  🚨 [1] IS THE ONE THAT MATTERS. `TDRAW` held two entries and the lookup ends `|| drawWire`, so
//  Reel — and every other name not in the table — drew the Studio card. A bar that only checks
//  "the table has five keys" would pass on five keys pointing at one function, so this one RENDERS
//  each mode to a canvas and requires five different pictures.
//
//    node Tests/_tp60_tape_gate.js <abs path to index.html>
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));}
                    else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:1400,height:900,deviceScaleFactor:2});
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.evaluateOnNewDocument(()=>{ try{ localStorage.setItem('tpLayout',JSON.stringify({v:6,pos:{},ext:{}})); }catch(e){} });
 await p.goto('file://'+process.argv[2]+'?page=1',{waitUntil:'load'}); await sleep(1900);
 await p.evaluate(()=>{ document.documentElement.setAttribute('data-theme','dark');
   const d=document.getElementById('osc-a-device'); if(d) d.classList.remove('osc-off'); });
 await sleep(500); await p.evaluate(()=>window.setActivePanel('tp')); await sleep(2600);

 // ── [0] THE SHELF LISTS FIVE ────────────────────────────────────────────────────────────
 const cat=await p.evaluate(()=>{ const all=window.__tpCatalog?window.__tpCatalog():[];
   return all.filter(i=>i.cat==='Tape').map(i=>i.n); });
 /* tp63 — REVERSED, ON MAX'S WORD. tp60 read "where are the rest of my TAPE MODES" as three more TYPES on the
    rack's Tape card and built them on a different engine. He had meant the three MACHINE MODULES — Reel (the
    Harmonic Sculptor), Porta, Wire — whose DSP and hero UI never left; tp43a had only deleted their browser
    entries. The shelf lists those three as modules again plus the card's two types, five entries, no prefix. */
 ok(cat.length===5 && cat.join('|')==='Reel|Porta|Wire|Studio|Cassette',
    '[0] 🚨 THE TAPE SHELF LISTS FIVE AGAIN — the three MACHINE modules tp43a unlisted, and the card\'s two types, plain names',
    JSON.stringify(cat));

 // ── [1] 🚨 FIVE DIFFERENT PICTURES, RENDERED ───────────────────────────────────────────
 const viz=await p.evaluate(()=>{
   const T=window.__tpeDraw; if(!T) return {err:'no __tpeDraw'};
   const names=['Studio','Cassette','Reel','Porta','Wire'];
   const missing=names.filter(n=>typeof T[n]!=='function');
   if(missing.length) return {err:'no drawer for '+missing.join(', ')};
   // one identical feed into every drawer, so any difference is the DRAWING
   const V={l:0.62,i:0.55,sp:0.37,pk:0.41,w:0.45,h:[0.7,0.4,0.55,0.25]};
   const sigs=names.map(n=>{
     const cv=document.createElement('canvas'); cv.width=300; cv.height=150;
     const c=cv.getContext('2d'); c.clearRect(0,0,300,150);
     try{ T[n](c,300,150,V); }catch(e){ return 'THREW '+e.message; }
     const d=c.getImageData(0,0,300,150).data;
     let ink=0, hash=0;
     for(let i=3;i<d.length;i+=4){ if(d[i]>8){ ink++; hash=(hash*31+i+d[i])>>>0; } }
     return {ink, hash};
   });
   return {names, sigs};
 });
 const bad = viz.err || viz.sigs.some(s=>typeof s==='string' || s.ink<200);
 const hashes = viz.err?[]:viz.sigs.map(s=>s.hash);
 const uniq = new Set(hashes).size;
 ok(!bad && uniq===5,
    '[1] 🚨 FIVE MODES, FIVE DIFFERENT PICTURES — Reel used to fall through `|| drawWire` and draw the Studio card',
    viz.err || (viz.names.map((n,i)=>n+':'+(viz.sigs[i].ink||viz.sigs[i])+'px').join('  ')+'   distinct drawings: '+uniq+'/5'));

 // ── [2] AND THE CARD OFFERS ALL FIVE ────────────────────────────────────────────────────
 const types=await p.evaluate(async()=>{
   const i=window.__fxrAdd?window.__fxrAdd('tape'):-1; if(i<0) return {err:'no free tape slot'};
   await new Promise(r=>setTimeout(r,600));
   const d=(window.__fxrDevs?window.__fxrDevs():[])[i];
   return {types:d?d.types:null}; });
 ok(types.types && types.types.join('|')==='Studio|Cassette',
    '[2] THE ROUTED TAPE CARD OFFERS TWO — Studio and Cassette, the ones with a back panel (tp63: Reel / Porta / Wire are modules, not card types)',
    JSON.stringify(types));

 ok(errs.length===0,'[3] NO PAGE ERRORS', errs.slice(0,3).join(' | ')||'clean');
 console.log('\n  '+pass+' passed, '+fail+' failed');
 await b.close(); process.exit(fail?1:0);
})();
