#!/usr/bin/env python3
# fb602 CERT — duplicate native-function names inside ONE WebBrowserComponent::Options chain.
# JUCE: Options::withNativeFunction does `copy.nativeFunctions[name] = std::move(callback);`
# (juce_WebBrowserComponent.h:324) behind `jassert (copy.nativeFunctions.count(name) == 0);` (:323).
# In a Release build the assert is compiled out and the LAST registration silently wins.
#
# SECTION B — THE OTHER HALF OF THE SAME BUG. Removing the collision inside one chain does not
# make the NAME mean one thing: "deletePreset" is still the PATCH deleter (an int index) in the
# main chain and the CARD deleter (a string card id) in the popped-card chain, and index.html has
# exactly one getNativeFunction('deletePreset') for both meanings. Section B groups every
# registration by name ACROSS chains and compares how each handler reads args[0]. An int-first
# handler and a string-first handler under one name is a MEANING SPLIT — the shipped bug, minus
# the duplicate. Known splits live in KNOWN_SPLITS with an owner; a NEW one is RED, and
# TI_SPLIT_STRICT=1 turns the allowlist off so the outstanding debt can be made to go red on demand.
#
# MUTATION CONTROL: TI_CERT_MUTATE=1 injects a synthetic duplicate into the parsed token stream
# (not the file) so the RED path is proven reachable. The count MUST move.
# NOT a duplicate of Tests/card_natives_gate.py (fb570), which asks the opposite question: whether
# a native the popped card window CALLS is registered in that window's chain at all. This one asks
# whether a name is registered TWICE in one chain (Section A) or means two different things across
# chains (Section B). Neither can see the other's failure.
import os, re, sys

PATH = sys.argv[1] if len(sys.argv) > 1 else \
    '/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/Terrain/Source/PluginEditor.cpp'
MUTATE = os.environ.get('TI_CERT_MUTATE') == '1'
STRICT = os.environ.get('TI_SPLIT_STRICT') == '1'

# name -> why it is allowed to mean two things TODAY, and who owns closing it.
# NOT a suppression list: an entry here still PRINTS, and a split that is not here is RED.
#
# It is EMPTY on purpose, and the empty is the fb602 story. The first draft of this gate carried a
# deletePreset entry saying the docked card X was still a silent no-op and naming a future commit
# as its owner -- i.e. a declared debt, a red bar with a note on it. It did not have to be: the two
# calls are unambiguous BY SHAPE (the card call is (String, String), the patch call is (Number)),
# so PluginEditor.cpp:1766 now dispatches on args[0].isString() and index.html needs no edit at
# all. A dispatcher is not a meaning split, it is the cure for one, and arg0_kind() below reports
# it as 'dispatch' rather than as one of the two meanings it routes between.
KNOWN_SPLITS = {}

src = open(PATH, encoding='utf-8').read().split('\n')

def close_paren(startline):
    """Return the 0-based line index where the paren opened on startline balances to 0."""
    depth = 0; started = False; instr = incomment = inchar = False
    i = startline
    while i < len(src):
        line = src[i]; j = 0
        while j < len(line):
            c = line[j]
            if incomment:
                if c == '*' and line[j+1:j+2] == '/': incomment = False; j += 2; continue
                j += 1; continue
            if instr:
                if c == '\\': j += 2; continue
                if c == '"': instr = False
                j += 1; continue
            if inchar:
                if c == '\\': j += 2; continue
                if c == "'": inchar = False
                j += 1; continue
            if c == '/' and line[j+1:j+2] == '/': break
            if c == '/' and line[j+1:j+2] == '*': incomment = True; j += 2; continue
            if c == '"': instr = True; j += 1; continue
            if c == "'": inchar = True; j += 1; continue
            if c == '(': depth += 1; started = True
            elif c == ')':
                depth -= 1
                if started and depth == 0: return i
            j += 1
        i += 1
    return len(src) - 1

# A chain begins at the `Options()` literal; the enclosing call paren is on that line or the one above.
chain_starts = [i for i, l in enumerate(src) if 'WebBrowserComponent::Options()' in l]
REG = re.compile(r'\.\s*(withNativeFunction|withEventListener)\s*\(\s*"([^"]+)"')

print(f'cert_native_dupes: {PATH}')
print(f'MUTATION CONTROL TI_CERT_MUTATE={"1 (ACTIVE - expect RED)" if MUTATE else "0 (inactive)"}')
print(f'chains found: {len(chain_starts)} at lines {[i+1 for i in chain_starts]}')

total_dupes = 0
for cs in chain_starts:
    # walk up at most 2 lines to find the enclosing open paren of the statement
    anchor = cs
    for back in (0, 1, 2):
        cand = cs - back
        if cand < 0: break
        if '(' in src[cand]:
            anchor = cand
            if close_paren(anchor) > cs: break
    ce = close_paren(anchor)
    names = []
    for ln in range(anchor, ce + 1):
        for kind, nm in REG.findall(src[ln]):
            names.append((nm, ln + 1, kind))
    if MUTATE and names:
        names.append((names[0][0], names[0][1], names[0][2]))  # synthetic duplicate
    seen = {}
    dupes = {}
    for nm, ln, kind in names:
        seen.setdefault(nm, []).append((ln, kind))
    for nm, hits in seen.items():
        if len(hits) > 1: dupes[nm] = hits
    print(f'\n  CHAIN lines {anchor+1}..{ce+1}: {len(names)} registrations, {len(seen)} distinct')
    for nm, hits in sorted(dupes.items(), key=lambda kv: kv[1][0][0]):
        total_dupes += 1
        locs = ', '.join(f'{k}@{l}' for l, k in hits)
        print(f'    DUPLICATE "{nm}" x{len(hits)}: {locs}   <-- LAST WINS, earlier ones dead')

print(f'\nSECTION A — TOTAL COLLIDING NAMES WITHIN A CHAIN: {total_dupes}')

# ══ SECTION B — one name, two meanings, across chains ══════════════════════════════════════════
def body_after(anchor_line, anchor_col):
    """Text of the balanced ( ... ) whose '(' is the first one at/after anchor_col.
    String- and comment-aware, because a ')' inside a string literal or a // comment inside one of
    the 260 lambdas would otherwise close the call early and hand the wrong body to arg0_kind."""
    depth = 0; started = False; out = []
    i, j = anchor_line, anchor_col
    instr = inchar = incomment = False
    while i < len(src):
        line = src[i]; startj = j
        while j < len(line):
            c = line[j]
            if incomment:
                if c == '*' and line[j+1:j+2] == '/': incomment = False; j += 2; continue
                j += 1; continue
            if instr:
                if c == '\\': j += 2; continue
                if c == '"': instr = False
                j += 1; continue
            if inchar:
                if c == '\\': j += 2; continue
                if c == "'": inchar = False
                j += 1; continue
            if c == '/' and line[j+1:j+2] == '/': break
            if c == '/' and line[j+1:j+2] == '*': incomment = True; j += 2; continue
            if c == '"': instr = True; j += 1; continue
            if c == "'": inchar = True; j += 1; continue
            if c == '(': depth += 1; started = True
            elif c == ')':
                depth -= 1
                if started and depth == 0: out.append(line[startj:j]); return '\n'.join(out)
            j += 1
        out.append(line[startj:]); i += 1; j = 0
        if i - anchor_line > 400: break        # a runaway means the tokenizer lost the thread
    return '\n'.join(out)

FUNC = re.compile(r'^\s*static\s+\w[\w:<>\s\*&]*?\b(\w+)\s*\(', re.M)
whole = '\n'.join(src)
def named_body(fn):
    m = re.search(r'^\s*static\s+[\w:<>\s\*&]+\b' + re.escape(fn) + r'\s*\(', whole, re.M)
    if not m: return ''
    k = whole.find('{', m.end())
    if k < 0: return ''
    d = 0
    for i in range(k, len(whole)):
        if whole[i] == '{': d += 1
        elif whole[i] == '}':
            d -= 1
            if d == 0: return whole[k:i+1]
    return ''

def arg0_kind(body):
    """How this handler reads args[0]. The shipped bug in one function.
    'dispatch' is the ONE answer compatible with every other: a body that ASKS what shape args[0]
    is before reading it cannot be the deletePreset failure, because that failure was a body
    reading one shape while the page sent the other. Checked FIRST for that reason."""
    if re.search(r'args\[0\]\s*\.\s*is(String|Int|Int64|Double|Bool|Object|Array|Void)\s*\(', body):
        return 'dispatch'
    if re.search(r'static_cast\s*<\s*int\s*>\s*\(\s*args\[0\]', body) or re.search(r'\(\s*int\s*\)\s*args\[0\]', body):
        return 'int'
    if re.search(r'args\[0\]\s*\.\s*toString\s*\(', body):
        return 'string'
    for t in ('float', 'double'):
        if re.search(r'static_cast\s*<\s*' + t + r'\s*>\s*\(\s*args\[0\]', body) or re.search(r'\(\s*' + t + r'\s*\)\s*args\[0\]', body):
            return 'num'
    if re.search(r'static_cast\s*<\s*bool\s*>\s*\(\s*args\[0\]', body) or re.search(r'\(\s*bool\s*\)\s*args\[0\]', body):
        return 'bool'
    return '' if 'args[0]' in body else 'noargs'

meanings = {}   # name -> {kind: [(line, chain)]}
for cs in chain_starts:
    anchor = cs
    for back in (0, 1, 2):
        cand = cs - back
        if cand < 0: break
        if '(' in src[cand]:
            anchor = cand
            if close_paren(anchor) > cs: break
    ce = close_paren(anchor)
    for ln in range(anchor, ce + 1):
        for m in REG.finditer(src[ln]):
            if m.group(1) != 'withNativeFunction': continue
            nm = m.group(2)
            body = body_after(ln, m.start())
            # a bare identifier callback -> resolve it to the function it names
            tail = body[body.find(nm) + len(nm):]
            id_m = re.match(r'"\s*,\s*([A-Za-z_]\w*)\s*$', tail.strip())
            if id_m: body = named_body(id_m.group(1)) or body
            meanings.setdefault(nm, {}).setdefault(arg0_kind(body), []).append((ln + 1, anchor + 1))

splits = {n: k for n, k in meanings.items()
          if len({x for x in k if x and x not in ('noargs', 'dispatch')}) > 1}
dispatchers = sorted(n for n, k in meanings.items() if 'dispatch' in k)
print(f'\nSECTION B — ONE NAME, TWO MEANINGS ACROSS CHAINS  ({len(meanings)} distinct names seen)')
if dispatchers:
    print(f'  shape-dispatching handlers (a name that asks what args[0] IS before reading it): {dispatchers}')
new_splits = 0
for n, kinds in sorted(splits.items()):
    known = (n in KNOWN_SPLITS) and not STRICT
    if not known: new_splits += 1
    where = ' · '.join(f'{k}@line {v[0][0]} (chain @{v[0][1]})' for k, v in sorted(kinds.items()) if k and k != 'noargs')
    print(f'  {"KNOWN" if known else "NEW  "}  "{n}" reads args[0] as {where}')
    if n in KNOWN_SPLITS:
        for chunk in [KNOWN_SPLITS[n][i:i+104] for i in range(0, len(KNOWN_SPLITS[n]), 104)]:
            print(f'           {chunk}')
if not splits: print('  (none)')
if STRICT:
    # With KNOWN_SPLITS empty there is nothing for the allowlist knob to reveal, so under STRICT the
    # gate asserts the STRONGER thing instead: a name that means two things anywhere must be
    # reachable only through a dispatcher, and there must be at least one. That keeps the knob
    # able to go red -- point it at a tree where the dispatcher has been reverted and it will.
    if not dispatchers:
        print('  STRICT: no shape-dispatching handler found at all — the fb602 cure is gone')
        new_splits += 1
print(f'\nSECTION B — UNDECLARED MEANING SPLITS: {new_splits}'
      + ('   [TI_SPLIT_STRICT=1: allowlist OFF, dispatcher required]' if STRICT else ''))

print(f'\nRESULT: ' + ('FAIL' if (total_dupes or new_splits) else 'PASS'))
sys.exit(1 if (total_dupes or new_splits) else 0)
