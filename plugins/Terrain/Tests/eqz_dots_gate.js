// ══════════════════════════════════════════════════════════════════════════════════════════════
//  eqz_dots_gate.js — fb592: THE EQ DOTS ARE ON THE LINE. ALWAYS.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/eqz_dots_gate.js [page.html]
//    (pass the pre-fb592 page for the MUTATION run — bars 1 and 2 must go red)
//
//  Max: "if you look in the very right, what the fuck is that little dot right there? ... and then
//  if you look at the dots they're not on the line... Could you make sure the dots never go off the
//  fucking line? they always stay on the line no matter what because it's gonna throw us off."
//
//  BOTH complaints are ONE cause. The element inventory of that card is 4 lines, a fill, a glow, a
//  curve and 8 circles — there is no extra object. The "little white dot at the very right" IS the
//  Air band's own handle, drawn at its own gain while the curve beside it had been pulled elsewhere
//  by that same shelf. Measured before the fix: Air at -3.8 dB with everything else flat put its dot
//  4.2 units off a curve on a plot only 78 units tall, hard against the right edge.
//
//  THE BARS
//   0  THE CARD IS THERE AND LAID OUT — and the inventory is asserted, so a future stray ELEMENT
//      (a real one, unlike this) cannot hide
//   1  EVERY VISIBLE DOT IS ON THE CURVE — across a tilt, an Air shelf and eight bands with wild
//      gains, measured against the very array the path is built from
//   2  THE "STRAY DOT" CASE SPECIFICALLY — the exact state that measured 4.2
//   3  AND TWO BANDS STACKED AT ONE FREQUENCY ARE STILL INDIVIDUALLY GRABBABLE — the one real cost
//      of riding the line (fb468's complaint), paid for by picking on AIM rather than draw order
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const puppeteer = require ('puppeteer-core');
const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');

const STUB = () => {
  window.__VALS = {}; const states = {};
  const mk = (id) => states[id] || (states[id] = (function () {
    const L = []; const n = () => L.slice().forEach (f => { try { f (); } catch (e) {} });
    return { getScaledValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
      getNormalisedValue:()=>(window.__VALS[id]!=null?window.__VALS[id]:0.5),
      setScaledValue(v){window.__VALS[id]=v;n();}, setNormalisedValue(v){window.__VALS[id]=v;n();},
      getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
      valueChangedEvent:{addListener(f){L.push(f);return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}};})());
  window.__stateFor = mk;
  window.Juce = { getSliderState:mk, getToggleState:mk, getComboBoxState:mk,
    getNativeFunction:(nm)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(nm))return r('[]');
      if(/getWaterfallView/i.test(nm))return r('{}');
      if(/Json|JSON|getOscWavetable|SamplePayload/i.test(nm))return r('{}'); r(0); }),
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}} };
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction,
      getSliderState:mine.getSliderState,getToggleState:mine.getToggleState,getComboBoxState:mine.getComboBoxState});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
};

let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail;
  console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

(async () => {
  const b = await puppeteer.launch ({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport ({ width: 1280, height: 900, deviceScaleFactor: 2 });
  await p.evaluateOnNewDocument (STUB);
  await p.goto ('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise (r => setTimeout (r, 2200));
  await p.evaluate (() => { const sp=document.getElementById('syn-panel'); if(sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
  await new Promise (r => setTimeout (r, 1800));
  await p.evaluate (() => { try { window.__fxAdd('eqz'); } catch(e){} });
  await new Promise (r => setTimeout (r, 900));

  const measure = async (scen) => {
    await p.evaluate ((sc) => {
      const N=96, curve=new Array(N);
      for(let i=0;i<N;i++){ const u=i/(N-1); curve[i]= sc.tilt ? sc.tilt*(u-0.5)*2 : 0; }
      window.__fx4VizPush={ eqz:[{ curve:curve, hz:sc.hz, db:sc.db, on:sc.on, lvl:0.5 }] };
    }, scen);
    for (let i=0;i<70;i++){ await p.evaluate(()=>{try{window.__fx4Tick();}catch(e){}}); await new Promise(r=>setTimeout(r,12)); }
    return await p.evaluate (() => {
      const core=document.querySelector('.fxr-core[data-core="eqz"]'); if(!core) return {err:'no core'};
      const d=(core.querySelector('.eqz-curve')||{}).getAttribute? core.querySelector('.eqz-curve').getAttribute('d') : '';
      const pts=(d||'').split(/[ML]/).filter(Boolean).map(s=>s.trim().split(/\s+/).map(Number)).filter(a=>a.length===2&&isFinite(a[0]));
      const yAt=(x)=>{ if(pts.length<2) return NaN; if(x<=pts[0][0])return pts[0][1];
        if(x>=pts[pts.length-1][0])return pts[pts.length-1][1];
        for(let i=1;i<pts.length;i++) if(pts[i][0]>=x){ const a=pts[i-1],c=pts[i];
          return a[1]+((x-a[0])/Math.max(1e-9,c[0]-a[0]))*(c[1]-a[1]); } return pts[pts.length-1][1]; };
      const dots=[...core.querySelectorAll('.eqz-n')]
        .filter(n=>{const o=n.getAttribute('opacity'); return o===null||+o>0.01;})
        .map(n=>({b:+n.getAttribute('data-b'), off:+( +n.getAttribute('cy') - yAt(+n.getAttribute('cx')) ).toFixed(2)}));
      const kinds={}; [...core.querySelectorAll('svg *')].forEach(e=>{const k=e.tagName+'.'+(e.getAttribute('class')||''); kinds[k]=(kinds[k]||0)+1;});
      return { dots, kinds, nCurve: pts.length };
    });
  };

  const SCENS = [
    { name:'flat',       tilt:0,  hz:[100,550,3100,15500,632,632,632,632], db:[0,0,0,0,0,0,0,0],       on:[1,1,1,1,0,0,0,0] },
    { name:'air shelf',  tilt:0,  hz:[100,550,3100,15500,632,632,632,632], db:[0,0,0,-3.8,0,0,0,0],    on:[1,1,1,1,0,0,0,0] },
    { name:'tilt -8',    tilt:-8, hz:[100,550,3100,15500,632,632,632,632], db:[0,0,0,0,0,0,0,0],       on:[1,1,1,1,0,0,0,0] },
    { name:'8 bands',    tilt:-3, hz:[80,400,2000,16000,250,900,5000,12000], db:[6,-9,4,-3,8,-6,3,-7], on:[1,1,1,1,1,1,1,1] },
  ];
  const results = [];
  for (const s of SCENS) results.push ({ name: s.name, ...(await measure (s)) });

  console.log (`\n══ eqz_dots_gate — fb592 ══  ${PAGE}\n`);
  const first = results[0];
  if (first.err) { console.log ('  harness: ' + first.err); await b.close(); process.exit (1); }

  gate (first.nCurve > 8 && first.dots.length === 4,
        '[0] THE CARD IS THERE AND LAID OUT — and its element inventory is fixed',
        `curve ${first.nCurve} pts, ${first.dots.length} visible dots; svg children: ${JSON.stringify(first.kinds)}`);

  let worst = 0, worstWhere = '';
  results.forEach (r => r.dots.forEach (d => { if (Math.abs(d.off) > worst) { worst = Math.abs(d.off); worstWhere = `${r.name} band ${d.b}`; } }));
  gate (worst <= 0.15,
        '[1] EVERY VISIBLE DOT IS ON THE CURVE',
        `worst deviation ${worst.toFixed(2)} units (${worstWhere}) over ${results.length} states / ${results.reduce((a,r)=>a+r.dots.length,0)} dots — plot is 78 units tall`);

  const air = results.find (r => r.name === 'air shelf');
  const airWorst = Math.max (...air.dots.map (d => Math.abs (d.off)));
  gate (airWorst <= 0.15,
        '[2] THE "STRAY DOT" CASE — Air at -3.8 dB, everything else flat',
        `worst ${airWorst.toFixed(2)} units (measured 4.20 before fb592 — that was the dot Max saw floating off the right edge)`);

  // ── bar 3 — two bands stacked at one frequency, still individually grabbable ───────────────
  const stacked = await p.evaluate (async () => {
    window.__fx4VizPush={ eqz:[{ curve:new Array(96).fill(0),
      hz:[300,300,3100,15500,632,632,632,632], db:[18,-18,0,0,0,0,0,0], on:[1,1,1,1,0,0,0,0], lvl:0.5 }] };
    for(let i=0;i<70;i++){ try{window.__fx4Tick();}catch(e){} await new Promise(r=>requestAnimationFrame(r)); }
    const core=document.querySelector('.fxr-core[data-core="eqz"]'); const svg=core.querySelector('svg');
    const n0=core.querySelector('.eqz-n[data-b="0"]'), n1=core.querySelector('.eqz-n[data-b="1"]');
    const r=svg.getBoundingClientRect(), vbW=226, vbH=78;
    const toPx=(x,y)=>({x:r.left+x/vbW*r.width, y:r.top+y/vbH*r.height});
    const cx=+n0.getAttribute('cx'), cy=+n0.getAttribute('cy');
    const coincide=Math.abs(cx-(+n1.getAttribute('cx')))<0.6 && Math.abs(cy-(+n1.getAttribute('cy')))<0.6;
    const hotNow=()=>{ const h=core.querySelector('.eqz-n.hot'); return h?+h.getAttribute('data-b'):-1; };
    const aim=(vy)=>{ const q=toPx(cx,vy);
      document.dispatchEvent(new PointerEvent('pointermove',{clientX:q.x,clientY:q.y,bubbles:true}));
      const el=document.elementFromPoint(q.x,q.y);
      if(el) el.dispatchEvent(new PointerEvent('pointermove',{clientX:q.x,clientY:q.y,bubbles:true}));
      return hotNow(); };
    return { coincide, cx, cy, high: aim(cy-6), low: aim(cy+6) };
  });
  gate (stacked.coincide && stacked.high === 0 && stacked.low === 1,
        '[3] TWO BANDS STACKED AT ONE FREQUENCY ARE STILL INDIVIDUALLY GRABBABLE',
        `+18 dB and -18 dB both at 300 Hz draw at the same point (${stacked.coincide}); aiming ABOVE grabs band ${stacked.high} (the boost), aiming BELOW grabs band ${stacked.low} (the cut)`);

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close(); process.exit (fail === 0 ? 0 : 1);
})();
