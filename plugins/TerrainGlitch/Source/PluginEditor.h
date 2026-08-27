#pragma once
// =============================================================================
//  Terrain Glitch — PluginEditor.h
//  Waves Crate
//
//  Minimal by design: the Monitor card page rAF-polls its feed, so there is NO
//  push timer, NO keep-alive, NO frame arbiter here. A WebBrowserComponent with
//  a resource provider serving the embedded index.html (lane U's card page), a
//  WebSliderRelay + WebSliderParameterAttachment per FLOW_GLI_* param (relay
//  name == param id — Terrain's convention; 4-point bind law), the card's
//  natives (getGliFeed / gliRoll / setSynParam / getSynParam), and the fb95
//  resize law: fixed aspect, whole-page scale via WKWebView pageZoom.
//
//  The one timer below is a BOOT-RETRY for the zoom (the peer may not exist on
//  the first resized()); it counts down and stops itself — not a push loop.
// =============================================================================

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class TerrainGlitchAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit TerrainGlitchAudioProcessorEditor (TerrainGlitchAudioProcessor&);
    ~TerrainGlitchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Boot size = the card page's own viewport fallbacks (__vw 380 / __vh 640).
    // The page then pushes the card's EXACT natural size through its
    // resizeCardWindow native (ResizeObserver, Terrain's pop-out contract) and
    // adoptCardSize() re-bases the editor on it, preserving the user's zoom.
    static constexpr int kBaseW = 380;
    static constexpr int kBaseH = 451;

private:
    void timerCallback() override;
    void applyZoom();
    void adoptCardSize (int w, int h);

    TerrainGlitchAudioProcessor& audioProcessor;
    double baseW_ = (double) kBaseW, baseH_ = (double) kBaseH;

    // relays FIRST (they must outlive nothing, but must exist before the
    // webview's Options are built and stay alive while it does)
    juce::OwnedArray<juce::WebSliderRelay> relays_;
    std::unique_ptr<juce::WebBrowserComponent> webView;
    juce::OwnedArray<juce::WebSliderParameterAttachment> attachments_;

    int zoomPushLeft_ = 0;   // boot retries — timer stops itself at 0

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainGlitchAudioProcessorEditor)
};
