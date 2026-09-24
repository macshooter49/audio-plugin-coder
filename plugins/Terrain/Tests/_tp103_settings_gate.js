// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp103 — SETTINGS: THE APP / LIBRARY / SUPPORT HALF IS LIVE (and says so only when it is).
//    node Tests/_tp103_settings_gate.js        (from plugins/Terrain; puppeteer-core in Tests/node_modules)
//  Boots the page with a backend registry listing the tp103 natives (the shape TerrainSettingsNatives.cpp
//  registers), once as the STANDALONE app and once as a PLUGIN, and checks what each row does:
//    [1]  Performance: Playing / Bounce quality, Presets can change quality, Sleep, Cut tails are LIVE (not Soon)
//    [2]  the three quality rows travel together as one setRtQuality({rt,off,presets}); Sleep / Tails send 0/1
//    [3]  boot never calls setBootWidth (a page load must not resize a window) …
//    [4]  … and picking 125 % does: setBootWidth(1025)
//    [5]  Library: the three folders show the paths getLibraryInfo reports, a moved one says "Yours"
//    [6]  Rescan library reports the counts from the native ("Last rescan: 1,204 presets · 38 packs · 2,346 samples, 0.8 s")
//         and re-reads the preset browser
//    [7]  Show / Change… / Default call revealLibraryFolder / chooseLibraryFolder / resetLibraryFolder
//    [8]  Support: Copy system info → copySystemInfo · Report a bug… → reportIssue({kind:"bug"}) ·
//         Report a crash… → reportIssue({kind:"crash"}); the licence status rides along; Manual is "Soon"
//    [9]  no mail app (reportIssue answers ok:false): the toast says it was copied for contact@wavescrate.com
//    [10] About shows the build the native reports (getBuildInfo answers with a promise — the old read never saw it)
//    [11] Audio (standalone): Rescan devices is live and calls rescanAudioDevices; a MIDI port that sent something
//         lights its lamp (getMidiActivity); the page polls getAudioSetup for hot-plug
//    [12] in a DAW the device rows (incl. Rescan devices) read "App only"; the library / support rows stay live
//    [13] no page errors
//    [14] Scan on start OFF: the page boots without reading the preset catalogue (no listPresets) and reads it
//         the first time the browser opens; ON: it reads it at boot
//  Screenshots: $TMPDIR/tp103_<page>.png
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path'),os=require('os');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const html=fs.readFileSync('Source/ui/public/index.html','utf8');
const PAGE=path.join(os.tmpdir(),'tp103s.html'); fs.writeFileSync(PAGE,html);
const SHOTS=os.tmpdir();
let pass=0,fail=0; const ok=(c,l,d)=>{ (c?pass++:fail++); console.log('  '+(c?'PASS':'FAIL')+'  '+l+(d?'\n        '+d:'')); };
const DEV={standalone:true,drivers:['CoreAudio'],driver:'CoreAudio',outputs:['MacBook Pro Speakers','Scarlett 2i2'],inputs:['None','Scarlett 2i2'],
  out:'Scarlett 2i2',inDev:'None',rates:[44100,48000],bufs:[64,128,256,512],outChs:['1 + 2'],inChs:['1','2','1 + 2'],sr:48000,buf:128,outCh:'1 + 2',inCh:'Off',outLatMs:3.1,inLatMs:2.9,
  midi:[{name:'Launchkey MK4 61 MIDI Out',id:'lk',on:true},{name:'IAC Driver Bus 1',id:'iac',on:false}]};
const LIB={scanOnStart:true,presets:{path:'/Users/max/Library/Application Support/WavesCrate/Terrain/Banks',exists:true,custom:false,missing:''},
  samples:{path:'/Volumes/Samples/Terrain',exists:true,custom:true,missing:''},
  factory:{path:'/Library/Audio/Plug-Ins/Components/Terrain.component/Contents/Resources',exists:true,custom:false,missing:''}};
async function boot(standalone,mailOk,scanOnStart){ const L2=Object.assign({},LIB,{scanOnStart:scanOnStart!==false});
  const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
  const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:2});
  await p.evaluateOnNewDocument(stubSrc+'\nstub();'+`
    (function(){ window.__calls=[]; const DEV=${JSON.stringify(DEV)}; const LIB=${JSON.stringify(L2)}; const SA=${standalone}; const MAIL=${mailOk}; let midiOnce=false;
      const NAT={ getAudioSetup:()=>JSON.stringify(SA?DEV:{standalone:false}), setAudioSetup:()=>JSON.stringify(SA?DEV:{standalone:false}),
                  rescanAudioDevices:()=>JSON.stringify(SA?DEV:{standalone:false}),
                  getMidiActivity:()=>{ if(!SA||midiOnce) return '[]'; midiOnce=true; return JSON.stringify(['Launchkey MK4 61 MIDI Out']); },
                  getOutLevel:()=>0, playTestTone:()=>0, setMidiInput:()=>0, setStandaloneBpm:()=>0, getDspLoad:()=>3, setVelCurve:()=>0,
                  setRtQuality:()=>1, setSleepWhenSilent:(v)=>v, setCutTailsOnStop:(v)=>v, getEngineState:()=>'{}', setBootWidth:()=>0,
                  getLibraryInfo:()=>JSON.stringify(LIB), rescanLibrary:()=>JSON.stringify({presets:1204,packs:38,unreadable:0,factorySamples:2310,userSamples:36,ms:812}),
                  revealLibraryFolder:(k)=>LIB[k].path, chooseLibraryFolder:()=>JSON.stringify({ok:false}), resetLibraryFolder:()=>JSON.stringify(LIB),
                  getBuildInfo:()=>JSON.stringify({ver:'1.0.0-beta',build:'abc1234 · Sep 24 2026',wrap:SA?'Standalone app':'AU in Ableton Live 12.1'}),
                  getSystemInfo:()=>'Terrain 1.0.0-beta', copySystemInfo:()=>'Terrain 1.0.0-beta',
                  reportIssue:(j)=>JSON.stringify({ok:MAIL,copied:!MAIL,truncated:false,crashLog:'',url:'mailto:contact@wavescrate.com'}),
                  listPresets:()=>'' };
      const reg=Object.keys(NAT).concat(['setMotionEnabled','setCaptureEnabled','getSynParam']);
      if(SA) window.__isStandalone=1;
      const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>(...a)=>{ window.__calls.push([n].concat(a)); return Promise.resolve(NAT[n]?NAT[n](...a):(orig.getNativeFunction(n)?orig.getNativeFunction(n)(...a):undefined)); } });
      Object.defineProperty(window,'Juce',{configurable:true,get(){return wrap;},set(){}});
      const J=window.__JUCE__; if(J&&J.initialisationData) J.initialisationData.__juce__functions=reg; })();`);
  const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
  await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2200);
  return {b,p,errs};
}
const tagsOf=(p,ids)=>p.evaluate((ids)=>{ const o={}; ids.forEach(id=>{ const r=document.querySelector('#st-overlay [data-row="'+id+'"]');
  o[id]=!r?'missing':(r.classList.contains('soon')?((r.querySelector('.tag')||{}).textContent||'soon'):'live'); }); return o; },ids);
const go=async(p,pg)=>{ await p.evaluate((pg)=>{ window.__st.open(true); window.__st.go(pg); },pg); await sleep(500); };
const shot=async(p,name)=>{ const f=path.join(SHOTS,'tp103_'+name+'.png'); await p.screenshot({path:f}); return f; };
(async()=>{
 // ══ as the STANDALONE app ══
 let {b,p,errs}=await boot(true,true);
 const bootCalls=await p.evaluate(()=>window.__calls.map(x=>x[0]));
 ok(!bootCalls.includes('setBootWidth'), '[3] booting the page does not call setBootWidth', bootCalls.filter(n=>/Width|Quality|Sleep|Tails/.test(n)).join(' '));

 await go(p,'perf');
 const perf=await tagsOf(p,['rtQ','offQ','presetQ','sleep','tails']);
 ok(Object.values(perf).every(v=>v==='live'), '[1] the quality / sleep / tails rows are LIVE', JSON.stringify(perf));
 const pc=await p.evaluate(async()=>{ window.__calls.length=0; const row=id=>document.querySelector('#st-overlay [data-row="'+id+'"]');
   const pick=(id,t)=>{ [...row(id).querySelectorAll('.tp-chip')].find(x=>x.textContent===t).click(); };
   pick('rtQ','High'); await new Promise(r=>setTimeout(r,60)); pick('offQ','Best'); await new Promise(r=>setTimeout(r,60)); pick('presetQ','Off'); await new Promise(r=>setTimeout(r,60));
   pick('sleep','Off'); await new Promise(r=>setTimeout(r,60)); pick('tails','On'); await new Promise(r=>setTimeout(r,60));
   return window.__calls.map(x=>x[0]+'('+x.slice(1).join(',')+')'); });
 ok(pc.includes('setRtQuality({"rt":"High","off":"Best","presets":"Off"})') && pc.includes('setSleepWhenSilent(0)') && pc.includes('setCutTailsOnStop(1)'),
    '[2] quality travels as one setRtQuality({rt,off,presets}); sleep / tails send 0 / 1', pc.filter(x=>/Quality|Sleep|Tails/.test(x)).join(' '));
 await shot(p,'perf');

 await go(p,'ui');
 const sz=await p.evaluate(async()=>{ window.__calls.length=0; window.__st.openMenuFor('size'); await new Promise(r=>setTimeout(r,120));
   const it=[...document.querySelectorAll('.st-menu .pi')].find(x=>x.querySelector('.nm').textContent==='125 %'); if(it) it.click(); await new Promise(r=>setTimeout(r,120));
   return window.__calls.filter(x=>x[0]==='setBootWidth').map(x=>x[1]); });
 ok(sz.length===1 && sz[0]===1025, '[4] picking Window size 125 % calls setBootWidth(1025)', JSON.stringify(sz));

 await go(p,'library'); await sleep(300);
 const lib=await p.evaluate(()=>[...document.querySelectorAll('#st-overlay [data-row="libdirs"] .r:not(.hd)')].map(r=>({n:r.querySelector('.c3').textContent,p:r.querySelector('.n').textContent,
   tag:(r.querySelector('.tg')||{}).textContent||'',links:[...r.querySelectorAll('.lk:not(.off)')].map(x=>x.textContent)})));
 ok(lib.length===3 && lib[0].p==='~/Library/Application Support/WavesCrate/Terrain/Banks' && lib[1].tag==='Yours' && lib[1].links.includes('Default') && lib[2].p.includes('Terrain.component'),
    '[5] the three folders show the native\'s paths (home as ~); the moved one says Yours and offers Default', JSON.stringify(lib));
 const rs=await p.evaluate(async()=>{ window.__calls.length=0; let browserRescans=0; const R=window.__tiPresets&&window.__tiPresets.rescan; if(window.__tiPresets) window.__tiPresets.rescan=function(){ browserRescans++; return R?R.apply(this,arguments):Promise.resolve(); };
   document.querySelector('#st-overlay [data-row="rescan"] .tp-btn').click(); await new Promise(r=>setTimeout(r,300));
   return {txt:document.querySelector('#st-overlay [data-row="rescan"] .note.res').textContent, lane:document.querySelector('#st-overlay [data-row="rescan"] .val').textContent, calls:window.__calls.map(x=>x[0]), browserRescans}; });
 ok(rs.txt==='Last rescan: 1,204 presets · 38 packs · 2,346 samples, 0.8 s' && rs.lane==='0.8 s' && rs.calls.includes('rescanLibrary') && rs.browserRescans===1,
    '[6] Rescan library shows the real counts and re-reads the preset browser', JSON.stringify(rs));
 const lk=await p.evaluate(async()=>{ window.__calls.length=0; const rows=[...document.querySelectorAll('#st-overlay [data-row="libdirs"] .r:not(.hd)')];
   rows[0].querySelector('.sh').click(); rows[0].querySelector('.ch').click(); await new Promise(r=>setTimeout(r,80)); rows[1].querySelector('.df').click(); await new Promise(r=>setTimeout(r,120));
   return window.__calls.map(x=>x[0]+'('+x.slice(1).join(',')+')'); });
 ok(lk.includes('revealLibraryFolder(presets)') && lk.includes('chooseLibraryFolder(presets)') && lk.includes('resetLibraryFolder(samples)'),
    '[7] Show / Change… / Default reach revealLibraryFolder / chooseLibraryFolder / resetLibraryFolder', lk.join(' '));
 await sleep(400); await shot(p,'library');

 await go(p,'about'); await sleep(300);
 const ab=await p.evaluate(async()=>{ window.__calls.length=0; const bs=[...document.querySelectorAll('#st-overlay [data-row="sup"] .tp-btn')];
   const names=bs.map(x=>x.textContent); bs[2].click(); await new Promise(r=>setTimeout(r,60)); bs[0].click(); await new Promise(r=>setTimeout(r,60)); bs[1].click(); await new Promise(r=>setTimeout(r,80));
   const kv=[...document.querySelectorAll('#st-overlay [data-row="ver"] .v')].map(v=>v.textContent);
   return {names,calls:window.__calls.map(x=>x[0]+'('+x.slice(1).join(',')+')'),kv}; });
 ok(ab.names.join('|')==='Report a bug…|Report a crash…|Copy system info|Manual Soon'
    && ab.calls.some(x=>x.startsWith('copySystemInfo('))
    && ab.calls.some(x=>/^reportIssue\(\{"kind":"bug","license":"Not registered"\}\)$/.test(x))
    && ab.calls.some(x=>/^reportIssue\(\{"kind":"crash","license":"Not registered"\}\)$/.test(x)),
    '[8] Copy system info / Report a bug… / Report a crash… reach their natives with the licence status; Manual reads Soon', JSON.stringify(ab.names)+' '+ab.calls.join(' '));
 ok(ab.kv[1]==='abc1234 · Sep 24 2026' && ab.kv[2]==='Standalone app', '[10] About shows the build getBuildInfo answered with', JSON.stringify(ab.kv));
 await sleep(2600); await shot(p,'about');

 await go(p,'audio'); await sleep(900);
 const au=await p.evaluate(async()=>{ const row=id=>document.querySelector('#st-overlay [data-row="'+id+'"]');
   const live=!row('devscan').classList.contains('soon'); window.__calls.length=0; row('devscan').querySelector('.tp-btn').click(); await new Promise(r=>setTimeout(r,100));
   const polled=[]; const t0=performance.now(); while(performance.now()-t0<600){ polled.push(...window.__calls.map(x=>x[0])); await new Promise(r=>setTimeout(r,50)); }
   return {live,calls:[...new Set(polled)]}; });
 const lampSeen=await p.evaluate(()=>{ const L=document.querySelectorAll('#st-overlay [data-row="ports"] .lamp'); return L.length; });
 ok(au.live && au.calls.includes('rescanAudioDevices') && au.calls.includes('getMidiActivity'), '[11a] Rescan devices is live and calls rescanAudioDevices; the page polls getMidiActivity', JSON.stringify(au));
 ok(lampSeen===2, '[11b] every MIDI port has its lamp', String(lampSeen));
 await shot(p,'audio');
 ok(errs.length===0,'[13a] no page errors (standalone)',errs.join(' | '));
 await b.close();

 // the lamp, measured on a fresh boot so the one-shot activity lands while the page is watching
 ({b,p,errs}=await boot(true,false));
 await p.evaluate(()=>{ window.__st.open(true); window.__st.go('audio'); });
 const lampColour=await p.evaluate(()=>new Promise(res=>{ const t0=performance.now(); (function look(){ const L=[...document.querySelectorAll('#st-overlay [data-row="ports"] .r')].find(r=>(r.querySelector('.n')||{}).textContent==='Launchkey MK4 61 MIDI Out');
     const lamp=L&&L.querySelector('.lamp'); if(lamp&&lamp.classList.contains('lv')){ res(getComputedStyle(lamp).backgroundColor); return; } if(performance.now()-t0>3000){ res('never'); return; } setTimeout(look,10); })(); }));
 ok(lampColour==='rgb(183, 148, 255)', '[11c] a port that sent MIDI lights its lamp purple', lampColour);
 // [9] no mail app
 await go(p,'about');
 const nm=await p.evaluate(async()=>{ [...document.querySelectorAll('#st-overlay [data-row="sup"] .tp-btn')][0].click(); await new Promise(r=>setTimeout(r,150)); return document.getElementById('st-toast').textContent; });
 ok(/Copied — paste into an email to contact@wavescrate\.com/.test(nm), '[9] no mail app: the report is copied and the toast says where to send it', nm);
 ok(errs.length===0,'[13b] no page errors (standalone, no mail)',errs.join(' | '));
 await b.close();

 // ══ as a PLUGIN in a DAW ══
 ({b,p,errs}=await boot(false,true));
 await go(p,'audio');
 const daw=await tagsOf(p,['driver','out','devscan','test']);
 await go(p,'library'); const dl=await tagsOf(p,['rescan','scan']);
 await go(p,'perf'); const dp=await tagsOf(p,['rtQ','sleep','tails']);
 ok(Object.values(daw).every(v=>v==='App only') && Object.values(dl).every(v=>v==='live') && Object.values(dp).every(v=>v==='live'),
    '[12] in a DAW the device rows read "App only"; library and performance rows stay live', JSON.stringify({daw,dl,dp}));
 await go(p,'audio'); await shot(p,'audio_daw');
 ok(errs.length===0,'[13c] no page errors (plugin)',errs.join(' | '));
 await b.close();
 // ══ Scan on start ══
 ({b,p,errs}=await boot(false,true,false)); await sleep(1500);
 const offBoot=await p.evaluate(()=>window.__calls.filter(x=>x[0]==='listPresets').length);
 await p.evaluate(()=>{ if(window.__tiOpenPresetBrowser) window.__tiOpenPresetBrowser(); }); await sleep(400);
 const offOpen=await p.evaluate(()=>window.__calls.filter(x=>x[0]==='listPresets').length);
 await b.close();
 ({b,p,errs}=await boot(false,true,true)); await sleep(1500);
 const onBoot=await p.evaluate(()=>window.__calls.filter(x=>x[0]==='listPresets').length);
 await b.close();
 ok(offBoot===0 && offOpen>=1 && onBoot>=1, '[14] Scan on start: Off = no catalogue read at boot, read on first browse; On = read at boot',
    JSON.stringify({offBoot,offOpen,onBoot}));
 console.log(`\n  screenshots: ${SHOTS}/tp103_{perf,library,about,audio,audio_daw}.png`);
 console.log(`\n  ${pass} passed, ${fail} failed`); process.exit(fail?1:0);
})();
