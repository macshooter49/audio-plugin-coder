#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fb399 — the FLANGER mockup. Same shipped card as the chorus (via fx3lift), different window.

The chorus window is lines: a response curve and two voice trails. A flanger is not lines --
it is a moving harmonic comb, and the thing that makes OUR flanger worth building is the
through-zero null, measured at -61.2 dB where the same machine in Add polarity reads -10.5.

So this window is a WATERFALL. Time scrolls left, frequency runs bottom-to-top on the house
log axis, and the comb's nulls carve dark stripes through a bright field -- which is exactly
what a flanger looks like on a spectrogram, and where "jet" got its name. When the sweep
crosses zero the whole column goes white and then scrolls away, leaving a visible scar in the
history: the null becomes something you can still see two seconds after you heard it.

  python3 build-flanger.py  ->  flanger-mockup.html
"""
import io, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fx3lift as L

OUT = os.path.join(L.HERE, 'flanger-mockup.html')
src = L._src()
theme, css, knob = L.theme_vars(src), L.card_css(src), L.knob_svg(src)
IC_PLUS, IC_ARROW, IC_X = L.glyphs(src)
wk = L.worklet('flanger')

TYPES = ['Tape Zero', 'Jet', 'BBD', 'Endless', 'Envelope', 'Step']

page = u"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Flanger — mockup</title>
<style>
%(theme)s
*{box-sizing:border-box}
html,body{margin:0;height:100%%}
body{background:radial-gradient(900px 520px at 30%% -10%%,#241d3a 0,transparent 60%%),#141220;
  font-family:-apple-system,'SF Pro Text',system-ui,sans-serif;-webkit-font-smoothing:antialiased;
  display:flex;flex-direction:column;align-items:center;gap:20px;padding:24px 18px 40px}
/* ══ LIFTED VERBATIM FROM index.html — do not hand-edit ══ */
%(css)s
/* ══ end lifted ══ */
#syn-panel .fxr-core .fl-t{animation:none !important}   /* we drive opacity per frame; the
   inherited mvBreathe fought it and strobed purple (fb401) */
#syn-panel .fxr-core{margin:0}                          /* edge to edge (Max, fb401) */
.bar{display:flex;align-items:center;gap:16px;flex-wrap:wrap;justify-content:center;padding:10px 15px;
  border:1px solid var(--border-strong);border-radius:11px;background:rgba(255,255,255,.02)}
.bar h1{font-size:11px;font-weight:500;letter-spacing:2.4px;text-transform:uppercase;color:var(--text-secondary);margin:0 4px 0 0}
.seg{display:flex;border:1px solid var(--border-strong);border-radius:8px;overflow:hidden}
.seg button{background:transparent;border:0;color:var(--text-secondary);font:inherit;font-size:10px;
  padding:6px 12px;cursor:pointer;border-right:1px solid rgba(255,255,255,.12)}
.seg button:last-child{border-right:0}
.seg button[aria-pressed="true"]{color:#fff;background:rgba(155,109,255,.26)}
.play{background:transparent;border:1px solid var(--purple-400);color:var(--purple-400);border-radius:8px;
  padding:6px 15px;font:inherit;font-size:10px;letter-spacing:1.2px;text-transform:uppercase;cursor:pointer}
.play.on{background:var(--purple-500);border-color:var(--purple-500);color:#fff;box-shadow:0 0 14px rgba(139,92,246,.5)}
.note{font-size:10px;color:var(--text-muted);max-width:780px;text-align:center;line-height:1.7}
.err{color:#FF8B8B;font-size:11px;max-width:780px;text-align:center;white-space:pre-wrap}
.rackwrap{height:376px;display:flex;justify-content:center;width:100%%}
.zoom{transform:scale(2.0);transform-origin:top center;width:272px;flex:0 0 272px}
#syn-panel{display:block;width:272px}
.rackstrip{display:flex;gap:9px;height:170px;width:272px}
</style></head><body>

<div class="bar">
  <h1>Flanger</h1>
  <div class="seg" id="src">
    <button data-s="sine" aria-pressed="true">Sine</button>
    <button data-s="pad">Pad</button>
    <button data-s="pluck">Pluck</button>
    <button data-s="noise">Noise</button>
  </div>
  <button class="play" id="play">Play</button>
</div>

<div class="rackwrap"><div class="zoom"><div id="syn-panel"><div class="rackstrip" id="rack"></div></div></div></div>
<p class="note" id="status">Press Play, then try <b>Tape Zero</b> with Depth up — watch the column go white when the sweep crosses zero.</p>
<p class="err" id="err"></p>

<script id="wk" type="text/worklet">%(worklet)s</script>
<script>
"use strict";
var TYPES=%(types)s;
var IC_PLUS=%(icplus)s, IC_ARROW=%(icarrow)s, IC_X=%(icx)s;
var RLBL=['A','B','C','D','S','N'];
%(knob)s

var D={ name:'Flanger', type:TYPES[0], types:TYPES, on:true,
        pills:[{t:'Sync',on:false},{t:'Invert',on:true}],
        route:[true,true,false,false,false,false],
        knobs:[{l:'Rate',v:30},{l:'Depth',v:55},{l:'Feedback',v:50},{l:'Mix',v:50}] };
var CH=[], chIdx=0;

function devHTML(d){
  var pills=d.pills.map(function(p){return '<span class="fxr-pill'+(p.on?' fxr-on':'')+'"><span class="fxr-t">'+p.t+'</span></span>';}).join('');
  var route=d.route.map(function(on,i){return '<span class="fxr-r'+(on?' fxr-on':'')+'" data-r="'+i+'"><span class="fxr-t">'+RLBL[i]+'</span></span>';}).join('');
  var knobs=d.knobs.map(function(k,i){return '<div class="fxr-knob"><div class="fxr-dial" data-k="'+i+'">'+knobSVG(k.v,null,null)+'</div><span class="fxr-lab">'+k.l+'</span></div>';}).join('');
  return '<div class="fxr-dev fxr-sel" data-dev="0">'
    +'<div class="fxr-head">'
      +'<span class="fxr-grip">\\u22ee\\u22ee</span><span class="fxr-name">'+d.name+'</span>'
      +'<span class="fxr-type"><span class="fxr-tw"><span class="fxr-tl">'+d.type+'<span class="fxr-car">\\u25be</span></span>'
        +d.types.map(function(t){return '<span class="fxr-tg">'+t+'<span class="fxr-car">\\u25be</span></span>';}).join('')
        +'</span><select class="fxr-type-native" id="tsel">'+d.types.map(function(t){return '<option'+(t===d.type?' selected':'')+'>'+t+'</option>';}).join('')+'</select></span>'
      +'<span class="fxr-preset" data-act="preset"><span class="fxr-star">\\u2726</span><span class="fxr-pname" id="chname">Init</span><span class="fxr-car">\\u25be</span><select class="fxr-type-native" id="csel"></select></span>'
      +'<span class="fxr-spacer"></span>'
      +'<span class="fxr-headr"><span class="fxr-swap" data-act="swap" title="More parameters">'+IC_PLUS+IC_ARROW+'</span>'
        +'<span class="fxr-pwr" data-act="pwr"></span><span class="fxr-x" data-act="x" title="Remove">'+IC_X+'</span></span>'
    +'</div>'
    +'<div class="fxr-core" data-core="flanger"><svg id="wf" preserveAspectRatio="none">'
      +[0,1,2,3,4,5,6,7].map(function(i){return '<path class="dst-curve fl-t" id="t'+i+'"/>';}).join('')
      +'</svg></div>'
    +'<div class="fxr-ctrls">'
      +'<div class="fxr-knobs">'+knobs+'</div><div class="fxr-divider"></div>'
      +'<div class="fxr-rightcol"><div class="fxr-pills">'+pills+'</div><div class="fxr-route">'+route+'</div></div>'
    +'</div></div>';
}
document.getElementById('rack').innerHTML=devHTML(D);

var PKEY=['rate','depth','feedback','mix'];
document.querySelectorAll('.fxr-dial').forEach(function(dial){
  var i=+dial.getAttribute('data-k'), drag=false, y0=0, v0=0;
  dial.addEventListener('pointerdown',function(e){drag=true;y0=e.clientY;v0=D.knobs[i].v;dial.setPointerCapture(e.pointerId);e.preventDefault();});
  dial.addEventListener('pointermove',function(e){ if(!drag)return;
    var v=Math.max(0,Math.min(100,v0+(y0-e.clientY)*0.6));
    D.knobs[i].v=v; dial.innerHTML=knobSVG(v,null,null); push(PKEY[i],v/100); });
  dial.addEventListener('pointerup',function(){drag=false;});
});
document.querySelector('.fxr-pwr').addEventListener('click',function(){
  D.on=!D.on; document.querySelector('.fxr-dev').classList.toggle('fxr-off',!D.on);
  if(wetG){var t=ac.currentTime; wetG.gain.setTargetAtTime(D.on?1:0,t,0.02); dryG.gain.setTargetAtTime(D.on?0:1,t,0.02);} });
document.querySelectorAll('.fxr-r').forEach(function(e){e.addEventListener('click',function(){e.classList.toggle('fxr-on');});});
document.querySelectorAll('.fxr-pill').forEach(function(e,i){e.addEventListener('click',function(){
  var on=!e.classList.contains('fxr-on'); e.classList.toggle('fxr-on',on);
  if(!node) return;
  if(i===0) node.port.postMessage({tempoSync:on, bpm:120});          // Sync — real, not decorative
  else { D.pills[1].on=on;                                          // Invert — flip comb polarity
         push('feedback', on ? D.knobs[2].v/100 : 1-(D.knobs[2].v/100)); }
});});

var ac=null,node=null,wetG=null,dryG=null,outG=null,srcN=null,playing=false,kind='sine',viz={lfo:0,lvl:0,notch:[]};
function fail(m){document.getElementById('err').textContent=m;document.getElementById('status').textContent='Failed to start.';}
async function addWorklet(){
  var code=document.getElementById('wk').textContent;
  try{ await ac.audioWorklet.addModule(URL.createObjectURL(new Blob([code],{type:'text/javascript'}))); return 'blob'; }
  catch(e1){ try{ await ac.audioWorklet.addModule('data:text/javascript;base64,'+btoa(unescape(encodeURIComponent(code)))); return 'data'; }
    catch(e2){ throw new Error('addModule failed.\\nblob: '+e1.message+'\\ndata: '+e2.message); } }
}
async function boot(){
  ac=new (window.AudioContext||window.webkitAudioContext)();
  var how=await addWorklet();
  node=new AudioWorkletNode(ac,'terrain-flanger',{numberOfInputs:1,numberOfOutputs:1,outputChannelCount:[2]});
  wetG=ac.createGain(); dryG=ac.createGain(); outG=ac.createGain(); dryG.gain.value=0; outG.gain.value=0.6;
  node.connect(wetG); wetG.connect(outG); dryG.connect(outG); outG.connect(ac.destination);
  node.port.onmessage=function(e){ var d=e.data||{};
    if(d.types){ CH=(d.chars&&d.chars[0])||[]; fillChars(); return; }
    if(d.lfo!==undefined||d.notch) viz=d; };
  node.port.postMessage({query:'names'});
  PKEY.forEach(function(k,i){ push(k, D.knobs[i].v/100); });
  node.port.postMessage({type:0,character:0});
  document.getElementById('status').textContent='Running (worklet via '+how+'). Try Tape Zero with Depth up.';
}
/* the flanger exposes rate/depth/feedback/mix as real AudioParams; type/character go by port */
function push(k,v){ if(!node)return;
  var ap=node.parameters&&node.parameters.get&&node.parameters.get(k);
  if(ap) ap.setTargetAtTime(v,ac.currentTime,0.012); else node.port.postMessage(Object.fromEntries([[k,v]])); }
function fillChars(){ var s=document.getElementById('csel');
  s.innerHTML=CH.map(function(n,i){return '<option value="'+i+'">'+n+'</option>';}).join('');
  document.getElementById('chname').textContent=CH[chIdx]||'Init';
  s.onchange=function(){ chIdx=+s.value; node.port.postMessage({character:chIdx});
    document.getElementById('chname').textContent=CH[chIdx]; }; }
document.getElementById('tsel').onchange=function(e){
  var i=TYPES.indexOf(e.target.value); D.type=TYPES[i]; chIdx=0;
  document.querySelector('.fxr-tl').firstChild.nodeValue=D.type;
  node.port.postMessage({type:i,character:0}); node.port.postMessage({query:'names'}); };
function makeSrc(){
  if(srcN){ try{srcN.stop&&srcN.stop();}catch(e){} try{srcN.disconnect();}catch(e){} }
  var g=ac.createGain(); g.gain.value=0.3;
  if(kind==='sine'){var o=ac.createOscillator();o.type='sine';o.frequency.value=220;o.connect(g);o.start();srcN=o;}
  else if(kind==='noise'){var n=ac.createBufferSource(),b=ac.createBuffer(1,ac.sampleRate*2,ac.sampleRate),dt=b.getChannelData(0);
    for(var i=0;i<dt.length;i++)dt[i]=(Math.random()*2-1)*0.25; n.buffer=b;n.loop=true;n.connect(g);n.start();srcN=n;}
  else if(kind==='pad'){var m=ac.createGain();m.gain.value=0.15;
    [110,110.6,164.8,220.4].forEach(function(f){var o=ac.createOscillator();o.type='sawtooth';o.frequency.value=f;o.connect(m);o.start();});
    var lp=ac.createBiquadFilter();lp.type='lowpass';lp.frequency.value=1600;m.connect(lp);lp.connect(g);srcN=m;}
  else {var m2=ac.createGain();m2.gain.value=0;m2.connect(g);srcN=m2;
    var o2=ac.createOscillator();o2.type='triangle';o2.frequency.value=330;o2.connect(m2);o2.start();
    (function tick(){ if(!playing||kind!=='pluck')return; var t=ac.currentTime;
      m2.gain.cancelScheduledValues(t);m2.gain.setValueAtTime(0.36,t);
      m2.gain.exponentialRampToValueAtTime(0.001,t+0.55);setTimeout(tick,760);})();}
  g.connect(node); g.connect(dryG);
}
document.getElementById('play').addEventListener('click',async function(){
  var b=this;
  try{ if(!ac){document.getElementById('status').textContent='Starting\\u2026'; await boot();}
    if(ac.state==='suspended') await ac.resume();
    playing=!playing; b.classList.toggle('on',playing); b.textContent=playing?'Stop':'Play';
    if(playing) makeSrc(); else if(srcN){try{srcN.stop&&srcN.stop();}catch(e){}try{srcN.disconnect();}catch(e){}srcN=null;}
  }catch(err){ fail(String(err&&err.stack||err)); } });
document.querySelectorAll('#src button').forEach(function(b){b.addEventListener('click',function(){
  document.querySelectorAll('#src button').forEach(function(x){x.setAttribute('aria-pressed','false');});
  b.setAttribute('aria-pressed','true');kind=b.dataset.s;if(playing)makeSrc();});});

/* ══ THE NULL TRAJECTORIES ══════════════════════════════════════════════════════════════
   A flanger's comb is teeth at odd multiples of 1/(2D). As D sweeps, every tooth slides, and
   the higher ones slide FURTHER — so the family fans out and back like a jet. Drawing them as
   eight thin white lines over ~2 s of history gives that motion as strokes rather than as a
   grey field: same information, in the plugin's own language, and crisp at any zoom because it
   is vector. When the comb leaves the audio band (D -> 0) the lines run off the top together
   and the window flashes — the through-zero null, measured -61.2 dB. ═════════════════════ */
var FMIN=30,FMAX=18000,LMIN=Math.log(FMIN),LSPAN=Math.log(FMAX)-LMIN;
var HIST=[], NT=8, yOff=0, fSm=0;
function frame(){
  var svg=document.getElementById('wf');
  if(svg){
    var r=svg.getBoundingClientRect(), W=Math.max(2,r.width), H=Math.max(2,r.height);
    svg.setAttribute('viewBox','0 0 '+W+' '+H);
    var band=(viz.notch||[]).filter(function(f){return f>20&&f<FMAX;}).sort(function(a,b){return a-b;});
    var target=band.length?band[0]:600;                 // the LOWEST tooth, stably identified
    if(!fSm) fSm=target;
    // one-pole in log space, ~90 ms: continuous between engine frames instead of stair-stepped
    fSm=Math.exp(Math.log(fSm)+(Math.log(target)-Math.log(fSm))*0.10);
    var first=fSm;
    var lvl=Math.max(0,Math.min(1,(viz.lvl||0)*2.2)), wake=0.28+0.72*lvl;
    HIST.push(first); if(HIST.length>120) HIST.shift();
    // where does the family sit right now? centre it, smoothly.
    var sy=0, sn=0;
    for(var q=0;q<NT;q++){ var fq=first*(2*q+1); if(fq<=FMAX){
      sy+=H*(1-(Math.log(Math.max(FMIN,fq))-LMIN)/LSPAN); sn++; } }
    if(sn){ var want=H*0.5-(sy/sn); yOff += (want-yOff)*0.02; }   // slower than the sweep, so it never fights it
    for(var t=0;t<NT;t++){
      var d='', mult=2*t+1;
      for(var i=0;i<HIST.length;i++){
        var f=HIST[i]*mult; if(f>FMAX) { d=''; continue; }
        var x=(i/(HIST.length-1||1))*W;
        var y=H*(1-(Math.log(Math.max(FMIN,f))-LMIN)/LSPAN)+yOff;
        d+=(d?'L':'M')+x.toFixed(1)+' '+y.toFixed(1);
      }
      var el=document.getElementById('t'+t);
      el.setAttribute('d',d);
      el.style.opacity=((0.34+0.60*wake)*Math.pow(0.87,t)).toFixed(3);
    }
    // (fb402 — the white flash is GONE. See the commit: it strobed because its trigger jumped.)
  }
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
</script></body></html>
""" % dict(theme=theme, css=css, worklet=wk, knob=knob,
           types=repr(TYPES).replace("'", '"'),
           icplus=IC_PLUS, icarrow=IC_ARROW, icx=IC_X)

io.open(OUT, 'w', encoding='utf-8').write(page)
print('lifted  %d CSS rules + theme + knobSVG' % css.count('}'))
print('wrote   %s  (%.1f KB)' % (OUT, len(page.encode('utf-8')) / 1024.0))
