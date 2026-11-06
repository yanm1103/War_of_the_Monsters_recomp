"""HUD portrait from GTA SA's fist icon: 512x256, left = normal, right = hurt (red), flipped top to bottom as the HUD wants.
  python fistportrait.py fist.png out.png [preview.png]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw  # noqa: E402
from preview import read_png  # noqa: E402

w, h, d = read_png(sys.argv[1])


def sample(u, v):
    """Bilinear, premultiplied alpha; u, v in source pixels."""
    x0, y0 = int(u), int(v)
    fx, fy = u - x0, v - y0
    acc = [0.0, 0.0, 0.0, 0.0]
    for dx, dy, wt in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)), (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
        x, y = min(w - 1, max(0, x0 + dx)), min(h - 1, max(0, y0 + dy))
        o = 4 * (y * w + x)
        a = d[o + 3] / 255.0
        for c in range(3):
            acc[c] += d[o + c] * a * wt
        acc[3] += a * wt
    return acc


S = 256
MARGIN = 0.06
out = bytearray(2 * S * S * 4)
for half, tint in ((0, (1.0, 1.0, 1.0)), (1, (1.0, 0.32, 0.26))):
    for y in range(S):
        for x in range(S):
            u = (x / S - MARGIN) / (1 - 2 * MARGIN) * w - 0.5
            v = (y / S - MARGIN) / (1 - 2 * MARGIN) * h - 0.5
            c = sample(u, v) if -1 < u < w and -1 < v < h else [0, 0, 0, 0]
            # background: dark, a little lighter in the middle
            r2 = ((x - S / 2) ** 2 + (y - S / 2) ** 2) / (S / 2) ** 2
            bg = 34 - 22 * min(1.0, r2)
            bgc = (bg * (1.4 if half else 1.0), bg * (0.7 if half else 1.0), bg * (0.7 if half else 1.0))
            o = 4 * ((S - 1 - y) * 2 * S + half * S + x)   # flipped top to bottom
            for k in range(3):
                out[o + k] = max(0, min(255, int(round(c[k] * tint[k] + bgc[k] * (1 - c[3])))))
            out[o + 3] = 255
rw.write_png(sys.argv[2], 2 * S, S, bytes(out))
if len(sys.argv) > 3:   # upright, for looking at it
    up = bytearray(len(out))
    for y in range(S):
        up[4 * 2 * S * y:4 * 2 * S * (y + 1)] = out[4 * 2 * S * (S - 1 - y):4 * 2 * S * (S - y)]
    rw.write_png(sys.argv[3], 2 * S, S, bytes(up))
