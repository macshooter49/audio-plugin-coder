// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp109 — THE PRESSURE SENSITIVITY ROWS (Settings → MIDI & Controllers → Expression): Pressure curve (a drawn
//  curve you drag, the Velocity curve's look), Pressure start, Pressure ceiling.
//    node Tests/_tp109_pressure_gate.js          (from plugins/Terrain; Tests/node_modules = puppeteer-core)
//  The processor owns the three values (every instance, MidiSettings.json) and answers every setter with
//  getMidiSettings' JSON. The page must: (1) light the rows when setPressureShape exists and say Soon when it does
//  not; (2) READ the processor back on open over a stale saved copy and push nothing; (3) WRITE through
//  setPressureShape(curve, start 0..1, ceiling 0..1) from the chips, a drag on the curve, the numbers and a
//  double-click reset, holding start and ceiling >= 10 % apart; (4) PERSIST: the values the processor kept come back
//  on the next open; (5) draw the curve through the start and the ceiling and place the live dot on it.
//  The stub stands in for the processor and keeps its values in localStorage, as MidiSettings.json would.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path'),os=require('os');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const cpp=fs.readFileSync('Source/PluginEditor.cpp','utf8');
const i0=cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0=cpp.indexOf('html = html.replace ("</body>", heroOverlay',i0);
const ov=[...cpp.slice(i0,j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m=>m[1]).join('');
const html=fs.readFileSync('Source/ui/public/index.html','utf8').replace('</body>',ov+'</body>');
const PAGE=path.join(os.tmpdir(),'tp109.html'); fs.writeFileSync(PAGE,html);
let pass=0,fail=0; const ok=(c,l,d)=>{ (c?pass++:fail++); console.log('  '+(c?'PASS':'FAIL')+'  '+l+(d?'\n        '+d:'')); };
for(const n of ['setPressureShape','getPressureIn'])
  ok(cpp.includes('withNativeFunction ("'+n+'"'), 'PluginEditor.cpp registers '+n);
const proc=fs.readFileSync('Source/PluginProcessor.cpp','utf8');
ok(/"pressCurve"/.test(proc)&&/"pressStart"/.test(proc)&&/"pressCeil"/.test(proc), 'the processor writes pressCurve / pressStart / pressCeil (getMidiSettings + MidiSettings.json)');

const law=(u,c,s,e)=>{ u=Math.min(1,Math.max(0,u)); if(s>0||e<1) u=Math.min(1,Math.max(0,(u-s)/(e-s))); return c?Math.pow(u,Math.pow(2,2.6*c)):u; };

(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const open=async(withNative,fresh)=>{
  const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:1});
  await p.evaluateOnNewDocument(stubSrc+'\nstub();'+`
   (function(){ window.__log=[]; window.__pressIn=0;
     if(${fresh?'true':'false'}) try{ localStorage.removeItem('tp109M'); }catch(e){}
     let M; try{ M=JSON.parse(localStorage.getItem('tp109M')); }catch(e){}
     if(!M) M={mpe:0,mpeBend:48,mpeArmed:0,chan:0,a4:440,voices:96,pressCurve:0.45,pressStart:0.12,pressCeil:0.9};
     const keep=()=>{ try{ localStorage.setItem('tp109M',JSON.stringify(M)); }catch(e){} };
     const J=()=>JSON.stringify(M);
     const cl=(v,lo,hi)=>Math.min(hi,Math.max(lo,v));
     const mine={ getMidiSettings:()=>Promise.resolve(J()),
       getPressureIn:()=>Promise.resolve(window.__pressIn) };
     if(${withNative?'true':'false'}) mine.setPressureShape=(c,s,e)=>{ window.__log.push(['setPressureShape',c,s,e]);
         M.pressCurve=cl(c,-1,1); M.pressStart=cl(s,0,.5); M.pressCeil=cl(e,.5,1); if(M.pressCeil-M.pressStart<.1) M.pressCeil=M.pressStart+.1; keep(); return Promise.resolve(J()); };
     window.__JUCE__.initialisationData.__juce__functions=Object.keys(mine).concat(['setVelCurve','getSettings','saveSettings','setMpeOn','setMidiChannel','setA4','setVoiceCeiling']);
     const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>mine[n]||orig.getNativeFunction(n) });
     Object.defineProperty(window,'Juce',{configurable:true,get(){return wrap;},set(){}});
     window.__stInitS={pressCurve:0,pressStart:0,pressCeil:100}; })();`);   // a STALE page copy: it must not win
  const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
  await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
  await p.evaluate(()=>{ window.__st.open(true); window.__st.go('control'); }); await sleep(500);
  return {p,errs};
 };
 const state=p=>p.evaluate(()=>{ const S=window.__st.S(); return {c:S.pressCurve,s:S.pressStart,e:S.pressCeil}; });
 const rowState=p=>p.evaluate(()=>{ const o={}; ['press','pressStart','pressCeil'].forEach(id=>{ const r=document.querySelector('#st-overlay [data-row="'+id+'"]'); o[id]=r?(r.classList.contains('soon')?'soon':'live'):'missing'; }); return o; });
 const lastCall=p=>p.evaluate(()=>window.__log[window.__log.length-1]||null);
 const drag=(p,sel,dy)=>p.evaluate((sel,dy)=>{ const e=document.querySelector(sel); e.scrollIntoView({block:'center'}); const r=e.getBoundingClientRect(), x=r.left+r.width/2, y=r.top+r.height/2;
   const ev=(t,yy)=>e.dispatchEvent(new PointerEvent(t,{bubbles:true,cancelable:true,button:0,clientX:x,clientY:yy,pointerId:7}));
   ev('pointerdown',y); for(let k=1;k<=10;k++) ev('pointermove',y+dy*k/10); ev('pointerup',y+dy); },sel,dy);

 // ── (1)(2) live, read back, nothing pushed ─────────────────────────────────────────────────────────────
 let {p,errs}=await open(true,true);
 const rows=await rowState(p);
 ok(Object.values(rows).every(v=>v==='live'), 'Expression: Pressure curve, start and ceiling are live (not Soon)', JSON.stringify(rows));
 const order=await p.evaluate(()=>[...document.querySelectorAll('#st-overlay [data-row]')].map(e=>e.dataset.row).filter(x=>['mpe','mpeBend','polyat','press','pressStart','pressCeil','ccmap'].includes(x)).join(','));
 ok(order==='mpe,mpeBend,polyat,press,pressStart,pressCeil,ccmap', 'the rows sit in the Expression group, after Poly aftertouch', order);
 const S1=await state(p);
 ok(S1.c===0.45&&S1.s===12&&S1.e===90, 'the page READ the processor back (0.45 / 12 % / 90 %) over its stale saved copy', JSON.stringify(S1));
 ok((await p.evaluate(()=>window.__log.length))===0, 'opening the page pushed nothing over the processor');
 const shown=await p.evaluate(()=>{ const t=r=>{ const e=document.querySelector('#st-overlay [data-row="'+r+'"]'); return e?e.textContent.replace(/\s+/g,' ').trim():''; }; return {press:t('press'),s:t('pressStart'),e:t('pressCeil')}; });
 ok(/harder 45 %/.test(shown.press)&&/12–90 %/.test(shown.press)&&/12/.test(shown.s)&&/90/.test(shown.e), 'the rows SHOW the processor\'s values', JSON.stringify(shown));
 const help=await p.evaluate(()=>{ return (window.__st.S()&&document.documentElement.innerHTML.match(/id:'press',[^\n]*?help:'([^']*)'/)||[])[1]||''; });
 ok(/any MPE or poly-aftertouch controller/.test(help)&&!/PolyBrute/.test(help), 'the help is generic ("for any MPE or poly-aftertouch controller", no brand)', help.slice(0,90)+'…');

 // ── (5) the drawing: flat 0 up to the start, the curve, flat 100 % from the ceiling; the live dot ────────
 const geo=await p.evaluate(()=>{ const b=document.querySelector('#st-overlay [data-row="press"]'); const d=b.querySelector('.mv-stroke').getAttribute('d');
   const pts=d.replace(/[ML]/g,' ').trim().split(/\s+/).map(Number); const xy=[]; for(let i=0;i<pts.length;i+=2) xy.push([pts[i],pts[i+1]]);
   return {xy, lo:+b.querySelector('.prs-lo').getAttribute('width'), hi:+b.querySelector('.prs-hi').getAttribute('x')}; });
 let geoErr=0; for(const [x,y] of geo.xy) geoErr=Math.max(geoErr,Math.abs((90-y)/88-law(x/150,.45,.12,.9)));
 ok(geo.xy.length>100&&geoErr<0.002&&Math.abs(geo.lo-18)<0.01&&Math.abs(geo.hi-135)<0.01, 'the curve is the processor\'s law (start 12 % flat, ceiling 90 % flat)', 'max |drawn - law| '+geoErr.toFixed(5)+' · start shade '+geo.lo+' · ceiling shade at '+geo.hi);
 await p.evaluate(()=>{ window.__pressIn=0.5; }); await sleep(250);
 const dot=await p.evaluate(()=>{ const b=document.querySelector('#st-overlay [data-row="press"]'); const d=b.querySelector('.dt'); return {cx:+d.getAttribute('cx'),cy:+d.getAttribute('cy'),t:b.querySelector('.prs-live').textContent}; });
 const want=law(.5,.45,.12,.9);
 ok(Math.abs(dot.cx-75)<0.01&&Math.abs((90-dot.cy)/88-want)<0.001&&dot.t.indexOf(Math.round(want*100)+' %')>0, 'the live dot: pressing 50 % lands on the curve', JSON.stringify(dot)+' want '+(want*100).toFixed(1)+' %');
 await p.evaluate(()=>document.querySelector('#st-overlay [data-row="press"]').scrollIntoView({block:'center'})); await sleep(100);
 const SHOT=path.join(os.tmpdir(),'tp109_expression.png'); await p.screenshot({path:SHOT}); console.log('        screenshot: '+SHOT);
 await p.evaluate(()=>{ window.__pressIn=0; }); await sleep(250);

 // ── (3) write: a chip, a drag on the curve, the numbers, a double-click reset, the 10 % gap ─────────────
 await p.evaluate(()=>{ const c=[...document.querySelectorAll('#st-overlay [data-row="press"] .tp-chip')].find(x=>x.textContent==='Harder'); c&&c.click(); }); await sleep(150);
 let L=await lastCall(p);
 ok(L&&L[1]===0.8&&Math.abs(L[2]-.12)<1e-9&&Math.abs(L[3]-.9)<1e-9, 'Harder calls setPressureShape(0.8, 0.12, 0.9)', JSON.stringify(L));
 // a drag = the pointer events dragify listens to (pointerdown, ten moves, pointerup), dispatched on the element itself
 await drag(p,'#st-overlay [data-row="press"] .crv',-90); await sleep(150);
 L=await lastCall(p); const afterDrag=await state(p);
 ok(L&&Math.abs(L[1]-(-0.4))<1e-9&&L[1]===afterDrag.c, 'dragging the curve UP 90 px softens it (0.8 -> '+(L&&L[1])+', want 0.8 - 2*90/150 = -0.4) through setPressureShape', JSON.stringify(L));
 await p.evaluate(()=>{ const nb=document.querySelector('#st-overlay [data-row="press"] .crv'); nb.dispatchEvent(new MouseEvent('dblclick',{bubbles:true})); }); await sleep(600);
 await p.evaluate(()=>{ const nb=document.querySelector('#st-overlay [data-row="press"] .crv'); nb.dispatchEvent(new MouseEvent('dblclick',{bubbles:true})); }); await sleep(150);
 L=await lastCall(p);
 ok(L&&L[1]===0, 'double-click on the curve = straight (setPressureShape(0, …))', JSON.stringify(L));

 await drag(p,'#st-overlay [data-row="pressStart"] .num b',-80); await sleep(150);
 L=await lastCall(p); const sS=await state(p);
 ok(L&&sS.s>12&&Math.abs(L[2]-sS.s/100)<1e-9&&Math.abs(L[3]-.9)<1e-9, 'dragging Pressure start up sends the new start (12 -> '+sS.s+' %) with the ceiling kept', JSON.stringify(L));
 // the ceiling pulled under start + 10: the page lowers the start (the processor would otherwise push the ceiling back up)
 await p.evaluate(()=>{ const S=window.__st.S(); S.pressStart=45; S.pressCeil=52; }); await p.evaluate(()=>{ const nb=document.querySelector('#st-overlay [data-row="pressCeil"] .num b'); nb.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,button:0,clientX:0,clientY:0,pointerId:1})); nb.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,clientX:0,clientY:40,pointerId:1})); nb.dispatchEvent(new PointerEvent('pointerup',{bubbles:true,pointerId:1})); }); await sleep(500);
 L=await lastCall(p); const gap=await state(p);
 ok(L&&gap.e-gap.s>=10&&Math.abs(L[2]-gap.s/100)<1e-9&&Math.abs(L[3]-gap.e/100)<1e-9, 'start and ceiling stay >= 10 % apart (the page and the processor agree)', JSON.stringify(gap)+' '+JSON.stringify(L));
 await p.evaluate(()=>{ const nb=document.querySelector('#st-overlay [data-row="pressCeil"] .num b'); nb.dispatchEvent(new MouseEvent('dblclick',{bubbles:true})); }); await sleep(150);
 await p.evaluate(()=>{ const nb=document.querySelector('#st-overlay [data-row="pressStart"] .num b'); nb.dispatchEvent(new MouseEvent('dblclick',{bubbles:true})); }); await sleep(150);
 L=await lastCall(p);
 ok(L&&L[2]===0&&L[3]===1, 'double-click on the numbers = start 0 %, ceiling 100 %', JSON.stringify(L));
 await p.evaluate(()=>{ const c=[...document.querySelectorAll('#st-overlay [data-row="press"] .tp-chip')].find(x=>x.textContent==='Hard'); c&&c.click(); }); await sleep(150);
 await p.evaluate(()=>{ const S=window.__st.S(); S.pressStart=8; }); await p.evaluate(()=>{ const nb=document.querySelector('#st-overlay [data-row="pressStart"] .num b'); nb.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,button:0,clientX:0,clientY:100,pointerId:1})); nb.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,clientX:0,clientY:100-6,pointerId:1})); nb.dispatchEvent(new PointerEvent('pointerup',{bubbles:true,pointerId:1})); }); await sleep(500);
 const final=await state(p); L=await lastCall(p);
 ok(errs.length===0, 'the page threw nothing', errs.join(' | ')||'clean');
 await p.close();

 // ── (4) persist: the next open reads what the processor kept ────────────────────────────────────────────
 ({p,errs}=await open(true,false));
 const S2=await state(p);
 ok(S2.c===final.c&&S2.s===final.s&&S2.e===final.e&&S2.c===0.45, 'the next open shows what the processor kept (MidiSettings.json)', 'kept '+JSON.stringify(final)+' · reopened '+JSON.stringify(S2));
 ok((await p.evaluate(()=>window.__log.length))===0, 'the reopen pushed nothing');
 await p.close();

 // ── (1b) no native: the rows say Soon and the curve does nothing ────────────────────────────────────────
 ({p,errs}=await open(false,true));
 const rows2=await rowState(p);
 ok(Object.values(rows2).every(v=>v==='soon'), 'without setPressureShape the three rows say Soon', JSON.stringify(rows2));
 ok(errs.length===0, 'the Soon page threw nothing', errs.join(' | ')||'clean');
 await p.close();
 await b.close();
 console.log('\n  '+pass+' passed, '+fail+' failed'); process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(2); });
