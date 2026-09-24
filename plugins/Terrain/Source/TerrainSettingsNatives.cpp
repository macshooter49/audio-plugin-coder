// ══ tp103 — THE SETTINGS PAGE'S APP · LIBRARY · SUPPORT NATIVES (see TerrainSettingsNatives.h) ═══════════════════
//  Every native here answers a row of Settings (index.html, #st-overlay) that used to read "Soon":
//    Audio & MIDI (standalone) — rescanAudioDevices · getMidiActivity · getOutLevel (the Test audio meter)
//    Performance               — setSleepWhenSilent · setCutTailsOnStop · setRtQuality · getEngineState
//    Presets & Library         — rescanLibrary (off the message thread) · getLibraryInfo · revealLibraryFolder ·
//                                chooseLibraryFolder · resetLibraryFolder
//    Interface                 — setBootWidth
//    Updates & About           — getBuildInfo · getSystemInfo · copySystemInfo · reportIssue (bug / crash)
#include "TerrainSettingsNatives.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetBank.h"
#include "TerrainGlobalPrefs.h"
#include "TerrainSupportMail.h"

#ifndef TERRAIN_GIT_SHA
 #define TERRAIN_GIT_SHA "dev"
#endif

namespace
{
    constexpr const char* kSupportEmail = "contact@wavescrate.com";

    // ── the version line everything quotes ────────────────────────────────────────────────────────────────────
    juce::String tiVersion()   { return juce::String (JucePlugin_VersionString) + "-beta"; }
    juce::String tiBuildId()   { return juce::String (TERRAIN_GIT_SHA) + ", " + juce::String (__DATE__); }
    juce::String tiFormatName (const juce::AudioProcessor& p)
    {
        if (p.wrapperType == juce::AudioProcessor::wrapperType_AudioUnit)  return "AU";
        if (p.wrapperType == juce::AudioProcessor::wrapperType_VST3)       return "VST3";
        if (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone) return "Standalone";
        return juce::AudioProcessor::getWrapperTypeDescription (p.wrapperType);
    }

    juce::String tiHostLine (const juce::AudioProcessor& p)
    {
        if (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone) return "Terrain app";
        juce::PluginHostType host;
        juce::String name (host.getHostDescription());
        auto exe = juce::File::getSpecialLocation (juce::File::hostApplicationPath);
        if (name.isEmpty() || name == "Unknown") name = exe.getFileNameWithoutExtension();
        juce::String ver;
       #if JUCE_MAC
        // …/Host.app/Contents/MacOS/Host → the .app, whose Info.plist carries the version
        for (auto f = exe; f.exists() && f != f.getParentDirectory(); f = f.getParentDirectory())
            if (f.getFileExtension() == ".app") { ver = f.getVersion(); break; }
       #elif JUCE_WINDOWS
        ver = exe.getVersion();
       #endif
        return ver.isNotEmpty() ? name + " " + ver : name;
    }

    // ── the crash log a "Report a crash" points at ────────────────────────────────────────────────────────────
    bool tiNamesTerrain (const juce::File& f, bool nameIsEnough)
    {
        const auto n = f.getFileName();
        if (nameIsEnough && n.startsWithIgnoreCase ("Terrain")) return true;
        if (f.getSize() > 8 * 1024 * 1024) return false;   // a report that big is not a crash log worth reading
        const auto t = f.loadFileAsString();
        return t.contains ("com.wavescrate.terrain") || t.contains ("Terrain.component") || t.contains ("Terrain.vst3");
    }
}

juce::File tiFindRecentCrashLog()
{
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (7);
    juce::Array<juce::File> cands;
   #if JUCE_MAC
    const auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/DiagnosticReports");
    if (dir.isDirectory())
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.ips;*.crash"))
            if (f.getLastModificationTime() >= cutoff) cands.add (f);
   #elif JUCE_WINDOWS
    const auto local = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
    for (const auto& sub : { "Microsoft/Windows/WER/ReportArchive", "Microsoft/Windows/WER/ReportQueue" })
    {
        const auto dir = local.getChildFile (sub);
        if (dir.isDirectory())
            for (const auto& rep : dir.findChildFiles (juce::File::findFiles, true, "Report.wer"))
                if (rep.getLastModificationTime() >= cutoff) cands.add (rep);
    }
    const auto dumps = local.getChildFile ("CrashDumps");
    if (dumps.isDirectory())
        for (const auto& f : dumps.findChildFiles (juce::File::findFiles, false, "Terrain*.dmp"))
            if (f.getLastModificationTime() >= cutoff) cands.add (f);
   #endif
    std::sort (cands.begin(), cands.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    int read = 0;
    for (const auto& f : cands)
    {
        if (++read > 60) break;
        if (tiNamesTerrain (f, true)) return f;
    }
    return {};
}

juce::String tiSystemInfoText (TerrainAudioProcessor& p, const juce::var& extra)
{
    const double sr = p.getSampleRate();
    const int    bs = p.getBlockSize();
    const auto   cpu = juce::SystemStats::getCpuModel().trim();
    const int    ram = juce::SystemStats::getMemorySizeInMegabytes();
    auto preset = p.getPresetMeta().name.trim();
    if (preset.isEmpty()) preset = "(unsaved sound)";
    juce::String lic = extra.isObject() ? extra["license"].toString() : juce::String();
    if (lic.isEmpty()) lic = "unknown";
    juce::StringArray L;
    L.add ("Terrain " + tiVersion() + " (build " + tiBuildId() + ")");
    L.add ("Format: " + tiFormatName (p) + " - Host: " + tiHostLine (p));
    L.add ("OS: " + juce::SystemStats::getOperatingSystemName());
    L.add ("CPU: " + (cpu.isNotEmpty() ? cpu : juce::String ("unknown")) + ", " + juce::String (juce::SystemStats::getNumCpus()) + " cores"
           + " - RAM: " + juce::String (juce::roundToInt (ram / 1024.0)) + " GB");
    L.add ("Audio: " + (sr > 0.0 ? juce::String (sr / 1000.0, 1) + " kHz" : juce::String ("not running")) + " - buffer " + juce::String (bs) + " samples");
    L.add ("Preset: " + preset);
    L.add ("DSP load: " + juce::String (juce::roundToInt (p.dspLoadLast_)) + " %");
    L.add ("Licence: " + lic);
    return L.joinIntoString ("\n");
}

// ═══ tp100 → tp103 — THE STANDALONE DEVICE (moved verbatim from PluginEditor.cpp, plus the rescan) ═══════════════
#if JucePlugin_Build_Standalone
namespace
{
    // "3 + 4" -> {2,3}; "2" -> {1}; "Off"/"" -> {}
    juce::Array<int> tiParseChannels (const juce::String& label)
    {
        juce::Array<int> out;
        for (auto& tok : juce::StringArray::fromTokens (label, "+", ""))
        {
            const int c = tok.trim().getIntValue();
            if (c >= 1) out.add (c - 1);
        }
        return out;
    }
    juce::String tiChannelsLabel (const juce::BigInteger& bits)
    {
        juce::StringArray parts;
        for (int i = bits.findNextSetBit (0); i >= 0; i = bits.findNextSetBit (i + 1)) parts.add (juce::String (i + 1));
        return parts.isEmpty() ? juce::String ("Off") : parts.joinIntoString (" + ");
    }

    // tp103 — MIDI ACTIVITY. One callback on the holder's device manager hears every ENABLED input (the empty
    //  identifier = all of them) and notes which port spoke; getMidiActivity hands the names to the page, which
    //  flashes that row's lamp. The MIDI thread only appends a name under a spin lock; never the audio thread.
    struct TiMidiTap final : juce::MidiInputCallback
    {
        juce::SpinLock lock; juce::StringArray fired;
        void handleIncomingMidiMessage (juce::MidiInput* src, const juce::MidiMessage&) override
        {
            if (src == nullptr) return;
            const auto n = src->getName();
            const juce::SpinLock::ScopedLockType l (lock);
            if (! fired.contains (n) && fired.size() < 64) fired.add (n);
        }
    };
    TiMidiTap& tiMidiTap (juce::StandalonePluginHolder& h)
    {
        static TiMidiTap* tap = new TiMidiTap();   // deliberately immortal: it must outlive the holder's MIDI thread
        static juce::StandalonePluginHolder* on = nullptr;
        if (on != &h) { h.deviceManager.addMidiInputDeviceCallback ({}, tap); on = &h; }
        return *tap;
    }
}

juce::StringArray tiTakeMidiActivity (juce::StandalonePluginHolder& h)
{
    auto& tap = tiMidiTap (h);
    juce::StringArray got;
    { const juce::SpinLock::ScopedLockType l (tap.lock); got.swapWith (tap.fired); }
    return got;
}

juce::String tiAudioSetupJson (juce::StandalonePluginHolder* h, const juce::String& error)
{
    auto* o = new juce::DynamicObject();
    juce::var root (o);
    o->setProperty ("standalone", h != nullptr);
    if (h == nullptr) return juce::JSON::toString (root, true);
    auto& dm = h->deviceManager;
    if (error.isNotEmpty()) o->setProperty ("error", error);

    juce::Array<juce::var> drivers;
    for (auto* t : dm.getAvailableDeviceTypes()) drivers.add (t->getTypeName());
    o->setProperty ("drivers", drivers);
    o->setProperty ("driver", dm.getCurrentAudioDeviceType());

    const auto setup = dm.getAudioDeviceSetup();
    juce::Array<juce::var> outs, ins;
    ins.add ("None");
    if (auto* type = dm.getCurrentDeviceTypeObject())
    {
        for (auto& nm : type->getDeviceNames (false)) outs.add (nm);
        for (auto& nm : type->getDeviceNames (true))  ins.add (nm);
    }
    o->setProperty ("outputs", outs);
    o->setProperty ("inputs",  ins);
    o->setProperty ("out",   setup.outputDeviceName);
    o->setProperty ("inDev", setup.inputDeviceName.isEmpty() ? juce::String ("None") : setup.inputDeviceName);

    if (auto* dev = dm.getCurrentAudioDevice())
    {
        const double sr = dev->getCurrentSampleRate();
        juce::Array<juce::var> rates, bufs, outChs, inChs;
        for (auto r : dev->getAvailableSampleRates()) rates.add (r);
        for (auto b : dev->getAvailableBufferSizes()) bufs.add (b);
        const int nOut = dev->getOutputChannelNames().size(), nIn = dev->getInputChannelNames().size();
        for (int i = 0; i + 1 < nOut; i += 2) outChs.add (juce::String (i + 1) + " + " + juce::String (i + 2));
        if (nOut == 1) outChs.add ("1");
        for (int i = 0; i < nIn; ++i) inChs.add (juce::String (i + 1));
        for (int i = 0; i + 1 < nIn; i += 2) inChs.add (juce::String (i + 1) + " + " + juce::String (i + 2));
        o->setProperty ("rates", rates);
        o->setProperty ("bufs",  bufs);
        o->setProperty ("outChs", outChs);
        o->setProperty ("inChs",  inChs);
        o->setProperty ("sr",  sr);
        o->setProperty ("buf", dev->getCurrentBufferSizeSamples());
        o->setProperty ("outCh", tiChannelsLabel (dev->getActiveOutputChannels()));
        o->setProperty ("inCh",  setup.inputDeviceName.isEmpty() ? juce::String ("Off") : tiChannelsLabel (dev->getActiveInputChannels()));
        const double s = juce::jmax (1.0, sr);
        o->setProperty ("outLatMs", 1000.0 * dev->getOutputLatencyInSamples() / s);
        o->setProperty ("inLatMs",  1000.0 * dev->getInputLatencyInSamples()  / s);
    }
    else o->setProperty ("noDevice", true);

    juce::Array<juce::var> midi;
    for (auto& d : juce::MidiInput::getAvailableDevices())
    {
        auto* m = new juce::DynamicObject();
        m->setProperty ("name", d.name);
        m->setProperty ("id",   d.identifier);
        m->setProperty ("on",   dm.isMidiInputDeviceEnabled (d.identifier));
        midi.add (juce::var (m));
    }
    o->setProperty ("midi", midi);
    return juce::JSON::toString (root, true);
}

// Applies the fields present in `j` (driver / out / inDev / outCh / inCh / sr / buf); returns an error
// string ("" = fine). The holder's device state is saved so the choice survives a relaunch.
juce::String tiApplyAudioSetup (juce::StandalonePluginHolder& h, const juce::var& j)
{
    auto& dm = h.deviceManager;
    if (j.hasProperty ("driver"))
    {
        const auto want = j["driver"].toString();
        if (want.isNotEmpty() && want != dm.getCurrentAudioDeviceType())
            dm.setCurrentAudioDeviceType (want, true);
    }
    auto setup = dm.getAudioDeviceSetup();
    if (j.hasProperty ("out"))   setup.outputDeviceName = j["out"].toString();
    if (j.hasProperty ("inDev"))
    {
        const auto in = j["inDev"].toString();
        setup.inputDeviceName = (in == "None") ? juce::String() : in;
        if (in == "None") { setup.inputChannels.clear(); setup.useDefaultInputChannels = false; }
        else if (setup.inputChannels.isZero()) { setup.inputChannels.setRange (0, 2, true); setup.useDefaultInputChannels = false; }
    }
    if (j.hasProperty ("sr"))    { const double r = (double) j["sr"];  if (r > 0.0) setup.sampleRate = r; }
    if (j.hasProperty ("buf"))   { const int    b = (int)    j["buf"]; if (b > 0)   setup.bufferSize = b; }
    if (j.hasProperty ("outCh"))
    {
        setup.outputChannels.clear();
        for (int c : tiParseChannels (j["outCh"].toString())) setup.outputChannels.setBit (c);
        if (setup.outputChannels.countNumberOfSetBits() == 1) setup.outputChannels.setBit (setup.outputChannels.findNextSetBit (0) + 1);
        setup.useDefaultOutputChannels = setup.outputChannels.isZero();
    }
    if (j.hasProperty ("inCh"))
    {
        setup.inputChannels.clear();
        for (int c : tiParseChannels (j["inCh"].toString())) setup.inputChannels.setBit (c);
        setup.useDefaultInputChannels = false;
    }
    const auto err = dm.setAudioDeviceSetup (setup, true);
    h.saveAudioDeviceState();
    return err;
}

void tiRescanAudioDevices (juce::StandalonePluginHolder& h)
{
    auto& dm = h.deviceManager;
    for (auto* t : dm.getAvailableDeviceTypes()) t->scanForDevices();
    // a device that vanished leaves the manager closed: reopen on whatever the current type now offers
    if (dm.getCurrentAudioDevice() == nullptr)
    {
        auto setup = dm.getAudioDeviceSetup();
        if (auto* type = dm.getCurrentDeviceTypeObject())
        {
            const auto outs = type->getDeviceNames (false);
            if (! outs.contains (setup.outputDeviceName)) setup.outputDeviceName = outs[type->getDefaultDeviceIndex (false)];
            const auto ins = type->getDeviceNames (true);
            if (setup.inputDeviceName.isNotEmpty() && ! ins.contains (setup.inputDeviceName)) setup.inputDeviceName = {};
        }
        dm.setAudioDeviceSetup (setup, true);
    }
}
#endif

// ═══ THE LIBRARY ═════════════════════════════════════════════════════════════════════════════════════════════════
namespace
{
    // PluginEditor.cpp::sampleFactoryRoot()'s up-walk, plus the Settings override (a moved factory library)
    juce::File tiFactorySamplesRoot()
    {
        if (const auto o = tw::prefs::libraryOverride (tw::prefs::Lib::factory); o.getChildFile ("Samples").isDirectory())
            return o.getChildFile ("Samples");
        auto p = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        for (int up = 0; up < 5 && p.exists(); ++up)
        {
            const auto a = p.getChildFile ("Resources").getChildFile ("Samples");
            if (a.isDirectory()) return a;
            const auto b = p.getChildFile ("Contents").getChildFile ("Resources").getChildFile ("Samples");
            if (b.isDirectory()) return b;
            p = p.getParentDirectory();
        }
        return {};
    }
    /** Where the factory content lives: the folder holding Banks/ and Samples/ (inside the bundle unless moved). */
    juce::File tiFactoryRoot()
    {
        const auto o = tw::prefs::libraryOverride (tw::prefs::Lib::factory);
        if (o.isDirectory()) return o;
        const auto b = TerrainAudioProcessor::banksFactoryRoot();
        if (b.isDirectory()) return b.getParentDirectory();
        const auto s = tiFactorySamplesRoot();
        return s.isDirectory() ? s.getParentDirectory() : juce::File();
    }
    juce::File tiLibFolder (tw::prefs::Lib l)
    {
        switch (l)
        {
            case tw::prefs::Lib::presets: return TerrainAudioProcessor::banksUserRoot();
            case tw::prefs::Lib::samples: return TerrainAudioProcessor::userSamplesRoot();
            case tw::prefs::Lib::factory: return tiFactoryRoot();
        }
        return {};
    }
    bool tiParseLib (const juce::var& v, tw::prefs::Lib& out)
    {
        const auto s = v.toString();
        if (s == "presets") { out = tw::prefs::Lib::presets; return true; }
        if (s == "samples") { out = tw::prefs::Lib::samples; return true; }
        if (s == "factory") { out = tw::prefs::Lib::factory; return true; }
        return false;
    }
    int tiCountAudio (const juce::File& root)
    {
        if (! root.isDirectory()) return 0;
        int n = 0;
        for (const auto& e : juce::RangedDirectoryIterator (root, true, "*.flac;*.wav;*.aif;*.aiff;*.ogg;*.mp3",
                                                            juce::File::findFiles))
        { juce::ignoreUnused (e); if (++n >= 200000) break; }
        return n;
    }
    juce::var tiLibraryInfo()
    {
        auto* o = new juce::DynamicObject(); juce::var root (o);
        o->setProperty ("scanOnStart", tw::prefs::readEnginePrefs().scanOnStart);
        for (auto l : { tw::prefs::Lib::presets, tw::prefs::Lib::samples, tw::prefs::Lib::factory })
        {
            auto* e = new juce::DynamicObject();
            const auto chosen = tw::prefs::libraryOverride (l);
            const auto live   = tiLibFolder (l);
            e->setProperty ("path",    live.getFullPathName());
            e->setProperty ("exists",  live.isDirectory());
            e->setProperty ("custom",  chosen != juce::File() && chosen.isDirectory());
            e->setProperty ("missing", chosen != juce::File() && ! chosen.isDirectory() ? chosen.getFullPathName() : juce::String());
            o->setProperty (tw::prefs::libKey (l), juce::var (e));
        }
        return root;
    }
}

// ═══ THE NATIVES ═════════════════════════════════════════════════════════════════════════════════════════════════
juce::WebBrowserComponent::Options TiSettingsNatives::add (TerrainUiCore& core, juce::WebBrowserComponent::Options o)
{
    using Args = const juce::Array<juce::var>&;
    using Done = juce::WebBrowserComponent::NativeFunctionCompletion;
    TerrainAudioProcessor& proc = core.audioProcessor;
    juce::Component::SafePointer<TerrainUiCore> safe (&core);
    auto toJson = [] (const juce::var& v) { return juce::var (juce::JSON::toString (v, true)); };

    return std::move (o)
    // ── Updates & About ────────────────────────────────────────────────────────────────────────────────────────
    .withNativeFunction ("getBuildInfo", [&proc, toJson] (Args, Done done)
    {
        auto* b = new juce::DynamicObject();
        b->setProperty ("ver",   tiVersion());
        b->setProperty ("build", juce::String (TERRAIN_GIT_SHA) + " · " + juce::String (__DATE__));
        b->setProperty ("wrap",  tiFormatName (proc) == "Standalone" ? juce::String ("Standalone app") : tiFormatName (proc) + " in " + tiHostLine (proc));
        done (toJson (juce::var (b)));
    })
    .withNativeFunction ("getSystemInfo", [&proc] (Args a, Done done)
    {
        done (juce::var (tiSystemInfoText (proc, a.size() > 0 ? juce::JSON::parse (a[0].toString()) : juce::var())));
    })
    .withNativeFunction ("copySystemInfo", [&proc] (Args a, Done done)
    {
        const auto t = tiSystemInfoText (proc, a.size() > 0 ? juce::JSON::parse (a[0].toString()) : juce::var());
        juce::SystemClipboard::copyTextToClipboard (t);
        done (juce::var (t));
    })
    // args[0] = {kind:"bug"|"crash", license:"…"}. Opens the user's mail app on a written message; answers
    // {ok, copied, truncated, crashLog, url}. ok=false ⇒ no mail app took it: the whole report is on the clipboard.
    .withNativeFunction ("reportIssue", [&proc, toJson] (Args a, Done done)
    {
        const auto j = a.size() > 0 ? juce::JSON::parse (a[0].toString()) : juce::var();
        const bool crash = j.isObject() && j["kind"].toString() == "crash";
        const auto now = juce::Time::getCurrentTime();
        const auto subject = juce::String::fromUTF8 (tw::support::reportSubject (tiVersion().toStdString(), TERRAIN_GIT_SHA, crash,
                                                                                now.formatted ("%Y-%m-%d %H:%M").toStdString()).c_str());
        juce::String crashLine; juce::File log;
        if (crash)
        {
            log = tiFindRecentCrashLog();
           #if JUCE_MAC
            const juce::String where = "Finder";
           #else
            const juce::String where = "Explorer";
           #endif
            if (log.existsAsFile()) { log.revealToUser(); crashLine = "Crash log: " + log.getFileName() + " - please attach the file that just opened in " + where + "."; }
            else crashLine = "Crash log: none found from the last 7 days.";
        }
        const auto body = juce::String::fromUTF8 (tw::support::reportBody (crash, crashLine.toStdString(),
                                                                           tiSystemInfoText (proc, j).toStdString()).c_str());
        const auto m = tw::support::buildMailto (kSupportEmail, subject.toStdString(), body.toStdString(), 1800);
        const bool ok = juce::Process::openDocument (juce::String (m.url), {});
        bool copied = false;
        if (! ok || m.truncated)
        {
            juce::SystemClipboard::copyTextToClipboard ("To: " + juce::String (kSupportEmail) + "\nSubject: " + subject + "\n\n" + body);
            copied = true;
        }
        auto* r = new juce::DynamicObject();
        r->setProperty ("ok", ok); r->setProperty ("copied", copied); r->setProperty ("truncated", m.truncated);
        r->setProperty ("crashLog", log.getFullPathName()); r->setProperty ("url", juce::String (m.url));
        done (toJson (juce::var (r)));
    })
    // ── Performance ────────────────────────────────────────────────────────────────────────────────────────────
    .withNativeFunction ("setSleepWhenSilent", [&proc] (Args a, Done done)
    { if (a.size() > 0) proc.setSleepWhenSilent ((int) a[0] != 0); done (juce::var (proc.getSleepWhenSilent())); })
    .withNativeFunction ("setCutTailsOnStop", [&proc] (Args a, Done done)
    { if (a.size() > 0) proc.setCutTailsOnStop ((int) a[0] != 0); done (juce::var (proc.getCutTailsOnStop())); })
    // args[0] = {rt:"Eco|Standard|High", off:"Same|High|Best", presets:"On|Off"} (or a bare playing-quality string)
    .withNativeFunction ("setRtQuality", [&proc] (Args a, Done done)
    {
        if (a.size() > 0)
        {
            const auto j = juce::JSON::parse (a[0].toString());
            if (j.isObject())
                proc.setQualityPrefs (tw::prefs::rtQualityIndex (j["rt"].toString()),
                                      tw::prefs::offQualityIndex (j["off"].toString()),
                                      j["presets"].toString() != "Off");
            else
                proc.setQualityPrefs (tw::prefs::rtQualityIndex (a[0].toString()), proc.getOffQuality(), proc.getPresetsMayQuality());
        }
        done (juce::var (proc.filterOsPolicyNow()));
    })
    .withNativeFunction ("getEngineState", [&proc, toJson] (Args, Done done)
    {
        auto* r = new juce::DynamicObject();
        r->setProperty ("sleeping", proc.isSleeping());
        r->setProperty ("sleep", proc.getSleepWhenSilent());
        r->setProperty ("tails", proc.getCutTailsOnStop());
        r->setProperty ("filterOs", proc.filterOsPolicyNow());
        done (toJson (juce::var (r)));
    })
    .withNativeFunction ("getOutLevel", [&proc] (Args, Done done) { done (juce::var ((double) proc.takeOutputPeak())); })
    // ── Interface → Window size: args[0] = the width in px a new window opens at (and this one, now) ─────────────
    .withNativeFunction ("setBootWidth", [safe] (Args a, Done done)
    {
        if (safe != nullptr && a.size() > 0)
        {
            const int w = juce::jlimit (juce::roundToInt (820 * 0.65), juce::roundToInt (820 * 1.90), (int) a[0]);
            if (auto* shell = safe->currentShell()) shell->applyChosenWidth (w);
            else safe->audioProcessor.editorWidth.store (w);
        }
        done (juce::var{});
    })
    // ── Audio & MIDI (standalone) ──────────────────────────────────────────────────────────────────────────────
    .withNativeFunction ("rescanAudioDevices", [] (Args, Done done)
    {
       #if JucePlugin_Build_Standalone
        if (auto* h = tiStandaloneHolder()) { tiRescanAudioDevices (*h); done (juce::var (tiAudioSetupJson (h))); return; }
       #endif
        done (juce::var ("{\"standalone\":false}"));
    })
    .withNativeFunction ("getMidiActivity", [] (Args, Done done)
    {
        juce::Array<juce::var> names;
       #if JucePlugin_Build_Standalone
        if (auto* h = tiStandaloneHolder())
        {
            for (auto& n : tiTakeMidiActivity (*h)) names.add (n);
        }
       #endif
        done (juce::var (juce::JSON::toString (juce::var (names), true)));
    })
    // ── Presets & Library ──────────────────────────────────────────────────────────────────────────────────────
    .withNativeFunction ("getLibraryInfo", [toJson] (Args, Done done) { done (toJson (tiLibraryInfo())); })
    // Re-reads every pack and sample folder from disk OFF the message thread (the tp35 law: never block the host's
    // thread on a walk), then answers {presets, packs, unreadable, factorySamples, userSamples, ms}.
    .withNativeFunction ("rescanLibrary", [safe] (Args, Done done)
    {
        auto cb = std::make_shared<Done> (std::move (done));
        juce::Thread::launch ([safe, cb]
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            tw::bank::ScanStats st; tw::bank::Caps caps;
            (void) tw::bank::scan (TerrainAudioProcessor::banksFactoryRoot(), TerrainAudioProcessor::banksUserRoot(), caps, st);
            const int fs = tiCountAudio (tiFactorySamplesRoot());
            const int us = tiCountAudio (TerrainAudioProcessor::userSamplesRoot());
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            auto* r = new juce::DynamicObject();
            r->setProperty ("presets", st.presets); r->setProperty ("packs", st.banks); r->setProperty ("unreadable", st.unreadable);
            r->setProperty ("factorySamples", fs); r->setProperty ("userSamples", us); r->setProperty ("ms", ms);
            const auto json = juce::JSON::toString (juce::var (r), true);
            juce::MessageManager::callAsync ([safe, cb, json] { if (safe != nullptr) (*cb) (juce::var (json)); });
        });
    })
    .withNativeFunction ("revealLibraryFolder", [] (Args a, Done done)
    {
        tw::prefs::Lib l = tw::prefs::Lib::presets;
        if (a.size() > 0 && tiParseLib (a[0], l))
        {
            auto dir = tiLibFolder (l);
            if (dir != juce::File() && ! dir.exists() && l != tw::prefs::Lib::factory) dir.createDirectory();
            if (dir.exists()) { dir.revealToUser(); done (juce::var (dir.getFullPathName())); return; }
        }
        done (juce::var (juce::String()));
    })
    // Opens a folder chooser; completes once the user has chosen (or cancelled) with {ok, path, error}.
    .withNativeFunction ("chooseLibraryFolder", [safe, toJson] (Args a, Done done)
    {
        tw::prefs::Lib l = tw::prefs::Lib::presets;
        if (a.size() == 0 || ! tiParseLib (a[0], l)) { done (juce::var ("{\"ok\":false,\"error\":\"bad folder kind\"}")); return; }
        const auto title = l == tw::prefs::Lib::presets ? juce::String ("Choose where Terrain keeps your presets")
                         : l == tw::prefs::Lib::samples ? juce::String ("Choose where Terrain keeps your samples")
                                                        : juce::String ("Choose the folder holding Terrain's factory library (Banks / Samples)");
        auto chooser = std::make_shared<juce::FileChooser> (title, tiLibFolder (l));
        auto cb = std::make_shared<Done> (std::move (done));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe, chooser, cb, l, toJson] (const juce::FileChooser& fc)
        {
            auto* r = new juce::DynamicObject(); juce::var rv (r);
            const auto dir = fc.getResult();
            if (dir == juce::File()) r->setProperty ("ok", false);   // cancelled
            else if (l == tw::prefs::Lib::factory && ! dir.getChildFile ("Banks").isDirectory() && ! dir.getChildFile ("Samples").isDirectory())
            { r->setProperty ("ok", false); r->setProperty ("error", "That folder has no Banks or Samples folder in it - it is not a Terrain factory library."); }
            else if (! tw::prefs::setLibraryOverride (l, dir))
            { r->setProperty ("ok", false); r->setProperty ("error", "Terrain could not write its settings file."); }
            else { r->setProperty ("ok", true); r->setProperty ("path", dir.getFullPathName()); }
            if (safe != nullptr) (*cb) (toJson (rv));
        });
    })
    .withNativeFunction ("resetLibraryFolder", [toJson] (Args a, Done done)
    {
        tw::prefs::Lib l = tw::prefs::Lib::presets;
        if (a.size() > 0 && tiParseLib (a[0], l)) tw::prefs::setLibraryOverride (l, {});
        done (toJson (tiLibraryInfo()));
    });
}

// ── Interface → Window size, on the shell (the window owner) ──────────────────────────────────────────────────────
//  The chosen width becomes THIS window's intended size at once and this instance's remembered one; new instances
//  read the global choice in TerrainUiCore::bootWidth(). Same bookkeeping as a real drag (resized()), so the size
//  heal defends it instead of undoing it.
void TerrainAudioProcessorEditor::applyChosenWidth (int w)
{
    userSized_ = true;
    intendedW_ = w;
    audioProcessor.editorWidth.store (w);
    setSize (w, juce::roundToInt (w * 672.0 / 820.0));
}
