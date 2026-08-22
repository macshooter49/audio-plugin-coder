// ─────────────────────────────────────────────────────────────────────────────
// utility-worklet.js — the UTILITY glue strip (chain kind 14) as an AudioWorkletProcessor, so
// the six buttons are AUDIBLE in the Safari mockup (the fb296 law).
//
// A MIRROR OF THE MECHANISM in Source/TerrainUtilityFx.h: Gain is a -60..+30 dB fader with
// unity at exactly 60/90 and a GLIDED -inf at 0; Image is the BOUNDED width (0 mono · ½ neutral
// · 1 = 300 %, mid never zero — the deliberate contrast with Widen's R11); Steer is constant-
// power balance; the six pills (Flip L · Flip R · Trade · Sum · DC · Dim); Wiring; Mono Below
// (a side HPF). Characters' voicings are NOT here. The shipping engine is certified 84/84.
//
//   n.port.postMessage({ gain:.667, image:.5, steer:.5, mix:1, b1..b8, flipl, flipr, trade, sum, dc, dim, axis })
//   n.port.onmessage = e => draw(e.data);   // { lvl, pkL, pkR, gainDb }
// ─────────────────────────────────────────────────────────────────────────────
class TerrainUtility extends AudioWorkletProcessor{
  constructor(){ super(); this.fs=sampleRate;
    this.p={gain:60/90,image:.5,steer:.5,mix:1,b1:0,b2:.5,b3:0,b4:0,b5:.5,b6:0,b7:0,b8:.5,flipl:0,flipr:0,trade:0,sum:0,dc:0,dim:0,axis:0,type:0,character:0};
    this.gS=1; this.sideS=1; this.pL=1; this.pR=1; this.mixS=1; this.sumS=0; this.fl=1; this.fr=1; this.dimS=1; this.dcx=[0,0]; this.dcy=[0,0]; this.dcOn=0; this.dcR=0.9987;
    this.sz=0; this.sG=0; this.pkL=0; this.pkR=0; this.lvl=0; this.tick=0;
    this.port.onmessage=e=>{ Object.assign(this.p,e.data); this.resolve(); }; this.resolve(); }
  resolve(){ const p=this.p, c=v=>Math.max(0,Math.min(1,+v||0));
    const t=c(p.gain); this.gT=t<=0?0:Math.pow(10,(-60+90*t)/20); if(Math.abs(t-60/90)<1e-4) this.gT=1;
    const im=c(p.image); this.sideT=im<0.5?im*2:1+4*(im-0.5);            // 0 → 0 · .5 → 1 · 1 → 3 (300 %)
    const st=c(p.steer), th=(st-0.5)*Math.PI/2; this.pLT=Math.cos(th+Math.PI/4)*1.4142136; this.pRT=Math.sin(th+Math.PI/4)*1.4142136;
    this.mixT=c(p.mix); this.sumT=+p.sum?1:0; this.flT=+p.flipl?-1:1; this.frT=+p.flipr?-1:1; this.dimT=+p.dim?Math.pow(10,-20/20):1; this.dcOn=+p.dc?1:0; this.trade=+p.trade?1:0;
    this.dcR=Math.exp(-2*Math.PI*(15*Math.pow(320/15,c(p.b6)))/this.fs);   // fb448 — the DC lamp is the LOW CUT at the Cut At corner (b6: 15..320 Hz), as the engine's dcB_ is
    const mb=c(p.b3); this.mbOn=mb>0.001; const mbHz=20*Math.pow(60,mb); this.sG=1-Math.exp(-2*Math.PI*mbHz/this.fs);
    this.wiring=p.axis|0; }
  process(inputs,outputs){ const inp=inputs[0], out=outputs[0]; if(!out||!out[0]) return true;
    const N=out[0].length, L=inp&&inp[0]?inp[0]:null, R=inp&&inp[1]?inp[1]:(L||null); const a=1-Math.exp(-1/(0.02*this.fs));
    let acc=0, pl=0, pr=0;
    for(let i=0;i<N;i++){ let xl=L?L[i]:0, xr=R?R[i]:xl; const dl=xl, dr=xr;
      this.gS+=a*(this.gT-this.gS); this.sideS+=a*(this.sideT-this.sideS); this.pL+=a*(this.pLT-this.pL); this.pR+=a*(this.pRT-this.pR);
      this.mixS+=a*(this.mixT-this.mixS); this.sumS+=a*(this.sumT-this.sumS); this.fl+=a*(this.flT-this.fl); this.fr+=a*(this.frT-this.fr); this.dimS+=a*(this.dimT-this.dimS);
      if(this.trade){ const t=xl; xl=xr; xr=t; }
      if(this.wiring===1){ const d=0.5*(xl-xr); xl=d; xr=-d; } else if(this.wiring===2){ xr=xl; } else if(this.wiring===3){ xl=xr; } else if(this.wiring===4){ xr=0; } else if(this.wiring===5){ xl=0; }
      xl*=this.fl; xr*=this.fr;
      let m=0.5*(xl+xr), s=0.5*(xl-xr);
      if(this.mbOn){ this.sz+=this.sG*(s-this.sz); s-=this.sz; }           // Mono Below: side HPF
      s*=this.sideS; s*=(1-this.sumS);                                       // Image (bounded) · Sum
      xl=(m+s)*this.gS*this.pL*this.dimS; xr=(m-s)*this.gS*this.pR*this.dimS;
      if(this.dcOn){ const yl=xl-this.dcx[0]+this.dcR*this.dcy[0]; this.dcx[0]=xl; this.dcy[0]=yl; xl=yl; const yr=xr-this.dcx[1]+this.dcR*this.dcy[1]; this.dcx[1]=xr; this.dcy[1]=yr; xr=yr; }
      const wg=Math.sin(1.5707963*this.mixS), dg=Math.cos(1.5707963*this.mixS);
      const oL=dg*dl+wg*xl, oR=dg*dr+wg*xr; out[0][i]=oL; if(out[1]) out[1][i]=oR;
      acc+=oL*oL+oR*oR; pl=Math.max(pl,Math.abs(oL)); pr=Math.max(pr,Math.abs(oR)); }
    this.pkL=Math.max(pl,this.pkL*0.88); this.pkR=Math.max(pr,this.pkR*0.88); this.lvl=0.9*this.lvl+0.1*Math.sqrt(acc/(2*N));
    if(++this.tick>=6){ this.tick=0; this.port.postMessage({lvl:this.lvl,pkL:this.pkL,pkR:this.pkR,gainDb:20*Math.log10(Math.max(1e-6,this.gS))}); }
    return true; } }
registerProcessor('terrain-utility',TerrainUtility);
