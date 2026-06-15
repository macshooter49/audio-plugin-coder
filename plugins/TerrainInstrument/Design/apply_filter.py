#!/usr/bin/env python3
# apply_filter.py — transparent edge-to-edge filter: full-bleed live analyzer (white spectrum +
# purple response curve) with ghost dropdown/tabs and the animated TE×Serum emblems AS the controls
# (Cut/Res/Drv/Env, draggable, wired to SYN_FILTER1_*). Idempotent guarded <script> before </body>.
import sys, hashlib

SENT = '/* FILTER ANALYZER v2 */'
OLD  = '/* FILTER ANALYZER v1 */'
JS = r'''<script>
/* FILTER ANALYZER v2 */
(function(){
  if(window.__filtAna)return; window.__filtAna=true;
  var st=document.createElement('style'); st.textContent=
    '#syn-panel .device.filter{position:relative;}'+
    '#syn-panel .device.filter:not(.swapped){padding:0;overflow:hidden;background:var(--bg-hero);}'+
    '#syn-panel .device.filter .filt-bg{position:absolute;inset:0;z-index:0;}'+
    '#syn-panel .device.filter .filt-bg canvas{position:absolute;inset:0;width:100%;height:100%;display:block;}'+
    '#syn-panel .device.filter.swapped .filt-bg{display:none;}'+
    '#syn-panel .device.filter .filter-display{display:none;}'+
    '#syn-panel .device.filter:not(.swapped) .dd-trigger{position:absolute;top:8px;left:11px;z-index:3;height:auto;min-height:0;padding:0;width:auto;max-width:calc(100% - 88px);background:transparent;border:none;border-radius:0;color:rgba(255,255,255,.62);font-size:9px;letter-spacing:1.1px;font-weight:300;text-shadow:0 1px 3px rgba(0,0,0,.9);gap:5px;}'+
    '#syn-panel .device.filter:not(.swapped) .dd-trigger:hover{color:#fff;border:none;background:transparent;}'+
    '#syn-panel .device.filter:not(.swapped) .dd-trigger .dd-chev{color:inherit;opacity:.6;}'+
    '#syn-panel .device.filter:not(.swapped) .filter-toggle{top:7px;right:11px;z-index:3;gap:7px;}'+
    '#syn-panel .device.filter:not(.swapped) .filter-toggle .ft-pill{width:auto;min-width:0;height:auto;background:transparent;border:none;color:rgba(255,255,255,.4);font-size:10px;font-weight:400;text-shadow:0 1px 3px rgba(0,0,0,.9);}'+
    '#syn-panel .device.filter:not(.swapped) .filter-toggle .ft-pill.act{background:transparent;color:#fff;font-weight:600;border:none;}'+
    '#syn-panel .device.filter:not(.swapped) .filter-toggle .swap-btn{background:transparent;border:none;color:rgba(255,255,255,.5);}'+
    '#syn-panel .device.filter:not(.swapped) .filter-knobs{position:absolute;left:0;right:0;bottom:5px;z-index:3;display:grid;grid-template-columns:repeat(4,1fr);gap:8px;padding:0 12px;background:none;}'+
    '#syn-panel .device.filter .fk-em{display:flex;flex-direction:column;align-items:center;gap:1px;}'+
    '#syn-panel .device.filter .fk-em canvas{width:100%;height:23px;cursor:ns-resize;}'+
    '#syn-panel .device.filter .fk-em .lbl{font-size:7.5px;letter-spacing:.7px;color:rgba(255,255,255,.5);text-shadow:0 1px 2px rgba(0,0,0,.85);}'+
    '#syn-panel .device.filter:not(.swapped) .filt-exp{width:13px;height:13px;color:rgba(255,255,255,.55);cursor:pointer;display:inline-flex;align-items:center;margin-left:2px;}'+
    '#syn-panel .device.filter:not(.swapped) .filt-exp:hover{color:#fff;}'+
    '.filt-ext{position:fixed;z-index:2147483645;width:470px;background:#12121F;border:1px solid rgba(140,130,180,0.45);border-radius:12px;box-shadow:0 18px 50px rgba(0,0,0,.6);display:none;font-family:-apple-system,sans-serif;}'+
    '.filt-ext.open{display:block;}'+
    '.filt-ext .fe-h{display:flex;align-items:center;gap:9px;padding:8px 12px;border-bottom:1px solid rgba(58,58,88,0.55);cursor:grab;background:rgba(255,255,255,.03);}'+
    '.filt-ext .fe-h .fe-g{letter-spacing:2px;color:#4a4f68;font-size:10px;} .filt-ext .fe-h .fe-t{font-size:9px;letter-spacing:1.6px;color:#C5BFD2;font-weight:600;} .filt-ext .fe-h .fe-x{margin-left:auto;color:#908599;cursor:pointer;font-size:13px;}'+
    '.filt-ext .fe-b{position:relative;height:250px;} .filt-ext .fe-b canvas{position:absolute;inset:0;width:100%;height:100%;display:block;}';
  document.head.appendChild(st);
  // ---- spectrum (chained from existing EQ FFT push) ----
  var bins=null, base=window.__terrainEqAnalyzer;
  window.__terrainEqAnalyzer=function(d){ try{ bins=d.post; }catch(e){} if(base) base(d); };
  var CAT=['lp','lp','hp','lp','lp','lp','hp','bp','notch','lp','comb','comb','comb','comb','formant','formant','formant','formant','lp','notch','notch','bp','comb','lp','lp','bp','lp','none'];
  function ss(id){ try{ return (window.Juce&&window.Juce.getSliderState)?window.Juce.getSliderState(id):null; }catch(e){ return null; } }
  function Q(res){ return 0.6+res*res*16; }
  function mag(type,f,fc,res){ if(type==='none')return 1; var w=f/fc,Qf=Q(res),den=Math.sqrt(Math.pow(1-w*w,2)+Math.pow(w/Qf,2)),H;
    if(type==='lp')H=1/den; else if(type==='hp')H=w*w/den; else if(type==='bp')H=(w/Qf)/den;
    else if(type==='notch')H=Math.abs(1-w*w)/den;
    else if(type==='comb'){ H=Math.abs(Math.cos(Math.PI*1.2*f/fc))*(1/Math.sqrt(1+Math.pow(w*0.25,2))); H=0.2+0.8*H; }
    else if(type==='formant'){ var fps=[fc*0.7,fc*1.5,fc*2.7],H2=0; fps.forEach(function(fp,i){ var ww=f/fp,dd=Math.sqrt(Math.pow(1-ww*ww,2)+Math.pow(ww/Q(res*0.7+0.2),2)); H2+=(1/dd)*(1-i*0.22); }); H=H2/2.2; }
    else H=1/den; return H*H; }
  var SR=48000, FFT=4096;
  function fAtX(x,w){ return 20*Math.pow(1000,x/w); }
  function binMag(f){ if(!bins)return 0; var b=f*FFT/SR, i=Math.floor(b); if(i<0||i>=bins.length-1)return 0; var fr=b-i; return bins[i]*(1-fr)+bins[i+1]*fr; }
  function specY(m,h){ var db=20*Math.log10(Math.max(m*2.2,1e-6)), top=3, bot=-72; return h*(1-(db-bot)/(top-bot)); }
  function curveY(H,h){ var db=20*Math.log10(Math.max(H,1e-4)), top=26, bot=-40; return h*(1-(db-bot)/(top-bot)); }
  var dpr=Math.min(window.devicePixelRatio||1,2), anaCv,anaCtx,cutS=null,resS=null,typeS=null;
  function getP(){ if(!cutS)cutS=ss('SYN_FILTER1_CUT'); if(!resS)resS=ss('SYN_FILTER1_RES'); if(!typeS)typeS=ss('SYN_FILTER1_TYPE'); }
  var extOpen=false, extCv=null, extCtx=null, ext=null;
  function drawAnalyzer(){ if(anaCv) drawInto(anaCv,anaCtx); if(extOpen&&extCv) drawInto(extCv,extCtx); }
  function drawInto(cv,ctx){
    getP();
    var r=cv.getBoundingClientRect(), W=Math.max(1,Math.round(r.width*dpr)), H=Math.max(1,Math.round(r.height*dpr));
    if(cv.width!==W) cv.width=W; if(cv.height!==H) cv.height=H;
    ctx.setTransform(1,0,0,1,0,0); ctx.clearRect(0,0,W,H);
    ctx.strokeStyle='rgba(139,92,246,0.10)'; ctx.lineWidth=dpr*0.5;
    [100,1000,10000].forEach(function(fg){ var x=Math.log(fg/20)/Math.log(1000)*W; ctx.beginPath(); ctx.moveTo(x,0); ctx.lineTo(x,H); ctx.stroke(); });
    var fc=cutS?cutS.getScaledValue():1000, res=resS?resS.getScaledValue():0.3;
    var ti=typeS?Math.round(typeS.getNormalisedValue()*(CAT.length-1)):0, type=CAT[ti]||'lp';
    var N=Math.floor(W/dpr), spec=[], crv=[];
    for(var i=0;i<=N;i++){ var x=i/N*W, f=fAtX(i/N*(W/dpr),W/dpr); spec.push([x,specY(binMag(f),H)]); crv.push([x,curveY(mag(type,f,fc,res),H)]); }
    ctx.beginPath(); ctx.moveTo(0,H); spec.forEach(function(p){ctx.lineTo(p[0],p[1]);}); ctx.lineTo(W,H); ctx.closePath();
    var g=ctx.createLinearGradient(0,0,0,H); g.addColorStop(0,'rgba(236,232,242,0.18)'); g.addColorStop(1,'rgba(236,232,242,0.012)'); ctx.fillStyle=g; ctx.fill();
    ctx.beginPath(); spec.forEach(function(p,i){ i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]); }); ctx.strokeStyle='rgba(236,232,242,0.5)'; ctx.lineWidth=dpr*0.8; ctx.stroke();
    if(type!=='none'){
      ctx.save(); ctx.shadowColor='rgba(183,148,255,0.9)'; ctx.shadowBlur=6*dpr;
      ctx.beginPath(); crv.forEach(function(p,i){ i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]); }); ctx.strokeStyle='#C9B3FF'; ctx.lineWidth=dpr*1.8; ctx.stroke(); ctx.restore();
    }
  }
  // ---- emblems (TE×Serum: thin white, purple ping-pong follower, grab-aware) ----
  function pingpong(x){ x=((x%2)+2)%2; return x<1?x:2-x; }
  function envShape(x){ if(x<0.13)return x/0.13; if(x<0.42)return 1-(x-0.13)/0.29*0.4; if(x<0.72)return 0.6; return 0.6*(1-(x-0.72)/0.28); }
  function eS(ctx,grab){ ctx.lineWidth=grab?1.7:1.35; ctx.lineCap='round'; ctx.lineJoin='round'; ctx.strokeStyle=grab?'#FFFFFF':'#ECE8F2'; ctx.fillStyle='#B794FF'; ctx.shadowBlur=0; }
  function emCUT(ctx,W,H,v,t,grab){ eS(ctx,grab); var cx=W/2,cy=H/2,r=Math.min(W,H)*0.42,x0=cx-r,x1=cx+r,ytop=cy-r*0.5,ybot=cy+r*0.72,knee=cx-r*0.45+v*r*0.85;
    function sy(px){ if(px<=knee)return ytop; var d=(px-knee)/(x1-knee); return ytop+d*d*(ybot-ytop); }
    ctx.beginPath(); for(var i=0;i<=40;i++){var px=x0+i/40*2*r; i?ctx.lineTo(px,sy(px)):ctx.moveTo(px,sy(px));} ctx.stroke();
    var p=grab?v:pingpong(t*0.5),px=x0+p*2*r; ctx.beginPath(); ctx.arc(px,sy(px),2,0,7); ctx.fill(); }
  function emRES(ctx,W,H,v,t,grab){ eS(ctx,grab); var cx=W/2,cy=H/2,r=Math.min(W,H)*0.42,x0=cx-r,amp=(0.12+v*0.62)*r,osc=grab?1:Math.sin(t*(3+v*8));
    ctx.beginPath(); for(var i=0;i<=44;i++){var x=i/44,px=x0+x*2*r,py=cy-Math.sin(x*Math.PI)*amp*osc; i?ctx.lineTo(px,py):ctx.moveTo(px,py);} ctx.stroke();
    ctx.beginPath(); ctx.arc(cx,cy-amp*osc,2,0,7); ctx.fill(); }
  function emDRV(ctx,W,H,v,t,grab){ eS(ctx,grab); var cx=W/2,cy=H/2,r=Math.min(W,H)*0.42,k=1+v*10,nrm=Math.tanh(k);
    ctx.beginPath(); for(var i=0;i<=48;i++){var x=i/48,ph=x*Math.PI*2-t*3.2,s=Math.tanh(Math.sin(ph)*k)/nrm,px=cx-r+x*2*r,py=cy-s*r*0.7; i?ctx.lineTo(px,py):ctx.moveTo(px,py);} ctx.stroke(); }
  function emENV(ctx,W,H,v,t,grab){ eS(ctx,grab); var cx=W/2,cy=H/2,r=Math.min(W,H)*0.42,x0=cx-r,base=cy+r*0.7,top=base-r*1.35*(0.35+v*0.65);
    function ey(x){return base-envShape(x)*(base-top);}
    ctx.beginPath(); for(var i=0;i<=44;i++){var x=i/44,px=x0+x*2*r; i?ctx.lineTo(px,ey(x)):ctx.moveTo(px,ey(x));} ctx.stroke();
    var p=grab?v:pingpong(t*0.45); ctx.beginPath(); ctx.arc(x0+p*2*r,ey(p),2,0,7); ctx.fill(); }
  var EMD={cut:emCUT,res:emRES,drv:emDRV,env:emENV};
  var EM=[{t:'cut',id:'SYN_FILTER1_CUT',l:'Cut'},{t:'res',id:'SYN_FILTER1_RES',l:'Res'},{t:'drv',id:'SYN_FILTER1_DRV',l:'Drv'},{t:'env',id:'SYN_FILTER1_ENV',l:'Env'}];
  var emblems=[];
  function regEmblem(cv,type,id){ var e={cv:cv,ctx:cv.getContext('2d'),type:type,id:id,ss:null,grab:false}; emblems.push(e);
    cv.addEventListener('mousedown',function(ev){ ev.preventDefault(); e.grab=true; if(!e.ss)e.ss=ss(id); var y0=ev.clientY,v0=e.ss?e.ss.getNormalisedValue():0.5;
      function mv(m){ if(e.ss){ var nv=Math.max(0,Math.min(1,v0+(y0-m.clientY)*0.006)); try{e.ss.setNormalisedValue(nv);}catch(x){} } }
      function up(){ e.grab=false; document.removeEventListener('mousemove',mv); document.removeEventListener('mouseup',up); }
      document.addEventListener('mousemove',mv); document.addEventListener('mouseup',up); }); }
  function drawEmblems(t){ emblems.forEach(function(e){ if(!e.ss)e.ss=ss(e.id); var v=e.ss?e.ss.getNormalisedValue():0.5;
    var r=e.cv.getBoundingClientRect(), W=Math.max(1,Math.round(r.width*dpr)), H=Math.max(1,Math.round(r.height*dpr));
    if(e.cv.width!==W)e.cv.width=W; if(e.cv.height!==H)e.cv.height=H;
    var ctx=e.ctx; ctx.setTransform(1,0,0,1,0,0); ctx.clearRect(0,0,W,H); ctx.setTransform(dpr,0,0,dpr,0,0); EMD[e.type](ctx,W/dpr,H/dpr,v,t,e.grab); }); }
  function loop(ts){ var t=ts/1000; drawAnalyzer(); drawEmblems(t); requestAnimationFrame(loop); }
  function buildExt(){
    ext=document.createElement('div'); ext.className='filt-ext';
    ext.innerHTML='<div class="fe-h"><span class="fe-g">⠇</span><span class="fe-t">Filter 1</span><span class="fe-x">✕</span></div><div class="fe-b"><canvas></canvas></div>';
    document.body.appendChild(ext); extCv=ext.querySelector('canvas'); extCtx=extCv.getContext('2d');
    ext.querySelector('.fe-x').onclick=function(){ ext.classList.remove('open'); extOpen=false; };
    var h=ext.querySelector('.fe-h'); h.onmousedown=function(e){ if(e.target.classList.contains('fe-x'))return; e.preventDefault(); var r=ext.getBoundingClientRect(),dx=e.clientX-r.left,dy=e.clientY-r.top;
      function mv(ev){ ext.style.left=Math.max(4,Math.min(window.innerWidth-ext.offsetWidth-4,ev.clientX-dx))+'px'; ext.style.top=Math.max(4,Math.min(window.innerHeight-60,ev.clientY-dy))+'px'; }
      function up(){ document.removeEventListener('mousemove',mv); document.removeEventListener('mouseup',up); }
      document.addEventListener('mousemove',mv); document.addEventListener('mouseup',up); };
  }
  function openExt(){ if(!ext)buildExt(); var dev=document.getElementById('filter-device'); var r=dev.getBoundingClientRect(); ext.style.left=Math.max(8,Math.min(window.innerWidth-478,r.left))+'px'; ext.style.top=Math.max(8,r.top-285)+'px'; ext.classList.add('open'); extOpen=true; }
  function build(dev){
    var bg=dev.querySelector('.filt-bg'); if(!bg){ bg=document.createElement('div'); bg.className='filt-bg'; bg.innerHTML='<canvas></canvas>'; dev.insertBefore(bg, dev.firstChild); }
    anaCv=bg.querySelector('canvas'); anaCtx=anaCv.getContext('2d');
    var kn=dev.querySelector('.filter-knobs');
    if(kn && !kn.querySelector('.fk-em')){
      kn.innerHTML=EM.map(function(e){ return '<div class="fk-em"><canvas data-em="'+e.t+'" data-id="'+e.id+'"></canvas><div class="lbl">'+e.l+'</div></div>'; }).join('');
      kn.querySelectorAll('canvas').forEach(function(c){ regEmblem(c, c.getAttribute('data-em'), c.getAttribute('data-id')); });
    }
    var tg=dev.querySelector('.filter-toggle');
    if(tg && !tg.querySelector('.filt-exp')){ var ic=document.createElement('div'); ic.className='filt-exp'; ic.innerHTML='<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M6 2 H2 V6 M14 6 V2 H10 M10 14 H14 V10 M2 10 V14 H6"/></svg>'; ic.onclick=openExt; tg.appendChild(ic); }
    requestAnimationFrame(loop);
  }
  var tries=0;
  (function wait(){ var dev=document.getElementById('filter-device'); if(dev&&dev.querySelector('.filter-knobs')){ build(dev); return; } if(tries++<300) setTimeout(wait,100); })();
})();
</script>'''

def main(path):
    s=open(path,encoding='utf-8').read()
    for sent in (SENT, OLD):
        if sent in s:
            i=s.index(sent); jo=s.rindex('<script>',0,i); jc=s.index('</script>',i)+len('</script>')
            if s[jc:jc+1]=='\n': jc+=1
            s=s[:jo]+s[jc:]
    idx=s.rfind('</body>')
    if idx<0: print('ANCHOR ERROR: no </body>'); return 1
    s=s[:idx]+JS+'\n'+s[idx:]
    open(path,'w',encoding='utf-8').write(s)
    print('FILTER ANALYZER v2 applied. md5='+hashlib.md5(s.encode('utf-8')).hexdigest())
    return 0

if __name__=='__main__':
    sys.exit(main(sys.argv[1]))
