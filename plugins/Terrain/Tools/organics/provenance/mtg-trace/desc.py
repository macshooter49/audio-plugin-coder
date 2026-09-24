import re, json, os, subprocess, time
os.chdir(os.path.dirname(os.path.abspath(__file__)))
S = json.load(open('sounds.json'))
os.makedirs('prev', exist_ok=True)
for sid, v in S.items():
    h = open(f'pages/{sid}.html', encoding='utf-8', errors='replace').read()
    t = re.sub(r'<script.*?</script>', '', h, flags=re.S)
    t = re.sub(r'<[^>]+>', ' ', t)
    t = re.sub(r'\s+', ' ', t)
    m = re.search(r'(Recorded in the context.*?)Sound illegal', t)
    v['desc'] = m.group(1).strip() if m else None
    v['date'] = (re.search(r'MTG (\w+ \d+\w\w, \d{4}) Follow', t) or [None, None])[1]
    v['midi'] = int(re.search(r'midi note::(\d+)', t).group(1)) if 'midi note::' in t else None
    v['gsid'] = (re.search(r'good-sounds-id::(\d+)', t) or [None, None])[1]
    fn = f'prev/{sid}.mp3'
    if not os.path.exists(fn):
        subprocess.run(['curl', '-sL', '-o', fn, v['preview']])
        time.sleep(0.2)
json.dump(S, open('sounds.json', 'w'), indent=1)
for sid in list(S)[:3]:
    print(sid, S[sid]['name'], S[sid]['desc'])
from collections import Counter
print(Counter(bool(v['desc'] and 'good-sounds.org project from the Music Technology Group' in v['desc']) for v in S.values()))
print(Counter((v['pack'], v['midi']) for v in S.values()).most_common(5))
