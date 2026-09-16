// tp16 — ROLLING FROM THE PATCHER: the menu's two rows must really roll while the canvas owns the cards.
// The trap this exists for: the rack's setType used rack.querySelector, and with the Patcher open the
// cards are NOT inside #fxr-rack any more — it would have set no types at all, silently.
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
  const p=await b.newPage(); await p.setViewport({width:1200,height:820}); const errs=[]; p.on('pageerror',e=>errs.push(e.message));
  await p.evaluateOnNewDocument(()=>{ try{ localStorage.clear(); }catch(e){} });
  await p.goto('file://'+process.argv[2]+'?page=1',{waitUntil:'load'}); await sleep(1500);
  await p.evaluate(()=>{ if(window.setActivePanel) window.setActivePanel('syn');
    window.__setLog=[]; const o=window.__setSynParam; window.__setSynParam=function(id,v){ window.__setLog.push(id); return o.apply(this,arguments); }; });
  await sleep(600);
  const out={};
  // open the Patcher, set the reach to CRAZY through the menu, then use the two action rows
  await p.evaluate(()=>{ window.__tpOpen&&window.__tpOpen(); }); await sleep(1400);
  const clickRow=async(sel)=>p.evaluate(s=>{ const tool=document.querySelector('#tp-page .tp-tools .g[data-t="dice"]'); const r=tool.getBoundingClientRect();
      tool.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,clientX:r.left+5,clientY:r.top+5}));
      const el=document.querySelector('#tp-page .tp-nmenu '+s); if(!el) return false;
      el.dispatchEvent(new MouseEvent('mousedown',{bubbles:true})); return true; },sel);
  out.setCrazy=await clickRow('[data-dl="crazy"]'); await sleep(250);
  out.reachStored=await p.evaluate(()=>{ try{ return JSON.parse(localStorage.getItem('tpLayout')||'{}').dlevel; }catch(e){ return null; } });
  const before=await p.evaluate(()=>({nodes:window.__tpNodes().length, devs:(window.__fxrDevs()||[]).map(d=>d.core+':'+d.type).join(' > ')}));
  await p.evaluate(()=>{ window.__setLog.length=0; });
  out.clickedChain=await clickRow('[data-act="fx"]'); await sleep(2200);
  out.chain=await p.evaluate(bf=>{ const D=window.__fxrDevs()||[], N=window.__tpNodes();
      const fxNodes=N.filter(n=>/^fx-/.test(n.key));
      const ids=window.__setLog;
      return { before:bf.devs, after:D.map(d=>d.core+':'+d.type).join(' > '), devs:D.length,
        typesSet:D.slice(0,-1).filter(d=>d.types&&d.types.length>1).length,
        fxNodesOnCanvas:fxNodes.length, deadNodes:fxNodes.filter(n=>!D.some(d=>n.key==='fx-'+d.core+'-'+d.inst)).map(n=>n.key),
        missingNodes:D.filter(d=>d.core!=='cmp'&&!N.some(n=>n.key==='fx-'+d.core+'-'+d.inst)).map(d=>d.core),
        foreign:ids.filter(id=>/^SYN_OSC_|^SYN_FILTER\d|^SYN_ENV|^LFO\d/.test(id)).slice(0,5) }; },before);
  const rBefore=await p.evaluate(()=>(window.__tiRoutes()||[]).length);
  await p.evaluate(()=>{ window.__setLog.length=0; });
  out.clickedMod=await clickRow('[data-act="mod"]'); await sleep(1600);
  out.mod=await p.evaluate(rb=>{ const routes=window.__tiRoutes()||[], ids=window.__setLog;
      return { routesBefore:rb, routesAfter:routes.length,
        foreign:ids.filter(id=>!/^LFO\d|^SYN_ENV_|^SYN_MACRO_/.test(id)).slice(0,6),
        cables:window.__tpCables().length, nodes:window.__tpNodes().length }; },rBefore);
  out.errs=errs; console.log(JSON.stringify(out,null,1)); await b.close();
})().catch(e=>{ console.log('FAIL',String(e).slice(0,300)); process.exit(1); });
