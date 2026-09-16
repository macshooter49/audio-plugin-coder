const puppeteer=require('puppeteer-core'); const PAGE=process.argv[2];
(async()=>{ const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox','--allow-file-access-from-files']});
 const p=await b.newPage(); await p.setViewport({width:1200,height:820}); const errs=[]; p.on('pageerror',e=>errs.push(String(e).slice(0,200)));
 await p.goto('file://'+PAGE,{waitUntil:'networkidle0'}); await new Promise(r=>setTimeout(r,1500));
 const out=await p.evaluate(async()=>{ const R={}, sleep=ms=>new Promise(r=>setTimeout(r,ms));
  const a1=window.__flowCardOf('arp',1); a1.ensure(); const a2=window.__flowCardOf('arp',2); a2.ensure(); await sleep(100);
  const c1=document.querySelector('.ti-card.arp-ext:not(.ti-inst)'), c2=document.querySelector('.ti-card.arp-ext.ti-inst2');
  R.arp1={ found:!!c1, dests:c1?c1.querySelectorAll('[data-mod-dest]').length:0, mix:c1?c1.querySelector('.mix').getAttribute('data-mod-dest'):null, first:c1?(c1.querySelector('[data-mod-dest]')||{}).getAttribute?c1.querySelector('[data-mod-dest]').getAttribute('data-mod-dest'):null:null };
  R.arp2={ found:!!c2, dests:c2?c2.querySelectorAll('[data-mod-dest]').length:0, mix:c2?c2.querySelector('.mix').getAttribute('data-mod-dest'):null, first:c2?(c2.querySelector('[data-mod-dest]')||{}).getAttribute?c2.querySelector('[data-mod-dest]').getAttribute('data-mod-dest'):null:null };
  R.flowInstDest=window.__flowInstDest?window.__flowInstDest(2,399):null; R.md={ a1:window.__flowModDestOf&&window.__flowModDestOf('FLOW_ARP_SWING'), a2:window.__flowModDestOf&&window.__flowModDestOf('FLOW_ARP2_SWING'), c2:window.__flowModDestOf&&window.__flowModDestOf('FLOW_CHOP2_SCAN'), s3:window.__flowModDestOf&&window.__flowModDestOf('FLOW_SEQ3_RATE') };
  // E on -> the Patcher adopts it
  window.Juce.getSliderState('SYN_OSC_E_ENABLE').setNormalisedValue(1);
  if(window.__tpOpen) window.__tpOpen(); await sleep(1500);
  const dev=document.getElementById('osc-e-device'); R.eOnCanvas=!!(dev&&dev.closest('#tp-page'));
  R.eHidden=!!(dev&&dev.classList.contains('osc-hidden')); R.eParent=dev?dev.parentNode.className:null;
  R.nodes=[...document.querySelectorAll('#tp-page .tp-node')].length;
  R.poolAwake=document.body.classList.contains('tp-pool-awake');
  // add Chop 2 from the browser path
  window.__flowSetChain(['chop','chop2']); await sleep(900);
  R.flowNodes=[...document.querySelectorAll('#tp-page .tp-node .flow-mode')].map(t=>t.getAttribute('data-mode'));
  R.instBadge=!!document.querySelector('#tp-page .tp-node .flow-mode[data-mode="chop2"] .tp-inst');
  return R; });
 console.log(JSON.stringify(out,null,1)); console.log('errs',JSON.stringify(errs)); await b.close(); })().catch(e=>{ console.log('FAIL',String(e).slice(0,300)); process.exit(1); });
