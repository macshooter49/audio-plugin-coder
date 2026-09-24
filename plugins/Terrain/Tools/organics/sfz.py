"""sfz.py — SFZ v1/v2 (+ the ARIA structure extensions) → a flat list of regions.

Part of the Organics compiler (torgc.py). Offline only; never shipped.

Handles the structure the cleared libraries actually use (design §4.2):
  * headers  <control> <global> <master> <group> <region> <curve> (+ <effect>, ignored)
  * inheritance  global → master → group → region; a new <global> resets master+group,
    a new <master> resets group (SFZ v2 / ARIA semantics)
  * #define $VAR value  (sequential: a define takes effect for the text read after it,
    so Salamander's per-layer includes that redefine $VEL / $OFFnn work)
  * #include "file"  anywhere on a line (paths relative to the ROOT .sfz folder, like sfizz/ARIA)
  * default_path (per <control>, backslashes allowed), note_offset, octave_offset
  * note names (c4 = 60, c#4 / db4, lowercase or uppercase)
  * set_ccN / set_hdccN / label_ccN defaults
  * // and /* */ comments

Every region comes out as a dict of opcode -> raw string (the compiler interprets the values),
plus private keys:
  _sample   resolved absolute sample path (or None)
  _src      the file the <region> header was read from
  _index    region order
"""
from __future__ import annotations

import os
import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional

NOTE_BASE = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
_NOTE_RE = re.compile(r"^([a-gA-G])([#b♯♭]?)(-?\d+)$")
_OPCODE_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_$]*)=")
_HEADER_RE = re.compile(r"<(\w+)>")
_INCLUDE_RE = re.compile(r'#include\s+"([^"]+)"')
_DEFINE_RE = re.compile(r"^\s*#define\s+(\$[A-Za-z0-9_]+)\s+(.*?)\s*$")
_VAR_RE = re.compile(r"\$[A-Za-z0-9_]+")

# CC defaults when a file does not set them (sfizz / ARIA conventions)
DEFAULT_CC = {7: 100.0, 10: 64.0, 11: 127.0}


def note_to_midi(v: str, note_offset: int = 0, octave_offset: int = 0) -> Optional[int]:
    """'60' / 'c4' / 'C#4' / 'db4' → MIDI number (C4 = 60, the SFZ convention)."""
    if v is None:
        return None
    s = str(v).strip()
    if s == "":
        return None
    try:
        n = int(float(s))
        return n + note_offset + 12 * octave_offset
    except ValueError:
        pass
    m = _NOTE_RE.match(s)
    if not m:
        return None
    base = NOTE_BASE[m.group(1).lower()]
    acc = m.group(2)
    if acc in ("#", "♯"):
        base += 1
    elif acc in ("b", "♭"):
        base -= 1
    octave = int(m.group(3))
    return (octave + 1) * 12 + base + note_offset + 12 * octave_offset


@dataclass
class SfzFile:
    path: str
    regions: List[Dict[str, str]] = field(default_factory=list)
    control: Dict[str, str] = field(default_factory=dict)
    curves: Dict[int, List[float]] = field(default_factory=dict)
    cc: Dict[int, float] = field(default_factory=dict)          # default CC values, 0..127
    labels: Dict[int, str] = field(default_factory=dict)
    includes: List[str] = field(default_factory=list)            # every file read (for source/)
    dropped: Dict[str, int] = field(default_factory=dict)        # diagnostic counters

    def cc_value(self, n: int) -> float:
        if n in self.cc:
            return self.cc[n]
        return DEFAULT_CC.get(n, 0.0)


class SfzParser:
    def __init__(self, path: str, defines: Optional[Dict[str, str]] = None, default_path: str = ""):
        self.root_path = os.path.abspath(path)
        self.root_dir = os.path.dirname(self.root_path)
        self.defines: Dict[str, str] = dict(defines or {})
        self.out = SfzFile(path=self.root_path)
        # hierarchy state
        self.level = None           # current header name
        self.control: Dict[str, str] = {}
        self.glob: Dict[str, str] = {}
        self.master: Dict[str, str] = {}
        self.group: Dict[str, str] = {}
        self.region: Optional[Dict[str, str]] = None
        self.curve: Optional[Dict[str, str]] = None
        self.default_path = default_path.replace("\\", "/")   # a program-level default (bank XML) until <control> sets one
        self.note_offset = 0
        self.octave_offset = 0
        self.cur_file = self.root_path
        self._depth = 0

    # ---------------------------------------------------------------- text handling
    @staticmethod
    def _read(path: str) -> str:
        with open(path, "rb") as f:
            raw = f.read()
        for enc in ("utf-8-sig", "latin-1"):
            try:
                return raw.decode(enc)
            except UnicodeDecodeError:
                continue
        return raw.decode("latin-1", "replace")

    @staticmethod
    def _strip_comments(text: str) -> str:
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        out = []
        for line in text.splitlines():
            i = line.find("//")
            if i >= 0:
                line = line[:i]
            out.append(line)
        return "\n".join(out)

    def _subst(self, s: str) -> str:
        if "$" not in s or not self.defines:
            return s

        def rep(m):
            name = m.group(0)
            if name in self.defines:
                return self.defines[name]
            # longest defined prefix (sfizz behaviour for "$VAR_suffix"-style glue)
            for k in sorted(self.defines, key=len, reverse=True):
                if name.startswith(k):
                    return self.defines[k] + name[len(k):]
            return name

        return _VAR_RE.sub(rep, s)

    # ---------------------------------------------------------------- parse
    def parse(self) -> SfzFile:
        self._parse_file(self.root_path)
        self._close_region()
        self._close_curve()
        self.out.control = dict(self.control)
        return self.out

    def _parse_file(self, path: str):
        self._depth += 1
        if self._depth > 32:
            raise RuntimeError("#include nesting too deep at " + path)
        prev = self.cur_file
        self.cur_file = path
        self.out.includes.append(path)
        text = self._strip_comments(self._read(path))
        for line in text.splitlines():
            self._parse_line(line)
        self.cur_file = prev
        self._depth -= 1

    def _parse_line(self, line: str):
        if not line.strip():
            return
        m = _DEFINE_RE.match(line)
        if m:
            self.defines[m.group(1)] = self._subst(m.group(2))
            return
        # split on #include directives, processing segments in order (defines take effect lazily)
        pos = 0
        for im in _INCLUDE_RE.finditer(line):
            self._parse_segment(line[pos:im.start()])
            inc = self._subst(im.group(1)).replace("\\", "/")
            inc_path = os.path.normpath(os.path.join(self.root_dir, inc))
            if not os.path.exists(inc_path):
                alt = os.path.normpath(os.path.join(os.path.dirname(self.cur_file), inc))
                inc_path = alt if os.path.exists(alt) else inc_path
            if os.path.exists(inc_path):
                self._parse_file(inc_path)
            else:
                self.out.dropped["missing_include"] = self.out.dropped.get("missing_include", 0) + 1
            pos = im.end()
        self._parse_segment(line[pos:])

    def _parse_segment(self, seg: str):
        seg = self._subst(seg)
        if not seg.strip():
            return
        # tokens: headers and opcodes, in order
        tokens = []
        for hm in _HEADER_RE.finditer(seg):
            tokens.append((hm.start(), hm.end(), "H", hm.group(1)))
        for om in _OPCODE_RE.finditer(seg):
            # ignore matches inside a header token
            if any(t[0] <= om.start() < t[1] for t in tokens if t[2] == "H"):
                continue
            tokens.append((om.start(), om.end(), "O", om.group(1)))
        tokens.sort()
        for i, (s, e, kind, name) in enumerate(tokens):
            if kind == "H":
                self._header(name.lower())
            else:
                nxt = tokens[i + 1][0] if i + 1 < len(tokens) else len(seg)
                value = seg[e:nxt].strip()
                self._opcode(name, value)

    # ---------------------------------------------------------------- hierarchy
    def _close_region(self):
        if self.region is not None:
            r = {}
            r.update(self.glob)
            r.update(self.master)
            r.update(self.group)
            r.update(self.region)
            r["_default_path"] = self.default_path
            r["_note_offset"] = str(self.note_offset)
            r["_octave_offset"] = str(self.octave_offset)
            r["_src"] = self.region.get("_src", self.cur_file)
            r["_index"] = str(len(self.out.regions))
            smp = r.get("sample")
            r["_sample"] = self._resolve_sample(smp, r["_default_path"]) if smp else None
            self.out.regions.append(r)
        self.region = None

    def _close_curve(self):
        if self.curve is not None:
            try:
                idx = int(float(self.curve.get("curve_index", "0")))
            except ValueError:
                idx = 0
            pts = {}
            for k, v in self.curve.items():
                if re.match(r"^v\d{3}$", k):
                    try:
                        pts[int(k[1:])] = float(v)
                    except ValueError:
                        pass
            if pts:
                self.out.curves[idx] = _interp_points(pts)
        self.curve = None

    def _header(self, h: str):
        self._close_region()
        self._close_curve()
        self.level = h
        if h == "control":
            pass
        elif h == "global":
            self.glob, self.master, self.group = {}, {}, {}
        elif h == "master":
            self.master, self.group = {}, {}
        elif h == "group":
            self.group = {}
        elif h == "region":
            self.region = {"_src": self.cur_file}
        elif h == "curve":
            self.curve = {}
        else:  # <effect>, <midi>, <sample> …: swallow opcodes
            pass

    def _opcode(self, name: str, value: str):
        lv = self.level
        if lv == "control":
            self.control[name] = value
            if name == "default_path":
                self.default_path = value.replace("\\", "/")
            elif name == "note_offset":
                self.note_offset = int(float(value))
            elif name == "octave_offset":
                self.octave_offset = int(float(value))
            elif name.startswith("set_hdcc"):
                n = _cc_num(name[len("set_hdcc"):])
                if n is not None:
                    self.out.cc[n] = float(value) * 127.0
            elif name.startswith("set_cc"):
                n = _cc_num(name[len("set_cc"):])
                if n is not None:
                    self.out.cc[n] = float(value)
            elif name.startswith("label_cc"):
                n = _cc_num(name[len("label_cc"):])
                if n is not None:
                    self.out.labels[n] = value
            return
        if lv == "curve":
            self.curve[name] = value
            return
        if lv == "global":
            self.glob[name] = value
        elif lv == "master":
            self.master[name] = value
        elif lv == "group":
            self.group[name] = value
        elif lv == "region" and self.region is not None:
            self.region[name] = value
        else:
            # opcodes before any header: treat as global (lenient, like sfizz)
            if lv is None:
                self.glob[name] = value

    def _resolve_sample(self, smp: str, default_path: str) -> str:
        s = smp.strip().replace("\\", "/")
        if s.startswith("*"):
            return s  # generator (*sine, *noise …)
        cand = os.path.normpath(os.path.join(self.root_dir, default_path, s))
        if os.path.exists(cand):
            return cand
        # case-insensitive fallback (libraries authored on Windows)
        fixed = _ci_path(os.path.join(self.root_dir, default_path), s)
        return fixed or cand


def _cc_num(s: str) -> Optional[int]:
    try:
        return int(s)
    except ValueError:
        return None


def _interp_points(pts: Dict[int, float]) -> List[float]:
    """Linear interpolation of sparse <curve> / amp_velcurve points over 0..127."""
    if 0 not in pts:
        pts[0] = 0.0
    if 127 not in pts:
        pts[127] = 1.0
    ks = sorted(pts)
    out = []
    j = 0
    for v in range(128):
        while j + 1 < len(ks) and ks[j + 1] < v:
            j += 1
        a, b = ks[j], ks[min(j + 1, len(ks) - 1)]
        if v <= a:
            out.append(pts[a])
        elif b == a:
            out.append(pts[a])
        else:
            t = (v - a) / (b - a)
            out.append(pts[a] + t * (pts[b] - pts[a]))
    return out


_ci_cache: Dict[str, Dict[str, str]] = {}


def _ci_path(base: str, rel: str) -> Optional[str]:
    cur = os.path.normpath(base)
    for part in rel.split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            cur = os.path.dirname(cur)
            continue
        if cur not in _ci_cache:
            try:
                _ci_cache[cur] = {n.lower(): n for n in os.listdir(cur)}
            except OSError:
                return None
        real = _ci_cache[cur].get(part.lower())
        if real is None:
            return None
        cur = os.path.join(cur, real)
    return cur if os.path.exists(cur) else None


def parse(path: str, defines: Optional[Dict[str, str]] = None, default_path: str = "") -> SfzFile:
    return SfzParser(path, defines, default_path).parse()


if __name__ == "__main__":
    import sys
    f = parse(sys.argv[1])
    print(f"{len(f.regions)} regions, cc defaults {f.cc}, curves {list(f.curves)}")
    for r in f.regions[:5]:
        print({k: v for k, v in r.items() if not k.startswith('_')}, r["_sample"])
