#!/usr/bin/env python3
"""Pose de um monstro do WotM direto do disco, sem o jogo: avalia uma animacao (curvas) e monta as matrizes das partes
(ANIM_XFORM), o esqueleto e a paleta de ossos das malhas skinned, como o jogo faz na travessia. Formato em docs/FORMATS.md
("Animacao") e docs/guides/MODELS_3D.md.

  python tools/pose.py disc/MON/MONKEY0.NGP --list                      # animacoes do monstro (id, quadros, canais)
  python tools/pose.py disc/MON/MONKEY0.NGP saida.glb                   # pose do idle (animacao 90) no quadro 0
  python tools/pose.py disc/MON/MONKEY0.NGP saida.glb --anim 90 --frame 12
  python tools/pose.py disc/MON/MONKEY0.NGP saida.glb --rest            # pose de base (sem animacao)

Como o jogo faz (conferido contra dumps de RAM de luta em 2026-10-20):
- O .NGP do monstro vem pre-ligado para 0xA00000: lido nessa base, os ponteiros valem sem o .PTR (o .PTR so os desloca para
  onde o jogo carregou). Raiz 0 = o monstro (GROUP com o id do monstro); o ANIMATION_MGR lista as instancias de personagem.
- Animacao `id` = `character->animations[k]` com `head.id1 == id`; o Monster guarda a k-esima como m_anims[id] (0..299,
  Monster::initAfterDbLoad). Idle base = 90 (StateIdle::chooseBaseIdle: 0x5A; 0x5F cansado, 0x60 com especial).
- Cada curva escreve um canal val[dataIdx] (tools/animcurve.py). ANIM_XFORM: rotacao = -val dos canais rX/rY/rZ (os sem
  canal ficam com o valor do no), matriz PRH (mathfRotMatrixPRH = o microprograma 0x4D0 do VU0), pre/postRot,
  translacao; ANIM_SCALE le val direto.
- Esqueleto: SKEL_BONE empilha o osso (e, se for raiz, uma matriz identidade); o proximo no de transformacao (ANIM_XFORM,
  TRANSLATE, ROTATE) desempilha: acumula `local * pai` na pilha de matrizes e grava paleta[osso.skelOutputMatIdx] =
  restPoseInv(osso) * acumulada. A paleta fica nos blocos apontados por animMatrixPtr; o OBJECT skinned acha o seu pela
  cadeia animPktPtr[packetPtrIdx] (Scene::palette do nativo) e cada vertice mistura ate 3 ossos (Scene::skinVertex).
- Nao reproduz: molas do pelo (BoneSpring, ficam na pose da animacao), olhar/cabeca procedural e camadas de mistura.
"""
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import animcurve  # noqa: E402
import ngp  # noqa: E402

IDLE = 0x5A


def prh(x, y, z):
    """mathfRotMatrixPRH (src/common/mathf.cpp)."""
    sz, cz, sx, cx, sy, cy = math.sin(z), math.cos(z), math.sin(x), math.cos(x), math.sin(y), math.cos(y)
    m = np.eye(4)
    m[0][0], m[0][1], m[0][2] = cy * cz, -cy * sz, sy
    m[1][0], m[1][1], m[1][2] = sx * sy * cz + cx * sz, -sx * sy * sz + cx * cz, -sx * cy
    m[2][0], m[2][1], m[2][2] = -cx * sy * cz + sx * sz, cx * sy * sz + sx * cz, cx * cy
    return m


def translate(x, y, z):
    m = np.eye(4)
    m[3, :3] = (x, y, z)
    return m


def scale(x, y, z):
    return np.diag([x, y, z, 1.0])


class CharCtx:
    """Estado de animacao de uma instancia de personagem durante a travessia."""

    def __init__(self, img, ci):
        self.img, self.ci = img, ci
        ch = img.u32(ci + 0x20)
        self.nch = img.u16(ch + 2)
        vp = img.u32(ci + 4)
        self.val = [img.f32(vp + 4 * i) if img.ok(vp + 4 * i) else 0.0 for i in range(self.nch)]
        self.dirty = set()
        self.pal = {}      # endereco da matriz na paleta -> matriz
        self.bones = {}    # saida do osso (skelOutputMatIdx) -> (no SKEL_BONE, matriz acumulada, saida do osso pai ou None)
        amp = img.u32(ci + 0x14)
        self.outAddr = {}
        if img.ok(amp):
            for k in range(64):
                if not img.ok(amp + 4 * k):
                    break
                a = img.u32(amp + 4 * k) & 0x0FFFFFFF
                if not img.ok(a, 64):
                    break
                self.outAddr[k] = a
        self.animPkt = img.u32(ci + 0x18)

    def v(self, idx, fallback):
        return self.val[idx] if 0 <= idx < self.nch else fallback


class Item(tuple):
    """(OBJECT, matriz, CharCtx) de pose(), com o estado do GS herdado na travessia (gs: blend = ALPHA_1, aref = limiar do
    teste de alfa ou -1, zmsk, ztst) e o deslocamento do UV (uv), como o Item do native/wotm_model.hpp."""

    def __new__(cls, node, m, ctx, gs, uv):
        it = tuple.__new__(cls, (node, m, ctx))
        it.gs, it.uv = gs, uv
        return it


class PoseList(list):
    """Lista de Item de pose(), com os nos de efeito percorridos: emitters = [(no op 22, matriz do pai)] (emissores de
    particula, como o fogo do Agamo) e lights = [(no op 5, matriz do pai)] (HierLight, com lens flare), como o visited_ do
    native/wotm_model.hpp. Um no so e percorrido quando o canal de visibilidade dos ANIM_XFORM acima dele vale 1."""
    emitters = ()
    lights = ()


GS_DEFAULT = (0x44, -1, False, 2)   # ctxDef do jogo: ALPHA_1 0x44, sem teste de alfa, grava Z, teste de Z normal


def gs_apply(img, gs, ctxp):
    """Estado do GS depois do pacote A+D do ctx de um no (Scene::groupState; GsState::apply do nativo)."""
    ctxp &= 0x0FFFFFFF
    if not ctxp or not img.ok(ctxp, 0x20) or img.u32(ctxp + 0x14) != 0x10000000 or img.u32(ctxp + 0x18) != 0xE:
        return gs
    n = min(img.u32(ctxp + 0x10) & 0x7FFF, 8)
    if not img.ok(ctxp, 0x20 + 16 * n):
        return gs
    blend, aref, zmsk, ztst = gs
    for i in range(n):
        d, reg = img.u32(ctxp + 0x20 + 16 * i), img.u32(ctxp + 0x28 + 16 * i) & 0xFF
        if reg == 0x42:
            blend = d & 0xFF
        elif reg == 0x47:
            atst, ref = (d >> 1) & 7, (d >> 4) & 0xFF
            ztst = (d >> 17) & 3
            aref = -1 if (not (d & 1) or atst == 1) else ref if atst == 6 else max(ref - 1, 0) if atst == 5 else 8
        elif reg == 0x4E:
            zmsk = bool(img.u32(ctxp + 0x24 + 16 * i) & 1)
    return (blend, aref, zmsk, ztst)


class Model:
    """Monstro (raiz 0 do .NGP) ou, com parts, um modelo montado de pecas de uma fase (chefes): parts = [(no, matriz de
    posicao)] percorridas como raizes, main = a instancia de personagem que a animacao escolhida move. switch_on: SWITCHes
    forcados no filho 1 (o que o jogo liga na luta, ex. hierSetSwitch das cabecas da planta)."""

    def __init__(self, ngp_path, parts=None, main=None, switch_on=()):
        self.img = ngp.Image.load(ngp_path)
        self.parts = parts or [(self.img.roots()[0], np.eye(4))]
        self.root = self.parts[0][0]
        self.switch_on = set(switch_on)
        self.cis = []
        for n, _m in self.parts:
            self._find(n, 0)
        if not self.cis:
            raise ValueError("%s: sem CHAR_INSTANCE na raiz 0" % ngp_path)
        self.main = main or max(self.cis, key=lambda c: self.img.u16(self.img.u32(c + 0x20)))

    def _find(self, n, depth):
        """CHAR_INSTANCEs alcancaveis da raiz (para achar a principal)."""
        img = self.img
        if depth > 80 or not img.ok(n):
            return
        op = img.u32(n) & 63
        if op == 25:
            self.cis.append(n)
        for arr, cnt in self._kids(n, op):
            for k in range(min(cnt, 4096)):
                self._find(img.u32(arr + 4 * k), depth + 1)

    def _kids(self, n, op):
        img = self.img
        if op == 1:
            return [(n + 0x20, img.u16(n + 8))]
        if op == 2:
            nl = img.u32(n + 4)
            return [(n + 0x20 + 16 * i + 8, 1) for i in range(min(nl, 16))]
        table = {3: (8, 0x1C), 4: (8, 0x50), 17: (4, 0xCC), 25: (0x28, 0x2C), 35: (4, 0xC), 38: (4, 0x60), 30: (8, 0x2C)}
        if op in table:
            c, a = table[op]
            return [(n + a, img.u32(n + c))]
        return []

    def apply_anim(self, ctx, k, frame):
        """Escreve no ctx os canais da animacao de indice k no quadro frame (a partir do inicio, em laco)."""
        img = self.img
        ch = img.u32(ctx.ci + 0x20)
        if not 0 <= k < img.u16(ch):
            return
        an = img.u32(ch + 0xC + 4 * k)
        st, et, nch = img.f32(an + 4), img.f32(an + 8), img.u16(an + 0xC)
        t = st + frame
        if et > st and t > et:
            t = st + math.fmod(t - st, et - st)
        for c in range(nch):
            cv = animcurve.Curve(img, img.u32(an + 0x18 + 4 * c))
            if 0 <= cv.idx < ctx.nch:
                ctx.val[cv.idx] = cv.eval(t)
                ctx.dirty.add(cv.idx)

    def anims(self, ci=None):
        """[(id, indice, quadro inicial, quadro final, canais)] da instancia."""
        img = self.img
        ci = ci or self.main
        ch = img.u32(ci + 0x20)
        out = []
        for k in range(img.u16(ch)):
            a = img.u32(ch + 0xC + 4 * k)
            out.append((img.u32(a) >> 18, k, img.f32(a + 4), img.f32(a + 8), img.u16(a + 0xC)))
        return out

    def find_anim(self, anim_id, ci=None, k=None):
        """Animacao pelo id ou, com k, pelo indice na tabela (ids se repetem: o 0 aparece duas vezes no Togera)."""
        for a in self.anims(ci):
            if (a[1] == k) if k is not None else (a[0] == anim_id):
                return a
        return None

    def pose(self, anim_id=IDLE, frame=0.0, rest=False, k=None, assign=None):
        """[Item (OBJECT, matriz 4x4 do objeto, CharCtx ou None) com .gs e .uv] na pose da animacao `anim_id` (ou de indice
        k) no quadro `frame` (a partir do inicio; com laco). rest=True: sem animacao (valores guardados nos nos).
        assign = {instancia: k}: cada instancia na sua animacao, no mesmo quadro (pecas de um chefe; no lugar de anim_id/k)."""
        img = self.img
        ctxs = {}
        main = CharCtx(img, self.main)
        ctxs[self.main] = main
        if assign is not None:
            for ci, kk in assign.items():
                c = ctxs.setdefault(ci, CharCtx(img, ci))
                self.apply_anim(c, kk, frame)
        elif not rest:
            a = self.find_anim(anim_id, k=k)
            if a is None:
                raise ValueError("sem animacao %s" % (anim_id if k is None else "k %d" % k))
            _, k, st, et, nch = a
            t = st + frame
            if et > st and t > et:
                t = st + math.fmod(t - st, et - st)
            an = img.u32(img.u32(self.main + 0x20) + 0xC + 4 * k)
            for c in range(nch):
                cv = animcurve.Curve(img, img.u32(an + 0x18 + 4 * c))
                if 0 <= cv.idx < main.nch:
                    main.val[cv.idx] = cv.eval(t)
                    main.dirty.add(cv.idx)
        items = PoseList()
        items.emitters, items.lights = [], []
        smat = {}
        skel = {}
        owner = {}   # indice da pilha de matrizes -> saida do osso que a gravou (None = identidade de um osso raiz)

        def out(ctx, si, mi):
            bone = skel[si]
            o = img.s16(bone + 8)
            owner[mi] = o if o >= 0 and ctx is not None and o in ctx.outAddr else None
            if owner[mi] is not None:
                rp = np.array(struct.unpack_from("<16f", img.d, bone + 0x10 - img.base)).reshape(4, 4)
                ctx.pal[ctx.outAddr[o]] = rp @ smat[mi]
                ctx.bones[o] = (bone, smat[mi], owner.get(mi - 1))

        def skelstep(ctx, local, si, mi):
            if si < 0:
                return si, mi
            smat[mi + 1] = local @ smat[mi]
            out(ctx, si, mi + 1)
            return si - 1, mi + 1

        def visit(n, m, si, mi, ctx, depth, gs=GS_DEFAULT):
            if depth > 80 or not img.ok(n):
                return
            op = img.u32(n) & 63
            # ctx do GS do no (GROUP/TRANSLATE/ROTATE/SCALE em +4, ANIM_XFORM/ANIM_SCALE em +8): vale para os filhos
            if op in (1, 3, 4, 30):
                gs = gs_apply(img, gs, img.u32(n + 4))
            elif op in (17, 38):
                gs = gs_apply(img, gs, img.u32(n + 8))

            def kids(arr, cnt, mm, si2=si, mi2=mi, ctx2=ctx):
                for q in range(min(cnt, 4096)):
                    visit(img.u32(arr + 4 * q), mm, si2, mi2, ctx2, depth + 1, gs)

            if op == 0:
                # deslocamento do UV (Scene::objectUvOffset): +0x20/+0x24; com uvScroll (bit 0x800 de +0xC), os canais +0x30/+0x32
                uv = [img.f32(n + 0x20), img.f32(n + 0x24)]
                if (img.u16(n + 0xC) & 0x800) and ctx is not None:
                    uv = [ctx.v(img.s16(n + 0x30 + 2 * j), 0.0) for j in range(2)]
                items.append(Item(n, m, ctx, gs, tuple(uv)))
            elif op == 22:
                items.emitters.append((n, m))
            elif op == 5:
                items.lights.append((n, m))
            elif op == 1:
                if img.u16(n + 0xA) & 1:
                    kids(n + 0x20, img.u16(n + 8), m)
            elif op == 2:
                # o mais detalhado que o hierLod desenharia de perto: o maior indice com switchOutDis (+0x24) > 0 (o traje 3 do
                # Togera tem um ultimo nivel com 0, nunca desenhado; como o native/wotm_model.hpp)
                nl = min(img.u32(n + 4), 16)
                for i in range(nl - 1, -1, -1):
                    if img.f32(n + 0x24 + 16 * i) > 0.0:
                        visit(img.u32(n + 0x28 + 16 * i), m, si, mi, ctx, depth + 1, gs)
                        break
            elif op == 3:
                L = translate(img.f32(n + 0x10), img.f32(n + 0x14), img.f32(n + 0x18))
                s2, m2 = skelstep(ctx, L, si, mi)
                kids(n + 0x1C, img.u32(n + 8), L @ m, s2, m2)
            elif op == 4:
                L = np.array(struct.unpack_from("<16f", img.d, n + 0x10 - img.base)).reshape(4, 4)
                s2, m2 = skelstep(ctx, L, si, mi)
                kids(n + 0x50, img.u32(n + 8), L @ m, s2, m2)
            elif op == 30:
                L = scale(img.f32(n + 0x20), img.f32(n + 0x24), img.f32(n + 0x28)) @ translate(
                    img.f32(n + 0x10), img.f32(n + 0x14), img.f32(n + 0x18))
                kids(n + 0x2C, img.u32(n + 8), L @ m)
            elif op == 6:
                w, cnt = img.u8(n + 8), img.u8(n + 0xB)
                if n in self.switch_on:
                    w = 1
                if w < cnt:
                    visit(img.u32(n + 0xC + 4 * w), m, si, mi, ctx, depth + 1, gs)
            elif op in (8, 23):
                visit(img.u32(n + 4), m, si, mi, ctx, depth + 1, gs)
            elif op == 35:
                si2 = si + 1
                skel[si2] = n
                mi2 = mi
                if img.u16(n + 0xA):
                    mi2 = mi + 1
                    smat[mi2] = np.eye(4)
                    owner[mi2] = None
                kids(n + 0xC, img.u32(n + 4), m, si2, mi2)
            elif op == 25:
                c = ctxs.get(n)
                if c is None:
                    c = ctxs[n] = CharCtx(img, n)
                kids(n + 0x2C, img.u32(n + 0x28), m, si, mi, c)
            elif op == 17:
                c = ctx
                vis = img.f32(n + 0xC)
                vidx = img.s16(n + 0x10)
                if c and vidx >= 0 and vidx in c.dirty:
                    vis = c.val[vidx]
                if vis == 0.0:
                    return
                # hierAnimTransNode: so remonta quando um canal do no (firstIdx +0x1E, mascara dirty.animating +0xC1) foi escrito;
                # senao vale atMat[matrixIdx] (+0xC4), que vem pronta no arquivo (garras do Robo-47)
                first, mask = img.s16(n + 0x1E), img.u8(n + 0xC1)
                rebuild = bool(c) and first >= 0 and any((mask >> b) & 1 and (first + b) in c.dirty for b in range(8))
                if rebuild:
                    r = [img.f32(n + 0x20), img.f32(n + 0x24), img.f32(n + 0x28)]
                    tr = [img.f32(n + 0x30), img.f32(n + 0x34), img.f32(n + 0x38)]
                    for j, off in enumerate((0x12, 0x14, 0x16)):
                        i = img.s16(n + off)
                        if i >= 0:
                            r[j] = -c.v(i, -r[j])
                    for j, off in enumerate((0x18, 0x1A, 0x1C)):
                        i = img.s16(n + off)
                        if i >= 0:
                            tr[j] = c.v(i, tr[j])
                    L = prh(*r)
                    bits = img.u16(n + 0xC2)
                    pre = np.array(struct.unpack_from("<16f", img.d, n + 0x40 - img.base)).reshape(4, 4)
                    post = np.array(struct.unpack_from("<16f", img.d, n + 0x80 - img.base)).reshape(4, 4)
                    if bits & 1:
                        L = pre @ L
                        if bits & 2:
                            L = L @ post
                    elif bits & 2:
                        L = L @ post
                    L[3, :3] += tr
                    L[:3, 3] = 0.0
                    L[3, 3] = 1.0
                else:
                    at = img.u32(c.ci + 0xC) + 64 * img.u32(n + 0xC4) if c else 0
                    L = np.array(struct.unpack_from("<16f", img.d, at - img.base)).reshape(4, 4) if c and img.ok(at, 64) else np.eye(4)
                s2, m2 = skelstep(c, L, si, mi)
                kids(n + 0xCC, img.u32(n + 4), L @ m, s2, m2)
            elif op == 38:
                c = ctx
                def g(off, fb):
                    i = img.s16(n + off)
                    return c.v(i, fb) if (c and i >= 0) else fb
                vidx = img.s16(n + 0x10)
                vis = c.v(vidx, 1.0) if (c and vidx >= 0) else 1.0
                if vis == 0.0:
                    return
                sx, sy, sz = g(0x12, img.f32(n + 0x20)), g(0x14, img.f32(n + 0x24)), g(0x16, img.f32(n + 0x28))
                if sx == 0.0 and sy == 0.0 and sz == 0.0:
                    return
                tx, ty, tz = g(0x18, img.f32(n + 0x30)), g(0x1A, img.f32(n + 0x34)), g(0x1C, img.f32(n + 0x38))
                pre = translate(img.f32(n + 0x40), img.f32(n + 0x44), img.f32(n + 0x48))
                post = translate(tx + img.f32(n + 0x50), ty + img.f32(n + 0x54), tz + img.f32(n + 0x58))
                kids(n + 0x60, img.u32(n + 4), pre @ scale(sx, sy, sz) @ post @ m)
            elif op == 39:
                c2 = img.u32(n + 0xC)
                if img.ok(c2) and (img.u32(c2) & 63) == 31:
                    if img.u16(c2 + 0xA):
                        visit(img.u32(c2 + 0xC), m, si, mi, ctx, depth + 1, gs)
                else:
                    visit(c2, m, si, mi, ctx, depth + 1, gs)
            elif op == 31:
                if img.u16(n + 0xA):
                    visit(img.u32(n + 0xC), m, si, mi, ctx, depth + 1, gs)

        for part, pm in self.parts:
            visit(part, pm, -1, -1, None, 0)
        return items

    def palette_outs(self, node, ctx):
        """{slot: saida do osso (skelOutputMatIdx)} da paleta do OBJECT skinned, ou None."""
        img = self.img
        if ctx is None or not img.ok(ctx.animPkt):
            return None
        idx = struct.unpack_from("<i", img.d, node + 0x68 - img.base)[0]
        if idx < 0 or idx > 63:
            return None
        chain = img.u32(ctx.animPkt + 4 * idx) & 0x0FFFFFFF
        if not img.ok(chain, 8) or ((img.u32(chain) >> 28) & 7) != 3:
            return None
        block = img.u32(chain + 4) & 0x0FFFFFFF
        by_addr = {a: o for o, a in ctx.outAddr.items()}
        out = {}
        for s in range(32):
            o = by_addr.get(block + 0x10 + 0x40 * s)
            if o is not None:
                out[s] = o
        return out

    def palette_of(self, node, ctx):
        """{slot: matriz} da paleta do OBJECT skinned (Scene::palette), ou None."""
        outs = self.palette_outs(node, ctx)
        if outs is None:
            return None
        return {s: ctx.pal[ctx.outAddr[o]] for s, o in outs.items() if ctx.outAddr[o] in ctx.pal}


def skin(pal, v):
    """Posicao do vertice skinned no espaco do objeto (Scene::skinVertex: 3 ossos, w1 = w, w2 = frac(w*2048), w3 = resto)."""
    w1 = v.w
    s = v.w * 2048.0
    w2 = s - math.trunc(s)
    w3 = 1.0 - w1 - w2
    p = np.array([v.x, v.y, v.z, 1.0])
    q = np.zeros(3)
    for code, w in ((v.cx, w1), (v.cy, w2), (v.cz, w3)):
        if w == 0.0:
            continue
        mm = pal.get((code & 0x7F) // 4)
        q += (p @ mm)[:3] * w if mm is not None else p[:3] * w
    return q


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    path = argv[0]
    mdl = Model(path)
    if "--list" in argv:
        print("%s: instancia principal %x, %d animacoes" % (path, mdl.main, len(mdl.anims())))
        for aid, k, st, et, nch in mdl.anims():
            print("  id %3d (k %3d) quadros %6.1f..%6.1f canais %3d%s" % (aid, k, st, et, nch, "  <- idle" if aid == IDLE else ""))
        return 0
    out = argv[1]
    anim_id, frame = IDLE, 0.0
    if "--anim" in argv:
        anim_id = int(argv[argv.index("--anim") + 1])
    if "--frame" in argv:
        frame = float(argv[argv.index("--frame") + 1])
    import gltf
    gltf.export(path, out, skinned=True, items=mdl.pose(anim_id, frame, rest="--rest" in argv), model=mdl)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
