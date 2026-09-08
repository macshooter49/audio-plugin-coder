// ══════════════════════════════════════════════════════════════════════════════════════════════
//  lane_pills_gate.js — fb593: A LANE CARD'S PILLS OWN THE WHOLE COLUMN.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/lane_pills_gate.js [page.html]
//    (pass the pre-fb593 page for the MUTATION run — bars 2, 3 and 4 must go red)
//
//  Max, on a Widen inside the Splitter: "these two pills ... leave deadspace at the bottom, we dont
//  need this deadspace so could you re-organize this in a proper professional way ... retrig and
//  mono should be stacked and the pills should extend to fill up that space."
//
//  THE CAUSE: the right column has two forms. An ordinary card is [pills row][route row]; a LANE
//  card (a device inside a Splitter band) is pills ONLY, because its band does the routing. Nothing
//  replaced the route row, so exactly HALF the column was empty — measured 28.1 px of 56.2.
//  It is not even a new design: the lane-card comment in the builder already says "its pills fill
//  the column". That was the intent, never expressed in CSS.
//
//  THE BARS
//   0  THE ORDINARY CARD IS UNTOUCHED — pills still side by side, route row still there. This is the
//      bar that matters most: the fix must not reach the 12 devices that are not in a lane.
//   1  A LANE CARD STILL HAS ITS PILLS — the fix must not hide or collapse them (an early attempt
//      collapsed them to 2 px tall, which measures as "no dead space" and is worthless)
//   2  ZERO DEAD SPACE — nothing empty above or below the pills
//   3  AND THEY ARE STACKED, FULL WIDTH — distinct rows, each spanning the whole column
//   4  A ONE-PILL LANE CARD FILLS TOO — eqz / cmp / ott carry a single chassis pill, and half a
//      column of nothing is exactly the complaint
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const puppeteer = require ('puppeteer-core');
const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');

const STUB = () => {
  window.__VALS = {}; const states = {};
  const mk = (id) => states[id] || (states[id] = (function () {
    const L=[]; const n=()=>L.slice().forEach(f=>{try{f();}catch(e){}});
    return { getScaledValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
      getNormalisedValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
      setScaledValue(v){window.__VALS[id]=v;n();}, setNormalisedValue(v){window.__VALS[id]=v;n();},
      getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
      valueChangedEvent:{addListener(f){L.push(f);return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}};})());
  window.__stateFor=mk;
  window.Juce={getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
    getNativeFunction:(nm)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(nm))return r('[]');
      if(/getWaterfallView/i.test(nm))return r('{}');
      if(/Json|JSON|getOscWavetable|SamplePayload/i.test(nm))return r('{}'); r(0); }),
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction,
      getSliderState:mine.getSliderState,getToggleState:mine.getToggleState,getComboBoxState:mine.getComboBoxState});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
};

let pass=0, fail=0;
const gate=(ok,name,detail)=>{ ok?++pass:++fail; console.log(`  ${ok?'PASS':'FAIL'}  ${name}\n        ${detail}`); };

(async () => {
  const b = await puppeteer.launch ({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport ({ width:1280, height:900, deviceScaleFactor:2 });
  await p.evaluateOnNewDocument (STUB);
  await p.goto ('file://'+PAGE, { waitUntil:'load', timeout:60000 });
  await new Promise (r=>setTimeout(r,2200));
  await p.evaluate (()=>{ const sp=document.getElementById('syn-panel'); if(sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
  await new Promise (r=>setTimeout(r,1600));

  // measure a card, optionally converted to the SPLITTER-LANE dom exactly as the builder emits it
  const look = async (core, asLane) => {
    await p.evaluate ((c)=>{ try{ window.__fxAdd(c); }catch(e){} }, core);
    await new Promise (r=>setTimeout(r,650));
    return await p.evaluate (({c, lane}) => {
      const dev=[...document.querySelectorAll('.fxr-dev')].find(d=>d.querySelector('.fxr-core[data-core="'+c+'"]'));
      if(!dev) return {err:'no '+c};
      const rc=dev.querySelector('.fxr-rightcol');
      if(lane){ dev.classList.add('fxr-lanecard'); rc.classList.add('fxr-rc-lane');
                const rt=rc.querySelector('.fxr-route'); if(rt) rt.remove(); }
      const R=e=>{ if(!e) return null; const q=e.getBoundingClientRect();
        return {t:+q.top.toFixed(1),b:+q.bottom.toFixed(1),h:+q.height.toFixed(1),w:+q.width.toFixed(1),l:+q.left.toFixed(1)}; };
      const col=R(rc), pills=R(rc.querySelector('.fxr-pills'));
      const pl=[...rc.querySelectorAll('.fxr-pill')].map(R);
      return { col, pills, pl, hasRoute: !!rc.querySelector('.fxr-route'),
               above:+(pills.t-col.t).toFixed(1), below:+(col.b-pills.b).toFixed(1) };
    }, {c:core, lane:asLane});
  };

  const norm = await look ('reverb', false);
  const lane = await look ('wid', true);
  const one  = await look ('cmp', true);

  console.log (`\n══ lane_pills_gate — fb593 ══  ${PAGE}\n`);
  if (norm.err||lane.err||one.err) { console.log('  harness: '+(norm.err||lane.err||one.err)); await b.close(); process.exit(1); }

  const sideBySide = norm.pl.length===2 && Math.abs(norm.pl[0].t-norm.pl[1].t)<1 && norm.pl[0].l!==norm.pl[1].l;
  gate (norm.hasRoute && sideBySide,
        '[0] THE ORDINARY CARD IS UNTOUCHED',
        `route row present=${norm.hasRoute}; ${norm.pl.length} pills side by side (same top ${norm.pl[0].t}, different left ${norm.pl[0].l} vs ${norm.pl[1].l})`);

  const tall = lane.pl.every(q=>q.h>=10);
  gate (lane.pl.length===2 && tall,
        '[1] A LANE CARD STILL HAS ITS PILLS, AT A REAL SIZE',
        `${lane.pl.length} pills, heights ${lane.pl.map(q=>q.h).join(' / ')} px (an early attempt collapsed these to 2 px and still measured "no dead space")`);

  gate (Math.abs(lane.above)<=1 && Math.abs(lane.below)<=1,
        '[2] ZERO DEAD SPACE',
        `empty above ${lane.above} px, empty below ${lane.below} px  (was 0 above / 28.1 below — half the column)`);

  const stacked = lane.pl.length===2 && Math.abs(lane.pl[0].t-lane.pl[1].t)>5
                  && Math.abs(lane.pl[0].w-lane.col.w)<1 && Math.abs(lane.pl[1].w-lane.col.w)<1;
  gate (stacked,
        '[3] AND THEY ARE STACKED, EACH THE FULL COLUMN WIDTH',
        `tops ${lane.pl[0].t} / ${lane.pl[1].t}; widths ${lane.pl.map(q=>q.w).join(' / ')} vs column ${lane.col.w} (were 67.9 side by side)`);

  gate (one.pl.length===1 && Math.abs(one.above)<=1 && Math.abs(one.below)<=1,
        '[4] A ONE-PILL LANE CARD FILLS TOO',
        `${one.pl.length} pill ${one.pl[0].w}x${one.pl[0].h}; empty above ${one.above}, below ${one.below}`);

  console.log (`\n  ${fail===0?'✅':'❌'} ${pass} passed, ${fail} failed\n`);
  await b.close(); process.exit (fail===0?0:1);
})();
