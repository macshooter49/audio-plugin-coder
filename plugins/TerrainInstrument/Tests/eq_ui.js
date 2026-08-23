// ══════════════════════════════════════════════════════════════════════════════════════════════
//  eq_ui.js — fb468. THE EQUALIZER AUDIT, as gates.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/eq_ui.js
//
//  Max, in one message: "I can't even create notches anymore... can't double click to add bands...
//  can't right click to delete... the bands get stuck... I hover my mouse over the band and scroll
//  and it scrolls me over to the next effect... one band fucks with the other band... I can't even
//  make a low cut or a high cut."
//
//  The audit found EIGHT complaints and FIVE causes. This file is one gate per cause, each written
//  to FAIL on the tree that produced the complaint:
//
//   C1  fb451 (13eadae, "the wheel no longer eats the rack's scroll") deleted the EQ's 11-line
//       wheel->Q handler along with the Splitter's and the Bode's. The EQ's already released the
//       event when the pointer was not on a dot, so it was collateral. Per-band Q has been
//       unreachable ever since: qSet() has three callers and all three pass the neutral 50. The
//       ONLY width control left is the GLOBAL Trait knob, which moves all eight bands at once —
//       which is why "one band affects another" is AUDIBLY true, not just visually.
//   C2  The node's cy is drawn from the SUMMED, MIX-SCALED curve, not from the band's own gain, so
//       (a) cy is a pure function of cx and two bands at the same frequency sit at distance 0.0000,
//       (b) moving one band visibly moves another's dot, and (c) at Mix 50 % the dot travels a
//       fraction of the cursor's distance and "sticks".
//   C3  pick() takes the nearest dot with a STRICT <, so a tie always resolves to the lowest
//       data-b and the higher band becomes permanently ungrabbable.
//   C4  The Air/Reach band's range runs to 40 kHz while the plot clamps at 20 kHz, so the top
//       36.5 % of its travel draws at one pixel column and cannot be dragged.
//   C5  window.__tiToast is referenced twice and defined NOWHERE, so "8 bands max" never appears
//       and a double-click with all four free bells already on is a silent no-op.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const P = process.env.EQ_UI_PAGE || '/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Source/ui/public/index.html';
let pass=0, fail=0;
function chk(ok,label,detail){ if(ok){pass++; console.log('  ok    '+label+(detail?'   '+detail:''));}
  else {fail++; console.log('  FAIL  '+label+(detail?'   '+detail:''));} }

(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:1560,height:1200,deviceScaleFactor:2});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,160)));
  await pg.evaluateOnNewDocument(() => {
    window.__PMAP = {};
    const mk=()=>({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
      getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
      valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
    window.Juce={getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
      getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
        if(/Json|JSON/.test(n))return r('{}'); if(n==='getSynParam') return r((window.__PMAP[a[0]]!=null)?window.__PMAP[a[0]]:0); r(0);}),
      backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
    (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
      get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
    window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
      __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
  });
  await pg.goto('file://'+P,{waitUntil:'load',timeout:60000});
  await new Promise(r=>setTimeout(r,1600));
  await pg.evaluate(()=>{ const sp=document.getElementById('syn-panel'); if(sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
  await new Promise(r=>setTimeout(r,1200));

  console.log('\n══ fb468 — THE EQUALIZER AUDIT ══\n');
  chk(errs.length===0,'the page runs with no errors', errs.length?errs.slice(0,2).join(' | '):'0 page errors');

  // one EQ, and a feed that says where each band IS (the engine publishes hz[], db[], q[], on[])
  const setup = await pg.evaluate(()=>{
    window.__W=[]; const orig=window.__setSynParam;
    window.__setSynParam=function(id,v){ window.__W.push([id,v]); try{ return orig&&orig(id,v); }catch(e){} };
    window.__fxAdd('eqz');
    const curve=new Array(192).fill(0);
    window.__EQFEED={lvl:0.8,hz:[100,550,3100,15500,632,632,632,632],db:[0,0,0,0,0,0,0,0],
                     q:[1,1,1,1,1,1,1,1],on:[1,1,1,1,0,0,0,0],curve};
    window.__fx4VizPush={eqz:[window.__EQFEED,null,null,null,null,null]};
    for(let i=0;i<8;i++) window.__fx4Tick();
    return {n:document.querySelectorAll('.fxr-core[data-core="eqz"] .eqz-n').length};
  });
  chk(setup.n===8,'the EQ card renders its 8 nodes','nodes='+setup.n);

  // ── C1 — the wheel ────────────────────────────────────────────────────────────────────────
  const r1 = await pg.evaluate(()=>{
    const core=document.querySelector('.fxr-core[data-core="eqz"]'), svg=core.querySelector('svg');
    const rc=()=>svg.getBoundingClientRect();
    const at=(b)=>{const n=core.querySelector('.eqz-n[data-b="'+b+'"]'), r=rc();
      return [r.left+(+n.getAttribute('cx'))/226*r.width, r.top+(+n.getAttribute('cy'))/78*r.height];};
    const out={};
    // (a) wheel ON the Body dot: writes BODYQ and nothing else, and is consumed
    const [bx,by]=at(1); window.__W.length=0;
    const w=new WheelEvent('wheel',{bubbles:true,cancelable:true,clientX:bx,clientY:by,deltaY:-100});
    core.dispatchEvent(w);
    out.nodeWrites=[...new Set(window.__W.map(x=>x[0]))].sort().join(',');
    out.nodePrevented=w.defaultPrevented;
    // (b) wheel on the EMPTY grid: no writes, NOT consumed — the rack must still pan
    const r=rc(); const gx=r.left+0.90*r.width, gy=r.top+0.12*r.height; window.__W.length=0;
    const w2=new WheelEvent('wheel',{bubbles:true,cancelable:true,clientX:gx,clientY:gy,deltaY:-100});
    core.dispatchEvent(w2);
    out.gridWrites=window.__W.length; out.gridPrevented=w2.defaultPrevented;
    return out;
  });
  chk(/BODYQ/.test(r1.nodeWrites)&&r1.nodeWrites.split(',').length===1&&r1.nodePrevented===true,
      'C1  wheel over a DOT sets THAT band\'s Q, and only that','writes='+(r1.nodeWrites||'none')+' consumed='+r1.nodePrevented);
  chk(r1.gridWrites===0&&r1.gridPrevented===false,
      'C1  wheel over the GRID is left to the rack\'s scroll (fb451 stands)','writes='+r1.gridWrites+' consumed='+r1.gridPrevented);

  // ── C2 — the dot is drawn at its OWN gain ─────────────────────────────────────────────────
  const r2 = await pg.evaluate(()=>{
    const core=document.querySelector('.fxr-core[data-core="eqz"]'); const out={};
    const cy=(b)=>+core.querySelector('.eqz-n[data-b="'+b+'"]').getAttribute('cy');
    // Low and Body BOTH at 300 Hz, with OPPOSITE gains. Their dots must not coincide.
    window.__EQFEED.hz=[300,300,3100,15500,632,632,632,632];
    window.__EQFEED.db=[20,-20,0,0,0,0,0,0];
    // a summed curve that is FLAT (the two bells cancel) — the old drawer put both dots on it
    window.__EQFEED.curve=new Array(192).fill(0);
    for(let i=0;i<8;i++) window.__fx4Tick();
    out.lowCy=cy(0); out.bodyCy=cy(1); out.sameSpot=Math.abs(cy(0)-cy(1))<0.5;
    // moving BODY must not move LOW's dot. The curve is given a SHAPE that changes with Body — that is
    // what a real summed response does, and it is the only way this gate can fail on the old drawer
    // (which read the node's cy straight off that curve).
    const shape=(amp)=>{const c=new Array(192).fill(0); for(let i=0;i<192;i++) c[i]=amp*Math.exp(-Math.pow((i-96)/28,2)); return c;};
    window.__EQFEED.curve=shape(-20); for(let i=0;i<8;i++) window.__fx4Tick();
    const low0=cy(0);
    window.__EQFEED.db=[20,-28,0,0,0,0,0,0]; window.__EQFEED.curve=shape(-28);
    for(let i=0;i<8;i++) window.__fx4Tick();
    out.lowMovedByBody=Math.abs(cy(0)-low0);
    return out;
  });
  chk(!r2.sameSpot,'C2  two bands at the SAME Hz with opposite gains draw apart','low cy='+r2.lowCy+' body cy='+r2.bodyCy);
  chk(r2.lowMovedByBody<0.25,'C2  moving one band does NOT move another band\'s dot','Low moved '+r2.lowMovedByBody.toFixed(2)+' units when Body did');

  // ── C3 — you grab the band you are pointing at ────────────────────────────────────────────
  const r3 = await pg.evaluate(()=>{
    const core=document.querySelector('.fxr-core[data-core="eqz"]'), svg=core.querySelector('svg');
    const r=svg.getBoundingClientRect(); const out={};
    const n=core.querySelector('.eqz-n[data-b="1"]');
    const bx=r.left+(+n.getAttribute('cx'))/226*r.width, by=r.top+(+n.getAttribute('cy'))/78*r.height;
    const ev=(t,x,y)=>new PointerEvent(t,{bubbles:true,cancelable:true,clientX:x,clientY:y,pointerId:9,pointerType:'mouse',buttons:1,button:0});
    window.__W.length=0;
    core.dispatchEvent(ev('pointerdown',bx,by)); core.dispatchEvent(ev('pointermove',bx,by-10)); document.dispatchEvent(ev('pointerup',bx,by-10));
    out.grabbed=[...new Set(window.__W.map(x=>x[0]))].sort().join(',');
    return out;
  });
  chk(/BODY/.test(r3.grabbed)&&!/LOW/.test(r3.grabbed),
      'C3  pressing on the Body dot grabs BODY, not the band underneath it', r3.grabbed||'nothing');

  // the engine's own mapping, mirrored so the stub can answer the way the plugin answers
  await pg.evaluate(()=>{
    const LO=[20,100,700,6000], HI=[500,3000,14000,20000];
    window.__syncFeed=function(d){
      const F=window.__EQFEED;
      for(let b=0;b<4;b++){
        const t=(b<3?d.back.knobs[2*b][1]:d.back.knobs[6][1])/100;
        F.hz[b]=LO[b]*Math.pow(HI[b]/LO[b],t);
        const g=(b<3?d.back.knobs[2*b+1][1]:d.knobs[1].v);
        F.db[b]=(g/100*2-1)*30;
      }
      for(let k=0;k<4;k++){ F.hz[4+k]=20*Math.pow(1000,d.xb[k][0]/100); F.db[4+k]=(d.xb[k][1]/100*2-1)*30; F.on[4+k]=d.xb[k][2]?1:0; }
    };
  });

  // ── C2b — the dot tracks the cursor at Mix 50 % ───────────────────────────────────────────
  const r4 = await pg.evaluate(()=>{
    const core=document.querySelector('.fxr-core[data-core="eqz"]'), svg=core.querySelector('svg');
    const D=window.__fxDevs(), d=D.find(z=>z.core==='eqz'); d.knobs[3].v=50;   // Mix 50 %
    window.__EQFEED.curve=new Array(192).fill(0);
    window.__syncFeed(d); for(let i=0;i<10;i++) window.__fx4Tick();   // let the glide SETTLE on the model
    const n=core.querySelector('.eqz-n[data-b="1"]');
    const r=svg.getBoundingClientRect(), pxPerUnit=r.height/78;
    const cy0=+n.getAttribute('cy');
    const bx=r.left+(+n.getAttribute('cx'))/226*r.width, by=r.top+cy0*pxPerUnit;
    const ev=(t,x,y)=>new PointerEvent(t,{bubbles:true,cancelable:true,clientX:x,clientY:y,pointerId:11,pointerType:'mouse',buttons:1,button:0});
    const DY=20;   // move the cursor 20 CSS px up
    core.dispatchEvent(ev('pointerdown',bx,by)); core.dispatchEvent(ev('pointermove',bx,by-DY));
    // the model moved; feed the engine's answer back the way the plugin would, then release
    window.__syncFeed(d); for(let i=0;i<10;i++) window.__fx4Tick();
    document.dispatchEvent(ev('pointerup',bx,by-DY));
    const cy1=+core.querySelector('.eqz-n[data-b="1"]').getAttribute('cy');
    return {movedUnits:(cy0-cy1), wantUnits:DY/pxPerUnit};
  });
  chk(Math.abs(r4.movedUnits-r4.wantUnits)<1.2,
      'C2  at Mix 50 % the dot follows the cursor 1:1 (it used to crawl)',
      'cursor '+r4.wantUnits.toFixed(2)+' units, dot '+r4.movedUnits.toFixed(2));

  // ── C4 — the whole of Reach's travel is drawable and draggable ────────────────────────────
  const r5 = await pg.evaluate(()=>{
    const core=document.querySelector('.fxr-core[data-core="eqz"]'), svg=core.querySelector('svg');
    const D=window.__fxDevs(), d=D.find(z=>z.core==='eqz');
    window.__syncFeed(d); for(let i=0;i<10;i++) window.__fx4Tick();   // the dot must start where the MODEL is
    const r=svg.getBoundingClientRect();
    const ev=(t,x,y)=>new PointerEvent(t,{bubbles:true,cancelable:true,clientX:x,clientY:y,pointerId:13,pointerType:'mouse',buttons:1,button:0});
    const n=core.querySelector('.eqz-n[data-b="3"]');
    const bx=r.left+(+n.getAttribute('cx'))/226*r.width, by=r.top+(+n.getAttribute('cy'))/78*r.height;
    const v0=d.knobs[1].v;   // Air/Reach lives on front knob 1
    core.dispatchEvent(ev('pointerdown',bx,by));
    core.dispatchEvent(ev('pointermove',bx+8,by)); const vNear=d.back.knobs[6][1];
    core.dispatchEvent(ev('pointermove',bx+600,by)); const vFar=d.back.knobs[6][1];
    document.dispatchEvent(ev('pointerup',bx+600,by));
    return {v0:d.back.knobs[6][1], vNear, vFar, gained:(vFar-vNear)};
  });
  // ⚠️ the first version of this gate asked only that Reach MOVED (+2.0), and it passed on the broken
  //    tree: Reach climbed 55.56 -> 63.50 and stopped dead. 63.50 is exactly where 20 kHz sits inside a
  //    6 k..40 k range — the freeze itself. The gate has to ask that the drag can reach the TOP.
  chk(r5.vFar>95,'C4  dragging Reach right can reach the TOP of its range (it used to freeze at 63.5)',
      'after 8 px='+(+r5.vNear).toFixed(2)+', after 200 px='+(+r5.vFar).toFixed(2));

  // ── C5 — the "no free bands left" message exists ──────────────────────────────────────────
  const r6 = await pg.evaluate(()=>({ toast: typeof window.__tiToast }));
  chk(r6.toast==='function','C5  the toast the EQ calls when it runs out of bands is DEFINED','typeof __tiToast = '+r6.toast);

  console.log('\n  PASS '+pass+'   FAIL '+fail+'\n');
  await b.close();
  process.exit(fail?1:0);
})();
