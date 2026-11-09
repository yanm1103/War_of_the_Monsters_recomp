"""Test: the entry keys "hp" (hit points of a building) and "traffic" (a car pickup that drives in the traffic or stays parked) in
lvl.py's LevelPatch, with copies that inherit or name their own, and the entries it refuses.

Needs the game disc (configured in the level editor). Reads only; writes nothing.
Run: python tools/test_lvl_extras.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "level_editor"))
import editor   # noqa: E402
import lvl      # noqa: E402


def fresh(disc, name="CENTRAL"):
    return lvl.LevelPatch(lvl.Stage(name, disc), False)


def entry(o, **kw):
    return dict({"handle": "0x%X" % o["handle"], "from": [round(x, 1) for x in o["pos"]]}, **kw)


def destr_state(lp, h):
    """[(class, hitPoints, threshold)] of the destructibles of the object under h."""
    img = lp.img
    return [(img.u16(d + 4), img.u16(d + 6), img.u16(d + 8)) for d in lp.hp_targets(h)]


def traffic_bits(lp, h):
    return [(lp.img.u32(g) >> 7) & 1 for g in lp.traffic_targets(h)]


def skipped(lp, n=1):
    return lp.stats["skipped"] == n and any("skipped" in x for x in lp.log)


def main():
    disc = editor.load_config()["disc"]
    lp = fresh(disc)
    objs = lp.st.objects()
    b = next(o for o in objs if o["kind"] == "destr" and o["handle"] and not o["nested"] and len(children_of(lp, o["handle"])) == 1)
    cars = [o for o in objs if o["kind"] == "pickup" and o["handle"] and lp.st.ident(o["node"]) - lvl.PICKUP_BASE in lvl.CAR_TYPES]
    others = [o for o in objs if o["kind"] == "pickup" and o["handle"] and lp.st.ident(o["node"]) - lvl.PICKUP_BASE not in lvl.CAR_TYPES]
    assert cars and others
    disc_state = destr_state(lp, b["handle"])
    print("building %07x (%s), %d car pickups, %d other pickups" % (b["handle"], disc_state, len(cars), len(others)))

    def want(hp):
        """The disc's destructibles with only the number changed: class 0, the minimum damage they had (class 1 none, 2 and over 5)."""
        return [(0, hp, t if c == 0 else 0 if c == 1 else lvl.HP_MIN_DAMAGE) for c, _hp, t in disc_state]

    # hit points: the building, then a copy that inherits and a copy with its own
    lp.apply({"objects": [entry(b, hp=400, copies=[{"pos": [b["pos"][0] + 900, b["pos"][1], b["pos"][2]]},
                                                     {"pos": [b["pos"][0] + 1800, b["pos"][1], b["pos"][2]], "hp": 25}])]})
    assert not lp.stats["skipped"], lp.log
    assert destr_state(lp, b["handle"]) == want(400), destr_state(lp, b["handle"])
    wrap = lp.st.parents[b["handle"]][0]
    c1, c2 = lp.img.u32(wrap + 0x24) & lvl.MASK, lp.img.u32(wrap + 0x28) & lvl.MASK
    assert destr_state(lp, c1) == want(400), destr_state(lp, c1)
    assert destr_state(lp, c2) == want(25), destr_state(lp, c2)
    other = next(o for o in objs if o is not b and o["kind"] == "destr" and o["handle"])
    assert destr_state(lp, other["handle"]) == destr_state(fresh(disc), other["handle"]), "another building changed"
    print("ok    hp: the building 400, the copy that names nothing 400, the copy with its own 25 (the minimum damage as the disc had it)")
    # a building of another class: class 2 and over keep the minimum damage 5, class 0 its own threshold
    for o in objs:
        if o["kind"] == "destr" and o["handle"] and not o["nested"] and len(children_of(lp, o["handle"])) == 1:
            lp2 = fresh(disc)
            before = destr_state(lp2, o["handle"])
            if before and before[0][0] not in (1,):
                lp2.apply({"objects": [entry(o, hp=77)]})
                assert destr_state(lp2, o["handle"]) == [(0, 77, t if c == 0 else lvl.HP_MIN_DAMAGE) for c, _hp, t in before], (before, destr_state(lp2, o["handle"]))
                print("ok    hp: a building of class %d keeps its minimum damage" % before[0][0])
                break

    # traffic: a parked car turned into traffic, and the other way round, with a copy of each
    lp = fresh(disc)
    car = cars[0]
    was = [bool(v) for v in traffic_bits(lp, car["handle"])]
    lp.apply({"objects": [entry(car, traffic=not was[0], copies=[{"pos": [car["pos"][0] + 40, car["pos"][1], car["pos"][2]]},
                                                                  {"pos": [car["pos"][0] + 80, car["pos"][1], car["pos"][2]], "traffic": was[0]}])]})
    assert not lp.stats["skipped"], lp.log
    wrap = lp.st.parents[car["handle"]][0]
    c1, c2 = lp.img.u32(wrap + 0x24) & lvl.MASK, lp.img.u32(wrap + 0x28) & lvl.MASK
    assert traffic_bits(lp, car["handle"]) == [int(not was[0])], (was, traffic_bits(lp, car["handle"]))
    assert traffic_bits(lp, c1) == [int(not was[0])] and traffic_bits(lp, c2) == [int(was[0])], (traffic_bits(lp, c1), traffic_bits(lp, c2))
    print("ok    traffic: the car %s, its copy the same, the copy with its own %s" % (not was[0], was[0]))

    # the first word's other bits stay (op, id1 and the rest of id2)
    g = lp.traffic_targets(car["handle"])[0]
    base = fresh(disc)
    assert (lp.img.u32(g) & ~0x80) == (base.img.u32(g) & ~0x80), "traffic touched more than bit 0 of the id2"

    # refused, with the entry skipped and nothing written
    for what, e, text in (("hp on a pickup", entry(car, hp=100), "no destructible"),
                          ("traffic on a building", entry(b, traffic=True), "not a car pickup"),
                          ("traffic on a pickup that is not a car", entry(others[0], traffic=True), "not a car pickup"),
                          ("hp 0", entry(b, hp=0), "whole number"),
                          ("hp over the int limit", entry(b, hp=lvl.HP_MAX + 1), "whole number"),
                          ("hp fractional", entry(b, hp=10.5), "whole number"),
                          ("traffic as a number", entry(car, traffic=1), "true or false")):
        lp = fresh(disc)
        before = bytes(lp.img.d)
        lp.apply({"objects": [e]})
        assert skipped(lp) and any(text in x for x in lp.log), (what, lp.log)
        assert bytes(lp.img.d) == before, "%s wrote something" % what
        print("ok    refused: %s" % what)
    lp = fresh(disc)
    lp.apply({"objects": [entry(b, hp=100, copies=[{"pos": [0, 0, 0], "traffic": True}])]})
    assert skipped(lp) and any("not a car pickup" in x for x in lp.log), lp.log
    assert destr_state(lp, b["handle"]) == destr_state(fresh(disc), b["handle"]), "a bad copy left the hit points changed"
    print("ok    refused: a copy that asks for traffic on a building leaves the original as it was")


def children_of(lp, h):
    return lvl.children(lp.img, h)


if __name__ == "__main__":
    main()
