"""Gera um viewer HTML (WebGL puro) com as malhas dos monstros de Godzilla: Save the Earth (PS2).

O HTML embute assets do jogo: nao commitar (regra 1 do CLAUDE.md).

uso: python gen_viewer.py <iso> <saida.html>
"""
import sys, base64, json, struct, io
import os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ste_model as S
import skeleton as K

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


def b64(buf):
    return base64.b64encode(buf).decode('ascii')


def png_bytes(w, h, rgba):
    import zlib
    raw = b''.join(b'\x00' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    def ck(t, c):
        x = struct.pack('>I', len(c)) + t + c
        return x + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + ck(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + ck(b'IDAT', zlib.compress(raw, 9)) + ck(b'IEND', b'')


def pack_skeleton(b, me):
    """nos do esqueleto: nomes, pai e posicao de bind (espaco do modelo); None se nao achar."""
    try:
        sk = K.parse_skeleton(b, me)
        if not sk:
            return None
        W = K.world(sk, True)
        flat = [c for w in W for c in w[1]]
        return dict(names=[n.replace('Bip01 ', '') for n in sk['name']], parent=sk['parent'],
                    pos=b64(struct.pack('<%df' % len(flat), *flat)), skin=sk['skin'])
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
                    models.append(pack_model(label, m, tex, pack_skeleton(b, me)))
            except Exception as ex:
                print('skip', key + ext, ex)
                continue
    tpl = open(os.path.join(HERE, 'viewer_template.html'), encoding='utf-8').read()
    html = tpl.replace('/*MODELS*/[]', json.dumps(models))
    open(sys.argv[2], 'w', encoding='utf-8').write(html)
    print('written', sys.argv[2], len(html) // 1024, 'KB')


main()
