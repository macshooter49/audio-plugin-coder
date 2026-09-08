#!/usr/bin/env python3
"""fb603 — the mutation-control bookkeeping for Tests/fb603_filter_gates.sh.

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
        spine = sys.argv[4].split() if len(sys.argv) > 4 and sys.argv[4].strip() else None
        pool = [int(x) for x in spine] if spine else list(range(94))
        for i in pool:
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
