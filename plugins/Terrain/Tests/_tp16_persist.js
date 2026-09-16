// tp16 — REAL persistence: set the reach, RELOAD the page (storage intact), and see whether it comes back.
// A close/open proves nothing — `layout` is module state that outlives it.
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
  const p=await b.newPage(); await p.setViewport({width:1200,height:820}); const errs=[]; p.on('pageerror',e=>errs.push(e.message));
  const URL='file://'+process.argv[2]+'?page=1';
  await p.goto(URL,{waitUntil:'load'});
  await p.evaluate(()=>{ try{ localStorage.clear(); }catch(e){} });
  await p.goto(URL,{waitUntil:'load'}); await sleep(1500);
  const pick=async sel=>p.evaluate(s=>{ const t=document.querySelector('#tp-page .tp-tools .g[data-t="dice"]'), r=t.getBoundingClientRect();
      t.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,clientX:r.left+5,clientY:r.top+5}));
      const el=document.querySelector('#tp-page .tp-nmenu '+s); if(!el) return false; el.dispatchEvent(new MouseEvent('mousedown',{bubbles:true})); return true; },sel);
  const marked=async()=>p.evaluate(()=>{ const c=document.querySelector('#tp-page .tp-nmenu [data-dl].cur'), a=document.querySelector('#tp-page .tp-nmenu [data-aim].cur');
      return {reach:c?c.textContent:null, aim:a?a.textContent:null}; });
  await p.evaluate(()=>{ window.__tpOpen&&window.__tpOpen(); }); await sleep(1400);
  const out={};
  await pick('[data-dl="crazy"]'); await sleep(400);
  await pick('[data-aim="bass"]'); await sleep(1400);            // the AIM takes the same path — if one saves, both do
  out.keys = await p.evaluate(()=>{ const o={}; for(let i=0;i<localStorage.length;i++){ const k=localStorage.key(i); o[k]=(localStorage.getItem(k)||'').slice(0,90); } return o; });
  await p.goto(URL,{waitUntil:'load'}); await sleep(1600);        // ── a real reload ──
  await p.evaluate(()=>{ window.__tpOpen&&window.__tpOpen(); }); await sleep(1500);
  await pick('[data-dl="crazy"]'); await sleep(200);
  out.afterReload = await marked();
  out.errs=errs; console.log(JSON.stringify(out,null,1)); await b.close();
})().catch(e=>{ console.log('FAIL',String(e).slice(0,300)); process.exit(1); });
