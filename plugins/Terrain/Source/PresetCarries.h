// PresetCarries.h — fb632: WHAT A PRESET CARRIES, COUNTED WHERE IT IS PLAYED.
//
//  Max (2026-09-10): "Michael Myers has no one shots in it but the carry says it has one shots. The
//  only time a carry should even be activated is if there's a Sampler, Resynth, or a Granular engine
//  going on — those are the only samplers we have. We only have three."
//
//  WHAT WENT WRONG. fb618's counter (tiCarriesOf, once a static in PluginProcessor.cpp) counted a
//  one-shot for any oscillator slot whose tree carried oscAsset{o} OR oscSamplePath{o} — and never
//  read SYN_OSC_{o}_ENGINE. The path is a HINT (fb621: "not the audio … the only record of where a
//  one-shot came from"), it is written back on every save while non-empty, and nothing clears it
//  when the engine moves off Sample. Michael Myers' chunk: osc A on HARM (5), no oscAsset0, and
//  oscSamplePath0="AirCans_cleaning-spray-single-l-stereoflac_453179.ogg" from some earlier life of
//  the slot. One hint, no audio, a Harmonic oscillator — and the browser said "1 one-shot".
//
//  THE RULE. An oscillator slot's sample counts only while that oscillator PLAYS the slot: the three
//  samplers — Sample (1), Granular (2), Resynth (3) — and Modal (6) when its exciter source is Auto
//  or Sample, because a one-shot dropped on a Modal oscillator is its strike (ModalEngine.h
//  setExciterSample; the page's own drop law keeps engine 2/3/6 on a drop for exactly this reason).
//  The audio still travels in the state whatever the engine is (Max's "state persists — nothing
//  turns off by itself": switching back to Sample after a reload must find the sample), and the
//  bytes still price everything embedded — only the COUNT follows the engine. And a one-shot is
//  AUDIO: since fb621 the audio travels embedded (oscAsset{o}, written whenever the live slot holds
//  samples), so a slot that carries only a NAME (oscSamplePath{o}) held nothing when it was saved —
//  a name is a hint for the restore, not a one-shot. Michael Myers' hint was worse than empty: a
//  bare pre-fb602 file name nothing could ever open, toasting "1 sample is missing" on every editor
//  open; the processor now stops such a fossil at the door (restoreSampleSlotsFromState clears it,
//  so it never travels again). The layers (the pad sampler's own audio) are unchanged.
//
//  ONE FUNCTION, THREE READERS. The file's manifest (getStateInformation → the <preset> child), the
//  save sheet (getCarriesJson, which now builds the same tree) and the cert
//  (Tests/preset_carries_cert.cpp) all call of(). They cannot disagree.
//
//  THE HEAL. A manifest written before fv 3 carries a count made without the rule. healCatalogue()
//  recounts every such row that claims a one-shot off its own chunk — the number in the file is
//  still the number in the file — corrects the catalogue row, and under the user root writes the
//  corrected manifest back ONCE (fv 3, chunk untouched, mtime put back). Rows with smp 0 are exact
//  already: the new rule is a strict subset of the old one, a count can only go down.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <cmath>
#include "ParameterIDs.hpp"
#include "PresetAssets.h"   // isRef / sizeOf — what a wavetable weighs, and whether it is shipped
#include "PresetBank.h"     // unwrap / chunkToXml / rewriteCarries — the heal

namespace tw::carries
{
// The engines that play an oscillator's sample slot. PluginProcessor.cpp static_asserts these
// against SynthVoice::Engine, so a renumbered enum cannot move the gate silently.
inline constexpr int kEngSample = 1, kEngGranular = 2, kEngResynth = 3, kEngModal = 6;
inline constexpr int kEngineDefault = 0;     // the layout's default (WT) — what an absent PARAM means
inline constexpr int kModalSrcAuto = 0, kModalSrcSample = 3;   // SYN_OSC_x_MODAL_SOURCE: Auto · Noise · Click · Sample
inline constexpr int kFormatVersion = 3;     // fv: 1 paths · 2 FLAC assets (fb621) · 3 engine-gated counts (fb632)
inline constexpr const char* kMemSourcePrefix = "mem:";   // a dropped file's ref (PluginEditor.cpp kTiMemSourcePrefix — asserted equal there)

inline bool isSampleEngine (int e) noexcept { return e == kEngSample || e == kEngGranular || e == kEngResynth; }
inline bool isMemRef (const juce::String& p) { return p.startsWith (kMemSourcePrefix); }

// A choice parameter as the tree carries it: <PARAM id="…" value="5.0"/> — the choice INDEX
// (CLAUDE.md §4), never normalised. Absent → the layout's default.
inline int choiceOf (const juce::ValueTree& s, const char* id, int def)
{
    const auto p = s.getChildWithProperty ("id", juce::var (id));
    if (! p.isValid()) return def;
    return (int) std::lround ((double) p.getProperty ("value", (double) def));
}
inline int oscEngineOf (const juce::ValueTree& s, int o)
{
    static const char* const ids[4] = { ParameterIDs::SYN_OSC_A_ENGINE, ParameterIDs::SYN_OSC_B_ENGINE,
                                        ParameterIDs::SYN_OSC_C_ENGINE, ParameterIDs::SYN_OSC_D_ENGINE };
    return choiceOf (s, ids[juce::jlimit (0, 3, o)], kEngineDefault);
}
inline int modalSourceOf (const juce::ValueTree& s, int o)
{
    static const char* const ids[4] = { ParameterIDs::SYN_OSC_A_MODAL_SOURCE, ParameterIDs::SYN_OSC_B_MODAL_SOURCE,
                                        ParameterIDs::SYN_OSC_C_MODAL_SOURCE, ParameterIDs::SYN_OSC_D_MODAL_SOURCE };
    return choiceOf (s, ids[juce::jlimit (0, 3, o)], kModalSrcAuto);
}
// THE GATE — does oscillator o, as saved, play whatever sits in its sample slot?
inline bool slotPlaysSample (const juce::ValueTree& s, int o)
{
    const int e = oscEngineOf (s, o);
    if (isSampleEngine (e)) return true;
    if (e == kEngModal) { const int src = modalSourceOf (s, o); return src == kModalSrcAuto || src == kModalSrcSample; }
    return false;
}

// fb618 — what a saved tree CARRIES: imported wavetables, one-shots, IRs, flow cards, LFO shapes.
// Read off the tree that getStateInformation just built, never off live members, so the number
// in the file is the number in the file. nodes is the environment seat (0 until the patcher).
inline juce::var of (const juce::ValueTree& s)
{
    int wt = 0, wtf = 0, smp = 0, ir = 0, flow = 0, lfo = 0, nodes = 0;   // fb624 — wtf: of those, how many are SHIPPED
    juce::int64 bWt = 0, bSmp = 0, bIr = 0;
    // fb621 — the embedded asset is the truth. (Its fv=1 fallback — count a bare oscSamplePath — is
    // gone with fb632: a name is not audio, and every file on disk is fv 2 or later.)
    auto bytesOf = [&s] (const char* key, int idx) -> juce::int64
    {
        const auto v = s.getProperty (key + juce::String (idx), "").toString();
        return v.isEmpty() ? 0 : (juce::int64) tw::asset::sizeOf (v);
    };
    for (int o = 0; o < 4; ++o)
    {
        const auto wtA = s.getProperty ("wtAsset" + juce::String (o), "").toString();
        if (wtA.isNotEmpty() || s.getProperty ("wtImportPcm" + juce::String (o), "").toString().isNotEmpty())
        { ++wt;
          // fb624 — Max: "the wavetable is factory though, so you should probably let them know."
          // A reference IS the answer: shipped content is referenced, a user's own table is embedded.
          if (tw::asset::isRef (wtA)) ++wtf;
          bWt += tw::asset::isRef (wtA) ? 0 : bytesOf ("wtAsset", o) + bytesOf ("wtImportPcm", o); }
        // fb632 — THE GATE. A slot's one-shot is its EMBEDDED audio, counted only while the
        // oscillator PLAYS the slot. The bytes price what is embedded, played or not — they answer
        // "how big", the count answers "what plays".
        const bool slotHolds = s.getProperty ("oscAsset" + juce::String (o), "").toString().isNotEmpty();
        if (slotHolds && slotPlaysSample (s, o)) ++smp;
        bSmp += bytesOf ("oscAsset", o);
        if (s.getProperty ("layerAsset" + juce::String (o), "").toString().isNotEmpty())
        { ++smp; bSmp += bytesOf ("layerAsset", o); }
    }
    for (int i = 1; i <= 6; ++i)
        if (s.getProperty ("irAsset" + juce::String (i), "").toString().isNotEmpty()
            || s.getProperty ("convIRRaw" + juce::String (i), "").toString().isNotEmpty())
        { ++ir; bIr += bytesOf ("irAsset", i) + bytesOf ("convIRRaw", i); }
    if (auto layers = s.getChildWithName ("layers"); layers.isValid())
        for (int i = 0; i < layers.getNumChildren(); ++i)
            if (layers.getChild (i).getProperty ("sourcePath", "").toString().isNotEmpty()
                && s.getProperty ("layerAsset" + juce::String (i), "").toString().isEmpty()) ++smp;
    // fb621 — the environment seat: when the patcher writes its graph here, every preset that has
    // one becomes an Environment across the whole browser with no further wiring.
    if (auto pv = juce::JSON::parse (s.getProperty ("patcherJson", "").toString()); pv.isObject())
        if (auto* na = pv.getProperty ("nodes", juce::var()).getArray()) nodes = na->size();
    if (auto v = juce::JSON::parse (s.getProperty ("cardStates", "").toString()); v.getDynamicObject() != nullptr)
        flow = v.getDynamicObject()->getProperties().size();
    if (auto v = juce::JSON::parse (s.getProperty ("lfoShapesJson", "").toString()); v.isObject())
        if (auto* a = v.getProperty ("shapes", juce::var()).getArray()) lfo = a->size();
    auto* o = new juce::DynamicObject();
    o->setProperty ("wt", wt); o->setProperty ("wtf", wtf); o->setProperty ("smp", smp); o->setProperty ("ir", ir);
    o->setProperty ("flow", flow); o->setProperty ("lfo", lfo); o->setProperty ("nodes", nodes);
    o->setProperty ("bytes", (double) (bWt + bSmp + bIr));
    return juce::var (o);
}

// ── THE HEAL ──────────────────────────────────────────────────────────────────────────────────
//  Runs on the catalogue tw::bank::scan just returned. A row saved before fv 3 that claims a one-shot
//  is recounted off its own chunk with of(); the row is corrected in place; under the user root the
//  file's manifest + <preset> child are rewritten once with the corrected count and fv 3 (the chunk
//  is re-serialised and is byte-identical outside that child — the rewriteMeta law; the mtime is put
//  back — a corrected number is not an edit). A file that changed under us between the read and the
//  write is left alone for the next scan. Factory files are corrected in memory only, every scan:
//  ship them at fv 3 (getStateInformation writes fv 3 from here on). Returns how many files were
//  rewritten.
inline int healCatalogue (juce::var& catalogue, const juce::File& userRoot)
{
    int rewritten = 0;
    auto* banks = catalogue.getProperty ("banks", juce::var()).getArray();
    if (banks == nullptr) return 0;
    for (auto& b : *banks)
    {
        const bool factory = (bool) b.getProperty ("factory", false);
        auto* presets = b.getProperty ("presets", juce::var()).getArray();
        if (presets == nullptr) continue;
        for (auto& p : *presets)
        {
            auto* po = p.getDynamicObject();
            if (po == nullptr) continue;
            if ((int) po->getProperty ("fv") >= kFormatVersion) continue;
            const auto c = po->getProperty ("carries");
            if (c.isObject() && (int) c.getProperty ("smp", 0) <= 0) continue;   // a 0 is exact already; no carries at all → recount
            const juce::File f (po->getProperty ("path").toString());
            const auto sizeBefore = f.getSize(); const auto mtimeBefore = f.getLastModificationTime();
            juce::MemoryBlock file; juce::String manifest, err; juce::MemoryBlock chunk;
            if (! f.loadFileAsData (file) || ! tw::bank::unwrap (file, manifest, chunk, err)) continue;
            auto xml = tw::bank::chunkToXml (chunk);
            if (xml == nullptr) continue;
            const auto fixed = of (juce::ValueTree::fromXml (*xml));
            po->setProperty ("carries", fixed);
            if (factory || ! tw::bank::isInside (f, userRoot)) continue;
            if (f.getSize() != sizeBefore || f.getLastModificationTime() != mtimeBefore) continue;   // changed under us
            if (tw::bank::rewriteCarries (f, userRoot, *xml, juce::JSON::toString (fixed, true), kFormatVersion, err))
            { ++rewritten; po->setProperty ("fv", kFormatVersion); }
        }
    }
    return rewritten;
}
}   // namespace tw::carries
