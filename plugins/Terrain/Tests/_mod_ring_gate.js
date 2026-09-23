// ══════════════════════════════════════════════════════════════════════════════════════════════
//  MOD RING — the live modulated value on every CIRCULAR dial (Max, 2026-09-23).
//
//    node Tests/_mod_ring_gate.js [index.html]        MR_SHOTS=<dir> also writes screenshots
//
//  "that purple line [on the Macro 2 knob] is our modulation … add that in all of our knobs — flow
//   cards, patcher, everywhere … ONLY CIRCULAR KNOBS … the SAME COLOR as our underline … it should
//   move smoothly … it must NOT take up CPU. No glow — strict purple."
//
//  Every bar drives the SHIPPED loop (window.__ulTick = one pass of the underline's own rAF body)
//  with routes made through the shipped door (__tiAddSrc) and live values set where the comet
//  reads them (window.__mvMacro — p2v()'s fallback). Nothing here re-implements the ring.
//
//  [0] the page boots whole, the ring's hooks exist
//  [1] 🚨 COVERAGE, synth page: every modulated dial that shows its underline carries exactly ONE
//      ring, per family (syn knobs · rack front · rack back · macros); pills / env chips carry none;
//      the synth filter device and the rack's Filter card carry none (Max: "except for the filters")
//  [2] 🚨 COLOUR: the ring's stroke IS its underline span's colour, rest and selected (#B794FF);
//      no filter / drop-shadow on the ring; a bypassed knob has no comet and no ring
//  [3] the ring moves with the value and sits on the dial's own geometry: centre = the value arc's
//      centre, radius outside it, inside the dial's box (syn: ≤ 0.7 px over, overflow visible)
//  [4] FIXED POSITIONS: no modulated knob moves or resizes by a single px when its ring appears
//  [5] FLOW CARDS: every card dial routed gets its ring (Arp + Shaper + Glitch cards)
//  [6] THE PATCHER: the nodes' dials (the same DOM, adopted) keep their rings
//  [7] CPU: the ring's share of one underline pass with 20 modulated dials; no write when nothing moved
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const SRC=process.argv[2]||'Source/ui/public/index.html';
const src=fs.readFileSync(SRC,'utf8');
const PAGE=path.join(require('os').tmpdir(),'mod_ring_gate.html'); fs.writeFileSync(PAGE,src);
const SHOTS=process.env.MR_SHOTS||'';
let pass=0,fail=0;
const ok=(c,l,d)=>{ if(c){pass++;console.log('  PASS  '+l+(d?'\n        '+d:''));} else {fail++;console.log('  FAIL  '+l+(d?'\n        '+d:''));} };

/* in-page helpers, installed once */
const HELP=()=>{
  window.__mr={
    w:ms=>new Promise(r=>setTimeout(r,ms)),
    vis(el){ if(!el||!el.isConnected) return false; const cs=getComputedStyle(el); if(cs.display==='none'||cs.visibility==='hidden') return false; const r=el.getBoundingClientRect(); return r.width>0&&r.height>0; },
    /* the underline that belongs to el: the .sm-ul whose ring lives in el */
    ringsIn(el){ return [...el.querySelectorAll('circle.sm-ring')]; },
    ringOn(el){ const rs=this.ringsIn(el).filter(r=>getComputedStyle(r).display!=='none'); return rs; },
    tick(){ return window.__ulTick(); },
    route(S,dest,depth){ const t=window.__tiAddSrc(S,dest); if(t!=null&&depth!=null) window.__tiSetDepth(t,dest,depth); return t; },
    fam(el){ return window.__smRingGeom?window.__smRingGeom(el):null; },
    ulFor(el){ const r=el.getBoundingClientRect(); let best=null,bd=1e9;
      document.querySelectorAll('.sm-ul').forEach(u=>{ if(u.style.display==='none') return; const q=u.getBoundingClientRect(); const d=Math.abs(q.left-r.left)+Math.abs(q.top-r.bottom)*0.2; if(q.left>=r.left-2&&q.right<=r.right+2&&q.top>=r.top-2&&q.top<=r.bottom+12&&d<bd){bd=d;best=u;} });
      return best; },
  };
};

(async()=>{
 const b=await puppeteer.launch({executablePath:process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:1200,height:900,deviceScaleFactor:SHOTS?3:1});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();');
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,200)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>{ document.documentElement.setAttribute('data-theme','dark'); window.setActivePanel('syn'); }); await sleep(700);
 await p.evaluate(HELP);

 // ── [0] boot ──
 const r0=await p.evaluate(()=>({ tick:typeof window.__ulTick, geom:typeof window.__smRingGeom, add:typeof window.__tiAddSrc, frame:typeof window.__tiFrameReg }));
 ok(r0.tick==='function'&&r0.geom==='function'&&r0.add==='function'&&r0.frame==='function'&&errs.length===0, '[0] the page booted whole; __ulTick / __smRingGeom / __tiAddSrc are there', JSON.stringify({r0,errs}));

 // ── [1] coverage on the synth page ──
 const r1=await p.evaluate(async()=>{ const M=window.__mr;
   ['reverb','delay','flt'].forEach(c=>{ try{ window.__fxrAdd(c); }catch(e){} }); await M.w(700);
   window.__mvMacro=[0.95,0.05,0.9,0.1,0.8,0.2,0.7,0.3,0.6];
   window.__selMod={mac:1};
   const made=[];   /* {el,dest,want:'ring'|'none',why} */
   const add=(el,dest,S,want,why,depth)=>{ if(!el) return; const t=M.route(S,dest,depth); if(t!=null) made.push({el,dest,want,why,tag:t}); };
   // synth knobs: every VISIBLE osc/noise knob with a destination
   document.querySelectorAll('#syn-panel .knob[data-syn], #syn-panel .noise-knobs .knob[data-mod-dest]').forEach((k,i)=>{ if(!M.vis(k)||k.closest('.device.filter')) return;
     const d=k.hasAttribute('data-mod-dest')?+k.getAttribute('data-mod-dest'):window.__knobDest(k.getAttribute('data-syn')); if(d==null) return;
     add(k,d,{mac:(i%2)?1:3},'ring','syn');});
   // rack: every front + back dial of every card (the back ones are off screen → their marks are down)
   document.querySelectorAll('#syn-panel .fxr-knob[data-mod-dest], #syn-panel .fxr-bk-knob[data-mod-dest]').forEach((k,i)=>{ const card=k.closest('.fxr-dev'); const flt=!!(card&&card.querySelector('.fxr-core[data-core="flt"]'));
     add(k,+k.getAttribute('data-mod-dest'),{mac:(i%2)?1:3},flt?'none':'ring',flt?'rack Filter card (excluded)':'rack'); });
   // the synth filter device (excluded): cutoff 0, res 54, back Mix 62
   [0,54,62,340].forEach(d=>{ const el=window.__tiElForDest(d); add(el,d,{mac:1},'none','synth filter (excluded)'); });
   // non-circular: env chips, blend pills, coarse pill
   [481,483].forEach(d=>{ const el=window.__tiElForDest(d); add(el,d,{mac:1},'none','env chip (not a dial)'); });
   [42].forEach(d=>{ const el=window.__tiElForDest(d); add(el,d,{mac:1},'none','coarse pill (not a dial)'); });
   await M.w(50); M.tick(); M.tick();
   const fam={}, bad=[]; let rings=0, dials=0, shown=0;
   made.forEach(m=>{ const el=window.__tiElForDest(m.dest)||m.el; const ul=M.ulFor(el); const on=M.ringOn(el).length, all=M.ringsIn(el).length;
     const k=m.why; fam[k]=fam[k]||{routed:0,marks:0,rings:0}; fam[k].routed++; if(ul) fam[k].marks++; fam[k].rings+=on;
     if(m.want==='ring'){ dials++; if(ul){ shown++; if(on!==1) bad.push({why:m.why,dest:m.dest,on,all}); } else if(on!==0) bad.push({why:m.why,dest:m.dest,offscreenRing:on}); }
     else if(all!==0) bad.push({why:m.why,dest:m.dest,ringOnExcluded:all});
     rings+=on; });
   return {fam,bad,rings,dials,shown,routes:(window.__tiRoutes()||[]).length}; });
 const f1=r1.fam;
 ok(r1.bad.length===0 && f1.syn&&f1.syn.rings>=10 && f1.rack&&f1.rack.rings>=8 && r1.rings===r1.shown && (f1['rack Filter card (excluded)']||{}).rings===0 && (f1['synth filter (excluded)']||{}).rings===0 && (f1['env chip (not a dial)']||{}).rings===0,
   '🚨 [1] every modulated dial whose underline is on screen carries exactly ONE ring (syn · rack front); dials off screen carry none; the synth filter, the rack Filter card, env chips and pills carry NONE', JSON.stringify(r1));

 // ── [1c] the filters, ON SCREEN: the rack Filter card scrolled into view and the synth filter's back panel shown ──
 const r1c=await p.evaluate(async()=>{ const M=window.__mr; const out={rack:[],syn:[]};
   const fc=[...document.querySelectorAll('#syn-panel .fxr-dev')].find(c=>c.querySelector('.fxr-core[data-core="flt"]'));
   if(fc){ fc.scrollIntoView({block:'nearest',inline:'nearest'}); await M.w(150); if(window.__tiClipCacheFlush) window.__tiClipCacheFlush(); M.tick(); M.tick();
     fc.querySelectorAll('.fxr-knob[data-mod-dest]').forEach(k=>out.rack.push({ul:!!M.ulFor(k),rings:M.ringsIn(k).length})); }
   const fb=document.querySelector('#syn-panel .device.filter .flt-back-knobs');   /* its back face stays shut (opening it floats the filter overlay over the page); the Patcher bar [6] sees the filter node */
   if(fb) fb.querySelectorAll('.knob').forEach(k=>out.syn.push({vis:M.vis(k),ul:!!M.ulFor(k),rings:M.ringsIn(k).length}));
   return out; });
 const rackShown=r1c.rack.filter(x=>x.ul).length, synShown=r1c.syn.filter(x=>x.ul).length;
 ok(rackShown>=3 && r1c.rack.every(x=>x.rings===0) && r1c.syn.every(x=>x.rings===0),
   '🚨 [1c] "except for the filters": with their marks ON SCREEN ('+rackShown+' rack Filter dials'+(synShown?(', '+synShown+' synth-filter back knobs'):'')+'), the filters carry NO ring', JSON.stringify(r1c));

 // ── [1b] macros view ──
 const r1b=await p.evaluate(async()=>{ const M=window.__mr; const btn=document.querySelector('#syn-panel .vm-macros-btn'); if(btn) btn.click(); await M.w(200);
   const mac=[...document.querySelectorAll('#syn-panel .vm-macro')]; const out=[];
   mac.slice(1,5).forEach((c,i)=>{ const d=+c.getAttribute('data-mod-dest'); M.route({mac:1},d,0.6); out.push(c); });
   M.tick(); M.tick();
   const res=out.map(c=>({ul:!!M.ulFor(c),on:M.ringOn(c).length,fam:M.fam(c)}));
   return {res}; });
 ok(r1b.res.length===4 && r1b.res.every(x=>x.fam==='rack'&&x.ul&&x.on===1), '[1b] the MACRO knobs (Max\'s reference: "Macro 2 … that purple line") each carry their ring when modulated', JSON.stringify(r1b));
 if(SHOTS){ const clip=await p.evaluate(()=>{ const v=document.querySelector('#syn-panel .vm-macroview')||document.querySelector('#syn-panel .vm-macro').parentElement; const r=v.getBoundingClientRect(); return {x:r.left-6,y:r.top-6,width:r.width+12,height:r.height+16}; }); await p.screenshot({path:SHOTS+'/macros_sel.png',clip}); }

 // ── [2] colour + states ──
 const r2=await p.evaluate(async()=>{ const M=window.__mr; const out={pairs:[],glow:[]};
   const rings=[...document.querySelectorAll('circle.sm-ring')].filter(r=>getComputedStyle(r).display!=='none');
   const span=(sel)=>{ const u=[...document.querySelectorAll('.sm-ul')].find(x=>x.style.display!=='none'&&x.classList.contains('sel')===sel&&!x.classList.contains('byp')); return u?getComputedStyle(u.children[1]).backgroundColor:null; };
   out.spanRest=span(false); out.spanSel=span(true);
   rings.forEach(r=>{ const cs=getComputedStyle(r); out.pairs.push({sel:r.classList.contains('sel'),stroke:cs.stroke}); if(cs.filter!=='none'||(cs.boxShadow&&cs.boxShadow!=='none')) out.glow.push(cs.filter+' / '+cs.boxShadow); });
   // sel follows __selMod exactly like the underline
   window.__selMod={mac:3}; M.tick(); const live=[...document.querySelectorAll('circle.sm-ring')].filter(r=>getComputedStyle(r).display!=='none');
   const selRings=live.filter(r=>r.classList.contains('sel')).length;
   /* per knob: the ring's .sel is its OWN underline's .sel (the filters have marks but no ring, so a count of marks would not do) */
   const selUls=live.filter(r=>{ const k=r.closest('.knob,.fxr-knob,.fxr-bk-knob,.vm-macro,.cell'); const u=k&&M.ulFor(k); return u&&u.classList.contains('sel'); }).length;
   out.selMismatch=live.filter(r=>{ const k=r.closest('.knob,.fxr-knob,.fxr-bk-knob,.vm-macro,.cell'); const u=k&&M.ulFor(k); return !u||(u.classList.contains('sel')!==r.classList.contains('sel')); }).length;
   window.__selMod={env:9}; M.tick(); out.noneSel=[...document.querySelectorAll('circle.sm-ring.sel')].filter(r=>getComputedStyle(r).display!=='none').length;
   window.__selMod={mac:1}; M.tick(); out.selRings=selRings; out.selUls=selUls;
   // bypass: take one syn knob's only route off → no comet, no ring
   const R=window.__tiRoutes(); const k=document.querySelector('#syn-panel .knob[data-syn] circle.sm-ring')?.closest('.knob'); let idx=-1, dest=null;
   if(k){ dest=window.__knobDest(k.getAttribute('data-syn')); idx=R.findIndex(x=>x.d===dest); }
   out.bypKnob=!!k; if(idx>=0){ window.__tiSetBypass(idx,1); M.tick(); out.bypRing=M.ringOn(k).length; const u=M.ulFor(k); out.bypUl=u?u.classList.contains('byp'):null; window.__tiSetBypass(idx,0); M.tick(); out.backRing=M.ringOn(k).length; }
   return out; });
 const strokes=r2.pairs.reduce((a,x)=>{ a[x.sel?'sel':'rest'].add(x.stroke); return a; },{sel:new Set(),rest:new Set()});
 ok(r2.spanRest && r2.spanSel && [...strokes.rest].every(s=>s===r2.spanRest) && [...strokes.sel].every(s=>s===r2.spanSel) && strokes.rest.size===1 && strokes.sel.size===1 && r2.spanSel==='rgb(183, 148, 255)',
   '🚨 [2] the ring\'s stroke IS the underline span\'s colour — at rest '+r2.spanRest+', selected '+r2.spanSel+' (#B794FF) — one colour per state, nothing new', JSON.stringify({rest:[...strokes.rest],sel:[...strokes.sel],spanRest:r2.spanRest,spanSel:r2.spanSel}));
 ok(r2.glow.length===0, '🚨 [2b] no glow: every ring computes filter:none and no shadow', JSON.stringify(r2.glow.slice(0,4)));
 ok(r2.selRings>0 && r2.selRings===r2.selUls && r2.selMismatch===0 && r2.noneSel===0, '[2c] the ring turns purple ONLY when its source is the selected one — the same knobs the underline lights (.sel), and none when another source is selected', JSON.stringify({selRings:r2.selRings,selUls:r2.selUls,selMismatch:r2.selMismatch,noneSel:r2.noneSel}));
 ok(r2.bypKnob && r2.bypRing===0 && r2.bypUl===true && r2.backRing===1, '[2d] bypassed: the underline goes .byp (dim territory, no comet) and the ring goes with the comet; un-bypass brings it back', JSON.stringify(r2));
 if(SHOTS){ await p.evaluate(()=>{ const r=document.querySelector('#syn-panel .knob circle.sm-ring'); const d=r&&r.closest('.device.osc'); if(d) d.classList.remove('osc-off'); }); await sleep(450);   /* picture only: headless boots with the oscs OFF, which greys + dims their whole face (ring included, as it should) */
 if(SHOTS){ const clip=await p.evaluate(()=>{ /* a LIT osc: an osc that is off dims its whole face (and its ring with it) */ const lit=el=>{ for(let n=el;n&&n!==document.body;n=n.parentElement){ const c=getComputedStyle(n); if(+c.opacity<1||c.filter!=='none') return false; } return true; }; let ks=[...document.querySelectorAll('#syn-panel .knob')].filter(k=>{ const r=k.querySelector('circle.sm-ring'); return r&&getComputedStyle(r).display!=='none'; }); const lk=ks.filter(lit); if(lk.length) ks=lk; ks=ks.slice(0,5).map(k=>k.getBoundingClientRect()); const l=Math.min(...ks.map(r=>r.left)), t=Math.min(...ks.map(r=>r.top)), rr=Math.max(...ks.map(r=>r.right)), bb=Math.max(...ks.map(r=>r.bottom)); return {x:l-8+scrollX,y:t-8+scrollY,width:rr-l+16,height:bb-t+16}; }); await p.screenshot({path:SHOTS+'/synth_knobs.png',clip});
   const clip2=await p.evaluate(()=>{ const c=document.querySelector('#syn-panel .fxr-knob circle.sm-ring').closest('.fxr-dev'); c.scrollIntoView({block:'nearest',inline:'nearest'}); const r=c.getBoundingClientRect(); return {x:r.left-4,y:r.top-4,width:r.width+8,height:r.height+8}; }); await p.evaluate(()=>window.__ulTick()); await p.screenshot({path:SHOTS+'/rack_front.png',clip:clip2});
   await p.evaluate(()=>{ window.__selMod={env:9}; window.__ulTick(); }); await p.screenshot({path:SHOTS+'/synth_knobs_rest.png',clip}); await p.screenshot({path:SHOTS+'/rack_front_rest.png',clip:clip2}); await p.evaluate(()=>{ window.__selMod={mac:1}; window.__ulTick(); }); } }

 // ── [3] geometry + motion ──
 const r3=await p.evaluate(async()=>{ const M=window.__mr; const out={bad:[],moved:null};
   const rings=[...document.querySelectorAll('circle.sm-ring')].filter(r=>getComputedStyle(r).display!=='none');
   rings.forEach(R=>{ const svg=R.ownerSVGElement, vb=svg.viewBox.baseVal, cx=+R.getAttribute('cx'), cy=+R.getAttribute('cy'), r=+R.getAttribute('r'), sw=+R.getAttribute('stroke-width');
     let vc=null, vr=null;
     const kv=svg.querySelector('.kr-v'), kf=svg.querySelector('.kf'), ps=svg.querySelectorAll('path');
     if(kv){ vc=[+kv.getAttribute('cx'),+kv.getAttribute('cy')]; vr=+kv.getAttribute('r')+1; }
     else if(kf){ vc=[+kf.getAttribute('cx'),+kf.getAttribute('cy')]; vr=+kf.getAttribute('r')+1; }
     else if(ps.length){ const m=/A ([\d.]+)/.exec(ps[0].getAttribute('d')); vc=[vb.width/2,vb.height/2]; vr=(m?+m[1]:0)+1; }
     const over=(r+sw/2)-vb.width/2;
     const fam=kv?'syn':(kf?'card':'rack');
     if(!vc||Math.abs(vc[0]-cx)>1e-6||Math.abs(vc[1]-cy)>1e-6||!(r-sw/2>vr)||over>(fam==='syn'?0.7:0)) out.bad.push({fam,cx,cy,r,sw,vc,vr,over}); });
   // motion: move the macro value, the ring's dash changes; hold it, nothing is written
   const R0=rings.find(r=>r.closest('.knob')); const a0=R0&&R0.getAttribute('stroke-dasharray');
   window.__mvMacro=[0.2,0.05,0.3,0.1,0.8,0.2,0.7,0.3,0.6]; M.tick(); const a1=R0&&R0.getAttribute('stroke-dasharray');
   let writes=0; const mo=new MutationObserver(l=>{ writes+=l.length; }); document.querySelectorAll('circle.sm-ring').forEach(r=>mo.observe(r,{attributes:true}));
   M.tick(); M.tick(); M.tick(); await M.w(20); mo.disconnect();
   window.__mvMacro=[0.95,0.05,0.9,0.1,0.8,0.2,0.7,0.3,0.6]; M.tick();
   out.moved=(a0!==a1); out.idleWrites=writes; out.n=rings.length; return out; });
 ok(r3.bad.length===0 && r3.n>0, '[3] every ring is concentric with its dial\'s value arc, sits OUTSIDE it, and stays inside the dial\'s box (the synth\'s 24 px ring: ≤ 0.7 px over, its svg is overflow:visible)', JSON.stringify(r3.bad.slice(0,5)));
 ok(r3.moved && r3.idleWrites===0, '[3b] the ring MOVES with the live value and writes NOTHING while the value holds (quarter-degree write gate)', JSON.stringify({moved:r3.moved,idleWrites:r3.idleWrites}));

 // ── [3c] ABSOLUTE ANCHOR: min → the live modulated value, independent of the white base ──
 //   The ring must ALWAYS start at the dial's minimum (the far-left start of the sweep angle) — i.e.
 //   stroke-dashoffset 0, so the arc begins at rotate(a0). Moving the white base knob must NOT move
 //   where the ring starts; only its END tracks the live modulated value.
 const r3c=await p.evaluate(async()=>{ const M=window.__mr; const out={};
   // (i) every ring on screen begins at the minimum: stroke-dashoffset reads 0 (arc starts at a0)
   const rings=[...document.querySelectorAll('circle.sm-ring')].filter(r=>getComputedStyle(r).display!=='none');
   out.n=rings.length;
   out.allAtMin = rings.length>0 && rings.every(r=>Math.abs(parseFloat(r.getAttribute('stroke-dashoffset')||'0'))<1e-6);
   out.maxOff = rings.reduce((m,r)=>Math.max(m,Math.abs(parseFloat(r.getAttribute('stroke-dashoffset')||'0'))),0);
   // (ii) drive ONE syn knob's base (its own white .kr-v arc, non-bipolar) low→high with the mod HELD;
   //      the start must not budge. Then move the MOD; the end must follow.
   const R=rings.find(r=>r.closest('.knob') && r.ownerSVGElement && r.ownerSVGElement.querySelector('.kr-v')
                        && !r.ownerSVGElement.parentNode.classList.contains('kr-bip'));
   if(R){ const kv=R.ownerSVGElement.querySelector('.kr-v');
     const read=()=>{ M.tick(); M.tick(); return {off:R.getAttribute('stroke-dashoffset'), da:R.getAttribute('stroke-dasharray'), shown:getComputedStyle(R).display!=='none'}; };
     window.__mvMacro=[0.6,0.05,0.6,0.1,0.8,0.2,0.7,0.3,0.6];              // mod held across the base sweep
     kv.style.strokeDashoffset='0'; kv.style.strokeDasharray='18.75';     // white base ≈ 0.25 (dl/75)
     const b1=read();
     kv.style.strokeDasharray='63.75';                                    // white base ≈ 0.85 — mod unchanged
     const b2=read();
     window.__mvMacro=[0.1,0.05,0.1,0.1,0.8,0.2,0.7,0.3,0.6];             // now MOVE the modulated value
     const m2=read();
     out.b1=b1; out.b2=b2; out.m2=m2;
     out.startFixed = b1.shown && b2.shown && b1.off===b2.off && Math.abs(parseFloat(b1.off))<1e-6;   // base moved, start did not
     out.endTracksMod = m2.shown && (m2.da!==b2.da);                                                  // mod moved, end followed
   }
   window.__mvMacro=[0.95,0.05,0.9,0.1,0.8,0.2,0.7,0.3,0.6]; M.tick();    // restore
   return out; });
 ok(r3c.n>0 && r3c.allAtMin && r3c.startFixed && r3c.endTracksMod,
   '🚨 [3c] ABSOLUTE ANCHOR — every ring starts at the dial\'s MIN-sweep angle (dashoffset 0, max seen '+ (r3c.maxOff!=null?r3c.maxOff.toFixed(4):'?') +'); moving the white base does NOT move that start, while the end tracks the live modulated value', JSON.stringify(r3c));

 // ── [4] fixed positions ──
 const r4=await p.evaluate(async()=>{ const M=window.__mr;
   const els=[...document.querySelectorAll('circle.sm-ring')].map(r=>r.closest('.knob,.fxr-knob,.fxr-bk-knob,.vm-macro,.cell')).filter(Boolean);
   const box=e=>{ const r=e.getBoundingClientRect(); const s=e.querySelector('svg'); const q=s?s.getBoundingClientRect():r; return [r.left,r.top,r.width,r.height,q.left,q.top,q.width,q.height].map(v=>Math.round(v*100)/100).join(','); };
   const withR=els.map(box); window.__smRingOff=true; document.querySelectorAll('circle.sm-ring').forEach(r=>r.style.display='none'); const without=els.map(box); window.__smRingOff=false;
   document.querySelectorAll('circle.sm-ring').forEach(r=>{ r.__k=''; }); M.tick();
   return {n:els.length,diff:withR.filter((x,i)=>x!==without[i]).length}; });
 ok(r4.n>0 && r4.diff===0, '[4] FIXED POSITIONS — '+r4.n+' modulated knobs: not one box (knob or its svg) moves or resizes by 0.01 px with the ring on', JSON.stringify(r4));

 // ── [5] flow cards ──
 const r5=await p.evaluate(async()=>{ const M=window.__mr; const out={cards:{},bad:[]};
   for(const kind of ['arp','chop','glitch','drift','lfo']){ document.querySelectorAll('.ti-card.open').forEach(c=>c.classList.remove('open'));
     try{ if(kind==='lfo'){ if(window.__openLfoCard) window.__openLfoCard(); } else { const c=window.__flowCardOf(kind,1); if(c&&c.open) c.open(); } }catch(e){} await M.w(500);
     const card=[...document.querySelectorAll('.ti-card.open')].pop(); if(!card) continue;
     const cr=card.getBoundingClientRect(); if(cr.bottom>innerHeight||cr.right>innerWidth){ card.style.left='20px'; card.style.top='20px'; await M.w(80); }   /* headless 1200×900: a card that opens past the edge is brought on screen */
     const cells=[...card.querySelectorAll('[data-mod-dest]')].filter(e=>M.vis(e));
     let dials=0, n=0; cells.slice(0,40).forEach((c,i)=>{ const d=+c.getAttribute('data-mod-dest'); M.route({mac:(i%2)?1:3},d,0.5); });
     M.tick(); M.tick();
     cells.slice(0,40).forEach(c=>{ const f=M.fam(c), on=M.ringOn(c).length, ul=!!M.ulFor(c); if(f==='card'){ dials++; if(ul){ n++; if(on!==1) out.bad.push({kind,d:c.getAttribute('data-mod-dest'),on}); } } else if(M.ringsIn(c).length) out.bad.push({kind,notDial:c.className,rings:M.ringsIn(c).length}); });
     out.cards[kind]={cells:cells.length,dials,shownWithRing:n,title:(card.querySelector('.h .tt')||{}).textContent};
   }
   return out; });
 const cardsOk=Object.values(r5.cards).filter(c=>c.shownWithRing>0).length;
 ok(r5.bad.length===0 && cardsOk>=3, '[5] FLOW CARDS — every routed card dial whose mark shows carries its ring; sliders / headers carry none', JSON.stringify(r5));
 if(SHOTS){ const clip=await p.evaluate(()=>{ const c=[...document.querySelectorAll('.ti-card.open')].find(x=>x.querySelector('circle.sm-ring')); if(!c) return null; const r=c.getBoundingClientRect(); return {x:Math.max(0,r.left-4),y:Math.max(0,r.top-4),width:Math.min(1190,r.width+8),height:Math.min(890,r.height+8)}; }); if(clip) await p.screenshot({path:SHOTS+'/flow_card.png',clip}); }
 await p.evaluate(async()=>{ document.querySelectorAll('.ti-card.open').forEach(c=>{ const x=c.querySelector('.h .x, .h .cl, .close'); if(x) x.click(); }); });

 // ── [6] the patcher ──
 await p.evaluate(()=>window.setActivePanel('tp')); await sleep(2200);
 const r6=await p.evaluate(async()=>{ const M=window.__mr;
   /* the tape machine: its three dials are CANVAS knobs (drawFxKnob) — put a Tape node on the canvas and route all three */
   let tapeErr=null; try{ window.__tpAddFromList('tape:0',520,160); }catch(e){ tapeErr=String(e); } await M.w(900);
   [1887,1888,1889].forEach((d,i)=>M.route({mac:(i%2)?1:3},d,0.6)); M.tick(); M.tick();
   const tape=[...document.querySelectorAll('.fx-knob-group[data-mod-dest]')].filter(g=>M.vis(g)).map(g=>({d:g.getAttribute('data-mod-dest'),ul:!!M.ulFor(g),on:M.ringOn(g).length,fam:M.fam(g)}));
   const nodes=(window.__tpRaw?window.__tpRaw():[]);
   const inTp=[...document.querySelectorAll('circle.sm-ring')].filter(r=>nodes.some(n=>n.el&&n.el.contains&&n.el.contains(r)));   /* a ring inside a node's adopted DOM */
   const shown=inTp.filter(r=>getComputedStyle(r).display!=='none'); const fltRings=[...document.querySelectorAll('circle.sm-ring')].filter(r=>r.closest('.device.filter')||(r.closest('.fxr-dev')&&r.closest('.fxr-dev').querySelector('.fxr-core[data-core="flt"]'))).length;
   const marks=[...document.querySelectorAll('.sm-ul')].filter(u=>u.style.display!=='none').length;
   return {tape,tapeErr,fltRings,nodes:nodes.length,kinds:[...new Set(nodes.map(n=>n.kind))],ringsInNodes:inTp.length,shown:shown.length,marks}; });
 const tapeShown=r6.tape.filter(t=>t.ul); ok(r6.nodes>0 && r6.shown>=8 && r6.fltRings===0 && tapeShown.length>=1 && tapeShown.every(t=>t.on===1&&t.fam==='tape'), '[6] THE PATCHER — the nodes host the same dials, and their rings ride along ('+r6.shown+' shown on the canvas); the Filter node carries none; the Tape node\'s canvas dials carry theirs ('+tapeShown.length+')', JSON.stringify(r6));
 if(SHOTS){ await p.screenshot({path:SHOTS+'/patcher_view.png'}); const clip=await p.evaluate(()=>{ const g=[...document.querySelectorAll('.fx-knob-group[data-mod-dest]')].find(x=>x.querySelector('circle.sm-ring')); if(!g) return null; const q=g.parentElement.getBoundingClientRect(); return {x:Math.max(0,q.left-8),y:Math.max(0,q.top-8),width:q.width+16,height:q.height+16}; }); if(clip) await p.screenshot({path:SHOTS+'/patcher_tape.png',clip}); }
 await p.evaluate(()=>window.setActivePanel('syn')); await sleep(900);

 // ── [7] CPU ──
 const r7=await p.evaluate(async()=>{ const M=window.__mr; M.tick();
   const n=[...document.querySelectorAll('circle.sm-ring')].filter(r=>getComputedStyle(r).display!=='none').length;
   const vals=i=>[0.5+0.45*Math.sin(i*0.3),0.05,0.5+0.4*Math.cos(i*0.21),0.1,0.8,0.2,0.7,0.3,0.6];
   const med=x=>{ const y=x.slice().sort((a,b)=>a-b); return y[y.length>>1]; };
   /* (a) the ring's OWN time inside the shipped pass (its stopwatch, window.__smRingProf), every value moving every frame */
   const N=300; window.__smRingProf={t:0,n:0,w:0}; for(let i=0;i<N;i++){ window.__mvMacro=vals(i); window.__ulTick(); } const P=window.__smRingProf; window.__smRingProf=null;
   /* (b) the WHOLE pass, rings on vs off, interleaved; every pass sees NEW values (on: even steps, off: odd — so the comets
      move in both) and settles its own style + layout inside its timing, so the ring's recalc is charged to it */
   const pass=(off,i)=>{ window.__smRingOff=off; window.__mvMacro=vals(i); void document.body.offsetHeight; const t0=performance.now(); window.__ulTick(); void document.body.offsetHeight; const dt=performance.now()-t0; window.__smRingOff=false; return dt; };
   const on=[],off=[]; for(let i=0;i<400;i+=2){ on.push(pass(false,i)); off.push(pass(true,i+1)); }
   /* (c) held values: nothing written */
   window.__mvMacro=vals(7); window.__ulTick(); window.__smRingProf={t:0,n:0,w:0}; for(let i=0;i<60;i++){ window.__mvMacro=vals(7); window.__ulTick(); } const H=window.__smRingProf; window.__smRingProf=null;
   return {rings:n, perFrameUs:P.t/N*1000, callsPerFrame:P.n/N, writesPerFrame:P.w/N, heldWrites:H.w, passOn:med(on), passOff:med(off)}; });
 ok(r7.rings>=20 && r7.perFrameUs<150 && r7.heldWrites===0 && (r7.passOn-r7.passOff)<0.35,
   '[7] CPU — '+r7.rings+' live rings, every value moving every frame: the ring\'s JS is ~'+r7.perFrameUs.toFixed(0)+' µs/frame ('+r7.writesPerFrame.toFixed(1)+' rings rewritten; the stopwatch is 100 µs-coarse headless, so this is a mean over 300 passes), 0 writes while values hold; the whole underline pass it rides: '+r7.passOn.toFixed(2)+' ms with rings vs '+r7.passOff.toFixed(2)+' ms without (style+layout settled inside each; no new timer, no new rAF)', JSON.stringify(r7));

 ok(errs.length===0, 'no page errors', JSON.stringify(errs.slice(0,4)));
 console.log('\n  '+pass+' passed, '+fail+' failed');
 await b.close(); process.exit(fail?1:0);
})();
