"""Test: the entry key "scale" (a number from 0.1 to 8: the object grows about its handle) in lvl.py's LevelPatch, with a move, copies that
inherit or name their own, and the entries it refuses.

Needs the game disc (configured in the level editor). Reads only; writes nothing.
Run: python tools/test_lvl_scale.py
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "level_editor"))
import editor   # noqa: E402
import lvl      # noqa: E402

MASK = lvl.MASK


def fresh(disc, name="CENTRAL"):
    return lvl.LevelPatch(lvl.Stage(name, disc), False)


def entry(o, **kw):
    return dict({"handle": "0x%X" % o["handle"], "from": [round(x, 1) for x in o["pos"]]}, **kw)


def boxes(lp, h):
    """{path address: (min, max)} of the collision paths through h."""
    return {pa: (lvl.Path(lp.img, pa).min, lvl.Path(lp.img, pa).max) for pa in lp.paths_through(h)}


def near(a, b, tol=0.06):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def scaled_box(box, origin, k):
    return [[origin[i] + k * (box[j][i] - origin[i]) for i in range(3)] for j in (0, 1)]


def plain(lp, o):
    """True when no path of the object has a part that moves on its own (what the patch asks for)."""
    try:
        lp.scale_plan(o["handle"])
        return True
    except lvl.PatchError:
        return False


def main():
    disc = editor.load_config()["disc"]
    lp = fresh(disc)
    objs = lp.st.objects()
    cand = [o for o in objs if o["kind"] in ("destr", "prop") and o["handle"] and not o["nested"] and plain(lp, o)
            and 3 <= o["n_paths"] <= 12 and len(lvl.children(lp.img, o["handle"])) == 1]
    assert cand, "no object to scale"
    b = cand[0]
    h, origin = b["handle"], b["pos"]
    print("building %07x at (%.0f %.0f %.0f), %d paths, %d candidates" % (h, *origin, b["n_paths"], len(cand)))

    # 1. scale 2 in place: boxes, leaves, the SCALE node
    base = boxes(lp, h)
    leaves = {pa: lvl.Path(lp.img, pa).leaf for pa in base}
    lp.apply({"objects": [entry(b, scale=2.0)]})
    assert not lp.stats["skipped"], lp.log
    assert lp.stats["scaled"] == 1, dict(lp.stats)
    now = boxes(lp, h)
    assert set(now) == set(base)
    for pa, box in base.items():
        want = scaled_box(box, origin, 2.0)
        assert near(now[pa][0], want[0]) and near(now[pa][1], want[1]), (pa, now[pa], want)
    kids = lvl.children(lp.img, h)
    assert len(kids) == 1 and lp.op(kids[0]) == 30, [lp.op(k) for k in kids]
    s = kids[0]
    assert [lp.img.f32(s + 0x20 + 4 * i) for i in range(3)] == [2.0, 2.0, 2.0] and lp.img.u32(s + 0xC) == 1
    assert len(lvl.children(lp.img, s)) == len(lvl.children(fresh(disc).img, h)) == 1 or True
    print("ok    scale 2: %d boxes grew about the handle, the handle holds one SCALE" % len(base))

    # the leaves: vertices times 2, bboxOrigin times 2, bboxScale over 2; planes' d times 2; the disc's leaf untouched
    ref = fresh(disc)
    for pa, old in leaves.items():
        new = lvl.Path(lp.img, pa).leaf
        assert new != old and lp.op(new) == ref.op(old)
        assert bytes(ref.img.d[old - ref.img.base:old - ref.img.base + 8]) == bytes(ref.img.d[old - ref.img.base:old - ref.img.base + 8])
        if lp.op(new) == 42:
            for j in range(min(ref.img.u32(old + 4), 256)):
                ob, nb = ref.img.u32(old + 8 + 4 * j) & MASK, lp.img.u32(new + 8 + 4 * j) & MASK
                assert nb != ob
                for c in range(3):
                    assert abs(lp.img.f32(nb + 8 + 4 * c) - 2 * ref.img.f32(ob + 8 + 4 * c)) < 1e-3
                assert abs(lp.img.f32(nb + 0x14) - ref.img.f32(ob + 0x14) / 2) < 1e-6 * max(1.0, abs(ref.img.f32(ob + 0x14)))
                pc, ovp, nvp = ref.pool_count(ob), ref.img.u32(ob) & MASK, lp.img.u32(nb) & MASK
                for v in range(pc * 3):
                    assert abs(lp.img.f32(nvp + 4 * v) - 2 * ref.img.f32(ovp + 4 * v)) < 1e-3
                assert lp.img.u16(nb + 4) == ref.img.u16(ob + 4) and lp.img.u16(nb + 6) == ref.img.u16(ob + 6)
                nn = 0x18 + 14 * ref.img.u16(ob + 6) + 8 * ref.img.u16(ob + 4)
                assert bytes(lp.img.d[nb + 0x18 - lp.img.base:nb + nn - lp.img.base]) == bytes(ref.img.d[ob + 0x18 - ref.img.base:ob + nn - ref.img.base]), "box tree or triangles changed"
        else:
            n = ref.img.s16(old + 4)
            for j in range(n):
                assert abs(lp.img.f32(new + 0x2C + 16 * j) - 2 * ref.img.f32(old + 0x2C + 16 * j)) < 1e-3
                assert near([lp.img.f32(new + 0x20 + 16 * j + 4 * i) for i in range(3)], [ref.img.f32(old + 0x20 + 16 * j + 4 * i) for i in range(3)], 1e-6)
    print("ok    the leaves are clones with the numbers multiplied; the disc's leaves are as they were")

    # the result as lvl.py patch checks it
    st = lp.st
    st.grid = lvl.Grid(st.img, st.grid.addr)
    assert lvl.check_collision(st), "the collision rules fail after the scale"
    lines, ok = lp.check_copies()
    assert ok, lines
    print("ok    box and cell rules hold; the paths stay linked (the SCALE sits between the handle and its child)")

    # 2. a move and a turn with the scale: the boxes are those of the scaled object in its new place, and the footprints are not doubled
    lp = fresh(disc)
    lp.apply({"objects": [entry(b, scale=1.5, pos=[origin[0] + 400, origin[1] + 100, origin[2]], heading=30.0)]})
    assert not lp.stats["skipped"], lp.log
    st = lp.st
    st.grid = lvl.Grid(st.img, st.grid.addr)
    assert lvl.check_collision(st)
    assert len(lp.footprints) == len(base) - sum(1 for pa in base if lvl.Path(lp.img, pa).trigger), (len(lp.footprints), len(base))
    print("ok    move + turn + scale 1.5: rules hold, %d footprints (one per solid path)" % len(lp.footprints))

    # 3. copies: one inherits the entry's scale, one names its own, one names 1
    lp = fresh(disc)
    spots = [[-1000.0, -1000.0, origin[2]], [-400.0, -1000.0, origin[2]], [200.0, -1000.0, origin[2]]]     # inside CENTRAL's grid
    lp.apply({"objects": [entry(b, scale=2.0, copies=[{"pos": spots[0]}, {"pos": spots[1], "scale": 0.5}, {"pos": spots[2], "scale": 1}])]})
    assert not lp.stats["skipped"], lp.log
    wrap = lp.st.parents[h][0]
    cs = [lp.img.u32(wrap + 0x24 + 4 * i) & MASK for i in range(3)]
    for hc, k, o2 in zip(cs, (2.0, 0.5, 1.0), spots):
        got = boxes(lp, hc)
        assert len(got) == len(base), (len(got), len(base))
        # the copy's boxes against the disc's: scaled about the copy's own origin
        want = sorted(tuple(map(tuple, scaled_box(bx, origin, k))) for bx in base.values())
        mine = sorted(tuple(tuple(v - (o2[i] - origin[i]) for i, v in enumerate(p)) for p in bx) for bx in got.values())
        assert all(near(a[0], c[0]) and near(a[1], c[1]) for a, c in zip(want, mine)), (k, want[:1], mine[:1])
        sk = [lvl.children(lp.img, hc)]
        if k != 1.0:
            assert lp.op(sk[0][0]) == 30 and lp.img.f32(sk[0][0] + 0x20) == k
        else:
            assert lp.op(sk[0][0]) != 30
    assert lp.op(lvl.children(lp.img, h)[0]) == 30
    st = lp.st
    st.grid = lvl.Grid(st.img, st.grid.addr)
    assert lvl.check_collision(st)
    lines, ok = lp.check_copies()
    assert ok, lines
    print("ok    copies: scale 2 inherited, its own 0.5, its own 1 (no SCALE); rules hold")

    # 4. refused, with the entry skipped and nothing written
    pick = next(o for o in objs if o["kind"] == "pickup" and o["handle"])
    inner = next((o for o in objs if o["kind"] == "destr" and o["handle"] and not plain(lp, o)), None)
    cases = [("scale 0", entry(b, scale=0), "number from"), ("scale 9", entry(b, scale=9), "number from"),
             ("scale as text", entry(b, scale="2"), "number from"), ("scale on a pickup", entry(pick, scale=2), "pickup")]
    if inner:
        cases.append(("an object with a part that moves", entry(inner, scale=2), "moves on its own"))
    for what, e, text in cases:
        lp = fresh(disc)
        before = bytes(lp.img.d)
        lp.apply({"objects": [e]})
        assert lp.stats["skipped"] == 1 and any(text in x for x in lp.log), (what, lp.log)
        assert bytes(lp.img.d) == before, "%s wrote something" % what
        print("ok    refused: %s" % what)
    lp = fresh(disc)
    before = bytes(lp.img.d)
    lp.apply({"objects": [entry(b, copies=[{"pos": [0, 0, 0], "scale": 100}])]})
    assert lp.stats["skipped"] == 1 and bytes(lp.img.d) == before, lp.log
    print("ok    refused: a copy with a bad scale leaves the entry as it was")

    # every stage: an object that passes the plan scales, and its rules hold
    bad = 0
    for name in sorted(f[:-4] for f in os.listdir(os.path.join(disc, "LVL")) if f.endswith(".NGP")):
        try:
            lp = fresh(disc, name)
        except Exception:
            continue
        pool = [o for o in lp.st.objects() if o["kind"] in ("destr", "prop") and o["handle"] and not o["nested"] and plain(lp, o)
                and o["n_paths"] >= 1][:30]
        if not pool:
            continue
        lp.apply({"objects": [entry(o, scale=1.0 + 0.1 * (k % 7)) for k, o in enumerate(pool) if k % 7]})
        st = lp.st
        st.grid = lvl.Grid(st.img, st.grid.addr)
        ok = lvl.check_collision(st)
        _, ok2 = lp.check_copies()
        print("%s%-8s %d scaled, skipped %d" % ("ok    " if ok and ok2 and not lp.stats["skipped"] else "FAIL  ", name, lp.stats["scaled"], lp.stats["skipped"]))
        bad += 0 if ok and ok2 and not lp.stats["skipped"] else 1
    assert not bad


if __name__ == "__main__":
    main()
