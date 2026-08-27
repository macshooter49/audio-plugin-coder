cmake_minimum_required(VERSION 3.22)

# ── Platform-specific WebView flags (cloned from TerrainInstrument) ──
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
#  TERRAIN GLITCH — the fb115 Monitor card as a standalone FX plugin. The DSP is
#  Terrain's own FlowGlitch.h, INCLUDED from the TerrainInstrument tree (never
#  copied): one truth — a Terrain engine fix reaches this plugin on the next build.
juce_add_plugin(TerrainGlitch
    COMPANY_NAME "Waves Crate"
    BUNDLE_ID "com.wavescrate.terrainglitch"
    PLUGIN_MANUFACTURER_CODE Wvcr
    PLUGIN_CODE Tgli
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Terrain Glitch"
    DESCRIPTION "Terrain Glitch — beat-synced buffer mangler (the FLOW Monitor card)"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_WEBVIEW2 ${NEEDS_WEBVIEW2}
    NEEDS_WEB_BROWSER ${NEEDS_WEB_BROWSER}
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    VST3_CATEGORIES Fx
)

# ── Embed web UI (the Monitor card page, delivered by lane U) as binary data ──
juce_add_binary_data(TerrainGlitch_WebUI
    SOURCES
        Source/ui/public/index.html
)

# ── Source files ──
target_sources(TerrainGlitch
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

# ── Include paths ──
#  The second entry is the ENGINE-INCLUDE LAW: FlowGlitch.h / FlowArp.h are compiled
#  from TerrainInstrument's Source directly (header-only, no JUCE) — never copied.
target_include_directories(TerrainGlitch
    PRIVATE
        Source
        ${CMAKE_CURRENT_SOURCE_DIR}/../TerrainInstrument/Source
)

# ── Link libraries ──
target_link_libraries(TerrainGlitch
    PRIVATE
        TerrainGlitch_WebUI
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

# ── MSVC readiness (cloned from TerrainInstrument fb474/fb488) ──
#  · _USE_MATH_DEFINES — MSVC's <cmath> does not define M_PI; PUBLIC so the format
#    wrappers that include our headers get it too (juce_add_plugin builds a STATIC lib).
#  · /bigobj /utf-8 /arch:AVX — same flags the Terrain sources are compiled with.
if (MSVC)
    target_compile_definitions(TerrainGlitch PUBLIC _USE_MATH_DEFINES)
    target_compile_options(TerrainGlitch PUBLIC /bigobj /utf-8 /arch:AVX)
    message(STATUS "TerrainGlitch: MSVC — _USE_MATH_DEFINES + /bigobj + /utf-8 + /arch:AVX")
endif()

# ── Compile definitions ──
target_compile_definitions(TerrainGlitch
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
