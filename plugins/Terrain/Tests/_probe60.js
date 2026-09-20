const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:648,height:560,deviceScaleFactor:2});
 await p.evaluateOnNewDocument(()=>{ try{ localStorage.setItem('tpLayout',JSON.stringify({v:6,pos:{},ext:{}})); }catch(e){} });
 await p.goto('file://'+process.argv[2]+'?page=1',{waitUntil:'load'}); await sleep(1900);
 await p.evaluate(()=>{ document.documentElement.setAttribute('data-theme','dark');
   const d=document.getElementById('osc-a-device'); if(d) d.classList.remove('osc-off'); });
 await sleep(400); await p.evaluate(()=>window.setActivePanel('tp')); await sleep(2400);
 await p.evaluate(()=>{
   const T=window.__tpeDraw, names=['Studio','Cassette','Reel','Porta','Wire'];
   const V={l:0.62,i:0.55,sp:0.37,pk:0.41,w:0.45,h:[0.7,0.4,0.55,0.25]};
   document.body.innerHTML=''; document.body.style.cssText='margin:0;background:#14121f;display:flex;flex-wrap:wrap;gap:8px;padding:8px;width:648px;box-sizing:border-box';
   names.forEach(n=>{ const wrap=document.createElement('div');
     wrap.style.cssText='width:300px;height:170px;background:#1a1730;border:1px solid #33304d;border-radius:8px;position:relative';
     const cv=document.createElement('canvas'); cv.width=600; cv.height=300;
     cv.style.cssText='width:300px;height:150px;display:block';
     const c=cv.getContext('2d'); c.scale(2,2); T[n](c,300,150,V);
     const lab=document.createElement('div');
     lab.textContent=n; lab.style.cssText='color:#b794ff;font:600 10px system-ui;letter-spacing:.15em;text-align:center';
     wrap.appendChild(cv); wrap.appendChild(lab); document.body.appendChild(wrap); });
 });
 await sleep(300);
 await p.screenshot({path:'/tmp/_tape5.png'});
 await b.close();})();
