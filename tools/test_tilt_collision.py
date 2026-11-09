"""Prova no jogo da colisao de um prop inclinado (pitch, roll, heading): o chao que o jogo acha embaixo de cada monstro (hdHatTest, o que o
WOTM_SPAWN imprime em "[fase] largada pedida") segue o topo do prop. Precisa do disco, do wotm.exe e de uma tela.

    python tools/test_tilt_collision.py

Um mod temporario com dois modelos, postos no ar de CENTRAL, acima dos predios (z 1500), com inclinacoes diferentes:
  SLAB  um piso de 600 x 600 de triangulos (`coll_floor`, lvlprop.floor_glb);
  HULL  um bloco de 400 x 400 x 100 de casco convexo (`hull_box`, test_lvlprop_hull.make_glb).
Em cada partida o jogador 1 nasce no meio do prop e 4 CPUs numa roda de 150 (o bloco: 100) unidades em volta; o jogo derruba cada um do alto ate o
chao que acha, e a altura tem que bater (4 unidades) com o plano do topo que a matriz do patch (lvl.prop_matrix) faz.
"""
import base64
import json
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
import lvlprop  # noqa: E402
import test_lvlprop_hull as H  # noqa: E402

ZC = 1500.0
RING = 150.0          # the slab: the ring stays inside its 600 x 600
RING_HULL = 100.0     # the block's top is 400 wide but leans: 150 would put a point past the edge of the face when it is pitched 25
M = lvlprop.UNITS_PER_METER
# (label, model, x, y, heading, pitch, roll, top): "top" is the local height of the surface the monsters stand on (0 the slab, 100 the block's top)
CASES = (("slab: pitch 30", "SLAB", -1000.0, 0.0, 0.0, 30.0, 0.0, 0.0),
         ("slab: roll -25", "SLAB", 0.0, 0.0, 0.0, 0.0, -25.0, 0.0),
         ("slab: heading 40, pitch 20, roll 15", "SLAB", 1000.0, 0.0, 40.0, 20.0, 15.0, 0.0),
         ("hull: pitch 25", "HULL", -1000.0, 700.0, 0.0, 25.0, 0.0, 100.0),
         ("hull: heading 70, pitch 15, roll -20", "HULL", 0.0, 700.0, 70.0, 15.0, -20.0, 100.0))


def plane_z(pos, heading, pitch, roll, top, x, y):
    """The height at (x, y) of the plane z = top of the model's own axes, moved by the matrix of the patch's entry."""
    m = lvl.prop_matrix(pos, heading, pitch, roll)
    p = lambda lx, ly, lz: [sum(c * m[i][j] for i, c in enumerate((lx, ly, lz, 1.0))) for j in range(3)]  # noqa: E731
    o, ex, ey = p(0, 0, top), p(1, 0, top), p(0, 1, top)
    u = [ex[i] - o[i] for i in range(3)]
    v = [ey[i] - o[i] for i in range(3)]
    n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
    return o[2] - (n[0] * (x - o[0]) + n[1] * (y - o[1])) / n[2]


def hull_glb(path):
    """A convex block 400 x 400 x 100 units (Blender meters, Y up, base at 0) as a hull_ mesh, and the cube the stage draws."""
    hx, hy = 200.0 / M, 100.0 / M
    pts = [(-hx, 0, -hx), (hx, 0, -hx), (hx, hy, -hx), (-hx, hy, -hx), (-hx, 0, hx), (hx, 0, hx), (hx, hy, hx), (-hx, hy, hx)]
    tris = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 6, 2, 3, 7, 6, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5]
    H.make_glb(path, pts, tris)


def play(cfg, x, y, ring):
    """Opens CENTRAL with the player at (x, y) and four CPUs around; returns the "largada pedida" points [(x, y, z)]."""
    opts = {"monster": {"monster": "3"}, "players": [], "cpus": [{"monster": "1"}] * 4, "dummy": 0, "here": [x, y, ZC, 0, ring]}
    saved = dict(os.environ)
    os.environ.update({"WOTM_MUTE": "1", "WOTM_MONITOR": "1", "WOTM_RES": "720", "WOTM_SETTINGS": "0", "WOTM_VIBRATION": "0", "WOTM_NOFOCUS": "1"})
    pts = []
    try:
        editor.launch_game(cfg, "CENTRAL", False, opts)
        log, t0 = editor.game_log(cfg), time.time()
        while time.time() - t0 < 150 and editor.game_running():
            time.sleep(3)
            pts = re.findall(r"largada pedida: [^(]*\((-?\d+), (-?\d+), (-?\d+)\)", open(log, errors="replace").read())
            if len(pts) >= 5:
                break
        editor.stop_game()
        time.sleep(1)
    finally:
        os.environ.clear()
        os.environ.update(saved)
    return [tuple(int(v) for v in p) for p in pts]


def main():
    cfg = dict(editor.load_config())
    mods = tempfile.mkdtemp(prefix="tiltcoll_")
    cfg["mods"], cfg["mod"] = mods, "tc"
    os.makedirs(os.path.join(mods, "tc", "levels"))
    editor.import_prop(cfg, {"mod": "tc", "stage": "CENTRAL", "name": "SLAB", "replace": True, "title": "Slab", "units": "blender",
                             "data": base64.b64encode(lvlprop.floor_glb(300, 300)).decode()})
    glb = os.path.join(mods, "hull.glb")
    hull_glb(glb)
    with open(glb, "rb") as f:
        editor.import_prop(cfg, {"mod": "tc", "stage": "CENTRAL", "name": "HULL", "replace": True, "title": "Hull", "units": "blender",
                                 "data": base64.b64encode(f.read()).decode()})
    props = [{"id": "p%d" % k, "model": c[1], "pos": [c[2], c[3], ZC], "heading": c[4], "pitch": c[5], "roll": c[6]} for k, c in enumerate(CASES)]
    with open(os.path.join(mods, "tc", "levels", "CENTRAL.json"), "w") as f:
        json.dump({"props": props}, f)
    bad = 0
    try:
        for label, model, x, y, heading, pitch, roll, top in CASES:
            pts = play(cfg, x, y, RING if model == "SLAB" else RING_HULL)
            print("%s: %d monsters placed" % (label, len(pts)))
            if len(pts) < 5:
                print("FAIL  fewer than 5 monsters placed")
                bad += 1
                continue
            worst = 0.0
            for px, py, pz in pts:
                want = plane_z([x, y, ZC], heading, pitch, roll, top, px, py) + 20.0     # the game stands the monster 20 above the ground it finds
                worst = max(worst, abs(pz - want))
                print("   (%5d, %5d) z %5d  plane %7.1f" % (px, py, pz, want))
            ok = worst <= 4.0
            print(("ok    " if ok else "FAIL  ") + "%s: the floor the game finds follows the top (worst %.1f units)" % (label, worst))
            bad += 0 if ok else 1
    finally:
        shutil.rmtree(mods, ignore_errors=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
