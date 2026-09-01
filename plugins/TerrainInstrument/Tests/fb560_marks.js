// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb560_marks.js — THE MODULATION MARKS PAINT UNDER EVERY MENU.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/fb560_marks.js [page.html]
//
//  Max: "the streaks and the lines under the parameter — that purple streak — it overlays on top
//  of the menu... it needs to be UNDER."
//
//  WHY THIS CANNOT BE A Z-INDEX TEST: #syn-panel is position:absolute z-index:30, i.e. a stacking
//  context, so every menu inside it is sealed at z=30 against <body> no matter what its own
//  z-index says (the house menu computes 2147483646 and still loses to a mark at 2147483644 that
//  lives on <body>). The only truthful gate is elementFromPoint at a pixel they share.
//
//  PROOF THE BARS CAN FAIL (fb421):
//    · delete the coveredBy() call in the sm-ul loop        -> bars 1 and 2 red
//    · drop '.tpb-panel' from UL_OVER                        -> bar 2 reds
//    · drop the <body>-child inline-z net                    -> bar 3 reds
const puppeteer=require('puppeteer-core');
const P=process.argv[2]||require('path').join(__dirname,'..')+'/Source/ui/public/index.html';
let FAIL=0; const bad=m=>{FAIL++;return '  ✗ '+m;};
(async()=>{
  const b=await puppeteer.launch({executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:820,height:656,deviceScaleFactor:2});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,150)));
  await pg.goto('file://'+P,{waitUntil:'load'}); await new Promise(r=>setTimeout(r,2400));

  const r=await pg.evaluate(async ()=>{
    document.documentElement.classList.remove('card-only-late');
    document.querySelectorAll('.ti-preboot').forEach(e=>e.classList.remove('ti-preboot'));
    const sp=document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display='block';
    document.getElementById('syn-btn').click();
    const out={};
    // ⚠️ A HAND-MADE .sm-ul PROVES NOTHING: the stand-down lives in the loop that OWNS the marks,
    // so a div nobody manages is never hidden and the bar would read red for the wrong reason.
    // Make REAL routes and let the shipped loop build the real marks. (It also needs real TIME:
    // the loop measures each knob's word with a Range, so a mark is display:none for the first
    // frames — polling for a non-zero rect is the difference between testing this and testing
    // nothing.)
    const topAt=(x,y)=>{ const el=document.elementFromPoint(x,y);
      return el? ((el.closest&&el.closest('.sm-ul'))? 'MARK' : 'overlay') : 'nothing'; };
    const wait=ms=>new Promise(r=>setTimeout(r,ms));
    out.zPanel = getComputedStyle(document.getElementById('syn-panel')).zIndex;

    /* ⚠️ fb548's law again: __tiOff() asks an IntersectionObserver, and an offscreen/headless
       page is never composited, so the visibility gate answers "off" and the mark loop returns
       before it draws anything. Pin it open for the test — this is the harness lying, not the
       page. */
    window.__tiOff = function(){ return false; };
    if(!window.__tiAddRoute) return Object.assign(out,{err:'no __tiAddRoute'});
    window.__tiAddRoute(0,1,3); window.__tiAddRoute(0,1,64);
    let u=null, ur=null;
    for(let tries=0; tries<40 && !u; tries++){ await wait(30); if(window.__ulTick) window.__ulTick();
      for(const c of document.querySelectorAll('.sm-ul')){
        const r2=c.getBoundingClientRect();
        if(r2.width>4&&r2.height>2&&getComputedStyle(c).display!=='none'){ u=c; ur=r2; break; } } }
    out.marks = document.querySelectorAll('.sm-ul').length;
    if(!u) return Object.assign(out,{err:'the loop built no VISIBLE mark in 2 s'});
    out.markRect=[ur.left|0,ur.top|0,ur.width|0,ur.height|0];
    out.markVsNothing = topAt(ur.left+3, ur.top+3);   // the control: with nothing open, the mark IS on top

    // ── 1 · the house context menu, opened ON TOP of that mark ──
    window.__synShowMenu('',[{label:'a'},{label:'b'},{label:'c'}], ur.left-6, ur.top-6);
    await wait(60); if(window.__ulTick) window.__ulTick(); await wait(30);
    out.overlaysSeen=(window.__ulOverlays?window.__ulOverlays():[]).map(x=>x.cls).join(',');
    const m=document.querySelector('.syn-ctx-menu.act');
    out.menuZ = m? getComputedStyle(m).zIndex : null;
    out.markVsMenu = topAt(ur.left+3, ur.top+3);
    window.__synHideMenu(); await wait(60); if(window.__ulTick) window.__ulTick(); await wait(30);
    out.markBack = topAt(ur.left+3, ur.top+3);

    // ── 2 · the two-pane browser over the same mark ──
    window.openTwoPaneBrowser({clientX:ur.left-10,clientY:ur.top-10},
      {cats:[{label:'X',items:[{name:'a',pick(){}}]}],searchPlaceholder:'s'});
    await wait(60); if(window.__ulTick) window.__ulTick(); await wait(30);
    out.tpbHasClass=!!document.querySelector('.tpb-panel');
    out.markVsTpb = topAt(ur.left+3, ur.top+3);
    if(window.__tpbClose) window.__tpbClose(); await wait(60); if(window.__ulTick) window.__ulTick(); await wait(30);

    // ── 3 · an overlay with NO class in the list, only an inline z-index ──
    const ghost=document.createElement('div');
    ghost.style.cssText='position:fixed;left:'+(ur.left-10)+'px;top:'+(ur.top-10)+'px;width:200px;height:120px;z-index:2147483646;background:#222';
    document.body.appendChild(ghost);
    await wait(60); if(window.__ulTick) window.__ulTick(); await wait(30);
    out.ghostSeen=(window.__ulOverlays?window.__ulOverlays():[]).some(x=>x.r[0]===((ur.left-10)|0));
    out.markVsGhost = topAt(ur.left+3, ur.top+3);
    ghost.remove(); await wait(60); if(window.__ulTick) window.__ulTick(); await wait(30);

    // ── 4 · the hover route list: its grip must BE the shared emblem, at the ✕'s size ──
    u.dispatchEvent(new MouseEvent('mouseenter',{bubbles:true}));
    await wait(200);
    const rc=document.querySelector('.sm-routes .rc'), rx=document.querySelector('.sm-routes .rx');
    out.rowOpened=!!rc;
    if(rc&&rx){ out.gripIsSvg=!!rc.querySelector('svg');
      out.gripPath=/M15 3h6v6/.test(rc.innerHTML);
      const a1=rc.getBoundingClientRect(), b1=rx.getBoundingClientRect();
      out.gripVsCross=[+a1.height.toFixed(1), +b1.height.toFixed(1)];
      out.sameSize=Math.abs(a1.height-b1.height)<3.0;
      out.gripOpens=(typeof window.__tiCurveEdit==='function'); }
    out.markBack2 = topAt(ur.left+3, ur.top+3);
    return out;
  });

  console.log('fb560 — THE MARKS PAINT UNDER EVERY MENU');
  if(r.err){ console.log(bad(r.err)); }
  console.log('  #syn-panel is a stacking context at z='+r.zPanel+' (this is why z-index cannot fix it)');
  console.log('  a real route built '+r.marks+' mark(s); testing at '+JSON.stringify(r.markRect));
  console.log(r.markVsMenu==='overlay' ? '  ✓ the house context menu paints OVER the mark (menu z='+r.menuZ+', sealed at panel z=30)'
              : bad('the mark still paints over the context menu (top='+r.markVsMenu+')'));
  console.log(r.markBack==='MARK' ? '  ✓ and the mark COMES BACK when the menu closes' : bad('the mark never came back (top='+r.markBack+')'));
  console.log(r.tpbHasClass ? '  ✓ the two-pane browser is nameable (.tpb-panel)' : bad('the browser has no .tpb-panel class'));
  console.log(r.markVsTpb==='overlay' ? '  ✓ the two-pane browser paints OVER the mark'
              : bad('the mark paints over the browser (top='+r.markVsTpb+')'));
  console.log('  overlays the register saw with the menu open: '+(r.overlaysSeen||'(none)'));
  console.log(r.ghostSeen ? '  ✓ an unlisted <body> overlay with only an inline z-index is still registered'
              : bad('the inline-z net missed a body-child overlay'));
  console.log(r.markVsNothing==='MARK' ? '  ✓ the control holds: with nothing open the mark IS the top element'
              : bad('the control failed — the mark is not on top even with nothing open ('+r.markVsNothing+'), so this gate proves nothing'));
  console.log(r.markVsGhost==='overlay' ? '  ✓ ...and it paints over the mark'
              : bad('the mark paints over an unlisted overlay (top='+r.markVsGhost+')'));
  console.log(r.markBack2==='MARK' ? '  ✓ and comes back when that closes too' : bad('the mark stayed down (top='+r.markBack2+')'));
  console.log(r.rowOpened ? '  ✓ hovering the mark opens the route list' : bad('the route list did not open'));
  if(r.rowOpened){
    console.log(r.gripIsSvg&&r.gripPath ? '  ✓ its grip IS the shared EXTEND emblem (not the old ∿ text glyph)'
                : bad('the grip is not the shared emblem'));
    console.log(r.sameSize ? '  ✓ the grip and the ✕ are the same height: '+JSON.stringify(r.gripVsCross)
                : bad('grip/✕ heights differ: '+JSON.stringify(r.gripVsCross)));
  }
  console.log('page errors: '+(errs.length?errs.join(' | '):'none')); if(errs.length)FAIL++;
  console.log(FAIL? '\n  '+FAIL+' FAILED' : '\n  ALL BARS GREEN');
  await b.close(); process.exit(FAIL?1:0);
})();
