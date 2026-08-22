// fb457 — OVERPASS 1: "IF IT'S MODULATED, IT MOVES."  The UI gate.
//
// Max: "whenever we have something that is modulated, whatever is being modulated it MOVES...
//  the Bode shifter, that little purple ramp, those purple shattered shards, needs to move — not
//  just when we're moving it manually but automated. The only thing that has this is the master
//  filter." Every rack card drew from DEVS, the UI's own knob model, which cannot know a route
//  moved the dial — so the card sounded different and looked frozen.
//
//   NODE_PATH=<pptr>/node_modules node Tests/fxmod_move.js
//   FXMOVE_PAGE=/path/to/pre-fb457/index.html node ...   → MUST FAIL (the mutation run)
//
// WHY EACH ASSERTION IS SHAPED THE WAY IT IS
//  · MOVES     — the probe reads a DETERMINISTIC DOM attribute the modulated knob drives, never a
//                <canvas>. Bode's shards animate on their own (st.ph advances every frame), so a
//                pixel/canvas probe would differ between any two frames and pass on a BROKEN build.
//                That is the fb453 trap — a gate that cannot fail is decoration.
//  · DIAL STILL — Max ruled the dial itself does NOT move (the underline + comet already say what
//                is modulated). So the knob MODEL must be untouched while the picture moves.
//  · REDUCES   — feeding the effective value the SAME number the model holds must reproduce the
//                unmodulated drawing EXACTLY, which is what proves an un-routed rack is unchanged.
//
// TWO CONDITIONS THE PROBE MUST PRESENT, or it measures nothing (fb441 / fb393 — a harness must be
// as REAL as the plugin, never kinder and never poorer):
//  · SETTLE. Cards GLIDE (Utility's Haas is fx4Gl(...,0.16)), so a value needs ~40 frames to
//    converge. Four ticks compared two half-finished glides and called it a divergence.
//  · FEED. The EQ spends mix as fx4EffDb(curve[i], mix) — it SCALES the curve. With no engine feed
//    the curve is flat and scaling zero by any mix is still zero, so the card was being asked to
//    show a difference in nothing. A constant feed is pushed first and never changed again, so the
//    ONLY thing that can move the probe is the modulated knob.
const puppeteer = require('puppeteer-core');
const P = process.env.FXMOVE_PAGE || '/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Source/ui/public/index.html';
let pass=0, fail=0;
const chk=(ok,label,detail)=>{ if(ok){pass++;console.log('  ok    '+label+(detail?'   '+detail:''));}
  else{fail++;console.log('  FAIL  '+label+(detail?'   '+detail:''));} };

// probe: a deterministic signature of the geometry the modulated knob drives
const CASES = [
  { core:'bod', knob:0,  what:'Shift',      sel:['.bod-n@cx','.bod-span@x1','.bod-span@x2'] },
  { core:'utl', knob:10, what:'Haas',       sel:['.utl-fl@d','.utl-fr@d','.utl-rl@d','.utl-rr@d'] },
  { core:'spl', knob:0,  what:'Crossover',  sel:['.spl-rg@x','.spl-rg@width'] },
  { core:'eqz', knob:3,  what:'Mix',        sel:['.eqz-fill@d','.eqz-fill@opacity','.eqz-glow@opacity'] },
];

(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:1560,height:1200,deviceScaleFactor:2});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,160)));
  await pg.evaluateOnNewDocument(() => {
    window.__PMAP={};
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

  console.log('\n══ fb457 — OVERPASS 1: if it is MODULATED, it MOVES ══\n');
  chk(errs.length===0,'the page runs with no errors at all',errs.length?errs.slice(0,2).join(' | '):'0 page errors');
  chk(await pg.evaluate(()=>typeof window.__fxAdd==='function'),'the rack module is ALIVE');
  // FEED: one constant frame of the C++ viz shape, pushed once and never changed, so the only
  // thing that can move a probe is the modulated knob itself.
  await pg.evaluate(()=>{
    const NB=192, curve=[]; for(let i=0;i<NB;i++){ const hz=20*Math.pow(10,3*i/(NB-1));
      curve.push(-6*Math.exp(-Math.pow(Math.log(hz/90)/0.6,2))+5*Math.exp(-Math.pow(Math.log(hz/550)/0.5,2))+9/(1+Math.pow(12000/hz,2.2))); }
    const knee=[]; for(let q=0;q<32;q++){ const x=-60+72*q/31; knee.push(x<-18?x:-18+(x+18)*0.25); }
    const six=(o)=>[o,o,o,o,o,o];
    window.__fx4VizPush={ eqz:six({lvl:0.8,hz:[90,550,3100,17000],db:[-6,5,0,9],curve,on:[1,1,1,1,0,0,0,0],q:[0.7,0.7,0.7,0.7,0.7,0.7,0.7,0.7]}),
      wid:six({corr:0.4,nV:6,pan:[-0.3,0.3,-0.5,0.5,-0.7,0.7],cents:[5,-5,8,-8,12,-12],width:0.4,lvl:0.8}),
      cmp:six({gr:7,in:-12,out:-19,thr:-18,ratio:4,atk:12,rel:140,kneeDb:6,lvl:0.8,knee}),
      ott:six({nb:3,lvl:0.8,x:[120,2500],gr:[3.5,-4,-7],lv:[-22,-28,-36],tdn:[-20,-18,-24],tup:[-34,-33,-40]}),
      utl:six({pkL:0.6,pkR:0.5,corr:0.3,lvl:0.8}), spl:six({lvl:0.8,bands:[0.5,0.5,0.5]}) };
  });
  const hasHook = await pg.evaluate(()=>typeof window.__fxEffV==='function');
  chk(hasHook,'fxEffV exists (the effective-value accessor)', hasHook?'':'ABSENT — this is the pre-fb457 tree');

  for (const C of CASES){
    const r = await pg.evaluate(async (C)=>{
      const out={};
      try{
        if(!document.querySelector('.fxr-core[data-core="'+C.core+'"]')) window.__fxAdd(C.core);
        await new Promise(r=>setTimeout(r,140));
        const D=(window.__fxDevs?window.__fxDevs():[]).filter(d=>d.core===C.core);
        if(!D.length){ out.err='no device'; return out; }
        const dev=D[0]; out.inst=dev.inst;
        const dest=window.__fxModDest?window.__fxModDest(C.core,dev.inst,C.knob):null;
        out.dest=dest; if(dest==null){ out.err='no dest'; return out; }
        const core=document.querySelector('.fxr-dev .fxr-core[data-core="'+C.core+'"]');
        if(!core){ out.err='no core el'; return out; }
        // keep the engine feed CONSTANT so nothing but the modulated knob can move the probe
        const sig=()=>C.sel.map(s=>{ const [q,a]=s.split('@'); const els=[...core.querySelectorAll(q)];
                     return els.map(e=>e.getAttribute(a)).join('/'); }).join('|');
        // SETTLE: enough frames for the slowest glide on any card to converge
        const tick=async(n)=>{ for(let i=0;i<n;i++){ try{window.__fx4Tick&&window.__fx4Tick();}catch(e){}
                                 if((i%8)===7) await new Promise(r=>requestAnimationFrame(r)); }
                               await new Promise(r=>requestAnimationFrame(r)); };
        const SET=48;
        const model=(C.knob<4)?((dev.knobs&&dev.knobs[C.knob])?dev.knobs[C.knob].v:null)
                              :((dev.back&&dev.back.knobs&&dev.back.knobs[C.knob-4])?dev.back.knobs[C.knob-4][1]:null);
        out.model=model;
        window.__fxModEff=undefined; await tick(SET); out.s0=sig();
        window.__fxModEff={}; window.__fxModEff[dest]=0.10; await tick(SET); out.s1=sig();
        window.__fxModEff={}; window.__fxModEff[dest]=0.90; await tick(SET); out.s2=sig();
        // REDUCES: hand it the model's own number — the drawing must return to the unmodulated one
        window.__fxModEff={}; window.__fxModEff[dest]=(model==null?0:model/100); await tick(SET); out.sM=sig();
        window.__fxModEff=undefined; await tick(SET);
        const model2=(C.knob<4)?((dev.knobs&&dev.knobs[C.knob])?dev.knobs[C.knob].v:null)
                               :((dev.back&&dev.back.knobs&&dev.back.knobs[C.knob-4])?dev.back.knobs[C.knob-4][1]:null);
        out.model2=model2;
      }catch(e){ out.err=String(e).slice(0,140); }
      return out;
    }, C);

    const tag=C.core+'/'+C.what;
    if(r.err){ chk(false, tag+' — probe ran', r.err); continue; }
    chk(r.s1!==r.s2, tag+' MOVES when modulated', 'eff .10 → '+String(r.s1).slice(0,40)+'   eff .90 → '+String(r.s2).slice(0,40));
    chk(r.model!=null && r.model===r.model2, tag+' — the DIAL itself did NOT move (Max\'s ruling)', 'knob '+r.model+' → '+r.model2);
    chk(r.sM===r.s0, tag+' — effective == model reproduces the UNMODULATED drawing exactly', r.sM===r.s0?'identical':'DIVERGED');
  }

  // ── THE WAVETABLE HALF ─────────────────────────────────────────────────────────────────────
  // Max: "whenever we modulate wavetable position that also moves... currently it moves by us
  // manually clicking on wavetable position and scrolling ourselves, but whenever the LFO is on
  // that knob, I want it to move automatically."
  // wtpos() is the waterfall's ONLY position source, so this is the whole contract. The fallback
  // is NOT asserted against a hard-coded number — it is compared to the ORIGINAL expression,
  // evaluated live in the same page. That is the actual claim ("nothing sounding behaves exactly
  // as it did"), and it cannot drift with whatever an unbacked JUCE slider happens to answer in a
  // headless page — a hard-coded 0.5 asserted the STUB, not the product.
  const wt = await pg.evaluate(()=>{
    const out={}; const W=window.wtWaterfall; if(!W||!W.wtpos){ out.err='no wtWaterfall.wtpos'; return out; }
    // the pre-fb457 expression, verbatim, as the reference for the no-feed case
    out.raw=(function(){ try{ var ss=window.Juce.getSliderState('SYN_OSC_A_WT_FRAME');
      return ss?Math.max(0,Math.min(1,ss.getNormalisedValue())):0; }catch(e){ return 0; } })();
    window.__wtFrameEff=undefined;               out.fallbackNoFeed=W.wtpos('a');
    window.__wtFrameEff=[-1,-1,-1,-1];           out.fallbackIdle  =W.wtpos('a');
    window.__wtFrameEff=[0.10,-1,-1,-1];         out.lo            =W.wtpos('a');
    window.__wtFrameEff=[0.90,-1,-1,-1];         out.hi            =W.wtpos('a');
    window.__wtFrameEff=[-1,0.25,0.75,0.40];     out.b=W.wtpos('b'); out.c=W.wtpos('c'); out.d=W.wtpos('d'); out.aWhileBcd=W.wtpos('a');
    window.__wtFrameEff=[1.8,-1,-1,-1];          out.clamped       =W.wtpos('a');
    window.__wtFrameEff=undefined; return out;
  });
  if(wt.err) chk(false,'wavetable — probe ran',wt.err);
  else {
    chk(wt.lo===0.10 && wt.hi===0.90, 'WT POSITION follows the modulated frame', 'eff .10 → '+wt.lo+'   eff .90 → '+wt.hi);
    chk(wt.lo!==wt.hi,                'WT POSITION actually MOVES between two modulated values');
    chk(wt.fallbackIdle===wt.raw && wt.fallbackNoFeed===wt.raw,
                                      'nothing sounding (-1) falls back to the KNOB, bit-for-bit the OLD expression',
                                      'idle → '+wt.fallbackIdle+'   no feed → '+wt.fallbackNoFeed+'   old expression → '+wt.raw);
    chk(wt.b===0.25 && wt.c===0.75 && wt.d===0.40 && wt.aWhileBcd===wt.raw,
                                      'each osc reads its OWN frame (no cross-talk)', 'b '+wt.b+' c '+wt.c+' d '+wt.d+' · a idle '+wt.aWhileBcd);
    chk(wt.clamped===1,               'an out-of-range feed is clamped, never drawn off the table', '1.8 → '+wt.clamped);
  }

  console.log('\n  PASS '+pass+'   FAIL '+fail+'\n');
  await b.close(); process.exit(fail?1:0);
})().catch(e=>{ console.log('HARNESS ERROR '+e); process.exit(2); });
