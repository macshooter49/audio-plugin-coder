// ══════════════════════════════════════════════════════════════════════════════════════════════
//  warp_filter_probe.cpp — fb543. THE WARP FILTER (modes 35 LP / 36 HP). OVERPASS ONE item 4.
//
//    clang++ -O2 -std=c++17 Tests/warp_filter_probe.cpp -o /tmp/wf \
//      -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//
//  IT PROVES THE ONE CLAIM THE DESIGN RESTS ON: the corner is in HARMONIC NUMBER, so it rides the
//  note instead of sitting at a fixed Hz (the fb467 content-independent-unit law, and the same
//  choice the Low/High cuts already make). Measure the spectral centroid IN HARMONICS at two
//  octaves; a ratio near 1.00 means the corner is at the same harmonic at both pitches.
//
//  MEASURED fb543 (Prophet Saw, C2 vs C4):
//      dry            6.29 / 6.29   ratio 1.00
//      LP 0.60        3.45 / 3.43   ratio 1.01     <- pulls the centroid DOWN, tracks
//      LP 0.85        1.98 / 1.97   ratio 1.00     <- ~2 harmonics left; 128^(1-0.85)=2.07 ✓
//      HP 0.60       11.74 / 11.77  ratio 1.00     <- pushes it UP
//      LP .6 res .9   5.14 / 5.12   ratio 1.00     <- VAR is resonance; it lifts the centroid back
//
//  AND THE FLOOR (a separate run, relative spectrum vs amount 0): floor -72.3 dB, then -34.6 /
//  -24.0 / -13.2 / -6.5 dB at amounts .20/.35/.60/.85 — monotonic, ~38 dB clear of the floor at
//  the FIRST setting, and AT the floor at amount 0 (transparent, the fb462 law).
//
//  ⚠️ TWO TRAPS THIS FEATURE WALKED INTO, BOTH CAUGHT BEFORE SHIPPING:
//   1. warpAmpNeedsDc ends `default: return true` ("+ anything new"), which would have armed a
//      38 Hz DC blocker on top of a LOW-PASS and quietly high-passed it. Explicit cases added.
//   2. index.html keeps its OWN copy of WARP_MODES plus a FAMILY table with a boot guard. A mode
//      added only in C++ is invisible and desyncs the picker. Both were filled.
//  ⚠️ AND A TRAP IN THE MEASUREMENT ITSELF: "last harmonic within -12 dB of dry" reads the NOISE
//     FLOOR above the table's real content and reported no change even for Hard Clip, a mode that
//     definitely works. The relative-spectrum metric is the one that sees it. Always run a
//     known-good mode as the control before believing a null result.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
static const double SR=48000.0, PI=3.14159265358979323846;
static const int BLK=512, WARM=20, MEAS=48;
struct AU{
  AudioUnit au=nullptr; std::map<std::string,AudioUnitParameterID> byName; AudioTimeStamp ts{};
  bool open(const char*t,const char*s,const char*m){
    auto fc=[](const char*z){return (OSType)((z[0]<<24)|(z[1]<<16)|(z[2]<<8)|z[3]);};
    AudioComponentDescription d{}; d.componentType=fc(t); d.componentSubType=fc(s); d.componentManufacturer=fc(m);
    AudioComponent c=AudioComponentFindNext(nullptr,&d); if(!c||AudioComponentInstanceNew(c,&au)!=noErr) return false;
    AudioStreamBasicDescription f{}; f.mSampleRate=SR; f.mFormatID=kAudioFormatLinearPCM;
    f.mFormatFlags=kAudioFormatFlagsNativeFloatPacked|kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket=4;f.mFramesPerPacket=1;f.mBytesPerFrame=4;f.mChannelsPerFrame=2;f.mBitsPerChannel=32;
    AudioUnitSetProperty(au,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,&f,sizeof f);
    UInt32 mx=BLK; AudioUnitSetProperty(au,kAudioUnitProperty_MaximumFramesPerSlice,kAudioUnitScope_Global,0,&mx,sizeof mx);
    if(AudioUnitInitialize(au)!=noErr) return false;
    for(int i=0;i<60;++i) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.02,false);
    UInt32 sz=0; Boolean w=false;
    AudioUnitGetPropertyInfo(au,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,&sz,&w);
    std::vector<AudioUnitParameterID> ids(sz/sizeof(AudioUnitParameterID));
    AudioUnitGetProperty(au,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,ids.data(),&sz);
    for(auto id:ids){AudioUnitParameterInfo pi{};UInt32 s2=sizeof pi;
      if(AudioUnitGetProperty(au,kAudioUnitProperty_ParameterInfo,kAudioUnitScope_Global,id,&pi,&s2)!=noErr)continue;
      char b[256]={0};
      if((pi.flags&kAudioUnitParameterFlag_HasCFNameString)&&pi.cfNameString)CFStringGetCString(pi.cfNameString,b,sizeof b,kCFStringEncodingUTF8);
      else snprintf(b,sizeof b,"%s",pi.name);
      byName[b]=id;}
    return true;}
  void set(const std::string&n,float v){auto it=byName.find(n);if(it==byName.end()){printf("!! no %s\n",n.c_str());return;}
    AudioUnitSetParameter(au,it->second,kAudioUnitScope_Global,0,v,0);}
  void pump(double s){double t=0;while(t<s){CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.02,false);t+=0.02;}}
  // harmonic magnitudes at k*f0 for a held note
  std::vector<double> harm(int note,int K){
    std::vector<float> bl(BLK),br(BLK); std::vector<double> ring;
    AudioBufferList* abl=(AudioBufferList*)calloc(1,sizeof(AudioBufferList)+sizeof(AudioBuffer)); abl->mNumberBuffers=2;
    MusicDeviceMIDIEvent(au,0x90,note,100,0);
    for(int b=0;b<WARM+MEAS;++b){abl->mBuffers[0]={1,(UInt32)(BLK*4),bl.data()};abl->mBuffers[1]={1,(UInt32)(BLK*4),br.data()};
      AudioUnitRenderActionFlags fl=0;AudioUnitRender(au,&fl,&ts,0,BLK,abl);ts.mSampleTime+=BLK;
      if(b>=WARM)for(int i=0;i<BLK;++i)ring.push_back((double)bl[i]);}
    MusicDeviceMIDIEvent(au,0x80,note,0,0);
    for(int b=0;b<8;++b){abl->mBuffers[0]={1,(UInt32)(BLK*4),bl.data()};abl->mBuffers[1]={1,(UInt32)(BLK*4),br.data()};
      AudioUnitRenderActionFlags fl=0;AudioUnitRender(au,&fl,&ts,0,BLK,abl);ts.mSampleTime+=BLK;}
    free(abl);
    const double f0=440.0*std::pow(2.0,(note-69)/12.0);
    const int N=(int)ring.size();
    std::vector<double> H(K+1,0.0);
    for(int k=1;k<=K;++k){ if(k*f0>=SR/2-200) break;
      double re=0,im=0;
      for(int n=0;n<N;n+=2){const double wn=0.5-0.5*std::cos(2.0*PI*n/(N-1));
        const double a=2.0*PI*k*f0*n/SR; re+=ring[n]*wn*std::cos(a); im-=ring[n]*wn*std::sin(a);}
      H[k]=std::sqrt(re*re+im*im);}
    return H;}
};
int main(){
  AU a; if(!a.open("aumu","Tern","Wvcr")){printf("open failed\n");return 1;}
  a.set("Synth OSC A WT Preset", 4.0f);
  auto centroid=[&](int note)->double{                  // spectral centroid in HARMONIC number
    auto H=a.harm(note,120); double n=0,d=0;
    for(size_t k=1;k<H.size();++k){ n+=k*H[k]; d+=H[k]; }
    return d>0? n/d : 0.0; };
  printf("DOES THE CORNER TRACK THE NOTE?  centroid in HARMONIC number at two octaves\n");
  printf("%-14s %10s %10s %10s\n","setting","C2 (h)","C4 (h)","ratio");
  printf("%s\n",std::string(48,'-').c_str());
  struct C { const char* n; float mode, amt, var; };
  C cases[] = { {"dry",35,0.0f,0.0f}, {"LP 0.60",35,0.60f,0.0f}, {"LP 0.85",35,0.85f,0.0f},
                {"HP 0.60",36,0.60f,0.0f}, {"LP .6 res .9",35,0.60f,0.9f} };
  for (auto& c : cases) {
    a.set("Synth OSC A Warp Mode", c.mode);
    a.set("Synth OSC A Warp Amount", c.amt);
    a.set("Synth OSC A Warp Var", c.var);
    a.pump(0.45);
    double c2=centroid(36), c4=centroid(60);
    printf("%-14s %10.2f %10.2f %10.2f\n", c.n, c2, c4, (c4>0? c2/c4 : 0.0));
  }
  printf("\n(a ratio near 1.00 => the corner sits at the SAME HARMONIC at both pitches = it tracks)\n");
  return 0;
}
