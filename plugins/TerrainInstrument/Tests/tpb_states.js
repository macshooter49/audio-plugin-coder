// fb394 — screenshot EVERY state of the browser header (Max's rule), not just the resting one.
const puppeteer = require('puppeteer-core');
const P='/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Source/ui/public/index.html';
const OUT=process.argv[2]||'./shots_states';
const fs=require('fs'); try{fs.mkdirSync(OUT,{recursive:true});}catch(e){}
(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:1560,height:1200,deviceScaleFactor:3});
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

  const build = (withImport) => {
    const cats=[];
    const NAMES=['Acid 303','Acid Scream','Ladder LP 24','SEM Notch','Comb Positive','Reverb Metal','Vocal Ah','Wasp'];
    ['Ladder','State Variable','Vintage','Comb','Formant','Phaser','Effects','Reverb'].forEach((L,g)=>
      cats.push({label:L, items:NAMES.map((n,i)=>({name:n+' '+g+i, sel:(g===0&&i===0), pick(){}}))}));
    cats.push({label:'My Folder', delKind:'folder', delPath:'/x', items:[{name:'sweep 01.wav',pick(){}},{name:'sweep 02.wav',pick(){}}]});
    const cfg={cats, openCat:0, onAudition(){}, onDelete(){}};
    if(withImport){ cfg.importLabel='＋ Import Wavetable'; cfg.onImport=function(){}; }
    else cfg.searchPlaceholder='Search 94 filters…';
    return cfg;
  };

  const shot = async (name, withImport, act) => {
    await pg.evaluate((wi,a,src)=>{
      if(window.__tpbClose) try{window.__tpbClose();}catch(e){}
      const cfg=(new Function('withImport','return ('+src+')(withImport)'))(wi);
      const p=window.openTwoPaneBrowser({clientX:300,clientY:200},cfg);
      p.setAttribute('data-shot','1');
      if(a==='typed'){ const i=p.querySelector('input'); i.value='acid'; i.oninput(); }
      if(a==='folder'){ const rows=[...p.querySelectorAll('.tpb-pane')][0].children;
        rows[rows.length-1].onclick(); }                        // select the user-import folder → "Delete Folder" appears
      if(a==='blur'){ p.querySelector('input').blur(); }
    }, withImport, act, build.toString());
    await new Promise(r=>setTimeout(r,120));
    const el=await pg.$('[data-shot="1"]');
    await el.screenshot({path:`${OUT}/${name}.png`});
    console.log('shot', name);
  };

  await shot('wt_typed',      true,  'typed');
  await shot('wt_folder',     true,  'folder');   // Delete Folder in the right cluster
  await shot('wt_blur',       true,  'blur');     // unfocused: magnifier back to .32
  await shot('flt_typed',     false, 'typed');
  console.log('page errors:', errs.length?errs.slice(0,3):'none');
  await b.close();
})();
