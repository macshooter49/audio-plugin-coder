"""
TERRA factory bank — SPECTRAL category (22 tables).

Everything here is built by writing the harmonic magnitude array (128 x 1024) directly
and letting wtlib's fixed PHASE set turn it into cycles. No time-domain shaping, no
sampling, no borrowed material: every number below comes out of a formula.

The design rule for the category: the FRAME AXIS is the instrument. Each table picks one
operation on the harmonic index (comb, tilt, shift, stretch, sieve, mask, blur, notch,
mirror) and sweeps it across 128 frames so you hear something a filter physically cannot
do — partials moving relative to each other, not a curve moving over static partials.
"""
import sys
sys.path.insert(0, "/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/wt")
import numpy as np
import wtlib

F, N = wtlib.FRAMES, wtlib.NH            # 128 frames, 1024 harmonics
n    = wtlib.n_ax.astype(float)          # harmonic numbers 1..1024
ni   = wtlib.n_ax.astype(np.int64)
ln   = np.log2(n)                        # harmonic axis in octaves
u    = np.linspace(0.0, 1.0, F)          # the frame axis


# ── shared spectral primitives ────────────────────────────────────────────────────────
def sm(x):
    x = np.clip(np.asarray(x, float), 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def pw(a):
    """(F,) exponents -> (F,N) power-law spectra n**-a."""
    return n[None, :] ** (-np.asarray(a, float)[:, None])


def dboct(s):
    """(F,) slopes in dB/octave -> (F,N) tilt. -24 dB/oct is sub weight, +12 is air."""
    return 10.0 ** (np.asarray(s, float)[:, None] * ln[None, :] / 20.0)


def bands(B, curve=2.2):
    """B log-ish bands over the harmonic axis, every band non-empty."""
    e = np.round(N * (np.arange(B + 1) / B) ** curve).astype(int)
    e[0], e[-1] = 0, N
    idx = np.zeros(N, dtype=int)
    for j in range(B):
        idx[e[j]:e[j + 1]] = j
    return idx


def scatter(pos, amp):
    """Deposit amp at fractional 1-based harmonic positions (linear interpolation)."""
    m = np.zeros(N + 2)
    i0 = np.floor(pos).astype(np.int64)
    fr = pos - i0
    s1 = (i0 >= 1) & (i0 <= N)
    np.add.at(m, i0[s1] - 1, (amp * (1.0 - fr))[s1])
    s2 = (i0 >= 0) & (i0 <= N - 1)
    np.add.at(m, i0[s2], (amp * fr)[s2])
    return m[:N]


def primes_to(k):
    s = np.ones(k + 1, dtype=bool)
    s[:2] = False
    for p in range(2, int(k ** 0.5) + 1):
        if s[p]:
            s[p * p::p] = False
    return np.nonzero(s)[0]


PRIMES = primes_to(N)
PRIME_MASK = np.zeros(N, dtype=bool)
PRIME_MASK[PRIMES - 1] = True


def parity(x):
    p = np.zeros_like(x)
    for b in range(11):
        p ^= (x >> b) & 1
    return p


def out(mags):
    """Sanitise magnitudes, guarantee no dead frame, render, finalize."""
    m = np.maximum(np.nan_to_num(np.asarray(mags, float)), 0.0)
    rm = m.max(axis=1, keepdims=True)
    m = np.where(rm > 1e-30, m, (n ** -1.0)[None, :])
    return wtlib.finalize(wtlib.cycles_from_mags(m))


# ── 1. TERRA COMB WALK ────────────────────────────────────────────────────────────────
# FOR: a metallic resonator sweep — the comb teeth close from 46 harmonics apart to 2,
# so the "body" of the sound shrinks from a hollow pipe to a ring-modulated blade.
def terra_comb_walk():
    sp = 46.0 * (2.0 / 46.0) ** u
    a = 0.98 - 0.66 * sm(u)
    dep = 0.5 + 0.5 * sm(np.clip(u * 1.5, 0, 1))
    c = 0.5 + 0.5 * np.cos(2 * np.pi * n[None, :] / sp[:, None])
    return out(pw(a) * (1.0 - dep[:, None] + dep[:, None] * c) ** 2.4)


# ── 2. TERRA TILT ARC ─────────────────────────────────────────────────────────────────
# FOR: the whole tonal range in one knob — walks a straight spectral tilt from -26 dB/oct
# (pure sub weight, almost a sine) to +14 dB/oct (piercing air), with a slow shelf rotating
# through it so the middle of the sweep is not just a dull line.
def terra_tilt_arc():
    s = -26.0 + 40.0 * (u ** 0.85)
    shelf = 1.0 + 0.85 * np.cos(2 * np.pi * ln[None, :] / 11.0 - np.pi * u[:, None])
    return out(dboct(s) * (0.3 + 0.7 * shelf / 1.85))


# ── 3. TERRA SHIFT GRID ───────────────────────────────────────────────────────────────
# FOR: the timbre inverting while the pitch stands still — the whole series slides up the
# integer grid by 90 harmonics, so the fundamental and the low partials vanish and what is
# left is a shifted residue that still reads as the same note.
def terra_shift_grid():
    env = n ** -0.82
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        sv = 92.0 * sm(uu) ** 1.25
        m[i] = scatter(n + sv, env * (1.0 + 0.9 * sv / 92.0))
    return out(m)


# ── 4. TERRA STRETCH ──────────────────────────────────────────────────────────────────
# FOR: inharmonic bell weight without detuning anything — partials are remapped n -> n^k
# with k walking 0.62 (1024 partials crushed into 80 slots, a dark metal slab) to 1.55
# (80 partials fanned across the whole spectrum, a struck glass rod).
def terra_stretch():
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        k = 0.62 + 0.93 * uu
        a = 0.92 - 0.62 * uu
        m[i] = scatter(np.maximum(n ** k, 1.0), n ** -a)
    return out(m)


# ── 5. TERRA PRIME WINDOW ─────────────────────────────────────────────────────────────
# FOR: an unrepeatable "ladder" resonance — only prime harmonics sound, and a narrow
# resonant window climbs the prime ladder from 2 to 1021, so the gaps between the primes
# widen audibly as it goes. Nothing periodic can make this interval pattern.
def terra_prime_window():
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        c = 0.55 + 9.2 * sm(uu)
        w = 0.45 + 2.1 * uu
        win = np.exp(-((ln - c) ** 2) / (2 * w * w))
        skirt = 0.008 + 0.05 * uu
        m[i] = (n ** -0.42) * win * (PRIME_MASK + skirt)
    m[:, 0] = m.max(axis=1) * 0.35
    return out(m)


# ── 6. TERRA FIBONACCI ────────────────────────────────────────────────────────────────
# FOR: a chime that thins out as it rings — starts as a dense blurred cloud and collapses
# onto the 15 Fibonacci partials, whose spacing grows by the golden ratio, so the top end
# ends up as isolated bells over a solid low body.
def terra_fibonacci():
    fib, a, b = [], 1, 2
    while a <= N:
        fib.append(a)
        a, b = b, a + b
    fib = np.array(fib, float)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        w = 0.62 * (0.030 / 0.62) ** sm(uu)
        ex = 1.62 - 1.58 * sm(uu)
        g = np.zeros(N)
        for fv in fib:
            g += (fv ** -ex) * np.exp(-((ln - np.log2(fv)) ** 2) / (2 * w * w))
        m[i] = g / (0.22 + w) + (n ** -2.1) * (1.0 - sm(uu * 1.3)) * 0.8
    return out(m)


# ── 7. TERRA SQUARE LAW ───────────────────────────────────────────────────────────────
# FOR: a full saw dissolving into the 32 square-numbered partials (1,4,9,...,1024) with a
# bright spot climbing that lattice — quadratically widening gaps, a glassy "wrong octave"
# stack you cannot build from any oscillator shape.
def terra_square_law():
    k = np.arange(1, 33)
    sq = (k * k)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        w = sm(np.clip(uu * 1.15, 0, 1))
        dense = (n ** -0.72) * (1.0 - w)
        emph = np.exp(-((k - (3.0 + 29.0 * sm(uu))) ** 2) / (2 * (5.5 + 6.0 * uu) ** 2))
        g = np.zeros(N)
        g[sq - 1] = (k ** -0.30) * emph
        m[i] = dense + g * (0.25 + 0.75 * w)
    return out(m)


# ── 8. TERRA OCTAVE TREE ──────────────────────────────────────────────────────────────
# FOR: an organ that grows branches — begins as pure powers of two (11 partials, hollow
# stopped-flute), then odd multipliers 3,5,7,9... enter one at a time, each dragging its
# own octave series in, until the lattice is a dense self-similar chord.
def terra_octave_tree():
    odds = np.arange(1, 64, 2)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        nb = 1.0 + 30.0 * (uu ** 0.45)
        oct_a = 0.95 - 0.72 * uu
        g = np.zeros(N)
        for j, mm in enumerate(odds):
            gate = sm(nb - j)
            if gate <= 0:
                continue
            j2 = 0
            while mm * (1 << j2) <= N:
                h = mm * (1 << j2)
                g[h - 1] += gate * (mm ** -0.72) * (2.0 ** (-oct_a * j2))
                j2 += 1
        m[i] = g
    return out(m)


# ── 9. TERRA RES DRIFT ────────────────────────────────────────────────────────────────
# FOR: four resonant peaks gliding on independent paths and crossing through each other —
# no vowel, no filter bank could track four separate poles this way. Use it for a pad whose
# internal resonances refuse to stay put.
def terra_res_drift():
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        cs = [0.30 + 9.30 * uu,
              9.10 - 7.60 * uu ** 0.7,
              5.00 + 3.20 * np.sin(2 * np.pi * uu),
              2.20 + 2.60 * uu + 1.90 * np.sin(4 * np.pi * uu + 1.1)]
        ws = [0.62 + 0.55 * uu, 1.15 - 0.55 * uu, 0.42 + 0.30 * np.cos(2 * np.pi * uu), 0.85]
        gs = [1.00, 0.72 + 0.28 * np.cos(2 * np.pi * uu), 0.55, 0.44 + 0.30 * np.sin(6 * np.pi * uu)]
        g = 0.020 * (n ** -0.55)
        for c, w, gg in zip(cs, ws, gs):
            g += abs(gg) * np.exp(-((ln - c) ** 2) / (2 * w * w))
        m[i] = g * (n ** -0.12)
    return out(m)


# ── 10. TERRA SMEAR ───────────────────────────────────────────────────────────────────
# FOR: the sound of a reverb tail replacing its own source — 96 fixed partials start as
# needle-sharp spikes and are progressively blurred along the log-harmonic axis until they
# fuse into one continuous cloud. Granular/film-score wash without a reverb.
def terra_smear():
    rng = np.random.default_rng(0x5EEDDA7A)
    pos = 2.0 ** rng.uniform(0.0, 9.98, 96)
    amp = rng.uniform(0.35, 1.0, 96) ** 2.0
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        w = 0.004 * (0.55 / 0.004) ** sm(uu)
        a = 1.28 - 1.18 * uu
        g = np.zeros(N)
        for p, av in zip(pos, amp):
            g += av * (p ** -a) * np.exp(-((ln - np.log2(p)) ** 2) / (2 * w * w))
        m[i] = g / (0.05 + w) ** 0.5
    return out(m)


# ── 11. TERRA SIERPINSKI ──────────────────────────────────────────────────────────────
# FOR: an IDM spectral sequencer — 25 bands switched on and off by rule-90 cellular
# automaton generations, so the frame axis literally scans a Sierpinski triangle through
# the spectrum. Hard-edged band jumps that no envelope or LFO shape can imitate.
def terra_sierpinski():
    B = 25
    bidx = bands(B)
    st = np.zeros(B, dtype=int)
    st[2] = 1
    gens = []
    for g in range(33):
        if st.sum() == 0:
            st[(g * 7) % B] = 1
        gens.append(st.copy())
        st = (np.roll(st, 1) ^ np.roll(st, -1))
    gens = np.array(gens, float)
    m = np.zeros((F, N))
    for i in range(F):
        p = i / (F - 1) * 32.0
        j = int(np.floor(p))
        fr = sm(p - j)
        row = gens[j] * (1 - fr) + gens[min(j + 1, 32)] * fr
        gate = 0.012 + 0.988 * row[bidx]
        m[i] = (n ** -0.34) * gate
    return out(m)


# ── 12. TERRA FRACTAL SHELF ───────────────────────────────────────────────────────────
# FOR: a spectrum that repeats itself at every octave scale — one ripple period per octave,
# then half, then quarter, layered like a Weierstrass curve, with the whole pattern sliding
# two octaves downward. A shimmer that stays the same shape however far you zoom in.
def terra_fractal_shelf():
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        per = 2.10 - 1.55 * uu
        phi = 2.40 * uu
        depth = 1.0 + 3.0 * uu
        g = np.zeros(N)
        amp = 1.0
        s = 0
        while s < depth:
            wgt = min(1.0, depth - s)
            g += wgt * amp * np.cos(2 * np.pi * (2 ** s) * (ln - phi) / per)
            amp *= 0.58
            s += 1
        gn = g / 2.4
        m[i] = (n ** -(0.98 - 0.74 * uu)) * (0.004 + 0.996 * (0.5 + 0.5 * np.clip(gn, -1, 1)) ** 4.2)
    return out(m)


# ── 13. TERRA ODD ROTATE ──────────────────────────────────────────────────────────────
# FOR: continuous rotation of the odd/even balance — two full turns from hollow odd-only
# (clarinet/square) through the full series to even-only (which reads an octave up), with a
# third rotation on multiples of three folding in. A morph, not a crossfade.
def terra_odd_rotate():
    th = 2.0 * u
    wo = (0.5 + 0.5 * np.cos(2 * np.pi * th))[:, None]
    we = (0.5 + 0.5 * np.cos(2 * np.pi * th + np.pi))[:, None]
    w3 = (0.5 + 0.5 * np.cos(2 * np.pi * 1.5 * u + 0.7))[:, None]
    odd = (ni % 2 == 1)[None, :]
    ev = ~odd
    m3 = (ni % 3 == 0)[None, :]
    sel = wo * odd + we * ev
    sel = sel * (1.0 - 0.85 * w3 * m3) + 0.03
    a = 1.12 - 0.80 * sm(u)
    return out(pw(a) * sel)


# ── 14. TERRA HOLES ───────────────────────────────────────────────────────────────────
# FOR: hollowness no analog wave has — twelve notches at irrational log spacings, sharp
# enough to erase two or three partials each, crawling from the bottom of the spectrum to
# the top while deepening. The bright end gets eaten out from under the sound.
def terra_holes():
    ph = np.array([0.13, 0.87, 1.61, 2.34, 3.07, 3.99, 4.71, 5.62, 6.48, 7.33, 8.19, 9.05])
    wd = np.array([0.055, 0.075, 0.062, 0.090, 0.070, 0.110, 0.084, 0.130, 0.096, 0.150, 0.115, 0.170])
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        g = n ** -(0.30 + 0.86 * sm(uu))
        drift = -3.9 + 4.6 * sm(uu)
        dep = 0.42 + 0.578 * sm(uu)
        sc = 1.0 + 1.1 * uu
        for c0, w in zip(ph, wd):
            c = c0 + drift
            ww = w * sc
            g = g * (1.0 - dep * np.exp(-((ln - c) ** 2) / (2 * ww * ww)))
        m[i] = g
    return out(m)


# ── 15. TERRA SIEVE ───────────────────────────────────────────────────────────────────
# FOR: purification — the Sieve of Eratosthenes run on the harmonic series. Multiples of 2
# leave, then 3, 5, 7, ... 53, until only primes remain. A saw that erodes into something
# that has no octave and no fifth left in it.
def terra_sieve():
    ps = PRIMES[:16]
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        a = 0.98 - 0.72 * uu
        g = n ** -a
        for j, p in enumerate(ps):
            s = sm((uu * 128.0 - (3.0 + j * 7.4)) / 6.0)
            if s <= 0:
                break
            hit = (ni % p == 0) & (ni != p)
            g = g * (1.0 - 0.985 * s * hit)
        m[i] = g
    return out(m)


# ── 16. TERRA MOIRE ───────────────────────────────────────────────────────────────────
# FOR: interference you can hear the beat of — two combs, one fixed at 610 harmonics, one
# sweeping 900 down to 5, multiply into a moire whose beat pattern crawls through the
# spectrum. Broad drifting humps at the start, fine metallic teeth at the end.
def terra_moire():
    s1 = 900.0 * (5.0 / 900.0) ** u
    c1 = 0.5 + 0.5 * np.cos(2 * np.pi * n[None, :] / s1[:, None] + 0.6)
    c2 = 0.5 + 0.5 * np.cos(2 * np.pi * n[None, :] / 610.0 - 2.4 * np.pi * u[:, None])
    p = (1.1 + 2.6 * u)[:, None]
    return out(pw(np.full(F, 0.36)) * (c1 ** p) * (c2 ** 1.4) + 1e-4 * (n ** -2.0)[None, :])


# ── 17. TERRA BIT MASK ────────────────────────────────────────────────────────────────
# FOR: digital glitch that is still in tune — a partial sounds only when the parity of
# (harmonic number AND mask) is even, with three bit-planes of the mask rotating across the
# frames. Thue-Morse combs at three power-of-two scales at once; pure bit-rate texture.
def terra_bit_mask():
    ks = []
    for j in range(33):
        ks.append((1 << (j % 10)) | (1 << ((j * 3 + 1) % 10)) | (1 << ((j * 7 + 5) % 10)))
    gates = np.array([1.0 - parity(ni & k) for k in ks], float)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        p = uu * 32.0
        j = int(np.floor(p))
        fr = sm(p - j)
        row = gates[j] * (1 - fr) + gates[min(j + 1, 32)] * fr
        a = 1.22 - 0.94 * (uu ** 1.4)
        m[i] = (n ** -a) * (0.025 + 0.975 * row)
    return out(m)


# ── 18. TERRA DUST ────────────────────────────────────────────────────────────────────
# FOR: a film-score riser built from nothing but partial count — 6 scattered partials become
# 900, each fading in at its own moment on a fixed random schedule. Sparse and pitched at the
# start, a solid noise-like wall at the end, with no filter movement at all.
def terra_dust():
    rng = np.random.default_rng(0xD0570F)
    K = 900
    pos = np.sort(2.0 ** rng.uniform(0.0, 9.999, K))
    amp = np.abs(rng.normal(0, 1, K)) ** 2.4 + 0.05
    ordr = rng.permutation(K)
    birth = np.empty(K)
    birth[ordr] = (np.arange(K) / K) ** 1.7
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        live = sm((uu - birth) / 0.045)
        sel = live > 1e-4
        if not sel.any():
            sel = np.zeros(K, bool)
            sel[0] = True
            live = np.where(sel, 1.0, 0.0)
        ex = -0.38 + 1.55 * uu
        m[i] = scatter(pos[sel], (amp * live)[sel] * pos[sel] ** -ex)
    return out(m)


# ── 19. TERRA ZIPPER ──────────────────────────────────────────────────────────────────
# FOR: two timbres interleaved in the frequency domain instead of mixed — a dark power law
# and a bright ripple alternate in blocks of harmonics, and the block size shrinks from 128
# down to 3 while the boundaries walk. Chunky spectral banding that turns into fine grain.
def terra_zipper():
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        dark = n ** -(2.35 - 0.95 * uu)
        bright = (n ** -(0.85 - 0.80 * uu)) * (0.25 + 0.75 * (0.5 + 0.5 * np.cos(2 * np.pi * ln / 1.35)) ** 2)
        bs = 128.0 * (3.0 / 128.0) ** sm(uu)
        off = 47.0 * uu
        blk = np.floor((n - 1 + off) / bs).astype(np.int64)
        w = 0.012 + 0.988 * (blk % 2 == 0).astype(float)
        m[i] = dark * (1.0 - w) * 40.0 + bright * w
    return out(m)


# ── 20. TERRA HINGE ───────────────────────────────────────────────────────────────────
# FOR: turning a spectrum inside out — the envelope's peak is a hinge that slides from
# harmonic 1 to harmonic 1024, so the sound goes saw -> a V with a hollow bottom and a
# hollow top -> a perfectly reversed saw that is all air and no fundamental.
def terra_hinge():
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        h = 1.0 + 1023.0 * sm(uu) ** 1.15
        d = np.abs(n - h) + 1.0
        g = d ** -1.18
        g = g * (1.0 + 0.55 * np.cos(2 * np.pi * np.log2(d) / 1.7))
        m[i] = np.maximum(g, 0.0) + 2e-4 * (n ** -1.0)
    return out(m)


# ── 21. TERRA SHEAR ───────────────────────────────────────────────────────────────────
# FOR: getting darker and more detailed at the same time — the broad envelope slides down
# nine octaves while the fine ripple inside it gets finer and travels the other way. Two
# spectral layers shearing past each other; a subtractive patch can only do one of them.
def terra_shear():
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        c = 9.35 - 8.10 * sm(uu)
        w = 3.10 - 0.55 * uu
        env = np.exp(-((ln - c) ** 2) / (2 * w * w))
        rp = 1.05 + 5.6 * uu
        rip = 0.5 + 0.5 * np.cos(2 * np.pi * rp * ln + 5.2 * np.pi * uu)
        m[i] = env * (0.14 + 0.86 * rip ** 2.2) * (n ** -0.10)
    return out(m)


# ── 22. TERRA PARTIAL CHORD ───────────────────────────────────────────────────────────
# FOR: one oscillator playing a chord progression on the frame axis — three harmonic series
# stacked on integer multipliers, the triad walking 4:5:6 (major) -> 10:12:15 (minor) ->
# 4:6:7 -> 6:7:9 -> 8:11:13, so the voicing changes without any detuning or extra voices.
def terra_partial_chord():
    triads = [(4, 5, 6), (10, 12, 15), (4, 6, 7), (6, 7, 9), (8, 11, 13)]
    T = len(triads)
    ser = []
    for tr in triads:
        g = np.zeros(N)
        for mi, mm in enumerate(tr):
            k = np.arange(1, N // mm + 1)
            g[mm * k - 1] += (0.75 ** mi) * k ** -0.55
        ser.append(g)
    ser = np.array(ser)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        p = uu * (T - 1)
        j = min(int(np.floor(p)), T - 2)
        fr = sm(p - j)
        g = ser[j] * (1 - fr) + ser[j + 1] * fr
        a = 1.05 - 0.86 * sm(uu)
        m[i] = g * (n ** -(a - 0.55))
    return out(m)


TABLES = [
    ("TERRA COMB WALK", terra_comb_walk),
    ("TERRA TILT ARC", terra_tilt_arc),
    ("TERRA SHIFT GRID", terra_shift_grid),
    ("TERRA STRETCH", terra_stretch),
    ("TERRA PRIME WINDOW", terra_prime_window),
    ("TERRA FIBONACCI", terra_fibonacci),
    ("TERRA SQUARE LAW", terra_square_law),
    ("TERRA OCTAVE TREE", terra_octave_tree),
    ("TERRA RES DRIFT", terra_res_drift),
    ("TERRA SMEAR", terra_smear),
    ("TERRA SIERPINSKI", terra_sierpinski),
    ("TERRA FRACTAL SHELF", terra_fractal_shelf),
    ("TERRA ODD ROTATE", terra_odd_rotate),
    ("TERRA HOLES", terra_holes),
    ("TERRA SIEVE", terra_sieve),
    ("TERRA MOIRE", terra_moire),
    ("TERRA BIT MASK", terra_bit_mask),
    ("TERRA DUST", terra_dust),
    ("TERRA ZIPPER", terra_zipper),
    ("TERRA HINGE", terra_hinge),
    ("TERRA SHEAR", terra_shear),
    ("TERRA PARTIAL CHORD", terra_partial_chord),
]

if __name__ == "__main__":
    built = []
    for nm, fn in TABLES:
        fr = fn()
        ok, rep = wtlib.selfcheck(nm, fr)
        print(rep)
        built.append((nm, fr))
    fps = [(nm, wtlib.fingerprint(fr)) for nm, fr in built]
    worst = []
    for i in range(len(fps)):
        for j in range(i + 1, len(fps)):
            worst.append((wtlib.distance(fps[i][1], fps[j][1]), fps[i][0], fps[j][0]))
    worst.sort()
    print("\nclosest pairs (dB):")
    for d, a, b in worst[:8]:
        print(f"  {d:6.2f}  {a}  <->  {b}")
    print(f"\nMIN PAIRWISE DISTANCE = {worst[0][0]:.2f} dB   (gate 6.0)")
