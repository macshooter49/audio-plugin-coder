// ─────────────────────────────────────────────────────────────────────────────
// utility-worklet.js — the UTILITY channel strip (chain kind 14, fb450) as an AudioWorkletProcessor, so
// the card is AUDIBLE in the Safari mockup (the fb296 law).
//
// A MIRROR OF THE MECHANISM in Source/TerrainUtilityFx.h: Flip L/R → Haas → Mono → Gain(×Dim) → High
// Pass (2-pole) → Low Pass (2-pole) → Bass (low shelf 120 Hz ±12) → Air (high shelf 8 kHz ±12) → Drive
// (tanh, +12..+40 dB, LOUDNESS makeup at the bus peak) → Mono Below (side HPF) → Width → Rotate → Pan →
// Swap (at the output) → Mix. Same knob→value laws as the engine. The shipping engine is certified 67/67.
//
//   n.port.postMessage({ gain:.667, image:.5, steer:.5, mix:1, b1..b8, flipl, flipr, trade, sum, dim, type })
//   n.port.onmessage = e => draw(e.data);   // { lvl, pkL, pkR, corr, img }
// ─────────────────────────────────────────────────────────────────────────────
const BUS_PK=0.05*1.41421356;
function tanhF(x){ if(x>5)return 1; if(x<-5)return -1; const x2=x*x; return x*(27+x2)/(27+9*x2); }
class Svf{ constructor(){ this.ic1=0; this.ic2=0; this.g=0; this.a1=1; this.a2=0; this.a3=0; } set(fc,fs){ const f=Math.max(5,Math.min(fc,0.45*fs)); const g=Math.tan(Math.PI*f/fs); this.g=g; const k=1.4142136; const den=1+g*(g+k); this.a1=1/den; this.a2=g*this.a1; this.a3=g*this.a2; }
  tick(x){ const v3=x-this.ic2, v1=this.a1*this.ic1+this.a2*v3, v2=this.ic2+this.a2*this.ic1+this.a3*v3; this.ic1=2*v1-this.ic1; this.ic2=2*v2-this.ic2; return {lp:v2, hp:x-1.4142136*v1-v2}; } }
class Shelf{ constructor(){ this.b0=1;this.b1=0;this.b2=0;this.a1=0;this.a2=0;this.z1=0;this.z2=0; }
  design(high,dB,fc,fs){ const A=Math.pow(10,dB/40), w=2*Math.PI*Math.min(fc,0.45*fs)/fs, cs=Math.cos(w), sn=Math.sin(w), al=sn*0.70710678, sq=2*Math.sqrt(A)*al; let B0,B1,B2,A0,A1,A2;
    if(!high){ B0=A*((A+1)-(A-1)*cs+sq); B1=2*A*((A-1)-(A+1)*cs); B2=A*((A+1)-(A-1)*cs-sq); A0=(A+1)+(A-1)*cs+sq; A1=-2*((A-1)+(A+1)*cs); A2=(A+1)+(A-1)*cs-sq; }
    else { B0=A*((A+1)+(A-1)*cs+sq); B1=-2*A*((A-1)+(A+1)*cs); B2=A*((A+1)+(A-1)*cs-sq); A0=(A+1)-(A-1)*cs+sq; A1=2*((A-1)-(A+1)*cs); A2=(A+1)-(A-1)*cs-sq; }
    this.b0=B0/A0; this.b1=B1/A0; this.b2=B2/A0; this.a1=A1/A0; this.a2=A2/A0; }
  tick(x){ const y=this.b0*x+this.z1; this.z1=this.b1*x-this.a1*y+this.z2; this.z2=this.b2*x-this.a2*y; return y; } }
class TerrainUtility extends AudioWorkletProcessor{
  constructor(){ super(); this.fs=sampleRate;
    this.p={gain:60/90,image:.5,steer:.5,mix:1,b1:0,b2:1,b3:.5,b4:.5,b5:0,b6:.5,b7:.5,b8:0,flipl:0,flipr:0,trade:0,sum:0,dim:0,type:0};
    this.gS=1; this.sideS=1; this.pL=1; this.pR=1; this.mixS=1; this.sumS=0; this.fl=1; this.fr=1; this.dimS=1; this.swS=0;
    this.hp=[new Svf(),new Svf()]; this.lp=[new Svf(),new Svf()]; this.bass=[new Shelf(),new Shelf()]; this.air=[new Shelf(),new Shelf()];
    this.hpA=0; this.lpA=0; this.bA=0; this.aA=0; this.dA=0; this.hA=0; this.drvS=1; this.haasS=0; this.rotS=0; this.sz=0; this.sG=0; this.mbOn=false;
    const n=1<<Math.ceil(Math.log2(this.fs*0.025)); this.dl=[new Float32Array(n),new Float32Array(n)]; this.mask=n-1; this.w=0;
    this.pkL=0; this.pkR=0; this.lvl=0; this.tick=0;
    this.port.onmessage=e=>{ Object.assign(this.p,e.data); this.resolve(); }; this.resolve(); }
  resolve(){ const p=this.p, c=v=>Math.max(0,Math.min(1,+v||0));
    const t=c(p.gain); this.gT=t<=0?0:Math.pow(10,(-60+90*t)/20); if(Math.abs(t-60/90)<1e-4) this.gT=1; if(+p.dim) this.gT*=0.1;
    const im=c(p.image); this.sideT=im<0.5?im*2:1+4*(im-0.5);
    const st=c(p.steer), th=(st-0.5)*Math.PI/2; this.pLT=Math.cos(th+Math.PI/4)*1.4142136; this.pRT=Math.sin(th+Math.PI/4)*1.4142136;
    this.mixT=c(p.mix); this.sumT=+p.sum?1:0; this.flT=+p.flipl?-1:1; this.frT=+p.flipr?-1:1; this.swT=+p.trade?1:0;
    const hpT=c(p.b1), lpT=c(p.b2); this.hpHz=hpT<=0?0:40*Math.pow(100,hpT); this.lpHz=lpT>=1?0:300*Math.pow(16000/300,lpT);
    for(let k=0;k<2;k++){ this.hp[k].set(this.hpHz||40,this.fs); this.lp[k].set(this.lpHz||16000,this.fs); }
    this.bassDb=(2*c(p.b3)-1)*12; this.airDb=(2*c(p.b4)-1)*12; for(let k=0;k<2;k++){ this.bass[k].design(false,this.bassDb,120,this.fs); this.air[k].design(true,this.airDb,8000,this.fs); }
    const mb=c(p.b5); this.mbOn=mb>0.001; const mbHz=50*Math.pow(30,mb); this.sG=1-Math.exp(-2*Math.PI*mbHz/this.fs);
    this.rotT=(2*c(p.b6)-1)*40*Math.PI/180; this.haasT=(2*c(p.b7)-1)*20*0.001*this.fs;
    const dT=c(p.b8); this.drvT=dT<=0?1:Math.pow(10,(12+28*dT)/20); this.drvOn=dT>0; }
  process(inputs,outputs){ const inp=inputs[0], out=outputs[0]; if(!out||!out[0]) return true;
    const N=out[0].length, L=inp&&inp[0]?inp[0]:null, R=inp&&inp[1]?inp[1]:(L||null); const a=1-Math.exp(-1/(0.02*this.fs));
    let acc=0, pl=0, pr=0;
    for(let i=0;i<N;i++){ let xl=L?L[i]:0, xr=R?R[i]:xl; const dl=xl, dr=xr;
      this.gS+=a*(this.gT-this.gS); this.sideS+=a*(this.sideT-this.sideS); this.pL+=a*(this.pLT-this.pL); this.pR+=a*(this.pRT-this.pR);
      this.mixS+=a*(this.mixT-this.mixS); this.sumS+=a*(this.sumT-this.sumS); this.fl+=a*(this.flT-this.fl); this.fr+=a*(this.frT-this.fr); this.swS+=a*(this.swT-this.swS);
      this.hpA+=a*((this.hpHz>0?1:0)-this.hpA); this.lpA+=a*((this.lpHz>0?1:0)-this.lpA); this.bA+=a*((Math.abs(this.bassDb)>0.01?1:0)-this.bA); this.aA+=a*((Math.abs(this.airDb)>0.01?1:0)-this.aA);
      this.dA+=a*((this.drvOn?1:0)-this.dA); this.drvS+=a*(this.drvT-this.drvS); this.haasS+=a*(this.haasT-this.haasS); this.hA+=a*((Math.abs(this.haasT)>1e-3?1:0)-this.hA); this.rotS+=a*(this.rotT-this.rotS);
      xl*=this.fl; xr*=this.fr;
      // Haas: one channel delayed (fractional, glided), the other straight
      this.dl[0][this.w]=xl; this.dl[1][this.w]=xr; { const d=Math.abs(this.haasS), rp=this.w-d, i0=Math.floor(rp), fr=rp-i0, ch=this.haasS<0?1:0; const y=this.dl[ch][i0&this.mask]*(1-fr)+this.dl[ch][(i0+1)&this.mask]*fr; if(ch===0) xl+=this.hA*(y-xl); else xr+=this.hA*(y-xr); } this.w=(this.w+1)&this.mask;
      { const m=0.5*(xl+xr); xl+=this.sumS*(m-xl); xr+=this.sumS*(m-xr); }
      xl*=this.gS; xr*=this.gS;
      if(this.hpA>0){ const h0=this.hp[0].tick(xl).hp, h1=this.hp[1].tick(xr).hp; xl+=this.hpA*(h0-xl); xr+=this.hpA*(h1-xr); }
      if(this.lpA>0){ const l0=this.lp[0].tick(xl).lp, l1=this.lp[1].tick(xr).lp; xl+=this.lpA*(l0-xl); xr+=this.lpA*(l1-xr); }
      if(this.bA>0){ xl+=this.bA*(this.bass[0].tick(xl)-xl); xr+=this.bA*(this.bass[1].tick(xr)-xr); }
      if(this.aA>0){ xl+=this.aA*(this.air[0].tick(xl)-xl); xr+=this.aA*(this.air[1].tick(xr)-xr); }
      if(this.dA>0){ const drv=Math.max(1,this.drvS), mk=BUS_PK/Math.max(1e-6,tanhF(drv*BUS_PK)); xl+=this.dA*(mk*tanhF(drv*xl)-xl); xr+=this.dA*(mk*tanhF(drv*xr)-xr); }
      let m=0.5*(xl+xr), s=0.5*(xl-xr);
      if(this.mbOn){ this.sz+=this.sG*(s-this.sz); s-=this.sz; }
      s*=this.sideS;
      if(Math.abs(this.rotS)>1e-6){ const c=Math.cos(this.rotS), sn=Math.sin(this.rotS); const mm=m*c-s*sn, ss=m*sn+s*c; m=mm; s=ss; }
      xl=(m+s)*this.pL; xr=(m-s)*this.pR;
      { const l0=xl, r0=xr; xl=l0+this.swS*(r0-l0); xr=r0+this.swS*(l0-r0); }   // Swap at the output
      const wg=Math.sin(1.5707963*this.mixS), dg=Math.cos(1.5707963*this.mixS);
      const oL=dg*dl+wg*xl, oR=dg*dr+wg*xr; out[0][i]=oL; if(out[1]) out[1][i]=oR;
      acc+=oL*oL+oR*oR; pl=Math.max(pl,Math.abs(oL)); pr=Math.max(pr,Math.abs(oR)); }
    this.pkL=Math.max(pl,this.pkL*0.88); this.pkR=Math.max(pr,this.pkR*0.88); this.lvl=0.9*this.lvl+0.1*Math.sqrt(acc/(2*N));
    if(++this.tick>=6){ this.tick=0; this.port.postMessage({lvl:this.lvl,pkL:this.pkL,pkR:this.pkR,corr:0,img:this.sideT}); }
    return true; } }
registerProcessor('terrain-utility',TerrainUtility);
