// ══════════════════════════════════════════════════════════════════════════════════════════════
//  state_holes_au.cpp — fb602 · THE TWO THINGS getStateInformation NEVER WROTE DOWN.
//
//    c++ -std=c++17 -O2 -I Tests -I Tests/shim -I Source Tests/state_holes_au.cpp \
//        -framework Accelerate -framework AudioToolbox -framework CoreFoundation -o /tmp/sh && /tmp/sh
//
//  HOLE 1 — cardStates_. index.html:33710 calls this map "ONE truth" for the FLOW extension cards'
//  multi-slot chain (arp / gli / rbn / chop / crv / lfo). Before fb602,
//  `grep cardStates_ PluginProcessor.cpp` returned NOTHING: the map survived pop-out, dock-back and
//  an editor reopen, and was destroyed by every DAW project reload.
//
//  HOLE 2 — the user convolution IR AUDIO. loadConvIRFromFile keeps only f.getFileName(), so there
//  is no path to reload from; an un-embedded IR is a LOST IR.
//
//  THE NASTY ONE IS BAR [2]. A restore that CLEARS the card chain on a blob that has no cardStates
//  property is strictly worse than never having saved it: every pre-fb602 project the owner opens
//  would wipe the chains the editor had already published. STATE PERSISTS — "" is a NO-OP, never a
//  clear. That is the bar this file exists for.
//
//  This drives the INSTALLED AU through kAudioUnitProperty_ClassInfo, so every round trip below is
//  the real getStateInformation / setStateInformation pair, not a transcription of them.
//
//  ⚠️⚠️ THE TRAP THIS FILE ALMOST FELL INTO — READ BEFORE ADDING A BAR.
//  A plain "put X in the blob, read X back out" round trip PASSES ON THE BROKEN BUILD. Measured:
//  the first draft of bars [1] and [3] were GREEN against the fb601 binary, whose `strings` output
//  contains neither "cardStates" nor "convIRRaw1". The reason is that setStateInformation ends in
//  apvts.replaceState (newState) and getStateInformation begins with apvts.copyState() — so a
//  ValueTree root attribute NOBODY READS is carried straight through, verbatim, forever. That is a
//  green bar that cannot go red.
//  The fix is a DISCRIMINATOR: feed the blob something only the real code path can change.
//    · cardStates  — keys out of order and an extra space. cardStates_ is a std::map re-serialised
//                    by juce::JSON, so a real round trip comes back SORTED and unspaced; the
//                    pass-through comes back exactly as sent.
//    · convIRRaw   — a LIE in "n" (1 instead of 2048) and NO "R" at all. getConvIRRawJson recomputes
//                    n from convUserIrL_.size() and setConvIRRawFromJson duplicates L into R when R
//                    is missing, so a real round trip corrects both; the pass-through cannot.
//  Every bar below either uses a discriminator or is a second push that strips the property.
//
//  MUTATION CONTROL:  TI_HOLES_MUT=1 saves the patch WITHOUT the two properties — the pre-fb602
//  hole, re-created from the input side. Bars [1] [3] [4] MUST go red.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "au_state_blob.h"
#include <algorithm>

static const bool MUT = (getenv ("TI_HOLES_MUT") != nullptr && std::string (getenv ("TI_HOLES_MUT")) == "1");

// ── juce::MemoryBlock::toBase64Encoding, reimplemented EXACTLY ────────────────────────────────
//    JUCE's is NOT standard base64: "<decimal byte count>." then 6-bit groups read LSB-FIRST out
//    of the byte stream against the table below (juce_MemoryBlock.cpp:366-384, getBitRange :283).
//    A standard encoder here would decode to garbage and bar [3] would pass anyway, because
//    fromBase64Encoding only needs the dot. Bar [3] therefore compares the string the plugin
//    RE-EMITS (JUCE's own encoder, over the samples it retained) with the one sent in — which
//    certifies this reimplementation and the retention in one comparison.
static std::string juceB64 (const void* data, size_t size)
{
    static const char T[] = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
    const unsigned char* p = (const unsigned char*) data;
    auto bits = [&] (size_t start, size_t num)
    {
        int res = 0; size_t byte = start >> 3, off = start & 7, so = 0;
        while (num > 0 && byte < size)
        {
            const size_t n = std::min (num, (size_t) 8 - off);
            const int mask = (0xff >> (8 - n)) << off;
            res |= ((p[byte] & mask) >> off) << so;
            so += n; num -= n; ++byte; off = 0;
        }
        return res;
    };
    const size_t numChars = ((size << 3) + 5) / 6;
    std::string out = std::to_string (size); out += '.';
    for (size_t i = 0; i < numChars; ++i) out += T[bits (i * 6, 6)];
    return out;
}

// a small deterministic "IR": an exponentially-decaying noise burst, 2048 frames, L != R.
static void mkIr (std::vector<float>& L, std::vector<float>& R, int n)
{
    L.resize ((size_t) n); R.resize ((size_t) n);
    unsigned s = 12345u;
    for (int i = 0; i < n; ++i)
    {
        s = s * 1664525u + 1013904223u;
        const float w = ((float) ((s >> 9) & 0xffff) / 32768.0f - 1.0f) * std::exp (-3.0f * (float) i / (float) n);
        L[(size_t) i] = w; R[(size_t) i] = -0.5f * w;
    }
}

static bool contains (const std::string& h, const std::string& n) { return h.find (n) != std::string::npos; }

int main()
{
    std::printf ("\n══ fb602 · THE TWO STATE HOLES — cardStates_ AND THE USER CONVOLUTION IR ══\n");
    std::printf ("   MUTATION CONTROL TI_HOLES_MUT=%s\n\n", MUT ? "1 (ACTIVE — [1] [3] [4] MUST go RED)" : "0 (inactive)");

    AU a; if (! a.open()) { std::printf ("  !! cannot open the AU\n"); return 2; }
    std::string why; const std::string base = a.readXml (why);
    if (base.empty()) { std::printf ("  !! no blob: %s\n", why.c_str()); return 2; }

    // ── [0] A VIRGIN PATCH PAYS NOTHING. Both writes are empty-guarded; this is that guard. ────
    {
        const bool none = ! hasProperty (base, "cardStates")
                       && ! hasProperty (base, "convIRRaw1") && ! hasProperty (base, "convIRRaw2")
                       && ! hasProperty (base, "convIRRaw3") && ! hasProperty (base, "convIRRaw4")
                       && ! hasProperty (base, "convIRRaw5") && ! hasProperty (base, "convIRRaw6");
        chk (none, "[0] EMPTY-GUARDED — a patch with no cards and no user IR gains ZERO bytes",
             "virgin blob " + std::to_string (base.size()) + " bytes, and it carries none of "
             "cardStates / convIRRaw1..6");
    }

    // ── the card chain a real FLOW session would have saved, DELIBERATELY MIS-SHAPED ─────────
    //    Values are opaque strings the processor never parses (PluginProcessor.cpp:14453). Keys
    //    are in REVERSE order with a stray space after the comma: cardStates_ is a
    //    std::map<juce::String, juce::String>, so getCardStatesJson can only ever emit them
    //    SORTED and unspaced. Getting the sent bytes back = pass-through = the hole is still open.
    const std::string cardArpEsc = "{\\\"slots\\\":[{\\\"n\\\":\\\"up\\\",\\\"v\\\":0.25}],\\\"on\\\":true}";
    const std::string cardGliEsc = "{\\\"slots\\\":[{\\\"n\\\":\\\"stut\\\",\\\"v\\\":1}],\\\"on\\\":false}";
    const std::string cardsSent      = "{\"gli\":\"" + cardGliEsc + "\", \"arp\":\"" + cardArpEsc + "\"}";
    const std::string cardsCanonical = "{\"arp\":\"" + cardArpEsc + "\",\"gli\":\"" + cardGliEsc + "\"}";

    std::vector<float> L, R; mkIr (L, R, 2048);
    const std::string b64L = juceB64 (L.data(), L.size() * sizeof (float));
    const std::string b64R = juceB64 (R.data(), R.size() * sizeof (float));
    // DELIBERATELY MIS-SHAPED: n lies (1, not 2048) and there is NO "R". Only the real
    // setConvIRRawFromJson -> convUserIrL_/R_ -> getConvIRRawJson path can correct either.
    const std::string irSent = "{\"name\":\"fb602 cert IR\",\"n\":1,\"L\":\"" + b64L + "\"}";

    // ── the save→destroy→restore, for real ───────────────────────────────────────────────────
    std::string saved = base;
    if (! MUT)
    {
        setRootAttr (saved, "cardStates", cardsSent);
        setRootAttr (saved, "convIRRaw1", irSent);      // Reverb 1 — has a live engine
        setRootAttr (saved, "convIRRaw4", irSent);      // Reverb 4 — POOLED, engine still nullptr here
    }
    a.close();                                          // ◀ destroy

    std::string back;                                   // what the reloaded instance saves next time
    AU b; if (! b.open()) return 2;                     // ◀ recreate — a fresh processor
    b.writeXml (saved);                                 // ◀ restore, through setStateInformation
    { std::string w; back = b.readXml (w); }

    // ── [1] HOLE 1: the card chain went THROUGH cardStates_, not around it ───────────────────
    {
        const std::string got = getRootAttr (back, "cardStates");
        const bool canonical  = (got == cardsCanonical);
        const bool passthru   = (got == cardsSent);
        chk (canonical,
             "[1] cardStates ROUND-TRIPS THROUGH cardStates_ (HOLE 1) — sorted + unspaced, not echoed",
             std::string (got.empty() ? "<ABSENT — the pre-fb602 hole>"
                                      : passthru ? "<ECHOED BACK BYTE-FOR-BYTE — this is the ValueTree "
                                                   "pass-through, NOT a restore: nothing read the map>"
                                                 : "canonical")
               + "\n           sent      " + cardsSent.substr (0, 150)
               + "\n           expected  " + cardsCanonical.substr (0, 150)
               + "\n           got       " + (got.empty() ? std::string ("(nothing)") : got.substr (0, 150)));
    }

    // ── [2] THE NASTY ONE. An empty / absent cardStates must NOT clear what is already there. ─
    //        This is the bar that stops fb602 from being WORSE than the bug it fixes.
    {
        std::string second = base;                       // a PRE-fb602 blob: no cardStates at all
        setParam (second, "SYN_OSC_A_LEVEL", 0.4321);    // ...but it is a real, different patch
        b.writeXml (second);
        std::string w; const std::string after = b.readXml (w);
        const std::string got = getRootAttr (after, "cardStates");
        const double lvl = getParam (after, "SYN_OSC_A_LEVEL");
        chk (MUT ? true : (! got.empty() && contains (got, "arp") && contains (got, "gli")),
             "[2] A BLOB WITH NO cardStates IS A NO-OP, NEVER A CLEAR  ◀── worse-than-the-bug bar",
             "pushed a pre-fb602 blob (OSC A Level -> " + std::to_string (lvl)
               + ", proving the restore ran); cardStates after = "
               + (got.empty() ? std::string ("<WIPED — this would destroy every FLOW chain on a legacy project>")
                              : got.substr (0, 120))
               + (MUT ? "   [mutated: nothing was stored, bar not meaningful]" : ""));

        // and an explicitly EMPTY string is the same no-op
        std::string third = base; setRootAttr (third, "cardStates", "");
        b.writeXml (third);
        std::string w2; const std::string after2 = b.readXml (w2);
        const std::string got2 = getRootAttr (after2, "cardStates");
        chk (MUT ? true : (! got2.empty() && contains (got2, "arp")),
             "[2b] cardStates=\"\" IS ALSO A NO-OP (setCardStatesFromJson's first line)",
             "cardStates after an explicit empty string = "
               + (got2.empty() ? std::string ("<WIPED>") : got2.substr (0, 120)));
    }

    // ── [3] HOLE 2: the IR AUDIO went THROUGH convUserIrL_/R_ ───────────────────────────────
    {
        const std::string got = getRootAttr (back, "convIRRaw1");
        const bool nameOk  = contains (got, "fb602 cert IR");
        const bool lOk     = contains (got, b64L);
        const bool nFixed  = contains (got, "\"n\":" + std::to_string (L.size()));   // the lie was corrected
        const bool rMade   = contains (got, "\"R\":\"" + b64L + "\"");             // mono -> R duplicated from L
        const bool passthru = (got == irSent);
        chk (! got.empty() && nameOk && lOk && nFixed && rMade,
             "[3] convIRRaw1 ROUND-TRIPS THROUGH convUserIrL_ (HOLE 2) — n recomputed, R rebuilt",
             (passthru ? std::string ("<ECHOED BACK BYTE-FOR-BYTE — ValueTree pass-through, no decode happened>")
                       : got.empty() ? std::string ("<ABSENT — the pre-fb602 hole>")
                                     : std::to_string (got.size()) + " chars re-emitted")
               + "  ·  name " + (nameOk ? "ok" : "LOST")
               + "  ·  L base64 " + (lOk ? "byte-identical" : "DIFFERS")
               + "  ·  sent n=1, got n=" + std::to_string (L.size()) + "? " + (nFixed ? "YES" : "NO — still 1")
               + "  ·  sent no R, R rebuilt from L? " + (rMade ? "YES" : "NO")
               + "  ·  (L b64 = " + std::to_string (b64L.size()) + " chars for "
               + std::to_string (L.size() * sizeof (float)) + " raw bytes)");
    }

    // ── [4] THE POOLED SLOT. setStateInformation runs BEFORE buildPendingReverbEngines, so
    //        convEngineFor(4) is still nullptr; the old `if (eng == nullptr) return;` made a saved
    //        IR on Reverb 2..6 evaporate silently. Decode + RETAIN must happen regardless.
    {
        const std::string got = getRootAttr (back, "convIRRaw4");
        const bool real = contains (got, b64L) && contains (got, "\"n\":" + std::to_string (L.size()));
        chk (real,
             "[4] A POOLED SLOT (Reverb 4) RETAINS ITS IR EVEN WITH NO ENGINE YET",
             got.empty() ? std::string ("<ABSENT — the null-engine early-return threw it away>")
                         : got == irSent ? std::string ("<ECHOED — pass-through, the decode never ran>")
                                         : "re-emitted " + std::to_string (got.size()) + " chars, n recomputed, L byte-identical");
    }

    // ── [5] AN ABSENT convIRRaw LEAVES THE RETAINED IR ALONE (same law as [2]) ───────────────
    {
        std::string fourth = base;                       // no convIRRaw at all
        b.writeXml (fourth);
        std::string w; const std::string after = b.readXml (w);
        const std::string got = getRootAttr (after, "convIRRaw1");
        chk (MUT ? true : (! got.empty() && contains (got, b64L)),
             "[5] AN ABSENT convIRRaw IS A NO-OP — a legacy patch keeps the IR already loaded",
             got.empty() ? std::string ("<WIPED — the property is gone, so it was only ever the ValueTree "
                                        "echo and the retained samples do not exist>")
                         : "still there, " + std::to_string (got.size()) + " chars re-emitted from convUserIrL_");
    }

    // ── [6] THE COST, MEASURED. The comment at PluginProcessor.cpp:14550 states a size; state it
    //        back with a number from THIS run rather than trusting the prose.
    {
        const size_t grew = back.size() > base.size() ? back.size() - base.size() : 0;
        const size_t cards = getRootAttr (back, "cardStates").size();
        const size_t ir1 = getRootAttr (back, "convIRRaw1").size();
        // ⚠️ On a build where [3] is RED this measures the ECHO, not a real encode — the ratio is
        //    only meaningful once [3] is green. Printed either way, labelled either way.
        std::printf ("  ....  [6] COST (report, %s) — blob %zu -> %zu bytes, +%zu\n"
                     "        cardStates %zu chars · convIRRaw1 %zu chars\n"
                     "        ⇒ %.2f base64 chars per raw byte of float32 L+R; extrapolated to the\n"
                     "          6.14 s load cap (2 x 288000 x 4 B) that is %.2f MB for one loaded slot\n",
                     fail ? "UNRELIABLE — a hole bar is red, this is the ValueTree echo" : "real encode",
                     base.size(), back.size(), grew, cards, ir1,
                     ir1 / (double) (2 * 2048 * sizeof (float)),
                     (ir1 / (double) (2 * 2048 * sizeof (float))) * (2.0 * 288000 * 4) / 1048576.0);
    }

    b.close();
    return summary();
}
