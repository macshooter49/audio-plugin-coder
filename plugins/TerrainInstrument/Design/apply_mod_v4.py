#!/usr/bin/env python3
# apply_mod_v4.py — replace the colored v7 mod-engine with the transparent v4 design.
# Self-healing + idempotent. Splices the CSS block (sentinel -> #mod-panel) and the
# mod-engine <script>. Wires LFO 1: shape / sync / div / rate(Hz) / depth — all live.
import sys

CSS_SENT = '/* ===== MOD V4 (transparent LFO) ===== */'
OLD_CSS_SENT = '/* ===== Modulation Engine (color LFO workstation) ===== */'
JS_SENT = '/* MOD V4 component */'
OLD_JS_MARK = 'Terrain Modulation Engine v7'

NEW_CSS = r'''/* ===== MOD V4 (transparent LFO) ===== */
#syn-panel .device.mod{padding:0;overflow:hidden;}
#mod-engine{position:absolute;inset:0;border-radius:13px;overflow:hidden;background:var(--bg-hero);
  font-family:-apple-system,BlinkMacSystemFont,'SF Pro Text','Helvetica Neue',sans-serif;
  --ghost:rgba(255,255,255,0.55);--ghost-dim:rgba(255,255,255,0.34);--gray:#9b93a8;}
#mod-engine .mv-scope{position:absolute;inset:0;}
#mod-engine .mv-scope svg{position:absolute;inset:0;width:100%;height:100%;}
#mod-engine .mv-grid line{stroke:rgba(139,92,246,.14);stroke-width:.5;}
#mod-engine .mv-grid line.mid{stroke:rgba(139,92,246,.22);}
#mod-engine .mv-play{stroke:rgba(236,232,242,.42);stroke-width:1;}
#mod-engine .mv-fill{fill:rgba(236,232,242,.07);}
#mod-engine .mv-stroke{fill:none;stroke:#ECE8F2;stroke-width:1.7;}
#mod-engine .mv-foll{fill:#A78BFA;}
#mod-engine .mv-ov{position:absolute;left:0;right:0;display:flex;align-items:center;z-index:2;}
#mod-engine .mv-ov.top{top:6px;padding:0 11px;justify-content:flex-end;}
#mod-engine .mv-ov.bot{bottom:6px;padding:0 13px;justify-content:space-between;}
#mod-engine .mv-c{display:flex;align-items:center;gap:5px;font-size:9px;letter-spacing:1.1px;font-weight:300;color:var(--ghost);cursor:pointer;transition:color .12s;text-shadow:0 1px 3px rgba(0,0,0,.9);-webkit-user-select:none;user-select:none;}
#mod-engine .mv-c:hover{color:#fff;}
#mod-engine .mv-c.drag{cursor:ns-resize;}
#mod-engine .mv-c .ar{font-size:8px;opacity:.55;}
#mod-engine .mv-c svg.chev{width:8px;height:8px;opacity:.6;}
#mod-engine .mv-c .ic{display:inline-flex;width:20px;height:11px;color:inherit;}
#mod-engine .mv-seg{font-size:9px;letter-spacing:1px;font-weight:300;text-shadow:0 1px 3px rgba(0,0,0,.9);}
#mod-engine .mv-seg b{cursor:pointer;font-weight:300;} #mod-engine .mv-seg .on{color:#fff;} #mod-engine .mv-seg .off{color:var(--ghost-dim);}
#mod-engine .mv-exp{width:14px;height:14px;cursor:pointer;color:var(--ghost);transition:.12s;filter:drop-shadow(0 1px 3px rgba(0,0,0,.9));}
#mod-engine .mv-exp:hover{color:#fff;}
#mod-engine .mv-tabs{display:flex;align-items:center;gap:8px;}
#mod-engine .mv-tabs .t{position:relative;font-size:10px;font-weight:400;color:var(--ghost-dim);cursor:pointer;transition:.12s;text-shadow:0 1px 3px rgba(0,0,0,.9);}
#mod-engine .mv-tabs .t:hover{color:var(--ghost);} #mod-engine .mv-tabs .t.act{color:#fff;font-weight:600;}
#mod-engine .mv-tabs .t.act::after{content:'';position:absolute;left:-1px;right:-1px;bottom:-3px;height:1.5px;background:var(--purple-400);border-radius:1px;}
#mod-engine .mv-tabs .t.dis{opacity:.5;cursor:default;}
#mod-engine .mv-dep{display:flex;align-items:center;gap:5px;text-shadow:0 1px 3px rgba(0,0,0,.9);}
#mod-engine .mv-dep .l{font-size:7.5px;letter-spacing:1px;color:var(--ghost-dim);}
#mod-engine .mv-dep .v{font-size:9px;color:var(--gray);font-variant-numeric:tabular-nums;min-width:24px;}
#mod-engine .mv-ring{width:17px;height:17px;border-radius:50%;cursor:ns-resize;
  -webkit-mask:radial-gradient(closest-side,transparent 60%,#000 62%);mask:radial-gradient(closest-side,transparent 60%,#000 62%);}

/* body-level glass menu */
.mv-menu{position:fixed;z-index:2147483646;background:rgba(24,24,38,0.46);-webkit-backdrop-filter:blur(16px) saturate(1.4);backdrop-filter:blur(16px) saturate(1.4);
  border:1px solid rgba(255,255,255,0.10);border-radius:10px;box-shadow:0 16px 40px rgba(0,0,0,.5),inset 0 1px 0 rgba(255,255,255,.06);
  padding:5px;display:none;font-family:-apple-system,'SF Pro Text',sans-serif;max-height:228px;overflow:auto;}
.mv-menu.open{display:block;}
.mv-menu div{display:flex;align-items:center;gap:9px;padding:6px 12px 6px 8px;border-radius:6px;cursor:pointer;color:rgba(255,255,255,.55);font-size:9.5px;letter-spacing:1px;white-space:nowrap;transition:.1s;}
.mv-menu div:hover{background:rgba(255,255,255,0.08);color:#fff;}
.mv-menu div.cur{color:#B794FF;} .mv-menu div.cur:hover{color:#fff;}
.mv-menu div svg{width:22px;height:12px;flex:none;opacity:.9;}

/* body-level extender */
.mv-ext{position:fixed;z-index:2147483645;width:316px;background:#2A2A48;border:1px solid rgba(140,130,180,0.45);border-radius:12px;
  box-shadow:0 18px 50px rgba(0,0,0,.6);overflow:hidden;display:none;font-family:-apple-system,'SF Pro Text',sans-serif;}
.mv-ext.open{display:block;}
.mv-ext .h{display:flex;align-items:center;gap:9px;padding:9px 12px;border-bottom:1px solid rgba(58,58,88,0.55);cursor:grab;background:rgba(255,255,255,.025);}
.mv-ext .h.grab{cursor:grabbing;}
.mv-ext .h .g{letter-spacing:2px;color:#4a4f68;font-size:10px;} .mv-ext .h .tt{font-size:9px;letter-spacing:1.8px;color:#C5BFD2;text-transform:uppercase;font-weight:600;}
.mv-ext .h .tg{font-size:11px;color:#fff;font-weight:600;} .mv-ext .h .x{margin-left:auto;color:#908599;cursor:pointer;font-size:13px;}
.mv-ext .b{padding:11px 13px 13px;display:flex;flex-direction:column;gap:11px;}
.mv-ext .gl{font-size:8px;letter-spacing:1.4px;color:#6E6580;margin-bottom:6px;}
.mv-ext .es{background:#12121F;border-radius:9px;position:relative;overflow:hidden;height:110px;cursor:crosshair;}
.mv-ext .es svg{position:absolute;inset:0;width:100%;height:100%;}
.mv-ext .es .eh{position:absolute;bottom:6px;right:9px;font-size:7.5px;color:rgba(255,255,255,.34);letter-spacing:.5px;}
.mv-ext .es .nd{fill:#fff;stroke:rgba(167,139,250,.6);stroke-width:1;} .mv-ext .es .gl2{stroke:rgba(139,92,246,.14);stroke-width:.5;} .mv-ext .es .st{fill:none;stroke:#ECE8F2;stroke-width:1.9;} .mv-ext .es .fl{fill:rgba(236,232,242,.08);}
.mv-ext .g3{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;} .mv-ext .g4{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;}
.mv-ext .pill{height:24px;padding:0 9px;background:#12121F;border:1px solid rgba(140,130,180,0.45);border-radius:7px;display:flex;align-items:center;justify-content:center;gap:6px;font-size:9px;letter-spacing:1px;color:#ECE8F2;font-weight:300;cursor:pointer;}
.mv-ext .pill .k{color:#908599;font-size:7px;} .mv-ext .pill .ic{width:16px;height:9px;color:#B794FF;display:inline-flex;}
.mv-ext .tog{height:24px;border:1px solid rgba(140,130,180,0.45);border-radius:7px;display:flex;align-items:center;justify-content:center;font-size:8.5px;font-weight:500;letter-spacing:.6px;color:#908599;cursor:pointer;}
.mv-ext .tog.on{background:#191726;border-color:#4a3f6e;color:#B794FF;}
.mv-ext .mo{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;}
.mv-ext .mk{display:flex;flex-direction:column;align-items:center;gap:4px;}
.mv-ext .dn{width:30px;height:30px;border-radius:50%;-webkit-mask:radial-gradient(closest-side,transparent 64%,#000 66%);mask:radial-gradient(closest-side,transparent 64%,#000 66%);}
.mv-ext .mk .kl{font-size:7.5px;letter-spacing:.6px;color:#C5BFD2;} .mv-ext .mk .kv{font-size:7px;color:#908599;}

.mv-err{position:absolute;left:8px;bottom:6px;z-index:9;font-size:9px;color:#ff6b6b;background:rgba(0,0,0,.5);padding:2px 6px;border-radius:4px;display:none;}'''

NEW_JS = r'''<script>
/* MOD V4 component */
(function(){
  if(window.__modv4)return; window.__modv4=true;
  var SHORT=['Sine','Tri','Saw','Saw↓','Sqr','S&H','Rand'];      // idx -> LFO1_SHAPE choice (no needless caps; S&H mandatory)
  var NSHAPE=7;
  var DIVS=['8 bar','4 bar','2 bar','1 bar','1/2','1/4','1/8','1/16','1/32','1/4.','1/8.','1/4T','1/8T','1/16T'];
  var NDIV=DIVS.length;
  var SH=[ .65,.2,.85,.4,.95,.15,.7,.35 ], RP=[ .5,.85,.15,.7,.3,.95,.2,.6,.4,.5 ];
  var CUSTOM=[[0,.5],[.18,.92],[.4,.2],[.62,.78],[.82,.35],[1,.5]];
  function smooth(a,t){var f=t*(a.length-1),i=Math.floor(f),fr=f-i,x=a[i],y=a[Math.min(i+1,a.length-1)],m=(1-Math.cos(fr*Math.PI))/2;return x+(y-x)*m;}
  function yfor(k,t){var c=3,p=(t*c)%1;
    if(k===0)return Math.sin(t*Math.PI*2*c); if(k===1)return p<0.5?(p*4-1):(3-p*4);
    if(k===2)return p*2-1; if(k===3)return 1-p*2; if(k===4)return p<0.5?1:-1;
    if(k===5){var i=Math.floor(t*SH.length)%SH.length;return SH[i]*2-1;}
    if(k===6)return smooth(RP,t)*2-1; return 0;}
  function waveSVG(w,h,k,amp,mid,phOff){mid=mid||h/2;phOff=phOff||0;var n=200,d='';for(var i=0;i<=n;i++){var t=i/n;d+=(i?'L':'M')+(t*w).toFixed(1)+' '+(mid-yfor(k,(t+phOff)%1)*amp).toFixed(1)+' ';}
    var fillD='M0 '+mid+' '+d.replace(/^M/,'L')+'L'+w+' '+mid+' Z';
    var gh=[.25,.5,.75].map(function(g){return '<line class="'+(g===0.5?'mid':'')+'" x1="0" y1="'+(h*g).toFixed(1)+'" x2="'+w+'" y2="'+(h*g).toFixed(1)+'"/>';}).join('');
    var gv=[.1,.2,.3,.4,.5,.6,.7,.8,.9].map(function(g){return '<line class="'+(g===0.5?'mid':'')+'" x1="'+(w*g).toFixed(1)+'" y1="0" x2="'+(w*g).toFixed(1)+'" y2="'+h+'"/>';}).join('');
    return '<svg viewBox="0 0 '+w+' '+h+'" preserveAspectRatio="none"><g class="mv-grid">'+gh+gv+'</g><path class="mv-fill" d="'+fillD+'"/><path class="mv-stroke" d="'+d+'"/>'+
      '<line class="mv-play" id="mv-ph" x1="0" y1="0" x2="0" y2="'+h+'"/><circle class="mv-foll" id="mv-fd" cx="0" cy="'+mid+'" r="2.4"/></svg>';}
  function icon(k){var w=20,h=11,mid=h/2,n=60,d='';for(var i=0;i<=n;i++){var t=i/n;d+=(i?'L':'M')+(t*w).toFixed(1)+' '+(mid-yfor(k,t)*(h*0.34)).toFixed(1)+' ';}return '<svg viewBox="0 0 '+w+' '+h+'" fill="none" stroke="currentColor" stroke-width="1.3"><path d="'+d+'"/></svg>';}
  var CHEV='<svg class="chev" viewBox="0 0 10 10" fill="none" stroke="currentColor" stroke-width="1.4"><path d="M2 4l3 3 3-3"/></svg>';
  var EXPAND='<svg class="mv-exp" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M6 2 H2 V6 M14 6 V2 H10 M10 14 H14 V10 M2 10 V14 H6"/></svg>';
  var RINGBG=function(v){return 'conic-gradient(from 220deg,var(--knob-track) 140deg,var(--purple-400) 140deg '+(140+(v-.5)*280)+'deg,var(--knob-track) 0 280deg,transparent 0)';};
  var DONBG=function(v){return 'conic-gradient(from 220deg,#B794FF '+(v*280)+'deg,rgba(120,118,160,0.35) 0 280deg,transparent 0)';};

  function ssGet(id){try{return (window.Juce&&window.Juce.getSliderState)?window.Juce.getSliderState(id):null;}catch(e){return null;}}
  var LP={}, st={shape:0,sync:true,div:5,rateHz:2,depth:.35,phase:0,tab:1}, ph=0, lastT=0, root, errEl;
  function cur(){ return LP[st.tab]||{}; }
  function err(m){if(errEl){errEl.textContent=m;errEl.style.display='block';}if(window.console)console.error('mod-v4:',m);}

  function pull(){ var C=cur();
    if(C.shape) st.shape=Math.round(C.shape.getNormalisedValue()*(NSHAPE-1));
    if(C.sync)  st.sync =C.sync.getNormalisedValue()>0.5;
    if(C.div)   st.div  =Math.round(C.div.getNormalisedValue()*(NDIV-1));
    if(C.rate)  st.rateHz=C.rate.getScaledValue();
    if(C.depth) st.depth=C.depth.getScaledValue();
    if(C.phase) st.phase=C.phase.getScaledValue();
  }
  function setNorm(s,v){if(s){try{s.setNormalisedValue(Math.max(0,Math.min(1,v)));}catch(e){}}}

  var AMP=23, MID=40;
  function front(){
    var rate = st.sync
      ? '<span class="mv-c" id="mv-div">'+DIVS[st.div]+' '+CHEV+'</span>'
      : '<span class="mv-c drag" id="mv-rate"><span id="mv-rv">'+st.rateHz.toFixed(2)+'</span> Hz <span class="ar">⇅</span></span>';
    var dv=(st.depth>=0?'+':'')+Math.round(st.depth*100);
    var tabs='<div class="mv-tabs">'+[1,2,3,4,5].map(function(n){return '<span class="t'+(n===st.tab?' act':'')+'" data-tab="'+n+'">'+n+'</span>';}).join('')+'</div>';
    return '<div class="mv-scope">'+waveSVG(456,96,st.shape,AMP,MID,st.phase)+'</div>'
      +'<div class="mv-ov top">'+EXPAND+'</div>'
      +'<div class="mv-ov bot">'
        +'<span class="mv-c" id="mv-shape"><span class="ic">'+icon(st.shape)+'</span> '+SHORT[st.shape]+' '+CHEV+'</span>'
        +rate
        +'<span class="mv-seg"><b class="'+(st.sync?'on':'off')+'" id="mv-bpm">BPM</b> · <b class="'+(st.sync?'off':'on')+'" id="mv-hz">Hz</b></span>'
        +'<span class="mv-c drag" id="mv-shift" title="slide the LFO"><svg viewBox="0 0 14 10" width="13" height="9" style="opacity:.8" fill="none" stroke="currentColor" stroke-width="1.4"><path d="M4 3 L1 5 L4 7 M10 3 L13 5 L10 7 M1 5 H13"/></svg> <span id="mv-shv">'+Math.round(st.phase*360)+'°</span></span>'
        +tabs
        +'<div class="mv-dep"><span class="l">Dep</span><div class="mv-ring" id="mv-depth" style="background:'+RINGBG((st.depth+1)/2)+'"></div><span class="v">'+dv+'</span></div>'
      +'</div>'
      +'<div class="mv-err"></div>';
  }
  function render(){ root.innerHTML=front(); errEl=root.querySelector('.mv-err'); bindFront(); }

  // ---- body-level menu (glass) ----
  var menu=document.createElement('div'); menu.className='mv-menu'; document.body.appendChild(menu);
  function openMenu(items,cur,atEl,cb){
    menu.innerHTML=items.map(function(it,i){return '<div class="'+(i===cur?'cur':'')+'" data-i="'+i+'">'+(it.ic||'')+'<span>'+it.t+'</span></div>';}).join('');
    var r=atEl.getBoundingClientRect();
    menu.style.left=r.left+'px'; menu.style.top=''; menu.style.bottom=(window.innerHeight-r.top+6)+'px';
    menu.classList.add('open');
    menu.querySelectorAll('div[data-i]').forEach(function(d){ d.onclick=function(){ cb(+d.getAttribute('data-i')); menu.classList.remove('open'); }; });
  }
  document.addEventListener('mousedown',function(e){ if(menu.classList.contains('open') && !menu.contains(e.target)) menu.classList.remove('open'); }, true);

  function bindFront(){
    var sb=root.querySelector('#mv-shape');
    if(sb)sb.onclick=function(){ openMenu(SHORT.map(function(s,i){return {t:s,ic:icon(i)};}), st.shape, sb, function(i){ setNorm(cur().shape,i/(NSHAPE-1)); st.shape=i; render(); }); };
    var dv=root.querySelector('#mv-div');
    if(dv)dv.onclick=function(){ openMenu(DIVS.map(function(s){return {t:s};}), st.div, dv, function(i){ setNorm(cur().div,i/(NDIV-1)); st.div=i; render(); }); };
    var bpm=root.querySelector('#mv-bpm'), hz=root.querySelector('#mv-hz');
    if(bpm)bpm.onclick=function(){ setNorm(cur().sync,1); st.sync=true; render(); };
    if(hz)hz.onclick=function(){ setNorm(cur().sync,0); st.sync=false; render(); };
    var rt=root.querySelector('#mv-rate');
    if(rt)rt.onmousedown=function(e){ e.preventDefault(); var R=cur().rate, y0=e.clientY, v0=R?R.getNormalisedValue():0;
      function mv(ev){ var nv=Math.max(0,Math.min(1,v0+(y0-ev.clientY)*0.005)); setNorm(R,nv); if(R){st.rateHz=R.getScaledValue(); var rv=root.querySelector('#mv-rv'); if(rv)rv.textContent=st.rateHz.toFixed(2);} }
      function up(){ document.removeEventListener('mousemove',mv); document.removeEventListener('mouseup',up); }
      document.addEventListener('mousemove',mv); document.addEventListener('mouseup',up); };
    var dp=root.querySelector('#mv-depth');
    if(dp)dp.onmousedown=function(e){ e.preventDefault(); var D=cur().depth, y0=e.clientY, v0=D?D.getNormalisedValue():.5;
      function mv(ev){ var nv=Math.max(0,Math.min(1,v0+(y0-ev.clientY)*0.005)); setNorm(D,nv); if(D){st.depth=D.getScaledValue(); render();} }
      function up(){ document.removeEventListener('mousemove',mv); document.removeEventListener('mouseup',up); }
      document.addEventListener('mousemove',mv); document.addEventListener('mouseup',up); };
    var sf=root.querySelector('#mv-shift');
    if(sf)sf.onmousedown=function(e){ e.preventDefault(); var P=cur().phase, x0=e.clientX, v0=P?P.getNormalisedValue():0;
      function mv(ev){ var nv=((v0+(ev.clientX-x0)*0.004)%1+1)%1; setNorm(P,nv); if(P){st.phase=P.getScaledValue(); var sv=root.querySelector('#mv-shv'); if(sv)sv.textContent=Math.round(st.phase*360)+'°'; var sc=root.querySelector('.mv-scope'); if(sc)sc.innerHTML=waveSVG(456,96,st.shape,AMP,MID,st.phase);} }
      function up(){ document.removeEventListener('mousemove',mv); document.removeEventListener('mouseup',up); }
      document.addEventListener('mousemove',mv); document.addEventListener('mouseup',up); };
    root.querySelectorAll('.mv-tabs .t').forEach(function(t){ t.onclick=function(){ st.tab=+t.getAttribute('data-tab'); pull(); render(); }; });
    var ex=root.querySelector('.mv-exp'); if(ex)ex.onclick=openExt;
  }

  // ---- toast ----
  var to=document.createElement('div'); to.className='mv-toast'; document.body.appendChild(to); var toT;
  function toast(m){ to.textContent=m; to.style.opacity='1'; clearTimeout(toT); toT=setTimeout(function(){to.style.opacity='0';},1400); }

  // ---- extender (body-level, draggable) ----
  var ext=document.createElement('div'); ext.className='mv-ext'; document.body.appendChild(ext); var extPos=null;
  function editScope(w,h){var N=CUSTOM,d='';N.forEach(function(p,i){var x=p[0]*w,y=h-6-p[1]*(h-12);if(i===0)d+='M'+x+' '+y+' ';else{var pv=N[i-1],cx=(pv[0]+p[0])/2*w;d+='C'+cx+' '+(h-6-pv[1]*(h-12))+' '+cx+' '+y+' '+x+' '+y+' ';}});
    var fillD=d+'L'+w+' '+h+' L0 '+h+' Z';
    var gh=[.25,.5,.75].map(function(g){return '<line class="gl2" x1="0" y1="'+(h*g).toFixed(0)+'" x2="'+w+'" y2="'+(h*g).toFixed(0)+'"/>';}).join('');
    var gv=[.2,.4,.6,.8].map(function(g){return '<line class="gl2" x1="'+(w*g).toFixed(0)+'" y1="0" x2="'+(w*g).toFixed(0)+'" y2="'+h+'"/>';}).join('');
    var dots=N.map(function(p){return '<circle class="nd" cx="'+(p[0]*w).toFixed(0)+'" cy="'+(h-6-p[1]*(h-12)).toFixed(0)+'" r="3.4"/>';}).join('');
    return '<svg viewBox="0 0 '+w+' '+h+'" preserveAspectRatio="none">'+gh+gv+'<path class="fl" d="'+fillD+'"/><path class="st" d="'+d+'"/></svg><svg viewBox="0 0 '+w+' '+h+'" style="position:absolute;inset:0">'+dots+'</svg>';}
  function MK(l,v,n){return '<div class="mk"><div class="dn" style="background:'+DONBG(n)+'"></div><div class="kl">'+l+'</div><div class="kv">'+v+'</div></div>';}
  function buildExt(){
    ext.innerHTML='<div class="h"><span class="g">⠿</span><span class="tt">LFO</span><span class="tg">'+st.tab+'</span><span class="x">✕</span></div>'
      +'<div class="b">'
      +'<div class="es">'+editScope(290,110)+'<span class="eh">drag nodes → CUSTOM (Stage 4)</span></div>'
      +'<div><div class="gl">Rate</div><div class="g3"><div class="pill" id="ex-shape"><span class="ic">'+icon(st.shape)+'</span>'+SHORT[st.shape]+CHEV+'</div><div class="pill" id="ex-div"><span class="k">Sync</span>'+DIVS[st.div]+CHEV+'</div><div class="pill" id="ex-unit"><span class="k">Unit</span>'+(st.sync?'BPM':'Hz')+CHEV+'</div></div></div>'
      +'<div><div class="gl">Behaviour</div><div class="g4"><div class="pill"><span class="k">Trig</span>Free</div><div class="pill"><span class="k">Dir</span>Up</div><div class="tog">Trip</div><div class="tog on">Mono</div></div></div>'
      +'<div><div class="gl">Motion</div><div class="mo">'+MK('Rise','12ms',.3)+MK('Delay','0',0)+MK('Smooth','30%',.45)+MK('Phase','0°',.5)+'</div></div>'
      +'</div>';
    ext.querySelector('.x').onclick=function(){ext.classList.remove('open');};
    var sh=ext.querySelector('#ex-shape'); if(sh)sh.onclick=function(){ openMenu(SHORT.map(function(s,i){return {t:s,ic:icon(i)};}), st.shape, sh, function(i){ setNorm(cur().shape,i/(NSHAPE-1)); st.shape=i; buildExt(); render(); }); };
    var ed=ext.querySelector('#ex-div'); if(ed)ed.onclick=function(){ if(!st.sync){toast('switch to BPM to use divisions');return;} openMenu(DIVS.map(function(s){return {t:s};}), st.div, ed, function(i){ setNorm(cur().div,i/(NDIV-1)); st.div=i; buildExt(); render(); }); };
    var eu=ext.querySelector('#ex-unit'); if(eu)eu.onclick=function(){ st.sync=!st.sync; setNorm(cur().sync, st.sync?1:0); buildExt(); render(); };
    // drag header
    var h=ext.querySelector('.h'); h.onmousedown=function(e){ if(e.target.classList.contains('x'))return; e.preventDefault(); h.classList.add('grab'); var r=ext.getBoundingClientRect(); var dx=e.clientX-r.left, dy=e.clientY-r.top;
      function mv(ev){ extPos={x:Math.max(4,Math.min(window.innerWidth-ext.offsetWidth-4,ev.clientX-dx)),y:Math.max(4,Math.min(window.innerHeight-40,ev.clientY-dy))}; ext.style.left=extPos.x+'px'; ext.style.top=extPos.y+'px'; }
      function up(){ h.classList.remove('grab'); document.removeEventListener('mousemove',mv); document.removeEventListener('mouseup',up); }
      document.addEventListener('mousemove',mv); document.addEventListener('mouseup',up); };
  }
  function openExt(){ buildExt(); if(!extPos){ var r=root.getBoundingClientRect(); extPos={x:Math.min(window.innerWidth-330, r.left+r.width-316), y:Math.max(8, r.top-120)}; } ext.style.left=extPos.x+'px'; ext.style.top=extPos.y+'px'; ext.classList.add('open'); }

  // ---- live playhead + follower (JS-computed so it rides every LFO's shape) ----
  window.updateSynthLFO=function(v){};   // kept so the C++ per-frame push is a harmless no-op
  function tick(t){ if(!lastT)lastT=t; var dt=(t-lastT)/1000; lastT=t;
    ph+=dt*(st.rateHz||1); if(ph>=1)ph-=Math.floor(ph);
    var pl=root&&root.querySelector('#mv-ph'), fd=root&&root.querySelector('#mv-fd');
    if(pl){ var x=ph*456; pl.setAttribute('x1',x); pl.setAttribute('x2',x); }
    if(fd){ fd.setAttribute('cx',(ph*456).toFixed(1)); fd.setAttribute('cy',(MID-yfor(st.shape,(ph+st.phase)%1)*AMP).toFixed(1)); }
    requestAnimationFrame(tick); }

  function boot(){
    root=document.getElementById('mod-engine');
    if(!root){ return; }
    [1,2,3,4,5].forEach(function(n){ LP[n]={ shape:ssGet('LFO'+n+'_SHAPE'), sync:ssGet('LFO'+n+'_SYNC'), div:ssGet('LFO'+n+'_DIV'), rate:ssGet('LFO'+n+'_RATE'), depth:ssGet('LFO'+n+'_DEPTH'), phase:ssGet('LFO'+n+'_PHASE') }; });
    pull(); render();
    // re-render on external (automation/host) changes of the ACTIVE lfo
    [1,2,3,4,5].forEach(function(n){ ['shape','sync','div','rate','depth','phase'].forEach(function(k){ var s=LP[n][k]; if(s&&s.valueChangedEvent&&s.valueChangedEvent.addListener) s.valueChangedEvent.addListener(function(){ if(n===st.tab){ pull(); render(); } }); }); });
    requestAnimationFrame(tick);
  }
  var tries=0;
  (function wait(){ if(window.Juce&&window.Juce.getSliderState){ boot(); } else if(tries++<60){ setTimeout(wait,150); } else { boot(); } })();
})();
</script>'''

def main(path):
    s = open(path, encoding='utf-8').read()
    # ---- CSS splice ----
    cs = CSS_SENT if CSS_SENT in s else OLD_CSS_SENT
    if cs not in s:
        print('ANCHOR ERROR: no mod-engine CSS sentinel found'); return 1
    a = s.index(cs); b = s.index('#mod-panel {', a)
    s = s[:a] + NEW_CSS + '\n\n' + s[b:]
    # ---- JS splice ----
    if JS_SENT in s:
        mark = s.index(JS_SENT)
    elif OLD_JS_MARK in s:
        mark = s.index(OLD_JS_MARK)
    else:
        print('ANCHOR ERROR: no mod-engine JS marker found'); return 1
    jo = s.rindex('<script>', 0, mark)
    jc = s.index('</script>', mark) + len('</script>')
    s = s[:jo] + NEW_JS + s[jc:]
    open(path, 'w', encoding='utf-8').write(s)
    import hashlib
    print('MOD V4 applied. md5=' + hashlib.md5(s.encode('utf-8')).hexdigest())
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
