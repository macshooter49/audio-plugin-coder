// ══ fb621 — EVERYTHING A USER MAKES TRAVELS: the asset envelope, driven against real libFLAC ══════
//   The compile line lives in Tests/fb621_gates.sh (juce_core + juce_audio_basics + juce_audio_formats,
//   no GUI, no AU) — this cert is the FILE LAYER's proof, the same way preset_bank_cert is the bank's.
//
//   AS_MUT=noscale   after encoding, the envelope's `scale` field is rewritten to 1.0 — the exact
//                    field that stops a float WAV peaking above 1.0 from being CLIPPED by 24-bit
//                    quantisation. Bar [2] must go RED: that is the field earning its place.
//   AS_MUT=lenient   bar [5] flips its expectation and demands that garbage be ACCEPTED. A decoder
//                    that correctly refuses makes the mutated run fail — which is the point.
//
//   Max's law (Design/PRESET-SYSTEM-v1.md §3): a preset carries its own audio, so a bank handed to
//   somebody else SOUNDS THE SAME on their machine. Before this, one-shots were absolute paths and
//   wavetable imports were raw float32 base64. Bar [9] is the one that matters to him: an asset
//   written into a .terrain on one machine comes back out of it whole.
#include "PresetAssets.h"
#include "PresetBank.h"
#include <cstdlib>
#include <cstdio>

static int pass = 0, fail = 0;
static void chk (bool ok, const char* name, const juce::String& detail)
{
    ok ? ++pass : ++fail;
    std::printf ("  %s  %s\n        %s\n", ok ? "PASS" : "FAIL", name, detail.toRawUTF8());
}
static juce::String db (double x) { return juce::String (x <= 0 ? -200.0 : 20.0 * std::log10 (x), 1) + " dBFS"; }

static juce::AudioBuffer<float> tone (int nch, int n, float amp, float inc = 0.013f)
{
    juce::AudioBuffer<float> b (nch, n);
    for (int c = 0; c < nch; ++c)
        for (int i = 0; i < n; ++i)
            b.setSample (c, i, amp * std::sin ((float) i * inc + (float) c * 0.7f)
                                * (0.6f + 0.4f * std::sin ((float) i * 0.00031f)));
    return b;
}
static double worstErr (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples()) return 1e9;
    double w = 0.0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
            w = juce::jmax (w, (double) std::abs (a.getSample (c, i) - b.getSample (c, i)));
    return w;
}
static float peakOf (const juce::AudioBuffer<float>& b)
{
    float p = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
    { const auto r = b.findMinMax (c, 0, b.getNumSamples()); p = juce::jmax (p, std::abs (r.getStart()), std::abs (r.getEnd())); }
    return p;
}
// rewrite the envelope's scale field (offset 16, little-endian float) — the AS_MUT=noscale control
static juce::String stripScale (const juce::String& b64)
{
    juce::MemoryBlock mb; if (! mb.fromBase64Encoding (b64) || mb.getSize() < 20) return b64;
    const float one = 1.0f; std::memcpy (static_cast<juce::uint8*> (mb.getData()) + 16, &one, sizeof (float));
    return mb.toBase64Encoding();
}

// ── --emit <dir> [wavetableRoot] ───────────────────────────────────────────────────────────────
//  Writes the REAL encoder's output to disk so Tests/preset_assets_au.cpp can inject exactly what
//  the shipping plugin would have written. That seam is the point: a C++ shape the PROCESSOR does
//  not accept is green on both sides and broken in the plugin, and no single-layer gate sees it
//  (the fb606 payload precedent).
static int emit (const juce::String& dirPath, const juce::String& wtRootPath)
{
    const juce::File dir (dirPath); dir.createDirectory();
    auto put = [&dir] (const char* name, const juce::String& body)
    { dir.getChildFile (name).replaceWithText (body); std::printf ("  %-14s %8d chars\n", name, body.length()); };

    put ("osc.b64",   tw::asset::encode (tone (2, 24000, 0.7f), 48000.0, 0, "Artist One Shot.wav"));
    put ("layer.b64", tw::asset::encode (tone (1, 16000, 0.5f, 0.021f), 44100.0, 0, "Layer.wav"));
    put ("ir.b64",    tw::asset::encode (tone (2, 24000, 0.35f, 0.004f), 48000.0, 0, "Hall.wav"));
    put ("wt.b64",    tw::asset::encode (tone (1, 64 * 2048, 0.9f, 0.29f), 0.0, 64, "Artist Table"));

    // the factory reference — computed from a REAL shipped table in the installed bundle
    const juce::File wtRoot (wtRootPath);
    juce::String ref, refName;
    if (wtRoot.isDirectory())
    {
        auto flacs = wtRoot.findChildFiles (juce::File::findFiles, true, "*.flac");
        flacs.sort();
        if (! flacs.isEmpty())
        {
            const auto& f = flacs.getReference (0);
            juce::MemoryBlock mb; f.loadFileAsData (mb);
            ref = tw::asset::makeRef (f.getRelativePathFrom (wtRoot), tw::asset::hashOf (mb.getData(), mb.getSize()));
            refName = f.getFileNameWithoutExtension();
        }
    }
    put ("wtref.txt",  ref);
    put ("wtname.txt", refName);
    std::printf ("  wavetable root: %s (%s)\n", wtRootPath.toRawUTF8(), wtRoot.isDirectory() ? "found" : "NOT FOUND");
    return ref.isEmpty() ? 2 : 0;
}

int main (int argc, char** argv)
{
    if (argc > 1 && juce::String (argv[1]) == "--emit")
        return emit (argc > 2 ? juce::String (argv[2]) : juce::String ("/tmp/tp_assets"),
                     argc > 3 ? juce::String (argv[3])
                              : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                    .getChildFile ("Library/Audio/Plug-Ins/Components/Terrain.component/Contents/Resources/Wavetables")
                                    .getFullPathName());

    const char* mutC = std::getenv ("AS_MUT"); const juce::String mut (mutC ? mutC : "");
    std::printf ("══ fb621 THE ASSET ENVELOPE ══   mutation: %s\n", mut.isEmpty() ? "(none)" : mut.toRawUTF8());
    const bool noScale = (mut == "noscale"), lenient = (mut == "lenient");

    // [1] the round trip ───────────────────────────────────────────────────────────────────────
    {
        const auto src = tone (2, 48000, 0.7f);
        const auto s = tw::asset::encode (src, 48000.0, 0, "One Shot Killer.wav");
        tw::asset::Envelope e; juce::String err;
        const bool ok = tw::asset::decode (s, e, err);
        const double w = ok ? worstErr (src, e.audio) : 1e9;
        chk (ok && w < 2.0e-7 && e.sampleRate == 48000.0 && e.name == "One Shot Killer.wav" && e.audio.getNumChannels() == 2,
             "[1] A STEREO ONE-SHOT ENCODES AND DECODES TO THE SAME AUDIO — name, rate and channels intact",
             (ok ? "worst sample error " + juce::String (w, 10) + " (" + db (w) + ") · sr " + juce::String (e.sampleRate, 0)
                 + " · ch " + juce::String (e.audio.getNumChannels()) + " · n " + juce::String (e.audio.getNumSamples())
                 + " · name \"" + e.name + "\"" : "decode failed: " + err));
    }

    // [2] peaks above 1.0 — the reason `scale` exists ──────────────────────────────────────────
    {
        const auto src = tone (2, 24000, 1.8f);                     // a float WAV bounced hot
        auto s = tw::asset::encode (src, 44100.0, 0, "Hot");
        if (noScale) s = stripScale (s);
        tw::asset::Envelope e; juce::String err;
        const bool ok = tw::asset::decode (s, e, err);
        const double w = ok ? worstErr (src, e.audio) : 1e9;
        const float pk = ok ? peakOf (e.audio) : 0.0f;
        chk (ok && w < 4.0e-7 && pk > 1.7f,
             "[2] A BUFFER THAT PEAKS ABOVE 1.0 SURVIVES — 24-bit quantisation must not CLIP it",
             (ok ? "source peak " + juce::String (peakOf (src), 4) + " · decoded peak " + juce::String (pk, 4)
                 + " · worst error " + juce::String (w, 10) + " (" + db (w) + ")"
                 + (noScale ? "   (control: the scale field was rewritten to 1.0)" : "") : "decode failed: " + err));
    }

    // [3] the size win — the whole reason the format changed ───────────────────────────────────
    {
        const auto src = tone (2, 96000, 0.8f);
        const auto s = tw::asset::encode (src, 48000.0, 0, {});
        juce::MemoryBlock raw ((size_t) src.getNumSamples() * (size_t) src.getNumChannels() * sizeof (float));
        for (int c = 0; c < src.getNumChannels(); ++c)
            std::memcpy (static_cast<float*> (raw.getData()) + (size_t) c * (size_t) src.getNumSamples(),
                         src.getReadPointer (c), (size_t) src.getNumSamples() * sizeof (float));
        const auto rawB64 = raw.toBase64Encoding();
        const double ratio = (double) s.length() / juce::jmax (1, rawB64.length());
        chk (ratio < 0.60, "[3] FLAC IS MATERIALLY SMALLER THAN THE RAW float32 BASE64 IT REPLACES",
             "envelope " + juce::String (s.length()) + " chars vs raw " + juce::String (rawB64.length())
             + " chars = " + juce::String (100.0 * ratio, 1) + "% (a 1.4 MB wavetable import becomes "
             + juce::String (1.4 * ratio, 2) + " MB)");
    }

    // [4] a wavetable: the frame count is the meaning, not the sample rate ─────────────────────
    {
        const int frames = 64, len = 2048;
        const auto src = tone (1, frames * len, 0.95f, 0.31f);
        const auto s = tw::asset::encode (src, 0.0, frames, "Terra - Bell");
        tw::asset::Envelope e; juce::String err;
        const bool ok = tw::asset::decode (s, e, err);
        chk (ok && e.frames == frames && e.audio.getNumSamples() == frames * len && worstErr (src, e.audio) < 2.0e-7,
             "[4] AN IMPORTED WAVETABLE KEEPS ITS FRAME COUNT (a table has no meaningful sample rate)",
             ok ? "frames " + juce::String (e.frames) + " · samples " + juce::String (e.audio.getNumSamples())
                  + " · worst error " + db (worstErr (src, e.audio)) : "decode failed: " + err);
    }

    // [5] garbage is refused, never crashed on ─────────────────────────────────────────────────
    {
        juce::MemoryBlock legacy ((size_t) 4000 * sizeof (float), true);   // exactly what fv=1 stored
        const juce::StringArray junk { juce::String(), "not base64 at all", legacy.toBase64Encoding(),
                                       "12.AAAA", tw::asset::encode (tone (1, 64, 0.5f), 48000.0, 0, "x").substring (0, 20) };
        int refused = 0; juce::String detail;
        for (const auto& j : junk)
        {
            tw::asset::Envelope e; juce::String err;
            const bool ok = tw::asset::decode (j, e, err);
            if (! ok) ++refused;
            detail << (detail.isEmpty() ? "" : " · ") << (ok ? "ACCEPTED" : "refused(" + err + ")");
        }
        const bool wantAllRefused = ! lenient;
        chk ((refused == junk.size()) == wantAllRefused,
             "[5] GARBAGE IS REFUSED — empty, non-base64, a LEGACY raw-float32 blob, a truncated envelope",
             juce::String (refused) + "/" + juce::String (junk.size()) + " refused: " + detail
             + (lenient ? "   (control expects them accepted)" : ""));
    }

    // [6] factory references — content that ships in the bundle is never embedded ──────────────
    {
        const juce::String rel = "Spectral/Terra Bell 12.flac", h = tw::asset::hashOf ("abc", 3);
        const auto r = tw::asset::makeRef (rel, h);
        juce::String r2, h2;
        const bool parsed = tw::asset::parseRef (r, r2, h2);
        const bool disjoint = tw::asset::isRef (r) && ! tw::asset::isEnvelope (r)
                              && ! tw::asset::isRef (tw::asset::encode (tone (1, 128, 0.5f), 48000.0, 0, "e"));
        const bool hashMoves = tw::asset::hashOf ("abc", 3) != tw::asset::hashOf ("abd", 3);
        chk (parsed && r2 == rel && h2 == h && disjoint && hashMoves,
             "[6] A FACTORY REFERENCE NAMES THE FILE INSTEAD OF EMBEDDING IT — and a changed file is detectable",
             "\"" + r + "\" → rel \"" + r2 + "\" hash " + h2 + " · ref/envelope disjoint " + juce::String ((int) disjoint)
             + " · hash moves on one changed byte " + juce::String ((int) hashMoves));
    }

    // [7] the awkward shapes ───────────────────────────────────────────────────────────────────
    {
        struct Shape { int ch, n; double sr; const char* what; };
        const Shape shapes[] = { { 1, 1, 44100.0, "one mono sample" }, { 1, 4410, 44100.0, "mono 44.1k" },
                                 { 2, 192000, 96000.0, "stereo 96k, two seconds" }, { 4, 1024, 48000.0, "four channels" } };
        int good = 0; juce::String detail;
        for (const auto& sh : shapes)
        {
            const auto src = tone (sh.ch, sh.n, 0.5f);
            tw::asset::Envelope e; juce::String err;
            const bool ok = tw::asset::decode (tw::asset::encode (src, sh.sr, 0, "s"), e, err)
                            && e.audio.getNumChannels() == sh.ch && e.audio.getNumSamples() == sh.n
                            && worstErr (src, e.audio) < 4.0e-7;
            if (ok) ++good; else detail << (detail.isEmpty() ? "" : " · ") << juce::String (sh.what) + " FAILED " + err;
        }
        chk (good == 4, "[7] MONO, ONE SAMPLE, 96 kHz AND FOUR CHANNELS ALL SURVIVE",
             juce::String (good) + "/4 shapes round-tripped" + (detail.isEmpty() ? "" : " · " + detail));
    }

    // [8] deterministic — a save that re-encodes must not dirty a host's state comparison ──────
    {
        const auto src = tone (2, 8000, 0.6f);
        const auto a = tw::asset::encode (src, 48000.0, 0, "d"), b = tw::asset::encode (src, 48000.0, 0, "d");
        chk (a == b && a.isNotEmpty(), "[8] ENCODING IS DETERMINISTIC — the same buffer gives the same bytes twice",
             "identical " + juce::String ((int) (a == b)) + " · " + juce::String (a.length()) + " chars");
    }

    // [9] 🚨 IT TRAVELS — the asset goes into a .terrain and comes back out whole ──────────────
    {
        const auto src = tone (2, 32000, 0.75f);
        const auto blob = tw::asset::encode (src, 48000.0, 0, "Travelling.wav");
        juce::XmlElement xml ("Parameters");
        auto* pe = new juce::XmlElement ("preset");
        pe->setAttribute ("name", "Travels"); pe->setAttribute ("bank", "Cert"); pe->setAttribute ("fv", 2);
        xml.insertChildElement (pe, 0);
        xml.setAttribute ("oscAsset0", blob);

        juce::MemoryBlock chunk; tw::bank::xmlToChunk (xml, chunk);
        juce::MemoryOutputStream out; tw::bank::wrap (out, tw::bank::manifestFromChild (*pe), chunk);
        const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tw_asset_travel.terrain");
        f.replaceWithData (out.getData(), out.getDataSize());

        juce::MemoryBlock file; juce::String manifest, err; juce::MemoryBlock chunk2;
        const bool read = f.loadFileAsData (file) && tw::bank::unwrap (file, manifest, chunk2, err);
        auto back = read ? tw::bank::chunkToXml (chunk2) : nullptr;
        tw::asset::Envelope e; juce::String derr;
        const bool ok = back != nullptr
                        && tw::asset::decode (back->getStringAttribute ("oscAsset0"), e, derr)
                        && worstErr (src, e.audio) < 2.0e-7 && e.name == "Travelling.wav";
        chk (ok, "[9] 🚨 IT TRAVELS — the asset written into a .terrain comes back out of the file whole",
             (read ? "file " + juce::String (f.getSize() / 1024) + " KB · manifest " + manifest.substring (0, 60)
                     + "… · decoded " + juce::String (e.audio.getNumSamples()) + " samples, worst error "
                     + db (worstErr (src, e.audio)) : "could not read the file back: " + err)
             + (derr.isNotEmpty() ? " · " + derr : ""));
        f.deleteFile();
    }

    std::printf ("  %d pass, %d fail\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
