"""CJ (GTA SA player clothes pieces) -> one skinned Collada for tools/retarget.py.
  python cj2dae.py <raw dir with the .dff/.txd> <out dir> [--clump 2]
Pieces: head, hands, vest (over the torso skin), jeans, sneakers. Clump 0 = ripped, 1 = fat, 2 = normal.
The model faces +X in the files; a root node turns it to face +Y.
"""
import os
import sys
from xml.sax.saxutils import escape

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw  # noqa: E402

raw, out = sys.argv[1], sys.argv[2]
clump = int(sys.argv[sys.argv.index("--clump") + 1]) if "--clump" in sys.argv else 2
os.makedirs(out, exist_ok=True)


def tex(txd, name):
    return rw.read_txd(os.path.join(raw, txd))[name]


def over(top, bottom):
    w, h, a = top[0], top[1], top[2]
    assert (w, h) == bottom[:2]
    b = bottom[2]
    o = bytearray(len(a))
    for i in range(0, len(a), 4):
        al = a[i + 3] / 255.0
        for c in range(3):
            o[i + c] = int(round(a[i + c] * al + b[i + c] * (1 - al)))
        o[i + 3] = 255
    return (w, h, bytes(o))


TEX = {
    "cj_head": tex(os.environ.get("CJ_FACE_TXD", "player_face.txd"), os.environ.get("CJ_FACE_TEX", "face")),
    "cj_skin": tex("hands.txd", "torso8bit"),
    "cj_torso": over(tex("vest.txd", "vest"), tex("player_torso.txd", "torso")),
    "cj_legs": tex("jeansdenim.txd", "legs"),
    "cj_shoes": tex("sneakerbincblk.txd", "sneakerbincblk"),
}
# rigid props on a joint: name -> (joint, geometry, texture); the katana when katana.dff/.txd are in the raw dir
PROPS = {}
if os.path.exists(os.path.join(raw, "katana.dff")):
    TEX["cj_katana"] = tex("katana.txd", "gun_katana")
    PROPS["katana"] = ("R_Hand", rw.read_dff(os.path.join(raw, "katana.dff"))[0][1][0], "cj_katana")
# the heat-seeking rocket launcher (Robo-47's special: the missile) when heatseek.dff/.txd are in the raw dir
if os.path.exists(os.path.join(raw, "heatseek.dff")):
    TEX["cj_bazooka"] = tex("heatseek.txd", "heatseeker")
    PROPS["bazooka"] = ("R_Hand", rw.read_dff(os.path.join(raw, "heatseek.dff"))[0][1][0], "cj_bazooka")
for k, (w, h, rgba, *_r) in TEX.items():
    rgba = bytearray(rgba)
    for i in range(3, len(rgba), 4):
        rgba[i] = 255
    rw.write_png(os.path.join(out, k + ".png"), w, h, bytes(rgba))

PIECES = [("head.dff", ["cj_head", "cj_head"]), ("hands.dff", ["cj_skin"]), ("vest.dff", ["cj_torso"]),
          ("jeans.dff", ["cj_legs"]), ("sneaker.dff", ["cj_shoes"])]


def jname(s):
    return s.strip().replace(" ", "_") or "root"


def mat_mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def ibm_rows(f16):
    """RW matrix (rows right, up, at, pos; the 4th column holds flags, not 0 0 0 1) -> column-vector rows."""
    return [[f16[c * 4 + r] for c in range(4)] for r in range(3)] + [[0.0, 0.0, 0.0, 1.0]]


frames = None
pieces = []
for fn, texs in PIECES:
    fr, geos, atomics = rw.read_dff(os.path.join(raw, fn))[clump]
    if frames is None:
        frames = fr
        # the HAnim root ("Normal") carries an axis swap the skin's bind space does not have (the game's animation
        # replaces that matrix): at rest it is the identity
        for f in frames:
            if f.hanim_nodes:
                f.rot = (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)
    else:
        assert [f.name for f in fr] == [f.name for f in frames], fn
    assert len(geos) == 1, fn
    g = geos[0]
    root = [f for f in fr if f.hanim_nodes][0]
    by_id = {f.hanim_id: i for i, f in enumerate(fr) if f.hanim_id is not None}
    bone_frame = [by_id[nid] for (nid, nidx, fl) in sorted(root.hanim_nodes, key=lambda t: t[1])]
    pieces.append((fn[:-4], g, texs, bone_frame))

# world of each frame at rest = the skin's bind pose (inverse of its inverse bind matrix); the frames' own matrices
# differ from it (the spine's roll), so the joints' local matrices are rebuilt from the bind pose


def inv_rigid(m):
    R = [[m[c][r] for c in range(3)] for r in range(3)]
    t = [-sum(R[r][k] * m[k][3] for k in range(3)) for r in range(3)]
    return [R[0] + [t[0]], R[1] + [t[1]], R[2] + [t[2]], [0.0, 0.0, 0.0, 1.0]]


bind = {}
for name, g, texs, bf in pieces:
    for k, f16 in enumerate(g.skin["ibm"]):
        bind.setdefault(bf[k], inv_rigid(ibm_rows(f16)))
world = []
for i, f in enumerate(frames):
    L = f.local()
    world.append(bind.get(i) or (L if f.parent < 0 else mat_mul(world[f.parent], L)))
LOCAL = [world[i] if f.parent < 0 else mat_mul(inv_rigid(world[f.parent]), world[i]) for i, f in enumerate(frames)]
worst = 0.0
for name, g, texs, bf in pieces:
    for k, f16 in enumerate(g.skin["ibm"]):
        M = mat_mul(world[bf[k]], ibm_rows(f16))
        I = [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]
        worst = max(worst, max(abs(M[r][c] - I[r][c]) for r in range(4) for c in range(4)))
print("frames world x invBind: worst deviation from identity %.4f" % worst)


def mtxt(m):
    return " ".join("%.6f" % m[r][c] for r in range(4) for c in range(4))


children = {}
for i, f in enumerate(frames):
    children.setdefault(f.parent, []).append(i)


def node_xml(i, ind):
    f = frames[i]
    nm = jname(f.name) if i else "cj_frame0"
    s = '%s<node id="%s" sid="%s" name="%s" type="JOINT">\n%s  <matrix sid="transform">%s</matrix>\n' % (
        ind, nm, nm, nm, ind, mtxt(LOCAL[i]))
    for pname, (pjoint, _g, ptex) in PROPS.items():   # rigid, in the joint's space (GTA draws weapons on the hand bone)
        if pjoint == nm:
            s += ('%s  <node id="%s" name="%s"><instance_geometry url="#%s_geo"><bind_material><technique_common>'
                  '<instance_material symbol="m0" target="#%s_mat"/></technique_common></bind_material>'
                  '</instance_geometry></node>\n' % (ind, pname, pname, pname, ptex))
    for c in children.get(i, []):
        s += node_xml(c, ind + "  ")
    return s + ind + "</node>\n"


lib_img, lib_fx, lib_mat, lib_geo, lib_ctrl, scene_inst = [], [], [], [], [], []
for k in TEX:
    lib_img.append('<image id="%s_img" name="%s_img"><init_from>%s.png</init_from></image>' % (k, k, k))
    lib_fx.append('<effect id="%s_fx"><profile_COMMON><newparam sid="surf"><surface type="2D"><init_from>%s_img</init_from>'
                  '</surface></newparam><newparam sid="samp"><sampler2D><source>surf</source></sampler2D></newparam>'
                  '<technique sid="common"><lambert><diffuse><texture texture="samp" texcoord="UV0"/></diffuse></lambert>'
                  '</technique></profile_COMMON></effect>' % (k, k))
    lib_mat.append('<material id="%s_mat" name="%s"><instance_effect url="#%s_fx"/></material>' % (k, k, k))

for name, g, texs, bf in pieces:
    nv = len(g.pos)
    P = " ".join("%.6f %.6f %.6f" % v for v in g.pos)
    N = " ".join("%.6f %.6f %.6f" % v for v in g.nrm)
    T = " ".join("%.6f %.6f" % (u, 1.0 - v) for u, v in g.uv[0])
    tris_xml = ""
    for mi, tk in enumerate(texs):
        ts = [t for t in g.tris if t[3] == mi]
        if not ts:
            continue
        p = " ".join("%d %d %d" % (v, v, v) for t in ts for v in t[:3])
        tris_xml += ('<triangles material="m%d" count="%d"><input semantic="VERTEX" source="#%s_v" offset="0"/>'
                     '<input semantic="NORMAL" source="#%s_n" offset="1"/><input semantic="TEXCOORD" source="#%s_t" offset="2" set="0"/>'
                     '<p>%s</p></triangles>' % (mi, len(ts), name, name, name, p))
    lib_geo.append(
        '<geometry id="%s_geo" name="%s"><mesh>'
        '<source id="%s_p"><float_array id="%s_pa" count="%d">%s</float_array><technique_common><accessor source="#%s_pa" count="%d" stride="3">'
        '<param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>'
        '<source id="%s_n"><float_array id="%s_na" count="%d">%s</float_array><technique_common><accessor source="#%s_na" count="%d" stride="3">'
        '<param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>'
        '<source id="%s_t"><float_array id="%s_ta" count="%d">%s</float_array><technique_common><accessor source="#%s_ta" count="%d" stride="2">'
        '<param name="S" type="float"/><param name="T" type="float"/></accessor></technique_common></source>'
        '<vertices id="%s_v"><input semantic="POSITION" source="#%s_p"/></vertices>%s</mesh></geometry>' % (
            name, name, name, name, 3 * nv, P, name, nv, name, name, 3 * nv, N, name, nv, name, name, 2 * nv, T, name, nv,
            name, name, tris_xml))
    sk = g.skin
    jn = [jname(frames[bf[k]].name) for k in range(sk["nbones"])]
    ibm = " ".join(mtxt(ibm_rows(f16)) for f16 in sk["ibm"])
    wv, vcount, vv = [], [], []
    for i in range(nv):
        c = 0
        for bi, w in zip(sk["idx"][i], sk["w"][i]):
            if w > 0:
                vv += [bi, len(wv)]
                wv.append(w)
                c += 1
        vcount.append(c)
    lib_ctrl.append(
        '<controller id="%s_skin"><skin source="#%s_geo"><bind_shape_matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</bind_shape_matrix>'
        '<source id="%s_j"><Name_array id="%s_ja" count="%d">%s</Name_array><technique_common><accessor source="#%s_ja" count="%d" stride="1">'
        '<param name="JOINT" type="name"/></accessor></technique_common></source>'
        '<source id="%s_ib"><float_array id="%s_iba" count="%d">%s</float_array><technique_common><accessor source="#%s_iba" count="%d" stride="16">'
        '<param name="TRANSFORM" type="float4x4"/></accessor></technique_common></source>'
        '<source id="%s_w"><float_array id="%s_wa" count="%d">%s</float_array><technique_common><accessor source="#%s_wa" count="%d" stride="1">'
        '<param name="WEIGHT" type="float"/></accessor></technique_common></source>'
        '<joints><input semantic="JOINT" source="#%s_j"/><input semantic="INV_BIND_MATRIX" source="#%s_ib"/></joints>'
        '<vertex_weights count="%d"><input semantic="JOINT" source="#%s_j" offset="0"/><input semantic="WEIGHT" source="#%s_w" offset="1"/>'
        '<vcount>%s</vcount><v>%s</v></vertex_weights></skin></controller>' % (
            name, name, name, name, len(jn), " ".join(jn), name, len(jn), name, name, 16 * len(jn), ibm, name, len(jn),
            name, name, len(wv), " ".join("%.6f" % w for w in wv), name, len(wv), name, name, nv, name, name,
            " ".join(map(str, vcount)), " ".join(map(str, vv))))
    binds = "".join('<instance_material symbol="m%d" target="#%s_mat"/>' % (mi, tk) for mi, tk in enumerate(texs))
    scene_inst.append('<node id="%s_node" name="%s"><instance_controller url="#%s_skin"><skeleton>#root_cj</skeleton>'
                      '<bind_material><technique_common>%s</technique_common></bind_material></instance_controller></node>' % (
                          name, name, name, binds))

for pname, (pjoint, g, ptex) in PROPS.items():
    nv = len(g.pos)
    nrm = g.nrm or [(0.0, 0.0, 1.0)] * nv
    p = " ".join("%d" % v for t in g.tris for v in t[:3])
    lib_geo.append(
        '<geometry id="%s_geo" name="%s"><mesh>'
        '<source id="%s_p"><float_array id="%s_pa" count="%d">%s</float_array><technique_common><accessor source="#%s_pa" count="%d" stride="3">'
        '<param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>'
        '<source id="%s_n"><float_array id="%s_na" count="%d">%s</float_array><technique_common><accessor source="#%s_na" count="%d" stride="3">'
        '<param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>'
        '<source id="%s_t"><float_array id="%s_ta" count="%d">%s</float_array><technique_common><accessor source="#%s_ta" count="%d" stride="2">'
        '<param name="S" type="float"/><param name="T" type="float"/></accessor></technique_common></source>'
        '<vertices id="%s_v"><input semantic="POSITION" source="#%s_p"/></vertices>'
        '<triangles material="m0" count="%d"><input semantic="VERTEX" source="#%s_v" offset="0"/>'
        '<input semantic="NORMAL" source="#%s_n" offset="0"/><input semantic="TEXCOORD" source="#%s_t" offset="0" set="0"/>'
        '<p>%s</p></triangles></mesh></geometry>' % (
            pname, pname, pname, pname, 3 * nv, " ".join("%.6f %.6f %.6f" % v for v in g.pos), pname, nv,
            pname, pname, 3 * nv, " ".join("%.6f %.6f %.6f" % v for v in nrm), pname, nv,
            pname, pname, 2 * nv, " ".join("%.6f %.6f" % (u, 1.0 - v) for u, v in g.uv[0]), pname, nv,
            pname, pname, len(g.tris), pname, pname, pname, p))

skel = node_xml(0, "      ")
doc = ('<?xml version="1.0" encoding="utf-8"?>\n<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">\n'
       '<asset><unit name="meter" meter="1"/><up_axis>Z_UP</up_axis></asset>\n'
       '<library_images>%s</library_images>\n<library_effects>%s</library_effects>\n<library_materials>%s</library_materials>\n'
       '<library_geometries>%s</library_geometries>\n<library_controllers>%s</library_controllers>\n'
       '<library_visual_scenes><visual_scene id="scene" name="scene">\n'
       '  <node id="root_cj" name="root_cj"><rotate>0 0 1 90</rotate>\n%s    %s\n  </node>\n'
       '</visual_scene></library_visual_scenes>\n<scene><instance_visual_scene url="#scene"/></scene>\n</COLLADA>\n') % (
    "".join(lib_img), "".join(lib_fx), "".join(lib_mat), "".join(lib_geo), "".join(lib_ctrl), skel, "\n    ".join(scene_inst))
with open(os.path.join(out, "cj.dae"), "w", encoding="utf-8") as f:
    f.write(doc)
print("wrote", os.path.join(out, "cj.dae"), "joints:", ", ".join(jname(f.name) for f in frames[1:]))
