#!/usr/bin/env python3
"""noisemaps.py — writes the "noiseMap" of every recipe from ONE family table (so the per-family levels stay
consistent and reviewable in one place). Re-run after editing the table:

    python3 noisemaps.py            # rewrites recipes/*.json "noiseMap" (only for ids listed below)

relDb = level of the noise's loudest 100 ms (K-weighted) relative to the instrument's calibrated note loudness at
velocity 100 (so Noise 0.5 = these levels, 1.0 = +12 dB). Chosen by ear-proxy against the one authored reference we
have: Salamander's own release-noise level (its SFZ: volume −37, amp_veltrack 82), which measures ≈ −30 dB
relative to the note (see library-report.md), i.e. clearly present on a quiet passage, hidden under a chord.
"""
import glob
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))

PIANO = [{"set": "piano-keydown", "trig": "on", "relDb": -32, "zone": "key", "velPow": 1.2},
         {"set": "piano-keyup", "trig": "off", "relDb": -30, "zone": "key", "keyStep": 2, "velPow": 0.5}]
PIANO_DOWN_ONLY = PIANO[:1]
BOW = [{"set": "bow-start", "trig": "on", "relDb": -30, "zone": 12, "maxFiles": 4, "velPow": 1.0,
        "exclude": "pizz|pizzicato|harmonic"}]
GUITAR = [{"set": "guitar-pick", "trig": "on", "relDb": -34, "zone": 12, "maxFiles": 4, "velPow": 1.3},
          {"set": "guitar-fret", "trig": "off", "relDb": -34, "zone": 12, "maxFiles": 3, "velPow": 0.4}]
GUITAR_FRET_ONLY = GUITAR[1:]
HARP = [{"set": "guitar-fret", "trig": "off", "relDb": -40, "zone": 12, "maxFiles": 3, "velPow": 0.4}]
BREATH_FLUTE = [{"set": "breath-puff", "trig": "on", "relDb": -30, "zone": 12, "maxFiles": 3, "velPow": 0.8}]
BREATH_REED = [{"set": "breath-puff", "trig": "on", "relDb": -33, "zone": 12, "maxFiles": 3, "velPow": 0.8}]
BREATH_BRASS = [{"set": "breath-puff", "trig": "on", "relDb": -34, "zone": 12, "maxFiles": 3, "velPow": 0.8}]
BREATH_VOICE = [{"set": "breath-puff", "trig": "on", "relDb": -36, "zone": 12, "maxFiles": 3, "velPow": 0.6}]
SAX_KEYS = [{"set": "sax-keyclose", "trig": "on", "relDb": -34, "zone": "key", "keyMap": "stretch", "maxFiles": 2,
             "velPow": 0.8},
            {"set": "sax-keyopen", "trig": "off", "relDb": -32, "zone": "key", "keyMap": "stretch", "maxFiles": 2,
             "velPow": 0.3}]
BEAR_KEYS = [dict(SAX_KEYS[0], keyMap="nearest", maxFiles=4), dict(SAX_KEYS[1], keyMap="nearest", maxFiles=4)]
WW_KEYS = [dict(SAX_KEYS[0], relDb=-38), dict(SAX_KEYS[1], relDb=-36)]
KNOCK = [{"set": "wood-knock", "trig": "on", "relDb": -30, "zone": 12, "maxFiles": 4, "velPow": 1.4}]

TABLE = {
    # keys
    "salamander.grand.v3": PIANO_DOWN_ONLY,           # its own per-key release noises stay (trig off)
    "vcsl.keys.steinway-b": PIANO, "vcsl.keys.upright-yamaha": PIANO, "freepats.keys.old-piano": PIANO,
    "vcsl.keys.kawai-grand": PIANO, "vcsl.keys.upright-knight": PIANO,
    "osiris.keys.piano": PIANO[1:],                   # its recordings already carry the pre-note key/hammer noise
    "karoryfer.strings.cyborg-zinc": BOW, "vcsl.winds.ocarina": BREATH_FLUTE, "karoryfer.bass.sneaky": GUITAR_FRET_ONLY,
    # strings (bowed)
    "vsco2.strings.violin-section": BOW, "vsco2.strings.viola-section": BOW, "vsco2.strings.cello-section": BOW,
    "vsco2.strings.solo-violin": BOW, "vsco2.strings.contrabass": BOW, "karoryfer.strings.cello": BOW,
    "karoryfer.strings.meatbass": BOW, "karoryfer.strings.erhu": BOW,
    # plucked
    "karoryfer.guitar.emily": GUITAR, "karoryfer.guitar.shinyguitar": GUITAR_FRET_ONLY,
    "freepats.guitar.nylon": GUITAR, "itsclipping.plucked.ganjo": GUITAR, "karoryfer.bass.growlybass": GUITAR,
    "karoryfer.guitar.black-and-green": GUITAR,
    "vcsl.plucked.concert-harp": HARP, "versilian.harp.etherealwinds": HARP, "vcsl.plucked.dan-tranh": HARP,
    "karoryfer.plucked.hungarian-zither": HARP, "karoryfer.plucked.cithara": HARP,
    # winds
    "vsco2.woodwinds.flute": BREATH_FLUTE + WW_KEYS, "vcsl.winds.alto-recorder": BREATH_FLUTE,
    "vsco2.woodwinds.clarinet": BREATH_REED + WW_KEYS, "vsco2.woodwinds.oboe": BREATH_REED + WW_KEYS,
    "vsco2.woodwinds.bassoon": BREATH_REED + WW_KEYS,
    "karoryfer.sax.bear": BREATH_REED + BEAR_KEYS, "karoryfer.sax.weresax-alto": BREATH_REED + SAX_KEYS,
    "vcsl.winds.tenor-sax": BREATH_REED + SAX_KEYS, "mtg.sax.alto": BREATH_REED + SAX_KEYS,
    "mtg.sax.tenor": BREATH_REED + SAX_KEYS, "mtg.sax.soprano": BREATH_REED + SAX_KEYS,
    "mtg.sax.baritone": BREATH_REED + SAX_KEYS,
    # brass
    "vsco2.brass.trumpet": BREATH_BRASS, "vsco2.brass.french-horn": BREATH_BRASS, "vsco2.brass.trombone": BREATH_BRASS,
    "vsco2.brass.tuba": BREATH_BRASS, "karoryfer.brass.war-tuba": BREATH_BRASS,
    # mallets (wooden bars)
    "vcsl.mallets.marimba": KNOCK, "vcsl.mallets.xylophone": KNOCK, "vcsl.perc.balafon": KNOCK,
    # voice
    "karoryfer.voice.male-aah": BREATH_VOICE,
}


def main():
    n = 0
    for p in sorted(glob.glob(os.path.join(HERE, "recipes", "*.json"))):
        r = json.load(open(p))
        nm = TABLE.get(r["id"])
        if nm is None:
            continue
        r["noiseMap"] = nm
        with open(p, "w") as f:
            json.dump(r, f, indent=1)
        n += 1
    print(f"noiseMap written to {n} recipes")


if __name__ == "__main__":
    main()
