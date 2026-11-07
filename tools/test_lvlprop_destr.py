"""Test: a .glb with state1_ meshes becomes a destructible prop (tools/lvlprop.py, LevelPatch.apply_props): T|R -> INTERACTIVE_STATE ->
DESTRUCTIBLE -> one root per state, collision paths through them, an Interactives slot each.

Needs the game disc configured in the level editor (Settings). Writes only in a temporary folder.
Run: python tools/test_lvlprop_destr.py
"""
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "level_editor"))
import editor   # noqa: E402
import lvl      # noqa: E402
import lvlprop  # noqa: E402
from test_lvlprop_hull import make_glb   # noqa: E402

CUBE = [(-1, 0, -1), (1, 0, -1), (1, 2, -1), (-1, 2, -1), (-1, 0, 1), (1, 0, 1), (1, 2, 1), (-1, 2, 1)]
CIDX = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 6, 2, 3, 7, 6, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5]
STUMP = [(x * 0.5, y * 0.25, z * 0.5) for x, y, z in CUBE]


def main():
    cfg = editor.load_config()
    disc = editor.stage_root(cfg["disc"], "CENTRAL")
    with tempfile.TemporaryDirectory() as tmp:
        glb = os.path.join(tmp, "d.glb")
        # state 0: the cube with its automatic box; state 1: a low stump with a triangle collision
        make_glb(glb, None, None, extra=[("state1_stump", STUMP, CIDX), ("state1_coll_stump", STUMP, CIDX)])
        out = os.path.join(tmp, "DEST")
        meta = lvlprop.build(glb, out, "CENTRAL", "blender", None, None, "DEST", None, "auto", disc, log=lambda *_: None)
        assert meta["kind"] == "destructible" and len(meta["destructible"]["states"]) == 2, meta.get("destructible")
        prop = lvlprop.load_prop(out)
        assert prop.states[0] == prop.root and len(prop.states) == 2 and prop.strength == lvlprop.DEFAULT_STRENGTH
        res = lvlprop.check(prop, log=lambda *_: None)
        assert res["triangles"] == 24, res                       # both states' triangles
        assert abs(meta["bounds"]["max"][2] - 36) < 1e-3, meta["bounds"]      # the bounds are state 0's
        # applied: two instances, one turned
        mod = os.path.join(tmp, "mod")
        shutil.copytree(out, os.path.join(mod, "props", "DEST"))
        patch = {"objects": [], "props": [{"id": "a", "model": "DEST", "pos": [800.0, 900.0, 0.5]},
                                          {"id": "b", "model": "DEST", "pos": [700.0, 900.0, 0.5], "heading": 40}]}
        chk = editor.check_patch(cfg["disc"], "CENTRAL", patch, mod)
        assert chk["ok"], chk["problems"]
        st = lvl.Stage("CENTRAL", editor.stage_root(cfg["disc"], "CENTRAL"))
        lp = lvl.LevelPatch(st)
        before = lp.interactives_used
        lp.apply(patch)
        lp.apply_props(patch, mod)
        log = "\n".join(lp.log)
        assert "is destructible: 2 states" in log, log
        assert lp.interactives_used == before + 2, (before, lp.interactives_used)
        img = lp.img
        dnodes = [n for n in lp.st.parents if n >= lp.arena and img.u32(n) & 63 == 31]
        assert len(dnodes) == 2, dnodes
        for d in dnodes:
            assert img.u16(d + 0xA) == 2 and img.u16(d + 4) == lvlprop.DEFAULT_STRENGTH
            assert img.u16(d + 0xC + 8) == int(round(65535 * 1 / 2)) and img.u16(d + 0x18 + 8) == 0
            (ist,) = [n for n in lp.st.parents if n >= lp.arena and img.u32(n) & 63 == 39 and img.u32(n + 0xC) & lvl.MASK == d]
            assert img.u32(ist + 4) == 0
        # each instance has a path per state's leaf: [world, props, T|R, IST, DESTR, state root, ..., leaf]
        paths = [p for p in lp.redone.values() if p[2] == "prop"]
        assert len(paths) == 2 * 2, len(paths)         # the box of state 0 and the triangles of state 1, twice
        # a four-state prop and a missing middle state
        make_glb(glb, None, None, extra=[("state1_a", STUMP, CIDX), ("state2_a", STUMP, CIDX), ("state3_none", None, None)])
        meta = lvlprop.build(glb, out, "CENTRAL", "blender", None, None, "DEST", None, "auto", disc, strength=3, log=lambda *_: None)
        assert len(meta["destructible"]["states"]) == 4 and meta["destructible"]["class"] == 3
        lvlprop.check(lvlprop.load_prop(out), log=lambda *_: None)
        make_glb(glb, None, None, extra=[("state2_a", STUMP, CIDX)])
        try:
            lvlprop.build(glb, out, "CENTRAL", "blender", None, None, "DEST", None, "auto", disc, log=lambda *_: None)
        except lvlprop.PropError as e:
            assert "none missing" in str(e), e
        else:
            raise AssertionError("a missing state 1 was accepted")
    print("ok")


if __name__ == "__main__":
    main()
