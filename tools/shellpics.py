#!/usr/bin/env python3
"""Exporta as fotos largas das fases e os logos dos monstros do menu (SHELL/SHELL.TEX + SHELL.RTX) para PNG, como a lista
de saves do nativo mostra (native/wotm_savepics.hpp, NATIVE.md, "Save em JSON"). Usado pelo viewer web (WEB_VIEWER.md) para o
fundo da pagina.

  python tools/shellpics.py disc/SHELL saida/        # saida/stages/01_midtown_park.png (+ stages/thumbs/, 128x64) e
                                                     # saida/logos/lizard.png ...
  (nomes em ingles: o viewer web e todo em ingles)

- Fotos (512x256): dois pedacos T8 de 256x256 lado a lado (a foto quadrada no pedaco anterior nao e usada).
- Logos (256x128, T8).
- Paletas: entradas CT32 16x16 do .RTX pelo tbp (o .NGP nao liga essas; o menu as troca por switch). CSM1 nas T8.
- As linhas estao de baixo para cima (o jogo desenha com V invertido): o PNG sai de cima para baixo.

Nao grava nada do jogo no repo: le o disco do usuario e escreve onde ele mandar.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tex  # noqa: E402

# (arquivo, (texId, tbp da paleta) esquerda, direita) na ordem das fases (kLevels do wotm_save.inc: 1 central .. 11 capitol)
LEVELS = [
    ("01_midtown_park", (99, 308), (100, 312)),
    ("02_gamblers_gulch", (118, 384), (119, 388)),
    ("03_rosdale_canyon", (102, 320), (103, 324)),
    ("04_metro_city", (108, 344), (109, 348)),
    ("05_century_airfield", (93, 284), (94, 288)),
    ("06_atomic_island", (84, 248), (85, 252)),
    ("07_baytown", (111, 356), (112, 360)),
    ("08_club_caldera", (105, 332), (106, 336)),
    ("09_tsunopolis", (114, 368), (115, 372)),
    ("10_ufo", (88, 264), (89, 268)),
    ("11_capitol", (96, 296), (97, 300)),
]
# prefixo do monstro em MON/ -> logo
LOGOS = [
    ("MONKEY", (122, 400)), ("ROBOT", (127, 420)), ("LIZARD", (129, 428)), ("SHOGUN", (128, 424)), ("MANTIS", (126, 416)),
    ("ENERGY", (123, 404)), ("LAVA", (125, 412)), ("DRAGON", (52, 156)), ("ROCK", (124, 408)), ("ALIEN", (121, 396)),
]


class Shell:
    def __init__(self, folder):
        self.tex, _ = tex.read_file(os.path.join(folder, "SHELL.TEX"))
        self.rtx, _ = tex.read_file(os.path.join(folder, "SHELL.RTX"))
        self.tpk = tex.parse(self.tex)["packets"]
        self.rpk = tex.parse(self.rtx)["packets"]

    def tile(self, tex_id, pal_tbp):
        """(w, h, pixels de cima para baixo) de um pedaco T8."""
        p = next((k for k in self.tpk if k.texId == tex_id), None)
        e = next((k for k in self.rpk if k.tbp == pal_tbp and k.psm == tex.CT32 and k.w == 16 and k.h == 16), None)
        if p is None or e is None or p.psm != tex.T8:
            raise ValueError("pedaco texId %d / paleta tbp %d nao achado" % (tex_id, pal_tbp))
        pal = tex.palette_from_entry(self.rtx, e, rtx=True, swap=True)
        px = tex.decode(self.tex, p.data_off(False), p.psm, p.w, p.h, pal)
        rows = [px[y * p.w:(y + 1) * p.w] for y in range(p.h)]
        return p.w, p.h, [c for row in reversed(rows) for c in row]


THUMB = 4   # miniaturas (stages/thumbs/) a 1/4: 128x64, para a grade de fundos do viewer carregar rapido em rede lenta


def shrink(w, h, px, k):
    """Reduz por k com media de cada bloco k x k."""
    out = []
    for y in range(0, h - k + 1, k):
        for x in range(0, w - k + 1, k):
            acc = [0, 0, 0, 0]
            for dy in range(k):
                row = (y + dy) * w
                for dx in range(k):
                    c = px[row + x + dx]
                    for i in range(4):
                        acc[i] += c[i]
            out.append(tuple(v // (k * k) for v in acc))
    return w // k, h // k, out


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    sh = Shell(argv[0])
    stages, logos = os.path.join(argv[1], "stages"), os.path.join(argv[1], "logos")
    thumbs = os.path.join(stages, "thumbs")
    os.makedirs(thumbs, exist_ok=True)
    os.makedirs(logos, exist_ok=True)
    for name, left, right in LEVELS:
        w, h, a = sh.tile(*left)
        w2, _h2, b = sh.tile(*right)
        px = []
        for y in range(h):
            px += a[y * w:(y + 1) * w] + b[y * w2:(y + 1) * w2]
        tex.write_png(os.path.join(stages, name + ".png"), w + w2, h, px)
        tw, th, tpx = shrink(w + w2, h, px, THUMB)
        tex.write_png(os.path.join(thumbs, name + ".png"), tw, th, tpx)
    for pre, t in LOGOS:
        w, h, px = sh.tile(*t)
        tex.write_png(os.path.join(logos, "%s.png" % pre.lower()), w, h, px)
    print("gravadas %d fotos e %d logos em %s" % (len(LEVELS), len(LOGOS), argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
