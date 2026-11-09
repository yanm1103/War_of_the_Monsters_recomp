"""Test: terrain from a heightmap (lvlterrain.terrain_glb, editor.terrain_prop): the mesh's heights, its collision, the prop built for a
stage, the files kept in its folder, the refusals, and the prop passing the editor's check.

Needs the game disc configured in the level editor (Settings). Writes only into a temporary folder.
Run: python tools/test_lvl_terrain.py
"""
import base64
import math
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "level_editor"))
import editor      # noqa: E402
import lvlprop     # noqa: E402
import lvlterrain  # noqa: E402
import tex         # noqa: E402


def png(w, h, fn):
    """A PNG of w x h pixels, fn(x, y) -> (r, g, b)."""
    path = os.path.join(tempfile.gettempdir(), "terrain_test.png")
    tex.write_png(path, w, h, [tuple(fn(x, y)) + (255,) for y in range(h) for x in range(w)])
    with open(path, "rb") as f:
        data = f.read()
    os.remove(path)
    return data


def main():
    # a hill: white in the middle, black at the edges (a pyramid by distance), 33 x 33 pixels
    def hill(x, y):
        d = max(abs(x - 16), abs(y - 16)) / 16.0
        v = int(round(255 * (1 - d)))
        return (v, v, v)
    hm = png(33, 33, hill)
    glb, info = lvlterrain.terrain_glb(hm, 2000, 1000, 300, cells=40)
    assert info["cells"] == [40, 20] and info["triangles"] == 1600, info
    # the file as the prop builder reads it: a drawn mesh and a collision mesh of the same triangles
    path = os.path.join(tempfile.gettempdir(), "terrain_test.glb")
    with open(path, "wb") as f:
        f.write(glb)
    try:
        by_key, scale, stats = lvlprop.read_glb(path, "blender", warn=lambda *a: None)
    finally:
        os.remove(path)
    assert stats["triangles"] == 1600 and len(stats["colls"]) == 1, stats["triangles"]
    name, verts, tris, mat = stats["colls"][0]
    assert len(tris) == 1600 and len(verts) == 41 * 21, (len(tris), len(verts))
    zs = [v[2] for v in verts]
    assert abs(max(zs) - 300) < 0.5 and abs(min(zs)) < 0.01, (min(zs), max(zs))
    xs, ys = [v[0] for v in verts], [v[1] for v in verts]
    assert abs(min(xs) + 1000) < 0.5 and abs(max(xs) - 1000) < 0.5 and abs(min(ys) + 500) < 0.5 and abs(max(ys) - 500) < 0.5
    # every collision triangle faces up (a monster walks on it), and the drawn mesh sits just under the collision
    for a, b, c in tris:
        pa, pb, pc = verts[a], verts[b], verts[c]
        n = ((pb[0] - pa[0]) * (pc[1] - pa[1]) - (pb[1] - pa[1]) * (pc[0] - pa[0]))
        assert n > 0, (pa, pb, pc)
    drawn = [c[0][2] for tl in by_key.values() for t in tl for c in t]
    assert abs(max(drawn) - (300 - lvlterrain.DROP)) < 0.5, max(drawn)
    # the hill's top is at the middle of the stage's axes, the corners are the lowest
    top = max(verts, key=lambda v: v[2])
    assert abs(top[0]) < 60 and abs(top[1]) < 60, top
    # the colors PNG replaces the ramp: red on the left half, blue on the right
    cm = png(2, 2, lambda x, y: (255, 0, 0) if x == 0 else (0, 0, 255))
    glb2, _ = lvlterrain.terrain_glb(hm, 1000, 1000, 100, cells=10, colors=cm)
    assert glb2 != lvlterrain.terrain_glb(hm, 1000, 1000, 100, cells=10)[0]
    # the brightness scales the vertex colors (100 is a quarter again over 80), and 10..200 is the range
    def first_color(g):
        p = os.path.join(tempfile.gettempdir(), "terrain_test2.glb")
        with open(p, "wb") as f:
            f.write(g)
        try:
            return [c[3] for tl in lvlprop.read_glb(p, "blender", warn=lambda *a: None)[0].values() for t in tl for c in t][:3]
        finally:
            os.remove(p)
    c80 = first_color(lvlterrain.terrain_glb(hm, 1000, 1000, 100, cells=10)[0])
    c100 = first_color(lvlterrain.terrain_glb(hm, 1000, 1000, 100, cells=10, brightness=100)[0])
    assert c80 and c100 and c100[0][0] > c80[0][0] and c100[0][1] > c80[0][1], (c80, c100)
    # refusals
    for args in ((b"not a png", 1000, 1000, 100, 10), (hm, 5, 1000, 100, 10), (hm, 1000, 1000, 0, 10), (hm, 1000, 1000, 100, 0),
                 (hm, 1000, 1000, 100, 91), (hm, 1000, 1000, 100, 10, None, 5), (hm, 1000, 1000, 100, 10, None, 300), (png(1, 5, lambda x, y: (0, 0, 0)), 1000, 1000, 100, 10)):
        try:
            lvlterrain.terrain_glb(*args)
        except lvlterrain.TerrainError:
            continue
        raise AssertionError(args[1:])
    # 90 x 90 cells are the most the collision takes
    g90, i90 = lvlterrain.terrain_glb(hm, 1000, 1000, 100, cells=90)
    assert i90["triangles"] == 16200 <= lvlprop.MAX_COLL_TRIS

    # the editor's call: the prop is built for a stage, and the files stay in its folder
    cfg = dict(editor.load_config())
    tmp = tempfile.mkdtemp(prefix="terraintest_")
    try:
        cfg["mods"] = tmp
        os.makedirs(os.path.join(tmp, "M"))
        body = {"mod": "M", "stage": "CENTRAL", "name": "hills", "size": [2000, 2000], "height": 250, "cells": 48,
                "heights": base64.b64encode(hm).decode()}
        out = editor.terrain_prop(cfg, body)
        meta = out["prop"]
        assert meta and not meta.get("error") and meta["model"] == "HILLS", meta
        assert out["terrain"]["triangles"] == 48 * 48 * 2
        folder = os.path.join(tmp, "M", "props", "HILLS")
        for f in ("source.glb", "heightmap.png", "terrain.json"):
            assert os.path.isfile(os.path.join(folder, f)), f
        assert not os.path.exists(os.path.join(folder, "colors.png"))
        b = meta["bounds"]
        assert abs(b["max"][2] - 250) < 1 and abs(b["max"][0] - 1000) < 1, b
        # again: it exists; with replace and a colors PNG it is built again
        assert editor.terrain_prop(cfg, dict(body)).get("exists")
        out = editor.terrain_prop(cfg, dict(body, replace=True, colors=base64.b64encode(cm).decode(), height=100))
        assert os.path.isfile(os.path.join(folder, "colors.png")) and out["prop"]["bounds"]["max"][2] < 101
        # a bad number is a message, not a crash
        for bad in ({"size": [2000]}, {"height": "tall"}, {"heights": ""}, {"cells": 500}):
            try:
                editor.terrain_prop(cfg, dict(body, replace=True, **bad))
            except ValueError:
                continue
            raise AssertionError(bad)
        # the prop loads in the stage: a patch with it passes the editor's check
        patch = {"objects": [], "props": [{"id": "h1", "model": "HILLS", "pos": [0, 0, 0]}]}
        chk = editor.check_patch(cfg["disc"], "CENTRAL", patch, os.path.join(tmp, "M"))
        assert chk["ok"], chk.get("problems")
        print("ok")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
