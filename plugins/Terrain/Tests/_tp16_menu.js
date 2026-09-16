// tp16 — THE DICE MENU IS THE ONLY HOME: the two rack rolls live in the Patcher's dice menu,
// the row above the rack is gone, and ONE reach (Light…CRAZY) governs everything.
const puppeteer=require('puppeteer-core');
const PAGE=process.argv[2];
const CHROME='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
(async()=>{
  const b=await puppeteer.launch({executablePath:CHROME,headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const p=await b.newPage(); await p.setViewport({width:1200,height:820});
  const errs=[]; p.on('pageerror',e=>errs.push(String(e).slice(0,160)));
  await p.goto('file://'+PAGE,{waitUntil:'networkidle0'});
  await p.evaluate(()=>{ try{ localStorage.clear(); }catch(e){} });
  await new Promise(r=>setTimeout(r,1400));
  const out=await p.evaluate(async()=>{
    const R={};
    const sleep=ms=>new Promise(r=>setTimeout(r,ms));
    R.rowGone = !document.getElementById('fxr-dice');
    R.hooks = { fx: typeof window.__fxrDice, mod: typeof window.__modDice, devs: typeof window.__fxrDevs,
                add: typeof window.__fxrAdd, addSrc: typeof window.__tiAddSrc, render: typeof window.__fxrRender };
    // open the Patcher and right-click its dice tool
    if(window.__tpOpen) window.__tpOpen(); await sleep(1200);
    const tool=document.querySelector('#tp-page .tp-tools .g[data-t="dice"]');
    R.toolFound=!!tool;
    if(tool){ const r=tool.getBoundingClientRect();
      tool.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,clientX:r.left+5,clientY:r.top+5})); }
    await sleep(300);
    const menu=document.querySelector('#tp-page .tp-nmenu');
    R.menuOpen = !!(menu&&menu.classList.contains('on'));
    R.aims=[...(menu?menu.querySelectorAll('[data-aim]'):[])].map(e=>e.textContent);
    R.reaches=[...(menu?menu.querySelectorAll('[data-dl]'):[])].map(e=>e.textContent);
    R.acts=[...(menu?menu.querySelectorAll('[data-act]'):[])].map(e=>e.dataset.act+':'+e.textContent);
    return R;
  });
  console.log(JSON.stringify(out,null,1)); console.log('errs',JSON.stringify(errs));
  await b.close();
})().catch(e=>{ console.log('FAIL',String(e).slice(0,300)); process.exit(1); });
