// tp20 — THE POOL ON THE PAGE: oscillators E–H exist as devices with their own knobs, the mod-dest mirrors are
// stamped, the Flow chain carries instances, the cards build per instance, and the rack grew its E–H pills.
//   node Tests/_tp20_pool.js "$PWD/Source/ui/public/index.html"
const puppeteer=require('puppeteer-core');
const PAGE=process.argv[2];
const CHROME='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
(async()=>{
  const b=await puppeteer.launch({executablePath:CHROME,headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
  const p=await b.newPage(); await p.setViewport({width:1200,height:820});
  const errs=[]; p.on('pageerror',e=>errs.push(String(e).slice(0,160)));
  await p.goto('file://'+PAGE,{waitUntil:'networkidle0'});
  await p.evaluate(()=>{ try{ localStorage.clear(); }catch(e){} });
  await new Promise(r=>setTimeout(r,1500));
  const out=await p.evaluate(async()=>{
    const R={}, sleep=ms=>new Promise(r=>setTimeout(r,ms));
    // 1 · the devices
    R.devices=['a','b','c','d','e','f','g','h'].map(o=>!!document.getElementById('osc-'+o+'-device'));
    R.knobsB=document.querySelectorAll('#osc-b-device [data-syn^="SYN_OSC_B_"]').length;
    R.knobsE=document.querySelectorAll('#osc-e-device [data-syn^="SYN_OSC_E_"]').length;
    R.strayB=document.querySelectorAll('#osc-e-device [data-syn^="SYN_OSC_B_"], #osc-e-device [id^="osc-b-"]').length;
    R.poolHome=!!document.getElementById('osc-pool-home');
    R.letterE=(document.querySelector('#osc-e-device .osc-letter text')||{}).textContent;
    R.oscPool=(window.__oscPool||[]).join('');
    // 2 · the slider states
    const stE=window.Juce.getSliderState('SYN_OSC_E_LEVEL'), stA=window.Juce.getSliderState('SYN_OSC_A_LEVEL');
    R.poolStateE=!!(stE&&stE.__id==='SYN_OSC_E_LEVEL'); R.relayStateA=!!(stA&&stA.__id===undefined);
    R.isPool={e:window.__isPoolParam('SYN_OSC_E_LEVEL'), a:window.__isPoolParam('SYN_OSC_A_LEVEL'), c5:window.__isPoolParam('FLOW_CHAIN_5'), c4:window.__isPoolParam('FLOW_CHAIN_4'), i1:window.__isPoolParam('FLOW_CHAIN_INST_1'), arp2:window.__isPoolParam('FLOW_ARP2_RATE'), srcE:window.__isPoolParam('SYN_DLY2_SRC_E')};
    // 3 · the mod-dest mirrors
    R.dest={ aLevel:window.__knobDest?window.__knobDest('SYN_OSC_A_LEVEL'):null, eLevel:window.__knobDest?window.__knobDest('SYN_OSC_E_LEVEL'):null, hFrame:window.__knobDest?window.__knobDest('SYN_OSC_H_WT_FRAME'):null };
    const md=id=>window.__flowModDestOf?window.__flowModDestOf(id):null;
    R.flowDest={ chopScan:md('FLOW_CHOP_SCAN'), chop2Scan:md('FLOW_CHOP2_SCAN'), arp4Swing:md('FLOW_ARP4_SWING'), drf2:md('FLOW_DRF2_MORPH') };   // 399 · 4171 · 3780+2*473+358=5084 · undefined (Robin is one)
    // 4 · the chain carries instances
    window.__flowSetChain(['chop','glitch2','chop3']); await sleep(50);
    R.chain=window.__flowChain();
    const g=id=>{ try{ return +window.Juce.getSliderState(id).getNormalisedValue().toFixed(3); }catch(e){ return 'x'; } };
    R.slots={ i2:g('FLOW_CHAIN_INST_2'), i3:g('FLOW_CHAIN_INST_3'), i1:g('FLOW_CHAIN_INST_1') };   // slots 1..4 ride the fb131 shim (not readable here); the instances ride the pool state: 1/3 · 2/3 · 0
    R.badChain=window.__flowSetChain(['drift2','chop9','nope','arp']); R.chainClean=window.__flowChain();
    // 5 · the cards build per instance
    const c2=window.__flowCardOf('chop',2); if(c2&&c2.ensure) c2.ensure();
    const g3=window.__flowCardOf('glitch',3); if(g3&&g3.ensure) g3.ensure();
    const a1=window.__flowCardOf('arp',1); if(a1&&a1.ensure) a1.ensure();
    await sleep(100);
    R.cards={ chop1:!!document.querySelector('.ti-card.chop-ext:not(.ti-inst)'), chop2:!!document.querySelector('.ti-card.chop-ext.ti-inst2'), gli3:!!document.querySelector('.ti-card.gli-ext.ti-inst3'),
              dice:Object.keys(window.__tiDice||{}).filter(k=>/^(chop|gli|arp)\d?$/.test(k)).sort().join(','),
              chop2Title:(document.querySelector('.ti-card.chop-ext.ti-inst2 .tt')||{}).textContent,
              chop2Mix:(document.querySelector('.ti-card.chop-ext.ti-inst2 .mix')||{}).getAttribute?document.querySelector('.ti-card.chop-ext.ti-inst2 .mix').getAttribute('data-mod-dest'):null,
              chop2Knob:(document.querySelector('.ti-card.chop-ext.ti-inst2 [data-mod-dest]')||{}).getAttribute?document.querySelector('.ti-card.chop-ext.ti-inst2 [data-mod-dest]').getAttribute('data-mod-dest'):null };
    R.pidOf={ chop2:window.__flowPidOf('chop2'), gli:window.__flowPidOf('glitch'), rbn:window.__flowPidOf('drift'), back:window.__flowModeOfPid('gli2') };
    // 6 · the rack's pills
    const devs=window.__fxrDevs?window.__fxrDevs():null; R.rack={ devs:devs?devs.length:null };
    if(window.__fxrAdd){ try{ window.__fxrAdd('delay'); }catch(e){} await sleep(200); }
    const dv=window.__fxrDevs?window.__fxrDevs():[]; const d0=dv.find(d=>d&&d.rp);
    R.rack.rp=d0?d0.rp.length:null; R.rack.rpE=d0?d0.rp[6]:null; R.rack.route=d0?d0.route.length:null;
    R.rack.pills=document.querySelectorAll('#fxr-rack .fxr-dev .fxr-r').length; R.rack.poolPills=document.querySelectorAll('#fxr-rack .fxr-dev .fxr-r.fxr-pool').length;
    R.rack.poolPillsVisible=[...document.querySelectorAll('#fxr-rack .fxr-dev .fxr-r.fxr-pool')].filter(e=>getComputedStyle(e).display!=='none').length;
    // 7 · the Patcher lists the pool
    if(window.__tpOpen) window.__tpOpen(); await sleep(1200);
    R.tp={ open:!!document.querySelector('#tp-page'), oscEIdle:!document.getElementById('osc-e-device').closest('#tp-page') };   // E is off: not on the canvas (adoption is _tp20_canvas.js)
    return R;
  });
  console.log(JSON.stringify(out,null,1)); console.log('errs',JSON.stringify(errs));
  await b.close();
})().catch(e=>{ console.log('FAIL',String(e).slice(0,300)); process.exit(1); });
