// fb392 — MEASURE the drag smoothness. A curve fed by a 15 Hz push steps: consecutive frames are
// IDENTICAL for ~4 frames then jump. A curve fed by the live slider state moves every frame.
const puppeteer=require('puppeteer-core');
const P='/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Source/ui/public/index.html';
(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:1200,height:900,deviceScaleFactor:2});
  await pg.evaluateOnNewDocument(()=>{
    // a REAL slider stub: it stores what is written and hands it back, like the plugin's does.
    const store={};
    const mk=(id)=>({ getScaledValue:()=>store[id]??0.5, setScaledValue(v){store[id]=v;},
      getNormalisedValue:()=>store[id]??0.5, setNormalisedValue(v){store[id]=v;},
      sliderDragStarted(){}, sliderDragEnded(){},
      getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
      valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
    window.Juce={getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
      getNativeFunction:()=>()=>new Promise(r=>r('{}')),backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
    (function(){const m=window.Juce;let h=m;Object.defineProperty(window,'Juce',{configurable:true,get(){return h;},
      set(v){h=Object.assign({},v||{},{getNativeFunction:m.getNativeFunction,getSliderState:m.getSliderState});}});})();
    window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
      __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};});
  await pg.goto('file://'+P,{waitUntil:'load'}); await new Promise(r=>setTimeout(r,1500));
  await pg.evaluate(()=>{const sp=document.getElementById('syn-panel'); if(sp) sp.style.display='block';
    window.dispatchEvent(new Event('resize'));});
  await new Promise(r=>setTimeout(r,1200));
  const res=await pg.evaluate(async()=>{
    window.__fxAdd('flt');
    await new Promise(r=>setTimeout(r,200));
    const core=document.querySelector('.fxr-core[data-core="flt"]');
    const curve=core.querySelector('.flt-curve');
    const r=core.getBoundingClientRect();
    // the push stays STALE on purpose: if the curve still moves, it is not reading the push.
    window.__fltVizPush=[{on:1,cut:1000,res:0.3,lvl:0.05,env:1}];
    core.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,clientX:r.left+r.width*0.12,clientY:r.top+r.height*0.5}));
    const ds=[];
    for(let i=0;i<40;i++){
      const x=r.left+r.width*(0.12+0.76*i/39);
      document.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,clientX:x,clientY:r.top+r.height*0.5}));
      await new Promise(rq=>requestAnimationFrame(rq));
      ds.push(curve.getAttribute('d')||'');
    }
    document.dispatchEvent(new PointerEvent('pointerup',{bubbles:true}));
    let same=0, moved=0;
    for(let i=1;i<ds.length;i++){ if(ds[i]===ds[i-1]) same++; else moved++; }
    // and how far the drawn knee travelled, in plot units
    const kneeOf=(d)=>{ const pts=d.split(/[ML]/).slice(1).map(t=>t.trim().split(' ').map(Number));
      let lo=1e9,xi=0; pts.forEach(p=>{ if(p[1]<lo){lo=p[1];xi=p[0];} }); return xi; };
    return { frames:ds.length, movedFrames:moved, staleFrames:same,
             kneeStart:+kneeOf(ds[0]).toFixed(1), kneeEnd:+kneeOf(ds[ds.length-1]).toFixed(1) };
  });
  console.log(JSON.stringify(res,null,1));
  await b.close();
})().catch(e=>{console.error('ERR',e.message);process.exit(1);});
