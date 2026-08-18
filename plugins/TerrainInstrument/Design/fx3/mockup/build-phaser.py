#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fb408 — the PHASER mockup. Same shipped card (via fx3lift), a third distinct window.

THE CONSTRAINT THAT SHAPES IT: a phaser is ALL-PASS. Its wet magnitude spectrum is FLAT, so a
window drawn from the wet leg would show a straight line while the effect is screaming. The
notches only exist in the DRY+WET SUM. This plugin has paid for that confusion once already
(fb282: an allpass change measured "102% divergence" and was completely inaudible). So the
analyser sits on the DEVICE OUTPUT, after the internal mix — what you hear is what is drawn.

The window is ONE thin white curve on the log-f axis: the phaser's actual output spectrum, with
its notches showing as dips that slide. Distinct from the chorus (a MODELLED response plus voice
trails) and from the flanger (a MIRRORED ribbon), and it carries the phaser's own tell — the
notches are FINITE and COUNTABLE and NON-harmonically spaced, so the count is read off the
curve's own minima and printed. Ninety reads 2, Twelve reads 6, Vibe's are visibly uneven.

Everything the flanger arc taught is applied from the start:
  * driven by AUDIO, never by a control-rate scalar (fb402 — a discontinuous scalar makes the
    whole picture jump, and no amount of smoothing fixes the shape);
  * NOTHING writes a property per frame that a class also animates (fb401/402 — that fight is
    what strobed purple);
  * centred on the CARD, not the core, with the same optical nudge (fb404-407);
  * edge to edge (fb401); vector SVG, never a raster canvas in a scaled card (fb400).

  python3 build-phaser.py  ->  phaser-mockup.html
"""
import io, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fx3lift as L

OUT = os.path.join(L.HERE, 'phaser-mockup.html')
src = L._src()
theme, css, knob = L.theme_vars(src), L.card_css(src), L.knob_svg(src)
IC_PLUS, IC_ARROW, IC_X = L.glyphs(src)
wk = L.worklet('phaser')

TYPES = ['Ninety', 'Stone', 'Duo', 'Twelve', 'Kraut', 'Vibe', 'Barber', 'Envy', 'Steps']

page = u"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Phaser — mockup</title>
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
#syn-panel .fxr-core{margin:0}                        /* edge to edge (fb401) */
#syn-panel .fxr-core .ph-t{animation:none !important} /* we never write opacity, but never rely on that */
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
.note{font-size:10px;color:var(--text-muted);max-width:800px;text-align:center;line-height:1.7}
.err{color:#FF8B8B;font-size:11px;max-width:800px;text-align:center;white-space:pre-wrap}
.rackwrap{height:376px;display:flex;justify-content:center;width:100%%}
.zoom{transform:scale(2.0);transform-origin:top center;width:272px;flex:0 0 272px}
#syn-panel{display:block;width:272px}
.rackstrip{display:flex;gap:9px;height:170px;width:272px}
</style></head><body>

<div class="bar">
  <h1>Phaser</h1>
  <div class="seg" id="src">
    <button data-s="sine">Sine</button>
    <button data-s="pad">Pad</button>
    <button data-s="pluck">Pluck</button>
    <button data-s="noise" aria-pressed="true">Noise</button>
  </div>
  <button class="play" id="play">Play</button>
</div>

<div class="rackwrap"><div class="zoom"><div id="syn-panel"><div class="rackstrip" id="rack"></div></div></div></div>
<p class="note" id="status">Press Play. Switch Type and <b>count the notches</b> — Ninety has 2, Twelve has 6, Vibe's are visibly uneven.</p>
<p class="err" id="err"></p>

<script id="wk" type="text/worklet">%(worklet)s</script>
<script>
"use strict";
var TYPES=%(types)s;
var IC_PLUS=%(icplus)s, IC_ARROW=%(icarrow)s, IC_X=%(icx)s;
var RLBL=['A','B','C','D','S','N'];
%(knob)s

var D={ name:'Phaser', type:TYPES[0], types:TYPES, on:true,
        pills:[{t:'Sync',on:false},{t:'Invert',on:false}],
        route:[true,true,false,false,false,false],
        knobs:[{l:'Rate',v:35},{l:'Depth',v:60},{l:'Feedback',v:45},{l:'Mix',v:50}] };
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
    +'<div class="fxr-core" data-core="phaser"><svg id="pv" preserveAspectRatio="none">'
      +'<path id="notches" fill="none" stroke="var(--purple-400)" stroke-width="1" opacity="0.42"/>'
      +'<path class="dst-curve ph-t" id="spec"/>'
      +'</svg><div id="cnt" style="position:absolute;left:0;bottom:0;font-size:7.5px;letter-spacing:.8px;'
      +'color:var(--text-muted);text-transform:uppercase;pointer-events:none">—</div></div>'
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
  if(i===0) node.port.postMessage({tempoSync:on, bpm:120});        // Sync — real
  else push('feedback', on ? 1-(D.knobs[2].v/100) : D.knobs[2].v/100);   // Invert — flips notch geography
});});

var ANA=null,BINS=null,SM=null,ac=null,node=null,wetG=null,dryG=null,outG=null,srcN=null,
    playing=false,kind='noise';
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
  node=new AudioWorkletNode(ac,'terrain-phaser',{numberOfInputs:1,numberOfOutputs:1,outputChannelCount:[2]});
  wetG=ac.createGain(); dryG=ac.createGain(); outG=ac.createGain(); dryG.gain.value=0; outG.gain.value=0.6;
  node.connect(wetG); wetG.connect(outG); dryG.connect(outG); outG.connect(ac.destination);
  // ON THE OUTPUT, after the engine's internal mix — an all-pass leg alone would draw a flat line
  ANA=ac.createAnalyser(); ANA.fftSize=4096; ANA.smoothingTimeConstant=0.86;
  outG.connect(ANA); BINS=new Float32Array(ANA.frequencyBinCount);
  node.port.onmessage=function(e){ var d=e.data||{}; if(d.types){ CH=(d.chars&&d.chars[0])||[]; fillChars(); } };
  node.port.postMessage({query:'names'});
  PKEY.forEach(function(k,i){ push(k, D.knobs[i].v/100); });
  node.port.postMessage({type:0,character:0});
  document.getElementById('status').textContent='Running (worklet via '+how+'). Switch Type and count the notches.';
}
function push(k,v){ if(node) node.port.postMessage(Object.fromEntries([[k,v]])); }
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

/* ══ THE CURVE ═══════════════════════════════════════════════════════════════════════════════
   One thin white house line: the phaser's OUTPUT spectrum on the log-f axis. Its notches are
   the dips, and they slide. Under them, the notches the curve ITSELF shows — found as local
   minima of the smoothed spectrum, never read from a control-rate array, so they can neither
   jump nor disappear between engine frames. The count is the phaser's tell: finite, countable,
   and unevenly spaced in a way no other device in the rack produces. ════════════════════════ */
var FMIN=30,FMAX=18000,LMIN=Math.log(FMIN),LSPAN=Math.log(FMAX)-LMIN;
function frame(){
  var svg=document.getElementById('pv');
  if(svg){
    var r=svg.getBoundingClientRect(), W=Math.max(2,r.width), H=Math.max(2,r.height);
    svg.setAttribute('viewBox','0 0 '+W+' '+H);
    // centre on the CARD, with the same optical nudge the flanger settled on (fb404-407)
    var card=svg.closest('.fxr-dev'), mid=H*0.5;
    if(card){ var cr=card.getBoundingClientRect(), NUDGE=6.0, zoom=cr.width/272;
      mid=Math.max(H*0.20, Math.min(H*0.80, (cr.top+cr.height*0.5)-r.top - NUDGE*zoom)); }
    var N=Math.max(2,Math.round(W/2)), d='', dips='', count=0;
    if(ANA&&BINS){
      ANA.getFloatFrequencyData(BINS);
      if(!SM||SM.length!==N) SM=new Float32Array(N).fill(-100);
      var nyq=ac.sampleRate*0.5, nb=BINS.length;
      for(var i=0;i<N;i++){
        var f=Math.exp(LMIN+(i/(N-1))*LSPAN);
        var b2=Math.min(nb-1,Math.max(0,Math.round(f/nyq*nb)));
        var db=BINS[b2]; if(!isFinite(db)) db=-100;
        SM[i]+=(db-SM[i])*0.07;   // heavy: hunting a 6 dB dip under a noisy floor
      }
      // DEVIATION from the running mean, not absolute level. A phaser's notch is only ~6 dB
      // deep; against a 66 dB scale that is three pixels, and the source's own spectral tilt
      // dominates. Subtracting the mean removes the source and expands the comb — which is the
      // only thing this device actually does to the sound.
      var mean=0; for(var mi=0;mi<N;mi++) mean+=SM[mi]; mean/=N;
      for(var j=0;j<N;j++){
        var x=(j/(N-1))*W;
        var dev=Math.max(-14,Math.min(6,SM[j]-mean));
        d+=(j?'L':'M')+x.toFixed(1)+' '+(mid-(dev/20)*(H*0.62)).toFixed(1);
      }
      // count what the CURVE shows: 5 dB below BOTH shoulders and well spaced, so noise ripple
      // cannot masquerade as a notch (it reported "6" on Ninety, which has 2).
      for(var k=6;k<N-6;k++){
        if(SM[k]<SM[k-6]-5 && SM[k]<SM[k+6]-5 && SM[k]<=SM[k-1] && SM[k]<=SM[k+1]){
          var xk=(k/(N-1))*W;
          dips+='M'+xk.toFixed(1)+' '+(mid+H*0.30).toFixed(1)+'L'+xk.toFixed(1)+' '+(mid+H*0.38).toFixed(1);
          count++; k+=Math.round(N*0.05);
        }
      }
    } else { d='M0 '+mid.toFixed(1)+'L'+W.toFixed(1)+' '+mid.toFixed(1); }
    document.getElementById('spec').setAttribute('d',d);
    document.getElementById('notches').setAttribute('d',dips);
    document.getElementById('cnt').textContent=count?(count+' notches'):'—';
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
