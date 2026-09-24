// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp64 — MAX'S 2026-09-20 EVENING LIST, THROUGH THE SHIPPED DOORS.
//
//    node Tests/_tp64_gate.js
//
//  [0] the Tape shelf: the routed card's five types + the Deck; no global machine
//  [1] Reel is a CARD: Sculpt / Drive / Timbre, no flip; two Reels at once (per instance params)
//  [2] the Deck: on the shelf, lands from it, Remove takes it home, presence follows SYN_DCK_ACTIVE
//  [3] MOD and CHOP pills never toggle off (only SYN has a back)
//  [4] Settings: the bend "2" and its "st" wear the page's ink and the label's font
//  [5] 🚨 a preset recall re-hydrates the Chop page (waveform, chops, the library strip) and a recall
//      into an empty layer CLEARS the previous patch's picture
//  [6] the C++ half, by text: the clamp is five wide, the Reel's Sculptor read, the layer's loop mode
//      and typed BPM saved, the spare FLAC written, bank B released
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const cpp=fs.readFileSync('Source/PluginEditor.cpp','utf8');
const i0=cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0=cpp.indexOf('html = html.replace ("</body>", heroOverlay',i0);
const ov=[...cpp.slice(i0,j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m=>m[1]).join('');
const html=fs.readFileSync('Source/ui/public/index.html','utf8').replace('</body>',ov+'</body>');
const PAGE=path.join(require('os').tmpdir(),'tp64.html'); fs.writeFileSync(PAGE,html);
let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };

(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:1200,height:800,deviceScaleFactor:2});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();'+`
   /* the natives the page asks for, answered by the gate when it says so (window.__fakeNat) */
   (function(){ const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>{ if(window.__fakeNat&&window.__fakeNat[n]) return (...a)=>Promise.resolve(window.__fakeNat[n](...a)); return orig.getNativeFunction(n); } });
     Object.defineProperty(window,'Juce',{configurable:true,get(){return wrap;},set(){}}); })();`);
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>document.documentElement.setAttribute('data-theme','dark'));

 // ══ THE PATCHER ══════════════════════════════════════════════════════════════════════════════
 await p.evaluate(()=>window.setActivePanel('tp')); await sleep(2200);

 // ── [0] the shelf ──
 const r0=await p.evaluate(()=>(window.__tpCatalog?window.__tpCatalog():[]).filter(i=>i.cat==='Tape').map(i=>i.n+':'+i.k));
 ok(r0.join('|')==='Studio:tapefx:Studio|Cassette:tapefx:Cassette|Reel:tapefx:Reel|Porta:tapefx:Porta|Wire:tapefx:Wire|Deck:deck',
    '🚨 [0] THE TAPE SHELF: Studio / Cassette / Reel / Porta / Wire are the routed card\'s types, the Deck is beside them, and no global machine is offered', r0.join(' | '));

 // ── [1] Reel is a card: the Sculptor's three, no flip, and two of them ──
 const r1=await p.evaluate(async()=>{
   window.__tpAddFromList('tapefx:Reel',300,180); await new Promise(r=>setTimeout(r,900));
   window.__tpAddFromList('tapefx:Reel',560,180); await new Promise(r=>setTimeout(r,900));
   const devs=(window.__fxrDevs?window.__fxrDevs():[]).map((d,i)=>({i,d})).filter(x=>x.d&&x.d.core==='tape');
   const out=devs.map(x=>({type:x.d.type,noBack:!!x.d.noBack,knobs:(x.d.knobs||[]).map(k=>k.l+'='+k.p),tp:x.d.tp,
      swap:!!document.querySelector('.fxr-dev[data-dev="'+x.i+'"] [data-act="swap"]')}));
   const names=(window.__tpNodes?window.__tpNodes():[]).map(n=>window.__tpNodeName(window.__tpNodeByKey(n.key))).filter(n=>n==='Reel');
   return {out,names,t1:window.__params['SYN_TPE_TYPE'],t2:window.__params['SYN_TPE2_TYPE'],tape:!!window.__tpNodeByKey('tape')}; });
 const reel=r1.out.filter(o=>o.type==='Reel');
 ok(reel.length===2 && r1.names.length===2 && !r1.tape,
    '[1] TWO Reels at once, each a routed card node named Reel — duplicatable like every other card; the master machine stays off the canvas', JSON.stringify({cards:r1.out.length,reels:reel.length,names:r1.names,tape:r1.tape}));
 ok(reel.length===2 && reel.every(o=>o.noBack&&!o.swap) && reel[0].knobs.join(',')==='Sculpt=SYN_TPE_SCULPT,Drive=SYN_TPE_WEAVE,Timbre=SYN_TPE_TILT,Mix=SYN_TPE_MIX'
      && reel[1].knobs.join(',')==='Sculpt=SYN_TPE2_SCULPT,Drive=SYN_TPE2_WEAVE,Timbre=SYN_TPE2_TILT,Mix=SYN_TPE2_MIX',
    '[1b] a Reel wears Sculpt / Drive / Timbre (the Harmonic Sculptor\'s own per-instance params) and NO flip; the second one on its own SYN_TPE2_ set', JSON.stringify(reel.map(o=>({knobs:o.knobs,noBack:o.noBack,swap:o.swap}))));
 ok(Math.abs((r1.t1||0)-2/7)<0.02 && Math.abs((r1.t2||0)-2/7)<0.02,
    '[1c] both cards wrote TYPE = Reel (index 2 of 8) to their own parameter', JSON.stringify({t1:r1.t1,t2:r1.t2}));

 // ── [2] the Deck ──
 const r2=await p.evaluate(async()=>{
   const before=!!window.__tpNodeByKey('tapeloop');
   window.__tpAddFromList('deck',300,460); await new Promise(r=>setTimeout(r,900));
   const n=window.__tpNodeByKey('tapeloop'); const name=n?window.__tpNodeName(n):null; const lay1=window.__tpLayout().deck;
   const shelf=(window.__tpCatalog?window.__tpCatalog():[]).filter(i=>i.k==='deck')[0];
   const loopOn=!!(document.getElementById('loop-toggle')&&!document.getElementById('loop-toggle').classList.contains('off'));
   window.__tpRemoveNode(n); await new Promise(r=>setTimeout(r,900));
   const gone=!window.__tpNodeByKey('tapeloop'); const lay0=window.__tpLayout().deck; const act0=window.__params['SYN_DCK_ACTIVE'];
   /* the patch says it is cabled: presence follows the parameter, not a localStorage flag */
   window.__tpSetP('SYN_DCK_ACTIVE',1); window.__tpSyncPresence(); await new Promise(r=>setTimeout(r,900));   /* the canvas's own write (the shim pv() reads) */
   const back=!!window.__tpNodeByKey('tapeloop');
   window.__tpSetP('SYN_DCK_ACTIVE',0); window.__tpSyncPresence(); await new Promise(r=>setTimeout(r,900));
   const home=!window.__tpNodeByKey('tapeloop');
   return {before,name,lay1,dis:shelf?shelf.dis:null,loopOn,gone,lay0,act0,back,home}; });
 ok(!r2.before && r2.name==='Deck' && r2.lay1===1 && r2.dis===true && r2.loopOn,
    '[2] the Deck lands from the shelf (named Deck, the hero deck switched on, the shelf marks it present)', JSON.stringify(r2));
 ok(r2.gone && !r2.lay0 && (r2.act0===0||r2.act0==null),
    '[2b] Remove takes the Deck home and clears its In-Chain claim', JSON.stringify({gone:r2.gone,lay0:r2.lay0,act0:r2.act0}));
 ok(r2.back && r2.home,
    '🚨 [2c] presence follows SYN_DCK_ACTIVE — a cabled Deck is on the canvas because the PATCH says so (the sampler law, tp61), and leaves when the claim goes', JSON.stringify({back:r2.back,home:r2.home}));

 // ── [3] the pills ──
 await p.evaluate(()=>window.setActivePanel(null)); await sleep(400);
 const r3=await p.evaluate(async()=>{ const mod=document.getElementById('mod-btn'), mix=document.getElementById('mix-btn'), panel=document.getElementById('mix-panel');
   mod.click(); await new Promise(r=>setTimeout(r,300)); const m1=currentActivePanel; mod.click(); await new Promise(r=>setTimeout(r,300)); const m2=currentActivePanel;
   mix.click(); await new Promise(r=>setTimeout(r,600)); const c1=panel.classList.contains('open'); mix.click(); await new Promise(r=>setTimeout(r,600)); const c2=panel.classList.contains('open');
   return {m1,m2,c1,c2}; });
 ok(r3.m1==='mod' && r3.m2==='mod', '[3] the MOD pill: a second click on the lit pill stays on MOD (it used to fall back to the hero)', JSON.stringify(r3));
 ok(r3.c1 && r3.c2, '[3b] the CHOP pill: a second click on the lit pill stays on the Chop page', JSON.stringify(r3));
 /* tp65 — Max: "just make sure I can't go back to it whenever I press mod on and off and I press the chop engine on and
    off. I just don't want to see it." Every pill sequence, and the page-0 restore, must leave the front (#controls) hidden. */
 const r3c=await p.evaluate(async()=>{ const shown=id=>{ const e=document.getElementById(id); if(!e) return false; const cs=getComputedStyle(e), r=e.getBoundingClientRect(); return cs.display!=='none'&&cs.visibility!=='hidden'&&r.width>0&&r.height>0; };
   const out=[]; const go=async id=>{ document.getElementById(id).click(); await new Promise(r=>setTimeout(r,450)); out.push(id+':'+(shown('controls')?'FRONT':'ok')+':'+currentActivePanel); };
   for(const id of ['syn-btn','mod-btn','mix-btn','mod-btn','mix-btn','syn-btn','mix-btn','syn-btn','syn-btn','mod-btn','mod-btn','mix-btn','mix-btn','syn-btn']) await go(id);
   restoreUiPage(0); await new Promise(r=>setTimeout(r,300)); out.push('page0:'+(shown('controls')?'FRONT':'ok')+':'+currentActivePanel);
   return out; });
 ok(r3c.every(x=>x.indexOf(':FRONT')<0) && /page0:ok:syn$/.test(r3c[r3c.length-1]),
    '🚨 [3c] THE FRONT PAGE IS NEVER SHOWN: fourteen pill presses in every order (CHOP → MOD used to leave the hero under MOD) and a saved page 0 lands on SYN', r3c.join(' '));

 // ── [4] the Settings ink ──
 // tp100 — the gear panel's #rr-bend-row moved into the settings overlay (#st-overlay [data-row="bend"]), and the
 // number now follows the house ultra-thin law (white SF Pro, weight 200) instead of the label's face.
 const r4=await p.evaluate(async()=>{ if(!window.__st) return {err:'no overlay'}; window.__st.open(true); window.__st.go('control');
   await new Promise(r=>setTimeout(r,150));
   const b=document.querySelector('#st-overlay [data-row="bend"] .num b'), st=b&&b.nextElementSibling;
   if(!b||!st){ window.__st.open(false); return {err:'no bend row'}; }
   const br=c=>{ const m=/rgba?\((\d+),\s*(\d+),\s*(\d+)/.exec(c||''); return m?(+m[1]+ +m[2]+ +m[3])/3:-1; };
   const o={two:br(getComputedStyle(b).color), st:br(getComputedStyle(st).color), w:getComputedStyle(b).fontWeight, stText:st.textContent};
   window.__st.open(false); return o; });
 ok(!r4.err && r4.two>200 && r4.st>200 && +r4.w<=300 && r4.stText==='ST', '[4] Settings: the bend "2" is ultra-thin white and its unit reads "ST" (white, no black, no heavy face)', JSON.stringify(r4));

 // ══ THE CHOP PAGE FOLLOWS THE PRESET ═════════════════════════════════════════════════════════
 const r5=await p.evaluate(async()=>{
   const N=1200,mn=[],mx=[]; for(let i=0;i<N;i++){ const e=.2+.7*Math.abs(Math.sin(i/23)); mx.push(e); mn.push(-e); }
   const full={filename:'BELL ONE SHOT 01.wav',sampleRate:48000,lengthSamples:48000,numChannels:2,peaksMin:mn,peaksMax:mx,sliceMode:1,sampleLoopMode:1,rootMidiNote:60,activeSliceIndex:1};
   const slices={slices:[{start:0,end:12000},{start:12000,end:24000},{start:24000,end:36000},{start:36000,end:48000}]};
   window.__fakeNat={ getEditingLayerIdx:()=>0, getAllLayerPayloads:()=>[full,{},{},{}], getSlicesJson:()=>JSON.stringify(slices), getSliceMode:()=>1, getSliceSubMode:()=>0, getActiveSliceIndex:()=>1, getLayerHasSample:()=>true };
   window.onPatchLoaded('{"name":"Respawn","bank":"Synth Pad"}','host'); await new Promise(r=>setTimeout(r,1200));
   const hero=document.getElementById('hero'); const a={has:hero.classList.contains('has-sample'),empty:hero.classList.contains('empty-state'),
     markers:document.querySelectorAll('.ti-slice-marker').length, lib:(document.getElementById('ti-lib-name')||{}).textContent||'',
     pull:!!window.__tiChopRepull, pulls:(window.__tiChopPulls||[]).length};
   /* the next patch has NO sample in the editing layer: the picture must go */
   window.__fakeNat={ getEditingLayerIdx:()=>0, getAllLayerPayloads:()=>[{},{},{},{}], getSlicesJson:()=>'{"slices":[]}', getSliceMode:()=>0, getSliceSubMode:()=>0, getActiveSliceIndex:()=>0, getLayerHasSample:()=>false };
   window.onPatchLoaded('{"name":"Init","bank":"Init"}','host'); await new Promise(r=>setTimeout(r,1200));
   const c={has:hero.classList.contains('has-sample'),empty:hero.classList.contains('empty-state'),markers:document.querySelectorAll('.ti-slice-marker').length};
   return {a,c}; });
 ok(r5.a.pull && r5.a.pulls>=3, '[5] the page has a chop re-pull and the per-open pulls registered with it (strips/trigger/stems, hold, ARM/BPM)', JSON.stringify({pull:r5.a.pull,pulls:r5.a.pulls}));
 ok(r5.a.has && !r5.a.empty && r5.a.markers>=3 && /BELL/i.test(r5.a.lib),
    '🚨 [5b] A PRESET RECALL RE-HYDRATES THE CHOP PAGE: the waveform is up, the chops are drawn, the library strip names the one-shot', JSON.stringify(r5.a));
 ok(!r5.c.has && r5.c.empty && r5.c.markers===0,
    '[5c] a recall into an empty layer drops the previous patch\'s waveform and chops', JSON.stringify(r5.c));

 // ── [6] the C++ half, by text ──
 const pp=fs.readFileSync('Source/PluginProcessor.cpp','utf8'), eng=fs.readFileSync('Source/TapeFxEngine.h','utf8');
 const r6={ clamp:/wantType = \(ty >= 0 && ty <= 4\) \? ty : 0/.test(pp), sculpt:/tp\.sculpt = M \(V\.sculpt\); tp\.weave = M \(V\.weave\); tp\.tilt = M \(V\.tilt\) \* 2\.0f - 1\.0f;/.test(pp),
   engine:/processSample \(inL, mWow, sat, cassHiss, mWow, sat, wireHiss, pr_\.sculpt, pr_\.weave, tilt\)/.test(eng),
   loopSaved:/setProperty \("sampleLoopMode",\s+L\.sampleLoopMode\.load\(\)/.test(pp) && /L\.sampleLoopMode\.store\s+\(\(int\) layerNode\.getProperty \("sampleLoopMode"/.test(pp),
   bpmSaved:/setProperty \("sourceBpmUser"/.test(pp) && /L\.sourceBpmUser\.store\s+\(\(float\)\(double\) layerNode\.getProperty \("sourceBpmUser"/.test(pp),
   spare:/static void tiWriteSampleSpare/.test(pp) && /getChildFile \("Imported"\)/.test(pp) && /tiWriteSampleSpare \(\*buf/.test(pp),
   bankB:/void TerrainAudioProcessor::releaseIdleBankBIfUnused/.test(pp) && /releaseIdleBankBIfUnused\(\);/.test(pp) && /bankB_\.store \(nullptr, std::memory_order_release\);/.test(pp) && /synthEngineB_\.reset\(\);/.test(pp) };
 ok(Object.values(r6).every(Boolean), '[6] the C++ half: the five-wide clamp, the Reel\'s Sculptor read (processor + engine), the layer\'s loop mode and typed BPM saved and read, the spare FLAC, bank B released behind the fence', JSON.stringify(r6));

 ok(errs.length===0,'[7] the page threw nothing', errs.slice(0,3).join(' | ')||'clean');
 await b.close(); console.log('\n  '+pass+' passed, '+fail+' failed\n'); process.exit(fail?1:0);
})();
