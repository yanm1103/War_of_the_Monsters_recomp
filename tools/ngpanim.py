#!/usr/bin/env python3
"""Animations edited in Blender back into a game monster's .NGP/.PTR.

  python tools/ngpanim.py <base.NGP> <edited.glb> <out_dir> [--name NAME] [--into OTHER.NGP]

The .glb comes from `tools/monglb.py --edit` (the mod kit's "Export to Blender"): one node per transform the game walks
(`bone<N>` for the ANIM_XFORM that drives bone output N, `xf_<node>` for the other ANIM_XFORMs, `st_<node>` for the fixed
TRANSLATE/ROTATE/SCALE, `sc_<node>` for ANIM_SCALE), every one a joint of the skin, the bind pose as the rest pose, and every
animation of the monster as a clip `a<id>` (`a<id>_<k>` when the id repeats). In Blender the bones are posed and keyed; an
animation whose clip differs from the game's is rebuilt:
- each joint's motion relative to its rest pose (immune to the bone-axis change Blender makes on import) gives the game's
  world matrix of that transform, frame by frame (60 per second);
- walking the game's hierarchy top-down, each ANIM_XFORM's local matrix is solved into its channels (pre/post rotation
  removed, PRH angles unwrapped from frame to frame, translation); an axis without a channel keeps the node's value and the
  rest goes to the children; a joint scaled to 0 hides its part (visibility channel);
- moved nodes get new curves (linear 16-bit, or static), unmoved ones keep the game's; event channels (contacts, sounds)
  follow the new length;
- the animation goes to the end of the file (new `_animation`, its `animCtx` control node repointed with a new `prevKey`).

Clips with ids the monster does not have are reported and skipped. Only the standard library: it runs inside the mod kit.
Formats: docs/FORMATS.md ("Animation"); the game-side walk mirrors tools/pose.py.
"""
import bisect
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import animcurve  # noqa: E402
import ngp  # noqa: E402
import ngpmesh  # noqa: E402

BASE = 0xA00000
FPS = 60.0
ROOT_Q = (-math.sqrt(0.5), 0.0, 0.0, math.sqrt(0.5))   # monglb's root node "WotM": game Z-up -> glTF Y-up


class AnimError(Exception):
    pass


# ---------------------------------------------------------------- 4x4 matrices as lists of rows

def eye():
    return [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]


def mul(a, b):
    return [[a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c] + a[r][3] * b[3][c] for c in range(4)] for r in range(4)]


def tr(m):
    return [[m[c][r] for c in range(4)] for r in range(4)]


def inv(m):
    a = [list(row) + [1.0 if r == c else 0.0 for c in range(4)] for r, row in enumerate(m)]
    for c in range(4):
        p = max(range(c, 4), key=lambda r: abs(a[r][c]))
        if abs(a[p][c]) < 1e-12:
            raise AnimError("singular matrix")
        a[c], a[p] = a[p], a[c]
        d = a[c][c]
        a[c] = [x / d for x in a[c]]
        for r in range(4):
            if r != c and a[r][c]:
                f = a[r][c]
                a[r] = [x - f * y for x, y in zip(a[r], a[c])]
    return [row[4:] for row in a]


def det3(m):
    return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))


def prh(x, y, z):
    """mathfRotMatrixPRH (tools/pose.py), vetores-linha."""
    sz, cz, sx, cx, sy, cy = math.sin(z), math.cos(z), math.sin(x), math.cos(x), math.sin(y), math.cos(y)
    return [[cy * cz, -cy * sz, sy, 0.0],
            [sx * sy * cz + cx * sz, -sx * sy * sz + cx * cz, -sx * cy, 0.0],
            [-cx * sy * cz + sx * sz, cx * sy * sz + sx * cz, cx * cy, 0.0],
            [0.0, 0.0, 0.0, 1.0]]


def translate(x, y, z):
    m = eye()
    m[3][:3] = [x, y, z]
    return m


def scale(x, y, z):
    m = eye()
    m[0][0], m[1][1], m[2][2] = x, y, z
    return m


def quat_col(q):
    """Rotation matrix (column vectors) of a glTF quaternion (x, y, z, w)."""
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0.0],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0.0],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0.0],
            [0.0, 0.0, 0.0, 1.0]]


def trs_col(t, q, s):
    m = quat_col(q)
    for r in range(3):
        for c in range(3):
            m[r][c] *= s[c]
        m[r][3] = t[r]
    return m


def rot_err(a, b):
    """Angle (radians) between the rotation parts of two row matrices (orthonormalized by the trace)."""
    c = sum(a[r][k] * b[r][k] for r in range(3) for k in range(3))
    na = math.sqrt(sum(a[r][k] ** 2 for r in range(3) for k in range(3))) or 1.0
    nb = math.sqrt(sum(b[r][k] ** 2 for r in range(3) for k in range(3))) or 1.0
    return math.acos(max(-1.0, min(1.0, (c * 3.0 / (na * nb) - 1.0) / 2.0)))


def mat_diff(a, b):
    return (max(abs(a[r][c] - b[r][c]) for r in range(3) for c in range(3)),
            math.sqrt(sum((a[3][c] - b[3][c]) ** 2 for c in range(3))))


# ---------------------------------------------------------------- the game side

class Ctx:
    """A CHARACTER_INSTANCE's channel state (tools/pose.py CharCtx)."""

    def __init__(self, img, ci):
        self.ci = ci
        ch = img.u32(ci + 0x20)
        self.nch = img.u16(ch + 2)
        vp = img.u32(ci + 4)
        self.val = [img.f32(vp + 4 * i) if img.ok(vp + 4 * i) else 0.0 for i in range(self.nch)]
        self.dirty = set()
        self.outAddr = {}
        amp = img.u32(ci + 0x14)
        if img.ok(amp):
            for k in range(64):
                if not img.ok(amp + 4 * k):
                    break
                a = img.u32(amp + 4 * k) & 0x0FFFFFFF
                if not img.ok(a, 64):
                    break
                self.outAddr[k] = a

    def v(self, idx, fallback):
        return self.val[idx] if 0 <= idx < self.nch else fallback


class Node:
    __slots__ = ("op", "parent", "out", "bone", "ci", "name")

    def __init__(self, op, parent, out, bone, ci):
        self.op, self.parent, self.out, self.bone, self.ci, self.name = op, parent, out, bone, ci, None


class Game:
    """The base monster: transforms (structure, rest and bind pose) and its animations, evaluated as tools/pose.py does."""

    def __init__(self, path):
        self.path = path
        self.img = ngp.Image.load(path, BASE)
        img = self.img
        self.root = img.roots()[0]
        self.cis = []
        self._find(self.root, 0)
        if not self.cis:
            raise AnimError("%s: no CHAR_INSTANCE under root 0" % path)
        self.main = max(self.cis, key=lambda c: img.u16(img.u32(c + 0x20)))
        self.nodes = {}
        self.order = []
        rest = self.walk(self.contexts(), every=True)
        self.rest_L = {n: L for n, (L, _w, _v) in rest.items()}
        one_ci = len({nd.ci for nd in self.nodes.values() if nd.out is not None}) <= 1
        for n, nd in self.nodes.items():
            if nd.out is not None:
                nd.name = "bone%d" % nd.out if one_ci else "bone%x_%d" % (nd.ci, nd.out)
            else:
                nd.name = {17: "xf_%x", 38: "sc_%x"}.get(nd.op, "st_%x") % n
        self.by_name = {nd.name: n for n, nd in self.nodes.items()}

        def depth(n):
            d = 0
            while self.nodes[n].parent is not None:
                n, d = self.nodes[n].parent, d + 1
            return d
        self.order = sorted(self.nodes, key=lambda n: (depth(n), n))
        self.bind = {}
        for n in self.order:
            nd = self.nodes[n]
            if nd.out is not None:
                self.bind[n] = inv(self.mat(nd.bone + 0x10))
            else:
                L = self.rest_L[n]
                if abs(det3(L)) < 1e-9:   # a part scaled to 0 (or an empty atMat) at rest: the bind pose keeps it unscaled
                    L = [[1.0 if r == c else 0.0 for c in range(3)] + [0.0] for r in range(3)] + [list(L[3])]
                self.bind[n] = mul(L, self.bind[nd.parent] if nd.parent is not None else eye())
        self._curves = {}

    def mat(self, a):
        f = struct.unpack_from("<16f", self.img.d, a - BASE)
        return [list(f[4 * r:4 * r + 4]) for r in range(4)]

    def _find(self, n, depth):
        img = self.img
        if depth > 80 or not img.ok(n):
            return
        op = img.u32(n) & 63
        if op == 25:
            self.cis.append(n)
        for arr, cnt in self._kids(n, op):
            for k in range(min(cnt, 4096)):
                self._find(img.u32(arr + 4 * k), depth + 1)

    def _kids(self, n, op):
        img = self.img
        if op == 1:
            return [(n + 0x20, img.u16(n + 8))]
        if op == 2:
            nl = img.u32(n + 4)
            return [(n + 0x20 + 16 * i + 8, 1) for i in range(min(nl, 16))]
        table = {3: (8, 0x1C), 4: (8, 0x50), 17: (4, 0xCC), 25: (0x28, 0x2C), 35: (4, 0xC), 38: (4, 0x60), 30: (8, 0x2C)}
        if op in table:
            c, a = table[op]
            return [(n + a, img.u32(n + c))]
        return []

    def contexts(self):
        return {ci: Ctx(self.img, ci) for ci in self.cis}

    # ---- animations of the main instance
    def anims(self):
        """[(id, k, startTime, endTime, numChannels, address)]."""
        img = self.img
        ch = img.u32(self.main + 0x20)
        out = []
        for k in range(img.u16(ch)):
            a = img.u32(ch + 0xC + 4 * k)
            out.append((img.u32(a) >> 18, k, img.f32(a + 4), img.f32(a + 8), img.u16(a + 0xC), a))
        return out

    def curves(self, an):
        if an not in self._curves:
            img = self.img
            self._curves[an] = [(img.u32(an + 0x18 + 4 * c), animcurve.Curve(img, img.u32(an + 0x18 + 4 * c)))
                                for c in range(img.u16(an + 0xC))]
        return self._curves[an]

    def anim_pose(self, an, frame):
        """{transform: (L, world, visible)} of the main instance in animation `an` at `frame` (clamped to its end)."""
        img = self.img
        ctxs = self.contexts()
        c = ctxs[self.main]
        st, et = img.f32(an + 4), img.f32(an + 8)
        if abs(frame - round(frame)) < 1e-4:   # a .glb time (frame / 60, float32) lands on its frame, not beside a step
            frame = float(round(frame))
        t = st + max(0.0, min(frame, et - st))
        for _a, cv in self.curves(an):
            if 0 <= cv.idx < c.nch:
                c.val[cv.idx] = cv.eval(t)
                c.dirty.add(cv.idx)
        return self.walk(ctxs)

    # ---- the walk (tools/pose.py Model.pose, transforms only)
    def walk(self, ctxs, every=False):
        """{transform: (L, world, visible)}. every=True also enters the subtrees a visibility channel hides (structure)."""
        img = self.img
        out = {}
        smat, skel, owner = {}, {}, {}
        record = every and not self.nodes

        def skelstep(ctx, L, si, mi):
            if si < 0:
                return si, mi
            smat[mi + 1] = mul(L, smat[mi])
            bone = skel[si]
            o = img.s16(bone + 8)
            owner[mi + 1] = o if o >= 0 and ctx is not None and o in ctx.outAddr else None
            return si - 1, mi + 1

        def rec(n, op, L, world, ctx, tp, si, mi2, vis):
            out[n] = (L, world, vis)
            if record:
                o = owner.get(mi2) if si >= 0 else None
                self.nodes[n] = Node(op, tp, o, skel[si] if o is not None else None, ctx.ci if ctx else None)

        def visit(n, m, si, mi, ctx, tp, depth, vis=True):
            if depth > 80 or not img.ok(n):
                return

            def kids(arr, cnt, mm, si2=si, mi2=mi, ctx2=ctx, tp2=tp, vis2=vis):
                for q in range(min(cnt, 4096)):
                    visit(img.u32(arr + 4 * q), mm, si2, mi2, ctx2, tp2, depth + 1, vis2)

            op = img.u32(n) & 63
            if op == 1:
                if img.u16(n + 0xA) & 1:
                    kids(n + 0x20, img.u16(n + 8), m)
            elif op == 2:
                nl = min(img.u32(n + 4), 16)
                for i in range(nl - 1, -1, -1):
                    if img.f32(n + 0x24 + 16 * i) > 0.0:
                        visit(img.u32(n + 0x28 + 16 * i), m, si, mi, ctx, tp, depth + 1, vis)
                        break
            elif op in (3, 4):
                if op == 3:
                    L = translate(img.f32(n + 0x10), img.f32(n + 0x14), img.f32(n + 0x18))
                else:
                    L = self.mat(n + 0x10)
                s2, m2 = skelstep(ctx, L, si, mi)
                w = mul(L, m)
                rec(n, op, L, w, ctx, tp, si, m2, vis)
                kids(n + (0x1C if op == 3 else 0x50), img.u32(n + 8), w, s2, m2, tp2=n)
            elif op == 30:
                L = mul(scale(img.f32(n + 0x20), img.f32(n + 0x24), img.f32(n + 0x28)),
                        translate(img.f32(n + 0x10), img.f32(n + 0x14), img.f32(n + 0x18)))
                w = mul(L, m)
                rec(n, op, L, w, ctx, tp, -1, mi, vis)
                kids(n + 0x2C, img.u32(n + 8), w, tp2=n)
            elif op == 6:
                wh, cnt = img.u8(n + 8), img.u8(n + 0xB)
                if wh < cnt:
                    visit(img.u32(n + 0xC + 4 * wh), m, si, mi, ctx, tp, depth + 1, vis)
            elif op in (8, 23):
                visit(img.u32(n + 4), m, si, mi, ctx, tp, depth + 1, vis)
            elif op == 35:
                si2 = si + 1
                skel[si2] = n
                mi2 = mi
                if img.u16(n + 0xA):
                    mi2 = mi + 1
                    smat[mi2] = eye()
                    owner[mi2] = None
                kids(n + 0xC, img.u32(n + 4), m, si2, mi2)
            elif op == 25:
                kids(n + 0x2C, img.u32(n + 0x28), m, si, mi, ctxs.get(n))
            elif op == 17:
                c = ctx
                v = img.f32(n + 0xC)
                vidx = img.s16(n + 0x10)
                if c and vidx >= 0 and vidx in c.dirty:
                    v = c.val[vidx]
                shown = vis and v != 0.0
                if not shown and not every:
                    return
                L = self.xform_L(n, c)
                s2, m2 = skelstep(c, L, si, mi)
                w = mul(L, m)
                rec(n, op, L, w, c, tp, si, m2, shown)
                kids(n + 0xCC, img.u32(n + 4), w, s2, m2, tp2=n, vis2=shown)
            elif op == 38:
                c = ctx

                def g(off, fb):
                    i = img.s16(n + off)
                    return c.v(i, fb) if (c and i >= 0) else fb
                vidx = img.s16(n + 0x10)
                v = c.v(vidx, 1.0) if (c and vidx >= 0) else 1.0
                sx, sy, sz = g(0x12, img.f32(n + 0x20)), g(0x14, img.f32(n + 0x24)), g(0x16, img.f32(n + 0x28))
                shown = vis and v != 0.0 and not (sx == 0.0 and sy == 0.0 and sz == 0.0)
                if not shown and not every:
                    return
                tx, ty, tz = g(0x18, img.f32(n + 0x30)), g(0x1A, img.f32(n + 0x34)), g(0x1C, img.f32(n + 0x38))
                L = mul(mul(translate(img.f32(n + 0x40), img.f32(n + 0x44), img.f32(n + 0x48)), scale(sx, sy, sz)),
                        translate(tx + img.f32(n + 0x50), ty + img.f32(n + 0x54), tz + img.f32(n + 0x58)))
                w = mul(L, m)
                rec(n, op, L, w, c, tp, -1, mi, shown)
                kids(n + 0x60, img.u32(n + 4), w, tp2=n, vis2=shown)
            elif op == 39:
                c2 = img.u32(n + 0xC)
                if img.ok(c2) and (img.u32(c2) & 63) == 31:
                    if img.u16(c2 + 0xA):
                        visit(img.u32(c2 + 0xC), m, si, mi, ctx, tp, depth + 1, vis)
                else:
                    visit(c2, m, si, mi, ctx, tp, depth + 1, vis)
            elif op == 31:
                if img.u16(n + 0xA):
                    visit(img.u32(n + 0xC), m, si, mi, ctx, tp, depth + 1, vis)

        visit(self.root, eye(), -1, -1, None, None, 0)
        return out

    def xform(self, n):
        """Fields of an ANIM_XFORM: rotation/translation defaults, channel indices, visibility, pre/post rotation."""
        img = self.img
        return {"r0": [img.f32(n + 0x20 + 4 * j) for j in range(3)], "t0": [img.f32(n + 0x30 + 4 * j) for j in range(3)],
                "ridx": [img.s16(n + 0x12 + 2 * j) for j in range(3)], "tidx": [img.s16(n + 0x18 + 2 * j) for j in range(3)],
                "vidx": img.s16(n + 0x10), "bits": img.u16(n + 0xC2), "pre": self.mat(n + 0x40), "post": self.mat(n + 0x80),
                "first": img.s16(n + 0x1E), "mask": img.u8(n + 0xC1), "vis0": img.f32(n + 0xC)}

    def xform_L(self, n, c, vals=None):
        """The ANIM_XFORM's local matrix: rebuilt from the channels when one of them was written (dirty), else atMat."""
        img = self.img
        x = self.xform(n)
        first, mask = x["first"], x["mask"]
        if vals is None:
            rebuild = bool(c) and first >= 0 and any((mask >> b) & 1 and (first + b) in c.dirty for b in range(8))
            if not rebuild:
                at = img.u32(c.ci + 0xC) + 64 * img.u32(n + 0xC4) if c else 0
                return self.mat(at) if c and img.ok(at, 64) else eye()
            get = c.v
        else:
            def get(i, fb):
                return vals.get(i, fb)
        r, t = list(x["r0"]), list(x["t0"])
        for j in range(3):
            if x["ridx"][j] >= 0:
                r[j] = -get(x["ridx"][j], -r[j])
            if x["tidx"][j] >= 0:
                t[j] = get(x["tidx"][j], t[j])
        return compose(x, r, t)


def compose(x, r, t):
    L = prh(*r)
    if x["bits"] & 1:
        L = mul(x["pre"], L)
    if x["bits"] & 2:
        L = mul(L, x["post"])
    for j in range(3):
        L[3][j] += t[j]
        L[j][3] = 0.0
    L[3][3] = 1.0
    return L


# ---------------------------------------------------------------- solving an ANIM_XFORM's channels

TWO_PI = 2.0 * math.pi


def _near(a, ref):
    return a + TWO_PI * round((ref - a) / TWO_PI)


def prh_candidates(m, prev):
    """PRH angles (x, y, z) of the 3x3 part of row matrix m, both branches, unwrapped to the nearest of prev."""
    sy = max(-1.0, min(1.0, m[0][2]))
    y = math.asin(sy)
    cy = math.cos(y)
    out = []
    if cy > 1e-5:
        z = math.atan2(-m[0][1], m[0][0])
        x = math.atan2(-m[1][2], m[2][2])
        out = [(x, y, z), (x + math.pi, math.pi - y, z + math.pi)]
    else:   # gimbal: y = +-90 degrees, only x + z (or z - x) is known; keep x
        x = prev[0]
        if sy > 0:
            z = math.atan2(m[1][0], m[1][1]) - x
        else:
            z = math.atan2(m[1][0], m[1][1]) + x
        out = [(x, y, z)]
    return [tuple(_near(a, p) for a, p in zip(c, prev)) for c in out]


def fit_angles(R, free, fixed, prev):
    """Angles minimizing |prh - R| with the axes not in `free` held at `fixed`; continuous with prev."""
    cands = prh_candidates(R, prev)
    best = min(cands, key=lambda c: sum((a - p) ** 2 for a, p in zip(c, prev)))
    if all(free):
        return list(best)
    v = [best[j] if free[j] else fixed[j] for j in range(3)]
    idx = [j for j in range(3) if free[j]]
    if not idx:
        return v

    def resid(vv):
        P = prh(*vv)
        return [P[r][c] - R[r][c] for r in range(3) for c in range(3)]
    # Gauss-Newton from the unconstrained branch (and from prev, keeping the better)
    sols = []
    for start in (v, [prev[j] if free[j] else fixed[j] for j in range(3)]):
        cur = list(start)
        for _ in range(30):
            e = resid(cur)
            J = []
            for j in idx:
                d = list(cur)
                d[j] += 1e-6
                J.append([(a - b) / 1e-6 for a, b in zip(resid(d), e)])
            # normal equations (k x k, k <= 2)
            A = [[sum(J[a][q] * J[b][q] for q in range(9)) for b in range(len(idx))] for a in range(len(idx))]
            g = [sum(J[a][q] * e[q] for q in range(9)) for a in range(len(idx))]
            if len(idx) == 1:
                step = [-g[0] / A[0][0]] if abs(A[0][0]) > 1e-12 else [0.0]
            else:
                dt = A[0][0] * A[1][1] - A[0][1] * A[1][0]
                if abs(dt) < 1e-12:
                    break
                step = [-(A[1][1] * g[0] - A[0][1] * g[1]) / dt, -(A[0][0] * g[1] - A[1][0] * g[0]) / dt]
            for a, j in enumerate(idx):
                cur[j] += step[a]
            if max(abs(s) for s in step) < 1e-9:
                break
        sols.append((sum(x * x for x in resid(cur)), [(_near(cur[j], prev[j]) if free[j] else cur[j]) for j in range(3)]))
    return min(sols)[1]


def solve_xform(x, Lt, prev_r):
    """ANIM_XFORM fields x, target local Lt -> (r angles, t, fitted L). prev_r: last frame's angles (continuity)."""
    A = [row[:3] + [0.0] for row in Lt[:3]] + [[0.0, 0.0, 0.0, 1.0]]
    X = A
    if x["bits"] & 1:
        p3 = [row[:3] + [0.0] for row in x["pre"][:3]] + [[0.0, 0.0, 0.0, 1.0]]
        X = mul(inv(p3), X)
    if x["bits"] & 2:
        q3 = [row[:3] + [0.0] for row in x["post"][:3]] + [[0.0, 0.0, 0.0, 1.0]]
        X = mul(X, inv(q3))
    free = [i >= 0 for i in x["ridx"]]
    r = fit_angles(X, free, x["r0"], prev_r)
    base = compose(x, r, [0.0, 0.0, 0.0])
    t = [Lt[3][j] - base[3][j] if x["tidx"][j] >= 0 else x["t0"][j] for j in range(3)]
    return r, t, compose(x, r, t)


# ---------------------------------------------------------------- the edited .glb

def _slerp(a, b, u):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0:
        b, d = [-x for x in b], -d
    if d > 0.9995:
        q = [x + (y - x) * u for x, y in zip(a, b)]
    else:
        th = math.acos(d)
        s = math.sin(th)
        q = [(math.sin((1 - u) * th) * x + math.sin(u * th) * y) / s for x, y in zip(a, b)]
    n = math.sqrt(sum(x * x for x in q)) or 1.0
    return [x / n for x in q]


class Sampler:
    def __init__(self, times, vals, interp, rot):
        self.t, self.v, self.interp, self.rot = times, vals, interp, rot

    def at(self, t):
        ts, n = self.t, len(self.t)
        cubic = self.interp == "CUBICSPLINE"

        def val(i):
            return list(self.v[3 * i + 1] if cubic else self.v[i])
        if t <= ts[0] or n == 1:
            return val(0)
        if t >= ts[-1]:
            return val(n - 1)
        i = bisect.bisect_right(ts, t) - 1
        t0, t1 = ts[i], ts[i + 1]
        u = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
        if self.interp == "STEP":
            return val(i)
        if cubic:
            dt = t1 - t0
            p0, m0 = self.v[3 * i + 1], self.v[3 * i + 2]
            p1, m1 = self.v[3 * (i + 1) + 1], self.v[3 * (i + 1)]
            u2, u3 = u * u, u * u * u
            out = [(2 * u3 - 3 * u2 + 1) * a + (u3 - 2 * u2 + u) * dt * b + (-2 * u3 + 3 * u2) * c + (u3 - u2) * dt * d
                   for a, b, c, d in zip(p0, m0, p1, m1)]
            if self.rot:
                nn = math.sqrt(sum(x * x for x in out)) or 1.0
                out = [x / nn for x in out]
            return out
        if self.rot:
            return _slerp(self.v[i], self.v[i + 1], u)
        return [a + (b - a) * u for a, b in zip(self.v[i], self.v[i + 1])]


class Edited:
    """The .glb exported from Blender: nodes, their rest (default) transforms and the clips."""

    def __init__(self, path):
        self.glb = ngpmesh.Glb(path)
        j = self.glb.j
        self.nodes = self.glb.nodes
        self.parent = {}
        for i, nd in enumerate(self.nodes):
            for c in nd.get("children", []):
                self.parent[c] = i
        self.trs = []
        for nd in self.nodes:
            if "matrix" in nd:
                self.trs.append(("m", nd["matrix"]))
            else:
                self.trs.append((nd.get("translation", [0.0, 0.0, 0.0]), nd.get("rotation", [0.0, 0.0, 0.0, 1.0]),
                                 nd.get("scale", [1.0, 1.0, 1.0])))
        self.clips = {}
        for a in j.get("animations", []):
            chans = {}
            for ch in a.get("channels", []):
                tg = ch.get("target", {})
                path = tg.get("path")
                if "node" not in tg or path not in ("translation", "rotation", "scale"):
                    continue
                s = a["samplers"][ch["sampler"]]
                times = [x[0] for x in self.glb.accessor(s["input"])]
                vals = self.glb.accessor(s["output"])
                chans.setdefault(tg["node"], {})[path] = Sampler(times, vals, s.get("interpolation", "LINEAR"),
                                                                 path == "rotation")
            self.clips[a.get("name") or "clip%d" % len(self.clips)] = chans
        self.names = {}
        for i, nd in enumerate(self.nodes):
            self.names.setdefault(nd.get("name"), i)

    def local(self, i, clip, t):
        d = self.trs[i]
        if d[0] == "m":
            m = d[1]
            return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
        tt, q, s = d
        ch = clip.get(i) if clip else None
        if ch:
            if "translation" in ch:
                tt = ch["translation"].at(t)
            if "rotation" in ch:
                q = ch["rotation"].at(t)
            if "scale" in ch:
                s = ch["scale"].at(t)
        return trs_col(tt, q, s)

    def world(self, i, clip, t, cache):
        if i in cache:
            return cache[i]
        m = self.local(i, clip, t)
        p = self.parent.get(i)
        if p is not None:
            m = mul(self.world(p, clip, t, cache), m)
        cache[i] = m
        return m

    def key_times(self, clip, nodes):
        ts = set()
        for i in nodes:
            for s in clip.get(i, {}).values():
                ts.update(s.t)
        return sorted(ts)


# ---------------------------------------------------------------- curves and the new _animation

def _align(d, a):
    while len(d) % a:
        d.append(0)


class Writer:
    def __init__(self, base):
        self.b = base

    def alloc(self, size, align):
        d = self.b.d
        _align(d, align)
        a = BASE + len(d)
        d.extend(bytes(size))
        return a

    def static(self, idx, value):
        a = self.alloc(8, 4)
        struct.pack_into("<HHf", self.b.d, a - BASE, 3, idx, value)
        return a

    def pwl16(self, idx, st, vals):
        """kPwl k16bit: one key per frame from st (AnimNormData before the header)."""
        lo, hi = min(vals), max(vals)
        dv = (hi - lo) / 65535.0 if hi > lo else 1.0
        n = len(vals)
        blk = self.alloc(16 + 6 + 2 * n, 4)
        o = blk - BASE
        struct.pack_into("<ffff", self.b.d, o, st, 1.0, lo, dv)
        struct.pack_into("<HHh", self.b.d, o + 16, 2 | (1 << 3), idx, n)
        struct.pack_into("<%dH" % n, self.b.d, o + 22, *[max(0, min(65535, int(round((v - lo) / dv)))) for v in vals])
        return blk + 16

    def stepped(self, idx, keys):
        """kStepped float: [(time, value)]."""
        n = len(keys)
        blk = self.alloc(16 + 8 + 8 * n, 8)
        o = blk - BASE
        struct.pack_into("<HHh", self.b.d, o + 16, 5, idx, n)
        for i, (t, v) in enumerate(keys):
            struct.pack_into("<ff", self.b.d, o + 24 + 8 * i, t, v)
        return blk + 16

    def curve(self, idx, st, vals):
        if max(vals) - min(vals) <= 1e-6 * max(1.0, abs(vals[0])):
            return self.static(idx, vals[0])
        return self.pwl16(idx, st, vals)

    def animation(self, game, k, an, n_frames, curves):
        """New _animation for slot k of the main instance (header copied from `an`), repointing animations[k] and
        animCtx[k] (anim, a new prevKey)."""
        b = self.b
        nch = len(curves)
        a = self.alloc(0x18 + 4 * nch, 16)
        o = a - BASE
        b.d[o:o + 0x18] = game.img.d[an - BASE:an - BASE + 0x18]
        st = game.img.f32(an + 4)
        struct.pack_into("<f", b.d, o + 8, st + (n_frames - 1))
        struct.pack_into("<H", b.d, o + 0xC, nch)
        for i, c in enumerate(curves):
            struct.pack_into("<I", b.d, o + 0x18 + 4 * i, c)
            b.ptr[0].append(o + 0x18 + 4 * i)
        pk = self.alloc(max(4, 2 * nch), 4)
        ch = b.u32(game.main + 0x20)
        actx = b.u32(game.main + 0x10)
        b.put32(ch + 0xC + 4 * k, a)
        b.put32(actx + 60 * k + 4, a)
        b.put32(actx + 60 * k + 0x30, pk)
        return a


def retime(w, game, cv, addr, st, s, n_frames):
    """Copy of an event/other curve with its times scaled by s from st: static as is, stepped keys moved, others sampled."""
    if cv.ctype == 3:
        return addr
    if cv.ctype == 5:
        return w.stepped(cv.idx, [(st + (cv.T(i) - st) * s, cv.V(i)) for i in range(cv.n)])
    return w.curve(cv.idx, st, [cv.eval(st + f / s) for f in range(n_frames)])


# ---------------------------------------------------------------- the import

def _clip_key(name, game_anims):
    m = re.fullmatch(r"a(\d+)(?:_(\d+))?(?:\..*)?", name or "")
    if not m:
        return None
    aid = int(m.group(1))
    if m.group(2) is not None:
        k = int(m.group(2))
        return k if any(a[1] == k and a[0] == aid for a in game_anims) else -1
    return next((a[1] for a in game_anims if a[0] == aid), -1)


# what counts as "not edited": rotation part (matrix entries) and translation (game units) of every transform
SAME_ROT, SAME_POS = 2e-3, 0.02


def rebuilds(x):
    """Whether the game ever rebuilds this ANIM_XFORM from its channels (hierAnimTransNode: a dirty channel in
    firstIdx..+7 under the mask); otherwise it always draws atMat and its rotation/translation channels go unread."""
    return x["first"] >= 0 and x["mask"] != 0


def shown(o):
    """A game transform from anim_pose drawn with some volume: the .glb shows hidden parts as scale 0, so a scale node
    flattened to (almost) nothing counts as hidden on both sides."""
    return o is not None and o[2] and abs(det3(o[1])) >= 1e-9


def import_glb(ngp_path, glb_path, out_dir, name=None, log=print, into=None):
    """Rebuilds every clip of the .glb that differs from the game's animation. ngp_path: the base monster (disc); into:
    another .NGP (with .PTR) to write into, e.g. the base with a new mesh already in (default: ngp_path). Returns
    {id: (frames before, frames after, transforms moved)}."""
    game = Game(ngp_path)
    ed = Edited(glb_path)
    gnodes = game.nodes
    mapped = {}   # transform -> .glb node
    for n, nd in gnodes.items():
        i = ed.names.get(nd.name)
        if i is not None:
            mapped[n] = i
    if not any(nd.out is not None for n, nd in gnodes.items() if n in mapped):
        raise AnimError("no bone<N> joint of %s in the .glb (export it with Export to Blender / monglb --edit)" %
                        os.path.basename(ngp_path))
    # R^-1 and R (root "WotM"), in column form; W_game_col = R^-1 . G(t) . G_rest^-1 . R . W_bind_col
    Rq = quat_col(ROOT_Q)
    Ri = tr(Rq)
    rest_cache = {}
    G_rest = {n: ed.world(i, None, 0.0, rest_cache) for n, i in mapped.items()}
    K = {n: mul(mul(inv(G_rest[n]), Rq), tr(game.bind[n])) for n in mapped}

    def targets(clip, t):
        cache = {}
        out = {}
        for n, i in mapped.items():
            G = ed.world(i, clip, t, cache)
            hidden = abs(det3(G)) < 1e-9
            out[n] = (None if hidden else tr(mul(mul(Ri, G), K[n])))
        return out

    ganims = game.anims()
    by_k = {a[1]: a for a in ganims}
    clips = {}
    unknown = []
    for nm, clip in ed.clips.items():
        k = _clip_key(nm, ganims)
        if k is None:
            continue
        if k < 0:
            unknown.append(nm)
            continue
        clips[k] = (nm, clip)
    if unknown:
        log("warning: clips with no animation of that id in %s (new animations are not supported yet), skipped: %s" % (
            os.path.basename(ngp_path), ", ".join(sorted(unknown))))
    if not clips:
        raise AnimError("no clip a<id> in the .glb")

    # time scale: Blender keys at time * frame rate on import and writes frame / frame rate on export; a frame rate changed
    # in between stretches every clip alike
    ratios = []
    for k, (nm, clip) in clips.items():
        aid, _k, st, et, _n, an = by_k[k]
        ts = ed.key_times(clip, mapped.values())
        n0 = int(math.floor(et - st + 1e-6))
        if ts and n0 >= 12:
            ratios.append(ts[-1] * FPS / n0)
    tscale = 1.0
    if ratios:
        ratios.sort()
        med = ratios[len(ratios) // 2]
        if abs(med - 1.0) > 0.05:
            tscale = 1.0 / med
            log("warning: the clips are %.2fx the game's length (frame rate changed between the import and the export in "
                "Blender?); times scaled by %.3f" % (med, tscale))

    base = ngpmesh.Base(into or ngp_path)
    w = Writer(base)
    done = {}
    xf_of = {n: game.xform(n) for n, nd in gnodes.items() if nd.op == 17}
    chan_owner = {}   # channel -> ANIM_XFORM
    for n, x in xf_of.items():
        for i in x["ridx"] + x["tidx"] + [x["vidx"]]:
            if i >= 0:
                chan_owner[i] = n
    for k in sorted(clips):
        nm, clip = clips[k]
        aid, _k, st, et, nch0, an = by_k[k]
        n0 = int(math.floor(et - st + 1e-6)) + 1
        keys = ed.key_times(clip, mapped.values())
        if not keys:
            log("%s: no keys, kept" % nm)
            continue
        n_new = int(round(keys[-1] * tscale * FPS)) + 1
        # not edited? compare at the clip's own key times (where the export sampled), on the game's frame grid
        # (local matrices: a world compare would blow the export's key rounding up along long arms, e.g. a far locator)
        same = abs(n_new - n0) <= 1
        if same:
            for t in keys:
                tg = targets(clip, t)
                orig = game.anim_pose(an, t * tscale * FPS)
                for n, W in tg.items():
                    o = orig.get(n)
                    ovis = shown(o)
                    if (W is None) != (not ovis):
                        same = False
                        break
                    if W is None:
                        continue
                    par = gnodes[n].parent
                    Wp = tg.get(par) if par is not None else eye()
                    if Wp is None:
                        continue
                    dr, dp = mat_diff(mul(W, inv(Wp)), o[0])
                    if dr > SAME_ROT or dp > SAME_POS:
                        same = False
                        break
                if not same:
                    break
        if same:
            continue
        frames = n_new
        if frames < 2:
            log("%s: a single frame; kept" % nm)
            continue
        # solve frame by frame, top-down
        vals = {}          # channel -> [value per frame]
        moved = set()
        worst = [0.0, 0.0, None, 0]
        fixed_moved = set()
        s = (frames - 1) / float(n0 - 1) if n0 > 1 else 1.0
        # continuity starts from the game's own values at frame 0 (its angles are not wrapped to +-pi)
        c0 = {cv.idx: cv.eval(st) for _a, cv in game.curves(an)}
        init = game.contexts()[game.main].val
        prev_r, prev_t = {}, {}
        for n, x in xf_of.items():
            prev_r[n] = [-c0.get(i, init[i] if 0 <= i < len(init) else -x["r0"][j]) if i >= 0 else x["r0"][j]
                         for j, i in enumerate(x["ridx"])]
            prev_t[n] = [c0.get(i, init[i] if 0 <= i < len(init) else x["t0"][j]) if i >= 0 else x["t0"][j]
                         for j, i in enumerate(x["tidx"])]
        animated = {n for n, x in xf_of.items() if any(i >= 0 for i in x["ridx"] + x["tidx"] + [x["vidx"]])}
        for f in range(frames):
            tg = targets(clip, f / FPS / tscale)
            orig = game.anim_pose(an, f / s)
            m = {}
            for n in game.order:
                nd = gnodes[n]
                P = m.get(nd.parent, eye()) if nd.parent is not None else eye()
                W = tg.get(n) if n in mapped else None
                o = orig.get(n)
                if n in animated:
                    x = xf_of[n]
                    if not rebuilds(x):
                        # channels the game never reads (no firstIdx/mask): only the visibility channel counts
                        L = o[0] if o is not None else game.rest_L[n]
                        vis = 0.0 if W is None else 1.0
                        if x["vidx"] >= 0:
                            vals.setdefault(x["vidx"], []).append(vis)
                        if shown(o) != (vis != 0.0):
                            moved.add(n)
                        elif W is not None and mat_diff(mul(W, inv(P)), L)[0] > 0.02:
                            fixed_moved.add(nd.name)
                        m[n] = mul(L, P)
                        continue
                    if W is None:
                        r, tt = prev_r[n], prev_t[n]
                        L = compose(x, r, tt)
                        vis = 0.0
                    else:
                        Lt = mul(W, inv(P))
                        r, tt, L = solve_xform(x, Lt, prev_r[n])
                        dr, dp = mat_diff(mul(L, P), W)   # in the world: a parent scaled to almost nothing blows a local error up
                        if dr > worst[0] or dp > worst[1]:
                            worst = [max(dr, worst[0]), max(dp, worst[1]), nd.name, f]
                        vis = 1.0
                    prev_r[n], prev_t[n] = r, tt
                    for j in range(3):
                        if x["ridx"][j] >= 0:
                            vals.setdefault(x["ridx"][j], []).append(-r[j])
                        if x["tidx"][j] >= 0:
                            vals.setdefault(x["tidx"][j], []).append(tt[j])
                    if x["vidx"] >= 0:
                        vals.setdefault(x["vidx"], []).append(vis)
                    ovis = shown(o)
                    if ovis != (vis != 0.0):
                        moved.add(n)
                    elif ovis:
                        dr, dp = mat_diff(L, o[0])
                        if dr > SAME_ROT or dp > SAME_POS:
                            moved.add(n)
                    m[n] = mul(L, P)
                else:
                    # fixed transform (or ANIM_SCALE, not solved): the game's matrix in this frame
                    L = o[0] if o is not None else game.rest_L[n]
                    m[n] = mul(L, P)
                    Wp = tg.get(nd.parent) if nd.parent is not None else eye()
                    if W is not None and Wp is not None:
                        dr, dp = mat_diff(mul(W, inv(Wp)), L)
                        if dr > 0.02 or dp > 0.1:
                            fixed_moved.add(nd.name)
        if fixed_moved:
            log("warning: %s: %s cannot move in the game (no channel); their motion goes nowhere" % (
                nm, ", ".join(sorted(fixed_moved))))
        if not moved and frames == n0:
            continue
        # the curves: moved transforms get new ones, the rest keeps the game's (retimed when the length changed)
        old = game.curves(an)
        new_curves = []
        written = set()
        for addr, cv in old:
            owner = chan_owner.get(cv.idx)
            if owner in moved and cv.idx in vals:
                v = vals[cv.idx]
                if frames == n0 and all(abs(cv.eval(st + f) - v[f]) <= 1e-5 for f in range(frames)):
                    new_curves.append(addr)
                else:
                    new_curves.append(w.curve(cv.idx, st, v))
                written.add(cv.idx)
            elif frames == n0:
                new_curves.append(addr)
            else:
                new_curves.append(retime(w, game, cv, addr, st, s, frames))
            written.add(cv.idx)
        for n in sorted(moved):
            x = xf_of[n]
            for i in x["ridx"] + x["tidx"] + [x["vidx"]]:
                if i >= 0 and i not in written and i in vals:
                    new_curves.append(w.curve(i, st, vals[i]))
                    written.add(i)
        w.animation(game, k, an, frames, new_curves)
        done[aid if sum(1 for a in ganims if a[0] == aid) == 1 else "%d_%d" % (aid, k)] = (n0, frames, len(moved))
        log("%s: rebuilt, %d -> %d frames, %d transforms moved, %d curves (%d new)%s" % (
            nm, n0, frames, len(moved), len(new_curves), sum(1 for c in new_curves if c >= BASE + len(game.img.d)),
            "; fit off by up to %.2g (rotation) / %.2g units at %s, frame %d" % tuple(worst) if worst[2] and (worst[0] > 1e-3 or worst[1] > 1e-3) else ""))
    if not done:
        log("no clip differs from the game's animations; nothing written")
        return done
    ngpmesh.write(base, out_dir, name or os.path.splitext(os.path.basename(ngp_path))[0], textures=False)
    return done


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    name = into = None
    for opt in ("--name", "--into"):
        if opt in argv:
            v = argv[argv.index(opt) + 1]
            args.remove(v)
            if opt == "--name":
                name = v
            else:
                into = v
    if len(args) != 3:
        print(__doc__.strip().splitlines()[2])
        return 2
    try:
        import_glb(args[0], args[1], args[2], name, into=into)
    except (AnimError, ngpmesh.MeshError) as e:
        print("error:", e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
