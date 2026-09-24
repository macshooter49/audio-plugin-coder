import re, subprocess, json, time, urllib.parse, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
out = {}
for pid in [20239, 20247, 20251, 20253]:
    h = subprocess.run(['curl', '-sL', '-A', 'Mozilla/5.0', f'https://freesound.org/people/MTG/packs/{pid}/'], capture_output=True, text=True).stdout
    title = re.search(r'<title>Freesound - ([^<]*) by MTG', h).group(1)
    q = urllib.parse.quote(f'"{pid}_{title}"')
    sounds = {}
    page = 1
    total = None
    while True:
        url = f'https://freesound.org/search/?f=pack_grouping:{q}&s=Date+added+(newest+first)&g=1&page={page}'
        h = subprocess.run(['curl', '-sL', '-A', 'Mozilla/5.0', url], capture_output=True, text=True).stdout
        blocks = re.split(r'(?=<a class="bw-link--black" href="/people/)', h)
        n0 = len(sounds)
        for b in blocks[1:]:
            m = re.match(r'<a class="bw-link--black" href="/people/([^/]+)/sounds/(\d+)/" title="([^"]*)"', b)
            if not m:
                continue
            lic = re.search(r'title="License: ([^"]*)"', b)
            sounds[m.group(2)] = dict(user=m.group(1), name=m.group(3), lic=lic.group(1) if lic else None)
        t = re.search(r'(\d+) sounds', h)
        if t and not total:
            total = t.group(1)
        if len(sounds) == n0:
            break
        page += 1
        time.sleep(1)
    out[pid] = dict(title=title, total=total, sounds=sounds)
    print(pid, title, len(sounds), total, {v['lic'] for v in sounds.values()}, {v['user'] for v in sounds.values()})
json.dump(out, open('packs.json', 'w'), indent=1)
