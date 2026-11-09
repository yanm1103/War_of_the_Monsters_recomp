"""Test: the editor's start points (.SPT): the codec in tools/lvl.py (read_spt, build_spt) and editor.stage_spt / save_spt for a game
stage and for a custom stage. Needs the game disc configured in the editor; writes only into a temporary mods folder.
Run: python tools/level_editor/test_spt.py
"""
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE)))
sys.path.insert(0, HERE)
import editor   # noqa: E402
import lvl      # noqa: E402


def main():
    cfg = dict(editor.load_config())
    # the codec: the disc's own file reads and writes back byte for byte (w and the 0x3C byte are kept)
    disc = os.path.join(editor.stage_root(cfg["disc"], "CENTRAL"), "PTS", "CENTRAL.SPT")
    pts = lvl.read_spt(disc)
    assert len(pts) == 11 and {p["list"] for p in pts} <= {0, 1, 2, 3}, pts
    assert lvl.build_spt(pts) == open(disc, "rb").read(), "round trip"
    # a heading keeps 1.41 degrees, and -90 and 270 are the same byte
    p = lvl.build_spt([{"list": 2, "pos": [1.0, 2.0, 3.0], "heading": -90.0, "snap": True}])
    assert p[8 + 0x2D] == round(270 * 255 / 360) and p[8 + 0x2E] == 1 and p[8 + 0x2C] == 2
    assert abs(lvl.spt_heading(p[8 + 0x2D]) + 90.0) < 1.5
    for bad in ({"list": 4, "pos": [0, 0, 0]}, {"list": 0, "pos": [0, 0]}, {"list": 0, "pos": [0, 0, float("nan")]},
                {"list": True, "pos": [0, 0, 0]}):
        try:
            lvl.build_spt([bad])
        except ValueError:
            continue
        raise AssertionError("accepted %r" % (bad,))
    try:
        lvl.build_spt([{"list": 0, "pos": [0, 0, 0]}] * 256)
    except ValueError:
        pass
    else:
        raise AssertionError("256 points accepted")
    tmp = tempfile.mkdtemp(prefix="spttest_")
    try:
        cfg["mods"] = tmp
        os.makedirs(os.path.join(tmp, "M"))
        # a game stage with no file in the mod: the disc's points; saving makes mods/M/PTS/CENTRAL.SPT; reset takes it away
        r = editor.stage_spt(cfg, "M", "CENTRAL")
        assert r["source"] == "disc" and len(r["points"]) == 11 and r["caps"] == [1, 2, 20, 20], r
        mine = [{"list": 0, "pos": [10.0, 20.0, 30.0], "heading": 45.0, "snap": False},
                {"list": 1, "pos": [0.0, 0.0, 5.0], "heading": 0.0, "snap": True},
                {"list": 1, "pos": [50.0, 0.0, 5.0], "heading": 180.0, "snap": True},
                {"list": 1, "pos": [90.0, 0.0, 5.0], "heading": 0.0, "snap": True}]
        out = editor.save_spt(cfg, {"mod": "M", "name": "central", "points": mine})
        f = os.path.join(tmp, "M", "PTS", "CENTRAL.SPT")
        assert os.path.isfile(f) and out["source"] == "mod" and len(out["points"]) == 4, out
        assert len(out["warnings"]) == 1 and "List 1" in out["warnings"][0], out["warnings"]     # 3 points, room for 2
        assert abs(out["points"][0]["heading"] - 45.0) < 1.5 and out["points"][1]["snap"] is True
        out = editor.save_spt(cfg, {"mod": "M", "name": "CENTRAL", "reset": True})
        assert not os.path.exists(f) and out["source"] == "disc", out
        # no mod chosen on a game stage: reading gives the disc's, writing is refused
        assert editor.stage_spt(cfg, "", "CENTRAL")["source"] == "disc"
        try:
            editor.save_spt(cfg, {"mod": "", "name": "CENTRAL", "points": mine})
        except ValueError:
            pass
        else:
            raise AssertionError("saved with no mod")
        # a custom stage keeps its points in its own folder, whatever mod the editor has chosen
        sd = os.path.join(tmp, "M", "stages", "mycity")
        os.makedirs(sd)
        json.dump({"id": "mycity", "name": "MY CITY", "base": "CENTRAL"}, open(os.path.join(sd, "stage.json"), "w"))
        r = editor.stage_spt(cfg, "", "MYCITY")
        assert r["source"] == "disc" and len(r["points"]) == 11, r
        out = editor.save_spt(cfg, {"mod": "", "name": "MYCITY", "points": mine[:2]})
        assert os.path.isfile(os.path.join(sd, "MYCITY.SPT")) and out["source"] == "mod" and len(out["points"]) == 2, out
        assert not os.path.exists(os.path.join(tmp, "M", "PTS")), "a custom stage wrote into the mod's PTS"
        # the editor's pedestrian panel and check read the same file through points_root
        root = editor.points_root(cfg, "MYCITY")
        assert len(lvl.read_spt(os.path.join(root, "PTS", "CENTRAL.SPT"))) == 2
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("ok")


if __name__ == "__main__":
    main()
