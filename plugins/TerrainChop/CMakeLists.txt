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
#  TERRAIN CHOP — the fb106 Ribbon card as a standalone FX plugin. The DSP is
#  Terrain's own FlowChop.h, INCLUDED from the TerrainInstrument tree (never
#  copied): one truth — a Terrain engine fix reaches this plugin on the next build.
juce_add_plugin(TerrainChop
    COMPANY_NAME "Waves Crate"
    BUNDLE_ID "com.wavescrate.terrainchop"
    PLUGIN_MANUFACTURER_CODE Wvcr
    PLUGIN_CODE Tchp
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Terrain Chop"
    DESCRIPTION "Terrain Chop — real-time slice re-groover (the FLOW Ribbon card)"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_WEBVIEW2 ${NEEDS_WEBVIEW2}
    NEEDS_WEB_BROWSER ${NEEDS_WEB_BROWSER}
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    VST3_CATEGORIES Fx
)

# ── Embed web UI (the Ribbon card page, delivered by lane U) as binary data ──
#  Until lane U's index.html lands, a configure-time placeholder (generated into
#  the BINARY dir, never into Source/) keeps the target buildable; the condition
#  re-evaluates on every configure, so the real page takes over automatically.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/public/index.html")
    set(TERRAINCHOP_UI_HTML "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/public/index.html")
    message(STATUS "TerrainChop: embedding Source/ui/public/index.html")
else()
    set(TERRAINCHOP_UI_HTML "${CMAKE_CURRENT_BINARY_DIR}/placeholder_ui/index.html")
    file(WRITE "${TERRAINCHOP_UI_HTML}"
        "<!DOCTYPE html><html><body style=\"background:#0d0d16;color:#eee;font:12px -apple-system,sans-serif;display:flex;align-items:center;justify-content:center;height:100vh;margin:0\">Terrain Chop &mdash; UI pending (lane U index.html not present at configure time)</body></html>")
    message(STATUS "TerrainChop: lane U index.html NOT FOUND — embedding placeholder page (re-run cmake once it lands)")
endif()

juce_add_binary_data(TerrainChop_WebUI
    SOURCES
        ${TERRAINCHOP_UI_HTML}
)

# ── Source files ──
target_sources(TerrainChop
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

# ── Include paths ──
#  The second entry is the ENGINE-INCLUDE LAW: FlowChop.h / FlowArp.h /
#  ParameterIDs.hpp are compiled from TerrainInstrument's Source directly
#  (header-only, no JUCE in the engines) — never copied.
target_include_directories(TerrainChop
    PRIVATE
        Source
        ${CMAKE_CURRENT_SOURCE_DIR}/../TerrainInstrument/Source
)

# ── Link libraries ──
target_link_libraries(TerrainChop
    PRIVATE
        TerrainChop_WebUI
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
    target_compile_definitions(TerrainChop PUBLIC _USE_MATH_DEFINES)
    target_compile_options(TerrainChop PUBLIC /bigobj /utf-8 /arch:AVX)
    message(STATUS "TerrainChop: MSVC — _USE_MATH_DEFINES + /bigobj + /utf-8 + /arch:AVX")
endif()

# ── Compile definitions ──
target_compile_definitions(TerrainChop
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
