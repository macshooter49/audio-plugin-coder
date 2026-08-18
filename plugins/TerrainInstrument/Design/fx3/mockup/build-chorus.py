#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fb396 — build the CHORUS mockup out of the SHIPPED card, not out of a lookalike.

fb395 rebuilt the chassis by reading CSS values and retyping an approximation. Max: "that's
not even our buttons." Correct — it had no grip, no ghost-sized type pill, no preset pill, no
route pills, and hand-drawn knobs. This script instead LIFTS from Source/ui/public/index.html:

  * the :root theme variables, verbatim
  * every CSS rule that mentions .fxr- / .dst-curve / mvBreathe, verbatim (107+ rules)
  * knobSVG(), verbatim — the real knob, not a canvas imitation

and renders the card with devHTML()'s exact markup. The page wraps everything in
<div id="syn-panel"> so the shipped #syn-panel-scoped selectors match with zero rewriting.

  python3 build-chorus.py  ->  chorus-mockup.html
"""
import io, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FX3  = os.path.dirname(HERE)
PLUG = os.path.dirname(os.path.dirname(FX3))          # .../TerrainInstrument
IDX  = os.path.join(PLUG, 'Source', 'ui', 'public', 'index.html')
WK   = os.path.join(FX3, 'chorus', 'chorus-worklet.js')
OUT  = os.path.join(HERE, 'chorus-mockup.html')

src = io.open(IDX, encoding='utf-8').read()

# ── 1. the theme variables. They live on #syn-panel, NOT :root — which is exactly why the
#      page wraps the card in <div id="syn-panel">: lift this block and every var resolves.
#      Take ONLY the custom properties out of it: that same rule also carries the panel's own
#      layout (position:absolute, display:flex, inset) which, dragged in here, yanks the card
#      out of flow and collapses the whole page to 0x0. Variables yes, layout no.
theme_src = None
for m in re.finditer(r'#syn-panel\s*\{[^}]*\}', src):
    if '--purple-400' in m.group(0) and '--border-strong' in m.group(0):
        theme_src = m.group(0)
        break
assert theme_src, 'could not find the #syn-panel theme-variable block'
vars_only = re.findall(r'(--[A-Za-z0-9\-]+\s*:\s*[^;]+;)', theme_src)
assert len(vars_only) > 8, 'theme block yielded too few variables'
theme = ':root,#syn-panel{\n  color:var(--text-primary);\n  ' + '\n  '.join(v.strip() for v in vars_only) + '\n}'

# ── 2. every rule that dresses a rack device — taken whole, never retyped.
#      Tokenise by MATCHING BRACES, not by regex. A regex over raw CSS matches ".fxr-core"
#      where it appears inside a /* comment */ and then runs to the next "{", emitting a
#      fragment with a stray brace. That left the sheet 9 braces unbalanced, the parser
#      derailed, and every rule after it — including the page's own layout — was silently
#      dropped. Strip comments first, then walk.
def strip_comments(css):
    return re.sub(r'/\*.*?\*/', '', css, flags=re.S)

def top_level_rules(css):
    out, i, n = [], 0, len(css)
    while i < n:
        j = css.find('{', i)
        if j < 0: break
        sel = css[i:j].strip()
        depth, k = 1, j + 1
        while k < n and depth:
            if css[k] == '{': depth += 1
            elif css[k] == '}': depth -= 1
            k += 1
        if depth == 0 and sel:
            out.append((sel, css[j:k]))
        i = k
    return out

rules = []
for blk in re.findall(r'<style[^>]*>(.*?)</style>', src, re.S):
    for sel, body in top_level_rules(strip_comments(blk)):
        keep = ('.fxr-' in sel) or ('.dst-curve' in sel) or sel.startswith('@keyframes mvBreathe')
        if keep and not sel.startswith('@media'):
            rules.append(sel + body)
assert len(rules) > 60, 'expected the full rack ruleset, got %d' % len(rules)
css_lifted = '\n'.join(rules)
assert css_lifted.count('{') == css_lifted.count('}'), 'lifted CSS is brace-unbalanced'

# ── 3. the REAL knob renderer, lifted whole
k0 = src.index('function knobSVG(pct,label,glyph){')
depth, i = 0, k0
while True:
    if src[i] == '{': depth += 1
    elif src[i] == '}':
        depth -= 1
        if depth == 0: break
    i += 1
knob_fn = src[k0:i+1]
assert 'viewBox="0 0 30 30"' in knob_fn, 'knobSVG did not extract cleanly'

# the header glyphs, also lifted
def grab_var(name):
    m = re.search(r"var\s+%s\s*=\s*('(?:[^'\\]|\\.)*');" % name, src)
    assert m, 'missing ' + name
    return m.group(1)
IC_PLUS, IC_ARROW, IC_X = grab_var('IC_PLUS'), grab_var('IC_ARROW'), grab_var('IC_X')

worklet = io.open(WK, encoding='utf-8').read()
assert '</script' not in worklet.lower()

TYPES = ['Vintage','June','Pedal','Trio','Ensemble','Micro','Wow','Dark']

page = u"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Chorus — mockup</title>
<style>
%(theme)s
*{box-sizing:border-box}
html,body{margin:0;height:100%%}
body{background:radial-gradient(900px 520px at 30%% -10%%,#241d3a 0,transparent 60%%),#141220;
  color:var(--text-primary);font-family:-apple-system,'SF Pro Text',system-ui,sans-serif;
  -webkit-font-smoothing:antialiased;display:flex;flex-direction:column;align-items:center;
  gap:20px;padding:24px 18px 40px}
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
.note{font-size:10px;color:var(--text-muted);max-width:760px;text-align:center;line-height:1.7}
.err{color:#FF8B8B;font-size:11px;max-width:760px;text-align:center;white-space:pre-wrap}
/* the rack strip, at the plugin's real geometry, scaled up to read on a desktop */
/* the card is flex:0 0 272px with height:100%%, so every ancestor must carry a real size or
   the whole strip shrink-to-fits to zero and there is nothing to look at. */
.rackwrap{height:376px;display:flex;justify-content:center;width:100%%}
.zoom{transform:scale(2.0);transform-origin:top center;width:272px;flex:0 0 272px}
#syn-panel{display:block;width:272px}
.rackstrip{display:flex;gap:9px;height:170px;width:272px}
</style></head><body>

<div class="bar">
  <h1>Chorus</h1>
  <div class="seg" id="src">
    <button data-s="sine" aria-pressed="true">Sine</button>
    <button data-s="pad">Pad</button>
    <button data-s="pluck">Pluck</button>
    <button data-s="noise">Noise</button>
  </div>
  <button class="play" id="play">Play</button>
</div>

<div class="rackwrap"><div class="zoom"><div id="syn-panel"><div class="rackstrip" id="rack"></div></div></div></div>
<p class="note" id="status">Press Play. Type and Character are the two header pills; drag a knob vertically.</p>
<p class="err" id="err"></p>

<script id="wk" type="text/worklet">%(worklet)s</script>
<script>
"use strict";
var TYPES=%(types)s;
var IC_PLUS=%(icplus)s, IC_ARROW=%(icarrow)s, IC_X=%(icx)s;
var RLBL=['A','B','C','D','S','N'];
%(knob)s

/* ── the device model, shaped exactly like the rack's own `d` object ── */
var D={ name:'Chorus', core:'chorus', type:TYPES[0], types:TYPES, preset:'Init', on:true, noBack:false,
        pills:[{t:'Sync',on:false},{t:'Wide',on:true}],
        route:[true,true,false,false,false,false],
        knobs:[{l:'Rate',v:35},{l:'Depth',v:60},{l:'Feedback',v:15},{l:'Mix',v:50}] };
var CH=[]; var chIdx=0;

/* devHTML(), structure-for-structure as the plugin emits it */
function devHTML(d){
  var pills=d.pills.map(function(p){return '<span class="fxr-pill'+(p.on?' fxr-on':'')+'"><span class="fxr-t">'+p.t+'</span></span>';}).join('');
  var route=d.route.map(function(on,i){return '<span class="fxr-r'+(on?' fxr-on':'')+'" data-r="'+i+'"><span class="fxr-t">'+RLBL[i]+'</span></span>';}).join('');
  var knobs=d.knobs.map(function(k,i){return '<div class="fxr-knob"><div class="fxr-dial" data-k="'+i+'">'+knobSVG(k.v,null,null)+'</div><span class="fxr-lab">'+k.l+'</span></div>';}).join('');
  return '<div class="fxr-dev fxr-sel'+(d.on?'':' fxr-off')+'" data-dev="0">'
    +'<div class="fxr-head">'
      +'<span class="fxr-grip">\\u22ee\\u22ee</span><span class="fxr-name">'+d.name+'</span>'
      +'<span class="fxr-type"><span class="fxr-tw"><span class="fxr-tl">'+d.type+'<span class="fxr-car">\\u25be</span></span>'
        +d.types.map(function(t){return '<span class="fxr-tg">'+t+'<span class="fxr-car">\\u25be</span></span>';}).join('')
        +'</span><select class="fxr-type-native" id="tsel">'+d.types.map(function(t){return '<option'+(t===d.type?' selected':'')+'>'+t+'</option>';}).join('')+'</select></span>'
      +'<span class="fxr-preset" data-act="preset"><span class="fxr-star">\\u2726</span><span class="fxr-pname" id="chname">'+(CH[0]||'Init')+'</span><span class="fxr-car">\\u25be</span><select class="fxr-type-native" id="csel"></select></span>'
      +'<span class="fxr-spacer"></span>'
      +'<span class="fxr-headr"><span class="fxr-swap" data-act="swap" title="More parameters">'+IC_PLUS+IC_ARROW+'</span><span class="fxr-pwr" data-act="pwr"></span><span class="fxr-x" data-act="x" title="Remove">'+IC_X+'</span></span>'
    +'</div>'
    +'<div class="fxr-core" data-core="chorus">'
      +'<svg id="cs" preserveAspectRatio="none"><path class="dst-curve" id="cline"/>'
      +'<path class="dst-curve" id="cv0"/>'
      +'<path class="dst-curve" id="cv1"/></svg>'
    +'</div>'
    +'<div class="fxr-ctrls">'
      +'<div class="fxr-knobs">'+knobs+'</div>'
      +'<div class="fxr-divider"></div>'
      +'<div class="fxr-rightcol"><div class="fxr-pills">'+pills+'</div><div class="fxr-route">'+route+'</div></div>'
    +'</div></div>';
}
document.getElementById('rack').innerHTML=devHTML(D);

/* ── knob drag: the rack's own ns-resize gesture ── */
document.querySelectorAll('.fxr-dial').forEach(function(dial){
  var i=+dial.getAttribute('data-k'), drag=false, y0=0, v0=0;
  dial.addEventListener('pointerdown',function(e){drag=true;y0=e.clientY;v0=D.knobs[i].v;dial.setPointerCapture(e.pointerId);e.preventDefault();});
  dial.addEventListener('pointermove',function(e){ if(!drag)return;
    var v=Math.max(0,Math.min(100, v0+(y0-e.clientY)*0.6));
    D.knobs[i].v=v; dial.innerHTML=knobSVG(v,null,null);
    push(['rate','depth','feedback','mix'][i], v/100); });
  dial.addEventListener('pointerup',function(){drag=false;});
});
document.querySelector('.fxr-pwr').addEventListener('click',function(){
  D.on=!D.on; document.querySelector('.fxr-dev').classList.toggle('fxr-off',!D.on);
  if(wetG){ var t=ac.currentTime; wetG.gain.setTargetAtTime(D.on?1:0,t,0.02); dryG.gain.setTargetAtTime(D.on?0:1,t,0.02); }
});
document.querySelectorAll('.fxr-r').forEach(function(r){ r.addEventListener('click',function(){ r.classList.toggle('fxr-on'); }); });
document.querySelectorAll('.fxr-pill').forEach(function(p){ p.addEventListener('click',function(){ p.classList.toggle('fxr-on'); }); });

/* ── audio ── */
var ac=null,node=null,wetG=null,dryG=null,outG=null,srcN=null,playing=false,kind='sine',viz={lfo:0,lvl:0,notch:[]},hist=[];
function fail(m){ document.getElementById('err').textContent=m; document.getElementById('status').textContent='Failed to start.'; }
async function addWorklet(){
  var code=document.getElementById('wk').textContent;
  // Safari refuses blob: worklets on file:// in some builds — fb396: this is why the fb395
  // mockup sat on "Loading the three engines" forever. Try blob, then data:, then report.
  try{ await ac.audioWorklet.addModule(URL.createObjectURL(new Blob([code],{type:'text/javascript'}))); return 'blob'; }catch(e1){
    try{ await ac.audioWorklet.addModule('data:text/javascript;base64,'+btoa(unescape(encodeURIComponent(code)))); return 'data'; }
    catch(e2){ throw new Error('addModule failed.\\nblob: '+e1.message+'\\ndata: '+e2.message); }
  }
}
async function boot(){
  ac=new (window.AudioContext||window.webkitAudioContext)();
  var how=await addWorklet();
  node=new AudioWorkletNode(ac,'terrain-chorus',{numberOfInputs:1,numberOfOutputs:1,outputChannelCount:[2]});
  wetG=ac.createGain(); dryG=ac.createGain(); outG=ac.createGain();
  dryG.gain.value=0; outG.gain.value=0.6;
  node.connect(wetG); wetG.connect(outG); dryG.connect(outG); outG.connect(ac.destination);
  node.port.onmessage=function(e){ var d=e.data||{};
    if(d.roster){ CH=(d.roster.CHAR_NAMES&&d.roster.CHAR_NAMES[0])||[]; fillChars(); return; }
    var v=d.viz||d; if(v&&(v.notch||v.lfo!==undefined)) viz=v; };
  node.port.postMessage({query:'roster'});
  ['rate','depth','feedback','mix'].forEach(function(k,i){ push(k, D.knobs[i].v/100); });
  node.port.postMessage({type:0,character:0});
  document.getElementById('status').textContent='Running (worklet via '+how+'). Drag a knob; switch Type.';
}
function push(k,v){ if(node) node.port.postMessage(Object.fromEntries([[k,v]])); }
function fillChars(){
  var s=document.getElementById('csel');
  s.innerHTML=CH.map(function(n,i){return '<option value="'+i+'">'+n+'</option>';}).join('');
  document.getElementById('chname').textContent=CH[chIdx]||'Init';
  s.onchange=function(){ chIdx=+s.value; node.port.postMessage({character:chIdx});
    document.getElementById('chname').textContent=CH[chIdx]; };
}
document.getElementById('tsel').onchange=function(e){
  var i=TYPES.indexOf(e.target.value); D.type=TYPES[i]; chIdx=0;
  document.querySelector('.fxr-tl').firstChild.nodeValue=D.type;
  node.port.postMessage({type:i,character:0});
  node.port.postMessage({query:'roster'});
};
function makeSrc(){
  if(srcN){ try{srcN.stop&&srcN.stop();}catch(e){} try{srcN.disconnect();}catch(e){} }
  var g=ac.createGain(); g.gain.value=0.3;
  if(kind==='sine'){ var o=ac.createOscillator(); o.type='sine'; o.frequency.value=220; o.connect(g); o.start(); srcN=o; }
  else if(kind==='noise'){ var n=ac.createBufferSource(),b=ac.createBuffer(1,ac.sampleRate*2,ac.sampleRate),dt=b.getChannelData(0);
    for(var i=0;i<dt.length;i++)dt[i]=(Math.random()*2-1)*0.25; n.buffer=b; n.loop=true; n.connect(g); n.start(); srcN=n; }
  else if(kind==='pad'){ var m=ac.createGain(); m.gain.value=0.15;
    [110,110.6,164.8,220.4].forEach(function(f){var o=ac.createOscillator();o.type='sawtooth';o.frequency.value=f;o.connect(m);o.start();});
    var lp=ac.createBiquadFilter(); lp.type='lowpass'; lp.frequency.value=1600; m.connect(lp); lp.connect(g); srcN=m; }
  else { var m2=ac.createGain(); m2.gain.value=0; m2.connect(g); srcN=m2;
    var o2=ac.createOscillator(); o2.type='triangle'; o2.frequency.value=330; o2.connect(m2); o2.start();
    (function tick(){ if(!playing||kind!=='pluck')return; var t=ac.currentTime;
      m2.gain.cancelScheduledValues(t); m2.gain.setValueAtTime(0.36,t);
      m2.gain.exponentialRampToValueAtTime(0.001,t+0.55); setTimeout(tick,760); })(); }
  g.connect(node); g.connect(dryG);
}
document.getElementById('play').addEventListener('click',async function(){
  var b=this;
  try{
    if(!ac){ document.getElementById('status').textContent='Starting\\u2026'; await boot(); }
    if(ac.state==='suspended') await ac.resume();
    playing=!playing; b.classList.toggle('on',playing); b.textContent=playing?'Stop':'Play';
    if(playing) makeSrc();
    else if(srcN){ try{srcN.stop&&srcN.stop();}catch(e){} try{srcN.disconnect();}catch(e){} srcN=null; }
  }catch(err){ fail(String(err&&err.stack||err)); }
});
document.querySelectorAll('#src button').forEach(function(b){ b.addEventListener('click',function(){
  document.querySelectorAll('#src button').forEach(function(x){x.setAttribute('aria-pressed','false');});
  b.setAttribute('aria-pressed','true'); kind=b.dataset.s; if(playing) makeSrc(); }); });

/* ── the core: the SAME white line the distortion and filter draw (.dst-curve), plus the
      voice trails. Drawn as SVG paths in the shipped .fxr-core, not a foreign canvas. ── */
var FMIN=30,FMAX=18000,LMIN=Math.log(FMIN),LSPAN=Math.log(FMAX)-LMIN;
function combDb(f,delta,g){ return 20*Math.log10(Math.max(1e-4,Math.sqrt(1+g*g-2*g*Math.cos(2*Math.PI*f*delta)))); }
function frame(){
  var svg=document.getElementById('cs'); if(svg){
    var r=svg.getBoundingClientRect(), W=Math.max(2,r.width), H=Math.max(2,r.height);
    svg.setAttribute('viewBox','0 0 '+W+' '+H);
    var voices=(viz.notch||[]).filter(function(f){return f>20&&f<FMAX;});
    if(!voices.length) voices=[520,880];
    var lvl=Math.max(0,Math.min(1,(viz.lvl||0)*2.2)), wake=0.26+0.74*lvl;
    var deltas=voices.map(function(v){return 1/(2*Math.max(30,v));}), gg=0.42;
    var dres=Math.min.apply(null,deltas), d='';
    for(var px=0;px<=W;px+=1){
      var f=Math.exp(LMIN+(px/W)*LSPAN), db;
      var ppc=(Math.log(f*(1+1/(f*dres*2)))-Math.log(f))/LSPAN*W;   /* pixels per comb cycle */
      var blend=Math.max(0,Math.min(1,(ppc-2.2)/3.0));              /* smooth, never a cliff */
      var s=0; for(var i=0;i<deltas.length;i++) s+=combDb(f,deltas[i],gg);
      var avg=20*Math.log10(Math.sqrt(1+gg*gg))-0.4;
      db=avg+(s/deltas.length-avg)*blend;
      var fr=Math.max(0,Math.min(1,(db+16)/22));
      d+=(px?'L':'M')+px.toFixed(1)+' '+(H*0.50-(H*0.40)*fr*(0.45+0.55*wake)).toFixed(1);
    }
    document.getElementById('cline').setAttribute('d',d);
    hist.push(voices.slice(0,2)); if(hist.length>110) hist.shift();
    [0,1].forEach(function(v){
      var lo=1e9,hi=-1e9; hist.forEach(function(row){var f=row[v]; if(f){lo=Math.min(lo,f);hi=Math.max(hi,f);}});
      if(hi<=lo){lo=(voices[v]||500)*0.85;hi=(voices[v]||500)*1.2;}
      var p='';
      hist.forEach(function(row,i){ var f=row[v]; if(!f)return;
        var x=(i/(hist.length-1||1))*W, y=H*0.60+H*0.34*(1-Math.log(f/lo)/Math.log(hi/lo));
        p+=(p?'L':'M')+x.toFixed(1)+' '+y.toFixed(1); });
      var el=document.getElementById('cv'+v); if(el){ el.setAttribute('d',p); el.setAttribute('opacity',(0.55+0.45*wake)*(v?0.82:1)); }
    });
  }
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
</script></body></html>
""" % dict(theme=theme, css=css_lifted, worklet=worklet, knob=knob_fn,
           types=repr(TYPES).replace("'", '"'),
           icplus=IC_PLUS, icarrow=IC_ARROW, icx=IC_X)

io.open(OUT, 'w', encoding='utf-8').write(page)
print('lifted  %d .fxr- CSS rules + :root theme + knobSVG (%d chars)' % (len(rules), len(knob_fn)))
print('wrote   %s  (%.1f KB)' % (OUT, len(page.encode('utf-8')) / 1024.0))
