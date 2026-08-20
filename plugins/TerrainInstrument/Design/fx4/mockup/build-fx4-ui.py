#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fb436/fb437 — THE FX4 UI MOCKUP: Equalizer · Widen · Compress · Multiband, live, audible, side by
side in one rack strip — built the only way the house allows (LIFT THE CARD, NEVER RETYPE IT).

Everything that is the plugin's is LIFTED VERBATIM from Source/ui/public/index.html:
  * theme vars, every .fxr-/.dst-curve/mvBreathe rule, knobSVG(), the header glyphs (fx3lift.py)
  * the fx4 HELPERS + the four CORES generators        (markers FX4-CORES-BEGIN/END, fb437)
  * the fx4 FX_FMT readout entries + their formatters   (markers FX4-FMT-BEGIN/END)
  * fx4Tick, the four drawers, the node/crossover/wheel handlers (markers FX4-TICK-BEGIN/END)
The four worklets are the SAME algorithms the C++ engines run (Design/fx4/*/ *-worklet.js).
The page only provides what the plugin provides around that code: DEVS, DEV_TEMPLATES, rack,
fxFmt/fxGlyph, __setSynParam (→ the worklet port), __fxRedrawKnobs, reRenderKeepSwap, and the
filter card's spectrum hooks (__fltHasBins/__fltBinMag/__fltSpecY, here backed by an AnalyserNode
on the Equalizer's wet output). The worklet posts are adapted to the plugin's push field names.

    python3 build-fx4-ui.py   ->  fx4-ui-mockup.html
"""
import io, os, re, sys, json

HERE = os.path.dirname(os.path.abspath(__file__))
FX4  = os.path.dirname(HERE)
PLUG = os.path.dirname(os.path.dirname(FX4))
sys.path.insert(0, os.path.join(PLUG, 'Design', 'fx3', 'mockup'))
import fx3lift

IDX = os.path.join(PLUG, 'Source', 'ui', 'public', 'index.html')
OUT = os.path.join(HERE, 'fx4-ui-mockup.html')
src = io.open(IDX, encoding='utf-8').read()

theme  = fx3lift.theme_vars(src)
css    = fx3lift.card_css(src)
knob   = fx3lift.knob_svg(src)
ICP, ICA, ICX = fx3lift.glyphs(src)

def between(a, b, label):
    i = src.index(a); j = src.index(b, i)
    out = src[i + len(a):j]
    assert len(out) > 200, label + ' lifted suspiciously little'
    return out

HB = '/* ═══ fb437 — THE FX4 CORES (Equalizer · Widen · Compress · Multiband)'
helpers = HB + between(HB, '          var CORES={', 'helpers')
cores   = between('/* ═══ FX4-CORES-BEGIN (fb437) ═══ */', '/* ═══ FX4-CORES-END ═══ */', 'cores')
fmt     = between('/* ═══ FX4-FMT-BEGIN (fb437)', '/* ═══ FX4-FMT-END ═══ */', 'fmt')
fmt     = fmt[fmt.index('*/') + 2:]                      # drop the rest of the banner comment
TB = '/* ═══ FX4-TICK-BEGIN (fb437)'
tick    = TB + between(TB, '/* ═══ FX4-TICK-END ═══ */', 'tick')
fmt_helpers = '\n'.join(re.findall(r'^\s*function fx4(?:Hz|Db|DbS|Ms)\(.*$', src, re.M))
assert fmt_helpers.count('function') == 4, 'expected the four fx4 formatters'
assert 'eqz:function' in cores and 'ott:function' in cores, 'cores did not lift'
assert 'function fx4Tick' in tick and 'fx4DrawOtt' in tick, 'tick did not lift'
assert "'eqz|LOWHZ'" in fmt and "'ott|TREBLE'" in fmt, 'fmt did not lift'

def wk(rel):
    s = io.open(os.path.join(FX4, rel), encoding='utf-8').read()
    assert '</script' not in s.lower(), rel + ' contains </script'
    return s
WORKLETS = { 'eqz': wk('eq/eq-worklet.js'), 'wid': wk('widen/widen-worklet.js'),
             'cmp': wk('dynamics/compress-worklet.js'), 'ott': wk('dynamics/ott-worklet.js') }

# ── the four devices, exactly as DEV_TEMPLATES ships them, plus the worklet keys ─────────────
DEVICES = [
 dict(dev='Equalizer', core='eqz', proc='terrain-eq', pfx='SYN_EQZ_',
      types=['Surgical','British','American','Passive','Open','Dynamic','Chisel'], tpN=16,
      chars=['Plain','Tight','Broad','Steep','Scoop','Deep Pivot','Bright Pivot','Four Bells'],
      d2k='Focus', d2=['Stereo','Mid','Side','Left','Right'], d2p='FOCUS',
      knobs=[['Slant',50,'SLANT'],['Air',50,'AIR'],['Amount',50,'AMOUNT'],['Mix',100,'MIX']],
      keys=['f1','f2','f3','mix'],
      back=[['Low Hz',50,'LOWHZ'],['Low',50,'LOW'],['Body Hz',50,'BODYHZ'],['Body',50,'BODY'],
            ['Bite Hz',50,'BITEHZ'],['Bite',50,'BITE'],['Reach',50,'REACH'],['Trait',50,'TRAIT']],
      pills=[['Delta',0]], pillKeys=['delta']),
 dict(dev='Widen', core='wid', proc='terrain-widen', pfx='SYN_WID_',
      types=['Throng','Twin','Steady','Twofold','Blur','Bands'], tpN=16,
      chars=['JP Classic','Even Fan','Analog Drift','Tight Fan','Wide Fan','Octave Bloom','Sub Anchor','Three Phase'],
      d2k='Field', d2=['Straight','Alternate','Orbit','Swap','Side Only','Gather'], d2p='FIELD',
      knobs=[['Detune',35,'AMOUNT'],['Width',50,'WIDTH'],['Rate',35,'RATE'],['Mix',50,'MIX']],
      keys=['amount','width','rate','mix'],
      back=[['Voices',50,'VOICES'],['Spread',85,'SPREAD'],['Offset',50,'OFFSET'],['Roam',0,'ROAM'],
            ['Low Keep',0,'LOWKEEP'],['Tone',50,'TONE'],['Feedback',0,'FEEDBACK'],['Balance',50,'BALANCE']],
      pills=[['Retrig',0],['Mono',0]], pillKeys=['retrig','hearMono']),
 dict(dev='Compress', core='cmp', proc='terrain-compress', pfx='SYN_CMP_',
      types=['Exact','Bus','FET 76','Opto','Vari-Mu','OverEasy','Ride','Limit'], tpN=16,
      chars=['Precise','Soft Touch','Loose Grip','Blunt','Deep Release','Line Attack','Poise','Judder'],
      d2k='Detect', d2=['Native','Peak','Average','Patient','Spike'], d2p='DETECT',
      knobs=[['Push',20,'PUSH'],['Ratio',50,'RATIO'],['Lift',25,'LIFT'],['Mix',100,'MIX']],
      keys=['push','ratio','lift','mix'],
      back=[['Attack',61,'ATTACK'],['Release',63,'RELEASE'],['Round',25,'ROUND'],['Hear Cut',0,'HEARCUT'],
            ['Edge',50,'EDGE'],['Cling',0,'CLING'],['Tie',100,'TIE'],['Burn',0,'BURN']],
      pills=[['Auto',0]], pillKeys=['autoMakeup']),
 dict(dev='Multiband', core='ott', proc='terrain-ott', pfx='SYN_OTT_',
      types=['Over Top','Gentle','Heavy','Sheen','Bass Safe','Surge','Two Band','Stagger'], tpN=16,
      chars=['Straight Up','Sharp Ears','Long Ears','Wide Corner','One Detector','Slow Low','Twice Deep','Full Crest'],
      d2k='Stereo', d2=['Linked','Free Pair','Mid-Side'], d2p='STEREO',
      knobs=[['Amount',50,'AMOUNT'],['Chase',50,'CHASE'],['Top Lift',25,'TOPLIFT'],['Mix',100,'MIX']],
      keys=['amount','speed','topLift','mix'],
      back=[['Low Cross',47,'LOWCROSS'],['High Cross',44,'HIGHCROSS'],['Raise',67,'RAISE'],['Press',67,'PRESS'],
            ['Grip',50,'GRIP'],['Bass',50,'BASS'],['Mids',50,'MIDS'],['Treble',50,'TREBLE']],
      pills=[['Crest',0]], pillKeys=['crest']),
]

PAGE = u"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>FX4 — the four cards, live</title>
<style>
@@THEME@@
*{box-sizing:border-box}
html,body{margin:0;min-height:100%}
body{background:radial-gradient(1100px 560px at 30% -10%,#241d3a 0,transparent 60%),#141220;
  color:var(--text-primary);font-family:-apple-system,'SF Pro Text',system-ui,sans-serif;
  -webkit-font-smoothing:antialiased;display:flex;flex-direction:column;align-items:center;
  gap:12px;padding:22px 18px 40px}
h1{font:600 15px/1.2 -apple-system,system-ui;letter-spacing:.02em;margin:0;color:#e8e4f5}
.sub{font:400 11.5px/1.5 -apple-system,system-ui;color:#9a92b8;max-width:760px;text-align:center;margin:0}
.bar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;justify-content:center}
.bar .lbl{font:500 10px -apple-system,system-ui;letter-spacing:.08em;text-transform:uppercase;color:#7f789c;margin:0 2px 0 10px}
button{background:#241f38;color:#e8e4f5;border:1px solid #3a3356;border-radius:7px;
  padding:6px 12px;font:500 11.5px -apple-system,system-ui;cursor:pointer}
button:hover{background:#2e2846}
button.on{border-color:var(--purple-400);color:#fff;box-shadow:none}
#status{font:400 11px -apple-system,system-ui;color:#8f88ab;min-height:15px}
#err{font:400 11px/1.45 ui-monospace,Menlo,monospace;color:#ff9a9a;white-space:pre-wrap;max-width:760px}
#rackwrap{max-width:100%;overflow-x:auto;overflow-y:hidden;padding:6px 0 2px;scrollbar-width:none}
#rackwrap::-webkit-scrollbar{display:none}
#scale{transform-origin:top left;transform:scale(1.2)}
#rack{display:flex;gap:9px;height:170px;width:max-content}
#panels{display:flex;gap:9px;width:max-content;align-items:flex-start;margin-top:10px}
.bpslot{flex:0 0 auto}
@@CSS@@
/* the mockup's own scaffolding — NOT part of the lifted rack sheet */
.fxr-dev{position:relative}
.bp{display:none;border:1px solid var(--border-strong,#3a3356);border-radius:10px;background:rgba(24,20,38,.72);padding:10px 12px}
.bp.on{display:block}
.bp-row{display:flex;gap:10px;align-items:flex-end;margin-bottom:10px}
.bp-sel{display:flex;flex-direction:column;gap:4px;flex:1;min-width:0}
.bp-sel label{font:500 9px -apple-system,system-ui;letter-spacing:.08em;text-transform:uppercase;color:#8f88ab}
.bp-sel select{background:#1c1830;color:#e8e4f5;border:1px solid #3a3356;border-radius:6px;padding:3px 5px;font:400 10.5px -apple-system,system-ui;width:100%}
.bp-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px 0;position:relative}
.bp-k{display:flex;flex-direction:column;align-items:center;gap:4px}
.bp-k .fxr-lab{font:500 9px -apple-system,system-ui;color:#9a92b8}
.bp-sep{position:absolute;top:2%;bottom:2%;width:1px;background:linear-gradient(180deg,transparent,#3a3356 18%,#3a3356 82%,transparent)}
</style></head><body>
<h1>FX4 — Equalizer · Widen · Compress · Multiband</h1>
<p class="sub">The four cards, their cores LIFTED from the plugin (one copy of the code), live on the real engines.
Press <b>Start audio</b>, then <b>Play</b>. <b>Hear</b> follows whichever card you touch (or pick it); all four always process.
Equalizer: drag a <b>node</b> (X = Hz, Y = gain), scroll on the curve for <b>Trait</b>, double-click resets. Multiband: drag a <b>crossover line</b>. Knobs drag up/down, double-click resets.</p>
<div class="bar">
  <button id="go">Start audio</button>
  <button id="play">Play</button>
  <button id="src">Source: Saw chord</button>
  <span class="lbl">Hear</span>
  <button class="hear on" data-h="0">Equalizer</button><button class="hear" data-h="1">Widen</button>
  <button class="hear" data-h="2">Compress</button><button class="hear" data-h="3">Multiband</button><button class="hear" data-h="-1">Dry</button>
  <span class="lbl">Zoom</span>
  <button class="zoom" data-z="1">1×</button><button class="zoom on" data-z="1.2">1.2×</button><button class="zoom" data-z="1.6">1.6×</button><button class="zoom" data-z="2">2×</button>
</div>
<div id="status">Idle.</div><div id="err"></div>
<div id="rackwrap"><div id="scale"><div id="syn-panel"><div id="rack"></div><div id="panels"></div></div></div></div>

<script id="wk-eqz" type="text/plain">@@WK_EQZ@@</script>
<script id="wk-wid" type="text/plain">@@WK_WID@@</script>
<script id="wk-cmp" type="text/plain">@@WK_CMP@@</script>
<script id="wk-ott" type="text/plain">@@WK_OTT@@</script>
<script>
"use strict";
var DEVICES=@@DEVICES@@;
var IC_PLUS=@@ICP@@, IC_ARROW=@@ICA@@, IC_X=@@ICX@@;
var RLBL=['A','B','C','D','S','N'];
@@KNOB@@

/* ── what the plugin provides around the lifted code ── */
var rack=document.getElementById('rack');
var DEV_TEMPLATES=DEVICES.map(function(t){ return { core:t.core, knobs:t.knobs.map(function(k){return {l:k[0],v:k[1],p:t.pfx+k[2]};}),
  back:{ knobs:t.back.map(function(b){return [b[0],b[1],t.pfx+b[2]];}) } }; });
var DEVS=DEVICES.map(function(t){ return {
  name:t.dev, core:t.core, inst:1, pfx:t.pfx, type:t.types[0], types:t.types, tpN:t.tpN, preset:'Init', on:true,
  pills:t.pills.map(function(p){return {t:p[0],on:!!p[1]};}), pp:t.pillKeys.map(function(k){return t.pfx+k.toUpperCase();}),
  route:[true,false,false,false,false,false],
  knobs:t.knobs.map(function(k){return {l:k[0],v:k[1],p:t.pfx+k[2],d:k[1]};}),
  xp:(t.core==='eqz')?[1,2,3,4].map(function(k){return [t.pfx+'X'+k+'HZ',t.pfx+'X'+k,t.pfx+'X'+k+'ON'];}):null, xb:(t.core==='eqz')?[[50,50,0],[50,50,0],[50,50,0],[50,50,0]]:null,   /* fb438 — the free bells */
  back:{d1:{k:'Character',v:t.chars[0],opts:t.chars,p:t.pfx+'CHAR'}, d2:{k:t.d2k,v:t.d2[0],opts:t.d2,p:t.pfx+t.d2p,pN:8},
        knobs:t.back.map(function(b){return [b[0],b[1],t.pfx+b[2],b[1]];})},
  typeIdx:0, charIdx:0, axisIdx:0, __t:t }; });
function fxParamTail(pid){ return (pid||'').slice((pid||'').lastIndexOf('_')+1); }
var FX_FMT={
@@FMT@@
};
@@FMT_HELPERS@@
var FX_STEP={ 'wid|VOICES':6 };
function fxFmt(core,pid,v,d){ try{ var f=FX_FMT[core+'|'+fxParamTail(pid)]; return f?f(v,d):undefined; }catch(e){ return undefined; } }
function fxGlyph(){ return null; }
function fxStep(core,pid){ return FX_STEP[core+'|'+fxParamTail(pid)]||0; }
function detent(core,pid,v){ var n=fxStep(core,pid); if(n>1) v=Math.round(v/100*(n-1))/(n-1)*100; return v; }
/* the per-Type relabels — the engines' own tables, same as the plugin */
var FX4_WID_HERO=['Detune','Depth','Cents','Sway','Wash','Cleave'];
var FX4_EQZ_TRAIT=['Pinch','Slope','Taper','Dip','Silk','Pivot','Sting'];
function fx4ApplyType(d,typeName){ var ti=d.types.indexOf(typeName); if(ti<0) return; d.type=typeName; d.typeIdx=ti;
  if(d.core==='wid') d.knobs[0].l=FX4_WID_HERO[ti]||'Amount'; if(d.core==='eqz') d.back.knobs[7][0]=FX4_EQZ_TRAIT[ti]||'Trait'; }
DEVS.forEach(function(d){ fx4ApplyType(d,d.type); });

@@HELPERS@@
var CORES={
@@CORES@@
};

/* devHTML() — structure-for-structure as the plugin emits it */
function devHTML(d,idx){
  var pills=d.pills.map(function(p,i){return '<span class="fxr-pill'+(p.on?' fxr-on':'')+'" data-pi="'+i+'"><span class="fxr-t">'+p.t+'</span></span>';}).join('');
  var route=d.route.map(function(on,i){return '<span class="fxr-r'+(on?' fxr-on':'')+'" data-r="'+i+'"><span class="fxr-t">'+RLBL[i]+'</span></span>';}).join('');
  var knobs=d.knobs.map(function(k,i){return '<div class="fxr-knob"><div class="fxr-dial" data-k="'+i+'" data-p="'+k.p+'">'+knobSVG(k.v,fxFmt(d.core,k.p,k.v,d),null)+'</div><span class="fxr-lab">'+k.l+'</span></div>';}).join('');
  return '<div class="fxr-dev'+(d.on?'':' fxr-off')+'" data-dev="'+idx+'">'
    +'<div class="fxr-head">'
      +'<span class="fxr-grip">\\u22ee\\u22ee</span><span class="fxr-name">'+d.name+'</span>'
      +'<span class="fxr-type"><span class="fxr-tw"><span class="fxr-tl">'+d.type+'<span class="fxr-car">\\u25be</span></span>'
        +d.types.map(function(t){return '<span class="fxr-tg">'+t+'<span class="fxr-car">\\u25be</span></span>';}).join('')
        +'</span><select class="fxr-type-native" data-tsel="'+idx+'">'+d.types.map(function(t){return '<option'+(t===d.type?' selected':'')+'>'+t+'</option>';}).join('')+'</select></span>'
      +'<span class="fxr-preset" data-act="preset"><span class="fxr-star">\\u2726</span><span class="fxr-pname">Init</span><span class="fxr-car">\\u25be</span></span>'
      +'<span class="fxr-spacer"></span>'
      +'<span class="fxr-headr"><span class="fxr-swap" data-act="swap" title="More parameters">'+IC_PLUS+IC_ARROW+'</span><span class="fxr-pwr" data-act="pwr"></span><span class="fxr-x" data-act="x" title="Remove">'+IC_X+'</span></span>'
    +'</div>'
    +'<div class="fxr-core" data-core="'+d.core+'">'+CORES[d.core]()+'</div>'
    +'<div class="fxr-ctrls">'
      +'<div class="fxr-knobs">'+knobs+'</div>'
      +'<div class="fxr-divider"></div>'
      +'<div class="fxr-rightcol"><div class="fxr-pills">'+pills+'</div><div class="fxr-route">'+route+'</div></div>'
    +'</div></div>';
}
/* the OFFICIAL back panel (fb275): 2 dropdowns + 8 knobs 4x2 + 3 column separators. Back dials carry
   data-bk = LABEL (the rack's convention) so __fxRedrawKnobs finds them. */
function backHTML(d,idx){
  var ks=d.back.knobs.map(function(b,i){return '<div class="bp-k fxr-bk-knob"><div class="fxr-dial" data-bk="'+b[0]+'" data-p="'+b[2]+'">'+knobSVG(b[1],fxFmt(d.core,b[2],b[1],d),null)+'</div><span class="fxr-lab">'+b[0]+'</span></div>';}).join('');
  var seps=''; for(var s=1;s<4;s++) seps+='<div class="bp-sep" style="left:'+(s*25)+'%"></div>';
  return '<div class="bp'+(d.__bpOpen?' on':'')+'" data-bp="'+idx+'"><div class="bp-row">'
    +'<div class="bp-sel"><label>'+d.back.d1.k+'</label><select data-csel="'+idx+'">'+d.back.d1.opts.map(function(c){return '<option'+(c===d.back.d1.v?' selected':'')+'>'+c+'</option>';}).join('')+'</select></div>'
    +'<div class="bp-sel"><label>'+d.back.d2.k+'</label><select data-dsel="'+idx+'">'+d.back.d2.opts.map(function(c){return '<option'+(c===d.back.d2.v?' selected':'')+'>'+c+'</option>';}).join('')+'</select></div>'
    +'</div><div class="bp-grid">'+seps+ks+'</div></div>';
}
function render(){
  rack.innerHTML=DEVS.map(devHTML).join('');
  document.getElementById('panels').innerHTML=DEVS.map(function(d,i){ return '<div class="bpslot">'+backHTML(d,i)+'</div>'; }).join('');
  fitZoom();
}
function reRenderKeepSwap(){ render(); }
/* redraw one device's knobs from the MODEL — the plugin's __fxRedrawKnobs, here over the mockup's markup */
window.__fxRedrawKnobs=function(d){ try{
  var idx=DEVS.indexOf(d), card=rack.querySelector('.fxr-dev[data-dev="'+idx+'"]');
  if(card){ card.querySelectorAll('.fxr-dial[data-k]').forEach(function(el){ var k=d.knobs[+el.getAttribute('data-k')];
    el.innerHTML=knobSVG(k.v,fxFmt(d.core,k.p,k.v,d),null); var la=el.parentNode.querySelector('.fxr-lab'); if(la&&la.textContent!==k.l) la.textContent=k.l; }); }
  var bp=document.querySelector('.bp[data-bp="'+idx+'"]');
  if(bp){ bp.querySelectorAll('.fxr-dial[data-bk]').forEach(function(el){ var lbl=el.getAttribute('data-bk'), row=null;
    for(var q=0;q<d.back.knobs.length;q++) if(d.back.knobs[q][0]===lbl){ row=d.back.knobs[q]; break; } if(!row) return;
    el.innerHTML=knobSVG(row[1],fxFmt(d.core,row[2],row[1],d),null); var la2=el.parentNode.querySelector('.fxr-lab'); if(la2&&la2.textContent!==row[0]) la2.textContent=row[0]; }); }
}catch(e){} };
/* __setSynParam — the plugin's ONE write path. Here: find the param's device + slot, push the worklet key. */
window.__setSynParam=function(id,norm){ try{
  for(var i=0;i<DEVS.length;i++){ var d=DEVS[i]; if(id.indexOf(d.pfx)!==0) continue; var tail=id.slice(d.pfx.length), t=d.__t;
    for(var k=0;k<t.knobs.length;k++) if(t.knobs[k][2]===tail){ push(d,t.keys[k],norm); return; }
    for(var b=0;b<t.back.length;b++) if(t.back[b][2]===tail){ push(d,'b'+(b+1),norm); return; }
    for(var p=0;p<t.pillKeys.length;p++) if(t.pillKeys[p].toUpperCase()===tail){ push(d,t.pillKeys[p],norm>0.5?1:0,true); return; }
    var mx; if((mx=/^X([1-4])HZ$/.exec(tail))){ push(d,'x'+(2*(+mx[1])-1),norm); return; } if((mx=/^X([1-4])ON$/.exec(tail))){ push(d,'xon'+mx[1],norm>0.5?1:0,true); return; } if((mx=/^X([1-4])$/.exec(tail))){ push(d,'x'+(2*(+mx[1])),norm); return; }   /* fb438 */
    if(tail==='TYPE'){ push(d,'type',Math.round(norm*(d.tpN-1)),true); return; }
    if(tail==='CHAR'){ push(d,'character',Math.round(norm*7),true); return; }
    if(tail===t.d2p){ push(d,'axis',Math.round(norm*7),true); return; }
  } }catch(e){} };

render();

/* ── event delegation: knobs (front+back), type/char/axis, pills, power, swap; Hear follows the touched card ── */
(function(){
  var drag=null;
  function devOf(el){ var card=el.closest('.fxr-dev'); if(card) return DEVS[+card.getAttribute('data-dev')]; var bp=el.closest('.bp'); return bp?DEVS[+bp.getAttribute('data-bp')]:null; }
  document.addEventListener('pointerdown',function(e){
    var d0=e.target.closest&&(e.target.closest('.fxr-dev')||e.target.closest('.bp')); if(d0){ var dd=devOf(d0); if(dd){ var hi=DEVS.indexOf(dd); if(hi!==hear){ hear=hi; document.querySelectorAll('.hear').forEach(function(x){x.classList.toggle('on',+x.getAttribute('data-h')===hi);}); setHear(); } } }
    var dial=e.target.closest&&e.target.closest('.fxr-dial'); if(!dial) return;
    var d=devOf(dial); if(!d) return;
    var k=dial.getAttribute('data-k'), bk=dial.getAttribute('data-bk'), row=null;
    if(bk!=null) for(var q=0;q<d.back.knobs.length;q++) if(d.back.knobs[q][0]===bk){ row=d.back.knobs[q]; break; }
    var v0=(k!=null)?d.knobs[+k].v:(row?row[1]:0);
    drag={d:d,dial:dial,k:k,row:row,y0:e.clientY,v0:v0}; dial.setPointerCapture(e.pointerId); e.preventDefault();
  },true);
  document.addEventListener('pointermove',function(e){ if(!drag) return;
    var v=Math.max(0,Math.min(100,drag.v0+(drag.y0-e.clientY)*0.6)), d=drag.d;
    if(drag.k!=null){ var kk=d.knobs[+drag.k]; v=detent(d.core,kk.p,v); kk.v=v; window.__setSynParam(kk.p,v/100); }
    else if(drag.row){ v=detent(d.core,drag.row[2],v); drag.row[1]=v; window.__setSynParam(drag.row[2],v/100); }
    window.__fxRedrawKnobs(d);
  });
  document.addEventListener('pointerup',function(){ drag=null; });
  document.addEventListener('dblclick',function(e){
    var dial=e.target.closest&&e.target.closest('.fxr-dial'); if(!dial) return; var d=devOf(dial); if(!d) return;
    var k=dial.getAttribute('data-k'), bk=dial.getAttribute('data-bk');
    if(k!=null){ d.knobs[+k].v=d.knobs[+k].d; window.__setSynParam(d.knobs[+k].p,d.knobs[+k].v/100); }
    else if(bk!=null){ for(var q=0;q<d.back.knobs.length;q++) if(d.back.knobs[q][0]===bk){ d.back.knobs[q][1]=d.back.knobs[q][3]; window.__setSynParam(d.back.knobs[q][2],d.back.knobs[q][1]/100); } }
    window.__fxRedrawKnobs(d);
  });
  document.addEventListener('change',function(e){
    var t=e.target;
    if(t.hasAttribute('data-tsel')){ var d=DEVS[+t.getAttribute('data-tsel')]; fx4ApplyType(d,d.types[t.selectedIndex]); push(d,'type',t.selectedIndex,true); render(); }
    if(t.hasAttribute('data-csel')){ var d2=DEVS[+t.getAttribute('data-csel')]; d2.charIdx=t.selectedIndex; d2.back.d1.v=d2.back.d1.opts[t.selectedIndex]; push(d2,'character',t.selectedIndex,true); }
    if(t.hasAttribute('data-dsel')){ var d3=DEVS[+t.getAttribute('data-dsel')]; d3.axisIdx=t.selectedIndex; d3.back.d2.v=d3.back.d2.opts[t.selectedIndex]; push(d3,'axis',t.selectedIndex,true); }
  });
  document.addEventListener('click',function(e){
    var card=e.target.closest&&e.target.closest('.fxr-dev'); if(!card) return; var d=DEVS[+card.getAttribute('data-dev')];
    var pill=e.target.closest('.fxr-pill'); if(pill){ var pi=+pill.getAttribute('data-pi'); d.pills[pi].on=!d.pills[pi].on; pill.classList.toggle('fxr-on',d.pills[pi].on); push(d,d.__t.pillKeys[pi],d.pills[pi].on?1:0,true);
      /* the EQ's Delta is processor-level in the plugin (wet − dry); here the graph does it: the dry leg goes to −1 */
      if(d.core==='eqz'&&pi===0&&d.__dry&&ac){ d.__dry.gain.setTargetAtTime(d.pills[0].on?-1:0,ac.currentTime,0.02); } return; }
    var r=e.target.closest('.fxr-r'); if(r){ r.classList.toggle('fxr-on'); return; }
    if(e.target.closest('.fxr-pwr')){ d.on=!d.on; card.classList.toggle('fxr-off',!d.on); setPower(d); return; }
    if(e.target.closest('.fxr-swap')){ d.__bpOpen=!d.__bpOpen; var bp=document.querySelector('.bp[data-bp="'+DEVS.indexOf(d)+'"]'); if(bp) bp.classList.toggle('on',d.__bpOpen); fitZoom(); return; }
  });
})();

/* ═══ the plugin's own tick, drawers and handlers — LIFTED from index.html (markers) ═══ */
@@TICK@@

/* ═══ AUDIO — one context, one -26 dBFS source, four engines in PARALLEL, Hear picks the ear ═══ */
var ac=null,srcN=null,playing=false,kind=0,hear=0,dryG=null,master=null;
var KINDS=['Saw chord','Sine 220','Noise','Pluck','Drums','Pad','Bell','Sweep'];
function fail(m){ document.getElementById('err').textContent=m; document.getElementById('status').textContent='Failed to start.'; }
async function addWorklet(code){
  try{ await ac.audioWorklet.addModule(URL.createObjectURL(new Blob([code],{type:'text/javascript'}))); return 'blob'; }
  catch(e1){ try{ await ac.audioWorklet.addModule('data:text/javascript;base64,'+btoa(unescape(encodeURIComponent(code)))); return 'data'; }
    catch(e2){ throw new Error('addModule failed.\\nblob: '+e1.message+'\\ndata: '+e2.message); } }
}
function push(d,k,v,raw){ if(!d.__node||k==null) return; var m={}; m[k]=v; d.__node.port.postMessage(m); }
function setPower(d){ if(!d.__wet) return; var t=ac.currentTime; d.__wet.gain.setTargetAtTime(d.on?1:0,t,0.02); var dl=(d.core==='eqz'&&d.pills[0]&&d.pills[0].on)?-1:0; d.__dry.gain.setTargetAtTime(d.on?dl:1,t,0.02); }
function setHear(){ if(!ac) return; var t=ac.currentTime; DEVS.forEach(function(d,i){ if(d.__hear) d.__hear.gain.setTargetAtTime(hear===i?1:0,t,0.03); }); if(dryG) dryG.gain.setTargetAtTime(hear===-1?1:0,t,0.03); }
/* the worklet posts → the plugin's push field names, so the LIFTED drawers read them unchanged */
function adapt(core,z){
  if(core==='eqz') return {lvl:z.lvl,hz:z.nodeHz,db:z.nodeDb,on:z.nodeOn,curve:z.curve};
  if(core==='wid') return {corr:z.corr,nV:z.nV||6,pan:z.voicePan,cents:z.voiceCents,width:z.widthNow,lvl:z.lvl};
  if(core==='cmp') return {gr:z.grDb,'in':z.inDb,out:z.outDb,thr:z.thrDb,ratio:(z.ratio===Infinity||z.ratio>1e6)?-1:z.ratio,atk:z.attackMs,rel:z.releaseMs,kneeDb:z.kneeDb,knee:z.knee,lvl:z.lvl};
  if(core==='ott') return {gr:z.grDb,lv:z.bandDb,x:z.xoverHz,tdn:z.tdn,tup:z.tup,nb:z.bands||3,lvl:z.lvl};
  return z;
}
window.__fx4VizPush={eqz:[null],wid:[null],cmp:[null],ott:[null]};
/* the filter card's spectrum hooks, backed by an AnalyserNode on the Equalizer's wet output */
var specAn=null, specBuf=null;
window.__fltHasBins=function(){ return !!specAn; };
window.__fltBinMag=function(hz){ if(!specAn) return 0; if(!specBuf||specBuf.length!==specAn.frequencyBinCount) specBuf=new Float32Array(specAn.frequencyBinCount);
  if(!window.__specFrame||window.__specFrame!==window.__fx4Frame){ specAn.getFloatFrequencyData(specBuf); window.__specFrame=window.__fx4Frame; }
  var sr=ac?ac.sampleRate:48000, bb=hz/(sr/2)*specBuf.length, i0=Math.floor(bb), fr=bb-i0;
  var v0=specBuf[Math.min(specBuf.length-1,i0)], v1=specBuf[Math.min(specBuf.length-1,i0+1)]; var db=(isFinite(v0)?v0:-200)*(1-fr)+(isFinite(v1)?v1:-200)*fr;
  return Math.pow(10,(db+10)/20)/7.5; };      /* inverse of the plugin's specY input (m*7.5 -> dB), +10 dB visual gain */
window.__fltSpecY=function(m,h){ var db=20*Math.log10(Math.max(m*7.5,1e-7)), top=6, bot=-92; var y=h*(1-(db-bot)/(top-bot)); return Math.max(-h*0.1,Math.min(h,y)); };
(function(){ function fr(){ window.__fx4Frame=(window.__fx4Frame||0)+1; requestAnimationFrame(fr); } requestAnimationFrame(fr); })();
async function boot(){
  ac=new (window.AudioContext||window.webkitAudioContext)();
  master=ac.createGain(); master.gain.value=0.55; master.connect(ac.destination);
  dryG=ac.createGain(); dryG.gain.value=0; dryG.connect(master);
  var how='';
  for(var i=0;i<DEVS.length;i++){ var d=DEVS[i];
    how=await addWorklet(document.getElementById('wk-'+d.core).textContent);
    d.__node=new AudioWorkletNode(ac,d.__t.proc,{numberOfInputs:1,numberOfOutputs:1,outputChannelCount:[2]});
    d.__wet=ac.createGain(); d.__dry=ac.createGain(); d.__dry.gain.value=0; d.__hear=ac.createGain(); d.__hear.gain.value=(hear===i)?1:0;
    d.__node.connect(d.__wet); d.__wet.connect(d.__hear); d.__dry.connect(d.__hear); d.__hear.connect(master);
    if(d.core==='eqz'){ specAn=ac.createAnalyser(); specAn.fftSize=2048; specAn.smoothingTimeConstant=0.55; d.__wet.connect(specAn); }
    (function(dd){ dd.__node.port.onmessage=function(e){ var z=e.data||{}; window.__fx4VizPush[dd.core][0]=adapt(dd.core,z); }; })(d);
    d.__t.keys.forEach(function(k,j){ push(d,k,d.knobs[j].v/100); });
    d.back.knobs.forEach(function(b,j){ push(d,'b'+(j+1),b[1]/100); });
    push(d,'type',d.typeIdx,true); push(d,'character',d.charIdx,true); push(d,'axis',d.axisIdx,true);
    d.pills.forEach(function(p,j){ push(d,d.__t.pillKeys[j],p.on?1:0,true); });
  }
  document.getElementById('status').textContent='Running (worklets via '+how+'). Press Play, then drag things.';
}
function makeSrc(){
  var sr=ac.sampleRate, n=Math.floor(sr*4), b=ac.createBuffer(2,n,sr);
  var sd=0; function rnd(){ sd=(sd*1103515245+12345)&0x7fffffff; return (sd/0x40000000)-1; }
  for(var c=0;c<2;c++){ var d=b.getChannelData(c); sd=22222;
    for(var i=0;i<n;i++){ var t=i/sr, v=0;
      if(kind===0){ [110,138.6,164.8,220].forEach(function(f){ var ph=(t*f)%1; v+=(2*ph-1)*0.16; }); }
      else if(kind===1){ v=Math.sin(2*Math.PI*220*t)*0.5; }
      else if(kind===2){ v=rnd()*0.28; }
      else if(kind===3){ var env=Math.exp(-((t%0.5))*7); [220,330,440].forEach(function(f){ v+=Math.sin(2*Math.PI*f*t)*0.2*env; }); }
      else if(kind===4){ var tb=t%1.0, kt=tb%0.5, ke=Math.exp(-kt*20), kf=52*(1+3.2*Math.exp(-kt*42));
        v+=Math.sin(2*Math.PI*kf*kt)*0.95*ke;
        if(tb>=0.5){ var se=Math.exp(-(tb-0.5)*24); v+=(rnd()*0.5+Math.sin(2*Math.PI*186*(tb-0.5))*0.32)*se; }
        v+=rnd()*0.20*Math.exp(-(t%0.125)*130); }
      else if(kind===5){ [110,164.8,220,329.6].forEach(function(f,ix){ var det=1+(c?1:-1)*0.0016*(ix+1);
          v+=Math.sin(2*Math.PI*f*det*t)*0.15*(0.55+0.45*Math.sin(2*Math.PI*(0.07+ix*0.031)*t));
          v+=Math.sin(2*Math.PI*f*2*det*t)*0.045*(0.5+0.5*Math.sin(2*Math.PI*0.12*t)); }); }
      else if(kind===6){ var bt=t%2.0, be=Math.exp(-bt*2.6), mi=6.5*Math.exp(-bt*4.5); v=Math.sin(2*Math.PI*440*t+mi*Math.sin(2*Math.PI*622.3*t))*0.5*be; }
      else { var r=80, T=4, ph2=100*T/Math.log(r)*(Math.pow(r,t/T)-1); v=Math.sin(2*Math.PI*ph2)*0.45; }
      d[i]=v; } }
  var acc=0,d0=b.getChannelData(0); for(var j=0;j<n;j++) acc+=d0[j]*d0[j];
  var cur=Math.sqrt(acc/n), want=Math.pow(10,-26/20), k=cur>1e-9?want/cur:1;
  for(var c2=0;c2<2;c2++){ var dd=b.getChannelData(c2); for(var j2=0;j2<n;j2++) dd[j2]*=k; }
  document.getElementById('src').textContent='Source: '+KINDS[kind];
  var s=ac.createBufferSource(); s.buffer=b; s.loop=true; return s;
}
function start(){ srcN=makeSrc(); DEVS.forEach(function(d){ srcN.connect(d.__node); srcN.connect(d.__dry); }); srcN.connect(dryG); srcN.start(); playing=true; document.getElementById('play').textContent='Stop'; }
function stop(){ if(srcN){ try{srcN.stop();}catch(e){} srcN.disconnect(); srcN=null; } playing=false; document.getElementById('play').textContent='Play'; }
document.getElementById('go').addEventListener('click',function(){ if(ac) return; document.getElementById('status').textContent='Starting\\u2026'; boot().catch(function(e){fail(String(e&&e.stack||e));}); });
document.getElementById('play').addEventListener('click',function(){ if(!ac){ fail('Press Start audio first.'); return; } if(ac.state==='suspended') ac.resume(); playing?stop():start(); });
document.getElementById('src').addEventListener('click',function(){ kind=(kind+1)%KINDS.length; document.getElementById('src').textContent='Source: '+KINDS[kind]; if(playing){ stop(); start(); } });
document.querySelectorAll('.hear').forEach(function(b){ b.addEventListener('click',function(){ document.querySelectorAll('.hear').forEach(function(x){x.classList.remove('on');}); b.classList.add('on'); hear=+b.getAttribute('data-h'); setHear(); }); });
function fitZoom(){ var zb=document.querySelector('.zoom.on'); if(!zb) return; var z=+zb.getAttribute('data-z'); var sc=document.getElementById('scale'), rw=document.getElementById('rackwrap'), rk=document.getElementById('rack');
  sc.style.transform='scale('+z+')'; var W=rk.scrollWidth||1115; sc.style.width=W+'px';
  var cards=rk.querySelectorAll('.fxr-dev'); document.querySelectorAll('.bpslot').forEach(function(sl,i){ if(cards[i]) sl.style.width=cards[i].getBoundingClientRect().width/z+'px'; });
  rw.style.width=Math.min(window.innerWidth-44,Math.ceil(W*z)+2)+'px'; rw.style.height=((170+10+(document.querySelector('.bp.on')?236:0))*z+10)+'px'; }
document.querySelectorAll('.zoom').forEach(function(b){ b.addEventListener('click',function(){ document.querySelectorAll('.zoom').forEach(function(x){x.classList.remove('on');}); b.classList.add('on'); fitZoom(); }); });
(function(){ var fit=Math.max(1,Math.min(2,(window.innerWidth-44)/(document.getElementById('rack').scrollWidth||1115))); var best=null,bd=9; document.querySelectorAll('.zoom').forEach(function(b){ var z=+b.getAttribute('data-z'); if(z<=fit+0.01&&fit-z<bd){bd=fit-z;best=b;} }); (best||document.querySelector('.zoom')).click(); })();
window.addEventListener('resize',fitZoom);
</script></body></html>
"""

page = (PAGE.replace('@@THEME@@', theme).replace('@@CSS@@', css).replace('@@KNOB@@', knob)
            .replace('@@WK_EQZ@@', WORKLETS['eqz']).replace('@@WK_WID@@', WORKLETS['wid'])
            .replace('@@WK_CMP@@', WORKLETS['cmp']).replace('@@WK_OTT@@', WORKLETS['ott'])
            .replace('@@DEVICES@@', json.dumps(DEVICES))
            .replace('@@ICP@@', ICP).replace('@@ICA@@', ICA).replace('@@ICX@@', ICX)
            .replace('@@HELPERS@@', helpers).replace('@@CORES@@', cores)
            .replace('@@FMT@@', fmt).replace('@@FMT_HELPERS@@', fmt_helpers).replace('@@TICK@@', tick))
assert '@@' not in page, 'unfilled placeholder'
io.open(OUT, 'w', encoding='utf-8').write(page)
print('lifted: theme + %d chars CSS + knobSVG + fx4 helpers/cores/fmt/tick (%d/%d/%d/%d chars); wrote %s (%.1f KB)'
      % (len(css), len(helpers), len(cores), len(fmt), len(tick), OUT, len(page.encode('utf-8')) / 1024.0))
