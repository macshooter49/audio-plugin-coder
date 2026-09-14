#!/usr/bin/env python3
"""fb639 + fb640 — THE BROWSER NEVER WAITS: not for the disk (fb639), not for JUCE's escaper (fb640).

Max: "every time I try to open the tables, it freezes my FL Studio for about one second … the whole entire DAW … it
shouldn't freeze my DAW ever." fb639 moved the folder walks off the message thread (the host's UI thread). Max, after
it: "it still does that little freeze." Tests/browser_freeze_sim.sh (the real editor + WKWebView + a real mousedown)
found the rest: ~700 ms per open in juce::WebBrowserComponent::Impl::emitEvent, whose String::replace escape is
quadratic in the number of backslashes — and a JSON list sent as a string carries one per quote. This gate pins both
statically (comments and string literals blanked where code is read):
  [1] listNoiseImports / listWtImports / listSampleImports answer through requestImportsJson — none calls getImportsJson;
  [2] the ONLY call of getImportsJson in Source/ is inside the wtIoPool_ job in startImportsScan;
  [3] editor code never edits the registry directly — only addImportPathAsync / removeImportPathAsync;
  [4] editImportsRegistry only ever try_locks importsLock_ (the message thread must not wait on a walk);
  [5] requestImportsJson itself does no I/O and no serialisation (no findChildFiles / getImportsJson / JSON::toString);
  [6] createEditor prefetches the lists;
  [7] fb640 — all FOUR list natives (the three above + listImports) complete through tiListPayload, which sends
      "b64:" + base64 — nothing for emitEvent's replace to find — and none completes with a raw juce::var of the JSON;
  [8] fb640 — the page decodes exactly those four in Juce.getNativeFunction, and the decoder really round-trips UTF-8
      JSON (run under node: control characters, accents, CJK, emoji, backslashes, apostrophes);
  [9] fb640 — listImports never walks on the message thread: it goes through requestImportsJson (kManagedWtKind), and the
      only call of getManagedWavetablesJson in Source/ is the wtIoPool_ job in startImportsScan.
    python3 Tests/browser_never_waits_gate.py
    python3 Tests/browser_never_waits_gate.py --controls   # every mutation below must exit 1 with its bar red
    NEVERWAIT_MUT=sync|block|direct|noprefetch|io|rawpayload|nodecode|badb64|managedsync
"""
import base64, json, os, re, shutil, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); SRC = os.path.join(HERE, '..', 'Source')
MUT = os.environ.get('NEVERWAIT_MUT', '')

CONTROLS = {'sync': '1', 'block': '4', 'direct': '3', 'noprefetch': '6', 'io': '5',
            'rawpayload': '7', 'nodecode': '8', 'badb64': '8', 'managedsync': '9'}
if '--controls' in sys.argv:
    bad = 0
    for mut, tag in CONTROLS.items():
        r = subprocess.run([sys.executable, os.path.abspath(__file__)], env={**os.environ, 'NEVERWAIT_MUT': mut},
                           capture_output=True, text=True)
        red = re.findall(r'✗ \[(\d)\]', r.stdout)
        ok = r.returncode == 1 and tag in red
        bad += not ok
        print(f"  {'✓' if ok else '✗'} NEVERWAIT_MUT={mut:<11} exit {r.returncode} (want 1, bar [{tag}] red)  red={red}")
    print('browser_never_waits_gate controls:', 'ALL AS EXPECTED' if not bad else f'{bad} WRONG')
    sys.exit(1 if bad else 0)

def strip(code):
    out, i, n = [], 0, len(code)
    while i < n:
        c = code[i]; d = code[i + 1] if i + 1 < n else ''
        if c == '/' and d == '/':
            j = code.find('\n', i); j = n if j < 0 else j; out.append(' ' * (j - i)); i = j
        elif c == '/' and d == '*':
            j = code.find('*/', i + 2); j = n if j < 0 else j + 2; out.append(re.sub(r'[^\n]', ' ', code[i:j])); i = j
        elif c == 'R' and d == '"' and (i == 0 or not (code[i - 1].isalnum() or code[i - 1] == '_')):
            m = re.match(r'R"([^()\\\s]{0,16})\(', code[i:])
            if not m: out.append(c); i += 1; continue
            close = ')' + m.group(1) + '"'; j = code.find(close, i + m.end()); j = n if j < 0 else j + len(close)
            out.append(re.sub(r'[^\n]', ' ', code[i:j])); i = j     # length-preserving: positions stay aligned
        elif c == '"':
            j = i + 1
            while j < n and code[j] not in '"\n': j += 2 if code[j] == '\\' else 1
            out.append('"' + ' ' * (min(j, n) - i - 1) + ('"' if j < n else '')); i = j + 1   # length-preserving
        else:
            out.append(c); i += 1
    return ''.join(out)

def body_at(code, pos):
    i = code.index('{', pos); depth = 0
    for j in range(i, len(code)):
        if code[j] == '{': depth += 1
        elif code[j] == '}':
            depth -= 1
            if depth == 0: return code[i:j + 1]
    return None

def body_after(code, pat):
    m = re.search(pat, code)
    if not m: return None
    return body_at(code, m.end())

ed_raw = open(os.path.join(SRC, 'PluginEditor.cpp'), encoding='utf-8').read()
pp_raw = open(os.path.join(SRC, 'PluginProcessor.cpp'), encoding='utf-8').read()
html   = open(os.path.join(SRC, 'ui', 'public', 'index.html'), encoding='utf-8').read()
if MUT == 'sync':   ed_raw = ed_raw.replace('audioProcessor.requestImportsJson (1,', 'complete (juce::var (audioProcessor.getImportsJson (1))); audioProcessor.requestImportsJson (1,', 1)
if MUT == 'direct': ed_raw = ed_raw.replace('audioProcessor.addImportPathAsync (1,', 'audioProcessor.addImportPath (1,', 1)
if MUT == 'block':  pp_raw = pp_raw.replace('std::unique_lock<std::mutex> g (importsLock_, std::try_to_lock);', 'std::unique_lock<std::mutex> g (importsLock_);', 1)
if MUT == 'noprefetch': pp_raw = pp_raw.replace('prefetchImportsJson();   // fb639', '/* gone */   // fb639', 1)
if MUT == 'io':     pp_raw = pp_raw.replace('done (importsJson_[k]);', 'done (juce::JSON::toString (importsCache_[k]));', 1)
if MUT == 'rawpayload':  ed_raw = ed_raw.replace('complete (tiListPayload (js));', 'complete (juce::var (js));', 1)      # the fb639 form
if MUT == 'nodecode':    html = html.replace('listWtImports: 1, ', '', 1)                                                  # C++ sends b64, the page no longer decodes it
if MUT == 'badb64':      html = html.replace('return new TextDecoder("utf-8").decode(u);', 'return bin;', 1)             # decodes bytes as Latin-1
if MUT == 'managedsync': ed_raw = ed_raw.replace('audioProcessor.requestImportsJson (TerrainAudioProcessor::kManagedWtKind,',
                                                 'complete (juce::var (audioProcessor.getManagedWavetablesJson())); audioProcessor.requestImportsJson (TerrainAudioProcessor::kManagedWtKind,', 1)
ed, pp = strip(ed_raw), strip(pp_raw)

fails = []
def bar(tag, ok, text, detail=''):
    print("  %s [%s] %s%s" % ('✓' if ok else '✗', tag, text, ('\n        ' + detail) if detail and not ok else ''))
    if not ok: fails.append(tag)

def native_body(nat):
    m = re.search(r'withNativeFunction\s*\(\s*"%s"' % nat, ed_raw)       # the name lives in a string: find it raw,
    return None if not m else body_at(ed, m.start())                            # then read the body from the stripped copy

bad = []
for nat in ('listNoiseImports', 'listWtImports', 'listSampleImports'):
    b = native_body(nat)
    if b is None or 'requestImportsJson' not in b or 'getImportsJson' in b: bad.append(nat)
bar(1, not bad, 'the three list natives answer via requestImportsJson, none walks the disk itself', 'offenders: %s' % bad)

calls = [m.start() for m in re.finditer(r'\bgetImportsJson\s*\(', pp) ]
defs = [m.start() for m in re.finditer(r'TerrainAudioProcessor::getImportsJson\s*\(', pp)]
job = body_after(pp, r'void\s+TerrainAudioProcessor::startImportsScan\s*\(')
extra = len(re.findall(r'\bgetImportsJson\s*\(', ed))
inside = job is not None and len(re.findall(r'\bgetImportsJson\s*\(', job)) == 1 and 'wtIoPool_.addJob' in job
bar(2, inside and len(calls) - len(defs) == 1 and extra == 0,
    'the only getImportsJson call is the wtIoPool_ job in startImportsScan',
    'calls outside the definition: %d in the processor, %d in the editor' % (len(calls) - len(defs), extra))

direct = re.findall(r'\b(addImportPath|removeImportPath)\s*\(', ed)
bar(3, not direct, 'editor code edits the registry only through the Async wrappers', 'direct calls: %s' % direct)

er = body_after(pp, r'void\s+TerrainAudioProcessor::editImportsRegistry\s*\(')
bar(4, er is not None and 'std::try_to_lock' in er and not re.search(r'lock_guard|unique_lock<std::mutex>\s*\w+\s*\(\s*importsLock_\s*\)', er),
    'editImportsRegistry only try_locks importsLock_ — the message thread never waits on a walk')

rq = body_after(pp, r'void\s+TerrainAudioProcessor::requestImportsJson\s*\(')
io = [w for w in ('findChildFiles', 'getImportsJson', 'getManagedWavetablesJson', 'JSON::toString', 'existsAsFile', 'isDirectory')
      if rq is None or w in rq]
bar(5, rq is not None and not io, 'requestImportsJson does no I/O and no serialisation', 'found: %s' % io)

ce = body_after(pp, r'TerrainAudioProcessor::createEditor\s*\(\s*\)')
bar(6, ce is not None and 'prefetchImportsJson' in ce, 'createEditor prefetches the browser lists off the message thread')

# ── [7] the payload crosses as base64 ───────────────────────────────────────────────────────────────────────────────
LISTS = ('listNoiseImports', 'listWtImports', 'listSampleImports', 'listImports')
hm = re.search(r'static\s+juce::var\s+tiListPayload\s*\(\s*const\s+juce::String\s*&\s*json\s*\)', ed_raw)
hb = body_at(ed_raw, hm.end()) if hm else None                                  # raw: its "b64:" literal must be read
helper_ok = hb is not None and '"b64:"' in hb and 'Base64::toBase64' in hb and 'toRawUTF8' in hb and 'getNumBytesAsUTF8' in hb
bad7 = []
for nat in LISTS:
    b = native_body(nat)
    if b is None or 'tiListPayload' not in b or re.search(r'complete\s*\(\s*juce::var\s*\(\s*(js|audioProcessor)\b', b):
        bad7.append(nat)
bar(7, helper_ok and not bad7, 'all four list natives complete through tiListPayload ("b64:" + base64 of the UTF-8 JSON)',
    ('helper missing or not base64/UTF-8; ' if not helper_ok else '') + 'offenders: %s' % bad7)

# ── [8] the page decodes exactly those four, and the decoder is right ──────────────────────────────────────────────
problems = []
lm = re.search(r'const\s+B64_LISTS\s*=\s*\{([^}]*)\}', html)
names = set(re.findall(r'(\w+)\s*:\s*1', lm.group(1))) if lm else set()
if names != set(LISTS): problems.append('B64_LISTS = %s, want %s' % (sorted(names), sorted(LISTS)))
gm = re.search(r'function\s+getNativeFunction\s*\(\s*name\s*\)', html)
gb = body_at(html, gm.end()) if gm else ''
if not re.search(r'return\s+B64_LISTS\s*\[\s*name\s*\]\s*\?\s*result\s*\.\s*then\s*\(\s*b64Text\s*\)\s*:\s*result\s*;', gb or ''):
    problems.append('getNativeFunction does not route B64_LISTS results through b64Text')
fm = re.search(r'function\s+b64Text\s*\(\s*v\s*\)', html)
fsrc = ('function b64Text(v) ' + body_at(html, fm.end())) if fm else ''
if not fsrc: problems.append('b64Text missing')
node = shutil.which('node')
if fsrc and not node: problems.append('node not found — the decoder round trip cannot be run')
elif fsrc:
    sample = {'name': '\x7fPLUTO 2 — Ωmega Größe 日本語 🎛️', 'path': 'C:\\Program Files\\Terrain\\it\'s "quoted"', 'n': list(range(40))}
    text = json.dumps(sample, ensure_ascii=False)
    b64 = 'b64:' + base64.b64encode(text.encode('utf-8')).decode('ascii')
    prog = fsrc + '\nconst s=process.argv[1];const out=b64Text(s);process.stdout.write(JSON.stringify([out, b64Text("plain"), b64Text(42)]));'
    r = subprocess.run([node, '-e', prog, b64], capture_output=True, text=True, encoding='utf-8')
    try:
        out, plain, num = json.loads(r.stdout)
        if out != text: problems.append('round trip differs: %r' % out[:80])
        if plain != 'plain' or num != 42: problems.append('a non-b64 value was altered')
    except Exception as e:
        problems.append('node run failed: %s %s' % (e, r.stderr[:200]))
bar(8, not problems, 'the page decodes exactly the four lists in Juce.getNativeFunction, and the decoder round-trips UTF-8 JSON',
    '; '.join(problems))

# ── [9] listImports never walks on the message thread ───────────────────────────────────────────────────────────────
li = native_body('listImports')
mcalls = len(re.findall(r'\bgetManagedWavetablesJson\s*\(', pp)) - len(re.findall(r'TerrainAudioProcessor::getManagedWavetablesJson\s*\(', pp))
ecalls = len(re.findall(r'\bgetManagedWavetablesJson\s*\(', ed))
in_job = job is not None and len(re.findall(r'\bgetManagedWavetablesJson\s*\(', job)) == 1
bar(9, li is not None and 'requestImportsJson' in li and 'kManagedWtKind' in li and ecalls == 0 and mcalls == 1 and in_job,
    'listImports goes through requestImportsJson (kManagedWtKind); the only getManagedWavetablesJson call is the wtIoPool_ job',
    'editor calls: %d, processor calls outside the definition: %d, in the job: %s' % (ecalls, mcalls, in_job))

print('browser_never_waits_gate:', 'PASS' if not fails else 'FAIL ' + ' '.join('[%s]' % f for f in fails))
sys.exit(1 if fails else 0)
