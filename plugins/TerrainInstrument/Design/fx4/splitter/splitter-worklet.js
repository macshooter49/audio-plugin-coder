// ─────────────────────────────────────────────────────────────────────────────
// splitter-worklet.js — the band SPLITTER (chain kind 15) as an AudioWorkletProcessor, so the
// lanes light from REAL band energy in the Safari mockup (the fb296 law).
//
// A MIRROR OF THE MECHANISM in Source/TerrainSplitterFx.h: Linkwitz-Riley 4th order on the
// Simper TPT SVF (LP4 + HP4 == AP2 exactly), the lower bands run through the SAME allpass the
// upper legs receive so the recombination cannot comb, crossovers at f0 · f0·r · f0·r² with
// r >= 1.4 BY CONSTRUCTION (inversion impossible, not clamped), per-lane gain/mute/solo/flip,
// the dry through the identical cascade so Mix never combs. Same knob→Hz law as the engine
// (splitHzFor / spanRatioFor), because a mapping authored in two places is the fb373 defect.
//
//   n.port.postMessage({ type:1, slope:2, split:.5, balance:.5, spread:.5, mix:1, b1..b8,
//                        mute1..4, solo1..4, flip1..4 })
//   n.port.onmessage = e => draw(e.data);   // { nl, hz[3], pk[4], gt[4], lvl }
// ─────────────────────────────────────────────────────────────────────────────
const FC_BOT=25, FC_TOP=10000, SPAN_MIN=1.4, SPAN_MAX=40, G_TOP=12, G_BOT=-60;
const LANES=[2,3,4,2,2];
function laneCount(t){ return LANES[Math.max(0,Math.min(4,t|0))]||3; }
function splitHz(t,k){ const N=laneCount(t), top=N===2?FC_TOP:(N===3?2000:700); return FC_BOT*Math.pow(top/FC_BOT,Math.max(0,Math.min(1,k))); }
function spanRatio(t,f0,k){ const nx=laneCount(t)-1; if(nx<2) return 1; const rH=Math.pow(FC_TOP/Math.max(FC_BOT,f0),1/(nx-1)); const rT=Math.max(SPAN_MIN*1.05,Math.min(SPAN_MAX,rH)); return SPAN_MIN*Math.pow(rT/SPAN_MIN,Math.max(0,Math.min(1,k))); }
function laneGain(t){ t=Math.max(0,Math.min(1,t)); if(t<=0) return 0; const d=t<0.5?G_BOT*Math.pow(1-2*t,1.5):(2*G_TOP)*(t-0.5); let g=Math.pow(10,d/20); if(t<0.04) g*=t*25; return g; }
// Simper TPT SVF, Q = 1/sqrt2 (Butterworth): two in cascade = LR4. ap() = the matching AP2.
class Svf{ constructor(){ this.g=0; this.k=1.4142136; this.a1=1; this.a2=0; this.a3=0; this.ic1=0; this.ic2=0; }
  set(fc,fs){ const g=Math.tan(Math.PI*Math.min(fc,0.45*fs)/fs); this.g=g; this.a1=1/(1+g*(g+this.k)); this.a2=g*this.a1; this.a3=g*this.a2; }
  tick(x){ const v3=x-this.ic2, v1=this.a1*this.ic1+this.a2*v3, v2=this.ic2+this.a2*this.ic1+this.a3*v3;
    this.ic1=2*v1-this.ic1; this.ic2=2*v2-this.ic2; return {lp:v2,hp:x-this.k*v1-v2}; }
}
// LR4 = two Butterworth SVFs in cascade per leg. APc = the matching AP2: lp - k·bp + hp == 2(lp+hp) - x.
class LR4{ constructor(){ this.a=new Svf(); this.b=new Svf(); this.c=new Svf(); this.d=new Svf(); }
  set(fc,fs){ this.a.set(fc,fs); this.b.set(fc,fs); this.c.set(fc,fs); this.d.set(fc,fs); }
  split(x){ const o1=this.a.tick(x); const lo=this.b.tick(o1.lp).lp; const o2=this.c.tick(x); const hi=this.d.tick(o2.hp).hp; return {lo:lo,hi:hi}; } }
class APc{ constructor(){ this.s=new Svf(); } set(fc,fs){ this.s.set(fc,fs); } p(x){ const o=this.s.tick(x); return o.lp+o.hp-(x-o.lp-o.hp); } } // lp - bp·k + hp == AP2
class TerrainSplitter extends AudioWorkletProcessor{
  constructor(){ super(); this.fs=sampleRate;
    this.p={type:1,slope:2,split:.5,balance:.5,spread:.5,mix:1,b1:.5,b2:.5,b3:.5,b4:.5,b5:.4,b6:.5,b7:.5,b8:.5,
            mute1:0,mute2:0,mute3:0,mute4:0,solo1:0,solo2:0,solo3:0,solo4:0,flip1:0,flip2:0,flip3:0,flip4:0};
    this.x=[[new LR4(),new LR4(),new LR4()],[new LR4(),new LR4(),new LR4()]];
    // align lower bands — ONE allpass instance per (lane, crossover) pair. A stateful filter called
    // twice per sample is a different filter; sharing them between lane 0 and lane 1 nulled 4 lanes
    // at only -29 dB. [0]=lane0 by f1 (3-lane) · [1]=lane0 by f1, [2]=lane0 by f2, [3]=lane1 by f2 (4-lane)
    this.al=[[new APc(),new APc(),new APc(),new APc()],[new APc(),new APc(),new APc(),new APc()]];
    this.dry=[[new APc(),new APc(),new APc()],[new APc(),new APc(),new APc()]];  // the dry cascade (Mix law)
    this.pk=[0,0,0,0]; this.lvl=0; this.hz=[0,0,0]; this.nl=3; this.gate=[1,1,1,1]; this.gain=[1,1,1,1];
    this.port.onmessage=e=>{ Object.assign(this.p,e.data); this.resolve(); }; this.resolve(); this.tick=0; }
  resolve(){ const p=this.p, t=p.type|0; this.type=t; this.nl=laneCount(t);
    const f0=splitHz(t,+p.split), r=spanRatio(t,f0,+p.b5); this.hz=[f0,f0*r,f0*r*r].slice(0,this.nl-1); while(this.hz.length<3) this.hz.push(0);
    for(let c=0;c<2;c++){ for(let k=0;k<3;k++){ const f=this.hz[k]||1000; this.x[c][k].set(f,this.fs); this.dry[c][k].set(f,this.fs); }
      const f1=this.hz[1]||1000, f2=this.hz[2]||2000; this.al[c][0].set(f1,this.fs); this.al[c][1].set(f1,this.fs); this.al[c][2].set(f2,this.fs); this.al[c][3].set(f2,this.fs); }
    const anySolo=!!(+p.solo1||+p.solo2||+p.solo3||+p.solo4);
    for(let k=0;k<4;k++){ const m=+p['mute'+(k+1)], s=+p['solo'+(k+1)]; this.gate[k]=(m?0:1)*(anySolo?(s?1:0):1);
      const bal=+p.balance-0.5, lift=(k===this.nl-1?1:(k===0?-1:0))*bal*6; // Balance: lane energy morph, ±6 dB between bottom/top lane
      this.gain[k]=laneGain(+p['b'+(k+1)])*Math.pow(10,lift/20)*(+p['flip'+(k+1)]?-1:1); }
    this.mix=Math.max(0,Math.min(1,+p.mix)); }
  split(c,x,lanes){ const N=this.nl, t=this.type;
    if(t===3){ return; } if(t===4){ return; }
    if(N===2){ const o=this.x[c][0].split(x); lanes[0]=o.lo; lanes[1]=o.hi; return; }
    if(N===3){ const o=this.x[c][0].split(x); const u=this.x[c][1].split(o.hi); lanes[0]=this.al[c][0].p(o.lo); lanes[1]=u.lo; lanes[2]=u.hi; return; }
    const o=this.x[c][0].split(x), u=this.x[c][1].split(o.hi), v=this.x[c][2].split(u.hi);
    lanes[0]=this.al[c][2].p(this.al[c][1].p(o.lo)); lanes[1]=this.al[c][3].p(u.lo); lanes[2]=v.lo; lanes[3]=v.hi; }
  dryAligned(c,x){ const N=this.nl, t=this.type; if(t>=3) return x; let y=x; for(let k=0;k<N-1;k++) y=this.dry[c][k].p(y); return y; }
  process(inputs,outputs){ const inp=inputs[0], out=outputs[0]; if(!out||!out[0]) return true;
    const N=out[0].length, L=inp&&inp[0]?inp[0]:null, R=inp&&inp[1]?inp[1]:(L||null);
    const lanes=[0,0,0,0], pk=[0,0,0,0]; let acc=0; const t=this.type;
    for(let i=0;i<N;i++){ const xl=L?L[i]:0, xr=R?R[i]:xl; let oL=0,oR=0;
      if(t===3){ const m=0.5*(xl+xr), s=0.5*(xl-xr); const gm=this.gate[0]*this.gain[0], gs=this.gate[1]*this.gain[1];
        pk[0]=Math.max(pk[0],Math.abs(m)*this.gate[0]); pk[1]=Math.max(pk[1],Math.abs(s)*this.gate[1]); oL=m*gm+s*gs; oR=m*gm-s*gs; }
      else if(t===4){ const gl=this.gate[0]*this.gain[0], gr=this.gate[1]*this.gain[1]; pk[0]=Math.max(pk[0],Math.abs(xl)*this.gate[0]); pk[1]=Math.max(pk[1],Math.abs(xr)*this.gate[1]); oL=xl*gl; oR=xr*gr; }
      else { this.split(0,xl,lanes); for(let k=0;k<this.nl;k++){ const v=lanes[k]*this.gate[k]; pk[k]=Math.max(pk[k],Math.abs(v)); oL+=v*this.gain[k]; }
             this.split(1,xr,lanes); for(let k=0;k<this.nl;k++) oR+=lanes[k]*this.gate[k]*this.gain[k]; }
      const dL=this.dryAligned(0,xl), dR=this.dryAligned(1,xr);
      const wg=Math.sin(1.5707963*this.mix), dg=Math.cos(1.5707963*this.mix);
      const yL=dg*dL+wg*oL, yR=dg*dR+wg*oR; out[0][i]=yL; if(out[1]) out[1][i]=yR; acc+=yL*yL+yR*yR; }
    for(let k=0;k<4;k++) this.pk[k]=Math.max(pk[k],this.pk[k]*0.86);
    this.lvl=0.9*this.lvl+0.1*Math.sqrt(acc/(2*N));
    if(++this.tick>=6){ this.tick=0; this.port.postMessage({nl:this.nl,hz:this.hz.slice(0,3),pk:this.pk.slice(),gt:this.gate.slice(),lvl:this.lvl}); }
    return true; } }
registerProcessor('terrain-splitter',TerrainSplitter);
