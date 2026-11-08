"""Test: the editor's generated floor (editor.floor_prop, lvlprop.floor_glb): a prop over the grid's rectangle with a visible plate and a
coll_ slab of two triangles, placed at the rectangle's center.

Needs the game disc configured in the level editor (Settings). Writes only into a temporary folder.
Run: python tools/test_lvl_floor.py
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "level_editor"))
import editor   # noqa: E402
import lvl      # noqa: E402


def main():
    cfg = dict(editor.load_config())
    tmp = tempfile.mkdtemp(prefix="floortest_")
    try:
        cfg["mods"] = tmp
        os.makedirs(os.path.join(tmp, "M"))
        rect = {"mod": "M", "stage": "CENTRAL", "min": [-2700, -2600], "max": [2700, 2500]}
        out = editor.floor_prop(cfg, rect)
        assert out["model"] == "FL_CENTR" and out["pos"] == [0.0, -50.0, 0.0], out
        meta = out["prop"]
        assert meta and not meta.get("error"), meta
        folder = os.path.join(tmp, "M", "props", "FL_CENTR")
        assert os.path.isfile(os.path.join(folder, "source.glb"))
        # the same call again replaces it (the grid changed)
        rect["max"] = [3000, 2900]
        out = editor.floor_prop(cfg, rect)
        assert out["pos"] == [150.0, 150.0, 0.0] and out["prop"], out
        # a rectangle with no room for a floor, and one that is not four numbers, are refused
        for bad in ({"min": [0, 0], "max": [60, 60]}, {"min": None, "max": [1, 1]}):
            try:
                editor.floor_prop(cfg, dict(rect, **bad))
            except ValueError:
                continue
            raise AssertionError(bad)
        # the prop loads in the stage: a patch with it passes the editor's check
        patch = {"objects": [], "grid": {"min": rect["min"], "max": rect["max"]}, "walls": "off",
                 "props": [{"id": "chao", "model": "FL_CENTR", "pos": out["pos"]}]}
        chk = editor.check_patch(cfg["disc"], "CENTRAL", patch, os.path.join(tmp, "M"))
        assert chk["ok"], chk.get("problems")
        print("ok")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
