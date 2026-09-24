"""sf2.py — SoundFont 2.04 (and SF3) → the same flat region dicts sfz.py produces, so torgc.py compiles both.

The hydra walk follows the SF2 2.04 spec (and TinySoundFont's logic, MIT, as the reference):
  preset (phdr/pbag/pgen) zones × instrument (inst/ibag/igen) zones, global zones as defaults,
  key/velocity ranges intersected, preset generators ADDED to instrument generators (spec §9.4).
Samples are cut out of the smpl (+ sm24) chunk into WAV files in a cache folder; linked stereo pairs
(sampleLink, left/right sampleType) become one stereo file. SF3 (Ogg Vorbis samples) is decoded with
libsndfile — lossy at the source, so the provenance should say so.

Generator → SFZ opcode mapping (the subset the design §4.2 lists):
  keyRange lokey/hikey · velRange lovel/hivel · overridingRootKey pitch_keycenter (else the sample's
  originalPitch) · coarseTune transpose · fineTune + pitchCorrection tune · scaleTuning pitch_keytrack ·
  initialAttenuation volume (−cB/10) · pan pan (/5) · sampleModes loop_mode (0 none, 1 continuous,
  3 sustain) · address offsets offset/end/loop_start/loop_end · volEnv* ampeg_* (timecents → s, sustain cB →
  %) · exclusiveClass group + off_by.
"""
from __future__ import annotations

import hashlib
import math
import os
import struct
from typing import Dict, List, Optional, Tuple

import numpy as np
import soundfile as sf

import sfz as sfzmod

GEN = {0: "startAddrsOffset", 1: "endAddrsOffset", 2: "startloopAddrsOffset", 3: "endloopAddrsOffset",
       4: "startAddrsCoarseOffset", 12: "endAddrsCoarseOffset", 17: "pan", 33: "delayVolEnv", 34: "attackVolEnv",
       35: "holdVolEnv", 36: "decayVolEnv", 37: "sustainVolEnv", 38: "releaseVolEnv", 41: "instrument",
       43: "keyRange", 44: "velRange", 45: "startloopAddrsCoarseOffset", 48: "initialAttenuation",
       50: "endloopAddrsCoarseOffset", 51: "coarseTune", 52: "fineTune", 53: "sampleID", 54: "sampleModes",
       56: "scaleTuning", 57: "exclusiveClass", 58: "overridingRootKey"}
RANGE_GENS = (43, 44)
DEFAULTS = {33: -12000, 34: -12000, 35: -12000, 36: -12000, 37: 0, 38: -12000, 56: 100, 58: -1}


def _chunks(data: bytes, off: int, end: int):
    while off + 8 <= end:
        cid = data[off:off + 4]
        size = struct.unpack("<I", data[off + 4:off + 8])[0]
        yield cid, off + 8, size
        off += 8 + size + (size & 1)


class SF2:
    def __init__(self, path: str):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:4] != b"RIFF" or d[8:12] != b"sfbk":
            raise ValueError("not a SoundFont: " + path)
        self.smpl = self.sm24 = None
        pdta = {}
        for cid, o, n in _chunks(d, 12, len(d)):
            if cid == b"LIST":
                kind = d[o:o + 4]
                for sid, so, sn in _chunks(d, o + 4, o + n):
                    if kind == b"sdta":
                        if sid == b"smpl":
                            self.smpl = (so, sn)
                        elif sid == b"sm24":
                            self.sm24 = (so, sn)
                    elif kind == b"pdta":
                        pdta[sid.decode()] = d[so:so + sn]
        self.phdr = [struct.unpack("<20sHHHIII", pdta["phdr"][i:i + 38]) for i in range(0, len(pdta["phdr"]), 38)]
        self.pbag = [struct.unpack("<HH", pdta["pbag"][i:i + 4]) for i in range(0, len(pdta["pbag"]), 4)]
        self.pgen = [struct.unpack("<Hh", pdta["pgen"][i:i + 4]) for i in range(0, len(pdta["pgen"]), 4)]
        self.inst = [struct.unpack("<20sH", pdta["inst"][i:i + 22]) for i in range(0, len(pdta["inst"]), 22)]
        self.ibag = [struct.unpack("<HH", pdta["ibag"][i:i + 4]) for i in range(0, len(pdta["ibag"]), 4)]
        self.igen = [struct.unpack("<Hh", pdta["igen"][i:i + 4]) for i in range(0, len(pdta["igen"]), 4)]
        self.shdr = [struct.unpack("<20sIIIIIBbHH", pdta["shdr"][i:i + 46]) for i in range(0, len(pdta["shdr"]), 46)]

    @staticmethod
    def _name(b: bytes) -> str:
        return b.split(b"\0", 1)[0].decode("latin-1").strip()

    def presets(self) -> List[Tuple[str, int, int]]:
        return [(self._name(p[0]), p[2], p[1]) for p in self.phdr[:-1]]   # (name, bank, program)

    @staticmethod
    def _zone_gens(gens, a: int, b: int) -> Dict[int, object]:
        z = {}
        for op, amt in gens[a:b]:
            if op in RANGE_GENS:
                lo, hi = amt & 0xFF, (amt >> 8) & 0xFF
                z[op] = (lo, hi)
            else:
                z[op] = amt
        return z

    def _zones(self, bags, gens, first: int, last: int, term: int) -> Tuple[Dict, List[Dict]]:
        zones = []
        for bi in range(first, last):
            g0 = bags[bi][0]
            g1 = bags[bi + 1][0]
            zones.append(self._zone_gens(gens, g0, g1))
        glob = {}
        if zones and term not in zones[0]:
            glob = zones.pop(0)
        return glob, [z for z in zones if term in z]

    def sample_data(self, sid: int) -> Tuple[np.ndarray, int, int, int, int, int, int]:
        name, start, end, sl, el, rate, pitch, corr, link, typ = self.shdr[sid]
        if typ & 0x10:        # SF3: compressed (Ogg Vorbis) — start/end are byte offsets of the stream
            import io
            so, _ = self.smpl
            x, _ = sf.read(io.BytesIO(self.data[so + start: so + end]), dtype="float32")
            return x, rate, pitch, corr, sl, el, typ
        so, sn = self.smpl
        raw = np.frombuffer(self.data, dtype="<i2", count=end - start, offset=so + 2 * start).astype(np.int32)
        if self.sm24:
            lo = np.frombuffer(self.data, dtype=np.uint8, count=end - start, offset=self.sm24[0] + start).astype(np.int32)
            x = ((raw << 8) | lo) / 8388608.0
        else:
            x = raw / 32768.0
        return x.astype(np.float32), rate, pitch, corr, sl - start, el - start, typ


def _tc(v: int) -> float:
    return 0.0 if v <= -12000 else 2.0 ** (v / 1200.0)


def parse(path: str, preset=None, cache_dir: Optional[str] = None) -> sfzmod.SfzFile:
    """preset = (bank, program) | name | None (first preset). Returns an SfzFile of flat region dicts."""
    s = SF2(path)
    idx = 0
    if preset is not None:
        for i, (name, bank, prog) in enumerate(s.presets()):
            if (isinstance(preset, str) and name.lower() == preset.lower()) or (
                    isinstance(preset, (list, tuple)) and (bank, prog) == tuple(preset)):
                idx = i
                break
        else:
            raise ValueError(f"preset {preset} not in {path}")
    ph = s.phdr[idx]
    pglob, pzones = s._zones(s.pbag, s.pgen, ph[3], s.phdr[idx + 1][3], 41)
    cache_dir = cache_dir or os.path.join(os.path.dirname(os.path.abspath(path)), ".sf2cache")
    h = hashlib.sha1((os.path.abspath(path) + str(idx)).encode()).hexdigest()[:12]
    cdir = os.path.join(cache_dir, h)
    os.makedirs(cdir, exist_ok=True)
    out = sfzmod.SfzFile(path=os.path.abspath(path))
    out.includes.append(os.path.abspath(path))
    written: Dict[Tuple[int, ...], Tuple[str, int, int]] = {}
    raw_regions = []
    for pz in pzones:
        pz_all = dict(pglob)
        pz_all.update(pz)
        ii = pz_all[41]
        iglob, izones = s._zones(s.ibag, s.igen, s.inst[ii][1], s.inst[ii + 1][1], 53)
        for iz in izones:
            g = dict(DEFAULTS)
            g.update(iglob)
            g.update(iz)
            # preset generators are relative: add (ranges intersect)
            for op, v in pz_all.items():
                if op in (41,):
                    continue
                if op in RANGE_GENS:
                    lo0, hi0 = g.get(op, (0, 127))
                    g[op] = (max(lo0, v[0]), min(hi0, v[1]))
                elif op in (53, 54, 57, 58, 0, 1, 2, 3, 4, 12, 45, 50):
                    continue      # instrument-only generators
                else:
                    g[op] = g.get(op, DEFAULTS.get(op, 0)) + v
            kr, vr = g.get(43, (0, 127)), g.get(44, (0, 127))
            if kr[0] > kr[1] or vr[0] > vr[1]:
                continue
            raw_regions.append(g)
    # merge linked stereo pairs: same ranges, one left + one right sample linked to each other
    used = set()
    regions = []
    for i, g in enumerate(raw_regions):
        if i in used:
            continue
        sid = g[53]
        typ = s.shdr[sid][9]
        link = s.shdr[sid][8]
        pair = None
        if typ & 0x6:
            for j in range(i + 1, len(raw_regions)):
                h2 = raw_regions[j]
                if j not in used and h2[53] == link and h2.get(43) == g.get(43) and h2.get(44) == g.get(44):
                    pair = j
                    break
        if pair is not None:
            used.add(pair)
            left, right = (sid, raw_regions[pair][53]) if typ & 0x4 else (raw_regions[pair][53], sid)
            regions.append((g, (left, right)))
        else:
            regions.append((g, (sid,)))
    for g, sids in regions:
        key = sids
        if key not in written:
            chans = []
            for sid in sids:
                x, rate, pitch, corr, sl, el, typ = s.sample_data(sid)
                chans.append(x)
            n = min(len(c) for c in chans)
            data = np.stack([c[:n] for c in chans], axis=1) if len(chans) > 1 else chans[0][:n]
            fn = os.path.join(cdir, f"s{'_'.join(map(str, sids))}.wav")
            if not os.path.exists(fn):
                sf.write(fn, data, rate, subtype="PCM_24" if s.sm24 else "PCM_16")
            written[key] = (fn, sl, el)
        fn, sl, el = written[key]
        sid = sids[0]
        _, _, _, _, _, rate, pitch, corr, _, _ = s.shdr[sid]
        root = g[58] if g.get(58, -1) >= 0 else pitch
        r = {"sample": os.path.basename(fn), "_sample": fn, "_src": os.path.abspath(path),
             "_default_path": "", "_note_offset": "0", "_octave_offset": "0", "_index": str(len(out.regions)),
             "lokey": str(g.get(43, (0, 127))[0]), "hikey": str(g.get(43, (0, 127))[1]),
             "lovel": str(g.get(44, (0, 127))[0]), "hivel": str(g.get(44, (0, 127))[1]),
             "pitch_keycenter": str(root), "transpose": str(g.get(51, 0)),
             "tune": str(g.get(52, 0) + corr), "volume": f"{-g.get(48, 0) / 10.0:.2f}",
             "pan": f"{max(-500, min(500, g.get(17, 0))) / 5.0:.1f}"}
        if g.get(56, 100) != 100:
            r["pitch_keytrack"] = str(g[56])
        off = g.get(0, 0) + 32768 * g.get(4, 0)
        if off > 0:
            r["offset"] = str(off)
        eoff = g.get(1, 0) + 32768 * g.get(12, 0)
        if eoff < 0:
            r["end"] = str(max(1, sf.info(fn).frames - 1 + eoff))
        mode = g.get(54, 0) & 3
        if mode in (1, 3) and el > sl:
            r["loop_mode"] = "loop_continuous" if mode == 1 else "loop_sustain"
            r["loop_start"] = str(sl + g.get(2, 0) + 32768 * g.get(45, 0))
            r["loop_end"] = str(el - 1 + g.get(3, 0) + 32768 * g.get(50, 0))
        else:
            r["loop_mode"] = "no_loop"
        r["ampeg_delay"] = f"{_tc(g[33]):.4f}"
        r["ampeg_attack"] = f"{_tc(g[34]):.4f}"
        r["ampeg_hold"] = f"{_tc(g[35]):.4f}"
        r["ampeg_decay"] = f"{_tc(g[36]):.4f}"
        r["ampeg_sustain"] = f"{100.0 * 10 ** (-max(0, g[37]) / 200.0):.2f}"
        r["ampeg_release"] = f"{max(0.001, _tc(g[38])):.4f}"
        if g.get(57):
            r["group"] = str(1000 + g[57])
            r["off_by"] = str(1000 + g[57])
        out.regions.append(r)
    return out


def write_sf2(path: str, samples: List[Tuple[str, np.ndarray, int, int, int, int]], zones: List[Dict[int, object]],
              preset_name: str = "Test"):
    """Tiny SF2 writer (mono 16-bit), used by the compile test to round-trip the SF2 path.
    samples: (name, int16 data, rate, root, loopStart, loopEnd); zones: generator dicts (43/44 as (lo,hi))."""
    smpl = b""
    shdr = b""
    pos = 0
    for name, data, rate, root, ls, le in samples:
        d = np.asarray(data, dtype="<i2")
        smpl += d.tobytes() + b"\0" * 92            # 46 zero samples after each sample (spec)
        shdr += struct.pack("<20sIIIIIBbHH", name.encode()[:20], pos, pos + len(d), pos + ls, pos + le, rate, root, 0, 0, 1)
        pos += len(d) + 46
    shdr += struct.pack("<20sIIIIIBbHH", b"EOS", 0, 0, 0, 0, 0, 0, 0, 0, 0)

    def gen(op, v):
        if isinstance(v, tuple):
            return struct.pack("<HBB", op, v[0], v[1])
        return struct.pack("<Hh", op, v)
    igen = b""
    ibag = b""
    n = 0
    for z in zones:
        ibag += struct.pack("<HH", n, 0)
        for op in sorted(z, key=lambda o: (o not in RANGE_GENS, o == 53, o)):
            igen += gen(op, z[op])
            n += 1
    ibag += struct.pack("<HH", n, 0)
    igen += struct.pack("<Hh", 0, 0)
    inst = struct.pack("<20sH", b"TestInst", 0) + struct.pack("<20sH", b"EOI", len(zones))
    pgen = gen(41, 0) + struct.pack("<Hh", 0, 0)
    pbag = struct.pack("<HH", 0, 0) + struct.pack("<HH", 1, 0)
    phdr = struct.pack("<20sHHHIII", preset_name.encode(), 0, 0, 0, 0, 0, 0) + struct.pack("<20sHHHIII", b"EOP", 0, 0, 1, 0, 0, 0)
    pmod = struct.pack("<HHhHH", 0, 0, 0, 0, 0)

    def ck(cid, body):
        return cid + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")
    info = ck(b"ifil", struct.pack("<HH", 2, 4)) + ck(b"INAM", b"test\0\0")
    pdta = (ck(b"phdr", phdr) + ck(b"pbag", pbag) + ck(b"pmod", pmod) + ck(b"pgen", pgen) + ck(b"inst", inst)
            + ck(b"ibag", ibag) + ck(b"imod", pmod) + ck(b"igen", igen) + ck(b"shdr", shdr))
    body = (b"sfbk" + ck(b"LIST", b"INFO" + info) + ck(b"LIST", b"sdta" + ck(b"smpl", smpl)) + ck(b"LIST", b"pdta" + pdta))
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(body)) + body)
