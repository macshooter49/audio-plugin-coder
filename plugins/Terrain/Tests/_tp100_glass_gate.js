// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp100/tp101 — ONE GLASS TOP TO BOTTOM, FROSTED OVER THE SYNTH PAGE, LOOKED AT.
//    node Tests/_tp100_glass_gate.js
//  Max: "stop relying on numbers, look at it" — and then: the MOD matrix, Settings and the preset
//  browser are ONE dark TRANSPARENT glass (header band + page body + the native CAPTURE strip) over the
//  purple synth page, NOT a flat dark fill. For every glass page this renders the real page over a real
//  synth page and, from the SCREENSHOT (what a human sees):
//    1. ONE LAYER — a single window-wide coat (#plugin::before, 0→656) carries the backdrop blur, and the
//       header / sheet paint nothing of their own (no second glass, no seam at y=44).
//    2. NOT FLAT — header AND body are the purple frosted glass: bluer-than-grey by clearly more than the
//       old flat composite (--menu-bg over a flat --bg-main, what tp99 painted), and the body really
//       SEES the synth page: hiding the synth page under the glass changes the body's pixels.
//    3. ONE SURFACE — header == body within a few levels (header vs just under the seam, header vs the
//       bottom row), and the colour sent to the native CAPTURE strip (setStripGround / setBrowserGlass —
//       the strip is a JUCE component, so this is exactly what it paints) == the page's bottom row.
//  Writes a screenshot per page to $TMPDIR/tp100_<page>.png for a human look.
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
const PAGE=path.join(os.tmpdir(),'tp100.html'); fs.writeFileSync(PAGE,html);
let pass=0,fail=0; const ok=(c,l,d)=>{ (c?pass++:fail++); console.log('  '+(c?'PASS':'FAIL')+'  '+l+(d?'\n        '+d:'')); };
const hex=s=>{ s=String(s).replace(/^#?(ff)?/i,''); if(s.length>6) s=s.slice(-6); return [0,2,4].map(i=>parseInt(s.substr(i,2),16)); };
const dist=(a,b)=>Math.max(Math.abs(a[0]-b[0]),Math.abs(a[1]-b[1]),Math.abs(a[2]-b[2]));
const purp=c=>c[2]-(c[0]+c[1])/2;                     // how far a colour leans purple/blue off grey
const r1=c=>JSON.stringify(c.map(v=>Math.round(v*10)/10));
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:1});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();'+`
   (function(){ window.__stripLog=[]; const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>{
       if(n==='setStripGround'||n==='setBrowserGlass') return (...a)=>{ window.__stripLog.push([n].concat(a)); return Promise.resolve(); };
       return orig.getNativeFunction(n); } });
     Object.defineProperty(window,'Juce',{configurable:true,get(){return wrap;},set(){}}); })();`);
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>document.documentElement.setAttribute('data-theme','dark'));
 // a real synth page first: that is what every glass page stands on
 await p.evaluate(()=>{ const s=document.getElementById('syn-btn'); if(s&&!s.classList.contains('active')) s.click(); }); await sleep(900);
 // the flat composite tp99 painted: --menu-bg over a flat --bg-main (no blur, no saturate) — the thing we must NOT be
 const flat=await p.evaluate(()=>{ const cs=getComputedStyle(document.documentElement);
   const m=(cs.getPropertyValue('--menu-bg')||'').match(/[\d.]+/g).map(Number), g=(cs.getPropertyValue('--bg-main')||'').trim();
   const G=[1,3,5].map(i=>parseInt(g.substr(i,2),16)); return [0,1,2].map(i=>m[i]*m[3]+G[i]*(1-m[3])); });
 // strip colour the native component would paint right now: browser glass wins when on, else the ground
 const stripNow=()=>p.evaluate(()=>{ const L=window.__stripLog; let bg=null, gr=null;
   for(const e of L){ if(e[0]==='setBrowserGlass') bg=e; if(e[0]==='setStripGround') gr=e; }
   if(bg && +bg[1]===1) return String(bg[2]); return gr?String(gr[gr.length-1]):null; });
 // mean colour of rectangles in a screenshot (x0,y0,x1,y1 — exclusive), read back through a canvas
 async function shoot(file,rects){ await p.screenshot({path:file});
   return p.evaluate(async(src,rs)=>{ const im=new Image(); im.src=src; await im.decode(); const c=document.createElement('canvas'); c.width=im.width; c.height=im.height;
       const x=c.getContext('2d'); x.drawImage(im,0,0);
       return rs.map(([a,b2,cc,d])=>{ const D=x.getImageData(a,b2,cc-a,d-b2).data; let s=[0,0,0],n=0; for(let i=0;i<D.length;i+=4){ s[0]+=D[i]; s[1]+=D[i+1]; s[2]+=D[i+2]; n++; } return s.map(v=>v/n); }); },
     'data:image/png;base64,'+fs.readFileSync(file).toString('base64'),rects); }
 // regions that hold no text/controls on any glass page
 const HDR=[160,2,330,42], SEAM=[160,46,330,50], BOT=[160,648,330,656];
 const GRID=[]; for(let y=110;y<=590;y+=60) for(let x=20;x<=780;x+=95) GRID.push([x,y,x+24,y+24]);
 const pages=[
   ['mod',      '#mm',   async()=>{ await p.evaluate(()=>{ const m=document.getElementById('mod-btn'); if(m) m.click(); }); }],
   ['settings', '#st-b', async()=>{ await p.evaluate(()=>{ const s=document.getElementById('syn-btn'); if(s) s.click(); }); await sleep(400); await p.evaluate(()=>window.__st&&window.__st.open(true)); }],
   ['browser',  '#tp-b', async()=>{ await p.evaluate(()=>{ window.__st&&window.__st.open(false); if(window.__openPresetBrowser) window.__openPresetBrowser(); }); }],
 ];
 for(const [name,sheet,open] of pages){
   await open(); await sleep(900);
   await p.evaluate(()=>{ if(window.__tiStripKick) window.__tiStripKick(); }); await sleep(1300);
   // 1. ONE LAYER
   const st=await p.evaluate((sheet)=>{ const pl=document.getElementById('plugin'), co=getComputedStyle(pl,'::before'), h=getComputedStyle(document.getElementById('header')), sh=getComputedStyle(document.querySelector(sheet));
     const see=v=>v&&v!=='none'; return { coat:co.content!=='none'&&co.position==='absolute'&&co.top==='0px'&&co.bottom==='0px'&&/blur/.test(co.backdropFilter||co.webkitBackdropFilter||''),
       coatZ:co.zIndex, hZ:h.zIndex, hBg:h.backgroundColor, hBf:h.backdropFilter, shBg:sh.backgroundColor, shBf:sh.backdropFilter, plH:pl.getBoundingClientRect().height }; },sheet);
   ok(st.coat && st.plH===656 && +st.hZ>+st.coatZ, `[${name}] ONE window-wide glass coat (0→656) under the header's content`, JSON.stringify(st));
   ok(st.hBg==='rgba(0, 0, 0, 0)' && (!st.hBf||st.hBf==='none') && st.shBg==='rgba(0, 0, 0, 0)' && (!st.shBf||st.shBf==='none'),
      `[${name}] header and ${sheet} paint no glass of their own (no second layer, no seam)`, JSON.stringify(st));
   // 2/3 — look at it
   const shot=path.join(os.tmpdir(),'tp100_'+name+'.png');
   const [hdr,seam,bot,...grid]=await shoot(shot,[HDR,SEAM,BOT,...GRID]);
   const strip=await stripNow(), s=strip?hex(strip):null;
   ok(purp(hdr)>=purp(flat)+3 && purp(seam)>=purp(flat)+3,
      `[${name}] header AND body are frosted purple glass, not the flat fill (purple lean hdr ${purp(hdr).toFixed(1)} · body ${purp(seam).toFixed(1)} vs flat ${purp(flat).toFixed(1)})`,
      `header ${r1(hdr)} · body ${r1(seam)} · flat ${r1(flat)} · shot ${shot}`);
   // the body must SEE the synth page: take the synth page out from under the glass and the body changes
   await p.evaluate(()=>{ const s=document.getElementById('syn-panel'); s.dataset.g=s.style.visibility; s.style.visibility='hidden'; }); await sleep(300);
   const [,,,...grid0]=await shoot(path.join(os.tmpdir(),'tp100_'+name+'_nosynth.png'),[HDR,SEAM,BOT,...GRID]);
   await p.evaluate(()=>{ const s=document.getElementById('syn-panel'); s.style.visibility=s.dataset.g||''; }); await sleep(200);
   const seen=Math.max(...grid.map((c,i)=>dist(c,grid0[i])));
   ok(seen>=3, `[${name}] the synth page is visibly blurred THROUGH the body glass (max Δ ${seen.toFixed(1)} when it is taken away)`);
   const d1=dist(hdr,seam), d2=dist(hdr,bot);
   ok(d1<=3 && d2<=4, `[${name}] header == body: one surface (Δ header↔under-seam ${d1.toFixed(1)}, header↔bottom ${d2.toFixed(1)})`, `header ${r1(hdr)} · seam ${r1(seam)} · bottom ${r1(bot)}`);
   const d3=s?dist(bot,s):999;
   ok(s && d3<=2,`[${name}] the native CAPTURE strip is sent the glass's own bottom colour (Δ ${d3.toFixed(1)})`, `strip #${strip} = ${JSON.stringify(s)} · page bottom ${r1(bot)}`);
 }
 ok(errs.length===0,'no page errors',errs.join(' | '));
 console.log(`\n  ${pass} passed, ${fail} failed`); await b.close(); process.exit(fail?1:0);
})();
