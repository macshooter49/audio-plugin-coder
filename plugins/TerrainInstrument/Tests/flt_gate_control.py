#!/usr/bin/env python3
"""fb604 — the mutation-control bookkeeping for Tests/fb604_filter_gates.sh.

    pick   <bar> <normal.txt> [spine]   -> the type index to inject the fault into
    check  <bar> <normal.txt> <mut.txt> <victim> -> "OK <name>" or "BROKEN <why>"

WHY IT IS A FILE. This was three python -c one-liners embedded in the shell, quoting an
OFFENDERS: line through two layers of shell quoting; it emitted a stray `e: command not found`
and, worse, picked victims from bars that SKIP them. The rule it enforces:

  a mutation control is only meaningful if the injected type was CLEAN for that bar before —
  neither an existing offender nor a type the bar skips by design.
"""
import re, sys


def flags(path_or_text, tag, is_text=False):
    txt = path_or_text if is_text else open(path_or_text, errors="replace").read()
    m = re.search(r"^%s:(.*)$" % tag, txt, re.M)
    out = {}
    for idx, fl in re.findall(r"(\d+):([a-z,]+)", m.group(1) if m else ""):
        out[int(idx)] = set(f for f in fl.split(",") if f)
    return out


def main():
    cmd = sys.argv[1]
    bar = sys.argv[2]
    normal = sys.argv[3]
    off = flags(normal, "OFFENDERS")
    skip = flags(normal, "SKIPPED")

    if cmd == "pick":
        # fb604 — the pool was `range(94)`, a typed roster literal inside the mutation control
        # itself: at 118 it would have silently stopped ever nominating a victim from the
        # appended half, so every new type's bars would have gone un-controlled while the matrix
        # still printed OK. Both the spine and the roster size now come from the gate's own
        # machine lines (`SPINE: ...` / `ROSTER n`), which is the only copy of either.
        spine = sys.argv[4].split() if len(sys.argv) > 4 and sys.argv[4].strip() else None
        if spine is None:
            spine = re.findall(r"\d+", re.search(r"^SPINE:(.*)$", open(normal, errors="replace").read(), re.M).group(1)) \
                    if re.search(r"^SPINE:", open(normal, errors="replace").read(), re.M) else None
        if spine:
            pool = [int(x) for x in spine]
        else:
            m = re.search(r"^ROSTER (\d+)", open(normal, errors="replace").read(), re.M)
            pool = list(range(int(m.group(1)) if m else 94))
        # fb604 — START THE SCAN AT A BAR-DEPENDENT OFFSET. Scanning from 0 every time nominated
        # index 0 for nine of the ten bars, so the whole mutation matrix only ever proved the bars
        # could flag Ladder LP 24 — and at 118 types it would never once have exercised the 24
        # appended ones. The offset is a hash of the bar name, so it is deterministic (the same
        # bar picks the same victim on every run, which is what makes two runs comparable) and
        # spread (different bars land in different parts of the roster).
        n = len(pool)
        start = (sum(ord(c) * (i + 7) for i, c in enumerate(bar)) % n) if n else 0
        for k in range(n):
            i = pool[(start + k) % n]
            if bar in off.get(i, ()) or bar in skip.get(i, ()):
                continue
            print(i)
            return 0
        print("-1")
        return 1

    if cmd == "check":
        mut_txt, victim = sys.argv[4], int(sys.argv[5])
        after = flags(mut_txt, "OFFENDERS")
        was = bar in off.get(victim, ())
        now = bar in after.get(victim, ())
        if was:
            print("BROKEN the victim was ALREADY an offender for this bar — no fault was injected")
        elif not now:
            print("BROKEN the injected fault did not make the bar name the victim")
        else:
            print("OK")
        return 0

    print("usage: pick|check", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
