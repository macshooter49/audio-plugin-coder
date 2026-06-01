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
juce_add_plugin(TerrainInstrument
    COMPANY_NAME "Waves Crate"
    BUNDLE_ID "com.wavescrate.terraininstrument"
    PLUGIN_MANUFACTURER_CODE Wvcr
    PLUGIN_CODE Tern
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Terrain Instrument"
    DESCRIPTION "Terrain — Sampler / Instrument"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.png"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_WEBVIEW2 ${NEEDS_WEBVIEW2}
    NEEDS_WEB_BROWSER ${NEEDS_WEB_BROWSER}
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Sampler
)

# ── Embed web UI files as binary data ──
juce_add_binary_data(TerrainInstrument_WebUI
    SOURCES
        Source/ui/public/index.html
)

# ── Source files ──
target_sources(TerrainInstrument
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ParametricEQ_test.cpp
        Source/MoogDelay_test.cpp
        Source/TerrainChorus_test.cpp
        Source/Warp/WarpProcessor_test.cpp
        Source/LayerState_test.cpp
        Source/FxMask_test.cpp
        Source/IndyFxChain_test.cpp
        Source/SynthVoice_test.cpp
)

# ── Include paths ──
target_include_directories(TerrainInstrument
    PRIVATE
        Source
)

# ── Link libraries ──
target_link_libraries(TerrainInstrument
    PRIVATE
        TerrainInstrument_WebUI
        signalsmith-stretch
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
target_compile_definitions(TerrainInstrument
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
