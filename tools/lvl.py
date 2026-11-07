#!/usr/bin/env python3
"""A stage seen as the level editor sees it: placeable objects, collision and the per-stage points.

  python tools/lvl.py summary [STAGE...]              # one line per stage (all of disc/LVL by default)
  python tools/lvl.py objects CENTRAL [--csv out.csv] # destructibles, pickups and props, one per line
  python tools/lvl.py collision CENTRAL [--check]     # grid and paths; --check redoes every path's box and cells
  python tools/lvl.py points CENTRAL                  # PTS/<STAGE>.SPT/.PUP/.FOP/.PED/.AIP
  python tools/lvl.py mrs CENTRAL                     # RST/<STAGE>.MRS (background, fog, lights, time of day)
  python tools/lvl.py peds CENTRAL                    # pedestrian groups (0xBB8 roots) and the segments they walk
  python tools/lvl.py patch CENTRAL mods/X/levels/CENTRAL.json  # apply a level patch to the image and check its collision
  options: --disc DIR (default: disc/ at the repo root); patch --shallow copies the way WOTM_LEVELPATCH_SHALLOW=1 does;
  patch --mod DIR: the mod whose props/ the "props" models come from (default: the folder above the JSON's levels/)

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
import json
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
def child_slots(img, n):
    """Addresses of a node's child pointers, as dbsTraverse walks them, plus the DESTRUCTIBLE states and both CONTROL children."""
    u32, u16, u8 = img.u32, img.u16, img.u8
    op = u32(n) & 63
    # (first slot, where the count is, its width): read only for this op, so a node at the very end of the image is fine
    arrays = {1: (0x20, 8, 2), 3: (0x1C, 8, 4), 4: (0x50, 8, 4), 30: (0x2C, 8, 4), 6: (0xC, 0xB, 1),
              35: (0xC, 4, 4), 25: (0x2C, 0x28, 4), 17: (0xCC, 4, 4), 38: (0x60, 4, 4)}
    if op in arrays:
        off, at, width = arrays[op]
        cnt = {1: u8, 2: u16, 4: u32}[width](n + at)
        return [n + off + 4 * k for k in range(min(cnt, 8192))]
    if op == 2:
        return [n + 0x28 + 16 * k for k in range(min(u32(n + 4), 16))]
    if op == 8:
        return [n + 4, n + 8]
    if op == 23:
        return [n + 4]
    if op == 39:
        return [n + 0xC]
    if op == 31:
        return [n + 0xC + 12 * k for k in range(min(u16(n + 0xA), 64))]
    return []


def children(img, n):
    out = [img.u32(s) for s in child_slots(img, n)]
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
        c = self.path_corners(p)
        return aabb(c) if c else None

    def path_corners(self, p):
        """World corners of a path's leaf box (the leaf's local box through the chain without scale), or None."""
        if any(self.op(n) in (17, 25) for n in p.nodes):
            return None
        lb = aabb(leaf_points(self.img, p.leaf))
        if not lb:
            return None
        m = chain_matrix(self.img, p.nodes[:-1], with_scale=False)
        corners = [(x, y, z) for x in (lb[0][0], lb[1][0]) for y in (lb[0][1], lb[1][1]) for z in (lb[0][2], lb[1][2])]
        return [ngp.xform(m, c) for c in corners]

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

    def on_top(self, objs, tol=25.0):
        """{pickup handle: handle of the destr/prop it rests on}: the pickup's position is inside the XY hull of one of the
        object's solid collision boxes and within `tol` of that box's top. Rooftop water tanks, vents and antennas are pickups of
        their own and do not follow their building; an editor moves, turns, copies and deletes them with it."""
        through = self.paths_through()
        tops = []
        for o in objs:
            if o["kind"] == "pickup" or not o["handle"]:
                continue
            seen = set()
            for x in self.subtree(o["handle"]):
                for p in through.get(x, ()):
                    if p.addr in seen or p.trigger:
                        continue
                    seen.add(p.addr)
                    c = self.path_corners(p)
                    if c:
                        tops.append((o["handle"], hull2d(c), max(q[2] for q in c)))
        out = {}
        for o in objs:
            if o["kind"] != "pickup" or not o["handle"]:
                continue
            x, y, z = o["pos"]
            for h, poly, top in tops:
                if abs(z - top) <= tol and seg_in_poly((x, y), (x, y), poly, 1.0):
                    out[o["handle"]] = h
                    break
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


# ------------------------------------------------------------------------------------------------------------- level patch
def inv_affine(m):
    """Inverse of a row-vector affine matrix (rows 0..2 linear, row 3 translation)."""
    a = [r[:3] for r in m[:3]]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    if abs(det) < 1e-12:
        raise PatchError("singular matrix")
    i = [[(a[1][1] * a[2][2] - a[1][2] * a[2][1]) / det, (a[0][2] * a[2][1] - a[0][1] * a[2][2]) / det,
          (a[0][1] * a[1][2] - a[0][2] * a[1][1]) / det],
         [(a[1][2] * a[2][0] - a[1][0] * a[2][2]) / det, (a[0][0] * a[2][2] - a[0][2] * a[2][0]) / det,
          (a[0][2] * a[1][0] - a[0][0] * a[1][2]) / det],
         [(a[1][0] * a[2][1] - a[1][1] * a[2][0]) / det, (a[0][1] * a[2][0] - a[0][0] * a[2][1]) / det,
          (a[0][0] * a[1][1] - a[0][1] * a[1][0]) / det]]
    t = m[3][:3]
    return [i[0] + [0.0], i[1] + [0.0], i[2] + [0.0], [-sum(t[k] * i[k][c] for k in range(3)) for c in range(3)] + [1.0]]


def _maxscale(m):
    return math.sqrt(max(sum(m[r][c] ** 2 for c in range(3)) for r in range(3)))


def _union(c1, r1, c2, r2):
    """Smallest sphere holding both."""
    d = math.dist(c1, c2)
    if d + r2 <= r1:
        return list(c1), r1
    if d + r1 <= r2:
        return list(c2), r2
    r = (d + r1 + r2) / 2
    k = (r - r1) / d
    return [c1[i] + (c2[i] - c1[i]) * k for i in range(3)], r


class PatchError(Exception):
    pass


def hull2d(pts):
    """Convex hull of the XY of pts, counter-clockwise (monotone chain)."""
    p = sorted(set((round(q[0], 3), round(q[1], 3)) for q in pts))
    if len(p) < 3:
        return p

    def half(seq):
        out = []
        for q in seq:
            while len(out) >= 2 and (out[-1][0] - out[-2][0]) * (q[1] - out[-2][1]) - \
                    (out[-1][1] - out[-2][1]) * (q[0] - out[-2][0]) <= 0:
                out.pop()
            out.append(q)
        return out[:-1]
    return half(p) + half(p[::-1])


def seg_in_poly(a, b, poly, margin):
    """Part [t0, t1] of segment a->b (XY) inside the convex CCW polygon grown by `margin` (each edge pushed out), or None."""
    if len(poly) < 3:
        return None
    t0, t1 = 0.0, 1.0
    dx, dy = b[0] - a[0], b[1] - a[1]
    for i in range(len(poly)):
        p, q = poly[i], poly[(i + 1) % len(poly)]
        nx, ny = q[1] - p[1], p[0] - q[0]
        ln = math.hypot(nx, ny)
        if ln < 1e-9:
            continue
        nx, ny = nx / ln, ny / ln
        f = nx * (a[0] - p[0]) + ny * (a[1] - p[1]) - margin     # inside while f + t*g <= 0
        g = nx * dx + ny * dy
        if abs(g) < 1e-12:
            if f > 0:
                return None
            continue
        t = -f / g
        if g > 0:
            t1 = min(t1, t)
        else:
            t0 = max(t0, t)
        if t0 >= t1:
            return None
    return t0, t1


GROUND_NODE_TYPES = (0, 3)      # PathNet node types that drive on the ground (car, tank/truck); 1 and 2 fly
PED_MIN_LEN = 30.0              # a pedestrian piece shorter than this after clipping is dropped
PED_MARGIN = 15.0               # pedestrians: distance kept from a wall (the disc's own segments run right along walls)
PED_WALL = 40.0                 # pedestrians: a box that rises less than this above the segment is walked over (curbs, pads)
NET_NODES = 255                 # PathNet holds 256 nodes; the tool loads at most 255


def cut_nets(fps, nets):
    """Vehicles and pedestrians walk PathNet::fodder and ::ped without looking at the collision. For each solid box placed by
    the patch (`fps`), cut the vehicle links whose lane (width*10 to each side) crosses it, both ways (PathNode::removePath).
    Pedestrians: only walls cut (boxes rising PED_WALL above the segment), with PED_MARGIN; each segment (a pair of nodes linked
    to each other) keeps its free parts, the first in its own nodes and the others as new pairs, and the pedestrian nodes left
    with no link leave the list (PedGroup reads paths[0] of the nearest node without looking at the count).
    nets: {"fodder"|"ped": [{"pos": [x, y, z], "type": t, "links": [[node, width, drop], ...]}]}, changed in place."""
    log = []

    def covered(a, b, margin, wall=None):
        out = []
        for poly, zmin, zmax in fps:
            if max(a[2], b[2]) < zmin - 50.0 or min(a[2], b[2]) > zmax:
                continue
            if wall is not None and zmax - max(a[2], b[2]) < wall:
                continue
            iv = seg_in_poly(a, b, poly, margin)
            if iv:
                out.append(iv)
        return out

    def unlink(nodes, i, k):
        nodes[i]["links"] = [l for l in nodes[i]["links"] if l[0] != k]
        nodes[k]["links"] = [l for l in nodes[k]["links"] if l[0] != i]

    nodes = nets.get("fodder") or []
    cut = []
    for i, n in enumerate(nodes):
        if n["type"] not in GROUND_NODE_TYPES:
            continue
        for k, w, _ in n["links"]:
            if k < len(nodes) and (k > i or not any(l[0] == i for l in nodes[k]["links"])) and \
                    covered(n["pos"], nodes[k]["pos"], w * 10.0):
                cut.append((i, k))
    for i, k in cut:
        unlink(nodes, i, k)
        log.append("vehicles: link %d-%d cut" % (i, k))

    nodes = nets.get("ped") or []
    for i in range(len(nodes)):
        n = nodes[i]
        for k, w, d in list(n["links"]):
            if k >= len(nodes) or k < i and any(l[0] == i for l in nodes[k]["links"]):
                continue
            a, b = n["pos"], nodes[k]["pos"]
            ivs = covered(a, b, PED_MARGIN, PED_WALL)
            if not ivs:
                continue
            pair = len(n["links"]) == 1 and [l[0] for l in nodes[k]["links"]] == [i]
            seg = math.dist(a, b)
            gaps, t = [], 0.0
            for t0, t1 in sorted(ivs):
                if t0 > t:
                    gaps.append((t, t0))
                t = max(t, t1)
            if t < 1.0:
                gaps.append((t, 1.0))
            gaps = [g for g in gaps if (g[1] - g[0]) * seg >= PED_MIN_LEN] if pair else []
            if not gaps:
                unlink(nodes, i, k)
                log.append("pedestrians: segment %d-%d removed" % (i, k))
                continue
            at = lambda t: [a[j] + t * (b[j] - a[j]) for j in range(3)]     # noqa: E731
            n["pos"], nodes[k]["pos"] = at(gaps[0][0]), at(gaps[0][1])
            pieces = ["%.0f" % ((gaps[0][1] - gaps[0][0]) * seg)]
            for g in gaps[1:]:
                if len(nodes) + 2 > NET_NODES:
                    break
                m = len(nodes)
                nodes.append({"pos": at(g[0]), "type": n["type"], "links": [[m + 1, w, d]], "copy_of": i})
                nodes.append({"pos": at(g[1]), "type": n["type"], "links": [[m, w, d]], "copy_of": k})
                pieces.append("%.0f (new %d-%d)" % ((g[1] - g[0]) * seg, m, m + 1))
            log.append("pedestrians: segment %d-%d (%.0f) clipped to %s" % (i, k, seg, " + ".join(pieces)))
    keep = [i for i, n in enumerate(nodes) if n["links"]]
    if len(keep) < len(nodes):
        log.append("pedestrians: %d nodes removed, %d left" % (len(nodes) - len(keep), len(keep)))
        remap = {old: new for new, old in enumerate(keep)}
        nodes[:] = [nodes[i] for i in keep]
        for n in nodes:
            n["links"] = [[remap[l[0]], l[1], l[2]] for l in n["links"] if l[0] in remap]
    return log


def net_from_points(pts):
    """PathTool::exportPoints, read back from the file's records: position, node type, links {node, width, drop}."""
    return [{"pos": list(p["pos"][:3]), "type": p["raw"][0x32],
             "links": [[p["links"][j] & 0xFFFF, p["raw"][0x2C + j], p["raw"][0x33 + j]] for j in range(min(p["nlinks"], 6))]}
            for p in pts]


# ------------------------------------------------------------------------------------------------------------- pedestrians
PED_GROUP_ID = 0xBB8            # a root TRANSLATE with this id builds a PedGroup (dbProcInteractive): head bits 7..14 = how many
PED_MAX_GROUPS = 20             # PedGroup::s_allPedGroups[20], unchecked: a 21st group writes over PathNet::ped's header
PED_MAX_SEGMENTS = NET_NODES // 2
# PathNet::ped with no .PED file: two nodes at z 250 with a 250 lane (2500 to each side); a group bound to it scatters
DEFAULT_PED_NET = [{"pos": [-100.0, 0.0, 250.0], "type": 0, "links": [[1, 250, 0]]},
                   {"pos": [100.0, 0.0, 250.0], "type": 0, "links": [[0, 250, 0]]}]


def ped_groups(st):
    """The stage's pedestrian groups: [{"node", "pos", "count"}], one per root TRANSLATE with id 0xBB8 (its only child is a NULL;
    no parent, no collision; it exists in every mode)."""
    img = st.img
    out = []
    for r in st.roots:
        w = img.u32(r)
        if w >> 18 == PED_GROUP_ID and w & 63 == 3:
            out.append({"node": r, "pos": [img.f32(r + 0x10 + 4 * i) for i in range(3)], "count": (w >> 7) & 0xFF})
    return out


def ped_bind(net, pos):
    """What PedGroup's constructor walks: the node nearest to the group (3D distance, any node type, the first of equals) and
    its link 0, whatever its count. (a, b), or None when that node has no link; an empty net is PathNet's default one."""
    nodes = net or DEFAULT_PED_NET
    best = None
    for i, n in enumerate(nodes):
        d = math.dist(n["pos"], pos)
        if best is None or d < best[0]:
            best = (d, i)
    if best is None or not nodes[best[1]]["links"]:
        return None
    return best[1], nodes[best[1]]["links"][0][0]


def ped_net_segments(net):
    """The pedestrian net as segments {"a", "b", "width", "i", "k"}: each node's link 0 (what a group walks), a pair once."""
    out, seen = [], set()
    for i, n in enumerate(net):
        if not n["links"]:
            continue
        k, w = n["links"][0][0], n["links"][0][1]
        if k >= len(net) or (min(i, k), max(i, k)) in seen:
            continue
        seen.add((min(i, k), max(i, k)))
        out.append({"a": list(n["pos"]), "b": list(net[k]["pos"]), "width": w, "i": i, "k": k})
    return out


ARENA_SIZE = 0x100000           # the hook's arena for new nodes (wotm_mods_hooks::kLevelArenaSize)
ANIM_OPS = (25, 17, 35, 38)     # CHAR_INSTANCE, ANIM_XFORM, SKEL_BONE, ANIM_SCALE: the animated parts
ANIM_STATE_OPS = (17, 38)       # ANIM_XFORM, ANIM_SCALE: keep per-instance state (visible, rotation, dirty bits) of their CHAR_INSTANCE
ID2_MAX = 0x7FF                 # ActAnim::ids.anim and _hierhead::id2 have 11 bits
ACTANIM_MONSTER = 0x1800        # ACTANIM_PLAYER | ACTANIM_AI: the target is a monster
CB_OPS = (0, 1, 2, 3, 4, 6, 11, 17, 30, 40, 41)   # dbsTraverse calls dbProcInteractive for these when they have an id
PICKUP_IDS = (0x12C0, 0x13EC)   # a node with an id here is a pickup (one per instance: LevelPickups::createPickup)
EXCLUDED_IDS = (0x7D0, 0x898)   # ids dbProcInteractive ignores
KEEP_DESTR_IDS = (0x1A90, 0x1B58)   # DESTRUCTIBLE id1 here: never loses health (Destructible::takeHit)
GLOBAL_ACTS = (2, 12, 14)       # ANIM, RUNSCRIPT, GENERICEVENT: they act on something fixed in the level, so a copy drops them
DOMINO_OFF = 6                  # an action type DelayAction::doIt skips: what a DOMINO aimed at a deleted object becomes
INTERACTIVES = 1024            # Interactives::s_dbInteractive, unchecked
INTERACTIVE_SPARE = 32          # monsters, helicopters, bugs: what the budget keeps free besides slot 0


def node_size(img, n):
    """Bytes the deep copy clones for node n, or None for an op it does not know (or with more slots than child_slots walks)."""
    o = img.u32(n) & 63
    if o == 1:
        return 0x20 + 4 * img.u16(n + 8)
    if o in (3, 4, 30):
        k = img.u32(n + 8)
        return None if k > 8192 else {3: 0x1C, 4: 0x50, 30: 0x2C}[o] + 4 * k
    if o == 6:
        return 0xC + 4 * img.u8(n + 0xB)
    if o == 2:
        k = img.u32(n + 4)
        return None if k > 16 else 0x20 + 16 * k
    if o == 8:
        return max(0x30, 0x20 + 4 * img.u8(n + 0xC))
    if o == 39:
        return 16
    if o == 31:
        k = img.u16(n + 0xA)
        return None if k > 64 else 0xC + 12 * k
    if o == 22:
        return 0xB0
    if o in (25, 17, 38):
        k = img.u32(n + (0x28 if o == 25 else 4))
        return None if k > 8192 else {25: 0x2C, 17: 0xCC, 38: 0x60}[o] + 4 * k
    return None


def act_blocks(img, d):
    """The action blocks of DESTRUCTIBLE d's states, each once: [(address, type, size)] (GROUP 0, TRANSFORM 1, DEBRIS 3,
    others 0x10; src/game/ActionDispatch.cpp, Debris.cpp)."""
    out, seen = [], set()

    def walk(a, depth):
        a &= MASK
        if not a or a in seen or not img.ok(a, 8) or depth > 16:
            return
        seen.add(a)
        t = img.u16(a)
        if t == 0:
            n = min(img.u32(a + 8), 256)
            out.append((a, t, 0xC + 4 * n))
            for k in range(n):
                walk(img.u32(a + 0xC + 4 * k), depth + 1)
        elif t == 1:
            out.append((a, t, 0x50))
            walk(img.u32(a + 8), depth + 1)
        elif t == 3:
            out.append((a, t, 0x10 + 0x30 * min(img.u32(a + 8), 256)))
        else:
            out.append((a, t, 0x10))
    for k in range(min(img.u16(d + 0xA), 64)):
        walk(img.u32(d + 0x10 + 12 * k), 0)
    return out


def debris_pickups(img, d):
    """Debris chunks of DESTRUCTIBLE d that turn into a pickup when they settle (each takes an Interactives slot then)."""
    n = 0
    for a, t, _ in act_blocks(img, d):
        if t == 3:
            for k in range(min(img.u32(a + 8), 256)):
                n += (img.u16(a + 0x10 + 0x30 * k + 0xE) >> 5) & 31 != 0
    return n


def _number(x):
    """What the hook's json isInt() takes: a number or a bool."""
    return isinstance(x, (int, float))


def _child_nocase(d, name):
    """The entry of directory d named `name` without regard to case (the hook's childNoCase), or None."""
    try:
        for f in sorted(os.listdir(d)) if d else ():
            if f.upper() == name.upper():
                return os.path.join(d, f)
    except OSError:
        pass
    return None


def registers_id(img, n):
    """The node has an id dbProcInteractive acts on (besides INTERACTIVE_STATE and DESTRUCTIBLE, which always register)."""
    i, o = img.u32(n) >> 18, img.u32(n) & 63
    return o in CB_OPS and i != 0 and not EXCLUDED_IDS[0] <= i < EXCLUDED_IDS[1]


class LevelPatch:
    """A level patch (mods/<mod>/levels/<STAGE>.json, docs/MODS.md "Level patches") applied to the stage image the way
    native/runtime/wotm_levelpatch.inc applies it at the entry of dbInitDb: each entry names an object by its handle (the
    TRANSLATE/ROTATE `lvl.py objects` prints) and its position on the disc ("from"), and moves, turns, deletes or copies it.
    Then the collision paths through it get their box and cell lists redone, and the GROUPs above it get their spheres grown.
    New nodes go to an arena; here it is appended to the image."""

    FROM_TOL = 0.5

    def __init__(self, st, shallow=False):
        self.st, self.img = st, st.img
        self.img.d = bytearray(self.img.d)
        # the arena starts 0x80-aligned, like the hook's (a prop's packets are 0x80-aligned): the same allocations take the
        # same bytes on both sides
        self.img.d.extend(bytes((-st.end) % 0x80))
        self.arena = st.end + (-st.end) % 0x80
        self.cells = {a: set(c) for a, c in st.grid.cells_of.items()}
        self.free = []
        self.log = []
        self.redone = {}            # path -> (box, cells, how)
        self.footprints = []        # (convex XY polygon, zmin, zmax) of every solid box in its new place
        self.stats = collections.Counter()
        self.shallow = shallow      # WOTM_LEVELPATCH_SHALLOW: copies share all but the handle and the top state pair
        self.wraps = []             # GROUPs the copies made: child 0 is the original, the others its copies
        self.added_paths = []
        self.through = set()        # GROUPs the patch put between two nodes of a path: the copies', their clones, the pedestrians'
        self.unlinked_before = {pa for pa in self.cells if not self.path_linked(pa)}   # as on the disc: check_copies skips them
        # the stage's ANIM_MGR root (animationSetGlobal reads its list after the hook), id1 -> id2 of the CHAR_INSTANCEs registered and
        # cloned, and the cloned ones not in the ANIM_MGR yet
        self.mgr = next((r for r in st.roots if self.op(r) == 7), 0)
        self.id2_used = collections.defaultdict(set)
        for ci in self.instances():
            self.id2_used[st.ident(ci)].add((self.img.u32(ci) >> 7) & ID2_MAX)
        self.anim_added = []
        self.interactives_before = self.interactives()
        self.interactives_used = sum(self.interactives_before)

    # --- memory
    def w32(self, a, v):
        struct.pack_into("<I", self.img.d, a - self.img.base, v & 0xFFFFFFFF)

    def w16(self, a, v):
        struct.pack_into("<H", self.img.d, a - self.img.base, v & 0xFFFF)

    def wf32(self, a, v):
        struct.pack_into("<f", self.img.d, a - self.img.base, v)

    def set_ptr(self, slot, target):
        self.w32(slot, (self.img.u32(slot) & ~MASK) | target)

    def arena_free(self):
        return self.arena + ARENA_SIZE - (self.img.base + len(self.img.d))

    def arena_top(self):
        return self.img.base + len(self.img.d)

    def alloc(self, size, align=4):
        d = self.img.d
        pad = (-(self.img.base + len(d))) % align
        if pad + size > self.arena_free():
            raise PatchError("the 1 MB arena is full")
        d.extend(bytes(pad + size))
        self.stats["arena bytes"] = self.img.base + len(d) - self.arena
        return self.img.base + len(d) - size

    def rollback(self, top):
        """Gives back what the arena took since arena_top() was `top` (an entry refused after its first allocation)."""
        del self.img.d[top - self.img.base:]
        self.stats["arena bytes"] = top - self.arena

    def op(self, n):
        return self.img.u32(n) & 63

    def world(self, n, with_scale=True):
        return chain_matrix(self.img, self.st.chain(n), with_scale)

    # --- collision
    def paths_through(self, n):
        return [a for a in sorted(self.cells) if n in Path(self.img, a).nodes]

    def _head(self, c):
        return self.st.grid.addr + 0x20 + 4 * c

    def _unlink(self, c, pa):
        img, slot = self.img, self._head(c)
        ln = img.u32(slot) & MASK
        while ln:
            if img.u32(ln + 4) & MASK == pa:
                self.w32(slot, img.u32(ln))
                self.free.append(ln)
                return
            slot, ln = ln, img.u32(ln) & MASK
        raise PatchError("path %07x missing from cell %d" % (pa, c))

    def _link(self, c, pa):
        if self.free:
            ln = self.free.pop()
        else:
            ln = self.alloc(8)
            self.stats["new list nodes"] += 1
        self.w32(ln, self.img.u32(self._head(c)))
        self.w32(ln + 4, pa)
        self.w32(self._head(c), ln)

    def set_cells(self, pa, new):
        old = self.cells.get(pa, set())
        for c in sorted(old - new):
            self._unlink(c, pa)
        for c in sorted(new - old):
            self._link(c, pa)
        if new:
            self.cells[pa] = set(new)
        else:
            self.cells.pop(pa, None)

    def redo(self, pa, carry):
        """New box and cells of path pa: by the disc's rule when the leaf allows it, else its old box carried by `carry`."""
        p = Path(self.img, pa)
        wc, how = self.st.path_corners(p), "rule"
        if wc is None:
            corners = [(x, y, z) for x in (p.min[0], p.max[0]) for y in (p.min[1], p.max[1]) for z in (p.min[2], p.max[2])]
            wc, how = [ngp.xform(carry, c) for c in corners], "carried"
        bb = aabb(wc)
        if not p.trigger:
            self.footprints.append((hull2d(wc), bb[0][2], bb[1][2]))
        for i in range(3):
            self.wf32(pa + 4 * i, bb[0][i])
            self.wf32(pa + 0xC + 4 * i, bb[1][i])
        cells = self.st.grid.cells_for_box(bb[0], bb[1])
        self.set_cells(pa, cells)
        self.redone[pa] = (bb, len(cells), how)
        self.stats["paths redone"] += 1

    # --- drawing spheres
    def obj_sphere(self, h):
        """Local sphere around what hangs under handle h: the first GROUPs below it, through INTERACTIVE_STATE and DESTRUCTIBLE."""
        img, out, stack = self.img, [], list(children(self.img, h))
        while stack:
            x = stack.pop()
            o = self.op(x)
            if o == 1:
                out.append(([img.f32(x + 0x10 + 4 * i) for i in range(3)], img.f32(x + 0x1C)))
            elif o in (39, 31):
                stack.extend(children(img, x))
            elif o != 9:
                return None
        if not out:
            return None
        c, r = out[0]
        for c2, r2 in out[1:]:
            c, r = _union(c, r, c2, r2)
        return c, r

    def grow_spheres(self, h, old_pos):
        """The drawing skips a GROUP whose sphere is out of view: grow the ones above h to keep covering what hangs under it."""
        img, st = self.img, self.st
        W = self.world(h)
        groups = [g for g in st.chain(h)[:-1] if self.op(g) == 1]
        s = self.obj_sphere(h)
        if s:
            co, ro = list(ngp.xform(W, s[0])), s[1] * _maxscale(W)
        else:   # nothing to measure: the nearest GROUP's sphere, carried from the old place
            g = groups[-1]
            M = chain_matrix(img, st.chain(g)[:-1])
            cw = ngp.xform(M, [img.f32(g + 0x10 + 4 * i) for i in range(3)])
            co, ro = list(W[3][:3]), img.f32(g + 0x1C) * _maxscale(M) + math.dist(cw, old_pos)
        for g in groups:
            M = chain_matrix(img, st.chain(g)[:-1])
            k = _maxscale(M)
            cw, rw = ngp.xform(M, [img.f32(g + 0x10 + 4 * i) for i in range(3)]), img.f32(g + 0x1C) * k
            c, r = _union(cw, rw, co, ro)
            if r <= rw + 1e-3:
                continue
            lc, lr = ngp.xform(inv_affine(M), c), r / k + 0.5
            for i in range(3):
                self.wf32(g + 0x10 + 4 * i, lc[i])
            self.wf32(g + 0x1C, lr)
            self.wf32(g + 0xC, lr * lr)
            self.stats["spheres grown"] += 1

    # --- handles
    def replace(self, old, new):
        """Put node `new` where `old` was: in its parent's child list and in every collision path."""
        img, st = self.img, self.st
        for pa in self.paths_through(old):
            for k in range(Path(img, pa).length):
                if img.u32(pa + 0x24 + 4 * k) & MASK == old:
                    self.set_ptr(pa + 0x24 + 4 * k, new)
        p = st.parents[old][0]
        for s in child_slots(img, p):
            if img.u32(s) & MASK == old:
                self.set_ptr(s, new)
        st.parents[new], st.parents[old] = list(st.parents[old]), []
        for c in children(img, new):
            st.parents[c] = [new if x == old else x for x in st.parents[c]]

    def to_rotate(self, h):
        """A ROTATE in place of TRANSLATE h (same children, identity rotation), so the object can turn."""
        img = self.img
        if img.u32(h + 0xC):
            raise PatchError("TRANSLATE %07x has action data (+0xC): it cannot become a ROTATE" % h)
        n = img.u32(h + 8)
        r = self.alloc(0x50 + 4 * n, 16)
        self.w32(r, (img.u32(h) & ~63) | 4)
        self.w32(r + 4, img.u32(h + 4))
        self.w32(r + 8, n)
        for row in range(4):
            for c in range(4):
                self.wf32(r + 0x10 + 16 * row + 4 * c, 1.0 if row == c else 0.0)
        for i in range(3):
            self.wf32(r + 0x40 + 4 * i, img.f32(h + 0x10 + 4 * i))
        for k in range(n):
            self.w32(r + 0x50 + 4 * k, img.u32(h + 0x1C + 4 * k))
        self.replace(h, r)
        self.stats["TRANSLATE to ROTATE"] += 1
        return r

    def place(self, h, pos, heading):
        """Write handle h so it sits at world `pos` with world `heading` (degrees, None = keep). Returns the handle, which is a
        new ROTATE when a TRANSLATE has to turn."""
        img = self.img
        P = chain_matrix(img, self.st.chain(h)[:-1])
        W = self.world(h)
        o = self.op(h)
        R = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]] if o == 3 else \
            [[img.f32(h + 0x10 + 16 * r + 4 * c) for c in range(3)] for r in range(3)]
        delta = 0.0 if heading is None else heading - math.degrees(math.atan2(W[0][1], W[0][0]))
        delta = (delta + 180.0) % 360.0 - 180.0
        if abs(delta) > 1e-3:
            cs, sn = math.cos(math.radians(delta)), math.sin(math.radians(delta))
            R = [[r[0] * cs - r[1] * sn, r[0] * sn + r[1] * cs, r[2]] for r in R]
            if o == 3:
                h, o = self.to_rotate(h), 4
        t = ngp.xform(inv_affine(P), pos)
        if o == 3:
            for i in range(3):
                self.wf32(h + 0x10 + 4 * i, t[i])
        else:
            for r in range(3):
                for c in range(3):
                    self.wf32(h + 0x10 + 16 * r + 4 * c, R[r][c])
            for i in range(3):
                self.wf32(h + 0x40 + 4 * i, t[i])
        return h

    # --- edits
    def move(self, h, pos, heading):
        before, old_pos = self.world(h, False), self.world(h)[3][:3]
        h = self.place(h, pos, heading)
        carry = ngp.mul(inv_affine(before), self.world(h, False))
        for pa in self.paths_through(h):
            self.redo(pa, carry)
        self.grow_spheres(h, old_pos)
        self.stats["moved"] += 1

    def delete(self, h):
        for pa in self.paths_through(h):
            self.set_cells(pa, set())
            self.stats["paths removed"] += 1
        self.w32(h, (self.img.u32(h) & ~63) | 9)
        self.stats["deleted"] += 1

    def reached(self):
        """The nodes dbsTraverse reaches from the roots it walks (dbProcInteractive registers only these)."""
        roots = [r for r in self.st.roots if self.op(r) != 7]
        seen, stack = set(roots), list(roots)
        while stack:
            for c in self._trav_kids(stack.pop()):
                if c not in seen:
                    seen.add(c)
                    stack.append(c)
        return seen

    def cut_incoming(self):
        """After the deletes: a DOMINO whose target dbsTraverse no longer reaches would call ChangeState on Interactives 0
        (NULL: the target's id2 stays 0); its type goes to DOMINO_OFF, in place."""
        img, seen, done = self.img, self.reached(), set()
        for n in seen:
            if self.op(n) == 31:
                for a, t, _ in act_blocks(img, n):
                    if t == 8 and a not in done and img.u32(a + 0xC) & MASK not in seen:
                        done.add(a)
                        self.w16(a, DOMINO_OFF)
                        self.stats["incoming dominoes cut"] += 1

    CLONE_HEAD = {1: (0x20, lambda img, n: img.u16(n + 8)), 6: (0xC, lambda img, n: img.u8(n + 0xB)),
                  3: (0x1C, lambda img, n: img.u32(n + 8)), 4: (0x50, lambda img, n: img.u32(n + 8))}

    def clone_private(self, n, top=True):
        """Copy of n and of the GROUP/SWITCH/TRANSLATE/ROTATE below it that have no other parent; shared nodes stay shared."""
        img, st, base = self.img, self.st, self.img.base
        o = self.op(n)
        if o not in self.CLONE_HEAD or (not top and len(st.parents.get(n, ())) != 1):
            return n
        head, count = self.CLONE_HEAD[o]
        size = head + 4 * count(img, n)
        a = self.alloc(size, 16)
        img.d[a - base:a - base + size] = img.d[n - base:n - base + size]
        for s_old in child_slots(img, n):
            s_new = a + (s_old - n)
            c = img.u32(s_old) & MASK
            if c:
                nc = self.clone_private(c, False)
                if nc != c:
                    self.set_ptr(s_new, nc)
                    st.parents[nc] = [a]
        self.stats["nodes cloned"] += 1
        return a

    # --- deep copies (docs/guides/MODS.md, "Under the hood")
    def _trav_kids(self, n):
        return [] if self.op(n) == 38 else children(self.img, n)      # dbsTraverse does not go below ANIM_SCALE

    def weights(self, roots):
        """node -> (Interactives taken at load, debris pickups at most) by what dbsTraverse visits under it, once per way down."""
        img, st, memo = self.img, self.st, {}
        for r in roots:
            if r in memo:
                continue
            memo[r] = None
            stack = [[r, self._trav_kids(r), 0]]
            while stack:
                top = stack[-1]
                n, ks, i = top
                if i < len(ks):
                    top[2] += 1
                    if ks[i] not in memo:
                        memo[ks[i]] = None
                        stack.append([ks[i], self._trav_kids(ks[i]), 0])
                    continue
                stack.pop()
                o = self.op(n)
                load = 1 if o == 39 or (o in CB_OPS and PICKUP_IDS[0] <= st.ident(n) < PICKUP_IDS[1]) else 0
                deb = debris_pickups(img, n) if o == 31 else 0
                for c in ks:
                    if memo[c]:
                        load, deb = load + memo[c][0], deb + memo[c][1]
                memo[n] = (load, deb)
        return memo

    def interactives(self):
        """(slots taken at load, debris pickups at most, spare) over the whole image; the sum must fit 1024."""
        roots = [r for r in self.st.roots if self.op(r) != 7]
        w = self.weights(roots)
        return 1 + sum(w[r][0] for r in roots), sum(w[r][1] for r in roots), INTERACTIVE_SPARE

    def mutable(self, n):
        """A node that keeps state per object: emitter (isActivated, cached matrix), SWITCH (whichChild), INTERACTIVE_STATE,
        DESTRUCTIBLE, and a node whose id dbProcInteractive registers."""
        return self.op(n) in (22, 6, 39, 31) or registers_id(self.img, n)

    # --- animated parts (src/common/animation.cpp; the hook has the same names)
    def instances(self):
        """CHAR_INSTANCEs of the stage's ANIM_MGR list, in the order animationGetHandle looks them up."""
        img, m = self.img, self.mgr
        if not m or not img.ok(m, 8):
            return []
        out = []
        for k in range(min(img.u32(m + 4), 4096)):
            ci = img.u32(m + 8 + 4 * k) & MASK
            if img.ok(ci, 0x2C):
                out.append(ci)
        return out

    def resolve_anim(self, id1, id2, anim):
        """The CHAR_INSTANCE the stage's animationGetHandle(id1, id2, anim) finds (the first with those ids and an animation with
        that id), or 0."""
        img = self.img
        for ci in self.instances():
            if img.u32(ci) >> 18 != id1 or (img.u32(ci) >> 7) & ID2_MAX != id2:
                continue
            chr_ = img.u32(ci + 0x20) & MASK
            for k in range(min(img.u16(chr_), 256)):
                if img.u32(img.u32(chr_ + 0xC + 4 * k) & MASK) >> 18 == anim:
                    return ci
        return 0

    def anim_target(self, a):
        """The CHAR_INSTANCE the ACT_ANIM a (a stage one, not a monster's) animates, or 0."""
        img = self.img
        if img.u16(a + 0xC) & ACTANIM_MONSTER:
            return 0
        return self.resolve_anim(img.u32(a + 8), img.u16(a + 0xC) & ID2_MAX, img.u16(a + 0xE))

    def anim_state_cost(self, ci):
        """Bytes of a CHAR_INSTANCE clone's own animation state (clone_anim_state), with each block's alignment."""
        img = self.img
        chr_, ctx = img.u32(ci + 0x20) & MASK, img.u32(ci + 0x10) & MASK
        nan, nch = img.u16(chr_), img.u16(chr_ + 2)
        c = 4 * nch + 15 + (nch >> 3) + 1 + 3 + 64 * img.u16(chr_ + 4) + 15 + 0x3C * nan + 15
        for k in range(nan):
            if img.u32(ctx + 0x3C * k + 0x30) & MASK:
                c += 2 * img.u16((img.u32(ctx + 0x3C * k + 4) & MASK) + 0xC) + 3
        return c

    def fresh_id2(self, id1):
        """An id2 no CHAR_INSTANCE with this id1 uses, from the top down (monsters' dupId is small)."""
        used = self.id2_used[id1]
        for v in range(ID2_MAX, 0, -1):
            if v not in used:
                used.add(v)
                return v
        raise PatchError("no free id2 for the CHAR_INSTANCEs with id1 %x" % id1)

    def clone_anim_state(self, n, a):
        """Clone a of CHAR_INSTANCE n gets its own animation state: output (val, dirty, atMat), control nodes and each one's key
        cache, copied from the disc; activeTree 0 (animationSetGlobal makes the AnimPlayer) and a new id2 (the copy's ACT_ANIM
        looks it up by it)."""
        img, base = self.img, self.img.base
        chr_ = img.u32(n + 0x20) & MASK
        nan, nch = img.u16(chr_), img.u16(chr_ + 2)

        def dup(field, size, align):
            d, src = self.alloc(size, align), img.u32(n + field) & MASK
            img.d[d - base:d - base + size] = img.d[src - base:src - base + size]
            self.set_ptr(a + field, d)
            return d
        val = dup(4, 4 * nch, 16)
        dup(8, (nch >> 3) + 1, 4)
        dup(0xC, 64 * img.u16(chr_ + 4), 16)        # one matrix per ANIM_XFORM (matrixIdx)
        ctx = dup(0x10, 0x3C * nan, 16)
        for k in range(nan):
            c = ctx + 0x3C * k
            pk = img.u32(c + 0x30) & MASK
            if pk:
                size = 2 * img.u16((img.u32(c + 4) & MASK) + 0xC)
                d = self.alloc(size, 4)
                img.d[d - base:d - base + size] = img.d[pk - base:pk - base + size]
                self.set_ptr(c + 0x30, d)
        if img.u32(n + 0x18) & MASK == img.u32(n + 4) & MASK:
            self.set_ptr(a + 0x18, val)         # animPktPtr = val on the stages (no skeleton)
        self.w32(a + 0x24, 0)
        self.w32(a, (img.u32(a) & ~(ID2_MAX << 7)) | (self.fresh_id2(self.st.ident(n)) << 7))
        self.anim_added.append(a)
        self.stats["anim instances cloned"] += 1

    def register_instances(self):
        """A new ANIM_MGR in the arena with the old list and the cloned CHAR_INSTANCEs, in the old root's place."""
        if not self.anim_added:
            return
        img, base, m0 = self.img, self.img.base, self.mgr
        n = min(img.u32(m0 + 4), 4096)
        m = self.alloc(8 + 4 * (n + len(self.anim_added)), 16)
        img.d[m - base:m - base + 8 + 4 * n] = img.d[m0 - base:m0 - base + 8 + 4 * n]
        hi_bits = img.u32(m0 + 8) & ~MASK if n else 0
        for k, ci in enumerate(self.anim_added):
            self.w32(m + 8 + 4 * (n + k), hi_bits | ci)
        self.w32(m + 4, n + len(self.anim_added))
        for i in range(img.u32(base)):
            if img.u32(base + 4 + 4 * i) & MASK == m0:
                self.set_ptr(base + 4 + 4 * i, m)
        self.st.roots = [m if r == m0 else r for r in self.st.roots]
        self.mgr = m
        self.anim_added = []

    def _under_bone(self, h, a, seen, left):
        """Below a SKEL_BONE (a skeleton: skin matrices through animMatrixPtr) the copy clones nothing; a destructible there is
        refused."""
        stack = [a]
        while stack:
            x = stack.pop()
            if x in seen:
                continue
            seen.add(x)
            if self.op(x) == 39:
                raise PatchError("copy of %07x: destructible %07x is inside a skeleton (%s %07x)" % (
                    h, x, OPN.get(self.op(a), self.op(a)), a))
            left[0] += self.op(x) == 22
            stack.extend(children(self.img, x))

    def anim_targets(self, h):
        """CHAR_INSTANCEs of h's subtree an ACT_ANIM of its destructibles animates."""
        sub, out = self.st.subtree(h), set()
        for n in sub:
            if self.op(n) == 31:
                for a, t, _ in act_blocks(self.img, n):
                    if t == 2:
                        ci = self.anim_target(a)
                        if ci and ci in sub:
                            out.add(ci)
        return out

    def _spine_walk(self, h, targets, cis):
        """One pass of the spine: a mutable node, a CHAR_INSTANCE a copy's ACT_ANIM animates and, with `cis`, an ANIM_XFORM/
        ANIM_SCALE of a CHAR_INSTANCE in `cis` (the nearest one above it: drawing uses its output) pull the node and the ones above
        it up to h."""
        img, memo, order, seen, left = self.img, {h: None}, [], set(), [0]
        stack = [[h, children(img, h), 0, 0]]
        while stack:
            top = stack[-1]
            n, ks, i, owner = top
            if i < len(ks):
                top[2] += 1
                c = ks[i]
                if c not in memo:
                    memo[c] = None
                    if self.op(c) == 35:
                        if cis is not None and owner in cis:
                            raise PatchError("copy of %07x: CHAR_INSTANCE %07x has a skeleton (SKEL_BONE %07x)" % (h, owner, c))
                        self._under_bone(h, c, seen, left)
                        memo[c] = False
                    else:
                        stack.append([c, children(img, c), 0, c if self.op(c) == 25 else owner])
                continue
            stack.pop()
            memo[n] = n == h or self.mutable(n) or n in targets or \
                (cis is not None and self.op(n) in ANIM_STATE_OPS and owner in cis) or any(memo.get(c) for c in ks)
            if memo[n]:
                if node_size(img, n) is None:
                    raise PatchError("copy of %07x: %s %07x, on the way to what it clones, has no known size" % (
                        h, OPN.get(self.op(n), self.op(n)), n))
                order.append(n)
        return order, left[0]

    def spine(self, h):
        """What the deep copy of handle h clones, children first: h and every node of its subtree with a mutable node at or
        below it. A CHAR_INSTANCE goes in when an ACT_ANIM of the copy animates it or a mutable node hangs below it; then its
        ANIM_XFORMs/ANIM_SCALEs go in too (they keep the instance's visible flag, rotation and dirty bits). What hangs from a
        SKEL_BONE stays shared; returns (nodes, emitters left shared there)."""
        targets = self.anim_targets(h)
        order, left = self._spine_walk(h, targets, None)
        cis = {n for n in order if self.op(n) == 25}
        return self._spine_walk(h, targets, cis) if cis else (order, left)

    def rewrite_act(self, a, twins, memo, depth=0):
        """Action a for a copy: a DOMINO aimed at the original's own destructible goes to the copy's, any other DOMINO is dropped
        (it would break the original's neighbour); an ANIM of a cloned CHAR_INSTANCE goes to the clone, every other GLOBAL_ACTS
        one is dropped; the GROUPs/TRANSFORMs above a changed one are cloned. 0 = nothing left."""
        img, base = self.img, self.img.base
        if not a or not img.ok(a, 8) or depth > 16:
            return a
        if a in memo:
            return memo[a]
        memo[a] = a
        t, out = img.u16(a), a
        if t == 8:
            tg = img.u32(a + 0xC) & MASK
            if tg in twins:
                out = self.alloc(0x10, 16)
                img.d[out - base:out - base + 0x10] = img.d[a - base:a - base + 0x10]
                self.set_ptr(out + 0xC, twins[tg])
                self.stats["dominoes redirected"] += 1
            else:
                out = 0
                self.stats["dominoes dropped"] += 1
        elif t == 2 and self.anim_target(a) in twins:      # ANIM of the copy's own animated part: to the clone, by its id2
            out = self.alloc(0x10, 16)
            img.d[out - base:out - base + 0x10] = img.d[a - base:a - base + 0x10]
            self.w16(out + 0xC, (img.u16(a + 0xC) & ~ID2_MAX) | (img.u32(twins[self.anim_target(a)]) >> 7) & ID2_MAX)
            self.stats["anim actions redirected"] += 1
        elif t in GLOBAL_ACTS:
            out = 0
            self.stats["global actions dropped"] += 1
        elif t == 0:
            words = [img.u32(a + 0xC + 4 * k) for k in range(min(img.u32(a + 8), 256))]
            new = [self.rewrite_act(w & MASK, twins, memo, depth + 1) for w in words]
            if any(x != w & MASK for x, w in zip(new, words)):
                keep = [w if x == w & MASK else x for x, w in zip(new, words) if x]
                out = self.alloc(0xC + 4 * len(keep), 16)
                img.d[out - base:out - base + 8] = img.d[a - base:a - base + 8]
                self.w32(out + 8, len(keep))
                for k, x in enumerate(keep):
                    self.w32(out + 0xC + 4 * k, x)
        elif t == 1:
            w = img.u32(a + 8)
            x = self.rewrite_act(w & MASK, twins, memo, depth + 1)
            if x != w & MASK:
                out = self.alloc(0x50, 16)          # 16-aligned: vu0CopyMatrix reads the matrix with lq
                img.d[out - base:out - base + 0x50] = img.d[a - base:a - base + 0x50]
                self.w32(out + 8, x or self.alloc(0xC, 16))     # an empty GROUP: doTransform does not test for null
        memo[a] = out
        return out

    def deep_clone(self, order, twins):
        """Clones `order` (children first) into the arena, twins gets original -> clone: child slots go to the clones, ids that
        register once go to 0, emitters start unlit, each CHAR_INSTANCE gets its own animation state and id2, and each
        DESTRUCTIBLE's actions are redone."""
        img, st, base = self.img, self.st, self.img.base
        for n in order:
            size = node_size(img, n)
            a = self.alloc(size, 16)
            img.d[a - base:a - base + size] = img.d[n - base:n - base + size]
            twins[n] = a
            self.stats["nodes cloned"] += 1
        for n in order:
            a, o, ident = twins[n], self.op(n), st.ident(n)
            for s in child_slots(img, n):
                c = img.u32(s) & MASK
                if c in twins:
                    self.set_ptr(a + (s - n), twins[c])
                    if not st.parents.get(twins[c]):
                        st.parents[twins[c]] = [a]
            if o == 39:
                self.stats["destructibles copied"] += 1
            elif o == 25:
                self.clone_anim_state(n, a)
            elif o == 22:
                self.w32(a + 0x5C, img.u32(n + 0x5C) & ~0x10)      # isActivated
                self.stats["emitters cloned"] += 1
                if img.u32(n + 0x5C) & 8:       # isAutoStart: a lasting fx of fx[100] from the load on, and a slot of autoEmitters[128]
                    self.stats["auto-start emitters cloned"] += 1
            if n in self.through:
                self.through.add(a)
            # id1 0: the level's special pieces (3-MILE stacks, SANFRAN, pillars) and anything registered once stay with the
            # original; a pickup id stays (createPickup reads its type), and so does "never loses health" on a DESTRUCTIBLE
            keep = (o == 31 and KEEP_DESTR_IDS[0] <= ident < KEEP_DESTR_IDS[1]) or \
                   (o not in (39, 31) and (not registers_id(img, n) or PICKUP_IDS[0] <= ident < PICKUP_IDS[1]))
            if ident and not keep:
                self.w32(a, img.u32(n) & 0x3FFFF)
                self.stats["ids cleared"] += 1
        memo = {}
        for n in order:
            if self.op(n) == 31:
                for k in range(min(img.u16(n + 0xA), 64)):
                    slot = twins[n] + 0x10 + 12 * k
                    act = img.u32(slot) & MASK
                    na = self.rewrite_act(act, twins, memo)
                    if na != act:
                        self.w32(slot, na)

    # --- what a copy may take (checked before anything is written, so a refused entry leaves nothing behind)
    def cells_bound(self, p):
        """Most cells path p can cover after a copy turns it: its box's XY diagonal on each axis."""
        g = self.st.grid
        n = int(math.floor((math.hypot(p.max[0] - p.min[0], p.max[1] - p.min[1]) + 2.0) / g.cell)) + 2
        return min(n, g.cols) * min(n, g.rows)

    def acts_bound(self, d):
        return sum(size + 15 + (0xC + 15 if t == 1 else 0) for a, t, size in act_blocks(self.img, d) if t in (0, 1, 2, 8))

    def private_cost(self, n, top=True):
        o = self.op(n)
        if o not in self.CLONE_HEAD or (not top and len(self.st.parents.get(n, ())) != 1):
            return 0
        head, count = self.CLONE_HEAD[o]
        return head + 4 * count(self.img, n) + 15 + sum(self.private_cost(c, False) for c in children(self.img, n))

    def move_cost(self, h, turns):
        cost = 0x50 + 4 * self.img.u32(h + 8) + 15 if self.op(h) == 3 and turns else 0
        return cost + sum(8 * self.cells_bound(Path(self.img, pa)) for pa in self.paths_through(h))

    def copy_cost(self, h, n, order, pickup):
        img = self.img
        cis = 0
        if pickup:
            per = self.private_cost(h)
        else:
            per = sum(node_size(img, x) + 15 for x in order) + sum(self.acts_bound(x) for x in order if self.op(x) == 31) + \
                sum(self.anim_state_cost(x) for x in order if self.op(x) == 25)
            cis = sum(1 for x in order if self.op(x) == 25)
        if self.op(h) == 3:
            per += 0x50 + 4 * img.u32(h + 8) + 15       # to_rotate, when the copy turns
        for pa in self.paths_through(h):
            p = Path(img, pa)
            per += 0x24 + 4 * p.length + 3 + 8 * self.cells_bound(p)
        mgr_cost = 8 + 4 * (len(self.instances()) + cis * n) + 15 if cis else 0       # register_instances
        return 0x20 + 4 * (1 + n) + 15 + per * n + mgr_cost

    def copy(self, h, places, extra=0):
        """Copies of the object under handle h, each in its own place; a GROUP in the parent's slot holds the original and the
        copies. A pickup gets its own handle, id GROUP, SWITCH and the GROUPs between (clone_private); anything else a deep copy:
        its own handle and every node that keeps state per object with the nodes between (spine), sharing meshes, collision,
        GS contexts and actions. Each copy gets its own collision paths; the cloned CHAR_INSTANCEs go into a new ANIM_MGR.
        `extra` is what the same entry's move takes after."""
        img, st = self.img, self.st
        kids = children(img, h)
        ist = next((c for c in kids if self.op(c) == 39), None)
        pickup = any(self.op(c) == 1 and PICKUP_IDS[0] <= st.ident(c) < PICKUP_IDS[1] for c in kids)
        left = 0
        if pickup:
            order = []
        elif self.shallow:
            if any(x != ist and self.op(x) == 39 for x in st.subtree(h)):
                raise PatchError("copy of %07x: it has destructibles inside it (a shallow copy cannot)" % h)
            order = ([img.u32(ist + 0xC) & MASK, ist] if ist else []) + [h]
        else:
            order, left = self.spine(h)
        if not self.mgr and any(self.op(x) == 25 for x in order):
            raise PatchError("copy of %07x: it has an animated part and the stage has no ANIM_MGR" % h)
        cost = self.copy_cost(h, len(places), order, pickup) + extra
        if cost > self.arena_free():
            raise PatchError("copy of %07x: %d copies take up to %d bytes, the arena has %d left" % (
                h, len(places), cost, self.arena_free()))
        w = self.weights([h])[h]
        need = len(places) * (w[0] + w[1])
        if self.interactives_used + need > INTERACTIVES:
            raise PatchError("copy of %07x: %d copies take %d interactives, %d of %d are taken" % (
                h, len(places), need, self.interactives_used, INTERACTIVES))
        self.interactives_used += need
        p = st.parents[h][0]
        before, old_pos, paths = self.world(h, False), self.world(h)[3][:3], self.paths_through(h)
        wrap = self.alloc(0x20 + 4 * (1 + len(places)), 16)
        self.w32(wrap, 1)
        self.w32(wrap + 4, img.u32(p + 4) if self.op(p) in (1, 3, 4, 30) else 0)
        self.w16(wrap + 8, 1)                       # numKids goes up as each copy is ready
        self.w16(wrap + 0xA, 7)                     # visual | collision | noFOV
        self.w32(wrap + 0x20, h)
        for s in child_slots(img, p):
            if img.u32(s) & MASK == h:
                self.set_ptr(s, wrap)
        st.parents[wrap], st.parents[h] = [p], [wrap]
        self.wraps.append(wrap)
        self.through.add(wrap)
        for i, pl in enumerate(places):
            twins = {}
            if pickup:      # LevelPickups/Pickup::init write into the id GROUP (id2, numKids) and the SWITCH (whichChild)
                hc = self.clone_private(h)
            else:
                self.deep_clone(order, twins)
                hc = twins[h]
            st.parents[hc] = [wrap]
            hc = self.place(hc, pl["pos"], pl.get("heading"))
            self.w32(wrap + 0x24 + 4 * i, hc)           # after place: a new ROTATE takes the TRANSLATE's place
            twins[h] = hc
            carry = ngp.mul(inv_affine(before), self.world(hc, False))
            base = img.base
            for pa in paths:
                q = Path(img, pa)
                np_ = self.alloc(0x24 + 4 * q.length)
                img.d[np_ - base:np_ - base + 0x24] = img.d[pa - base:pa - base + 0x24]
                self.w32(np_ + 0x18, 0)
                for k, nd in enumerate(q.nodes):
                    self.w32(np_ + 0x24 + 4 * k, (img.u32(pa + 0x24 + 4 * k) & ~MASK) | twins.get(nd, nd))
                self.redo(np_, carry)
                self.added_paths.append(np_)
                if pa in self.unlinked_before:
                    self.unlinked_before.add(np_)
                self.stats["paths added"] += 1
            self.grow_spheres(hc, old_pos)
            self.w16(wrap + 8, 2 + i)
            self.stats["copies"] += 1
        self.register_instances()
        if left:
            self.stats["emitters left shared"] += left * len(places)

    # --- checks of the result (lvl.py patch only)
    def visits(self):
        """How many times dbsTraverse reaches each node from the roots it walks."""
        roots = [r for r in self.st.roots if self.op(r) != 7]
        indeg, seen, stack = collections.Counter(), set(roots), list(roots)
        while stack:
            for c in self._trav_kids(stack.pop()):
                indeg[c] += 1
                if c not in seen:
                    seen.add(c)
                    stack.append(c)
        cnt = collections.Counter(roots)
        q = [n for n in seen if not indeg[n]]
        while q:
            n = q.pop()
            for c in self._trav_kids(n):
                cnt[c] += cnt[n]
                indeg[c] -= 1
                if not indeg[c]:
                    q.append(c)
        return cnt

    def reach(self, n):
        """Nodes below n, down to the skeletons (what the deep copy leaves shared)."""
        seen, stack = set(), [n]
        while stack:
            x = stack.pop()
            if x in seen:
                continue
            seen.add(x)
            if self.op(x) != 35:
                stack.extend(children(self.img, x))
        return seen

    def reach_mutable(self, n):
        return {x for x in self.reach(n) if self.mutable(x)}

    def _next_to(self, a, b):
        """b is a child of a, or hangs from a through GROUPs the patch put in between (self.through)."""
        img, stack, seen = self.img, [a], set()
        while stack:
            for c in children(img, stack.pop()):
                if c == b:
                    return True
                if c in self.through and c not in seen:
                    seen.add(c)
                    stack.append(c)
        return False

    def path_linked(self, pa):
        nodes = Path(self.img, pa).nodes
        return all(self._next_to(a, b) for a, b in zip(nodes, nodes[1:]))

    def check_copies(self):
        """Lines on the patched image: paths linked node to node (a GROUP the patch put in may sit between two; the paths the
        disc already has unlinked, and their copies, are only counted), mutable nodes the copies share with their originals,
        INTERACTIVE_STATEs dbsTraverse reaches twice. Second value: all hold. hdTraversePath compares parent and child only at
        a DESTRUCTIBLE and a SWITCH, so the game takes an unlinked path too; the check is for what the patch writes."""
        img = self.img
        linked = old = 0
        for pa in self.cells:
            if self.path_linked(pa):
                linked += 1
            elif pa in self.unlinked_before:
                old += 1
        shared = anim_orig = unregistered = 0
        registered = set(self.instances())
        for w in self.wraps:
            kids = children(img, w)
            orig = self.reach(kids[0]) if kids else set()
            orig_mut = {x for x in orig if self.mutable(x)}
            for c in kids[1:]:
                mine = self.reach(c)
                shared += len(orig_mut & {x for x in mine if self.mutable(x)})
                unregistered += sum(1 for x in mine - orig if self.op(x) == 25 and x not in registered)
                for d in mine:
                    if self.op(d) == 31:
                        anim_orig += sum(1 for a, t, _ in act_blocks(img, d) if t == 2 and self.anim_target(a) in orig)
        v = self.visits()
        twice = sum(1 for n, c in v.items() if c > 1 and self.op(n) == 39)
        added = sum(1 for pa in self.added_paths if pa in self.cells)
        lines = ["paths linked node to node: %d of %d (%d of them added by copies; %d unlinked as on the disc)" % (
                     linked, len(self.cells), added, old),
                 "mutable nodes the copies share with their originals: %d" % shared,
                 "ANIM actions of the copies that animate their original's part: %d" % anim_orig,
                 "CHAR_INSTANCEs of the copies missing from the ANIM_MGR: %d" % unregistered,
                 "INTERACTIVE_STATEs reached more than once: %d" % twice]
        return lines, linked + old == len(self.cells) and shared == 0 and anim_orig == 0 and unregistered == 0 and twice == 0

    GRID_MAX_CELLS = 65536

    def apply_grid(self, spec):
        """The patch's "grid": {"min": [x, y], "max": [x, y]}, a collision grid rectangle that holds the stage's: a new grid in the
        arena (same cell size, cols = floor(width / cell) + 1 as the disc's), every path listed again in the cells its box covers,
        the root of the image pointing to it. The old grid's list nodes are reused. Done before the entries, so they see the new
        grid. A bad spec raises PatchError (nothing is written)."""
        st, img = self.st, self.img
        g = st.grid
        try:
            lo, hi = [float(v) for v in spec["min"]], [float(v) for v in spec["max"]]
            ok = len(lo) == len(hi) == 2 and all(math.isfinite(v) for v in lo + hi)
        except (TypeError, KeyError, ValueError):
            ok = False
        if not ok:
            raise PatchError('"grid" needs "min" and "max" as [x, y]')
        if lo[0] > g.min_x or lo[1] > g.min_y or hi[0] < g.max_x or hi[1] < g.max_y:
            raise PatchError("\"grid\" (%.0f %.0f)-(%.0f %.0f) does not hold the stage's (%.0f %.0f)-(%.0f %.0f)" % (
                lo[0], lo[1], hi[0], hi[1], g.min_x, g.min_y, g.max_x, g.max_y))
        cols, rows = int((hi[0] - lo[0]) / g.cell) + 1, int((hi[1] - lo[1]) / g.cell) + 1
        if cols * rows > self.GRID_MAX_CELLS:
            raise PatchError("\"grid\" of %d x %d cells: at most %d cells" % (cols, rows, self.GRID_MAX_CELLS))
        need = 0x20 + 4 * cols * rows + 15
        if need > self.arena_free():
            raise PatchError("\"grid\" of %d x %d cells needs %d bytes, the arena has %d" % (cols, rows, need, self.arena_free()))
        old = g.addr
        nodes = []                              # the old lists' nodes, cell by cell, head first: they feed the new lists
        for i in range(g.cols * g.rows):
            ln, guard = img.u32(old + 0x20 + 4 * i) & MASK, 0
            while ln and guard < 100000:
                nodes.append(ln)
                ln, guard = img.u32(ln) & MASK, guard + 1
        new = self.alloc(0x20 + 4 * cols * rows, 16)
        img.d[new - img.base:new - img.base + 0x20] = img.d[old - img.base:old - img.base + 0x20]
        struct.pack_into("<hh", img.d, new + 4 - img.base, cols, rows)
        for k, v in enumerate((g.cell, lo[0], lo[1], hi[0], hi[1])):
            self.wf32(new + 8 + 4 * k, v)
        slot = next(img.base + 4 + 4 * i for i, r in enumerate(img.roots()) if r & MASK == old)
        self.set_ptr(slot, new)
        st.roots = [new if r == old else r for r in st.roots]
        st.grid = Grid(img, new)                # empty lists: the paths go in below
        self.free = list(reversed(nodes)) + self.free
        before = self.stats["new list nodes"]
        for pa in sorted(self.cells):
            p = Path(img, pa)
            cells = st.grid.cells_for_box(p.min, p.max)
            self.cells[pa] = set(cells)
            for c in sorted(cells):
                self._link(c, pa)
        self.stats["grid cells"] = cols * rows
        self.log.append("grid: %d x %d cells (was %d x %d), x [%.0f, %.0f] y [%.0f, %.0f]; %d paths listed again, %d new list nodes" % (
            cols, rows, g.cols, g.rows, lo[0], st.grid.max_x, lo[1], st.grid.max_y, len(self.cells), self.stats["new list nodes"] - before))

    def apply(self, patch):
        """Every entry: {"handle", "from": [x, y, z], and "pos"/"heading", "delete": true or "copies": [{"pos", "heading"}]}.
        An entry that does not match the image is skipped with a line in the log."""
        st, img = self.st, self.img
        if patch.get("grid") is not None:
            try:
                self.apply_grid(patch["grid"])
            except PatchError as err:       # nothing was written: the entries go on with the disc's grid
                self.log.append("grid skipped: %s" % err)
        for k, e in enumerate(patch.get("objects", [])):
            try:
                hv = e.get("handle")
                h = int(hv, 0) if isinstance(hv, str) else int(hv or 0)
                if h not in st.root_of or self.op(h) not in (3, 4):
                    raise PatchError("handle %07x is not a TRANSLATE/ROTATE of the stage" % h)
                if len(st.parents[h]) != 1:
                    raise PatchError("handle %07x has %d parents" % (h, len(st.parents[h])))
                pos = self.world(h)[3][:3]
                fr = e.get("from")
                if not fr or max(abs(pos[i] - fr[i]) for i in range(3)) > self.FROM_TOL:
                    raise PatchError("handle %07x is at (%.1f %.1f %.1f), not at \"from\" %s: another .NGP?" % (h, *pos, fr))
                if len(children(img, h)) > 1:
                    self.log.append("entry %d: handle %07x holds %d objects; they all go" % (k, h, len(children(img, h))))
                moving = not e.get("delete") and ("pos" in e or "heading" in e)
                extra = self.move_cost(h, e.get("heading") is not None) if moving else 0
                cp = e.get("copies")
                if isinstance(cp, list) and cp:
                    places = []         # all checked before copy(), as the hook does: a bad one leaves nothing behind
                    for c in cp:
                        p3 = c.get("pos") if isinstance(c, dict) else None
                        if not (isinstance(p3, list) and len(p3) == 3 and all(_number(x) for x in p3)):
                            raise PatchError("a copy without \"pos\" [x, y, z]")
                        hd = c.get("heading")
                        places.append({"pos": [float(x) for x in p3], "heading": float(hd) if _number(hd) else None})
                    self.copy(h, places, extra)
                elif extra > self.arena_free():
                    raise PatchError("handle %07x: the move takes up to %d bytes, the arena has %d left" % (h, extra, self.arena_free()))
                if e.get("delete"):
                    self.delete(h)
                elif moving:
                    self.move(h, e.get("pos") or pos, e.get("heading"))
            except (PatchError, KeyError, TypeError, ValueError) as err:
                self.log.append("entry %d skipped: %s" % (k, err))
                self.stats["skipped"] += 1
        if self.stats["deleted"]:
            self.cut_incoming()
        if patch.get("objects"):
            a, b = self.interactives_before, self.interactives()
            self.log.append("interactives: %d of %d before the patch, %d after (%d taken at load, %d debris pickups at most, "
                            "%d spare)" % (sum(a), INTERACTIVES, sum(b), b[0], b[1], b[2]))

    def _prop_chains(self, root):
        """Node chains from a prop's root down to each collision leaf (op 10, 11, 42), pre-order in slot order."""
        out = []

        def walk(n, chain, depth):
            if depth > 16:
                return
            if self.op(n) in (10, 11, 42):
                out.append(chain + [n])
                return
            for c in children(self.img, n):
                walk(c, chain + [n], depth + 1)
        walk(root, [], 0)
        return out

    def apply_props(self, patch, mod_dir):
        """The patch's "props" (docs/guides/MODS.md, "Level patches"), after "objects" and before "peds": each entry {"id",
        "model", "pos": [x, y, z], "heading", "name"} places a prop of <mod_dir>/props/<MODEL>/ (tools/lvlprop.py build). Each
        model goes once to the arena (0x80-aligned), relocated, its texture slots turned into the stage's texIds and its
        contexts copied from the first stage OBJECT (by address) with that texId; each entry is a TRANSLATE (no heading, or 0)
        or a ROTATE over the model's root. They all hang from a new GROUP (flags 7) in the world GROUP's first slot, and each
        collision leaf of each entry gets a path [world, props, T|R, root, ..., leaf] with its box by the 8-corner rule, its
        cells and a footprint for cut_nets. Refused, with a line and nothing left in the arena: a model missing or of another
        stage, a texId the stage's .TEX does not have or no OBJECT draws with, an entry whose box leaves the grid, a model
        whose entries do not fit in the arena."""
        props = patch.get("props") or []
        if not props or not isinstance(props, list):
            return
        import lvlprop
        st, img, g = self.st, self.img, self.st.grid
        start = self.arena_top()
        models, order = {}, []
        for k, e in enumerate(props):
            try:
                if not isinstance(e, dict):
                    raise PatchError("not an object")
                name = str(e.get("model") or "").upper()
                p3 = e.get("pos")
                if not (isinstance(p3, list) and len(p3) == 3 and all(_number(x) for x in p3)):
                    raise PatchError("without \"pos\" [x, y, z]")
                hd = e.get("heading")
                hd = float(hd) if _number(hd) else 0.0
                if name not in models:
                    models[name] = []
                    order.append(name)
                models[name].append((k, e, [float(x) for x in p3], hd))
            except PatchError as err:
                self.log.append("prop %d skipped: %s" % (k, err))
                self.stats["skipped"] += 1
        have = objs = None
        made, summary = [], []          # made: (T|R, model root, chains)
        reserve_group = 0x20 + 4 + 15
        for name in order:
            ents = models[name]
            top = self.arena_top()
            try:
                pdir = _child_nocase(_child_nocase(mod_dir, "props"), name)
                if not pdir or not os.path.isfile(os.path.join(pdir, "prop.json")):
                    raise PatchError("model %s not in %s/props" % (name, os.path.basename(os.path.normpath(mod_dir))))
                try:
                    prop = lvlprop.load_prop(pdir)
                except (lvlprop.PropError, OSError, ValueError, KeyError) as err:
                    raise PatchError("model %s: %s" % (name, err))
                if prop.stage.upper() != st.name:
                    raise PatchError("model %s is a prop of %s, not of %s" % (name, prop.stage, st.name))
                if have is None:
                    have = lvlprop.stage_tex_ids(st.disc, st.name)
                    objs = sorted(o for o in st.root_of if self.op(o) == 0)
                    self.log.append("props: the stage's .TEX has %d textures" % len(have))
                ctx, own_ids, own_lines = {}, {}, []
                for t in prop.own:          # a texture of its own: a texId past the loaded ones, the smallest bin that fits
                    tw, th = t["game"]
                    fit = [sz for _n, sz in lvlprop.stage_tex_sizes(st.disc, st.name) if sz >= 4 * tw * th]
                    if not fit:
                        raise PatchError("model %s: image %s (%dx%d) is bigger than every texture bin of the stage" % (
                            name, t.get("png"), tw, th))
                    own_ids[int(t["slot"])] = self.next_own_tex = (getattr(self, "next_own_tex", 0) or max(have) + 1)
                    self.next_own_tex += 1
                    own_lines.append("props: %s %s: own texture %dx%d, bin of %d bytes" % (name, t.get("png"), tw, th, min(fit)))
                for t in prop.textures:
                    if "texId" not in t:
                        continue
                    tid = int(t["texId"])
                    if tid not in have:
                        raise PatchError("model %s: tex%d is not in %s.TEX" % (name, tid, st.name))
                    found = lvlprop.stage_ctx(img, tid, objs)
                    if found is None:
                        raise PatchError("model %s: no OBJECT of the stage draws with tex%d" % (name, tid))
                    ctx[tid] = found
                slots = {int(t["slot"]) for t in prop.textures}
                for k, lst in ((1, prop.ptr[1]), (2, prop.ptr[2])):
                    for off in lst:
                        s = struct.unpack_from("<H" if k == 1 else "<I", prop.ngp, off)[0]
                        if s not in slots:
                            raise PatchError("model %s: slot %d without a texture in prop.json" % (name, s))
                at = self.alloc(prop.size, 0x80)
                img.d[at - img.base:at - img.base + prop.size] = prop.resolve(at, lambda tid: ctx[tid][1], own_ids.get)
                root = at + prop.root
                chains = self._prop_chains(root)
                mine, need = [], 0
                for k, e, pos, hd in ents:
                    t_top = self.arena_top()
                    turn = abs(math.remainder(hd, 360.0)) > 1e-6
                    t = self.alloc(0x54 if turn else 0x20, 16)
                    if turn:
                        cs, sn = math.cos(math.radians(hd)), math.sin(math.radians(hd))
                        self.w32(t, 4)
                        self.w32(t + 8, 1)
                        for r, row in enumerate(((cs, sn, 0, 0), (-sn, cs, 0, 0), (0, 0, 1, 0), (pos[0], pos[1], pos[2], 1))):
                            for c in range(4):
                                self.wf32(t + 0x10 + 16 * r + 4 * c, row[c])
                        self.w32(t + 0x50, root)
                    else:
                        self.w32(t, 3)
                        self.w32(t + 8, 1)
                        for i in range(3):
                            self.wf32(t + 0x10 + 4 * i, pos[i])
                        self.w32(t + 0x1C, root)
                    cost, bad = 4, None
                    for ch in chains:
                        lb = aabb(leaf_points(img, ch[-1]))
                        if not lb:
                            bad = "leaf %07x without points" % ch[-1]
                            break
                        m = chain_matrix(img, [t] + ch[:-1], with_scale=False)
                        bb = aabb([ngp.xform(m, (x, y, z)) for x in (lb[0][0], lb[1][0]) for y in (lb[0][1], lb[1][1])
                                   for z in (lb[0][2], lb[1][2])])
                        if bb[0][0] < g.min_x or bb[0][1] < g.min_y or bb[1][0] > g.max_x or bb[1][1] > g.max_y:
                            bad = "box (%.1f %.1f)-(%.1f %.1f) leaves the grid (%.1f %.1f)-(%.1f %.1f)" % (
                                bb[0][0], bb[0][1], bb[1][0], bb[1][1], g.min_x, g.min_y, g.max_x, g.max_y)
                            break
                        cost += 0x24 + 4 * (len(ch) + 3) + 3 + 8 * len(g.cells_for_box(bb[0], bb[1]))
                    if bad:
                        self.rollback(t_top)
                        self.log.append("prop %d (%s) skipped: %s" % (k, e.get("id") or name, bad))
                        self.stats["skipped"] += 1
                        continue
                    mine.append((t, root, chains))
                    need += cost
                if not mine:
                    self.rollback(top)
                    continue
                if need + (0 if made else reserve_group) > self.arena_free():
                    self.rollback(top)
                    raise PatchError("model %s: %d entries take up to %d bytes more, the arena has %d left" % (
                        name, len(mine), need, self.arena_free()))
                made += mine
                summary.append("%s x%d (%d bytes)" % (name, len(mine), prop.size))
                self.stats["prop models"] += 1
                for t in prop.textures:
                    if "texId" in t:
                        self.log.append("props: %s tex%d <- context of OBJECT %07x" % (name, int(t["texId"]), ctx[int(t["texId"])][0]))
                self.log += own_lines
            except PatchError as err:
                if self.arena_top() != top:
                    self.rollback(top)
                self.log.append("props of model %s skipped (%d): %s" % (name or "?", len(ents), err))
                self.stats["skipped"] += len(ents)
        if not made:
            return
        w = st.world
        slot = child_slots(img, w)[0]
        old = img.u32(slot) & MASK
        wrap = self.alloc(0x20 + 4 * (1 + len(made)), 16)
        self.w32(wrap, 1)
        self.w32(wrap + 4, img.u32(w + 4))
        self.w16(wrap + 8, 1 + len(made))
        self.w16(wrap + 0xA, 7)                     # visual | collision | noFOV
        self.w32(wrap + 0x20, old)
        for i, (t, _r, _c) in enumerate(made):
            self.w32(wrap + 0x24 + 4 * i, t)
        self.set_ptr(slot, wrap)
        st.parents[wrap], st.parents[old] = [w], [wrap]
        self.through.add(wrap)
        for t, root, chains in made:
            st.parents[t] = [wrap]
            st.parents[root].append(t)
            for ch in chains:
                nodes = [w, wrap, t] + ch
                pa = self.alloc(0x24 + 4 * len(nodes))
                self.w32(pa + 0x1C, len(nodes))
                for i, n in enumerate(nodes):
                    self.w32(pa + 0x24 + 4 * i, n)
                wc = st.path_corners(Path(img, pa))
                bb = aabb(wc)
                self.footprints.append((hull2d(wc), bb[0][2], bb[1][2]))
                for i in range(3):
                    self.wf32(pa + 4 * i, bb[0][i])
                    self.wf32(pa + 0xC + 4 * i, bb[1][i])
                cells = g.cells_for_box(bb[0], bb[1])
                self.set_cells(pa, cells)
                self.redone[pa] = (bb, len(cells), "prop")
                self.stats["prop paths"] += 1
            self.stats["props"] += 1
        self.log.append("props: %s; %d bytes of the arena" % (", ".join(summary), self.arena_top() - start))

    def apply_peds(self, patch, nets):
        """The patch's "peds" (docs/guides/MODS.md, "Level patches"), after "objects" and before cut_nets: "segments" replaces the
        pedestrian net (nets["ped"], changed in place), "groups" moves, recounts or deletes the disc's groups (handle + from) and
        adds new ones (pos, count), "off" deletes every group. A deleted group gets id 0 (a plain TRANSLATE); new groups are
        TRANSLATE 0xBB8 + NULL in the arena, hung from the world GROUP by a new GROUP in its first slot that holds the old child
        too. Refused, with a line: more than 20 groups, a count out of 1..255, a group whose nearest node has no link."""
        peds = patch.get("peds") or {}
        if not peds:
            return
        st, img = self.st, self.img
        if "segments" in peds:
            segs = list(peds["segments"] or [])
            if len(segs) > PED_MAX_SEGMENTS:
                self.log.append("pedestrians: %d segments, %d kept (256 nodes)" % (len(segs), PED_MAX_SEGMENTS))
                segs = segs[:PED_MAX_SEGMENTS]
            net = []
            for s in segs:
                w = max(1, min(255, int(s.get("width", 4))))
                m = len(net)
                net.append({"pos": [float(x) for x in s["a"]], "type": 0, "links": [[m + 1, w, 0]]})
                net.append({"pos": [float(x) for x in s["b"]], "type": 0, "links": [[m, w, 0]]})
            nets["ped"] = net
            self.stats["ped segments"] = len(segs)
        disk = {g["node"]: g for g in ped_groups(st)}
        alive = set(disk)
        off = bool(peds.get("off"))

        def clear(n):
            self.w32(n, img.u32(n) & 0x3FFFF)
            alive.discard(n)

        def check(pos, count):
            if not 1 <= count <= 255:
                raise PatchError("count %d is not 1..255" % count)
            if not nets.get("ped") or ped_bind(nets["ped"], pos) is None:
                raise PatchError("no sidewalk near (%.0f %.0f %.0f): its nearest node has no link" % tuple(pos)
                                 if nets.get("ped") else "the stage has no pedestrian net")

        if off:
            for n in list(alive):
                clear(n)
            self.stats["ped groups off"] = len(disk)
        new = []
        for k, e in enumerate(peds.get("groups") or []):
            try:
                if e.get("handle") is None:
                    if not off:
                        new.append((k, e))
                    continue
                hv = e["handle"]
                h = int(hv, 0) if isinstance(hv, str) else int(hv)
                g = disk.get(h)
                if not g:
                    raise PatchError("%07x is not a pedestrian group of the stage" % h)
                fr = e.get("from")
                if not fr or max(abs(g["pos"][i] - fr[i]) for i in range(3)) > self.FROM_TOL:
                    raise PatchError("group %07x is at (%.1f %.1f %.1f), not at \"from\" %s: another .NGP?" % (h, *g["pos"], fr))
                if off or e.get("delete"):
                    if h in alive:
                        clear(h)
                        self.stats["ped groups deleted"] += 1
                    continue
                pos = [float(x) for x in (e.get("pos") or g["pos"])]
                count = int(e.get("count", g["count"]))
                check(pos, count)
                for i in range(3):
                    self.wf32(h + 0x10 + 4 * i, pos[i])
                self.w32(h, (img.u32(h) & ~(0xFF << 7)) | (count << 7))
                self.stats["ped groups changed"] += 1
            except (PatchError, KeyError, TypeError, ValueError) as err:
                self.log.append("pedestrian group %d skipped: %s" % (k, err))
                self.stats["skipped"] += 1
        made = []
        for k, e in new:
            try:
                if len(alive) + len(made) >= PED_MAX_GROUPS:
                    raise PatchError("%d groups at most" % PED_MAX_GROUPS)
                pos = [float(x) for x in e["pos"]]
                count = int(e.get("count", 50))
                check(pos, count)
                nul = self.alloc(0x10, 16)
                self.w32(nul, 9)
                t = self.alloc(0x20, 16)
                self.w32(t, (PED_GROUP_ID << 18) | (count << 7) | 3)
                self.w32(t + 8, 1)
                for i in range(3):
                    self.wf32(t + 0x10 + 4 * i, pos[i])
                self.w32(t + 0x1C, nul)
                made.append(t)
            except (PatchError, KeyError, TypeError, ValueError) as err:
                self.log.append("pedestrian group %d skipped: %s" % (k, err))
                self.stats["skipped"] += 1
        if made:
            w = st.world
            slot = child_slots(img, w)[0]
            old = img.u32(slot) & MASK
            wrap = self.alloc(0x20 + 4 * (1 + len(made)), 16)
            self.w32(wrap, 1)
            self.w32(wrap + 4, img.u32(w + 4))
            self.w16(wrap + 8, 1 + len(made))
            self.w16(wrap + 0xA, 7)                     # visual | collision | noFOV
            self.w32(wrap + 0x20, old)
            for i, t in enumerate(made):
                self.w32(wrap + 0x24 + 4 * i, t)
            self.set_ptr(slot, wrap)
            st.parents[wrap], st.parents[old] = [w], [wrap]
            self.through.add(wrap)
            self.stats["ped groups added"] = len(made)
        self.ped_new = made

    def ped_report(self, nets):
        """After cut_nets: where each live group walks (its segment and length), and the segments nobody walks."""
        img, lines = self.img, []
        groups = [g for g in ped_groups(self.st) if img.u32(g["node"]) >> 18 == PED_GROUP_ID]
        groups += [{"node": t, "pos": [img.f32(t + 0x10 + 4 * i) for i in range(3)], "count": (img.u32(t) >> 7) & 0xFF}
                   for t in getattr(self, "ped_new", [])]
        net = nets.get("ped") or []
        used = set()
        for g in groups:
            b = ped_bind(net, g["pos"])
            if b is None:
                lines.append("pedestrians: group %07x (%d) at (%.1f %.1f %.1f) has no sidewalk" % (g["node"], g["count"], *g["pos"]))
                continue
            used.add((min(b), max(b)))
            ln = math.dist(net[b[0]]["pos"], net[b[1]]["pos"]) if net else 200.0
            lines.append("pedestrians: group %07x (%d) at (%.1f %.1f %.1f) walks %d-%d (%.0f long)%s" % (
                g["node"], g["count"], *g["pos"], b[0], b[1], ln, "" if net else " on the default net"))
        empty = [s for s in ped_net_segments(net) if (min(s["i"], s["k"]), max(s["i"], s["k"])) not in used]
        if groups and empty:
            lines.append("pedestrians: nobody walks %s" % ", ".join("%d-%d" % (s["i"], s["k"]) for s in empty))
        return lines


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
    on = st.on_top(objs)
    for o in objs:
        o["on"] = on.get(o["handle"], 0)
        print("%-6s node %07x handle %-9s %07x id %#06x pos (%8.1f %8.1f %7.1f) heading %6.1f states %d paths %3d%s%s%s" % (
            o["kind"], o["node"], o["handle_op"], o["handle"] or 0, o["id"], o["pos"][0], o["pos"][1], o["pos"][2], o["heading"],
            o["states"], o["n_paths"], " shared-handle" if o["handle_shared"] else "", " nested" if o["nested"] else "",
            " on %07x" % o["on"] if o["on"] else ""))
    if csv_path:
        keys = ["kind", "node", "handle", "handle_op", "id", "pos", "heading", "states", "handle_shared", "nested", "parents",
                "n_nodes", "n_obj", "n_emit", "n_anim", "n_paths", "on", "chain"]
        with open(csv_path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(keys)
            for o in objs:
                w.writerow(["%x" % o[k] if k in ("node", "handle", "on") and o[k] else
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
    if check:
        check_collision(st)


def check_collision(st, out=None):
    """Box rule and cell rule over every path of the grid; True when both hold everywhere they can be checked. out (a dict)
    gets the counts: box_ok, box_bad, worst (the largest error) and worst_at (its path), cell_ok, paths, skipped."""
    g = st.grid
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
    if out is not None:
        out.update(box_ok=box_ok, box_bad=box_bad, worst=worst[0], worst_at=worst[1], cell_ok=cell_ok, paths=len(g.paths),
                   skipped=dict(skipped))
    return box_bad == 0 and cell_ok == len(g.paths)


def cmd_patch(disc, name, path, shallow=False, mod_dir=None):
    """Apply a level patch to the disc image, print what changed and check the collision of the result (and of the copies).
    mod_dir: the mod folder the "props" models are looked up in (default: the folder above the JSON's levels/)."""
    st = Stage(name, disc)
    with open(path, encoding="utf-8") as f:
        patch = json.load(f)
    lp = LevelPatch(st, shallow)
    lp.apply(patch)
    try:
        lp.apply_props(patch, mod_dir or os.path.dirname(os.path.dirname(os.path.abspath(path))))
    except PatchError as err:           # only a full arena gets here
        lp.log.append("props: stopped halfway: %s" % err)
    pts = stage_points(disc, st.name)
    nets = {"fodder": net_from_points(pts.get("FOP", [])), "ped": net_from_points(pts.get("PED", []))}
    try:
        lp.apply_peds(patch, nets)      # the hook's order: objects, props, pedestrians, then the path cuts
    except PatchError as err:           # only a full arena gets here
        lp.log.append("pedestrians: stopped halfway: %s" % err)
    for line in lp.log:
        print("  " + line)
    print("%s: %s" % (st.name, ", ".join("%s %d" % kv for kv in sorted(lp.stats.items())) or "nothing changed"))
    for pa, (bb, n, how) in sorted(lp.redone.items()):
        print("  path %07x  box (%.1f %.1f %.1f)-(%.1f %.1f %.1f)  %d cells  %s" % (pa, *bb[0], *bb[1], n, how))
    for line in cut_nets(lp.footprints, nets) + lp.ped_report(nets):
        print("  " + line)
    st.grid = Grid(st.img, st.grid.addr)
    print("after the patch:")
    ok = check_collision(st)
    lines, copies_ok = lp.check_copies()
    for line in lines:
        print("  " + line)
    return 0 if ok and copies_ok else 1


def cmd_peds(disc, name):
    st = Stage(name, disc)
    net = net_from_points(stage_points(disc, st.name).get("PED", []))
    groups = ped_groups(st)
    print("%s: %d pedestrian groups (%d people), %d segments%s" % (st.name, len(groups), sum(g["count"] for g in groups),
          len(ped_net_segments(net)), "" if net else " (no .PED: the default net)"))
    for s in ped_net_segments(net):
        print("  segment %2d-%-2d width %3d  (%.1f %.1f %.1f) - (%.1f %.1f %.1f)  %.0f long" % (
            s["i"], s["k"], s["width"], *s["a"], *s["b"], math.dist(s["a"], s["b"])))
    lp = LevelPatch(st)
    for line in lp.ped_report({"ped": net}):
        print("  " + line)


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
    mod_dir = None
    if "--mod" in argv:
        i = argv.index("--mod")
        mod_dir = argv[i + 1]
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
    elif cmd in ("objects", "collision", "points", "mrs", "peds") and len(names) == 1:
        if cmd == "peds":
            cmd_peds(disc, names[0])
        elif cmd == "objects":
            cmd_objects(disc, names[0], csv_path)
        elif cmd == "collision":
            cmd_collision(disc, names[0], "--check" in flags)
        elif cmd == "points":
            cmd_points(disc, names[0])
        else:
            cmd_mrs(disc, names[0])
    elif cmd == "patch" and len(args) == 3:
        return cmd_patch(disc, args[1].upper(), args[2], "--shallow" in flags, mod_dir)
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
