#pragma once
// =============================================================================
//  Terrain Chop — editor. The whole UI is lane U's Ribbon-card page (index.html,
//  embedded as binary data), shown in a WebBrowserComponent that fills the
//  editor. Poll-driven page (rAF-polls getChopFeed) — no push timer here.
//  Waves Crate
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class TerrainChopAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit TerrainChopAudioProcessorEditor (TerrainChopAudioProcessor&);
    ~TerrainChopAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    TerrainChopAudioProcessor& audioProcessor;

    // the page's card pushes its natural CSS size via the resizeCardWindow
    // native; that size becomes the zoom base (until then: the 820x460 default)
    int baseW_, baseH_;

    // relays named by param id — one per control the fb106 BIND block touches
    // (declared BEFORE the web view, destroyed after it)
    std::vector<std::unique_ptr<juce::WebSliderRelay>> relays_;
    std::unique_ptr<juce::WebBrowserComponent> webView;
    // attachments last: destroyed first, while params + relays still exist
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> attachments_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainChopAudioProcessorEditor)
};
