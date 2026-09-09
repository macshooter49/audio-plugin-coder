// wt_bake_bench.cpp — HOW LONG DOES THE BAKE ACTUALLY TAKE?
// Max: "the tables take a FEW SECONDS. Serum's take NONE."
// Compiles the SHIPPING Wavetable.h (not a copy) and times buildFromPcm on a REAL factory .wav,
// so the answer is the plugin's own code on the plugin's own data. If this comes back in tens of
// milliseconds the stall is NOT the bake and the search moves to the bridge/UI.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

// Wavetable.h pulls juce_core in for exactly three symbols (jlimit/jmax/jmin), so link the real
// module rather than stub them — a stub that disagreed with JUCE's clamping would silently change
// the frame count and the bench would time something the plugin never runs.
#include "Wavetable.h"

using clk = std::chrono::steady_clock;
static double ms (clk::time_point a, clk::time_point b)
{ return std::chrono::duration<double, std::milli> (b - a).count(); }

// minimal RIFF float32 reader — the factory files are WAVE_FORMAT_IEEE_FLOAT, 1 ch, 44.1k
static bool readWavF32 (const char* path, std::vector<float>& out, int& ch, int& sr)
{
    FILE* f = std::fopen (path, "rb"); if (! f) return false;
    std::fseek (f, 0, SEEK_END); long sz = std::ftell (f); std::fseek (f, 0, SEEK_SET);
    std::vector<uint8_t> raw ((size_t) sz);
    if (std::fread (raw.data(), 1, (size_t) sz, f) != (size_t) sz) { std::fclose (f); return false; }
    std::fclose (f);
    if (sz < 44 || std::memcmp (raw.data(), "RIFF", 4) || std::memcmp (raw.data() + 8, "WAVE", 4)) return false;
    size_t p = 12; ch = 0; sr = 0;
    while (p + 8 <= (size_t) sz)
    {
        char id[5] = {0}; std::memcpy (id, raw.data() + p, 4);
        uint32_t len; std::memcpy (&len, raw.data() + p + 4, 4);
        const size_t body = p + 8;
        if (! std::strcmp (id, "fmt ")) { uint16_t c; uint32_t s;
            std::memcpy (&c, raw.data() + body + 2, 2); std::memcpy (&s, raw.data() + body + 4, 4);
            ch = c; sr = (int) s; }
        else if (! std::strcmp (id, "data")) {
            const size_t n = len / 4; out.resize (n);
            std::memcpy (out.data(), raw.data() + body, n * 4); return true; }
        p = body + len + (len & 1);
    }
    return false;
}

int main (int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : nullptr;
    if (! path) { std::printf ("usage: wt_bake_bench <file.wav>\n"); return 2; }
    std::vector<float> pcm; int ch = 0, sr = 0;
    if (! readWavF32 (path, pcm, ch, sr)) { std::printf ("could not read %s\n", path); return 2; }

    const int FS = tw::Wavetable::kFrameSize;
    const int fileFrames = (int) pcm.size() / FS;
    std::printf ("\n══ WAVETABLE BAKE — the shipping Wavetable.h on a real factory file ══\n");
    std::printf ("  file        %s\n", path);
    std::printf ("  pcm         %zu samples  (%d ch, %d Hz)  → %d frames of %d\n",
                 pcm.size(), ch, sr, fileFrames, FS);
    std::printf ("  ladder      kNumMipLevels = %d   mipData = %d x %d x %d floats = %.1f MB\n",
                 tw::Wavetable::kNumMipLevels, tw::Wavetable::kNumMipLevels, fileFrames, FS,
                 (double) tw::Wavetable::kNumMipLevels * fileFrames * FS * 4.0 / 1048576.0);
    std::printf ("  transform   %s\n\n", tw::wtfft::accelerated() ? "vDSP (Accelerate)" : "*** SCALAR radix-2 fallback ***");

    // the SHIPPING call: importAudioAsWavetable detects a wavetable file and passes n/frameSize
    std::printf ("  frames   transforms        bake ms   (5 runs, ms)\n");
    for (int frames : { 8, 40, 128 })
    {
        if (frames > tw::Wavetable::kMaxFrames) continue;
        double best = 1e18, tot = 0; std::string all;
        for (int r = 0; r < 5; ++r)
        {
            tw::Wavetable wt;                                  // fresh, like the double-buffer's other half
            auto t0 = clk::now();
            wt.buildFromPcm (pcm.data(), (int) pcm.size(), frames);
            auto t1 = clk::now();
            const double d = ms (t0, t1); best = std::min (best, d); tot += d;
            char b[32]; std::snprintf (b, sizeof b, "%s%.1f", r ? " " : "", d); all += b;
        }
        std::printf ("  %5d   %6d      %9.1f   [%s]%s\n", frames, frames * (1 + tw::Wavetable::kNumMipLevels),
                     best, all.c_str(), frames == fileFrames ? "   <- THE SHIPPING CALL" : "");
    }

    // the factory-spec path, for scale
    {
        auto spec = tw::Wavetable::makeProphetSawSpec();
        double best = 1e18;
        for (int r = 0; r < 5; ++r)
        { tw::Wavetable wt; auto t0 = clk::now(); wt.buildFromSpec (spec); auto t1 = clk::now();
          best = std::min (best, ms (t0, t1)); }
        std::printf ("\n  buildFromSpec (16 frames, the built-in path)   %.1f ms\n", best);
    }

    // what does the ALLOCATION alone cost? (mipData_.assign zero-fills the whole thing)
    {
        const size_t n = (size_t) tw::Wavetable::kNumMipLevels * (size_t) fileFrames * (size_t) FS;
        double best = 1e18;
        for (int r = 0; r < 5; ++r)
        { std::vector<float> v; auto t0 = clk::now(); v.assign (n, 0.0f);
          volatile float sink = v[n - 1]; (void) sink; auto t1 = clk::now();
          best = std::min (best, ms (t0, t1)); }
        std::printf ("  zero-fill of the mip buffer alone (%.1f MB)   %.1f ms\n",
                     (double) n * 4.0 / 1048576.0, best);
    }
    std::printf ("\n");
    return 0;
}
