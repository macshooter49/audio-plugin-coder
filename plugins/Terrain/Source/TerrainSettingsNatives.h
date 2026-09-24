#pragma once
// ══ tp103 — THE SETTINGS PAGE'S APP · LIBRARY · SUPPORT NATIVES ═════════════════════════════════════════════════
//  One seam into the editor's native table: TiSettingsNatives::add() wraps the Options the editor starts its chain
//  with, so every native the Settings page's app / library / support rows need lives in TerrainSettingsNatives.cpp
//  and PluginEditor.cpp carries one call. The standalone device helpers (tp100) moved here too, so a harness can
//  call the very function getAudioSetup answers with (Tests/standalone_devices.cpp).
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>

class TerrainUiCore;
class TerrainAudioProcessor;

struct TiSettingsNatives
{
    static juce::WebBrowserComponent::Options add (TerrainUiCore& core, juce::WebBrowserComponent::Options o);
};

// ── tp100 — Settings → Audio & MIDI (the STANDALONE app's device manager) ──
// The standalone wrapper owns the AudioDeviceManager (juce::StandalonePluginHolder). Its header is all in-class /
// inline (currentInstance is a C++17 inline static), so including it is ODR-safe; in the AU / VST3 binaries nothing
// ever creates a holder, getInstance() is null and the natives answer {"standalone":false} — the page then shows
// those rows as "App only".
#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
 inline juce::StandalonePluginHolder* tiStandaloneHolder() { return juce::StandalonePluginHolder::getInstance(); }
 /** The device as it stands: drivers, outputs/inputs, channels, rates, buffers, latency, every MIDI input. */
 juce::String tiAudioSetupJson (juce::StandalonePluginHolder* h, const juce::String& error = {});
 /** Applies the fields present in `j` (driver / out / inDev / outCh / inCh / sr / buf); "" = fine. Saved. */
 juce::String tiApplyAudioSetup (juce::StandalonePluginHolder& h, const juce::var& j);
 /** Re-asks every driver type for its devices (a new interface appears); the current device is kept. */
 void tiRescanAudioDevices (juce::StandalonePluginHolder& h);
 /** The MIDI inputs that sent anything since the last call (the Settings lamps). Registers the listener on first use. */
 juce::StringArray tiTakeMidiActivity (juce::StandalonePluginHolder& h);
#else
 namespace juce { class StandalonePluginHolder; }
 inline juce::StandalonePluginHolder* tiStandaloneHolder() { return nullptr; }
#endif

/** The system block every support path shares (Copy system info · Report a bug · Report a crash). `extra` is the
    page's JSON ({license:"…"}). Message thread. */
juce::String tiSystemInfoText (TerrainAudioProcessor& p, const juce::var& extra);
/** macOS: the newest DiagnosticReports file from the last 7 days that names Terrain (ours, or a host that died in
    us). Windows: the newest WER dump / report that names Terrain. Empty when there is none. */
juce::File tiFindRecentCrashLog();
