// tp19 — DOES THE WIDEN PICTURE SURVIVE A ROLL? On the synth page and on the Patcher: roll chain + modulation,
// then feed a synthetic fx4 bank and see whether fx4Tick still paints the widen card, throw-free.
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
  const p=await b.newPage(); await p.setViewport({width:1200,height:820}); const errs=[]; p.on('pageerror',e=>errs.push(e.message));
  await p.evaluateOnNewDocument(()=>{ try{ localStorage.clear(); }catch(e){} });
  await p.goto('file://'+process.argv[2]+'?page=1',{waitUntil:'load'}); await sleep(1500);
  const probe=async(where)=>p.evaluate(async(where)=>{
    const sleep=ms=>new Promise(r=>setTimeout(r,ms));
    // make sure a Widen exists, then roll both dice
    if(!(window.__fxrDevs()||[]).some(d=>d.core==='wid')) window.__fxrAdd('wid');
    await sleep(200);
    const before=(window.__fx4Errs|0);
    window.__fxrDice('wild'); await sleep(900);
    if(!(window.__fxrDevs()||[]).some(d=>d.core==='wid')) window.__fxrAdd('wid');
    await sleep(300);
    window.__modDice('crazy'); await sleep(700);
    const D=window.__fxrDevs()||[]; const wid=D.find(d=>d.core==='wid'); if(!wid) return {where, noWiden:true};
    const card=document.querySelector('.fxr-dev[data-dev="'+D.indexOf(wid)+'"] .fxr-core[data-core="wid"]');
    if(!card) return {where, noCard:true};
    const snap=()=>card.innerHTML.length+':'+[...card.querySelectorAll('[cx],[x1],[d],[width]')].map(e=>e.getAttribute('cx')||e.getAttribute('x1')||e.getAttribute('width')||(e.getAttribute('d')||'').slice(0,12)).join('|').slice(0,200);
    // two different synthetic frames for this instance: the picture must move between them
    const mk=v=>{ const bank=[]; bank[(wid.inst||1)-1]={lvl:v,nV:6,pan:[v,-v,v*0.5,-v*0.5,0.2,-0.2],cents:[v*20,-v*20,5,-5,0,0],corr:1-2*v,width:v}; return {wid:bank}; };
    window.__fx4VizPush=mk(0.05); for(let i=0;i<12;i++) window.__fx4Tick(); const s1=snap();
    window.__fx4VizPush=mk(0.95); for(let i=0;i<12;i++) window.__fx4Tick(); const s2=snap();
    return {where, inst:wid.inst, painted:s1!==s2, errsDuring:(window.__fx4Errs|0)-before, routes:(window.__tiRoutes()||[]).length, devs:D.map(d=>d.core).join('>')};
  },where);
  const synth=await probe('synth');
  await p.evaluate(()=>{ window.__tpOpen&&window.__tpOpen(); }); await sleep(1400);
  const patcher=await probe('patcher');
  console.log(JSON.stringify({synth,patcher,errs},null,1)); await b.close();
})().catch(e=>{ console.log('FAIL',String(e).slice(0,300)); process.exit(1); });
