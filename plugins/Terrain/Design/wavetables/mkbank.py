#!/usr/bin/env python3
"""mkbank.py — turn the rendered TERRA bank into the SHIPPING factory library.

Max: "we may have to make terra a factory lib. everything respective capitals, not ALL CAPS.
preset names 'Terra - (Name)' just so we have organization, the dash will give us that."

  ALL CAPS  ->  Title Case, acronyms kept, prefixed "Terra - "
  .wav f32  ->  .flac 24-bit   (120.0 MB -> ~58 MB; worst round-trip -138.5 dBr, measured)

The source is the RENDERED bank, not a regeneration: these are the exact tables Max has been
auditioning, so the conversion cannot change a sound."""
import soundfile as sf, numpy as np, os, sys, glob, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wtlib   # fb612 — the naming law lives in ONE place; a second copy here is how the two drift

# default to the generator's own output first; the legacy install path is the fallback so this
# still works on a machine where the bank was rendered before fb612 moved it into the repo.
_HERE = os.path.dirname(os.path.abspath(__file__))
_DEFAULTS = [os.path.join(_HERE, "bank"),
             os.path.expanduser("~/Library/WavesCrate/Terrain/Wavetables/Factory"),
             os.path.expanduser("~/Library/WavesCrate/TerrainInstrument/Wavetables/Factory")]
SRC = os.path.expanduser(sys.argv[1]) if len(sys.argv) > 1 and not sys.argv[1].startswith("-") \
      else next((d for d in _DEFAULTS if os.path.isdir(d)), _DEFAULTS[0])
DST = sys.argv[2] if len(sys.argv) > 2 else "."
WRITE = "--write" in sys.argv

pretty = wtlib.shipping_name

files = sorted(glob.glob(SRC + "/*/*.wav"))
if not files: sys.exit("no wavs under " + SRC)

# two tables genuinely share a name in different categories (verified: max|diff| 1.42 and 1.94, so
# they are different sounds, not a duplicated file). In a SHIPPING library that is ambiguous, so the
# non-primary one carries its category. Mechanical, and easy for Max to overrule.
stems = [os.path.splitext(os.path.basename(f))[0] for f in files]
dupes = {s for s, n in collections.Counter(stems).items() if n > 1}
PRIMARY = {"TERRA SHATTER": "Chaos", "TERRA SIERPINSKI": "Chaos"}

rows, seen = [], {}
for f in files:
    cat  = os.path.basename(os.path.dirname(f))
    stem = os.path.splitext(os.path.basename(f))[0]
    name = pretty(stem)
    if stem in dupes and PRIMARY.get(stem) != cat:
        name += " " + cat
    rows.append((cat, stem, name, f))
    seen.setdefault(name, []).append(cat)

clash = {n: c for n, c in seen.items() if len(c) > 1}
print("  %d tables · %d categories" % (len(rows), len({r[0] for r in rows})))
print("  name collisions after renaming: %s" % (clash if clash else "none"))
if clash: sys.exit("REFUSING to write with colliding names")

wrote = tot_in = tot_out = 0
worst = (0.0, "")
for cat, stem, name, f in rows:
    if WRITE:
        d = os.path.join(DST, cat); os.makedirs(d, exist_ok=True)
        out = os.path.join(d, name + ".flac")
        x, sr = sf.read(f, dtype='float32', always_2d=False)
        sf.write(out, x, sr, format='FLAC', subtype='PCM_24')
        y, _ = sf.read(out, dtype='float32')
        assert y.shape == x.shape, (out, y.shape, x.shape)
        err = float(np.max(np.abs(x - y))); pk = float(np.max(np.abs(x)))
        db = 20*np.log10(max(err,1e-30)/max(pk,1e-30))
        if db > worst[0] or worst[1] == "": worst = (db, name) if db > worst[0] else worst
        tot_in += os.path.getsize(f); tot_out += os.path.getsize(out); wrote += 1
print()
for cat in sorted({r[0] for r in rows}):
    ex = [r for r in rows if r[0] == cat][:2]
    print("  %-13s %2d   e.g.  %s" % (cat, sum(1 for r in rows if r[0] == cat),
          "   ".join('%s  ->  "%s"' % (r[1], r[2]) for r in ex)))
if WRITE:
    print("\n  wrote %d flac · %.1f MB -> %.1f MB (%.0f%%) · worst round-trip %.1f dBr (%s)"
          % (wrote, tot_in/1048576, tot_out/1048576, 100*tot_out/max(1,tot_in), worst[0], worst[1]))
else:
    print("\n  (dry run — pass --write to convert)")
