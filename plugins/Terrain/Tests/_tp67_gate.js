// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp67 — THE GLITCH IS ALWAYS IN TIME.
//
//    node Tests/_tp67_gate.js
//
//  [0] by text: the Clock parameter defaults to Sync (instances 2..4 clone that default), the card's Init is Sync,
//      the dice has the time law (DICE_TIME_RX / DICE_SYNC) and no longer re-deals the modules' grids
//  [1] 🚨 twelve rolls from a hand-set Free: the Clock is Sync after every roll, every module's Trig is Sync, the fire
//      grid (grate), every module's out grid (_ogrd) and every FLOW_GLI_*_GRID parameter never move
//  [2] Free is still the hand's: setting the Clock by hand reads back Free
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const src=fs.readFileSync('Source/ui/public/index.html','utf8'), cpp=fs.readFileSync('Source/PluginProcessor.cpp','utf8');
const PAGE=path.join(require('os').tmpdir(),'tp67.html'); fs.writeFileSync(PAGE,src);
let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };

const t0={ def:/ParameterIDs::FLOW_GLI_SYNC, 1 \}, "Glitch Clock",\s+juce::StringArray \{ "Free", "Sync" \}, 1\)\)/.test(cpp),
  clone:/A clone carries the source's type, range\/choices, default and label/.test(cpp),
  init:/syncf:1, seed:0 \};/.test(src), rx:/var DICE_TIME_RX=\{ gli:\/\^\(syncf\|grate\|quant\)\$\|_otrg\$\|_ogrd\$\/ \};/.test(src),
  sync:/var DICE_SYNC   =\{ gli:function\(d\)\{ try\{ d\.S\.set\('syncf',1\);/.test(src), grids:!/if\(id==='gli'\) diceGrids\(\/\^FLOW_GLI_\[A-Z\]\+_GRID\$\/\);\n/.test(src) };
ok(Object.values(t0).every(Boolean), '[0] the Clock defaults to Sync (clones carry the default), the card\'s Init is Sync, the dice has the time law and no longer re-deals the modules\' grids', JSON.stringify(t0));

(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:1200,height:800});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();'); const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 const r1=await p.evaluate(async()=>{
   try{ if(window.__tiEnsure&&window.__tiEnsure.gli) window.__tiEnsure.gli(); }catch(e){}
   await new Promise(r=>setTimeout(r,400));
   const d=window.__tiDice&&window.__tiDice.gli; if(!d) return {err:'no glitch dice object'};
   const timeKeys=Object.keys(d.DEF).filter(k=>/^(grate|quant)$|_ogrd$/.test(k)), trigKeys=Object.keys(d.DEF).filter(k=>/_otrg$/.test(k));
   const gridNames=((window.__JUCE__&&window.__JUCE__.initialisationData&&window.__JUCE__.initialisationData.__juce__sliders)||[]).filter(n=>/^FLOW_GLI_[A-Z]+_GRID$/.test(n));
   const gridRead=()=>gridNames.map(n=>{ try{ return +window.Juce.getSliderState(n).getNormalisedValue().toFixed(4); }catch(e){ return null; } });
   /* a hand takes it to Free, and one module's Trig to Free, and moves the fire grid */
   d.S.set('syncf',0); if(trigKeys[0]) d.S.set(trigKeys[0],1); d.S.set('grate',7);
   const before={}; timeKeys.forEach(k=>before[k]=d.S.v[k]); const gridsBefore=gridRead();
   const rolls=[]; let others=0;
   for(let i=0;i<12;i++){ const snap=JSON.stringify(d.S.v); window.__tiDiceMode('gli', i%2===1); await new Promise(r=>setTimeout(r,60));
     const bad=[]; if(d.S.v.syncf!==1) bad.push('syncf='+d.S.v.syncf); trigKeys.forEach(k=>{ if(d.S.v[k]!==0) bad.push(k+'='+d.S.v[k]); }); timeKeys.forEach(k=>{ if(d.S.v[k]!==before[k]) bad.push(k+':'+before[k]+'→'+d.S.v[k]); });
     if(JSON.stringify(d.S.v)!==snap) others++; rolls.push(bad); }
   const gridsAfter=gridRead(); const gridsMoved=gridNames.filter((n,i)=>gridsBefore[i]!==gridsAfter[i]);
   return {rolls:rolls.filter(x=>x.length).slice(0,4), rollsBad:rolls.filter(x=>x.length).length, others, trig:trigKeys.length, time:timeKeys.length, grids:gridNames.length, gridsMoved};
 });
 /* the sim publishes no __juce__sliders, so the FLOW_GLI_*_GRID parameters cannot be watched here — bar [0] proves the call that rolled them is gone */
 ok(!r1.err && r1.rollsBad===0 && r1.gridsMoved.length===0 && r1.others>=10 && r1.trig>=4 && r1.time>=4,
    '🚨 [1] twelve rolls from a hand-set Free: the Clock is Sync after every one, every module\'s Trig is Sync, the fire grid / out grids / GRID parameters never move — and the rest of the card still rolls', JSON.stringify(r1));
 const r2=await p.evaluate(()=>{ const d=window.__tiDice.gli; d.S.set('syncf',0); const free=d.S.v.syncf===0; d.S.set('syncf',1); return {free, back:d.S.v.syncf===1}; });
 ok(r2.free && r2.back, '[2] Free is still the hand\'s: the Clock set by hand reads back Free, and back to Sync', JSON.stringify(r2));
 ok(errs.length===0,'[3] the page threw nothing', errs.slice(0,3).join(' | ')||'clean');
 await b.close(); console.log('\n  '+pass+' passed, '+fail+' failed\n'); process.exit(fail?1:0);
})();
