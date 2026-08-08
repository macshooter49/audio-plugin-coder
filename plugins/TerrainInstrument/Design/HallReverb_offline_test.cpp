// Offline validation for HallReverb.h — fb277: click-free (interp fix + smoothing)
// AND still-correct DSP (RT60/damping/width/mix). Clicks = first-difference spikes
// while sweeping a param vs steady play.
#include "HallReverb.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static const float FS = 48000.0f;

static void renderIR (HallReverb& rv, std::vector<float>& l, std::vector<float>& r, int n)
{
    l.assign(n,0.f); r.assign(n,0.f);
    for (int i=0;i<n;++i){ float in=(i==0)?1.f:0.f, wl,wr; rv.processSample(in,in,wl,wr); l[i]=wl; r[i]=wr; }
}
static float rt60 (const std::vector<float>& x){
    const int F=2048,H=1024; std::vector<float> t,db;
    for (int i=0;i+F<(int)x.size();i+=H){ double e=0; for(int j=0;j<F;++j) e+=(double)x[i+j]*x[i+j];
        db.push_back(10.f*std::log10((float)(e/F)+1e-30f)); t.push_back((i+F*0.5f)/FS); }
    int pk=(int)(std::max_element(db.begin(),db.end())-db.begin()); float pkdb=db[pk];
    double sx=0,sy=0,sxx=0,sxy=0; int n=0;
    for (int i=pk+2;i<(int)db.size();++i){ if(db[i]<pkdb-65.f) break; sx+=t[i];sy+=db[i];sxx+=t[i]*t[i];sxy+=t[i]*db[i];++n; }
    if(n<4) return -1.f; double sl=(n*sxy-sx*sy)/(n*sxx-sx*sx); if(sl>=-1e-4) return 999.f; return (float)(-60.0/sl);
}
static float hfRatio (const std::vector<float>& x){ double hi=0,tot=0; float p=0;
    for(size_t i=4800;i<x.size();++i){ float d=x[i]-p; p=x[i]; hi+=(double)d*d; tot+=(double)x[i]*x[i]; } return (float)(hi/(tot+1e-30)); }
static float corrLR (const std::vector<float>& l,const std::vector<float>& r){ double sll=0,srr=0,slr=0;
    for(size_t i=4800;i<l.size();++i){ sll+=(double)l[i]*l[i]; srr+=(double)r[i]*r[i]; slr+=(double)l[i]*r[i]; } return (float)(slr/(std::sqrt(sll*srr)+1e-30)); }
static float peakAbs (const std::vector<float>& x){ float m=0; for(float v:x){ if(!std::isfinite(v)) return 1e9f; m=std::max(m,std::fabs(v)); } return m; }
// max |x[n]-x[n-1]| over the STEADY region [from..]
static float maxDiff (const std::vector<float>& x, int from){ float m=0; for(int i=from+1;i<(int)x.size();++i){ float d=std::fabs(x[i]-x[i-1]); if(!std::isfinite(d)) return 1e9f; m=std::max(m,d);} return m; }

// feed a steady sine; optional per-block sweep of a param; return wet L
enum Sweep { NONE, SIZE, DECAY_W, WIDTH_W, DAMP_W };
static void renderSine (HallReverb& rv, std::vector<float>& out, int n, Sweep sw)
{
    out.assign(n,0.f); const int B=64; double ph=0, inc=2*M_PI*220.0/FS;
    for (int b=0;b*B<n;++b){
        if (sw!=NONE){ float t=(float)(b*B)/(float)n;
            if(sw==SIZE)   rv.setSize(0.3f+0.6f*t);
            if(sw==DECAY_W)rv.setDecay(0.3f+0.6f*t);
            if(sw==WIDTH_W)rv.setWidth(1.0f*t);
            if(sw==DAMP_W) rv.setHighDamping(1.0f*t);
            rv.updateCoefficients();
        }
        for (int j=0;j<B && b*B+j<n;++j){ float in=0.3f*(float)std::sin(ph); ph+=inc; float wl,wr; rv.processSample(in,in,wl,wr); out[b*B+j]=wl; }
    }
}

int main()
{
    int pass=0,fail=0;
    auto CK=[&](const char* nm,bool ok,const char* d){ printf("  [%s] %s — %s\n",ok?"PASS":"FAIL",nm,d); if(ok)++pass; else ++fail; };
    std::vector<float> l,r; const int NS=(int)(12.f*FS);
    printf("HallReverb fb277 validation @ %.0f Hz\n",FS);

    // ---- DSP correctness (unchanged behaviour) ----
    HallReverb rv; rv.prepare(FS);
    rv.setDecay(0.15f);rv.setSize(0.4f);rv.setHighDamping(0.2f);rv.setModDepth(0.f);rv.updateCoefficients();
    renderIR(rv,l,r,NS); float rtLo=rt60(l);
    rv.reset(); rv.setDecay(0.9f); rv.updateCoefficients(); renderIR(rv,l,r,NS); float rtHi=rt60(l), pkHi=peakAbs(l);
    { char b[128]; snprintf(b,128,"RT60 %.2fs -> %.2fs (%.1fx)",rtLo,rtHi,rtHi/(rtLo+1e-6f)); CK("Decay dramatic",rtLo>0.2f&&rtHi>3.f&&rtHi>rtLo*3.f,b); }
    rv.reset(); rv.setDecay(1.f);rv.setModDepth(1.f);rv.setModRate(2.f);rv.setLowDecay(2.f);rv.updateCoefficients(); renderIR(rv,l,r,NS); float pkF=peakAbs(l);
    { char b[128]; snprintf(b,128,"peaks long=%.3f freeze=%.3f",pkHi,pkF); CK("Stable (no blow-up/NaN)",pkHi<4&&pkF<8&&pkF>0,b); }
    rv.reset(); rv.setDecay(0.7f);rv.setModDepth(0.2f);rv.setLowDecay(1.f);rv.setHighDamping(0.f);rv.updateCoefficients(); renderIR(rv,l,r,NS); float hf0=hfRatio(l);
    rv.reset(); rv.setHighDamping(1.f); rv.updateCoefficients(); renderIR(rv,l,r,NS); float hf1=hfRatio(l);
    { char b[128]; snprintf(b,128,"HF damp0=%.4f damp100=%.4f (%.1fx darker)",hf0,hf1,hf0/(hf1+1e-9f)); CK("High Damping darkens",hf1<hf0*0.6f,b); }
    rv.reset(); rv.setHighDamping(0.3f);rv.setWidth(0.f);rv.updateCoefficients(); renderIR(rv,l,r,NS); float c0=corrLR(l,r);
    rv.reset(); rv.setWidth(1.f);rv.updateCoefficients(); renderIR(rv,l,r,NS); float c1=corrLR(l,r);
    { char b[128]; snprintf(b,128,"corr width0=%.3f width100=%.3f",c0,c1); CK("Width decorrelates",c0>0.85f&&c1<0.6f,b); }

    // ---- CLICK-FREE (the fb277 fix) ----
    int skip=(int)(1.5f*FS);   // ignore reverb build-up transient
    HallReverb s; s.prepare(FS); s.setModDepth(0.4f); s.setModRate(1.5f); s.setSize(0.5f); s.setDecay(0.6f); s.updateCoefficients();
    std::vector<float> steady,szsw,dcsw,wdsw,dmsw;
    renderSine(s,steady,NS,NONE);   float dSteady=maxDiff(steady,skip), pkS=peakAbs(steady);
    s.reset(); s.setSize(0.3f); s.updateCoefficients(); renderSine(s,szsw,NS,SIZE);    float dSize=maxDiff(szsw,skip);
    s.reset(); s.updateCoefficients();                  renderSine(s,dcsw,NS,DECAY_W); float dDec=maxDiff(dcsw,skip);
    s.reset(); s.updateCoefficients();                  renderSine(s,wdsw,NS,WIDTH_W); float dWid=maxDiff(wdsw,skip);
    s.reset(); s.updateCoefficients();                  renderSine(s,dmsw,NS,DAMP_W);  float dDam=maxDiff(dmsw,skip);
    { char b[192]; snprintf(b,192,"maxDiff steady=%.5f (peak %.3f, ratio %.3f) — mod-on interp smooth",dSteady,pkS,dSteady/(pkS+1e-9f));
      CK("Steady play click-free (interp)", dSteady/(pkS+1e-9f) < 0.20f, b); }
    { char b[192]; snprintf(b,192,"maxDiff sweep: Size=%.5f Decay=%.5f Width=%.5f Damp=%.5f  vs steady %.5f",dSize,dDec,dWid,dDam,dSteady);
      CK("Param sweeps click-free", dSize<4*dSteady && dDec<4*dSteady && dWid<4*dSteady && dDam<4*dSteady, b); }

    printf("\nRESULT: %d passed, %d failed\n",pass,fail);
    return fail==0?0:1;
}
