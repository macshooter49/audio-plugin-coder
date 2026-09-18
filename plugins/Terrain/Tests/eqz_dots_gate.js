// ══════════════════════════════════════════════════════════════════════════════════════════════
//  eqz_dots_gate.js — tp44: THE DOT SITS ON ITS OWN BAND. NOTHING ELSE MOVES IT.
//
//    NODE_PATH=Tests/node_modules node Tests/eqz_dots_gate.js [page.html]
//
//  Max, 2026-09-18: "whenever I'm controlling a point in EQ, I don't want it to move the other point...
//  it's like a seven year old child with ADHD, it just can't stay still" — and on the Dynamic type:
//  "every time I press a MIDI it goes to its original position, and when I stop holding the MIDI it
//  shifts somewhere else and I can't drag it".
//
//  fb592 put every dot ON THE COMPOSITE LINE, so a wide band lifted its neighbours' dots and the Dynamic
//  ride (folded into the composite) dragged every dot with the audio. tp44 reverses that: the engine
//  pushes each node's OWN static curve (`bc`, pre-ride), the card draws it faintly, and the dot sits on
//  it — at its own gain, at its own frequency. The composite stays the bold truth of what you hear.
//
//  THE BARS
//   0  THE CARD IS THERE AND LAID OUT — element inventory fixed (8 band paths joined it)
//   1  EVERY VISIBLE DOT IS ON ITS OWN BAND'S CURVE, never on the composite (a tilt, a shelf, 8 bands)
//   2  MOVING ONE BAND MOVES NO OTHER DOT — and the ride (the composite and the post-ride db changing
//      under a fixed bc) moves NO dot at all
//   3  TWO BANDS AT ONE FREQUENCY WITH OPPOSITE GAINS DRAW APART — and each grabs its own
//   4  A DRAG WRITES ONE BAND (its Hz and gain) and no other parameter; the other dots stay put
//   5  THE WHEEL OVER A DOT WRITES THAT BAND'S Q ONLY
//   6  THE LOW BAND'S MENU ADDS A LOW CUT AT ITS CORNER (a free band, shape 1)
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
      const bc=sc.db.map(g=>new Array(48).fill(g));   /* each band's OWN curve: flat at its gain (the engine's real ones are bumps; flat makes the expectation exact) */
      window.__fx4VizPush={ eqz:[{ curve:curve, hz:sc.hz, db:sc.db, on:sc.on, lvl:0.5, bc:bc }] };
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
        .map(n=>({b:+n.getAttribute('data-b'), cy:+n.getAttribute('cy'), off:+( +n.getAttribute('cy') - yAt(+n.getAttribute('cx')) ).toFixed(2)}));
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

  // [1] each dot at 39 - 1.1 * its own gain — the composite (a tilt) is elsewhere, so a dot on the composite would miss
  let worst = 0, worstWhere = '', offLine = 0;
  results.forEach (r => { const sc = SCENS.find (x => x.name === r.name); r.dots.forEach (d => {
    const want = Math.max (3, Math.min (75, 39 - sc.db[d.b] * 1.1)); const e = Math.abs (d.cy - want);
    if (e > worst) { worst = e; worstWhere = `${r.name} band ${d.b} (cy ${d.cy} want ${want.toFixed(1)})`; }
    if (sc.tilt && Math.abs (d.off) < 0.3) offLine++;   /* a dot still ON the tilted composite = the fb592 law is back */
  }); });
  gate (worst <= 0.15,
        '[1] EVERY VISIBLE DOT IS ON ITS OWN BAND\'S CURVE (at its own gain), never on the composite',
        `worst ${worst.toFixed(2)} units (${worstWhere}) over ${results.length} states / ${results.reduce((a,r)=>a+r.dots.length,0)} dots`);

  // [2] band 1 goes to +12 with a composite that lifts EVERYWHERE (a wide band): dots 0, 2, 3 must not move;
  //     then the ride: composite and post-ride db go wild under the SAME bc — no dot moves at all
  const A = await measure ({ name: 'A', tilt: 0, hz:[100,550,3100,15500,632,632,632,632], db:[0,0,0,0,0,0,0,0], on:[1,1,1,1,0,0,0,0] });
  const B = await p.evaluate (async () => {
    const curve = new Array (96).fill (9);   /* the composite: lifted everywhere, as a very wide bell would */
    const bc = [0,12,0,0,0,0,0,0].map (g => new Array (48).fill (g));
    window.__fx4VizPush = { eqz: [{ curve, hz:[100,550,3100,15500,632,632,632,632], db:[0,12,0,0,0,0,0,0], on:[1,1,1,1,0,0,0,0], lvl:0.5, bc }] };
    for (let i = 0; i < 70; i++) { try { window.__fx4Tick(); } catch (e) {} await new Promise (r => setTimeout (r, 12)); }
    const core = document.querySelector ('.fxr-core[data-core="eqz"]');
    return [...core.querySelectorAll ('.eqz-n')].slice (0, 4).map (n => +n.getAttribute ('cy'));
  });
  const C = await p.evaluate (async () => {   /* the ride: the audio moves the composite and the post-ride gains; bc is the SAME */
    const curve = new Array (96).fill (0).map ((_, i) => Math.sin (i * 0.37) * 14);
    const bc = [0,12,0,0,0,0,0,0].map (g => new Array (48).fill (g));
    window.__fx4VizPush = { eqz: [{ curve, hz:[100,550,3100,15500,632,632,632,632], db:[-7,3,9,-11,0,0,0,0], on:[1,1,1,1,0,0,0,0], lvl:0.9, bc }] };
    for (let i = 0; i < 70; i++) { try { window.__fx4Tick(); } catch (e) {} await new Promise (r => setTimeout (r, 12)); }
    const core = document.querySelector ('.fxr-core[data-core="eqz"]');
    return [...core.querySelectorAll ('.eqz-n')].slice (0, 4).map (n => +n.getAttribute ('cy'));
  });
  const a0 = A.dots.map (d => d.cy);
  const othersStill = [0, 2, 3].every (b => Math.abs (B[b] - a0[b]) <= 0.15), oneMoved = Math.abs (B[1] - (39 - 12 * 1.1)) <= 0.15;
  const rideStill = [0, 1, 2, 3].every (b => Math.abs (C[b] - B[b]) <= 0.15);
  gate (othersStill && oneMoved && rideStill,
        '[2] MOVING ONE BAND MOVES NO OTHER DOT — and the Dynamic ride moves no dot at all',
        `flat ${JSON.stringify(a0)} → body +12 ${JSON.stringify(B)} (others still ${othersStill}, body at 25.8 ${oneMoved}) → ride ${JSON.stringify(C)} (still ${rideStill})`);

  // ── bar 3 — two bands stacked at one frequency, still individually grabbable ───────────────
  const stacked = await p.evaluate (async () => {
    window.__fx4VizPush={ eqz:[{ curve:new Array(96).fill(0),
      hz:[300,300,3100,15500,632,632,632,632], db:[18,-18,0,0,0,0,0,0], on:[1,1,1,1,0,0,0,0], lvl:0.5, bc:[18,-18,0,0,0,0,0,0].map(g=>new Array(48).fill(g)) }] };
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
    const cy1=+n1.getAttribute('cy');
    return { coincide, cx, cy, cy1, high: aim(cy), low: aim(cy1) };
  });
  gate (! stacked.coincide && stacked.high === 0 && stacked.low === 1,
        '[3] TWO BANDS AT ONE FREQUENCY WITH OPPOSITE GAINS DRAW APART — and each grabs its own',
        `+18 dB and -18 dB both at 300 Hz: cy ${stacked.cy} vs ${stacked.cy1} (coincide ${stacked.coincide}); on the boost's dot the hot band is ${stacked.high}, on the cut's ${stacked.low}`);

  // ── bars 4-6 — the hands: a drag, the wheel, the Low band's menu. Every parameter write is recorded. ──
  const hands = await p.evaluate (async () => {
    const W = []; const orig = window.__setSynParam; window.__setSynParam = function (id, v) { W.push ([String (id), +v]); return orig ? orig.apply (this, arguments) : undefined; };
    const core = document.querySelector ('.fxr-core[data-core="eqz"]'); const svg = core.querySelector ('svg'); const r = svg.getBoundingClientRect ();
    const toPx = (x, y) => ({ x: r.left + x / 226 * r.width, y: r.top + y / 78 * r.height });
    const dot = (b) => core.querySelector ('.eqz-n[data-b="' + b + '"]');
    const cyOf = (b) => +dot (b).getAttribute ('cy');
    const ev = (type, el, q, extra) => el.dispatchEvent (new PointerEvent (type, Object.assign ({ clientX: q.x, clientY: q.y, bubbles: true, cancelable: true, pointerId: 1, button: 0, isPrimary: true }, extra || {})));
    // [4] drag band 1 (Body) 24 px right and 16 px up
    const before = [0, 2, 3].map (cyOf); W.length = 0;
    const d1 = dot (1), q0 = toPx (+d1.getAttribute ('cx'), +d1.getAttribute ('cy'));
    ev ('pointerdown', d1, q0); for (let k = 1; k <= 8; k++) ev ('pointermove', document, { x: q0.x + 3 * k, y: q0.y - 2 * k }); ev ('pointerup', document, { x: q0.x + 24, y: q0.y - 16 });
    await new Promise (r => setTimeout (r, 100)); try { window.__fx4Tick (); } catch (e) {}
    const dragW = W.slice (); const dragIds = [...new Set (dragW.map (w => w[0]))];
    const after = [0, 2, 3].map (cyOf);
    // [5] the wheel over band 2 (Bite)
    W.length = 0; const d2 = dot (2), q2 = toPx (+d2.getAttribute ('cx'), +d2.getAttribute ('cy'));
    d2.dispatchEvent (new WheelEvent ('wheel', { clientX: q2.x, clientY: q2.y, deltaY: -100, bubbles: true, cancelable: true }));
    const wheelIds = [...new Set (W.map (w => w[0]))];
    // [6] right-click the Low dot: the menu rows are captured, the Low Cut row picked
    let rows = null; const om = window.__synShowMenu; window.__synShowMenu = function (t, rr) { rows = rr; };
    W.length = 0; const d0 = dot (0), q3 = toPx (+d0.getAttribute ('cx'), +d0.getAttribute ('cy'));
    ev ('pointerdown', d0, q3, { button: 2 }); ev ('pointerup', document, q3, { button: 2 });
    const labels = (rows || []).map (x => x.label || (x.isHeader ? '#' + x.label : '')); const cut = (rows || []).find (x => /Add a Low Cut here/.test (x.label || ''));
    if (cut && cut.onPick) cut.onPick ();
    await new Promise (r => setTimeout (r, 80)); window.__synShowMenu = om; window.__setSynParam = orig;
    const menuW = {}; W.forEach (w => menuW[w[0]] = w[1]);
    return { before, after, dragIds, wheelIds, labels, menuW, hasCut: !! cut };
  });
  const only = (ids, want) => ids.length > 0 && ids.every (x => want.indexOf (x) >= 0);
  gate (only (hands.dragIds, ['SYN_EQZ_BODYHZ', 'SYN_EQZ_BODY']) && hands.dragIds.length === 2 && hands.before.every ((v, i) => Math.abs (v - hands.after[i]) <= 0.15),
        '[4] A DRAG WRITES ONE BAND — Body\'s Hz and gain, nothing else — and the other dots stay put',
        `wrote ${JSON.stringify(hands.dragIds)}; dots 0/2/3 ${JSON.stringify(hands.before)} → ${JSON.stringify(hands.after)}`);
  gate (only (hands.wheelIds, ['SYN_EQZ_BITEQ']),
        '[5] THE WHEEL OVER A DOT WRITES THAT BAND\'S Q ONLY',
        `wrote ${JSON.stringify(hands.wheelIds)}`);
  const sh = hands.menuW['SYN_EQZ_X1SH'], on = hands.menuW['SYN_EQZ_X1ON'], hz = hands.menuW['SYN_EQZ_X1HZ'];
  gate (hands.hasCut && on === 1 && Math.abs (sh - 1 / 7) < 1e-6 && hz != null && Math.abs (hz - Math.log (300 / 20) / Math.log (1000)) < 0.03,
        '[6] THE LOW BAND\'S MENU ADDS A LOW CUT AT ITS CORNER (free band 1, shape Low Cut, at 300 Hz)',
        `rows ${JSON.stringify(hands.labels)}; X1ON ${on} X1SH ${sh} X1HZ ${hz} (want ${(Math.log(300/20)/Math.log(1000)).toFixed(3)})`);

  // [7] tp46 — THE PLOT AUTO-RANGES: +39 dB of Air (Amount 150 %) leaves through no edge and runs flat along none
  const rng = await p.evaluate (async () => {
    const curve = new Array (192).fill (0).map ((_, i) => i > 150 ? 39 * (i - 150) / 41 : 0);
    const bc = [0, 0, 0, 39, 0, 0, 0, 0].map ((g, b) => new Array (48).fill (0).map ((_, i) => b === 3 && i > 36 ? g * (i - 36) / 11 : 0));
    window.__fx4VizPush = { eqz: [{ curve, hz:[100,550,3100,15500,632,632,632,632], db:[0,0,0,39,0,0,0,0], on:[1,1,1,1,0,0,0,0], lvl:0.5, bc }] };
    for (let i = 0; i < 80; i++) { try { window.__fx4Tick(); } catch (e) {} await new Promise (r => setTimeout (r, 12)); }
    const core = document.querySelector ('.fxr-core[data-core="eqz"]'); const ys = core.querySelector ('.eqz-curve').getAttribute ('d').split (/[ML]/).filter (Boolean).map (s => +s.trim ().split (/\s+/)[1]);
    const by = core.querySelector ('.eqz-bc[data-b="3"]').getAttribute ('d').split (/[ML]/).filter (Boolean).map (s => +s.trim ().split (/\s+/)[1]);
    return { sc: window.__fxrDevs ()[0].__st.sc, minY: Math.min (...ys), flat: ys.filter (y => y <= 3.05).length, bandMinY: Math.min (...by) }; });
  gate (rng.sc < 1.0 && rng.minY >= 4 && rng.flat === 0 && rng.bandMinY >= 4,
        '[7] THE PLOT AUTO-RANGES — +39 dB runs flat along no edge (the "paint rubbing off")',
        `units/dB ${rng.sc.toFixed(3)} (1.1 = ±30 dB); composite top ${rng.minY}, ${rng.flat} points on the edge; band top ${rng.bandMinY}`);

  console.log (`\n  ${fail === 0 ? '✅' : '❌'} ${pass} passed, ${fail} failed\n`);
  await b.close(); process.exit (fail === 0 ? 0 : 1);
})();
