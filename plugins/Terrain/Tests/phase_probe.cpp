// ══════════════════════════════════════════════════════════════════════════════════════════════
//  phase_probe.cpp — fb532. DOES IT ACTUALLY PHASE?  THE MOVEMENT GATE FOR NOTE-ON PHASE.
//
//    clang++ -O2 -std=c++17 Tests/phase_probe.cpp -o /tmp/phase_probe \
//      -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//
//    /tmp/phase_probe aumu Tern Wvcr @8                                  # default patch
//    /tmp/phase_probe aumu Tern Wvcr "Synth OSC A Phase Mode=0" "Synth OSC A Phase Amount=0" \
//                     "Synth OSC A Phase=0" @3 "Synth OSC A Phase=0.25" @3
//    /tmp/phase_probe aumu Xf2X XFER "A Rand Phase=0" "A Phase=0" @4     # THE REFERENCE
//
//  It hosts ANY AU by (type, subtype, manufacturer), so the same rig measures us and Serum 2 —
//  which is the whole point: the target is a NUMBER measured off the reference's own output.
//  ⚖️ CLEAN ROOM. We drive the reference's published parameters and measure the audio it emits.
//  Nothing is decompiled, extracted or transcribed.
//
//  WHY IT EXISTS. `SYN_OSC_x_PHASE` and `_PHASE_AMT` were registered, pulled, pushed to the voice
//  and consumed in the render loop — and were still INERT, because the shipped default mode was
//  FREE, whose resolvePhase branch returns the carried accumulator and ignores both knobs. Nothing
//  catches that: it compiles, auval passes, and reading the code makes it look wired. Only
//  measuring the START PHASE OF A NOTE catches it.
//
//  THE TELL. In FREE the measured phases march CONTINUOUSLY ACROSS A PARAMETER CHANGE —
//  270 -> 269 -> 321 -> 322 -> 14 -> 14 -> 67 -> 67 -> 120 -> 120 -> 172 -> 173, a steady
//  +52.5 deg/note that is the free accumulator, with NO jump injected when Phase moves 0 -> 0.25.
//
//  MEASURED, fb532 (R = circular resultant: 1.0000 = every note identical, ~0 = uniformly random):
//                                    OURS        SERUM 2 v2.1.4
//    Rand 0, Phase 0.00              268.75      267.06          <- 1.69 deg apart: our sine vs
//    Rand 0, Phase 0.25              358.75      357.06             their saw, not a behaviour gap
//    Rand 0, Phase 0.50               88.75       87.06
//    Rand 0, Phase 0.75              178.75      177.06
//    => EXACTLY +90.00 deg per quarter turn on BOTH, R = 1.0000 on BOTH.
//    Rand 0 / 0.25 / 0.50 / 1.00  ->  R 1.0000 / 0.9345 / 0.7700 / 0.4048   (ours)
//                                     R 1.0000 / 0.9922 / 0.9124 / 0.6939   (Serum)
//    Ours goes DEEPER at full — closer to true uniform, where Serum ropes off the top. That is
//    the LIFEGUARD LAW (the knob's 100% must be the algorithm's 100%), so it is deliberate and
//    must NOT be "corrected" to match their shallower taper.
//
//  🚨 THE GATE: a fresh default instance must measure R < 0.6. It measured 0.2771 at fb532.
//     If that ever returns to ~1.0 with a marching sequence, phase has gone inert again.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

static const double SR = 48000.0;
static const int BLK = 512;
static const int SKIP = 1024;      // samples skipped after note-on (attack / click)
static const int WIN  = 8192;      // measurement span
static const double PI = 3.14159265358979323846;

struct AU
{
    AudioUnit au = nullptr;
    std::map<std::string, AudioUnitParameterID> byName;
    std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    AudioTimeStamp ts {};

    bool open (const char* t, const char* s, const char* m)
    {
        auto fc=[](const char* z){ return (OSType)((z[0]<<24)|(z[1]<<16)|(z[2]<<8)|z[3]); };
        AudioComponentDescription d {}; d.componentType=fc(t); d.componentSubType=fc(s); d.componentManufacturer=fc(m);
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c || AudioComponentInstanceNew (c, &au) != noErr) return false;
        AudioStreamBasicDescription f {}; f.mSampleRate=SR; f.mFormatID=kAudioFormatLinearPCM;
        f.mFormatFlags=kAudioFormatFlagsNativeFloatPacked|kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket=4; f.mFramesPerPacket=1; f.mBytesPerFrame=4; f.mChannelsPerFrame=2; f.mBitsPerChannel=32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx=BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) return false;
        pump (1.0);
        UInt32 sz=0; Boolean w=false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz/sizeof(AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids) { AudioUnitParameterInfo pi{}; UInt32 s2=sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s2)!=noErr) continue;
            char b[256]={0};
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString) CFStringGetCString (pi.cfNameString,b,sizeof b,kCFStringEncodingUTF8);
            else snprintf (b,sizeof b,"%s",pi.name);
            byName[b]=id; info[id]=pi; }
        return true;
    }
    void close(){ if(au){ AudioUnitUninitialize(au); AudioComponentInstanceDispose(au); au=nullptr; } }
    void pump (double s){ double t=0; while(t<s){ CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.02,false); t+=0.02; } }
    bool has (const std::string& n) const { return byName.count(n)!=0; }
    void setRaw (const std::string& n, float v)
    { auto it=byName.find(n); if(it==byName.end()){ printf("  !! no param '%s'\n",n.c_str()); return; }
      AudioUnitSetParameter (au,it->second,kAudioUnitScope_Global,0,v,0); }

    void render (int n, std::vector<double>& out, float* bl, float* br, AudioBufferList* abl)
    {
        int done=0;
        while (done < n) {
            abl->mBuffers[0]={1,(UInt32)(BLK*4),bl}; abl->mBuffers[1]={1,(UInt32)(BLK*4),br};
            AudioUnitRenderActionFlags fl=0;
            AudioUnitRender (au,&fl,&ts,0,BLK,abl); ts.mSampleTime += BLK;
            for (int i=0;i<BLK && done<n;++i,++done) out.push_back((double)bl[i]);
        }
    }
    // start phase in DEGREES [0,360) for one note-on, or NAN if the note is silent
    double startPhase (int note)
    {
        std::vector<float> bl(BLK), br(BLK);
        AudioBufferList* abl=(AudioBufferList*)calloc(1,sizeof(AudioBufferList)+sizeof(AudioBuffer));
        abl->mNumberBuffers=2;
        std::vector<double> sig; sig.reserve(SKIP+WIN+BLK);
        MusicDeviceMIDIEvent (au,0x90,note,100,0);
        render (SKIP+WIN, sig, bl.data(), br.data(), abl);
        MusicDeviceMIDIEvent (au,0x80,note,0,0);
        std::vector<double> tail; render (BLK*10, tail, bl.data(), br.data(), abl);   // release
        free (abl);
        const double f0 = 440.0*std::pow(2.0,(note-69)/12.0);
        double C=0,S=0,pw=0;
        for (int n2=0;n2<WIN;++n2){
            const double x=sig[(size_t)(SKIP+n2)];
            const double wnd=0.5-0.5*std::cos(2.0*PI*n2/(WIN-1));
            const double a=2.0*PI*f0*n2/SR;
            C+=wnd*x*std::cos(a); S+=wnd*x*std::sin(a); pw+=wnd*x*x;
        }
        if (pw < 1e-12) return NAN;                       // silent
        double psi=std::atan2(-S,C);
        double phi=psi - 2.0*PI*f0*SKIP/SR;               // back-propagate to note-on
        phi=std::fmod(phi,2.0*PI); if(phi<0) phi+=2.0*PI;
        return phi*180.0/PI;
    }
};

static double circMeanAbsDev (const std::vector<double>& d)   // spread on a circle, degrees
{
    double sx=0,sy=0; for(double v:d){ sx+=std::cos(v*PI/180.0); sy+=std::sin(v*PI/180.0); }
    const double R=std::sqrt(sx*sx+sy*sy)/(double)d.size();   // 1 = identical, 0 = uniform
    return R;
}
int main (int argc, char** argv)
{
    if (argc<4){ printf("usage: phase_probe <type> <sub> <mfr> [name=val ...] -- runs\n"); return 2; }
    AU a; if(!a.open(argv[1],argv[2],argv[3])){ printf("open failed\n"); return 1; }
    printf("opened. params=%zu\n",a.byName.size());
    // remaining args: NAME=VALUE (raw), or "@N" to run N notes and report
    std::vector<double> phases;
    int notes=6;
    for (int i=4;i<argc;++i){
        std::string s=argv[i];
        if (s[0]=='@'){ notes=atoi(s.c_str()+1);
            phases.clear();
            for(int k=0;k<notes;++k){ double p=a.startPhase(45); phases.push_back(p);
                printf("    note %2d  phase %8.2f deg%s\n",k,p,std::isnan(p)?"  (SILENT)":""); }
            std::vector<double> ok; for(double v:phases) if(!std::isnan(v)) ok.push_back(v);
            if(ok.size()>1){ printf("    R = %.4f   (1.000 = identical every note, ~0 = uniformly random)\n", circMeanAbsDev(ok)); }
            continue; }
        const size_t eq=s.find('=');
        if(eq==std::string::npos) continue;
        const std::string n=s.substr(0,eq); const float v=(float)atof(s.c_str()+eq+1);
        a.setRaw(n,v); a.pump(0.15);
        printf("  set %-28s = %g\n",n.c_str(),v);
    }
    a.close();
    return 0;
}
