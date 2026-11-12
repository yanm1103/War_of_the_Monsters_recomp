"""Narration size of a tutorial script: characters per scene (what the TTS bills) and a rough spoken length.

    python tools/tutorial/scriptinfo.py tools/tutorial/skin/script.json
"""
import json
import sys

WPS = 2.6   # words per second of an energetic narrator, a rough guess until the real audio exists

with open(sys.argv[1], encoding='utf-8') as f:
    s = json.load(f)
tc = tw = 0
for sc in s['scenes']:
    n = sc['narration']
    c, w = len(n), len(n.split())
    tc, tw = tc + c, tw + w
    print('%-12s %4d chars %3d words ~%4.1f s' % (sc['id'], c, w, w / WPS))
print('total        %4d chars %3d words ~%4.0f s' % (tc, tw, tw / WPS))
