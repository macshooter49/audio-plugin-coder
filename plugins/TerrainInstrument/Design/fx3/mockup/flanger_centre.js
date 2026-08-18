const puppeteer=require('puppeteer-core');
const P='/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Design/fx3/mockup/flanger-mockup.html';
(async()=>{const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
 headless:'new',args:['--no-sandbox','--allow-file-access-from-files','--autoplay-policy=no-user-gesture-required','--mute-audio']});
 const pg=await b.newPage(); await pg.setViewport({width:1200,height:760,deviceScaleFactor:2});
 await pg.goto('file://'+P,{waitUntil:'load'}); await new Promise(r=>setTimeout(r,400));
 await pg.click('#play'); await new Promise(r=>setTimeout(r,2200));
 const o=await pg.evaluate(()=>{
   const up=document.getElementById('up').getAttribute('d').split('L');
   const dn=document.getElementById('dn').getAttribute('d').split('L');
   const k=Math.floor(up.length*0.3);
   const yu=parseFloat(up[k].trim().split(' ')[1]), yd=parseFloat(dn[k].trim().split(' ')[1]);
   const svg=document.getElementById('wf').getBoundingClientRect();
   const card=document.querySelector('.fxr-dev').getBoundingClientRect();
   const ribbonAbs=svg.top+(yu+yd)/2;
   return {ribbonCentre:+ribbonAbs.toFixed(1), cardCentre:+(card.top+card.height/2).toFixed(1),
           coreCentre:+(svg.top+svg.height/2).toFixed(1)};
 });
 o.offsetFromCard=+(o.ribbonCentre-o.cardCentre).toFixed(1);
 console.log(JSON.stringify(o,null,1));
 console.log(Math.abs(o.offsetFromCard)<2 ? '══ ribbon sits on the card centre: PASS' : '══ off by '+o.offsetFromCard+'px');
 await b.close();})();
