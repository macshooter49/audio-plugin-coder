// fb394 — the TWO-PANE BROWSER HEADER gate. Max: "the search menu now clobbers the import menu…
// import wavetables, import noise, all that shit looks clobbered… make it seamless and transparent."
// Two things must hold FOREVER after this: (1) the Import label's position/size is identical whether
// the search exists or not, and (2) the search field draws NO box — no fill, no border, no radius.
const puppeteer = require('puppeteer-core');
const P=require('path').join(__dirname,'..')+'/Source/ui/public/index.html';
const OUT=process.argv[2]||'/private/tmp/claude-501/-Users-macshooter-Developer-VST-Plugins/f793112d-b160-4b3e-b0a3-d7da686e2a12/scratchpad/shots';
const fs=require('fs'); try{fs.mkdirSync(OUT,{recursive:true});}catch(e){}

const CASES=[
  {key:'wavetable', importLabel:'＋ Import Wavetable', audition:true, del:true,  search:undefined},
  {key:'sample',    importLabel:'＋ Import Sample',    audition:true, del:true,  search:undefined},
  {key:'noise',     importLabel:'＋ Import Noise',     audition:true, del:true,  search:undefined},
  {key:'filter94',  importLabel:null,                  audition:false,del:false, search:'Search 94 filters…'},
];
let FAIL=0; const bad=m=>{FAIL++; return '  ✗ '+m;};

(async()=>{
  const b=await puppeteer.launch({executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
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
  console.log('page errors:', errs.length?errs.slice(0,3):'none');

  const results=[];
  for (const c of CASES){
    const m = await pg.evaluate((c)=>{
      if(window.__tpbClose) try{window.__tpbClose();}catch(e){}
      const cats=[];
      const NAMES=['Acid 303','Acid Scream','Ladder LP 24','SEM Notch','Comb Positive','Reverb Metal','Vocal Ah','Wasp'];
      for(let g=0; g<9; g++){
        cats.push({label:['Ladder','State Variable','Vintage','Comb','Formant','Phaser','Effects','Reverb','Basic Shapes'][g],
          items: NAMES.map((n,i)=>({name:n+' '+g+i, sel:(g===0&&i===0), pick(){}}))});
      }
      if(c.del) cats.push({label:'My Imports', delKind:'folder', delPath:'/x', items:[{name:'thing.wav',pick(){}}]});
      const cfg={cats:cats, openCat:0};
      if(c.importLabel){ cfg.importLabel=c.importLabel; cfg.onImport=function(){}; }
      if(c.audition) cfg.onAudition=function(){};
      if(c.del) cfg.onDelete=function(){};
      if(c.search) cfg.searchPlaceholder=c.search;
      const panel=window.openTwoPaneBrowser({clientX:300,clientY:200}, cfg);
      panel.setAttribute('data-tpb-shot','1');
      const head=panel.children[0];
      const imp=head.children[0], inp=panel.querySelector('input'), row=panel.querySelector('.tpb-srow');
      const mag=row?row.querySelector('svg'):null;
      const cat0=panel.querySelector('.tpb-pane>div');           // first category row
      const rp=panel.getBoundingClientRect();
      // the panel renders under the plugin's UI transform — divide it out so every number is CSS px
      const K=rp.width/384;
      const L=e=>{ if(!e) return null; return +((e.getBoundingClientRect().left-rp.left)/K).toFixed(2); };
      const R=e=>{ if(!e) return null; const r=e.getBoundingClientRect();
        return {l:+((r.left-rp.left)/K).toFixed(1), t:+((r.top-rp.top)/K).toFixed(1),
                w:+(r.width/K).toFixed(1), h:+(r.height/K).toFixed(1)}; };
      const cs=inp?getComputedStyle(inp):null;
      return {
        scale:+K.toFixed(3), panelW:+(rp.width/K).toFixed(1), panelH:+(rp.height/K).toFixed(1),
        headH:+(head.getBoundingClientRect().height/K).toFixed(1),
        impRect:R(imp), impText:(imp&&imp.textContent||'').trim(),
        impWraps: imp? (imp.scrollHeight > imp.clientHeight+0.5) : false,
        impTrunc: imp? (imp.scrollWidth  > imp.clientWidth +0.5) : false,
        searchInHeader: !!(inp && head.contains(inp)),
        rowRect:R(row), inpRect:R(inp),
        // "no box" — every one of these must be zero / transparent
        bg: cs? cs.backgroundColor : null,
        borderW: cs? [cs.borderTopWidth,cs.borderRightWidth,cs.borderBottomWidth,cs.borderLeftWidth].join('/') : null,
        radius: cs? cs.borderRadius : null,
        boxShadow: cs? cs.boxShadow : null,
        // THE RAIL — magnifier, ＋ label and category letters must share one left edge. Compare the
        // GLYPH rails (box left + the box's own left padding), not the boxes: the paddings differ by
        // design and that is exactly what makes the glyphs line up.
        railMag:L(mag),
        railImp: imp? +(L(imp)+parseFloat(getComputedStyle(imp).paddingLeft||0)).toFixed(2) : null,
        railCat: cat0? +(L(cat0)+parseFloat(getComputedStyle(cat0).paddingLeft||0)).toFixed(2) : null,
      };
    }, c);
    const el = await pg.$('[data-tpb-shot="1"]');
    await el.screenshot({path:`${OUT}/tpb_${c.key}.png`});
    results.push([c.key,m]);
  }

  const moved = await pg.evaluate(()=>{
    const open=(withSearch)=>{
      if(window.__tpbClose) try{window.__tpbClose();}catch(e){}
      const cfg={cats:[{label:'A',items:[{name:'one',pick(){}}]}],openCat:0,
        importLabel:'＋ Import Wavetable',onImport(){},onAudition(){},onDelete(){}};
      if(!withSearch) cfg.search=false;
      const p=window.openTwoPaneBrowser({clientX:300,clientY:200},cfg);
      const rp=p.getBoundingClientRect(), K=rp.width/384, h=p.children[0];
      const ri=h.children[0].getBoundingClientRect(), rr=h.children[h.children.length-1].getBoundingClientRect();
      return {impL:+((ri.left-rp.left)/K).toFixed(2), impT:+((ri.top-rp.top)/K).toFixed(2), impW:+(ri.width/K).toFixed(2),
              impH:+(ri.height/K).toFixed(2), rightR:+((rp.right-rr.right)/K).toFixed(2),
              headH:+(h.getBoundingClientRect().height/K).toFixed(2)};
    };
    const off=open(false), on=open(true);
    if(window.__tpbClose) try{window.__tpbClose();}catch(e){}
    return {searchOff:off, searchOn:on, deltas:{
      L:+(on.impL-off.impL).toFixed(2), T:+(on.impT-off.impT).toFixed(2), W:+(on.impW-off.impW).toFixed(2),
      H:+(on.impH-off.impH).toFixed(2), right:+(on.rightR-off.rightR).toFixed(2), head:+(on.headH-off.headH).toFixed(2)}};
  });

  const fn = await pg.evaluate(()=>{
    if(window.__tpbClose) try{window.__tpbClose();}catch(e){}
    const cats=[{label:'Ladder',items:[{name:'Ladder LP 24',pick(){}},{name:'Acid 303',pick(){}}]},
                {label:'Vintage',items:[{name:'Acid Scream',pick(){}},{name:'SEM Notch',pick(){}}]}];
    const p=window.openTwoPaneBrowser({clientX:300,clientY:200},{cats,openCat:0,searchPlaceholder:'Search 94 filters…'});
    const inp=p.querySelector('input');
    const rows=()=>[...p.querySelectorAll('div')].filter(d=>/^(Ladder LP 24|Acid 303|Acid Scream|SEM Notch)$/.test(d.textContent.trim()));
    inp.value='acid'; inp.oninput();
    const hits=rows().map(d=>d.textContent.trim());
    inp.onkeydown({key:'Escape',stopPropagation(){}});           // Esc #1 clears
    const afterEsc=rows().map(d=>d.textContent.trim());
    const out={query_acid:hits.join(' | '), afterEsc:afterEsc.join(' | '), escCleared:inp.value===''};
    inp.onkeydown({key:'Escape',stopPropagation(){}});           // Esc #2 (empty) closes
    out.escClosed = !document.body.contains(p);
    if(window.__tpbClose) try{window.__tpbClose();}catch(e){}
    return out;
  });
  // does the field actually take focus on open?
  const focused = await pg.evaluate(async()=>{
    if(window.__tpbClose) try{window.__tpbClose();}catch(e){}
    const p=window.openTwoPaneBrowser({clientX:300,clientY:200},{cats:[{label:'A',items:[{name:'one',pick(){}}]}],openCat:0});
    await new Promise(r=>setTimeout(r,60));
    const ok = document.activeElement === p.querySelector('input');
    if(window.__tpbClose) try{window.__tpbClose();}catch(e){}
    return ok;
  });

  for(const [k,m] of results){
    console.log('── '+k+'   (UI scale ×'+m.scale+', all numbers in CSS px)');
    console.log('   panel '+m.panelW+'×'+m.panelH+'   header h='+m.headH+(Math.abs(m.headH-31)>0.6?bad('header is not 31px'):''));
    console.log('   import  "'+m.impText+'"  '+JSON.stringify(m.impRect)
      +(m.impWraps?bad('import label WRAPS'):'')+(m.impTrunc?bad('import label TRUNCATED'):''));
    console.log('   search  row='+JSON.stringify(m.rowRect)+'  inHeader='+m.searchInHeader);
    console.log('   no-box  bg='+m.bg+'  border='+m.borderW+'  radius='+m.radius+'  shadow='+m.boxShadow);
    if(!/rgba\(0, 0, 0, 0\)|transparent/.test(m.bg||'')) console.log(bad('search has a FILL'));
    if(!/^0px\/0px\/0px\/0px$/.test(m.borderW||'')) console.log(bad('search has a BORDER'));
    if(!/^0px$/.test(m.radius||'')) console.log(bad('search has a RADIUS'));
    if(!/none/.test(m.boxShadow||'')) console.log(bad('search has a SHADOW'));
    const rails=[m.railMag, (m.impText? m.railImp : null), m.railCat].filter(v=>v!=null&&v>0);   // an absent import label has no rail to share
    console.log('   rail    magnifier='+m.railMag+'  import='+m.railImp+'  categories='+m.railCat
      +'   spread='+(+(Math.max(...rails)-Math.min(...rails)).toFixed(2)));
    if(Math.max(...rails)-Math.min(...rails)>0.6) console.log(bad('glyphs are off one shared left rail'));
  }
  console.log('── import label, search OFF vs ON:'); console.log(JSON.stringify(moved,null,1));
  for(const k of Object.keys(moved.deltas)) if(moved.deltas[k]!==0) console.log(bad('import moved: Δ'+k+'='+moved.deltas[k]));
  console.log('── search behaviour:'); console.log(JSON.stringify(fn,null,1), 'focusOnOpen='+focused);
  if(fn.query_acid!=='Acid 303 | Acid Scream') console.log(bad('cross-category search broken'));
  if(!fn.escCleared) console.log(bad('Esc did not clear'));
  if(!fn.escClosed)  console.log(bad('Esc on empty did not close'));
  if(!focused)       console.log(bad('search does not take focus on open'));
  console.log('page errors:', errs.length?errs.slice(0,3):'none');
  if(errs.length) FAIL++;
  console.log(FAIL? ('══ '+FAIL+' FAILED') : '══ ALL GATES PASS');
  await b.close();
  process.exit(FAIL?1:0);
})();
