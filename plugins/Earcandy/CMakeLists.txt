cmake_minimum_required(VERSION 3.22)

# ── Platform-specific WebView flags ──
if(WIN32)
    set(NEEDS_WEBVIEW2 TRUE)
    set(NEEDS_WEB_BROWSER FALSE)
elseif(UNIX AND NOT APPLE)
    set(NEEDS_WEBVIEW2 FALSE)
    set(NEEDS_WEB_BROWSER TRUE)
else()
    # macOS: WKWebView is a system framework, no special flags needed
    set(NEEDS_WEBVIEW2 FALSE)
    set(NEEDS_WEB_BROWSER FALSE)
endif()

# ── Plugin definition ──
juce_add_plugin(Earcandy
    COMPANY_NAME "APC"
    PLUGIN_MANUFACTURER_CODE Apcc
    PLUGIN_CODE Ecdy
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Earcandy"
    NEEDS_WEBVIEW2 ${NEEDS_WEBVIEW2}
    NEEDS_WEB_BROWSER ${NEEDS_WEB_BROWSER}
)

# ── Embed web UI files as binary data ──
# All JS is inlined into index.html (module imports don't work in WKWebView custom schemes)
juce_add_binary_data(Earcandy_WebUI
    SOURCES
        Source/ui/public/index.html
)

# ── Source files ──
target_sources(Earcandy
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

# ── Include paths ──
target_include_directories(Earcandy
    PRIVATE
        Source
)

# ── Link libraries ──
target_link_libraries(Earcandy
    PRIVATE
        Earcandy_WebUI
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ── Compile definitions ──
target_compile_definitions(Earcandy
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
