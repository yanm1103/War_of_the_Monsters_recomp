"""Gera um viewer HTML (WebGL puro) com as malhas dos monstros de Godzilla: Save the Earth (PS2).

O HTML embute assets do jogo: nao commitar (regra 1 do CLAUDE.md).

uso: python gen_viewer.py <iso> <saida.html> [monstros com animacoes de luta, ex. Godzilla2k Rodan; padrao: todos]
"""
import sys, base64, json, struct, io, zlib
import os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ste_model as S
import skeleton as K
import anim as A

MONSTERS = [
    ('Godzilla2k', 'Godzilla 2000'), ('Godzilla90s', 'Godzilla 90s'), ('MechaGodzilla2', 'Mechagodzilla 2'),
    ('MechaGodzilla3', 'Mechagodzilla 3'), ('SpaceGodzilla', 'SpaceGodzilla'), ('Anguirus', 'Anguirus'),
    ('Baragon', 'Baragon'), ('Biollante', 'Biollante'), ('Destoroyah', 'Destoroyah'),
    ('Destoroyah_Carnival', 'Destoroyah (carnival)'), ('Ebirah', 'Ebirah'), ('Ghidorah', 'King Ghidorah'),
    ('MGhidorah', 'Mecha-King Ghidorah'), ('Gigan', 'Gigan'), ('JetJaguar', 'Jet Jaguar'),
    ('JetJaguar_Carnival', 'Jet Jaguar (carnival)'), ('Megaguirus', 'Megaguirus'), ('Megalon', 'Megalon'),
    ('MOGUERA2', 'Moguera'), ('moguera', 'Moguera (bdp)'), ('Mothra', 'Mothra'), ('Mothra_Larvae', 'Mothra (larva)'),
    ('Orga', 'Orga'), ('Rodan', 'Rodan'),
]


ANIM_FOR = set(sys.argv[3:]) or {'all'}   # chaves de MONSTERS cujas animacoes do .cmp entram no HTML ('all' = todos)


def b64(buf):
    return base64.b64encode(buf).decode('ascii')


def png_bytes(w, h, rgba):
    import zlib
    raw = b''.join(b'\x00' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    def ck(t, c):
        x = struct.pack('>I', len(c)) + t + c
        return x + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + ck(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + ck(b'IDAT', zlib.compress(raw, 9)) + ck(b'IEND', b'')


def pack_skeleton(b, me, with_cmp_anims=False):
    """nos do esqueleto: nomes, pai e posicao de bind (espaco do modelo); None se nao achar."""
    try:
        sk = K.parse_skeleton(b, me)
        if not sk:
            return None
        W = K.world(sk, True)
        flat = [c for w in W for c in w[1]]
        cj = lambda q: (-q[0], -q[1], -q[2], q[3])   # quaternion invertido no disco: o viewer recebe o conjugado
        lt = [c for t in sk['t'] for c in t]
        lq = [c for q in sk['q'] for c in cj(q)]
        anims = []
        for a in A.parse_animations(b, sk['name'] if with_cmp_anims else None):
            if a['nodes'] != sk['name']:
                continue
            # chaves quantizadas em 16 bits (translacao normalizada pelo maximo por no/eixo) e comprimidas com zlib;
            # o viewer descomprime so quando a animacao e escolhida
            cnt, mxs, buf = [], [], bytearray()
            q16 = lambda v: max(-32767, min(32767, int(round(v * 32767))))
            for tr, rot in zip(a['tr'], a['rot']):
                cnt.append([len(tr), len(rot)])
                m = [max(abs(k[1 + j]) for k in tr) or 1.0 for j in range(3)]
                mxs += m
                for k in tr:
                    buf += struct.pack('<Hhhh', int(round(k[0] * 65534)), *[q16(k[1 + j] / m[j]) for j in range(3)])
                for k in rot:
                    qq = cj(k[1:])
                    if qq[3] < 0:
                        qq = tuple(-c for c in qq)
                    buf += struct.pack('<Hhhh', int(round(k[0] * 65534)), q16(qq[0]), q16(qq[1]), q16(qq[2]))
            anims.append(dict(name='%s (%.2fs)' % (a.get('name') or 'anim %d' % a['id'], a['dur']), dur=a['dur'], cnt=cnt,
                              mx=b64(struct.pack('<%df' % len(mxs), *mxs)), z=b64(zlib.compress(bytes(buf), 9))))
        return dict(names=[n.replace('Bip01 ', '') for n in sk['name']], parent=sk['parent'],
                    pos=b64(struct.pack('<%df' % len(flat), *flat)), skin=sk['skin'],
                    lt=b64(struct.pack('<%df' % len(lt), *lt)), lq=b64(struct.pack('<%df' % len(lq), *lq)), anims=anims)
    except Exception as ex:
        print('  esqueleto:', ex)
        return None


def pack_model(label, m, tex, sk=None):
    nv = len(m['pos'])
    pos = struct.pack('<%df' % (nv * 3), *[c for p in m['pos'] for c in p])
    nrm = struct.pack('<%db' % (nv * 3), *[max(-127, min(127, round(c * 127))) for n in m['nrm'] for c in n])
    uv = struct.pack('<%dH' % (nv * 2), *[min(65535, int(round(c * 4096))) for u in m['uv'] for c in u])
    bones = bytes(x for i in range(nv) for x in (m['b0'][i], m['b1'][i]))
    wts = struct.pack('<%dH' % nv, *[min(4096, int(round(w * 4096))) for w in m['w0']])
    idx = struct.pack('<%dH' % (len(m['tris']) * 3), *[i for t in m['tris'] for i in t])
    w, h, rgba = tex
    return dict(name=label, nv=nv, nt=len(m['tris']), nbones=m['nbones'], pos=b64(pos), nrm=b64(nrm), uv=b64(uv),
                bones=b64(bones), wts=b64(wts), idx=b64(idx), tex=b64(png_bytes(w, h, rgba)), sk=sk)


def main():
    iso = S.Iso(sys.argv[1])
    vol = S.Vol(iso)
    models = []
    for key, nice in MONSTERS:
        for ext in ('.cmp', '.bdp'):
            try:
                data = vol.get(key + ext)
            except StopIteration:
                continue
            try:
                b = S.Bundle(data)
                ms, tx = b.meshes(), b.textures()
                if not ms or not tx:
                    continue
                tex = S.decode_texture(b, tx[0])
                for me in ms:
                    m = S.decode_mesh(b, me)
                    if not m['tris']:
                        continue
                    tag = ext[1:] + ('' if len(ms) == 1 else ' #%d' % me['id'])
                    label = '%s [%s, %d tris]' % (nice, tag, len(m['tris']))
                    print(len(models), label)
                    # animacoes comprimidas do .cmp pesam ~3 MB por monstro: so para os pedidos na linha de comando
                    models.append(pack_model(label, m, tex, pack_skeleton(b, me, ('all' in ANIM_FOR or key in ANIM_FOR) and ext == '.cmp')))
            except Exception as ex:
                print('skip', key + ext, ex)
                continue
    tpl = open(os.path.join(HERE, 'viewer_template.html'), encoding='utf-8').read()
    html = tpl.replace('/*MODELS*/[]', json.dumps(models))
    open(sys.argv[2], 'w', encoding='utf-8').write(html)
    print('written', sys.argv[2], len(html) // 1024, 'KB')


main()
