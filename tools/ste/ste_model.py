"""Leitor de modelos de Godzilla: Save the Earth (PS2) a partir da ISO.

Pipeline: ISO9660 -> GODZILLA.VOL (PVOL) -> Pipeworks Bundle 1.3 (.bdp/.cmp)
-> malha (pos float + peso/ossos, normal s8 + flag de strip, UV u16 /4096)
-> textura PSMT8 (GS-swizzled) + paleta CSM1.
"""
import struct, zlib

SEC = 2048


class Iso:
    def __init__(self, path):
        self.f = open(path, 'rb')
        pvd = self.read(16 * SEC, SEC)
        assert pvd[1:6] == b'CD001'
        lba, ln = struct.unpack_from('<II', pvd, 158)
        self.root = self._dir(lba, ln)

    def read(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def _dir(self, lba, ln):
        data = self.read(lba * SEC, (ln + SEC - 1) // SEC * SEC)
        out, off = {}, 0
        while off < len(data):
            l = data[off]
            if l == 0:
                off = (off // SEC + 1) * SEC
                continue
            elba, elen = struct.unpack_from('<II', data, off + 2)
            nl = data[off + 32]
            name = data[off + 33:off + 33 + nl].decode('ascii', 'replace').split(';')[0]
            out[name] = (elba, elen)
            off += l
        return out


class Vol:
    """GODZILLA.VOL: header 16B, N x {off,size,idx}, depois N + offsets de nome."""

    def __init__(self, iso):
        lba, ln = iso.root['GODZILLA.VOL']
        self.iso, self.base = iso, lba * SEC
        hdr = iso.read(self.base, 0x8000)
        assert hdr[:4] == b'PVOL'
        n = struct.unpack_from('<I', hdr, 8)[0]
        tab = [struct.unpack_from('<III', hdr, 16 + i * 12) for i in range(n)]
        p = 16 + n * 12
        offs = struct.unpack_from('<%dI' % n, hdr, p + 4)
        self.files = {}
        for off, size, idx in tab:
            e = hdr.index(b'\0', p + offs[idx])
            self.files[hdr[p + offs[idx]:e].decode()] = (off, size)

    def names(self):
        return sorted(self.files, key=str.lower)

    def get(self, name):
        key = next(k for k in self.files if k.lower() == name.lower())
        off, size = self.files[key]
        return self.iso.read(self.base + off, size)


class Bundle:
    """Pipeworks Bundle File version 1.3."""

    def __init__(self, data):
        assert data.startswith(b'Pipeworks Bundle File version 1.3')
        self.d = data
        u = lambda o: struct.unpack_from('<I', data, o)[0]
        self.base = u(0x68)       # inicio dos dados (tamanho do cabecalho)
        self.tail = u(0x70)       # regiao "tail": dados crus do GS/PS2
        self.tail_size = u(0x74)
        # entradas de 18 bytes a partir de 0x78 ate id 65535
        self.entries = []
        o = 0x78
        while True:
            i, off, size, to, ts = struct.unpack_from('<HIIII', data, o)
            if i == 0xffff:
                break
            self.entries.append(dict(id=i, off=off, size=size, toff=to, tsize=ts))
            o += 18

    def chunk(self, e):
        return self.d[self.base + e['off']: self.base + e['off'] + e['size']]

    def tail_bytes(self, e):
        return self.d[self.tail + e['toff']: self.tail + e['toff'] + e['tsize']]

    def meshes(self):
        """Entradas de malha: tail grande + descritor com pelo menos uma submalha reconhecivel."""
        res = []
        for e in self.entries:
            if e['tsize'] < 0x4000 or e['size'] < 0x100 or e['size'] > 0x10000:
                continue
            if find_submeshes(self, e):
                res.append(e)
        return res

    def textures(self):
        """(entrada, paleta, w, h) PSMT8: descritor u32 3, 0x40000013, u16 w, u16 h; paleta = entrada seguinte."""
        res = []
        for k, e in enumerate(self.entries[:-1]):
            if e['size'] != 0x104:
                continue
            c = self.chunk(e)
            if len(c) >= 12 and struct.unpack_from('<I', c, 0)[0] == 3 and (struct.unpack_from('<I', c, 4)[0] & 0xff) == 0x13:
                w, h = struct.unpack_from('<HH', c, 8)
                res.append((e, self.entries[k + 1], w, h))
        return res


def _plausible(t, toff, nv):
    n = min(nv, 64)
    for i in range(n):
        x, y, z = struct.unpack_from('<3f', t, toff + i * 16)
        if not all(abs(v) < 3000 for v in (x, y, z)) or x != x or y != y or z != z:
            return False
    ko = toff + nv * 16
    for i in range(n):
        if t[ko + i * 4 + 3] not in (0, 0x7f):
            return False
    return True


def find_submeshes(b, e):
    """Blocos {nv, nb, flags, 0, toff, size} dentro do descritor; devolve [(nv, toff, size)] ordenado por toff."""
    c = b.chunk(e)
    t = b.tail_bytes(e)
    n = len(c) // 4
    w = struct.unpack_from('<%dI' % n, c, 0)
    found = set()
    for j in range(n - 5):
        nv, nb, fl, z, toff, size = w[j:j + 6]
        if not (3 <= nv <= 30000 and 1 <= nb <= 200 and z == 0):
            continue
        if toff % 16 or size % 16 or toff + size > e['tsize'] or not (nv * 24 <= size <= nv * 24 + 0x20000):
            continue
        if not _plausible(t, toff, nv):
            continue
        found.add((nv, toff, size))
    out = []
    for f in sorted(found, key=lambda f: f[1]):
        if not out or f[1] >= out[-1][1] + out[-1][2]:
            out.append(f)
    return out


def decode_mesh(b, e):
    """Malha completa (todas as submalhas concatenadas). Tira de triangulos: o byte 3 da normal e o flag de
    "desenhar" (0x7f) do vertice; o triangulo (i-2,i-1,i) so existe se o flag de i estiver ligado."""
    c = b.chunk(e)
    nb = struct.unpack_from('<H', c, 2)[0]
    if not (8 <= nb <= 200):
        nb = 0
    t = b.tail_bytes(e)
    pos, w0, bi0, bi1, nrm, kick, uv, tris, parts = [], [], [], [], [], [], [], [], []
    for si, (nv, toff, size) in enumerate(find_submeshes(b, e)):
        base = len(pos)
        # formato rigido: o 4o dword do vertice e um float 0.0/1.0 (flag de desenhar) em vez de peso+2 ossos
        w4 = [struct.unpack_from('<I', t, toff + i * 16 + 12)[0] for i in range(nv)]
        rigid = all(v in (0, 0x3f800000) for v in w4) and any(v == 0x3f800000 for v in w4)
        for i in range(nv):
            x, y, z, w, a, bb = struct.unpack_from('<3fHBB', t, toff + i * 16)
            pos.append((x, y, z))
            if rigid:
                w0.append(1.0); bi0.append(si); bi1.append(si)
            else:
                w0.append(w / 4096.0); bi0.append(a); bi1.append(bb)
        o = toff + nv * 16
        sk = []
        for i in range(nv):
            nx, ny, nz, k = struct.unpack_from('<3bB', t, o + i * 4)
            nrm.append((nx / 127.0, ny / 127.0, nz / 127.0))
            sk.append((w4[i] == 0x3f800000) if rigid else (k != 0))
        kick += sk
        o = toff + ((nv * 20 + 15) & ~15)   # o stream de UV comeca alinhado em 16 bytes
        for i in range(nv):
            u, v = struct.unpack_from('<HH', t, o + i * 4)
            uv.append((u / 4096.0, v / 4096.0))
        n0 = len(tris)
        for i in range(2, nv):
            if sk[i]:
                tris.append((base + i - 2, base + i - 1, base + i) if (i & 1) == 0 else (base + i - 1, base + i - 2, base + i))
        parts.append((nv, len(tris) - n0))
    return dict(nbones=nb, pos=pos, w0=w0, b0=bi0, b1=bi1, nrm=nrm, uv=uv, tris=tris, kick=kick, parts=parts)


# -- textura -----------------------------------------------------------------
def unswizzle8(data, w, h):
    out = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            blk = (y & ~0xf) * w + (x & ~0xf) * 2
            sw = (((y + 2) >> 2) & 1) * 4
            py = (((y & ~3) >> 1) + (y & 1)) & 7
            col = py * w * 2 + ((x + sw) & 7) * 4
            bn = ((y >> 1) & 1) + ((x >> 2) & 2)
            out[y * w + x] = data[blk + col + bn]
    return bytes(out)


def decode_texture(b, tex):
    e, pe, w, h = tex
    t = b.tail_bytes(e)
    pal_raw = b.tail_bytes(pe)
    pal = [tuple(pal_raw[i * 4:i * 4 + 4]) for i in range(256)]
    p2 = list(pal)
    for g in range(0, 256, 32):
        for k in range(8):
            p2[g + 8 + k], p2[g + 16 + k] = pal[g + 16 + k], pal[g + 8 + k]
    lin = unswizzle8(t[:w * h], w, h)
    rgba = bytearray()
    for i in lin:
        r, g, bl, a = p2[i]
        rgba += bytes((r, g, bl, min(255, a * 2)))
    return w, h, bytes(rgba)


def write_png(path, w, h, rgba):
    raw = b''.join(b'\x00' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    def ck(t, c):
        x = struct.pack('>I', len(c)) + t + c
        return x + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ck(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                           + ck(b'IDAT', zlib.compress(raw)) + ck(b'IEND', b''))
