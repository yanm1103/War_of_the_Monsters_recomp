"""Animacoes em float cru dos .bdp de Godzilla: Save the Earth (PS2).

Cada animacao e um trio de entradas: arvore de nos (como a do esqueleto), dados e um registro de 0x38 bytes
{0, id da arvore, id dos dados}. Entrada de dados: u32@0x1c = duracao (float, s), u32@0x24 = tamanho, u32@0x2c = n nos,
tabela de offsets por no em 0x38+4*i. Registro de no: +0 idx, +4 comprimento do osso, +8 n chaves de translacao,
+0xc n chaves de rotacao, +0x1c / +0x20 = offsets das duas listas dentro do registro.
Chave de translacao = 24 B (x,y,z, dx,dy,dz); chave de rotacao = 36 B (quaternion x,y,z,w + 4 floats de diferenca + pad).
Amostragem uniforme (n chaves em `duracao` segundos, ~30 fps); n = 1 significa osso parado.
O quaternion segue a mesma convencao invertida do esqueleto (usar o conjugado).
"""
import struct
import skeleton as K


def _tree_names(b, entry, nm):
    """nomes dos nos de uma entrada de arvore, na ordem do indice."""
    c = b.chunk(entry)
    n = struct.unpack_from('<I', c, 0x20)[0]
    out = [None] * n
    stack, seen = [0x40], set()
    while stack:
        o = stack.pop()
        if o in seen or o + 16 > len(c):
            continue
        seen.add(o)
        idx, par, nch, nidx = struct.unpack_from('<IIII', c, o)
        if idx >= n:
            continue
        out[idx] = nm[nidx] if nidx < len(nm) else '?%d' % nidx
        for j in range(nch):
            stack.append(struct.unpack_from('<I', c, o + 16 + j * 4)[0])
    return out


def parse_animations(b):
    """lista de dict(name, dur, nk, nodes=[nomes], tr=[lista de (x,y,z)], rot=[lista de (x,y,z,w)]) por no."""
    nm = K.names_of(b)
    byid = {e['id']: e for e in b.entries}
    out = []
    for e in b.entries:
        if e['size'] != 0x38:
            continue
        w = struct.unpack_from('<4I', b.chunk(e), 0)
        if w[0] != 0 or w[1] not in byid or w[2] not in byid:
            continue
        te, de = byid[w[1]], byid[w[2]]
        c = b.chunk(de)
        if len(c) < 0x40 or struct.unpack_from('<I', c, 0x24)[0] != len(c):
            continue
        n = struct.unpack_from('<I', c, 0x2c)[0]
        names = _tree_names(b, te, nm)
        if len(names) != n:
            continue
        dur = struct.unpack_from('<f', c, 0x1c)[0]
        offs = struct.unpack_from('<%dI' % n, c, 0x38)
        trs, rots, nk = [], [], 1
        for i in range(n):
            o = offs[i]
            ntr, nrot = struct.unpack_from('<2I', c, o + 8)
            ot, orot = struct.unpack_from('<2I', c, o + 0x1c)
            trs.append([struct.unpack_from('<3f', c, o + ot + 24 * k) for k in range(ntr)])
            rots.append([struct.unpack_from('<4f', c, o + orot + 36 * k) for k in range(nrot)])
            nk = max(nk, ntr, nrot)
        out.append(dict(id=de['id'], dur=dur, nk=nk, nodes=names, tr=trs, rot=rots))
    return out
