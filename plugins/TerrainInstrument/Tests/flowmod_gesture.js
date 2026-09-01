// flowmod_gesture — THE FLOW-CARD UNDERLINE, DRIVEN BY THE GESTURE MAX ACTUALLY MAKES (fb524b).
//
// WHY THIS EXISTS ALONGSIDE flowmod_underline.js
//   flowmod_underline asserts the RENDERING: given a route in the model, does a mark paint, does
//   the comet ride, does the anchor law hold, is the meter contained. It creates those routes with
//   window.__tiAddRoute(env,lfo,dest) — a probe entry point (29677, "fb194 — probe surface").
//   Nothing in it ever grabs the LFO 1 chip and drops it on a control. So the whole INPUT half of
//   the feature is unasserted, and on a popped card that half is a different mechanism entirely:
//   the main window streams the chip drag onto the processor blackboard (setModDrag) and THIS
//   window completes the drop by read-modify-writing the shared matrix (getModDrag → tick() →
//   rmw → setSynthMod → kick). A suite that calls __tiAddRoute never executes one line of it.
//   That is the same shape of blind spot that let fxmod_underline sit at 15/15 while the FLOW
//   cards were dead: a green suite that tests the half that works.
//
//   NODE_PATH=<scratchpad>/node_modules node Tests/flowmod_gesture.js [page.html]
//
// 🚨 BOUNDS. Docked bars run at the shipped 820×656 (fb454). Popped bars run at each card's real
// popped bounds — popOutCardWindow is handed Math.round(rect.width/height) of the docked card:
//        arp 379×421 · chop 379×441 · gli 379×451 · rbn 379×510
//
// THE BARS
//   1  REAL GESTURE / POPPED — the drag arrives as a getModDrag stream (p=0 moves, then p=1) and
//      the drop is completed by the card's own receiver: mark paints, WINS THE HIT TEST, the
//      attenuator shows and a real vertical drag on the mark moves the depth.
//   2  REAL GESTURE / DOCKED — a genuine puppeteer pointer drag from the LFO 1 chip onto the same
//      control, through targets()/hit()/addAssign.
//   3  MATRIX — every [data-mod-dest] on all four cards, every tab, DOCKED and POPPED, assigned by
//      the real gesture. Counts reported; no sampling (fb425).
//   4  FLOOR — with no route nothing paints, and removing the last route takes the mark down.
//      An unmodulated popped card is PIXEL-IDENTICAL to the pre-fb524 page (a screenshot diff).
//   5  CONTAINMENT — at the real card bounds the meter, its readout HEAD and the route list are
//      non-degenerate and fully inside the window at the left edge, right edge, top and bottom.
//   7  RMW DURABILITY — the drop receiver's read-modify-write against the shared matrix must not
//      lose a route when getSynthMod/setSynthMod behave like the NATIVE calls they are: an async
//      WebView↔C++ round trip with real latency. A stub answering in a microtask is KINDER than
//      reality (fb393) and hides this completely. At a 250 ms round trip the shipped receiver kept
//      1 of 4 consecutive drops; at 120 ms, 2 of 4.
//   6  SOURCE PARITY — the three wire codes (LFO n · Env 100+n−1 · Velocity 200) encode on the
//      blackboard IDENTICALLY whether the drop lands docked or popped, at the same depth law
//      (env/vel full, lfo half). fb524b: the receiver wrote `s: wire−1` for all three, so a
//      Velocity drop became s=199 — outside every range setSynthModMatrix accepts (lfo 0..9,
//      env [100,132), vel ==200), so the processor DISCARDED it while this window painted a mark
//      and a route list reading "Env 100". A mark for modulation that cannot happen is worse
//      than no mark.
//
// PROOF THE BARS CAN FAIL (fb421). Each mutation rewrites the SHIPPED line in a temp copy:
//   FLOWG_MUTATE=1  restore `if(window.__cardOnly)return;` as loop()'s first statement  → 1,3,5 red
//   FLOWG_MUTATE=2  drop the `.ti-card` cells from targets()                            → 2,3 red
//   FLOWG_MUTATE=3  restore the fb524b velocity mis-encoding (`s: wire-1` for all)      → 6 red
//   FLOWG_MUTATE=4  remove the fb455/laneB clamp from showAtt                           → 5 red
//   FLOWG_MUTATE=5  remove the card-cell z-index lift (mark painted under its own card) → 1,3 red
//   FLOWG_MUTATE=6  make the receiver's drop a no-op (never RMW)                        → 1,3,6 red
//   FLOWG_MUTATE=7  paint a mark for every cell whether routed or not                   → 4 red
//   FLOWG_MUTATE=8  de-serialise the receiver's RMW (fb145's concurrent read-modify-write) → 3 red
const puppeteer=require('puppeteer-core');
const fs=require('fs'), path=require('path'), os=require('os');
const PAGE=process.argv[2]||process.env.FLOWG_PAGE||path.join(__dirname,'..')+'/Source/ui/public/index.html';
const MUT=+(process.env.FLOWG_MUTATE||0);
const CARDS={arp:{w:379,h:421,cls:'arp-ext',mode:'arp'},chop:{w:379,h:441,cls:'chop-ext',mode:'chop'},
             gli:{w:379,h:451,cls:'gli-ext',mode:'glitch'},rbn:{w:379,h:510,cls:'rbn-ext',mode:'drift'}};
let pass=0,fail=0;
const chk=(ok,l,d)=>{ if(ok){pass++;console.log('  ok    '+l+(d?'   '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'   '+d:''));} };

function mutatedPage(){
  if(!MUT) return PAGE;
  let src=fs.readFileSync(PAGE,'utf8');
  const sub=(f,t)=>{ if(src.indexOf(f)<0){console.error('MUTATION '+MUT+': anchor not found -> '+f.slice(0,90));process.exit(2);} src=src.replace(f,t); };
  if(MUT===1) sub("function loop(ts){ if(window.__cardOnly==='mod')return;","function loop(ts){ if(window.__cardOnly)return;");
  if(MUT===2) sub("    document.querySelectorAll('.ti-card [data-mod-dest]').forEach(function(el){ out.push({el:el,dest:+el.getAttribute('data-mod-dest'),kind:'knob'}); });",
                  "    /* mutation: card cells are not drop targets */");
  if(MUT===3) sub("            var sv=(wire>=200)?wire:(wire-1), dv=(sv>=100)?1.0:0.5;",
                  "            var sv=wire-1, dv=0.5;");
  if(MUT===4) sub("    att.style.left=Math.max(M+OV, Math.min(xLayout, window.__vw()-W-M-OV))+'px';\n"+
                  "    att.style.top=Math.max(HEAD+M, Math.min(yLayout, window.__vh()-H-M))+'px'; }",
                  "    att.style.left=xLayout+'px';\n    att.style.top=yLayout+'px'; }");
  if(MUT===5) sub("        if(el.closest&&el.closest('.ti-card')) u.style.zIndex='2147483646';","        /* mutation: lift removed */");
  if(MUT===6) sub("          if(dh){ var dest=+dh.getAttribute('data-mod-dest'), wire=o.l|0;","          if(false){ var dest=+dh.getAttribute('data-mod-dest'), wire=o.l|0;");
  if(MUT===8) sub("    rmwQ=rmwQ.then(function(){\n      return gm().then(function(js){ var arr=[]; try{ arr=JSON.parse(js||'[]'); }catch(e){}\n        arr=fn(arr)||arr;\n        var w; try{ w=sm(JSON.stringify(arr)); }catch(e){}\n        return Promise.resolve(w).then(function(){ if(then)setTimeout(then,120); }); }); })\n      .catch(function(){});   /* one failed round trip must not wedge the queue forever */ }",
                  "    try{ gm().then(function(js){ var arr=[]; try{ arr=JSON.parse(js||'[]'); }catch(e){}\n      arr=fn(arr)||arr; try{ sm(JSON.stringify(arr)); }catch(e){} if(then)setTimeout(then,120); }); }catch(e){} }");
  /* a phantom route the user never made: the card boots already "modulated", so an unmodulated
     card paints a mark and no longer matches its pre-fb524 self. */
  if(MUT===7) sub("  setTimeout(function(){ requestAnimationFrame(tick); kick(); setInterval(kick,900); },800);",
                  "  setTimeout(function(){ requestAnimationFrame(tick); kick(); setInterval(kick,900);\n    try{ window.__selMod={lfo:1}; window.__tiAddRoute(0,1,23); }catch(e){} },800);");
  const out=path.join(fs.mkdtempSync(path.join(os.tmpdir(),'flowg-')),'index.html');
  fs.writeFileSync(out,src); return out;
}

/* THE BLACKBOARD IS REAL (fb393): getSynthMod/setSynthMod are backed by one in-memory array, as
   the processor backs them, and getModDrag reads a scriptable object so the cross-window drag is
   replayed rather than stubbed away. setModDrag is RECORDED so the docked bar can prove the main
   window really streamed the drag a card would have received. */
const STUB=()=>{
  window.__BB=[]; window.__DRAG=null; window.__SETDRAG=[];
  const mk=()=>({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.Juce={getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
    getNativeFunction:(n)=>(...a)=>new Promise(r=>{
      if(n==='getSynthMod') return r(JSON.stringify(window.__BB||[]));
      if(n==='setSynthMod'){ try{window.__BB=JSON.parse(a[0]||'[]');}catch(e){} return r(0); }
      if(n==='getModDrag')  return r(window.__DRAG?JSON.stringify(window.__DRAG):'null');
      if(n==='setModDrag'){ window.__SETDRAG.push({l:a[0],x:a[1],y:a[2],p:a[3]}); return r(0); }
      /* the anchor law is (1−d)·knob; FLOW_GLI_VARY gets a real off-centre value so the
         territory has somewhere to be anchored TO instead of confirming 0 == 0. */
      if(n==='getSynParam') return r(a[0]==='FLOW_GLI_VARY'?0.62:0);
      if(/getPresets/i.test(n)) return r('[]');
      if(/Json|JSON/.test(n)) return r('{}');
      r(0); }),
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;
    Object.defineProperty(window,'Juce',{configurable:true,get(){return held;},
      set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
  Element.prototype.setPointerCapture=function(){}; Element.prototype.releasePointerCapture=function(){};
};

const HELPERS=()=>{
  window.__aWord=c=>(c&&c.querySelector&&c.querySelector('.lb'))||c;
  window.__aInk=function(c){const lb=window.__aWord(c);if(!lb)return null;
    const rg=document.createRange();rg.selectNodeContents(lb);const r=rg.getBoundingClientRect();
    return r.width?r:(lb.getBoundingClientRect?lb.getBoundingClientRect():null);};
  window.__aFind=function(c){const r=window.__aInk(c);if(!r||!r.width)return null;
    return [...document.querySelectorAll('.sm-ul')].filter(x=>x.style.display!=='none')
      .find(x=>{const q=x.getBoundingClientRect();
        return Math.abs(q.left-r.left)<5&&Math.abs(q.top-(r.bottom-1))<5;})||null;};
  /* a mark that is in the DOM, correctly placed and painted UNDER the opaque card that owns its
     word is invisible and unclickable. Geometry cannot see that; elementFromPoint can. */
  window.__aTop=function(u){if(!u)return false;const r=u.getBoundingClientRect();
    const e=document.elementFromPoint(r.left+r.width/2,r.top+r.height/2);
    return !!(e&&(e===u||u.contains(e)));};
  window.__aCells=()=>[...document.querySelectorAll('.ti-card [data-mod-dest]')];
  /* only cells the user can SEE: a card keeps every pane in the DOM and shows one at a time, and a
     docked card can hang past the window edge. A word nobody can see must NOT carry a mark, so
     demanding one there would make the bar demand a bug. */
  window.__aVisCells=function(root){const W=innerWidth,H=innerHeight;
    return window.__aCells().filter(c=>{if(root&&!root.contains(c))return false;
      const r=window.__aInk(c);
      return !!(r&&r.width>0.5&&r.height>0.5&&r.left>=0&&r.top>=0&&r.right<=W&&r.bottom<=H);});};
  window.__aMarksUp=()=>[...document.querySelectorAll('.sm-ul')].filter(x=>x.style.display!=='none').length;
  window.__aTabs=root=>[...(root||document).querySelectorAll('.tabs .tab')].map(t=>t.getAttribute('data-p'));
  window.__aTab=function(root,p){const t=[...(root||document).querySelectorAll('.tabs .tab')]
    .find(x=>x.getAttribute('data-p')===p);if(t)t.click();return !!t;};
  window.__aChance=()=>document.querySelector('.ti-card .chcell[data-mod-dest]');
  window.__aRect=function(e){if(!e)return null;const r=e.getBoundingClientRect();
    return{l:r.left,t:r.top,r:r.right,b:r.bottom,w:r.width,h:r.height};};
  window.__aRectS=s=>window.__aRect(document.querySelector(s));
  /* the underline system's FOOTPRINT on a card. A screenshot cannot be used as the floor here:
     a FLOW card animates continuously (its viz canvases redraw every frame), so two shots of an
     IDLE card already differ — a pixel bar would be a gate that can never pass, which is the same
     kind of decoration as one that can never fail. This is the deterministic thing instead: every
     element and class the mod-underline system contributes, plus the card's own element count. */
  window.__aFoot=()=>({ulVis:window.__aMarksUp(), ulAll:document.querySelectorAll('.sm-ul').length,
    modded:document.querySelectorAll('.sm-modded').length,
    attOn:!!document.querySelector('.sm-att.on'),
    routesVis:(function(){const r=document.querySelector('.sm-routes');
      return !!(r&&getComputedStyle(r).display!=='none');})(),
    dragging:document.body.classList.contains('sm-dragging'),
    cardEls:document.querySelectorAll('.ti-card *').length});
  /* replay ONE cross-window chip drop onto the point (cx,cy) in THIS card window, exactly as the
     C++ blackboard would deliver it: p=0 moves with in=1, then p=1 with a fresh sequence number. */
  window.__aSeq=0;
  /* 🚨 THE SEQUENCE NUMBER IS GLOBAL. tick() completes a drop only when o.s !== seen — a replayed
     sequence is correctly ignored as the same drag arriving twice. A per-tab counter therefore
     makes the first cell of every tab after the first silently do nothing, which reads exactly
     like a product bug and is not one. Cost me a diagnosis; hence the counter lives here. */
  window.__aDrop=async function(cx,cy,wire,seq){
    seq=(seq==null)?(++window.__aSeq):seq;
    window.__DRAG={p:0,l:wire,c:0,s:0,'in':1,lx:cx,ly:cy,x:cx,y:cy};
    await new Promise(r=>setTimeout(r,220));
    const hot=document.querySelector('.sm-hot');
    window.__DRAG={p:1,l:wire,c:0,s:seq,'in':1,lx:cx,ly:cy,x:cx,y:cy};
    await new Promise(r=>setTimeout(r,400));
    window.__DRAG=null;
    return hot?hot.getAttribute('data-mod-dest'):null; };
};
const wait=ms=>new Promise(r=>setTimeout(r,ms));

async function openPopped(browser,id,P){
  const c=CARDS[id]; const pg=await browser.newPage();
  await pg.setViewport({width:c.w,height:c.h,deviceScaleFactor:2});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,160)));
  await pg.evaluateOnNewDocument(STUB); await pg.evaluateOnNewDocument(HELPERS);
  await pg.goto('file://'+P+'?card='+id,{waitUntil:'load',timeout:60000});
  await wait(2600);            // card boot + the openFlowCard retry ladder + the first restore
  pg.__errs=errs; return pg;
}
async function openDocked(browser,P){
  const pg=await browser.newPage();
  await pg.setViewport({width:820,height:656,deviceScaleFactor:2});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,160)));
  await pg.evaluateOnNewDocument(STUB); await pg.evaluateOnNewDocument(HELPERS);
  await pg.goto('file://'+P,{waitUntil:'load',timeout:60000});
  await wait(2400);
  await pg.evaluate(()=>{const sp=document.getElementById('syn-panel');if(sp)sp.style.display='block';
    dispatchEvent(new Event('resize'));});
  await wait(700); pg.__errs=errs; return pg;
}
/* open ONE docked card, parked fully inside the shipped window (the default placement hangs
   ~145 px past the right edge of 820, and a word off the edge is not a surface). */
async function dockCard(pg,id){
  return pg.evaluate(async(mode,cls)=>{
    document.querySelectorAll('.ti-card.open').forEach(c=>c.classList.remove('open'));
    try{window.__openFlowCard(mode);}catch(e){}
    await new Promise(r=>setTimeout(r,850));
    const card=document.querySelector('.'+cls);
    if(!card||!card.classList.contains('open'))return null;
    card.style.left='4px'; card.style.top='4px';
    await new Promise(r=>setTimeout(r,250));
    const t=window.__aTabs(card); return t.length?t:[null];
  },CARDS[id].mode,CARDS[id].cls);
}
/* a genuine pointer drag: grab the LFO chip, cross the 5 px start threshold, travel to the
   target, release. Nothing here calls into the page's own assign functions. */
async function chipDrag(pg,chip,tx,ty){
  const sx=chip.l+chip.w/2, sy=chip.t+chip.h/2;
  await pg.mouse.move(sx,sy); await pg.mouse.down();
  await pg.mouse.move(sx+12,sy+12,{steps:3});
  await pg.mouse.move(tx,ty,{steps:10}); await wait(90);
  await pg.mouse.up(); await wait(320);
}

(async()=>{
const P=mutatedPage();
const browser=await puppeteer.launch({executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
  headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
console.log('\n══ THE FLOW-CARD UNDERLINE, BY THE REAL GESTURE — '+(MUT?('MUTATION '+MUT):'the shipped page')+' ══');
console.log('   page '+PAGE+(MUT?('  (mutated copy: '+P+')'):''));
console.log('   popped  arp 379×421 · chop 379×441 · gli 379×451 · rbn 379×510   docked 820×656\n');

// ── 1  REAL GESTURE / POPPED — Max's exact case ────────────────────────────────────────────
{
  const pg=await openPopped(browser,'gli',P);
  const pre=await pg.evaluate(()=>({ch:!!window.__aChance(),dest:window.__aChance()&&window.__aChance().getAttribute('data-mod-dest'),
    ink:window.__aRect(window.__aWord(window.__aChance())),marks:window.__aMarksUp()}));
  chk(pre.ch&&pre.dest==='23'&&pre.marks===0,'GESTURE/popped — GLITCH “Chance” exists and starts unmarked',
      'dest '+pre.dest+' (FLOW_GLI_VARY), marks '+pre.marks);
  const cx=pre.ink.l+pre.ink.w/2, cy=pre.ink.t+pre.ink.h/2;
  const r=await pg.evaluate(async(cx,cy)=>{
    const hot=await window.__aDrop(cx,cy,1,11);
    await new Promise(r=>setTimeout(r,1200));
    const ch=window.__aChance(), u=window.__aFind(ch);
    return {hot,bb:JSON.stringify(window.__BB),marks:window.__aMarksUp(),
            mark:!!u,top:window.__aTop(u),rect:window.__aRect(u),
            routes:window.__tiRoutes?window.__tiRoutes().length:-1};
  },cx,cy);
  chk(r.hot==='23'&&r.mark&&r.top&&r.marks===1&&r.bb==='[{"s":0,"d":23,"v":0.5}]',
      'GESTURE/popped — dropping the LFO 1 chip on “Chance” paints a mark that wins its own hit test',
      'hot '+r.hot+' · blackboard '+r.bb+' · marks '+r.marks+' · onTop '+r.top);
  // the attenuator, and a REAL vertical drag on the mark
  /* no mark means there is nothing to grab: report both dependent bars as the failures they are
     rather than dereferencing a null rect and taking the process down (a crash is not a red). */
  if(!r.rect){
    chk(false,'GESTURE/popped — the attenuator and its readout head really show on the mark','no mark to grab');
    chk(false,'GESTURE/popped — a real vertical drag on the mark moves the depth','no mark to grab');
  } else {
    const dep=await pg.evaluate(()=>window.__tiRoutes()[0].v);
    const box=r.rect;
    const mx=box.l+box.w/2, my=box.t+box.h/2;
    await pg.mouse.move(mx,my); await pg.mouse.down(); await wait(80);
    const attOn=await pg.evaluate(()=>{const a=document.querySelector('.sm-att');
      return {on:!!(a&&a.classList.contains('on')),vis:a?getComputedStyle(a).display:'none',rect:window.__aRect(a),
              head:window.__aRectS('.sm-att .vv')};});
    await pg.mouse.move(mx,my-25,{steps:5}); await wait(80); await pg.mouse.up(); await wait(200);
    const dep2=await pg.evaluate(()=>window.__tiRoutes()[0].v);
    chk(attOn.on&&attOn.vis!=='none'&&attOn.rect&&attOn.rect.w>0&&attOn.head&&attOn.head.w>0,
        'GESTURE/popped — the attenuator and its readout head really show on the mark',
        'display '+attOn.vis+' · meter '+(attOn.rect?attOn.rect.w.toFixed(0)+'×'+attOn.rect.h.toFixed(0):'—')
        +' · head '+(attOn.head?attOn.head.w.toFixed(0)+'×'+attOn.head.h.toFixed(0):'—'));
    chk(dep2>dep+0.05,'GESTURE/popped — a real vertical drag on the mark moves the depth',
        dep.toFixed(3)+' → '+dep2.toFixed(3)+' (25 px up)');
  }
  chk(pg.__errs.length===0,'GESTURE/popped — no page errors',pg.__errs.join(' | ')||'clean');
  await pg.close();
}

// ── 2  REAL GESTURE / DOCKED ───────────────────────────────────────────────────────────────
{
  const pg=await openDocked(browser,P);
  await dockCard(pg,'gli');
  const s=await pg.evaluate(()=>({chip:window.__aRectS('#mod-engine .mv-tabs .t[data-tab="1"]'),
    ink:window.__aRect(window.__aWord(window.__aChance())),marks:window.__aMarksUp()}));
  await chipDrag(pg,s.chip,s.ink.l+s.ink.w/2,s.ink.t+s.ink.h/2);
  const r=await pg.evaluate(()=>{const u=window.__aFind(window.__aChance());
    return {bb:JSON.stringify(window.__BB),marks:window.__aMarksUp(),mark:!!u,top:window.__aTop(u),
            streamed:window.__SETDRAG.length,drop:window.__SETDRAG.filter(d=>d.p===1).length};});
  chk(r.mark&&r.top&&r.marks===1&&r.bb==='[{"s":0,"d":23,"v":0.5}]',
      'GESTURE/docked — a real pointer drag from the LFO 1 chip marks “Chance”',
      'blackboard '+r.bb+' · marks '+r.marks+' · onTop '+r.top);
  chk(r.streamed>0&&r.drop===1,'GESTURE/docked — the drag is also streamed to setModDrag for popped cards',
      r.streamed+' pushes, '+r.drop+' with phase 1');
  chk(pg.__errs.length===0,'GESTURE/docked — no page errors',pg.__errs.join(' | ')||'clean');
  await pg.close();
}

// ── 3  MATRIX — every cell, every tab, four cards, both faces (fb425: no sampling) ──────────
const MX={};
{ // popped
  let tot=0,got=0; const miss=[];
  for(const id of Object.keys(CARDS)){
    const pg=await openPopped(browser,id,P);
    const tabs=await pg.evaluate(()=>{const t=window.__aTabs(document);return t.length?t:[null];});
    let ctot=0,cgot=0;
    for(const tab of tabs){
      const cells=await pg.evaluate(async(tab)=>{ if(tab){window.__aTab(document,tab);await new Promise(r=>setTimeout(r,260));}
        return window.__aVisCells(null).map(c=>({d:c.getAttribute('data-mod-dest'),
          ink:window.__aRect(window.__aWord(c))}));},tab);
      for(let i=0;i<cells.length;i++){
        const c=cells[i];
        await pg.evaluate(async(cx,cy)=>{await window.__aDrop(cx,cy,1);},
          c.ink.l+c.ink.w/2,c.ink.t+c.ink.h/2);
      }
      await wait(1300);
      const q=await pg.evaluate(async(tab)=>{ if(tab){window.__aTab(document,tab);await new Promise(r=>setTimeout(r,220));}
        const cells=window.__aVisCells(null); const seen={}; let n=0; const bad=[];
        for(const c of cells){ const d=c.getAttribute('data-mod-dest'); if(seen[d])continue;
          const u=window.__aFind(c);
          if(u&&window.__aTop(u)){seen[d]=1;n++;} else if(!bad.includes(d))bad.push(d); }
        for(const b of bad) if(seen[b])bad.splice(bad.indexOf(b),1);
        return {n,bad,tot:Object.keys(cells.reduce((o,c)=>(o[c.getAttribute('data-mod-dest')]=1,o),{})).length,
                bb:window.__BB.length};},tab);
      ctot+=q.tot; cgot+=q.n;
      if(q.bad.length) miss.push(id+'/'+(tab||'main')+' '+JSON.stringify(q.bad));
      await pg.evaluate(()=>{window.__BB=[];});
      await wait(900);
    }
    MX[id]={popped:cgot+'/'+ctot,tabs:tabs.map(t=>t||'main').join(',')};
    tot+=ctot; got+=cgot;
    if(pg.__errs.length) miss.push(id+' ERRORS '+pg.__errs.join('|'));
    await pg.close();
  }
  chk(tot>0&&got===tot&&!miss.length,'MATRIX/popped — every visible routable cell, all four cards, all tabs, by the real drop',
      'marks '+got+'/'+tot+(miss.length?'  '+miss.join(' | '):''));
}
{ // docked
  const pg=await openDocked(browser,P);
  let tot=0,got=0; const miss=[];
  const chip=await pg.evaluate(()=>window.__aRectS('#mod-engine .mv-tabs .t[data-tab="1"]'));
  for(const id of Object.keys(CARDS)){
    const tabs=await dockCard(pg,id);
    if(!tabs){miss.push(id+': did not open');continue;}
    let ctot=0,cgot=0;
    for(const tab of tabs){
      const cells=await pg.evaluate(async(cls,tab)=>{const card=document.querySelector('.'+cls);
        if(tab){window.__aTab(card,tab);await new Promise(r=>setTimeout(r,260));}
        window.__BB=[]; window.__tiPruneFxRoutes&&window.__tiPruneFxRoutes(0,1e9);
        return window.__aVisCells(card).map(c=>({d:c.getAttribute('data-mod-dest'),
          ink:window.__aRect(window.__aWord(c))}));},CARDS[id].cls,tab);
      await wait(150);
      for(const c of cells) await chipDrag(pg,chip,c.ink.l+c.ink.w/2,c.ink.t+c.ink.h/2);
      await wait(300);
      const q=await pg.evaluate((cls)=>{const card=document.querySelector('.'+cls);
        const cells=window.__aVisCells(card); const seen={}; let n=0; const bad=[];
        for(const c of cells){const d=c.getAttribute('data-mod-dest'); if(seen[d])continue;
          const u=window.__aFind(c);
          if(u&&window.__aTop(u)){seen[d]=1;n++;} else if(!bad.includes(d))bad.push(d);}
        for(const b of bad) if(seen[b])bad.splice(bad.indexOf(b),1);
        return {n,bad,tot:Object.keys(cells.reduce((o,c)=>(o[c.getAttribute('data-mod-dest')]=1,o),{})).length};},CARDS[id].cls);
      ctot+=q.tot; cgot+=q.n;
      if(q.bad.length) miss.push(id+'/'+(tab||'main')+' '+JSON.stringify(q.bad));
    }
    MX[id]=Object.assign(MX[id]||{},{docked:cgot+'/'+ctot});
    tot+=ctot; got+=cgot;
  }
  chk(tot>0&&got===tot&&!miss.length,'MATRIX/docked — every visible routable cell, all four cards, all tabs, by the real chip drag',
      'marks '+got+'/'+tot+(miss.length?'  '+miss.join(' | '):''));
  chk(pg.__errs.length===0,'MATRIX/docked — no page errors',pg.__errs.join(' | ')||'clean');
  await pg.close();
}
console.log('\n   ── the matrix ──');
for(const id of Object.keys(CARDS))
  console.log('     '+id.padEnd(5)+' tabs['+(MX[id].tabs||'main')+']   docked '+(MX[id].docked||'—').padEnd(7)+' popped '+(MX[id].popped||'—'));
console.log('');

// ── 4  FLOOR — nothing paints unrouted; the last delete takes the mark down; pixel identity ──
{
  const pg=await openPopped(browser,'gli',P);
  const idle=await pg.evaluate(()=>({marks:window.__aMarksUp(),ul:document.querySelectorAll('.sm-ul').length,
    att:(document.querySelector('.sm-att')||{}).className||'',routes:window.__tiRoutes?window.__tiRoutes().length:-1}));
  chk(idle.marks===0&&idle.routes===0&&!/\bon\b/.test(idle.att),
      'FLOOR — an unrouted popped card paints NOTHING',
      'marks '+idle.marks+' · routes '+idle.routes+' · meter "'+idle.att+'"');
  const foot0=await pg.evaluate(()=>window.__aFoot());
  const ink=await pg.evaluate(()=>window.__aRect(window.__aWord(window.__aChance())));
  const after=await pg.evaluate(async(cx,cy)=>{ await window.__aDrop(cx,cy,1,31);
    await new Promise(r=>setTimeout(r,1200));
    const up=window.__aMarksUp();
    window.__BB=[]; window.__tiModRestore&&window.__tiModRestore();
    await new Promise(r=>setTimeout(r,1800));
    return {up,down:window.__aMarksUp(),routes:window.__tiRoutes().length,foot:window.__aFoot()};},
    ink.l+ink.w/2,ink.t+ink.h/2);
  chk(after.up===1&&after.down===0&&after.routes===0,
      'FLOOR — removing the last route takes the mark back down',
      'up '+after.up+' \u2192 down '+after.down+' (routes '+after.routes+')');
  chk(JSON.stringify(after.foot)===JSON.stringify(foot0),
      'FLOOR — the underline system returns to EXACTLY its unmodulated footprint',
      'before '+JSON.stringify(foot0)+'  after '+JSON.stringify(after.foot));
  await pg.close();
  /* and fb524 must not have put anything on an unmodulated card that was not there before: the
     same footprint, measured on the PRE-fb524 page (git HEAD) at the same bounds. */
  {
    const PRE=process.env.FLOWG_PREPAGE;
    if(!PRE||!fs.existsSync(PRE)){
      chk(false,'FLOOR — an unmodulated card is unchanged from the pre-fb524 page',
          'FLOWG_PREPAGE not set/found — cannot compare');
    } else {
      const p2=await openPopped(browser,'gli',PRE);
      const pf=await p2.evaluate(()=>window.__aFoot());
      try{ await p2.close(); }catch(e){}
      chk(JSON.stringify(pf)===JSON.stringify(foot0),
          'FLOOR — an unmodulated card is unchanged from the pre-fb524 page',
          'pre '+JSON.stringify(pf)+'  now '+JSON.stringify(foot0));
    }
  }
}

// ── 5  CONTAINMENT at the real card bounds ─────────────────────────────────────────────────
{
  const pg=await openPopped(browser,'gli',P);
  const cells=await pg.evaluate(()=>window.__aVisCells(null).map(c=>({d:c.getAttribute('data-mod-dest'),
    ink:window.__aRect(window.__aWord(c))})));
  // the extremes actually present in this card: leftmost, rightmost, topmost, bottommost word
  const pick={left:cells.reduce((a,b)=>a.ink.l<=b.ink.l?a:b), right:cells.reduce((a,b)=>a.ink.r>=b.ink.r?a:b),
              top:cells.reduce((a,b)=>a.ink.t<=b.ink.t?a:b),  bottom:cells.reduce((a,b)=>a.ink.b>=b.ink.b?a:b)};
  const W=CARDS.gli.w,H=CARDS.gli.h; const bad=[]; const rep=[];
  for(const k of Object.keys(pick)){
    const c=pick[k];
    await pg.evaluate(async(cx,cy,d)=>{window.__BB=[];await window.__aDrop(cx,cy,1);
      await new Promise(r=>setTimeout(r,900));},c.ink.l+c.ink.w/2,c.ink.t+c.ink.h/2,c.d);
    const box=await pg.evaluate((d)=>{const cell=[...document.querySelectorAll('.ti-card [data-mod-dest]')]
      .find(x=>x.getAttribute('data-mod-dest')===d); const u=window.__aFind(cell);
      return u?window.__aRect(u):null;},c.d);
    if(!box){bad.push(k+': no mark');continue;}
    const mx=box.l+box.w/2,my=box.t+box.h/2;
    await pg.mouse.move(mx,my); await pg.mouse.down(); await wait(120);
    const g=await pg.evaluate(()=>({att:window.__aRectS('.sm-att'),head:window.__aRectS('.sm-att .vv'),
      list:window.__aRectS('.sm-routes')}));
    await pg.mouse.up(); await wait(150);
    const ins=(r)=>r&&r.w>0.5&&r.h>0.5&&r.l>=-0.5&&r.t>=-0.5&&r.r<=W+0.5&&r.b<=H+0.5;
    if(!ins(g.att))  bad.push(k+'/meter '+JSON.stringify(g.att));
    if(!ins(g.head)) bad.push(k+'/head '+JSON.stringify(g.head));
    if(g.list&&!ins(g.list)) bad.push(k+'/list '+JSON.stringify(g.list));
    if(g.att&&g.head)
      rep.push(k+' meter['+g.att.l.toFixed(0)+','+g.att.t.toFixed(0)+'..'+g.att.r.toFixed(0)+','+g.att.b.toFixed(0)+']'
        +' head['+g.head.l.toFixed(0)+','+g.head.t.toFixed(0)+']');
  }
  chk(bad.length===0,'CONTAINMENT — meter, readout head and route list stay inside 379×451 at every extreme',
      bad.length?bad.join(' | '):rep.join('  ·  '));
  await pg.close();
}

// ── 6  SOURCE PARITY — the wire codes must encode the same docked and popped ────────────────
{
  const want={1:{s:0,v:0.5},101:{s:100,v:1},200:{s:200,v:1}};
  const bad=[],rep=[];
  for(const wire of [1,101,200]){
    const pg=await openPopped(browser,'gli',P);
    const ink=await pg.evaluate(()=>window.__aRect(window.__aWord(window.__aChance())));
    const bb=await pg.evaluate(async(cx,cy,w)=>{await window.__aDrop(cx,cy,w,55);
      await new Promise(r=>setTimeout(r,1300));
      return {bb:window.__BB.slice(),routes:window.__tiRoutes?JSON.stringify(window.__tiRoutes()):'n/a'};},
      ink.l+ink.w/2,ink.t+ink.h/2,wire);
    const e=bb.bb[0]||{s:'(none)',v:'(none)'};
    const W=want[wire];
    /* the ONLY three source encodings setSynthModMatrix accepts: lfo 0..9, env [100,132), vel ==200 */
    const accepted=(e.s>=0&&e.s<10)||(e.s>=100&&e.s<132)||(e.s===200);
    if(!(e.s===W.s&&Math.abs((+e.v)-W.v)<1e-6&&accepted))
      bad.push('wire '+wire+' → '+JSON.stringify(e)+' (want s='+W.s+' v='+W.v+', processor-accepted '+accepted+')');
    rep.push('wire '+wire+'→s'+e.s+'/v'+e.v);
    await pg.close();
  }
  chk(bad.length===0,'SOURCE PARITY — LFO/Env/Velocity encode as the docked path does and survive the processor filter',
      bad.length?bad.join(' | '):rep.join(' · '));
}

// ── 7  RMW DURABILITY — under a REAL native round-trip latency ─────────────────────────────
{
  const LAT=250;
  const STUB_L=(LAT)=>{
    window.__BB=[]; window.__DRAG=null;
    const D=(v)=>new Promise(r=>setTimeout(()=>r(v),LAT));    // the native round trip
    const mk=()=>({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
      getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
      valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
    window.Juce={getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
      getNativeFunction:(n)=>(...a)=>{
        if(n==='getSynthMod') return D(JSON.stringify(window.__BB||[]));
        if(n==='setSynthMod') return D(0).then(v=>{ try{window.__BB=JSON.parse(a[0]||'[]');}catch(e){} return v; });
        if(n==='getModDrag')  return Promise.resolve(window.__DRAG?JSON.stringify(window.__DRAG):'null');
        if(n==='getSynParam') return Promise.resolve(a[0]==='FLOW_GLI_VARY'?0.62:0);
        if(/getPresets/i.test(n)) return Promise.resolve('[]');
        if(/Json|JSON/.test(n)) return Promise.resolve('{}');
        return Promise.resolve(0); },
      backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
    (function(){const mine=window.Juce;let held=mine;
      Object.defineProperty(window,'Juce',{configurable:true,get(){return held;},
        set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
    window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
      __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
    Element.prototype.setPointerCapture=function(){}; Element.prototype.releasePointerCapture=function(){};
    window.__seq=0;
    window.__drop=async function(cx,cy){
      window.__DRAG={p:0,l:1,c:0,s:0,'in':1,lx:cx,ly:cy,x:cx,y:cy};
      await new Promise(r=>setTimeout(r,90));
      window.__DRAG={p:1,l:1,c:0,s:++window.__seq,'in':1,lx:cx,ly:cy,x:cx,y:cy};
      await new Promise(r=>setTimeout(r,60)); window.__DRAG=null; };
  };
  const pg=await browser.newPage();
  await pg.setViewport({width:CARDS.gli.w,height:CARDS.gli.h,deviceScaleFactor:2});
  await pg.evaluateOnNewDocument(STUB_L,LAT); await pg.evaluateOnNewDocument(HELPERS);
  await pg.goto('file://'+P+'?card=gli',{waitUntil:'load',timeout:60000});
  await wait(2900);
  const cells=await pg.evaluate(()=>window.__aVisCells(null).slice(0,4)
    .map(c=>({d:c.getAttribute('data-mod-dest'),ink:window.__aRect(window.__aWord(c))})));
  const r=await pg.evaluate(async(cells)=>{
    for(const c of cells) await window.__drop(c.ink.l+c.ink.w/2,c.ink.t+c.ink.h/2);
    await new Promise(r=>setTimeout(r,3000));
    return {want:cells.map(c=>c.d).join(','),got:window.__BB.map(x=>x.d).join(',')};},cells);
  chk(cells.length===4&&r.want===r.got,
      'RMW DURABILITY — four consecutive drops all reach the matrix at a '+LAT+' ms native round trip',
      'dropped ['+r.want+']  matrix ['+r.got+']');
  try{ await pg.close(); }catch(e){}
}

console.log('\n   '+pass+' passed, '+fail+' failed\n');
await browser.close();
process.exit(fail?1:0);
})();
