// organics_audit_tone.cpp — THE TONE AUDIT (tp108). Part of Tests/organics_audit (--tone). Tone's job is BRIGHTNESS, so its
// metric is the power spectral centroid (phase-independent, fb283) plus the HF ratio (energy above max(3 kHz, 2·centroid₀) re the
// whole note) and, on C7, the energy above 16 kHz (the exciter's aliasing guard). Two passes:
//   1. the TABLE: eight instruments (16 rows) at their measured keys × Tone −1 … +1. Bars:
//        • the pure tones (vibraphone, glockenspiel, tubular bells, flute): centroid(+1) / centroid(−1) ≥ 1.8 and the HF ratio
//          (energy above max(3 kHz, 2 × the note's own Tone-0 centroid)) moves ≥ 6 dB between −1 and +1 (dramatic);
//        • the rich ones (grand C4, violin section, trumpet, nylon guitar): the centroid within ±15 % of TODAY's (ceff345d,
//          the table below, measured before the exciter existed) at every Tone setting — Tone must not get harsher where it
//          already worked (the grand's C6 is WATCHED, not barred: today's Tone didn't reach ×1.8 there either);
//   ORG_TONE_PROBE=1 adds the cheap-probe columns and the +1 spectral peaks per row (diagnostics).
//        • C7 at +1: the energy above 16 kHz no more than 6 dB over today's +1 (or under −60 dB re the note).
//   2. the WHOLE LIBRARY: every installed instrument at its centre key, centroid(+1) / centroid(−1) ≥ 1.3 (the knob sweep's
//      own bar) — Tone reads on every instrument; and every instrument's C7 at +1: the energy over 16 kHz with the Tone stages
//      no more than 6 dB over the tilt alone (or under −60 dB re the note) — no aliasing blow-up anywhere.
#include "../Source/organics/OrganicEngine.h"
#include "../Source/organics/OrganicsLibrary.h"
#include <juce_events/juce_events.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace tw;
namespace
{
    using Buf = std::vector<float>;
    constexpr double kPi = 3.14159265358979323846, SR = 48000.0;
    const float kNoDetT[16] = {};
    double dbt (double v) { return 10.0 * std::log10 (std::max (v, 1e-30)); }   // POWER dB
    std::string fmtt (const char* f, ...) { char b[1024]; va_list ap; va_start (ap, f); std::vsnprintf (b, sizeof b, f, ap); va_end (ap); return b; }

    void fftt (std::vector<std::complex<double>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = -2.0 * kPi / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
        }
    }
    /** Power spectrum of [s, s+n) (Hann). */
    std::vector<double> powt (const Buf& x, int64_t s, int n)
    {
        std::vector<std::complex<double>> a ((size_t) n);
        for (int i = 0; i < n; ++i) { const double h = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1)); a[(size_t) i] = s + i < (int64_t) x.size() && s + i >= 0 ? h * x[(size_t) (s + i)] : 0.0; }
        fftt (a);
        std::vector<double> m ((size_t) n / 2); for (int i = 0; i < n / 2; ++i) m[(size_t) i] = std::norm (a[(size_t) i]);
        return m;
    }
    struct Meas { double cen = 0, hf = 0, top = 0; };
    /** The knob sweep's window (20 ms after the onset, 8192 frames): centroid · HF ratio (energy above fhf) · >16 kHz, both
        re the whole note (power dB). fhf = max(3 kHz, 2 × the note's own centroid at Tone 0): "above where this note lives"
        (a glockenspiel sounds far over its key, so a key-based split would call its whole note HF). */
    Meas measure (const Buf& x, double fhf)
    {
        const int n = 8192; const auto p = powt (x, (int64_t) (0.02 * SR), n);
        double num = 0, den = 0, hf = 0, top = 0;
        for (size_t i = 1; i < p.size(); ++i)
        {
            const double f = (double) i * SR / n; if (f < 20) continue;
            if (f <= 20000) { num += f * p[i]; den += p[i]; }
            if (f >= fhf) hf += p[i];
            if (f >= 16000) top += p[i];
        }
        Meas m; m.cen = num / std::max (1e-30, den); m.hf = dbt (hf / std::max (1e-30, den)); m.top = dbt (top / std::max (1e-30, den));
        return m;
    }
    std::shared_ptr<const OrganicInstrument> loadt (const juce::String& id)
    {
        std::shared_ptr<const OrganicInstrument> out; bool done = false;
        OrganicsLibrary::get().request (id, [&] (std::shared_ptr<const OrganicInstrument> p) { out = p; done = true; });
        for (int i = 0; i < 4000 && ! done; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        return out;
    }
    Buf rendt (const std::shared_ptr<const OrganicInstrument>& I, float tone, int key, int vel)
    {
        I->resetPerformanceState();
        OrganicEngine e; e.prepare (SR, 256); e.setInstrument (I);
        OrganicParams p; p.human = 0.f; p.tone = tone;
        e.noteOn (key, (float) vel / 127.f, 1, kNoDetT, 1u);
        Buf M; Buf l (256), r (256);
        for (int64_t t = 0; t < (int64_t) (0.5 * SR); t += 256)
        {
            std::fill (l.begin(), l.end(), 0.f); std::fill (r.begin(), r.end(), 0.f);
            e.render (p, 0.f, l.data(), r.data(), 256);
            for (int i = 0; i < 256; ++i) M.push_back (0.5f * (l[(size_t) i] + r[(size_t) i]));
        }
        return M;
    }
    double mtoft (int k) { return 440.0 * std::pow (2.0, (k - 69) / 12.0); }
    /** The engine's own cheap brightness probes over [s, s+n): f_rms from the first difference (Hz) and f_zc from the
        zero-crossing rate (Hz) — what the runtime can afford to measure per block. */
    std::pair<double, double> probe (const Buf& x, int64_t s, int64_t n)
    {
        double px = 0, pd = 0; int zc = 0;
        for (int64_t i = std::max<int64_t> (1, s); i < s + n && i < (int64_t) x.size(); ++i)
        { const double a = x[(size_t) i], b = x[(size_t) i - 1]; px += a * a; pd += (a - b) * (a - b); zc += (a >= 0) != (b >= 0); }
        const double fr = SR / kPi * std::asin (std::min (1.0, 0.5 * std::sqrt (pd / std::max (1e-30, px))));
        return { fr, 0.5 * zc * SR / (double) n };
    }

    /** Offline twin of the engine's prediction: the ±1 tilt's centroid swing from the power spectrum of [s, s+n). */
    double predSwing (const Buf& x, int64_t s, int n, double pivot)
    {
        const auto P = powt (x, s, n);
        const double K = std::tan (kPi * pivot / SR), A2 = std::pow (10.0, 0.9);
        double cn[2] {}, cd[2] {};
        for (size_t k = 1; k < P.size(); ++k)
        {
            const double f = (double) k * SR / n; if (f < 20 || f > 20000) continue;
            const double w = std::tan (kPi * f / SR);
            for (int q = 0; q < 2; ++q) { const double a2 = q == 0 ? A2 : 1.0 / A2, g = P[k] * (a2 * w * w + K * K) / (w * w + a2 * K * K); cn[q] += g * f; cd[q] += g; }
        }
        return (cn[0] / cd[0]) / (cn[1] / cd[1]);
    }

    constexpr int kNT = 7;
    const float kTones[kNT] = { -1.f, -0.5f, -0.25f, 0.f, 0.25f, 0.5f, 1.f };
    enum Cls { Pure, Rich, Watch };   // Watch = reported, not barred (a rich instrument on a key where Tone did NOT already reach ×1.8)
    struct Row { const char* id; int key; Cls cls; double base[kNT]; double baseTop; };   // base = TODAY's centroid (Hz) per Tone; baseTop = today's >16 kHz dB at +1
    // TODAY (ceff345d, before the exciter): `organics_audit --tone` on the installed library, 2026-09-25. Filled from that run.
    const Row kRows[] = {
        { "vcsl.mallets.vibraphone", 65, Pure, { 349.7, 350.9, 352.6, 355.9, 362.0, 371.8, 401.0 }, -67.8 },
        { "vcsl.mallets.vibraphone", 77, Pure, { 698.8, 698.8, 698.8, 698.9, 698.9, 698.9, 699.0 }, -68.9 },
        { "vcsl.mallets.vibraphone", 96, Pure, { 2092.8, 2092.9, 2092.9, 2093.0, 2093.0, 2093.0, 2093.1 }, -55.1 },
        { "vcsl.mallets.glockenspiel", 84, Pure, { 5359.9, 5587.5, 5651.8, 5700.3, 5741.7, 5781.0, 5858.7 }, -50.4 },
        { "vcsl.mallets.glockenspiel", 72, Pure, { 1843.7, 2397.6, 2705.1, 3018.5, 3341.2, 3678.1, 4369.2 }, -55.7 },
        { "vcsl.mallets.glockenspiel", 96, Pure, { 4173.8, 4414.8, 4503.2, 4581.8, 4660.6, 4752.3, 5037.9 }, -47.4 },
        { "vcsl.bells.tubular", 67, Pure, { 1144.0, 1229.2, 1279.8, 1333.8, 1392.0, 1457.3, 1618.1 }, -51.2 },
        { "vcsl.bells.tubular", 60, Pure, { 1106.1, 1209.0, 1264.2, 1318.8, 1373.9, 1432.2, 1563.7 }, -53.2 },
        { "vsco2.woodwinds.flute", 72, Pure, { 848.1, 970.0, 1057.7, 1153.5, 1247.9, 1335.8, 1490.5 }, -56.7 },
        { "vsco2.woodwinds.flute", 84, Pure, { 1057.6, 1065.7, 1073.3, 1084.6, 1100.0, 1119.7, 1165.7 }, -58.4 },
        { "vsco2.woodwinds.flute", 96, Pure, { 2053.0, 2060.0, 2064.9, 2070.4, 2076.1, 2081.8, 2093.8 }, -49.4 },
        { "salamander.grand.v3", 60, Rich, { 385.5, 421.9, 446.3, 476.8, 515.6, 563.9, 678.4 }, -84.0 },
        { "salamander.grand.v3", 84, Watch, { 963.4, 1151.4, 1242.8, 1336.1, 1429.2, 1516.0, 1648.5 }, -80.9 },
        { "vsco2.strings.violin-section", 69, Rich, { 455.3, 484.9, 523.3, 596.0, 722.3, 913.6, 1432.4 }, -38.5 },
        { "vsco2.brass.trumpet", 67, Rich, { 673.7, 803.6, 899.7, 1003.8, 1101.4, 1184.2, 1305.8 }, -61.1 },
        { "freepats.guitar.nylon", 52, Rich, { 185.2, 193.3, 198.2, 204.6, 214.2, 230.1, 292.7 }, -60.6 },
    };
}

int runTone (const juce::File&, bool baseOnly)
{
    int fails = 0, bars = 0;
    auto bar = [&] (bool ok, const std::string& s) { ++bars; if (! ok) ++fails; std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", s.c_str()); };
    std::printf ("══ ORGANICS TONE AUDIT — centroid (Hz) at Tone −1 … +1, vel 80, 20 ms + 8192 ══\n");
    std::printf ("%-30s key  %8s %8s %8s %8s %8s %8s %8s   ratio  HF−1  HF0  HF+1  >16k(0/+1)\n", "instrument", "−1", "−.5", "−.25", "0", "+.25", "+.5", "+1");
    std::string cur; std::shared_ptr<const OrganicInstrument> I;
    for (const auto& row : kRows)
    {
        if (cur != row.id) { I.reset(); org::drainDeferredReleases(); I = loadt (row.id); cur = row.id; }
        if (! I) { bar (false, fmtt ("%s missing", row.id)); continue; }
        Meas m[kNT];
        const double fhf = std::max (3000.0, 2.0 * measure (rendt (I, 0.f, row.key, 80), 3000.0).cen);
        for (int t = 0; t < kNT; ++t) m[t] = measure (rendt (I, kTones[t], row.key, 80), fhf);
        const auto ts = organics_debug::lastTone();   // the +1 render's own measurement (the last one above)
        std::printf ("      engine: predicted tilt swing ×%.2f (f_rms ×%.2f) · flatness %.3f → sparse %.2f · f_dom %.0f Hz\n", ts.swing, ts.swingRms, ts.flat, ts.sparse, ts.fDom);
        const double ratio = m[kNT - 1].cen / std::max (1.0, m[0].cen), dHf = m[kNT - 1].hf - m[0].hf;
        std::string s; for (int t = 0; t < kNT; ++t) s += fmtt (" %8.0f", m[t].cen);
        std::printf ("%-30s %3d %s   ×%.2f %5.1f %5.1f %5.1f  %6.1f/%6.1f\n", row.id, row.key, s.c_str(), ratio, m[0].hf, m[3].hf, m[kNT - 1].hf, m[3].top, m[kNT - 1].top);
        if (std::getenv ("ORG_TONE_PROBE"))
        {
            const auto x0 = rendt (I, 0.f, row.key, 80), x1 = rendt (I, 1.f, row.key, 80), xm = rendt (I, -1.f, row.key, 80);
            const auto a = probe (x0, 960, 8192), b = probe (x1, 960, 8192), c = probe (xm, 960, 8192), a2 = probe (x0, 0, 24000);
            {
                const auto P = powt (x1, 960, 8192); double tot = 0; for (double v : P) tot += v;
                std::string pk;
                for (size_t i = 2; i + 2 < P.size(); ++i) { const double f = (double) i * SR / 8192; if (P[i] > P[i - 1] && P[i] >= P[i + 1] && P[i] > tot * 1e-6) pk += fmtt (" %.0f:%.0f", f, dbt (P[i] / tot)); }
                std::printf ("PEAKS+1 %s\n", pk.c_str());
            }
            std::printf ("PROBE   f0 %.0f  frms %.0f fzc %.0f P %.2f (0-500ms P %.2f) · E+ %.2f E− %.2f\n", mtoft (row.key), a.first, a.second, a.first / a.second, a2.first / a2.second, b.first / a.first, c.first / a.first);
        }
        if (baseOnly)
        {
            std::string b; for (int t = 0; t < kNT; ++t) b += fmtt ("%s%.1f", t ? ", " : "", m[t].cen);
            std::printf ("BASE        { \"%s\", %d, %s, { %s }, %.1f },\n", row.id, row.key, row.cls == Pure ? "Pure" : "Rich", b.c_str(), m[kNT - 1].top);
            continue;
        }
        if (row.cls == Pure)
            bar (ratio >= 1.8 && dHf >= 6.0, fmtt ("%s k%d: centroid ×%.2f (bar ×1.8; today ×%.2f) · HF ratio %+.1f dB (bar 6)", row.id, row.key, ratio,
                                                     row.base[kNT - 1] / std::max (1.0, row.base[0]), dHf));
        else
        {
            double worst = 0; int wt = 0;
            for (int t = 0; t < kNT; ++t) { const double d = m[t].cen / std::max (1.0, row.base[t]) - 1.0; if (std::abs (d) > std::abs (worst)) { worst = d; wt = t; } }
            const auto msg = fmtt ("%s k%d: centroid within ±15 %% of today at every Tone (worst %+.1f %% at Tone %+.2f) · ×%.2f (today ×%.2f)", row.id, row.key,
                                   100 * worst, kTones[wt], ratio, row.base[kNT - 1] / std::max (1.0, row.base[0]));
            if (row.cls == Rich) bar (std::abs (worst) <= 0.15, msg);
            else std::printf ("INFO  %s — watched, not barred: today's Tone did not reach ×1.8 on this key either\n", msg.c_str());
        }
        if (row.key == 96)
            bar (m[kNT - 1].top <= std::max (row.baseTop + 6.0, -60.0), fmtt ("%s C7 +1: energy > 16 kHz %.1f dB re the note (today %.1f; bar ≤ max(today + 6, −60))",
                                                                              row.id, m[kNT - 1].top, row.baseTop));
        std::fflush (stdout);
    }
    I.reset(); org::drainDeferredReleases();
    if (baseOnly) return 0;

    // ── 2. the whole library: every instrument at its centre key ──
    std::printf ("── the whole library: centroid(+1) / centroid(−1) at the centre key (artic 0, vel 80), bar ×1.3 · C7 at +1: energy > 16 kHz"
                 " with the Tone stages ≤ max(the tilt alone + 6 dB, −60 dB) ──\n");
    const auto idx = OrganicsLibrary::get().index();
    int n = 0, lowN = 0; double lowR = 1e9; std::string lowId;
    int hotN = 0; double worstC7 = -1e9; std::string worstC7Id;
    for (int ii = 0; idx.isArray() && ii < idx.size(); ++ii)
    {
        const juce::String id = idx[ii]["id"].toString();
        auto J = loadt (id);
        if (! J) { bar (false, fmtt ("%s did not load", id.toRawUTF8())); continue; }
        int lo = 128, hi = -1;
        for (auto& r : J->regions) if (r.artic == 0 && r.kind == org::Kind::Attack) { lo = std::min (lo, r.lk); hi = std::max (hi, r.hk); }
        if (hi < 0) { J.reset(); org::drainDeferredReleases(); continue; }
        const int key = (lo <= 60 && 60 <= hi) ? 60 : (lo + hi + 1) / 2;
        const auto x0 = rendt (J, 0.f, key, 80); const auto z = measure (x0, 3000.0);
        const double pv = std::max (700.0, mtoft (key)), ps43 = predSwing (x0, 960, 2048, pv), ps170 = predSwing (x0, 960, 8192, pv);
        const double fhf = std::max (3000.0, 2.0 * z.cen);
        const auto a = measure (rendt (J, -1.f, key, 80), fhf), b = measure (rendt (J, 1.f, key, 80), fhf);
        const auto ts = organics_debug::lastTone();
        const double r = b.cen / std::max (1.0, a.cen);
        // C7 at +1, the Tone stages on vs off (the tilt alone): what the exciter adds over 16 kHz (tp108b: the flute's trimmed
        // top keys pushed −33 dB — a breathy onset through the shaper)
        const double c7on = measure (rendt (J, 1.f, 96, 80), 16000.0).top;
        organics_debug::setToneStages (false);
        const double c7off = measure (rendt (J, 1.f, 96, 80), 16000.0).top;
        organics_debug::setToneStages (true);
        const bool hot = c7on > std::max (c7off + 6.0, -60.0);
        if (hot) ++hotN;
        if (c7on - std::max (c7off + 6.0, -60.0) > worstC7) { worstC7 = c7on - std::max (c7off + 6.0, -60.0); worstC7Id = fmtt ("%s %+.1f dB (tilt alone %+.1f)", id.toRawUTF8(), c7on, c7off); }
        if (hot) std::printf ("HOT   %-36s C7 +1: > 16 kHz %+.1f dB re the note, the tilt alone %+.1f\n", id.toRawUTF8(), c7on, c7off);
        ++n; if (r < 1.3) ++lowN;
        if (r < lowR) { lowR = r; lowId = id.toStdString(); }
        std::printf ("%s  %-36s k%-3d  −1 %6.0f  0 %6.0f  +1 %6.0f Hz  ×%.2f   HF %+.1f dB   (swing %.2f · f_rms %.2f · sparse %.2f · f_dom %.0f · offline 43ms %.2f 170ms %.2f)\n", r >= 1.3 ? "    " : "LOW ",
                     id.toRawUTF8(), key, a.cen, z.cen, b.cen, r, b.hf - a.hf, ts.swing, ts.swingRms, ts.sparse, ts.fDom, ps43, ps170);
        std::fflush (stdout);
        J.reset(); org::drainDeferredReleases();
    }
    bar (lowN == 0 && n > 0, fmtt ("the whole library: Tone ×1.3 on %d/%d instruments (lowest ×%.2f, %s)", n - lowN, n, lowR, lowId.c_str()));
    bar (hotN == 0 && n > 0, fmtt ("the whole library: C7 at +1, energy > 16 kHz ≤ max(tilt alone + 6, −60) dB on %d/%d instruments (closest: %s)", n - hotN, n, worstC7Id.c_str()));
    std::printf ("══ %s — tone audit: %d/%d bars pass ══\n", fails ? "FAIL" : "PASS", bars - fails, bars);
    return fails ? 1 : 0;
}
