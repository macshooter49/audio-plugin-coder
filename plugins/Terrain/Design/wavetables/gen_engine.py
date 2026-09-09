"""
gen_engine — the tables only Terrain can ship.

Every table here is rendered from one of Terrain's OWN engines:
  HARMONIC : the 512-partial additive bank, read straight out of HarmonicEngine's prepared
             spectrum. Six spectral families x sculpt regimes.
  PHYSICAL : ModalEngine, across all NINE instrument families and all THREE of its cores —
             a string waveguide (Grand, Pluck), a reed-bore (Bow, Flute, Reed, Brass) and a
             modal bank (Bars, Bells, Skin). No wavetable synth has factory tables like these,
             because none of them has a physical-modelling engine to render from.

The C++ side (bank2_probe.cpp) does the rendering; this module turns the dumps into cycles.
"""
import sys, os, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import wtlib

HERE = os.path.dirname(os.path.abspath(__file__))
# fb606 — the dumps are 102 MB of raw float; they do NOT belong in the worktree. bank2_probe
# writes wherever you point it, so honour the same env var here and say out loud which path
# won and whether anything was actually found. A silent empty DUMP is how you get a bank with
# no HARMONIC and no PHYSICAL folder at all and no error to explain it.
DUMP = os.environ.get("TERRAIN_WT_DUMP") or os.path.join(HERE, "dump2")
MODAL_F0 = 55.0

# what each table is FOR, musically
PURPOSE = {
    "BLADE RISE":    "the workhorse additive sweep — thin and dark to wide and blazing",
    "BLADE SPLAY":   "partials fan apart mid-sweep; wide leads that refuse to sit still",
    "BLADE CULL":    "runs BACKWARDS, bright to hollow — for reverse risers and suck-ins",
    "NEON RISE":     "the brightest additive family, 512 partials at the top; hyperpop leads",
    "NEON CLANG":    "Clang sculpt over Neon — glassy, struck, faintly metallic",
    "CONSOLE DRIVE": "deliberately sparse — an organ-ish few-partial tone, for stacking under",
    "CHANT RISE":    "vocal-leaning additive family; pads that bloom toward a choir",
    "CHANT TERRACE": "Terrace sculpt steps the spectrum in tiers — rhythmic timbre under a held note",
    "BRONZE RISE":   "stiff-string inharmonic family, projected — gong-adjacent tonal beds",
    "BRONZE TIDE":   "Tide sculpt ripples the bronze partials — slow evolving metal",
    "HORNET RISE":   "pulse-train buzz wall; aggressive basses and reeds",
    "HORNET CULL":   "buzz thinned to a whistle across the sweep — tension risers",
    "GRAND STRIKE":  "STRING WAVEGUIDE — struck piano-like, strike position sweeping",
    "PLUCK NAIL":    "STRING WAVEGUIDE — plucked, flesh to nail; guitars and kotos",
    "BOW PRESSURE":  "REED-BORE — bow pressure RELEASING, crush back to whisper (fb606: swept\n                     the other way; forwards it measured 4.23 dB from BRASS BLARE and was cut)",
    "FLUTE BREATH":  "REED-BORE — air-column tone, breath opening across the sweep",
    "REED BITE":     "REED-BORE — clarinet-ish odd-harmonic bite growing to a squeal",
    "BRASS BLARE":   "REED-BORE — brass overblow; the spectrum opens as it gets loud",
    "BARS WOOD":     "MODAL BANK — marimba to glass bar, the material morphing under you",
    "BARS GLASS":    "MODAL BANK — glassy bars OPENING, geometry expanding (fb606: reversed;\n                     compressing it sat 4.79 dB from BELLS GONG and was cut)",
    "BELLS METAL":   "MODAL BANK — a huge metal bell CLOSING to small and dry (fb606: reversed;\n                     rising it sat 3.78 dB from BARS WOOD and was cut)",
    "BELLS GONG":    "MODAL BANK — gong geometry, stretch collapsing; cinematic impacts",
    "SKIN DRUM":     "MODAL BANK — membrane modes; toms and hand percussion beds",
    "SKIN TENSION":  "MODAL BANK — a drum head tightening to a rattle; industrial textures",
}


def _from_spec(path):
    a = np.fromfile(path, dtype='<f4').reshape(wtlib.FRAMES, 1024)[:, :wtlib.NH]
    a = np.nan_to_num(a, nan=0.0, posinf=0.0, neginf=0.0)
    pk = a.max(axis=1, keepdims=True)
    a = np.divide(a, pk, out=np.zeros_like(a), where=pk > 0)
    return wtlib.finalize(wtlib.cycles_from_mags(a))


def _from_modal(path, f0=MODAL_F0):
    """Project the rendered resonator onto the harmonic grid.

    A struck bar is INHARMONIC, so its partials do not sit on integer multiples of f0. Summing
    the energy in a +-half-harmonic window around each n*f0 keeps WHICH regions ring and how
    loudly, and lets the grid quantise the rest. That is what makes the result still sound like
    the instrument while being exactly periodic — which a wavetable frame must be.
    """
    a = np.fromfile(path, dtype='<f4').reshape(wtlib.FRAMES, 16384)
    freqs = np.fft.rfftfreq(16384, 1.0 / wtlib.SR)
    nmax = min(int((wtlib.SR / 2 - f0) / f0), wtlib.NH)
    idx = [np.where((freqs >= n * f0 - f0 * 0.5) & (freqs < n * f0 + f0 * 0.5))[0]
           for n in range(1, nmax + 1)]
    mags = np.zeros((wtlib.FRAMES, wtlib.NH))
    win = np.hanning(16384)
    for i, row in enumerate(a):
        S2 = np.abs(np.fft.rfft(row * win)) ** 2
        v = np.array([np.sqrt(S2[ix].sum()) if len(ix) else 0.0 for ix in idx])
        if v.max() > 0:
            v /= v.max()
        mags[i, :nmax] = v
    return wtlib.finalize(wtlib.cycles_from_mags(mags))


def _mk(path, modal):
    return (lambda p=path, m=modal: (_from_modal(p) if m else _from_spec(p)))


TABLES = []
for p in sorted(glob.glob(os.path.join(DUMP, "H_*.spec"))):
    TABLES.append(("TERRA " + os.path.basename(p)[2:-5].replace("_", " "), _mk(p, False)))
for p in sorted(glob.glob(os.path.join(DUMP, "M_*.audio"))):
    TABLES.append(("TERRA " + os.path.basename(p)[2:-6].replace("_", " "), _mk(p, True)))

# ══════════════════════════════════════════════════════════════════════════════════════
# fb606 — THE MERGED TEN. Additive bank -> "Harmonic", modal engine -> "Physical".
#
# ⚠️ THE BELLS STAY HERE. The obvious re-file was BELLS METAL / BELLS GONG -> "Metallic", next
# to the additive clang tables imitating them, and it was MEASURED AND REJECTED: in Metallic the
# two modal bells compete against twelve bright additive tables, the selection score rewards
# brightness, and BOTH bells were cut while Physical shipped 8 instead of 10. That is the EXACT
# failure the per-category quota was written to stop (gate.py: "a global score deleted all 12
# PHYSICAL tables") — moving a dark table into a bright category re-creates it inside one folder.
# A physical-modelled bell is a physical-model table first; Metallic is served by the twelve
# struck/rung tables in gen_cinematic. Do not move these two without re-running the gate.
# ══════════════════════════════════════════════════════════════════════════════════════
_PHYSICAL = ("GRAND STRIKE", "PLUCK NAIL", "BOW PRESSURE", "FLUTE BREATH", "REED BITE",
             "BRASS BLARE", "BARS WOOD", "BARS GLASS", "BELLS METAL", "BELLS GONG",
             "SKIN DRUM", "SKIN TENSION")

CATEGORY = {name: ("Physical" if name[6:] in _PHYSICAL else "Harmonic") for name, _ in TABLES}

if not TABLES:
    print(f"  !! gen_engine: NO DUMPS in {DUMP} — the Harmonic and Physical folders will be "
          f"EMPTY. Build bank2_probe.cpp and run it into that directory first.")
else:
    print(f"  gen_engine: {len(TABLES)} engine dumps found in {DUMP}  "
          f"({sum(v=='Harmonic' for v in CATEGORY.values())} Harmonic, "
          f"{sum(v=='Physical' for v in CATEGORY.values())} Physical)")

if __name__ == "__main__":
    fps = {}
    for n, f in TABLES:
        fr = f()
        ok, line = wtlib.selfcheck(n, fr, min_h60=5, min_span=3.0)
        print(f"{CATEGORY[n]:<9} {line}   — {PURPOSE.get(n[6:], '')}")
        fps[n] = wtlib.fingerprint(fr)
    names = list(fps)
    worst = min(((wtlib.distance(fps[a], fps[b]), a, b)
                 for i, a in enumerate(names) for b in names[i + 1:]), default=(99, '', ''))
    print(f"\nminimum pairwise distance within gen_engine: {worst[0]:.2f} dB  ({worst[1]} vs {worst[2]})")
