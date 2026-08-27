// =============================================================================
//  Terrain Glitch — PluginEditor.cpp
//  Waves Crate
//
//  Shapes cloned from Terrain Instrument's PluginEditor.cpp:
//    · WebBrowserComponent Options (:199 region) — keepPageLoadedWhenBrowserIsHidden,
//      webview2 backend + temp user-data folder, native integration
//    · the card natives — getGliFeed / gliRoll (:1091-1102) + setSynParam /
//      getSynParam (:815-839, the __synSliderShim backend the fb115 BIND rides)
//    · getResource — a plain build (this page is small; no fb514 cache needed)
//    · fb95 resize law — whole-page scale via the native WKWebView pageZoom
//      (terrainApplyWebScale, simplified: single-phase, page told via __setUIScale)
// =============================================================================

#include "PluginEditor.h"
#include "BinaryData.h"

#if JUCE_MAC
 #include <objc/message.h>
 #include <objc/runtime.h>
#endif

//==============================================================================
// fb132 — the canonical per-card preset natives (Terrain PluginEditor.cpp :26/:52,
// cloned; this plugin's OWN directory so the two products never collide on disk)
static juce::File tgPresetDir (const juce::String& card)
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
             .getChildFile ("Library/WavesCrate/TerrainGlitch/presets")
             .getChildFile (card.retainCharacters ("abcdefghijklmnopqrstuvwxyz"));
}

static juce::String tgSafePresetName (const juce::String& name)
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

//==============================================================================
// fb95 — editor resize scales the WHOLE web UI via the native WKWebView pageZoom
// (macOS 11+). Simplified from Terrain's two-phase version: this page is a single
// card, so one crisp settle per resize is enough. Windows/Linux fall through to
// the page-side __setUIScale hook (pushed from applyZoom()).
#if JUCE_MAC
static void terrainGlitchApplyWebScale (juce::Component& root, double pageZoom)
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
            // fb148 — kill WKWebView's WHITE backing (the open-flash): drawsBackground=NO via KVC
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
#else
static void terrainGlitchApplyWebScale (juce::Component&, double) {}
#endif

//==============================================================================
// Every FLOW_GLI_* param the card can bind — relay name == param id (Terrain's
// convention). The 32 per-effect routing ids are generated exactly the way the
// processor registers them.
static juce::StringArray terrainGlitchParamIds()
{
    juce::StringArray ids {
        "FLOW_GLI_BLEND", "FLOW_GLI_RATE", "FLOW_GLI_GATE", "FLOW_GLI_VARY", "FLOW_GLI_TRAJ", "FLOW_GLI_MORPH",
        "FLOW_GLI_EN_REP", "FLOW_GLI_EN_REV", "FLOW_GLI_EN_TAPE", "FLOW_GLI_EN_GATE",
        "FLOW_GLI_EN_PIT", "FLOW_GLI_EN_CRSH", "FLOW_GLI_EN_FRZ", "FLOW_GLI_EN_SCT",
        "FLOW_GLI_HOLD", "FLOW_GLI_LOOP", "FLOW_GLI_QUANT", "FLOW_GLI_RELEASE",
        "FLOW_GLI_FILTER", "FLOW_GLI_PAN", "FLOW_GLI_SYNC",
        "FLOW_GLI_DEJAVU", "FLOW_GLI_DECAY", "FLOW_GLI_OUTMODE", "FLOW_GLI_PING",
        "FLOW_GLI_DROP", "FLOW_GLI_BURST", "FLOW_GLI_BEND", "FLOW_GLI_SEED",
        "FLOW_GLI_REP_SIZE", "FLOW_GLI_REP_SPEED", "FLOW_GLI_REP_FADE", "FLOW_GLI_REP_VARY",
        "FLOW_GLI_REV_LEN", "FLOW_GLI_REV_FADE", "FLOW_GLI_REV_SPRD", "FLOW_GLI_REV_SNAP",
        "FLOW_GLI_TAPE_CURVE", "FLOW_GLI_TAPE_TIME", "FLOW_GLI_TAPE_DEPTH", "FLOW_GLI_TAPE_SPIN",
        "FLOW_GLI_GATE_RATE", "FLOW_GLI_GATE_SHAPE", "FLOW_GLI_GATE_NUDGE", "FLOW_GLI_GATE_AMT",
        "FLOW_GLI_PIT_SHIFT", "FLOW_GLI_PIT_WALK", "FLOW_GLI_PIT_GLIDE", "FLOW_GLI_PIT_JUMP",
        "FLOW_GLI_CRSH_BITS", "FLOW_GLI_CRSH_RATE", "FLOW_GLI_CRSH_TONE", "FLOW_GLI_CRSH_AMT",
        "FLOW_GLI_FRZ_SIZE", "FLOW_GLI_FRZ_SPRAY", "FLOW_GLI_FRZ_SHINE", "FLOW_GLI_FRZ_MELT",
        "FLOW_GLI_SCT_SIZE", "FLOW_GLI_SCT_AMT", "FLOW_GLI_SCT_VARY", "FLOW_GLI_SCT_WIDTH"
    };
    static const char* fx[8] = { "REP", "REV", "TAPE", "GATE", "PIT", "CRSH", "FRZ", "SCT" };
    for (int i = 0; i < 8; ++i)
    {
        ids.add (juce::String ("FLOW_GLI_") + fx[i] + "_FLT");
        ids.add (juce::String ("FLOW_GLI_") + fx[i] + "_PAN");
        ids.add (juce::String ("FLOW_GLI_") + fx[i] + "_TRG");
        ids.add (juce::String ("FLOW_GLI_") + fx[i] + "_GRID");
    }
    return ids;
}

//==============================================================================
TerrainGlitchAudioProcessorEditor::TerrainGlitchAudioProcessorEditor (TerrainGlitchAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    const auto ids = terrainGlitchParamIds();

    // (1) relay member per bound param — relay name == param id
    for (const auto& id : ids)
        relays_.add (new juce::WebSliderRelay (id));

    // (2) .withOptionsFrom every relay + the card's natives + the resource provider
    auto options = juce::WebBrowserComponent::Options{}
        .withKeepPageLoadedWhenBrowserIsHidden()   // fb148 — FL hides/shows plugin windows
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (
            juce::WebBrowserComponent::Options::WinWebView2{}
                .withUserDataFolder (juce::File::getSpecialLocation (
                    juce::File::SpecialLocationType::tempDirectory)))
        .withNativeIntegrationEnabled()
        .withNativeFunction ("getGliFeed", [this] (const juce::Array<juce::var>&,
                                                   juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            // fb115 — Monitor playhead/fire/levels snapshot (rAF-polled by the glitch card)
            complete (juce::var (audioProcessor.getGliFeedJson()));
        })
        .withNativeFunction ("gliRoll", [this] (const juce::Array<juce::var>&,
                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            audioProcessor.requestGliRoll();    // fb115 — Roll: quantized punch-in
            complete (juce::var{});
        })
        .withNativeFunction ("setSynParam", [this] (const juce::Array<juce::var>& args,
                                                    juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            // DIRECT APVTS write — the __synSliderShim backend the fb115 BIND block
            // rides for non-relayed controls. args = [ paramId (string), normalised 0..1 ].
            if (args.size() >= 2)
                if (auto* prm = audioProcessor.getAPVTS().getParameter (args[0].toString()))
                    prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (args[1]))));
            complete (juce::var{});
        })
        .withNativeFunction ("getSynParam", [this] (const juce::Array<juce::var>& args,
                                                    juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            // READ a param's normalised value (0..1) so the card restores its display
            // state on reopen (the state-persists law). args = [ paramId ].
            float v = 0.0f;
            if (args.size() >= 1)
                if (auto* prm = audioProcessor.getAPVTS().getParameter (args[0].toString()))
                    v = prm->getValue();
            complete (juce::var (v));
        })
        .withNativeFunction ("resizeCardWindow", [this] (const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            // The card-only page reports the card's offset size (ResizeObserver) so
            // the editor always fits it exactly — subtab switches resize live
            // (Terrain's pop-out contract, TerrainCardWindow :5143-5156).
            if (args.size() >= 2)
                adoptCardSize ((int) static_cast<double> (args[0]), (int) static_cast<double> (args[1]));
            complete (juce::var{});
        })
        .withNativeFunction ("setCardState", [this] (const juce::Array<juce::var>& args,
                                                     juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            // fb137 — [card, slotsChainJson]: the processor holds one truth
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
                auto dir = tgPresetDir (args[0].toString());
                dir.createDirectory();
                dir.getChildFile (tgSafePresetName (args[1].toString()) + ".json")
                   .replaceWithText (args[2].toString());
            }
            complete (juce::var{});
        })
        .withNativeFunction ("getPresets", [] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
        {
            // fb132 — returns a JSON array of the card's saved payloads (each file is one object)
            juce::String out ("[");
            if (args.size() >= 1)
            {
                bool first = true;
                for (const auto& f : tgPresetDir (args[0].toString())
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
                tgPresetDir (args[0].toString())
                    .getChildFile (tgSafePresetName (args[1].toString()) + ".json").deleteFile();
            complete (juce::var{});
        })
        .withResourceProvider ([] (const auto&) -> std::optional<juce::WebBrowserComponent::Resource>
        {
            // All JS is inlined into index.html — only one resource to serve (a plain
            // build; the card page is small, no fb514 byte-cache needed)
            const auto* data = BinaryData::index_html;
            const auto  size = (size_t) BinaryData::index_htmlSize;
            std::vector<std::byte> bytes ((size_t) size);
            std::memcpy (bytes.data(), data, size);
            return juce::WebBrowserComponent::Resource { std::move (bytes),
                                                         juce::String ("text/html; charset=utf-8") };
        });

    for (auto* r : relays_)
        options = options.withOptionsFrom (*r);

    webView = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*webView);
    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    // (3) attachment per param — created AFTER the webview, Terrain's order
    auto& apvts = audioProcessor.getAPVTS();
    for (int i = 0; i < ids.size(); ++i)
        if (auto* prm = apvts.getParameter (ids[i]))
            attachments_.add (new juce::WebSliderParameterAttachment (*prm, *relays_[i], nullptr));
    // (4) — the JS side of the bind chain lives in the card page (lane U)

    // fb95 — every plugin resizes: fixed aspect, whole-page zoom. The base is
    // re-adopted from the card's own resizeCardWindow push once the page boots.
    setResizeLimits (juce::roundToInt (kBaseW * 0.65), juce::roundToInt (kBaseH * 0.65),
                     kBaseW * 2, kBaseH * 2);
    if (auto* cons = getConstrainer())
        cons->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setResizable (true, true);
    setSize (kBaseW, kBaseH);

    // boot retries: the peer may not exist on the first resized(); idempotent,
    // and the timer STOPS ITSELF when the retries run out (not a push loop)
    zoomPushLeft_ = 40;
    startTimerHz (15);
}

TerrainGlitchAudioProcessorEditor::~TerrainGlitchAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void TerrainGlitchAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF14141F));   // card-dark behind the WebView's first frame
}

void TerrainGlitchAudioProcessorEditor::resized()
{
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());
    applyZoom();
}

void TerrainGlitchAudioProcessorEditor::applyZoom()
{
    if (webView == nullptr || getWidth() <= 0 || baseW_ <= 0) return;
    const double zoom = (double) getWidth() / baseW_;
    terrainGlitchApplyWebScale (*this, zoom);
    // page-side hook (Windows/Linux path + canvas re-buffer at true resolution)
    webView->evaluateJavascript ("window.__setUIScale&&window.__setUIScale(" + juce::String (zoom, 4) + ");");
}

void TerrainGlitchAudioProcessorEditor::adoptCardSize (int w, int h)
{
    // sanity clamp = Terrain's (w/h > 60); re-base preserving the user's zoom
    if (w <= 60 || h <= 60) return;
    if (std::abs ((double) w - baseW_) < 0.5 && std::abs ((double) h - baseH_) < 0.5) return;
    const double zoom = baseW_ > 0 ? (double) getWidth() / baseW_ : 1.0;
    baseW_ = (double) w; baseH_ = (double) h;
    setResizeLimits (juce::roundToInt (w * 0.65), juce::roundToInt (h * 0.65), w * 2, h * 2);
    if (auto* cons = getConstrainer())
        cons->setFixedAspectRatio ((double) w / (double) h);
    const int nw = juce::roundToInt (w * zoom), nh = juce::roundToInt (h * zoom);
    if (nw != getWidth() || nh != getHeight()) setSize (nw, nh);
    else                                       applyZoom();
}

void TerrainGlitchAudioProcessorEditor::timerCallback()
{
    applyZoom();
    if (--zoomPushLeft_ <= 0)
        stopTimer();
}
