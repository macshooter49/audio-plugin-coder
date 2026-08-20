#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fb428 — build the FOUR fx4 mockups out of the SHIPPED card, not out of a lookalike.

fb395 rebuilt the chassis by reading CSS values and retyping an approximation. Max: "that's
not even our buttons." Correct — it had no grip, no ghost-sized type pill, no preset pill, no
route pills, and hand-drawn knobs. This script instead LIFTS from Source/ui/public/index.html:

  * the :root theme variables, verbatim
  * every CSS rule that mentions .fxr- / .dst-curve / mvBreathe, verbatim (107+ rules)
  * knobSVG(), verbatim — the real knob, not a canvas imitation

and renders the card with devHTML()'s exact markup. The page wraps everything in
<div id="syn-panel"> so the shipped #syn-panel-scoped selectors match with zero rewriting.

  python3 build-fx4.py  ->  eq- / widen- / compress- / ott-mockup.html
"""
import io, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FX4  = os.path.dirname(HERE)
PLUG = os.path.dirname(os.path.dirname(FX4))          # .../TerrainInstrument
IDX  = os.path.join(PLUG, 'Source', 'ui', 'public', 'index.html')

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


# ═══════════════════════════════════════════════════════════════════════════════
#  THE FOUR DEVICES. Every string below is the one the ENGINE publishes — Types,
#  Characters, knob labels and the second dropdown all come from the headers
#  (frontNames/backNames/dropdownNames/charNames), which is why fb423 made the
#  engine the single source of truth. If a label here disagrees with the card in
#  the plugin, the card is wrong, not this file.
# ═══════════════════════════════════════════════════════════════════════════════
DEVICES = [
 dict(key='eq', dev='Equalizer', core='eqz', proc='terrain-eq',
      wk=os.path.join(FX4, 'eq', 'eq-worklet.js'),
      types=['Surgical','British','American','Passive','Open','Dynamic','Chisel'],
      chars=['Plain','Tight','Broad','Steep','Scoop','Deep Pivot','Bright Pivot','Four Bells'],
      d2k='Focus', d2=['Stereo','Mid','Side','Left','Right'],
      knobs=[('Slant',50),('Air',50),('Amount',50),('Mix',100)],
      keys=['f1','f2','f3','mix'],
      back=[('Low Hz',50),('Low',50),('Body Hz',50),('Body',50),
            ('Bite Hz',50),('Bite',50),('Reach',50),('Trait',50)],
      pills=[],
      # the curve IS the device — 96 log bins straight off the engine
      note='The back-8 ARE the curve nodes. Drag a node, a back knob moves.'),
 dict(key='widen', dev='Widen', core='wid', proc='terrain-widen',
      wk=os.path.join(FX4, 'widen', 'widen-worklet.js'),
      types=['Throng','Twin','Steady','Twofold','Blur','Bands'],
      chars=['JP Classic','Even Fan','Analog Drift','Tight Fan','Wide Fan','Octave Bloom','Sub Anchor','Three Phase'],
      d2k='Field', d2=['Straight','Alternate','Orbit','Swap','Side Only','Gather'],
      knobs=[('Amount',35),('Width',50),('Rate',35),('Mix',50)],
      keys=['amount','width','rate','mix'],
      back=[('Voices',50),('Spread',85),('Offset',50),('Roam',0),
            ('Low Keep',0),('Tone',50),('Feedback',0),('Balance',50)],
      pills=[('Retrig',0),('Mono',0)],
      note='Width 50 is EXACTLY neutral. Amount relabels per Type.'),
 dict(key='compress', dev='Compress', core='cmp', proc='terrain-compress',
      wk=os.path.join(FX4, 'dynamics', 'compress-worklet.js'),
      types=['Exact','Bus','FET 76','Opto','Vari-Mu','OverEasy','Ride','Limit'],
      chars=['Precise','Soft Touch','Loose Grip','Blunt','Deep Release','Line Attack','Poise','Judder'],
      d2k='Detect', d2=['Native','Peak','Average','Patient','Spike'],
      knobs=[('Push',20),('Ratio',50),('Lift',25),('Mix',100)],
      keys=['push','ratio','lift','mix'],
      back=[('Attack',61),('Release',63),('Round',25),('Hear Cut',0),
            ('Edge',50),('Cling',0),('Tie',100),('Burn',0)],
      pills=[('Auto',0)],
      note='Ratio 100 WALLS — the feedback Types cross to feedforward in the last 10%.'),
 dict(key='ott', dev='OTT', core='ott', proc='terrain-ott',
      wk=os.path.join(FX4, 'dynamics', 'ott-worklet.js'),
      types=['Over Top','Gentle','Heavy','Sheen','Bass Safe','Surge','Two Band','Stagger'],
      chars=['Straight Up','Sharp Ears','Long Ears','Wide Corner','One Detector','Slow Low','Twice Deep','Full Crest'],
      d2k='Stereo', d2=['Linked','Free Pair','Mid-Side'],
      knobs=[('Amount',50),('Chase',50),('Top Lift',25),('Mix',100)],
      keys=['amount','speed','topLift','mix'],
      back=[('Low Cross',47),('High Cross',44),('Raise',67),('Press',67),
            ('Grip',50),('Bass',50),('Mids',50),('Treble',50)],
      pills=[('Crest',0)],
      note='Amount 0 is UNITY — three bands, each with an upward AND a downward computer.'),
]

PAGE = u"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>%(dev)s — mockup</title>
<style>
%(theme)s
*{box-sizing:border-box}
html,body{margin:0;height:100%%}
body{background:radial-gradient(900px 520px at 30%% -10%%,#241d3a 0,transparent 60%%),#141220;
  color:var(--text-primary);font-family:-apple-system,'SF Pro Text',system-ui,sans-serif;
  -webkit-font-smoothing:antialiased;display:flex;flex-direction:column;align-items:center;
  gap:14px;padding:26px 18px 40px}
h1{font:600 15px/1.2 -apple-system,system-ui;letter-spacing:.02em;margin:0;color:#e8e4f5}
.sub{font:400 11.5px/1.5 -apple-system,system-ui;color:#9a92b8;max-width:660px;text-align:center;margin:0}
.bar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;justify-content:center}
button{background:#241f38;color:#e8e4f5;border:1px solid #3a3356;border-radius:7px;
  padding:7px 13px;font:500 11.5px -apple-system,system-ui;cursor:pointer}
button:hover{background:#2e2846}
#status{font:400 11px -apple-system,system-ui;color:#8f88ab;min-height:15px}
#err{font:400 11px/1.45 ui-monospace,Menlo,monospace;color:#ff9a9a;white-space:pre-wrap;max-width:680px}
/* THE REAL CARD IS 272 x 170. Lifting the CSS is not enough: `.fxr-dev` is
   `flex:0 0 272px; height:100%%`, so with no sized parent the core collapses to nothing and the
   knob row stretches — which is exactly what the first render showed. These two rules are the
   fx3 mockup's own `.rackwrap`/`.rackstrip`, reused. */
#wrap{display:flex;flex-direction:column;align-items:center;gap:16px}
#rack{display:flex;gap:9px;height:170px;width:272px}
#backpanel{width:272px}
%(css)s
/* the mockup's own scaffolding — NOT part of the lifted rack sheet */
.fxr-dev{position:relative}
#backpanel{display:none;border:1px solid var(--border-strong,#3a3356);border-radius:10px;
  background:rgba(24,20,38,.72);padding:12px 14px}
#backpanel.on{display:block}
.bp-row{display:flex;gap:14px;align-items:flex-end;margin-bottom:12px}
.bp-sel{display:flex;flex-direction:column;gap:4px}
.bp-sel label{font:500 9.5px -apple-system,system-ui;letter-spacing:.08em;text-transform:uppercase;color:#8f88ab}
.bp-sel select{background:#1c1830;color:#e8e4f5;border:1px solid #3a3356;border-radius:6px;
  padding:4px 6px;font:400 10.5px -apple-system,system-ui;width:118px}
.bp-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px 0;position:relative}
.bp-k{display:flex;flex-direction:column;align-items:center;gap:5px}
.bp-k .fxr-lab{font:500 9.5px -apple-system,system-ui;color:#9a92b8}
.bp-sep{position:absolute;top:2%%;bottom:2%%;width:1px;background:linear-gradient(180deg,transparent,#3a3356 18%%,#3a3356 82%%,transparent)}
</style></head><body>
<h1>%(dev)s</h1>
<p class="sub">%(note)s<br>Real card, real engine — the worklet is the same algorithm the C++ runs.
Press <b>Start</b>, then <b>Play</b>. Drag knobs vertically. <b>+</b> opens the back panel.</p>
<div class="bar">
  <button id="go">Start audio</button>
  <button id="play">Play</button>
  <button id="src">Source: Saw chord</button>
</div>
<div id="status">Idle.</div><div id="err"></div>
<div id="wrap"><div id="syn-panel"><div id="rack"></div><div id="backpanel" class="on"></div></div></div>

<script id="wk" type="text/plain">%(worklet)s</script>
<script>
"use strict";
var TYPES=%(types)s, CHARS=%(chars)s, D2=%(d2)s, D2K=%(d2k)s;
var BACK=%(back)s, KEYS=%(keys)s;
var IC_PLUS=%(icplus)s, IC_ARROW=%(icarrow)s, IC_X=%(icx)s;
var RLBL=['A','B','C','D','S','N'];
%(knob)s

var D={ name:'%(dev)s', core:'%(core)s', type:TYPES[0], types:TYPES, preset:'Init', on:true,
        pills:%(pills)s, route:[true,false,false,false,false,false],
        knobs:%(knobs)s };
var BV=BACK.map(function(b){return b[1];});

/* devHTML() — structure-for-structure as the plugin emits it (lifted markup, not retyped) */
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
      +'<span class="fxr-preset" data-act="preset"><span class="fxr-star">\\u2726</span><span class="fxr-pname">Init</span><span class="fxr-car">\\u25be</span></span>'
      +'<span class="fxr-spacer"></span>'
      +'<span class="fxr-headr"><span class="fxr-swap" data-act="swap" title="More parameters">'+IC_PLUS+IC_ARROW+'</span><span class="fxr-pwr" data-act="pwr"></span><span class="fxr-x" data-act="x" title="Remove">'+IC_X+'</span></span>'
    +'</div>'
    +'<div class="fxr-core" data-core="'+d.core+'">'
      +'<svg id="cs" preserveAspectRatio="none">'
      +'<path class="dst-curve" id="cline"/><path class="dst-curve" id="cv0"/><path class="dst-curve" id="cv1"/></svg>'
    +'</div>'
    +'<div class="fxr-ctrls">'
      +'<div class="fxr-knobs">'+knobs+'</div>'
      +'<div class="fxr-divider"></div>'
      +'<div class="fxr-rightcol"><div class="fxr-pills">'+pills+'</div><div class="fxr-route">'+route+'</div></div>'
    +'</div></div>';
}
document.getElementById('rack').innerHTML=devHTML(D);

/* the OFFICIAL back panel (fb275): 2 dropdowns + 8 knobs 4x2 + 3 column separators */
function backHTML(){
  var ks=BACK.map(function(b,i){return '<div class="bp-k"><div class="fxr-dial" data-b="'+i+'">'+knobSVG(b[1],null,null)+'</div><span class="fxr-lab">'+b[0]+'</span></div>';}).join('');
  var seps=''; for(var s=1;s<4;s++) seps+='<div class="bp-sep" style="left:'+(s*25)+'%%"></div>';
  return '<div class="bp-row">'
    +'<div class="bp-sel"><label>Character</label><select id="csel">'+CHARS.map(function(c){return '<option>'+c+'</option>';}).join('')+'</select></div>'
    +'<div class="bp-sel"><label>'+D2K+'</label><select id="dsel">'+D2.map(function(c){return '<option>'+c+'</option>';}).join('')+'</select></div>'
    +'</div><div class="bp-grid">'+seps+ks+'</div>';
}
document.getElementById('backpanel').innerHTML=backHTML();
document.querySelector('.fxr-swap').addEventListener('click',function(){
  document.getElementById('backpanel').classList.toggle('on'); });

/* ── knob drag: the rack's own ns-resize gesture ── */
function bindDial(dial,get,set){
  var drag=false,y0=0,v0=0;
  dial.addEventListener('pointerdown',function(e){drag=true;y0=e.clientY;v0=get();dial.setPointerCapture(e.pointerId);e.preventDefault();});
  dial.addEventListener('pointermove',function(e){ if(!drag)return;
    var v=Math.max(0,Math.min(100,v0+(y0-e.clientY)*0.6)); set(v); dial.innerHTML=knobSVG(v,null,null); });
  dial.addEventListener('pointerup',function(){drag=false;});
}
document.querySelectorAll('.fxr-dial[data-k]').forEach(function(d){ var i=+d.getAttribute('data-k');
  bindDial(d,function(){return D.knobs[i].v;},function(v){D.knobs[i].v=v; push(KEYS[i],v/100);}); });
document.querySelectorAll('.fxr-dial[data-b]').forEach(function(d){ var i=+d.getAttribute('data-b');
  bindDial(d,function(){return BV[i];},function(v){BV[i]=v; push('b'+(i+1),v/100);}); });
document.getElementById('tsel').addEventListener('change',function(e){
  var i=e.target.selectedIndex; D.type=TYPES[i];
  document.querySelector('.fxr-tl').firstChild.nodeValue=D.type; push('type',i,true); });
document.getElementById('csel').addEventListener('change',function(e){ push('character',e.target.selectedIndex,true); });
document.getElementById('dsel').addEventListener('change',function(e){ push('axis',e.target.selectedIndex,true); });
document.querySelector('.fxr-pwr').addEventListener('click',function(){
  D.on=!D.on; document.querySelector('.fxr-dev').classList.toggle('fxr-off',!D.on);
  if(wetG){var t=ac.currentTime; wetG.gain.setTargetAtTime(D.on?1:0,t,0.02); dryG.gain.setTargetAtTime(D.on?0:1,t,0.02);} });
document.querySelectorAll('.fxr-r').forEach(function(r){ r.addEventListener('click',function(){ r.classList.toggle('fxr-on'); }); });
document.querySelectorAll('.fxr-pill').forEach(function(p,i){ p.addEventListener('click',function(){
  p.classList.toggle('fxr-on'); push(['pill1','pill2'][i], p.classList.contains('fxr-on')?1:0, true); }); });

/* ── audio ── */
var ac=null,node=null,wetG=null,dryG=null,outG=null,srcN=null,playing=false,viz={},kind=0;
var KINDS=['Saw chord','Sine 220','Noise','Pluck'];
function fail(m){document.getElementById('err').textContent=m;document.getElementById('status').textContent='Failed to start.';}
function push(k,v,raw){ if(!node)return; var m={}; m[k]=raw?v:v; node.port.postMessage(m); }
async function addWorklet(){
  var code=document.getElementById('wk').textContent;
  /* Safari refuses blob: worklets on file:// in some builds (fb396) — blob, then data:, then report */
  try{ await ac.audioWorklet.addModule(URL.createObjectURL(new Blob([code],{type:'text/javascript'}))); return 'blob'; }
  catch(e1){ try{ await ac.audioWorklet.addModule('data:text/javascript;base64,'+btoa(unescape(encodeURIComponent(code)))); return 'data'; }
    catch(e2){ throw new Error('addModule failed.\\nblob: '+e1.message+'\\ndata: '+e2.message); } }
}
async function boot(){
  ac=new (window.AudioContext||window.webkitAudioContext)();
  var how=await addWorklet();
  node=new AudioWorkletNode(ac,'%(proc)s',{numberOfInputs:1,numberOfOutputs:1,outputChannelCount:[2]});
  wetG=ac.createGain(); dryG=ac.createGain(); outG=ac.createGain();
  dryG.gain.value=0; outG.gain.value=0.55;
  node.connect(wetG); wetG.connect(outG); dryG.connect(outG); outG.connect(ac.destination);
  node.port.onmessage=function(e){ viz=e.data||{}; };
  KEYS.forEach(function(k,i){ push(k,D.knobs[i].v/100); });
  BACK.forEach(function(b,i){ push('b'+(i+1), b[1]/100); });
  push('type',0,true); push('character',0,true); push('axis',0,true);
  document.getElementById('status').textContent='Running (worklet via '+how+'). Drag a knob; switch Type.';
}
document.getElementById('go').addEventListener('click',function(){ boot().catch(function(e){fail(String(e&&e.message||e));}); });
document.getElementById('src').addEventListener('click',function(){ kind=(kind+1)%%KINDS.length;
  document.getElementById('src').textContent='Source: '+KINDS[kind]; if(playing){stop();start();} });
function makeSrc(){
  var sr=ac.sampleRate, n=Math.floor(sr*2), b=ac.createBuffer(2,n,sr);
  for(var c=0;c<2;c++){ var d=b.getChannelData(c);
    for(var i=0;i<n;i++){ var t=i/sr, v=0;
      if(kind===0){ [110,138.6,164.8,220].forEach(function(f){ var ph=(t*f)%%1; v+=(2*ph-1)*0.16; }); }
      else if(kind===1){ v=Math.sin(2*Math.PI*220*t)*0.5; }
      else if(kind===2){ v=(Math.random()*2-1)*0.28; }
      else { var env=Math.exp(-((t%%0.5))*7); [220,330,440].forEach(function(f){ v+=Math.sin(2*Math.PI*f*t)*0.2*env; }); }
      d[i]=v; } }
  /* fb431 — NORMALISE TO THE BUS. Every threshold in Compress and OTT is an ABSOLUTE dBFS
     number calibrated for the -26 dBFS Terrain bus. These sources ran at -9..-16 dBFS, i.e.
     up to 19 dB hot, which puts every band above OTT's UPWARD thresholds so the upward
     computer — the entire point of an OTT — never engages, and what is left is a downward
     compressor REMOVING top end. Measured on this very saw chord at Amount 0.5: -32.5 dBFS
     in gave +10.98 dB and +10.75 dB of high band; -13.7 dBFS in gave -4.67 and -6.39. The
     mockup was auditioning a level the rack never produces. */
  var acc=0,d0=b.getChannelData(0); for(var j=0;j<n;j++) acc+=d0[j]*d0[j];
  var cur=Math.sqrt(acc/n), want=Math.pow(10,-26/20), k=cur>1e-9?want/cur:1;
  for(var c2=0;c2<2;c2++){ var dd=b.getChannelData(c2); for(var j2=0;j2<n;j2++) dd[j2]*=k; }
  var el=document.getElementById('src');
  if(el) el.textContent='Source: '+KINDS[kind]+'  (-26 dBFS, bus level)';
  var s=ac.createBufferSource(); s.buffer=b; s.loop=true; return s;
}
function start(){ srcN=makeSrc(); srcN.connect(node); srcN.connect(dryG); srcN.start(); playing=true;
  document.getElementById('play').textContent='Stop'; }
function stop(){ if(srcN){try{srcN.stop();}catch(e){} srcN.disconnect(); srcN=null;} playing=false;
  document.getElementById('play').textContent='Play'; }
document.getElementById('play').addEventListener('click',function(){ if(!node){fail('Press Start audio first.');return;}
  if(ac.state==='suspended')ac.resume(); playing?stop():start(); });

/* ── the core visual. Driven by whatever the engine actually publishes — no invented data. ── */
function frame(){
  var svg=document.getElementById('cs'); if(svg){
    var W=svg.clientWidth||600,H=svg.clientHeight||120;
    svg.setAttribute('viewBox','0 0 '+W+' '+H);
    var d='', c=viz.curve||viz.mag||null;
    if(c&&c.length){                                     /* EQ: 96 log bins, dB */
      for(var i=0;i<c.length;i++){ var x=i/(c.length-1)*W;
        var y=H*0.5-Math.max(-30,Math.min(30,c[i]))*(H*0.5/30);
        d+=(i?'L':'M')+x.toFixed(1)+' '+y.toFixed(1); }
    } else {
      var g=viz.grDb, gb=viz.grDbBands||viz.grBands||null, lv=viz.lvl||0;
      if(gb&&gb.length){                                 /* OTT: per-band GR */
        for(var b2=0;b2<gb.length;b2++){ var x0=W*(b2+0.12)/gb.length, x1=W*(b2+0.88)/gb.length;
          var yy=H*0.5-Math.max(-24,Math.min(24,gb[b2]))*(H*0.42/24);
          d+='M'+x0.toFixed(1)+' '+yy.toFixed(1)+'L'+x1.toFixed(1)+' '+yy.toFixed(1); }
      } else if(g!==undefined){                          /* Compress: GR trace */
        HIST.push(g); if(HIST.length>140)HIST.shift();
        for(var k=0;k<HIST.length;k++){ var x=k/(HIST.length-1||1)*W;
          var y=H*0.12+Math.max(0,Math.min(24,HIST[k]))*(H*0.76/24);
          d+=(k?'L':'M')+x.toFixed(1)+' '+y.toFixed(1); }
      } else {                                           /* Widen: correlation */
        var cr=(viz.corr!==undefined)?viz.corr:0;
        HIST.push(cr); if(HIST.length>140)HIST.shift();
        for(var k2=0;k2<HIST.length;k2++){ var x2=k2/(HIST.length-1||1)*W;
          var y2=H*0.5-HIST[k2]*(H*0.40);
          d+=(k2?'L':'M')+x2.toFixed(1)+' '+y2.toFixed(1); }
      }
    }
    var el=document.getElementById('cline'); if(el)el.setAttribute('d',d||('M0 '+(H*0.5)+'L'+W+' '+(H*0.5)));
  }
  requestAnimationFrame(frame);
}
var HIST=[];
requestAnimationFrame(frame);
</script></body></html>
"""

import json
for D in DEVICES:
    worklet = io.open(D['wk'], encoding='utf-8').read()
    assert '</script' not in worklet.lower(), D['key'] + ': worklet contains a </script'
    out = os.path.join(HERE, D['key'] + '-mockup.html')
    page = PAGE % dict(theme=theme, css=css_lifted, worklet=worklet, knob=knob_fn,
        icplus=IC_PLUS, icarrow=IC_ARROW, icx=IC_X,
        dev=D['dev'], core=D['core'], proc=D['proc'], note=D['note'],
        types=json.dumps(D['types']), chars=json.dumps(D['chars']),
        d2=json.dumps(D['d2']), d2k=json.dumps(D['d2k']),
        back=json.dumps(D['back']), keys=json.dumps(D['keys']),
        pills=json.dumps([{'t':p[0],'on':bool(p[1])} for p in D['pills']]),
        knobs=json.dumps([{'l':k[0],'v':k[1]} for k in D['knobs']]))
    io.open(out, 'w', encoding='utf-8').write(page)
    print('wrote  %-22s %6.1f KB' % (os.path.basename(out), len(page.encode('utf-8'))/1024.0))
print('lifted %d .fxr- CSS rules + theme + knobSVG (%d chars)' % (len(rules), len(knob_fn)))
