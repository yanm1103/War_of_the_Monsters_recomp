#!/usr/bin/env python3
"""A stage as a binary glTF for the level editor: one node per lvl.py object, named and tagged by its handle.

  python tools/lvlglb.py CENTRAL out.glb [--disc <disc>] [--lod N]

Scene: each object is a top-level node "0xB18310 destr" whose matrix is the handle's world matrix (extras: kind, handle, from,
heading, id, states, on); its meshes hang under "state0" (intact) and "state1"... (broken states, extras.hidden). An object
nested in another (a sign on a building) stays inside its owner's state. Everything without a handle hangs under "rootN"
(extras: root, ident; ident 1 is the world, 2 the sky). Meshes are shared between instances. The .glb is derived from the
user's disc: it goes to a cache, never to the repo.
"""
import collections
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gltf  # noqa: E402
import lvl   # noqa: E402
import ngp   # noqa: E402
import tex   # noqa: E402

# game (x, y, z), Z up -> glTF (x, z, -y), Y up; row vectors: p_gl = p_game * C
C = [[1, 0, 0, 0], [0, 0, -1, 0], [0, 1, 0, 0], [0, 0, 0, 1]]
CI = [[1, 0, 0, 0], [0, 0, 1, 0], [0, -1, 0, 0], [0, 0, 0, 1]]


def gl_matrix(m):
    n = ngp.mul(ngp.mul(CI, m), C)
    return [float(n[r][c]) for r in range(4) for c in range(4)]


def export(st, objs, disc, out, lod=99, on=None):
    """Write the stage `st` (lvl.Stage) with its objects `objs` (st.objects()) to `out`. Returns a Counter of what went in."""
    img, name = st.img, st.name
    on = on or {}
    by_handle = {o["handle"] or o["node"]: o for o in objs}
    base = os.path.join(disc, "LVL", name)
    lib = tex.Library(base + ".TEX", base + ".RTX", base + ".NGP")
    g = gltf.Glb()
    g.j["extensionsUsed"] = ["KHR_materials_unlit"]
    stats = collections.Counter()
    material = gltf.Materials(g, img, lib, True, stats).get
    mesh_of, group_node, handle_node, seen = {}, {}, {}, set()

    def mesh(node):
        if node in mesh_of:
            return mesh_of[node]
        md = ngp.decode_mesh(img, node)
        stats["sprites left out"] += len(md.sprites)
        if md.skinned or not md.tris:
            mesh_of[node] = None
            return None
        groups = {}
        for k in range(0, len(md.tris), 3):
            groups.setdefault(md.tris[k + 2].slot, []).append(md.tris[k:k + 3])
        prims = []
        for slot, tris in sorted(groups.items()):
            verts, index, idx = {}, [], []
            # As = Av * At / 0x80 (alfa da textura ate 0xFF): o glTF so tem a textura ate 1,0, entao o ganho vai para o vertice
            gain = 1.0
            if md.abe and slot != ngp.SLOT_NOTEX:
                s = slot if slot < min(img.u16(node + 0x5A), 5) else 0
                gain = lib.alpha_max(img.u16(node + 0x50 + 2 * s),
                                     img.u32(node + 0x70 + 0x30 * s) | (img.u32(node + 0x74 + 0x30 * s) << 32)) / 128.0
            for tri in tris:
                for v in tri:
                    p = gltf.to_y_up((v.x, v.y, v.z))
                    ln = (v.nx * v.nx + v.ny * v.ny + v.nz * v.nz) ** 0.5
                    nn = gltf.to_y_up((v.nx / ln, v.ny / ln, v.nz / ln)) if ln > 1e-6 else (0.0, 1.0, 0.0)
                    a = min(1.0, v.a * gain / 128.0) if md.abe else 1.0
                    c = (min(1.0, v.r / 128.0), min(1.0, v.g / 128.0), min(1.0, v.b / 128.0), 1.0 if a >= 0.9 else a)
                    k = (p, nn, (v.u, v.v), c)
                    if k not in verts:
                        verts[k] = len(verts)
                        index.append(k)
                    idx.append(verts[k])
            attrs = {"POSITION": g.accessor("f", 3, [k[0] for k in index], minmax=True),
                     "NORMAL": g.accessor("f", 3, [k[1] for k in index]),
                     "COLOR_0": g.accessor("f", 4, [k[3] for k in index])}
            if slot != ngp.SLOT_NOTEX:
                attrs["TEXCOORD_0"] = g.accessor("f", 2, [k[2] for k in index])
            ctype, fmt = (5123, "H") if len(index) < 65536 else (5125, "I")
            # alfa de vertice >= 1 em tudo: opaco (o three.js nao grava Z nos BLEND e o resto da fase aparece atras)
            blend = md.abe and any(k[3][3] < 1.0 for k in index)
            prims.append({"attributes": attrs, "indices": g.accessor(fmt, 1, idx, ctype=ctype, target=34963),
                          "material": material(node, slot, blend)})
            stats["triangles"] += len(tris)
        g.j["meshes"].append({"name": "obj_%x" % node, "primitives": prims})
        mesh_of[node] = len(g.j["meshes"]) - 1
        return mesh_of[node]

    def new_node(d, parent=None):
        g.j["nodes"].append(d)
        i = len(g.j["nodes"]) - 1
        if parent is None:
            g.j["scenes"][0]["nodes"].append(i)
        else:
            g.j["nodes"][parent].setdefault("children", []).append(i)
        return i

    def owner_node(owner, state):
        key = (owner, state)
        if key not in group_node:
            group_node[key] = new_node({"name": "state%d" % state, **({"extras": {"hidden": True}} if state else {})},
                                       handle_node[owner])
        return group_node[key]

    def open_handle(h, mm, parent, extras=None):
        if isinstance(h, str):
            d = {"name": h, "extras": extras or {}}
        else:
            o = by_handle[h]
            d = {"name": "0x%X %s" % (h, o["kind"]), "matrix": gl_matrix(mm),
                 "extras": {"kind": o["kind"], "handle": "0x%X" % h, "from": [round(x, 1) for x in o["pos"]],
                            "heading": round(o["heading"], 1), "id": o["id"], "states": o["states"],
                            "on": "0x%X" % on[h] if h in on else ""}}
        handle_node[h] = new_node(d, parent)

    def walk(node, m, owner, state, depth=0):
        if depth > 64 or not img.ok(node, 4):
            return
        key = (node, owner, state, tuple(map(tuple, m)))
        if key in seen:
            return
        seen.add(key)
        op = img.u32(node) & 63

        def kids(arr, n, mm):
            for k in range(min(n, 4096)):
                go(img.u32(arr + 4 * k), mm)

        def go(child, mm):
            child &= lvl.MASK
            if child in by_handle and child not in handle_node:
                par = owner_node(owner, state) if not isinstance(owner, str) else None
                lm = lvl.local_matrix(img, child)
                open_handle(child, ngp.mul(lm, mm) if lm else mm, par)     # nested: relative to the owner's node
                walk_handle(child)
            elif child not in by_handle:
                walk(child, mm, owner, state, depth + 1)

        if op == 0:
            mi = mesh(node)
            if mi is not None:
                new_node({"mesh": mi, "matrix": gl_matrix(m)}, owner_node(owner, state))
                stats["instances"] += 1
        elif op == 1:
            if img.u16(node + 0xA) & 1:
                kids(node + 0x20, img.u16(node + 8), m)
        elif op == 3:
            kids(node + 0x1C, img.u32(node + 8), ngp.mul(ngp.translate_m((img.f32(node + 0x10), img.f32(node + 0x14),
                                                                          img.f32(node + 0x18))), m))
        elif op == 4:
            rm = [[img.f32(node + 0x10 + 16 * r + 4 * c) for c in range(4)] for r in range(4)]
            kids(node + 0x50, img.u32(node + 8), ngp.mul(rm, m))
        elif op == 30:
            t = (img.f32(node + 0x10), img.f32(node + 0x14), img.f32(node + 0x18))
            s = (img.f32(node + 0x20), img.f32(node + 0x24), img.f32(node + 0x28))
            kids(node + 0x2C, img.u32(node + 8), ngp.mul(ngp.mul(ngp.scale_m(s), ngp.translate_m(t)), m))
        elif op == 6:       # SWITCH: the child the game starts with
            which, n = img.u8(node + 8), img.u8(node + 0xB)
            if which < n:
                go(img.u32(node + 0xC + 4 * which), m)
        elif op == 2:       # LOD: the most detailed by default
            n = img.u32(node + 4)
            if 0 < n < 16:
                go(img.u32(node + 0x20 + 16 * min(lod, n - 1) + 8), m)
        elif op in (8, 23):
            go(img.u32(node + 4), m)
        elif op == 35:
            kids(node + 0xC, img.u32(node + 4), m)
        elif op == 25:
            kids(node + 0x2C, img.u32(node + 0x28), m)
        elif op == 17:
            kids(node + 0xCC, img.u32(node + 4), m)
        elif op == 38:
            sx, sy, sz = img.f32(node + 0x20), img.f32(node + 0x24), img.f32(node + 0x28)
            if sx or sy or sz:
                pre = ngp.translate_m((img.f32(node + 0x40), img.f32(node + 0x44), img.f32(node + 0x48)))
                post = ngp.translate_m((img.f32(node + 0x30) + img.f32(node + 0x50), img.f32(node + 0x34) + img.f32(node + 0x54),
                                        img.f32(node + 0x38) + img.f32(node + 0x58)))
                kids(node + 0x60, img.u32(node + 4), ngp.mul(ngp.mul(ngp.mul(pre, ngp.scale_m((sx, sy, sz))), post), m))
        elif op == 39:
            go(img.u32(node + 0xC), m)
        elif op == 31:      # DESTRUCTIBLE: every state, each in its owner's "stateN"
            for k in range(min(img.u16(node + 0xA), 8)):
                walk(img.u32(node + 0xC + 12 * k) & lvl.MASK, m, owner, k if not isinstance(owner, str) else state, depth + 1)

    def walk_handle(h):
        op = img.u32(h) & 63
        arr, n = {3: (h + 0x1C, img.u32(h + 8)), 4: (h + 0x50, img.u32(h + 8)), 30: (h + 0x2C, img.u32(h + 8))}.get(op, (0, 0))
        if not arr:
            walk(h, ngp.ident(), h, 0, 1)
            return
        for k in range(min(n, 4096)):
            walk(img.u32(arr + 4 * k) & lvl.MASK, ngp.ident(), h, 0, 1)

    for ri, r in enumerate(st.roots):
        label = "root%d" % ri
        open_handle(label, None, None, {"root": ri, "ident": st.ident(r)})
        if r not in by_handle:
            walk(r, ngp.ident(), label, 0)
    g.write(out)
    stats["objects"] = sum(1 for h in handle_node if not isinstance(h, str))
    stats["meshes"] = len(g.j["meshes"])
    return stats


def main(argv):
    disc = os.path.join(lvl.REPO, "disc")
    if "--disc" in argv:
        i = argv.index("--disc")
        disc = argv[i + 1]
        del argv[i:i + 2]
    lod = 99
    if "--lod" in argv:
        i = argv.index("--lod")
        lod = int(argv[i + 1])
        del argv[i:i + 2]
    if len(argv) != 2:
        print(__doc__)
        return 1
    t0 = time.time()
    st = lvl.Stage(argv[0].upper(), disc)
    objs = st.objects()
    stats = export(st, objs, disc, argv[1], lod, st.on_top(objs))
    print("%s -> %s: %.1f MB in %.1f s; %s" % (st.name, argv[1], os.path.getsize(argv[1]) / 1e6, time.time() - t0,
                                             ", ".join("%s %d" % kv for kv in sorted(stats.items()))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
