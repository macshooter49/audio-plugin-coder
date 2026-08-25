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
        Source/Wavetable_test.cpp
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

# ══ fb474 — WINDOWS BUILD READINESS ═══════════════════════════════════════════════════════════
#  This target has only ever been compiled by clang. Three things MSVC does differently, all found
#  by inspection before the first Windows build rather than after it:
#
#  · _USE_MATH_DEFINES — MSVC's <cmath> does NOT define M_PI (it is not standard C++; libc++ and
#    libstdc++ provide it anyway). Three headers use it: HarmonicSculptor.h, ShimmerReverb.h and
#    TapeMachines.h. Set as a COMPILE DEFINITION rather than a #define in those files, so it is on
#    the command line and therefore in effect before ANY header is read — a #define inside a header
#    loses the race whenever something else includes <cmath> first.
#  · /bigobj — PluginProcessor.cpp is 884 KB and PluginEditor.cpp is 742 KB of source. MSVC's COFF
#    object format caps sections per object file and heavy inline templates blow past it on files
#    this size. Harmless where it is not needed.
#  · PUBLIC, not PRIVATE — juce_add_plugin makes this target a STATIC library (JUCEUtils.cmake:2196)
#    and the VST3 / Standalone wrappers LINK it while also compiling glue that includes our headers.
#    A PRIVATE definition would cover the library and miss the wrappers.
if (MSVC)
    target_compile_definitions(TerrainInstrument PUBLIC _USE_MATH_DEFINES)
    #  fb488 — /arch:AVX. CMake's Release default for MSVC is bare /O2, which targets the SSE2
    #  baseline: every DSP loop was auto-vectorised 128 bits at a time (or not at all) while the
    #  Mac gets clang + NEON. AVX is Sandy Bridge (2011) — older than any machine that can run a
    #  modern DAW — so this raises no practical floor. Deliberately NOT /fp:fast: it lets MSVC
    #  assume no NaN/Inf, and this codebase's std::isfinite guards are LOAD-BEARING (a NaN token
    #  printed into a viz frame kills every segment after it — the fb214 class of bug).
    #  fb499 — /arch:AVX2, measured. AVX (fb488) gave MSVC 8-wide float registers but NO FMA:
    #  every multiply-add in the DSP — which is most of it, in every filter, mixer, interpolator
    #  and waveguide — issued as two instructions. AVX2 brings FMA3, and unlike /fp:fast it does
    #  NOT permit the compiler to assume the absence of NaN/Inf, so the std::isfinite guards this
    #  codebase leans on (the fb214 class of bug) remain exactly as valid as before. The floor
    #  moves from Sandy Bridge (2011) to Haswell (2013).
    #  FMA fuses multiply-add under a SINGLE rounding, so the last bits of the output change.
    #  That is normal, inaudible, and was checked with the audio fingerprint gate rather than
    #  assumed (Tests/win_blk_cpu.cpp --modal).
    target_compile_options(TerrainInstrument PUBLIC /bigobj /utf-8 /arch:AVX)
    message(STATUS "TerrainInstrument: MSVC — _USE_MATH_DEFINES + /bigobj + /utf-8 + /arch:AVX")
endif()

# fb467 — Source/WtFft.h calls vDSP directly (the wavetable bake's transform: 19.1 ms -> 2.3 ms).
# juce_dsp already drags Accelerate in on Apple, but this is now a FIRST-PARTY dependency of a
# header the plugin includes, so it is named here rather than inherited by luck. WtFft.h compiles
# to its scalar reference path where the framework is absent, so non-Apple builds are unaffected.
if (APPLE)
    target_link_libraries(TerrainInstrument PRIVATE "-framework Accelerate")
endif()

# ── Compile definitions ──
target_compile_definitions(TerrainInstrument
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# ══ fb498 — THE WINDOWS BLOCK-SIZE / MEMORY HARNESS ═══════════════════════════════════════════
#  Tests/au_blk_cpu.cpp is AudioUnit-only, so every Windows number so far had to be relayed by
#  hand from a DAW. This target is its port (Tests/win_blk_cpu.cpp) and it is the reason anything
#  in this session can be claimed as measured rather than believed.
#
#  It HOSTS the installed .vst3 through JUCE's own VST3 host rather than linking the plugin's
#  static library. That is deliberate and load-bearing: juce_add_plugin compiles the JUCE modules
#  INTO "Terrain Instrument_SharedCode.lib", so a target that both linked that library and
#  compiled the modules for itself would collide on every JUCE symbol. Hosting the real artifact
#  also means the harness measures what actually ships, exactly as auval did on the Mac.
#
#  JUCE_WEB_BROWSER=0 here (the plugin itself needs 1): the harness never opens an editor, and
#  leaving it on would drag the WebView2 SDK into a console app for nothing.
if (WIN32)
    juce_add_console_app(terrain_winbench PRODUCT_NAME "terrain_winbench")

    target_sources(terrain_winbench PRIVATE Tests/win_blk_cpu.cpp)

    target_compile_definitions(terrain_winbench
        PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_MODAL_LOOPS_PERMITTED=1   # --modal-live pumps the message loop so the 60 Hz timer ticks
            JUCE_PLUGINHOST_VST=0
            JUCE_PLUGINHOST_AU=0
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            _USE_MATH_DEFINES
    )

    target_compile_options(terrain_winbench PRIVATE /bigobj /utf-8)

    target_link_libraries(terrain_winbench
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_audio_devices
            juce::juce_dsp
            psapi                       # GetProcessMemoryInfo — the peak-working-set read
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    message(STATUS "TerrainInstrument: + terrain_winbench (Windows block-size / memory harness)")

    #  The voice memory map. Includes SynthVoice.h and prints sizeof — it links no plugin code
    #  for the same duplicate-symbol reason as above, and needs nothing at runtime.
    juce_add_console_app(terrain_sizeof PRODUCT_NAME "terrain_sizeof")
    target_sources(terrain_sizeof PRIVATE Tests/win_sizeof.cpp)
    target_include_directories(terrain_sizeof PRIVATE Source)
    target_compile_definitions(terrain_sizeof
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            _USE_MATH_DEFINES
    )
    target_compile_options(terrain_sizeof PRIVATE /bigobj /utf-8 /arch:AVX)
    target_link_libraries(terrain_sizeof
        PRIVATE
            signalsmith-stretch
            juce::juce_audio_basics
            juce::juce_audio_processors
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
    )
endif()
