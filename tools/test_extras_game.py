"""Prova no jogo das chaves "hp" e "traffic" do patch de fase (CENTRAL). Precisa do disco, do wotm.exe e de uma tela.

    python tools/test_extras_game.py [hp] [traffic]      (sem argumento: todos)

  hp       uma construcao com "hp" 400, uma copia dela sem hp (herda) e outra com 25: o log "[fase] vida:" (WOTM_LEVELPATCH_HP) traz a
           vida que o jogo guardou para cada uma, a classe 0 e o dano minimo que ela tinha;
  traffic  a cena base conta os carros do transito (WOTM_CARLOG, "[carro]"); com "traffic": true em todo carro estacionado a conta sobe
           ao total de carros da fase; com "traffic": false em todo carro do transito ela cai a zero.
"""
import os
import re
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "level_editor"))
sys.path.insert(0, HERE)
import editor  # noqa: E402
import lvl  # noqa: E402
import json  # noqa: E402

ENV = {"WOTM_MUTE": "1", "WOTM_MONITOR": "1", "WOTM_RES": "720", "WOTM_SETTINGS": "0", "WOTM_VIBRATION": "0", "WOTM_NOFOCUS": "1"}


def check(cond, text):
    print(("ok   " if cond else "FAIL ") + text)
    return bool(cond)


def play(patch, secs, env, stage="CENTRAL"):
    """Runs the stage with `patch` as the mod's levels/<stage>.json (a fight against one CPU) and returns the log's lines."""
    cfg = dict(editor.load_config())
    mods = tempfile.mkdtemp(prefix="extras_")
    os.makedirs(os.path.join(mods, "x", "levels"))
    with open(os.path.join(mods, "x", "levels", stage + ".json"), "w") as f:
        json.dump(patch, f)
    cfg["mods"], cfg["mod"] = mods, "x"
    saved = dict(os.environ)
    os.environ.update(ENV)
    os.environ.update(env)
    try:
        editor.launch_game(cfg, stage, False, {"monster": {"monster": "3"}, "players": [], "cpus": [{"monster": "1"}], "dummy": 0})
        t0 = time.time()
        while time.time() - t0 < secs and editor.game_running():
            time.sleep(2)
        time.sleep(1)
        log = editor.game_log(cfg)
        editor.stop_game()
        time.sleep(1)
        return open(log, errors="replace").read().splitlines()
    finally:
        os.environ.clear()
        os.environ.update(saved)
        shutil.rmtree(mods, ignore_errors=True)


def stage_objects():
    st = lvl.Stage("CENTRAL", editor.load_config()["disc"])
    return st, st.objects()


def entry(o, **kw):
    return dict({"handle": "0x%X" % o["handle"], "from": [round(x, 1) for x in o["pos"]]}, **kw)


def run_hp():
    st, objs = stage_objects()
    # a building with a real size: the DESTRUCTIBLE with the most states that is not nested and is alone under its handle
    cand = [o for o in objs if o["kind"] == "destr" and o["handle"] and not o["nested"] and o["states"] >= 3 and not o["handle_shared"]]
    b = cand[0]
    lp = lvl.LevelPatch(st, False)
    cls, hp0, thr = [(lp.img.u16(d + 4), lp.img.u16(d + 6), lp.img.u16(d + 8)) for d in lp.hp_targets(b["handle"])][0]
    x, y, z = b["pos"]
    patch = {"objects": [entry(b, hp=400, copies=[{"pos": [x + 700, y, z]}, {"pos": [x - 700, y, z], "hp": 25}])]}
    lines = play(patch, 40, {"WOTM_LEVELPATCH_HP": "1"})
    good = check(any("[fase] CENTRAL <- x" in l and "hit points set" in l for l in lines), "the game applies the patch (hit points set)")
    vida = [re.search(r"vida: destrutivel (\w+) classe (\d+) vida (\d+) de (\d+), dano minimo (\d+), flags (\w+)", l) for l in lines]
    vida = [m.groups() for m in vida if m]
    print("   disc: class %d, %d hit points, minimum damage %d" % (cls, hp0, thr))
    good &= check(len(vida) == 3, "three destructibles found in the game's table: %d" % len(vida))
    if len(vida) == 3:
        hps = sorted((int(v[2]), int(v[3])) for v in vida)
        good &= check(hps == [(25, 25), (400, 400), (400, 400)], "their health and maximum: 25, 400 (the original) and 400 (the copy that names nothing): %s" % hps)
        good &= check(all(int(v[1]) == 0 for v in vida), "class 0 in all three")
        mind = {int(v[4]) for v in vida}
        want = thr if cls == 0 else 0 if cls == 1 else 5
        good &= check(mind == {want}, "the minimum damage stays what the building had (%d): %s" % (want, mind))
        good &= check(all(int(v[5], 16) & 8 for v in vida), "bit 3 of the Interactive's flags is set: %s" % [v[5] for v in vida])
    good &= check(any("[fase] vida: 3 de 3" in l for l in lines), "and the count says 3 of 3")
    # the minimum damage each building had stays, whatever its hit point class: CENTRAL has classes 0 and 1, VEGAS 2 and SANFRAN 4
    for stage, classes in (("CENTRAL", (0, 1)), ("VEGAS", (2,)), ("SANFRAN", (4,))):
        good &= class_check(stage, classes, b["handle"] if stage == "CENTRAL" else None)
    return good


def class_check(stage, classes, skip):
    """Four buildings of each class in `classes` get an hp; each one the game registers has to come out class 0 with it, the minimum
    damage it had and bit 3 of its Interactive's flags. (A group with mode bits in its flags, which the stage does not register
    in a fight, is left out of the candidates: CENTRAL's three class 2 buildings are in one.)"""
    st = lvl.Stage(stage, editor.load_config()["disc"])
    lp = lvl.LevelPatch(st, False)
    by_class = {}
    for o in st.objects():
        if o["kind"] != "destr" or not o["handle"] or o["nested"] or o["handle_shared"] or o["handle"] == skip or not o["n_paths"]:
            continue
        ds = lp.hp_targets(o["handle"])
        c = lp.img.u16(ds[0] + 4) if len(ds) == 1 else -1
        if c in classes and len(by_class.setdefault(c, [])) < 4 and all((lp.img.u16(x + 0xA) & 0x30) == 0 for x in st.chain(o["node"])[:-1] if lp.op(x) == 1):
            by_class[c].append((o, ds[0]))
    good = check(set(by_class) == set(classes), "%s has buildings of classes %s to try: %s" % (stage, classes, {c: len(v) for c, v in sorted(by_class.items())}))
    patch = {"objects": [entry(o, hp=100 + 10 * c) for c, v in sorted(by_class.items()) for o, _d in v]}
    lines = play(patch, 40, {"WOTM_LEVELPATCH_HP": "1"}, stage)
    got = {}
    for m in (re.search(r"vida: destrutivel (\w+) classe (\d+) vida (\d+) de (\d+), dano minimo (\d+), flags (\w+)", l) for l in lines):
        if m:
            got[int(m.group(1), 16)] = tuple(int(v, 16) if i == 5 else int(v) for i, v in enumerate(m.groups()[1:]))
    for c, v in sorted(by_class.items()):
        seen = [(d, got[d]) for _o, d in v if d in got]
        want_min = lambda d: lp.img.u16(d + 8) if c == 0 else 0 if c == 1 else 5   # noqa: E731
        good &= check(seen and all(g[0] == 0 and g[1] == 100 + 10 * c and g[3] == want_min(d) and g[4] & 8 for d, g in seen),
                      "%s class %d: %d of %d buildings in the game's table, each class 0 with %d hit points, its own minimum damage and flags bit 3: %s" % (
                          stage, c, len(seen), len(v), 100 + 10 * c, [g for _d, g in seen][:2]))
    return good


def run_traffic():
    st, objs = stage_objects()
    cars = [o for o in objs if o["kind"] == "pickup" and o["handle"] and st.ident(o["node"]) - lvl.PICKUP_BASE in lvl.CAR_TYPES]
    parked = [o for o in cars if not o["traffic"]]
    driving = [o for o in cars if o["traffic"]]
    print("   the disc has %d car pickups: %d in the traffic, %d parked" % (len(cars), len(driving), len(parked)))

    def count(patch):
        lines = play(patch, 50, {"WOTM_CARLOG": "1"})
        ids = {m.group(1) for m in (re.search(r"\[carro\] \d+ (\w+) pos", l) for l in lines) if m}
        return len(ids), lines

    base, _ = count({"objects": []})
    good = check(base == len(driving), "the game drives %d cars in the traffic with no patch (the disc says %d)" % (base, len(driving)))
    more, lines = count({"objects": [entry(o, traffic=True) for o in parked]})
    good &= check(any("traffic set %d" % len(parked) in l for l in lines), "the patch says it set %d cars" % len(parked))
    good &= check(more == len(cars), "every car pickup of the stage drives once the parked ones are in the traffic: %d of %d" % (more, len(cars)))
    none, _ = count({"objects": [entry(o, traffic=False) for o in driving]})
    good &= check(none == 0, "no car drives once the traffic ones are parked: %d" % none)
    return good


if __name__ == "__main__":
    runs = {"hp": run_hp, "traffic": run_traffic}
    want = sys.argv[1:] or list(runs)
    results = [runs[w]() if w in runs else sys.exit("hp or traffic") for w in want]
    sys.exit(0 if all(results) else 1)
