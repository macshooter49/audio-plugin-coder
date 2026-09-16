import re, os
def norm(s): return re.sub(r'[_\-\.]+',' ',s.lower())
def exclude(o):
    rel=o['rel']; k=o['kit']; low=rel.lower(); base=os.path.basename(low)
    if base.endswith('.mp3'): return 'mp3 demo'
    if 'important - terms' in low: return 'terms'
    if k=='Nuclear' and '@macshooter49' not in low: return 'not @macshooter49'
    if k!='Essentia Looped' and re.search(r'(^|/)[^/]*loops?(/|$)', low) and 'looped' not in low: return 'loop'
    if re.search(r'\bloop\b', norm(base)) and k!='Essentia Looped': return 'loop'
    if 'phrase' in low: return 'phrase'
    if 'songstarter' in low: return 'songstarter'
    if 'pedal memories' in low and re.search(r'\d+\s*bpm', low): return 'pedal loop (bpm)'
    return None
RULES=[('808',r'\b808s?\b'),('Drums',r'\b(kick|kicks|snare|snares|rim|rims|clap|claps|hi ?hats?|hats?|open hats?|percs?|percussion|shaker|tom|cymbal|crash|noises = percussion)\b'),
 ('Bass',r'\b(bass|basses|sub)\b'),('Bell',r'\bbells?\b'),('Keys',r'\b(keys?|piano|rhodes|ep)\b'),('Organ',r'\borgan\b'),('Lead',r'\bleads?\b'),
 ('Pad',r'\bpads?\b'),('Pluck',r'\bplucks?\b|\bplucked\b'),('Guitar',r'\bguitars?\b'),('Vocal',r'\b(vocal|vox|choir|voice)\b'),('Flute',r'\bflutes?\b'),
 ('Chord',r'\bchords?\b'),('Accent',r'\b(accents?|runs?|melody accents)\b'),('Atmosphere',r'\b(soundscapes?|textures?|ambien\w*|atmos\w*|textural)\b'),
 ('FX',r'\b(fx|riser|impact|sweep|whoosh|extra \+ fx|whistle)\b'),('Synth',r'\b(synths?|tones?|sequencer)\b')]
def classify(o):
    txt=norm(o['rel']); 
    for cat,rx in RULES:
        if re.search(rx,txt): return cat
    return None
