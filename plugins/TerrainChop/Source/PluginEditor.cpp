// =============================================================================
//  Terrain Chop — editor implementation.
//  Waves Crate
// =============================================================================

#include "PluginEditor.h"
#include "BinaryData.h"

#if JUCE_MAC
 #include <objc/message.h>
 #include <objc/runtime.h>

// fb132 — user presets on disk, one JSON per preset (cloned from TerrainGlitch's tg* helpers)
static juce::File tcPresetDir (const juce::String& card)
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
             .getChildFile ("Library/WavesCrate/TerrainChop/presets")
             .getChildFile (card.retainCharacters ("abcdefghijklmnopqrstuvwxyz"));
}

static juce::String tcSafePresetName (const juce::String& name)
{
    juce::String out;
    const auto trimmed = name.trim().substring (0, 48);
    for (int i = 0; i < trimmed.length(); ++i)
    {
        const auto c = trimmed[i];
        const bool ok = juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ' || c == '-' || c == '_';
        out << (ok ? juce::String::charToString (c) : juce::String ("_"));
    }
    return out.trim();
}
#endif

// lane U's page is authored at this base size; fb95 resize scales it via pageZoom
static constexpr int kBaseW = 380, kBaseH = 441;   // laneU-measured natural card size — no first-open jump

// ─────────────────────────────────────────────────────────────────────────────
//  Every parameter the fb106 BIND block touches, including the five FLOW_SEQ_*
//  macros and the mix header (FLOW_CHOP_BLEND) — i.e. the full APVTS. The page
//  binds sliders, toggles and choices all through getSliderState(pid) with a
//  steps divisor, so a WebSliderRelay per id covers every control kind.
// ─────────────────────────────────────────────────────────────────────────────
static const char* const kChopParamIds[] = {
    ParameterIDs::FLOW_SEQ_RATE,    ParameterIDs::FLOW_SEQ_GATE,
    ParameterIDs::FLOW_SEQ_VARY,    ParameterIDs::FLOW_SEQ_TRAJ,
    ParameterIDs::FLOW_SEQ_MORPH,
    ParameterIDs::FLOW_CHOP_BLEND,
    ParameterIDs::FLOW_CHOP_CATCH,  ParameterIDs::FLOW_CHOP_SLICES,
    ParameterIDs::FLOW_CHOP_LOOP,   ParameterIDs::FLOW_CHOP_MODE,
    ParameterIDs::FLOW_CHOP_RPTS,   ParameterIDs::FLOW_CHOP_FILTER,
    ParameterIDs::FLOW_CHOP_FREEZE, ParameterIDs::FLOW_CHOP_COLLECT,
    ParameterIDs::FLOW_CHOP_SCAN,   ParameterIDs::FLOW_CHOP_WANDER,
    ParameterIDs::FLOW_CHOP_SPREAD, ParameterIDs::FLOW_CHOP_SPEED,
    ParameterIDs::FLOW_CHOP_STEPS,  ParameterIDs::FLOW_CHOP_DETUNE,
    ParameterIDs::FLOW_CHOP_WOW,    ParameterIDs::FLOW_CHOP_SMOOTH,
    ParameterIDs::FLOW_CHOP_GRIT,   ParameterIDs::FLOW_CHOP_TRIM,
    ParameterIDs::FLOW_CHOP_O_SPREAD, ParameterIDs::FLOW_CHOP_O_BIAS,
    ParameterIDs::FLOW_CHOP_O_LOCK,   ParameterIDs::FLOW_CHOP_O_SEED,
    ParameterIDs::FLOW_CHOP_P_RANGE,  ParameterIDs::FLOW_CHOP_P_STEPS,
    ParameterIDs::FLOW_CHOP_P_GLIDE,  ParameterIDs::FLOW_CHOP_P_QUANT,
    ParameterIDs::FLOW_CHOP_RV_ODDS,  ParameterIDs::FLOW_CHOP_RV_RUN,
    ParameterIDs::FLOW_CHOP_RV_SPREAD,ParameterIDs::FLOW_CHOP_RV_SNAP,
    ParameterIDs::FLOW_CHOP_T_LEN,    ParameterIDs::FLOW_CHOP_T_CURVE,
    ParameterIDs::FLOW_CHOP_T_RAND,   ParameterIDs::FLOW_CHOP_T_GATE,
    ParameterIDs::FLOW_CHOP_R_COUNT,  ParameterIDs::FLOW_CHOP_R_DECAY,
    ParameterIDs::FLOW_CHOP_R_CURVE,  ParameterIDs::FLOW_CHOP_R_ODDS,
    ParameterIDs::FLOW_CHOP_D_AMT,    ParameterIDs::FLOW_CHOP_D_SIZE,
    ParameterIDs::FLOW_CHOP_D_SPRAY,  ParameterIDs::FLOW_CHOP_D_TONE,
};

#if JUCE_MAC
// fb95 (simplified) — editor resize scales the WHOLE web UI via the native
// WKWebView pageZoom (real browser zoom: JS coordinate APIs stay consistent).
// We reach the WKWebView by walking the editor peer's NSView tree. Also kills
// WKWebView's white backing (the open-flash) via KVC, like Terrain fb148.
static void chopApplyWebScale (juce::Component& root, double pageZoom)
{
    auto* peer = root.getPeer();
    if (peer == nullptr) return;
    id rootView = (id) peer->getNativeHandle();
    Class wkClass = objc_getClass ("WKWebView");
    if (rootView == nullptr || wkClass == nullptr) return;
    std::vector<id> stack { rootView };
    while (! stack.empty())
    {
        id v = stack.back(); stack.pop_back();
        if (((bool (*) (id, SEL, Class)) objc_msgSend) (v, sel_registerName ("isKindOfClass:"), wkClass))
        {
            {
                id no  = ((id (*) (Class, SEL, signed char)) objc_msgSend) (objc_getClass ("NSNumber"), sel_registerName ("numberWithBool:"), 0);
                id key = ((id (*) (Class, SEL, const char*)) objc_msgSend) (objc_getClass ("NSString"), sel_registerName ("stringWithUTF8String:"), "drawsBackground");
                ((void (*) (id, SEL, id, id)) objc_msgSend) (v, sel_registerName ("setValue:forKey:"), no, key);
            }
            if (((bool (*) (id, SEL, SEL)) objc_msgSend) (v, sel_registerName ("respondsToSelector:"), sel_registerName ("setPageZoom:")))
                ((void (*) (id, SEL, double)) objc_msgSend) (v, sel_registerName ("setPageZoom:"), pageZoom);
            return;
        }
        id subs = ((id (*) (id, SEL)) objc_msgSend) (v, sel_registerName ("subviews"));
        if (subs == nullptr) continue;
        const auto nSubs = ((unsigned long (*) (id, SEL)) objc_msgSend) (subs, sel_registerName ("count"));
        for (unsigned long i = 0; i < nSubs; ++i)
            stack.push_back (((id (*) (id, SEL, unsigned long)) objc_msgSend) (subs, sel_registerName ("objectAtIndex:"), i));
    }
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
TerrainChopAudioProcessorEditor::TerrainChopAudioProcessorEditor (TerrainChopAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), audioProcessor (p), baseW_ (kBaseW), baseH_ (kBaseH)
{
    // relays first — the options borrow them, the web view keeps them wired
    for (auto* id : kChopParamIds)
        relays_.push_back (std::make_unique<juce::WebSliderRelay> (id));

    auto options = juce::WebBrowserComponent::Options()
        .withKeepPageLoadedWhenBrowserIsHidden()   // fb148 — hosts hide/show plugin windows
       #if JUCE_WINDOWS
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (
            juce::WebBrowserComponent::Options::WinWebView2{}
                .withUserDataFolder (juce::File::getSpecialLocation (
                    juce::File::SpecialLocationType::tempDirectory)))
       #endif
        .withNativeIntegrationEnabled()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
        .withNativeFunction ("getChopFeed", [this] (const juce::Array<juce::var>&,
                                                    juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            // fb106 — Ribbon playhead/slice/wet snapshot (rAF-polled by the chop card)
            complete (juce::var (audioProcessor.getChopFeedJson()));
        })
        .withNativeFunction ("chopWipe", [this] (const juce::Array<juce::var>&,
                                                 juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            audioProcessor.requestChopWipe();   // fb106 — Wipe: clear the chop memory
            complete (juce::var{});
        })
        .withNativeFunction ("setSynParam", [this] (const juce::Array<juce::var>& args,
                                                    juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            // DIRECT APVTS write (the page's __synSliderShim fallback path).
            // args = [ paramId (string), normalised 0..1 ].
            if (args.size() >= 2)
                if (auto* prm = audioProcessor.getAPVTS().getParameter (args[0].toString()))
                    prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (float) (double) args[1]));
            complete (juce::var{});
        })
        .withNativeFunction ("getSynParam", [this] (const juce::Array<juce::var>& args,
                                                    juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            // READ a param's normalised value (0..1) so the page can restore
            // display state on reopen without a relay. args = [ paramId ].
            float v = 0.0f;
            if (args.size() >= 1)
                if (auto* prm = audioProcessor.getAPVTS().getParameter (args[0].toString()))
                    v = prm->getValue();
            complete (juce::var (v));
        })
        .withNativeFunction ("resizeCardWindow", [this] (const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            // the card pushes its natural CSS size (the page hides its resize
            // grip when this native is absent). The pushed size becomes the
            // zoom BASE; the user's current scale factor is preserved.
            if (args.size() >= 2)
            {
                const int w = (int) args[0], h = (int) args[1];
                if (w >= 200 && w <= 4000 && h >= 150 && h <= 4000
                    && (std::abs (w - baseW_) > 1 || std::abs (h - baseH_) > 1))
                {
                    const double zoom = baseW_ > 0 ? (double) getWidth() / (double) baseW_ : 1.0;
                    baseW_ = w; baseH_ = h;
                    setResizeLimits (juce::roundToInt (baseW_ * 0.65), juce::roundToInt (baseH_ * 0.65),
                                     juce::roundToInt (baseW_ * 1.90), juce::roundToInt (baseH_ * 1.90));
                    if (auto* cons = getConstrainer())
                        cons->setFixedAspectRatio ((double) baseW_ / (double) baseH_);
                    setSize (juce::roundToInt (baseW_ * zoom), juce::roundToInt (baseH_ * zoom));
                }
            }
            complete (juce::var{});
        })
        // fb137/fb132 — the card's slots+chain + user presets: the page calls these guarded
        // (silent no-op when absent); registering them lights the features up. Cloned from
        // TerrainGlitch signature-for-signature.
        .withNativeFunction ("setCardState", [this] (const juce::Array<juce::var>& args,
                                                     juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            if (args.size() >= 2)
                audioProcessor.setCardStateJson (args[0].toString(), args[1].toString());
            complete (juce::var{});
        })
        .withNativeFunction ("getCardState", [this] (const juce::Array<juce::var>& args,
                                                     juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            complete (juce::var (args.size() >= 1 ? audioProcessor.getCardStateJson (args[0].toString())
                                                  : juce::String()));
        })
        .withNativeFunction ("savePreset", [] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            // fb132 — args = [card, name, payloadJson]; same-name overwrite is the save semantics
            if (args.size() >= 3)
            {
                auto dir = tcPresetDir (args[0].toString());
                dir.createDirectory();
                dir.getChildFile (tcSafePresetName (args[1].toString()) + ".json")
                   .replaceWithText (args[2].toString());
            }
            complete (juce::var{});
        })
        .withNativeFunction ("getPresets", [] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            juce::String out ("[");
            if (args.size() >= 1)
            {
                bool first = true;
                for (const auto& f : tcPresetDir (args[0].toString())
                                         .findChildFiles (juce::File::findFiles, false, "*.json"))
                {
                    const auto t = f.loadFileAsString().trim();
                    if (t.startsWith ("{") && t.endsWith ("}"))
                    {
                        if (! first) out << ",";
                        out << t; first = false;
                    }
                }
            }
            out << "]";
            complete (juce::var (out));
        })
        .withNativeFunction ("deletePreset", [] (const juce::Array<juce::var>& args,
                                                 juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            if (args.size() >= 2)
                tcPresetDir (args[0].toString())
                    .getChildFile (tcSafePresetName (args[1].toString()) + ".json").deleteFile();
            complete (juce::var{});
        });

    for (auto& r : relays_)
        options = options.withOptionsFrom (*r);

    webView = std::make_unique<juce::WebBrowserComponent> (options);

    // attach every relay to its parameter (id string == relay name)
    for (size_t i = 0; i < relays_.size(); ++i)
        if (auto* prm = audioProcessor.getAPVTS().getParameter (kChopParamIds[i]))
            attachments_.push_back (std::make_unique<juce::WebSliderParameterAttachment> (*prm, *relays_[i], nullptr));

    addAndMakeVisible (*webView);
    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizeLimits (juce::roundToInt (kBaseW * 0.65), juce::roundToInt (kBaseH * 0.65),
                     juce::roundToInt (kBaseW * 1.90), juce::roundToInt (kBaseH * 1.90));
    if (auto* cons = getConstrainer())
        cons->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setResizable (true, true);
    setSize (kBaseW, kBaseH);

    // fb95 — keep pageZoom in step with the editor width (the peer may not
    // exist yet at construction, so a light timer re-applies; idempotent)
    startTimerHz (10);
}

TerrainChopAudioProcessorEditor::~TerrainChopAudioProcessorEditor()
{
    stopTimer();
}

void TerrainChopAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0d16));   // page ground — no white open-flash
}

void TerrainChopAudioProcessorEditor::resized()
{
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());
   #if JUCE_MAC
    chopApplyWebScale (*this, (double) getWidth() / (double) baseW_);
   #endif
}

void TerrainChopAudioProcessorEditor::timerCallback()
{
   #if JUCE_MAC
    chopApplyWebScale (*this, (double) getWidth() / (double) baseW_);
   #endif
}

std::optional<juce::WebBrowserComponent::Resource>
TerrainChopAudioProcessorEditor::getResource (const juce::String& url)
{
    // All JS is inlined into index.html — only one resource to serve
    if (url == "/" || url.endsWith ("index.html"))
    {
        std::vector<std::byte> bytes ((size_t) BinaryData::index_htmlSize);
        std::memcpy (bytes.data(), BinaryData::index_html, (size_t) BinaryData::index_htmlSize);
        return juce::WebBrowserComponent::Resource { std::move (bytes),
                                                     juce::String ("text/html; charset=utf-8") };
    }
    return std::nullopt;
}
