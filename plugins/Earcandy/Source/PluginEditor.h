#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "ParameterIDs.hpp"

class EarcandyAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    EarcandyAudioProcessorEditor (EarcandyAudioProcessor&);
    ~EarcandyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    EarcandyAudioProcessor& audioProcessor;

    // ═══════════════════════════════════════════════════════════════
    // CRITICAL: Member Declaration Order (Prevents DAW Crashes)
    // C++ destroys members in REVERSE order of declaration.
    // CORRECT ORDER: Relays → WebView → Attachments
    // ═══════════════════════════════════════════════════════════════

    // 1. PARAMETER RELAYS FIRST (destroyed last)
    juce::WebSliderRelay grainSizeRelay  { ParameterIDs::GRAIN_SIZE };
    juce::WebSliderRelay densityRelay    { ParameterIDs::DENSITY };
    juce::WebSliderRelay sprayRelay      { ParameterIDs::SPRAY };
    juce::WebSliderRelay pitchRelay      { ParameterIDs::PITCH };
    juce::WebSliderRelay feedbackRelay   { ParameterIDs::FEEDBACK };
    juce::WebSliderRelay mixRelay        { ParameterIDs::MIX };

    // 2. WEBVIEW SECOND (destroyed middle)
    std::unique_ptr<juce::WebBrowserComponent> webView;

    // 3. PARAMETER ATTACHMENTS LAST (destroyed first)
    std::unique_ptr<juce::WebSliderParameterAttachment> grainSizeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> densityAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> sprayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> pitchAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> feedbackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> mixAttachment;

    // Resource provider
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EarcandyAudioProcessorEditor)
};
