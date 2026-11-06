"""Nearest-neighbour resize of a PNG.  python resize.py in.png out.png W H"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw  # noqa: E402
from preview import read_png  # noqa: E402

w, h, d = read_png(sys.argv[1])
W, H = int(sys.argv[3]), int(sys.argv[4])
out = bytearray(W * H * 4)
for y in range(H):
    for x in range(W):
        s = 4 * ((y * h // H) * w + x * w // W)
        out[4 * (y * W + x):4 * (y * W + x) + 4] = d[s:s + 4]
rw.write_png(sys.argv[2], W, H, bytes(out))
