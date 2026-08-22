// fb447 — headless state shots of the fx5 mockup: read them BEFORE Max does (the eyeball law).
//   NODE_PATH=<scratchpad>/node_modules node Design/fx5/mockup/shots.js Design/fx5/mockup/fx5-ui-mockup.html <outdir>
const puppeteer=require('puppeteer-core'); const fs=require('fs'); const path=require('path');
const CH='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const PAGE=process.argv[2], OUT=process.argv[3];
const wait=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
  const br=await puppeteer.launch({executablePath:CH,headless:'new',args:['--autoplay-policy=no-user-gesture-required','--no-sandbox','--disable-gpu','--allow-file-access-from-files']});
  const pg=await br.newPage(); await pg.setViewport({width:1500,height:560,deviceScaleFactor:2});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,200)));
  await pg.goto('file://'+PAGE,{waitUntil:'load'}); await wait(600);
  await pg.evaluate(()=>{ const z=[...document.querySelectorAll('.zoom')].find(b=>b.getAttribute('data-z')==='1.2'); if(z) z.click(); });
  const shot=async(tag)=>{ await wait(250); const rw=await pg.$('#rackwrap'); await rw.screenshot({path:path.join(OUT,tag+'.png')}); console.log('  shot',tag); };
  // s0: idle — no audio at all: the Bode ladder streams, the Utility wiring, the Splitter bands
  await pg.evaluate(()=>{ const d=DEVS[0]; d.knobs[0].v=72; window.__setSynParam(d.knobs[0].p,.72); window.__fxRedrawKnobs(d); });
  await wait(400); await shot('s0-idle-shift-up');
  await pg.evaluate(()=>{ const d=DEVS[0]; d.knobs[0].v=28; d.knobs[2].v=55; window.__setSynParam(d.knobs[0].p,.28); window.__setSynParam(d.knobs[2].p,.55); window.__fxRedrawKnobs(d); });
  await wait(300); await shot('s1-idle-shift-down-fdbk');
  // audio on
  await pg.click('#go'); await wait(1200); await pg.click('#play'); await wait(2000);
  await pg.evaluate(()=>{ const d=DEVS[0]; d.knobs[0].v=72; d.knobs[2].v=0; window.__setSynParam(d.knobs[0].p,.72); window.__setSynParam(d.knobs[2].p,0); window.__fxRedrawKnobs(d); });
  await wait(900); await shot('s2-play-bode-up');
  await pg.evaluate(()=>{ const d=DEVS[0]; d.knobs[1].v=50; window.__setSynParam(d.knobs[1].p,.5); window.__fxRedrawKnobs(d); });
  await wait(700); await shot('s3-play-bode-ring');
  await pg.evaluate(()=>{ const d=DEVS[0]; d.knobs[1].v=100; d.knobs[0].v=50; window.__setSynParam(d.knobs[1].p,1); window.__setSynParam(d.knobs[0].p,.5); window.__fxRedrawKnobs(d);
    [...document.querySelectorAll('.hear')].find(x=>x.getAttribute('data-h')==='1').click(); });
  await wait(900); await shot('s4-play-utility-default');
  await pg.evaluate(()=>{ const card=document.querySelectorAll('.fxr-dev')[1]; const p=card.querySelectorAll('.fxr-pill'); p[2].click(); p[0].click(); const d=DEVS[1]; d.knobs[1].v=92; d.knobs[2].v=22; window.__setSynParam(d.knobs[1].p,.92); window.__setSynParam(d.knobs[2].p,.22); window.__fxRedrawKnobs(d); });
  await wait(900); await shot('s5-play-utility-swap-flipL-wide-panL');
  await pg.evaluate(()=>{ const card=document.querySelectorAll('.fxr-dev')[1]; const p=card.querySelectorAll('.fxr-pill'); p[2].click(); p[0].click(); p[3].click(); });
  await wait(900); await shot('s6-play-utility-mono');
  await pg.evaluate(()=>{ const card=document.querySelectorAll('.fxr-dev')[1]; const p=card.querySelectorAll('.fxr-pill'); p[3].click(); const d=DEVS[1]; d.back.knobs[6][1]=80; window.__setSynParam(d.back.knobs[6][2],.8); d.back.knobs[7][1]=70; window.__setSynParam(d.back.knobs[7][2],.7); window.__fxRedrawKnobs(d);
    [...document.querySelectorAll('.hear')].find(x=>x.getAttribute('data-h')==='2').click(); });
  await wait(900); await shot('s7-play-utility-haas-drive-splitter-hear');
  await pg.evaluate(()=>{ const d=DEVS[2]; d.pills[0].on=true; d.pills[8].on=true; push(d,'mute1',1,true); push(d,'flip3',1,true); });
  await wait(900); await shot('s7b-splitter-mute-low-flip-high');
  await pg.evaluate(()=>{ const d=DEVS[2]; d.pills[0].on=false; d.pills[8].on=false; push(d,'mute1',0,true); push(d,'flip3',0,true); });
  await pg.evaluate(()=>{ const s=document.querySelector('select[data-tsel="2"]'); s.selectedIndex=0; s.dispatchEvent(new Event('change',{bubbles:true})); });
  await wait(900); await shot('s8-splitter-lowhigh');
  await pg.evaluate(()=>{ const s=document.querySelector('select[data-tsel="2"]'); s.selectedIndex=2; s.dispatchEvent(new Event('change',{bubbles:true})); });
  await wait(900); await shot('s9-splitter-4lane');
  // back panels, all three
  await pg.evaluate(()=>{ document.querySelectorAll('.fxr-swap').forEach(x=>x.click()); });
  await wait(500); await pg.setViewport({width:1500,height:900,deviceScaleFactor:2}); await wait(300); await shot('s10-backs-4lane');
  await pg.evaluate(()=>{ const s=document.querySelector('select[data-tsel="2"]'); s.selectedIndex=0; s.dispatchEvent(new Event('change',{bubbles:true})); document.querySelectorAll('.fxr-swap').forEach(x=>x.click()); });
  await wait(600); await shot('s11-backs-lowhigh');
  await pg.evaluate(()=>{ const s=document.querySelector('select[data-tsel="2"]'); s.selectedIndex=3; s.dispatchEvent(new Event('change',{bubbles:true})); document.querySelectorAll('.fxr-swap').forEach(x=>x.click()); });
  await wait(600); await shot('s12-backs-midside');
  console.log('  page errors:', errs.length?errs.join(' | '):'none');
  await br.close();
})().catch(e=>{ console.error('FAILED',e.stack||e.message); process.exit(1); });
