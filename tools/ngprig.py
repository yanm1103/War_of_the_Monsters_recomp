#!/usr/bin/env python3
"""A monster with a skeleton of its own: a rigged .glb (Blender) becomes a new body on a base monster's .NGP/.PTR.

  python tools/ngprig.py <base.NGP> <rig.glb> <out_dir> [--name NAME] [--front +y|-y] [--scale S | --height H] [--no-fill]

The base keeps everything but the body: the type's code, shadow, reticle, attachments, collision grid and sounds. Its main
CHAR_INSTANCE is replaced by a new one built from the .glb:
- skeleton: every joint of the skin that carries weights, a locator or a rigid mesh, with its ancestors (names and hierarchy
  are free); per joint a SKEL_BONE (bind pose = the .glb's rest pose) and an ANIM_XFORM with 3 rotation and 3 translation
  channels and the rest rotation as pre-rotation (rest angles 0);
- mesh: the skinned meshes (3 bones per vertex, the heaviest kept) and the rigid ones (on the joint above them), in blocks
  of at most 32 bones (one OBJECT per block and up to 5 textures; a bone used by two blocks gets a second output). A rigid
  mesh that scales or moves against that joint in some action (a claw that grows, a part shown only in some moves) gets
  its own ANIM_XFORM and ANIM_SCALE under the joint and a rigid OBJECT per texture, as the game's own parts do (bone
  scale does not reach skinned meshes: the game's skeleton has no scale);
- animations: every action `a<id>` (ids up to 299; `a<id>_loop` / `a<id>_once` force the loop flag), sampled at 60 per
  second and solved into the channels; the hand and foot contact channels (footsteps, dust) come from the height of the
  contact locators, the sounds and the AnimPappy channels (ids 1..7, throws) from the base's animation with that id. Every
  id the base has and the .glb lacks gets the rest pose for the base's length (`--no-fill`: left out; the game then has no
  animation for that move); a base animation with no translation curve, or only zero ones on channels the idle keeps away
  from 0, is a layer the game adds on top of the main one, so its new one holds only the rotations that leave the rest;
- locators: empties (or joints) named `loc_<hex id>` or by alias (`hand_l` 0x802, `hand_r` 0x803, `foot_l` 0x804,
  `foot_r` 0x805, `grabbed` 0x806, `grab` 0x807, `impale` 0x808, `chest` 0x809, `hold` 0x80A, `mouth` 0x80B, `head` 0x80C,
  `look` 0x80E, `extra1` 0x816, `extra2` 0x817), under the joint they follow; the orientation is the empty's when it was
  rotated, else the base's. A locator of the base the .glb lacks is placed where the base has it (scaled to the height),
  on the nearest joint, and the log says so;
- collision: one box per mesh named `coll*` (not drawn), else one box around the rest mesh; they follow the main root;
- textures: a material `tex<texId>` naming one of the base's keeps it; any other material with an image gets a new texId
  (a copy of the body's packet in the .TEX) and the image as `<name>_t<texId>.png` next to the files, the PNG the port
  draws; a material without an image gets a PNG of its color.

Axes and size: a .glb with the kit's "WotM" root (`tools/monglb.py --edit`) is in the game's axes and units; any other is a
Blender scene (Z up, facing -Y, meters) turned to face the game's +Y and scaled to the base's height (`--front`, `--scale`,
`--height` change that). The feet go at height 0 (the rest pose's lowest point is the ground).
Base: any monster but Congar (its code moves 8 bones by number, BoneSpring). Only the standard library: it runs inside the
mod kit. Formats: docs/FORMATS.md; guide: docs/guides/MODELS_3D.md ("New skeleton").
"""
import base64
import math
import os
import re
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ngpanim  # noqa: E402
import ngpmesh  # noqa: E402
import tex  # noqa: E402
import vifpkt  # noqa: E402
from ngpanim import eye, inv, mul, tr  # noqa: E402

BASE = 0xA00000
FPS = 60.0
MAX_SLOTS = 32            # matrices per palette block (VU1 palette, Scene::palette); Jelly uses 32
MAX_TEX = 5               # textures per OBJECT (gsTexCtx[5])
MAX_OUTPUTS = 64          # bone outputs the native viewer and tools/pose.py follow
CONTACT_IDS = (0x802, 0x803, 0x804, 0x805, 0x816, 0x817)   # AnimContact: hands, feet, extra limbs
FEET = (0x804, 0x805, 0x816, 0x817)
PAPPY_IDS = (1, 2, 3, 4, 5, 6, 7)                         # AnimPappy: rotation, translation, on/off
BONESPRING_TYPE = 0x20    # Congar: initDynamics hooks springs on atMat 4, 8, 11, 13, 20, 22, 26, 28
ALIASES = {"hand_l": 0x802, "hand_r": 0x803, "foot_l": 0x804, "foot_r": 0x805, "grabbed": 0x806, "grab": 0x807,
           "impale": 0x808, "chest": 0x809, "hold": 0x80A, "mouth": 0x80B, "head": 0x80C, "look": 0x80E,
           "extra1": 0x816, "extra2": 0x817}


class RigError(Exception):
    pass


# ---------------------------------------------------------------- small vector/matrix helpers (row form unless noted)

def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def _norm(a):
    return math.sqrt(_dot(a, a))


def orth(m):
    """Row matrix with its 3x3 part made a rotation (Gram-Schmidt on the x and y rows); None when degenerate (scale 0)."""
    x = m[0][:3]
    nx = _norm(x)
    if nx < 1e-9:
        return None
    x = [c / nx for c in x]
    y = m[1][:3]
    d = _dot(y, x)
    y = [a - d * b for a, b in zip(y, x)]
    ny = _norm(y)
    if ny < 1e-9:
        return None
    y = [c / ny for c in y]
    z = _cross(x, y)
    return [x + [0.0], y + [0.0], z + [0.0], [m[3][0], m[3][1], m[3][2], 1.0]]


def split_scale(L):
    """Row matrix L = diag(s) . R . T -> (s, R with T; None when a scale is 0)."""
    s = [_norm(L[r][:3]) for r in range(3)]
    return s, orth(L)


def rot_only(m):
    return [m[0][:3] + [0.0], m[1][:3] + [0.0], m[2][:3] + [0.0], [0.0, 0.0, 0.0, 1.0]]


def col_apply(m, p, w=1.0):
    """Column-form matrix times point (w=1) or direction (w=0)."""
    return [m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] * w for r in range(3)]


def percentile(vals, q):
    s = sorted(vals)
    if not s:
        return 0.0
    k = max(0, min(len(s) - 1, int(round(q * (len(s) - 1)))))
    return s[k]


# ---------------------------------------------------------------- the base monster

class BaseInfo:
    """What the rig takes from the base: the main instance, templates, height, locators, textures, animations."""

    def __init__(self, path, log):
        self.path = path
        self.mb = ngpmesh.Base(path)
        self.game = ngpanim.Game(path)
        g, img = self.game, self.game.img
        self.ci = g.main
        self.type = img.u32(self.ci) >> 18
        if self.type == BONESPRING_TYPE:
            raise RigError("Congar's code moves 8 of its bones by number (BoneSpring): pick another base")
        objs = sorted(o for o, (ci, _c, _s) in self.mb.skinned.items() if ci == self.ci)
        if not objs:
            raise RigError("%s: the main instance has no skinned OBJECT" % os.path.basename(path))
        self.objs = objs
        self.tmpl = max(objs, key=lambda o: sum(q for _a, q in self.mb.skinned[o][2]))
        self.gif_first, self.gif_rest = ngpmesh._gif_templates(self.mb, self.tmpl)
        # height: z extent of the rest positions of the main instance's skinned meshes
        zs = []
        for o in objs:
            for a, q in self.mb.skinned[o][2]:
                p = vifpkt.parse(bytes(self.mb.d[a - BASE:a - BASE + 16 * q]), True)
                for b in p.batches:
                    for v in b.pos:
                        zs.append(struct.unpack("<f", struct.pack("<I", v[2]))[0])
        self.zmin, self.zmax = min(zs), max(zs)
        self.height = self.zmax - max(0.0, self.zmin)
        # locators of root 0: world (bind pose) of the transform above each ATTACHPT
        self.locs = {}
        seen = set()

        def walk(n, holder, depth):
            if depth > 80 or n in seen or not img.ok(n):
                return
            seen.add(n)
            op = img.u32(n) & 63
            if op == 40:
                lid = img.u32(n) >> 18
                if lid and holder is not None and lid not in self.locs:
                    self.locs[lid] = g.bind[holder]
                return
            h = n if n in g.nodes else holder
            for arr, cnt in g._kids(n, op):
                for k in range(min(cnt, 4096)):
                    walk(img.u32(arr + 4 * k), h, depth + 1)
        walk(g.root, None, 0)
        # textures the base's root 0 and 1 objects use: texId -> gsTexCtx block (TEX0, TEX1, MIP)
        self.tex_ctx = {}
        for r in img.roots()[:2]:
            for n in vifpkt._subtree(img, r):
                if img.u32(n) & 63 != 0:
                    continue
                for k in range(min(img.u16(n + 0x5A), MAX_TEX)):
                    t = img.u16(n + 0x50 + 2 * k)
                    self.tex_ctx.setdefault(t, bytes(self.mb.d[n + 0x70 + 0x30 * k - BASE:n + 0xA0 + 0x30 * k - BASE]))
        self.body_tex = img.u16(self.tmpl + 0x50)
        # animations of the main instance, first slot of each id
        ch = img.u32(self.ci + 0x20)
        actx = img.u32(self.ci + 0x10)
        # a layer (looking around, flinches, leans) is added by the game on top of the main animation: it animates no
        # translation (Togera) or only zeros on translation channels that the idle keeps away from 0 (Ultra-V: deltas)
        tch = set()
        for n, nd in g.nodes.items():
            if nd.op == 17 and nd.ci == self.ci:
                tch.update(i for i in g.xform(n)["tidx"] if i >= 0)
        all_anims = list(g.anims())
        idle_t = {}
        idle_an = next((an for aid, _k, _st, _et, _n, an in all_anims if aid == 90), None)
        if idle_an is not None:
            for _a, c in g.curves(idle_an):
                if c.idx in tch:
                    idle_t[c.idx] = c.eval(img.f32(idle_an + 4))

        def is_layer(an):
            if not tch:
                return False
            tc = [c for _a, c in g.curves(an) if c.idx in tch]
            if not tc:
                return True
            st, et = img.f32(an + 4), img.f32(an + 8)
            steps = max(1, min(64, int(et - st)))
            if any(abs(c.eval(st + (et - st) * i / steps)) > 1e-3 for c in tc for i in range(steps + 1)):
                return False
            return any(abs(idle_t.get(c.idx, 0.0)) > 1e-2 for c in tc)
        self.anims = {}
        for aid, k, st, et, _n, an in all_anims:
            if aid in self.anims:
                continue
            self.anims[aid] = {"k": k, "st": st, "frames": int(math.floor(et - st + 1e-6)) + 1, "an": an,
                               "head": bytes(img.d[an - BASE:an - BASE + 0x18]),
                               "ctx": bytes(img.d[actx + 60 * k - BASE:actx + 60 * k + 60 - BASE]),
                               "loop": img.u8(actx + 60 * k + 0x29),
                               "layer": is_layer(an)}
        if 90 not in self.anims:
            raise RigError("%s has no idle (90)" % os.path.basename(path))
        mp = img.u32(ch + 8)
        self.chmap = {}
        if mp and img.ok(mp):
            for i in range(img.u32(mp)):
                self.chmap[img.u16(mp + 4 + 4 * i)] = img.u16(mp + 6 + 4 * i)
        # the LOD the skinned meshes hang from (CI kid), copied for the new body
        self.lod = None
        for k in range(img.u32(self.ci + 0x28)):
            c = img.u32(self.ci + 0x2C + 4 * k)
            if img.ok(c) and img.u32(c) & 63 == 2:
                self.lod = c
        log("base %s: type %#x, height %.1f, %d animations, %d locators, body texture %d" % (
            os.path.basename(path), self.type, self.height, len(self.anims), len(self.locs), self.body_tex))

    def curve_at(self, aid, chan_id, frames):
        """Values of the base animation `aid`'s channel with map id `chan_id` stretched to `frames` frames (None: absent)."""
        a = self.anims.get(aid)
        idx = self.chmap.get(chan_id)
        if a is None or idx is None:
            return None
        cv = next((c for _addr, c in self.game.curves(a["an"]) if c.idx == idx), None)
        if cv is None:
            return None
        n0 = a["frames"]
        s = (n0 - 1) / float(frames - 1) if frames > 1 else 0.0
        return [cv.eval(a["st"] + f * s) for f in range(frames)]


# ---------------------------------------------------------------- the rig (.glb)

def _locator_id(name):
    if not name:
        return None
    nm = name.lower().split(".")[0]
    m = re.fullmatch(r"loc_?(?:0x)?([0-9a-f]{3,4})", nm)
    if m:
        return int(m.group(1), 16)
    return ALIASES.get(nm)


def _clip_id(name):
    m = re.search(r"(?:^|\|)a(\d+)(?:_(\d+|loop|once))?(?:\.\d+)?$", name or "")
    if not m:
        return None, None
    suf = m.group(2)
    return int(m.group(1)), (None if suf is None or suf.isdigit() else suf == "loop")


class Joint:
    __slots__ = ("node", "name", "parent", "kids", "rest", "local", "idx", "out", "dups", "pre", "t0", "det0")

    def __init__(self, node, name):
        self.node, self.name, self.parent, self.kids = node, name, None, []
        self.out, self.dups = -1, []


class Rig:
    def __init__(self, path, base, front=None, scale=None, height=None, log=print):
        self.log = log
        self.ed = ed = ngpanim.Edited(path)
        self.path = path
        glb = ed.glb
        j = glb.j
        nodes = glb.nodes
        skins = j.get("skins", [])
        if not skins:
            raise RigError("no armature in the .glb (a mesh with a skin)")
        jset = set(k for s in skins for k in s["joints"])

        def ancestors(i):
            out = []
            while i in ed.parent:
                i = ed.parent[i]
                out.append(i)
            return out

        def joint_above(i):
            return next((a for a in ancestors(i) if a in jset), None)
        game_units = any(nodes[a].get("name") == "WotM" for k in jset for a in ancestors(k))
        self.game_units = game_units
        self.front = front or ("+y" if game_units else "-y")
        cache = {}
        W0 = [ed.world(i, None, 0.0, cache) for i in range(len(nodes))]
        # what the idle shows: rigid parts go in as its first frame shows them (a part scaled to 0 there is one the game
        # hides and shows in some move); the kit's bind pose drops the scale of the game's scale nodes
        idle = next((c for nm, c in ed.clips.items() if _clip_id(nm)[0] == 90), None)
        cache_i = {}

        def W_idle(i):
            return ed.world(i, idle, 0.0, cache_i) if idle is not None else W0[i]

        def scale_ref(i):
            top = [a for a in ancestors(i) if a not in jset]
            return abs(ngpanim.det3(W0[top[0]])) if top else 1.0

        def unscaled(a, W):
            ref = scale_ref(a) or 1.0
            return abs(abs(ngpanim.det3(W)) / ref - 1.0) < 0.05

        def steady_joint(i):
            """The nearest joint at or above node i with no scale of its own, at rest and in the idle."""
            for a in ([i] if i in jset else []) + ancestors(i):
                if a in jset and unscaled(a, W0[a]) and unscaled(a, W_idle(a)):
                    return a
            return joint_above(i)
        A = [[1.0, 0.0, 0.0, 0.0], [0.0, 0.0, -1.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 0.0, 1.0]]
        Y = eye() if self.front == "+y" else [[-1.0, 0.0, 0.0, 0.0], [0.0, -1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0],
                                               [0.0, 0.0, 0.0, 1.0]]
        C0 = mul(Y, A)
        # the local axes: inside the kit's "WotM" root they already are the game's; in a Blender scene they turn like the world
        self.Cr = eye() if game_units else inv(C0)
        self.C = C0   # scaled below, once the size is known (live_local only runs after that)

        def live(ni, holder):
            """Whether a rigid mesh moves or scales against the joint that holds it in some action (else it can ride
            the joint's palette slot)."""
            nodes_k = [ni, holder] + ancestors(ni)
            ref = None
            for clip in ed.clips.values():
                cache = {}
                ts = ed.key_times(clip, nodes_k) or [0.0]
                for t in ts:
                    Wn = ed.world(ni, clip, t, cache)
                    Wh = ed.world(holder, clip, t, cache)
                    L = mul(inv(Wh), Wn) if abs(ngpanim.det3(Wh)) > 1e-12 else Wn
                    if ref is None:
                        ref = L
                    elif max(abs(L[r][c] - ref[r][c]) for r in range(3) for c in range(3)) > 1e-3 or \
                            max(abs(L[r][3] - ref[r][3]) for r in range(3)) > 1e-3 * (1.0 + _norm([ref[r][3] for r in range(3)])):
                        return True
            return False

        # ---- meshes at rest (game axes, unscaled): corners (pos, normal, uv16, rgba, ((node, w), ...)) by material
        mats = j.get("materials", [])
        self.images = j.get("images", [])
        prims = []          # (material key, [triangle of corners])
        coll_pts = []       # [[points]] of the coll* meshes
        hidden_rigid = []
        self.rigid = []     # live rigid meshes: {"node", "holder", "name", "prims"}, corners in the mesh's own axes
        for ni, nd in enumerate(nodes):
            if "mesh" not in nd:
                continue
            mesh = j["meshes"][nd["mesh"]]
            is_coll = (nd.get("name") or mesh.get("name") or "").lower().startswith("coll")
            skin = skins[nd["skin"]] if "skin" in nd else None
            is_live = False
            if skin is not None:
                ibm = glb.accessor(skin["inverseBindMatrices"]) if "inverseBindMatrices" in skin else None
                M = [mul(W0[k], ngpmesh._col(ibm[q]) if ibm else eye()) for q, k in enumerate(skin["joints"])]
                holder = None
            else:
                holder = steady_joint(ni)
                is_live = not is_coll and holder is not None and live(ni, holder)
                if is_live:
                    # drawn by its own OBJECT under an ANIM_XFORM + ANIM_SCALE of its own: corners in its axes (game's)
                    Wm = mul(inv(C0), inv(self.Cr))
                elif abs(ngpanim.det3(W0[ni])) < 1e-12 or abs(ngpanim.det3(W_idle(ni))) < 1e-12:
                    hidden_rigid.append(nd.get("name"))
                    continue
                else:
                    # idle's first frame, moved with the holder back to its rest
                    Wm = W_idle(ni)
                    if holder is not None:
                        Wm = mul(mul(W0[holder], inv(W_idle(holder))), Wm)
            pts = []
            live_prims = []
            for prim in mesh.get("primitives", []):
                if prim.get("mode", 4) != 4:
                    raise RigError("mesh %s: primitive mode %d (triangles only)" % (nd.get("name"), prim["mode"]))
                at = prim["attributes"]
                pos = glb.accessor(at["POSITION"])
                nrm = glb.accessor(at["NORMAL"]) if "NORMAL" in at else None
                uv = glb.accessor(at["TEXCOORD_0"]) if "TEXCOORD_0" in at else None
                col = glb.accessor(at["COLOR_0"]) if "COLOR_0" in at else None
                sets = []
                for s in range(4):
                    if skin is not None and "JOINTS_%d" % s in at and "WEIGHTS_%d" % s in at:
                        sets.append((glb.accessor(at["JOINTS_%d" % s]), glb.accessor(at["WEIGHTS_%d" % s])))
                if skin is not None and not sets:
                    raise RigError("mesh %s: skin without JOINTS_0/WEIGHTS_0" % nd.get("name"))
                idx = [i[0] for i in glb.accessor(prim["indices"])] if "indices" in prim else list(range(len(pos)))
                corners = {}

                def corner(v):
                    if v in corners:
                        return corners[v]
                    if skin is not None:
                        acc = {}
                        for js, ws in sets:
                            for q, w in zip(js[v], ws[v]):
                                if w > 0.0 and q < len(skin["joints"]):
                                    acc[q] = acc.get(q, 0.0) + w
                        tot = sum(acc.values())
                        if tot <= 0.0:
                            raise RigError("mesh %s: vertex %d has no weights" % (nd.get("name"), v))
                        p = [0.0, 0.0, 0.0]
                        n = [0.0, 0.0, 0.0]
                        for q, w in acc.items():
                            pp = col_apply(M[q], pos[v])
                            p = [a + w / tot * b for a, b in zip(p, pp)]
                            if nrm:
                                nn = col_apply(M[q], nrm[v], 0.0)
                                n = [a + w / tot * b for a, b in zip(n, nn)]
                        wts = {}
                        for q, w in acc.items():
                            k = skin["joints"][q]
                            wts[k] = wts.get(k, 0.0) + w / tot
                        wts = sorted(wts.items(), key=lambda kv: (-kv[1], kv[0]))[:3]
                        t3 = sum(w for _k, w in wts)
                        wts = tuple((k, w / t3) for k, w in wts)
                    else:
                        p = col_apply(Wm, pos[v])
                        n = col_apply(Wm, nrm[v], 0.0) if nrm else [0.0, 1.0, 0.0]
                        wts = ((holder, 1.0),)
                    pg = col_apply(C0, p)
                    ng = col_apply(C0, n, 0.0) if nrm else [0.0, 0.0, 1.0]
                    ln = _norm(ng) or 1.0
                    n8 = tuple(max(-127, min(127, int(round(c / ln * 127)))) for c in ng)
                    t = uv[v] if uv else (0.0, 0.0)
                    uv16 = tuple(max(-32768, min(32767, int(round(c * 4096)))) for c in t)
                    if col:
                        c = tuple(col[v]) + ((1.0,) if len(col[v]) == 3 else ())
                        rgba = tuple(max(0, min(255, int(round(x * 128)))) for x in c)
                    else:
                        rgba = (0x80, 0x80, 0x80, 0x80)
                    corners[v] = [pg, n8, uv16, rgba, wts]
                    return corners[v]
                tris = []
                for k in range(0, len(idx) - 2, 3):
                    tri = [corner(idx[k]), corner(idx[k + 1]), corner(idx[k + 2])]
                    tris.append(tri)
                    pts += [c[0] for c in tri]
                if not is_coll and tris:
                    (live_prims if is_live else prims).append((self._mat_key(mats, prim), tris))
            if is_coll and pts:
                coll_pts.append(pts)
            if live_prims:
                self.rigid.append({"node": ni, "holder": holder, "name": nd.get("name") or "node%d" % ni,
                                   "prims": live_prims})
        if hidden_rigid:
            log("warning: rigid meshes scaled to 0 at rest and in every action, left out: %s" % ", ".join(map(str, hidden_rigid)))
        if not prims:
            raise RigError("no mesh to draw in the .glb")

        # ---- size: the base's height unless the .glb is in game units or --scale/--height says otherwise
        allz = [c[0][2] for _k, tris in prims for t in tris for c in t]
        h = max(allz) - min(allz)
        if h <= 1e-6:
            raise RigError("the mesh is flat (no height)")
        if scale:
            s = scale
        elif height:
            s = height / h
        elif game_units:
            s = 1.0
        else:
            s = base.height / h
        self.scale = s
        lift = -min(allz) * s if not game_units else 0.0   # feet on the ground (z 0)
        if game_units and abs(min(allz)) > 0.1 * h:
            log("warning: the rest mesh goes from z %.1f to %.1f; the game puts z 0 on the ground" % (min(allz), max(allz)))
        S = [[s, 0.0, 0.0, 0.0], [0.0, s, 0.0, 0.0], [0.0, 0.0, s, lift], [0.0, 0.0, 0.0, 1.0]]
        self.C = mul(S, C0)
        for _k, tris in prims:
            for t in tris:
                for c in t:
                    if len(c) == 5:
                        c[0] = [c[0][0] * s, c[0][1] * s, c[0][2] * s + lift]
                        c.append(True)   # scaled (corners are shared between triangles)
        coll_pts = [[[p[0] * s, p[1] * s, p[2] * s + lift] for p in pts] for pts in coll_pts]
        self.height = h * s
        log("rig %s: %s, facing %s, scale %.4g, height %.1f" % (
            os.path.basename(path), "game units (WotM root)" if game_units else "Blender scene", self.front, s, self.height))

        # ---- locators
        locs = {}
        # empties first: a bone named like an alias ("head", "chest") is a locator only when no empty takes that id
        for i in sorted(range(len(nodes)), key=lambda i: i in jset):
            nd = nodes[i]
            lid = _locator_id(nd.get("name"))
            if lid is None or "mesh" in nd:
                continue
            if lid in locs:
                if not (i in jset and locs[lid] not in jset):
                    log("warning: locator %#x named twice (%s); the first kept" % (lid, nd.get("name")))
                continue
            locs[lid] = i

        # ---- joints kept: weighted, holders (rigid meshes, locators) and their ancestors
        jpar = {k: joint_above(k) for k in jset}
        need = set()
        for _k, tris in prims:
            for t in tris:
                for c in t:
                    for k, _w in c[4]:
                        if k is not None:
                            need.add(k)
        for r in self.rigid:
            need.add(r["holder"])
        loc_holder = {}
        for lid, i in locs.items():
            hj = steady_joint(i)
            loc_holder[lid] = hj
            if hj is not None:
                need.add(hj)
        kept = set()
        for k in need:
            while k is not None and k not in kept:
                kept.add(k)
                k = jpar[k]
        if not kept:
            raise RigError("no joint carries weights")
        kids = {}
        for k in kept:
            kids.setdefault(jpar[k], []).append(k)

        def count(k):
            return 1 + sum(count(c) for c in kids.get(k, []))
        roots = sorted(kids.get(None, []))
        main = max(roots, key=count)
        for _k, tris in prims:
            for t in tris:
                for c in t:
                    if any(k is None for k, _w in c[4]):
                        c[4] = tuple((main if k is None else k, w) for k, w in c[4])
        for lid in loc_holder:
            if loc_holder[lid] is None:
                loc_holder[lid] = main
        # depth-first order, main root first
        order = []

        def dfs(k):
            order.append(k)
            for c in sorted(kids.get(k, [])):
                dfs(c)
        for r in [main] + [r for r in roots if r != main]:
            dfs(r)
        self.joints = []
        jx = {}
        for k in order:
            jt = Joint(k, nodes[k].get("name") or "node%d" % k)
            jt.idx = len(self.joints)
            jx[k] = jt.idx
            self.joints.append(jt)
        for jt in self.joints:
            p = jpar[jt.node]
            jt.parent = jx[p] if p is not None else None
            if jt.parent is not None:
                self.joints[jt.parent].kids.append(jt.idx)
        self.jx = jx
        self.scaled = set()   # joints some action scales (pose(); the skin cannot follow)
        for jt in self.joints:
            G = self.game_row(W0[jt.node])
            R = orth(G)
            if R is None:
                raise RigError("joint %s is scaled to 0 at rest" % jt.name)
            jt.rest = R
            jt.det0 = abs(ngpanim.det3(G)) / s ** 3
        for jt in self.joints:
            P = self.joints[jt.parent].rest if jt.parent is not None else eye()
            jt.local = mul(jt.rest, inv(P))
            jt.pre = rot_only(jt.local)
            jt.t0 = list(jt.local[3][:3])
        self.main = 0
        self.prims = [(key, [[(c[0], c[1], c[2], c[3], tuple((jx[k], w) for k, w in c[4])) for c in t] for t in tris])
                      for key, tris in prims]
        # locators: (joint, local row matrix)
        self.locs = {}
        for lid, i in locs.items():
            hj = jx[loc_holder[lid]]
            G = self.game_row(W0[i])
            R = orth(G) or [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0], G[3][:3] + [1.0]]
            own = max(abs(R[r][c] - (1.0 if r == c else 0.0)) for r in range(3) for c in range(3)) > 1e-3
            if not own and lid in base.locs:
                B = base.locs[lid]
                R = [B[0][:3] + [0.0], B[1][:3] + [0.0], B[2][:3] + [0.0], R[3]]
            self.locs[lid] = (hj, mul(R, inv(self.joints[hj].rest)))
        self.coll_pts = coll_pts
        for r in self.rigid:
            r["holder"] = jx[r["holder"]]
            r["tris"] = [(key, [[(c[0], c[1], c[2], c[3]) for c in t] for t in tris]) for key, tris in r["prims"]]
            del r["prims"]
            # at rest: as the idle's first frame shows it (the kit's bind pose drops the scale of the scale nodes)
            r["rest"] = self.live_local(r, idle, 0.0) if idle is not None else self.live_local(r, None, 0.0)
        if self.rigid:
            log("rigid parts with an animated scale or offset, each with its own OBJECT: %s" % ", ".join(
                "%s (on %s)" % (r["name"], self.joints[r["holder"]].name) for r in self.rigid))
        self.clips = {}
        for nm, clip in ed.clips.items():
            aid, loop = _clip_id(nm)
            if aid is None:
                continue
            if aid > ngpanim.MAX_ID:
                log("warning: %s: animation ids go up to %d; skipped" % (nm, ngpanim.MAX_ID))
                continue
            if aid in self.clips:
                log("warning: %s: id %d already given by %s; skipped" % (nm, aid, self.clips[aid][0]))
                continue
            self.clips[aid] = (nm, clip, loop)
        log("skeleton: %d joints (%d roots, main %s), %d locators, %d actions" % (
            len(self.joints), len(roots), self.joints[0].name, len(self.locs), len(self.clips)))

    def game_row(self, Wcol):
        """A node's world in the game's axes, row form (the rotation part keeps the .glb's scale: orth() drops it)."""
        return tr(mul(mul(self.C, Wcol), self.Cr))

    def _mat_key(self, mats, prim):
        if "material" not in prim or prim["material"] >= len(mats):
            return ("color", (204, 204, 204, 255))
        m = mats[prim["material"]]
        pbr = m.get("pbrMetallicRoughness", {})
        t = pbr.get("baseColorTexture")
        img = None
        if t is not None:
            img = self.ed.glb.j.get("textures", [])[t["index"]].get("source")
        mm = re.match(r"tex(\d+)", m.get("name") or "")
        if mm:
            return ("tex", int(mm.group(1)), img)
        if img is not None:
            return ("img", img)
        f = pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
        return ("color", tuple(max(0, min(255, int(round(c * 255)))) for c in f))

    def image_bytes(self, k):
        """(PNG bytes or None, name) of image k of the .glb."""
        im = self.images[k]
        nm = im.get("name") or "image%d" % k
        data = None
        if "bufferView" in im:
            v = self.ed.glb.j["bufferViews"][im["bufferView"]]
            o = v.get("byteOffset", 0)
            data = self.ed.glb.bin[o:o + v["byteLength"]]
        elif "uri" in im:
            u = im["uri"]
            if u.startswith("data:"):
                data = base64.b64decode(u.split(",", 1)[1])
            else:
                p = os.path.join(os.path.dirname(self.path), u.replace("%20", " "))
                if os.path.exists(p):
                    data = open(p, "rb").read()
        if data is not None and data[:8] != b"\x89PNG\r\n\x1a\n":
            return None, nm
        return data, nm

    def pose(self, clip, t):
        """Game world (row form, rotation only) of every joint at time t of the clip; a joint scaled to 0 keeps its rest."""
        cache = {}
        out = []
        s3 = self.scale ** 3
        for jt in self.joints:
            G = self.game_row(self.ed.world(jt.node, clip, t, cache))
            if abs(abs(ngpanim.det3(G)) / s3 - jt.det0) > 0.05 * jt.det0:
                self.scaled.add(jt.name)
            R = orth(G)
            out.append(R if R is not None else jt.rest)
        return out

    def live_local(self, r, clip, t, cache=None):
        """A live rigid mesh's local matrix (row form, with its scale) against its joint's game pose at time t."""
        cache = {} if cache is None else cache
        G = self.game_row(self.ed.world(r["node"], clip, t, cache))
        H = orth(self.game_row(self.ed.world(self.joints[r["holder"]].node, clip, t, cache)))
        return mul(G, inv(H if H is not None else self.joints[r["holder"]].rest))


# ---------------------------------------------------------------- palette blocks and objects

def partition(rig, log):
    """Triangles in blocks of at most MAX_SLOTS joints: [(joints in slot order, [(material key, triangle)])]."""
    tris = [(key, t) for key, ts in rig.prims for t in ts]
    used = sorted({j for _k, t in tris for c in t for j, _w in c[4]})
    if len(used) <= MAX_SLOTS:
        return [(used, tris)]
    blocks = []
    order = sorted(tris, key=lambda kt: min(j for c in kt[1] for j, w in c[4] if w == max(ww for _jj, ww in c[4])))
    cur, cur_j = [], set()
    for kt in order:
        js = {j for c in kt[1] for j, _w in c[4]}
        if len(cur_j | js) > MAX_SLOTS and cur:
            blocks.append((sorted(cur_j), cur))
            cur, cur_j = [], set()
        cur.append(kt)
        cur_j |= js
    if cur:
        blocks.append((sorted(cur_j), cur))
    log("mesh in %d palette blocks of %s joints" % (len(blocks), "/".join(str(len(b[0])) for b in blocks)))
    return blocks


# ---------------------------------------------------------------- writing

class Out:
    def __init__(self, mb):
        self.b = mb
        self.w = ngpanim.Writer(mb)

    def alloc(self, size, align=16):
        return self.w.alloc(size, align)

    def u32(self, a, v):
        struct.pack_into("<I", self.b.d, a - BASE, v & 0xFFFFFFFF)

    def u16(self, a, v):
        struct.pack_into("<H", self.b.d, a - BASE, v & 0xFFFF)

    def s16(self, a, v):
        struct.pack_into("<h", self.b.d, a - BASE, v)

    def f(self, a, v):
        struct.pack_into("<f", self.b.d, a - BASE, v)

    def mat(self, a, m):
        struct.pack_into("<16f", self.b.d, a - BASE, *[m[r][c] for r in range(4) for c in range(4)])

    def ptr(self, a, target):
        self.u32(a, target)
        self.b.ptr[0].append(a - BASE)

    def raw(self, a, data):
        self.b.d[a - BASE:a - BASE + len(data)] = data


def sphere(pts):
    lo = [min(p[c] for p in pts) for c in range(3)]
    hi = [max(p[c] for p in pts) for c in range(3)]
    ctr = [0.5 * (a + b) for a, b in zip(lo, hi)]
    return ctr, max(math.dist(ctr, p) for p in pts)


def build(base_path, glb_path, out_dir, name=None, front=None, scale=None, height=None, fill=True, layer_ids=(), log=print):
    base = BaseInfo(base_path, log)
    rig = Rig(glb_path, base, front, scale, height, log)
    name = name or os.path.splitext(os.path.basename(base_path))[0]
    mb = base.mb
    o = Out(mb)
    img = base.game.img
    J = rig.joints
    nj = len(J)

    # ---- outputs: one per (joint, block); the first block of a joint is its own SKEL_BONE, the others a copy
    blocks = partition(rig, log)
    outs = []                      # (joint, block, slot)
    slot_of = []                   # per block: {joint: slot}
    for b, (js, _tris) in enumerate(blocks):
        slot_of.append({j: s for s, j in enumerate(js)})
        for s, j in enumerate(js):
            outs.append((j, b, s))
            if J[j].out < 0:
                J[j].out = len(outs) - 1
            else:
                J[j].dups.append(len(outs) - 1)
    if len(outs) > MAX_OUTPUTS:
        log("warning: %d bone outputs; the native viewer and tools/pose.py follow the first %d" % (len(outs), MAX_OUTPUTS))

    # ---- textures: key -> texId; new ones copy the body's packet
    tex_dat, tex_info = tex.read_file(tex.sibling(base_path, ".TEX"))
    tp = tex.parse(tex_dat)
    top = max(p.texId for p in tp["packets"])
    key_tex = {}
    new_tex = {}                   # texId -> (PNG bytes, what)
    for key, _ts in rig.prims + [kt for r in rig.rigid for kt in r["tris"]]:
        if key in key_tex:
            continue
        if key[0] == "tex" and key[1] in base.tex_ctx:
            key_tex[key] = key[1]
            continue
        k2 = key
        if key[0] == "tex":
            if key[2] is None:
                log("warning: material tex%d is not one of the base's textures and has no image; drawn with the body's" % key[1])
                key_tex[key] = base.body_tex
                continue
            k2 = ("img", key[2])
        if k2[0] == "img":
            png, what = rig.image_bytes(k2[1])
            if png is None:
                log("warning: image %s is not a PNG inside or next to the .glb; drawn white" % what)
                png = tex.png_bytes(4, 4, [(255, 255, 255, 255)] * 16)
        else:
            what = "color %02x%02x%02x" % k2[1][:3]
            png = tex.png_bytes(4, 4, [tuple(k2[1][:3]) + (255,)] * 16)
        same = next((t for t, (p, _w) in new_tex.items() if p == png), None)
        if same is None:
            top += 1
            same = top
            new_tex[same] = (png, what)
        key_tex[key] = same
    tex_ctx = dict(base.tex_ctx)
    for t in new_tex:
        tex_ctx[t] = base.tex_ctx[base.body_tex]

    # ---- OBJECTs: per block, up to MAX_TEX textures each, every texture in its own sub-packets
    objects = []                   # (block, [texIds], {slot: [encoded triangles]})
    for b, (js, tris) in enumerate(blocks):
        by_tex = {}
        for key, t in tris:
            by_tex.setdefault(key_tex[key], []).append(t)
        tids = sorted(by_tex)
        for c in range(0, len(tids), MAX_TEX):
            chunk = tids[c:c + MAX_TEX]
            by_slot = {}
            for s, t in enumerate(chunk):
                enc = []
                for tri in by_tex[t]:
                    e = tuple((ngpmesh.encode_skinned(cn[0], [(slot_of[b][j], w) for j, w in cn[4]], len(js)), cn[1], cn[2],
                               cn[3]) for cn in tri)
                    if e[0][0][:3] == e[1][0][:3] or e[1][0][:3] == e[2][0][:3] or e[0][0][:3] == e[2][0][:3]:
                        continue
                    enc.append(e)
                if enc:
                    by_slot[s] = enc
            objects.append((b, chunk, by_slot))

    # ---- channels: 6 per joint, 9 per live rigid part (its ANIM_XFORM and ANIM_SCALE), then the contacts and AnimPappy
    RG = rig.rigid
    nch = 6 * nj + 9 * len(RG)
    contact_ch = {}
    for lid in CONTACT_IDS:
        contact_ch[lid] = nch
        nch += 1
    pappy_ch = {}
    for pid in PAPPY_IDS:
        pappy_ch[pid] = nch
        nch += 1
    if nch > 600:
        log("warning: %d channels; nested blends take %d bytes each from the 16 KB animation stack" % (nch, 4 * nch + nch // 8 + 1))
    angular = set()
    for jt in J:
        angular |= {6 * jt.idx, 6 * jt.idx + 1, 6 * jt.idx + 2}
    angular |= {pappy_ch[1], pappy_ch[2], pappy_ch[3]}
    xf = []
    for jt in J:
        b0 = 6 * jt.idx
        xf.append({"bits": 1, "pre": jt.pre, "post": eye(), "ridx": [b0, b0 + 1, b0 + 2], "r0": [0.0, 0.0, 0.0],
                   "tidx": [b0 + 3, b0 + 4, b0 + 5], "t0": jt.t0})
    for k, r in enumerate(RG):
        b0 = 6 * nj + 9 * k
        angular |= {b0, b0 + 1, b0 + 2}
        s0, R0 = split_scale(r["rest"])
        if R0 is None:   # hidden at rest: the axes of the joint
            R0 = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0], r["rest"][3][:3] + [1.0]]
        r["s0"], r["local0"] = s0, R0
        r["sidx"] = [b0 + 6, b0 + 7, b0 + 8]
        xf.append({"bits": 1, "pre": rot_only(R0), "post": eye(), "ridx": [b0, b0 + 1, b0 + 2], "r0": [0.0, 0.0, 0.0],
                   "tidx": [b0 + 3, b0 + 4, b0 + 5], "t0": list(R0[3][:3])})

    # ---- locators: the rig's plus the base's it lacks (scaled, on the nearest joint)
    locs = dict(rig.locs)
    auto = []
    k_h = rig.height / base.height if base.height > 0 else 1.0
    for lid, B in sorted(base.locs.items()):
        if lid in locs:
            continue
        p = [c * k_h for c in B[3][:3]]
        hj = min(range(nj), key=lambda q: math.dist(J[q].rest[3][:3], p))
        R = [B[0][:3] + [0.0], B[1][:3] + [0.0], B[2][:3] + [0.0], p + [1.0]]
        locs[lid] = (hj, mul(R, inv(J[hj].rest)))
        auto.append("%x (%s)" % (lid, J[hj].name))
    if auto:
        log("warning: locators placed where the base has them: %s" % ", ".join(auto))
    contact_locs = {lid: locs[lid] for lid in CONTACT_IDS if lid in locs}
    feet_z = [mul(L, J[h].rest)[3][2] for lid, (h, L) in contact_locs.items() if lid in FEET]
    ground = max(feet_z) if feet_z else 0.0
    thr = {lid: min(mul(L, J[h].rest)[3][2], ground) + 0.04 * rig.height for lid, (h, L) in contact_locs.items()}

    # ---- collision boxes, in the model's space (under a ROTATE that undoes the main root's rest)
    boxes = []
    for pts in rig.coll_pts:
        boxes.append(([min(p[c] for p in pts) for c in range(3)], [max(p[c] for p in pts) for c in range(3)]))
    if not boxes:
        pts = [c[0] for _k, ts in rig.prims for t in ts for c in t]
        lo = [percentile([p[c] for p in pts], 0.1) for c in range(2)] + [0.0]
        hi = [percentile([p[c] for p in pts], 0.9) for c in range(2)] + [percentile([p[2] for p in pts], 0.99)]
        boxes.append((lo, hi))

    # ---- animations: (id, frames, {channel: values}, loop, header, ctx)
    anims = []
    layers = []
    layer_rot = angular - {pappy_ch[1], pappy_ch[2], pappy_ch[3]}
    worst_all = []
    steps = []   # median spacing of each action's keys
    ids = sorted(set(rig.clips) | (set(base.anims) if fill else set()))
    for aid in ids:
        ba = base.anims.get(aid)
        if aid in rig.clips:
            nm, clip, loop = rig.clips[aid]
            ts = rig.ed.key_times(clip, range(len(rig.ed.nodes)))
            if len(ts) > 2:
                steps.append(sorted(b - a for a, b in zip(ts, ts[1:]))[(len(ts) - 1) // 2])
            frames = int(round(ts[-1] * FPS)) + 1 if ts else 1
            if frames < 2:
                frames = ba["frames"] if ba else 2
        else:
            nm, clip, loop = None, None, None
            frames = ba["frames"]
        if loop is None:
            loop = bool(ba["loop"]) if ba else aid in ngpanim.LOOPING_IDS
        if aid in layer_ids:   # the special scrubs or runs it once by percent
            loop = False
        vals = {}
        prev = [[0.0, 0.0, 0.0] for _ in xf]
        for r in RG:
            r.pop("last", None)
        worst = [0.0, 0.0, None, 0]
        lz = {lid: [] for lid in contact_locs}
        for f in range(frames if clip is not None else 1):
            W = rig.pose(clip, f / FPS) if clip is not None else [jt.rest for jt in J]
            for k, r in enumerate(RG):
                Lm = rig.live_local(r, clip, f / FPS) if clip is not None else r["rest"]
                s, R = split_scale(Lm)
                if R is None:   # scaled to 0 on some axis: hidden, the rest of the transform held from the last frame
                    R = r.get("last", r["local0"])
                r["last"] = R
                q = nj + k
                rr, t, L = ngpanim.solve_xform(xf[q], R, prev[q])
                prev[q] = rr
                b0 = 6 * nj + 9 * k
                for c in range(3):
                    vals.setdefault(b0 + c, []).append(-rr[c])
                    vals.setdefault(b0 + 3 + c, []).append(t[c])
                    vals.setdefault(b0 + 6 + c, []).append(s[c])
                dr, dp = ngpanim.mat_diff(L, R)
                if dr > worst[0] or dp > worst[1]:
                    worst = [max(dr, worst[0]), max(dp, worst[1]), r["name"], f]
            for jt in J:
                P = W[jt.parent] if jt.parent is not None else eye()
                Lt = mul(W[jt.idx], inv(P))
                r, t, L = ngpanim.solve_xform(xf[jt.idx], Lt, prev[jt.idx])
                prev[jt.idx] = r
                b0 = 6 * jt.idx
                for q in range(3):
                    vals.setdefault(b0 + q, []).append(-r[q])
                    vals.setdefault(b0 + 3 + q, []).append(t[q])
                dr, dp = ngpanim.mat_diff(L, Lt)
                if dr > worst[0] or dp > worst[1]:
                    worst = [max(dr, worst[0]), max(dp, worst[1]), jt.name, f]
            for lid, (h, L) in contact_locs.items():
                lz[lid].append(mul(L, W[h])[3][2])
        if clip is None:
            vals = {c: v * frames for c, v in vals.items()}
            for lid in lz:
                lz[lid] = lz[lid] * frames
        for lid in CONTACT_IDS:
            if lid in lz:
                vals[contact_ch[lid]] = [1.0 if z <= thr[lid] else 0.0 for z in lz[lid]]
            else:
                vals[contact_ch[lid]] = [0.0] * frames
        for pid in PAPPY_IDS:
            v = base.curve_at(aid, pid, frames)
            vals[pappy_ch[pid]] = v if v is not None else [0.0] * frames
        own_layer = aid in layer_ids or (ba is None and aid in ngpanim.LAYER_IDS)
        if own_layer and clip is not None:
            # a layer the base lacks (or has as a plain animation; `layers`, from a borrowed special): authored over the
            # pose it is added to, the first frame is that pose and the game adds the difference from it
            vals = {c: [x - v[0] for x in v] if c in layer_rot else v for c, v in vals.items()}
        if own_layer or (ba is not None and ba["layer"]):
            # added to the main animation: only the rotations that leave the rest (rest angles are 0), as deltas
            vals = {c: v for c, v in vals.items() if c in layer_rot and max(abs(x) for x in v) > 1e-3}
            if not vals:
                vals = {0: [0.0] * frames}
            layers.append(aid)
        anims.append((aid, frames, vals, loop, ba))
        if clip is not None and (worst[0] > 1e-3 or worst[1] > 1e-3):
            worst_all.append("%s %.2g/%.2g at %s, frame %d" % (nm, worst[0], worst[1], worst[2], worst[3]))
        if clip is not None:
            log("%s: animation %d, %d frames%s" % (nm, aid, frames, ", looping" if loop else ""))
    if worst_all:
        log("warning: channels off the .glb (rotation / units): %s" % "; ".join(worst_all))
    if rig.game_units and steps and sorted(steps)[len(steps) // 2] > 1.5 / FPS:
        log("warning: the actions have a key every %.3f s; a .glb from the kit keeps one per game frame (1/60 s) only when "
            "Blender's scene is at 60 fps before the import (fast moves lose detail)" % sorted(steps)[len(steps) // 2])
    skin_scaled = sorted(rig.scaled & {J[j].name for js, _t in blocks for j in js})
    if skin_scaled:
        log("warning: actions scale joints the skin follows; the game's skeleton has no scale, so the skin keeps its "
            "size: %s" % ", ".join(skin_scaled))
    if layers:
        log("%d layers the game adds on top of the main animation (rotations only, from the rest): %s" % (
            len(layers), " ".join(str(a) for a in layers)))
    filled = [a[0] for a in anims if a[0] not in rig.clips]
    if filled:
        log("%d animations of the base held at the rest pose (no action in the .glb): %s" % (
            len(filled), " ".join(str(a) for a in filled)))
    missing = [a for a in (90, 120) if a not in rig.clips]
    if missing:
        log("warning: no action for %s (idle, death); %s" % (
            ", ".join(map(str, missing)), "held at the rest pose" if fill else "the game needs them"))
    if not fill and 90 not in rig.clips:
        raise RigError("no a90 (idle): the game needs it")

    # ================================================================ the file
    # palette blocks: header + MAX_SLOTS identity matrices
    blk_addr = []
    for js, _t in blocks:
        a = o.alloc(0x10 + 0x40 * MAX_SLOTS, 16)
        n = len(js)
        o.u32(a + 8, 0x11000000)
        o.u32(a + 12, 0x6C000000 | ((4 * n) & 0xFF) << 16 | 0x358)
        for s in range(MAX_SLOTS):
            o.mat(a + 0x10 + 0x40 * s, eye())
        blk_addr.append(a)

    # OBJECTs with their sub-packets and chains
    tmpl = base.tmpl
    obj_addr = []
    all_pts = []
    for oi, (b, tids, by_slot) in enumerate(objects):
        pkts = []
        for slot, tris in sorted(by_slot.items()):
            first = base.gif_first if len(by_slot) == 1 and slot == 0 else ngpmesh._with_slot(base.gif_first, slot)
            pkts += ngpmesh.build_packets(tris, first, base.gif_rest)
        blobs = [vifpkt.encode(p) for p in pkts]
        offs, size = [], 0
        for blob in blobs:
            size = (size + 0x7F) & ~0x7F
            offs.append(size)
            size += len(blob)
        start = o.alloc(size, 0x80)
        for off, blob in zip(offs, blobs):
            o.raw(start + off, blob)
        n = len(blocks[b][0])
        chain = o.alloc(32 * len(blobs) + 16, 16)
        for k, (off, blob) in enumerate(zip(offs, blobs)):
            q = chain + 32 * k
            o.u32(q, 0x30000000 | (1 + 4 * n))
            o.ptr(q + 4, blk_addr[b])
            o.u32(q + 16, 0x30000000 | (len(blob) // 16))
            o.ptr(q + 20, start + off)
        o.u32(chain + 32 * len(blobs), 0x60000000)
        a = o.alloc(0x70 + 0x30 * max(1, len(tids)), 16)
        o.raw(a, bytes(mb.d[tmpl - BASE:tmpl + 0x70 - BASE]))
        o.ptr(a + 4, (mb.u32(tmpl + 4) & 0xF0000000) | start)
        nverts = sum(len(s.idx) for p in pkts for bb in p.batches for s in bb.sections)
        bits = mb.u32(tmpl + 0xC)
        o.u32(a + 0xC, (bits & ~(0x1FFF << 16)) | (min(nverts, 0x1FFF) << 16))
        pts = [tuple(struct.unpack("<f", struct.pack("<I", v[c]))[0] for c in range(3)) for p in pkts for bb in p.batches
               for v in bb.pos]
        all_pts += pts
        ctr, rad = sphere(pts)
        for c in range(3):
            o.f(a + 0x10 + 4 * c, ctr[c])
        o.f(a + 0x1C, rad)
        o.f(a + 8, rad * rad)
        for k in range(MAX_TEX):
            o.u16(a + 0x50 + 2 * k, tids[k] if k < len(tids) else 0)
        o.u16(a + 0x5A, len(tids))
        for k, t in enumerate(tids):
            mb.ptr[1].append(a + 0x50 + 2 * k - BASE)
            o.raw(a + 0x70 + 0x30 * k, tex_ctx[t])
            mb.ptr[2].append(a + 0x70 + 0x30 * k - BASE)
        o.u32(a + 0x64, 0)
        o.u32(a + 0x68, oi)
        obj_addr.append((a, chain))
        log("object %d: block %d, textures %s, %d triangles, %d sub-packets" % (
            oi, b, ",".join(map(str, tids)), sum(len(v) for v in by_slot.values()), len(pkts)))

    # live rigid parts: one rigid OBJECT per texture (polyPkt = the packet with its DMA tag; no animPktPtr entry)
    for r in RG:
        r["objs"] = []
        by_tex = {}
        for key, ts in r["tris"]:
            by_tex.setdefault(key_tex[key], []).extend(ts)
        for t, ts in sorted(by_tex.items()):
            a = rigid_object(o, mb, tmpl, base, t, tex_ctx[t], ts)
            if a is not None:
                r["objs"].append(a)
        log("rigid part %s: %d objects, %d triangles" % (r["name"], len(r["objs"]), sum(len(v) for v in by_tex.values())))

    # animation character: map, angular bits, animations, control nodes
    ids_map = sorted([(lid, contact_ch[lid]) for lid in CONTACT_IDS] + [(pid, pappy_ch[pid]) for pid in PAPPY_IDS])
    mp = o.alloc(4 + 4 * len(ids_map), 4)
    o.u32(mp, len(ids_map))
    for i, (cid, c) in enumerate(ids_map):
        o.u16(mp + 4 + 4 * i, cid)
        o.u16(mp + 6 + 4 * i, c)
    na = len(anims)
    ch = o.alloc(0xC + 4 * na, 16)
    o.u16(ch, na)
    o.u16(ch + 2, nch)
    o.u16(ch + 4, len(xf))
    o.ptr(ch + 8, mp)
    abits = [0] * ((nch + 7) // 8)
    for c in angular:
        abits[c // 8] |= 1 << (c % 8)
    if nch <= 16:
        o.u16(ch + 6, abits[0] | (abits[1] << 8 if len(abits) > 1 else 0))
    else:
        ba = o.alloc(len(abits), 4)
        o.raw(ba, bytes(abits))
        o.s16(ch + 6, ba - ch)
    actx = o.alloc(60 * na, 4)
    idle_ctx = base.anims[90]["ctx"]
    for k, (aid, frames, vals, loop, ba_) in enumerate(anims):
        curves = []
        for c in sorted(vals):
            v = vals[c]
            if c in contact_ch.values():
                keys = [(0.0, v[0])] + [(float(f), v[f]) for f in range(1, len(v)) if v[f] != v[f - 1]]
                curves.append(o.w.static(c, v[0]) if len(keys) == 1 else o.w.stepped(c, keys))
            else:
                curves.append(o.w.curve(c, 0.0, v))
        a = o.alloc(0x18 + 4 * len(curves), 16)
        head = ba_["head"] if ba_ else struct.pack("<IffHHHhhH", 0xC, 0.0, 0.0, 0, 0, 0, -1, -1, 0)
        o.raw(a, head)
        o.u32(a, (aid << 18) | (struct.unpack_from("<I", head, 0)[0] & 0x3FFFF))
        o.f(a + 4, 0.0)
        o.f(a + 8, float(frames - 1))
        o.u16(a + 0xC, len(curves))
        o.u16(a + 0xE, 0)
        o.u16(a + 0x10, k)
        for i, c in enumerate(curves):
            o.ptr(a + 0x18 + 4 * i, c)
        o.ptr(ch + 0xC + 4 * k, a)
        e = actx + 60 * k
        o.raw(e, ba_["ctx"] if ba_ else idle_ctx)
        pk = o.alloc(max(4, 2 * len(curves)), 4)
        o.ptr(e + 4, a)
        o.ptr(e + 0x30, pk)
        mb.d[e + 0x28 - BASE] = 0
        mb.d[e + 0x29 - BASE] = 1 if loop else 0
        o.u16(e + 0x2A, 0)

    # instance state: channel values, dirty bits, atMat (rest locals)
    val = o.alloc(4 * nch, 4)
    for q, x in enumerate(xf):
        for c in range(3):
            o.f(val + 4 * x["tidx"][c], x["t0"][c])
    for r in RG:
        for c in range(3):
            o.f(val + 4 * r["sidx"][c], r["s0"][c])
    dirty = o.alloc(nch // 8 + 1, 4)
    atm = o.alloc(64 * len(xf), 16)
    for jt in J:
        o.mat(atm + 64 * jt.idx, jt.local)
    for k, r in enumerate(RG):
        o.mat(atm + 64 * (nj + k), r["local0"])
    amp = o.alloc(4 * len(outs), 4)
    for i, (_j, b, s) in enumerate(outs):
        o.ptr(amp + 4 * i, blk_addr[b] + 0x10 + 0x40 * s)
    apk = o.alloc(4 * len(obj_addr), 4)
    for i, (_a, chain) in enumerate(obj_addr):
        o.ptr(apk + 4 * i, chain)

    # the node tree
    def node_xf(jt):
        kid_specs = []
        for d in jt.dups:
            kid_specs.append(("dup", d))
        for lid, (h, L) in sorted(locs.items()):
            if h == jt.idx:
                kid_specs.append(("loc", lid, L))
        if jt.idx == rig.main:
            kid_specs.append(("coll",))
        for k, r in enumerate(RG):
            if r["holder"] == jt.idx and r["objs"]:
                kid_specs.append(("rigid", k))
        for c in jt.kids:
            kid_specs.append(("joint", c))
        return xf_node(jt.idx, jt.idx == rig.main, kid_specs)

    def xf_node(q, is_main, kid_specs):
        """ANIM_XFORM q (channels and defaults from xf[q], atMat q) with its children."""
        a = o.alloc(0xD0 + 4 * len(kid_specs), 16)
        x = xf[q]
        o.u32(a, 17)
        o.u32(a + 4, len(kid_specs))
        o.f(a + 0xC, 1.0)
        o.s16(a + 0x10, -1)
        for c in range(3):
            o.s16(a + 0x12 + 2 * c, x["ridx"][c])
            o.s16(a + 0x18 + 2 * c, x["tidx"][c])
        o.s16(a + 0x1E, x["ridx"][0])
        o.f(a + 0x2C, 1.0)
        for c in range(3):
            o.f(a + 0x30 + 4 * c, x["t0"][c])
        o.f(a + 0x3C, 1.0)
        o.mat(a + 0x40, x["pre"])
        o.mat(a + 0x80, eye())
        mb.d[a + 0xC1 - BASE] = 0x3F
        o.u16(a + 0xC2, 1 | (4 if is_main else 0))
        o.u32(a + 0xC4, q)
        for k, spec in enumerate(kid_specs):
            o.ptr(a + 0xCC + 4 * k, emit(spec))
        return a

    def emit(spec):
        kind = spec[0]
        if kind == "joint":
            jt = J[spec[1]]
            a = o.alloc(0x50, 16)
            o.u32(a, 35)
            o.u32(a + 4, 1)
            o.s16(a + 8, jt.out)
            o.u16(a + 0xA, 1 if jt.parent is None else 0)
            o.mat(a + 0x10, inv(jt.rest))
            o.ptr(a + 0xC, node_xf(jt))
            return a
        if kind == "dup":
            j = outs[spec[1]][0]
            a = o.alloc(0x50, 16)
            o.u32(a, 35)
            o.u32(a + 4, 1)
            o.s16(a + 8, spec[1])
            o.u16(a + 0xA, 0)
            o.mat(a + 0x10, inv(J[j].rest))
            t = o.alloc(0x20, 16)
            o.u32(t, 3)
            o.ptr(a + 0xC, t)
            return a
        if kind == "rigid":
            return xf_node(nj + spec[1], False, [("scale", spec[1])])
        if kind == "scale":
            # ANIM_SCALE: L = T(a) . S(s) . T(t + b) with a = b = t = 0; all three scales hide the part at 0
            r = RG[spec[1]]
            a = o.alloc(0x64, 16)
            o.u32(a, 38)
            o.u32(a + 4, 1)
            o.f(a + 0xC, 1.0)
            o.s16(a + 0x10, -1)
            for c in range(3):
                o.s16(a + 0x12 + 2 * c, r["sidx"][c])
                o.s16(a + 0x18 + 2 * c, -1)
                o.f(a + 0x20 + 4 * c, r["s0"][c])
            o.f(a + 0x3C, 1.0)
            grp = o.alloc(0x20 + 4 * len(r["objs"]), 16)
            o.u32(grp, 1)
            o.u16(grp + 8, len(r["objs"]))
            o.u16(grp + 0xA, 1)
            ctr = [sum(ob[1][c] for ob in r["objs"]) / len(r["objs"]) for c in range(3)]
            rad = max(math.dist(ctr, ob[1]) + ob[2] for ob in r["objs"])
            o.f(grp + 0xC, rad * rad)
            for c in range(3):
                o.f(grp + 0x10 + 4 * c, ctr[c])
            o.f(grp + 0x1C, rad)
            for k, ob in enumerate(r["objs"]):
                o.ptr(grp + 0x20 + 4 * k, ob[0])
            o.ptr(a + 0x60, grp)
            return a
        if kind == "loc":
            r = o.alloc(0x60, 16)
            o.u32(r, 4)
            o.u32(r + 8, 1)
            o.mat(r + 0x10, spec[2])
            at = o.alloc(16, 16)
            o.u32(at, (spec[1] << 18) | 40)
            o.ptr(r + 0x50, at)
            return r
        if kind == "coll":
            r = o.alloc(0x50 + 4 * len(boxes), 16)
            o.u32(r, 4)
            o.u32(r + 8, len(boxes))
            o.mat(r + 0x10, inv(J[rig.main].rest))
            for k, (lo, hi) in enumerate(boxes):
                cv = o.alloc(0x20 + 16 * 6, 16)
                o.u32(cv, 10)
                o.s16(cv + 4, 6)
                o.s16(cv + 6, -1)
                o.u16(cv + 8, 1)
                ctr = [0.5 * (a_ + b_) for a_, b_ in zip(lo, hi)]
                for c in range(3):
                    o.f(cv + 0x10 + 4 * c, ctr[c])
                o.f(cv + 0x1C, math.dist(lo, hi) * 0.5)
                planes = [((0, 0, 1), hi[2]), ((0, 1, 0), hi[1]), ((0, 0, -1), -lo[2]), ((0, -1, 0), -lo[1]),
                          ((1, 0, 0), hi[0]), ((-1, 0, 0), -lo[0])]
                for q, (nrm, d) in enumerate(planes):
                    struct.pack_into("<4f", mb.d, cv + 0x20 + 16 * q - BASE, float(nrm[0]), float(nrm[1]), float(nrm[2]), d)
                o.ptr(r + 0x50 + 4 * k, cv)
            return r
        raise ValueError(kind)

    # CHAR_INSTANCE: [skeleton roots..., LOD]
    roots = [jt for jt in J if jt.parent is None]
    ci = o.alloc(0x2C + 4 * (len(roots) + 1), 16)
    o.u32(ci, mb.u32(base.ci))
    o.ptr(ci + 4, val)
    o.ptr(ci + 8, dirty)
    o.ptr(ci + 0xC, atm)
    o.ptr(ci + 0x10, actx)
    o.ptr(ci + 0x14, amp)
    o.ptr(ci + 0x18, apk)
    o.u32(ci + 0x1C, 0)
    o.ptr(ci + 0x20, ch)
    o.u32(ci + 0x24, 0)
    o.u32(ci + 0x28, len(roots) + 1)
    for k, jt in enumerate(roots):
        o.ptr(ci + 0x2C + 4 * k, emit(("joint", jt.idx)))
    ctr, rad = sphere(all_pts)
    grp = o.alloc(0x20 + 4 * len(obj_addr), 16)
    o.u32(grp, 1)
    o.u16(grp + 8, len(obj_addr))
    o.u16(grp + 0xA, 1)
    o.f(grp + 0xC, rad * rad)
    for c in range(3):
        o.f(grp + 0x10 + 4 * c, ctr[c])
    o.f(grp + 0x1C, rad)
    for k, (a, _c) in enumerate(obj_addr):
        o.ptr(grp + 0x20 + 4 * k, a)
    top_node = grp
    if base.lod is not None:
        lod = base.lod
        nl = min(mb.u32(lod + 4), 16)
        la = o.alloc(0x20 + 16 * nl, 16)
        o.raw(la, bytes(mb.d[lod - BASE:lod + 0x20 + 16 * nl - BASE]))
        if mb.u32(lod + 8):
            o.ptr(la + 8, mb.u32(lod + 8))
        for c in range(3):
            o.f(la + 0x10 + 4 * c, ctr[c])
        o.f(la + 0x1C, rad)
        for i in range(nl):
            o.ptr(la + 0x28 + 16 * i, grp)
        top_node = la
    o.ptr(ci + 0x2C + 4 * len(roots), top_node)

    # root 0 and the ANIM_MGR point at the new instance (the old one stays in the file, unused)
    r0 = img.roots()[0]
    swapped = 0
    for k in range(img.u16(r0 + 8)):
        if mb.u32(r0 + 0x20 + 4 * k) == base.ci:
            o.u32(r0 + 0x20 + 4 * k, ci)
            swapped += 1
    for r in img.roots():
        if img.u32(r) & 63 == 7:
            for k in range(img.u32(r + 4)):
                if mb.u32(r + 8 + 4 * k) == base.ci:
                    o.u32(r + 8 + 4 * k, ci)
                    swapped += 1
    if swapped < 2:
        raise RigError("the base's main instance is not a child of root 0 and in the ANIM_MGR (%d references)" % swapped)

    # files
    ngpmesh.write(mb, out_dir, name, textures=False)
    stem = os.path.join(out_dir, name)
    if new_tex:
        tex_out = add_textures(tex_dat, tp, base.body_tex, sorted(new_tex))
        with open(stem + ".TEX", "wb") as f:
            f.write(tex.zip_ie(tex_out, tex_info))
        for t, (png, what) in sorted(new_tex.items()):
            with open(os.path.join(out_dir, "%s_t%04d.png" % (name.lower(), t)), "wb") as f:
                f.write(png)
        log("textures: %s" % ", ".join("%d (%s)" % (t, w) for t, (_p, w) in sorted(new_tex.items())))
    else:
        shutil.copyfile(tex.sibling(base_path, ".TEX"), stem + ".TEX")
    rtx = tex.sibling(base_path, ".RTX")
    if os.path.exists(rtx):
        shutil.copyfile(rtx, stem + ".RTX")
    log("wrote %s.NGP (%d bytes), .PTR, .TEX, .RTX: %d joints, %d outputs, %d objects, %d animations, %d channels" % (
        stem, len(mb.d), nj, len(outs), len(obj_addr), na, nch))
    return {"joints": nj, "outputs": len(outs), "objects": len(obj_addr), "animations": na, "channels": nch,
            "textures": sorted(new_tex)}


def _fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def rigid_object(o, mb, tmpl, base, tex_id, ctx, tris):
    """A rigid OBJECT (one texture) for triangles of corners (position, normal, uv16, rgba) in its own axes:
    (address, sphere center, radius), None when every triangle is degenerate. The header is the body's OBJECT with
    no animPktPtr entry (+0x68 = -1); polyPkt points at the packet, which starts with its DMA RET tag."""
    enc = []
    for t in tris:
        e = tuple((tuple(_fbits(c) for c in cn[0]) + (_fbits(1.0),), cn[1], cn[2], cn[3]) for cn in t)
        if e[0][0] == e[1][0] or e[1][0] == e[2][0] or e[0][0] == e[2][0]:
            continue
        enc.append(e)
    if not enc:
        return None
    pkt = vifpkt.Packet(False)
    for k, sp in enumerate(ngpmesh.build_packets(enc, base.gif_first, base.gif_rest)):
        b = sp.batches[0]
        b.pos = [p[:3] for p in b.pos]
        b.mscal = None
        if k:   # the texture is loaded once, by the packet's first section
            g = b.sections[0].gif
            b.sections[0].gif = g[:3] + (g[3] & ~0x7FFF,)
        pkt.batches.append(b)
    blob = vifpkt.encode(pkt)
    start = o.alloc(len(blob), 0x80)
    o.raw(start, blob)
    a = o.alloc(0x70 + 0x30, 16)
    o.raw(a, bytes(mb.d[tmpl - BASE:tmpl + 0x70 - BASE]))
    o.ptr(a + 4, start)
    nverts = sum(len(s.idx) for b in pkt.batches for s in b.sections)
    bits = mb.u32(tmpl + 0xC)
    o.u32(a + 0xC, (bits & ~(0x1FFF << 16)) | (min(nverts, 0x1FFF) << 16))
    ctr, rad = sphere([c[0] for t in tris for c in t])
    for c in range(3):
        o.f(a + 0x10 + 4 * c, ctr[c])
    o.f(a + 0x1C, rad)
    o.f(a + 8, rad * rad)
    for k in range(MAX_TEX):
        o.u16(a + 0x50 + 2 * k, tex_id if k == 0 else 0)
    o.u16(a + 0x5A, 1)
    mb.ptr[1].append(a + 0x50 - BASE)
    o.raw(a + 0x70, ctx)
    mb.ptr[2].append(a + 0x70 - BASE)
    o.u32(a + 0x64, 0)
    o.u32(a + 0x68, 0xFFFFFFFF)
    return a, ctr, rad


def add_textures(data, tp, tmpl_id, new_ids):
    """The .TEX with a copy of packet `tmpl_id` for each new texId (same block size bin), chained after the last one."""
    pk = next(p for p in tp["packets"] if p.texId == tmpl_id)
    last = tp["packets"][-1]
    out = bytearray(data[:last.end])
    struct.pack_into("<I", out, last.off, ((last.end - last.off) >> 4) * 4)
    sizes = [list(s) for s in tp["sizes"]]
    for i, t in enumerate(new_ids):
        b = bytearray(data[pk.off:pk.end])
        struct.pack_into("<I", b, 0, 0 if i == len(new_ids) - 1 else (len(b) >> 4) * 4)
        struct.pack_into("<H", b, 8, t)
        out += b
        if pk.bin < len(sizes):
            sizes[pk.bin][0] += 1
    for i, (cnt, blk) in enumerate(sizes):
        struct.pack_into("<II", out, 8 + 8 * i, cnt, blk)
    return bytes(out)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opts = {}
    for opt in ("--name", "--front", "--scale", "--height"):
        if opt in argv:
            v = argv[argv.index(opt) + 1]
            args.remove(v)
            opts[opt[2:]] = v
    if len(args) != 3:
        print(__doc__.strip().splitlines()[2])
        return 2
    if opts.get("front") not in (None, "+y", "-y"):
        print("error: --front is +y or -y")
        return 2
    try:
        build(args[0], args[1], args[2], name=opts.get("name"), front=opts.get("front"),
              scale=float(opts["scale"]) if "scale" in opts else None,
              height=float(opts["height"]) if "height" in opts else None, fill="--no-fill" not in argv)
    except (RigError, ngpanim.AnimError, ngpmesh.MeshError) as e:
        print("error:", e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
