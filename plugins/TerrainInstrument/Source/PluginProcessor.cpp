#include "PluginProcessor.h"

// fb178 — mono-tap DAHDSR forwarding (mirrors SynthVoice::setEnvelopeDAHDSR's mapping)
static void terrain_setEnvDAHDSR (terrain::TerrainEnvelope& e, float dl, float a, float h,
                                  float d, float sv, float r, float ca, float cd, float cr, bool lp)
{
    e.setDelay (dl * 0.001); e.setAttack (a * 0.001); e.setHold (h * 0.001);
    e.setDecay (d * 0.001);  e.setSustain (sv);       e.setRelease (r * 0.001);
    e.setAttackCurve (ca);   e.setDecayCurve (cd);    e.setReleaseCurve (cr);
    e.setLoop (lp);
}
#include "PluginEditor.h"

static void terrainCardLogP (const juce::String& msg);   // fb84 — card-window forensic log (defined with the card-window methods below)
#include "ParametricEQ.h"
#include <cmath>
#include <algorithm>   // LFO ARC L1 — std::sort (shape-point ordering in setSynthLfoShapes)
#include <cstring>     // LFO ARC L1 — std::memcpy (drawn-table shared→audio copy)
#if JUCE_MAC
 #include <objc/message.h>   // fb151 — physical mouse-button state via +[NSEvent pressedMouseButtons]
 #include <objc/runtime.h>
#endif

// fb249 — MASTER LEVEL: instrument makeup gain + a transparent-knee soft-clipper.
// Root cause of "Terrain is ~10 dB quieter than Serum": at default the voice mix ate a
// -6 dB pre-FX pad (kVoiceToFxPad, NEVER made up) on top of conservative 0.7 osc-level
// and 0.7 amp-sustain defaults — ~-12 dB baked in before velocity. This makeup restores
// the pad's output cost and adds ~+3 dB so a default sine lands ~-15 dBFS (Serum-matched).
// The OLD master clipper was an always-curving tanh (y = c·tanh(x/c)); at the louder level
// it put a visible ~-32 dB 3rd harmonic on a PURE sine. The new clipper is UNITY (0% added
// THD, measured) below the knee, so single notes stay pristine, then rolls smoothly to the
// same -0.3 dBFS ceiling — hot chords still bound safely (DAC-protected), normal levels stay
// clean. Makeup is applied PRE-clip so peaks can never exceed the ceiling.
// fb264 — ROOT CAUSE of "dense chords hard-clip above ~4 notes": the tanh knee was narrow
// (0.75→0.966, span 0.216) and voices sum with NO polyphony gain management, so once the summed
// peak was driven past the knee — e.g. velocity→Volume at full depth on a 9th/11th — the wave
// FLATTENED into a near-square = audibly hard clipping (measured: buzz 0.1%→23% as notes 1→11).
// Fix = a stereo-linked peak LIMITER (gain-reduction, ~0.8 ms atk / 120 ms rel) BEFORE the clip:
// it turns the level down a hair instead of squaring the wave, so chords stay LOUD but clean —
// Serum's output stage is a limiter, not a fixed shaper. Knee raised 0.75→0.90 to MEET the limiter
// threshold so nothing distorts below it; the clip is now only a transient safety catch. Validated
// offline: buzz 23%→<3% at 11 notes, single notes 0% THD at every level, peak still 0.966 (no
// loudness lost). See clip_final.py in the session scratchpad for the measurement.
static constexpr float kMasterCeiling    = 0.96605f;  // -0.3 dBFS — output ceiling (unchanged)
static constexpr float kSoftClipKnee     = 0.90f;     // fb264 — knee raised 0.75→0.90 (-0.9 dBFS): the limiter holds peaks here, so the clip is a rare safety catch. Transparent (unity) below this.
static constexpr float kInstrumentMakeup = 2.0f;      // fb299 — +6.02 dB (×2), MEASURED Serum match. A/B of IDENTICAL MIDI (bass triangle ~49 Hz, same settings) showed Serum exactly +6.02 dB louder on PEAK, RMS *and* LUFS SIMULTANEOUSLY, with IDENTICAL crest factor (4.72 vs 4.72 dB) and identical low-end shape (±0.02 dB level-matched) → a pure flat gain gap, NOT compression/EQ (measure_ab.py). +6.02 dB == our own kVoiceToFxPad (the −6 dB pre-FX pad on the dry buffer that fb265 left un-made-up); ×2 exactly cancels it so a single note goes −20→−14 dBFS = Serum's −14.01. VERIFIED SAFE (verify_makeup.py, exact port of this output stage): single-note added-THD = 0 (harmonics identical at makeup 1.0 vs 2.0 = the triangle itself), chords ≤4 notes fully transparent, extreme 6–11-note clusters ride the fb264 limiter bounded at −0.3 dBFS (gentle soft-knee, no hard-clip/square — Serum-equivalent). fb265's buzz was at +9 dB (2.8184); +6 dB keeps 3 dB MORE headroom than that AND matches Serum's actual per-note level. NOTE fb265's "single voice ~-15 dBFS" estimate was ~5 dB optimistic — the real bounce measured -20 dBFS (osc not full-scale). Loudness now lives at the source, like Serum; lifts the WHOLE engine +6 dB uniformly.
static constexpr float kLimiterThresh    = 0.90f;     // fb264 — master peak-limiter threshold (== knee). Post-makeup peaks are gain-reduced (not squared) to here; dense chords stay LOUD but clean.
static inline float masterSoftClip (float x) noexcept
{
    const float a = std::abs (x);
    if (a <= kSoftClipKnee) return x;                 // transparent — zero added distortion in the normal range
    const float span = kMasterCeiling - kSoftClipKnee;
    return std::copysign (kSoftClipKnee + span * std::tanh ((a - kSoftClipKnee) / span), x);
}

//==============================================================================
TerrainInstrumentAudioProcessor::TerrainInstrumentAudioProcessor()
    : AudioProcessor (BusesProperties()
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    initializePresets();

    // LFO ARC L1 — default every drawn-shape table to the triangle (a shape param restored
    // to CUSTOM without blob data must still sound sane; setStateInformation re-bakes over this).
    for (int li = 0; li < wc::NUM_LFOS; ++li)
        for (int lk = 0; lk <= wc::kLfoTableN; ++lk)
        {
            const float lp = (float) (lk % wc::kLfoTableN) / (float) wc::kLfoTableN;
            lfoTableShared_[li][lk] = lfoTableAudio_[li][lk] = 1.0f - 4.0f * std::fabs (lp - 0.5f);
        }

    // === Sampler engine wiring — Task 5: singleton synth removed. Each layer's
    // synth is wired in LayerState's constructor using that layer's own atomics.
    // Label each layer with its 0-3 index for self-identification.
    for (size_t i = 0; i < layers.size(); ++i)
        layers[i].layerIndex = static_cast<int>(i);

    // Mix page Phase A: seed each layer's default VELOCITY zone to even quarters
    // (0-31 / 32-63 / 64-95 / 96-127). Users can drag the boundary handles in
    // the Mix page's VELOCITY-mode UI; this just gives a sensible starting point
    // so the first time a user selects VELOCITY mode each layer already responds
    // to its own slice of the 0-127 range.
    layers[0].velocityZoneMin.store (0);    layers[0].velocityZoneMax.store (31);
    layers[1].velocityZoneMin.store (32);   layers[1].velocityZoneMax.store (63);
    layers[2].velocityZoneMin.store (64);   layers[2].velocityZoneMax.store (95);
    layers[3].velocityZoneMin.store (96);   layers[3].velocityZoneMax.store (127);

    // KEYTRACK trigger mode: seed each layer's default key zone to even quarters
    // of the MIDI note range (0-31 / 32-63 / 64-95 / 96-127) — a sensible keyboard
    // split so KEYTRACK responds immediately. Users drag the handles in the UI.
    layers[0].keyZoneMin.store (0);    layers[0].keyZoneMax.store (31);
    layers[1].keyZoneMin.store (32);   layers[1].keyZoneMax.store (63);
    layers[2].keyZoneMin.store (64);   layers[2].keyZoneMax.store (95);
    layers[3].keyZoneMin.store (96);   layers[3].keyZoneMax.store (127);

    // Phase 1 task 9: wire modulationEngine into ALL layers' voices (was layer 0 only).
    // LayerState constructor passes nullptr for ModulationEngine; we patch it here.
    // Note: SamplerVoice stores a pointer to the engine so this is safe as long as
    // the engine outlives the voices (processor owns both, destruction is LIFO).
    for (auto& layer : layers)
    {
        for (int v = 0; v < layer.synth.getNumVoices(); ++v)
        {
            if (auto* sv = dynamic_cast<tw::SamplerVoice*> (layer.synth.getVoice (v)))
                sv->setModulationEngine (&modulationEngine);
        }
    }

    // Synth engine — Phase 1 MPV (one SynthSound + kSynthVoiceCount voices)
    // CPU: keep TYPED pointers alongside — the audio thread iterates all 96 voices several
    // times per block, and each dynamic_cast is a real RTTI walk (~300 casts/block killed).
    synthEngine.addSound (new tw::SynthSound());
    for (int i = 0; i < kSynthVoiceCount; ++i)
    {
        auto* v = new tw::SynthVoice();
        synthVoices_[i] = v;              // owned by synthEngine; array never changes after this
        // CPU: instance-wide grain budget — every granular engine shares one live-grain
        // counter, so 4 dense granular oscs × polyphony thin gracefully at kGranBudget
        // instead of multiplying to thousands of grains (≈8.5 ns per grain-sample each).
        v->setGrainBudget (&granGrainsLive_, kGranBudget);
        v->setPartialBudget (&geodePartialsLive_, kGeodePartialBudget);   // GEODE-ENGINE — shared partial budget
        v->setGeodeStores (&geodeSlot_[0].live, &geodeSlot_[1].live,
                           &geodeSlot_[2].live, &geodeSlot_[3].live);      // GEODE-ENGINE — atomic store pointers
        synthEngine.addVoice (v);
    }

    // Spectral-morph rebuild runs on the message thread (the rebuild is ~5.6ms,
    // far too heavy for the audio thread). 60Hz polling keeps the morph knob
    // responsive while never touching a buffer the audio thread is reading.
    startTimerHz (60);

    loadImportsRegistry();   // IMPORTS (fb60) — restore referenced files/folders from the app-data JSON
}

TerrainInstrumentAudioProcessor::~TerrainInstrumentAudioProcessor()
{
    if (! cardWindows_.empty())
        terrainCardLogP ("instance dtor clearing " + juce::String ((int) cardWindows_.size()) + " card window(s)");
    uiClients_.fetch_sub ((int) cardWindows_.size(), std::memory_order_relaxed);   // fb148 — viz census
    cardWindows_.clear();   // fb83 — popped card windows die with the plugin INSTANCE (hosts destroy processors on the message thread)

    // Stop the morph rebuild timer BEFORE any members are destroyed — the
    // callback touches apvts / wavetableBank / morph slots.
    stopTimer();

    if (captureExportThread && captureExportThread->joinable())
        captureExportThread->join();
}

//==============================================================================
// fb83 — popped-out FLOW card windows (see PluginProcessor.h). The concrete
// window class lives in PluginEditor.cpp; here they're just Components to own.
// fb84 — every intentional teardown logs a marker BEFORE the erase, so the
// window dtor's "destroyed" line in cardwin.log always has a named cause; a
// "destroyed" with no preceding marker = external teardown (the smoking gun).
//==============================================================================
// fb158 — gate single-sourced in PluginProcessor.h (TERRAIN_CARDWIN_LOG)
static void terrainCardLogP (const juce::String& msg)
{
   #if TERRAIN_CARDWIN_LOG
    auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("WavesCrate").getChildFile ("TerrainInstrument").getChildFile ("cardwin.log");
    f.getParentDirectory().createDirectory();
    f.appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + "  " + msg + "\n");
   #else
    juce::ignoreUnused (msg);
   #endif
}

void TerrainInstrumentAudioProcessor::adoptCardWindow (const juce::String& id, std::unique_ptr<juce::Component> w)
{
    if (cardWindows_.find (id) == cardWindows_.end())
        uiClients_.fetch_add (1, std::memory_order_relaxed);              // fb148 — viz census
    cardWindows_[id] = std::move (w);
}

void TerrainInstrumentAudioProcessor::closeCardWindow (const juce::String& id)
{
    terrainCardLogP ("closing " + id + " (card ✕)");
    if (cardWindows_.erase (id) > 0)
        uiClients_.fetch_sub (1, std::memory_order_relaxed);              // fb148 — viz census
    if (auto* ed = dynamic_cast<TerrainInstrumentAudioProcessorEditor*> (getActiveEditor()))
        ed->notifyCardWindowGone (id, false);
}

void TerrainInstrumentAudioProcessor::dockCardWindow (const juce::String& id)
{
    terrainCardLogP ("docking " + id + " (card ⧉)");
    if (cardWindows_.erase (id) > 0)
        uiClients_.fetch_sub (1, std::memory_order_relaxed);              // fb148 — viz census
    if (auto* ed = dynamic_cast<TerrainInstrumentAudioProcessorEditor*> (getActiveEditor()))
        ed->notifyCardWindowGone (id, true);    // no editor open → dock degrades to a plain close
}

//==============================================================================
// Spectral Morph — message-thread rebuild + audio-thread resolve.
//==============================================================================
const tw::Wavetable*
TerrainInstrumentAudioProcessor::resolveMorphTable (MorphSlot& slot, int presetIdx) noexcept
{
    // Audio thread: atomic-load the published morphed table. If a morph is active,
    // report which buffer we're reading so the rebuild never overwrites it.
    // RACE HARDENING: a buffer whose rebuild is IN FLIGHT (ready == false) is never
    // handed to voices — fall back to the plain bank table (sound, never zeros).
    const tw::Wavetable* m = slot.live.load (std::memory_order_acquire);
    if (m != nullptr)
    {
        const int idx = (m == &slot.buf[1]) ? 1 : 0;
        if (slot.ready[idx].load (std::memory_order_acquire))
        {
            slot.audioReadingIdx.store (idx, std::memory_order_release);
            return m;
        }
    }
    slot.audioReadingIdx.store (-1, std::memory_order_release);
    return wavetableBank.getTable (presetIdx);
}

//==============================================================================
// Wavetable EXTENDER — build an imported table from dropped audio (message thread).
//==============================================================================
void TerrainInstrumentAudioProcessor::rebuildImport (int osc)
{
    osc = juce::jlimit (0, 3, osc);
    auto& slot = importSlot_[osc];
    const int idx = slot.nextIdx;                                   // build into the NON-live buffer
    slot.buf[idx].buildFromPcm (importedPcm_[osc].data(), (int) importedPcm_[osc].size(), importFrames_[osc]);
    slot.live.store (&slot.buf[idx], std::memory_order_release);    // publish only AFTER the build finishes
    slot.nextIdx = 1 - idx;
}

// fb248 — the SAME build, off the message thread. Snapshot the source (fast copy on the msg thread), then
// the heavy 8-mip FFT reconstruction runs on wtBuildPool_ (1 serialized worker) so the UI never freezes on a
// big Serum-size table. live is published atomically when done; the audio thread reads the old table meanwhile.
void TerrainInstrumentAudioProcessor::rebuildImportAsync (int osc)
{
    osc = juce::jlimit (0, 3, osc);
    auto snap = std::make_shared<std::vector<float>> (importedPcm_[osc]);
    const int frames = importFrames_[osc];
    wtBuildPool_.addJob ([this, osc, snap, frames]
    {
        auto& slot = importSlot_[osc];
        const int idx = slot.nextIdx;
        slot.buf[idx].buildFromPcm (snap->data(), (int) snap->size(), frames);
        slot.live.store (&slot.buf[idx], std::memory_order_release);
        slot.nextIdx = 1 - idx;
    });
}

void TerrainInstrumentAudioProcessor::importAudioAsWavetable (int osc, const float* pcm, int numSamples)
{
    osc = juce::jlimit (0, 3, osc);
    importedPcm_[osc].assign (pcm, pcm + juce::jmax (0, numSamples));   // keep the source so resolution can change later
    // Auto-detect a WAVETABLE FILE (concatenated kFrameSize single-cycles, e.g. Serum/Vital): an exact
    // multiple of kFrameSize giving 2..kMaxFrames frames → use its REAL frames. buildFromPcm's evenly-
    // spaced windows land exactly on the frame boundaries when framesWanted = n/frameSize, so it's
    // frame-perfect. Otherwise it's arbitrary audio → keep the resolution-mode frame count.
    const int fs = tw::Wavetable::kFrameSize;
    const int n  = (int) importedPcm_[osc].size();
    importIsFile_[osc] = (n >= fs * 2 && (n % fs) == 0 && (n / fs) <= tw::Wavetable::kMaxFrames);
    if (importIsFile_[osc]) importFrames_[osc] = n / fs;
    rebuildImportAsync (osc);   // fb248 — off the message thread (no UI freeze / purple flash on big tables)
}

void TerrainInstrumentAudioProcessor::setImportFrames (int osc, int frames)
{
    osc = juce::jlimit (0, 3, osc);
    importFrames_[osc] = juce::jlimit (2, tw::Wavetable::kMaxFrames, frames);
    if (! importedPcm_[osc].empty()) rebuildImportAsync (osc);   // fb248 — re-slice off the message thread (no freeze)
}

void TerrainInstrumentAudioProcessor::clearImportedWavetable (int osc)
{
    osc = juce::jlimit (0, 3, osc);
    importSlot_[osc].live.store (nullptr, std::memory_order_release);
    importedPcm_[osc].clear();
    importName_[osc]   = {};
    importIsFile_[osc] = false;
}

void TerrainInstrumentAudioProcessor::setImportName (int osc, const juce::String& name)
{
    importName_[juce::jlimit (0, 3, osc)] = name;
}

juce::String TerrainInstrumentAudioProcessor::getImportStateJson()
{
    juce::String j = "{";
    for (int o = 0; o < 4; ++o)
    {
        if (o) j += ",";
        const bool active = importSlot_[o].live.load (std::memory_order_acquire) != nullptr;
        const char k[2] = { (char) ('a' + o), 0 };
        auto nm = importName_[o].replace ("\\", "\\\\").replace ("\"", "\\\"");
        j += "\"" + juce::String (k) + "\":{\"active\":" + (active ? "true" : "false")
           + ",\"name\":\"" + nm + "\"}";
    }
    return j + "}";
}

void TerrainInstrumentAudioProcessor::setWaterfallView (int osc, bool on)
{
    wt3dView_[juce::jlimit (0, 3, osc)] = on;
}

juce::String TerrainInstrumentAudioProcessor::getWaterfallViewJson()
{
    juce::String j = "{";
    for (int o = 0; o < 4; ++o)
    {
        if (o) j += ",";
        const char k[2] = { (char) ('a' + o), 0 };
        j += "\"" + juce::String (k) + "\":" + (wt3dView_[o] ? "true" : "false");
    }
    return j + "}";
}

juce::String TerrainInstrumentAudioProcessor::getNoiseWavePeaksJson()
{
    // fb66 — compact min/max envelope of the loaded noise sample for the waveform viz. Empty ("") when no
    // sample is loaded (algorithmic type) → the UI draws the live oscilloscope instead. Msg-thread only.
    auto buf = noiseSampleBuffer_.load();
    const int len = (buf != nullptr) ? buf->getNumSamples() : 0;
    if (buf == nullptr || len < 2) return {};
    const int cols = 220;                                   // downsample to ~viz width
    const int ch   = juce::jmin (2, buf->getNumChannels());
    juce::String mn, mx;
    for (int c = 0; c < cols; ++c)
    {
        const int s0 = (int) ((juce::int64) c * len / cols);
        int s1 = (int) ((juce::int64) (c + 1) * len / cols);
        if (s1 <= s0) s1 = s0 + 1;
        if (s1 > len)  s1 = len;
        float lo = 0.0f, hi = 0.0f;
        for (int chan = 0; chan < ch; ++chan)
        {
            const float* d = buf->getReadPointer (chan);
            for (int s = s0; s < s1; ++s) { const float v = d[s]; if (v < lo) lo = v; if (v > hi) hi = v; }
        }
        if (c) { mn << ","; mx << ","; }
        mn << juce::String (lo, 3);
        mx << juce::String (hi, 3);
    }
    return "{\"min\":[" + mn + "],\"max\":[" + mx + "]}";
}

juce::String TerrainInstrumentAudioProcessor::getOscWavetableJson (int osc)
{
    osc = juce::jlimit (0, 3, osc);
    // Resolve the table the same way the voice does: imported override, else the factory bank.
    const tw::Wavetable* wt = importSlot_[osc].live.load (std::memory_order_acquire);
    if (wt == nullptr)
    {
        static const char* const WTP[4] = { ParameterIDs::SYN_OSC_A_WT_PRESET, ParameterIDs::SYN_OSC_B_WT_PRESET,
                                            ParameterIDs::SYN_OSC_C_WT_PRESET, ParameterIDs::SYN_OSC_D_WT_PRESET };
        wt = wavetableBank.getTable ((int) *apvts.getRawParameterValue (WTP[osc]));
    }
    if (wt == nullptr) return "{}";

    const int numFrames = juce::jmax (1, wt->getNumFrames());
    const int dispN = juce::jlimit (1, 64, numFrames);     // decimate to ≤64 display frames (viz LOD)
    const int pts   = 160;                                 // points per frame polyline
    juce::MemoryOutputStream out;
    out << "{\"n\":" << dispN << ",\"p\":" << pts << ",\"nf\":" << numFrames << ",\"d\":[";
    for (int i = 0; i < dispN; ++i)
    {
        const float framePos = (dispN > 1) ? (float) i / (float) (dispN - 1) : 0.0f;
        for (int p = 0; p < pts; ++p)
        {
            const float v = wt->lookup (0, framePos, (float) p / (float) pts);   // mip 0 = full bandwidth
            if (i || p) out << ',';
            out << juce::String (v, 4);
        }
    }
    out << "]}";
    return out.toString();
}

// fb248 — WT→LFO: the EXACT current frame at full resolution (no viz decimation, no frame rounding).
// Reads the same base WT_FRAME the user sees/hears, mip 0 (full bandwidth), N clean samples of ONE cycle.
// JS simplifies these to minimal breakpoints (Douglas-Peucker) so the LFO becomes the precise shape.
juce::String TerrainInstrumentAudioProcessor::getOscLfoWaveJson (int osc)
{
    osc = juce::jlimit (0, 3, osc);
    const tw::Wavetable* wt = importSlot_[osc].live.load (std::memory_order_acquire);
    if (wt == nullptr)
    {
        static const char* const WTP[4] = { ParameterIDs::SYN_OSC_A_WT_PRESET, ParameterIDs::SYN_OSC_B_WT_PRESET,
                                            ParameterIDs::SYN_OSC_C_WT_PRESET, ParameterIDs::SYN_OSC_D_WT_PRESET };
        wt = wavetableBank.getTable ((int) *apvts.getRawParameterValue (WTP[osc]));
    }
    if (wt == nullptr) return "{}";
    static const char* const WTF[4] = { ParameterIDs::SYN_OSC_A_WT_FRAME, ParameterIDs::SYN_OSC_B_WT_FRAME,
                                        ParameterIDs::SYN_OSC_C_WT_FRAME, ParameterIDs::SYN_OSC_D_WT_FRAME };
    const float framePos = juce::jlimit (0.0f, 1.0f, (float) *apvts.getRawParameterValue (WTF[osc]));
    const int N = 256;
    juce::MemoryOutputStream out;
    out << "{\"n\":" << N << ",\"d\":[";
    for (int i = 0; i < N; ++i)
    {
        const float v = wt->lookup (0, framePos, (float) i / (float) N);
        if (i) out << ',';
        out << juce::String (v, 5);
    }
    out << "]}";
    return out.toString();
}

// fb340 — the dstBakePts tension math on FLOAT points (the audio-thread per-point-mod rebake
// cannot build juce::vars). MUST stay formula-identical to dstBakePts (the :8046 twin lineage).
void TerrainInstrumentAudioProcessor::dstBakeEff (const LfoShapePtM* pts, int np, float* out, int n) noexcept
{
    if (np < 2) { for (int i = 0; i < n; ++i) out[i] = (float) i / (float) (n - 1) * 2.0f - 1.0f; return; }
    int seg = 0;
    for (int i = 0; i < n; ++i)
    {
        const float p = (float) i / (float) (n - 1);
        while (seg < np - 2 && pts[seg + 1].x <= p) ++seg;
        const float w = juce::jmax (1.0e-6f, pts[seg + 1].x - pts[seg].x);
        float t = juce::jlimit (0.0f, 1.0f, (p - pts[seg].x) / w);
        const float c = pts[seg].c;
        if (std::fabs (c) > 1.0e-4f) { const float P8 = -c * 8.0f; t = (std::exp (P8 * t) - 1.0f) / (std::exp (P8) - 1.0f); }
        out[i] = (pts[seg].y + (pts[seg + 1].y - pts[seg].y) * t) * 2.0f - 1.0f;
    }
}

// fb339 — TABLE SOURCE (bible §9.5): sample an osc's CURRENT wavetable into 16 frames × 1025 and
// upload as the Table transfer stack. src -1 = the generated morph table. Persisted as the SOURCE
// token only — reload re-reads whatever the osc holds then (the pill is a live link, not a copy).
void TerrainInstrumentAudioProcessor::setDistortionTableSrc (int osc)
{
    dstTableSrc_ = juce::jlimit (-1, 3, osc);
    if (dstTableSrc_ < 0) { distortionEngine.clearUserTable(); return; }
    const tw::Wavetable* wt = importSlot_[dstTableSrc_].live.load (std::memory_order_acquire);
    if (wt == nullptr)
    {
        static const char* const WTP[4] = { ParameterIDs::SYN_OSC_A_WT_PRESET, ParameterIDs::SYN_OSC_B_WT_PRESET,
                                            ParameterIDs::SYN_OSC_C_WT_PRESET, ParameterIDs::SYN_OSC_D_WT_PRESET };
        wt = wavetableBank.getTable ((int) *apvts.getRawParameterValue (WTP[dstTableSrc_]));
    }
    if (wt == nullptr) { distortionEngine.clearUserTable(); return; }
    std::vector<float> stack (16 * 1025);               // LOCAL — a static here is shared ACROSS INSTANCES (pluginval multi-instance = race/segv); this is a UI/state call, the allocation is fine
    for (int f = 0; f < 16; ++f)
    {
        const float fp = (float) f / 15.0f;
        for (int i = 0; i <= 1024; ++i)
            stack[(size_t) f * 1025 + (size_t) i] = wt->lookup (0, fp, (float) i / 1024.0f);
    }
    distortionEngine.setUserTable (stack.data(), 16, 1025);
}

//==============================================================================
// IMPORTS REGISTRY (fb60, 3-way fb74) — reference-in-place user imports (paths only, no audio copied).
// kind: 0 = noise · 1 = wavetable · 2 = sample (Sample/Granular/Resynth share one registry).
namespace {
    juce::File importsRegPath (int kind)
    {
        static const char* const kRegNames[3] = { "imports-noise.json", "imports-wavetable.json", "imports-sample.json" };
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("WavesCrate").getChildFile ("TerrainInstrument")
                 .getChildFile (kRegNames[juce::jlimit (0, 2, kind)]);
    }
    const char* const kImportWild = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";
}

void TerrainInstrumentAudioProcessor::addImportPath (int kind, const juce::String& path)
{
    const int i = juce::jlimit (0, 2, kind);
    juce::File f (path);
    if (! f.exists()) return;
    if (f.isDirectory()) { if (! importFolders_[i].contains (path)) importFolders_[i].add (path); }
    else                 { if (! importFiles_[i].contains (path))   importFiles_[i].add (path); }
    saveImportsRegistry (i);
}

void TerrainInstrumentAudioProcessor::removeImportPath (int kind, const juce::String& path)
{
    const int i = juce::jlimit (0, 2, kind);
    importFiles_[i].removeString (path);      // remove a single import OR
    importFolders_[i].removeString (path);    // a whole user folder (only one array holds it) — file on disk untouched
    saveImportsRegistry (i);
}

juce::String TerrainInstrumentAudioProcessor::getImportsJson (int kind)
{
    const int idx = juce::jlimit (0, 2, kind);
    juce::Array<juce::var> files;
    juce::StringArray deadFiles;   // fb73 — a single import whose file is gone (Finder-deleted) is PRUNED from the
                                   // registry, not just hidden: Max's model is "delete it in the OS folder = it's gone".
                                   // FOLDERS are NOT pruned (an unmounted drive would lose them permanently; they just hide).
    for (auto& p : importFiles_[idx])
    {
        juce::File f (p);
        if (! f.existsAsFile()) { deadFiles.add (p); continue; }
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("name", f.getFileNameWithoutExtension());
        o->setProperty ("path", f.getFullPathName());
        files.add (juce::var (o.get()));
    }
    if (! deadFiles.isEmpty())
    {
        for (auto& p : deadFiles) importFiles_[idx].removeString (p);
        saveImportsRegistry (idx);
    }
    juce::Array<juce::var> folders;
    for (auto& p : importFolders_[idx])
    {
        juce::File d (p);
        if (! d.isDirectory()) continue;
        auto found = d.findChildFiles (juce::File::findFiles, false, kImportWild);
        found.sort();
        juce::Array<juce::var> items;
        for (auto& cf : found)
        {
            juce::DynamicObject::Ptr io = new juce::DynamicObject();
            io->setProperty ("name", cf.getFileNameWithoutExtension());
            io->setProperty ("path", cf.getFullPathName());
            items.add (juce::var (io.get()));
        }
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("name",  d.getFileName());
        o->setProperty ("path",  d.getFullPathName());
        o->setProperty ("count", items.size());
        o->setProperty ("items", items);
        folders.add (juce::var (o.get()));
    }
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("files",   files);
    root->setProperty ("folders", folders);
    return juce::JSON::toString (juce::var (root.get()));
}

void TerrainInstrumentAudioProcessor::saveImportsRegistry (int kind)
{
    const int idx = juce::jlimit (0, 2, kind);
    juce::Array<juce::var> f, d;
    for (auto& p : importFiles_[idx])   f.add (p);
    for (auto& p : importFolders_[idx]) d.add (p);
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("files", f);
    root->setProperty ("folders", d);
    auto file = importsRegPath (idx);
    file.getParentDirectory().createDirectory();                        // best-effort (may fail in a sandbox)
    file.replaceWithText (juce::JSON::toString (juce::var (root.get())));// best-effort — in-memory registry still works this session
}

void TerrainInstrumentAudioProcessor::loadImportsRegistry ()
{
    for (int k = 0; k < 3; ++k)   // fb74 — 0 noise · 1 wavetable · 2 sample
    {
        auto file = importsRegPath (k);
        if (! file.existsAsFile()) continue;
        auto v = juce::JSON::parse (file.loadFileAsString());
        if (auto* o = v.getDynamicObject())
        {
            importFiles_[k].clear(); importFolders_[k].clear();
            if (auto* fa = o->getProperty ("files").getArray())   for (auto& e : *fa) importFiles_[k].add (e.toString());
            if (auto* da = o->getProperty ("folders").getArray()) for (auto& e : *da) importFolders_[k].add (e.toString());
        }
    }
}

void TerrainInstrumentAudioProcessor::rebuildMorphIfNeeded (MorphSlot& slot, int oscIdx,
                                                            const juce::String& presetId,
                                                            const juce::String& modeId,
                                                            const juce::String& amtId)
{
    const int   preset = (int) *apvts.getRawParameterValue (presetId);
    const int   mode   = (int) *apvts.getRawParameterValue (modeId);
    // fb76 — SPECTRAL LFO MOD: use the EFFECTIVE amount the audio thread publishes each block
    // (base + LFO, quantized to 1/128 while a route is live so micro-wiggles don't churn; EXACT
    // raw pass-through when unmodded). -1 = audio thread hasn't run yet → raw param fallback.
    // Rebuild churn stays naturally throttled by the retireCooldown below (~20 Hz worst case),
    // entirely on the message thread — the audio thread never pays for a morph rebuild.
    const float effAmt = spectralEffAmt_[juce::jlimit (0, 3, oscIdx)].load (std::memory_order_relaxed);
    const float amount = (effAmt >= 0.0f) ? effAmt : apvts.getRawParameterValue (amtId)->load();   // ->load(): atomic<float> can't deduce in a ternary

    // fb253 — the morph SOURCE is the loaded IMPORT if one exists, else the factory preset spec.
    const int  oi        = juce::jlimit (0, 3, oscIdx);
    const tw::Wavetable* imp = importSlot_[(size_t) oi].live.load (std::memory_order_acquire);
    const bool hasImport = (imp != nullptr);
    const int  impEpoch  = hasImport ? imp->buildEpoch() : -1;

    // None (or zero amount) → publish nullptr; voices then read the RAW import (fb253) or the bank.
    if (mode <= 0 || amount <= 0.0f)
    {
        if (slot.live.load (std::memory_order_relaxed) != nullptr)
            slot.retireCooldown = 2;   // voices may still be mid-block on the retiring buffer
        slot.live.store (nullptr, std::memory_order_release);
        slot.builtPreset = preset;
        slot.builtMode   = mode;
        slot.builtAmount = amount;
        slot.builtImportPtr   = imp;         // fb253
        slot.builtImportEpoch = impEpoch;
        return;
    }

    // Nothing changed since the last build → skip. fb253 — the SOURCE identity is the import
    // (pointer + buildEpoch) when one is loaded, else the preset index; a re-import re-morphs.
    const bool srcSame = hasImport
        ? (imp == slot.builtImportPtr && impEpoch == slot.builtImportEpoch)
        : (slot.builtImportPtr == nullptr && preset == slot.builtPreset);
    if (srcSame && mode == slot.builtMode
        && std::abs (amount - slot.builtAmount) < 1.0e-4f)
        return;

    // RETIRE COOLDOWN — audioReadingIdx only refreshes at block START, so for up to a
    // full audio block after a publish, voices can still be rendering from the buffer
    // it says they left. Two 60 Hz ticks (~33ms) safely outlives any sane block size.
    if (slot.retireCooldown > 0) { --slot.retireCooldown; return; }

    // Never rebuild the LIVE buffer, and never one the audio thread reports reading.
    const int target = slot.buildIdx;
    if (slot.live.load (std::memory_order_relaxed) == &slot.buf[target])
        return;
    if (slot.audioReadingIdx.load (std::memory_order_acquire) == target)
        return;   // try again on the next tick (after the audio thread moves off it)

    // Mark the build in flight FIRST: any resolve during the rebuild parks voices on
    // the plain bank table (audible) instead of a half-zeroed morph buffer (silence).
    // fb253 — pick the morph source spec: a cached analysis of the loaded IMPORT (re-derived only when
    // the import changes — toSpec is 16 FFTs), else the factory preset spec. Both are 16-frame specs, so
    // morph-of-import behaves exactly like morph-of-preset (raw import still plays full-res when morph off).
    tw::WavetableSpec presetSpec;
    const tw::WavetableSpec* srcSpec;
    if (hasImport)
    {
        if (imp != importSpecSrc_[oi] || impEpoch != importSpecEpoch_[oi])
        {
            importSpec_[oi]      = imp->toSpec();
            importSpecSrc_[oi]   = imp;
            importSpecEpoch_[oi] = impEpoch;
        }
        srcSpec = &importSpec_[oi];
    }
    else
    {
        presetSpec = tw::WavetableBank::specForPreset (preset);
        srcSpec    = &presetSpec;
    }

    slot.ready[target].store (false, std::memory_order_release);
    slot.buf[target].buildFromSpec (
        tw::SpectralMorph::apply (*srcSpec, (tw::SpectralMode) mode, amount));
    slot.ready[target].store (true, std::memory_order_release);

    slot.live.store (&slot.buf[target], std::memory_order_release);
    slot.buildIdx  ^= 1;
    slot.retireCooldown = 2;   // let in-flight blocks leave the buffer we just retired
    slot.builtPreset = preset;
    slot.builtMode   = mode;
    slot.builtAmount = amount;
    slot.builtImportPtr   = imp;         // fb253 — remember the morphed source so a re-import re-morphs
    slot.builtImportEpoch = impEpoch;
}

// fb151 — THE PHANTOM-DROP FIX (Max live: "hover links it... flashing constantly").
// juce::ModifierKeys::getCurrentModifiersRealtime() on macOS refreshes only KEYBOARD
// flags ([NSEvent modifierFlags]); its mouse-button bits are set solely by JUCE peer
// mouse handlers — and an LFO-chip drag starts INSIDE the WKWebView, which JUCE never
// sees. The old check read "released" for the entire drag, so the 60Hz timer declared
// a drop every tick (assigning whatever was hovered) while the next JS move re-armed
// phase 0: the 0↔1 strobe. Ask the window server instead — +[NSEvent pressedMouseButtons]
// bit 0 is the physical left button for the whole session, any app, any view, no TCC
// prompt (JUCE itself polls it: juce_NSViewComponentPeer_mac.mm:1867).
bool TerrainInstrumentAudioProcessor::physicalLeftButtonDown()
{
   #if JUCE_MAC
    using Fn = unsigned long (*) (id, SEL);
    if (id cls = (id) objc_getClass ("NSEvent"))
        return (reinterpret_cast<Fn> (&objc_msgSend) (cls, sel_registerName ("pressedMouseButtons")) & 1UL) != 0;
    return false;
   #else
    return juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown();   // Windows realtime IS physical (GetKeyState)
   #endif
}

void TerrainInstrumentAudioProcessor::timerCallback()
{
    // fb149 — NATIVE mod-drag tracking: while an LFO drag is live, the PROCESSOR follows
    // the real mouse (Desktop) and detects the release itself. WebKit's event delivery
    // outside a window can never strand a cross-window drag, and screen coords are
    // zoom-proof (the per-window getModDrag natives transform to local).
    if (modDragPhase_ == 0)
    {
        auto ms = juce::Desktop::getInstance().getMainMouseSource();   // by-value handle
        const auto p = ms.getScreenPosition();
        modDragX_ = (float) p.x; modDragY_ = (float) p.y; ++modDragSeq_;
        if (! physicalLeftButtonDown())   // fb151 — the JUCE realtime check can't see WKWebView-held buttons
        { modDragPhase_ = 1; modDragIdleTicks_ = 0; ++modDragSeq_; }   // native drop
    }
    else if (modDragPhase_ == 1 && ++modDragIdleTicks_ > 12)
    { modDragPhase_ = 2; modDragIdleTicks_ = 0; }                      // fb150 — consumed; back to idle

    rebuildMorphIfNeeded (morphA_, 0, ParameterIDs::SYN_OSC_A_WT_PRESET,
                          ParameterIDs::SYN_OSC_A_SPECTRAL_TYPE,
                          ParameterIDs::SYN_OSC_A_SPECTRAL_AMT);
    rebuildMorphIfNeeded (morphB_, 1, ParameterIDs::SYN_OSC_B_WT_PRESET,
                          ParameterIDs::SYN_OSC_B_SPECTRAL_TYPE,
                          ParameterIDs::SYN_OSC_B_SPECTRAL_AMT);
    rebuildMorphIfNeeded (morphC_, 2, ParameterIDs::SYN_OSC_C_WT_PRESET,
                          ParameterIDs::SYN_OSC_C_SPECTRAL_TYPE,
                          ParameterIDs::SYN_OSC_C_SPECTRAL_AMT);
    rebuildMorphIfNeeded (morphD_, 3, ParameterIDs::SYN_OSC_D_WT_PRESET,
                          ParameterIDs::SYN_OSC_D_SPECTRAL_TYPE,
                          ParameterIDs::SYN_OSC_D_SPECTRAL_AMT);
    // GEODE — analyze any SPEC oscillator's source into partials+noise (off the audio thread).
    rebuildGeodeIfNeeded (0); rebuildGeodeIfNeeded (1);
    rebuildGeodeIfNeeded (2); rebuildGeodeIfNeeded (3);
}

// No sample loaded → a default harmonic (saw) store so GEODE sounds the instant it's selected.
static void buildDefaultGeodeStore (tw::GeodeFrameStore& out)
{
    constexpr int WF = 4, WP = 48;
    std::vector<float> wr ((size_t) WF * WP, 0.f), wa ((size_t) WF * WP, 0.f);
    for (int f = 0; f < WF; ++f)
        for (int h = 1; h <= WP; ++h)
        { wr[(size_t) f * WP + (h - 1)] = (float) h; wa[(size_t) f * WP + (h - 1)] = 1.f / (float) h; }
    tw::GeodeAnalyzer::buildFromWave (wr.data(), wa.data(), WF, WP, out);
}

void TerrainInstrumentAudioProcessor::rebuildGeodeIfNeeded (int o)
{
    static const char* const ENG[4] = { ParameterIDs::SYN_OSC_A_ENGINE, ParameterIDs::SYN_OSC_B_ENGINE,
                                        ParameterIDs::SYN_OSC_C_ENGINE, ParameterIDs::SYN_OSC_D_ENGINE };
    static const char* const WTP[4] = { ParameterIDs::SYN_OSC_A_WT_PRESET, ParameterIDs::SYN_OSC_B_WT_PRESET,
                                        ParameterIDs::SYN_OSC_C_WT_PRESET, ParameterIDs::SYN_OSC_D_WT_PRESET };
    GeodeSlot& slot = geodeSlot_[o];
    const int engineIdx = (int) *rawParam (ENG[o]);
    if (engineIdx != 3)   // not GEODE (Engine::SPEC = 3) → publish nothing, force re-analyze on return
    {
        slot.live.store (nullptr);
        slot.built = false;              // force re-analyze when this osc returns to GEODE
        slot.builtEngine = engineIdx;
        return;
    }
    tw::SampleBuffer& sb = getOscSampleBuffer (o);
    auto bp = sb.load();
    const int   wtPreset = (int) *rawParam (WTP[o]);
    const void* srcPtr   = (bp && bp->getNumSamples() > 1) ? (const void*) bp.get() : nullptr;

    // change-gate: same source + engine + (wavetable) same preset → skip the heavy analysis
    if (slot.built && slot.builtSample == srcPtr && slot.builtEngine == engineIdx
        && (srcPtr != nullptr || slot.builtWtPreset == wtPreset))
        return;

    const int bi = slot.buildIdx ^ 1;   // build into the buffer the audio thread is NOT reading
    if (srcPtr != nullptr)
    {
        const int nCh = bp->getNumChannels();
        const int nSm = bp->getNumSamples();
        const double nr = sb.getSampleRate();
        std::vector<float> mono ((size_t) nSm);
        const float* const* rp = bp->getArrayOfReadPointers();
        for (int i = 0; i < nSm; ++i)
        {
            float s = rp[0][i];
            if (nCh > 1) s = 0.5f * (s + rp[1][i]);
            mono[(size_t) i] = s;
        }
        tw::GeodeAnalyzer::analyzeSample (mono.data(), nSm, nr > 0.0 ? nr : getSampleRate(), slot.buf[bi]);
    }
    else
    {
        buildDefaultGeodeStore (slot.buf[bi]);
    }

    if (slot.buf[bi].valid)
    {
        slot.buildIdx      = bi;
        slot.live.store (&slot.buf[bi]);
        slot.built         = true;
        slot.builtSample   = srcPtr;
        slot.builtEngine   = engineIdx;
        slot.builtWtPreset = wtPreset;
    }
}

//==============================================================================
void TerrainInstrumentAudioProcessor::initializePresets()
{
    //                                    grSz   dens  spray pitch  drift  frz    mix    wow    sat   hiss  outGn  mMix xyEn xyMd xySpd  gSync  grEn  tpEn
    presets = {
        { "Init",                          80.f,  20.f, 40.f,  0.f,   0.f,   0.f,  50.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Soft Keys",                    332.5f, 29.8f, 7.f,  0.f,  17.9f,  0.f,  82.f, 39.5f, 54.5f, 5.f,  7.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Movements",                     99.3f, 34.f, 43.6f, 0.f,  57.7f,  0.f,  50.f,  0.f,   0.f,  0.f,  7.f, 100.f, 1.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Super Messed Up",               55.4f,  7.1f, 31.9f, 0.f, 24.9f,  0.f,  74.f, 43.f,  12.f,  3.f,  3.5f, 100.f, 1.f, 1.f, 0.472f, 0.f, 1.f, 1.f },
        { "Tape Master",                   60.3f, 80.1f, 11.9f, 0.f,  0.f,   0.f,  39.f, 46.f,  70.f,  3.f,  0.f, 100.f, 0.f, 1.f, 0.472f, 0.f, 1.f, 1.f },
        { "Delay The Pluck",              500.f,   3.3f,  3.5f, 12.f,  0.f,   0.f,  40.f, 26.5f, 22.5f, 0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Synced Grains",                268.7f,  5.8f,  9.5f, 0.f,   0.f,   0.f,  67.5f, 17.5f, 31.5f, 1.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Clown Delay",                  114.3f, 16.8f,  0.f,  0.f,   0.f,   0.f,  80.5f, 17.5f,  7.f,  1.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Instant Pad",                  152.1f, 26.8f,  5.f,  0.f,   3.f,   0.f,  50.f, 41.f,  13.f,  0.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Slow Changes",                 346.5f, 43.8f, 83.1f, 0.f,  65.8f,  0.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 1.f, 0.f, 0.f,   1.f, 1.f, 1.f },
        { "Mood",                          156.f,  7.8f,  9.f,  0.f,  23.1f,  0.f,  50.f, 40.f,  32.5f, 1.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Soundscape",                   220.2f, 54.9f, 81.5f, 0.f,   0.f,   0.f, 100.f, 44.f,  20.5f, 0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Use A Key",                    103.3f, 28.3f,  1.f, -12.f,  0.f,   0.f,  50.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 1.f, 0.f,   1.f, 1.f, 1.f },
        { "Blank Forms",                   17.5f, 100.f, 25.f,  0.f,   0.f,   0.f, 100.f,  9.5f, 29.f,  5.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Habits",                       261.1f, 55.2f,  0.f,  0.f,  71.f,   0.f,  20.5f, 44.f,   9.5f, 8.f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Pitch Drifter",                146.3f, 24.2f,  2.5f,-12.f, 100.f,  0.f, 100.f, 28.f,  30.5f, 0.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Mandalorian",                  240.1f, 13.5f, 68.9f,-12.f,  35.6f, 0.f, 100.f, 28.f,  30.5f, 0.5f, 0.f, 100.f, 1.f, 1.f, 0.056f, 0.f, 1.f, 1.f },
        { "See The Light",                280.7f, 53.f,  32.6f, 12.f,  31.9f, 0.f, 100.f, 28.f,  30.5f, 0.5f, 0.f, 100.f, 0.f, 1.f, 0.056f, 0.f, 1.f, 1.f },
        { "Back To The Future",           325.8f,  1.f, 100.f,  0.f,  39.f,   0.f,  50.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Saturation",                   268.9f, 10.5f,  1.5f, 0.f,   0.f,   0.f,  50.f, 19.f,  83.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Chop Shop",                     49.3f, 100.f, 40.f,  0.f,   0.f,   0.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Depressed",                    171.2f, 81.2f, 40.f, -12.f,  0.f,   0.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Deep Rest",                    168.3f, 81.2f, 40.f, -12.f, 25.f,   0.f, 100.f, 48.5f, 54.5f, 3.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Final Tape",                   183.3f, 71.2f,  3.f,  0.f,   0.f,   0.f,  40.f, 33.f,  53.f, 15.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        // ── Wander presets ──
        { "Gentle Wander",                150.f,  25.f, 20.f,  0.f,  15.f,   0.f,  55.f, 10.f,  15.f,  2.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Scattered",                     80.f,  65.f, 45.f,  0.f,  55.f,   0.f,  70.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Disintegration",               120.f,  12.f, 30.f, 12.f,  85.f,   0.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Breathing Texture",            350.f,  60.f, 15.f,  0.f,  25.f,   0.f,  60.f,  5.f,   8.f,  1.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Glitch Cloud",                  20.f,  15.f, 50.f,  0.f,  70.f,   0.f,  80.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        // ── Freeze presets ──
        { "Frozen Cathedral",             200.f,  60.f, 30.f,  0.f,  20.f, 100.f,  80.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Ghost Layer",                  120.f,  30.f, 20.f,  0.f,  10.f,  60.f,  65.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Living Pad",                   250.f,  50.f, 25.f, 12.f,  40.f, 100.f,  90.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Instant Eno",                  400.f,  10.f, 80.f,  0.f,  15.f,  85.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Sub Freeze",                   300.f,  40.f, 15.f,-12.f,   5.f, 100.f,  85.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Thaw",                         150.f,  35.f, 40.f,  0.f,  60.f,  40.f,  70.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Time Crystal",                 180.f,  45.f,100.f,  0.f,   0.f, 100.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
    };

    // ── Harmonic Sculptor presets (Studio v2.0) ──
    auto makeSculptorPreset = [](const char* name, float sculpt, float weave, float tilt) {
        PresetData p;
        p.name        = name;
        p.grainSize   = 80.f;   p.density   = 20.f;   p.spray  = 40.f;
        p.pitch       = 0.f;    p.drift     = 0.f;    p.freeze = 0.f;
        p.mix         = 50.f;
        p.wowFlutter  = 0.f;    p.saturation = 0.f;   p.hiss   = 0.f;  // unused on Studio (sculptor mode)
        p.outputGain  = 0.f;
        // masterMix, loopLength/Feedback/Degrade/Speed, xyAuto*, grainSync*, grainEngineEnabled, tapeEnabled,
        // tapeMachine (defaults to 0 = Studio), wanderLinked, grainFilter, space*, eq* — all take defaults
        p.studioSculpt = sculpt;
        p.studioWeave  = weave;
        p.studioTilt   = tilt;
        p.tag          = "TAPE";
        return p;
    };

    presets.push_back(makeSculptorPreset("Crystal Console",   15.f,  40.f,  20.f));
    presets.push_back(makeSculptorPreset("Warm Web",          35.f,  70.f, -30.f));
    presets.push_back(makeSculptorPreset("Flat Crunch",       55.f,  20.f,   0.f));
    presets.push_back(makeSculptorPreset("Shimmer Density",   40.f,  85.f,  40.f));
    presets.push_back(makeSculptorPreset("Harmonic Blanket",  70.f,  95.f, -10.f));
    presets.push_back(makeSculptorPreset("Sculptor Extreme", 100.f, 100.f,   0.f));

    numFactoryPresets = static_cast<int>(presets.size());  // All presets above are factory

    // Load any additional user presets from disk
    loadUserPresetsFromFile();
}

void TerrainInstrumentAudioProcessor::loadPreset(int index)
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;

    currentPresetIndex.store(index);
    const auto& p = presets[static_cast<size_t>(index)];

    auto setParam = [this](const char* id, float value)
    {
        if (auto* param = apvts.getParameter(id))
            param->setValueNotifyingHost(param->convertTo0to1(value));
    };

    setParam(ParameterIDs::GRAIN_SIZE,   p.grainSize);
    setParam(ParameterIDs::DENSITY,      p.density);
    setParam(ParameterIDs::SPRAY,        p.spray);
    setParam(ParameterIDs::PITCH,        p.pitch);
    setParam(ParameterIDs::WANDER,       p.drift);
    setParam(ParameterIDs::FREEZE,       p.freeze);
    setParam(ParameterIDs::GRAIN_FILTER, p.grainFilter);
    setParam(ParameterIDs::MIX,          p.mix);
    setParam(ParameterIDs::WOW_FLUTTER,  p.wowFlutter);
    setParam(ParameterIDs::SATURATION,   p.saturation);
    setParam(ParameterIDs::HISS,         p.hiss);
    setParam(ParameterIDs::WIRE_WOW,        p.wireWow);
    setParam(ParameterIDs::WIRE_SATURATION, p.wireSaturation);
    setParam(ParameterIDs::WIRE_HISS,       p.wireHiss);
    setParam (ParameterIDs::STUDIO_SCULPT, p.studioSculpt);
    setParam (ParameterIDs::STUDIO_WEAVE,  p.studioWeave);
    setParam (ParameterIDs::STUDIO_TILT,   p.studioTilt);
    setParam(ParameterIDs::OUTPUT_GAIN,  p.outputGain);
    setParam(ParameterIDs::MASTER_MIX,   p.masterMix);
    setParam(ParameterIDs::LOOP_LENGTH,   p.loopLength);
    setParam(ParameterIDs::LOOP_FEEDBACK, p.loopFeedback);
    setParam(ParameterIDs::LOOP_DEGRADE,  p.loopDegrade);
    setParam(ParameterIDs::LOOP_SPEED,    p.loopSpeed);
    setParam(ParameterIDs::SPACE_SIZE,   p.spaceSize);
    setParam(ParameterIDs::SPACE_DECAY,  p.spaceDecay);
    setParam(ParameterIDs::SPACE_TONE,   p.spaceTone);
    setParam(ParameterIDs::SPACE_MIX,    p.spaceMix);

    // Parametric EQ — 35 APVTS params + 2 UI-only filter-mode flags.
    setParam (ParameterIDs::EQ_MASTER_BYPASS, p.eqMasterBypass);
    setParam (ParameterIDs::EQ_HP_FREQ,   p.eqHpFreq);
    setParam (ParameterIDs::EQ_HP_SLOPE,  p.eqHpSlope);
    setParam (ParameterIDs::EQ_HP_BYPASS, p.eqHpBypass);
    setParam (ParameterIDs::EQ_LP_FREQ,   p.eqLpFreq);
    setParam (ParameterIDs::EQ_LP_SLOPE,  p.eqLpSlope);
    setParam (ParameterIDs::EQ_LP_BYPASS, p.eqLpBypass);
    setParam (ParameterIDs::EQ_B1_FREQ, p.eqB1Freq); setParam (ParameterIDs::EQ_B1_GAIN, p.eqB1Gain); setParam (ParameterIDs::EQ_B1_Q, p.eqB1Q); setParam (ParameterIDs::EQ_B1_BYPASS, p.eqB1Bypass);
    setParam (ParameterIDs::EQ_B2_FREQ, p.eqB2Freq); setParam (ParameterIDs::EQ_B2_GAIN, p.eqB2Gain); setParam (ParameterIDs::EQ_B2_Q, p.eqB2Q); setParam (ParameterIDs::EQ_B2_BYPASS, p.eqB2Bypass);
    setParam (ParameterIDs::EQ_B3_FREQ, p.eqB3Freq); setParam (ParameterIDs::EQ_B3_GAIN, p.eqB3Gain); setParam (ParameterIDs::EQ_B3_Q, p.eqB3Q); setParam (ParameterIDs::EQ_B3_BYPASS, p.eqB3Bypass);
    setParam (ParameterIDs::EQ_B4_FREQ, p.eqB4Freq); setParam (ParameterIDs::EQ_B4_GAIN, p.eqB4Gain); setParam (ParameterIDs::EQ_B4_Q, p.eqB4Q); setParam (ParameterIDs::EQ_B4_BYPASS, p.eqB4Bypass);
    setParam (ParameterIDs::EQ_B5_FREQ, p.eqB5Freq); setParam (ParameterIDs::EQ_B5_GAIN, p.eqB5Gain); setParam (ParameterIDs::EQ_B5_Q, p.eqB5Q); setParam (ParameterIDs::EQ_B5_BYPASS, p.eqB5Bypass);
    setParam (ParameterIDs::EQ_B6_FREQ, p.eqB6Freq); setParam (ParameterIDs::EQ_B6_GAIN, p.eqB6Gain); setParam (ParameterIDs::EQ_B6_Q, p.eqB6Q); setParam (ParameterIDs::EQ_B6_BYPASS, p.eqB6Bypass);
    setParam (ParameterIDs::EQ_B7_FREQ, p.eqB7Freq); setParam (ParameterIDs::EQ_B7_GAIN, p.eqB7Gain); setParam (ParameterIDs::EQ_B7_Q, p.eqB7Q); setParam (ParameterIDs::EQ_B7_BYPASS, p.eqB7Bypass);
    setParam (ParameterIDs::EQ_B1_HP_MODE, p.eqB1HpMode);
    setParam (ParameterIDs::EQ_B7_LP_MODE, p.eqB7LpMode);

    setParam (ParameterIDs::DLY_TIME,      p.dlyTime);
    setParam (ParameterIDs::DLY_FEEDBACK,  p.dlyFeedback);
    setParam (ParameterIDs::DLY_TONE,      p.dlyTone);
    setParam (ParameterIDs::DLY_CHARACTER, p.dlyCharacter);
    setParam (ParameterIDs::DLY_MOD,       p.dlyMod);
    setParam (ParameterIDs::DLY_MOD_RATE,  p.dlyModRate);
    setParam (ParameterIDs::DLY_MIX,       p.dlyMix);
    setParam (ParameterIDs::DLY_DUCK,      p.dlyDuck);
    setParam (ParameterIDs::DLY_MOD_WAVE,  p.dlyModWave);
    setParam (ParameterIDs::DLY_SYNC,      p.dlySync);
    setParam (ParameterIDs::DLY_SYNC_DIV,  p.dlySyncDiv);
    setParam (ParameterIDs::DLY_PITCH,     p.dlyPitch);
    setParam (ParameterIDs::DLY_WIDTH,     p.dlyWidth);
    setParam (ParameterIDs::CHORUS_AMOUNT,    p.chorusAmount);
    setParam (ParameterIDs::CHORUS_WIDTH,     p.chorusWidth);
    setParam (ParameterIDs::CHORUS_CHARACTER, p.chorusCharacter);

    // Restore XY automation state for this preset
    xyAutoEnabled.store(p.xyAutoEnabled);
    xyAutoMode.store(p.xyAutoMode);
    xyAutoSpeed.store(p.xyAutoSpeed);

    // Restore grain sync state
    grainSyncEnabled.store(p.grainSyncEnabled);

    // Restore grain engine on/off, tape on/off, and drift link states
    grainEngineEnabled.store(p.grainEngineEnabled);
    tapeEnabled.store(p.tapeEnabled);
    tapeLoopEnabled.store(p.tapeLoopEnabled);
    setParam(ParameterIDs::TAPE_MACHINE, p.tapeMachine);
    wanderLinked.store(p.wanderLinked);
    tapeLinkEnabled.store(p.tapeLinkEnabled);

    // Restore modulation state (LFOs + assignments) for this preset
    modStateJson = p.modState;
    if (modStateJson.isNotEmpty())
        modulationEngine.updateConfig(ModulationEngine::parseJSON(modStateJson));
    else
        modulationEngine.updateConfig(ModulationEngine::Config{});  // Clear all LFOs
}

int TerrainInstrumentAudioProcessor::getPresetCount() const
{
    return static_cast<int>(presets.size());
}

juce::String TerrainInstrumentAudioProcessor::getPresetName(int index) const
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return {};
    return presets[static_cast<size_t>(index)].name;
}

//==============================================================================
PresetData TerrainInstrumentAudioProcessor::captureCurrentParams() const
{
    PresetData p;
    p.grainSize  = rawParam (ParameterIDs::GRAIN_SIZE)->load();
    p.density    = rawParam (ParameterIDs::DENSITY)->load();
    p.spray      = rawParam (ParameterIDs::SPRAY)->load();
    p.pitch      = rawParam (ParameterIDs::PITCH)->load();
    p.drift      = rawParam (ParameterIDs::WANDER)->load();
    p.freeze     = rawParam (ParameterIDs::FREEZE)->load();
    p.grainFilter = rawParam (ParameterIDs::GRAIN_FILTER)->load();
    p.mix        = rawParam (ParameterIDs::MIX)->load();
    p.wowFlutter = rawParam (ParameterIDs::WOW_FLUTTER)->load();
    p.saturation = rawParam (ParameterIDs::SATURATION)->load();
    p.hiss       = rawParam (ParameterIDs::HISS)->load();
    p.wireWow        = rawParam (ParameterIDs::WIRE_WOW)       ->load();
    p.wireSaturation = rawParam (ParameterIDs::WIRE_SATURATION)->load();
    p.wireHiss       = rawParam (ParameterIDs::WIRE_HISS)      ->load();
    p.studioSculpt = rawParam (ParameterIDs::STUDIO_SCULPT)->load();
    p.studioWeave  = rawParam (ParameterIDs::STUDIO_WEAVE) ->load();
    p.studioTilt   = rawParam (ParameterIDs::STUDIO_TILT)  ->load();
    p.outputGain = rawParam (ParameterIDs::OUTPUT_GAIN)->load();
    p.masterMix  = rawParam (ParameterIDs::MASTER_MIX)->load();
    p.loopLength   = rawParam (ParameterIDs::LOOP_LENGTH)->load();
    p.loopFeedback = rawParam (ParameterIDs::LOOP_FEEDBACK)->load();
    p.loopDegrade  = rawParam (ParameterIDs::LOOP_DEGRADE)->load();
    p.loopSpeed    = rawParam (ParameterIDs::LOOP_SPEED)->load();
    p.spaceSize    = rawParam (ParameterIDs::SPACE_SIZE)->load();
    p.spaceDecay   = rawParam (ParameterIDs::SPACE_DECAY)->load();
    p.spaceTone    = rawParam (ParameterIDs::SPACE_TONE)->load();
    p.spaceMix     = rawParam (ParameterIDs::SPACE_MIX)->load();
    // Parametric EQ — 35 APVTS params + 2 UI-only filter-mode flags.
    p.eqMasterBypass = rawParam (ParameterIDs::EQ_MASTER_BYPASS)->load();
    p.eqHpFreq    = rawParam (ParameterIDs::EQ_HP_FREQ)  ->load();
    p.eqHpSlope   = rawParam (ParameterIDs::EQ_HP_SLOPE) ->load();
    p.eqHpBypass  = rawParam (ParameterIDs::EQ_HP_BYPASS)->load();
    p.eqLpFreq    = rawParam (ParameterIDs::EQ_LP_FREQ)  ->load();
    p.eqLpSlope   = rawParam (ParameterIDs::EQ_LP_SLOPE) ->load();
    p.eqLpBypass  = rawParam (ParameterIDs::EQ_LP_BYPASS)->load();
    p.eqB1Freq = rawParam (ParameterIDs::EQ_B1_FREQ)->load(); p.eqB1Gain = rawParam (ParameterIDs::EQ_B1_GAIN)->load(); p.eqB1Q = rawParam (ParameterIDs::EQ_B1_Q)->load(); p.eqB1Bypass = rawParam (ParameterIDs::EQ_B1_BYPASS)->load();
    p.eqB2Freq = rawParam (ParameterIDs::EQ_B2_FREQ)->load(); p.eqB2Gain = rawParam (ParameterIDs::EQ_B2_GAIN)->load(); p.eqB2Q = rawParam (ParameterIDs::EQ_B2_Q)->load(); p.eqB2Bypass = rawParam (ParameterIDs::EQ_B2_BYPASS)->load();
    p.eqB3Freq = rawParam (ParameterIDs::EQ_B3_FREQ)->load(); p.eqB3Gain = rawParam (ParameterIDs::EQ_B3_GAIN)->load(); p.eqB3Q = rawParam (ParameterIDs::EQ_B3_Q)->load(); p.eqB3Bypass = rawParam (ParameterIDs::EQ_B3_BYPASS)->load();
    p.eqB4Freq = rawParam (ParameterIDs::EQ_B4_FREQ)->load(); p.eqB4Gain = rawParam (ParameterIDs::EQ_B4_GAIN)->load(); p.eqB4Q = rawParam (ParameterIDs::EQ_B4_Q)->load(); p.eqB4Bypass = rawParam (ParameterIDs::EQ_B4_BYPASS)->load();
    p.eqB5Freq = rawParam (ParameterIDs::EQ_B5_FREQ)->load(); p.eqB5Gain = rawParam (ParameterIDs::EQ_B5_GAIN)->load(); p.eqB5Q = rawParam (ParameterIDs::EQ_B5_Q)->load(); p.eqB5Bypass = rawParam (ParameterIDs::EQ_B5_BYPASS)->load();
    p.eqB6Freq = rawParam (ParameterIDs::EQ_B6_FREQ)->load(); p.eqB6Gain = rawParam (ParameterIDs::EQ_B6_GAIN)->load(); p.eqB6Q = rawParam (ParameterIDs::EQ_B6_Q)->load(); p.eqB6Bypass = rawParam (ParameterIDs::EQ_B6_BYPASS)->load();
    p.eqB7Freq = rawParam (ParameterIDs::EQ_B7_FREQ)->load(); p.eqB7Gain = rawParam (ParameterIDs::EQ_B7_GAIN)->load(); p.eqB7Q = rawParam (ParameterIDs::EQ_B7_Q)->load(); p.eqB7Bypass = rawParam (ParameterIDs::EQ_B7_BYPASS)->load();
    p.eqB1HpMode = rawParam (ParameterIDs::EQ_B1_HP_MODE)->load();
    p.eqB7LpMode = rawParam (ParameterIDs::EQ_B7_LP_MODE)->load();
    p.xyAutoEnabled    = xyAutoEnabled.load();
    p.xyAutoMode       = xyAutoMode.load();
    p.xyAutoSpeed      = xyAutoSpeed.load();
    p.grainSyncEnabled = grainSyncEnabled.load();
    p.grainEngineEnabled = grainEngineEnabled.load();
    p.tapeEnabled        = tapeEnabled.load();
    p.tapeLoopEnabled    = tapeLoopEnabled.load();
    p.tapeMachine        = rawParam (ParameterIDs::TAPE_MACHINE)->load();
    p.wanderLinked        = wanderLinked.load();
    p.tapeLinkEnabled    = tapeLinkEnabled.load();
    p.modState            = modStateJson;  // Capture current LFO/mod config
    p.dlyTime      = rawParam (ParameterIDs::DLY_TIME)->load();
    p.dlyFeedback  = rawParam (ParameterIDs::DLY_FEEDBACK)->load();
    p.dlyTone      = rawParam (ParameterIDs::DLY_TONE)->load();
    p.dlyCharacter = rawParam (ParameterIDs::DLY_CHARACTER)->load();
    p.dlyMod       = rawParam (ParameterIDs::DLY_MOD)->load();
    p.dlyModRate   = rawParam (ParameterIDs::DLY_MOD_RATE)->load();
    p.dlyMix       = rawParam (ParameterIDs::DLY_MIX)->load();
    p.dlyDuck      = rawParam (ParameterIDs::DLY_DUCK)->load();
    p.dlyModWave   = rawParam (ParameterIDs::DLY_MOD_WAVE)->load();
    p.dlySync      = rawParam (ParameterIDs::DLY_SYNC)->load();
    p.dlySyncDiv   = rawParam (ParameterIDs::DLY_SYNC_DIV)->load();
    p.dlyPitch     = rawParam (ParameterIDs::DLY_PITCH)->load();
    p.dlyWidth     = rawParam (ParameterIDs::DLY_WIDTH)->load();
    p.chorusAmount    = rawParam (ParameterIDs::CHORUS_AMOUNT)   ->load();
    p.chorusWidth     = rawParam (ParameterIDs::CHORUS_WIDTH)    ->load();
    p.chorusCharacter = rawParam (ParameterIDs::CHORUS_CHARACTER)->load();
    return p;
}

int TerrainInstrumentAudioProcessor::saveNewPreset(const juce::String& name, const juce::String& tag)
{
    PresetData p = captureCurrentParams();
    p.name = name;
    p.tag = tag;
    presets.push_back(p);
    int newIdx = static_cast<int>(presets.size()) - 1;
    currentPresetIndex.store(newIdx);
    saveUserPresetsToFile();
    return newIdx;
}

void TerrainInstrumentAudioProcessor::renamePreset(int index, const juce::String& newName)
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;
    presets[static_cast<size_t>(index)].name = newName;
    saveUserPresetsToFile();
}

void TerrainInstrumentAudioProcessor::overwritePreset(int index)
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;
    juce::String name = presets[static_cast<size_t>(index)].name;
    juce::String tag  = presets[static_cast<size_t>(index)].tag;
    presets[static_cast<size_t>(index)] = captureCurrentParams();
    presets[static_cast<size_t>(index)].name = name;
    presets[static_cast<size_t>(index)].tag  = tag;
    saveUserPresetsToFile();
}

void TerrainInstrumentAudioProcessor::deletePreset(int index)
{
    if (index < numFactoryPresets || index >= static_cast<int>(presets.size()))
        return;
    presets.erase(presets.begin() + index);
    if (currentPresetIndex.load() >= static_cast<int>(presets.size()))
        currentPresetIndex.store(static_cast<int>(presets.size()) - 1);
    saveUserPresetsToFile();
}

juce::String TerrainInstrumentAudioProcessor::getPresetTag(int index) const
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return {};
    return presets[static_cast<size_t>(index)].tag;
}

void TerrainInstrumentAudioProcessor::setPresetTag(int index, const juce::String& tag)
{
    if (index < numFactoryPresets || index >= static_cast<int>(presets.size()))
        return;
    presets[static_cast<size_t>(index)].tag = tag;
    saveUserPresetsToFile();
}

juce::String TerrainInstrumentAudioProcessor::getCustomTags() const
{
    return customTags.joinIntoString(",");
}

void TerrainInstrumentAudioProcessor::setCustomTags(const juce::String& commaSeparated)
{
    customTags.clear();
    customTags.addTokens(commaSeparated, ",", "");
    customTags.removeEmptyStrings();
    saveUserPresetsToFile();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TerrainInstrumentAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::GRAIN_SIZE, 1 },
        "Grain Size",
        juce::NormalisableRange<float>(5.0f, 500.0f, 0.1f, 0.5f),
        80.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DENSITY, 1 },
        "Density",
        juce::NormalisableRange<float>(1.0f, 100.0f, 0.1f, 0.5f),
        20.0f,
        juce::AudioParameterFloatAttributes().withLabel("grains/s")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SPRAY, 1 },
        "Spray",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        40.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::PITCH, 1 },
        "Pitch",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("st")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::WANDER, 1 },
        "Wander",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::FREEZE, 1 },
        "Freeze",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::MIX, 1 },
        "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,   // stays 50 — the OFF default lives in grainEngineEnabled, so powering on is instantly audible
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Grain filter: 0 = HP, 50 = bypass, 100 = LP
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::GRAIN_FILTER, 1 },
        "Grain Filter",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("")));

    // Tape parameters
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::WOW_FLUTTER, 1 },
        "Wow/Flutter",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SATURATION, 1 },
        "Saturation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::HISS, 1 },
        "Hiss",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Wire-specific wow/sat/hiss — independent of Cassette so each machine
    // can be tuned separately when LINK is on.
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::WIRE_WOW, 1 },
        "Wire Wow/Flutter",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::WIRE_SATURATION, 1 },
        "Wire Saturation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::WIRE_HISS, 1 },
        "Wire Hiss",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Tape machine selection
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::TAPE_MACHINE, 1 },
        "Tape Machine",
        juce::StringArray { "Studio", "Cassette", "Wire" },
        0));

    // Harmonic Sculptor (Studio v2.0)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::STUDIO_SCULPT, 1 },
        "Studio Sculpt",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::STUDIO_WEAVE, 1 },
        "Studio Weave",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::STUDIO_TILT, 1 },
        "Studio Tilt",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Output gain: -12 dB to +12 dB, default 0 dB
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::OUTPUT_GAIN, 1 },
        "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Master dry/wet mix: 0-100%, default 100% (fully wet)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::MASTER_MIX, 1 },
        "Master Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Tape loop params
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::LOOP_LENGTH, 1 },
        "Loop Length",
        juce::NormalisableRange<float>(0.0f, 6.0f, 1.0f),
        3.0f,
        juce::AudioParameterFloatAttributes().withLabel("")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::LOOP_FEEDBACK, 1 },
        "Loop Feedback",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        85.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::LOOP_DEGRADE, 1 },
        "Loop Degrade",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        30.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::LOOP_SPEED, 1 },
        "Loop Speed",
        juce::NormalisableRange<float>(0.0f, 9.0f, 0.01f),
        6.0f,
        juce::AudioParameterFloatAttributes().withLabel("")));

    // Space reverb params (0-100%)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SPACE_SIZE, 1 },
        "Space Size",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SPACE_DECAY, 1 },
        "Space Decay",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SPACE_TONE, 1 },
        "Space Tone",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SPACE_MIX, 1 },
        "Space Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Parametric EQ — 7 bands + HP/LP edge cuts
    {
        using NRange = juce::NormalisableRange<float>;
        // Logarithmic frequency range, like a Pro-Q.
        auto freqRange = []() { NRange r (20.0f, 20000.0f, 0.1f); r.setSkewForCentre (1000.0f); return r; }();
        auto qRange    = []() { NRange r (0.1f, 30.0f, 0.001f); r.setSkewForCentre (2.0f); return r; }();
        // Symmetrically log-spaced defaults across [20Hz, 20kHz] — bands at 1/8..7/8 of log range.
        constexpr float defaultFreqs[ParametricEQ::NUM_BANDS] = { 50.f, 110.f, 270.f, 630.f, 1500.f, 3500.f, 8500.f };

        layout.add (std::make_unique<juce::AudioParameterBool>  (ParameterIDs::EQ_MASTER_BYPASS, "EQ Bypass", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterIDs::EQ_HP_FREQ,  "EQ HP Freq",  freqRange, 35.f));
        layout.add (std::make_unique<juce::AudioParameterChoice>(ParameterIDs::EQ_HP_SLOPE, "EQ HP Slope", juce::StringArray { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1));
        layout.add (std::make_unique<juce::AudioParameterBool>  (ParameterIDs::EQ_HP_BYPASS, "EQ HP Bypass", true));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterIDs::EQ_LP_FREQ,  "EQ LP Freq",  freqRange, 16000.f));
        layout.add (std::make_unique<juce::AudioParameterChoice>(ParameterIDs::EQ_LP_SLOPE, "EQ LP Slope", juce::StringArray { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 0));
        layout.add (std::make_unique<juce::AudioParameterBool>  (ParameterIDs::EQ_LP_BYPASS, "EQ LP Bypass", true));

        const char* freqIds [7] = { ParameterIDs::EQ_B1_FREQ,   ParameterIDs::EQ_B2_FREQ,   ParameterIDs::EQ_B3_FREQ,   ParameterIDs::EQ_B4_FREQ,   ParameterIDs::EQ_B5_FREQ,   ParameterIDs::EQ_B6_FREQ,   ParameterIDs::EQ_B7_FREQ };
        const char* gainIds [7] = { ParameterIDs::EQ_B1_GAIN,   ParameterIDs::EQ_B2_GAIN,   ParameterIDs::EQ_B3_GAIN,   ParameterIDs::EQ_B4_GAIN,   ParameterIDs::EQ_B5_GAIN,   ParameterIDs::EQ_B6_GAIN,   ParameterIDs::EQ_B7_GAIN };
        const char* qIds    [7] = { ParameterIDs::EQ_B1_Q,      ParameterIDs::EQ_B2_Q,      ParameterIDs::EQ_B3_Q,      ParameterIDs::EQ_B4_Q,      ParameterIDs::EQ_B5_Q,      ParameterIDs::EQ_B6_Q,      ParameterIDs::EQ_B7_Q };
        const char* bypIds  [7] = { ParameterIDs::EQ_B1_BYPASS, ParameterIDs::EQ_B2_BYPASS, ParameterIDs::EQ_B3_BYPASS, ParameterIDs::EQ_B4_BYPASS, ParameterIDs::EQ_B5_BYPASS, ParameterIDs::EQ_B6_BYPASS, ParameterIDs::EQ_B7_BYPASS };
        for (int b = 0; b < 7; ++b)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (freqIds[b], juce::String ("EQ B") + juce::String (b + 1) + " Freq", freqRange, defaultFreqs[b]));
            layout.add (std::make_unique<juce::AudioParameterFloat> (gainIds[b], juce::String ("EQ B") + juce::String (b + 1) + " Gain", juce::NormalisableRange<float> (-24.f, 24.f, 0.01f), 0.0f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (qIds[b],    juce::String ("EQ B") + juce::String (b + 1) + " Q",    qRange, 0.707f));
            layout.add (std::make_unique<juce::AudioParameterBool>  (bypIds[b],  juce::String ("EQ B") + juce::String (b + 1) + " Bypass", false));
        }

        // Filter-mode flags for the corner bands (UI-only — restores _asHighPass /
        // _asLowPass visual state. Audio behaviour is fully captured by the bell +
        // HP/LP bypass/freq/slope params above.)
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterIDs::EQ_B1_HP_MODE, "EQ B1 HP Mode", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterIDs::EQ_B7_LP_MODE, "EQ B7 LP Mode", false));
    }

    // MF-104S delay (v6)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_TIME, 1 },
        "Delay Time",
        juce::NormalisableRange<float>(5.0f, 1500.0f, 0.1f, 0.4f),
        350.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_FEEDBACK, 1 },
        "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 1.10f, 0.001f),
        0.45f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_TONE, 1 },
        "Delay Tone",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_CHARACTER, 1 },
        "Delay Character",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,                                   // Instrument default 0 (no Moog drift on first load)
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_MOD, 1 },
        "Delay Mod Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,                                   // Instrument default 0
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_MOD_RATE, 1 },
        "Delay Mod Rate",
        juce::NormalisableRange<float>(0.05f, 8.0f, 0.001f, 0.3f),
        0.05f,                                  // Instrument default at the bottom of the range
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_MIX, 1 },
        "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,   // fresh instance: effects OFF (was 0.30 — a new instance shipped 30% wet delay)
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_DUCK, 1 },
        "Delay Duck",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_FREEZE, 1 },
        "Delay Freeze",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        0.0f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::DLY_MOD_WAVE, 1 },
        "Delay Mod Wave",
        juce::StringArray { "Sine", "Tri", "Square", "Ramp", "Saw", "S&H", "Chaos" },
        0));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::DLY_SYNC, 1 },
        "Delay Sync",
        juce::StringArray { "Free", "Sync" },
        0));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::DLY_SYNC_DIV, 1 },
        "Delay Sync Division",
        juce::StringArray { "1/32", "1/16T", "1/16", "1/8T", "1/8.", "1/8", "1/4T", "1/4", "1/2" },
        5));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::DLY_PITCH, 1 },
        "Delay Pitch",
        juce::StringArray { "OFF", "-12", "-7", "+7", "+12" },
        0));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::DLY_WIDTH, 1 },
        "Delay Width",
        juce::StringArray { "Mono", "Stereo", "Ping" },
        1));

    // Chorus (v6)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::CHORUS_AMOUNT, 1 },
        "June Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        0.0f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::CHORUS_WIDTH, 1 },
        "June Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        0.7f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::CHORUS_CHARACTER, 1 },
        "June Character",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        0.3f,
        juce::AudioParameterFloatAttributes()));

    // Sampler params (Terrain Instrument v0a)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::ATTACK_MS, 1 },
        "Attack",
        juce::NormalisableRange<float>(0.0f, 2000.0f, 0.0f, 0.4f),
        5.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::RELEASE_MS, 1 },
        "Release",
        juce::NormalisableRange<float>(1.0f, 5000.0f, 0.0f, 0.4f),
        800.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { ParameterIDs::ROOT_NOTE, 1 },
        "Root Note", 0, 127, 60));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SLICE_MODE, 1 },
        "Mode",
        juce::StringArray { "PITCH", "SLICE" }, 0));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SLICE_SUB_MODE, 1 },
        "Slice Sub-Mode",
        juce::StringArray { "CHOP", "CHROMATIC", "RANDOM", "LAYER" }, 0));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SAMPLE_LOOP_MODE, 1 },
        "Sample Loop",
        juce::StringArray { "ONE-SHOT", "LOOP" }, 0));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::CHOP_FADE_MS, 1 },
        "Chop Fade",
        juce::NormalisableRange<float>(0.0f, 50.0f, 0.1f),
        5.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // ── Synth section (Phase 1 MPV — see Design/v1-syn-spec.md) ──────────
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_ENGINE, 1 },
        "Synth OSC A Engine",
        juce::StringArray { "WT", "SAMP", "GRAN", "SPEC", "FM", "HARM", "MODAL" },
        0));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_OCT, 1 },
        "Synth OSC A Octave", -3, 3, 0));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SEMI, 1 },
        "Synth OSC A Semitone", -12, 12, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_CENT, 1 },
        "Synth OSC A Cents",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_LEVEL, 1 },
        "Synth OSC A Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.7f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_PAN, 1 },
        "Synth OSC A Pan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_ENABLE, 1 }, "Osc A Enable", true));   // fresh instance: only OSC A on
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_MUTE, 1 }, "Osc A Mute", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SOLO, 1 }, "Osc A Solo", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_CUT, 1 },
        "Synth Filter 1 Cutoff",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.25f),
        20000.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_RES, 1 },
        "Synth Filter 1 Resonance",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_KEYTRACK, 1 },
        "Synth Filter 1 Keytrack",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));

    // ── Batch 1 — Modulation (synth per-voice). LFO 1 free rate + its depth into
    //    Filter 1 cutoff. These two drive the audible vertical slice; the full LFO
    //    bank, sync, shapes, and the route matrix arrive in Batches 2–5.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::LFO1_RATE, 1 },
        "LFO 1 Rate",
        juce::NormalisableRange<float> (0.01f, 40.0f, 0.0f, 0.3f),
        2.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::LFO1_DEPTH, 1 },
        "LFO 1 Depth",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f),
        1.0f));   // per-LFO master amount (default full; scales all this LFO's routes, modulatable)
    // ── Mod redesign — LFO 1 shape / sync / division.
    //    SHAPE indices match wc::LFOShape (Sine=0 … Random=6).
    //    DIV indices match wc::kSyncDivisions exactly (so syncIdx = div index).
    //    Default = BPM · 1/4 (= 2 Hz @ 120 bpm) so the default breathe is unchanged.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::LFO1_SHAPE, 1 },
        "LFO 1 Shape",
        juce::StringArray { "SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE", "S&H", "RANDOM", "CUSTOM", "PATH", "LORENZ", "ROSSLER" },   // LFO ARC L1 + fb239 (enum-append law; DUNE = idx5 repurposed; PATH/EDDY/VORTEX appended)
        0));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::LFO1_SYNC, 1 },
        "LFO 1 Sync", false));   // fb78 ROOT-CAUSE FIX: was true — the UI boots FREE, and a sync'd LFO froze at phase 0 with the transport stopped, so EVERY route multiplied by 0.0 ("none of them work")
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::LFO1_DIV, 1 },
        "LFO 1 Division",
        juce::StringArray { "8 bar","4 bar","2 bar","1 bar","1/2","1/4","1/8","1/16","1/32","1/4.","1/8.","1/4T","1/8T","1/16T","32 bar","16 bar","1/64","1/128","1/256" },   // fb219 — appended (must match kSyncDivisions + the lambda list)
        5));
    // Per-LFO PHASE (slides the waveform). 0..1, default 0.
    for (auto* pid : { ParameterIDs::LFO1_PHASE, ParameterIDs::LFO2_PHASE, ParameterIDs::LFO3_PHASE, ParameterIDs::LFO4_PHASE, ParameterIDs::LFO5_PHASE,
                       ParameterIDs::LFO6_PHASE, ParameterIDs::LFO7_PHASE, ParameterIDs::LFO8_PHASE, ParameterIDs::LFO9_PHASE, ParameterIDs::LFO10_PHASE })
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { pid, 1 }, juce::String (pid).replace ("LFO", "LFO ").replace ("_PHASE", " Phase"),
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    // ── Mod redesign Stage 2 — LFOs 2..5 (same set as L1). Default depth 0 (silent until dialed).
    {
        const juce::StringArray lfoShapes { "SINE","TRIANGLE","SAW UP","SAW DOWN","SQUARE","S&H","RANDOM","CUSTOM","PATH","LORENZ","ROSSLER" };   // LFO ARC L1 + fb239 — must match the L1 inline list
        const juce::StringArray lfoDivs   { "8 bar","4 bar","2 bar","1 bar","1/2","1/4","1/8","1/16","1/32","1/4.","1/8.","1/4T","1/8T","1/16T","32 bar","16 bar","1/64","1/128","1/256" };   // fb219 — must match the L1 inline list
        auto addLfo = [&] (int n, const char* rate, const char* depth, const char* shape, const char* sync, const char* div)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rate, 1 },  "LFO " + juce::String (n) + " Rate",
                juce::NormalisableRange<float> (0.01f, 40.0f, 0.0f, 0.3f), 2.0f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { depth, 1 }, "LFO " + juce::String (n) + " Depth",
                juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 1.0f));   // master amount, default full
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { shape, 1 }, "LFO " + juce::String (n) + " Shape", lfoShapes, 0));
            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { sync, 1 },  "LFO " + juce::String (n) + " Sync", false));   // fb78 ROOT-CAUSE FIX (see LFO 1)
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { div, 1 },   "LFO " + juce::String (n) + " Division", lfoDivs, 5));
        };
        addLfo (2, ParameterIDs::LFO2_RATE, ParameterIDs::LFO2_DEPTH, ParameterIDs::LFO2_SHAPE, ParameterIDs::LFO2_SYNC, ParameterIDs::LFO2_DIV);
        addLfo (3, ParameterIDs::LFO3_RATE, ParameterIDs::LFO3_DEPTH, ParameterIDs::LFO3_SHAPE, ParameterIDs::LFO3_SYNC, ParameterIDs::LFO3_DIV);
        addLfo (4, ParameterIDs::LFO4_RATE, ParameterIDs::LFO4_DEPTH, ParameterIDs::LFO4_SHAPE, ParameterIDs::LFO4_SYNC, ParameterIDs::LFO4_DIV);
        addLfo (5, ParameterIDs::LFO5_RATE, ParameterIDs::LFO5_DEPTH, ParameterIDs::LFO5_SHAPE, ParameterIDs::LFO5_SYNC, ParameterIDs::LFO5_DIV);
        addLfo (6, ParameterIDs::LFO6_RATE, ParameterIDs::LFO6_DEPTH, ParameterIDs::LFO6_SHAPE, ParameterIDs::LFO6_SYNC, ParameterIDs::LFO6_DIV);
        addLfo (7, ParameterIDs::LFO7_RATE, ParameterIDs::LFO7_DEPTH, ParameterIDs::LFO7_SHAPE, ParameterIDs::LFO7_SYNC, ParameterIDs::LFO7_DIV);
        addLfo (8, ParameterIDs::LFO8_RATE, ParameterIDs::LFO8_DEPTH, ParameterIDs::LFO8_SHAPE, ParameterIDs::LFO8_SYNC, ParameterIDs::LFO8_DIV);
        addLfo (9, ParameterIDs::LFO9_RATE, ParameterIDs::LFO9_DEPTH, ParameterIDs::LFO9_SHAPE, ParameterIDs::LFO9_SYNC, ParameterIDs::LFO9_DIV);
        addLfo (10,ParameterIDs::LFO10_RATE,ParameterIDs::LFO10_DEPTH,ParameterIDs::LFO10_SHAPE,ParameterIDs::LFO10_SYNC,ParameterIDs::LFO10_DIV);
    }

    // ── Batch 1 Filter — TYPE (27 choices, NONE last in enum but first in UI),
    //                     DRV (0..1 → 0..+24 dB drive), ENV (bipolar -1..+1).
    {
        juce::StringArray filterTypeChoices;
        // Display names: proper case, mandatory acronyms kept (LP/HP/BP/SVF/OB-X/4P/vowels).
        filterTypeChoices.add ("Ladder LP 24");
        filterTypeChoices.add ("Ladder LP 12");
        filterTypeChoices.add ("Ladder HP 24");
        filterTypeChoices.add ("Diode LP");
        filterTypeChoices.add ("Acid 303");
        filterTypeChoices.add ("SVF LP");
        filterTypeChoices.add ("SVF HP");
        filterTypeChoices.add ("SVF BP");
        filterTypeChoices.add ("SVF Notch");
        filterTypeChoices.add ("OB-X SVF");
        filterTypeChoices.add ("Comb +");
        filterTypeChoices.add ("Comb -");
        filterTypeChoices.add ("Comb Shimmer");
        filterTypeChoices.add ("Karplus-Strong");
        filterTypeChoices.add ("Formant A");
        filterTypeChoices.add ("Formant E");
        filterTypeChoices.add ("Formant I");
        filterTypeChoices.add ("Formant Morph");
        filterTypeChoices.add ("Reverb Filter");
        filterTypeChoices.add ("Phaser 4P");
        filterTypeChoices.add ("Phaser 8P");
        filterTypeChoices.add ("Ring Mod");
        filterTypeChoices.add ("Bode Shifter");
        filterTypeChoices.add ("Bit-Crush");
        filterTypeChoices.add ("Waveshaper");
        filterTypeChoices.add ("Grain Mask");
        filterTypeChoices.add ("Reverb Filter 2");
        filterTypeChoices.add ("None");
        // fb165 — THE FILTER EXPANSION (append-only; None stays index 27)
        filterTypeChoices.add ("Ladder LP 6");
        filterTypeChoices.add ("Ladder LP 18");
        filterTypeChoices.add ("German LP");
        filterTypeChoices.add ("Germanium LP");
        filterTypeChoices.add ("French LP");
        filterTypeChoices.add ("Acid Scream");
        filterTypeChoices.add ("Xpd HP 6");
        filterTypeChoices.add ("Xpd HP 12");
        filterTypeChoices.add ("Xpd HP 18");
        filterTypeChoices.add ("Xpd BP 12");
        filterTypeChoices.add ("Xpd BP 24");
        filterTypeChoices.add ("Xpd BP 6");
        filterTypeChoices.add ("Xpd Notch");
        filterTypeChoices.add ("Xpd Phase");
        filterTypeChoices.add ("Xpd 1-Pole");
        filterTypeChoices.add ("SVF LP 24");
        filterTypeChoices.add ("SVF HP 24");
        filterTypeChoices.add ("SVF BP 24");
        filterTypeChoices.add ("SVF Notch 24");
        filterTypeChoices.add ("SVF Peak");
        filterTypeChoices.add ("SEM LP");
        filterTypeChoices.add ("SEM Notch");
        filterTypeChoices.add ("SEM HP");
        filterTypeChoices.add ("SEM BP");
        filterTypeChoices.add ("Multi LP+HP");
        filterTypeChoices.add ("Multi LP+BP");
        filterTypeChoices.add ("Multi LP+Notch");
        filterTypeChoices.add ("Multi HP+BP");
        filterTypeChoices.add ("Multi HP+Notch");
        filterTypeChoices.add ("Multi BP+BP");
        filterTypeChoices.add ("Multi BP+Notch");
        filterTypeChoices.add ("Multi Peak+Peak");
        filterTypeChoices.add ("Multi Notch+Notch");
        filterTypeChoices.add ("Multi Peak+HP");
        filterTypeChoices.add ("Comb Wide");
        filterTypeChoices.add ("Comb Octave");
        filterTypeChoices.add ("Comb Fifth");
        filterTypeChoices.add ("Comb Damp");
        filterTypeChoices.add ("Karplus Bright");
        filterTypeChoices.add ("Karplus Mute");
        filterTypeChoices.add ("Formant O");
        filterTypeChoices.add ("Formant U");
        filterTypeChoices.add ("Formant Wide");
        filterTypeChoices.add ("Formant Growl");
        filterTypeChoices.add ("Phaser 6P");
        filterTypeChoices.add ("Phaser 12P");
        filterTypeChoices.add ("Phaser 16P");
        filterTypeChoices.add ("Diffusor");
        filterTypeChoices.add ("Bode Down");
        filterTypeChoices.add ("Tilt");
        filterTypeChoices.add ("Low EQ");
        filterTypeChoices.add ("High EQ");
        filterTypeChoices.add ("Band EQ");
        filterTypeChoices.add ("Air");
        filterTypeChoices.add ("Add Bass");
        filterTypeChoices.add ("Samp-Hold");
        filterTypeChoices.add ("Samp-Hold -");
        filterTypeChoices.add ("Scream LP");
        filterTypeChoices.add ("Scream BP");
        filterTypeChoices.add ("Wasp");
        filterTypeChoices.add ("MS-20 LP");
        filterTypeChoices.add ("Polivoks");
        filterTypeChoices.add ("Ring Mod X2");
        filterTypeChoices.add ("Radio");
        filterTypeChoices.add ("Reverb Dark");
        filterTypeChoices.add ("Reverb Metal");
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIDs::SYN_FILTER1_TYPE, 1 },
            "Synth Filter 1 Type", filterTypeChoices, 27));  // default = NONE (filters start OFF; wire on the back panel — Max 2026-07-13)
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIDs::SYN_FILTER2_TYPE, 1 },
            "Synth Filter 2 Type", filterTypeChoices, 27));  // default = NONE (slot 2 inert this batch)
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_DRV, 1 },
        "Synth Filter 1 Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_ENV, 1 },
        "Synth Filter 1 Env Amount",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_FILTER_SLOT, 1 },
        "Synth Filter Edit Slot", 0, 1, 0));

    // Slot 2 reserved (inert this batch — no DSP wiring, just preset persistence)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_CUT, 1 },
        "Synth Filter 2 Cutoff",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.25f), 20000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_RES, 1 },
        "Synth Filter 2 Resonance",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_KEYTRACK, 1 },
        "Synth Filter 2 Keytrack",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_DRV, 1 },
        "Synth Filter 2 Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_ENV, 1 },
        "Synth Filter 2 Env Amount",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

    // Per-filter wet/dry mix (default fully filtered) + series/parallel routing.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_MIX, 1 },
        "Synth Filter 1 Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_MIX, 1 },
        "Synth Filter 2 Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    // fb79 — PER-OSC continuous filter sends (the F1/F2 pills). DEFAULT 0 = fully dry: a fresh
    // Terrain routes NOTHING anywhere until you fill a pill (Max). Replaces the binary A-D masks.
    for (auto* pid : { ParameterIDs::SYN_OSC_A_F1MIX, ParameterIDs::SYN_OSC_B_F1MIX, ParameterIDs::SYN_OSC_C_F1MIX, ParameterIDs::SYN_OSC_D_F1MIX,
                       ParameterIDs::SYN_OSC_A_F2MIX, ParameterIDs::SYN_OSC_B_F2MIX, ParameterIDs::SYN_OSC_C_F2MIX, ParameterIDs::SYN_OSC_D_F2MIX })
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { pid, 1 }, juce::String (pid).replace ("SYN_OSC_", "Synth OSC ").replace ("_F1MIX", " Filter 1 Send").replace ("_F2MIX", " Filter 2 Send"),
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    // Back-panel Vel (velocity→cutoff) + post-filter Drive, per filter. Default 0 = inert.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_VEL, 1 },  "Synth Filter 1 Velocity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_VEL, 1 },  "Synth Filter 2 Velocity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_PDRV, 1 }, "Synth Filter 1 Post Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_PDRV, 1 }, "Synth Filter 2 Post Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    // DRIVE TYPE — post-filter drive waveshaper flavor (Tube/Diode/Fold/Hard/Crush/Fuzz), per filter.
    {
        const juce::StringArray driveTypes { "Tube","Diode","Fold","Hard","Crush","Fuzz" };
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIDs::SYN_FILTER1_DRIVETYPE, 1 }, "Synth Filter 1 Drive Type", driveTypes, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIDs::SYN_FILTER2_DRIVETYPE, 1 }, "Synth Filter 2 Drive Type", driveTypes, 0));
    }
    // POLES — ladder slope tap: 6/12/18/24 dB/oct (choice 0..3), default 24 dB (index 3).
    {
        const juce::StringArray poleSlopes { "6","12","18","24" };
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIDs::SYN_FILTER1_POLES, 1 }, "Synth Filter 1 Poles", poleSlopes, 3));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIDs::SYN_FILTER2_POLES, 1 }, "Synth Filter 2 Poles", poleSlopes, 3));
    }
    // SPREAD — filter stereo width (L/R cutoff offset 0..1), per filter.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_SPREAD, 1 }, "Synth Filter 1 Spread",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_SPREAD, 1 }, "Synth Filter 2 Spread",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_FILTER_ROUTING, 1 },
        "Synth Filter Routing", juce::StringArray { "SERIES", "PARALLEL" }, 0));
    // Per-oscillator filter routing masks — default FALSE (nothing routed): a fresh patch has the
    // filters OFF and connects nothing, so every osc passes DRY (the filter is a modular node you
    // wire up on the back panel). Max 2026-07-13.
    {
        struct { const char* id; const char* name; } fltSrc[] = {
            { ParameterIDs::SYN_FILTER1_SRC_A,   "Synth Filter 1 Source A"   },
            { ParameterIDs::SYN_FILTER1_SRC_B,   "Synth Filter 1 Source B"   },
            { ParameterIDs::SYN_FILTER1_SRC_C,   "Synth Filter 1 Source C"   },
            { ParameterIDs::SYN_FILTER1_SRC_D,   "Synth Filter 1 Source D"   },
            { ParameterIDs::SYN_FILTER1_SRC_SUB, "Synth Filter 1 Source Sub" },
            { ParameterIDs::SYN_FILTER2_SRC_A,   "Synth Filter 2 Source A"   },
            { ParameterIDs::SYN_FILTER2_SRC_B,   "Synth Filter 2 Source B"   },
            { ParameterIDs::SYN_FILTER2_SRC_C,   "Synth Filter 2 Source C"   },
            { ParameterIDs::SYN_FILTER2_SRC_D,   "Synth Filter 2 Source D"   },
            { ParameterIDs::SYN_FILTER2_SRC_SUB, "Synth Filter 2 Source Sub" },
            { ParameterIDs::SYN_FILTER1_SRC_NOISE, "Synth Filter 1 Source Noise" },   // fb63 — noise → filter routing
            { ParameterIDs::SYN_FILTER2_SRC_NOISE, "Synth Filter 2 Source Noise" },
        };
        for (auto& s : fltSrc)
            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { s.id, 1 }, s.name, false));
    }

    // Filter ADSR (independent from AMP env — drives the cutoff via the bipolar ENV knob).
    // Defaults: classic "filter sweep down" shape (instant attack, mid decay, no sustain, short release).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_A, 1 },
        "Synth Filter Env Attack",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 5.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_D, 1 },
        "Synth Filter Env Decay",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 200.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_S, 1 },
        "Synth Filter Env Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));   // canonical env default (== reset shape)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_R, 1 },
        "Synth Filter Env Release",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 300.0f));   // canonical env default (== reset shape)

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_A, 1 },
        "Synth Amp Attack",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f),
        5.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_D, 1 },
        "Synth Amp Decay",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f),
        200.0f));   // canonical env default (== reset shape) — uniform across all 5 envelopes

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_S, 1 },
        "Synth Amp Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.7f));   // canonical env default (== reset shape)

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_R, 1 },
        "Synth Amp Release",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f),
        300.0f));   // canonical env default (== reset shape)

    // ── Envelope DAHDSR params (Batch 2/3): 5 envelopes × delay/hold/curves/loop (+ADSR for PIT/M1/M2) ──
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_DLY, 1 }, "Synth Amp Delay",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.0f, 0.3f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_H, 1 }, "Synth Amp Hold",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.0f, 0.3f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_CA, 1 }, "Synth Amp Attack Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_CD, 1 }, "Synth Amp Decay Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_CR, 1 }, "Synth Amp Release Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_LOOP, 1 }, "Synth Amp Loop", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_DLY, 1 }, "Synth Filter Delay",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.0f, 0.3f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_H, 1 }, "Synth Filter Hold",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.0f, 0.3f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_CA, 1 }, "Synth Filter Attack Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_CD, 1 }, "Synth Filter Decay Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_CR, 1 }, "Synth Filter Release Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_LOOP, 1 }, "Synth Filter Loop", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_PIT_A, 1 }, "Synth Pitch Attack",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 5.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_PIT_D, 1 }, "Synth Pitch Decay",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 200.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_PIT_S, 1 }, "Synth Pitch Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));   // canonical env default (== reset shape)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_PIT_R, 1 }, "Synth Pitch Release",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 300.0f));   // canonical env default (== reset shape)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_PIT_DLY, 1 }, "Synth Pitch Delay",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.0f, 0.3f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_PIT_H, 1 }, "Synth Pitch Hold",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.0f, 0.3f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_PIT_CA, 1 }, "Synth Pitch Attack Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_PIT_CD, 1 }, "Synth Pitch Decay Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_PIT_CR, 1 }, "Synth Pitch Release Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_PIT_DEPTH, 1 }, "Synth Pitch Env Depth",
        juce::NormalisableRange<float> (-48.0f, 48.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_ENV_PIT_LOOP, 1 }, "Synth Pitch Loop", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M1_A, 1 }, "Synth Mod 1 Attack",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 5.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M1_D, 1 }, "Synth Mod 1 Decay",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 200.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M1_S, 1 }, "Synth Mod 1 Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));   // canonical env default (== reset shape)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M1_R, 1 }, "Synth Mod 1 Release",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 300.0f));   // canonical env default (== reset shape)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M1_DLY, 1 }, "Synth Mod 1 Delay",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.0f, 0.3f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M1_H, 1 }, "Synth Mod 1 Hold",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.0f, 0.3f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M1_CA, 1 }, "Synth Mod 1 Attack Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M1_CD, 1 }, "Synth Mod 1 Decay Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M1_CR, 1 }, "Synth Mod 1 Release Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M1_LOOP, 1 }, "Synth Mod 1 Loop", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M2_A, 1 }, "Synth Mod 2 Attack",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 5.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M2_D, 1 }, "Synth Mod 2 Decay",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 200.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M2_S, 1 }, "Synth Mod 2 Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));   // canonical env default (== reset shape)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M2_R, 1 }, "Synth Mod 2 Release",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f), 300.0f));   // canonical env default (== reset shape)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M2_DLY, 1 }, "Synth Mod 2 Delay",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.0f, 0.3f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M2_H, 1 }, "Synth Mod 2 Hold",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.0f, 0.3f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M2_CA, 1 }, "Synth Mod 2 Attack Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M2_CD, 1 }, "Synth Mod 2 Decay Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M2_CR, 1 }, "Synth Mod 2 Release Curve",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_ENV_M2_LOOP, 1 }, "Synth Mod 2 Loop", false));

    // ── Per-envelope ROUTING (envs 2–5 free; DEST + bipolar DEPTH each) ──
    // DEST labels MUST stay in this order — index is read as an int in the voice
    // and mirrored by the WebUI menu: 0=Off 1=Amp 2=Filter 1 3=Filter 2
    // 4=Filter 1+2 5=Mod 1 6=Mod 2 7=Pitch. Defaults pre-route to the natural
    // destination at DEPTH 0 (nothing modulates on load; assign depth in the menu).
    const juce::StringArray envDestChoices {
        "Off", "Amp", "Filter 1", "Filter 2", "Filter 1+2", "Mod 1", "Mod 2", "Pitch" };
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_ENV2_DEST, 1 }, "Synth Env 2 Destination", envDestChoices, 0)); // fb180 — Off (no auto-routes; Env 1 = Amp is the only lock, per Max)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV2_DEPTH, 1 }, "Synth Env 2 Depth",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_ENV3_DEST, 1 }, "Synth Env 3 Destination", envDestChoices, 0)); // fb180 — Off
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV3_DEPTH, 1 }, "Synth Env 3 Depth",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_ENV4_DEST, 1 }, "Synth Env 4 Destination", envDestChoices, 0)); // Off
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV4_DEPTH, 1 }, "Synth Env 4 Depth",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_ENV5_DEST, 1 }, "Synth Env 5 Destination", envDestChoices, 0)); // Off
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV5_DEPTH, 1 }, "Synth Env 5 Depth",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));


    // ── Synth section — Phase 2A (Wavetable foundation) ──────────────────
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WT_PRESET, 1 },
        "Synth OSC A WT Preset",
        juce::StringArray { "Sine", "Triangle", "Square", "Pulse",
                            "Prophet Saw", "Jupiter PWM", "Moog Sqr",
                            "OB-X Saw", "CS-80 Brass", "Juno Str",
                            "PPG Wave", "DX7 EP", "D-50 Bell", "M1 Piano",
                            "Choir A->O", "Whisper", "Vowel Morph",
                            "Bowed Metal", "Glass Harmonics", "Railroad",
                            "Dustbowl", "Static Evolve", "Spectral Drift", "Serum HD",
                            // Morph (Phase 11h)
                            "Rise", "Even", "Drift", "Sweep", "Formant", "Stack" },
        0));  // default = Sine

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WT_FRAME, 1 },
        "Synth OSC A WT Frame",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));

    // ── Synth section — Phase 2C (Warp modes) ────────────────────────────
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WARP_MODE, 1 },
        "Synth OSC A Warp Mode",
        juce::StringArray { "NONE", "Bend", "Sync", "Formant", "PWM", "Skew", "Mirror", "Fractalize", "P-Quantize", "Rectify", "Sine Shaper" },
        0));

    // PHASE mode (back panel pill 1 — replaces the redundant WARP-mode selector).
    // 0=RETRIG (all voices to phase 0, tight/punchy) · 1=FREE (never reset, analog) ·
    // 2=RANDOM (fresh decorrelated phase each note, default) · 3=SPREAD (even fan, wide+repeatable)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_PHASE_MODE, 1 },
        "Synth OSC A Phase Mode",
        juce::StringArray { "Retrig", "Free", "Random", "Spread" },
        2));

    // WAVER (back panel pill 2 — replaces the redundant SPECTRAL-mode selector).
    // Per-OSC analog pitch-drift depth (Ornstein–Uhlenbeck, per unison sine).
    // Supersedes the old SYN_EROSION pitch drift; SYN_EROSION now drives FILTER drift only.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WAVER, 1 },
        "Synth OSC A Waver",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f));

    // KEYTRACK (back panel pill 3 — replaces the redundant FOLD-shape selector).
    // First note->destination modulation route: depth 0..100 %, destination selectable
    // (FRAME=WT POS, WARP, FOLD). Per-voice, latched at note-on. Architected toward the
    // future mod-matrix. (SPECTRAL is off-thread; sample START has no per-OSC param yet.)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_KEYTRACK, 1 },
        "Synth OSC A Keytrack",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_KEYTRACK_DEST, 1 },
        "Synth OSC A Keytrack Dest",
        juce::StringArray { "Frame", "Warp", "Fold" },
        0));

    // ROUTE (back panel pill 4 — replaces the rehomed INTERP selector).
    // Mod route #2 — the generalized slot: source (Note/Velocity) -> destination
    // (FRAME/WARP/FOLD/CUT1/CUT2) by a BIPOLAR amount. Per-voice, latched at note-on.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_ROUTE_SRC, 1 },
        "Synth OSC A Route Source",
        juce::StringArray { "Note", "Vel" },
        0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_ROUTE_DEST, 1 },
        "Synth OSC A Route Dest",
        juce::StringArray { "Frame", "Warp", "Fold" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_ROUTE_AMT, 1 },
        "Synth OSC A Route Amount",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_UNISON, 1 },
        "Synth OSC A Unison", 1, 16, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_UDETUNE, 1 },
        "Synth OSC A Unison Detune",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_UBLEND, 1 },
        "Synth OSC A Unison Blend",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_UWIDTH, 1 },
        "Synth OSC A Unison Width",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WARP_AMOUNT, 1 },
        "Synth OSC A Warp Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WARP2_MODE, 1 },
        "Synth OSC A Warp 2 Mode",
        juce::StringArray { "NONE", "Bend", "Sync", "Formant", "PWM", "Skew", "Mirror", "Fractalize", "P-Quantize", "Rectify", "Sine Shaper" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WARP2_AMT, 1 },
        "Synth OSC A Warp 2 Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // ── Phase 11a — OSC A wavetable rework foundation ────────────────────
    // SPECTRAL MORPH mode (Phase 11c rework — frequency-domain morph applied to the
    // wavetable's spectrum off the audio thread). v1 exposes the implemented modes;
    // order MUST match tw::SpectralMode (None=0, HarmonicStretch=1, InharmonicStretch=2).
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SPECTRAL_TYPE, 1 },
        "OSC A Spectral Type",
        juce::StringArray { "None", "Harmonic Stretch", "Inharmonic Stretch",
                            "Vocode", "Smear", "Random Amps", "Data Compress", "Spectral Phaser" },
        0));
    // SPECTRAL AMT — morph amount (0 = base table, 1 = full morph).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SPECTRAL_AMT, 1 },
        "OSC A Spectral Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    // FOLD SHAPE choice (Phase 11d: 3 shapes — Linear / Sine / Triangle)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_FOLD_SHAPE, 1 },
        "OSC A Fold Shape",
        juce::StringArray { "Linear", "Sine", "Triangle" },
        0));
    // FOLD AMT (placeholder param)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_FOLD_AMT, 1 },
        "OSC A Fold Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    // BLUR (real DSP — frame-blend width; repurposes the old FRAME_SPREAD param ID)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_FRAME_SPREAD, 1 },
        "OSC A Blur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    // INTERP MODE choice (Phase 11g: 2 modes — Linear / Stepped)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_INTERP_MODE, 1 },
        "OSC A Interp Mode",
        juce::StringArray { "Linear", "Stepped" },
        0));

    // ── Synth section — Phase 9 (OSC B chassis) ──────────────────────────
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_ENGINE, 1 },
        "Synth OSC B Engine",
        juce::StringArray { "WT", "SAMP", "GRAN", "SPEC", "FM", "HARM", "MODAL" },
        0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_OCT, 1 },
        "Synth OSC B Octave", -3, 3, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SEMI, 1 },
        "Synth OSC B Semitone", -12, 12, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_CENT, 1 },
        "Synth OSC B Cents",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_LEVEL, 1 },
        "Synth OSC B Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));   // audible default — OFF now lives in SYN_OSC_B_ENABLE, so switching on sounds immediately
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_PAN, 1 },
        "Synth OSC B Pan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_ENABLE, 1 }, "Osc B Enable", false));   // fresh instance: only OSC A on
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_MUTE, 1 }, "Osc B Mute", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SOLO, 1 }, "Osc B Solo", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WT_PRESET, 1 },
        "Synth OSC B WT Preset",
        juce::StringArray { "Sine", "Triangle", "Square", "Pulse",
                            "Prophet Saw", "Jupiter PWM", "Moog Sqr",
                            "OB-X Saw", "CS-80 Brass", "Juno Str",
                            "PPG Wave", "DX7 EP", "D-50 Bell", "M1 Piano",
                            "Choir A->O", "Whisper", "Vowel Morph",
                            "Bowed Metal", "Glass Harmonics", "Railroad",
                            "Dustbowl", "Static Evolve", "Spectral Drift", "Serum HD",
                            // Morph (Phase 11h)
                            "Rise", "Even", "Drift", "Sweep", "Formant", "Stack" },
        0));  // default = Sine
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WT_FRAME, 1 },
        "Synth OSC B WT Frame",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WARP_MODE, 1 },
        "Synth OSC B Warp Mode",
        juce::StringArray { "NONE", "Bend", "Sync", "Formant", "PWM", "Skew", "Mirror", "Fractalize", "P-Quantize", "Rectify", "Sine Shaper" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_PHASE_MODE, 1 },
        "Synth OSC B Phase Mode",
        juce::StringArray { "Retrig", "Free", "Random", "Spread" },
        2));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WAVER, 1 },
        "Synth OSC B Waver",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_KEYTRACK, 1 },
        "Synth OSC B Keytrack",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_KEYTRACK_DEST, 1 },
        "Synth OSC B Keytrack Dest",
        juce::StringArray { "Frame", "Warp", "Fold" },
        0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_ROUTE_SRC, 1 },
        "Synth OSC B Route Source",
        juce::StringArray { "Note", "Vel" },
        0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_ROUTE_DEST, 1 },
        "Synth OSC B Route Dest",
        juce::StringArray { "Frame", "Warp", "Fold" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_ROUTE_AMT, 1 },
        "Synth OSC B Route Amount",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_UNISON, 1 },
        "Synth OSC B Unison", 1, 16, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_UDETUNE, 1 },
        "Synth OSC B Unison Detune",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_UBLEND, 1 },
        "Synth OSC B Unison Blend",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_UWIDTH, 1 },
        "Synth OSC B Unison Width",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WARP_AMOUNT, 1 },
        "Synth OSC B Warp Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WARP2_MODE, 1 },
        "Synth OSC B Warp 2 Mode",
        juce::StringArray { "NONE", "Bend", "Sync", "Formant", "PWM", "Skew", "Mirror", "Fractalize", "P-Quantize", "Rectify", "Sine Shaper" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WARP2_AMT, 1 },
        "Synth OSC B Warp 2 Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // ── Phase 11a — OSC B wavetable rework foundation (SPECTRAL MORPH — Phase 11c) ─
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SPECTRAL_TYPE, 1 },
        "OSC B Spectral Type",
        juce::StringArray { "None", "Harmonic Stretch", "Inharmonic Stretch",
                            "Vocode", "Smear", "Random Amps", "Data Compress", "Spectral Phaser" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SPECTRAL_AMT, 1 },
        "OSC B Spectral Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_FOLD_SHAPE, 1 },
        "OSC B Fold Shape",
        juce::StringArray { "Linear", "Sine", "Triangle" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_FOLD_AMT, 1 },
        "OSC B Fold Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_FRAME_SPREAD, 1 },
        "OSC B Blur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_INTERP_MODE, 1 },
        "OSC B Interp Mode",
        juce::StringArray { "Linear", "Stepped" },
        0));
    // ── OSC C chassis (4-osc) — mirrors OSC B ──
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_ENGINE, 1 },
        "Synth OSC C Engine",
        juce::StringArray { "WT", "SAMP", "GRAN", "SPEC", "FM", "HARM", "MODAL" },
        0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_OCT, 1 },
        "Synth OSC C Octave", -3, 3, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SEMI, 1 },
        "Synth OSC C Semitone", -12, 12, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_CENT, 1 },
        "Synth OSC C Cents",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_LEVEL, 1 },
        "Synth OSC C Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));   // audible default — OFF now lives in SYN_OSC_C_ENABLE
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_PAN, 1 },
        "Synth OSC C Pan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_ENABLE, 1 }, "Osc C Enable", false));   // fresh instance: only OSC A on
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_MUTE, 1 }, "Osc C Mute", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SOLO, 1 }, "Osc C Solo", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_WT_PRESET, 1 },
        "Synth OSC C WT Preset",
        juce::StringArray { "Sine", "Triangle", "Square", "Pulse",
                            "Prophet Saw", "Jupiter PWM", "Moog Sqr",
                            "OB-X Saw", "CS-80 Brass", "Juno Str",
                            "PPG Wave", "DX7 EP", "D-50 Bell", "M1 Piano",
                            "Choir A->O", "Whisper", "Vowel Morph",
                            "Bowed Metal", "Glass Harmonics", "Railroad",
                            "Dustbowl", "Static Evolve", "Spectral Drift", "Serum HD",
                            // Morph (Phase 11h)
                            "Rise", "Even", "Drift", "Sweep", "Formant", "Stack" },
        0));  // default = Sine
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_WT_FRAME, 1 },
        "Synth OSC C WT Frame",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_WARP_MODE, 1 },
        "Synth OSC C Warp Mode",
        juce::StringArray { "NONE", "Bend", "Sync", "Formant", "PWM", "Skew", "Mirror", "Fractalize", "P-Quantize", "Rectify", "Sine Shaper" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_PHASE_MODE, 1 },
        "Synth OSC C Phase Mode",
        juce::StringArray { "Retrig", "Free", "Random", "Spread" },
        2));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_WAVER, 1 },
        "Synth OSC C Waver",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_KEYTRACK, 1 },
        "Synth OSC C Keytrack",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_KEYTRACK_DEST, 1 },
        "Synth OSC C Keytrack Dest",
        juce::StringArray { "Frame", "Warp", "Fold" },
        0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_ROUTE_SRC, 1 },
        "Synth OSC C Route Source",
        juce::StringArray { "Note", "Vel" },
        0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_ROUTE_DEST, 1 },
        "Synth OSC C Route Dest",
        juce::StringArray { "Frame", "Warp", "Fold" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_ROUTE_AMT, 1 },
        "Synth OSC C Route Amount",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_UNISON, 1 },
        "Synth OSC C Unison", 1, 16, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_UDETUNE, 1 },
        "Synth OSC C Unison Detune",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_UBLEND, 1 },
        "Synth OSC C Unison Blend",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_UWIDTH, 1 },
        "Synth OSC C Unison Width",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_WARP_AMOUNT, 1 },
        "Synth OSC C Warp Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_WARP2_MODE, 1 },
        "Synth OSC C Warp 2 Mode",
        juce::StringArray { "NONE", "Bend", "Sync", "Formant", "PWM", "Skew", "Mirror", "Fractalize", "P-Quantize", "Rectify", "Sine Shaper" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_WARP2_AMT, 1 },
        "Synth OSC C Warp 2 Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // ── Phase 11a — OSC C wavetable rework foundation (SPECTRAL MORPH — Phase 11c) ─
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SPECTRAL_TYPE, 1 },
        "OSC C Spectral Type",
        juce::StringArray { "None", "Harmonic Stretch", "Inharmonic Stretch",
                            "Vocode", "Smear", "Random Amps", "Data Compress", "Spectral Phaser" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SPECTRAL_AMT, 1 },
        "OSC C Spectral Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_FOLD_SHAPE, 1 },
        "OSC C Fold Shape",
        juce::StringArray { "Linear", "Sine", "Triangle" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_FOLD_AMT, 1 },
        "OSC C Fold Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_FRAME_SPREAD, 1 },
        "OSC C Blur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_INTERP_MODE, 1 },
        "OSC C Interp Mode",
        juce::StringArray { "Linear", "Stepped" },
        0));
    // ── OSC D chassis (4-osc) — mirrors OSC B ──
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_ENGINE, 1 },
        "Synth OSC D Engine",
        juce::StringArray { "WT", "SAMP", "GRAN", "SPEC", "FM", "HARM", "MODAL" },
        0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_OCT, 1 },
        "Synth OSC D Octave", -3, 3, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SEMI, 1 },
        "Synth OSC D Semitone", -12, 12, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_CENT, 1 },
        "Synth OSC D Cents",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_LEVEL, 1 },
        "Synth OSC D Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));   // audible default — OFF now lives in SYN_OSC_D_ENABLE
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_PAN, 1 },
        "Synth OSC D Pan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_ENABLE, 1 }, "Osc D Enable", false));   // fresh instance: only OSC A on
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_MUTE, 1 }, "Osc D Mute", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SOLO, 1 }, "Osc D Solo", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_WT_PRESET, 1 },
        "Synth OSC D WT Preset",
        juce::StringArray { "Sine", "Triangle", "Square", "Pulse",
                            "Prophet Saw", "Jupiter PWM", "Moog Sqr",
                            "OB-X Saw", "CS-80 Brass", "Juno Str",
                            "PPG Wave", "DX7 EP", "D-50 Bell", "M1 Piano",
                            "Choir A->O", "Whisper", "Vowel Morph",
                            "Bowed Metal", "Glass Harmonics", "Railroad",
                            "Dustbowl", "Static Evolve", "Spectral Drift", "Serum HD",
                            // Morph (Phase 11h)
                            "Rise", "Even", "Drift", "Sweep", "Formant", "Stack" },
        0));  // default = Sine
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_WT_FRAME, 1 },
        "Synth OSC D WT Frame",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_WARP_MODE, 1 },
        "Synth OSC D Warp Mode",
        juce::StringArray { "NONE", "Bend", "Sync", "Formant", "PWM", "Skew", "Mirror", "Fractalize", "P-Quantize", "Rectify", "Sine Shaper" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_PHASE_MODE, 1 },
        "Synth OSC D Phase Mode",
        juce::StringArray { "Retrig", "Free", "Random", "Spread" },
        2));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_WAVER, 1 },
        "Synth OSC D Waver",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_KEYTRACK, 1 },
        "Synth OSC D Keytrack",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_KEYTRACK_DEST, 1 },
        "Synth OSC D Keytrack Dest",
        juce::StringArray { "Frame", "Warp", "Fold" },
        0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_ROUTE_SRC, 1 },
        "Synth OSC D Route Source",
        juce::StringArray { "Note", "Vel" },
        0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_ROUTE_DEST, 1 },
        "Synth OSC D Route Dest",
        juce::StringArray { "Frame", "Warp", "Fold" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_ROUTE_AMT, 1 },
        "Synth OSC D Route Amount",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_UNISON, 1 },
        "Synth OSC D Unison", 1, 16, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_UDETUNE, 1 },
        "Synth OSC D Unison Detune",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_UBLEND, 1 },
        "Synth OSC D Unison Blend",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_UWIDTH, 1 },
        "Synth OSC D Unison Width",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));

    // ════════ SAMPLE-ENGINE-PARAMS (Opus, 2026-06-25) ════════
    // ── SAMPLE engine params — OSC A ──
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_SCAN, 1 },
        "Synth OSC A Sample Scan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.5f));   // SCAN-DEFAULT-HALFRIGHT — 1x fwd on load
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH, 1 },
        "Synth OSC A Sample Stretch",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_FORMANT, 1 },
        "Synth OSC A Sample Formant",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_SPRAY, 1 },
        "Synth OSC A Sample Spray",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_XFADE, 1 },
        "Synth OSC A Sample XFade",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.12f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_START, 1 },
        "Synth OSC A Sample Start",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_END, 1 },
        "Synth OSC A Sample End",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_START, 1 },
        "Synth OSC A Sample Loop Start",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_END, 1 },
        "Synth OSC A Sample Loop End",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_MODE, 1 },
        "Synth OSC A Sample Loop Mode",
        juce::StringArray { "One-Shot", "Forward", "Reverse", "Ping-Pong", "Tailed" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH_MODE, 1 },
        "Synth OSC A Sample Stretch Mode",
        juce::StringArray { "Tones", "Beats", "Texture" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_FORMANT_MODE, 1 },
        "Synth OSC A Sample Formant Mode",
        juce::StringArray { "Normal", "Inverted", "Cross-Formant", "Spectral-Tilt" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_SNAP, 1 },
        "Synth OSC A Sample Snap",
        juce::StringArray { "Off", "Zero-cross", "Transient" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_FADE_IN, 1 },
        "Synth OSC A Sample Fade In",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_FADE_OUT, 1 },
        "Synth OSC A Sample Fade Out",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_AIR, 1 },
        "Synth OSC A Sample Air",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_WARP, 1 },
        "Synth OSC A Sample Warp",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SAMPLE_WARPMODE, 1 },
        "Synth OSC A Sample Warp Mode",
        juce::StringArray { "Off", "Sine Shaper", "Rectify", "Fold", "Drive", "Crush" }, 0));

    // ── SAMPLE engine params — OSC B ──
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_SCAN, 1 },
        "Synth OSC B Sample Scan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.5f));   // SCAN-DEFAULT-HALFRIGHT — 1x fwd on load
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_STRETCH, 1 },
        "Synth OSC B Sample Stretch",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_FORMANT, 1 },
        "Synth OSC B Sample Formant",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_SPRAY, 1 },
        "Synth OSC B Sample Spray",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_XFADE, 1 },
        "Synth OSC B Sample XFade",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.12f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_START, 1 },
        "Synth OSC B Sample Start",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_END, 1 },
        "Synth OSC B Sample End",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_START, 1 },
        "Synth OSC B Sample Loop Start",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_END, 1 },
        "Synth OSC B Sample Loop End",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_MODE, 1 },
        "Synth OSC B Sample Loop Mode",
        juce::StringArray { "One-Shot", "Forward", "Reverse", "Ping-Pong", "Tailed" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_STRETCH_MODE, 1 },
        "Synth OSC B Sample Stretch Mode",
        juce::StringArray { "Tones", "Beats", "Texture" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_FORMANT_MODE, 1 },
        "Synth OSC B Sample Formant Mode",
        juce::StringArray { "Normal", "Inverted", "Cross-Formant", "Spectral-Tilt" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_SNAP, 1 },
        "Synth OSC B Sample Snap",
        juce::StringArray { "Off", "Zero-cross", "Transient" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_FADE_IN, 1 },
        "Synth OSC B Sample Fade In",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_FADE_OUT, 1 },
        "Synth OSC B Sample Fade Out",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_AIR, 1 },
        "Synth OSC B Sample Air",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_WARP, 1 },
        "Synth OSC B Sample Warp",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SAMPLE_WARPMODE, 1 },
        "Synth OSC B Sample Warp Mode",
        juce::StringArray { "Off", "Sine Shaper", "Rectify", "Fold", "Drive", "Crush" }, 0));

    // ── SAMPLE engine params — OSC C ──
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_SCAN, 1 },
        "Synth OSC C Sample Scan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.5f));   // SCAN-DEFAULT-HALFRIGHT — 1x fwd on load
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_STRETCH, 1 },
        "Synth OSC C Sample Stretch",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_FORMANT, 1 },
        "Synth OSC C Sample Formant",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_SPRAY, 1 },
        "Synth OSC C Sample Spray",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_XFADE, 1 },
        "Synth OSC C Sample XFade",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.12f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_START, 1 },
        "Synth OSC C Sample Start",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_END, 1 },
        "Synth OSC C Sample End",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_START, 1 },
        "Synth OSC C Sample Loop Start",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_END, 1 },
        "Synth OSC C Sample Loop End",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_MODE, 1 },
        "Synth OSC C Sample Loop Mode",
        juce::StringArray { "One-Shot", "Forward", "Reverse", "Ping-Pong", "Tailed" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_STRETCH_MODE, 1 },
        "Synth OSC C Sample Stretch Mode",
        juce::StringArray { "Tones", "Beats", "Texture" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_FORMANT_MODE, 1 },
        "Synth OSC C Sample Formant Mode",
        juce::StringArray { "Normal", "Inverted", "Cross-Formant", "Spectral-Tilt" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_SNAP, 1 },
        "Synth OSC C Sample Snap",
        juce::StringArray { "Off", "Zero-cross", "Transient" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_FADE_IN, 1 },
        "Synth OSC C Sample Fade In",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_FADE_OUT, 1 },
        "Synth OSC C Sample Fade Out",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_AIR, 1 },
        "Synth OSC C Sample Air",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_WARP, 1 },
        "Synth OSC C Sample Warp",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_C_SAMPLE_WARPMODE, 1 },
        "Synth OSC C Sample Warp Mode",
        juce::StringArray { "Off", "Sine Shaper", "Rectify", "Fold", "Drive", "Crush" }, 0));

    // ── SAMPLE engine params — OSC D ──
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_SCAN, 1 },
        "Synth OSC D Sample Scan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.5f));   // SCAN-DEFAULT-HALFRIGHT — 1x fwd on load
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_STRETCH, 1 },
        "Synth OSC D Sample Stretch",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_FORMANT, 1 },
        "Synth OSC D Sample Formant",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_SPRAY, 1 },
        "Synth OSC D Sample Spray",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_XFADE, 1 },
        "Synth OSC D Sample XFade",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.12f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_START, 1 },
        "Synth OSC D Sample Start",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_END, 1 },
        "Synth OSC D Sample End",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_START, 1 },
        "Synth OSC D Sample Loop Start",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_END, 1 },
        "Synth OSC D Sample Loop End",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_MODE, 1 },
        "Synth OSC D Sample Loop Mode",
        juce::StringArray { "One-Shot", "Forward", "Reverse", "Ping-Pong", "Tailed" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_STRETCH_MODE, 1 },
        "Synth OSC D Sample Stretch Mode",
        juce::StringArray { "Tones", "Beats", "Texture" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_FORMANT_MODE, 1 },
        "Synth OSC D Sample Formant Mode",
        juce::StringArray { "Normal", "Inverted", "Cross-Formant", "Spectral-Tilt" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_SNAP, 1 },
        "Synth OSC D Sample Snap",
        juce::StringArray { "Off", "Zero-cross", "Transient" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_FADE_IN, 1 },
        "Synth OSC D Sample Fade In",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_FADE_OUT, 1 },
        "Synth OSC D Sample Fade Out",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_AIR, 1 },
        "Synth OSC D Sample Air",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_WARP, 1 },
        "Synth OSC D Sample Warp",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SAMPLE_WARPMODE, 1 },
        "Synth OSC D Sample Warp Mode",
        juce::StringArray { "Off", "Sine Shaper", "Rectify", "Fold", "Drive", "Crush" }, 0));

    // ════════ GRAIN-ENGINE-PARAMS — per-OSC Granular engine (2026-07-02) ════════
    // 6 primary controls × 4 osc. Defaults = slow-drift (Scan 0.15, Density 0.4, Size 0.25, Spray 0.10, Shape 0.5, Key Off).
    auto addGrainOsc = [&layout] (const char* scanId, const char* densId, const char* sizeId,
                                  const char* sprayId, const char* shapeId, const char* keyId,
                                  const juce::String& osc)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { scanId, 1 },  "Synth OSC " + osc + " Grain Scan",
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.15f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { densId, 1 },  "Synth OSC " + osc + " Grain Density",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sizeId, 1 },  "Synth OSC " + osc + " Grain Size",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sprayId, 1 }, "Synth OSC " + osc + " Grain Spray",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.10f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { shapeId, 1 }, "Synth OSC " + osc + " Grain Shape",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { keyId, 1 },   "Synth OSC " + osc + " Grain Key",
            juce::StringArray { "Off", "Oct", "5th", "Chord", "Maj", "Min", "Penta" }, 0));
    };
    addGrainOsc (ParameterIDs::SYN_OSC_A_GRAIN_SCAN, ParameterIDs::SYN_OSC_A_GRAIN_DENSITY, ParameterIDs::SYN_OSC_A_GRAIN_SIZE,
                 ParameterIDs::SYN_OSC_A_GRAIN_SPRAY, ParameterIDs::SYN_OSC_A_GRAIN_SHAPE, ParameterIDs::SYN_OSC_A_GRAIN_KEY, "A");
    addGrainOsc (ParameterIDs::SYN_OSC_B_GRAIN_SCAN, ParameterIDs::SYN_OSC_B_GRAIN_DENSITY, ParameterIDs::SYN_OSC_B_GRAIN_SIZE,
                 ParameterIDs::SYN_OSC_B_GRAIN_SPRAY, ParameterIDs::SYN_OSC_B_GRAIN_SHAPE, ParameterIDs::SYN_OSC_B_GRAIN_KEY, "B");
    addGrainOsc (ParameterIDs::SYN_OSC_C_GRAIN_SCAN, ParameterIDs::SYN_OSC_C_GRAIN_DENSITY, ParameterIDs::SYN_OSC_C_GRAIN_SIZE,
                 ParameterIDs::SYN_OSC_C_GRAIN_SPRAY, ParameterIDs::SYN_OSC_C_GRAIN_SHAPE, ParameterIDs::SYN_OSC_C_GRAIN_KEY, "C");
    addGrainOsc (ParameterIDs::SYN_OSC_D_GRAIN_SCAN, ParameterIDs::SYN_OSC_D_GRAIN_DENSITY, ParameterIDs::SYN_OSC_D_GRAIN_SIZE,
                 ParameterIDs::SYN_OSC_D_GRAIN_SPRAY, ParameterIDs::SYN_OSC_D_GRAIN_SHAPE, ParameterIDs::SYN_OSC_D_GRAIN_KEY, "D");

    // ════════ FM-ENGINE-PARAMS — per-OSC wavetable-carrier FM (2026-07-04) ════════
    // Carrier = the osc's own wavetable; M1/M2 = sine modulators. Ratio 0.25..16 skewed
    // so the musical 0.5..4 zone gets most of the knob throw; depth/feedback are squared
    // in the voice for a musical taper. Defaults (Ratio1 1, Depth1 0.35) = instant
    // classic 1:1 FM warmth the moment the engine is selected.
    auto addFmOsc = [&layout] (const char* algoId, const char* r1Id, const char* d1Id,
                               const char* r2Id, const char* d2Id, const char* fbId,
                               const juce::String& osc)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { algoId, 1 }, "Synth OSC " + osc + " FM Algo",
            juce::StringArray { "Stack", "Split", "Ring" }, 0));
        // RATIO QUANTIZE (Digitone's trick): 0.25 steps — every click of the knob is a
        // DIFFERENT harmonic identity, night-and-day audible. The in-between inharmonic
        // colors now live on RUST/AGE instead of accidental knob positions.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { r1Id, 1 },   "Synth OSC " + osc + " FM Ratio 1",
            juce::NormalisableRange<float> (0.25f, 16.0f, 0.25f, 0.5f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { d1Id, 1 },   "Synth OSC " + osc + " FM Depth 1",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { r2Id, 1 },   "Synth OSC " + osc + " FM Ratio 2",
            juce::NormalisableRange<float> (0.25f, 16.0f, 0.25f, 0.5f), 2.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { d2Id, 1 },   "Synth OSC " + osc + " FM Depth 2",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { fbId, 1 },   "Synth OSC " + osc + " FM Feedback",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    };
    addFmOsc (ParameterIDs::SYN_OSC_A_FM_ALGO, ParameterIDs::SYN_OSC_A_FM_RATIO1, ParameterIDs::SYN_OSC_A_FM_DEPTH1,
              ParameterIDs::SYN_OSC_A_FM_RATIO2, ParameterIDs::SYN_OSC_A_FM_DEPTH2, ParameterIDs::SYN_OSC_A_FM_FB, "A");
    addFmOsc (ParameterIDs::SYN_OSC_B_FM_ALGO, ParameterIDs::SYN_OSC_B_FM_RATIO1, ParameterIDs::SYN_OSC_B_FM_DEPTH1,
              ParameterIDs::SYN_OSC_B_FM_RATIO2, ParameterIDs::SYN_OSC_B_FM_DEPTH2, ParameterIDs::SYN_OSC_B_FM_FB, "B");
    addFmOsc (ParameterIDs::SYN_OSC_C_FM_ALGO, ParameterIDs::SYN_OSC_C_FM_RATIO1, ParameterIDs::SYN_OSC_C_FM_DEPTH1,
              ParameterIDs::SYN_OSC_C_FM_RATIO2, ParameterIDs::SYN_OSC_C_FM_DEPTH2, ParameterIDs::SYN_OSC_C_FM_FB, "C");
    addFmOsc (ParameterIDs::SYN_OSC_D_FM_ALGO, ParameterIDs::SYN_OSC_D_FM_RATIO1, ParameterIDs::SYN_OSC_D_FM_DEPTH1,
              ParameterIDs::SYN_OSC_D_FM_RATIO2, ParameterIDs::SYN_OSC_D_FM_DEPTH2, ParameterIDs::SYN_OSC_D_FM_FB, "D");

    // ── FM WEATHERING SUITE — page-2 functions (Strike/Age/Rust/Quake/Scorch/Storm),
    //    all 0..1 default 0 = page 2 untouched changes NOTHING (backward compatible).
    //    NOTE: param IDs stay …_FM_GALE (→Quake) / …_FM_BEND (→Scorch) — frozen to keep
    //    the WebView bind chain + saved sessions intact; only the display name changed. ──
    auto addFmWeather = [&layout] (const char* strikeId, const char* ageId, const char* rustId,
                                   const char* galeId, const char* bendId, const char* stormId,
                                   const juce::String& osc)
    {
        auto addF = [&layout, &osc] (const char* id, const char* nm)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, "Synth OSC " + osc + " FM " + nm,
                juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        };
        addF (strikeId, "Strike"); addF (ageId, "Age");   addF (rustId, "Rust");
        addF (galeId, "Quake");    addF (bendId, "Scorch"); addF (stormId, "Storm");
    };
    addFmWeather (ParameterIDs::SYN_OSC_A_FM_STRIKE, ParameterIDs::SYN_OSC_A_FM_AGE, ParameterIDs::SYN_OSC_A_FM_RUST,
                  ParameterIDs::SYN_OSC_A_FM_GALE, ParameterIDs::SYN_OSC_A_FM_BEND, ParameterIDs::SYN_OSC_A_FM_STORM, "A");
    addFmWeather (ParameterIDs::SYN_OSC_B_FM_STRIKE, ParameterIDs::SYN_OSC_B_FM_AGE, ParameterIDs::SYN_OSC_B_FM_RUST,
                  ParameterIDs::SYN_OSC_B_FM_GALE, ParameterIDs::SYN_OSC_B_FM_BEND, ParameterIDs::SYN_OSC_B_FM_STORM, "B");
    addFmWeather (ParameterIDs::SYN_OSC_C_FM_STRIKE, ParameterIDs::SYN_OSC_C_FM_AGE, ParameterIDs::SYN_OSC_C_FM_RUST,
                  ParameterIDs::SYN_OSC_C_FM_GALE, ParameterIDs::SYN_OSC_C_FM_BEND, ParameterIDs::SYN_OSC_C_FM_STORM, "C");
    addFmWeather (ParameterIDs::SYN_OSC_D_FM_STRIKE, ParameterIDs::SYN_OSC_D_FM_AGE, ParameterIDs::SYN_OSC_D_FM_RUST,
                  ParameterIDs::SYN_OSC_D_FM_GALE, ParameterIDs::SYN_OSC_D_FM_BEND, ParameterIDs::SYN_OSC_D_FM_STORM, "D");

    // ════════ HARM-ENGINE-PARAMS — per-OSC HARMONIC additive oscillator (2026-07-08) ════════
    // Pure additive: 512-partial banks built procedurally per block. MODE = base spectrum
    // family (HUE morphs the regime inside it), SCULPT = spectral transform (CARVE = depth,
    // CHURN = motion rate). Defaults land on an instant playable saw-family voice.
    auto addHarmOsc = [&layout] (const char* const id[14], const juce::String& osc)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id[0], 1 }, "Synth OSC " + osc + " Harmonic Mode",
            juce::StringArray { "Blade", "Neon", "Console", "Chant", "Bronze", "Hornet" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id[1], 1 }, "Synth OSC " + osc + " Harmonic Sculpt",
            juce::StringArray { "Keel", "Splay", "Cull", "Tide", "Terrace", "Clang" }, 0));
        static const char* nm[12] = { "Hue", "Partials", "Lean", "Fan", "Grit", "Braid",
                                      "Carve", "Churn", "Root", "Shine", "Wilt", "Forge" };
        static const float dv[12] = { 0.35f, 0.5f, 0.5f, 0.f, 0.f, 0.f,
                                      0.f, 0.5f, 0.f, 0.f, 0.5f, 0.f };
        for (int k = 0; k < 12; ++k)
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id[k + 2], 1 }, "Synth OSC " + osc + " Harmonic " + nm[k],
                juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), dv[k]));
    };
    {
        static const char* const HARM_A[14] = { ParameterIDs::SYN_OSC_A_HARM_MODE, ParameterIDs::SYN_OSC_A_HARM_SCULPT, ParameterIDs::SYN_OSC_A_HARM_HUE, ParameterIDs::SYN_OSC_A_HARM_COUNT, ParameterIDs::SYN_OSC_A_HARM_LEAN, ParameterIDs::SYN_OSC_A_HARM_FAN, ParameterIDs::SYN_OSC_A_HARM_GRIT, ParameterIDs::SYN_OSC_A_HARM_BRAID, ParameterIDs::SYN_OSC_A_HARM_CARVE, ParameterIDs::SYN_OSC_A_HARM_CHURN, ParameterIDs::SYN_OSC_A_HARM_ROOT, ParameterIDs::SYN_OSC_A_HARM_SHINE, ParameterIDs::SYN_OSC_A_HARM_WILT, ParameterIDs::SYN_OSC_A_HARM_FIZZ };
        static const char* const HARM_B[14] = { ParameterIDs::SYN_OSC_B_HARM_MODE, ParameterIDs::SYN_OSC_B_HARM_SCULPT, ParameterIDs::SYN_OSC_B_HARM_HUE, ParameterIDs::SYN_OSC_B_HARM_COUNT, ParameterIDs::SYN_OSC_B_HARM_LEAN, ParameterIDs::SYN_OSC_B_HARM_FAN, ParameterIDs::SYN_OSC_B_HARM_GRIT, ParameterIDs::SYN_OSC_B_HARM_BRAID, ParameterIDs::SYN_OSC_B_HARM_CARVE, ParameterIDs::SYN_OSC_B_HARM_CHURN, ParameterIDs::SYN_OSC_B_HARM_ROOT, ParameterIDs::SYN_OSC_B_HARM_SHINE, ParameterIDs::SYN_OSC_B_HARM_WILT, ParameterIDs::SYN_OSC_B_HARM_FIZZ };
        static const char* const HARM_C[14] = { ParameterIDs::SYN_OSC_C_HARM_MODE, ParameterIDs::SYN_OSC_C_HARM_SCULPT, ParameterIDs::SYN_OSC_C_HARM_HUE, ParameterIDs::SYN_OSC_C_HARM_COUNT, ParameterIDs::SYN_OSC_C_HARM_LEAN, ParameterIDs::SYN_OSC_C_HARM_FAN, ParameterIDs::SYN_OSC_C_HARM_GRIT, ParameterIDs::SYN_OSC_C_HARM_BRAID, ParameterIDs::SYN_OSC_C_HARM_CARVE, ParameterIDs::SYN_OSC_C_HARM_CHURN, ParameterIDs::SYN_OSC_C_HARM_ROOT, ParameterIDs::SYN_OSC_C_HARM_SHINE, ParameterIDs::SYN_OSC_C_HARM_WILT, ParameterIDs::SYN_OSC_C_HARM_FIZZ };
        static const char* const HARM_D[14] = { ParameterIDs::SYN_OSC_D_HARM_MODE, ParameterIDs::SYN_OSC_D_HARM_SCULPT, ParameterIDs::SYN_OSC_D_HARM_HUE, ParameterIDs::SYN_OSC_D_HARM_COUNT, ParameterIDs::SYN_OSC_D_HARM_LEAN, ParameterIDs::SYN_OSC_D_HARM_FAN, ParameterIDs::SYN_OSC_D_HARM_GRIT, ParameterIDs::SYN_OSC_D_HARM_BRAID, ParameterIDs::SYN_OSC_D_HARM_CARVE, ParameterIDs::SYN_OSC_D_HARM_CHURN, ParameterIDs::SYN_OSC_D_HARM_ROOT, ParameterIDs::SYN_OSC_D_HARM_SHINE, ParameterIDs::SYN_OSC_D_HARM_WILT, ParameterIDs::SYN_OSC_D_HARM_FIZZ };
        addHarmOsc (HARM_A, "A"); addHarmOsc (HARM_B, "B"); addHarmOsc (HARM_C, "C"); addHarmOsc (HARM_D, "D");
    }

    // ── MODAL engine params: 3 selectors (Family/Form/Source) + 10 knobs, ×4 oscs ──
    auto addModalOsc = [&layout] (const char* const id[13], const juce::String& osc)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id[0], 1 }, "Synth OSC " + osc + " Modal Family",
            juce::StringArray { "Grand", "Pluck", "Bow", "Flute", "Reed", "Brass", "Bars", "Bells", "Skin" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id[1], 1 }, "Synth OSC " + osc + " Modal Form",
            juce::StringArray { "I", "II", "III", "IV", "V" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id[2], 1 }, "Synth OSC " + osc + " Modal Source",
            juce::StringArray { "Auto", "Noise", "Click", "Sample" }, 0));
        static const char* nm[10] = { "Hard", "Pos", "Decay", "Material", "Breath",
                                      "Stretch", "Bloom", "Halo", "Age", "Body" };
        static const float dv[10] = { 0.5f, 0.28f, 0.6f, 0.5f, 0.0f,
                                      0.5f, 0.0f, 0.0f, 0.0f, 0.5f };
        for (int k = 0; k < 10; ++k)
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id[k + 3], 1 }, "Synth OSC " + osc + " Modal " + nm[k],
                juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), dv[k]));
    };
    {
        static const char* const MODAL_A[13] = { ParameterIDs::SYN_OSC_A_MODAL_FAMILY, ParameterIDs::SYN_OSC_A_MODAL_FORM, ParameterIDs::SYN_OSC_A_MODAL_SOURCE, ParameterIDs::SYN_OSC_A_MODAL_HARD, ParameterIDs::SYN_OSC_A_MODAL_POS, ParameterIDs::SYN_OSC_A_MODAL_DECAY, ParameterIDs::SYN_OSC_A_MODAL_MATERIAL, ParameterIDs::SYN_OSC_A_MODAL_BREATH, ParameterIDs::SYN_OSC_A_MODAL_STRETCH, ParameterIDs::SYN_OSC_A_MODAL_BLOOM, ParameterIDs::SYN_OSC_A_MODAL_HALO, ParameterIDs::SYN_OSC_A_MODAL_AGE, ParameterIDs::SYN_OSC_A_MODAL_BODY };
        static const char* const MODAL_B[13] = { ParameterIDs::SYN_OSC_B_MODAL_FAMILY, ParameterIDs::SYN_OSC_B_MODAL_FORM, ParameterIDs::SYN_OSC_B_MODAL_SOURCE, ParameterIDs::SYN_OSC_B_MODAL_HARD, ParameterIDs::SYN_OSC_B_MODAL_POS, ParameterIDs::SYN_OSC_B_MODAL_DECAY, ParameterIDs::SYN_OSC_B_MODAL_MATERIAL, ParameterIDs::SYN_OSC_B_MODAL_BREATH, ParameterIDs::SYN_OSC_B_MODAL_STRETCH, ParameterIDs::SYN_OSC_B_MODAL_BLOOM, ParameterIDs::SYN_OSC_B_MODAL_HALO, ParameterIDs::SYN_OSC_B_MODAL_AGE, ParameterIDs::SYN_OSC_B_MODAL_BODY };
        static const char* const MODAL_C[13] = { ParameterIDs::SYN_OSC_C_MODAL_FAMILY, ParameterIDs::SYN_OSC_C_MODAL_FORM, ParameterIDs::SYN_OSC_C_MODAL_SOURCE, ParameterIDs::SYN_OSC_C_MODAL_HARD, ParameterIDs::SYN_OSC_C_MODAL_POS, ParameterIDs::SYN_OSC_C_MODAL_DECAY, ParameterIDs::SYN_OSC_C_MODAL_MATERIAL, ParameterIDs::SYN_OSC_C_MODAL_BREATH, ParameterIDs::SYN_OSC_C_MODAL_STRETCH, ParameterIDs::SYN_OSC_C_MODAL_BLOOM, ParameterIDs::SYN_OSC_C_MODAL_HALO, ParameterIDs::SYN_OSC_C_MODAL_AGE, ParameterIDs::SYN_OSC_C_MODAL_BODY };
        static const char* const MODAL_D[13] = { ParameterIDs::SYN_OSC_D_MODAL_FAMILY, ParameterIDs::SYN_OSC_D_MODAL_FORM, ParameterIDs::SYN_OSC_D_MODAL_SOURCE, ParameterIDs::SYN_OSC_D_MODAL_HARD, ParameterIDs::SYN_OSC_D_MODAL_POS, ParameterIDs::SYN_OSC_D_MODAL_DECAY, ParameterIDs::SYN_OSC_D_MODAL_MATERIAL, ParameterIDs::SYN_OSC_D_MODAL_BREATH, ParameterIDs::SYN_OSC_D_MODAL_STRETCH, ParameterIDs::SYN_OSC_D_MODAL_BLOOM, ParameterIDs::SYN_OSC_D_MODAL_HALO, ParameterIDs::SYN_OSC_D_MODAL_AGE, ParameterIDs::SYN_OSC_D_MODAL_BODY };
        addModalOsc (MODAL_A, "A"); addModalOsc (MODAL_B, "B"); addModalOsc (MODAL_C, "C"); addModalOsc (MODAL_D, "D");
    }

    // ── BLEND MODES: 4 warp slots (B1..B4) × 4 oscs — cross-osc FM/PD/AM/RM/… ──
    {
        auto addBlendSlots = [&layout] (const char* const id[12], const juce::String& osc)
        {
            const juce::StringArray modes { "Off", "FM", "PD", "AM", "RM", "Sync", "Warp", "Dist", "Filter" };
            const juce::StringArray srcs  { "Osc A", "Osc B", "Osc C", "Osc D", "Sub", "Noise", "Self",
                                            "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8", "LFO 9", "LFO 10" };   // fb223 — WARP x LFO: drawn shapes as blend sources (APPEND-ONLY; JS NSRC must equal this count)
            for (int s = 0; s < 4; ++s)
            {
                const juce::String n = "Synth OSC " + osc + " Blend " + juce::String (s + 1) + " ";
                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    juce::ParameterID { id[s * 3 + 0], 1 }, n + "Mode",   modes, 0));
                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    juce::ParameterID { id[s * 3 + 1], 1 }, n + "Source", srcs,  1));   // default Osc B (moot while Off)
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    juce::ParameterID { id[s * 3 + 2], 1 }, n + "Depth",
                    juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
            }
        };
        static const char* const WS_A[12] = { ParameterIDs::SYN_OSC_A_WSLOT1_MODE, ParameterIDs::SYN_OSC_A_WSLOT1_SRC, ParameterIDs::SYN_OSC_A_WSLOT1_DEPTH, ParameterIDs::SYN_OSC_A_WSLOT2_MODE, ParameterIDs::SYN_OSC_A_WSLOT2_SRC, ParameterIDs::SYN_OSC_A_WSLOT2_DEPTH, ParameterIDs::SYN_OSC_A_WSLOT3_MODE, ParameterIDs::SYN_OSC_A_WSLOT3_SRC, ParameterIDs::SYN_OSC_A_WSLOT3_DEPTH, ParameterIDs::SYN_OSC_A_WSLOT4_MODE, ParameterIDs::SYN_OSC_A_WSLOT4_SRC, ParameterIDs::SYN_OSC_A_WSLOT4_DEPTH };
        static const char* const WS_B[12] = { ParameterIDs::SYN_OSC_B_WSLOT1_MODE, ParameterIDs::SYN_OSC_B_WSLOT1_SRC, ParameterIDs::SYN_OSC_B_WSLOT1_DEPTH, ParameterIDs::SYN_OSC_B_WSLOT2_MODE, ParameterIDs::SYN_OSC_B_WSLOT2_SRC, ParameterIDs::SYN_OSC_B_WSLOT2_DEPTH, ParameterIDs::SYN_OSC_B_WSLOT3_MODE, ParameterIDs::SYN_OSC_B_WSLOT3_SRC, ParameterIDs::SYN_OSC_B_WSLOT3_DEPTH, ParameterIDs::SYN_OSC_B_WSLOT4_MODE, ParameterIDs::SYN_OSC_B_WSLOT4_SRC, ParameterIDs::SYN_OSC_B_WSLOT4_DEPTH };
        static const char* const WS_C[12] = { ParameterIDs::SYN_OSC_C_WSLOT1_MODE, ParameterIDs::SYN_OSC_C_WSLOT1_SRC, ParameterIDs::SYN_OSC_C_WSLOT1_DEPTH, ParameterIDs::SYN_OSC_C_WSLOT2_MODE, ParameterIDs::SYN_OSC_C_WSLOT2_SRC, ParameterIDs::SYN_OSC_C_WSLOT2_DEPTH, ParameterIDs::SYN_OSC_C_WSLOT3_MODE, ParameterIDs::SYN_OSC_C_WSLOT3_SRC, ParameterIDs::SYN_OSC_C_WSLOT3_DEPTH, ParameterIDs::SYN_OSC_C_WSLOT4_MODE, ParameterIDs::SYN_OSC_C_WSLOT4_SRC, ParameterIDs::SYN_OSC_C_WSLOT4_DEPTH };
        static const char* const WS_D[12] = { ParameterIDs::SYN_OSC_D_WSLOT1_MODE, ParameterIDs::SYN_OSC_D_WSLOT1_SRC, ParameterIDs::SYN_OSC_D_WSLOT1_DEPTH, ParameterIDs::SYN_OSC_D_WSLOT2_MODE, ParameterIDs::SYN_OSC_D_WSLOT2_SRC, ParameterIDs::SYN_OSC_D_WSLOT2_DEPTH, ParameterIDs::SYN_OSC_D_WSLOT3_MODE, ParameterIDs::SYN_OSC_D_WSLOT3_SRC, ParameterIDs::SYN_OSC_D_WSLOT3_DEPTH, ParameterIDs::SYN_OSC_D_WSLOT4_MODE, ParameterIDs::SYN_OSC_D_WSLOT4_SRC, ParameterIDs::SYN_OSC_D_WSLOT4_DEPTH };
        addBlendSlots (WS_A, "A"); addBlendSlots (WS_B, "B"); addBlendSlots (WS_C, "C"); addBlendSlots (WS_D, "D");
    }

    // ════════ UNIVERSAL OSC BOXES (2026-07-09) — COARSE + SUB per oscillator ════════
    // COARSE = continuous ±64 st, NO snap (the smooth modulatable pitch lane — Serum CRS).
    // SUB = voice-anchored sub osc: Range / Form / Weight / Heat. Defaults keep it OFF.
    {
        auto addSubCoarse = [&layout] (const char* coarse, const char* range, const char* form,
                                       const char* weight, const char* heat, const juce::String& osc)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { coarse, 1 }, "Synth OSC " + osc + " Coarse",
                juce::NormalisableRange<float> (-64.0f, 64.0f, 0.0f), 0.0f));
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { range, 1 }, "Synth OSC " + osc + " Sub Octave",
                juce::StringArray { "-4 Oct", "-3 Oct", "-2 Oct", "-1 Oct", "0 Oct",
                                    "+1 Oct", "+2 Oct", "+3 Oct", "+4 Oct" }, 4));   // default index 4 = 0 Oct (regular pitch)
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { form, 1 }, "Synth OSC " + osc + " Sub Shape",
                juce::StringArray { "Sine", "Triangle", "Square", "Saw" }, 0));
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { weight, 1 }, "Synth OSC " + osc + " Sub Mix",
                juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { heat, 1 }, "Synth OSC " + osc + " Sub Drive",
                juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        };
        addSubCoarse (ParameterIDs::SYN_OSC_A_COARSE, ParameterIDs::SYN_OSC_A_SUB_RANGE, ParameterIDs::SYN_OSC_A_SUB_FORM, ParameterIDs::SYN_OSC_A_SUB_WEIGHT, ParameterIDs::SYN_OSC_A_SUB_HEAT, "A");
        addSubCoarse (ParameterIDs::SYN_OSC_B_COARSE, ParameterIDs::SYN_OSC_B_SUB_RANGE, ParameterIDs::SYN_OSC_B_SUB_FORM, ParameterIDs::SYN_OSC_B_SUB_WEIGHT, ParameterIDs::SYN_OSC_B_SUB_HEAT, "B");
        addSubCoarse (ParameterIDs::SYN_OSC_C_COARSE, ParameterIDs::SYN_OSC_C_SUB_RANGE, ParameterIDs::SYN_OSC_C_SUB_FORM, ParameterIDs::SYN_OSC_C_SUB_WEIGHT, ParameterIDs::SYN_OSC_C_SUB_HEAT, "C");
        addSubCoarse (ParameterIDs::SYN_OSC_D_COARSE, ParameterIDs::SYN_OSC_D_SUB_RANGE, ParameterIDs::SYN_OSC_D_SUB_FORM, ParameterIDs::SYN_OSC_D_SUB_WEIGHT, ParameterIDs::SYN_OSC_D_SUB_HEAT, "D");

        // ── NOISE ENGINE (center module — one shared source, routed through Filter 1) ──
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParameterIDs::SYN_NOISE_ON, 1 }, "Noise On", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIDs::SYN_NOISE_TYPE, 1 }, "Noise Type",
            juce::StringArray { "White Noise", "Pink Noise", "Brown Noise", "Geiger",
                                "Tape Hiss", "Tape Hum", "Tape Air", "Tape Crackle",
                                "Clean Vinyl", "Dirty Vinyl",
                                "Space Open", "Space Helium", "Space Wind" }, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParameterIDs::SYN_NOISE_LEVEL, 1 }, "Noise Level",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.08f));   // fb80 — was 0.35: the noise gets LOUD past ~20-30% (Max: default 5-10%)
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParameterIDs::SYN_NOISE_PITCH, 1 }, "Noise Scan",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParameterIDs::SYN_NOISE_PAN, 1 }, "Noise Pan",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        // fb66 — PLAY MODE (sample playback only; algorithmic types ignore it). Default Random =
        // today's accidental feel made deliberate (each note enters the loop at a fresh spot).
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIDs::SYN_NOISE_PLAYMODE, 1 }, "Noise Play Mode",
            juce::StringArray { "Random", "Envelope", "Free" }, 0));
        // fb69 — STEREO WIDTH (M/S on the noise output): 0 = mono, 1 = normal (default, identity), 2 = wide.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParameterIDs::SYN_NOISE_WIDTH, 1 }, "Noise Width",
            juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f));
    }

    // ════════ RESYNTH-ENGINE-PARAMS — per-OSC resynthesis oscillator (Engine::SPEC) ════════
    // ID strings keep GEODE_* for preset stability; meaning REMAPPED (see the gather):
    //   Page 1: Scan(CREEP)/Stretch(FOSSIL)/Sieve/Cut/Shape(DISTILL)/Drive(HAZE)
    //   Page 2: Quality/Formant/Tilt/Crush(SILT)/Start(POSITION)/Melt(FRACTURE — temporal smear)
    //   + Formant-Keep + Loop + Shape-Target/Cut-Mode/Drive-Mode/Sieve-Mode (choices). BEDROCK reserved.
    auto addGeodeOsc = [&layout] (const char* const* id, const juce::String& osc)
    {
        auto F = [&layout, &osc] (const char* pid, const char* nm, float def)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { pid, 1 }, "Synth OSC " + osc + " Resynth " + nm,
                juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), def));
        };
        // Neutral/faithful defaults: SCAN 0.5 = natural play-through, QUALITY high, everything else off.
        F (id[0], "Start",    0.0f);  F (id[1], "Stretch", 0.0f);  F (id[2],  "Scan",    0.5f);
        F (id[3], "Crush",    0.0f);  F (id[4], "Formant", 0.5f);  F (id[5],  "Cut",     1.0f);
        F (id[6], "Sieve",    0.0f);  F (id[7], "Shape",   0.0f);  F (id[8],  "Drive",   0.0f);
        F (id[9], "Melt",     0.0f);  F (id[10],"Tilt",    0.5f);  F (id[11], "Quality", 0.80f);   // Melt = FRACTURE id repurposed (was reserved @0.5 → now 0 = off)
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id[12], 1 }, "Synth OSC " + osc + " Resynth Formant Keep", true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id[13], 1 }, "Synth OSC " + osc + " Resynth Loop",
            juce::StringArray { "One-Shot", "Forward", "Reverse", "Ping-Pong" }, 1));
        F (id[14], "Bedrock", 0.5f);   // RESERVED (unused)
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id[15], 1 }, "Synth OSC " + osc + " Resynth Shape Target",
            juce::StringArray { "Sine", "Square", "Saw", "Triangle", "Pulse", "Hollow",
                                "Organ", "Half", "Vowel", "Bright", "Metal" }, 2));   // default Saw (0..10 = shapeWeight cases)
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id[16], 1 }, "Synth OSC " + osc + " Resynth Cut Mode",
            juce::StringArray { "LP", "HP" }, 0));                // default LP
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id[17], 1 }, "Synth OSC " + osc + " Resynth Drive Mode",
            juce::StringArray { "Saturate", "Bloom", "Glint", "Moire", "Foldback", "Ember" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id[18], 1 }, "Synth OSC " + osc + " Resynth Sieve Mode",
            juce::StringArray { "Floor", "Sparse", "Cloak", "Flicker", "Rake", "Parity" }, 0));
    };
    static const char* const GEODE_A[19] = {
        ParameterIDs::SYN_OSC_A_GEODE_POSITION, ParameterIDs::SYN_OSC_A_GEODE_FOSSIL, ParameterIDs::SYN_OSC_A_GEODE_CREEP,
        ParameterIDs::SYN_OSC_A_GEODE_SILT, ParameterIDs::SYN_OSC_A_GEODE_FORMANT, ParameterIDs::SYN_OSC_A_GEODE_CUT,
        ParameterIDs::SYN_OSC_A_GEODE_SIEVE, ParameterIDs::SYN_OSC_A_GEODE_DISTILL, ParameterIDs::SYN_OSC_A_GEODE_HAZE,
        ParameterIDs::SYN_OSC_A_GEODE_FRACTURE, ParameterIDs::SYN_OSC_A_GEODE_TILT, ParameterIDs::SYN_OSC_A_GEODE_QUALITY,
        ParameterIDs::SYN_OSC_A_GEODE_FKEEP, ParameterIDs::SYN_OSC_A_GEODE_LOOP, ParameterIDs::SYN_OSC_A_GEODE_BEDROCK,
        ParameterIDs::SYN_OSC_A_GEODE_SHAPE_TARGET, ParameterIDs::SYN_OSC_A_GEODE_CUT_MODE,
        ParameterIDs::SYN_OSC_A_GEODE_DRIVE_MODE, ParameterIDs::SYN_OSC_A_GEODE_SIEVE_MODE };
    static const char* const GEODE_B[19] = {
        ParameterIDs::SYN_OSC_B_GEODE_POSITION, ParameterIDs::SYN_OSC_B_GEODE_FOSSIL, ParameterIDs::SYN_OSC_B_GEODE_CREEP,
        ParameterIDs::SYN_OSC_B_GEODE_SILT, ParameterIDs::SYN_OSC_B_GEODE_FORMANT, ParameterIDs::SYN_OSC_B_GEODE_CUT,
        ParameterIDs::SYN_OSC_B_GEODE_SIEVE, ParameterIDs::SYN_OSC_B_GEODE_DISTILL, ParameterIDs::SYN_OSC_B_GEODE_HAZE,
        ParameterIDs::SYN_OSC_B_GEODE_FRACTURE, ParameterIDs::SYN_OSC_B_GEODE_TILT, ParameterIDs::SYN_OSC_B_GEODE_QUALITY,
        ParameterIDs::SYN_OSC_B_GEODE_FKEEP, ParameterIDs::SYN_OSC_B_GEODE_LOOP, ParameterIDs::SYN_OSC_B_GEODE_BEDROCK,
        ParameterIDs::SYN_OSC_B_GEODE_SHAPE_TARGET, ParameterIDs::SYN_OSC_B_GEODE_CUT_MODE,
        ParameterIDs::SYN_OSC_B_GEODE_DRIVE_MODE, ParameterIDs::SYN_OSC_B_GEODE_SIEVE_MODE };
    static const char* const GEODE_C[19] = {
        ParameterIDs::SYN_OSC_C_GEODE_POSITION, ParameterIDs::SYN_OSC_C_GEODE_FOSSIL, ParameterIDs::SYN_OSC_C_GEODE_CREEP,
        ParameterIDs::SYN_OSC_C_GEODE_SILT, ParameterIDs::SYN_OSC_C_GEODE_FORMANT, ParameterIDs::SYN_OSC_C_GEODE_CUT,
        ParameterIDs::SYN_OSC_C_GEODE_SIEVE, ParameterIDs::SYN_OSC_C_GEODE_DISTILL, ParameterIDs::SYN_OSC_C_GEODE_HAZE,
        ParameterIDs::SYN_OSC_C_GEODE_FRACTURE, ParameterIDs::SYN_OSC_C_GEODE_TILT, ParameterIDs::SYN_OSC_C_GEODE_QUALITY,
        ParameterIDs::SYN_OSC_C_GEODE_FKEEP, ParameterIDs::SYN_OSC_C_GEODE_LOOP, ParameterIDs::SYN_OSC_C_GEODE_BEDROCK,
        ParameterIDs::SYN_OSC_C_GEODE_SHAPE_TARGET, ParameterIDs::SYN_OSC_C_GEODE_CUT_MODE,
        ParameterIDs::SYN_OSC_C_GEODE_DRIVE_MODE, ParameterIDs::SYN_OSC_C_GEODE_SIEVE_MODE };
    static const char* const GEODE_D[19] = {
        ParameterIDs::SYN_OSC_D_GEODE_POSITION, ParameterIDs::SYN_OSC_D_GEODE_FOSSIL, ParameterIDs::SYN_OSC_D_GEODE_CREEP,
        ParameterIDs::SYN_OSC_D_GEODE_SILT, ParameterIDs::SYN_OSC_D_GEODE_FORMANT, ParameterIDs::SYN_OSC_D_GEODE_CUT,
        ParameterIDs::SYN_OSC_D_GEODE_SIEVE, ParameterIDs::SYN_OSC_D_GEODE_DISTILL, ParameterIDs::SYN_OSC_D_GEODE_HAZE,
        ParameterIDs::SYN_OSC_D_GEODE_FRACTURE, ParameterIDs::SYN_OSC_D_GEODE_TILT, ParameterIDs::SYN_OSC_D_GEODE_QUALITY,
        ParameterIDs::SYN_OSC_D_GEODE_FKEEP, ParameterIDs::SYN_OSC_D_GEODE_LOOP, ParameterIDs::SYN_OSC_D_GEODE_BEDROCK,
        ParameterIDs::SYN_OSC_D_GEODE_SHAPE_TARGET, ParameterIDs::SYN_OSC_D_GEODE_CUT_MODE,
        ParameterIDs::SYN_OSC_D_GEODE_DRIVE_MODE, ParameterIDs::SYN_OSC_D_GEODE_SIEVE_MODE };
    addGeodeOsc (GEODE_A, "A"); addGeodeOsc (GEODE_B, "B");
    addGeodeOsc (GEODE_C, "C"); addGeodeOsc (GEODE_D, "D");

    // BLEND — the 6 one-shot blend/morph knobs per osc (Morph Attack Body Breath Sculpt Dice).
    // OFFLINE-BAKE params: the editor listens and re-renders the blended buffer; the audio
    // thread only ever plays the published result. DICE defaults 0 (off), the rest centered.
    auto addBlendOsc = [&layout] (const char* morphId, const char* attackId, const char* bodyId,
                                  const char* breathId, const char* sculptId, const char* diceId,
                                  const juce::String& osc)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { morphId, 1 }, "Synth OSC " + osc + " Blend Morph",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { attackId, 1 }, "Synth OSC " + osc + " Blend Attack",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { bodyId, 1 }, "Synth OSC " + osc + " Blend Body",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { breathId, 1 }, "Synth OSC " + osc + " Blend Breath",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sculptId, 1 }, "Synth OSC " + osc + " Blend Sculpt",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { diceId, 1 }, "Synth OSC " + osc + " Blend Dice",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    };
    addBlendOsc (ParameterIDs::SYN_OSC_A_BLEND_MORPH, ParameterIDs::SYN_OSC_A_BLEND_ATTACK, ParameterIDs::SYN_OSC_A_BLEND_BODY,
                 ParameterIDs::SYN_OSC_A_BLEND_BREATH, ParameterIDs::SYN_OSC_A_BLEND_SCULPT, ParameterIDs::SYN_OSC_A_BLEND_DICE, "A");
    addBlendOsc (ParameterIDs::SYN_OSC_B_BLEND_MORPH, ParameterIDs::SYN_OSC_B_BLEND_ATTACK, ParameterIDs::SYN_OSC_B_BLEND_BODY,
                 ParameterIDs::SYN_OSC_B_BLEND_BREATH, ParameterIDs::SYN_OSC_B_BLEND_SCULPT, ParameterIDs::SYN_OSC_B_BLEND_DICE, "B");
    addBlendOsc (ParameterIDs::SYN_OSC_C_BLEND_MORPH, ParameterIDs::SYN_OSC_C_BLEND_ATTACK, ParameterIDs::SYN_OSC_C_BLEND_BODY,
                 ParameterIDs::SYN_OSC_C_BLEND_BREATH, ParameterIDs::SYN_OSC_C_BLEND_SCULPT, ParameterIDs::SYN_OSC_C_BLEND_DICE, "C");
    addBlendOsc (ParameterIDs::SYN_OSC_D_BLEND_MORPH, ParameterIDs::SYN_OSC_D_BLEND_ATTACK, ParameterIDs::SYN_OSC_D_BLEND_BODY,
                 ParameterIDs::SYN_OSC_D_BLEND_BREATH, ParameterIDs::SYN_OSC_D_BLEND_SCULPT, ParameterIDs::SYN_OSC_D_BLEND_DICE, "D");

    // FADE CURVES — Ableton-style curve diamond per region fade edge (0.5 = classic sin).
    auto addFadeCurve = [&layout] (const char* id, const juce::String& nm)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, "Synth OSC " + nm,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    };
    addFadeCurve (ParameterIDs::SYN_OSC_A_SAMPLE_FADEIN_CURVE,  "A Fade-In Curve");
    addFadeCurve (ParameterIDs::SYN_OSC_A_SAMPLE_FADEOUT_CURVE, "A Fade-Out Curve");
    addFadeCurve (ParameterIDs::SYN_OSC_B_SAMPLE_FADEIN_CURVE,  "B Fade-In Curve");
    addFadeCurve (ParameterIDs::SYN_OSC_B_SAMPLE_FADEOUT_CURVE, "B Fade-Out Curve");
    addFadeCurve (ParameterIDs::SYN_OSC_C_SAMPLE_FADEIN_CURVE,  "C Fade-In Curve");
    addFadeCurve (ParameterIDs::SYN_OSC_C_SAMPLE_FADEOUT_CURVE, "C Fade-Out Curve");
    addFadeCurve (ParameterIDs::SYN_OSC_D_SAMPLE_FADEIN_CURVE,  "D Fade-In Curve");
    addFadeCurve (ParameterIDs::SYN_OSC_D_SAMPLE_FADEOUT_CURVE, "D Fade-Out Curve");

    // GRAIN-EXPANDED — the 6 page-2 functions (defaults match GranularEngineParams).
    // Life + Jump removed (2026-07-02); Air + Stretch live on the waveform right-click, reusing
    // the Sample osc's SYN_OSC_x_SAMPLE_AIR / _STRETCH / _STRETCH_MODE params (no new params).
    auto addGrainExp = [&layout] (const char* posId, const char* pitchId, const char* psprayId, const char* widthId,
                                  const char* dirId, const char* skewId,
                                  const juce::String& osc)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { posId, 1 },    "Synth OSC " + osc + " Grain Position",    juce::NormalisableRange<float> ( 0.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pitchId, 1 },  "Synth OSC " + osc + " Grain Pitch",       juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { psprayId, 1 }, "Synth OSC " + osc + " Grain Pitch Spray", juce::NormalisableRange<float> ( 0.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { widthId, 1 },  "Synth OSC " + osc + " Grain Width",       juce::NormalisableRange<float> ( 0.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { dirId, 1 },    "Synth OSC " + osc + " Grain Direction",   juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { skewId, 1 },   "Synth OSC " + osc + " Grain Skew",        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    };
    addGrainExp (ParameterIDs::SYN_OSC_A_GRAIN_POSITION, ParameterIDs::SYN_OSC_A_GRAIN_PITCH, ParameterIDs::SYN_OSC_A_GRAIN_PSPRAY, ParameterIDs::SYN_OSC_A_GRAIN_WIDTH, ParameterIDs::SYN_OSC_A_GRAIN_DIR, ParameterIDs::SYN_OSC_A_GRAIN_SKEW, "A");
    addGrainExp (ParameterIDs::SYN_OSC_B_GRAIN_POSITION, ParameterIDs::SYN_OSC_B_GRAIN_PITCH, ParameterIDs::SYN_OSC_B_GRAIN_PSPRAY, ParameterIDs::SYN_OSC_B_GRAIN_WIDTH, ParameterIDs::SYN_OSC_B_GRAIN_DIR, ParameterIDs::SYN_OSC_B_GRAIN_SKEW, "B");
    addGrainExp (ParameterIDs::SYN_OSC_C_GRAIN_POSITION, ParameterIDs::SYN_OSC_C_GRAIN_PITCH, ParameterIDs::SYN_OSC_C_GRAIN_PSPRAY, ParameterIDs::SYN_OSC_C_GRAIN_WIDTH, ParameterIDs::SYN_OSC_C_GRAIN_DIR, ParameterIDs::SYN_OSC_C_GRAIN_SKEW, "C");
    addGrainExp (ParameterIDs::SYN_OSC_D_GRAIN_POSITION, ParameterIDs::SYN_OSC_D_GRAIN_PITCH, ParameterIDs::SYN_OSC_D_GRAIN_PSPRAY, ParameterIDs::SYN_OSC_D_GRAIN_WIDTH, ParameterIDs::SYN_OSC_D_GRAIN_DIR, ParameterIDs::SYN_OSC_D_GRAIN_SKEW, "D");

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_WARP_AMOUNT, 1 },
        "Synth OSC D Warp Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_WARP2_MODE, 1 },
        "Synth OSC D Warp 2 Mode",
        juce::StringArray { "NONE", "Bend", "Sync", "Formant", "PWM", "Skew", "Mirror", "Fractalize", "P-Quantize", "Rectify", "Sine Shaper" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_WARP2_AMT, 1 },
        "Synth OSC D Warp 2 Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // ── Phase 11a — OSC D wavetable rework foundation (SPECTRAL MORPH — Phase 11c) ─
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SPECTRAL_TYPE, 1 },
        "OSC D Spectral Type",
        juce::StringArray { "None", "Harmonic Stretch", "Inharmonic Stretch",
                            "Vocode", "Smear", "Random Amps", "Data Compress", "Spectral Phaser" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_SPECTRAL_AMT, 1 },
        "OSC D Spectral Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_FOLD_SHAPE, 1 },
        "OSC D Fold Shape",
        juce::StringArray { "Linear", "Sine", "Triangle" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_FOLD_AMT, 1 },
        "OSC D Fold Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_FRAME_SPREAD, 1 },
        "OSC D Blur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_D_INTERP_MODE, 1 },
        "OSC D Interp Mode",
        juce::StringArray { "Linear", "Stepped" },
        0));

    // ── Synth section — Phase 8a (Voice settings + flagship features) ────
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_VOICES, 1 },
        "Synth Voices", 1, 32, 8));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_UNISON, 1 },
        "Synth Unison", 1, 8, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_SPREAD, 1 },
        "Synth Spread",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_EROSION, 1 },
        "Synth Erosion",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_HORIZON, 1 },
        "Synth Horizon",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));

    // VOICING / PORTAMENTO
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_PORTA, 1 },
        "Synth Portamento",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (   // fb262 — velocity CURVE (repurposed from depth): 50=linear, >50 lifts soft hits
        juce::ParameterID { ParameterIDs::SYN_VEL_DEPTH, 1 },
        "Synth Velocity Curve",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));   // default 50 = linear response
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_GLIDE_CURVE, 1 },
        "Synth Glide Curve",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_GLIDE_ALWAYS, 1 }, "Synth Glide Always", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_GLIDE_SCALED, 1 }, "Synth Glide Scaled", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_MONO, 1 }, "Synth Mono", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterIDs::SYN_LEGATO, 1 }, "Synth Legato", false));

    // ── FLOW ───────────────────────────────────────────────────────────────
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_MODE, 1 }, "Flow Mode",
        juce::StringArray { "Off", "Arp", "Chop", "Glitch", "Robin" }, 0));   // 0 = Off; 2 = CHOP (replaced Seq); 4 = ROBIN (replaced Drift — index frozen; fb121: "Round Robin" -> "Robin", Max's name)
    // fb131 — MODE CHAIN: the ordered multi-select (click order = signal path). Each slot
    // shares FLOW_MODE's index space so a slot IS a mode id; "Off" = empty. All-empty =
    // legacy single FLOW_MODE (old saves untouched); a non-empty chain owns the truth.
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHAIN_1, 1 }, "Flow Chain 1",
        juce::StringArray { "Off", "Arp", "Chop", "Glitch", "Robin" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHAIN_2, 1 }, "Flow Chain 2",
        juce::StringArray { "Off", "Arp", "Chop", "Glitch", "Robin" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHAIN_3, 1 }, "Flow Chain 3",
        juce::StringArray { "Off", "Arp", "Chop", "Glitch", "Robin" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHAIN_4, 1 }, "Flow Chain 4",
        juce::StringArray { "Off", "Arp", "Chop", "Glitch", "Robin" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParameterIDs::FLOW_ARP_LATCH, 1 }, "Arp Latch", false));
    auto addFlowKnob = [&] (const char* id, const char* name, float def) {
        layout.add (std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float>(0.0f, 1.0f), def)); };
    addFlowKnob (ParameterIDs::FLOW_ARP_RATE,"Arp Rate",0.6111f);  addFlowKnob (ParameterIDs::FLOW_ARP_GATE,"Arp Gate",0.55f);   // fb107: rate default = 1/16 (rich idx 11)
    addFlowKnob (ParameterIDs::FLOW_ARP_VARY,"Arp Vary",0.00f);  addFlowKnob (ParameterIDs::FLOW_ARP_TRAJ,"Arp Traj",0.00f);
    addFlowKnob (ParameterIDs::FLOW_ARP_MORPH,"Arp Morph",0.00f);
    // mode-2 macros — IDs stay FLOW_SEQ_* (preset-stable) but now drive CHOP (Rate/Gate/Vary/Style/Morph)
    addFlowKnob (ParameterIDs::FLOW_SEQ_RATE,"Chop Rate",0.6111f);  addFlowKnob (ParameterIDs::FLOW_SEQ_GATE,"Chop Gate",0.55f);   // fb107: chop grid default = 1/16
    addFlowKnob (ParameterIDs::FLOW_SEQ_VARY,"Chop Vary",0.00f);  addFlowKnob (ParameterIDs::FLOW_SEQ_TRAJ,"Chop Style",0.00f);
    addFlowKnob (ParameterIDs::FLOW_SEQ_MORPH,"Chop Morph",0.00f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_BLEND,"Chop Blend",1.00f);   // dry/wet — fb131: default 100% wet (Max: "when I select it, the mix is at 100")
    addFlowKnob (ParameterIDs::FLOW_GLI_BLEND, "Glitch Blend",1.00f); // GLITCH dry/wet — fb131: default 100% wet (Max's law)
    addFlowKnob (ParameterIDs::FLOW_ARP_BLEND, "Arp Blend",1.00f);    // ARP vs dry held-chord — 1.0 = pure arp (normal); pull down to hear the sustained chord under it
    // ── ARP extension card (fb105): PLAY/MOTION scalars + 28 lane depth knobs.
    // Post-ceiling params: driven by setSynParam only (no relays). Choice params
    // are read as the INDEX ((int)*rawParam) per the CLAUDE.md hard rule.

    // ════════ FX RACK · REVERB (Hall) — fb276. setSynParam-only; choices = INDEX; routes default OFF. ════════
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SYN_RVB_TYPE, 1 }, "Reverb Type",
        juce::StringArray { "Hall","Room","Plate","Spring","Digital","Vintage","Basin","Shimmer","Convolution" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SYN_RVB_CHARACTER, 1 }, "Reverb Character",
        juce::StringArray { "Smooth","Random","Vintage","Cathedral","Chamber","Dark","Bright","Ethereal" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SYN_RVB_MODMODE, 1 }, "Reverb Mod Mode",
        juce::StringArray { "Off","Subtle","Lush","Chorale","Random","Chaos" }, 2));
    auto addRvbF = [&] (const char* id, const char* nm, float def) {
        layout.add (std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, nm, juce::NormalisableRange<float>(0.0f, 1.0f), def)); };
    addRvbF (ParameterIDs::SYN_RVB_SIZE,     "Reverb Size",       0.30f);
    addRvbF (ParameterIDs::SYN_RVB_DECAY,    "Reverb Decay",      0.55f);
    addRvbF (ParameterIDs::SYN_RVB_TONE,     "Reverb Tone",       0.50f);
    addRvbF (ParameterIDs::SYN_RVB_MIX,      "Reverb Mix",        0.35f);
    addRvbF (ParameterIDs::SYN_RVB_PREDELAY, "Reverb Pre-Delay",  0.10f);
    addRvbF (ParameterIDs::SYN_RVB_DIFFUSE,  "Reverb Diffusion",  0.70f);
    addRvbF (ParameterIDs::SYN_RVB_MODDEPTH, "Reverb Mod Depth",  0.25f);
    addRvbF (ParameterIDs::SYN_RVB_MODRATE,  "Reverb Mod Rate",   0.30f);
    addRvbF (ParameterIDs::SYN_RVB_HIDAMP,   "Reverb High Damp",  0.35f);
    addRvbF (ParameterIDs::SYN_RVB_LOWDECAY, "Reverb Low Decay",  0.50f);
    addRvbF (ParameterIDs::SYN_RVB_LOWCUT,   "Reverb Low Cut",    0.00f);
    addRvbF (ParameterIDs::SYN_RVB_WIDTH,    "Reverb Width",      0.80f);
    for (const char* rid : { ParameterIDs::SYN_RVB_SRC_A, ParameterIDs::SYN_RVB_SRC_B, ParameterIDs::SYN_RVB_SRC_C,
                             ParameterIDs::SYN_RVB_SRC_D, ParameterIDs::SYN_RVB_SRC_SUB, ParameterIDs::SYN_RVB_SRC_NOISE })
        layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { rid, 1 }, juce::String (rid), false));
    // fb279 — front Mod / Freeze toggles. fb303 — Mod default OFF (Max: static by default; mod is opt-in) + Freeze OFF.
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_RVB_MOD,    1 }, "Reverb Mod",    false));
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_RVB_FREEZE, 1 }, "Reverb Freeze", false));
    // fb287 device POWER + DUCK. fb303 — POWER default OFF: a fresh patch is DRY; turning a device ON with no
    // route pills = MAIN SEND (whole synth through it). Existing saved projects restore their own power state.
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_RVB_POWER,  1 }, "Reverb Power",  false));
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_RVB_DUCK,   1 }, "Reverb Duck",   false));

    // ════════ FX RACK · DELAY — fb296. setSynParam-only; choices = INDEX; routes default OFF; parallel to reverb. ════════
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SYN_DLY_TYPE, 1 }, "Delay Type",
        juce::StringArray { "Digital","Tape","BBD","Diffuse" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SYN_DLY_CHARACTER, 1 }, "Delay Character",
        juce::StringArray { "Clean","Warm","Vintage","Modern","Lo-Fi","Bright","Dark","Wide" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SYN_DLY_SYNCDIV, 1 }, "Delay Sync Division",
        // fb304 — HARD RULE: time range 4 bar → 1/256 (Max: no "fast" beyond 1/256). Index-aligned with the
        // multiplier switch in the delay block AND the UI dropdown. Default 1/8 (index 10).
        juce::StringArray { "Free","4 bar","2 bar","1 bar","1/2","1/2D","1/2T","1/4","1/4D","1/4T",
                            "1/8","1/8D","1/8T","1/16","1/16D","1/16T","1/32","1/64","1/128","1/256" }, 10));   // default 1/8
    layout.add (std::make_unique<juce::AudioParameterChoice>(                                       // fb306 — RIGHT sync division (unlinked)
        juce::ParameterID { ParameterIDs::SYN_DLY_SYNCDIV_R, 1 }, "Delay Sync Division R",
        juce::StringArray { "Free","4 bar","2 bar","1 bar","1/2","1/2D","1/2T","1/4","1/4D","1/4T",
                            "1/8","1/8D","1/8T","1/16","1/16D","1/16T","1/32","1/64","1/128","1/256" }, 10));   // default 1/8
    auto addDlyF = [&] (const char* id, const char* nm, float def) {
        layout.add (std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, nm, juce::NormalisableRange<float>(0.0f, 1.0f), def)); };
    addDlyF (ParameterIDs::SYN_DLY_TIME,     "Delay Time",       0.50f);
    addDlyF (ParameterIDs::SYN_DLY_TIME_R,   "Delay Time R",     0.50f);   // fb306 — RIGHT time (free ms) when unlinked
    addDlyF (ParameterIDs::SYN_DLY_FEEDBACK, "Delay Feedback",   0.10f);   // fb309 — LOW default (Max: turning the delay on at 58% + the amplified feedback = jumpscare). Safe/subtle on turn-on.
    addDlyF (ParameterIDs::SYN_DLY_TONE,     "Delay Tone",       0.44f);
    addDlyF (ParameterIDs::SYN_DLY_MIX,      "Delay Mix",        0.34f);
    addDlyF (ParameterIDs::SYN_DLY_LOWCUT,   "Delay Low Cut",    0.22f);
    addDlyF (ParameterIDs::SYN_DLY_HICUT,    "Delay Hi Cut",     0.72f);
    addDlyF (ParameterIDs::SYN_DLY_SPREAD,   "Delay Spread",     0.60f);
    addDlyF (ParameterIDs::SYN_DLY_WIDTH,    "Delay Width",      0.78f);
    addDlyF (ParameterIDs::SYN_DLY_MODRATE,  "Delay Mod Rate",   0.40f);
    addDlyF (ParameterIDs::SYN_DLY_MODDEPTH, "Delay Mod Depth",  0.0f);    // fb303 — Mod OFF by default (Max: delay was wonky-on-turn-on)
    addDlyF (ParameterIDs::SYN_DLY_WOW,      "Delay Wow",        0.0f);    // fb303 — OFF by default (Max: only natural/Tape wow)
    addDlyF (ParameterIDs::SYN_DLY_DUCK,     "Delay Ducking",    0.0f);    // fb303 — OFF by default (Max never liked ducking)
    for (const char* rid : { ParameterIDs::SYN_DLY_SRC_A, ParameterIDs::SYN_DLY_SRC_B, ParameterIDs::SYN_DLY_SRC_C,
                             ParameterIDs::SYN_DLY_SRC_D, ParameterIDs::SYN_DLY_SRC_SUB, ParameterIDs::SYN_DLY_SRC_NOISE })
        layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { rid, 1 }, juce::String (rid), false));
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_DLY_SYNC,  1 }, "Delay Sync",  true));
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_DLY_LINK,  1 }, "Delay Link L/R", true));   // fb306 — default LINKED (R follows L; byte-identical)
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_DLY_PING,  1 }, "Delay Ping-Pong", false));
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_DLY_POWER, 1 }, "Delay Power", false));   // fb303 — OFF by default (dry init; on = main send)
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_DLY_HQ,    1 }, "Delay HQ",    true));
    layout.add (std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { ParameterIDs::SYN_FX_ORDER, 1 }, "FX Chain Order",
        juce::StringArray { "Reverb > Delay > Distortion",       // 0 — legacy false (norm 0.0) restores EXACTLY
                            "Reverb > Distortion > Delay",
                            "Distortion > Reverb > Delay",
                            "Distortion > Delay > Reverb",
                            "Delay > Distortion > Reverb",
                            "Delay > Reverb > Distortion" },     // 5 — legacy true (norm 1.0) restores EXACTLY
        0));   // fb341 — the 6-way serial permutation (was a bool; index order anchors both legacy states)

    // ════════ FX RACK · DISTORTION — fb315. 23 modes / 6 families; back-8 keyed to the FAMILY.
    // POWER default OFF ⇒ dry init ⇒ byte-identical default sound (same contract as reverb + delay).
    // Spec: Design/DISTORTION-BUILD-BIBLE.md ════════
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SYN_DST_TYPE, 1 }, "Distortion Type",
        // INDEX-ALIGNED with the DSP switch AND the UI optgroup list. Grouped by family, in family order.
        juce::StringArray { "Tube","Tape","Transformer","Stomp Box","Overdrive",                  //  0-4  ANALOG
                            "Soft Clip","Hard Clip","Zero-Square","Slew Clip",                    //  5-8  CLIP
                            "Diode 1","Diode 2","Asym","Rectify",                                 //  9-12 DIODE
                            "Linear Fold","Sine Fold","West Coast",                               // 13-15 FOLD
                            "Shaper","Shaper Asym","Harmonics","Table",                           // 16-19 SHAPER
                            "Downsample","Bitcrush","Overflow" }, 5));                            // 20-22 DIGITAL — default Soft Clip
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SYN_DST_CHARACTER, 1 }, "Distortion Character",
        // Soft Clip's 8 voicings (the default mode). Relabelled per MODE by the UI; the DSP reads the index.
        juce::StringArray { "Diff Pair","Glue","Cubic","Sine","Asym","Slam","Squeeze","Wall" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SYN_DST_QUALITY, 1 }, "Distortion Quality",
        juce::StringArray { "Off","Standard","High","Ultra" }, 1));   // default Standard (= each mode's declared FLOOR)

    auto addDstF = [&] (const char* id, const char* nm, float def) {
        layout.add (std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, nm, juce::NormalisableRange<float>(0.0f, 1.0f), def)); };
    // fb309 LESSON APPLIED: default Drive lands the signal right AT the knee — audibly saturating the
    // moment you power it on, but NOT a blast. 0.20 ⇒ 48*0.20^0.8 ≈ +13.2 dB ⇒ a −26 dBFS bus signal
    // arrives at u ≈ 0.92, i.e. just touching the ±1 threshold. Working from the first degree of travel
    // (no dead first third) without the delay-feedback jumpscare. The RANGE still reaches +48 dB.
    addDstF (ParameterIDs::SYN_DST_DRIVE, "Distortion Drive", 0.20f);
    addDstF (ParameterIDs::SYN_DST_SIG,   "Distortion Knee",  0.65f);  // signature knob. 0.65 = Soft Clip's default Knee (Hard Clip's is 0.08 —
                                                                       // the two defaults sit FAR APART so the modes never boot into their overlap zone).
    addDstF (ParameterIDs::SYN_DST_TONE,  "Distortion Tone",  0.50f);  // post tilt, neutral
    addDstF (ParameterIDs::SYN_DST_MIX,   "Distortion Mix",   1.00f);  // a distortion is normally a full-wet insert
    // Back-8 (CLIP labels). All defaults are NO-OP values, so powering the device on gives clean drive
    // and nothing else — every back knob starts out of the way.
    addDstF (ParameterIDs::SYN_DST_P1, "Distortion Low Cut",  0.00f);  // pre HP, off
    addDstF (ParameterIDs::SYN_DST_P2, "Distortion Hi Cut",   1.00f);  // post LP, open
    addDstF (ParameterIDs::SYN_DST_P3, "Distortion Emphasis", 0.50f);  // pre/de tilt pair, neutral
    addDstF (ParameterIDs::SYN_DST_P4, "Distortion Width",    0.50f);  // M/S drive balance, matched
    addDstF (ParameterIDs::SYN_DST_P5, "Distortion Bias",     0.50f);  // centre = 0
    addDstF (ParameterIDs::SYN_DST_P6, "Distortion Gap",      0.00f);  // dead zone off
    addDstF (ParameterIDs::SYN_DST_P7, "Distortion Punch",    0.50f);  // centre = 0
    addDstF (ParameterIDs::SYN_DST_P8, "Distortion Feedback", 0.00f);  // exactly off (ADAA stays exact)
    for (const char* rid : { ParameterIDs::SYN_DST_SRC_A, ParameterIDs::SYN_DST_SRC_B, ParameterIDs::SYN_DST_SRC_C,
                             ParameterIDs::SYN_DST_SRC_D, ParameterIDs::SYN_DST_SRC_SUB, ParameterIDs::SYN_DST_SRC_NOISE })
        layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { rid, 1 }, juce::String (rid), false));
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_DST_POWER, 1 }, "Distortion Power", false));  // OFF = dry init
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_DST_AUTO,  1 }, "Distortion Auto",  false));  // OFF — see bible §2.6/§4.2
    layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParameterIDs::SYN_DST_PILL2, 1 }, "Distortion Wrap",  false));  // family-unique 2nd pill

    // ════════════════ fb346 — THE DYNAMIC CHAIN: chain membership + the instance pool ════════════════
    // WHY THIS SHAPE (read before touching): JUCE/VST3/AU cache the parameter list at load, so params
    // can never be born at runtime. The rack's + button therefore CLAIMS a pre-allocated instance; it
    // never creates one. The user only sees a rack that grows. Serum 2 does the same trick (its LFOs
    // 7-10 "appear" once you assign LFO 6).
    //
    // _ACTIVE = is this device IN the chain (the + adds it, the × removes it).
    // _POWER  = bypass (the power dot) — a DIFFERENT thing, and both must survive independently.
    // _RANK   = chain position, a FLOAT. This is why drag-reorder is now legal at all: a choice
    //           param's cardinality is frozen at birth (the fb342 law), which is exactly why the old
    //           6-way SYN_FX_ORDER could never describe a 4th device. Rank has no cardinality, so the
    //           chain can hold any number of devices in any order, and reordering is click-free
    //           (it re-sorts a list; it never renumbers a stored choice index).
    {
        // Instance 1 = the legacy blocks above (SYN_RVB_*/SYN_DLY_*/SYN_DST_*), so every one of the 66
        // factory presets and every user preset keeps loading byte-identically. Default chain = the
        // three shipped devices in their historical order, all ACTIVE (nothing changes for old sessions).
        auto addChainSlot = [&] (const char* activeId, const char* rankId, const char* nm,
                                 bool activeDef, float rankDef)
        {
            layout.add (std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID { activeId, 1 }, juce::String (nm) + " In Chain", activeDef));
            layout.add (std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { rankId, 1 }, juce::String (nm) + " Chain Rank",
                juce::NormalisableRange<float>(0.0f, 1.0f), rankDef));
        };
        addChainSlot (ParameterIDs::SYN_RVB_ACTIVE, ParameterIDs::SYN_RVB_RANK, "Reverb",     true, 0.10f);
        addChainSlot (ParameterIDs::SYN_DLY_ACTIVE, ParameterIDs::SYN_DLY_RANK, "Delay",      true, 0.20f);
        addChainSlot (ParameterIDs::SYN_DST_ACTIVE, ParameterIDs::SYN_DST_RANK, "Distortion", true, 0.30f);

        // ── Instances 2..kFxInstances. Delay and Distortion pool first because each is ONE engine
        //    object; the Reverb device is SIX (Space/Hall/Digital/Basin/Shimmer/Convolution) and gets
        //    lazily-built instances in the next pass rather than 6x memory per slot up front.
        const juce::StringArray dlyTypes { "Digital","Tape","BBD","Diffuse" };
        const juce::StringArray dlyChars { "Clean","Warm","Vintage","Modern","Lo-Fi","Bright","Dark","Wide" };
        const juce::StringArray syncDiv  { "Free","4 bar","2 bar","1 bar","1/2","1/2D","1/2T","1/4","1/4D","1/4T",
                                           "1/8","1/8D","1/8T","1/16","1/16D","1/16T","1/32","1/64","1/128","1/256" };
        const juce::StringArray dstTypes { "Tube","Tape","Transformer","Stomp Box","Overdrive",
                                           "Soft Clip","Hard Clip","Zero-Square","Slew Clip",
                                           "Diode 1","Diode 2","Asym","Rectify",
                                           "Linear Fold","Sine Fold","West Coast",
                                           "Shaper","Shaper Asym","Harmonics","Table",
                                           "Downsample","Bitcrush","Overflow" };
        const juce::StringArray dstChars { "Diff Pair","Glue","Cubic","Sine","Asym","Slam","Squeeze","Wall" };
        const juce::StringArray dstQual  { "Off","Standard","High","Ultra" };
        const juce::StringArray srcSuf   { "SRC_A","SRC_B","SRC_C","SRC_D","SRC_SUB","SRC_NOISE" };

        auto F = [&] (const juce::String& id, const juce::String& nm, float def) {
            layout.add (std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { id, 1 }, nm, juce::NormalisableRange<float>(0.0f, 1.0f), def)); };
        auto C = [&] (const juce::String& id, const juce::String& nm, const juce::StringArray& o, int def) {
            layout.add (std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, nm, o, def)); };
        auto B = [&] (const juce::String& id, const juce::String& nm, bool def) {
            layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, nm, def)); };

        for (int n = 2; n <= ParameterIDs::kFxInstances; ++n)
        {
            // ── DELAY instance n — defaults IDENTICAL to instance 1 (a duplicate must sound like the
            //    original the moment you add it; a differently-voiced clone is a bug, not a feature).
            {
                const juce::String p = "SYN_DLY" + juce::String (n) + "_";
                const juce::String d = "Delay " + juce::String (n) + " ";
                C (p + "TYPE", d + "Type", dlyTypes, 0);
                C (p + "CHARACTER", d + "Character", dlyChars, 0);
                C (p + "SYNCDIV",   d + "Sync Division",   syncDiv, 10);
                C (p + "SYNCDIV_R", d + "Sync Division R", syncDiv, 10);
                F (p + "TIME", d + "Time", 0.50f);   F (p + "TIME_R",   d + "Time R",    0.50f);
                F (p + "FEEDBACK", d + "Feedback", 0.10f);
                F (p + "TONE", d + "Tone", 0.44f);   F (p + "MIX",      d + "Mix",       0.34f);
                F (p + "LOWCUT", d + "Low Cut", 0.22f); F (p + "HICUT", d + "Hi Cut",    0.72f);
                F (p + "SPREAD", d + "Spread", 0.60f);  F (p + "WIDTH", d + "Width",     0.78f);
                F (p + "MODRATE", d + "Mod Rate", 0.40f); F (p + "MODDEPTH", d + "Mod Depth", 0.0f);
                F (p + "WOW", d + "Wow", 0.0f);      F (p + "DUCK",     d + "Ducking",   0.0f);
                for (auto& s : srcSuf) B (p + s, d + s, false);
                B (p + "SYNC", d + "Sync", true);    B (p + "LINK",  d + "Link L/R",  true);
                B (p + "PING", d + "Ping-Pong", false);
                B (p + "POWER", d + "Power", false); B (p + "HQ",    d + "HQ",        true);
                B (p + "ACTIVE", d + "In Chain", false);          // not in the chain until the + adds it
                F (p + "RANK",   d + "Chain Rank", 0.5f);
            }
            // ── DISTORTION instance n
            {
                const juce::String p = "SYN_DST" + juce::String (n) + "_";
                const juce::String d = "Distortion " + juce::String (n) + " ";
                C (p + "TYPE", d + "Type", dstTypes, 5);           // Soft Clip, same as instance 1
                C (p + "CHARACTER", d + "Character", dstChars, 0);
                C (p + "QUALITY",   d + "Quality",   dstQual, 1);
                F (p + "DRIVE", d + "Drive", 0.20f); F (p + "SIG",  d + "Knee", 0.65f);
                F (p + "TONE",  d + "Tone",  0.50f); F (p + "MIX",  d + "Mix",  1.00f);
                F (p + "P1", d + "Low Cut",  0.00f); F (p + "P2", d + "Hi Cut",   1.00f);
                F (p + "P3", d + "Emphasis", 0.50f); F (p + "P4", d + "Width",    0.50f);
                F (p + "P5", d + "Bias",     0.50f); F (p + "P6", d + "Gap",      0.00f);
                F (p + "P7", d + "Punch",    0.50f); F (p + "P8", d + "Feedback", 0.00f);
                for (auto& s : srcSuf) B (p + s, d + s, false);
                B (p + "POWER", d + "Power", false); B (p + "AUTO", d + "Auto", false);
                B (p + "PILL2", d + "Wrap",  false);
                B (p + "ACTIVE", d + "In Chain", false);
                F (p + "RANK",   d + "Chain Rank", 0.5f);
            }
        }
    }

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_ARP_DIR, 1 }, "Arp Direction",
        juce::StringArray { "Up", "Down", "Up-Dn", "Random" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_ARP_OCTR, 1 }, "Arp Octaves",
        juce::StringArray { "1", "2", "3", "4" }, 0));   // fb108: industry init = ONE octave (research law)
    layout.add (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParameterIDs::FLOW_ARP_SORTED, 1 }, "Arp Sorted", true));
    addFlowKnob (ParameterIDs::FLOW_ARP_SWING, "Arp Swing", 0.00f);  addFlowKnob (ParameterIDs::FLOW_ARP_MROLL, "Arp Roll", 0.00f);   // fb107 neutral: straight, no surprise rolls
    addFlowKnob (ParameterIDs::FLOW_ARP_TIMBRE,"Arp Timbre",0.60f);  addFlowKnob (ParameterIDs::FLOW_ARP_GLIDE, "Arp Glide",0.15f);
    addFlowKnob (ParameterIDs::FLOW_ARP_P_RANGE,"Arp Pitch Range",0.50f); addFlowKnob (ParameterIDs::FLOW_ARP_P_CURVE,"Arp Pitch Curve",0.50f);
    addFlowKnob (ParameterIDs::FLOW_ARP_P_QUANT,"Arp Pitch Quant",0.60f); addFlowKnob (ParameterIDs::FLOW_ARP_P_SLIDE,"Arp Pitch Slide",0.00f);
    addFlowKnob (ParameterIDs::FLOW_ARP_G_LEN,  "Arp Gate Length",0.52f); addFlowKnob (ParameterIDs::FLOW_ARP_G_CURVE,"Arp Gate Curve",0.50f);   // fb107: ×1.0 neutral
    addFlowKnob (ParameterIDs::FLOW_ARP_G_RAND, "Arp Gate Random",0.00f); addFlowKnob (ParameterIDs::FLOW_ARP_G_SLIDE,"Arp Gate Slide",0.00f);
    addFlowKnob (ParameterIDs::FLOW_ARP_V_RANGE,"Arp Vel Range",0.70f);   addFlowKnob (ParameterIDs::FLOW_ARP_V_CURVE,"Arp Vel Curve",0.50f);
    addFlowKnob (ParameterIDs::FLOW_ARP_V_RAND, "Arp Vel Random",0.00f);  addFlowKnob (ParameterIDs::FLOW_ARP_V_FLOOR,"Arp Vel Floor",0.20f);
    addFlowKnob (ParameterIDs::FLOW_ARP_O_RANGE,"Arp Oct Range",0.25f);   addFlowKnob (ParameterIDs::FLOW_ARP_O_BIAS, "Arp Oct Bias",0.50f);
    addFlowKnob (ParameterIDs::FLOW_ARP_O_RAND, "Arp Oct Random",0.00f);  addFlowKnob (ParameterIDs::FLOW_ARP_O_SPREAD,"Arp Oct Spread",0.00f);
    addFlowKnob (ParameterIDs::FLOW_ARP_R_COUNT,"Arp Roll Count",0.33f);  addFlowKnob (ParameterIDs::FLOW_ARP_R_DECAY,"Arp Roll Decay",0.40f);
    addFlowKnob (ParameterIDs::FLOW_ARP_R_CURVE,"Arp Roll Curve",0.50f);  addFlowKnob (ParameterIDs::FLOW_ARP_R_AMT,  "Arp Roll Amount",0.50f);
    addFlowKnob (ParameterIDs::FLOW_ARP_C_AMT,  "Arp Chance Amount",0.80f); addFlowKnob (ParameterIDs::FLOW_ARP_C_BIAS,"Arp Chance Bias",0.50f);
    addFlowKnob (ParameterIDs::FLOW_ARP_C_SEED, "Arp Chance Seed",0.44f);   addFlowKnob (ParameterIDs::FLOW_ARP_C_DRIFT,"Arp Chance Drift",0.00f);
    addFlowKnob (ParameterIDs::FLOW_ARP_W_DEPTH,"Arp Wave Depth",0.60f);    addFlowKnob (ParameterIDs::FLOW_ARP_W_CURVE,"Arp Wave Curve",0.45f);
    addFlowKnob (ParameterIDs::FLOW_ARP_W_SLIDE,"Arp Wave Slide",0.25f);    addFlowKnob (ParameterIDs::FLOW_ARP_W_RAND, "Arp Wave Random",0.00f);
    // ── CHOP extension card (fb106): Ribbon scalars + 24 lane depth knobs.
    // Same law as fb105: setSynParam-only, choices read as INDEX.
    layout.add (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_CATCH, 1 }, "Chop Catch", false));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_SLICES, 1 }, "Chop Slices",
        juce::StringArray { "2", "3", "4", "6", "8", "12", "16" }, 4));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_LOOP, 1 }, "Chop Loop",
        juce::StringArray { "2", "4", "6", "8", "10", "12", "16" }, 3));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_MODE, 1 }, "Chop Order Mode",
        juce::StringArray { "Step", "Ping", "Rand", "Walk" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_RPTS, 1 }, "Chop Repeats",
        juce::StringArray { "1", "2", "3", "4" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_FILTER, 1 }, "Chop Filter",
        juce::StringArray { "Off", "Low", "Mid", "High" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_FREEZE, 1 }, "Chop Freeze", false));
    layout.add (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParameterIDs::FLOW_CHOP_COLLECT, 1 }, "Chop Collect", false));
    addFlowKnob (ParameterIDs::FLOW_CHOP_SCAN,  "Chop Scan",  1.00f);  addFlowKnob (ParameterIDs::FLOW_CHOP_WANDER,"Chop Wander",0.00f);   // fb107: Scan = Now (live chop, not a fixed delay)
    addFlowKnob (ParameterIDs::FLOW_CHOP_SPREAD,"Chop Spread",0.00f);  addFlowKnob (ParameterIDs::FLOW_CHOP_SPEED, "Chop Speed", 0.00f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_STEPS, "Chop Steps", 0.00f);  addFlowKnob (ParameterIDs::FLOW_CHOP_DETUNE,"Chop Detune",0.00f);   // fb107 neutral bus
    addFlowKnob (ParameterIDs::FLOW_CHOP_WOW,   "Chop Wow",   0.00f);  addFlowKnob (ParameterIDs::FLOW_CHOP_SMOOTH,"Chop Smooth",0.15f);   // smooth .15 = legacy fade ×1.0
    addFlowKnob (ParameterIDs::FLOW_CHOP_GRIT,  "Chop Grit",  0.00f);  addFlowKnob (ParameterIDs::FLOW_CHOP_TRIM,  "Chop Trim",  0.50f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_O_SPREAD,"Chop Order Spread",0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_O_BIAS,"Chop Order Bias",0.50f);   // fb110: scatter opt-in
    addFlowKnob (ParameterIDs::FLOW_CHOP_O_LOCK,  "Chop Order Lock",  0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_O_SEED,"Chop Order Seed",0.44f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_P_RANGE, "Chop Pitch Range", 0.33f); addFlowKnob (ParameterIDs::FLOW_CHOP_P_STEPS,"Chop Pitch Steps",0.00f);   // fb111: semitone interval + every-Nth pattern
    addFlowKnob (ParameterIDs::FLOW_CHOP_P_GLIDE, "Chop Pitch Glide", 0.15f); addFlowKnob (ParameterIDs::FLOW_CHOP_P_QUANT,"Chop Pitch Fall",0.00f);   // fb111: deterministic rise/fall, no dice
    addFlowKnob (ParameterIDs::FLOW_CHOP_RV_ODDS, "Chop Rev Odds",    0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_RV_RUN, "Chop Rev Run",  0.25f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_RV_SPREAD,"Chop Rev Spread", 0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_RV_SNAP,"Chop Rev Snap", 0.60f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_T_LEN,   "Chop Trim Length", 0.875f); addFlowKnob (ParameterIDs::FLOW_CHOP_T_CURVE,"Chop Trim Curve",0.30f);   // ×1.0 neutral
    addFlowKnob (ParameterIDs::FLOW_CHOP_T_RAND,  "Chop Trim Random", 0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_T_GATE, "Chop Trim Gate", 0.00f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_R_COUNT, "Chop Repeat Count",0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_R_DECAY,"Chop Repeat Decay",0.40f);   // fb113: Count=1 neutral, the doer
    addFlowKnob (ParameterIDs::FLOW_CHOP_R_CURVE, "Chop Repeat Curve",0.50f); addFlowKnob (ParameterIDs::FLOW_CHOP_R_ODDS, "Chop Repeat Odds",1.00f);   // fb113: Odds carves DOWN from All — Count is instantly audible
    addFlowKnob (ParameterIDs::FLOW_CHOP_D_AMT,   "Chop Drop Amount", 0.00f); addFlowKnob (ParameterIDs::FLOW_CHOP_D_SIZE, "Chop Drop Size", 0.40f);
    addFlowKnob (ParameterIDs::FLOW_CHOP_D_SPRAY, "Chop Drop Spray",  0.10f); addFlowKnob (ParameterIDs::FLOW_CHOP_D_TONE, "Chop Drop Tone", 0.50f);
    addFlowKnob (ParameterIDs::FLOW_GLI_RATE,"Glitch Rate",0.6111f);  addFlowKnob (ParameterIDs::FLOW_GLI_GATE,"Glitch Gate",0.55f);   // fb115: grid default = 1/16 (TIME IS TRUTHFUL)
    addFlowKnob (ParameterIDs::FLOW_GLI_VARY,"Glitch Vary",0.50f);  addFlowKnob (ParameterIDs::FLOW_GLI_TRAJ,"Glitch Traj",0.00f);  // VARY = fire CHANCE; 0 = never fires (silent), 0.5 = glitches out of the box
    addFlowKnob (ParameterIDs::FLOW_GLI_MORPH,"Glitch Morph",0.00f);
    // ── fb115 GLITCH Monitor card: 8 tile switches + masters + 32 per-effect knobs ──
    {
        auto addGliBool = [&] (const char* id, const char* name, bool def) {
            layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, name, def)); };
        addGliBool (ParameterIDs::FLOW_GLI_EN_REP, "Glitch Repeat On",  true);   // neutral law: REP only
        addGliBool (ParameterIDs::FLOW_GLI_EN_REV, "Glitch Reverse On", false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_TAPE,"Glitch Tape On",   false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_GATE,"Glitch Gate FX On",false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_PIT, "Glitch Pitch On",  false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_CRSH,"Glitch Crush On",  false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_FRZ, "Glitch Freeze On", false);
        addGliBool (ParameterIDs::FLOW_GLI_EN_SCT, "Glitch Scatter On",false);
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_HOLD, 1 }, "Glitch Hold",
            juce::StringArray { "1", "2", "3", "4", "6", "8" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_LOOP, 1 }, "Glitch Loop",
            juce::StringArray { "2", "4", "8", "12", "16" }, 2));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_QUANT, 1 }, "Glitch Quant",
            juce::StringArray { "1/4", "1/8", "1/16", "1/32" }, 2));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_RELEASE, 1 }, "Glitch Release",
            juce::StringArray { "End", "Now" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_FILTER, 1 }, "Glitch Filter",
            juce::StringArray { "Off", "Low", "Mid", "High" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_PAN, 1 }, "Glitch Pan",
            juce::StringArray { "L", "C", "R" }, 1));
        layout.add (std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { ParameterIDs::FLOW_GLI_SYNC, 1 }, "Glitch Clock",
            juce::StringArray { "Free", "Sync" }, 1));
    }
    addFlowKnob (ParameterIDs::FLOW_GLI_DEJAVU,"Glitch Deja Vu",0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_DECAY,"Glitch Decay",0.00f);
    // fb142 — the Fire pane's two new fronts: OUT MODE (Beat Repeat's Mix/Insert/Gate law)
    // + PING (each fire bounces across the stereo field). Defaults = bit-exact old behavior.
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::FLOW_GLI_OUTMODE, 1 }, "Glitch Out Mode",
        juce::StringArray { "Mix", "Cut", "Gate" }, 0));
    addFlowKnob (ParameterIDs::FLOW_GLI_PING,"Glitch Ping",0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_DROP,"Glitch Drop",0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_BURST,"Glitch Burst",0.00f);   // fb143
    addFlowKnob (ParameterIDs::FLOW_GLI_BEND,  "Glitch Bend",  0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_SEED, "Glitch Seed", 0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_REP_SIZE,  "Glitch Repeat Size", 1.00f); addFlowKnob (ParameterIDs::FLOW_GLI_REP_SPEED, "Glitch Repeat Speed",0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_REP_FADE,  "Glitch Repeat Fade", 0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_REP_VARY,  "Glitch Repeat Vary", 0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_REV_LEN,   "Glitch Rev Length",  1.00f); addFlowKnob (ParameterIDs::FLOW_GLI_REV_FADE,  "Glitch Rev Fade",    0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_REV_SPRD,  "Glitch Rev Spread",  0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_REV_SNAP,  "Glitch Rev Snap",    1.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_TAPE_CURVE,"Glitch Tape Curve",  0.70f); addFlowKnob (ParameterIDs::FLOW_GLI_TAPE_TIME, "Glitch Tape Time",   0.50f);
    addFlowKnob (ParameterIDs::FLOW_GLI_TAPE_DEPTH,"Glitch Tape Depth",  1.00f); addFlowKnob (ParameterIDs::FLOW_GLI_TAPE_SPIN, "Glitch Tape Spin",   0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_GATE_RATE, "Glitch Gate Rate",   0.14f); addFlowKnob (ParameterIDs::FLOW_GLI_GATE_SHAPE,"Glitch Gate Shape",  0.50f);
    addFlowKnob (ParameterIDs::FLOW_GLI_GATE_NUDGE,"Glitch Gate Nudge",  0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_GATE_AMT,  "Glitch Gate Amount", 1.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_PIT_SHIFT, "Glitch Pitch Shift", 0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_PIT_WALK,  "Glitch Pitch Walk",  0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_PIT_GLIDE, "Glitch Pitch Glide", 0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_PIT_JUMP,  "Glitch Pitch Jump",  0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_CRSH_BITS, "Glitch Crush Bits",  0.67f); addFlowKnob (ParameterIDs::FLOW_GLI_CRSH_RATE, "Glitch Crush Rate",  0.31f);
    addFlowKnob (ParameterIDs::FLOW_GLI_CRSH_TONE, "Glitch Crush Tone",  1.00f); addFlowKnob (ParameterIDs::FLOW_GLI_CRSH_AMT,  "Glitch Crush Amount",1.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_FRZ_SIZE,  "Glitch Freeze Size", 0.19f); addFlowKnob (ParameterIDs::FLOW_GLI_FRZ_SPRAY, "Glitch Freeze Spray",0.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_FRZ_SHINE, "Glitch Freeze Shine",0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_FRZ_MELT,  "Glitch Freeze Melt", 0.30f);
    addFlowKnob (ParameterIDs::FLOW_GLI_SCT_SIZE,  "Glitch Scatter Size",0.26f); addFlowKnob (ParameterIDs::FLOW_GLI_SCT_AMT,   "Glitch Scatter Amount",1.00f);
    addFlowKnob (ParameterIDs::FLOW_GLI_SCT_VARY,  "Glitch Scatter Vary",0.00f); addFlowKnob (ParameterIDs::FLOW_GLI_SCT_WIDTH, "Glitch Scatter Width",0.00f);
    // ── fb122 ROBIN Wheel card: station bank + behavior + rotation + feel ──
    {
        auto addRbnBool = [&] (const char* id, const char* name, bool def) {
            layout.add (std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, name, def)); };
        addRbnBool (ParameterIDs::FLOW_RBN_A, "Robin Station A", true);
        addRbnBool (ParameterIDs::FLOW_RBN_B, "Robin Station B", true);
        addRbnBool (ParameterIDs::FLOW_RBN_C, "Robin Station C", true);
        addRbnBool (ParameterIDs::FLOW_RBN_D, "Robin Station D", true);
        addRbnBool (ParameterIDs::FLOW_RBN_AFIRST, "Robin A First", true);
        addRbnBool (ParameterIDs::FLOW_RBN_RETRIG, "Robin Retrig", false);
        auto addRbnChoice = [&] (const char* id, const char* name, juce::StringArray xs, int def) {
            layout.add (std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, name, xs, def)); };
        addRbnChoice (ParameterIDs::FLOW_RBN_MODE,   "Robin Mode",    { "Cycle", "Shuffle", "Random", "Pong" }, 0);
        addRbnChoice (ParameterIDs::FLOW_RBN_LEGATO, "Robin Legato",  { "Keep", "New" }, 0);
        addRbnChoice (ParameterIDs::FLOW_RBN_STEAL,  "Robin Steal",   { "Follow", "Stay" }, 0);
        addRbnChoice (ParameterIDs::FLOW_RBN_RELEASE,"Robin Release", { "Hold", "Free" }, 1);
        addRbnChoice (ParameterIDs::FLOW_RBN_TIMES,  "Robin Times",   { "1", "2", "3", "4" }, 0);
        addRbnChoice (ParameterIDs::FLOW_RBN_RESET,  "Robin Reset",   { "Free", "Bar", "Phrase" }, 0);
        addRbnChoice (ParameterIDs::FLOW_RBN_RUN,    "Robin Run",     { "Forward", "Backward" }, 0);
        // fb125 — glitch per-effect Out routing (Filter Off/Low/Mid/High + Pan L/C/R)
        {
            static const char* fx[8]  = { "REP", "REV", "TAPE", "GATE", "PIT", "CRSH", "FRZ", "SCT" };
            static const char* nm[8]  = { "Repeat", "Rev", "Tape", "Gate FX", "Pitch", "Crush", "Freeze", "Scatter" };
            for (int i = 0; i < 8; ++i)
            {
                layout.add (std::make_unique<juce::AudioParameterChoice>(
                    juce::ParameterID { juce::String ("FLOW_GLI_") + fx[i] + "_FLT", 1 },
                    juce::String ("Glitch ") + nm[i] + " Filter",
                    juce::StringArray { "Off", "Low", "Mid", "High" }, 0));
                layout.add (std::make_unique<juce::AudioParameterChoice>(
                    juce::ParameterID { juce::String ("FLOW_GLI_") + fx[i] + "_PAN", 1 },
                    juce::String ("Glitch ") + nm[i] + " Pan",
                    juce::StringArray { "L", "C", "R" }, 1));
                layout.add (std::make_unique<juce::AudioParameterChoice>(
                    juce::ParameterID { juce::String ("FLOW_GLI_") + fx[i] + "_TRG", 1 },
                    juce::String ("Glitch ") + nm[i] + " Trig",
                    juce::StringArray { "Sync", "Free", "Roll" }, 0));
                layout.add (std::make_unique<juce::AudioParameterChoice>(
                    juce::ParameterID { juce::String ("FLOW_GLI_") + fx[i] + "_GRID", 1 },
                    juce::String ("Glitch ") + nm[i] + " Grid",
                    juce::StringArray { "Main", "1/1", "1/2.", "1/2", "1/4.", "1/2T", "1/4", "1/8.", "1/4T",
                                        "1/8", "1/16.", "1/8T", "1/16", "1/32.", "1/16T", "1/32", "1/32T",
                                        "1/64", "1/128", "1/256" }, 0));
            }
        }
        addRbnChoice (ParameterIDs::FLOW_RBN_O1, "Robin Order 1st", { "A", "B", "C", "D" }, 0);
        addRbnChoice (ParameterIDs::FLOW_RBN_O2, "Robin Order 2nd", { "A", "B", "C", "D" }, 1);
        addRbnChoice (ParameterIDs::FLOW_RBN_O3, "Robin Order 3rd", { "A", "B", "C", "D" }, 2);
        addRbnChoice (ParameterIDs::FLOW_RBN_O4, "Robin Order 4th", { "A", "B", "C", "D" }, 3);
    }
    addFlowKnob (ParameterIDs::FLOW_RBN_VARY,  "Robin Vary",  0.00f); addFlowKnob (ParameterIDs::FLOW_RBN_DRIFT,  "Robin Drift",  0.00f);
    addFlowKnob (ParameterIDs::FLOW_RBN_WOBBLE,"Robin Wobble",0.00f); addFlowKnob (ParameterIDs::FLOW_RBN_LVL,    "Robin Level",  0.00f);
    addFlowKnob (ParameterIDs::FLOW_RBN_PAN,   "Robin Pan",   0.00f); addFlowKnob (ParameterIDs::FLOW_RBN_AFTER,  "Robin After",  0.40f);
    addFlowKnob (ParameterIDs::FLOW_RBN_GLIDE, "Robin Glide", 0.00f); addFlowKnob (ParameterIDs::FLOW_RBN_OVERLAP,"Robin Overlap",0.00f);
    addFlowKnob (ParameterIDs::FLOW_RBN_FADE,  "Robin Fade",  0.00f);
    addFlowKnob (ParameterIDs::FLOW_DRF_RATE,"Drift Rate",0.40f);  addFlowKnob (ParameterIDs::FLOW_DRF_GATE,"Drift Gate",0.55f);
    addFlowKnob (ParameterIDs::FLOW_DRF_VARY,"Drift Vary",0.50f);  addFlowKnob (ParameterIDs::FLOW_DRF_TRAJ,"Drift Traj",0.00f);
    addFlowKnob (ParameterIDs::FLOW_DRF_MORPH,"Drift Depth",1.00f);  // DEPTH = output amplitude; 0 = inert (no modulation), 0.5 = breathes out of the box

    // ── ANNULUS resonator (global key-tracked physical-modeling node) ──
    addFlowKnob (ParameterIDs::SYN_RESO_STRUCTURE,  "Reso Structure",  0.30f);
    addFlowKnob (ParameterIDs::SYN_RESO_BRIGHTNESS, "Reso Brightness", 0.55f);
    addFlowKnob (ParameterIDs::SYN_RESO_DAMPING,    "Reso Damping",    0.45f);
    addFlowKnob (ParameterIDs::SYN_RESO_POSITION,   "Reso Position",   0.20f);
    addFlowKnob (ParameterIDs::SYN_RESO_MIX,        "Reso Mix",        0.00f);  // default 0 = bypassed (load → turn up)
    addFlowKnob (ParameterIDs::SYN_RESO_KEYTRACK,   "Reso Key Track",  1.00f);  // default fully pitched (matches played note)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_RESO_MATERIAL, 1 }, "Reso Material",
        juce::StringArray { "String", "Pluck", "Piano", "Bar", "Metal", "Drum" }, 0));


    return layout;
}

//==============================================================================
// ════════ fb346 — THE DYNAMIC CHAIN ORDER ════════
// Builds the ordered list of devices that are IN the chain, sorted by their float _RANK.
// ⚠️ MESSAGE THREAD ONLY — it builds juce::Strings and may grow the vector. processBlock reads
// chainOrder_ but never rebuilds it; the UI calls this after any add / delete / drag-reorder.
// Rank is a float precisely so this list has no fixed cardinality: the old 6-way SYN_FX_ORDER
// choice param could never describe a 4th device (choice cardinality is frozen at birth, fb342).
void TerrainInstrumentAudioProcessor::rebuildChainOrder()
{
    chainOrder_.clear();
    auto add = [&] (int kind, int inst, const juce::String& activeId, const juce::String& rankId)
    {
        auto* a = apvts.getRawParameterValue (activeId);
        if (a == nullptr || a->load() <= 0.5f) return;               // not in the chain — skipped entirely
        auto* r = apvts.getRawParameterValue (rankId);
        chainOrder_.push_back ({ kind, inst, r != nullptr ? r->load() : 0.5f });
    };
    add (0, 1, ParameterIDs::SYN_RVB_ACTIVE, ParameterIDs::SYN_RVB_RANK);
    add (1, 1, ParameterIDs::SYN_DLY_ACTIVE, ParameterIDs::SYN_DLY_RANK);
    add (2, 1, ParameterIDs::SYN_DST_ACTIVE, ParameterIDs::SYN_DST_RANK);
    for (int n = 2; n <= ParameterIDs::kFxInstances; ++n)
    {
        const juce::String dp = "SYN_DLY" + juce::String (n) + "_";
        const juce::String sp = "SYN_DST" + juce::String (n) + "_";
        add (1, n, dp + "ACTIVE", dp + "RANK");
        add (2, n, sp + "ACTIVE", sp + "RANK");
    }
    // stable_sort so two devices sharing a rank keep a deterministic order (no audible reshuffle).
    std::stable_sort (chainOrder_.begin(), chainOrder_.end(),
                      [] (const ChainEntry& a, const ChainEntry& b) { return a.rank < b.rank; });
}

void TerrainInstrumentAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    for (auto& e : monoLegEnv_) e.prepare (sampleRate);   // fb178 — mono env tap
    for (auto& e : monoDynEnv_) e.prepare (sampleRate);
    monoHeld_ = 0;

    // fb264 — master peak-limiter coefficients (stereo-linked; applied at the master output,
    // see the fb264 note at the top of this file). One-pole time constants: 0.8 ms attack so a
    // loud chord onset is caught before it squares, 120 ms release so recovery is smooth/click-free.
    limAtkCoef_ = std::exp (-1.0f / (float) (sampleRate * 0.0008));   // 0.8 ms
    limRelCoef_ = std::exp (-1.0f / (float) (sampleRate * 0.120));    // 120 ms
    limEnv_  = 0.0f;
    limGain_ = 1.0f;
    // CPU: prepare resets voice state — force the change-gated broadcasts (ModConfig +
    // engine params) to re-push on the first block after any prepare.
    synCfgPushed_    = false;
    engParamsPushed_ = false;
    // Grain-budget drift insurance: every engine resetPool()s during the synth prep below
    // (each releasing its live grains), so the shared counter lands at 0 anyway — this zero
    // just guarantees a missed path can never leak the budget permanently.
    granGrainsLive_  = 0;
    geodePartialsLive_ = 0;   // GEODE — reset the shared partial budget (self-heals a leaked count)
    flowRobin_.reset();       // fb122 — the Wheel restarts from the first station

    // Task 5: singleton synth removed. Prep all layer synths instead.
    // Mark 2 task 4: prep per-layer scratch buffers and per-layer synth rates.
    // Each layer synth renders into its own scratch buffer in processBlock,
    // then the results are summed (with mixer math) into the master `buffer`.
    for (auto& buf : layerScratch)
        buf.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);

    for (auto& layer : layers)
        layer.synth.setCurrentPlaybackSampleRate (sampleRate);

    // Synth scratch buffer + per-voice DSP prep.
    synthScratch.setSize (2, samplesPerBlock, false, true, true);
    synthEngine.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synthEngine.getNumVoices(); ++i)
    {
        if (auto* sv = synthVoices_[(size_t) i])   // typed array — no per-voice RTTI
        {
            sv->prepareToPlay (sampleRate, samplesPerBlock, 2);
            sv->setLfoCustomTables (lfoTableAudio_);   // LFO ARC L1 — wire drawn-shape tables
        }
    }

    grainEngineL.prepare(sampleRate, samplesPerBlock);
    grainEngineR.prepare(sampleRate, samplesPerBlock);
    tapeProcessorL.prepare(sampleRate, samplesPerBlock);
    tapeProcessorR.prepare(sampleRate, samplesPerBlock);
    tapeLoop.prepare(sampleRate, samplesPerBlock);
    spaceReverb.prepare(sampleRate, samplesPerBlock);
    hallReverb.prepare (sampleRate);   // fb276 — synth FX-rack Hall reverb
    roomReverb.prepare (sampleRate);   // fb281 — synth FX-rack Room reverb
    plateReverb.prepare (sampleRate);  // fb282 — synth FX-rack Plate reverb
    springReverb.prepare (sampleRate); // fb284 — synth FX-rack Spring reverb
    digitalReverb.prepare (sampleRate);// fb285 — synth FX-rack Digital reverb (Lexicon 224)
    vintageReverb.prepare (sampleRate);// fb288 — synth FX-rack Vintage reverb (80s digital rack)
    basinReverb.prepare (sampleRate);  // fb289 — synth FX-rack Basin reverb (huge dark wash)
    shimmerReverb.prepare (sampleRate);// fb290 — synth FX-rack Shimmer reverb (octave wash)
    convolutionReverb.prepare (sampleRate);// fb291 — synth FX-rack Convolution reverb (FFT convolution)
    delayEngine.prepare (sampleRate);      // fb296 — synth FX-rack Delay (Digital/Tape/BBD/Diffuse)
    distortionEngine.prepare (sampleRate); // fb315 — synth FX-rack Distortion (23 modes / 6 families)
    // fb346 — prepare the INSTANCE POOL. Every extra instance is prepared up front because the audio
    // thread may never allocate; an instance the user has not added simply never runs (the chain loop
    // skips it), which is the "an empty slot costs exactly zero" law.
    for (auto& d : delayPool_) d.prepare (sampleRate);
    for (auto& d : distPool_)  d.prepare (sampleRate);
    poolDlyType_.fill (-1);
    poolDlyEnv_.fill (0.0f);
    poolDstEnv_.fill (0.0f);
    chainOrder_.reserve ((size_t) ParameterIDs::kFxInstances * 3 + 4);   // reserve once — never allocates in processBlock
    rebuildChainOrder();
    activeDlyType_ = -1; dlySwapping_ = false;
    activeRvbType_ = -1; rvbSwapping_ = false;
    hallSm_ = 1.0f - std::exp (-1.0f / (0.015f * (float) sampleRate));   // fb277 — ~15 ms mix/env smoothing (no clicks)
    // fb287 — DUCK follower: fast attack (grab transients ~5 ms), slow release (bloom back ~280 ms) → the
    // classic ducking-reverb swell in the gaps between notes. One-pole per-sample; click-free by construction.
    duckAtkCoef_ = std::exp (-1.0f / (float) (sampleRate * 0.005));
    duckRelCoef_ = std::exp (-1.0f / (float) (sampleRate * 0.220));
    duckEnv_ = 0.0f;
    reverbSendBuf_.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);   // fb280 — per-osc no-bleed send bus
    reverbSendBuf_.clear();
    delaySendBuf_.clear();
    distortionSendBuf_.clear();   // fb338
    hallBloomEnv_ = 0.0f; hallBloomViz_.store (0.0f, std::memory_order_relaxed);
    moogDelay.prepare(sampleRate, samplesPerBlock);
    terrainChorus.prepare (sampleRate, samplesPerBlock);

    // Per-chop FX independence capture bus — stereo, sized to the host block.
    // Voices write here when their chop has fxIndependent=true; the bus is
    // added directly to master output, bypassing the entire global FX chain.
    indyCaptureBus.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);

    // Per-chop FX-independence (option 1): prepare the shared indy chain
    // and allocate its output sum buffer. The chain processes
    // indyCaptureBus into indySumBuffer once per block; the per-sample
    // master loop reads indySumBuffer and mixes it in.
    indyChain.prepare (sampleRate, samplesPerBlock);
    indySumBuffer.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    indySumBuffer.clear();

    // Mix page Phase D: allocate per-layer rolling stem buffers (~92 MB at 48k).
    allocateStemBuffers (sampleRate);
    indyCaptureBus.clear();

    smoothedChorusAmount.reset    (sampleRate, 0.02);
    smoothedChorusWidth.reset     (sampleRate, 0.02);
    smoothedChorusCharacter.reset (sampleRate, 0.05);  // CHARACTER changes more dramatically
    smoothedChorusAmount.setCurrentAndTargetValue    (rawParam (ParameterIDs::CHORUS_AMOUNT)->load());
    smoothedChorusWidth.setCurrentAndTargetValue     (rawParam (ParameterIDs::CHORUS_WIDTH)->load());
    smoothedChorusCharacter.setCurrentAndTargetValue (rawParam (ParameterIDs::CHORUS_CHARACTER)->load());

    eqL.prepare(sampleRate, samplesPerBlock);
    eqR.prepare(sampleRate, samplesPerBlock);
    analyzerPre.prepare (sampleRate);
    analyzerPost.prepare (sampleRate);
    // Initialize EQ smoothers to APVTS defaults so the first audio block sees
    // valid values (not 0, which would make ParametricEQ's internal Q smoother
    // briefly cross zero and produce NaN coefficients).
    {
        const char* freqIds [7] = { ParameterIDs::EQ_B1_FREQ, ParameterIDs::EQ_B2_FREQ, ParameterIDs::EQ_B3_FREQ, ParameterIDs::EQ_B4_FREQ, ParameterIDs::EQ_B5_FREQ, ParameterIDs::EQ_B6_FREQ, ParameterIDs::EQ_B7_FREQ };
        const char* gainIds [7] = { ParameterIDs::EQ_B1_GAIN, ParameterIDs::EQ_B2_GAIN, ParameterIDs::EQ_B3_GAIN, ParameterIDs::EQ_B4_GAIN, ParameterIDs::EQ_B5_GAIN, ParameterIDs::EQ_B6_GAIN, ParameterIDs::EQ_B7_GAIN };
        const char* qIds    [7] = { ParameterIDs::EQ_B1_Q,    ParameterIDs::EQ_B2_Q,    ParameterIDs::EQ_B3_Q,    ParameterIDs::EQ_B4_Q,    ParameterIDs::EQ_B5_Q,    ParameterIDs::EQ_B6_Q,    ParameterIDs::EQ_B7_Q };
        for (int b = 0; b < 7; ++b)
        {
            smoothedEqBandFreq[b].reset (sampleRate, 0.005);
            smoothedEqBandGain[b].reset (sampleRate, 0.005);
            smoothedEqBandQ[b]   .reset (sampleRate, 0.005);
            smoothedEqBandFreq[b].setCurrentAndTargetValue (rawParam (freqIds[b])->load());
            smoothedEqBandGain[b].setCurrentAndTargetValue (rawParam (gainIds[b])->load());
            smoothedEqBandQ[b]   .setCurrentAndTargetValue (rawParam (qIds[b])->load());
        }
        smoothedEqHpFreq.reset (sampleRate, 0.005);
        smoothedEqLpFreq.reset (sampleRate, 0.005);
        smoothedEqHpFreq.setCurrentAndTargetValue (rawParam (ParameterIDs::EQ_HP_FREQ)->load());
        smoothedEqLpFreq.setCurrentAndTargetValue (rawParam (ParameterIDs::EQ_LP_FREQ)->load());
    }
    captureBuffer.prepare(sampleRate, samplesPerBlock);

    // Prepare modulation engine
    modulationEngine.prepare(sampleRate);

    // FLOW · ARP — prepare the block-rate global LFO bank + reset the engine
    for (auto& l : flowLfo_) l.prepare (sampleRate);
    for (int fli = 0; fli < wc::NUM_LFOS; ++fli) flowLfo_[fli].setCustomTable (lfoTableAudio_[fli]);   // LFO ARC L1 — wire drawn-shape tables
    chop.prepare   (sampleRate, 8.0);   // FLOW · CHOP capture ring — fb106: 8 s so the Ribbon's 16-cell memory holds at slow rates
    glitch.prepare (sampleRate, 4.0);   // FLOW · GLITCH capture ring (4 s)
    prevGlitchOn_ = false;               // FLOW · re-anchor the glitch enable-edge on (re)prepare
    drift.prepare  (sampleRate);        // FLOW · DRIFT generator (no audio buffer)
    flowRobin_.prepare (sampleRate);    // fb122 ROBIN rotation brain
    {   // fb125 — glitch per-effect Out routing: resolve the 16 raw pointers ONCE (RT-safe reads)
        static const char* fxIds[8] = { "REP", "REV", "TAPE", "GATE", "PIT", "CRSH", "FRZ", "SCT" };
        for (int fi = 0; fi < 8; ++fi)
        {
            gliFxFltP_[fi] = apvts.getRawParameterValue (juce::String ("FLOW_GLI_") + fxIds[fi] + "_FLT");
            gliFxPanP_[fi] = apvts.getRawParameterValue (juce::String ("FLOW_GLI_") + fxIds[fi] + "_PAN");
            gliFxTrgP_[fi] = apvts.getRawParameterValue (juce::String ("FLOW_GLI_") + fxIds[fi] + "_TRG");
            gliFxGrdP_[fi] = apvts.getRawParameterValue (juce::String ("FLOW_GLI_") + fxIds[fi] + "_GRID");
        }
    }
    synthEngine.setRobinBrain (&flowRobin_);
    reso.prepare   (sampleRate);        // ANNULUS resonator — allocates mode/delay state here only
    for (auto& e : resoVizEnergy_) e.store (0.0f, std::memory_order_relaxed);
    resoVizOut_.store (0.0f, std::memory_order_relaxed);
    flowArp.reset();
    if (modStateJson.isNotEmpty())
        modulationEngine.updateConfig(ModulationEngine::parseJSON(modStateJson));

    // 20ms ramp for all smoothed parameters
    smoothedGrainSize.reset(sampleRate, 0.02);
    smoothedDensity.reset(sampleRate, 0.02);
    smoothedSpray.reset(sampleRate, 0.02);
    smoothedPitch.reset(sampleRate, 0.02);
    smoothedWander.reset(sampleRate, 0.02);
    smoothedFreeze.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);
    smoothedGrainFilter.reset(sampleRate, 0.02);
    smoothedWowFlutter.reset(sampleRate, 0.02);
    smoothedSaturation.reset(sampleRate, 0.02);
    smoothedHiss.reset(sampleRate, 0.02);
    smoothedStudioSculpt.reset (sampleRate, 0.02);
    smoothedStudioWeave .reset (sampleRate, 0.02);
    smoothedStudioTilt  .reset (sampleRate, 0.02);
    smoothedWireWow .reset (sampleRate, 0.02);
    smoothedWireSat .reset (sampleRate, 0.02);
    smoothedWireHiss.reset (sampleRate, 0.02);
    smoothedOutputGain.reset(sampleRate, 0.02);
    smoothedMasterMix.reset(sampleRate, 0.02);
    smoothedLoopFeedback.reset(sampleRate, 0.02);
    smoothedLoopDegrade.reset(sampleRate, 0.02);

    // Space reverb smoothing
    smoothedSpaceSize.reset(sampleRate, 0.02);
    smoothedSpaceDecay.reset(sampleRate, 0.02);
    smoothedSpaceTone.reset(sampleRate, 0.02);
    smoothedSpaceMix.reset(sampleRate, 0.02);

    // Delay smoothing (20 ms ramp like the rest)
    smoothedDlyTime.reset(sampleRate, 0.02);
    smoothedDlyFeedback.reset(sampleRate, 0.02);
    smoothedDlyTone.reset(sampleRate, 0.02);
    smoothedDlyCharacter.reset(sampleRate, 0.02);
    smoothedDlyMod.reset(sampleRate, 0.02);
    smoothedDlyModRate.reset(sampleRate, 0.02);
    smoothedDlyMix.reset(sampleRate, 0.02);
    smoothedDlyDuck.reset(sampleRate, 0.02);
    smoothedDelayFreeze.reset(sampleRate, 0.005); // 5ms — fast since freeze is binary-ish

    // Set initial values
    smoothedGrainSize.setCurrentAndTargetValue(rawParam (ParameterIDs::GRAIN_SIZE)->load());
    smoothedDensity.setCurrentAndTargetValue(rawParam (ParameterIDs::DENSITY)->load());
    smoothedSpray.setCurrentAndTargetValue(rawParam (ParameterIDs::SPRAY)->load());
    smoothedPitch.setCurrentAndTargetValue(rawParam (ParameterIDs::PITCH)->load());
    smoothedWander.setCurrentAndTargetValue(rawParam (ParameterIDs::WANDER)->load());
    smoothedFreeze.setCurrentAndTargetValue(rawParam (ParameterIDs::FREEZE)->load());
    smoothedMix.setCurrentAndTargetValue(rawParam (ParameterIDs::MIX)->load());
    smoothedGrainFilter.setCurrentAndTargetValue(rawParam (ParameterIDs::GRAIN_FILTER)->load());
    smoothedWowFlutter.setCurrentAndTargetValue(rawParam (ParameterIDs::WOW_FLUTTER)->load());
    smoothedSaturation.setCurrentAndTargetValue(rawParam (ParameterIDs::SATURATION)->load());
    smoothedHiss.setCurrentAndTargetValue(rawParam (ParameterIDs::HISS)->load());
    smoothedStudioSculpt.setCurrentAndTargetValue (rawParam (ParameterIDs::STUDIO_SCULPT)->load());
    smoothedStudioWeave .setCurrentAndTargetValue (rawParam (ParameterIDs::STUDIO_WEAVE) ->load());
    smoothedStudioTilt  .setCurrentAndTargetValue (rawParam (ParameterIDs::STUDIO_TILT)  ->load());
    smoothedWireWow .setCurrentAndTargetValue (rawParam (ParameterIDs::WIRE_WOW)       ->load());
    smoothedWireSat .setCurrentAndTargetValue (rawParam (ParameterIDs::WIRE_SATURATION)->load());
    smoothedWireHiss.setCurrentAndTargetValue (rawParam (ParameterIDs::WIRE_HISS)      ->load());
    smoothedOutputGain.setCurrentAndTargetValue(rawParam (ParameterIDs::OUTPUT_GAIN)->load());
    smoothedMasterMix.setCurrentAndTargetValue(rawParam (ParameterIDs::MASTER_MIX)->load());
    smoothedLoopFeedback.setCurrentAndTargetValue(rawParam (ParameterIDs::LOOP_FEEDBACK)->load());
    smoothedLoopDegrade.setCurrentAndTargetValue(rawParam (ParameterIDs::LOOP_DEGRADE)->load());

    // Space reverb initial values
    smoothedSpaceSize.setCurrentAndTargetValue(rawParam (ParameterIDs::SPACE_SIZE)->load());
    smoothedSpaceDecay.setCurrentAndTargetValue(rawParam (ParameterIDs::SPACE_DECAY)->load());
    smoothedSpaceTone.setCurrentAndTargetValue(rawParam (ParameterIDs::SPACE_TONE)->load());
    smoothedSpaceMix.setCurrentAndTargetValue(rawParam (ParameterIDs::SPACE_MIX)->load());

    // Delay initial values
    smoothedDlyTime.setCurrentAndTargetValue(rawParam (ParameterIDs::DLY_TIME)->load());
    smoothedDlyFeedback.setCurrentAndTargetValue(rawParam (ParameterIDs::DLY_FEEDBACK)->load());
    smoothedDlyTone.setCurrentAndTargetValue(rawParam (ParameterIDs::DLY_TONE)->load());
    smoothedDlyCharacter.setCurrentAndTargetValue(rawParam (ParameterIDs::DLY_CHARACTER)->load());
    smoothedDlyMod.setCurrentAndTargetValue(rawParam (ParameterIDs::DLY_MOD)->load());
    smoothedDlyModRate.setCurrentAndTargetValue(rawParam (ParameterIDs::DLY_MOD_RATE)->load());
    smoothedDlyMix.setCurrentAndTargetValue(rawParam (ParameterIDs::DLY_MIX)->load());
    smoothedDlyDuck.setCurrentAndTargetValue(rawParam (ParameterIDs::DLY_DUCK)->load());
    smoothedDelayFreeze.setCurrentAndTargetValue(rawParam (ParameterIDs::DLY_FREEZE)->load());

}

void TerrainInstrumentAudioProcessor::releaseResources()
{
    grainEngineL.reset();
    grainEngineR.reset();
    tapeProcessorL.reset();
    tapeProcessorR.reset();
    tapeLoop.reset();
    spaceReverb.reset();
    moogDelay.reset();
    terrainChorus.reset();
    eqL.reset();
    eqR.reset();
}

bool TerrainInstrumentAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Instrument: only require mono or stereo output. No input bus check —
    // instruments don't have an audio input bus (host disables it).
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

// ── CPU: equality for the ModConfig broadcast change-gate (field-wise — memcmp is unsafe
//    on structs with padding). Assignments compared only up to numAssignments.
static bool lfoSettingsEq (const wc::LFOSettings& a, const wc::LFOSettings& b) noexcept
{
    return a.shape == b.shape && a.sync == b.sync && a.rateHz == b.rateHz
        && a.syncIdx == b.syncIdx && a.startPhase == b.startPhase
        && a.phaseOffset == b.phaseOffset && a.trigger == b.trigger && a.polarity == b.polarity
        && a.direction == b.direction && a.loopPt == b.loopPt && a.riseMs == b.riseMs          // fb228 — motion edits must rebroadcast
        && a.delayMs == b.delayMs && a.smoothMs == b.smoothMs && a.swing == b.swing
        && a.tripDot == b.tripDot;
}
static bool modCfgEq (const wc::ModConfig& a, const wc::ModConfig& b) noexcept
{
    if (a.numAssignments != b.numAssignments) return false;
    for (int i = 0; i < wc::NUM_LFOS; ++i)
        if (! lfoSettingsEq (a.lfos[i], b.lfos[i])) return false;
    for (int i = 0; i < a.numAssignments; ++i)
    {
        const auto& x = a.assignments[i]; const auto& y = b.assignments[i];
        if (x.source != y.source || x.dest != y.dest || x.depth != y.depth
            || x.auxSource != y.auxSource || x.useAux != y.useAux || x.enabled != y.enabled)
            return false;
    }
    for (int i = 0; i < 8; ++i)
        if (a.driftLanes[i] != b.driftLanes[i]) return false;
    return true;
}

void TerrainInstrumentAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const bool vizLive = vizConsumersLive();   // fb148 — no UI, no viz work (Serum does the same)

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples == 0) return;

    // ANNULUS polyphony: track currently-held MIDI notes (read-only scan; does not
    // consume midiMessages). The resonator tunes ONE voice per held note → polyphonic,
    // pitched, no glide. note-on adds, note-off removes, all-notes-off clears.
    for (const auto meta : midiMessages)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int nn = m.getNoteNumber();
            bool dup = false;
            for (int j = 0; j < resoHeldN_; ++j) if (resoHeld_[j] == nn) { dup = true; break; }
            if (! dup && resoHeldN_ < (int) (sizeof (resoHeld_) / sizeof (int)))
                resoHeld_[resoHeldN_++] = nn;
        }
        else if (m.isNoteOff())
        {
            const int nn = m.getNoteNumber();
            for (int j = 0; j < resoHeldN_; ++j)
                if (resoHeld_[j] == nn) { resoHeld_[j] = resoHeld_[--resoHeldN_]; break; }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            resoHeldN_ = 0;
        }
    }

    // === Sampler engine (Terrain Instrument v0a + slicer v0b) ================
    // Pull sampler params from APVTS into the lock-free atomics that voices read.
    // Phase 1 task 9: APVTS is global — broadcast every block to ALL layers so
    // their voices have live values. Per-layer APVTS is a future Phase refactor.
    //
    // Mark 2 Phase 1 audio-fix (LOOP per-layer): sampleLoopMode REMOVED from
    // this broadcast — it's now per-layer (atomic written by setSampleLoopMode
    // native fn directly, V1 preset compat handled in setStateInformation).
    // Without this, switching LOOP on layer A also forced B/C/D into LOOP.
    {
        const int   rootMidi   = (int) *rawParam (ParameterIDs::ROOT_NOTE);
        const float attackMs   = *rawParam (ParameterIDs::ATTACK_MS);
        const float releaseMs  = *rawParam (ParameterIDs::RELEASE_MS);
        const float chopFadeMs = *rawParam (ParameterIDs::CHOP_FADE_MS);

        for (auto& layer : layers)
        {
            layer.rootMidiNote.store   (rootMidi);
            layer.attackMsAtomic.store (attackMs);
            layer.releaseMsAtomic.store(releaseMs);
            layer.chopFadeMs.store     (chopFadeMs);
        }
    }

    // Mark 2 Phase 1 audio-fix: sliceMode is now per-layer (read from
    // layer.sliceMode inside the layer loop below). sliceSubMode is still
    // global for Phase 1 — only matters when 2+ layers are simultaneously
    // in SLICE mode with different sub-modes (edge case to address later).
    const int sliceSubModeIdx_blk = (int) *rawParam (ParameterIDs::SLICE_SUB_MODE);
    const size_t elIdx = (size_t) editingLayer.load();

    // Helper: derive the SliceContext::Mode enum from a per-layer slice mode +
    // the (still-global) slice sub-mode. PITCH (layerSliceMode==0) always
    // resolves to Whole regardless of sub-mode; SLICE (==1) picks the variant
    // based on the global sub-mode selector.
    auto modeFromLayer = [sliceSubModeIdx_blk] (int layerSliceMode) -> tw::SliceContext::Mode
    {
        if (layerSliceMode == 0)               return tw::SliceContext::Mode::Whole;
        if (sliceSubModeIdx_blk == 3)          return tw::SliceContext::Mode::Layer;
        if (sliceSubModeIdx_blk == 2)          return tw::SliceContext::Mode::ChromaticRandom;
        if (sliceSubModeIdx_blk == 1)          return tw::SliceContext::Mode::ChromaticOneSlice;
        return tw::SliceContext::Mode::ChopChromaticLayout;
    };

    // Drain audition queue — UI-clicked previews of individual slices.
    // Triggered before renderNextBlock so the audition voice starts within
    // this block. Dispatches to the currently-editing layer's synth.
    {
        std::vector<int> drained;
        {
            const juce::SpinLock::ScopedLockType sl (auditionLock);
            drained.swap (auditionQueue);
        }
        if (! drained.empty())
        {
            auto sl = std::atomic_load (&layers[elIdx].currentSlices);
            if (sl && ! sl->empty())
            {
                for (int idx : drained)
                {
                    if (idx >= 0 && idx < (int) sl->size())
                    {
                        layers[elIdx].synth.auditionSlice ((*sl)[(size_t) idx], idx, getSourceVersionId());
                    }
                }
            }
        }
    }

    // ── Per-chop FX independence routing ────────────────────────────────
    // Voices whose chop has fxIndependent=true redirect their output to
    // indyCaptureBus (bypasses the global FX chain entirely). All others
    // write into `buffer` as before and flow through the chain. We add
    // the indy bus back at the END of processBlock as a clean signal.
    if (indyCaptureBus.getNumSamples() < numSamples)
        indyCaptureBus.setSize (2, numSamples, false, true, true);
    indyCaptureBus.clear (0, numSamples);

    // ── Mix page Phase 2: MIDI dispatch filter (trigger modes) ───────────
    // Walks midiMessages once and builds 4 per-layer MIDI buffers based on
    // the active triggerMode. Non-note-on events (note-off, CC, pitch-bend,
    // aftertouch) go to ALL layers so voices already in flight can be
    // turned off / modulated correctly.
    //
    // For note-ons, the picker decides which layer(s) receive the event:
    //   LAYER     → all populated layers
    //   RR        → one layer at roundRobinPos (skip empties, advance)
    //   RANDOM    → one layer picked via weighted random over probabilityWeight
    //   KEYTRACK  → one layer whose [keyZoneMin..Max] contains the note number
    //   VELOCITY  → one layer whose [velocityZoneMin..Max] contains note vel
    //
    // After this block, per-layer renderNextBlock uses perLayerMidi[li]
    // instead of the shared midiMessages.
    std::array<juce::MidiBuffer, 4> perLayerMidi;
    {
        const int  tmode = triggerMode.load();
        const auto isPopulated = [this] (int li) {
            return li >= 0 && li < 4 && layers[(size_t) li].hasSample();
        };

        for (const auto event : midiMessages)
        {
            const auto msg = event.getMessage();
            const int  pos = event.samplePosition;

            if (! msg.isNoteOn())
            {
                // Non-note-on events broadcast to all layers (note-offs, CC, pitch-bend,
                // aftertouch). Layers that didn't receive the corresponding note-on
                // simply have nothing to turn off — JUCE Synthesiser handles gracefully.
                for (auto& buf : perLayerMidi)
                    buf.addEvent (msg, pos);
                continue;
            }

            // ── Note-on routing per trigger mode ──
            const int vel = msg.getVelocity();

            if (tmode == 0)
            {
                // LAYER — all populated layers fire (current Phase 1 behavior).
                for (int li = 0; li < 4; ++li)
                    if (isPopulated (li))
                        perLayerMidi[(size_t) li].addEvent (msg, pos);
            }
            else if (tmode == 1)
            {
                // ROUND-ROBIN — one layer per note. SHUFFLE = random non-repeating
                // populated layer; otherwise sequential A→B→C→D (skip empties).
                int picked = -1;
                if (rrShuffle.load())
                {
                    int pops[4]; int n = 0;
                    for (int li = 0; li < 4; ++li) if (isPopulated (li)) pops[n++] = li;
                    if (n == 1) picked = pops[0];
                    else if (n > 1)
                    {
                        for (int attempts = 0; attempts < 8 && picked < 0; ++attempts)
                        {
                            int cand = pops[juce::jlimit (0, n - 1, (int) (triggerRandom.nextFloat() * (float) n))];
                            if (cand != lastRrLayer) picked = cand;
                        }
                        if (picked < 0) picked = pops[0];
                    }
                    if (picked >= 0)
                    {
                        perLayerMidi[(size_t) picked].addEvent (msg, pos);
                        lastRrLayer = picked;
                        roundRobinPos.store (picked);            // dot shows last-played
                    }
                }
                else
                {
                    int cur = roundRobinPos.load();
                    for (int attempts = 0; attempts < 4; ++attempts)
                    {
                        if (isPopulated (cur)) { picked = cur; break; }
                        cur = (cur + 1) % 4;
                    }
                    if (picked >= 0)
                    {
                        perLayerMidi[(size_t) picked].addEvent (msg, pos);
                        lastRrLayer = picked;
                        roundRobinPos.store ((picked + 1) % 4);  // cursor + dot show next
                    }
                }
            }
            else if (tmode == 2)
            {
                // RANDOM — weighted pick over populated layers' probabilityWeight.
                // weights renormalized over the populated set so empty layers can't win.
                float total = 0.0f;
                for (int li = 0; li < 4; ++li)
                    if (isPopulated (li))
                        total += juce::jmax (0.0f, layers[(size_t) li].probabilityWeight.load());

                if (total > 1.0e-6f)
                {
                    float r = triggerRandom.nextFloat() * total;
                    int picked = -1;
                    for (int li = 0; li < 4; ++li)
                    {
                        if (! isPopulated (li)) continue;
                        const float w = juce::jmax (0.0f, layers[(size_t) li].probabilityWeight.load());
                        if (r < w) { picked = li; break; }
                        r -= w;
                    }
                    if (picked >= 0)
                        perLayerMidi[(size_t) picked].addEvent (msg, pos);
                }
                // total == 0 → no eligible layers, drop the note (predictable silence).
            }
            else if (tmode == 3)
            {
                // KEYTRACK — keyboard split. Fire the layer whose key zone contains
                // this note number. First match wins (zones enforced contiguous +
                // non-overlapping by UI). No match → drop the note.
                const int note = msg.getNoteNumber();
                for (int li = 0; li < 4; ++li)
                {
                    if (! isPopulated (li)) continue;
                    const int lo = layers[(size_t) li].keyZoneMin.load();
                    const int hi = layers[(size_t) li].keyZoneMax.load();
                    if (note >= lo && note <= hi)
                    {
                        perLayerMidi[(size_t) li].addEvent (msg, pos);
                        break;
                    }
                }
            }
            else if (tmode == 4)
            {
                // VELOCITY — fire the layer whose zone contains this velocity.
                // First match wins (zones are enforced contiguous + non-overlapping by UI).
                for (int li = 0; li < 4; ++li)
                {
                    if (! isPopulated (li)) continue;
                    const int lo = layers[(size_t) li].velocityZoneMin.load();
                    const int hi = layers[(size_t) li].velocityZoneMax.load();
                    if (vel >= lo && vel <= hi)
                    {
                        perLayerMidi[(size_t) li].addEvent (msg, pos);
                        break;
                    }
                }
            }
        }
    }

    // ── Mark 2 task 4 / task 9: render each populated layer into its own scratch
    // buffer, then sum (with vol/mute/solo) into master `buffer`.
    // Task 9 complete: each layer now receives its own per-layer SliceContext.
    buffer.clear();   // master starts silent; we sum each layer into it below

    {
        const bool anySolo = std::any_of (layers.begin(), layers.end(),
                                           [](const tw::LayerState& l){ return l.solo.load(); });

        const double blockSec = (double) numSamples / juce::jmax (1.0, getSampleRate());
        const float  decay    = (float) std::exp (-blockSec / 0.065);

        // LAYER-mode MORPH precompute: map the morph focus across POPULATED layers
        // only, so a single loaded layer stays full and the blend travels just over
        // what's actually loaded. morphPos is in populated-index units (0..popCount-1).
        const bool layerMode = (triggerMode.load() == 0);
        int popCount = 0; int popIdx[4] = { -1, -1, -1, -1 };
        for (int i = 0; i < 4; ++i) if (layers[(size_t) i].hasSample()) popIdx[i] = popCount++;
        const float morphPos = layerMorph.load() * (float) juce::jmax (1, popCount - 1);

        for (size_t li = 0; li < layers.size(); ++li)
        {
            auto& layer = layers[li];

            // Skip layers that have no sample loaded (no voices possible).
            if (! layer.hasSample())               continue;

            // Mix page audio-fix: mute/solo gate the SUMMING, NOT the render.
            // We must still call renderNextBlock on every populated layer so its
            // voices process note-offs even when the layer is silenced. Without
            // this, a voice playing on a layer that gets muted or solo-excluded
            // mid-note never receives its note-off and sticks forever — this was
            // the "infinite scan" bug (solo layer A on the mixer while layer C
            // has a scan voice in flight → C is skipped → its scan never ends).
            const bool audible = ! layer.mute.load()
                               && (! anySolo || layer.solo.load());

            // Build per-layer SliceContext. ctx.mode is now sourced from each
            // layer's own sliceMode atomic (Mark 2 Phase 1 audio-fix) so layers
            // can independently choose PITCH vs SLICE. sliceSubMode is still
            // global — when 2+ layers are simultaneously in SLICE the most
            // recent sub-mode click wins for all of them.
            {
                tw::SliceContext ctx;
                ctx.mode             = modeFromLayer (layer.sliceMode.load());
                ctx.rootMidiNote     = layer.rootMidiNote.load();
                ctx.activeSliceIndex = layer.activeSliceIndex.load();
                ctx.sourceVersionId  = getSourceVersionId();
                ctx.slices           = std::atomic_load (&layer.currentSlices);
                ctx.pitchModeSlice   = layer.pitchModeSlice;   // copy-by-value snapshot
                ctx.holdMode         = holdMode.load (std::memory_order_relaxed);
                layer.synth.setSliceContext (ctx);
            }

            // Indy bus routing — Phase 1: only layers[0] feeds the shared indy bus.
            // Other layers would get nullptr (skip routing) if they ever reached here.
            if (li == 0)
                layer.synth.setIndyTargetBufferForVoices (&indyCaptureBus);

            // Render this layer's voices into the per-layer scratch buffer.
            // Mix page Phase 2: each layer now sees a FILTERED midi buffer
            // (perLayerMidi[li] built above by the trigger-mode dispatcher).
            // In LAYER mode this is identical to the old broadcast behavior;
            // in RR/RANDOM/SOLO/VELOCITY modes only the picked layer(s) see
            // each note-on, while non-note-on events still broadcast to all.
            auto& scratch = layerScratch[li];
            scratch.clear();
            layer.synth.renderNextBlock (scratch, perLayerMidi[li], 0, numSamples);

            // Detach indy pointer immediately after render.
            if (li == 0)
                layer.synth.setIndyTargetBufferForVoices (nullptr);

            // Per-layer glow update: walk this layer's voices, write into this
            // layer's sliceGlowLevel. snapshotSliceGlowLevels() reads directly
            // from layers[editingLayer].sliceGlowLevel (Task 5 — singleton removed).
            {
                std::array<float, tw::kMaxGlowSlots> blockMax {};
                for (int v = 0; v < layer.synth.getNumVoices(); ++v)
                {
                    if (auto* sv = dynamic_cast<tw::SamplerVoice*> (layer.synth.getVoice (v)))
                    {
                        if (! sv->isPlaying()) continue;
                        const int idx = sv->getSliceIndex();
                        if (idx < 0 || idx >= tw::kMaxGlowSlots) continue;
                        const float lvl = sv->getEnvelopeLevel();
                        if (lvl > blockMax[(size_t) idx]) blockMax[(size_t) idx] = lvl;
                    }
                }
                for (int i = 0; i < tw::kMaxGlowSlots; ++i)
                {
                    float prev = layer.sliceGlowLevel[(size_t) i].load (std::memory_order_relaxed);
                    float next = juce::jmax (blockMax[(size_t) i], prev * decay);
                    layer.sliceGlowLevel[(size_t) i].store (next, std::memory_order_relaxed);
                }
            }

            // Mix page Phase D: write to this layer's rolling stem buffer.
            // DRY = raw layerScratch (just synth output before mix). MIX = post
            // volume/pan applied (computed below). We capture DRY now so it's
            // available regardless of which mode the user picks at export time;
            // the MIX path snapshots after the mixer math.
            // (For v1 we just capture DRY always; the stemSourceMode toggle
            // gates which version the EXPORT writes. Two-buffer-per-layer
            // would double RAM — instead we apply mix at export when needed.)
            writeToStemBuffer ((int) li, scratch.getReadPointer (0), scratch.getReadPointer (1), numSamples);

            // Sum this layer into the master buffer with per-layer gain + pan.
            // Equal-power pan: panAngle = (panNorm + 1) * π/4 → leftGain = cos,
            // rightGain = sin. Center (panNorm=0) gives 0.707/0.707 (constant
            // perceived power across the pan range). pre-clamped to [-1, +1].
            float morphGain = 1.0f;
            if (layerMode && popCount > 1 && popIdx[(int) li] >= 0)
                morphGain = juce::jlimit (0.0f, 1.0f,
                                          1.0f - std::abs ((float) popIdx[(int) li] - morphPos) / 2.0f);
            const float g    = layer.volume.load() * morphGain;
            const float pn   = juce::jlimit (-1.0f, 1.0f, layer.pan.load());
            const float ang  = (pn + 1.0f) * (juce::MathConstants<float>::pi * 0.25f);
            const float gL   = g * std::cos (ang);
            const float gR   = g * std::sin (ang);
            // Only AUDIBLE layers contribute to the master mix. Muted / solo-
            // excluded layers still rendered above (so their voices release),
            // they just don't sum here.
            if (audible)
            {
                buffer.addFrom (0, 0, scratch, 0, 0, numSamples, gL);
                buffer.addFrom (1, 0, scratch, 1, 0, numSamples, gR);
            }

            // Per-layer peak meter — feeds the channel-strip meter widget.
            // Visual gain: the raw magnitude reads low for typical samples, so
            // the strip meters barely moved. Apply a perceptual sqrt curve +
            // ~6 dB makeup so the LED bars are lively and responsive (user
            // wanted "4K 60fps interactive" levels) while still clipping at 1.0.
            // Muted layers report 0 (meter goes dark, matching what you hear).
            {
                const float rawL = audible ? scratch.getMagnitude (0, 0, numSamples) * std::abs (gL) : 0.0f;
                const float rawR = audible ? scratch.getMagnitude (1, 0, numSamples) * std::abs (gR) : 0.0f;
                // Perceptual curve: sqrt expands low levels, ×1.6 makeup, clamp.
                const float visL = juce::jmin (1.0f, std::sqrt (rawL) * 1.6f);
                const float visR = juce::jmin (1.0f, std::sqrt (rawR) * 1.6f);
                // Fast attack (instant), slower decay for readable peaks.
                const float prevL = layer.peakLevelL.load (std::memory_order_relaxed);
                const float prevR = layer.peakLevelR.load (std::memory_order_relaxed);
                layer.peakLevelL.store (juce::jmax (visL, prevL * decay), std::memory_order_relaxed);
                layer.peakLevelR.store (juce::jmax (visR, prevR * decay), std::memory_order_relaxed);
            }
        }
    }

    // ── Synth section render (Phase 1 MPV) ──────────────────────────────
    // Push current SYN_* APVTS values onto every SynthVoice each block
    // — same pattern as the per-layer atomic broadcast above.
    // FLOW · ARP reads synModCfg + synModBpm at the synth-render site (which sits
    // AFTER this SYN_* scope closes), so their declarations are hoisted out here.
    // [CC integration note: wiring patch anchored 5b/5c against these but they were
    //  scope-local; hoisting keeps Opus's 5b/5c code verbatim and in-scope.]
    wc::ModConfig synModCfg;
    float         synModBpm = 0.0f;
    {
        // ═══ fb75 — UNIVERSAL LFO MOD (block-rate) ═══════════════════════════════════
        // ONE O(routes) pass turns the mod matrix into per-destination offsets for every
        // newly-routable target (filters 1/2, noise, blend depths, per-osc level/pan, and
        // all the engine macro knobs). Sources = the processor's free-running LFO mirrors
        // (flowLfo_ — the proven FLOW/RESO pattern; the voice LFOs run Free so values agree).
        // CPU: zero routes ⇒ sums stay 0 ⇒ every wrapped value is byte-identical ⇒ the
        // change-gates below hold and this whole feature costs NOTHING at idle. Dests below
        // Res1 (the per-voice batch: frame/warp/fold/cutoff/coarse/sub…) keep their richer
        // per-voice application in SynthVoice and are skipped here (no double-modulation).
        // ZPROBE-ENV (TERRAIN_ENV_PROBE=1): drive the whole env→global chain without a UI or MIDI
        {
            static const bool envProbe = (getenv ("TERRAIN_ENV_PROBE") != nullptr);
            static int pb = 0;
            if (envProbe)
            {
                ++pb;
                // fb183 — the probe plays Max's exact repro through REAL MIDI: env 7 (pluck,
                // sustain 0) owns osc A LEVEL with the knob AT ZERO; C held, then F while C's
                // 2s amp release still rings. Old code: F re-opened C's level (the ghost).
                static double probeMs = 0.0;
                static bool pOnC = false, pOffC = false, pOnF = false;
                if (pb == 5)
                {
                    setSynthDynEnvs ("{\"n\":2,\"e\":[{\"a\":80,\"d\":400,\"s\":0.4,\"r\":250},{\"a\":5,\"d\":200,\"s\":0.0,\"r\":300}]}");
                    setSynthModMatrix ("[{\"s\":106,\"d\":" + juce::String ((int) wc::ModDest::LevelA) + ",\"v\":1.0},"
                                        "{\"s\":105,\"d\":" + juce::String ((int) wc::ModDest::Res1)   + ",\"v\":1.0},"
                                        "{\"s\":106,\"d\":" + juce::String ((int) wc::ModDest::FmFbA)  + ",\"v\":1.0},"
                                        "{\"s\":106,\"d\":" + juce::String ((int) wc::ModDest::Warp)   + ",\"v\":1.0},"
                                        "{\"s\":105,\"d\":" + juce::String ((int) wc::ModDest::EnvPBase + 1) + ",\"v\":1.0}]");
                    if (auto* pL = apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_LEVEL)) pL->store (0.0f);
                    if (auto* pW = apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_WARP_AMOUNT)) pW->store (0.0f);   // fb188 — the iffy-warp repro: knob at ZERO
                    if (auto* pR = apvts.getRawParameterValue ("SYN_ENV_AMP_R"))               pR->store (2000.0f);
                }
                if (pb >= 5)
                {
                    if (! pOnC  && probeMs >= 100.0) { midiMessages.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 0); pOnC  = true; }
                    if (! pOffC && probeMs >= 400.0) { midiMessages.addEvent (juce::MidiMessage::noteOff (1, 60), 0);                    pOffC = true; }
                    if (! pOnF  && probeMs >= 430.0) { midiMessages.addEvent (juce::MidiMessage::noteOn  (1, 65, (juce::uint8) 100), 0); pOnF  = true; }
                    probeMs += 1000.0 * (double) buffer.getNumSamples() / getSampleRate();
                }
            }
        }
        // ── fb178 — MONO ENVELOPE TAP upkeep (only when an env feeds a global dest) ──
        {
            const uint32_t gm = monoEnvGlobalMask_.load (std::memory_order_acquire);
            if (gm != 0)
            {
                // legacy 1..5 shapes from their params (same values the voices consume)
                static const char* const kPfx[5] = { "SYN_ENV_AMP_", "SYN_ENV_FLT_", "SYN_ENV_PIT_", "SYN_ENV_M1_", "SYN_ENV_M2_" };
                for (int k = 0; k < 5; ++k)
                {
                    if ((gm & (1u << k)) == 0) continue;
                    auto rp = [&] (const char* f) -> float { auto* v = apvts.getRawParameterValue (juce::String (kPfx[k]) + f); return v ? v->load() : 0.0f; };
                    terrain_setEnvDAHDSR (monoLegEnv_[k], rp("DLY"), rp("A"), rp("H"), rp("D"), rp("S"), rp("R"),
                                          rp("CA"), rp("CD"), rp("CR"), rp("LOOP") > 0.5f);
                }
                // dynamic 6..32 shapes from the blob mirror (refresh if stale)
                if (dynEnvSeen_ != dynEnvVersion_.load (std::memory_order_acquire))
                {
                    const juce::ScopedTryLock dsl (dynEnvLock_);
                    if (dsl.isLocked())
                    {
                        for (int k = 0; k < kMaxDynEnvs; ++k) dynEnvAudio_[k] = dynEnvShapes_[k];
                        dynEnvAudioCount_ = dynEnvCount_;
                        dynEnvSeen_ = dynEnvVersion_.load (std::memory_order_acquire);
                    }
                }
                for (int k = 0; k < dynEnvAudioCount_; ++k)
                {
                    if ((gm & (1u << (5 + k))) == 0) continue;
                    const auto& de = dynEnvAudio_[k];
                    terrain_setEnvDAHDSR (monoDynEnv_[k], de.dl, de.a, de.h, de.d, de.s, de.r, de.ca, de.cd, de.cr, de.loop);
                }
                // ANY note-on retriggers the pool; the last note-off releases it.
                for (const auto meta : midiMessages)
                {
                    const auto msg = meta.getMessage();
                    if (msg.isNoteOn())
                    {
                        ++monoHeld_;
                        for (int k = 0; k < 5; ++k)           if (gm & (1u << k))       monoLegEnv_[k].noteOn();   // fb180 — no reset: click-free retrigger from the current level
                        for (int k = 0; k < kMaxDynEnvs; ++k) if (gm & (1u << (5 + k))) monoDynEnv_[k].noteOn();
                    }
                    else if (msg.isNoteOff())
                    {
                        if (--monoHeld_ <= 0)
                        {
                            monoHeld_ = 0;
                            for (int k = 0; k < 5; ++k)           if (gm & (1u << k))       monoLegEnv_[k].noteOff();
                            for (int k = 0; k < kMaxDynEnvs; ++k) if (gm & (1u << (5 + k))) monoDynEnv_[k].noteOff();
                        }
                    }
                }
                const int nAdv = buffer.getNumSamples();
                for (int k = 0; k < 5; ++k)           if (gm & (1u << k))       for (int n2 = 0; n2 < nAdv; ++n2) monoLegEnv_[k].tick();
                for (int k = 0; k < kMaxDynEnvs; ++k) if (gm & (1u << (5 + k))) for (int n2 = 0; n2 < nAdv; ++n2) monoDynEnv_[k].tick();
            }
        }
        float modSums[(int) wc::ModDest::NumDests] = { 0 };
        float envOwnW[(int) wc::ModDest::NumDests] = { 0 };   // fb184 — Σ|depth| of env claims per Linear01 dest
        float envOwnV[(int) wc::ModDest::NumDests] = { 0 };   // fb184 — Σ|depth|·env: the owned target (0..1 of span)
        {
            static const char* const kLfoDepthIds[wc::NUM_LFOS] = {
                ParameterIDs::LFO1_DEPTH, ParameterIDs::LFO2_DEPTH, ParameterIDs::LFO3_DEPTH,
                ParameterIDs::LFO4_DEPTH, ParameterIDs::LFO5_DEPTH, ParameterIDs::LFO6_DEPTH,
                ParameterIDs::LFO7_DEPTH, ParameterIDs::LFO8_DEPTH, ParameterIDs::LFO9_DEPTH,
                ParameterIDs::LFO10_DEPTH };
            const juce::ScopedLock sl (synModLock);
            // fb245 — LFO→LFO amt for GLOBAL dests. Per-voice already scales its LFO peaks (SynthVoice ~1985),
            // but the processor global path read the RAW peek, so LfoAmt silently no-op'd for every global route.
            // Pre-pass the amt exactly like the per-voice pass (source × master × depth), then scale the peek below.
            float lfoAmt[wc::NUM_LFOS] = { 0.0f };
            for (const auto& r : synModRoutes)
            {
                if (r.dest < (int) wc::ModDest::LfoAmt1 || r.dest >= (int) wc::ModDest::LfoAmt1 + wc::NUM_LFOS) continue;
                if (r.src >= 0 && r.src < wc::NUM_LFOS)
                    lfoAmt[r.dest - (int) wc::ModDest::LfoAmt1] += flowLfo_[r.src].peek() * (r.depth * *rawParam (kLfoDepthIds[r.src]));
                else if (r.src >= wc::kEnvSrcBase && r.src < wc::kEnvSrcBase + 32)
                    lfoAmt[r.dest - (int) wc::ModDest::LfoAmt1] += monoEnvLevelOf ((int) wc::envSourceFor (r.src - wc::kEnvSrcBase + 1)) * std::abs (r.depth);
            }
            const float velCurve01_ = *rawParam (ParameterIDs::SYN_VEL_DEPTH) * 0.01f;   // fb263 — velocity block-rate feed: curve-shaped, most-active voice
            const float velGlobal_  = std::pow (juce::jmax (0.0f, velVis_.load (std::memory_order_relaxed)), std::pow (3.0f, 1.0f - 2.0f * velCurve01_));
            for (const auto& r : synModRoutes)
            {
                if (r.dest < (int) wc::ModDest::Res1 || r.dest >= (int) wc::ModDest::NumDests) continue;
                if (r.src >= wc::kEnvSrcBase && r.src < wc::kEnvSrcBase + 32)   // fb178 — mono env tap
                {
                    // fb183 — env→LEVEL went PER-VOICE (ownership in SynthVoice): the shared
                    // mono tap re-opened every sounding voice's level on any note-on — the
                    // ghost-note gate Max heard. Levels never read the tap again.
                    if (r.dest >= (int) wc::ModDest::LevelA && r.dest <= (int) wc::ModDest::LevelD) continue;
                    const float lv = monoEnvLevelOf ((int) wc::envSourceFor (r.src - wc::kEnvSrcBase + 1));
                    // fb184 — OWNERSHIP for every unipolar (Linear01) dest: the env claims the
                    // knob by |depth| instead of offsetting it — knob-down + atten-100 follows
                    // the shape (Max's law, generalized from fb183 Levels). Semitone/Bipolar
                    // dests keep the fb179 offset (a pitch/pan pluck swings AROUND the knob).
                    const auto& diR = wc::kDestInfo[r.dest];
                    if (diR.domain == wc::ModDomain::Linear01)
                    {
                        const float dwR = std::abs (r.depth);
                        envOwnW[r.dest] += dwR;
                        envOwnV[r.dest] += dwR * (lv + 1.0f);
                        continue;
                    }
                    modSums[r.dest] += wc::routeContribution (diR, lv, std::abs (r.depth));   // fb180 — magnitude
                    continue;
                }
                if (r.src == wc::kVelSrc)   // fb263 — VELOCITY at block-rate: reaches Level/Pan/Res/FX/macros (global, most-active voice). Fixes velocity→Volume being a silent no-op (viz moved, no audio).
                {
                    modSums[r.dest] += wc::routeContribution (wc::kDestInfo[r.dest], velGlobal_, r.depth);
                    continue;
                }
                if (r.src < 0 || r.src >= wc::NUM_LFOS) continue;
                const float master = *rawParam (kLfoDepthIds[r.src]);   // per-LFO MASTER ring (same law as the matrix merge below)
                modSums[r.dest] += wc::routeContribution (wc::kDestInfo[r.dest],
                                                          flowLfo_[r.src].peek() * juce::jlimit (0.0f, 2.0f, 1.0f + lfoAmt[r.src]), r.depth * master);   // fb245 — LfoAmt now scales global dests too
            }
        }
        { // ZPROBE-ENV log
            static const bool envProbe2 = (getenv ("TERRAIN_ENV_PROBE") != nullptr);
            static int pb2 = 0;
            if (envProbe2 && (++pb2 % 8) == 0)
            {
                float v60 = -1.f, v65 = -1.f, w60 = -1.f; int a60 = 0, a65 = 0;
                for (int i = 0; i < synthEngine.getNumVoices(); ++i)
                    if (auto* sv = synthVoices_[(size_t) i])
                    {
                        const int n = sv->getCurrentlyPlayingNote();
                        if (n == 60) { v60 = sv->dbgLvlSm (0); a60 = sv->isAmpEnvActive() ? 1 : 0; w60 = sv->dbgWarpEffA(); }
                        if (n == 65) { v65 = sv->dbgLvlSm (0); a65 = sv->isAmpEnvActive() ? 1 : 0; }
                    }
                juce::File ("/tmp/envprobe.log").appendText (
                    "pb=" + juce::String (pb2)
                    + " held=" + juce::String (monoHeld_)
                    + " v60=" + juce::String (v60, 4) + "/" + juce::String (a60)
                    + " v65=" + juce::String (v65, 4) + "/" + juce::String (a65)
                    + " sumLevA=" + juce::String (modSums[(int) wc::ModDest::LevelA], 4)
                    + " wFb=" + juce::String (envOwnW[(int) wc::ModDest::FmFbA], 2)
                    + " vFb=" + juce::String (envOwnV[(int) wc::ModDest::FmFbA], 4)
                    + " vRes=" + juce::String (envOwnV[(int) wc::ModDest::Res1], 4)
                    + " w60=" + juce::String (w60, 4)
                    + " wA1=" + juce::String (envOwnW[(int) wc::ModDest::EnvPBase + 1], 2)
                    + " vA1=" + juce::String (envOwnV[(int) wc::ModDest::EnvPBase + 1], 4) + "\n");
            }
        }
        // fb184 — OWNERSHIP at the app site: the env's claim w crossfades the (LFO-modulated)
        // base toward the env's own shape mapped across lo..hi. w=0 → legacy additive exactly.
        auto ownM = [&] (float base, int d, float lo, float hi)
        {
            const float w = envOwnW[d] > 1.0f ? 1.0f : envOwnW[d];
            return juce::jlimit (lo, hi, (base + modSums[d]) * (1.0f - w) + lo * w + envOwnV[d] * (hi - lo));
        };
        // Wrap helper: base param + this block's mod, clamped ONCE to the param's range.
        auto mdP = [&] (const char* pid, wc::ModDest d, float lo, float hi)
        { return ownM (*rawParam (pid), (int) d, lo, hi); };
        // fb193 — S4b env-param dests: the same ownership law applied in the param's own
        // normalized space (convertTo0to1 honors range+skew). Early-out when unrouted.
        auto modP = [&] (const char* pid, float raw, int d) -> float
        {
            const float w0 = envOwnW[d];
            if (w0 <= 0.0f && modSums[d] == 0.0f) return raw;
            if (auto* p = apvts.getParameter (juce::String (pid)))
            {
                const float w = w0 > 1.0f ? 1.0f : w0;
                const float n = p->convertTo0to1 (raw);
                return p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, (n + modSums[d]) * (1.0f - w) + envOwnV[d]));
            }
            return raw;
        };
        // fb340 — Morph's mod resolve lives HERE (modP's scope closes before the FX push site):
        dstMorphEff_ = modP (ParameterIDs::SYN_DST_SIG,
                             rawParam (ParameterIDs::SYN_DST_SIG)->load(),
                             (int) wc::ModDest::DstMorph);
        // dyn envs (blob ms, no APVTS param) — the editor's own norm curve (1..8000ms, skew .3)
        auto dynModMs = [&] (float ms, int d) -> float
        {
            const float w0 = envOwnW[d]; if (w0 <= 0.0f && modSums[d] == 0.0f) return ms;
            const float w = w0 > 1.0f ? 1.0f : w0;
            const float n = std::pow (juce::jlimit (0.0f, 1.0f, (ms - 1.0f) / 9999.0f), 0.3f);
            const float e = juce::jlimit (0.0f, 1.0f, (n + modSums[d]) * (1.0f - w) + envOwnV[d]);
            return 1.0f + std::pow (e, 1.0f / 0.3f) * 9999.0f;
        };
        auto dynModS = [&] (float s, int d) -> float
        {
            const float w0 = envOwnW[d]; if (w0 <= 0.0f && modSums[d] == 0.0f) return s;
            const float w = w0 > 1.0f ? 1.0f : w0;
            return juce::jlimit (0.0f, 1.0f, (s + modSums[d]) * (1.0f - w) + envOwnV[d]);
        };
        // fb252 — SPECTRAL MOD: publish the effective (base + LFO/env) spectral amount per osc so the
        // message-thread morph rebuild (rebuildMorphIfNeeded reads spectralEffAmt_) follows modulation.
        // mdP applies the same ownership law as every other Linear01 dest (LFO additive via modSums, env
        // via envOwnW/V). Quantize to 1/128 ONLY when routed, so a moving LFO doesn't churn the throttled
        // rebuild (~20 Hz on the message thread — the audio thread never builds a wavetable).
        {
            static const char* const kSpecIds[4] = { ParameterIDs::SYN_OSC_A_SPECTRAL_AMT, ParameterIDs::SYN_OSC_B_SPECTRAL_AMT,
                                                     ParameterIDs::SYN_OSC_C_SPECTRAL_AMT, ParameterIDs::SYN_OSC_D_SPECTRAL_AMT };
            for (int o = 0; o < 4; ++o)
            {
                const int d = (int) wc::ModDest::SpectralA + o;   // fb76 dests already exist (line ~128); only the WRITE was missing
                float eff = mdP (kSpecIds[o], (wc::ModDest) d, 0.0f, 1.0f);
                if (envOwnW[d] > 0.0f || modSums[d] != 0.0f) eff = std::round (eff * 128.0f) / 128.0f;   // anti-churn, routed only
                spectralEffAmt_[o].store (eff, std::memory_order_relaxed);
            }
        }
        const int   oct     = (int)   *rawParam (ParameterIDs::SYN_OSC_A_OCT);
        const int   semi    = (int)   *rawParam (ParameterIDs::SYN_OSC_A_SEMI);
        const float cent    =         *rawParam (ParameterIDs::SYN_OSC_A_CENT);
        const float lvl     =         mdP (ParameterIDs::SYN_OSC_A_LEVEL, wc::ModDest::LevelA, 0.0f, 1.0f);
        const float pan     =         mdP (ParameterIDs::SYN_OSC_A_PAN, wc::ModDest::PanA, -1.0f, 1.0f);
        const float cut     =         *rawParam (ParameterIDs::SYN_FILTER1_CUT);
        const float res     =         mdP (ParameterIDs::SYN_FILTER1_RES, wc::ModDest::Res1, 0.0f, 1.0f);
        const float fltKt1  =         100.0f * ownM (*rawParam (ParameterIDs::SYN_FILTER1_KEYTRACK) * 0.01f, (int) wc::ModDest::FTrack1, 0.0f, 1.0f);   // fb78 Track mod · fb184 ownership
        // Batch 1 Filter — TYPE, DRV, bipolar ENV, and the dedicated FLT ADSR.
        const int   filtType= (int)   *rawParam (ParameterIDs::SYN_FILTER1_TYPE);
        const float filtDrv =         mdP (ParameterIDs::SYN_FILTER1_DRV, wc::ModDest::FDrv1, 0.0f, 1.0f);
        const float filtEnv =         mdP (ParameterIDs::SYN_FILTER1_ENV, wc::ModDest::FEnv1, -1.0f, 1.0f);
        // Filter 2 (independent) + per-filter mix + routing.
        const float cut2     =        *rawParam (ParameterIDs::SYN_FILTER2_CUT);
        const float res2     =        mdP (ParameterIDs::SYN_FILTER2_RES, wc::ModDest::Res2, 0.0f, 1.0f);
        const float fltKt2   =        100.0f * ownM (*rawParam (ParameterIDs::SYN_FILTER2_KEYTRACK) * 0.01f, (int) wc::ModDest::FTrack2, 0.0f, 1.0f);   // fb184 ownership
        const int   filtType2= (int)  *rawParam (ParameterIDs::SYN_FILTER2_TYPE);
        const float filtDrv2 =        mdP (ParameterIDs::SYN_FILTER2_DRV, wc::ModDest::FDrv2, 0.0f, 1.0f);
        const float filtEnv2 =        mdP (ParameterIDs::SYN_FILTER2_ENV, wc::ModDest::FEnv2, -1.0f, 1.0f);
        const float filtMix1 =        mdP (ParameterIDs::SYN_FILTER1_MIX, wc::ModDest::FMix1, 0.0f, 1.0f);
        const float filtMix2 =        mdP (ParameterIDs::SYN_FILTER2_MIX, wc::ModDest::FMix2, 0.0f, 1.0f);
        const float filtVel1 =        mdP (ParameterIDs::SYN_FILTER1_VEL, wc::ModDest::FVel1, 0.0f, 1.0f);   // fb78 — back-panel Vel mod
        const float filtVel2 =        mdP (ParameterIDs::SYN_FILTER2_VEL, wc::ModDest::FVel2, 0.0f, 1.0f);
        const float filtPdrv1=        mdP (ParameterIDs::SYN_FILTER1_PDRV, wc::ModDest::FPDrv1, 0.0f, 1.0f);
        const float filtPdrv2=        mdP (ParameterIDs::SYN_FILTER2_PDRV, wc::ModDest::FPDrv2, 0.0f, 1.0f);
        const int   filtDrvType1=(int)*rawParam (ParameterIDs::SYN_FILTER1_DRIVETYPE);   // 0=Tube..5=Fuzz
        const int   filtDrvType2=(int)*rawParam (ParameterIDs::SYN_FILTER2_DRIVETYPE);
        const int   filtPole1= (int)  *rawParam (ParameterIDs::SYN_FILTER1_POLES);    // 0=6 1=12 2=18 3=24 dB
        const int   filtPole2= (int)  *rawParam (ParameterIDs::SYN_FILTER2_POLES);
        const float filtSpread1=      mdP (ParameterIDs::SYN_FILTER1_SPREAD, wc::ModDest::FSpread1, 0.0f, 1.0f);   // stereo width 0..1 · fb78 mod
        const float filtSpread2=      mdP (ParameterIDs::SYN_FILTER2_SPREAD, wc::ModDest::FSpread2, 0.0f, 1.0f);
        const int   filtRoute= (int)  *rawParam (ParameterIDs::SYN_FILTER_ROUTING);
        // Per-osc filter routing masks (A,B,C,D,Sub) for each filter — bool as >0.5.
        // fb79 — PER-OSC CONTINUOUS FILTER SENDS (the F1/F2 pills, each osc independent, default 0 =
        // dry). Replaces the binary A-D masks (SYN_FILTER*_SRC_A..D are no longer consumed for oscs —
        // the pills + the filter back-panel A-D toggles both drive these send params now). Sub stays
        // a binary mask (the S pill). LFO-routable per send (OscF1SendA.. — "everything routable").
        const float f1src[5] = { mdP (ParameterIDs::SYN_OSC_A_F1MIX, wc::ModDest::OscF1SendA, 0.0f, 1.0f),
                                 mdP (ParameterIDs::SYN_OSC_B_F1MIX, wc::ModDest::OscF1SendB, 0.0f, 1.0f),
                                 mdP (ParameterIDs::SYN_OSC_C_F1MIX, wc::ModDest::OscF1SendC, 0.0f, 1.0f),
                                 mdP (ParameterIDs::SYN_OSC_D_F1MIX, wc::ModDest::OscF1SendD, 0.0f, 1.0f),
                                 *rawParam (ParameterIDs::SYN_FILTER1_SRC_SUB) > 0.5f ? 1.0f : 0.0f };
        const float f2src[5] = { mdP (ParameterIDs::SYN_OSC_A_F2MIX, wc::ModDest::OscF2SendA, 0.0f, 1.0f),
                                 mdP (ParameterIDs::SYN_OSC_B_F2MIX, wc::ModDest::OscF2SendB, 0.0f, 1.0f),
                                 mdP (ParameterIDs::SYN_OSC_C_F2MIX, wc::ModDest::OscF2SendC, 0.0f, 1.0f),
                                 mdP (ParameterIDs::SYN_OSC_D_F2MIX, wc::ModDest::OscF2SendD, 0.0f, 1.0f),
                                 *rawParam (ParameterIDs::SYN_FILTER2_SRC_SUB) > 0.5f ? 1.0f : 0.0f };
        const bool  noiseF1 = *rawParam (ParameterIDs::SYN_FILTER1_SRC_NOISE) > 0.5f;   // fb63 — noise → filter routing
        const bool  noiseF2 = *rawParam (ParameterIDs::SYN_FILTER2_SRC_NOISE) > 0.5f;
        const float fltEnvA =         modP (ParameterIDs::SYN_ENV_FLT_A, *rawParam (ParameterIDs::SYN_ENV_FLT_A), (int) wc::ModDest::EnvPBase + 7);   // fb193
        const float fltEnvD =         modP (ParameterIDs::SYN_ENV_FLT_D, *rawParam (ParameterIDs::SYN_ENV_FLT_D), (int) wc::ModDest::EnvPBase + 9);   // fb193
        const float fltEnvS =         modP (ParameterIDs::SYN_ENV_FLT_S, *rawParam (ParameterIDs::SYN_ENV_FLT_S), (int) wc::ModDest::EnvPBase + 10);   // fb193
        const float fltEnvR =         modP (ParameterIDs::SYN_ENV_FLT_R, *rawParam (ParameterIDs::SYN_ENV_FLT_R), (int) wc::ModDest::EnvPBase + 11);   // fb193
        const float ampA    =         modP (ParameterIDs::SYN_ENV_AMP_A, *rawParam (ParameterIDs::SYN_ENV_AMP_A), (int) wc::ModDest::EnvPBase + 1);   // fb193
        const float ampD    =         modP (ParameterIDs::SYN_ENV_AMP_D, *rawParam (ParameterIDs::SYN_ENV_AMP_D), (int) wc::ModDest::EnvPBase + 3);   // fb193
        const float ampS    =         modP (ParameterIDs::SYN_ENV_AMP_S, *rawParam (ParameterIDs::SYN_ENV_AMP_S), (int) wc::ModDest::EnvPBase + 4);   // fb193
        const float ampR    =         modP (ParameterIDs::SYN_ENV_AMP_R, *rawParam (ParameterIDs::SYN_ENV_AMP_R), (int) wc::ModDest::EnvPBase + 5);   // fb193

        // ── Envelope DAHDSR extension reads (Batch 2/3) ──
        const float ampDly = modP (ParameterIDs::SYN_ENV_AMP_DLY, *rawParam (ParameterIDs::SYN_ENV_AMP_DLY), (int) wc::ModDest::EnvPBase + 0);   // fb193
        const float ampHld = modP (ParameterIDs::SYN_ENV_AMP_H, *rawParam (ParameterIDs::SYN_ENV_AMP_H), (int) wc::ModDest::EnvPBase + 2);   // fb193
        const float ampCa = *rawParam (ParameterIDs::SYN_ENV_AMP_CA);
        const float ampCd = *rawParam (ParameterIDs::SYN_ENV_AMP_CD);
        const float ampCr = *rawParam (ParameterIDs::SYN_ENV_AMP_CR);
        const bool  ampLoop = *rawParam (ParameterIDs::SYN_ENV_AMP_LOOP) > 0.5f;
        const float fltDly = modP (ParameterIDs::SYN_ENV_FLT_DLY, *rawParam (ParameterIDs::SYN_ENV_FLT_DLY), (int) wc::ModDest::EnvPBase + 6);   // fb193
        const float fltHld = modP (ParameterIDs::SYN_ENV_FLT_H, *rawParam (ParameterIDs::SYN_ENV_FLT_H), (int) wc::ModDest::EnvPBase + 8);   // fb193
        const float fltCa = *rawParam (ParameterIDs::SYN_ENV_FLT_CA);
        const float fltCd = *rawParam (ParameterIDs::SYN_ENV_FLT_CD);
        const float fltCr = *rawParam (ParameterIDs::SYN_ENV_FLT_CR);
        const bool  fltLoop = *rawParam (ParameterIDs::SYN_ENV_FLT_LOOP) > 0.5f;
        const float pitDly = modP (ParameterIDs::SYN_ENV_PIT_DLY, *rawParam (ParameterIDs::SYN_ENV_PIT_DLY), (int) wc::ModDest::EnvPBase + 12);   // fb193
        const float pitA = modP (ParameterIDs::SYN_ENV_PIT_A, *rawParam (ParameterIDs::SYN_ENV_PIT_A), (int) wc::ModDest::EnvPBase + 13);   // fb193
        const float pitHld = modP (ParameterIDs::SYN_ENV_PIT_H, *rawParam (ParameterIDs::SYN_ENV_PIT_H), (int) wc::ModDest::EnvPBase + 14);   // fb193
        const float pitD = modP (ParameterIDs::SYN_ENV_PIT_D, *rawParam (ParameterIDs::SYN_ENV_PIT_D), (int) wc::ModDest::EnvPBase + 15);   // fb193
        const float pitS = modP (ParameterIDs::SYN_ENV_PIT_S, *rawParam (ParameterIDs::SYN_ENV_PIT_S), (int) wc::ModDest::EnvPBase + 16);   // fb193
        const float pitR = modP (ParameterIDs::SYN_ENV_PIT_R, *rawParam (ParameterIDs::SYN_ENV_PIT_R), (int) wc::ModDest::EnvPBase + 17);   // fb193
        const float pitCa = *rawParam (ParameterIDs::SYN_ENV_PIT_CA);
        const float pitCd = *rawParam (ParameterIDs::SYN_ENV_PIT_CD);
        const float pitCr = *rawParam (ParameterIDs::SYN_ENV_PIT_CR);
        const bool  pitLoop = *rawParam (ParameterIDs::SYN_ENV_PIT_LOOP) > 0.5f;
        const float pitDepth = *rawParam (ParameterIDs::SYN_ENV_PIT_DEPTH);
        const float m1eDly = modP (ParameterIDs::SYN_ENV_M1_DLY, *rawParam (ParameterIDs::SYN_ENV_M1_DLY), (int) wc::ModDest::EnvPBase + 18);   // fb193
        const float m1eA = modP (ParameterIDs::SYN_ENV_M1_A, *rawParam (ParameterIDs::SYN_ENV_M1_A), (int) wc::ModDest::EnvPBase + 19);   // fb193
        const float m1eHld = modP (ParameterIDs::SYN_ENV_M1_H, *rawParam (ParameterIDs::SYN_ENV_M1_H), (int) wc::ModDest::EnvPBase + 20);   // fb193
        const float m1eD = modP (ParameterIDs::SYN_ENV_M1_D, *rawParam (ParameterIDs::SYN_ENV_M1_D), (int) wc::ModDest::EnvPBase + 21);   // fb193
        const float m1eS = modP (ParameterIDs::SYN_ENV_M1_S, *rawParam (ParameterIDs::SYN_ENV_M1_S), (int) wc::ModDest::EnvPBase + 22);   // fb193
        const float m1eR = modP (ParameterIDs::SYN_ENV_M1_R, *rawParam (ParameterIDs::SYN_ENV_M1_R), (int) wc::ModDest::EnvPBase + 23);   // fb193
        const float m1eCa = *rawParam (ParameterIDs::SYN_ENV_M1_CA);
        const float m1eCd = *rawParam (ParameterIDs::SYN_ENV_M1_CD);
        const float m1eCr = *rawParam (ParameterIDs::SYN_ENV_M1_CR);
        const bool  m1eLoop = *rawParam (ParameterIDs::SYN_ENV_M1_LOOP) > 0.5f;
        const float m2eDly = modP (ParameterIDs::SYN_ENV_M2_DLY, *rawParam (ParameterIDs::SYN_ENV_M2_DLY), (int) wc::ModDest::EnvPBase + 24);   // fb193
        const float m2eA = modP (ParameterIDs::SYN_ENV_M2_A, *rawParam (ParameterIDs::SYN_ENV_M2_A), (int) wc::ModDest::EnvPBase + 25);   // fb193
        const float m2eHld = modP (ParameterIDs::SYN_ENV_M2_H, *rawParam (ParameterIDs::SYN_ENV_M2_H), (int) wc::ModDest::EnvPBase + 26);   // fb193
        const float m2eD = modP (ParameterIDs::SYN_ENV_M2_D, *rawParam (ParameterIDs::SYN_ENV_M2_D), (int) wc::ModDest::EnvPBase + 27);   // fb193
        const float m2eS = modP (ParameterIDs::SYN_ENV_M2_S, *rawParam (ParameterIDs::SYN_ENV_M2_S), (int) wc::ModDest::EnvPBase + 28);   // fb193
        const float m2eR = modP (ParameterIDs::SYN_ENV_M2_R, *rawParam (ParameterIDs::SYN_ENV_M2_R), (int) wc::ModDest::EnvPBase + 29);   // fb193
        const float m2eCa = *rawParam (ParameterIDs::SYN_ENV_M2_CA);
        const float m2eCd = *rawParam (ParameterIDs::SYN_ENV_M2_CD);
        const float m2eCr = *rawParam (ParameterIDs::SYN_ENV_M2_CR);
        const bool  m2eLoop = *rawParam (ParameterIDs::SYN_ENV_M2_LOOP) > 0.5f;
        // Per-envelope routing (envs 2–5): destination index + bipolar depth.
        const int   env2Dest  = (int) *rawParam (ParameterIDs::SYN_ENV2_DEST);
        const float env2Depth =       *rawParam (ParameterIDs::SYN_ENV2_DEPTH);
        const int   env3Dest  = (int) *rawParam (ParameterIDs::SYN_ENV3_DEST);
        const float env3Depth =       *rawParam (ParameterIDs::SYN_ENV3_DEPTH);
        const int   env4Dest  = (int) *rawParam (ParameterIDs::SYN_ENV4_DEST);
        const float env4Depth =       *rawParam (ParameterIDs::SYN_ENV4_DEPTH);
        const int   env5Dest  = (int) *rawParam (ParameterIDs::SYN_ENV5_DEST);
        const float env5Depth =       *rawParam (ParameterIDs::SYN_ENV5_DEPTH);
        // Phase 2A wavetable selection — resolve preset enum to const Wavetable*.
        const int            wtPreset = (int) *rawParam (ParameterIDs::SYN_OSC_A_WT_PRESET);
        const float          wtFrame  =       *rawParam (ParameterIDs::SYN_OSC_A_WT_FRAME);
        const tw::Wavetable* wt       = wavetableForOsc (0, morphA_, wtPreset);
        // Phase 2C — warp mode + amount
        const int   warpMode   = (int) *rawParam (ParameterIDs::SYN_OSC_A_WARP_MODE);
        const int   phaseModeA = (int) *rawParam (ParameterIDs::SYN_OSC_A_PHASE_MODE);
        const float warpAmount =       *rawParam (ParameterIDs::SYN_OSC_A_WARP_AMOUNT);
        // Phase 3 — OSC A engine choice
        const int engineIdx = (int) *rawParam (ParameterIDs::SYN_OSC_A_ENGINE);

        // Phase 9 — OSC B params
        const int   octB       = (int)  *rawParam (ParameterIDs::SYN_OSC_B_OCT);
        const int   semiB      = (int)  *rawParam (ParameterIDs::SYN_OSC_B_SEMI);
        const float centB      =        *rawParam (ParameterIDs::SYN_OSC_B_CENT);
        const float lvlB       =        mdP (ParameterIDs::SYN_OSC_B_LEVEL, wc::ModDest::LevelB, 0.0f, 1.0f);
        const float panB       =        mdP (ParameterIDs::SYN_OSC_B_PAN, wc::ModDest::PanB, -1.0f, 1.0f);
        const int   wtPresetB  = (int)  *rawParam (ParameterIDs::SYN_OSC_B_WT_PRESET);
        const float wtFrameB   =        *rawParam (ParameterIDs::SYN_OSC_B_WT_FRAME);
        const tw::Wavetable* wtB = wavetableForOsc (1, morphB_, wtPresetB);
        const int   warpModeB  = (int)  *rawParam (ParameterIDs::SYN_OSC_B_WARP_MODE);
        const int   phaseModeB = (int)  *rawParam (ParameterIDs::SYN_OSC_B_PHASE_MODE);
        const float warpAmountB =       *rawParam (ParameterIDs::SYN_OSC_B_WARP_AMOUNT);
        // WARP 2 — chained second slot per OSC
        const int   warp2ModeA = (int)  *rawParam (ParameterIDs::SYN_OSC_A_WARP2_MODE);
        const float warp2AmtA  =        mdP (ParameterIDs::SYN_OSC_A_WARP2_AMT, wc::ModDest::Warp2A, 0.0f, 1.0f);   // fb77 — back-panel WARP2 amount mod
        const int   warp2ModeB = (int)  *rawParam (ParameterIDs::SYN_OSC_B_WARP2_MODE);
        const float warp2AmtB  =        mdP (ParameterIDs::SYN_OSC_B_WARP2_AMT, wc::ModDest::Warp2B, 0.0f, 1.0f);
        const int   engineIdxB = (int)  *rawParam (ParameterIDs::SYN_OSC_B_ENGINE);
        // WAVER — per-OSC analog pitch-drift depth (0..100 %). Pushed per voice below.
        const float waverA      =       *rawParam (ParameterIDs::SYN_OSC_A_WAVER);
        const float waverB      =       *rawParam (ParameterIDs::SYN_OSC_B_WAVER);
        // KEYTRACK — per-OSC note->destination depth (0..100 %) + destination choice.
        const float ktDepthA    =       *rawParam (ParameterIDs::SYN_OSC_A_KEYTRACK);
        const int   ktDestA     = (int)  *rawParam (ParameterIDs::SYN_OSC_A_KEYTRACK_DEST);
        const float ktDepthB    =       *rawParam (ParameterIDs::SYN_OSC_B_KEYTRACK);
        const int   ktDestB     = (int)  *rawParam (ParameterIDs::SYN_OSC_B_KEYTRACK_DEST);
        // ROUTE — per-OSC source + destination + bipolar amount (-100..100 %).
        const int   rtSrcA      = (int)  *rawParam (ParameterIDs::SYN_OSC_A_ROUTE_SRC);
        const int   rtDestA     = (int)  *rawParam (ParameterIDs::SYN_OSC_A_ROUTE_DEST);
        const float rtAmtA      =       *rawParam (ParameterIDs::SYN_OSC_A_ROUTE_AMT);
        const int   rtSrcB      = (int)  *rawParam (ParameterIDs::SYN_OSC_B_ROUTE_SRC);
        const int   rtDestB     = (int)  *rawParam (ParameterIDs::SYN_OSC_B_ROUTE_DEST);
        const float rtAmtB      =       *rawParam (ParameterIDs::SYN_OSC_B_ROUTE_AMT);

        // ── OSC C / D params (4-osc) — mirror OSC B; pushed per voice below ──
        const int   octC=(int)*rawParam (ParameterIDs::SYN_OSC_C_OCT), semiC=(int)*rawParam (ParameterIDs::SYN_OSC_C_SEMI);
        const float centC=*rawParam (ParameterIDs::SYN_OSC_C_CENT), lvlC=mdP (ParameterIDs::SYN_OSC_C_LEVEL, wc::ModDest::LevelC, 0.0f, 1.0f), panC=mdP (ParameterIDs::SYN_OSC_C_PAN, wc::ModDest::PanC, -1.0f, 1.0f);
        const int   wtPresetC=(int)*rawParam (ParameterIDs::SYN_OSC_C_WT_PRESET);
        const float wtFrameC=*rawParam (ParameterIDs::SYN_OSC_C_WT_FRAME);
        const tw::Wavetable* wtC = wavetableForOsc (2, morphC_, wtPresetC);
        const int   warpModeC=(int)*rawParam (ParameterIDs::SYN_OSC_C_WARP_MODE), phaseModeC=(int)*rawParam (ParameterIDs::SYN_OSC_C_PHASE_MODE);
        const float warpAmountC=*rawParam (ParameterIDs::SYN_OSC_C_WARP_AMOUNT);
        const int   warp2ModeC=(int)*rawParam (ParameterIDs::SYN_OSC_C_WARP2_MODE);
        const float warp2AmtC=mdP (ParameterIDs::SYN_OSC_C_WARP2_AMT, wc::ModDest::Warp2C, 0.0f, 1.0f);
        const int   engineIdxC=(int)*rawParam (ParameterIDs::SYN_OSC_C_ENGINE);
        const float waverC=*rawParam (ParameterIDs::SYN_OSC_C_WAVER);
        const float ktDepthC=*rawParam (ParameterIDs::SYN_OSC_C_KEYTRACK);
        const int   ktDestC=(int)*rawParam (ParameterIDs::SYN_OSC_C_KEYTRACK_DEST);
        const int   rtSrcC=(int)*rawParam (ParameterIDs::SYN_OSC_C_ROUTE_SRC), rtDestC=(int)*rawParam (ParameterIDs::SYN_OSC_C_ROUTE_DEST);
        const float rtAmtC=*rawParam (ParameterIDs::SYN_OSC_C_ROUTE_AMT);
        const int   octD=(int)*rawParam (ParameterIDs::SYN_OSC_D_OCT), semiD=(int)*rawParam (ParameterIDs::SYN_OSC_D_SEMI);
        const float centD=*rawParam (ParameterIDs::SYN_OSC_D_CENT), lvlD=mdP (ParameterIDs::SYN_OSC_D_LEVEL, wc::ModDest::LevelD, 0.0f, 1.0f), panD=mdP (ParameterIDs::SYN_OSC_D_PAN, wc::ModDest::PanD, -1.0f, 1.0f);
        const int   wtPresetD=(int)*rawParam (ParameterIDs::SYN_OSC_D_WT_PRESET);
        const float wtFrameD=*rawParam (ParameterIDs::SYN_OSC_D_WT_FRAME);
        const tw::Wavetable* wtD = wavetableForOsc (3, morphD_, wtPresetD);
        const int   warpModeD=(int)*rawParam (ParameterIDs::SYN_OSC_D_WARP_MODE), phaseModeD=(int)*rawParam (ParameterIDs::SYN_OSC_D_PHASE_MODE);
        const float warpAmountD=*rawParam (ParameterIDs::SYN_OSC_D_WARP_AMOUNT);
        const int   warp2ModeD=(int)*rawParam (ParameterIDs::SYN_OSC_D_WARP2_MODE);
        const float warp2AmtD=mdP (ParameterIDs::SYN_OSC_D_WARP2_AMT, wc::ModDest::Warp2D, 0.0f, 1.0f);
        const int   engineIdxD=(int)*rawParam (ParameterIDs::SYN_OSC_D_ENGINE);
        const float waverD=*rawParam (ParameterIDs::SYN_OSC_D_WAVER);
        const float ktDepthD=*rawParam (ParameterIDs::SYN_OSC_D_KEYTRACK);
        const int   ktDestD=(int)*rawParam (ParameterIDs::SYN_OSC_D_KEYTRACK_DEST);
        const int   rtSrcD=(int)*rawParam (ParameterIDs::SYN_OSC_D_ROUTE_SRC), rtDestD=(int)*rawParam (ParameterIDs::SYN_OSC_D_ROUTE_DEST);
        const float rtAmtD=*rawParam (ParameterIDs::SYN_OSC_D_ROUTE_AMT);

        // ── SOLO / MUTE per OSC — bool params (getRawParameterValue returns normalized 0..1 → >0.5) ──
        const bool muteA = *rawParam (ParameterIDs::SYN_OSC_A_MUTE) > 0.5f;
        const bool soloA = *rawParam (ParameterIDs::SYN_OSC_A_SOLO) > 0.5f;
        const bool muteB = *rawParam (ParameterIDs::SYN_OSC_B_MUTE) > 0.5f;
        const bool soloB = *rawParam (ParameterIDs::SYN_OSC_B_SOLO) > 0.5f;
        const bool muteC = *rawParam (ParameterIDs::SYN_OSC_C_MUTE) > 0.5f;
        const bool soloC = *rawParam (ParameterIDs::SYN_OSC_C_SOLO) > 0.5f;
        const bool muteD = *rawParam (ParameterIDs::SYN_OSC_D_MUTE) > 0.5f;
        const bool soloD = *rawParam (ParameterIDs::SYN_OSC_D_SOLO) > 0.5f;
        const bool anySolo = soloA || soloB || soloC || soloD;
        // OSC ENABLE — the real per-osc ON/OFF (the white OSC letters in the UI). Rides the
        // same click-free gate one-pole as solo/mute; once the gate settles at silence the
        // voice SKIPS the osc's whole render path (engines included), so OFF costs ~nothing —
        // unlike volume 0, which kept the osc silently burning CPU.
        const bool enA = *rawParam (ParameterIDs::SYN_OSC_A_ENABLE) > 0.5f;
        const bool enB = *rawParam (ParameterIDs::SYN_OSC_B_ENABLE) > 0.5f;
        const bool enC = *rawParam (ParameterIDs::SYN_OSC_C_ENABLE) > 0.5f;
        const bool enD = *rawParam (ParameterIDs::SYN_OSC_D_ENABLE) > 0.5f;
        auto oscGate = [anySolo](bool en, bool mute, bool solo){ return (! en || mute || (anySolo && !solo)) ? 0.0f : 1.0f; };
        const float gateA = oscGate(enA, muteA, soloA), gateB = oscGate(enB, muteB, soloB),
                    gateC = oscGate(enC, muteC, soloC), gateD = oscGate(enD, muteD, soloD);

        // ════════ SAMPLE-ENGINE-PUSH — read per-OSC Sample params (Opus) ════════
        tw::SynthVoice::SampleEngineParams spA;
        spA.scan      = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_SCAN);       spA.stretch = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH);
        spA.formant   = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_FORMANT);    spA.spray   = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_SPRAY);
        spA.xfade     = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_XFADE);      spA.start   = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_START);
        spA.end       = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_END);        spA.loopStart = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_START);
        spA.loopEnd   = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_END);   spA.loopMode  = (int) *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_MODE);
        spA.stretchMode = (int) *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH_MODE);
        spA.formantMode = (int) *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_FORMANT_MODE);
        spA.snap      = (int) *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_SNAP);      spA.fadeIn    = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_FADE_IN);
        spA.fadeOut   = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_FADE_OUT);
        spA.air       = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_AIR);
        spA.warp      = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_WARP);
        spA.warpMode  = (int) *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_WARPMODE);
        spA.fadeInCurve  = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_FADEIN_CURVE);
        spA.fadeOutCurve = *rawParam (ParameterIDs::SYN_OSC_A_SAMPLE_FADEOUT_CURVE);
        tw::SynthVoice::SampleEngineParams spB;
        spB.scan      = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_SCAN);       spB.stretch = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_STRETCH);
        spB.formant   = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_FORMANT);    spB.spray   = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_SPRAY);
        spB.xfade     = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_XFADE);      spB.start   = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_START);
        spB.end       = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_END);        spB.loopStart = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_START);
        spB.loopEnd   = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_END);   spB.loopMode  = (int) *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_MODE);
        spB.stretchMode = (int) *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_STRETCH_MODE);
        spB.formantMode = (int) *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_FORMANT_MODE);
        spB.snap      = (int) *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_SNAP);      spB.fadeIn    = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_FADE_IN);
        spB.fadeOut   = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_FADE_OUT);
        spB.air       = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_AIR);
        spB.warp      = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_WARP);
        spB.warpMode  = (int) *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_WARPMODE);
        spB.fadeInCurve  = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_FADEIN_CURVE);
        spB.fadeOutCurve = *rawParam (ParameterIDs::SYN_OSC_B_SAMPLE_FADEOUT_CURVE);
        tw::SynthVoice::SampleEngineParams spC;
        spC.scan      = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_SCAN);       spC.stretch = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_STRETCH);
        spC.formant   = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_FORMANT);    spC.spray   = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_SPRAY);
        spC.xfade     = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_XFADE);      spC.start   = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_START);
        spC.end       = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_END);        spC.loopStart = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_START);
        spC.loopEnd   = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_END);   spC.loopMode  = (int) *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_MODE);
        spC.stretchMode = (int) *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_STRETCH_MODE);
        spC.formantMode = (int) *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_FORMANT_MODE);
        spC.snap      = (int) *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_SNAP);      spC.fadeIn    = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_FADE_IN);
        spC.fadeOut   = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_FADE_OUT);
        spC.air       = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_AIR);
        spC.warp      = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_WARP);
        spC.warpMode  = (int) *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_WARPMODE);
        spC.fadeInCurve  = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_FADEIN_CURVE);
        spC.fadeOutCurve = *rawParam (ParameterIDs::SYN_OSC_C_SAMPLE_FADEOUT_CURVE);
        tw::SynthVoice::SampleEngineParams spD;
        spD.scan      = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_SCAN);       spD.stretch = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_STRETCH);
        spD.formant   = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_FORMANT);    spD.spray   = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_SPRAY);
        spD.xfade     = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_XFADE);      spD.start   = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_START);
        spD.end       = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_END);        spD.loopStart = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_START);
        spD.loopEnd   = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_END);   spD.loopMode  = (int) *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_MODE);
        spD.stretchMode = (int) *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_STRETCH_MODE);
        spD.formantMode = (int) *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_FORMANT_MODE);
        spD.snap      = (int) *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_SNAP);      spD.fadeIn    = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_FADE_IN);
        spD.fadeOut   = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_FADE_OUT);
        spD.air       = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_AIR);
        spD.warp      = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_WARP);
        spD.warpMode  = (int) *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_WARPMODE);
        spD.fadeInCurve  = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_FADEIN_CURVE);
        spD.fadeOutCurve = *rawParam (ParameterIDs::SYN_OSC_D_SAMPLE_FADEOUT_CURVE);

        // ── GRAIN engine: gather the 12 grain functions per OSC (GRAIN-ENGINE-GATHER) ──
        // ID order: scan,density,size,spray,shape,key, position,pitch,pspray,width,dir,skew.
        // 'key' is the only choice → cast to index. static table = built once (no per-block alloc).
        // Air/Stretch/StretchMode + region(start/end) are patched in AFTER from the already-gathered
        // Sample params (spA..spD) — they reuse the Sample osc's params (waveform right-click + handles).
        // ── fb75 — SAMPLE-ENGINE knob mod (block-rate; also flows into GRANULAR via withSampleExtras).
        //    MUST run before the gpX gather below and before the engChanged compare (a modulated
        //    struct must differ from lastSpX_ so it pushes). Region/loop points stay UNMODDED. ──
        {
            tw::SynthVoice::SampleEngineParams* spMod[4] = { &spA, &spB, &spC, &spD };
            for (int o = 0; o < 4; ++o)
            {
                auto& sp = *spMod[o];
                sp.scan    = ownM (sp.scan, (int) wc::ModDest::SampScanA + o, -1.0f, 1.0f);
                sp.stretch = ownM (sp.stretch, (int) wc::ModDest::SampStretchA + o, 0.0f, 1.0f);
                sp.formant = ownM (sp.formant, (int) wc::ModDest::SampFormantA + o, -1.0f, 1.0f);   // param range IS -1..1 (bipolar formant)
                sp.air     = ownM (sp.air, (int) wc::ModDest::SampAirA + o, 0.0f, 1.0f);
                sp.spray   = ownM (sp.spray, (int) wc::ModDest::SampSprayA + o, 0.0f, 1.0f);
                sp.xfade   = ownM (sp.xfade, (int) wc::ModDest::SampXfadeA + o, 0.0f, 1.0f);
            }
        }
        static const char* const GRAIN_IDS[4][12] = {
            { ParameterIDs::SYN_OSC_A_GRAIN_SCAN, ParameterIDs::SYN_OSC_A_GRAIN_DENSITY, ParameterIDs::SYN_OSC_A_GRAIN_SIZE, ParameterIDs::SYN_OSC_A_GRAIN_SPRAY, ParameterIDs::SYN_OSC_A_GRAIN_SHAPE, ParameterIDs::SYN_OSC_A_GRAIN_KEY, ParameterIDs::SYN_OSC_A_GRAIN_POSITION, ParameterIDs::SYN_OSC_A_GRAIN_PITCH, ParameterIDs::SYN_OSC_A_GRAIN_PSPRAY, ParameterIDs::SYN_OSC_A_GRAIN_WIDTH, ParameterIDs::SYN_OSC_A_GRAIN_DIR, ParameterIDs::SYN_OSC_A_GRAIN_SKEW },
            { ParameterIDs::SYN_OSC_B_GRAIN_SCAN, ParameterIDs::SYN_OSC_B_GRAIN_DENSITY, ParameterIDs::SYN_OSC_B_GRAIN_SIZE, ParameterIDs::SYN_OSC_B_GRAIN_SPRAY, ParameterIDs::SYN_OSC_B_GRAIN_SHAPE, ParameterIDs::SYN_OSC_B_GRAIN_KEY, ParameterIDs::SYN_OSC_B_GRAIN_POSITION, ParameterIDs::SYN_OSC_B_GRAIN_PITCH, ParameterIDs::SYN_OSC_B_GRAIN_PSPRAY, ParameterIDs::SYN_OSC_B_GRAIN_WIDTH, ParameterIDs::SYN_OSC_B_GRAIN_DIR, ParameterIDs::SYN_OSC_B_GRAIN_SKEW },
            { ParameterIDs::SYN_OSC_C_GRAIN_SCAN, ParameterIDs::SYN_OSC_C_GRAIN_DENSITY, ParameterIDs::SYN_OSC_C_GRAIN_SIZE, ParameterIDs::SYN_OSC_C_GRAIN_SPRAY, ParameterIDs::SYN_OSC_C_GRAIN_SHAPE, ParameterIDs::SYN_OSC_C_GRAIN_KEY, ParameterIDs::SYN_OSC_C_GRAIN_POSITION, ParameterIDs::SYN_OSC_C_GRAIN_PITCH, ParameterIDs::SYN_OSC_C_GRAIN_PSPRAY, ParameterIDs::SYN_OSC_C_GRAIN_WIDTH, ParameterIDs::SYN_OSC_C_GRAIN_DIR, ParameterIDs::SYN_OSC_C_GRAIN_SKEW },
            { ParameterIDs::SYN_OSC_D_GRAIN_SCAN, ParameterIDs::SYN_OSC_D_GRAIN_DENSITY, ParameterIDs::SYN_OSC_D_GRAIN_SIZE, ParameterIDs::SYN_OSC_D_GRAIN_SPRAY, ParameterIDs::SYN_OSC_D_GRAIN_SHAPE, ParameterIDs::SYN_OSC_D_GRAIN_KEY, ParameterIDs::SYN_OSC_D_GRAIN_POSITION, ParameterIDs::SYN_OSC_D_GRAIN_PITCH, ParameterIDs::SYN_OSC_D_GRAIN_PSPRAY, ParameterIDs::SYN_OSC_D_GRAIN_WIDTH, ParameterIDs::SYN_OSC_D_GRAIN_DIR, ParameterIDs::SYN_OSC_D_GRAIN_SKEW }
        };
        auto gatherGrain = [this] (const char* const* id)
        {
            tw::GranularEngineParams g;
            g.scan       = *rawParam (id[0]);
            g.density    = *rawParam (id[1]);
            g.size       = *rawParam (id[2]);
            g.spray      = *rawParam (id[3]);
            g.shape      = *rawParam (id[4]);
            g.key        = (int) *rawParam (id[5]);   // choice → index
            g.position   = *rawParam (id[6]);
            g.pitch      = *rawParam (id[7]);
            g.pitchSpray = *rawParam (id[8]);
            g.width      = *rawParam (id[9]);
            g.dir        = *rawParam (id[10]);
            g.skew       = *rawParam (id[11]);
            return g;
        };
        // Patch Air/Stretch/StretchMode + region from the Sample params so the granular engine
        // hears the waveform right-click controls AND the region (start/end) handles.
        auto withSampleExtras = [] (tw::GranularEngineParams g, const tw::SynthVoice::SampleEngineParams& sp)
        {
            g.air         = sp.air;
            g.stretch     = sp.stretch;
            g.stretchMode = sp.stretchMode;
            g.regStart    = sp.start;
            g.regEnd      = sp.end;
            g.loopStart   = sp.loopStart;  // the LOOP bracket — loop modes catch + loop INSIDE it
            g.loopEnd     = sp.loopEnd;    // (was missing → Ping-Pong bounced at the trim ends, sailing past the bracket)
            g.loopMode    = sp.loopMode;   // One-Shot/Fwd/Rev/Ping-Pong/Tailed → granular scan behavior
            return g;
        };
        tw::GranularEngineParams gpA = withSampleExtras (gatherGrain (GRAIN_IDS[0]), spA);   // fb75 — non-const: the mod block below offsets the knob fields
        tw::GranularEngineParams gpB = withSampleExtras (gatherGrain (GRAIN_IDS[1]), spB);
        tw::GranularEngineParams gpC = withSampleExtras (gatherGrain (GRAIN_IDS[2]), spC);
        tw::GranularEngineParams gpD = withSampleExtras (gatherGrain (GRAIN_IDS[3]), spD);
        // ── fb75 — GRANULAR knob mod (block-rate; the "star position" ask lives here: GrainPos). ──
        {
            tw::GranularEngineParams* gpMod[4] = { &gpA, &gpB, &gpC, &gpD };
            for (int o = 0; o < 4; ++o)
            {
                auto& g = *gpMod[o];
                g.position   = ownM (g.position, (int) wc::ModDest::GrainPosA + o, 0.0f, 1.0f);
                g.density    = ownM (g.density   , (int) wc::ModDest::GrainDensityA + o, 0.0f, 1.0f);
                g.size       = ownM (g.size, (int) wc::ModDest::GrainSizeA + o, 0.0f, 1.0f);
                g.pitch      = ownM (g.pitch, (int) wc::ModDest::GrainPitchA + o, -48.0f, 48.0f);
                g.spray      = ownM (g.spray, (int) wc::ModDest::GrainSprayA + o, 0.0f, 1.0f);
                g.pitchSpray = ownM (g.pitchSpray, (int) wc::ModDest::GrainPSprayA + o, 0.0f, 1.0f);
                g.shape      = ownM (g.shape, (int) wc::ModDest::GrainShapeA + o, 0.0f, 1.0f);
                g.skew       = ownM (g.skew, (int) wc::ModDest::GrainSkewA + o, -1.0f, 1.0f);
                g.width      = ownM (g.width, (int) wc::ModDest::GrainWidthA + o, 0.0f, 1.0f);
                g.scan       = ownM (g.scan, (int) wc::ModDest::GrainScanA + o, -1.0f, 1.0f);
                g.dir        = ownM (g.dir, (int) wc::ModDest::GrainDirA + o, -1.0f, 1.0f);   // fb78
                g.key        = juce::jlimit (0, 6, (int) std::lround (ownM ((float) g.key, (int) wc::ModDest::GrainKeyA + o, 0.0f, 6.0f)));   // fb78 stepped · fb184 ownership
            }
        }

        // ── FM engine: gather the 12 wavetable-carrier FM params per OSC (FM-ENGINE-GATHER) ──
        // ID order: algo, ratio1, depth1, ratio2, depth2, feedback, then the WEATHERING page:
        // strike, age, rust, gale, bend, storm. 'algo' is the only choice.
        static const char* const FM_IDS[4][12] = {
            { ParameterIDs::SYN_OSC_A_FM_ALGO, ParameterIDs::SYN_OSC_A_FM_RATIO1, ParameterIDs::SYN_OSC_A_FM_DEPTH1, ParameterIDs::SYN_OSC_A_FM_RATIO2, ParameterIDs::SYN_OSC_A_FM_DEPTH2, ParameterIDs::SYN_OSC_A_FM_FB,
              ParameterIDs::SYN_OSC_A_FM_STRIKE, ParameterIDs::SYN_OSC_A_FM_AGE, ParameterIDs::SYN_OSC_A_FM_RUST, ParameterIDs::SYN_OSC_A_FM_GALE, ParameterIDs::SYN_OSC_A_FM_BEND, ParameterIDs::SYN_OSC_A_FM_STORM },
            { ParameterIDs::SYN_OSC_B_FM_ALGO, ParameterIDs::SYN_OSC_B_FM_RATIO1, ParameterIDs::SYN_OSC_B_FM_DEPTH1, ParameterIDs::SYN_OSC_B_FM_RATIO2, ParameterIDs::SYN_OSC_B_FM_DEPTH2, ParameterIDs::SYN_OSC_B_FM_FB,
              ParameterIDs::SYN_OSC_B_FM_STRIKE, ParameterIDs::SYN_OSC_B_FM_AGE, ParameterIDs::SYN_OSC_B_FM_RUST, ParameterIDs::SYN_OSC_B_FM_GALE, ParameterIDs::SYN_OSC_B_FM_BEND, ParameterIDs::SYN_OSC_B_FM_STORM },
            { ParameterIDs::SYN_OSC_C_FM_ALGO, ParameterIDs::SYN_OSC_C_FM_RATIO1, ParameterIDs::SYN_OSC_C_FM_DEPTH1, ParameterIDs::SYN_OSC_C_FM_RATIO2, ParameterIDs::SYN_OSC_C_FM_DEPTH2, ParameterIDs::SYN_OSC_C_FM_FB,
              ParameterIDs::SYN_OSC_C_FM_STRIKE, ParameterIDs::SYN_OSC_C_FM_AGE, ParameterIDs::SYN_OSC_C_FM_RUST, ParameterIDs::SYN_OSC_C_FM_GALE, ParameterIDs::SYN_OSC_C_FM_BEND, ParameterIDs::SYN_OSC_C_FM_STORM },
            { ParameterIDs::SYN_OSC_D_FM_ALGO, ParameterIDs::SYN_OSC_D_FM_RATIO1, ParameterIDs::SYN_OSC_D_FM_DEPTH1, ParameterIDs::SYN_OSC_D_FM_RATIO2, ParameterIDs::SYN_OSC_D_FM_DEPTH2, ParameterIDs::SYN_OSC_D_FM_FB,
              ParameterIDs::SYN_OSC_D_FM_STRIKE, ParameterIDs::SYN_OSC_D_FM_AGE, ParameterIDs::SYN_OSC_D_FM_RUST, ParameterIDs::SYN_OSC_D_FM_GALE, ParameterIDs::SYN_OSC_D_FM_BEND, ParameterIDs::SYN_OSC_D_FM_STORM }
        };
        float fmVals[4][12];
        for (int o = 0; o < 4; ++o)
            for (int k = 0; k < 12; ++k)
                fmVals[o][k] = *rawParam (FM_IDS[o][k]);
        // fb75/78 — FM knob mod (block-rate): ratios/depths (k=1..4), fb (k=5), WEATHERING (k=6..11). algo untouched.
        for (int o = 0; o < 4; ++o)
        {
            fmVals[o][1]  = ownM (fmVals[o][1], (int) wc::ModDest::FmRatio1A + o, 0.25f, 16.0f);   // fb78
            fmVals[o][2]  = ownM (fmVals[o][2] , (int) wc::ModDest::FmDepth1A + o, 0.0f, 1.0f);
            fmVals[o][3]  = ownM (fmVals[o][3], (int) wc::ModDest::FmRatio2A + o, 0.25f, 16.0f);
            fmVals[o][4]  = ownM (fmVals[o][4] , (int) wc::ModDest::FmDepth2A + o, 0.0f, 1.0f);
            fmVals[o][5]  = ownM (fmVals[o][5], (int) wc::ModDest::FmFbA + o, 0.0f, 1.0f);
            fmVals[o][6]  = ownM (fmVals[o][6] , (int) wc::ModDest::FmStrikeA + o, 0.0f, 1.0f);
            fmVals[o][7]  = ownM (fmVals[o][7], (int) wc::ModDest::FmAgeA + o, 0.0f, 1.0f);
            fmVals[o][8]  = ownM (fmVals[o][8], (int) wc::ModDest::FmRustA + o, 0.0f, 1.0f);
            fmVals[o][9]  = ownM (fmVals[o][9], (int) wc::ModDest::FmGaleA + o, 0.0f, 1.0f);
            fmVals[o][10] = ownM (fmVals[o][10], (int) wc::ModDest::FmBendA + o, 0.0f, 1.0f);
            fmVals[o][11] = ownM (fmVals[o][11], (int) wc::ModDest::FmStormA + o, 0.0f, 1.0f);
        }

        // ── RESYNTH engine: gather the resynthesis params per OSC (GEODE-ENGINE-GATHER) ──
        // ID strings keep GEODE_* (preset stability); meaning REMAPPED to the Resynth fields:
        // POSITION→start, FOSSIL→stretch, CREEP→scan, SILT→crush, DISTILL→shape, HAZE→drive.
        // FRACTURE(id9)=MELT smear; BEDROCK(id14) reserved. id15=Shape id16=Cut id17=Drive id18=Sieve modes.
        static const char* const GEODE_IDS[4][28] = {
            { ParameterIDs::SYN_OSC_A_GEODE_POSITION, ParameterIDs::SYN_OSC_A_GEODE_FOSSIL, ParameterIDs::SYN_OSC_A_GEODE_CREEP, ParameterIDs::SYN_OSC_A_GEODE_SILT, ParameterIDs::SYN_OSC_A_GEODE_FORMANT, ParameterIDs::SYN_OSC_A_GEODE_CUT, ParameterIDs::SYN_OSC_A_GEODE_SIEVE, ParameterIDs::SYN_OSC_A_GEODE_DISTILL, ParameterIDs::SYN_OSC_A_GEODE_HAZE, ParameterIDs::SYN_OSC_A_GEODE_FRACTURE, ParameterIDs::SYN_OSC_A_GEODE_TILT, ParameterIDs::SYN_OSC_A_GEODE_QUALITY, ParameterIDs::SYN_OSC_A_GEODE_FKEEP, ParameterIDs::SYN_OSC_A_GEODE_LOOP, ParameterIDs::SYN_OSC_A_GEODE_BEDROCK, ParameterIDs::SYN_OSC_A_GEODE_SHAPE_TARGET, ParameterIDs::SYN_OSC_A_GEODE_CUT_MODE, ParameterIDs::SYN_OSC_A_GEODE_DRIVE_MODE, ParameterIDs::SYN_OSC_A_GEODE_SIEVE_MODE , ParameterIDs::SYN_OSC_A_SAMPLE_START, ParameterIDs::SYN_OSC_A_SAMPLE_END, ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_START, ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_END, ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_MODE, ParameterIDs::SYN_OSC_A_SAMPLE_FADE_IN, ParameterIDs::SYN_OSC_A_SAMPLE_FADE_OUT, ParameterIDs::SYN_OSC_A_SAMPLE_FADEIN_CURVE, ParameterIDs::SYN_OSC_A_SAMPLE_FADEOUT_CURVE },
            { ParameterIDs::SYN_OSC_B_GEODE_POSITION, ParameterIDs::SYN_OSC_B_GEODE_FOSSIL, ParameterIDs::SYN_OSC_B_GEODE_CREEP, ParameterIDs::SYN_OSC_B_GEODE_SILT, ParameterIDs::SYN_OSC_B_GEODE_FORMANT, ParameterIDs::SYN_OSC_B_GEODE_CUT, ParameterIDs::SYN_OSC_B_GEODE_SIEVE, ParameterIDs::SYN_OSC_B_GEODE_DISTILL, ParameterIDs::SYN_OSC_B_GEODE_HAZE, ParameterIDs::SYN_OSC_B_GEODE_FRACTURE, ParameterIDs::SYN_OSC_B_GEODE_TILT, ParameterIDs::SYN_OSC_B_GEODE_QUALITY, ParameterIDs::SYN_OSC_B_GEODE_FKEEP, ParameterIDs::SYN_OSC_B_GEODE_LOOP, ParameterIDs::SYN_OSC_B_GEODE_BEDROCK, ParameterIDs::SYN_OSC_B_GEODE_SHAPE_TARGET, ParameterIDs::SYN_OSC_B_GEODE_CUT_MODE, ParameterIDs::SYN_OSC_B_GEODE_DRIVE_MODE, ParameterIDs::SYN_OSC_B_GEODE_SIEVE_MODE , ParameterIDs::SYN_OSC_B_SAMPLE_START, ParameterIDs::SYN_OSC_B_SAMPLE_END, ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_START, ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_END, ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_MODE, ParameterIDs::SYN_OSC_B_SAMPLE_FADE_IN, ParameterIDs::SYN_OSC_B_SAMPLE_FADE_OUT, ParameterIDs::SYN_OSC_B_SAMPLE_FADEIN_CURVE, ParameterIDs::SYN_OSC_B_SAMPLE_FADEOUT_CURVE },
            { ParameterIDs::SYN_OSC_C_GEODE_POSITION, ParameterIDs::SYN_OSC_C_GEODE_FOSSIL, ParameterIDs::SYN_OSC_C_GEODE_CREEP, ParameterIDs::SYN_OSC_C_GEODE_SILT, ParameterIDs::SYN_OSC_C_GEODE_FORMANT, ParameterIDs::SYN_OSC_C_GEODE_CUT, ParameterIDs::SYN_OSC_C_GEODE_SIEVE, ParameterIDs::SYN_OSC_C_GEODE_DISTILL, ParameterIDs::SYN_OSC_C_GEODE_HAZE, ParameterIDs::SYN_OSC_C_GEODE_FRACTURE, ParameterIDs::SYN_OSC_C_GEODE_TILT, ParameterIDs::SYN_OSC_C_GEODE_QUALITY, ParameterIDs::SYN_OSC_C_GEODE_FKEEP, ParameterIDs::SYN_OSC_C_GEODE_LOOP, ParameterIDs::SYN_OSC_C_GEODE_BEDROCK, ParameterIDs::SYN_OSC_C_GEODE_SHAPE_TARGET, ParameterIDs::SYN_OSC_C_GEODE_CUT_MODE, ParameterIDs::SYN_OSC_C_GEODE_DRIVE_MODE, ParameterIDs::SYN_OSC_C_GEODE_SIEVE_MODE , ParameterIDs::SYN_OSC_C_SAMPLE_START, ParameterIDs::SYN_OSC_C_SAMPLE_END, ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_START, ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_END, ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_MODE, ParameterIDs::SYN_OSC_C_SAMPLE_FADE_IN, ParameterIDs::SYN_OSC_C_SAMPLE_FADE_OUT, ParameterIDs::SYN_OSC_C_SAMPLE_FADEIN_CURVE, ParameterIDs::SYN_OSC_C_SAMPLE_FADEOUT_CURVE },
            { ParameterIDs::SYN_OSC_D_GEODE_POSITION, ParameterIDs::SYN_OSC_D_GEODE_FOSSIL, ParameterIDs::SYN_OSC_D_GEODE_CREEP, ParameterIDs::SYN_OSC_D_GEODE_SILT, ParameterIDs::SYN_OSC_D_GEODE_FORMANT, ParameterIDs::SYN_OSC_D_GEODE_CUT, ParameterIDs::SYN_OSC_D_GEODE_SIEVE, ParameterIDs::SYN_OSC_D_GEODE_DISTILL, ParameterIDs::SYN_OSC_D_GEODE_HAZE, ParameterIDs::SYN_OSC_D_GEODE_FRACTURE, ParameterIDs::SYN_OSC_D_GEODE_TILT, ParameterIDs::SYN_OSC_D_GEODE_QUALITY, ParameterIDs::SYN_OSC_D_GEODE_FKEEP, ParameterIDs::SYN_OSC_D_GEODE_LOOP, ParameterIDs::SYN_OSC_D_GEODE_BEDROCK, ParameterIDs::SYN_OSC_D_GEODE_SHAPE_TARGET, ParameterIDs::SYN_OSC_D_GEODE_CUT_MODE, ParameterIDs::SYN_OSC_D_GEODE_DRIVE_MODE, ParameterIDs::SYN_OSC_D_GEODE_SIEVE_MODE, ParameterIDs::SYN_OSC_D_SAMPLE_START, ParameterIDs::SYN_OSC_D_SAMPLE_END, ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_START, ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_END, ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_MODE, ParameterIDs::SYN_OSC_D_SAMPLE_FADE_IN, ParameterIDs::SYN_OSC_D_SAMPLE_FADE_OUT, ParameterIDs::SYN_OSC_D_SAMPLE_FADEIN_CURVE, ParameterIDs::SYN_OSC_D_SAMPLE_FADEOUT_CURVE }
        };
        tw::GeodeParams geodeP[4];
        for (int o = 0; o < 4; ++o)
        {
            const char* const* id = GEODE_IDS[o];
            tw::GeodeParams g;
            g.start   = *rawParam (id[0]);  g.stretch = *rawParam (id[1]);  g.scan    = *rawParam (id[2]);
            g.crush   = *rawParam (id[3]);  g.formant = *rawParam (id[4]);  g.cut     = *rawParam (id[5]);
            g.sieve   = *rawParam (id[6]);  g.shape   = *rawParam (id[7]);  g.drive   = *rawParam (id[8]);
            g.smear   = *rawParam (id[9]);  g.tilt    = *rawParam (id[10]); g.quality = *rawParam (id[11]);   // id[9] = FRACTURE repurposed as MELT
            g.formantKeep = *rawParam (id[12]) > 0.5f;
            /* id[13] GEODE_LOOP retired (kept for preset compat) · id[14] BEDROCK reserved */
            g.shapeTarget = (int) *rawParam (id[15]);   g.cutMode   = (int) *rawParam (id[16]);
            g.driveMode   = (int) *rawParam (id[17]);   g.sieveMode = (int) *rawParam (id[18]);
            // SAMPLER-PARITY region/loop/fades — SHARED with the Sample engine's params (idle while
            // this osc runs Resynth): one region UI, one preset story (rs7).
            g.regionStart = *rawParam (id[19]);  g.regionEnd    = *rawParam (id[20]);
            g.loopStart   = *rawParam (id[21]);  g.loopEnd      = *rawParam (id[22]);
            g.loopMode    = (int) *rawParam (id[23]);
            g.fadeIn      = *rawParam (id[24]);  g.fadeOut      = *rawParam (id[25]);
            g.fadeInCurve = *rawParam (id[26]);  g.fadeOutCurve = *rawParam (id[27]);
            // fb75 — RESYNTH knob mod (block-rate; struct fields are all 0..1)
            g.quality = ownM (g.quality, (int) wc::ModDest::GeoQualityA + o, 0.0f, 1.0f);
            g.formant = ownM (g.formant, (int) wc::ModDest::GeoFormantA + o, 0.0f, 1.0f);
            g.tilt    = ownM (g.tilt, (int) wc::ModDest::GeoTiltA + o, 0.0f, 1.0f);
            g.crush   = ownM (g.crush, (int) wc::ModDest::GeoCrushA + o, 0.0f, 1.0f);
            g.start   = ownM (g.start, (int) wc::ModDest::GeoStartA + o, 0.0f, 1.0f);
            g.smear   = ownM (g.smear, (int) wc::ModDest::GeoMeltA + o, 0.0f, 1.0f);
            g.scan    = ownM (g.scan, (int) wc::ModDest::GeoScanA + o, 0.0f, 1.0f);
            g.cut     = ownM (g.cut, (int) wc::ModDest::GeoCutA + o, 0.0f, 1.0f);
            g.shape   = ownM (g.shape, (int) wc::ModDest::GeoShapeA + o, 0.0f, 1.0f);
            g.stretch = ownM (g.stretch, (int) wc::ModDest::GeoStretchA + o, 0.0f, 1.0f);
            g.drive   = ownM (g.drive, (int) wc::ModDest::GeoDriveA + o, 0.0f, 1.0f);
            g.sieve   = ownM (g.sieve, (int) wc::ModDest::GeoSieveA + o, 0.0f, 1.0f);
            geodeP[o] = g;
        }
        // ── HARMONIC engine: gather the additive params per OSC (HARM-ENGINE-GATHER) ──
        static const char* const HARM_IDS[4][14] = {
            { ParameterIDs::SYN_OSC_A_HARM_MODE, ParameterIDs::SYN_OSC_A_HARM_SCULPT, ParameterIDs::SYN_OSC_A_HARM_HUE, ParameterIDs::SYN_OSC_A_HARM_COUNT, ParameterIDs::SYN_OSC_A_HARM_LEAN, ParameterIDs::SYN_OSC_A_HARM_FAN, ParameterIDs::SYN_OSC_A_HARM_GRIT, ParameterIDs::SYN_OSC_A_HARM_BRAID, ParameterIDs::SYN_OSC_A_HARM_CARVE, ParameterIDs::SYN_OSC_A_HARM_CHURN, ParameterIDs::SYN_OSC_A_HARM_ROOT, ParameterIDs::SYN_OSC_A_HARM_SHINE, ParameterIDs::SYN_OSC_A_HARM_WILT, ParameterIDs::SYN_OSC_A_HARM_FIZZ },
            { ParameterIDs::SYN_OSC_B_HARM_MODE, ParameterIDs::SYN_OSC_B_HARM_SCULPT, ParameterIDs::SYN_OSC_B_HARM_HUE, ParameterIDs::SYN_OSC_B_HARM_COUNT, ParameterIDs::SYN_OSC_B_HARM_LEAN, ParameterIDs::SYN_OSC_B_HARM_FAN, ParameterIDs::SYN_OSC_B_HARM_GRIT, ParameterIDs::SYN_OSC_B_HARM_BRAID, ParameterIDs::SYN_OSC_B_HARM_CARVE, ParameterIDs::SYN_OSC_B_HARM_CHURN, ParameterIDs::SYN_OSC_B_HARM_ROOT, ParameterIDs::SYN_OSC_B_HARM_SHINE, ParameterIDs::SYN_OSC_B_HARM_WILT, ParameterIDs::SYN_OSC_B_HARM_FIZZ },
            { ParameterIDs::SYN_OSC_C_HARM_MODE, ParameterIDs::SYN_OSC_C_HARM_SCULPT, ParameterIDs::SYN_OSC_C_HARM_HUE, ParameterIDs::SYN_OSC_C_HARM_COUNT, ParameterIDs::SYN_OSC_C_HARM_LEAN, ParameterIDs::SYN_OSC_C_HARM_FAN, ParameterIDs::SYN_OSC_C_HARM_GRIT, ParameterIDs::SYN_OSC_C_HARM_BRAID, ParameterIDs::SYN_OSC_C_HARM_CARVE, ParameterIDs::SYN_OSC_C_HARM_CHURN, ParameterIDs::SYN_OSC_C_HARM_ROOT, ParameterIDs::SYN_OSC_C_HARM_SHINE, ParameterIDs::SYN_OSC_C_HARM_WILT, ParameterIDs::SYN_OSC_C_HARM_FIZZ },
            { ParameterIDs::SYN_OSC_D_HARM_MODE, ParameterIDs::SYN_OSC_D_HARM_SCULPT, ParameterIDs::SYN_OSC_D_HARM_HUE, ParameterIDs::SYN_OSC_D_HARM_COUNT, ParameterIDs::SYN_OSC_D_HARM_LEAN, ParameterIDs::SYN_OSC_D_HARM_FAN, ParameterIDs::SYN_OSC_D_HARM_GRIT, ParameterIDs::SYN_OSC_D_HARM_BRAID, ParameterIDs::SYN_OSC_D_HARM_CARVE, ParameterIDs::SYN_OSC_D_HARM_CHURN, ParameterIDs::SYN_OSC_D_HARM_ROOT, ParameterIDs::SYN_OSC_D_HARM_SHINE, ParameterIDs::SYN_OSC_D_HARM_WILT, ParameterIDs::SYN_OSC_D_HARM_FIZZ }
        };
        tw::HarmParams harmP[4];
        for (int o = 0; o < 4; ++o)
        {
            const char* const* id = HARM_IDS[o];
            tw::HarmParams h;
            h.mainMode   = (int) *rawParam (id[0]);
            h.sculptMode = (int) *rawParam (id[1]);
            h.hue   = *rawParam (id[2]);  h.count = *rawParam (id[3]);  h.lean  = *rawParam (id[4]);
            h.fan   = *rawParam (id[5]);  h.grit  = *rawParam (id[6]);  h.braid = *rawParam (id[7]);
            h.carve = *rawParam (id[8]);  h.churn = *rawParam (id[9]);  h.root  = *rawParam (id[10]);
            h.shine = *rawParam (id[11]); h.wilt  = *rawParam (id[12]); h.forge = *rawParam (id[13]);
            // fb75 — HARMONIC knob mod (block-rate; all fields 0..1, knobs MORPH not switch)
            h.hue   = ownM (h.hue, (int) wc::ModDest::HarmHueA + o, 0.0f, 1.0f);
            h.count = ownM (h.count, (int) wc::ModDest::HarmCountA + o, 0.0f, 1.0f);
            h.lean  = ownM (h.lean, (int) wc::ModDest::HarmLeanA + o, 0.0f, 1.0f);
            h.fan   = ownM (h.fan, (int) wc::ModDest::HarmFanA + o, 0.0f, 1.0f);
            h.grit  = ownM (h.grit, (int) wc::ModDest::HarmGritA + o, 0.0f, 1.0f);
            h.braid = ownM (h.braid, (int) wc::ModDest::HarmBraidA + o, 0.0f, 1.0f);
            h.carve = ownM (h.carve, (int) wc::ModDest::HarmCarveA + o, 0.0f, 1.0f);
            h.churn = ownM (h.churn, (int) wc::ModDest::HarmChurnA + o, 0.0f, 1.0f);
            h.root  = ownM (h.root, (int) wc::ModDest::HarmRootA + o, 0.0f, 1.0f);
            h.shine = ownM (h.shine, (int) wc::ModDest::HarmShineA + o, 0.0f, 1.0f);
            h.wilt  = ownM (h.wilt, (int) wc::ModDest::HarmWiltA + o, 0.0f, 1.0f);
            h.forge = ownM (h.forge, (int) wc::ModDest::HarmFizzA + o, 0.0f, 1.0f);
            harmP[o] = h;
        }
        harmDisplayParams_[0] = harmP[0]; harmDisplayParams_[1] = harmP[1];   // HARM-VIZ — message-thread
        harmDisplayParams_[2] = harmP[2]; harmDisplayParams_[3] = harmP[3];   // display engines read these

        // ── MODAL engine: gather the physical-model params per OSC (MODAL-ENGINE-GATHER) ──
        static const char* const MODAL_IDS[4][13] = {
            { ParameterIDs::SYN_OSC_A_MODAL_FAMILY, ParameterIDs::SYN_OSC_A_MODAL_FORM, ParameterIDs::SYN_OSC_A_MODAL_SOURCE, ParameterIDs::SYN_OSC_A_MODAL_HARD, ParameterIDs::SYN_OSC_A_MODAL_POS, ParameterIDs::SYN_OSC_A_MODAL_DECAY, ParameterIDs::SYN_OSC_A_MODAL_MATERIAL, ParameterIDs::SYN_OSC_A_MODAL_BREATH, ParameterIDs::SYN_OSC_A_MODAL_STRETCH, ParameterIDs::SYN_OSC_A_MODAL_BLOOM, ParameterIDs::SYN_OSC_A_MODAL_HALO, ParameterIDs::SYN_OSC_A_MODAL_AGE, ParameterIDs::SYN_OSC_A_MODAL_BODY },
            { ParameterIDs::SYN_OSC_B_MODAL_FAMILY, ParameterIDs::SYN_OSC_B_MODAL_FORM, ParameterIDs::SYN_OSC_B_MODAL_SOURCE, ParameterIDs::SYN_OSC_B_MODAL_HARD, ParameterIDs::SYN_OSC_B_MODAL_POS, ParameterIDs::SYN_OSC_B_MODAL_DECAY, ParameterIDs::SYN_OSC_B_MODAL_MATERIAL, ParameterIDs::SYN_OSC_B_MODAL_BREATH, ParameterIDs::SYN_OSC_B_MODAL_STRETCH, ParameterIDs::SYN_OSC_B_MODAL_BLOOM, ParameterIDs::SYN_OSC_B_MODAL_HALO, ParameterIDs::SYN_OSC_B_MODAL_AGE, ParameterIDs::SYN_OSC_B_MODAL_BODY },
            { ParameterIDs::SYN_OSC_C_MODAL_FAMILY, ParameterIDs::SYN_OSC_C_MODAL_FORM, ParameterIDs::SYN_OSC_C_MODAL_SOURCE, ParameterIDs::SYN_OSC_C_MODAL_HARD, ParameterIDs::SYN_OSC_C_MODAL_POS, ParameterIDs::SYN_OSC_C_MODAL_DECAY, ParameterIDs::SYN_OSC_C_MODAL_MATERIAL, ParameterIDs::SYN_OSC_C_MODAL_BREATH, ParameterIDs::SYN_OSC_C_MODAL_STRETCH, ParameterIDs::SYN_OSC_C_MODAL_BLOOM, ParameterIDs::SYN_OSC_C_MODAL_HALO, ParameterIDs::SYN_OSC_C_MODAL_AGE, ParameterIDs::SYN_OSC_C_MODAL_BODY },
            { ParameterIDs::SYN_OSC_D_MODAL_FAMILY, ParameterIDs::SYN_OSC_D_MODAL_FORM, ParameterIDs::SYN_OSC_D_MODAL_SOURCE, ParameterIDs::SYN_OSC_D_MODAL_HARD, ParameterIDs::SYN_OSC_D_MODAL_POS, ParameterIDs::SYN_OSC_D_MODAL_DECAY, ParameterIDs::SYN_OSC_D_MODAL_MATERIAL, ParameterIDs::SYN_OSC_D_MODAL_BREATH, ParameterIDs::SYN_OSC_D_MODAL_STRETCH, ParameterIDs::SYN_OSC_D_MODAL_BLOOM, ParameterIDs::SYN_OSC_D_MODAL_HALO, ParameterIDs::SYN_OSC_D_MODAL_AGE, ParameterIDs::SYN_OSC_D_MODAL_BODY }
        };
        static const char* const MODAL_LOOP_IDS[4] = {
            ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_MODE, ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_MODE,
            ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_MODE, ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_MODE };
        // exciter loop region (the PURPLE BOX) — same per-osc sample loop-start/end the sample-view edits
        static const char* const MODAL_LOOPSTART_IDS[4] = {
            ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_START, ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_START,
            ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_START, ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_START };
        static const char* const MODAL_LOOPEND_IDS[4] = {
            ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_END, ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_END,
            ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_END, ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_END };
        tw::ModalParams modalP[4];
        for (int o = 0; o < 4; ++o)
        {
            const char* const* id = MODAL_IDS[o];
            tw::ModalParams m;
            m.family = (int) *rawParam (id[0]);
            m.form   = (int) *rawParam (id[1]);
            m.source = (int) *rawParam (id[2]);
            m.hard     = *rawParam (id[3]);  m.pos     = *rawParam (id[4]);  m.decay   = *rawParam (id[5]);
            m.material = *rawParam (id[6]);  m.breath  = *rawParam (id[7]);  m.stretch = *rawParam (id[8]);
            m.bloom    = *rawParam (id[9]);  m.halo    = *rawParam (id[10]); m.age     = *rawParam (id[11]);
            m.body     = *rawParam (id[12]);
            const int lm = (int) *rawParam (MODAL_LOOP_IDS[o]);   // exciter loop mode (from the samp-head loop header)
            m.loopMode = (lm >= 0 && lm <= 3) ? lm : 0;           // 0..3 One-Shot/Fwd/Rev/PingPong (Tailed=4 → One-Shot)
            m.loopStart = *rawParam (MODAL_LOOPSTART_IDS[o]);      // purple-box start → exciter loops CONFINED here
            m.loopEnd   = *rawParam (MODAL_LOOPEND_IDS[o]);        // purple-box end
            modalP[o] = m;
        }

        // ── BLEND MODES: gather the 4 warp slots × 4 oscs once (cross-osc FM/PD/AM/RM) ──
        static const char* const WSLOT_IDS[4][12] = {
            { ParameterIDs::SYN_OSC_A_WSLOT1_MODE, ParameterIDs::SYN_OSC_A_WSLOT1_SRC, ParameterIDs::SYN_OSC_A_WSLOT1_DEPTH, ParameterIDs::SYN_OSC_A_WSLOT2_MODE, ParameterIDs::SYN_OSC_A_WSLOT2_SRC, ParameterIDs::SYN_OSC_A_WSLOT2_DEPTH, ParameterIDs::SYN_OSC_A_WSLOT3_MODE, ParameterIDs::SYN_OSC_A_WSLOT3_SRC, ParameterIDs::SYN_OSC_A_WSLOT3_DEPTH, ParameterIDs::SYN_OSC_A_WSLOT4_MODE, ParameterIDs::SYN_OSC_A_WSLOT4_SRC, ParameterIDs::SYN_OSC_A_WSLOT4_DEPTH },
            { ParameterIDs::SYN_OSC_B_WSLOT1_MODE, ParameterIDs::SYN_OSC_B_WSLOT1_SRC, ParameterIDs::SYN_OSC_B_WSLOT1_DEPTH, ParameterIDs::SYN_OSC_B_WSLOT2_MODE, ParameterIDs::SYN_OSC_B_WSLOT2_SRC, ParameterIDs::SYN_OSC_B_WSLOT2_DEPTH, ParameterIDs::SYN_OSC_B_WSLOT3_MODE, ParameterIDs::SYN_OSC_B_WSLOT3_SRC, ParameterIDs::SYN_OSC_B_WSLOT3_DEPTH, ParameterIDs::SYN_OSC_B_WSLOT4_MODE, ParameterIDs::SYN_OSC_B_WSLOT4_SRC, ParameterIDs::SYN_OSC_B_WSLOT4_DEPTH },
            { ParameterIDs::SYN_OSC_C_WSLOT1_MODE, ParameterIDs::SYN_OSC_C_WSLOT1_SRC, ParameterIDs::SYN_OSC_C_WSLOT1_DEPTH, ParameterIDs::SYN_OSC_C_WSLOT2_MODE, ParameterIDs::SYN_OSC_C_WSLOT2_SRC, ParameterIDs::SYN_OSC_C_WSLOT2_DEPTH, ParameterIDs::SYN_OSC_C_WSLOT3_MODE, ParameterIDs::SYN_OSC_C_WSLOT3_SRC, ParameterIDs::SYN_OSC_C_WSLOT3_DEPTH, ParameterIDs::SYN_OSC_C_WSLOT4_MODE, ParameterIDs::SYN_OSC_C_WSLOT4_SRC, ParameterIDs::SYN_OSC_C_WSLOT4_DEPTH },
            { ParameterIDs::SYN_OSC_D_WSLOT1_MODE, ParameterIDs::SYN_OSC_D_WSLOT1_SRC, ParameterIDs::SYN_OSC_D_WSLOT1_DEPTH, ParameterIDs::SYN_OSC_D_WSLOT2_MODE, ParameterIDs::SYN_OSC_D_WSLOT2_SRC, ParameterIDs::SYN_OSC_D_WSLOT2_DEPTH, ParameterIDs::SYN_OSC_D_WSLOT3_MODE, ParameterIDs::SYN_OSC_D_WSLOT3_SRC, ParameterIDs::SYN_OSC_D_WSLOT3_DEPTH, ParameterIDs::SYN_OSC_D_WSLOT4_MODE, ParameterIDs::SYN_OSC_D_WSLOT4_SRC, ParameterIDs::SYN_OSC_D_WSLOT4_DEPTH }
        };
        struct BlendCfg { int mode; int src; float depth; };
        BlendCfg blendCfg[4][4];
        for (int o = 0; o < 4; ++o)
            for (int s = 0; s < 4; ++s)
            {
                const char* const* id = WSLOT_IDS[o];
                blendCfg[o][s] = { (int) *rawParam (id[s * 3 + 0]), (int) *rawParam (id[s * 3 + 1]), *rawParam (id[s * 3 + 2]) };
                blendCfg[o][s].depth = ownM (blendCfg[o][s].depth, (int) wc::ModDest::BlendDepthA1 + o * 4 + s, 0.0f, 1.0f);   // fb75 blend-slot depth mod · fb184 ownership
            }

        // PEROSC-PUSH — Sample sources are per-OSC now; pushed via setSampleSources below.

        // ── Batch 1 — assemble the synth modulation config from params + transport,
        //    then publish it to every voice. One LFO (L1, sine, free rate) and one
        //    default route L1 → Filter 1 cutoff (depth from LFO1_DEPTH) so the slice
        //    is audible the instant it loads. Shape/sync/extra LFOs + dests: Batch 2+.
        // (synModCfg hoisted to the outer processBlock scope above — FLOW ARP reads it post-scope.)
        {
            struct LfoP { const char* shape; const char* sync; const char* div; const char* rate; const char* depth; const char* phase; };
            static const LfoP lp[wc::NUM_LFOS] = {
                { ParameterIDs::LFO1_SHAPE, ParameterIDs::LFO1_SYNC, ParameterIDs::LFO1_DIV, ParameterIDs::LFO1_RATE, ParameterIDs::LFO1_DEPTH, ParameterIDs::LFO1_PHASE },
                { ParameterIDs::LFO2_SHAPE, ParameterIDs::LFO2_SYNC, ParameterIDs::LFO2_DIV, ParameterIDs::LFO2_RATE, ParameterIDs::LFO2_DEPTH, ParameterIDs::LFO2_PHASE },
                { ParameterIDs::LFO3_SHAPE, ParameterIDs::LFO3_SYNC, ParameterIDs::LFO3_DIV, ParameterIDs::LFO3_RATE, ParameterIDs::LFO3_DEPTH, ParameterIDs::LFO3_PHASE },
                { ParameterIDs::LFO4_SHAPE, ParameterIDs::LFO4_SYNC, ParameterIDs::LFO4_DIV, ParameterIDs::LFO4_RATE, ParameterIDs::LFO4_DEPTH, ParameterIDs::LFO4_PHASE },
                { ParameterIDs::LFO5_SHAPE, ParameterIDs::LFO5_SYNC, ParameterIDs::LFO5_DIV, ParameterIDs::LFO5_RATE, ParameterIDs::LFO5_DEPTH, ParameterIDs::LFO5_PHASE },
                { ParameterIDs::LFO6_SHAPE, ParameterIDs::LFO6_SYNC, ParameterIDs::LFO6_DIV, ParameterIDs::LFO6_RATE, ParameterIDs::LFO6_DEPTH, ParameterIDs::LFO6_PHASE },
                { ParameterIDs::LFO7_SHAPE, ParameterIDs::LFO7_SYNC, ParameterIDs::LFO7_DIV, ParameterIDs::LFO7_RATE, ParameterIDs::LFO7_DEPTH, ParameterIDs::LFO7_PHASE },
                { ParameterIDs::LFO8_SHAPE, ParameterIDs::LFO8_SYNC, ParameterIDs::LFO8_DIV, ParameterIDs::LFO8_RATE, ParameterIDs::LFO8_DEPTH, ParameterIDs::LFO8_PHASE },
                { ParameterIDs::LFO9_SHAPE, ParameterIDs::LFO9_SYNC, ParameterIDs::LFO9_DIV, ParameterIDs::LFO9_RATE, ParameterIDs::LFO9_DEPTH, ParameterIDs::LFO9_PHASE },
                { ParameterIDs::LFO10_SHAPE,ParameterIDs::LFO10_SYNC,ParameterIDs::LFO10_DIV,ParameterIDs::LFO10_RATE,ParameterIDs::LFO10_DEPTH,ParameterIDs::LFO10_PHASE },
            };
            int na = 0;
            for (int i = 0; i < wc::NUM_LFOS; ++i)
            {
                const int  sh = (int) *rawParam (lp[i].shape);
                const bool sy =       *rawParam (lp[i].sync) > 0.5f;
                const int  dv = (int) *rawParam (lp[i].div);
                synModCfg.lfos[i].shape       = (wc::LFOShape) juce::jlimit (0, (int) wc::LFOShape::NumShapes - 1, sh);
                synModCfg.lfos[i].sync        = sy;
                synModCfg.lfos[i].rateHz      = modP (lp[i].rate, *rawParam (lp[i].rate), (int) wc::ModDest::LfoRateBase + i);   // fb196 — env-on-RATE (Hz mode; sync stays grid-locked until the LFO arc)
                synModCfg.lfos[i].syncIdx     = juce::jlimit (0, wc::kNumSyncDivisions - 1, dv);
                synModCfg.lfos[i].phaseOffset = modP (lp[i].phase, *rawParam (lp[i].phase), (int) wc::ModDest::LfoPhaseBase + i);   // fb245 — env/LFO on PHASE (read-phase shift, block-rate; output slew smooths)
                {   // fb228 — L5 MOTION feeds the config (the Free-forcing is DEAD; RETRIG is the default)
                    const auto& mo = lfoMotionAudio_[i];
                    synModCfg.lfos[i].trigger   = mo.mn ? wc::LFOTrigger::Free                      // MONO: a Free pool is phase-locked = one shared LFO
                                                : (mo.tg == 2 ? wc::LFOTrigger::Env
                                                :  mo.tg == 1 ? wc::LFOTrigger::Trig
                                                :               wc::LFOTrigger::Free);
                    synModCfg.lfos[i].polarity  = (mo.pol == 1) ? wc::LFOPolarity::UniPlus
                                                : (mo.pol == 2) ? wc::LFOPolarity::UniMinus
                                                :                 wc::LFOPolarity::Bipolar;   // fb246 — Uni/Bi toggle (was force-locked Bipolar)
                    synModCfg.lfos[i].direction = mo.dir;
                    synModCfg.lfos[i].loopPt    = mo.lb;
                    synModCfg.lfos[i].riseMs    = mo.ri;
                    synModCfg.lfos[i].delayMs   = mo.de;
                    synModCfg.lfos[i].smoothMs  = mo.sm;
                    synModCfg.lfos[i].swing     = mo.sw;
                    synModCfg.lfos[i].tripDot   = mo.td;
                    synModCfg.lfos[i].reseed    = (mo.rs != 0);   // fb245 — per-note reseed toggle
                }
            }
            // Assignments come from the UI drag-matrix. Effective depth = per-route badge (the dest
            // "meter") × that LFO's DEPTH ring (per-LFO MASTER amount, default full so every LFO works
            // out of the box; turn it down to tame all of one LFO's routes, and it's itself modulatable
            // via LFO→LFO so you can put an LFO on an LFO's depth).
            {
                const juce::ScopedLock sl (synModLock);
                for (const auto& r : synModRoutes)
                {
                    if (na >= wc::MAX_ASSIGNMENTS) break;
                    if (r.src >= wc::kEnvSrcBase && r.src < wc::kEnvSrcBase + 32)   // fb178 — envelope source
                    {
                        synModCfg.assignments[na].source  = wc::envSourceFor (r.src - wc::kEnvSrcBase + 1);
                        synModCfg.assignments[na].dest    = (wc::ModDest) r.dest;
                        synModCfg.assignments[na].depth   = std::abs (r.depth);   // fb180 — envelope depth is MAGNITUDE (direction is always knob-is-the-peak; the inverted mode read as 'the sound comes back')
                        synModCfg.assignments[na].enabled = true;
                        ++na; continue;
                    }
                    if (r.src == wc::kVelSrc)   // fb260 — Velocity → per-voice source (signed depth, no LFO master)
                    {
                        synModCfg.assignments[na].source  = wc::ModSource::Velocity;
                        synModCfg.assignments[na].dest    = (wc::ModDest) r.dest;
                        synModCfg.assignments[na].depth   = r.depth;
                        synModCfg.assignments[na].enabled = true;
                        ++na; continue;
                    }
                    if (r.src < 0 || r.src >= wc::NUM_LFOS) continue;
                    const float master = *rawParam (lp[r.src].depth);
                    synModCfg.assignments[na].source  = (wc::ModSource) ((int) wc::ModSource::L1 + r.src);
                    synModCfg.assignments[na].dest    = (wc::ModDest) r.dest;
                    synModCfg.assignments[na].depth   = r.depth * master;
                    synModCfg.assignments[na].enabled = true;
                    ++na;
                }
            }
            // ── FLOW · DRIFT (mode 4): publish last block's 8 lane values to the voices, and while
            //    DRIFT is the active mode, inject default routes so it audibly modulates the wavetable
            //    timbre — DEPTH (FLOW_DRF_MORPH) scales it. (Full per-lane custom routing = mod-matrix phase.)
            for (int i = 0; i < 8; ++i) synModCfg.driftLanes[i] = driftLane_[i];
            // (FLOW mode 4 = ROUND ROBIN now — the old DRIFT default-route injection that wobbled
            //  the wavetables is retired; the drift lanes stay published as mod-matrix sources.)
            synModCfg.numAssignments = na;
        }
        synModBpm = currentBPM.load();   // (declared in outer scope — hoisted for FLOW ARP)

        // CPU: change-gates for the HEAVY pushes below. Idle blocks (no knob/BPM movement —
        // the overwhelming majority) skip the ModConfig copy + 10 LFO reconfigs and the 8
        // engine-param struct copies on all 96 voices. Any change re-broadcasts to the FULL
        // pool, so a voice can never render with stale config.
        const bool synCfgChanged = ! synCfgPushed_
                                   || synModBpm != lastSynModBpm_
                                   || ! modCfgEq (synModCfg, lastSynModCfg_);
        if (synCfgChanged) { lastSynModCfg_ = synModCfg; lastSynModBpm_ = synModBpm; synCfgPushed_ = true; }
        const bool engChanged = ! engParamsPushed_
                                || ! (spA == lastSpA_) || ! (spB == lastSpB_) || ! (spC == lastSpC_) || ! (spD == lastSpD_)
                                || ! (gpA == lastGpA_) || ! (gpB == lastGpB_) || ! (gpC == lastGpC_) || ! (gpD == lastGpD_);
        if (engChanged)
        {
            lastSpA_ = spA; lastSpB_ = spB; lastSpC_ = spC; lastSpD_ = spD;
            lastGpA_ = gpA; lastGpB_ = gpB; lastGpC_ = gpC; lastGpD_ = gpD;
            engParamsPushed_ = true;
        }

        // ── UNIVERSAL OSC BOXES — COARSE folds into the cents lane (±6400 c: one term,
        //    every engine); SUB params push straight to the voice lanes. Read once per block.
        const float coarseA = *rawParam (ParameterIDs::SYN_OSC_A_COARSE);
        const float coarseB = *rawParam (ParameterIDs::SYN_OSC_B_COARSE);
        const float coarseC = *rawParam (ParameterIDs::SYN_OSC_C_COARSE);
        const float coarseD = *rawParam (ParameterIDs::SYN_OSC_D_COARSE);
        const int   subRngA = juce::jlimit (0, 8, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_A_SUB_RANGE), (int) wc::ModDest::SubRangeA + 0, 0.0f, 8.0f))), subFrmA = juce::jlimit (0, 3, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_A_SUB_FORM), (int) wc::ModDest::SubFormA + 0, 0.0f, 3.0f)));   // fb78 — stepped sub octave/shape mod
        const int   subRngB = juce::jlimit (0, 8, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_B_SUB_RANGE), (int) wc::ModDest::SubRangeA + 1, 0.0f, 8.0f))), subFrmB = juce::jlimit (0, 3, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_B_SUB_FORM), (int) wc::ModDest::SubFormA + 1, 0.0f, 3.0f)));   // fb78 — stepped sub octave/shape mod
        const int   subRngC = juce::jlimit (0, 8, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_C_SUB_RANGE), (int) wc::ModDest::SubRangeA + 2, 0.0f, 8.0f))), subFrmC = juce::jlimit (0, 3, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_C_SUB_FORM), (int) wc::ModDest::SubFormA + 2, 0.0f, 3.0f)));   // fb78 — stepped sub octave/shape mod
        const int   subRngD = juce::jlimit (0, 8, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_D_SUB_RANGE), (int) wc::ModDest::SubRangeA + 3, 0.0f, 8.0f))), subFrmD = juce::jlimit (0, 3, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_D_SUB_FORM), (int) wc::ModDest::SubFormA + 3, 0.0f, 3.0f)));   // fb78 — stepped sub octave/shape mod
        const float subWgtA = *rawParam (ParameterIDs::SYN_OSC_A_SUB_WEIGHT), subHtA = *rawParam (ParameterIDs::SYN_OSC_A_SUB_HEAT);
        const float subWgtB = *rawParam (ParameterIDs::SYN_OSC_B_SUB_WEIGHT), subHtB = *rawParam (ParameterIDs::SYN_OSC_B_SUB_HEAT);
        const float subWgtC = *rawParam (ParameterIDs::SYN_OSC_C_SUB_WEIGHT), subHtC = *rawParam (ParameterIDs::SYN_OSC_C_SUB_HEAT);
        const float subWgtD = *rawParam (ParameterIDs::SYN_OSC_D_SUB_WEIGHT), subHtD = *rawParam (ParameterIDs::SYN_OSC_D_SUB_HEAT);
        // ── NOISE ENGINE reads ──
        // getRawParameterValue() for an AudioParameterChoice returns the INDEX (0..N-1) directly —
        // exactly like SYN_FILTER*_DRIVETYPE / _POLES / SYN_OSC_*_ENGINE are read below. The old
        // `lround(raw * 12)` (from a wrong CLAUDE.md note claiming raw is normalised) pushed indices
        // 2..12 to 24..144 → every type but White(0)/Pink(→12=SpaceWind) collapsed to the switch default (White).
        const bool  noiseOn    = *rawParam (ParameterIDs::SYN_NOISE_ON) > 0.5f;
        const int   noiseType  = (int) *rawParam (ParameterIDs::SYN_NOISE_TYPE);   // choice index 0..12
        const float noiseLevel = mdP (ParameterIDs::SYN_NOISE_LEVEL, wc::ModDest::NoiseLevel, 0.0f, 1.0f);
        const float noisePitch = mdP (ParameterIDs::SYN_NOISE_PITCH, wc::ModDest::NoiseScan, 0.0f, 1.0f);
        const float noisePan   = mdP (ParameterIDs::SYN_NOISE_PAN, wc::ModDest::NoisePan, 0.0f, 1.0f);
        const int   noisePlayMode = (int) *rawParam (ParameterIDs::SYN_NOISE_PLAYMODE);   // fb66 — 0 Random · 1 Envelope · 2 Free
        const float noiseWidth    = mdP (ParameterIDs::SYN_NOISE_WIDTH, wc::ModDest::NoiseWidth, 0.0f, 2.0f);   // fb69 — stereo width 0..2 (M/S)

        // fb66 — FREE play mode: a GLOBAL always-running tape playhead. Advanced once per block (even with
        // no notes) at the rate the voices read the loop, wrapped to length. Voices in Free mode resync to
        // this at block start (setNoiseFreePos) so every note reads the ONE shared tape; the waveform
        // follower reads the normalised copy. Audio + follower use the same value → perfectly consistent.
        bool noiseSampleLoaded = false;   // fb68 — the Free-mode mono carrier gate only engages with a real sample loaded
        {
            auto nb = noiseSampleBuffer_.load();
            const int nlen = (nb != nullptr) ? nb->getNumSamples() : 0;
            noiseSampleLoaded = (nlen > 1);
            if (nlen > 1)
            {
                const double nnr = noiseSampleBuffer_.getSampleRate();
                const double sr  = getSampleRate();
                const double nativeOverOut = (nnr > 0.0 && sr > 0.0) ? (nnr / sr) : 1.0;
                const float  sc   = juce::jlimit (0.0f, 1.0f, noisePitch);
                const double rate = (sc < 0.5f) ? (0.1 + 1.8 * (double) sc) : (1.0 + 2.0 * ((double) sc - 0.5));
                noiseFreePos_ += (double) numSamples * rate * nativeOverOut;
                while (noiseFreePos_ >= (double) nlen) noiseFreePos_ -= (double) nlen;
                if (noiseFreePos_ < 0.0) noiseFreePos_ = 0.0;
                noiseFreeNorm_.store ((float) (noiseFreePos_ / (double) nlen), std::memory_order_relaxed);
            }
            else { noiseFreePos_ = 0.0; noiseFreeNorm_.store (0.0f, std::memory_order_relaxed); }
        }

        // fb68 — Free mode is MONOPHONIC noise: stacked polyphonic tape copies comb/phase, so pick ONE carrier voice
        // (newest key-HELD active voice; fallback newest active so a release tail still sounds) and let only it add the
        // audible noise. Poly modes / no sample → every voice carries (no-op). A note started mid-block is promoted
        // next block (its voice starts muted at note-on and ramps in click-free).
        const bool monoNoise = (noisePlayMode == 2) && noiseSampleLoaded;
        tw::SynthVoice* noiseCarrierVoice = nullptr;
        if (monoNoise)
        {
            juce::uint32 bestHeld = 0, bestAny = 0;
            tw::SynthVoice* held = nullptr; tw::SynthVoice* anyv = nullptr;
            for (int i = 0; i < synthEngine.getNumVoices(); ++i)
                if (auto* sv = synthVoices_[(size_t) i])
                    if (sv->isAmpEnvActive())
                    {
                        const juce::uint32 st = sv->getNoteStartStamp();
                        if (anyv == nullptr || st >= bestAny) { bestAny = st; anyv = sv; }
                        if (sv->isKeyDown() && (held == nullptr || st >= bestHeld)) { bestHeld = st; held = sv; }
                    }
            noiseCarrierVoice = (held != nullptr) ? held : anyv;
        }

        // fb77 — BACK-PANEL TUNING MOD (Oct/Semi/Cent): the sums arrive in SEMITONES and fold
        // into the voice's CENTS lane next to COARSE. fb233 (Max) — THE OCTAVE SNAP LAW: the Oct
        // lane (±48 st = ±4 octaves at full depth) is clamped then ROUNDED to whole octaves before
        // the fold — an octave knob under an LFO JUMPS through octaves (square = octave gate,
        // triangle = staircase), it never reads as continuous detune. Semi/Cent stay continuous
        // (the vibrato lanes). Steps are phase-continuous frequency changes — clickless by nature.
        // fb131 — MODE CHAIN: resolve once for this scope's voice hooks (the flow stage
        // below re-resolves; both read the same params so the truth cannot diverge).
        const wc::FlowChainState flowChain = flowChainNow();
        float tuneModCents[4];
        for (int o = 0; o < 4; ++o)
        {
            const float octSt = 12.0f * std::round (juce::jlimit (-48.0f, 48.0f,
                                    modSums[(int) wc::ModDest::OctA + o]) * (1.0f / 12.0f));   // fb233 — snap to whole octaves, rail ±4
            tuneModCents[o] = (octSt
                             + modSums[(int) wc::ModDest::SemiA + o]
                             + modSums[(int) wc::ModDest::CentA + o]) * 100.0f;
        }
        if (flowChain.robin)                                                // fb122 ROBIN (fb131: chain-aware)
            for (int o = 0; o < 4; ++o) tuneModCents[o] += robinDriftCents_[o];   // per-station wander
        for (int i = 0; i < synthEngine.getNumVoices(); ++i)
        {
            if (auto* sv = synthVoices_[(size_t) i])   // typed array — no per-voice RTTI
            {
                if (synCfgChanged)
                    sv->setModConfig          (synModCfg, synModBpm);
                sv->setTuning                 (oct, semi, cent + coarseA * 100.0f + tuneModCents[0]);   // + COARSE + Oct/Semi/Cent mod (cents lane)
                sv->setLevel                  (lvl);
                sv->setPan                    (pan);
                sv->setFilterParameters       (cut, res);
                sv->setFilterKeytrack         (fltKt1 / 100.0f);
                sv->setFilterKeytrack2        (fltKt2 / 100.0f);
                sv->setFilterType             (filtType);
                sv->setFilterDrive            (filtDrv);
                sv->setFilterEnvAmount        (filtEnv);
                sv->setFltEnvDAHDSR           (fltDly, fltEnvA, fltHld, fltEnvD, fltEnvS, fltEnvR, fltCa, fltCd, fltCr, fltLoop);
                // fb177 — dynamic envelope pool: version-gated copy (once per change,
                // try-lock never blocks audio), then the per-voice broadcast.
                if (dynEnvSeen_ != dynEnvVersion_.load (std::memory_order_acquire))
                {
                    const juce::ScopedTryLock dsl (dynEnvLock_);
                    if (dsl.isLocked())
                    {
                        for (int k = 0; k < kMaxDynEnvs; ++k) dynEnvAudio_[k] = dynEnvShapes_[k];
                        dynEnvAudioCount_ = dynEnvCount_;
                        dynEnvSeen_ = dynEnvVersion_.load (std::memory_order_acquire);
                    }
                }
                sv->setDynEnvCount (dynEnvAudioCount_);
                for (int k = 0; k < dynEnvAudioCount_; ++k)
                {
                    const auto& de = dynEnvAudio_[k];
                    const int dBk = (int) wc::ModDest::EnvPBase + (5 + k) * 6;   // fb193 — Env (6+k) params
                    sv->setDynEnvDAHDSR (k, dynModMs (de.dl, dBk + 0), dynModMs (de.a, dBk + 1), dynModMs (de.h, dBk + 2),
                                         dynModMs (de.d, dBk + 3), dynModS (de.s, dBk + 4), dynModMs (de.r, dBk + 5),
                                         de.ca, de.cd, de.cr, de.loop);
                }
                sv->setFilterParameters2      (cut2, res2);
                sv->setFilterType2            (filtType2);
                sv->setFilterDrive2           (filtDrv2);
                sv->setFilterEnvAmount2       (filtEnv2);
                sv->setFilterMix1             (filtMix1);
                sv->setFilterMix2             (filtMix2);
                sv->setFilterRouting          (filtRoute);
                sv->setFilterSources          (f1src, f2src);
                sv->setNoiseFilterRouting     (noiseF1, noiseF2);   // fb63 — route the noise layer into F1/F2/dry
                sv->setFilterVelocity         (filtVel1, filtVel2);
                sv->setFilterPostDrive        (filtPdrv1, filtPdrv2);
                sv->setFilterDriveType        (filtDrvType1, filtDrvType2);
                sv->setFilterPoles            (filtPole1, filtPole2);
                sv->setFilterSpread           (filtSpread1, filtSpread2);
                sv->setAmpEnv                 (ampDly, ampA, ampHld, ampD, ampS, ampR, ampCa, ampCd, ampCr, ampLoop);
                sv->setPitchEnv               (pitDly, pitA, pitHld, pitD, pitS, pitR, pitCa, pitCd, pitCr, pitLoop);
                sv->setPitchEnvDepth          (pitDepth);
                sv->setMod1Env                (m1eDly, m1eA, m1eHld, m1eD, m1eS, m1eR, m1eCa, m1eCd, m1eCr, m1eLoop);
                sv->setMod2Env                (m2eDly, m2eA, m2eHld, m2eD, m2eS, m2eR, m2eCa, m2eCd, m2eCr, m2eLoop);
                sv->setEnvRouting             (env2Dest, env2Depth, env3Dest, env3Depth,
                                               env4Dest, env4Depth, env5Dest, env5Depth);
                sv->setWavetable              (wt);
                sv->setWavetableFrame         (wtFrame);
                sv->setWarp                   (warpMode, warpAmount);
                sv->setEngine                 (engineIdx);
                // Phase 9 — OSC B setters
                sv->setTuningB                (octB, semiB, centB + coarseB * 100.0f + tuneModCents[1]);
                sv->setLevelB                 (lvlB);
                sv->setPanB                   (panB);
                sv->setWavetableB             (wtB);
                sv->setWavetableFrameB        (wtFrameB);
                sv->setWarpB                  (warpModeB, warpAmountB);
                sv->setWarp2                  (warp2ModeA, warp2AmtA, warp2ModeB, warp2AmtB);   // WARP 2
                sv->setPhaseMode              (phaseModeA, phaseModeB);
                sv->setWaver                  (waverA / 100.0f, waverB / 100.0f);   // WAVER — analog pitch drift (OU)
                sv->setKeytrack               (ktDepthA / 100.0f, ktDestA, ktDepthB / 100.0f, ktDestB);  // KEYTRACK
                sv->setRoute                  (rtSrcA, rtDestA, rtAmtA / 100.0f, rtSrcB, rtDestB, rtAmtB / 100.0f);  // ROUTE
                sv->setEngineB                (engineIdxB);
                // ── OSC C / D pushes (4-osc) ──
                sv->setTuningC (octC, semiC, centC + coarseC * 100.0f + tuneModCents[2]);  sv->setTuningD (octD, semiD, centD + coarseD * 100.0f + tuneModCents[3]);
                sv->setLevelC (lvlC);                 sv->setLevelD (lvlD);
                sv->setOscGates (gateA, gateB, gateC, gateD);   // SOLO/MUTE — click-free per-osc gate
                sv->setSub (0, subRngA, subFrmA, subWgtA, subHtA);   // SUB — universal osc box
                sv->setSub (1, subRngB, subFrmB, subWgtB, subHtB);
                sv->setSub (2, subRngC, subFrmC, subWgtC, subHtC);
                sv->setSub (3, subRngD, subFrmD, subWgtD, subHtD);
                sv->setNoise (noiseOn, noiseType, noiseLevel, noisePitch, noisePan);   // NOISE engine (center module)
                sv->setNoiseSampleSource (&noiseSampleBuffer_);   // NOISE IMPORT (P5) — looping-sample override (empty buffer = algorithmic type)
                sv->setNoisePlayMode      (noisePlayMode);        // fb66 — Random / Envelope / Free (sample playback)
                sv->setNoiseFreePos       (noiseFreePos_);        // fb66/fb67 — latest global tape position (a Free note reads it once at note-on; no per-block resync)
                sv->setNoiseCarrier       (! monoNoise || (sv == noiseCarrierVoice));   // fb68 — Free = only the newest voice sounds the noise (mono); poly modes = all carry
                sv->setNoiseWidth         (noiseWidth);            // fb69 — noise stereo width (M/S)
                sv->setRobin (flowChain.robin, &flowRobin_,     // fb122: the Wheel brain (fb131: chain-aware)
                              gateA > 0.001f, gateB > 0.001f, gateC > 0.001f, gateD > 0.001f);
                flowRobin_.setAudible (gateA > 0.001f, gateB > 0.001f, gateC > 0.001f, gateD > 0.001f);
                sv->setFlowWave (arpWaveMod_);        // FLOW · ARP WAVE lane → wavetable frame offset (last block's value)
                sv->setPanC (panC);                   sv->setPanD (panD);
                sv->setWavetableC (wtC);              sv->setWavetableD (wtD);
                sv->setWavetableFrameC (wtFrameC);    sv->setWavetableFrameD (wtFrameD);
                sv->setWarpC (warpModeC, warpAmountC); sv->setWarpD (warpModeD, warpAmountD);
                sv->setWarp2CD (warp2ModeC, warp2AmtC, warp2ModeD, warp2AmtD);
                sv->setPhaseModeCD (phaseModeC, phaseModeD);
                sv->setWaverCD (waverC / 100.0f, waverD / 100.0f);
                sv->setKeytrackCD (ktDepthC / 100.0f, ktDestC, ktDepthD / 100.0f, ktDestD);
                sv->setRouteCD (rtSrcC, rtDestC, rtAmtC / 100.0f, rtSrcD, rtDestD, rtAmtD / 100.0f);
                sv->setEngineC (engineIdxC);          sv->setEngineD (engineIdxD);
                sv->setGeodeParamsA (geodeP[0]); sv->setGeodeParamsB (geodeP[1]);   // GEODE-ENGINE-PUSH (cheap stores, ungated)
                sv->setGeodeParamsC (geodeP[2]); sv->setGeodeParamsD (geodeP[3]);
                sv->setHarmParamsA (harmP[0]);   sv->setHarmParamsB (harmP[1]);   // HARM-ENGINE-PUSH (cheap stores, ungated)
                sv->setHarmParamsC (harmP[2]);   sv->setHarmParamsD (harmP[3]);
                sv->setModalParamsA (modalP[0]); sv->setModalParamsB (modalP[1]); // MODAL-ENGINE-PUSH
                sv->setModalParamsC (modalP[2]); sv->setModalParamsD (modalP[3]);
                for (int bo = 0; bo < 4; ++bo)                                     // BLEND-MODES-PUSH (cross-osc warp slots)
                    for (int bs = 0; bs < 4; ++bs)
                        sv->setBlendSlot (bo, bs, blendCfg[bo][bs].mode, blendCfg[bo][bs].src, blendCfg[bo][bs].depth);
                // ── SAMPLE engine: push params + shared buffer source (SAMPLE-ENGINE-PUSH) ──
                if (engChanged)
                {
                    sv->setSampleParamsA (spA);  sv->setSampleParamsB (spB);
                    sv->setSampleParamsC (spC);  sv->setSampleParamsD (spD);
                    sv->setGranParamsA (gpA);    sv->setGranParamsB (gpB);      // GRAIN-ENGINE-PUSH
                    sv->setGranParamsC (gpC);    sv->setGranParamsD (gpD);
                }
                for (int o = 0; o < 4; ++o)   // FM-ENGINE-PUSH — wavetable-carrier FM knobs (cheap stores; ungated)
                {
                    sv->setFMOsc  (o, (int) fmVals[o][0], fmVals[o][1], fmVals[o][2],
                                   fmVals[o][3], fmVals[o][4], fmVals[o][5]);
                    sv->setFMOsc2 (o, fmVals[o][6], fmVals[o][7], fmVals[o][8],   // WEATHERING page
                                   fmVals[o][9], fmVals[o][10], fmVals[o][11]);
                }
                sv->setSampleSources (&getOscSampleBuffer (0), &getOscSampleBuffer (1),
                                      &getOscSampleBuffer (2), &getOscSampleBuffer (3));   // PEROSC-PUSH
            }
        }

        // Phase 8b — Voice settings: UNISON+SPREAD pushed per-voice (in-voice unison).
        // The voice computes per-sine detune+pan internally and renders all sines as one note.
        const int   unisonCount = (int) *rawParam (ParameterIDs::SYN_UNISON);
        const float spreadPct   =       *rawParam (ParameterIDs::SYN_SPREAD);
        const float erosionPct  =       *rawParam (ParameterIDs::SYN_EROSION);
        const float horizonPct  =       *rawParam (ParameterIDs::SYN_HORIZON);
        const float unisonSpread01 = spreadPct / 100.0f;
        juce::ignoreUnused (unisonCount, unisonSpread01);   // global UNISON/SPREAD retired → per-OSC below

        // Per-OSC UNISON (replaces global). Voices 1..16 + Detune/Blend/Width (0..100 %→0..1).
        const int   uniCountA = juce::jlimit (1, 16, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_A_UNISON), (int) wc::ModDest::UniVoicesA + 0, 1.0f, 16.0f)));   // fb78 — stepped voices mod
        const float uniDetA   =       ownM (*rawParam (ParameterIDs::SYN_OSC_A_UDETUNE) / 100.0f, (int) wc::ModDest::UniDetA, 0.0f, 1.0f);     // fb77 — unison pill mod
        const float uniBlnA   =       ownM (*rawParam (ParameterIDs::SYN_OSC_A_UBLEND)  / 100.0f, (int) wc::ModDest::UniBlendA, 0.0f, 1.0f);
        const float uniWidA   =       ownM (*rawParam (ParameterIDs::SYN_OSC_A_UWIDTH)  / 100.0f, (int) wc::ModDest::UniWidthA, 0.0f, 1.0f);
        const int   uniCountB = juce::jlimit (1, 16, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_B_UNISON), (int) wc::ModDest::UniVoicesA + 1, 1.0f, 16.0f)));   // fb78 — stepped voices mod
        const float uniDetB   =       ownM (*rawParam (ParameterIDs::SYN_OSC_B_UDETUNE) / 100.0f, (int) wc::ModDest::UniDetB, 0.0f, 1.0f);
        const float uniBlnB   =       ownM (*rawParam (ParameterIDs::SYN_OSC_B_UBLEND)  / 100.0f, (int) wc::ModDest::UniBlendB, 0.0f, 1.0f);
        const float uniWidB   =       ownM (*rawParam (ParameterIDs::SYN_OSC_B_UWIDTH)  / 100.0f, (int) wc::ModDest::UniWidthB, 0.0f, 1.0f);

        // Phase 11a — per-OSC FRAME SPREAD (real DSP). Other 4 new params per OSC
        // (SPECTRAL_TYPE/AMT, FOLD_SHAPE/AMT, INTERP_MODE) persist via APVTS but
        // have no audio-thread effect yet — render path will start reading them
        // in Phase 11c (SPECTRAL) and 11d (FOLD).
        const float blurA = mdP (ParameterIDs::SYN_OSC_A_FRAME_SPREAD, wc::ModDest::BlurA, 0.0f, 1.0f);   // fb76 — blur is LFO-routable (bounded Gaussian band; same cost path as frame-LFO + static blur)
        const float blurB = mdP (ParameterIDs::SYN_OSC_B_FRAME_SPREAD, wc::ModDest::BlurB, 0.0f, 1.0f);

        // Phase 11d — FOLD per OSC.
        const int   foldShapeA  = (int) *rawParam (ParameterIDs::SYN_OSC_A_FOLD_SHAPE);
        const float foldAmtA    =       *rawParam (ParameterIDs::SYN_OSC_A_FOLD_AMT);
        const int   foldShapeB  = (int) *rawParam (ParameterIDs::SYN_OSC_B_FOLD_SHAPE);
        const float foldAmtB    =       *rawParam (ParameterIDs::SYN_OSC_B_FOLD_AMT);

        // Phase 11c — SPECTRAL MORPH per OSC is now applied to the wavetable spectrum
        // off the audio thread (see timerCallback / resolveMorphTable). The TYPE/AMT
        // params are read on the message thread; nothing to push per-voice here.

        // Phase 11g — INTERP per OSC.
        const int interpModeA = (int) *rawParam (ParameterIDs::SYN_OSC_A_INTERP_MODE);
        const int interpModeB = (int) *rawParam (ParameterIDs::SYN_OSC_B_INTERP_MODE);
        // OSC C / D — unison / blur / fold / interp (4-osc)
        const int   uniCountC=juce::jlimit (1, 16, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_C_UNISON), (int) wc::ModDest::UniVoicesA + 2, 1.0f, 16.0f)));   // fb78 — stepped voices mod
        const float uniDetC=ownM (*rawParam (ParameterIDs::SYN_OSC_C_UDETUNE)/100.0f, (int) wc::ModDest::UniDetC, 0.0f, 1.0f), uniBlnC=ownM (*rawParam (ParameterIDs::SYN_OSC_C_UBLEND)/100.0f, (int) wc::ModDest::UniBlendC, 0.0f, 1.0f), uniWidC=ownM (*rawParam (ParameterIDs::SYN_OSC_C_UWIDTH)/100.0f, (int) wc::ModDest::UniWidthC, 0.0f, 1.0f);
        const float blurC=mdP (ParameterIDs::SYN_OSC_C_FRAME_SPREAD, wc::ModDest::BlurC, 0.0f, 1.0f);
        const int   foldShapeC=(int)*rawParam (ParameterIDs::SYN_OSC_C_FOLD_SHAPE);
        const float foldAmtC=*rawParam (ParameterIDs::SYN_OSC_C_FOLD_AMT);
        const int   interpModeC=(int)*rawParam (ParameterIDs::SYN_OSC_C_INTERP_MODE);
        const int   uniCountD=juce::jlimit (1, 16, (int) std::lround (ownM (*rawParam (ParameterIDs::SYN_OSC_D_UNISON), (int) wc::ModDest::UniVoicesA + 3, 1.0f, 16.0f)));   // fb78 — stepped voices mod
        const float uniDetD=ownM (*rawParam (ParameterIDs::SYN_OSC_D_UDETUNE)/100.0f, (int) wc::ModDest::UniDetD, 0.0f, 1.0f), uniBlnD=ownM (*rawParam (ParameterIDs::SYN_OSC_D_UBLEND)/100.0f, (int) wc::ModDest::UniBlendD, 0.0f, 1.0f), uniWidD=ownM (*rawParam (ParameterIDs::SYN_OSC_D_UWIDTH)/100.0f, (int) wc::ModDest::UniWidthD, 0.0f, 1.0f);
        const float blurD=mdP (ParameterIDs::SYN_OSC_D_FRAME_SPREAD, wc::ModDest::BlurD, 0.0f, 1.0f);
        const int   foldShapeD=(int)*rawParam (ParameterIDs::SYN_OSC_D_FOLD_SHAPE);
        const float foldAmtD=*rawParam (ParameterIDs::SYN_OSC_D_FOLD_AMT);
        const int   interpModeD=(int)*rawParam (ParameterIDs::SYN_OSC_D_INTERP_MODE);

        // Phase 8b polish-3 — push VOICES knob into UnisonSynth as polyphony cap.
        // VOICES=8 → exactly 8 simultaneous, new notes steal oldest (Serum 2 behavior).
        const int voiceCap = (int) *rawParam (ParameterIDs::SYN_VOICES);
        synthEngine.setVoiceCap (voiceCap);

        // VOICING — MONO/LEGATO voice modes (last-note priority + legato retarget).
        const bool synMono   = (*rawParam (ParameterIDs::SYN_MONO))   > 0.5f;
        const bool synLegato = (*rawParam (ParameterIDs::SYN_LEGATO)) > 0.5f;
        synthEngine.setVoiceModes (synMono, synLegato);

        // VOICING / PORTAMENTO — glide context broadcast to every voice this block.
        const float portaPct  =       *rawParam (ParameterIDs::SYN_PORTA);
        const float glCurvePct =      *rawParam (ParameterIDs::SYN_GLIDE_CURVE);
        const bool  glAlways  = (*rawParam (ParameterIDs::SYN_GLIDE_ALWAYS)) > 0.5f;
        const bool  glScaled  = (*rawParam (ParameterIDs::SYN_GLIDE_SCALED)) > 0.5f;
        const float portaSec  = std::pow (portaPct * 0.01f, 2.0f) * 2.0f;   // squared → fine low end, ~2 s max
        const float glCurve01 = glCurvePct / 100.0f;
        const float velDepth01 = *rawParam (ParameterIDs::SYN_VEL_DEPTH) * 0.01f;   // fb260 — vel→amp depth 0..1
        const bool  glAnyHeld = synthNotesHeld_ > 0;
        for (int v = 0; v < synthEngine.getNumVoices(); ++v)
        {
            if (auto* tv = synthVoices_[(size_t) v])   // typed array — no per-voice RTTI
            {
                tv->setUnisonA (uniCountA, uniDetA, uniBlnA, uniWidA);   // per-OSC UNISON
                tv->setUnisonB (uniCountB, uniDetB, uniBlnB, uniWidB);
                tv->setBlur (blurA, blurB);   // WT BLUR (frame blend)
                tv->setFold (foldShapeA, foldAmtA, foldShapeB, foldAmtB);   // Phase 11d
                tv->setInterpMode (interpModeA, interpModeB);   // Phase 11g
                tv->setUnisonC (uniCountC, uniDetC, uniBlnC, uniWidC);  tv->setUnisonD (uniCountD, uniDetD, uniBlnD, uniWidD);
                tv->setBlurCD (blurC, blurD);
                tv->setFoldCD (foldShapeC, foldAmtC, foldShapeD, foldAmtD);
                tv->setInterpModeCD (interpModeC, interpModeD);
                tv->setGlide (portaSec, glCurve01, glAlways, glScaled, synthGlideFrom_, glAnyHeld);   // PORTAMENTO
                tv->setVelDepth (velDepth01);   // fb260 — velocity→amp depth
                tv->setHorizonAmount (horizonPct  / 100.0f);   // (merged third pass — CPU: one 96-voice loop fewer)
                // SYN_EROSION now drives the FILTER cutoff drift only — the per-voice
                // PITCH drift it used to add is superseded by per-OSC WAVER (setWaver above).
                tv->setErosionAmount_filter (erosionPct / 100.0f);
            }
        }

        // Track the last synth note + held count for glide. Updated AFTER the broadcast so
        // this block's note-ons glide from the PREVIOUS note (the origin), not themselves.
        for (const auto meta : midiMessages)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())       { synthGlideFrom_ = (float) m.getNoteNumber(); ++synthNotesHeld_; }
            else if (m.isNoteOff()) { synthNotesHeld_ = juce::jmax (0, synthNotesHeld_ - 1); }
        }
    }

    // ── FLOW transport + global LFO bank (block-rate mirror; free or transport-locked) ──
    double flowBpm = (double) synModBpm, flowPpq = 0.0; bool flowPlaying = false;
    // (fb137: flowPlayingViz_ stored right after the playhead read below)
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())         flowBpm     = *b;
            if (auto q = pos->getPpqPosition()) flowPpq     = *q;
            flowPlaying = pos->getIsPlaying();
        }
    // LFO ARC L1 — drawn-shape tables: copy shared→audio once per edit (try-lock never
    // blocks; a one-block-stale table is inaudible). Runs before any LFO advances this
    // block, so flowLfo_ and every voice read one coherent table set per callback.
    if (lfoShapeSeen_ != lfoShapeVersion_.load (std::memory_order_acquire))
    {
        const juce::ScopedTryLock lts (lfoShapeLock_);
        if (lts.isLocked())
        {
            std::memcpy (lfoTableAudio_, lfoTableShared_, sizeof (lfoTableAudio_));
            std::memcpy (lfoMotionAudio_, lfoMotionShared_, sizeof (lfoMotionAudio_));   // fb228 — motion rides the same gate
            std::memcpy (lfoPtAudio_,       lfoPtShared_,       sizeof (lfoPtAudio_));        // fb238 — the point lists ride too
            std::memcpy (lfoPtNpAudio_,     lfoPtNpShared_,     sizeof (lfoPtNpAudio_));
            std::memcpy (lfoPtHasModAudio_, lfoPtHasModShared_, sizeof (lfoPtHasModAudio_));
            for (int n2 = 0; n2 < wc::NUM_LFOS; ++n2) lfoPtDirty_[n2] = true;
            lfoShapeSeen_ = lfoShapeVersion_.load (std::memory_order_acquire);
        }
    }
    // fb238 — LIVE PER-POINT MODULATION: re-bake a modded LFO's audio table in place from the
    // current source LFO values (flowLfo peeks — end of last block, one block of lag, inaudible
    // at LFO rates). Voices and the mirror hold pointers into these rows (the setCustomTable
    // contract: content updates in place at block top), so the edit reaches every consumer with
    // zero copies. Gates: no mods = zero cost; not in Custom shape = skip; sources still = skip.
    for (int nP = 0; nP < wc::NUM_LFOS; ++nP)
    {
        if (! lfoPtHasModAudio_[nP]) continue;
        if (synModCfg.lfos[nP].shape != wc::LFOShape::Custom) continue;
        const int np = lfoPtNpAudio_[nP];
        if (np < 2) continue;
        float sv[wc::NUM_LFOS];
        for (int mS = 0; mS < wc::NUM_LFOS; ++mS) sv[mS] = flowLfo_[mS].peek();
        bool need = lfoPtDirty_[nP];
        for (int i2 = 0; i2 < np && ! need; ++i2)
        {
            const auto& q = lfoPtAudio_[nP][i2];
            if (q.xs > 0 && std::fabs (sv[q.xs - 1] - lfoPtSrcLast_[nP][q.xs - 1]) > 0.002f) need = true;
            if (q.ys > 0 && std::fabs (sv[q.ys - 1] - lfoPtSrcLast_[nP][q.ys - 1]) > 0.002f) need = true;
        }
        if (! need) continue;
        lfoPtDirty_[nP] = false;
        for (int mS = 0; mS < wc::NUM_LFOS; ++mS) lfoPtSrcLast_[nP][mS] = sv[mS];
        LfoShapePtM eff[160];
        for (int i2 = 0; i2 < np; ++i2)
        {
            eff[i2] = lfoPtAudio_[nP][i2];
            if (eff[i2].xs > 0 && i2 > 0 && i2 < np - 1)
                eff[i2].x = juce::jlimit (0.0f, 1.0f, eff[i2].x + eff[i2].xa * sv[eff[i2].xs - 1]);
            if (eff[i2].ys > 0)
                eff[i2].y = juce::jlimit (0.0f, 1.0f, eff[i2].y + eff[i2].ya * sv[eff[i2].ys - 1]);
        }
        std::sort (eff, eff + np, [] (const LfoShapePtM& a, const LfoShapePtM& b) { return a.x < b.x; });
        eff[0].x = 0.0f; eff[np - 1].x = 1.0f;
        bakeLfoShapeTable (eff, np, lfoTableAudio_[nP]);
    }
    // fb340 — LIVE PER-POINT CURVE MOD (the fb238 machinery on the distortion banks): re-bake the
    // user banks from the current source-LFO peeks. Same gates: no mods = zero cost; sources
    // still = skip. setUserCurves lands through the engine's 40 ms crossfade — mod becomes a
    // continuous curve morph, not steps.
    {
        const int dv = dstPtVersion_.load (std::memory_order_acquire);
        if (dv != dstPtSeen_)
        {
            std::memcpy (dstPtAudio_,   dstPtShared_,   sizeof (dstPtAudio_));
            std::memcpy (dstPtNpAudio_, dstPtNpShared_, sizeof (dstPtNpAudio_));
            dstPtHasModAudio_ = dstPtHasModShared_;
            dstPtDirty_ = true; dstPtSeen_ = dv;
        }
        if (dstPtHasModAudio_)
        {
            float sv[wc::NUM_LFOS];
            for (int mS = 0; mS < wc::NUM_LFOS; ++mS) sv[mS] = flowLfo_[mS].peek();
            bool need = dstPtDirty_;
            for (int bk = 0; bk < 4 && ! need; ++bk)
                for (int i2 = 0; i2 < dstPtNpAudio_[bk] && ! need; ++i2)
                {
                    const auto& q = dstPtAudio_[bk][i2];
                    if (q.ys > 0 && std::fabs (sv[q.ys - 1] - dstPtSrcLast_[q.ys - 1]) > 0.002f) need = true;
                    if (q.xs > 0 && std::fabs (sv[q.xs - 1] - dstPtSrcLast_[q.xs - 1]) > 0.002f) need = true;
                }
            if (need)
            {
                dstPtDirty_ = false;
                for (int mS = 0; mS < wc::NUM_LFOS; ++mS) dstPtSrcLast_[mS] = sv[mS];
                float bkArr[4][257]; bool hasB2[4];
                for (int bk = 0; bk < 4; ++bk)
                {
                    const int np2 = dstPtNpAudio_[bk];
                    hasB2[bk] = np2 >= 2;
                    if (! hasB2[bk]) continue;
                    LfoShapePtM eff2[32];
                    for (int i2 = 0; i2 < np2; ++i2)
                    {
                        eff2[i2] = dstPtAudio_[bk][i2];
                        if (eff2[i2].ys > 0)
                            eff2[i2].y = juce::jlimit (0.0f, 1.0f, eff2[i2].y + eff2[i2].ya * sv[eff2[i2].ys - 1]);
                        if (eff2[i2].xs > 0 && i2 > 0 && i2 < np2 - 1)   // §6.7 — x-mod unadvertised, kept able
                            eff2[i2].x = juce::jlimit (0.0f, 1.0f, eff2[i2].x + eff2[i2].xa * sv[eff2[i2].xs - 1]);
                    }
                    std::sort (eff2, eff2 + np2, [] (const LfoShapePtM& A2, const LfoShapePtM& B2) { return A2.x < B2.x; });
                    eff2[0].x = 0.0f; eff2[np2 - 1].x = 1.0f;
                    dstBakeEff (eff2, np2, bkArr[bk], 257);
                }
                distortionEngine.setUserCurves (hasB2[0] ? bkArr[0] : nullptr, hasB2[1] ? bkArr[1] : nullptr,
                                                hasB2[2] ? bkArr[2] : nullptr, hasB2[3] ? bkArr[3] : nullptr, 257);
            }
        }
    }
    for (int i = 0; i < wc::NUM_LFOS; ++i)
    {
        {   // fb228 — the voice/mirror CONTRACT: the mirror (global dests + the viz dot) NEVER retrigs
            //         and carries no per-note motion; voices honor the real trigger/rise/delay.
            auto ms = synModCfg.lfos[i];
            ms.trigger = wc::LFOTrigger::Free; ms.riseMs = 0.0f; ms.delayMs = 0.0f; ms.loopPt = -1.0f;
            flowLfo_[i].setSettings (ms);
        }
        if (synModCfg.lfos[i].sync && flowPlaying && lfoMotionAudio_[i].ho != 0
            && ! wc::isFreeRunShape (synModCfg.lfos[i].shape))   // fb239 — chaos/dune have no transport phase: always free-run at rate

        {
            const float bpc = wc::kSyncDivisions[ juce::jlimit (0, wc::kNumSyncDivisions - 1, synModCfg.lfos[i].syncIdx) ].beatsPerCycle;
            const double tdScale = (synModCfg.lfos[i].tripDot == 1 ? (2.0 / 3.0) : synModCfg.lfos[i].tripDot == 2 ? 1.5 : 1.0);   // fb228 — TRIP/DOT scale the bar cycle
            flowLfo_[i].setPhaseFromTransport ((float) std::fmod (flowPpq / ((double) bpc * tdScale), 1.0)); // locks to bar + arp clock
        }
        else
        {
            // fb78 ROOT-CAUSE FIX: a sync'd LFO with the TRANSPORT STOPPED used to pin its phase to the
            // frozen ppq (= 0 in most idle hosts) → shapeAt(0) = 0 forever → every route it fed was DEAD
            // ("none of them work", Max — he sound-designs with the transport stopped). Now a stopped
            // sync'd LFO FREE-RUNS at its tempo-derived rate — exactly what the per-voice LFOs already
            // do (setModConfig resolves syncedHz and free-runs) — and re-locks to the bar on play.
            const float hz = synModCfg.lfos[i].sync
                               ? wc::syncedHz (synModCfg.lfos[i].syncIdx, synModBpm > 0.0f ? synModBpm : 120.0f)
                               : synModCfg.lfos[i].rateHz;
            flowLfo_[i].setFrequency (hz);
            for (int s = 0; s < numSamples; ++s) flowLfo_[i].processSample();   // advance to track time
        }
    }

    // Synth renders into its own scratch (broadcast midiMessages unfiltered —
    // the trigger-mode dispatcher above gates the LAYERS only, the synth
    // is a parallel pipeline that always receives the host's MIDI).
    if (synthScratch.getNumSamples() < numSamples)
        synthScratch.setSize (2, numSamples, false, true, true);
    synthScratch.clear();

    // ── fb280 — PER-OSC NO-BLEED REVERB SEND: read the six route pills, hand each SynthVoice the
    // shared send bus + 0/1 gains, and clear the bus. During render below the voices accumulate ONLY
    // the routed oscillators into it (post-level/pan/amp-env); the master loop reverbs it and adds the
    // wet back. Routes off ⇒ null target ⇒ voices skip the tap ⇒ zero cost + zero change to the mix.
    if (reverbSendBuf_.getNumSamples() < numSamples)
        reverbSendBuf_.setSize (2, numSamples, false, true, true);
    reverbSendBuf_.clear (0, numSamples);
    hallRvbG_[0] = rawParam (ParameterIDs::SYN_RVB_SRC_A)->load()     > 0.5f ? 1.0f : 0.0f;
    hallRvbG_[1] = rawParam (ParameterIDs::SYN_RVB_SRC_B)->load()     > 0.5f ? 1.0f : 0.0f;
    hallRvbG_[2] = rawParam (ParameterIDs::SYN_RVB_SRC_C)->load()     > 0.5f ? 1.0f : 0.0f;
    hallRvbG_[3] = rawParam (ParameterIDs::SYN_RVB_SRC_D)->load()     > 0.5f ? 1.0f : 0.0f;
    hallRvbG_[4] = rawParam (ParameterIDs::SYN_RVB_SRC_SUB)->load()   > 0.5f ? 1.0f : 0.0f;
    hallRvbG_[5] = rawParam (ParameterIDs::SYN_RVB_SRC_NOISE)->load() > 0.5f ? 1.0f : 0.0f;
    // fb287 — POWER GATES EVERYTHING (Max): the device power pill OFF fully bypasses the reverb AND
    // disables its per-osc routing — zero the send gains so nothing passes regardless of the A/B/C/D/S/N
    // pills (the routing OBEYS the power button; you never turn it off by clearing already-grayed routes).
    hallPower_ = rawParam (ParameterIDs::SYN_RVB_POWER)->load() > 0.5f;
    if (! hallPower_)
        for (int k = 0; k < 6; ++k) hallRvbG_[k] = 0.0f;
    hallRouteActive_ = (hallRvbG_[0] + hallRvbG_[1] + hallRvbG_[2] + hallRvbG_[3] + hallRvbG_[4] + hallRvbG_[5]) > 0.0f;
    hallMainSend_ = hallPower_ && ! hallRouteActive_;   // fb303 — power on + NO pills ⇒ MAIN SEND (whole mix, serial insert)
    // fb296 — DELAY per-osc route resolution (independent mask + power gate), parallel to the reverb send.
    if (delaySendBuf_.getNumSamples() < numSamples)
        delaySendBuf_.setSize (2, numSamples, false, true, true);
    delaySendBuf_.clear (0, numSamples);
    dlyG_[0] = rawParam (ParameterIDs::SYN_DLY_SRC_A)->load()     > 0.5f ? 1.0f : 0.0f;
    dlyG_[1] = rawParam (ParameterIDs::SYN_DLY_SRC_B)->load()     > 0.5f ? 1.0f : 0.0f;
    dlyG_[2] = rawParam (ParameterIDs::SYN_DLY_SRC_C)->load()     > 0.5f ? 1.0f : 0.0f;
    dlyG_[3] = rawParam (ParameterIDs::SYN_DLY_SRC_D)->load()     > 0.5f ? 1.0f : 0.0f;
    dlyG_[4] = rawParam (ParameterIDs::SYN_DLY_SRC_SUB)->load()   > 0.5f ? 1.0f : 0.0f;
    dlyG_[5] = rawParam (ParameterIDs::SYN_DLY_SRC_NOISE)->load() > 0.5f ? 1.0f : 0.0f;
    dlyPower_ = rawParam (ParameterIDs::SYN_DLY_POWER)->load() > 0.5f;
    if (! dlyPower_)          // power gates routing (same law as reverb)
        for (int k = 0; k < 6; ++k) dlyG_[k] = 0.0f;
    dlyRouteActive_ = (dlyG_[0] + dlyG_[1] + dlyG_[2] + dlyG_[3] + dlyG_[4] + dlyG_[5]) > 0.0f;
    dlyMainSend_ = dlyPower_ && ! dlyRouteActive_;   // fb303 — power on + NO pills ⇒ MAIN SEND (whole mix, serial insert)
    fxPerm_ = juce::jlimit (0, 5, (int) rawParam (ParameterIDs::SYN_FX_ORDER)->load());   // fb341 — choice INDEX (the AudioParameterChoice law: raw = index)
    // fb315/fb338 — DISTORTION: power gates everything (same law as reverb/delay); per-osc route
    // resolution = the third parallel send bus (mirrors the delay's exactly).
    dstPower_ = rawParam (ParameterIDs::SYN_DST_POWER)->load() > 0.5f;
    if (distortionSendBuf_.getNumSamples() < numSamples)
        distortionSendBuf_.setSize (2, numSamples, false, true, true);
    distortionSendBuf_.clear (0, numSamples);
    dstG_[0] = rawParam (ParameterIDs::SYN_DST_SRC_A)->load()     > 0.5f ? 1.0f : 0.0f;
    dstG_[1] = rawParam (ParameterIDs::SYN_DST_SRC_B)->load()     > 0.5f ? 1.0f : 0.0f;
    dstG_[2] = rawParam (ParameterIDs::SYN_DST_SRC_C)->load()     > 0.5f ? 1.0f : 0.0f;
    dstG_[3] = rawParam (ParameterIDs::SYN_DST_SRC_D)->load()     > 0.5f ? 1.0f : 0.0f;
    dstG_[4] = rawParam (ParameterIDs::SYN_DST_SRC_SUB)->load()   > 0.5f ? 1.0f : 0.0f;
    dstG_[5] = rawParam (ParameterIDs::SYN_DST_SRC_NOISE)->load() > 0.5f ? 1.0f : 0.0f;
    if (! dstPower_)          // power gates routing (same law as reverb/delay)
        for (int k = 0; k < 6; ++k) dstG_[k] = 0.0f;
    dstRouteActive_ = (dstG_[0] + dstG_[1] + dstG_[2] + dstG_[3] + dstG_[4] + dstG_[5]) > 0.0f;
    dstMainSend_ = dstPower_ && ! dstRouteActive_;   // fb303 grammar — power on + NO pills ⇒ MAIN SEND
    {
        float* rsL = hallRouteActive_ ? reverbSendBuf_.getWritePointer (0) : nullptr;
        float* rsR = hallRouteActive_ ? reverbSendBuf_.getWritePointer (1) : nullptr;
        float* dsL = dlyRouteActive_  ? delaySendBuf_.getWritePointer (0) : nullptr;
        float* dsR = dlyRouteActive_  ? delaySendBuf_.getWritePointer (1) : nullptr;
        for (int vi = 0; vi < kSynthVoiceCount; ++vi)
            if (auto* sv = synthVoices_[(size_t) vi])
            {
                sv->setReverbRoutes (hallRvbG_[0], hallRvbG_[1], hallRvbG_[2], hallRvbG_[3], hallRvbG_[4], hallRvbG_[5]);
                sv->setReverbSendTarget (rsL, rsR);
                sv->setDelayRoutes (dlyG_[0], dlyG_[1], dlyG_[2], dlyG_[3], dlyG_[4], dlyG_[5]);
                sv->setDelaySendTarget (dsL, dsR);
                sv->setDistortionRoutes (dstG_[0], dstG_[1], dstG_[2], dstG_[3], dstG_[4], dstG_[5]);
                sv->setDistortionSendTarget (dstRouteActive_ ? distortionSendBuf_.getWritePointer (0) : nullptr,
                                             dstRouteActive_ ? distortionSendBuf_.getWritePointer (1) : nullptr);
            }
    }

    // GEODE — the partial budget is a PER-BLOCK quota (partials are re-rendered every block,
    // unlike grains which persist and retire). Reset it to 0 before the voices render, or it
    // grows unbounded and clamps every SPEC voice to 0 active partials (static → silence).
    geodePartialsLive_ = 0;

    // ── FLOW · ARP / SEQ: transform incoming MIDI (0=Off, 1=Arp, 2=Seq; 3/4 Glitch/Drift not built) ──
    // fb131 — MODE CHAIN: re-resolve at the flow stage (the voice scope above owns its own
    // copy). Multiple modes run at once now — the chop/glitch audio inserts process in CHAIN
    // ORDER below; arp/robin act at the note stage wherever they sit in the chain.
    const wc::FlowChainState flowChain = flowChainNow();
    flowPlayingViz_.store (flowPlaying ? 1 : 0, std::memory_order_relaxed);   // fb137 — feeds carry "pl"
    flowRobin_.setActive (flowChain.robin);   // fb122 — the Wheel brain follows the chain

    // FLOW · GLITCH (mode 3): reset the engine on the ENABLE EDGE so its step clock re-anchors to
    // the live transport ppq instead of resuming from a stale free-run phase (fired late / "whenever",
    // worse the longer the editor was closed). reset() clears haveClock_/nextStep_/freePpq_ and the
    // next process() re-anchors to hostPpq. Default-identical: only fires when GLITCH is (re)selected.
    if (flowChain.glitch && ! prevGlitchOn_)
        glitch.reset();
    prevGlitchOn_ = flowChain.glitch;

    // EFFECTIVE FLOW knobs = base param + Σ(global-LFO × depth), clamp once. Shared by ARP + SEQ —
    // SEQ reuses the same FLOW_ARP_* params (per-mode memory lives in the JS).  (proven: 35/35)
    // fb245 — LFO→LFO amt for FLOW/card knobs (same no-op the global path had: flowMod read the raw peek).
    // Built from the resolved assignments (they already fold the source LFO's master into as.depth).
    float flowLfoAmt[wc::NUM_LFOS] = { 0.0f };
    for (int a = 0; a < synModCfg.numAssignments; ++a) {
        const auto& as = synModCfg.assignments[a];
        if (! as.enabled) continue;
        const int dI = (int) as.dest;
        if (dI < (int) wc::ModDest::LfoAmt1 || dI >= (int) wc::ModDest::LfoAmt1 + wc::NUM_LFOS) continue;
        const int sI = (int) as.source;
        if (sI >= (int) wc::ModSource::L1 && sI < (int) wc::ModSource::L1 + wc::NUM_LFOS)
            flowLfoAmt[dI - (int) wc::ModDest::LfoAmt1] += flowLfo_[sI - (int) wc::ModSource::L1].peek() * as.depth;
        else if (wc::isEnvModSource (sI))
            flowLfoAmt[dI - (int) wc::ModDest::LfoAmt1] += monoEnvLevelOf (sI) * as.depth;
    }
    auto flowBase = [&] (const char* id) { return juce::jlimit (0.0f, 1.0f, rawParam (id)->load()); };  // ->load(): atomic<float> can't deduce in jlimit
    auto flowMod  = [&] (wc::ModDest dest, float& oW, float& oV) -> float {
        float sum = 0.0f; const auto& info = wc::kDestInfo[(int) dest];
        for (int a = 0; a < synModCfg.numAssignments; ++a) {
            const auto& as = synModCfg.assignments[a];
            if (! as.enabled || as.dest != dest) continue;
            const int si = (int) as.source - (int) wc::ModSource::L1;
            if (wc::isEnvModSource ((int) as.source))             // fb184 — envs OWN flow/card knobs (mono tap value, ownership math)
            { const float dwF = std::abs (as.depth);
              oW += dwF; oV += dwF * (monoEnvLevelOf ((int) as.source) + 1.0f); continue; }
            if (si < 0 || si >= wc::NUM_LFOS) continue;           // only LFO sources have a global value
            sum += wc::routeContribution (info, flowLfo_[si].peek() * juce::jlimit (0.0f, 2.0f, 1.0f + flowLfoAmt[si]), as.depth);   // fb245 — LfoAmt scales flow/card knobs too
        }
        return sum; };
    // Each mode reads its OWN 5 knob params (per-mode knob memory: ARP=FLOW_ARP_*, SEQ=FLOW_SEQ_*).
    // The FLOW mod-dests are shared, so the LFO→FLOW-knob contribution is added on top of whichever
    // mode's base value. Latch is a single shared param (no per-mode latch exists).
    auto flowKnob = [&] (const char* id, wc::ModDest d) {
        float oW = 0.0f, oV = 0.0f; const float m = flowMod (d, oW, oV);
        const float w = juce::jmin (1.0f, oW);   // fb184 — ownership crossfade, same law as ownM
        return juce::jlimit (0.f, 1.f, (flowBase (id) + m) * (1.0f - w) + oV); };
    const bool  kLatch = *rawParam (ParameterIDs::FLOW_ARP_LATCH) > 0.5f;

    if (flowChain.arp)   // ── ARP (mode 1) ──
    {
        const float kRate  = flowKnob (ParameterIDs::FLOW_ARP_RATE,  wc::ModDest::FlowTime);
        const float kGate  = flowKnob (ParameterIDs::FLOW_ARP_GATE,  wc::ModDest::FlowGate);
        const float kVary  = flowKnob (ParameterIDs::FLOW_ARP_VARY,  wc::ModDest::FlowVary);
        const float kTraj  = flowKnob (ParameterIDs::FLOW_ARP_TRAJ,  wc::ModDest::FlowTraj);
        const float kMorph = flowKnob (ParameterIDs::FLOW_ARP_MORPH, wc::ModDest::FlowMorph);

        // ARP BLEND (glass menu): 1.0 = pure arp (normal). Pull down to also hear the dry held
        // chord sustaining under the arp — the held note-ons pass through at vel×(1-blend), the arp
        // events play at vel×blend (velocity crossfade through the shared poly synth). Changing the
        // blend affects newly-struck notes (MIDI velocity is set at note-on).
        const float kBlend  = flowKnob (ParameterIDs::FLOW_ARP_BLEND, wc::ModDest::FlowArpMix);
        const float dryGain = 1.0f - kBlend;

        // ── extension card (fb105): lane pattern copy-on-change + the 35 card scalars ──
        {
            const int lv = arpLanesVersion_.load (std::memory_order_acquire);
            if (lv != arpLanesSeen_)
            {
                const juce::ScopedLock sl (arpLaneLock_);
                flowArp.setLanes (arpLanesShared_);
                arpLanesSeen_ = lv;
            }
            wc::ArpExtParams X;
            X.dir     = (int) rawParam (ParameterIDs::FLOW_ARP_DIR)->load();       // choice = INDEX
            X.octaves = 1 + (int) rawParam (ParameterIDs::FLOW_ARP_OCTR)->load();
            X.sorted  = rawParam (ParameterIDs::FLOW_ARP_SORTED)->load() > 0.5f;
            X.swing  = flowKnob (ParameterIDs::FLOW_ARP_SWING, wc::ModDest::FlowArpSwing);  X.mroll  = flowKnob (ParameterIDs::FLOW_ARP_MROLL, wc::ModDest::FlowArpRoll);
            X.timbre = flowKnob (ParameterIDs::FLOW_ARP_TIMBRE, wc::ModDest::FlowArpTimbre); X.glide  = flowKnob (ParameterIDs::FLOW_ARP_GLIDE, wc::ModDest::FlowArpGlide);
            X.pRange = flowKnob (ParameterIDs::FLOW_ARP_P_RANGE, wc::ModDest::FlowArpPRange); X.pCurve = flowKnob (ParameterIDs::FLOW_ARP_P_CURVE, wc::ModDest::FlowArpPCurve);
            X.pQuant = flowKnob (ParameterIDs::FLOW_ARP_P_QUANT, wc::ModDest::FlowArpPQuant); X.pSlide = flowKnob (ParameterIDs::FLOW_ARP_P_SLIDE, wc::ModDest::FlowArpPSlide);
            X.gLen   = flowKnob (ParameterIDs::FLOW_ARP_G_LEN, wc::ModDest::FlowArpGLen);   X.gCurve = flowKnob (ParameterIDs::FLOW_ARP_G_CURVE, wc::ModDest::FlowArpGCurve);
            X.gRand  = flowKnob (ParameterIDs::FLOW_ARP_G_RAND, wc::ModDest::FlowArpGRand);  X.gSlide = flowKnob (ParameterIDs::FLOW_ARP_G_SLIDE, wc::ModDest::FlowArpGSlide);
            X.vRange = flowKnob (ParameterIDs::FLOW_ARP_V_RANGE, wc::ModDest::FlowArpVRange); X.vCurve = flowKnob (ParameterIDs::FLOW_ARP_V_CURVE, wc::ModDest::FlowArpVCurve);
            X.vRand  = flowKnob (ParameterIDs::FLOW_ARP_V_RAND, wc::ModDest::FlowArpVRand);  X.vFloor = flowKnob (ParameterIDs::FLOW_ARP_V_FLOOR, wc::ModDest::FlowArpVFloor);
            X.oRange = flowKnob (ParameterIDs::FLOW_ARP_O_RANGE, wc::ModDest::FlowArpORange); X.oBias  = flowKnob (ParameterIDs::FLOW_ARP_O_BIAS, wc::ModDest::FlowArpOBias);
            X.oRand  = flowKnob (ParameterIDs::FLOW_ARP_O_RAND, wc::ModDest::FlowArpORand);  X.oSpread= flowKnob (ParameterIDs::FLOW_ARP_O_SPREAD, wc::ModDest::FlowArpOSpread);
            X.rCount = flowKnob (ParameterIDs::FLOW_ARP_R_COUNT, wc::ModDest::FlowArpRCount); X.rDecay = flowKnob (ParameterIDs::FLOW_ARP_R_DECAY, wc::ModDest::FlowArpRDecay);
            X.rCurve = flowKnob (ParameterIDs::FLOW_ARP_R_CURVE, wc::ModDest::FlowArpRCurve); X.rAmt   = flowKnob (ParameterIDs::FLOW_ARP_R_AMT, wc::ModDest::FlowArpRAmt);
            X.cAmt   = flowKnob (ParameterIDs::FLOW_ARP_C_AMT, wc::ModDest::FlowArpCAmt);   X.cBias  = flowKnob (ParameterIDs::FLOW_ARP_C_BIAS, wc::ModDest::FlowArpCBias);
            X.cSeed  = flowKnob (ParameterIDs::FLOW_ARP_C_SEED, wc::ModDest::FlowArpCSeed);  X.cDrift = flowKnob (ParameterIDs::FLOW_ARP_C_DRIFT, wc::ModDest::FlowArpCDrift);
            X.wDepth = flowKnob (ParameterIDs::FLOW_ARP_W_DEPTH, wc::ModDest::FlowArpWDepth); X.wCurve = flowKnob (ParameterIDs::FLOW_ARP_W_CURVE, wc::ModDest::FlowArpWCurve);
            X.wSlide = flowKnob (ParameterIDs::FLOW_ARP_W_SLIDE, wc::ModDest::FlowArpWSlide); X.wRand  = flowKnob (ParameterIDs::FLOW_ARP_W_RAND, wc::ModDest::FlowArpWRand);
            flowArp.setExt (X);
        }
        flowArp.setLatch (kLatch);
        juce::MidiBuffer flowMidi;
        for (const auto meta : midiMessages)
        {
            const auto m = meta.getMessage();
            if      (m.isNoteOn())
            {
                flowArp.noteOn (m.getNoteNumber(), m.getVelocity());
                const int dv = (int) std::lround (m.getVelocity() * dryGain);   // dry held chord (un-arped)
                if (dv >= 1) flowMidi.addEvent (juce::MidiMessage::noteOn (1, m.getNoteNumber(), (juce::uint8) juce::jlimit (1, 127, dv)), meta.samplePosition);
            }
            else if (m.isNoteOff())
            {
                flowArp.noteOff (m.getNoteNumber());
                flowMidi.addEvent (juce::MidiMessage::noteOff (1, m.getNoteNumber()), meta.samplePosition);   // release any sustained dry note
            }
            else                    flowMidi.addEvent (m, meta.samplePosition);   // CC / pitchbend pass through
        }

        wc::ArpEvent ev[wc::kArpMaxEvents];
        const int n = flowArp.process (kRate, kGate, kVary, kTraj, kMorph,
                                       flowPpq, flowBpm, getSampleRate(), numSamples,
                                       flowPlaying, ev, wc::kArpMaxEvents);
        for (int i = 0; i < n; ++i)
        {
            if (ev[i].on)
            {
                const int av = (int) std::lround (ev[i].vel * kBlend);   // arp stream scaled by blend
                if (av >= 1) flowMidi.addEvent (juce::MidiMessage::noteOn (1, ev[i].note, (juce::uint8) juce::jlimit (1, 127, av)), juce::jlimit (0, numSamples - 1, ev[i].sampleOffset));
            }
            else
                flowMidi.addEvent (juce::MidiMessage::noteOff (1, ev[i].note), juce::jlimit (0, numSamples - 1, ev[i].sampleOffset));
        }
        synthEngine.renderNextBlock (synthScratch, flowMidi, 0, numSamples);

        // publish the live playhead/fire feed (UI rAF-polls getArpFeed) + the WAVE
        // lane's frame-offset for the voices (consumed NEXT block — drift-lane pattern)
        arpVizStepF_.store  (flowArp.vizStepF(),            std::memory_order_relaxed);
        arpVizCount_.store  ((int) flowArp.vizFireCount(),  std::memory_order_relaxed);
        arpVizNote_.store   (flowArp.vizNote(),             std::memory_order_relaxed);
        arpVizVel_.store    (flowArp.vizVel(),              std::memory_order_relaxed);
        arpVizActive_.store (flowArp.vizActive() ? 1 : 0,   std::memory_order_relaxed);
        arpWaveMod_ = flowArp.waveMod();
    }
    else
    {
        arpVizActive_.store (0, std::memory_order_relaxed);
        arpWaveMod_ = 0.0f;
        // ARP inactive (Off, or CHOP/GLITCH = end-of-block audio inserts, or DRIFT = mod source):
        // release any note the arp was holding so it can't hang, then pass raw MIDI straight through.
        wc::ArpEvent arel[wc::kArpMaxEvents]; const int an = flowArp.releaseAll (arel, wc::kArpMaxEvents);
        if (an > 0)
        {
            juce::MidiBuffer mixed;
            mixed.addEvents (midiMessages, 0, numSamples, 0);
            for (int i = 0; i < an; ++i) mixed.addEvent (juce::MidiMessage::noteOff (1, arel[i].note), 0);
            synthEngine.renderNextBlock (synthScratch, mixed, 0, numSamples);
        }
        else
            synthEngine.renderNextBlock (synthScratch, midiMessages, 0, numSamples);
    }

    // ── Envelope follower tap ──
    // After the block renders, sample the most-active synth voice's AMP envelope
    // output for the UI playhead dot. "Most active" = the highest live level, so a
    // newly struck note's follower wins over a tail that's releasing. When nothing
    // sounds we send -1 and JS parks/hides the follower.
    {
        float best = 0.f; float bestFollow = -1.f; float bestLfo = 0.f; bool any = false;
        tw::SynthVoice* bestVoice = nullptr;   // most-active voice (env dot + scope pick)
        // SAMPLE-FOLLOWER (multi) — gathered in this SAME voice pass: every sounding voice's per-osc
        // read position, keyed by voice index i (stable identity → smooth fade on release in the UI),
        // capped at kMaxFollowers, so the editor draws one fading white playhead per held note.
        int cnt[4] = { 0, 0, 0, 0 };
        for (int i = 0; i < synthEngine.getNumVoices(); ++i)
            if (auto* sv = synthVoices_[(size_t) i])   // typed array — no per-voice RTTI
                if (sv->isAmpEnvActive())
                {
                    const float lv = sv->getAmpEnvLevel();
                    if (!any || lv > best) { best = lv; bestFollow = sv->getAmpEnvFollow(); bestLfo = sv->getSynthLfoVis(); bestVoice = sv; any = true; }
                    for (int o = 0; o < 4; ++o)
                        if (cnt[o] < kMaxFollowers)
                        {
                            float p = sv->sampleFollowPos01 (o);
                            if (p < 0.f) p = sv->granScanPos01 (o);   // GRANULAR: the voice's scan head IS its follower (drawn purple in the UI)
                            if (p < 0.f) p = sv->geodeFollowPos01 (o); // RESYNTH: the geode read-head IS its follower
                            if (p < 0.f) p = sv->modalFollowPos01 (o); // MODAL: the exciter read-head IS its follower (same mechanism)
                            if (p >= 0.f) { sampleFollowIdx_[o][cnt[o]].store (i, std::memory_order_relaxed);
                                            sampleFollowPos_[o][cnt[o]].store (p, std::memory_order_relaxed); ++cnt[o]; }
                        }
                }
        ampEnvVis.store       (any ? best       : -1.f, std::memory_order_relaxed);
        velVis_.store         ((any && bestVoice != nullptr) ? bestVoice->getCurrentVelocity() : -1.f, std::memory_order_relaxed);   // fb262 — live velocity streak feed (most-active voice)
        // fb189 — the living underline's feed: every env slot from the most-active voice
        // (envSourceValue returns level−1 → +1 restores raw 0..1) + the global LFO peeks.
        for (int k = 0; k < 32; ++k)
            modVizEnv_[k].store ((any && bestVoice != nullptr)
                                     ? bestVoice->envSourceValue ((int) wc::envSourceFor (k + 1)) + 1.0f
                                     : -1.f, std::memory_order_relaxed);
        for (int k = 0; k < wc::NUM_LFOS; ++k)
        {
            modVizLfo_[k].store (flowLfo_[k].peek(), std::memory_order_relaxed);
            modVizLfoVX_[k].store (flowLfo_[k].chaosVX(), std::memory_order_relaxed);   // fb239 — the swirl feed
            modVizLfoVY_[k].store (flowLfo_[k].chaosVY(), std::memory_order_relaxed);
            {   // fb231 — RETRIG/ENV made VISIBLE: a non-Free LFO's dot rides the most-active VOICE's phase
                //         (resets per note, pins at Env end); Free/mono keep the mirror. (fb228 contract intact.)
                const bool vTrig = synModCfg.lfos[k].trigger != wc::LFOTrigger::Free;
                modVizLfoPh_[k].store ((vTrig && any && bestVoice != nullptr)
                                           ? bestVoice->lfoPhase (k)
                                           : flowLfo_[k].currentPhase(), std::memory_order_relaxed);   // fb217 — real phase for the follower
            }
        }
        noiseVizLevel_.store  ((*rawParam (ParameterIDs::SYN_NOISE_ON) > 0.5f && any) ? juce::jmax (0.f, best) : 0.f, std::memory_order_relaxed);   // NOISE viz trigger
        // fb66 — waveform follower position. Free → the global tape head (visible even when idle);
        // Random/Envelope → the loudest sounding voice's read head; -1 = nothing to draw.
        noiseVizPos_.store ((((int) *rawParam (ParameterIDs::SYN_NOISE_PLAYMODE)) == 2) ? noiseFreeNorm_.load (std::memory_order_relaxed)
                                                 : (bestVoice != nullptr ? bestVoice->noiseFollowPos01() : -1.0f),
                            std::memory_order_relaxed);
        ampEnvFollowVis.store (any ? bestFollow  : -1.f, std::memory_order_relaxed);
        // Batch 1 — most-active voice's L1 value drives the live LFO dot (0 when idle).
        synthLfo1Vis.store    (any ? bestLfo     :  0.f, std::memory_order_relaxed);
        // fb163 — LIVE FILTER CURVE: same loudest-voice pick feeds the filter display.
        fltVisHz1_.store  (any && bestVoice != nullptr ? bestVoice->getFltVisHz1()  : -1.f, std::memory_order_relaxed);
        fltVisRes1_.store (any && bestVoice != nullptr ? bestVoice->getFltVisRes1() :  0.f, std::memory_order_relaxed);
        fltVisHz2_.store  (any && bestVoice != nullptr ? bestVoice->getFltVisHz2()  : -1.f, std::memory_order_relaxed);
        fltVisRes2_.store (any && bestVoice != nullptr ? bestVoice->getFltVisRes2() :  0.f, std::memory_order_relaxed);
        for (int o = 0; o < 4; ++o) sampleFollowCount_[o].store (cnt[o], std::memory_order_relaxed);   // count LAST = coherent list
        oscScopePubAccum_ += (double) numSamples;
        if (! vizLive) oscScopePubAccum_ = 0.0;   // fb148 — no backlog while closed (a grown accumulator would publish every block on reopen)
        const bool oscDoPub = vizLive && (oscScopePubAccum_ >= getSampleRate() / 60.0);
        if (oscDoPub) oscScopePubAccum_ -= getSampleRate() / 60.0;
        // HARM-VIZ (hm2) — the most-active voice's LIVE partial bank feeds the white bars,
        // so every key press moves the display (the params-only bake is the idle fallback).
        // 60 Hz-gated (hm4): the editor samples at 60 Hz; publishing every block was waste.
        if (oscDoPub)
            for (int o = 0; o < 4; ++o)
            {
                float hvb[96];
                const bool hvLive = any && bestVoice != nullptr && bestVoice->harmLiveBins (o, hvb, 96);
                if (hvLive) for (int b = 0; b < 96; ++b) harmVizBins_[o][b].store (hvb[b], std::memory_order_relaxed);
                harmVizLive_[o].store (hvLive ? 1 : 0, std::memory_order_relaxed);
            }

        // GRANULAR-FOLLOWER — retired 2026-07-02. The grain-dot scatter cloud is gone; granular
        // now rides the SAME multi-playhead follower system as the Sample engine (aggregated above via
        // granScanPos01), drawn PURPLE in the UI. One purple line per sounding voice = the new visual.

        // ── OSC SCOPE — publish the SUM of ALL sounding voices' 4 osc windows ──
        // Still on the AUDIO thread, right after renderNextBlock: every active voice's
        // rings were written this same block on this same thread, so copyScopeWindow
        // reads them with no sync. The "visual shaper" shows the REAL combined waveform
        // — 1 note = a clean shape, 2+ notes = the held-voice interference pattern
        // (matching the Notes reference). We accumulate each active voice's per-osc
        // window into a stack scratch (no heap), RMS-normalize by 1/sqrt(nActive) so
        // display height stays ~constant regardless of voice count (1 voice → ×1 =
        // byte-identical to the old single-voice publish), then store into the
        // lock-free oscScope atomics (same discipline as scopeBuffer) and bump the seq
        // so the editor knows a fresh frame is ready. No note sounding -> active=false
        // and JS smooths the display down to a flatline.
        // RT-SAFE: stack temps only (acc[1024]+tmp[1024] = 8 KB), plain float add/mul,
        // no heap/lock/IO on the audio thread.
        // PERF: the per-voice sum is O(4·active-voices·1024); the editor only consumes at 60 Hz,
        // so gate the whole publish to ~60 Hz instead of block-rate (~750 Hz) → ~12× less
        // audio-thread work with zero visual cost (the editor reads the last published frame).
        // Output-ring RMS every publish tick (256 relaxed loads, trivial): drives the
        // TAIL-MODE gate below and the VIZDBG overlay's "is audio actually audible" truth.
        float tailRing[SCOPE_SIZE];
        float outRms = 0.0f;
        if (oscDoPub)
        {
            const int wp = scopeWritePos.load (std::memory_order_relaxed);   // next write = oldest sample
            double e = 0.0;
            for (int s = 0; s < SCOPE_SIZE; ++s)
            {
                tailRing[s] = scopeBuffer[(size_t) ((wp + s) % SCOPE_SIZE)].load (std::memory_order_relaxed);
                e += (double) tailRing[s] * (double) tailRing[s];
            }
            outRms = (float) std::sqrt (e / (double) SCOPE_SIZE);
            oscScopeORms.store (outRms, std::memory_order_relaxed);
        }
        if (oscDoPub && bestVoice != nullptr)
        {
            // Count the voices we will sum (same iteration discipline as bestVoice above).
            int nActive = 0;
            for (int i = 0; i < synthEngine.getNumVoices(); ++i)
                if (auto* sv = synthVoices_[(size_t) i])   // typed array — no per-voice RTTI
                    if (sv->isAmpEnvActive())
                        ++nActive;
            if (nActive < 1) nActive = 1;
            const float norm = 1.0f / std::sqrt ((float) nActive);   // RMS-style height hold

            // SPSC seqlock WRITE: bracket the window stores with odd→even so the editor
            // can detect a torn snapshot and retry (see PluginEditor timerCallback).
            float acc[OSC_SCOPE_SIZE];
            float tmp[OSC_SCOPE_SIZE];
            int badWin = 0;   // non-finite samples sanitized this publish (→ oscScopeBad → overlay F:PUSH-POISON)
            oscScopeSeq.fetch_add (1, std::memory_order_release);   // → odd: window write begins
            for (int o = 0; o < 4; ++o)
            {
                for (int s = 0; s < OSC_SCOPE_SIZE; ++s) acc[s] = 0.0f;
                for (int i = 0; i < synthEngine.getNumVoices(); ++i)
                    if (auto* sv = synthVoices_[(size_t) i])   // typed array — no per-voice RTTI
                        if (sv->isAmpEnvActive())
                        {
                            sv->copyScopeWindow (o, tmp, OSC_SCOPE_SIZE);
                            for (int s = 0; s < OSC_SCOPE_SIZE; ++s) acc[s] += tmp[s];
                        }
                float wpk = 0.0f;
                for (int s = 0; s < OSC_SCOPE_SIZE; ++s)
                {
                    float v = acc[s] * norm;
                    // WINDOW SANITIZE (wd9) — a non-finite sample would SF()-serialize as 0
                    // (silent flat); a huge-finite one crushes the JS auto-gain for seconds.
                    // Zero the former (counted → overlay names it), clamp the latter.
                    if (! std::isfinite (v)) { v = 0.0f; ++badWin; }
                    else if (v > 8.0f)  v = 8.0f;
                    else if (v < -8.0f) v = -8.0f;
                    const float a = v < 0.0f ? -v : v;
                    if (a > wpk) wpk = a;
                    oscScope[(size_t) o][(size_t) s].store (v, std::memory_order_relaxed);
                }
                oscScopeWpk[(size_t) o].store (wpk, std::memory_order_relaxed);
                // VIZDBG — best voice's per-osc render state rides along with the window
                int eng = 0, au = 0, mip = 0; float d1e = 0.0f, un = 0.0f;
                bestVoice->getVizDiag (o, eng, au, mip, d1e, un);
                oscScopeEng[(size_t) o].store (eng, std::memory_order_relaxed);
                oscScopeAu[(size_t) o].store  (au,  std::memory_order_relaxed);
                oscScopeMip[(size_t) o].store (mip, std::memory_order_relaxed);
                oscScopeD1e[(size_t) o].store (d1e, std::memory_order_relaxed);
                oscScopeUn[(size_t) o].store  (un,  std::memory_order_relaxed);
                oscScopeGt[(size_t) o].store  (bestVoice->oscGateTargetVal (o), std::memory_order_relaxed);
            }
            // Anchor the display period on the loudest/most-active voice's fundamental.
            // (sanitized — a non-finite/negative hz would corrupt the JS period math)
            float pubHz = bestVoice->getFundamentalHz();
            if (! std::isfinite (pubHz) || pubHz < 0.0f) pubHz = 0.0f;
            oscScopeHz.store     (pubHz,                         std::memory_order_relaxed);
            oscScopeSr.store     ((float) getSampleRate(),       std::memory_order_relaxed);
            oscScopeBad.store    (badWin,                        std::memory_order_relaxed);
            oscScopeSeq.fetch_add (1, std::memory_order_release);   // → even: window complete
            oscScopeNv.store     (nActive, std::memory_order_relaxed);
            oscScopeLv.store     (best,    std::memory_order_relaxed);
            oscScopeTail.store   (false,   std::memory_order_relaxed);
            oscScopeActive.store (true,    std::memory_order_relaxed);
            oscScopeTailGate_ = false;
        }
        else if (oscDoPub)
        {
            // ── TAIL MODE — no voice is amp-active, but the OUTPUT may still ring
            // (grain delay / tape loop / FX feedback ring for MINUTES). Max's invariant:
            // audio playing ⇒ the scopes see it. Publish the MASTER OUTPUT ring (post-FX,
            // post-clipper — literally what the ear hears) as the window for all four
            // oscs, hz=0 → the JS shaper spans the whole window (slow drifting wisps).
            // Hysteresis so the gate doesn't chatter at the threshold. 1-block latency
            // (the ring holds last block's master) — invisible at 60 Hz.
            if (outRms > 0.0015f) oscScopeTailGate_ = true;        // ~-56 dBFS on
            else if (outRms < 0.0008f) oscScopeTailGate_ = false;  // ~-62 dBFS off
            if (oscScopeTailGate_)
            {
                oscScopeSeq.fetch_add (1, std::memory_order_release);
                for (int s = 0; s < OSC_SCOPE_SIZE; ++s)
                {
                    const float fp = (float) s * (float) (SCOPE_SIZE - 1) / (float) (OSC_SCOPE_SIZE - 1);
                    const int   j  = (int) fp;
                    const float fr = fp - (float) j;
                    const float v  = tailRing[j] + (tailRing[juce::jmin (SCOPE_SIZE - 1, j + 1)] - tailRing[j]) * fr;
                    for (int o = 0; o < 4; ++o)
                        oscScope[(size_t) o][(size_t) s].store (v, std::memory_order_relaxed);
                }
                oscScopeHz.store (0.0f, std::memory_order_relaxed);   // no pitch anchor — whole-window span
                oscScopeSr.store ((float) getSampleRate(), std::memory_order_relaxed);
                oscScopeSeq.fetch_add (1, std::memory_order_release);
                oscScopeNv.store   (0,     std::memory_order_relaxed);
                oscScopeLv.store   (0.0f,  std::memory_order_relaxed);
                oscScopeTail.store (true,  std::memory_order_relaxed);
                oscScopeActive.store (true, std::memory_order_relaxed);
            }
            else
            {
                oscScopeNv.store   (0,     std::memory_order_relaxed);
                oscScopeLv.store   (0.0f,  std::memory_order_relaxed);
                oscScopeTail.store (false, std::memory_order_relaxed);
                oscScopeActive.store (false, std::memory_order_relaxed);
            }
        }
    }


    // Sum synth into master buffer (flows through the master FX chain below).
    for (int ch = 0; ch < buffer.getNumChannels() && ch < 2; ++ch)
        buffer.addFrom (ch, 0, synthScratch, ch, 0, numSamples);

    // -6 dB pad on the voice mix before the FX chain. The FX modules
    // (TapeProcessor saturation, SpaceReverb feedback paths, etc.) were
    // tuned for plugin-input levels around -12 to -6 dBFS — typical track
    // signal coming from a DAW. Voice output at velocity=1 + envLevel=1
    // is a unity-gain (0 dBFS) signal, ~6 dB hotter than the FX expects,
    // which causes audible distortion at "subtle" tape settings. This pad
    // brings the gain staging into the FX's design window.
    constexpr float kVoiceToFxPad = 0.5f; // -6 dB
    buffer.applyGain (kVoiceToFxPad);

    // ── ANNULUS RESONATOR — on the SYNTH-SECTION output (PRE-FX). ───────────────
    //    Moved here from end-of-block (2026-07-01, per Max). Reasons:
    //      • The front-panel FX (delay / reverb / grain / tape) now process the
    //        resonator's OUTPUT — so you actually hear delay/reverb ON the resonated
    //        sound instead of the resonator swallowing them at the master.
    //      • It RELEASES with the notes: it's fed the amp-enveloped voice mix, which
    //        goes silent on note-off, so the ring decays (Damping) — instead of being
    //        fed forever by the FX tails at the end of the chain (that end-of-chain
    //        feed was exactly why it "never released").
    //    Bypassed (exact passthrough) at Mix 0, so it costs nothing until dialed in.
    //    Params via flowKnob() → modulatable. NOTE for Opus: fold this SYNTH-SECTION
    //    placement (not the master end) into the next resonator drop.
    {
        const float rStruct = flowKnob (ParameterIDs::SYN_RESO_STRUCTURE,  wc::ModDest::ResoStructure);
        const float rBright = flowKnob (ParameterIDs::SYN_RESO_BRIGHTNESS, wc::ModDest::ResoBrightness);
        const float rDamp   = flowKnob (ParameterIDs::SYN_RESO_DAMPING,    wc::ModDest::ResoDamping);
        const float rPos    = flowKnob (ParameterIDs::SYN_RESO_POSITION,   wc::ModDest::ResoPosition);
        const float rMix    = flowKnob (ParameterIDs::SYN_RESO_MIX,        wc::ModDest::ResoMix);
        const float rKey    = flowBase (ParameterIDs::SYN_RESO_KEYTRACK);
        const int   rMat    = (int) (rawParam (ParameterIDs::SYN_RESO_MATERIAL)->load() + 0.5f);
        float* rl = buffer.getWritePointer (0);
        float* rr = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : rl;
        reso.process (rStruct, rBright, rDamp, rPos, rMat, rMix, rKey,
                      resoHeld_, resoHeldN_, getSampleRate(),
                      rl, rr, numSamples);
        for (int b = 0; b < 4; ++b) resoVizEnergy_[b].store (reso.vizEnergy[b], std::memory_order_relaxed);
        resoVizOut_.store (reso.vizOut, std::memory_order_relaxed);
    }


    // Read BPM from DAW playhead
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                currentBPM.store(static_cast<float>(*bpm));
        }
    }

    // Update smoothing targets from APVTS
    float grainSizeMs = rawParam (ParameterIDs::GRAIN_SIZE)->load();

    // BPM sync: override grain size with note-division-based ms
    if (grainSyncEnabled.load() > 0.5f)
    {
        static constexpr float divisionMultipliers[10] = {
            0.125f, 0.1667f, 0.25f, 0.3333f, 0.5f,
            0.6667f, 1.0f, 1.3333f, 2.0f, 4.0f
        };

        // Map the knob's normalized 0-1 value to one of 10 divisions
        auto* param = apvts.getParameter(ParameterIDs::GRAIN_SIZE);
        float norm = param->convertTo0to1(grainSizeMs);
        int idx = static_cast<int>(norm * 10.0f);
        if (idx > 9) idx = 9;
        if (idx < 0) idx = 0;

        float bpm = currentBPM.load();
        if (bpm < 20.f) bpm = 20.f;
        grainSizeMs = (60000.f / bpm) * divisionMultipliers[idx];
    }

    smoothedGrainSize.setTargetValue(grainSizeMs);
    smoothedDensity.setTargetValue(rawParam (ParameterIDs::DENSITY)->load());
    smoothedSpray.setTargetValue(rawParam (ParameterIDs::SPRAY)->load());
    smoothedPitch.setTargetValue(rawParam (ParameterIDs::PITCH)->load());
    smoothedWander.setTargetValue(rawParam (ParameterIDs::WANDER)->load());
    smoothedFreeze.setTargetValue(rawParam (ParameterIDs::FREEZE)->load());
    smoothedMix.setTargetValue(rawParam (ParameterIDs::MIX)->load());
    smoothedGrainFilter.setTargetValue(rawParam (ParameterIDs::GRAIN_FILTER)->load());
    smoothedWowFlutter.setTargetValue(rawParam (ParameterIDs::WOW_FLUTTER)->load());
    smoothedSaturation.setTargetValue(rawParam (ParameterIDs::SATURATION)->load());
    smoothedHiss.setTargetValue(rawParam (ParameterIDs::HISS)->load());
    smoothedStudioSculpt.setTargetValue (rawParam (ParameterIDs::STUDIO_SCULPT)->load());
    smoothedStudioWeave .setTargetValue (rawParam (ParameterIDs::STUDIO_WEAVE) ->load());
    smoothedStudioTilt  .setTargetValue (rawParam (ParameterIDs::STUDIO_TILT)  ->load());
    smoothedWireWow .setTargetValue (rawParam (ParameterIDs::WIRE_WOW)       ->load());
    smoothedWireSat .setTargetValue (rawParam (ParameterIDs::WIRE_SATURATION)->load());
    smoothedWireHiss.setTargetValue (rawParam (ParameterIDs::WIRE_HISS)      ->load());
    smoothedOutputGain.setTargetValue(rawParam (ParameterIDs::OUTPUT_GAIN)->load());
    smoothedMasterMix.setTargetValue(rawParam (ParameterIDs::MASTER_MIX)->load());

    smoothedLoopFeedback.setTargetValue(rawParam (ParameterIDs::LOOP_FEEDBACK)->load());
    smoothedLoopDegrade.setTargetValue(rawParam (ParameterIDs::LOOP_DEGRADE)->load());

    // Space reverb targets
    smoothedSpaceSize.setTargetValue(rawParam (ParameterIDs::SPACE_SIZE)->load());
    smoothedSpaceDecay.setTargetValue(rawParam (ParameterIDs::SPACE_DECAY)->load());
    smoothedSpaceTone.setTargetValue(rawParam (ParameterIDs::SPACE_TONE)->load());
    smoothedSpaceMix.setTargetValue(rawParam (ParameterIDs::SPACE_MIX)->load());

    // Delay targets (MF-104S)
    smoothedDlyTime.setTargetValue(rawParam (ParameterIDs::DLY_TIME)->load());
    smoothedDlyFeedback.setTargetValue(rawParam (ParameterIDs::DLY_FEEDBACK)->load());
    smoothedDlyTone.setTargetValue(rawParam (ParameterIDs::DLY_TONE)->load());
    smoothedDlyCharacter.setTargetValue(rawParam (ParameterIDs::DLY_CHARACTER)->load());
    smoothedDlyMod.setTargetValue(rawParam (ParameterIDs::DLY_MOD)->load());
    smoothedDlyModRate.setTargetValue(rawParam (ParameterIDs::DLY_MOD_RATE)->load());
    smoothedDlyMix.setTargetValue(rawParam (ParameterIDs::DLY_MIX)->load());
    smoothedDlyDuck.setTargetValue(rawParam (ParameterIDs::DLY_DUCK)->load());
    smoothedDelayFreeze.setTargetValue(rawParam (ParameterIDs::DLY_FREEZE)->load());

    // Chorus targets
    smoothedChorusAmount.setTargetValue    (rawParam (ParameterIDs::CHORUS_AMOUNT)   ->load());
    smoothedChorusWidth.setTargetValue     (rawParam (ParameterIDs::CHORUS_WIDTH)    ->load());
    smoothedChorusCharacter.setTargetValue (rawParam (ParameterIDs::CHORUS_CHARACTER)->load());

    // Parametric EQ targets
    {
        const char* freqIds [7] = { ParameterIDs::EQ_B1_FREQ, ParameterIDs::EQ_B2_FREQ, ParameterIDs::EQ_B3_FREQ, ParameterIDs::EQ_B4_FREQ, ParameterIDs::EQ_B5_FREQ, ParameterIDs::EQ_B6_FREQ, ParameterIDs::EQ_B7_FREQ };
        const char* gainIds [7] = { ParameterIDs::EQ_B1_GAIN, ParameterIDs::EQ_B2_GAIN, ParameterIDs::EQ_B3_GAIN, ParameterIDs::EQ_B4_GAIN, ParameterIDs::EQ_B5_GAIN, ParameterIDs::EQ_B6_GAIN, ParameterIDs::EQ_B7_GAIN };
        const char* qIds    [7] = { ParameterIDs::EQ_B1_Q,    ParameterIDs::EQ_B2_Q,    ParameterIDs::EQ_B3_Q,    ParameterIDs::EQ_B4_Q,    ParameterIDs::EQ_B5_Q,    ParameterIDs::EQ_B6_Q,    ParameterIDs::EQ_B7_Q };
        for (int b = 0; b < 7; ++b)
        {
            smoothedEqBandFreq[b].setTargetValue (rawParam (freqIds[b])->load());
            smoothedEqBandGain[b].setTargetValue (rawParam (gainIds[b])->load());
            smoothedEqBandQ[b]   .setTargetValue (rawParam (qIds[b])->load());
        }
        smoothedEqHpFreq.setTargetValue (rawParam (ParameterIDs::EQ_HP_FREQ)->load());
        smoothedEqLpFreq.setTargetValue (rawParam (ParameterIDs::EQ_LP_FREQ)->load());
    }

    // Tape loop discrete params (no smoothing needed)
    const float loopLengthParam = rawParam (ParameterIDs::LOOP_LENGTH)->load();
    const float loopSpeedBase   = rawParam (ParameterIDs::LOOP_SPEED)->load();

    // Tape loop transport state (may be modified by auto-stop)
    bool wantRecord = tapeLoopRecording.load() > 0.5f;
    bool wantPlay   = tapeLoopPlaying.load() > 0.5f;

    // Auto-start playback when recording with existing content (overdub while paused).
    // Without this, feed-to-grain doesn't activate and playback section doesn't run.
    if (wantRecord && tapeLoop.hasContent() && !wantPlay)
        wantPlay = true;
    const float bpm = currentBPM.load();

    // Modulation engine: pick up pending config, set XY, precompute rates
    modulationEngine.setXY(xyPadX.load(std::memory_order_relaxed),
                           xyPadY.load(std::memory_order_relaxed));
    modulationEngine.beginBlock(bpm);
    const bool isFreeform = speedFreeform.load() > 0.5f;

    // Dynamic loop length resizing (once per block, not per sample)
    tapeLoop.updateLength(loopLengthParam, bpm);

    // Per-sample processing
    auto* leftChannel  = buffer.getWritePointer(0);
    auto* rightChannel = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;
    const float* rvbSendL = reverbSendBuf_.getReadPointer (0);   // fb280 — routed-osc send (filled during synth render)
    const float* rvbSendR = reverbSendBuf_.getReadPointer (1);
    float hallBlockWetPk = 0.0f;                                 // fb280 — peak wet this block → bloom viz
    const float* dlySendL = delaySendBuf_.getReadPointer (0);    // fb296 — delay routed-osc send
    const float* dlySendR = delaySendBuf_.getReadPointer (1);
    const float* dstSendL = distortionSendBuf_.getReadPointer (0);   // fb338 — distortion routed-osc send
    const float* dstSendR = distortionSendBuf_.getReadPointer (1);
    float dlyBlockWetPk = 0.0f;                                  // fb296 — peak wet this block → delay core viz
    float dstBlockWetPk = 0.0f;                                  // fb315 — peak wet this block → distortion core viz

    // ── Per-chop FX-independence (option 1): aggregate active indy masks ─
    // Walk all voices across all 4 layers. For each voice that's currently
    // playing AND has fxIndependent latched at startNote, OR its packed
    // chip mask into activeIndyMask. The indy chain runs once below with
    // this aggregate mask driving per-module bypass.
    std::uint8_t activeIndyMask = 0;
    for (auto& layer : layers)
    {
        auto& synth = layer.synth;
        for (int v = 0; v < synth.getNumVoices(); ++v)
        {
            if (auto* sv = dynamic_cast<tw::SamplerVoice*> (synth.getVoice (v)))
            {
                if (sv->isPlaying() && sv->isFxIndependent())
                    activeIndyMask |= sv->packFxMask();
            }
        }
    }

    // ── Run the indy chain into indySumBuffer ───────────────────────────
    // Grow + clear the sum buffer for this block; snapshot APVTS into
    // ParamTargets; run the chain on indyCaptureBus (which holds whatever
    // the indy voices wrote during layer.synth.renderNextBlock above).
    if (indySumBuffer.getNumSamples() < numSamples)
        indySumBuffer.setSize (2, numSamples, false, true, true);
    indySumBuffer.clear (0, numSamples);

    indyChain.setMask (activeIndyMask);
    indyChain.setParamTargets (snapshotFxParamTargets());
    indyChain.processInto (indyCaptureBus, indySumBuffer, numSamples);

    int scopePos = scopeWritePos.load();

    const bool grainOn = grainEngineEnabled.load() > 0.5f;
    const bool tapeOn = tapeEnabled.load() > 0.5f;
    // Tape loop transport is independent of tape FX (gated separately so
    // disabling the FX section keeps the loop running and vice versa).
    const bool tapeLoopOn = tapeLoopEnabled.load() > 0.5f;
    const int tapeMachineIdx = [&]() {
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(ParameterIDs::TAPE_MACHINE)))
            return cp->getIndex();
        return 0;
    }();
    tapeProcessorL.setMachine(tapeMachineIdx);
    tapeProcessorR.setMachine(tapeMachineIdx);

    // Wire-only mode toggles
    const bool wireSpace = wireSpaceNoiseEnabled.load() > 0.5f;
    const bool wireTube = wireTubeSatEnabled.load() > 0.5f;
    tapeProcessorL.setWireModes(wireSpace, wireTube);
    tapeProcessorR.setWireModes(wireSpace, wireTube);

    // Tape Link: when on, route input through ALL THREE tape machines in
    // series (Studio → Cassette → Wire) instead of just the active machine.
    const bool tapeLinkOn = tapeLinkEnabled.load() > 0.5f;

    const bool feedToGrain = tapeLoopFeedToGrain.load() > 0.5f;

    // When feed mode activates, clear the grain engine's circular buffer.
    // Without this, stale live-input data persists (up to 5s) and grains with
    // spray read old piano/input instead of loop content → dry signal leaks
    // into overdub recordings.
    const bool feedActiveNow = feedToGrain && wantPlay && tapeLoop.hasContent();
    if (feedActiveNow && !prevFeedActive)
    {
        grainEngineL.clearBuffer();
        grainEngineR.clearBuffer();
    }
    prevFeedActive = feedActiveNow;

    // Tape section OFF→ON: clear stale filter/reverb state so the first
    // sample after re-enable doesn't slam the biquads / reverb tail with
    // input that's been bypassing them.
    if (tapeOn && !prevTapeOn)
    {
        spaceReverb.reset();
        eqL.reset();
        eqR.reset();
    }
    prevTapeOn = tapeOn;

    for (int i = 0; i < numSamples; ++i)
    {
        // Advance LFOs and compute per-param offsets
        modulationEngine.processSample();

        // Read smoothed base values, apply modulation offsets, then scale
        const float grainSize    = modulationEngine.getModulatedValue(ModulationEngine::pGrainSize,  smoothedGrainSize.getNextValue());
        const float density      = modulationEngine.getModulatedValue(ModulationEngine::pDensity,    smoothedDensity.getNextValue());
        const float spray        = modulationEngine.getModulatedValue(ModulationEngine::pSpray,      smoothedSpray.getNextValue());
        const float pitch        = modulationEngine.getModulatedValue(ModulationEngine::pPitch,      smoothedPitch.getNextValue());
        const float wanderRaw    = modulationEngine.getModulatedValue(ModulationEngine::pWander,     smoothedWander.getNextValue()) * 0.01f;
        const float freezeRaw    = modulationEngine.getModulatedValue(ModulationEngine::pFreeze,     smoothedFreeze.getNextValue()) * 0.01f;
        const float freeze       = std::pow(freezeRaw, 1.5f);
        const float mix          = modulationEngine.getModulatedValue(ModulationEngine::pMix,        smoothedMix.getNextValue());
        const float grainFilterVal = modulationEngine.getModulatedValue(ModulationEngine::pGrainFilter, smoothedGrainFilter.getNextValue());
        const float wowFlutter   = modulationEngine.getModulatedValue(ModulationEngine::pWowFlutter, smoothedWowFlutter.getNextValue()) * 0.01f;
        const float saturationAmt = modulationEngine.getModulatedValue(ModulationEngine::pSaturation, smoothedSaturation.getNextValue()) * 0.01f;
        const float hissAmt      = modulationEngine.getModulatedValue(ModulationEngine::pHiss,       smoothedHiss.getNextValue()) * 0.01f;
        // Wire-specific wow/sat/hiss — modulation-aware (mirrors cassette pattern).
        const float wireWowAmt   = modulationEngine.getModulatedValue (ModulationEngine::pWireWow,        smoothedWireWow .getNextValue()) * 0.01f;
        const float wireSatAmt   = modulationEngine.getModulatedValue (ModulationEngine::pWireSaturation, smoothedWireSat .getNextValue()) * 0.01f;
        const float wireHissAmt  = modulationEngine.getModulatedValue (ModulationEngine::pWireHiss,       smoothedWireHiss.getNextValue()) * 0.01f;
        const float sculptAmt = modulationEngine.getModulatedValue (ModulationEngine::pStudioSculpt, smoothedStudioSculpt.getNextValue()) * 0.01f;
        const float weaveAmt  = modulationEngine.getModulatedValue (ModulationEngine::pStudioWeave,  smoothedStudioWeave .getNextValue()) * 0.01f;
        const float tiltAmt   = modulationEngine.getModulatedValue (ModulationEngine::pStudioTilt,   smoothedStudioTilt  .getNextValue()) * 0.01f;
        const float outputGainDb = smoothedOutputGain.getNextValue();
        const float outputGain   = std::pow(10.0f, outputGainDb / 20.0f);
        const float masterMixAmt = smoothedMasterMix.getNextValue() * 0.01f;
        const float loopFeedback = modulationEngine.getModulatedValue(ModulationEngine::pLoopFeedback, smoothedLoopFeedback.getNextValue()) * 0.01f;
        const float loopDegrade  = modulationEngine.getModulatedValue(ModulationEngine::pLoopDegrade,  smoothedLoopDegrade.getNextValue()) * 0.01f;
        const float loopSpeedParam = modulationEngine.getModulatedValue(ModulationEngine::pLoopSpeed, loopSpeedBase);

        // MF-104S delay params (read once per sample so smoothers advance)
        const float dlyTimeRaw   = modulationEngine.getModulatedValue (ModulationEngine::pDlyTime,      smoothedDlyTime.getNextValue());
        const float dlyFeedback  = modulationEngine.getModulatedValue (ModulationEngine::pDlyFeedback,  smoothedDlyFeedback.getNextValue());
        const float dlyTone      = modulationEngine.getModulatedValue (ModulationEngine::pDlyTone,      smoothedDlyTone.getNextValue());
        const float dlyCharacter = modulationEngine.getModulatedValue (ModulationEngine::pDlyCharacter, smoothedDlyCharacter.getNextValue());
        const float dlyMod       = modulationEngine.getModulatedValue (ModulationEngine::pDlyMod,       smoothedDlyMod.getNextValue());
        const float dlyModRate   = modulationEngine.getModulatedValue (ModulationEngine::pDlyModRate,   smoothedDlyModRate.getNextValue());
        const float dlyMix       = modulationEngine.getModulatedValue (ModulationEngine::pDlyMix,       smoothedDlyMix.getNextValue());
        const float dlyDuck      = modulationEngine.getModulatedValue (ModulationEngine::pDlyDuck,      smoothedDlyDuck.getNextValue());
        // dlyFreeze is a binary gate (manual button or LFO-driven). Using the
        // standard `getModulatedValue > 0.5` threshold meant an assignment at
        // default depth (50%) topped out at 0.25 — never crossing the gate.
        // Treat dlyFreeze as a gate target: engage if manual pressed OR if any
        // assignment's offset is currently positive (LFO above center). Sine
        // bipolar @ depth 50% → 50% duty cycle gate; square → on/off.
        const float dlyFreezeBase   = smoothedDelayFreeze.getNextValue();
        const float dlyFreezeOffset = modulationEngine.getOffset (ModulationEngine::pDlyFreeze);
        const bool  dlyFreezeEngaged = (dlyFreezeBase > 0.5f) || (dlyFreezeOffset > 1.0e-6f);
        const float dlyFreezeRaw    = dlyFreezeEngaged ? 1.0f : 0.0f;

        // Choice params: read raw indices.
        const int dlyModWaveIdx = static_cast<int> (rawParam (ParameterIDs::DLY_MOD_WAVE)->load());
        const int dlySyncIdx    = static_cast<int> (rawParam (ParameterIDs::DLY_SYNC)->load());
        const int dlySyncDivIdx = static_cast<int> (rawParam (ParameterIDs::DLY_SYNC_DIV)->load());
        const int dlyPitchIdx   = static_cast<int> (rawParam (ParameterIDs::DLY_PITCH)->load());
        const int dlyWidthIdx   = static_cast<int> (rawParam (ParameterIDs::DLY_WIDTH)->load());

        // If sync mode is on, replace TIME with the BPM-derived ms.
        float dlyTimeMs = dlyTimeRaw;
        if (dlySyncIdx == 1)
        {
            // beats per division (matches DLY_SYNC_DIV order):
            //   1/32, 1/16T, 1/16, 1/8T, 1/8., 1/8, 1/4T, 1/4, 1/2
            static constexpr float beatsPerDiv[9] = {
                0.125f,         // 1/32
                0.16667f,       // 1/16T
                0.25f,          // 1/16
                0.33333f,       // 1/8T
                0.75f,          // 1/8.
                0.5f,           // 1/8
                0.66667f,       // 1/4T
                1.0f,           // 1/4
                2.0f            // 1/2
            };
            float bpm = currentBPM.load();
            if (bpm < 20.0f) bpm = 20.0f;
            const int divIdx = (dlySyncDivIdx < 0 || dlySyncDivIdx > 8) ? 5 : dlySyncDivIdx;
            dlyTimeMs = (60000.0f / bpm) * beatsPerDiv[divIdx];
        }

        // Space reverb params (read early so smoothers advance every sample)
        const float spSize  = modulationEngine.getModulatedValue(ModulationEngine::pSpaceSize,  smoothedSpaceSize.getNextValue()) * 0.01f;
        const float spDecay = modulationEngine.getModulatedValue(ModulationEngine::pSpaceDecay, smoothedSpaceDecay.getNextValue()) * 0.01f;
        const float spTone  = modulationEngine.getModulatedValue(ModulationEngine::pSpaceTone,  smoothedSpaceTone.getNextValue()) * 0.01f;
        const float spMix   = modulationEngine.getModulatedValue(ModulationEngine::pSpaceMix,   smoothedSpaceMix.getNextValue()) * 0.01f;

        // Capture dry input before processing
        const float dryL = leftChannel[i];
        const float dryR = rightChannel != nullptr ? rightChannel[i] : 0.0f;

        // Choose grain engine input: live audio OR tape loop feedback (one-sample delay)
        const bool feedActive = feedToGrain && wantPlay && tapeLoop.hasContent();
        const float grainInputL = feedActive ? feedDelayL : leftChannel[i];
        const float grainInputR = feedActive
            ? feedDelayR
            : (rightChannel != nullptr ? rightChannel[i] : leftChannel[i]);

        // When feed-to-grain is active, reduce wander to prevent click compounding
        // (already-wandered signal gets re-wandered → clicks multiply)
        const float wander = feedActive ? wanderRaw * 0.7f : wanderRaw;

        // Signal chain: Input → GrainEngine → GrainFilter → TapeProcessor → TapeLoop → MasterMix → OutputGain
        float wetL = grainOn
            ? grainEngineL.processSample(grainInputL, grainSize, density, spray, pitch, wander, freeze, mix)
            : grainInputL;

        float wetR;
        if (rightChannel != nullptr)
        {
            wetR = grainOn
                ? grainEngineR.processSample(grainInputR, grainSize, density, spray, pitch, wander, freeze, mix)
                : grainInputR;
        }
        else
        {
            wetR = wetL;
        }

        // (Previously a "dry-strip" subtracted grainInput * (1 - mix/100) from
        // wet here to prevent loop content from feeding back through the grain
        // engine's dry path during overdub. That strip killed the legitimate
        // "feed loop through FX without grain" workflow: at grain mix=0 the
        // strip would zero out the entire feed signal so reverb/EQ/tape never
        // received the loop content. Strip removed; loop content now reaches
        // the FX chain regardless of grain mix. Doubling with the tape loop's
        // internal self-feedback (TapeLoopProcessor at slow speeds) is avoided
        // by disabling that internal path whenever externalFeedActive is true.)

        // Grain filter: bipolar one-pole (0=HP, 50=bypass, 100=LP)
        if (grainOn && std::abs(grainFilterVal - 50.f) > 0.5f)
        {
            float cutoff;
            bool isHP;
            if (grainFilterVal < 50.f)
            {
                const float hpNorm = (50.f - grainFilterVal) / 50.f;
                cutoff = 20.f * std::pow(400.f, hpNorm);
                isHP = true;
            }
            else
            {
                const float lpNorm = (grainFilterVal - 50.f) / 50.f;
                cutoff = 20000.f * std::pow(0.01f, lpNorm);
                isHP = false;
            }
            const float alpha = 1.f - std::exp(-6.283185f * cutoff / static_cast<float>(getSampleRate()));
            grainFilterStateL += alpha * (wetL - grainFilterStateL);
            grainFilterStateR += alpha * (wetR - grainFilterStateR);
            if (isHP)
            {
                wetL -= grainFilterStateL;
                wetR -= grainFilterStateR;
            }
            else
            {
                wetL = grainFilterStateL;
                wetR = grainFilterStateR;
            }
        }

        // Signal chain: Grain → Space → EQ → TapeProc → TapeLoop
        // Space + EQ are applied BEFORE the tape loop so all recordings
        // capture the full FX chain (reverb, EQ, and tape character).
        // The whole chain is gated on tapeOn — toggling the tape section
        // off bypasses Space, EQ, the tape processor, and the tape loop.

        // MF-104S delay: lives between grain output and SpaceReverb so reverb
        // tails wash over the repeats. Gated by tapeOn (FX chain master switch).
        if (tapeOn && dlyMix > 0.001f)
        {
            MoogDelay::Params dp;
            dp.timeMs    = dlyTimeMs;
            dp.feedback  = dlyFeedback;
            dp.tone      = dlyTone;
            dp.character = dlyCharacter;
            dp.modDepth  = dlyMod;
            dp.modRateHz = dlyModRate;
            dp.modWave   = dlyModWaveIdx;
            dp.mix       = dlyMix;
            dp.duck      = dlyDuck;
            dp.pitch     = dlyPitchIdx;
            dp.width     = dlyWidthIdx;
            dp.freezeHeld = (dlyFreezeRaw > 0.5f);
            if (delayEnabled.load() > 0.5f)
                moogDelay.processStereo (wetL, wetR, dp);
            // Bypass: wetL/wetR pass through unchanged when delayEnabled is false
        }

        // Space reverb (before tape loop so recordings include reverb)
        if (tapeOn && spMix > 0.001f)
            spaceReverb.processStereo(wetL, wetR, spSize, spDecay, spTone, spMix);

        // Capture pre-tape signal (after Space, before tape effects)
        // Overdub records this — avoids tape compounding
        const float preTapeL = wetL;
        const float preTapeR = wetR;

        if (tapeOn)
        {
            if (tapeLinkOn)
            {
                wetL = tapeProcessorL.processSampleLinked (wetL,
                                                            wowFlutter, saturationAmt, hissAmt,
                                                            wireWowAmt, wireSatAmt, wireHissAmt,
                                                            sculptAmt, weaveAmt, tiltAmt);
                if (rightChannel != nullptr)
                    wetR = tapeProcessorR.processSampleLinked (wetR,
                                                                wowFlutter, saturationAmt, hissAmt,
                                                                wireWowAmt, wireSatAmt, wireHissAmt,
                                                                sculptAmt, weaveAmt, tiltAmt);
                else
                    wetR = wetL;
            }
            else
            {
                wetL = tapeProcessorL.processSample (wetL,
                                                      wowFlutter, saturationAmt, hissAmt,
                                                      wireWowAmt, wireSatAmt, wireHissAmt,
                                                      sculptAmt, weaveAmt, tiltAmt);
                if (rightChannel != nullptr)
                    wetR = tapeProcessorR.processSample (wetR,
                                                          wowFlutter, saturationAmt, hissAmt,
                                                          wireWowAmt, wireSatAmt, wireHissAmt,
                                                          sculptAmt, weaveAmt, tiltAmt);
                else
                    wetR = wetL;
            }
        }

        // Chorus — post-tape, pre-loop
        {
            const float chAmount    = modulationEngine.getModulatedValue (
                ModulationEngine::pChorusAmount,    smoothedChorusAmount.getNextValue());
            const float chWidth     = modulationEngine.getModulatedValue (
                ModulationEngine::pChorusWidth,     smoothedChorusWidth.getNextValue());
            const float chCharacter = modulationEngine.getModulatedValue (
                ModulationEngine::pChorusCharacter, smoothedChorusCharacter.getNextValue());

            const bool chorusOn = chorusEnabled.load() > 0.5f;
            if (chorusOn)
            {
                terrainChorus.setParams (chAmount, chWidth, chCharacter);
                terrainChorus.processStereo (wetL, wetR);
            }
            else
            {
                // Bypass — still advance smoothing so re-engagement isn't a jump
                terrainChorus.setParams (chAmount, chWidth, chCharacter);
            }
        }

        // Tape loop (records fully processed signal on first pass,
        // preTapeL/R on overdub to avoid tape effect compounding).
        // Gated on tapeLoopOn (its own independent flag) — tape FX bypass
        // no longer freezes the loop transport. Resumes from same position
        // on re-enable since tapeLoop preserves its internal write head.
        const float preLoopL = wetL;
        const float preLoopR = wetR;
        if (tapeLoopOn)
            tapeLoop.processStereo(wetL, wetR, wantRecord, wantPlay,
                                   loopLengthParam, loopFeedback, loopDegrade,
                                   loopSpeedParam, bpm, isFreeform,
                                   preTapeL, preTapeR, feedActive);

        if (feedToGrain)
        {
            // Extract loop-only contribution for the feed delay buffer.
            // The feed path runs the loop output back through grain → reverb
            // → EQ → tape on the NEXT sample, accumulating into the overdub
            // buffer if recording is active.
            feedDelayL = wetL - preLoopL;
            feedDelayR = wetR - preLoopR;

            // (Previously stripped wetL = preLoopL here so the user "heard the
            // loop only through grain". That muted direct playback and made
            // the loop inaudible whenever the feed path produced silence — at
            // grain mix=0 with feed first activating, before feedDelay ramps
            // up, or during count-in when tapeLoop returns early. Removed:
            // direct loop playback now always audible. With grain mix high
            // the user will hear loop + grain layered, which is the natural
            // "send" semantics they asked for.)
        }
        else
        {
            feedDelayL = wetL;
            feedDelayR = wetR;
        }

        // Parametric EQ — post-tape, pre-master mix.
        // Independent of tapeOn so the EQ panel works even when the tape
        // section is toggled off. Master gate is EQ_MASTER_BYPASS only.
        // The per-band/per-cut params and analyzer feeds tap around this.
        {
            const float hpF    = smoothedEqHpFreq.getNextValue();
            const float lpF    = smoothedEqLpFreq.getNextValue();
            const auto* hpSlope = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParameterIDs::EQ_HP_SLOPE));
            const auto* lpSlope = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParameterIDs::EQ_LP_SLOPE));
            const bool hpByp  = rawParam (ParameterIDs::EQ_HP_BYPASS)->load() > 0.5f;
            const bool lpByp  = rawParam (ParameterIDs::EQ_LP_BYPASS)->load() > 0.5f;
            const bool eqByp  = rawParam (ParameterIDs::EQ_MASTER_BYPASS)->load() > 0.5f;
            eqL.setMasterBypass (eqByp); eqR.setMasterBypass (eqByp);
            eqL.setHp (hpF, hpSlope ? hpSlope->getIndex() : 1, hpByp);
            eqR.setHp (hpF, hpSlope ? hpSlope->getIndex() : 1, hpByp);
            eqL.setLp (lpF, lpSlope ? lpSlope->getIndex() : 0, lpByp);
            eqR.setLp (lpF, lpSlope ? lpSlope->getIndex() : 0, lpByp);

            for (int b = 0; b < 7; ++b)
            {
                const float bf = modulationEngine.getModulatedValue (ModulationEngine::pEqB1Freq + b * 3, smoothedEqBandFreq[b].getNextValue());
                const float bg = modulationEngine.getModulatedValue (ModulationEngine::pEqB1Gain + b * 3, smoothedEqBandGain[b].getNextValue());
                const float bq = modulationEngine.getModulatedValue (ModulationEngine::pEqB1Q    + b * 3, smoothedEqBandQ[b].getNextValue());
                const bool bByp = rawParam (
                                       b == 0 ? ParameterIDs::EQ_B1_BYPASS : b == 1 ? ParameterIDs::EQ_B2_BYPASS :
                                       b == 2 ? ParameterIDs::EQ_B3_BYPASS : b == 3 ? ParameterIDs::EQ_B4_BYPASS :
                                       b == 4 ? ParameterIDs::EQ_B5_BYPASS : b == 5 ? ParameterIDs::EQ_B6_BYPASS :
                                                                                       ParameterIDs::EQ_B7_BYPASS)->load() > 0.5f;
                eqL.setBandParams (b, bf, bg, bq, bByp);
                eqR.setBandParams (b, bf, bg, bq, bByp);
            }

            if (vizLive) analyzerPre.pushSample (0.5f * (wetL + wetR));
            wetL = eqL.processSample (wetL);
            if (rightChannel != nullptr) wetR = eqR.processSample (wetR);
            else wetR = wetL;
            if (vizLive) analyzerPost.pushSample (0.5f * (wetL + wetR));
        }

        // Master mix + output gain
        float outL = (dryL * (1.0f - masterMixAmt) + wetL * masterMixAmt) * outputGain;
        leftChannel[i] = outL;

        if (rightChannel != nullptr)
        {
            float outR = (dryR * (1.0f - masterMixAmt) + wetR * masterMixAmt) * outputGain;
            rightChannel[i] = outR;
        }

        // Per-chop FX-independence (option 1): read the indy chain's output
        // (which already processed indyCaptureBus through the enabled FX per
        // activeIndyMask above) and mix into master. Same spot as the old
        // indy add-back so master volume + soft-clipper still apply.
        leftChannel[i] += indySumBuffer.getSample (0, i) * outputGain;
        if (rightChannel != nullptr)
            rightChannel[i] += indySumBuffer.getSample (1, i) * outputGain;

        // ── fb276-280 — synth FX-rack REVERB (Hall). PER-OSC NO-BLEED SEND: only the routed oscillators
        // (accumulated into reverbSendBuf_ during synth render) feed the reverb; the wet is added back with
        // a true wet/dry on the ROUTED portion only (duck routed dry + add wet ⇒ 100% Mix = fully wet),
        // so unrouted oscs stay bone dry. hallRouteActive_ + gains were resolved pre-render. Routes off ⇒
        // env fades to 0 ⇒ fully bypassed. Pre-makeup so the limiter catches wet peaks.
        if (i == 0)
        {
            // fb281 — TYPE ROUTING (Hall=0 / Room=1; other types fall back to Hall until built). A type
            // change dips the wet through 0 (click-free), then swaps + resets the incoming engine.
            int pend = (int) *rawParam (ParameterIDs::SYN_RVB_TYPE);
            if (pend < 0 || pend > 8) pend = 0;   // fb291 — all 9 types now have DSP (Hall..Convolution)
            if (activeRvbType_ < 0) activeRvbType_ = pend;   // first block: adopt immediately (no dip)
            if (pend != activeRvbType_)
            {
                rvbSwapping_ = true;
                if (hallRvbEnv_ < 1.0e-3f)   // faded out → commit the swap on the (now silent) engines
                {
                    activeRvbType_ = pend;
                    if (pend == 8) convolutionReverb.reset(); else if (pend == 7) shimmerReverb.reset(); else if (pend == 6) basinReverb.reset(); else if (pend == 5) vintageReverb.reset(); else if (pend == 4) digitalReverb.reset(); else if (pend == 3) springReverb.reset(); else if (pend == 2) plateReverb.reset(); else if (pend == 1) roomReverb.reset(); else hallReverb.reset();
                    rvbSwapping_ = false;
                }
            }
            else rvbSwapping_ = false;
            hallEnvT_ = (hallPower_ && ! rvbSwapping_) ? 1.0f : 0.0f;   // fb303 — reverb runs on POWER (main-send or per-osc)
            if (hallPower_)
            {
                if (activeRvbType_ == 8)
                {
                    // ── CONVOLUTION (true FFT convolution + synth/user IR) — the one non-synthetic reverb. Signature slots:
                    //    DIFFUSE→Density (smear) · MODDEPTH/MODRATE→Motion (chorus) · HIDAMP→Attack (onset fade) ·
                    //    LOWDECAY→Distance (ER↔tail) · MODMODE→Shape (ER pattern) · FREEZE→Reverse (backwards). ──
                    convolutionReverb.setSize        (rawParam (ParameterIDs::SYN_RVB_SIZE)->load());
                    convolutionReverb.setDecay       (rawParam (ParameterIDs::SYN_RVB_DECAY)->load());
                    convolutionReverb.setTone        (rawParam (ParameterIDs::SYN_RVB_TONE)->load());
                    convolutionReverb.setPreDelayMs  (rawParam (ParameterIDs::SYN_RVB_PREDELAY)->load() * 200.0f);
                    convolutionReverb.setDensity     (rawParam (ParameterIDs::SYN_RVB_DIFFUSE)->load());
                    convolutionReverb.setMotionDepth (rawParam (ParameterIDs::SYN_RVB_MODDEPTH)->load());
                    convolutionReverb.setMotionRate  (0.05f + rawParam (ParameterIDs::SYN_RVB_MODRATE)->load() * 4.95f);
                    convolutionReverb.setAttack      (rawParam (ParameterIDs::SYN_RVB_HIDAMP)->load());     // HIDAMP slot → Attack
                    convolutionReverb.setDistance    (rawParam (ParameterIDs::SYN_RVB_LOWDECAY)->load());   // LOWDECAY slot → Distance
                    convolutionReverb.setLowCutHz    (20.0f * std::pow (50.0f, rawParam (ParameterIDs::SYN_RVB_LOWCUT)->load()));
                    convolutionReverb.setWidth       (rawParam (ParameterIDs::SYN_RVB_WIDTH)->load());
                    convolutionReverb.setCharacter   ((int) *rawParam (ParameterIDs::SYN_RVB_CHARACTER));
                    convolutionReverb.setShape        ((int) *rawParam (ParameterIDs::SYN_RVB_MODMODE));
                    convolutionReverb.setModEnabled  (rawParam (ParameterIDs::SYN_RVB_MOD)->load()    > 0.5f);
                    convolutionReverb.setReverse     (rawParam (ParameterIDs::SYN_RVB_FREEZE)->load() > 0.5f);   // FREEZE slot → Reverse pill
                    convolutionReverb.updateCoefficients();
                    convolutionReverb.prepareBlock();   // rate-limited IR rebake (off the tight per-sample loop)
                }
                else if (activeRvbType_ == 7)
                {
                    // ── SHIMMER (ethereal octave wash) — Hall FDN + pitch-shifter in the feedback loop. Signature slots:
                    //    HIDAMP→Shimmer (pitch blend = shimmer amount) · LOWDECAY→Regen (feedback buildup) ·
                    //    MODMODE→Shift (6 intervals; +12 default) · MODDEPTH+RATE = the chorus pair. ──
                    shimmerReverb.setSize        (rawParam (ParameterIDs::SYN_RVB_SIZE)->load());
                    shimmerReverb.setDecay       (rawParam (ParameterIDs::SYN_RVB_DECAY)->load());
                    shimmerReverb.setTone        (rawParam (ParameterIDs::SYN_RVB_TONE)->load());
                    shimmerReverb.setPreDelayMs  (rawParam (ParameterIDs::SYN_RVB_PREDELAY)->load() * 200.0f);
                    shimmerReverb.setDiffusion   (rawParam (ParameterIDs::SYN_RVB_DIFFUSE)->load());
                    shimmerReverb.setModDepth    (rawParam (ParameterIDs::SYN_RVB_MODDEPTH)->load());
                    shimmerReverb.setModRate     (0.05f + rawParam (ParameterIDs::SYN_RVB_MODRATE)->load() * 4.95f);
                    shimmerReverb.setShimmer     (rawParam (ParameterIDs::SYN_RVB_HIDAMP)->load());     // HIDAMP slot → Shimmer (pitch blend)
                    shimmerReverb.setRegen       (rawParam (ParameterIDs::SYN_RVB_LOWDECAY)->load());   // LOWDECAY slot → Regen (feedback)
                    shimmerReverb.setLowCutHz    (20.0f * std::pow (50.0f, rawParam (ParameterIDs::SYN_RVB_LOWCUT)->load()));
                    shimmerReverb.setWidth       (rawParam (ParameterIDs::SYN_RVB_WIDTH)->load());
                    shimmerReverb.setCharacter   ((int) *rawParam (ParameterIDs::SYN_RVB_CHARACTER));
                    shimmerReverb.setShift        ((int) *rawParam (ParameterIDs::SYN_RVB_MODMODE));
                    shimmerReverb.setModEnabled  (rawParam (ParameterIDs::SYN_RVB_MOD)->load()    > 0.5f);
                    shimmerReverb.setFreeze      (rawParam (ParameterIDs::SYN_RVB_FREEZE)->load() > 0.5f);
                    shimmerReverb.updateCoefficients();
                }
                else if (activeRvbType_ == 6)
                {
                    // ── BASIN (huge dark ambient wash) — Hall FDN retuned. Signature slots: HIDAMP→Damping (dark) ·
                    //    LOWDECAY→Bass Decay = the BASS-SAFE crossover (default <1 ⇒ lows decay FASTER than mids;
                    //    the 0.22·9^v curve lands 0.5→0.66 safe, sweeps to bloom) · MODMODE→Motion · MODDEPTH+RATE pair. ──
                    basinReverb.setSize        (rawParam (ParameterIDs::SYN_RVB_SIZE)->load());
                    basinReverb.setDecay       (rawParam (ParameterIDs::SYN_RVB_DECAY)->load());
                    basinReverb.setTone        (rawParam (ParameterIDs::SYN_RVB_TONE)->load());
                    basinReverb.setPreDelayMs  (rawParam (ParameterIDs::SYN_RVB_PREDELAY)->load() * 250.0f);
                    basinReverb.setDiffusion   (rawParam (ParameterIDs::SYN_RVB_DIFFUSE)->load());
                    basinReverb.setModDepth    (rawParam (ParameterIDs::SYN_RVB_MODDEPTH)->load());
                    basinReverb.setModRate     (0.05f + rawParam (ParameterIDs::SYN_RVB_MODRATE)->load() * 4.95f);
                    basinReverb.setDamping     (rawParam (ParameterIDs::SYN_RVB_HIDAMP)->load());
                    basinReverb.setBassDecay   (0.22f * std::pow (9.0f, rawParam (ParameterIDs::SYN_RVB_LOWDECAY)->load()));   // bass-safe(<1) ↔ bloom(>1)
                    basinReverb.setLowCutHz    (20.0f * std::pow (50.0f, rawParam (ParameterIDs::SYN_RVB_LOWCUT)->load()));
                    basinReverb.setWidth       (rawParam (ParameterIDs::SYN_RVB_WIDTH)->load());
                    basinReverb.setCharacter   ((int) *rawParam (ParameterIDs::SYN_RVB_CHARACTER));
                    basinReverb.setMotion      ((int) *rawParam (ParameterIDs::SYN_RVB_MODMODE));
                    basinReverb.setModEnabled  (rawParam (ParameterIDs::SYN_RVB_MOD)->load()    > 0.5f);
                    basinReverb.setFreeze      (rawParam (ParameterIDs::SYN_RVB_FREEZE)->load() > 0.5f);
                    basinReverb.updateCoefficients();
                }
                else if (activeRvbType_ == 5)
                {
                    // ── VINTAGE (80s digital rack) — signature slots: HIDAMP→Age (reduced-SR alias+band-limit) ·
                    //    LOWDECAY→Grit (bit-crush) · LOWCUT→Drive (input saturation) · MODMODE→Shape (Normal/Gate/
                    //    Gate-Long/Reverse/Nonlin/Ambience) · DIFFUSE→Diffusion · MODDEPTH+MODRATE = the chorus pair. ──
                    vintageReverb.setSize        (rawParam (ParameterIDs::SYN_RVB_SIZE)->load());
                    vintageReverb.setDecay       (rawParam (ParameterIDs::SYN_RVB_DECAY)->load());
                    vintageReverb.setTone        (rawParam (ParameterIDs::SYN_RVB_TONE)->load());
                    vintageReverb.setPreDelayMs  (rawParam (ParameterIDs::SYN_RVB_PREDELAY)->load() * 200.0f);
                    vintageReverb.setDiffusion   (rawParam (ParameterIDs::SYN_RVB_DIFFUSE)->load());
                    vintageReverb.setModDepth    (rawParam (ParameterIDs::SYN_RVB_MODDEPTH)->load());
                    vintageReverb.setModRate     (0.05f + rawParam (ParameterIDs::SYN_RVB_MODRATE)->load() * 4.95f);
                    vintageReverb.setAge         (rawParam (ParameterIDs::SYN_RVB_HIDAMP)->load());     // HIDAMP slot → Age
                    vintageReverb.setGrit        (rawParam (ParameterIDs::SYN_RVB_LOWDECAY)->load());   // LOWDECAY slot → Grit
                    vintageReverb.setDrive       (rawParam (ParameterIDs::SYN_RVB_LOWCUT)->load());     // LOWCUT slot → Drive
                    vintageReverb.setWidth       (rawParam (ParameterIDs::SYN_RVB_WIDTH)->load());
                    vintageReverb.setCharacter   ((int) *rawParam (ParameterIDs::SYN_RVB_CHARACTER));
                    vintageReverb.setShape        ((int) *rawParam (ParameterIDs::SYN_RVB_MODMODE));
                    vintageReverb.setModEnabled  (rawParam (ParameterIDs::SYN_RVB_MOD)->load()    > 0.5f);
                    vintageReverb.setFreeze      (rawParam (ParameterIDs::SYN_RVB_FREEZE)->load() > 0.5f);
                    vintageReverb.updateCoefficients();
                }
                else if (activeRvbType_ == 4)
                {
                    // ── DIGITAL (Lexicon 224) — shared slots: DIFFUSE→Diffusion · MODDEPTH→Mod Depth (the chorus) ·
                    //    MODRATE→Mod Rate · HIDAMP→Treble Decay · LOWDECAY→Bass Decay · MODMODE→Chorus Voicing. ──
                    digitalReverb.setSize        (rawParam (ParameterIDs::SYN_RVB_SIZE)->load());
                    digitalReverb.setDecay       (rawParam (ParameterIDs::SYN_RVB_DECAY)->load());
                    digitalReverb.setTone        (rawParam (ParameterIDs::SYN_RVB_TONE)->load());
                    digitalReverb.setPreDelayMs  (rawParam (ParameterIDs::SYN_RVB_PREDELAY)->load() * 200.0f);
                    digitalReverb.setDiffusion   (rawParam (ParameterIDs::SYN_RVB_DIFFUSE)->load());
                    digitalReverb.setModDepth    (rawParam (ParameterIDs::SYN_RVB_MODDEPTH)->load());
                    digitalReverb.setModRate     (0.02f + rawParam (ParameterIDs::SYN_RVB_MODRATE)->load() * 5.98f);
                    digitalReverb.setTrebleDecay (rawParam (ParameterIDs::SYN_RVB_HIDAMP)->load());
                    digitalReverb.setBassDecay   (0.16f + rawParam (ParameterIDs::SYN_RVB_LOWDECAY)->load() * 6.84f);   // fb286 — wider LF bloom
                    digitalReverb.setLowCutHz    (20.0f * std::pow (50.0f, rawParam (ParameterIDs::SYN_RVB_LOWCUT)->load()));
                    digitalReverb.setWidth       (rawParam (ParameterIDs::SYN_RVB_WIDTH)->load());
                    digitalReverb.setCharacter   ((int) *rawParam (ParameterIDs::SYN_RVB_CHARACTER));
                    digitalReverb.setVoicing     ((int) *rawParam (ParameterIDs::SYN_RVB_MODMODE));
                    digitalReverb.setModEnabled  (rawParam (ParameterIDs::SYN_RVB_MOD)->load()    > 0.5f);
                    digitalReverb.setFreeze      (rawParam (ParameterIDs::SYN_RVB_FREEZE)->load() > 0.5f);
                    digitalReverb.updateCoefficients();
                }
                else if (activeRvbType_ == 3)
                {
                    // ── SPRING — shared slots: SIZE→Tension · DIFFUSE→Transition · MODDEPTH→Shake · MODRATE→Dispersion
                    //    HIDAMP→Damping · LOWDECAY→Drive · MODMODE→Springs(count). The boing = dispersive allpass loop. ──
                    springReverb.setTension    (rawParam (ParameterIDs::SYN_RVB_SIZE)->load());
                    springReverb.setDecay      (rawParam (ParameterIDs::SYN_RVB_DECAY)->load());
                    springReverb.setTone       (rawParam (ParameterIDs::SYN_RVB_TONE)->load());
                    springReverb.setPreDelayMs  (rawParam (ParameterIDs::SYN_RVB_PREDELAY)->load() * 150.0f);
                    springReverb.setTransition (rawParam (ParameterIDs::SYN_RVB_DIFFUSE)->load());
                    springReverb.setShake      (rawParam (ParameterIDs::SYN_RVB_MODDEPTH)->load());
                    springReverb.setDispersion (rawParam (ParameterIDs::SYN_RVB_MODRATE)->load());
                    springReverb.setDamping    (rawParam (ParameterIDs::SYN_RVB_HIDAMP)->load());
                    springReverb.setDrive      (rawParam (ParameterIDs::SYN_RVB_LOWDECAY)->load());
                    springReverb.setLowCutHz   (20.0f * std::pow (50.0f, rawParam (ParameterIDs::SYN_RVB_LOWCUT)->load()));
                    springReverb.setWidth      (rawParam (ParameterIDs::SYN_RVB_WIDTH)->load());
                    springReverb.setCharacter  ((int) *rawParam (ParameterIDs::SYN_RVB_CHARACTER));
                    springReverb.setSprings    ((int) *rawParam (ParameterIDs::SYN_RVB_MODMODE));
                    springReverb.setModEnabled (true);   // fb287 — SYN_RVB_MOD is Duck for Spring; Shake stays gated by its own knob (0 = silent)
                    springReverb.setFreeze     (rawParam (ParameterIDs::SYN_RVB_FREEZE)->load() > 0.5f);
                    springReverb.updateCoefficients();
                }
                else if (activeRvbType_ == 2)
                {
                    // ── PLATE — fb287 MATCHED MOD PAIR: DIFFUSE slot → Mod Rate (relabeled; plate density is
                    //    Character/Material, so the Diffusion knob retired) · MODDEPTH → Mod Depth · MODRATE → Dispersion
                    //    · MODMODE → Material · HIDAMP → Damping · LOWDECAY → Bass Decay. ──
                    plateReverb.setSize        (rawParam (ParameterIDs::SYN_RVB_SIZE)->load());
                    plateReverb.setDecay       (rawParam (ParameterIDs::SYN_RVB_DECAY)->load());
                    plateReverb.setTone        (rawParam (ParameterIDs::SYN_RVB_TONE)->load());
                    plateReverb.setPreDelayMs  (rawParam (ParameterIDs::SYN_RVB_PREDELAY)->load() * 120.0f);
                    plateReverb.setModRate     (0.05f + rawParam (ParameterIDs::SYN_RVB_DIFFUSE)->load() * 4.95f);   // fb287 — matched pair w/ Mod Depth (was Diffusion)
                    plateReverb.setModDepth    (rawParam (ParameterIDs::SYN_RVB_MODDEPTH)->load());
                    plateReverb.setDispersion  (rawParam (ParameterIDs::SYN_RVB_MODRATE)->load());
                    plateReverb.setDamping     (rawParam (ParameterIDs::SYN_RVB_HIDAMP)->load());
                    plateReverb.setBassDecay   (0.25f + rawParam (ParameterIDs::SYN_RVB_LOWDECAY)->load() * 2.25f);
                    plateReverb.setLowCutHz    (20.0f * std::pow (50.0f, rawParam (ParameterIDs::SYN_RVB_LOWCUT)->load()));
                    plateReverb.setWidth       (rawParam (ParameterIDs::SYN_RVB_WIDTH)->load());
                    plateReverb.setCharacter   ((int) *rawParam (ParameterIDs::SYN_RVB_CHARACTER));
                    plateReverb.setMaterial    ((int) *rawParam (ParameterIDs::SYN_RVB_MODMODE));
                    plateReverb.setModEnabled  (rawParam (ParameterIDs::SYN_RVB_MOD)->load()    > 0.5f);
                    plateReverb.setFreeze      (rawParam (ParameterIDs::SYN_RVB_FREEZE)->load() > 0.5f);
                    plateReverb.updateCoefficients();
                }
                else if (activeRvbType_ == 1)
                {
                    // ── ROOM — fb287: NO user mod (its 2nd pill is Duck). MODDEPTH slot → Spread (ER stereo width);
                    //    MODRATE→Reflections · MODMODE→Shape · HIDAMP→Damping · LOWDECAY→Bass Decay. Internal gentle
                    //    de-metallizing mod stays ON (fixed default) — it's part of the room, not a user control. ──
                    roomReverb.setSize        (rawParam (ParameterIDs::SYN_RVB_SIZE)->load());
                    roomReverb.setDecay       (rawParam (ParameterIDs::SYN_RVB_DECAY)->load());
                    roomReverb.setTone        (rawParam (ParameterIDs::SYN_RVB_TONE)->load());
                    roomReverb.setPreDelayMs  (rawParam (ParameterIDs::SYN_RVB_PREDELAY)->load() * 120.0f);
                    roomReverb.setDiffusion   (rawParam (ParameterIDs::SYN_RVB_DIFFUSE)->load());
                    roomReverb.setSpread      (rawParam (ParameterIDs::SYN_RVB_MODDEPTH)->load());   // fb287 — ER stereo width (owns the MODDEPTH slot)
                    roomReverb.setReflections (rawParam (ParameterIDs::SYN_RVB_MODRATE)->load());
                    roomReverb.setDamping     (rawParam (ParameterIDs::SYN_RVB_HIDAMP)->load());
                    roomReverb.setBassDecay   (0.25f + rawParam (ParameterIDs::SYN_RVB_LOWDECAY)->load() * 2.25f);
                    roomReverb.setLowCutHz    (20.0f * std::pow (50.0f, rawParam (ParameterIDs::SYN_RVB_LOWCUT)->load()));
                    roomReverb.setWidth       (rawParam (ParameterIDs::SYN_RVB_WIDTH)->load());
                    roomReverb.setCharacter   ((int) *rawParam (ParameterIDs::SYN_RVB_CHARACTER));
                    roomReverb.setShape       ((int) *rawParam (ParameterIDs::SYN_RVB_MODMODE));
                    roomReverb.setModEnabled  (true);   // fb287 — SYN_RVB_MOD is Duck for Room; keep the gentle internal mod always on
                    roomReverb.setFreeze      (rawParam (ParameterIDs::SYN_RVB_FREEZE)->load() > 0.5f);
                    roomReverb.updateCoefficients();
                }
                else
                {
                    // ── HALL ──
                    hallReverb.setSize        (rawParam (ParameterIDs::SYN_RVB_SIZE)->load());
                    hallReverb.setDecay       (rawParam (ParameterIDs::SYN_RVB_DECAY)->load());
                    hallReverb.setTone        (rawParam (ParameterIDs::SYN_RVB_TONE)->load());
                    hallReverb.setPreDelayMs  (rawParam (ParameterIDs::SYN_RVB_PREDELAY)->load() * 250.0f);
                    hallReverb.setDiffusion   (rawParam (ParameterIDs::SYN_RVB_DIFFUSE)->load());
                    hallReverb.setModDepth    (rawParam (ParameterIDs::SYN_RVB_MODDEPTH)->load());
                    hallReverb.setModRate     (0.05f + rawParam (ParameterIDs::SYN_RVB_MODRATE)->load() * 4.95f);
                    hallReverb.setHighDamping (rawParam (ParameterIDs::SYN_RVB_HIDAMP)->load());
                    hallReverb.setLowDecay    (0.25f + rawParam (ParameterIDs::SYN_RVB_LOWDECAY)->load() * 1.75f);
                    hallReverb.setLowCutHz    (20.0f * std::pow (50.0f, rawParam (ParameterIDs::SYN_RVB_LOWCUT)->load()));
                    hallReverb.setWidth       (rawParam (ParameterIDs::SYN_RVB_WIDTH)->load());
                    hallReverb.setCharacter   ((int) *rawParam (ParameterIDs::SYN_RVB_CHARACTER));
                    hallReverb.setModMode     ((int) *rawParam (ParameterIDs::SYN_RVB_MODMODE));
                    hallReverb.setModEnabled  (rawParam (ParameterIDs::SYN_RVB_MOD)->load()    > 0.5f);
                    hallReverb.setFreeze      (rawParam (ParameterIDs::SYN_RVB_FREEZE)->load() > 0.5f);
                    hallReverb.updateCoefficients();
                }
                const float mixv = rawParam (ParameterIDs::SYN_RVB_MIX)->load();
                hallRvbWetT_ = std::sin (mixv * 0.5f * juce::MathConstants<float>::pi);
                hallRvbDryT_ = std::cos (mixv * 0.5f * juce::MathConstants<float>::pi);
                // fb287 — DUCK is the Room/Spring 2nd pill (SYN_RVB_DUCK). Only those two types ; other types'
                // 2nd pill is Mod. Resolve once per block so the per-sample follower stays branch-light.
                rvbDuckActive_ = (activeRvbType_ == 1 || activeRvbType_ == 3)
                                 && rawParam (ParameterIDs::SYN_RVB_DUCK)->load() > 0.5f;
            }
            // fb293 — CONVOLUTION IDLE BAKE: keep the IR baked for the VIZ even when NO reverb route is active, so a
            // dropped user IR / selected Space / bake-knob change shows its REAL waveform immediately. bakeL (the
            // getConvIR envelope) is ONLY written by a rebake; without this it stays the default = the "placeholder"
            // Max saw. Bake-affecting params only; bakeIfDirtyIdle hard-bakes off the tight loop (idle ⇒ no click).
            else if (activeRvbType_ == 8 && ! rvbSwapping_)
            {
                convolutionReverb.setSize     (rawParam (ParameterIDs::SYN_RVB_SIZE)->load());
                convolutionReverb.setDecay    (rawParam (ParameterIDs::SYN_RVB_DECAY)->load());
                convolutionReverb.setDensity  (rawParam (ParameterIDs::SYN_RVB_DIFFUSE)->load());
                convolutionReverb.setAttack   (rawParam (ParameterIDs::SYN_RVB_HIDAMP)->load());
                convolutionReverb.setDistance (rawParam (ParameterIDs::SYN_RVB_LOWDECAY)->load());
                convolutionReverb.setCharacter((int) *rawParam (ParameterIDs::SYN_RVB_CHARACTER));
                convolutionReverb.setShape    ((int) *rawParam (ParameterIDs::SYN_RVB_MODMODE));
                convolutionReverb.setReverse  (rawParam (ParameterIDs::SYN_RVB_FREEZE)->load() > 0.5f);
                convolutionReverb.updateCoefficients();
                convolutionReverb.bakeIfDirtyIdle();
            }
        }
        auto applyRvb = [&]()   // fb307 — reverb INSERT as a lambda so the chain order (reverb↔delay) is swappable by drag; default reverb-first = byte-identical
        {
        if (hallPower_ || hallRvbEnv_ > 1.0e-4f)
        {
            hallRvbEnv_ += (hallEnvT_    - hallRvbEnv_) * hallSm_;   // fade on/off
            hallRvbDry_ += (hallRvbDryT_ - hallRvbDry_) * hallSm_;   // ramp mix
            hallRvbWet_ += (hallRvbWetT_ - hallRvbWet_) * hallSm_;
            // Send = routed oscs (voice-level), scaled by outputGain AND the -6 dB kVoiceToFxPad (buffer.applyGain
            // near L6055) so sgL/sgR match the routed dry EXACTLY as it sits in the master mix. fb292 BUGFIX (Max:
            // "Mix up = fully wet"): the pad was MISSING here, so the duck subtracted 2x the routed dry actually in
            // leftChannel → at Mix 100% the dry was only half-cancelled (phase-inverted, still audible). Padding
            // sgL/sgR corrects BOTH the duck term and the wet input (equal-power) for all 9 types. Proven offline:
            // Mix 100% dry residual 0 dB → -93 dB. Routes off ⇒ send=nullptr ⇒ raw=0 ⇒ byte-identical default.
            // fb303 — MAIN SEND (no pills): feed the WHOLE current mix so "add wet − duck·input" becomes a
            // wet/dry INSERT on the whole synth; running before the delay below ⇒ the delay hears this reverb
            // output (serial). Per-osc (pills) ⇒ the padded routed send bus, exactly as before.
            float sgL, sgR;
            if (hallMainSend_) {
                // fb305 — MAIN SEND excludes per-osc-routed oscs: an osc routed to ANY effect (its dry sits in
                // reverbSendBuf_/delaySendBuf_) LEAVES the main send, so it is affected ONLY by the effect(s) it's
                // routed to (Max: "osc C routed to delay should get delay, NOT the reverb main send"). Subtract that
                // routed dry (send × outputGain × kVoiceToFxPad = exactly how it sits in leftChannel). No pills ⇒ 0.
                const float rtdL = ((rvbSendL ? rvbSendL[i] : 0.0f) + (dlySendL ? dlySendL[i] : 0.0f) + (dstSendL ? dstSendL[i] : 0.0f)) * outputGain * kVoiceToFxPad;   /* fb338 — the fb305 law: EVERY send bus joins EVERY main-send exclusion */
                sgL = leftChannel[i] - rtdL;
                if (rightChannel != nullptr) { const float rtdR = ((rvbSendR ? rvbSendR[i] : 0.0f) + (dlySendR ? dlySendR[i] : 0.0f) + (dstSendR ? dstSendR[i] : 0.0f)) * outputGain * kVoiceToFxPad; sgR = rightChannel[i] - rtdR; }
                else sgR = sgL;
            }
            else { const float rawL = (rvbSendL != nullptr) ? rvbSendL[i] : 0.0f;
                   const float rawR = (rvbSendR != nullptr) ? rvbSendR[i] : rawL;
                   sgL = rawL * outputGain * kVoiceToFxPad; sgR = rawR * outputGain * kVoiceToFxPad; }
            float rl, rr;
            if      (activeRvbType_ == 8) convolutionReverb.processSample (sgL, sgR, rl, rr);  // fb291 — Convolution (internally block-buffered, B-latency)
            else if (activeRvbType_ == 7) shimmerReverb.processSample (sgL, sgR, rl, rr);  // fb290 — Shimmer
            else if (activeRvbType_ == 6) basinReverb.processSample   (sgL, sgR, rl, rr);  // fb289 — Basin
            else if (activeRvbType_ == 5) vintageReverb.processSample (sgL, sgR, rl, rr);  // fb288 — Vintage
            else if (activeRvbType_ == 4) digitalReverb.processSample (sgL, sgR, rl, rr);  // fb285 — active engine
            else if (activeRvbType_ == 3) springReverb.processSample (sgL, sgR, rl, rr);   // fb284
            else if (activeRvbType_ == 2) plateReverb.processSample (sgL, sgR, rl, rr);   // fb282
            else if (activeRvbType_ == 1) roomReverb.processSample  (sgL, sgR, rl, rr);
            else                          hallReverb.processSample  (sgL, sgR, rl, rr);
            const float e = hallRvbEnv_, duck = e * (1.0f - hallRvbDry_), wet = e * hallRvbWet_;
            // fb287 — DUCK (Room/Spring 2nd pill): env-follow the routed dry (send) level and pull the WET
            // down under it (dynamic — louder input ducks deeper), so the reverb recedes while you play and
            // BLOOMS in the gaps. duck (the wet/dry crossfade above) is untouched; only the wet gain scales.
            float duckG = 1.0f;
            if (rvbDuckActive_)
            {
                const float inLvl = 0.5f * (std::abs (sgL) + std::abs (sgR));
                duckEnv_ = inLvl + (inLvl > duckEnv_ ? duckAtkCoef_ : duckRelCoef_) * (duckEnv_ - inLvl);
                duckG = 1.0f / (1.0f + 7.0f * duckEnv_);   // 1 in the gaps → deep duck under signal
            }
            const float wetG = wet * duckG;
            leftChannel[i]  += wetG * rl - duck * sgL;    // add wet (ducked); duck ONLY the routed dry (unrouted untouched)
            if (rightChannel != nullptr)
                rightChannel[i] += wetG * rr - duck * sgR;
            const float wmag = 0.5f * (std::abs (rl) + std::abs (rr)) * e * duckG;   // fb280/fb287 — bloom follows audible (ducked) wet
            if (wmag > hallBlockWetPk) hallBlockWetPk = wmag;
        }
        };   // fb307 — end applyRvb (both blooms now publish AFTER the ordered chain below)

        // ── fb296 — synth FX-rack DELAY (parallel per-osc send). Click-free type swap + Mix-100%-wet, mirrors
        //    the reverb above. Own send bus (delaySendBuf_) so its routing is fully independent of the reverb.
        if (i == 0)
        {
            int dpend = (int) *rawParam (ParameterIDs::SYN_DLY_TYPE);
            if (dpend < 0 || dpend > 3) dpend = 0;               // Digital/Tape/BBD/Diffuse
            if (activeDlyType_ < 0) activeDlyType_ = dpend;
            if (dpend != activeDlyType_)
            {
                dlySwapping_ = true;
                if (dlyEnv_ < 1.0e-3f) { activeDlyType_ = dpend; delayEngine.reset(); dlySwapping_ = false; }
            }
            else dlySwapping_ = false;
            dlyEnvT_ = (dlyPower_ && ! dlySwapping_) ? 1.0f : 0.0f;   // fb303 — delay runs on POWER (main-send or per-osc)
            if (dlyPower_)
            {
                // Resolve delay TIME — synced to a note division, or free ms from the Time knob.
                const bool sync    = rawParam (ParameterIDs::SYN_DLY_SYNC)->load() > 0.5f;
                const int  syncDiv = (int) *rawParam (ParameterIDs::SYN_DLY_SYNCDIV);   // 0 Free/1 1-4/2 1-8/3 1-8T/4 1-8D/5 1-16
                // fb304/fb306 — division index → multiplier of a QUARTER note (qms). Shared by L and (unlinked) R.
                auto divMult = [] (int d) -> float {
                    switch (d) {
                        case 1:  return 16.0f;            // 4 bar
                        case 2:  return 8.0f;             // 2 bar
                        case 3:  return 4.0f;             // 1 bar
                        case 4:  return 2.0f;             // 1/2
                        case 5:  return 3.0f;             // 1/2 dotted
                        case 6:  return 2.0f*2.0f/3.0f;   // 1/2 triplet
                        case 7:  return 1.0f;             // 1/4
                        case 8:  return 1.5f;             // 1/4 dotted
                        case 9:  return 1.0f*2.0f/3.0f;   // 1/4 triplet
                        case 10: return 0.5f;             // 1/8
                        case 11: return 0.75f;            // 1/8 dotted
                        case 12: return 0.5f*2.0f/3.0f;   // 1/8 triplet
                        case 13: return 0.25f;            // 1/16
                        case 14: return 0.375f;           // 1/16 dotted
                        case 15: return 0.25f*2.0f/3.0f;  // 1/16 triplet
                        case 16: return 0.125f;           // 1/32
                        case 17: return 0.0625f;          // 1/64
                        case 18: return 0.03125f;         // 1/128
                        case 19: return 0.015625f;        // 1/256
                    }
                    return 0.5f; };
                float bpmNow = currentBPM.load(); if (bpmNow < 20.0f) bpmNow = 120.0f;
                const float qms = 60000.0f / bpmNow;            // quarter-note ms
                const float timeMs = (sync && syncDiv > 0)
                    ? qms * divMult (syncDiv)
                    : std::pow (8000.0f, rawParam (ParameterIDs::SYN_DLY_TIME)->load());   // fb304 — 1 ms → 8000 ms (exp)
                delayEngine.setType      (activeDlyType_);
                delayEngine.setCharacter ((int) *rawParam (ParameterIDs::SYN_DLY_CHARACTER));
                delayEngine.setTimeMs    (timeMs);
                // fb306 — LINK + independent RIGHT time. Linked (default): DelayEngine derives R from L (+Spread) →
                // byte-identical to before. Unlinked: R uses its OWN synced division (SYNCDIV_R) or free time (TIME_R).
                const bool dlyLink = rawParam (ParameterIDs::SYN_DLY_LINK)->load() > 0.5f;
                delayEngine.setLink (dlyLink);
                if (! dlyLink)
                {
                    const int   syncDivR = (int) *rawParam (ParameterIDs::SYN_DLY_SYNCDIV_R);
                    const float timeMsR  = (sync && syncDivR > 0)
                        ? qms * divMult (syncDivR)
                        : std::pow (8000.0f, rawParam (ParameterIDs::SYN_DLY_TIME_R)->load());
                    delayEngine.setTimeMsR (timeMsR);
                }
                delayEngine.setFeedback  (rawParam (ParameterIDs::SYN_DLY_FEEDBACK)->load() * 1.2f);           // fb303 — amplified (0..120%): 100% ≈ "someone playing it back over you" (softClip-bounded in the loop, no runaway)
                delayEngine.setTone      (rawParam (ParameterIDs::SYN_DLY_TONE)->load());
                delayEngine.setLowCutHz  (20.0f   * std::pow (50.0f, rawParam (ParameterIDs::SYN_DLY_LOWCUT)->load()));  // 20..1000 Hz
                delayEngine.setHiCutHz   (1200.0f * std::pow (15.0f, rawParam (ParameterIDs::SYN_DLY_HICUT)->load()));   // 1.2k..18k Hz
                delayEngine.setSpread    (rawParam (ParameterIDs::SYN_DLY_SPREAD)->load());
                delayEngine.setWidth     (rawParam (ParameterIDs::SYN_DLY_WIDTH)->load() * 1.6f);              // 0..1.6 M/S
                delayEngine.setModRate   (0.05f + rawParam (ParameterIDs::SYN_DLY_MODRATE)->load() * 7.95f);   // 0.05..8 Hz
                delayEngine.setModDepth  (rawParam (ParameterIDs::SYN_DLY_MODDEPTH)->load());
                delayEngine.setWow       (rawParam (ParameterIDs::SYN_DLY_WOW)->load());    // fb303 — default 0 now (off); kept for Tape. Full removal + L/R redesign next.
                delayEngine.setDucking   (rawParam (ParameterIDs::SYN_DLY_DUCK)->load());   // fb303 — default 0 now (Max never liked it)
                delayEngine.setPing      (rawParam (ParameterIDs::SYN_DLY_PING)->load() > 0.5f);
                delayEngine.setHQ        (rawParam (ParameterIDs::SYN_DLY_HQ)->load()   > 0.5f);
                delayEngine.updateCoefficients();
                const float mixv = rawParam (ParameterIDs::SYN_DLY_MIX)->load();
                dlyWetT_ = std::sin (mixv * 0.5f * juce::MathConstants<float>::pi);
                dlyDryT_ = std::cos (mixv * 0.5f * juce::MathConstants<float>::pi);
            }
        }
        // ════════ fb315 — DISTORTION setup (block-rate, i==0) ════════
        if (i == 0)
        {
            dstEnvT_ = dstPower_ ? 1.0f : 0.0f;          // on/off FADE (never a hard cut)
            if (dstPower_)
            {
                distortionEngine.setMode      ((int) *rawParam (ParameterIDs::SYN_DST_TYPE));
                distortionEngine.setCharacter ((int) *rawParam (ParameterIDs::SYN_DST_CHARACTER));
                distortionEngine.setQuality   ((int) *rawParam (ParameterIDs::SYN_DST_QUALITY));
                distortionEngine.setAuto      (rawParam (ParameterIDs::SYN_DST_AUTO)->load()  > 0.5f);
                distortionEngine.setPill2     (rawParam (ParameterIDs::SYN_DST_PILL2)->load() > 0.5f);
                distortionEngine.setKeyHz     (440.0f * std::pow (2.0f, (synthGlideFrom_ - 69.0f) / 12.0f));   // fb336 — FOLD Track rides the last-played note (the glide tracker; mono law on a post-mix bus)
                // Drive is dB-linear inside the engine (48·t^0.8) — do NOT pre-scale it here into a
                // linear multiplier, that is the dead-first-third bug this device exists to avoid.
                distortionEngine.setDrive     (rawParam (ParameterIDs::SYN_DST_DRIVE)->load());
                distortionEngine.setKnee      (dstMorphEff_);   // fb340 — Morph/Knee is a first-class dest (§6.7, resolved in modP's scope): env destroys the attack, the tail stays clean
                distortionEngine.setTone      (rawParam (ParameterIDs::SYN_DST_TONE)->load());
                // fb319 — the back-8 goes in RAW; the engine interprets each slot per FAMILY. Slots 0
                // and 1 are Low Cut / Hi Cut in every family; the rest change meaning per family.
                static const char* const kDstP[8] = {
                    ParameterIDs::SYN_DST_P1, ParameterIDs::SYN_DST_P2, ParameterIDs::SYN_DST_P3,
                    ParameterIDs::SYN_DST_P4, ParameterIDs::SYN_DST_P5, ParameterIDs::SYN_DST_P6,
                    ParameterIDs::SYN_DST_P7, ParameterIDs::SYN_DST_P8 };
                for (int pIdx = 0; pIdx < 8; ++pIdx)
                    distortionEngine.setP (pIdx, rawParam (kDstP[pIdx])->load());
                // fb318 — the ENGINE owns Mix now (it is the only place the wet and the dry can be
                // latency-aligned across the 2× resampler; see DistortionEngine::setMix).
                distortionEngine.setMix (rawParam (ParameterIDs::SYN_DST_MIX)->load());
            }
        }

        auto applyDst = [&]()   // fb315 — distortion INSERT. MAIN SEND: the whole mix through it.
        {
            if (dstPower_ || dstEnv_ > 1.0e-4f)
            {
                dstEnv_ += (dstEnvT_ - dstEnv_) * hallSm_;      // on/off fade (~15 ms, no click)
                // fb338 — PHASE E: per-osc routing live. Pills lit ⇒ the engine eats ONLY the routed
                // oscs (the padded send bus, post-filter per the fb280 law); no pills ⇒ MAIN SEND =
                // the whole mix MINUS every send-routed osc (the fb305 exclusion, all three buses).
                float sgL, sgR;
                if (dstRouteActive_)
                {
                    const float rawL = (dstSendL != nullptr) ? dstSendL[i] : 0.0f;
                    const float rawR = (dstSendR != nullptr) ? dstSendR[i] : rawL;
                    sgL = rawL * outputGain * kVoiceToFxPad; sgR = rawR * outputGain * kVoiceToFxPad;
                }
                else
                {
                    const float rtdL = ((rvbSendL ? rvbSendL[i] : 0.0f) + (dlySendL ? dlySendL[i] : 0.0f) + (dstSendL ? dstSendL[i] : 0.0f)) * outputGain * kVoiceToFxPad;   /* fb338 — the fb305 law: EVERY send bus joins EVERY main-send exclusion */
                    sgL = leftChannel[i] - rtdL;
                    if (rightChannel != nullptr) { const float rtdR = ((rvbSendR ? rvbSendR[i] : 0.0f) + (dlySendR ? dlySendR[i] : 0.0f) + (dstSendR ? dstSendR[i] : 0.0f)) * outputGain * kVoiceToFxPad; sgR = rightChannel[i] - rtdR; }
                    else sgR = sgL;
                }
                float wl, wr; distortionEngine.processSample (sgL, sgR, wl, wr);
                // fb318 — ENV-GATED REPLACE. The engine returns the FINISHED signal (its own Mix
                // applied, dry latency-aligned to the 2× resampler), so the insert is just a crossfade
                // from the untouched mix to the engine's output. At env 0 this contributes EXACTLY 0 —
                // no click on power toggle, and no delay line in circuit when the device is off.
                const float e = dstEnv_;
                leftChannel[i]  += e * (wl - sgL);
                if (rightChannel != nullptr)
                    rightChannel[i] += e * (wr - sgR);
                const float wmag = 0.5f * (std::abs (wl) + std::abs (wr)) * e;
                if (wmag > dstBlockWetPk) dstBlockWetPk = wmag;
            }
        };

        auto applyDly = [&]()   // fb307 — delay INSERT as a lambda (swapped with the reverb per the drag order)
        {
        if (dlyPower_ || dlyEnv_ > 1.0e-4f)
        {
            dlyEnv_ += (dlyEnvT_ - dlyEnv_) * hallSm_;           // on/off fade
            dlyDry_ += (dlyDryT_ - dlyDry_) * hallSm_;           // ramp mix (no zipper)
            dlyWet_ += (dlyWetT_ - dlyWet_) * hallSm_;
            // fb303 — MAIN SEND (no pills): feed the WHOLE current mix (already post-reverb this block ⇒ serial)
            // as a wet/dry insert. Per-osc (pills) ⇒ the padded routed send bus, as before.
            float sgL, sgR;
            if (dlyMainSend_) {
                // fb305 — MAIN SEND excludes per-osc-routed oscs (see reverb block above): subtract the routed
                // dry so an osc routed to the reverb (or any effect) is NOT re-processed by the delay main send.
                const float rtdL = ((rvbSendL ? rvbSendL[i] : 0.0f) + (dlySendL ? dlySendL[i] : 0.0f) + (dstSendL ? dstSendL[i] : 0.0f)) * outputGain * kVoiceToFxPad;   /* fb338 — the fb305 law: EVERY send bus joins EVERY main-send exclusion */
                sgL = leftChannel[i] - rtdL;
                if (rightChannel != nullptr) { const float rtdR = ((rvbSendR ? rvbSendR[i] : 0.0f) + (dlySendR ? dlySendR[i] : 0.0f) + (dstSendR ? dstSendR[i] : 0.0f)) * outputGain * kVoiceToFxPad; sgR = rightChannel[i] - rtdR; }
                else sgR = sgL;
            }
            else { const float rawL = (dlySendL != nullptr) ? dlySendL[i] : 0.0f;
                   const float rawR = (dlySendR != nullptr) ? dlySendR[i] : rawL;
                   sgL = rawL * outputGain * kVoiceToFxPad; sgR = rawR * outputGain * kVoiceToFxPad; }
            float dl, dr; delayEngine.processSample (sgL, sgR, dl, dr);
            const float e = dlyEnv_, duck = e * (1.0f - dlyDry_), wet = e * dlyWet_;
            leftChannel[i]  += wet * dl - duck * sgL;            // Mix 100% ⇒ dry crossfade→0 ⇒ routed dry fully removed
            if (rightChannel != nullptr)
                rightChannel[i] += wet * dr - duck * sgR;
            const float wmag = 0.5f * (std::abs (dl) + std::abs (dr)) * e;
            if (wmag > dlyBlockWetPk) dlyBlockWetPk = wmag;
        }
        };   // fb307 — end applyDly

        // fb307 — SERIAL CHAIN ORDER (drag-to-reorder): run the two INSERTS in the dragged order. Both setups
        // (i==0) have run above, so either order is valid. Default reverb→delay = byte-identical to fb306. Per-osc
        // (parallel) sends are order-independent, so the swap only re-routes the MAIN-SEND serial case — exactly
        // what the drag controls (delay hears reverb, or reverb hears delay).
        // fb341 — the REAL 6-way serial permutation (bible §4.5). Per-osc (parallel) sends are
        // order-independent; the perm re-routes what the MAIN-SEND serial cases hear. Index order
        // anchors the two legacy bool states (0 = R·D·T, 5 = D·R·T) so old sessions restore exactly.
        switch (fxPerm_)
        {
            default:
            case 0: applyRvb(); applyDly(); applyDst(); break;
            case 1: applyRvb(); applyDst(); applyDly(); break;
            case 2: applyDst(); applyRvb(); applyDly(); break;
            case 3: applyDst(); applyDly(); applyRvb(); break;
            case 4: applyDly(); applyDst(); applyRvb(); break;
            case 5: applyDly(); applyRvb(); applyDst(); break;
        }
        if (i == numSamples - 1)   // fb280/fb296 — publish BOTH blooms once/block, after both inserts ran
        {
            // fb312 — INSTANT attack (peak-hold): the smoothed 0.40 attack made the viz lag the audio
            // ~20-60ms and read "late" (Max). The peak now lands the same block; only the fall smooths.
            if (hallBlockWetPk > hallBloomEnv_) hallBloomEnv_ = hallBlockWetPk;
            else                                hallBloomEnv_ += (hallBlockWetPk - hallBloomEnv_) * 0.05f;
            hallBloomViz_.store (juce::jlimit (0.0f, 1.5f, hallBloomEnv_), std::memory_order_relaxed);
            if (dlyBlockWetPk > dlyBloomEnv_)   dlyBloomEnv_ = dlyBlockWetPk;
            else                                dlyBloomEnv_ += (dlyBlockWetPk - dlyBloomEnv_) * 0.05f;
            dlyBloomViz_.store (juce::jlimit (0.0f, 1.5f, dlyBloomEnv_), std::memory_order_relaxed);
            // fb315 — distortion core viz. Same fb312 instant-attack / smoothed-fall shape, so the
            // curve's excursion glow lands on the same block as the audio instead of lagging it.
            if (dstBlockWetPk > dstBloomEnv_)   dstBloomEnv_ = dstBlockWetPk;
            else                                dstBloomEnv_ += (dstBlockWetPk - dstBloomEnv_) * 0.05f;
            dstBloomViz_.store (juce::jlimit (0.0f, 1.5f, dstBloomEnv_), std::memory_order_relaxed);
        }

        // fb249 — instrument makeup gain (Serum-matched loudness). fb264 — THEN a stereo-linked
        // peak LIMITER (gain-reduction) so dense chords stay loud without the tanh squaring them
        // into a hard-clip buzz, THEN the transparent-knee clip as a final transient safety catch.
        // Output knob (outputGain) already applied above, so it keeps full authority over this stage.
        const float mL = leftChannel[i] * kInstrumentMakeup;
        const float mR = (rightChannel != nullptr) ? rightChannel[i] * kInstrumentMakeup : mL;
        // Stereo-linked peak detector — one shared gain preserves the stereo image.
        const float mPeak = juce::jmax (std::abs (mL), std::abs (mR));
        limEnv_ = mPeak + (mPeak > limEnv_ ? limAtkCoef_ : limRelCoef_) * (limEnv_ - mPeak);
        const float limTarget = (limEnv_ <= kLimiterThresh) ? 1.0f : (kLimiterThresh / limEnv_);
        // Fast attack (pull down now), slow release (recover smoothly — click-free per the declick rule).
        limGain_ = (limTarget < limGain_) ? (limAtkCoef_ * limGain_ + (1.0f - limAtkCoef_) * limTarget)
                                          : (limRelCoef_ * limGain_ + (1.0f - limRelCoef_) * limTarget);
        leftChannel[i]  = masterSoftClip (mL * limGain_);
        if (rightChannel != nullptr)
            rightChannel[i] = masterSoftClip (mR * limGain_);

        // Write to scope buffer (mono mix for visualization)
        float scopeSample = rightChannel != nullptr ? (leftChannel[i] + rightChannel[i]) * 0.5f : leftChannel[i];
        scopeBuffer[static_cast<size_t>(scopePos)].store(scopeSample, std::memory_order_relaxed);
        scopePos = (scopePos + 1) % SCOPE_SIZE;
    }

    // Write final output to rolling capture buffer
    captureBuffer.writeBlock(leftChannel,
        numChannels > 1 ? rightChannel : nullptr, numSamples);

    // Capture the post-FX master into the masterFx ring (in lockstep with the
    // per-layer DRY rings). WET stem export uses energy-ratio attribution
    // against this ring so each layer's WET file carries its proportional
    // share of the shared FX processing.
    writeToMasterFxRing (leftChannel,
                         numChannels > 1 ? rightChannel : leftChannel,
                         numSamples);

    // Sync transport state back (auto-stop may have changed wantRecord/wantPlay)
    tapeLoopRecording.store(wantRecord ? 1.f : 0.f);
    tapeLoopPlaying.store(wantPlay ? 1.f : 0.f);

    // Auto-disable feed-to-grain when recording stops (prevents accidental feedback loops)
    if (prevProcessBlockRecording && !wantRecord)
    {
        tapeLoopFeedToGrain.store(0.f);
        // Flush grain engine circular buffers to purge stale loop content
        // that would otherwise persist for up to 5 seconds after overdub
        grainEngineL.clearBuffer();
        grainEngineR.clearBuffer();
    }
    prevProcessBlockRecording = wantRecord;

    scopeWritePos.store(scopePos);

    // Update grain count for visualization
    activeGrainCount.store(grainEngineL.getActiveGrainCount() + grainEngineR.getActiveGrainCount());

    // ── FLOW · CHOP (mode 2): audio insert — re-groove the fully-FX'd output IN PLACE,
    //    click-free (FlowChop.h). MIDI passed through normally above; this chops the mix.
    //    Engine defaults (AlwaysOn, 8 slices, full wet) groove out of the box; the 5 mode-2
    //    macros (FLOW_SEQ_* IDs, now CHOP) ride it. Always call process so it free-runs when stopped.
    if (! flowChain.chop) chopVizActive_.store (0, std::memory_order_relaxed);
    auto chopStage = [&]   // fb131 — dispatched in chain order below
    {
        const float cRate  = flowKnob (ParameterIDs::FLOW_SEQ_RATE,  wc::ModDest::ChopRate);
        const float cGate  = flowKnob (ParameterIDs::FLOW_SEQ_GATE,  wc::ModDest::ChopGate);
        const float cVary  = flowKnob (ParameterIDs::FLOW_SEQ_VARY,  wc::ModDest::ChopVary);
        const float cTraj  = flowKnob (ParameterIDs::FLOW_SEQ_TRAJ,  wc::ModDest::ChopTraj);
        const float cMorph = flowKnob (ParameterIDs::FLOW_SEQ_MORPH, wc::ModDest::ChopMorph);
        chop.setMix (flowKnob (ParameterIDs::FLOW_CHOP_BLEND, wc::ModDest::FlowChopMix));   // dry/wet (glass menu); default 0.60

        // ── fb106 extension card: every Ribbon control, read per block ──
        {
            static constexpr int kSliceL[7] = { 2, 3, 4, 6, 8, 12, 16 };
            static constexpr int kLoopL[7]  = { 2, 4, 6, 8, 10, 12, 16 };
            wc::FlowChop::ChopExtParams X;
            X.slices    = kSliceL[juce::jlimit (0, 6, (int) *rawParam (ParameterIDs::FLOW_CHOP_SLICES))];
            X.loopCells = kLoopL [juce::jlimit (0, 6, (int) *rawParam (ParameterIDs::FLOW_CHOP_LOOP))];
            X.modeOrder = (int) *rawParam (ParameterIDs::FLOW_CHOP_MODE);
            X.rpts      = 1 + (int) *rawParam (ParameterIDs::FLOW_CHOP_RPTS);
            X.filter    = (int) *rawParam (ParameterIDs::FLOW_CHOP_FILTER);
            X.freeze    = *rawParam (ParameterIDs::FLOW_CHOP_FREEZE)  > 0.5f;
            X.collect   = *rawParam (ParameterIDs::FLOW_CHOP_COLLECT) > 0.5f;
            X.scan   = flowKnob (ParameterIDs::FLOW_CHOP_SCAN, wc::ModDest::FlowChopScan);   X.wander = flowKnob (ParameterIDs::FLOW_CHOP_WANDER, wc::ModDest::FlowChopWander);
            X.spread = flowKnob (ParameterIDs::FLOW_CHOP_SPREAD, wc::ModDest::FlowChopSpread); X.speed  = flowKnob (ParameterIDs::FLOW_CHOP_SPEED, wc::ModDest::FlowChopSpeed);
            X.steps  = flowKnob (ParameterIDs::FLOW_CHOP_STEPS, wc::ModDest::FlowChopCrush);  X.detune = flowKnob (ParameterIDs::FLOW_CHOP_DETUNE, wc::ModDest::FlowChopDetune);
            X.wow    = flowKnob (ParameterIDs::FLOW_CHOP_WOW, wc::ModDest::FlowChopWow);    X.smooth = flowKnob (ParameterIDs::FLOW_CHOP_SMOOTH, wc::ModDest::FlowChopSmooth);
            X.grit   = flowKnob (ParameterIDs::FLOW_CHOP_GRIT, wc::ModDest::FlowChopGrit);   X.trim   = flowKnob (ParameterIDs::FLOW_CHOP_TRIM, wc::ModDest::FlowChopTrim);
            X.oSpread= flowKnob (ParameterIDs::FLOW_CHOP_O_SPREAD, wc::ModDest::FlowChopOSpread); X.oBias = flowKnob (ParameterIDs::FLOW_CHOP_O_BIAS, wc::ModDest::FlowChopOBias);
            X.oLock  = flowKnob (ParameterIDs::FLOW_CHOP_O_LOCK, wc::ModDest::FlowChopOLock);   X.oSeed = flowKnob (ParameterIDs::FLOW_CHOP_O_SEED, wc::ModDest::FlowChopOSeed);
            X.pRange = flowKnob (ParameterIDs::FLOW_CHOP_P_RANGE, wc::ModDest::FlowChopPRange);  X.pSteps= flowKnob (ParameterIDs::FLOW_CHOP_P_STEPS, wc::ModDest::FlowChopPSteps);
            X.pGlide = flowKnob (ParameterIDs::FLOW_CHOP_P_GLIDE, wc::ModDest::FlowChopPGlide);  X.pQuant= flowKnob (ParameterIDs::FLOW_CHOP_P_QUANT, wc::ModDest::FlowChopPQuant);
            X.rvOdds = flowKnob (ParameterIDs::FLOW_CHOP_RV_ODDS, wc::ModDest::FlowChopRvOdds);  X.rvRun = flowKnob (ParameterIDs::FLOW_CHOP_RV_RUN, wc::ModDest::FlowChopRvRun);
            X.rvSpread=flowKnob (ParameterIDs::FLOW_CHOP_RV_SPREAD, wc::ModDest::FlowChopRvSpread);X.rvSnap= flowKnob (ParameterIDs::FLOW_CHOP_RV_SNAP, wc::ModDest::FlowChopRvSnap);
            X.tLen   = flowKnob (ParameterIDs::FLOW_CHOP_T_LEN, wc::ModDest::FlowChopTLen);    X.tCurve= flowKnob (ParameterIDs::FLOW_CHOP_T_CURVE, wc::ModDest::FlowChopTCurve);
            X.tRand  = flowKnob (ParameterIDs::FLOW_CHOP_T_RAND, wc::ModDest::FlowChopTRand);   X.tGate = flowKnob (ParameterIDs::FLOW_CHOP_T_GATE, wc::ModDest::FlowChopTGate);
            X.rCount = flowKnob (ParameterIDs::FLOW_CHOP_R_COUNT, wc::ModDest::FlowChopRCount);  X.rDecay= flowKnob (ParameterIDs::FLOW_CHOP_R_DECAY, wc::ModDest::FlowChopRDecay);
            X.rCurve = flowKnob (ParameterIDs::FLOW_CHOP_R_CURVE, wc::ModDest::FlowChopRCurve);  X.rOdds = flowKnob (ParameterIDs::FLOW_CHOP_R_ODDS, wc::ModDest::FlowChopROdds);
            X.dAmt   = flowKnob (ParameterIDs::FLOW_CHOP_D_AMT, wc::ModDest::FlowChopDAmt);    X.dSize = flowKnob (ParameterIDs::FLOW_CHOP_D_SIZE, wc::ModDest::FlowChopDSize);
            X.dSpray = flowKnob (ParameterIDs::FLOW_CHOP_D_SPRAY, wc::ModDest::FlowChopDSpray);  X.dTone = flowKnob (ParameterIDs::FLOW_CHOP_D_TONE, wc::ModDest::FlowChopDTone);
            chop.setExt (X);
            chop.setMode (*rawParam (ParameterIDs::FLOW_CHOP_CATCH) > 0.5f ? wc::ChopMode::Catch
                                                                            : wc::ChopMode::AlwaysOn);
            chop.setCatchHeld (resoHeldN_ > 0);                 // CATCH rides the real held keys
            if (resoHeldN_ > 0) chop.noteOnRoot (resoHeld_[resoHeldN_ - 1]);
            if (chopWipeReq_.exchange (false)) chop.wipe();     // Wipe button (UI native)
        }

        float* cl = buffer.getWritePointer (0);
        float* cr = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : cl;
        chop.process (cRate, cGate, cVary, cTraj, cMorph,
                      flowPpq, flowBpm, getSampleRate(), cl, cr, numSamples, flowPlaying);

        // live Ribbon feed (UI rAF-polls getChopFeed)
        chopVizStepF_.store (chop.vizStepF(),             std::memory_order_relaxed);
        chopVizCount_.store ((int) chop.vizFireCount(),   std::memory_order_relaxed);
        chopVizSlice_.store (chop.lastSliceIndex(),       std::memory_order_relaxed);
        chopVizWet_.store   (chop.wetLevel(),             std::memory_order_relaxed);
        chopVizActive_.store(chop.isActive() ? 1 : 0,     std::memory_order_relaxed);
    };

    // ── FLOW · GLITCH (mode 3): audio insert — beat-synced buffer-mangler IN PLACE, click-free
    //    (FlowGlitch.h, equal-power seams + exponential tape-stop). Same insert shape as CHOP.
    //    Macros read FLOW_GLI_* (RATE/GATE/VARY=CHANCE/TRAJ=CHAOS/MORPH=SWING); BLEND default 1.0.
    auto glitchStage = [&]   // fb131 — dispatched in chain order below
    {
        const float gRate  = flowKnob (ParameterIDs::FLOW_GLI_RATE,  wc::ModDest::FlowTime);
        const float gGate  = flowKnob (ParameterIDs::FLOW_GLI_GATE,  wc::ModDest::FlowGate);
        const float gVary  = flowKnob (ParameterIDs::FLOW_GLI_VARY,  wc::ModDest::FlowVary);
        const float gTraj  = flowKnob (ParameterIDs::FLOW_GLI_TRAJ,  wc::ModDest::FlowTraj);
        const float gMorph = flowKnob (ParameterIDs::FLOW_GLI_MORPH, wc::ModDest::FlowMorph);
        glitch.setMix (flowKnob (ParameterIDs::FLOW_GLI_BLEND, wc::ModDest::FlowGliMix));   // dry/wet (card MIX header); default 0.60
        glitch.setOutMode ((int) *rawParam (ParameterIDs::FLOW_GLI_OUTMODE));   // fb142 — Mix/Cut/Gate
        glitch.setPing (flowKnob (ParameterIDs::FLOW_GLI_PING, wc::ModDest::FlowGliPing));                // fb142 — stereo bounce

        // ── fb115 extension card: every Monitor control, read per block ──
        {
            static constexpr float kHoldL[6] = { 1, 2, 3, 4, 6, 8 };
            static constexpr int   kLoopG[5] = { 2, 4, 8, 12, 16 };
            wc::GlitchExtParams X;
            X.en[0] = *rawParam (ParameterIDs::FLOW_GLI_EN_REP)  > 0.5f;
            X.en[1] = *rawParam (ParameterIDs::FLOW_GLI_EN_REV)  > 0.5f;
            X.en[2] = *rawParam (ParameterIDs::FLOW_GLI_EN_TAPE) > 0.5f;
            X.en[3] = *rawParam (ParameterIDs::FLOW_GLI_EN_GATE) > 0.5f;
            X.en[4] = *rawParam (ParameterIDs::FLOW_GLI_EN_PIT)  > 0.5f;
            X.en[5] = *rawParam (ParameterIDs::FLOW_GLI_EN_CRSH) > 0.5f;
            X.en[6] = *rawParam (ParameterIDs::FLOW_GLI_EN_FRZ)  > 0.5f;
            X.en[7] = *rawParam (ParameterIDs::FLOW_GLI_EN_SCT)  > 0.5f;
            X.dejavu = flowBase (ParameterIDs::FLOW_GLI_DEJAVU);
            X.decay  = flowKnob (ParameterIDs::FLOW_GLI_DECAY, wc::ModDest::FlowGliDecay);
            X.drop   = flowKnob (ParameterIDs::FLOW_GLI_DROP, wc::ModDest::FlowGliDrop);               // fb143 — hole fires
            X.burst  = flowKnob (ParameterIDs::FLOW_GLI_BURST, wc::ModDest::FlowGliBurst);              // fb143 — fires streak
            X.bend   = flowKnob (ParameterIDs::FLOW_GLI_BEND, wc::ModDest::FlowGliBend);
            X.seed   = (int) std::lround (flowBase (ParameterIDs::FLOW_GLI_SEED) * 99.0f);
            X.holdSteps  = kHoldL[juce::jlimit (0, 5, (int) *rawParam (ParameterIDs::FLOW_GLI_HOLD))];
            X.loopLen    = kLoopG[juce::jlimit (0, 4, (int) *rawParam (ParameterIDs::FLOW_GLI_LOOP))];
            X.quantIdx   = (int) *rawParam (ParameterIDs::FLOW_GLI_QUANT);
            X.releaseNow = (int) *rawParam (ParameterIDs::FLOW_GLI_RELEASE) == 1;
            // fb125 — per-effect Out routing (pointers cached at prepare; FLOW_GLI_FILTER/PAN
            // retired but stay registered for old sessions)
            for (int fi = 0; fi < 8; ++fi)
            {
                X.fxFlt[fi] = gliFxFltP_[fi] != nullptr ? (int) gliFxFltP_[fi]->load() : 0;
                X.fxPan[fi] = gliFxPanP_[fi] != nullptr ? (int) gliFxPanP_[fi]->load() : 1;
                X.fxTrig[fi] = gliFxTrgP_[fi] != nullptr ? (int) gliFxTrgP_[fi]->load() : 0;
                X.fxGrid[fi] = gliFxGrdP_[fi] != nullptr ? (int) gliFxGrdP_[fi]->load() : 0;
            }
            X.sync       = (int) *rawParam (ParameterIDs::FLOW_GLI_SYNC) == 1;
            X.repSize  = flowKnob (ParameterIDs::FLOW_GLI_REP_SIZE, wc::ModDest::FlowGliRepSize);   X.repSpeed = flowKnob (ParameterIDs::FLOW_GLI_REP_SPEED, wc::ModDest::FlowGliRepSpeed);
            X.repFade  = flowKnob (ParameterIDs::FLOW_GLI_REP_FADE, wc::ModDest::FlowGliRepFade);   X.repVary  = flowKnob (ParameterIDs::FLOW_GLI_REP_VARY, wc::ModDest::FlowGliRepVary);
            X.revLen   = flowKnob (ParameterIDs::FLOW_GLI_REV_LEN, wc::ModDest::FlowGliRevLen);    X.revFade  = flowKnob (ParameterIDs::FLOW_GLI_REV_FADE, wc::ModDest::FlowGliRevFade);
            X.revSprd  = flowKnob (ParameterIDs::FLOW_GLI_REV_SPRD, wc::ModDest::FlowGliRevSprd);   X.revSnap  = flowKnob (ParameterIDs::FLOW_GLI_REV_SNAP, wc::ModDest::FlowGliRevSnap);
            X.tapeCurve= flowKnob (ParameterIDs::FLOW_GLI_TAPE_CURVE, wc::ModDest::FlowGliTapeCurve); X.tapeTime = flowKnob (ParameterIDs::FLOW_GLI_TAPE_TIME, wc::ModDest::FlowGliTapeTime);
            X.tapeDepth= flowKnob (ParameterIDs::FLOW_GLI_TAPE_DEPTH, wc::ModDest::FlowGliTapeDepth); X.tapeSpin = flowKnob (ParameterIDs::FLOW_GLI_TAPE_SPIN, wc::ModDest::FlowGliTapeSpin);
            X.gateRate = flowKnob (ParameterIDs::FLOW_GLI_GATE_RATE, wc::ModDest::FlowGliGateRate);  X.gateShape= flowKnob (ParameterIDs::FLOW_GLI_GATE_SHAPE, wc::ModDest::FlowGliGateShape);
            X.gateNudge= flowKnob (ParameterIDs::FLOW_GLI_GATE_NUDGE, wc::ModDest::FlowGliGateNudge); X.gateAmt  = flowKnob (ParameterIDs::FLOW_GLI_GATE_AMT, wc::ModDest::FlowGliGateAmt);
            X.pitShift = flowKnob (ParameterIDs::FLOW_GLI_PIT_SHIFT, wc::ModDest::FlowGliPitShift);  X.pitWalk  = flowKnob (ParameterIDs::FLOW_GLI_PIT_WALK, wc::ModDest::FlowGliPitWalk);
            X.pitGlide = flowKnob (ParameterIDs::FLOW_GLI_PIT_GLIDE, wc::ModDest::FlowGliPitGlide);  X.pitJump  = flowKnob (ParameterIDs::FLOW_GLI_PIT_JUMP, wc::ModDest::FlowGliPitJump);
            X.crshBits = flowKnob (ParameterIDs::FLOW_GLI_CRSH_BITS, wc::ModDest::FlowGliCrshBits);  X.crshRate = flowKnob (ParameterIDs::FLOW_GLI_CRSH_RATE, wc::ModDest::FlowGliCrshRate);
            X.crshTone = flowKnob (ParameterIDs::FLOW_GLI_CRSH_TONE, wc::ModDest::FlowGliCrshTone);  X.crshAmt  = flowKnob (ParameterIDs::FLOW_GLI_CRSH_AMT, wc::ModDest::FlowGliCrshAmt);
            X.frzSize  = flowKnob (ParameterIDs::FLOW_GLI_FRZ_SIZE, wc::ModDest::FlowGliFrzSize);   X.frzSpray = flowKnob (ParameterIDs::FLOW_GLI_FRZ_SPRAY, wc::ModDest::FlowGliFrzSpray);
            X.frzShine = flowKnob (ParameterIDs::FLOW_GLI_FRZ_SHINE, wc::ModDest::FlowGliFrzShine);  X.frzMelt  = flowKnob (ParameterIDs::FLOW_GLI_FRZ_MELT, wc::ModDest::FlowGliFrzMelt);
            X.sctSize  = flowKnob (ParameterIDs::FLOW_GLI_SCT_SIZE, wc::ModDest::FlowGliSctSize);   X.sctAmt   = flowKnob (ParameterIDs::FLOW_GLI_SCT_AMT, wc::ModDest::FlowGliSctAmt);
            X.sctVary  = flowKnob (ParameterIDs::FLOW_GLI_SCT_VARY, wc::ModDest::FlowGliSctVary);   X.sctWidth = flowKnob (ParameterIDs::FLOW_GLI_SCT_WIDTH, wc::ModDest::FlowGliSctWidth);
            glitch.setExt (X);
            if (gliRollReq_.exchange (false)) glitch.rollNow();   // Roll button (UI native, quantized)
        }

        float* gl = buffer.getWritePointer (0);
        float* gr = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : gl;
        glitch.process (gRate, gGate, gVary, gTraj, gMorph,
                        flowPpq, flowBpm, getSampleRate(), gl, gr, numSamples, flowPlaying);

        // live Monitor feed (UI rAF-polls getGliFeed)
        gliVizStepF_.store (glitch.vizStepF16(),          std::memory_order_relaxed);
        gliVizLoopF_.store (glitch.vizLoopSlotF(),        std::memory_order_relaxed);
        gliVizFx_.store    (glitch.vizFx(),               std::memory_order_relaxed);
        gliVizFireS_.store (glitch.vizFireStep16(),       std::memory_order_relaxed);
        gliVizHold_.store  (glitch.vizHoldSteps(),        std::memory_order_relaxed);
        gliVizWet_.store   (glitch.wetLevelViz(),         std::memory_order_relaxed);
        gliVizCount_.store ((int) glitch.vizFireCount(),  std::memory_order_relaxed);
        gliVizActive_.store(glitch.isActive() ? 1 : 0,    std::memory_order_relaxed);
        gliVizOut_.store   (glitch.outLevel(),            std::memory_order_relaxed);
        for (int vi = 0; vi < 16; ++vi) gliVizLvl_[vi].store (glitch.stepLevel (vi), std::memory_order_relaxed);
    };

    // fb131 — MODE CHAIN dispatch: the audio stages run in CLICK ORDER. Chop-then-glitch
    // stutters the chopped groove; glitch-then-chop re-grooves the stutters. Each stage is
    // the untouched fb106/fb115 insert — the chain only decides who reads the buffer first.
    for (int ci = 0; ci < flowChain.len; ++ci)
    {
        if      (flowChain.order[ci] == 2) chopStage();
        else if (flowChain.order[ci] == 3) glitchStage();
    }

    // ── FLOW · DRIFT (mode 4): generative MOD SOURCE — makes no audio; advances 8 bipolar lanes
    //    (FlowDrift.h). Kept alive every block so the card scopes have live motion and the lanes
    //    are ready for the mod matrix. DEPTH = MORPH macro. (Lane→ModDest routing = mod-matrix phase.)
    if (flowChain.robin)
    {
        const float dRate  = flowKnob (ParameterIDs::FLOW_DRF_RATE,  wc::ModDest::FlowTime);
        const float dGlide = flowBase (ParameterIDs::FLOW_DRF_GATE);    // GATE macro = GLIDE/slew
        const float dVary  = flowBase (ParameterIDs::FLOW_DRF_VARY);    // VARY  = volatility/character
        const float dTraj  = flowBase (ParameterIDs::FLOW_DRF_TRAJ);    // TRAJ  = shape selector
        const float dDepth = flowKnob (ParameterIDs::FLOW_DRF_MORPH, wc::ModDest::FlowMorph);   // MORPH = DEPTH (the robin MIX header; LFO-moddable fb145)
        // fb344 SIGBUS FIX (the auval P1, shipped broken since fb122) — FlowDrift::process
        // writes a 2D LANE-MAJOR buffer: outLanes[l*numSamples+i], numLanes*numSamples floats
        // (FlowDrift.h:209; the offline harness allocates nLanes*blk). The old 8-float stack
        // array here overran by numLanes*numSamples*4 bytes EVERY Robin-active block — silent
        // caller-stack corruption at small blocks, a guard-page SIGBUS at 4096 frames (auval's
        // render test; 15/15 crash reports + live lldb: fault = lanesBase + 3*16384, proven).
        // It was ALSO a data bug: lanes[i] read lane 0's samples 0..7, never per-lane finals.
        // We only need each lane's end-of-block value = Lane::outScaled via the bounds-checked
        // value(i); process() guards `if (outLanes)`, so nullptr = zero writes, RT-safe.
        drift.process (dRate, dGlide, dVary, dTraj, dDepth,
                       flowPpq, flowBpm, getSampleRate(), nullptr, numSamples, flowPlaying);
        for (int i = 0; i < wc::kDriftLanes; ++i) driftLane_[i] = drift.value (i);

        // ── fb122 ROBIN Wheel card: the rotation brain, read per block ──
        {
            wc::RobinExtParams X;
            X.bank[0]  = *rawParam (ParameterIDs::FLOW_RBN_A) > 0.5f;
            X.bank[1]  = *rawParam (ParameterIDs::FLOW_RBN_B) > 0.5f;
            X.bank[2]  = *rawParam (ParameterIDs::FLOW_RBN_C) > 0.5f;
            X.bank[3]  = *rawParam (ParameterIDs::FLOW_RBN_D) > 0.5f;
            X.aFirst   = *rawParam (ParameterIDs::FLOW_RBN_AFIRST) > 0.5f;
            X.retrig   = *rawParam (ParameterIDs::FLOW_RBN_RETRIG) > 0.5f;
            X.mode     = (int) *rawParam (ParameterIDs::FLOW_RBN_MODE);
            X.legato   = (int) *rawParam (ParameterIDs::FLOW_RBN_LEGATO);
            X.steal    = (int) *rawParam (ParameterIDs::FLOW_RBN_STEAL);
            X.release  = (int) *rawParam (ParameterIDs::FLOW_RBN_RELEASE);
            X.times    = 1 + (int) *rawParam (ParameterIDs::FLOW_RBN_TIMES);
            X.reset    = (int) *rawParam (ParameterIDs::FLOW_RBN_RESET);
            X.backward = (int) *rawParam (ParameterIDs::FLOW_RBN_RUN) == 1;
            X.order[0] = (int) *rawParam (ParameterIDs::FLOW_RBN_O1);
            X.order[1] = (int) *rawParam (ParameterIDs::FLOW_RBN_O2);
            X.order[2] = (int) *rawParam (ParameterIDs::FLOW_RBN_O3);
            X.order[3] = (int) *rawParam (ParameterIDs::FLOW_RBN_O4);
            X.vary   = flowKnob (ParameterIDs::FLOW_RBN_VARY, wc::ModDest::FlowRbnVary);   X.wobble = flowKnob (ParameterIDs::FLOW_RBN_WOBBLE, wc::ModDest::FlowRbnWobble);
            X.lvl    = flowKnob (ParameterIDs::FLOW_RBN_LVL, wc::ModDest::FlowRbnLvl);    X.pan    = flowKnob (ParameterIDs::FLOW_RBN_PAN, wc::ModDest::FlowRbnPan);
            X.after  = 0.05f + flowKnob (ParameterIDs::FLOW_RBN_AFTER, wc::ModDest::FlowRbnAfter) * 4.95f;   // 0.05..5 s (card readout)
            X.glide  = flowKnob (ParameterIDs::FLOW_RBN_GLIDE, wc::ModDest::FlowRbnGlide);  X.overlap = flowKnob (ParameterIDs::FLOW_RBN_OVERLAP, wc::ModDest::FlowRbnOverlap);
            X.fade   = flowKnob (ParameterIDs::FLOW_RBN_FADE, wc::ModDest::FlowRbnFade);
            flowRobin_.setExt (X);
            flowRobin_.tick (flowPpq, numSamples, flowPlaying);

            // Drift: per-STATION slow pitch wander — the drift engine's first four lanes
            // (already MORPH/MIX-scaled) feed each station's cents, capped ±18 (perceptual law)
            const float dAmt = flowKnob (ParameterIDs::FLOW_RBN_DRIFT, wc::ModDest::FlowRbnDrift);
            for (int k = 0; k < 4; ++k) robinDriftCents_[k] = driftLane_[k] * dAmt * 18.0f;

            // live Wheel feed (UI rAF-polls getRbnFeed)
            rbnVizNow_.store  (flowRobin_.nowStation(),        std::memory_order_relaxed);
            rbnVizNext_.store (flowRobin_.nextStation(),       std::memory_order_relaxed);
            rbnVizNotes_.store(flowRobin_.notesOnCur(),        std::memory_order_relaxed);
            rbnVizMask_.store (flowRobin_.cycleMaskViz(),      std::memory_order_relaxed);
            rbnVizWrap_.store ((int) flowRobin_.wrapCount(),   std::memory_order_relaxed);
            rbnVizHits_.store ((int) flowRobin_.hitCount(),    std::memory_order_relaxed);
        }
    }

    // PREVIEW STOP (Max: double-click to select / closing the browser must SILENCE the audition immediately —
    // otherwise a sample's ~3.5s tail "keeps fucking playing"). Kills both one-shots this block.
    if (previewStop_.exchange (false, std::memory_order_relaxed))
    {
        // Fade out (not an abrupt cut → no click) and cancel any queued preview.
        const int fl = juce::jmax (1, (int) (getSampleRate() * 0.008));
        if (noiseAudCtr_ > 0 && noiseAudFade_ <= 0) { noiseAudFadeLen_ = fl; noiseAudFade_ = fl; }
        if (wtAudCtr_    > 0 && wtAudFade_    <= 0) { wtAudFadeLen_    = fl; wtAudFade_    = fl; }
        if (sampAudCtr_  > 0 && sampAudFade_  <= 0) { sampAudFadeLen_  = fl; sampAudFade_  = fl; }   // fb74 — sample preview too
        noiseAudCtr_ = 0; wtAudCtr_ = 0; sampAudCtr_ = 0;
        noiseAudPending_ = false; wtAudPending_ = false; sampAudPending_ = false;
    }

    // ── NOISE AUDITION (browser headphone preview) — ONE-SHOT, re-triggerable (Max: "play once, re-trigger,
    //    don't loop"). On each trigger: if a sample is loaded → play it once start-to-end (cap 3.5 s); else →
    //    generate the CURRENT algorithmic type for a ~1.1 s burst via the faithful NoisePreviewGen (fixes the
    //    old "built-in types are silent" bug — they had no buffer). Keyless, mixed post-FX, soft-clipped. CPU:
    //    only during an active preview.
    {
        const double sr = getSampleRate();
        // Start a fresh preview: capture + HOLD the current buffer so scanning doesn't bleed (the shared buffer
        // swaps under the playing preview otherwise), and decide sample-vs-type from the committed state.
        auto startNoisePreview = [this, sr]()
        {
            noiseAudPos_   = 0.0;
            noiseAudHeld_  = noiseSampleBuffer_.load();
            noiseAudRatio_ = (noiseSampleBuffer_.getSampleRate() > 0.0) ? (noiseSampleBuffer_.getSampleRate() / sr) : 1.0;
            if (noiseAudHeld_ != nullptr && noiseAudHeld_->getNumSamples() > 1)
            {
                const double outLen = (double) noiseAudHeld_->getNumSamples() / juce::jmax (1.0e-6, noiseAudRatio_);
                noiseAudType_ = -1;                                                                  // sample one-shot
                noiseAudCtr_  = (int) juce::jmin (sr * 3.5, outLen);
            }
            else
            {
                noiseAudType_ = (int) *apvts.getRawParameterValue (ParameterIDs::SYN_NOISE_TYPE);    // choice index 0..12
                noisePrevGen_.setSR ((float) sr); noisePrevGen_.setType (noiseAudType_); noisePrevGen_.reset();
                noiseAudCtr_  = (int) (sr * 1.10);                                                   // ~1.1 s burst
            }
            noiseAudTotal_ = juce::jmax (1, noiseAudCtr_);
        };

        const int req = noiseAuditionReq_.load (std::memory_order_relaxed);
        if (req != noiseAudSeen_)
        {
            noiseAudSeen_ = req;
            if (noiseAudCtr_ > 0 || noiseAudFade_ > 0)   // already sounding → fade the current one OUT first (declick), queue the new
            {
                noiseAudFadeLen_ = juce::jmax (1, (int) (sr * 0.008));   // ~8 ms
                if (noiseAudFade_ <= 0) noiseAudFade_ = noiseAudFadeLen_;
                noiseAudPending_ = true;
            }
            else startNoisePreview();
        }

        if ((noiseAudCtr_ > 0 || noiseAudFade_ > 0) && buffer.getNumChannels() >= 1)
        {
            const float atk  = (float) (sr * 0.012);
            const float rel  = (float) (sr * 0.13);
            const bool  fade = noiseAudFade_ > 0;
            float* oL = buffer.getWritePointer (0);
            float* oR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : oL;
            auto nb = noiseAudHeld_;   // the HELD buffer (stable — no mid-scan swap)
            const int    nlen = (nb != nullptr) ? nb->getNumSamples() : 0;
            const float* nL   = (nb != nullptr) ? nb->getReadPointer (0) : nullptr;
            const float* nR   = (nb != nullptr && nb->getNumChannels() > 1) ? nb->getReadPointer (1) : nL;
            if (noiseAudType_ < 0 && (nb == nullptr || nlen < 2)) { noiseAudCtr_ = 0; noiseAudFade_ = 0; }
            for (int i = 0; i < numSamples; ++i)
            {
                if (fade) { if (noiseAudFade_ <= 0) break; } else { if (noiseAudCtr_ <= 0) break; }
                float sL, sR;
                if (noiseAudType_ < 0)   // sample one-shot (no loop — stop at end)
                {
                    const int i0 = (int) noiseAudPos_;
                    if (i0 >= nlen - 1) { if (fade) noiseAudFade_ = 0; else noiseAudCtr_ = 0; break; }
                    const int   i1 = i0 + 1;
                    const float fr = (float) (noiseAudPos_ - (double) i0);
                    sL = nL[i0] + (nL[i1] - nL[i0]) * fr;
                    sR = nR[i0] + (nR[i1] - nR[i0]) * fr;
                    noiseAudPos_ += noiseAudRatio_;
                }
                else noisePrevGen_.tick (sL, sR);       // algorithmic type — real DSP

                float g;
                if (fade) { g = (noiseAudType_ < 0 ? 0.55f : 0.42f) * ((float) noiseAudFade_ / (float) noiseAudFadeLen_); --noiseAudFade_; }
                else
                {
                    const float elapsed = (float) (noiseAudTotal_ - noiseAudCtr_);
                    float env = 1.0f;
                    if (elapsed < atk)               env = elapsed / atk;
                    if ((float) noiseAudCtr_ < rel)  env = juce::jmin (env, (float) noiseAudCtr_ / rel);
                    g = (noiseAudType_ < 0 ? 0.55f : 0.42f) * juce::jlimit (0.0f, 1.0f, env);
                    --noiseAudCtr_;
                }
                if (noiseAudType_ < 0) { oL[i] += sL * g; oR[i] += sR * g; }
                else                   { oL[i] += std::tanh (sL * g); oR[i] += std::tanh (sR * g); }   // soft-clip hot types
            }
            if (noiseAudFade_ <= 0 && noiseAudPending_) { noiseAudPending_ = false; startNoisePreview(); }   // fade done → start the queued one (next block)
        }
    }

    // ── WAVETABLE AUDITION (browser headphone preview) — ONE-SHOT plucked note of the osc's CURRENT table at a
    //    fixed pitch (C3), with a slow frame-scan (0→1) so you hear the whole table morph. Table resolved exactly
    //    like the voice/viz: imported override else the factory bank. Keyless, mixed post-FX, band-limited via mip.
    {
        const double sr = getSampleRate();
        auto startWtPreview = [this, sr]()
        {
            wtAudOsc_   = juce::jlimit (0, 3, wtAudReqOsc_.load (std::memory_order_relaxed));
            wtAudPhase_ = 0.0;
            wtAudInc_   = 130.81 / sr;                 // C3
            wtAudCtr_   = (int) (sr * 0.95);
            wtAudTotal_ = juce::jmax (1, wtAudCtr_);
        };
        const int req = wtAuditionReq_.load (std::memory_order_relaxed);
        if (req != wtAudSeen_)
        {
            wtAudSeen_ = req;
            if (wtAudCtr_ > 0 || wtAudFade_ > 0)   // already sounding → fade OUT first (declick), queue the new
            {
                wtAudFadeLen_ = juce::jmax (1, (int) (sr * 0.008));
                if (wtAudFade_ <= 0) wtAudFade_ = wtAudFadeLen_;
                wtAudPending_ = true;
            }
            else startWtPreview();
        }
        if ((wtAudCtr_ > 0 || wtAudFade_ > 0) && buffer.getNumChannels() >= 1)
        {
            const int o = wtAudOsc_;
            const tw::Wavetable* wt = importSlot_[o].live.load (std::memory_order_acquire);
            if (wt == nullptr)
            {
                static const char* const WTP[4] = { ParameterIDs::SYN_OSC_A_WT_PRESET, ParameterIDs::SYN_OSC_B_WT_PRESET,
                                                    ParameterIDs::SYN_OSC_C_WT_PRESET, ParameterIDs::SYN_OSC_D_WT_PRESET };
                wt = wavetableBank.getTable ((int) *apvts.getRawParameterValue (WTP[o]));
            }
            if (wt == nullptr) { wtAudCtr_ = 0; wtAudFade_ = 0; }
            else
            {
                const int   mip  = tw::Wavetable::mipLevelForPhaseIncrement (wtAudInc_);
                const float atk  = (float) (sr * 0.010);
                const float rel  = (float) (sr * 0.16);
                const bool  fade = wtAudFade_ > 0;
                float* oL = buffer.getWritePointer (0);
                float* oR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : oL;
                for (int i = 0; i < numSamples; ++i)
                {
                    if (fade) { if (wtAudFade_ <= 0) break; } else { if (wtAudCtr_ <= 0) break; }
                    const float framePos = juce::jlimit (0.0f, 1.0f, (float) (wtAudTotal_ - wtAudCtr_) / (float) wtAudTotal_);   // frozen during fade
                    const float s = wt->lookup (mip, framePos, (float) wtAudPhase_);
                    float g;
                    if (fade) { g = 0.42f * ((float) wtAudFade_ / (float) wtAudFadeLen_); --wtAudFade_; }
                    else
                    {
                        const float elapsed = (float) (wtAudTotal_ - wtAudCtr_);
                        float env = 1.0f;
                        if (elapsed < atk)            env = elapsed / atk;
                        if ((float) wtAudCtr_ < rel)  env = juce::jmin (env, (float) wtAudCtr_ / rel);
                        g = 0.42f * juce::jlimit (0.0f, 1.0f, env);
                        --wtAudCtr_;
                    }
                    oL[i] += s * g; oR[i] += s * g;
                    wtAudPhase_ += wtAudInc_; if (wtAudPhase_ >= 1.0) wtAudPhase_ -= 1.0;
                }
                if (wtAudFade_ <= 0 && wtAudPending_) { wtAudPending_ = false; startWtPreview(); }
            }
        }
    }

    // ── SAMPLE AUDITION (fb74 — browser headphone preview for Sample/Granular/Resynth) — ONE-SHOT of the
    //    osc's CURRENT sample buffer at its native pitch (keyless), capped 3.5 s, HELD at trigger so a fast
    //    scan doesn't swap the buffer under the playing preview. Same fades/retrigger declick as the noise
    //    preview. Mixed post-FX; CPU only while actively previewing.
    {
        const double sr = getSampleRate();
        auto startSampPreview = [this, sr]()
        {
            sampAudOsc_   = juce::jlimit (0, 3, sampAudReqOsc_.load (std::memory_order_relaxed));
            sampAudPos_   = 0.0;
            sampAudHeld_  = oscSampleBuffers_[(size_t) sampAudOsc_].load();
            const double srcRate = oscSampleBuffers_[(size_t) sampAudOsc_].getSampleRate();
            sampAudRatio_ = (srcRate > 0.0) ? (srcRate / sr) : 1.0;
            if (sampAudHeld_ != nullptr && sampAudHeld_->getNumSamples() > 1)
            {
                const double outLen = (double) sampAudHeld_->getNumSamples() / juce::jmax (1.0e-6, sampAudRatio_);
                sampAudCtr_ = (int) juce::jmin (sr * 3.5, outLen);
            }
            else sampAudCtr_ = 0;   // nothing loaded on this osc → silent (the browser click still committed the load; audition follows next click)
            sampAudTotal_ = juce::jmax (1, sampAudCtr_);
        };
        const int req = sampAuditionReq_.load (std::memory_order_relaxed);
        if (req != sampAudSeen_)
        {
            sampAudSeen_ = req;
            if (sampAudCtr_ > 0 || sampAudFade_ > 0)   // already sounding → fade OUT first (declick), queue the new
            {
                sampAudFadeLen_ = juce::jmax (1, (int) (sr * 0.008));
                if (sampAudFade_ <= 0) sampAudFade_ = sampAudFadeLen_;
                sampAudPending_ = true;
            }
            else startSampPreview();
        }
        if ((sampAudCtr_ > 0 || sampAudFade_ > 0) && buffer.getNumChannels() >= 1)
        {
            const float atk  = (float) (sr * 0.012);
            const float rel  = (float) (sr * 0.13);
            const bool  fade = sampAudFade_ > 0;
            float* oL = buffer.getWritePointer (0);
            float* oR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : oL;
            auto nb = sampAudHeld_;   // the HELD buffer (stable — no mid-scan swap)
            const int    nlen = (nb != nullptr) ? nb->getNumSamples() : 0;
            const float* nL   = (nb != nullptr) ? nb->getReadPointer (0) : nullptr;
            const float* nR   = (nb != nullptr && nb->getNumChannels() > 1) ? nb->getReadPointer (1) : nL;
            if (nb == nullptr || nlen < 2) { sampAudCtr_ = 0; sampAudFade_ = 0; }
            for (int i = 0; i < numSamples; ++i)
            {
                if (fade) { if (sampAudFade_ <= 0) break; } else { if (sampAudCtr_ <= 0) break; }
                const int i0 = (int) sampAudPos_;
                if (i0 >= nlen - 1) { if (fade) sampAudFade_ = 0; else sampAudCtr_ = 0; break; }
                const int   i1 = i0 + 1;
                const float fr = (float) (sampAudPos_ - (double) i0);
                const float sL = nL[i0] + (nL[i1] - nL[i0]) * fr;
                const float sR = nR[i0] + (nR[i1] - nR[i0]) * fr;
                sampAudPos_ += sampAudRatio_;
                float g;
                if (fade) { g = 0.55f * ((float) sampAudFade_ / (float) sampAudFadeLen_); --sampAudFade_; }
                else
                {
                    const float elapsed = (float) (sampAudTotal_ - sampAudCtr_);
                    float env = 1.0f;
                    if (elapsed < atk)              env = elapsed / atk;
                    if ((float) sampAudCtr_ < rel)  env = juce::jmin (env, (float) sampAudCtr_ / rel);
                    g = 0.55f * juce::jlimit (0.0f, 1.0f, env);
                    --sampAudCtr_;
                }
                oL[i] += sL * g; oR[i] += sR * g;
            }
            if (sampAudFade_ <= 0 && sampAudPending_) { sampAudPending_ = false; startSampPreview(); }
        }
    }

    // (ANNULUS RESONATOR moved UP to the synth-section output — pre-FX — see above.)
}

//==============================================================================
juce::AudioProcessorEditor* TerrainInstrumentAudioProcessor::createEditor()
{
    return new TerrainInstrumentAudioProcessorEditor(*this);
}

bool TerrainInstrumentAudioProcessor::hasEditor() const { return true; }

const juce::String TerrainInstrumentAudioProcessor::getName() const { return JucePlugin_Name; }
bool TerrainInstrumentAudioProcessor::acceptsMidi() const { return true; }
bool TerrainInstrumentAudioProcessor::producesMidi() const { return false; }
bool TerrainInstrumentAudioProcessor::isMidiEffect() const { return false; }
double TerrainInstrumentAudioProcessor::getTailLengthSeconds() const { return 5.0; }

int TerrainInstrumentAudioProcessor::getNumPrograms() { return static_cast<int>(presets.size()); }
int TerrainInstrumentAudioProcessor::getCurrentProgram() { return currentPresetIndex.load(); }
void TerrainInstrumentAudioProcessor::setCurrentProgram (int index) { loadPreset(index); }
const juce::String TerrainInstrumentAudioProcessor::getProgramName (int index) { return getPresetName(index); }
void TerrainInstrumentAudioProcessor::changeProgramName (int, const juce::String&) {}

void TerrainInstrumentAudioProcessor::setLoadedSamplePath (const juce::String& path)
{
    juce::ScopedLock sl (sampleSourcePathLock);
    loadedSamplePath = path;
}

juce::String TerrainInstrumentAudioProcessor::getLoadedSamplePath() const
{
    juce::ScopedLock sl (sampleSourcePathLock);
    return loadedSamplePath;
}

// ── Mix page Phase D: rolling stem buffers ───────────────────────────────────

void TerrainInstrumentAudioProcessor::allocateStemBuffers (double sampleRate)
{
    const int totalSamples = juce::jmax (1, (int) (sampleRate * (double) kStemSeconds));
    for (auto& s : stemBuffers)
    {
        s.ring.setSize (2, totalSamples, false, true, true);
        s.ring.clear();
        s.totalSize = totalSamples;
        s.writeIndex.store     (0, std::memory_order_relaxed);
        s.samplesWritten.store (0, std::memory_order_relaxed);
    }
    masterFxBuffer.ring.setSize (2, totalSamples, false, true, true);
    masterFxBuffer.ring.clear();
    masterFxBuffer.totalSize = totalSamples;
    masterFxBuffer.writeIndex.store     (0, std::memory_order_relaxed);
    masterFxBuffer.samplesWritten.store (0, std::memory_order_relaxed);
}

void TerrainInstrumentAudioProcessor::writeToMasterFxRing (const float* L,
                                                            const float* R,
                                                            int numSamples)
{
    if (numSamples <= 0) return;
    auto& s = masterFxBuffer;
    if (s.totalSize <= 0) return;

    int w = s.writeIndex.load (std::memory_order_relaxed);
    auto* destL = s.ring.getWritePointer (0);
    auto* destR = s.ring.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        destL[w] = L[i];
        destR[w] = R[i];
        if (++w >= s.totalSize) w = 0;
    }
    s.writeIndex.store (w, std::memory_order_relaxed);
    const int prev = s.samplesWritten.load (std::memory_order_relaxed);
    s.samplesWritten.store (juce::jmin (s.totalSize, prev + numSamples),
                             std::memory_order_relaxed);
}

void TerrainInstrumentAudioProcessor::writeToStemBuffer (int layerIdx,
                                                          const float* L,
                                                          const float* R,
                                                          int numSamples)
{
    if (layerIdx < 0 || layerIdx > 3 || numSamples <= 0) return;
    auto& s = stemBuffers[(size_t) layerIdx];
    if (s.totalSize <= 0) return;

    // Live capture-level meter: decaying peak of what's being written. Read by
    // the UI at ~30Hz to drive the 4 mini-meters on the stem row.
    {
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
        const float prev = stemCaptureLevel[(size_t) layerIdx].load (std::memory_order_relaxed);
        stemCaptureLevel[(size_t) layerIdx].store (juce::jmax (peak, prev * 0.92f),
                                                    std::memory_order_relaxed);
    }

    int w = s.writeIndex.load (std::memory_order_relaxed);
    auto* destL = s.ring.getWritePointer (0);
    auto* destR = s.ring.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        destL[w] = L[i];
        destR[w] = R[i];
        if (++w >= s.totalSize) w = 0;
    }
    s.writeIndex.store (w, std::memory_order_relaxed);
    // Saturate samplesWritten at totalSize so we know when the ring is fully populated.
    const int prev = s.samplesWritten.load (std::memory_order_relaxed);
    s.samplesWritten.store (juce::jmin (s.totalSize, prev + numSamples),
                             std::memory_order_relaxed);
}

void TerrainInstrumentAudioProcessor::clearStemBuffers()
{
    for (int i = 0; i < 4; ++i)
    {
        auto& s = stemBuffers[(size_t) i];
        if (s.totalSize > 0) s.ring.clear();
        s.writeIndex.store     (0, std::memory_order_relaxed);
        s.samplesWritten.store (0, std::memory_order_relaxed);
        stemCaptureLevel[(size_t) i].store (0.0f, std::memory_order_relaxed);
    }
    if (masterFxBuffer.totalSize > 0) masterFxBuffer.ring.clear();
    masterFxBuffer.writeIndex.store     (0, std::memory_order_relaxed);
    masterFxBuffer.samplesWritten.store (0, std::memory_order_relaxed);
}

juce::File TerrainInstrumentAudioProcessor::exportStemToFile (int layerIdx, const juce::File& dest)
{
    if (layerIdx < 0 || layerIdx > 3) return {};
    auto& s = stemBuffers[(size_t) layerIdx];
    if (s.totalSize <= 0) return {};

    // Only export what's actually been captured. Below totalSize, the ring
    // hasn't wrapped yet — audio sits chronologically at [0..captured-1] and
    // we skip the silent tail. Once captured == totalSize, full rolling unwrap.
    const int captured = s.samplesWritten.load (std::memory_order_relaxed);
    if (captured <= 0) return {};                       // nothing recorded yet

    // Snapshot current write position. Audio thread may continue writing during
    // the unwrap loop below — at most one sample tear at the wrap boundary,
    // inaudible in practice.
    const int writeIdx = s.writeIndex.load (std::memory_order_relaxed);

    const int  mode     = stemSourceMode.load();
    const bool ringFull = (captured >= s.totalSize);
    const int  outLen   = ringFull ? s.totalSize : captured;

    juce::AudioBuffer<float> out (2, outLen);
    auto* dstL = out.getWritePointer (0);
    auto* dstR = out.getWritePointer (1);

    if (mode == 0)   // DRY — raw per-layer ring, no gain.
    {
        const auto* srcL = s.ring.getReadPointer (0);
        const auto* srcR = s.ring.getReadPointer (1);
        if (ringFull)
        {
            int srcIdx = writeIdx;
            for (int i = 0; i < outLen; ++i)
            {
                dstL[i] = srcL[srcIdx];
                dstR[i] = srcR[srcIdx];
                if (++srcIdx >= s.totalSize) srcIdx = 0;
            }
        }
        else
        {
            for (int i = 0; i < outLen; ++i) { dstL[i] = srcL[i]; dstR[i] = srcR[i]; }
        }
    }
    else             // WET — attribute the post-FX master ring back to this layer
                     // by per-sample energy ratio (computed from all 4 dry rings
                     // weighted by each layer's volume). Sum of the 4 layers'
                     // WET stems == the master FX output.
    {
        const auto* mL = masterFxBuffer.ring.getReadPointer (0);
        const auto* mR = masterFxBuffer.ring.getReadPointer (1);
        const float* dryL[4]; const float* dryR[4];
        float        vol[4];
        for (int j = 0; j < 4; ++j)
        {
            dryL[j] = stemBuffers[(size_t) j].ring.getReadPointer (0);
            dryR[j] = stemBuffers[(size_t) j].ring.getReadPointer (1);
            vol[j]  = juce::jmax (0.0f, layers[(size_t) j].volume.load());
        }
        constexpr float kEps = 1.0e-7f;
        for (int i = 0; i < outLen; ++i)
        {
            const int srcIdx = ringFull ? ((writeIdx + i) % s.totalSize) : i;
            float total = kEps;
            float strengths[4];
            for (int j = 0; j < 4; ++j)
            {
                strengths[j] = (std::abs (dryL[j][srcIdx]) + std::abs (dryR[j][srcIdx])) * vol[j];
                total       += strengths[j];
            }
            const float ratio = strengths[layerIdx] / total;
            dstL[i] = mL[srcIdx] * ratio;
            dstR[i] = mR[srcIdx] * ratio;
        }
    }

    // Filename: Stem-<A/B/C/D>-YYYYMMDD-HHMMSS-{DRY|MIX}.wav
    const auto letter    = juce::String::charToString ((juce::juce_wchar)('A' + layerIdx));
    const auto timestamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    const auto suffix    = (mode == 1) ? juce::String("-WET") : juce::String("-DRY");
    const auto file      = dest.getChildFile ("Stem-" + letter + "-" + timestamp + suffix + ".wav");

    if (! dest.exists()) dest.createDirectory();

    const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr || ! stream->openedOk()) return {};
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (
        stream.get(), sr, 2 /*channels*/, 24 /*bit depth*/, {}, 0));
    if (writer == nullptr) return {};
    stream.release();  // writer takes ownership
    writer->writeFromAudioSampleBuffer (out, 0, outLen);
    // writer destructor finalizes the WAV chunks on scope exit
    return file;
}

void TerrainInstrumentAudioProcessor::setCachedSamplePayload (const juce::String& jsonPayload, int layerIdx)
{
    if (layerIdx < 0)         layerIdx = editingLayer.load();
    if (layerIdx < 0 || layerIdx > 3) return;
    juce::ScopedLock sl (samplePayloadLock);
    cachedLayerPayloads[(size_t) layerIdx] = jsonPayload;
}

juce::String TerrainInstrumentAudioProcessor::getCachedSamplePayload (int layerIdx) const
{
    if (layerIdx < 0)         layerIdx = editingLayer.load();
    if (layerIdx < 0 || layerIdx > 3) return {};
    juce::ScopedLock sl (samplePayloadLock);
    return cachedLayerPayloads[(size_t) layerIdx];
}

std::array<juce::String, 4> TerrainInstrumentAudioProcessor::getAllLayerPayloads() const
{
    juce::ScopedLock sl (samplePayloadLock);
    return cachedLayerPayloads;   // returns a copy under the lock
}

//==============================================================================
// Slicer state management
//==============================================================================
void TerrainInstrumentAudioProcessor::replaceSlices (tw::SliceList newSlices)
{
    // Task 5: routes through layers[editingLayer] instead of singleton slicesPtr/activeSliceIndex/synth.
    const size_t el = (size_t) editingLayer.load();
    auto snapshot = std::make_shared<const tw::SliceList> (std::move (newSlices));
    std::atomic_store (&layers[el].currentSlices, tw::SliceListPtr (snapshot));

    // Clamp activeSliceIndex to new range.
    const int n = (int) snapshot->size();
    int idx = layers[el].activeSliceIndex.load();
    if (n == 0) idx = 0;
    else if (idx >= n) idx = n - 1;
    else if (idx < 0) idx = 0;
    layers[el].activeSliceIndex.store (idx);

    // Push fresh slice bounds into the warp cache so prewarm() callers
    // that fire shortly after (e.g. setSliceScanEnabled native fn) have
    // valid bounds for the new layout.
    //
    // NOTE: do NOT call invalidateSlice() here. setSliceBounds() already
    // drops cache entries whose bounds changed (the only mutation that
    // makes a cached render stale). Calling invalidateSlice()
    // unconditionally was discarding renders-in-flight triggered by the
    // immediately-preceding prewarm() call in setSliceScanEnabled — the
    // race caused a cache-miss-every-note loop on Warp+Scan chops.
    for (int i = 0; i < n; ++i)
    {
        const auto& s = (*snapshot)[(size_t) i];
        layers[el].synth.warpCache.setSliceBounds (i, s.startSample, s.endSample);
    }
}

tw::SliceListPtr TerrainInstrumentAudioProcessor::loadSlices() const
{
    // Task 5: routes through layers[editingLayer].currentSlices.
    const size_t el = (size_t) editingLayer.load();
    return std::atomic_load (&layers[el].currentSlices);
}

int TerrainInstrumentAudioProcessor::getNumSlices() const
{
    auto p = loadSlices();
    return p ? (int) p->size() : 0;
}

juce::String TerrainInstrumentAudioProcessor::getSlicesJson() const
{
    auto p = loadSlices();
    if (! p) return "{\"slices\":[]}";
    return tw::slicesToJson (*p);
}

void TerrainInstrumentAudioProcessor::setSlicesFromJson (const juce::String& json)
{
    replaceSlices (tw::slicesFromJson (json));
}

// Synth mod-matrix: JS pushes an array [{ "s":src, "d":dest, "v":depth }, …]. Parse it
// (message thread) into a thread-safe route list the audio thread copies each block.
void TerrainInstrumentAudioProcessor::setSynthModMatrix (const juce::String& json)
{
    std::vector<SynModRoute> parsed;
    auto v = juce::JSON::parse (json);
    if (auto* arr = v.getArray())
    {
        for (auto& item : *arr)
        {
            SynModRoute r;
            r.src   = (int)   item.getProperty ("s", 0);
            r.dest  = (int)   item.getProperty ("d", 0);
            r.depth = (float) (double) item.getProperty ("v", 0.0);
            const bool lfoSrc = (r.src >= 0 && r.src < wc::NUM_LFOS);
            const bool envSrc = (r.src >= wc::kEnvSrcBase && r.src < wc::kEnvSrcBase + 32);   // fb178
            const bool velSrc = (r.src == wc::kVelSrc);   // fb260 — Velocity source
            if (! lfoSrc && ! envSrc && ! velSrc)                    continue;
            if (r.dest < 0 || r.dest >= (int) wc::ModDest::NumDests) continue;
            r.depth = juce::jlimit (-1.0f, 1.0f, r.depth);
            if (parsed.size() < (size_t) wc::MAX_ASSIGNMENTS) parsed.push_back (r);
        }
    }
    // fb178 — which envelopes feed GLOBAL (processor-side) dests → the mono tap
    uint32_t gm = 0;
    for (const auto& r : parsed)
        if (r.src >= wc::kEnvSrcBase && r.src < wc::kEnvSrcBase + 32)
            gm |= (1u << (r.src - wc::kEnvSrcBase));   // ANY env route arms the mono tap (flow knobs sit below Res1)
    monoEnvGlobalMask_.store (gm, std::memory_order_release);
    const juce::ScopedLock sl (synModLock);
    synModRoutes = std::move (parsed);
    synModJson   = json;
}

// ── fb177 — DYNAMIC ENVELOPES (Env 6..32): the UI pushes {n, e:[{dl,a,h,d,s,r,ca,
// cd,cr,lp}..]} in natural units. Same lifecycle as the mod-matrix blob: parse on
// the message thread, swap under lock, version-bump; audio thread copies once per
// change (try-lock — never blocks) and broadcasts to the voices.
void TerrainInstrumentAudioProcessor::setSynthDynEnvs (const juce::String& json)
{
    auto v = juce::JSON::parse (json);
    if (! v.isObject()) return;
    DynEnvShape parsed[kMaxDynEnvs];
    const int n = juce::jlimit (0, kMaxDynEnvs, (int) v.getProperty ("n", 0));
    if (auto* arr = v.getProperty ("e", juce::var()).getArray())
        for (int i = 0; i < arr->size() && i < n; ++i)
        {
            const auto& it = (*arr)[i]; DynEnvShape sh;
            sh.dl   = juce::jlimit (0.0f, 8000.0f, (float) (double) it.getProperty ("dl", 0.0));
            sh.a    = juce::jlimit (1.0f, 8000.0f, (float) (double) it.getProperty ("a",  5.0));
            sh.h    = juce::jlimit (0.0f, 8000.0f, (float) (double) it.getProperty ("h",  0.0));
            sh.d    = juce::jlimit (1.0f, 8000.0f, (float) (double) it.getProperty ("d",  200.0));
            sh.s    = juce::jlimit (0.0f, 1.0f,    (float) (double) it.getProperty ("s",  0.7));
            sh.r    = juce::jlimit (1.0f, 8000.0f, (float) (double) it.getProperty ("r",  300.0));
            sh.ca   = juce::jlimit (-1.0f, 1.0f,   (float) (double) it.getProperty ("ca", 0.0));
            sh.cd   = juce::jlimit (-1.0f, 1.0f,   (float) (double) it.getProperty ("cd", 0.0));
            sh.cr   = juce::jlimit (-1.0f, 1.0f,   (float) (double) it.getProperty ("cr", 0.0));
            sh.loop = (bool) it.getProperty ("lp", false);
            parsed[i] = sh;
        }
    {
        const juce::ScopedLock sl2 (dynEnvLock_);
        for (int i = 0; i < kMaxDynEnvs; ++i) dynEnvShapes_[i] = (i < n ? parsed[i] : DynEnvShape());
        dynEnvCount_ = n;
        dynEnvJson_  = json;
    }
    dynEnvVersion_.fetch_add (1, std::memory_order_release);
}

juce::String TerrainInstrumentAudioProcessor::getSynthDynEnvsJson() const
{
    const juce::ScopedLock sl2 (dynEnvLock_);
    return dynEnvJson_.isNotEmpty() ? dynEnvJson_ : juce::String ("{\"n\":0,\"e\":[]}");
}

// ── LFO ARC L1 — THE SHAPER. The UI pushes {"shapes":[{"n":1..10,"pts":[[x,y,c],..],
// "gh":8,"gv":8,"sn":1},..]} (sparse per-LFO entries; pts sorted by x, y in 0..1 with
// 1 = top, c = the bias tension of the segment AFTER the point). The bake uses the
// EXACT env-editor curve ((e^{Pt}-1)/(e^P-1), P = -c·8 — TerrainEnvelope's biasCurve)
// so the drawn graph is literally what you hear; y maps to bipolar 2y-1 (house LFO
// grammar), and SynthLFO's 2.5ms output slew declicks drawn steps at the source.
static float lfoShapeBias (float t, float c) noexcept
{
    if (std::fabs (c) < 1.0e-4f) return t;
    const float P = -c * 8.0f;
    return (std::exp (P * t) - 1.0f) / (std::exp (P) - 1.0f);
}

void TerrainInstrumentAudioProcessor::bakeLfoPathTable (const LfoShapePtM* pts, int np, float* tb) noexcept
{
    // fb239 — PATH: traverse the free 2D drawing at constant speed (arc-length), output = Y.
    // A path can loop/double-back (multi-valued in x), so we parameterise by cumulative length,
    // never by x. Periodic: slot [wc::kLfoTableN] == [0] so the cycle wraps (the 2.5ms slew declicks
    // any open-path seam).
    float seglen[160]; float total = 0.0f;
    for (int i = 0; i < np - 1; ++i)
    {
        const float dx = pts[i + 1].x - pts[i].x, dy = pts[i + 1].y - pts[i].y;
        seglen[i] = std::sqrt (dx * dx + dy * dy); total += seglen[i];
    }
    if (total < 1.0e-6f) { const float y0 = 2.0f * pts[0].y - 1.0f; for (int k = 0; k <= wc::kLfoTableN; ++k) tb[k] = y0; return; }
    int seg = 0; float acc = 0.0f;
    for (int k = 0; k < wc::kLfoTableN; ++k)
    {
        const float target = (float) k / (float) wc::kLfoTableN * total;
        while (seg < np - 2 && acc + seglen[seg] < target) { acc += seglen[seg]; ++seg; }
        const float sp = (seglen[seg] > 1.0e-6f) ? (target - acc) / seglen[seg] : 0.0f;
        const float y  = pts[seg].y + (pts[seg + 1].y - pts[seg].y) * sp;
        tb[k] = 2.0f * y - 1.0f;
    }
    tb[wc::kLfoTableN] = tb[0];
}

void TerrainInstrumentAudioProcessor::bakeLfoShapeTable (const LfoShapePtM* pts, int np, float* tb) noexcept
{
    int seg = 0;
    for (int k = 0; k < wc::kLfoTableN; ++k)
    {
        const float p = (float) k / (float) wc::kLfoTableN;
        while (seg < np - 2 && pts[seg + 1].x <= p) ++seg;   // zero-width segs = hard steps (later point rules)
        const auto& a = pts[seg]; const auto& b = pts[seg + 1];
        const float w = b.x - a.x;
        const float y = (w <= 1.0e-6f) ? b.y
                      : a.y + (b.y - a.y) * lfoShapeBias ((p - a.x) / w, a.c);
        tb[k] = 2.0f * y - 1.0f;
    }
    tb[wc::kLfoTableN] = tb[0];   // wrap guard sample
}

void TerrainInstrumentAudioProcessor::setSynthLfoShapes (const juce::String& json)
{
    auto v = juce::JSON::parse (json);
    if (! v.isObject()) return;
    auto* arr = v.getProperty ("shapes", juce::var()).getArray();
    if (arr == nullptr) return;
    const juce::ScopedLock sl (lfoShapeLock_);
    for (const auto& e : *arr)
    {
        const int n = (int) e.getProperty ("n", 0);
        if (n < 1 || n > wc::NUM_LFOS) continue;
        auto* pa = e.getProperty ("pts", juce::var()).getArray();
        if (pa == nullptr || pa->size() < 2) continue;
        LfoShapePtM pts[160]; int np = 0;   // fb213 cap · fb238 — points may carry per-axis mods at index 3
        for (const auto& pv : *pa)
        {
            if (np >= 160) break;
            auto* t = pv.getArray(); if (t == nullptr || t->size() < 2) continue;
            LfoShapePtM P;
            P.x = juce::jlimit (0.0f, 1.0f, (float) (double) (*t)[0]);
            P.y = juce::jlimit (0.0f, 1.0f, (float) (double) (*t)[1]);
            P.c = t->size() > 2 ? juce::jlimit (-1.0f, 1.0f, (float) (double) (*t)[2]) : 0.0f;
            if (t->size() > 3 && (*t)[3].isObject())   // fb238 — {xs,xa,ys,ya} rides at index 3
            {
                const auto& m3 = (*t)[3];
                P.xs = juce::jlimit (0, wc::NUM_LFOS, (int) m3.getProperty ("xs", 0));
                P.xa = juce::jlimit (-1.0f, 1.0f, (float) (double) m3.getProperty ("xa", 0.0));
                P.ys = juce::jlimit (0, wc::NUM_LFOS, (int) m3.getProperty ("ys", 0));
                P.ya = juce::jlimit (-1.0f, 1.0f, (float) (double) m3.getProperty ("ya", 0.0));
            }
            pts[np++] = P;
        }
        if (np < 2) continue;
        const bool pathMode = ((int) e.getProperty ("pm", 0)) != 0;   // fb239 — Path bakes by arc-length, no sort
        if (pathMode)
        {
            bakeLfoPathTable (pts, np, lfoTableShared_[n - 1]);
            lfoPtNpShared_[n - 1] = 0; lfoPtHasModShared_[n - 1] = false;   // Path carries no per-point mods
        }
        else
        {
            std::sort (pts, pts + np, [] (const LfoShapePtM& a, const LfoShapePtM& b) { return a.x < b.x; });
            pts[0].x = 0.0f; pts[np - 1].x = 1.0f;   // endpoints pin the cycle
            bakeLfoShapeTable (pts, np, lfoTableShared_[n - 1]);
            std::memcpy (lfoPtShared_[n - 1], pts, sizeof (LfoShapePtM) * (size_t) np);   // fb238 — keep the list for live re-bakes
            lfoPtNpShared_[n - 1] = np;
            { bool hm = false; for (int i2 = 0; i2 < np; ++i2) if (pts[i2].xs > 0 || pts[i2].ys > 0) { hm = true; break; }
              lfoPtHasModShared_[n - 1] = hm; }
        }
        {   // fb228 — blob v2: per-LFO MOTION rides beside the points (absent = the LfoMotion defaults; fb235 — FREE)
            auto mo = e.getProperty ("mo", juce::var());
            auto& M = lfoMotionShared_[n - 1];
            if (mo.isObject())
            {
                M.tg  = juce::jlimit (0, 2, (int) mo.getProperty ("tg", 0));   // fb235 — absent tg = Free
                M.lb  = juce::jlimit (-1.0f, 0.99f, (float) (double) mo.getProperty ("lb", -1.0));
                M.dir = juce::jlimit (0, 2, (int) mo.getProperty ("dir", 0));
                M.mn  = ((int) mo.getProperty ("mn", 0)) ? 1 : 0;
                M.ri  = juce::jlimit (0.0f, 5000.0f, (float) (double) mo.getProperty ("ri", 0.0));
                M.de  = juce::jlimit (0.0f, 5000.0f, (float) (double) mo.getProperty ("de", 0.0));
                M.sm  = juce::jlimit (0.0f, 2000.0f, (float) (double) mo.getProperty ("sm", 0.0));
                M.sw  = juce::jlimit (0.0f, 1.0f,    (float) (double) mo.getProperty ("sw", 0.0));
                M.td  = juce::jlimit (0, 2, (int) mo.getProperty ("td", 0));
                M.ho  = ((int) mo.getProperty ("ho", 1)) ? 1 : 0;
                M.rs  = ((int) mo.getProperty ("rs", 0)) ? 1 : 0;   // fb245 — per-note reseed
                M.pol = juce::jlimit (0, 2, (int) mo.getProperty ("pol", 0));   // fb246 — polarity (0 Bi · 1 Uni+ · 2 Uni−)
            }
        }
    }
    lfoShapesJson_ = json;
    lfoShapeVersion_.fetch_add (1, std::memory_order_release);
}

juce::String TerrainInstrumentAudioProcessor::getSynthLfoShapesJson() const
{
    const juce::ScopedLock sl (lfoShapeLock_);
    return lfoShapesJson_.isNotEmpty() ? lfoShapesJson_ : juce::String ("{\"shapes\":[]}");
}

// ═══ fb328 — the DISTORTION curve-card blob (mirror of the LFO shaper transport) ═══════════════
// { a:[[x,y,c]…], b:[[x,y,c]…], bars:[16], src:"Tube" } — pts in [0,1]², tension c ∈ [−1,1].
// Banks bake to dense transfer tables HERE on the message thread; the engine picks them up at its
// 64-sample dirty cadence and lands them through the 40 ms output crossfade (bible §6.5).
static void dstBakePts (const juce::var& pv, float* out, int n)
{
    struct P { float x, y, c; };
    P pts[32]; int np = 0;                                   // §6.8 — 32-pt cap (more = alias generators)
    if (auto* pa = pv.getArray())
        for (const auto& e : *pa)
        {
            if (np >= 32) break;
            auto* t = e.getArray(); if (t == nullptr || t->size() < 2) continue;
            pts[np++] = { juce::jlimit (0.0f, 1.0f, (float) (double) (*t)[0]),
                          juce::jlimit (0.0f, 1.0f, (float) (double) (*t)[1]),
                          t->size() > 2 ? juce::jlimit (-1.0f, 1.0f, (float) (double) (*t)[2]) : 0.0f };
        }
    if (np < 2) { for (int i = 0; i < n; ++i) out[i] = (float) i / (float) (n - 1) * 2.0f - 1.0f; return; }
    std::sort (pts, pts + np, [] (const P& a, const P& b) { return a.x < b.x; });
    pts[0].x = 0.0f; pts[np - 1].x = 1.0f;
    int seg = 0;
    for (int i = 0; i < n; ++i)
    {
        const float p = (float) i / (float) (n - 1);
        while (seg < np - 2 && pts[seg + 1].x <= p) ++seg;
        const float w = juce::jmax (1.0e-6f, pts[seg + 1].x - pts[seg].x);
        float t = juce::jlimit (0.0f, 1.0f, (p - pts[seg].x) / w);
        const float c = pts[seg].c;                          // the ONE house tension formula (:8046 twin)
        if (std::fabs (c) > 1.0e-4f) { const float P8 = -c * 8.0f; t = (std::exp (P8 * t) - 1.0f) / (std::exp (P8) - 1.0f); }
        out[i] = (pts[seg].y + (pts[seg + 1].y - pts[seg].y) * t) * 2.0f - 1.0f;   // NO periodicity (§6.8-1)
    }
}

void TerrainInstrumentAudioProcessor::setDistortionCurves (const juce::String& json)
{
    auto v = juce::JSON::parse (json);
    if (! v.isObject()) return;
    // fb341-sweep — SINGLE-WRITER LAW (the fb238 architecture): when per-point mods are live the
    // AUDIO thread owns the engine bake (the block-rate rebake below picks up the new points via
    // the version bump within one block); the message thread baking too was a dual-writer race
    // on uCv_ under drag+mod. Pre-scan for mods; skip the direct bake when any are present.
    bool anyPtMod = false;
    {
        static const char* const bks0[4] = { "a", "b", "c", "d" };
        for (int bk = 0; bk < 4 && ! anyPtMod; ++bk)
            if (auto* pa = v.getProperty (bks0[bk], juce::var()).getArray())
                for (const auto& e : *pa)
                {
                    auto* t = e.getArray();
                    if (t != nullptr && t->size() > 3 && (*t)[3].isObject())
                    {
                        const auto& m = (*t)[3];
                        if ((int) m.getProperty ("ys", 0) > 0 || (int) m.getProperty ("xs", 0) > 0) { anyPtMod = true; break; }
                    }
                }
    }
    if (v.getProperty ("a", juce::var()).getArray() != nullptr)
    {
        // fb330 — FOUR banks (A→B→C→D on one Morph). Absent banks keep the engine's generated defaults.
        float a[257], b[257], c[257], d[257];
        dstBakePts (v.getProperty ("a", juce::var()), a, 257);
        const bool hasB = v.getProperty ("b", juce::var()).getArray() != nullptr;
        const bool hasC = v.getProperty ("c", juce::var()).getArray() != nullptr;
        const bool hasD = v.getProperty ("d", juce::var()).getArray() != nullptr;
        if (hasB) dstBakePts (v.getProperty ("b", juce::var()), b, 257);
        if (hasC) dstBakePts (v.getProperty ("c", juce::var()), c, 257);
        if (hasD) dstBakePts (v.getProperty ("d", juce::var()), d, 257);
        if (! anyPtMod)
            distortionEngine.setUserCurves (a, hasB ? b : nullptr, hasC ? c : nullptr, hasD ? d : nullptr, 257);
    }
    else distortionEngine.clearUserCurves();
    if (auto* ba = v.getProperty ("bars", juce::var()).getArray())
    {
        float bars[16] {};
        for (int i = 0; i < 16 && i < ba->size(); ++i)
            bars[i] = juce::jlimit (-1.0f, 1.0f, (float) (double) (*ba)[i]);
        distortionEngine.setHarmonicBars (bars);
    }
    { const juce::ScopedLock sl (dstCurveLock_); dstCurvesJson_ = json; }
    // fb340 — capture the point lists + per-point mods for the block-rate rebake (fb238 handoff:
    // shared written here, version bumped after; the audio thread copies on version change).
    {
        static const char* const bks[4] = { "a", "b", "c", "d" };
        bool hasMod = false;
        for (int bk = 0; bk < 4; ++bk)
        {
            int np = 0;
            if (auto* pa = v.getProperty (bks[bk], juce::var()).getArray())
                for (const auto& e : *pa)
                {
                    if (np >= 32) break;
                    auto* t = e.getArray(); if (t == nullptr || t->size() < 2) continue;
                    auto& q = dstPtShared_[bk][np];
                    q = LfoShapePtM{};
                    q.x = juce::jlimit (0.0f, 1.0f, (float) (double) (*t)[0]);
                    q.y = juce::jlimit (0.0f, 1.0f, (float) (double) (*t)[1]);
                    if (t->size() > 2) q.c = juce::jlimit (-1.0f, 1.0f, (float) (double) (*t)[2]);
                    if (t->size() > 3 && (*t)[3].isObject())
                    {
                        const auto& m = (*t)[3];
                        q.xs = juce::jlimit (0, 10, (int) m.getProperty ("xs", 0));
                        q.xa = juce::jlimit (-1.0f, 1.0f, (float) (double) m.getProperty ("xa", 0.0));
                        q.ys = juce::jlimit (0, 10, (int) m.getProperty ("ys", 0));
                        q.ya = juce::jlimit (-1.0f, 1.0f, (float) (double) m.getProperty ("ya", 0.0));
                        if (q.ys > 0 || q.xs > 0) hasMod = true;
                    }
                    ++np;
                }
            dstPtNpShared_[bk] = np;
        }
        dstPtHasModShared_ = hasMod;
        dstPtVersion_.fetch_add (1, std::memory_order_release);
    }
}

juce::String TerrainInstrumentAudioProcessor::getDistortionCurvesJson() const
{
    const juce::ScopedLock sl (dstCurveLock_);
    return dstCurvesJson_.isNotEmpty() ? dstCurvesJson_ : juce::String ("{}");
}

juce::String TerrainInstrumentAudioProcessor::getDistortionCurveVizJson()
{
    // fb328 — the §5.8 live core feed: {m,b,c[128],o[48]} — every mode's real stateless transfer at
    // the CURRENT knobs + where the signal actually lives on it. UI rAF-polls this (~15 Hz).
    float cv[128], oc[48];
    distortionEngine.sampleCurve (cv, 128);
    distortionEngine.copyOcc (oc);
    juce::String s; s.preallocateBytes (1600);
    s << "{\"m\":" << (int) *rawParam (ParameterIDs::SYN_DST_TYPE) << ",\"b\":"
      << juce::String (dstBloomViz_.load (std::memory_order_relaxed), 3) << ",\"c\":[";
    for (int i = 0; i < 128; ++i) { if (i) s << ','; s << juce::String (cv[i], 3); }
    s << "],\"o\":[";
    for (int i = 0; i < 48; ++i) { if (i) s << ','; s << juce::String (oc[i], 3); }
    s << "]}";
    return s;
}

// ── FLOW · ARP extension lanes (fb105): JS pushes the 7×16 pattern as one JSON blob
// (mod-matrix lifecycle: parse on the message thread, swap under lock, version-bump
// so the audio thread copies into the engine exactly once per change).
void TerrainInstrumentAudioProcessor::setArpLanesFromJson (const juce::String& json)
{
    auto v = juce::JSON::parse (json);
    if (! v.isObject()) return;
    wc::ArpLaneData l;
    l.steps = juce::jlimit (1, wc::kArpLaneMax, (int) v.getProperty ("steps", 16));
    auto fill = [&] (const char* key, float* dst, float lo, float hi, float def)
    {
        for (int i = 0; i < wc::kArpLaneMax; ++i) dst[i] = def;
        if (auto* arr = v.getProperty (key, juce::var()).getArray())
            for (int i = 0; i < arr->size() && i < wc::kArpLaneMax; ++i)
                dst[i] = juce::jlimit (lo, hi, (float) (double) (*arr)[i]);
    };
    fill ("pitch",   l.pitch,   -1.0f, 6.0f, 3.0f);   // −1 = rest
    fill ("gate",    l.gate,    0.05f, 1.0f, 0.72f);
    fill ("vel",     l.vel,     0.0f,  1.0f, 0.8f);
    fill ("oct",     l.oct,     -1.0f, 1.0f, 0.0f);
    fill ("ratchet", l.ratchet, 1.0f,  4.0f, 1.0f);
    fill ("prob",    l.prob,    0.0f,  1.0f, 1.0f);
    fill ("wt",      l.wt,      0.0f,  1.0f, 0.5f);
    {
        const juce::ScopedLock sl (arpLaneLock_);
        arpLanesShared_ = l;
        arpLanesJson_   = json;
    }
    arpLanesVersion_.fetch_add (1, std::memory_order_release);
}

juce::String TerrainInstrumentAudioProcessor::getArpLanesJson() const
{
    const juce::ScopedLock sl (arpLaneLock_);
    if (arpLanesJson_.isNotEmpty()) return arpLanesJson_;
    // no push yet → serialize the engine defaults so the card always boots on DSP truth
    auto arr = [&] (const float* v) {
        juce::String o ("[");
        for (int i = 0; i < arpLanesShared_.steps; ++i) { if (i) o << ","; o << juce::String (v[i], 3); }
        return o + "]"; };
    juce::String j ("{\"steps\":");
    j << arpLanesShared_.steps
      << ",\"pitch\""   << ":" << arr (arpLanesShared_.pitch)
      << ",\"gate\""    << ":" << arr (arpLanesShared_.gate)
      << ",\"vel\""     << ":" << arr (arpLanesShared_.vel)
      << ",\"oct\""     << ":" << arr (arpLanesShared_.oct)
      << ",\"ratchet\"" << ":" << arr (arpLanesShared_.ratchet)
      << ",\"prob\""    << ":" << arr (arpLanesShared_.prob)
      << ",\"wt\""      << ":" << arr (arpLanesShared_.wt) << "}";
    return j;
}

juce::String TerrainInstrumentAudioProcessor::getArpFeedJson() const
{
    const float sf = arpVizStepF_.load (std::memory_order_relaxed);
    juce::String j ("{\"s\":");
    j << juce::String (std::isfinite (sf) ? sf : 0.0f, 3)
      << ",\"c\"" << ":" << arpVizCount_.load (std::memory_order_relaxed)
      << ",\"n\"" << ":" << arpVizNote_.load (std::memory_order_relaxed)
      << ",\"v\"" << ":" << arpVizVel_.load (std::memory_order_relaxed)
      << ",\"a\"" << ":" << arpVizActive_.load (std::memory_order_relaxed)
      << ",\"b\"" << ":" << juce::String (juce::jlimit (1.0f, 999.0f, currentBPM.load()), 2)
      << ",\"m\"" << ":" << (int) apvts.getRawParameterValue (ParameterIDs::FLOW_MODE)->load()
      << ",\"on\":" << (flowChainNow().arp ? 1 : 0)
      << ",\"pl\":" << flowPlayingViz_.load (std::memory_order_relaxed) << "}";   // fb131/137 — chain membership + transport
    return j;
}

juce::String TerrainInstrumentAudioProcessor::getChopFeedJson() const
{
    const float sf = chopVizStepF_.load (std::memory_order_relaxed);
    const float wt = chopVizWet_.load (std::memory_order_relaxed);
    juce::String j ("{\"s\":");
    j << juce::String (std::isfinite (sf) ? sf : 0.0f, 3)
      << ",\"c\""  << ":" << chopVizCount_.load (std::memory_order_relaxed)
      << ",\"sl\"" << ":" << chopVizSlice_.load (std::memory_order_relaxed)
      << ",\"a\""  << ":" << chopVizActive_.load (std::memory_order_relaxed)
      << ",\"w\""  << ":" << juce::String (std::isfinite (wt) ? wt : 0.0f, 3)
      << ",\"b\""  << ":" << juce::String (juce::jlimit (1.0f, 999.0f, currentBPM.load()), 2)
      << ",\"m\""  << ":" << (int) apvts.getRawParameterValue (ParameterIDs::FLOW_MODE)->load()
      << ",\"on\":" << (flowChainNow().chop ? 1 : 0)
      << ",\"pl\":" << flowPlayingViz_.load (std::memory_order_relaxed) << "}";   // fb131/137
    return j;
}

juce::String TerrainInstrumentAudioProcessor::getGliFeedJson() const
{
    // fb115 — Monitor snapshot: step16 playhead, loop slot, firing fx + start/hold,
    // wet, fires, seed, bpm, mode, and the 16-slot input level history for the bars.
    const float sf = gliVizStepF_.load (std::memory_order_relaxed);
    const float lf = gliVizLoopF_.load (std::memory_order_relaxed);
    const float fs = gliVizFireS_.load (std::memory_order_relaxed);
    const float hl = gliVizHold_.load  (std::memory_order_relaxed);
    const float wt = gliVizWet_.load   (std::memory_order_relaxed);
    juce::String j ("{\"s\":");
    j << juce::String (std::isfinite (sf) ? sf : 0.0f, 3)
      << ",\"ls\"" << ":" << juce::String (std::isfinite (lf) ? lf : 0.0f, 3)
      << ",\"f\""  << ":" << gliVizFx_.load (std::memory_order_relaxed)
      << ",\"fs\"" << ":" << juce::String (std::isfinite (fs) ? fs : 0.0f, 2)
      << ",\"hl\"" << ":" << juce::String (std::isfinite (hl) ? hl : 1.0f, 1)
      << ",\"w\""  << ":" << juce::String (std::isfinite (wt) ? wt : 0.0f, 3)
      << ",\"a\""  << ":" << gliVizActive_.load (std::memory_order_relaxed)
      << ",\"c\""  << ":" << gliVizCount_.load (std::memory_order_relaxed)
      << ",\"sd\"" << ":" << (int) std::lround (apvts.getRawParameterValue (ParameterIDs::FLOW_GLI_SEED)->load() * 99.0f)
      << ",\"b\""  << ":" << juce::String (juce::jlimit (1.0f, 999.0f, currentBPM.load()), 2)
      << ",\"m\""  << ":" << (int) apvts.getRawParameterValue (ParameterIDs::FLOW_MODE)->load()
      << ",\"on\":" << (flowChainNow().glitch ? 1 : 0)   // fb131 — chain membership
      << ",\"pl\":" << flowPlayingViz_.load (std::memory_order_relaxed)
      << ",\"ol\"" << ":" << juce::String (juce::jlimit (0.0f, 1.5f, gliVizOut_.load (std::memory_order_relaxed)), 3)
      << ",\"lv\":[";
    for (int i = 0; i < 16; ++i)
    {
        const float l = gliVizLvl_[i].load (std::memory_order_relaxed);
        j << (i ? "," : "") << juce::jlimit (0, 99, (int) std::lround (std::sqrt (l > 0.f ? l : 0.f) * 125.0f));
    }
    j << "]}";
    return j;
}

juce::String TerrainInstrumentAudioProcessor::getRbnFeedJson() const
{
    // fb122 — Wheel snapshot: current/next station, notes-on-station, cycle mask,
    // wrap count (chain), hit count (pulse), mode + flow mode + bpm.
    juce::String j ("{\"now\":");
    j << rbnVizNow_.load (std::memory_order_relaxed)
      << ",\"nx\""  << ":" << rbnVizNext_.load (std::memory_order_relaxed)
      << ",\"nc\""  << ":" << rbnVizNotes_.load (std::memory_order_relaxed)
      << ",\"mask\""<< ":" << rbnVizMask_.load (std::memory_order_relaxed)
      << ",\"wr\""  << ":" << rbnVizWrap_.load (std::memory_order_relaxed)
      << ",\"c\""   << ":" << rbnVizHits_.load (std::memory_order_relaxed)
      << ",\"md\""  << ":" << (int) apvts.getRawParameterValue (ParameterIDs::FLOW_RBN_MODE)->load()
      << ",\"b\""   << ":" << juce::String (juce::jlimit (1.0f, 999.0f, currentBPM.load()), 2)
      << ",\"m\""   << ":" << (int) apvts.getRawParameterValue (ParameterIDs::FLOW_MODE)->load()
      << ",\"on\":" << (flowChainNow().robin ? 1 : 0)
      << ",\"pl\":" << flowPlayingViz_.load (std::memory_order_relaxed) << "}";   // fb131/137
    return j;
}

juce::String TerrainInstrumentAudioProcessor::getPitchSliceJson() const
{
    // Serialise pitchModeSlice as a single-element object so JS can use
    // a consistent schema with no special-casing on the network boundary.
    // Task 5: routes through layers[editingLayer].pitchModeSlice.
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    const size_t el = (size_t) editingLayer.load();
    const auto& s = layers[el].pitchModeSlice;
    obj->setProperty ("startSample",  (juce::int64) s.startSample);
    obj->setProperty ("endSample",    (juce::int64) s.endSample);
    obj->setProperty ("reverse",      s.reverse);
    obj->setProperty ("pitch",        (double) s.pitchOffsetSemis);
    obj->setProperty ("warpMode",     (int) s.warpMode);
    obj->setProperty ("stretchRatio", (double) s.stretchRatio);
    obj->setProperty ("attackMs",     (double) s.attackMs);
    obj->setProperty ("releaseMs",    (double) s.releaseMs);
    obj->setProperty ("decayMs",      (double) s.decayMs);
    obj->setProperty ("sustainLevel", (double) s.sustainLevel);
    obj->setProperty ("volume",       (double) s.volume);
    obj->setProperty ("scanEnabled",  s.scanEnabled);
    obj->setProperty ("scanRate",     (double) s.scanRate);
    obj->setProperty ("scanWindow",   (double) s.scanWindow);
    return juce::JSON::toString (juce::var (obj.get()), true);
}

juce::var TerrainInstrumentAudioProcessor::snapshotSliceGlowLevels() const
{
    // Task 5: reads from layers[editingLayer].sliceGlowLevel (singleton removed).
    const size_t el = (size_t) editingLayer.load();
    const int n = juce::jmin (getNumSlices(), tw::kMaxGlowSlots);
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated (n);
    for (int i = 0; i < n; ++i)
        arr.add ((double) layers[el].sliceGlowLevel[(size_t) i].load (std::memory_order_relaxed));
    return juce::var (arr);
}

float TerrainInstrumentAudioProcessor::getScanPosition (int sliceIndex) const noexcept
{
    // Task 5: routes through layers[editingLayer].synth.
    const size_t el = (size_t) editingLayer.load();
    for (int i = 0; i < layers[el].synth.getNumVoices(); ++i)
    {
        if (auto* v = dynamic_cast<const tw::SamplerVoice*> (layers[el].synth.getVoice (i)))
        {
            if (v->isPlaying()
                && v->getSliceIndex() == sliceIndex
                && v->isScanActive())
            {
                return v->getScanPositionNormalized();
            }
        }
    }
    return -1.0f;
}

TerrainInstrumentAudioProcessor::ScanWindowBounds
TerrainInstrumentAudioProcessor::getScanWindowBounds (int sliceIndex) const noexcept
{
    // Task 5: routes through layers[editingLayer].synth.
    const size_t el = (size_t) editingLayer.load();
    for (int i = 0; i < layers[el].synth.getNumVoices(); ++i)
    {
        if (auto* v = dynamic_cast<const tw::SamplerVoice*> (layers[el].synth.getVoice (i)))
        {
            if (v->isPlaying()
                && v->getSliceIndex() == sliceIndex
                && v->isScanActive())
            {
                const float window = v->getScanWindowLive();
                const float margin = (1.0f - window) * 0.5f;
                return { margin, 1.0f - margin };
            }
        }
    }
    return { 0.0f, 1.0f };
}

void TerrainInstrumentAudioProcessor::auditionSlice (int sliceIndex)
{
    const int n = getNumSlices();
    if (sliceIndex < 0 || sliceIndex >= n) return;

    const juce::SpinLock::ScopedLockType sl (auditionLock);
    auditionQueue.push_back (sliceIndex);
}

//==============================================================================
void TerrainInstrumentAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // DAW state: parameter values + preset index + XY auto state
    // Presets themselves live on disk only (getUserPresetsFile)
    auto state = apvts.copyState();
    state.setProperty("presetIndex",      currentPresetIndex.load(),  nullptr);
    state.setProperty("xyAutoEnabled",    xyAutoEnabled.load(),     nullptr);
    state.setProperty("xyAutoMode",       xyAutoMode.load(),        nullptr);
    state.setProperty("xyAutoSpeed",      xyAutoSpeed.load(),       nullptr);
    state.setProperty("grainSyncEnabled",  grainSyncEnabled.load(),  nullptr);
    state.setProperty("grainEngineEnabled", grainEngineEnabled.load(), nullptr);
    state.setProperty("tapeEnabled",        tapeEnabled.load(),        nullptr);
    state.setProperty("tapeLoopEnabled",    tapeLoopEnabled.load(),    nullptr);
    state.setProperty("wanderLinked",        wanderLinked.load(),        nullptr);
    state.setProperty("wireSpaceNoise",     wireSpaceNoiseEnabled.load(), nullptr);
    state.setProperty("wireTubeSat",        wireTubeSatEnabled.load(),    nullptr);
    state.setProperty("tapeLinkEnabled",    tapeLinkEnabled.load(),       nullptr);
    state.setProperty("chorusEnabled",      chorusEnabled.load(),         nullptr);
    state.setProperty("delayEnabled",       delayEnabled.load(),          nullptr);
    if (modStateJson.isNotEmpty())
        state.setProperty("modStateJson", modStateJson, nullptr);
    {
        const juce::ScopedLock sl (synModLock);
        if (synModJson.isNotEmpty())
            state.setProperty("synModJson", synModJson, nullptr);
        {
            const juce::ScopedLock del (dynEnvLock_);
            if (dynEnvJson_.isNotEmpty())
                state.setProperty ("dynEnvJson", dynEnvJson_, nullptr);   // fb177
        }
    }
    {
        const juce::ScopedLock sl (arpLaneLock_);
        if (arpLanesJson_.isNotEmpty())
            state.setProperty ("arpLanesJson", arpLanesJson_, nullptr);   // FLOW · ARP lane pattern (fb105)
    }
    {
        const juce::ScopedLock lsl (lfoShapeLock_);
        if (lfoShapesJson_.isNotEmpty())
            state.setProperty ("lfoShapesJson", lfoShapesJson_, nullptr);   // LFO ARC L1 — drawn shapes
        if (dstCurvesJson_.isNotEmpty())
            state.setProperty ("dstCurvesJson", dstCurvesJson_, nullptr);   // fb328 — drawn distortion curves
        if (dstTableSrc_ >= 0)
            state.setProperty ("dstTableSrc", dstTableSrc_, nullptr);       // fb339 — Table source pill
    }
    if (noiseSampleSelJson_.isNotEmpty())
        state.setProperty ("noiseSampleSel", noiseSampleSelJson_, nullptr);   // NOISE IMPORT (P5c) — factory/user selection
    if (noiseVizMode_ != 1)
        state.setProperty ("noiseVizMode", noiseVizMode_, nullptr);   // fb66 — noise waveform/particle viz choice (default particle)

    // ── V2 format marker ─────────────────────────────────────────────────────
    // Task 12: introduce version=2 and editingLayer so Task 13 (setStateInformation)
    // can distinguish V1 blobs (no "version" property) from V2 blobs.
    state.setProperty ("version",      2,                   nullptr);
    state.setProperty ("editingLayer", editingLayer.load(), nullptr);
    // Mix page Phase 2: global trigger-mode state at the root level.
    state.setProperty ("triggerMode",     triggerMode.load(),     nullptr);
    state.setProperty ("rrSyncToBar",     (bool) rrSyncToBar.load(), nullptr);
    state.setProperty ("rrShuffle",       (bool) rrShuffle.load(), nullptr);
    state.setProperty ("layerMorph",      (double) layerMorph.load(), nullptr);
    state.setProperty ("stemSourceMode",  stemSourceMode.load(),  nullptr);

    // ── V2: per-layer state node array ───────────────────────────────────────
    // Helper lambda — serialises a pitchModeSlice into the same JSON string
    // format that V1 used, so Task 13 can reuse the identical parse path
    // when restoring per-layer pitchModeSlice from V2.
    auto pitchSliceToJson = [] (const tw::Slice& s) -> juce::String
    {
        juce::DynamicObject::Ptr pObj = new juce::DynamicObject();
        pObj->setProperty ("startSample",  (juce::int64) s.startSample);
        pObj->setProperty ("endSample",    (juce::int64) s.endSample);
        pObj->setProperty ("reverse",      s.reverse);
        pObj->setProperty ("pitch",        (double) s.pitchOffsetSemis);
        pObj->setProperty ("warpMode",     (int) s.warpMode);
        pObj->setProperty ("stretchRatio", (double) s.stretchRatio);
        pObj->setProperty ("attackMs",     (double) s.attackMs);
        pObj->setProperty ("releaseMs",    (double) s.releaseMs);
        pObj->setProperty ("decayMs",      (double) s.decayMs);
        pObj->setProperty ("sustainLevel", (double) s.sustainLevel);
        pObj->setProperty ("volume",       (double) s.volume);
        pObj->setProperty ("scanEnabled",  s.scanEnabled);
        pObj->setProperty ("scanRate",     (double) s.scanRate);
        pObj->setProperty ("scanWindow",   (double) s.scanWindow);
        return juce::JSON::toString (juce::var (pObj.get()), true);
    };

    juce::ValueTree layersTree ("layers");
    for (size_t li = 0; li < layers.size(); ++li)
    {
        const auto& L = layers[li];

        juce::ValueTree layerNode ("layer");
        layerNode.setProperty ("index",            (int) li,                    nullptr);
        layerNode.setProperty ("sourcePath",       L.sourcePath,                nullptr);
        layerNode.setProperty ("sourceFileName",   L.sourceFileName,            nullptr);
        layerNode.setProperty ("activeSliceIndex", L.activeSliceIndex.load(),   nullptr);
        layerNode.setProperty ("rootMidiNote",     L.rootMidiNote.load(),       nullptr);
        layerNode.setProperty ("sliceMode",        L.sliceMode.load(),          nullptr);
        layerNode.setProperty ("playMode",         L.playMode.load(),           nullptr);
        layerNode.setProperty ("sliceCount",       L.sliceCount.load(),         nullptr);
        layerNode.setProperty ("chopFadeMs",       L.chopFadeMs.load(),         nullptr);
        layerNode.setProperty ("volume",           L.volume.load(),             nullptr);
        layerNode.setProperty ("mute",             (bool) L.mute.load(),        nullptr);
        layerNode.setProperty ("solo",             (bool) L.solo.load(),        nullptr);
        // Mix page Phase 2: per-layer creative-routing + mixer fields.
        layerNode.setProperty ("pan",              (double) L.pan.load(),               nullptr);
        layerNode.setProperty ("pitchJitterCents", (double) L.pitchJitterCents.load(),  nullptr);
        layerNode.setProperty ("probabilityWeight",(double) L.probabilityWeight.load(), nullptr);
        layerNode.setProperty ("keyZoneMin",       L.keyZoneMin.load(),                 nullptr);
        layerNode.setProperty ("keyZoneMax",       L.keyZoneMax.load(),                 nullptr);
        layerNode.setProperty ("velocityZoneMin",  L.velocityZoneMin.load(),            nullptr);
        layerNode.setProperty ("velocityZoneMax",  L.velocityZoneMax.load(),            nullptr);

        // slicesJson — same JSON format as V1 (slicesToJson produces
        // {"slices":[...]}), so Task 13's parse path is identical.
        const auto layerSlices = std::atomic_load (&L.currentSlices);
        const auto sliceJson = layerSlices ? tw::slicesToJson (*layerSlices)
                                           : juce::String ("{\"slices\":[]}");
        layerNode.setProperty ("slicesJson", sliceJson, nullptr);

        // pitchSliceJson — same JSON format as V1.
        layerNode.setProperty ("pitchSliceJson", pitchSliceToJson (L.pitchModeSlice), nullptr);

        layersTree.addChild (layerNode, -1, nullptr);
    }
    state.addChild (layersTree, -1, nullptr);

    // ── V1 root-level properties — KEPT for backward compat ──────────────────
    // These duplicate the layer-A data so that:
    //   (a) An older build that loads a V2 blob via the pre-Task-13 setStateInformation
    //       still finds sampleSourcePath / slicesJson / pitchSliceJson / holdMode at
    //       the root and restores layer A correctly (no crash, partial restore).
    //   (b) Task 13 migration code can detect V1 vs V2 by checking "version".
    //
    // When Task 13 is complete, the V1 root-level read path will be replaced by
    // the V2 layers[] parse path. The V1 properties can be removed from saves
    // once there are no V1 users left.
    {
        // PEROSC-STATE — persist each oscillator's sample path (survives DAW project reload).
        for (int oi = 0; oi < 4; ++oi)
            if (oscSourcePaths_[(size_t) oi].isNotEmpty())
                state.setProperty ("oscSamplePath" + juce::String (oi), oscSourcePaths_[(size_t) oi], nullptr);

        // BLEND-STATE — persist each osc's live blend source pair; the editor reloads both
        // files on reopen so the blend knobs stay live (knob values ride in the APVTS).
        for (int oi = 0; oi < 4; ++oi)
            for (int w = 0; w < 2; ++w)
                if (blendSrcPaths_[(size_t) oi][(size_t) w].isNotEmpty())
                    state.setProperty ("blendSrc" + juce::String (w ? "B" : "A") + juce::String (oi),
                                       blendSrcPaths_[(size_t) oi][(size_t) w], nullptr);

        // Full path for the V1 sample-path restore path (layers[0] only).
        if (layers[0].sourcePath.isNotEmpty())
            state.setProperty ("sampleSourcePath", layers[0].sourcePath, nullptr);
        else
        {
            // Fallback: processor-level singleton (unchanged for pure-V1 instances
            // that never went through Task 12's editor path).
            juce::ScopedLock sl (sampleSourcePathLock);
            if (loadedSamplePath.isNotEmpty())
                state.setProperty ("sampleSourcePath", loadedSamplePath, nullptr);
        }

        // V1 slicesJson / activeSliceIndex from layer A (same data as layersTree[0]).
        const auto layerASlices = std::atomic_load (&layers[0].currentSlices);
        const auto sliceJson = layerASlices ? tw::slicesToJson (*layerASlices)
                                            : juce::String ("{\"slices\":[]}");
        if (sliceJson.isNotEmpty() && sliceJson != "{\"slices\":[]}")
            state.setProperty ("slicesJson", sliceJson, nullptr);
        state.setProperty ("activeSliceIndex", layers[0].activeSliceIndex.load(), nullptr);

        // V1 pitchSliceJson from layer A.
        state.setProperty ("pitchSliceJson", pitchSliceToJson (layers[0].pitchModeSlice), nullptr);

        // HOLD mode — global (not yet per-layer).
        state.setProperty ("holdMode", (bool) holdMode.load(), nullptr);
    }

    // Wavetable EXTENDER — embed active imports (base64 float32 source, capped to kMaxFrames·kFrameSize)
    // so they survive reload. Only oscs with a live import write anything.
    for (int o = 0; o < 4; ++o)
    {
        if (importedPcm_[o].empty()) continue;
        const int cap = tw::Wavetable::kMaxFrames * tw::Wavetable::kFrameSize;
        const int nn  = juce::jmin ((int) importedPcm_[o].size(), cap);
        juce::MemoryBlock mb (importedPcm_[o].data(), (size_t) nn * sizeof (float));
        const juce::String s (o);
        state.setProperty ("wtImportPcm"    + s, mb.toBase64Encoding(), nullptr);
        state.setProperty ("wtImportFrames" + s, importFrames_[o],      nullptr);
        state.setProperty ("wtImportFile"   + s, importIsFile_[o],      nullptr);
        state.setProperty ("wtImportName"   + s, importName_[o],        nullptr);
    }
    for (int o = 0; o < 4; ++o) state.setProperty ("wt3dView" + juce::String (o), wt3dView_[o], nullptr);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void TerrainInstrumentAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
    {
        if (xmlState->hasTagName (apvts.state.getType()))
        {
            auto newState = juce::ValueTree::fromXml (*xmlState);

            // rs6 migration (cleanup sweep): FRACTURE was a RESERVED no-op defaulting to 0.5; it is
            // now MELT (temporal smear, default 0). Pre-rs6 sessions all carry exactly 0.5 → snap to
            // 0 so old sessions don't load with half-Melt engaged. (A deliberate post-rs6 Melt of
            // exactly 0.500 is a knife-edge rarity; dblclick-reset now targets 0.)
            {
                static const char* const fractureIds[4] = {
                    ParameterIDs::SYN_OSC_A_GEODE_FRACTURE, ParameterIDs::SYN_OSC_B_GEODE_FRACTURE,
                    ParameterIDs::SYN_OSC_C_GEODE_FRACTURE, ParameterIDs::SYN_OSC_D_GEODE_FRACTURE };
                for (auto* fid : fractureIds)
                    for (int c = 0; c < newState.getNumChildren(); ++c)
                    {
                        auto ch = newState.getChild (c);
                        if (ch.hasType ("PARAM") && ch.getProperty ("id").toString() == fid)
                        {
                            const float v = (float) ch.getProperty ("value", 0.0f);
                            if (std::abs (v - 0.5f) < 1e-4f) ch.setProperty ("value", 0.0f, nullptr);
                        }
                    }
            }

            // Restore preset index (clamped to valid range after disk presets load)
            int presetIdx = newState.getProperty("presetIndex", 0);

            // Restore XY auto state
            xyAutoEnabled.store(static_cast<float>(newState.getProperty("xyAutoEnabled", 0.f)));
            xyAutoMode.store(static_cast<float>(newState.getProperty("xyAutoMode", 0.f)));
            xyAutoSpeed.store(static_cast<float>(newState.getProperty("xyAutoSpeed", 0.5f)));

            // Restore grain sync state
            grainSyncEnabled.store(static_cast<float>(newState.getProperty("grainSyncEnabled", 0.f)));

            // Restore grain engine on/off, tape on/off, and drift link states
            grainEngineEnabled.store(static_cast<float>(newState.getProperty("grainEngineEnabled", 1.f)));
            tapeEnabled.store(static_cast<float>(newState.getProperty("tapeEnabled", 1.f)));
            tapeLoopEnabled.store(static_cast<float>(newState.getProperty("tapeLoopEnabled", 1.f)));
            wanderLinked.store(static_cast<float>(newState.getProperty("wanderLinked", 1.f)));
            wireSpaceNoiseEnabled.store(static_cast<float>(newState.getProperty("wireSpaceNoise", 0.f)));
            wireTubeSatEnabled.store(static_cast<float>(newState.getProperty("wireTubeSat", 0.f)));
            tapeLinkEnabled.store(static_cast<float>(newState.getProperty("tapeLinkEnabled", 0.f)));
            chorusEnabled.store(static_cast<float>(newState.getProperty("chorusEnabled", 1.f)));
            delayEnabled.store(static_cast<float>(newState.getProperty("delayEnabled", 1.f)));

            // NOISE IMPORT (P5c) — restore the noise-sample selection (factory path or embedded user audio).
            // The editor re-loads the buffer on GUI open via getNoiseSampleSel. Empty = algorithmic type.
            noiseSampleSelJson_ = newState.getProperty ("noiseSampleSel", juce::String()).toString();
            noiseVizMode_ = (int) newState.getProperty ("noiseVizMode", 1);   // fb66 — restore noise viz choice (default particle)

            // Wavetable EXTENDER — restore embedded imports (or clear the osc if none was saved).
            for (int o = 0; o < 4; ++o)
            {
                const juce::String s (o);
                const juce::String b64 = newState.getProperty ("wtImportPcm" + s, juce::String()).toString();
                if (b64.isEmpty())
                {
                    importedPcm_[o].clear(); importName_[o] = {}; importIsFile_[o] = false;
                    importSlot_[o].live.store (nullptr, std::memory_order_release);
                    continue;
                }
                juce::MemoryBlock mb; mb.fromBase64Encoding (b64);
                const int nn = (int) (mb.getSize() / sizeof (float));
                importedPcm_[o].assign ((const float*) mb.getData(), (const float*) mb.getData() + nn);
                importFrames_[o] = (int)  newState.getProperty ("wtImportFrames" + s, 40);
                importIsFile_[o] = (bool) newState.getProperty ("wtImportFile"   + s, false);
                importName_[o]   =        newState.getProperty ("wtImportName"   + s, juce::String()).toString();
                rebuildImport (o);
            }
            for (int o = 0; o < 4; ++o) wt3dView_[o] = (bool) newState.getProperty ("wt3dView" + juce::String (o), false);
            modStateJson = newState.getProperty("modStateJson", "").toString();
            if (modStateJson.isNotEmpty())
                modulationEngine.updateConfig(ModulationEngine::parseJSON(modStateJson));
            {
                auto sm = newState.getProperty("synModJson", "").toString();
                if (sm.isNotEmpty()) setSynthModMatrix (sm);
                auto de = newState.getProperty ("dynEnvJson", "").toString();   // fb177
                if (de.isNotEmpty()) setSynthDynEnvs (de);
                auto lsj = newState.getProperty ("lfoShapesJson", "").toString();   // LFO ARC L1
                if (lsj.isNotEmpty()) setSynthLfoShapes (lsj);
                auto dcv = newState.getProperty ("dstCurvesJson", "").toString();   // fb328
                if (dcv.isNotEmpty()) setDistortionCurves (dcv);
                setDistortionTableSrc ((int) newState.getProperty ("dstTableSrc", -1));   // fb339 — re-reads the osc's CURRENT table
            }
            {
                auto al = newState.getProperty ("arpLanesJson", "").toString();
                if (al.isNotEmpty()) setArpLanesFromJson (al);   // FLOW · ARP lane pattern (fb105)
            }

            // ── Task 13: V1 / V2 branching ────────────────────────────────────
            // V2 blobs (saved by Task 12) carry version=2 and a "layers" child tree.
            // V1 blobs (saved by pre-Task-12 builds) have no "version" property and
            // carry the sample state at the root level (sampleSourcePath, slicesJson,
            // pitchSliceJson, activeSliceIndex).
            const int version = (int) newState.getProperty ("version", 1);

            if (version >= 2)
            {
                loadV2State (newState);
                // Restore editingLayer from V2 blob (clamped to 0..3).
                editingLayer.store (juce::jlimit (0, 3, (int) newState.getProperty ("editingLayer", 0)));
                // Mix page Phase 2: global trigger-mode state. Defaults to LAYER mode
                // (and DRY stems / RR-not-bar-synced) if absent so V2 blobs from
                // pre-Phase-2 builds open cleanly into the new feature defaults.
                triggerMode.store    (juce::jlimit (0, 4, (int) newState.getProperty ("triggerMode",    0)));
                rrSyncToBar.store    ((bool) newState.getProperty ("rrSyncToBar",  false));
                rrShuffle.store      ((bool) newState.getProperty ("rrShuffle",    false));
                layerMorph.store     (juce::jlimit (0.0f, 1.0f, (float)(double) newState.getProperty ("layerMorph", 0.5)));
                stemSourceMode.store (juce::jlimit (0, 1, (int) newState.getProperty ("stemSourceMode", 0)));
            }
            else
            {
                loadV1State (newState);
                editingLayer.store (0);
            }

            // HOLD mode — global (not yet per-layer). Applies to both V1 and V2.
            holdMode.store ((bool) newState.getProperty ("holdMode", false),
                            std::memory_order_relaxed);

            // Reload presets from disk (the single source of truth)
            while (static_cast<int>(presets.size()) > numFactoryPresets)
                presets.pop_back();
            loadUserPresetsFromFile();

            // Clamp preset index to valid range
            if (presetIdx >= static_cast<int>(presets.size()))
                presetIdx = 0;
            currentPresetIndex.store(presetIdx);

            apvts.replaceState (newState);

            // Mark 2 Phase 1 audio-fix: V1 backward-compat. V1 presets carry
            // sliceMode and sampleLoopMode only in APVTS (global). Engine now
            // reads from per-layer atomics. Seed layers[0] from the freshly
            // loaded APVTS values so V1 presets keep their SLICE/PITCH and
            // 1-SHOT/LOOP selections. V2 presets already populated per-layer
            // in loadV2State.
            if (version < 2)
            {
                const int v1SliceMode = (int) *rawParam (ParameterIDs::SLICE_MODE);
                const int v1LoopMode  = (int) *rawParam (ParameterIDs::SAMPLE_LOOP_MODE);
                layers[0].sliceMode.store      (v1SliceMode);
                layers[0].sampleLoopMode.store (v1LoopMode);
            }
        }
    }
}

// ── Task 13: pitchSliceJson → LayerState helper ───────────────────────────────
// Shared by loadV1State and loadV2State.  Reads the JSON string produced by the
// pitchSliceToJson lambda in getStateInformation and writes the fields into the
// provided LayerState's pitchModeSlice.  No-op if the string is empty or invalid.
/*static*/ void TerrainInstrumentAudioProcessor::applyPitchSliceJson (
        const juce::String& psJson, tw::LayerState& layer)
{
    if (psJson.isEmpty()) return;

    auto pv = juce::JSON::parse (psJson);
    if (! pv.isObject()) return;

    auto& ps = layer.pitchModeSlice;
    ps.reverse          = (bool) pv.getProperty ("reverse", false);
    ps.pitchOffsetSemis = juce::jlimit (-12.0f, 12.0f,
                              (float)(double) pv.getProperty ("pitch", 0.0));
    const int wmRaw     = (int) pv.getProperty ("warpMode", 0);
    ps.warpMode         = (wmRaw >= 0 && wmRaw <= 3)
                              ? static_cast<tw::WarpMode> (wmRaw)
                              : tw::WarpMode::None;
    ps.stretchRatio     = juce::jlimit (0.1f, 15.0f,
                              (float)(double) pv.getProperty ("stretchRatio", 1.0));
    const double atkRaw = (double) pv.getProperty ("attackMs",  -1.0);
    const double relRaw = (double) pv.getProperty ("releaseMs", -1.0);
    ps.attackMs         = atkRaw < 0.0 ? -1.0f : juce::jlimit (0.0f, 2000.0f, (float) atkRaw);
    ps.releaseMs        = relRaw < 0.0 ? -1.0f : juce::jlimit (1.0f, 5000.0f, (float) relRaw);
    ps.decayMs          = juce::jlimit (0.0f, 2000.0f,
                              (float)(double) pv.getProperty ("decayMs",      0.0));
    ps.sustainLevel     = juce::jlimit (0.0f, 1.0f,
                              (float)(double) pv.getProperty ("sustainLevel", 1.0));
    ps.volume           = juce::jlimit (0.0f, 2.0f,
                              (float)(double) pv.getProperty ("volume",       1.0));
    ps.scanEnabled      = (bool) pv.getProperty ("scanEnabled", false);
    float scRt = (float)(double) pv.getProperty ("scanRate",   0.0);
    float scWn = (float)(double) pv.getProperty ("scanWindow", 0.0);
    ps.scanRate         = (scRt < 0.05f) ? 1.0f : juce::jlimit (0.1f, 8.0f,  scRt);
    ps.scanWindow       = (scWn < 0.04f) ? 1.0f : juce::jlimit (0.05f, 1.0f, scWn);

    // Restore pitch-mode in/out bounds if serialised.  If absent (old state),
    // leave as-is — a subsequent sample file load will overwrite with [0, N].
    const juce::int64 savedStart = (juce::int64)(double) pv.getProperty ("startSample", (juce::int64)-1);
    const juce::int64 savedEnd   = (juce::int64)(double) pv.getProperty ("endSample",   (juce::int64)-1);
    if (savedStart >= 0 && savedEnd > savedStart)
    {
        ps.startSample = savedStart;
        ps.endSample   = savedEnd;
        // Register restored bounds with the WarpRenderCache under sliceIndex=-1
        // (pitch-mode sentinel) so warp+scan in pitch mode has valid bounds
        // immediately after state reload without waiting for a file load.
        layer.synth.warpCache.setSliceBounds (-1, (int) savedStart, (int) savedEnd);
    }
}

// ── Task 13: V1 state loader ──────────────────────────────────────────────────
// Reads the pre-Mark-2 blob layout: single sample at the root level.
// Populates layers[0]; clears layers[1..3] so they show as empty.
void TerrainInstrumentAudioProcessor::loadV1State (const juce::ValueTree& loaded)
{
    // Clear all 4 layers first so layers[1..3] show as empty after a V1 load.
    for (auto& L : layers)
    {
        L.sourceFileName = juce::String();
        L.sourcePath     = juce::String();
        // Drop the sample buffer (atomic store of nullptr).
        L.sampleBuffer.store (tw::SampleBuffer::BufferPtr{});
        // Drop slice list.
        std::atomic_store (&L.currentSlices, tw::SliceListPtr{});
        // Reset pitchModeSlice to default-constructed state.
        L.pitchModeSlice = tw::Slice{};
        L.activeSliceIndex.store (0);
        L.volume.store (1.0f);
        L.mute.store   (false);
        L.solo.store   (false);
    }

    // ── Populate layer A (index 0) from root-level V1 properties ─────────────
    auto& A = layers[0];

    // V1 stored the path under "sampleSourcePath" at the root.
    A.sourcePath     = loaded.getProperty ("sampleSourcePath", "").toString();
    // PEROSC-STATE — restore each oscillator's sample path; the editor reload loop re-decodes it.
    for (int oi = 0; oi < 4; ++oi)
        oscSourcePaths_[(size_t) oi] = loaded.getProperty ("oscSamplePath" + juce::String (oi), "").toString();
    // BLEND-STATE — restore the blend source pairs (editor re-analyzes lazily on reopen).
    for (int oi = 0; oi < 4; ++oi)
        for (int w = 0; w < 2; ++w)
            blendSrcPaths_[(size_t) oi][(size_t) w] =
                loaded.getProperty ("blendSrc" + juce::String (w ? "B" : "A") + juce::String (oi), "").toString();
    A.sourceFileName = loaded.getProperty ("sourceFileName",   "").toString();

    // Persist the path via the legacy singleton so the editor constructor's
    // V1 reload path (getLoadedSamplePath → loadSampleAsync for layer 0) fires.
    {
        juce::ScopedLock sl (sampleSourcePathLock);
        loadedSamplePath = A.sourcePath;
    }

    // Slices.
    const juce::String sliceJson = loaded.getProperty ("slicesJson", "").toString();
    if (sliceJson.isNotEmpty())
    {
        auto sl = std::make_shared<const tw::SliceList> (tw::slicesFromJson (sliceJson));
        std::atomic_store (&A.currentSlices, tw::SliceListPtr (sl));

        // Push slice bounds into the warp cache (same as replaceSlices does,
        // but writing directly to layer[0] instead of routing through editingLayer).
        for (int i = 0; i < (int) sl->size(); ++i)
        {
            const auto& s = (*sl)[(size_t) i];
            A.synth.warpCache.setSliceBounds (i, (int) s.startSample, (int) s.endSample);
        }
    }

    A.activeSliceIndex.store ((int) loaded.getProperty ("activeSliceIndex", 0));

    // pitchModeSlice — uses the shared helper.
    applyPitchSliceJson (loaded.getProperty ("pitchSliceJson", "").toString(), A);

    // Mode/play/count come via APVTS (already replaced by the caller).
}

// ── Task 13: V2 state loader ──────────────────────────────────────────────────
// Reads the Task-12 blob layout: 4-layer "layers" child tree.
// Populates all 4 layers.  Falls back to loadV1State if the tree is absent.
void TerrainInstrumentAudioProcessor::loadV2State (const juce::ValueTree& loaded)
{
    auto layersTree = loaded.getChildWithName ("layers");
    if (! layersTree.isValid())
    {
        // Defensive: V2 marker set but no layers tree (shouldn't happen with
        // Task-12-saved blobs, but protect against partial writes).
        loadV1State (loaded);
        return;
    }

    // Clear all 4 layers first so empty layer nodes leave those layers visually
    // empty (no ghost sample state from a previous load).
    for (auto& L : layers)
    {
        L.sourceFileName = juce::String();
        L.sourcePath     = juce::String();
        L.sampleBuffer.store (nullptr);
        std::atomic_store (&L.currentSlices, tw::SliceListPtr{});
        L.pitchModeSlice = tw::Slice{};
        L.activeSliceIndex.store (0);
        L.volume.store (1.0f);
        L.mute.store   (false);
        L.solo.store   (false);
    }

    for (int i = 0; i < layersTree.getNumChildren(); ++i)
    {
        auto layerNode = layersTree.getChild (i);
        const int idx  = (int) layerNode.getProperty ("index", i);
        if (idx < 0 || idx > 3) continue;

        auto& L = layers[(size_t) idx];

        L.sourceFileName = layerNode.getProperty ("sourceFileName", "").toString();
        L.sourcePath     = layerNode.getProperty ("sourcePath",     "").toString();
        L.activeSliceIndex.store ((int) layerNode.getProperty ("activeSliceIndex", 0));
        L.rootMidiNote.store     ((int) layerNode.getProperty ("rootMidiNote", 60));
        L.sliceMode.store        ((int) layerNode.getProperty ("sliceMode",    0));
        L.playMode.store         ((int) layerNode.getProperty ("playMode",     0));
        L.sliceCount.store       ((int) layerNode.getProperty ("sliceCount",   4));
        L.chopFadeMs.store ((float)(double) layerNode.getProperty ("chopFadeMs", 5.0));
        L.volume.store     ((float)(double) layerNode.getProperty ("volume",     1.0));
        L.mute.store  ((bool) layerNode.getProperty ("mute",  false));
        L.solo.store  ((bool) layerNode.getProperty ("solo",  false));
        // Mix page Phase 2: per-layer creative-routing + mixer fields.
        // Defaults match LayerState defaults so pre-Phase-2 V2 blobs open clean.
        // Velocity + key zone defaults per-layer index: even quarters (matches processor seed).
        const int dvzMin = (idx == 0 ? 0  : idx == 1 ? 32 : idx == 2 ? 64 : 96);
        const int dvzMax = (idx == 0 ? 31 : idx == 1 ? 63 : idx == 2 ? 95 : 127);
        L.pan.store               ((float)(double) layerNode.getProperty ("pan",               0.0));
        L.pitchJitterCents.store  ((float)(double) layerNode.getProperty ("pitchJitterCents",  0.0));
        L.probabilityWeight.store ((float)(double) layerNode.getProperty ("probabilityWeight", 0.25));
        L.keyZoneMin.store        ((int)           layerNode.getProperty ("keyZoneMin",        dvzMin));
        L.keyZoneMax.store        ((int)           layerNode.getProperty ("keyZoneMax",        dvzMax));
        L.velocityZoneMin.store   ((int)           layerNode.getProperty ("velocityZoneMin",   dvzMin));
        L.velocityZoneMax.store   ((int)           layerNode.getProperty ("velocityZoneMax",   dvzMax));

        // Slices — same JSON format as V1 (slicesToJson/slicesFromJson).
        const juce::String sliceJson = layerNode.getProperty ("slicesJson", "").toString();
        if (sliceJson.isNotEmpty())
        {
            auto sl = std::make_shared<const tw::SliceList> (tw::slicesFromJson (sliceJson));
            std::atomic_store (&L.currentSlices, tw::SliceListPtr (sl));

            // Push slice bounds into this layer's warp cache.
            for (int si = 0; si < (int) sl->size(); ++si)
            {
                const auto& s = (*sl)[(size_t) si];
                L.synth.warpCache.setSliceBounds (si, (int) s.startSample, (int) s.endSample);
            }
        }

        // pitchModeSlice — shared helper.
        applyPitchSliceJson (layerNode.getProperty ("pitchSliceJson", "").toString(), L);
    }

    // Keep the legacy loadedSamplePath in sync with layer 0's path so the
    // editor constructor's existing V1 reload path fires for layer 0.  The
    // editor constructor's V2 path (added in the same task) handles layers 1-3.
    {
        juce::ScopedLock sl (sampleSourcePathLock);
        loadedSamplePath = layers[0].sourcePath;
    }
}

//==============================================================================
// User Preset File Persistence
//==============================================================================
juce::File TerrainInstrumentAudioProcessor::getUserPresetsFile() const
{
    // Separate folder from FX (which writes to Noizefield/Terrain/UserPresets.xml).
    // Schemas differ — instrument has SAMPLE_LOOP_MODE / ATTACK_MS / RELEASE_MS /
    // ROOT_NOTE etc. that the FX doesn't know about. If both wrote to the same
    // file, save-from-one would clobber/merge incompatibly with the other.
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Noizefield")
        .getChildFile("Terrain Instrument")
        .getChildFile("UserPresets.xml");
}

void TerrainInstrumentAudioProcessor::saveUserPresetsToFile()
{
    auto file = getUserPresetsFile();

    // Ensure the directory tree exists
    auto dir = file.getParentDirectory();
    if (!dir.exists())
        dir.createDirectory();

    juce::ValueTree root ("TerrainUserPresets");

    // Save custom tags list
    if (customTags.size() > 0)
        root.setProperty("customTags", customTags.joinIntoString(","), nullptr);

    // Save everything except Init (index 0)
    for (int i = numFactoryPresets; i < static_cast<int>(presets.size()); ++i)
    {
        const auto& p = presets[static_cast<size_t>(i)];
        juce::ValueTree node ("Preset");
        node.setProperty("name",          p.name,          nullptr);
        if (p.tag.isNotEmpty())
            node.setProperty("tag",       p.tag,           nullptr);
        node.setProperty("grainSize",     p.grainSize,     nullptr);
        node.setProperty("density",       p.density,       nullptr);
        node.setProperty("spray",         p.spray,         nullptr);
        node.setProperty("pitch",         p.pitch,         nullptr);
        node.setProperty("drift",         p.drift,         nullptr);
        node.setProperty("freeze",        p.freeze,        nullptr);
        node.setProperty("grainFilter",   p.grainFilter,   nullptr);
        node.setProperty("mix",           p.mix,           nullptr);
        node.setProperty("wowFlutter",    p.wowFlutter,    nullptr);
        node.setProperty("saturation",    p.saturation,    nullptr);
        node.setProperty("hiss",          p.hiss,          nullptr);
        node.setProperty("wireWow",        p.wireWow,        nullptr);
        node.setProperty("wireSaturation", p.wireSaturation, nullptr);
        node.setProperty("wireHiss",       p.wireHiss,       nullptr);
        node.setProperty ("studioSculpt", p.studioSculpt, nullptr);
        node.setProperty ("studioWeave",  p.studioWeave,  nullptr);
        node.setProperty ("studioTilt",   p.studioTilt,   nullptr);
        node.setProperty("outputGain",    p.outputGain,    nullptr);
        node.setProperty("masterMix",     p.masterMix,     nullptr);
        node.setProperty("xyAutoEnabled",    p.xyAutoEnabled,    nullptr);
        node.setProperty("xyAutoMode",       p.xyAutoMode,       nullptr);
        node.setProperty("xyAutoSpeed",      p.xyAutoSpeed,      nullptr);
        node.setProperty("grainSyncEnabled",  p.grainSyncEnabled,  nullptr);
        node.setProperty("grainEngineEnabled", p.grainEngineEnabled, nullptr);
        node.setProperty("tapeEnabled",        p.tapeEnabled,        nullptr);
        node.setProperty("tapeLoopEnabled",    p.tapeLoopEnabled,    nullptr);
        node.setProperty("tapeMachine",        p.tapeMachine,        nullptr);
        node.setProperty("wanderLinked",        p.wanderLinked,        nullptr);
        node.setProperty("tapeLinkEnabled",    p.tapeLinkEnabled,    nullptr);
        node.setProperty("loopLength",      p.loopLength,      nullptr);
        node.setProperty("loopFeedback",    p.loopFeedback,    nullptr);
        node.setProperty("loopDegrade",     p.loopDegrade,     nullptr);
        node.setProperty("loopSpeed",       p.loopSpeed,       nullptr);
        node.setProperty("spaceSize",      p.spaceSize,       nullptr);
        node.setProperty("spaceDecay",     p.spaceDecay,      nullptr);
        node.setProperty("spaceTone",      p.spaceTone,       nullptr);
        node.setProperty("spaceMix",       p.spaceMix,        nullptr);
        // Parametric EQ (v6) — 35 APVTS values + 2 UI-only filter-mode flags.
        node.setProperty ("eqMasterBypass", p.eqMasterBypass, nullptr);
        node.setProperty ("eqHpFreq",   p.eqHpFreq,   nullptr);
        node.setProperty ("eqHpSlope",  p.eqHpSlope,  nullptr);
        node.setProperty ("eqHpBypass", p.eqHpBypass, nullptr);
        node.setProperty ("eqLpFreq",   p.eqLpFreq,   nullptr);
        node.setProperty ("eqLpSlope",  p.eqLpSlope,  nullptr);
        node.setProperty ("eqLpBypass", p.eqLpBypass, nullptr);
        node.setProperty ("eqB1Freq", p.eqB1Freq, nullptr); node.setProperty ("eqB1Gain", p.eqB1Gain, nullptr); node.setProperty ("eqB1Q", p.eqB1Q, nullptr); node.setProperty ("eqB1Bypass", p.eqB1Bypass, nullptr);
        node.setProperty ("eqB2Freq", p.eqB2Freq, nullptr); node.setProperty ("eqB2Gain", p.eqB2Gain, nullptr); node.setProperty ("eqB2Q", p.eqB2Q, nullptr); node.setProperty ("eqB2Bypass", p.eqB2Bypass, nullptr);
        node.setProperty ("eqB3Freq", p.eqB3Freq, nullptr); node.setProperty ("eqB3Gain", p.eqB3Gain, nullptr); node.setProperty ("eqB3Q", p.eqB3Q, nullptr); node.setProperty ("eqB3Bypass", p.eqB3Bypass, nullptr);
        node.setProperty ("eqB4Freq", p.eqB4Freq, nullptr); node.setProperty ("eqB4Gain", p.eqB4Gain, nullptr); node.setProperty ("eqB4Q", p.eqB4Q, nullptr); node.setProperty ("eqB4Bypass", p.eqB4Bypass, nullptr);
        node.setProperty ("eqB5Freq", p.eqB5Freq, nullptr); node.setProperty ("eqB5Gain", p.eqB5Gain, nullptr); node.setProperty ("eqB5Q", p.eqB5Q, nullptr); node.setProperty ("eqB5Bypass", p.eqB5Bypass, nullptr);
        node.setProperty ("eqB6Freq", p.eqB6Freq, nullptr); node.setProperty ("eqB6Gain", p.eqB6Gain, nullptr); node.setProperty ("eqB6Q", p.eqB6Q, nullptr); node.setProperty ("eqB6Bypass", p.eqB6Bypass, nullptr);
        node.setProperty ("eqB7Freq", p.eqB7Freq, nullptr); node.setProperty ("eqB7Gain", p.eqB7Gain, nullptr); node.setProperty ("eqB7Q", p.eqB7Q, nullptr); node.setProperty ("eqB7Bypass", p.eqB7Bypass, nullptr);
        node.setProperty ("eqB1HpMode", p.eqB1HpMode, nullptr);
        node.setProperty ("eqB7LpMode", p.eqB7LpMode, nullptr);
        node.setProperty ("chorusAmount",    p.chorusAmount,    nullptr);
        node.setProperty ("chorusWidth",     p.chorusWidth,     nullptr);
        node.setProperty ("chorusCharacter", p.chorusCharacter, nullptr);
        if (p.modState.isNotEmpty())
            node.setProperty("modState",  p.modState,       nullptr);
        root.addChild(node, -1, nullptr);
    }

    if (auto xml = root.createXml())
    {
        auto result = xml->writeTo(file);
        DBG("Terrain: saveUserPresetsToFile -> " + file.getFullPathName()
            + " (" + juce::String(presets.size() - numFactoryPresets) + " presets, "
            + (result ? "OK" : "FAILED") + ")");
    }
}

void TerrainInstrumentAudioProcessor::loadUserPresetsFromFile()
{
    auto file = getUserPresetsFile();

    DBG("Terrain: loadUserPresetsFromFile -> " + file.getFullPathName()
        + " exists=" + (file.existsAsFile() ? "YES" : "NO"));

    if (!file.existsAsFile())
        return;

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("TerrainUserPresets"))
    {
        DBG("Terrain: XML parse failed or wrong tag");
        return;
    }

    auto root = juce::ValueTree::fromXml(*xml);
    int loaded = 0;

    // Load custom tags
    juce::String tagsStr = root.getProperty("customTags", "").toString();
    if (tagsStr.isNotEmpty())
    {
        customTags.clear();
        customTags.addTokens(tagsStr, ",", "");
        customTags.removeEmptyStrings();
    }

    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        auto child = root.getChild(i);
        if (!child.hasType("Preset"))
            continue;

        PresetData p;
        p.name          = child.getProperty("name").toString();
        p.tag           = child.getProperty("tag", "").toString();
        p.grainSize     = static_cast<float>(child.getProperty("grainSize",     80.f));
        p.density       = static_cast<float>(child.getProperty("density",       20.f));
        p.spray         = static_cast<float>(child.getProperty("spray",         40.f));
        p.pitch         = static_cast<float>(child.getProperty("pitch",          0.f));
        p.drift         = static_cast<float>(child.getProperty("drift",          0.f));
        p.freeze        = static_cast<float>(child.getProperty("freeze",         0.f));
        p.grainFilter   = static_cast<float>(child.getProperty("grainFilter",  50.f));
        p.mix           = static_cast<float>(child.getProperty("mix",           50.f));
        p.wowFlutter    = static_cast<float>(child.getProperty("wowFlutter",     0.f));
        p.saturation    = static_cast<float>(child.getProperty("saturation",     0.f));
        p.hiss          = static_cast<float>(child.getProperty("hiss",           0.f));
        p.wireWow        = static_cast<float>(child.getProperty("wireWow",        0.f));
        p.wireSaturation = static_cast<float>(child.getProperty("wireSaturation", 0.f));
        p.wireHiss       = static_cast<float>(child.getProperty("wireHiss",       0.f));
        p.studioSculpt = static_cast<float> (child.getProperty ("studioSculpt", 0.f));
        p.studioWeave  = static_cast<float> (child.getProperty ("studioWeave",  0.f));
        p.studioTilt   = static_cast<float> (child.getProperty ("studioTilt",   0.f));
        p.outputGain    = static_cast<float>(child.getProperty("outputGain",     0.f));
        p.masterMix     = static_cast<float>(child.getProperty("masterMix",    100.f));
        p.xyAutoEnabled    = static_cast<float>(child.getProperty("xyAutoEnabled",    0.f));
        p.xyAutoMode       = static_cast<float>(child.getProperty("xyAutoMode",       0.f));
        p.xyAutoSpeed      = static_cast<float>(child.getProperty("xyAutoSpeed",     0.5f));
        p.grainSyncEnabled  = static_cast<float>(child.getProperty("grainSyncEnabled",  0.f));
        p.grainEngineEnabled = static_cast<float>(child.getProperty("grainEngineEnabled", 1.f));
        p.tapeEnabled        = static_cast<float>(child.getProperty("tapeEnabled",        1.f));
        p.tapeLoopEnabled    = static_cast<float>(child.getProperty("tapeLoopEnabled",    1.f));
        p.tapeMachine        = static_cast<float>(child.getProperty("tapeMachine",        0.f));
        p.wanderLinked        = static_cast<float>(child.getProperty("wanderLinked",        1.f));
        p.tapeLinkEnabled    = static_cast<float>(child.getProperty("tapeLinkEnabled",    0.f));
        p.loopLength      = static_cast<float>(child.getProperty("loopLength",      3.f));
        p.loopFeedback    = static_cast<float>(child.getProperty("loopFeedback",   85.f));
        p.loopDegrade     = static_cast<float>(child.getProperty("loopDegrade",    30.f));
        p.loopSpeed       = static_cast<float>(child.getProperty("loopSpeed",       6.f));
        p.spaceSize       = static_cast<float>(child.getProperty("spaceSize",      50.f));
        p.spaceDecay      = static_cast<float>(child.getProperty("spaceDecay",     50.f));
        p.spaceTone       = static_cast<float>(child.getProperty("spaceTone",      50.f));
        p.spaceMix        = static_cast<float>(child.getProperty("spaceMix",        0.f));
        // Parametric EQ (v6) — fall back to APVTS defaults for old presets that
        // pre-date the EQ persistence work (so loading them leaves EQ at sane defaults).
        p.eqMasterBypass = static_cast<float> (child.getProperty ("eqMasterBypass", 0.f));
        p.eqHpFreq    = static_cast<float> (child.getProperty ("eqHpFreq",    35.f));
        p.eqHpSlope   = static_cast<float> (child.getProperty ("eqHpSlope",    1.f));
        p.eqHpBypass  = static_cast<float> (child.getProperty ("eqHpBypass",   1.f));
        p.eqLpFreq    = static_cast<float> (child.getProperty ("eqLpFreq", 16000.f));
        p.eqLpSlope   = static_cast<float> (child.getProperty ("eqLpSlope",    0.f));
        p.eqLpBypass  = static_cast<float> (child.getProperty ("eqLpBypass",   1.f));
        p.eqB1Freq = static_cast<float> (child.getProperty ("eqB1Freq",   50.f)); p.eqB1Gain = static_cast<float> (child.getProperty ("eqB1Gain", 0.f)); p.eqB1Q = static_cast<float> (child.getProperty ("eqB1Q", 0.707f)); p.eqB1Bypass = static_cast<float> (child.getProperty ("eqB1Bypass", 0.f));
        p.eqB2Freq = static_cast<float> (child.getProperty ("eqB2Freq",  110.f)); p.eqB2Gain = static_cast<float> (child.getProperty ("eqB2Gain", 0.f)); p.eqB2Q = static_cast<float> (child.getProperty ("eqB2Q", 0.707f)); p.eqB2Bypass = static_cast<float> (child.getProperty ("eqB2Bypass", 0.f));
        p.eqB3Freq = static_cast<float> (child.getProperty ("eqB3Freq",  270.f)); p.eqB3Gain = static_cast<float> (child.getProperty ("eqB3Gain", 0.f)); p.eqB3Q = static_cast<float> (child.getProperty ("eqB3Q", 0.707f)); p.eqB3Bypass = static_cast<float> (child.getProperty ("eqB3Bypass", 0.f));
        p.eqB4Freq = static_cast<float> (child.getProperty ("eqB4Freq",  630.f)); p.eqB4Gain = static_cast<float> (child.getProperty ("eqB4Gain", 0.f)); p.eqB4Q = static_cast<float> (child.getProperty ("eqB4Q", 0.707f)); p.eqB4Bypass = static_cast<float> (child.getProperty ("eqB4Bypass", 0.f));
        p.eqB5Freq = static_cast<float> (child.getProperty ("eqB5Freq", 1500.f)); p.eqB5Gain = static_cast<float> (child.getProperty ("eqB5Gain", 0.f)); p.eqB5Q = static_cast<float> (child.getProperty ("eqB5Q", 0.707f)); p.eqB5Bypass = static_cast<float> (child.getProperty ("eqB5Bypass", 0.f));
        p.eqB6Freq = static_cast<float> (child.getProperty ("eqB6Freq", 3500.f)); p.eqB6Gain = static_cast<float> (child.getProperty ("eqB6Gain", 0.f)); p.eqB6Q = static_cast<float> (child.getProperty ("eqB6Q", 0.707f)); p.eqB6Bypass = static_cast<float> (child.getProperty ("eqB6Bypass", 0.f));
        p.eqB7Freq = static_cast<float> (child.getProperty ("eqB7Freq", 8500.f)); p.eqB7Gain = static_cast<float> (child.getProperty ("eqB7Gain", 0.f)); p.eqB7Q = static_cast<float> (child.getProperty ("eqB7Q", 0.707f)); p.eqB7Bypass = static_cast<float> (child.getProperty ("eqB7Bypass", 0.f));
        p.eqB1HpMode = static_cast<float> (child.getProperty ("eqB1HpMode", 0.f));
        p.eqB7LpMode = static_cast<float> (child.getProperty ("eqB7LpMode", 0.f));
        p.chorusAmount    = static_cast<float> (child.getProperty ("chorusAmount",    0.0f));
        p.chorusWidth     = static_cast<float> (child.getProperty ("chorusWidth",     0.7f));
        p.chorusCharacter = static_cast<float> (child.getProperty ("chorusCharacter", 0.3f));
        p.modState       = child.getProperty("modState", "").toString();

        presets.push_back(p);
        loaded++;
    }

    DBG("Terrain: loaded " + juce::String(loaded) + " user presets from disk, total=" + juce::String(presets.size()));
}

//==============================================================================
// Rolling Capture Buffer
//==============================================================================
float TerrainInstrumentAudioProcessor::getCaptureAvailableSeconds() const
{
    return captureBuffer.getAvailableSeconds();
}

juce::String TerrainInstrumentAudioProcessor::getLastCaptureFilePath() const
{
    return lastCaptureFilePath;
}

void TerrainInstrumentAudioProcessor::exportCapture(int durationSeconds)
{
    // CAS: only start if idle (0)
    int expected = 0;
    if (!captureExportState.compare_exchange_strong(expected, 1))
        return; // already exporting or ready

    // Join any previous thread
    if (captureExportThread && captureExportThread->joinable())
        captureExportThread->join();

    const double sr = captureBuffer.getSampleRate();
    if (sr <= 0.0)
    {
        captureExportState.store(3); // error
        return;
    }

    const int maxSamples = static_cast<int>(sr * durationSeconds);
    auto tempL = std::make_shared<std::vector<float>>(static_cast<size_t>(maxSamples), 0.0f);
    auto tempR = std::make_shared<std::vector<float>>(static_cast<size_t>(maxSamples), 0.0f);

    int copied = captureBuffer.copyForExport(tempL->data(), tempR->data(),
                                              static_cast<double>(durationSeconds));
    if (copied <= 0)
    {
        captureExportState.store(3); // error
        return;
    }

    // Build output path: ~/Music/Waves Crate/Terrain Instrument/Capture_YYYY-MM-DD_HH-MM-SS.wav
    // Separate folder from FX (~/Music/Waves Crate/Terrain/) so the same
    // user can run both products without their captures landing in the same
    // bucket — instrument captures are typically MIDI-recorded sample takes,
    // FX captures are processed track recordings; users want them separated.
    auto now = juce::Time::getCurrentTime();
    auto timestamp = now.formatted("%Y-%m-%d_%H-%M-%S");
    auto dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                   .getChildFile("Waves Crate")
                   .getChildFile("Terrain Instrument");

    if (!dir.exists())
        dir.createDirectory();

    auto filePath = dir.getChildFile("Terrain_Instrument_Capture_" + timestamp + ".wav");

    captureExportThread = std::make_unique<std::thread>(
        [this, tempL, tempR, copied, sr, filePath]()
        {
            juce::WavAudioFormat wav;
            auto outFile = filePath;
            outFile.deleteFile(); // remove if exists

            auto stream = outFile.createOutputStream();
            if (stream == nullptr)
            {
                captureExportState.store(3);
                return;
            }

            JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wdeprecated-declarations")
            auto* writer = wav.createWriterFor(stream.release(), sr, 2, 16, {}, 0);
            JUCE_END_IGNORE_WARNINGS_GCC_LIKE
            if (writer == nullptr)
            {
                captureExportState.store(3);
                return;
            }
            std::unique_ptr<juce::AudioFormatWriter> writerPtr(writer);

            // Write in chunks
            constexpr int chunkSize = 8192;
            juce::AudioBuffer<float> chunk(2, chunkSize);

            for (int pos = 0; pos < copied; pos += chunkSize)
            {
                int samplesThisChunk = std::min(chunkSize, copied - pos);
                chunk.copyFrom(0, 0, tempL->data() + pos, samplesThisChunk);
                chunk.copyFrom(1, 0, tempR->data() + pos, samplesThisChunk);
                writerPtr->writeFromAudioSampleBuffer(chunk, 0, samplesThisChunk);
            }

            writerPtr.reset(); // flush and close

            lastCaptureFilePath = outFile.getFullPathName();
            captureExportState.store(2); // ready
        });
}

//==============================================================================
// Per-chop FX-independence (option 1): snapshots APVTS parameters into a
// IndyFxChain::ParamTargets struct so the shared indy chain uses the same
// values as the global chain. Scaling conventions match the global chain's
// per-sample loop exactly (see baked-in lessons in the implementation plan).
tw::IndyFxChain::ParamTargets
TerrainInstrumentAudioProcessor::snapshotFxParamTargets() const noexcept
{
    tw::IndyFxChain::ParamTargets t;

    // Grain — density / spray are RAW (matching global chain's per-sample loop)
    t.grainSize = rawParam (ParameterIDs::GRAIN_SIZE)->load();
    t.density   = rawParam (ParameterIDs::DENSITY)->load();
    t.spray     = rawParam (ParameterIDs::SPRAY)->load();
    t.pitch     = rawParam (ParameterIDs::PITCH)->load();
    t.wander01  = rawParam (ParameterIDs::WANDER)->load() * 0.01f;
    // Freeze uses the global chain's concave curve: pow(raw * 0.01f, 1.5f).
    {
        const float raw = rawParam (ParameterIDs::FREEZE)->load() * 0.01f;
        t.freeze01 = std::pow (raw, 1.5f);
    }
    t.mix = rawParam (ParameterIDs::MIX)->load();

    // Tape (cassette params *0.01f to match global chain per-sample scaling)
    t.wowFlutter01 = rawParam (ParameterIDs::WOW_FLUTTER)->load() * 0.01f;
    t.saturation01 = rawParam (ParameterIDs::SATURATION)->load() * 0.01f;
    t.hiss01       = rawParam (ParameterIDs::HISS)->load() * 0.01f;
    // Studio sculpt/weave/tilt: global chain computes sculptAmt = raw*0.01f,
    // then passes sculptAmt to tapeProcessor. IndyFxChain passes these directly
    // to tapeL.processSample in the same argument position — must match.
    t.studioSculpt = rawParam (ParameterIDs::STUDIO_SCULPT)->load() * 0.01f;
    t.studioWeave  = rawParam (ParameterIDs::STUDIO_WEAVE)->load() * 0.01f;
    t.studioTilt   = rawParam (ParameterIDs::STUDIO_TILT)->load() * 0.01f;
    // Wire wow/sat/hiss: global chain uses wireWowAmt = raw*0.01f.
    // IndyFxChain passes these in the same wire argument position.
    t.wireWow  = rawParam (ParameterIDs::WIRE_WOW)->load() * 0.01f;
    t.wireSat  = rawParam (ParameterIDs::WIRE_SATURATION)->load() * 0.01f;
    t.wireHiss = rawParam (ParameterIDs::WIRE_HISS)->load() * 0.01f;
    t.wireSpaceNoise = wireSpaceNoiseEnabled.load() > 0.5f;
    t.wireTubeSat    = wireTubeSatEnabled.load() > 0.5f;

    // Space — global chain scales ALL FOUR by * 0.01f (see PluginProcessor.cpp
    // lines 1663-1666). Passing raw 0..100 values causes SpaceReverb to silence
    // / NaN out at typical user knob settings.
    t.spaceSize  = rawParam (ParameterIDs::SPACE_SIZE)->load() * 0.01f;
    t.spaceDecay = rawParam (ParameterIDs::SPACE_DECAY)->load() * 0.01f;
    t.spaceTone  = rawParam (ParameterIDs::SPACE_TONE)->load() * 0.01f;
    t.spaceMix   = rawParam (ParameterIDs::SPACE_MIX)->load() * 0.01f;

    // Delay (MoogDelay::Params field names verified from MoogDelay.h)
    t.dlyTime       = rawParam (ParameterIDs::DLY_TIME)->load();
    t.dlyFeedback   = rawParam (ParameterIDs::DLY_FEEDBACK)->load();
    t.dlyTone       = rawParam (ParameterIDs::DLY_TONE)->load();
    t.dlyCharacter  = rawParam (ParameterIDs::DLY_CHARACTER)->load();
    t.dlyMod        = rawParam (ParameterIDs::DLY_MOD)->load();
    t.dlyModRate    = rawParam (ParameterIDs::DLY_MOD_RATE)->load();
    t.dlyModWave    = (int) rawParam (ParameterIDs::DLY_MOD_WAVE)->load();
    t.dlyMix        = rawParam (ParameterIDs::DLY_MIX)->load();
    t.dlyDuck       = rawParam (ParameterIDs::DLY_DUCK)->load();
    t.dlyPitch      = (int) rawParam (ParameterIDs::DLY_PITCH)->load();
    t.dlyWidth      = (int) rawParam (ParameterIDs::DLY_WIDTH)->load();
    t.dlyFreezeHeld = rawParam (ParameterIDs::DLY_FREEZE)->load() > 0.5f;

    // June (chorus)
    t.chAmount    = rawParam (ParameterIDs::CHORUS_AMOUNT)->load();
    t.chWidth     = rawParam (ParameterIDs::CHORUS_WIDTH)->load();
    t.chCharacter = rawParam (ParameterIDs::CHORUS_CHARACTER)->load();

    // EQ — bypass args are bool
    t.eqMasterBypass = rawParam (ParameterIDs::EQ_MASTER_BYPASS)->load() > 0.5f;
    t.eqHpFreq   = rawParam (ParameterIDs::EQ_HP_FREQ)->load();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParameterIDs::EQ_HP_SLOPE)))
        t.eqHpSlope = p->getIndex();
    t.eqHpBypass = rawParam (ParameterIDs::EQ_HP_BYPASS)->load() > 0.5f;
    t.eqLpFreq   = rawParam (ParameterIDs::EQ_LP_FREQ)->load();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParameterIDs::EQ_LP_SLOPE)))
        t.eqLpSlope = p->getIndex();
    t.eqLpBypass = rawParam (ParameterIDs::EQ_LP_BYPASS)->load() > 0.5f;

    static const char* freqIds[7] { ParameterIDs::EQ_B1_FREQ, ParameterIDs::EQ_B2_FREQ, ParameterIDs::EQ_B3_FREQ, ParameterIDs::EQ_B4_FREQ, ParameterIDs::EQ_B5_FREQ, ParameterIDs::EQ_B6_FREQ, ParameterIDs::EQ_B7_FREQ };
    static const char* gainIds[7] { ParameterIDs::EQ_B1_GAIN, ParameterIDs::EQ_B2_GAIN, ParameterIDs::EQ_B3_GAIN, ParameterIDs::EQ_B4_GAIN, ParameterIDs::EQ_B5_GAIN, ParameterIDs::EQ_B6_GAIN, ParameterIDs::EQ_B7_GAIN };
    static const char* qIds[7]    { ParameterIDs::EQ_B1_Q,    ParameterIDs::EQ_B2_Q,    ParameterIDs::EQ_B3_Q,    ParameterIDs::EQ_B4_Q,    ParameterIDs::EQ_B5_Q,    ParameterIDs::EQ_B6_Q,    ParameterIDs::EQ_B7_Q };
    static const char* bypIds[7]  { ParameterIDs::EQ_B1_BYPASS, ParameterIDs::EQ_B2_BYPASS, ParameterIDs::EQ_B3_BYPASS, ParameterIDs::EQ_B4_BYPASS, ParameterIDs::EQ_B5_BYPASS, ParameterIDs::EQ_B6_BYPASS, ParameterIDs::EQ_B7_BYPASS };
    for (int b = 0; b < 7; ++b)
    {
        t.bandFreq[b]   = rawParam (freqIds[b])->load();
        t.bandGain[b]   = rawParam (gainIds[b])->load();
        t.bandQ[b]      = rawParam (qIds[b])->load();
        t.bandBypass[b] = rawParam (bypIds[b])->load() > 0.5f;
    }

    return t;
}

//==============================================================================
// fb292 — Convolution USER IR loader. Decode IN-MEMORY (no disk — [[feedback-plugin-no-disk-writes-decode-in-memory]])
// → SR-correct to the host rate (one-time, offline → LagrangeInterpolator; else echoes run fast/slow + amplitude drifts)
// → onset-trim leading silence (else user IRs read as phantom latency) → channel-map → cap+fade → setUserIR. The engine's
// bake then applies Size/Decay/Attack/Distance/Density/Reverse + energy-normalizes, so a user IR behaves like a factory one.
static bool tw_decodeResampleTrimIR (juce::AudioFormatReader* reader, double hostSR,
                                     std::vector<float>& outL, std::vector<float>& outR)
{
    if (reader == nullptr) return false;
    const int srcLen = (int) reader->lengthInSamples;
    const int nCh    = (int) reader->numChannels;
    if (srcLen < 4 || nCh < 1) return false;
    // read the first two channels (the AudioBuffer<float> reader fills at most 2): mono → dual-mono ·
    // stereo → L/R · 4-ch true-stereo (LL/LR/RL/RR) degrades to ch0/ch1 (full true-stereo = a v1 follow-up).
    juce::AudioBuffer<float> src (2, srcLen); src.clear();
    reader->read (&src, 0, srcLen, 0, true, true);
    if (nCh == 1) src.copyFrom (1, 0, src, 0, 0, srcLen);   // ensure dual-mono (don't rely on reader duplication)
    const float* sL = src.getReadPointer (0);
    const float* sR = src.getReadPointer (1);
    const double srcSR = reader->sampleRate > 0.0 ? reader->sampleRate : hostSR;
    const double ratio = srcSR / (hostSR > 0.0 ? hostSR : srcSR);   // input samples consumed per output sample
    const int MAXLEN = ConvolutionReverb::MAXP * ConvolutionReverb::B;

    std::vector<float> rL, rR;
    if (std::abs (ratio - 1.0) < 1.0e-4)
    { rL.assign (sL, sL + srcLen); rR.assign (sR, sR + srcLen); }
    else
    {
        const int outLen = juce::jlimit (4, MAXLEN + 64, (int) std::floor ((double) srcLen / ratio));
        rL.assign ((size_t) outLen, 0.0f); rR.assign ((size_t) outLen, 0.0f);
        juce::LagrangeInterpolator iL, iR; iL.reset(); iR.reset();
        iL.process (ratio, sL, rL.data(), outLen);
        iR.process (ratio, sR, rR.data(), outLen);
    }
    int len = (int) rL.size();
    // onset-trim: first sample past -60 dB of peak, backed up 4 samples
    float pk = 1.0e-9f;
    for (int i = 0; i < len; ++i) { float a = std::fabs (rL[(size_t) i]), b = std::fabs (rR[(size_t) i]); float v = a > b ? a : b; if (v > pk) pk = v; }
    const float thr = pk * 0.001f;
    int onset = 0; while (onset < len && std::fabs (rL[(size_t) onset]) < thr && std::fabs (rR[(size_t) onset]) < thr) ++onset;
    onset = juce::jmax (0, onset - 4);
    int keep = len - onset; if (keep > MAXLEN) keep = MAXLEN;
    if (keep < 4) return false;
    outL.assign ((size_t) keep, 0.0f); outR.assign ((size_t) keep, 0.0f);
    for (int i = 0; i < keep; ++i) { outL[(size_t) i] = rL[(size_t) (onset + i)]; outR[(size_t) i] = rR[(size_t) (onset + i)]; }
    // if we truncated a longer IR, raised-cosine fade the last 64 samples so the cut doesn't gate-click
    if (onset + keep < len)
    { int fl = juce::jmin (64, keep);
      for (int i = 0; i < fl; ++i) { float g = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (fl - 1 - i) / (float) fl);
                                     outL[(size_t) (keep - fl + i)] *= g; outR[(size_t) (keep - fl + i)] *= g; } }
    return true;
}

bool TerrainInstrumentAudioProcessor::loadConvIRFromMemory (const void* data, size_t size, const juce::String& name)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (
        std::make_unique<juce::MemoryInputStream> (data, size, false)));
    std::vector<float> L, R;
    if (! tw_decodeResampleTrimIR (reader.get(), getSampleRate(), L, R)) return false;
    convolutionReverb.setUserIR (L.data(), R.data(), (int) L.size());
    convUserIrL_ = std::move (L); convUserIrR_ = std::move (R);   // fb311 — retain raw IR for preset serialization
    convIRName_ = name; convIRUser_ = true;
    return true;
}

bool TerrainInstrumentAudioProcessor::loadConvIRFromFile (const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    std::vector<float> L, R;
    if (! tw_decodeResampleTrimIR (reader.get(), getSampleRate(), L, R)) return false;
    convolutionReverb.setUserIR (L.data(), R.data(), (int) L.size());
    convUserIrL_ = std::move (L); convUserIrR_ = std::move (R);   // fb311 — retain raw IR for preset serialization
    convIRName_ = f.getFileName(); convIRUser_ = true;
    return true;
}

void TerrainInstrumentAudioProcessor::clearConvUserIR ()
{
    convolutionReverb.clearUserIR();
    convUserIrL_.clear(); convUserIrR_.clear();   // fb311
    convIRName_ = juce::String(); convIRUser_ = false;
}

// fb311 — serialize the RETAINED raw user IR as base64 float (L+R) so a Convolution preset recalls the EXACT
// one-shot. {} when no user IR. Cap ~6 s (matches the load cap) so a preset .json can't balloon unbounded.
juce::String TerrainInstrumentAudioProcessor::getConvIRRawJson () const
{
    if (! convIRUser_ || convUserIrL_.empty()) return "{}";
    const int   n   = (int) convUserIrL_.size();
    const int   nR  = (int) convUserIrR_.size();
    juce::MemoryBlock mbL (convUserIrL_.data(), (size_t) n  * sizeof (float));
    juce::MemoryBlock mbR (convUserIrR_.data(), (size_t) nR * sizeof (float));
    juce::String out;
    out << "{\"name\":" << convIRName_.quoted()
        << ",\"n\":"    << n
        << ",\"L\":\""  << mbL.toBase64Encoding() << "\""
        << ",\"R\":\""  << mbR.toBase64Encoding() << "\"}";
    return out;
}

// fb311 — restore a user IR from a preset's embedded base64. Runs on the message thread (native), same as the
// drag-drop load path; setUserIR is thread-safe (pre-reserved, no realloc). Non-destructive: bad/empty ⇒ no-op.
void TerrainInstrumentAudioProcessor::setConvIRRawFromJson (const juce::String& json)
{
    auto v = juce::JSON::parse (json);
    auto* o = v.getDynamicObject();
    if (o == nullptr) return;
    const juce::String bL = o->getProperty ("L").toString();
    const juce::String bR = o->getProperty ("R").toString();
    const juce::String nm = o->getProperty ("name").toString();
    if (bL.isEmpty()) return;
    juce::MemoryBlock mbL, mbR;
    if (! mbL.fromBase64Encoding (bL)) return;
    mbR.fromBase64Encoding (bR);
    const int nL = (int) (mbL.getSize() / sizeof (float));
    const int nR = (int) (mbR.getSize() / sizeof (float));
    if (nL <= 0) return;
    std::vector<float> L ((size_t) nL), R ((size_t) juce::jmax (nL, nR), 0.0f);
    std::memcpy (L.data(), mbL.getData(), (size_t) nL * sizeof (float));
    if (nR > 0) std::memcpy (R.data(), mbR.getData(), (size_t) nR * sizeof (float));
    else        R = L;                                   // mono → duplicate to R
    convolutionReverb.setUserIR (L.data(), R.data(), nL);
    convUserIrL_ = std::move (L); convUserIrR_ = std::move (R);
    convIRName_ = nm; convIRUser_ = true;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TerrainInstrumentAudioProcessor();
}
