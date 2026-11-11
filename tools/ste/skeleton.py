"""Esqueleto de Godzilla: Save the Earth (PS2): arvore de nos + pose local (translacao, quaternion).

No bundle (.cmp/.bdp):
- tabela de nomes (576 nomes, ordenados): apos o TOC; "000Bip01 Head" etc. (prefixo de 3 caracteres).
- descritor de malha: u16 nSub, u16 nBones, nBones x u32 indice de nome (osso de pele i -> nome).
- arvore de nos: entrada com u32@0x1c == 0x40 (inicio dos registros), u32@0x20 = n nos; cada registro
  {idx, pai (ffffffff = raiz), nFilhos, indice de nome, offsets dos filhos...}.
- entrada seguinte: registros de 0x60 bytes a partir de 0x11c: idx, comprimento, ..., +0x24 translacao (3 f),
  +0x3c quaternion (x,y,z,w).
"""
import struct, math, re


def _name_table(b):
    """(posicao da tabela, offsets dos nomes) ou None: u32 N + N offsets crescentes (relativos ao u32 N)."""
    d = b.d
    o = 0x78 + 18 * len(b.entries)
    for p in range(o, o + 400, 2):
        n = struct.unpack_from('<I', d, p)[0]
        if 20 < n < 4000:
            offs = struct.unpack_from('<%dI' % min(n, 6), d, p + 4)
            if all(offs[i] < offs[i + 1] for i in range(len(offs) - 1)) and offs[0] < 6000:
                return p, struct.unpack_from('<%dI' % n, d, p + 4)
    return None


def names_of(b):
    t = _name_table(b)
    if not t:
        return []
    p, offs = t
    d = b.d
    return [re.sub(r'^[\x01-\x1f]\d{0,3}', '', d[p + of:d.index(b'\0', p + of)].decode('latin1')) for of in offs]   # o .cmp prefixa <ctrl>000


def entry_names(b):
    """{id da entrada: nome}: depois das strings ha 1 registro de 16 B por entrada {id, tipo, indice do nome, n, extra}
    (tipo 4 = animacao)."""
    t = _name_table(b)
    if not t:
        return {}
    p, offs = t
    d = b.d
    nm = names_of(b)
    e = d.index(b'\0', p + max(offs)) + 1
    while e < len(d) and d[e] == 0xff:
        e += 1
    out = {}
    for i in range((b.base - e) // 16):
        eid, typ, ni, _, _ = struct.unpack_from('<HHIII', d, e + 16 * i)
        if ni < len(nm):
            out[eid] = (typ, nm[ni])
    return out


def find_tree(b):
    """(indice da entrada da arvore, n nos) ou None."""
    for k, e in enumerate(b.entries[:-1]):
        if e['size'] < 0x80:
            continue
        c = b.chunk(e)
        w = struct.unpack_from('<10I', c, 0)
        if w[7] == 0x40 and 2 <= w[8] <= 200 and struct.unpack_from('<2I', c, 0x40)[1] == 0xffffffff:
            return k, w[8]
    return None


def parse_skeleton(b, desc_entry=None):
    """dict(names=[...], parent=[...], name=[...], t=[(x,y,z)], q=[(x,y,z,w)], skin=[no do osso de pele i])."""
    nm = names_of(b)
    r = find_tree(b)
    if not r:
        return None
    k, n = r
    c = b.chunk(b.entries[k])
    parent, name = [None] * n, [None] * n
    # percorre os registros a partir de 0x40 seguindo os offsets dos filhos
    stack = [0x40]
    seen = set()
    while stack:
        o = stack.pop()
        if o in seen or o + 16 > len(c):
            continue
        seen.add(o)
        idx, par, nch, nidx = struct.unpack_from('<IIII', c, o)
        if idx >= n:
            continue
        parent[idx] = -1 if par == 0xffffffff else par
        name[idx] = nm[nidx] if nidx < len(nm) else '?%d' % nidx
        for j in range(nch):
            stack.append(struct.unpack_from('<I', c, o + 16 + j * 4)[0])
    # pose local
    ct = b.chunk(b.entries[k + 1])
    t, q = [], []
    offs = struct.unpack_from('<%dI' % n, ct, 0x38)       # offset do registro de cada no
    for i in range(n):
        o = offs[i]
        assert struct.unpack_from('<I', ct, o)[0] == i, ('registro', i)
        ot, orot = struct.unpack_from('<II', ct, o + 0x1c)  # onde ficam translacao e quaternion no registro
        t.append(struct.unpack_from('<3f', ct, o + ot) if ot else (0.0, 0.0, 0.0))
        q.append(struct.unpack_from('<4f', ct, o + orot) if orot else (0.0, 0.0, 0.0, 1.0))
    skin = []
    if desc_entry is not None:
        cd = b.chunk(desc_entry)
        nb = struct.unpack_from('<H', cd, 2)[0]
        for i in range(nb):
            ni = struct.unpack_from('<I', cd, 4 + i * 4)[0]
            nn = nm[ni] if ni < len(nm) else None
            skin.append(name.index(nn) if nn in name else -1)
    return dict(names=nm, parent=parent, name=name, t=t, q=q, skin=skin)


# -- matematica de quaternion/matriz (4x4 linha-maior, vetor coluna) -------------
def qmat(q):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def world(sk, conj=False):
    """matriz (R 3x3, t) de cada no no espaco do modelo."""
    n = len(sk['parent'])
    out = [None] * n

    def get(i):
        if out[i] is not None:
            return out[i]
        R = qmat(sk['q'][i])
        if conj:
            R = [[R[j][i2] for j in range(3)] for i2 in range(3)]
        t = sk['t'][i]
        p = sk['parent'][i]
        if p < 0:
            out[i] = (R, tuple(t))
        else:
            Rp, tp = get(p)
            Rw = [[sum(Rp[a][k] * R[k][c] for k in range(3)) for c in range(3)] for a in range(3)]
            tw = tuple(tp[a] + sum(Rp[a][k] * t[k] for k in range(3)) for a in range(3))
            out[i] = (Rw, tw)
        return out[i]

    for i in range(n):
        get(i)
    return out
