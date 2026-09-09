// fb606 — the UI against the C++ agent's ACTUAL emitted payload (not a hand-written stub).
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path');
const SC = '/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/wtfolders';
const PAGE = '/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/Terrain/Source/ui/public/index.html';
const REG = JSON.parse(fs.readFileSync(path.join(SC, 'payload/imports.json'), 'utf8'));
const WT_N = (() => { const s = fs.readFileSync('/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/Terrain/Source/PluginProcessor.cpp','utf8');
  const a = s.indexOf('ParameterIDs::SYN_OSC_A_WT_PRESET, 1 }'); const b = s.indexOf('juce::StringArray {', a); const e = s.indexOf('},', b);
  return (s.slice(b,e).replace(/\/\/[^\n]*/g,'').match(/"(?:[^"\\]|\\.)*"/g)||[]).length; })();
const settle = (ms) => new Promise((r) => setTimeout(r, ms));
let pass=0, fail=0; const chk=(ok,l,d)=>{ if(ok){pass++;console.log('  ok    '+l+(d?'\n          '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n          '+d:''));} };

const STUB = (cfg) => {
  const CH = {}; ['A','B','C','D'].forEach(o=>{ CH['SYN_OSC_'+o+'_WT_PRESET']=cfg.wtN; CH['SYN_OSC_'+o+'_ENGINE']=7; });
  window.__reg = cfg.reg; const states = new Map();
  const mk=(name,n)=>{const props={start:0,end:(n?n-1:1),skew:1,name,label:'',numSteps:n||100,interval:n?1:0,parameterIndex:states.size};
    const st={name,scaledValue:0,properties:props,getScaledValue:()=>st.scaledValue,setScaledValue(v){st.scaledValue=v;},
      getNormalisedValue(){return (st.scaledValue-props.start)/((props.end-props.start)||1);},
      setNormalisedValue(v){st.scaledValue=n?Math.round(v*(props.end-props.start)):v;(st.__ls||[]).forEach(f=>{try{f();}catch(e){}});},
      valueChangedEvent:{addListener(f){(st.__ls=st.__ls||[]).push(f);return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      getChoiceIndex:()=>st.scaledValue,setChoiceIndex(i){st.scaledValue=i;},getValue:()=>false,setValue(){},sliderDragStarted(){},sliderDragEnded(){}};return st;};
  const get=(nm)=>{ if(!states.has(nm))states.set(nm,mk(nm,CH[nm])); return states.get(nm); };
  const nativeFn=(n)=>(...a)=>new Promise(r=>{ if(n==='listWtImports')return r(JSON.stringify(window.__reg));
    if(n==='listImports')return r('[]'); if(/getPresets/i.test(n))return r('[]'); if(/Json|JSON/.test(n))return r('{}'); r(0); });
  window.Juce={getSliderState:get,getToggleState:get,getComboBoxState:get,getNativeFunction:nativeFn,backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction,getSliderState:mine.getSliderState});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',__juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
  Element.prototype.setPointerCapture=function(){}; Element.prototype.releasePointerCapture=function(){};
};
const HELP = () => {
  window.__panes=()=>[...document.querySelectorAll('.tpb-pane')];
  window.__rows=(p)=>[...p.children].map(d=>({el:d,name:(d.querySelector('span[style*="flex:1"]')||d).textContent.trim(),
    count:(()=>{const ss=[...d.querySelectorAll('span')];const s=ss[ss.length-1];return (s&&/^\d+$/.test(s.textContent.trim()))?+s.textContent.trim():null;})(),
    chev:!!d.querySelector('svg path[d^="M1.5 1.3"]')}));
  window.__cats=()=>{const p=window.__panes();return p.length<2?null:window.__rows(p[0]);};
  window.__items=()=>{const p=window.__panes();return p.length<2?null:window.__rows(p[1]);};
  window.__crumb=()=>{const c=document.querySelector('.tpb-crumb');return c?c.textContent.trim():null;};
  window.__openWt=()=>{try{if(window.__tpbClose)window.__tpbClose();}catch(e){}
    [...document.querySelectorAll('#syn-panel .device.osc')].forEach(d=>['engine-sample','engine-granular','engine-geode','engine-harm','engine-modal','engine-fm'].forEach(c=>d.classList.remove(c)));
    window.openWtSelectMenu('a',{clientX:180,clientY:120,preventDefault(){},stopPropagation(){}});return 'opened';};
  window.__clickCat=(nm)=>{const c=(window.__cats()||[]).find(r=>r.name===nm);if(!c)return 'no cat '+nm;c.el.click();return 'ok';};
};
(async()=>{
  const b=await puppeteer.launch({executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:820,height:656,deviceScaleFactor:2});
  const logs=[]; pg.on('console',m=>{const t=m.text(); if(/fb606/.test(t))logs.push(m.type()+': '+t);});
  await pg.evaluateOnNewDocument(STUB,{reg:REG,wtN:WT_N}); await pg.evaluateOnNewDocument(HELP);
  await pg.goto('file://'+PAGE,{waitUntil:'load',timeout:60000}); await settle(1400);
  await pg.evaluate(()=>{const sp=document.getElementById('syn-panel');if(sp)sp.style.display='block';window.dispatchEvent(new Event('resize'));}); await settle(700);
  console.log('\n══ fb606 — the UI against the C++ agent\'s ACTUAL payload/imports.json ══\n');
  await pg.evaluate(()=>window.__openWt()); await settle(500);
  const cats=await pg.evaluate(()=>window.__cats());
  console.log('   left column: [' + cats.map(c=>c.name+'('+c.count+(c.chev?'›':'')+')').join(' · ') + ']\n');
  const nm=cats.map(c=>c.name);
  chk(nm.indexOf('MASTER')>=0, 'the registered MASTER folder is a category', 'MASTER count '+(cats.find(c=>c.name==='MASTER')||{}).count+' (payload says count '+REG.folders[0].count+')');
  chk((cats.find(c=>c.name==='MASTER')||{}).chev===true, 'and it carries the has-subfolders chevron', 'payload depth '+REG.folders[0].depth+', subs '+JSON.stringify(REG.folders[0].subs));
  chk(nm.filter(x=>x==='Analog').length===1 && nm.filter(x=>x==='Vocal').length===1 && nm.filter(x=>x==='Spectral').length===1,
      'the factory bank MERGED into the ten — no duplicate Analog / Vocal / Spectral drawer',
      'Analog '+(cats.find(c=>c.name==='Analog')||{}).count+' · Vocal '+(cats.find(c=>c.name==='Vocal')||{}).count+' · Spectral '+(cats.find(c=>c.name==='Spectral')||{}).count);
  chk(nm.indexOf('Ash Fall')>=0, 'a factory folder the C++ side could NOT file (cat -1) keeps its own drawer instead of being guessed',
      '"Ash Fall" present, count '+(cats.find(c=>c.name==='Ash Fall')||{}).count);
  chk(cats.filter(c=>c.name!=='All'&&c.count===0).length===0, 'NO EMPTY CATEGORY — reg.factory being an OBJECT did not create a nameless drawer',
      'zero empty categories among ['+nm.join(' · ')+']');
  await pg.evaluate(()=>window.__clickCat('MASTER')); await settle(150);
  const l1=await pg.evaluate(()=>({cats:window.__cats().map(c=>c.name+'('+c.count+')'),items:window.__items().map(r=>r.name),crumb:window.__crumb()}));
  chk(l1.cats[0].indexOf('All')===0 && l1.cats.length===4,
      'descending MASTER shows its real subfolders (from the payload\'s own `rel`)', '['+l1.cats.join(' · ')+']  crumb "'+l1.crumb+'"');
  chk(l1.items.length===REG.folders[0].count, 'and the master view lists EVERY table beneath it', l1.items.length+' rows: '+l1.items.join(', '));
  console.log('\n  console:'); [...new Set(logs)].forEach(l=>console.log('    '+l));
  console.log('\n  '+pass+' passed, '+fail+' failed');
  await b.close(); process.exit(fail?1:0);
})().catch(e=>{console.error(e);process.exit(2);});
