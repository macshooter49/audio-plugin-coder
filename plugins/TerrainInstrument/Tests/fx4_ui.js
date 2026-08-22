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
    for(const core of ['eqz','wid','cmp','ott','bod','utl','spl']) for(let i=0;i<8;i++){ try{ window.__fxAdd(core); }catch(e){ out.addErr=core+': '+String(e).slice(0,100); } }
    const D=window.__fxDevs?window.__fxDevs():[];
    for(const core of ['eqz','wid','cmp','ott','bod','utl','spl']){
      out.counts[core]=D.filter(d=>d.core===core).length;
      // fb444 — this gate means "the CORES entry exists and the card RENDERED", and it used to
      // prove that by counting <svg>. That was only ever true because every core happened to be
      // SVG. Utility's core is a button bank and the Splitter's is a lane stack — deliberately
      // HTML, because Max asked for buttons instead of a scope. Counting svg would have failed
      // two perfectly correct cards while a genuinely empty core (the fb437 blocker this gate
      // exists to catch) still slipped through on any HTML core. So: count cores that actually
      // rendered CONTENT, which is the thing being asserted.
      out.cores[core]=[...document.querySelectorAll('.fxr-dev .fxr-core[data-core="'+core+'"]')]
                        .filter(c=>c.children.length>0).length;
    }
    out.cards=document.querySelectorAll('.fxr-dev').length;
    out.mark={ eqzCurve:document.querySelectorAll('.fxr-core[data-core="eqz"] .eqz-curve.dst-curve').length,
               eqzNodes:document.querySelectorAll('.fxr-core[data-core="eqz"] .eqz-n').length,
               ottLanes:document.querySelectorAll('.fxr-core[data-core="ott"] .ott-lane').length,
               ottX:document.querySelectorAll('.fxr-core[data-core="ott"] .ott-xl').length,
               cmpKnee:document.querySelectorAll('.fxr-core[data-core="cmp"] .cmp-knee.dst-curve').length,
               widV:document.querySelectorAll('.fxr-core[data-core="wid"] .wid-v').length,
               bodNode:document.querySelectorAll('.fxr-core[data-core="bod"] .bod-n').length,
               bodCanvas:document.querySelectorAll('.fxr-core[data-core="bod"] canvas.fx-spec').length,
               bodGrid:document.querySelectorAll('.fxr-core[data-core="bod"] svg g[stroke="rgba(255,255,255,0.07)"]').length,   // fb447 — the decade grid is GONE
               splGrid:document.querySelectorAll('.fxr-core[data-core="spl"] svg g[stroke="rgba(255,255,255,0.07)"]').length,
               utlBtn:document.querySelectorAll('.fxr-core[data-core="utl"] .utl-lamps .fxr-pill').length,
               utlRails:document.querySelectorAll('.fxr-core[data-core="utl"] .utl-rl').length+document.querySelectorAll('.fxr-core[data-core="utl"] .utl-rr').length,
               utlStrip:document.querySelectorAll('.fxr-core[data-core="utl"] .utl-strip, .fxr-core[data-core="utl"] .utl-sw.fxr-pill[style]').length,
               utlRow:[...document.querySelectorAll('.fxr-dev')].filter(dv=>dv.querySelector('.fxr-core[data-core="utl"]'))
                          .reduce((a,dv)=>a+dv.querySelectorAll('.fxr-pills .fxr-pill').length,0),
               utlDup:(function(){ var d=[...document.querySelectorAll('.fxr-dev')].find(dv=>dv.querySelector('.fxr-core[data-core="utl"]')); if(!d) return -1;
                          var core=[...d.querySelectorAll('.utl-lamps .fxr-t')].map(e=>e.textContent.trim()), row=[...d.querySelectorAll('.fxr-pills .fxr-t')].map(e=>e.textContent.trim());
                          return core.filter(x=>row.indexOf(x)>=0).length; })(),
               splLane:document.querySelectorAll('.fxr-core[data-core="spl"] .spl-lane').length,
               splAdd:document.querySelectorAll('.fxr-core[data-core="spl"] .spl-add svg.ic-plus').length,   // fb447 — the + IS the header's glyph
               splAddIsHeader:(function(){ var a=document.querySelector('.fxr-core[data-core="spl"] .spl-add svg'), h=document.querySelector('.fxr-dev .fxr-swap svg.ic-plus'); return !!(a&&h&&a.outerHTML===h.outerHTML); })(),
               splAddOld:document.querySelectorAll('.fxr-core[data-core="spl"] .spl-addt, .fxr-core[data-core="spl"] g.spl-add').length,
               splNames:document.querySelectorAll('.fxr-core[data-core="spl"] .spl-nm').length,
               splLt:document.querySelectorAll('.fxr-core[data-core="spl"] .spl-lt').length,
               splBoxes:document.querySelectorAll('.fxr-core[data-core="spl"] .spl-addg').length,
               eqzPill:(document.querySelector('.fxr-core[data-core="eqz"]')||{}).closest ? [...document.querySelector('.fxr-core[data-core="eqz"]').closest('.fxr-dev').querySelectorAll('.fxr-pill .fxr-t')].map(e=>e.textContent).join(',') : '' };
    return out; });
  chk(!r1.addErr, 'adding the seven devices throws nothing', r1.addErr||'');
  chk(r1.cards===42, 'eight of each ADD, capped at six each (42 cards)', 'cards='+r1.cards+' '+JSON.stringify(r1.counts));
  for(const core of ['eqz','wid','cmp','ott','bod','utl','spl']) chk(r1.cores[core]===6, core+': every card renders its CORE (CORES['+core+'] exists)', 'svg cores='+r1.cores[core]);
  chk(r1.mark.eqzCurve===6 && r1.mark.eqzNodes===48, 'Equalizer core = the house line + 8 nodes per card (4 roles + 4 free bells, fb438)', JSON.stringify({curve:r1.mark.eqzCurve,nodes:r1.mark.eqzNodes}));
  chk(r1.mark.ottLanes===18 && r1.mark.ottX===12, 'Multiband core = 3 lanes + 2 crossover lines per card', JSON.stringify({lanes:r1.mark.ottLanes,x:r1.mark.ottX}));
  chk(r1.mark.bodNode===6 && r1.mark.bodCanvas===6 && r1.mark.bodGrid===0, 'Bode core = the rail node + the canvas, and NO decade grid (fb447, Max: "those lines are not symmetrical — why are they there?")', JSON.stringify({node:r1.mark.bodNode,canvas:r1.mark.bodCanvas,grid:r1.mark.bodGrid}));
  chk(r1.mark.splGrid===0, 'Splitter core: NO decade grid either (fb447)', 'grid groups='+r1.mark.splGrid);
  chk(r1.mark.utlBtn===18 && r1.mark.utlRails===12 && r1.mark.utlStrip===0, 'Utility core = the two rails per card + THREE lamp switches (fb450: Flip L · Flip R · Swap)', JSON.stringify({lamps:r1.mark.utlBtn,rails:r1.mark.utlRails,strip:r1.mark.utlStrip}));
  chk(r1.mark.utlRow===12, 'Utility keeps its chassis pills (Sum · Dim) like every other card (fb446)', 'chassis pills='+r1.mark.utlRow);
  chk(r1.mark.utlDup===0, 'Utility NO DOUBLES — no switch caption repeats on the chassis row', 'dupes='+r1.mark.utlDup);
  chk(r1.mark.splLane===24 && r1.mark.splAdd===24 && r1.mark.splLt===24, 'Splitter core = four bands, a thin Hz number and a "+" each (fb446)', JSON.stringify({lanes:r1.mark.splLane,plus:r1.mark.splAdd,hz:r1.mark.splLt}))
  chk(r1.mark.splAddIsHeader===true && r1.mark.splAddOld===0, 'the band "+" IS the header\'s + glyph, byte-identical SVG (fb447, Max: "literally copy the plus button from the header")', 'identical='+r1.mark.splAddIsHeader+' oldPaths='+r1.mark.splAddOld);;
  chk(r1.mark.splNames===0 && r1.mark.splBoxes===0, 'Splitter: NO band names, NO boxed plus (fb446, Max: "that low and that high has to go · that plus button looks terrible")', JSON.stringify({names:r1.mark.splNames,boxes:r1.mark.splBoxes}));
  /* ═══ fb446 — THE READOUT LAW, by evaluation over every formatter. A value in a knob face is a BARE
     NUMBER: no Hz/ms/dB/oct/°; a sign, a %, a k are glyphs of the number; four glyphs, five with a
     leading sign, a time signature, a ratio, or an arrow. Max: "it's never supposed to move outside
     of the knob — it's always supposed to be on the inside." */
  const rl=await pg.evaluate(()=>{ const vals=[0,1,2,5,10,20,25,33,40,50,60,66,75,80,90,95,98,99,100]; const bad=[]; let total=0;
    const stub=core=>{ const T=(window.__devTemplates||[]).find(t=>t.core===core)||{}; return {core,types:T.types||['x'],type:T.type||(T.types||['x'])[0],knobs:[{v:50},{v:50},{v:50},{v:100}],back:{knobs:[['a',50],['b',50],['c',50],['d',50],['e',40],['f',50],['g',50],['h',50]],d2:{v:'1/8',opts:['Free','1/8']}},pills:[{on:false},{on:false}],__vz:null,xb:[[50,50,0],[50,50,0],[50,50,0],[50,50,0]]}; };
    const ok=s=>{ if(/Hz|hz|ms\b|dB|oct|°/.test(s)) return false; if(s.length<=4) return true; if(s.length===5&&(/^[+−-]/.test(s)||/bar|\//.test(s)||/:1$/.test(s)||/^←|→$/.test(s))) return true; return false; };
    const FXT=window.__fxFmtTable||{}; Object.keys(FXT).forEach(key=>{ const f=FXT[key], core=key.split('|')[0]; vals.forEach(v=>{ total++; try{ let s=f(v,stub(core)); if(s==null) return; s=''+s; if(!ok(s)) bad.push(key+'@'+v+'="'+s+'"'); }catch(e){} }); });
    return {total,n:Object.keys(FXT).length,bad}; });
  chk(rl.n>=80 && rl.bad.length===0, 'THE READOUT LAW: every formatter, every value, bare and fits ('+rl.total+' values / '+rl.n+' readouts)', rl.bad.slice(0,6).join(' · ')||('clean, '+rl.n+' readouts'));
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
  chk(r4.trait.length===0, 'wheel over the EQ core writes NOTHING — the wheel belongs to the rack\'s scroll (fb451, Max: "if I scroll on top of it, it won\'t move to the next effect")', r4.trait.join(',')||'no writes');
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
  chk(/^[+−-]?\d+(\.\d)?$/.test((r5.push||'').trim()), 'READOUT LAW (fb446): Push prints its threshold as a BARE signed number — no dB suffix', r5.push);
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
  chk(r9.nodeWheelPrevented===false && !r9.nodeWheelWrites && r9.bodyQ===50, 'wheel over the Body dot writes NOTHING and is left to the rack\'s scroll (fb451 — Q lives on the back panel)', 'writes='+(r9.nodeWheelWrites||'none')+' q='+r9.bodyQ+' prevented='+r9.nodeWheelPrevented);
  chk(r9.hotOnHover===true && r9.hotOnGrid===false, 'hover lights the dot (purple, larger, grab cursor); the grid clears it', 'hover='+r9.hotOnHover+' grid='+r9.hotOnGrid);
  chk(/_X[1-4]HZ/.test(r9.curveAddWrites)&&/_X[1-4]ON/.test(r9.curveAddWrites)&&/_X[1-4]Q/.test(r9.curveAddWrites)&&r9.curveAddGain===0.5, 'double-click ON THE CURVE adds a band there at 0 dB (gain 0.5, Q at the law)', r9.curveAddWrites+' gain='+r9.curveAddGain);
  chk(/~Delete band/.test(r9.roleRows||'')&&/(^|\|)Reset band/.test(r9.roleRows||''), 'right-click a ROLE dot: Delete band disabled (fixed), Reset band enabled', r9.roleRows);
  chk(r9.rightDragWrites===0, 'a right-button press on a dot never drags it', 'writes='+r9.rightDragWrites);

  // ── fb442: THE SPECTRUM IS A CANVAS. Max: "grainy... a static portrait of the last audio... it copies
  //    and pastes the new audio" — and "it doesn't do it with the main filter", which IS a canvas. The cards
  //    drew the same feed into an SVG path on a preserveAspectRatio="none" viewBox (the fb356 trap), and
  //    fb441's peak-per-column made it mesas. These gates pin the renderer, the clearing, and the aggregation.
  const r10 = await pg.evaluate(async()=>{
    const out={};
    // ⚠️ __fxAdd re-renders the whole rack, so ANY node captured before it is detached — query after.
    if(!document.querySelector('.fxr-core[data-core="flt"]')) window.__fxAdd('flt');
    await new Promise(r=>setTimeout(r,120));
    const eq=document.querySelector('.fxr-core[data-core="eqz"]');
    const fl=document.querySelector('.fxr-core[data-core="flt"]');
    out.eqCv=!!eq.querySelector('canvas.fx-spec'); out.fltCv=!!fl.querySelector('canvas.fx-spec');
    out.oldPaths=document.querySelectorAll('.eqz-spec,.flt-spec').length;
    out.bandMag=typeof window.__fltBandMag==='function';
    // the canvas must sit UNDER the SVG (the curve and nodes stay on top)
    const kids=[...eq.children].map(e=>e.tagName.toLowerCase()); out.order=kids.join(',');
    const ink=(c)=>{ const cv=c.querySelector('canvas.fx-spec'); if(!cv||!cv.width) return -1;
      const d=cv.getContext('2d').getImageData(0,0,cv.width,cv.height).data; let k=0;
      for(let i=3;i<d.length;i+=4) if(d[i]>6) k++; return +(100*k/(cv.width*cv.height)).toFixed(2); };
    // a LOUD frame → ink on both cards
    const N=2048, loud=new Array(N).fill(0.0002);
    for(let h=1;h<=40;h++){ const b=Math.round(110*h*4096/48000); if(b<N) loud[b]=0.05/h; }
    for(let k=0;k<4;k++){ window.__terrainEqAnalyzer({pre:loud,post:loud,sr:48000});
      window.__fx4Tick&&window.__fx4Tick(); if(window.__fltTick) window.__fltTick(); await new Promise(r=>requestAnimationFrame(r)); }
    out.inkEq=ink(eq); out.inkFlt=ink(fl);
    // SILENCE → the canvas must go EMPTY (it is cleared and repainted every frame: a stale layer is impossible)
    const dead=new Array(N).fill(0);
    for(let k=0;k<4;k++){ window.__terrainEqAnalyzer({pre:dead,post:dead,sr:48000});
      window.__fx4Tick&&window.__fx4Tick(); if(window.__fltTick) window.__fltTick(); await new Promise(r=>requestAnimationFrame(r)); }
    out.inkDead=ink(eq);
    // the aggregator: a pixel spanning many bins returns the PEAK inside it, so a peak between two
    // point-samples can never be skipped (that skipping, re-rolling per frame, was the "static")
    // (guarded: on a tree without these hooks the gate must FAIL, not crash the run)
    try{ const one=new Array(N).fill(0); one[Math.round(5000*4096/48000)]=0.5;
      window.__terrainEqAnalyzer({pre:one,post:one,sr:48000});
      out.peakKept=window.__fltBandMag(4700,5300)>=0.49;
      out.pointMissed=window.__fltBinMag(5300)<0.01;      // the same spot, point-sampled, sees nothing
      const t441=new Array(N).fill(0); t441[Math.round(1000*4096/44100)]=0.5;
      window.__terrainEqAnalyzer({pre:t441,post:t441,sr:44100});
      out.srUsed=window.__fltBandMag(960,1040)>=0.49;
    }catch(e){ out.peakKept=false; out.pointMissed=false; out.srUsed=false; out.hookErr=String(e).slice(0,60); }
    return out; });
  chk(r10.eqCv && r10.fltCv, 'both cards draw their spectrum into a <canvas> (the main filter\'s renderer)', 'eqz='+r10.eqCv+' flt='+r10.fltCv);
  chk(r10.oldPaths===0, 'the stretched-SVG spectrum paths are GONE (fb356: non-uniform scale warps stroke weight)', 'left='+r10.oldPaths);
  chk(/^canvas,svg/.test(r10.order), 'the canvas sits UNDER the SVG, so curve + nodes stay on top', r10.order);
  chk(r10.inkEq>0.5 && r10.inkFlt>0.5, 'a live frame paints both canvases', 'eqz='+r10.inkEq+'%  flt='+r10.inkFlt+'%');
  chk(r10.inkDead===0, 'SILENCE leaves the canvas completely empty — no stale layer can survive a frame', 'ink='+r10.inkDead+'%');
  chk(r10.bandMag && r10.peakKept && r10.pointMissed, 'a pixel spanning many bins returns the PEAK (point-sampling would skip it)', 'peakKept='+r10.peakKept+' pointMissed='+r10.pointMissed);
  chk(r10.srUsed, 'the analyzer feed\'s sample rate is honoured (44.1 k no longer reads 8.8 % sharp)', 'srUsed='+r10.srUsed);

  // ── fb443: NO FLOATING READOUT ON A RACK CARD. Max: "there's this text that pops up at the top left,
  //    we don't need that... there's no space for it without it getting crowded" — EQ and Multiband both.
  //    Gate the ABSENCE, and gate that dragging still WORKS without it (the removal must not take the
  //    interaction with it), and that the right-click menu HEADER still carries the band's Hz + Q.
  const r11 = await pg.evaluate(async()=>{
    await new Promise(r=>setTimeout(r,260));   // clear the right-click belt left armed by r9
    const out={}; const core=document.querySelector('.fxr-core[data-core="eqz"]');
    out.roNodes=document.querySelectorAll('.eqz-ro,.ott-ro').length;
    /* fb445: the LAW is no floating READOUT — a NUMBER on the card. A Splitter lane's NAME and its '+' glyph are
       structure (the affordance for 'add a device into THIS band'), not readouts. Count <text> carrying the
       readout signature: digits, Hz, dB, %, Q — which is exactly what the fb443 EQ readout carried. */
    out.anyCardText=[...document.querySelectorAll('.fxr-core[data-core="eqz"] text, .fxr-core[data-core="ott"] text')].filter(t=>/[0-9]|Hz|dB|%|\bQ\b/.test(t.textContent||'')).length;   /* fb446: scoped to the two cards fb443 was about — the Splitter's corner Hz numbers are Max's explicit ask */
    const svg=core.querySelector('svg'), r=svg.getBoundingClientRect();
    const D=window.__fxDevs(), d=D.find(z=>z.core==='eqz');
    const nb=core.querySelector('.eqz-n[data-b="1"]');
    const bx=r.left+(+nb.getAttribute('cx'))/226*r.width, by=r.top+(+nb.getAttribute('cy'))/78*r.height;
    const ev=(t,x,y)=>new PointerEvent(t,{bubbles:true,cancelable:true,clientX:x,clientY:y,pointerId:9,pointerType:'mouse',buttons:1,button:0});
    // the menu FIRST, on an untouched node (a drag leaves the node under a different cursor position)
    core.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:bx,clientY:by}));
    const cm=document.getElementById('syn-ctx-menu');
    out.menuText=cm?cm.textContent:''; out.secText=cm?[...cm.querySelectorAll('.syn-ctx-section')].map(e=>e.textContent).join('|'):'';
    try{ if(window.__synHideMenu) window.__synHideMenu(); }catch(e){}
    const hz0=d.back.knobs[2][1];
    window.__W.length=0;
    core.dispatchEvent(ev('pointerdown',bx,by)); core.dispatchEvent(ev('pointermove',bx+26,by-8)); document.dispatchEvent(ev('pointerup',bx+26,by-8));
    out.dragStillWrites=[...new Set(window.__W.map(w=>w[0]))].filter(x=>/BODYHZ|_BODY$/.test(x)).sort().join(',');
    out.hzMoved=d.back.knobs[2][1]!==hz0;
    return out; });
  chk(r11.roNodes===0 && r11.anyCardText===0, 'no floating readout text on ANY rack card (EQ + Multiband)', 'ro='+r11.roNodes+' <text>='+r11.anyCardText);
  chk(/BODYHZ/.test(r11.dragStillWrites) && r11.hzMoved, 'dragging a node still writes its Hz with the readout gone', r11.dragStillWrites);
  chk(/Q /.test(r11.secText||'') && /(Hz|k)/.test(r11.secText||''), 'the right-click menu HEADER still reports that band\'s Hz and Q', (r11.secText||'(no menu)').slice(0,70));

  /* ═══ fb447 — THE BODE LADDER LIVES ON THE CANVAS (no SVG ladder to pop in and out): with no feed at all,
     a few ticks must leave INK on the Bode canvas (the white rungs + the purple streams), and the streams must
     MOVE between frames when the shift is non-zero. ═══ */
  const bl=await pg.evaluate(()=>{ const out={}; const D=window.__fxrDevs(); const d=D.find(x=>x.core==='bod'); if(!d) return {err:'no bode'};
    const idx=D.indexOf(d), core=document.querySelectorAll('.fxr-dev')[idx].querySelector('.fxr-core[data-core="bod"]'); d.knobs[0].v=78; d.knobs[2].v=40;
    const hb=window.__fltHasBins; window.__fltHasBins=()=>false;   // NO FEED, for real: the live layers may not ink the canvas here
    const ink=()=>{ const cv=core.querySelector('canvas.fx-spec'); if(!cv||!cv.width) return -1; const px=cv.getContext('2d').getImageData(0,0,cv.width,cv.height).data; let n=0; for(let i=3;i<px.length;i+=4) if(px[i]>8) n++; return n; };
    for(let k=0;k<48;k++) window.__fx4Tick(); out.ink1=ink();   // the ladder FADES in over a glide (~0.7 s at 60 Hz) — give it the frames a real stop would
    const snap=()=>{ const cv=core.querySelector('canvas.fx-spec'); const px=cv.getContext('2d').getImageData(0,0,cv.width,cv.height).data; let h=0; for(let i=3;i<px.length;i+=4*7) h=(h*31+px[i])>>>0; return h; };
    out.h1=snap(); for(let k=0;k<8;k++) window.__fx4Tick(); out.h2=snap(); out.ink2=ink();
    out.svgLadder=core.querySelectorAll('.bod-ref,.bod-sh,.bod-sh2').length; out.w=core.querySelector('canvas.fx-spec').width;
    d.knobs[0].v=50; d.knobs[2].v=0; window.__fltHasBins=hb; return out; });
  chk(bl.ink1>50 && bl.svgLadder===0, 'Bode: with NO feed the canvas carries the ladder (ink), and no SVG ladder exists to pop (fb447)', JSON.stringify(bl));
  chk(bl.h1!==bl.h2 && bl.ink2>50, 'Bode: the streams MOVE between frames at a non-zero shift (the motion is the shift)', 'hash '+bl.h1+' → '+bl.h2);

  /* ═══ fb447 — THE UTILITY'S RAILS ARE DRAWN, not just declared: after a tick each rail carries a path, the
     paths MOVE when Swap lights, and the lamps paint from the model. ═══ */
  const ur=await pg.evaluate(()=>{ const out={}; const D=window.__fxrDevs(); const d=D.find(x=>x.core==='utl'); if(!d) return {err:'no utility'};
    const idx=D.indexOf(d), core=document.querySelectorAll('.fxr-dev')[idx].querySelector('.fxr-core[data-core="utl"]');
    d.pills.forEach(p=>p.on=false); for(let k=0;k<3;k++) window.__fx4Tick();
    const dL=()=>(core.querySelector('.utl-rl').getAttribute('d')||''), dR=()=>(core.querySelector('.utl-rr').getAttribute('d')||'');
    out.len=dL().length; out.d0=dL(); const before=dL()+'|'+dR();
    d.pills[2].on=true; for(let k=0;k<40;k++) window.__fx4Tick();                  // Swap glides; 40 frames is plenty
    out.swapMoved=(dL()+'|'+dR())!==before; out.lampOn=core.querySelectorAll('.utl-lamps .fxr-pill')[2].classList.contains('fxr-on');
    d.pills[2].on=false; for(let k=0;k<40;k++) window.__fx4Tick(); return out; });
  chk(ur.len>60 && /^M/.test(ur.d0||''), 'Utility: the rails are DRAWN (a path with real geometry after a tick)', 'len='+ur.len);
  chk(ur.swapMoved===true && ur.lampOn===true, 'Utility: Swap re-routes the rails and its lamp lights from the model', JSON.stringify({moved:ur.swapMoved,lamp:ur.lampOn}));

  /* ═══ fb448 — Max's third pass: the lamps FILL WHITE · the Splitter is TRANSPARENT (regions are hit areas,
     the selected band wears a hairline) · its numbers are HTML and the low band says 20 · the + is 11px · three
     chassis pills act on the SELECTED band · the DC lamp is a Low Cut you can hear. ═══ */
  const m3=await pg.evaluate(async()=>{ const out={}; const D=window.__fxrDevs();
    const u=D.find(x=>x.core==='utl'), ui=D.indexOf(u), ucore=document.querySelectorAll('.fxr-dev')[ui].querySelector('.fxr-core[data-core="utl"]');
    u.pills.forEach(p=>p.on=false); u.pills[0].on=true; window.__fx4Tick(); await new Promise(r=>setTimeout(r,300));   // the lamp's fill has a 120 ms transition — read it settled, not mid-fade
    const dot=ucore.querySelector('.utl-lamps .fxr-pill.fxr-on .utl-dot');
    out.lampBg=dot?getComputedStyle(dot).backgroundColor:'none'; out.lampShadow=dot?getComputedStyle(dot).boxShadow:'none'; u.pills[0].on=false; window.__fx4Tick();
    out.lampCaps=[...ucore.querySelectorAll('.utl-lamps .fxr-t')].map(e=>e.textContent);
    const sp=D.find(x=>x.core==='spl'), si=D.indexOf(sp); sp.type='Low / Mid / High'; window.__fxApplyType(sp,sp.type); sp.sel=1; window.__fxrRender(); for(let k=0;k<3;k++) window.__fx4Tick();
    const card=document.querySelectorAll('.fxr-dev')[si], core=card.querySelector('.fxr-core[data-core="spl"]');
    out.rgOp=[...core.querySelectorAll('.spl-rg')].map(r=>r.getAttribute('opacity')).join(',');
    out.lt=[...core.querySelectorAll('.spl-lt')].map(e=>e.tagName+':'+e.textContent+(e.classList.contains('spl-on')?'*':'')).join('|');
    out.noSelClass=!core.querySelector('.sel');   // fb451 — ".sel" is the panel's global BOX style; nothing in a core may wear it
    out.plusW=Math.round(core.querySelector('.spl-add').getBoundingClientRect().width); out.swapW=Math.round(card.querySelector('.fxr-swap').getBoundingClientRect().width);   // the panel may be SCALED: compare to the header's own box
    out.noUnderline=!core.querySelector('.spl-selh');
    const rg=[...core.querySelectorAll('.spl-rg')]; out.rgSel=rg[1]?rg[1].getAttribute('opacity')+'/'+rg[1].getAttribute('fill'):'';
    sp.pills.forEach(p=>p.on=false); sp.pills[0].on=true; /* mute band 1 */ window.__fx4VizPush={spl:[{nl:3,hz:[224,1200,0],pk:[0.1,0.1,0.1],gt:[0,1,1]}]}; window.__fx4Tick(); out.rgMuted=rg[0]?rg[0].getAttribute('opacity')+'/'+rg[0].getAttribute('fill'):''; out.ltMutedDim=core.querySelector('.spl-lt[data-lane="0"]').classList.contains('dim'); window.__fx4VizPush=null; sp.pills[0].on=false; window.__fx4Tick();
    out.chassis=[...card.querySelectorAll('.fxr-pills .fxr-t')].map(e=>e.textContent).join(',');
    window.__W.splice(0); sp.pills.forEach(p=>p.on=false); sp.back.d2.v='Latching'; card.querySelectorAll('.fxr-pills .fxr-pill')[1].click(); out.soloWrites=window.__W.splice(0).map(w=>w[0]+'='+w[1]); out.lane2Solo=!!sp.pills[4].on;
    sp.sel=0; window.__fxrRender(); for(let k=0;k<2;k++) window.__fx4Tick(); const card2=document.querySelectorAll('.fxr-dev')[si]; out.pillFollowsSel=card2.querySelectorAll('.fxr-pills .fxr-pill')[1].classList.contains('fxr-on');   // band 1 is NOT soloed → the proxy reads off
    sp.sel=1; window.__fxrRender(); for(let k=0;k<2;k++) window.__fx4Tick(); out.pillBackOn=document.querySelectorAll('.fxr-dev')[si].querySelectorAll('.fxr-pills .fxr-pill')[1].classList.contains('fxr-on');
    sp.pills.forEach(p=>p.on=false); return out; });
  chk(m3.lampBg==='rgb(255, 255, 255)' && m3.lampShadow==='none', 'Utility lamps FILL WHITE when on — no purple, no glow (fb448)', m3.lampBg+' shadow='+m3.lampShadow);
  chk(m3.lampCaps.join(',')==='Flip L,Flip R,Swap', 'the lamps are Flip L · Flip R · Swap — the DC lamp is GONE (fb450: a 15 Hz DC block is inaudible by nature)', m3.lampCaps.join(','));
  chk(/^0,0\.07,0,0$/.test(m3.rgOp) && m3.noUnderline && /^0\.07\/#B794FF$/.test(m3.rgSel||''), 'Splitter: the SELECTED band wears a low-opacity purple wash (0.07), the others are transparent, NO underline, NO box (fb451)', 'rg='+m3.rgOp+' sel='+m3.rgSel+' noUnderline='+m3.noUnderline);
  chk(/^0\.66\/#14121f$/.test(m3.rgMuted||'') && m3.ltMutedDim===true, 'a MUTED band is GREYED OUT — a dark wash over its spectrum and a dim number (fb451)', 'muted='+m3.rgMuted+' dim='+m3.ltMutedDim);
  chk(/^SPAN:20\|SPAN:\S+\*\|SPAN:\S+/.test(m3.lt||'') && m3.noSelClass===true, 'Splitter numbers are HTML spans, the LOW band says 20, the selected band is bright — and nothing wears the panel\'s ".sel" box class (fb451)', m3.lt+' noSel='+m3.noSelClass);
  chk(m3.swapW>0 && Math.abs(m3.plusW/m3.swapW-11/14)<0.08, 'the band + is 11/14 of the header\'s + box (smaller, same glyph)', 'plus='+m3.plusW+' header='+m3.swapW);
  chk(m3.chassis==='Mute,Solo,Flip' && m3.lane2Solo===true && m3.soloWrites.some(w=>/SYN_SPL_SOLO2=1/.test(w)), 'Splitter chassis pills = Mute · Solo · Flip for the SELECTED band: Solo with band 2 selected writes SOLO2', m3.chassis+' writes='+m3.soloWrites.join(','));
  chk(m3.pillFollowsSel===false && m3.pillBackOn===true, 'the proxy pills FOLLOW the selection (band 1: off · back to band 2: on)', JSON.stringify({b1:m3.pillFollowsSel,b2:m3.pillBackOn}));

  /* ═══ fb447 — THE SPLITTER'S BACK KNOBS WEAR THE BAND'S NAME PER TYPE, and the cells a Type cannot bind are
     DEAD (dim, '—'), never a knob that does nothing (Max: "lane three and lane four wasn't doing much"). ═══ */
  const rb=await pg.evaluate(()=>{ const out={}; const D=window.__fxrDevs(); const d=D.find(x=>x.core==='spl'); if(!d) return {err:'no splitter'};
    const idx=D.indexOf(d); const labels=()=>{ const ks=[...document.querySelectorAll('.fxr-dev')[idx].querySelectorAll('.fxr-bk-knob')]; const out=new Array(8).fill('?');
      ks.forEach(k=>{ const m=/grid-column:\s*(\d+);\s*grid-row:\s*(\d+)/.exec(k.getAttribute('style')||''); if(!m) return; const i=((+m[1])-1)/2+((+m[2])-1)*4; out[i]=k.querySelector('.fxr-lab').textContent+(k.classList.contains('fxr-bk-dead')?'†':''); }); return out; };
    const setType=(name)=>{ d.type=name; window.__fxApplyType(d,name); window.__fxrRender(); };   // what the real type-change path does
    setType('Low / Mid / High'); out.lmh=labels();
    setType('Low / High'); out.lh=labels(); out.lhB3=(window.__fxFmtTable['spl|B3'])(50,d); out.lhB4=(window.__fxFmtTable['spl|B4'])(50,d);
    setType('Mid / Side'); out.ms=labels(); out.msB6=(window.__fxFmtTable['spl|B6'])(50,d);
    setType('Low / Mid / High'); return out; });
  chk(rb.lmh&&rb.lmh.join('|')==='Low Gain|Mid Gain|High Gain|Mid Width|Spacing|Low Width|High Width|High Pan', 'Splitter L/M/H: the eight back knobs are named by BAND (Low Gain, Mid Width, Spacing …)', (rb.lmh||[]).join('|'));
  chk(rb.lh&&rb.lh.join('|')==='Low Gain|High Gain|Low Pan|—†|—†|Low Width|High Width|High Pan' && rb.lhB3==='C' && rb.lhB4==='—', 'Splitter Low/High: b3 = Low PAN (bound), b4 + Spacing DEAD and dimmed', (rb.lh||[]).join('|')+' B3='+rb.lhB3+' B4='+rb.lhB4);
  chk(rb.ms&&rb.ms[5]==='—†'&&rb.ms[2]==='Mid Pan'&&rb.ms[6]==='Side Width'&&rb.msB6==='—', 'Splitter Mid/Side: Mid Width is DEAD by construction, Side Width is the width, Mid Pan bound', (rb.ms||[]).join('|'));

  /* ═══ fb447 — SOLO MODE (the back dropdown fb446 declared and nothing read), through its REAL paths: the lane
     menu's Solo row (Exclusive un-solos the others) and press-and-hold on a band (Momentary solos while held).
     The Splitter has no S glyph pills (fb446 moved M/S/F into the right-click menu), so those are the paths. ═══ */
  const sm=await pg.evaluate(()=>{ const out={}; const D=window.__fxrDevs(); const d=D.find(x=>x.core==='spl'); const idx=D.indexOf(d); const W=()=>window.__W.splice(0);
    d.type='Low / Mid / High'; window.__fxApplyType(d,d.type); window.__fxrRender(); for(let k=0;k<3;k++) window.__fx4Tick();
    const card=document.querySelectorAll('.fxr-dev')[idx], core=card.querySelector('.fxr-core[data-core="spl"]'), svg=core.querySelector('svg'); const r=svg.getBoundingClientRect();
    const px=u=>r.left+r.width*u/226, py=v=>r.top+r.height*v/78; const cx=[...core.querySelectorAll('.spl-rg')].map(e=>(+e.getAttribute('x'))+(+e.getAttribute('width'))/2);
    let rows=null; const orig=window.__synShowMenu; window.__synShowMenu=function(tt,rw){ rows=rw; };
    const ev=(type,x,y,btn)=>{ const e=new PointerEvent(type,{bubbles:true,cancelable:true,clientX:x,clientY:y,button:btn||0,buttons:btn===2?2:1,pointerId:1,pointerType:'mouse'}); core.querySelectorAll('.spl-rg')[0].dispatchEvent(e); };
    const soloVia=(L)=>{ rows=null; ev('pointerdown',px(cx[L]),py(30),2); if(!rows) return 'no menu'; const row=rows.find(rw=>/^(Un)?solo /i.test(rw.label)); if(!row) return 'no solo row'; row.onPick(); return row.label; };
    d.back.d2.v='Exclusive'; d.pills.forEach(p=>p.on=false); W(); out.r1=soloVia(0); out.r2=soloVia(1); out.exS1=!!d.pills[1].on; out.exS2=!!d.pills[4].on; out.exWrites=W().map(w=>w[0]+'='+w[1]);
    d.pills.forEach(p=>p.on=false); d.back.d2.v='Momentary'; W(); ev('pointerdown',px(cx[2]),py(30),0); out.momDown=!!d.pills[7].on;
    document.dispatchEvent(new PointerEvent('pointerup',{bubbles:true,cancelable:true,clientX:px(cx[2]),clientY:py(30),button:0,pointerId:1,pointerType:'mouse'}));   // selecting the band re-rendered the rack; the release lands on the live document, as a real mouse's does
    out.momUp=!!d.pills[7].on; out.momWrites=W().map(w=>w[0]+'='+w[1]);
    d.back.d2.v='Latching'; window.__synShowMenu=orig; d.pills.forEach(p=>p.on=false); return out; });
  chk(sm.exS2===true && sm.exS1===false && sm.exWrites.some(w=>/SOLO1=0/.test(w)) && sm.exWrites.some(w=>/SOLO2=1/.test(w)), 'Solo Mode EXCLUSIVE: the menu\'s Solo on band 2 un-solos band 1 (model + the param writes)', JSON.stringify({r1:sm.r1,r2:sm.r2,s1:sm.exS1,s2:sm.exS2,writes:sm.exWrites}));
  chk(sm.momDown===true && sm.momUp===false && sm.momWrites.some(w=>/SOLO3=1/.test(w)) && sm.momWrites.some(w=>/SOLO3=0/.test(w)), 'Solo Mode MOMENTARY: press a band = solo, let go = off (model + the param writes)', JSON.stringify({down:sm.momDown,up:sm.momUp,writes:sm.momWrites}));

  /* ═══ fb446 — LANE CARDS: a device added INTO a Splitter band is hidden unless that band is selected,
     has NO route row (it is fed by the band), wears the band's range as a tag, and selecting another
     band hides it. Serum's "click the band, the chain switches", gated. Runs LAST: it rebuilds the rack. */
  const lcg=await pg.evaluate(()=>{ const out={}; const DEVS=window.__fxrDevs();
    try{
      DEVS.length=0; window.__fxAdd('spl'); const si=DEVS.findIndex(d=>d.core==='spl'); const sp=DEVS[si];
      window.__fxAdd('saturate',2,si);
      const card=()=>{ const di=DEVS.findIndex(d=>d.core==='saturate'); return {di,el:document.querySelector('.fxr-dev[data-dev="'+di+'"]')}; };
      let c=card(); out.added=c.di>=0; out.lane=DEVS[c.di]&&DEVS[c.di].lane; out.selAfterAdd=sp.sel;
      out.visibleWhenSelected=!!(c.el&&!c.el.classList.contains('fxr-hidden')); out.noRoute=!!(c.el&&!c.el.querySelector('.fxr-route'));
      out.laneClass=!!(c.el&&c.el.classList.contains('fxr-lanecard')); out.tag=c.el?((c.el.querySelector('.fxr-lane')||{}).textContent||''):'';
      sp.sel=0; window.__fxrRender(); try{ window.__fx4Tick(); }catch(e){} c=card(); out.hiddenWhenOther=!!(c.el&&c.el.classList.contains('fxr-hidden'));
      try{ window.__fx4Tick(); }catch(e){} const core=document.querySelector('.fxr-core[data-core="spl"]'); const rg=core.querySelector('.spl-rg[data-lane="1"]'); const r=rg.getBoundingClientRect();
      const ev=(t,x,y,b)=>new PointerEvent(t,{bubbles:true,cancelable:true,clientX:x,clientY:y,pointerId:7,pointerType:'mouse',button:b||0,buttons:b===2?2:1});
      rg.dispatchEvent(ev('pointerdown',r.left+r.width/2,r.top+r.height/2,0)); document.dispatchEvent(ev('pointerup',r.left+r.width/2,r.top+r.height/2,0));
      try{ window.__fx4Tick(); }catch(e){} c=card(); out.selAfterClick=sp.sel; out.visibleAfterClick=!!(c.el&&!c.el.classList.contains('fxr-hidden'));
    }catch(e){ out.err=String(e).slice(0,160); }
    return out; });
  chk(lcg.added&&lcg.lane===2&&lcg.selAfterAdd===1&&lcg.visibleWhenSelected, 'fxAdd INTO band 2 selects that band and the card is visible', JSON.stringify(lcg));
  chk(lcg.noRoute&&lcg.laneClass&&/–/.test(lcg.tag), 'a lane card has NO route row, the lane class, and wears the band RANGE as its tag', 'tag='+lcg.tag+' noRoute='+lcg.noRoute);
  chk(lcg.hiddenWhenOther===true, 'selecting a different band HIDES the lane card (the chain switches)', 'hidden='+lcg.hiddenWhenOther);
  chk(lcg.selAfterClick===1&&lcg.visibleAfterClick===true, 'clicking a band on the core SELECTS it and its chain shows again', JSON.stringify({sel:lcg.selAfterClick,vis:lcg.visibleAfterClick}));
  console.log('\n  PASS '+pass+'   FAIL '+fail+'\n');
  await b.close();
  process.exit(fail?1:0);
})().catch(e=>{ console.error('GATE CRASHED: '+(e&&e.stack||e)); process.exit(2); });
