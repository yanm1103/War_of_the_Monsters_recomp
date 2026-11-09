"""Prova no jogo da chave "scale" do patch de fase (CENTRAL): o chao que o jogo acha embaixo de cada monstro (hdHatTest, o "largada pedida"
do WOTM_SPAWN) segue o telhado do predio escalado. Precisa do disco, do wotm.exe e de uma tela.

    python tools/test_scale_game.py

Escolhe um predio de CENTRAL sem nada em cima, de telhado largo, e joga o patch com "scale" 1, 2 e 0.5 nele. Em cada partida o jogador 1 nasce no
meio do telhado (a caixa do patch, que o lvl.py refaz) e 4 CPUs numa roda dentro dela, do alto (z 1500); o jogo derruba cada um ate o chao que
acha, e a altura tem que bater (6 unidades) com o topo da caixa de colisao do predio no lugar novo. Com a escala 2 a roda cai fora da caixa
do disco e dentro da nova: sem a folha escalada o chao dali seria a rua.
"""
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "level_editor"))
sys.path.insert(0, HERE)
import editor  # noqa: E402
import lvl  # noqa: E402
import test_tilt_collision as T  # noqa: E402

TOL = 6.0
LIFT = 20.0        # o jogo poe o monstro 20 acima do chao que acha
DROP = 120.0       # o monstro e posto 120 acima do telhado esperado


def pick_building(disc):
    """The destructible with the widest top box among those with nothing on its roof and no part that moves."""
    lp = lvl.LevelPatch(lvl.Stage("CENTRAL", disc), False)
    objs = lp.st.objects()
    carried = set(lp.st.on_top(objs).values())
    best = None
    for o in objs:
        if o["kind"] != "destr" or not o["handle"] or o["nested"] or o["handle"] in carried or len(lvl.children(lp.img, o["handle"])) != 1:
            continue
        try:
            paths = lp.scale_plan(o["handle"])
        except lvl.PatchError:
            continue
        boxes = [lvl.Path(lp.img, pa) for pa in paths if not lvl.Path(lp.img, pa).trigger]
        if not boxes:
            continue
        top = max(boxes, key=lambda p: p.max[2])
        w = min(top.max[0] - top.min[0], top.max[1] - top.min[1])
        if w >= 120 and top.max[2] - top.min[2] > 150 and abs(o["pos"][0]) < 900 and abs(o["pos"][1]) < 900:
            if best is None or w > best[0]:
                best = (w, o)
    assert best, "no building to scale"
    return best[1]


def roof(disc, o, scale):
    """The roof box of the building after the patch, as lvl.py leaves it: (cx, cy, half-width, top z)."""
    lp = lvl.LevelPatch(lvl.Stage("CENTRAL", disc), False)
    h = o["handle"]
    lp.apply({"objects": [{"handle": "0x%X" % h, "from": [round(x, 1) for x in o["pos"]], "scale": scale}]})
    assert not lp.stats["skipped"], lp.log
    boxes = [lvl.Path(lp.img, pa) for pa in lp.paths_through(h) if not lvl.Path(lp.img, pa).trigger]
    top = max(boxes, key=lambda p: p.max[2])
    return ((top.min[0] + top.max[0]) / 2, (top.min[1] + top.max[1]) / 2, min(top.max[0] - top.min[0], top.max[1] - top.min[1]) / 2, top.max[2])


def main():
    cfg = dict(editor.load_config())
    disc = cfg["disc"]
    o = pick_building(disc)
    print("building %07x at (%.0f %.0f %.0f)" % (o["handle"], *o["pos"]))
    mods = tempfile.mkdtemp(prefix="scalegame_")
    cfg["mods"], cfg["mod"] = mods, "sg"
    os.makedirs(os.path.join(mods, "sg", "levels"))
    bad = 0
    try:
        for scale in (1.0, 2.0, 0.5):
            cx, cy, half, top = roof(disc, o, scale)
            patch = {"objects": [{"handle": "0x%X" % o["handle"], "from": [round(x, 1) for x in o["pos"]], "scale": scale}]}
            with open(os.path.join(mods, "sg", "levels", "CENTRAL.json"), "w") as f:
                json.dump(patch, f)
            ring = 0.55 * half
            T.ZC = top + DROP          # the game looks for the floor a limited way down from where the monster is put
            pts = T.play(cfg, cx, cy, ring)
            print("scale %g: roof centre (%.0f, %.0f) half-width %.0f top z %.1f, %d monsters placed" % (scale, cx, cy, half, top, len(pts)))
            if len(pts) < 5:
                print("FAIL  fewer than 5 monsters placed")
                bad += 1
                continue
            worst = 0.0
            for px, py, pz in pts:
                worst = max(worst, abs(pz - (top + LIFT)))
                print("   (%5d, %5d) z %5d  roof %7.1f" % (px, py, pz, top + LIFT))
            ok = worst <= TOL
            print(("ok    " if ok else "FAIL  ") + "scale %g: the floor the game finds follows the roof (worst %.1f units)" % (scale, worst))
            bad += 0 if ok else 1
    finally:
        shutil.rmtree(mods, ignore_errors=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
