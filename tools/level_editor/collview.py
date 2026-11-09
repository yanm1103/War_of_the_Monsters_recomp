"""The collision of a stage as the editor draws it (the Collision layer, web/coll.js): what the game collides with, read from the stage
image after the patch (tools/lvl.py). /api/collision answers with the shapes once per leaf and the paths that place them:

  {"leaves": {"<hex>": {"k": "hull"|"mesh", "v": [x, y, z, ...], "t": [i, j, k, ...], "e": [i, j, ...]}},
   "paths": [{"l": "<hex>" | "", "m": [12 numbers] | null, "min": [x, y, z], "max": [x, y, z], "trig": 0|1, "s": state, "k": kind}],
   "count": {...}}

A leaf's vertices are local (game axes: X right, Y forward, Z up); "m" is the path's chain without scale as p' = p * M for row vectors,
M = [m0 m1 m2; m3 m4 m5; m6 m7 m8; m9 m10 m11] (rows of the 4 x 4, its last column left out). "t" are triangles (a hull's faces are fanned),
"e" are the edges to draw when they are not the triangles' (a hull's: its faces' outlines). A path with "m" null is drawn as its box
(min, max in the world): a trigger volume, a path through an animated node, a hull with too many planes. "s" is the DESTRUCTIBLE state the
path belongs to (0: always there; the others collide only once the building breaks). "k" is the path's leaf: hull, mesh or trigger.
"""
import math

import lvl

MAX_PLANES = 40         # as lvl.leaf_points: plane triples grow as n^3
TOL = 0.05              # a vertex is on a plane within this many units


def _round(v, n=2):
    return round(float(v), n)


def hull_leaf(img, leaf):
    """{"k": "hull", v, t, e} for a convex hull (op 10), or None when it has too many planes or no vertices."""
    planes = lvl.hull_planes(img, leaf)
    if not planes or len(planes) > MAX_PLANES:
        return None
    cx, cy, cz = img.f32(leaf + 0x10), img.f32(leaf + 0x14), img.f32(leaf + 0x18)
    pos = sum(1 for a, b, c, d in planes if a * cx + b * cy + c * cz <= d + 1e-3)
    neg = sum(1 for a, b, c, d in planes if a * cx + b * cy + c * cz + d <= 1e-3)
    sign = 1 if pos >= neg else -1
    pl = [(a, b, c, d if sign > 0 else -d) for a, b, c, d in planes]
    corners = lvl._hull_corners(planes, sign)
    verts, seen = [], {}
    for p in corners:
        key = (round(p[0], 1), round(p[1], 1), round(p[2], 1))
        if key not in seen:
            seen[key] = len(verts)
            verts.append(p)
    if len(verts) < 4:
        return None
    tris, edges, done = [], set(), set()
    for a, b, c, d in pl:
        n = math.sqrt(a * a + b * b + c * c)
        if n < 1e-9:
            continue
        on = [i for i, p in enumerate(verts) if abs((a * p[0] + b * p[1] + c * p[2] - d) / n) <= TOL]
        if len(on) < 3 or tuple(on) in done:
            continue                    # a plane that is not a face (or the same face twice)
        done.add(tuple(on))
        nx, ny, nz = a / n, b / n, c / n
        ux, uy, uz = (0.0, 0.0, 1.0) if abs(nz) < 0.9 else (1.0, 0.0, 0.0)
        tx, ty, tz = ny * uz - nz * uy, nz * ux - nx * uz, nx * uy - ny * ux    # u x n: a vector in the plane
        tl = math.sqrt(tx * tx + ty * ty + tz * tz) or 1.0
        tx, ty, tz = tx / tl, ty / tl, tz / tl
        sx, sy, sz = ny * tz - nz * ty, nz * tx - nx * tz, nx * ty - ny * tx
        mx = sum(verts[i][0] for i in on) / len(on)
        my = sum(verts[i][1] for i in on) / len(on)
        mz = sum(verts[i][2] for i in on) / len(on)

        def ang(i):
            dx, dy, dz = verts[i][0] - mx, verts[i][1] - my, verts[i][2] - mz
            return math.atan2(dx * sx + dy * sy + dz * sz, dx * tx + dy * ty + dz * tz)
        ring = sorted(on, key=ang)
        for k in range(1, len(ring) - 1):
            tris += [ring[0], ring[k], ring[k + 1]]
        for k in range(len(ring)):
            i, j = ring[k], ring[(k + 1) % len(ring)]
            edges.add((min(i, j), max(i, j)))
    if not tris:
        return None
    return {"k": "hull", "v": [_round(x) for p in verts for x in p], "t": tris, "e": [i for e in sorted(edges) for i in e]}


def mesh_leaf(img, leaf):
    """{"k": "mesh", v, t} for a triangle group (op 42): the vertices its triangles use, renumbered, with the objects joined."""
    verts, tris = [], []
    for k in range(min(img.u32(leaf + 4), 256)):
        o = img.u32(leaf + 8 + 4 * k)
        vp, npol, nbox = img.u32(o), img.u16(o + 4), img.u16(o + 6)
        polys = o + 0x18 + 14 * nbox
        remap = {}
        for i in range(npol):
            for j in range(3):
                v = img.u8(polys + 8 * i + j)
                if v not in remap:
                    remap[v] = len(verts)
                    verts.append(tuple(img.f32(vp + 12 * v + 4 * q) for q in range(3)))
                tris.append(remap[v])
    if not tris:
        return None
    return {"k": "mesh", "v": [_round(x) for p in verts for x in p], "t": tris}


def collision_view(st):
    """The shapes of the stage `st` (a lvl.Stage, the patch applied, its grid read again)."""
    img, g = st.img, st.grid
    leaves, paths, cache = {}, [], {}
    count = {"hull": 0, "mesh": 0, "trigger": 0, "box": 0, "triangles": 0}
    for p in sorted(g.paths.values(), key=lambda q: q.addr):
        op = st.op(p.leaf)
        kind = "trigger" if p.trigger or op == 11 else "hull" if op == 10 else "mesh" if op == 42 else "other"
        entry = {"min": [_round(x, 1) for x in p.min], "max": [_round(x, 1) for x in p.max], "trig": int(p.trigger), "k": kind,
                 "l": "", "m": None, "s": 0}
        for i, n in enumerate(p.nodes[:-1]):        # the DESTRUCTIBLE state this path is in: its next node among the states
            if st.op(n) == 31:
                kids = lvl.children(img, n)
                nxt = p.nodes[i + 1]
                entry["s"] = kids.index(nxt) if nxt in kids else 0
                break
        animated = any(st.op(n) in (17, 25) for n in p.nodes)
        if kind in ("hull", "mesh") and not animated:
            if p.leaf not in cache:
                cache[p.leaf] = hull_leaf(img, p.leaf) if kind == "hull" else mesh_leaf(img, p.leaf)
            leaf = cache[p.leaf]
            if leaf:
                key = "%x" % p.leaf
                if key not in leaves:
                    leaves[key] = leaf
                    count["triangles"] += len(leaf["t"]) // 3
                m = lvl.chain_matrix(img, p.nodes[:-1], with_scale=False)
                entry["l"] = key
                entry["m"] = [_round(m[r][c], 4) for r in range(4) for c in range(3)]
        count[kind if kind in count else "box"] += 1
        if not entry["m"]:
            count["box"] += 1
        paths.append(entry)
    return {"leaves": leaves, "paths": paths, "count": count}
