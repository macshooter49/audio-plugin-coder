#!/usr/bin/env python3
"""organics_import_fixtures.py — tp108: the fixtures for Tests/organics_import_test.sh, written into <dir>.

    python3 Tests/organics_import_fixtures.py <dir>

<dir>/library/                  a temp library root: the frozen fixture's index.json / ids.json / test.sine (factory side)
<dir>/Sons déjà ✓/              the user's files (a unicode folder name on purpose):
    sfz-src/                    a copy of Tests/fixtures/organics/sfz-src (keyswitch artics, 2 velocity layers with an authored
                                loop, a sequential round robin with ±10 ¢ tune, a release trigger, #define/#include/
                                default_path/note names — sines at C4)
    t.sf2                       the compiler test's SoundFont: zone A (48-59, root 55, fine −7) + zone B (60-72, root 64,
                                looped, 6 dB attenuation)
    multi.sf2                   3 presets: "Soft Piano" (GM 0) · "Bright Strings" (GM 48, a linked stereo pair) · "Choir Pad" (GM 52)
    t.sf3                       zone B again, its sample Ogg Vorbis-compressed (SF3), when soundfile can write OGG
    garbage.sf2 · truncated.sf2 · huge.sf2 (a sample header claiming 300 M frames) · noregions.sfz · missing.sfz ·
    loop.sfz (includes itself) · undecodable.sfz (+ junk.wav) · bad.wav
<dir>/ref/                      the OFFLINE compiler's output (Tools/organics/torgc.py) for sfz-src and t.sf2 — the reference
                                Tests/organics_import_compare.py compares the C++ map.json against
"""
import io
import json
import os
import shutil
import struct
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
PLUG = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(PLUG, "Tools", "organics"))
import sf2 as sf2mod   # noqa: E402
import torgc           # noqa: E402

UNI = "Sons déjà ✓"
SR = 44100


def ck(cid, body):
    return cid + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def gen(op, v):
    if isinstance(v, tuple):
        return struct.pack("<HBB", op, v[0], v[1])
    return struct.pack("<Hh", op, v) if op not in (41, 53) else struct.pack("<HH", op, v)


def write_sf2(path, samples, instruments, presets, sf3=False):
    """samples: [{name, data(int16), rate, root, ls, le, type, link}] · instruments: [(name, [zone gen dicts])] ·
    presets: [(name, bank, program, instrument index)]. sf3=True stores each sample as Ogg Vorbis (start/end = byte
    offsets into smpl, loop points relative to the decoded sample — the SF3 convention)."""
    smpl = b""
    shdr = b""
    pos = 0
    for s in samples:
        d = np.asarray(s["data"], dtype="<i2")
        typ = s.get("type", 1)
        if sf3:
            bio = io.BytesIO()
            sf.write(bio, d.astype(np.float32) / 32768.0, s["rate"], format="OGG", subtype="VORBIS")
            b = bio.getvalue()
            shdr += struct.pack("<20sIIIIIBbHH", s["name"].encode()[:20], len(smpl), len(smpl) + len(b), s["ls"], s["le"],
                                s["rate"], s["root"], 0, s.get("link", 0), typ | 0x10)
            smpl += b
            continue
        smpl += d.tobytes() + b"\0" * 92
        shdr += struct.pack("<20sIIIIIBbHH", s["name"].encode()[:20], pos, pos + len(d), pos + s["ls"], pos + s["le"],
                            s["rate"], s["root"], 0, s.get("link", 0), typ)
        pos += len(d) + 46
    shdr += struct.pack("<20sIIIIIBbHH", b"EOS", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    igen = b""
    ibag = b""
    inst = b""
    n = 0
    nb = 0
    for name, zones in instruments:
        inst += struct.pack("<20sH", name.encode()[:20], nb)
        for z in zones:
            ibag += struct.pack("<HH", n, 0)
            nb += 1
            for op in sorted(z, key=lambda o: (o not in (43, 44), o == 53, o)):
                igen += gen(op, z[op])
                n += 1
    ibag += struct.pack("<HH", n, 0)
    igen += struct.pack("<Hh", 0, 0)
    inst += struct.pack("<20sH", b"EOI", nb)
    pgen = b""
    pbag = b""
    phdr = b""
    for i, (name, bank, prog, ii) in enumerate(presets):
        phdr += struct.pack("<20sHHHIII", name.encode()[:20], prog, bank, i, 0, 0, 0)
        pbag += struct.pack("<HH", i, 0)
        pgen += gen(41, ii)
    pbag += struct.pack("<HH", len(presets), 0)
    pgen += struct.pack("<Hh", 0, 0)
    phdr += struct.pack("<20sHHHIII", b"EOP", 0, 0, len(presets), 0, 0, 0)
    pmod = struct.pack("<HHhHH", 0, 0, 0, 0, 0)
    info = ck(b"ifil", struct.pack("<HH", 3 if sf3 else 2, 1 if sf3 else 4)) + ck(b"INAM", b"test\0\0")
    pdta = (ck(b"phdr", phdr) + ck(b"pbag", pbag) + ck(b"pmod", pmod) + ck(b"pgen", pgen) + ck(b"inst", inst)
            + ck(b"ibag", ibag) + ck(b"imod", pmod) + ck(b"igen", igen) + ck(b"shdr", shdr))
    body = b"sfbk" + ck(b"LIST", b"INFO" + info) + ck(b"LIST", b"sdta" + ck(b"smpl", smpl)) + ck(b"LIST", b"pdta" + pdta)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(body)) + body)


def sine(hz, sec, amp=0.5, decay=0.0):
    t = np.arange(int(sec * SR)) / SR
    return (amp * np.sin(2 * np.pi * hz * t) * np.exp(-t * decay) * 32767).astype(np.int16)


def main():
    out = sys.argv[1]
    lib = os.path.join(out, "library")
    uni = os.path.join(out, UNI)
    ref = os.path.join(out, "ref")
    fix = os.path.join(HERE, "fixtures", "organics")
    os.makedirs(lib, exist_ok=True)
    os.makedirs(uni, exist_ok=True)
    os.makedirs(ref, exist_ok=True)
    for n in ("index.json", "ids.json"):
        shutil.copy2(os.path.join(fix, n), os.path.join(lib, n))
    shutil.copytree(os.path.join(fix, "test.sine"), os.path.join(lib, "test.sine"))
    shutil.copytree(os.path.join(fix, "sfz-src"), os.path.join(uni, "sfz-src"))

    # ── t.sf2: exactly the compiler test's generated SoundFont (sf2_round_trip)
    t = np.arange(int(0.8 * SR)) / SR
    a = (0.5 * np.sin(2 * np.pi * 196.0 * t) * np.exp(-t * 3) * 32767).astype(np.int16)
    period = SR / 329.6275569
    ls, le = int(0.2 * SR), int(0.2 * SR) + int(round(100 * period))
    b = (0.5 * np.sin(2 * np.pi * 329.6275569 * t) * 32767).astype(np.int16)
    sf2mod.write_sf2(os.path.join(uni, "t.sf2"), [("A", a, SR, 55, 0, 0), ("B", b, SR, 64, ls, le)],
                     [{43: (48, 59), 44: (1, 127), 52: -7, 53: 0},
                      {43: (60, 72), 44: (1, 127), 48: 60, 54: 1, 53: 1}])

    # ── multi.sf2: three presets, one with a linked stereo pair
    p = sine(261.6255653, 0.6, decay=2.0)
    sl = sine(440.0, 0.8, 0.4)
    sr_ = sine(440.0, 0.8, 0.3)
    ch = sine(523.2511306, 0.8, 0.4)
    write_sf2(os.path.join(uni, "multi.sf2"),
              [{"name": "Pno", "data": p, "rate": SR, "root": 60, "ls": 0, "le": 0},
               {"name": "StrL", "data": sl, "rate": SR, "root": 69, "ls": 0, "le": 0, "type": 4, "link": 2},
               {"name": "StrR", "data": sr_, "rate": SR, "root": 69, "ls": 0, "le": 0, "type": 2, "link": 1},
               {"name": "Chr", "data": ch, "rate": SR, "root": 72, "ls": 0, "le": 0}],
              [("Piano", [{43: (0, 127), 44: (0, 127), 53: 0}]),
               ("Strings", [{43: (0, 127), 44: (0, 127), 17: -500, 53: 1}, {43: (0, 127), 44: (0, 127), 17: 500, 53: 2}]),
               ("Choir", [{43: (0, 127), 44: (0, 127), 53: 3}])],
              [("Soft Piano", 0, 0, 0), ("Bright Strings", 0, 48, 1), ("Choir Pad", 0, 52, 2)])

    # ── t.sf3: zone B, Ogg Vorbis
    try:
        write_sf2(os.path.join(uni, "t.sf3"), [{"name": "B", "data": b, "rate": SR, "root": 64, "ls": ls, "le": le}],
                  [("B", [{43: (0, 127), 44: (1, 127), 54: 1, 53: 0}])], [("Ogg Tone", 0, 0, 0)], sf3=True)
    except Exception as e:  # no Ogg writer in this libsndfile: the C++ bar reports a skip
        print("  (no SF3 fixture:", e, ")")
        if os.path.exists(os.path.join(uni, "t.sf3")):
            os.remove(os.path.join(uni, "t.sf3"))

    # ── the bad files
    with open(os.path.join(uni, "garbage.sf2"), "wb") as f:
        f.write(np.random.default_rng(7).integers(0, 255, 5000, dtype=np.uint8).tobytes())
    raw = open(os.path.join(uni, "t.sf2"), "rb").read()
    with open(os.path.join(uni, "truncated.sf2"), "wb") as f:
        f.write(raw[: int(len(raw) * 0.6)])
    write_sf2(os.path.join(uni, "huge.sf2"), [{"name": "Big", "data": sine(440, 0.1), "rate": SR, "root": 69, "ls": 0, "le": 0}],
              [("Big", [{43: (0, 127), 44: (0, 127), 53: 0}])], [("Big", 0, 0, 0)])
    hb = bytearray(open(os.path.join(uni, "huge.sf2"), "rb").read())
    i = hb.find(b"shdr") + 8                      # the first sample header: end = 300 M frames (the smpl chunk is 0.1 s)
    struct.pack_into("<I", hb, i + 24, 300_000_000)
    open(os.path.join(uni, "huge.sf2"), "wb").write(bytes(hb))
    open(os.path.join(uni, "noregions.sfz"), "w").write("// no regions here\n<control> default_path=nowhere/\n<global> ampeg_release=1\n")
    open(os.path.join(uni, "missing.sfz"), "w").write("<region> sample=gone/a.wav key=60\n<region> sample=gone/b.wav key=62\n")
    open(os.path.join(uni, "loop.sfz"), "w").write('#include "loop.sfz"\n<region> sample=x.wav key=60\n')
    open(os.path.join(uni, "junk.wav"), "wb").write(b"RIFF\x10\x00\x00\x00WAVEjunkjunkjunk")
    open(os.path.join(uni, "undecodable.sfz"), "w").write("<region> sample=junk.wav key=60\n")
    sf.write(os.path.join(uni, "bad.wav"), sine(440, 0.1).astype(np.float32) / 32768.0, SR)

    # ── the offline compiler's reference maps (recipes that say what the in-plugin import infers by itself: the
    #    file's keyswitches with their sw_label names, no budget, no loop polish — the import keeps authored loops)
    src = os.path.join(uni, "sfz-src")
    json.dump({"id": "ref.sfz", "name": "fixture", "family": "grand", "category": "Keys", "tags": ["test"],
               "licence": "CC0-1.0", "credit": "fixture.sfz", "author": "Terrain", "url": "generated", "licenceDir": ".",
               "loopPolish": False,
               "artics": [{"name": "Sustain", "sfz": "fixture.sfz", "sw": "c1"},
                          {"name": "Staccato", "sfz": "fixture.sfz", "sw": "c#1"}]},
              open(os.path.join(src, "ref-recipe.json"), "w"))
    so, se = sys.stdout, sys.stderr
    sys.stdout = sys.stderr = open(os.path.join(out, "torgc.log"), "w")
    try:
        torgc.Compiler(os.path.join(src, "ref-recipe.json"), src, ref, 2).run()
        open(os.path.join(uni, "LICENSE"), "w").write("generated test data, CC0\n")
        json.dump({"id": "ref.sf2", "name": "t", "family": "grand", "category": "Keys", "tags": ["test"],
                   "licence": "CC0-1.0", "credit": "t.sf2", "author": "Terrain", "url": "generated", "licenceDir": ".",
                   "loopPolish": False, "artics": [{"name": "Default", "sfz": "t.sf2"}]},
                  open(os.path.join(uni, "ref-recipe.json"), "w"))
        torgc.Compiler(os.path.join(uni, "ref-recipe.json"), uni, ref, 2).run()
    finally:
        sys.stdout.close()
        sys.stdout, sys.stderr = so, se
    os.remove(os.path.join(src, "ref-recipe.json"))            # the import copies sfz-src's mapping files: keep it the user's
    shutil.rmtree(os.path.join(ref, ".sf2cache"), ignore_errors=True)
    print(f"  fixtures in {out}")
    print(f"  torgc reference maps: {sorted(os.listdir(ref))}")


if __name__ == "__main__":
    main()
