"""Animacoes de Godzilla: Save the Earth (PS2): .bdp (float cru) e .cmp (comprimido).

Saida comum de parse_animations(b): lista de dict(id, dur, nodes=[nomes], tr=[[(t,x,y,z)...] por no],
rot=[[(t,x,y,z,w)...] por no]) com t em [0,1] (fracao da duracao); lista de 1 chave = osso parado.
Os quaternions saem no formato do disco (o conjugado e que vale, ver skeleton.py).

== .bdp: trio de entradas {arvore de nos, dados, registro 0x38 {0, id arvore, id dados}} ==
Dados: f32@0x1c = duracao (s), u32@0x24 = tamanho, u32@0x2c = n nos, offsets por no em 0x38+4*i; registro de no:
+8 n chaves de translacao, +0xc n de rotacao, +0x1c / +0x20 = offsets das listas. Chave de translacao 24 B
(x,y,z, dx,dy,dz); de rotacao 36 B (quat x,y,z,w + diferenca + pad); n chaves uniformes em `duracao`; n=1 = parado.

== .cmp: entrada com u32@0x20 == 2 (comprimida) ==
Cabecalho: f32@0x1c duracao, u32@0x24 tamanho, u32@0x2c = n nos (indice = no da arvore do .cmp), u32@0x3c = inicio
do fluxo de rotacoes. Dois fluxos, um bloco por no, na ordem dos nos:
- translacao (a partir de u32@0x38, 0x4c..0x54): 3 floats (maximo |valor| por eixo) + cabecalho {no, A, B, 0} + A chaves de 8 B
  {u16 t, s16 cx, cy, cz}; posicao = (cx,cy,cz)/32767 * maximo (o sinal esta no c; 1.0 = o maximo). t/0xfffe = fracao.
- rotacao (a partir de u32@0x3c): cabecalho {no, N, 0, 0} + 6 B (s16 x,y,z da chave 0, t=0) + (N-1) x {u16 t, s16 x,y,z};
  w = sqrt(1-x^2-y^2-z^2).
"""
import struct, math, re, collections
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


def _const(keys, eps=1e-5):
    return all(max(abs(a - b) for a, b in zip(k[1:], keys[0][1:])) < eps for k in keys)


def _slim(keys):
    """lista de chaves constante vira 1 chave."""
    return [keys[0]] if len(keys) > 1 and _const(keys) else keys


def parse_float_animations(b):
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
            tr = [(k / ntr,) + struct.unpack_from('<3f', c, o + ot + 24 * k) for k in range(ntr)]
            rot = [(k / nrot,) + struct.unpack_from('<4f', c, o + orot + 36 * k) for k in range(nrot)]
            trs.append(_slim(tr)); rots.append(_slim(rot))
            nk = max(nk, ntr, nrot)
        if nk > 1:
            out.append(dict(id=de['id'], dur=dur, nodes=names, tr=trs, rot=rots))
    return out


def _s16(v):
    return max(-1.0, (v - 65536 if v >= 32768 else v) / 32767.0)


def parse_compressed(b, entry, names):
    c = b.chunk(entry)
    if len(c) < 0x60 or struct.unpack_from('<4I', c, 0) != (0, 1, 1, 6) or struct.unpack_from('<I', c, 0x20)[0] != 2:
        return None
    if struct.unpack_from('<I', c, 0x24)[0] != len(c):
        return None
    n = struct.unpack_from('<I', c, 0x2c)[0]
    if n != len(names):
        return None
    dur = struct.unpack_from('<f', c, 0x1c)[0]
    rstart = struct.unpack_from('<I', c, 0x3c)[0]
    trs, rots = [None] * n, [None] * n
    o = struct.unpack_from('<I', c, 0x38)[0]     # inicio do fluxo de translacao (0x4c..0x54 conforme o monstro)
    for _ in range(n):
        mx = struct.unpack_from('<3f', c, o)
        node, A = c[o + 12], c[o + 13]
        if node >= n or c[o + 15]:
            return None
        keys = []
        for i in range(A):
            t, x, y, z = struct.unpack_from('<4H', c, o + 16 + 8 * i)
            keys.append((t / 65534.0, _s16(x) * mx[0], _s16(y) * mx[1], _s16(z) * mx[2]))
        trs[node] = _slim(keys)
        o += 16 + 8 * A
    o = rstart
    for _ in range(n):
        node, N = c[o], c[o + 1]
        if node >= n or c[o + 2] or c[o + 3] or N < 1:
            return None
        o += 4
        keys = []
        for i in range(N):
            if i == 0:
                t, (x, y, z) = 0.0, struct.unpack_from('<3h', c, o)
                o += 6
            else:
                tt, x, y, z = struct.unpack_from('<Hhhh', c, o)
                t = tt / 65534.0
                o += 8
            x, y, z = max(-1.0, x / 32767.0), max(-1.0, y / 32767.0), max(-1.0, z / 32767.0)
            w = math.sqrt(max(0.0, 1.0 - x * x - y * y - z * z))
            keys.append((t, x, y, z, w))
        rots[node] = _slim(keys)
    if o != len(c) or None in trs or None in rots:
        return None
    return dict(id=entry['id'], dur=dur, nodes=names, tr=trs, rot=rots, kind='cmp')


def parse_compressed_animations(b, names):
    out = []
    for e in b.entries:
        a = parse_compressed(b, e, names)
        if a:
            out.append(a)
    return out


def name_animations(b, anims):
    """Poe `name` nas animacoes comprimidas a partir da tabela de entradas do bundle (skeleton.entry_names): o nome de cada
    entrada sem o prefixo do monstro (GODZILLA2K_WALK -> WALK)."""
    nm = K.names_of(b)
    root = None
    for n in nm:
        m = re.match(r'(.+?)_', n)
        if m and n.upper().endswith('_SKELETON') and '.' not in n:
            root = m.group(1)
    names = K.entry_names(b)
    pre = re.compile(r'^%s[A-Z]?_' % re.escape(root), re.I) if root else None
    todo = [a for a in anims if a.get('kind') == 'cmp' and a['id'] in names]
    for a in todo:
        a['name'] = pre.sub('', names[a['id']][1]) if pre else names[a['id']][1]
    # prefixo que o nome do monstro nao cobre (JAGUAR_, GODZILLA_, LARVAE_): tira o primeiro termo se a maioria o compartilha
    first = collections.Counter(a['name'].split('_')[0] for a in todo if '_' in a['name'])
    if first:
        tok, n = first.most_common(1)[0]
        if n * 2 >= len(todo):
            for a in todo:
                if a['name'].startswith(tok + '_'):
                    a['name'] = a['name'][len(tok) + 1:]
    return len(todo)


def parse_animations(b, sk_names=None):
    """.bdp: trios de float cru; .cmp: animacoes comprimidas (precisa dos nomes dos nos do esqueleto)."""
    out = parse_float_animations(b)
    if sk_names:
        out += parse_compressed_animations(b, sk_names)
        name_animations(b, out)
    return out
