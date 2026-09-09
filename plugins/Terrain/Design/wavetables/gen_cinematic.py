"""
gen_cinematic.py — TERRA factory bank: CINEMATIC / DRONE (12) + METALLIC / INDUSTRIAL (12).

Everything is additive and computed from first principles: spectral envelopes designed as
functions of harmonic number, and explicit partial-ratio ladders (stiff-bar, bell, plate,
ring-mod sum/difference, geometric/irrational) PROJECTED onto the integer harmonic grid so
the clang survives while the frame stays periodic.

Design order for every table: the JOURNEY of the frame axis first, the timbre second.
"""
import sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # fb606
import numpy as np, wtlib

F, SZ, NH = wtlib.FRAMES, wtlib.SIZE, wtlib.NH
NA  = wtlib.n_ax.astype(float)          # 1..1024
NN  = NA[None, :]
U   = np.linspace(0.0, 1.0, F)          # frame parameter
UU  = U[:, None]
ODD = (wtlib.n_ax % 2 == 1)
EVN = ~ODD
PHI = (1.0 + 5.0 ** 0.5) / 2.0


# ── small shape kit ───────────────────────────────────────────────────────────────────
def sm(x):
    x = np.clip(np.asarray(x, float), 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)

def geo(a, b, x):
    return a * (b / a) ** np.asarray(x, float)

def col(v):
    return np.asarray(v, float).reshape(-1, 1)

def lp(fc, q):
    return (1.0 + (NN / col(fc)) ** 2) ** (-col(q))

def hp(fc, s=3.0):
    return 1.0 / (1.0 + (col(fc) / NN) ** s)

def glog(c, w):
    return np.exp(-0.5 * ((np.log(NN) - np.log(col(c))) / col(w)) ** 2)

def blank():
    return np.zeros((F, NH))

def put(m, i, idx, amp):
    idx = np.asarray(idx)
    if idx.size == 0:
        return
    amp = np.broadcast_to(np.asarray(amp, float), idx.shape)
    ok = (idx >= 1) & (idx <= NH)
    if not ok.any():
        return
    np.add.at(m[i], idx[ok].astype(np.intp) - 1, amp[ok])

def putf(m, i, x, amp):
    x = np.asarray(x, float)
    if x.size == 0:
        return
    amp = np.broadcast_to(np.asarray(amp, float), x.shape)
    lo = np.floor(x)
    fr = x - lo
    put(m, i, lo.astype(int), amp * (1.0 - fr))
    put(m, i, lo.astype(int) + 1, amp * fr)


def done(m):
    return wtlib.finalize(wtlib.cycles_from_mags(m))


# ══════════════════════════════════════════════════════════════════════════════════════
#  CINEMATIC / DRONE
# ══════════════════════════════════════════════════════════════════════════════════════

def terra_void_bloom():
    """Near-sine to enormous: the master swell for a cue that goes from nothing to everything."""
    s  = sm(U)
    m  = lp(geo(1.9, 190.0, s), 1.80 - 1.05 * s)
    m  = m * (NN ** col(-0.10 * (1.0 - s)))
    m  = m * np.exp(-(NN / col(geo(9.0, 360.0, s))) ** 2.0)
    m[:, EVN] *= (0.30 + 0.70 * s[:, None])
    return done(m)


def terra_choral_dawn():
    """One voice becomes a massed choir: rank after rank enters and every partial widens to a cluster."""
    m = blank()
    bases = [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48]
    ents  = [-0.30, 0.02, 0.11, 0.20, 0.29, 0.38, 0.47, 0.56, 0.65, 0.75, 0.85]
    k = np.arange(1, 300)
    for v, b in enumerate(bases):
        g = sm(np.clip((U - ents[v]) / 0.17, 0.0, 1.0)) * (0.30 + 1.05 * v / 10.0)
        if v < 3:
            g = g * (1.0 - 0.80 * sm(np.clip((U - 0.35) / 0.45, 0.0, 1.0)))
        pos = b * k
        amp = k ** -1.45 * np.exp(-(pos / 950.0) ** 3)
        for i in range(F):
            if g[i] <= 1e-4:
                continue
            w = 1.0 + 8.0 * sm(U[i]) * (0.30 + 0.70 * (v / len(bases)))
            for off, wt in ((0, 1.0), (-1, 0.42), (1, 0.42), (-2, 0.18), (2, 0.18)):
                putf(m, i, pos + off * w, g[i] * wt * amp)
    m *= lp(geo(7.0, 620.0, sm(U)), np.full(F, 0.85))
    return done(m)


def terra_glass_edge():
    """Sweet glassy pad that slowly grows teeth — tension cues that must not change note."""
    bumps = 0.020
    for c, g, w in [(1.0, 1.00, 0.16), (2.0, 0.62, 0.14), (3.0, 0.40, 0.13),
                    (5.0, 0.30, 0.15), (9.0, 0.46, 0.17), (14.0, 0.20, 0.14),
                    (23.0, 0.24, 0.16), (37.0, 0.10, 0.15)]:
        bumps = bumps + g * np.exp(-0.5 * ((np.log(NA) - np.log(c)) / w) ** 2)
    body = bumps[None, :] * lp(np.full(F, 60.0), np.full(F, 1.10)) * col(1.0 - 0.55 * sm(U))
    nc   = geo(26.0, 470.0, sm(U))
    gain = 10.0 ** ((-72.0 + 82.0 * sm(U)) / 20.0)
    edge = hp(np.full(F, 19.0), 3.0) * (NN ** -0.30) * np.exp(-(NN / col(nc)) ** 4)
    return done(body + col(gain) * edge)


def terra_sub_drift():
    """Sub-heavy bed whose upper structure drifts in and out like fog over the low end."""
    floor = (NN ** -2.30) * lp(np.full(F, 130.0), np.full(F, 0.55))
    d = 0.5 - 0.5 * np.cos(2.0 * np.pi * 0.85 * U)
    c = geo(6.0, 560.0, d)
    w = 0.85 + 0.55 * d
    band = 0.30 * glog(c, w * 0.72) * (NN ** -0.62) * np.exp(-(NN / 780.0) ** 4)
    return done(floor + band)


def terra_breath():
    """A drone that inhales and exhales — two counter-moving bands over a fixed sub. Living tension bed."""
    p  = 0.5 - 0.5 * np.cos(2.0 * np.pi * 1.15 * U)
    b1 = 0.55 * glog(geo(4.5, 620.0, p), np.full(F, 0.36)) * np.exp(-(NN / 620.0) ** 3)
    b2 = 0.34 * glog(geo(760.0, 11.0, p), np.full(F, 0.32)) * np.exp(-(NN / 880.0) ** 3)
    sub = 0.85 * (NN ** -3.10)
    haze = 0.0022 * (NN ** -0.35) * lp(np.full(F, 420.0), np.full(F, 1.4))
    return done(sub + b1 + b2 + haze)


def terra_ash_fall():
    """Bright to dark: sweep it forward and a held note feels like it is burning out."""
    s  = sm(U)
    m  = lp(geo(420.0, 2.4, s), 0.55 + 1.55 * s)
    m *= (1.0 + 1.10 * (1.0 - s)[:, None] * hp(geo(40.0, 300.0, s), 2.2))
    m *= (1.0 - 0.45 * s[:, None] * (0.5 + 0.5 * np.cos(2.0 * np.pi * np.log2(NN) * 1.5)))
    return done(m)


def terra_shimmer_ladder():
    """Octave rungs filling in — the slow shimmer riser that never changes pitch."""
    m = blank()
    odds = np.arange(1, 128, 2)
    for i in range(F):
        s   = sm(U[i])
        lim = 1.0 + 63.0 * s ** 1.45
        for j, o in enumerate(odds):
            g = float(np.clip(lim - j, 0.0, 1.0))
            if g <= 0.0:
                break
            kk = np.arange(0, 11)
            pos = o * (2 ** kk)
            amp = g * (o ** -1.10) * (2.0 ** (-(0.95 - 0.62 * s) * kk))
            put(m, i, pos, amp)
    return done(m)


def terra_tide():
    """Ocean bed: two layers beat against each other and the interference tightens as the tide comes in."""
    bed = (NN ** col(-1.60 + 1.25 * sm(U))) * np.exp(-(NN / col(geo(26.0, 430.0, sm(U)))) ** 3)
    P   = geo(15.0, 5.0, sm(U))
    comb = 0.5 + 0.5 * np.cos(2.0 * np.pi * NN / col(P))
    dep = 0.98 - 0.30 * sm(U)
    return done(bed * ((1.0 - dep)[:, None] + col(dep) * comb))


def terra_cathedral():
    """Stopped pipes opening into full organ — grand sacred beds, the sweep is the registration."""
    m = blank()
    body = (NN ** -1.10) * lp(geo(55.0, 860.0, sm(U)), np.full(F, 0.85))
    m += body * ODD[None, :]
    m += body * EVN[None, :] * (0.02 + 0.90 * sm(U) ** 1.4)[:, None]
    for b, ent, g in [(3, 0.28, 0.55), (4, 0.44, 0.46), (6, 0.58, 0.38),
                      (8, 0.71, 0.31), (12, 0.84, 0.25)]:
        gate = sm(np.clip((U - ent) / 0.20, 0.0, 1.0))
        j = np.arange(1, NH // b + 1)
        pos = j * b
        amp = g * (j ** -0.95) * np.exp(-(pos / 900.0) ** 3)
        for i in range(F):
            if gate[i] > 1e-4:
                put(m, i, pos, gate[i] * amp)
    return done(m)


def terra_ember():
    """A single glowing resonance climbing off a steep dark floor — a filter sweep baked into the table."""
    c  = geo(2.4, 360.0, sm(U))
    w  = 0.30 - 0.09 * U
    m  = blank() + 0.85 * (NN ** -2.90)
    m += glog(c, w) + 0.22 * glog(c, w * 3.2)
    m[:, 0] += 0.60
    return done(m)


def terra_nebula():
    """A diffuse cloud that condenses into a pitched pad — noise-bed to note transitions."""
    cloud = np.zeros(NH)
    for c, g in [(3.1, 1.00), (7.9, 0.86), (19.4, 0.76), (47.0, 0.68),
                 (113.0, 0.58), (271.0, 0.48), (640.0, 0.40)]:
        cloud += g * np.exp(-0.5 * ((np.log(NA) - np.log(c)) / 0.52) ** 2)
    cloud = cloud / cloud.max() + 0.010
    m  = cloud[None, :] ** col(0.95 + 2.60 * sm(U))
    m *= lp(geo(210.0, 22.0, sm(U)), np.full(F, 1.15))
    m += 0.09 * (1.0 - sm(U))[:, None] * (NN ** -0.42) * lp(np.full(F, 460.0), np.full(F, 1.3))
    return done(m)


def terra_horizon():
    """The bed lifts off: the fundamental dissolves and the whole spectrum climbs into the ceiling."""
    lo = geo(1.0, 200.0, sm(U))
    hi = geo(9.5, 1010.0, sm(U))
    m  = hp(lo, 5.0) * (1.0 + (NN / col(hi)) ** 2) ** -1.55 * (NN ** -0.30)
    return done(m)


# ══════════════════════════════════════════════════════════════════════════════════════
#  METALLIC / INDUSTRIAL
# ══════════════════════════════════════════════════════════════════════════════════════

def terra_stiff_bar():
    """Stiff-bar inharmonicity from string-like to steel-xylophone clang — struck metal leads."""
    m = blank()
    k = np.arange(1, 185)
    B = geo(1.5e-6, 1.35e-3, sm(U))
    strike = 0.16 + 1.00 * np.exp(-0.5 * ((np.log(NA) - np.log(58.0)) / 0.85) ** 2) \
                  + 0.34 * np.exp(-0.5 * ((np.log(NA) - np.log(420.0)) / 0.55) ** 2)
    for i in range(F):
        s = sm(U[i])
        x = k * np.sqrt(1.0 + B[i] * k * k)
        amp = k ** -(1.25 - 0.62 * s) * np.exp(-(k / 176.0) ** 5)
        putf(m, i, x, amp)
    m *= strike[None, :]
    return done(m)


def terra_bell_array():
    """Tolling bell: hum/prime/tierce alone, opening to the full inharmonic mode set — cathedral hits."""
    m = blank()
    low = np.array([0.5, 1.0, 1.183, 1.506, 2.0, 2.514, 2.662, 3.011,
                    3.462, 4.166, 5.433, 6.796])
    j = np.arange(13, 300)
    a = 6.796 / (12.0 ** 1.30)
    ratios = np.concatenate([low, a * j ** 1.30])
    base = 2.0
    pos = np.round(base * ratios).astype(int)
    idx = np.arange(1, len(ratios) + 1, dtype=float)
    pos2 = np.round(base * ratios * 1.0135).astype(int)
    for i in range(F):
        s = sm(U[i])
        tilt = 1.95 - 1.42 * s
        amp = (base * ratios) ** -tilt * np.exp(-(idx / 288.0) ** 4)
        amp[:12] *= (1.0 + 2.2 * (1.0 - s))
        put(m, i, pos, amp)
        put(m, i, pos2, amp * (0.30 + 0.45 * s))
    return done(m)


def terra_ringmod_anvil():
    """Ring-modulated stack sliding from locked to wildly inharmonic — mechanical stabs and hard leads."""
    m = blank()
    c = np.arange(1, 49) * 6.0
    Mb = geo(3.0, 42.0, sm(U))
    for i in range(F):
        ca = np.arange(1, 49) ** -(1.35 - 0.65 * sm(U[i]))
        put(m, i, np.round(c).astype(int), ca * 0.9)
        for l in range(1, 6):
            mm = l * Mb[i]
            g = l ** -1.30 * 0.75
            for sgn in (1.0, -1.0):
                putf(m, i, np.abs(c + sgn * mm), ca * g)
    m *= lp(np.full(F, 950.0), np.full(F, 1.0))
    return done(m)


def terra_gong_wash():
    """Plate-mode wash blooming upward — huge inharmonic dread for impacts and drops."""
    m = blank()
    j = np.arange(1, 190, dtype=float)
    p = 1.00 + 0.30 * sm(U)
    tl = 1.45 - 1.05 * sm(U)
    for i in range(F):
        for scale, wgt, off in ((1.60, 1.0, 0.0), (2.19, 0.62, 0.5), (3.07, 0.40, 0.25)):
            x = scale * (j + off) ** p[i]
            amp = wgt * np.maximum(x, 1.0) ** -tl[i] * np.exp(-(j / 178.0) ** 5)
            putf(m, i, x, amp)
    return done(m)


def terra_spring_tension():
    """A spring pulled tight: dispersion collapses toward a harmonic series and the chirp ripple tightens."""
    m = blank()
    k = np.arange(1, 210)
    B = geo(1.1e-3, 2.0e-6, sm(U))
    for i in range(F):
        s = sm(U[i])
        st = k * np.sqrt(1.0 + B[i] * k * k)
        rip = 0.05 + 0.95 * np.cos(np.pi * st / (11.0 - 6.0 * s) + 0.4) ** 6
        amp = k ** -(0.55 + 0.85 * s) * rip * np.exp(-(k / 200.0) ** 5)
        putf(m, i, st, amp)
    return done(m)


def terra_turbine():
    """Blade-passing order climbing over a growl floor — jet, turbine and vehicle beds."""
    m = blank()
    bo = geo(2.2, 24.0, sm(U))
    for i in range(F):
        b0 = int(np.floor(bo[i])); fr = bo[i] - b0
        s = sm(U[i])
        for b, g in ((b0, 1.0 - fr), (b0 + 1, fr)):
            if g <= 1e-4 or b < 1:
                continue
            j = np.arange(1, NH // b + 1)
            base = j * b
            a = g * base ** -(1.55 - 1.05 * s) * np.exp(-(base / (110.0 * (1.0 + 8.0 * s))) ** 3)
            put(m, i, base, a)
            for sb in (1, 2, 3):
                w = 0.55 ** sb * (0.22 + 0.78 * s)
                put(m, i, base + sb, a * w)
                put(m, i, base - sb, a * w)
    m += (0.006 + 0.026 * sm(U))[:, None] * (NN ** -0.85) * lp(geo(30.0, 900.0, sm(U)), np.full(F, 1.30))
    return done(m)


def terra_pipe_resonant():
    """Stopped metal pipe shortening: three resonances climb together while it starts to overblow."""
    p = geo(3.2, 110.0, sm(U))
    series = (NN ** -0.85) * (ODD[None, :] + EVN[None, :] * (0.03 + 0.72 * sm(U))[:, None])
    res = 0.09 + 1.00 * glog(p, np.full(F, 0.26)) \
              + 0.70 * glog(2.76 * p, np.full(F, 0.24)) \
              + 0.50 * glog(5.10 * p, np.full(F, 0.22))
    return done(series * res * lp(np.full(F, 980.0), np.full(F, 0.9)))


def terra_sheet_rattle():
    """Sheet metal roaring: the rattle groups shrink from slabs to a fizz as the tilt goes bright."""
    G = geo(52.0, 7.0, sm(U))
    r = 0.5 + 0.5 * np.tanh(2.2 * np.sin(2.0 * np.pi * NN / (2.0 * col(G))))
    tilt = NN ** col(-0.95 + 1.15 * sm(U))
    m = (0.05 + 0.95 * r) * tilt * lp(geo(130.0, 900.0, sm(U)), np.full(F, 1.60))
    return done(m)


def terra_cluster_forge():
    """Detuned clusters widening until they smear into a continuum — brutal beating industrial chords."""
    m = blank()
    S = 9
    for i in range(F):
        s = sm(U[i])
        kmax = int(7 + 105 * s)
        k = np.arange(1, kmax + 1)
        p0 = k * S
        w = 1.0 + 3.4 * s
        amp = k ** -(1.30 - 0.80 * s) * (0.72 + 0.28 * np.cos(0.7 * k))
        for off, wt in ((0, 1.0), (-1, 0.80), (1, 0.80), (-2, 0.46), (2, 0.46)):
            putf(m, i, p0 + off * w, amp * wt)
    m *= lp(np.full(F, 990.0), np.full(F, 0.9))
    return done(m)


def terra_golden_clang():
    """Three irrational ladders (phi, root-two, e) climbing the grid — alien clang with no repeating interval."""
    m = blank()
    step = geo(0.62, 0.032, sm(U))
    top  = geo(16.0, 1024.0, sm(U))
    for i in range(F):
        for base, rat, g in ((2.0, PHI, 1.0), (2.7, 2.0 ** 0.5, 0.72),
                             (4.3, np.e, 0.55), (6.1, 3.0 ** 0.5, 0.44)):
            r = np.log(rat) * step[i]
            jm = int(np.log(top[i] / base) / r) + 1
            if jm < 2:
                jm = 2
            j = np.arange(0, jm)
            pos = np.round(base * np.exp(r * j)).astype(int)
            pos, ui = np.unique(pos, return_index=True)
            amp = g * np.maximum(pos, 1) ** -(1.30 - 1.15 * sm(U[i])) * np.exp(-(pos / (top[i] * 1.15)) ** 6)
            put(m, i, pos, amp)
    return done(m)


def terra_hammer_mill():
    """A mill slowing down: a sparse bright machine buzz thickening into a full dark roar."""
    m = blank()
    d = geo(17.0, 1.0, sm(U))
    for i in range(F):
        s = sm(U[i])
        tl = 0.30 + 0.75 * s
        d0 = int(np.floor(d[i])); fr = d[i] - d0
        for dd, g in ((d0, 1.0 - fr), (d0 + 1, fr)):
            if g <= 1e-4 or dd < 1:
                continue
            j = np.arange(1, NH // dd + 1)
            pos = j * dd
            put(m, i, pos, g * pos ** -tl * np.exp(-(pos / 980.0) ** 6))
    return done(m)


def terra_iron_lung():
    """A machine that breathes: fixed box modes under a bright band that pumps twice — factory horror beds."""
    m = blank()
    a, b, c = np.meshgrid(np.arange(0, 10), np.arange(0, 10), np.arange(0, 10), indexing='ij')
    r = np.sqrt(a ** 2 + (1.37 * b) ** 2 + (1.93 * c) ** 2).ravel()
    r = r[r > 0.0]
    r = np.sort(r / r.min())
    pos = np.round(3.0 * r).astype(int)
    amp = 0.9 * np.maximum(pos, 1) ** -1.10
    ph = (U * 2.0) % 1.0
    pump = np.where(ph < 0.30, sm(ph / 0.30), 1.0 - sm((ph - 0.30) / 0.70))
    cen = geo(38.0, 820.0, pump)
    band = 0.75 * glog(cen, np.full(F, 0.62)) * (NN ** -0.35) * np.exp(-(NN / 900.0) ** 4)
    for i in range(F):
        put(m, i, pos, amp)
    m += band
    m *= (1.0 + 1.30 * UU * ODD[None, :])
    return done(m)


TABLES = [
    ("TERRA VOID BLOOM",      terra_void_bloom),
    ("TERRA CHORAL DAWN",     terra_choral_dawn),
    ("TERRA GLASS EDGE",      terra_glass_edge),
    ("TERRA SUB DRIFT",       terra_sub_drift),
    ("TERRA BREATH",          terra_breath),
    ("TERRA ASH FALL",        terra_ash_fall),
    ("TERRA SHIMMER LADDER",  terra_shimmer_ladder),
    ("TERRA TIDE",            terra_tide),
    ("TERRA CATHEDRAL",       terra_cathedral),
    ("TERRA EMBER",           terra_ember),
    ("TERRA NEBULA",          terra_nebula),
    ("TERRA HORIZON",         terra_horizon),
    ("TERRA STIFF BAR",       terra_stiff_bar),
    ("TERRA BELL ARRAY",      terra_bell_array),
    ("TERRA RINGMOD ANVIL",   terra_ringmod_anvil),
    ("TERRA GONG WASH",       terra_gong_wash),
    ("TERRA SPRING TENSION",  terra_spring_tension),
    ("TERRA TURBINE",         terra_turbine),
    ("TERRA PIPE RESONANT",   terra_pipe_resonant),
    ("TERRA SHEET RATTLE",    terra_sheet_rattle),
    ("TERRA CLUSTER FORGE",   terra_cluster_forge),
    ("TERRA GOLDEN CLANG",    terra_golden_clang),
    ("TERRA HAMMER MILL",     terra_hammer_mill),
    ("TERRA IRON LUNG",       terra_iron_lung),
]

# ══════════════════════════════════════════════════════════════════════════════════════
# fb606 — THE MERGED TEN. The bank shipped as eight generated folders; the browser showed a
# DIFFERENT set for its built-ins, so the same sound had two names depending on where you
# looked. The two sets are now ONE ten-folder taxonomy, and every module declares which of
# the ten each of its tables belongs to. gate.py reads CATEGORY and never guesses from the
# module name — a table with no entry here is a hard error, not a silent "GEN_WHATEVER".
# ══════════════════════════════════════════════════════════════════════════════════════

# This module was always two banks in one file (see the section banners above): twelve
# atmospheric drones and twelve struck/rung metal tables. The merged taxonomy has a folder
# for each, so the split that was already in the source is now the split on disk.
DRONE = {nm for nm, _ in TABLES[:12]}
CATEGORY = {nm: ("Cinematic" if nm in DRONE else "Metallic") for nm, _ in TABLES}


if __name__ == "__main__":
    built, fps = [], []
    for n, fn in TABLES:
        fr = fn()
        built.append((n, fr))
        fps.append(wtlib.fingerprint(fr))
        print(wtlib.selfcheck(n, fr)[1])
    pairs = []
    for i in range(len(built)):
        for j in range(i + 1, len(built)):
            pairs.append((wtlib.distance(fps[i], fps[j]), built[i][0], built[j][0]))
    pairs.sort()
    print("\n-- closest pairs --")
    for dd, x, y in pairs[:8]:
        print(f"   {dd:6.2f} dB   {x} | {y}")
    print(f"\nMIN PAIRWISE DISTANCE {pairs[0][0]:.2f} dB  ({pairs[0][1]} vs {pairs[0][2]})")
