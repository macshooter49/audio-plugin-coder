#!/usr/bin/env python3
"""tp39f — THE STRIKER FOLLOWS THE KEY. Max: "some one shots stay locked to one note … no matter what key I play."
Renders Tests/fixtures/modal_striker.terrain (built by Tests/_tp39f_fixture.js: osc A Modal Pluck + a Keys one-shot, osc B
Modal Brass + a Bell one-shot, nothing else) through the REAL installed AU, every oscillator alone with effects / flow / filters / unison off, and pitch-tracks single notes
48 / 55 / 60. Before the fix the Pluck / Brass / Skin oscillators all rang the striker's own C4 / C5 / C6.
    python3 Tests/modal_striker_pitch_gate.py            (builds Tests/au_preset_census.cpp into the scratch dir)"""
import os, re, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
exe = os.path.join(tempfile.gettempdir(), 'aucensus_pitch_gate')
src = os.path.join(here, 'au_preset_census.cpp')
if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
    subprocess.run(['clang++', '-O2', '-std=c++17', src, '-o', exe, '-framework', 'AudioToolbox', '-framework', 'AudioUnit', '-framework', 'CoreFoundation', '-framework', 'CoreAudio'], check=True)
env = dict(os.environ, TP_BANK=os.path.join(here, 'fixtures'))
out = subprocess.run([exe, 'pitch', 'modal_striker'], env=env, capture_output=True, text=True).stdout
print(out.strip())
modal = [l for l in out.splitlines() if 'Modal' in l and 'osc' in l]
locked = [l for l in modal if 'LOCKED' in l]; tracks = [l for l in modal if l.rstrip().find('tracks') >= 0 and 'LOCKED' not in l]
silent = [l for l in modal if 'silent' in l]
ok = len(modal) >= 2 and not locked and not silent and len(tracks) == len(modal)
print('\nmodal oscillators %d · tracks %d · LOCKED %d · silent %d' % (len(modal), len(tracks), len(locked), len(silent)))
print('modal_striker_pitch_gate: ' + ('PASS' if ok else 'FAIL'))
sys.exit(0 if ok else 1)
