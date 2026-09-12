// ══════════════════════════════════════════════════════════════════════════════════════════════
//  altwarp_au.cpp — fb636 ALT WARP (39-46) ON THE REAL PLUGIN: THE HOST SEES THE NAMES, EVERY MODE SOUNDS,
//  THE CENTRED MODES ARE DRY IN A PLAYING VOICE, ODD/EVEN IS PARITY-EXACT THROUGH THE WHOLE VOICE, AND FLIP'S
//  DC IS GONE ON BOTH ENGINES.
//
//    c++ -std=c++17 -O2 -I Tests Tests/altwarp_au.cpp -framework AudioToolbox -framework CoreFoundation \
//        -o /tmp/awau && TERRAIN_AU_BUNDLE=<Terrain.component> /tmp/awau
//
//  altwarp_cert.cpp proves the kernels on sliced statics; this drives the SHIPPING BINARY the way a DAW does:
//  AudioUnitSetParameter on the host's own parameter list, MIDI, AudioUnitRender, 48 kHz, 512-sample blocks. Every
//  scenario is a FRESH instance of the virgin state (nothing is loaded, read or written — no preset, no blob), with
//  the goldens' determinism hook on (TERRAIN_DETERMINISTIC=1, set here): without it a voice's start phase follows
//  its heap address, and a comparison across two instances measures the allocator, not the warp.
//
//  BARS
//   [A0] REACH     the 8 warp-mode parameters and Osc A's amounts, engine, unison and table are host parameters
//   [A1] NAMES     every warp-mode parameter has 48 value strings: 39-46 the Alt Warp names, 47 'Reserved 47',
//                  0 'NONE', 38 'Draw Amp'; each of 39-46 round-trips through the host
//   [A2] ALIVE     each of 39-46 on Osc A changes the rendered sound off its dry point (the fb470 rule on the
//                  binary), and at its static dry point (0 %; Flip 0 %) renders BIT-IDENTICAL to warp None;
//                  every sample finite, none subnormal
//   [A3] DRY WHILE PLAYING   41 / 44 / 46 at 0 %, moved to 50 % mid-note: after the float glide settles
//                  (it STALLS short of ½ — the dead band's reason to exist) the mode is EXACTLY dry. Measured with
//                  the history held equal: both instances play the mode and make the same move; after 0.32 s only
//                  the reference switches its mode to None, and from there on the two must be BIT-IDENTICAL.
//                  (A never-changed None is NOT a usable reference: after ANY warp change the voice keeps a
//                  ~−70 dB trace of the earlier signal — the SHIPPED Skew moved 0.3 -> 0 shows it too — so that
//                  comparison measures the voice's memory, not the dead band.) CONTROL: the same test at 49.9 %
//                  (|s| = 0.002, just outside the band) must differ.
//   [A4] PARITY    Odd/Even through the whole voice, on the first of Osc A's own tables (the host's value strings)
//                  that carries BOTH parities dry (the virgin table is odd-only, which would make 100 % vacuous):
//                  0 % leaves no even harmonic above −90 dBr, 100 % no odd one (h1 included); CONTROL: 50 %
//                  carries both (above −40 dBr)
//   [A5] FLIP DC   Flip 25 % in slot 1 on the WT engine and on the FM engine: the held note's mean over 55 WHOLE
//                  cycles is < 0.1 % of its rms (Max: remove Flip's DC, FM slot 1 included); CONTROL: Skew 100 % — a
//                  phase warp that makes DC and is NOT blocked — reads > 1 % on the same meter
//   [A6] STRESS    unison 16 on WT and on FM, every Alt mode at its extremes: finite, no subnormal, peak < 8
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "au_state_blob.h"
#include <functional>

static const char* kAlt[8] = { "Bend +", "Bend -", "Bend +/-", "Asym +", "Asym -", "Asym +/-", "Flip", "Odd/Even" };
static const char* kModeParams[8] = { "Synth OSC A Warp Mode", "Synth OSC A Warp 2 Mode", "Synth OSC B Warp Mode", "Synth OSC B Warp 2 Mode",
                                      "Synth OSC C Warp Mode", "Synth OSC C Warp 2 Mode", "Synth OSC D Warp Mode", "Synth OSC D Warp 2 Mode" };
static const std::string MODE1 = "Synth OSC A Warp Mode", AMT1 = "Synth OSC A Warp Amount",
                         MODE2 = "Synth OSC A Warp 2 Mode", AMT2 = "Synth OSC A Warp 2 Amount",
                         ENGINE = "Synth OSC A Engine", UNISON = "Synth OSC A Unison", TABLE = "Synth OSC A WT Preset";
using Sets = std::vector<std::pair<std::string, float>>;

// one fresh instance: settings, pump, note on, then each step = its settings + that many blocks
struct Step { Sets sets; int blocks; };
static std::vector<float> play (const Sets& pre, int note, const std::vector<Step>& steps)
{
    AU a; if (! a.open()) return {};
    for (auto& s : pre) a.set (s.first, s.second);
    a.pump (0.45);
    a.midi (0x90, (UInt32) note, 100);
    std::vector<float> out;
    for (auto& st : steps)
    {
        for (auto& s : st.sets) a.set (s.first, s.second);
        auto more = a.render (st.blocks);
        out.insert (out.end(), more.begin(), more.end());
    }
    a.midi (0x80, (UInt32) note, 0);
    a.close();
    return out;
}
static std::vector<float> play (const Sets& pre, int note, int blocks) { return play (pre, note, { Step { {}, blocks } }); }
static double rmsOf (const std::vector<float>& v, size_t from, size_t to)
{ double s = 0; for (size_t i = from; i < to && i < v.size(); ++i) s += (double) v[i] * v[i]; return std::sqrt (s / std::max<size_t> (1, to - from)); }
static double diffDb (const std::vector<float>& x, const std::vector<float>& r, size_t from, size_t to)
{
    double d = 0; for (size_t i = from; i < to && i < x.size() && i < r.size(); ++i) d = std::max (d, (double) std::fabs (x[i] - r[i]));
    return 20.0 * std::log10 (std::max (1e-30, d) / std::max (1e-30, rmsOf (r, from, to)));
}
static bool sane (const std::vector<float>& v, float& peak)
{
    peak = 0; bool ok = ! v.empty();
    for (float s : v) { if (! std::isfinite (s) || std::fpclassify (s) == FP_SUBNORMAL) ok = false; peak = std::max (peak, std::fabs (s)); }
    return ok;
}
// Hann-windowed DFT magnitude at each harmonic of f0 (the warp_filter_probe metric)
static std::vector<double> harmonics (const std::vector<float>& v, size_t from, size_t n, double f0, int K)
{
    std::vector<double> H ((size_t) K + 1, 0.0);
    for (int k = 1; k <= K; ++k)
    {
        if (k * f0 >= SR / 2 - 200) break;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {   const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) (n - 1));
            const double ph = 2.0 * M_PI * k * f0 * (double) i / SR;
            re += v[from + i] * w * std::cos (ph); im -= v[from + i] * w * std::sin (ph); }
        H[(size_t) k] = std::sqrt (re * re + im * im);
    }
    return H;
}
static double parityDbr (const std::vector<double>& H, bool even)
{
    double mx = 0, worst = 0;
    for (size_t k = 1; k < H.size(); ++k) mx = std::max (mx, H[k]);
    for (size_t k = even ? 2 : 1; k < H.size(); k += 2) worst = std::max (worst, H[k]);
    return 20.0 * std::log10 (std::max (1e-30, worst) / std::max (1e-30, mx));
}
static std::vector<std::string> valueStrings (AU& a, const std::string& name)
{
    std::vector<std::string> out; if (! a.has (name)) return out;
    CFArrayRef arr = nullptr; UInt32 sz = sizeof arr;
    if (AudioUnitGetProperty (a.au, kAudioUnitProperty_ParameterValueStrings, kAudioUnitScope_Global, a.byName[name], &arr, &sz) != noErr || ! arr) return out;
    for (CFIndex i = 0; i < CFArrayGetCount (arr); ++i) out.push_back (cf2s ((CFStringRef) CFArrayGetValueAtIndex (arr, i)));
    CFRelease (arr); return out;
}
static double meanOverRms (const std::vector<float>& v, size_t from, size_t to)
{ double m = 0; for (size_t i = from; i < to; ++i) m += v[i]; m /= (double) (to - from); return std::fabs (m) / std::max (1e-30, rmsOf (v, from, to)); }

int main()
{
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);   // before the first open: the side-load reads it
    std::printf ("altwarp_au — fb636 ALT WARP on %s\n\n", std::getenv ("TERRAIN_AU_BUNDLE") ? std::getenv ("TERRAIN_AU_BUNDLE") : "the INSTALLED AU");

    // ── [A0] + [A1] ────────────────────────────────────────────────────────────────────────────────
    {
        AU a; if (! a.open()) { std::printf ("  !! cannot open the AU\n"); return 2; }
        int reach = 0; std::string missing;
        for (auto* n : kModeParams) if (a.has (n)) ++reach; else missing += std::string (" ") + n;
        for (auto& n : { AMT1, AMT2, ENGINE, UNISON, TABLE }) if (a.has (n)) ++reach; else missing += " " + n;
        chk (reach == 13, "[A0] the harness reaches the 8 warp-mode params and Osc A's amounts, engine, unison, table",
             std::to_string (reach) + "/13" + (missing.empty() ? "" : " — missing:" + missing));
        int good = 0; std::string bad;
        for (auto* n : kModeParams)
        {
            if (! a.has (n)) continue;
            CFArrayRef arr = nullptr; UInt32 sz = sizeof arr;
            const OSStatus st = AudioUnitGetProperty (a.au, kAudioUnitProperty_ParameterValueStrings, kAudioUnitScope_Global, a.byName[n], &arr, &sz);
            if (st != noErr || ! arr) { bad += std::string (" ") + n + ": no value strings;"; continue; }
            const CFIndex cnt = CFArrayGetCount (arr);
            auto at = [&] (CFIndex i) { return (i < cnt) ? cf2s ((CFStringRef) CFArrayGetValueAtIndex (arr, i)) : std::string ("?"); };
            bool ok = (cnt == 48) && at (0) == "NONE" && at (38) == "Draw Amp" && at (47) == "Reserved 47";
            for (int i = 0; i < 8; ++i) ok = ok && at (39 + i) == kAlt[i];
            for (int i = 39; i <= 46; ++i) { a.set (n, (float) i); if (std::lround (a.get (n)) != i) ok = false; }
            a.set (n, 0.0f);
            if (ok) ++good; else bad += std::string (" ") + n + " (" + std::to_string (cnt) + " strings, 39 = '" + at (39) + "', 47 = '" + at (47) + "');";
            CFRelease (arr);
        }
        chk (good == 8, "[A1] 48 value strings on all 8 warp-mode params: 39-46 = the Alt Warp names, 47 'Reserved 47'; 39-46 round-trip",
             std::to_string (good) + "/8" + bad);
        a.close();
    }

    const int NOTE = 45;                       // A2, 110 Hz
    const double F0 = 110.0;
    const int BL = 60;                         // 0.64 s
    const size_t LATE0 = 30 * BLK, LATE1 = (size_t) BL * BLK;

    // ── [A2] ALIVE, DRY AT THE STATIC DRY POINT, SANE ──────────────────────────────────────────────
    {
        const auto dry = play ({ {MODE1, 0.0f}, {AMT1, 0.0f} }, NOTE, BL);
        std::string txt, movedTxt; int deadModes = 0, dryMoved = 0, insane = 0; float worstPeak = 0;
        struct M { int m; float live, dryAt; } ms[] = { {39, 0.8f, 0.0f}, {40, 0.8f, 0.0f}, {41, 0.1f, -1}, {42, 0.8f, 0.0f},
                                                        {43, 0.8f, 0.0f}, {44, 0.9f, -1}, {45, 0.3f, 0.0f}, {46, 0.0f, -1} };
        for (auto c : ms)
        {
            const auto live = play ({ {MODE1, (float) c.m}, {AMT1, c.live} }, NOTE, BL);
            const double d = diffDb (live, dry, LATE0, LATE1);
            float pk = 0; if (! sane (live, pk)) ++insane; worstPeak = std::max (worstPeak, pk);
            if (d < -40.0) ++deadModes;
            char b[96]; std::snprintf (b, sizeof b, " %d@%.0f%% %+.0f dB", c.m, c.live * 100, d); txt += b;
            if (c.dryAt >= 0.0f)
            {
                const auto z = play ({ {MODE1, (float) c.m}, {AMT1, c.dryAt} }, NOTE, BL);
                if (z.size() != dry.size() || std::memcmp (z.data(), dry.data(), z.size() * sizeof (float)) != 0)
                { ++dryMoved; char mb[80]; std::snprintf (mb, sizeof mb, " %d@%.0f%% (%.0f dB)", c.m, c.dryAt * 100, diffDb (z, dry, 0, z.size())); movedTxt += mb; }
            }
        }
        chk (deadModes == 0 && dryMoved == 0 && insane == 0 && ! dry.empty(),
             "[A2] every Alt mode changes the sound (late-window max difference vs None, dB re its rms); each static dry point renders BIT-IDENTICAL to None",
             "live:" + txt + " · " + std::to_string (deadModes) + " silent, " + std::to_string (dryMoved) + " dry points moved, "
             + std::to_string (insane) + " renders with a non-finite/subnormal sample, peak " + std::to_string (worstPeak)
             + (movedTxt.empty() ? "" : " — MOVED:" + movedTxt));
    }

    // ── [A3] DRY WHILE PLAYING (the glide stall), with the history held equal ─────────────────────
    {
        const int A = 30, B = 30, C = 40;               // 0.32 s at 0 %, the move + 0.32 s to settle, then 0.43 s compared
        const size_t c0 = (size_t) (A + B) * BLK, c1 = (size_t) (A + B + C) * BLK;
        std::string txt; bool ok = true;
        for (int m : { 41, 44, 46 })
            for (float to : { 0.5f, 0.499f })
            {
                const Sets pre { {MODE1, (float) m}, {AMT1, 0.0f} };
                const auto x   = play (pre, NOTE, { Step { {}, A }, Step { { {AMT1, to} }, B }, Step { {}, C } });
                const auto ref = play (pre, NOTE, { Step { {}, A }, Step { { {AMT1, to} }, B }, Step { { {MODE1, 0.0f} }, C } });
                const bool bit = x.size() == ref.size() && x.size() >= c1
                              && std::memcmp (x.data() + c0, ref.data() + c0, (c1 - c0) * sizeof (float)) == 0;
                const bool same0 = x.size() >= c0 && std::memcmp (x.data(), ref.data(), c0 * sizeof (float)) == 0;   // the held-equal history
                const double d = diffDb (x, ref, c0, c1);
                if (to == 0.5f) ok = ok && bit && same0; else ok = ok && d > -100.0 && same0;
                char b[160]; std::snprintf (b, sizeof b, " %d@%.1f%%: %s (%.0f dB)%s;", m, to * 100, bit ? "BIT-IDENTICAL to None" : "differs from None", d,
                                            same0 ? "" : " [history NOT equal]"); txt += b;
            }
        chk (ok, "[A3] 41/44/46 moved 0 -> 50 % mid-note are EXACTLY dry once the float glide settles (the mode vs None after the same history); 49.9 % is live", txt);
    }

    // ── [A4] PARITY THROUGH THE WHOLE VOICE ───────────────────────────────────────────────────────
    {
        const size_t n = 24000, from = 20 * BLK;      // 24000 samples = 55 whole cycles of 110 Hz
        std::vector<std::string> tables; { AU a; if (a.open()) { tables = valueStrings (a, TABLE); a.close(); } }
        int pick = -1;
        for (int t = 0; t < (int) tables.size() && pick < 0; ++t)
        {
            const auto H = harmonics (play ({ {TABLE, (float) t}, {MODE1, 0.0f}, {AMT1, 0.0f} }, NOTE, 80), from, n, F0, 100);
            if (parityDbr (H, true) > -30.0 && parityDbr (H, false) > -30.0) pick = t;
        }
        if (pick < 0) chk (false, "[A4] Odd/Even through the whole voice", "none of Osc A's " + std::to_string (tables.size()) + " tables carries both parities — nothing to test");
        else
        {
            auto par = [&] (float amt, bool even)
            { const auto v = play ({ {TABLE, (float) pick}, {MODE1, 46.0f}, {AMT1, amt} }, NOTE, 80); return parityDbr (harmonics (v, from, n, F0, 100), even); };
            const double ev0 = par (0.0f, true), od1 = par (1.0f, false), ev5 = par (0.5f, true), od5 = par (0.5f, false);
            char b[320]; std::snprintf (b, sizeof b, "table %d '%s': 0 %%: worst even %.0f dBr · 100 %%: worst odd %.0f dBr · CONTROL 50 %%: evens %.0f, odds %.0f dBr",
                                        pick, tables[(size_t) pick].c_str(), ev0, od1, ev5, od5);
            chk (ev0 <= -90.0 && od1 <= -90.0 && ev5 > -40.0 && od5 > -40.0, "[A4] Odd/Even through the whole voice (Osc A, A2, h1-h100)", b);
        }
    }

    // ── [A5] FLIP DC, WT AND FM ───────────────────────────────────────────────────────────────────
    {
        const size_t f0 = 30 * BLK, f1 = f0 + 24000;   // exactly 55 cycles of 110 Hz: a zero-mean periodic signal sums to 0
        const auto wt   = play ({ {MODE1, 45.0f}, {AMT1, 0.25f} }, NOTE, 80);
        const auto fm   = play ({ {ENGINE, 4.0f}, {MODE1, 45.0f}, {AMT1, 0.25f} }, NOTE, 80);
        const auto skew = play ({ {MODE1, 5.0f}, {AMT1, 1.0f} }, NOTE, 80);
        const auto fmDry = play ({ {ENGINE, 4.0f}, {MODE1, 0.0f}, {AMT1, 0.0f} }, NOTE, 80);
        const double mw = meanOverRms (wt, f0, f1), mf = meanOverRms (fm, f0, f1), ms = meanOverRms (skew, f0, f1);
        const double fmLive = diffDb (fm, fmDry, f0, f1);
        char b[256]; std::snprintf (b, sizeof b, "|mean|/rms: WT Flip %.2e · FM Flip %.2e (FM Flip vs FM dry %+.0f dB, so it is live) · CONTROL Skew 100 %% %.2e",
                                    mw, mf, fmLive, ms);
        chk (mw < 1e-3 && mf < 1e-3 && ms > 1e-2 && fmLive > -40.0, "[A5] Flip's DC is removed on the WT engine and in FM slot 1", b);
    }

    // ── [A6] STRESS ───────────────────────────────────────────────────────────────────────────────
    {
        int bad = 0; float worst = 0;
        for (float eng : { 0.0f, 4.0f })
            for (int m = 39; m <= 46; ++m)
                for (float amt : { 0.0f, 1.0f })
                {
                    const auto v = play ({ {ENGINE, eng}, {UNISON, 16.0f}, {MODE1, (float) m}, {AMT1, amt}, {MODE2, (float) (m == 46 ? 45 : 46)}, {AMT2, 0.7f} }, 60, 24);
                    float pk = 0; if (! sane (v, pk) || pk >= 8.0f) ++bad; worst = std::max (worst, pk);
                }
        chk (bad == 0, "[A6] unison 16, WT and FM, every Alt mode at 0 and 100 % with a second Alt mode in slot 2: finite, no subnormal, peak < 8",
             std::to_string (bad) + " of 32 renders bad, worst peak " + std::to_string (worst));
    }
    return summary();
}
