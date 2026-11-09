#!/usr/bin/env python3
"""Terrain from a heightmap: a PNG of heights becomes a binary glTF for tools/lvlprop.py, a prop with hills and ramps.

  python tools/lvlterrain.py <heights.png> <out.glb> --size X Y --height H [--cells N] [--colors colors.png] [--brightness PERCENT]

The mesh is a grid of N cells along the longer side (at most MAX_CELLS in all: the collision has up to 16384 triangles), centered on
the prop's origin, Z from 0 (black) to H (white), the top of the image toward +Y. It comes twice: `terrain` (what is drawn: vertex
colors with the light baked in, DROP under the collision so that flat places do not fight with the stage's floor) and `coll_terrain`
(the same triangles as collision, what monsters walk on). The colors are a ramp from grass through earth to rock by height, or
the colors PNG laid over the same rectangle; either way shaded by the slope against a fixed sun and scaled by the brightness (percent,
default 80: the stages' own vertex colors are darker than a plain palette; a vertex color goes to twice the full brightness).
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lvlprop   # noqa: E402
import tex       # noqa: E402

U = lvlprop.UNITS_PER_METER
DROP = 0.6                                  # the drawn mesh sits this far under the collision (game units)
MAX_CELLS = 90                              # along the longer side: 90 x 90 cells are 16200 triangles, collision's 16384 limit is 128 x 64
SUN = (0.35, -0.4, 0.85)                    # toward the light, game axes (X, Y, Z up)
DEFAULT_BRIGHTNESS = 80                     # percent
SHADE_MIN = 0.45                            # the darkest a slope faces away from the sun
RAMP = ((0.0, (74, 122, 58)), (0.5, (132, 108, 72)), (1.0, (178, 176, 170)))     # height 0..1 -> sRGB


class TerrainError(Exception):
    pass


def read_image(data):
    """(w, h, [(r, g, b, a)]) of a PNG (8 or 16 bits, any color type, not interlaced)."""
    try:
        return tex.read_png(bytes(data))
    except (ValueError, KeyError, OSError) as e:
        raise TerrainError("the heightmap is not a PNG this tool reads (%s). Save it as an 8-bit grayscale or RGB PNG." % e)
    except Exception as e:                  # zlib.error and struct.error on a damaged file
        raise TerrainError("the PNG does not read (%s)" % e)


def heights_of(data):
    """(w, h, rows) with rows[y][x] in 0..1: the pixel's brightness (the mean of red, green and blue, so a gray map is exact)."""
    w, h, px = read_image(data)
    if w < 2 or h < 2:
        raise TerrainError("the heightmap is %d x %d pixels: it needs at least 2 x 2" % (w, h))
    return w, h, [[sum(px[y * w + x][:3]) / 765.0 for x in range(w)] for y in range(h)]


def sample(rows, w, h, u, v):
    """The bilinear value of a w x h grid at u, v in 0..1 (the pixels' centers are the corners of the cells)."""
    fx, fy = u * (w - 1), v * (h - 1)
    x0, y0 = min(int(fx), w - 2), min(int(fy), h - 2)
    tx, ty = fx - x0, fy - y0
    a = rows[y0][x0] * (1 - tx) + rows[y0][x0 + 1] * tx
    b = rows[y0 + 1][x0] * (1 - tx) + rows[y0 + 1][x0 + 1] * tx
    return a * (1 - ty) + b * ty


def grid_cells(size_x, size_y, cells):
    """(cells along X, cells along Y): `cells` along the longer side, the other in proportion (at least 1)."""
    if not 1 <= cells <= MAX_CELLS:
        raise TerrainError("resolution %s: 1 to %d cells along the longer side" % (cells, MAX_CELLS))
    if size_x >= size_y:
        return cells, max(1, int(round(cells * size_y / size_x)))
    return max(1, int(round(cells * size_x / size_y))), cells


def triangles(cx, cy):
    return cx * cy * 2


def _lin(c):
    c = max(0.0, min(1.0, c))
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _ramp(t):
    t = max(0.0, min(1.0, t))
    for (t0, c0), (t1, c1) in zip(RAMP, RAMP[1:]):
        if t <= t1:
            k = (t - t0) / (t1 - t0)
            return tuple((c0[i] * (1 - k) + c1[i] * k) / 255.0 for i in range(3))
    return tuple(c / 255.0 for c in RAMP[-1][1])


def terrain_glb(data, size_x, size_y, height, cells=64, colors=None, brightness=DEFAULT_BRIGHTNESS):
    """The binary glTF (Blender's axes and meters: build it with units blender and no scale). data: the heightmap PNG's bytes;
    colors: a PNG's bytes or None. Returns (glb bytes, info) with info = {cells: [cx, cy], triangles, size, height}."""
    size_x, size_y, height = float(size_x), float(size_y), float(height)
    if not 10 <= brightness <= 200:
        raise TerrainError("the brightness is %g%%: 10 to 200" % brightness)
    k = brightness / 100.0
    if size_x < 10 or size_y < 10 or size_x > 40000 or size_y > 40000:
        raise TerrainError("the size is %g x %g units: each side is 10 to 40000" % (size_x, size_y))
    if not 1 <= height <= 20000:
        raise TerrainError("the height is %g units: 1 to 20000" % height)
    cx, cy = grid_cells(size_x, size_y, int(cells))
    w, h, rows = heights_of(data)
    cmap = None
    if colors:
        cw, ch, cpx = read_image(colors)
        cmap = (cw, ch, [[tuple(cpx[y * cw + x][:3]) for x in range(cw)] for y in range(ch)])
    nx, ny = cx + 1, cy + 1
    z = [[sample(rows, w, h, i / cx, j / cy) * height for i in range(nx)] for j in range(ny)]          # game height at vertex (i, j)
    sx, sy = size_x / cx, size_y / cy

    def at(i, j):
        return (-size_x / 2 + i * sx, size_y / 2 - j * sy, z[j][i])        # game axes: the image's top row is +Y

    sun = math.sqrt(sum(c * c for c in SUN))
    sun = tuple(c / sun for c in SUN)
    pos, vis, nrm, col = [], [], [], []
    for j in range(ny):
        for i in range(nx):
            x, y, zz = at(i, j)
            dzx = (z[j][min(i + 1, cx)] - z[j][max(i - 1, 0)]) / ((min(i + 1, cx) - max(i - 1, 0)) * sx)
            dzy = (z[max(j - 1, 0)][i] - z[min(j + 1, cy)][i]) / ((min(j + 1, cy) - max(j - 1, 0)) * sy)       # rows go toward -Y
            n = (-dzx, -dzy, 1.0)
            ln = math.sqrt(sum(c * c for c in n))
            n = tuple(c / ln for c in n)
            shade = SHADE_MIN + (1 - SHADE_MIN) * max(0.0, sum(n[k] * sun[k] for k in range(3)))
            if cmap:
                base = tuple(c / 255.0 for c in _bilinear_rgb(cmap, i / cx, j / cy))
            else:
                base = _ramp(zz / height)
            pos.append((x / U, zz / U, -y / U))                                       # glTF: X, up, -Y
            vis.append((x / U, (zz - DROP) / U, -y / U))
            nrm.append((n[0], n[2], -n[1]))
            col.append(tuple(_lin(c) * shade * k for c in base))
    idx = []
    for j in range(cy):
        for i in range(cx):
            a, b, c, d = j * nx + i, j * nx + i + 1, (j + 1) * nx + i + 1, (j + 1) * nx + i
            idx += [a, c, b, a, d, c]                                                 # facing up, as lvlprop.floor_glb's
    blob, views, accs, meshes, nodes = b"", [], [], [], []

    def add(values, ctype, typ, target, bounds=False):
        nonlocal blob
        flat = [c for v in values for c in (v if isinstance(v, tuple) else (v,))]
        data_b = struct.pack("<%d%s" % (len(flat), "f" if ctype == 5126 else "H"), *flat)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data_b), "target": target})
        blob += data_b + bytes((-len(data_b)) % 4)
        acc = {"bufferView": len(views) - 1, "componentType": ctype, "count": len(values), "type": typ}
        if bounds:
            acc["min"] = [min(v[k] for v in values) for k in range(3)]
            acc["max"] = [max(v[k] for v in values) for k in range(3)]
        accs.append(acc)
        return len(accs) - 1
    iv = add(idx, 5123, "SCALAR", 34963)
    for name, points, extra in (("terrain", vis, True), ("coll_terrain", pos, False)):
        at_ = {"POSITION": add(points, 5126, "VEC3", 34962, True)}
        if extra:
            at_["NORMAL"] = add(nrm, 5126, "VEC3", 34962)
            at_["COLOR_0"] = add(col, 5126, "VEC3", 34962)
        meshes.append({"primitives": [{"attributes": at_, "indices": iv}]})
        nodes.append({"mesh": len(meshes) - 1, "name": name})
    js = json.dumps({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes,
                     "meshes": meshes, "accessors": accs, "bufferViews": views, "buffers": [{"byteLength": len(blob)}]}).encode()
    js += b" " * ((-len(js)) % 4)
    glb = (struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(js) + 8 + len(blob)) + struct.pack("<I4s", len(js), b"JSON") + js
           + struct.pack("<I4s", len(blob), b"BIN\0") + blob)
    return glb, {"cells": [cx, cy], "triangles": triangles(cx, cy), "size": [size_x, size_y], "height": height, "brightness": brightness}


def _bilinear_rgb(cmap, u, v):
    w, h, rows = cmap
    fx, fy = u * (w - 1), v * (h - 1)
    x0, y0 = min(int(fx), max(w - 2, 0)), min(int(fy), max(h - 2, 0))
    x1, y1 = min(x0 + 1, w - 1), min(y0 + 1, h - 1)
    tx, ty = fx - x0, fy - y0
    out = []
    for k in range(3):
        a = rows[y0][x0][k] * (1 - tx) + rows[y0][x1][k] * tx
        b = rows[y1][x0][k] * (1 - tx) + rows[y1][x1][k] * tx
        out.append(a * (1 - ty) + b * ty)
    return out


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    opts = {}
    for opt, n in (("--size", 2), ("--height", 1), ("--cells", 1), ("--colors", 1), ("--brightness", 1)):
        if opt in argv:
            k = argv.index(opt)
            opts[opt[2:]] = argv[k + 1:k + 1 + n]
            for v in argv[k + 1:k + 1 + n]:
                if v in args:
                    args.remove(v)
    if len(args) != 2 or "size" not in opts or "height" not in opts:
        print(__doc__.strip().splitlines()[0])
        print(__doc__.strip().splitlines()[2])
        return 2
    try:
        with open(args[0], "rb") as f:
            hm = f.read()
        cm = None
        if "colors" in opts:
            with open(opts["colors"][0], "rb") as f:
                cm = f.read()
        glb, info = terrain_glb(hm, float(opts["size"][0]), float(opts["size"][1]), float(opts["height"][0]),
                                int(opts.get("cells", ["64"])[0]), cm, float(opts.get("brightness", [DEFAULT_BRIGHTNESS])[0]))
    except (TerrainError, OSError, ValueError) as e:
        print("error:", e)
        return 1
    with open(args[1], "wb") as f:
        f.write(glb)
    print("%s: %d x %d cells, %d triangles" % (args[1], info["cells"][0], info["cells"][1], info["triangles"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
