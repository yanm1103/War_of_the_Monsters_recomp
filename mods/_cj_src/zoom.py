"""Side by side, scaled 4x: PNGs given -> one PNG.  python zoom.py out.png a.png b.png ..."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw  # noqa: E402
from preview import read_png  # noqa: E402

imgs = [read_png(p) for p in sys.argv[2:]]
K = 4
H = max(h for w, h, d in imgs) * K
W = sum(w for w, h, d in imgs) * K + 8 * (len(imgs) - 1)
out = bytearray(b"\x20\x20\x20\xff" * W * H)
x0 = 0
for w, h, d in imgs:
    for y in range(h * K):
        for x in range(w * K):
            s = 4 * ((y // K) * w + x // K)
            o = 4 * (y * W + x0 + x)
            out[o:o + 3] = d[s:s + 3]
            out[o + 3] = 255
    x0 += w * K + 8
rw.write_png(sys.argv[1], W, H, bytes(out))
