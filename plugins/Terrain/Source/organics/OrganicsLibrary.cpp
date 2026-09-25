// OrganicsLibrary.cpp — .torg v1 loader, the refcounted instrument cache (5 s lazy release) and the
// deferred-release queue that keeps every instrument free off the audio thread.
//
// Threads:
//   • message thread  — request(), index(), rescan(), idToIndex()/indexToId(), residentCount()/Bytes().
//   • "Organics" thread — started lazily by the FIRST request() (an unused library starts no thread):
//                          loads (JSON + FLAC → int16), drains the deferred-release queue, frees idle
//                          instruments 5 s after their last user let go. Callbacks hop to the message thread.
//   • audio thread     — only org::deferRelease() (lock-free, fixed capacity, no allocation).
#include "OrganicsLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace tw
{
    //==============================================================================================
    //  Deferred release: MPSC ring of shared_ptr slots. Constant-initialised (safe from any thread,
    //  before or after the library object exists).
    //==============================================================================================
    namespace
    {
        struct ReleaseQueue
        {
            static constexpr int kSlots = 2048;
            std::shared_ptr<const OrganicInstrument> slot[kSlots];
            std::atomic<uint8_t> state[kSlots] {};            // 0 empty · 1 writing · 2 full
            std::atomic<uint32_t> ticket { 0 };

            bool push (std::shared_ptr<const OrganicInstrument>& p) noexcept
            {
                for (int tries = 0; tries < 64; ++tries)
                {
                    const uint32_t i = ticket.fetch_add (1, std::memory_order_relaxed) % (uint32_t) kSlots;
                    uint8_t expect = 0;
                    if (state[i].compare_exchange_strong (expect, 1, std::memory_order_acquire))
                    {
                        slot[i] = std::move (p);                  // moving into an EMPTY slot: no free here
                        state[i].store (2, std::memory_order_release);
                        return true;
                    }
                }
                return false;
            }

            void drain()
            {
                for (int i = 0; i < kSlots; ++i)
                {
                    if (state[i].load (std::memory_order_acquire) != 2) continue;
                    std::shared_ptr<const OrganicInstrument> dead = std::move (slot[i]);
                    state[i].store (0, std::memory_order_release);
                    dead.reset();                                 // the (possibly last) release happens HERE
                }
            }
        };
        ReleaseQueue gReleaseQueue;
    }

    bool org::deferRelease (std::shared_ptr<const OrganicInstrument>& p) noexcept
    {
        if (p == nullptr) return true;
        return gReleaseQueue.push (p);
    }
    void org::drainDeferredReleases() { gReleaseQueue.drain(); }

    //==============================================================================================
    //  Loader
    //==============================================================================================
    struct OrganicLoader
    {
        static bool fail (juce::String* e, const juce::String& why) { if (e) *e = why; return false; }

        static org::Kind kindOf (const juce::String& s)
        {
            if (s == "release") return org::Kind::Release;
            if (s == "noise")   return org::Kind::Noise;
            return org::Kind::Attack;
        }
        static org::Loop loopOf (const juce::String& s)
        {
            if (s == "one_shot")                           return org::Loop::OneShot;
            if (s == "continuous" || s == "loop_continuous") return org::Loop::Continuous;
            if (s == "sustain" || s == "loop_sustain")     return org::Loop::Sustain;
            return org::Loop::NoLoop;
        }
        static double num (const juce::var& o, const char* k, double def)
        {
            const auto& v = o[k];
            return (v.isInt() || v.isInt64() || v.isDouble() || v.isBool()) ? (double) v : def;
        }

        static bool decodeSample (juce::AudioFormatManager& fm, const juce::File& f, org::Sample& out, juce::String* err)
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (r == nullptr) return fail (err, "cannot decode " + f.getFileName());
            const int ch = (int) juce::jlimit<unsigned> (1, 2, r->numChannels);
            const int64_t n = r->lengthInSamples;
            if (n < 2 || n > (int64_t) 1 << 30) return fail (err, "bad length " + f.getFileName());
            out.channels = ch; out.frames = n; out.sampleRate = r->sampleRate > 1000.0 ? r->sampleRate : 48000.0;
            out.pcm.assign ((size_t) ((n + 2 * org::kPad) * ch), 0);
            constexpr int kChunk = 32768;
            std::vector<int> b0 (kChunk), b1 (kChunk);
            int* dest[2] = { b0.data(), b1.data() };
            int16_t* d = out.pcm.data() + (size_t) (org::kPad * ch);
            for (int64_t pos = 0; pos < n; pos += kChunk)
            {
                const int m = (int) std::min<int64_t> (kChunk, n - pos);
                if (! r->read (dest, ch, pos, m, true)) return fail (err, "read error " + f.getFileName());
                for (int i = 0; i < m; ++i)
                    for (int c = 0; c < ch; ++c)
                        d[(size_t) ((pos + i) * ch + c)] = (int16_t) (dest[c][i] >> 16);
            }
            // envelope table: RMS over both channels in a CENTRED 4096-frame window (≥ 2 periods down to 23 Hz,
            // so a low note's table does not ripple with its own waveform), one value per kEnvWin frames, dB re FS.
            {
                std::vector<double> pre ((size_t) n + 1, 0.0);
                for (int64_t i = 0; i < n; ++i)
                {
                    double e = 0.0;
                    for (int c = 0; c < ch; ++c) { const double x = d[(size_t) (i * ch + c)] * (1.0 / 32768.0); e += x * x; }
                    pre[(size_t) i + 1] = pre[(size_t) i] + e / ch;
                }
                constexpr int64_t kHalf = 2048;
                const int64_t nw = (n + org::kEnvWin - 1) / org::kEnvWin;
                out.envDb.resize ((size_t) nw);
                for (int64_t w = 0; w < nw; ++w)
                {
                    const int64_t c0 = w * org::kEnvWin + org::kEnvWin / 2;
                    const int64_t a0 = std::max<int64_t> (0, c0 - kHalf), a1 = std::min<int64_t> (n, c0 + kHalf);
                    const double ms = (pre[(size_t) a1] - pre[(size_t) a0]) / (double) std::max<int64_t> (1, a1 - a0);
                    out.envDb[(size_t) w] = (float) (10.0 * std::log10 (ms + 1.0e-14));
                }
            }
            return true;
        }

        static std::shared_ptr<OrganicInstrument> load (const juce::File& folder, juce::String* err)
        {
            auto inst = std::make_shared<OrganicInstrument>();
            if (! build (*inst, folder, err)) return nullptr;
            return inst;
        }

        static bool build (OrganicInstrument& I, const juce::File& folder, juce::String* err)
        {
            const auto mapFile = folder.getChildFile ("map.json");
            if (! mapFile.existsAsFile()) return fail (err, "no map.json in " + folder.getFullPathName());
            juce::var m;
            if (juce::JSON::parse (mapFile.loadFileAsString(), m).failed() || ! m.isObject())
                return fail (err, "map.json is not valid JSON");
            if ((int) num (m, "torg", 1) != 1) return fail (err, "unsupported torg version");
            const auto* regs = m["regions"].getArray();
            const auto* smps = m["samples"].getArray();
            if (regs == nullptr || smps == nullptr || regs->isEmpty() || smps->isEmpty())
                return fail (err, "map.json lacks regions/samples");
            if (regs->size() > 65000) return fail (err, "too many regions");

            I.id = m["id"].toString(); I.name = m["name"].toString(); I.family = m["family"].toString();
            I.category = m["category"].toString(); I.credit = m["credit"].toString();
            I.polyMax = (int) num (m, "polyMax", 32);
            if (const auto* a = m["artics"].getArray()) for (auto& s : *a) I.artics.add (s.toString());
            if (I.artics.isEmpty()) I.artics.add ("Default");
            I.numArtics = juce::jlimit (1, 8, I.artics.size());

            // samples
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            I.samples.resize ((size_t) smps->size());
            for (int i = 0; i < smps->size(); ++i)
            {
                const auto name = (*smps)[i].toString();
                if (name.isEmpty() || name.contains ("..") || name.containsAnyOf ("/\\")) return fail (err, "bad sample name");
                if (! decodeSample (fm, folder.getChildFile ("samples").getChildFile (name), I.samples[(size_t) i], err))
                    return false;
            }

            // regions
            I.regions.resize ((size_t) regs->size());
            for (int i = 0; i < regs->size(); ++i)
            {
                const auto& o = (*regs)[i];
                if (! o.isObject()) return fail (err, "region is not an object");
                auto& R = I.regions[(size_t) i];
                R.artic = (int) num (o, "a", 0);
                R.smp   = (int) num (o, "smp", -1);
                if (R.smp < 0 || R.smp >= (int) I.samples.size()) return fail (err, "region.smp out of range");
                if (R.artic < 0 || R.artic >= I.numArtics) return fail (err, "region.a out of range");
                const auto& S = I.samples[(size_t) R.smp];
                R.kind = kindOf (o["kind"].toString());
                R.lk = juce::jlimit (0, 127, (int) num (o, "lk", 0));    R.hk = juce::jlimit (0, 127, (int) num (o, "hk", 127));
                R.lv = juce::jlimit (0, 127, (int) num (o, "lv", 1));    R.hv = juce::jlimit (0, 127, (int) num (o, "hv", 127));
                R.xfLo = juce::jlimit (R.lv, R.hv, (int) num (o, "xfLo", R.lv));
                R.xfHi = juce::jlimit (R.xfLo, R.hv, (int) num (o, "xfHi", R.hv));
                if (R.lk > R.hk || R.lv > R.hv) return fail (err, "region key/vel range inverted");
                R.root = juce::jlimit (0, 127, (int) num (o, "root", 60));
                R.cents = (float) num (o, "cents", 0); R.gainDb = (float) num (o, "gainDb", 0);
                R.gainNorm = (float) num (o, "gainNorm", 1); R.pan = (float) juce::jlimit (-100.0, 100.0, num (o, "pan", 0));
                R.start = juce::jlimit<int64_t> (0, S.frames - 2, (int64_t) num (o, "start", 0));
                R.end   = juce::jlimit<int64_t> (R.start + 2, S.frames, (int64_t) num (o, "end", (double) S.frames));
                R.onset = juce::jlimit<int64_t> (R.start, R.end - 1, (int64_t) num (o, "onset", (double) R.start));
                R.loop  = loopOf (o["loop"].toString());
                R.ls = (int64_t) num (o, "ls", 0); R.le = (int64_t) num (o, "le", 0); R.xf = (int64_t) num (o, "xf", 0);
                if (R.loop == org::Loop::Continuous || R.loop == org::Loop::Sustain)
                {
                    R.le = std::min (R.le, R.end);
                    if (R.ls < R.start || R.le <= R.ls + 8) { R.loop = org::Loop::NoLoop; R.ls = R.le = 0; }
                }
                R.xf = juce::jlimit<int64_t> (0, std::max<int64_t> (0, (R.le - R.ls) / 2), R.xf);
                R.tailLs = (int64_t) num (o, "tailLs", 0); R.tailLe = (int64_t) num (o, "tailLe", 0);
                if (R.tailLe > R.end || R.tailLs < R.start || R.tailLe <= R.tailLs + 16) R.tailLs = R.tailLe = 0;
                if (const auto* rr = o["rr"].getArray(); rr != nullptr && rr->size() >= 2)
                { R.rrPos = std::max (0, (int) (*rr)[0]); R.rrLen = std::max (1, (int) (*rr)[1]); R.rrPos %= R.rrLen; }
                if (const auto* rd = o["rand"].getArray(); rd != nullptr && rd->size() >= 2)
                { R.randLo = (float) (double) (*rd)[0]; R.randHi = (float) (double) (*rd)[1]; }
                R.grp = (int) num (o, "grp", 0); R.offBy = (int) num (o, "offBy", 0);
                R.offFast = o["offMode"].toString() == "fast";
                const auto& e = o["env"];
                R.envA = (float) num (e, "a", 0); R.envH = (float) num (e, "h", 0); R.envD = (float) num (e, "d", 0);
                R.envS = (float) num (e, "s", 1); R.envR = (float) num (e, "r", 0.25);
                R.rtDecay = (float) std::max (0.0, num (o, "rtDecay", 0));
                R.trigOn  = o["trig"].toString() == "on";                           // tp105 (missing = "off")
                R.tfix    = (float) juce::jlimit (-100.0, 100.0, num (o, "tfix", 0)); // tp105 cents (Tuning = Equal applies it)
                // velCurve → 128-entry table (piecewise linear; default linear)
                std::vector<std::pair<float, float>> pts;
                if (const auto* vc = o["velCurve"].getArray())
                    for (auto& p : *vc)
                        if (const auto* pa = p.getArray(); pa != nullptr && pa->size() >= 2)
                            pts.push_back ({ (float) (double) (*pa)[0], (float) (double) (*pa)[1] });
                std::sort (pts.begin(), pts.end());
                if (pts.size() < 2) pts = { { 0.f, 0.f }, { 127.f, 1.f } };
                for (int v = 0; v < 128; ++v)
                {
                    float y = pts.front().second;
                    if (v >= pts.back().first) y = pts.back().second;
                    else for (size_t k = 1; k < pts.size(); ++k)
                        if (v <= pts[k].first)
                        {
                            const float x0 = pts[k - 1].first, x1 = pts[k].first;
                            const float t = x1 > x0 ? ((float) v - x0) / (x1 - x0) : 1.f;
                            y = pts[k - 1].second + t * (pts[k].second - pts[k - 1].second);
                            break;
                        }
                    R.velCurve[v] = juce::jlimit (0.f, 4.f, y);
                }
                // Level = gainDb × gainNorm × velCurve(vel). gainNorm already carries the 50 % natural-loudness
                // restore (the compiler's contract), so it is applied linearly.
                R.staticGain = (float) (std::pow (10.0, R.gainDb / 20.0) * std::max (0.0f, R.gainNorm));
                R.isRR = R.rrLen > 1 || R.randLo > 0.0001f || R.randHi < 0.9999f;
                if (R.kind == org::Kind::Noise)   I.hasNoise = true;
                if (R.kind == org::Kind::Release) I.hasRelease = true;
            }

            deriveVelocityWindows (I);
            deriveGroups (I);
            deriveSustain (I);
            buildLookup (I);

            I.rrSeq_.reset      (new std::atomic<uint32_t>[(size_t) I.numArtics * 128]());
            I.rrLast_.reset     (new std::atomic<int32_t> [(size_t) I.numArtics * 128]());
            I.fakeLast_.reset   (new std::atomic<int32_t> [128]());
            I.noiseLast_.reset  (new std::atomic<int32_t> [(size_t) I.numArtics * 2 * 128]());   // tp107
            I.groupEpoch_.reset (new std::atomic<uint32_t>[(size_t) std::max (1, I.numGroups)]());
            I.resetPerformanceState();

            int64_t b = 0;
            for (auto& s : I.samples) b += (int64_t) (s.pcm.size() * sizeof (int16_t) + s.envDb.size() * sizeof (float));
            b += (int64_t) (I.regions.size() * sizeof (org::Region) + I.spans.size() * sizeof (org::Span)
                            + I.lists.size() * sizeof (uint16_t) + I.rrKeys.size() + I.nearKey.size());
            I.bytes = b;
            return true;
        }

        static void deriveVelocityWindows (OrganicInstrument& I)
        {
            auto& R = I.regions;
            for (auto& r : R)
            {
                if (r.xfLo > r.lv) { r.fiLo = (float) r.lv; r.fiHi = (float) r.xfLo; }
                else               { r.fiLo = r.fiHi = (float) r.lv - 0.5f; }
                if (r.xfHi < r.hv) { r.foLo = (float) r.xfHi; r.foHi = (float) r.hv; }
                else               { r.foLo = r.foHi = (float) r.hv + 0.5f; }
            }
            // unauthored seams between butting layers → a ±3 velocity equal-power band
            for (size_t i = 0; i < R.size(); ++i)
            {
                auto& lo = R[i];
                if (lo.kind != org::Kind::Attack || lo.xfHi < lo.hv || lo.hv >= 127) continue;
                for (size_t j = 0; j < R.size(); ++j)
                {
                    auto& hi = R[j];
                    if (j == i || hi.kind != org::Kind::Attack || hi.artic != lo.artic || hi.lv != lo.hv + 1) continue;
                    if (hi.xfLo > hi.lv || hi.lk > lo.hk || hi.hk < lo.lk) continue;
                    if (hi.rrLen != lo.rrLen || hi.rrPos != lo.rrPos || hi.randLo != lo.randLo || hi.randHi != lo.randHi) continue;
                    // Each side of the seam reaches 30 % into its OWN layer (1.5..8 velocities), so a narrow layer
                    // (Salamander's 121-127) borrows blend room from its wide neighbour and still keeps a plateau:
                    // an 8 dB recorded step spreads over up to 16 velocities. Both regions share the one interval →
                    // sin/cos over it stays equal-power.
                    const float c = (float) lo.hv + 0.5f;
                    const float dLo = std::clamp (0.3f * (float) (lo.hv - lo.lv + 1), 1.5f, 8.0f);
                    const float dHi = std::clamp (0.3f * (float) (hi.hv - hi.lv + 1), 1.5f, 8.0f);
                    const float bLo = std::max (c - dLo, std::max ((float) lo.lv - 0.5f, lo.fiHi));
                    const float bHi = std::min (c + dHi, (float) hi.hv + 0.5f);
                    if (c - bLo <= 0.25f || bHi - c <= 0.25f) continue;
                    lo.foLo = bLo; lo.foHi = bHi;
                    hi.fiLo = bLo; hi.fiHi = bHi;
                }
            }
        }

        static void deriveGroups (OrganicInstrument& I)
        {
            std::map<int, int> dense;
            for (auto& r : I.regions) if (r.grp > 0 && dense.find (r.grp) == dense.end()) { const int n = (int) dense.size(); dense[r.grp] = n; }
            I.numGroups = (int) dense.size();
            for (auto& r : I.regions)
            {
                r.grpIdx   = r.grp   > 0 ? dense[r.grp] : -1;
                auto it = dense.find (r.offBy);
                r.offByIdx = (r.offBy > 0 && it != dense.end()) ? it->second : -1;
            }
        }

        static void deriveSustain (OrganicInstrument& I)
        {
            for (auto& r : I.regions)
            {
                const auto& S = I.samples[(size_t) r.smp];
                const double sr = S.sampleRate;
                r.refDb = S.envAt ((double) r.onset + 0.15 * sr);
                if (r.hasTail())
                {
                    const double a = std::max ((double) r.onset, (double) r.tailLs - 0.5 * sr);
                    const double dt = ((double) r.tailLs - a) / sr;
                    r.tailSlopeDb = dt > 0.05 ? (float) std::max (0.0, (double) (S.envAt (a) - S.envAt ((double) r.tailLs)) / dt) : 0.f;
                }
                else
                {
                    const double a = (double) r.onset + 0.15 * sr, b = (double) r.end - 0.05 * sr;
                    r.tailSlopeDb = b > a + 0.1 * sr ? (float) std::max (0.0, (double) (S.envAt (a) - S.envAt (b)) / ((b - a) / sr)) : 0.f;
                }
            }
        }

        static void buildLookup (OrganicInstrument& I)
        {
            const size_t cells = (size_t) I.numArtics * org::kMaxKinds * 128 * 128;
            I.spans.assign (cells, {});
            I.lists.clear();
            I.rrKeys.assign ((size_t) I.numArtics * 128, 0);
            std::vector<uint16_t> tmp;
            for (int a = 0; a < I.numArtics; ++a)
                for (int k = 0; k < org::kMaxKinds; ++k)
                    for (int key = 0; key < 128; ++key)
                    {
                        // regions touching this key/kind/artic, then filter per velocity
                        tmp.clear();
                        for (size_t i = 0; i < I.regions.size(); ++i)
                        {
                            const auto& r = I.regions[i];
                            if (r.artic == a && (int) r.kind == k && key >= r.lk && key <= r.hk) tmp.push_back ((uint16_t) i);
                        }
                        for (int v = 0; v < 128; ++v)
                        {
                            const float vc = (float) std::max (1, v);
                            auto& sp = I.spans[(((size_t) a * org::kMaxKinds + (size_t) k) * 128 + (size_t) key) * 128 + (size_t) v];
                            sp.first = (uint32_t) I.lists.size();
                            for (auto idx : tmp)
                            {
                                const auto& r = I.regions[idx];
                                if (r.fiLo <= vc + 0.5f && r.foHi >= vc - 0.5f) I.lists.push_back (idx);
                            }
                            sp.count = (uint32_t) I.lists.size() - sp.first;
                        }
                        if (k == (int) org::Kind::Attack)
                            for (auto idx : tmp) if (I.regions[idx].isRR) I.rrKeys[(size_t) a * 128 + (size_t) key] = 1;
                    }
            // tp105 NO-SILENCE law (Max: "it's round-robinning to a silence"): a key outside the authored range maps to
            // the nearest key that has an attack region; a tie goes UP (the edge sample then pitches down, not up).
            I.nearKey.assign ((size_t) I.numArtics * 128, 0);
            for (int a = 0; a < I.numArtics; ++a)
            {
                bool mapped[128] = {};
                for (const auto& r : I.regions)
                    if (r.artic == a && r.kind == org::Kind::Attack)
                        for (int key = r.lk; key <= r.hk; ++key) mapped[key] = true;
                for (int key = 0; key < 128; ++key)
                {
                    int best = key;
                    if (! mapped[key])
                        for (int d = 1; d < 128; ++d)
                        {
                            if (key + d < 128 && mapped[key + d]) { best = key + d; break; }
                            if (key - d >= 0  && mapped[key - d]) { best = key - d; break; }
                        }
                    I.nearKey[(size_t) a * 128 + (size_t) key] = (uint8_t) best;
                }
            }
        }
    };

    void OrganicInstrument::resetPerformanceState() const noexcept
    {
        for (int i = 0; i < numArtics * 128; ++i) { rrSeq_[(size_t) i].store (0); rrLast_[(size_t) i].store (-1); }
        for (int i = 0; i < 128; ++i) fakeLast_[(size_t) i].store (0);
        if (noiseLast_ != nullptr) for (int i = 0; i < numArtics * 2 * 128; ++i) noiseLast_[(size_t) i].store (-1);   // tp107
        for (int i = 0; i < std::max (1, numGroups); ++i) groupEpoch_[(size_t) i].store (0);
    }

    std::shared_ptr<OrganicInstrument> OrganicInstrument::loadFromFolder (const juce::File& folder, juce::String* error)
    {
        try { return OrganicLoader::load (folder, error); }
        catch (...) { if (error) *error = "exception while loading"; return nullptr; }
    }

    //==============================================================================================
    //  The library state (all of it: the API class has no data members)
    //==============================================================================================
    namespace
    {
        bool validId (const juce::String& id)
        {
            return id.isNotEmpty() && id.length() < 200 && ! id.contains ("..") && ! id.containsAnyOf ("/\\:")
                   && ! id.startsWithChar ('.');
        }

        struct LibState : public juce::Thread
        {
            using Done = std::function<void (std::shared_ptr<const OrganicInstrument>)>;
            struct Entry
            {
                std::shared_ptr<const OrganicInstrument> inst;
                int64_t bytes = 0;
                bool    loading = false;
                std::vector<Done> waiting;
                double  idleSince = -1.0;
            };

            juce::CriticalSection lock;
            std::map<juce::String, Entry> cache;
            std::vector<juce::String> loadQueue;
            juce::var indexVar;  bool indexLoaded = false;
            std::map<juce::String, int> idToIdx;  std::map<int, juce::String> idxToId;  bool idsLoaded = false;
            bool started = false;

            LibState() : juce::Thread ("Organics") {}
            ~LibState() override { stopThread (5000); }

            void ensureStarted()
            {
                if (! started) { started = true; startThread (juce::Thread::Priority::low); }
            }

            void loadIds()
            {
                idToIdx.clear(); idxToId.clear(); idsLoaded = true;
                const auto root = OrganicsLibrary::get().root();
                juce::var v;
                const auto f = root.getChildFile ("ids.json");
                if (f.existsAsFile() && juce::JSON::parse (f.loadFileAsString(), v).wasOk())
                    if (auto* o = v.getDynamicObject())
                        for (auto& p : o->getProperties())
                        {
                            const int n = (int) p.value;
                            if (n <= 0) continue;
                            idToIdx[p.name.toString()] = n;
                            idxToId[n] = p.name.toString();
                        }
                // tp108 — the user's imports (User/user-ids.json, numbers from 2048). A factory number or id always wins.
                juce::var u;
                const auto uf = root.getChildFile ("User").getChildFile ("user-ids.json");
                if (uf.existsAsFile() && juce::JSON::parse (uf.loadFileAsString(), u).wasOk())
                    if (auto* o = u.getDynamicObject())
                        for (auto& p : o->getProperties())
                        {
                            const int n = (int) p.value;
                            const auto id = p.name.toString();
                            if (n < 2048 || n > 4095 || ! org::isUserId (id) || idxToId.count (n) || idToIdx.count (id)) continue;
                            idToIdx[id] = n;
                            idxToId[n] = id;
                        }
            }

            void deliver (std::vector<Done>&& cbs, std::shared_ptr<const OrganicInstrument> inst)
            {
                for (auto& cb : cbs)
                {
                    if (! cb) continue;
                    juce::MessageManager::callAsync ([cb, inst] { cb (inst); });
                }
            }

            void run() override
            {
                while (! threadShouldExit())
                {
                    juce::String job;
                    {
                        const juce::ScopedLock sl (lock);
                        if (! loadQueue.empty()) { job = loadQueue.front(); loadQueue.erase (loadQueue.begin()); }
                    }
                    if (job.isNotEmpty())
                    {
                        auto folder = org::instrumentFolder (job);
                        juce::String err;
                        std::shared_ptr<const OrganicInstrument> inst = folder.isDirectory() ? OrganicInstrument::loadFromFolder (folder, &err) : nullptr;
                        std::vector<Done> cbs;
                        {
                            const juce::ScopedLock sl (lock);
                            auto it = cache.find (job);
                            if (it != cache.end())
                            {
                                cbs = std::move (it->second.waiting);
                                if (inst != nullptr) { it->second.inst = inst; it->second.bytes = static_cast<const OrganicInstrument&> (*inst).bytes;
                                                       it->second.loading = false; it->second.idleSince = -1.0; }
                                else cache.erase (it);
                            }
                        }
                        deliver (std::move (cbs), inst);
                        continue;
                    }
                    housekeeping();
                    wait (100);
                }
            }

            void housekeeping()
            {
                gReleaseQueue.drain();
                const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
                std::vector<std::shared_ptr<const OrganicInstrument>> dying;
                {
                    const juce::ScopedLock sl (lock);
                    for (auto it = cache.begin(); it != cache.end();)
                    {
                        auto& e = it->second;
                        if (e.inst != nullptr && ! e.loading)
                        {
                            if (e.inst.use_count() == 1)
                            {
                                if (e.idleSince < 0.0) e.idleSince = now;
                                else if (now - e.idleSince >= 5.0) { dying.push_back (std::move (e.inst)); it = cache.erase (it); continue; }
                            }
                            else e.idleSince = -1.0;
                        }
                        ++it;
                    }
                }
                dying.clear();   // frees outside the lock, on this thread
            }
        };

        LibState& state() { static LibState s; return s; }
    }

    //==============================================================================================
    OrganicsLibrary& OrganicsLibrary::get() { static OrganicsLibrary lib; return lib; }

    juce::File OrganicsLibrary::root() const
    {
        const auto env = juce::SystemStats::getEnvironmentVariable ("TERRAIN_ORGANICS_DIR", {});
        if (env.isNotEmpty() && juce::File::isAbsolutePath (env)) return juce::File (env);
        // <terrainDataDir>/Organics — terrainDataDir() is PluginEditor.cpp's six lines (fb605 fresh/legacy rule).
        const auto base  = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
        const auto wc    = base.getChildFile ("WavesCrate");
        const auto fresh = wc.getChildFile ("Terrain"), legacy = wc.getChildFile ("TerrainInstrument");
        const auto data  = fresh.exists() ? fresh : (legacy.exists() ? legacy : fresh);
        const auto org   = data.getChildFile ("Organics");
       #if JUCE_MAC
        // The contract text names ~/Library/Application Support/WavesCrate/Terrain/Organics; terrainDataDir() on a
        // Mac is ~/Library/WavesCrate/Terrain. Honour an install at the contract path until the two agree.
        if (! org.isDirectory())
        {
            const auto alt = base.getChildFile ("Application Support/WavesCrate/Terrain/Organics");
            if (alt.isDirectory()) return alt;
        }
       #endif
        return org;
    }

    juce::var OrganicsLibrary::index()
    {
        auto& s = state();
        const juce::ScopedLock sl (s.lock);
        if (! s.indexLoaded)
        {
            s.indexLoaded = true;
            juce::var v;
            const auto f = root().getChildFile ("index.json");
            if (f.existsAsFile() && juce::JSON::parse (f.loadFileAsString(), v).wasOk() && v.isArray()) s.indexVar = v;
            else s.indexVar = juce::var (juce::Array<juce::var>());
            // tp108 — merge the user's imports (User/user-index.json): category "User", after the factory entries. A copy,
            // so the factory index.json var is never touched; an id the factory already has is skipped.
            juce::var u;
            const auto uf = root().getChildFile ("User").getChildFile ("user-index.json");
            if (uf.existsAsFile() && juce::JSON::parse (uf.loadFileAsString(), u).wasOk())
                if (auto* ua = u.getArray(); ua != nullptr && ! ua->isEmpty())
                {
                    juce::Array<juce::var> merged;
                    std::set<juce::String> have;
                    if (auto* fa = s.indexVar.getArray())
                        for (auto& e : *fa) { merged.add (e); have.insert (e.getProperty ("id", {}).toString()); }
                    for (auto& e : *ua)
                    {
                        const auto id = e.getProperty ("id", {}).toString();
                        if (! org::isUserId (id) || have.count (id) || ! e.isObject()) continue;
                        auto* o = new juce::DynamicObject();
                        if (auto* src = e.getDynamicObject()) for (auto& kv : src->getProperties()) o->setProperty (kv.name, kv.value);
                        o->setProperty ("category", "User");
                        merged.add (juce::var (o));
                        have.insert (id);
                    }
                    s.indexVar = juce::var (merged);
                }
        }
        return s.indexVar;
    }

    void OrganicsLibrary::rescan()
    {
        auto& s = state();
        const juce::ScopedLock sl (s.lock);
        s.indexLoaded = false; s.idsLoaded = false;
        s.indexVar = juce::var();
        s.loadIds();
        // tp108 — a user instrument may have been deleted or re-imported under the same id: drop the cached copies (an
        // oscillator that still plays one keeps its own reference; the next request() reads the folder again)
        std::vector<std::shared_ptr<const OrganicInstrument>> drop;
        for (auto it = s.cache.begin(); it != s.cache.end();)
            if (org::isUserId (it->first) && ! it->second.loading) { drop.push_back (std::move (it->second.inst)); it = s.cache.erase (it); }
            else ++it;
    }

    void OrganicsLibrary::request (const juce::String& id, std::function<void (std::shared_ptr<const OrganicInstrument>)> done)
    {
        auto& s = state();
        if (! validId (id))
        {
            if (done) juce::MessageManager::callAsync ([done] { done (nullptr); });
            return;
        }
        std::shared_ptr<const OrganicInstrument> hit;
        {
            const juce::ScopedLock sl (s.lock);
            auto& e = s.cache[id];
            if (e.inst != nullptr) { hit = e.inst; e.idleSince = -1.0; }
            else
            {
                e.waiting.push_back (std::move (done));
                if (! e.loading) { e.loading = true; s.loadQueue.push_back (id); }
            }
        }
        s.ensureStarted();
        if (hit != nullptr) { if (done) juce::MessageManager::callAsync ([done, hit] { done (hit); }); }
        else s.notify();
    }

    bool org::isUserId (const juce::String& id) noexcept
    {
        return id.startsWith ("user.") && id.length() > 5 && validId (id);
    }

    juce::File org::instrumentFolder (const juce::String& id)
    {
        const auto root = OrganicsLibrary::get().root();
        return isUserId (id) ? root.getChildFile ("User").getChildFile (id) : root.getChildFile (id);
    }

    int OrganicsLibrary::idToIndex (const juce::String& id) const
    {
        auto& s = state();
        const juce::ScopedLock sl (s.lock);
        if (! s.idsLoaded) s.loadIds();
        auto it = s.idToIdx.find (id);
        return it == s.idToIdx.end() ? -1 : it->second;
    }

    juce::String OrganicsLibrary::indexToId (int index) const
    {
        auto& s = state();
        const juce::ScopedLock sl (s.lock);
        if (! s.idsLoaded) s.loadIds();
        auto it = s.idxToId.find (index);
        return it == s.idxToId.end() ? juce::String() : it->second;
    }

    int OrganicsLibrary::residentCount() const
    {
        auto& s = state();
        const juce::ScopedLock sl (s.lock);
        int n = 0;
        for (auto& p : s.cache) if (p.second.inst != nullptr) ++n;
        return n;
    }

    int64_t OrganicsLibrary::residentBytes() const
    {
        auto& s = state();
        const juce::ScopedLock sl (s.lock);
        int64_t b = 0;
        for (auto& p : s.cache) if (p.second.inst != nullptr) b += p.second.bytes;
        return b;
    }
}
