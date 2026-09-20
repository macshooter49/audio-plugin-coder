// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp63 — THE 2026-09-20 AFTERNOON LIST (the page half). Memory is Tests/au_lazy_memory.cpp and
//  Tests/mac_dice_memory.mm; the shaper de-phase is a source bar here because it is a C++ counter.
//    node Tests/_tp63_gate.js
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
const PAGE=path.join(require('os').tmpdir(),'tp63.html'); fs.writeFileSync(PAGE,html);
let pass=0,fail=0; const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };
const fake=()=>{const N=1200,mn=[],mx=[];for(let i=0;i<N;i++){const e=.2+.7*Math.abs(Math.sin(i/23));mx.push(e);mn.push(-e);}
 window.onSampleLoaded({filename:'BELL ONE SHOT 01.wav',lengthSamples:48000,peaksMin:mn,peaksMax:mx,rootMidiNote:60});};
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:2});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();'); await p.evaluateOnNewDocument(()=>{ try{ localStorage.setItem('tpLayout',JSON.stringify({v:6,pos:{},ext:{}})); }catch(e){} });
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE+'?page=1',{waitUntil:'load'}); await sleep(2200);
 await p.evaluate(()=>document.documentElement.setAttribute('data-theme','dark'));

 // ── [0] the Tape shelf (tp64 law): the ROUTED card's five types + the Deck; picking a machine lands a CARD, not the master module ──
 await p.evaluate(()=>window.setActivePanel('tp')); await sleep(2200);
 const r0=await p.evaluate(async()=>{ const cat=(window.__tpCatalog?window.__tpCatalog():[]).filter(i=>i.cat==='Tape').map(i=>i.n+':'+i.k);
   const tapeBefore=!!window.__tpNodeByKey('tape');
   window.__tpAddFromList && window.__tpAddFromList('tapefx:Porta',300,200); await new Promise(r=>setTimeout(r,900));
   const devs=(window.__fxrDevs?window.__fxrDevs():[]).filter(d=>d&&d.core==='tape');
   const names=(window.__tpNodes?window.__tpNodes():[]).map(n=>window.__tpNodeName(window.__tpNodeByKey(n.key)));
   return {cat, tapeBefore, tapeAfter:!!window.__tpNodeByKey('tape'), cardTypes:devs.map(d=>d.type), names,
           tapeOn:!!(document.getElementById('tape-toggle')&&!document.getElementById('tape-toggle').classList.contains('off'))}; });
 ok(r0.cat.join('|')==='Studio:tapefx:Studio|Cassette:tapefx:Cassette|Reel:tapefx:Reel|Porta:tapefx:Porta|Wire:tapefx:Wire|Deck:deck',
    '🚨 [0] THE TAPE SHELF (tp64): the routed card in its five types + the Deck — the global master machine is not offered, no "Tape ·"', r0.cat.join(' | '));
 ok(!r0.tapeBefore && !r0.tapeAfter && r0.cardTypes.indexOf('Porta')>=0 && r0.names.indexOf('Porta')>=0 && !r0.tapeOn,
    '[0b] picking Porta lands a routed CARD named Porta (a node, cables in and out) and leaves the hero\'s master machine off', JSON.stringify({tapeAfter:r0.tapeAfter,cardTypes:r0.cardTypes,tapeOn:r0.tapeOn}));

 // ── [1] the master filter always wins ──
 const r1=await p.evaluate(async()=>{ const N=()=>window.__tpRaw();
   window.__tpAddFromList('osc:a',120,120); await new Promise(r=>setTimeout(r,600));
   window.__flowSetChain(['glitch']); await new Promise(r=>setTimeout(r,700)); window.__tpSyncPresence&&window.__tpSyncPresence(); await new Promise(r=>setTimeout(r,400));
   const o=N().find(n=>n.kind==='osc'&&n.sub==='a'), g=N().find(n=>n.kind==='flow'&&n.sub==='glitch'), f=N().find(n=>n.kind==='filter');
   if(!o||!g||!f) return {err:'nodes', have:N().map(n=>n.kind+':'+(n.sub||''))};
   window.__tpConnect(o,'out',0,g,'in',0); await new Promise(r=>setTimeout(r,300));          // osc A → Glitch: a RAW tap (tp41)
   const tapRaw=(window.__tpFlowTaps('glitch')>>0)&1;
   window.__tpConnect(o,'out',0,f,'in',0); await new Promise(r=>setTimeout(r,300));          // then osc A → the master FILTER
   const tapAfter=(window.__tpFlowTaps('glitch')>>0)&1, mix=window.__tpParam('SYN_OSC_A_F1MIX');
   // and the other way round: the oscillator is in the filter first, then cabled into the card
   window.__tpConnect(o,'out',0,g,'in',0); await new Promise(r=>setTimeout(r,300));
   const tapAgain=(window.__tpFlowTaps('glitch')>>0)&1;
   return {tapRaw, tapAfter, mix, tapAgain}; });
 ok(!r1.err && r1.tapRaw===1 && r1.tapAfter===0 && r1.mix>0.5 && r1.tapAgain===0,
    '🚨 [1] THE MASTER FILTER ALWAYS WINS: osc A → Glitch taps raw (tp41); osc A → Filter turns that tap post-filter; a card cabled while A is in the filter takes it filtered', JSON.stringify(r1));

 // ── [2] the chop page: the Slices pill holds its width across 8 / 16 / 32 / 64; no caption; ink ──
 await p.evaluate(()=>{ const m=document.getElementById('mix-btn'); if(m) m.click(); }); await sleep(1300); await p.evaluate(fake); await sleep(700);
 const r2=await p.evaluate(async()=>{
   const br=window.Juce, orig=br.getNativeFunction; let cur=8;
   const mk=()=>JSON.stringify({slices:Array.from({length:cur},(_,i)=>({start:i*1000,end:(i+1)*1000,pitch:0,volume:1,attackMs:1,decayMs:0,sustainLevel:1,releaseMs:20,reverse:false,warpMode:0,stretchRatio:1}))});
   Object.defineProperty(window,'Juce',{configurable:true,value:Object.assign({},br,{getNativeFunction:(n)=>((n==='getSlicesJson'||n==='gridSliceSlices')?(()=>Promise.resolve(mk())):orig(n))})});
   const q=document.querySelectorAll('#ti-mode-toggle .ti-mode-pill'); if(q[1]) q[1].dispatchEvent(new MouseEvent('click',{bubbles:true})); await new Promise(r=>setTimeout(r,500));
   const sb=document.getElementById('ti-slices-btn'); const widths={}, lefts={};
   for(const n of [8,16,32,64]){ cur=n; const g=document.querySelector('#ti-grid-row .ti-grid-pill[data-n="'+n+'"]'); if(g) g.dispatchEvent(new MouseEvent('click',{bubbles:true})); await new Promise(r=>setTimeout(r,700));
     const r=sb.getBoundingClientRect(); widths[n]=+r.width.toFixed(1); lefts[n]=+r.left.toFixed(1); }
   const st=document.querySelector('#mix-stem-area .stem-status'); document.body.classList.add('ti-capture-off'); await new Promise(r=>setTimeout(r,200));
   const caption=st?getComputedStyle(st,'::after').content:'-'; document.body.classList.remove('ti-capture-off');
   return {widths, lefts, spread:+(Math.max(...Object.values(lefts))-Math.min(...Object.values(lefts))).toFixed(1), caption}; });
 ok(r2.spread<0.6, '[2] the Slices pill does not move between 8 / 16 / 32 / 64 chops (its count reserves two digits)', JSON.stringify(r2.lefts)+' widths '+JSON.stringify(r2.widths));
 ok(r2.caption==='none'||r2.caption==='""'||r2.caption==='normal', '[3] no caption under the stems with capture off — "forget captions and breadcrumbs"', 'content '+r2.caption);

 // ── [4] the pitch-bend number wears the theme ink, not black ──
 const r4=await p.evaluate(()=>{ const b=document.querySelector('#rr-bend-row b'); const c=b?getComputedStyle(b).color:null;
   const m=/rgba?\((\d+),\s*(\d+),\s*(\d+)/.exec(c||''); return {color:c, bright: m ? (+m[1]+ +m[2]+ +m[3])/3 : -1}; });
 ok(r4.bright>150, '[4] the pitch-bend "2" in Settings is the page\'s ink on the dark theme, not black', JSON.stringify(r4));

 // ── [5] SOURCE — the shaper follows its type with motion off: one counter per feed ──
 const ed=fs.readFileSync('Source/PluginEditor.cpp','utf8');
 ok(/const bool quarter = \(\+\+dstVizPushCtr_ >= 4\); if \(quarter\) dstVizPushCtr_ = 0;\s+if \(uiStatic \? decoTick : quarter\) js << "window\.__dstVizPush=/.test(ed),
    '[5] the distortion push is ONE counter (static: decoTick; motion on: every 4th tick) — tp62 nested two and they de-phased after any quiet spell');
 ok(/const juce::Colour house  = dark \? juce::Colour \(0xFFFFFFFF\) : juce::Colour \(0xFF1A1A2E\);/.test(ed), '[6] the capture strip\'s OFF word is white ink on dark, dark ink on light');

 ok(errs.length===0, '[7] the page threw nothing', errs.join(' | '));
 console.log('\n  '+pass+' passed, '+fail+' failed\n');
 await b.close(); process.exit(fail?1:0); })();
