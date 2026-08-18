// fb402 — motion gate. A stair-step has a signature: consecutive frames IDENTICAL (the engine
// value is frozen between messages while rAF keeps sampling) then a jump. Water has neither.
const puppeteer=require('puppeteer-core');
const P='/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Design/fx3/mockup/flanger-mockup.html';
(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files','--autoplay-policy=no-user-gesture-required','--mute-audio']});
  const pg=await b.newPage(); await pg.setViewport({width:1200,height:760,deviceScaleFactor:2});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,150)));
  await pg.goto('file://'+P,{waitUntil:'load'}); await new Promise(r=>setTimeout(r,400));
  await pg.click('#play'); await new Promise(r=>setTimeout(r,2500));
  const r=await pg.evaluate(async()=>{
    const out={ flashEl: !!document.getElementById('flash') };
    const path=document.getElementById('up');
    const ys=[];
    for(let i=0;i<150;i++){
      await new Promise(rq=>requestAnimationFrame(rq));
      const d=path.getAttribute('d')||'';
      const pts=d.split('L'); const m=(pts[Math.floor(pts.length*0.22)]||'').trim().split(' ');
      ys.push(m&&m.length>1?parseFloat(m[1]):NaN);
    }
    const good=ys.filter(v=>isFinite(v));
    let same=0, jumps=[];
    for(let i=1;i<good.length;i++){
      const dv=Math.abs(good[i]-good[i-1]);
      if(dv<1e-9) same++;
      jumps.push(dv);
    }
    jumps.sort((a,b)=>b-a);
    out.frames=good.length;
    out.identicalConsecutive=same;
    out.maxJumpPx=+jumps[0].toFixed(3);
    out.p95JumpPx=+jumps[Math.floor(jumps.length*0.05)].toFixed(3);
    out.medianJumpPx=+jumps[Math.floor(jumps.length*0.5)].toFixed(3);
    return out;
  });
  console.log(JSON.stringify(r,null,1));
  let F=0;
  if(r.flashEl){F++;console.log('  ✗ the white flash element still exists');}
  if(r.identicalConsecutive > r.frames*0.15){F++;console.log('  ✗ STAIR-STEPPING: '+r.identicalConsecutive+'/'+r.frames+' frames identical to the previous');}
  if(r.maxJumpPx > 8){F++;console.log('  ✗ discontinuous: max frame jump '+r.maxJumpPx+'px');}
  console.log('page errors:',errs.length?errs.slice(0,2):'none');
  console.log(F?('══ '+F+' FAILED'):'══ motion is continuous, no flash: PASS');
  await b.close();
})();
