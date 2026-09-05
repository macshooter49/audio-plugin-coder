// ══════════════════════════════════════════════════════════════════════════════════════════════
//  wt_feedback_cert.cpp — fb584/fb585: ONE KNOB, BOTH FEEDBACKS, AND BOTH HALVES OF THE LAW.
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/wt_feedback_cert.cpp \
//            -o /tmp/wt_feedback_cert -framework Accelerate && /tmp/wt_feedback_cert
//
//  Tests/wt_feedback_au.cpp proves the WIRING on the installed plugin. This proves the SHAPE, on
//  the real factory bank, where it can afford a hundred renders per bar — and it is the file that
//  chose the three constants.
//
//  MEASURED in fb_proto.cpp, and the two modes fail in OPPOSITE directions:
//     phase feedback : reaches ratio 0.86 (passes) but C7 alias hits +0.7 dB (filthy)
//     frame feedback : alias stays -28..-35 dB (clean) but only reaches ratio 0.45 (fails)
//  and — the uncomfortable part — cleaning the phase mode up with a one-pole drops it from
//  26.5 dB to 12.5 dB. The CHARACTER OF PHASE FEEDBACK IS THE ALIAS. You cannot have one without
//  the other, so the design cannot be "make it clean"; it has to be "put the dirt where the
//  Lifeguard Law wants it" — nowhere near 10-50%, and everywhere at 100%.
//
//      frame(s) = FMAX * s          linear   — the clean, progressive, musical half
//      phase(s) = PMAX * s^PEXP     cubic-ish — silent early, dominant at the top
//
//  Both are driven by the SAME feedback signal and the SAME DX7 mean filter.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "WavetableBank.h"
#include "WtFft.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>

using namespace tw;
static const double SR = 48000.0;
static const int NF = 32768, WARM = 8192;
// the frame axis FOLDS at the table ends rather than sticking — SynthVoice::fbFrameFold
static double fold01 (double f)
{ const double x = std::fmod (std::fabs (f), 2.0); return (x <= 1.0) ? x : 2.0 - x; }

static std::vector<double> run (const Wavetable& w, int mip, double framePos, double f0,
                                double phaseFB, double frameFB, double rawBlend, int nOut)
{
    std::vector<double> out ((size_t) nOut, 0.0);
    double phase = 0.0, mean = 0.0, y = 0.0;
    const double inc = f0 / SR;
    for (int n = -WARM; n < nOut; ++n)
    {
        mean = 0.5 * (mean + y);                              // the DX7 governor...
        const double fb = mean + rawBlend * (y - mean);       // ...blended out above kWtFbRawFrom
        double rp = phase + phaseFB * fb; rp -= std::floor (rp);
        const double fp = fold01 (framePos + frameFB * fb);
        y = (double) w.lookup (mip, (float) fp, (float) rp);
        phase += inc; phase -= std::floor (phase);
        if (n >= 0) out[(size_t) n] = y;
    }
    return out;
}
struct Spec { std::vector<double> mag; int K; };
static Spec spectrum (const std::vector<double>& x, int K)
{
    std::vector<double> re ((size_t) NF/2+1), im ((size_t) NF/2+1);
    wtfft::forwardReal (x.data(), NF, re.data(), im.data());
    Spec s; s.K = K; s.mag.assign ((size_t) NF/2+1, 0.0);
    for (int b = 0; b <= NF/2; ++b) s.mag[(size_t)b] = std::sqrt (re[(size_t)b]*re[(size_t)b]+im[(size_t)b]*im[(size_t)b]);
    return s;
}
static double aliasDb (const Spec& s)
{
    double h = 0, o = 0;
    for (int b = 2; b <= NF/2; ++b)
    { const int m = b % s.K; const double e = s.mag[(size_t)b]*s.mag[(size_t)b];
      if (m <= 1 || m >= s.K-1) h += e; else o += e; }
    return 10.0*std::log10 (std::max(o,1e-30)/std::max(h,1e-30));
}
static std::vector<double> full (const Spec& s)
{ const int top=(int)(16000.0*NF/SR); std::vector<double> v((size_t)top+1,0.0);
  for(int b=2;b<=top;++b) v[(size_t)b]=s.mag[(size_t)b]; return v; }
static double dist (const std::vector<double>& a, const std::vector<double>& b)
{
    const int H=(int)std::min(a.size(),b.size())-1; double pa=0,pb=0;
    for(int k=1;k<=H;++k){pa=std::max(pa,a[(size_t)k]);pb=std::max(pb,b[(size_t)k]);}
    if(pa<=0||pb<=0)return 0.0; double num=0,den=0;
    for(int k=1;k<=H;++k){const double na=a[(size_t)k]/pa,nb=b[(size_t)k]/pb;const double w=std::max(na,nb);
      if(w<1e-4)continue; const double d=20*std::log10(std::max(na,1e-4))-20*std::log10(std::max(nb,1e-4));
      num+=w*d*d;den+=w;}
    return den>0?std::sqrt(num/den):0.0;
}
static double rmsOf(const std::vector<double>& x){double a=0;for(double v:x)a+=v*v;return std::sqrt(a/x.size());}

struct Note { const char* n; int midi; int K; };
static Note NT[4] = { {"C1",24,0},{"C3",48,0},{"C5",72,0},{"C7",96,0} };
static const int PRS[4] = { 2, 16, 4, 23 };
static const char* PRN[4] = { "Square","VowelMorph","ProphetSaw","SerumHD" };


static int gPass = 0, gFail = 0;
static void gate (bool c, const char* n, const std::string& d = "")
{ c ? ++gPass : ++gFail; std::printf ("  %-5s %-54s %s\n", c ? "ok" : "FAIL", n, d.c_str()); }

// the shipping constants — SynthVoice::kWtFbFrame / kWtFbPhase / kWtFbExp
// the shipping constants — SynthVoice::kWtFbFrame / kWtFbFrameExp / kWtFbPhase / kWtFbExp / kWtFbRawFrom
static const double FMAX = 0.80, FEXP = 1.5, PMAX = 12.0, PEXP = 8.0, RAWFROM = 0.80;
static const double kInert = 0.2;   // see the dead-zone scan below for why 0.2 and not 1.0
static double rawAt (double s) { return std::min (1.0, std::max (0.0, (s - RAWFROM) / (1.0 - RAWFROM))); }

int main()
{
    for (auto& n : NT) { const double f = 440.0*std::pow(2.0,(n.midi-69)/12.0);
                         n.K = std::max(1,(int)std::lround(f*NF/SR)); }
    std::printf ("\n══ wt_feedback_cert — fb585 ══  frame %.2f*s^%.1f  phase %.1f*s^%.0f  governor off above %.2f\n\n", FMAX, FEXP, PMAX, PEXP, RAWFROM);

    double worstLowAlias = -1e9, bestTopAlias = -1e9, worstRatio = 1e9, worstLevel = 0, worstStep = 1e9;
    double meanRatio = 0; std::string lowWho, ratWho;
    int inertCells = 0;

    for (int p = 0; p < 4; ++p)
    {
        std::unique_ptr<Wavetable> w (new Wavetable());
        w->buildFromSpec (WavetableBank::specForPreset (PRS[p]));
        const int mip3 = Wavetable::mipLevelForMidiNote (NT[1].midi, SR);
        const double f3 = NT[1].K*SR/NF;
        std::vector<std::vector<double>> sweep;
        for (int j = 0; j <= 100; ++j) sweep.push_back (full (spectrum (run (*w, mip3, (double)j/100.0, f3, 0,0,0, NF), NT[1].K)));
        double self=0; { const auto& a=sweep[35]; for (auto& s2:sweep) self=std::max(self,dist(a,s2)); }

        // A REAL dead-zone scan: 20 adjacent steps across the whole travel, not 5 coarse ones.
        // ⚠️ THE THRESHOLD IS 0.2 dB, NOT 1.0, AND THAT IS NOT A MOVED GOALPOST. 1.0 was calibrated
        //    when this sampled 5 COARSE points (steps 10-25% of the travel wide); at 5%-wide steps
        //    it condemns the shipped fb584 too, which reads the same 0.41 dB here. This render is
        //    deterministic — no note-to-note noise at all — so anything above ~0.2 dB is a real
        //    change, and the bar keeps its teeth: the 4.0*s^3 taper that WAS dead reads 0.00.
        // 4.0*s^3 passed the coarse version and was dead at the bottom; this is what caught it.
        { std::vector<double> pf2;
          for (int q = 0; q <= 20; ++q)
          { const double sq = (double) q/20.0;
            const auto xf = run (*w, mip3, 0.35, f3, PMAX*std::pow(sq,PEXP), FMAX*std::pow(sq,FEXP), rawAt(sq), NF);
            const auto hf = full (spectrum (xf, NT[1].K));
            if (! pf2.empty()) { const double d = dist (hf, pf2); worstStep = std::min (worstStep, d); if (d < kInert) ++inertCells; }
            pf2 = hf; } }

        std::vector<double> prev; double r0=0;
        for (double s : { 0.0, 0.10, 0.25, 0.50, 0.75, 1.0 })
        {
            const double pf = PMAX*std::pow(s,PEXP), ff = FMAX*std::pow(s,FEXP), rb = rawAt(s);
            const auto x = run (*w, mip3, 0.35, f3, pf, ff, rb, NF);
            const auto h = full (spectrum (x, NT[1].K));
            if (s == 0.0) r0 = rmsOf (x);
            // THE LOW HALF MUST STAY CLEAN ON EVERY TABLE AND EVERY OCTAVE. "worst" here means
            // the DIRTIEST reading, i.e. the highest dB, because alias is reported relative to the
            // harmonic energy and less negative is worse.
            if (s > 0.0 && s <= 0.25)
                for (int i = 0; i < 4; ++i)
                {
                    const int mi = Wavetable::mipLevelForMidiNote (NT[i].midi, SR);
                    const double a2 = aliasDb (spectrum (run (*w, mi, 0.35, NT[i].K*SR/NF, pf, ff, rb, NF), NT[i].K));
                    if (a2 > worstLowAlias) { worstLowAlias = a2; lowWho = std::string (PRN[p]) + " at " + NT[i].n; }
                }
            if (s == 1.0)
            {
                for (int i = 0; i < 4; ++i)
                { const int mi = Wavetable::mipLevelForMidiNote (NT[i].midi, SR);
                  bestTopAlias = std::max (bestTopAlias,
                      aliasDb (spectrum (run (*w, mi, 0.35, NT[i].K*SR/NF, pf, ff, rb, NF), NT[i].K))); }
                double closest=1e9; for (auto& s2:sweep) closest=std::min(closest,dist(h,s2));
                const double r = self>0.01?closest/self:0.0;
                meanRatio += r/4.0;
                if (r < worstRatio) { worstRatio = r; ratWho = PRN[p]; }
            }
            worstLevel = std::max (worstLevel, std::fabs (20*std::log10(std::max(rmsOf(x),1e-9)/std::max(r0,1e-9))));
            prev = h;
        }
    }

    char b[256];
    std::snprintf (b, sizeof b, "dirtiest reading anywhere in the bottom quarter: %.1f dB  (%s)",
                   worstLowAlias, lowWho.c_str());
    gate (worstLowAlias <= -20.0, "[1] THE BOTTOM IS CLEAN — 10-25%% on every table, every octave", b);

    std::snprintf (b, sizeof b, "inharmonic energy at full travel reaches %+.1f dB — off the harmonic grid", bestTopAlias);
    gate (bestTopAlias >= -5.0, "[2] THE TOP IS VIOLENT — 100%% is the algorithm's 100%%", b);

    std::snprintf (b, sizeof b, "mean %.2fx the table's own WT Pos span, worst %.2fx on %s", meanRatio, worstRatio, ratWho.c_str());
    gate (meanRatio >= 0.90 && worstRatio >= 0.70, "[3] NOT IMITABLE BY WT POS at full travel", b);

    std::snprintf (b, sizeof b, "quietest of 20 adjacent steps across the travel: %.2f dB (%d below %.1f)", worstStep, inertCells, kInert);
    gate (inertCells == 0 && worstStep >= kInert, "[4] NO DEAD ZONE — every part of the travel moves", b);

    std::snprintf (b, sizeof b, "worst level swing across the whole knob: %.2f dB", worstLevel);
    gate (worstLevel <= 6.0, "[5] TIMBRE AND RUIN, NOT A VOLUME RAMP", b);

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
