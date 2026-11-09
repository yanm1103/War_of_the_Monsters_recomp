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


def cloud_objects(st):
    """Addresses of the OBJECTs under the CHAR_INSTANCEs (op 25) of the sky nodes."""
    img, out = st.img, []
    for n in sorted(st.root_of):
        if st.ident(n) == 2 and st.op(n) == 1:
            stack, seen = [n], set()
            while stack:
                x = stack.pop()
                if x in seen:
                    continue
                seen.add(x)
                if st.op(x) == 25:
                    sub = list(lvl.children(img, x))
                    while sub:
                        y = sub.pop()
                        if img.u32(y) & 63 == 0:
                            out.append(y)
                        else:
                            sub.extend(lvl.children(img, y))
                else:
                    stack.extend(lvl.children(img, x))
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

        # the cloud layer: "sky": "clouds" swaps the OBJECT under the CHAR_INSTANCE's ANIM_XFORM and leaves the dome and the animation
        shutil.rmtree(os.path.join(mod, "props", "SKYBOX"))
        lvlprop.build(glb, out, "CENTRAL", "game", 400.0, None, "SKYBOX", None, "auto", editor.stage_root(cfg["disc"], "CENTRAL"))
        shutil.copytree(out, os.path.join(mod, "props", "SKYBOX"))
        cpatch = {"objects": [], "props": [{"model": "SKYBOX", "sky": "clouds"}]}
        st = lvl.Stage("CENTRAL", editor.stage_root(cfg["disc"], "CENTRAL"))
        old_dome, old_cloud = domes(st), cloud_objects(st)
        assert len(old_cloud) == 1, old_cloud
        ctpl = {o: st.img.u32(old_cloud[0] + o) for o in (0, 0xC, 0x2C, 0x40)}
        lp = lvl.LevelPatch(st)
        lp.apply(cpatch)
        lp.apply_props(cpatch, mod)
        assert not any("skipped" in x for x in lp.log), lp.log
        img = lp.img
        new_cloud = cloud_objects(lp.st)
        assert len(new_cloud) == 1 and new_cloud[0] >= lp.arena, (new_cloud, lp.log)
        assert img.u32(new_cloud[0]) == ctpl[0] and img.u32(new_cloud[0] + 0x2C) == ctpl[0x2C] and img.u32(new_cloud[0] + 0x40) == ctpl[0x40]
        assert domes(lp.st) == old_dome, "the dome must stay"
        assert lp.stats["sky clouds replaced"] == 1 and not lp.stats["sky domes replaced"], dict(lp.stats)
        def below(n, target, seen=None):
            seen = set() if seen is None else seen
            if n == target:
                return True
            seen.add(n)
            return any(c not in seen and below(c, target, seen) for c in lvl.children(img, n))
        xf = [n for n in lp.st.root_of if lp.st.op(n) == 17 and below(n, new_cloud[0])]
        assert xf, "the model must hang under the ANIM_XFORM that turns the clouds"
        chk = editor.check_patch(cfg["disc"], "CENTRAL", cpatch, mod)
        assert chk["ok"] and "1 cloud layer replaced" in chk["summary"], chk
        # dome and clouds from one model are refused (the second entry), with a line
        both = {"objects": [], "props": [{"model": "SKYBOX", "sky": True}, {"model": "SKYBOX", "sky": "clouds"}]}
        st = lvl.Stage("CENTRAL", editor.stage_root(cfg["disc"], "CENTRAL"))
        lp = lvl.LevelPatch(st)
        lp.apply(both)
        lp.apply_props(both, mod)
        assert any("sky skipped" in x and "both" in x for x in lp.log), lp.log
        assert lp.stats["sky domes replaced"] == 1 and not lp.stats["sky clouds replaced"], dict(lp.stats)
        # a stage's check names every kind of sky change
        assert lvl.sky_kind({"sky": True}) == "dome" and lvl.sky_kind({"sky": "clouds"}) == "clouds" and not lvl.sky_kind({"sky": "x"})
    print("ok")


if __name__ == "__main__":
    main()
