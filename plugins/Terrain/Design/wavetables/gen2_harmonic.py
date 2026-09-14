"""
gen2_harmonic — the HARMONIC category of the 500: Terrain's own HarmonicEngine, pushed.

Every table starts from a dump of the REAL engine made by bank3h_probe.cpp (beside this file):
    c++ -std=c++17 -O2 -I ../../Source bank3h_probe.cpp -o /tmp/bank3h && /tmp/bank3h <dump dir>
    (dump dir = $TERRAIN_H3_DUMP, else <wt500>/engine3h; about 4 s for every job)
  S_<JOB>.spec   the post-sculpt 512-partial bank, per frame, per partial {ratio, ampL, ampR}
  A_<JOB>.audio  one steady-state rendered period per frame, incl. FORGE (the engine's saturator)
The probe runs a per-frame PROGRAM over one engine (family, sculpt, hue/lean/carve, the engine clock,
Fan's orbit, Root/Shine ghosts, Grit, Braid, Forge), stacks engine voices into chords as audio, and
chains the engine into itself through its TABLE source. This module only
  - projects the banks onto the 1024-harmonic grid (energy split between the two nearest bins, so a
    stretched partial glides), or takes the rendered period's own spectrum;
  - stacks separately-dumped engine voices where a table is an organ, a chord or an arpeggio;
  - picks ONE phase law per table (see phase_law) — the look and the crest factor, never the spectrum.
Each table's docstring says what its frame axis does. Nothing is random at render time: the engine
dumps are deterministic (fixed seeds) and every phase law is closed-form.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import wtlib

F, N, NH = wtlib.FRAMES, wtlib.SIZE, wtlib.NH
_WT500 = "/private/tmp/claude-501/-Users-macshooter/521ae994-b058-4e64-9804-78f1254cea68/scratchpad/wt500"
DUMP = os.environ.get("TERRAIN_H3_DUMP") or os.path.join(os.environ.get("TERRAIN_WT500") or _WT500, "engine3h")
U = np.linspace(0.0, 1.0, F)


# ── engine dumps ──────────────────────────────────────────────────────────────────────────────
def _spec(name):
    p = os.path.join(DUMP, "S_%s.spec" % name)
    if not os.path.isfile(p):
        raise FileNotFoundError("%s missing — build and run bank3h_probe.cpp into %s" % (p, DUMP))
    return np.fromfile(p, dtype="<f4").reshape(F, 512, 3).astype(np.float64)


def _audio(name):
    p = os.path.join(DUMP, "A_%s.audio" % name)
    if not os.path.isfile(p):
        raise FileNotFoundError("%s missing — build and run bank3h_probe.cpp into %s" % (p, DUMP))
    return np.fromfile(p, dtype="<f4").reshape(F, 2, N).astype(np.float64)


def project(ratio, amp):
    """(F, K) partial ratios (in table harmonics) + amplitudes -> (F, NH) magnitudes. Energy is split
    between the two nearest harmonics in proportion to distance, so a stretched / detuned partial
    GLIDES across the grid instead of snapping; coincident partials add in power."""
    P = np.zeros((ratio.shape[0], NH + 2))
    r = np.where(amp > 0, ratio, 0.0)
    b = np.floor(r).astype(int)
    fr = r - b
    ok = (r >= 1.0) & (r < NH)
    rows = np.repeat(np.arange(ratio.shape[0])[:, None], ratio.shape[1], axis=1)
    e = amp ** 2
    np.add.at(P, (rows[ok], b[ok]), e[ok] * (1.0 - fr[ok]))
    np.add.at(P, (rows[ok], b[ok] + 1), e[ok] * fr[ok])
    return np.sqrt(P[:, 1:NH + 1])


def spec_mags(name, ch="mono"):
    a = _spec(name)
    r, L, R = a[..., 0], a[..., 1], a[..., 2]
    amp = {"mono": np.hypot(L, R), "L": L, "R": R}[ch]
    return project(r, amp)


def _dcb_gain():
    # FORGE's DC blocker y = x - x1 + 0.995 y1 sits at 1.6 x f0 when a period is 2048 samples; at a
    # played pitch it is inaudible (35 Hz at 44.1k), so it is divided back out of the analysis.
    w = 2 * np.pi * np.arange(1, NH + 1) / N
    z = np.exp(-1j * w)
    return np.abs((1 - z) / (1 - 0.995 * z))


def audio_spec(name, ch=0):
    """(F, NH) complex harmonics of the rendered period (DC blocker compensated)."""
    a = _audio(name)[:, ch, :]
    S = np.fft.rfft(a, axis=1)[:, 1:NH + 1]
    return S / _dcb_gain()[None, :]


def norm_rows(m):
    pk = m.max(axis=1, keepdims=True)
    return np.divide(m, pk, out=np.zeros_like(m), where=pk > 0)


def build(mags, phase=None):
    return wtlib.finalize(wtlib.cycles_from_mags(norm_rows(mags), phase))


def build_complex(S):
    out = np.zeros((F, N))
    for i in range(F):
        spec = np.zeros(N // 2 + 1, dtype=complex)
        spec[1:NH + 1] = S[i]
        out[i] = np.fft.irfft(spec, n=N)
    return wtlib.finalize(seam_roll(out))


# ── phase laws ────────────────────────────────────────────────────────────────────────────────
# The magnitudes are the engine's; the PHASE decides what the frame LOOKS like and how loud it
# plays at peak 1. One law per table, fixed across all 128 frames (a moving phase mushes the morph),
# except MINPHASE, which follows the magnitudes smoothly (a real filter's own phase).
#   rand      wtlib.PHASE — the bank-wide neutral set
#   sine      every partial in sine phase: 1/n spectra draw as ramps, sparse ones as clean figures
#   chirp K   Schroeder: partial n sits at time n/K of the cycle — the spectrum becomes a picture in
#             time (a sieve draws as gaps, a formant as a burst) and the crest factor collapses
#   logchirp  as chirp but partial n sits at log(n)/log(K): octaves get equal time
#   minphase  per-frame minimum phase: resonances ring and decay inside the cycle
#   scatter   (fb638 level fix) NOT a law of its own: a zero-mean, closed-form offset per harmonic
#             (golden-ratio quadratic residue, wrapped to (-pi, pi]) ADDED to a table's law by a weight
#             w(frame) in 0..1 — see scatter(). Where a sieve or a stretch leaves the survivors lined up
#             in phase, the frame is a needle (crest 7-10) that plays 10 dB quiet at peak 1; the scatter
#             spreads it (crest ~3) without touching a magnitude, so the fingerprint and the journey are
#             the engine's. w rises and falls over >= 20 frames, so no partial's phase moves more than
#             ~0.2 rad from one frame to the next (the morph never smears), and w = 0 wherever the
#             table's own look is the point (a true saw at frame 0, the fuzz brick at the end).
SCATTER = np.angle(np.exp(2j * np.pi * ((np.arange(1, NH + 1, dtype=float) ** 2 * (np.sqrt(5.0) - 1.0) / 2.0) % 1.0)))


def scatter(S, w):
    """(F, NH) complex harmonics, w (F,) in 0..1 -> the same magnitudes with w * SCATTER added to every phase."""
    return S * np.exp(1j * np.asarray(w, float)[:, None] * SCATTER[None, :])


def phase_law(kind, K=512.0):
    n = np.arange(1, NH + 1, dtype=float)
    if kind == "rand":
        return wtlib.PHASE
    if kind == "sine":
        return np.full(NH, -np.pi / 2)
    if kind == "chirp":
        return -np.pi / 2 - np.pi * n ** 2 / K
    if kind == "logchirp":
        tpos = np.log(n) / np.log(K)
        return -np.pi / 2 - 2 * np.pi * np.cumsum(tpos)
    raise ValueError(kind)


def minphase_cycles(mags, floor_db=-110.0):
    """(F, NH) magnitudes -> (F, SIZE) minimum-phase cycles (real-cepstrum folding)."""
    out = np.zeros((mags.shape[0], N))
    fl = 10 ** (floor_db / 20)
    w = np.zeros(N); w[0] = 1.0; w[1:N // 2] = 2.0; w[N // 2] = 1.0
    for i, m in enumerate(norm_rows(mags)):
        mag = np.zeros(N)
        mag[1:NH + 1] = m
        mag[N - NH + 1:] = m[:NH - 1][::-1]          # conjugate-symmetric magnitude
        c = np.fft.ifft(np.log(np.maximum(mag, fl))).real
        ph = np.angle(np.exp(np.fft.fft(c * w)))
        H = mag * np.exp(1j * ph)
        H[0] = 0.0
        out[i] = np.fft.ifft(H).real
    return out


def seam_roll(cyc, tries=64):
    """Rotate every frame by the SAME amount so the wrap lands where the waveform is quiet: the roll
    (of `tries` candidates) whose worst frame has the smallest wrap jump relative to its own largest
    in-cycle steps (the measure wtkit's seam bar uses). Magnitudes and the fingerprint are untouched."""
    x = np.asarray(cyc, float)
    d = np.abs(np.diff(x, axis=1))
    k = max(1, int(round(0.01 * d.shape[1])))
    typ = np.partition(d, d.shape[1] - k, axis=1)[:, -k:].mean(axis=1)
    best, bestv = 0, np.inf
    for r in range(0, N, N // tries):
        i0, i1 = (-r) % N, (-r - 1) % N          # after np.roll(x, r): x[:, -r] is first, x[:, -r-1] last
        v = (np.abs(x[:, i0] - x[:, i1]) / np.maximum(typ, 1e-12)).max()
        if v < bestv - 1e-9:
            best, bestv = r, v
    return np.roll(x, best, axis=1)


def _crest(cyc):
    rms = np.sqrt((cyc ** 2).mean(axis=1))
    return float(np.mean(np.abs(cyc).max(axis=1) / np.maximum(rms, 1e-12)))


def build_law(mags, law="rand", K=None, roll=None):
    """Magnitudes + a phase law -> finalized table. For the chirps K=None picks the sweep constant
    (of 128 .. 2048) with the lowest mean crest over the table — one K for all 128 frames, so the
    phase still never moves. Laws that line partials up (sine, minphase) get the seam-safe roll
    unless a fixed roll (fraction of a cycle) is given."""
    mg = norm_rows(mags)
    if law == "minphase":
        cyc = minphase_cycles(mags)
    elif law in ("chirp", "logchirp") and K is None:
        best = None
        for k in (128.0, 256.0, 512.0, 1024.0, 2048.0):
            c = wtlib.cycles_from_mags(mg, phase_law(law, k))
            cr = _crest(c)
            if best is None or cr < best[0]:
                best = (cr, c)
        cyc = best[1]
    else:
        cyc = wtlib.cycles_from_mags(mg, phase_law(law, 512.0 if K is None else K))
    if roll is not None:
        cyc = np.roll(cyc, int(round(roll * N)), axis=1)
    elif law in ("sine", "minphase"):
        cyc = seam_roll(cyc)
    return wtlib.finalize(cyc)


def sstep(u, a, b):
    t = np.clip((np.asarray(u, float) - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


# ── the tables ────────────────────────────────────────────────────────────────────────────────
def overdrive_organ():
    """Console drawbars rendered as AUDIO through the engine's own FORGE (m = 2: the 16' is the root).
    Frames: full drawbars, the drive climbing with its feedback snarl; late on the bars detune toward
    the carillon under the drive and the organ tears into a storm."""
    return build_complex(audio_spec("OVERDRIVE_ORGAN"))


def carillon_storm():
    """Console full registration -> detuned carillon (hue .5 -> 1) with the Clang intermod lattice,
    Shine's octave ghost and Braid twins growing. Frames: organ -> bell cluster -> ringing storm."""
    return build_law(spec_mags("CARILLON"), "logchirp")


_RANKS = [(1, 1.00, -1.0), (2, 0.80, 0.06), (3, 0.62, 0.15), (4, 0.66, 0.22), (5, 0.50, 0.30),
          (6, 0.52, 0.36), (8, 0.55, 0.42), (10, 0.42, 0.48), (12, 0.45, 0.53), (16, 0.45, 0.58),
          (24, 0.40, 0.63), (32, 0.40, 0.67)]


def plenum_collapse():
    """A pipe organ built from twelve engine voices (8', 4', 2 2/3', 2', tierce ... 32-foot-up
    mixtures) plus a reed rank. Frames: the registration pulls stops one by one (8' flute -> full
    plenum with mixtures -> reeds), then every rank stretches (Splay) and haunts (Grit) at once."""
    P = np.zeros((F, NH))
    for m, g, t0 in _RANKS:
        env = np.ones(F) if t0 < 0 else sstep(U, t0, t0 + 0.07)
        P += (g * env[:, None] * spec_mags("RANK_%02d" % m)) ** 2
    P += (0.9 * sstep(U, 0.70, 0.80)[:, None] * spec_mags("RANK_REED")) ** 2
    return build_law(np.sqrt(P), "logchirp")


def overtone_whistle():
    """Neon at Q 12 with its sweep clock parked so the resonance walks UP the harmonic series
    (2.6 -> 12, an overtone melody); then Tide cuts a comb through it and Shine's ghost and Grit's
    drift break it apart. Minimum phase: the resonance rings inside the cycle."""
    return build_law(spec_mags("OVERTONE_WHISTLE"), "minphase")


def overtone_chant():
    """Chant read at C2 so its formants sit on harmonics 5..46: oo -> ah -> ee is an overtone
    singer's melody, then the choir scatters, the Clang lattice and Braid twins take over."""
    return build_law(spec_mags("OVERTONE_CHANT"), "logchirp")


def prime_choir():
    """Chant through Cull: the choir's formants on a sieve. Frames: full choir -> odd harmonics ->
    primes -> only the Fibonacci harmonics still sing."""
    return build_law(spec_mags("PRIME_CHOIR"), "chirp")


def fibonacci_sieve():
    """Hornet's full 512-partial swarm through the whole Cull chain. Frames: full -> odd -> primes ->
    only the Fibonacci harmonics (1 2 3 5 8 13 21 34 55 89 144 233 377) survive. Chirp phase: the
    sieve draws as gaps in the sweep."""
    return build_law(spec_mags("FIB_SIEVE"), "chirp")


def tide_storm():
    """Tide's travelling amplitude ripple with the engine clock running across the frames while the
    tilt opens. Frames: a saw -> slow swell -> tightening comb -> spectral Leslie flutter."""
    return build_law(spec_mags("TIDE_STORM"), "chirp")


def golden_orbit():
    """Fan read from the RIGHT channel while the tilt opens: saw -> odd/even split (the right ear
    hears the even partials: the octave jumps up over a thinned root) -> golden-angle partial
    scatter, the orbit turning three times across the table."""
    return build_law(spec_mags("GOLDEN_ORBIT", "R"), "logchirp")


def forged_swarm():
    """Hornet's aligned-phase buzz rendered as AUDIO through FORGE. Frames: a 12-partial buzz ->
    the full swarm -> saturated into a fuzz brick. Level fix (fb638 critique): the full aligned swarm
    (frames ~27-79) was a needle at crest 8.6 / -18.7 dB RMS, so the scatter fades in over frames 6-28
    and back out over 90-121; the 12-partial buzz and the brick keep FORGE's own phase."""
    w = sstep(U, 0.05, 0.22) * (1.0 - sstep(U, 0.70, 0.95))
    return build_complex(scatter(audio_spec("FORGED_SWARM"), w))


def megaphone():
    """Keel: saw -> felt-blanket tilt -> the pivot slides up to harmonic 18 and the lows duck (a
    megaphone hump climbing), Shine's octave ghost on top."""
    return build_law(spec_mags("MEGAPHONE"), "minphase")


def sub_bell():
    """Bronze (stiff string -> gong) at half-harmonic resolution with Root's sub ghost holding bin 1,
    the Clang lattice rising. Frames: string over a sub -> stretched bell -> intermod gong."""
    return build_law(spec_mags("SUB_BELL"), "chirp")


def terrace_sputter():
    """Terrace dB-quantising Neon's parked resonance, re-dithered as the clock runs. Frames: fine
    shelves -> coarse steps -> dithered sputter."""
    return build_law(spec_mags("TERRACE_SPUTTER"), "chirp")


_VOICES = [(1, 0.85, -1.0), (4, 1.00, -1.0), (5, 0.90, 0.10), (6, 0.85, 0.20), (7, 0.80, 0.33),
           (9, 0.70, 0.44), (11, 0.65, 0.54), (13, 0.60, 0.63)]   # sub, root, 3rd, 5th, 7th, 9th, 11th, 13th


def overtone_chord():
    """Chord in one cycle from eight engine voices on harmonics 1, 4, 5, 6, 7, 9, 11, 13 (a sub two
    octaves under a root, then 3rd, 5th, harmonic 7th, 9th, 11th, 13th). Frames: sub + root -> triad
    -> the whole overtone chord -> every voice stretches (Splay) and braids."""
    P = np.zeros((F, NH))
    for m, g, t0 in _VOICES:
        env = np.ones(F) if t0 < 0 else sstep(U, t0, t0 + 0.08)
        P += (g * env[:, None] * spec_mags("VOICE_%02d" % m)) ** 2
    return build_law(np.sqrt(P), "logchirp")


def sub_shatter():
    """Hornet swarm at m = 2 with Root's sub ghost locked on bin 1; Splay is already stretching it at
    frame 0 (wild from the start, the sub keeps it playable), then glass -> gong, partials flying off
    the top of the grid."""
    return build_law(spec_mags("SUB_SHATTER"), "chirp")


def octave_ghost():
    """A hollow reed (evens faded) and Shine's detuned octave ghost filling the even slots, beating
    as the engine clock runs while the tilt opens; Braid twins, then Splay stretches it to glass."""
    return build_law(spec_mags("OCTAVE_GHOST"), "sine")


def fuzz_chant():
    """Chant rendered as AUDIO through FORGE: the vowel walk oo -> ah -> ee while the drive climbs
    until the choir is a fuzz scream."""
    return build_complex(audio_spec("FUZZ_CHANT"))


def power_chord():
    """Five Neon voices (root, fifth, octave on harmonics 2 3 4; later the third 5 and harmonic
    seventh 7) summed as AUDIO into ONE Forge. Frames: clean power chord -> driven -> the third and
    seventh walk into the fuzz and the intermodulation tears it open."""
    return build_complex(audio_spec("POWER_CHORD"))


def prime_glass():
    """The engine run twice: Hornet's swarm sieved to its PRIMES (Cull), fed back in as a TABLE and
    stretched (Splay). Frames: swarm -> primes -> the primes fan out into glass and fly apart."""
    return build_law(spec_mags("PRIME_GLASS"), "sine")


def glass_choir():
    """Chant (ah, a scattered choir) through Splay: the formants stay where the voice put them while
    the partials under them are stretched off the harmonic series. Frames: choir -> glass choir ->
    shattered glass."""
    return build_law(spec_mags("GLASS_CHOIR"), "logchirp")


def sieve_fuzz():
    """A true (sine-phase) saw through the Cull chain, rendered as AUDIO into FORGE: as the sieve
    empties the spectrum (odd -> primes -> Fibonacci) the drive rises and the survivors'
    intermodulation refills every hole. Level fix (fb638 critique): with the sieve down to primes and
    Fibonacci survivors still in sine phase, frames ~20-66 were clicks (crest 10, -20 dB RMS), so the
    scatter fades in over frames 0-23 and out over 66-91: frame 0 is still the true saw and the end is
    still FORGE's brick."""
    w = sstep(U, 0.0, 0.18) * (1.0 - sstep(U, 0.52, 0.72))
    return build_complex(scatter(audio_spec("SIEVE_FUZZ"), w))


def sub_choir():
    """Chant at m = 2 over Root's sub ghost (bin 1, an octave under the voice). Frames: oo -> ah ->
    ee over a held sub, then Tide's ripple spins the choir into a Leslie flutter."""
    return build_law(spec_mags("SUB_CHOIR"), "logchirp")


_ARP = [4, 5, 6, 8, 10, 12, 16, 14, 13, 11, 9, 7]


def overtone_arpeggio():
    """A melody on the harmonic series, one engine voice at a time over a held sub: harmonics 4 5 6
    8 10 12 16 then down the strange ones 14 13 11 9 7 — scan it and it PLAYS. Each voice crossfades
    into the next; late in the table the voices' own Splay stretch and Braid smear the arpeggio.
    Sine phase draws each note as its saw teeth. Level fix (fb638 critique): once Splay/Braid smear the
    voices, sine phase lines the smeared partials up into needles (crest 5-7, -16 dB over the last 40
    frames), so the scatter fades in over frames 66-94, after the clean notes."""
    P = (0.8 * spec_mags("VOICE_01")) ** 2
    seg_len = 1.0 / len(_ARP)
    for i, m in enumerate(_ARP):
        c = (i + 0.5) * seg_len
        env = np.clip(1.0 - np.abs(U - c) / (0.75 * seg_len), 0.0, 1.0)
        env = env * env * (3 - 2 * env)
        if i == 0:
            env = np.where(U < c, 1.0, env)
        if i == len(_ARP) - 1:
            env = np.where(U > c, 1.0, env)
        P += (env[:, None] * spec_mags("VOICE_%02d" % m)) ** 2
    w = sstep(U, 0.52, 0.74)
    S = norm_rows(np.sqrt(P)) * np.exp(1j * phase_law("sine"))[None, :]
    cyc = np.fft.irfft(np.concatenate([np.zeros((F, 1), complex), scatter(S, w)], axis=1), n=N, axis=1)
    return wtlib.finalize(seam_roll(cyc))


def harmonic_cluster():
    """A harmonic-series TONE CLUSTER from eight engine voices on harmonics 8 .. 15, rendered as AUDIO:
    frame 0 is the lone voice on 8, the others join one by one, then ONE Forge on the sum grinds the
    cluster into a roar."""
    return build_complex(audio_spec("HARMONIC_CLUSTER"))


TABLES = [
    ("OVERDRIVE ORGAN", overdrive_organ),
    ("CARILLON STORM", carillon_storm),
    ("PLENUM COLLAPSE", plenum_collapse),
    ("OVERTONE WHISTLE", overtone_whistle),
    ("OVERTONE CHANT", overtone_chant),
    ("PRIME CHOIR", prime_choir),
    ("FIBONACCI SIEVE", fibonacci_sieve),
    ("TIDE STORM", tide_storm),
    ("GOLDEN ORBIT", golden_orbit),
    ("FORGED SWARM", forged_swarm),
    ("MEGAPHONE", megaphone),
    ("SUB BELL", sub_bell),
    ("TERRACE SPUTTER", terrace_sputter),
    ("OVERTONE CHORD", overtone_chord),
    ("SWARM SHATTER", sub_shatter),
    ("OCTAVE GHOST", octave_ghost),
    ("FUZZ CHANT", fuzz_chant),
    ("POWER CHORD", power_chord),
    ("PRIME GLASS", prime_glass),
    ("GLASS CHOIR", glass_choir),
    ("SIEVE FUZZ", sieve_fuzz),
    ("SPINNING CHOIR", sub_choir),
    ("OVERTONE ARPEGGIO", overtone_arpeggio),
    ("HARMONIC CLUSTER", harmonic_cluster),
]
CATEGORY = {ident: "Harmonic" for ident, _ in TABLES}
