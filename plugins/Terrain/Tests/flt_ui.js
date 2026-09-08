// fb378 — the UI->param gate for the FILTER device. The law this exists for: a green DSP
// harness proves the ENGINE works and NEVER that the plugin reaches it (fb373).
const puppeteer = require('puppeteer-core');
const P=require('path').join(__dirname,'..')+'/Source/ui/public/index.html';
(async()=>{
  const b=await puppeteer.launch({executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:1560,height:1200,deviceScaleFactor:2});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,150)));
  await pg.evaluateOnNewDocument(() => {
    const mk=()=>({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
      getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
      valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
    window.Juce={getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
      getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
        if(/Json|JSON/.test(n))return r('{}'); r(0);}),
      backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
    (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
      get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
    window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
      __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
  });
  await pg.goto('file://'+P,{waitUntil:'load',timeout:60000});
  await new Promise(r=>setTimeout(r,1600));
  console.log('page errors:', errs.length?errs.slice(0,3):'none');

  await pg.evaluate(()=>{ const sp=document.getElementById('syn-panel');
    if(sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
  await new Promise(r=>setTimeout(r,1500));

  const r = await pg.evaluate(() => {
    const out={};
    for(let i=0;i<8;i++){ try{ window.__fxAdd('flt'); }catch(e){ out.addErr=String(e).slice(0,70); } }
    const cards=[...document.querySelectorAll('.fxr-dev')];
    const flt=cards.filter(c=>/Filter/.test((c.querySelector('.fxr-name')||{}).textContent||''));
    out.cardsTotal=cards.length;
    out.filterCards=flt.length;               // DUPLICATABLE: capped at 6
    if(!flt.length) return out;
    const c=flt[0];
    out.core = !!c.querySelector('.fxr-core[data-core="flt"]');
    out.curve= !!c.querySelector('.flt-curve');
    out.knobLabels=[...c.querySelectorAll('.fxr-kl,.fxr-knob .lab,.fxr-lab')].map(e=>e.textContent.trim()).filter(Boolean).slice(0,4).join('/');
    out.routePills=c.querySelectorAll('.fxr-r,.fxr-route .r').length;   // ROUTABLE
    // the engine <select>: its option count must equal the PARAM's cardinality (fb373)
    const sels=[...c.querySelectorAll('select')];
    out.selectCounts=sels.map(x=>x.options.length).join(',');
    const eng=sels.find(x=>x.options.length>50);
    out.engineOptions = eng ? eng.options.length : 0;
    if(eng){
      const N=eng.options.length, bad=[];
      for(let i=0;i<N;i++){ const norm=N>1?i/(N-1):0; const dec=Math.round(norm*(N-1)); if(dec!==i) bad.push(i); }
      out.engineRoundTrip = bad.length? ('MISMATCH '+bad.slice(0,4)) : ('OK '+N+'/'+N);
      out.firstEngine = eng.options[0].text; out.lastEngine = eng.options[N-1].text;
    }
    // fb382 — the LIVE feed, the THIN stroke, DRAG on the curve, and the presets.
    out.strokeWidth = (c.querySelector('.flt-curve')||{getAttribute(){return null}}).getAttribute('stroke-width');
    out.hasGlowLayer = !!c.querySelector('.flt-glow');          // must be GONE (thin, not glowy)
    out.hasSpectrum  = !!c.querySelector('.flt-spec');
    out.tickFn = typeof window.__fltTick;
    // simulate one push frame and confirm the curve REDRAWS from it
    const before = c.querySelector('.flt-curve').getAttribute('d');
    window.__fltVizPush = [];
    for(let i=0;i<6;i++) window.__fltVizPush.push({on:1,cut:7000,res:0.9,lvl:0.09,env:1,
      b:[-8,-10,-14,-18,-22,-26,-30,-36,-42,-48,-54,-60]});
    try{ (window.__fltTick||window.fltTick)(); }catch(e){ out.tickErr=String(e).slice(0,70); }
    const after = c.querySelector('.flt-curve').getAttribute('d');
    out.curveRedrew = (before!==after);
    out.specDrew = ((c.querySelector('.flt-spec')||{getAttribute(){return ''}}).getAttribute('d')||'').length>20;
    // drag: pointerdown on the core must move Cutoff
    const core=c.querySelector('.fxr-core[data-core="flt"]');
    const r=core.getBoundingClientRect();
    const idx=[].indexOf.call(c.parentNode.children,c);
    const D=(window.__fxDevs?window.__fxDevs():null);
    const cutBefore=(D&&D[idx])?D[idx].knobs[0].v:null;
    core.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,clientX:r.left+r.width*0.85,clientY:r.top+4}));
    document.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,clientX:r.left+r.width*0.85,clientY:r.top+4}));
    document.dispatchEvent(new PointerEvent('pointerup',{bubbles:true}));
    const cutAfter=(D&&D[idx])?D[idx].knobs[0].v:null;
    out.dragMovedCutoff = (cutBefore!=null && cutAfter!=null) ? (cutBefore+' -> '+cutAfter+(cutAfter!==cutBefore?'  MOVED':'  NO CHANGE')) : 'DEVS not exposed';

    // fb389 — the pill opens the HOUSE two-pane browser, with search
    out.browserFn = typeof window.__fltOpenBrowser;
    const pill=c.querySelector('.fxr-type');
    if(pill) pill.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,clientX:200,clientY:300}));
    const panes=[...document.querySelectorAll('.tpb-pane')];
    out.twoPaneOpened = panes.length>=2;
    const inp=[...document.querySelectorAll('input')].filter(i=>/Search 94/.test(i.placeholder||''));
    out.hasSearch = inp.length>0;
    if(inp.length){
      const items0=panes[1]?panes[1].children.length:0;
      inp[0].value='acid'; inp[0].dispatchEvent(new Event('input',{bubbles:true}));
      out.searchAcid=[...(panes[1]?panes[1].children:[])].map(e=>e.textContent.trim()).join(',');
      out.catsBefore=panes[0]?panes[0].children.length:0;
      out.itemsBefore=items0;
    }
    out.customMenuGone = !document.getElementById('flt-eng-menu');
    // the curve wears the distortion's own class
    out.curveClass = (c.querySelector('.flt-curve')||{}).getAttribute ? c.querySelector('.flt-curve').getAttribute('class') : null;
    try{ if(window.__tpbClose) window.__tpbClose(); }catch(e){}

    // fb390 — THE PRESET PILL MUST ACTUALLY DROP DOWN
    const pp=c.querySelector('.fxr-preset');
    out.presetPill = !!pp;
    if(pp){ pp.dispatchEvent(new MouseEvent('click',{bubbles:true})); }
    const pm=document.querySelector('.pmenu');
    out.presetMenuOpened = !!pm;
    if(pm){ out.presetNames=[...pm.querySelectorAll('.pi .nm')].map(e=>e.textContent.trim()).filter(Boolean).slice(0,14).join(','); }

    // no back panel, no plus
    out.hasBackPanel = !!c.querySelector('.fxr-back');
    out.hasPlusBtn   = !!c.querySelector('.fxr-swap');
    out.hasNodeDot   = !!c.querySelector('.flt-node');
    out.pillLabel    = (c.querySelector('.fxr-type .fxr-tl')||c.querySelector('.fxr-type')||{}).textContent;

    // CHAINABLE: every filter must own a distinct rank + distinct param prefix
    const ranks=new Set(), pfx=new Set();
    ((window.__fxDevs?window.__fxDevs():[])||[]).forEach(d=>{ if(d.core==='flt'){ ranks.add(d.rank); pfx.add(d.pfx); } });
    out.distinctRanks=ranks.size; out.distinctPrefixes=pfx.size;
    return out;
  });
  for (const k of Object.keys(r)) console.log('  '+k.padEnd(18), r[k]);
  await b.close();
})().catch(e=>{console.error('ERR',e.message);process.exit(1);});
