// tp18 — the rack must really hand out 8 instances of every kind, not just declare them.
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
  const p=await b.newPage(); await p.setViewport({width:1200,height:820}); const errs=[]; p.on('pageerror',e=>errs.push(e.message));
  await p.evaluateOnNewDocument(()=>{ try{ localStorage.clear(); }catch(e){} });
  await p.goto('file://'+process.argv[2]+'?page=1',{waitUntil:'load'}); await sleep(1600);
  const out=await p.evaluate(async()=>{
    const R={};
    // how many of ONE kind can actually be claimed through the rack's own add path
    let added=0; for(let i=0;i<10;i++){ const idx=window.__fxrAdd?window.__fxrAdd('flt'):-1; if(idx>=0) added++; else break; }
    const D=window.__fxrDevs?window.__fxrDevs():[];
    R.filtersClaimed=added;
    R.instances=D.filter(d=>d.core==='flt').map(d=>d.inst).sort((a,b)=>a-b);
    // the mod destinations for the NEW instances must exist and must NOT collide with the old block
    const dest=(i,k)=>window.__fxModDest?window.__fxModDest('flt',i,k):null;
    R.dest_inst1_knob0=dest(1,0); R.dest_inst6_knob0=dest(6,0);
    R.dest_inst7_knob0=dest(7,0); R.dest_inst8_knob11=dest(8,11);
    R.dest_inst9_isNull=dest(9,0)===null;
    const all=[]; for(let i=1;i<=8;i++) for(let k=0;k<12;k++){ const d=dest(i,k); if(d!=null) all.push(d); }
    R.destCount=all.length; R.destUnique=new Set(all).size;
    return R;
  });
  console.log(JSON.stringify(out,null,1)); console.log('errs',JSON.stringify(errs));
  await b.close();
})().catch(e=>{ console.log('FAIL',String(e).slice(0,300)); process.exit(1); });
