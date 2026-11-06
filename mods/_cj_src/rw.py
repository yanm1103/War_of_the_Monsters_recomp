"""RenderWare (GTA SA PC) readers: .dff clumps (frames, HAnim, geometry, skin) and .txd textures (to RGBA)."""
import struct
import zlib

CLUMP, STRUCT, STRING, EXT, TEXTURE, MATERIAL, MATLIST, FRAMELIST, GEOMETRY, ATOMIC, TEXNATIVE, TEXDICT, GEOLIST = \
    0x10, 0x1, 0x2, 0x3, 0x6, 0x7, 0x8, 0xE, 0xF, 0x14, 0x15, 0x16, 0x1A
SKIN, HANIM, FRAMENAME = 0x116, 0x11E, 0x253F2FE


def chunks(b, off, end):
    while off + 12 <= end:
        t, sz, ver = struct.unpack_from("<III", b, off)
        if t == 0 and sz == 0:
            break
        yield t, off + 12, sz, ver
        off += 12 + sz


def child(b, off, sz, typ):
    for t, o, s, v in chunks(b, off, off + sz):
        if t == typ:
            return o, s, v
    return None


def libver(v):
    return ((v >> 14 & 0x3FF00) + 0x30000) | (v >> 16 & 0x3F) if v & 0xFFFF0000 else v << 8


class Frame:
    def __init__(self, rot, pos, parent):
        self.rot, self.pos, self.parent = rot, pos, parent
        self.name, self.hanim_id, self.hanim_nodes = "", None, None

    def local(self):
        """Column-vector 4x4 rows: column c is axis c (right, up, at), column 3 the position."""
        r = self.rot
        return [[r[0], r[3], r[6], self.pos[0]], [r[1], r[4], r[7], self.pos[1]], [r[2], r[5], r[8], self.pos[2]],
                [0.0, 0.0, 0.0, 1.0]]


class Geometry:
    pass


def read_clump(b, off, sz):
    frames = []
    fl = child(b, off, sz, FRAMELIST)
    so, ss, _ = child(b, fl[0], fl[1], STRUCT)
    n = struct.unpack_from("<I", b, so)[0]
    for i in range(n):
        vals = struct.unpack_from("<12fiI", b, so + 4 + 56 * i)
        frames.append(Frame(vals[0:9], vals[9:12], vals[12]))
    exts = [(o, s) for t, o, s, v in chunks(b, fl[0], fl[0] + fl[1]) if t == EXT]
    for i, (eo, es) in enumerate(exts[:n]):
        for t, o, s, v in chunks(b, eo, eo + es):
            if t == FRAMENAME:
                frames[i].name = b[o:o + s].split(b"\0")[0].decode("latin-1")
            elif t == HANIM:
                ver, nid, nn = struct.unpack_from("<IiI", b, o)
                frames[i].hanim_id = nid
                if nn:
                    flags, kfs = struct.unpack_from("<II", b, o + 12)
                    frames[i].hanim_nodes = [struct.unpack_from("<iiI", b, o + 20 + 12 * k) for k in range(nn)]
    geos = []
    gl = child(b, off, sz, GEOLIST)
    for t, o, s, v in chunks(b, gl[0], gl[0] + gl[1]):
        if t == GEOMETRY:
            geos.append(read_geometry(b, o, s, v))
    atomics = []
    for t, o, s, v in chunks(b, off, off + sz):
        if t == ATOMIC:
            ao, _as, _ = child(b, o, s, STRUCT)
            fi, gi = struct.unpack_from("<II", b, ao)
            atomics.append((fi, gi))
    return frames, geos, atomics


def read_geometry(b, off, sz, ver):
    g = Geometry()
    so, ss, _ = child(b, off, sz, STRUCT)
    flags, nuv, native, ntri, nvert, nmorph = struct.unpack_from("<HBBIII", b, so)
    p = so + 16
    if libver(ver) < 0x34000:
        p += 12
    if not nuv and flags & 0x4:
        nuv = 1
    g.colors = None
    if flags & 0x8:
        g.colors = [b[p + 4 * i:p + 4 * i + 4] for i in range(nvert)]
        p += 4 * nvert
    g.uv = []
    for k in range(nuv):
        g.uv.append([struct.unpack_from("<2f", b, p + 8 * i) for i in range(nvert)])
        p += 8 * nvert
    g.tris = []
    for i in range(ntri):
        v1, v0, mat, v2 = struct.unpack_from("<4H", b, p + 8 * i)
        g.tris.append((v0, v1, v2, mat))
    p += 8 * ntri
    p += 16  # bounding sphere
    hasv, hasn = struct.unpack_from("<II", b, p)
    p += 8
    g.pos = [struct.unpack_from("<3f", b, p + 12 * i) for i in range(nvert)] if hasv else []
    p += 12 * nvert if hasv else 0
    g.nrm = [struct.unpack_from("<3f", b, p + 12 * i) for i in range(nvert)] if hasn else None
    # materials
    ml = child(b, off, sz, MATLIST)
    mo, msz, _ = child(b, ml[0], ml[1], STRUCT)
    nm = struct.unpack_from("<I", b, mo)[0]
    idx = struct.unpack_from("<%di" % nm, b, mo + 4)
    mats = [(o, s) for t, o, s, v in chunks(b, ml[0], ml[0] + ml[1]) if t == MATERIAL]
    g.mats = []
    for i in range(nm):
        o, s = mats[i] if idx[i] < 0 else mats[idx[i]]
        mso, _s, _ = child(b, o, s, STRUCT)
        _f, rgba, _u, istex = struct.unpack_from("<I4sII", b, mso)
        tex = None
        tx = child(b, o, s, TEXTURE)
        if tx:
            strs = [b[so2:so2 + ss2].split(b"\0")[0].decode("latin-1") for t2, so2, ss2, v2 in chunks(b, tx[0], tx[0] + tx[1]) if t2 == STRING]
            tex = strs[0] if strs else None
        g.mats.append({"color": tuple(rgba), "tex": tex})
    # skin
    g.skin = None
    ex = child(b, off, sz, EXT)
    if ex:
        sk = child(b, ex[0], ex[1], SKIN)
        if sk:
            o = sk[0]
            nb, nused, maxw = b[o], b[o + 1], b[o + 2]
            o += 4 + nused
            bi = [tuple(b[o + 4 * i:o + 4 * i + 4]) for i in range(nvert)]
            o += 4 * nvert
            bw = [struct.unpack_from("<4f", b, o + 16 * i) for i in range(nvert)]
            o += 16 * nvert
            old = libver(sk[2]) < 0x34003
            ibm = []
            for k in range(nb):
                if old:
                    o += 4
                ibm.append(struct.unpack_from("<16f", b, o))
                o += 64
            g.skin = {"nbones": nb, "idx": bi, "w": bw, "ibm": ibm}
    return g


def read_dff(path):
    b = open(path, "rb").read()
    return [read_clump(b, o, s) for t, o, s, v in chunks(b, 0, len(b)) if t == CLUMP]


# ---------------------------------------------------------------- textures

def _dxt_color(c0, c1, four):
    def rgb(c):
        r, g, bb = c >> 11 & 31, c >> 5 & 63, c & 31
        return [r * 255 // 31, g * 255 // 63, bb * 255 // 31]
    a, b = rgb(c0), rgb(c1)
    if four or c0 > c1:
        return [a + [255], b + [255], [(2 * x + y) // 3 for x, y in zip(a, b)] + [255],
                [(x + 2 * y) // 3 for x, y in zip(a, b)] + [255]]
    return [a + [255], b + [255], [(x + y) // 2 for x, y in zip(a, b)] + [255], [0, 0, 0, 0]]


def decode_dxt(data, w, h, kind):
    out = bytearray(w * h * 4)
    bs = 8 if kind == 1 else 16
    p = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            alpha = None
            if kind == 3:
                av = int.from_bytes(data[p:p + 8], "little")
                alpha = [(av >> (4 * i) & 15) * 17 for i in range(16)]
            elif kind == 5:
                a0, a1 = data[p], data[p + 1]
                bits = int.from_bytes(data[p + 2:p + 8], "little")
                if a0 > a1:
                    pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                else:
                    pal = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
                alpha = [pal[bits >> (3 * i) & 7] for i in range(16)]
            q = p + (8 if kind != 1 else 0)
            c0, c1, bits = struct.unpack_from("<HHI", data, q)
            cols = _dxt_color(c0, c1, kind != 1)
            for i in range(16):
                x, y = bx + (i & 3), by + (i >> 2)
                if x < w and y < h:
                    c = cols[bits >> (2 * i) & 3]
                    o = 4 * (y * w + x)
                    out[o:o + 4] = bytes([c[0], c[1], c[2], alpha[i] if alpha else c[3]])
            p += bs
    return bytes(out)


def read_txd(path):
    """{name: (w, h, rgba bytes)}"""
    b = open(path, "rb").read()
    out = {}
    td = child(b, 0, len(b), TEXDICT)
    for t, o, s, v in chunks(b, td[0], td[0] + td[1]):
        if t != TEXNATIVE:
            continue
        so, ss, _ = child(b, o, s, STRUCT)
        plat, _flt = struct.unpack_from("<II", b, so)
        name = b[so + 8:so + 40].split(b"\0")[0].decode("latin-1")
        rfmt, d3d, w, h, depth, nlev, rtype, cflag = struct.unpack_from("<IIHHBBBB", b, so + 72)
        p = so + 88
        pal = None
        if rfmt & 0x2000:
            pal = b[p:p + 1024]
            p += 1024
        elif rfmt & 0x4000:
            pal = b[p:p + 64]
            p += 64
        dsz = struct.unpack_from("<I", b, p)[0]
        data = b[p + 4:p + 4 + dsz]
        fourcc = struct.pack("<I", d3d) if plat == 9 else b""
        if fourcc in (b"DXT1", b"DXT3", b"DXT5") or (plat == 8 and cflag in (1, 3)):
            kind = int(fourcc[3:4]) if fourcc else cflag
            rgba = decode_dxt(data, w, h, kind)
        elif pal is not None:
            if rfmt & 0x4000:
                idx = []
                for x in data[:(w * h + 1) // 2]:
                    idx += [x & 15, x >> 4]
            else:
                idx = data[:w * h]
            rgba = b"".join(pal[4 * i:4 * i + 4] for i in idx[:w * h])
        elif rfmt & 0xF00 in (0x500, 0x600):
            rgba = bytearray(w * h * 4)
            for i in range(w * h):
                bb, g, r, a = data[4 * i:4 * i + 4]
                rgba[4 * i:4 * i + 4] = bytes([r, g, bb, a if rfmt & 0xF00 == 0x500 else 255])
            rgba = bytes(rgba)
        else:
            raise ValueError("%s: texture %s format 0x%X d3d %r not handled" % (path, name, rfmt, d3d))
        out[name] = (w, h, rgba, "fmt 0x%X %s" % (rfmt, fourcc.decode("latin-1") if fourcc else cflag))
    return out


def write_png(path, w, h, rgba):
    raw = b"".join(b"\0" + rgba[4 * w * y:4 * w * (y + 1)] for y in range(h))

    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
