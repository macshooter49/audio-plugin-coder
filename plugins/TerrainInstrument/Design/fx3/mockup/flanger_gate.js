const puppeteer=require('puppeteer-core');
const P='/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Design/fx3/mockup/flanger-mockup.html';
const OUT=process.argv[2]; const fs=require('fs'); try{fs.mkdirSync(OUT,{recursive:true});}catch(e){}
const sleep=ms=>new Promise(r=>setTimeout(r,ms)); let FAIL=0;
(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files','--autoplay-policy=no-user-gesture-required','--mute-audio']});
  const pg=await b.newPage(); await pg.setViewport({width:1200,height:760,deviceScaleFactor:3});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,160)));
  await pg.goto('file://'+P,{waitUntil:'load'}); await sleep(500);
  await pg.click('#play'); await sleep(2200);
  const r=await pg.evaluate(()=>{
    const out={};
    // CENTERLINE LAW — every header element's vertical MIDDLE on one line
    const head=document.querySelector('.fxr-head');
    const kids=['.fxr-grip','.fxr-name','.fxr-type','.fxr-preset','.fxr-swap','.fxr-pwr','.fxr-x'];
    const mids={};
    kids.forEach(s=>{const e=head.querySelector(s); if(e){const b=e.getBoundingClientRect(); mids[s.slice(5)]=+(b.top+b.height/2).toFixed(2);}});
    out.centers=mids;
    const vals=Object.values(mids); out.centerlineSpread=+(Math.max(...vals)-Math.min(...vals)).toFixed(2);
    out.hasPlus=!!head.querySelector('.fxr-swap');
    // + and x must be identical boxes (fb275)
    const sw=head.querySelector('.fxr-swap'), xb=head.querySelector('.fxr-x');
    if(sw&&xb){const a=sw.getBoundingClientRect(),c=xb.getBoundingClientRect();
      out.plusBox=[+a.width.toFixed(1),+a.height.toFixed(1)]; out.xBox=[+c.width.toFixed(1),+c.height.toFixed(1)];}
    // NOTHING BLACK — resolved colours that matter
    const cs=getComputedStyle(document.querySelector('#syn-panel'));
    out.panelColor=cs.color;
    out.bodyColor=getComputedStyle(document.body).color;
    out.dialColor=getComputedStyle(document.querySelector('.fxr-dial')).color;
    out.labColor=getComputedStyle(document.querySelector('.fxr-lab')).color;
    return out;
  });
  console.log(JSON.stringify(r,null,1));
  const isBlack=c=>/rgba?\(0, ?0, ?0/.test(c||'');
  ['panelColor','bodyColor','dialColor','labColor'].forEach(k=>{ if(isBlack(r[k])){FAIL++;console.log('  ✗ BLACK: '+k+' = '+r[k]);} });
  if(r.centerlineSpread>0.6){FAIL++;console.log('  ✗ centerline spread '+r.centerlineSpread+'px (must be <=0.6)');}
  if(!r.hasPlus){FAIL++;console.log('  ✗ + button missing');}
  if(r.plusBox&&r.xBox&&(r.plusBox[0]!==r.xBox[0]||r.plusBox[1]!==r.xBox[1])){FAIL++;console.log('  ✗ + and x are not identical boxes');}
  await (await pg.$('.rackwrap')).screenshot({path:OUT+'/chorus.png'});
  console.log('page errors:',errs.length?errs.slice(0,2):'none');
  console.log(FAIL?('══ '+FAIL+' FAILED'):'══ centerline + no-black + plus: ALL PASS');
  await b.close();
})();
