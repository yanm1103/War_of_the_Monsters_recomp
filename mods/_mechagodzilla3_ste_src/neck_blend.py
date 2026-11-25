#!/usr/bin/env python3
"""Pesos suaves no pescoco do Mechagodzilla 3 (rig.glb, no lugar).

  python neck_blend.py <mechagodzilla3_ste_rig.glb> [--dry] [--out saida.glb]

No STE o pescoco e uma pilha de aneis (pecas soltas) 100% no Spine1 e a cabeca 100% na Head; o osso Bip01_Neck_Dont_Touch nao
tem vertice. Com a cabeca para cima (ou para baixo) o pescoco nao acompanha: a cabeca descola e as poucas faixas de transicao
(0.2/0.5 de peso na Head) esticam em fios. Aqui cada vertice dos aneis do pescoco ganha o peso da posicao dele ao longo da
cadeia Spine1 -> Neck -> Head (t de 0 a 2: Spine1 puro ate t=1.0, a base do pescoco, que fica presa ao tronco; Spine1 -> Neck
ate 1.35, Neck -> Head ate 1.9, Head puro dai em diante), e o pescoco dobra em arco. So entram as pecas cujo centroide fica a
menos de RC do eixo da cadeia abaixo da base do pescoco (as placas e aletas das costas ficam de fora) e a menos de RC_NECK
acima dela (as aletas da nuca acompanham o pescoco); o anel da gola, na altura da cabeca, entra tambem. Cabeca, mandibula e o
resto do corpo nao mudam. Roda sobre o rig ainda sem estes pesos (o do monster rig do retarget): uma peca ja reescrita deixa
de ter Spine1 e nao e mais reconhecida.
As aletas pequenas (ate SMALL vertices) levam o peso do centroide, para nao entortarem. As distancias (RC) estao em unidades do .dae do STE e sao escaladas pelo comprimento Spine1-Neck do .glb (o --size do retarget).
Depois: modkit.py monster rig ... <este .glb>, texturas (x0.62 e eye_yellow.py) e pack, como no LEIAME.txt.
"""
import argparse
import collections
import json
import struct
import sys

import numpy as np

CHAIN = ("Bip01_Spine1", "Bip01_Neck_Dont_Touch", "Bip01_Head")
T0, T1, T2 = 1.0, 1.35, 1.9
RC_NECK = 17.0       # como o RC, acima da base do pescoco (t > T_NECK): as aletas da nuca entram
T_NECK = 1.05
RC = 11.0            # peso so para pecas com o centroide a menos disto do eixo (unidades do .dae do STE)
SMALL = 12           # pecas com ate tantos vertices (aletas) levam um peso so
LEN_SN = 15.6201     # comprimento Spine1 -> Neck no .dae do STE (a escala do .glb sai da razao)

CT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}


def read_glb(path):
    d = bytearray(open(path, "rb").read())
    magic, ver, total = struct.unpack("<4sII", d[:12])
    assert magic == b"glTF" and ver == 2
    off = 12
    js = binoff = None
    while off < total:
        cl, ct = struct.unpack("<I4s", d[off:off + 8])
        if ct == b"JSON":
            js = (off + 8, cl)
        elif ct == b"BIN\x00":
            binoff = (off + 8, cl)
        off += 8 + cl
    return d, json.loads(bytes(d[js[0]:js[0] + js[1]])), binoff[0]


def accessor(d, j, binbase, idx):
    a = j["accessors"][idx]
    bv = j["bufferViews"][a["bufferView"]]
    fmt, size = CT[a["componentType"]]
    n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
    stride = bv.get("byteStride") or size * n
    start = binbase + bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    return a, start, stride, fmt, size, n


def read_acc(d, j, binbase, idx):
    a, start, stride, fmt, size, n = accessor(d, j, binbase, idx)
    out = np.zeros((a["count"], n), dtype=np.float64 if fmt == "f" else np.int64)
    for i in range(a["count"]):
        out[i] = struct.unpack_from("<" + fmt * n, d, start + i * stride)
    return out


def seg(p, a, b):
    ab = b - a
    u = float(np.clip(np.dot(p - a, ab) / np.dot(ab, ab), 0, 1))
    return u, float(np.linalg.norm(p - (a + u * ab)))


def chain_t(p, S, N, H):
    u1, d1 = seg(p, S, N)
    u2, d2 = seg(p, N, H)
    return (u1, d1) if d1 < d2 else (1 + u2, d2)


def blend(t):
    """pesos por nome de osso para a posicao t na cadeia"""
    if t < T0:
        return {CHAIN[0]: 1.0}
    if t < T1:
        w = (t - T0) / (T1 - T0)
        return {CHAIN[0]: 1 - w, CHAIN[1]: w}
    if t < T2:
        w = (t - T1) / (T2 - T1)
        return {CHAIN[1]: 1 - w, CHAIN[2]: w}
    return {CHAIN[2]: 1.0}


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("glb")
    ap.add_argument("--dry", action="store_true")
    ap.add_argument("--out")
    a = ap.parse_args(argv)
    d, j, bb = read_glb(a.glb)
    skin = j["skins"][0]
    names = [j["nodes"][i]["name"] for i in skin["joints"]]
    jid = {n: k for k, n in enumerate(names)}
    for c in CHAIN:
        if c not in jid:
            sys.exit("falta o osso %s no skin" % c)
    ibm = read_acc(d, j, bb, skin["inverseBindMatrices"]).reshape(-1, 4, 4)
    # glTF guarda matrizes por coluna: o .T devolve a matriz de linha
    wpos = {c: np.linalg.inv(ibm[jid[c]].T)[:3, 3] for c in CHAIN}
    S, N, H = (wpos[c] for c in CHAIN)
    scale = float(np.linalg.norm(N - S)) / LEN_SN
    prim = j["meshes"][0]["primitives"][0]
    P = read_acc(d, j, bb, prim["attributes"]["POSITION"])[:, :3]
    J = read_acc(d, j, bb, prim["attributes"]["JOINTS_0"])
    Wt = read_acc(d, j, bb, prim["attributes"]["WEIGHTS_0"])
    wa = j["accessors"][prim["attributes"]["WEIGHTS_0"]]
    if wa["componentType"] != 5126:
        sys.exit("WEIGHTS_0 nao e float: nao tratado")
    idx = read_acc(d, j, bb, prim["indices"]).reshape(-1, 3)
    print("escala do glb em relacao ao .dae: %.4f; Spine1 %s Neck %s Head %s" % (scale, S.round(2), N.round(2), H.round(2)))

    par = list(range(len(P)))

    def fnd(x):
        while par[x] != x:
            par[x] = par[par[x]]
            x = par[x]
        return x

    for t in idx:
        r0 = fnd(int(t[0]))
        par[fnd(int(t[1]))] = r0
        par[fnd(int(t[2]))] = r0
    pieces = collections.defaultdict(list)
    for i in range(len(P)):
        pieces[fnd(i)].append(i)
    allowed = {jid[c] for c in CHAIN}
    neck_pieces = {}
    for root, vs in pieces.items():
        used = {int(J[i][k]) for i in vs for k in range(4) if Wt[i][k] > 1e-4}
        if jid[CHAIN[0]] not in used or not used <= allowed:   # so pecas do tronco/pescoco; a cabeca (so Head) e o resto ficam
            continue
        cen = P[vs].mean(axis=0)
        t, dist = chain_t(cen, S, N, H)
        if dist < (RC_NECK if t > T_NECK else RC) * scale:
            neck_pieces[root] = t if len(vs) <= SMALL else None   # peca pequena (aleta): um peso so, o do centroide
    # so vertices cujo conjunto de ossos e subconjunto de Spine1/Neck/Head (cabeca, mandibula e o resto ficam)
    changed = 0
    newJ, newW = J.copy(), Wt.copy()
    stat = collections.Counter()
    for i in range(len(P)):
        if fnd(i) not in neck_pieces:
            continue
        t = neck_pieces[fnd(i)]
        if t is None:
            t, dist = chain_t(P[i], S, N, H)
        w = blend(t)
        js = [jid[n] for n in w] + [0] * (4 - len(w))
        ws = list(w.values()) + [0.0] * (4 - len(w))
        newJ[i] = js
        newW[i] = ws
        changed += 1
        stat[tuple(sorted(w))] += 1
    print("pecas no pescoco: %d de %d; vertices reescritos: %d" % (len(neck_pieces), len(pieces), changed))
    for k, v in stat.most_common():
        print("  %4d  %s" % (v, "+".join(x.replace("Bip01_", "") for x in k)))
    if a.dry:
        return 0
    # grava no lugar (mesmos tamanhos)
    aj, startj, strj, fj, sj, nj = accessor(d, j, bb, prim["attributes"]["JOINTS_0"])
    aw, startw, strw, fw, sw, nw = accessor(d, j, bb, prim["attributes"]["WEIGHTS_0"])
    for i in range(len(P)):
        struct.pack_into("<" + fj * 4, d, startj + i * strj, *[int(x) for x in newJ[i]])
        struct.pack_into("<" + fw * 4, d, startw + i * strw, *[float(x) for x in newW[i]])
    open(a.out or a.glb, "wb").write(d)
    print("gravado:", a.out or a.glb)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
