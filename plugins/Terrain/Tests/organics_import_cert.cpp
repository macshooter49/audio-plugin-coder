// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp108 — THE USER IMPORT ON THE SHIPPING PROCESSOR (Tests/organics_import_cert.sh; links libTerrain_SharedCode.a).
//
//  The SFZ fixture is imported with the plugin's own code (tw::orgimport, in the SharedCode) into a TEMP library root
//  (a copy of Tests/fixtures/organics), then played through TerrainAudioProcessor::processBlock:
//    [1] organicsIndexJson() lists user.fixture under "User"; organicsSetInstrument(0, "user.fixture") loads it (status ok)
//        and sets ORG_INST to its user number (≥ 2048); note 60 sounds at 261.63 Hz ± 3 ¢, note 67 at 392 Hz ± 3 ¢.
//    [2] state: save → a NEW instance → load → the same id and number, status ok, the same sound (RMS within 1 dB, same pitch).
//    [3] a host that restores only the INT (automation / a preset with no <ORGANICS> string) finds the user id by number.
//    [4] after Remove (deleteUserInstrument) a reload falls back like any missing instrument (the saved family's first installed
//        one, wantedId = the user id, so the page says what was wanted), no crash.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <CoreFoundation/CoreFoundation.h>
#define private public
#define protected public
#include "PluginProcessor.h"
#undef private
#undef protected
#include "organics/OrganicsImport.h"
#include "organics/OrganicsLibrary.h"

static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& d = "")
{ std::printf ("  %s  %s\n", ok ? "PASS " : "FAIL ", what); if (! d.empty()) std::printf ("        %s\n", d.c_str()); ok ? ++npass : ++nfail; std::fflush (stdout); }
static constexpr double SR = 48000.0; static constexpr int BLK = 512;
static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { std::printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}
struct Inst
{
    std::unique_ptr<TerrainAudioProcessor> p; juce::AudioBuffer<float> buf { 2, BLK }; std::vector<float> L;
    Inst() { p = std::make_unique<TerrainAudioProcessor>(); p->setPlayConfigDetails (0, 2, SR, BLK); p->prepareToPlay (SR, BLK); }
    void block (std::initializer_list<std::pair<int,int>> ev = {})
    {
        buf.clear(); juce::MidiBuffer m;
        for (auto e : ev) m.addEvent (e.second ? juce::MidiMessage::noteOn (1, e.first, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, e.first), 0);
        p->processBlock (buf, m);
        L.insert (L.end(), buf.getReadPointer (0), buf.getReadPointer (0) + BLK);
    }
    void tick (int n = 1) { for (int i = 0; i < n; ++i) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.002, false); p->timerCallback(); } }
    void run (double sec) { const int n = (int) std::ceil (sec * SR / BLK); for (int b = 0; b < n; ++b) { block(); if (b & 1) tick(); } }
    juce::String waitLoaded (int osc, double maxSec = 8.0)
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        while (juce::Time::getMillisecondCounterHiRes() - t0 < maxSec * 1000.0)
        {
            tick(); block();
            const auto st = p->organicSlot (osc).status;
            if (st != "loading" && ! p->orgSlot_[osc].publishPending && ! (p->orgMailState_[osc].load() != 0)) { block(); return st; }
        }
        return p->organicSlot (osc).status;
    }
};
static double rmsOf (const std::vector<float>& x, size_t a, size_t b)
{ double s = 0; size_t n = 0; for (size_t i = a; i < b && i < x.size(); ++i) { s += (double) x[i] * x[i]; ++n; } return n ? std::sqrt (s / (double) n) : 0.0; }
static double dbOf (double r) { return 20.0 * std::log10 (std::max (1e-12, r)); }
static double centsOff (const std::vector<float>& x, size_t a, size_t n, double nominal)
{
    if (a + n > x.size()) n = x.size() > a ? x.size() - a : 0;
    if (n < 4096) return 1e9;
    double best = 0, bestF = nominal;
    for (double c = -60.0; c <= 60.0; c += 0.25)
    {
        const double f = nominal * std::pow (2.0, c / 1200.0), w = 2.0 * juce::MathConstants<double>::pi * f / SR;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double h = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) (n - 1));
            re += x[a + i] * h * std::cos (w * (double) i); im -= x[a + i] * h * std::sin (w * (double) i);
        }
        if (re * re + im * im > best) { best = re * re + im * im; bestF = f; }
    }
    return 1200.0 * std::log2 (bestF / nominal);
}
static double mtof (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
static std::string fmt (const char* f, double a = 0, double b = 0, double c = 0, double d = 0) { char s[512]; std::snprintf (s, sizeof s, f, a, b, c, d); return s; }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);
    if (argc < 3) { std::printf ("usage: organics_import_cert <libraryRoot> <fixture.sfz>\n"); return 2; }
    const juce::File root (juce::String::fromUTF8 (argv[1])), sfz (juce::String::fromUTF8 (argv[2]));
    setenv ("TERRAIN_ORGANICS_DIR", root.getFullPathName().toRawUTF8(), 1);
    tw::OrganicsLibrary::get().rescan();
    std::printf ("organics_import_cert — the user import on the shipping processBlock · library %s\n", root.getFullPathName().toRawUTF8());

    tw::orgimport::Request rq; rq.source = sfz; rq.root = root;
    const auto res = tw::orgimport::importSoundFont (rq);
    tw::OrganicsLibrary::get().rescan();
    const juce::String id = res.ids.isEmpty() ? juce::String() : res.ids[0];
    chk (res.ok && id == "user.fixture", "0 the fixture imports (the plugin's own code)", (res.ok ? id : res.error).toStdString());
    const int num = tw::OrganicsLibrary::get().idToIndex (id);

    // ═══ [1] ═══
    std::printf ("\n[1] Osc A plays the user instrument\n");
    juce::MemoryBlock blob; double r1 = 0, c1 = 0;
    {
        Inst a;
        const auto idx = a.p->organicsIndexJson();
        chk (idx.contains ("\"user.fixture\"") && idx.contains ("\"User\""), "1a organicsIndexJson lists it under \"User\"");
        setP (*a.p, ParameterIDs::kOsc_ENABLE[0], 1.f); setP (*a.p, ParameterIDs::kOsc_ENGINE[0], 7.f); a.tick (3);
        a.p->organicsSetInstrument (0, id);
        const auto st = a.waitLoaded (0);
        const int instParam = (int) *a.p->apvts.getRawParameterValue (ParameterIDs::kOsc_ORG_INST[0]);
        chk (st == "ok" && instParam == num && num >= 2048, "1b organicsSetInstrument loads it (status ok) and ORG_INST = its user number",
             ("status " + st).toStdString() + fmt (" · ORG_INST %.0f · number %.0f", instParam, num));
        setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_HUMAN, 0.0f);
        a.L.clear(); a.block ({ {60,1} }); a.run (0.7); a.block ({ {60,0} }); a.run (0.2);
        const size_t s0 = (size_t) (0.25 * SR);
        r1 = rmsOf (a.L, s0, s0 + 16384); c1 = centsOff (a.L, s0, 16384, mtof (60));
        a.L.clear(); a.block ({ {67,1} }); a.run (0.7); a.block ({ {67,0} }); a.run (0.2);
        const double c67 = centsOff (a.L, s0, 16384, mtof (67));
        chk (r1 > 1e-3 && std::fabs (c1) <= 3.0 && std::fabs (c67) <= 3.0, "1c note 60 → 261.63 Hz and note 67 → 392 Hz through processBlock",
             fmt ("rms %.1f dBFS · %+.2f / %+.2f cents", dbOf (r1), c1, c67));
        a.p->getStateInformation (blob);
    }
    // ═══ [2] ═══
    std::printf ("\n[2] State: save → a new instance → load, by id\n");
    {
        const juce::String xml = [&] { auto x = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize()); return x ? x->toString() : juce::String(); }();
        chk (xml.contains ("id=\"user.fixture\""), "2a the state carries the user id", xml.fromFirstOccurrenceOf ("<ORGANICS>", true, false).upToFirstOccurrenceOf ("</ORGANICS>", true, false).toStdString());
        tw::OrganicsLibrary::get().rescan();                                  // a fresh session: the cache is cold
        Inst b; b.p->setStateInformation (blob.getData(), (int) blob.getSize());
        const auto st = b.waitLoaded (0);
        const int instParam = (int) *b.p->apvts.getRawParameterValue (ParameterIDs::kOsc_ORG_INST[0]);
        b.L.clear(); b.block ({ {60,1} }); b.run (0.7);
        const size_t s0 = (size_t) (0.25 * SR);
        const double r2 = rmsOf (b.L, s0, s0 + 16384), c2 = centsOff (b.L, s0, 16384, mtof (60));
        chk (b.p->organicSlot (0).id == id && st == "ok" && instParam == num, "2b the new instance holds the same id and number (status ok)",
             ("id " + b.p->organicSlot (0).id + " · status " + st).toStdString() + fmt (" · ORG_INST %.0f", instParam));
        chk (std::fabs (dbOf (r2) - dbOf (r1)) < 1.0 && std::fabs (c2) <= 3.0, "2c ...and the same sound", fmt ("rms %.2f → %.2f dBFS · %+.2f cents", dbOf (r1), dbOf (r2), c2));
    }
    // ═══ [3] ═══
    std::printf ("\n[3] The int alone (host automation / a preset without the id string)\n");
    {
        Inst c;
        setP (*c.p, ParameterIDs::kOsc_ENABLE[0], 1.f); setP (*c.p, ParameterIDs::kOsc_ENGINE[0], 7.f); c.tick (3);
        c.p->organicsSetInstrument (0, "test.sine"); c.waitLoaded (0);
        setP (*c.p, ParameterIDs::kOsc_ORG_INST[0], (float) num);
        c.tick (4); const auto st = c.waitLoaded (0);
        chk (c.p->organicSlot (0).id == id && st == "ok", "3 ORG_INST = the user number selects the user instrument",
             ("id " + c.p->organicSlot (0).id + " · status " + st).toStdString());
    }
    // ═══ [4] ═══
    std::printf ("\n[4] Removed, then a session that names it\n");
    {
        juce::String err;
        const bool ok = tw::orgimport::deleteUserInstrument (root, id, &err);
        tw::OrganicsLibrary::get().rescan();
        Inst d; d.p->setStateInformation (blob.getData(), (int) blob.getSize());
        const auto st = d.waitLoaded (0);
        d.L.clear(); d.block ({ {60,1} }); d.run (0.4);
        const auto& sl = d.p->orgSlot_[0];
        // contract §7 / tp108: a missing user instrument falls back like any missing one — the saved family's first installed
        // instrument (the fixture library has test.sine, family "grand"), and the page is told what was wanted
        chk (ok && sl.wantedId == id && sl.id != id && (st == "missing" || st == "ok"), "4 a removed user instrument falls back like any missing one (wantedId says so), no crash",
             ("status " + st + " · now " + sl.id + " · wanted " + sl.wantedId + " " + err).toStdString());
    }
    std::printf ("\norganics_import_cert: %d passed, %d failed\n", npass, nfail);
    tw::orgimport::cancelAll();
    return nfail == 0 ? 0 : 1;
}
