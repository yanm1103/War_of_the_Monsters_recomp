"""Test: a hull_ mesh in the .glb becomes a COLL_VOL of its planes (tools/lvlprop.py), not the automatic box.

Needs the game disc configured in the level editor (Settings). Writes only in a temporary folder.
Run: python tools/test_lvlprop_hull.py
"""
import json
import shutil
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "level_editor"))
import editor   # noqa: E402
import lvl      # noqa: E402
import lvlprop  # noqa: E402


def make_glb(path, hull_pts, hull_tris, yoff=0.0, coll=None):
    """Two meshes: `vis` (a 2 m cube, vertex color only) and `hull_pyr` (the given points and triangles); Blender meters, Y up."""
    cube = [(-1, 0, -1), (1, 0, -1), (1, 2, -1), (-1, 2, -1), (-1, 0, 1), (1, 0, 1), (1, 2, 1), (-1, 2, 1)]
    cube = [(x, y + yoff, z) for x, y, z in cube]
    cidx = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 6, 2, 3, 7, 6, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5]
    blobs, views, accs = b"", [], []

    def add(data, ctype, count, typ, target, mm=None):
        nonlocal blobs
        off = len(blobs)
        blobs += data + bytes((-len(data)) % 4)
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data), "target": target})
        a = {"bufferView": len(views) - 1, "componentType": ctype, "count": count, "type": typ}
        if mm:
            a["min"], a["max"] = mm
        accs.append(a)
        return len(accs) - 1

    def mesh(pts, idx):
        lo = [min(p[i] for p in pts) for i in range(3)]
        hi = [max(p[i] for p in pts) for i in range(3)]
        pa = add(struct.pack("<%df" % (3 * len(pts)), *[x for p in pts for x in p]), 5126, len(pts), "VEC3", 34962, (lo, hi))
        ia = add(struct.pack("<%dH" % len(idx), *idx), 5123, len(idx), "SCALAR", 34963)
        return {"primitives": [{"attributes": {"POSITION": pa}, "indices": ia}]}

    meshes = [mesh(cube, cidx)]
    nodes = [{"mesh": 0, "name": "vis"}]
    if hull_pts:
        meshes.append(mesh(hull_pts, hull_tris))
        nodes.append({"mesh": len(meshes) - 1, "name": "hull_pyr"})
    if coll:
        meshes.append(mesh(*coll))
        nodes.append({"mesh": len(meshes) - 1, "name": "coll_wall"})
    j = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}],
         "nodes": nodes, "meshes": meshes, "accessors": accs,
         "bufferViews": views, "buffers": [{"byteLength": len(blobs)}]}
    js = json.dumps(j).encode()
    js += b" " * ((-len(js)) % 4)
    total = 12 + 8 + len(js) + 8 + len(blobs)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total) + struct.pack("<I4s", len(js), b"JSON") + js
                + struct.pack("<I4s", len(blobs), b"BIN\0") + blobs)


def main():
    cfg = editor.load_config()
    # a pyramid (square base 2 m, 1 m tall): 5 faces
    pts = [(-1, 0, -1), (1, 0, -1), (1, 0, 1), (-1, 0, 1), (0, 1, 0)]
    tris = [0, 1, 2, 0, 2, 3, 0, 4, 1, 1, 4, 2, 2, 4, 3, 3, 4, 0]
    with tempfile.TemporaryDirectory() as tmp:
        glb = os.path.join(tmp, "p.glb")
        make_glb(glb, pts, tris)
        out = os.path.join(tmp, "PYR")
        lvlprop.build(glb, out, "CENTRAL", "blender", None, None, "PYR", None, "auto", editor.stage_root(cfg["disc"], "CENTRAL"))
        meta = json.load(open(os.path.join(out, "prop.json")))
        c = meta["collision"]
        assert c["auto"] == "mesh" and c["hull"] == 1 and c["hulls"][0]["planes"] == 5, c
        # the pyramid is 36 units (2 m) wide and 18 tall; the cube drawn is 36 x 36 x 36: the hull is smaller than the visual
        assert abs(c["hulls"][0]["max"][2] - 18) < 1e-3 and abs(meta["bounds"]["max"][2] - 36) < 1e-3, (c, meta["bounds"])
        prop = lvlprop.load_prop(out)
        res = lvlprop.check(prop, log=lambda *_: None)
        assert res["triangles"] == 12, res
        # placed in the stage: the patch applies, and the collision check (boxes, cells) finds nothing out of place
        mod = os.path.join(tmp, "mod")
        shutil.copytree(out, os.path.join(mod, "props", "PYR"))
        patch = {"objects": [], "props": [{"id": "p1", "model": "PYR", "pos": [800.0, 900.0, 0.5], "heading": 30}]}
        chk = editor.check_patch(cfg["disc"], "CENTRAL", patch, mod)
        assert chk["ok"], chk["problems"]
        # a flat hull is refused with a clear message
        make_glb(glb, pts[:4] + [(0, 0, 0)], tris)
        try:
            lvlprop.build(glb, out, "CENTRAL", "blender", None, None, "PYR", None, "auto", editor.stage_root(cfg["disc"], "CENTRAL"))
        except lvlprop.PropError as e:
            assert "flat" in str(e) or "volume" in str(e) or "at least 4" in str(e), e
        else:
            raise AssertionError("a flat hull_ mesh was accepted")
    # units game: a piece whose origin is at its middle (z -1..1) is lifted so its lowest point is at z 0
    with tempfile.TemporaryDirectory() as tmp:
        glb = os.path.join(tmp, "g.glb")
        make_glb(glb, pts, tris, yoff=-1.0)
        out = os.path.join(tmp, "MID")
        lvlprop.build(glb, out, "CENTRAL", "game", None, None, "MID", None, "auto", editor.stage_root(cfg["disc"], "CENTRAL"))
        meta = json.load(open(os.path.join(out, "prop.json")))
        assert abs(meta["bounds"]["min"][2]) < 1e-3 and abs(meta["bounds"]["max"][2] - 2) < 1e-3, meta["bounds"]
    # coll_ meshes: collision by triangles (a POLY_COLL leaf, no box tree); the cube's own faces stand in for a walkable shape
    with tempfile.TemporaryDirectory() as tmp:
        glb = os.path.join(tmp, "c.glb")
        cube = [(-1, 0, -1), (1, 0, -1), (1, 2, -1), (-1, 2, -1), (-1, 0, 1), (1, 0, 1), (1, 2, 1), (-1, 2, 1)]
        cidx = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 6, 2, 3, 7, 6, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5]
        make_glb(glb, None, None, coll=(cube, cidx))
        out = os.path.join(tmp, "WALL")
        lvlprop.build(glb, out, "CENTRAL", "blender", None, None, "WALL", None, "auto", editor.stage_root(cfg["disc"], "CENTRAL"))
        meta = json.load(open(os.path.join(out, "prop.json")))
        c = meta["collision"]
        assert c["auto"] == "mesh" and c["poly"] == 1 and c["tris"] == 12 and c["hull"] == 0, c
        res = lvlprop.check(lvlprop.load_prop(out), log=lambda *_: None)
        assert res["triangles"] == 12, res
        mod = os.path.join(tmp, "mod")
        shutil.copytree(out, os.path.join(mod, "props", "WALL"))
        patch = {"objects": [], "props": [{"id": "w1", "model": "WALL", "pos": [800.0, 900.0, 0.5], "heading": 30}]}
        chk = editor.check_patch(cfg["disc"], "CENTRAL", patch, mod)
        assert chk["ok"], chk["problems"]
        # a mesh over the triangle limit is refused
        big = [(i * 0.1, 0.0, 0.0) for i in range(200)]
        make_glb(glb, None, None, coll=(big, [k for i in range(70) for k in (i, i + 1, i + 2)]))
        try:
            lvlprop.build(glb, out, "CENTRAL", "blender", None, None, "WALL", None, "auto", editor.stage_root(cfg["disc"], "CENTRAL"))
        except lvlprop.PropError as e:
            assert "at most 64" in str(e), e
        else:
            raise AssertionError("a 70-triangle coll_ mesh was accepted")
    print("ok")


if __name__ == "__main__":
    main()
