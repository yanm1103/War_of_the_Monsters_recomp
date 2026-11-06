"""PNG alpha as a gray image (white glyphs on black).  python alpha.py in.png out.png"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw  # noqa: E402
from preview import read_png  # noqa: E402

w, h, d = read_png(sys.argv[1])
out = bytearray(w * h * 4)
for i in range(w * h):
    a = d[4 * i + 3]
    out[4 * i:4 * i + 4] = bytes([a, a, a, 255])
rw.write_png(sys.argv[2], w, h, bytes(out))
