// ─────────────────────────────────────────────────────────────────────────────
// bode-worklet.js — the BODE frequency shifter (chain kind 13) as an AudioWorkletProcessor,
// so Max can HEAR the spectrum slide in a Safari mockup before judging the card (the fb296 law).
//
// A MIRROR OF THE MECHANISM in Source/TerrainBodeFx.h — the same Niemitalo quadrature pair,
// the same 1-sample align, the same recursive oscillator, the same sideband coefficient
// (y = i·cos + m·q·sin, m=+1 UP · 0 RING · -1 DOWN, sign PROVEN by bod_cert gate 0), the same
// ±5000 Hz exponential taper, the same Echobode loop (delay → damping LP → soft clip INSIDE the
// loop, makeup divided back so Character cannot move loop gain), the same squared input gate so
// nothing free-runs, the same stereo shift MIRROR. The Characters' voicings are NOT here — this
// is the card's ears, not the shipping engine; the shipping engine is certified 28/28.
//
//   n.port.postMessage({ shift:.5, dir:1, fdbk:0, mix:1, b1..b8, type, character, axis, guard, sync })
//   n.port.onmessage = e => draw(e.data);   // { shiftHz, blend, fb, lvl, img }
// ─────────────────────────────────────────────────────────────────────────────
const AP_I=[0.6923878,0.9360654322959,0.9882295226860,0.9987488452737];
const AP_Q=[0.4021921162426,0.8561710882420,0.9722909545651,0.9952884791278];
const SHIFT_MAX=5000, FB_MAX=0.995;
function tanhF(x){ if(x>5)return 1; if(x<-5)return -1; const x2=x*x; return x*(27+x2)/(27+9*x2); }
class AP2{ constructor(a){ this.A=a*a; this.x1=this.x2=this.y1=this.y2=0; }
  p(x){ const y=this.A*(x+this.y2)-this.x2; this.x2=this.x1; this.x1=x; this.y2=this.y1; this.y1=y; return y; } }
class Quad{
  constructor(){ this.i=AP_I.map(a=>new AP2(a)); this.q=AP_Q.map(a=>new AP2(a)); this.d=0; this.c=1; this.s=0; this.oc=1; this.os=0; this.n=0; }
  setHz(hz,fs){ const w=2*Math.PI*hz/fs; this.oc=Math.cos(w); this.os=Math.sin(w); }
  p(x,m){ let i=x; for(const a of this.i) i=a.p(i); const io=this.d; this.d=i;
    let q=x; for(const a of this.q) q=a.p(q);
    const nc=this.oc*this.c-this.os*this.s; this.s=this.os*this.c+this.oc*this.s; this.c=nc;
    if(++this.n>=512){ this.n=0; const mm=1.5-0.5*(this.c*this.c+this.s*this.s); this.c*=mm; this.s*=mm; }
    return io*this.c+m*(q*this.s); } }
class TerrainBode extends AudioWorkletProcessor{
  constructor(){ super();
    this.p={shift:.5,dir:1,fdbk:0,mix:1,b1:.5,b2:1,b3:.45,b4:0,b5:0,b6:1,b7:.5,b8:0,type:0,character:0,axis:0,guard:1,sync:0};
    this.fs=sampleRate; this.quad=[new Quad(),new Quad()];
    const sz=1<<Math.ceil(Math.log2(this.fs*2.2)); this.mask=sz-1; this.dl=[new Float32Array(sz),new Float32Array(sz)]; this.w=[0,0];
    this.damp=[0,0]; this.dampG=1; this.lowZ=[0,0]; this.lowG=0; this.lowOn=false;
    this.dcx=[0,0]; this.dcy=[0,0];
    this.shiftT=0; this.shiftS=0; this.fbT=0; this.fbS=0; this.timeT=480; this.timeS=480; this.mixT=1; this.mixS=1;
    this.blend=1; this.ring=1; this.clipK=1; this.spread=-1; this.touch=0; this.env=0; this.gate=0; this.img=0; this.lvl=0;
    this.port.onmessage=e=>{ Object.assign(this.p,e.data); this.resolve(); }; this.resolve(); this.tick=0; }
  resolve(){ const p=this.p, c=v=>Math.max(0,Math.min(1,+v||0));
    const v=2*c(p.shift)-1, mag=Math.pow(SHIFT_MAX+1,Math.abs(v))-1;
    this.shiftT=(v<0?-mag:mag)+(2*c(p.b1)-1)*2.0;
    const b=c(p.dir); this.blend=2*b-1; this.ring=1+0.41421356*(1-Math.abs(this.blend));
    this.fbT=FB_MAX*c(p.fdbk);
    const ms=0.02*Math.pow(50000,c(p.b3)); this.timeT=Math.max(1,ms*0.001*this.fs);
    const dHz=400*Math.pow(50,c(p.b6)); this.dampG=1-Math.exp(-2*Math.PI*Math.min(dHz,0.45*this.fs)/this.fs);
    const lk=c(p.b5); this.lowOn=lk>0.001; const lHz=20*Math.pow(100,lk); this.lowG=1-Math.exp(-2*Math.PI*lHz/this.fs);
    const K=[1,1.6,2.6,1.2,4,6.5,2,3.2]; this.clipK=K[Math.max(0,Math.min(7,p.character|0))];
    this.touch=(2*c(p.b7)-1)*1200; this.spread=1-2*c(p.b2); this.mixT=c(p.mix); this.blurMix=c(p.b4); }
  process(inputs,outputs){
    const inp=inputs[0], out=outputs[0]; if(!out||!out[0]) return true;
    const N=out[0].length, L=inp&&inp[0]?inp[0]:null, R=inp&&inp[1]?inp[1]:(L||null);
    const aP=1-Math.exp(-1/(0.015*this.fs)), aM=1-Math.exp(-1/(0.030*this.fs));
    let acc=0, img=0;
    for(let i=0;i<N;i++){
      const xl=L?L[i]:0, xr=R?R[i]:xl;
      const rect=0.5*(Math.abs(xl)+Math.abs(xr)); this.env+=(rect>this.env?0.35:0.0006)*(rect-this.env);
      const gT=this.env>2.2e-4?1:0; this.gate+=(gT>this.gate?0.02:0.0009)*(gT-this.gate); const g2=this.gate*this.gate;
      this.shiftS+=aP*(this.shiftT-this.shiftS); this.fbS+=aP*(this.fbT-this.fbS); this.timeS+=aM*(this.timeT-this.timeS); this.mixS+=aM*(this.mixT-this.mixS);
      const touch=this.touch*this.env*this.env*24, gFb=this.fbS*g2;
      const dry=[xl,xr], x=[xl,xr], low=[0,0], wet=[0,0];
      if(this.lowOn) for(let c=0;c<2;c++){ this.lowZ[c]+=this.lowG*(x[c]-this.lowZ[c]); low[c]=this.lowZ[c]; x[c]-=low[c]; }
      for(let c=0;c<2;c++){
        const mir=c===0?1:this.spread; this.quad[c].setHz((this.shiftS+touch)*mir,this.fs);
        const len=Math.max(1,Math.min(this.timeS|0,this.mask-2)), fr=this.timeS-Math.floor(this.timeS);
        const r0=(this.w[c]-len)&this.mask, r1=(this.w[c]-len-1)&this.mask;
        let tap=this.dl[c][r0]+fr*(this.dl[c][r1]-this.dl[c][r0]);
        this.damp[c]+=this.dampG*(tap-this.damp[c]); tap=this.damp[c];
        let u=x[c]+gFb*tap; u=tanhF(u*this.clipK)/this.clipK;
        const y=this.quad[c].p(u,this.blend)*this.ring;
        this.dl[c][this.w[c]]=y; this.w[c]=(this.w[c]+1)&this.mask;
        const dc=y-this.dcx[c]+0.995*this.dcy[c]; this.dcx[c]=y; this.dcy[c]=dc;
        wet[c]=dc+low[c]; }
      const wg=Math.sin(1.5707963*this.mixS), dg=Math.cos(1.5707963*this.mixS);
      const oL=dg*dry[0]+wg*wet[0], oR=dg*dry[1]+wg*wet[1];
      out[0][i]=oL; if(out[1]) out[1][i]=oR;
      acc+=oL*oL+oR*oR; img+=Math.abs(wet[0])+Math.abs(wet[1]); }
    this.lvl=0.9*this.lvl+0.1*Math.sqrt(acc/(2*N)); this.img=0.9*this.img+0.1*(img/(2*N));
    if(++this.tick>=8){ this.tick=0; this.port.postMessage({shiftHz:this.shiftS,blend:this.blend,fb:this.fbS*this.gate*this.gate,lvl:this.lvl,img:this.img}); }
    return true; } }
registerProcessor('terrain-bode',TerrainBode);
