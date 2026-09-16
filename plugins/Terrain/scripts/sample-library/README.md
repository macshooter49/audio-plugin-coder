# Terrain factory one-shot library (tp12)

Builds `plugins/Terrain/Resources/Samples/<Category>/<Name>.flac` from Max's kits (not in git — ~1 GB; CMake copies it into each bundle).

1. `python3 inventory.py inventory.json` — lists every file in the source kits (paths inside; edit `SRC` for new kits).
2. `python3 build_lib.py inventory.json <out>` — excludes loops / phrases / songstarters / @trifreeze (rules.py), trims the lead-in,
   finds the key (the name's label, else pYIN, else chroma), pitch-shifts to the nearest C with `rubberband -3 -F` (length kept),
   peak-normalises to −1 dBFS, drops duplicates, names `<Category> NN`, writes 24-bit FLAC + `manifest.json` (source → name, key, shift).
   Drums / FX / Misc are not pitch-shifted unless their name carries a key.
3. `python3 verify_c.py <out>` — re-measures every shifted file: its pitch must sit on C.
4. Copy `<out>/*` into `Resources/Samples/` (Textures = the CC0 Freesound set, copied from the Noise library).

Needs: numpy, soundfile, librosa, `brew install rubberband`. Source files on an iCloud Desktop may be evicted — `brctl download` them first.
