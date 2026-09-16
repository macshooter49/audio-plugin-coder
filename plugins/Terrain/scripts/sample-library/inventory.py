import os, json, re, sys, soundfile as sf
W=os.environ.get('WAVES_CRATE', '/Users/macshooter/Desktop/waves crate')   # the kits' root; override with WAVES_CRATE=…
SRC=[('Anachronous','2023/MULTI KITS/MACSHOOTER - ANACHRONOUS MULTI-KIT/01 - One Shots'),
('Soundsource 1','2023/MULTI KITS/MACSHOOTER - SOUNDSOURCE VOL. 1/SOUNDSOURCE - ONE-SHOTS'),
('Soundsource 2','2023/MULTI KITS/MACSHOOTER - SOUNDSOURCE VOL. 2'),
('Soundsource 3','2023/MULTI KITS/MACSHOOTER - SOUNDSOURCE VOL. 3'),
('Soundsource 4','2023/MULTI KITS/MACSHOOTER - SOUNDSOURCE VOL. 4'),
('WC Vol 3 Textures','2023/MULTI KITS/WAVES CRATE - VOL. 3/WAVES CRATE - TEXTURES'),
('Nuclear','2023/ONE SHOT KITS/Nuclear Vol. 1 - @macshooter49 x @trifreeze'),
('Creator Crate','2024/7. MULTI KITS/WAVES CRATE - CREATOR CRATE VOL. 1'),
('Essentia Looped','2024/7. MULTI KITS/WAVES CRATE x ESSENTIA AUDIO - ESSENTIA CRATE/1. STUDIO ESSENTIALS/LOOPED ONESHOTS'),
('Splice Archives','2025/Waves Crate 1.0/7. MULTI KITS/SPLICE - MACSHOOTER ARCHIVES'),
('Pedalphonics','2025/Waves Crate 1.0/7. MULTI KITS/WAVES CRATE - PEDALPHONICS VOL. 1/4. PDL + ONE S.'),
('Circuit Motions','2025/Waves Crate 1.0/9. VSTS/WAVES CRATE - CIRCUIT MOTIONS/2. ONE_SHOTS')]
EXT=('.wav','.aif','.aiff','.mp3','.flac')
out=[]
for kit,rel in SRC:
    root=os.path.join(W,rel)
    for dp,dn,fn in os.walk(root):
        for f in fn:
            if f.startswith('.') or not f.lower().endswith(EXT): continue
            p=os.path.join(dp,f); r=os.path.relpath(p,root)
            info={'kit':kit,'rel':r,'path':p,'bytes':os.path.getsize(p)}
            try:
                i=sf.info(p); info.update(sr=i.samplerate,ch=i.channels,dur=round(i.duration,3))
            except Exception as e: info['err']=str(e)[:60]
            out.append(info)
json.dump(out,open(sys.argv[1],'w'),indent=0)
print(len(out),'files'); 
from collections import Counter
print(Counter(o['kit'] for o in out))
print('errors',sum(1 for o in out if 'err' in o), [o['rel'] for o in out if 'err' in o][:8])
print('total MB',round(sum(o['bytes'] for o in out)/1e6), 'total min', round(sum(o.get('dur',0) for o in out)/60,1))
print('sr', Counter(o.get('sr') for o in out))
