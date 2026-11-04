#!/usr/bin/env python3
"""Mesh packets of the .NGP OBJECTs: read into geometry and write back (docs/FORMATS.md, "Meshes").

Two packet kinds:
- rigid: `HierObject::polyPkt` (+4) is a DMA `RET` tag (QWC in the low 16 bits) and the packet has QWC + 1 quadwords;
  the VIF stream starts at word 2.
- skinned: the CHAR_INSTANCE `animPktPtr[OBJECT+0x68]` chain is `REF palette, REF sub-packet` per sub-packet, then `RET`.
  Each REF gives the sub-packet length; the sub-packet is pure VIF and starts with `UNPACK S-32 @0xB4` = vertex count.

`parse(data, skinned)` returns a `Packet` (batches of positions and normals, each with its strip sections);
`encode(pkt)` writes the same bytes back. `python tools/vifpkt.py <disc>/MON` checks the round trip on every .NGP.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ngp  # noqa: E402

VU_COUNT = 0xB4      # S-32: vertex count (skinned)
VU_POS = 0xB5        # V3-32 (rigid) / V4-32 with bone codes (skinned), up to 190
VU_NRM = 0x175       # V3-8 signed normals
SECTION_ADDRS = (0x238, 0x2C8, 0x358)   # GIF tag of each strip section, cycling (VU1 triple buffer)
MAX_POS = 190
MAX_SECTION = 44     # 1 + 3*44 qwords fit in the 0x90 between section addresses

CMD_NOP, CMD_STCYCL, CMD_MSCALF, CMD_MSCNT, CMD_STMASK = 0x00, 0x01, 0x15, 0x17, 0x20
MASK_ADC = 0xBFBFBFBF    # ADC unpack: only W written (from the column register)
MASK_UV = 0x50505050     # UV unpack: S/T from the data, the rest from the row register


CMD_FLUSHE = 0x10
STRIP, SPRITE = "strip", "sprite"


class Section:
    """One section, ended by MSCNT. Strip: the GIF tag (4 words, NLOOP = len(idx)), then per vertex the position index,
    RGBA and UV. Sprite: per sprite one position (x, y, z, half side; raw u32), one RGBA and two UVs."""

    def __init__(self, kind=STRIP):
        self.kind = kind
        self.gif = (0, 0, 0, 0)
        self.idx = []
        self.rgba = []
        self.uv = []
        self.adc = []        # strip: k = vertices k and k+1 do not close a triangle (strip restart)
        self.spos = []       # sprite: (x, y, z, w) raw u32

    def count(self):
        return len(self.spos) if self.kind == SPRITE else len(self.idx)


class Batch:
    """Positions and normals sent together (up to 190), and the sections that index them. A sprite packet has one batch
    with no positions."""

    def __init__(self):
        self.pos = []        # rigid: (x, y, z) as raw u32; skinned: (x, y, z, w) raw u32 (bone code in the low 8 bits of x/y/z)
        self.nrm = []        # (nx, ny, nz) signed bytes
        self.sections = []
        self.mscal = None    # skinned: MSCALF address after the normals (microprogram entry)


class Packet:
    def __init__(self, skinned):
        self.skinned = skinned
        self.batches = []
        self.qwc = 0         # total length in quadwords (rigid: QWC + 1; skinned: the REF length)

    def nverts(self):
        return sum(len(b.pos) for b in self.batches)


class FormatError(Exception):
    pass


def _is_sprite_gif(gif):
    g1 = gif[1]
    return bool((g1 >> 14) & 1) and ((g1 >> 15) & 7) == 6


# ---------------------------------------------------------------- reading

def _words(data):
    return struct.unpack("<%dI" % (len(data) // 4), data[:len(data) // 4 * 4])


def parse(data, skinned):
    """Packet from the raw bytes of a rigid packet (with its DMA tag) or a skinned sub-packet. Anything outside the
    grammar the encoder writes raises FormatError."""
    w = _words(data)
    pkt = Packet(skinned)
    pkt.qwc = len(data) // 16
    if skinned:
        if w[0] != 0x60010000 | VU_COUNT:
            raise FormatError("skinned: word 0 %08x is not UNPACK S-32 @0xB4" % w[0])
        count = w[1]
    else:
        if (w[0] >> 28) & 7 != 6 or w[1] != 0:
            raise FormatError("rigid: tag %08x %08x" % (w[0], w[1]))
        if (w[0] & 0xFFFF) + 1 != pkt.qwc:
            raise FormatError("rigid: QWC %d with %d quadwords" % (w[0] & 0xFFFF, pkt.qwc))
    i = 2
    b = sec = None
    mask = cycle = None
    nsec = 0
    closed = False       # after STCYCL 4,4: FLUSHE opens the next batch, zeros end the packet
    padding = False
    while i < len(w):
        word = w[i]
        cmd, num, imm = (word >> 24) & 0x7F, (word >> 16) & 0xFF, word & 0xFFFF
        if padding or (closed and word == 0):
            if word != 0:
                raise FormatError("data after the end at word %d: %08x" % (i, word))
            padding = True
            i += 1
            continue
        if word >> 31:
            raise FormatError("IRQ bit at word %d" % i)
        if closed:
            if cmd != CMD_FLUSHE or word != CMD_FLUSHE << 24 or skinned:
                raise FormatError("after STCYCL 4,4: %08x at word %d" % (word, i))
            closed = False
            b = None
            i += 1
            continue
        if cmd >= 0x60:
            vn, vl, m = (cmd >> 2) & 3, cmd & 3, (cmd >> 4) & 1
            n = num or 256
            addr, usn, flg = imm & 0x3FF, (imm >> 14) & 1, imm >> 15
            if imm & 0x3C00 or flg:
                raise FormatError("unpack flags %04x at word %d" % (imm, i))
            nw = ngp._unpack_words(vn, vl, n)
            pay = data[4 * (i + 1):4 * (i + 1 + nw)]
            fmt = (vn, vl, m, usn)
            h = SECTION_ADDRS[nsec % 3]
            if sec is None and addr == VU_POS and fmt == ((3, 0, 0, 0) if skinned else (2, 0, 0, 1)):
                if b is not None:
                    raise FormatError("positions twice in a batch at word %d" % i)
                if cycle not in (None, (4, 4)):
                    raise FormatError("positions with STCYCL %s" % (cycle,))
                b = Batch()
                pkt.batches.append(b)
                k = 4 if skinned else 3
                pw = _words(pay)
                b.pos = [tuple(pw[k * j:k * j + k]) for j in range(n)]
            elif sec is None and addr == VU_NRM and fmt == (2, 2, 0, 0):
                if b is None or b.nrm or b.sections:
                    raise FormatError("normals out of place at word %d" % i)
                if n != len(b.pos):
                    raise FormatError("%d normals for %d positions" % (n, len(b.pos)))
                b.nrm = [struct.unpack_from("<3b", pay, 3 * j) for j in range(n)]
                if any(pay[3 * n:]):
                    raise FormatError("normal padding not zero")
            elif sec is None and fmt == (3, 0, 0, 0) and n == 1 and addr in SECTION_ADDRS:
                if addr != h:
                    raise FormatError("section %d at %x (expected %x)" % (nsec, addr, h))
                gif = _words(pay)
                if b is None:
                    if pkt.batches or skinned or not _is_sprite_gif(gif):
                        raise FormatError("section before positions at word %d" % i)
                    b = Batch()
                    pkt.batches.append(b)
                    b.pos = None
                sec = Section(SPRITE if _is_sprite_gif(gif) else STRIP)
                if (sec.kind == STRIP) != (b.pos is not None):
                    raise FormatError("%s section in a batch %s positions" % (sec.kind, "without" if b.pos is None else "with"))
                if sec.kind == STRIP and cycle != (3, 1):
                    raise FormatError("strip section with STCYCL %s" % (cycle,))
                sec.gif = gif
            elif sec is not None and sec.kind == STRIP and addr == h + 1 and fmt == (0, 2, 0, 1) and not sec.idx:
                sec.idx = list(pay[:n])
                if any(pay[n:]):
                    raise FormatError("index padding not zero")
            elif sec is not None and sec.kind == STRIP and addr == h + 2 and fmt == (3, 2, 0, 1) and sec.idx and not sec.rgba:
                sec.rgba = [tuple(pay[4 * j:4 * j + 4]) for j in range(n)]
            elif sec is not None and sec.kind == STRIP and addr == h + 3 and fmt == (1, 1, 1, 0) and sec.rgba and not sec.uv:
                if mask not in (None, MASK_UV):
                    raise FormatError("UV with mask %08x" % mask)
                sec.uv = [struct.unpack_from("<2h", pay, 4 * j) for j in range(n)]
            elif sec is not None and sec.kind == STRIP and fmt == (0, 2, 1, 1) and n == 2 and mask == MASK_ADC:
                e = addr - h - 3
                if e < 0 or e % 3 or (sec.adc and e // 3 <= sec.adc[-1]):
                    raise FormatError("ADC at %x" % addr)
                if any(pay):
                    raise FormatError("ADC data not zero")
                sec.adc.append(e // 3)
            elif sec is not None and sec.kind == SPRITE and addr == h + 3 and fmt == (3, 0, 0, 0) and cycle == (6, 1) and not sec.spos:
                pw = _words(pay)
                sec.spos = [tuple(pw[4 * j:4 * j + 4]) for j in range(n)]
            elif sec is not None and sec.kind == SPRITE and addr == h + 2 and fmt == (3, 2, 0, 1) and cycle == (6, 1) and sec.spos and not sec.rgba:
                sec.rgba = [tuple(pay[4 * j:4 * j + 4]) for j in range(n)]
            elif sec is not None and sec.kind == SPRITE and addr == h + 1 and fmt == (1, 1, 1, 0) and cycle == (3, 1) and sec.rgba and not sec.uv:
                if mask not in (None, MASK_UV):
                    raise FormatError("UV with mask %08x" % mask)
                sec.uv = [struct.unpack_from("<2h", pay, 4 * j) for j in range(n)]
            else:
                raise FormatError("unexpected UNPACK fmt=%s n=%d addr=%x at word %d" % (fmt, n, addr, i))
            i += 1 + nw
            continue
        if cmd == CMD_STMASK and word == CMD_STMASK << 24:
            mask = w[i + 1]
            if mask not in (MASK_ADC, MASK_UV):
                raise FormatError("STMASK %08x" % mask)
            if sec is None or sec.kind != STRIP or not sec.uv or (mask == MASK_ADC) == bool(sec.adc):
                raise FormatError("STMASK %08x out of place at word %d" % (mask, i))
            i += 2
            continue
        if cmd == CMD_STCYCL and not num:
            cycle = (imm & 0xFF, imm >> 8)
            if cycle == (4, 4):
                if sec is not None:
                    raise FormatError("STCYCL 4,4 inside a section")
                closed = True
            elif cycle not in ((3, 1), (6, 1)):
                raise FormatError("STCYCL %s" % (cycle,))
            i += 1
            continue
        if cmd == CMD_MSCALF and not num:
            if not skinned or sec is not None or b is None or b.sections or b.mscal is not None or not b.nrm:
                raise FormatError("MSCALF %x at word %d" % (imm, i))
            b.mscal = imm
            i += 1
            continue
        if word == CMD_MSCNT << 24:
            if sec is None:
                raise FormatError("MSCNT outside a section at word %d" % i)
            n = sec.count()
            nloop = sec.gif[0] & 0x7FFF
            if sec.kind == STRIP:
                ok = nloop == n and len(sec.rgba) == n and len(sec.uv) == n
            else:
                ok = len(sec.rgba) == n and len(sec.uv) == 2 * n
            if not ok or n == 0:
                raise FormatError("%s section sizes: nloop %d, %d/%d/%d" % (sec.kind, nloop, n, len(sec.rgba), len(sec.uv)))
            if sec.adc and sec.adc[-1] + 1 >= n:
                raise FormatError("ADC past the section")
            b.sections.append(sec)
            sec = None
            nsec += 1
            i += 1
            continue
        raise FormatError("VIF cmd %02x (%08x) at word %d" % (cmd, word, i))
    if not closed or sec is not None:
        raise FormatError("no STCYCL 4,4 at the end")
    for x in pkt.batches:
        if not x.sections or (x.pos is not None and not x.nrm):
            raise FormatError("batch without sections or normals")
    if skinned and (count != pkt.nverts() or len(pkt.batches) != 1):
        raise FormatError("count %d for %d positions in %d batches" % (count, pkt.nverts(), len(pkt.batches)))
    return pkt


# ---------------------------------------------------------------- writing

def _unpack(vn, vl, addr, n, payload, m=0, usn=0):
    if not 1 <= n <= 256:
        raise FormatError("UNPACK of %d elements" % n)
    word = (0x60 | m << 4 | vn << 2 | vl) << 24 | (n & 0xFF) << 16 | usn << 14 | addr
    pad = (-len(payload)) % 4
    return struct.pack("<I", word) + payload + b"\0" * pad


def encode(pkt, qwc=None):
    """Bytes of the packet. `qwc`: total length in quadwords (zero padding at the end); default the smallest that fits."""
    out = bytearray()
    if pkt.skinned:
        if len(pkt.batches) != 1:
            raise FormatError("a skinned sub-packet has one batch")
        out += struct.pack("<II", 0x60010000 | VU_COUNT, pkt.nverts())
    else:
        out += b"\0" * 8   # DMA tag, filled at the end
    cur = [None]

    def cycle(cl, wl):
        if cur[0] != (cl, wl):
            out.extend(struct.pack("<I", CMD_STCYCL << 24 | wl << 8 | cl))
            cur[0] = (cl, wl)

    nsec = 0
    for bi, b in enumerate(pkt.batches):
        if bi:
            out += struct.pack("<I", CMD_FLUSHE << 24)
        if b.pos is not None:
            if not 1 <= len(b.pos) <= MAX_POS or len(b.nrm) != len(b.pos):
                raise FormatError("batch of %d positions and %d normals" % (len(b.pos), len(b.nrm)))
            k = 4 if pkt.skinned else 3
            pay = b"".join(struct.pack("<%dI" % k, *p) for p in b.pos)
            out += _unpack(3 if pkt.skinned else 2, 0, VU_POS, len(b.pos), pay, usn=0 if pkt.skinned else 1)
            out += _unpack(2, 2, VU_NRM, len(b.nrm), b"".join(struct.pack("<3b", *q) for q in b.nrm))
            if b.mscal is not None:
                out += struct.pack("<I", CMD_MSCALF << 24 | b.mscal)
            cycle(3, 1)
        for s in b.sections:
            h = SECTION_ADDRS[nsec % 3]
            nsec += 1
            out += _unpack(3, 0, h, 1, struct.pack("<4I", *s.gif))
            if s.kind == SPRITE:
                cycle(6, 1)
                out += _unpack(3, 0, h + 3, len(s.spos), b"".join(struct.pack("<4I", *p) for p in s.spos))
                out += _unpack(3, 2, h + 2, len(s.rgba), b"".join(bytes(c) for c in s.rgba), usn=1)
                cycle(3, 1)
                out += _unpack(1, 1, h + 1, len(s.uv), b"".join(struct.pack("<2h", *t) for t in s.uv), m=1)
            else:
                if len(s.idx) > MAX_SECTION:
                    raise FormatError("section of %d vertices (max %d)" % (len(s.idx), MAX_SECTION))
                out += _unpack(0, 2, h + 1, len(s.idx), bytes(s.idx), usn=1)
                out += _unpack(3, 2, h + 2, len(s.rgba), b"".join(bytes(c) for c in s.rgba), usn=1)
                out += _unpack(1, 1, h + 3, len(s.uv), b"".join(struct.pack("<2h", *t) for t in s.uv), m=1)
                if s.adc:
                    out += struct.pack("<II", CMD_STMASK << 24, MASK_ADC)
                    for e in s.adc:
                        out += _unpack(0, 2, h + 3 + 3 * e, 2, b"\0\0", m=1, usn=1)
                    out += struct.pack("<II", CMD_STMASK << 24, MASK_UV)
            out += struct.pack("<I", CMD_MSCNT << 24)
        out += struct.pack("<I", CMD_STCYCL << 24 | 0x0404)
        cur[0] = (4, 4)
    out += b"\0" * ((-len(out)) % 16)
    if qwc is not None:
        if len(out) > 16 * qwc:
            raise FormatError("packet of %d quadwords does not fit in %d" % (len(out) // 16, qwc))
        out += b"\0" * (16 * qwc - len(out))
    if not pkt.skinned:
        if len(out) // 16 - 1 > 0xFFFF:
            raise FormatError("rigid packet too long")
        struct.pack_into("<I", out, 0, 0x60000000 | (len(out) // 16 - 1))
    return bytes(out)


# ---------------------------------------------------------------- packets of a .NGP

def _subtree(img, root):
    """Every node reachable from root (all children, not only the drawn ones)."""
    u32, u16, u8 = img.u32, img.u16, img.u8
    seen, order, st = set(), [], [root]
    while st:
        n = st.pop()
        if n in seen or not img.ok(n, 0x30):
            continue
        seen.add(n)
        order.append(n)
        op = u32(n) & 63
        lay = {1: (0x20, u16(n + 8)), 3: (0x1C, u32(n + 8)), 4: (0x50, u32(n + 8)), 30: (0x2C, u32(n + 8)),
               6: (0xC, u8(n + 0xB)), 35: (0xC, u32(n + 4)), 25: (0x2C, u32(n + 0x28)), 17: (0xCC, u32(n + 4)),
               38: (0x60, u32(n + 4)), 7: (8, u32(n + 4))}
        kids = []
        if op in lay:
            off, cnt = lay[op]
            kids = [u32(n + off + 4 * k) for k in range(min(cnt, 4096))]
        elif op == 2:
            kids = [u32(n + 0x28 + 16 * i) for i in range(min(u32(n + 4), 16))]
        elif op == 8:
            kids = [u32(n + 4), u32(n + 8)]
        elif op in (23, 39):
            kids = [u32(n + (4 if op == 23 else 0xC))]
        elif op == 31:
            kids = [u32(n + 0xC + 12 * i) for i in range(min(u16(n + 0xA), 16))]
        st.extend(reversed([c & 0x0FFFFFFF for c in kids]))
    return order


def packets(img):
    """[(OBJECT, kind, address, quadwords)] of every mesh packet: ('rigid', polyPkt) or ('skin', each sub-packet of the chain)."""
    out = []
    seen = set()
    for r in img.roots():
        nodes = _subtree(img, r)
        cis = [n for n in nodes if img.u32(n) & 63 == 25]
        for ci in cis:
            ap = img.u32(ci + 0x18)
            for o in _subtree(img, ci):
                if img.u32(o) & 63 != 0 or o in seen:
                    continue
                idx = struct.unpack_from("<i", img.d, o + 0x68 - img.base)[0]
                if idx < 0 or not img.ok(ap, 4 * (idx + 1)):
                    continue
                seen.add(o)
                p = img.u32(ap + 4 * idx) & 0x0FFFFFFF
                for _ in range(256):
                    t0 = img.u32(p)
                    if (t0 >> 28) & 7 != 3:
                        break
                    sub = img.u32(p + 16) if (img.u32(p + 16) >> 28) & 7 == 3 else None
                    if sub is None:
                        raise FormatError("chain %x: palette without a sub-packet" % p)
                    out.append((o, "skin", img.u32(p + 20) & 0x0FFFFFFF, img.u32(p + 16) & 0xFFFF))
                    p += 32
        for o in nodes:
            if img.u32(o) & 63 != 0 or o in seen:
                continue
            seen.add(o)
            pp = img.u32(o + 4) & 0x0FFFFFFF
            if not img.ok(pp, 16) or (img.u32(pp) >> 28) & 7 != 6 or img.u32(pp) & 0x10000:
                continue
            out.append((o, "rigid", pp, (img.u32(pp) & 0xFFFF) + 1))
    return out


def selftest(paths):
    total = bad = 0
    for path in paths:
        img = ngp.Image.load(path)
        name = os.path.basename(path)
        fails = []
        pk = packets(img)
        for o, kind, addr, qwc in pk:
            raw = bytes(img.d[addr - img.base:addr - img.base + 16 * qwc])
            total += 1
            try:
                p = parse(raw, kind == "skin")
                again = encode(p, qwc)
            except FormatError as e:
                fails.append("%s @%x obj %x: %s" % (kind, addr, o, e))
                continue
            if again != raw:
                k = next(j for j in range(0, len(raw), 4) if again[j:j + 4] != raw[j:j + 4])
                fails.append("%s @%x obj %x: word %d differs (%s x %s)" % (kind, addr, o, k // 4, raw[k:k + 4].hex(), again[k:k + 4].hex()))
        bad += len(fails)
        print("%-14s %4d packets, %d differ" % (name, len(pk), len(fails)))
        for f in fails[:40]:
            print("    " + f)
    print("total: %d packets, %d differ" % (total, bad))
    return bad == 0


def main(argv):
    if len(argv) != 2:
        print("usage: vifpkt.py <disc>/MON | file.NGP")
        return 2
    p = argv[1]
    paths = [os.path.join(p, f) for f in sorted(os.listdir(p)) if f.upper().endswith(".NGP")] if os.path.isdir(p) else [p]
    return 0 if selftest(paths) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
