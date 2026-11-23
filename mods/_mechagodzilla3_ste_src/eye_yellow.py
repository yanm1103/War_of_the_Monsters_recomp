#!/usr/bin/env python3
"""Pinta de amarelo o olho do Mechagodzilla 3 numa textura do mod.

  python eye_yellow.py <mechagodzilla3_ste.dae> <textura.png> [--color FFC820] [--out saida.png]

No STE o olho e uma submalha propria (8 triangulos rigidos na cabeca, na frente do rosto) desenhada a parte com uma cor que o
export (tools/ste/export_dae.py) nao leva: os texels dela sao um desenho de olho em cinza (contorno escuro, miolo claro). Este
script acha os triangulos do olho no .dae pela posicao (a caixa da submalha 2 do STE), marca os texels que so eles cobrem
(nenhum outro triangulo do modelo) e multiplica o cinza pela cor, mantendo o claro e o escuro do desenho. A textura pode estar
em outra escala (a do mod e o PNG do STE x0.62): o miolo fica claro pela cor cheia vezes o cinza normalizado pelo mais claro.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import retarget as R  # noqa: E402
from PIL import Image  # noqa: E402

EYE_LO, EYE_HI = (-3.9, 14.5, 100.2), (3.9, 17.4, 102.5)   # caixa da submalha do olho (STE, unidades do .dae)


def raster(tri, w, h):
    """Texels (x, y) cujo centro cai no triangulo de UV (em pixels)."""
    (ax, ay), (bx, by), (cx, cy) = tri
    out = set()
    for y in range(max(0, int(min(ay, by, cy))), min(h, int(max(ay, by, cy)) + 1)):
        for x in range(max(0, int(min(ax, bx, cx))), min(w, int(max(ax, bx, cx)) + 1)):
            px, py = x + 0.5, y + 0.5
            d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by)
            d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy)
            d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay)
            if not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0)):
                out.add((x, y))
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dae")
    ap.add_argument("png")
    ap.add_argument("--color", default="FFC820")
    ap.add_argument("--out")
    a = ap.parse_args(argv)
    col = tuple(int(a.color[i:i + 2], 16) for i in (0, 2, 4))
    m = R.Model(a.dae)
    im = Image.open(a.png).convert("RGBA")
    w, h = im.size
    eye, other = set(), set()
    neye = 0
    for _img, corners in m.tris:
        uv = [(c[2][0] * w, c[2][1] * h) for c in corners]   # o retarget.Model ja vira o v do .dae: linha da imagem = v * h
        inside = all(all(EYE_LO[k] <= c[0][k] <= EYE_HI[k] for k in range(3)) for c in corners)
        # o olho: os triangulos da caixa que usam a faixa de UV da submalha (o resto da caixa e o focinho)
        if inside and all(0.80 * w <= u <= 0.90 * w and 0.37 * h <= v <= 0.44 * h for u, v in uv):
            eye |= raster(uv, w, h)
            neye += 1
        else:
            other |= raster(uv, w, h)
    only = eye - other
    # mais um texel em volta (a filtragem bilinear puxa a borda bege para dentro do olho), onde nenhum outro triangulo amostra
    only |= {(x + dx, y + dy) for x, y in only for dx in (-1, 0, 1) for dy in (-1, 0, 1)
             if 0 <= x + dx < w and 0 <= y + dy < h} - other
    if neye != 8:
        print("aviso: %d triangulos de olho (o STE tem 8)" % neye)
    px = im.load()
    top = max(sum(px[x, y][:3]) / 3.0 for x, y in only) or 1.0
    for x, y in only:
        r, g, b, al = px[x, y]
        k = min(1.0, (r + g + b) / 3.0 / top)
        px[x, y] = (int(col[0] * k), int(col[1] * k), int(col[2] * k), al)
    im.save(a.out or a.png)
    print("%d triangulos, %d texels do olho, %d tambem de outros triangulos (ficam como estao) -> %s" % (
        neye, len(eye), len(eye & other), a.out or a.png))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
