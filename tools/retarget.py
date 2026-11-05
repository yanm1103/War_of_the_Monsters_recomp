#!/usr/bin/env python3
"""A game monster's animations on another model's skeleton: a rigged model (Collada .dae) and a bone map become the rig .glb
that `modkit.py monster rig` (tools/ngprig.py) takes.

  python tools/retarget.py <source.glb> <model.dae> <map.json> <out.glb> [--size F | --height H] [--front +y|-y]
                           [--clips 90,1,...]

- source.glb: the base costume as the kit exports it (`modkit.py monster mesh export`, e.g. LIZARD0.glb, "WotM" root): its
  skeleton and its actions `a<id>` are the motion.
- model.dae: the new body, a skinned mesh on an armature (Z up; `--front` is the way it faces, +y by default). Its scene
  pose is the bind pose. The texture of each material is the effect's diffuse image, read from next to the file.
- map.json: which model joint follows which source bone (`tools/rigmaps/biped_togera.json` is the 3ds Max Biped, Bip01_*,
  on Togera). Keys:
  - "bones": {joint: {"from": bone | [bone_a, bone_b, f], "move": true, "align": [child_joint, child_bone]}}. The joint turns
    in the world as its source bone turns from its rest (with [a, b, f], a blend of the two turns at f); "move" also carries
    the bone's change of position (scaled by the ratio of the two rest heights); "align" first turns the joint so that the
    direction to child_joint is the source's direction to child_bone (the model is posed like the source before the motion
    goes in, e.g. arms from a T-pose to the source's); "level": true lays the joint's underside flat on the ground at rest
    (a foot the model's bind pose leaves tilted; the plane through the lowest point of each cell of a grid over the vertices
    it carries, toes included); "turn": [x, y, z] turns it at rest by those degrees about the game axes (+x lifts what
    faces forward: closes a jaw the bind pose leaves open).
  - "chains": [{"joints": [...], "from": [...]}]: two chains of different lengths (tails), matched by their position along
    the chain.
  - "locators": {alias or loc_<hex>: {"joint": joint, "at": [x, y, z] | [joint, ...]}}: a point in the model's units (before
    the size) or the mean of some joints. Every locator of the source that the map does not name goes on the joint its
    source bone maps to (or the nearest mapped ancestor), with the source offset scaled.
  - "keep": joints kept without a source (they hold the rest pose against their parent).
  Joints the map does not reach are rigid on the nearest kept ancestor: their weights go to it.
- Size: the model is scaled to the source's height (`--size 1.2` = 20% taller; `--height H` in game units), feet on the
  ground. The out.glb is in the game's axes and units (the "WotM" root), so ngprig takes it as is.

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

    def __init__(self, path):
        self.path = path
        root = ET.parse(path).getroot()
        self.joints = []          # [{"name", "parent", "world"}]
        jidx = {}                 # sid / id / name -> joint index
        inst = []                 # (node world, instance_controller element)
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
            if f is not None and f.text:
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
                    uv = T[p[e * st + inputs["TEXCOORD"][0]]] if T is not None else [0.0, 0.0]
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


# ---------------------------------------------------------------- the source (kit .glb)

class Source:
    def __init__(self, path):
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


# ---------------------------------------------------------------- the retarget

def build(src_path, model_path, map_path, out_path, size=1.0, height=None, front="+y", clips=None, log=print):
    src = Source(src_path)
    mdl = Model(model_path)
    cfg = load_map(map_path)
    J = mdl.joints
    nj = len(J)

    def jid(name):
        if name not in mdl.names:
            raise RetargetError("map: joint %s is not in the model" % name)
        return mdl.names[name]

    # ---- axes and size: model -> game (Z up, facing +Y)
    Cm = eye() if front == "+y" else [[-1.0, 0, 0, 0], [0, -1.0, 0, 0], [0, 0, 1.0, 0], [0, 0, 0, 1.0]]
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
    for name, spec in cfg.get("bones", {}).items():
        f = spec["from"]
        if isinstance(f, str):
            bones[jid(name)] = {"a": src.node(f), "b": None, "f": 0.0, "move": bool(spec.get("move")), "align": spec.get("align"),
                                "level": bool(spec.get("level")), "turn": spec.get("turn")}
        else:
            bones[jid(name)] = {"a": src.node(f[0]), "b": src.node(f[1]), "f": float(f[2]), "move": bool(spec.get("move")),
                                "align": spec.get("align"), "level": bool(spec.get("level")), "turn": spec.get("turn")}
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
        if bones.get(i, {}).get("turn"):
            A[i] = with_pos(mul(axis_turn(bones[i]["turn"]), rot3(A[i])), pos(A[i]))
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

    # ---- motion
    rest_src = {}
    for b in {x for bb in bones.values() for x in (bb["a"], bb["b"]) if x is not None}:
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
    want = None
    if clips:
        want = set(int(x) for x in clips.split(","))
    anims = []
    for nm, clip, frames in src.clips:
        m = re.match(r"a(\d+)", nm)
        if want is not None and (not m or int(m.group(1)) not in want):
            continue
        tracks = {i: ([], []) for i in korder}   # joint -> (quats, positions) local
        for f in range(frames):
            t = f / FPS
            cache = {}
            D = {}
            for b in rest_src:
                W = src.world(b, clip, t, cache)
                D[b] = (quat_of(mul(rot3(W), tmat(rot3(rest_src[b])))), pos(W))
            Wt = {}
            for i in korder:
                p = kparent[i]
                bb = bones.get(i)
                if bb is not None:
                    q = D[bb["a"]][0] if bb["b"] is None else slerp(D[bb["a"]][0], D[bb["b"]][0], bb["f"])
                    R = mul(quat_col(q), rot3(A[i]))
                else:
                    R = rot3(mul(Wt[p], Lk[i])) if p is not None else rot3(A[i])
                if i in move_k:
                    d = [a - c for a, c in zip(D[bb["a"]][1], pos(rest_src[bb["a"]]))]
                    P = [a + move_k[i] * c for a, c in zip(pos(A[i]), d)]
                elif p is not None:
                    P = apply(Wt[p], pos(Lk[i]))
                else:
                    P = pos(A[i])
                Wt[i] = with_pos(R, P)
                L = mul(inv(Wt[p]), Wt[i]) if p is not None else Wt[i]
                tracks[i][0].append(quat_of(L))
                tracks[i][1].append(pos(L))
        anims.append((nm, frames, tracks))
    log("%d actions retargeted, %d joints kept (%s), %d locators" % (
        len(anims), len(korder), ", ".join(J[i]["name"] for i in korder), len(locs)))
    write_glb(out_path, mdl, J, korder, kparent, Lk, A, out_tris, locs, anims, log)
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

def write_glb(path, mdl, J, korder, kparent, Lk, A, tris, locs, anims, log):
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
    # mesh: one primitive per image
    imgs = sorted({img for img, _t in tris if img}, key=str)
    mat_of = {}
    for k, im in enumerate(imgs):
        data = open(im, "rb").read()
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise RetargetError("%s: textures must be PNG" % im)
        j["images"].append({"name": os.path.splitext(os.path.basename(im))[0], "mimeType": "image/png", "bufferView": view(data)})
        j["textures"].append({"source": k, "sampler": 0})
        j["materials"].append({"name": os.path.splitext(os.path.basename(im))[0],
                               "pbrMetallicRoughness": {"baseColorTexture": {"index": k}, "metallicFactor": 0.0}})
        mat_of[im] = k
    sj = {i: q for q, i in enumerate(korder)}
    prims = []
    for im in imgs + ([None] if any(img is None for img, _t in tris) else []):
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
    # animations: rotation per joint that moves, translation where it moves
    times = {}
    for nm, frames, tracks in anims:
        if frames not in times:
            times[frames] = acc("f", 5126, "SCALAR", [f / FPS for f in range(frames)], True)
        an = {"name": nm, "samplers": [], "channels": []}
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
    ap = argparse.ArgumentParser(description="retarget a kit .glb's animations onto a rigged model (Collada) for ngprig")
    ap.add_argument("source")
    ap.add_argument("model")
    ap.add_argument("map")
    ap.add_argument("out")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--size", type=float, default=1.0, help="height as a fraction of the source's (default 1)")
    g.add_argument("--height", type=float, help="height in game units")
    ap.add_argument("--front", choices=("+y", "-y"), default="+y", help="the way the model faces (Z up)")
    ap.add_argument("--clips", help="only these animation ids (e.g. 90,1)")
    a = ap.parse_args(argv)
    try:
        build(a.source, a.model, a.map, a.out, a.size, a.height, a.front, a.clips)
    except (RetargetError, ngpanim.AnimError, OSError, KeyError, ValueError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
