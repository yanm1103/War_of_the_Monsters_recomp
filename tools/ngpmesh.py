#!/usr/bin/env python3
"""New mesh on the skeleton of a game monster: a .glb edited in Blender back into the monster's .NGP/.PTR.

  python tools/ngpmesh.py <base.NGP> <edited.glb> <out_dir> [--name NAME]

The .glb comes from `tools/monglb.py` (the base monster in its skeleton, one mesh per skinned OBJECT, `obj_<address>`;
joints `bone<N>` = skelOutputMatIdx N) and goes through Blender: the meshes can be remodeled, reweighted and re-UVed as long
as the bones keep their names and the armature is not moved. Each `obj_<address>` mesh of the .glb replaces that OBJECT's
sub-packets; every level of the LOD that holds it points at its level (the game draws it at any distance). Textures stay the base's (texIds of the
OBJECT): each primitive draws with the texture its material names (`tex<texId>`, as monglb names them; otherwise the first), and a new
UV layout pairs with a PNG of the same size in the mod.

Limits: skinned OBJECTs with one palette block (every one of Congar, Robo-47, Togera, Ultra-V, Preytor, Kineticlops, Magmo,
Agamo and Goliath; it refuses the others); 3 bones per vertex (the heaviest kept). Only the standard library: it runs inside the
mod kit.
Formats: docs/FORMATS.md ("Meshes", "Fixups"); the packet writer is tools/vifpkt.py.
"""
import json
import math
import os
import re
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vifpkt  # noqa: E402

BASE = 0xA00000


class MeshError(Exception):
    pass


# ---------------------------------------------------------------- glTF (binary) reader

_SIZES = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
_FMT = {5126: "f", 5123: "H", 5125: "I", 5121: "B", 5120: "b", 5122: "h"}
_NORM = {5121: 255.0, 5123: 65535.0, 5120: 127.0, 5122: 32767.0}


class Glb:
    def __init__(self, path):
        raw = open(path, "rb").read()
        if raw[:4] != b"glTF":
            raise MeshError("%s: not a binary glTF (.glb)" % path)
        off, self.j, self.bin = 12, None, b""
        while off + 8 <= len(raw):
            ln, typ = struct.unpack_from("<II", raw, off)
            chunk = raw[off + 8:off + 8 + ln]
            if typ == 0x4E4F534A:
                self.j = json.loads(chunk.decode("utf-8"))
            elif typ == 0x004E4942:
                self.bin = chunk
            off += 8 + ln
        if self.j is None:
            raise MeshError("%s: no JSON chunk" % path)
        self.nodes = self.j.get("nodes", [])

    def accessor(self, i):
        a = self.j["accessors"][i]
        if "sparse" in a:
            raise MeshError("sparse accessor %d (export without sparse data)" % i)
        n, k, ct = a["count"], _SIZES[a["type"]], a["componentType"]
        f = _FMT[ct]
        if "bufferView" not in a:
            return [(0,) * k] * n
        v = self.j["bufferViews"][a["bufferView"]]
        if v.get("buffer", 0) != 0:
            raise MeshError("accessor %d outside the .glb buffer" % i)
        sz = struct.calcsize(f)
        stride = v.get("byteStride") or sz * k
        base = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        out = []
        for e in range(n):
            vals = struct.unpack_from("<%d%s" % (k, f), self.bin, base + e * stride)
            if a.get("normalized"):
                vals = tuple(max(-1.0, x / _NORM[ct]) for x in vals)
            out.append(vals)
        return out


# ---------------------------------------------------------------- small matrix helpers (4x4, glTF column-major lists)

def _col(m16):
    """glTF column-major list -> rows[r][c]."""
    return [[m16[c * 4 + r] for c in range(4)] for r in range(4)]


def _inv_affine(m):
    """Inverse of an affine 4x4 (rows[r][c], column vectors)."""
    a = [row[:3] for row in m[:3]]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    if abs(det) < 1e-12:
        raise MeshError("singular matrix")
    inv = [[(a[(c + 1) % 3][(r + 1) % 3] * a[(c + 2) % 3][(r + 2) % 3] - a[(c + 1) % 3][(r + 2) % 3] * a[(c + 2) % 3][(r + 1) % 3]) / det
            for c in range(3)] for r in range(3)]
    t = [m[0][3], m[1][3], m[2][3]]
    it = [-sum(inv[r][c] * t[c] for c in range(3)) for r in range(3)]
    return [inv[0] + [it[0]], inv[1] + [it[1]], inv[2] + [it[2]], [0.0, 0.0, 0.0, 1.0]]


def _to_game(p):
    """glTF Y-up (monglb root: game (x, y, z) -> (x, z, -y)) back to the game's Z-up."""
    return (p[0], -p[2], p[1])


# ---------------------------------------------------------------- the base monster

class Base:
    """The base .NGP image and what the import needs from it."""

    def __init__(self, ngp_path):
        self.path = ngp_path
        self.d = bytearray(open(ngp_path, "rb").read())
        if self.d[:4] == b"IE\x03\x04":
            raise MeshError("%s: zipped .NGP (stages); a monster .NGP is raw" % ngp_path)
        ptr_path = os.path.splitext(ngp_path)[0] + ".PTR"
        if not os.path.exists(ptr_path):
            ptr_path = os.path.splitext(ngp_path)[0] + ".ptr"
        raw = open(ptr_path, "rb").read()
        o, self.ptr = 0, []
        for _ in range(3):
            n = struct.unpack_from("<I", raw, o)[0]
            self.ptr.append(list(struct.unpack_from("<%dI" % n, raw, o + 4)))
            o += 4 + 4 * n
        self.ptr_tail = raw[o:]
        self._scan()

    def ok(self, a, n=4):
        return BASE <= a and a - BASE + n <= len(self.d)

    def u16(self, a):
        return struct.unpack_from("<H", self.d, a - BASE)[0]

    def u32(self, a):
        return struct.unpack_from("<I", self.d, a - BASE)[0]

    def f32(self, a):
        return struct.unpack_from("<f", self.d, a - BASE)[0]

    def put32(self, a, v):
        struct.pack_into("<I", self.d, a - BASE, v)

    def putf(self, a, v):
        struct.pack_into("<f", self.d, a - BASE, v)

    def _img(self):
        import ngp
        return ngp.Image(bytes(self.d), BASE)

    def _scan(self):
        img = self._img()
        self.packets = vifpkt.packets(img)
        self.skinned = {}      # OBJECT -> (CHAR_INSTANCE, chain address, [(sub-packet, qwc)])
        self.lod_of = {}       # OBJECT -> (LOD node, level)
        cis = {}
        for r in img.roots():
            nodes = vifpkt._subtree(img, r)
            for n in nodes:
                op = self.u32(n) & 63
                if op == 25:
                    for o in vifpkt._subtree(img, n):
                        cis.setdefault(o, n)
                elif op == 2:
                    for i in range(min(self.u32(n + 4), 16)):
                        for o in vifpkt._subtree(img, self.u32(n + 0x28 + 16 * i) & 0x0FFFFFFF):
                            if self.u32(o) & 63 == 0:
                                self.lod_of.setdefault(o, (n, i))
        for o, kind, addr, qwc in self.packets:
            if kind != "skin":
                continue
            ci = cis[o]
            idx = struct.unpack_from("<i", self.d, o + 0x68 - BASE)[0]
            chain = self.u32(self.u32(ci + 0x18) + 4 * idx) & 0x0FFFFFFF
            self.skinned.setdefault(o, (ci, chain, []))[2].append((addr, qwc))

    def palette(self, obj):
        """(palette REF tag words, {bone output: slot}) of a skinned OBJECT. One block for the whole object."""
        ci, chain, _subs = self.skinned[obj]
        refs = set()
        p = chain
        while (self.u32(p) >> 28) & 7 == 3:
            refs.add(tuple(self.u32(p + 4 * k) for k in range(4)))
            p += 32
        if len(refs) != 1:
            raise MeshError("OBJECT %x uses %d palette blocks (only one-block monsters)" % (obj, len(refs)))
        ref = refs.pop()
        blk = ref[1] & 0x0FFFFFFF
        nslots = ((ref[0] & 0xFFFF) - 1) // 4
        amp = self.u32(ci + 0x14)
        slots = {}
        for k in range(64):
            if not self.ok(amp + 4 * k):
                break
            a = self.u32(amp + 4 * k) & 0x0FFFFFFF
            if not self.ok(a, 64):
                break
            if blk + 0x10 <= a < blk + 0x10 + 0x40 * nslots and (a - blk - 0x10) % 0x40 == 0:
                slots[k] = (a - blk - 0x10) // 0x40
        return ref, slots, nslots

    def rest_translation(self, ci):
        """{bone output: translation of the bone at rest (inverse of restPoseInv), game space}."""
        out = {}
        img = self._img()
        for n in vifpkt._subtree(img, ci):
            if self.u32(n) & 63 != 35:
                continue
            o = struct.unpack_from("<h", self.d, n + 8 - BASE)[0]
            if o < 0:
                continue
            rp = [[self.f32(n + 0x10 + 16 * r + 4 * c) for c in range(4)] for r in range(4)]
            # row vectors: p' = p * rp; rest matrix = rp^-1; its translation row
            colm = [[rp[c][r] for c in range(4)] for r in range(4)]
            inv = _inv_affine(colm)
            out[o] = (inv[0][3], inv[1][3], inv[2][3])
        return out


# ---------------------------------------------------------------- glb mesh -> game vertices

def _bone_out(name):
    # bone<N>; bone<N>@<OBJECT>: the copy of the bones under the "off_<OBJECT>" node (skin with an object matrix, Agamo)
    m = re.fullmatch(r"bone(?:[0-9a-f]+_)?(\d+)(?:@[0-9a-f]+)?", name or "")
    return int(m.group(1)) if m else None


def read_mesh(glb, name, base, obj, warn):
    """Triangles of the .glb mesh `name` in the game's terms, by texture slot of the OBJECT: {slot: [(corner, corner, corner)]},
    corner = (pos4 raw u32, normal s8x3, uv s16x2, rgba u8x4); positions with the bone codes and weights already encoded for
    `obj`'s palette. The slot comes from the primitive's material name (`tex<texId>`)."""
    node = next((n for n in glb.nodes if n.get("name") == name and "mesh" in n), None)
    if node is None:
        mi = next((k for k, m in enumerate(glb.j.get("meshes", [])) if m.get("name") == name), None)
        node = next((n for n in glb.nodes if n.get("mesh") == mi), None) if mi is not None else None
    if node is None:
        raise MeshError("mesh %s not found in the .glb" % name)
    if "skin" not in node:
        raise MeshError("mesh %s has no skin (keep it parented to the armature)" % name)
    skin = glb.j["skins"][node["skin"]]
    ref, slots, nslots = base.palette(obj)
    ci = base.skinned[obj][0]
    joint_out = [_bone_out(glb.nodes[k].get("name")) for k in skin["joints"]]
    ibm = glb.accessor(skin["inverseBindMatrices"]) if "inverseBindMatrices" in skin else [None] * len(joint_out)

    # Which space the positions are in: the game's (monglb) or turned Y-up (Blender export). The bind matrices tell:
    # inverse(IBM_j) is joint j at rest in the mesh's space.
    rest = base.rest_translation(ci)
    err = {"game": 0.0, "yup": 0.0}
    nj = 0
    for k, o in enumerate(joint_out):
        if o is None or o not in rest or ibm[k] is None:
            continue
        t = _inv_affine(_col(ibm[k]))
        t = (t[0][3], t[1][3], t[2][3])
        g = rest[o]
        err["game"] = max(err["game"], math.dist(t, g))
        err["yup"] = max(err["yup"], math.dist(_to_game(t), g))
        nj += 1
    if not nj:
        raise MeshError("mesh %s: no joint named bone<N> with a bind matrix" % name)
    space = min(err, key=err.get)
    if err[space] > 0.5:
        raise MeshError("mesh %s: the skeleton was moved or rescaled (joints up to %.2f units off the game's rest pose); "
                        "keep the armature where the export put it" % (name, err[space]))
    conv = _to_game if space == "yup" else (lambda p: p)

    texids = [base.u16(obj + 0x50 + 2 * k) for k in range(min(base.u16(obj + 0x5A), 5))]
    by_slot = {}
    lost = {}
    for prim in glb.j["meshes"][node["mesh"]]["primitives"]:
        if prim.get("mode", 4) != 4:
            raise MeshError("mesh %s: primitive mode %d (triangles only)" % (name, prim["mode"]))
        mats = glb.j.get("materials", [])
        mname = mats[prim["material"]].get("name", "") if "material" in prim and prim["material"] < len(mats) else ""
        m = re.match(r"tex(\d+)", mname or "")
        tid = int(m.group(1)) if m else None
        if tid in texids:
            slot = texids.index(tid)
        else:
            slot = 0
            if len(texids) > 1:
                warn("mesh %s: material %r is not one of the OBJECT's textures (%s); drawn with tex%d" % (
                    name, mname, ", ".join("tex%d" % t for t in texids), texids[0]))
        tris = by_slot.setdefault(slot, [])
        at = prim["attributes"]
        pos = glb.accessor(at["POSITION"])
        nrm = glb.accessor(at["NORMAL"]) if "NORMAL" in at else None
        uv = glb.accessor(at["TEXCOORD_0"]) if "TEXCOORD_0" in at else None
        col = glb.accessor(at["COLOR_0"]) if "COLOR_0" in at else None
        sets = []
        for s in range(4):
            if "JOINTS_%d" % s in at and "WEIGHTS_%d" % s in at:
                sets.append((glb.accessor(at["JOINTS_%d" % s]), glb.accessor(at["WEIGHTS_%d" % s])))
        if not sets:
            raise MeshError("mesh %s: no JOINTS_0/WEIGHTS_0" % name)
        idx = [i[0] for i in glb.accessor(prim["indices"])] if "indices" in prim else list(range(len(pos)))
        corner = {}

        def make(v):
            if v in corner:
                return corner[v]
            acc = {}
            for js, ws in sets:
                for j, w in zip(js[v], ws[v]):
                    if w <= 0.0:
                        continue
                    o = joint_out[j] if j < len(joint_out) else None
                    if o is None or o not in slots:
                        lost[glb.nodes[skin["joints"][j]].get("name") if j < len(joint_out) else j] = True
                        continue
                    acc[slots[o]] = acc.get(slots[o], 0.0) + w
            if not acc:
                raise MeshError("mesh %s: vertex %d has no weight on a bone of the palette" % (name, v))
            bones = sorted(acc.items(), key=lambda kv: (-kv[1], kv[0]))[:3]
            tot = sum(w for _, w in bones)
            bones = [(s, w / tot) for s, w in bones]
            p = conv(pos[v])
            n = conv(nrm[v]) if nrm else (0.0, 0.0, 1.0)
            ln = math.sqrt(sum(c * c for c in n)) or 1.0
            n8 = tuple(max(-127, min(127, int(round(c / ln * 127)))) for c in n)
            t = uv[v] if uv else (1.0, 1.0)   # no UV: untextured (PRIM.TME 0), where the game's files have 1.0
            uv16 = tuple(max(-32768, min(32767, int(round(c * 4096)))) for c in t)
            if col:
                c = col[v] + ((1.0,) if len(col[v]) == 3 else ())
                rgba = tuple(max(0, min(255, int(round(x * 128)))) for x in c)
            else:
                rgba = (0x80, 0x80, 0x80, 0x80)
            corner[v] = (encode_skinned(p, bones, nslots), n8, uv16, rgba)
            return corner[v]

        for k in range(0, len(idx) - 2, 3):
            a, b, c = make(idx[k]), make(idx[k + 1]), make(idx[k + 2])
            if a[0][:3] == b[0][:3] or b[0][:3] == c[0][:3] or a[0][:3] == c[0][:3]:
                continue   # degenerate
            tris.append((a, b, c))
    if lost:
        warn("mesh %s: weights on bones outside the palette dropped: %s" % (name, ", ".join(sorted(map(str, lost)))))
    by_slot = {k: v for k, v in by_slot.items() if v}
    if not by_slot:
        raise MeshError("mesh %s: no triangles" % name)
    return by_slot


def encode_skinned(p, bones, nslots):
    """Position as the skinning program reads it (Scene::skinVertex): x/y/z with a bone code (slot * 4) in the low bits and w
    such that w1 = w, w2 = frac(w * 2048), w3 = 1 - w1 - w2, heaviest bone first (the normal follows bone 1)."""
    slots = [s for s, _ in bones]
    ws = [w for _, w in bones] + [0.0] * (3 - len(bones))
    for s in range(nslots):   # the unused slots are distinct too, as in the game's files
        if len(slots) == 3:
            break
        if s not in slots:
            slots.append(s)
    w2 = min(ws[1], 0.999)
    a = round(ws[0] * 2048 - w2)
    w = struct.unpack("<f", struct.pack("<f", (a + w2) / 2048.0))[0]
    out = []
    for c in range(3):
        bits = struct.unpack("<I", struct.pack("<f", p[c]))[0]
        out.append((bits & ~0x7F) | (slots[c] * 4))
    out.append(struct.unpack("<I", struct.pack("<f", w))[0])
    return tuple(out)


def _weights_used(pos4):
    w = struct.unpack("<f", struct.pack("<I", pos4[3]))[0]
    w2 = math.modf(w * 2048.0)[0]
    return 1 if w2 < 1e-6 and abs(1.0 - w) < 1e-6 else 3


# ---------------------------------------------------------------- triangles -> sub-packets

def _groups(tris, limit=vifpkt.MAX_POS):
    """Triangles in groups of at most `limit` distinct positions, grown over shared positions (locality)."""
    by_pos = {}
    for t, tri in enumerate(tris):
        for c in tri:
            by_pos.setdefault(c[0], []).append(t)
    used = [False] * len(tris)
    groups = []
    order = sorted(range(len(tris)), key=lambda t: min(c[0][2] for c in tris[t]))   # from the bottom up (z)
    for seed in order:
        if used[seed]:
            continue
        pos, cur, queue = set(), [], [seed]
        while queue:
            t = queue.pop(0)
            if used[t]:
                continue
            new = {c[:2] for c in tris[t]} - pos   # entries of the position table (build_packets)
            if len(pos) + len(new) > limit:
                continue
            used[t] = True
            pos |= new
            cur.append(t)
            for c in tris[t]:
                for u in by_pos[c[0]]:
                    if not used[u]:
                        queue.append(u)
        groups.append([tris[t] for t in cur])
    return groups


def _strips(tris):
    """Greedy strips over shared corners (position index, UV and color all equal), keeping each triangle's winding."""
    edge = {}
    for t, (a, b, c) in enumerate(tris):
        for u, v in ((a, b), (b, c), (c, a)):
            edge.setdefault((u, v), []).append(t)
    used = [False] * len(tris)

    def third(t, u, v):
        a, b, c = tris[t]
        return a if a not in (u, v) else (b if b not in (u, v) else c)

    def grow(start):
        s = list(start)
        mine = []
        taken = set()
        while True:
            x, y = s[-2], s[-1]
            want = (x, y) if (len(s) - 2) % 2 == 0 else (y, x)
            nxt = next((t for t in edge.get(want, ()) if not used[t] and t not in taken), None)
            if nxt is None:
                return s, mine
            mine.append(nxt)
            taken.add(nxt)
            s.append(third(nxt, x, y))

    def neighbors(t):
        a, b, c = tris[t]
        return sum(1 for u, v in ((b, a), (c, b), (a, c)) for x in edge.get((u, v), ()) if not used[x])

    out = []
    pending = list(range(len(tris)))
    while pending:
        pending = [t for t in pending if not used[t]]
        if not pending:
            break
        t0 = min(pending, key=neighbors)
        a, b, c = tris[t0]
        best = None
        for rot in ((a, b, c), (b, c, a), (c, a, b)):
            used[t0] = True
            s, mine = grow(rot)
            used[t0] = False
            if best is None or len(s) > len(best[0]):
                best = (s, mine)
        used[t0] = True
        for t in best[1]:
            used[t] = True
        out.append(best[0])
    return out


def build_packets(tris, gif_first, gif_rest, warn=None):
    """Skinned sub-packets (vifpkt.Packet) for the triangles. gif_first / gif_rest: the GIF tag words of the first section of
    a sub-packet and of the others (NLOOP filled here)."""
    pkts = []
    for grp in _groups(tris):
        pos_index = {}
        b = vifpkt.Batch()
        for tri in grp:
            for c in tri:
                if c[:2] not in pos_index:   # position and normal: a crease has one entry per normal
                    pos_index[c[:2]] = len(b.pos)
                    b.pos.append(c[0])
                    b.nrm.append(c[1])
        svert = {}
        local = []
        for tri in grp:
            local.append(tuple(svert.setdefault((pos_index[c[:2]], c[2], c[3]), (pos_index[c[:2]], c[2], c[3])) for c in tri))
        b.mscal = 0 if all(_weights_used(p) == 1 for p in b.pos) else 4
        cur = None

        def section():
            s = vifpkt.Section()
            b.sections.append(s)
            return s

        for strip in _strips(local):
            rest = strip
            while rest:
                room = vifpkt.MAX_SECTION - len(cur.idx) if cur is not None else 0
                if len(rest) > room and room < 4:
                    cur = section()
                    room = vifpkt.MAX_SECTION
                if len(rest) <= room:
                    take, nxt = rest, []
                else:
                    m = room if room % 2 == 0 else room - 1   # cut at an even vertex: the rest keeps its winding
                    take, nxt = rest[:m], rest[m - 2:]
                if cur.idx:
                    cur.adc.append(len(cur.idx))
                for v in take:
                    cur.idx.append(v[0])
                    cur.uv.append(v[1])
                    cur.rgba.append(v[2])
                rest = nxt
                if rest:
                    cur = None
        for k, s in enumerate(b.sections):
            g = gif_first if k == 0 else gif_rest
            s.gif = ((g[0] & ~0x7FFF) | len(s.idx), g[1], g[2], g[3])
        p = vifpkt.Packet(True)
        p.batches.append(b)
        pkts.append(p)
    return pkts


# ---------------------------------------------------------------- patching the .NGP

def _gif_templates(base, obj):
    """GIF tag words of the first section of a sub-packet and of the others, from the base's first sub-packet. The low 15 bits of
    word 3 pick the texture: 1 + 3 * slot loads the slot's GS context (TEX0, TEX1, MIP), 0 keeps the current one."""
    import ngp
    img = ngp.Image(bytes(base.d), BASE)
    addr, qwc = base.skinned[obj][2][0]
    p = vifpkt.parse(bytes(img.d[addr - BASE:addr - BASE + 16 * qwc]), True)
    secs = p.batches[0].sections
    rest = secs[1].gif if len(secs) > 1 else secs[0].gif
    return secs[0].gif, rest[:3] + (rest[3] & ~0x7FFF,)


def _with_slot(gif, slot):
    return gif[:3] + ((gif[3] & ~0x7FFF) | (1 + 3 * slot),)


def replace(base, obj, pkts):
    """Writes the sub-packets and a new chain for `obj` into the image (in the space of the old sub-packets when they fit,
    else at the end), points animPktPtr at it and updates the OBJECT (strip vertex count, bounding sphere)."""
    ci, chain, subs = base.skinned[obj]
    ref, _slots, _n = base.palette(obj)
    blobs = [vifpkt.encode(p) for p in pkts]
    # free space: the old sub-packets of this object (and the zero padding between them)
    free = sorted((a, a + 16 * q) for a, q in subs)
    spans = []
    for a, e in free:
        if spans and spans[-1][1] <= a and not any(base.d[spans[-1][1] - BASE:a - BASE]):
            spans[-1][1] = max(spans[-1][1], e)
        else:
            spans.append([a, e])
    ptrs = set(base.ptr[0])
    others = [(a, a + 16 * q) for o, (_c, _ch, ss) in base.skinned.items() if o != obj for a, q in ss]
    for a, e in spans:
        if any(a - BASE <= o < e - BASE for o in ptrs) or any(x < e and a < y for x, y in others):
            spans = []   # a pointer or another object's packet inside: do not reuse
            break
    for a, e in spans:
        base.d[a - BASE:e - BASE] = bytes(e - a)
    # The sub-packets stay contiguous, in order, each 0x80-aligned (zero padding between them): the native renderer decodes
    # them in sequence from polyPkt and stops at the first quadword after the zeros that is not a sub-packet.
    offs, size = [], 0
    for blob in blobs:
        size = (size + 0x7F) & ~0x7F
        offs.append(size)
        size += len(blob)
    clen = 32 * len(blobs) + 16
    start = None
    for s in spans:
        at = (s[0] + 0x7F) & ~0x7F
        if at + size <= s[1]:
            start, room = at, s[1] - (at + size)
            break
    if start is None:
        start = BASE + ((len(base.d) + 0x7F) & ~0x7F)
        room = 0
    if start + size - BASE > len(base.d):
        base.d += bytes(start + size - BASE - len(base.d))
    addrs = [start + o for o in offs]
    for a, blob in zip(addrs, blobs):
        base.d[a - BASE:a - BASE + len(blob)] = blob
    # chain: REF palette, REF sub-packet ... RET; right after the sub-packets (it also ends the native decode), else at the end
    at = (start + size + 0xF) & ~0xF
    if at + clen > start + size + room:
        at = BASE + ((len(base.d) + 0xF) & ~0xF)
    if at + clen - BASE > len(base.d):
        base.d += bytes(at + clen - BASE - len(base.d))
    new_chain = at
    for k, (a, blob) in enumerate(zip(addrs, blobs)):
        q = at + 32 * k
        for w in range(4):
            base.put32(q + 4 * w, ref[w])
        base.put32(q + 16, 0x30000000 | (len(blob) // 16))
        base.put32(q + 20, a)
        base.put32(q + 24, 0)
        base.put32(q + 28, 0)
        base.ptr[0] += [q + 4 - BASE, q + 20 - BASE]
    base.put32(at + 32 * len(blobs), 0x60000000)
    for w in range(1, 4):
        base.put32(at + 32 * len(blobs) + 4 * w, 0)
    idx = struct.unpack_from("<i", base.d, obj + 0x68 - BASE)[0]
    base.put32(base.u32(ci + 0x18) + 4 * idx, new_chain)
    base.put32(obj + 4, (base.u32(obj + 4) & 0xF0000000) | addrs[0])   # polyPkt: the first sub-packet
    # OBJECT: numVerts (strip vertices, 13 bits) and the bounding sphere of the rest positions
    nverts = sum(len(s.idx) for p in pkts for b in p.batches for s in b.sections)
    bits = base.u32(obj + 0xC)
    base.put32(obj + 0xC, (bits & ~(0x1FFF << 16)) | (min(nverts, 0x1FFF) << 16))
    pts = [tuple(struct.unpack("<f", struct.pack("<I", v[c]))[0] for c in range(3)) for p in pkts for b in p.batches for v in b.pos]
    lo = [min(x[c] for x in pts) for c in range(3)]
    hi = [max(x[c] for x in pts) for c in range(3)]
    r = 0.5 * math.dist(lo, hi)
    for c in range(3):
        base.putf(obj + 0x10 + 4 * c, 0.5 * (lo[c] + hi[c]))
    base.putf(obj + 0x1C, r)
    base.putf(obj + 8, r * r)
    return new_chain, addrs


def share_lod(base, objs, log):
    """Every level of the LOD that holds an edited OBJECT points at the child of the edited level (the most detailed one when
    several are edited): whatever level the game picks (fight, shadow, menus) draws the new mesh. The fight never draws the
    last level of a monster LOD (the game zeroes its switchOutDis at load), so changing the distances would not help."""
    edited = {}
    for o in objs:
        if o in base.lod_of:
            lod, lvl = base.lod_of[o]
            edited.setdefault(lod, set()).add(lvl)
    out = []
    for lod, lvls in sorted(edited.items()):
        lvl = max(lvls)
        if len(lvls) > 1:
            log("warning: LOD %x has edited meshes in levels %s; only level %d is kept" % (lod, sorted(lvls), lvl))
        child = base.u32(lod + 0x28 + 16 * lvl)
        for i in range(base.u32(lod + 4)):
            base.put32(lod + 0x28 + 16 * i, child)
        out.append((lod, lvl))
    return out


def write(base, out_dir, name, textures=True):
    os.makedirs(out_dir, exist_ok=True)
    stem = os.path.join(out_dir, name)
    with open(stem + ".NGP", "wb") as f:
        f.write(base.d)
    ptr = bytearray()
    for lst in base.ptr:
        ptr += struct.pack("<I", len(lst)) + struct.pack("<%dI" % len(lst), *lst)
    ptr += base.ptr_tail
    with open(stem + ".PTR", "wb") as f:
        f.write(ptr)
    if not textures:
        return
    src = os.path.splitext(base.path)[0]
    for ext in (".TEX", ".RTX"):
        for cand in (src + ext, src + ext.lower()):
            if os.path.exists(cand):
                shutil.copyfile(cand, stem + ext)
                break


def import_glb(ngp_path, glb_path, out_dir, name=None, log=print, textures=True):
    """Replaces every skinned OBJECT whose `obj_<address>` mesh is in the .glb. Returns {OBJECT: (sub-packets, triangles)}.
    textures=False: only the .NGP/.PTR (a skin or custom folder takes the missing .TEX/.RTX from the base costume)."""
    base = Base(ngp_path)
    glb = Glb(glb_path)
    names = {n.get("name") for n in glb.nodes} | {m.get("name") for m in glb.j.get("meshes", [])}
    done = {}
    for obj in sorted(base.skinned):
        nm = "obj_%x" % obj
        if nm not in names:
            continue
        by_slot = read_mesh(glb, nm, base, obj, log)
        first, rest = _gif_templates(base, obj)
        pkts = []
        for slot, tris in sorted(by_slot.items()):   # each sub-packet draws one texture, picked by its first section
            pkts += build_packets(tris, first if by_slot.keys() == {0} else _with_slot(first, slot), rest)
        replace(base, obj, pkts)
        ntri = sum(len(t) for t in by_slot.values())
        done[obj] = (len(pkts), ntri)
        log("%s: %d triangles in %d sub-packets (%d positions)%s" % (nm, ntri, len(pkts), sum(p.nverts() for p in pkts),
            "; textures %s" % ", ".join("slot %d: %d" % (k, len(v)) for k, v in sorted(by_slot.items())) if len(by_slot) > 1 else ""))
    if not done:
        raise MeshError("no obj_<address> mesh of a skinned OBJECT of %s in the .glb" % os.path.basename(ngp_path))
    for lod, lvl in share_lod(base, done, log):
        log("LOD %x: every level draws level %d" % (lod, lvl))
    write(base, out_dir, name or os.path.splitext(os.path.basename(ngp_path))[0], textures)
    return done


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    name = None
    if "--name" in argv:
        name = argv[argv.index("--name") + 1]
        args.remove(name)
    if len(args) != 3:
        print(__doc__.strip().splitlines()[2])
        return 2
    try:
        import_glb(args[0], args[1], args[2], name)
    except MeshError as e:
        print("error:", e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
