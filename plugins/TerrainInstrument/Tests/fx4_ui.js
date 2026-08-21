// fb437 — the UI gate for EQUALIZER · WIDEN · COMPRESS · MULTIBAND (chain kinds 9-12).
//
// The law this exists for (fb373 / fb381 / fb413): a green DSP harness proves the ENGINE works and
// never that the plugin REACHES it, and a clean build proves nothing about whether the page RUNS.
// fb435 wired the four engines and declared them "in the + menu" — but CORES had no entry for any
// of them and devHTML() calls CORES[d.core]() unguarded, so adding one threw inside the rack's
// try/catch and no card ever rendered. This gate would have failed on that tree; it MUST fail on it.
//
// It drives the real page: adds eight of each (cap six), checks every core's markup is there, pushes
// one frame of the C++ viz shape and ticks (every window must redraw from it), drags an EQ node and
// a Multiband crossover with synthetic pointer events and watches the params get written, scrolls
// for Trait, checks the readout + relabel + strobe + route-pill laws, and exercises the generic
// restore with a stub that answers per-id (decode by cardinality, fb373).
//
//   NODE_PATH=<scratchpad>/node_modules node Tests/fx4_ui.js            (page = Source/ui/public/index.html)
//   FX4_UI_PAGE=/path/to/old/index.html node Tests/fx4_ui.js             (to prove it FAILS on the old tree)
const puppeteer = require('puppeteer-core');
const P = process.env.FX4_UI_PAGE || '/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Source/ui/public/index.html';

let pass=0, fail=0;
function chk(ok,label,detail){ if(ok){pass++; console.log('  ok    '+label+(detail?'   '+detail:''));}
  else {fail++; console.log('  FAIL  '+label+(detail?'   '+detail:''));} }

(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:1560,height:1200,deviceScaleFactor:2});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,160)));
  await pg.evaluateOnNewDocument(() => {
    // fb393 law: the stub is as DEAD as the backend — getters return constants, setters do nothing.
    // The one exception is getSynParam, which answers from window.__PMAP (default 0) so the
    // RESTORE path can be exercised with known values; it still stores nothing on its own.
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

  console.log('\n══ fb437 — EQUALIZER · WIDEN · COMPRESS · MULTIBAND: the UI gate ══\n');
  chk(errs.length===0, 'the page runs with no errors at all', errs.length?errs.slice(0,2).join(' | '):'0 page errors');
  chk(await pg.evaluate(()=>typeof window.__fxAdd==='function'), 'the rack module is ALIVE (__fxAdd was assigned)');

  // ── add 8 of each: duplicatable, capped at 6; every card must RENDER its core
  const r1 = await pg.evaluate(()=>{
    const out={counts:{},cores:{},addErr:null};
    // record every param write the UI makes — the only proof the drag reaches the DSP path
    window.__W=[]; const orig=window.__setSynParam; window.__setSynParam=function(id,v){ window.__W.push([id,v]); try{ return orig&&orig(id,v); }catch(e){} };
    for(const core of ['eqz','wid','cmp','ott']) for(let i=0;i<8;i++){ try{ window.__fxAdd(core); }catch(e){ out.addErr=core+': '+String(e).slice(0,100); } }
    const D=window.__fxDevs?window.__fxDevs():[];
    for(const core of ['eqz','wid','cmp','ott']){
      out.counts[core]=D.filter(d=>d.core===core).length;
      out.cores[core]=document.querySelectorAll('.fxr-dev .fxr-core[data-core="'+core+'"] svg').length;
    }
    out.cards=document.querySelectorAll('.fxr-dev').length;
    out.mark={ eqzCurve:document.querySelectorAll('.fxr-core[data-core="eqz"] .eqz-curve.dst-curve').length,
               eqzNodes:document.querySelectorAll('.fxr-core[data-core="eqz"] .eqz-n').length,
               ottLanes:document.querySelectorAll('.fxr-core[data-core="ott"] .ott-lane').length,
               ottX:document.querySelectorAll('.fxr-core[data-core="ott"] .ott-xl').length,
               cmpKnee:document.querySelectorAll('.fxr-core[data-core="cmp"] .cmp-knee.dst-curve').length,
               widV:document.querySelectorAll('.fxr-core[data-core="wid"] .wid-v').length,
               eqzPill:(document.querySelector('.fxr-core[data-core="eqz"]')||{}).closest ? [...document.querySelector('.fxr-core[data-core="eqz"]').closest('.fxr-dev').querySelectorAll('.fxr-pill .fxr-t')].map(e=>e.textContent).join(',') : '' };
    return out; });
  chk(!r1.addErr, 'adding the four devices throws nothing', r1.addErr||'');
  chk(r1.cards===24, 'eight of each ADD, capped at six each (24 cards)', 'cards='+r1.cards+' '+JSON.stringify(r1.counts));
  for(const core of ['eqz','wid','cmp','ott']) chk(r1.cores[core]===6, core+': every card renders its CORE (CORES['+core+'] exists)', 'svg cores='+r1.cores[core]);
  chk(r1.mark.eqzCurve===6 && r1.mark.eqzNodes===48, 'Equalizer core = the house line + 8 nodes per card (4 roles + 4 free bells, fb438)', JSON.stringify({curve:r1.mark.eqzCurve,nodes:r1.mark.eqzNodes}));
  chk(r1.mark.ottLanes===18 && r1.mark.ottX===12, 'Multiband core = 3 lanes + 2 crossover lines per card', JSON.stringify({lanes:r1.mark.ottLanes,x:r1.mark.ottX}));
  chk(r1.mark.cmpKnee===6 && r1.mark.widV===48, 'Compress knee (house line) + Widen 8 voice beams per card', JSON.stringify({knee:r1.mark.cmpKnee,beams:r1.mark.widV}));
  chk(r1.mark.eqzPill==='Delta', 'the Equalizer has its Delta pill', r1.mark.eqzPill);

  // ── one frame of the REAL push shape, then tick: every window must redraw from it
  const r2 = await pg.evaluate(()=>{
    const curve=[]; for(let i=0;i<96;i++){ const hz=20*Math.pow(10,3*i/95); curve.push(-6*Math.exp(-Math.pow(Math.log(hz/90)/0.6,2))+5*Math.exp(-Math.pow(Math.log(hz/550)/0.5,2))+9/(1+Math.pow(12000/hz,2.2))); }
    const knee=[]; for(let q=0;q<32;q++){ const x=-60+72*q/31; knee.push(x<-18?x:-18+(x+18)*0.25); }
    const six=(o)=>[o,o,o,o,o,o];
    window.__fx4VizPush={ eqz:six({lvl:0.8,hz:[90,550,3100,17000],db:[-6,5,0,9],curve}),
      wid:six({corr:0.4,nV:6,pan:[-0.3,0.3,-0.5,0.5,-0.7,0.7],cents:[5,-5,8,-8,12,-12],width:0.4,lvl:0.8}),
      cmp:six({gr:7,in:-12,out:-19,thr:-18,ratio:4,atk:12,rel:140,kneeDb:6,lvl:0.8,knee}),
      ott:six({nb:3,lvl:0.8,x:[120,2500],gr:[3.5,-4,-7],lv:[-22,-28,-36],tdn:[-20,-18,-24],tup:[-34,-33,-40]}) };
    const flat=document.querySelector('.fxr-core[data-core="eqz"] .eqz-curve').getAttribute('d');
    const kneeBefore=document.querySelector('.fxr-core[data-core="cmp"] .cmp-knee').getAttribute('d');
    for(let i=0;i<6;i++) window.__fx4Tick();
    const out={};
    out.eqzMoved=document.querySelector('.fxr-core[data-core="eqz"] .eqz-curve').getAttribute('d')!==flat;
    const nodes=[...document.querySelectorAll('.fxr-core[data-core="eqz"] .eqz-n')].slice(0,4);
    out.nodeCx=nodes.map(n=>+n.getAttribute('cx')); out.nodeCy=nodes.map(n=>+n.getAttribute('cy'));
    // the node must sit ON the drawn line: sample the curve path at the node's x
    const d=document.querySelector('.fxr-core[data-core="eqz"] .eqz-curve').getAttribute('d');
    const pts=d.replace(/[ML]/g,' ').trim().split(/\s+/).map(Number); const xs=[],ys=[]; for(let i=0;i<pts.length;i+=2){xs.push(pts[i]);ys.push(pts[i+1]);}
    out.nodeOnLine=nodes.map(n=>{ const x=+n.getAttribute('cx'), y=+n.getAttribute('cy'); let best=1e9; for(let i=0;i<xs.length;i++){ if(Math.abs(xs[i]-x)<1.2) best=Math.min(best,Math.abs(ys[i]-y)); } return best; });
    out.nodeFill=getComputedStyle(nodes[1]).fill;
    const col=document.querySelector('.fxr-core[data-core="ott"] .ott-col'); out.ottColH=+col.getAttribute('height');
    const edn=document.querySelector('.fxr-core[data-core="ott"] .ott-edge-dn'); out.ottEdgeY=+edn.getAttribute('y1');
    const xl=[...document.querySelectorAll('.fxr-core[data-core="ott"] .ott-xl')].slice(0,2); out.ottX=xl.map(l=>+l.getAttribute('x1'));
    out.kneeMoved=document.querySelector('.fxr-core[data-core="cmp"] .cmp-knee').getAttribute('d')!==kneeBefore;
    out.press=+document.querySelector('.fxr-core[data-core="cmp"] .cmp-press').getAttribute('height');
    const beams=[...document.querySelectorAll('.fxr-core[data-core="wid"] .wid-v')].slice(0,8);
    out.beamsLit=beams.filter(l=>+l.getAttribute('opacity')>0.2).length; out.beamSpread=beams.slice(0,6).map(l=>+l.getAttribute('x2')).filter(x=>Math.abs(x-113)>3).length;
    out.needle=+document.querySelector('.fxr-core[data-core="wid"] .wid-r').getAttribute('x1');
    out.strobe=[...document.querySelectorAll('.fxr-core .dst-curve')].filter(p=>p.hasAttribute('opacity')).length;
    out.grText=document.querySelectorAll('.fxr-core[data-core="cmp"] text').length;
    return out; });
  chk(r2.eqzMoved, 'Equalizer: the curve redraws from the push');
  chk(r2.nodeOnLine.every(v=>v<1.5), 'Equalizer: every node sits ON the drawn line (never hovering at its own gain)', 'off-line by '+r2.nodeOnLine.map(v=>v.toFixed(2)).join(','));
  chk(/rgb\(2[0-9]{2}, 2[0-9]{2}, 2[0-9]{2}\)|rgb\(255, 255, 255\)/.test(r2.nodeFill), 'Equalizer: nodes are FILLED WHITE (no dark "eye" centres)', r2.nodeFill);
  chk(r2.nodeCx[0]<r2.nodeCx[1]&&r2.nodeCx[1]<r2.nodeCx[2]&&r2.nodeCx[2]<r2.nodeCx[3], 'Equalizer: nodes at the feed\'s Hz, in order', r2.nodeCx.map(v=>v.toFixed(1)).join(' < '));
  chk(r2.ottColH>0 && r2.ottEdgeY>4, 'Multiband: the level column and the ceiling jaw draw from the push', 'colH='+r2.ottColH.toFixed(1)+' edgeY='+r2.ottEdgeY.toFixed(1));
  chk(r2.ottX[0]>6.5 && r2.ottX[1]>r2.ottX[0], 'Multiband: crossover lines sit at the live xoverHz on the log axis', r2.ottX.map(v=>v.toFixed(1)).join(' < '));
  chk(r2.kneeMoved && r2.press>0, 'Compress: the knee redraws and the ceiling presses by the GR', 'press='+r2.press.toFixed(1));
  chk(r2.beamsLit>=6 && r2.beamSpread>=6, 'Widen: six beams lit and fanned by the live pans', 'lit='+r2.beamsLit+' spread='+r2.beamSpread);
  chk(Math.abs(r2.needle-(113+73*0.4))<12, 'Widen: the correlation needle rides r', 'x='+r2.needle.toFixed(1));
  chk(r2.strobe===0, 'STROBE LAW: no .dst-curve carries an inline opacity', r2.strobe+' offenders');
  chk(r2.grText===0, 'Compress: no number on the card (Max: the press bar is the number)', r2.grText+' text nodes');

  // ── drag an EQ node with synthetic pointer events → the Hz + gain params are WRITTEN and the back knob moves
  const r3 = await pg.evaluate(()=>{
    const core=document.querySelector('.fxr-core[data-core="eqz"]'), svg=core.querySelector('svg'), rct=svg.getBoundingClientRect();
    const D=window.__fxDevs(); const d=D.find(x=>x.core==='eqz');
    const n=core.querySelector('.eqz-n[data-b="1"]'); const cx=+n.getAttribute('cx'), cy=+n.getAttribute('cy');
    const px=rct.left+cx/226*rct.width, py=rct.top+cy/78*rct.height;
    const before={hz:d.back.knobs[2][1], g:d.back.knobs[3][1]};
    const faceBefore=(document.querySelector('.fxr-dev .fxr-dial[data-bk="Body Hz"] text')||{}).textContent;
    window.__W.length=0;
    const ev=(t,x,y)=>new PointerEvent(t,{bubbles:true,cancelable:true,clientX:x,clientY:y,pointerId:1,pointerType:'mouse',buttons:1});
    core.dispatchEvent(ev('pointerdown',px,py)); core.dispatchEvent(ev('pointermove',px+40,py-18)); document.dispatchEvent(ev('pointerup',px+40,py-18));
    const ids=window.__W.map(w=>w[0]);
    const after={hz:d.back.knobs[2][1], g:d.back.knobs[3][1]};
    const faceAfter=(document.querySelector('.fxr-dev .fxr-dial[data-bk="Body Hz"] text')||{}).textContent;
    return {ids:[...new Set(ids)], before, after, faceBefore, faceAfter, hot:core.querySelectorAll('.eqz-n.hot').length};
  });
  chk(r3.ids.includes('SYN_EQZ_BODYHZ')&&r3.ids.includes('SYN_EQZ_BODY'), 'EQ node drag WRITES that band\'s Hz and gain params', r3.ids.join(','));
  chk(r3.after.hz>r3.before.hz && r3.after.g>r3.before.g, 'EQ node drag moves the MODEL (Hz up, gain up)', JSON.stringify({before:r3.before,after:r3.after}));
  chk(r3.faceAfter!==r3.faceBefore, 'the back knob\'s FACE follows the node (__fxRedrawKnobs)', r3.faceBefore+' → '+r3.faceAfter);
  chk(r3.hot===0, 'the held node un-highlights on release');

  // ── wheel over the EQ core = Trait; crossover drag on the Multiband = Low Cross
  const r4 = await pg.evaluate(()=>{
    const core=document.querySelector('.fxr-core[data-core="eqz"]'), r=core.getBoundingClientRect();
    window.__W.length=0;
    core.dispatchEvent(new WheelEvent('wheel',{bubbles:true,cancelable:true,deltaY:-120,clientX:r.left+r.width/2,clientY:r.top+r.height/2}));
    const trait=window.__W.map(w=>w[0]);
    const oc=document.querySelector('.fxr-core[data-core="ott"]'), svg=oc.querySelector('svg'), rc=svg.getBoundingClientRect();
    const xl=oc.querySelector('.ott-xl[data-x="0"]'); const x=+xl.getAttribute('x1');
    const px=rc.left+x/226*rc.width, py=rc.top+40/78*rc.height;
    window.__W.length=0;
    const ev=(t,x,y)=>new PointerEvent(t,{bubbles:true,cancelable:true,clientX:x,clientY:y,pointerId:2,pointerType:'mouse',buttons:1});
    oc.dispatchEvent(ev('pointerdown',px,py)); oc.dispatchEvent(ev('pointermove',px+25,py)); document.dispatchEvent(ev('pointerup',px+25,py));
    const D=window.__fxDevs(); const d=D.find(z=>z.core==='ott');
    return {trait:[...new Set(trait)], xo:[...new Set(window.__W.map(w=>w[0]))], lowcross:d.back.knobs[0][1]};
  });
  chk(!r4.trait.includes('SYN_EQZ_TRAIT') && r4.trait.includes('SYN_EQZ_BODYQ'), 'wheel over the EQ core (its centre = the Body dot) writes Body\'s Q, NEVER Trait (fb441)', r4.trait.join(','));
  chk(r4.xo.includes('SYN_OTT_LOWCROSS'), 'dragging the Multiband\'s first crossover writes Low Cross', r4.xo.join(',')+' model='+(+r4.lowcross).toFixed(1));

  // ── readout law + relabel law + the route pill colour
  const r5 = await pg.evaluate(()=>{
    const faces=(bk)=>(document.querySelector('.fxr-dev .fxr-dial[data-bk="'+bk+'"] text')||{}).textContent;
    const out={lowHz:faces('Low Hz'), reach:faces('Reach'), attack:faces('Attack'), push:(document.querySelector('.fxr-core[data-core="cmp"]').closest('.fxr-dev').querySelector('.fxr-dial[data-k="0"] text')||{}).textContent};
    // Type change on the Widen card → hero relabels; on the EQ → Trait relabels
    const wcard=document.querySelector('.fxr-core[data-core="wid"]').closest('.fxr-dev'), wsel=wcard.querySelector('select.fxr-type-native');
    wsel.value='Steady'; wsel.dispatchEvent(new Event('change',{bubbles:true}));
    const wcard2=document.querySelector('.fxr-core[data-core="wid"]').closest('.fxr-dev');
    out.widHero=wcard2.querySelector('.fxr-knob .fxr-lab').textContent;
    const ecard=document.querySelector('.fxr-core[data-core="eqz"]').closest('.fxr-dev'), esel=ecard.querySelector('select.fxr-type-native');
    esel.value='Chisel'; esel.dispatchEvent(new Event('change',{bubbles:true}));
    const ecard2=document.querySelector('.fxr-core[data-core="eqz"]').closest('.fxr-dev');
    out.traitLabel=[...ecard2.querySelectorAll('.fxr-bk-knob .fxr-lab')].map(e=>e.textContent).pop();
    const on=document.querySelector('.fxr-r.fxr-on')||(()=>{ const r=document.querySelector('.fxr-r'); r.classList.add('fxr-on'); return r; })();
    out.routeBorder=getComputedStyle(on).borderTopColor;
    out.routeInk=getComputedStyle(on).color;
    out.routeOffInk=getComputedStyle(document.querySelector('.fxr-r:not(.fxr-on)')).color;
    // the in-rack sibling that already wears the house grammar correctly: a LIT front pill.
    const lp=document.querySelector('.fxr-pill.fxr-on')||(()=>{ const q=document.querySelector('.fxr-pill'); q.classList.add('fxr-on'); return q; })();
    const lcs=getComputedStyle(lp); out.pillBorder=lcs.borderTopColor; out.pillInk=lcs.color;
    return out; });
  chk(/^\d+$|k$/.test(r5.lowHz||'') && /k$/.test(r5.reach||''), 'READOUT LAW: Low Hz / Reach print Hz', r5.lowHz+' / '+r5.reach);
  chk(/dB$/.test(r5.push||''), 'READOUT LAW: Push prints its threshold in dB', r5.push);
  chk(r5.widHero==='Cents', 'RELABEL: Widen type Steady → hero reads Cents', r5.widHero);
  chk(r5.traitLabel==='Sting', 'RELABEL: Equalizer type Chisel → P8 reads Sting', r5.traitLabel);
  // fb440 — Max: "I want the outline to be purple, but the inside to be white just like how we have it
  //   on the four modes." fb437 had read "white letters" as "white everything" and taken the BORDER
  //   white too — the only white-outline-on-select in the UI, and it rode every card. Both halves are
  //   gated now, so neither can drift alone, and both are compared to the fb118 mode tile that defines
  //   the house grammar rather than to a hard-coded hex authored a second time.
  chk(/183, 148, 255/.test(r5.routeBorder), 'ROUTE PILL: the lit letter\'s OUTLINE is purple-400', r5.routeBorder);
  chk(r5.routeInk===r5.pillInk && r5.routeInk!==r5.routeOffInk,
      'ROUTE PILL: the lit LETTER is the same white as a lit front pill (and not the unlit muted ink)',
      r5.routeInk+'  pill '+r5.pillInk+'  unlit '+r5.routeOffInk);
  chk(r5.routeBorder===r5.pillBorder,
      'ROUTE PILL outline == the lit front pill\'s outline — ONE selected-state look, not two',
      r5.routeBorder+'  vs pill '+r5.pillBorder);

  // ── the generic restore: per-id answers, decoded by CARDINALITY (fb373), drives type/knob/pill/route
  const r6 = await pg.evaluate(async()=>{
    const D=window.__fxDevs(); const idx=D.findIndex(z=>z.core==='eqz'); const d=D[idx];
    window.__PMAP[d.pfx+'TYPE']=6/15; window.__PMAP[d.pfx+'LOW']=0.75; window.__PMAP[d.pfx+'AIR']=0.9; window.__PMAP[d.pfx+'DELTA']=1; window.__PMAP[d.pfx+'SRC_B']=1; window.__PMAP[d.pfx+'FOCUS']=2/7;
    if(typeof window.__fxrRestoreGenericOne!=='function') return {hook:false};
    window.__fxrRestoreGenericOne(idx); await new Promise(r=>setTimeout(r,400));
    const d2=window.__fxDevs()[idx];
    return {hook:true,type:d2.type,low:d2.back.knobs[1][1],air:d2.knobs[1].v,delta:d2.pills[0].on,routeB:d2.route[1],focus:d2.back.d2.v,trait:d2.back.knobs[7][0]};
  });
  chk(r6.hook, 'the generic restore is exposed for the gate (__fxrRestoreGenericOne)');
  if(r6.hook){
    chk(r6.type==='Chisel'&&r6.trait==='Sting', 'RESTORE decodes Type by tpN (6/15 → Chisel) and relabels', r6.type+'/'+r6.trait);
    chk(Math.abs(r6.low-75)<0.01&&Math.abs(r6.air-90)<0.01, 'RESTORE reads back knob + front knob values', 'low='+r6.low+' air='+r6.air);
    chk(r6.delta===true&&r6.routeB===1, 'RESTORE reads pills and routes', 'delta='+r6.delta+' B='+r6.routeB);
    chk(r6.focus==='Side', 'RESTORE decodes the 2nd dropdown by pN (2/7 → Side)', r6.focus);
  }

  // ── fb438: the Character names are PER TYPE (mirrored from the engines) and the preset menu is per core
  let dumpJson=null;
  try{
    const cp=require('child_process'), fs=require('fs'), os=require('os');
    const bin=os.tmpdir()+'/fx_chars_dump_gate';
    const root='/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument';
    cp.execSync('clang++ -O2 -std=c++17 -I '+root+'/Tests/shim -I '+root+'/Source '+root+'/Tests/fx_chars_dump.cpp -o '+bin,{stdio:'pipe'});
    dumpJson=JSON.parse(cp.execSync(bin).toString());
  }catch(e){ console.log('  (could not build/run Tests/fx_chars_dump.cpp: '+String(e).slice(0,120)+')'); }
  const r7 = await pg.evaluate((dump)=>{
    const out={};
    out.hasChars=typeof window.__fxApplyType==='function';
    // the page's FX_CHARS literal vs the engines' own tables — read it off the page source, the var is module-scoped
    const src=[...document.scripts].map(s=>s.textContent).join('\n'); const m=src.match(/\/\* FX-CHARS-BEGIN \*\/\s*var FX_CHARS=(\{[\s\S]*?\});\s*\/\* FX-CHARS-END \*\//);
    out.literalFound=!!m; let lit=null; try{ lit=m?JSON.parse(m[1]):null; }catch(e){ out.parseErr=String(e).slice(0,80); }
    out.matchesDump = (dump&&lit) ? (JSON.stringify(dump)===JSON.stringify(lit)) : null;
    out.cores = lit?Object.keys(lit).join(','):'';
    // Type change on the EQ → the Character dropdown wears British's names, index kept
    const ecard=document.querySelector('.fxr-core[data-core="eqz"]').closest('.fxr-dev'), esel=ecard.querySelector('select.fxr-type-native');
    const D=window.__fxDevs(); const d=D.find(z=>z.core==='eqz');
    esel.value='British'; esel.dispatchEvent(new Event('change',{bubbles:true}));
    out.britishChars=d.back.d1.opts.slice(0,3).join(','); out.charV=d.back.d1.v;
    const card2=document.querySelector('.fxr-core[data-core="eqz"]').closest('.fxr-dev');
    const bksel=card2.querySelector('.fxr-bk-native'); out.domFirstOpt=bksel?bksel.options[0].textContent:'(no back select)';
    // the preset pill on the EQ card → a Factory section with rows; clicking one writes params
    window.__W.length=0;
    const pill=card2.querySelector('.fxr-preset'); pill.dispatchEvent(new MouseEvent('click',{bubbles:true,cancelable:true}));
    const menu=document.querySelector('.pmenu'); out.menu=!!menu;
    const secs=menu?[...menu.querySelectorAll('.ps')].map(e=>e.textContent):[]; out.secs=secs.join('|');
    const rows=menu?[...menu.querySelectorAll('.pi')].filter(r=>!r.classList.contains('psave')):[]; out.rows=rows.map(r=>r.querySelector('.nm')?r.querySelector('.nm').textContent:'').slice(0,4).join('|');
    const row=rows.find(r=>/Presence Push/.test(r.textContent)); if(row){ row.dispatchEvent(new MouseEvent('click',{bubbles:true,cancelable:true})); }
    out.applied=row?[...new Set(window.__W.map(w=>w[0]))].filter(x=>/SYN_EQZ_/.test(x)).length:0;
    out.pillName=(document.querySelector('.fxr-core[data-core="eqz"]').closest('.fxr-dev').querySelector('.fxr-pname')||{}).textContent;
    // the Chorus card's factory list is the CHORUS's, never the reverb's
    const ccard=document.querySelector('.fxr-core[data-core="cho"]');
    if(!ccard){ window.__fxAdd('cho'); }
    const cc=document.querySelector('.fxr-core[data-core="cho"]').closest('.fxr-dev'); const m2=document.querySelector('.pmenu'); if(m2) m2.remove();
    cc.querySelector('.fxr-preset').dispatchEvent(new MouseEvent('click',{bubbles:true,cancelable:true}));
    const menu2=document.querySelector('.pmenu'); out.choRows=menu2?[...menu2.querySelectorAll('.pi')].filter(r=>!r.classList.contains('psave')).map(r=>r.textContent.replace('✕','').trim()).slice(0,3).join('|'):'(no menu)';
    if(menu2) menu2.remove();
    return out; }, dumpJson);
  chk(r7.literalFound && r7.cores==='cho,fla,pha,eqz,wid,cmp,ott', 'FX_CHARS literal present for the seven relabelling cores', r7.cores);
  if(dumpJson) chk(r7.matchesDump===true, 'FX_CHARS equals the engines\' own charNames() tables (Tests/fx_chars_dump.cpp)', r7.matchesDump===true?'identical':'DIFFERS — re-run the dump and re-mirror');
  chk(r7.britishChars==='Desk,Big Knob,Ahead' && r7.charV==='Desk', 'Type British → the Character dropdown wears British\'s names (index kept)', r7.britishChars+' v='+r7.charV);
  chk(r7.domFirstOpt==='Desk', 'the back-panel Character <select> re-rendered with the Type\'s names', r7.domFirstOpt);
  chk(r7.menu && /Factory/.test(r7.secs), 'the preset pill opens the house menu with a Factory section', r7.secs);
  chk(/Presence Push/.test(r7.rows), 'the EQ (British) lists ITS factory presets', r7.rows);
  chk(r7.applied>=4 && r7.pillName==='Presence Push', 'applying a factory preset writes the params and renames the pill', 'writes='+r7.applied+' pill='+r7.pillName);
  chk(/Init Vintage/.test(r7.choRows), 'the Chorus lists the CHORUS factory presets, not the reverb\'s', r7.choRows);

  // ── fb438: the FREE BELLS — double-click empty curve adds a band, drag moves it, right-click removes it
  const r8 = await pg.evaluate(()=>{
    const core=document.querySelector('.fxr-core[data-core="eqz"]'), svg=core.querySelector('svg'), rct=svg.getBoundingClientRect();
    const D=window.__fxDevs(); const d=D.find(z=>z.core==='eqz'); const out={};
    out.nodes=core.querySelectorAll('.eqz-n').length; out.freeHidden=[...core.querySelectorAll('.eqz-n.eqz-free')].filter(n=>n.getAttribute('opacity')==='0').length;
    // an EMPTY spot on the plot: x for 5 kHz, y near the top (no node there)
    const px=rct.left+(6.5+213*Math.log(5000/20)/Math.log(1000))/226*rct.width, py=rct.top+8/78*rct.height;
    window.__W.length=0;
    core.dispatchEvent(new MouseEvent('dblclick',{bubbles:true,cancelable:true,clientX:px,clientY:py}));
    out.addWrites=[...new Set(window.__W.map(w=>w[0]))].filter(x=>/_X1/.test(x)).sort().join(',');
    out.modelOn=d.xb?d.xb[0][2]:null; out.modelHz=d.xb?Math.round(20*Math.pow(1000,d.xb[0][0]/100)):null;
    for(let i=0;i<3;i++) window.__fx4Tick();
    const n5=core.querySelector('.eqz-n[data-b="4"]'); out.n5op=n5.getAttribute('opacity'); out.n5cx=+n5.getAttribute('cx');
    // drag the new node: its X param must move
    const ev=(t,x,y)=>new PointerEvent(t,{bubbles:true,cancelable:true,clientX:x,clientY:y,pointerId:3,pointerType:'mouse',buttons:1});
    const nx=rct.left+(+n5.getAttribute('cx'))/226*rct.width, ny=rct.top+(+n5.getAttribute('cy'))/78*rct.height;
    window.__W.length=0; core.dispatchEvent(ev('pointerdown',nx,ny)); core.dispatchEvent(ev('pointermove',nx-30,ny+10)); document.dispatchEvent(ev('pointerup',nx-30,ny+10));
    out.dragWrites=[...new Set(window.__W.map(w=>w[0]))].filter(x=>/_X1/.test(x)).sort().join(','); out.hzAfter=d.xb?Math.round(20*Math.pow(1000,d.xb[0][0]/100)):null;
    // fb441 — right-click it: the HOUSE MENU opens (Delete band / Reset band); clicking Delete removes it
    window.__W.length=0; core.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:nx,clientY:ny}));
    const cm8=document.getElementById('syn-ctx-menu'); const rows8=cm8?[...cm8.querySelectorAll('.syn-ctx-item')]:[];
    out.menuRows=rows8.map(r=>r.textContent.trim()).join('|'); const delRow=rows8.find(r=>/^Delete band/.test(r.textContent.trim())); if(delRow) delRow.click();
    out.delWrites=window.__W.filter(w=>/_X1ON$/.test(w[0])).map(w=>w[1]).join(','); out.modelOnAfter=d.xb?d.xb[0][2]:null;
    try{ if(window.__synHideMenu) window.__synHideMenu(); }catch(e){}
    for(let i=0;i<3;i++) window.__fx4Tick(); out.n5opAfter=core.querySelector('.eqz-n[data-b="4"]').getAttribute('opacity');
    return out; });
  chk(r8.nodes===8 && r8.freeHidden===4, 'Equalizer core carries 8 nodes, the 4 free ones hidden by default', 'nodes='+r8.nodes+' hidden='+r8.freeHidden);
  chk(/X1HZ/.test(r8.addWrites)&&/X1ON/.test(r8.addWrites)&&r8.modelOn===1, 'double-click an empty spot ADDS a free bell (writes X1HZ/X1/X1ON, model on)', r8.addWrites+' hz≈'+r8.modelHz);
  chk(r8.n5op!=='0' && r8.n5cx>6.5, 'the new node appears on the curve at the click\'s frequency', 'opacity='+r8.n5op+' cx='+r8.n5cx);
  chk(/X1HZ/.test(r8.dragWrites) && r8.hzAfter<r8.modelHz, 'dragging the free bell writes its Hz (moved left = lower)', r8.modelHz+' → '+r8.hzAfter);
  chk(/Delete band/.test(r8.menuRows||'') && r8.delWrites==='0' && r8.modelOnAfter===0 && r8.n5opAfter==='0', 'right-click opens the house menu; Delete band REMOVES it (X1ON 0, node hidden)', 'menu='+r8.menuRows+' writes='+r8.delWrites);

  // ── fb441: THE NODE IS THE HANDLE — the grid moves nothing, the wheel on the grid pans, the wheel on a dot is that band's Q only
  const r9 = await pg.evaluate(()=>{
    const core=document.querySelector('.fxr-core[data-core="eqz"]'), svg=core.querySelector('svg'), rct=svg.getBoundingClientRect();
    const D=window.__fxDevs(); const d=D.find(z=>z.core==='eqz'); const out={};
    for(let i=0;i<3;i++) window.__fx4Tick();
    const ev=(t,x,y,extra)=>new PointerEvent(t,Object.assign({bubbles:true,cancelable:true,clientX:x,clientY:y,pointerId:5,pointerType:'mouse',buttons:1,button:0},extra||{}));
    const X=(hz)=>rct.left+(6.5+213*Math.log(hz/20)/Math.log(1000))/226*rct.width, Y=(u)=>rct.top+u/78*rct.height;
    // (a) press on the EMPTY grid (5 kHz, near the top) and drag: NOTHING may be written, no band may move
    const low0=d.back.knobs[0][1], body0=d.back.knobs[2][1], bite0=d.back.knobs[4][1], air0=d.knobs[1].v;
    window.__W.length=0; core.dispatchEvent(ev('pointerdown',X(5000),Y(8))); core.dispatchEvent(ev('pointermove',X(5000)-40,Y(8)+20)); document.dispatchEvent(ev('pointerup',X(5000)-40,Y(8)+20));
    out.gridWrites=window.__W.length; out.gridMoved=(d.back.knobs[0][1]!==low0)||(d.back.knobs[2][1]!==body0)||(d.back.knobs[4][1]!==bite0)||(d.knobs[1].v!==air0);
    // (b) wheel on the EMPTY grid: the EQ does not act (no writes); the rack is free to pan
    const clip=document.querySelector('.fxr-clip'); const sl0=clip?clip.scrollLeft:0; window.__W.length=0;
    const w1=new WheelEvent('wheel',{bubbles:true,cancelable:true,clientX:X(5000),clientY:Y(8),deltaY:100}); core.dispatchEvent(w1);
    out.gridWheelWrites=window.__W.length; out.gridWheelFree=(!w1.defaultPrevented)||(clip&&clip.scrollLeft!==sl0);
    if(clip) clip.scrollLeft=sl0;   // the pan above moved the card on screen: restore, and re-read the rect for everything below
    const r2=svg.getBoundingClientRect();
    // (c) wheel on the BODY dot: handled, writes ONLY SYN_EQZ_BODYQ (no TRAIT, no other band), Q up on wheel-up
    const nb=core.querySelector('.eqz-n[data-b="1"]'); const bx=r2.left+(+nb.getAttribute('cx'))/226*r2.width, by=r2.top+(+nb.getAttribute('cy'))/78*r2.height;
    window.__W.length=0; const w2=new WheelEvent('wheel',{bubbles:true,cancelable:true,clientX:bx,clientY:by,deltaY:-100}); core.dispatchEvent(w2);
    out.nodeWheelPrevented=w2.defaultPrevented; out.nodeWheelWrites=[...new Set(window.__W.map(w=>w[0]))].sort().join(','); out.bodyQ=d.xq?d.xq[1]:null;
    const X2=(hz)=>r2.left+(6.5+213*Math.log(hz/20)/Math.log(1000))/226*r2.width, Y2=(u)=>r2.top+u/78*r2.height;
    // (d) hover the Body dot: it lights (.hot, r 3.2) and the core wears the grab cursor; resting on the grid clears it
    core.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,clientX:bx,clientY:by,pointerType:'mouse'})); out.hotOnHover=nb.classList.contains('hot')&&core.classList.contains('eqz-over')&&nb.getAttribute('r')==='3.2';
    core.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,clientX:X2(5000),clientY:Y2(8),pointerType:'mouse'})); out.hotOnGrid=nb.classList.contains('hot')||core.classList.contains('eqz-over');
    // (e) double-click ON THE CURVE away from any dot (2 kHz on the flat line, y 39) ADDS a band there at 0 dB
    window.__W.length=0; core.dispatchEvent(new MouseEvent('dblclick',{bubbles:true,cancelable:true,clientX:X2(2000),clientY:Y2(39)}));
    const aw=window.__W.filter(w=>/_X[1-4](HZ|ON|Q)?$/.test(w[0])); out.curveAddWrites=[...new Set(aw.map(w=>w[0]))].sort().join(','); const gw=aw.find(w=>/_X[1-4]$/.test(w[0])); out.curveAddGain=gw?gw[1]:null;
    // (f) right-click a ROLE dot (Low): the menu offers Reset band; Delete band is there but disabled (fixed)
    const nl=core.querySelector('.eqz-n[data-b="0"]'); const lx=r2.left+(+nl.getAttribute('cx'))/226*r2.width, ly=r2.top+(+nl.getAttribute('cy'))/78*r2.height;
    core.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:lx,clientY:ly}));
    const cm=document.getElementById('syn-ctx-menu'); const rows=cm?[...cm.querySelectorAll('.syn-ctx-item')]:[];
    out.roleRows=rows.map(r=>(r.classList.contains('disabled')?'~':'')+r.textContent.trim()).join('|');
    try{ if(window.__synHideMenu) window.__synHideMenu(); }catch(e){}
    // (g) a RIGHT-button press on a dot never starts a drag
    window.__W.length=0; core.dispatchEvent(ev('pointerdown',bx,by,{button:2,buttons:2})); core.dispatchEvent(ev('pointermove',bx-40,by+20,{buttons:2})); document.dispatchEvent(ev('pointerup',bx-40,by+20,{button:2,buttons:0}));
    out.rightDragWrites=window.__W.filter(w=>/_BODYHZ$|_BODY$/.test(w[0])).length; try{ if(window.__synHideMenu) window.__synHideMenu(); }catch(e){}
    return out; });
  chk(r9.gridWrites===0 && !r9.gridMoved, 'press+drag on the EMPTY grid moves NOTHING (the grid is not a handle)', 'writes='+r9.gridWrites+' moved='+r9.gridMoved);
  chk(r9.gridWheelWrites===0 && r9.gridWheelFree===true, 'wheel over the grid is left to the rack (no EQ writes, pan free)', 'writes='+r9.gridWheelWrites+' free='+r9.gridWheelFree);
  chk(r9.nodeWheelPrevented===true && r9.nodeWheelWrites==='SYN_EQZ_BODYQ' && r9.bodyQ>50, 'wheel over the Body dot writes ONLY Body\'s Q (no Trait, no other band)', r9.nodeWheelWrites+' q='+r9.bodyQ);
  chk(r9.hotOnHover===true && r9.hotOnGrid===false, 'hover lights the dot (purple, larger, grab cursor); the grid clears it', 'hover='+r9.hotOnHover+' grid='+r9.hotOnGrid);
  chk(/_X[1-4]HZ/.test(r9.curveAddWrites)&&/_X[1-4]ON/.test(r9.curveAddWrites)&&/_X[1-4]Q/.test(r9.curveAddWrites)&&r9.curveAddGain===0.5, 'double-click ON THE CURVE adds a band there at 0 dB (gain 0.5, Q at the law)', r9.curveAddWrites+' gain='+r9.curveAddGain);
  chk(/~Delete band/.test(r9.roleRows||'')&&/(^|\|)Reset band/.test(r9.roleRows||''), 'right-click a ROLE dot: Delete band disabled (fixed), Reset band enabled', r9.roleRows);
  chk(r9.rightDragWrites===0, 'a right-button press on a dot never drags it', 'writes='+r9.rightDragWrites);

  console.log('\n  PASS '+pass+'   FAIL '+fail+'\n');
  await b.close();
  process.exit(fail?1:0);
})().catch(e=>{ console.error('GATE CRASHED: '+(e&&e.stack||e)); process.exit(2); });
