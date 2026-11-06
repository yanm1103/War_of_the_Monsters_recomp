#!/usr/bin/env python3
"""Exporta a geometria de um .NGP do WotM para glTF binario (.glb), com as texturas do .TEX/.RTX do mesmo arquivo.

  python tools/gltf.py disc/LVL/CENTRAL.NGP saida/central.glb            # acha CENTRAL.TEX e CENTRAL.RTX ao lado
  python tools/gltf.py disc/LVL/CENTRAL.NGP saida/central.glb --lod 0    # LOD mais grosseiro (padrao: o mais detalhado)
  python tools/gltf.py disc/MON/CONGAR1.NGP saida/congar.glb --skinned   # inclui as malhas skinned cruas (ver abaixo)
  opcoes: --tex X.TEX --rtx X.RTX (outros arquivos de textura), --no-tex (so cor de vertice), --lit (sem KHR_materials_unlit)

O que sai certo: objetos estaticos (fase, predios, destrutiveis intactos, menu), com a hierarquia de TRANSLATE/ROTATE/SCALE
aplicada, a textura de cada secao (Vtx.slot -> texId/gsTexCtx do objeto), UV, cor de vertice e normais. A malha vem do mesmo
decodificador do nativo (ngp.decode_mesh). Eixos: o jogo usa Z para cima; o glTF, Y para cima (x, z, -y).

Monstros (conferido em 2026-10-20 com os 13 monstros, traje 0, e o validador oficial do glTF: 0 erros): com --skinned o corpo
sai inteiro na pose de base (pose T), porque as posicoes das malhas skinned estao no espaco do modelo em repouso (a paleta do
jogo e restPoseInv * osso). O que ainda nao sai: animacao e esqueleto (curvas do AnimCurve.cpp, SKEL_BONE/ANIM_XFORM). Partes
rigidas sob ANIM_XFORM saem na matriz identidade, e entram junto objetos que nao sao o corpo (retratos do HUD, projeteis,
efeitos), amontoados perto da origem.
Sprites (halo dos power-ups) tambem ficam de fora. A cor de vertice ja traz a luz pre-calculada das fases, por isso o material
e "unlit" por padrao.

Nao grava nada do jogo no repo: le o disco do usuario e escreve onde ele mandar.
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ngp  # noqa: E402
import tex  # noqa: E402


def find_sibling(path, ext):
    return tex.sibling(path, ext)


class Glb:
    """Montador de glTF 2.0 binario: buffer unico, accessors com min/max nas posicoes."""

    def __init__(self):
        self.bin = bytearray()
        self.j = {"asset": {"version": "2.0", "generator": "WotM tools/gltf.py"}, "scene": 0, "scenes": [{"nodes": []}],
                  "nodes": [], "meshes": [], "materials": [], "accessors": [], "bufferViews": [], "buffers": [],
                  "images": [], "textures": [], "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]}

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.j["bufferViews"].append(v)
        return len(self.j["bufferViews"]) - 1

    def accessor(self, fmt, comps, values, ctype=5126, target=34962, minmax=False, normalized=False):
        flat = [x for v in values for x in (v if comps > 1 else (v,))]
        data = struct.pack("<%d%s" % (len(flat), fmt), *flat)
        a = {"bufferView": self.view(data, target), "componentType": ctype, "count": len(values),
             "type": {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4"}[comps]}
        if normalized:
            a["normalized"] = True
        if minmax:
            a["min"] = [min(v[k] for v in values) for k in range(comps)]
            a["max"] = [max(v[k] for v in values) for k in range(comps)]
        self.j["accessors"].append(a)
        return len(self.j["accessors"]) - 1

    def image(self, png, name):
        bv = self.view(png)
        self.j["images"].append({"bufferView": bv, "mimeType": "image/png", "name": name})
        self.j["textures"].append({"sampler": 0, "source": len(self.j["images"]) - 1})
        return len(self.j["textures"]) - 1

    def write(self, path):
        for k in ("images", "textures", "materials"):
            if not self.j[k]:
                del self.j[k]
        if "textures" not in self.j:
            del self.j["samplers"]
        while len(self.bin) % 4:
            self.bin.append(0)
        self.j["buffers"] = [{"byteLength": len(self.bin)}]
        js = json.dumps(self.j, separators=(",", ":")).encode()
        js += b" " * ((-len(js)) % 4)
        out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(self.bin))
        out += struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin)
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "wb") as f:
            f.write(out)


def to_y_up(p):
    return (p[0], p[2], -p[1])


class Materials:
    """Um material por (textura do slot da secao, TEX0, mistura), com a imagem do tex.Library embutida no .glb."""

    def __init__(self, g, img, lib, unlit, stats, raw=False):
        """raw: a textura com o alfa cru do GS (0x80 = 1,0, ate 0xFF; tex.Library.image hi=True), para quem faz a modulacao
        do GS no shader (tools/lvlglb.py); o padrao e o alfa do PNG (0x80 ou mais = 255)."""
        self.g, self.img, self.lib, self.unlit, self.stats, self.raw = g, img, lib, unlit, stats, raw
        self.cache = {}

    def get(self, node, slot, blend, gs=None, extras=None):
        """blend: PRIM.ABE da malha. gs: estado do GS herdado (pose.Item.gs: ALPHA_1, aref, zmsk, ztst) ou None (o padrao
        antigo: recorte de alfa em ~6%). Com gs, o material leva em extras.gs o modo de mistura (com ABE), o limiar do
        teste de alfa e o ZMSK; o viewer monta a mistura do GS com eles (o glTF so tem OPAQUE/MASK/BLEND).
        extras: campos a mais nos extras do material (entram na chave), ex. a nevoa e o alphaSort do tools/lvlglb.py."""
        img, lib, g, stats = self.img, self.lib, self.g, self.stats
        gk = None
        if gs is not None:
            gk = (gs[0] if blend else None, gs[1], bool(gs[2]) if blend else False)
        ek = tuple(sorted(extras.items())) if extras else None
        if slot == ngp.SLOT_NOTEX or lib is None:
            key = ("cor", blend, gk, ek)
        else:
            s = slot if slot < min(img.u16(node + 0x5A), 5) else 0
            tid = img.u16(node + 0x50 + 2 * s)
            t0 = img.u32(node + 0x70 + 0x30 * s) | (img.u32(node + 0x74 + 0x30 * s) << 32)
            key = ("tex", tid, t0, blend, gk, ek)
        if key in self.cache:
            return self.cache[key]
        m = {"pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0}, "doubleSided": True}
        if self.unlit:
            m["extensions"] = {"KHR_materials_unlit": {}}
        if blend:
            m["alphaMode"] = "BLEND"
        if gk is not None:
            m["extras"] = {"gs": {"blend": gk[0], "aref": gk[1], "zmsk": gk[2]}}
        if extras:
            m.setdefault("extras", {}).update(extras)
        if key[0] == "tex":
            im = lib.image(key[1], key[2], hi=self.raw)
            if im is not None and self.raw and not tex.tex0_fields(key[2])["tcc"]:
                im = (im[0], im[1], [(r, gg, b, 0x80) for (r, gg, b, _a) in im[2]])   # TCC = 0: At = 1,0
            if im is None:
                stats["sem_textura"] += 1
                m["name"] = "tex%d_nao_achada" % key[1]
            else:
                w, h, px = im
                m["name"] = "tex%d" % key[1]
                m["pbrMetallicRoughness"]["baseColorTexture"] = {"index": g.image(tex.png_bytes(w, h, px), m["name"])}
                stats["texturas"] += 1
                if gk is not None:
                    # como o shader do nativo: so descarta com teste de alfa (As*128 <= aref; As = 1 em 0x80)
                    if not blend and gk[1] >= 0:
                        m["alphaMode"] = "MASK"
                        m["alphaCutoff"] = (gk[1] + 0.5) / 128.0
                elif not blend and any(a < 255 for (_r, _g, _b, a) in px):
                    m["alphaMode"] = "MASK"
                    m["alphaCutoff"] = 0.06   # o shader do nativo descarta abaixo de ~6%
        else:
            m["name"] = "cor_de_vertice"
        g.j["materials"].append(m)
        self.cache[key] = len(g.j["materials"]) - 1
        return self.cache[key]


def export(ngp_path, out, lod=99, skinned=False, tex_path=None, rtx_path=None, use_tex=True, unlit=True, quiet=False,
           items=None, model=None):
    """items/model: pose do tools/pose.py (lista de OBJECTs com matriz e a paleta de ossos); sem eles, a hierarquia crua."""
    if model is not None:
        global pose_mod
        import pose as pose_mod
    data = ngp.inflate_ngp(ngp_path)
    img = ngp.Image(data, 0)
    img.base = tex.guess_base(img)
    lib = None
    if use_tex:
        tex_path = tex_path or find_sibling(ngp_path, ".TEX")
        rtx_path = rtx_path or find_sibling(ngp_path, ".RTX")
        if tex_path or rtx_path:
            lib = tex.Library(tex_path, rtx_path, ngp_path)
    g = Glb()
    stats = {"objetos": 0, "triangulos": 0, "skinned_fora": 0, "sprites_fora": 0, "sem_textura": 0, "texturas": 0}
    if unlit:
        g.j["extensionsUsed"] = ["KHR_materials_unlit"]
    material = Materials(g, img, lib, unlit, stats).get

    seen_obj = set()

    def visit(node, mtx, ctx=None):
        mtx = [list(map(float, r)) for r in mtx]
        key = (node, tuple(map(tuple, mtx)))
        if key in seen_obj:
            return
        seen_obj.add(key)
        md = ngp.decode_mesh(img, node)
        stats["sprites_fora"] += len(md.sprites)
        if md.skinned and not skinned:
            stats["skinned_fora"] += 1
            return
        if not md.tris:
            return
        groups = {}
        for k in range(0, len(md.tris), 3):
            groups.setdefault(md.tris[k + 2].slot, []).append(md.tris[k:k + 3])
        prims = []
        rot = [r[:3] for r in mtx[:3]]
        pal = model.palette_of(node, ctx) if (model is not None and md.skinned) else None
        for slot, tris in sorted(groups.items()):
            verts, index, idx = {}, [], []
            for tri in tris:
                for v in tri:
                    sp = tuple(pose_mod.skin(pal, v)) if pal else (v.x, v.y, v.z)
                    p = to_y_up(ngp.xform(mtx, sp))
                    n = (v.nx * rot[0][0] + v.ny * rot[1][0] + v.nz * rot[2][0], v.nx * rot[0][1] + v.ny * rot[1][1] + v.nz * rot[2][1],
                         v.nx * rot[0][2] + v.ny * rot[1][2] + v.nz * rot[2][2])
                    ln = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) ** 0.5
                    n = to_y_up((n[0] / ln, n[1] / ln, n[2] / ln)) if ln > 1e-6 else (0.0, 1.0, 0.0)
                    c = (min(1.0, v.r / 128.0), min(1.0, v.g / 128.0), min(1.0, v.b / 128.0), min(1.0, v.a / 128.0) if md.abe else 1.0)
                    k = (p, n, (v.u, v.v), c)
                    if k not in verts:
                        verts[k] = len(verts)
                        index.append(k)
                    idx.append(verts[k])
            attrs = {"POSITION": g.accessor("f", 3, [k[0] for k in index], minmax=True),
                     "NORMAL": g.accessor("f", 3, [k[1] for k in index]),
                     "COLOR_0": g.accessor("f", 4, [k[3] for k in index])}
            if slot != ngp.SLOT_NOTEX and lib is not None:
                attrs["TEXCOORD_0"] = g.accessor("f", 2, [k[2] for k in index])
            ctype, fmt = (5123, "H") if len(index) < 65536 else (5125, "I")
            prims.append({"attributes": attrs, "indices": g.accessor(fmt, 1, idx, ctype=ctype, target=34963),
                          "material": material(node, slot, md.abe)})
            stats["triangulos"] += len(tris)
        g.j["meshes"].append({"name": "obj_%x" % node, "primitives": prims})
        g.j["nodes"].append({"name": "obj_%x" % node, "mesh": len(g.j["meshes"]) - 1})
        g.j["scenes"][0]["nodes"].append(len(g.j["nodes"]) - 1)
        stats["objetos"] += 1

    if items is not None:   # pose montada pelo tools/pose.py: (OBJECT, matriz, instancia)
        for node, mtx, ctx in items:
            visit(node, mtx, ctx)
    else:
        for r in img.roots():
            ngp.walk(img, r, visit, lod=lod)
    g.write(out)
    if not quiet:
        print("gravado %s: %d objetos, %d triangulos, %d texturas (%d nao achadas); fora: %d skinned, %d sprites" % (
            out, stats["objetos"], stats["triangulos"], stats["texturas"], stats["sem_textura"], stats["skinned_fora"],
            stats["sprites_fora"]))
    return stats


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    args, opts, i = [], {}, 0
    while i < len(argv):
        a = argv[i]
        if a in ("--lod", "--tex", "--rtx"):
            opts[a[2:]] = argv[i + 1]
            i += 2
            continue
        if a in ("--skinned", "--no-tex", "--lit"):
            opts[a[2:]] = True
        else:
            args.append(a)
        i += 1
    export(args[0], args[1], lod=int(opts.get("lod", 99)), skinned=opts.get("skinned", False), tex_path=opts.get("tex"),
           rtx_path=opts.get("rtx"), use_tex=not opts.get("no-tex", False), unlit=not opts.get("lit", False))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
