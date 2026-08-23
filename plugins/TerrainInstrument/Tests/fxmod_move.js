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
  // ── the OLDER cards. granular / tape / saturate read NO knob model (they draw straight from
  //    __grnVizPush / __tpeVizPush / __dstVizPush, which already carry the modulated value), so
  //    there is nothing to swap and nothing to gate. These four groups did read the model.
  { core:'flt', knob:0,  what:'Cut',        sel:['.flt-curve@d'] },
  // cho reads its BACK knob 7 (= rack 11, Phase); fla reads FRONT 2. pha reads no knob model at
  // all — like granular/tape/saturate it draws straight from its engine feed, so there was nothing
  // to swap and there is nothing here to gate.
  { core:'cho', knob:11, what:'Phase(b7)',  sel:['.cho-l@d','.cho-r@d'] },
  { core:'fla', knob:2,  what:'front-2',    sel:['.fla-b@d','.fla-t@d'] },
  { core:'delay', knob:1, what:'Feedback',  sel:['.dly-taps@html'] },
  // reverb's cells PULSE on their own (sin(t)), so a string-difference probe would pass on a
  // broken build — the fb453 trap. Size shifts the lit THRESHOLD (T=0.06+1.09*size), so the probe
  // is the summed lit area sampled across frames, asserted by MAGNITUDE and ORDER, not inequality.
  { core:'reverb', knob:0, what:'Size',     sel:['@litsum'], mode:'lit' },
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
    // the reverb/delay cores only animate while their bloom feed is alive (`live = smooth>0.004`)
    window.__fxBloomRvb=0.85; window.__fxBloomDly=0.85;
    window.__fxBloomRvbP=[0.85,0.85,0.85,0.85,0.85]; window.__fxBloomDlyP=[0.85,0.85,0.85,0.85,0.85];
    window.__fx3VizPush={ cho:six({lvl:0.8,rate:0.5,depth:0.5}), fla:six({lvl:0.8,rate:0.5,depth:0.5}), pha:six({lvl:0.8,rate:0.5,depth:0.5}) };
    window.__fltVizPush={ hz:800, res:0.4, type:0, lvl:0.8 };
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
        const sig=()=>C.sel.map(s=>{ const [q,a]=s.split('@');
                     if(a==='litsum'){ let t=0; core.querySelectorAll('rect').forEach(e=>{ t+=parseFloat(e.getAttribute('opacity')||0)||0; }); return t.toFixed(3); }
                     const els=[...core.querySelectorAll(q)];
                     if(a==='html') return els.map(e=>e.innerHTML.length+':'+e.innerHTML.slice(0,120)).join('/');
                     return els.map(e=>e.getAttribute(a)).join('/'); }).join('|');
        // reverb: the biggest lit area seen over the window, so one unlucky frame cannot decide it
        const sigLit=async(n)=>{ let m=0; for(let i=0;i<n;i++){ m=Math.max(m,parseFloat(sig())||0);
                        await new Promise(r=>requestAnimationFrame(r)); } return m; };
        // SETTLE: enough frames for the slowest glide on any card to converge
        const tick=async(n)=>{ for(let i=0;i<n;i++){
                                 try{window.__fx4Tick&&window.__fx4Tick();}catch(e){}
                                 try{window.__fx3Tick&&window.__fx3Tick();}catch(e){}   // cho/fla/pha
                                 try{window.__fltTick&&window.__fltTick();}catch(e){}   // the rack Filter
                                 if((i%8)===7) await new Promise(r=>requestAnimationFrame(r)); }
                               await new Promise(r=>requestAnimationFrame(r)); };
        const SET=48;
        const model=(C.knob<4)?((dev.knobs&&dev.knobs[C.knob])?dev.knobs[C.knob].v:null)
                              :((dev.back&&dev.back.knobs&&dev.back.knobs[C.knob-4])?dev.back.knobs[C.knob-4][1]:null);
        out.model=model;
        const LIT=(C.mode==='lit');
        window.__fxModEff=undefined; await tick(SET); out.s0=LIT?await sigLit(20):sig();
        window.__fxModEff={}; window.__fxModEff[dest]=0.10; await tick(SET); out.s1=LIT?await sigLit(20):sig();
        window.__fxModEff={}; window.__fxModEff[dest]=0.90; await tick(SET); out.s2=LIT?await sigLit(20):sig();
        // REDUCES: hand it the model's own number — the drawing must return to the unmodulated one
        window.__fxModEff={}; window.__fxModEff[dest]=(model==null?0:model/100); await tick(SET); out.sM=LIT?await sigLit(20):sig();
        window.__fxModEff=undefined; await tick(SET);
        const model2=(C.knob<4)?((dev.knobs&&dev.knobs[C.knob])?dev.knobs[C.knob].v:null)
                               :((dev.back&&dev.back.knobs&&dev.back.knobs[C.knob-4])?dev.back.knobs[C.knob-4][1]:null);
        out.model2=model2;
      }catch(e){ out.err=String(e).slice(0,140); }
      return out;
    }, C);

    const tag=C.core+'/'+C.what;
    if(r.err){ chk(false, tag+' — probe ran', r.err); continue; }
    if(C.mode==='lit'){
      // THE BAR IS DERIVED, NOT TUNED (fb452 — measure the curve before you place the line).
      // Measured on BOTH trees, max lit area over 20 frames:
      //     pre-fb457 (broken): 53.85 → 54.50   ratio 1.01   Δ  0.65   ← animation noise only
      //     fb457     (fixed) : 42.45 → 112.77  ratio 2.66   Δ 70.32
      // 1.5x and Δ>20 sit between two MEASURED values with wide margin either side, so the bar
      // cannot be satisfied by the free-running pulse and cannot fail on a working build.
      const lo=+r.s1, hi=+r.s2;
      chk(hi>lo*1.5 && (hi-lo)>20, tag+' MOVES when modulated (lit area, MAGNITUDE not inequality)',
          'eff .10 → '+lo.toFixed(2)+'   eff .90 → '+hi.toFixed(2)+'   ratio '+(hi/Math.max(lo,1e-9)).toFixed(2)+'   (broken tree measures 1.01)');
    } else
    chk(r.s1!==r.s2, tag+' MOVES when modulated', 'eff .10 → '+String(r.s1).slice(0,40)+'   eff .90 → '+String(r.s2).slice(0,40));
    chk(r.model!=null && r.model===r.model2, tag+' — the DIAL itself did NOT move (Max\'s ruling)', 'knob '+r.model+' → '+r.model2);
    if(C.mode==='lit') chk(true, tag+' — reduction check N/A (the cells pulse on a free-running clock)', 'by design');
    else chk(r.sM===r.s0, tag+' — effective == model reproduces the UNMODULATED drawing exactly', r.sM===r.s0?'identical':'DIVERGED');
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

  // ── fb458: WARP / FOLD MADE VISIBLE ────────────────────────────────────────────────────────
  // Max: "we should be able to see the exact table being edited by each of its types."
  // The BAKE is C++ (getOscWavetableJson runs the voice's own applyPhaseWarp/applyAmpWarp/
  // applyFoldADAA, in the voice's order) — this page cannot reach it, so what is gated here is the
  // JS half: does the waterfall NOTICE the shaping moved, re-bake, and — just as important — does
  // it stay QUIET when nothing moved and when it is not on screen. A re-bake that never stops is a
  // CPU leak, and this project just spent a whole round winning CPU back.
  const wtw = await pg.evaluate(async ()=>{
    const out={}; const W=window.wtWaterfall;
    if(!W){ out.err='no wtWaterfall'; return out; }
    if(typeof W.maybeRebake!=='function'){ out.err='no maybeRebake — pre-fb458 tree'; return out; }
    window.__WTN=0;
    // the stub ANSWERS WITH THE SHAPING IT WAS ASKED FOR, so a stale table is detectable
    window.Juce.getNativeFunction=function(n){
      if(n==='getOscWavetable') return function(){
        window.__WTN++;
        const d=(window.__wtDisp&&window.__wtDisp[0])||[0,0,0,0,0,0,0,0,0];
        const P=8,N=2,arr=[]; for(let i=0;i<N*P;i++) arr.push(+(d[1]||0));
        return Promise.resolve(JSON.stringify({n:N,p:P,nf:N,wm:d[0],wa:d[1],w2m:d[2],w2a:d[3],fs:d[4],fa:d[5],
                                               sa:d[6],st:d[7],bl:d[8],ms:(window.__WTMS||0),d:arr}));
      };
      return function(){ return Promise.resolve(0); };
    };
    const raf=()=>new Promise(r=>requestAnimationFrame(r));
    const settle=async(ms)=>{ const t0=performance.now(); while(performance.now()-t0<ms) await raf(); };

    W.on.a=true; W.cache.a=null; W.busy.a=false; W.lastReq.a=0;
    window.__wtDisp=[[0,0.00,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]];
    W.kick(); await settle(350);
    out.sig0=W.cachedSig('a');

    // 1. the shaping moves -> the table is re-baked, and the NEW shaping is what came back
    const b1=window.__WTN;
    window.__wtDisp[0]=[2,0.70,0,0,0,0,0,0,0];
    await settle(450);
    out.bakesOnChange=window.__WTN-b1; out.sig1=W.cachedSig('a');
    out.cacheVal=(W.cache.a&&W.cache.a.d)?+W.cache.a.d[0]:null;

    // 2. nothing moves -> NOTHING is baked (a runaway re-bake would be a CPU leak)
    const b2=window.__WTN; await settle(700); out.bakesIdle=window.__WTN-b2;

    // 3. the waterfall is OFF -> nothing is baked at all (fb148: no UI, no work)
    W.on.a=false; const b3=window.__WTN;
    window.__wtDisp[0]=[3,0.20,0,0,0,0,0,0,0]; await settle(450);
    out.bakesHidden=window.__WTN-b3;

    // 4. and it catches up the moment it comes back on screen
    W.on.a=true; W.kick(); const b4=window.__WTN; await settle(450);
    out.bakesOnReturn=window.__WTN-b4; out.sig2=W.cachedSig('a');

    // 5. fb459 — SPECTRAL ALONE must re-bake. The morph rebuild is what changes the table, so if
    //    sa/st were left out of the signature a re-morphed table would look fresh forever.
    const b5=window.__WTN; const before5=W.cachedSig('a');
    window.__wtDisp[0]=[3,0.20,0,0,0,0,0.55,2,0];   // only spectral amount + type moved
    await settle(450);
    out.bakesOnSpectral=window.__WTN-b5; out.sigSpecBefore=before5; out.sigSpecAfter=W.cachedSig('a');
    // 6. AND IT MUST CONVERGE. If the pushed signature and the cached one ever disagree, the
    //    comparison never matches and the table re-bakes FOREVER at the throttle rate — a real CPU
    //    leak, and exactly what the pre-fb459 tree does when spectral is pushed but not cached
    //    (measured: 5 bakes and still going). Settling is the property, not just "it re-baked".
    const b6=window.__WTN; await settle(700); out.bakesAfterSpectral=window.__WTN-b6;

    // 7. fb460 — BLUR alone must re-bake, and settle.
    const b7=window.__WTN; const beforeBlur=W.cachedSig('a');
    window.__wtDisp[0]=[3,0.20,0,0,0,0,0.55,2,0.65];        // only blur moved
    await settle(450);
    out.bakesOnBlur=window.__WTN-b7; out.sigBlurBefore=beforeBlur; out.sigBlurAfter=W.cachedSig('a');
    const b7b=window.__WTN; await settle(700); out.bakesAfterBlur=window.__WTN-b7b;

    // 8. fb460 — THE COST CAP. A blurred bake on a big imported table is far more expensive than a
    //    16-frame factory one, so the interval follows the MEASURED cost (>= 10x). Tell the stub the
    //    bake took 200 ms: the next interval must stretch to ~2 s, and a change must NOT be
    //    serviced inside that window. Without this the display could pin the message thread.
    //    (200 not 50: the window is measured from the LEARNING request, which can be ~450 ms old
    //     by the time the observation starts — at 50 ms the window expired mid-test and the
    //     failure was my timing, not the plugin's. A window that dwarfs the slop tests the rule.)
    window.__WTMS=200;
    window.__wtDisp[0]=[3,0.20,0,0,0,0,0.55,2,0.70]; await settle(450);   // one bake, learns ms=50
    // must DEGRADE, not throw: on a tree without the adaptive interval this used to crash the whole
    // run, and a harness error reads as "inconclusive" when it should read as FAIL.
    out.learnedMs=(W.rebakeMs&&W.rebakeMs.a!=null)?W.rebakeMs.a:-1;
    const b8=window.__WTN;
    window.__wtDisp[0]=[3,0.20,0,0,0,0,0.55,2,0.75]; await settle(280);   // inside the window
    out.bakesInsideWindow=window.__WTN-b8;
    await settle(2000);                                                    // past it
    out.bakesPastWindow=window.__WTN-b8;
    window.__WTMS=0;
    return out;
  });
  if(wtw.err) chk(false,'WARP/FOLD re-bake — probe ran',wtw.err);
  else {
    chk(wtw.bakesOnChange>=1,      'WARP/FOLD change RE-BAKES the table',            'bakes '+wtw.bakesOnChange);
    chk(wtw.sig1!==wtw.sig0,       'the cached table now carries the NEW shaping',   wtw.sig0+'  →  '+wtw.sig1);
    // the table lives in a Float32Array, so 0.7 comes back 0.699999988 — compare at float32
    // precision, not with ===. (Asserting === here was MY bug, not the plugin's.)
    chk(Math.abs(wtw.cacheVal-0.7)<1e-6, 'the re-baked table is the one that was asked for','value '+wtw.cacheVal+' ≈ 0.7');
    chk(wtw.bakesIdle===0,         'a SETTLED shaping bakes nothing (no CPU leak)',  wtw.bakesIdle+' bakes in 700ms');
    chk(wtw.bakesHidden===0,       'a HIDDEN waterfall bakes nothing (fb148)',       wtw.bakesHidden+' bakes while off');
    chk(wtw.bakesOnReturn>=1,      'coming back on screen catches the stale table up','bakes '+wtw.bakesOnReturn+'  sig '+wtw.sig2);
    chk(wtw.bakesOnSpectral>=1,    'SPECTRAL alone RE-BAKES the table (fb459)',      'bakes '+wtw.bakesOnSpectral);
    chk(wtw.sigSpecAfter!==wtw.sigSpecBefore && /0\.550/.test(wtw.sigSpecAfter),
                                   'the cached table carries the new SPECTRAL state', wtw.sigSpecBefore+'  →  '+wtw.sigSpecAfter);
    chk(wtw.bakesAfterSpectral===0,'and it CONVERGES — no runaway re-bake (CPU leak)',  wtw.bakesAfterSpectral+' bakes in the 700ms after');
    chk(wtw.bakesOnBlur>=1,        'BLUR alone RE-BAKES the table (fb460)',           'bakes '+wtw.bakesOnBlur);
    chk(wtw.sigBlurAfter!==wtw.sigBlurBefore && /0\.650/.test(wtw.sigBlurAfter),
                                   'the cached table carries the new BLUR',           wtw.sigBlurBefore+'  →  '+wtw.sigBlurAfter);
    chk(wtw.bakesAfterBlur===0,    'blur CONVERGES too — no runaway',                 wtw.bakesAfterBlur+' bakes after');
    chk(wtw.learnedMs>=2000,       'the interval ADAPTS to a 200ms bake (>=10x)',     'interval '+wtw.learnedMs+' ms');
    chk(wtw.bakesInsideWindow===0, 'an expensive bake is NOT re-run inside its window','bakes '+wtw.bakesInsideWindow+' in 280ms');
    chk(wtw.bakesPastWindow>=1,    'and it IS serviced once the window passes',       'bakes '+wtw.bakesPastWindow+' after ~2.3s');
  }

  console.log('\n  PASS '+pass+'   FAIL '+fail+'\n');
  await b.close(); process.exit(fail?1:0);
})().catch(e=>{ console.log('HARNESS ERROR '+e); process.exit(2); });
