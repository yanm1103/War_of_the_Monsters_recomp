#!/usr/bin/env python3
"""A stage seen as the level editor sees it: placeable objects, collision and the per-stage points.

  python tools/lvl.py summary [STAGE...]              # one line per stage (all of disc/LVL by default)
  python tools/lvl.py objects CENTRAL [--csv out.csv] # destructibles, pickups and props, one per line
  python tools/lvl.py collision CENTRAL [--check]     # grid and paths; --check redoes every path's box and cells
  python tools/lvl.py points CENTRAL                  # PTS/<STAGE>.SPT/.PUP/.FOP/.PED/.AIP
  python tools/lvl.py mrs CENTRAL                     # RST/<STAGE>.MRS (background, fog, lights, time of day)
  option: --disc DIR (default: disc/ at the repo root)

Formats: docs/FORMATS.md (".NGP image", "Fixups", "Stage collision", "Stage objects", "Stage points"). The rules this tool
checks are the ones a level patch has to keep when it moves, removes or copies an object:
- every collision path's box is the leaf's local box, its 8 corners taken to the world through the path's TRANSLATE/ROTATE
  nodes (SCALE is ignored by the collision);
- a path is listed in exactly the grid cells that box covers.
Only the standard library; it reads the user's disc and writes nothing of the game.
"""
import bisect
import collections
import csv
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ngp  # noqa: E402

B = ngp.LEVEL_BASE
OPN = ngp.OPNAMES
MASK = 0x0FFFFFFF
XFORM = (3, 4, 30)              # TRANSLATE, ROTATE, SCALE
NGP_CEILING = 0x1F10000         # end of the stage NGP region (docs/guides/MODS.md)
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# ------------------------------------------------------------------------------------------------------------- hierarchy
def children(img, n):
    """Children of a node as dbsTraverse walks them, plus the DESTRUCTIBLE states and both CONTROL children."""
    u32, u16, u8 = img.u32, img.u16, img.u8
    op = u32(n) & 63
    arrays = {1: (0x20, u16(n + 8)), 3: (0x1C, u32(n + 8)), 4: (0x50, u32(n + 8)), 30: (0x2C, u32(n + 8)),
              6: (0xC, u8(n + 0xB)), 35: (0xC, u32(n + 4)), 25: (0x2C, u32(n + 0x28)), 17: (0xCC, u32(n + 4)),
              38: (0x60, u32(n + 4))}
    if op in arrays:
        off, cnt = arrays[op]
        out = [u32(n + off + 4 * k) for k in range(min(cnt, 8192))]
    elif op == 2:
        out = [u32(n + 0x28 + 16 * k) for k in range(min(u32(n + 4), 16))]
    elif op == 8:
        out = [u32(n + 4), u32(n + 8)]
    elif op == 23:
        out = [u32(n + 4)]
    elif op == 39:
        out = [u32(n + 0xC)]
    elif op == 31:
        out = [u32(n + 0xC + 12 * k) for k in range(min(u16(n + 0xA), 64))]
    else:
        out = []
    return [c & MASK for c in out if c and img.ok(c & MASK)]


def local_matrix(img, n, with_scale=True):
    """Local-to-parent matrix of a transform node (row vectors, p' = p * M), or None."""
    op = img.u32(n) & 63
    if op == 3:
        return ngp.translate_m((img.f32(n + 0x10), img.f32(n + 0x14), img.f32(n + 0x18)))
    if op == 4:
        return [[img.f32(n + 0x10 + 16 * r + 4 * c) for c in range(4)] for r in range(4)]
    if op == 30:
        t = (img.f32(n + 0x10), img.f32(n + 0x14), img.f32(n + 0x18))
        if not with_scale:
            return ngp.translate_m(t)
        s = (img.f32(n + 0x20), img.f32(n + 0x24), img.f32(n + 0x28))
        return ngp.mul(ngp.scale_m(s), ngp.translate_m(t))
    return None


def chain_matrix(img, nodes, with_scale=True):
    """World matrix of a root-to-leaf chain: the child's transform applies first."""
    m = ngp.ident()
    for n in nodes:
        lm = local_matrix(img, n, with_scale)
        if lm is not None:
            m = ngp.mul(lm, m)
    return m


def read_ptr(path):
    """The .PTR lists: 0 = pointer slots, 1 = 16-bit texture ids, 2 = GS TEX0 doublewords (byte offsets in the image)."""
    raw = open(path, "rb").read()
    o, out = 0, []
    for _ in range(3):
        n = struct.unpack_from("<I", raw, o)[0]
        out.append(list(struct.unpack_from("<%dI" % n, raw, o + 4)))
        o += 4 + 4 * n
    return out


# ------------------------------------------------------------------------------------------------------------- collision
def hull_planes(img, h):
    n = img.s16(h + 4)
    return [tuple(img.f32(h + 0x20 + 16 * k + 4 * i) for i in range(4)) for k in range(max(0, min(n, 256)))]


def _hull_corners(planes, sign):
    """Vertices of the convex hull {p: n.p <= d} (sign +1) or {n.p + d <= 0} (sign -1), by intersecting plane triples."""
    pl = [(a, b, c, d if sign > 0 else -d) for a, b, c, d in planes]
    out = []
    for i in range(len(pl)):
        for j in range(i + 1, len(pl)):
            for k in range(j + 1, len(pl)):
                a, b, c = pl[i], pl[j], pl[k]
                det = a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])
                if abs(det) < 1e-9:
                    continue
                dx = a[3] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[3] * c[2] - b[2] * c[3]) + a[2] * (b[3] * c[1] - b[1] * c[3])
                dy = a[0] * (b[3] * c[2] - b[2] * c[3]) - a[3] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[3] - b[3] * c[0])
                dz = a[0] * (b[1] * c[3] - b[3] * c[1]) - a[1] * (b[0] * c[3] - b[3] * c[0]) + a[3] * (b[0] * c[1] - b[1] * c[0])
                p = (dx / det, dy / det, dz / det)
                if all(q[0] * p[0] + q[1] * p[1] + q[2] * p[2] <= q[3] + 1e-2 * max(1.0, abs(q[3])) for q in pl):
                    out.append(p)
    return out


def leaf_points(img, leaf):
    """Local points that bound a collision leaf: hull corners (op 10) or the vertices its triangles use (op 42). A trigger
    (op 11) is a BSP of planes with no vertex list: its box is not redone here (moving one shifts the old box)."""
    op = img.u32(leaf) & 63
    if op == 10:
        planes = hull_planes(img, leaf)
        if len(planes) > 40:
            return []           # plane triples grow as n^3: big hulls are left unchecked
        cx, cy, cz = img.f32(leaf + 0x10), img.f32(leaf + 0x14), img.f32(leaf + 0x18)
        inside_pos = sum(1 for a, b, c, d in planes if a * cx + b * cy + c * cz <= d + 1e-3)
        inside_neg = sum(1 for a, b, c, d in planes if a * cx + b * cy + c * cz + d <= 1e-3)
        return _hull_corners(planes, 1 if inside_pos >= inside_neg else -1)
    if op == 42:
        pts = []
        for k in range(min(img.u32(leaf + 4), 256)):
            o = img.u32(leaf + 8 + 4 * k)
            vp, npol, nbox = img.u32(o), img.u16(o + 4), img.u16(o + 6)
            polys = o + 0x18 + 14 * nbox
            used = {img.u8(polys + 8 * i + j) for i in range(npol) for j in range(3)}
            pts += [tuple(img.f32(vp + 12 * v + 4 * i) for i in range(3)) for v in sorted(used)]
        return pts
    return []


def aabb(pts):
    return ([min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]) if pts else None


class Path:
    """_HierCollisionPath: world box, flags (bit 0 = trigger) and the chain of nodes from a root GROUP down to the leaf."""

    def __init__(self, img, addr):
        self.addr = addr
        self.min = [img.f32(addr + 4 * k) for k in range(3)]
        self.max = [img.f32(addr + 0xC + 4 * k) for k in range(3)]
        self.length = struct.unpack_from("<i", img.d, addr + 0x1C - img.base)[0]
        self.trigger = img.u32(addr + 0x20) & 1
        self.nodes = [img.u32(addr + 0x24 + 4 * k) & MASK for k in range(max(0, min(self.length, 256)))]

    @property
    def leaf(self):
        return self.nodes[-1] if self.nodes else 0


class Grid:
    """_HierCollisionGrid (op 15, one per stage, a root of the image): cells of cellSize in XY, each a list of paths."""

    def __init__(self, img, addr):
        self.addr = addr
        self.cols, self.rows = img.s16(addr + 4), img.s16(addr + 6)
        self.cell, self.min_x, self.min_y, self.max_x, self.max_y = (img.f32(addr + 8 + 4 * k) for k in range(5))
        self.paths = {}                             # address -> Path
        self.cells_of = collections.defaultdict(set)  # path address -> cell indices it is listed in
        for i in range(self.cols * self.rows):
            ln, guard = img.u32(addr + 0x20 + 4 * i) & MASK, 0
            while ln and guard < 100000:
                p = img.u32(ln + 4) & MASK
                if p not in self.paths:
                    self.paths[p] = Path(img, p)
                self.cells_of[p].add(i)
                ln, guard = img.u32(ln) & MASK, guard + 1

    def cells_for_box(self, mn, mx):
        """Cells a world box covers (clamped to the grid), the rule the disc follows."""
        c0 = max(0, int(math.floor((mn[0] - self.min_x) / self.cell)))
        c1 = min(self.cols - 1, int(math.floor((mx[0] - self.min_x) / self.cell)))
        r0 = max(0, int(math.floor((mn[1] - self.min_y) / self.cell)))
        r1 = min(self.rows - 1, int(math.floor((mx[1] - self.min_y) / self.cell)))
        return {r * self.cols + c for r in range(r0, r1 + 1) for c in range(c0, c1 + 1)}


# ------------------------------------------------------------------------------------------------------------- stage
class Stage:
    def __init__(self, name, disc=None):
        self.name = name.upper()
        self.disc = disc or os.path.join(REPO, "disc")
        lvl = os.path.join(self.disc, "LVL", self.name)
        self.img = img = ngp.Image.load(lvl + ".NGP")
        self.ptr = read_ptr(lvl + ".PTR")
        self.end = B + len(img.d)
        self.roots = [r & MASK for r in img.roots()]
        self.parents = collections.defaultdict(list)
        self.root_of = {}
        for ri, r in enumerate(self.roots):
            stack = [r]
            while stack:
                n = stack.pop()
                if n in self.root_of:
                    continue
                self.root_of[n] = ri
                for c in children(img, n):
                    self.parents[c].append(n)
                    stack.append(c)
        self.world = next((r for r in self.roots if img.u32(r) >> 18 == 1 and img.u32(r) & 63 == 1), None)
        g = next((r for r in self.roots if img.u32(r) & 63 == 15), None)
        self.grid = Grid(img, g) if g else None
        self._acts()

    def op(self, n):
        return self.img.u32(n) & 63

    def ident(self, n):
        return self.img.u32(n) >> 18

    # --- actions of the destructible states (debris, transforms): data the .PTR pointer list does not cover
    def _acts(self):
        img = self.img
        self.acts = {}          # DESTRUCTIBLE -> [(address, size)]
        self.act_nodes = {}     # DESTRUCTIBLE -> debris nodes only its actions point at
        for n in list(self.root_of):
            if self.op(n) == 31:
                blocks, dn = [], []
                for k in range(min(img.u16(n + 0xA), 64)):
                    self._walk_act(img.u32(n + 0x10 + 12 * k), blocks, dn, set())
                self.acts[n], self.act_nodes[n] = blocks, dn
        for d, dn in self.act_nodes.items():
            for x in dn:
                if d not in self.parents[x]:
                    self.parents[x].append(d)
                for y in self.subtree(x):
                    if y not in self.root_of:
                        for c in children(img, y):
                            if y not in self.parents[c]:
                                self.parents[c].append(y)

    def _walk_act(self, a, blocks, dnodes, seen, depth=0):
        """ActHead: GROUP 0, TRANSFORM 1, DEBRIS 3, others 0x10 bytes (src/game/ActionDispatch.cpp, Debris.cpp)."""
        img = self.img
        a &= MASK
        if not a or a in seen or not img.ok(a, 8) or depth > 16:
            return
        seen.add(a)
        t = img.u16(a)
        if t == 0:
            n = min(img.u32(a + 8), 256)
            blocks.append((a, 0xC + 4 * n))
            for k in range(n):
                self._walk_act(img.u32(a + 0xC + 4 * k), blocks, dnodes, seen, depth + 1)
        elif t == 1:
            blocks.append((a, 0x50))
            self._walk_act(img.u32(a + 8), blocks, dnodes, seen, depth + 1)
        elif t == 3:
            n = min(img.u32(a + 8), 256)
            blocks.append((a, 0x10 + 0x30 * n))
            for k in range(n):
                h = img.u32(a + 0x10 + 0x30 * k) & MASK
                if img.ok(h):
                    dnodes.append(h)
        else:
            blocks.append((a, 0x10))

    def subtree(self, n, with_debris=False):
        seen, stack = set(), [n]
        while stack:
            x = stack.pop()
            if x in seen:
                continue
            seen.add(x)
            stack.extend(children(self.img, x))
            if with_debris and self.op(x) == 31:
                stack.extend(self.act_nodes.get(x, ()))
        return seen

    def chain(self, n):
        """Nodes from a root down to n, through each node's first parent."""
        out, seen = [n], {n}
        while self.parents.get(out[-1]):
            p = self.parents[out[-1]][0]
            if p in seen:
                break
            seen.add(p)
            out.append(p)
        return out[::-1]

    def chain_str(self, nodes):
        return " / ".join("%s%s" % (OPN.get(self.op(x), self.op(x)), ("#%x" % self.ident(x)) if self.ident(x) else "")
                          for x in nodes)

    # --- collision
    def paths_through(self):
        """node -> collision paths whose chain passes through it (what has to be redone when that node changes)."""
        idx = collections.defaultdict(list)
        if self.grid:
            for p in self.grid.paths.values():
                for n in p.nodes:
                    idx[n].append(p)
        return idx

    def path_box(self, p):
        """World box of a path by the disc's rule, or None when it cannot be redone (animated chain, no leaf points)."""
        if any(self.op(n) in (17, 25) for n in p.nodes):
            return None
        lb = aabb(leaf_points(self.img, p.leaf))
        if not lb:
            return None
        m = chain_matrix(self.img, p.nodes[:-1], with_scale=False)
        corners = [(x, y, z) for x in (lb[0][0], lb[1][0]) for y in (lb[0][1], lb[1][1]) for z in (lb[0][2], lb[1][2])]
        return aabb([ngp.xform(m, c) for c in corners])

    # --- placeable objects
    def objects(self):
        """destr: INTERACTIVE_STATE over a DESTRUCTIBLE; pickup: GROUP with id 0x12C0..0x13EB; prop: the topmost transform
        under a GROUP of the world root with an OBJECT below and no destructible or pickup inside. The handle is the
        transform right above (destr, pickup) or the node itself (prop)."""
        img = self.img
        through = self.paths_through()
        istates = sorted(n for n in self.root_of if self.op(n) == 39 and self.op(img.u32(n + 0xC) & MASK) == 31)
        pickups = sorted(n for n in self.root_of if self.op(n) == 1 and 0x12C0 <= self.ident(n) <= 0x13EB)
        claimed = set()
        for n in istates + pickups:
            claimed |= self.subtree(n)
        out = [self._describe(n, "destr", through) for n in istates]
        out += [self._describe(n, "pickup", through) for n in pickups]
        for n in sorted(self.subtree(self.world) if self.world else ()):
            if self.op(n) not in XFORM or n in claimed:
                continue
            ps = self.parents.get(n, [])
            if not ps or self.op(ps[0]) != 1 or any(self.op(a) in XFORM for a in self.chain(n)[:-1]):
                continue
            sub = self.subtree(n)
            if sub & claimed or not any(self.op(x) == 0 for x in sub):
                continue
            out.append(self._describe(n, "prop", through, handle=n))
        return out

    def _describe(self, node, kind, through, handle=None):
        img = self.img
        if handle is None:
            ps = self.parents.get(node, [])
            handle = ps[0] if ps and self.op(ps[0]) in XFORM else None
        chain = self.chain(handle or node)
        m = chain_matrix(img, chain)
        o = {"kind": kind, "node": node, "handle": handle, "id": self.ident(node),
             "pos": (m[3][0], m[3][1], m[3][2]), "heading": math.degrees(math.atan2(m[0][1], m[0][0])),
             "handle_op": OPN.get(self.op(handle)) if handle else "-",
             # a handle with other children moves them too: the editor inserts a TRANSLATE of its own
             "handle_shared": bool(handle and handle != node and len(children(img, handle)) > 1),
             "nested": any(self.op(a) == 39 for a in self.chain(node)[:-1]),
             "parents": len(self.parents.get(node, [])), "chain": self.chain_str(self.chain(node))}
        o["states"] = img.u16((img.u32(node + 0xC) & MASK) + 0xA) if kind == "destr" else 0
        sub = self.subtree(node if (o["handle_shared"] or not handle) else handle)
        c = collections.Counter(self.op(x) for x in sub)
        o.update(n_nodes=len(sub), n_obj=c[0], n_emit=c[22], n_anim=c[25],
                 n_paths=len({p.addr for x in sub for p in through.get(x, ())}))
        return o

    # --- bytes a copy of an object takes (deep copy for duplicating)
    def footprint(self, top):
        """Ranges of the image used only by this subtree: (bytes, [[start, end]], own nodes, shared nodes, outside refs).
        A range runs from each own node to the next known start and grows by the .PTR pointers inside it; targets used
        from outside the subtree (shared meshes, GS contexts) stay shared."""
        slots = sorted(B + s for s in self.ptr[0] if s)
        target = {s: self.img.u32(s) & MASK for s in slots}
        into = collections.defaultdict(list)
        for s, t in target.items():
            into[t].append(s)
        act_addrs = {a for bl in self.acts.values() for a, _ in bl}
        starts = sorted(set(target.values()) | set(self.roots) | set(self.root_of) | act_addrs)

        def next_start(a):
            i = bisect.bisect_right(starts, a)
            return starts[i] if i < len(starts) else self.end
        sub = self.subtree(top, with_debris=True)
        shared_top = {n for n in sub if n != top and any(p not in sub for p in self.parents.get(n, ()))}
        excl = set()
        for s in shared_top:
            excl |= self.subtree(s, with_debris=True)
        own = sub - excl
        act_blocks = {a: a + s for d in own if self.op(d) == 31 for a, s in self.acts.get(d, ())}
        blocked = set(excl)
        while True:
            spans, work = dict(act_blocks), list(own)
            while work:
                a = work.pop()
                if a in spans:
                    continue
                e = spans[a] = next_start(a)
                for s in slots[bisect.bisect_left(slots, a):bisect.bisect_left(slots, e)]:
                    t = target[s]
                    if t in blocked or t in spans or (t in self.root_of and t not in own):
                        continue
                    work.append(t)
            merged = []
            for a in sorted(spans):
                if merged and a <= merged[-1][1]:
                    merged[-1][1] = max(merged[-1][1], spans[a])
                else:
                    merged.append([a, spans[a]])
            heads = [r[0] for r in merged]

            def inside(x):
                i = bisect.bisect_right(heads, x) - 1
                return i >= 0 and merged[i][0] <= x < merged[i][1]
            ext, grow = [], set()
            for t in spans:
                for s in into.get(t, ()):
                    if not inside(s):
                        (ext.append((s, t)) if t in own else grow.add(t))
            if not grow:
                break
            blocked |= grow
        outside = [(s, t) for s, t in ext if t != top]
        return sum(e - a for a, e in merged), merged, own, shared_top | (blocked - excl), outside


# ------------------------------------------------------------------------------------------------------------- points
POINTS_MAGIC = 0x12282000
POINT_EXTS = ("SPT", "PUP", "FOP", "PED", "AIP")


def read_points(path):
    """PointToolKit::loadPoints: u32 version, u32 count, count x 64-byte records (position, links, per-format bytes)."""
    data = open(path, "rb").read()
    magic, n = struct.unpack_from("<II", data, 0)
    if magic != POINTS_MAGIC:
        raise ValueError("%s: version %08x" % (path, magic))
    out = []
    for i in range(n):
        r = data[8 + 64 * i: 8 + 64 * (i + 1)]
        out.append({"i": i, "pos": struct.unpack_from("<4f", r, 0), "nlinks": struct.unpack_from("<i", r, 0x10)[0],
                    "links": list(struct.unpack_from("<6i", r, 0x14)), "raw": r})
    return out


def describe_point(ext, p):
    r, n = p["raw"], min(p["nlinks"], 6)
    if ext == "SPT":
        ang = r[0x2D] * 360.0 / 255.0
        return "list %d, angle %.1f, ground %d" % (r[0x2C], ang - 360.0 if ang > 180.0 else ang, r[0x2E])
    if ext == "PUP":
        return "type %d, flags %#x" % (r[0x2C], r[0x2D])
    if ext in ("FOP", "PED"):
        return "node type %d, links %s" % (r[0x32], " ".join("%d(w%d,d%d)" % (p["links"][j] & 0xFFFF, r[0x2C + j], r[0x33 + j])
                                                              for j in range(n)))
    return "path type %d, links %s" % (r[0x32], " ".join("%d(w%d)" % (p["links"][j], r[0x2C + j]) for j in range(n)))


def stage_points(disc, name):
    out = {}
    for ext in POINT_EXTS:
        path = os.path.join(disc, "PTS", "%s.%s" % (name, ext))
        if os.path.exists(path):
            out[ext] = read_points(path)
    return out


# ------------------------------------------------------------------------------------------------------------- .MRS
def read_mrs(path):
    """resetcom settings file: 3 floats of header, then {s32 key, s32 size (with this 8-byte head), body}, key 1 ends."""
    data = open(path, "rb").read()
    off, out = 12, []
    while off + 8 <= len(data):
        key, size = struct.unpack_from("<ii", data, off)
        if key == 1 or size <= 0:
            break
        out.append((off, key, data[off + 8: off + size]))
        off += size
    return out


def describe_mrs(key, body):
    if key == 2:
        return "background rgba %s" % (struct.unpack_from("<4i", body),)
    if key == 3:
        return "ambient rgb (%.3f %.3f %.3f)" % struct.unpack_from("<3f", body)
    if key == 4:
        f = struct.unpack_from("<4f3i", body)
        return "fog min %.0f max %.0f maxV %.2f far clip %.0f rgb %s" % (f[0], f[1], f[2], f[3], f[4:])
    if key == 5:
        v = struct.unpack_from("<15f", body)
        return "; ".join("light %d rgb (%.2f %.2f %.2f) heading %.1f pitch %.1f" % (i, v[i], v[3 + i], v[6 + i], v[9 + i], v[12 + i])
                         for i in range(3))
    if key == 7:
        return "time of day: speed %.2f, tod %d, time %.2f" % struct.unpack_from("<fif", body)
    if key == 0xBBA:
        return "default ground height %.1f" % struct.unpack_from("<f", body)
    names = {0xBC2: "dust lighting", 0xBC3: "trail coloring", 0xBC4: "frag coloring"}
    return "%s %s" % (names.get(key, "key %#x" % key), body.hex() if key in names else "(%d bytes)" % len(body))


# ------------------------------------------------------------------------------------------------------------- commands
def cmd_summary(disc, names):
    print("%-9s %8s %7s %6s %6s %5s %5s %9s %6s  %s" % ("stage", "image", "free MB", "destr", "pickup", "prop", "s/hnd",
                                                       "grid", "paths", "points"))
    for name in names:
        st = Stage(name, disc)
        objs = st.objects()
        k = collections.Counter(o["kind"] for o in objs)
        no_handle = sum(1 for o in objs if o["kind"] == "destr" and (not o["handle"] or o["handle_shared"]))
        g = st.grid
        pts = stage_points(disc, st.name)
        print("%-9s %8d %7.1f %6d %6d %5d %5d %9s %6d  %s" % (
            st.name, len(st.img.d), (NGP_CEILING - st.end) / 1048576.0, k["destr"], k["pickup"], k["prop"], no_handle,
            "%dx%d" % (g.cols, g.rows) if g else "-", len(g.paths) if g else 0,
            " ".join("%s %d" % (e, len(v)) for e, v in pts.items())))


def cmd_objects(disc, name, csv_path=None):
    st = Stage(name, disc)
    objs = st.objects()
    for o in objs:
        print("%-6s node %07x handle %-9s %07x id %#06x pos (%8.1f %8.1f %7.1f) heading %6.1f states %d paths %3d%s%s" % (
            o["kind"], o["node"], o["handle_op"], o["handle"] or 0, o["id"], o["pos"][0], o["pos"][1], o["pos"][2], o["heading"],
            o["states"], o["n_paths"], " shared-handle" if o["handle_shared"] else "", " nested" if o["nested"] else ""))
    if csv_path:
        keys = ["kind", "node", "handle", "handle_op", "id", "pos", "heading", "states", "handle_shared", "nested", "parents",
                "n_nodes", "n_obj", "n_emit", "n_anim", "n_paths", "chain"]
        with open(csv_path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(keys)
            for o in objs:
                w.writerow(["%x" % o[k] if k in ("node", "handle") and o[k] else
                            "%.1f;%.1f;%.1f" % o[k] if k == "pos" else o[k] for k in keys])
    print("%d objects: %s" % (len(objs), dict(collections.Counter(o["kind"] for o in objs))))


def cmd_collision(disc, name, check=False):
    st = Stage(name, disc)
    g = st.grid
    if not g:
        print("%s: no collision grid" % st.name)
        return
    leaves = collections.Counter(OPN.get(st.op(p.leaf), st.op(p.leaf)) for p in g.paths.values())
    lists = sum(len(c) for c in g.cells_of.values())
    print("%s: grid %07x, %d x %d cells of %.0f, x [%.0f, %.0f] y [%.0f, %.0f]" % (
        st.name, g.addr, g.cols, g.rows, g.cell, g.min_x, g.max_x, g.min_y, g.max_y))
    print("  %d paths (%d triggers), %d cell entries; leaves %s" % (
        len(g.paths), sum(p.trigger for p in g.paths.values()), lists, dict(leaves)))
    if not check:
        return
    box_ok = box_bad = cell_ok = 0
    skipped = collections.Counter()
    worst = (0.0, 0)
    for p in g.paths.values():
        if g.cells_for_box(p.min, p.max) == g.cells_of[p.addr]:
            cell_ok += 1
        bb = st.path_box(p)
        if bb is None:
            animated = any(st.op(n) in (17, 25) for n in p.nodes)
            skipped["animated" if animated else "%s leaf" % OPN.get(st.op(p.leaf), st.op(p.leaf))] += 1
            continue
        e = max(max(abs(bb[0][i] - p.min[i]), abs(bb[1][i] - p.max[i])) for i in range(3))
        worst = max(worst, (e, p.addr))
        if e < 1.01:
            box_ok += 1
        else:
            box_bad += 1
    print("  box rule: %d ok (error <= 1), %d off, not redone %s; worst %.2f at %07x" % (
        box_ok, box_bad, dict(skipped) or 0, worst[0], worst[1]))
    print("  cell rule: %d of %d paths listed in exactly the cells their box covers" % (cell_ok, len(g.paths)))


def cmd_points(disc, name):
    for ext, pts in stage_points(disc, name.upper()).items():
        print("== %s.%s: %d points" % (name.upper(), ext, len(pts)))
        for p in pts:
            x, y, z, w = p["pos"]
            print("  %3d (%8.1f %8.1f %7.1f | w %5.1f) %s" % (p["i"], x, y, z, w, describe_point(ext, p)))


def cmd_mrs(disc, name):
    path = os.path.join(disc, "RST", name.upper() + ".MRS")
    for off, key, body in read_mrs(path):
        print("  +%04X %s" % (off, describe_mrs(key, body)))


def main(argv):
    disc = os.path.join(REPO, "disc")
    if "--disc" in argv:
        i = argv.index("--disc")
        disc = argv[i + 1]
        del argv[i:i + 2]
    csv_path = None
    if "--csv" in argv:
        i = argv.index("--csv")
        csv_path = argv[i + 1]
        del argv[i:i + 2]
    flags = {a for a in argv if a.startswith("--")}
    args = [a for a in argv if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 1
    cmd, names = args[0], [a.upper() for a in args[1:]]
    if cmd == "summary":
        cmd_summary(disc, names or sorted(f[:-4].upper() for f in os.listdir(os.path.join(disc, "LVL"))
                                          if f.upper().endswith(".NGP")))
    elif cmd in ("objects", "collision", "points", "mrs") and len(names) == 1:
        if cmd == "objects":
            cmd_objects(disc, names[0], csv_path)
        elif cmd == "collision":
            cmd_collision(disc, names[0], "--check" in flags)
        elif cmd == "points":
            cmd_points(disc, names[0])
        else:
            cmd_mrs(disc, names[0])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
