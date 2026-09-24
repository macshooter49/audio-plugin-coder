// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp103 — THE SETTINGS PAGE'S EXPRESSION + MIDI ROWS ARE LIVE, AND THEY READ THE PROCESSOR BACK.
//    node Tests/_tp103_midi_settings_gate.js          (from plugins/Terrain; Tests/node_modules = puppeteer-core)
//  The processor owns MPE / the MPE range / the MIDI channel (per instance, saved with the project) and A4 / the
//  voice ceiling (every instance). The page must: (1) light those rows (not "Soon") when the natives exist and keep
//  a row whose native is absent on "Soon"; (2) READ the real values back on open (getMidiSettings) — never push its
//  own saved copy over them; (3) send a change through the right native with the right arguments; (4) offer the new
//  Slide source in the mod picker's MIDI family.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer=require('puppeteer-core'); const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fs=require('fs'),path=require('path'),os=require('os');
const sim=fs.readFileSync(process.cwd()+'/Tests/_ui_lockin_sim.js','utf8');
const stubSrc=sim.slice(sim.indexOf('const stub = () => {'),sim.indexOf('// ── the instruments'));
const cpp=fs.readFileSync('Source/PluginEditor.cpp','utf8');
const i0=cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0=cpp.indexOf('html = html.replace ("</body>", heroOverlay',i0);
const ov=[...cpp.slice(i0,j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m=>m[1]).join('');
const html=fs.readFileSync('Source/ui/public/index.html','utf8').replace('</body>',ov+'</body>');
const PAGE=path.join(os.tmpdir(),'tp103.html'); fs.writeFileSync(PAGE,html);
let pass=0,fail=0; const ok=(c,l,d)=>{ (c?pass++:fail++); console.log('  '+(c?'PASS':'FAIL')+'  '+l+(d?'\n        '+d:'')); };
// every native the C++ registers must exist in PluginEditor.cpp (the page's NEED names them)
for(const n of ['getMidiSettings','setMpeOn','setMidiChannel','setA4','setVoiceCeiling'])
  ok(cpp.includes('withNativeFunction ("'+n+'"'), 'PluginEditor.cpp registers '+n);
(async()=>{
 const b=await puppeteer.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:'new',args:['--no-sandbox']});
 const p=await b.newPage(); await p.setViewport({width:820,height:656,deviceScaleFactor:1});
 await p.evaluateOnNewDocument(stubSrc+'\nstub();'+`
   (function(){ window.__midiLog=[]; const M={mpe:1,mpeBend:24,mpeArmed:0,chan:5,a4:432,voices:40};
     const J=()=>JSON.stringify(M);
     const mine={ getMidiSettings:()=>Promise.resolve(J()),
       setMpeOn:(on,bend)=>{ window.__midiLog.push(['setMpeOn',on,bend]); M.mpe=on?1:0; M.mpeBend=bend; return Promise.resolve(J()); },
       setMidiChannel:(c)=>{ window.__midiLog.push(['setMidiChannel',c]); M.chan=c; return Promise.resolve(J()); },
       setA4:(h)=>{ window.__midiLog.push(['setA4',h]); M.a4=h; return Promise.resolve(J()); },
       setVoiceCeiling:(v)=>{ window.__midiLog.push(['setVoiceCeiling',v]); M.voices=v; return Promise.resolve(J()); } };
     window.__JUCE__.initialisationData.__juce__functions=Object.keys(mine).concat(['setVelCurve','getSettings','saveSettings']);
     const orig=window.Juce; const wrap=Object.assign({},orig,{ getNativeFunction:(n)=>mine[n]||orig.getNativeFunction(n) });
     Object.defineProperty(window,'Juce',{configurable:true,get(){return wrap;},set(){}});
     // a STALE page copy in the settings file: it must not win over the processor
     window.__stInitS={mpe:'Off',mpeBend:48,chan:'Omni',a4:440,voices:32}; })();`);
 const errs=[]; p.on('pageerror',e=>errs.push(e.message.slice(0,160)));
 await p.goto('file://'+PAGE,{waitUntil:'load'}); await sleep(2400);
 await p.evaluate(()=>{ window.__st.open(true); window.__st.go('control'); }); await sleep(500);
 const rows=await p.evaluate(()=>{ const o={}; ['chan','a4','mpe','mpeBend','polyat'].forEach(id=>{ const r=document.querySelector('#st-overlay [data-row="'+id+'"]'); o[id]=r?(r.classList.contains('soon')?'soon':'live'):'missing'; }); return o; });
 ok(Object.values(rows).every(v=>v==='live'), 'MIDI & Controllers: channel, tuning, MPE, MPE range, poly aftertouch are live (not Soon)', JSON.stringify(rows));
 const S1=await p.evaluate(()=>{ const S=window.__st.S(); return {mpe:S.mpe,mpeBend:S.mpeBend,chan:S.chan,a4:S.a4,voices:S.voices}; });
 ok(S1.mpe==='On'&&S1.mpeBend===24&&S1.chan==='Channel 5'&&S1.a4===432&&S1.voices===40, 'the page READ the processor back over its stale saved copy', JSON.stringify(S1));
 const pushedOnOpen=await p.evaluate(()=>window.__midiLog.slice());
 ok(pushedOnOpen.length===0, 'opening the page pushed nothing over the processor', JSON.stringify(pushedOnOpen));
 const shown=await p.evaluate(()=>{ const t=r=>{ const e=document.querySelector('#st-overlay [data-row="'+r+'"]'); return e?e.textContent.replace(/\s+/g,' ').trim():''; }; return {a4:t('a4'),chan:t('chan'),mpe:t('mpe')}; });
 ok(/432/.test(shown.a4)&&/Channel 5/.test(shown.chan), 'the rows SHOW the processor\'s values', JSON.stringify(shown));
 // a change goes through the native with the right arguments
 await p.evaluate(()=>{ const b=[...document.querySelectorAll('#st-overlay [data-row="mpe"] .tp-chip')].find(x=>x.textContent==='Off'); b&&b.click(); }); await sleep(200);
 await p.evaluate(()=>{ window.__st.openMenuFor('chan'); }); await sleep(200);
 await p.evaluate(()=>{ const it=[...document.querySelectorAll('.pmenu .pi, #st-menu .pi, .pi')].find(x=>x.textContent.trim().startsWith('Channel 12')); it&&it.click(); }); await sleep(200);
 const log=await p.evaluate(()=>window.__midiLog.slice());
 ok(log.some(e=>e[0]==='setMpeOn'&&e[1]===0&&e[2]===24), 'MPE Off calls setMpeOn(0, 24)', JSON.stringify(log));
 ok(log.some(e=>e[0]==='setMidiChannel'&&e[1]===12), 'picking Channel 12 calls setMidiChannel(12)', JSON.stringify(log));
 await p.evaluate(()=>window.__st.go('perf')); await sleep(300);
 const perf=await p.evaluate(()=>{ const r=q=>{ const e=document.querySelector('#st-overlay [data-row="'+q+'"]'); return e?(e.classList.contains('soon')?'soon':'live'):'missing'; }; return {voices:r('voices'),rtQ:r('rtQ')}; });
 ok(perf.voices==='live'&&perf.rtQ==='soon', 'Voice ceiling is live; a row whose native is absent (Playing quality) still says Soon', JSON.stringify(perf));
 // the mod picker's MIDI family carries Slide, and the codec round-trips it
 const codec=await p.evaluate(()=>{ try{ const html=document.documentElement.innerHTML; return { picker:/mk\(\{sld:1\},'Slide'\)/.test(html) }; }catch(e){ return {err:String(e)}; } });
 ok(codec.picker, 'the mod picker lists Slide in its MIDI family', JSON.stringify(codec));
 ok(errs.length===0, 'the page threw nothing', errs.join(' | ')||'clean');
 await b.close();
 console.log('\n  '+pass+' passed, '+fail+' failed'); process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(2); });
