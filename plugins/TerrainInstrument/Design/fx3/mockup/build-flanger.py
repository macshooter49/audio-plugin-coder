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
    +'<div class="fxr-core" data-core="flanger"><canvas id="wf"></canvas></div>'
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
document.querySelectorAll('.fxr-r,.fxr-pill').forEach(function(e){e.addEventListener('click',function(){e.classList.toggle('fxr-on');});});

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

/* ══ THE WATERFALL ═══════════════════════════════════════════════════════════════════════
   Time scrolls LEFT, frequency runs bottom-to-top on the same log axis the rest of the plugin
   uses. Each new column is the comb's magnitude right now: bright white where energy passes,
   dark where a null carves. The nulls therefore draw themselves as stripes that BEND as the
   sweep moves -- the jet. At the through-zero crossing the comb leaves the audio band and the
   column goes solid white; it then scrolls away as a scar you can still see. ══════════════ */
var FMIN=30,FMAX=18000,LMIN=Math.log(FMIN),LSPAN=Math.log(FMAX)-LMIN;
function combMag(f,delta,g){ return Math.sqrt(1+g*g-2*g*Math.cos(2*Math.PI*f*delta)); }
function frame(){
  var cv=document.getElementById('wf');
  if(cv){
    var r=cv.getBoundingClientRect(), dpr=Math.min(window.devicePixelRatio||1,3);
    var W=Math.max(2,Math.round(r.width*dpr)), H=Math.max(2,Math.round(r.height*dpr));
    var fresh=(cv.width!==W||cv.height!==H);
    if(fresh){ cv.width=W; cv.height=H; }
    var g=cv.getContext('2d');
    g.globalCompositeOperation='copy'; g.drawImage(cv,-1*dpr,0); g.globalCompositeOperation='source-over';
    var band=(viz.notch||[]).filter(function(f){return f>20&&f<FMAX;});
    var first=band.length?band[0]:600;
    var lvl=Math.max(0,Math.min(1,(viz.lvl||0)*2.2)), wake=0.20+0.80*lvl;
    var delta=1/(2*Math.max(40,first)), gg=0.86;
    var nullness=Math.max(0,Math.min(1,(first-4200)/7000));
    var x=W-1*dpr, colW=Math.max(1,1*dpr);
    for(var y=0;y<H;y++){
      var f=Math.exp(LMIN+(1-y/H)*LSPAN);
      var m=combMag(f,delta,gg)/(1+gg);              // 0..1, 0 at a null
      // gamma it hard: a comb's PEAKS are most of the field, so a linear map reads as a
      // white blob. 2.4 keeps the picture as stripes on dark, which is what the nulls are.
      var a=Math.pow(m,2.4)*wake*0.78;
      a=a*(1-nullness)+nullness*0.92;                 // the crossing whites the whole column
      g.fillStyle='rgba(236,232,242,'+a.toFixed(3)+')';
      g.fillRect(fresh?0:x, y, fresh?W:colW, 1);      // first sizing paints the WHOLE field, so
    }                                                 // it never opens as a half-empty window
    /* a purple tick riding the sweep, so the current notch is findable in the scroll */
    var yt=H*(1-(Math.log(Math.max(FMIN,first))-LMIN)/LSPAN);
    g.fillStyle='rgba(183,148,255,'+(0.5+0.5*wake).toFixed(2)+')'; g.fillRect(x-colW,yt-1*dpr,colW*2,2*dpr);
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
