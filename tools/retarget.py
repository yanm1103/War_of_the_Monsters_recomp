#!/usr/bin/env python3
"""A game monster's animations on another model's skeleton: a rigged model (Collada .dae, or glTF .glb / .gltf) and a bone map
become the rig .glb that `modkit.py monster rig` (tools/ngprig.py) takes.

  python tools/retarget.py <source.glb> <model.dae|.glb|.gltf> <map.json> <out.glb> [--size F | --height H]
                           [--front +y|-y] [--up y|z] [--clips 90,1,...] [--own clips.json] [--base base.NGP]

- source.glb: the base costume as the kit exports it (`modkit.py monster mesh export`, e.g. LIZARD0.glb, "WotM" root): its
  skeleton and its actions `a<id>` are the motion.
- model: the new body, a skinned mesh on an armature; its scene pose is the bind pose. Up axis: the .dae's `<up_axis>` (a
  .dae without one: Y when its head joint sits above the top joint along Y, else Z), glTF is Y up; `--up` says it. A Y-up
  scene is turned to Z up and then faces -Y (glTF's +Z front); `--front` is the way it faces once Z is up (default -y for a
  Y-up scene, +y for a Z-up one). Textures: the .dae effect's diffuse image next to the file, or the glTF base color image
  (inside or next to it); a missing one, or one that is not PNG or JPEG, leaves those triangles untextured, with a warning.
- map.json: which model joint follows which source bone (`tools/rigmaps/biped_togera.json` is the 3ds Max Biped, Bip01_*,
  on Togera); `auto` makes one from the two skeletons (`tools/automap.py`) and writes it next to out.glb as
  `<out>_map.json`, to edit and pass back. A source bone is named as the export names it (`L_Hand_bone1`), by its label
  (`L_Hand`) or by `bone1`. Keys:
  - "bones": {joint: {"from": bone | [bone_a, bone_b, f], "move": true, "align": [child_joint, child_bone]}}. The joint turns
    in the world as its source bone turns from its rest (with [a, b, f], a blend of the two turns at f); "move" also carries
    the bone's change of position (scaled by the ratio of the two rest heights); "align" first turns the joint so that the
    direction to child_joint is the source's direction to child_bone (the model is posed like the source before the motion
    goes in, e.g. arms from a T-pose to the source's); "level": true lays the joint's underside flat on the ground at rest
    (a foot the model's bind pose leaves tilted; the plane through the lowest point of each cell of a grid over the vertices
    it carries, toes included); "turn": [x, y, z] turns it at rest by those degrees about the game axes (+x lifts what
    faces forward: closes a jaw the bind pose leaves open). A joint with "turn" and no "from" is only turned at rest and
    stays on the joint above it (fingers closed into a fist, a mouth left open): no channels of its own.
  - "chains": [{"joints": [...], "from": [...]}]: two chains of different lengths (tails), matched by their position along
    the chain.
  - "clipBones": [{"clips": [100, 101, ...], "bones": {joint: {"from": bone | [bone_a, bone_b, f], "aim": [child_joint,
    child_bone]}}}]: in those animation ids the joint turns as these source bones turn instead of its "bones" entry (rest,
    "move" and "align" stay), and "aim" then turns it, at every frame, so that the direction to child_joint is the source's
    direction from its bone to child_bone. For poses that read wrong on a body shaped otherwise: an upright torso and a
    short neck that take a forward-leaning lizard's turn up in the climb lean back and tip the head over; aiming the spine
    and neck along the source's keeps them on the wall, and [head, neck, 0.5] halves the head's turn.
  - "locators": {alias or loc_<hex>: {"joint": joint, "at": [x, y, z] | [joint, ...]}}: a point in the model's units (before
    the size) or the mean of some joints. Every locator of the source that the map does not name goes on the joint its
    source bone maps to (or the nearest mapped ancestor), with the source offset scaled.
  - "keep": joints kept without a source (they hold the rest pose against their parent).
  - "props": {node: {"show": source node}}: a rigid mesh of the model (a node with instance_geometry under a joint) is seen as
    much as that source node is in each clip (its scale: a weapon shown only in some moves; hidden in an own clip with an id
    the source lacks); "show": [160, 161] = seen only in the actions with those ids. Props the map leaves out are always seen.
  Joints the map does not reach are rigid on the nearest kept ancestor: their weights go to it.
- Size: the model is scaled to the source's height (`--size 1.2` = 20% taller; `--height H` in game units), feet on the
  ground. The out.glb is in the game's axes and units (the "WotM" root), so ngprig takes it as is.
- --own clips.json: the model's own animations, in place of the source's with the same id (or as new ids):
  {"clips": [{"name": "a090", "frames": N, "joints": {joint: [[qx, qy, qz, qw, x, y, z] per frame]}}]}, each the joint's
  world in the model's scene (the .dae's axes and units, at 60 frames a second). Kept joints a clip leaves out hold their
  rest against their parent; the map does not act on these clips. "laid": {"turn": [x, y, z], "shift": [x, y, z]} marks a
  pose the game lays down itself (a monster on the ground: the knocked-down clips are stored standing): the game turns it
  by those degrees about the game axes, then shifts it by those game units; the clip gives the pose as seen on the ground
  and is stored before that.

Only the standard library: it runs where the mod kit runs. Guide: docs/guides/MODS.md ("Own skeleton"); docs/guides/MODELS_3D.md.
"""
import argparse
import json
import math
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ngpanim  # noqa: E402
import ngpmesh  # noqa: E402
from ngpanim import ROOT_Q, eye, inv, mul, quat_col  # noqa: E402

FPS = 60.0


class RetargetError(Exception):
    pass


# ---------------------------------------------------------------- 4x4 column-vector matrices (rows[r][c]) and quaternions

def pos(m):
    return [m[0][3], m[1][3], m[2][3]]


def rot3(m):
    """The rotation of m (Gram-Schmidt on the columns), as a 4x4 without translation."""
    cols = [[m[r][c] for r in range(3)] for c in range(3)]
    x = _unit(cols[0])
    y = cols[1]
    d = sum(a * b for a, b in zip(x, y))
    y = _unit([a - d * b for a, b in zip(y, x)])
    z = _cross(x, y)
    if sum(a * b for a, b in zip(z, cols[2])) < 0:   # mirrored: keep the handedness of a rotation
        z = [-c for c in z]
    return [[x[0], y[0], z[0], 0.0], [x[1], y[1], z[1], 0.0], [x[2], y[2], z[2], 0.0], [0.0, 0.0, 0.0, 1.0]]


def with_pos(R, p):
    m = [row[:] for row in R]
    for r in range(3):
        m[r][3] = p[r]
    return m


def tmat(m):
    """Transpose of the 3x3 part (inverse of a rotation)."""
    return [[m[0][0], m[1][0], m[2][0], 0.0], [m[0][1], m[1][1], m[2][1], 0.0], [m[0][2], m[1][2], m[2][2], 0.0],
            [0.0, 0.0, 0.0, 1.0]]


def apply(m, p, w=1.0):
    return [m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] * w for r in range(3)]


def _cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def _unit(a):
    n = math.sqrt(sum(c * c for c in a))
    return [c / n for c in a] if n > 1e-12 else [0.0, 0.0, 0.0]


def quat_of(m):
    """glTF quaternion (x, y, z, w) of the rotation part of m."""
    a, b, c = m[0][0], m[1][1], m[2][2]
    t = a + b + c
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        q = [(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s]
    elif a > b and a > c:
        s = math.sqrt(1.0 + a - b - c) * 2
        q = [0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s]
    elif b > c:
        s = math.sqrt(1.0 + b - a - c) * 2
        q = [(m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s]
    else:
        s = math.sqrt(1.0 + c - a - b) * 2
        q = [(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s]
    n = math.sqrt(sum(x * x for x in q))
    return [x / n for x in q]


def slerp(a, b, u):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0:
        b, d = [-x for x in b], -d
    if d > 0.9995:
        q = [x + (y - x) * u for x, y in zip(a, b)]
    else:
        th = math.acos(d)
        s = math.sin(th)
        wa, wb = math.sin((1 - u) * th) / s, math.sin(u * th) / s
        q = [wa * x + wb * y for x, y in zip(a, b)]
    n = math.sqrt(sum(x * x for x in q))
    return [x / n for x in q]


def rot_between(a, b):
    """Smallest rotation taking direction a to direction b."""
    a, b = _unit(a), _unit(b)
    c = _cross(a, b)
    d = sum(x * y for x, y in zip(a, b))
    if d < -0.999999:
        ax = _unit(_cross(a, [1.0, 0.0, 0.0])) or _unit(_cross(a, [0.0, 1.0, 0.0]))
        return quat_col([ax[0], ax[1], ax[2], 0.0])
    q = [c[0], c[1], c[2], 1.0 + d]
    n = math.sqrt(sum(x * x for x in q))
    return quat_col([x / n for x in q])


# ---------------------------------------------------------------- the model (Collada)

NS = "{http://www.collada.org/2005/11/COLLADASchema}"
NS15 = "{http://www.collada.org/2008/03/COLLADASchema}"


def _floats(el):
    return [float(x) for x in (el.text or "").split()]


def _mat16(vals):
    """Collada matrix (row-major, column vectors) -> rows."""
    return [vals[0:4], vals[4:8], vals[8:12], vals[12:16]]


def _node_local(n):
    m = eye()
    for el in n:
        tag = el.tag.replace(NS, "")
        if tag == "matrix":
            m = mul(m, _mat16(_floats(el)))
        elif tag == "translate":
            x, y, z = _floats(el)
            m = mul(m, [[1, 0, 0, x], [0, 1, 0, y], [0, 0, 1, z], [0, 0, 0, 1]])
        elif tag == "rotate":
            x, y, z, ang = _floats(el)
            h = math.radians(ang) / 2
            ax = _unit([x, y, z])
            m = mul(m, quat_col([ax[0] * math.sin(h), ax[1] * math.sin(h), ax[2] * math.sin(h), math.cos(h)]))
        elif tag == "scale":
            x, y, z = _floats(el)
            m = mul(m, [[x, 0, 0, 0], [0, y, 0, 0], [0, 0, z, 0], [0, 0, 0, 1]])
    return m


class Model:
    """Joints (name, parent, scene world) and skinned triangles of a Collada file."""

    def __init__(self, path, up=None):
        self.path = path
        root = ET.parse(path).getroot()
        for el in root.iter():   # Collada 1.5 (Cinema 4D, Maya): the same elements under another namespace
            if isinstance(el.tag, str) and el.tag.startswith(NS15):
                el.tag = NS + el.tag[len(NS15):]
        self.joints = []          # [{"name", "parent", "world"}]
        jidx = {}                 # sid / id / name -> joint index
        inst = []                 # (node world, instance_controller element)
        rigid = []                # (node name, joint, node world, instance_geometry element)
        scene = root.find(NS + "library_visual_scenes/" + NS + "visual_scene")
        if scene is None:
            raise RetargetError("%s: no visual scene" % path)

        def walk(n, P, parent):
            W = mul(P, _node_local(n))
            me = parent
            if n.get("type") == "JOINT":
                me = len(self.joints)
                self.joints.append({"name": n.get("name") or n.get("id"), "parent": parent, "world": W})
                for k in (n.get("sid"), n.get("id"), n.get("name")):
                    if k:
                        jidx.setdefault(k, me)
            for ic in n.findall(NS + "instance_controller"):
                inst.append((W, ic))
            for ig in n.findall(NS + "instance_geometry"):
                if me is not None:   # a rigid prop on a joint
                    rigid.append((n.get("name") or n.get("id"), me, W, ig))
            for c in n.findall(NS + "node"):
                walk(c, W, me)
        for n in scene.findall(NS + "node"):
            walk(n, eye(), None)
        if not self.joints:
            raise RetargetError("%s: no joints (an armature) in the scene" % path)
        self.names = {j["name"]: i for i, j in enumerate(self.joints)}

        by_id = {el.get("id"): el for el in root.iter() if el.get("id")}
        images = {}
        for im in root.iter(NS + "image"):
            f = im.find(NS + "init_from")
            if f is not None and f.find(NS + "ref") is not None:   # 1.5: <init_from><ref>file</ref></init_from>
                f = f.find(NS + "ref")
            if f is not None and f.text and f.text.strip():
                images[im.get("id")] = os.path.join(os.path.dirname(path), f.text.strip().replace("file://", ""))

        def material_image(mat_id):
            mat = by_id.get(mat_id)
            ie = mat.find(NS + "instance_effect") if mat is not None else None
            eff = by_id.get(ie.get("url", "#")[1:]) if ie is not None else None
            if eff is None:
                return None
            params = {p.get("sid"): p for p in eff.iter(NS + "newparam")}
            tex = eff.find(".//" + NS + "diffuse/" + NS + "texture")
            if tex is None:
                return None
            ref = tex.get("texture")
            for _ in range(3):
                p = params.get(ref)
                if p is None:
                    break
                s = p.find(".//" + NS + "source")
                f = p.find(".//" + NS + "init_from")
                ii = p.find(".//" + NS + "instance_image")   # 1.5: <sampler2D><instance_image url="#image"/>
                if ii is not None:
                    ref = ii.get("url", "#")[1:]
                    break
                ref = (s.text if s is not None else f.text if f is not None else ref).strip()
            return images.get(ref)

        self.tris = []            # [(image path, [corner x3])]; corner = (pos, normal, uv, {joint: w})
        for W_node, ic in inst:
            ctrl = by_id.get(ic.get("url", "#")[1:])
            skin = ctrl.find(NS + "skin") if ctrl is not None else None
            if skin is None:
                continue
            geo = by_id.get(skin.get("source", "#")[1:])
            bsm = _mat16(_floats(skin.find(NS + "bind_shape_matrix"))) if skin.find(NS + "bind_shape_matrix") is not None else eye()
            srcs = {s.get("id"): s for s in skin.findall(NS + "source")}
            jin = {i.get("semantic"): i.get("source")[1:] for i in skin.find(NS + "joints").findall(NS + "input")}
            jnames = (srcs[jin["JOINT"]].find(NS + "Name_array").text or "").split()
            ibm = _floats(srcs[jin["INV_BIND_MATRIX"]].find(NS + "float_array"))
            vw = skin.find(NS + "vertex_weights")
            winp = {i.get("semantic"): (int(i.get("offset")), i.get("source")[1:]) for i in vw.findall(NS + "input")}
            wvals = _floats(srcs[winp["WEIGHT"][1]].find(NS + "float_array"))
            vcount = [int(x) for x in vw.find(NS + "vcount").text.split()]
            v = [int(x) for x in vw.find(NS + "v").text.split()]
            stride = max(o for o, _s in winp.values()) + 1
            jmap = []
            for nm in jnames:
                if nm not in jidx:
                    raise RetargetError("%s: skin joint %s is not in the scene" % (path, nm))
                jmap.append(jidx[nm])
            # skin matrix of each skin joint at the scene pose: world . invBind . bindShape
            SM = [mul(mul(self.joints[jmap[q]]["world"], _mat16(ibm[16 * q:16 * q + 16])), bsm) for q in range(len(jnames))]
            weights = []
            k = 0
            for c in vcount:
                acc = {}
                for _ in range(c):
                    ji, wi = v[k + winp["JOINT"][0]], v[k + winp["WEIGHT"][0]]
                    k += stride
                    if ji >= 0:
                        acc[ji] = acc.get(ji, 0.0) + wvals[wi]
                weights.append(acc)
            mesh = geo.find(NS + "mesh")
            msrc = {s.get("id"): s for s in mesh.findall(NS + "source")}
            vert = mesh.find(NS + "vertices")
            vsrc = {i.get("semantic"): i.get("source")[1:] for i in vert.findall(NS + "input")}

            def vec(sid, n):
                a = _floats(msrc[sid].find(NS + "float_array"))
                acc = msrc[sid].find(".//" + NS + "accessor")
                st = int(acc.get("stride", n)) if acc is not None else n
                return [a[i:i + n] for i in range(0, len(a) - n + 1, st)]
            P = vec(vsrc["POSITION"], 3)
            NV = vec(vsrc["NORMAL"], 3) if "NORMAL" in vsrc else None
            TV = vec(vsrc["TEXCOORD"], 2) if "TEXCOORD" in vsrc else None   # UV por vertice (<vertices>), sem indice proprio
            bmat = {im.get("symbol"): im.get("target", "#")[1:] for im in ic.iter(NS + "instance_material")}
            for prims in list(mesh.findall(NS + "triangles")) + list(mesh.findall(NS + "polylist")):
                inputs = {}
                for i in prims.findall(NS + "input"):
                    inputs.setdefault(i.get("semantic"), (int(i.get("offset")), i.get("source")[1:]))
                st = max(o for o, _s in inputs.values()) + 1
                p = [int(x) for x in prims.find(NS + "p").text.split()]
                N = vec(inputs["NORMAL"][1], 3) if "NORMAL" in inputs else None
                T = vec(inputs["TEXCOORD"][1], 2) if "TEXCOORD" in inputs else None
                img = material_image(bmat.get(prims.get("material"), prims.get("material")))
                if prims.tag == NS + "polylist":
                    vc = [int(x) for x in prims.find(NS + "vcount").text.split()]
                else:
                    vc = [3] * (len(p) // (3 * st))
                cache = {}

                def corner(e):
                    key = tuple(p[e * st:(e + 1) * st])
                    if key in cache:
                        return cache[key]
                    vi = p[e * st + inputs["VERTEX"][0]]
                    acc = weights[vi]
                    tot = sum(acc.values())
                    if tot <= 0:
                        raise RetargetError("%s: vertex %d has no weights" % (path, vi))
                    pp = [0.0, 0.0, 0.0]
                    nn = [0.0, 0.0, 0.0]
                    if N is not None:
                        n0 = N[p[e * st + inputs["NORMAL"][0]]]
                    elif NV is not None:
                        n0 = NV[vi]
                    else:
                        n0 = [0.0, 0.0, 1.0]
                    for q, w in acc.items():
                        pp = [a + w / tot * b for a, b in zip(pp, apply(SM[q], P[vi]))]
                        nn = [a + w / tot * b for a, b in zip(nn, apply(SM[q], n0, 0.0))]
                    uv = T[p[e * st + inputs["TEXCOORD"][0]]] if T is not None else TV[vi] if TV is not None else [0.0, 0.0]
                    wj = {}
                    for q, w in acc.items():
                        wj[jmap[q]] = wj.get(jmap[q], 0.0) + w / tot
                    cache[key] = (pp, _unit(nn), [uv[0], 1.0 - uv[1]], wj)
                    return cache[key]
                e = 0
                for c in vc:
                    ring = [corner(e + i) for i in range(c)]
                    e += c
                    for i in range(1, c - 1):
                        self.tris.append((img, [ring[0], ring[i], ring[i + 1]]))
        if not self.tris:
            raise RetargetError("%s: no skinned triangles" % path)

        self.props = []           # [{"name", "joint", "tris": [(image path, [(pos, normal, uv) x3])]}], scene space
        for name, ji, W_node, ig in rigid:
            geo = by_id.get(ig.get("url", "#")[1:])
            mesh = geo.find(NS + "mesh") if geo is not None else None
            if mesh is None:
                continue
            msrc = {s.get("id"): s for s in mesh.findall(NS + "source")}
            vsrc = {i.get("semantic"): i.get("source")[1:] for i in mesh.find(NS + "vertices").findall(NS + "input")}

            def vec(sid, n):
                a = _floats(msrc[sid].find(NS + "float_array"))
                return [a[i:i + n] for i in range(0, len(a) - n + 1, n)]
            P = vec(vsrc["POSITION"], 3)
            NV = vec(vsrc["NORMAL"], 3) if "NORMAL" in vsrc else None
            TV = vec(vsrc["TEXCOORD"], 2) if "TEXCOORD" in vsrc else None
            bmat = {im.get("symbol"): im.get("target", "#")[1:] for im in ig.iter(NS + "instance_material")}
            tris = []
            for prims in mesh.findall(NS + "triangles"):
                inputs = {}
                for i in prims.findall(NS + "input"):
                    inputs.setdefault(i.get("semantic"), (int(i.get("offset")), i.get("source")[1:]))
                st = max(o for o, _s in inputs.values()) + 1
                p = [int(x) for x in prims.find(NS + "p").text.split()]
                N = vec(inputs["NORMAL"][1], 3) if "NORMAL" in inputs else None
                T = vec(inputs["TEXCOORD"][1], 2) if "TEXCOORD" in inputs else None
                img = material_image(bmat.get(prims.get("material"), prims.get("material")))
                cs = []
                for e in range(len(p) // st):
                    vi = p[e * st + inputs["VERTEX"][0]]
                    pp = apply(W_node, P[vi])
                    n0 = N[p[e * st + inputs["NORMAL"][0]]] if N is not None else NV[vi] if NV is not None else None
                    nn = _unit(apply(W_node, n0, 0.0)) if n0 is not None else [0.0, 0.0, 1.0]
                    uv = T[p[e * st + inputs["TEXCOORD"][0]]] if T is not None else TV[vi] if TV is not None else [0.0, 0.0]
                    cs.append((pp, nn, [uv[0], 1.0 - uv[1]]))
                tris += [(img, cs[k:k + 3]) for k in range(0, len(cs) - 2, 3)]
            if tris:
                self.props.append({"name": name, "joint": ji, "tris": tris})
        ua = root.find(NS + "asset/" + NS + "up_axis")
        ua = (ua.text or "").strip().upper() if ua is not None else ""
        if up is None:
            if ua == "X_UP":
                raise RetargetError("%s: X_UP scenes are not read (export it with Z or Y up)" % path)
            up = {"Y_UP": "y", "Z_UP": "z"}.get(ua) or guess_up(self)
            self.up_from = "the file's <up_axis>" if ua else "the head above the root (no <up_axis>)"
        else:
            self.up_from = "--up"
        self.up_axis = up
        self.up = YUP if up == "y" else eye()
        if up == "y":
            turn_model(self, YUP)


YUP = [[1.0, 0.0, 0.0, 0.0], [0.0, 0.0, -1.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 0.0, 1.0]]   # Y up -> Z up (+Z -> -Y)


def guess_up(mdl):
    """"y" or "z": the axis along which a joint named like a head sits furthest from the top joint; "z" when no head."""
    head = next((j for j in mdl.joints if "head" in (j["name"] or "").lower()), None)
    top = next((j for j in mdl.joints if j["parent"] is None), None)
    if head is None or top is None:
        return "z"
    d = [a - b for a, b in zip(pos(head["world"]), pos(top["world"]))]
    return "y" if abs(d[1]) > abs(d[2]) else "z"


def turn_model(mdl, U):
    """The whole model (joints, skinned triangles, props) turned by U, in its scene."""
    for jt in mdl.joints:
        jt["world"] = mul(U, jt["world"])
    mdl.tris = [(img, [(apply(U, p), _unit(apply(U, n, 0.0)), uv, wj) for p, n, uv, wj in cs]) for img, cs in mdl.tris]
    for pr in mdl.props:
        pr["tris"] = [(img, [(apply(U, p), _unit(apply(U, n, 0.0)), uv) for p, n, uv in cs]) for img, cs in pr["tris"]]


class GlbModel:
    """The same as Model from a glTF (.glb or .gltf): the joints of its skins, the skinned triangles at rest, rigid meshes
    under a joint as props. glTF is Y up and faces +Z, so the model faces -Y once turned to Z up."""

    def __init__(self, path, up=None):
        self.path = path
        ed = ngpanim.Edited(_gltf_as_glb(path) if path.lower().endswith(".gltf") else path)
        glb = ed.glb
        nodes = glb.nodes
        skins = glb.j.get("skins", [])
        if not skins:
            raise RetargetError("%s: no skin (a mesh on an armature)" % path)
        jset = []
        for s in skins:
            for k in s.get("joints", []):
                if k not in jset:
                    jset.append(k)
        parent = ed.parent

        def joint_above(i):
            p = parent.get(i)
            while p is not None and p not in jset:
                p = parent.get(p)
            return p

        def depth(i):
            d = 0
            while i in parent:
                i, d = parent[i], d + 1
            return d
        order = sorted(jset, key=lambda k: (depth(k), k))
        U = eye() if up == "z" else YUP
        self.up_axis = up or "y"
        self.up = U
        self.up_from = "--up" if up else "glTF (Y up)"
        cache = {}
        world = {i: mul(U, ed.world(i, None, 0.0, cache)) for i in range(len(nodes))}
        self.joints = []
        jix = {}
        seen = set()
        for k in order:
            nm = nodes[k].get("name") or "joint%d" % k
            if nm in seen:
                nm = "%s_%d" % (nm, k)
            seen.add(nm)
            jix[k] = len(self.joints)
            self.joints.append({"name": nm, "parent": None, "world": world[k]})
        for k in order:
            p = joint_above(k)
            self.joints[jix[k]]["parent"] = jix.get(p)
        self.names = {j["name"]: i for i, j in enumerate(self.joints)}
        self.images = {}
        mats = glb.j.get("materials", [])

        def image_key(prim):
            mi = prim.get("material")
            if mi is None or mi >= len(mats):
                return None
            t = mats[mi].get("pbrMetallicRoughness", {}).get("baseColorTexture")
            if t is None:
                return None
            src = glb.j.get("textures", [])[t["index"]].get("source")
            if src is None:
                return None
            key = "glb:%d" % src
            if key not in self.images:
                im = glb.j["images"][src]
                data = None
                if "bufferView" in im:
                    v = glb.j["bufferViews"][im["bufferView"]]
                    data = glb.bin[v.get("byteOffset", 0):v.get("byteOffset", 0) + v["byteLength"]]
                elif im.get("uri", "").startswith("data:"):
                    import base64
                    data = base64.b64decode(im["uri"].split(",", 1)[1])
                elif im.get("uri"):
                    f = os.path.join(os.path.dirname(path), im["uri"].replace("%20", " "))
                    data = open(f, "rb").read() if os.path.isfile(f) else None
                self.images[key] = (im.get("name") or os.path.splitext(os.path.basename(im.get("uri", "")))[0]
                                    or "image%d" % src, data)
            return key

        self.tris = []
        self.props = []
        for ni, nd in enumerate(nodes):
            if "mesh" not in nd:
                continue
            skin = skins[nd["skin"]] if "skin" in nd else None
            if skin is None:
                h = joint_above(ni) if ni not in jset else ni
                if h is None:
                    continue
                Wn = world[ni]
            else:
                sj = skin["joints"]
                ibm = glb.accessor(skin["inverseBindMatrices"]) if "inverseBindMatrices" in skin else [None] * len(sj)
                SM = [mul(world[k], ngpmesh._col(ibm[q]) if ibm[q] else eye()) for q, k in enumerate(sj)]
            ptris = []
            for prim in glb.j["meshes"][nd["mesh"]]["primitives"]:
                if prim.get("mode", 4) != 4:
                    raise RetargetError("%s: mesh %s is not triangles" % (path, nd.get("name")))
                at = prim["attributes"]
                P = glb.accessor(at["POSITION"])
                N = glb.accessor(at["NORMAL"]) if "NORMAL" in at else None
                T = glb.accessor(at["TEXCOORD_0"]) if "TEXCOORD_0" in at else None
                idx = [i[0] for i in glb.accessor(prim["indices"])] if "indices" in prim else list(range(len(P)))
                img = image_key(prim)
                sets = [(glb.accessor(at["JOINTS_%d" % s]), glb.accessor(at["WEIGHTS_%d" % s]))
                        for s in range(4) if skin is not None and "JOINTS_%d" % s in at and "WEIGHTS_%d" % s in at]
                if skin is not None and not sets:
                    raise RetargetError("%s: mesh %s has a skin but no weights" % (path, nd.get("name")))
                cache_c = {}

                def corner(v):
                    if v in cache_c:
                        return cache_c[v]
                    n0 = list(N[v]) if N else [0.0, 0.0, 1.0]
                    uv = list(T[v]) if T else [0.0, 0.0]
                    if skin is None:
                        c = (apply(Wn, P[v]), _unit(apply(Wn, n0, 0.0)), uv)
                    else:
                        acc = {}
                        for js, ws in sets:
                            for q, w in zip(js[v], ws[v]):
                                if w > 0.0 and q < len(sj):
                                    acc[q] = acc.get(q, 0.0) + w
                        tot = sum(acc.values())
                        if tot <= 0:
                            raise RetargetError("%s: mesh %s, vertex %d has no weights" % (path, nd.get("name"), v))
                        pp = [0.0, 0.0, 0.0]
                        nn = [0.0, 0.0, 0.0]
                        wj = {}
                        for q, w in acc.items():
                            pp = [a + w / tot * b for a, b in zip(pp, apply(SM[q], P[v]))]
                            nn = [a + w / tot * b for a, b in zip(nn, apply(SM[q], n0, 0.0))]
                            wj[jix[sj[q]]] = wj.get(jix[sj[q]], 0.0) + w / tot
                        c = (pp, _unit(nn), uv, wj)
                    cache_c[v] = c
                    return c
                for k in range(0, len(idx) - 2, 3):
                    t = (img, [corner(idx[k]), corner(idx[k + 1]), corner(idx[k + 2])])
                    (self.tris if skin is not None else ptris).append(t)
            if skin is None and ptris:
                self.props.append({"name": nd.get("name") or "mesh%d" % ni, "joint": jix[h], "tris": ptris})
        if not self.tris:
            raise RetargetError("%s: no skinned triangles" % path)


def _gltf_as_glb(path):
    """A .gltf (JSON + .bin next to it) as a temporary .glb that ngpmesh.Glb reads."""
    import tempfile
    j = json.load(open(path, encoding="utf-8"))
    bufs = j.get("buffers", [])
    if len(bufs) > 1:
        raise RetargetError("%s: more than one buffer (export it as .glb)" % path)
    data = b""
    if bufs:
        u = bufs[0].get("uri", "")
        if u.startswith("data:"):
            import base64
            data = base64.b64decode(u.split(",", 1)[1])
        else:
            data = open(os.path.join(os.path.dirname(path), u.replace("%20", " ")), "rb").read()
        bufs[0].pop("uri", None)
    for im in j.get("images", []):   # images next to the .gltf: the path made absolute, so the temporary .glb finds them
        u = im.get("uri", "")
        if u and not u.startswith("data:"):
            im["uri"] = os.path.abspath(os.path.join(os.path.dirname(path), u.replace("%20", " ")))
    js = json.dumps(j).encode("utf-8")
    js += b" " * (-len(js) % 4)
    data += b"\0" * (-len(data) % 4)
    out = tempfile.NamedTemporaryFile(suffix=".glb", delete=False)
    out.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(data)))
    out.write(struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(data), 0x004E4942) + data)
    out.close()
    return out.name


def load_model(path, up=None):
    """Model (.dae) or GlbModel (.glb / .gltf) by the extension."""
    if path.lower().endswith((".glb", ".gltf")):
        return GlbModel(path, up)
    return Model(path, up)


# ---------------------------------------------------------------- the source (kit .glb)

class Source:
    def __init__(self, path):
        self.path = path
        self.ed = ngpanim.Edited(path)
        nodes = self.ed.nodes
        r = self.ed.names.get("WotM")
        if r is None:
            raise RetargetError("%s: not a kit export (no WotM root)" % path)
        self.root_inv = inv(self.ed.world(r, None, 0.0, {}))
        self.names = self.ed.names
        self.rest = {}
        cache = {}
        for i in range(len(nodes)):
            self.rest[i] = mul(self.root_inv, self.ed.world(i, None, 0.0, cache))
        # height of the rest mesh, as ngprig measures the base (z extent of the skinned meshes)
        zs = []
        glb = self.ed.glb
        for nd in nodes:
            if "mesh" not in nd or "skin" not in nd:
                continue
            sk = glb.j["skins"][nd["skin"]]
            ibm = glb.accessor(sk["inverseBindMatrices"])
            M = [mul(self.root_inv, mul(self.ed.world(k, None, 0.0, cache), [[m[c * 4 + rr] for c in range(4)] for rr in range(4)]))
                 for k, m in zip(sk["joints"], ibm)]
            for prim in glb.j["meshes"][nd["mesh"]]["primitives"]:
                at = prim["attributes"]
                P = glb.accessor(at["POSITION"])
                J0 = glb.accessor(at["JOINTS_0"])
                W0 = glb.accessor(at["WEIGHTS_0"])
                for p, js, ws in zip(P, J0, W0):
                    q = max(range(len(ws)), key=lambda i: ws[i])
                    zs.append(apply(M[js[q]], p)[2])
        self.height = (max(zs) - max(0.0, min(zs))) if zs else None
        self.clips = []
        for nm, clip in self.ed.clips.items():
            ts = self.ed.key_times(clip, range(len(nodes)))
            frames = int(round(ts[-1] * FPS)) + 1 if ts else 1
            self.clips.append((nm, clip, max(frames, 2)))

    def node(self, name):
        if name not in self.names:
            raise RetargetError("source bone %s is not in the source .glb" % name)
        return self.names[name]

    def world(self, i, clip, t, cache):
        return mul(self.root_inv, self.ed.world(i, clip, t, cache))

    def layer_poses(self, base_ngp):
        """{clip name: {node: world}} for each layer of the base .NGP the export came from (tools/ngprig.py layer_anims): the pose
        the layer is added to, with its channels at 0. The export shows a layer as that pose plus the layer, the channels it
        leaves alone at the base's stored pose (atMat), which is not always the bind pose (Congar's feet, ~30 degrees off);
        measured from the bind pose, a layer would carry that difference and the game would add it on top of every move."""
        import bonelabels
        import ngprig
        g = ngpanim.Game(base_ngp)
        node_of = {}
        for i, nd in enumerate(self.ed.nodes):
            n = g.by_name.get(bonelabels.canonical(nd.get("name") or ""))
            if n is not None:
                node_of[i] = n
        anims = g.anims()
        ids = [a[0] for a in anims]
        layer = ngprig.layer_anims(g, base_ngp)
        out = {}
        for aid, k, _st, _et, _n, an in anims:
            if an not in layer:
                continue
            ctxs = g.contexts()
            c = ctxs[g.main]
            for _a, cv in g.curves(an):
                if 0 <= cv.idx < c.nch:
                    c.val[cv.idx] = 0.0
                    c.dirty.add(cv.idx)
            w = g.walk(ctxs)
            nm = "a%03d" % aid + ("_%d" % k if ids.count(aid) > 1 else "")   # the export's name (tools/monglb.py)
            out[nm] = {i: ngpanim.tr(w[n][1]) for i, n in node_of.items() if n in w}
        return out


# ---------------------------------------------------------------- the retarget

def turn_matrix(spec):
    """"x90,z-90": quarter turns about the game's axes (x right, y front, z up), one after the other -> 4x4."""
    M = eye()
    for step in [s for s in re.split(r"[,\s]+", spec.strip()) if s]:
        m = re.fullmatch(r"([xyz])([+-]?\d+)", step.lower())
        if not m or int(m.group(2)) % 90:
            raise RetargetError("turn %r: steps like x90, z-90, y180" % step)
        a = int(m.group(2)) // 90 % 4
        c, s = [1, 0, -1, 0][a], [0, 1, 0, -1][a]
        R = {"x": [[1, 0, 0], [0, c, -s], [0, s, c]], "y": [[c, 0, s], [0, 1, 0], [-s, 0, c]],
             "z": [[c, -s, 0], [s, c, 0], [0, 0, 1]]}[m.group(1)]
        M = mul([[float(v) for v in r] + [0.0] for r in R] + [[0.0, 0.0, 0.0, 1.0]], M)
    return M


def build(src_path, model_path, map_path, out_path, size=1.0, height=None, front=None, clips=None, own=None, log=print,
          up=None, turn=None, rest=None, base=None):
    """map_path: a map file, "auto" or the map itself (a dict). rest: a dict to fill with the posed rest instead of writing
    out_path (the page's preview, tools/orientpage.py): "joints" [{"name", "parent", "pos", "kept"}], "tris" at the posed rest
    in the game's axes and units, "cfg" the map. base: the .NGP src was exported from (its layers, Source.layer_poses)."""
    src = src_path if isinstance(src_path, Source) else Source(src_path)        # loaded ones too (the page's previews)
    mdl = model_path if not isinstance(model_path, str) else load_model(model_path, up)
    src_path, model_path = src.path, mdl.path
    if front is None:   # a Y-up scene faces +Z, which is -Y once it stands on Z
        front = "-y" if mdl.up_axis == "y" else "+y"
    log("model %s: %s up (%s), facing %s" % (os.path.basename(model_path), mdl.up_axis.upper(), mdl.up_from, front))
    if map_path == "auto":   # made from the two skeletons (tools/automap.py) and kept next to the output, to edit
        import automap
        try:
            cfg = automap.build_map(src_path, mdl, log)
        except automap.MapError as e:
            raise RetargetError("bone map: %s" % e)
        mp = os.path.splitext(out_path)[0] + "_map.json"
        with open(mp, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=1)
        log("bone map written to %s" % mp)
    elif isinstance(map_path, dict):
        cfg = map_path
    else:
        cfg = load_map(map_path)
    J = mdl.joints
    nj = len(J)

    def jid(name):
        if name not in mdl.names:
            raise RetargetError("map: joint %s is not in the model" % name)
        return mdl.names[name]

    # ---- axes and size: model -> game (Z up, facing +Y)
    Cm = eye() if front == "+y" else [[-1.0, 0, 0, 0], [0, -1.0, 0, 0], [0, 0, 1.0, 0], [0, 0, 0, 1.0]]
    if turn:
        Cm = mul(turn_matrix(turn), Cm)
        log("turned %s about the game's axes" % turn)
    zs = [apply(Cm, c[0])[2] for _i, t in mdl.tris for c in t]
    h0 = max(zs) - min(zs)
    if height is None:
        if src.height is None:
            raise RetargetError("the source has no skinned mesh to measure; give --height")
        height = src.height * size
    s = height / h0
    lift = -min(zs) * s

    def to_game_p(p):
        q = apply(Cm, p)
        return [q[0] * s, q[1] * s, q[2] * s + lift]
    G0 = [with_pos(rot3(mul(Cm, j["world"])), to_game_p(pos(j["world"]))) for j in J]
    log("model %s: %d joints, %d triangles, height %.1f -> %.1f (scale %.4g)" % (
        os.path.basename(model_path), nj, len(mdl.tris), h0, height, s))

    # ---- the map
    bones = {}
    rest_turn = {}   # joints only turned at rest ("turn" without "from"): rigid on the joint above
    for name, spec in cfg.get("bones", {}).items():
        if "from" not in spec:
            if spec.get("turn"):
                rest_turn[jid(name)] = spec["turn"]
            continue
        f = spec["from"]
        if isinstance(f, str):
            bones[jid(name)] = {"a": src.node(f), "b": None, "f": 0.0, "move": bool(spec.get("move")), "align": spec.get("align"),
                                "level": bool(spec.get("level")), "turn": spec.get("turn")}
        else:
            bones[jid(name)] = {"a": src.node(f[0]), "b": src.node(f[1]), "f": float(f[2]), "move": bool(spec.get("move")),
                                "align": spec.get("align"), "level": bool(spec.get("level")), "turn": spec.get("turn")}
    clip_bones = {}   # animation id -> {joint: (a, b, f, aim)}: "clipBones", the joint turns otherwise in those clips
    for cb in cfg.get("clipBones", []):
        over = {}
        for name, spec in cb.get("bones", {}).items():
            j = jid(name)
            if j not in bones:
                raise RetargetError("map: clipBones joint %s has no \"from\" in \"bones\"" % name)
            f = spec.get("from")
            if f is None:
                a, b, w = bones[j]["a"], bones[j]["b"], bones[j]["f"]
            elif isinstance(f, str):
                a, b, w = src.node(f), None, 0.0
            else:
                a, b, w = src.node(f[0]), src.node(f[1]), float(f[2])
            aim = None
            if spec.get("aim"):
                cj, cbone = jid(spec["aim"][0]), src.node(spec["aim"][1])
                aim = (cj, bones[j]["a"], cbone)
            over[j] = (a, b, w, aim)
        for aid in cb.get("clips", []):
            clip_bones.setdefault(int(aid), {}).update(over)
    src_of = {}   # source bone -> model joint (for the locators)
    for j, b in bones.items():
        if b["b"] is None:
            src_of.setdefault(b["a"], j)
    for ch in cfg.get("chains", []):
        tj = [jid(n) for n in ch["joints"]]
        sb = [src.node(n) for n in ch["from"]]
        for i, j in enumerate(tj):
            u = i * (len(sb) - 1) / float(max(1, len(tj) - 1))
            a = int(math.floor(u))
            b = min(a + 1, len(sb) - 1)
            bones[j] = {"a": sb[a], "b": sb[b], "f": u - a, "move": False, "align": None}
        for i, b in enumerate(sb):
            src_of.setdefault(b, tj[int(round(i * (len(tj) - 1) / float(max(1, len(sb) - 1))))])

    # ---- the model posed like the source (align), top-down over every joint
    kids = {}
    for i, j in enumerate(J):
        kids.setdefault(j["parent"], []).append(i)
    order = []

    def dfs(i):
        order.append(i)
        for c in kids.get(i, []):
            dfs(c)
    for r in kids.get(None, []):
        dfs(r)
    L0 = [mul(inv(G0[j["parent"]]), G0[i]) if j["parent"] is not None else G0[i] for i, j in enumerate(J)]
    A = [None] * nj

    def axis_turn(deg):
        """Turns about the game's x, y and z (degrees, in that order): +x lifts what faces forward."""
        Q = eye()
        for ax, d in enumerate(deg):
            c, s_ = math.cos(math.radians(d)), math.sin(math.radians(d))
            a, b = [k for k in range(3) if k != ax]
            if ax == 1:
                a, b = b, a
            R = eye()
            R[a][a], R[a][b], R[b][a], R[b][b] = c, -s_, s_, c
            Q = mul(R, Q)
        return Q

    def level_turn(i, Ai):
        """The turn that lays the sole of joint i flat: the plane through the underside of the vertices it carries (with
        its unmapped joints below, toes included), as they sit with the joint at Ai."""
        sub, stack = set(), [i]
        while stack:
            k = stack.pop()
            sub.add(k)
            stack += [c for c in kids.get(k, []) if c not in bones]
        K = mul(Ai, inv(G0[i]))
        pts = {}
        for _img, tri in mdl.tris:
            for p, _n, _uv, wj in tri:
                if sum(w for j, w in wj.items() if j in sub) > 0.5:
                    q = apply(K, to_game_p(p))
                    pts[tuple(round(c, 3) for c in q)] = q
        pts = list(pts.values())
        if len(pts) >= 3:   # the underside: the lowest point of each cell of an 8x8 grid over the footprint
            lo = [min(q[c] for q in pts) for c in (0, 1)]
            span = [max(max(q[c] for q in pts) - lo[c], 1e-6) for c in (0, 1)]
            cell = {}
            for q in pts:
                k = tuple(min(7, int(8 * (q[c] - lo[c]) / span[c])) for c in (0, 1))
                if k not in cell or q[2] < cell[k][2]:
                    cell[k] = q
            pts = list(cell.values())
        if len(pts) < 3:
            raise RetargetError("map: %s carries no mesh to level" % J[i]["name"])
        # z = a x + b y + c, least squares
        sx = sy = sz = sxx = sxy = syy = sxz = syz = 0.0
        for x, y, z in pts:
            sx, sy, sz = sx + x, sy + y, sz + z
            sxx, sxy, syy = sxx + x * x, sxy + x * y, syy + y * y
            sxz, syz = sxz + x * z, syz + y * z
        n = float(len(pts))
        M = [[sxx, sxy, sx], [sxy, syy, sy], [sx, sy, n]]
        r = [sxz, syz, sz]

        def det(m):
            return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                    + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
        d = det(M)
        if abs(d) < 1e-9:
            return eye()
        a, b = [det([[r[q] if c == col else M[q][c] for c in range(3)] for q in range(3)]) / d for col in (0, 1)]
        nrm = _unit([-a, -b, 1.0])
        log("level %s: sole tilted %.1f degrees, laid flat" % (J[i]["name"], math.degrees(math.acos(max(-1.0, min(1.0, nrm[2]))))))
        return rot_between(nrm, [0.0, 0.0, 1.0])
    for i in order:
        p = J[i]["parent"]
        A[i] = mul(A[p], L0[i]) if p is not None else G0[i]
        al = bones.get(i, {}).get("align")
        if al:
            c = jid(al[0])
            chain = [c]
            while J[chain[-1]]["parent"] != i:
                chain.append(J[chain[-1]]["parent"])
                if chain[-1] is None:
                    raise RetargetError("map: %s is not below %s" % (al[0], J[i]["name"]))
            Wc = A[i]
            for k in reversed(chain):
                Wc = mul(Wc, L0[k])
            dg = [a - b for a, b in zip(pos(Wc), pos(A[i]))]
            sa = bones[i]["a"]
            ds = [a - b for a, b in zip(pos(src.rest[src.node(al[1])]), pos(src.rest[sa]))]
            Q = rot_between(dg, ds)
            A[i] = with_pos(mul(Q, rot3(A[i])), pos(A[i]))
        if bones.get(i, {}).get("level"):
            A[i] = with_pos(mul(level_turn(i, A[i]), rot3(A[i])), pos(A[i]))
        if bones.get(i, {}).get("turn") or i in rest_turn:
            A[i] = with_pos(mul(axis_turn(bones[i]["turn"] if i in bones else rest_turn[i]), rot3(A[i])), pos(A[i]))
    # skin to the posed rest
    Kmat = [mul(A[i], inv(G0[i])) for i in range(nj)]

    # ---- joints kept: mapped, "keep", locator holders; the rest is rigid on the nearest kept ancestor
    keep = set(bones) | {jid(n) for n in cfg.get("keep", [])}
    loc_cfg = {}
    for name, spec in cfg.get("locators", {}).items():
        loc_cfg[name] = spec
        keep.add(jid(spec["joint"]))

    def kept_above(i):
        p = J[i]["parent"]
        while p is not None and p not in keep:
            p = J[p]["parent"]
        return p
    kparent = {i: kept_above(i) for i in keep}
    tops = [i for i in keep if kparent[i] is None]
    if len(tops) != 1:
        raise RetargetError("map: the kept joints have %d roots (%s); map the pelvis/root" % (
            len(tops), ", ".join(J[i]["name"] for i in tops)))
    top = tops[0]
    korder = [i for i in order if i in keep]

    def holder(i):
        k = i
        while k is not None and k not in keep:
            k = J[k]["parent"]
        return top if k is None else k

    # ---- triangles at the posed rest, weights on kept joints
    out_tris = []
    for img, tri in mdl.tris:
        cs = []
        for p, n, uv, wj in tri:
            pg = to_game_p(p)
            ng = _unit(apply(Cm, n, 0.0))
            pp = [0.0, 0.0, 0.0]
            nn = [0.0, 0.0, 0.0]
            acc = {}
            for j, w in wj.items():
                pp = [a + w * b for a, b in zip(pp, apply(Kmat[j], pg))]
                nn = [a + w * b for a, b in zip(nn, apply(Kmat[j], ng, 0.0))]
                hj = holder(j)
                acc[hj] = acc.get(hj, 0.0) + w
            ws = sorted(acc.items(), key=lambda kv: -kv[1])[:4]
            t = sum(w for _j, w in ws)
            cs.append((pp, _unit(nn), uv, [(j, w / t) for j, w in ws]))
        out_tris.append((img, cs))
    if rest is not None:
        rest.update({"cfg": cfg, "tris": out_tris, "joints": [{"name": j["name"], "parent": j["parent"], "pos": pos(A[i]),
                                                               "kept": i in keep} for i, j in enumerate(J)]})
        return rest

    # ---- locators
    k_size = height / src.height if src.height else s
    locs = {}   # glTF name -> (joint, game point)

    def mapped_above(n):
        i = n
        while i is not None:
            if i in src_of:
                return i
            i = src.ed.parent.get(i)
        return None
    for i, nd in enumerate(src.ed.nodes):
        nm = nd.get("name") or ""
        if not re.fullmatch(r"loc_[0-9a-f]{3,4}", nm):
            continue
        b = mapped_above(i)
        if b is None:
            log("warning: source locator %s has no mapped bone above it; left to ngprig" % nm)
            continue
        j = src_of[b]
        off = [a - c for a, c in zip(pos(src.rest[i]), pos(src.rest[b]))]
        locs[nm] = (j, [a + k_size * c for a, c in zip(pos(A[j]), off)])
    for name, spec in loc_cfg.items():
        lid = ngprig_locator_id(name)
        if lid is None:
            raise RetargetError("map: locator %s is not an alias nor loc_<hex>" % name)
        at = spec["at"]
        if at and isinstance(at[0], str):
            pts = [pos(A[jid(n)]) for n in at]
            pt = [sum(c) / len(pts) for c in zip(*pts)]
        else:
            g = to_game_p(at)
            j0 = holder(jid(spec["joint"]))
            pt = apply(Kmat[j0], g)
        locs["loc_%03x" % lid] = (jid(spec["joint"]), pt)

    # ---- rigid props: in the space of the kept joint above them; seen as much as the source node "show" is (its scale)
    pcfg = cfg.get("props", {})
    extra = sorted(set(pcfg) - {pr["name"] for pr in mdl.props})
    if extra:
        raise RetargetError("map: props not in the model (a rigid mesh on a joint): %s" % ", ".join(extra))
    props = []
    for pr in mdl.props:
        h = holder(pr["joint"])
        Gi = inv(G0[h])
        tl = [(img, [(apply(Gi, to_game_p(p)), _unit(apply(Gi, apply(Cm, n, 0.0), 0.0)), uv) for p, n, uv in cs])
              for img, cs in pr["tris"]]
        show = pcfg.get(pr["name"], {}).get("show")
        ids = set(int(x) for x in show) if isinstance(show, list) else None
        props.append({"name": pr["name"], "joint": h, "tris": tl, "show": src.node(show) if show and ids is None else None,
                      "ids": ids})
        log("prop %s on %s%s" % (pr["name"], J[h]["name"], ", seen with %s" % show if show else ""))

    def prop_scale(pi, clip, t, cache, aid=None):
        if props[pi]["ids"] is not None:
            return 1.0 if aid in props[pi]["ids"] else 0.0
        b = props[pi]["show"]
        if b is None:
            return 1.0
        if clip is None:   # an own clip with a new id: the source has no such move
            return 0.0
        W = src.world(b, clip, t, cache)
        return sum(math.sqrt(sum(W[r][c] ** 2 for r in range(3))) for c in range(3)) / 3.0
    src_clip = {}
    for nm, clip, frames in src.clips:
        m = re.match(r"a(\d+)", nm)
        if m:
            src_clip.setdefault(int(m.group(1)), (clip, frames))

    # ---- motion
    rest_src = {}
    for b in {x for bb in bones.values() for x in (bb["a"], bb["b"]) if x is not None} | \
            {x for over in clip_bones.values() for t in over.values() for x in t[:2] + (t[3][1:] if t[3] else ())
             if x is not None}:
        rest_src[b] = src.rest[b]
    move_k = {}
    for j, b in bones.items():
        if b["move"]:
            zs_ = pos(src.rest[b["a"]])[2]
            move_k[j] = pos(A[j])[2] / zs_ if abs(zs_) > 1e-6 else s
    Lk = {}
    for i in korder:
        p = kparent[i]
        Lk[i] = mul(inv(A[p]), A[i]) if p is not None else A[i]
    for over in clip_bones.values():
        for j, t in over.items():
            if t[3] and kparent.get(t[3][0]) != j:
                raise RetargetError("map: clipBones %s aims at %s, which is not a kept child of it" % (
                    J[j]["name"], J[t[3][0]]["name"]))
    layer_pose = {}
    if base:   # the layers turn from the pose they are added to, not from the bind pose
        try:
            layer_pose = src.layer_poses(base)
        except (OSError, ValueError, ngpanim.AnimError) as e:
            raise RetargetError("base %s: %s" % (os.path.basename(base), e))
        log("%d layers measured from the pose the game adds them to (%s)" % (len(layer_pose), os.path.basename(base)))
    want = want_names = None
    if isinstance(clips, (list, tuple, set)):   # clip names ("a001", "a000_26"): the page's animation preview
        want_names = set(clips)
    elif clips:
        want = set(int(x) for x in clips.split(","))
    anims = []
    for nm, clip, frames in src.clips:
        m = re.match(r"a(\d+)", nm)
        if want is not None and (not m or int(m.group(1)) not in want):
            continue
        if want_names is not None and nm not in want_names:
            continue
        tracks = {i: ([], []) for i in korder}   # joint -> (quats, positions) local
        pscale = {}                              # prop -> scale per frame
        lp = layer_pose.get(nm)
        ref = {b: lp.get(b, rest_src[b]) if lp else rest_src[b] for b in rest_src}   # what the source bone turns from
        over = clip_bones.get(int(m.group(1)), {}) if m else {}
        for f in range(frames):
            t = f / FPS
            cache = {}
            D = {}
            for b in rest_src:
                W = src.world(b, clip, t, cache)
                D[b] = (quat_of(mul(rot3(W), tmat(rot3(ref[b])))), pos(W))
            Wt = {}
            for i in korder:
                p = kparent[i]
                bb = bones.get(i)
                if bb is not None:
                    ra, rb, rf, aim = over[i] if i in over else (bb["a"], bb["b"], bb["f"], None)
                    q = D[ra][0] if rb is None else slerp(D[ra][0], D[rb][0], rf)
                    R = mul(quat_col(q), rot3(A[i]))
                    if aim:   # the segment to the child joint along the source's bone -> child bone, at every frame
                        cj, sa, sb = aim
                        R = mul(rot_between(apply(R, pos(Lk[cj]), 0.0), [x - y for x, y in zip(D[sb][1], D[sa][1])]), R)
                else:
                    R = rot3(mul(Wt[p], Lk[i])) if p is not None else rot3(A[i])
                if i in move_k:
                    d = [a - c for a, c in zip(D[bb["a"]][1], pos(ref[bb["a"]]))]
                    P = [a + move_k[i] * c for a, c in zip(pos(A[i]), d)]
                elif p is not None:
                    P = apply(Wt[p], pos(Lk[i]))
                else:
                    P = pos(A[i])
                Wt[i] = with_pos(R, P)
                L = mul(inv(Wt[p]), Wt[i]) if p is not None else Wt[i]
                tracks[i][0].append(quat_of(L))
                tracks[i][1].append(pos(L))
            for pi in range(len(props)):
                pscale.setdefault(pi, []).append(prop_scale(pi, clip, t, cache, int(m.group(1)) if m else None))
        anims.append((nm, frames, tracks, pscale))
    log("%d actions retargeted, %d joints kept (%s), %d locators" % (
        len(anims), len(korder), ", ".join(J[i]["name"] for i in korder), len(locs)))

    # ---- the model's own clips: each joint's world in the model's scene, per frame
    if own:
        oc = json.load(open(own, encoding="utf-8"))
        done = []
        for clip in oc.get("clips", []):
            nm, frames, jd = clip["name"], int(clip["frames"]), clip["joints"]
            if not re.fullmatch(r"a\d+(_loop|_once)?", nm):
                raise RetargetError("own clip %s: the name is a<id> (a090), with _loop or _once for a new id" % nm)
            miss = sorted(n for n in jd if n not in mdl.names)
            if miss:
                raise RetargetError("own clip %s: joints not in the model: %s" % (nm, ", ".join(miss)))
            for n, ks in jd.items():
                if len(ks) != frames:
                    raise RetargetError("own clip %s: joint %s has %d frames, not %d" % (nm, n, len(ks), frames))
            given = {mdl.names[n]: ks for n, ks in jd.items()}
            aid = int(re.match(r"a(\d+)", nm).group(1))
            sc = src_clip.get(aid)   # the props follow the source's clip with the same id, over the same share of time
            lay = clip.get("laid")
            U = inv(with_pos(axis_turn(lay.get("turn", [0, 0, 0])), [float(x) for x in lay.get("shift", [0, 0, 0])])) \
                if lay else None
            tracks = {i: ([], []) for i in korder}
            pscale = {}
            for f in range(frames):
                cache = {}
                for pi in range(len(props)):
                    t = f / float(max(1, frames - 1)) * (sc[1] - 1) / FPS if sc else 0.0
                    pscale.setdefault(pi, []).append(prop_scale(pi, sc[0] if sc else None, t, cache, aid))
                Wt = {}
                for i in korder:
                    p = kparent[i]
                    k = given.get(i)
                    if k is not None:
                        Wd = mul(mdl.up, with_pos(quat_col(k[f][:4]), k[f][4:7]))
                        Wt[i] = with_pos(rot3(mul(Cm, Wd)), to_game_p(pos(Wd)))
                        if U is not None:
                            Wt[i] = mul(U, Wt[i])
                    elif p is not None:
                        Wt[i] = mul(Wt[p], Lk[i])
                    else:
                        Wt[i] = A[i]
                    L = mul(inv(Wt[p]), Wt[i]) if p is not None else Wt[i]
                    tracks[i][0].append(quat_of(L))
                    tracks[i][1].append(pos(L))
            anims = [a for a in anims if int(re.match(r"a(\d+)", a[0]).group(1)) != aid]
            anims.append((nm, frames, tracks, pscale))
            done.append(nm)
        log("%d own clips: %s" % (len(done), ", ".join(done)))
    write_glb(out_path, mdl, J, korder, kparent, Lk, A, out_tris, locs, anims, log, props)
    return out_path


def load_map(path, depth=0):
    """The map, with "extends": <map> (next to it or in tools/rigmaps/) under it: its bones, locators and keep are added
    to, its chains replaced when the map gives its own."""
    cfg = json.load(open(path, encoding="utf-8"))
    ext = cfg.get("extends")
    if not ext:
        return cfg
    if depth > 4:
        raise RetargetError("map: \"extends\" goes too deep")
    cand = [os.path.join(os.path.dirname(os.path.abspath(path)), ext),
            os.path.join(os.path.dirname(os.path.abspath(__file__)), "rigmaps", ext)]
    base_path = next((c for c in cand if os.path.exists(c)), None)
    if base_path is None:
        raise RetargetError("map: extends %s, not found next to the map nor in tools/rigmaps/" % ext)
    base = load_map(base_path, depth + 1)
    out = dict(base)
    for k in ("bones", "locators"):
        out[k] = dict(base.get(k, {}), **cfg.get(k, {}))
    out["keep"] = list(base.get("keep", [])) + list(cfg.get("keep", []))
    if "chains" in cfg:
        out["chains"] = cfg["chains"]
    return out


def ngprig_locator_id(name):
    import ngprig
    return ngprig._locator_id(name)


# ---------------------------------------------------------------- the .glb

def image_data(mdl, key):
    """(name, bytes or None) of a model image: a file next to a .dae, or an image inside a .glb (mdl.images)."""
    if key in getattr(mdl, "images", {}):
        return mdl.images[key]
    name = os.path.splitext(os.path.basename(key))[0]
    return name, (open(key, "rb").read() if os.path.isfile(key) else None)


def write_glb(path, mdl, J, korder, kparent, Lk, A, tris, locs, anims, log, props=()):
    binb = bytearray()
    j = {"asset": {"version": "2.0", "generator": "WotM tools/retarget.py"}, "scene": 0, "scenes": [{"nodes": [0]}],
         "nodes": [], "meshes": [], "materials": [], "textures": [], "images": [], "samplers": [{}],
         "accessors": [], "bufferViews": [], "buffers": [], "skins": [], "animations": []}

    def view(data):
        while len(binb) % 4:
            binb.append(0)
        j["bufferViews"].append({"buffer": 0, "byteOffset": len(binb), "byteLength": len(data)})
        binb.extend(data)
        return len(j["bufferViews"]) - 1

    def acc(fmt, ctype, typ, rows, minmax=False):
        flat = [x for r in rows for x in (r if isinstance(r, (list, tuple)) else (r,))]
        a = {"bufferView": view(struct.pack("<%d%s" % (len(flat), fmt), *flat)), "componentType": ctype,
             "count": len(rows), "type": typ}
        if minmax:
            n = len(rows[0]) if isinstance(rows[0], (list, tuple)) else 1
            cols = [[(r[c] if n > 1 else r) for r in rows] for c in range(n)]
            a["min"] = [min(c) for c in cols]
            a["max"] = [max(c) for c in cols]
        j["accessors"].append(a)
        return len(j["accessors"]) - 1

    root = quat_col(list(ROOT_Q))
    j["nodes"].append({"name": "WotM", "rotation": list(ROOT_Q), "children": []})
    nidx = {}
    for i in korder:
        nidx[i] = len(j["nodes"])
        L = Lk[i]
        j["nodes"].append({"name": J[i]["name"], "translation": pos(L), "rotation": quat_of(L)})
    for i in korder:
        p = kparent[i]
        (j["nodes"][nidx[p]] if p is not None else j["nodes"][0]).setdefault("children", []).append(nidx[i])
    for nm, (ji, pt) in sorted(locs.items()):
        Ai = A[ji]
        lp = apply(tmat(rot3(Ai)), [a - b for a, b in zip(pt, pos(Ai))], 0.0)
        # world orientation = identity: ngprig then takes the base's orientation for it
        q = quat_of(tmat(rot3(Ai)))
        j["nodes"].append({"name": nm, "translation": lp, "rotation": q})
        j["nodes"][nidx[ji]].setdefault("children", []).append(len(j["nodes"]) - 1)
    # mesh: one primitive per image; an image that is missing or not PNG/JPEG goes untextured, with a warning
    imgs = []
    got = {}
    for im in sorted({img for img, _t in list(tris) + [x for pr in props for x in pr["tris"]] if img}, key=str):
        name, data = image_data(mdl, im)
        mime = {b"\x89PNG": "image/png", b"\xff\xd8\xff": "image/jpeg"}
        mt = next((v for k, v in mime.items() if data and data.startswith(k)), None)
        if mt is None:
            log("warning: image %s %s; drawn without it" % (name, "not found" if data is None else "is not PNG or JPEG"))
            continue
        got[im] = (name, data, mt)
        imgs.append(im)
    tris = [(img if img in got else None, t) for img, t in tris]
    props = [dict(pr, tris=[(img if img in got else None, t) for img, t in pr["tris"]]) for pr in props]
    mat_of = {}
    for k, im in enumerate(imgs):
        name, data, mt = got[im]
        j["images"].append({"name": name, "mimeType": mt, "bufferView": view(data)})
        j["textures"].append({"source": k, "sampler": 0})
        j["materials"].append({"name": name, "pbrMetallicRoughness": {"baseColorTexture": {"index": k}, "metallicFactor": 0.0}})
        mat_of[im] = k
    sj = {i: q for q, i in enumerate(korder)}
    prims = []
    body_imgs = [im for im in imgs if any(img == im for img, _t in tris)]
    for im in body_imgs + ([None] if any(img is None for img, _t in tris) else []):
        P, N, T, JJ, WW, idx = [], [], [], [], [], []
        seen = {}
        for img, tri in tris:
            if img != im:
                continue
            for c in tri:
                key = (tuple(c[0]), tuple(c[1]), tuple(c[2]))
                if key not in seen:
                    seen[key] = len(P)
                    P.append(apply(root, c[0]))
                    N.append(apply(root, c[1], 0.0))
                    T.append(c[2])
                    js = [sj[a] for a, _w in c[3]] + [0] * (4 - len(c[3]))
                    ws = [w for _a, w in c[3]] + [0.0] * (4 - len(c[3]))
                    JJ.append(js)
                    WW.append(ws)
                idx.append(seen[key])
        pr = {"attributes": {"POSITION": acc("f", 5126, "VEC3", P, True), "NORMAL": acc("f", 5126, "VEC3", N),
                             "TEXCOORD_0": acc("f", 5126, "VEC2", T), "JOINTS_0": acc("H", 5123, "VEC4", JJ),
                             "WEIGHTS_0": acc("f", 5126, "VEC4", WW)},
              "indices": acc("I", 5125, "SCALAR", idx), "mode": 4}
        if im is not None:
            pr["material"] = mat_of[im]
        prims.append(pr)
    j["meshes"].append({"name": "body", "primitives": prims})
    ibm = [inv(mul(root, A[i])) for i in korder]
    j["skins"].append({"joints": [nidx[i] for i in korder],
                       "inverseBindMatrices": acc("f", 5126, "MAT4", [[m[r][c] for c in range(4) for r in range(4)] for m in ibm])})
    j["nodes"].append({"name": "body", "mesh": 0, "skin": 0})
    j["nodes"][0]["children"].append(len(j["nodes"]) - 1)
    # rigid props: a mesh node under their joint, in its space
    pnode = []
    for pr in props:
        pp = []
        for im in sorted({img for img, _t in pr["tris"]}, key=str):
            P, N, T, idx = [], [], [], []
            for img, tri in pr["tris"]:
                if img != im:
                    continue
                for c in tri:
                    idx.append(len(P))
                    P.append(c[0])
                    N.append(c[1])
                    T.append(c[2])
            p = {"attributes": {"POSITION": acc("f", 5126, "VEC3", P, True), "NORMAL": acc("f", 5126, "VEC3", N),
                                "TEXCOORD_0": acc("f", 5126, "VEC2", T)}, "indices": acc("I", 5125, "SCALAR", idx), "mode": 4}
            if im is not None:
                p["material"] = mat_of[im]
            pp.append(p)
        j["meshes"].append({"name": pr["name"], "primitives": pp})
        pnode.append(len(j["nodes"]))
        j["nodes"].append({"name": pr["name"], "mesh": len(j["meshes"]) - 1})
        j["nodes"][nidx[pr["joint"]]].setdefault("children", []).append(pnode[-1])
    # animations: rotation per joint that moves, translation where it moves, the props' scale
    times = {}
    for nm, frames, tracks, pscale in anims:
        if frames not in times:
            times[frames] = acc("f", 5126, "SCALAR", [f / FPS for f in range(frames)], True)
        an = {"name": nm, "samplers": [], "channels": []}
        for pi, ss in sorted(pscale.items()):
            if props[pi]["show"] is None and props[pi]["ids"] is None:
                continue
            an["samplers"].append({"input": times[frames], "output": acc("f", 5126, "VEC3", [[x, x, x] for x in ss]),
                                   "interpolation": "LINEAR"})
            an["channels"].append({"sampler": len(an["samplers"]) - 1, "target": {"node": pnode[pi], "path": "scale"}})
        for i, (qs, ps) in tracks.items():
            q0 = quat_of(Lk[i])
            if any(abs(abs(sum(a * b for a, b in zip(q, q0))) - 1.0) > 1e-7 for q in qs):
                prev = None
                fixed = []
                for q in qs:   # one hemisphere, for linear blending
                    if prev is not None and sum(a * b for a, b in zip(q, prev)) < 0:
                        q = [-x for x in q]
                    fixed.append(q)
                    prev = q
                an["samplers"].append({"input": times[frames], "output": acc("f", 5126, "VEC4", fixed), "interpolation": "LINEAR"})
                an["channels"].append({"sampler": len(an["samplers"]) - 1, "target": {"node": nidx[i], "path": "rotation"}})
            p0 = pos(Lk[i])
            if any(max(abs(a - b) for a, b in zip(p, p0)) > 1e-4 for p in ps):
                an["samplers"].append({"input": times[frames], "output": acc("f", 5126, "VEC3", ps), "interpolation": "LINEAR"})
                an["channels"].append({"sampler": len(an["samplers"]) - 1, "target": {"node": nidx[i], "path": "translation"}})
        if not an["channels"]:   # a still action still needs its length
            an["samplers"].append({"input": times[frames], "output": acc("f", 5126, "VEC4", [quat_of(Lk[korder[0]])] * frames),
                                   "interpolation": "LINEAR"})
            an["channels"].append({"sampler": 0, "target": {"node": nidx[korder[0]], "path": "rotation"}})
        j["animations"].append(an)
    while len(binb) % 4:
        binb.append(0)
    j["buffers"].append({"byteLength": len(binb)})
    js = json.dumps(j, separators=(",", ":")).encode("utf-8")
    while len(js) % 4:
        js += b" "
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binb)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binb), 0x004E4942) + bytes(binb))
    log("wrote %s (%.1f MB)" % (path, (28 + len(js) + len(binb)) / 1e6))


def main(argv):
    ap = argparse.ArgumentParser(description="retarget a kit .glb's animations onto a rigged model (.dae, .glb, .gltf) for ngprig")
    ap.add_argument("source")
    ap.add_argument("model")
    ap.add_argument("map")
    ap.add_argument("out")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--size", type=float, default=1.0, help="height as a fraction of the source's (default 1)")
    g.add_argument("--height", type=float, help="height in game units")
    ap.add_argument("--front", choices=("+y", "-y"), help="the way the model faces once Z is up (default: +y for a Z-up "
                                                          "scene, -y for a Y-up one)")
    ap.add_argument("--up", choices=("y", "z"), help="the scene's up axis (default: the file's; a .dae without <up_axis> "
                                                     "by where its head joint is)")
    ap.add_argument("--turn", help="quarter turns about the game's axes after up and front, e.g. x90,z180 (x right, y front, "
                                   "z up)")
    ap.add_argument("--clips", help="only these animation ids (e.g. 90,1)")
    ap.add_argument("--own", help="the model's own clips (JSON) in place of the source's with the same id")
    ap.add_argument("--base", help="the .NGP the source was exported from: its layers turn from the pose the game adds "
                                   "them to")
    argv = list(argv)
    for k, x in enumerate(argv[:-1]):   # "--front -y": argparse would read -y as an option
        if x == "--front" and argv[k + 1] in ("-y", "+y"):
            argv[k:k + 2] = ["--front=" + argv[k + 1], ""]
    a = ap.parse_args([x for x in argv if x != ""])
    try:
        build(a.source, a.model, a.map, a.out, a.size, a.height, a.front, a.clips, a.own, up=a.up, turn=a.turn, base=a.base)
    except (RetargetError, ngpanim.AnimError, OSError, KeyError, ValueError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
