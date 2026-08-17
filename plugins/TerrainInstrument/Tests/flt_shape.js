const puppeteer=require('puppeteer-core');
const P='/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Source/ui/public/index.html';
const OUT=process.argv[2]||'/tmp';
(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:1200,height:900,deviceScaleFactor:4});
  await pg.evaluateOnNewDocument(()=>{const mk=()=>({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,
    setNormalisedValue(){},getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
    window.Juce={getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
      getNativeFunction:()=>()=>new Promise(r=>r('{}')),backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
    (function(){const m=window.Juce;let h=m;Object.defineProperty(window,'Juce',{configurable:true,get(){return h;},
      set(v){h=Object.assign({},v||{},{getNativeFunction:m.getNativeFunction});}});})();
    window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
      __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};});
  await pg.goto('file://'+P,{waitUntil:'load'}); await new Promise(r=>setTimeout(r,1600));
  await pg.evaluate(()=>{const sp=document.getElementById('syn-panel'); if(sp) sp.style.display='block';
    window.dispatchEvent(new Event('resize'));});
  await new Promise(r=>setTimeout(r,1200));
  const res=await pg.evaluate(()=>{
    window.__fxAdd('flt');
    const D=window.__fxDevs(); const d=D[D.length-1];
    const names=d.back.d1.opts;
    const out={};
    // drive the feed like the plugin does, then sample the drawn curve for each shape
    const probe=(engName)=>{
      d.back.d1.v=engName;
      window.__fltVizPush=[{on:1,cut:1000,res:0.55,lvl:0.08,env:1,b:[-20,-22,-24,-26,-28,-30,-34,-38,-42,-46,-50,-54]}];
      window.__fltTick();
      const c=document.querySelector('.fxr-core[data-core="flt"] .flt-curve');
      const dd=c.getAttribute('d')||'';
      const ys=dd.split(/[ML]/).slice(1).map(t=>parseFloat(t.trim().split(' ')[1])).filter(v=>!isNaN(v));
      return {min:Math.min(...ys).toFixed(1), max:Math.max(...ys).toFixed(1), n:ys.length};
    };
    for(const n of ['SVF LP','SVF HP','SVF BP','SVF Notch','SEM Notch','Formant A','Comb +','Phaser 8P','Air'])
      out[n]=probe(n);
    return out;
  });
  for(const k of Object.keys(res)) console.log('  '+k.padEnd(12), JSON.stringify(res[k]));
  // and a picture of the notch
  await pg.evaluate(()=>{ const D=window.__fxDevs(); D[D.length-1].back.d1.v='SEM Notch';
    window.__fltVizPush=[{on:1,cut:1000,res:0.55,lvl:0.09,env:1,b:[-18,-20,-22,-24,-26,-30,-34,-38,-42,-46,-50,-54]}];
    window.__fltTick(); });
  const card=await pg.$('.fxr-dev'); if(card) await card.screenshot({path:OUT+'/flt-notch.png'});
  await b.close();
})().catch(e=>{console.error('ERR',e.message);process.exit(1);});
