"""Select-screen logo from GTA SA font glyphs (fonts.txd): white letters, thick black outline, drop shadow,
transparent background, 512x256.
  python cjlogo.py <font.png> <out.png> <preview.png> row:col [row:col ...]
Font textures: 16 columns of 32 px, 13 rows of 512/13 px. font1 rows 9-10 = Pricedown (c = 9:13, j = 10:4);
font2 row 2 = gothic capitals (C = 2:3, J = 2:10).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw  # noqa: E402
from preview import read_png  # noqa: E402

FW, FH, F = read_png(sys.argv[1])
A = [[F[4 * (y * FW + x) + 3] / 255.0 for x in range(FW)] for y in range(FH)]
cells = [tuple(int(v) for v in s.split(":")) for s in sys.argv[4:]]


def glyph(row, col):
    """Cell (full height, so the baseline holds) cropped left and right to the ink."""
    y0, y1 = int(round(row * FH / 13.0)), int(round((row + 1) * FH / 13.0))
    x0, x1 = 32 * col, 32 * col + 32
    xs = [x for x in range(x0, x1) if any(A[yy][x] > 0.1 for yy in range(y0, y1))]
    return [[A[yy][x] for x in range(xs[0], xs[-1] + 1)] for yy in range(y0, y1)]


def smooth(a):
    t = max(0.0, min(1.0, (a - 0.28) / 0.44))
    return t * t * (3 - 2 * t)


K = 8                       # working scale
gs = [glyph(r, c) for r, c in cells]
gap = 4                     # source px
cw = sum(len(g[0]) for g in gs) + gap * (len(gs) - 1)
ch = len(gs[0])
SW, SH = cw * K, ch * K
big = [[0.0] * SW for _ in range(SH)]
ox = 0
for g in gs:
    gh, gw = len(g), len(g[0])
    for yy in range(SH):
        v = (yy + 0.5) / K - 0.5
        y0 = int(math.floor(v))
        fy = v - y0
        for xx in range(gw * K):
            u = (xx + 0.5) / K - 0.5
            x0 = int(math.floor(u))
            fx = u - x0
            s = 0.0
            for dx, dy, wt in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)), (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
                xi, yi = x0 + dx, y0 + dy
                if 0 <= xi < gw and 0 <= yi < gh:
                    s += g[yi][xi] * wt
            big[yy][ox * K + xx] = max(big[yy][ox * K + xx], smooth(s))
    ox += gw + gap
# fit the ink to the canvas
ys = [y for y in range(SH) if max(big[y]) > 0.02]
xs = [x for x in range(SW) if any(big[y][x] > 0.02 for y in ys)]
W, H = 512, 256
PAD = 26
sc = min((W - 2 * PAD) / (xs[-1] - xs[0] + 1), (H - 2 * PAD) / (ys[-1] - ys[0] + 1))
tw, th = int((xs[-1] - xs[0] + 1) * sc), int((ys[-1] - ys[0] + 1) * sc)
offx, offy = (W - tw) // 2 - 4, (H - th) // 2 - 4
mask = [[0.0] * W for _ in range(H)]
for y in range(th):
    for x in range(tw):
        # box filter of the big image (downscale)
        sx0, sx1 = xs[0] + x / sc, xs[0] + (x + 1) / sc
        sy0, sy1 = ys[0] + y / sc, ys[0] + (y + 1) / sc
        acc = n = 0
        for yy in range(int(sy0), max(int(sy0) + 1, int(math.ceil(sy1)))):
            for xx in range(int(sx0), max(int(sx0) + 1, int(math.ceil(sx1)))):
                if yy < SH and xx < SW:
                    acc += big[yy][xx]
                    n += 1
        mask[offy + y][offx + x] = acc / n if n else 0.0


def dilate(m, r):
    offs = [(dx, dy) for dy in range(-r, r + 1) for dx in range(-r, r + 1) if dx * dx + dy * dy <= r * r]
    out = [[0.0] * W for _ in range(H)]
    for yy in range(H):
        row = m[yy]
        for xx in range(W):
            a = row[xx]
            if a <= 0.02:
                continue
            for dx, dy in offs:
                X, Y = xx + dx, yy + dy
                if 0 <= X < W and 0 <= Y < H and out[Y][X] < a:
                    out[Y][X] = a
    return out


R = max(5, int(th * 0.05))
outline = dilate(mask, R)
out = bytearray(W * H * 4)
for yy in range(H):
    for xx in range(W):
        sx, sy = xx - R * 2 // 3, yy - R
        a_sh = outline[sy][sx] * 0.6 if 0 <= sx < W and 0 <= sy < H else 0.0
        a = outline[yy][xx] + a_sh * (1 - outline[yy][xx])
        a_f = mask[yy][xx]
        shade = 1.0 - 0.22 * max(0.0, min(1.0, (yy - offy) / max(1, th)))
        col = [255 * shade * a_f] * 3
        a = a_f + a * (1 - a_f)
        o = 4 * (yy * W + xx)
        if a > 0:
            out[o:o + 4] = bytes([min(255, int(col[k] / a + 0.5)) for k in range(3)] + [min(255, int(a * 255 + 0.5))])
rw.write_png(sys.argv[2], W, H, bytes(out))
pv = bytearray(W * H * 4)
for i in range(W * H):
    a = out[4 * i + 3] / 255.0
    pv[4 * i:4 * i + 4] = bytes([int(out[4 * i + k] * a + 90 * (1 - a)) for k in range(3)] + [255])
rw.write_png(sys.argv[3], W, H, bytes(pv))
