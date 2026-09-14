"""
gen2_abstract.py — ABSTRACT: geometry, and "what is this shape?" (fb638, the Terrain 500).

Every table here is a SHAPE first: a curve, a figure, a fractal or a picture, read as a waveform. Most are
TIME-built at 8x (16384 samples per cycle) and band-limited to harmonic 1000 by FFT, so corners, jumps, pen-lifts
and fractal detail become the full harmonic series instead of aliasing. The frame axis is always a PROCESS on the
geometry (a fractal level, a fold angle, an observer walking, a camera flying, a scanline moving down a picture),
never a crossfade between two finished shapes: frame 0 is playable, the last frame is the wild one.

Helpers:  rows(fn) renders fn(u) -> one oversampled cycle for u = 0..1 over the 128 frames, band-limited.
          fin(Y)   rotates every frame by one common offset if the wrap would click (a rotation changes no
                   magnitude, metric or fingerprint), then wtlib.finalize.
Deterministic: every random choice comes from a seeded numpy Generator.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, wtlib

FR, SZ = wtlib.FRAMES, wtlib.SIZE
OS = 8
M = SZ * OS                        # oversampled cycle length
T = np.arange(M) / M               # oversampled phase, [0, 1)
U = np.linspace(0.0, 1.0, FR)      # the frame axis
TAU = 2.0 * np.pi
NMAX = 1000


# ── helpers ───────────────────────────────────────────────────────────────────────────────────
def frac(x):
    return x - np.floor(x)


def sq(x, duty=0.5):
    return np.where(frac(x) < duty, 1.0, -1.0)


def saw(x):
    return 2.0 * frac(x) - 1.0


def tri(x):
    return 1.0 - 4.0 * np.abs(frac(x + 0.25) - 0.5)


def ease(u, a, b):
    return float(np.clip((u - a) / (b - a), 0.0, 1.0))


def smooth(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def pdist(a, b):
    """signed shortest distance a-b on the unit circle."""
    return frac(a - b + 0.5) - 0.5


def _bl1(x, nmax=NMAX):
    x = np.asarray(x, dtype=float)
    S = np.fft.rfft(x)
    o = np.zeros(SZ // 2 + 1, dtype=complex)
    k = min(nmax, SZ // 2 - 1, len(S) - 1)
    o[1:k + 1] = S[1:k + 1]
    return np.fft.irfft(o, n=SZ)


def rows(fn, nmax=NMAX):
    out = np.empty((FR, SZ))
    for i, u in enumerate(U):
        out[i] = _bl1(fn(float(u)), nmax)
    return out


def seamfix(Y, thresh=1.2):
    Y = np.asarray(Y, dtype=float)
    d = np.abs(np.diff(np.concatenate([Y, Y[:, :1]], axis=1), axis=1))   # d[:, SZ-1] is the wrap
    k = max(1, int(round(0.01 * (SZ - 1))))
    typ = np.partition(d, SZ - k, axis=1)[:, -k:].mean(axis=1)
    r = d / np.maximum(typ[:, None], 1e-12)
    if r[:, -1].max() <= thresh:
        return Y
    i = int(np.argmin(r.max(axis=0)))
    return np.roll(Y, -(i + 1), axis=1)


def fin(Y):
    return wtlib.finalize(seamfix(Y))


def arc_resample(P, m=M, closed=True):
    """Closed (or open) polyline of complex points -> m samples at constant pen speed."""
    P = np.asarray(P, dtype=complex)
    Q = np.append(P, P[0]) if closed else P
    s = np.concatenate([[0.0], np.cumsum(np.abs(np.diff(Q)))])
    q = np.arange(m) / m * s[-1]
    return np.interp(q, s, Q.real) + 1j * np.interp(q, s, Q.imag)


def step_resample(P, m=M):
    """Equal time per vertex-to-vertex segment of a closed polyline."""
    P = np.asarray(P, dtype=complex)
    Q = np.append(P, P[0])
    x = np.arange(m) / m * (len(Q) - 1)
    return np.interp(x, np.arange(len(Q)), Q.real) + 1j * np.interp(x, np.arange(len(Q)), Q.imag)


def hold_resample(v, m=M):
    """Sample-and-hold: each value holds for an equal share of the cycle."""
    v = np.asarray(v)
    return v[np.minimum((np.arange(m) * len(v)) // m, len(v) - 1)]


# ═════════════════════════════ CLONES & FACETS (phase multiplication) ═════════════════════════════
def clone_fan():
    """CLONE FAN — a half-sine bump cloned C times inside the cycle (C = 1 -> 20, fractional, so the last clone is cut
    off), every clone tilted a little more than the one before, like a deck of cards fanned open, and past the middle
    every other clone flips upside down. Frame 0 is one bump (a rounded pulse); the end is a 20-blade fan of tilted,
    alternating facets."""
    def f(u):
        C = 1.0 + 19.0 * u ** 1.3
        ph = T * C
        j = np.floor(ph); tau = ph - j
        n = max(np.ceil(C), 1.0)
        spread = 1.6 * smooth(u / 0.75)
        tilt = spread * (2.0 * j / max(n - 1.0, 1.0) - 1.0)
        amp = 1.0 - 0.55 * u * j / n
        flip = 1.0 - 2.0 * ease(u, 0.4, 0.75) * (j % 2)
        return amp * flip * (np.sin(np.pi * tau) + tilt * (tau - 0.5))
    return fin(rows(f))


def zeno_clones():
    """ZENO CLONES — the cycle holds a sine, then a copy r times shorter, then r^2 ... (Zeno's paradox: infinitely many
    copies before the wrap). r = 0.03 -> 0.5, so mid-table the copies halve exactly and the spectrum becomes an octave
    comb (energy at 2, 4, 8, 16 ...); then every copy is windowed into a burst, the bursts carry 1 -> 3 cycles, every
    other one flips and the tiny ones swell: a chirp of octave sparks racing into the wrap."""
    J = 30
    def f(u):
        r = 0.03 + 0.47 * ease(u, 0.0, 0.5) ** 1.1
        c = 1.0 + 2.0 * ease(u, 0.5, 1.0)
        w = r ** np.arange(J); w /= w.sum()
        e = np.concatenate([[0.0], np.cumsum(w)]); e[-1] = 1.0 + 1e-12
        j = np.clip(np.searchsorted(e, T, side='right') - 1, 0, J - 1)
        tau = (T - e[j]) / w[j]
        win = (0.5 - 0.5 * np.cos(TAU * tau)) ** ease(u, 0.0, 0.3)
        amp = (w[0] / w[j]) ** (0.3 * ease(u, 0.3, 1.0))
        return amp * win * np.sin(TAU * c * tau) * (1.0 - 2.0 * ease(u, 0.25, 0.5) * (j % 2))
    return fin(rows(f))


def kaleidoscope():
    """KALEIDOSCOPE — a random smooth motif mirrored into 2K panes (each odd pane is the motif played backwards),
    K = 1 -> 24; the motif itself turns slowly like the tube of a kaleidoscope and, past the middle, alternate
    pairs of panes flip upside down. Frame 0: one smooth motif. End: 48 mirrored shards."""
    rng = np.random.default_rng(1101)
    H = 7
    a = rng.normal(size=H) / np.arange(1, H + 1) ** 0.8
    p0 = rng.uniform(0, TAU, H); dr = rng.normal(size=H) * 3.0
    hh = np.arange(1, H + 1)
    def f(u):
        K = 1.0 + 23.0 * u ** 1.5
        seg = T * 2.0 * K
        j = np.floor(seg); tau = seg - j
        tm = np.where(j % 2 == 0, tau, 1.0 - tau)
        base = (a[:, None] * np.sin(TAU * hh[:, None] * tm[None, :] + (p0 + dr * u)[:, None])).sum(0)
        neg = 1.0 - 2.0 * ease(u, 0.5, 0.8) * ((j // 2) % 2)
        return base * neg
    return fin(rows(f))


# ═════════════════════════════ STACKED SQUARES ═════════════════════════════
def stacked_squares():
    """STACKED SQUARES — square waves at 1, 2, 5, 11, 23, 47, 97, 197 cycles (each a little more than double the one
    below) stacked one on top of the next like blocks. Frame 0 is one square; each new layer slides in on top, every
    other layer narrows into a pulse, and the weighting tips toward the top of the stack: the end is a fast square
    riding a tower of slower ones."""
    fr_ = [1, 2, 5, 11, 23, 47, 97, 197]
    def f(u):
        y = np.zeros(M)
        tp = ease(u, 0.2, 1.0)
        for n, fq in enumerate(fr_):
            g = 1.0 if n == 0 else ease(u, 0.05 + 0.1 * (n - 1), 0.15 + 0.1 * (n - 1))
            if g <= 0:
                continue
            amp = (fq ** 0.45) ** tp / (1.0 + 0.4 * n) ** (1 - tp)
            duty = 0.5 if n % 2 == 0 else 0.5 - 0.3 * ease(u, 0.3, 0.8)
            y += g * amp * sq(fq * T + 0.13 * n * u, duty)
        return y
    return fin(rows(f))


def skyline():
    """SKYLINE — dawn over hills, then a city: frame 0 is the soft silhouette of two hills (a sub-heavy hump); buildings
    rise out of them one after another with walls that harden from slopes to sheer cliffs, antenna needles arrive
    last, and the windows light up finer and finer until every facade buzzes: a downtown at night."""
    rng = np.random.default_rng(4242)
    NB = 72
    c = rng.uniform(0, 1, NB); w = rng.uniform(0.01, 0.11, NB); h = rng.uniform(0.15, 1.0, NB)
    ant = rng.random(NB) < 0.18
    w[ant] = rng.uniform(0.002, 0.006, ant.sum()); h[ant] = rng.uniform(0.9, 1.4, ant.sum())
    born = np.sort(rng.uniform(0.08, 0.93, NB))
    born[ant] = np.maximum(born[ant], 0.6)
    wp = rng.uniform(0, 1, NB)
    hills = 0.35 * (1 + np.cos(TAU * (T - 0.3))) * 0.5 + 0.2 * (1 + np.cos(TAU * (2 * T - 0.1))) * 0.5
    def f(u):
        sharp = 40.0 * (60.0 ** ease(u, 0.1, 0.8))
        y = hills * (1.0 - 0.6 * ease(u, 0.1, 0.6)); top = np.full(M, -1)
        for b in range(NB):
            g = np.clip((u - born[b]) / 0.1, 0.0, 1.0)
            if g <= 0:
                continue
            wall = 0.5 + 0.5 * np.tanh(sharp * (w[b] / 2 - np.abs(pdist(T, c[b]))))
            hb = h[b] * g * wall
            ins = hb > y
            y = np.where(ins, hb, y); top = np.where(ins & (wall > 0.5), b, top)
        W = 24.0 * (14.0 ** ease(u, 0.3, 1.0))
        lit = sq(W * T + wp[np.maximum(top, 0)] * 7.0, 0.35 + 0.3 * u) * 0.5 + 0.5
        return y - (top >= 0) * lit * ease(u, 0.3, 0.55) * (0.1 + 0.25 * u) * np.maximum(y, 0.3)
    return fin(rows(f))


# ═════════════════════════════ CROSSING LINE-ART ═════════════════════════════
def scope_scribble():
    """SCOPE SCRIBBLE — oscilloscope line-art: the beam hops between K sine curves (harmonics 1, 3, 2, 5, 4, 7)
    many times per cycle, so the scope draws K crossing lines at once. K = 1 -> 6 and the hop rate 48 -> 250.
    Frame 0 is a pure sine (a sub); the lines multiply, slide across each other and the hops tear the top open."""
    hs = np.array([1, 3, 2, 5, 4, 7]); p0 = np.array([0.0, 0.3, 1.1, 2.0, 0.7, 1.6])
    def mux(K, R, u):
        slot = np.floor(T * R).astype(int)
        idx = slot % K
        return np.sin(TAU * hs[idx] * T + p0[idx] + u * 2.0 * idx)
    def f(u):
        Kf = 1.0 + 5.0 * u ** 0.9
        R = int(round(48 + 200 * u ** 2))
        K0 = int(np.floor(Kf)); fr_ = Kf - K0
        y = mux(K0, R, u)
        if fr_ > 0 and K0 < 6:
            y = (1 - fr_) * y + fr_ * mux(K0 + 1, R, u)
        return y
    return fin(rows(f))


def times_table():
    """TIMES TABLE — string art: N pins on a circle, a thread from pin k to pin m*k and on to pin k+1 (the 'times
    table' that draws a cardioid at m = 2, a nephroid at 3 ...). The pen's height along the thread is the
    waveform, so two sine lines interleave as a buzzing zig-zag. m = 2 -> 47, pins 40 -> 400, the view rotates."""
    def f(u):
        N = int(round(40 * 10 ** u))
        k = np.arange(N)
        m = 2.0 + 45.0 * u ** 1.4
        A = np.exp(1j * TAU * k / N); B = np.exp(1j * TAU * m * k / N)
        P = np.stack([A, B], axis=1).reshape(-1)
        z = step_resample(P)
        return (z * np.exp(-1j * (0.5 * np.pi * u + 0.3))).real
    return fin(rows(f))


# ═════════════════════════════ FRACTALS ═════════════════════════════
def blancmange():
    """BLANCMANGE — the Takagi curve: a triangle plus half-size triangles at twice the rate plus ... Levels 0 -> 12
    grow in (frame 0 is a triangle), the weight climbs 0.5 -> 0.95 (the pudding becomes a jagged mountain range of
    itself) and at the end each little triangle flips at random: a shattered ridge."""
    rng = np.random.default_rng(77)
    flips = [rng.random(2 ** min(n, 13)) < 0.5 for n in range(14)]
    def f(u):
        L = 12.0 * ease(u, 0.0, 0.3)
        w = 0.5 + 0.45 * ease(u, 0.3, 0.8) ** 1.1
        p = ease(u, 0.6, 1.0)
        y = np.zeros(M)
        for n in range(14):
            g = float(np.clip(L - n + 1.0, 0.0, 1.0))
            if g <= 0:
                break
            x = (2 ** n) * T
            term = np.abs(x - np.round(x))
            idx = np.floor(x).astype(int) % len(flips[n])
            y += g * w ** n * term * (1.0 - 2.0 * p * flips[n][idx])
        return y
    return fin(rows(f))


def koch_points(P, level, h):
    for _ in range(level):
        a = P; d = np.roll(P, -1) - P
        P = np.stack([a, a + d / 3, a + d * (0.5 - 1j * h), a + 2 * d / 3], axis=1).reshape(-1)
    return P


def koch_coastline():
    """KOCH COASTLINE — the Koch snowflake, level 7 (49152 edges), traced at constant pen speed; the bump height of the
    generator grows 0 -> 0.29 (triangle -> snowflake) and on to 0.88, where the bumps overlap and the coast folds over
    itself into a near-space-filling tangle. The view rotates 60 degrees. Frame 0: a triangle traced. Three-fold
    symmetry: no harmonic divisible by three ever sounds."""
    base = np.exp(1j * (np.pi / 2 + TAU * np.arange(3) / 3))
    def f(u):
        h = 0.2887 * ease(u, 0.0, 0.35) + (0.88 - 0.2887) * ease(u, 0.35, 1.0) ** 1.1
        P = koch_points(base, 7, h)
        z = arc_resample(P, m=4 * M)[::4]
        return (z * np.exp(-1j * np.pi / 3 * u)).real
    return fin(rows(f))


def _paperfold(L):
    k = np.arange(1, 2 ** L)
    low = k & -k
    return np.where((k & (low << 1)) == 0, 1.0, -1.0)


def paper_dragon():
    """PAPER DRAGON — fold a strip of paper in half 13 times and open every crease to the same angle: at 90 degrees it
    is the Heighway dragon. Five dragons are laid nose to tail round a pentagon; the crease angle runs 40 -> 90 -> 170
    degrees (a gentle meander -> five dragons -> a crumpled paper ball). Five-fold symmetry: harmonics 1, 4, 6, 9, 11 ...
    only."""
    turns = _paperfold(13)
    def f(u):
        th = np.radians(40.0 + 130.0 * u ** 1.1)
        head = np.concatenate([[0.0], np.cumsum(turns * th)])
        P = np.concatenate([[0.0], np.cumsum(np.exp(1j * head))])
        z = step_resample(rosette(P, 5))
        return (z * np.exp(-1j * 0.6 * u)).real
    return fin(rows(f))


def hilbert_walk():
    """HILBERT WALK — a walker on the Moore curve, the closed-loop cousin of Hilbert's space-filling curve (four
    Hilbert curves joined in a ring), seen from a slowly turning direction. Order 1 -> 7, each order's path morphing
    into the next: frame 0 is a small square loop (a trapezoid); the end is a 16384-step self-similar maze. The loop's
    four-fold symmetry keeps it odd-harmonic all the way."""
    cache = {}
    def path(o):
        if o not in cache:
            z = np.concatenate([[0.0], np.cumsum(np.exp(1j * _moore(o) * np.pi / 2))])[:-1]
            z = z - z.mean()
            z = z / np.abs(z).max()
            cache[o] = step_resample(z)
        return cache[o]
    def f(u):
        o = 1.0 + 6.0 * u ** 0.9
        o0 = int(np.floor(o)); g = o - o0
        z = path(o0)
        if g > 0 and o0 < 7:
            z = (1 - g) * z + g * path(o0 + 1)
        a = 0.2 + 1.1 * u
        return (z * np.exp(-1j * a)).real
    return fin(rows(f))


def _derham(a, L=14, conj=False):
    P = np.array([0.0 + 0j, 1.0 + 0j])
    for _ in range(L):
        Q = np.conj(P) if conj else P
        P = np.concatenate([a * Q[:-1], a + (1 - a) * Q])
    return P


def de_rham_morph():
    """DE RHAM MORPH — one recursive rule (squash the curve into two copies joined at a point a) whose single complex
    parameter a walks through a whole family of fractals: a bumpy arch, the Cesaro-Koch curve, the Levy C curve, then
    off-centre into lopsided dragons, their bumps stretched tall. Seven copies ring a heptagon, so only harmonics
    1, 6, 8, 13, 15 ... sound: a hollow seven-fold bell of a fractal."""
    def f(u):
        r = 0.1 + 0.45 * u ** 0.9
        psi = np.radians(90.0 + 25.0 * ease(u, 0.5, 1.0))
        a = 0.5 + r * np.exp(1j * psi)
        P = _derham(a, L=11)
        P = P.real + 1j * P.imag * (2.5 + 1.5 * u)
        z = step_resample(rosette(P, 7))
        return (z * np.exp(-1j * 0.8 * u)).real
    return fin(rows(f))


def _lsys(axiom, rules, n):
    s = axiom
    for _ in range(n):
        s = "".join(rules.get(ch, ch) for ch in s)
    return s


def _turtle_struct(s):
    """Parse an L-system string once: for every 'F' its (left-turn count, right-turn count, start = index of the
    segment it continues from, or -1 for the origin)."""
    L = R = 0; last = -1; stack = []; segs = []
    for ch in s:
        if ch == 'F':
            segs.append((L, R, last)); last = len(segs) - 1
        elif ch == '+':
            L += 1
        elif ch == '-':
            R += 1
        elif ch == '[':
            stack.append((L, R, last))
        elif ch == ']':
            L, R, last = stack.pop()
    a = np.array(segs, dtype=int)
    return a[:, 0], a[:, 1], a[:, 2]


def fractal_bush():
    """FRACTAL BUSH — a wreath: eight L-system plants (X -> F+[[X]-X]-F[-FX]+X) growing outward from a ring, drawn by a
    pen that never lifts (it walks out along every branch and back down to the fork, then along the ring to the next
    plant). Generation 1 -> 5 while the branching angle opens 22 -> 40 degrees and a wind bends the right turns.
    Eight-fold symmetry: harmonics 1, 7, 9, 15, 17 ... only."""
    rules = {'X': 'F+[[X]-X]-F[-FX]+X', 'F': 'FF'}
    structs = {}
    NR = 8
    V = np.exp(1j * TAU * np.arange(NR) / NR)
    def tour(n, th_l, th_r):
        if n not in structs:
            structs[n] = _turtle_tour(_lsys('X', rules, n))
        Lc, Rc, prev, tk, td = structs[n]
        step = np.exp(1j * (np.pi / 2 + Lc * th_l - Rc * th_r))
        end = np.empty(len(step), dtype=complex)
        for i in range(len(step)):
            p = prev[i]
            end[i] = (end[p] if p >= 0 else 0) + step[i]
        start = end - step
        return np.where(td > 0, start[tk], end[tk]) / (np.abs(end).max() + 1e-12)
    def draw(n, th_l, th_r):
        A = tour(n, th_l, th_r)
        seg = int(max(4, len(A) // 10))
        parts = []
        for j in range(NR):
            parts.append(V[j] + 1.7 * A * (-1j * V[j]))
            parts.append(V[j] + (V[(j + 1) % NR] - V[j]) * np.arange(seg) / seg)
        return step_resample(np.concatenate(parts))
    def f(u):
        n = 1.0 + 4.0 * ease(u, 0.0, 0.75)
        th_l = np.radians(22.0 + 18.0 * u); th_r = np.radians(22.0 + 30.0 * u ** 1.5)
        n0 = int(np.floor(n)); g = n - n0
        z = draw(n0, th_l, th_r)
        if g > 0 and n0 < 5:
            z = (1 - g) * z + g * draw(n0 + 1, th_l, th_r)
        return (z * np.exp(-1j * 0.3 * u)).real
    return fin(rows(f))


def ford_circles():
    """FORD CIRCLES — a circle touching the line at every fraction p/q, radius 1/2q^2 (they never overlap). The
    waveform is the landscape seen from the line: the underside of every circle up to denominator Q, Q = 1 -> 46.
    Frame 0 is one bowl (a round-bottomed wave with vertical walls); the fractions carve notch after notch into it
    until it is a Farey fractal of scallops, and the end drops the odd-denominator circles below the line."""
    from math import gcd
    circ = [(p / q, q) for q in range(1, 47) for p in range(q) if gcd(p, q) == 1]
    def f(u):
        Q = 1.0 + 45.0 * u ** 1.5
        mix = ease(u, 0.65, 1.0)
        y = np.full(M, 0.5)
        z = np.zeros(M)
        for c, q in circ:
            g = float(np.clip(Q - q + 1.0, 0.0, 1.0))
            if g <= 0:
                continue
            r = g / (2.0 * q * q)
            i0 = int(np.floor((c - r) * M)); i1 = int(np.ceil((c + r) * M)) + 1
            idx = np.arange(i0, i1) % M
            dx = pdist(T[idx], c)
            inside = np.abs(dx) < r
            if not inside.any():
                continue
            idx = idx[inside]; dx = dx[inside]
            bot = r - np.sqrt(np.maximum(r * r - dx * dx, 0.0))
            y[idx] = np.minimum(y[idx], bot)
            if q % 2 == 1 and q > 1:
                z[idx] = np.maximum(z[idx], r - bot)
        return y - mix * 1.5 * z
    return fin(rows(f))


def popcorn_function():
    """POPCORN FUNCTION — Thomae's function: at every fraction p/q a spike of height 1/q, nothing elsewhere (continuous at
    every irrational, broken at every rational). Denominators 2 -> 80 pop in while every spike thins to a needle:
    frame 0 is a single spike as wide as the cycle (a triangle wave), the end is a Farey forest of needles."""
    from math import gcd
    bars = [(p / q, q) for q in range(2, 81) for p in range(1, q) if gcd(p, q) == 1]
    def f(u):
        Q = 2.0 + 78.0 * u ** 1.5
        y = np.zeros(M)
        for c, q in bars:
            g = float(np.clip(Q - q + 1.0, 0.0, 1.0))
            if g <= 0:
                continue
            hw = min(0.5, 0.5 / q ** 2 * (1.0 + 7.0 * (1.0 - u) ** 2) * (4.0 if q == 2 else 1.0) ** (1 - u))
            i0 = int(np.floor((c - hw) * M)); i1 = int(np.ceil((c + hw) * M)) + 1
            idx = np.arange(i0, i1) % M
            dx = np.abs(pdist(T[idx], c))
            y[idx] = np.maximum(y[idx], np.maximum(1.0 - dx / hw, 0.0) * g / q ** 0.55)
        return y
    return fin(rows(f))


# ═════════════════════════════ SUPERFORMULA / SPIROGRAPH / LISSAJOUS / ORBITS ═════════════════════════════
def superformula_bloom():
    """SUPERFORMULA BLOOM — Gielis' superformula r(phi) = (|cos(m phi/4)|^n2 + |sin(m phi/4)|^n3)^(-1/n1), drawn and
    projected. Symmetry m = 2 -> 16 (non-integer: the flower does not close, leaving a crack), n1 = 3 -> 0.25
    (round -> spiked), n2 = n3 = 1.7 -> 0.7. Frame 0 is an almost-circle (a sine); the end is a cracked star."""
    phi = TAU * T
    def f(u):
        m = 2.0 + 14.0 * u ** 1.2
        n1 = 3.0 * (0.25 / 3.0) ** u
        n2 = 1.7 - 1.0 * u
        r = (np.abs(np.cos(m * phi / 4)) ** n2 + np.abs(np.sin(m * phi / 4)) ** n2) ** (-1.0 / n1)
        r = r / r.max()
        a = 0.9 * u
        return r * np.cos(phi - a)
    return fin(rows(f))


def spirograph_velocity():
    """SPIROGRAPH VELOCITY — a spirograph pen (hypotrochoid, 3 then 4 lobes) drawn at constant speed, and we listen to
    the pen's DIRECTION, not its position: on a circle that is a sine, but when the pen arm reaches the rim the curve
    grows cusps where the pen stops and reverses (a jump), and past it, loops where it spins. Arm 0 -> 1.8."""
    th = TAU * np.arange(4 * M) / (4 * M)
    def one(k, d):
        dz = 1j * k * (np.exp(1j * th) - d * np.exp(-1j * k * th))
        sp = np.abs(dz) + 1e-9
        s = np.concatenate([[0.0], np.cumsum(sp[:-1])]); s /= s[-1] + sp[-1]
        ts = np.interp(T, s, th)
        zz = k * np.exp(1j * ts) + d * np.exp(-1j * k * ts)
        v = 1j * k * (np.exp(1j * ts) - d * np.exp(-1j * k * ts))
        return zz / (k + d), v / (np.abs(v) + 1e-9)
    def f(u):
        d = 1.8 * u ** 1.1
        g = ease(u, 0.4, 0.6)
        z3, v3 = one(3, d)
        if g > 0:
            z4, v4 = one(4, d); z3 = (1 - g) * z3 + g * z4; v3 = (1 - g) * v3 + g * v4
        b = ease(u, 0.0, 0.4)
        return (1 - 0.8 * b) * z3.real + 0.8 * b * (v3.real + 0.3 * v3.imag)
    return fin(rows(f))


def kepler_orbit():
    """KEPLER ORBIT — a planet on a Kepler ellipse (equal areas in equal times), seen edge-on while its orbit precesses.
    Eccentricity 0 -> 0.997: frame 0 is a circle (a pure sine, a sub), then the planet lingers far out and whips through
    perihelion faster and faster, and early on two resonant comets (2:1 and 3:1) join the slingshot. The fundamental
    survives, the top becomes a triple whip-crack."""
    E = np.linspace(0, TAU, 600001)
    def orbit(e, tt):
        Et = np.interp(frac(tt) * TAU, E - e * np.sin(E), E)
        return np.cos(Et) - e, np.sqrt(1 - e * e) * np.sin(Et)
    def f(u):
        e = 0.997 * (1 - (1 - u) ** 5.0)
        x, y = orbit(e, T)
        w = 0.4 + 2.6 * u
        out = x * np.cos(w) + y * np.sin(w)
        g = ease(u, 0.15, 0.6)
        if g > 0:
            x2, y2 = orbit(e * 0.995, 2 * T + 0.37); x3, y3 = orbit(e * 0.99, 3 * T + 0.71)
            out = out + g * (0.8 * (x2 * np.cos(-w) + y2 * np.sin(-w)) + 0.6 * (x3 * np.cos(2 * w) + y3 * np.sin(2 * w)))
        return out
    return fin(rows(f))


def winding_number():
    """WINDING NUMBER — an observer walks into a looped curve (a circle carrying five inner loops) and the waveform is
    the direction in which she sees the pen. Outside, the bearing only wobbles (a soft wave); once inside, it winds all
    the way round once per cycle (a saw riser); inside the loops it winds twice. The walk ends brushing past the curve,
    where the bearing snaps."""
    th = TAU * T
    z = np.exp(1j * th) + 0.5 * np.exp(1j * 6 * th) + 0.15 * np.exp(-1j * 3 * th)
    pts = [2.6 + 0.4j, 1.5 + 0.3j, 0.7 + 0.15j, 0.08 + 0.05j, -0.4 - 0.3j, -0.72 - 0.4j, -1.1 - 0.2j]
    def f(u):
        x = u * (len(pts) - 1); i = min(int(x), len(pts) - 2); g = smooth(x - i)
        c = pts[i] * (1 - g) + pts[i + 1] * g
        ref = -c / (abs(c) + 1e-9) if abs(c) > 0.2 else 1.0
        return np.angle((z - c) / ref) / np.pi
    return fin(rows(f))


def shattered_heart():
    """SHATTERED HEART — the heart curve (x = 16 sin^3 t, y = 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t) as a
    waveform: frame 0 is a soft, round, subby shape. Then it cracks: 24 fractures appear one by one, the shards
    slip up and down, turn back to front and finally slide out of place — a broken heart of cliffs."""
    rng = np.random.default_rng(2024)
    K = 24
    cuts = np.sort(rng.uniform(0, 1, K)); born = np.sort(rng.uniform(0.08, 0.7, K))
    order = rng.permutation(K); born = born[np.argsort(order)]
    off = rng.normal(0, 0.55, K + 1); rev = rng.random(K + 1) < 0.45; sh = rng.uniform(-0.2, 0.2, K + 1)
    def heart(t):
        th = TAU * t
        return (13 * np.cos(th) - 5 * np.cos(2 * th) - 2 * np.cos(3 * th) - np.cos(4 * th)) / 17.0 + \
            0.35 * 16 * np.sin(th) ** 3 / 16.0
    def f(u):
        act = np.clip((u - born) / 0.12, 0, 1)
        j = np.searchsorted(cuts, T, side='right')
        a = np.concatenate([[0.0], cuts]); b = np.concatenate([cuts, [1.0]])
        g = np.concatenate([[1.0], act])[j] * np.concatenate([act, [1.0]])[j]
        gv = np.maximum(np.concatenate([[0.0], act])[j], np.concatenate([act, [0.0]])[j])
        loc = (T - a[j]) / np.maximum(b[j] - a[j], 1e-9)
        rv = ease(u, 0.45, 0.75) * rev[j]
        tt = a[j] + (b[j] - a[j]) * ((1 - rv) * loc + rv * (1 - loc)) + ease(u, 0.7, 1.0) * sh[j] * gv
        return heart(tt) + off[j] * gv * ease(u, 0.1, 0.6)
    return fin(rows(f))


# ═════════════════════════════ PICTURES READ AS WAVES ═════════════════════════════
def zone_plate():
    """ZONE PLATE — a Fresnel zone plate (rings whose spacing shrinks with radius) cut along a line through an
    off-centre point: frame 0 is one soft bump, the rings multiply (k = 4 -> 380) and harden from shading to black
    and white, so every cycle becomes a chirp of squares racing outwards from the centre."""
    x = 2.0 * T - 1.0
    def f(u):
        k = 4.0 * (95.0 ** u)
        x0 = 0.3 * np.sin(np.pi * u)
        g = 0.6 + 30.0 * ease(u, 0.15, 0.8) ** 2
        return np.tanh(g * np.cos(k * ((x - x0) ** 2 + 0.2 * u)))
    return fin(rows(f))


def penrose_stairs():
    """PENROSE STAIRS — Escher's impossible staircase: four flights round a square courtyard, every step going up, and
    yet the walker arrives back where she started. The wave is her position in isometric view: the square loop (a
    trapezoid) with the stair treads cut into it. 1 -> 12 steps per flight (48 per loop) while the risers grow and
    the view turns, until the courtyard is a saw of teeth."""
    corners = np.array([1 + 1j, -1 + 1j, -1 - 1j, 1 - 1j])
    q = 4.0 * T; fl = np.floor(q).astype(int) % 4; p = q - np.floor(q)
    xy = corners[fl] + (corners[(fl + 1) % 4] - corners[fl]) * p
    def walk(S, u):
        zst = np.floor(4.0 * S * T) - 4.0 * S * T
        h = 0.4 + 1.2 * u
        a = 0.35 + 0.9 * u
        iso = (xy * np.exp(-1j * a)).real * 0.866 + 0.25 * (xy * np.exp(-1j * a)).imag
        return iso + h * zst * (1.0 + 0.5 * np.sin(TAU * T * 4 + u))
    def f(u):
        S = 1.0 + 11.0 * u ** 1.2
        S0 = int(np.floor(S)); g = S - S0
        y = walk(S0, u)
        if g > 0 and S0 < 12:
            y = (1 - g) * y + g * walk(S0 + 1, u)
        return y
    return fin(rows(f))


def nautilus_spiral():
    """NAUTILUS SPIRAL — a pen draws a logarithmic spiral out from the centre at constant speed and retraces it back
    in, so the cycle is a whirl, a wide sweep and a whirl again. Few turns at frame 0 (a soft hump); the spiral
    tightens to 36 turns, so each cycle opens and closes with a chirp that races to the centre of the shell."""
    def f(u):
        r0 = 0.25 * (0.04 ** u)
        b = 0.9 * (0.022 ** u)
        s = 1.0 - np.abs(2.0 * T - 1.0)
        r = r0 + (1 - r0) * s
        return r * np.cos(np.log(r) / b - 0.6 * u)
    return fin(rows(f))


def gear_teeth():
    """GEAR TEETH — the outline of a cog read round its rim. 3 -> 24 teeth (non-integer, so one tooth is always
    half cut), flanks sharpening from bumps to vertical walls, a second meshing gear bolted on and teeth snapping
    off. Frame 0 is a circle with three soft bumps: a sub. The fundamental stays; the top becomes machinery."""
    rng = np.random.default_rng(31)
    miss = rng.random(64) < 0.3
    th = TAU * T
    def f(u):
        n = 3.0 + 21.0 * u
        sh = 1.0 + 40.0 * u ** 1.5
        h = 0.08 + 0.3 * u
        tooth = np.tanh(sh * np.sin(n * th)) / np.tanh(sh)
        k = np.floor(n * T).astype(int) % 64
        tooth = tooth * (1 - ease(u, 0.6, 1.0) * miss[k])
        g2 = ease(u, 0.4, 0.9)
        t2 = np.tanh(sh * np.sin(2.5 * n * th + 1.0)) / np.tanh(sh)
        r = 1.0 + h * tooth + g2 * 0.5 * h * t2
        return r * np.cos(th)
    return fin(rows(f))


def whirl_of_squares():
    """WHIRL OF SQUARES — the classic pursuit whirl: a square, a turned square inscribed in it, another inside that ...
    The pen draws each square and lifts to the next, and the inner squares are magnified toward the end, so the vortex
    never fades. 1 -> 40 squares, the twist per square 0 -> 16 degrees. Frame 0 is one square traced (a trapezoid);
    the end is a spinning well of squares, each faster than the last."""
    def f(u):
        Nf = 1.0 + 39.0 * u ** 1.2
        n = int(np.ceil(Nf))
        lam = 0.01 + 0.29 * u
        corners = np.array([1 + 1j, -1 + 1j, -1 - 1j, 1 - 1j])
        sqs = [corners]
        for _ in range(n - 1):
            c = sqs[-1]
            sqs.append(c + lam * (np.roll(c, -1) - c))
        w = np.array([abs(s[0] - s[1]) for s in sqs]) ** 0.35
        if n > 1:
            w[-1] *= (Nf - (n - 1))
        e = np.concatenate([[0.0], np.cumsum(w)]); e /= e[-1]
        j = np.clip(np.searchsorted(e, T, side='right') - 1, 0, n - 1)
        tau = (T - e[j]) / np.maximum(e[j + 1] - e[j], 1e-12)
        S = np.array(sqs)
        q = tau * 4; qi = np.minimum(q.astype(int), 3); qf = q - qi
        z = S[j, qi] + (S[j, (qi + 1) % 4] - S[j, qi]) * qf
        z = z / (np.abs(S[j, 0]) + 0.15) ** ease(u, 0.3, 1.0)
        return (z * np.exp(-1j * 0.4 * u)).real
    return fin(rows(f))


def spiral_theodorus():
    """SPIRAL OF THEODORUS — right triangles glued hypotenuse to leg, spiralling out (the spokes are sqrt 1, sqrt 2,
    sqrt 3 ...). The pen runs out along each spoke, along the outer edge, and back to the centre. 3 -> 140 triangles:
    frame 0 is a few triangle pulses, the end a whirl of spokes of growing length and turning angle."""
    def f(u):
        N = int(round(3 + 137 * u ** 1.4))
        k = np.arange(1, N + 2)
        ang = np.concatenate([[0.0], np.cumsum(np.arctan(1.0 / np.sqrt(k[:-1])))])
        P = np.sqrt(k) * np.exp(1j * ang)
        pts = np.stack([np.zeros(N), P[:-1], P[1:]], axis=1).reshape(-1)
        z = step_resample(pts)
        z = z / np.sqrt(N + 1)
        return (z * np.exp(-1j * 0.7 * u)).real
    return fin(rows(f))


def voronoi_crystal():
    """VORONOI CRYSTAL — a scanline moving down a Voronoi diagram of 36 crystals. It starts as the distance to the
    nearest seed (a row of soft facets), then each crystal gets its own level (a random stair) and finally the cell
    walls light up as cracks: the waterfall draws the crystal map as it goes."""
    rng = np.random.default_rng(606)
    NSd = 36
    sx = rng.uniform(0, 1, NSd); sy = rng.uniform(0, 1, NSd); lev = rng.uniform(-1, 1, NSd)
    def f(u):
        yy = 0.1 + 0.8 * u
        dx = pdist(T[:, None], sx[None, :]); dy = pdist(yy, sy)[None, :]
        d = np.sqrt(dx * dx + dy * dy)
        o = np.argsort(d, axis=1)[:, :2]
        F1 = np.take_along_axis(d, o[:, :1], 1)[:, 0]; F2 = np.take_along_axis(d, o[:, 1:2], 1)[:, 0]
        a = ease(u, 0.15, 0.65); c = ease(u, 0.55, 1.0)
        return (1 - a) * F1 * 6 + a * lev[o[:, 0]] + c * 1.5 * (np.tanh((F2 - F1) * 60) - 1)
    return fin(rows(f))


# ═════════════════════════════ SOLIDS & KNOTS ═════════════════════════════
def _tesseract():
    V = np.array([[(i >> b) & 1 for b in range(4)] for i in range(16)], dtype=float) * 2 - 1
    adj = {i: [i ^ (1 << b) for b in range(4)] for i in range(16)}
    used = set(); stack = [0]; circ = []
    while stack:
        v = stack[-1]
        nxt = [w for w in adj[v] if (min(v, w), max(v, w)) not in used]
        if nxt:
            w = nxt[0]; used.add((min(v, w), max(v, w))); stack.append(w)
        else:
            circ.append(stack.pop())
    return V, circ


def tesseract_spin():
    """TESSERACT SPIN — a four-dimensional cube, its 32 edges drawn in one unbroken Euler circuit, rotating in the xw and
    yz planes and seen through two perspective projections (4D -> 3D -> 2D) as the camera closes in. The wave slides from
    the pen's POSITION (frame 0: a cube seen square on, a stepped wave) to its HEADING, which snaps at every vertex:
    32 flat steps that tilt and warp as the hypercube turns inside out."""
    V, circ = _tesseract()
    P = V[circ]; n = len(P) - 1
    s = np.arange(M) / M * n
    k = np.minimum(s.astype(int), n - 1); fr_ = (s - k)[:, None]
    X = P[k] * (1 - fr_) + P[k + 1] * fr_
    def f(u):
        a = 0.15 + 2.2 * u; b = 0.1 + 1.4 * u ** 1.3; c = 0.5 * u
        x, y, z, w = X[:, 0], X[:, 1], X[:, 2], X[:, 3]
        x, w = x * np.cos(a) - w * np.sin(a), x * np.sin(a) + w * np.cos(a)
        y, z = y * np.cos(b) - z * np.sin(b), y * np.sin(b) + z * np.cos(b)
        x, y = x * np.cos(c) - y * np.sin(c), x * np.sin(c) + y * np.cos(c)
        Dw = 6.0 - 3.6 * u; Dz = 7.0 - 4.2 * u
        s4 = 1.0 / (Dw - w); x, y, z = x * s4, y * s4, z * s4
        s3 = 1.0 / (Dz - z * 1.5)
        pz = x * s3 + 1j * y * s3
        d = _unit_dir(pz)
        mix = ease(u, 0.1, 0.7)
        pos = (pz * np.exp(-1j * 0.3)).real
        return (1 - mix) * pos / (np.abs(pos).max() + 1e-12) + mix * (d.real + 0.35 * d.imag)
    return fin(rows(f))


def knot_flythrough():
    """KNOT FLYTHROUGH — a (1,7) torus knot (a ring wound seven times round a doughnut), filmed by a camera flying in along
    the doughnut's axis until the seven near crossings almost touch the lens, then tilting away. Frame 0: the knot far
    away (a sine wearing a seven-fold ripple, a sub); on-axis only harmonics 1, 6, 8, 13 ... can sound, so the crown of
    flattened spikes rings like a bell; the final tilt breaks the symmetry and floods the gaps."""
    th = TAU * T
    R0, r0 = 1.0, 0.45
    X0 = (R0 + r0 * np.cos(7 * th)) * np.cos(th); Y0 = (R0 + r0 * np.cos(7 * th)) * np.sin(th); Z0 = r0 * np.sin(7 * th)
    def f(u):
        tl = 0.9 * ease(u, 0.55, 1.0)
        y = Y0 * np.cos(tl) - Z0 * np.sin(tl); z = Y0 * np.sin(tl) + Z0 * np.cos(tl)
        zm = z.max()
        D = zm + 0.008 + (12.0 - zm) * (1 - ease(u, 0.0, 0.6)) ** 2.0
        out = (X0 + 0.35 * y) / (D - z)
        sc = np.percentile(np.abs(out), 90) + 1e-12
        drv = 0.3 + 3.7 * ease(u, 0.3, 1.0)
        return np.tanh(drv * out / sc)
    return fin(rows(f))


def _turtle_tour(s):
    """Parse a bracketed L-system string once. Returns per 'F' segment (left turns, right turns, the segment it
    grows from or -1) and the pen's depth-first TOUR: every F forward and, at ']', the walk back down the branch to
    its fork, so the pen never lifts and the loop closes at the root."""
    L = R = 0; last = -1; stack = []; segs = []; tour = []
    for ch in s:
        if ch == 'F':
            segs.append((L, R, last)); last = len(segs) - 1; tour.append((last, 1))
        elif ch == '+':
            L += 1
        elif ch == '-':
            R += 1
        elif ch == '[':
            stack.append((L, R, last))
        elif ch == ']':
            L, R, back = stack.pop()
            k = last
            while k != back and k >= 0:
                tour.append((k, -1)); k = segs[k][2]
            last = back
    k = last
    while k >= 0:
        tour.append((k, -1)); k = segs[k][2]
    a = np.array(segs, dtype=int); tr = np.array(tour, dtype=int)
    return a[:, 0], a[:, 1], a[:, 2], tr[:, 0], tr[:, 1]


def harmonograph():
    """HARMONOGRAPH — two damped pendulums drawing on one sheet: they start in a 1:2 Lissajous (frame 0 is a sine
    with its octave, a soft sub) and the pens restart at every cycle. The pendulums speed up to 8x, their ratio
    drifts off 1:2 toward 1:3.03 and the damping rises, so each cycle becomes a ringing spiral that dies before the
    restart: the pen's decay IS the wave."""
    def f(u):
        N0 = 1.0 + 7.0 * u ** 1.3
        d = 0.2 + 5.5 * u ** 1.2
        r2 = 2.0 + 1.03 * u
        x = np.sin(TAU * N0 * T) * np.exp(-d * T)
        y = np.sin(TAU * N0 * r2 * T + 0.7 + 2.0 * u) * np.exp(-0.7 * d * T)
        return x + 0.7 * y
    return fin(rows(f))


def _lcurve_counts(axiom, rules, n, draw):
    s = _lsys(axiom, rules, n)
    b = np.frombuffer(s.encode(), dtype=np.uint8)
    turn = (b == ord('+')).astype(np.int64) - (b == ord('-')).astype(np.int64)
    cum = np.cumsum(turn)
    return cum[np.isin(b, [ord(c) for c in draw])]


def _curve_wave(counts, ang):
    z = np.concatenate([[0.0], np.cumsum(np.exp(1j * counts * ang))])
    N = len(z) - 1
    z = z - np.linspace(0.0, 1.0, N + 1) * z[-1]
    zz = np.interp(T * N, np.arange(N + 1), z.real) + 1j * np.interp(T * N, np.arange(N + 1), z.imag)
    return zz / (np.abs(zz).max() + 1e-12)


def gosper_flowsnake():
    """GOSPER FLOWSNAKE — the hexagonal space-filling curve (A -> A-B--B+A++AA+B-, B -> +A-AA++A+B--B-A) laid round a
    hexagon, six snakes nose to tail: a Gosper island. Levels 1 -> 5 (6 x 7 -> 6 x 16807 strokes) morphing into each
    other, and past level 4 the turn angle opens beyond 60 degrees and the island knots itself up. Six-fold symmetry:
    only harmonics 1, 5, 7, 11, 13 ... ever sound."""
    rules = {'A': 'A-B--B+A++AA+B-', 'B': '+A-AA++A+B--B-A'}
    cache = {}
    def lv(n, ang):
        if n not in cache:
            cache[n] = _lcurve_counts('A', rules, n, 'AB')
        P = np.concatenate([[0.0], np.cumsum(np.exp(1j * cache[n] * ang))])
        return step_resample(rosette(P, 6))
    def f(u):
        n = 1.0 + 4.0 * u ** 0.9
        ang = np.radians(60.0 + 24.0 * ease(u, 0.7, 1.0))
        n0 = int(np.floor(n)); g = n - n0
        z = lv(n0, ang)
        if g > 0 and n0 < 5:
            z = (1 - g) * z + g * lv(n0 + 1, ang)
        return (z * np.exp(-1j * 1.2 * u)).real
    return fin(rows(f))


def arrowhead_curve():
    """ARROWHEAD CURVE — Sierpinski's arrowhead (A -> B-A-B, B -> A+B+A): one unbroken line that fills a triangle with
    triangles. Levels 1 -> 9 (3 -> 19683 strokes); then the turn angle bends 60 -> 118 degrees and the triangle
    folds into a spiky star of self-crossings. Frame 0: a three-stroke hook."""
    rules = {'A': 'B-A-B', 'B': 'A+B+A'}
    cache = {}
    def lv(n, ang):
        if n not in cache:
            cache[n] = _lcurve_counts('A', rules, n, 'AB')
        return _curve_wave(cache[n], ang)
    def f(u):
        n = 1.0 + 8.0 * ease(u, 0.0, 0.6)
        ang = np.radians(60.0 + 58.0 * ease(u, 0.6, 1.0) ** 1.2)
        n0 = int(np.floor(n)); g = n - n0
        z = lv(n0, ang)
        if g > 0 and n0 < 9:
            z = (1 - g) * z + g * lv(n0 + 1, ang)
        z = z * np.exp(-1j * (0.3 + 0.8 * u))
        return z.imag + 0.5 * z.real
    return fin(rows(f))


def cantor_dust():
    """CANTOR DUST — remove the middle of the cycle, then the middle of what is left, and again; every surviving grain
    is a smooth bump, and the grains flip sign by their binary address (a Thue-Morse pattern). Levels 1 -> 7 and the
    removed fraction grows 1/3 -> 0.6. The Cantor set's spectrum never dies away: its peaks at 3, 9, 27, 81 ... stay loud
    however far up you listen. Frame 0: one notch, a soft pulse wave."""
    def intervals(g, L):
        a = np.array([0.0]); b = np.array([1.0])
        for _ in range(L):
            wdt = (b - a) * (1.0 - g) / 2.0
            a, b = np.stack([a, b - wdt], 1).reshape(-1), np.stack([a + wdt, b], 1).reshape(-1)
        return a, b
    def lvl(g, L, flip):
        a, b = intervals(g, L)
        cc = 0.5 * (a + b); ww = np.maximum(b - a, 0.005)
        tm = np.array([bin(i).count('1') % 2 for i in range(len(a))])
        y = np.zeros(M)
        for i in range(len(a)):
            i0 = int(np.floor((cc[i] - ww[i] / 2) * M)); i1 = int(np.ceil((cc[i] + ww[i] / 2) * M)) + 1
            idx = np.arange(i0, i1) % M
            tt = np.clip((pdist(T[idx], cc[i]) / ww[i]) + 0.5, 0.0, 1.0)
            y[idx] += (0.5 - 0.5 * np.cos(TAU * tt)) * (1.0 - 2.0 * flip * tm[i])
        return y
    def f(u):
        L = 1.0 + 6.0 * u ** 0.9
        g = 1.0 / 3.0 + (0.6 - 1.0 / 3.0) * ease(u, 0.25, 1.0)
        fl = ease(u, 0.2, 0.7)
        L0 = int(np.floor(L)); fr_ = L - L0
        y = lvl(g, L0, fl)
        if fr_ > 0 and L0 < 7:
            y = (1 - fr_) * y + fr_ * lvl(g, L0 + 1, fl)
        return np.roll(y, int(0.17 * M))
    return fin(rows(f))


def truchet_weave():
    """TRUCHET WEAVE — Truchet tiles: every square tile carries two quarter-circle arcs, turned one way or the other at
    random, and together they weave endless meandering ropes. A scanline moves down the weave while the tiles shrink
    (3 -> 16 per cycle): frame 0 crosses a few fat arcs (a pulse wave), the end threads through a dense braid."""
    rng = np.random.default_rng(5)
    ori = rng.random((64, 64)) < 0.5
    def render(G, y0):
        xg = T * G
        i = np.floor(xg).astype(int) % 64; fx = xg - np.floor(xg)
        yg = y0 * G; j = int(np.floor(yg)) % 64; fy = yg - np.floor(yg)
        o = ori[i, j]
        fxx = np.where(o, 1.0 - fx, fx)
        d = np.minimum(np.abs(np.hypot(fxx, fy) - 0.5), np.abs(np.hypot(1.0 - fxx, 1.0 - fy) - 0.5))
        return np.tanh((0.14 - d) * 60.0)
    def f(u):
        G = 3.0 + 13.0 * u
        y0 = 0.13 + 0.61 * u
        G0 = int(np.floor(G)); g = G - G0
        y = render(G0, y0)
        if g > 0 and G0 < 16:
            y = (1 - g) * y + g * render(G0 + 1, y0)
        return y
    return fin(rows(f))


def quasicrystal():
    """QUASICRYSTAL — five plane waves at 72 degrees make a pattern with five-fold symmetry that never repeats (a
    Penrose-like quasicrystal). The wave walks a circle through it, off-centre; the pattern's scale shrinks
    (k = 1.5 -> 60) and its contrast hardens into black-and-white tiles. Frame 0 is a gentle wobble."""
    ths = TAU * np.arange(5) / 5
    def f(u):
        k = 1.5 * (40.0 ** u)
        c = 0.35 * np.exp(1j * (0.5 + 2.0 * u))
        p = c + 0.9 * np.exp(1j * TAU * T)
        q = sum(np.cos(k * (p * np.exp(-1j * t)).real) for t in ths)
        g = 0.4 + 6.0 * ease(u, 0.2, 0.9) ** 1.5
        return np.tanh(g * q)
    return fin(rows(f))


def halftone_screen():
    """HALFTONE SCREEN — a newspaper halftone: a picture (a ringed blob) printed as dots on a 45-degree screen, the
    dot size carrying the grey level. Each frame is a scanline across the print while the screen gets finer
    (pitch 0.22 -> 0.012): frame 0 is a few fat dots (pulses), the end a fizz of tiny dots swelling and shrinking."""
    x = 2.0 * T - 1.0
    s2 = np.sqrt(2.0)
    def f(u):
        a = 0.22 * (0.012 / 0.22) ** u
        y0 = 0.35 - 0.5 * u
        X = (x + y0) / (s2 * a); Y = (y0 - x) / (s2 * a)
        cx, cy = np.round(X), np.round(Y)
        wx = (cx - cy) * a / s2; wy = (cx + cy) * a / s2
        rr = np.hypot(wx, wy)
        I = np.exp(-rr * rr * 2.2) * (0.62 + 0.38 * np.cos(9.0 * rr))
        rad = 0.75 * np.sqrt(np.clip(I, 0.0, 1.0))
        return np.where(np.hypot(X - cx, Y - cy) < rad, 1.0, -1.0)
    return fin(rows(f))


def contour_map():
    """CONTOUR MAP — a hilly landscape (eight random ridges on a torus) sliced along a moving line. Frame 0 is the
    plain height profile (a soft sub-heavy wave); then it is cut into contour ribbons, 1 -> 14 per unit height,
    each ribbon ramping from one contour line to the next: the terrain turns into a topographic buzz."""
    rng = np.random.default_rng(88)
    kv = rng.integers(-3, 4, (8, 2)); kv[kv[:, 0] == 0, 0] = 1
    amp = rng.normal(size=8) / (1.0 + np.abs(kv).sum(1)); ph = rng.uniform(0, TAU, 8)
    def hgt(x, y):
        return sum(amp[i] * np.cos(TAU * (kv[i, 0] * x + kv[i, 1] * y) + ph[i]) for i in range(8))
    nrm = np.abs(hgt(T[:, None], np.linspace(0, 1, 64)[None, :])).max()
    def f(u):
        hh = hgt(T, 0.1 + 0.8 * u) / nrm
        K = 0.5 + 13.5 * u ** 1.5
        m = ease(u, 0.08, 0.5)
        return (1 - m) * hh + m * (frac(K * hh) - 0.5) * (0.6 + 0.4 * u)
    return fin(rows(f))


def canyon_orbit():
    """CANYON ORBIT — wave-terrain synthesis: a point circles over a height field of ridges and one sheer terrace line
    (T = sin 3x cos 2y + 0.5 sin 5xy + 0.25 cos 13(x - y) + 0.08 cos 29xy, plus a cliff that rises in mid-table), and
    the height under it is the wave. The orbit widens 0.15 -> 5 while it drifts off-centre: frame 0 circles a gentle
    hilltop (a sub), the end tears across crinkled canyons and falls off the cliff edges."""
    def f(u):
        r = 0.15 * ((5.0 / 0.15) ** u)
        ox = 0.6 * np.sin(np.pi * u) + 0.2; oy = -0.3 * u
        x = ox + r * np.cos(TAU * T); y = oy + 0.55 * r * np.sin(TAU * T + 0.4 * u)
        return (np.sin(3 * x) * np.cos(2 * y) + 0.5 * np.sin(5 * x * y) + 0.25 * np.cos(13 * (x - y))
                + 0.15 * np.sign(np.sin(7 * x + 3 * y)) * ease(u, 0.3, 0.9) + 0.08 * np.cos(29 * x * y))
    return fin(rows(f))


def ripple_tank():
    """RIPPLE TANK — four point sources drop circular waves into a tank; a probe circles the tank and the water height
    it meets is the wave. The wavenumber climbs 0.5 -> 700: frame 0 is a slow swell (a sub), then the rings multiply
    and interfere into chirping ripples that bunch up wherever the probe passes a source."""
    src = np.array([0.35 + 0.2j, -0.4 + 0.3j, 0.1 - 0.5j, -0.2 - 0.1j]); phs = np.array([0.0, 1.3, 2.1, 4.0])
    p = 1.2 * np.exp(1j * TAU * T)
    dist = np.abs(p[None, :] - src[:, None])
    def f(u):
        k = 0.5 * (1400.0 ** u)
        return (np.cos(k * dist + phs[:, None]) / (0.3 + dist)).sum(0)
    return fin(rows(f))


def bubble_raft():
    """BUBBLE RAFT — a raft of soap bubbles (80 domes of random size, half of them dimples pressed downward) seen in
    cross-section as a scanline moves across it; the bubbles inflate as it goes (x0.7 -> x2), crowding and swallowing
    each other, and the dimples deepen. Each dome has vertical walls, so the wave is a row of rounded pulses with hard
    feet. Frame 0 crosses three small bubbles."""
    rng = np.random.default_rng(909)
    NB = 80
    bx, by = rng.uniform(0, 1, NB), rng.uniform(0, 1, NB); br = rng.uniform(0.035, 0.13, NB)
    sg = np.where(rng.random(NB) < 0.5, 1.0, -1.0)
    bx[:3] = [0.18, 0.5, 0.77]; by[:3] = [0.1, 0.12, 0.09]; br[:3] = [0.07, 0.09, 0.06]; sg[:3] = [1, -1, 1]
    def f(u):
        y0 = 0.1 + 0.8 * u
        sc = 0.7 + 1.3 * u
        up = np.zeros(M); dn = np.zeros(M)
        for i in range(NB):
            r = br[i] * sc
            dy = pdist(y0, by[i])
            if abs(dy) >= r:
                continue
            hc2 = r * r - dy * dy
            dx = pdist(T, bx[i])
            dome = np.sqrt(np.maximum(hc2 - dx * dx, 0.0)) + (dx * dx < hc2) * 0.02
            if sg[i] > 0:
                up = np.maximum(up, dome)
            else:
                dn = np.maximum(dn, dome)
        return up - (0.3 + 0.7 * ease(u, 0.0, 0.5)) * dn
    return fin(rows(f))


def dropped_ball():
    """DROPPED BALL — a ball thrown up and left to bounce: arches that shrink by the restitution e at every bounce and
    come faster and faster until it rests. e = 0.35 (a few thuds) -> 0.97 (a rattle of 200 bounces). The wave slides
    from the ball's HEIGHT (frame 0: parabolic arches) to its VELOCITY, which flips at every impact: a sawtooth that
    accelerates into a buzz."""
    J = 220
    def f(u):
        e = 0.35 + 0.62 * u ** 0.8
        d = e ** np.arange(J); d = d / d.sum() * 0.9
        ed = np.concatenate([[0.0], np.cumsum(d)])
        j = np.clip(np.searchsorted(ed, T, side='right') - 1, 0, J)
        rest = j >= J
        jj = np.minimum(j, J - 1)
        tau = np.clip((T - ed[jj]) / d[jj], 0.0, 1.0)
        h = np.where(rest, 0.0, e ** (2 * jj) * 4.0 * tau * (1.0 - tau))
        v = np.where(rest, 0.0, e ** jj * (1.0 - 2.0 * tau))
        mix = ease(u, 0.25, 0.85)
        return (1 - mix) * h + mix * 0.8 * v
    return fin(rows(f))


def razor_serration():
    """RAZOR SERRATION — a smooth window bump (frame 0: a Gaussian hump, almost a sine) ground into a razor: it narrows,
    its trailing edge is honed to a vertical cut, and then the blade is serrated into 10 teeth with alternating bevels.
    The window never stops being one blade, just a sharper and more jagged one."""
    def f(u):
        B = 1.0 + 9.0 * ease(u, 0.35, 1.0) ** 1.2
        w = 0.42 * (0.26 / 0.42) ** ease(u, 0.0, 0.5)
        asym = ease(u, 0.1, 0.45)
        ph = T * B; j = np.floor(ph); tau = ph - j
        xx = (tau - 0.6) / w
        s = 1.0 - 0.985 * asym
        prof = np.where(xx < 0, np.exp(-np.pi * xx * xx), np.exp(-np.pi * (xx / s) ** 2))
        hgt = 1.0 - 0.35 * j / max(B, 1.0)
        bev = 1.0 - 2.0 * ease(u, 0.6, 0.95) * (j % 2)
        return prof * hgt * bev
    return fin(rows(f))


def pentagram_velocity():
    """PENTAGRAM VELOCITY — the pen tracing a polygon, but we hear its DIRECTION: rounding every corner fully is a
    circle (frame 0: a sine); as the corners sharpen it becomes a pentagon (five flat steps), then the turn per
    corner grows into star polygons {5/2}, {7/3} ... and past 5 edges the stars stop closing: stepped, aliased
    sines tumbling through each other."""
    def f(u):
        n = 5.0 + 6.0 * ease(u, 0.55, 1.0)
        m = 1.0 + 3.5 * ease(u, 0.3, 1.0)
        wt = (1.0 - ease(u, 0.0, 0.3)) * 0.999 + 1e-3
        x = T * n; k = np.floor(x); fx = x - k
        s = np.clip(fx / wt, 0.0, 1.0)
        head = TAU * m / n * (k + s)
        return np.cos(head) + 0.35 * np.sin(head)
    return fin(rows(f))


def circle_inversion():
    """CIRCLE INVERSION — a square traced round and round, reflected through a circle whose centre walks toward it:
    far away the reflection is a small square (frame 0: a trapezoid wave), as the centre nears an edge that edge
    balloons toward infinity, and once inside, the square is turned inside out, its corners flung out as spikes."""
    sqp = step_resample(np.array([1 + 1j, -1 + 1j, -1 - 1j, 1 - 1j]))
    pts = [3.2 + 0.6j, 1.8 + 0.45j, 1.12 + 0.3j, 0.75 + 0.2j, 0.3 + 0.12j]
    def f(u):
        x = u * (len(pts) - 1); i = min(int(x), len(pts) - 2); g = smooth(x - i)
        c = pts[i] * (1 - g) + pts[i + 1] * g
        w = 1.0 / np.conj(sqp - c)
        v = (w * np.exp(-1j * 0.5 * u)).real
        v = v - np.median(v)
        sc = np.percentile(np.abs(v), 92) + 1e-12
        return np.tanh(v / sc)
    return fin(rows(f))


def epicycle_chain():
    """EPICYCLE CHAIN — a Fourier drawing: arms spinning at 1, -4, 6, -9, 11, -14, 16, -19 turns per cycle (all 1 mod 5,
    so the figure has five-fold symmetry), the pen at the tip, drawn at constant pen speed. Frame 0 is one arm (a
    circle, a sine); arms join one by one and lengthen until the pen stalls and flips at cusps: an eight-arm orrery
    scribbling a five-petalled flower."""
    fq = np.array([1, -4, 6, -9, 11, -14, 16, -19]); rd = np.array([1.0, 0.5, 0.4, 0.3, 0.25, 0.2, 0.17, 0.14])
    th = TAU * np.arange(4 * M) / (4 * M)
    def f(u):
        J = 1.0 + 7.0 * u ** 0.9
        g = np.clip(J - np.arange(8), 0.0, 1.0) * (1.0 + 1.4 * u * np.arange(8) / 7)
        dz = sum(g[i] * rd[i] * 1j * fq[i] * np.exp(1j * fq[i] * th) for i in range(8) if g[i] > 0)
        sp = np.abs(dz) + 1e-9
        s = np.concatenate([[0.0], np.cumsum(sp[:-1])]); s /= s[-1] + sp[-1]
        ts = np.interp(T, s, th)
        z = sum(g[i] * rd[i] * np.exp(1j * fq[i] * ts) for i in range(8) if g[i] > 0)
        return (z * np.exp(-1j * 0.4 * u)).real
    return fin(rows(f))


def starburst():
    """STARBURST — a star read ray by ray: r(th) = 1 + A |cos(N th/2)|^p. Frame 0 is a round triangle (a sub-heavy
    sine); the points multiply 3 -> 40 (non-integer, so one point is always half-formed), the exponent p 1 -> 61
    sharpens them into needles and they grow long: a sine wearing a crown of spikes."""
    th = TAU * T
    def f(u):
        N = 3.0 + 37.0 * u ** 1.2
        p = 1.0 + 60.0 * u ** 1.5
        A = 0.25 + 1.25 * u
        r = 1.0 + A * np.abs(np.cos(N * th / 2.0)) ** p
        return r * np.cos(th)
    return fin(rows(f))


def crossing_sines():
    """CROSSING SINES — line art of crossing sine curves, heard as their upper silhouette: the wave is the highest of
    K sines at every instant, so it jumps from curve to curve at each crossing (a corner). K = 1 -> 16 rising in one by
    one from below, sliding in phase. Frame 0 is a pure sine; the end is a scalloped crest of sixteen curves."""
    rng = np.random.default_rng(333)
    K = 16
    hk = rng.integers(2, 24, K); hk[0] = 1
    ak = 1.0 / hk ** 0.25; pk = rng.uniform(0, TAU, K); dk = rng.normal(0, 2.0, K)
    def f(u):
        Kf = 1.0 + 15.0 * u ** 1.2
        y = np.full(M, -10.0)
        for k in range(int(np.ceil(Kf))):
            g = float(np.clip(Kf - k, 0.0, 1.0))
            y = np.maximum(y, ak[k] * np.sin(TAU * hk[k] * T + pk[k] + dk[k] * u) - 2.5 * (1.0 - g))
        return y
    return fin(rows(f))


def spirolateral():
    """SPIROLATERAL — a turtle walks 1, 2, 3 ... n steps, turning the same angle after each, and repeats the whole run
    six times. n = 3 -> 9 and the angle 90 -> 160 degrees: square spirals, woven knots and starry mazes appear and
    fall apart. The pen's offset from its own path is the wave."""
    def draw(n, ang):
        L = np.tile(np.arange(1, n + 1), 6).astype(float)
        head = ang * np.arange(len(L))
        P = np.concatenate([[0.0], np.cumsum(L * np.exp(1j * head))])
        z = arc_resample(P, closed=False)
        z = z - T * (P[-1] - P[0])
        return z / (np.abs(z).max() + 1e-12)
    def f(u):
        n = 3.0 + 6.0 * u
        ang = np.radians(90.0 + 70.0 * u)
        n0 = int(np.floor(n)); g = n - n0
        z = draw(n0, ang)
        if g > 0 and n0 < 9:
            z = (1 - g) * z + g * draw(n0 + 1, ang)
        return z.real + 0.5 * z.imag
    return fin(rows(f))


def rosette(P, n, outward=True):
    """Open curve P (complex, P[0] -> P[-1]) laid on every side of a regular n-gon: a closed curve with n-fold
    rotational symmetry (the Koch-snowflake construction). n-fold symmetry keeps only harmonics k = +-1 (mod n) in the
    traced x-coordinate, so every rosette has its own harmonic lattice."""
    P = np.asarray(P, dtype=complex)
    c = (P - P[0]) / (P[-1] - P[0])
    if outward:
        c = np.conj(c)
    V = np.exp(1j * TAU * np.arange(n) / n)
    return np.concatenate([V[j] + (V[(j + 1) % n] - V[j]) * c[:-1] for j in range(n)])


def _moore(n):
    return _lcurve_counts('LFL+F+LFL', {'L': '-RF+LFL+FR-', 'R': '+LF-RFR-FL+'}, n, 'F')


def sunflower_spirals():
    """SUNFLOWER SPIRALS — a sunflower head of 1500 seeds on the golden angle, read round a circle that widens from near
    the centre to the rim, off-centre over the dome of the head. Every seed it crosses is a small dome, and the seeds
    line up in the flower's spiral families, so the pulse rate locks to the Fibonacci numbers (13, 21, 34, 55, 89 ...)
    and jumps from one to the next as the circle grows."""
    N = 1500
    k = np.arange(1, N + 1)
    ga = np.pi * (3.0 - np.sqrt(5.0))
    sr = np.sqrt(k / N); sa = k * ga
    S = sr * np.exp(1j * sa)
    rs = 0.017
    def f(u):
        rho = 0.07 + 0.87 * u
        c = (0.2 * (1 - u) + 0.04) * np.exp(1j * 0.7)
        p = c + rho * np.exp(1j * TAU * T)
        y = 0.8 * np.sqrt(np.maximum(1.0 - np.abs(p) ** 2, 0.0))
        near = np.where(np.abs(np.abs(S - c) - rho) < rs * 1.2)[0]
        for i in near:
            d = np.abs(p - S[i])
            y = np.maximum(y, 0.8 * np.sqrt(max(1.0 - sr[i] ** 2, 0.0))
                           + np.sqrt(np.maximum(rs * rs - d * d, 0.0)) / rs * (0.3 + 0.4 * u))
        return y
    return fin(rows(f))


def facet_prism():
    """FACET PRISM — phase multiplication: the cycle is cut into m facets (1 -> 24, fractional, so a new facet is always
    being born) and every facet replays a triangle whose apex leans a different way, like light bent by the faces of
    a prism, with a bright band of tall facets sweeping across. Frame 0: one triangle. End: a crystal of 24 leaning
    teeth."""
    def f(u):
        m = 1.0 + 23.0 * u ** 1.3
        ph = T * m; j = np.floor(ph); tau = ph - j
        lean = 0.5 + 0.46 * np.sin(TAU * (0.37 * j + 0.8 * u)) * ease(u, 0.05, 0.4)
        y = np.where(tau < lean, -1.0 + 2.0 * tau / np.maximum(lean, 1e-6), 1.0 - 2.0 * (tau - lean) / np.maximum(1.0 - lean, 1e-6))
        amp = 0.35 + 0.65 * np.cos(np.pi * (j / max(m, 1.0) - 0.5 - 0.4 * np.sin(TAU * u))) ** 2
        return y * amp
    return fin(rows(f))


def pixel_star():
    """PIXEL STAR — a pentagram rasterised on a grid of square pixels and scanned row by row, top to bottom, so the
    waterfall draws the star. The grid refines as the scan descends (8 -> 72 pixels across): frame 0 is the chunky tip
    (a blocky pulse), the middle rows split into the star's arms, and the fine pixel steps put a comb of nulls into the
    spectrum that climbs with the resolution."""
    V = np.exp(1j * (np.pi / 2 + TAU * np.arange(5) * 2 / 5))
    def inside(X, Y):
        z = X + 1j * Y
        wn = np.zeros(z.shape)
        for i in range(5):
            wn += np.angle((V[(i + 1) % 5] - z) / (V[i] - z))
        return np.abs(wn) > np.pi
    def f(u):
        R = int(round(8 * (9.0 ** u)))
        # the scan ends one row earlier: down to y = -0.70 it is the original 1.55/cycle scan, then it slows to stop on
        # the last row that still crosses both bottom tips at the final 72-pixel grid (centre -0.75). The old end, -0.83,
        # ran into empty rows below the tips and the last frames fell to -19 dB RMS.
        uk = (0.72 + 0.70) / 1.55
        yrow = 0.72 - 1.55 * u if u <= uk else -0.70 - 0.045 * (u - uk) / (1.0 - uk)
        cells = (np.arange(R) + 0.5) / R * 2.4 - 1.2
        py = (np.floor((yrow + 1.2) / 2.4 * R) + 0.5) / R * 2.4 - 1.2
        v = inside(cells, np.full(R, py)).astype(float)
        if v.sum() == 0:
            v[R // 2] = 1.0
        xi = np.minimum((T * R).astype(int), R - 1)
        return v[xi] * 2.0 - 1.0
    return fin(rows(f))


def moire_fringes():
    """MOIRE FRINGES — two line gratings of almost the same pitch laid over each other: where their lines coincide
    and where they alternate, big slow fringes appear (the moire). The wave is one scan across both, their product
    plus a little of each. Pitch 3 -> 64 lines, the mismatch 1 -> 5 fringes, the lines hardening from soft sines to
    black bars: frame 0 is a gentle beat, the end a fast grating throbbing in five fringes."""
    def f(u):
        fq = int(round(3 + 61 * u ** 1.4))
        B = 1 + int(round(4 * ease(u, 0.3, 1.0)))
        s = 1.0 + 9.0 * ease(u, 0.0, 0.7)
        g1 = np.tanh(s * np.sin(TAU * fq * T)); g2 = np.tanh(s * np.sin(TAU * (fq + B) * T + 0.3 + u))
        return g1 * g2 + 0.35 * (g1 + g2) * (1 - ease(u, 0.2, 0.8))
    return fin(rows(f))


def butterfly_curve():
    """BUTTERFLY CURVE — Temple Fay's transcendental butterfly, r = e^cos(th) - 2 cos 4th + sin^5(th/12). Frame 0 traces
    one turn of the bare e^cos(th) loop (a soft sub); the wing term and the slow sin^5 term fade in while the pen
    traces more and more turns (1 -> 12), until each cycle is the whole butterfly, twelve wing-beats of loops."""
    def f(u):
        Tn = 1.0 + 11.0 * ease(u, 0.1, 0.9)
        c4 = ease(u, 0.0, 0.4); c5 = ease(u, 0.2, 0.8)
        th = TAU * Tn * T
        r = np.exp(np.cos(th)) - 2.0 * c4 * np.cos(4 * th) + c5 * np.sin(th / 12.0) ** 5
        z = r * np.exp(1j * th)
        return (z * np.exp(-1j * (0.3 + 0.8 * u))).real
    return fin(rows(f))


def maurer_rose():
    """MAURER ROSE — a rose curve r = sin(n th) visited at 361 points spaced d degrees apart and joined by straight
    lines. At d = 1 the lines trace the rose itself (frame 0: a four-petal rose, a soft wave); as d grows toward 119
    the pen leaps across the flower on every step and the lines weave the famous Maurer lace, while the rose's petals
    multiply n = 2 -> 7."""
    k = np.arange(361)
    def pts(n, d):
        th = np.radians(k * d)
        return np.sin(n * th) * np.exp(1j * th)
    def f(u):
        d = 1.0 + 118.0 * u ** 1.3
        n = 2.0 + 5.0 * ease(u, 0.2, 1.0)
        n0 = int(np.floor(n)); g = n - n0
        P = pts(n0, d)
        if g > 0:
            P = (1 - g) * P + g * pts(n0 + 1, d)
        z = step_resample(P[:-1])
        return (z * np.exp(-1j * 0.4 * u)).imag + 0.3 * z.real
    return fin(rows(f))


def _cantor_fn(x, L, g):
    """The Cantor function (devil's staircase) of the middle-g Cantor set, L levels, then linear."""
    a = (1.0 - g) / 2.0
    y = np.zeros_like(x); xx = x.copy(); done = np.zeros(x.shape, bool); sc = 1.0
    for _ in range(L):
        left = xx < a; right = xx >= 1.0 - a
        mid = ~(left | right) & ~done
        y[mid] += sc * 0.5; done |= mid
        y[right & ~done] += sc * 0.5
        xx = np.where(left, xx / a, np.where(right, (xx - (1.0 - a)) / a, xx))
        sc *= 0.5
    y[~done] += sc * xx[~done]
    return y


def cantor_clock():
    """CANTOR CLOCK — a sine whose clock only runs on the Cantor set: its phase is the devil's staircase, so it freezes
    across every removed gap and races through the dust in between. Frame 0 is a plain sine (the clock runs evenly);
    the staircase grows 0 -> 8 levels, the sine is wound 1 -> 12 times onto it and the gaps widen (1/3 -> 0.73): frozen
    plateaus split by ever faster flurries."""
    def f(u):
        L = 8.0 * ease(u, 0.0, 0.6)
        N = 1.0 + 11.0 * ease(u, 0.3, 1.0) ** 1.2
        g = 1.0 / 3.0 + 0.4 * ease(u, 0.4, 1.0)
        L0 = int(np.floor(L)); fr_ = L - L0
        C = _cantor_fn(T, L0, g)
        if fr_ > 0:
            C = (1 - fr_) * C + fr_ * _cantor_fn(T, L0 + 1, g)
        N0 = int(np.floor(N)); gN = N - N0
        y = np.sin(TAU * N0 * C)
        if gN > 0:
            y = (1 - gN) * y + gN * np.sin(TAU * (N0 + 1) * C)
        return y
    return fin(rows(f))


def terdragon_rosette():
    """TERDRAGON ROSETTE — the terdragon (fold a strip in thirds, again and again, every crease at 120 degrees:
    F -> F+F-F), levels 1 -> 9 (3 -> 19683 strokes), nine of them laid round a nonagon and folded inward; past level 9
    the crease angle opens to 160 degrees and the ring crumples. Nine-fold symmetry: harmonics 1, 8, 10, 17, 19 ... only,
    a hollow ringing lattice."""
    cnt = {n: _lcurve_counts('F', {'F': 'F+F-F'}, n, 'F') for n in range(1, 10)}
    def lv(n, ang):
        P = np.concatenate([[0.0], np.cumsum(np.exp(1j * cnt[n] * ang))])
        return step_resample(rosette(P, 9, outward=False))
    def f(u):
        n = 1.0 + 8.0 * ease(u, 0.0, 0.6)
        ang = np.radians(120.0 + 40.0 * ease(u, 0.6, 1.0))
        n0 = int(np.floor(n)); g = n - n0
        z = lv(n0, ang)
        if g > 0 and n0 < 9:
            z = (1 - g) * z + g * lv(n0 + 1, ang)
        return (z * np.exp(-1j * 0.7 * u)).real
    return fin(rows(f))


def euler_spiral():
    """EULER SPIRAL — the clothoid (Cornu spiral), whose curvature grows with its length, so it curls tighter and tighter
    into two eyes. The pen runs from one eye to the other and back along the mirrored spiral. Its length grows 2 -> 18,
    so each cycle becomes a chirp that winds up into each eye and unwinds again (the road engineer's easement curve,
    pushed until it spins)."""
    def f(u):
        S = 2.0 + 16.0 * u ** 1.3
        s = np.linspace(-S, S, 4 * M)
        ds = s[1] - s[0]
        ph = np.pi * s * s / 2.0
        x = np.cumsum(np.cos(ph)) * ds; y = np.cumsum(np.sin(ph)) * ds
        x -= x[len(x) // 2]; y -= y[len(y) // 2]
        z = (x + 1j * y)[::8]
        z = np.concatenate([z, -z[::-1] * np.exp(1j * 0.9 * u)])
        return (z * np.exp(-1j * (0.5 + 0.9 * u))).real + 0.5 * z.imag
    return fin(rows(f))


def nonagram():
    """NONAGRAM — the pen runs round nine points: frame 0 is the circle through them (a sine), the corners sharpen into a
    nonagon, and then the pen skips 1 -> 4 points per stroke, through the stars {9/2}, {9/3}, {9/4}. A regular star
    {9/m} only sounds harmonics m and 9-m, 9+m ... — no fundamental at all — so the table walks from a sine into
    hollow, pitch-shifted lattices, cracking open between the stars where the figure stops closing."""
    n = 9
    def f(u):
        m = 1.0 + 3.0 * ease(u, 0.15, 1.0)
        rnd = 1.0 - ease(u, 0.0, 0.2)
        x = T * n; k = np.floor(x); fx = x - k
        A = np.exp(1j * TAU * m * k / n); B = np.exp(1j * TAU * m * (k + 1) / n)
        z = rnd * np.exp(1j * TAU * m * (k + fx) / n) + (1 - rnd) * (A + (B - A) * fx)
        return (z * np.exp(-1j * 0.3 * u)).real
    return fin(rows(f))


def op_art_current():
    """OP ART CURRENT — Bridget Riley's rippling stripes: black and white bands whose spacing is bent by a wave, read
    across the canvas. Frame 0 is one soft band (a sine); the stripes multiply (1 -> 41), harden into black and white,
    and the ripple bending them deepens, so the bands bunch and fan out along the cycle: a square wave being squeezed
    and stretched by the painting itself."""
    def f(u):
        k = 1.0 + 40.0 * u ** 1.4
        A = 2.5 * ease(u, 0.1, 1.0)
        m = 1 + int(round(3 * u))
        s = 1.0 + 12.0 * ease(u, 0.0, 0.7)
        ph = np.round(k) * T + A * np.sin(TAU * m * T) / TAU * 3.0
        return np.tanh(s * np.cos(TAU * ph))
    return fin(rows(f))


def diffraction_grating():
    """DIFFRACTION GRATING — light through S slits lands on a screen as fringes; the wave is the field across the screen,
    sin(S b)/(S sin b) under the single-slit sinc envelope. Frame 0 is the double slit (soft cos fringes); the slits
    multiply 2 -> 12, the fringes sharpen into bright principal lines with ripples of subsidiary maxima between, and the
    envelope widens so more orders crowd in."""
    x = 2.0 * T - 1.0
    def amp(S, F, a):
        be = np.pi * F * x
        sb = np.sin(be)
        return np.sin(S * be) / (S * np.where(np.abs(sb) < 1e-9, 1e-9, sb)) * np.sinc(a * F * x)
    def f(u):
        S = 2.0 + 10.0 * u ** 1.1
        F = 1.5 + 5.5 * u
        a = 0.9 - 0.75 * u
        S0 = int(np.floor(S)); g = S - S0
        y = amp(S0, F, a)
        if g > 0:
            y = (1 - g) * y + g * amp(S0 + 1, F, a)
        return np.tanh(2.2 * y / (np.abs(y).max() + 1e-12))
    return fin(rows(f))


def _unit_dir(z):
    """Unit direction of travel of a sampled closed path (the pen's heading as a complex number)."""
    v = np.diff(np.append(z, z[0]))
    return v / (np.abs(v) + 1e-12)


def lissajous_pen():
    """LISSAJOUS PEN — a Lissajous figure (sin(a t + d), sin(b t)) drawn at constant pen speed while the ratio a:b steps
    through 1:2, 2:3, 3:4, 3:5, 4:5, 5:7, 7:9 and the phase d drifts. The wave slides from the pen's position (frame 0:
    the 1:2 figure, a soft wave) to its heading, which whips round at every edge of the figure's box: the denser the
    figure, the more whip-cracks per cycle."""
    th = TAU * np.arange(4 * M) / (4 * M)
    pairs = [(1, 2), (2, 3), (3, 4), (3, 5), (4, 5), (5, 7), (7, 9)]
    def one(a, b, dl):
        sp = np.hypot(a * np.cos(a * th + dl), b * np.cos(b * th)) + 1e-9
        s = np.concatenate([[0.0], np.cumsum(sp[:-1])]); s /= s[-1] + sp[-1]
        ts = np.interp(T, s, th)
        return np.sin(a * ts + dl) + 1j * np.sin(b * ts)
    def f(u):
        x = u * (len(pairs) - 1); i0 = min(int(x), len(pairs) - 2); g = smooth(x - i0)
        dl = np.pi / 4 + 1.5 * u
        z = one(*pairs[i0], dl)
        if g > 0:
            z = (1 - g) * z + g * one(*pairs[i0 + 1], dl)
        d = _unit_dir(z)
        mix = ease(u, 0.0, 0.5)
        return (1 - mix) * (z * np.exp(-1j * 0.5)).real + mix * (d * np.exp(-1j * 0.5)).real
    return fin(rows(f))


def peano_serpent():
    """PEANO SERPENT — Peano's original space-filling curve (X -> XFYFX+F+YFXFY-F-XFYFX), the serpentine that fills a
    square by folding three-by-three, levels 2 -> 4 (81 -> 6561 strokes), turning in view; past level 4 its right angles
    open 90 -> 140 degrees and the serpent coils over itself. Wild from the first frame: a meander of 81 steps."""
    cache = {n: _lcurve_counts('X', {'X': 'XFYFX+F+YFXFY-F-XFYFX', 'Y': 'YFXFY-F-XFYFX+F+YFXFY'}, n, 'F') for n in range(2, 5)}
    def f(u):
        n = 2.0 + 2.0 * ease(u, 0.0, 0.5)
        ang = np.radians(90.0 + 50.0 * ease(u, 0.5, 1.0) ** 1.2)
        n0 = int(np.floor(n)); g = n - n0
        z = _curve_wave(cache[n0], ang)
        if g > 0 and n0 < 4:
            z = (1 - g) * z + g * _curve_wave(cache[n0 + 1], ang)
        z = z * np.exp(-1j * (0.4 + 1.0 * u))
        return z.real + 0.3 * z.imag
    return fin(rows(f))


def _qmark(x):
    """Minkowski's question-mark function ?(x) on [0,1), from the continued fraction of x."""
    xx = np.array(x, dtype=float); y = np.zeros_like(xx); e = np.zeros_like(xx)
    act = xx > 1e-12; sg = 1.0
    for _ in range(48):
        inv = np.where(act, 1.0 / np.maximum(xx, 1e-300), 0.0)
        a = np.minimum(np.floor(inv), 64.0)
        e = e + np.where(act, a, 0.0)
        y = y + np.where(act, sg * 2.0 * 2.0 ** (-e), 0.0)
        xx = np.where(act, inv - a, 0.0)
        act = act & (xx > 1e-9); sg = -sg
    return y


def minkowski_clock():
    """MINKOWSKI CLOCK — a sine whose clock is Minkowski's question-mark function ?(x), the map that sends the fractions
    to the dyadic numbers: smooth-looking, yet flat almost everywhere, all its rise hidden in the rationals. The clock is
    bent into ?, then ?(?), then ?(?(?)), while the sine winds 1 -> 16 times onto it: frame 0 is a plain sine, the end a
    sine frozen in long lulls and torn through the fractions."""
    q1 = _qmark(T); q2 = _qmark(q1); q3 = _qmark(q2)
    def f(u):
        s = ease(u, 0.0, 0.4); s2 = ease(u, 0.4, 0.7); s3 = ease(u, 0.7, 1.0)
        C = (1 - s) * T + s * q1
        C = (1 - s2) * C + s2 * q2
        C = (1 - s3) * C + s3 * q3
        N = 1.0 + 15.0 * ease(u, 0.2, 1.0) ** 1.3
        N0 = int(np.floor(N)); g = N - N0
        y = np.sin(TAU * N0 * C)
        if g > 0:
            y = (1 - g) * y + g * np.sin(TAU * (N0 + 1) * C)
        return y
    return fin(rows(f))


def _knight_tour(N):
    """A knight's tour of an N x N board by Warnsdorff's rule (fewest onward moves first), deterministic."""
    mv = [(1, 2), (2, 1), (2, -1), (1, -2), (-1, -2), (-2, -1), (-2, 1), (-1, 2)]
    best = None
    for start in [(0, 0), (0, 1), (1, 0), (2, 2), (0, 2)]:
        seen = np.zeros((N, N), bool); x, y = start; seen[x, y] = True; path = [(x, y)]
        for _ in range(N * N - 1):
            cand = []
            for dx, dy in mv:
                a, b = x + dx, y + dy
                if 0 <= a < N and 0 <= b < N and not seen[a, b]:
                    deg = sum(1 for ex, ey in mv if 0 <= a + ex < N and 0 <= b + ey < N and not seen[a + ex, b + ey])
                    cand.append((deg, dx, dy))
            if not cand:
                break
            cand.sort()
            _, dx, dy = cand[0]; x, y = x + dx, y + dy; seen[x, y] = True; path.append((x, y))
        if best is None or len(path) > len(best):
            best = path
        if len(best) == N * N:
            break
    P = np.array(best, dtype=float)
    return ((P[:, 0] + 1j * P[:, 1]) - (N - 1) / 2.0 * (1 + 1j)) / ((N - 1) / 2.0)


def knights_tour():
    """KNIGHT'S TOUR — a chess knight visiting every square of the board exactly once (Warnsdorff's rule), its L-shaped
    leaps joined into one path. The board grows 5x5 -> 20x20 (25 -> 400 leaps per cycle) and the wave slides from the
    knight's position to its heading, which has only eight values: a scribble of leaps that turns into a stepped code
    of the tour."""
    cache = {}
    def path(N):
        if N not in cache:
            cache[N] = step_resample(_knight_tour(N))
        return cache[N]
    def f(u):
        Nf = 5.0 + 15.0 * u ** 1.1
        N0 = int(np.floor(Nf)); g = Nf - N0
        z = path(N0)
        if g > 0 and N0 < 20:
            z = (1 - g) * z + g * path(N0 + 1)
        z = z * np.exp(-1j * (0.3 + 0.7 * u))
        d = _unit_dir(z)
        mix = ease(u, 0.2, 0.8)
        return (1 - mix) * z.real + mix * (d.real + 0.4 * d.imag)
    return fin(rows(f))


def fibonacci_word_curve():
    """FIBONACCI WORD CURVE — the Fibonacci word 0100101001001... drawn as a turtle path (every 0 turns left or right by
    the parity of its position): a fractal of square notches whose proportions are golden. The word grows from 5 to
    10946 letters (grown, not faded), then the right angles open 90 -> 150 degrees and the notches fold into pinwheels.
    Wild from the first frame."""
    a, b = '0', '01'
    words = []
    while len(b) < 12000:
        a, b = b, b + a
        words.append(b)
    def turns(wd):
        d = np.frombuffer(wd.encode(), dtype=np.uint8) - 48
        k = np.arange(1, len(d) + 1)
        return np.where(d == 0, np.where(k % 2 == 0, 1, -1), 0)
    tt = [np.concatenate([[0], np.cumsum(turns(w))])[:-1] for w in words]
    def f(u):
        x = 4.0 + (len(words) - 5.0) * ease(u, 0.0, 0.5)
        i0 = int(np.floor(x)); g = x - i0
        ang = np.radians(90.0 + 60.0 * ease(u, 0.5, 1.0))
        z = _curve_wave(tt[i0], ang)
        if g > 0 and i0 + 1 < len(words):
            z = (1 - g) * z + g * _curve_wave(tt[i0 + 1], ang)
        z = z * np.exp(-1j * 0.5 * u)
        return z.imag + 0.4 * z.real
    return fin(rows(f))


def mandala():
    """MANDALA — a twelve-petalled mandala (petal rings bent by a radial ripple, crossed by concentric bands) read round
    an off-centre circle that swells and drifts, printed in soft ink at first and hard black-and-white later. Wild from
    the first frame: a lattice of petals flickering past."""
    def fld(p, u):
        r = np.abs(p); th = np.angle(p)
        return np.cos(12 * th + 5.0 * np.sin(9.0 * r) * (0.3 + u)) * np.cos((6.0 + 30.0 * u) * r + 2.0 * np.cos(5 * th))
    def f(u):
        c = 0.25 * np.exp(1j * (0.4 + 2.0 * u))
        rho = 0.55 + 0.3 * np.sin(np.pi * u)
        p = c + rho * np.exp(1j * TAU * T)
        s = 1.5 + 10.0 * ease(u, 0.0, 0.6)
        return np.tanh(s * fld(p, u))
    return fin(rows(f))


def dragon_compass():
    """DRAGON COMPASS — the paper-folding dragon heard as a compass: the wave is the direction the strip is heading at
    each crease, held from crease to crease (8192 creases per cycle). Opened to 20 degrees the heading only wavers
    (a stepped wobble); at 90 degrees it is the Heighway dragon, whose heading takes just four values in its famous
    fractal order; past that, up to 170 degrees, the compass spins into a white glitter of steps."""
    turns = _paperfold(13)
    def f(u):
        th = np.radians(20.0 + 150.0 * u ** 1.1)
        head = np.concatenate([[0.0], np.cumsum(turns * th)])
        v = hold_resample(np.exp(1j * head))
        return v.real + 0.4 * v.imag
    return fin(rows(f))


def koch_compass():
    """KOCH COMPASS — the Koch curve heard as a compass: the direction of each of its 4096 strokes, held stroke by
    stroke. The generator's bend opens 8 -> 88 degrees (a flat line's shiver -> the snowflake's six headings -> the
    crumpled Cesaro curve), so the wave climbs from a quiet stepped hum into a self-similar staircase of headings."""
    cnt = _lcurve_counts('F', {'F': 'F+F--F+F'}, 6, 'F')
    def f(u):
        ang = np.radians(8.0 + 80.0 * u ** 1.1)
        v = hold_resample(np.exp(1j * cnt * ang))
        return (v * np.exp(-1j * 0.6 * u)).real + 0.4 * v.imag
    return fin(rows(f))


def levy_c_curve():
    """LEVY C CURVE — Paul Levy's C curve (F -> +F--F+ at 45 degrees): every stroke replaced by two at right angles,
    levels 4 -> 13 (16 -> 8192 strokes), so a simple bracket grows the famous fuzzy C of tapestry; then the angle opens
    45 -> 85 degrees and the C unravels into a tangle. The wave is its offset from its own chord."""
    cnt = {n: _lcurve_counts('F', {'F': '+F--F+'}, n, 'F') for n in range(4, 14)}
    def f(u):
        n = 4.0 + 9.0 * ease(u, 0.0, 0.55)
        ang = np.radians(45.0 + 40.0 * ease(u, 0.55, 1.0))
        n0 = int(np.floor(n)); g = n - n0
        z = _curve_wave(cnt[n0], ang)
        if g > 0 and n0 < 13:
            z = (1 - g) * z + g * _curve_wave(cnt[n0 + 1], ang)
        z = z * np.exp(-1j * 0.8 * u)
        return z.imag + 0.4 * z.real
    return fin(rows(f))


def takagi_clock():
    """TAKAGI CLOCK — a sine whose clock is bent by the blancmange (Takagi) curve: time runs ahead on every little
    triangle's rise and lags on its fall. The bend grows (frame 0: a sine with a slight lean), the blancmange gains
    levels 1 -> 12 and roughness, and the sine is wound 1 -> 10 times onto the warped clock: a stammering, fractal
    vibrato frozen into one cycle."""
    def tak(L, w):
        y = np.zeros(M)
        for n in range(L):
            x = (2 ** n) * T
            y += w ** n * np.abs(x - np.round(x))
        return y
    def f(u):
        L = int(round(1 + 11 * ease(u, 0.0, 0.5)))
        w = 0.5 + 0.42 * ease(u, 0.4, 1.0)
        K = tak(L, w); K = K / (K.max() + 1e-12)
        depth = 0.15 + 0.85 * ease(u, 0.0, 0.6)
        N = 1.0 + 9.0 * ease(u, 0.3, 1.0) ** 1.2
        N0 = int(np.floor(N)); g = N - N0
        ph = T + depth * K * 0.5
        y = np.sin(TAU * N0 * ph)
        if g > 0:
            y = (1 - g) * y + g * np.sin(TAU * (N0 + 1) * ph)
        return y
    return fin(rows(f))


def seaweed_fractal():
    """SEAWEED FRACTAL — an L-system weed (F -> FF-[-F+F+F]+[+F-F-F]) drawn by a pen that walks out along every frond and
    back, never lifting. Generation 1 -> 4 (8 -> 4096 strokes), the frond angle opening 22 -> 42 degrees as if the
    current strengthened: a sprig becomes a swaying clump."""
    rules = {'F': 'FF-[-F+F+F]+[+F-F-F]'}
    structs = {}
    def draw(n, th):
        if n not in structs:
            structs[n] = _turtle_tour(_lsys('F', rules, n))
        Lc, Rc, prev, tk, td = structs[n]
        step = np.exp(1j * (np.pi / 2 + (Lc - Rc) * th))
        end = np.empty(len(step), dtype=complex)
        for i in range(len(step)):
            p = prev[i]
            end[i] = (end[p] if p >= 0 else 0) + step[i]
        start = end - step
        A = np.where(td > 0, start[tk], end[tk])
        return step_resample(A / (np.abs(end).max() + 1e-12))
    def f(u):
        n = 1.0 + 3.0 * ease(u, 0.0, 0.6)
        th = np.radians(22.5 + 20.0 * u)
        n0 = int(np.floor(n)); g = n - n0
        z = draw(n0, th)
        if g > 0 and n0 < 4:
            z = (1 - g) * z + g * draw(n0 + 1, th)
        return (z * np.exp(-1j * 0.4 * u)).real + 0.3 * z.imag
    return fin(rows(f))


def terdragon_compass():
    """TERDRAGON COMPASS — the terdragon (a strip folded in thirds nine times, 19683 creases) heard as a compass: the
    heading of every stroke, held stroke by stroke. At 15 degrees the heading barely trembles; at 120 degrees the
    terdragon's heading takes only three values in a self-similar order; the crease angle keeps opening to 170 degrees,
    where the compass spins into a fine stepped storm."""
    cnt = _lcurve_counts('F', {'F': 'F+F-F'}, 9, 'F')
    def f(u):
        ang = np.radians(15.0 + 155.0 * u ** 1.1)
        v = hold_resample(np.exp(1j * cnt * ang))
        return (v * np.exp(-1j * 0.7 * u)).real + 0.4 * v.imag
    return fin(rows(f))


def hilbert_clock():
    """HILBERT CLOCK — a sine whose clock is pushed back and forth by a walker on the Moore (closed Hilbert) curve: the
    walker's x-coordinate, continuous but nowhere smooth, is added to time. The push grows from nothing (frame 0: a plain
    sine), the curve's order climbs 3 -> 7 and the sine is wound 1 -> 8 times: a vibrato that is a space-filling curve."""
    cache = {}
    def xs(o):
        if o not in cache:
            z = np.concatenate([[0.0], np.cumsum(np.exp(1j * _moore(o) * np.pi / 2))])[:-1]
            z = z - z.mean(); z = z / np.abs(z).max()
            cache[o] = step_resample(z)
        return cache[o]
    def f(u):
        o = 3.0 + 4.0 * ease(u, 0.2, 0.9)
        o0 = int(np.floor(o)); g = o - o0
        z = xs(o0)
        if g > 0 and o0 < 7:
            z = (1 - g) * z + g * xs(o0 + 1)
        A = 3.0 * ease(u, 0.0, 0.7) ** 1.2
        N = 1.0 + 7.0 * ease(u, 0.3, 1.0)
        N0 = int(np.floor(N)); gN = N - N0
        ph = T + A * z.real / TAU * 2.0
        y = np.sin(TAU * N0 * ph)
        if gN > 0:
            y = (1 - gN) * y + gN * np.sin(TAU * (N0 + 1) * ph)
        return y
    return fin(rows(f))


def zigzag_blaze():
    """ZIGZAG BLAZE — Bridget Riley's 'Blaze': black and white bands bent into zigzags, read across the canvas. Frame 0 is
    one soft band (a sine); the bands multiply 1 -> 37, harden to black and white, and the zigzag bending them deepens
    and multiplies, so the stripes pinch and splay at every kink: a square wave folded by a triangle."""
    def f(u):
        k = np.round(1.0 + 36.0 * u ** 1.4)
        A = 3.0 * ease(u, 0.1, 1.0)
        m = 1 + int(round(4 * u))
        s = 1.0 + 12.0 * ease(u, 0.0, 0.7)
        return np.tanh(s * np.cos(TAU * (k * T + A * tri(m * T) * 0.5)))
    return fin(rows(f))


def minkowski_sausage():
    """MINKOWSKI SAUSAGE — the quadratic Koch curve (F -> F+F-F-FF+F+F-F), level 4 (4096 strokes), whose generator is a
    square step up and down. Its corner angle opens from 15 degrees (a line with faint crenellations) through 90 (the
    sausage: battlements within battlements) to 135, where the battlements fold over each other. The wave is its offset
    from its own chord."""
    cnt = _lcurve_counts('F', {'F': 'F+F-F-FF+F+F-F'}, 4, 'F')
    def f(u):
        ang = np.radians(15.0 + 75.0 * ease(u, 0.0, 0.6) ** 1.1 + 45.0 * ease(u, 0.6, 1.0))
        z = _curve_wave(cnt, ang) * np.exp(-1j * 0.6 * u)
        return z.imag + 0.4 * z.real
    return fin(rows(f))


def heptagram():
    """HEPTAGRAM — the pen runs round seven points: frame 0 is the circle through them (a sine), the corners sharpen
    into a heptagon whose edges then bow out into petals and in again, while the pen starts skipping points, through
    the stars {7/2} and {7/3}. A regular star {7/m} sounds only harmonics m, 7-m, 7+m ... so the petals ring in hollow,
    shifted lattices."""
    n = 7
    def f(u):
        m = 1.0 + 2.0 * ease(u, 0.2, 1.0)
        rnd = 1.0 - ease(u, 0.0, 0.2)
        bow = 0.7 * np.sin(np.pi * ease(u, 0.2, 1.0)) - 0.4 * ease(u, 0.7, 1.0)
        x = T * n; k = np.floor(x); fx = x - k
        A = np.exp(1j * TAU * m * k / n); B = np.exp(1j * TAU * m * (k + 1) / n)
        lin = A + (B - A) * fx + bow * 1j * (B - A) * 4.0 * fx * (1 - fx)
        z = rnd * np.exp(1j * TAU * m * (k + fx) / n) + (1 - rnd) * lin
        return (z * np.exp(-1j * 0.5 * u)).real + 0.2 * z.imag
    return fin(rows(f))


def hendecagram():
    """HENDECAGRAM — the pen runs round eleven points: frame 0 is the circle through them (a sine), the corners sharpen
    into an 11-gon, then the pen skips 1 -> 5 points per stroke, through the stars {11/2} ... {11/5}, and every stroke
    grows three little curls. A regular star {11/m} sounds only harmonics m, 11-m, 11+m ..., so the table walks from a
    sine into hollow, pitch-shifted, curling lattices."""
    n = 11
    def f(u):
        m = 1.0 + 4.0 * ease(u, 0.15, 1.0)
        rnd = 1.0 - ease(u, 0.0, 0.2)
        loop = 0.3 * ease(u, 0.3, 1.0)
        x = T * n; k = np.floor(x); fx = x - k
        A = np.exp(1j * TAU * m * k / n); B = np.exp(1j * TAU * m * (k + 1) / n)
        z = rnd * np.exp(1j * TAU * m * (k + fx) / n) + (1 - rnd) * (A + (B - A) * fx)
        z = z + loop * np.exp(1j * TAU * 3 * n * T) * np.sin(np.pi * fx) ** 2
        return (z * np.exp(-1j * 0.4 * u)).real
    return fin(rows(f))


def peano_clock():
    """PEANO CLOCK — a sine whose clock is pushed back and forth by a walker on Peano's serpentine curve (levels 1 -> 4),
    whose position is continuous but nowhere smooth. The push grows from nothing (frame 0: a plain sine) and the sine is
    wound 1 -> 10 times: a vibrato that folds three-by-three at every scale."""
    cnt = {n: _lcurve_counts('X', {'X': 'XFYFX+F+YFXFY-F-XFYFX', 'Y': 'YFXFY-F-XFYFX+F+YFXFY'}, n, 'F') for n in range(1, 5)}
    def f(u):
        o = 1.0 + 3.0 * ease(u, 0.2, 0.9)
        o0 = int(np.floor(o)); g = o - o0
        z = _curve_wave(cnt[o0], np.pi / 2)
        if g > 0 and o0 < 4:
            z = (1 - g) * z + g * _curve_wave(cnt[o0 + 1], np.pi / 2)
        A = 3.5 * ease(u, 0.0, 0.7) ** 1.2
        N = 1.0 + 9.0 * ease(u, 0.3, 1.0)
        N0 = int(np.floor(N)); gN = N - N0
        ph = T + A * (z.imag + 0.5 * z.real) / TAU * 2.0
        y = np.sin(TAU * N0 * ph)
        if gN > 0:
            y = (1 - gN) * y + gN * np.sin(TAU * (N0 + 1) * ph)
        return y
    return fin(rows(f))


TABLES = [
    ("CLONE FAN", clone_fan),
    ("ZENO CLONES", zeno_clones),
    ("FACET PRISM", facet_prism),
    ("KALEIDOSCOPE", kaleidoscope),
    ("STACKED SQUARES", stacked_squares),
    ("SKYLINE", skyline),
    ("SCOPE SCRIBBLE", scope_scribble),
    ("TIMES TABLE", times_table),
    ("MAURER ROSE", maurer_rose),
    ("MOIRE FRINGES", moire_fringes),
    ("CROSSING SINES", crossing_sines),
    ("OP ART CURRENT", op_art_current),
    ("ZIGZAG BLAZE", zigzag_blaze),
    ("MANDALA", mandala),
    ("BLANCMANGE", blancmange),
    ("KOCH COASTLINE", koch_coastline),
    ("KOCH COMPASS", koch_compass),
    ("PAPER DRAGON", paper_dragon),
    ("DRAGON COMPASS", dragon_compass),
    ("LEVY C CURVE", levy_c_curve),
    ("MINKOWSKI SAUSAGE", minkowski_sausage),
    ("HILBERT WALK", hilbert_walk),
    ("PEANO SERPENT", peano_serpent),
    ("GOSPER FLOWSNAKE", gosper_flowsnake),
    ("ARROWHEAD CURVE", arrowhead_curve),
    ("TERDRAGON ROSETTE", terdragon_rosette),
    ("TERDRAGON COMPASS", terdragon_compass),
    ("FIBONACCI WORD CURVE", fibonacci_word_curve),
    ("DE RHAM MORPH", de_rham_morph),
    ("FRACTAL BUSH", fractal_bush),
    ("SEAWEED FRACTAL", seaweed_fractal),
    ("CANTOR DUST", cantor_dust),
    ("CANTOR CLOCK", cantor_clock),
    ("MINKOWSKI CLOCK", minkowski_clock),
    ("TAKAGI CLOCK", takagi_clock),
    ("PEANO CLOCK", peano_clock),
    ("HILBERT CLOCK", hilbert_clock),
    ("FORD CIRCLES", ford_circles),
    ("POPCORN FUNCTION", popcorn_function),
    ("SUPERFORMULA BLOOM", superformula_bloom),
    ("ROLLING CIRCLE PEN", spirograph_velocity),
    ("LISSAJOUS PEN", lissajous_pen),
    ("EPICYCLE CHAIN", epicycle_chain),
    ("BUTTERFLY CURVE", butterfly_curve),
    ("HARMONOGRAPH", harmonograph),
    ("KEPLER ORBIT", kepler_orbit),
    ("STARBURST", starburst),
    ("GEAR TEETH", gear_teeth),
    ("PENTAGRAM VELOCITY", pentagram_velocity),
    ("HEPTAGRAM", heptagram),
    ("NONAGRAM", nonagram),
    ("HENDECAGRAM", hendecagram),
    ("SPIROLATERAL", spirolateral),
    ("KNIGHT'S TOUR", knights_tour),
    ("WINDING NUMBER", winding_number),
    ("CIRCLE INVERSION", circle_inversion),
    ("SHATTERED HEART", shattered_heart),
    ("PENROSE STAIRS", penrose_stairs),
    ("NAUTILUS SPIRAL", nautilus_spiral),
    ("EULER SPIRAL", euler_spiral),
    ("WHIRL OF SQUARES", whirl_of_squares),
    ("SPIRAL OF THEODORUS", spiral_theodorus),
    ("RAZOR SERRATION", razor_serration),
    ("DROPPED BALL", dropped_ball),
    ("ZONE PLATE", zone_plate),
    ("DIFFRACTION GRATING", diffraction_grating),
    ("SUNFLOWER SPIRALS", sunflower_spirals),
    ("PIXEL STAR", pixel_star),
    ("TRUCHET WEAVE", truchet_weave),
    ("VORONOI CRYSTAL", voronoi_crystal),
    ("HALFTONE SCREEN", halftone_screen),
    ("BUBBLE RAFT", bubble_raft),
    ("QUASICRYSTAL", quasicrystal),
    ("CONTOUR MAP", contour_map),
    ("CANYON ORBIT", canyon_orbit),
    ("RIPPLE TANK", ripple_tank),
    ("TESSERACT SPIN", tesseract_spin),
    ("KNOT FLYTHROUGH", knot_flythrough),
]
CATEGORY = {ident: "Abstract" for ident, _ in TABLES}
