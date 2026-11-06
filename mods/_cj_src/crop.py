"""Crop a PNG and scale it K times (nearest).  python crop.py in.png out.png x0 y0 x1 y1 K"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw  # noqa: E402
from preview import read_png  # noqa: E402

w, h, d = read_png(sys.argv[1])
x0, y0, x1, y1, K = map(int, sys.argv[3:8])
W, H = (x1 - x0) * K, (y1 - y0) * K
out = bytearray(W * H * 4)
for y in range(H):
    for x in range(W):
        s = 4 * ((y0 + y // K) * w + x0 + x // K)
        out[4 * (y * W + x):4 * (y * W + x) + 4] = d[s:s + 4]
rw.write_png(sys.argv[2], W, H, bytes(out))
