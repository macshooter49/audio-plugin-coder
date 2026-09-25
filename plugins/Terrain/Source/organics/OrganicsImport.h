// OrganicsImport.h — tp108: the USER SOUNDFONT IMPORT (contract tp108 amendment). SFZ (+ its samples), SF2 and SF3
// convert IN THE PLUGIN into a .torg v1 instrument under <library root>/User/<id>/ — no Python, no network.
//
// It is a C++ port of the parts of the offline compiler (Tools/organics/torgc.py · sfz.py · sf2.py · analyse.py) a user
// file needs: SFZ flattening (#define / #include / default_path / note names / <global> <master> <group> <region>
// <curve> / CC defaults), the opcode subset (keys, velocities + xfin/xfout, tune/transpose/pitch_keycenter,
// volume/amplitude/pan, offset/end, loops, seq/rand round robin, trigger=release + rt_decay, ampeg, group/off_by,
// amp_velcurve/amp_veltrack, pitch_keytrack expansion), keyswitches (sw_last/sw_label) → articulations, the SF2 hydra
// (presets → instrument zones, global zones, generator addition + range intersection, sample headers, stereo links,
// loops, sm24) and SF3 (Ogg Vorbis samples through JUCE's reader). Per sample: DC guard, −60 dB end trim, onset, peak
// normalisation, 16-bit FLAC; per note: the 50 % RMS gainNorm and the velocity power fit; per region: tfix (YIN +
// the fundamental partial); per instrument: the loudness calibration (−24 LUFS K-weighted at the centre key) and a
// preview.flac. Skipped on purpose: the tail-loop search and the loop polish (a looping region keeps its AUTHORED
// loop and crossfade; a decaying one has no tail loop, so Sustain does nothing on it).
//
// Laws: never on the audio or message thread (startImport → a background job with progress); a bad file reports WHY
// and leaves nothing behind (built in User/.importing-*, renamed into place, index written by replace-in-place);
// factory files are never written; user ids are "user.<slug>", numbered from 2048 in User/user-ids.json (append-only).
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

namespace tw
{
namespace orgimport
{
    constexpr int    kFirstPreset  = -1;      // SF2/SF3: the first preset (the only one for most files)
    constexpr int    kAllPresets   = -2;      // SF2/SF3: one instrument per preset ("Import all presets")
    constexpr double kLargeMB      = 500.0;   // decoded audio above this asks the user first (Request::allowLarge)
    constexpr int    kFirstUserNum = 2048;    // user ids.json numbers start here (factory ids stay < 2048)

    struct Request
    {
        juce::File   source;                  // .sfz / .sf2 / .sf3
        int          preset     = kFirstPreset;   // SF2: phdr index (0-based), kFirstPreset or kAllPresets
        bool         allowLarge = false;      // the user confirmed an import above kLargeMB
        juce::File   root;                    // the library root (empty = OrganicsLibrary::get().root())
        juce::String name;                    // optional display name (default: the file / preset name)
    };

    struct Result
    {
        bool   ok = false;
        juce::StringArray ids, names;         // the instrument(s) written (one per preset for kAllPresets)
        juce::String error;                   // why nothing was imported (empty when ok)
        juce::String warning;                 // imported, but something was skipped (missing samples, empty presets …)
        bool   needConfirm = false;           // !ok because the audio is over kLargeMB and allowLarge was false
        double mb = 0.0;                      // decoded audio the import holds in RAM (int16, all channels), MB
    };

    /** pct 0..100 and a short stage word ("Reading", "Samples", "Writing" …). Called on the importing thread. */
    using ProgressFn = std::function<void (float pct, const juce::String& stage)>;

    /** Synchronous import (runs on the calling thread — the job thread, or a test). Thread-safe against other imports
        and deletions (a process lock + an inter-process lock guard the User/ index files). */
    Result importSoundFont (const Request& req, ProgressFn progress = {}, const std::atomic<bool>* cancel = nullptr);

    /** The preset names of an SF2/SF3 (phdr order, the EOP terminator excluded). Empty + *error on a bad file. */
    juce::StringArray listSf2Presets (const juce::File& f, juce::String* error = nullptr);

    /** Remove a user instrument: its entry in User/user-index.json and its folder. Its number stays in user-ids.json
        (append-only: never reused by another id). Factory ids are refused. */
    bool deleteUserInstrument (const juce::File& root, const juce::String& id, juce::String* error = nullptr);

    bool isSoundFontFile (const juce::File& f);        // .sfz / .sf2 / .sf3 (case-insensitive)

    //==============================================================================================================
    //  Background jobs (message-thread API). One worker thread runs the queue; events hop to the message thread.
    //==============================================================================================================
    struct JobEvent
    {
        int    job = 0;
        float  pct = 0.0f;
        bool   done = false;
        juce::String stage;
        Result result;                        // meaningful when done
    };

    /** Queue an import. Returns the job id (> 0). onEvent runs on the MESSAGE thread: progress (≤ 10 per second), then
        exactly one event with done = true. When the import wrote something the library is rescanned before that event. */
    int  startImport (const Request& req, std::function<void (const JobEvent&)> onEvent);

    /** Cancel every queued/running job (their done event reports "Cancelled"). */
    void cancelAll();

    /** Test hook: true while any job is queued or running. */
    bool busy();
}
}
