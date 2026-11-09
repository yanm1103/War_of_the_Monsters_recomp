"""Prova no jogo dos arquivos de pontos do mod (PTS/CENTRAL.SPT, .PUP, .AIP, .FOP). Precisa do disco, do wotm.exe e de uma tela.

    python tools/test_points_game.py [spt] [aip] [fop]      (sem argumento: os três)

Cada cenário monta um mod temporário, abre o jogo por editor.launch_game, lê o log e confere:
  spt  o monstro nasce no ponto do mod (WOTM_MOVELOG) e a foto guarda os power-ups do mod (olhe a foto em $TEMP/wotm_points_*.png);
  aip  a CPU, com uma rede em U pela rua do norte, passa por ela (WOTM_AILOG, "[ai] ... pos"); a rede do disco a leva em diagonal;
  fop  o exército (WOTM_ARMY) acorda os veículos nos nós da rede do mod ("[exercito] ... acordado no no").
"""
import glob
import math
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

OUT = tempfile.gettempdir()


def spt(human, cpu):
    """One start point of the human in the lists 0, 1 and 3 and the CPU's in 1 and 2 (a free-for-all fight takes list 1 for everyone)."""
    pts = [{"list": 0, "pos": human[:3], "heading": human[3]}, {"list": 1, "pos": human[:3], "heading": human[3]},
           {"list": 1, "pos": cpu[:3], "heading": cpu[3]}, {"list": 2, "pos": cpu[:3], "heading": cpu[3]},
           {"list": 3, "pos": human[:3], "heading": human[3]}]
    return lvl.build_spt(pts)


def scenario(what):
    """(training, opts, files {ext: bytes}, env) of a scenario."""
    env = {"WOTM_MUTE": "1", "WOTM_MONITOR": "1", "WOTM_RES": "720", "WOTM_SETTINGS": "0"}
    fight = {"monster": {"monster": "3"}, "players": [], "cpus": [{"monster": "1"}], "dummy": 0}
    if what == "spt":
        env.update({"WOTM_MOVELOG": "1", "WOTM_SHOTS": "30"})
        return True, {"monster": {"monster": "3"}, "players": [], "cpus": [], "dummy": 0}, {
            "SPT": spt([11, 270, 9, 90], [-640, 326, 9, 90]),
            "PUP": lvl.build_pup([{"type": 2, "pos": [130, 350, 40]}, {"type": 5, "pos": [230, 350, 40]}, {"type": 8, "pos": [330, 350, 40]}])}, env
    if what == "aip":
        # the human waits at (570, -427), the CPU starts at (-640, 326); the mod's net is a U through the north street (y 1080)
        env.update({"WOTM_AILOG": "1", "WOTM_MOVELOG": "1"})
        nodes = [(-640, 326), (-640, 1080), (570, 1080), (570, 326), (570, -427)]
        net = [{"pos": [x, y, 9], "type": 0, "links": ([{"to": k - 1, "width": 12}] if k else []) +
                ([{"to": k + 1, "width": 12}] if k + 1 < len(nodes) else [])} for k, (x, y) in enumerate(nodes)]
        return False, fight, {"SPT": spt([570, -427, 9, 0], [-640, 326, 9, 90]), "AIP": lvl.build_aip(net)}, env
    if what == "fop":
        # a flight square (type 1) at (-900..-500, 700..1000), a street (type 0) along y = 700, far from the disc's net
        env.update({"WOTM_ARMY": "1", "WOTM_ARMY_EVERY": "6", "WOTM_ARMY_WAVE": "3", "WOTM_ARMY_TRIGGER": "0"})
        sq = [(-900, 700), (-500, 700), (-500, 1000), (-900, 1000)]
        nodes = [{"pos": [x, y, 300], "type": 1, "links": [{"to": (k + 1) % 4, "width": 12}, {"to": (k + 3) % 4, "width": 12}]}
                 for k, (x, y) in enumerate(sq)]
        for k, x in enumerate((-900, -700, -500)):
            nodes.append({"pos": [x, 700, 9], "type": 0, "links": [{"to": 4 + k + d, "width": 8} for d in (-1, 1) if 0 <= k + d < 3]})
        return False, fight, {"SPT": spt([11, 319, 9, 90], [-640, 326, 9, 90]), "FOP": lvl.build_fop(nodes)}, env
    raise SystemExit("spt, aip or fop")


def play(what, secs):
    """Runs the scenario and returns the log's lines."""
    training, opts, files, env = scenario(what)
    cfg = dict(editor.load_config())
    mods = tempfile.mkdtemp(prefix="ptsmods_")
    os.makedirs(os.path.join(mods, "pts", "PTS"))
    for ext, data in files.items():
        with open(os.path.join(mods, "pts", "PTS", "CENTRAL." + ext), "wb") as f:
            f.write(data)
    cfg["mods"], cfg["mod"] = mods, "pts"
    workdir = cfg.get("cwd") or os.path.dirname(cfg["game"])
    for f in glob.glob(os.path.join(workdir, "shot_*.png")):
        os.remove(f)
    saved = dict(os.environ)
    os.environ.update(env)
    try:
        editor.launch_game(cfg, "CENTRAL", training, opts)
        t0 = time.time()
        while time.time() - t0 < secs and editor.game_running():
            time.sleep(2)
        time.sleep(1)
        log = editor.game_log(cfg)
        editor.stop_game()
        time.sleep(1)
        lines = open(log, errors="replace").read().splitlines()
    finally:
        os.environ.clear()
        os.environ.update(saved)
        shutil.rmtree(mods, ignore_errors=True)
    for f in glob.glob(os.path.join(workdir, "shot_*.png")):
        shutil.move(f, os.path.join(OUT, "wotm_points_%s_%s" % (what, os.path.basename(f))))
    return lines


def check(cond, text):
    print(("ok   " if cond else "FAIL ") + text)
    return bool(cond)


def run_spt():
    lines = play("spt", 60)
    mv = [re.search(r"\[move\] g=(\d+) p0 .* pos=(-?\d+),(-?\d+),(-?\d+) ang=([+-][\d.]+)", l) for l in lines if "[move]" in l]
    mv = [m for m in mv if m]
    good = check(any("PTS/CENTRAL.SPT <- pts" in l for l in lines) and any("PTS/CENTRAL.PUP <- pts" in l for l in lines), "the game reads the mod's .SPT and .PUP")
    if not check(mv, "the monster's [move] lines"):
        return False
    x, y, _z = int(mv[0].group(2)), int(mv[0].group(3)), int(mv[0].group(4))
    good &= check((x, y) == (11, 270), "the monster starts at the mod's point (11, 270): %d, %d" % (x, y))
    good &= check(abs(float(mv[0].group(5)) - math.pi / 2) < 0.03, "and faces +X (90 degrees): angle %s" % mv[0].group(5))
    return good


def run_aip():
    lines = play("aip", 70)
    pos = [tuple(int(v) for v in m.groups()) for m in (re.search(r"\[ai\] (\d+) m1 pos (-?\d+),(-?\d+)", l) for l in lines) if m]
    good = check(any("PTS/CENTRAL.AIP <- pts" in l for l in lines), "the game reads the mod's .AIP")
    if not check(len(pos) > 12, "the CPU's positions (%d)" % len(pos)):
        return False
    first = [p for p in pos if p[0] < 1800]
    north = [p for p in first if p[2] > 900]
    good &= check(north, "the CPU goes along the north street (y > 900) on its way to the human: %s" % (north[:3],))
    good &= check(not any(abs(p[1]) < 150 and abs(p[2]) < 150 for p in first), "and not through the middle of the stage, as the disc's net takes it")
    return good


def run_fop():
    lines = play("fop", 70)
    woke = [m.groups() for m in (re.search(r"\[exercito\] (\w+) acordado no no (\d+) \((-?\d+), (-?\d+), (-?\d+)\)", l) for l in lines) if m]
    good = check(any("PTS/CENTRAL.FOP <- pts" in l for l in lines), "the game reads the mod's .FOP")
    good &= check(any("rede de 7 nos" in l for l in lines), "the army sees the mod's 7 nodes")
    mine = {(-900, 700), (-700, 700), (-500, 700), (-500, 1000), (-900, 1000)}
    good &= check(woke and all((int(w[2]), int(w[3])) in mine for w in woke), "every vehicle wakes on a node of the mod's net (%d woke)" % len(woke))
    return good


if __name__ == "__main__":
    runs = {"spt": run_spt, "aip": run_aip, "fop": run_fop}
    want = sys.argv[1:] or list(runs)
    results = [runs[w]() if w in runs else sys.exit("spt, aip or fop") for w in want]
    sys.exit(0 if all(results) else 1)
