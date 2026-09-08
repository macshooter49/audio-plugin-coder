// ══════════════════════════════════════════════════════════════════════════════════════════════
//  delete_dispatch_cert.cpp — fb602 bug (a): ONE "deletePreset", TWO MEANINGS, ROUTED BY SHAPE.
//
//  index.html asks for the SAME native name for two unrelated jobs:
//     :12207  var delFn = NFX('deletePreset');   delFn(pid, p.name)      -> card/FX preset file
//     :33601  var delFn = NF ('deletePreset');   delFn(pid, p.name)      -> card preset file
//     :17000  await deletePresetFn(idx)                                  -> patch index
//  Registering both C++ bodies under that name in ONE Options chain is the bug: JUCE does
//  copy.nativeFunctions[name] = std::move(callback) behind a jassert compiled out in Release
//  (juce_WebBrowserComponent.h:323-324), so the LAST one silently won and every card ✕ in the
//  docked editor ran static_cast<int>("gli") == 0 into the patch deleter.
//
//  The args arrive as a JSON array parsed to juce::var (juce_WebBrowserComponent.cpp:299-320:
//  FromVar::convert<NativeEvents::Invoke>, then *invocation->params.getArray()), so this cert
//  feeds the dispatcher the REAL thing: JSON::parse of the exact payloads those three JS lines
//  produce. It also greps PluginEditor.cpp for the predicate so the cert cannot drift off it.
//
//  MUTATION CONTROL: TI_CERT_MUTATE=collide restores HEAD's behaviour (patch body wins every
//  call). The card rows MUST flip to CARD-DELETE-LOST.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_core/juce_core.h>
#include <cstdlib>
#include <cstring>

static int passes = 0, fails = 0;
static void chk (bool ok, const juce::String& what)
{ (ok ? passes : fails)++; std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); }

static const char* MUT = std::getenv ("TI_CERT_MUTATE") ? std::getenv ("TI_CERT_MUTATE") : "";
static const bool COLLIDE = std::strcmp (MUT, "collide") == 0;

enum Route { kCard, kPatch };
static const char* routeName (Route r) { return r == kCard ? "card file" : "patch index"; }

// THE PREDICATE UNDER TEST — the one line PluginEditor.cpp's deletePreset now branches on.
static Route dispatch (const juce::Array<juce::var>& args)
{
    if (COLLIDE) return kPatch;                       // HEAD: the patch body was the only survivor
    if (args.size() >= 2 && args[0].isString()) return kCard;
    return kPatch;
}

int main (int argc, char** argv)
{
    const juce::File pe (argc > 1 ? juce::String (argv[1])
                                  : juce::String ("/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/"
                                                  ".worktrees/terrain-instrument/plugins/TerrainInstrument/"
                                                  "Source/PluginEditor.cpp"));
    std::printf ("delete_dispatch_cert (fb602)   TI_CERT_MUTATE=%s\n", MUT[0] ? MUT : "(none - expect GREEN)");
    std::printf ("──────────────────────────────────────────────────────────────────────────────\n");

    // ── the cert measures the SHIPPING predicate, or says so ──────────────────────────────────
    const auto src = pe.loadFileAsString();
    chk (src.isNotEmpty(), "read " + pe.getFullPathName());
    chk (src.contains ("if (args.size() >= 2 && args[0].isString())"),
         "the predicate this cert evaluates is verbatim in PluginEditor.cpp");
    chk (src.contains ("tiDeleteCardPresetNative (args, std::move (complete));"),
         "the card branch calls the ONE courier body (no second copy)");

    // ── the three real payloads, parsed the way the bridge parses them ────────────────────────
    struct Case { const char* json; Route want; const char* who; };
    const Case cases[] = {
        { "[\"gli\",\"Broke\"]",                    kCard,  "index.html:33601  card ✕ (extension card)" },
        { "[\"rvb_convolution\",\"Pocket D\"]",     kCard,  "index.html:12207  FX rack ✕" },
        { "[\"cmp_fet 76\",\"Bus Glue\"]",          kCard,  "index.html:12207  FX ✕, id with a digit" },
        { "[3]",                                    kPatch, "index.html:17000  patch delete, idx 3" },
        { "[0]",                                    kPatch, "index.html:17000  patch delete, idx 0" },
        { "[]",                                     kPatch, "defensive: no args at all" },
    };
    for (const auto& c : cases)
    {
        auto v = juce::JSON::parse (juce::String (c.json));
        juce::Array<juce::var> args;
        if (auto* a = v.getArray()) args = *a;
        const auto got = dispatch (args);
        std::printf ("     %-32s -> %-12s  %s\n", c.json, routeName (got), c.who);
        chk (got == c.want, juce::String (c.json) + " routes to " + routeName (c.want)
             + (got == c.want ? "" : "  <-- CARD-DELETE-LOST"));
    }

    // ── what HEAD actually did to a card call ────────────────────────────────────────────────
    for (const char* id : { "gli", "chop", "rvb_convolution" })
        std::printf ("     HEAD: audioProcessor.deletePreset (static_cast<int> (\"%s\")) == deletePreset(%d)"
                     "  -> out of range, early return, file untouched\n", id, (int) juce::var (juce::String (id)));

    std::printf ("──────────────────────────────────────────────────────────────────────────────\n");
    std::printf ("RESULT: %d pass, %d FAIL  ->  %s\n", passes, fails, fails ? "RED" : "GREEN");
    return fails ? 1 : 0;
}
