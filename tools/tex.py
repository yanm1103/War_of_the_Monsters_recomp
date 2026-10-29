#!/usr/bin/env python3
"""Texturas do WotM (.TEX e .RTX): lista, exporta para PNG e reinjeta PNG/TGA. Formato em docs/FORMATS.md ("Texturas").

  python tools/tex.py list  disc/MON/CONGAR1.TEX                   # pacotes do arquivo (texId, formato, tamanho)
  python tools/tex.py export disc/MON/CONGAR1.TEX saida/           # PNGs + manifest.json (acha .RTX e .NGP irmaos)
  python tools/tex.py export disc/LVL/CENTRAL.TEX saida/ --rtx X.RTX --ngp X.NGP
  python tools/tex.py inject disc/MON/CONGAR1.TEX 12 nova.png novo/CONGAR1.TEX [--rtx in.RTX --rtx-out novo/CONGAR1.RTX]
                                                                   [--new-palette]

Como o jogo le (src/common/texm.cpp, texmLoadTexture/texmLoadResTexture/texmRelocate/texmResRelocate):
- Arquivo: cabecalho `_texfile` {u32 numSizes, u32 headerQw, {u32 count, u32 blkSize} x numSizes}; o primeiro pacote fica em
  headerQw * 16. Alguns arquivos vem num zip de um arquivo so com o magic "IE\\3\\4" (zipInflateAll).
- Pacote `TexPkt` (cabecalho de 16 bytes): u32 size (o proximo esta (size >> 2) * 16 bytes adiante; 0 = ultimo), u32 pad,
  u16 texId, u16 tbp (bloco da VRAM, 64 palavras), e um u32 de bits: psm 0..5, noUpload 6, skip 7, logW 8..11, logH 12..15,
  numMips 16..18, unk19 19..22, unk23 23..26, bin 27..31.
- Pixels lineares (linhas de w pixels; T4 com o nibble baixo primeiro). No .TEX comecam em +0x80 (o jogo monta o
  sceGsLoadImage em +0x20). No .RTX: paletas e outras texturas comuns em +0x10; os formatos "altos" (T8H/T4HL/T4HH: telas do
  menu, fontes, retratos) em +0x80.
- Paleta: entrada do .RTX em CT32, 16x16 (256 cores) ou 8x2 (16 cores), achada pelo CBP do TEX0 do objeto (tbp da entrada).
  CLUT de 256 cores em CSM1 tem os bits 3 e 4 do indice trocados. Alfa do GS: 0x80 = 1,0.
- Cada arquivo e relocado na carga (texId e tbp somam o maximo do arquivo anterior); o .NGP do mesmo arquivo pode guardar os ids
  ja somados ou nao. O export acha o deslocamento que mais liga objetos a pacotes de mesmo formato e tamanho (e a paletas do
  tamanho certo); o manifest.json grava os dois deslocamentos achados.
- Mipmaps (numMips > 0): so o nivel 0 sai em PNG.

Nao grava nada do jogo no repo: le o disco do usuario e escreve onde ele mandar. O decodificador segue native/wotm_gs.hpp
(decodeUpload, fillPal); conferido contra ele com dados aleatorios.
"""
import collections
import json
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

PSM_NAMES = {0x00: "CT32", 0x01: "CT24", 0x02: "CT16", 0x0A: "CT16S", 0x13: "T8", 0x14: "T4", 0x1B: "T8H", 0x24: "T4HL",
             0x2C: "T4HH"}
CT32, CT24, CT16, CT16S, T8, T4, T8H, T4HL, T4HH = 0x00, 0x01, 0x02, 0x0A, 0x13, 0x14, 0x1B, 0x24, 0x2C
HIGH = (T8H, T4HL, T4HH)


# ---------------------------------------------------------------- zip "IE"

def unzip_ie(raw):
    """Devolve (dados, info). info = None se o arquivo nao veio em zip; senao, o que precisa para regravar igual."""
    if raw[:4] != b"IE\x03\x04":
        return raw, None
    method = struct.unpack_from("<H", raw, 8)[0]
    csize, usize = struct.unpack_from("<II", raw, 18)
    nlen, xlen = struct.unpack_from("<HH", raw, 26)
    off = 30 + nlen + xlen
    if method != 8:
        raise ValueError("zip com metodo %d (so deflate)" % method)
    data = zlib.decompressobj(-15).decompress(raw[off:off + csize])
    if len(data) != usize:
        raise ValueError("zip: %d bytes, esperava %d" % (len(data), usize))
    return data, {"header": raw[:off], "tail": raw[off + csize:]}


def zip_ie(data, info):
    """Regrava no mesmo formato do original: cabecalho local com crc/tamanhos novos e o resto (diretorio central, se houver)
    com os mesmos campos corrigidos. O jogo (zipCheckHeader) so le o cabecalho local."""
    if info is None:
        return data
    co = zlib.compressobj(9, zlib.DEFLATED, -15)
    comp = co.compress(data) + co.flush()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    hdr = bytearray(info["header"])
    struct.pack_into("<III", hdr, 14, crc, len(comp), len(data))
    tail = bytearray(info["tail"])
    delta = len(comp) - struct.unpack_from("<I", info["header"], 18)[0]
    for sig in (b"IE\x01\x02", b"PK\x01\x02"):
        p = tail.find(sig)
        if p >= 0 and p + 46 <= len(tail):
            struct.pack_into("<III", tail, p + 16, crc, len(comp), len(data))
    for sig in (b"IE\x05\x06", b"PK\x05\x06"):
        p = tail.find(sig)
        if p >= 0 and p + 22 <= len(tail):
            cd = struct.unpack_from("<I", tail, p + 16)[0]
            struct.pack_into("<I", tail, p + 16, cd + delta)
    return bytes(hdr) + comp + bytes(tail)


def read_file(path):
    with open(path, "rb") as f:
        return unzip_ie(f.read())


# ---------------------------------------------------------------- .TEX / .RTX

class Packet:
    __slots__ = ("off", "size", "texId", "tbp", "psm", "noUpload", "skip", "logW", "logH", "numMips", "unk19", "unk23", "bin",
                 "end", "last")

    @property
    def w(self):
        return 1 << self.logW

    @property
    def h(self):
        return 1 << self.logH

    def is_palette(self):
        return self.psm == CT32 and (self.logW, self.logH) in ((4, 4), (3, 1))

    def data_off(self, rtx):
        """Inicio dos pixels: +0x80 no .TEX; no .RTX, +0x80 nos formatos altos e +0x10 no resto (texmLoadResTexture)."""
        if rtx and self.psm not in HIGH:
            return self.off + 0x10
        return self.off + 0x80


def parse(data):
    """Cabecalho e pacotes de um .TEX ou .RTX ja descompactado."""
    if len(data) < 8:
        raise ValueError("arquivo curto demais")
    num_sizes, header_qw = struct.unpack_from("<II", data, 0)
    sizes = [struct.unpack_from("<II", data, 8 + 8 * i) for i in range(min(num_sizes, 64)) if 16 + 8 * i <= len(data)]
    pkts = []
    off = header_qw * 16
    while off + 16 <= len(data) and len(pkts) < 4096:
        size, _pad, tex_id, tbp, bits = struct.unpack_from("<IIHHI", data, off)
        p = Packet()
        p.off, p.size, p.texId, p.tbp = off, size, tex_id, tbp
        p.psm, p.noUpload, p.skip = bits & 0x3F, (bits >> 6) & 1, (bits >> 7) & 1
        p.logW, p.logH, p.numMips = (bits >> 8) & 15, (bits >> 12) & 15, (bits >> 16) & 7
        p.unk19, p.unk23, p.bin = (bits >> 19) & 15, (bits >> 23) & 15, (bits >> 27) & 31
        p.last = size == 0
        p.end = len(data) if p.last else off + (size >> 2) * 16
        if p.end <= off or p.end > len(data):
            raise ValueError("pacote em %#x com tamanho invalido (%#x)" % (off, size))
        pkts.append(p)
        if p.last:
            break
        off = p.end
    return {"numSizes": num_sizes, "headerQw": header_qw, "sizes": sizes, "packets": pkts}


def pixel_bytes(psm, w, h):
    if psm in (T4, T4HL, T4HH):
        return (w * h + 1) // 2
    if psm in (T8, T8H):
        return w * h
    if psm in (CT16, CT16S):
        return w * h * 2
    return w * h * 4


# ---------------------------------------------------------------- pixels (como native/wotm_gs.hpp)

def fix_alpha(a, hi=False):
    """Alfa do GS -> PNG. Comum: 0x80 (1,0) = 255, resto * 2. hi (textura com alfa acima de 0x80, como o logo do titulo, que vai
    a 0xFF e o jogo desenha com o vertice em meia opacidade): o PNG guarda o alfa cru, 255 = 0xFF. Mesma regra do
    native/wotm_texmods.hpp (loadPng), para o export ida e volta nao perder nada."""
    if hi:
        return a
    return 255 if a >= 128 else a * 2


def raw_hi(px_raw):
    """Pixels decodificados com hi=True: a textura usa alfa acima de 0x80 (o PNG dela vai cru)?"""
    return any(c[3] > 0x80 for c in px_raw)


def from16(v):
    r, g, b = (v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, ((v >> 10) & 31) * 255 // 31
    return (r, g, b, 255 if v & 0x8000 else 0)


def csm1(i):
    """Indice logico -> posicao no upload de uma CLUT de 256 cores (bits 3 e 4 trocados)."""
    return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)


def palette_from_entry(data, p, rtx=True, swap=True, hi=False):
    """Paleta RGBA (alfa 0..255; hi: cru) de uma entrada CT32 do .RTX, ja na ordem logica."""
    n = p.w * p.h
    o = p.data_off(rtx)
    raw = [struct.unpack_from("<4B", data, o + 4 * k) if o + 4 * k + 4 <= len(data) else (0, 0, 0, 0) for k in range(n)]
    if n >= 256 and swap:   # CSM1: so nas texturas de 8 bits (como o fillPal do nativo)
        return [(r, g, b, fix_alpha(a, hi)) for (r, g, b, a) in (raw[csm1(i)] for i in range(256))]
    return [(r, g, b, fix_alpha(a, hi)) for (r, g, b, a) in raw]


def gray_palette(n):
    return [(v, v, v, 255) for v in (i * 255 // (n - 1) for i in range(n))]


def decode(buf, off, psm, w, h, pal=None, csa=0, hi=False):
    """Pixels lineares -> lista de (r,g,b,a) com alfa 0..255 (hi: CT32 com alfa cru). pal: 256 (T8) ou 16+ (T4) cores."""
    out = []
    if psm in (T8, T8H):
        for n in range(w * h):
            out.append(pal[buf[off + n]] if off + n < len(buf) else (0, 0, 0, 0))
    elif psm in (T4, T4HL, T4HH):
        for n in range(w * h):
            b = buf[off + n // 2] if off + n // 2 < len(buf) else 0
            k = (b >> 4) if n & 1 else (b & 15)
            idx = (csa * 16 + k) & 255
            out.append(pal[idx] if idx < len(pal) else pal[k])
    elif psm == CT32:
        for n in range(w * h):
            r, g, b, a = struct.unpack_from("<4B", buf, off + 4 * n)
            out.append((r, g, b, fix_alpha(a, hi)))
    elif psm == CT24:
        for n in range(w * h):
            r, g, b, _ = struct.unpack_from("<4B", buf, off + 4 * n)
            out.append((r, g, b, 255))
    elif psm in (CT16, CT16S):
        for n in range(w * h):
            out.append(from16(struct.unpack_from("<H", buf, off + 2 * n)[0]))
    else:
        raise ValueError("formato %#x nao suportado" % psm)
    return out


# ---------------------------------------------------------------- PNG e TGA (sem dependencias)

def png_bytes(w, h, px):
    """PNG RGBA de 8 bits (sem filtro) em memoria."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for (r, g, b, a) in px[y * w:(y + 1) * w]:
            raw += bytes((r, g, b, a))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    return png + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


def write_png(path, w, h, px):
    with open(path, "wb") as f:
        f.write(png_bytes(w, h, px))


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(path):
    """PNG de 8 bits sem entrelacamento: tons de cinza, RGB, paleta, cinza+alfa e RGBA. Devolve (w, h, [(r,g,b,a)])."""
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: nao e PNG" % path)
    o, idat, plte, trns = 8, b"", None, None
    w = h = depth = ctype = inter = 0
    while o < len(d):
        n, t = struct.unpack_from(">I4s", d, o)
        body = d[o + 8:o + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
        elif t == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif t == b"tRNS":
            trns = body
        elif t == b"IDAT":
            idat += body
        elif t == b"IEND":
            break
        o += 12 + n
    if inter or depth not in (1, 2, 4, 8, 16) or (depth < 8 and ctype not in (0, 3)):
        raise ValueError("%s: PNG entrelacado ou com profundidade incomum (salve de novo como RGBA 8 bits)" % path)
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, chans * depth // 8)            # bytes por pixel para o filtro (1 nas profundidades < 8)
    raw = zlib.decompress(idat)
    stride = (w * chans * depth + 7) // 8
    rows, prev, o = [], bytearray(stride), 0
    for _ in range(h):
        f, line = raw[o], bytearray(raw[o + 1:o + 1 + stride])
        o += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], (prev[i - bpp] if i >= bpp else 0)
            line[i] = (line[i] + (0, a, b, (a + b) >> 1, _paeth(a, b, c))[f]) & 255
        rows.append(line)
        prev = line
    px = []
    for line in rows:
        if depth < 8:      # amostras empacotadas, a primeira no bit mais alto
            per = 8 // depth
            vals = [(line[x // per] >> (8 - depth * (x % per + 1))) & ((1 << depth) - 1) for x in range(w)]
            if ctype == 0:
                vals = [v * 255 // ((1 << depth) - 1) for v in vals]
            line = bytearray(vals)
        elif depth == 16:  # fica o byte alto de cada amostra
            line = line[0::2]
        for x in range(w):
            s = line[x * chans:(x + 1) * chans]
            if ctype == 0:
                px.append((s[0], s[0], s[0], 255))
            elif ctype == 2:
                px.append((s[0], s[1], s[2], 255))
            elif ctype == 3:
                r, g, b = plte[s[0]]
                px.append((r, g, b, trns[s[0]] if trns and s[0] < len(trns) else 255))
            elif ctype == 4:
                px.append((s[0], s[0], s[0], s[1]))
            else:
                px.append(tuple(s))
    return w, h, px


def read_tga(path):
    """TGA sem compressao ou RLE, 24 ou 32 bits (cor verdadeira) ou 8 bits em tons de cinza."""
    d = open(path, "rb").read()
    idlen, cmtype, itype = d[0], d[1], d[2]
    w, h, bits, desc = struct.unpack_from("<HHBB", d, 12)
    if cmtype or itype not in (2, 3, 10, 11) or bits not in (8, 24, 32):
        raise ValueError("%s: TGA nao suportado (use 24/32 bits ou cinza de 8 bits)" % path)
    bpp = bits // 8
    o = 18 + idlen
    flat = bytearray()
    if itype in (2, 3):
        flat = d[o:o + w * h * bpp]
    else:
        while len(flat) < w * h * bpp:
            c = d[o]
            o += 1
            n = (c & 0x7F) + 1
            if c & 0x80:
                flat += d[o:o + bpp] * n
                o += bpp
            else:
                flat += d[o:o + bpp * n]
                o += bpp * n
    px = []
    for k in range(w * h):
        s = flat[k * bpp:(k + 1) * bpp]
        if bpp == 1:
            px.append((s[0], s[0], s[0], 255))
        else:
            px.append((s[2], s[1], s[0], s[3] if bpp == 4 else 255))
    if not desc & 0x20:   # origem embaixo
        px = [p for y in range(h - 1, -1, -1) for p in px[y * w:(y + 1) * w]]
    return w, h, px


def read_image(path):
    return read_tga(path) if path.lower().endswith(".tga") else read_png(path)


# ---------------------------------------------------------------- objetos do .NGP (quem usa cada textura)

def ngp_texrefs(path):
    """[(texId, tex0)] de todos os objetos com textura do .NGP (todos os LODs)."""
    import ngp
    img = ngp.Image(ngp.inflate_ngp(path), 0)
    img.base = guess_base(img)
    refs = set()

    def visit(node, _m):
        if not img.ok(node, 0xA0):
            return
        n = img.u16(node + 0x5A)
        for s in range(min(n, 5)):
            if not img.ok(node + 0x78 + 0x30 * s, 0):
                break
            t0 = img.u32(node + 0x70 + 0x30 * s) | (img.u32(node + 0x74 + 0x30 * s) << 32)
            refs.add((img.u16(node + 0x50 + 2 * s), t0))
    for lod in (0, 1, 2, 3, 99):
        for r in img.roots():
            ngp.walk(img, r, visit, lod=lod)
    return sorted(refs)


def guess_base(img):
    """Fase e menu vem pre-ligados para 0xA00000; monstros vem com ponteiros relativos (relocados pelo .ptr)."""
    n = struct.unpack_from("<I", img.d, 0)[0]
    roots = [struct.unpack_from("<I", img.d, 4 + 4 * i)[0] for i in range(min(n, 256))]
    if roots and all(0 < r < len(img.d) for r in roots):
        return 0
    return 0xA00000


def tex0_fields(v):
    return {"tbp0": v & 0x3FFF, "psm": (v >> 20) & 0x3F, "tw": (v >> 26) & 15, "th": (v >> 30) & 15, "tcc": (v >> 34) & 1,
            "cbp": (v >> 37) & 0x3FFF, "cpsm": (v >> 51) & 15, "csa": (v >> 56) & 31}


def best_offset(candidates, score):
    """Deslocamento com a maior pontuacao; empate fica com o menor em modulo (0 primeiro). Sem candidatos, 0."""
    best, bs = 0, score(0) if candidates else 0
    top = [d for d, _ in collections.Counter(candidates).most_common(64)]   # os mais frequentes bastam
    for d in sorted(set(top), key=abs):
        s = score(d)
        if s > bs:
            best, bs = d, s
    return best


# ---------------------------------------------------------------- comandos

def sibling(path, ext):
    stem = os.path.splitext(path)[0]
    for e in (ext.upper(), ext.lower()):
        if os.path.exists(stem + e):
            return stem + e
    return None


def cmd_list(path):
    data, z = read_file(path)
    t = parse(data)
    rtx = path.upper().endswith(".RTX")
    print("%s: %d bytes%s, %d faixas de tamanho, %d pacotes" % (path, len(data), " (zip)" if z else "", t["numSizes"],
                                                                  len(t["packets"])))
    for p in t["packets"]:
        kind = "paleta" if rtx and p.is_palette() else ""
        print("  %#07x texId=%4d tbp=%5d %-5s %4dx%-4d mips=%d bin=%2d%s%s %s" % (
            p.off, p.texId, p.tbp, PSM_NAMES.get(p.psm, hex(p.psm)), p.w, p.h, p.numMips, p.bin,
            " skip" if p.skip else "", " noUpload" if p.noUpload else "", kind))


class Pairing:
    """Liga cada textura do .TEX a uma paleta do .RTX pelos objetos do .NGP."""

    def __init__(self, tex, rtx_data, rtx, refs):
        self.tex, self.rtx_data, self.rtx = tex, rtx_data, rtx
        self.pals = {p.tbp: p for p in (rtx["packets"] if rtx else []) if p.is_palette()}
        by_id = {p.texId: p for p in tex["packets"]}

        def tex_ok(r, d):
            p = by_id.get(r[0] - d)
            f = tex0_fields(r[1])
            return p is not None and f["psm"] == p.psm and f["tw"] == p.logW and f["th"] == p.logH

        self.dtex = best_offset([r[0] - t for r in refs for t in by_id], lambda d: sum(tex_ok(r, d) for r in refs))
        ok = [r for r in refs if tex_ok(r, self.dtex)]

        def pal_ok(r, d):
            f = tex0_fields(r[1])
            e = self.pals.get(f["cbp"] - d)
            if e is None:
                return False
            n = e.w * e.h
            return n == 256 if f["psm"] in (T8, T8H) else True   # T4 usa CLUT de 16, ou um trecho (csa) de uma de 256

        self.dcbp = best_offset([tex0_fields(r[1])["cbp"] - b for r in ok for b in self.pals],
                                lambda d: sum(pal_ok(r, d) for r in ok))
        self.uses = collections.defaultdict(set)   # texId (do arquivo) -> {(cbp do arquivo, csa)}
        for tid, t0 in ok:
            f = tex0_fields(t0)
            self.uses[tid - self.dtex].add((f["cbp"] - self.dcbp, f["csa"]))

    def palette(self, p, hi=False):
        """(paleta, tbp da paleta, csa, todas as combinacoes vistas) ou (None, ...) se nao achou. hi: alfa cru."""
        combos = sorted(self.uses.get(p.texId, ()))
        for cbp, csa in combos:
            e = self.pals.get(cbp)
            if e is not None:
                return palette_from_entry(self.rtx_data, e, swap=p.psm in (T8, T8H), hi=hi), cbp, csa, combos
        if len(self.pals) == 1 and not combos:   # arquivo com uma paleta so
            cbp, e = next(iter(self.pals.items()))
            return palette_from_entry(self.rtx_data, e, swap=p.psm in (T8, T8H), hi=hi), cbp, 0, combos
        return None, None, 0, combos


class Library:
    """Texturas de um arquivo (.TEX + .RTX) para quem desenha os objetos do .NGP: `image(texId, tex0)` com os numeros como
    estao no objeto. Usa os mesmos deslocamentos do export (Pairing)."""

    def __init__(self, tex_path=None, rtx_path=None, ngp_path=None, refs=None):
        self.src = {}   # id(pacote) -> (dados, e do .RTX)
        pkts = []
        tex_data = rtx_data = rtx = None
        if tex_path:
            tex_data, _ = read_file(tex_path)
            for p in parse(tex_data)["packets"]:
                pkts.append(p)
                self.src[id(p)] = (tex_data, False)
        if rtx_path:
            rtx_data, _ = read_file(rtx_path)
            rtx = parse(rtx_data)
            for p in rtx["packets"]:
                if not p.is_palette():
                    pkts.append(p)
                    self.src[id(p)] = (rtx_data, True)
        if refs is None:
            refs = ngp_texrefs(ngp_path) if ngp_path else []
        self.pair = Pairing({"packets": pkts}, rtx_data, rtx, refs)
        self.by_id = {}
        for p in pkts:
            self.by_id.setdefault(p.texId, p)
        self.cache = {}

    def image(self, tex_id, tex0):
        """(w, h, [(r,g,b,a)]) da textura que o objeto usa, ou None. TCC = 0 (so RGB) sai com alfa 255, como no jogo."""
        key = (tex_id, tex0)
        if key in self.cache:
            return self.cache[key]
        res = None
        p = self.by_id.get(tex_id - self.pair.dtex)
        f = tex0_fields(tex0)
        if p is not None:
            data, is_rtx = self.src[id(p)]
            psm = p.psm
            try:
                if psm in (T8, T4, T8H, T4HL, T4HH):
                    e = self.pair.pals.get(f["cbp"] - self.pair.dcbp)
                    if e is not None:
                        pal = palette_from_entry(self.pair.rtx_data, e, swap=psm in (T8, T8H))
                        csa = f["csa"] if len(pal) >= 256 else 0
                        px = decode(data, p.data_off(is_rtx), T8 if psm in (T8, T8H) else T4, p.w, p.h, pal, csa)
                        res = (p.w, p.h, px)
                elif psm in (CT32, CT24, CT16, CT16S):
                    res = (p.w, p.h, decode(data, p.data_off(is_rtx), psm, p.w, p.h))
            except (struct.error, IndexError):
                res = None
            if res and not f["tcc"]:
                res = (res[0], res[1], [(r, g, b, 255) for (r, g, b, _a) in res[2]])
        self.cache[key] = res
        return res


def cmd_export(path, outdir, rtx_path=None, ngp_path=None):
    os.makedirs(outdir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(path))[0].lower()
    data, _ = read_file(path)
    is_rtx_main = path.upper().endswith(".RTX")
    tex = parse(data)
    rtx_path = rtx_path or (None if is_rtx_main else sibling(path, ".RTX"))
    ngp_path = ngp_path or sibling(path, ".NGP")
    rtx_data, rtx = (None, None)
    if is_rtx_main:
        rtx_data, rtx = data, tex
    elif rtx_path:
        rtx_data, _ = read_file(rtx_path)
        rtx = parse(rtx_data)
    refs = ngp_texrefs(ngp_path) if ngp_path else []
    pair = Pairing(tex, rtx_data, rtx, refs)
    man = {"file": os.path.basename(path), "rtx": os.path.basename(rtx_path) if rtx_path else None,
           "ngp": os.path.basename(ngp_path) if ngp_path else None, "texIdOffsetInNgp": pair.dtex,
           "cbpOffsetInNgp": pair.dcbp, "textures": []}
    done = semp = 0
    for p in tex["packets"]:
        if is_rtx_main and p.is_palette():
            continue
        ent = {"texId": p.texId, "psm": PSM_NAMES.get(p.psm, hex(p.psm)), "w": p.w, "h": p.h, "tbp": p.tbp, "mips": p.numMips,
               "offset": p.off}
        rtx_side = is_rtx_main
        off = p.data_off(rtx_side)
        hi = False   # alfa acima de 0x80: o PNG vai cru (fix_alpha)
        if p.psm in (T8, T4, T8H, T4HL, T4HH):
            pal, cbp, csa, combos = pair.palette(p, hi=True)
            ent["palettesSeen"] = [{"tbp": c, "csa": s} for c, s in combos]
            if pal is None:
                pal = gray_palette(256 if p.psm in (T8, T8H) else 16)
                ent["palette"] = None
                semp += 1
            else:
                ent["palette"] = {"tbp": cbp, "csa": csa}
            if p.psm in (T4, T4HL, T4HH) and len(pal) < 256:
                csa = 0
            px = decode(data, off, p.psm if p.psm not in HIGH else (T8 if p.psm == T8H else T4), p.w, p.h, pal, csa)
            hi = ent["palette"] is not None and raw_hi(px)
        elif p.psm in (CT32, CT24, CT16, CT16S):
            px = decode(data, off, p.psm, p.w, p.h, hi=True)
            hi = p.psm == CT32 and raw_hi(px)
        else:
            ent["error"] = "formato nao suportado"
            man["textures"].append(ent)
            continue
        if hi:
            ent["alpha"] = "raw"   # 255 no PNG = 0xFF do GS (o dobro do opaco comum)
        elif p.psm != CT24 and p.psm not in (CT16, CT16S) and ent.get("palette", True) is not None:
            px = [(r, g, b, fix_alpha(a)) for (r, g, b, a) in px]   # decodificado cru acima: 0x80 -> 255
        # Mesmo nome que o desenho nativo procura em mods/ (native/wotm_texmods.hpp): <arquivo>_t<id> do .TEX, <arquivo>_rtx_t<id> do .RTX
        name = "%s%s_t%04d%s.png" % (stem, "_rtx" if is_rtx_main else "", p.texId,
                                     "" if ent.get("palette", True) is not None else "_sempaleta")
        write_png(os.path.join(outdir, name), p.w, p.h, px)
        ent["png"] = name
        man["textures"].append(ent)
        done += 1
    if rtx is not None and not is_rtx_main:
        man["palettes"] = [{"tbp": e.tbp, "colors": e.w * e.h} for e in rtx["packets"] if e.is_palette()]
    with open(os.path.join(outdir, stem + ("_rtx" if is_rtx_main else "") + "_manifest.json"), "w", encoding="utf-8") as f:
        json.dump(man, f, indent=1, ensure_ascii=False)
    print("%d texturas em %s (%d sem paleta: em tons de cinza)%s" % (done, outdir, semp,
                                                                     "" if refs else "; sem .NGP, paletas so por arquivo de uma paleta"))
    return man


# ---------------------------------------------------------------- reinjecao

def to_gs_alpha(a, hi=False):
    """Alfa 0..255 do PNG -> GS (0x80 = 1,0; hi: cru). Inverso de fix_alpha nos valores que ele produz."""
    if hi:
        return a
    return 0x80 if a >= 255 else a // 2


def nearest(pal, c):
    best, bd = 0, 1 << 40
    for i, q in enumerate(pal):
        d = (q[0] - c[0]) ** 2 + (q[1] - c[1]) ** 2 + (q[2] - c[2]) ** 2 + 2 * (q[3] - c[3]) ** 2
        if d < bd:
            best, bd = i, d
            if d == 0:
                break
    return best


def median_cut(px, n):
    """Paleta de ate n cores (corte pela mediana no canal de maior amplitude)."""
    boxes = [list(set(px))]
    while len(boxes) < n:
        boxes.sort(key=lambda b: max(max(c[k] for c in b) - min(c[k] for c in b) for k in range(4)) if len(b) > 1 else -1)
        b = boxes.pop()
        if len(b) < 2:
            boxes.append(b)
            break
        k = max(range(4), key=lambda k: max(c[k] for c in b) - min(c[k] for c in b))
        b.sort(key=lambda c: c[k])
        boxes += [b[:len(b) // 2], b[len(b) // 2:]]
    pal = [tuple(sum(c[k] for c in b) // len(b) for k in range(4)) for b in boxes if b]
    return pal + [(0, 0, 0, 0)] * (n - len(pal))


def encode(px, psm, pal=None, csa=0, hi=False):
    """(r,g,b,a) -> bytes lineares do formato. Indexados usam a paleta dada (indice mais proximo). hi: alfa do PNG cru (CT32)."""
    out = bytearray()
    if psm in (T8, T8H):
        cache = {}
        for c in px:
            if c not in cache:
                cache[c] = nearest(pal, c)
            out.append(cache[c])
    elif psm in (T4, T4HL, T4HH):
        sub = pal[csa * 16:csa * 16 + 16] if len(pal) > 16 else pal
        cache = {}
        idx = []
        for c in px:
            if c not in cache:
                cache[c] = nearest(sub, c)
            idx.append(cache[c])
        if len(idx) & 1:
            idx.append(0)
        for i in range(0, len(idx), 2):
            out.append(idx[i] | (idx[i + 1] << 4))
    elif psm == CT32:
        for (r, g, b, a) in px:
            out += bytes((r, g, b, to_gs_alpha(a, hi)))
    elif psm == CT24:
        for (r, g, b, _a) in px:
            out += bytes((r, g, b, 0))
    elif psm in (CT16, CT16S):
        for (r, g, b, a) in px:
            out += struct.pack("<H", (r * 31 + 127) // 255 | ((g * 31 + 127) // 255) << 5 | ((b * 31 + 127) // 255) << 10 |
                               (0x8000 if a >= 128 else 0))
    else:
        raise ValueError("formato %#x nao suportado" % psm)
    return bytes(out)


def write_palette(buf, e, pal, swap=True, hi=False):
    """Grava a paleta (ordem logica, alfa 0..255; hi: cru) na entrada CT32 do .RTX."""
    o = e.data_off(True)
    n = e.w * e.h
    for i in range(min(n, len(pal))):
        k = csm1(i) if n >= 256 and swap else i
        r, g, b, a = pal[i]
        buf[o + 4 * k:o + 4 * k + 4] = bytes((r, g, b, to_gs_alpha(a, hi)))


def cmd_inject(path, tex_id, image, out, rtx_path=None, rtx_out=None, ngp_path=None, new_palette=False):
    """Troca os pixels de uma textura (mesmo tamanho e formato). Indexadas: cores mais proximas da paleta atual, ou paleta nova
    (--new-palette, grava o .RTX; so se nenhuma outra textura usar a mesma paleta)."""
    raw_data, zinfo = read_file(path)
    data = bytearray(raw_data)
    is_rtx_main = path.upper().endswith(".RTX")
    tex = parse(bytes(data))
    p = next((q for q in tex["packets"] if q.texId == tex_id and not (is_rtx_main and q.is_palette())), None)
    if p is None:
        raise SystemExit("texId %d nao existe em %s" % (tex_id, path))
    w, h, px = read_image(image)
    if (w, h) != (p.w, p.h):
        raise SystemExit("a imagem tem %dx%d; a textura %d tem %dx%d (redimensione antes)" % (w, h, tex_id, p.w, p.h))
    psm = p.psm
    pal = None
    csa = 0
    hi = False   # a original usa alfa acima de 0x80: o PNG vem cru (fix_alpha)
    rtx_data = rtx = e = None
    if psm in (T8, T4, T8H, T4HL, T4HH):
        rtx_path = rtx_path or (path if is_rtx_main else sibling(path, ".RTX"))
        ngp_path = ngp_path or sibling(path, ".NGP")
        if not rtx_path:
            raise SystemExit("textura indexada: preciso do .RTX (--rtx)")
        rtx_bytes, rtx_z = read_file(rtx_path)
        rtx_data = bytearray(rtx_bytes)
        rtx = parse(bytes(rtx_data))
        pair = Pairing(tex, bytes(rtx_data), rtx, ngp_texrefs(ngp_path) if ngp_path else [])
        cur, cbp, csa, _ = pair.palette(p, hi=True)
        if cur is None:
            raise SystemExit("nao achei a paleta da textura %d (passe o .NGP com --ngp)" % tex_id)
        dpsm = psm if psm not in HIGH else (T8 if psm == T8H else T4)
        hi = raw_hi(decode(bytes(data), p.data_off(is_rtx_main), dpsm, p.w, p.h, cur, csa if len(cur) >= 256 else 0))
        if not hi:
            cur = [(r, g, b, fix_alpha(a)) for (r, g, b, a) in cur]
        e = pair.pals[cbp]
        n = 256 if psm in (T8, T8H) else 16
        if new_palette:
            shared = [q.texId for q in tex["packets"] if q is not p and any(c == cbp for c, _ in pair.uses.get(q.texId, ()))]
            if shared:
                raise SystemExit("a paleta %d tambem serve as texturas %s; sem --new-palette a imagem usa as cores atuais" %
                                 (cbp, shared))
            newp = median_cut(px, n)
            if e.w * e.h > n:   # T4 dentro de uma CLUT de 256: so a faixa do csa muda
                cur = list(cur)
                cur[csa * 16:csa * 16 + 16] = newp
                newp = cur
            write_palette(rtx_data, e, newp, swap=psm in (T8, T8H), hi=hi)
            pal = newp
        else:
            pal = cur
    elif psm == CT32:
        hi = raw_hi(decode(bytes(data), p.data_off(is_rtx_main), psm, p.w, p.h, hi=True))
    enc = encode(px, psm if psm not in HIGH else (T8 if psm == T8H else T4), pal, csa, hi)
    o = p.data_off(is_rtx_main)
    need = pixel_bytes(psm, p.w, p.h)
    if o + need > p.end:
        raise SystemExit("pacote menor que os pixels (%d > %d)" % (need, p.end - o))
    data[o:o + need] = enc[:need]
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as f:
        f.write(zip_ie(bytes(data), zinfo))
    msg = "gravado %s" % out
    if new_palette and rtx_data is not None:
        rtx_out = rtx_out or os.path.join(os.path.dirname(os.path.abspath(out)), os.path.basename(rtx_path))
        with open(rtx_out, "wb") as f:
            f.write(zip_ie(bytes(rtx_data), rtx_z))
        msg += " e %s (paleta nova)" % rtx_out
    print(msg)


def main(argv):
    if len(argv) < 2 or argv[0] not in ("list", "export", "inject"):
        print(__doc__)
        return 1
    opts, args, i = {}, [], 1
    while i < len(argv):
        if argv[i] in ("--rtx", "--ngp", "--rtx-out"):
            opts[argv[i][2:].replace("-", "_")] = argv[i + 1]
            i += 2
        elif argv[i] == "--new-palette":
            opts["new_palette"] = True
            i += 1
        else:
            args.append(argv[i])
            i += 1
    if argv[0] == "list":
        cmd_list(args[0])
    elif argv[0] == "export":
        cmd_export(args[0], args[1], opts.get("rtx"), opts.get("ngp"))
    else:
        cmd_inject(args[0], int(args[1], 0), args[2], args[3], opts.get("rtx"), opts.get("rtx_out"), opts.get("ngp"),
                   opts.get("new_palette", False))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
