#!/usr/bin/env python3
"""
fixed_set.py — fingerprint the FIXED part of the 500-table factory library.

    python3 fixed_set.py              # (re)build <wt500>/fixed_fp.npz + fixed_rename.json, print a summary
    python3 fixed_set.py --if-stale   # rebuild only when an input changed

FIXED = what the 334 new tables must stay away from and can never displace:
  * the 120 FLACs that already ship in Resources/Wavetables/<Category>/Terra - <Name>.flac. They are
    DECODED (soundfile) and never re-encoded or rewritten: presets find them by a hash of their bytes.
  * the 46 built-ins, IF <wt500>/builtins/index.json exists (another agent dumps them there: one wav
    per built-in, possibly with FEWER than 128 frames — the frame axis is resampled to 128 by linear
    interpolation between neighbouring frames before fingerprinting). Tagged kind="builtin".
Every table is fingerprinted with wtlib.fingerprint — the function both gates use — and carries its
plain name by the NEW law (wtlib.shipping_name: 'Terra - Bit Ladder' -> 'Bit Ladder'), its current
file name and its category. fixed_rename.json is the old -> new rename map for the 120, computed with
wtlib.legacy_shipping_name so the map and the files cannot disagree.
The npz carries a signature of every input (FLAC sizes + mtimes, the index and its wavs, wtlib.py);
load() rebuilds when it no longer matches, so wtkit.py / gate500.py never judge against a stale set.
"""
import sys, os, re, csv, json, glob, time, hashlib, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import numpy as np
import wtlib
from wtkit import wt500_dir, seam_ratios, SEAM_MAX

FIXED_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", "Resources", "Wavetables"))
PROCESSOR_CPP = os.path.normpath(os.path.join(HERE, "..", "..", "Source", "PluginProcessor.cpp"))
MANIFEST = os.path.join(HERE, "MANIFEST.csv")
FORMAT = 1


def paths():
    root = wt500_dir()
    return dict(root=root, npz=os.path.join(root, "fixed_fp.npz"), rename=os.path.join(root, "fixed_rename.json"),
                index=os.path.join(root, "builtins", "index.json"))


def flac_files():
    return sorted(glob.glob(os.path.join(FIXED_ROOT, "*", "*.flac")))


def _stat(p):
    st = os.stat(p)
    return [os.path.abspath(p), st.st_size, st.st_mtime_ns]


def signature():
    P = paths()
    items = [FORMAT] + [_stat(f) for f in flac_files()]
    if os.path.isfile(P['index']):
        items.append(_stat(P['index']))
        items += [_stat(f) for f in sorted(glob.glob(os.path.join(os.path.dirname(P['index']), "**", "*.wav"), recursive=True))]
    items.append(_stat(wtlib.__file__))
    items.append(_stat(os.path.abspath(__file__)))     # a parser fix here must invalidate the cache
    return hashlib.sha1(json.dumps(items).encode()).hexdigest()


def strip_terra(n):
    return n[len("Terra "):] if n.startswith("Terra ") else n


# ── built-ins ────────────────────────────────────────────────────────────────────────────────
def _first(e, keys):
    for k in keys:
        v = e.get(k)
        if v not in (None, ""):
            return v
    return None


def builtin_entries():
    """Parse <wt500>/builtins/index.json tolerantly. Accepts a list of entries, or a dict holding one
    under builtins/tables/entries/items/wavetables, or {name: file}. An entry names its wav with
    file/wav/path/filename and itself with name/display/title/label; optional frames (count),
    frame_size (samples per frame), category. Returns (entries or None, reason)."""
    P = paths()
    if not os.path.isfile(P['index']):
        return None, "no %s" % P['index']
    try:
        with open(P['index']) as f:
            doc = json.load(f)
    except Exception as e:
        return None, "unreadable %s (%s)" % (P['index'], e)
    items = doc
    if isinstance(doc, dict):
        for k in ("builtins", "tables", "entries", "items", "wavetables"):
            if isinstance(doc.get(k), (list, dict)):
                items = doc[k]; break
    if isinstance(items, dict):
        if all(isinstance(v, str) for v in items.values()):
            items = [dict(name=k, file=v) for k, v in items.items()]
        else:
            items = [dict(v, name=v.get("name", k)) if isinstance(v, dict) else dict(name=k) for k, v in items.items()]
    if not isinstance(items, list):
        return None, "index.json has no list of entries"
    base = os.path.dirname(P['index'])
    out = []
    for i, e in enumerate(items):
        if isinstance(e, str):
            e = dict(file=e)
        if not isinstance(e, dict):
            continue
        f = _first(e, ("file", "wav", "path", "filename", "fname"))
        nm = _first(e, ("name", "display", "display_name", "title", "label"))
        ix = _first(e, ("idx", "index", "id"))
        if f is None:       # no file key: the dumper's convention is <idx>.wav, else <name>.wav
            for c in ([("%s.wav" % ix)] if ix is not None else []) + ([("%s.wav" % nm)] if nm is not None else []):
                if os.path.isfile(os.path.join(base, c)):
                    f = c; break
        if nm is None and f:
            nm = os.path.splitext(os.path.basename(f))[0]
        if not f or nm is None:
            continue
        fp = f if os.path.isabs(f) else os.path.join(base, f)
        fr = _first(e, ("frames", "num_frames", "n_frames", "frame_count"))
        fs = _first(e, ("frame_size", "samples_per_frame", "table_size", "size"))
        out.append(dict(name=str(nm), file=fp, index=ix if ix is not None else i, category=str(e.get("category", "") or ""),
                        frames=int(fr) if isinstance(fr, (int, float)) else None,
                        frame_size=int(fs) if isinstance(fs, (int, float)) else None))
    return (out, "ok") if out else (None, "index.json lists no usable entries")


def _names_from_processor():
    src = open(PROCESSOR_CPP, encoding="utf-8", errors="replace").read()
    m = re.search(r"SYN_OSC_A_WT_PRESET[^;{]*?juce::StringArray\s*\{(.*?)\}", src, re.S)
    if not m:
        raise RuntimeError("SYN_OSC_A_WT_PRESET StringArray not found in " + PROCESSOR_CPP)
    body = "\n".join(line.split("//", 1)[0] for line in m.group(1).splitlines())
    return re.findall(r'"((?:[^"\\]|\\.)*)"', body)


def builtin_names():
    """The built-in roster's names (a leading 'Terra ' stripped): from index.json when present, else
    from the SYN_OSC_A_WT_PRESET StringArray in PluginProcessor.cpp."""
    ents, _ = builtin_entries()
    if ents:
        orig, src = [e['name'] for e in ents], paths()['index']
    else:
        orig, src = _names_from_processor(), PROCESSOR_CPP + " (SYN_OSC_A_WT_PRESET StringArray)"
    return dict(names=[strip_terra(n) for n in orig], originals=orig, source=src)


def resample_to_128(x, frames_hint=None, frame_size=None):
    """1-D samples -> (128, 2048) finalize()d frames. Frame length from frame_size, else len/frames,
    else 2048. A frame length other than 2048 is resampled per frame in the spectrum; the frame AXIS
    goes to 128 by linear interpolation between neighbouring frames."""
    x = np.asarray(x, dtype=np.float64).reshape(-1)
    n = x.size
    if frame_size:
        fs = int(frame_size)
    elif frames_hint and n % int(frames_hint) == 0:
        fs = n // int(frames_hint)
    elif n % wtlib.SIZE == 0:
        fs = wtlib.SIZE
    else:
        raise ValueError("%d samples is not a whole number of %d-sample frames (give frame_size)" % (n, wtlib.SIZE))
    K = n // fs
    if K < 1:
        raise ValueError("fewer samples than one frame")
    fr = x[:K * fs].reshape(K, fs)
    if fs != wtlib.SIZE:
        Sp = np.fft.rfft(fr, axis=1)
        T = np.zeros((K, wtlib.SIZE // 2 + 1), dtype=complex)
        m = min(Sp.shape[1], T.shape[1])
        T[:, :m] = Sp[:, :m]
        fr = np.fft.irfft(T, n=wtlib.SIZE, axis=1) * (wtlib.SIZE / fs)
    if K == 1:
        out = np.repeat(fr, wtlib.FRAMES, axis=0)
    else:
        p = np.linspace(0.0, K - 1, wtlib.FRAMES)
        i0 = np.minimum(np.floor(p).astype(int), K - 2)
        w = (p - i0)[:, None]
        out = (1 - w) * fr[i0] + w * fr[i0 + 1]
    return wtlib.finalize(out), K


def _analyse_job(job):
    import soundfile as sf
    try:
        if job['kind'] == "shipped":
            x, sr = sf.read(job['file'], dtype='float32', always_2d=True)
            x = x[:, 0]
            if x.size != wtlib.FRAMES * wtlib.SIZE:
                raise ValueError("%d samples, expected %d" % (x.size, wtlib.FRAMES * wtlib.SIZE))
            fr, K = x.reshape(wtlib.FRAMES, wtlib.SIZE), wtlib.FRAMES
        else:
            x, sr = sf.read(job['file'], dtype='float64', always_2d=True)
            fr, K = resample_to_128(x[:, 0], job.get('frames'), job.get('frame_size'))
        m = wtlib.measure(fr)
        return dict(ok=True, fp=wtlib.fingerprint(fr), harm60=m['harm60'], span=m['span'],
                    seam=float(seam_ratios(fr).max()), src_frames=int(K))
    except Exception as e:
        return dict(ok=False, error="%s: %s" % (type(e).__name__, e))


def _manifest_idents():
    out = {}
    if os.path.isfile(MANIFEST):
        with open(MANIFEST, newline="") as f:
            for row in csv.DictReader(f):
                out.setdefault(row['category'], []).append(row['name'])
    return out


def _resolve_ident(stem, cat, idmap):
    """Which historical identifier produced this file? Proven by the LEGACY law, not guessed."""
    order = idmap.get(cat, []) + [i for c, v in idmap.items() if c != cat for i in v]
    for ident in order:
        L = wtlib.legacy_shipping_name(ident)
        if stem == L or stem == L + " " + cat:
            return ident
    return ""


def build(jobs=None, log=print):
    from concurrent.futures import ProcessPoolExecutor
    import multiprocessing as mp
    P = paths()
    os.makedirs(P['root'], exist_ok=True)
    sig = signature()
    files = flac_files()
    if not files:
        raise SystemExit("fixed_set: no FLACs under %s" % FIXED_ROOT)
    idmap = _manifest_idents()
    J = []
    for f in files:
        cat = os.path.basename(os.path.dirname(f))
        stem = os.path.splitext(os.path.basename(f))[0]
        J.append(dict(kind="shipped", file=f, category=cat, current=stem, plain=wtlib.shipping_name(stem),
                      ident=_resolve_ident(stem, cat, idmap)))
    ents, why = builtin_entries()
    if ents:
        for e in ents:
            J.append(dict(kind="builtin", file=e['file'], category=e['category'], current=e['name'],
                          plain=strip_terra(e['name']), ident="", frames=e['frames'], frame_size=e['frame_size']))
    else:
        log("   built-ins: %s — fingerprinting the %d shipped FLACs only" % (why, len(files)))
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=min(jobs or os.cpu_count() or 4, len(J)), mp_context=mp.get_context("spawn")) as ex:
        res = list(ex.map(_analyse_job, J, chunksize=4))
    keep = []
    for j, r in zip(J, res):
        if not r['ok']:
            log("   !! %s '%s' (%s): %s — LEFT OUT of the fixed set" % (j['kind'], j['current'], j['file'], r['error']))
            continue
        keep.append((j, r))
    arr = lambda key, src="j": np.array([(j if src == "j" else r)[key] for j, r in keep])
    tmp = P['npz'] + ".tmp.npz"
    np.savez(tmp, fps=np.array([r['fp'] for _, r in keep]).reshape(len(keep), -1),
             names=arr('plain'), current=arr('current'), cats=arr('category'), kinds=arr('kind'),
             idents=arr('ident'), files=arr('file'), harm60=arr('harm60', 'r'), span=arr('span', 'r'),
             seam=arr('seam', 'r'), src_frames=arr('src_frames', 'r'), signature=np.array(sig),
             built=np.array(time.strftime("%Y-%m-%d %H:%M:%S")))
    os.replace(tmp, P['npz'])
    ren = [dict(category=j['category'], identifier=j['ident'], current=j['current'] + ".flac",
                plain=j['plain'] + ".flac", legacy_law_reproduces_current=bool(j['ident']))
           for j in J if j['kind'] == "shipped"]
    with open(P['rename'], "w") as f:
        json.dump(ren, f, indent=1)
    ns = sum(1 for j, _ in keep if j['kind'] == "shipped")
    nb = sum(1 for j, _ in keep if j['kind'] == "builtin")
    log("   fixed set built in %.1f s: %d shipped FLACs + %d built-ins -> %s" % (time.time() - t0, ns, nb, P['npz']))
    unres = [r['current'] for r in ren if not r['legacy_law_reproduces_current']]
    log("   rename map (%d): %s -> %s" % (len(ren), P['rename'],
        "legacy_shipping_name() reproduces every current file name" if not unres else "%d NOT reproduced: %s" % (len(unres), unres)))
    return load(auto=False)


def load(auto=True, log=print):
    """The fixed set as dict(fps (N,768), names, current, cats, kinds, idents, files, harm60, span,
    seam, src_frames, summary). auto=True rebuilds when the npz is missing or stale."""
    P = paths()
    if auto:
        need = not os.path.isfile(P['npz'])
        if not need:
            try:
                with np.load(P['npz']) as z:
                    need = str(z['signature']) != signature()
            except Exception:
                need = True
        if need:
            log("   fixed set: %s is missing or stale — rebuilding" % P['npz'])
            return build(log=log)
    with np.load(P['npz']) as z:
        d = {k: z[k] for k in z.files}
    for k in ("names", "current", "cats", "kinds", "idents", "files"):
        d[k] = [str(x) for x in d[k]]
    d['fps'] = np.asarray(d['fps'], dtype=np.float64)
    # fb638 — THE CLEAN-ROOM GUARD. <wt500>/ref_fp.npz holds FINGERPRINTS ONLY (768 numbers per table, no audio) of the
    # Serum 2 Xfer factory tables and DYNOX PLUTO 2 (built by <wt500>/ref_fp.py). Appended as kind="reference", every
    # distance check (wtkit's nearest-fixed, gate500's acceptance) must clear them too: a new Terrain table that lands
    # within the collision floor of a reference table has no identity of its own, however it was made. Names are NOT
    # checked against them (only "shipped" and built-in names are), so a generic word is not forbidden by a reference.
    rp = os.path.join(P['root'], "ref_fp.npz")
    nr = 0
    if os.path.isfile(rp):
        with np.load(rp) as z:
            rf, rn, rc = np.asarray(z['fps'], dtype=np.float64), [str(x) for x in z['names']], [str(x) for x in z['cats']]
        if len(rf) and rf.shape[1] == d['fps'].shape[1]:
            nr = len(rf)
            d['fps'] = np.vstack([d['fps'], rf])
            d['names'] += rn; d['current'] += rn; d['cats'] += rc; d['kinds'] += ["reference"] * nr
            d['idents'] += [""] * nr; d['files'] += [""] * nr
            for k in ("harm60", "span", "seam", "src_frames"):
                if k in d: d[k] = np.concatenate([np.asarray(d[k], dtype=np.float64), np.zeros(nr)])
    ns, nb = d['kinds'].count("shipped"), d['kinds'].count("builtin")
    d['summary'] = "%d shipped FLACs + %d built-ins + %d reference fingerprints (%s)" % (ns, nb, nr, P['npz'])
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--if-stale", action="store_true")
    a = ap.parse_args()
    d = load(auto=True) if a.if_stale else build()
    print("\n══ FIXED SET ══  %s" % d['summary'])
    bn = builtin_names()
    print("   built-in names (%d) from %s" % (len(bn['names']), bn['source']))
    cf = {}
    for nm, c, k in zip(d['names'], d['cats'], d['kinds']):
        cf.setdefault(nm.casefold(), []).append("%s %s/%s" % (k, c, nm))
    for nm in bn['names']:
        if not any(x.startswith("builtin") for x in cf.get(nm.casefold(), [])):
            cf.setdefault(nm.casefold(), []).append("builtin '%s'" % nm)
    clashes = {k: v for k, v in cf.items() if len(v) > 1}
    print("   plain-name clashes INSIDE the fixed set once 'Terra' is dropped: %s" % (len(clashes) if clashes else "none"))
    for k, v in sorted(clashes.items()):
        print("      %-14s %s" % (k, " | ".join(v)))
    F = d['fps']
    if len(F) > 1:
        D = np.array([np.abs(F - F[i]).mean(axis=1) for i in range(len(F))])
        np.fill_diagonal(D, np.inf)
        i, j = np.unravel_index(np.argmin(D), D.shape)
        print("   closest fixed pair: %.2f dB  %s vs %s" % (D[i, j], d['names'][i], d['names'][j]))
    over = [(float(s), nm) for s, nm, k in zip(d['seam'], d['names'], d['kinds']) if s > SEAM_MAX]
    print("   fixed tables over the new seam bar (%.1f) — reported, never re-gated: %d" % (SEAM_MAX, len(over)))
    for s, nm in sorted(over, reverse=True):
        print("      %6.2f  %s" % (s, nm))
    return 0


if __name__ == "__main__":
    sys.exit(main())
