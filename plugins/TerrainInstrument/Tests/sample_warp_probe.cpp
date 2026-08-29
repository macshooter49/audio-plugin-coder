// ══════════════════════════════════════════════════════════════════════════════════════════════
//  sample_warp_probe.cpp — fb542. WHICH ENGINES DOES THE BACK-PANEL WARP PILL ACTUALLY REACH?
//
//    clang++ -O2 -std=c++17 Tests/sample_warp_probe.cpp -o /tmp/sw \
//      -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//
//  WHY. The back-panel warp pill is ENGINE-AWARE: on WT/FM it drives SYN_OSC_x_WARP2_*, and on
//  Sample / Granular / Resynth / Harmonics it drives SYN_OSC_x_SAMPLE_WARP(_MODE) instead. Because
//  the JS builds that id dynamically ('SYN_OSC_' + letter + '_SAMPLE_WARP'), a literal grep of
//  index.html finds NOTHING and the parameter looks stranded. It is not — I claimed it was, twice,
//  and was wrong both times. This measures it instead of reading it.
//
//  It also settles where the shaper lives: NOT inside renderSampleOsc (which is gated on
//  Engine::SAMP) but in renderNextBlock, the SHARED per-sample path — which is exactly why it
//  reaches engines other than Sample.
//
//  MEASURED fb542 (Sample Warp 0 -> 1 on mode 4 = Drive, against a same-settings floor):
//      HARM   +14.3 dB over a  -88.1 dB floor   -> reaches the audio
//      GEODE   +5.9 dB over a  -91.1 dB floor   -> reaches the audio
//      GRAN / SAMP: BOTH READ -999. That is SILENCE, not "no effect" — neither engine renders
//      anything with no sample loaded, and a ratio against a silent reference is not a
//      measurement. To test those, load a sample first and re-run.
//  🚨 That last line is the trap this file exists to stop anyone walking into again.
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
static const double SR=48000.0; static const int BLK=512, WARM=24, MEAS=64, NFFT=4096;
static const double PI=3.14159265358979323846;
struct AU {
  AudioUnit au=nullptr; std::map<std::string,AudioUnitParameterID> byName;
  AudioTimeStamp ts{};
  bool open(){
    AudioComponentDescription d{}; d.componentType=kAudioUnitType_MusicDevice;
    d.componentSubType='Tern'; d.componentManufacturer='Wvcr';
    AudioComponent c=AudioComponentFindNext(nullptr,&d);
    if(!c||AudioComponentInstanceNew(c,&au)!=noErr) return false;
    AudioStreamBasicDescription f{}; f.mSampleRate=SR; f.mFormatID=kAudioFormatLinearPCM;
    f.mFormatFlags=kAudioFormatFlagsNativeFloatPacked|kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket=4; f.mFramesPerPacket=1; f.mBytesPerFrame=4; f.mChannelsPerFrame=2; f.mBitsPerChannel=32;
    AudioUnitSetProperty(au,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,&f,sizeof f);
    UInt32 mx=BLK; AudioUnitSetProperty(au,kAudioUnitProperty_MaximumFramesPerSlice,kAudioUnitScope_Global,0,&mx,sizeof mx);
    if(AudioUnitInitialize(au)!=noErr) return false;
    for(int i=0;i<50;++i) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.02,false);
    UInt32 sz=0; Boolean w=false;
    AudioUnitGetPropertyInfo(au,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,&sz,&w);
    std::vector<AudioUnitParameterID> ids(sz/sizeof(AudioUnitParameterID));
    AudioUnitGetProperty(au,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,ids.data(),&sz);
    for(auto id:ids){ AudioUnitParameterInfo pi{}; UInt32 s2=sizeof pi;
      if(AudioUnitGetProperty(au,kAudioUnitProperty_ParameterInfo,kAudioUnitScope_Global,id,&pi,&s2)!=noErr) continue;
      char b[256]={0};
      if((pi.flags&kAudioUnitParameterFlag_HasCFNameString)&&pi.cfNameString) CFStringGetCString(pi.cfNameString,b,sizeof b,kCFStringEncodingUTF8);
      else snprintf(b,sizeof b,"%s",pi.name);
      byName[b]=id; }
    return true; }
  void set(const std::string&n,float v){ auto it=byName.find(n); if(it==byName.end()){printf("!! no %s\n",n.c_str());return;}
    AudioUnitSetParameter(au,it->second,kAudioUnitScope_Global,0,v,0); }
  void pump(double s){ double t=0; while(t<s){ CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.02,false); t+=0.02; } }
  std::vector<double> spec(int note=45){
    std::vector<float> bl(BLK),br(BLK);
    AudioBufferList* abl=(AudioBufferList*)calloc(1,sizeof(AudioBufferList)+sizeof(AudioBuffer)); abl->mNumberBuffers=2;
    std::vector<double> ring;
    MusicDeviceMIDIEvent(au,0x90,note,100,0);
    for(int b=0;b<WARM+MEAS;++b){
      abl->mBuffers[0]={1,(UInt32)(BLK*4),bl.data()}; abl->mBuffers[1]={1,(UInt32)(BLK*4),br.data()};
      AudioUnitRenderActionFlags fl=0; AudioUnitRender(au,&fl,&ts,0,BLK,abl); ts.mSampleTime+=BLK;
      if(b>=WARM) for(int i=0;i<BLK;++i) ring.push_back((double)bl[i]); }
    MusicDeviceMIDIEvent(au,0x80,note,0,0);
    for(int b=0;b<10;++b){ abl->mBuffers[0]={1,(UInt32)(BLK*4),bl.data()}; abl->mBuffers[1]={1,(UInt32)(BLK*4),br.data()};
      AudioUnitRenderActionFlags fl=0; AudioUnitRender(au,&fl,&ts,0,BLK,abl); ts.mSampleTime+=BLK; }
    free(abl);
    std::vector<double> acc(NFFT/2+1,0.0); int frames=0;
    for(size_t off=0; off+NFFT<=ring.size(); off+=NFFT/2){
      for(int k=1;k<=NFFT/2;++k){ double re=0,im=0;
        for(int n=0;n<NFFT;n+=4){ const double wnd=0.5-0.5*std::cos(2.0*PI*n/(NFFT-1));
          const double x=ring[off+n]*wnd, a=2.0*PI*k*n/NFFT;
          re+=x*std::cos(a); im-=x*std::sin(a); }
        acc[k]+=std::sqrt(re*re+im*im); }
      if(++frames>=3) break; }
    for(auto&v:acc) v/=std::max(1,frames);
    return acc; }
};
int main(int argc,char**argv){
  AU a; if(!a.open()){printf("open failed\n");return 1;}
  // Does the back-panel pill's SAMPLE WARP actually reach each engine's audio?
  struct E { const char* name; float idx; };
  E engines[] = { {"HARM",5}, {"GRAN",2}, {"GEODE",3}, {"SAMP",1} };
  printf("%-7s %12s %12s\n","engine","floor 0->0","warp 0->1");
  printf("%s\n", std::string(34,'-').c_str());
  for (auto& e : engines) {
    a.set("Synth OSC A Engine", e.idx);
    a.set("Synth OSC A Sample Warp Mode", 4.0f);      // Drive
    a.pump(0.4);
    a.set("Synth OSC A Sample Warp", 0.0f); a.pump(0.25); auto s0=a.spec();
    a.set("Synth OSC A Sample Warp", 0.0f); a.pump(0.25); auto sF=a.spec();   // floor
    a.set("Synth OSC A Sample Warp", 1.0f); a.pump(0.25); auto s1=a.spec();
    auto rel=[&](std::vector<double>&x){ double num=0,den=0;
      for(size_t k=1;k<s0.size();++k){ double d=x[k]-s0[k]; num+=d*d; den+=s0[k]*s0[k]; }
      return (den>1e-20)? 10.0*std::log10(std::max(num,1e-30)/den) : -999.0; };
    double fl=rel(sF), wp=rel(s1);
    printf("%-7s %9.1f dB %9.1f dB  %s\n", e.name, fl, wp,
           (fl < -900.0) ? "SILENT - unmeasurable, load a sample" : ((wp > fl + 12.0) ? "REACHES the audio" : "no effect"));
  }
  return 0;
}
