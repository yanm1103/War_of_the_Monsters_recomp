"""Test: the patch's "delete" on the objects the stage keeps in odd places (tools/lvl.py, LevelPatch.apply).
  - an object with no TRANSLATE/ROTATE of its own is its INTERACTIVE node: it cannot move, but "delete" takes it out (CENTRAL: 0x1196C60);
    a move of it is still refused;
  - an object inside another one that an earlier entry deleted (a sign on a roof, in a state of the building) goes with its parent: a line
    in the log, not a skipped entry, and the same when the parent comes after it in the list.

Needs the game disc configured in the level editor (Settings). Writes nothing.
Run: python tools/test_lvl_delete_nested.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "level_editor"))
import editor   # noqa: E402
import lvl      # noqa: E402

NO_HANDLE = 0x1196C60                       # a destructible with no TRANSLATE/ROTATE above it
ROOF, BUILDING = 0xB376A0, 0xB65A70         # a ROTATE inside a state of the building that holds it, and that building's TRANSLATE


def entry(st, h, **kw):
    pos = lvl.chain_matrix(st.img, st.chain(h))[3][:3]
    return dict(handle="0x%X" % h, **{"from": [round(float(x), 1) for x in pos]}, **kw)


def apply(disc, objects):
    st = lvl.Stage("CENTRAL", disc)
    lp = lvl.LevelPatch(st)
    lp.apply({"objects": [entry(st, h, **kw) for h, kw in objects]})
    return lp


def main():
    cfg = editor.load_config()
    disc = editor.stage_root(cfg["disc"], "CENTRAL")

    lp = apply(disc, [(NO_HANDLE, {"delete": True})])
    assert lp.stats["deleted"] == 1 and not lp.stats["skipped"], (lp.stats, lp.log)
    assert lp.op(NO_HANDLE) == 9, lp.op(NO_HANDLE)
    a, b = lp.interactives_before, lp.interactives()
    assert sum(b) < sum(a), (a, b)      # the Interactive it registered at load is gone

    lp = apply(disc, [(NO_HANDLE, {"pos": [10.0, 10.0, 0.0]})])
    assert lp.stats["skipped"] == 1 and any("not a TRANSLATE/ROTATE" in x for x in lp.log), lp.log

    for order in ((BUILDING, ROOF), (ROOF, BUILDING)):
        lp = apply(disc, [(h, {"delete": True}) for h in order])
        assert not lp.stats["skipped"], (order, lp.log)
        assert lp.op(BUILDING) == 9, lp.op(BUILDING)
        goes = [x for x in lp.log if "goes with it" in x]
        assert len(goes) == (1 if order[0] == BUILDING else 0), (order, lp.log)
    print("test_lvl_delete_nested: ok")


if __name__ == "__main__":
    main()
