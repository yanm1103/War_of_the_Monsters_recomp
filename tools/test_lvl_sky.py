"""Test: a "props" entry with "sky": true replaces the stage's sky dome with a prop's model (tools/lvl.py, LevelPatch.sky_plan and sky_swap).

Needs the game disc configured in the level editor (Settings). Writes only in a temporary folder.
Run: python tools/test_lvl_sky.py
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
import lvlprop  # noqa: E402
from test_lvlprop_hull import make_glb   # noqa: E402


def domes(st):
    """Addresses of the OBJECTs a sky node holds outside its clouds (the CHAR_INSTANCE, op 25, branch)."""
    img, out, seen = st.img, [], set()

    def walk(n):
        if n in seen:
            return
        seen.add(n)
        for c in lvl.children(img, n):
            op = img.u32(c) & 63
            if op == 0:
                out.append(c)
            elif op != 25:
                walk(c)
    for n in sorted(st.root_of):
        if st.ident(n) == 2 and st.op(n) == 1:
            walk(n)
    return out


def main():
    cfg = editor.load_config()
    with tempfile.TemporaryDirectory() as tmp:
        glb = os.path.join(tmp, "s.glb")
        make_glb(glb, None, None, flip=True)
        out = os.path.join(tmp, "SKYBOX")
        lvlprop.build(glb, out, "CENTRAL", "game", 400.0, None, "SKYBOX", None, "auto", editor.stage_root(cfg["disc"], "CENTRAL"))
        mod = os.path.join(tmp, "mod")
        shutil.copytree(out, os.path.join(mod, "props", "SKYBOX"))
        patch = {"objects": [], "props": [{"model": "SKYBOX", "sky": True}]}

        # CENTRAL: one dome; after the patch its slot points at the model, which wears the dome's style
        st = lvl.Stage("CENTRAL", editor.stage_root(cfg["disc"], "CENTRAL"))
        old = domes(st)
        assert len(old) == 1, old
        tpl = {o: st.img.u32(old[0] + o) for o in (0, 0xC, 0x2C, 0x40)}
        lp = lvl.LevelPatch(st)
        lp.apply(patch)
        lp.apply_props(patch, mod)
        assert not any("skipped" in x for x in lp.log), lp.log
        new = domes(lp.st)
        assert len(new) == 1 and new[0] >= lp.arena and new[0] != old[0], (new, old, lp.log)
        img = lp.img
        assert img.u32(new[0]) == tpl[0] and img.u32(new[0] + 0x2C) == tpl[0x2C] and img.u32(new[0] + 0x40) == tpl[0x40], lp.log
        vmask = 0x1FFF << 16
        assert img.u32(new[0] + 0xC) & ~vmask == tpl[0xC] & ~vmask and img.u32(new[0] + 0xC) & vmask, hex(img.u32(new[0] + 0xC))
        assert lp.stats["sky domes replaced"] == 1 and lp.stats["props"] == 0
        # the editor's check: the summary counts it
        chk = editor.check_patch(cfg["disc"], "CENTRAL", patch, mod)
        assert chk["ok"] and "1 sky dome replaced" in chk["summary"], chk
        # the clouds stay where they were
        cl = [c for n in lp.st.root_of if lp.st.op(n) == 25 for c in lvl.children(img, n)]
        assert cl, "clouds branch lost"

        # TOKYO: two dome OBJECTs; the first slot takes the model, the second an empty GROUP
        st = lvl.Stage("TOKYO", editor.stage_root(cfg["disc"], "TOKYO"))
        shutil.rmtree(os.path.join(mod, "props", "SKYBOX"))
        lvlprop.build(glb, out, "TOKYO", "game", 400.0, None, "SKYBOX", None, "auto", editor.stage_root(cfg["disc"], "TOKYO"))
        shutil.copytree(out, os.path.join(mod, "props", "SKYBOX"))
        assert len(domes(st)) == 2
        lp = lvl.LevelPatch(st)
        lp.apply(patch)
        lp.apply_props(patch, mod)
        assert not any("skipped" in x for x in lp.log), lp.log
        assert lp.stats["sky domes replaced"] == 2, lp.log
        left = domes(lp.st)
        assert len(left) == 1 and left[0] >= lp.arena, left      # the empty GROUP holds no OBJECT

        # a stage without a dome is refused, with nothing left in the arena
        st = lvl.Stage("CANYON1", editor.stage_root(cfg["disc"], "CANYON1"))
        shutil.rmtree(os.path.join(mod, "props", "SKYBOX"))
        lvlprop.build(glb, out, "CANYON1", "game", 400.0, None, "SKYBOX", None, "auto", editor.stage_root(cfg["disc"], "CANYON1"))
        shutil.copytree(out, os.path.join(mod, "props", "SKYBOX"))
        lp = lvl.LevelPatch(st)
        top = lp.arena_top()
        lp.apply(patch)
        lp.apply_props(patch, mod)
        assert any("sky skipped" in x and "no dome" in x for x in lp.log), lp.log
        assert lp.arena_top() == top and not lp.stats["sky domes replaced"]
        chk = editor.check_patch(cfg["disc"], "CANYON1", patch, mod)
        assert not chk["ok"] and any(p["kind"] == "sky" for p in chk["problems"]), chk["problems"]
    print("ok")


if __name__ == "__main__":
    main()
