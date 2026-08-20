#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fb436 — THE FX4 UI MOCKUP: the four cards (Equalizer · Widen · Compress · Multiband) with
their designed cores, live, audible, side by side in one rack strip.

Built the only way the house allows (LIFT THE CARD, NEVER RETYPE IT — fb395/fb399):
  * theme vars, every .fxr-/.dst-curve/mvBreathe rule, knobSVG() and the header glyphs are
    pulled VERBATIM from Source/ui/public/index.html through Design/fx3/mockup/fx3lift.py
  * the four worklets are the SAME algorithms the C++ engines run (Design/fx4/*/ *-worklet.js)
  * the card markup is devHTML()'s structure, with the four NEW cores in CORES — those four
    generators, fx4Tick(), the node/crossover drag handlers and the FX_FMT entries are written
    so they port into index.html as-is (same 226x78 viewBox, same X0/X1/CY, same class grammar).

    python3 build-fx4-ui.py   ->  fx4-ui-mockup.html
"""
import io, os, sys, json

HERE = os.path.dirname(os.path.abspath(__file__))
FX4  = os.path.dirname(HERE)
PLUG = os.path.dirname(os.path.dirname(FX4))
sys.path.insert(0, os.path.join(PLUG, 'Design', 'fx3', 'mockup'))
import fx3lift                                       # the shared lift (fb399)

OUT = os.path.join(HERE, 'fx4-ui-mockup.html')

theme  = fx3lift.theme_vars()
css    = fx3lift.card_css()
knob   = fx3lift.knob_svg()
ICP, ICA, ICX = fx3lift.glyphs()

def wk(rel):
    s = io.open(os.path.join(FX4, rel), encoding='utf-8').read()
    assert '</script' not in s.lower(), rel + ' contains </script'
    return s

WORKLETS = {
    'eqz': wk('eq/eq-worklet.js'),
    'wid': wk('widen/widen-worklet.js'),
    'cmp': wk('dynamics/compress-worklet.js'),
    'ott': wk('dynamics/ott-worklet.js'),
}

# ── the four devices, exactly as DEV_TEMPLATES ships them (fb426), plus the worklet keys ──
DEVICES = [
 dict(dev='Equalizer', core='eqz', proc='terrain-eq', pfx='eqz',
      types=['Surgical','British','American','Passive','Open','Dynamic','Chisel'],
      chars=['Plain','Tight','Broad','Steep','Scoop','Deep Pivot','Bright Pivot','Four Bells'],
      d2k='Focus', d2=['Stereo','Mid','Side','Left','Right'],
      knobs=[['Slant',50,'SLANT'],['Air',50,'AIR'],['Amount',50,'AMOUNT'],['Mix',100,'MIX']],
      keys=['f1','f2','f3','mix'],
      back=[['Low Hz',50,'LOWHZ'],['Low',50,'LOW'],['Body Hz',50,'BODYHZ'],['Body',50,'BODY'],
            ['Bite Hz',50,'BITEHZ'],['Bite',50,'BITE'],['Reach',50,'REACH'],['Trait',50,'TRAIT']],
      pills=[], pillKeys=[],
      # the P8 relabel per Type — the engine's own shapeName() table, mirrored
      trait=['Pinch','Slope','Taper','Dip','Silk','Pivot','Sting']),
 dict(dev='Widen', core='wid', proc='terrain-widen', pfx='wid',
      types=['Throng','Twin','Steady','Twofold','Blur','Bands'],
      chars=['JP Classic','Even Fan','Analog Drift','Tight Fan','Wide Fan','Octave Bloom','Sub Anchor','Three Phase'],
      d2k='Field', d2=['Straight','Alternate','Orbit','Swap','Side Only','Gather'],
      knobs=[['Detune',35,'AMOUNT'],['Width',50,'WIDTH'],['Rate',35,'RATE'],['Mix',50,'MIX']],
      keys=['amount','width','rate','mix'],
      back=[['Voices',50,'VOICES'],['Spread',85,'SPREAD'],['Offset',50,'OFFSET'],['Roam',0,'ROAM'],
            ['Low Keep',0,'LOWKEEP'],['Tone',50,'TONE'],['Feedback',0,'FEEDBACK'],['Balance',50,'BALANCE']],
      pills=[['Retrig',0],['Mono',0]], pillKeys=['retrig','hearMono'],
      # the hero relabel per Type — the engine's own frontNames(type), mirrored
      hero=['Detune','Depth','Cents','Sway','Wash','Cleave']),
 dict(dev='Compress', core='cmp', proc='terrain-compress', pfx='cmp',
      types=['Exact','Bus','FET 76','Opto','Vari-Mu','OverEasy','Ride','Limit'],
      chars=['Precise','Soft Touch','Loose Grip','Blunt','Deep Release','Line Attack','Poise','Judder'],
      d2k='Detect', d2=['Native','Peak','Average','Patient','Spike'],
      knobs=[['Push',20,'PUSH'],['Ratio',50,'RATIO'],['Lift',25,'LIFT'],['Mix',100,'MIX']],
      keys=['push','ratio','lift','mix'],
      back=[['Attack',61,'ATTACK'],['Release',63,'RELEASE'],['Round',25,'ROUND'],['Hear Cut',0,'HEARCUT'],
            ['Edge',50,'EDGE'],['Cling',0,'CLING'],['Tie',100,'TIE'],['Burn',0,'BURN']],
      pills=[['Auto',0]], pillKeys=['autoMakeup']),
 dict(dev='Multiband', core='ott', proc='terrain-ott', pfx='ott',
      types=['Over Top','Gentle','Heavy','Sheen','Bass Safe','Surge','Two Band','Stagger'],
      chars=['Straight Up','Sharp Ears','Long Ears','Wide Corner','One Detector','Slow Low','Twice Deep','Full Crest'],
      d2k='Stereo', d2=['Linked','Free Pair','Mid-Side'],
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
/* THE REAL CARD IS 272 x 170. `.fxr-dev` is `flex:0 0 272px; height:100%`, so the strip gives it
   a height; the wrapper scales the WHOLE strip with a transform so nothing inside is resized. */
#rackwrap{max-width:100%;overflow-x:auto;overflow-y:hidden;padding:6px 0 2px;scrollbar-width:none}
#rackwrap::-webkit-scrollbar{display:none}
#scale{transform-origin:top left;transform:scale(1.2)}
#rack{display:flex;gap:9px;height:170px;width:max-content}
#panels{display:flex;gap:9px;width:max-content;align-items:flex-start;margin-top:10px}
.bpslot{flex:0 0 auto}
@@CSS@@
/* the mockup's own scaffolding — NOT part of the lifted rack sheet */
.fxr-dev{position:relative}
.bp{display:none;border:1px solid var(--border-strong,#3a3356);border-radius:10px;
  background:rgba(24,20,38,.72);padding:10px 12px}
.bp.on{display:block}
.bp-row{display:flex;gap:10px;align-items:flex-end;margin-bottom:10px}
.bp-sel{display:flex;flex-direction:column;gap:4px;flex:1;min-width:0}
.bp-sel label{font:500 9px -apple-system,system-ui;letter-spacing:.08em;text-transform:uppercase;color:#8f88ab}
.bp-sel select{background:#1c1830;color:#e8e4f5;border:1px solid #3a3356;border-radius:6px;
  padding:3px 5px;font:400 10.5px -apple-system,system-ui;width:100%}
.bp-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px 0;position:relative}
.bp-k{display:flex;flex-direction:column;align-items:center;gap:4px}
.bp-k .fxr-lab{font:500 9px -apple-system,system-ui;color:#9a92b8}
.bp-sep{position:absolute;top:2%;bottom:2%;width:1px;background:linear-gradient(180deg,transparent,#3a3356 18%,#3a3356 82%,transparent)}
/* ── the four cores: every mark has a job. white = structure/controls, purple = live signal ── */
#syn-panel .fxr-core[data-core="eqz"], #syn-panel .fxr-core[data-core="ott"]{ cursor:default; }
#syn-panel .eqz-n{ cursor:grab; transition:r .12s; }
#syn-panel .eqz-n.hot{ stroke:var(--purple-400); }
#syn-panel .ott-xl{ cursor:ew-resize; }
#syn-panel .ott-xl.hot{ stroke:var(--purple-400); opacity:1; }
#syn-panel .fxr-core text{ font-family:-apple-system,'SF Pro Display',system-ui,sans-serif; font-weight:300; pointer-events:none; user-select:none; }
</style></head><body>
<h1>FX4 — Equalizer · Widen · Compress · Multiband</h1>
<p class="sub">The four cards with their cores designed and live. Real chassis, real engines — each card runs the same algorithm the C++ does.
Press <b>Start audio</b>, then <b>Play</b>. <b>Hear</b> picks which card reaches the speakers; all four always process, so all four move.
On the Equalizer drag a <b>band node</b> (X = Hz, Y = gain), scroll on it for <b>Trait</b>, double-click to reset. On Multiband drag a <b>crossover line</b>. Knobs: drag up/down, double-click resets.</p>
<div class="bar">
  <button id="go">Start audio</button>
  <button id="play">Play</button>
  <button id="src">Source: Saw chord</button>
  <span class="lbl">Hear</span>
  <button class="hear" data-h="0">Equalizer</button><button class="hear" data-h="1">Widen</button>
  <button class="hear" data-h="2">Compress</button><button class="hear on" data-h="3">Multiband</button><button class="hear" data-h="-1">Dry</button>
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

/* ═══════════════════════════════════════════════════════════════════════════════════
   THE DEVICE MODELS — shaped exactly like the rack's DEVS entries (fb346), one per card.
   ═══════════════════════════════════════════════════════════════════════════════════ */
var DEVS=DEVICES.map(function(t){ return {
  name:t.dev, core:t.core, inst:1, type:t.types[0], types:t.types, preset:'Init', on:true,
  pills:t.pills.map(function(p){return {t:p[0],on:!!p[1]};}),
  route:[true,false,false,false,false,false],
  knobs:t.knobs.map(function(k){return {l:k[0],v:k[1],p:'SYN_'+t.pfx.toUpperCase()+'_'+k[2],d:k[1]};}),
  back:{d1:{k:'Character',v:t.chars[0],opts:t.chars}, d2:{k:t.d2k,v:t.d2[0],opts:t.d2},
        knobs:t.back.map(function(b){return [b[0],b[1],'SYN_'+t.pfx.toUpperCase()+'_'+b[2],b[1]];})},
  typeIdx:0, charIdx:0, axisIdx:0, __t:t }; });

/* ═══════════════════════════════════════════════════════════════════════════════════
   READOUT LAW (fb363) — a knob whose value MEANS something prints the meaning, never a
   percent. Every map below is the ENGINE'S OWN (read from the worklet/C++), so a number on
   the card is a number in the DSP. Type-dependent numbers (Attack/Release/Ratio, the live
   crossover) are read from the device's viz push (d.__vz), because only the engine knows
   them. Keyed `core|SUFFIX`, exactly like the shipped FX_FMT, so this block ports verbatim.
   ═══════════════════════════════════════════════════════════════════════════════════ */
function fmtHz(hz){ return hz>=10000?Math.round(hz/1000)+'k':(hz>=1000?(hz/1000).toFixed(1)+'k':Math.round(hz)+''); }
function fmtDb(db){ var r=Math.round(db*10)/10; if(Math.abs(r)<0.05) return '0'; return (r>0?'+':'')+(Math.abs(r)<10?r.toFixed(1):Math.round(r)); }
function fmtDbS(db){ var r=Math.abs(db)>=3?Math.round(db):Math.round(db*10)/10; if(Math.abs(r)<0.05) return '0dB'; return (r>0?'+':'')+r+'dB'; }
function fmtMs(ms){ return ms>=1000?(ms/1000).toFixed(1)+'s':(ms<10?ms.toFixed(1):Math.round(ms))+'m'; }
function expMap(t,lo,hi){ return lo*Math.pow(hi/lo,Math.max(0,Math.min(1,t))); }
var FX4_FMT={
  /* EQUALIZER — bandHz() lo/hi per band, +-30 dB per band, +-24 dB Slant, Amount 0..200 % */
  'eqz|LOWHZ':function(v){ return fmtHz(expMap(v/100,20,500)); },
  'eqz|BODYHZ':function(v){ return fmtHz(expMap(v/100,100,3000)); },
  'eqz|BITEHZ':function(v){ return fmtHz(expMap(v/100,700,14000)); },
  'eqz|REACH':function(v){ return fmtHz(expMap(v/100,6000,40000)); },
  'eqz|LOW':function(v){ return fmtDb((v/100*2-1)*30); },
  'eqz|BODY':function(v){ return fmtDb((v/100*2-1)*30); },
  'eqz|BITE':function(v){ return fmtDb((v/100*2-1)*30); },
  'eqz|AIR':function(v){ return fmtDb((v/100*2-1)*30); },
  'eqz|SLANT':function(v){ var d=(v/100*2-1)*24; return Math.abs(d)<0.3?'flat':fmtDb(d); },
  'eqz|AMOUNT':function(v){ return Math.round(v*2)+'%'; },
  /* WIDEN — Width 50 is exactly neutral; Rate is the engine's 0.08 * 175^t Hz; Voices 3..8 */
  'wid|WIDTH':function(v){ var w=Math.round((v-50)*2); return w<=-99?'mono':(w===0?'0':(w>0?'+':'')+w); },
  'wid|RATE':function(v){ var hz=expMap(v/100,0.08,14); return (hz<10?hz.toFixed(2):Math.round(hz))+'hz'; },
  'wid|VOICES':function(v){ return (3+Math.round(v/100*5))+'v'; },
  /* COMPRESS — Push IS the threshold (9 - 48 t^0.9 dBp); the rest is Type-dependent: read the engine */
  'cmp|PUSH':function(v){ return fmtDbS(9-48*Math.pow(v/100,0.9)); },
  'cmp|RATIO':function(v,d){ var z=d&&d.__vz; var r=(z&&z.ratio!==undefined)?z.ratio:1/(1-Math.min(0.9999,Math.pow(v/100,0.85)));
    return (r>99||!isFinite(r))?'\\u221e:1':(r<10?r.toFixed(1):Math.round(r))+':1'; },
  'cmp|LIFT':function(v){ return fmtDbS(v/100*24); },
  'cmp|ATTACK':function(v,d){ var z=d&&d.__vz; return (z&&z.attackMs!==undefined)?fmtMs(z.attackMs):''+Math.round(v); },
  'cmp|RELEASE':function(v,d){ var z=d&&d.__vz; return (z&&z.releaseMs!==undefined)?fmtMs(z.releaseMs):''+Math.round(v); },
  'cmp|ROUND':function(v,d){ var z=d&&d.__vz; return (z&&z.kneeDb!==undefined)?Math.round(z.kneeDb)+'dB':''+Math.round(v); },
  /* MULTIBAND — the crossovers print the LIVE Hz (Type multipliers and Top Lift move them); trims +-12 dB; Grip +-18 */
  'ott|LOWCROSS':function(v,d){ var z=d&&d.__vz; return fmtHz(z&&z.xoverHz?z.xoverHz[0]:expMap(v/100,30,300)); },
  'ott|HIGHCROSS':function(v,d){ var z=d&&d.__vz; var x=z&&z.xoverHz?z.xoverHz[1]:0; return x>0?fmtHz(x):(z?'\\u2014':fmtHz(expMap(v/100,1000,8000))); },
  'ott|RAISE':function(v){ return (v/100*1.5).toFixed(1)+'x'; },
  'ott|PRESS':function(v){ return (v/100*1.5).toFixed(1)+'x'; },
  'ott|GRIP':function(v){ return fmtDbS((v/100-0.5)*36); },
  'ott|BASS':function(v){ return fmtDbS((v/100-0.5)*24); },
  'ott|MIDS':function(v){ return fmtDbS((v/100-0.5)*24); },
  'ott|TREBLE':function(v){ return fmtDbS((v/100-0.5)*24); }
};
/* SWITCH LAW — discrete things detent */
var FX4_STEP={ 'wid|VOICES':6 };
function fmtKey(d,p){ return d.core+'|'+p.slice(p.lastIndexOf('_')+1); }
function knobLabel(d,p,v){ var f=FX4_FMT[fmtKey(d,p)]; return f?f(v,d):null; }
function detent(d,p,v){ var n=FX4_STEP[fmtKey(d,p)]; if(!n) return v; var s=100/(n-1); return Math.round(v/s)*s; }

/* ═══════════════════════════════════════════════════════════════════════════════════
   THE FOUR CORES — SVG in the shared 226x78 viewBox, plot X 6.5..219.5, centre line y=39.
   The log-f axis (20 Hz..20 kHz over the plot) is SHARED by the Equalizer and the Multiband,
   so a crossover line and an EQ node at the same x mean the same frequency.
   ═══════════════════════════════════════════════════════════════════════════════════ */
var X0=6.5, X1=219.5, PW=X1-X0, CY=39, VH=78;
function hzX(hz){ return X0+PW*Math.log(Math.max(20,Math.min(20000,hz))/20)/Math.log(1000); }
function xHz(x){ return 20*Math.pow(1000,Math.max(0,Math.min(1,(x-X0)/PW))); }
function flatD(n){ var d=''; for(var i=0;i<=n;i++){ var x=X0+PW*i/n; d+=(i?'L':'M')+x.toFixed(1)+' '+CY; } return d; }
var CORES={
  /* ═══ EQUALIZER — THE LIVING CURVE. The filter card's spectrum grammar underneath (the same
     white 7 % fill), the house white line as the summed response, a purple glow under it that
     breathes with the audio (fx3's .*-glow precedent — never opacity on a .dst-curve, the
     strobe law), and FOUR NODES that sit ON the curve at the engine's own nodeHz/nodeDb. The
     nodes are controls, so they are white; the one you hold turns purple. ═══ */
  eqz:function(){
    var g=''; [100,1000,10000].forEach(function(hz){ var x=hzX(hz).toFixed(1); g+='<line x1="'+x+'" y1="4" x2="'+x+'" y2="74"/>'; });
    var n=''; for(var b=0;b<4;b++) n+='<circle class="eqz-n" data-b="'+b+'" cx="'+hzX([100,550,3100,15500][b]).toFixed(1)+'" cy="39" r="3.2" fill="rgba(18,16,30,0.88)" stroke="currentColor" stroke-width="1.1"/>';
    return '<svg viewBox="0 0 226 78" preserveAspectRatio="none">'
      +'<g stroke="rgba(255,255,255,0.07)" stroke-width="1">'+g+'</g>'
      +'<line x1="6.5" y1="39" x2="219.5" y2="39" stroke="rgba(255,255,255,0.13)" stroke-width="1"/>'
      +'<path class="eqz-spec" d="" fill="rgba(255,255,255,0.07)" stroke="rgba(255,255,255,0.34)" stroke-width="0.7"/>'
      +'<path class="eqz-fill" d="" fill="#B794FF" opacity="0.05"/>'
      +'<path class="eqz-glow" d="'+flatD(95)+'" fill="none" stroke="#B794FF" stroke-width="3.4" opacity="0"/>'
      +'<path class="eqz-curve dst-curve" d="'+flatD(95)+'"/>'
      +'<g class="eqz-nodes">'+n+'</g>'
      +'<text class="eqz-ro" x="10" y="11" font-size="7" fill="currentColor" opacity="0"></text>'
      +'</svg>';
  },
  /* ═══ WIDEN — THE VOICE FAN + THE CORRELATION RAIL. The dry centre is a white beam; every
     wet copy is a purple beam fanned by its LIVE pan and leaning by its LIVE cents (it sways
     with the engine's own LFO/walk). The rail below is the stereo correlation: needle at r,
     the dashed half is the mono-danger zone (r < 0). Mono pill: the fan closes. ═══ */
  wid:function(){
    var v=''; for(var i=0;i<8;i++) v+='<line class="wid-v" data-v="'+i+'" x1="113" y1="70" x2="113" y2="70" stroke="#B794FF" stroke-width="1.1" stroke-linecap="round" opacity="0"/>';
    return '<svg viewBox="0 0 226 78" preserveAspectRatio="none">'
      +'<line x1="113" y1="70" x2="113" y2="12" stroke="currentColor" stroke-width="1" opacity=".22"/>'
      +v
      +'<line class="wid-dry" x1="113" y1="70" x2="113" y2="14" stroke="currentColor" stroke-width="1.2" stroke-linecap="round" opacity=".55"/>'
      +'<line x1="40" y1="75" x2="113" y2="75" stroke="currentColor" stroke-width="1" opacity=".18" stroke-dasharray="1.5 2.5"/>'
      +'<line x1="113" y1="75" x2="186" y2="75" stroke="currentColor" stroke-width="1" opacity=".22"/>'
      +'<line x1="113" y1="73" x2="113" y2="77" stroke="currentColor" stroke-width="1" opacity=".35"/>'
      +'<line class="wid-r" x1="186" y1="72" x2="186" y2="78" stroke="#B794FF" stroke-width="1.6" stroke-linecap="round" opacity=".8"/>'
      +'</svg>';
  },
  /* ═══ COMPRESS — THE PRESS RIVER + THE KNEE. Left: the last ~2.5 s of input (dim outline)
     and output (white body) scrolling; the gap between them is the crushed dB, tinted purple;
     a ceiling bar at the threshold presses down by the current gain reduction. Right: the
     transfer curve the engine publishes (white), the 1:1 diagonal dashed, a purple ball
     riding the curve at the live input level. One number: the gain reduction. ═══ */
  cmp:function(){
    return '<svg viewBox="0 0 226 78" preserveAspectRatio="none">'
      +'<line x1="6.5" y1="75" x2="138" y2="75" stroke="rgba(255,255,255,0.13)" stroke-width="1"/>'
      +'<path class="cmp-gap" d="" fill="#B794FF" opacity=".22"/>'
      +'<path class="cmp-out" d="" fill="rgba(255,255,255,0.06)" stroke="none"/>'
      +'<path class="cmp-in" d="" fill="none" stroke="currentColor" stroke-width=".8" opacity=".42"/>'
      +'<path class="cmp-outl dst-curve" d="M6.5 75L138 75"/>'
      +'<rect class="cmp-press" x="6.5" y="20" width="131.5" height="0" fill="#B794FF" opacity=".35"/>'
      +'<line class="cmp-thr" x1="6.5" y1="20" x2="138" y2="20" stroke="currentColor" stroke-width="1" opacity=".5"/>'
      +'<text class="cmp-gr" x="136" y="11" font-size="7.5" text-anchor="end" fill="currentColor" opacity=".75">0.0</text>'
      +'<line x1="146" y1="75" x2="219.5" y2="4" stroke="currentColor" stroke-width="1" opacity=".14" stroke-dasharray="2 3"/>'
      +'<line x1="146" y1="75" x2="219.5" y2="75" stroke="rgba(255,255,255,0.13)" stroke-width="1"/>'
      +'<line x1="146" y1="75" x2="146" y2="4" stroke="rgba(255,255,255,0.13)" stroke-width="1"/>'
      +'<path class="cmp-knee dst-curve" d="M146 75L219.5 4"/>'
      +'<circle class="cmp-ball" cx="146" cy="75" r="2.6" fill="#B794FF" opacity=".9"/>'
      +'</svg>';
  },
  /* ═══ MULTIBAND — THE JAWS, on the log-f axis. Three lanes split at the LIVE crossovers (two
     lanes on Two Band); per lane a purple level column rides the band's level, a ceiling slab
     rests at the downward threshold and PRESSES DOWN by the reduction, a floor slab rests at the
     upward threshold and RISES by the lift — the purple parts of each slab are the dB being
     eaten or poured, right now. The crossover lines are draggable. ═══ */
  ott:function(){
    var lanes=''; for(var b=0;b<3;b++){
      lanes+='<g class="ott-lane" data-b="'+b+'">'
        +'<rect class="ott-bite-dn" x="0" y="4" width="0" height="0" fill="#B794FF" opacity=".34"/>'
        +'<rect class="ott-bite-up" x="0" y="75" width="0" height="0" fill="#B794FF" opacity=".34"/>'
        +'<rect class="ott-col" x="0" y="75" width="0" height="0" fill="#B794FF" opacity=".3"/>'
        +'<line class="ott-rest-dn" x1="0" y1="0" x2="0" y2="0" stroke="currentColor" stroke-width="1" opacity=".16" stroke-dasharray="1.5 2.5"/>'
        +'<line class="ott-rest-up" x1="0" y1="0" x2="0" y2="0" stroke="currentColor" stroke-width="1" opacity=".16" stroke-dasharray="1.5 2.5"/>'
        +'<line class="ott-edge-dn" x1="0" y1="0" x2="0" y2="0" stroke="currentColor" stroke-width="1.1" opacity=".72"/>'
        +'<line class="ott-edge-up" x1="0" y1="0" x2="0" y2="0" stroke="currentColor" stroke-width="1.1" opacity=".72"/>'
        +'</g>'; }
    var xl=''; for(var i=0;i<2;i++) xl+='<line class="ott-xl" data-x="'+i+'" x1="60" y1="3" x2="60" y2="76" stroke="currentColor" stroke-width="1" opacity=".45"/>';
    return '<svg viewBox="0 0 226 78" preserveAspectRatio="none">'
      +'<line x1="6.5" y1="75" x2="219.5" y2="75" stroke="rgba(255,255,255,0.13)" stroke-width="1"/>'
      +lanes+xl
      +'<text class="ott-ro" x="10" y="11" font-size="7" fill="currentColor" opacity="0"></text>'
      +'</svg>';
  }
};

/* devHTML() — structure-for-structure as the plugin emits it */
function devHTML(d,idx){
  var pills=d.pills.map(function(p,i){return '<span class="fxr-pill'+(p.on?' fxr-on':'')+'" data-pi="'+i+'"><span class="fxr-t">'+p.t+'</span></span>';}).join('');
  var route=d.route.map(function(on,i){return '<span class="fxr-r'+(on?' fxr-on':'')+'" data-r="'+i+'"><span class="fxr-t">'+RLBL[i]+'</span></span>';}).join('');
  var knobs=d.knobs.map(function(k,i){return '<div class="fxr-knob"><div class="fxr-dial" data-k="'+i+'">'+knobSVG(k.v,knobLabel(d,k.p,k.v),null)+'</div><span class="fxr-lab">'+k.l+'</span></div>';}).join('');
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
/* the OFFICIAL back panel (fb275): 2 dropdowns + 8 knobs 4x2 + 3 column separators */
function backHTML(d,idx){
  var ks=d.back.knobs.map(function(b,i){return '<div class="bp-k"><div class="fxr-dial" data-b="'+i+'">'+knobSVG(b[1],knobLabel(d,b[2],b[1]),null)+'</div><span class="fxr-lab">'+b[0]+'</span></div>';}).join('');
  var seps=''; for(var s=1;s<4;s++) seps+='<div class="bp-sep" style="left:'+(s*25)+'%"></div>';
  return '<div class="bp" data-bp="'+idx+'"><div class="bp-row">'
    +'<div class="bp-sel"><label>'+d.back.d1.k+'</label><select data-csel="'+idx+'">'+d.back.d1.opts.map(function(c){return '<option>'+c+'</option>';}).join('')+'</select></div>'
    +'<div class="bp-sel"><label>'+d.back.d2.k+'</label><select data-dsel="'+idx+'">'+d.back.d2.opts.map(function(c){return '<option>'+c+'</option>';}).join('')+'</select></div>'
    +'</div><div class="bp-grid">'+seps+ks+'</div></div>';
}
function render(){
  document.getElementById('rack').innerHTML=DEVS.map(devHTML).join('');
  document.getElementById('panels').innerHTML=DEVS.map(function(d,i){ return '<div class="bpslot">'+backHTML(d,i)+'</div>'; }).join('');
}
render();

/* redraw one device's knobs from the MODEL (drag-from-model law, fb364) */
function redrawKnobs(d){
  var idx=DEVS.indexOf(d), card=document.querySelector('.fxr-dev[data-dev="'+idx+'"]');
  if(card){ card.querySelectorAll('.fxr-dial[data-k]').forEach(function(el){ var i=+el.getAttribute('data-k'); var k=d.knobs[i];
    el.innerHTML=knobSVG(k.v,knobLabel(d,k.p,k.v),null); var lab=el.parentNode.querySelector('.fxr-lab'); if(lab&&lab.textContent!==k.l) lab.textContent=k.l; }); }
  var bp=document.querySelector('.bp[data-bp="'+idx+'"]');
  if(bp){ bp.querySelectorAll('.fxr-dial[data-b]').forEach(function(el){ var i=+el.getAttribute('data-b'); var b=d.back.knobs[i];
    el.innerHTML=knobSVG(b[1],knobLabel(d,b[2],b[1]),null); var lab=el.parentNode.querySelector('.fxr-lab'); if(lab&&lab.textContent!==b[0]) lab.textContent=b[0]; }); }
}
/* per-Type relabels — the engine's own tables (frontNames / shapeName), never a card-side guess */
function applyType(d,ti){
  d.typeIdx=ti; d.type=d.types[ti];
  if(d.core==='wid'&&d.__t.hero) d.knobs[0].l=d.__t.hero[ti];
  if(d.core==='eqz'&&d.__t.trait) d.back.knobs[7][0]=d.__t.trait[ti];
  var idx=DEVS.indexOf(d), card=document.querySelector('.fxr-dev[data-dev="'+idx+'"]');
  if(card){ var tl=card.querySelector('.fxr-tl'); if(tl) tl.firstChild.nodeValue=d.type; }
  redrawKnobs(d);
}

/* ── event delegation: knobs (front+back), type/char/axis, pills, power, swap ── */
(function(){
  var drag=null;
  function devOf(el){ var card=el.closest('.fxr-dev'); if(card) return DEVS[+card.getAttribute('data-dev')]; var bp=el.closest('.bp'); return bp?DEVS[+bp.getAttribute('data-bp')]:null; }
  document.addEventListener('pointerdown',function(e){
    var dial=e.target.closest&&e.target.closest('.fxr-dial'); if(!dial) return;
    var d=devOf(dial); if(!d) return;
    var k=dial.getAttribute('data-k'), b=dial.getAttribute('data-b');
    var get=(k!=null)?function(){return d.knobs[+k].v;}:function(){return d.back.knobs[+b][1];};
    drag={d:d,dial:dial,k:k,b:b,y0:e.clientY,v0:get()}; dial.setPointerCapture(e.pointerId); e.preventDefault();
  });
  document.addEventListener('pointermove',function(e){ if(!drag) return;
    var v=Math.max(0,Math.min(100,drag.v0+(drag.y0-e.clientY)*0.6)), d=drag.d;
    if(drag.k!=null){ var kk=d.knobs[+drag.k]; v=detent(d,kk.p,v); kk.v=v; push(d,d.__t.keys[+drag.k],v/100); }
    else { var bb=d.back.knobs[+drag.b]; v=detent(d,bb[2],v); bb[1]=v; push(d,'b'+(+drag.b+1),v/100); }
    redrawKnobs(d);
  });
  document.addEventListener('pointerup',function(){ drag=null; });
  /* double-click -> the template default (fb363 rule 3) */
  document.addEventListener('dblclick',function(e){
    var dial=e.target.closest&&e.target.closest('.fxr-dial'); if(!dial) return; var d=devOf(dial); if(!d) return;
    var k=dial.getAttribute('data-k'), b=dial.getAttribute('data-b');
    if(k!=null){ d.knobs[+k].v=d.knobs[+k].d; push(d,d.__t.keys[+k],d.knobs[+k].v/100); }
    else { d.back.knobs[+b][1]=d.back.knobs[+b][3]; push(d,'b'+(+b+1),d.back.knobs[+b][1]/100); }
    redrawKnobs(d);
  });
  document.addEventListener('change',function(e){
    var t=e.target;
    if(t.hasAttribute('data-tsel')){ var d=DEVS[+t.getAttribute('data-tsel')]; applyType(d,t.selectedIndex); d.charIdx=0; push(d,'type',t.selectedIndex,true); push(d,'character',0,true); var cs=document.querySelector('select[data-csel="'+DEVS.indexOf(d)+'"]'); if(cs) cs.selectedIndex=0; }
    if(t.hasAttribute('data-csel')){ var d2=DEVS[+t.getAttribute('data-csel')]; d2.charIdx=t.selectedIndex; d2.back.d1.v=d2.back.d1.opts[t.selectedIndex]; push(d2,'character',t.selectedIndex,true); }
    if(t.hasAttribute('data-dsel')){ var d3=DEVS[+t.getAttribute('data-dsel')]; d3.axisIdx=t.selectedIndex; d3.back.d2.v=d3.back.d2.opts[t.selectedIndex]; push(d3,'axis',t.selectedIndex,true); }
  });
  document.addEventListener('click',function(e){
    var card=e.target.closest&&e.target.closest('.fxr-dev'); if(!card) return; var d=DEVS[+card.getAttribute('data-dev')];
    var pill=e.target.closest('.fxr-pill'); if(pill){ var pi=+pill.getAttribute('data-pi'); d.pills[pi].on=!d.pills[pi].on; pill.classList.toggle('fxr-on',d.pills[pi].on); push(d,d.__t.pillKeys[pi],d.pills[pi].on?1:0,true); return; }
    var r=e.target.closest('.fxr-r'); if(r){ r.classList.toggle('fxr-on'); return; }
    if(e.target.closest('.fxr-pwr')){ d.on=!d.on; card.classList.toggle('fxr-off',!d.on); setPower(d); return; }
    if(e.target.closest('.fxr-swap')){ var bp=document.querySelector('.bp[data-bp="'+DEVS.indexOf(d)+'"]'); if(bp) bp.classList.toggle('on'); return; }
  });
})();

/* ═══════════════════════════════════════════════════════════════════════════════════
   THE EQUALIZER'S NODES — the Pro-Q grammar (R12), recycled in spirit from the panel EQ:
   drag X = that band's Hz knob, drag Y = its gain knob (DELTA from the grab, so the node never
   jumps), wheel = Trait (the Type's shape slot), double-click = reset the band. A press that
   misses every node grabs the band that OWNS that part of the axis (Low / Body / Bite / Air are
   ordered), so dragging anywhere on the curve always does something sensible.
   ═══════════════════════════════════════════════════════════════════════════════════ */
var EQZ_LO=[20,100,700,6000], EQZ_HI=[500,3000,14000,40000];
(function(){
  var drag=null;
  function unit(svg,e){ var r=svg.getBoundingClientRect(); return {x:(e.clientX-r.left)/r.width*226, y:(e.clientY-r.top)/r.height*78}; }
  function gainRef(d,b){ return b<3?{get:function(){return d.back.knobs[2*b+1][1];},set:function(v){d.back.knobs[2*b+1][1]=v; push(d,'b'+(2*b+2),v/100);}}
                                 :{get:function(){return d.knobs[1].v;},set:function(v){d.knobs[1].v=v; push(d,'f2',v/100);}}; }
  function hzRef(d,b){ return {set:function(v){ d.back.knobs[2*b][1]=v; push(d,'b'+(2*b+1),v/100); }}; }
  function pick(core,u){
    var nodes=core.querySelectorAll('.eqz-n'), best=-1, bd=1e9;
    nodes.forEach(function(n){ var dx=u.x-(+n.getAttribute('cx')), dy=u.y-(+n.getAttribute('cy')); var dd=Math.sqrt(dx*dx+dy*dy); if(dd<bd){bd=dd;best=+n.getAttribute('data-b');} });
    if(bd<=12) return best;
    var hz=xHz(u.x); if(hz<170) return 0; if(hz<1500) return 1; if(hz<7000) return 2; return 3;   // the owner of that part of the axis
  }
  function readout(core,d,b){
    var t=core.querySelector('.eqz-ro'); if(!t) return;
    var z=d.__vz, hz=z&&z.nodeHz?z.nodeHz[b]:0, db=z&&z.nodeDb?z.nodeDb[b]:0;
    t.textContent=['Low','Body','Bite','Air'][b]+'  '+fmtHz(hz)+'  '+fmtDb(db); t.setAttribute('opacity','.8');
    clearTimeout(d.__roT); d.__roT=setTimeout(function(){ t.setAttribute('opacity','0'); },900);
  }
  document.addEventListener('pointerdown',function(e){
    var core=e.target.closest&&e.target.closest('.fxr-core[data-core="eqz"]'); if(!core) return;
    var card=core.closest('.fxr-dev'), d=DEVS[+card.getAttribute('data-dev')], svg=core.querySelector('svg');
    var u=unit(svg,e), b=pick(core,u);
    drag={core:core,d:d,b:b,svg:svg,y0:u.y,g0:gainRef(d,b).get()};
    core.querySelectorAll('.eqz-n').forEach(function(n){ n.classList.toggle('hot',+n.getAttribute('data-b')===b); });
    try{ core.setPointerCapture(e.pointerId); }catch(err){}
    e.preventDefault(); move(e);
  });
  function move(e){ if(!drag) return; var u=unit(drag.svg,e), d=drag.d, b=drag.b;
    var hz=xHz(u.x), t=Math.log(hz/EQZ_LO[b])/Math.log(EQZ_HI[b]/EQZ_LO[b]);
    hzRef(d,b).set(Math.max(0,Math.min(100,t*100)));
    var dDb=-(u.y-drag.y0)/1.1;                          // 1.1 units per dB on the plot
    gainRef(d,b).set(Math.max(0,Math.min(100,drag.g0+dDb/30*50)));
    redrawKnobs(d); readout(drag.core,d,b);
  }
  document.addEventListener('pointermove',move);
  document.addEventListener('pointerup',function(){ if(drag){ drag.core.querySelectorAll('.eqz-n').forEach(function(n){n.classList.remove('hot');}); } drag=null; });
  document.addEventListener('wheel',function(e){
    var core=e.target.closest&&e.target.closest('.fxr-core[data-core="eqz"]'); if(!core) return;
    var d=DEVS[+core.closest('.fxr-dev').getAttribute('data-dev')]; e.preventDefault();
    var v=Math.max(0,Math.min(100,d.back.knobs[7][1]-e.deltaY*0.15)); d.back.knobs[7][1]=v; push(d,'b8',v/100); redrawKnobs(d);
    var t=core.querySelector('.eqz-ro'); if(t){ t.textContent=d.back.knobs[7][0]+'  '+Math.round(v); t.setAttribute('opacity','.8'); clearTimeout(d.__roT); d.__roT=setTimeout(function(){t.setAttribute('opacity','0');},900); }
  },{passive:false});
  document.addEventListener('dblclick',function(e){
    var core=e.target.closest&&e.target.closest('.fxr-core[data-core="eqz"]'); if(!core) return;
    var d=DEVS[+core.closest('.fxr-dev').getAttribute('data-dev')], b=pick(core,unit(core.querySelector('svg'),e));
    hzRef(d,b).set(d.back.knobs[2*b][3]); gainRef(d,b).set(b<3?d.back.knobs[2*b+1][3]:d.knobs[1].d); redrawKnobs(d);
  });
})();

/* ═══ THE MULTIBAND'S CROSSOVERS — drag a lane boundary, the Low/High Cross knob moves ═══ */
(function(){
  var drag=null;
  function unit(svg,e){ var r=svg.getBoundingClientRect(); return {x:(e.clientX-r.left)/r.width*226, y:(e.clientY-r.top)/r.height*78}; }
  document.addEventListener('pointerdown',function(e){
    var core=e.target.closest&&e.target.closest('.fxr-core[data-core="ott"]'); if(!core) return;
    var d=DEVS[+core.closest('.fxr-dev').getAttribute('data-dev')], svg=core.querySelector('svg'), u=unit(svg,e);
    var best=-1,bd=1e9; core.querySelectorAll('.ott-xl').forEach(function(l){ if(l.getAttribute('opacity')==='0') return; var dx=Math.abs(u.x-(+l.getAttribute('x1'))); if(dx<bd){bd=dx;best=+l.getAttribute('data-x');} });
    if(best<0||bd>9) return;
    drag={core:core,d:d,i:best,svg:svg}; core.querySelectorAll('.ott-xl').forEach(function(l){ l.classList.toggle('hot',+l.getAttribute('data-x')===best); });
    try{ core.setPointerCapture(e.pointerId); }catch(err){} e.preventDefault(); move(e);
  });
  function move(e){ if(!drag) return; var u=unit(drag.svg,e), d=drag.d, hz=xHz(u.x);
    var two=d.type==='Two Band', lo=drag.i===0?(two?150:30):1000, hi=drag.i===0?(two?2000:300):8000;
    var t=Math.max(0,Math.min(1,Math.log(hz/lo)/Math.log(hi/lo)));
    d.back.knobs[drag.i][1]=t*100; push(d,'b'+(drag.i+1),t); redrawKnobs(d);
    var ro=drag.core.querySelector('.ott-ro'); if(ro){ ro.textContent=(drag.i?'High Cross  ':'Low Cross  ')+fmtHz(hz); ro.setAttribute('opacity','.8'); clearTimeout(d.__roT); d.__roT=setTimeout(function(){ro.setAttribute('opacity','0');},900); }
  }
  document.addEventListener('pointermove',move);
  document.addEventListener('pointerup',function(){ if(drag) drag.core.querySelectorAll('.ott-xl').forEach(function(l){l.classList.remove('hot');}); drag=null; });
})();

/* ═══════════════════════════════════════════════════════════════════════════════════
   THE FEED + THE TICK. FEED is shaped exactly like the plugin's window.__fx4VizPush will be:
   { eqz:[6], wid:[6], cmp:[6], ott:[6] } — an entry per instance, null when not in the chain.
   Here each worklet's onmessage fills FEED[core][0]. fx4Tick() is ONE rAF loop for all four
   cards (fb413: never a setInterval that free-runs against the compositor). Everything drawn
   glides toward the feed — dB linearly, Hz in LOG space (the fb163 constant) — so the picture
   moves like water and nothing sample-and-holds. Idle = dim, playing = bright (fb311).
   ═══════════════════════════════════════════════════════════════════════════════════ */
var FEED={eqz:[null],wid:[null],cmp:[null],ott:[null]};
function gl(o,k,t,a){ if(o[k]==null||!isFinite(o[k])) o[k]=t; o[k]+=a*(t-o[k]); return o[k]; }
function glLog(o,k,t,a){ t=Math.max(1e-6,t); if(o[k]==null||!isFinite(o[k])) o[k]=t; o[k]=Math.exp(Math.log(o[k])+a*(Math.log(t)-Math.log(o[k]))); return o[k]; }
function c01(v){ return Math.max(0,Math.min(1,v||0)); }
function setA(el,k,v){ if(el) el.setAttribute(k,v); }

/* EQUALIZER */
function dbY(db){ return Math.max(3,Math.min(75,CY-db*1.1)); }
function effDb(c,m){ if(m>=0.999) return c; var lin=(1-m)+m*Math.pow(10,c/20); return 20*Math.log10(Math.max(1e-6,Math.abs(lin))); }
var specBuf=null;
function drawEqz(core,dev,f){
  var st=dev.__st||(dev.__st={}); var N=96; if(!st.c){ st.c=new Float32Array(N); }
  st.g=gl(st,'g',f?c01(f.lvl):0,0.18);
  setA(core.querySelector('.eqz-glow'),'opacity',(0.05+0.30*st.g).toFixed(3));
  setA(core.querySelector('.eqz-fill'),'opacity',(0.04+0.12*st.g).toFixed(3));
  var mix=c01((dev.knobs[3].v||0)/100), src=(f&&f.curve&&f.curve.length===N)?f.curve:null, d='';
  for(var i=0;i<N;i++){ var tv=src?src[i]:0; if(!isFinite(tv)) tv=0; st.c[i]+=0.45*(tv-st.c[i]);
    d+=(i?'L':'M')+(X0+PW*i/(N-1)).toFixed(1)+' '+dbY(effDb(st.c[i],mix)).toFixed(1); }
  var curve=core.querySelector('.eqz-curve'); if(curve&&curve.getAttribute('d')!==d){ curve.setAttribute('d',d); setA(core.querySelector('.eqz-glow'),'d',d); setA(core.querySelector('.eqz-fill'),'d',d+'L'+X1+' '+CY+'L'+X0+' '+CY+'Z'); }
  var nodes=core.querySelectorAll('.eqz-n');
  for(var b=0;b<4;b++){ var hz=glLog(st,'hz'+b,f&&f.nodeHz?f.nodeHz[b]:[100,550,3100,15500][b],0.5), db=gl(st,'db'+b,f&&f.nodeDb?f.nodeDb[b]:0,0.5);
    setA(nodes[b],'cx',Math.max(X0,Math.min(X1,hzX(hz))).toFixed(1)); setA(nodes[b],'cy',dbY(effDb(db,mix)).toFixed(1)); }
  /* the audio — the post-device spectrum, per pixel column on the same log axis (the filter card's grammar) */
  var spec=core.querySelector('.eqz-spec');
  if(spec&&dev.__an){ var an=dev.__an; if(!specBuf||specBuf.length!==an.frequencyBinCount) specBuf=new Float32Array(an.frequencyBinCount);
    an.getFloatFrequencyData(specBuf); var sr=ac?ac.sampleRate:48000, sd='', M=96, top=-14, bot=-96, H=VH-4;
    for(var j=0;j<=M;j++){ var u=j/M, hz2=20*Math.pow(1000,u), bb=hz2/(sr/2)*specBuf.length, i0=Math.floor(bb), fr=bb-i0;
      var v0=specBuf[Math.min(specBuf.length-1,i0)], v1=specBuf[Math.min(specBuf.length-1,i0+1)]; var db2=(isFinite(v0)?v0:-200)*(1-fr)+(isFinite(v1)?v1:-200)*fr;
      var y=2+H*(1-(db2-bot)/(top-bot)); sd+=(j?'L':'M')+(X0+PW*u).toFixed(1)+' '+Math.max(2,Math.min(VH-2,y)).toFixed(1); }
    sd+='L'+X1+' '+(VH-2)+'L'+X0+' '+(VH-2)+'Z'; spec.setAttribute('d',sd); }
}

/* MULTIBAND */
function dbpY(db){ return Math.max(4,Math.min(75,4+(6-db)*(71/72))); }
function drawOtt(core,dev,f){
  var st=dev.__st||(dev.__st={}); st.g=gl(st,'g',f?c01(f.lvl):0,0.18);
  var nb=f&&f.bands?f.bands:3;
  var xlo=glLog(st,'xlo',f&&f.xoverHz?f.xoverHz[0]:120,0.3), xhi=glLog(st,'xhi',(f&&f.xoverHz&&f.xoverHz[1]>0)?f.xoverHz[1]:2500,0.3);
  var bounds=[X0,hzX(xlo),nb===3?hzX(xhi):X1,X1];
  var lanes=core.querySelectorAll('.ott-lane');
  for(var b=0;b<3;b++){ var L=lanes[b]; if(b>=nb){ L.setAttribute('opacity','0'); continue; } L.setAttribute('opacity','1');
    var lx0=bounds[b], lx1=bounds[b+1], lw=lx1-lx0, cx=(lx0+lx1)/2;
    var lev=gl(st,'lv'+b,f&&f.bandDb?Math.max(-66,f.bandDb[b]):-66,0.35);
    var gr=gl(st,'gr'+b,f&&f.grDb?f.grDb[b]:0,0.3);
    var tdn=gl(st,'td'+b,f&&f.tdn?f.tdn[b]:-20,0.2), tup=gl(st,'tu'+b,f&&f.tup?Math.min(f.tup[b],f.tdn?f.tdn[b]:0):-40,0.2);
    var yLev=dbpY(lev), yTdn=dbpY(tdn), yTup=dbpY(tup), dn=Math.max(0,gr), up=Math.max(0,-gr);
    var yDn=dbpY(tdn-dn), yUp=dbpY(tup+up);
    var col=L.querySelector('.ott-col'); setA(col,'x',(cx-lw*0.2).toFixed(1)); setA(col,'width',Math.max(2,lw*0.4).toFixed(1)); setA(col,'y',yLev.toFixed(1)); setA(col,'height',Math.max(0,75-yLev).toFixed(1)); setA(col,'opacity',(0.18+0.62*st.g).toFixed(3));
    var sx=(cx-lw*0.36).toFixed(1), sw=Math.max(2,lw*0.72).toFixed(1);
    var bdn=L.querySelector('.ott-bite-dn'); setA(bdn,'x',sx); setA(bdn,'width',sw); setA(bdn,'y',yTdn.toFixed(1)); setA(bdn,'height',Math.max(0,yDn-yTdn).toFixed(1));
    var bup=L.querySelector('.ott-bite-up'); setA(bup,'x',sx); setA(bup,'width',sw); setA(bup,'y',yUp.toFixed(1)); setA(bup,'height',Math.max(0,yTup-yUp).toFixed(1));
    var rdn=L.querySelector('.ott-rest-dn'); setA(rdn,'x1',sx); setA(rdn,'x2',(+sx+ +sw).toFixed(1)); setA(rdn,'y1',yTdn.toFixed(1)); setA(rdn,'y2',yTdn.toFixed(1)); setA(rdn,'opacity',dn>0.3?'.22':'0');
    var rup=L.querySelector('.ott-rest-up'); setA(rup,'x1',sx); setA(rup,'x2',(+sx+ +sw).toFixed(1)); setA(rup,'y1',yTup.toFixed(1)); setA(rup,'y2',yTup.toFixed(1)); setA(rup,'opacity',up>0.3?'.22':'0');
    var edn=L.querySelector('.ott-edge-dn'); setA(edn,'x1',sx); setA(edn,'x2',(+sx+ +sw).toFixed(1)); setA(edn,'y1',yDn.toFixed(1)); setA(edn,'y2',yDn.toFixed(1));
    var eup=L.querySelector('.ott-edge-up'); setA(eup,'x1',sx); setA(eup,'x2',(+sx+ +sw).toFixed(1)); setA(eup,'y1',yUp.toFixed(1)); setA(eup,'y2',yUp.toFixed(1));
  }
  var xl=core.querySelectorAll('.ott-xl');
  setA(xl[0],'x1',bounds[1].toFixed(1)); setA(xl[0],'x2',bounds[1].toFixed(1));
  if(nb===3){ setA(xl[1],'x1',bounds[2].toFixed(1)); setA(xl[1],'x2',bounds[2].toFixed(1)); if(!xl[1].classList.contains('hot')) setA(xl[1],'opacity','.45'); }
  else setA(xl[1],'opacity','0');
}

/* COMPRESS */
function rivY(db){ return Math.max(4,Math.min(75,4+(6-db)*(71/66))); }
function drawCmp(core,dev,f){
  var st=dev.__st||(dev.__st={h:[]}); st.g=gl(st,'g',f?c01(f.lvl):0,0.18);
  var inDb=f?Math.max(-60,Math.min(6,f.inDb)):-60, outDb=f?Math.max(-60,Math.min(6,f.outDb)):-60, gr=f?Math.max(0,f.grDb):0;
  st.h.push([inDb,outDb]); if(st.h.length>150) st.h.shift();
  var RW=131.5, n=st.h.length, pin='', pout='', gap='';
  for(var i=0;i<n;i++){ var x=X0+RW*i/149; var yi=rivY(st.h[i][0]), yo=rivY(st.h[i][1]);
    pin+=(i?'L':'M')+x.toFixed(1)+' '+yi.toFixed(1); pout+=(i?'L':'M')+x.toFixed(1)+' '+yo.toFixed(1); }
  for(var j=n-1;j>=0;j--){ var x2=X0+RW*j/149; gap+='L'+x2.toFixed(1)+' '+rivY(st.h[j][1]).toFixed(1); }
  var xe=(X0+RW*(n-1)/149).toFixed(1);
  setA(core.querySelector('.cmp-in'),'d',pin);
  setA(core.querySelector('.cmp-outl'),'d',pout);
  setA(core.querySelector('.cmp-out'),'d',pout+'L'+xe+' 75L'+X0+' 75Z');
  setA(core.querySelector('.cmp-gap'),'d',pin+gap+'Z'); setA(core.querySelector('.cmp-gap'),'opacity',(0.12+0.30*st.g).toFixed(3));
  var thr=gl(st,'thr',f&&f.thrDb!==undefined?f.thrDb:-3,0.3), yT=rivY(thr), grs=gl(st,'gr',gr,0.35);
  var tl=core.querySelector('.cmp-thr'); setA(tl,'y1',yT.toFixed(1)); setA(tl,'y2',yT.toFixed(1));
  var pr=core.querySelector('.cmp-press'); setA(pr,'y',yT.toFixed(1)); setA(pr,'height',Math.min(75-yT,grs*(71/66)).toFixed(1));
  var t=core.querySelector('.cmp-gr'); if(t){ var s=(grs>0.05?'\\u2212'+grs.toFixed(1):'0.0'); if(t.textContent!==s) t.textContent=s; t.setAttribute('opacity',(0.35+0.45*st.g).toFixed(2)); }
  /* the knee: input -60..+12 dBp across x 146..219.5, output -60..+12 up y 75..4 */
  var KW=73.5, kd='', kn=(f&&f.knee&&f.knee.length===32)?f.knee:null;
  if(!st.k) st.k=new Float32Array(32);
  for(var q=0;q<32;q++){ var tin=-60+72*q/31, tv=kn?kn[q]:tin; if(!isFinite(tv)) tv=tin; st.k[q]+=0.4*(tv-st.k[q]);
    kd+=(q?'L':'M')+(146+KW*q/31).toFixed(1)+' '+(4+71*(1-(Math.max(-60,Math.min(12,st.k[q]))+60)/72)).toFixed(1); }
  var kp=core.querySelector('.cmp-knee'); if(kp&&kp.getAttribute('d')!==kd) kp.setAttribute('d',kd);
  var bi=gl(st,'bi',inDb,0.5), qq=(Math.max(-60,Math.min(12,bi))+60)/72*31, q0=Math.floor(qq), q1=Math.min(31,q0+1), fr=qq-q0;
  var bo=st.k[q0]*(1-fr)+st.k[q1]*fr;
  var ball=core.querySelector('.cmp-ball'); setA(ball,'cx',(146+KW*qq/31).toFixed(1)); setA(ball,'cy',(4+71*(1-(Math.max(-60,Math.min(12,bo))+60)/72)).toFixed(1)); setA(ball,'opacity',(0.25+0.65*st.g).toFixed(2));
}

/* WIDEN */
function drawWid(core,dev,f){
  var st=dev.__st||(dev.__st={}); st.g=gl(st,'g',f?c01(f.lvl):0,0.18);
  var nV=f&&f.nV?f.nV:6, beams=core.querySelectorAll('.wid-v'), len=54*(0.70+0.30*st.g);
  for(var v=0;v<8;v++){ var el=beams[v]; if(v>=nV){ setA(el,'opacity','0'); continue; }
    var pan=gl(st,'p'+v,f&&f.voicePan?f.voicePan[v]:0,0.25), cents=gl(st,'c'+v,f&&f.voiceCents?f.voiceCents[v]:0,0.25);
    var a=pan*0.78, tx=113+Math.sin(a)*len+cents/100*5, ty=70-Math.cos(a)*len;
    setA(el,'x2',tx.toFixed(1)); setA(el,'y2',ty.toFixed(1)); setA(el,'opacity',(0.16+0.64*st.g).toFixed(3)); }
  setA(core.querySelector('.wid-dry'),'opacity',(0.30+0.40*st.g).toFixed(3));
  var r=gl(st,'r',f&&f.corr!==undefined?f.corr:1,0.2), rx=(113+73*Math.max(-1,Math.min(1,r))).toFixed(1);
  var nd=core.querySelector('.wid-r'); setA(nd,'x1',rx); setA(nd,'x2',rx);
}

function fx4Tick(){
  var cards=document.querySelectorAll('.fxr-dev');
  for(var c=0;c<cards.length;c++){ var dev=DEVS[+cards[c].getAttribute('data-dev')]; if(!dev) continue;
    var core=cards[c].querySelector('.fxr-core[data-core="'+dev.core+'"]'); if(!core) continue;
    var bank=FEED[dev.core], f=bank?bank[(dev.inst||1)-1]:null;
    if(dev.core==='eqz') drawEqz(core,dev,f); else if(dev.core==='ott') drawOtt(core,dev,f);
    else if(dev.core==='cmp') drawCmp(core,dev,f); else if(dev.core==='wid') drawWid(core,dev,f); }
  requestAnimationFrame(fx4Tick);
}
requestAnimationFrame(fx4Tick);
/* refresh the engine-fed readouts (Attack/Release/Ratio/crossovers) at a gentle rate */
setInterval(function(){ DEVS.forEach(function(d){ if(d.__vz&&(d.core==='cmp'||d.core==='ott')) redrawKnobs(d); }); },400);

/* ═══ AUDIO — one context, one -26 dBFS source, four engines in PARALLEL, Hear picks the ear ═══ */
var ac=null,srcN=null,playing=false,kind=0,hear=3,dryG=null,master=null;
var KINDS=['Saw chord','Sine 220','Noise','Pluck','Drums','Pad','Bell','Sweep'];
function fail(m){ document.getElementById('err').textContent=m; document.getElementById('status').textContent='Failed to start.'; }
async function addWorklet(code){
  try{ await ac.audioWorklet.addModule(URL.createObjectURL(new Blob([code],{type:'text/javascript'}))); return 'blob'; }
  catch(e1){ try{ await ac.audioWorklet.addModule('data:text/javascript;base64,'+btoa(unescape(encodeURIComponent(code)))); return 'data'; }
    catch(e2){ throw new Error('addModule failed.\\nblob: '+e1.message+'\\ndata: '+e2.message); } }
}
function push(d,k,v,raw){ if(!d.__node||k==null) return; var m={}; m[k]=v; d.__node.port.postMessage(m); }
function setPower(d){ if(!d.__wet) return; var t=ac.currentTime; d.__wet.gain.setTargetAtTime(d.on?1:0,t,0.02); d.__dry.gain.setTargetAtTime(d.on?0:1,t,0.02); }
function setHear(){ if(!ac) return; var t=ac.currentTime; DEVS.forEach(function(d,i){ if(d.__hear) d.__hear.gain.setTargetAtTime(hear===i?1:0,t,0.03); }); if(dryG) dryG.gain.setTargetAtTime(hear===-1?1:0,t,0.03); }
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
    if(d.core==='eqz'){ d.__an=ac.createAnalyser(); d.__an.fftSize=2048; d.__an.smoothingTimeConstant=0.55; d.__wet.connect(d.__an); }
    (function(dd){ dd.__node.port.onmessage=function(e){ var z=e.data||{}; dd.__vz=z; FEED[dd.core][0]=z; }; })(d);
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
  /* fb431 — normalise to the -26 dBFS bus every threshold is calibrated for */
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
document.querySelectorAll('.zoom').forEach(function(b){ b.addEventListener('click',function(){ document.querySelectorAll('.zoom').forEach(function(x){x.classList.remove('on');}); b.classList.add('on');
  var z=+b.getAttribute('data-z'); var sc=document.getElementById('scale'), rw=document.getElementById('rackwrap'), rk=document.getElementById('rack');
  sc.style.transform='scale('+z+')'; var W=rk.scrollWidth||1115; sc.style.width=W+'px';
  /* each back-panel slot sits under ITS card: same width as the card above it */
  var cards=rk.querySelectorAll('.fxr-dev'); document.querySelectorAll('.bpslot').forEach(function(sl,i){ if(cards[i]) sl.style.width=cards[i].getBoundingClientRect().width/z+'px'; });
  rw.style.width=Math.min(window.innerWidth-44,Math.ceil(W*z)+2)+'px'; rw.style.height=((170+10+(document.querySelector('.bp.on')?236:0))*z+10)+'px'; }); });
(function(){ var fit=Math.max(1,Math.min(2,(window.innerWidth-44)/(document.getElementById('rack').scrollWidth||1115))); var best=null,bd=9; document.querySelectorAll('.zoom').forEach(function(b){ var z=+b.getAttribute('data-z'); if(z<=fit+0.01&&fit-z<bd){bd=fit-z;best=b;} }); (best||document.querySelector('.zoom')).click(); })();
new MutationObserver(function(){ document.querySelector('.zoom.on').click(); }).observe(document.getElementById('panels'),{attributes:true,subtree:true,attributeFilter:['class']});
window.addEventListener('resize',function(){ document.querySelector('.zoom.on').click(); });
</script></body></html>
"""

page = (PAGE.replace('@@THEME@@', theme).replace('@@CSS@@', css).replace('@@KNOB@@', knob)
            .replace('@@WK_EQZ@@', WORKLETS['eqz']).replace('@@WK_WID@@', WORKLETS['wid'])
            .replace('@@WK_CMP@@', WORKLETS['cmp']).replace('@@WK_OTT@@', WORKLETS['ott'])
            .replace('@@DEVICES@@', json.dumps(DEVICES))
            .replace('@@ICP@@', ICP).replace('@@ICA@@', ICA).replace('@@ICX@@', ICX))
assert '@@' not in page, 'unfilled placeholder'
io.open(OUT, 'w', encoding='utf-8').write(page)
print('lifted theme + %d chars of rack CSS + knobSVG; wrote %s (%.1f KB)' % (len(css), OUT, len(page.encode('utf-8')) / 1024.0))
