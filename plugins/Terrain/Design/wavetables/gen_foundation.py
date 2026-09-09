"""
gen_foundation.py — TERRAIN factory bank, FOUNDATION (12) + ANALOG (14).

FOUNDATION = the known quantities: the shapes a user reaches for when they want a
sine, a saw, a pulse, an organ. Simple is allowed here, boring is not: every table
still travels on the frame axis.

ANALOG = the vintage-hardware lane: drift, detune, PWM, sync, ladder resonance,
diode and iron saturation, tape wow, divider stacks, brownout and starving VCO
cores. Everything is computed from first principles — band-limited additive series,
integer/continuous sync on an exact single cycle, and honest waveshaping followed
by bandlimit().
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # fb606
import numpy as np, wtlib

F, N, SZ = wtlib.FRAMES, wtlib.NH, wtlib.SIZE
n    = wtlib.n_ax.astype(float)          # 1..1024
nrow = n[None, :]
t    = wtlib.t
s    = np.linspace(0.0, 1.0, F)          # frame axis 0..1
COH  = np.full(N, -np.pi / 2)            # coherent phase set: gives real saw/pulse
                                         # shapes to feed the time-domain shapers.
ODD  = (n % 2 == 1)
EVEN = ~ODD


# ── small helpers ─────────────────────────────────────────────────────────────
def col(v):  return np.asarray(v, dtype=float).reshape(-1, 1)
def lin(a, b, c=1.0): return a + (b - a) * s ** c
def geo(a, b, c=1.0): return np.exp(np.log(a) + (np.log(b) - np.log(a)) * s ** c)
def ss(x0, x1):
    a = np.clip((s - x0) / (x1 - x0), 0.0, 1.0)
    return a * a * (3 - 2 * a)
def softcut(nc, k=6.0):  return 1.0 / (1.0 + (nrow / col(nc)) ** k)
def slope(p):            return nrow ** (-col(p))


def plateau(vals, blend=0.16, log=False):
    """Piecewise-constant frame parameter with smooth-stepped transitions."""
    v = np.log(np.asarray(vals, float)) if log else np.asarray(vals, float)
    k, out = len(v), np.empty(F)
    x = np.linspace(0, k, F, endpoint=False)
    for i, xv in enumerate(x):
        seg = min(int(xv), k - 1); u = xv - seg
        if seg < k - 1 and u > 1 - blend:
            a = (u - (1 - blend)) / blend; a = a * a * (3 - 2 * a)
            out[i] = v[seg] * (1 - a) + v[seg + 1] * a
        else:
            out[i] = v[seg]
    return np.exp(out) if log else out


def fmags(m):   return wtlib.finalize(wtlib.cycles_from_mags(m))
def fcoh(m):    return wtlib.cycles_from_mags(m, phase=COH)


# ══════════════════════════════════════════════════════════════════════════════
# FOUNDATION — 12
# ══════════════════════════════════════════════════════════════════════════════
def terra_sine_bloom():
    """Sine that opens into a full saw: the default 'add some teeth' table."""
    a = col(geo(0.0035, 1.0, 0.85))
    e1 = np.zeros(N); e1[0] = 1.0
    m = (1.0 - a) * e1[None, :] + a * (1.0 / nrow)
    return fmags(m * softcut(np.full(F, 1000.0), 10.0))


def terra_tri_steel():
    """Odd-only: triangle (1/n^2) hardening into a square (1/n)."""
    m = slope(lin(2.30, 1.00, 0.85)) * ODD[None, :] * softcut(np.full(F, 980.0), 8.0)
    return fmags(m)


def terra_pulse_sweep():
    """Pure PWM: 50% square narrowing to a 1.5% needle."""
    d = col(lin(0.5, 0.015, 1.35))
    m = np.abs(np.sin(np.pi * nrow * d)) / nrow
    return fmags(m * softcut(np.full(F, 960.0), 8.0))


def terra_saw_steps():
    """Four fixed brightnesses of a band-limited saw — pick a plateau, no morph mush."""
    nc = plateau([3.5, 18.0, 110.0, 950.0], blend=0.12, log=True)
    pw = plateau([1.45, 1.20, 1.00, 0.86], blend=0.12)
    return fmags(slope(pw) * softcut(nc, 18.0))


def terra_ramp_skew():
    """Ramp skew knob: falling saw -> triangle -> rising saw, one continuous shape."""
    sk = col(lin(0.055, 0.945))
    tt = t[None, :]
    w = np.where(tt < sk, tt / sk, (1.0 - tt) / (1.0 - sk))
    return wtlib.finalize(wtlib.bandlimit(2.0 * w - 1.0, 700))


def terra_rectify():
    """Sine -> half-wave -> full-wave rectified: the even-harmonic primer."""
    a = col(lin(0.0, 1.0, 0.9))
    x = np.sin(2 * np.pi * t)[None, :]
    y = (1.0 - a) * x + a * np.abs(x)
    return wtlib.finalize(wtlib.bandlimit(np.repeat(y, 1, axis=0), 420))


def terra_upshift():
    """Octave/twelfth doubler: reinforces 2x, 3x and 4x series without a 2nd osc."""
    w2, w3, w4 = ss(0.05, 0.45), ss(0.33, 0.72), ss(0.62, 1.0)
    m2 = (n % 2 == 0)[None, :]; m3 = (n % 3 == 0)[None, :]; m4 = (n % 4 == 0)[None, :]
    g = 1.0 + 3.2 * col(w2) * m2 + 4.6 * col(w3) * m3 + 6.0 * col(w4) * m4
    return fmags((1.0 / nrow) * g * softcut(np.full(F, 300.0), 5.0))


def terra_tilt():
    """One knob, all 1024 harmonics present: -24 dB/oct rolloff up to nearly flat."""
    m = slope(lin(4.00, 0.50, 1.0)) * softcut(np.full(F, 1010.0), 12.0)
    return fmags(m)


def terra_drawbar():
    """Additive organ: 16' flute -> full registration -> screaming upper drawbars."""
    parts = np.array([1, 2, 3, 4, 6, 8, 10, 12, 16])
    A = np.array([8., 0., 0., 0., 0., 0., 0., 0., 0.])
    B = np.array([8., 8., 8., 2., 0., 4., 0., 0., 2.])
    C = np.array([2., 3., 5., 7., 8., 8., 8., 8., 8.])
    u = np.clip(s * 2.0, 0, 1)[:, None]; v = np.clip(s * 2.0 - 1.0, 0, 1)[:, None]
    amp = (A[None, :] * (1 - u) + B[None, :] * u) * (1 - v) + C[None, :] * v
    m = np.zeros((F, N))
    for j, p in enumerate(parts):
        m[:, int(p) - 1] += amp[:, j] / 8.0
    m += 0.016 * (1.0 / nrow) * softcut(np.full(F, 110.0), 4.0)      # drawbar leakage
    return fmags(m)


def terra_stairs():
    """Sample-and-hold staircase: 64 steps per cycle down to 3 — the clean lo-fi basic."""
    K = geo(64.0, 3.2, 0.50)
    k0 = np.floor(K); u = col(K - k0); k0 = col(k0)
    def sh(k):
        return np.sin(2 * np.pi * (np.floor(t[None, :] * k) + 0.5) / k)
    y = (1 - u) * sh(k0) + u * sh(k0 + 1)
    return wtlib.finalize(wtlib.bandlimit(y, 700))


def terra_sub_round():
    """Bass foundation: pure sine rounding out to a fat 8-harmonic squared-off sub."""
    m = slope(lin(2.6, 1.05, 0.9)) * softcut(geo(1.05, 9.0, 1.0), 3.0)
    m = m * (1.0 + col(lin(0.0, 0.55)) * EVEN[None, :])
    m[:, 12:] = 0.0
    return fmags(m)


def terra_count():
    """Flat-spectrum buzz builder: 1 harmonic to 96, all at equal level."""
    K = col(geo(1.0, 96.0, 1.0))
    tap = np.maximum(1.0, K * 0.18)
    m = np.clip((K + 1.0 - nrow) / tap, 0.0, 1.0)
    return fmags(m)


# ══════════════════════════════════════════════════════════════════════════════
# ANALOG — 14
# ══════════════════════════════════════════════════════════════════════════════
def terra_supersaw():
    """7-voice saw stack: unison detune widens and the stack opens — trance width."""
    dv = np.array([-1.0, -0.66, -0.31, 0.0, 0.33, 0.67, 1.0])
    spread = lin(0.0, 1.45, 1.3)
    ph = 2 * np.pi * n[None, :, None] * dv[None, None, :] * spread[:, None, None]
    comb = np.abs(np.exp(1j * ph).sum(axis=2)) / 7.0
    m = slope(lin(2.25, 0.80, 0.8)) * softcut(geo(26.0, 1100.0, 0.9), 5.0)
    return fmags(m * (0.05 + 0.95 * comb))


def terra_vco_drift():
    """Two free-running VCOs whose phase relationship rots: irregular beating comb."""
    rng = np.random.default_rng(20250908)
    rn = rng.uniform(0.55, 1.65, N)
    drift = col(lin(0.0, 1.0, 1.25))
    ph = 2 * np.pi * rn[None, :] * nrow * drift * 0.075
    comb = np.abs(1.0 + 0.985 * np.exp(1j * ph)) / 1.985
    warm = np.exp(np.log(22.0) + (np.log(1050.0) - np.log(22.0)) * np.sin(np.pi * s) ** 0.65)
    m = slope(1.35 - 0.42 * np.sin(np.pi * s) ** 0.7) * softcut(warm, 6.0)
    return fmags(m * (0.06 + 0.94 * comb))


def terra_pwm_duo():
    """Two detuned pulses, widths pulling apart: the string-machine PWM bed."""
    d1 = col(lin(0.50, 0.155, 1.1)); d2 = col(lin(0.50, 0.400, 1.1))
    det = col(lin(0.0, 0.58, 1.4))
    P1 = np.sin(np.pi * nrow * d1) / nrow
    P2 = np.sin(np.pi * nrow * d2) / nrow
    m = np.abs(P1 + P2 * np.exp(1j * 2 * np.pi * nrow * det))
    m = m * nrow ** (-col(lin(0.30, 0.0)))          # string-machine tone control
    return fmags(m * softcut(geo(21.0, 900.0, 0.85), 5.0))


def terra_sync_lead():
    """Hard sync: slave ramp swept 1x -> 8.6x against a locked master. Screaming lead."""
    R = col(lin(1.0, 8.6, 1.35))
    ph = (t[None, :] * R) % 1.0
    return wtlib.finalize(wtlib.bandlimit(1.0 - 2.0 * ph, 850))


def terra_ladder():
    """Transistor-ladder sweep baked in: 24 dB/oct cutoff climb with a resonance swell."""
    fc = col(geo(1.7, 640.0, 1.0))
    k = col(0.20 + 3.10 * np.sin(np.pi * s) ** 1.2)
    H = 1.0 / ((1.0 + 1j * nrow / fc) ** 4 + k)
    src = slope(lin(1.15, 0.78))
    return fmags(np.abs(H) * src)


def terra_diode():
    """Asymmetric diode clipper: rounded ramp driven into lopsided crossover grit."""
    src = (1.0 / nrow) * softcut(geo(2.4, 900.0, 1.0), 4.0)
    x = fcoh(src)
    x = x / np.maximum(np.abs(x).max(axis=1, keepdims=True), 1e-9)
    g = col(geo(0.35, 160.0, 1.1))
    vf = col(lin(0.0, 0.38, 1.2))                    # forward-voltage dead zone
    xd = np.sign(x) * np.maximum(np.abs(x) - vf, 0.0)
    y = np.tanh(g * np.maximum(xd, 0)) + 0.55 * np.tanh(0.10 * g * np.minimum(xd, 0))
    return wtlib.finalize(wtlib.bandlimit(y, 850))


def terra_transformer():
    """Iron core loading up: clean and open -> square-law even harmonics, HF eaten."""
    src = (1.0 / nrow) * softcut(np.full(F, 700.0), 4.0)        # clean bright primary
    x = fcoh(src)
    x = x / np.maximum(np.abs(x).max(axis=1, keepdims=True), 1e-9)
    a = col(lin(0.55, 4.6, 1.0)); b = col(lin(0.04, 2.60, 1.1))
    y = np.tanh(a * x + b * (x * x - 0.5))
    y = wtlib.bandlimit(y, 820)
    S = np.fft.rfft(y, axis=1)
    kk = np.arange(SZ // 2 + 1)[None, :]
    bump = 1.0 + col(lin(0.2, 3.2)) * np.exp(-0.5 * ((np.log(kk + 1e-9) - np.log(2.1)) / 0.38) ** 2)
    core = 1.0 / (1.0 + (kk / col(geo(820.0, 52.0, 1.0))) ** 3)  # core loss rises with flux
    return wtlib.finalize(np.fft.irfft(S * bump * core, n=SZ, axis=1))


def terra_tape_wow():
    """Tape wow + oxide dropouts: pristine print washing out into warbly, dull HF loss."""
    rng = np.random.default_rng(7717)
    f = rng.uniform(0, 1, N)
    f = np.convolve(f, np.ones(9) / 9.0, mode='same')
    f = (f - f.min()) / (f.max() - f.min() + 1e-12)
    dep = col(lin(0.0, 0.052, 1.15))
    smear = np.exp(-0.5 * (nrow * dep) ** 2)
    bump = 1.0 + 0.85 * np.exp(-0.5 * ((np.log(nrow) - np.log(2.6)) / 0.5) ** 2)
    drop = 1.0 - col(lin(0.0, 0.88, 1.3)) * f[None, :]
    m = slope(lin(0.72, 1.30, 1.0)) * smear * bump * drop
    return fmags(m * softcut(np.full(F, 1000.0), 10.0))


def terra_vco_sick():
    """VCO health failing: pristine ramp -> duty error, dead partials, HF spitting."""
    rng = np.random.default_rng(31337)
    dead = rng.uniform(0, 1, N)
    lump = np.sin(n * 0.41 + rng.uniform(0, 6.283, N)) * 0.5 + 0.5
    crk = rng.uniform(0, 1, N) ** 2
    ill = col(lin(0.0, 1.0, 1.15))
    d = col(lin(0.5, 0.462, 1.0))
    duty = np.abs(np.sin(np.pi * nrow * d)) / np.sin(np.pi * d)
    m = slope(lin(1.20, 0.62, 1.0)) * duty
    m = m * (1.0 - 0.985 * ill * (dead[None, :] > 0.66))
    m = m * (1.0 - 0.80 * ill * lump[None, :])
    m = m + 0.010 * ill * crk[None, :] * (nrow > 45)
    return fmags(m * softcut(np.full(F, 1000.0), 10.0))


def terra_juno_chorus():
    """BBD chorus on a string-machine saw: two delay taps, notches crawling upward."""
    D1 = col(geo(0.012, 0.135, 1.0)); D2 = col(geo(0.020, 0.088, 1.0))
    g = col(lin(0.45, 0.97, 1.0))
    H = np.abs(1.0 + g * np.exp(-1j * 2 * np.pi * nrow * D1)
                   + 0.75 * g * np.exp(-1j * 2 * np.pi * nrow * D2)) / 2.7
    bright = 2.15 - 1.35 * np.sin(np.pi * s) ** 0.8
    m = slope(bright) * softcut(geo(70.0, 900.0, 0.7), 5.0)
    return fmags(m * (0.05 + 0.95 * H))


def terra_divider():
    """CMOS divider organ: top flip-flop alone, then /2, /4, /8 stack downward to the sub."""
    w = [1.7 * ss(0.55, 0.98), 1.25 * ss(0.30, 0.66), ss(0.05, 0.38), np.ones(F)]
    m = np.zeros((F, N))
    for j, b in enumerate([1, 2, 4, 8]):
        mask = (n % b == 0) & (((n / b) % 2) == 1)      # exact square out of a flip-flop
        amp = np.zeros((F, N))
        amp[:, mask] = b / n[mask][None, :]
        m += col(w[j]) * amp
    return fmags(m * softcut(np.full(F, 950.0), 8.0))


def terra_varishape():
    """The vintage shape knob, one continuous crossfade: triangle -> saw -> square -> 24% pulse."""
    a = col(ss(0.00, 0.38)); b = col(ss(0.38, 0.72)); c = col(ss(0.72, 1.00))
    tt = t[None, :]
    tri = 2.0 * np.abs(2.0 * tt - 1.0) - 1.0
    saw = 1.0 - 2.0 * tt
    d = 0.5 - 0.26 * c
    pul = np.where(tt < d, 1.0, -1.0)
    y = (1 - a) * tri + a * ((1 - b) * saw + b * pul)
    return wtlib.finalize(wtlib.bandlimit(y, 800))


def terra_brownout():
    """Failing supply: the ramp charges slower and sags every frame — dying-gear bass."""
    tau = col(geo(0.075, 2.4, 1.0))
    amp = col(lin(1.0, 0.52, 1.0))
    rip = col(lin(0.0, 0.34, 1.5))
    ch = (1.0 - np.exp(-t[None, :] / tau)) / (1.0 - np.exp(-1.0 / tau))
    y = amp * (2.0 * ch - 1.0) + rip * np.sin(6 * np.pi * t[None, :] + 0.7)   # rail ripple
    S = np.fft.rfft(wtlib.bandlimit(y, 900), axis=1)
    kk = np.arange(SZ // 2 + 1)[None, :]
    slew = 1.0 / (1.0 + 1j * kk / col(geo(700.0, 11.0, 1.1)))     # one-pole: slew rate dies
    return wtlib.finalize(np.fft.irfft(S * slew, n=SZ, axis=1))


def terra_tri_core():
    """Triangle-core VCO starving: bowed exponential slopes collapsing into a ramp."""
    c1 = col(lin(0.14, 9.0, 1.1)); c2 = col(lin(0.85, 2.1, 1.1))
    tt = t[None, :]
    up = np.clip(tt / 0.5, 0, 1); dn = np.clip((1.0 - tt) / 0.5, 0, 1)
    def bow(u, c): return (1.0 - np.exp(-c * u)) / (1.0 - np.exp(-c))
    tri = np.where(tt < 0.5, bow(up, c1), bow(dn, c2))
    ramp = 1.0 - 2.0 * bow(tt, 2.6)
    col_ = col(ss(0.62, 1.0)) * 1.0
    y = (1 - col_) * (2.0 * tri - 1.0) + col_ * ramp
    y = wtlib.bandlimit(y, 850)
    S = np.fft.rfft(y, axis=1)
    kk = np.arange(SZ // 2 + 1)[None, :]
    return wtlib.finalize(np.fft.irfft(S / (1.0 + (kk / 700.0) ** 3), n=SZ, axis=1))


TABLES = [
    ("TERRA SINE BLOOM",  terra_sine_bloom),
    ("TERRA TRI STEEL",   terra_tri_steel),
    ("TERRA PULSE SWEEP", terra_pulse_sweep),
    ("TERRA SAW STEPS",   terra_saw_steps),
    ("TERRA RAMP SKEW",   terra_ramp_skew),
    ("TERRA RECTIFY",     terra_rectify),
    ("TERRA UPSHIFT",     terra_upshift),
    ("TERRA TILT",        terra_tilt),
    ("TERRA DRAWBAR",     terra_drawbar),
    ("TERRA STAIRS",      terra_stairs),
    ("TERRA SUB ROUND",   terra_sub_round),
    ("TERRA COUNT",       terra_count),
    ("TERRA SUPERSAW",    terra_supersaw),
    ("TERRA VCO DRIFT",   terra_vco_drift),
    ("TERRA PWM DUO",     terra_pwm_duo),
    ("TERRA SYNC LEAD",   terra_sync_lead),
    ("TERRA LADDER",      terra_ladder),
    ("TERRA DIODE",       terra_diode),
    ("TERRA TRANSFORMER", terra_transformer),
    ("TERRA TAPE WOW",    terra_tape_wow),
    ("TERRA VCO SICK",    terra_vco_sick),
    ("TERRA JUNO CHORUS", terra_juno_chorus),
    ("TERRA DIVIDER",     terra_divider),
    ("TERRA VARISHAPE",   terra_varishape),
    ("TERRA BROWNOUT",    terra_brownout),
    ("TERRA TRI CORE",    terra_tri_core),
]

FOUNDATION = {nm for nm, _ in TABLES[:12]}

# ══════════════════════════════════════════════════════════════════════════════════════
# fb606 — THE MERGED TEN. The bank shipped as eight generated folders; the browser showed a
# DIFFERENT set for its built-ins, so the same sound had two names depending on where you
# looked. The two sets are now ONE ten-folder taxonomy, and every module declares which of
# the ten each of its tables belongs to. gate.py reads CATEGORY and never guesses from the
# module name — a table with no entry here is a hard error, not a silent "GEN_WHATEVER".
# ══════════════════════════════════════════════════════════════════════════════════════

# The file's own two sections ARE the split: the first twelve are the known quantities
# (sine, saw, pulse, organ) and become "Basic Shapes"; the fourteen after them are the
# vintage-hardware lane — drift, PWM, ladder, tape, brownout — and become "Analog".
CATEGORY = {nm: ("Basic Shapes" if nm in FOUNDATION else "Analog") for nm, _ in TABLES}


if __name__ == "__main__":
    built = []
    for nm, fn in TABLES:
        fr = fn()
        if nm in FOUNDATION:
            ok, rep = wtlib.selfcheck(nm, fr, min_h60=1, min_span=0.5)
        else:
            ok, rep = wtlib.selfcheck(nm, fr)
        print(rep)
        built.append((nm, fr))
    fps = [(nm, wtlib.fingerprint(fr)) for nm, fr in built]
    worst, pair = 1e9, None
    rows = []
    for i in range(len(fps)):
        for j in range(i + 1, len(fps)):
            d = wtlib.distance(fps[i][1], fps[j][1])
            rows.append((d, fps[i][0], fps[j][0]))
            if d < worst:
                worst, pair = d, (fps[i][0], fps[j][0])
    rows.sort()
    print("\nclosest pairs:")
    for d, a, b in rows[:12]:
        print(f"   {d:6.2f} dB   {a} | {b}")
    print(f"\nMIN PAIRWISE DISTANCE = {worst:.2f} dB  ({pair[0]} <-> {pair[1]})")
