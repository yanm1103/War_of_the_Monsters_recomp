"""Triangulos desenhados com aresta 3D curta mas aresta UV longa (UV claramente errado)."""
import sys, os, math
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ste_model as S

iso = S.Iso(sys.argv[1]); vol = S.Vol(iso)
b = S.Bundle(vol.get(sys.argv[2]))
m = S.decode_mesh(b, b.meshes()[0])
P, UV, T = m['pos'], m['uv'], m['tris']
bad = []
for t in T:
    for x, y in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
        d3 = math.dist(P[x], P[y]); d2 = math.hypot(UV[x][0] - UV[y][0], UV[x][1] - UV[y][1])
        if d3 < 6 and d2 > 0.12:
            bad.append((t, x, y, round(d3, 1), round(d2, 2)))
            break
print('triangulos com UV claramente errado:', len(bad), 'de', len(T))
idx = sorted({min(t) for t, *_ in bad})
print('primeiros vertices:', idx[:40])
# agrupar por intervalos
runs = []
for v in idx:
    if runs and v - runs[-1][1] <= 3:
        runs[-1][1] = v
    else:
        runs.append([v, v])
print('intervalos de vertices:', runs[:40])
for t, x, y, d3, d2 in bad[:8]:
    print(t, 'aresta', x, y, '3D', d3, 'UV', d2, 'b0', m['b0'][x], m['b0'][y])
