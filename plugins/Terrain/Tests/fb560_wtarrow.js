// fb560 — THE ARROW STEPS THE FOLDER YOU ARE IN (factory side; imports need a registry, which a
// headless page has none of — that half is exercised in the plugin).
const puppeteer=require('puppeteer-core');
const P=process.argv[2]||require('path').join(__dirname,'..')+'/Source/ui/public/index.html';
let FAIL=0; const bad=m=>{FAIL++;return '  ✗ '+m;};
(async()=>{
  const b=await puppeteer.launch({executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:820,height:656,deviceScaleFactor:2});
  // the one-press-one-step bar throws ON PURPOSE inside a callback; that is the stimulus, not a defect
  const errs=[]; pg.on('pageerror',e=>{ const t=String(e); if(!/deliberate/.test(t)) errs.push(t.slice(0,150)); });
  await pg.goto('file://'+P,{waitUntil:'load'}); await new Promise(r=>setTimeout(r,2400));
  const r=await pg.evaluate(async()=>{
    const sp=document.getElementById('syn-panel'); sp.classList.remove('hidden');
    document.getElementById('syn-btn').click();
    const wait=ms=>new Promise(z=>setTimeout(z,ms));
    const sel=document.getElementById('osc-a-preset-select');
    if(!sel) return {err:'no preset select'};
    const groups=[...sel.getElementsByTagName('optgroup')].map(g=>({label:g.label,
      vals:[...g.getElementsByTagName('option')].map(o=>o.value)}));
    const out={groups:groups.map(g=>g.label+'('+g.vals.length+')')};
    if(!groups.length) return Object.assign(out,{err:'no optgroups'});

    // pick a folder with >=3 entries and start at its LAST item, so a flat stepper would leave it
    const g=groups.find(x=>x.vals.length>=3)||groups[0];
    out.folder=g.label; out.n=g.vals.length;
    const last=g.vals[g.vals.length-1];
    sel.value=last; window.__wtFolder['a']=null;
    await window.__wtAllCats && null;
    window.wtStepPreset('a',1); await wait(300);
    out.wrapped = sel.value;                      // must WRAP to the folder's head, not leak forward
    out.wrapOk  = (sel.value===g.vals[0]);
    // walk the whole folder and confirm we never leave it
    sel.value=g.vals[0]; let left=null;
    for(let i=0;i<g.vals.length+2;i++){ window.wtStepPreset('a',1); await wait(90);
      if(g.vals.indexOf(sel.value)<0){ left=sel.value; break; } }
    out.neverLeft = (left===null); out.leaked=left;

    // ── fb562 · ONE PRESS, ONE STEP. `.then(f).catch(g)` is not try/catch: if f throws, g runs
    //    too, so a callback that failed stepped the arrow TWICE. Prove the shipped collector
    //    calls back exactly once even when the callback itself throws.
    out.cbCalls = 0;
    try { window.__wtAllCats('a', function(){ out.cbCalls++; throw new Error('deliberate'); }); }
    catch(e){ /* thrown asynchronously; the count is what matters */ }
    await wait(700);
    out.onceOnly = (out.cbCalls === 1);

    // ── ALL: if that is where you browsed, that is what you step ──
    const flat=[...sel.options].map(o=>o.value);
    window.__wtFolder['a']='All';
    sel.value=g.vals[g.vals.length-1];
    window.wtStepPreset('a',1); await wait(300);
    const want=flat[(flat.indexOf(g.vals[g.vals.length-1])+1)%flat.length];
    out.allStep = sel.value; out.allWant = want; out.allOk = (sel.value===want);
    out.allCrossesFolder = (g.vals.indexOf(want)<0);   // the test is only meaningful if it leaves the folder
    return out;
  });
  console.log('fb560 — THE WAVETABLE ARROW');
  if(r.err){ console.log(bad(r.err)); }
  else{
    console.log('  folders: '+r.groups.join(' '));
    console.log('  stepping inside "'+r.folder+'" ('+r.n+' tables)');
    console.log(r.wrapOk ? '  ✓ past the last table it WRAPS to the folder\'s first, it does not leak forward'
                         : bad('it left the folder at the end: landed on '+r.wrapped));
    console.log(r.neverLeft ? '  ✓ a full walk of the folder never leaves it'
                            : bad('it escaped to '+r.leaked+' mid-folder'));
    console.log(r.allCrossesFolder ? '  ✓ the All test is meaningful (the next All item is outside that folder)'
                                   : bad('the All test is vacuous here'));
    console.log(r.allOk ? '  ✓ browsing All steps All — across folder boundaries, in the browser\'s order'
                        : bad('All did not step All: got '+r.allStep+', expected '+r.allWant));
    console.log(r.onceOnly ? '  ✓ one press, one step — the collector calls back exactly once even when the callback throws'
                           : bad('the collector called back '+r.cbCalls+' times: one press would step twice'));
  }
  console.log('page errors: '+(errs.length?errs.join(' | '):'none')); if(errs.length)FAIL++;
  console.log(FAIL? '\n  '+FAIL+' FAILED' : '\n  ALL BARS GREEN');
  await b.close(); process.exit(FAIL?1:0);
})();
