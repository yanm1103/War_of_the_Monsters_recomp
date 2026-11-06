"""Software render of a Collada through retarget.Model (front and side views, textured) -> PNG.
  python preview.py <model.dae> <out.png> [size]
"""
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, r"C:\Users\TwistZero\WoTM\tools")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retarget  # noqa: E402
import rw  # noqa: E402


def read_png(path):
    b = open(path, "rb").read()
    p = 8
    idat = b""
    while p < len(b):
        n, = struct.unpack_from(">I", b, p)
        t = b[p + 4:p + 8]
        d = b[p + 8:p + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype = struct.unpack_from(">IIBB", d)
        elif t == b"IDAT":
            idat += d
        p += 12 + n
    bpp = {6: 4, 2: 3}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    o = 0
    for y in range(h):
        f = raw[o]
        line = bytearray(raw[o + 1:o + 1 + stride])
        o += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            up = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + up) & 255
            elif f == 3:
                line[i] = (line[i] + (a + up) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(up - c), abs(a - c), abs(a + up - 2 * c)
                pr = a if pa <= pb and pa <= pc else up if pb <= pc else c
                line[i] = (line[i] + pr) & 255
        for x in range(w):
            px = line[x * bpp:x * bpp + bpp]
            out[4 * (y * w + x):4 * (y * w + x) + 4] = bytes(px[:3]) + (bytes(px[3:4]) if bpp == 4 else b"\xff")
        prev = line
    return w, h, out


def render(tris, view, S, texcache):
    """view: 'front' looks from +Y toward -Y (x right... mirrored), 'side' from +X."""
    if os.environ.get("HEAD"):
        tris = [t for t in tris if min(c[0][2] for c in t[1]) > 0.5]
    pts = [c[0] for _img, cs in tris for c in cs]
    zmin = min(p[2] for p in pts)
    zmax = max(p[2] for p in pts)
    span = zmax - zmin
    sc = S * 0.9 / span
    img = bytearray([40, 40, 48] * S * S)
    zb = [-1e9] * (S * S)
    L = retarget._unit([0.3, 0.8, 0.5])
    for path, cs in tris:
        tex = texcache.get(path)
        if tex is None and path:
            tex = texcache[path] = read_png(path)
        P = []
        for pos, nrm, uv, _w in cs:
            if view == "front":
                sx, d = -pos[0], pos[1]
            else:
                sx, d = pos[1], pos[0]
            P.append((S / 2 + sx * sc, S * 0.95 - (pos[2] - zmin) * sc, d, uv, nrm))
        n = retarget._unit(retarget._cross([a - b for a, b in zip(cs[1][0], cs[0][0])], [a - b for a, b in zip(cs[2][0], cs[0][0])]))
        lum = 0.45 + 0.55 * abs(sum(a * b for a, b in zip(n, L)))
        xs = [p[0] for p in P]
        ys = [p[1] for p in P]
        x0, x1 = max(0, int(min(xs))), min(S - 1, int(max(xs)) + 1)
        y0, y1 = max(0, int(min(ys))), min(S - 1, int(max(ys)) + 1)
        (ax, ay, *_a), (bx, by, *_b), (cx, cy, *_c) = P
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9:
            continue
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                w0 = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / den
                w1 = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / den
                w2 = 1 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                d = w0 * P[0][2] + w1 * P[1][2] + w2 * P[2][2]
                k = y * S + x
                if d <= zb[k]:
                    continue
                zb[k] = d
                if tex:
                    u = w0 * P[0][3][0] + w1 * P[1][3][0] + w2 * P[2][3][0]
                    v = w0 * P[0][3][1] + w1 * P[1][3][1] + w2 * P[2][3][1]
                    tw, th, td = tex
                    tx = int(u * tw) % tw
                    ty = int(v * th) % th
                    col = td[4 * (ty * tw + tx):4 * (ty * tw + tx) + 3]
                else:
                    col = b"\xc0\xc0\xc0"
                img[3 * k:3 * k + 3] = bytes(min(255, int(c * lum)) for c in col)
    return img


def main():
    m = retarget.Model(sys.argv[1])
    S = int(sys.argv[3]) if len(sys.argv) > 3 else 400
    cache = {}
    a = render(m.tris, "front", S, cache)
    b = render(m.tris, "side", S, cache)
    rgba = bytearray()
    for y in range(S):
        for img in (a, b):
            for x in range(S):
                rgba += img[3 * (y * S + x):3 * (y * S + x) + 3] + b"\xff"
    rw.write_png(sys.argv[2], 2 * S, S, bytes(rgba))
    zs = [c[0][2] for _i, cs in m.tris for c in cs]
    print("tris", len(m.tris), "joints", len(m.joints), "height %.3f" % (max(zs) - min(zs)))


if __name__ == "__main__":
    main()
