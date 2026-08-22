// fb451 — THE CENTERLINE / SEPARATOR AUDIT, by measurement. A reusable probe, not a gate: run it after ANY change to the
// chassis CSS and read the table — every card kind must print the SAME last-dial→divider and divider→pill gaps and a
// pill-centre/dial-centre delta near 0. (The numbers that fixed fb451: 21.9 / 22.8 / −0.1 px at the panel's scale.)
//   NODE_PATH=<scratchpad>/node_modules node Tests/probe_centerline.js Source/ui/public/index.html
// fb451 — THE CENTERLINE / SEPARATOR AUDIT, by measurement (Max: "make that separator perfectly set between the pills and
// the four parameters, and that goes for everything"). For every card kind: the gap knobs→divider, divider→pills, and the
// pills' vertical centre against the knob dial's centre. Printed as a table; the CSS is tuned from these numbers.
const puppeteer=require('puppeteer-core');
const P=process.argv[2];
(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:1560,height:1200,deviceScaleFactor:2});
  await pg.evaluateOnNewDocument(()=>{ window.__PMAP={}; const mk=()=>({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
    window.Juce={getSliderState:mk,getToggleState:mk,getComboBoxState:mk,getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]'); if(/Json|JSON/.test(n))return r('{}'); r(0);}),backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
    (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
    window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',__juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}}; });
  await pg.goto('file://'+P,{waitUntil:'load',timeout:60000}); await new Promise(r=>setTimeout(r,1500));
  await pg.evaluate(()=>{ const sp=document.getElementById('syn-panel'); if(sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
  const rows=await pg.evaluate(()=>{
    const kinds=['reverb','delay','saturate','granular','tape','flt','cho','fla','pha','eqz','wid','cmp','ott','bod','utl','spl'];
    for(const k of kinds){ try{ window.__fxAdd(k); }catch(e){} }
    // a lane card: a Distortion inside the Splitter's band 2
    try{ const D=window.__fxrDevs(); const si=D.findIndex(d=>d.core==='spl'); window.__fxAdd('saturate',2,si); }catch(e){}
    try{ window.__fx4Tick(); }catch(e){}
    const out=[]; const D=window.__fxrDevs();
    document.querySelectorAll('.fxr-dev').forEach((card,i)=>{ const d=D[i]; if(!d) return; if(card.classList.contains('fxr-hidden')) card.classList.remove('fxr-hidden');
      const kn=card.querySelector('.fxr-knobs'), dv=card.querySelector('.fxr-divider'), rc=card.querySelector('.fxr-rightcol'), pl=card.querySelector('.fxr-pills'), rt=card.querySelector('.fxr-route');
      const dials=[...card.querySelectorAll('.fxr-knobs .fxr-dial')]; if(!kn||!dv||!rc||!dials.length) return;
      const K=kn.getBoundingClientRect(), V=dv.getBoundingClientRect(), C=rc.getBoundingClientRect(), last=dials[dials.length-1].getBoundingClientRect(), first=dials[0].getBoundingClientRect();
      const pr=pl?pl.getBoundingClientRect():null, pill=pl?pl.querySelector('.fxr-pill'):null, PR=pill?pill.getBoundingClientRect():null;
      const dialCy=(first.top+first.bottom)/2, lab=card.querySelector('.fxr-knobs .fxr-lab'), LB=lab?lab.getBoundingClientRect():null;
      const dx=dials.map(e=>{ const r=e.getBoundingClientRect(); return +((r.left+r.right)/2-K.left).toFixed(1); }), kw=+K.width.toFixed(1), nK=card.querySelectorAll('.fxr-knobs .fxr-knob').length;
      out.push({card:d.core+(card.classList.contains('fxr-lanecard')?'(lane)':''), kw, nK, dx, knobsR_to_div:+(V.left-K.right).toFixed(1), lastDial_to_div:+(V.left-last.right).toFixed(1), div_to_col:+(C.left-V.right).toFixed(1), div_to_pill:PR?+(PR.left-V.right).toFixed(1):null,
        dialCy:+dialCy.toFixed(1), pillCy:PR?+((PR.top+PR.bottom)/2).toFixed(1):null, pillH:PR?+PR.height.toFixed(1):null, pillsTop:pr?+pr.top.toFixed(1):null, routeCy:rt?+((rt.getBoundingClientRect().top+rt.getBoundingClientRect().bottom)/2).toFixed(1):null, labBot:LB?+LB.bottom.toFixed(1):null, divTop:+V.top.toFixed(1), divBot:+V.bottom.toFixed(1), cardBot:+card.getBoundingClientRect().bottom.toFixed(1)}); });
    return out; });
  console.log(JSON.stringify(rows,null,0).replace(/\},\{/g,'},\n{'));
  await b.close();
})().catch(e=>{ console.error('FAILED',e.stack||e.message); process.exit(1); });
