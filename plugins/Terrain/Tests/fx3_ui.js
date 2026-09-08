// fb413 — the UI→param gate for CHORUS · FLANGER · PHASER (chain kinds 6, 7, 8).
//
// The law this exists for: a green DSP harness proves the ENGINE works and NEVER that the plugin
// REACHES it (fb373 — selecting Cassette silently gave you Studio through four rounds of green
// measurement). And a clean build proves nothing about whether the page RUNS: at fb381 a `var`
// declared after its use hoisted to undefined, threw inside DEV_TEMPLATES, and killed the whole
// rack module with a clean parse and a green build. Only this kind of gate saw it.
//
// So this drives the real page: adds eight of each device (they must CAP at six), reads the
// cards back, and pushes one frame of the C++ viz feed to prove every window actually redraws.
const puppeteer = require('puppeteer-core');
const P=require('path').join(__dirname,'..')+'/Source/ui/public/index.html';

let pass=0, fail=0;
function chk(ok,label,detail){ if(ok){pass++; console.log('  ok    '+label+(detail?'   '+detail:''));}
  else {fail++; console.log('  FAIL  '+label+(detail?'   '+detail:''));} }

(async()=>{
  const b=await puppeteer.launch({executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const pg=await b.newPage(); await pg.setViewport({width:1560,height:1200,deviceScaleFactor:2});
  const errs=[]; pg.on('pageerror',e=>errs.push(String(e).slice(0,150)));
  await pg.evaluateOnNewDocument(() => {
    // ⚠️ THE fb393 LAW: a stub must be as DEAD as the real backend. fb392's slider stub STORED
    // what was written to it, so 39/39 gates went green while the plugin sat frozen — the gate
    // was measuring the stub. These getters return constants and the setters do NOTHING.
    const mk=()=>({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
      getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
      valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
    window.Juce={getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
      getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
        if(/Json|JSON/.test(n))return r('{}'); r(0);}),
      backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
    (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
      get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
    window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
      __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
  });
  await pg.goto('file://'+P,{waitUntil:'load',timeout:60000});
  await new Promise(r=>setTimeout(r,1600));
  await pg.evaluate(()=>{ const sp=document.getElementById('syn-panel');
    if(sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
  await new Promise(r=>setTimeout(r,1500));

  console.log('\n══ fb413 — CHORUS · FLANGER · PHASER: the UI gate ══\n');
  chk(errs.length===0, 'the page runs with no errors at all',
      errs.length?errs.slice(0,2).join(' | '):'0 page errors');
  chk(await pg.evaluate(()=>typeof window.__fxAdd==='function'),
      'the rack module is ALIVE (__fxAdd was assigned)');

  const r = await pg.evaluate(() => {
    const out={devs:{}};
    // 8 of each: DUPLICATABLE, and capped at 6 (kFxInstances)
    for(const core of ['cho','fla','pha'])
      for(let i=0;i<8;i++){ try{ window.__fxAdd(core); }catch(e){ out.addErr=core+': '+String(e).slice(0,80); } }

    const cards=[...document.querySelectorAll('.fxr-dev')];
    out.cardsTotal=cards.length;
    const D=(window.__fxDevs?window.__fxDevs():[]);

    // one synthetic frame of the REAL feed shape, so every window has to redraw from it
    const one=(lfo,dep,n)=>({lfo:lfo,lvl:0.42,dep:dep,env:1,n:n});
    window.__fx3VizPush={
      cho:[one(0.6,9.4,[0,0,0,0,0,0,0,0]),null,null,null,null,null],
      fla:[one(-0.3,2.1,[0,0,0,0,0,0,0,0]),null,null,null,null,null],
      pha:[one(0.2,4.4,[220,640,1850,5200,0,0,0,0]),null,null,null,null,null]};

    for(const [core,name] of [['cho','Chorus'],['fla','Flanger'],['pha','Phaser']]){
      const mine=cards.filter(c=>new RegExp('^'+name).test(((c.querySelector('.fxr-name')||{}).textContent||'').trim()));
      const o={count:mine.length};
      if(mine.length){
        const c=mine[0], idx=[].indexOf.call(c.parentNode.children,c);
        o.core       = !!c.querySelector('.fxr-core[data-core="'+core+'"]');
        o.routePills = c.querySelectorAll('.fxr-r,.fxr-route .r').length;
        o.pills      = [...c.querySelectorAll('.fxr-pill .fxr-t')].map(e=>e.textContent.trim()).join('/');
        o.knobs      = [...c.querySelectorAll('.fxr-knob')].length;
        o.backKnobs  = (D[idx]&&D[idx].back&&D[idx].back.knobs)?D[idx].back.knobs.length:0;
        const sels=[...c.querySelectorAll('select')];
        o.selectCounts=sels.map(x=>x.options.length).join(',');
        // the shape of the paths this window draws
        const paths=[...c.querySelectorAll('.fxr-core path')];
        o.dstCurves = c.querySelectorAll('.fxr-core .dst-curve').length;
        // ⚠️ THE STROBE LAW: nothing wearing .dst-curve may carry an inline opacity, because
        // .dst-curve animates opacity via mvBreathe and a CSS animation beats an attribute.
        o.opacityOnCurve = [...c.querySelectorAll('.fxr-core .dst-curve')]
            .filter(e=>e.getAttribute('opacity')!=null||e.style.opacity).length;
        // ⚠️ redraw is measured OUTSIDE this loop. __fx3Tick draws every card in one pass, so
        // ticking here would already have redrawn the other two devices and their own
        // before/after would compare equal — which is exactly what the first run of this gate
        // reported (chorus 2 paths moved, flanger and phaser 0). The gate was wrong, not the
        // driver. Snapshot all three FIRST, tick ONCE, then compare.
        o.__paths=paths;
        // the SYNC LAW: the Rate readout must switch to a time signature when Sync is lit
        const dd=D[idx];
        if(dd){
          o.rateFree = window.__fxFmtProbe ? window.__fxFmtProbe(core,dd.knobs[0].p,50,dd) : null;
          dd.pills[0].on=true;
          o.rateSync = window.__fxFmtProbe ? window.__fxFmtProbe(core,dd.knobs[0].p,50,dd) : null;
          dd.pills[0].on=false;
        }
      }
      out.devs[core]=o;
    }
    // now: snapshot every window, ONE tick, compare
    const snap={};
    for(const core of ['cho','fla','pha']){
      const o=out.devs[core]; if(!o||!o.__paths) continue;
      snap[core]=o.__paths.map(p=>p.getAttribute('d')||'');
    }
    try{ window.__fx3Tick(); }catch(e){ out.tickErr=String(e).slice(0,90); }
    for(const core of ['cho','fla','pha']){
      const o=out.devs[core]; if(!o||!o.__paths) continue;
      const after=o.__paths.map(p=>p.getAttribute('d')||'');
      o.redrew=snap[core].filter((d,i)=>d!==after[i]).length;
      o.tickErr=out.tickErr; delete o.__paths;
    }
    // ═══ fb413 — THE fb373 GATE. Drive the REAL back-panel <select> and read what the page
    //  actually writes to the param. A Type roster of 8 live entries sits on a choice(16) param
    //  (RACK LAW C reserves the rest), so picking entry 1 must write 1/15 = 0.0667, NOT 1/7.
    //  Decoded the way JUCE decodes it, that has to come back as index 1. This is the exact
    //  shape of the bug that made Cassette play Studio for eight builds.
    out.typeWrite={};
    {
      const spy=[]; const real=window.__setSynParam;
      window.__setSynParam=function(id,v){ spy.push([id,v]); try{ return real.apply(this,arguments); }catch(e){} };
      for(const [core,name] of [['cho','Chorus'],['fla','Flanger'],['pha','Phaser']]){
        const card=[...document.querySelectorAll('.fxr-dev')]
          .find(c=>new RegExp('^'+name).test(((c.querySelector('.fxr-name')||{}).textContent||'').trim()));
        if(!card) continue;
        const sels=[...card.querySelectorAll('select.fxr-bk-native')];
        // 🔑 fb418 — NO DOUBLES. The back panel must NOT carry a second Type selector: the
        //  header pill owns Type, and a duplicate is both dead space and the same name twice on
        //  one card. The cardinality check moves to whatever d2 actually is now (Motion/Route),
        //  which is where the fb373 trap can still bite — those params are choice(8) showing 4.
        out.typeWrite[core+'_dupe'] = sels.filter(x=>/_TYPE$/.test(x.getAttribute('data-p')||'')).length;
        const typeSel=sels.find(x=>!/CHAR$/.test(x.getAttribute('data-p')||''));
        if(!typeSel){ out.typeWrite[core]='no d2 select'; continue; }
        spy.length=0;
        typeSel.selectedIndex=1;
        typeSel.dispatchEvent(new Event('change',{bubbles:true}));
        const w=spy.find(e=>e[0]===typeSel.getAttribute('data-p'));
        if(!w){ out.typeWrite[core]='nothing written'; continue; }
        const pn=+(typeSel.getAttribute('data-pn')||typeSel.options.length);
        const decoded=Math.round(w[1]*(pn-1));
        out.typeWrite[core]={norm:+w[1].toFixed(4), pn:pn, opts:typeSel.options.length, decoded:decoded,
                             id:(typeSel.getAttribute('data-p')||'').replace(/^SYN_[A-Z]+\d*_/,'')};
      }
      window.__setSynParam=real;
    }
    // ═══ fb414 — THE SEND PILL. It must exist on EVERY device (it is a rack-wide routing mode,
    //  not an fx3 feature), it must write its own param, and it must not reflow the six route
    //  letters beside it (the fixed-positions law: nothing moves when content changes).
    out.send={};
    {
      const spy=[]; const real=window.__setSynParam;
      window.__setSynParam=function(id,v){ spy.push([id,v]); try{ return real.apply(this,arguments); }catch(e){} };
      const cards=[...document.querySelectorAll('.fxr-dev')];
      // 🔑 fb415 THE FIRST-SLOT LAW: the Send glyph is on the LEFTMOST card and nowhere else.
      out.send.onFirst = cards.length>0 && !!cards[0].querySelector('.fxr-snd');
      out.send.onOthers = cards.slice(1).filter(c=>!!c.querySelector('.fxr-snd')).length;
      // ...and every other card gets its plain six-letter route row back at FULL width. The
      // crowding was the complaint, so measure it: a later card's letters must be WIDER than
      // the first card's, and all six must be equal to each other.
      const wOf=c=>[...c.querySelectorAll('.fxr-r')].map(e=>Math.round(e.getBoundingClientRect().width));
      const w0=wOf(cards[0]), w1=cards.length>1?wOf(cards[1]):[];
      out.send.firstW=w0[0]; out.send.otherW=w1[0];
      out.send.othersEven = w1.length===6 && w1.every(x=>Math.abs(x-w1[0])<=1);
      const c0=cards[0];
      if(c0){
        const letters=[...c0.querySelectorAll('.fxr-r')];
        const before=letters.map(e=>Math.round(e.getBoundingClientRect().left));
        const pill=c0.querySelector('.fxr-snd');
        out.send.litBefore = pill.classList.contains('fxr-on');
        spy.length=0;
        pill.dispatchEvent(new MouseEvent('click',{bubbles:true}));
        out.send.litAfter = pill.classList.contains('fxr-on');
        const w=spy.find(e=>/_SEND$/.test(e[0]));
        out.send.wrote = w ? (w[0]+'='+w[1]) : 'nothing';
        const after=letters.map(e=>Math.round(e.getBoundingClientRect().left));
        out.send.moved = before.filter((x,i)=>x!==after[i]).length;
        // and it must be a real toggle, not a one-way latch
        pill.dispatchEvent(new MouseEvent('click',{bubbles:true}));
        out.send.litAgain = pill.classList.contains('fxr-on');
      }
      window.__setSynParam=real;
    }
    // nothing may resolve to pure black (the fb398 trap: an undefined CSS var falls back to
    // `initial`, which is black, and every currentColor consumer inherits it)
    // ⚠️ only PAINTING elements, and only the paint each one actually uses: an <svg> element's
    // computed fill is rgb(0,0,0) and it paints nothing, which made the first run of this gate
    // report 7 false positives. A gate that cries wolf gets ignored, which is worse than none.
    const paint=[...document.querySelectorAll('.fxr-dev .fxr-core path,.fxr-dev .fxr-core line,.fxr-dev .fxr-core rect,.fxr-dev .fxr-core circle,.fxr-dev .fxr-core text,.fxr-dev .fxr-core polyline')];
    out.blackList=paint.filter(e=>{const st=getComputedStyle(e);
      const strokeBad = st.stroke==='rgb(0, 0, 0)' && st.strokeWidth!=='0' && st.stroke!=='none';
      // a <line>/<polyline> never paints fill, and its computed fill is rgb(0,0,0) by default —
      // checking it flagged six perfectly good white centre rules on the first run.
      const canFill   = !/^(line|polyline)$/i.test(e.tagName);
      const fillBad   = canFill && st.fill==='rgb(0, 0, 0)' && (e.getAttribute('fill')||'')!=='none';
      return strokeBad||fillBad; }).map(e=>{const st=getComputedStyle(e);
        const par=e.closest('.fxr-core'); return e.tagName+'['+(par?par.getAttribute('data-core'):'?')+']'
          +' stroke='+st.stroke+' fill='+st.fill+' attrFill='+(e.getAttribute('fill')||'-'); }).slice(0,4);
    out.blackNodes=out.blackList.length;
    return out;
  });

  if(r.addErr) chk(false,'adding a device threw', r.addErr);
  for(const [core,name] of [['cho','Chorus'],['fla','Flanger'],['pha','Phaser']]){
    const o=r.devs[core]||{};
    console.log('\n── '+name);
    chk(o.count===6, name+': DUPLICATABLE and capped at 6 instances', 'asked for 8, got '+o.count);
    chk(o.core===true, name+': the card renders its own core');
    chk(o.routePills===6, name+': six route pills (per-osc ROUTABLE)', 'got '+o.routePills);
    chk(o.knobs===4, name+': four front knobs', 'got '+o.knobs);
    chk(o.backKnobs===8, name+': eight back knobs (the fb275 spec)', 'got '+o.backKnobs);
    chk(/Sync/.test(o.pills||''), name+': the Sync pill is present', o.pills);
    chk((o.dstCurves||0)>=1, name+': its lines ARE .dst-curve, not a copy of it', 'x'+o.dstCurves);
    chk(o.opacityOnCurve===0, name+': no per-frame opacity on an animated class (STROBE LAW)',
        'offenders '+o.opacityOnCurve);
    chk(!o.tickErr, name+': the 60 Hz driver runs clean', o.tickErr||'no throw');
    chk((o.redrew||0)>0, name+': the window REDRAWS from the C++ push', o.redrew+' paths moved');
    if(o.rateFree!=null||o.rateSync!=null)
      chk(o.rateFree!==o.rateSync && /\/|bar/.test(String(o.rateSync)),
          name+': SYNC LAW — Rate prints a TIME SIGNATURE when synced',
          '"'+o.rateFree+'" -> "'+o.rateSync+'"');
  }
  console.log('');
  for(const [core,name] of [['cho','Chorus'],['fla','Flanger'],['pha','Phaser']]){
    const t=r.typeWrite&&r.typeWrite[core];
    chk((r.typeWrite||{})[core+'_dupe']===0,
        name+': fb418 NO DOUBLES — the back panel carries no second Type selector',
        'duplicate Type selects: '+((r.typeWrite||{})[core+'_dupe']));
    chk(t && t.decoded===1 && t.pn===8,
        name+': fb373 GATE — d2 writes the PARAM\'s scale, not the list\'s',
        t ? (t.id+': picked entry 1 of '+t.opts+' on a choice('+t.pn+') param -> wrote '+t.norm+' -> decodes to index '+t.decoded)
          : 'no measurement');
  }
  const S=r.send||{};
  chk(S.onFirst===true && S.onOthers===0,
      'fb415 — the FIRST-SLOT LAW: Send is on the leftmost card and nowhere else',
      'first='+S.onFirst+', others carrying it='+S.onOthers);
  chk(S.otherW>S.firstW && S.othersEven===true,
      'fb415 — every later card gets its full-width, evenly-spaced route row back',
      'route letter width: first card '+S.firstW+'px, the rest '+S.otherW+'px, all six even');
  chk(S.litBefore===false && S.litAfter===true && S.litAgain===false,
      'fb414 — Send toggles both ways (off is the default, so old projects load identical)',
      S.litBefore+' -> '+S.litAfter+' -> '+S.litAgain);
  chk(/_SEND=1$/.test(S.wrote||''), 'fb414 — and it writes its own param', S.wrote);
  chk(S.moved===0, 'fb414 — adding it moved NONE of the six route letters (fixed-positions law)',
      S.moved+' moved');
  chk(r.blackNodes===0, 'nothing in any core resolves to black (the undefined-var trap)',
      r.blackNodes ? r.blackList.join(', ') : '0 black nodes');
  chk(r.cardsTotal===18, 'eighteen cards live at once (6 x 3) and the rack survives it',
      'got '+r.cardsTotal);

  console.log('\n  '+pass+' passed, '+fail+' FAILED\n');
  await b.close();
  process.exit(fail?1:0);
})();
