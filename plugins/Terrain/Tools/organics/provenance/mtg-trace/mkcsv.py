import json, os, csv, re, collections
D = os.path.dirname(os.path.abspath(__file__))
m = json.load(open(os.path.join(D, 'match.json')))
S = json.load(open(os.path.join(D, 'sounds.json')))
packs = json.load(open(os.path.join(D, 'packs.json')))
DATA = os.path.expanduser('~/Developer/VST-Plugins/organics-library/raw/sfzinstruments/MTG.SoloSax/MTG Solo Saxophones/Data')
# region key per sample file (from the rr1 data files: key=NN sample=xxx)
key = {}
for fn in os.listdir(DATA):
    for line in open(os.path.join(DATA, fn)):
        mm = re.search(r'key=(\d+) sample=(\w+)\.\$EXT', line)
        if mm:
            key.setdefault(mm.group(2) + '.flac', int(mm.group(1)))
out = os.path.join(D, 'mtg-solosax-freesound.csv')
rows = []
dk = collections.Counter()
for fn in sorted(m):
    if fn[4] not in 'fpm':
        continue
    v = m[fn]
    s = S[v['sid']]
    k = key.get(fn)
    if k is not None and s['midi'] is not None:
        dk[k - s['midi']] += 1
    rows.append(dict(source_file='sfzinstruments/MTG.SoloSax/MTG Solo Saxophones/Samples/' + fn,
                     freesound_id=v['sid'], freesound_url=f"https://freesound.org/people/MTG/sounds/{v['sid']}/",
                     freesound_name=s['name'], uploader=s['user'], recordist='Music Technology Group, Universitat Pompeu Fabra (good-sounds.org)',
                     licence='CC-BY-3.0', licence_url=s['lic_urls'][0], pack=f"https://freesound.org/people/MTG/packs/{s['pack']}/",
                     good_sounds_id=s['gsid'], match_r=v['r'], runner_up_r=v['second_r'], checked='2026-09-24'))
with open(out, 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0]))
    w.writeheader()
    w.writerows(rows)
print(len(rows), 'rows; key - freesound midi offsets:', dict(dk))
print('distinct sids', len({r['freesound_id'] for r in rows}))
# the full per-sound licence audit of all 4 packs (every sound, whether used or not)
with open(os.path.join(D, 'mtg-sax-packs-licence-audit.csv'), 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['freesound_id', 'url', 'name', 'uploader', 'pack', 'licence_url', 'uploaded', 'description', 'checked'])
    for sid, s in sorted(S.items()):
        w.writerow([sid, f'https://freesound.org/people/MTG/sounds/{sid}/', s['name'], s['user'],
                    f"{s['pack']} {packs[s['pack']]['title']}", s['lic_urls'][0], s['date'], s['desc'], '2026-09-24'])
