"""Test: the patch key "clear": "scenery" (lvl.py LevelPatch.apply_clear) on the nine stages a custom stage can be based on: the bare
scenery's drawing and collision go, what has a handle, what is animated, the sky and the camera triggers stay.

Needs the game disc (configured in the level editor). Reads only; writes nothing.
Run: python tools/test_lvl_clear.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "level_editor"))
import editor   # noqa: E402
import lvl      # noqa: E402

BASES = ("CENTRAL", "VEGAS", "CANYON2", "METRO", "UFO", "CAPITOL", "CANYON1", "MINISF", "TOKYODM")


def main():
    disc = editor.load_config()["disc"]
    for name in BASES:
        st = lvl.Stage(name, disc)
        lp = lvl.LevelPatch(st, False)
        slots, paths = lp.scenery_plan()
        before = len(lp.cells)
        assert slots, name
        lp.apply_clear("scenery")
        assert lp.stats["scenery cleared"] == len(slots) and lp.stats["scenery paths removed"] == len(paths), (name, lp.stats)
        # what is left drawn without a handle: no OBJECT or emitter of the plan is still reachable through a bare chain
        assert not lp.scenery_plan()[0], name
        st.grid = lvl.Grid(st.img, st.grid.addr)
        left = {p.leaf: lp.op(p.leaf) for p in st.grid.paths.values()}
        covered = sum(1 for p in st.grid.paths.values()
                      if lp.op(p.leaf) in (10, 42) and any(lp.op(n) in lp.CLEAR_COVER for n in p.nodes))
        triggers = sum(1 for op in left.values() if op == 11)
        assert len(st.grid.paths) == before - len(paths), (name, len(st.grid.paths), before, len(paths))
        assert len(st.grid.paths) == covered + triggers + sum(1 for p in st.grid.paths.values() if lp.op(p.leaf) not in (10, 11, 42)), name
        assert lvl.check_collision(st), name        # the box and cell rules of what is left
        print("%-8s %5d OBJECTs/emitters, %3d of %3d collision paths out, %3d left (%d handled, %d triggers)" % (
            name, len(slots), len(paths), before, len(st.grid.paths), covered, triggers))
    # a patch with the key skipped for a bad value logs it and changes nothing else
    lp = lvl.LevelPatch(lvl.Stage("CENTRAL", disc), False)
    lp.apply({"clear": "everything"})
    assert any("clear skipped" in x for x in lp.log) and not lp.stats["scenery cleared"], lp.log
    print("ok")


if __name__ == "__main__":
    main()
