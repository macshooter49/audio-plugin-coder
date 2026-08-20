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
  chk(r1.mark.eqzCurve===6 && r1.mark.eqzNodes===24, 'Equalizer core = the house line + 4 nodes per card', JSON.stringify({curve:r1.mark.eqzCurve,nodes:r1.mark.eqzNodes}));
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
  chk(r4.trait.includes('SYN_EQZ_TRAIT'), 'wheel over the EQ core writes Trait', r4.trait.join(','));
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
    return out; });
  chk(/^\d+$|k$/.test(r5.lowHz||'') && /k$/.test(r5.reach||''), 'READOUT LAW: Low Hz / Reach print Hz', r5.lowHz+' / '+r5.reach);
  chk(/dB$/.test(r5.push||''), 'READOUT LAW: Push prints its threshold in dB', r5.push);
  chk(r5.widHero==='Cents', 'RELABEL: Widen type Steady → hero reads Cents', r5.widHero);
  chk(r5.traitLabel==='Sting', 'RELABEL: Equalizer type Chisel → P8 reads Sting', r5.traitLabel);
  chk(!/183, 148, 255/.test(r5.routeBorder), 'ROUTE PILL: the lit letter\'s border is white, not purple', r5.routeBorder);

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

  console.log('\n  PASS '+pass+'   FAIL '+fail+'\n');
  await b.close();
  process.exit(fail?1:0);
})().catch(e=>{ console.error('GATE CRASHED: '+(e&&e.stack||e)); process.exit(2); });
