"""Test: the patch's "grid" key (tools/lvl.py, LevelPatch.apply_grid): a bigger collision grid with its own list of moving objects
(csListArr, one pointer a cell: the game indexes it by cell, so the disc's one is too short for a grid with more cells).

Needs the game disc configured in the level editor (Settings). Writes nothing.
Run: python tools/test_lvl_grid.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "level_editor"))
import editor   # noqa: E402
import lvl      # noqa: E402


def main():
    cfg = editor.load_config()
    disc = editor.stage_root(cfg["disc"], "CENTRAL")
    st = lvl.Stage("CENTRAL", disc)
    g = st.grid
    old_cells, old_lists = g.cols * g.rows, st.img.u32(g.addr + 0x1C) & lvl.MASK
    grow = 2000.0
    lp = lvl.LevelPatch(st)
    lp.apply({"objects": [], "grid": {"min": [g.min_x - grow, g.min_y - grow], "max": [g.max_x + grow, g.max_y + grow]}})
    assert not any("grid skipped" in x for x in lp.log), lp.log
    img, ng = lp.img, lp.st.grid
    assert ng.addr >= lp.arena and ng.cols * ng.rows > 3 * old_cells, (hex(ng.addr), ng.cols, ng.rows)
    lists = img.u32(ng.addr + 0x1C) & lvl.MASK
    cells = ng.cols * ng.rows
    # the list array is new, empty, inside the arena and has one pointer for every cell of the new grid
    assert lists != old_lists and lists >= lp.arena and lists + 4 * cells <= lp.arena_top(), (hex(lists), hex(old_lists), cells)
    assert all(img.u32(lists + 4 * i) == 0 for i in range(cells))
    # it does not overlap the cells it sits beside
    assert lists >= ng.addr + 0x20 + 4 * cells or lists + 4 * cells <= ng.addr, (hex(lists), hex(ng.addr))
    # a grid over the cell limit is refused, with nothing written
    st2 = lvl.Stage("CENTRAL", disc)
    lp2 = lvl.LevelPatch(st2)
    lp2.apply({"objects": [], "grid": {"min": [g.min_x - 30000, g.min_y - 30000], "max": [g.max_x + 30000, g.max_y + 30000]}})
    assert any("grid skipped" in x for x in lp2.log) and lp2.st.grid.addr == g.addr, lp2.log
    print("ok")


if __name__ == "__main__":
    main()
