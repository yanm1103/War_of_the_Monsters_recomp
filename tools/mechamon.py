#!/usr/bin/env python3
"""Monta o Mecha (robo da fase 1 do chefe final, pecas da fase CAPITOL) como arquivo de monstro de base Jelly.

  python tools/mechamon.py <saida> [--disc disc]      # grava <saida>/MECHA.NGP, .PTR, .TEX, .RTX

O molde e o MON/JELLY0 (tipo 0x1C0): fica tudo dele (sombra, reticula, anexos da raiz 1, COLL_GRID), menos o corpo. A raiz 0
passa a apontar para a instancia do Mecha copiada do LVL/CAPITOL.NGP (faixa contigua, sem ponteiro para fora), e um ANIM_MGR
novo lista as instancias do Mecha no lugar das do Jelly. Pelo caminho (MODS.md, "Mecha"):
- ids: a instancia principal vira 0x1C0; escudo 0x27A7 -> 0x1D8 e laser 0x27A9 -> 0x1D9 (instancias, o port as acha pelo id);
  todo outro id de no da faixa (0x27A6..0x27AA) zera, para o finalBoss.addEpNode nao pegar as pecas fora da fase.
- localizadores: 0x27AA (ponta do laser) -> 0x80B; uma ponta de dedo de cada garra -> 0x802/0x803; outra da direita -> 0x808.
- animacoes renumeradas para os ids que o Monster pede (ANIM_MAP).
- texturas: os 7 pacotes do CAPITOL.TEX (204 a 214) vao para o fim do JELLY0.TEX com ids depois dos dele, e as paletas para o
  fim do JELLY0.RTX; texIds e o CBP dos TEX0 da faixa sao reescritos.
- .PTR: as listas do JELLY0 mais as posicoes da faixa (do CAPITOL.PTR) deslocadas, mais as do ANIM_MGR novo.
Le o disco do usuario; nada do jogo vai para o repo (a saida e arquivo do jogo modificado: nao commitar).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ngp  # noqa: E402
import tex  # noqa: E402

B = ngp.LEVEL_BASE
LO, HI = 0x1232E80, 0x12A9A00          # faixa do Mecha no CAPITOL.NGP (docs/investigacoes/2026-10-27/mecha_pecas_capitol.md)
CI_MAIN, CI_SHIELD, CI_LASER, CI_SCROLL = 0x126A5C0, 0x123A290, 0x1237CA0, 0x1237730
ARM_L, ARM_R = 0x1235C40, 0x1237F60    # SKEL_BONE dos bracos (dedos com ATTACHPT id 0 na ponta)
TYPE = 0x1C0
# id do Mecha -> id do Monster (MonsterInit: m_anims[0..299]; tabela em mecha_formato_monstro.md, secao 2)
ANIM_MAP = {
    2: 90,      # parar/esperar -> idle
    0: 2,       # anda +y -> corrida (StateRun)
    3: 70,      # levar golpe -> recuo (StateRecoil::transitionOK pede o 70)
    4: 160, 6: 161, 5: 162,   # preparar, disparar, fim do laser -> especial (StateUltraTazer)
    9: 92,      # religar -> provocar
    10: 120,    # desligar -> morte (sem o 120 a rodada nao acaba)
    1: 289, 65: 288, 11: 290, 12: 291, 194: 299, 195: 298,   # sem uso no Monster
}
TEX_IDS = (204, 205, 207, 209, 210, 211, 214)   # o 214 e do disco 0x27A8 (fora do monstro, mas dentro da faixa)


def read_ptr(path):
    raw = open(path, "rb").read()
    o, out = 0, []
    for _ in range(3):
        n = struct.unpack_from("<I", raw, o)[0]
        out.append(list(struct.unpack_from("<%dI" % n, raw, o + 4)))
        o += 4 + 4 * n
    return out


def kids_of(img, n):
    """Filhos de um no pelos layouts do dbsTraverse (mesmo do tools/monglb.py e build/mecha_inv/mecha_tree.py)."""
    u32, u16, u8 = img.u32, img.u16, img.u8
    op = u32(n) & 63
    lay = {1: (0x20, u16(n + 8)), 3: (0x1C, u32(n + 8)), 4: (0x50, u32(n + 8)), 30: (0x2C, u32(n + 8)),
           6: (0xC, u8(n + 0xB)), 35: (0xC, u32(n + 4)), 25: (0x2C, u32(n + 0x28)), 17: (0xCC, u32(n + 4)),
           38: (0x60, u32(n + 4))}
    out = []
    if op in lay:
        off, cnt = lay[op]
        out = [u32(n + off + 4 * k) for k in range(min(cnt, 4096))]
    elif op == 2:
        out = [u32(n + 0x28 + 16 * i) for i in range(min(u32(n + 4), 16))]
    elif op == 8:
        out = [u32(n + 4), u32(n + 8)]
    elif op in (23,):
        out = [u32(n + 4)]
    elif op == 39:
        out = [u32(n + 0xC)]
    return [c & 0x0FFFFFFF for c in out if img.ok(c & 0x0FFFFFFF)]


def subtree(img, root):
    seen, order, st = set(), [], [root]
    while st:
        n = st.pop()
        if n in seen:
            continue
        seen.add(n)
        order.append(n)
        st.extend(reversed(kids_of(img, n)))
    return order


def pad16(b):
    b.extend(b"\0" * (-len(b) & 15))


def build_tex(jtex, ctex, new_ids):
    """JELLY0.TEX + os pacotes do Mecha (ids novos, bin pelo tamanho do bloco)."""
    jd, _ = tex.read_file(jtex)
    cd, _ = tex.read_file(ctex)
    jp, cp = tex.parse(jd), tex.parse(cd)
    sizes = [list(s) for s in jp["sizes"]]
    out = bytearray(jd[:jp["packets"][-1].end])
    last = jp["packets"][-1]
    by_id = {p.texId: p for p in cp["packets"]}
    cblk = {i: s[1] for i, s in enumerate(cp["sizes"])}
    pkts = []
    for old in TEX_IDS:
        p = by_id[old]
        blk = cblk[p.bin]
        b = next((i for i, s in enumerate(sizes) if s[1] == blk), None)
        if b is None:
            b = len(sizes)
            sizes.append([0, blk])
        sizes[b][0] += 1
        pk = bytearray(cd[p.off:p.end])
        struct.pack_into("<H", pk, 8, new_ids[old])
        bits = struct.unpack_from("<I", pk, 0xC)[0]
        bits = (bits & ~(31 << 27)) | (b << 27)
        struct.pack_into("<I", pk, 0xC, bits)
        pkts.append(pk)
    if 8 + 8 * len(sizes) > jp["headerQw"] * 16:
        raise ValueError("cabecalho do .TEX sem espaco para %d faixas" % len(sizes))
    # liga o ultimo pacote do Jelly ao primeiro novo, e cada novo ao seguinte; o ultimo fica com size 0
    struct.pack_into("<I", out, last.off, ((last.end - last.off) >> 4) * 4)
    for i, pk in enumerate(pkts):
        struct.pack_into("<I", pk, 0, 0 if i == len(pkts) - 1 else (len(pk) >> 4) * 4)
        out += pk
    struct.pack_into("<I", out, 0, len(sizes))
    for i, (cnt, blk) in enumerate(sizes):
        struct.pack_into("<II", out, 8 + 8 * i, cnt, blk)
    return bytes(out)


def build_rtx(jrtx, crtx, cbps):
    """JELLY0.RTX + as paletas do Mecha; devolve (arquivo, {cbp velho: cbp novo})."""
    jd, _ = tex.read_file(jrtx)
    cd, _ = tex.read_file(crtx)
    jp, cp = tex.parse(jd), tex.parse(cd)
    last = jp["packets"][-1]
    out = bytearray(jd[:last.end])
    nxt = 0
    for p in jp["packets"]:
        nxt = max(nxt, p.tbp + max(1, (p.w * p.h * 4) // 256))
    pals = {p.tbp: p for p in cp["packets"] if p.is_palette()}
    remap, pk_list = {}, []
    for old in sorted(cbps):
        p = pals[old]
        pk = bytearray(cd[p.off:p.end])
        remap[old] = nxt
        struct.pack_into("<H", pk, 0xA, nxt)
        nxt += max(1, (p.w * p.h * 4) // 256)
        pk_list.append(pk)
    struct.pack_into("<I", out, last.off, ((last.end - last.off) >> 4) * 4)
    for i, pk in enumerate(pk_list):
        struct.pack_into("<I", pk, 0, 0 if i == len(pk_list) - 1 else (len(pk) >> 4) * 4)
        out += pk
    return bytes(out), remap


def build(disc, outdir):
    cap = ngp.Image.load(os.path.join(disc, "LVL", "CAPITOL.NGP"))
    jel = ngp.Image.load(os.path.join(disc, "MON", "JELLY0.NGP"))
    c0, c1, c2 = read_ptr(os.path.join(disc, "LVL", "CAPITOL.PTR"))
    j0, j1, j2 = read_ptr(os.path.join(disc, "MON", "JELLY0.PTR"))

    img = bytearray(jel.d)
    while len(img) & 0x7F:
        img.append(0)
    nb = B + len(img)                      # onde a faixa comeca na imagem nova
    img += cap.d[LO - B:HI - B]

    def new(a):                            # endereco do CAPITOL -> endereco na imagem nova
        assert LO <= a < HI, hex(a)
        return a - LO + nb

    def put32(a, v):
        struct.pack_into("<I", img, a - B, v)

    def get32(a):
        return struct.unpack_from("<I", img, a - B)[0]

    # ponteiros da faixa
    slots = sorted(B + x for x in c0 if LO <= B + x < HI)
    for s in slots:
        v = cap.u32(s)
        put32(new(s), (v & 0xF0000000) | new(v & 0x0FFFFFFF))
    texid_slots = sorted(B + x for x in c1 if LO <= B + x < HI)
    tex0_slots = sorted(B + x for x in c2 if LO <= B + x < HI)

    # nos do Mecha (a partir da instancia principal; o escudo, o laser e a rolagem estao dentro dela)
    nodes = subtree(cap, CI_MAIN)

    def set_id(n, id1):
        w = get32(new(n))
        put32(new(n), (w & 0x3FFFF) | (id1 << 18))

    fingers = {}
    for arm in (ARM_L, ARM_R):
        fingers[arm] = [n for n in subtree(cap, arm) if cap.u32(n) & 63 == 40 and cap.u32(n) >> 18 == 0]
    loc = {fingers[ARM_L][0]: 0x803, fingers[ARM_R][0]: 0x802, fingers[ARM_R][1]: 0x808}
    renamed = []
    for n in nodes:
        op, id1 = cap.u32(n) & 63, cap.u32(n) >> 18
        if n == CI_MAIN:
            set_id(n, TYPE)
        elif n == CI_SHIELD:
            set_id(n, TYPE + 0x18)
        elif n == CI_LASER:
            set_id(n, TYPE + 0x19)
        elif op == 40 and id1 == 0x27AA:
            set_id(n, 0x80B)
        elif n in loc:
            set_id(n, loc[n])
        elif op == 40:
            continue                           # 0x804/0x805/0x816/0x817/0x809/0x80C/0x80E ficam
        elif id1:
            set_id(n, 0)
            renamed.append((ngp.OPNAMES.get(op, op), hex(id1)))

    # animacoes da instancia principal
    ch = cap.u32(CI_MAIN + 0x20)
    anims = []
    for k in range(cap.u16(ch)):
        a = cap.u32(ch + 0xC + 4 * k) & 0x0FFFFFFF
        old = cap.u32(a) >> 18
        nid = ANIM_MAP.get(old, old)
        set_id(a, nid)
        anims.append((old, nid, cap.f32(a + 8)))

    # texturas
    jtex = tex.parse(tex.read_file(os.path.join(disc, "MON", "JELLY0.TEX"))[0])
    top = max(p.texId for p in jtex["packets"])
    new_ids = {old: top + 1 + i for i, old in enumerate(TEX_IDS)}
    cbps = set()
    for s in tex0_slots:
        v = struct.unpack_from("<Q", cap.d, s - B)[0]
        cbps.add(tex.tex0_fields(v)["cbp"])
    rtx, cbp_map = build_rtx(os.path.join(disc, "MON", "JELLY0.RTX"), os.path.join(disc, "LVL", "CAPITOL.RTX"), cbps)
    for s in texid_slots:
        old = cap.u16(s)
        if old:
            struct.pack_into("<H", img, new(s) - B, new_ids[old])
    for s in tex0_slots:
        v = struct.unpack_from("<Q", cap.d, s - B)[0]
        cbp = (v >> 37) & 0x3FFF
        v = (v & ~(0x3FFF << 37)) | (cbp_map[cbp] << 37)
        struct.pack_into("<Q", img, new(s) - B, v)
    texf = build_tex(os.path.join(disc, "MON", "JELLY0.TEX"), os.path.join(disc, "LVL", "CAPITOL.TEX"), new_ids)

    # raiz 0 (GROUP 0x1C0) -> instancia do Mecha; ANIM_MGR novo no fim
    roots = jel.roots()
    r0, rmgr = roots[0], roots[3]
    assert jel.u32(rmgr) & 63 == 7 and jel.u32(r0) & 63 == 1
    struct.pack_into("<H", img, r0 + 8 - B, 1)
    put32(r0 + 0x20, new(CI_MAIN))
    keep = [jel.u32(rmgr + 8 + 4 * k) for k in range(jel.u32(rmgr + 4))]
    keep = [c for c in keep if jel.u32(c & 0x0FFFFFFF) >> 18 not in (0, TYPE)]   # fica a 0x63 da raiz 1
    cis = [new(CI_MAIN)] + keep + [new(CI_SHIELD), new(CI_LASER), new(CI_SCROLL)]
    pad16(img)
    mgr = B + len(img)
    img += struct.pack("<II", jel.u32(rmgr), len(cis)) + struct.pack("<%dI" % len(cis), *cis)
    pad16(img)
    struct.pack_into("<I", img, 4 + 4 * 3, mgr)
    new_slots = [mgr + 8 + 4 * k for k in range(len(cis))]

    # .PTR
    l0 = j0 + [new(s) - B for s in slots] + [s - B for s in new_slots]
    l1 = j1 + [new(s) - B for s in texid_slots]
    l2 = j2 + [new(s) - B for s in tex0_slots]
    ptr = bytearray()
    for lst in (l0, l1, l2):
        ptr += struct.pack("<I", len(lst)) + struct.pack("<%dI" % len(lst), *lst)
    ptr += b"\0" * 16
    if len(ptr) > 0x30D40:
        raise ValueError(".PTR com %d bytes (o jogo aceita ate 200000)" % len(ptr))

    os.makedirs(outdir, exist_ok=True)
    for ext, data in ((".NGP", img), (".PTR", ptr), (".TEX", texf), (".RTX", rtx)):
        with open(os.path.join(outdir, "MECHA" + ext), "wb") as f:
            f.write(data)
    print("MECHA.NGP %d bytes (Mecha em %#x), .PTR %d (%d/%d/%d), .TEX %d, .RTX %d" % (
        len(img), nb, len(ptr), len(l0), len(l1), len(l2), len(texf), len(rtx)))
    print("texturas:", {k: v for k, v in new_ids.items()}, "paletas:", cbp_map)
    print("animacoes (velho -> novo, quadros):", anims)
    print("ids zerados:", renamed)
    print("localizadores dos dedos:", {hex(k): hex(v) for k, v in loc.items()})


def main(argv):
    if not argv or argv[0].startswith("-"):
        print(__doc__)
        return 1
    disc = argv[argv.index("--disc") + 1] if "--disc" in argv else "disc"
    build(disc, argv[0])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
