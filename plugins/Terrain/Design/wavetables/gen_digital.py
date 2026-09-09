"""
gen_digital.py — TERRAIN factory bank, DIGITAL / FM category (22 tables).

Everything here is computed from first principles: FM operator stacks in several
topologies, operator feedback, prime integer ratios, Casio-style phase distortion,
wavefolding, in-cycle bit/sample-rate reduction, ring modulation, hard sync and
Chebyshev waveshaping. Nothing is sampled, resampled or derived from anyone else's
wavetable content.

Anti-alias policy: every time-domain construction is rendered on an OVERSAMPLED
grid (OS x 2048 points of one exact fundamental period) and then reduced to 2048
samples by discarding harmonics above 1023 in the frequency domain. That is a
truncation, not a naive decimation, so nothing folds back. Tables whose partials
are exact by construction (Chebyshev) are built at base resolution directly.
"""
import sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # fb606
import numpy as np
import wtlib

F   = wtlib.FRAMES          # 128
N   = wtlib.SIZE            # 2048
NH  = wtlib.NH              # 1024
TAU = 2.0 * np.pi

OS   = 8                    # default oversample for time-domain builds
NOS  = N * OS
tos  = np.arange(NOS) / NOS                  # one cycle on the oversampled grid
pos  = TAU * tos                             # its phase
fx   = np.arange(F) / (F - 1.0)              # frame axis, 0..1

OS2  = 16                   # heavier oversample for discontinuous (sync / saw) builds
NOS2 = N * OS2
tos2 = np.arange(NOS2) / NOS2
pos2 = TAU * tos2


# ── helpers ───────────────────────────────────────────────────────────────────────────
def down(w):
    """(F, OS*N) exact-period render -> (F, N), bandlimited to harmonic 1023.

    Frequency-domain truncation: harmonics above the new Nyquist are DISCARDED, so
    nothing aliases back into the audible comb the way a naive decimation would.
    """
    S = np.fft.rfft(np.asarray(w, dtype=float), axis=1)
    out = np.zeros((S.shape[0], N // 2 + 1), dtype=complex)
    out[:, :NH] = S[:, :NH]
    return np.fft.irfft(out, n=N, axis=1)


def ramp(x, a, b):
    """Clipped 0..1 ramp of the frame axis between fractions a and b."""
    return np.clip((x - a) / (b - a), 0.0, 1.0)


def tfold(x):
    """Ideal triangle wavefolder, period 4, exact and vectorised."""
    return (2.0 / np.pi) * np.arcsin(np.sin(np.pi * x / 2.0))


def fbop(phase, beta, extra=0.0, iters=32):
    """Self-modulating (feedback) sine operator, solved by fixed-point iteration.

    y = sin(phase + extra + beta*y). A finite composition of sines, so y is a smooth
    function of beta even past beta=1 where the operator breaks up — the frame morph
    stays continuous while the SOUND tears apart.
    """
    ph = phase + extra
    y = np.zeros(np.broadcast(ph, beta).shape)
    for _ in range(iters):
        y = np.sin(ph + beta * y)
    return y


def xfade_int(vals, build):
    """Crossfade a per-frame CONTINUOUS parameter over an INTEGER-only generator.

    Integer ratios are what make a time-domain build exactly periodic, but stepping
    them frame to frame would click. Blending floor(v) and floor(v)+1 keeps the ratio
    set legal on every frame while the morph glides.
    """
    lo = np.floor(vals).astype(int)
    fr = (vals - lo)[:, None]
    cache = {}
    out = np.zeros((F, build.length))
    for i in range(F):
        for k in (lo[i], lo[i] + 1):
            if k not in cache:
                cache[k] = build(k)
        out[i] = (1.0 - fr[i]) * cache[lo[i]] + fr[i] * cache[lo[i] + 1]
    return out


# ══ 1 ══ TERRA SINE BOMB ══════════════════════════════════════════════════════════════
# PURPOSE: the bank's Lifeguard table — frame 0 is a mathematically pure sine and frame
# 127 is a wall of FM sidebands, so a single macro takes a lead from nothing to violence.
def terra_sine_bomb():
    I = 300.0 * fx ** 1.25
    w = np.sin(pos[None, :] + I[:, None] * np.sin(pos[None, :]))
    return wtlib.finalize(down(w))


# ══ 2 ══ TERRA STACK THREE ════════════════════════════════════════════════════════════
# PURPOSE: a 3-operator SERIES stack (5 -> 2 -> 1) for DX-style electric-piano and bell
# leads whose brightness folds in on itself instead of just opening up.
def terra_stack_three():
    I2 = (7.0 * ramp(fx, 0.0, 0.62) ** 1.3)[:, None]
    I1 = (12.0 * fx ** 0.85)[:, None]
    m3 = np.sin(5.0 * pos)[None, :]
    m2 = np.sin(2.0 * pos[None, :] + I2 * m3)
    w = np.sin(pos[None, :] + I1 * m2)
    return wtlib.finalize(down(w))


# ══ 3 ══ TERRA PARALLEL RAKE ══════════════════════════════════════════════════════════
# PURPOSE: four modulators in PARALLEL on one carrier at square ratios 1/4/9/16, each
# blooming alone at its own point of the axis — a pad whose sideband SPACING rakes
# upward in four hand-offs instead of just getting brighter.
def terra_parallel_rake():
    rats = (1.0, 5.0, 12.0, 23.0)
    amps = (34.0, 23.0, 15.0, 10.0)
    cens = (0.04, 0.36, 0.68, 1.00)
    arg = np.tile(pos, (F, 1)) + (1.5 * fx)[:, None] * np.sin(2.0 * pos)[None, :]
    for r, a, c in zip(rats, amps, cens):
        env = a * np.exp(-((fx - c) / 0.185) ** 2)
        arg = arg + env[:, None] * np.sin(r * pos + 0.31 * r)[None, :]
    return wtlib.finalize(down(np.sin(arg)))


# ══ 4 ══ TERRA BRANCH Y ═══════════════════════════════════════════════════════════════
# PURPOSE: branching topology — one modulator drives two carriers (1 and 2) with OPPOSED
# index curves, so the octave crossfades under you as the table opens. Good for hybrid bass.
def terra_branch_y():
    m = np.sin(3.0 * pos)[None, :]
    Ia = (2.0 + 104.0 * fx)[:, None]
    Ib = (0.5 + 172.0 * fx ** 2.1)[:, None]
    x = (0.35 + 0.62 * fx ** 1.4)[:, None]
    a = np.sin(pos[None, :] + Ia * m)
    b = np.sin(2.0 * pos[None, :] + Ib * m)
    return wtlib.finalize(down((1.0 - x) * a + x * b))


# ══ 5 ══ TERRA FEEDBACK SAW ═══════════════════════════════════════════════════════════
# PURPOSE: one self-modulating operator taken past its stability point — sine, to saw, to
# tearing. The classic feedback-op transition, laid out as a playable frame axis.
def terra_feedback_saw():
    beta = (2.45 * fx ** 1.05)[:, None]
    return wtlib.finalize(down(fbop(pos[None, :], beta, iters=36)))


# ══ 6 ══ TERRA PRIME COMB ═════════════════════════════════════════════════════════════
# PURPOSE: carrier 3 against modulator 11 — a prime-ratio comb that reads as inharmonic
# metal but is perfectly periodic, with a fine 1:1 modulator splitting each comb tooth.
def terra_prime_comb():
    Ia = (74.0 * fx ** 1.15)[:, None]
    Ib = (2.6 * fx ** 0.8)[:, None]
    w = np.sin(3.0 * pos[None, :] + Ia * np.sin(11.0 * pos)[None, :]
               + Ib * np.sin(pos)[None, :])
    return wtlib.finalize(down(w))


# ══ 7 ══ TERRA THIRTEEN ═══════════════════════════════════════════════════════════════
# PURPOSE: a 13:5 lattice on a ratio-1 carrier — two prime combs interleaving into a
# shimmering, faintly detuned-sounding digital choir that never leaves the fundamental.
def terra_thirteen():
    Ia = (46.0 * fx ** 1.3)[:, None]
    Ib = (3.2 * fx ** 0.6)[:, None]
    w = np.sin(pos[None, :] + Ia * np.sin(13.0 * pos)[None, :]
               + Ib * np.sin(5.0 * pos + 1.1)[None, :])
    return wtlib.finalize(down(w))


# ══ 8 ══ TERRA CZ WARP ════════════════════════════════════════════════════════════════
# PURPOSE: Casio CZ phase distortion — the phase read of a cosine is compressed into an
# ever-smaller window, opening a saw from a sine with no modulator at all.
def terra_cz_warp():
    snap = (0.5 * 10.0 ** (-2.85 * fx))[:, None]   # 50% -> 0.07% of the cycle, log law,
    d = 1.0 - snap                                 # so the axis is useful end to end
    T = tos[None, :]
    w = np.where(T < d, 0.5 * T / d, 0.5 + 0.5 * (T - d) / snap)
    return wtlib.finalize(down(-np.cos(TAU * w)))


# ══ 9 ══ TERRA CZ RESO ════════════════════════════════════════════════════════════════
# PURPOSE: the CZ resonant wave — a ramp-windowed sine whose multiple climbs 1 -> 52, a
# hard digital resonance sweep with no filter in the signal path.
def terra_cz_reso():
    R = 1.0 + 95.0 * fx ** 1.25
    lo = np.floor(R).astype(int)
    fr = (R - lo)[:, None]
    win = (1.0 - tos)[None, :]                     # ramp that RESETS at the wrap — the
    a = np.cos(TAU * lo[:, None] * tos[None, :])   # step is what makes CZ resonance bite
    b = np.cos(TAU * (lo + 1)[:, None] * tos[None, :])
    return wtlib.finalize(down(win * ((1.0 - fr) * a + fr * b)))


# ══ 10 ══ TERRA FOLD ENGINE ═══════════════════════════════════════════════════════════
# PURPOSE: a sine driven into an ideal triangle wavefolder at rising gain, with a drifting
# DC offset so the fold pattern goes asymmetric — West-Coast digital brass and stabs.
def terra_fold_engine():
    g = (1.0 + 17.0 * fx ** 1.2)[:, None]
    off = (0.62 * fx ** 1.5)[:, None]
    return wtlib.finalize(down(tfold(g * np.sin(pos)[None, :] + off)))


# ══ 11 ══ TERRA BIT LADDER ════════════════════════════════════════════════════════════
# PURPOSE: a real bit-crusher on one cycle — 4096 levels down to 2, then a 2nd-order error
# feedback loop that throws the quantiser's own residue into the top octave. Lo-fi to hiss.
def terra_bit_ladder():
    # The crusher runs at the TABLE's own rate, so the shaped residue climbs to harmonic
    # 1023 and nothing has to be discarded: a 2048-point cycle cannot alias by definition.
    tq = wtlib.t
    base = (np.sin(TAU * tq) + 0.45 * np.sin(TAU * 2.0 * tq + 1.1)
            + 0.25 * np.sin(TAU * 3.0 * tq))
    base = base / np.abs(base).max()

    L = 2.0 * 2.0 ** (11.0 * (1.0 - ramp(fx, 0.0, 0.70)) ** 1.15)   # 4096 -> 2 levels
    a = ramp(fx, 0.34, 1.0) ** 1.1                                   # error-shaper depth
    c1, c2 = 2.0 * a, a * a                        # NTF = (1 - a z^-1)^2

    out = np.zeros((F, N))
    e1 = np.zeros(F); e2 = np.zeros(F)
    for _pass in range(2):                         # pass 1 warms the loop up cyclically
        for k in range(N):
            v = np.clip(base[k] - c1 * e1 + c2 * e2, -1.0, 1.0)
            q = np.round(v * L) / L
            e = np.clip(q - v, -1.5 / L, 1.5 / L)
            e2, e1 = e1, e
            out[:, k] = q
    return wtlib.finalize(out)


# ══ 12 ══ TERRA DECIMATOR ═════════════════════════════════════════════════════════════
# PURPOSE: sample-RATE reduction inside the cycle — a 2-op tone held at 1024 steps down to
# 3, growing the sinc-image comb of a broken converter. The frequency-axis twin of BIT LADDER.
def terra_decimator():
    steps = 2.0 ** (10.0 - 8.35 * fx ** 0.85)
    lo = np.floor(steps)[:, None]
    fr = (steps - np.floor(steps))[:, None]        # whole step counts are crossfaded, or
    T = tos[None, :]                               # the hold pattern would re-shuffle whole
    def hold(nst):                                 # frames at a time and click
        ph = np.floor(T * nst) / nst
        return np.sin(TAU * ph + 4.0 * np.sin(TAU * 2.0 * ph))
    w = (1.0 - fr) * hold(lo) + fr * hold(lo + 1.0)
    return wtlib.finalize(down(w))


# ══ 13 ══ TERRA RING TOWER ════════════════════════════════════════════════════════════
# PURPOSE: ring modulation between two operators — an all-even FM comb multiplied by a
# partner climbing 1 -> 46, so the comb flips parity on every integer. Clangour and bells.
def terra_ring_tower():
    # The operator is built ENTIRELY on even ratios (2, 6, 10), so its comb has spacing 2.
    # Ringing it against a partner that climbs through the integers flips that comb between
    # even and odd harmonics as R passes each whole number — a parity shimmer nothing else has.
    R = 1.0 + 45.0 * fx ** 0.75
    lo = np.floor(R).astype(int)
    fr = (R - lo)[:, None]
    I1 = (0.4 + 26.0 * fx ** 1.7)[:, None]
    I2 = (13.0 * fx ** 2.2)[:, None]
    op = np.sin(2.0 * pos[None, :] + I1 * np.sin(6.0 * pos)[None, :]
                + I2 * np.sin(10.0 * pos + 0.4)[None, :])
    ring = ((1.0 - fr) * np.sin(TAU * lo[:, None] * tos[None, :] + 0.7)
            + fr * np.sin(TAU * (lo + 1)[:, None] * tos[None, :] + 0.7))
    return wtlib.finalize(down(op * ring))


# ══ 14 ══ TERRA HARD SYNC ═════════════════════════════════════════════════════════════
# PURPOSE: textbook hard sync — a saw slave reset by the master every cycle, ratio 1 -> 17.
# The frame axis IS the sync knob, so a mod source gives you the classic sync sweep for free.
def terra_hard_sync():
    r = (1.0 + 16.0 * fx ** 1.15)[:, None]
    ph = np.mod(r * tos2[None, :], 1.0)
    return wtlib.finalize(down(2.0 * ph - 1.0))


# ══ 15 ══ TERRA GONG OP ═══════════════════════════════════════════════════════════════
# PURPOSE: carrier 5 against modulator 7 — a metallic 2-op with NO fundamental at rest, so
# it starts as a thin harmonic and grows a gong around itself. Percussive digital mallet.
def terra_gong_op():
    I = (30.0 * fx ** 1.2)[:, None]
    J = (4.4 * fx ** 0.7)[:, None]
    w = np.sin(5.0 * pos[None, :] + I * np.sin(7.0 * pos)[None, :]
               + J * np.sin(3.0 * pos + 2.2)[None, :])
    return wtlib.finalize(down(w))


# ══ 16 ══ TERRA XOR DUST ══════════════════════════════════════════════════════════════
# PURPOSE: pure integer arithmetic — the cycle's sample index XORed against a shifted copy
# of itself, marching through twelve coprime multipliers. Ramp at frame 0, IDM grit at 127.
def terra_xor_dust():
    ki = (tos * 4096.0).astype(np.int64)
    sets = [(1, 12), (1, 10), (3, 9), (5, 8), (7, 7), (11, 6),
            (13, 5), (17, 4), (23, 3), (29, 3), (37, 2), (53, 2)]

    def build(j):
        j = int(np.clip(j, 0, len(sets) - 1))
        A, S = sets[j]
        v = ((ki * A) ^ (ki >> S)) & 4095
        return v.astype(float) / 2048.0 - 1.0

    build.length = NOS
    p = fx * (len(sets) - 1)
    return wtlib.finalize(down(xfade_int(p, build)))


# ══ 17 ══ TERRA PD PULSE ══════════════════════════════════════════════════════════════
# PURPOSE: the CZ pulse — the phase races through a whole cosine in the first k of the
# cycle then HOLDS. A pulse whose width is the frame axis, but with no rectangular edges.
def terra_pd_pulse():
    k = (10.0 ** (-2.6 * fx ** 1.1))[:, None]      # 1.0 -> 0.0025 of a cycle
    w = np.minimum(tos[None, :] / k, 1.0)
    return wtlib.finalize(down(np.cos(TAU * w)))


# ══ 18 ══ TERRA DOUBLE FB ═════════════════════════════════════════════════════════════
# PURPOSE: two feedback operators in series — a self-modulating op at ratio 3 driving a
# carrier that ALSO feeds back on itself. Two tearing points on one axis, for aggressive leads.
def terra_double_fb():
    b1 = (2.0 * ramp(fx, 0.0, 0.72) ** 1.1)[:, None]
    b2 = (1.15 * ramp(fx, 0.42, 1.0) ** 1.4)[:, None]
    I = (16.0 * fx ** 0.95)[:, None]
    u = fbop(3.0 * pos[None, :], b1, iters=28)
    return wtlib.finalize(down(fbop(pos[None, :], b2, extra=I * u, iters=28)))


# ══ 19 ══ TERRA SYNC SINE ═════════════════════════════════════════════════════════════
# PURPOSE: a SINE slave hard-synced and simultaneously FM'd — the sync edge stays a single
# step instead of a saw ramp, so it reads glassy and vocal-free rather than buzzy.
def terra_sync_sine():
    r = (1.0 + 17.0 * fx ** 1.25)[:, None]
    I = (6.5 * fx ** 1.5)[:, None]
    ph = np.mod(r * tos2[None, :], 1.0)
    return wtlib.finalize(down(np.sin(TAU * ph + I * np.sin(pos2)[None, :])))


# ══ 20 ══ TERRA CHEBY STACK ═══════════════════════════════════════════════════════════
# PURPOSE: Chebyshev waveshaping — T_n of a sine is EXACTLY harmonic n, so a Gaussian
# weight window sliding up the polynomial order drags a hard-edged harmonic band up the
# spectrum. Nothing else in the bank has square shoulders like this.
def terra_cheby_stack():
    NMAX = 400
    th = TAU * wtlib.t
    x, sn = np.cos(th), np.sin(th)
    # T_n(cos t) = cos(n t) and sin(t)*U_(n-1)(cos t) = sin(n t): the two Chebyshev kinds
    # give the quadrature pair, so the band can carry wtlib.PHASE instead of stacking every
    # order at the same instant (which spikes the crest factor to ~12 and nothing else).
    T = np.zeros((NMAX + 1, N)); T[0] = 1.0; T[1] = x
    U = np.zeros((NMAX + 1, N)); U[0] = 1.0; U[1] = 2.0 * x
    for n in range(2, NMAX + 1):
        T[n] = 2.0 * x * T[n - 1] - T[n - 2]
        U[n] = 2.0 * x * U[n - 1] - U[n - 2]
    ph = wtlib.PHASE[:NMAX]
    basis = (np.cos(ph)[:, None] * T[1:]) - (np.sin(ph)[:, None] * (sn[None, :] * U[:NMAX]))
    C = 2.0 + 150.0 * fx ** 1.15
    W = 1.0 + 78.0 * fx ** 1.45
    n_ax = np.arange(1, NMAX + 1)[None, :]
    wgt = np.exp(-((n_ax - C[:, None]) / W[:, None]) ** 2)
    wgt /= wgt.sum(axis=1, keepdims=True)
    return wtlib.finalize(wgt @ basis)


# ══ 21 ══ TERRA WARP FOLD ═════════════════════════════════════════════════════════════
# PURPOSE: a power-law phase warp feeding a wavefolder — two digital distortions stacked,
# so the fold notches slide as the warp compresses. Snarling digital reed / bass tone.
def terra_warp_fold():
    p = (1.0 - 0.82 * fx ** 1.1)[:, None]
    g = (1.0 + 11.0 * fx ** 1.25)[:, None]
    h = (1.0 + 4.0 * ramp(fx, 0.35, 1.0) ** 1.6)[:, None]
    T = tos[None, :]
    u = np.where(T < 0.5, 0.5 * (2.0 * T) ** p, 1.0 - 0.5 * (2.0 - 2.0 * T) ** p)
    w = np.sin(TAU * u)
    return wtlib.finalize(down(tfold(h * tfold(g * w))))


# ══ 22 ══ TERRA OVERLOAD ══════════════════════════════════════════════════════════════
# PURPOSE: the four-stage destruction table — FM index, then operator feedback, then hard
# sync, then bit-crush and decimation, each stage handed over in turn across the axis.
def terra_overload():
    I = (11.0 * ramp(fx, 0.00, 0.42) ** 1.2)[:, None]
    b = (1.75 * ramp(fx, 0.30, 0.64))[:, None]
    r = (1.0 + 8.0 * ramp(fx, 0.52, 0.86) ** 1.25)[:, None]
    L = (512.0 * (1.0 - 0.994 * ramp(fx, 0.70, 1.0) ** 1.3))[:, None]
    D = (2048.0 * (1.0 - 0.9955 * ramp(fx, 0.78, 1.0) ** 1.2))[:, None]

    ph = np.mod(r * tos2[None, :], 1.0)                    # stage 3: hard sync
    y = fbop(TAU * ph, b, extra=I * np.sin(TAU * 3.0 * ph), iters=26)   # stages 1+2
    y = np.round(y * L) / L                                # stage 4a: bit crush
    idx = np.floor(tos2[None, :] * D) / D                  # stage 4b: decimate
    pick = np.clip((idx * NOS2).astype(int), 0, NOS2 - 1)
    y = np.take_along_axis(y, pick, axis=1)
    return wtlib.finalize(down(y))


TABLES = [
    ("TERRA SINE BOMB",     terra_sine_bomb),
    ("TERRA STACK THREE",   terra_stack_three),
    ("TERRA PARALLEL RAKE", terra_parallel_rake),
    ("TERRA BRANCH Y",      terra_branch_y),
    ("TERRA FEEDBACK SAW",  terra_feedback_saw),
    ("TERRA PRIME COMB",    terra_prime_comb),
    ("TERRA THIRTEEN",      terra_thirteen),
    ("TERRA CZ WARP",       terra_cz_warp),
    ("TERRA CZ RESO",       terra_cz_reso),
    ("TERRA FOLD ENGINE",   terra_fold_engine),
    ("TERRA BIT LADDER",    terra_bit_ladder),
    ("TERRA DECIMATOR",     terra_decimator),
    ("TERRA RING TOWER",    terra_ring_tower),
    ("TERRA HARD SYNC",     terra_hard_sync),
    ("TERRA GONG OP",       terra_gong_op),
    ("TERRA XOR DUST",      terra_xor_dust),
    ("TERRA PD PULSE",      terra_pd_pulse),
    ("TERRA DOUBLE FB",     terra_double_fb),
    ("TERRA SYNC SINE",     terra_sync_sine),
    ("TERRA CHEBY STACK",   terra_cheby_stack),
    ("TERRA WARP FOLD",     terra_warp_fold),
    ("TERRA OVERLOAD",      terra_overload),
]

# ══════════════════════════════════════════════════════════════════════════════════════
# fb606 — THE MERGED TEN. The bank shipped as eight generated folders; the browser showed a
# DIFFERENT set for its built-ins, so the same sound had two names depending on where you
# looked. The two sets are now ONE ten-folder taxonomy, and every module declares which of
# the ten each of its tables belongs to. gate.py reads CATEGORY and never guesses from the
# module name — a table with no entry here is a hard error, not a silent "GEN_WHATEVER".
# ══════════════════════════════════════════════════════════════════════════════════════

# FM, phase distortion, bit/rate reduction, sync, ring mod — the whole module is one folder.
CATEGORY = {nm: "Digital" for nm, _ in TABLES}


if __name__ == "__main__":
    built = []
    for n, f in TABLES:
        fr = f()
        print(wtlib.selfcheck(n, fr)[1], flush=True)
        built.append((n, fr))

    fps = [(n, wtlib.fingerprint(fr)) for n, fr in built]
    worst, pairs = 1e9, []
    for i in range(len(fps)):
        for j in range(i + 1, len(fps)):
            d = wtlib.distance(fps[i][1], fps[j][1])
            pairs.append((d, fps[i][0], fps[j][0]))
            worst = min(worst, d)
    pairs.sort()
    print("\n-- closest pairs --")
    for d, a, b in pairs[:8]:
        print(f"   {d:6.2f} dB   {a} <-> {b}")
    print(f"\nMIN PAIRWISE DISTANCE (digital set) = {worst:.2f} dB")
