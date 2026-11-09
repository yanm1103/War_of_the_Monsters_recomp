"""Test: the Collision layer's data (collview.py, editor.collision_patch): every shape placed by its path's matrix fills the path's world
box, hulls come out as faces and edges, a patch's moved object and a prop's hull_ and coll_ meshes show up in the answer.

Needs the game disc configured in the level editor (Settings). Writes only in a temporary folder.
Run: python tools/level_editor/test_collision.py   (prints ok)
"""
import contextlib
import io
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)
sys.path.insert(0, HERE)
import editor   # noqa: E402
import lvl      # noqa: E402
import lvlprop  # noqa: E402
from test_lvlprop_hull import make_glb   # noqa: E402


def world_box(res, p):
    """The box of a path's leaf taken to the world by its matrix (the test's own copy of the page's formula)."""
    m, v = p["m"], res["leaves"][p["l"]]["v"]
    pts = [(v[i] * m[0] + v[i + 1] * m[3] + v[i + 2] * m[6] + m[9], v[i] * m[1] + v[i + 1] * m[4] + v[i + 2] * m[7] + m[10],
            v[i] * m[2] + v[i + 1] * m[5] + v[i + 2] * m[8] + m[11]) for i in range(0, len(v), 3)]
    return [min(q[k] for q in pts) for k in range(3)], [max(q[k] for q in pts) for k in range(3)]


def off_box(res, only=None):
    """The largest distance between a path's world box and the box of its drawn shape (leaf vertices only: the same rule as the disc's)."""
    worst, n = 0.0, 0
    for p in res["paths"]:
        if not p["m"] or (only and p["l"] not in only):
            continue
        lo, hi = world_box(res, p)
        worst = max(worst, max(max(abs(lo[k] - p["min"][k]), abs(hi[k] - p["max"][k])) for k in range(3)))
        n += 1
    return worst, n


def main():
    cfg = editor.load_config()
    disc = cfg["disc"]
    plain = editor.collision_patch(disc, "CENTRAL", {})
    c = plain["count"]
    assert c["mesh"] > 100 and c["triangles"] > 1000 and len(plain["paths"]) == c["mesh"] + c["hull"] + c["trigger"] + c.get("other", 0), c
    worst, n = off_box(plain)
    assert n > 100 and worst < 2.0, (worst, n)       # the shapes sit where the game's boxes say (rounding to 0.1 and to 4 digits)
    assert any(p["s"] > 0 for p in plain["paths"]) and any(p["s"] == 0 for p in plain["paths"]), "destructible states"
    assert all(p["m"] is None for p in plain["paths"] if p["k"] == "trigger"), "a trigger is drawn as its box"
    for leaf in plain["leaves"].values():
        assert len(leaf["t"]) % 3 == 0 and max(leaf["t"]) < len(leaf["v"]) // 3
    json.dumps(plain)

    # a moved object: its paths move with it
    objs = editor_objects(disc)[:1]
    assert objs, "no object with collision to move"
    h = objs[0]["handle"]
    moved = editor.collision_patch(disc, "CENTRAL", {"objects": [{"handle": "0x%X" % h, "from": list(objs[0]["pos"]),
                                                               "pos": [objs[0]["pos"][0] + 600.0, objs[0]["pos"][1], objs[0]["pos"][2]]}]})
    boxes = lambda r: sorted((tuple(p["min"]), tuple(p["max"])) for p in r["paths"])    # noqa: E731
    assert boxes(moved) != boxes(plain), "moving an object left the collision where it was"
    worst, n = off_box(moved)
    assert worst < 2.0, worst

    # a scaled object: its shapes are the scaled leaves and still fill the boxes the patch put the paths in
    scaled = editor.collision_patch(disc, "CENTRAL", {"objects": [{"handle": "0x%X" % h, "from": list(objs[0]["pos"]), "scale": 2.0}]})
    assert boxes(scaled) != boxes(plain), "scaling an object left the collision as it was"
    worst, n = off_box(scaled)
    assert worst < 2.0, worst
    assert scaled["count"]["mesh"] == plain["count"]["mesh"] and len(scaled["leaves"]) > len(plain["leaves"]) - 1, (scaled["count"], plain["count"])

    # a hull_ prop (a pyramid) and a coll_ prop (a cube) placed with a turn
    with tempfile.TemporaryDirectory() as tmp:
        mod = os.path.join(tmp, "mod")
        root = editor.stage_root(disc, "CENTRAL")
        for name, kw in (("PYR", dict(hull=True)), ("WALL", dict(coll=True))):
            glb = os.path.join(tmp, name + ".glb")
            if kw.get("hull"):
                pts = [(-1, 0, -1), (1, 0, -1), (1, 0, 1), (-1, 0, 1), (0, 1, 0)]
                make_glb(glb, pts, [0, 1, 2, 0, 2, 3, 0, 4, 1, 1, 4, 2, 2, 4, 3, 3, 4, 0])
            else:
                cube = [(-1, 0, -1), (1, 0, -1), (1, 2, -1), (-1, 2, -1), (-1, 0, 1), (1, 0, 1), (1, 2, 1), (-1, 2, 1)]
                make_glb(glb, None, None, coll=(cube, [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 6, 2, 3, 7, 6, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5]))
            out = os.path.join(tmp, name)
            lvlprop.build(glb, out, "CENTRAL", "blender", None, None, name, None, "auto", root)
            shutil.copytree(out, os.path.join(mod, "props", name))
        patch = {"objects": [], "props": [{"id": "p1", "model": "PYR", "pos": [800.0, 900.0, 0.5], "heading": 30},
                                          {"id": "w1", "model": "WALL", "pos": [1200.0, 900.0, 0.5], "heading": -45}]}
        res = editor.collision_patch(disc, "CENTRAL", patch, mod)
        assert res["count"]["hull"] == plain["count"]["hull"] + 1 and res["count"]["mesh"] == plain["count"]["mesh"] + 1, res["count"]
        hulls = [p for p in res["paths"] if p["k"] == "hull"]
        assert len(hulls) == 1, hulls
        leaf = res["leaves"][hulls[0]["l"]]
        assert leaf["k"] == "hull" and len(leaf["v"]) // 3 == 5 and len(leaf["t"]) // 3 == 6, leaf     # 5 corners; the quad is 2 triangles
        assert len(leaf["e"]) // 2 == 8, leaf["e"]                                               # 4 base + 4 slanted edges
        lo, hi = world_box(res, hulls[0])
        assert abs(lo[0] + hi[0] - 1600.0) < 400 and hi[2] - lo[2] > 10, (lo, hi)
        worst, n = off_box(res)
        assert worst < 2.0, worst
    print("ok")


def editor_objects(disc):
    """The stage's objects whose handle has collision paths through it (the buildings), a TRANSLATE/ROTATE the patch can move."""
    with contextlib.redirect_stdout(io.StringIO()):
        st = lvl.Stage("CENTRAL", editor.stage_root(disc, "CENTRAL"))
        through = st.paths_through()
        return [o for o in st.objects() if o["kind"] == "destr" and o["handle"] and st.op(o["handle"]) in (3, 4)
                and any(through.get(x) for x in st.subtree(o["handle"]))]


if __name__ == "__main__":
    main()
