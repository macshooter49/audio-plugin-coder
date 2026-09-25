// organics_audit.cpp — THE ORGANICS LIBRARY AUDIT (final overpass, tp106). Every installed instrument × articulation,
// rendered through the real runtime (OrganicEngine + OrganicsLibrary, the plugin's own code), measured with the
// perceptual-harness metrics (phase-independent: K-weighted loudness, HP residual vs the LOCAL signal, RMS ripple).
// Built + run by Tests/organics_audit.sh.
//
//   organics_audit --lib   <root> [idFilter]   the whole-library sweep (chromatic run × vel 30/80/127, pedal chord with a
//                                              10 s release, loudness through the engine, velocity response)
//   organics_audit --loops <root> [idFilter]   every sustain loop held 8 s: pump (repeated swell) + seam click
//   organics_audit --knobs <root>              every knob 0 → 100 % on four instruments + live-turn zipper/click
//   organics_audit --calib <root> [idFilter]   the calibration point through the engine (Tools/organics/engine_calibrate.py)
//   organics_audit --pitchdump <root> <dir> <id> [vels]  renders for Tests/organics_pitch_check.py (Tuning = Equal)
//   organics_audit --note  <root> <id> <artic> <key> <vel> [hold] [release] [wav]   one note, its click candidates
//   organics_audit --null  <root> <file> [check]  FNV hash of a fixed set of renders (the Organics null test for
//                                              CPU-only engine changes: write before, check after → bit-identical)
//
// --lib bars (FAIL = exit 1): silence · non-finite · denormals · clicks (HP(8k) residual > 20 dB over its ±10 ms
// neighbourhood RMS, > 6 dB over every other HF peak within ±25 ms, > −45 dB re the local signal AND over −90 dBFS, outside
// the note's own first 30 ms) · DC (the note's mean over −50 dBFS and within 20 dB of its RMS) · centre-key loudness ±1 dB of −24 LUFS (less any peak limit the calibration reports) ·
// adjacent-velocity jump ≤ 6 dB · the per-engine reader cap · tp108: every key's velocity-127 peak ≤ −1 dBFS (the library
// trims the recording per key — Tools/organics/peaktrim.py — never a clipper, never a limiter).
//   organics_audit --peaks <root> [idFilter] [vels]  every key's v127 peak, every RR take (Tools/organics/peaktrim.py)
#include "../Source/organics/OrganicEngine.h"
#include "../Source/organics/OrganicsLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace tw;
using Buf = std::vector<float>;
static constexpr double kPi = 3.14159265358979323846;
static double gSR = 48000.0;
static double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-20)); }
static std::string fmt (const char* f, ...)
{
    char b[2048]; va_list ap; va_start (ap, f); std::vsnprintf (b, sizeof b, f, ap); va_end (ap); return b;
}
static const float kNoDet[16] = {};

//==================================================================================================
//  Analysis
//==================================================================================================
namespace an
{
    /** ITU-R BS.1770 K-weighting (same analog prototypes as Tools/organics/analyse.py), stereo loudness of [s, s+n). */
    static double lufs (const Buf& L, const Buf& R, int64_t s, int64_t n)
    {
        auto kw = [] (const Buf& x, int64_t s0, int64_t n0) {
            double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            double K = std::tan (kPi * f0 / gSR), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            double a0 = 1.0 + K / Q + K * K;
            const double b1[3] = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0 };
            const double a1[3] = { 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
            f0 = 38.13547087602444; Q = 0.5003270373238773; K = std::tan (kPi * f0 / gSR); a0 = 1.0 + K / Q + K * K;
            const double b2[3] = { 1.0, -2.0, 1.0 };
            const double a2[3] = { 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, z1 = 0, z2 = 0, acc = 0; int64_t c = 0;
            for (int64_t i = 0; i < s0 + n0 && i < (int64_t) x.size(); ++i)
            {
                const double in = x[(size_t) i];
                const double y = b1[0] * in + b1[1] * x1 + b1[2] * x2 - a1[1] * y1 - a1[2] * y2;
                x2 = x1; x1 = in; y2 = y1; y1 = y;
                const double z = b2[0] * y + b2[1] * u1 + b2[2] * u2 - a2[1] * z1 - a2[2] * z2;
                u2 = u1; u1 = y; z2 = z1; z1 = z;
                if (i >= s0) { acc += z * z; ++c; }
            }
            return c > 0 ? acc / (double) c : 0.0;
        };
        const double p = kw (L, s, n) + kw (R, s, n);
        return p > 1e-20 ? -0.691 + 10.0 * std::log10 (p) : -200.0;
    }
    /** First frame whose |mono| reaches peak − 24 dB (Tools/organics find_onset). */
    static int64_t onset (const Buf& m)
    {
        double pk = 0; for (float v : m) pk = std::max (pk, (double) std::abs (v));
        const double lim = pk * std::pow (10.0, -24.0 / 20.0);
        for (size_t i = 0; i < m.size(); ++i) if (std::abs (m[i]) >= lim) return (int64_t) i;
        return 0;
    }

    /** 4th-order Butterworth high-pass (two cascaded biquads). */
    static Buf highpass (const Buf& x, double fc, double fs = 0.0)
    {
        Buf y = x;
        if (fs <= 0.0) fs = gSR;
        for (double q : { 0.5411961, 1.3065630 })
        {
            const double w0 = 2 * kPi * fc / fs, al = std::sin (w0) / (2 * q), c = std::cos (w0);
            const double b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& v : y)
            {
                const double in = v, o = (b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
                x2 = x1; x1 = in; y2 = y1; y1 = o; v = (float) o;
            }
        }
        return y;
    }

    /** THE CLICK METRIC — HP(8 kHz) residual vs the LOCAL signal. A click is a sample whose HP residual is far above
        the HP residual of its own ±10 ms neighbourhood (excl. ±0.5 ms: its own width) — natural HF (bow hair, breath,
        hammer) is spread, a discontinuity is not — AND audible against the local full-band signal. Skip ranges hold
        the note's own attack (the recording's transient is not a click). Returns the worst candidate. */
    struct Click { double excessDb = -200, relDb = -200; int64_t at = -1; bool hit = false; };
    static Click clicks (const Buf& x, const std::vector<std::pair<int64_t, int64_t>>& skip, double excessBar = 20.0, double relBar = -45.0)
    {
        Click out;
        const int64_t N = (int64_t) x.size();
        if (N < 2000) return out;
        const auto h = highpass (x, 8000.0);
        std::vector<double> ph ((size_t) N + 1, 0.0), px ((size_t) N + 1, 0.0);
        for (int64_t i = 0; i < N; ++i) { ph[(size_t) i + 1] = ph[(size_t) i] + (double) h[(size_t) i] * h[(size_t) i]; px[(size_t) i + 1] = px[(size_t) i] + (double) x[(size_t) i] * x[(size_t) i]; }
        const int64_t W = (int64_t) (0.010 * gSR), C = (int64_t) (0.0005 * gSR);
        double bestScore = -1e9;
        for (int64_t i = 256; i < N; ++i)
        {
            bool sk = false; for (auto& r : skip) if (i >= r.first && i < r.second) { sk = true; break; }
            if (sk) continue;
            const double a = std::abs ((double) h[(size_t) i]);
            if (a < 1e-7) continue;
            const int64_t a0 = std::max<int64_t> (0, i - W), a1 = std::min (N, i + W), c0 = std::max<int64_t> (0, i - C), c1 = std::min (N, i + C);
            const double ringE = (ph[(size_t) a1] - ph[(size_t) a0]) - (ph[(size_t) c1] - ph[(size_t) c0]);
            const int64_t ringN = (a1 - a0) - (c1 - c0);
            const double rloc = std::sqrt (std::max (ringE, 0.0) / (double) std::max<int64_t> (1, ringN)) + 1e-12;
            const double sloc = std::sqrt ((px[(size_t) a1] - px[(size_t) a0]) / (double) std::max<int64_t> (1, a1 - a0)) + 1e-12;
            if (sloc < 1e-5) continue;                                         // below −100 dBFS: inaudible
            const double ex = db (a / rloc), rel = db (a / sloc);
            bool hit = ex > excessBar && rel > relBar && a > 3.16e-5;    // (an HP residual under −90 dBFS is below the 16-bit floor)
            if (hit)
            {
                // ISOLATION: a discontinuity stands alone; a spiky periodic waveform (brass ff, a reed, a harpsichord
                // pluck) repeats its HF peak every period — so the candidate must also clear the LARGEST HF peak of the
                // ±25 ms ring around it (40 Hz periods) by 6 dB
                const int64_t R = (int64_t) (0.025 * gSR);
                double ring = 1e-12;
                for (int64_t j = std::max<int64_t> (0, i - R); j < std::min (N, i + R); ++j)
                    if (j < i - C || j >= i + C) ring = std::max (ring, (double) std::abs (h[(size_t) j]));
                hit = db (a / ring) > 6.0;
            }
            const double score = (hit ? 1000.0 : 0.0) + std::min (ex, 60.0) + 0.5 * rel;
            if (score > bestScore) { bestScore = score; out.excessDb = ex; out.relDb = rel; out.at = i; out.hit = hit; }
        }
        return out;
    }
}

//==================================================================================================
//  Rendering
//==================================================================================================
static std::shared_ptr<const OrganicInstrument> load (const juce::String& id)
{
    std::shared_ptr<const OrganicInstrument> out; bool done = false;
    OrganicsLibrary::get().request (id, [&] (std::shared_ptr<const OrganicInstrument> p) { out = p; done = true; });
    for (int i = 0; i < 4000 && ! done; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return out;
}

struct Note { Buf L, R; int64_t offAt = 0; int maxLive = 0; int region = -1; };
/** One note on a fresh engine (= one synth voice): hold, note-off, then render until the engine is idle (≤ relMax s). */
static Note renderNote (const std::shared_ptr<const OrganicInstrument>& I, const OrganicParams& p, int key, int vel,
                        double holdS, double relMax, bool nonRT = false, int blk = 256, float pitch = 0.f)
{
    I->resetPerformanceState();
    OrganicEngine e; e.prepare (gSR, blk); e.setInstrument (I); e.setNonRealtime (nonRT);
    e.noteOn (key, (float) vel / 127.f, 1, kNoDet, 0x9e3779b9u);
    Note n; Buf l ((size_t) blk), r ((size_t) blk);
    const int64_t hold = (int64_t) (holdS * gSR), total = hold + (int64_t) (relMax * gSR);
    int64_t t = 0; bool off = false;
    while (t < total)
    {
        if (! off && t >= hold) { e.noteOff (false); off = true; n.offAt = t; }
        std::fill (l.begin(), l.end(), 0.f); std::fill (r.begin(), r.end(), 0.f);
        e.render (p, pitch, l.data(), r.data(), blk);
        if (t == 0) n.region = organics_debug::lastNoteRegion();
        n.maxLive = std::max (n.maxLive, organics_debug::lastLiveReaders());
        n.L.insert (n.L.end(), l.begin(), l.end()); n.R.insert (n.R.end(), r.begin(), r.end());
        t += blk;
        if (off && ! e.isActive()) break;
    }
    e.kill(); e.setInstrument (nullptr);
    return n;
}
/** A click the RECORDING already has is the recording's, not the engine's: the same isolation measure (HP(8k) peak within
    ±1 ms over the largest HF peak within ±25 ms) on the source sample at the frame the note was reading. A source event
    within 6 dB of its ring counts (resampling reshapes a transient's HF by a few dB). */
static double sourceIsolationDb (const OrganicInstrument& I, int ridx, int key, int64_t tOut);
/** The same for a click AFTER the note-off: the release-trigger regions of (artic, key, vel) start at the note-off —
    the recording is the release sample (a reed stopping, a damper, a bow lift). The largest isolation among them. */
static double releaseSourceIsolationDb (const OrganicInstrument& I, int artic, int key, int vel, int64_t tAfterOff)
{
    double best = -200.0;
    for (size_t ri = 0; ri < I.regions.size(); ++ri)
    {
        const auto& r = I.regions[ri];
        if (r.kind != org::Kind::Release || r.artic != artic || key < r.lk || key > r.hk || vel < r.lv || vel > r.hv) continue;
        best = std::max (best, sourceIsolationDb (I, (int) ri, key, tAfterOff));
    }
    return best;
}
/** …and over every attack region that can sound at (artic, key, vel) — a crossfaded layer pair or a stacked second
    sample carries the transient as often as the top region does. */
static double attackSourceIsolationDb (const OrganicInstrument& I, int artic, int key, int vel, int64_t tOut)
{
    double best = -200.0;
    const int mk = I.mappedKey (artic, key);
    for (size_t ri = 0; ri < I.regions.size(); ++ri)
    {
        const auto& r = I.regions[ri];
        if (r.kind != org::Kind::Attack || r.artic != artic || mk < r.lk || mk > r.hk || vel < r.lv - 12 || vel > r.hv + 12) continue;
        best = std::max (best, sourceIsolationDb (I, (int) ri, key, tOut));
    }
    return best;
}
static double sourceIsolationDb (const OrganicInstrument& I, int ridx, int key, int64_t tOut)
{
    if (ridx < 0 || ridx >= (int) I.regions.size()) return -200.0;
    const auto& r = I.regions[(size_t) ridx];
    const auto& S = I.samples[(size_t) r.smp];
    const double ratio = S.sampleRate / gSR * std::pow (2.0, (key - r.root) / 12.0 + ((double) r.cents + (double) r.tfix) / 1200.0);
    double pos = (double) r.start + (double) tOut * ratio;
    if (r.looping()) while (pos >= (double) r.le) pos -= (double) (r.le - r.ls);
    const int64_t c = (int64_t) pos, W = (int64_t) (0.06 * S.sampleRate);
    if (c < 0 || c >= S.frames) return -200.0;
    const int64_t a = std::max<int64_t> (0, c - W), b = std::min<int64_t> (S.frames, c + W);
    Buf x ((size_t) (b - a));
    const int16_t* d = S.data();
    for (int64_t i = a; i < b; ++i)
    {
        float v = 0.f; for (int k = 0; k < S.channels; ++k) v += (float) d[i * S.channels + k];
        x[(size_t) (i - a)] = v / (32768.f * (float) S.channels);
    }
    const auto h = an::highpass (x, 8000.0, S.sampleRate);
    const int64_t C = (int64_t) (0.001 * S.sampleRate), R = (int64_t) (0.025 * S.sampleRate), j = c - a;
    double near = 0, ring = 1e-12;
    for (int64_t i = std::max<int64_t> (0, j - R); i < std::min<int64_t> ((int64_t) h.size(), j + R); ++i)
    {
        const double v = std::abs ((double) h[(size_t) i]);
        if (i >= j - C && i < j + C) near = std::max (near, v); else ring = std::max (ring, v);
    }
    return db (near / ring);
}

static Buf mono (const Note& n) { Buf m (n.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (n.L[i] + n.R[i]); return m; }

struct Signal { double peak = 0, dc = 0, rms = 0; int nonFinite = 0, denorm = 0; };
static Signal signalStats (const Note& n)
{
    Signal s; double sl = 0, sr = 0, e = 0;
    for (const Buf* b : { &n.L, &n.R })
        for (float v : *b)
        {
            if (! std::isfinite (v)) { ++s.nonFinite; continue; }
            if (v != 0.f && std::abs (v) < FLT_MIN) ++s.denorm;
            s.peak = std::max (s.peak, (double) std::abs (v)); e += (double) v * v;
        }
    for (float v : n.L) sl += v;
    for (float v : n.R) sr += v;
    const double N = (double) std::max<size_t> (1, n.L.size());
    s.dc = std::max (std::abs (sl), std::abs (sr)) / N;
    s.rms = std::sqrt (e / (2.0 * N));
    return s;
}

static juce::var readReport (const juce::File& root, const juce::String& id)
{
    auto f = juce::File (juce::File (root.getChildFile (id)).getLinkedTarget()).getChildFile ("build-report.json");
    if (! f.existsAsFile())
        f = juce::File ("~/Developer/VST-Plugins/organics-library/compiled").getChildFile (id).getChildFile ("build-report.json");
    return f.existsAsFile() ? juce::JSON::parse (f) : juce::var();
}
static juce::var readRecipe (const juce::String& id)
{
    const auto f = juce::File (TERRAIN_TOOLS_ORGANICS).getChildFile ("recipes").getChildFile (id + ".json");
    return f.existsAsFile() ? juce::JSON::parse (f) : juce::var();
}

static void setEnv (const char* k, const char* v) { setenv (k, v, 1); }

//==================================================================================================
//  --lib : the whole-library sweep
//==================================================================================================
static int gFails = 0;
static std::string gFailList;

static int runLib (const juce::File& root, const juce::String& filter, FILE* tsv)
{
    const auto idx = OrganicsLibrary::get().index();
    std::printf ("══ ORGANICS LIBRARY AUDIT — %s ══\n", root.getFullPathName().toRawUTF8());
    if (! idx.isArray() || idx.size() == 0) { std::printf ("FAIL  no instruments\n"); return 1; }
    if (tsv) std::fprintf (tsv, "id\tartic\tkey\tvel\tpeakDb\tdcDb\tclickExcessDb\tclickRelDb\tclickAtMs\tclickPhase\tnonFinite\tdenorm\tmaxLive\n");
    int insts = 0, instFail = 0, flagsHot = 0;
    double worstLoud = 0, worstJump = 0;
    for (auto& ent : *idx.getArray())
    {
        const juce::String id = ent["id"].toString();
        if (filter.isNotEmpty() && ! id.contains (filter)) continue;
        auto I = load (id);
        ++insts;
        if (! I) { std::printf ("FAIL  %-36s does not load\n", id.toRawUTF8()); ++instFail; ++gFails; continue; }
        const auto rep = readReport (root, id);
        const double limited = (double) rep["loudness"].getProperty ("peakLimitedDb", 0.0);
        std::vector<std::string> fails, flags;
        std::string info;
        const auto t0 = std::chrono::steady_clock::now();

        for (int a = 0; a < I->numArtics; ++a)
        {
            int lo = 128, hi = -1;
            for (auto& r : I->regions) if (r.artic == a && r.kind == org::Kind::Attack) { lo = std::min (lo, r.lk); hi = std::max (hi, r.hk); }
            if (hi < 0) continue;
            const std::string an_ = I->artics[a].toStdString();
            OrganicParams p; p.human = 0.f; p.artic = a;       // every other knob at its APVTS default (Tuning = Equal)

            // ── A. chromatic run × vel 30 / 80 / 127 (Noise 0: the mechanical-noise regions ARE authored transients — a
            //    key clack is not a click; their own start/end go through the same fades, measured by the chord pass) ──
            OrganicParams pa = p; pa.noise = 0.f;
            int silent = 0, nonFin = 0, denorm = 0, clickN = 0, dcN = 0, hotN = 0, capN = 0, srcN = 0;
            double worstClickEx = -200, worstClickRel = -200, worstDc = -200, peak127 = -200; std::string clickWhere, dcWhere, hotWhere;
            for (int key = lo; key <= hi; ++key)
                for (int vel : { 30, 80, 127 })
                {
                    auto n = renderNote (I, pa, key, vel, 0.6, 4.0);
                    const auto s = signalStats (n);
                    const auto m = mono (n);
                    const int64_t on = an::onset (m);
                    // skip: the note's own attack (first 30 ms after its onset) — the recording's transient
                    const auto c = an::clicks (m, { { 0, on + (int64_t) (0.030 * gSR) } });
                    const char* phase = c.at < 0 ? "-" : (c.at < n.offAt ? "hold" : (c.at < n.offAt + (int64_t) (0.05 * gSR) ? "noteoff" : "release"));
                    if (s.peak < 0.001) ++silent;
                    nonFin += s.nonFinite; denorm += s.denorm;
                    bool inSource = false;
                    // the recording's own: the sounding attack region at that frame (a looped one keeps playing after
                    // the note-off, under the release curve), or a release-trigger sample started at the note-off
                    if (c.hit && (attackSourceIsolationDb (*I, a, key, vel, c.at) > -6.0
                                  || (c.at >= n.offAt && releaseSourceIsolationDb (*I, a, key, vel, c.at - n.offAt) > -6.0))) { inSource = true; ++srcN; }
                    if (c.hit && ! inSource) { ++clickN; if (c.excessDb > worstClickEx) { worstClickEx = c.excessDb; worstClickRel = c.relDb; clickWhere = fmt ("%s k%d v%d %s %.0fms", an_.c_str(), key, vel, phase, 1000.0 * (double) c.at / gSR); } }
                    const double dcDb = db (s.dc);
                    if (dcDb > worstDc) { worstDc = dcDb; dcWhere = fmt ("k%d v%d", key, vel); }
                    if (dcDb > -50.0 && dcDb > db (s.rms) - 20.0) ++dcN;   // (a subsonic bow / breath bump averages to −55…−60 dBFS: not DC)
                    if (vel == 127) { peak127 = std::max (peak127, db (s.peak)); if (db (s.peak) > -1.0) { ++hotN; if (hotWhere.empty()) hotWhere = fmt ("%s k%d %.1f dBFS", an_.c_str(), key, db (s.peak)); } }
                    if (n.maxLive > organics::kMaxRegionsPerOsc) ++capN;
                    if (tsv) std::fprintf (tsv, "%s\t%s\t%d\t%d\t%.2f\t%.1f\t%.1f\t%.1f\t%.1f\t%s\t%d\t%d\t%d\n", id.toRawUTF8(), an_.c_str(), key, vel,
                                           db (s.peak), dcDb, c.excessDb, c.relDb, 1000.0 * (double) c.at / gSR, phase, s.nonFinite, s.denorm, n.maxLive);
                }
            if (silent)  fails.push_back (fmt ("%s: %d silent notes", an_.c_str(), silent));
            if (nonFin)  fails.push_back (fmt ("%s: %d non-finite samples", an_.c_str(), nonFin));
            if (denorm)  fails.push_back (fmt ("%s: %d denormal samples", an_.c_str(), denorm));
            if (clickN)  fails.push_back (fmt ("%s: %d notes click (worst excess %.1f dB, %.1f dB re local, %s)", an_.c_str(), clickN, worstClickEx, worstClickRel, clickWhere.c_str()));
            if (dcN)     fails.push_back (fmt ("%s: DC on %d notes (worst %.1f dBFS %s)", an_.c_str(), dcN, worstDc, dcWhere.c_str()));
            if (capN)    fails.push_back (fmt ("%s: reader cap exceeded on %d notes", an_.c_str(), capN));
            if (srcN)    flags.push_back (fmt ("%s: %d notes carry an isolated HF transient that is IN THE RECORDING (not the engine)", an_.c_str(), srcN));
            // tp108: a BAR, no longer a flag — the library trims every key (Tools/organics/peaktrim.py), never a limiter
            if (hotN)    fails.push_back (fmt ("%s: %d keys over -1 dBFS at vel 127 (first %s)", an_.c_str(), hotN, hotWhere.c_str()));
            info += fmt (" [%s k%d-%d pk127 %.1f dc %.0f]", an_.c_str(), lo, hi, peak127, worstDc);

            // ── C. loudness (artic 0: the compiler's calibration point) + velocity response ──
            {
                const int centre = (lo <= 60 && 60 <= hi) ? 60 : (lo + hi + 1) / 2;
                if (a == 0)
                {
                    OrganicParams q = p; q.noise = 0.f; q.release = 0.f;          // the compiler measures without noise
                    auto n = renderNote (I, q, centre, 100, 2.0, 0.05);
                    const int64_t on = an::onset (mono (n));
                    const double l = an::lufs (n.L, n.R, on, (int64_t) gSR);
                    const double want = -24.0 - limited;
                    info += fmt (" LUFS %.2f", l);
                    worstLoud = std::max (worstLoud, std::abs (l - want));
                    if (std::abs (l - want) > 1.0) fails.push_back (fmt ("loudness %.2f LUFS at k%d v100 (want %.1f)", l, centre, want));
                }
                double jumpW = 0; int jumpV = 0, jumpK = 0; int dips = 0; double dipW = 0;
                for (int key : { std::max (lo, centre - 12), centre, std::min (hi, centre + 12) })
                {
                    double prev = -999;
                    std::vector<double> lv;
                    for (int vel = 1; vel <= 127; ++vel)
                    {
                        OrganicParams q = p; q.noise = 0.f;
                        auto n = renderNote (I, q, key, vel, 0.45, 0.0);
                        const int64_t on = an::onset (mono (n));
                        lv.push_back (an::lufs (n.L, n.R, on, (int64_t) (0.4 * gSR)));
                    }
                    for (int vel = 2; vel <= 127; ++vel)
                    {
                        const double d = lv[(size_t) vel - 1] - lv[(size_t) vel - 2];
                        if (std::abs (d) > std::abs (jumpW)) { jumpW = d; jumpV = vel; jumpK = key; }
                        if (d < -1.0) { ++dips; dipW = std::min (dipW, d); }
                    }
                    (void) prev;
                }
                worstJump = std::max (worstJump, std::abs (jumpW));
                info += fmt (" veljump %+.1f dB (k%d v%d)", jumpW, jumpK, jumpV);
                if (std::abs (jumpW) > 6.0) fails.push_back (fmt ("%s: adjacent-velocity jump %+.1f dB at k%d v%d", an_.c_str(), jumpW, jumpK, jumpV));
                if (dips) flags.push_back (fmt ("%s: %d velocity steps quieter by > 1 dB (worst %.1f)", an_.c_str(), dips, dipW));
            }

            // ── D. sustained chord with the pedal, long release (Release 1 = ≥ 10 s): 4 voices ──
            {
                const int centre = (lo <= 60 && 60 <= hi) ? 60 : (lo + hi + 1) / 2;
                const int keys[4] = { centre - 12, centre - 5, centre, centre + 4 };
                OrganicParams q = p; q.release = 1.f; q.noise = 0.f;
                std::vector<std::unique_ptr<OrganicEngine>> v;
                I->resetPerformanceState();
                for (int k = 0; k < 4; ++k) { v.emplace_back (new OrganicEngine()); v.back()->prepare (gSR, 256); v.back()->setInstrument (I); v.back()->pedal (true); }
                Buf L, R, l (256), r (256); int maxLive = 0;
                const int64_t total = (int64_t) (16.0 * gSR);
                int64_t upAt = (int64_t) (3.0 * gSR);
                for (int64_t t = 0; t < total; t += 256)
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        if (t == (int64_t) k * 256 * 19) v[(size_t) k]->noteOn (std::clamp (keys[k], lo, hi), 0.7f, 1, kNoDet, 77u + (uint32_t) k);
                        if (t == (int64_t) (1.0 * gSR) / 256 * 256) v[(size_t) k]->noteOff (true);
                        if (t == upAt / 256 * 256) v[(size_t) k]->pedal (false);
                    }
                    std::fill (l.begin(), l.end(), 0.f); std::fill (r.begin(), r.end(), 0.f);
                    bool any = false;
                    for (auto& e : v) { e->render (q, 0.f, l.data(), r.data(), 256); maxLive = std::max (maxLive, organics_debug::lastLiveReaders()); any |= e->isActive(); }
                    L.insert (L.end(), l.begin(), l.end()); R.insert (R.end(), r.begin(), r.end());
                    if (! any && t > upAt) break;
                }
                Note n; n.L = L; n.R = R;
                const auto s = signalStats (n);
                const auto m = mono (n);
                std::vector<std::pair<int64_t, int64_t>> sk;
                for (int k = 0; k < 4; ++k) { const int64_t t0 = (int64_t) k * 256 * 19; sk.push_back ({ t0, t0 + (int64_t) (0.08 * gSR) }); }   // each note's own attack (a brass tongue, a pizz)
                const auto c = an::clicks (m, sk);
                info += fmt (" chord pk %.1f live %d tail %.1fs", db (s.peak), maxLive, (double) L.size() / gSR - 3.0);
                bool chordSrc = false;   // the recording's own (any of the four notes' attack regions at that frame)?
                for (int k = 0; k < 4 && c.hit; ++k)
                {
                    const int64_t t0 = (int64_t) k * 256 * 19;
                    if (c.at > t0 && attackSourceIsolationDb (*I, a, std::clamp (keys[k], lo, hi), 89, c.at - t0) > -6.0) chordSrc = true;
                }
                if (c.hit && chordSrc) flags.push_back (fmt ("%s: the pedal chord carries an isolated HF transient IN THE RECORDING at %.2f s", an_.c_str(), (double) c.at / gSR));
                else if (c.hit) fails.push_back (fmt ("%s: pedal chord clicks (excess %.1f dB, %.1f dB re local, at %.2f s)", an_.c_str(), c.excessDb, c.relDb, (double) c.at / gSR));
                if (s.nonFinite || s.denorm) fails.push_back (fmt ("%s: pedal chord non-finite %d denormal %d", an_.c_str(), s.nonFinite, s.denorm));
                if (maxLive > organics::kMaxRegionsPerOsc) fails.push_back (fmt ("%s: pedal chord %d live readers > cap", an_.c_str(), maxLive));
                for (auto& e : v) { e->kill(); e->setInstrument (nullptr); }
            }
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        if (! flags.empty()) ++flagsHot;
        if (fails.empty()) std::printf ("PASS  %-36s%s  (%.0f s)\n", id.toRawUTF8(), info.c_str(), secs);
        else
        {
            ++instFail; ++gFails; gFailList += " " + id.toStdString();
            std::printf ("FAIL  %-36s%s  (%.0f s)\n", id.toRawUTF8(), info.c_str(), secs);
            for (auto& f : fails) std::printf ("        ✗ %s\n", f.c_str());
        }
        for (auto& f : flags) std::printf ("        FLAG %s\n", f.c_str());
        std::fflush (stdout);
        I.reset(); org::drainDeferredReleases();
    }
    std::printf ("══ %s — %d/%d instruments clean · %d flagged hot/dips · worst loudness miss %.2f dB · worst adjacent-vel jump %.1f dB ══\n",
                 instFail == 0 ? "PASS" : "FAIL", insts - instFail, insts, flagsHot, worstLoud, worstJump);
    if (instFail) std::printf ("failing:%s\n", gFailList.c_str());
    return instFail == 0 ? 0 : 1;
}

//==================================================================================================
//  --loops : every authored sustain loop, held 8 s at its root, velocity in the middle of its layer.
//  SEAM  = the click metric inside each seam's crossfade window (± 5 ms), vs the same metric everywhere else in the
//          looped part (the recording's own bow / breath grain) — a seam may not stand out.
//  PUMP  = the slow level ripple the loop repeats: 200 ms RMS (averages vibrato / tremolo / bellows AM out), detrended,
//          max − min over the looped part. A bow change or swell inside the loop comes back every loop length.
//==================================================================================================
struct LoopRes { double pump = 0, seamEx = -200, seamRel = -200, restEx = -200; int seams = 0; bool click = false; };
static LoopRes measureLoop (const std::shared_ptr<const OrganicInstrument>& I, const org::Region& r, int vel, double holdS = 8.0)
{
    LoopRes o;
    OrganicParams p; p.human = 0.f; p.artic = r.artic; p.noise = 0.f;
    auto nt = renderNote (I, p, r.root, vel, holdS, 0.0);
    const auto m = mono (nt);
    const auto& S = I->samples[(size_t) r.smp];
    const double ratio = S.sampleRate / gSR * std::pow (2.0, ((double) r.cents + (double) r.tfix) / 1200.0);
    const int64_t loopIn = (int64_t) ((double) (r.ls - r.start) / ratio);
    const int64_t s0 = std::max<int64_t> (loopIn, (int64_t) (0.4 * gSR));
    const int64_t N = (int64_t) m.size();
    if (s0 + (int64_t) gSR > N) return o;
    // seams (output frames): the k-th wrap at ((le − start) + k·L) / ratio; the crossfade occupies xf / ratio before it
    std::vector<std::pair<int64_t, int64_t>> seams;
    const double L = (double) (r.le - r.ls), xfO = (double) std::min (r.xf, r.ls) / ratio;
    for (int k = 0; ; ++k)
    {
        const double T = ((double) (r.le - r.start) + k * L) / ratio;
        if (T > (double) N) break;
        seams.push_back ({ (int64_t) (T - xfO) - 240, (int64_t) T + 240 });
    }
    o.seams = (int) seams.size();
    // per-sample click metric
    const auto h = an::highpass (m, 8000.0);
    std::vector<double> ph ((size_t) N + 1, 0.0), px ((size_t) N + 1, 0.0);
    for (int64_t i = 0; i < N; ++i) { ph[(size_t) i + 1] = ph[(size_t) i] + (double) h[(size_t) i] * h[(size_t) i]; px[(size_t) i + 1] = px[(size_t) i] + (double) m[(size_t) i] * m[(size_t) i]; }
    const int64_t W = 480, C = 24;
    for (int64_t i = std::max<int64_t> (s0, 256); i < N - 1; ++i)
    {
        const double a = std::abs ((double) h[(size_t) i]); if (a < 1e-7) continue;
        const int64_t a0 = i - W, a1 = std::min (N, i + W), c0 = i - C, c1 = std::min (N, i + C);
        const double rl = std::sqrt (std::max (0.0, (ph[(size_t) a1] - ph[(size_t) a0]) - (ph[(size_t) c1] - ph[(size_t) c0])) / (double) ((a1 - a0) - (c1 - c0))) + 1e-12;
        const double sl = std::sqrt ((px[(size_t) a1] - px[(size_t) a0]) / (double) (a1 - a0)) + 1e-12;
        if (sl < 1e-5) continue;
        const double ex = db (a / rl), rel = db (a / sl);
        bool inSeam = false; for (auto& sw : seams) if (i >= sw.first && i < sw.second) { inSeam = true; break; }
        if (inSeam) { if (ex > o.seamEx) { o.seamEx = ex; o.seamRel = rel; } }
        else o.restEx = std::max (o.restEx, ex);
    }
    o.click = o.seamEx > 20.0 && o.seamRel > -45.0 && o.seamEx > o.restEx + 3.0;
    // pump: 200 ms RMS every 25 ms, detrended
    std::vector<double> e; const int64_t w = (int64_t) (0.2 * gSR), hop = (int64_t) (0.025 * gSR);
    for (int64_t i = s0; i + w <= N; i += hop) e.push_back (10 * std::log10 ((px[(size_t) (i + w)] - px[(size_t) i]) / (double) w + 1e-20));
    const size_t K = e.size();
    if (K > 4)
    {
        double mx = 0, my = 0, sxy = 0, sxx = 0;
        for (size_t i = 0; i < K; ++i) { mx += (double) i; my += e[i]; }
        mx /= (double) K; my /= (double) K;
        for (size_t i = 0; i < K; ++i) { sxy += ((double) i - mx) * (e[i] - my); sxx += ((double) i - mx) * ((double) i - mx); }
        const double sl = sxx > 0 ? sxy / sxx : 0;
        double lo = 1e9, hi = -1e9; for (size_t i = 0; i < K; ++i) { const double v = e[i] - sl * ((double) i - mx); lo = std::min (lo, v); hi = std::max (hi, v); }
        o.pump = hi - lo;
    }
    return o;
}

static int runLoops (const juce::File& root, const juce::String& filter)
{
    const auto idx = OrganicsLibrary::get().index();
    std::printf ("══ ORGANICS LOOP AUDIT — 8 s holds at the root · PUMP = 200 ms RMS ripple over the looped part (bar 4 dB, Tremolo/Vibrato artics 6 dB) ·"
                 " SEAM = click metric inside the crossfade vs the rest of the loop ══\n");
    const bool verbose = std::getenv ("ORG_LOOP_VERBOSE") != nullptr;
    int bad = 0, total = 0;
    for (auto& ent : *idx.getArray())
    {
        const juce::String id = ent["id"].toString();
        if (filter.isNotEmpty() && ! id.contains (filter)) continue;
        auto I = load (id);
        if (! I) continue;
        double wPump = 0, wEx = -200, wRest = -200; std::string wWhere, cWhere; int n = 0, nb = 0, nClick = 0, nPump = 0;
        // an organ's beating ranks / a rotary speaker / bellows / stacked unison players: the recipe says why the level
        // moves inside the loop ("loopMotionWhy") — reported, not failed
        const bool naturalMotion = readRecipe (id).getProperty ("loopMotionWhy", juce::var()).toString().isNotEmpty();
        for (size_t ri = 0; ri < I->regions.size(); ++ri)
        {
            const auto& r = I->regions[ri];
            if (r.kind != org::Kind::Attack || ! r.looping()) continue;
            if (r.rrLen > 1 && r.rrPos != 0) continue;                          // one take per zone is enough
            if (r.randLo > 0.f) continue;
            const int vel = std::clamp ((std::max (r.lv, (int) r.fiHi) + std::min (r.hv, (int) r.foLo)) / 2, 1, 127);
            const auto o = measureLoop (I, r, vel);
            if (o.seams == 0) continue;
            const auto& S = I->samples[(size_t) r.smp];
            ++n; ++total;
            // a Tremolo / Vibrato articulation IS amplitude motion (bow tremolo, breath vibrato): 6 dB there, 4 dB elsewhere
            const bool amArtic = I->artics[r.artic].containsIgnoreCase ("trem") || I->artics[r.artic].containsIgnoreCase ("vib");
            const bool pumpBad = o.pump > (amArtic ? 6.0 : 4.0) && ! naturalMotion;
            if (pumpBad) ++nPump;
            if (o.click) ++nClick;
            if (pumpBad || o.click) { ++nb; ++bad; }
            if (verbose) std::printf ("   r%-4d %-12s k%-3d v%-3d loop %.2f s xf %.0f ms · pump %5.2f dB · seam %5.1f dB (%.1f re local) rest %5.1f%s%s\n", (int) ri,
                                      I->artics[r.artic].toRawUTF8(), r.root, vel, (double) (r.le - r.ls) / S.sampleRate, 1000.0 * (double) r.xf / S.sampleRate,
                                      o.pump, o.seamEx, o.seamRel, o.restEx, pumpBad ? " PUMP" : "", o.click ? " CLICK" : "");
            if (o.pump > wPump) { wPump = o.pump; wWhere = fmt ("%s k%d v%d (loop %.2f s)", I->artics[r.artic].toRawUTF8(), r.root, vel, (double) (r.le - r.ls) / S.sampleRate); }
            if (wEx < -100 || o.seamEx - o.restEx > wEx - wRest) { wEx = o.seamEx; wRest = o.restEx; cWhere = fmt ("%s k%d v%d", I->artics[r.artic].toRawUTF8(), r.root, vel); }
        }
        if (n == 0) { I.reset(); org::drainDeferredReleases(); continue; }
        std::printf ("%s  %-36s %3d loops · %2d pump · %2d seam clicks · worst pump %.2f dB%s (%s) · worst seam %.1f dB vs rest %.1f (%s)\n",
                     nb ? "FAIL" : "PASS", id.toRawUTF8(), n, nPump, nClick, wPump, naturalMotion ? " [the instrument's own motion]" : "",
                     wWhere.c_str(), wEx, wRest, cWhere.c_str());
        std::fflush (stdout);
        I.reset(); org::drainDeferredReleases();
    }
    std::printf ("══ %s — %d of %d loops pump over the bar or click at the seam ══\n", bad ? "FAIL" : "PASS", bad, total);
    return bad ? 1 : 0;
}

//==================================================================================================
//  --null : a fixed render set for the Organics null (CPU-only engine changes must be bit-identical)
//==================================================================================================
static int runHash (const juce::File&, const juce::File& file, bool check)
{
    const char* ids[] = { "salamander.grand.v3", "vsco2.strings.violin-section", "vcsl.mallets.glockenspiel", "vsco2.woodwinds.flute",
                          "karoryfer.sax.bear", "terrain.ep.rhodes", "vcsl.mallets.vibraphone", "freepats.organ.drawbar" };
    juce::StringArray lines;
    for (auto* id : ids)
    {
        auto I = load (id);
        if (! I) { lines.add (juce::String (id) + " missing"); continue; }
        for (int variant = 0; variant < 6; ++variant)
        {
            OrganicParams p; p.human = variant == 1 ? 0.6f : 0.f;
            if (variant == 2) { p.release = 1.f; p.sustain = 0.6f; }
            if (variant == 3) { p.vibrato = 0.7f; p.tone = 0.5f; p.body = -0.4f; }
            if (variant == 4) { p.dyn = 0.6f; p.image = 0.3f; p.noise = 1.f; p.velo = 0.2f; }
            if (std::getenv ("ORG_NULL_NOISE0")) p.noise = 0.f;   // tp107: the round-robin noise changed its law — null everything else
            const bool sinc = variant == 5;
            uint64_t h = 1469598103934665603ull;
            std::vector<std::unique_ptr<OrganicEngine>> v;
            I->resetPerformanceState();
            const int keys[3] = { 48, 60, 67 };
            for (int k = 0; k < 3; ++k) { v.emplace_back (new OrganicEngine()); v.back()->prepare (gSR, 256); v.back()->setInstrument (I); v.back()->setNonRealtime (sinc); }
            const float det[4] = { -7.f, 5.f, 11.f, -2.f };
            for (int k = 0; k < 3; ++k) v[(size_t) k]->noteOn (keys[k], 0.4f + 0.2f * (float) k, variant == 1 ? 4 : 1, variant == 1 ? det : kNoDet, 99u + (uint32_t) k);
            Buf l (256), r (256);
            for (int64_t t = 0; t < (int64_t) (5.0 * gSR); t += 256)
            {
                if (t == (int64_t) (2.0 * gSR) / 256 * 256) for (auto& e : v) e->noteOff (false);
                std::fill (l.begin(), l.end(), 0.f); std::fill (r.begin(), r.end(), 0.f);
                for (auto& e : v) e->render (p, variant == 3 ? 13.f : 0.f, l.data(), r.data(), 256);
                for (int i = 0; i < 256; ++i) { uint32_t a, b; std::memcpy (&a, &l[(size_t) i], 4); std::memcpy (&b, &r[(size_t) i], 4); h = (h ^ a) * 1099511628211ull; h = (h ^ b) * 1099511628211ull; }
            }
            lines.add (fmt ("%s v%d %016llx", id, variant, (unsigned long long) h));
        }
        I.reset(); org::drainDeferredReleases();
    }
    if (! check) { file.replaceWithText (lines.joinIntoString ("\n")); std::printf ("wrote %d hashes to %s\n", lines.size(), file.getFullPathName().toRawUTF8()); return 0; }
    juce::StringArray want; want.addLines (file.loadFileAsString());
    int diff = 0;
    for (int i = 0; i < lines.size(); ++i) if (i >= want.size() || want[i] != lines[i]) { ++diff; std::printf ("DIFF  %s   (was %s)\n", lines[i].toRawUTF8(), i < want.size() ? want[i].toRawUTF8() : "-"); }
    std::printf ("%s — organics render null: %d/%d renders bit-identical\n", diff ? "FAIL" : "PASS", lines.size() - diff, lines.size());
    return diff ? 1 : 0;
}

//==================================================================================================
//  --note : one note, every click candidate listed with its absolute level (diagnostics) + a WAV of it
//==================================================================================================
static int runNote (const juce::String& id, int artic, int key, int vel, double hold, double release, const juce::File& wav)
{
    auto I = load (id);
    if (! I) { std::printf ("missing %s\n", id.toRawUTF8()); return 1; }
    OrganicParams p; p.human = 0.f; p.artic = artic; p.release = (float) release;
    if (const char* nz = std::getenv ("ORG_NOISE")) p.noise = (float) std::atof (nz);
    auto n = renderNote (I, p, key, vel, hold, 16.0);
    const auto m = mono (n);
    const int64_t on = an::onset (m);
    std::printf ("%s a%d k%d v%d: %zu frames, note-off at %.3f s, onset %.3f s, peak %.1f dBFS\n", id.toRawUTF8(), artic, key, vel, m.size(),
                 (double) n.offAt / gSR, (double) on / gSR, db (signalStats (n).peak));
    std::vector<std::pair<int64_t, int64_t>> skip { { 0, on + (int64_t) (0.03 * gSR) } };
    for (int k = 0; k < 8; ++k)
    {
        const auto c = an::clicks (m, skip);
        if (c.at < 0 || ! c.hit) break;
        double loc = 0; for (int64_t i = std::max<int64_t> (0, c.at - 480); i < std::min<int64_t> ((int64_t) m.size(), c.at + 480); ++i) loc = std::max (loc, (double) std::abs (m[(size_t) i]));
        std::printf ("  click at %.4f s (%.1f ms after note-off): excess %.1f dB, %.1f dB re local, local peak %.1f dBFS\n", (double) c.at / gSR,
                     1000.0 * (double) (c.at - n.offAt) / gSR, c.excessDb, c.relDb, db (loc));
        skip.push_back ({ c.at - 480, c.at + 480 });
    }
    if (wav != juce::File())
    {
        wav.deleteFile();
        juce::WavAudioFormat fmtW;
        std::unique_ptr<juce::OutputStream> os (wav.createOutputStream().release());
        const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (gSR).withNumChannels (2).withBitsPerSample (32)
                                                           .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        if (os != nullptr)
            if (auto w = fmtW.createWriterFor (os, opts)) { const float* ch[2] = { n.L.data(), n.R.data() }; w->writeFromFloatArrays (ch, 2, (int) n.L.size()); }
    }
    return 0;
}

//==================================================================================================
//  --calib : the compiler's calibration point measured THROUGH THE ENGINE (Tools/organics/engine_calibrate.py reads it):
//  artic 0, the centre key (middle C when playable), velocity 100, every knob at its default, Noise 0 — K-weighted
//  loudness of the first second from the onset; and the velocity-127 peak of the same key.
//==================================================================================================
static int runCalib (const juce::String& filter)
{
    const auto idx = OrganicsLibrary::get().index();
    for (auto& ent : *idx.getArray())
    {
        const juce::String id = ent["id"].toString();
        if (filter.isNotEmpty() && ! id.contains (filter)) continue;
        auto I = load (id);
        if (! I) { std::printf ("CALIB %s missing\n", id.toRawUTF8()); continue; }
        int lo = 128, hi = -1;
        for (auto& r : I->regions) if (r.artic == 0 && r.kind == org::Kind::Attack) { lo = std::min (lo, r.lk); hi = std::max (hi, r.hk); }
        const int centre = (lo <= 60 && 60 <= hi) ? 60 : (lo + hi + 1) / 2;
        OrganicParams q; q.human = 0.f; q.noise = 0.f; q.release = 0.f;
        auto n = renderNote (I, q, centre, 100, 2.0, 0.05);
        const int64_t on = an::onset (mono (n));
        const double l = an::lufs (n.L, n.R, on, (int64_t) gSR);
        auto n127 = renderNote (I, q, centre, 127, 2.0, 0.05);
        std::printf ("CALIB %s %d %.3f %.3f\n", id.toRawUTF8(), centre, l, db (signalStats (n127).peak));
        std::fflush (stdout);
        I.reset(); org::drainDeferredReleases();
    }
    return 0;
}

//==================================================================================================
//  --pitchdump <root> <outDir> <id> [vels] : every key of every articulation at the velocities (default 80), Tuning =
//  Equal, 1.5 s held, as raw mono float32 (<outDir>/<artic>_<key>_<vel>.f32) — Tests/organics_pitch_check.py measures them with the COMPILER'S
//  OWN pitch detector (Tools/organics/analyse.measure_f0, the one that wrote tfix): the runtime has to land on 12-TET.
//==================================================================================================
static int runPitchDump (const juce::File& out, const juce::String& id, const juce::String& velList)
{
    juce::StringArray vs; vs.addTokens (velList.isEmpty() ? juce::String ("80") : velList, ",", "");
    auto I = load (id);
    if (! I) { std::printf ("missing %s\n", id.toRawUTF8()); return 1; }
    out.createDirectory();
    int n = 0;
    for (int a = 0; a < I->numArtics; ++a)
    {
        int lo = 128, hi = -1;
        for (auto& r : I->regions) if (r.artic == a && r.kind == org::Kind::Attack) { lo = std::min (lo, r.lk); hi = std::max (hi, r.hk); }
        for (int key = lo; key <= hi; ++key)
            for (auto& vstr : vs)
            {
                const int vel = juce::jlimit (1, 127, vstr.getIntValue());
                OrganicParams p; p.human = 0.f; p.artic = a; p.noise = 0.f; p.tuning = 1;
                auto nt = renderNote (I, p, key, vel, 1.5, 0.0);
                const auto m = mono (nt);
                // tp108: + the region the note played (its top attack region) — Tools/organics/retune.py closes each
                // region's tuning on the notes that actually played it
                juce::FileOutputStream os (out.getChildFile (juce::String (a) + "_" + juce::String (key) + "_" + juce::String (vel)
                                                             + "_" + juce::String (nt.region) + ".f32"));
                if (os.openedOk()) { os.setPosition (0); os.truncate(); os.write (m.data(), m.size() * sizeof (float)); ++n; }
            }
    }
    std::printf ("dumped %d notes of %s\n", n, id.toRawUTF8());
    I.reset(); org::drainDeferredReleases();
    return 0;
}

//==================================================================================================
//  --peaks <root> [idFilter] [vels] : tp108 — the velocity-127 peak of EVERY key of every articulation through the engine
//  (Human 0, 1 player, every knob at its default, Noise 0 unless ORG_PEAK_NOISE is set), the loudest of every round-robin
//  take / random slot the key can play (one press per candidate region at v127, ×2 for random slots, ≤ 12, seeds varied,
//  the performance state carried between presses like a player's). One line per key:
//      PEAK <id> <artic> <key> <vel> <peakDb> <presses>
//  Tools/organics/peaktrim.py reads it (the per-key trim) and Tests/organics_compile_test.py holds the bar (≤ −1 dBFS).
//==================================================================================================
static int runPeaks (const juce::String& filter, const juce::String& velList)
{
    juce::StringArray vs; vs.addTokens (velList.isEmpty() ? juce::String ("127") : velList, ",", "");
    const auto idx = OrganicsLibrary::get().index();
    const char* nzEnv = std::getenv ("ORG_PEAK_NOISE");
    int worstBad = 0;
    for (auto& ent : *idx.getArray())
    {
        const juce::String id = ent["id"].toString();
        if (filter.isNotEmpty() && ! id.contains (filter)) continue;
        auto I = load (id);
        if (! I) { std::printf ("PEAKMISSING %s\n", id.toRawUTF8()); continue; }
        I->resetPerformanceState();
        for (int a = 0; a < I->numArtics; ++a)
        {
            int lo = 128, hi = -1;
            for (auto& r : I->regions) if (r.artic == a && r.kind == org::Kind::Attack) { lo = std::min (lo, r.lk); hi = std::max (hi, r.hk); }
            if (hi < 0) continue;
            OrganicParams p; p.human = 0.f; p.artic = a; p.noise = nzEnv ? (float) std::atof (nzEnv) : 0.f;
            for (int key = lo; key <= hi; ++key)
                for (auto& vstr : vs)
                {
                    const int vel = juce::jlimit (1, 127, vstr.getIntValue());
                    const auto& sp = I->span (a, org::Kind::Attack, I->mappedKey (a, key), vel);
                    bool rnd = false; int cand = 0;
                    for (uint32_t i = 0; i < sp.count; ++i) { const auto& r = I->regions[I->list (sp)[i]]; ++cand; rnd |= (r.randLo > 0.f || r.randHi < 1.f); }
                    const int presses = std::clamp (rnd ? 2 * cand : cand, 1, 12);
                    double pk = -200.0;
                    for (int k = 0; k < presses; ++k)
                    {
                        OrganicEngine e; e.prepare (gSR, 256); e.setInstrument (I);
                        e.noteOn (key, (float) vel / 127.f, 1, kNoDet, 0x9e3779b9u + 7919u * (uint32_t) k);
                        Buf l (256), r (256);
                        const int64_t hold = (int64_t) (0.6 * gSR), total = hold + (int64_t) (1.5 * gSR);
                        bool off = false;
                        for (int64_t t = 0; t < total; t += 256)
                        {
                            if (! off && t >= hold) { e.noteOff (false); off = true; }
                            std::fill (l.begin(), l.end(), 0.f); std::fill (r.begin(), r.end(), 0.f);
                            e.render (p, 0.f, l.data(), r.data(), 256);
                            for (int i = 0; i < 256; ++i) pk = std::max (pk, db (std::max (std::abs ((double) l[(size_t) i]), std::abs ((double) r[(size_t) i]))));
                            if (off && ! e.isActive()) break;
                        }
                        e.kill(); e.setInstrument (nullptr);
                    }
                    if (pk > -1.0) ++worstBad;
                    std::printf ("PEAK %s %d %d %d %.3f %d\n", id.toRawUTF8(), a, key, vel, pk, presses);
                }
        }
        std::fflush (stdout);
        I.reset(); org::drainDeferredReleases();
    }
    std::printf ("PEAKSUMMARY %d keys over -1 dBFS — %s\n", worstBad, worstBad ? "FAIL" : "PASS");
    return worstBad ? 1 : 0;
}

int runKnobs (const juce::File& root);   // organics_audit_knobs.cpp

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: %s --lib|--loops|--knobs|--null <root> [...]\n", argv[0]); return 2; }
    const std::string mode = argv[1];
    const auto root = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    setEnv ("TERRAIN_ORGANICS_DIR", root.getFullPathName().toRawUTF8());
    juce::MessageManager::getInstance();
    OrganicsLibrary::get().rescan();
    if (mode == "--lib")
    {
        FILE* tsv = std::getenv ("ORG_AUDIT_TSV") ? std::fopen (std::getenv ("ORG_AUDIT_TSV"), "w") : nullptr;
        const int rc = runLib (root, argc >= 4 ? juce::String (argv[3]) : juce::String(), tsv);
        if (tsv) std::fclose (tsv);
        return rc;
    }
    if (mode == "--loops") return runLoops (root, argc >= 4 ? juce::String (argv[3]) : juce::String());
    if (mode == "--knobs") return runKnobs (root);
    if (mode == "--calib") return runCalib (argc >= 4 ? juce::String (argv[3]) : juce::String());
    if (mode == "--peaks") return runPeaks (argc >= 4 ? juce::String (argv[3]) : juce::String(), argc >= 5 ? juce::String (argv[4]) : juce::String());
    if (mode == "--pitchdump" && argc >= 5) return runPitchDump (juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]), argv[4], argc >= 6 ? juce::String (argv[5]) : juce::String());
    if (mode == "--note" && argc >= 7) return runNote (argv[3], std::atoi (argv[4]), std::atoi (argv[5]), std::atoi (argv[6]), argc >= 8 ? std::atof (argv[7]) : 0.6,
                                                       argc >= 9 ? std::atof (argv[8]) : 0.5, argc >= 10 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[9]) : juce::File());
    if (mode == "--null" && argc >= 4) return runHash (root, juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]), argc >= 5 && std::string (argv[4]) == "check");
    return 2;
}
