"""
gen2_physical — the PHYSICAL category of the Terrain 500 (14 new tables; candidates for gate500.py).

Two sources, both physical models, no samples and no third-party material anywhere:

  1. TERRAIN'S OWN ModalEngine, pushed past its knobs by bank3_probe.cpp (same folder). The probe plays the
     shipping engine code at rate R with f0 chosen so ONE loop period is 2048 samples, reaches into the
     per-note coefficients (breath past the Schelleng clamp, reed/lip/bow laws, jet length, loop cutoff,
     dispersion past its cap, an inverted damping law, 48-mode custom geometries) and dumps raw audio.
     This module finds each frame's true period, resamples it to exactly 2048 and Hann-folds several periods
     (a partition of unity, so the wrap is seamless by construction and inharmonic energy is projected with
     a smooth kernel). Frames keep the model's REAL waveform and phase — not a magnitude projection.
  2. First-principles models in numpy: the Korteweg-de Vries soliton equation, the nonlinear Schroedinger equation
     (rogue waves), a string slapping a rising fretboard and a string wrapping a jawari bridge (exact Courant-1
     finite differences), struck membranes and plates read along a closed orbit, a string draining into a tuned
     soundboard, a finger sliding over the string's nodes, Seebeck's hole siren, a ring of 2048 pendulums, a
     loudspeaker driven to its backplate, a marble bouncing on a bar.

Every frame is one period by construction (spectral builds, closed orbits, odd-extended strings) or by the
period-exact resample + Hann fold above, so every wrap is seamless. Deterministic: fixed seeds, no clocks.

Dumps: $TERRAIN_WT_DUMP3, else <wt500>/engine3. Rebuild them (a few seconds):
    c++ -std=c++17 -O2 -I ../../Tests/shim -I ../../Source bank3_probe.cpp -framework Accelerate -o /tmp/bank3
    /tmp/bank3 <wt500>/engine3
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from scipy import signal
import wtlib

N, F = wtlib.SIZE, wtlib.FRAMES
_WT500 = os.environ.get("TERRAIN_WT500") or \
    "/private/tmp/claude-501/-Users-macshooter/521ae994-b058-4e64-9804-78f1254cea68/scratchpad/wt500"
DUMP3 = os.environ.get("TERRAIN_WT_DUMP3") or os.path.join(_WT500, "engine3")


# ══ engine dumps -> cycles ═══════════════════════════════════════════════════════════════════════
def _dump(name):
    meta, raw = os.path.join(DUMP3, name + ".txt"), os.path.join(DUMP3, name + ".f32")
    if not (os.path.isfile(meta) and os.path.isfile(raw)):
        raise FileNotFoundError("bank3 dump '%s' not found in %s — build and run bank3_probe.cpp "
                                "(header of this module)" % (name, DUMP3))
    rate, f0, fr, sl, _fam, _form = open(meta).read().split()
    a = np.fromfile(raw, dtype='<f4').reshape(int(fr), int(sl)).astype(np.float64)
    return a, float(rate) / float(f0)


def _track(x, P0, lo, hi):
    """Fundamental-first period in samples: the best autocorrelation peak in [lo, hi]·P0, then the shortest
    sub-multiple that is nearly as good (a register jump is a new period, not a multiple), refined by Newton
    iterations on the exact fractional-lag autocorrelation."""
    x = x - x.mean(); n = len(x)
    S = np.abs(np.fft.rfft(x * np.hanning(n), 2 * n)) ** 2
    ac = np.fft.irfft(S)[:n]; ac = ac / (ac[0] + 1e-30)
    a, b = max(8, int(P0 * lo)), min(int(P0 * hi) + 2, n // 2)
    k = a + int(np.argmax(ac[a:b])); best = ac[k]
    for d in (8, 7, 6, 5, 4, 3, 2):
        c = int(round(k / d))
        if c < a:
            continue
        j = max(c - 3, 1) + int(np.argmax(ac[max(c - 3, 1):c + 4]))
        if ac[j] > 0.88 * best:
            k = j; break
    w = 2 * np.pi * np.arange(len(S)) / (2 * n); tau = float(k)
    for _ in range(8):
        d1 = -(S * w * np.sin(w * tau)).sum(); d2 = -(S * w * w * np.cos(w * tau)).sum()
        if d2 >= 0:
            break
        st = -d1 / d2
        if abs(st) > 1:
            break
        tau += st
        if abs(st) < 1e-7:
            break
    return tau


def _periodise(x, P, K):
    """Resample so one period is exactly N samples, then fold K periods under a periodic Hann window
    (the K shifted windows sum to K/2 everywhere, so a periodic input comes back exactly and the wrap is
    continuous for any input)."""
    y = signal.resample(x, int(round(len(x) * N / P))) if abs(P - N) > 1e-6 else x
    L = K * N; s = max(0, (len(y) - L) // 2); seg = y[s:s + L]
    if len(seg) < L:
        raise ValueError("segment too short for K=%d" % K)
    w = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(L) / L)
    return (seg * w).reshape(K, N).sum(0) / (K / 2.0)


def _align(frames):
    """Rotate each frame so harmonic 1 sits at phase 0; when it is too weak to trust, rotate to the best
    correlation with the previous frame. Pure rotations: spectra and fingerprints are unchanged."""
    out = np.zeros_like(frames); prev = None; k = np.arange(N // 2 + 1)
    for i, c in enumerate(frames):
        S = np.fft.rfft(c)
        if np.abs(S[1]) > 1e-2 * np.abs(S[1:]).max():
            S = S * np.exp(-1j * k * np.angle(S[1]))
        elif prev is not None:
            sh = int(np.argmax(np.fft.irfft(S * np.conj(np.fft.rfft(prev)), n=N)))
            S = S * np.exp(2j * np.pi * k * sh / N)
        out[i] = np.fft.irfft(S, n=N); prev = out[i]
    return out


def _engine(name, track=None, K=4, reverse=False):
    a, P0 = _dump(name)
    fr = np.array([_periodise(r, _track(r, P0, *track) if track else P0, K) for r in a])
    if reverse:
        fr = fr[::-1]
    return wtlib.finalize(wtlib.bandlimit(_align(fr), 1000))


# ══ first-principles models ══════════════════════════════════════════════════════════════════════
def _kdv(delta, T, curve=1.3, M=1024, dt=1e-4):
    """Zabusky-Kruskal: u_t + u·u_x + delta²·u_xxx = 0 on x in [0,2), u(x,0) = cos(pi x). Pseudo-spectral on
    M points, 2/3 dealiased, integrating-factor RK4 (the dispersive term exact); each snapshot is zero-padded
    to the 2048 grid. Returns the field at F times."""
    m = np.arange(M // 2 + 1); k = np.pi * m; keep = m <= (M // 2) * 2 // 3
    v = np.fft.rfft(np.cos(np.pi * np.arange(M) * 2.0 / M))
    E1 = np.exp(1j * delta ** 2 * k ** 3 * dt / 2); E2 = E1 * E1

    def nl(vh):
        u = np.fft.irfft(vh, n=M)
        return -0.5j * k * np.fft.rfft(u * u) * keep
    times = T * (np.arange(F) / (F - 1)) ** curve
    out = np.zeros((F, N)); t, fi = 0.0, 0
    while True:
        while fi < F and times[fi] <= t + 1e-12:
            S = np.zeros(N // 2 + 1, complex); S[:M // 2 + 1] = v * keep; out[fi] = np.fft.irfft(S, n=N); fi += 1
        if fi >= F:
            return out
        a = nl(v); b = nl(E1 * (v + dt / 2 * a)); c = nl(E1 * v + dt / 2 * b); d = nl(E2 * v + dt * E1 * c)
        v = E2 * v + dt / 6 * (E2 * a + 2 * E1 * (b + c) + d)
        t += dt


def _rogue(a0, L, eps, T, dt=0.002, curve=1.0, seed=5):
    """Nonlinear Schroedinger (deep-water waves / fibre optics): i psi_t + psi_xx/2 + |psi|^2 psi = 0 on a ring
    of length L, a background of amplitude a0 with a small modulation. Modulational instability grows it into
    Akhmediev breathers / Peregrine-like rogue peaks. Exact split-step Fourier. Observable = the intensity |psi|^2."""
    x = L * np.arange(N) / N; kk = 2 * np.pi * np.fft.fftfreq(N, L / N); rng = np.random.default_rng(seed)
    psi = a0 * (1 + eps * np.cos(2 * np.pi * x / L)
                + sum(1e-4 * rng.standard_normal() * np.cos(2 * np.pi * j * x / L + rng.uniform(0, 6.3)) for j in range(2, 6)))
    psi = psi.astype(complex); lin = np.exp(-0.5j * kk ** 2 * dt)
    times = T * (np.arange(F) / (F - 1)) ** curve; out = np.zeros((F, N)); t, fi = 0.0, 0
    while fi < F:
        while fi < F and times[fi] <= t + 1e-9:
            out[fi] = np.abs(psi) ** 2; fi += 1
        psi *= np.exp(0.5j * np.abs(psi) ** 2 * dt); psi = np.fft.ifft(lin * np.fft.fft(psi))
        psi *= np.exp(0.5j * np.abs(psi) ** 2 * dt); t += dt
    return out


def _pendulums(A, S, T, dt=0.02, M=2048, curve=1.0):
    """A ring of M weakly torsion-coupled pendulums (sine-Gordon: phi_tt = phi_xx/S² - sin phi), pendulum i let go
    at angle A·sin(2 pi i/M). Big swings are SLOWER than small ones (the pendulum period grows with amplitude), so
    neighbours dephase into ever-denser waves and the coupling turns the shear into travelling kinks. Split-step:
    the linear Klein-Gordon part rotated exactly per mode, sin(phi)-phi kicked. Observable = each bob's horizontal
    position sin(phi)."""
    k = np.fft.rfftfreq(M, 1.0 / M); om = np.sqrt((k / S) ** 2 + 1.0)
    x = 2 * np.pi * np.arange(M) / M
    U = np.fft.rfft(A * np.sin(x) + 0.02 * np.sin(3 * x + 0.7)); V = np.zeros(M // 2 + 1, complex)

    def kick(U):
        p = np.fft.irfft(U, n=M)
        return np.fft.rfft(-(np.sin(p) - p))
    c, s_ = np.cos(om * dt), np.sin(om * dt)
    times = T * (np.arange(F) / (F - 1)) ** curve; out = np.zeros((F, N)); t, fi = 0.0, 0; Fk = kick(U)
    while fi < F:
        while fi < F and times[fi] <= t + 1e-9:
            out[fi] = np.sin(np.fft.irfft(U, n=M)); fi += 1
        V = V + 0.5 * dt * Fk; U, V = U * c + V / om * s_, -U * om * s_ + V * c; Fk = kick(U); V = V + 0.5 * dt * Fk
        t += dt
    return out


def _woofer(drive=(0.3, 4.5), zeta=0.18, curve=0.8, per=26, spp=1024, kb=1200.0, cb=30.0):
    """A loudspeaker driven by a sine at 1.25x its resonance, drive rising per frame (all 128 frames integrated at
    once, RK4): a hardening, asymmetric suspension K(x) = 1 + 0.6x + 1.8x², force factor Bl(x) falling as the voice
    coil leaves the gap, and a stiff backplate the former hits past x = -1.25. Observable = cone ACCELERATION (what
    radiates). The steady state is Hann-folded over the last two drive periods and band-limited up to 2048."""
    u = drive[0] + (drive[1] - drive[0]) * (np.arange(F) / (F - 1)) ** curve
    w0 = 2 * np.pi * 0.8; x = np.zeros(F); v = np.zeros(F); dt = 1.0 / spp

    def acc(t, x, v):
        K = 1 + 1.8 * x ** 2 + 0.6 * x; Bl = np.exp(-0.9 * (x / 1.1) ** 2)
        a = -2 * zeta * w0 * v - w0 ** 2 * K * x + w0 ** 2 * Bl * u * np.sin(2 * np.pi * t)
        return a - kb * np.maximum(-x - 1.25, 0) ** 1.5 - cb * v * (x < -1.25)
    keep = np.zeros((F, 2 * spp)); t = 0.0
    for n in range(per * spp):
        k1v = acc(t, x, v); k1x = v
        k2v = acc(t + dt / 2, x + dt / 2 * k1x, v + dt / 2 * k1v); k2x = v + dt / 2 * k1v
        k3v = acc(t + dt / 2, x + dt / 2 * k2x, v + dt / 2 * k2v); k3x = v + dt / 2 * k2v
        k4v = acc(t + dt, x + dt * k3x, v + dt * k3v); k4x = v + dt * k3v
        x = x + dt / 6 * (k1x + 2 * k2x + 2 * k3x + k4x); v = v + dt / 6 * (k1v + 2 * k2v + 2 * k3v + k4v); t += dt
        if n >= (per - 2) * spp:
            keep[:, n - (per - 2) * spp] = acc(t, x, v)
    w = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(2 * spp) / (2 * spp))
    S = np.fft.rfft((keep * w).reshape(F, 2, spp).sum(1), axis=1)
    full = np.zeros((F, N // 2 + 1), complex); full[:, :spp // 2] = S[:, :spp // 2]
    return np.fft.irfft(full, n=N, axis=1)


def _string_contact(barrier, loss, pluck=0.21, Np=1025):
    """Exact (Courant-1) finite-difference string, fixed ends, plucked, one-sided contact with barrier(u)
    (an array over the string, re-evaluated every frame). Snapshots once per period, so every change between
    frames is the CONTACT's doing. Observable = transverse VELOCITY, odd-extended to one 2048 cycle."""
    x = np.linspace(0, 1, Np)
    y = np.where(x < pluck, x / pluck, (1 - x) / (1 - pluck)); yo = y.copy(); per = 2 * (Np - 1)
    out = np.zeros((F, N))
    for f in range(F):
        b = barrier(x, f / (F - 1))
        for _ in range(per):
            yn = np.empty_like(y); yn[1:-1] = y[2:] + y[:-2] - yo[1:-1]; yn[0] = yn[-1] = 0.0
            yn *= (1 - loss); np.maximum(yn, b, out=yn); yo, y = y, yn
        v = y - yo
        out[f] = np.concatenate([v[:-1], -v[:0:-1]])
    return out


def _orbit(plate, w, T, M=160, curve=1.4, x0=(0.31, 0.43), c=(0.52, 0.47), r=0.33, sig=0.15, lis=None):
    """A square membrane (wave equation) or plate (bending, dispersive), struck by a Gaussian at x0, read
    along a CLOSED orbit — a circle or a Lissajous figure — so every frame is periodic by construction.
    Exact modal solution (M x M modes); frames = time after the strike."""
    m = np.arange(1, M + 1); th = 2 * np.pi * np.arange(N) / N
    if lis is None:
        px, py = c[0] + r * np.cos(th), c[1] + r * np.sin(th)
    else:
        px, py = 0.5 + 0.42 * np.sin(lis[0] * th), 0.5 + 0.42 * np.sin(lis[1] * th + lis[2])
    Sx = np.sin(np.pi * np.outer(px, m)); Sy = np.sin(np.pi * np.outer(py, m))
    mm, nn = np.meshgrid(m, m, indexing='ij'); q = mm ** 2 + nn ** 2
    A = np.sin(np.pi * mm * x0[0]) * np.sin(np.pi * nn * x0[1]) * np.exp(-(np.pi * w) ** 2 * q / 2)
    om = (np.pi * q / 40.0) if plate else np.sqrt(q) * np.pi
    out = np.zeros((F, N))
    for i, t in enumerate(T * (np.arange(F) / (F - 1)) ** curve):
        out[i] = ((Sx @ (A * np.cos(om * t) * np.exp(-sig * t * q / (M * M)))) * Sy).sum(1)
    return out


# ══ THE TABLES ═══════════════════════════════════════════════════════════════════════════════════
# ── Terrain's own cores (bank3_probe.cpp dumps) ──
def clarinet_blowout():
    """ENGINE · REED-BORE clarinet. Breath held inside the speaking window while the bore's loop cutoff opens
    250 Hz -> 40 kHz, then the output is driven into the engine's own soft clipper.
    Frames: a dark hollow near-sine -> reedy odd-harmonic buzz -> a clipped, flat-topped reed scream."""
    return _engine("CLARINET_BLOWOUT", track=(0.9, 1.1), K=4)


def reed_collapse():
    """ENGINE · REED-BORE sax (conical form). A stiff, bright reed relaxes while the bore darkens until the reed
    can no longer sustain the oscillation and the tone collapses into the breath that was driving it.
    Frames: raw bright reed -> hollowing, register wobble -> frozen breath noise."""
    return _engine("REED_COLLAPSE", track=(0.5, 2.2), K=4)


def brass_lip_chaos():
    """ENGINE · REED-BORE trumpet. The mouthpiece formant made absurd (+22 dB, Q 6) and swept 3 kHz -> 150 Hz:
    the level-driven brassiness feeds a lip loop that finally loses its period.
    Frames: a blatty brass pulse -> formant-ringing edges -> chaotic ripple."""
    return _engine("BRASS_LIP_CHAOS", track=(0.5, 2.2), K=4)


def brass_rip():
    """ENGINE · REED-BORE trumpet. Lips blown from the bottom of the speaking window to the top while the bore's
    loss opens 700 Hz -> 40 kHz and the mouthpiece formant climbs 600 Hz -> 4 kHz / +4 -> +26 dB, the output pushed
    into the soft clipper. Frames: a muffled low blat -> a brassy shock edge -> a clipped, ripping pulse."""
    return _engine("BRASS_RIP", track=(0.5, 2.2), K=4)


def flute_register_hop():
    """ENGINE · REED-BORE flute. The jet delay pushed from its speaking length to 1.3x the bore: the jet locks
    onto a different bore mode almost every frame. Each frame is one period of whatever register the flute is
    in, stretched to the note. Soft flute at frame 0; register hops, multiphonic wobble and chaos after."""
    return _engine("FLUTE_REGISTER_HOP", track=(0.1, 2.6), K=3)


def wire_rewind():
    """ENGINE · STRING lute plucked at its very end, dispersion 0.99 (past the engine's 0.85 cap), loop cutoff wide
    open, 3 s of one note played BACKWARDS. Frames: the settled ringing tail -> partials un-decaying -> the
    dispersive attack: chirped, smeared wavefronts of an impossibly stiff wire."""
    return _engine("WIRE_REWIND", K=2, reverse=True)


def sympathetic_beat():
    """ENGINE · STRING steel pluck with HALO 1: the second polarisation, detuned 14 cents and ringing longer,
    beats against the first. Frames = 3 s of one note: a bright pluck -> comb notches sliding down through the
    harmonics as the two polarisations drift in and out of phase (a flanger made of physics)."""
    return _engine("SYMPATHETIC_BEAT", K=2)


def sub_piano_clang():
    """ENGINE · STRING concert grand played at 21.5 Hz (below the lowest piano key), STRETCH at maximum, read
    BACKWARDS from 2.5 s into the attack while (frame by frame, each its own note) the hammer hardens from felt to
    steel and the material brightens. Subby: frame 0 is a round, dark sub; the end is the stiff string's
    dispersive wave packets clanging over it."""
    return _engine("SUB_PIANO_CLANG", K=2, reverse=True)


def impossible_bell():
    """ENGINE · MODAL 48-mode free-free bar, soft mallet, with the damping law INVERTED (T60 grows with
    frequency): the highs outlive the fundamental. Frames = 4 s of one strike: a warm mallet hit -> the body
    drains away -> only a spray of high modes is left ringing."""
    return _engine("IMPOSSIBLE_BELL", K=2)


def mode_fan():
    """ENGINE · MODAL bank, 48 modes at ratios k^p with p swept 0.7 -> 2.0 (hard strike, metal damping):
    partials crowded below the harmonic series fan out through it and past any real metal's stiffness.
    Frames: dense square-pulse clusters whose spacing keeps re-shuffling. Wild from frame 0."""
    return _engine("MODE_FAN", K=2)


def bessel_drum():
    """ENGINE · MODAL bank with a 48-mode circular-membrane geometry (Bessel-function zeros). 2 s of one hit read
    BACKWARDS while (each frame its own note) the strike walks centre -> rim, the damping turns from woody to
    ringing and the output is driven into the engine's clipper. Frames: a round tom-like thud -> the head's
    inharmonic ring -> a crackling, clipped rim shot."""
    return _engine("BESSEL_DRUM", K=2, reverse=True)


# ── first-principles models ──
def soliton_train():
    """MODEL · Korteweg-de Vries (shallow water / FPU chain), delta 0.007. Frames = time: a pure sine steepens,
    nearly breaks, and dispersion splits it into a train of solitons of graded height that overtake each other.
    Sub-weight fundamental at frame 0; a comb of sharp pulses by the end."""
    return wtlib.finalize(wtlib.bandlimit(_kdv(0.007, 1.3), 1000))


def fretboard_slap():
    """MODEL · plucked string, exact finite differences, against a flat fretboard that rises under it (gap
    1.3 -> 0.12 of the pluck height). Velocity observable. Frames: a clean pluck -> first slaps -> the string
    pinned and chattering on the board (comb notches sweeping through the spectrum)."""
    return wtlib.finalize(wtlib.bandlimit(_string_contact(lambda x, u: -(1.3 - 1.18 * u), 2e-5), 1000))


def jawari_bridge():
    """MODEL · the sitar/tanpura jawari: a string plucked at 35% wrapping a curved bridge over its first 9%,
    the curve flattening across the table. Frames: a round pluck -> the bridge buzz grows -> a hard rattle."""
    return wtlib.finalize(wtlib.bandlimit(_string_contact(
        lambda x, u: np.where(x < 0.09, -(x / 0.09) ** 2 * (0.5 * (1 - 0.9 * u)), -9.0), 3e-5, pluck=0.35), 1000))


def drumhead_orbit():
    """MODEL · square membrane struck off-centre, read along a circle that passes the strike point. Frames =
    time: one bump -> circular wavefronts crossing the orbit as spike pairs -> edge reflections multiplying."""
    return wtlib.finalize(wtlib.bandlimit(_orbit(False, 0.009, 2.0), 1000))


def plate_orbit():
    """MODEL · simply-supported plate (bending waves: high frequencies travel faster) struck and read along a
    circle. Frames = time: a bump -> dispersive ripples racing ahead of the slow bulk -> a ringing ripple field."""
    return wtlib.finalize(wtlib.bandlimit(_orbit(True, 0.006, 6.0), 1000))


def soundboard_focus():
    """MODEL · a plucked string draining into a soundboard that only sustains four high body resonances
    (harmonics ~90, 150, 240, 380) with a little stiffness dispersion. Frames = round trips (0 -> 900):
    a warm pluck -> everything but the body resonances dies -> a bright ringing formant cluster."""
    k = np.arange(1, 1024)
    X0 = (1.0 / k) * np.abs(np.sin(np.pi * k * 0.137)) * (1 + 0.3 * np.random.default_rng(7).standard_normal(k.size))
    R = np.zeros(k.size)
    for p in (90, 150, 240, 380):
        R = np.maximum(R, np.exp(-0.5 * ((np.log(k) - np.log(p)) / 0.06) ** 2))
    lg = -0.02 * (1 - R) - 1e-5 * k; ph = 4e-5 * k ** 2
    out = np.zeros((F, N))
    for i, n in enumerate(900 * (np.arange(F) / (F - 1)) ** 1.6):
        S = np.zeros(N // 2 + 1, complex); S[1:1024] = X0 * np.exp(n * lg + 1j * (n * ph + wtlib.PHASE[:1023]))
        out[i] = np.fft.irfft(S, n=N)
    return wtlib.finalize(out)


def rogue_wave():
    """MODEL · nonlinear Schroedinger (ocean swell / light in a fibre), background amplitude 2.5 on a ring 12
    long. Frames = time: a gentle swell (a sine) -> modulational instability lifts it into Peregrine-like rogue
    peaks -> many unstable modes interleave into a breaking, spiky sea."""
    return wtlib.finalize(wtlib.bandlimit(_rogue(2.5, 12.0, 0.03, 8.0), 1000))


def seebeck_siren():
    """MODEL · Seebeck's acoustic siren: air blown through a spinning disk with 18 rings of holes (1, 2, 3 ... 48
    holes per turn), orifice flow ~ open area^1.5. One cycle = one turn. Frames: the nozzle slides outward across
    the rings — one fat pulse -> pulse trains of every hole count overlapping -> a dense 40-48 hole buzz."""
    th = np.arange(N * 8) / (N * 8); rings = (1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48)
    out = np.zeros((F, N))
    for i in range(F):
        pos = i / (F - 1) * (len(rings) - 1); y = np.zeros_like(th)
        for j, n in enumerate(rings):
            g = np.exp(-0.5 * ((j - pos) / 0.63) ** 2)
            if g < 1e-4:
                continue
            ph = (th * n) % 1.0; ph = np.minimum(ph, 1 - ph)
            y += g * np.clip(1 - (ph / 0.6) ** 2, 0, None) ** 1.5
        S = np.zeros(N // 2 + 1, complex); S[:1001] = np.fft.rfft(y)[:1001]; out[i] = np.fft.irfft(S, n=N)
    return wtlib.finalize(out)


def flageolet_slide():
    """MODEL · a plucked string touched lightly by a finger that slides from the midpoint toward the nut: after
    60 periods only the modes with a NODE under the finger survive. Frames: the octave flageolet -> the
    surviving set flickers through 1/3, 2/5, 1/4 ... every rational point -> a sparse high whistle cluster."""
    k = np.arange(1, 1024); A = np.sin(np.pi * k * 0.137) / k ** 0.8
    out = np.zeros((F, N))
    for i in range(F):
        x = 0.5 + (0.06 - 0.5) * i / (F - 1)
        a = A * np.exp(-0.35 * 60 * np.sin(np.pi * k * x) ** 2 - 2e-7 * k ** 2 * 60)
        S = np.zeros(N // 2 + 1, complex); S[1:1024] = -1j * a; out[i] = np.fft.irfft(S, n=N)
    return wtlib.finalize(out)


def pendulum_chain():
    """MODEL · 2048 weakly coupled pendulums in a ring, let go at up to 3 rad (just short of the top) in one smooth
    wave. Big swings are slower, so neighbours dephase: frames = time, and the bob positions run from a soft
    sine-like wave to ever-denser folds and travelling kinks (the classic pendulum-wave demo, made nonlinear)."""
    return wtlib.finalize(wtlib.bandlimit(_pendulums(3.0, 40.0, 150.0), 1000))


def dropped_marble():
    """MODEL · a marble dropped on a free-free bar, one whole bounce sequence per cycle: impacts at Zeno times (each
    flight e times the last, e = restitution), each driving the bar's six modes (1 : 2.76 : 5.40 : 8.93 : 13.3 :
    18.6) through the contact's low-pass. Frames: the materials harden together — restitution 0.2 -> 0.94 while the
    contact gets 15x shorter — from one dull rubber thud to an accelerating, glassy roll of ticks. Built as a
    spectrum, so it is periodic and alias-free by construction."""
    k = np.arange(1, 1001).astype(float)
    H = np.zeros(k.size, complex)
    for j, r in enumerate((1.0, 2.756, 5.404, 8.933, 13.34, 18.64)):
        H += (1.0 / (1 + 0.5 * j)) / (1 + 1j * (k - 23.0 * r) / (0.6 + 1.0 / (50 * np.pi * 0.05)))
    out = np.zeros((F, N)); n = np.arange(400)
    for i in range(F):
        u = i / (F - 1); e = 0.2 + 0.74 * u ** 0.7; w = 0.012 * (0.0008 / 0.012) ** u
        tau = e ** n; t = np.concatenate([[0.0], np.cumsum(tau)[:-1]]) / tau.sum() * 0.96
        C = (e ** n * np.exp(-2j * np.pi * np.outer(k, t))).sum(1) * np.exp(-(np.pi * k * w) ** 2)
        S = np.zeros(N // 2 + 1, complex); S[1:1001] = H * C; out[i] = np.fft.irfft(S, n=N)
    return wtlib.finalize(out)


def blown_woofer():
    """MODEL · a loudspeaker driven harder every frame: suspension stiffening, the voice coil leaving the magnet
    gap, finally the cone slamming the backplate. Subby: frame 0 is the clean sine it was fed; the cone then jumps
    onto its large-excursion branch and the radiated acceleration grows slap spikes."""
    return wtlib.finalize(wtlib.bandlimit(_align(_woofer()), 1000))


TABLES = [
    ("CLARINET BLOWOUT", clarinet_blowout),
    ("REED COLLAPSE", reed_collapse),
    ("BRASS LIP CHAOS", brass_lip_chaos),
    ("BRASS RIP", brass_rip),
    ("FLUTE REGISTER HOP", flute_register_hop),
    ("WIRE REWIND", wire_rewind),
    ("SYMPATHETIC BEAT", sympathetic_beat),
    ("SUB PIANO CLANG", sub_piano_clang),
    ("IMPOSSIBLE BELL", impossible_bell),
    ("MODE FAN", mode_fan),
    ("TOM TO RIM SHOT", bessel_drum),
    ("SOLITON TRAIN", soliton_train),
    ("FRETBOARD SLAP", fretboard_slap),
    ("JAWARI BRIDGE", jawari_bridge),
    ("DRUMHEAD ORBIT", drumhead_orbit),
    ("PLATE ORBIT", plate_orbit),
    ("SOUNDBOARD FOCUS", soundboard_focus),
    ("ROGUE WAVE", rogue_wave),
    ("SPINNING DISK SIREN", seebeck_siren),
    ("FLAGEOLET SLIDE", flageolet_slide),
    ("PENDULUM CHAIN", pendulum_chain),
    ("DROPPED MARBLE", dropped_marble),
    ("BLOWN WOOFER", blown_woofer),
]
CATEGORY = {ident: "Physical" for ident, _ in TABLES}
