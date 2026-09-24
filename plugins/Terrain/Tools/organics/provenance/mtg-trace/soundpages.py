import re, subprocess, json, time, os, html
os.chdir(os.path.dirname(os.path.abspath(__file__)))
packs = json.load(open('packs.json'))
os.makedirs('pages', exist_ok=True)
res = {}
for pid, p in packs.items():
    for sid, s in p['sounds'].items():
        fn = f'pages/{sid}.html'
        if not os.path.exists(fn) or os.path.getsize(fn) < 5000:
            subprocess.run(['curl', '-sL', '-A', 'Mozilla/5.0', '-o', fn, f'https://freesound.org/people/MTG/sounds/{sid}/'])
            time.sleep(0.4)
        h = open(fn, encoding='utf-8', errors='replace').read()
        lic = sorted(set(re.findall(r'https?://creativecommons\.org/(?:licenses|publicdomain)/[a-z0-9\-+]+/[0-9.]+/?', h)))
        prev = re.findall(r'https://cdn\.freesound\.org/previews/[^"\']+?-hq\.mp3', h)
        desc = re.search(r'<meta name="description" content="([^"]*)"', h)
        m_up = re.search(r'<meta property="og:audio:artist" content="([^"]*)"', h)
        res[sid] = dict(pack=pid, name=s['name'], user=s['user'], list_lic=s['lic'], lic_urls=lic,
                        preview=prev[0] if prev else None,
                        desc=html.unescape(desc.group(1)) if desc else None, artist=m_up.group(1) if m_up else None)
json.dump(res, open('sounds.json', 'w'), indent=1)
from collections import Counter
print(Counter(tuple(v['lic_urls']) for v in res.values()))
print(Counter(v['user'] for v in res.values()), Counter(v['artist'] for v in res.values()))
print(sum(1 for v in res.values() if v['preview']), 'previews')
print(list(res.values())[0])
