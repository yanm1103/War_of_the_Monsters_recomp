"""Exporta um monstro de Godzilla: Save the Earth (PS2) como Collada (.dae) com esqueleto, pesos e textura, no formato que
o tools/retarget.py le (Z para cima, frente +y, pose de bind na cena, nomes de junta com '_' no lugar de espaco).

  python tools/ste/export_dae.py <iso> <arquivo do VOL: Godzilla2k.cmp | godzilla2k.bdp> <pasta de saida> [--name NOME] [--mesh N]

Grava <NOME>.dae e <NOME>.png na pasta de saida (nada disso vai para o repo: sao dados do jogo). Depois:
  python tools/retarget.py <LIZARD0.glb> <NOME>.dae <mapa.json> rig.glb      # o mapa de ossos (tools/rigmaps/)
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ste_model as S
import skeleton as K


def jname(n):
    return re.sub(r'[^A-Za-z0-9_]', '_', n)


def fmt(vals):
    return ' '.join('%.7g' % v for v in vals)


def row_major(R, t):
    return [R[0][0], R[0][1], R[0][2], t[0], R[1][0], R[1][1], R[1][2], t[1], R[2][0], R[2][1], R[2][2], t[2], 0, 0, 0, 1]


def build(b, me, name, png):
    m = S.decode_mesh(b, me)
    sk = K.parse_skeleton(b, me)
    W = K.world(sk, True)                                 # (R, t) de cada no no espaco do modelo, em repouso
    n = len(sk['parent'])
    names = [jname(x) for x in sk['name']]
    assert len(set(names)) == n, 'nomes de junta repetidos'

    # pesos: osso 0 com w, osso 1 com 1-w (rigido: um osso so); osso de pele -> no
    skin_nodes = sorted({sk['skin'][bi] for pair in zip(m['b0'], m['b1']) for bi in pair if bi < len(sk['skin']) and sk['skin'][bi] >= 0})
    jpos = {nd: k for k, nd in enumerate(skin_nodes)}
    vw = []
    root = jpos.get(names.index('Bip01_Pelvis')) if 'Bip01_Pelvis' in names else 0
    for i in range(len(m['pos'])):
        acc = {}
        for bi, w in ((m['b0'][i], m['w0'][i]), (m['b1'][i], 1.0 - m['w0'][i])):
            nd = sk['skin'][bi] if bi < len(sk['skin']) else -1
            if nd >= 0 and w > 1e-6:
                acc[jpos[nd]] = acc.get(jpos[nd], 0.0) + w
        if not acc:
            acc = {root or 0: 1.0}
        tot = sum(acc.values())
        vw.append([(j, w / tot) for j, w in acc.items()])

    nv = len(m['pos'])
    o = []
    o.append('<?xml version="1.0" encoding="utf-8"?>')
    o.append('<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">')
    o.append('<asset><contributor><authoring_tool>tools/ste/export_dae.py</authoring_tool></contributor><unit name="unit" meter="1"/>'
             '<up_axis>Z_UP</up_axis></asset>')
    o.append('<library_images><image id="img" name="img"><init_from>%s</init_from></image></library_images>' % png)
    o.append('<library_effects><effect id="eff"><profile_COMMON>'
             '<newparam sid="surf"><surface type="2D"><init_from>img</init_from></surface></newparam>'
             '<newparam sid="samp"><sampler2D><source>surf</source></sampler2D></newparam>'
             '<technique sid="common"><phong><diffuse><texture texture="samp" texcoord="UV"/></diffuse></phong></technique>'
             '</profile_COMMON></effect></library_effects>')
    o.append('<library_materials><material id="mat" name="mat"><instance_effect url="#eff"/></material></library_materials>')

    # malha: um indice por vertice serve a posicao, a normal e o UV
    o.append('<library_geometries><geometry id="geo" name="%s"><mesh>' % name)
    o.append('<source id="geo-pos"><float_array id="geo-pos-a" count="%d">%s</float_array><technique_common>'
             '<accessor source="#geo-pos-a" count="%d" stride="3"><param name="X" type="float"/><param name="Y" type="float"/>'
             '<param name="Z" type="float"/></accessor></technique_common></source>' % (nv * 3, fmt([c for p in m['pos'] for c in p]), nv))
    o.append('<source id="geo-nrm"><float_array id="geo-nrm-a" count="%d">%s</float_array><technique_common>'
             '<accessor source="#geo-nrm-a" count="%d" stride="3"><param name="X" type="float"/><param name="Y" type="float"/>'
             '<param name="Z" type="float"/></accessor></technique_common></source>' % (nv * 3, fmt([c for p in m['nrm'] for c in p]), nv))
    o.append('<source id="geo-uv"><float_array id="geo-uv-a" count="%d">%s</float_array><technique_common>'
             '<accessor source="#geo-uv-a" count="%d" stride="2"><param name="S" type="float"/><param name="T" type="float"/>'
             '</accessor></technique_common></source>' % (nv * 2, fmt([c for u in m['uv'] for c in (u[0], 1.0 - u[1])]), nv))
    o.append('<vertices id="geo-v"><input semantic="POSITION" source="#geo-pos"/></vertices>')
    o.append('<triangles material="matsym" count="%d"><input semantic="VERTEX" offset="0" source="#geo-v"/>'
             '<input semantic="NORMAL" offset="0" source="#geo-nrm"/><input semantic="TEXCOORD" offset="0" source="#geo-uv" set="0"/>'
             '<p>%s</p></triangles>' % (len(m['tris']), ' '.join('%d %d %d' % t for t in m['tris'])))
    o.append('</mesh></geometry></library_geometries>')

    # pele: juntas usadas, matriz inversa de ligacao = inversa do mundo em repouso
    ibm = []
    for nd in skin_nodes:
        R, t = W[nd]
        Rt = [[R[c][r] for c in range(3)] for r in range(3)]
        ti = [-sum(Rt[r][c] * t[c] for c in range(3)) for r in range(3)]
        ibm += row_major(Rt, ti)
    nj = len(skin_nodes)
    o.append('<library_controllers><controller id="ctl"><skin source="#geo">')
    o.append('<bind_shape_matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</bind_shape_matrix>')
    o.append('<source id="ctl-j"><Name_array id="ctl-j-a" count="%d">%s</Name_array><technique_common><accessor source="#ctl-j-a" '
             'count="%d"><param name="JOINT" type="name"/></accessor></technique_common></source>' % (nj, ' '.join(names[nd] for nd in skin_nodes), nj))
    o.append('<source id="ctl-i"><float_array id="ctl-i-a" count="%d">%s</float_array><technique_common><accessor source="#ctl-i-a" '
             'count="%d" stride="16"><param name="TRANSFORM" type="float4x4"/></accessor></technique_common></source>' % (nj * 16, fmt(ibm), nj))
    wl = [w for v in vw for _j, w in v]
    o.append('<source id="ctl-w"><float_array id="ctl-w-a" count="%d">%s</float_array><technique_common><accessor source="#ctl-w-a" '
             'count="%d"><param name="WEIGHT" type="float"/></accessor></technique_common></source>' % (len(wl), fmt(wl), len(wl)))
    o.append('<joints><input semantic="JOINT" source="#ctl-j"/><input semantic="INV_BIND_MATRIX" source="#ctl-i"/></joints>')
    vflat, k = [], 0
    for v in vw:
        for j, _w in v:
            vflat += [j, k]
            k += 1
    o.append('<vertex_weights count="%d"><input semantic="JOINT" offset="0" source="#ctl-j"/><input semantic="WEIGHT" offset="1" '
             'source="#ctl-w"/><vcount>%s</vcount><v>%s</v></vertex_weights>' % (nv, ' '.join(str(len(v)) for v in vw), ' '.join(map(str, vflat))))
    o.append('</skin></controller></library_controllers>')

    # cena: arvore de juntas (matriz local = rotacao do disco conjugada + translacao) e a malha ligada a ela
    kids = {}
    for i, p in enumerate(sk['parent']):
        kids.setdefault(p, []).append(i)

    def joint(i):
        R = K.qmat(sk['q'][i])
        Rl = [[R[c][r] for c in range(3)] for r in range(3)]       # conjugado = transposta
        s = '<node id="j%d" name="%s" sid="%s" type="JOINT"><matrix sid="transform">%s</matrix>' % (
            i, names[i], names[i], fmt(row_major(Rl, sk['t'][i])))
        for c in kids.get(i, []):
            s += joint(c)
        return s + '</node>'
    o.append('<library_visual_scenes><visual_scene id="scene"><node id="Armature" name="Armature" type="NODE">')
    for r in kids.get(-1, []):
        o.append(joint(r))
    o.append('</node><node id="Body" name="%s" type="NODE"><instance_controller url="#ctl"><skeleton>#j%d</skeleton>'
             '<bind_material><technique_common><instance_material symbol="matsym" target="#mat"/></technique_common></bind_material>'
             '</instance_controller></node></visual_scene></library_visual_scenes>' % (name, kids[-1][0]))
    o.append('<scene><instance_visual_scene url="#scene"/></scene></COLLADA>')
    return '\n'.join(o), m, sk


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    iso, fn, out = argv[0], argv[1], argv[2]
    name = fn.rsplit('.', 1)[0]
    mesh_i = 0
    if '--name' in argv:
        name = argv[argv.index('--name') + 1]
    if '--mesh' in argv:
        mesh_i = int(argv[argv.index('--mesh') + 1])
    vol = S.Vol(S.Iso(iso))
    b = S.Bundle(vol.get(fn))
    ms = b.meshes()
    me = ms[mesh_i]
    os.makedirs(out, exist_ok=True)
    png = name + '.png'
    w, h, rgba = S.decode_texture(b, b.textures()[0])
    S.write_png(os.path.join(out, png), w, h, rgba)
    xml, m, sk = build(b, me, name, png)
    with open(os.path.join(out, name + '.dae'), 'w', encoding='utf-8') as f:
        f.write(xml)
    print('%s: %d vertices, %d triangulos, %d nos (%d de pele) -> %s' % (fn, len(m['pos']), len(m['tris']), len(sk['parent']),
                                                                      len({x for x in sk['skin'] if x >= 0}), os.path.join(out, name + '.dae')))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
