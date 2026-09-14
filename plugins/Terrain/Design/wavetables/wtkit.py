#!/usr/bin/env python3
"""
wtkit.py — the checker every gen2_<category>.py author runs, and the renderer gate500.py reuses.

    python3 wtkit.py check gen2_<category>.py [--out DIR] [--jobs N] [--timeout S] [--fresh] [--no-sheets]
    python3 wtkit.py selftest

The generator contract (wtlib.py, README.md):
    TABLES   = [("IDENTIFIER IN CAPS", fn), ...]    # fn() returns wtlib.finalize(frames), (128, 2048)
    CATEGORY = {"IDENTIFIER IN CAPS": "<Category>", ...}
New identifiers carry NO "TERRA " prefix; the name a user sees is wtlib.shipping_name(IDENT).

`check` renders every TABLES entry in a pool of worker processes (a per-table timeout; an exception
is reported for THAT table only), then:

  BARS — a table that misses any of these is not a candidate for gate500.py
    ident    ALL CAPS, no "TERRA " prefix, filename-safe, CATEGORY is one of the 13
    shape    exactly (128, 2048)             finite  no NaN / inf
    peak     every frame peaks at 1.0        dc      every frame has zero mean
    dead     no silent frame (wtlib.measure()['dead'] == 0)
    seam     the wrap from sample 2047 back to sample 0 is no larger than the frame's typical step;
             the worst ratio over the 128 frames must be <= SEAM_MAX (see seam_ratios())
    static   the frame axis travels: some frame differs from frame 0 by >= STATIC_MIN_DB
  FLAGS — reported here, enforced by gate500.py against the whole library
    nn-module  nearest neighbour inside this module — below the collision floor?
    nn-fixed   nearest neighbour among the FIXED tables (120 shipped FLACs + built-in dumps when
               present, fixed_set.py) — below the floor?
    name       wtlib.shipping_name(IDENT) already taken (case-insensitive) by a fixed table, a
               built-in, or another table in this module
  The floor is gate.calibrate()'s — saw vs a gently re-tilted saw, ~5.10 dB — imported, not copied.

  OUTPUT under <out>/<module>/ (default <wt500>/cand):
    <IDENT>.wav     float32 44.1 kHz, 128 x 2048 concatenated (the bank layout)
    metrics.json    every number above, per table
    sheet_NN.png    contact sheets, 12 tables each (3 x 4), 1200 px wide: a Serum-like waterfall of 40
                    of the 128 frames (frame 1 in front, highlighted), the name, harm60 and travel
  Exit 0 only when every table passes every bar and raises no flag.

Renders are cached under <wt500>/cache, keyed on mtime+size of the module, every sibling module it
imports, wtlib.py and this file: edit any of them and the module re-renders. Data files a generator
reads are NOT tracked — use --fresh after changing one. <wt500> is $TERRAIN_WT500, else the session
scratch directory.
"""
import sys, os, re, io, ast, json, time, math, glob, shutil, hashlib, argparse, tempfile
import traceback, contextlib, importlib.util
import multiprocessing as mp
from multiprocessing.connection import wait as mp_wait

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import numpy as np
import wtlib

TOOL_VERSION = 1
_SESSION_WT500 = "/private/tmp/claude-501/-Users-macshooter/521ae994-b058-4e64-9804-78f1254cea68/scratchpad/wt500"


def wt500_dir():
    """Where caches, renders and the fixed-set fingerprints live (never the worktree)."""
    d = os.environ.get("TERRAIN_WT500")
    if d:
        return os.path.abspath(os.path.expanduser(d))
    if os.path.isdir(os.path.dirname(_SESSION_WT500)):
        return _SESSION_WT500
    return os.path.join(tempfile.gettempdir(), "terrain_wt500")


# The 13 categories and how many NEW tables each takes (existing FLAC count in the comment).
# fb638 — Max, mid-build (2026-09-13): "Riot's banks are EXTREME and RAW + they usually morph into something crazy …
# keep basic stuff in there too (a little bit lol, aim for the crazy ones, even some subby ones)". So the split skews
# wild: Basic Shapes only +12, Chaos / Abstract / Processed / Digital / Spectral carry the weight. Existing count in the
# comment; new + existing = the 454 FLAC tables, + 46 built-ins = 500.
QUOTA_NEW = {"Basic Shapes": 12,   # 10
             "Analog":       16,   # 12
             "Digital":      36,   # 14
             "Spectral":     32,   # 14
             "Vocal":        16,   # 14
             "Metallic":     18,   # 12
             "Physical":     14,   # 10
             "Harmonic":     14,   # 10
             "Cinematic":    18,   # 10
             "Chaos":        44,   # 14
             "Abstract":     48,   # 0
             "Processed":    42,   # 0
             "Textures":     24}   # 0
CATEGORIES = tuple(QUOTA_NEW)
assert len(CATEGORIES) == 13 and sum(QUOTA_NEW.values()) == 334
DARK_OK = ("Basic Shapes", "Physical")     # structurally dark; never ranked on brightness

PEAK_TOL      = 1e-4     # |frame peak - 1|
DC_TOL        = 1e-4     # |frame mean|
SEAM_MAX      = 1.5      # wrap jump / typical step
SEAM_TOP      = 0.01     # "typical step" = mean of the largest 1% of in-cycle steps
STATIC_MIN_DB = 1.0      # some frame must differ from frame 0 by this much (mean |dB|, 48 bands)
DEFAULT_TIMEOUT = 240.0  # seconds per table
IDENT_RE = re.compile(r"[A-Z0-9][A-Z0-9 '&.\-]*\Z")

# ── SEAM ─────────────────────────────────────────────────────────────────────────────────────
# "Typical step" is the mean of the largest 1% (20) of the frame's 2047 in-cycle |x[i+1]-x[i]|.
# Measured before choosing (2026-09-13, `wtkit.py selftest` re-measures it):
#   periodic tables built with cycles_from_mags score 0.1-0.5 (random-phase saw 0.12, sine 0.37,
#   swept saw 0.46); genuinely broken wraps score 20-174 (a 1.37-cycle sine 174, a brown-noise chunk
#   39, a 16-step stair with its riser on the seam 20).
# The median / mean step can NOT be the reference: a staircase's median step is 0, and a sine beats
# its own mean step by pi/2 at its steepest point, so a perfect sine would fail a "mean" bar.
# 12 of the 120 shipped FLACs score over 1.5 (Tri Core 15.7, Sync Lead 12.8, Stairs 5.6 ...); they
# are fixed and only reported by fixed_set.py. A jump that is part of the waveform — a COH-phase
# saw's riser (7.7), a hard-sync reset — also scores high: np.roll every frame by the same amount so
# the riser sits mid-cycle. A rotation leaves the magnitude spectrum, every metric and the
# fingerprint unchanged.
def seam_ratios(frames):
    x = np.asarray(frames, dtype=np.float64)
    d = np.abs(np.diff(x, axis=1))
    k = max(1, int(round(SEAM_TOP * d.shape[1])))
    typ = np.partition(d, d.shape[1] - k, axis=1)[:, -k:].mean(axis=1)
    seam = np.abs(x[:, 0] - x[:, -1])
    return seam / np.maximum(typ, 1e-12)


_E = wtlib._EDGES


def band_rows(frames):
    """(F, 48) per-frame-normalised dB rows — EXACTLY the rows wtlib.fingerprint() takes at 16
    positions, for every frame (selftest proves the equality)."""
    x = np.asarray(frames, dtype=np.float64)
    S = np.abs(np.fft.rfft(x, axis=1))[:, 1:wtlib.NH]
    pk = S.max(axis=1, keepdims=True)
    db = 20 * np.log10(np.maximum(S, 1e-12) / np.maximum(pk, 1e-300))
    db = np.maximum(db, -90.0)
    db[pk[:, 0] <= 0] = -90.0
    seg = db[:, :_E[-1] - 1]
    return np.add.reduceat(seg, _E[:-1] - 1, axis=1) / np.diff(_E)[None, :]


def analyse(fr):
    """Every bar and number for one rendered table. Returns a dict; '_frames' carries the float32
    frames when the shape is right (the caller stores them and pops the key)."""
    out = dict(errors=[], warnings=[])
    try:
        a = np.asarray(fr)
    except Exception:
        out['errors'].append("returned %s, not an array" % type(fr).__name__)
        return out
    if a.shape != (wtlib.FRAMES, wtlib.SIZE):
        out['errors'].append("shape %s, expected (%d, %d)" % (a.shape, wtlib.FRAMES, wtlib.SIZE))
        return out
    if not (np.issubdtype(a.dtype, np.floating) or np.issubdtype(a.dtype, np.integer)):
        out['errors'].append("dtype %s is not numeric" % a.dtype)
        return out
    if a.dtype != np.float32:
        out['warnings'].append("dtype %s, not float32 — was wtlib.finalize() the last step?" % a.dtype)
    x = a.astype(np.float64)
    bad = ~np.isfinite(x)
    if bad.any():
        out['errors'].append("finite: %d NaN/inf samples in %d frame(s)" % (int(bad.sum()), int(bad.any(axis=1).sum())))
        x = np.nan_to_num(x, nan=0.0, posinf=0.0, neginf=0.0)
    pk = np.abs(x).max(axis=1)
    pe = np.abs(pk - 1.0)
    i = int(np.argmax(pe))
    out['peak_err'] = float(pe[i])
    if pe[i] > PEAK_TOL:
        out['errors'].append("peak: frame %d peaks at %.6g, not 1.0 (%d frames off) — end with wtlib.finalize()"
                             % (i, pk[i], int((pe > PEAK_TOL).sum())))
    dc = np.abs(x.mean(axis=1))
    j = int(np.argmax(dc))
    out['dc_max'] = float(dc[j])
    if dc[j] > DC_TOL:
        out['errors'].append("dc: frame %d has mean %.3g (%d frames over %.0e) — end with wtlib.finalize()"
                             % (j, dc[j], int((dc > DC_TOL).sum()), DC_TOL))
    xf = x.astype(np.float32)
    m = wtlib.measure(xf)
    out.update(m)
    if m['dead']:
        out['errors'].append("dead: %d silent frame(s)" % m['dead'])
    r = seam_ratios(xf)
    k = int(np.argmax(r))
    out['seam'] = float(r[k]); out['seam_frame'] = k; out['seam_frames_over'] = int((r > SEAM_MAX).sum())
    if r[k] > SEAM_MAX:
        out['errors'].append("seam: frame %d wraps with a jump %.2fx its typical step (bar %.1f; %d/128 frames over)"
                             " — build it periodic, or np.roll every frame so the wrap lands on a quiet point"
                             % (k, r[k], SEAM_MAX, out['seam_frames_over']))
    rows = band_rows(xf)
    trav = np.abs(rows - rows[0]).mean(axis=1)
    adj = np.abs(np.diff(rows, axis=0)).mean(axis=1)
    out['travel_db'] = float(trav.max())
    out['jump_db'] = float(adj.max()); out['jump_frame'] = int(np.argmax(adj)) + 1
    if trav.max() < STATIC_MIN_DB:
        out['errors'].append("static: no frame differs from frame 0 by %.1f dB — the frame axis does not travel"
                             % STATIC_MIN_DB)
    out['fp'] = wtlib.fingerprint(xf)
    out['_frames'] = xf
    return out


# ── module loading ───────────────────────────────────────────────────────────────────────────
def resolve_module(p):
    for c in (p, os.path.join(HERE, p), os.path.join(HERE, p + ".py")):
        if os.path.isfile(c):
            return os.path.abspath(c)
    raise SystemExit("wtkit: no such module: %s" % p)


def load_module(path):
    path = os.path.abspath(path)
    for p in (os.path.dirname(path), HERE):          # HERE ends up first: its wtlib wins
        if p in sys.path:
            sys.path.remove(p)
        sys.path.insert(0, p)
    name = os.path.splitext(os.path.basename(path))[0]
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    try:
        spec.loader.exec_module(mod)
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return mod


def _fmt_exc(e):
    tb = traceback.extract_tb(e.__traceback__)
    where = ""
    if tb:
        f = tb[-1]
        where = " (%s:%d in %s)" % (os.path.basename(f.filename), f.lineno, f.name)
    msg = str(e).strip().splitlines()[0] if str(e).strip() else ""
    return ("%s: %s%s" % (type(e).__name__, msg, where))[:400]


def module_info(path):
    """Import in THIS process and validate the contract. Per-table contract errors land in
    table_errors[i] (that table fails); every error is also listed in errors (gate500: hard)."""
    path = os.path.abspath(path)
    info = dict(path=path, module=os.path.splitext(os.path.basename(path))[0], errors=[], warnings=[],
                idents=[], cats=[], table_errors=[], fatal=False)
    try:
        mod = load_module(path)
    except BaseException as e:
        info['errors'].append("import failed: " + _fmt_exc(e)); info['fatal'] = True
        return info
    T = getattr(mod, "TABLES", None)
    if not isinstance(T, (list, tuple)) or not T:
        info['errors'].append("no TABLES list (or it is empty)"); info['fatal'] = True
        return info
    C = getattr(mod, "CATEGORY", None)
    if not isinstance(C, dict):
        info['errors'].append("no CATEGORY dict"); C = {}
    seen, ships = {}, {}
    for k, item in enumerate(T):
        errs = []
        if not (isinstance(item, (tuple, list)) and len(item) == 2 and isinstance(item[0], str) and callable(item[1])):
            ident = "<TABLES[%d]>" % k
            errs.append('malformed TABLES entry — must be ("IDENT", fn)')
            info['idents'].append(ident); info['cats'].append(None); info['table_errors'].append(errs)
            info['errors'].append("%s: %s" % (ident, errs[0]))
            continue
        ident = item[0]
        if ident.startswith("TERRA ") or ident == "TERRA":
            errs.append('ident: carries the retired "TERRA " prefix — new identifiers are the bare name')
        if ident != ident.upper():
            errs.append("ident: not ALL CAPS")
        if not IDENT_RE.match(ident) or "  " in ident or ident.endswith((" ", ".")):
            errs.append("ident: only A-Z 0-9, single spaces and - ' & . (it becomes a file name)")
        if ident in seen:
            errs.append("ident: duplicate identifier (also TABLES[%d])" % seen[ident])
        else:
            seen[ident] = k
        cat = C.get(ident)
        if cat is None:
            errs.append("category: no CATEGORY entry")
        elif cat not in QUOTA_NEW:
            errs.append("category: '%s' is not one of the 13 (%s)" % (cat, ", ".join(CATEGORIES)))
        try:
            key = wtlib.shipping_name(ident).casefold()
            if key in ships and ships[key] != ident:
                errs.append("name: shipping name '%s' is also produced by %s" % (wtlib.shipping_name(ident), ships[key]))
            ships.setdefault(key, ident)
        except Exception as e:
            errs.append("name: shipping_name failed: " + _fmt_exc(e))
        info['idents'].append(ident); info['cats'].append(cat); info['table_errors'].append(errs)
        info['errors'].extend("%s: %s" % (ident, x) for x in errs)
    extra = sorted(set(C) - set(seen))
    if extra:
        info['warnings'].append("CATEGORY keys with no TABLES entry: " + ", ".join(map(str, extra)))
    return info


# ── cache key ────────────────────────────────────────────────────────────────────────────────
def _deps(path):
    seen, stack = [], [os.path.abspath(path)]
    while stack:
        p = stack.pop()
        if p in seen:
            continue
        seen.append(p)
        try:
            tree = ast.parse(open(p, encoding="utf-8").read())
        except Exception:
            continue
        mods = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                mods.update(a.name.split(".")[0] for a in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module and node.level == 0:
                mods.add(node.module.split(".")[0])
        for m in mods:
            for d in (os.path.dirname(p), HERE):
                q = os.path.join(d, m + ".py")
                if os.path.isfile(q):
                    stack.append(os.path.abspath(q)); break
    return seen


def cache_key(path):
    deps = set(_deps(path)) | {os.path.abspath(wtlib.__file__), os.path.abspath(__file__)}
    sig = []
    for p in sorted(deps):
        st = os.stat(p)
        sig.append([p, st.st_mtime_ns, st.st_size])
    return hashlib.sha1(json.dumps([TOOL_VERSION, sig]).encode()).hexdigest()


# ── the worker pool: one import per worker, per-table deadline, a hung table is KILLED ──────
def _worker(conn, path, cache_dir):
    try:
        with open(os.devnull, "w") as dn, contextlib.redirect_stdout(dn):
            mod = load_module(path)
        tables = list(mod.TABLES)
    except BaseException as e:
        try:
            conn.send(("importerror", _fmt_exc(e)))
        except Exception:
            pass
        return
    conn.send(("ready", os.getpid()))
    while True:
        try:
            idx = conn.recv()
        except (EOFError, OSError):
            return
        if idx is None:
            return
        t0 = time.time()
        buf = io.StringIO()
        try:
            fn = tables[idx][1]
            with contextlib.redirect_stdout(buf):
                fr = fn()
            res = analyse(fr)
            frames = res.pop("_frames", None)
            if frames is not None:
                np.save(os.path.join(cache_dir, "%03d.npy" % idx), frames)
                res['npy'] = "%03d.npy" % idx
        except BaseException as e:
            res = dict(errors=["raised " + _fmt_exc(e)], warnings=[])
        res['secs'] = round(time.time() - t0, 3)
        txt = buf.getvalue()
        if txt:
            res['stdout'] = txt[-1500:]
        conn.send(("done", idx, res))


def _run_tables(path, n, jobs, timeout, cache_dir, tick=None):
    ctx = mp.get_context("spawn")        # macOS: never fork a process that has touched Accelerate
    pending, results, workers = list(range(n)), {}, []
    nw = max(1, min(int(jobs), n))
    state = dict(import_error=None)

    def spawn():
        a, b = ctx.Pipe(duplex=True)
        p = ctx.Process(target=_worker, args=(b, path, cache_dir), daemon=True)
        p.start(); b.close()
        workers.append(dict(proc=p, conn=a, task=None, t0=0.0, born=time.time(), ready=False))

    def retire(w, kill=False):
        if kill and w['proc'].is_alive():
            w['proc'].kill()
        try:
            w['conn'].close()
        except Exception:
            pass
        w['proc'].join(timeout=5)
        if w in workers:
            workers.remove(w)

    for _ in range(nw):
        spawn()
    while workers and (pending or any(w['task'] is not None for w in workers)):
        for c in mp_wait([w['conn'] for w in workers], timeout=0.2):
            w = next((w for w in workers if w['conn'] is c), None)
            if w is None:
                continue
            try:
                msg = c.recv()
            except (EOFError, OSError):
                w['proc'].join(timeout=2)
                code = w['proc'].exitcode
                if w['task'] is not None:
                    results[w['task']] = dict(errors=["worker died (exit code %s) while rendering — a hard crash "
                                                      "in native code, or os._exit()?" % code], warnings=[], transient=True)
                    if tick: tick(w['task'], results[w['task']])
                elif not w['ready']:
                    state['import_error'] = state['import_error'] or "worker died while importing (exit code %s)" % code
                retire(w)
                continue
            if msg[0] == "importerror":
                state['import_error'] = msg[1]
                retire(w)
                continue
            if msg[0] == "ready":
                w['ready'] = True
            elif msg[0] == "done":
                results[msg[1]] = msg[2]
                w['task'] = None
                if tick: tick(msg[1], msg[2])
            if w['ready'] and w['task'] is None:
                if pending:
                    w['task'] = pending.pop(0); w['t0'] = time.time()
                    c.send(w['task'])
                else:
                    try:
                        c.send(None)
                    except Exception:
                        pass
                    retire(w)
        now = time.time()
        for w in list(workers):
            if w['task'] is not None and now - w['t0'] > timeout:
                results[w['task']] = dict(errors=["TIMEOUT: still running after %.0f s (per-table limit, --timeout)" % timeout],
                                          warnings=[], transient=True)
                if tick: tick(w['task'], results[w['task']])
                retire(w, kill=True)
            elif not w['ready'] and now - w['born'] > max(timeout, 120.0):
                state['import_error'] = state['import_error'] or "module import in a worker took over %.0f s" % max(timeout, 120.0)
                retire(w, kill=True)
        if state['import_error']:
            break
        busy = sum(1 for w in workers if w['task'] is not None)
        while pending and len(workers) < min(nw, len(pending) + busy):
            spawn()
    for w in list(workers):
        retire(w, kill=True)
    if state['import_error']:
        for i in range(n):
            if i not in results:
                results[i] = dict(errors=["module failed in a worker: " + state['import_error']], warnings=[], transient=True)
    return results


def render_module(path, jobs=None, timeout=DEFAULT_TIMEOUT, fresh=False, cache_root=None, log=print, info=None):
    """Render (or load from cache) every TABLES entry of one module. Returns
    dict(module, path, info, rows, cached, secs, cache_dir); rows[i] carries ident, category, name
    (shipping), ok, errors (contract + render), warnings, measure() numbers, seam, travel_db,
    jump_db, fp (np array) and npy_path (the float32 frames) when the shape was right."""
    path = os.path.abspath(path)
    info = info or module_info(path)
    name = info['module']
    cache_root = cache_root or os.path.join(wt500_dir(), "cache")
    cdir = os.path.join(cache_root, "%s-%s" % (name, hashlib.sha1(path.encode()).hexdigest()[:8]))
    t0 = time.time()
    n = len(info['idents'])
    raw, cached = [], False
    if n and not info['fatal']:
        key = cache_key(path)
        meta_p = os.path.join(cdir, "meta.json")
        if not fresh and os.path.isfile(meta_p):
            try:
                with open(meta_p) as f:
                    meta = json.load(f)
                if (meta.get('key') == key and meta.get('idents') == info['idents']
                        and all(os.path.isfile(os.path.join(cdir, r['npy'])) for r in meta['rows'] if r.get('npy'))):
                    raw, cached = meta['rows'], True
            except Exception:
                raw, cached = [], False
        if not cached:
            if os.path.isdir(cdir):
                shutil.rmtree(cdir)
            os.makedirs(cdir, exist_ok=True)
            cnt = [0]

            def tick(i, r):
                cnt[0] += 1
                if log:
                    log("   [%3d/%d] %-34s %7.2f s  %s" % (cnt[0], n, info['idents'][i], r.get('secs', 0.0),
                                                          "ok" if not r.get('errors') else "FAIL"))
            res = _run_tables(path, n, jobs or os.cpu_count() or 4, timeout, cdir, tick)
            raw = [res.get(i, dict(errors=["no result"], warnings=[], transient=True)) for i in range(n)]
            if not any(r.get('transient') for r in raw):      # timeouts/crashes may be load: never cache them
                dump = [dict(r, fp=np.asarray(r['fp']).tolist()) if 'fp' in r else r for r in raw]
                with open(meta_p + ".tmp", "w") as f:
                    json.dump(dict(key=key, idents=info['idents'], rows=dump, tool=TOOL_VERSION,
                                   made=time.strftime("%Y-%m-%d %H:%M:%S")), f)
                os.replace(meta_p + ".tmp", meta_p)
    rows = []
    for i in range(len(raw)):
        r = dict(raw[i])
        r['index'] = i
        r['ident'] = info['idents'][i]
        r['category'] = info['cats'][i]
        r['errors'] = list(info['table_errors'][i]) + list(r.get('errors', []))
        r['warnings'] = list(r.get('warnings', []))
        if 'fp' in r:
            r['fp'] = np.asarray(r['fp'], dtype=np.float64)
        try:
            r['name'] = wtlib.shipping_name(r['ident'])
        except Exception:
            r['name'] = r['ident']
        r['npy_path'] = os.path.join(cdir, r['npy']) if r.get('npy') else None
        r['ok'] = not r['errors']
        rows.append(r)
    return dict(module=name, path=path, info=info, rows=rows, cached=cached, secs=time.time() - t0, cache_dir=cdir)


def nearest(fp, F, exclude=None):
    """(index, distance) of fp's nearest row in F (wtlib.distance, vectorised)."""
    if F is None or len(F) == 0:
        return None, float("inf")
    d = np.abs(np.asarray(F) - fp).mean(axis=1)
    if exclude is not None:
        d[exclude] = np.inf
    j = int(np.argmin(d))
    return (j, float(d[j])) if np.isfinite(d[j]) else (None, float("inf"))


# ── contact sheets ───────────────────────────────────────────────────────────────────────────
_BG, _PANEL, _EDGE = (11, 14, 17), (21, 26, 31), (46, 54, 62)
_BAD, _WARN = (232, 80, 80), (242, 172, 60)
_TXT, _SUB, _DIM = (238, 242, 244), (168, 180, 188), (112, 124, 132)
_BACKL, _FRONTL, _HILITE = (28, 70, 68), (118, 220, 202), (255, 200, 87)
_FONTS = {}


def _font(size, bold=False):
    from PIL import ImageFont
    k = (size, bold)
    if k in _FONTS:
        return _FONTS[k]
    f = None
    for p, i in (("/System/Library/Fonts/Helvetica.ttc", 1 if bold else 0),
                 ("/System/Library/Fonts/HelveticaNeue.ttc", 1 if bold else 0),
                 ("/Library/Fonts/Arial.ttf", 0),
                 ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf", 0)):
        try:
            f = ImageFont.truetype(p, size, index=i); break
        except Exception:
            continue
    if f is None:
        try:
            f = ImageFont.load_default(size)
        except TypeError:
            f = ImageFont.load_default()
    _FONTS[k] = f
    return f


def _fit(d, text, maxw, size, bold, S, minsize=10):
    while size > minsize:
        f = _font(size * S, bold)
        if d.textlength(text, font=f) <= maxw:
            return text, f
        size -= 1
    f = _font(minsize * S, bold)
    while text and d.textlength(text + "…", font=f) > maxw:
        text = text[:-1]
    return text + "…", f


def _waterfall(d, frames, px0, py0, px1, py1, S, nshow=40):
    W, H = px1 - px0, py1 - py0
    lw = 0.70 * W                      # one frame's width; the rest is the diagonal recession
    ox = W - lw
    A = 0.25 * H                       # amplitude of one frame (peak = 1 after finalize)
    oy = H - 2 * A - 2 * S
    yfront = py1 - A - S
    F_, N_ = frames.shape
    sel = np.unique(np.round(np.linspace(0, F_ - 1, nshow)).astype(int))
    xs = np.linspace(0.0, lw, N_)
    ncol = max(8, int(lw / S))         # occlusion polygon: the upper envelope at 1x-pixel resolution
    cidx = np.linspace(0, N_, ncol + 1)[:-1].astype(int)
    cx = (np.append(cidx[1:], N_) + cidx - 1) / 2.0 / (N_ - 1) * lw
    n = len(sel)
    for j in range(n - 1, -1, -1):
        fr = frames[sel[j]].astype(np.float64)
        f = j / (n - 1) if n > 1 else 0.0
        x0 = px0 + ox * f
        yc = yfront - oy * f
        env = np.maximum.reduceat(fr, cidx)
        poly = [(x0, yc + A + S)] + list(zip((x0 + cx).tolist(), (yc - A * env).tolist())) + [(x0 + lw, yc + A + S)]
        d.polygon(poly, fill=_PANEL)
        pts = list(zip((x0 + xs).tolist(), (yc - A * fr).tolist()))
        if j == 0:
            d.line(pts, fill=_HILITE, width=max(2, int(2.5 * S)))
        else:
            g = (1.0 - f) ** 1.2
            col = tuple(int(_BACKL[c] + (_FRONTL[c] - _BACKL[c]) * g) for c in range(3))
            d.line(pts, fill=col, width=S)


def _draw_tile(d, t, x0, y0, tw, th, S):
    pad = 5 * S
    edge = _BAD if t['bad'] else (_WARN if t['warn'] else _EDGE)
    d.rounded_rectangle([x0 + pad, y0 + pad, x0 + tw - pad, y0 + th - pad], radius=7 * S, fill=_PANEL,
                        outline=edge, width=(2 * S if (t['bad'] or t['warn']) else S))
    lx = x0 + pad + 9 * S
    num = "#%d" % t['n']
    fnum = _font(12 * S)
    nw = d.textlength(num, font=fnum)
    d.text((x0 + tw - pad - 9 * S - nw, y0 + pad + 10 * S), num, font=fnum, fill=_DIM)
    name, fn = _fit(d, t['name'], tw - 2 * pad - 30 * S - nw, 19, True, S)
    d.text((lx, y0 + pad + 7 * S), name, font=fn, fill=_TXT)
    fs = _font(14 * S)
    d.text((lx, y0 + pad + 32 * S), t['stats'], font=fs, fill=_SUB)
    fc = _font(12 * S)
    cw = d.textlength(t['cat'], font=fc)
    d.text((x0 + tw - pad - 9 * S - cw, y0 + pad + 34 * S), t['cat'], font=fc, fill=_DIM)
    px0, px1 = x0 + pad + 9 * S, x0 + tw - pad - 9 * S
    py0, py1 = y0 + pad + 56 * S, y0 + th - pad - 25 * S
    if t.get('npy') and os.path.isfile(t['npy']):
        _waterfall(d, np.load(t['npy']), px0, py0, px1, py1, S)
    else:
        fe = _font(13 * S)
        msg = t['foot'] or "no frames"
        yy = py0 + 20 * S
        while msg and yy < py1:
            k = len(msg)
            while k > 1 and d.textlength(msg[:k], font=fe) > (px1 - px0):
                k -= 1
            d.text((px0, yy), msg[:k], font=fe, fill=_BAD)
            msg = msg[k:].lstrip(); yy += 18 * S
    foot, ff = _fit(d, t['foot'], px1 - px0, 12, False, S, 9)
    d.text((px0, y0 + th - pad - 20 * S), foot, font=ff, fill=_BAD if t['bad'] else (_WARN if t['warn'] else _DIM))


def render_sheet(job):
    """job = dict(png, title, subtitle, tiles=[dict(n,name,stats,cat,foot,bad,warn,npy)]) -> png path.
    Drawn at 2x and LANCZOS-reduced to 1200 px wide, so the lines are antialiased."""
    from PIL import Image, ImageDraw
    S, TW, TH, HDR, COLS, ROWS = 2, 400, 336, 42, 3, 4
    W, H = TW * COLS, HDR + TH * ROWS
    img = Image.new("RGB", (W * S, H * S), _BG)
    d = ImageDraw.Draw(img)
    d.text((12 * S, 11 * S), job['title'], font=_font(18 * S, True), fill=_TXT)
    fsub = _font(13 * S)
    d.text((W * S - 12 * S - d.textlength(job['subtitle'], font=fsub), 15 * S), job['subtitle'], font=fsub, fill=_SUB)
    for k, t in enumerate(job['tiles'][:COLS * ROWS]):
        r, c = divmod(k, COLS)
        _draw_tile(d, t, c * TW * S, (HDR + r * TH) * S, TW * S, TH * S, S)
    rs = getattr(Image, "Resampling", Image)
    img = img.resize((W, H), rs.LANCZOS)
    img.save(job['png'], optimize=True)
    return job['png']


def render_sheets(jobs_list, nproc):
    if not jobs_list:
        return []
    if nproc <= 1 or len(jobs_list) == 1:
        return [render_sheet(j) for j in jobs_list]
    from concurrent.futures import ProcessPoolExecutor
    with ProcessPoolExecutor(max_workers=min(nproc, len(jobs_list)), mp_context=mp.get_context("spawn")) as ex:
        return list(ex.map(render_sheet, jobs_list))


# ── check ────────────────────────────────────────────────────────────────────────────────────
def _jsonable(v):
    if isinstance(v, dict):
        return {k: _jsonable(x) for k, x in v.items() if k not in ('fp',)}
    if isinstance(v, (list, tuple)):
        return [_jsonable(x) for x in v]
    if isinstance(v, (np.integer,)):
        return int(v)
    if isinstance(v, (np.floating, float)):
        v = float(v)
        return v if math.isfinite(v) else None
    if isinstance(v, np.ndarray):
        return v.tolist()
    return v


def cmd_check(a):
    import gate, fixed_set
    path = resolve_module(a.module)
    jobs = a.jobs or os.cpu_count() or 4
    t_all = time.time()
    print("\n══ wtkit check · %s ══" % path)
    floor, ceil_ = gate.calibrate()
    print("collision floor %.2f dB  (gate.calibrate(): saw vs gently re-tilted saw)   ceiling %.2f dB (saw vs sine)"
          % (floor, ceil_))
    fixed = fixed_set.load(log=print)
    print("fixed set: %s" % fixed['summary'])
    bn = fixed_set.builtin_names()
    info = module_info(path)
    for w in info['warnings']:
        print("   warning: " + w)
    if info['fatal']:
        for e in info['errors']:
            print("   FATAL: " + e)
        return 1
    print("module %s: %d tables — rendering with %d workers, timeout %.0f s/table"
          % (info['module'], len(info['idents']), min(jobs, len(info['idents'])), a.timeout))
    R = render_module(path, jobs=jobs, timeout=a.timeout, fresh=a.fresh, log=print, info=info)
    rows = R['rows']
    print("rendered %d tables in %.1f s%s" % (len(rows), R['secs'], "  (cache hit: module unchanged, nothing re-rendered)" if R['cached'] else ""))

    fx_names, fx_kinds, fx_cats = fixed['names'], fixed['kinds'], fixed['cats']
    fixed_cf = {}
    for nm, k, c in zip(fx_names, fx_kinds, fx_cats):
        if k == "shipped":
            fixed_cf.setdefault(nm.casefold(), (nm, c))
    b_cf = {}
    for nm in bn['names']:
        b_cf.setdefault(nm.casefold(), nm)
    have = [i for i, r in enumerate(rows) if r.get('fp') is not None]
    MF = np.array([rows[i]['fp'] for i in have]) if have else np.zeros((0, 1))
    for pos, i in enumerate(have):
        r = rows[i]
        j, dm = nearest(r['fp'], MF, exclude=pos) if len(have) > 1 else (None, float("inf"))
        r['nn_module'] = (dict(ident=rows[have[j]]['ident'], name=rows[have[j]]['name'], dist=dm, below_floor=dm < floor)
                          if j is not None else None)
        k, df = nearest(r['fp'], fixed['fps'])
        r['nn_fixed'] = (dict(name=fx_names[k], kind=fx_kinds[k], category=fx_cats[k], dist=df, below_floor=df < floor)
                         if k is not None else None)
    for r in rows:
        fl = []
        if r.get('nn_module') and r['nn_module']['below_floor']:
            fl.append("collides in-module with %s (%.2f dB < floor %.2f)" % (r['nn_module']['ident'], r['nn_module']['dist'], floor))
        if r.get('nn_fixed') and r['nn_fixed']['below_floor']:
            fl.append("collides with fixed %s '%s' (%s) (%.2f dB < floor %.2f)" % (r['nn_fixed']['kind'], r['nn_fixed']['name'],
                      r['nn_fixed']['category'], r['nn_fixed']['dist'], floor))
        key = r['name'].casefold()
        if key in fixed_cf:
            fl.append("name '%s' is taken by fixed table '%s' (%s)" % (r['name'], fixed_cf[key][0], fixed_cf[key][1]))
        if key in b_cf:
            fl.append("name '%s' is taken by built-in '%s'" % (r['name'], b_cf[key]))
        r['flags'] = fl

    # ── print ──
    print("\n  %3s  %-6s %-30s %-12s %5s %6s %6s %6s   %-26s %-26s" % ("#", "status", "IDENT", "category", "h60", "span",
          "seam", "trav", "nearest in module (dB)", "nearest fixed (dB)"))
    print("  " + "-" * 146)

    def nn_s(x, key='name'):
        if not x:
            return "-"
        return ("%s %.1f%s" % (x[key][:20], x['dist'], "!" if x['below_floor'] else ""))
    for r in rows:
        st = "ok" if r['ok'] and not r['flags'] else ("FAIL" if not r['ok'] else "FLAG")
        print("  %3d  %-6s %-30s %-12s %5s %6s %6s %6s   %-26s %-26s" % (
            r['index'] + 1, st, r['ident'][:30], (r['category'] or "?")[:12],
            r.get('harm60', "-"), ("%.1f" % r['span']) if 'span' in r else "-",
            ("%.2f" % r['seam']) if 'seam' in r else "-", ("%.1f" % r['travel_db']) if 'travel_db' in r else "-",
            nn_s(r.get('nn_module'), 'ident'), nn_s(r.get('nn_fixed'))))
        for e in r['errors']:
            print("         ✗ " + e)
        for f in r['flags']:
            print("         ! " + f)
        for w in r['warnings']:
            print("         · " + w)

    # ── outputs ──
    out_root = a.out or os.path.join(wt500_dir(), "cand")
    od = os.path.join(out_root, info['module'])
    os.makedirs(od, exist_ok=True)
    for f in glob.glob(os.path.join(od, "*.wav")) + glob.glob(os.path.join(od, "sheet_*.png")):
        os.remove(f)
    nw = 0
    for r in rows:
        if r['npy_path']:
            wtlib.write_wav(os.path.join(od, r['ident'] + ".wav"), np.load(r['npy_path'])); nw += 1
    ok_n = sum(1 for r in rows if r['ok'] and not r['flags'])
    bar_n = sum(1 for r in rows if not r['ok'])
    flag_n = sum(1 for r in rows if r['ok'] and r['flags'])
    seams = [(r['seam'], r['ident'], r['seam_frame']) for r in rows if 'seam' in r]
    worst_seam = max(seams) if seams else None
    mods = [(r['nn_module']['dist'], r['ident'], r['nn_module']['ident']) for r in rows if r.get('nn_module')]
    fxs = [(r['nn_fixed']['dist'], r['ident'], r['nn_fixed']['name']) for r in rows if r.get('nn_fixed')]
    summary = dict(module=info['module'], path=path, floor_db=floor, ceiling_db=ceil_, fixed=fixed['summary'],
                   builtin_names_source=bn['source'],
                   bars=dict(PEAK_TOL=PEAK_TOL, DC_TOL=DC_TOL, SEAM_MAX=SEAM_MAX, SEAM_TOP=SEAM_TOP,
                             STATIC_MIN_DB=STATIC_MIN_DB, timeout_s=a.timeout),
                   counts=dict(tables=len(rows), ok=ok_n, failed_bars=bar_n, flagged=flag_n),
                   worst_seam=dict(ratio=worst_seam[0], ident=worst_seam[1], frame=worst_seam[2]) if worst_seam else None,
                   closest_in_module=dict(dist=min(mods)[0], a=min(mods)[1], b=min(mods)[2]) if mods else None,
                   closest_to_fixed=dict(dist=min(fxs)[0], ident=min(fxs)[1], fixed=min(fxs)[2]) if fxs else None,
                   module_errors=[e for e in info['errors']], warnings=info['warnings'],
                   rendered_s=R['secs'], cached=R['cached'],
                   tables=[_jsonable({k: v for k, v in r.items() if k not in ('npy', 'npy_path')}) for r in rows])
    with open(os.path.join(od, "metrics.json"), "w") as f:
        json.dump(summary, f, indent=1)
    sheets = []
    if not a.no_sheets:
        per = 12
        chunks = [rows[i:i + per] for i in range(0, len(rows), per)]
        jl = []
        for si, ch in enumerate(chunks):
            tiles = []
            for r in ch:
                if not r['ok']:
                    foot = r['errors'][0]
                elif r['flags']:
                    foot = r['flags'][0]
                else:
                    nf = r.get('nn_fixed')
                    foot = ("nearest fixed: %s %.1f dB   seam %.2f" % (nf['name'], nf['dist'], r['seam'])) if nf else ""
                tiles.append(dict(n=r['index'] + 1, name=r['name'], cat=r['category'] or "?",
                                  stats=("harm60 %d   travel %.1f st" % (r['harm60'], r['span'])) if 'harm60' in r else "not rendered",
                                  foot=foot, bad=not r['ok'], warn=bool(r['flags']), npy=r['npy_path']))
            jl.append(dict(png=os.path.join(od, "sheet_%02d.png" % (si + 1)),
                           title="%s — sheet %d/%d" % (info['module'], si + 1, len(chunks)),
                           subtitle="tables %d–%d of %d · floor %.2f dB · frame 1 in front" % (
                               ch[0]['index'] + 1, ch[-1]['index'] + 1, len(rows), floor),
                           tiles=tiles))
        t0 = time.time()
        sheets = render_sheets(jl, jobs)
        print("\n  contact sheets (%.1f s):" % (time.time() - t0))
        for s in sheets:
            print("    " + s)
    print("\n  wrote %d wav + metrics.json to %s" % (nw, od))
    if worst_seam:
        print("  worst seam ratio %.2f (%s, frame %d) — bar %.1f" % (worst_seam[0], worst_seam[1], worst_seam[2], SEAM_MAX))
    if mods:
        print("  closest pair inside the module  %.2f dB  %s vs %s" % min(mods))
    if fxs:
        print("  closest to the fixed set        %.2f dB  %s vs '%s'" % min(fxs))
    verdict = "PASS" if ok_n == len(rows) and not info['errors'] else "FAIL"
    print("\n  %s — %d tables: %d ok · %d failed a bar · %d flagged (collision / name)   [%.1f s total]\n"
          % (verdict, len(rows), ok_n, bar_n, flag_n, time.time() - t_all))
    return 0 if verdict == "PASS" else 1


# ── selftest ─────────────────────────────────────────────────────────────────────────────────
def cmd_selftest(a):
    import gate
    fails = []

    def ck(cond, what):
        print("  %s  %s" % ("ok  " if cond else "FAIL", what))
        if not cond:
            fails.append(what)
    print("\n══ wtkit selftest ══")
    F, n = wtlib.FRAMES, wtlib.n_ax
    s = np.linspace(0, 1, F)[:, None]
    saw = wtlib.finalize(wtlib.cycles_from_mags(np.tile(1.0 / n, (F, 1))))
    sweep = wtlib.finalize(wtlib.cycles_from_mags((1.0 / n) * (n[None, :] < (2 + s * 900))))
    rows = band_rows(sweep)
    ref = wtlib.fingerprint(sweep).reshape(16, -1)
    ck(np.allclose(rows[wtlib._FSEL], ref, atol=1e-9), "band_rows() == wtlib.fingerprint() rows (max diff %.2e)"
       % np.abs(rows[wtlib._FSEL] - ref).max())
    e1 = np.zeros((F, wtlib.NH)); e1[:, 0] = 1
    sine = wtlib.finalize(wtlib.cycles_from_mags(e1))
    coh = wtlib.finalize(wtlib.cycles_from_mags(np.tile(1.0 / n, (F, 1)), phase=np.full(wtlib.NH, -np.pi / 2)))
    t = wtlib.t
    broken = wtlib.finalize(np.tile(np.sin(2 * np.pi * 1.37 * t), (F, 1)))
    stairs = wtlib.finalize(np.tile(np.floor(t * 16) / 16 - 0.5, (F, 1)))
    for nm, x, want in (("random-phase saw", saw, True), ("swept saw", sweep, True), ("sine", sine, True),
                        ("16-step stair, riser mid-cycle", np.roll(stairs, 64, axis=1), True),
                        ("1.37-cycle sine (broken wrap)", broken, False), ("16-step stair, riser on the seam", stairs, False),
                        ("COH-phase saw (riser on the seam)", coh, False),
                        ("COH-phase saw rolled 1024", np.roll(coh, 1024, axis=1), True)):
        r = seam_ratios(x).max()
        ck((r <= SEAM_MAX) == want, "seam %-36s %7.2f  (%s)" % (nm, r, "passes" if r <= SEAM_MAX else "fails"))
    res = analyse(saw)
    ck(any(e.startswith("static") for e in res['errors']), "a 128-identical-frame table fails 'static'")
    res = analyse(sweep)
    ck(not res['errors'], "a swept saw passes every bar (%s)" % (res['errors'] or "no errors"))
    ck(any(e.startswith("peak") for e in analyse(sweep * 0.5)['errors']), "an un-normalised table fails 'peak'")
    ck(any(e.startswith("dc") for e in analyse(np.clip(sweep + 0.01, -1, 1))['errors']), "an offset table fails 'dc'")
    ck(any(e.startswith("shape") for e in analyse(sweep[:64])['errors']), "a 64-frame table fails 'shape'")
    floor, ceil_ = gate.calibrate()
    ck(abs(floor - 5.10) < 0.05, "gate.calibrate() floor %.3f dB (README: 5.10)" % floor)
    for s_, want in (("TERRA BIT LADDER", "Bit Ladder"), ("BIT LADDER", "Bit Ladder"), ("Terra - Bit Ladder", "Bit Ladder"),
                     ("TERRA XORFOLD", "XOR Fold"), ("PWM DUO", "PWM Duo"), ("RULE30 CELLS", "Rule 30 Cells")):
        ck(wtlib.shipping_name(s_) == want, "shipping_name(%r) == %r" % (s_, want))
    ck(wtlib.legacy_shipping_name("BIT LADDER") == "Terra - Bit Ladder" == wtlib.legacy_shipping_name("TERRA BIT LADDER"),
       "legacy_shipping_name() gives the fb612 form from both identifier shapes")
    print("\n  %s\n" % ("SELFTEST PASS" if not fails else "SELFTEST FAIL: %d" % len(fails)))
    return 0 if not fails else 1


def main(argv=None):
    ap = argparse.ArgumentParser(prog="wtkit.py", description="Terrain wavetable generator checker")
    sub = ap.add_subparsers(dest="cmd")
    c = sub.add_parser("check", help="render + check one generator module")
    c.add_argument("module")
    c.add_argument("--out", default=None, help="output root (default <wt500>/cand)")
    c.add_argument("--jobs", type=int, default=None)
    c.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT, help="seconds per table (default %.0f)" % DEFAULT_TIMEOUT)
    c.add_argument("--fresh", action="store_true", help="ignore the render cache")
    c.add_argument("--no-sheets", action="store_true")
    sub.add_parser("selftest", help="prove the metrics against known tables")
    a = ap.parse_args(argv)
    if a.cmd == "check":
        return cmd_check(a)
    if a.cmd == "selftest":
        return cmd_selftest(a)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
