# Terrain Settings: research + design notes (2026-09-23)

This note goes with `Design/settings-mockup.html`, a standalone, interactive and audible mockup of the settings surface. It records what the reference instruments put in their settings, what Terrain already has, and how the mockup groups it all. Every claim about another product cites the page it came from. Sources marked *(snippet)* were only seen as search-result text, because the page itself would not fetch.

---

## 1. What the references do

### Serum 2 (Xfer)
Source: Serum 2 User Guide v1.0.3, https://www.xferrecords.com/manual/serum-2/docs

- **Main menu:**
  - About, Read the manual, **Check for updates**.
  - Init Preset / LFOs / Mods.
  - **Save as Default Preset** (`User/default.SerumPreset`).
  - Import Preset Pack.
  - **Open Presets Folder**, **Rescan Folders on Disk**.
  - **Load/Save MIDI Map**. A map saved as `default.SerumMIDIMap` loads on every new instance.
  - Load Tuning (.tun).
  - **MPE:** enable; XYZ→Macros; MPE Bend Range 1–96, default 48. VST3 Note Expression is honoured when MPE is off.
- **Global → Preferences:**
  - Help tooltips, param-value tooltips.
  - Double-click = reset or type a value.
  - Mouse-wheel control, keyboard shortcuts on/off.
  - Default waveform view.
  - MPE on by default.
  - Silence note + FX tails when the host stops.
  - **Load MIDI Map from Presets** (off by default).
  - **Use Ultra quality when rendering**.
  - **Automatically check for updates**.
- **Quality:** Draft 1× / High 2× / Ultra 4× oversampling, with a **lock** so presets can't override it.
- **Registration** is a web account (xferrecords.com). There is no in-plugin login.

### Native Instruments
- **Kontakt Options** (https://docs.native-instruments.com/ni-tech-manuals/kontakt-manual/en/options-dialog):
  - Interface: window size reset, info-pane language.
  - Engine: CPU overload protection, **multiprocessor set separately for standalone and plug-in**, offline interpolation quality.
  - Handling: computer keyboard as MIDI, keyboard velocity, MIDI channel assignment.
  - Loading and libraries: content path, Launch Native Access.
  - Memory: preload, Memory Server.
  - Usage-data tracking.
  - **The Audio and MIDI tabs exist only in the standalone app.**
- **Massive X** settings menu *(snippet)* (https://native-instruments.com/ni-tech-manuals/massive-x-manual/en/global-controls):
  - **View Size** (8 sizes; the chosen one becomes the default).
  - **Control Sensitivity 25–250 %**.
  - **Theme** (6, including "Flat" CPU-light versions).
  - Show User Content Folder, usage data.
- **Native Access 2:** sign in with an NI account, then "Add a Serial". Activation needs the internet once; after that, products work offline (https://support.native-instruments.com/support/solutions/articles/69000879293-native-access-2-faq). Offline activation was retired in 2017 (https://support.native-instruments.com/hc/en-us/articles/115005670809-Offline-Activation-of-Native-Instruments-Products).

### Arturia (Pigments 5, Analog Lab V)
Sources: https://dl.arturia.net/products/pigments/manual/pigments_Manual_5_0_0_EN.pdf and https://downloads.arturia.net/products/analoglab-v/manual/analog-lab-v_Manual_1_0_EN.pdf

- **Menu:**
  - Save/Save As with **Author**, Bank, Type, tags, Comments.
  - **Save as Opening Preset** (plug-in only).
  - Import/Export preset, bank or playlist.
  - **Resize Window 50–200 %**.
  - Theme dark/light.
- **Audio MIDI Settings, standalone only:**
  - Driver, Device, output channels.
  - **Buffer size with its latency in ms**, Sample rate.
  - **Test Tone → Play**.
  - MIDI device checkboxes (several at once).
  - Analog Lab adds **Tempo**, used only when standalone.
  - The Analog Lab manual states it is "only available in Standalone mode"; as a plug-in, the host handles it.
- **Settings tab:**
  - Global MIDI channel (All/1–16), Accessibility, **Multicore**.
  - Per preset: play mode, master tune, microtuning (Scala/TUN, MTS-ESP).
  - **MPE:** enable, zone, channels, bend range (default 48, max 96), Slide CC 74.
- **MIDI tab:**
  - Learn mode, and an assignment list with **min/max per mapping**.
  - Controller templates, MIDI Config profiles.
  - Reserved CCs: PB, 1, 11, 64, 123, AT.
- Analog Lab also has **Fader pickup: None / Hook / Scale**.
- **Account:** My Arturia plus **Arturia Software Center** (log in, register a serial and unlock code, activate, update). Nothing happens inside the plug-in.
- No global velocity curve appears in either manual. That lives in the controller.

### Vital
Source: the open-source menus at https://github.com/mtytel/vital, plus the guide https://davidmvogel.com/docs/Vital/UserGuide/Advanced

- **Preset menu:** Browse/Save/Import/Export, Load Tuning, **Log in / Log out – \<user\>**, **Load Skin / Skin Designer**.
- **About page:** version, **Check for updates**, **UI size buttons**.
- **Advanced:**
  - Voice priority/override, **MPE Enabled**.
  - **Oversampling 1×–8×**.
  - Frequency units, Skin.
- **In-plugin email/password sign-in.** Users complain it prompts repeatedly (https://forum.vital.audio/t/why-does-vital-require-a-sign-in-on-launch/6343).
- The MPE bend is fixed at 48, and users have asked for a setting (https://forum.vital.audio/t/pitch-bend-range-in-mpe-mode/5225).

### u-he Diva
Source: https://u-he.com/downloads/manuals/plugins/diva/Diva-user-guide.pdf

- **Configuration pages:** MIDI Learn, **MIDI Table**, Preferences.
  - MIDI Table columns: channel, CC, mode (normal/fine/octave/semitone), type (7-bit, 14-bit, relative encoders).
- **Preferences:**
  - Mouse-wheel raster, **Default Size**, **Default Skin**, gamma.
  - Oscilloscope style (sets its CPU cost).
  - **Auto Versioning** of presets, **Save Presets To**, **Scan On Startup**.
  - Base latency, Control A/B default CC, MIDI control slew.
- **Main panel:** **MPE Support**, Voices, **Accuracy / OfflineAcc** (a separate bounce quality), Multicore.
- **Registration:** an in-plugin **name + serial** dialog. Some hosts block Cmd-V, so paste via right-click (https://www.kvraudio.com/forum/viewtopic.php?t=599439).

### FabFilter
- Help menu: **Enter License**, **Deauthorize**, About (https://www.fabfilter.com/help/pro-q/purchase/license, https://www.fabfilter.com/help/pro-q/support/about).
- Resize: Mini…Extra Large, per-monitor scaling. Sizes that don't fit the screen are greyed out (https://www.fabfilter.com/help/pro-q/using/fullscreenandresize).

### Phase Plant (Kilohearts)
- No global preferences page. **MIDI CC binding** and typed value entry are in each control's right-click menu (https://kilohearts.com/docs/basic_usage).
- Oversampling is automatic. MPE is supported. Bend Range is saved with the project (https://kilohearts.com/docs/phase_plant).
- **Licensing and updates live in the installer:** a personalised link authenticates the machine, and there is an Update tab with "Show preview versions" (https://kilohearts.com/docs/download_and_installation).

---

## 2. Distilled: the essentials, and who owns them

| Group | Essentials (cross-product) | Standalone only? |
|---|---|---|
| **Account** | Who it is registered to; register / sign in; sign out or deauthorise; a paste-friendly code field; default preset **author** | Both |
| **Audio & MIDI** | Driver, device, in/out channels, sample rate, buffer (with ms), **Test tone**, MIDI input checkboxes, **Tempo** | **Standalone only.** In a DAW the host owns the device, and Kontakt and Arturia hide these tabs. JUCE's standalone wrapper already has an `AudioDeviceManager` for exactly this. |
| **MIDI & Controllers** | Receive channel; MIDI learn plus a map list; default map / "presets carry their map"; pickup Jump/Hook/Scale; **MPE** plus bend range (48); pitch-bend range; A4 tuning. A velocity curve is rare globally but useful. | Both (plug-in-wide) |
| **Presets & Library** | Default author; save as default / opening preset; import pack/bank; export; library location; **rescan**; scan on start; auto-versioning | Both |
| **Interface** | Default UI size; theme/skin; hover tooltips; value tooltips; knob sensitivity; mouse wheel; double-click = reset or type | Both |
| **Performance** | Realtime oversampling plus a **separate render quality**, with a lock against presets; voice limit; CPU protection; tails on stop | Both |
| **Updates & About** | Version and build; check for updates (manual + automatic); licence status; manual; support; usage-data opt-in | Both |

**Design consequence:**
- The Audio & MIDI page opens with one plain sentence: it belongs to the Terrain app, and in a DAW the DAW owns the device.
- Everything that is plug-in-wide (channel, velocity, MPE, learn, bend) lives on its own **MIDI & Controllers** page, which is always live, in every host.

---

## 3. What Terrain already has (read from the source, not guessed)

- **The gear already opens a panel.**
  - `#settings-panel` in `Source/ui/public/index.html` 7169-7217 has: **Theme** Light/Dark, **DAW capture** On/Off (tp43), **Motion** On/Off (tp62), **Pitch bend range** (fb563, `SYN_BEND_RANGE`), and a footer that reads "Terrain v3.0 by Waves Crate".
  - Its code is in `initSettingsPanel` at 15388.
  - It is a 280 px solid card, not the house glass.
- **Global settings file:**
  - `saveSettings` / `getSettings` natives (PluginEditor.cpp 2720-2740) write `userAppData/Waves Crate/Terrain/InstrumentSettings.json`. Today that holds `theme` and `xyEnabled`.
  - Capture and Motion are marker files (processor `setCaptureEnabled` / `setMotionEnabled`).
- **Author:**
  - The Save sheet has an Author field (`openSave`, 43162).
  - Its default is `S.author0`, which starts as "Me" and is then **guessed from the author on most of your own presets** (fb626, 42541-42546).
  - There is no stored default author and no identity at all.
- **MIDI learn:**
  - `setMidiLearn / removeMidiCc / getMidiMapJson` (PluginProcessor.h 1290-1336).
  - The map is saved **per instance in the state** (`midiCcMap`).
- **Standalone:**
  - `FORMATS VST3 AU Standalone` (CMakeLists.txt 22).
  - The QWERTY-to-MIDI native `qwertyNote` exists (fb484, standalone only).
  - `masterGuard_` applies in standalone only.
- **Window size:**
  - Drag-resize from 65 % to 190 % (PluginEditor.cpp 16735).
  - A saved boot width (`bootWidth`, fb103) that new windows open at.
- **Voices and quality:**
  - `kSynthVoiceCount = 96` pool.
  - `SYN_VOICES` per patch (1–16, "display-only this phase").
  - No global oversampling setting.
- **MPE:** fully designed but not built. See `Design/mpe/00-MPE-DESIGN-2026-09-02.md` (`SYN_MPE_ON`, `supportsMPE()`, the AU path in Ableton).
- **Version:**
  - The root project is `VERSION 1.0.0`.
  - The header says **V1**, but the old settings footer says **v3.0**. That inconsistency needs fixing before the beta.

---

## 4. The mockup's structure

Categories in the rail. The chassis is the preset browser's `#tp-b` glass sheet, reused whole.

1. **Account.** Registration (name → email → authorization code), **Author name** (defaults into every save), What you do (role chips, the credit line in the browser), Sign every save, and Anonymous usage data (Soon).
2. **Audio & MIDI.** Standalone app only. Driver, output device and channels, input device and channels, sample rate, buffer + latency, **Test audio** (a real chime, L/Both/R, with a meter), MIDI inputs (Web MIDI in Chrome = your real ports), computer keyboard, Tempo + Tap.
3. **MIDI & Controllers.** Every host. Channel, **velocity curve** (the Shaper screen plus playable keys, audible), pitch-bend range (live today), A4 tuning, MPE + MPE bend 48, the MIDI learn map (Learn / Save as default / Clear), pickup Jump/Hook/Scale, presets carry their map, program change.
4. **Performance.** Playing quality Eco/Standard/High, bounce quality, "presets can change quality" lock, voice ceiling, sleep when silent, cut tails on stop, CPU meter in the header, DAW capture (live), screen refresh 60/30.
5. **Presets & Library.** Author on every save, keep old versions, what new instances open with, Save as default, Import (presets / pack / folder, which opens a real file picker), Export, library + wavetable paths with Reveal, Rescan (live), scan on start, factory library (Soon).
6. **Interface.** Theme (live), window size (live via drag; the menu is new), Motion (live), hover help, values while turning, knob drag Vertical/Circular/Horizontal, sensitivity, a **Try it** knob, mouse wheel, double-click behaviour, Open on.
7. **Themes** (Soon). Night (in use) and Daylight ship today; Basalt / Aurora / Moss / Paper are disabled. Rest on one to see its palette.
8. **Updates & About.** Version/build/formats, Check now (mock), automatic checks + channel (Soon), licence (status, computers, code signing: ad-hoc until Developer ID), Copy system info (works), About Waves Crate, credits, legal, Reset all settings.

The right column is the browser's inspector:
- By default it shows a live summary of the page: your identity and "your next save", the device, the last MIDI note and velocity, the CPU.
- Resting on any row shows what that setting does and where it applies.
- With the mockup-only **build status** switch on (under the window), each row is also tagged *live* (exists today) or *new*, and the inspector adds where the setting is stored and what C++ it needs.

---

## 5. Why registration is name → email → code (and not a web login)

- Max asked for exactly this order.
- It also matches the two patterns that work inside a DAW window:
  - u-he: a name + serial dialog in the plug-in.
  - Native Access / Arturia: an emailed code, with no password typed into a plug-in.
- Vital's in-plugin password login is the one users complain about.
- The code field formats itself (`TRRN-XXXX-XXXX`), accepts a pasted code with or without dashes, and gives specific errors (too short, already used on N computers).
- The registered **name becomes the default author** unless the user has typed their own author name.
