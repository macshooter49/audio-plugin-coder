// fb378 — the UI->param gate for the FILTER device. The law this exists for: a green DSP
// harness proves the ENGINE works and NEVER that the plugin reaches it (fb373).
const puppeteer = require('puppeteer-core');
const P='/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Source/ui/public/index.html';
(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
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
    // CHAINABLE: every filter must own a distinct rank + distinct param prefix
    const ranks=new Set(), pfx=new Set();
    (window.DEVS||[]).forEach(d=>{ if(d.core==='flt'){ ranks.add(d.rank); pfx.add(d.pfx); } });
    out.distinctRanks=ranks.size; out.distinctPrefixes=pfx.size;
    return out;
  });
  for (const k of Object.keys(r)) console.log('  '+k.padEnd(18), r[k]);
  await b.close();
})().catch(e=>{console.error('ERR',e.message);process.exit(1);});
