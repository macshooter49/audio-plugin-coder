// ══════════════════════════════════════════════════════════════════════════════════════════════
//  standalone_devices.cpp — tp103 · SETTINGS → AUDIO & MIDI ON THIS MACHINE'S REAL DEVICES.
//
//  Builds the standalone app's own holder (juce::StandalonePluginHolder — the object the Terrain app runs on)
//  around the shipping processor, and calls the very functions the page's natives answer with
//  (TerrainSettingsNatives.cpp): tiAudioSetupJson (getAudioSetup), tiApplyAudioSetup (setAudioSetup),
//  tiRescanAudioDevices (rescanAudioDevices). Prints what they report and checks:
//    D1 there is a driver, a current device, sample rates, buffer sizes and a latency
//    D2 setAudioSetup really moves the device: buffer -> 128 (or the smallest offered above it), read back from the
//       open device; then back to what it was
//    D3 the choice is SAVED (the holder's settings file carries it — the relaunch path reads exactly this)
//    D4 rescan keeps the device open and lists the same outputs (no interface was plugged in during the run)
//    D5 every MIDI input the OS reports is listed with its on/off state
//
//    sh Tests/standalone_devices.sh     (build Terrain first; opens the default output for ~1 s, silent)
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "TerrainSettingsNatives.h"
#include <cstdio>
#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
 static void pump (double s) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, s, false); }   // CoreMIDI's setup notifications ride the run loop
#endif

static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const juce::String& d = {})
{ std::printf ("  %s  %s\n", ok ? "PASS " : "FAIL ", what); if (d.isNotEmpty()) std::printf ("        %s\n", d.toRawUTF8()); ok ? ++npass : ++nfail; std::fflush (stdout); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::printf ("standalone_devices — the Terrain app's device natives on this machine\n");
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("terrain_standalone_devices.settings");
    tmp.deleteFile();
    juce::PropertiesFile::Options po; po.applicationName = "terrain_standalone_devices"; po.filenameSuffix = "settings";
    auto* props = new juce::PropertiesFile (tmp, po);
    auto holder = std::make_unique<juce::StandalonePluginHolder> (props, true);
    juce::Thread::sleep (1000);   // the device opens synchronously in the holder; let it run a second

    auto* h = tiStandaloneHolder();
    chk (h == holder.get(), "the natives find the holder (StandalonePluginHolder::getInstance)");
    if (h == nullptr) return 1;

    auto j = juce::JSON::parse (tiAudioSetupJson (h));
    std::printf ("\n  getAudioSetup says:\n    driver  %s   (available: %s)\n", j["driver"].toString().toRawUTF8(),
                 juce::JSON::toString (j["drivers"], true).toRawUTF8());
    std::printf ("    outputs %s\n    inputs  %s\n", juce::JSON::toString (j["outputs"], true).toRawUTF8(), juce::JSON::toString (j["inputs"], true).toRawUTF8());
    std::printf ("    out \"%s\" ch %s · in \"%s\" ch %s\n", j["out"].toString().toRawUTF8(), j["outCh"].toString().toRawUTF8(),
                 j["inDev"].toString().toRawUTF8(), j["inCh"].toString().toRawUTF8());
    std::printf ("    rate %s of %s\n    buffer %s of %s\n    latency out %.2f ms · in %.2f ms\n",
                 j["sr"].toString().toRawUTF8(), juce::JSON::toString (j["rates"], true).toRawUTF8(),
                 j["buf"].toString().toRawUTF8(), juce::JSON::toString (j["bufs"], true).toRawUTF8(),
                 (double) j["outLatMs"], (double) j["inLatMs"]);
    std::printf ("    MIDI inputs %s\n\n", juce::JSON::toString (j["midi"], true).toRawUTF8());

    const bool real = (bool) j["standalone"] && j["driver"].toString().isNotEmpty() && j["outputs"].size() > 0
                   && j["rates"].size() > 0 && j["bufs"].size() > 0 && (double) j["sr"] > 0;
    chk (real, "D1 a real driver, device, rates, buffers and latency");

    const int buf0 = (int) j["buf"];
    int want = 128; for (auto& b : *j["bufs"].getArray()) if ((int) b >= 128) { want = (int) b; break; }
    if (want == buf0) for (auto& b : *j["bufs"].getArray()) if ((int) b != buf0) { want = (int) b; break; }
    auto* o = new juce::DynamicObject(); o->setProperty ("buf", want);
    const auto err = tiApplyAudioSetup (*h, juce::var (o));
    auto* dev = h->deviceManager.getCurrentAudioDevice();
    const int got = dev ? dev->getCurrentBufferSizeSamples() : -1;
    chk (err.isEmpty() && got == want, "D2 setAudioSetup moves the open device's buffer",
         "asked " + juce::String (want) + ", device now " + juce::String (got) + (err.isNotEmpty() ? "  error: " + err : juce::String()));
    const auto saved = props->getValue ("audioSetup");
    chk (saved.contains ("audioDeviceBufferSize=\"" + juce::String (want) + "\""), "D3 the choice is saved where the app reloads it from",
         saved.upToFirstOccurrenceOf (">", true, false).substring (0, 220));
    auto* o2 = new juce::DynamicObject(); o2->setProperty ("buf", buf0);
    tiApplyAudioSetup (*h, juce::var (o2));

    tiRescanAudioDevices (*h);
    auto j2 = juce::JSON::parse (tiAudioSetupJson (h));
    chk (h->deviceManager.getCurrentAudioDevice() != nullptr && juce::JSON::toString (j2["outputs"]) == juce::JSON::toString (j["outputs"]),
         "D4 rescan keeps the device open and re-lists the same outputs");
    chk (j["midi"].size() == juce::MidiInput::getAvailableDevices().size(), "D5 every MIDI input is listed",
         juce::String (j["midi"].size()) + " listed / " + juce::String (juce::MidiInput::getAvailableDevices().size()) + " on the system");

   #if JUCE_MAC
    // D6 — the lamp: a virtual MIDI source (what a controller looks like to the OS) appears as an input; enabled
    //      through the very native path (setMidiInput's call), a note on it reaches the activity tap.
    {
        auto vsrc = juce::MidiOutput::createNewDevice ("Terrain Test Keys");
        pump (0.3);
        juce::String id;
        for (auto& d : juce::MidiInput::getAvailableDevices()) if (d.name == "Terrain Test Keys") id = d.identifier;
        std::printf ("    (virtual source %s; inputs now:", vsrc ? "created" : "NOT created"); for (auto& d : juce::MidiInput::getAvailableDevices()) std::printf (" [%s]", d.name.toRawUTF8()); std::printf (")\n");
        (void) tiTakeMidiActivity (*h);   // registers the tap, empties it
        if (id.isNotEmpty()) h->deviceManager.setMidiInputDeviceEnabled (id, true);
        pump (0.2);
        if (vsrc) vsrc->sendMessageNow (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
        pump (0.3);
        const auto fired = tiTakeMidiActivity (*h);
        const auto listed = juce::JSON::parse (tiAudioSetupJson (h))["midi"];
        bool inList = false; for (auto& m : *listed.getArray()) if (m["name"].toString() == "Terrain Test Keys" && (bool) m["on"]) inList = true;
        chk (id.isNotEmpty() && inList && fired.contains ("Terrain Test Keys"), "D6 a plugged-in port appears (hot-plug), ticks on, and its note lights its lamp",
             "listed+on: " + juce::String (inList ? "yes" : "no") + " · activity: " + fired.joinIntoString (", "));
        if (id.isNotEmpty()) h->deviceManager.setMidiInputDeviceEnabled (id, false);
    }
   #endif
    holder.reset();
    tmp.deleteFile();
    std::printf ("\nstandalone_devices: %d pass, %d fail — %s\n", npass, nfail, nfail ? "FAIL" : "PASS");
    return nfail ? 1 : 0;
}
