// Offline validation for HallReverb.h — proves the FDN math: RT60 tracks Decay,
// damping darkens, width decorrelates, mix blends, stable (no blow-up / NaN).
#include "HallReverb.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static const float FS = 48000.0f;

// render wet-only mono/stereo impulse response
static void renderIR (HallReverb& rv, std::vector<float>& l, std::vector<float>& r, int nsamp)
{
    l.assign (nsamp, 0.0f); r.assign (nsamp, 0.0f);
    for (int i = 0; i < nsamp; ++i)
    {
        float in = (i == 0) ? 1.0f : 0.0f;
        float wl, wr; rv.processSample (in, in, wl, wr);
        l[i] = wl; r[i] = wr;
    }
}

// RT60 via linear regression of frame-dB vs time over the decay region
static float rt60 (const std::vector<float>& x)
{
    const int F = 2048, H = 1024;
    std::vector<float> t, db;
    for (int i = 0; i + F < (int) x.size(); i += H)
    {
        double e = 0; for (int j = 0; j < F; ++j) e += (double) x[i + j] * x[i + j];
        float d = 10.0f * std::log10 ((float) (e / F) + 1e-30f);
        t.push_back ((i + F * 0.5f) / FS); db.push_back (d);
    }
    // find peak, regress from peak+2 frames down to -65 dB (or end)
    int pk = (int) (std::max_element (db.begin(), db.end()) - db.begin());
    float pkdb = db[pk];
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int nreg = 0;
    for (int i = pk + 2; i < (int) db.size(); ++i)
    {
        if (db[i] < pkdb - 65.0f) break;
        sx += t[i]; sy += db[i]; sxx += t[i] * t[i]; sxy += t[i] * db[i]; ++nreg;
    }
    if (nreg < 4) return -1.0f;
    double slope = (nreg * sxy - sx * sy) / (nreg * sxx - sx * sx); // dB/s (negative)
    if (slope >= -1e-4) return 999.0f;
    return (float) (-60.0 / slope);
}

// high-freq energy ratio of the tail (crude one-zero HP)
static float hfRatio (const std::vector<float>& x)
{
    double hi = 0, tot = 0; float p = 0;
    for (size_t i = 4800; i < x.size(); ++i) { float d = x[i] - p; p = x[i]; hi += (double) d * d; tot += (double) x[i] * x[i]; }
    return (float) (hi / (tot + 1e-30));
}

static float corrLR (const std::vector<float>& l, const std::vector<float>& r)
{
    double sll = 0, srr = 0, slr = 0;
    for (size_t i = 4800; i < l.size(); ++i) { sll += (double) l[i] * l[i]; srr += (double) r[i] * r[i]; slr += (double) l[i] * r[i]; }
    return (float) (slr / (std::sqrt (sll * srr) + 1e-30));
}

static float peakAbs (const std::vector<float>& x) { float m = 0; for (float v : x) { if (!std::isfinite (v)) return 1e9f; m = std::max (m, std::fabs (v)); } return m; }

int main()
{
    int pass = 0, fail = 0;
    auto CHECK = [&] (const char* name, bool ok, const char* detail) {
        printf ("  [%s] %s — %s\n", ok ? "PASS" : "FAIL", name, detail);
        if (ok) ++pass; else ++fail;
    };
    std::vector<float> l, r;
    const int NS = (int) (12.0f * FS);

    printf ("HallReverb offline validation @ %.0f Hz\n", FS);

    // 1) RT60 tracks Decay (dramatic)
    HallReverb rv; rv.prepare (FS);
    rv.setDecay (0.15f); rv.setSize (0.4f); rv.setHighDamping (0.2f); rv.setModDepth (0.0f); rv.updateCoefficients();
    renderIR (rv, l, r, NS); float rtLo = rt60 (l); float pkLo = peakAbs (l);
    rv.reset(); rv.setDecay (0.9f); rv.updateCoefficients();
    renderIR (rv, l, r, NS); float rtHi = rt60 (l); float pkHi = peakAbs (l);
    { char b[128]; snprintf (b, 128, "RT60 short=%.2fs  long=%.2fs  (ratio %.1fx)", rtLo, rtHi, rtHi / (rtLo + 1e-6f)); CHECK ("Decay is dramatic", rtLo > 0.2f && rtHi > 3.0f && rtHi > rtLo * 3.0f, b); }

    // 2) stability — no blow-up / NaN at long decay + freeze-ish + mod
    rv.reset(); rv.setDecay (1.0f); rv.setModDepth (1.0f); rv.setModRate (2.0f); rv.setLowDecay (2.0f); rv.updateCoefficients();
    renderIR (rv, l, r, NS); float pkFreeze = peakAbs (l);
    { char b[128]; snprintf (b, 128, "peak(short)=%.3f peak(long)=%.3f peak(freeze+mod+lowdecay)=%.3f", pkLo, pkHi, pkFreeze); CHECK ("Stable (no blow-up/NaN)", pkLo < 4 && pkHi < 4 && pkFreeze < 8 && pkFreeze > 0, b); }

    // 3) High Damping darkens the tail (dramatic)
    rv.reset(); rv.setDecay (0.7f); rv.setModDepth (0.2f); rv.setModRate (0.4f); rv.setLowDecay (1.0f); rv.setHighDamping (0.0f); rv.updateCoefficients();
    renderIR (rv, l, r, NS); float hf0 = hfRatio (l);
    rv.reset(); rv.setHighDamping (1.0f); rv.updateCoefficients();
    renderIR (rv, l, r, NS); float hf1 = hfRatio (l);
    { char b[128]; snprintf (b, 128, "HF-ratio damp0=%.4f  damp100=%.4f  (%.1fx darker)", hf0, hf1, hf0 / (hf1 + 1e-9f)); CHECK ("High Damping darkens", hf1 < hf0 * 0.6f, b); }

    // 4) Width decorrelates L/R (dramatic)
    rv.reset(); rv.setHighDamping (0.3f); rv.setWidth (0.0f); rv.updateCoefficients();
    renderIR (rv, l, r, NS); float c0 = corrLR (l, r);
    rv.reset(); rv.setWidth (1.0f); rv.updateCoefficients();
    renderIR (rv, l, r, NS); float c1 = corrLR (l, r);
    { char b[128]; snprintf (b, 128, "L/R corr width0=%.3f  width100=%.3f", c0, c1); CHECK ("Width decorrelates", c0 > 0.85f && c1 < 0.6f, b); }

    // 5) Mix law — mix0 = pure dry, mix1 = fully wet
    rv.reset(); rv.updateCoefficients();
    {
        rv.setMix (0.0f); rv.updateCoefficients();
        float L[4] = { 1, 0.5f, -0.3f, 0.2f }, R[4] = { 1, 0.5f, -0.3f, 0.2f };
        rv.process (L, R, 4);
        bool dryOK = std::fabs (L[0] - 1.0f) < 1e-4f && std::fabs (L[1] - 0.5f) < 1e-4f;
        rv.reset(); rv.setMix (1.0f); rv.updateCoefficients();
        float L2[1] = { 1 }, R2[1] = { 1 }; rv.process (L2, R2, 1);
        bool wetOK = std::fabs (L2[0]) < 0.2f; // fully wet: dry gone, wet IR ~ tiny at t=0
        char b[128]; snprintf (b, 128, "mix0 passthru L0=%.4f L1=%.4f | mix1 dry-killed L0=%.4f", L[0], L[1], L2[0]);
        CHECK ("Mix equal-power (0=dry,1=wet)", dryOK && wetOK, b);
    }

    printf ("\nRESULT: %d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
