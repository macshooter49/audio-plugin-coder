// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp59 — MAX'S LIST, AND THE TWO GATES THAT LIED.
//
//  🚨 [0] AND [1] ARE CORRECTIONS TO EARLIER GREEN BARS.
//   · tp57 shipped ⌘A and its gate dispatched a synthetic KeyboardEvent straight into the page.
//     That proved the HANDLER and never the ROUTE — and fb135 had already recorded that the page
//     does not get keys in a host ("FL kept the keys"). Max: "I cannot control A shit, you
//     fucking liar." He was right. [0] drives the chord through window.__tiHostChord, which is
//     the function C++ calls from keyPressed, so the bar fails if that route is ever removed.
//   · tp54's bar [2] measures the Slices pill's clearance from the sample library WITH NO SLICES
//     LOADED — the one state where the count badge is display:none and the pill is a dozen pixels
//     narrower. It has read a comfortable 13.1 through two separate collisions. [1] loads sixteen
//     chops first, which is the state Max screenshots.
//
//    node Tests/_tp59_gate.js
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
const PAGE=path.join(require('os').tmpdir(),'tp59.html'); fs.writeFileSync(PAGE,html);

let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));}
                    else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };
const spy=()=>{ const br=window.Juce, orig=br.getNativeFunction; window.__natLog=[];
  const wrapped=(n)=>{ const f=orig(n); return function(){ window.__natLog.push([n].concat([].slice.call(arguments))); return f.apply(null,arguments); }; };
  let held=Object.assign({},br,{getNativeFunction:wrapped});
  Object.defineProperty(window,'Juce',{configurable:true,get(){return held;},
    set(v){ held=Object.assign({},v||{},{getNativeFunction:wrapped,getSliderState:br.getSliderState}); }}); };
const fake=()=>{const N=1200,mn=[],mx=[];for(let i=0;i<N;i++){const e=.2+.7*Math.abs(Math.sin(i/23));mx.push(e);mn.push(-e);}
 window.onSampleLoaded({filename:'D LOOP FOREST GUMP 140.wav',lengthSamples:192000,peaksMin:mn,peaksMax:mx,rootMidiNote:60});};

(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:2});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();\n('+spy.toString()+')();');
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>document.documentElement.setAttribute('data-theme','dark'));
 await p.evaluate(()=>document.getElementById('mix-btn').click()); await sleep(1300);
 await p.evaluate(fake); await sleep(700);
 await p.evaluate(()=>{const q=document.querySelectorAll('#ti-mode-toggle .ti-mode-pill'); if(q[1]) q[1].dispatchEvent(new MouseEvent('click',{bubbles:true}));}); await sleep(600);
 // sixteen real chops, through the shipped door
 await p.evaluate(async()=>{
   const mk=()=>JSON.stringify({slices:Array.from({length:16},(_,i)=>({start:i*12000,end:(i+1)*12000,pitch:0,volume:1,attackMs:1,decayMs:0,sustainLevel:1,releaseMs:20,reverse:false,warpMode:0,stretchRatio:1}))});
   const br=window.Juce, orig=br.getNativeFunction;
   const wrapped=(n)=>((n==='getSlicesJson'||n==='gridSliceSlices')?(()=>Promise.resolve(mk())):orig(n));
   Object.defineProperty(window,'Juce',{configurable:true,value:Object.assign({},br,{getNativeFunction:wrapped})});
   const g=document.querySelector('#ti-grid-row .ti-grid-pill[data-n="16"]')||document.querySelector('#ti-grid-row .ti-grid-pill[data-n="8"]');
   if(g) g.dispatchEvent(new MouseEvent('click',{bubbles:true}));
   await new Promise(r=>setTimeout(r,900));
 }); await sleep(800);

 // ── [0] ⌘A THROUGH THE ROUTE THE PLUGIN ACTUALLY USES ───────────────────────────────────
 const sel=await p.evaluate(async()=>{
   const before=(window.__tiChopSel?window.__tiChopSel():[]).length;
   if(typeof window.__tiHostChord!=='function') return {err:'no __tiHostChord — the host route does not exist'};
   window.__tiHostChord('a',1,0);                      // exactly what keyPressed evaluates
   await new Promise(r=>setTimeout(r,250));
   const after=(window.__tiChopSel?window.__tiChopSel():[]).length;
   window.__tiHostChord('Escape',0,0);
   await new Promise(r=>setTimeout(r,200));
   return {before,after,cleared:(window.__tiChopSel?window.__tiChopSel():[]).length}; });
 ok(!sel.err && sel.before===0 && sel.after===16 && sel.cleared===0,
    '[0] 🚨 ⌘A SELECTS EVERY CHOP THROUGH THE HOST-KEY ROUTE (tp57 only ever proved the handler)',
    JSON.stringify(sel));

 // ── [1] THE SLICES PILL CLEARS THE LIBRARY *WITH SIXTEEN CHOPS LOADED* ──────────────────
 const lay=await p.evaluate(()=>{
   const pill=document.getElementById('ti-slices-btn'), lib=document.getElementById('ti-lib');
   const cnt=document.getElementById('ti-slices-count'), play=document.querySelector('#ti-play-mode-toggle .ti-play-pill');
   const a=pill.getBoundingClientRect(), l=lib.getBoundingClientRect(), q=play.getBoundingClientRect();
   return {gap:+(l.left-a.right).toFixed(1), countText:cnt.textContent,
           countShown:getComputedStyle(cnt).display!=='none',
           pillH:+a.height.toFixed(1), playH:+q.height.toFixed(1)}; });
 ok(lay.countShown && lay.countText==='16' && lay.gap>12,
    '[1] 🚨 THE SLICES PILL CLEARS THE LIBRARY WITH A COUNT IN IT — tp54 measures the empty state and read 13.1 through two collisions',
    JSON.stringify(lay));
 ok(Math.abs(lay.pillH-lay.playH)<0.6,
    '[2] ...AND IT IS THE PLAY PILLS\' OWN HEIGHT — the 11 px count badge made it 19 against their 17.5',
    'Slices '+lay.pillH+'  ·  play pill '+lay.playH);

 // ── [3] THE EXPORT LINE IS INVISIBLE UNTIL IT SAYS SOMETHING ────────────────────────────
 const chip=await p.evaluate(()=>{const e=document.getElementById('stem-status'); if(!e) return null;
   const c=getComputedStyle(e); return {op:+c.opacity, pos:c.position, bg:c.backgroundColor};});
 ok(chip && chip.op===0 && chip.pos==='absolute',
    '[3] 🚨 THE STEM PANE\'S STATUS CHIP IS INVISIBLE AT REST — tp58 gave it a background and left it at opacity 1, so a dark bar painted across Export / Reveal from page load',
    JSON.stringify(chip));

 // ── [4] THE GRAIN ENGINE / EFFECTS STRIP CANNOT FOLLOW YOU ONTO THE CHOP PAGE ───────────
 const ctl=await p.evaluate(async()=>{
   const c=document.getElementById('controls');
   c.style.display='';                      // exactly what applySynPanelOpen(false) does
   await new Promise(r=>setTimeout(r,120));
   return {display:getComputedStyle(c).display, chopOpen:document.body.classList.contains('chop-open')};});
 ok(ctl.chopOpen && ctl.display==='none',
    '[4] 🚨 #controls STAYS HIDDEN EVEN WHEN SOMETHING ELSE SETS ITS INLINE STYLE BACK — it was hidden by one handler and four other places re-show it',
    JSON.stringify(ctl));

 // ── [5] CHOP 1'S NUMBER IS DRESSED LIKE EVERY OTHER NUMBER ──────────────────────────────
 const one=await p.evaluate(()=>{
   const all=[...document.querySelectorAll('#ti-slice-overlays .ti-slice-label')];
   const orphan=all.find(e=>!e.closest('.ti-slice-marker'));
   const inMarker=all.find(e=>e.closest('.ti-slice-marker'));
   if(!orphan||!inMarker) return {err:'labels not found', n:all.length};
   const a=getComputedStyle(orphan), b=getComputedStyle(inMarker);
   return {txt:orphan.textContent, colour:a.color, ref:b.color, bg:a.backgroundColor, refBg:b.backgroundColor,
           font:a.font||a.fontSize+'/'+a.fontWeight, refFont:b.font||b.fontSize+'/'+b.fontWeight};});
 ok(!one.err && one.colour===one.ref && one.bg===one.refBg,
    '[5] 🚨 CHOP 1\'S NUMBER WEARS THE HOUSE CHIP — it was a bare div outside any marker, so the `.ti-slice-marker .ti-slice-label` rule matched NOTHING and it rendered as unstyled default text: Max\'s "black one"',
    JSON.stringify(one));

 ok(errs.length===0,'[6] NO PAGE ERRORS', errs.slice(0,3).join(' | ')||'clean');
 console.log('\n  '+pass+' passed, '+fail+' failed');
 await b.close(); process.exit(fail?1:0);
})();
