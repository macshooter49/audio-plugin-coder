// tp77 — the card as Max sees it: docked, the Filter lane (arrows + long word), the Sine, a free-drawn stroke.
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const src=fs.readFileSync('Source/ui/public/index.html','utf8');
const PAGE=path.join(require('os').tmpdir(),'shot77.html'); fs.writeFileSync(PAGE,src);
const OUT=process.argv[2]||'/tmp/shot77';
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:1200,height:900,deviceScaleFactor:2});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();');
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>{ document.documentElement.setAttribute('data-theme','dark'); window.setActivePanel('syn'); }); await sleep(600);
 await p.evaluate(async()=>{ window.__flowCardOf('chop',1).open(); await new Promise(r=>setTimeout(r,800)); });
 await p.evaluate(()=>{ const card=document.querySelector('.ti-card.shp-ext.open'); card.style.left='6px'; card.style.top='6px';   /* tp77 — PARK THE CARD ON SCREEN. At its default zoom the card measures ~560 x 800 device px (body zoom 1.46 x card zoom 1.2) and opens near x 880, so its right third hung off a 1200 px viewport: a drawn stroke simply stopped at the edge (measured 59 of 91 moves reaching the field, the rest landing on <html>) and every gesture bar was silently short. */ }); await sleep(200);
 const shot=async(n)=>{ await (await p.$('.ti-card.shp-ext.open')).screenshot({path:OUT+'_'+n+'.png'}); };
 // 1 — Volume, the boot gate
 await shot('1vol');
 // 2 — the Filter lane: the arrows and the long word, the middle chip centred
 await p.evaluate(async()=>{ const t=[...document.querySelectorAll('.ti-card.shp-ext.open .fx .fxb')]; t[2].click(); await new Promise(r=>setTimeout(r,400)); }); await sleep(300);
 await shot('2filt');
 // 3 — the Sine
 await p.evaluate(async()=>{ const c=document.querySelector('.ti-card.shp-ext.open'); const ss=c.querySelector('.ch-shape select');
   const o=[...ss.querySelectorAll('option')]; const k=o.findIndex(q=>/^Sine$/i.test(q.textContent)); ss.value=o[k].value; ss.dispatchEvent(new Event('change',{bubbles:true})); }); await sleep(400);
 await shot('3sine');
 // 4 — a free-drawn stroke that starts INSIDE the field (the old cliff)
 await p.evaluate(async()=>{ const c=document.querySelector('.ti-card.shp-ext.open'); const bs=c.querySelector('.ch-brush select');
   const k=[...bs.options].findIndex(q=>/free/i.test(q.textContent)); bs.value=String(k); bs.dispatchEvent(new Event('change',{bubbles:true})); }); await sleep(250);
 const f=await p.evaluate(()=>{ const q=document.querySelector('.ti-card.shp-ext.open .screen .field').getBoundingClientRect(); return {x:q.left,y:q.top,w:q.width,h:q.height}; });
 const N=90, pth=[]; for(let i=0;i<=N;i++){ const t=i/N; pth.push([f.x+6+t*(f.w-12), f.y+f.h*0.5-Math.sin(t*Math.PI*2)*f.h*0.28]); }
 await p.mouse.move(pth[0][0],pth[0][1]); await p.mouse.down(); for(const q of pth) await p.mouse.move(q[0],q[1]); await p.mouse.up(); await sleep(400);
 await shot('4free');
 // 5 — the whole page, so the card's colour can be read against the synth panel behind it
 await p.screenshot({path:OUT+'_5page.png',clip:{x:700,y:60,width:500,height:880}});
 console.log('shots at '+OUT+'_*.png');
 await b.close();
})();
