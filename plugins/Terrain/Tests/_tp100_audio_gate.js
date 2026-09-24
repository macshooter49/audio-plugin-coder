// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp100 — SETTINGS → AUDIO & MIDI TALKS TO THE REAL DEVICE (Max: "not one setting is a mock-up").
//    node Tests/_tp100_audio_gate.js
//  Boots the page twice. As the STANDALONE app (window.__isStandalone + a backend registry listing the
//  audio natives + a fake device): the rows are LIVE, the menus list the device's real outputs / rates /
//  buffers / MIDI ports, and each change reaches the native. As a PLUGIN in a DAW: the same rows read
//  "App only", and a native the backend never registered (setA4) reads "Soon" even though JUCE's
//  getNativeFunction hands back a callable for any name (the bug that made unbuilt rows look live).
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path'),os=require('os');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const html=fs.readFileSync('Source/ui/public/index.html','utf8');
const PAGE=path.join(os.tmpdir(),'tp100a.html'); fs.writeFileSync(PAGE,html);
let pass=0,fail=0; const ok=(c,l,d)=>{ (c?pass++:fail++); console.log('  '+(c?'PASS':'FAIL')+'  '+l+(d?'\n        '+d:'')); };
const DEV={standalone:true,drivers:['CoreAudio'],driver:'CoreAudio',outputs:['MacBook Pro Speakers','Scarlett 2i2'],inputs:['None','MacBook Pro Microphone','Scarlett 2i2'],
  out:'Scarlett 2i2',inDev:'None',rates:[44100,48000,96000],bufs:[64,128,256,512],outChs:['1 + 2'],inChs:['1','2','1 + 2'],sr:48000,buf:128,outCh:'1 + 2',inCh:'Off',outLatMs:3.1,inLatMs:2.9,
  midi:[{name:'PolyBrute',id:'pb',on:false},{name:'IAC Driver Bus 1',id:'iac',on:true}]};
async function boot(standalone){
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
  const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:1});
  await p.evaluateOnNewDocument(stubSrc+'\nstub();'+`
    (function(){ window.__calls=[]; const DEV=${JSON.stringify(DEV)}; const SA=${standalone};
      const NAT={ getAudioSetup:()=>JSON.stringify(SA?DEV:{standalone:false}),
                  setAudioSetup:(j)=>{ const o=JSON.parse(j); Object.assign(DEV,o); return JSON.stringify(SA?DEV:{standalone:false}); },
                  playTestTone:()=>0, setMidiInput:()=>0, setStandaloneBpm:()=>0, getDspLoad:()=>3, setVelCurve:()=>0 };
      const reg=Object.keys(NAT).concat(['setMotionEnabled','setCaptureEnabled','getSynParam']);
      if(SA) window.__isStandalone=1;
      const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>(...a)=>{ window.__calls.push([n].concat(a)); return Promise.resolve(NAT[n]?NAT[n](...a):(orig.getNativeFunction(n)?orig.getNativeFunction(n)(...a):undefined)); } });
      Object.defineProperty(window,'Juce',{configurable:true,get(){return wrap;},set(){}});
      const J=window.__JUCE__; if(J&&J.initialisationData) J.initialisationData.__juce__functions=reg; })();`);
  const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
  await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2200);
  await p.evaluate(()=>{ window.__st.open(true); window.__st.go('audio'); }); await sleep(600);
  return {b,p,errs};
}
(async()=>{
 // ── as the STANDALONE app ──
 let {b,p,errs}=await boot(true);
 const a=await p.evaluate(()=>{ const row=id=>document.querySelector('#st-overlay [data-row="'+id+'"]');
   const soon=id=>{ const r=row(id); return !r?'missing':(r.classList.contains('soon')?(r.querySelector('.tag')||{}).textContent:'live'); };
   return {driver:soon('driver'),out:soon('out'),sr:soon('sr'),buf:soon('buf'),test:soon('test'),tempo:soon('tempo'),
     outVal:(row('out')&&row('out').querySelector('.v')||{}).textContent, rates:[...(row('sr')?row('sr').querySelectorAll('.tp-chip'):[])].map(c=>c.textContent),
     rateOn:(row('sr')&&row('sr').querySelector('.tp-chip.active')||{}).textContent, ports:[...document.querySelectorAll('#st-overlay [data-row="ports"] .r:not(.hd) .n')].map(n=>n.textContent),
     lat:(row('lat')&&row('lat').textContent||'') }; });
 ok(['driver','out','sr','buf','test','tempo'].every(k=>a[k]==='live'), '[1] standalone: driver / output / rate / buffer / test / tempo rows are LIVE', JSON.stringify(a));
 ok(a.outVal==='Scarlett 2i2' && a.rates.join()==='44.1,48,96' && a.rateOn==='48', '[2] the menus show the DEVICE\'s own output, rates (44.1/48/96) and current rate', JSON.stringify({o:a.outVal,r:a.rates,on:a.rateOn}));
 ok(a.ports.join()==='PolyBrute,IAC Driver Bus 1' && /3\.1 ms out/.test(a.lat), '[3] MIDI ports are the real ones, latency is the device\'s', JSON.stringify({ports:a.ports,lat:a.lat.slice(-40)}));
 const c=await p.evaluate(async()=>{ window.__calls.length=0;
   const row=id=>document.querySelector('#st-overlay [data-row="'+id+'"]');
   [...row('sr').querySelectorAll('.tp-chip')].find(x=>x.textContent==='96').click(); await new Promise(r=>setTimeout(r,120));
   [...document.querySelectorAll('#st-overlay [data-row="ports"] .r')].find(x=>(x.querySelector('.n')||{}).textContent==='PolyBrute').click(); await new Promise(r=>setTimeout(r,120));
   const tr=document.querySelector('#st-overlay [data-row="test"]'); [...tr.querySelectorAll('.tp-chip')].find(x=>x.textContent==='Left').click(); await new Promise(r=>setTimeout(r,80));
   document.querySelector('#st-overlay [data-row="test"] .tp-btn').click(); await new Promise(r=>setTimeout(r,80));
   const tap=document.querySelector('#st-overlay [data-row="tempo"] .tp-btn'); tap.click(); await new Promise(r=>setTimeout(r,500)); tap.click(); await new Promise(r=>setTimeout(r,80));
   return window.__calls.filter(x=>/Audio|Midi|Tone|Bpm/.test(x[0])).map(x=>x[0]+'('+x.slice(1).join(',')+')'); });
 ok(c.some(x=>x==='setAudioSetup({"sr":96000})'), '[4] picking 96 kHz sends setAudioSetup({"sr":96000})', c.join(' '));
 ok(c.some(x=>x==='setMidiInput(PolyBrute,1)'), '[5] ticking PolyBrute enables that MIDI port', c.join(' '));
 ok(c.some(x=>x==='playTestTone(1)'), '[6] Test audio · Left plays the native chime on the left only', c.join(' '));
 ok(c.some(x=>/^setStandaloneBpm\((1[0-9]{2}|[5-9][0-9])\)$/.test(x)), '[7] tapping the tempo sets the standalone BPM', c.join(' '));
 ok(errs.length===0,'[8] no page errors (standalone)',errs.join(' | '));
 await b.close();
 // ── as a PLUGIN in a DAW ──
 ({b,p,errs}=await boot(false));
 const d=await p.evaluate(()=>{ const row=id=>document.querySelector('#st-overlay [data-row="'+id+'"]');
   const tag=id=>{ const r=row(id); return !r?'missing':(r.classList.contains('soon')?(r.querySelector('.tag')||{}).textContent:'live'); };
   const o={out:tag('out'),sr:tag('sr'),test:tag('test'),qwerty:tag('qwerty')}; window.__st.go('control');
   return new Promise(res=>setTimeout(()=>{ o.a4=tag('a4'); o.bend=tag('bend'); res(o); },150)); });
 ok(d.out==='App only' && d.sr==='App only' && d.test==='App only' && d.qwerty==='App only', '[9] in a DAW the device rows read "App only" (the DAW owns the sound card)', JSON.stringify(d));
 ok(d.a4==='Soon' && d.bend==='live', '[10] an unregistered native (setA4) reads "Soon" even though getNativeFunction returns a callable; a real one (bend) stays live', JSON.stringify(d));
 ok(errs.length===0,'[11] no page errors (plugin)',errs.join(' | '));
 await b.close();
 console.log(`\n  ${pass} passed, ${fail} failed`); process.exit(fail?1:0);
})();
