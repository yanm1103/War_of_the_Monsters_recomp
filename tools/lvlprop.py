#!/usr/bin/env python3
"""A static prop for one stage: a .glb (Blender, or a piece of tools/lvlglb.py's export) becomes a prelinked mini image the
level patch places in the stage (docs/FORMATS.md, ".NGP image", "Fixups", "Meshes", "Stage collision").

  python tools/lvlprop.py build <in.glb> <prop dir> --stage CENTRAL [--units blender|game] [--scale S | --height H]
                                [--name NAME] [--title TEXT] [--light vertex|dir] [--disc DIR] [--preview-tex]
  python tools/lvlprop.py info <prop dir> [--at ADDR]

`build` writes into <prop dir>:
- `<NAME>.NGP`: the mini image, prelinked at 0xA00000 like a monster file: word 0 = 1, word 1 = the root GROUP;
- `<NAME>.PTR`: three lists in the monster `.PTR` layout, each a count and byte offsets into the image: pointers (add
  `destination - 0xA00000`), 16-bit texIds (the value written is the prop's texture slot) and OBJECT GS contexts (`gsTexCtx`,
  0x30 bytes: the u32 at the offset is the slot, the rest zero); the loader writes the stage texId and a live context of a stage
  OBJECT with that texId;
- `<NAME>.preview.glb`: the generated packets decoded back (what the game draws), in tools/lvlglb.py's conventions (game units,
  raw GS colors, materials named `tex<texId>`, with no image unless `--preview-tex` embeds the stage's);
- `prop.json`: stage, textures (slot -> texId), template (rule and the OBJECT it found), bounds, collision, counts.
`info` loads a prop, relocates it to ADDR (default the level arena, 0x3E00000) and walks it with the readers of ngp.py and
vifpkt.py: every node, packet and fixup has to be readable there.

Tree: root GROUP (flags 0x0003, no high byte) -> one OBJECT per texture (more when a texture passes the 13-bit numVerts) +
GROUP 0x0102 -> COLL_VOL (op 10, 6 planes from the visual's bounds). OBJECTs and leaves have id1 = id2 = 0.
Template ("first-object-roundtrip"): the first OBJECT of the stage's world root, by address, that round-trips in vifpkt and is
rigid, one texture, strip sections with fog (PRIM.FGE), texture and the asked ABE (default 1, the stages' majority),
fogBurnThru below 255, no alphaSort, environment map, UV scroll, color animation nor animated or sky node above it. The OBJECT
header (shadow and light bits, fogBurnThru) and the GIF tags come from it.
Units: `blender` (default) is a Blender scene (Z up, meters, the scene's X/Y are the game's) scaled by S game units per meter
(default 18, about the size of the game's cars and buildings); `game` is a glb in game units (tools/lvlglb.py), scale 1. Node
transforms are baked into the vertices; the scene origin is the prop's pivot. Colors: `blender` COLOR_0 (linear) to sRGB x 128,
saturated at 0xFF; `game` the raw bytes (COLOR_0 x 255); no COLOR_0 is 0x80. A material `tex<texId>` (a Blender `.001` suffix
is ignored) draws with that stage texture; a material with no image is vertex color only; any other image is refused (new
textures are a later step). Meshes named `coll_*`/`hull_*` are skipped with a warning (the box stands in for them), and
`state<N>_*` (destructible states) is refused. Only the standard library: it runs inside the level editor.
"""
import json
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ngp  # noqa: E402
import ngpmesh  # noqa: E402
import vifpkt  # noqa: E402

BASE = 0xA00000
NAME_RE = re.compile(r"^[A-Z0-9_]{1,8}$")
UNITS_PER_METER = 18.0
MAX_NUMVERTS = 0x1FFF          # _hierobject.bits.numVerts, 13 bits
ARENA = 0x3E00000              # the level patch arena (PS2_RAM_SIZE - 2 MB), info's default address
ROOT_FLAGS = 0x0003            # visual + collision, no option byte (hdTraversePath stops at flags & (option << 8))
COLL_FLAGS = 0x0102            # the GROUP the stages' building collision leaves hang from
COLL_MATID = 1                 # matId of the monsters' and 3-MILE's box hulls
OP_OBJECT, OP_GROUP, OP_COLL_VOL = 0, 1, 10
TEMPLATE_RULE = "first-object-roundtrip"
ANIM_OPS = (17, 25, 35, 38)    # ANIM_XFORM, CHAR_INSTANCE, SKEL_BONE, ANIM_SCALE
SKY_ID = 2


class PropError(Exception):
    pass


def _fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def _f32(v):
    return struct.unpack("<f", struct.pack("<f", v))[0]


# ---------------------------------------------------------------- the stage: template, textures, contexts

def _rigid_packet(img, o):
    pp = img.u32(o + 4) & 0x0FFFFFFF
    if not img.ok(pp, 16) or (img.u32(pp) >> 28) & 7 != 6 or img.u32(pp) & 0x10000:
        return None
    q = (img.u32(pp) & 0xFFFF) + 1
    if not img.ok(pp, 16 * q):
        return None
    return pp, q


def template_check(st, o, abe):
    """None when OBJECT `o` passes the template rule with that ABE, else why not (short)."""
    img = st.img
    if st.op(o) != OP_OBJECT:
        return "not an OBJECT"
    w = img.u32(o + 0xC)
    if img.u16(o + 0x5A) != 1 or (w >> 15) & 1 or (w >> 8) & 1 or w & 0x80E:
        return "header bits"
    if struct.unpack_from("<i", img.d, o + 0x68 - img.base)[0] != -1 or not img.f32(o + 0x2C) < 255:
        return "skinned or no fog"
    if any(img.s16(o + 0x30 + 2 * k) != -1 for k in range(6)):
        return "color/UV animation"
    rp = _rigid_packet(img, o)
    if rp is None:
        return "not rigid"
    raw = bytes(img.d[rp[0] - img.base:rp[0] - img.base + 16 * rp[1]])
    try:
        p = vifpkt.parse(raw, False)
        if vifpkt.encode(p, rp[1]) != raw:
            return "round trip"
    except vifpkt.FormatError:
        return "round trip"
    secs = [s for b in p.batches for s in b.sections]
    g1 = {s.gif[1] for s in secs}
    if len(g1) != 1 or any(s.kind != vifpkt.STRIP for s in secs):
        return "sections"
    g = g1.pop()
    if not ((g >> 20) & 1 and (g >> 19) & 1 and (g >> 21) & 1 == abe):
        return "PRIM"
    ch = st.chain(o)
    if any(st.op(x) in ANIM_OPS or st.ident(x) == SKY_ID for x in ch):
        return "animated or sky"
    return None


def find_template(st, abe=1):
    """(OBJECT, abe) by the template rule; the other ABE when the stage has none with the asked one."""
    wroot = st.root_of.get(st.world) if st.world else None
    objs = sorted(o for o, r in st.root_of.items() if r == wroot and st.op(o) == OP_OBJECT)
    for want in (abe, 1 - abe):
        for o in objs:
            if template_check(st, o, want) is None:
                return o, want
    raise PropError("%s: no OBJECT passes the template rule (%s)" % (st.name, TEMPLATE_RULE))


def template_parts(st, o):
    """What a new OBJECT takes from the template: header bytes (0x70), GIF tag of a batch's first section and of the others."""
    img = st.img
    pp, q = _rigid_packet(img, o)
    p = vifpkt.parse(bytes(img.d[pp - img.base:pp - img.base + 16 * q]), False)
    secs = p.batches[0].sections
    g0 = secs[0].gif
    gn = secs[1].gif if len(secs) > 1 else g0
    return bytes(img.d[o - img.base:o + 0x70 - img.base]), g0, gn


def stage_ctx(img, tex_id, objs):
    """(OBJECT, 0x30-byte GS context) of the first OBJECT of `objs` (stage_objects: by address) whose texture slot k is
    `tex_id`, or None. This is the context a prop's slot with that texId takes (the loader reads the live one at the same
    OBJECT)."""
    for o in objs:
        n = img.u16(o + 0x5A)
        for k in range(min(n, 5)):
            if img.u16(o + 0x50 + 2 * k) == tex_id:
                a = o + 0x70 + 0x30 * k - img.base
                return o, bytes(img.d[a:a + 0x30])
    return None


def stage_objects(st):
    return sorted(o for o in st.root_of if st.op(o) == OP_OBJECT)


def world_clamp(st):
    """(wms, wmt) of the CLAMP_1 the world GROUP's context sets (0 repeat, 1 clamp), or None."""
    img = st.img
    if not st.world:
        return None
    c = img.u32(st.world + 4) & 0x0FFFFFFF
    if not c or not img.ok(c, 0x20) or img.u32(c + 0x14) != 0x10000000 or img.u32(c + 0x18) != 0xE:
        return None
    for i in range(min(img.u32(c + 0x10) & 0x7FFF, 8)):
        if img.ok(c + 0x30 + 16 * i) and img.u32(c + 0x28 + 16 * i) & 0xFF == 0x08:
            d = img.u32(c + 0x20 + 16 * i)
            return d & 3, (d >> 2) & 3
    return None


def stage_tex_ids(disc, stage):
    import tex
    path = tex.sibling(os.path.join(disc, "LVL", stage + ".NGP"), ".TEX")
    if not path:
        return set()
    data, _ = tex.read_file(path)
    return {p.texId for p in tex.parse(data)["packets"]}


# ---------------------------------------------------------------- the .glb

def _mat_mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def _node_local(nd):
    """Local matrix of a glTF node (rows[r][c], column vectors)."""
    if "matrix" in nd:
        return ngpmesh._col(nd["matrix"])
    x, y, z, w = nd.get("rotation", (0.0, 0.0, 0.0, 1.0))
    sx, sy, sz = nd.get("scale", (1.0, 1.0, 1.0))
    tx, ty, tz = nd.get("translation", (0.0, 0.0, 0.0))
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    s = (sx, sy, sz)
    t = (tx, ty, tz)
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0.0, 0.0, 0.0, 1.0]]


def _det3(m):
    return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))


def _normal_matrix(m):
    """Inverse transpose of the 3x3 part (normals under a non-uniform scale)."""
    d = _det3(m)
    if abs(d) < 1e-20:
        return None
    a = [row[:3] for row in m[:3]]
    cof = [[(a[(r + 1) % 3][(c + 1) % 3] * a[(r + 2) % 3][(c + 2) % 3] - a[(r + 1) % 3][(c + 2) % 3] * a[(r + 2) % 3][(c + 1) % 3])
            for c in range(3)] for r in range(3)]
    return [[cof[r][c] / d for c in range(3)] for r in range(3)]


def _srgb(c):
    c = max(0.0, c)
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def _material_key(glb, prim, warn):
    """("tex", texId), or ("color",) for a material with no image; raises for an image that is not a stage texture."""
    mats = glb.j.get("materials", [])
    if "material" not in prim or prim["material"] >= len(mats):
        return ("color",), (1.0, 1.0, 1.0, 1.0)
    m = mats[prim["material"]]
    name = m.get("name") or ""
    pbr = m.get("pbrMetallicRoughness", {})
    factor = tuple(pbr.get("baseColorFactor", (1.0, 1.0, 1.0, 1.0)))
    t = re.match(r"tex(\d+)", name)
    if t:
        return ("tex", int(t.group(1))), factor
    if "baseColorTexture" in pbr:
        raise PropError("material %r has an image of its own: new textures are not supported yet (name the material "
                        "tex<texId> to draw with one of the stage's textures)" % name)
    return ("color",), factor


def read_glb(path, units, scale=None, height=None, warn=print):
    """Triangles of the .glb in the game's axes and units: ({key: [((x, y, z), n8, uv16, rgba) x 3]}, scale, stats).
    key = ("tex", texId) or ("color",)."""
    glb = ngpmesh.Glb(path)
    j = glb.j
    nodes = glb.nodes
    scene = j.get("scenes", [{}])[j.get("scene", 0)] if j.get("scenes") else {"nodes": list(range(len(nodes)))}
    world = {}
    stack = [(i, [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]) for i in scene.get("nodes", [])]
    while stack:
        i, pm = stack.pop()
        if i in world:
            continue
        world[i] = _mat_mul(pm, _node_local(nodes[i]))
        stack.extend((k, world[i]) for k in nodes[i].get("children", []))
    raw = {}           # key -> [[corner, corner, corner]] with corners (pos glTF world, normal, uv, color linear/raw)
    skipped = []
    for i, m in sorted(world.items()):
        nd = nodes[i]
        if "mesh" not in nd:
            continue
        mesh = j["meshes"][nd["mesh"]]
        name = nd.get("name") or mesh.get("name") or "node%d" % i
        low = name.lower()
        if low.startswith(("coll_", "hull_")):
            skipped.append(name)
            continue
        if re.match(r"state\d+_", low):
            raise PropError("mesh %s: destructible states are not supported yet (a static prop only)" % name)
        if "skin" in nd:
            raise PropError("mesh %s has a skin: a prop is static (apply the armature in Blender first)" % name)
        nm = _normal_matrix(m)
        flip = _det3(m) < 0
        for prim in mesh.get("primitives", []):
            if prim.get("mode", 4) != 4:
                raise PropError("mesh %s: primitive mode %d (triangles only)" % (name, prim["mode"]))
            key, factor = _material_key(glb, prim, warn)
            at = prim["attributes"]
            pos = glb.accessor(at["POSITION"])
            nrm = glb.accessor(at["NORMAL"]) if "NORMAL" in at else None
            uv = glb.accessor(at["TEXCOORD_0"]) if "TEXCOORD_0" in at else None
            col = glb.accessor(at["COLOR_0"]) if "COLOR_0" in at else None
            if key[0] == "tex" and uv is None:
                warn("mesh %s: material tex%d without UVs (every corner at UV 0, 0)" % (name, key[1]))
            idx = [v[0] for v in glb.accessor(prim["indices"])] if "indices" in prim else list(range(len(pos)))

            def corner(v):
                p = pos[v]
                wp = tuple(m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] for r in range(3))
                n = None
                if nrm is not None and nm is not None:
                    q = nrm[v]
                    n = tuple(nm[r][0] * q[0] + nm[r][1] * q[1] + nm[r][2] * q[2] for r in range(3))
                t = uv[v] if uv is not None else None
                c = tuple(col[v]) + ((1.0,) if len(col[v]) == 3 else ()) if col is not None else None
                return wp, n, t, c, factor
            lst = raw.setdefault(key, [])
            for k in range(0, len(idx) - 2, 3):
                tri = [corner(idx[k]), corner(idx[k + 1]), corner(idx[k + 2])]
                if flip:
                    tri = [tri[0], tri[2], tri[1]]
                lst.append(tri)
    if skipped:
        warn("collision meshes are a later step; skipped (the automatic box stands in): %s" % ", ".join(skipped))
    if not any(raw.values()):
        raise PropError("%s: no triangles to draw" % path)

    def game(p):                    # glTF Y up -> game Z up
        return (p[0], -p[2], p[1])
    zs = [game(c[0])[2] for tris in raw.values() for t in tris for c in t]
    if scale:
        s = scale
    elif height:
        h = max(zs) - min(zs)
        if h <= 1e-9:
            raise PropError("the mesh is flat (no height) and --height was given")
        s = height / h
    else:
        s = UNITS_PER_METER if units == "blender" else 1.0
    out, stats = {}, {"degenerate": 0, "triangles": 0}
    for key, tris in raw.items():
        lst = out.setdefault(key, [])
        for t in tris:
            cs = []
            for wp, n, uvv, c, factor in t:
                p = tuple(_f32(x * s) for x in game(wp))
                if n is None:
                    n8 = None
                else:
                    g = game(n)
                    ln = math.sqrt(sum(x * x for x in g))
                    n8 = tuple(max(-127, min(127, int(round(x / ln * 127)))) for x in g) if ln > 1e-12 else None
                if key[0] == "tex":
                    uvv = uvv if uvv is not None else (0.0, 0.0)
                    uv16 = tuple(max(-32768, min(32767, int(round(x * 4096)))) for x in uvv)
                else:
                    uv16 = (4096, 4096)      # untextured: PRIM.TME 0; the stages' files have 1.0 there
                if units == "blender":
                    lin = c if c is not None else (1.0, 1.0, 1.0, 1.0)
                    lin = tuple(a * b for a, b in zip(lin, factor))
                    rgba = tuple(min(255, int(round(_srgb(x) * 128))) for x in lin[:3]) + (
                        min(255, max(0, int(round(lin[3] * 128)))),)
                else:
                    if c is None:
                        rgba = tuple(min(255, int(round(0x80 * f))) for f in factor)
                    else:
                        rgba = tuple(min(255, max(0, int(round(x * 255 * f)))) for x, f in zip(c, factor))
                cs.append([p, n8, uv16, rgba])
            if cs[0][0] == cs[1][0] or cs[1][0] == cs[2][0] or cs[0][0] == cs[2][0]:
                stats["degenerate"] += 1
                continue
            for c in cs:            # a corner without a normal takes the face's
                if c[1] is None:
                    a, b, d = (cs[0][0], cs[1][0], cs[2][0])
                    u = [b[k] - a[k] for k in range(3)]
                    v = [d[k] - a[k] for k in range(3)]
                    fn = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
                    ln = math.sqrt(sum(x * x for x in fn)) or 1.0
                    c[1] = tuple(max(-127, min(127, int(round(x / ln * 127)))) for x in fn)
            lst.append(tuple(tuple(c) for c in cs))
            stats["triangles"] += 1
    return {k: v for k, v in out.items() if v}, s, stats


# ---------------------------------------------------------------- packets and OBJECTs

def _gif(g, nloop, sel, textured):
    w1 = g[1] if textured else g[1] & ~(1 << 19)       # PRIM.TME
    return ((g[0] & ~0x7FFF) | nloop, w1, g[2], (g[3] & ~0x7FFF) | sel)


def _sections(strips):
    """Strips packed into sections of at most 44 vertices, each strip starting at an even place of its section (the
    winding alternates from the section's start: a strip that would start at an odd place gets its first vertex twice, a
    degenerate triangle the GS draws as nothing). A strip longer than the room left is cut at an even vertex."""
    secs, cur = [], None
    for strip in strips:
        rest = strip
        while rest:
            pad = 1 if cur is not None and len(cur.idx) % 2 else 0
            room = vifpkt.MAX_SECTION - len(cur.idx) - pad if cur is not None else 0
            if len(rest) > room and room < 4:
                cur = vifpkt.Section()
                secs.append(cur)
                pad, room = 0, vifpkt.MAX_SECTION
            if len(rest) <= room:
                take, nxt = rest, []
            else:
                m = room - room % 2
                take, nxt = rest[:m], rest[m - 2:]
            if cur.idx:
                cur.adc.append(len(cur.idx))
            for v in take[:1] * pad + take:
                cur.idx.append(v[0])
                cur.uv.append(v[1])
                cur.rgba.append(v[2])
            rest = nxt
            if rest:
                cur = None
    return secs


def level_batches(tris, gif0, gifn, textured=True):
    """Rigid batches (vifpkt.Batch) for triangles of corners (pos as 3 raw u32, n8, uv16, rgba), each triangle's winding
    kept. Each batch's first section loads the OBJECT's texture (GIF word 3 = 1, as the stages do), the others keep it (0);
    untextured: PRIM.TME 0, word 3 0. Batches of up to 190 positions (ngpmesh._groups), strips by ngpmesh._strips."""
    out = []
    for grp in ngpmesh._groups(tris):
        b = vifpkt.Batch()
        pos_index = {}
        for tri in grp:
            for c in tri:
                if c[:2] not in pos_index:     # position and normal: a crease has one entry per normal
                    pos_index[c[:2]] = len(b.pos)
                    b.pos.append(c[0])
                    b.nrm.append(c[1])
        svert = {}
        local = [tuple(svert.setdefault(k, k) for k in ((pos_index[c[:2]], c[2], c[3]) for c in tri)) for tri in grp]
        b.sections = _sections(ngpmesh._strips(local))
        for k, s in enumerate(b.sections):
            s.gif = _gif(gif0 if k == 0 else gifn, len(s.idx), 1 if (k == 0 and textured) else 0, textured)
        out.append(b)
    return out


def split_objects(batches):
    """Batches in groups whose strip vertices fit numVerts (13 bits)."""
    groups, cur, n = [], [], 0
    for b in batches:
        k = sum(len(s.idx) for s in b.sections)
        if cur and n + k > MAX_NUMVERTS:
            groups.append(cur)
            cur, n = [], 0
        cur.append(b)
        n += k
    if cur:
        groups.append(cur)
    return groups


class Mini:
    """The mini image being written, at BASE, with its three fixup lists."""

    def __init__(self):
        self.d = bytearray(16)
        self.ptr = [[], [], []]

    def alloc(self, size, align=16):
        a = (len(self.d) + align - 1) & ~(align - 1)
        self.d += bytes(a + size - len(self.d))
        return BASE + a

    def u16(self, a, v):
        struct.pack_into("<H", self.d, a - BASE, v)

    def s16(self, a, v):
        struct.pack_into("<h", self.d, a - BASE, v)

    def u32(self, a, v):
        struct.pack_into("<I", self.d, a - BASE, v & 0xFFFFFFFF)

    def f(self, a, v):
        struct.pack_into("<f", self.d, a - BASE, v)

    def raw(self, a, b):
        self.d[a - BASE:a - BASE + len(b)] = b

    def ptr_to(self, a, target, keep_high=True):
        hi = struct.unpack_from("<I", self.d, a - BASE)[0] & 0xF0000000 if keep_high else 0
        self.u32(a, hi | target)
        self.ptr[0].append(a - BASE)


def _sphere(pts):
    lo = [min(p[c] for p in pts) for c in range(3)]
    hi = [max(p[c] for p in pts) for c in range(3)]
    ctr = [0.5 * (a + b) for a, b in zip(lo, hi)]
    return ctr, max(math.dist(ctr, p) for p in pts), lo, hi


def _batch_points(batches):
    return [tuple(struct.unpack("<f", struct.pack("<I", x))[0] for x in p) for b in batches for p in b.pos]


def level_object(mb, header, batches, slot, light):
    """A rigid stage OBJECT for the batches: the template's header (0x70 bytes) with id 0, polyPkt, numVerts, sphere, one
    texture slot (texId and context hold the slot; None: untextured) and dirLight per `light`. Returns its address."""
    pkt = vifpkt.Packet(False)
    pkt.batches = batches
    blob = vifpkt.encode(pkt)
    start = mb.alloc(len(blob), 0x80)
    mb.raw(start, blob)
    a = mb.alloc(0xA0, 16)
    mb.raw(a, header)
    mb.u32(a, OP_OBJECT)                                   # id1 = id2 = 0
    mb.ptr_to(a + 4, start)
    nverts = sum(len(s.idx) for b in batches for s in b.sections)
    bits = struct.unpack_from("<I", header, 0xC)[0]
    bits = (bits & ~(0x1FFF << 16)) | (nverts << 16)
    bits = (bits | 0x100) if light == "dir" else (bits & ~0x100)
    mb.u32(a + 0xC, bits)
    ctr, rad, _lo, _hi = _sphere(_batch_points(batches))
    for c in range(3):
        mb.f(a + 0x10 + 4 * c, ctr[c])
    mb.f(a + 0x1C, rad)
    mb.f(a + 8, rad * rad)
    for k in range(5):
        mb.u16(a + 0x50 + 2 * k, 0)
    if slot is None:
        mb.u16(a + 0x5A, 0)
    else:
        mb.u16(a + 0x50, slot)
        mb.ptr[1].append(a + 0x50 - BASE)
        mb.u16(a + 0x5A, 1)
        mb.raw(a + 0x70, struct.pack("<I", slot) + bytes(0x2C))
        mb.ptr[2].append(a + 0x70 - BASE)
    mb.u32(a + 0x64, 0)
    mb.u32(a + 0x68, 0xFFFFFFFF)
    return a, nverts, len(blob)


def _group(mb, flags, kids, pts):
    g = mb.alloc(0x20 + 4 * max(1, len(kids)), 16)
    mb.u32(g, OP_GROUP)
    mb.u16(g + 8, len(kids))
    mb.u16(g + 0xA, flags)
    ctr, rad, _lo, _hi = _sphere(pts)
    mb.f(g + 0xC, rad * rad)
    for c in range(3):
        mb.f(g + 0x10 + 4 * c, ctr[c])
    mb.f(g + 0x1C, rad)
    for k, kid in enumerate(kids):
        mb.ptr_to(g + 0x20 + 4 * k, kid, keep_high=False)
    return g


def box_hull(mb, lo, hi, matid=COLL_MATID):
    """COLL_VOL (op 10) of 6 planes, inside when n . p < d (the monsters' order: +z, +y, -z, -y, +x, -x)."""
    cv = mb.alloc(0x20 + 16 * 6, 16)
    mb.u32(cv, OP_COLL_VOL)
    mb.s16(cv + 4, 6)
    mb.s16(cv + 6, -1)
    mb.u16(cv + 8, matid)
    ctr = [0.5 * (a + b) for a, b in zip(lo, hi)]
    for c in range(3):
        mb.f(cv + 0x10 + 4 * c, ctr[c])
    mb.f(cv + 0x1C, 0.5 * math.dist(lo, hi))
    planes = [((0, 0, 1), hi[2]), ((0, 1, 0), hi[1]), ((0, 0, -1), -lo[2]), ((0, -1, 0), -lo[1]), ((1, 0, 0), hi[0]),
              ((-1, 0, 0), -lo[0])]
    for q, (n, d) in enumerate(planes):
        mb.raw(cv + 0x20 + 16 * q, struct.pack("<4f", float(n[0]), float(n[1]), float(n[2]), d))
    return cv


# ---------------------------------------------------------------- build

def build(glb_path, out_dir, stage, units="blender", scale=None, height=None, name=None, title=None, light="vertex",
          disc=None, abe=1, preview_tex=False, log=print):
    """Writes the prop into out_dir; returns the prop.json dict."""
    import lvl
    if units not in ("blender", "game"):
        raise PropError("--units is blender or game")
    if light not in ("vertex", "dir"):
        raise PropError("--light is vertex or dir")
    name = (name or os.path.basename(os.path.normpath(out_dir))).upper()
    if not NAME_RE.match(name):
        raise PropError("prop name %r: 1 to 8 letters, digits or _" % name)
    disc = disc or os.path.join(lvl.REPO, "disc")
    stage = stage.upper()
    st = lvl.Stage(stage, disc)
    by_key, s, gstats = read_glb(glb_path, units, scale, height, log)

    tmpl, abe = find_template(st, abe)
    header, gif0, gifn = template_parts(st, tmpl)
    log("%s: template OBJECT 0x%X (%s, ABE %d), bits %08x" % (stage, tmpl, TEMPLATE_RULE, abe, st.img.u32(tmpl + 0xC)))
    objs = stage_objects(st)
    have = stage_tex_ids(disc, stage)
    clamp = world_clamp(st)
    textures, slot_of = [], {}
    for key in sorted(by_key, key=lambda k: (k[0] != "tex", k[1:] if len(k) > 1 else ())):
        if key[0] != "tex":
            continue
        tid = key[1]
        if tid not in have:
            raise PropError("tex%d is not a texture of %s.TEX" % (tid, stage))
        found = stage_ctx(st.img, tid, objs)
        if found is None:
            raise PropError("tex%d: no OBJECT of %s draws with it (the loader takes its context from one)" % (tid, stage))
        o, ctx = found
        slot_of[tid] = len(textures)
        textures.append({"slot": len(textures), "texId": tid, "ctxFrom": "0x%X" % o,
                         "tex0": "0x%016X" % struct.unpack_from("<Q", ctx, 0)[0]})
        if clamp and 1 in clamp:
            uvs = [c[2] for t in by_key[key] for c in t]
            if any(not (0 <= u <= 4096) for uv in uvs for u in uv):
                log("warning: tex%d has UVs outside 0..1 and the stage clamps (CLAMP_1 %s): the edge texels stretch" % (
                    tid, clamp))

    mb = Mini()
    kids, stats_obj, all_pts = [], [], []
    ntris = nverts = pbytes = 0
    for key in sorted(by_key, key=lambda k: (k[0] != "tex", k[1:] if len(k) > 1 else ())):
        tris = [tuple((tuple(_fbits(x) for x in c[0]), c[1], c[2], c[3]) for c in t) for t in by_key[key]]
        textured = key[0] == "tex"
        for grp in split_objects(level_batches(tris, gif0, gifn, textured)):
            a, nv, nb = level_object(mb, header, grp, slot_of[key[1]] if textured else None, light)
            kids.append(a)
            stats_obj.append({"object": "0x%X" % a, "texId": key[1] if textured else None, "vertices": nv})
            nverts += nv
            pbytes += nb
            all_pts += _batch_points(grp)
        ntris += len(tris)
    _ctr, _rad, lo, hi = _sphere(all_pts)
    cv = box_hull(mb, lo, hi)
    cg = _group(mb, COLL_FLAGS, [cv], [lo, hi])
    root = _group(mb, ROOT_FLAGS, kids + [cg], all_pts)
    mb.u32(BASE, 1)
    mb.u32(BASE + 4, root)
    mb.d += bytes((-len(mb.d)) % 16)

    os.makedirs(out_dir, exist_ok=True)
    stem = os.path.join(out_dir, name)
    with open(stem + ".NGP", "wb") as f:
        f.write(mb.d)
    with open(stem + ".PTR", "wb") as f:
        for lst in mb.ptr:
            f.write(struct.pack("<I", len(lst)) + struct.pack("<%dI" % len(lst), *lst))
    src = os.path.relpath(os.path.abspath(glb_path), os.path.abspath(out_dir)).replace("\\", "/")
    meta = {"format": 1, "model": name, "name": title or os.path.splitext(os.path.basename(glb_path))[0], "stage": stage,
            "source": src, "units": units, "scale": s, "kind": "static", "light": light, "textures": textures,
            "template": {"rule": TEMPLATE_RULE, "abe": abe, "object": "0x%X" % tmpl, "bits": "0x%08X" % st.img.u32(tmpl + 0xC)},
            "bounds": {"min": [round(x, 4) for x in lo], "max": [round(x, 4) for x in hi]},
            "collision": {"auto": "box", "poly": 0, "hull": 0, "tris": 0, "matId": COLL_MATID},
            "objects": stats_obj, "triangles": ntris, "vertices": nverts, "packetBytes": pbytes, "bytes": len(mb.d),
            "root": "0x%X" % root, "files": {"ngp": name + ".NGP", "ptr": name + ".PTR", "preview": name + ".preview.glb"}}
    with open(os.path.join(out_dir, "prop.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=1)
        f.write("\n")
    prop = load_prop(out_dir)
    preview(prop, st, disc, stem + ".preview.glb", preview_tex)
    log("%s: %d triangles (%d degenerate dropped) in %d OBJECTs, %d bytes (%d of packets, %.1f per triangle), scale %.4g, "
        "bounds %s .. %s" % (name, ntris, gstats["degenerate"], len(kids), len(mb.d), pbytes, len(mb.d) / max(1, ntris), s,
                             [round(x, 2) for x in lo], [round(x, 2) for x in hi]))
    return meta


# ---------------------------------------------------------------- reading a prop (what the loader uses)

class Prop:
    """A prop as written by build: `ngp` (bytes, prelinked at BASE), `ptr` ([pointers, texIds, contexts]: byte offsets),
    `root` (byte offset of the root GROUP), `textures` (list of {"slot", "texId", ...}), `bounds` ((min xyz), (max xyz)),
    `meta` (prop.json), `name`, `stage`, `folder`."""

    def __init__(self, folder, meta, ngp_bytes, ptr):
        self.folder, self.meta, self.ngp, self.ptr = folder, meta, ngp_bytes, ptr
        self.name, self.stage = meta["model"], meta["stage"]
        self.textures = meta["textures"]
        self.bounds = (tuple(meta["bounds"]["min"]), tuple(meta["bounds"]["max"]))
        if struct.unpack_from("<I", ngp_bytes, 0)[0] != 1:
            raise PropError("%s: word 0 of the image is not 1" % folder)
        self.root = struct.unpack_from("<I", ngp_bytes, 4)[0] - BASE
        if not 0 <= self.root < len(ngp_bytes):
            raise PropError("%s: root outside the image" % folder)

    @property
    def size(self):
        return len(self.ngp)

    def relocate(self, at):
        """The image at address `at` (a multiple of 0x80: packets are 0x80-aligned): header word 1 and the pointer list get
        `at - BASE`; texIds and contexts still hold the slots."""
        if at % 0x80:
            raise PropError("address 0x%X is not 0x80-aligned" % at)
        d = bytearray(self.ngp)
        delta = at - BASE
        for off in [4] + self.ptr[0]:
            v = struct.unpack_from("<I", d, off)[0]
            struct.pack_into("<I", d, off, (v + delta) & 0xFFFFFFFF)
        return d

    def resolve(self, at, ctx_of):
        """relocate(at) with each slot's texId written and each context filled: ctx_of(texId) -> 0x30 bytes."""
        d = self.relocate(at)
        tid = {t["slot"]: t["texId"] for t in self.textures}
        for off in self.ptr[1]:
            struct.pack_into("<H", d, off, tid[struct.unpack_from("<H", d, off)[0]])
        for off in self.ptr[2]:
            c = ctx_of(tid[struct.unpack_from("<I", d, off)[0]])
            if len(c) != 0x30:
                raise PropError("context of %d bytes" % len(c))
            d[off:off + 0x30] = c
        return d


def load_prop(folder):
    with open(os.path.join(folder, "prop.json"), encoding="utf-8") as f:
        meta = json.load(f)
    if meta.get("format") != 1:
        raise PropError("%s: prop.json format %r" % (folder, meta.get("format")))
    files = meta.get("files", {})
    with open(os.path.join(folder, files.get("ngp", meta["model"] + ".NGP")), "rb") as f:
        data = f.read()
    with open(os.path.join(folder, files.get("ptr", meta["model"] + ".PTR")), "rb") as f:
        raw = f.read()
    o, ptr = 0, []
    for _ in range(3):
        n = struct.unpack_from("<I", raw, o)[0]
        ptr.append(list(struct.unpack_from("<%dI" % n, raw, o + 4)))
        o += 4 + 4 * n
    for k, lst in enumerate(ptr):
        for off in lst:
            if not 0 <= off <= len(data) - (2 if k == 1 else 4):
                raise PropError("%s: fixup %d at 0x%X outside the image" % (folder, k, off))
    return Prop(folder, meta, data, ptr)


def walk(img, root):
    """[(address, op)] of the prop's tree from the root, refusing what a prop does not hold."""
    out, seen, stack = [], set(), [root]
    while stack:
        n = stack.pop()
        if n in seen:
            continue
        if not img.ok(n, 0x20):
            raise PropError("node 0x%X outside the image" % n)
        seen.add(n)
        op = img.u32(n) & 63
        if op not in (OP_GROUP, OP_OBJECT, OP_COLL_VOL):
            raise PropError("node 0x%X: op %d (a static prop holds GROUP, OBJECT and COLL_VOL)" % (n, op))
        out.append((n, op))
        if op == OP_GROUP:
            stack.extend(img.u32(n + 0x20 + 4 * k) & 0x0FFFFFFF for k in range(img.u16(n + 8)))
    return out


def check(prop, at=ARENA, log=print):
    """Relocates the prop to `at` and reads it back: tree, packets (vifpkt round trip and ngp.decode_mesh), fixups, bounds.
    Returns {"objects", "triangles", "vertices", "bounds"}; raises PropError on anything unreadable."""
    import lvl
    d = prop.relocate(at)
    img = ngp.Image(bytes(d), at)
    root = img.u32(at + 4)
    if root != at + prop.root:
        raise PropError("root 0x%X after relocation (expected 0x%X)" % (root, at + prop.root))
    for off in prop.ptr[0]:
        t = img.u32(at + off) & 0x0FFFFFFF
        if not img.ok(t, 4):
            raise PropError("pointer at +0x%X goes to 0x%X, outside the image" % (off, t))
    nslots = len(prop.textures)
    for off in prop.ptr[1]:
        if img.u16(at + off) >= nslots:
            raise PropError("texId slot %d at +0x%X (prop has %d)" % (img.u16(at + off), off, nslots))
    for off in prop.ptr[2]:
        if img.u32(at + off) >= nslots or any(d[off + 4:off + 0x30]):
            raise PropError("context at +0x%X does not hold a slot" % off)
    tris = verts = nobj = 0
    pts, hulls = [], []
    for n, op in walk(img, root):
        if op == OP_OBJECT:
            nobj += 1
            rp = _rigid_packet(img, n)
            if rp is None or img.u32(n + 0x68) != 0xFFFFFFFF or img.u32(n) >> 6:
                raise PropError("OBJECT 0x%X: not a rigid OBJECT with id 0" % n)
            raw = bytes(img.d[rp[0] - at:rp[0] - at + 16 * rp[1]])
            p = vifpkt.parse(raw, False)
            if vifpkt.encode(p, rp[1]) != raw:
                raise PropError("OBJECT 0x%X: packet does not round-trip" % n)
            nv = sum(len(s.idx) for b in p.batches for s in b.sections)
            if nv != (img.u32(n + 0xC) >> 16) & 0x1FFF:
                raise PropError("OBJECT 0x%X: numVerts %d for %d strip vertices" % (n, (img.u32(n + 0xC) >> 16) & 0x1FFF, nv))
            verts += nv
            md = ngp.decode_mesh(img, n)
            tris += len(md.tris) // 3
            pts += [(v.x, v.y, v.z) for v in md.tris]
        elif op == OP_COLL_VOL:
            hulls.append(lvl.aabb(lvl.leaf_points(img, n)))
    lo = [min(p[c] for p in pts) for c in range(3)]
    hi = [max(p[c] for p in pts) for c in range(3)]
    for c in range(3):
        if abs(lo[c] - prop.bounds[0][c]) > 1e-2 or abs(hi[c] - prop.bounds[1][c]) > 1e-2:
            raise PropError("decoded bounds %s .. %s differ from prop.json" % (lo, hi))
    for h in hulls:
        if h is None or max(abs(h[0][c] - lo[c]) + abs(h[1][c] - hi[c]) for c in range(3)) > 1e-2:
            raise PropError("box hull %s does not match the bounds" % (h,))
    if tris != prop.meta.get("triangles", tris):
        raise PropError("decoded %d triangles, prop.json says %d" % (tris, prop.meta["triangles"]))
    log("%s (%s): %d bytes at 0x%X, %d OBJECTs, %d triangles, %d strip vertices, %d box hulls, %d/%d/%d fixups, bounds %s .. %s"
        % (prop.name, prop.stage, prop.size, at, nobj, tris, verts, len(hulls), len(prop.ptr[0]), len(prop.ptr[1]),
           len(prop.ptr[2]), [round(x, 2) for x in lo], [round(x, 2) for x in hi]))
    return {"objects": nobj, "triangles": tris, "vertices": verts, "bounds": (lo, hi)}


# ---------------------------------------------------------------- preview

def preview(prop, st, disc, out, textures=False):
    """The prop's OBJECTs decoded back from its own packets into a .glb in tools/lvlglb.py's conventions. Without
    `textures` the materials only name the stage texture (`tex<texId>`, with lvlglb's extras: the editor draws them with
    the stage's own), so the file carries nothing of the game; with it they embed the images from the user's disc."""
    import collections
    import gltf
    import tex
    objs = stage_objects(st)
    ctx = {t["texId"]: stage_ctx(st.img, t["texId"], objs)[1] for t in prop.textures}
    d = prop.resolve(BASE, ctx.get)
    img = ngp.Image(bytes(d), BASE)
    g = gltf.Glb()
    g.j["extensionsUsed"] = ["KHR_materials_unlit"]
    g.j["asset"]["extras"] = {"gsRaw": True}
    stats = collections.Counter()
    if textures:
        refs = sorted({(st.img.u16(o + 0x50 + 2 * k), st.img.u32(o + 0x70 + 0x30 * k) | (st.img.u32(o + 0x74 + 0x30 * k) << 32))
                       for o in objs for k in range(min(st.img.u16(o + 0x5A), 5))})
        base = os.path.join(disc, "LVL", st.name)
        lib = tex.Library(tex.sibling(base + ".NGP", ".TEX"), tex.sibling(base + ".NGP", ".RTX"), refs=refs)
        material = gltf.Materials(g, img, lib, True, stats, raw=True).get
    else:
        cache = {}

        def material(node, slot, blend, gs, extras):
            tid = img.u16(node + 0x50) if slot != ngp.SLOT_NOTEX and img.u16(node + 0x5A) else None
            key = (tid, blend, tuple(sorted(extras.items())))
            if key not in cache:
                m = {"name": "tex%d" % tid if tid is not None else "cor_de_vertice", "doubleSided": True,
                     "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0},
                     "extensions": {"KHR_materials_unlit": {}},
                     "extras": dict(extras, gs={"blend": gs[0] if blend else None, "aref": gs[1],
                                                "zmsk": bool(gs[2]) if blend else False})}
                if blend:
                    m["alphaMode"] = "BLEND"
                g.j["materials"].append(m)
                cache[key] = len(g.j["materials"]) - 1
            return cache[key]
    gs = ngp.gs_apply(st.img, ngp.GS_DEFAULT, st.img.u32(st.world + 4)) if st.world else ngp.GS_DEFAULT
    top = {"name": prop.name, "extras": {"prop": prop.name, "stage": prop.stage}, "children": []}
    g.j["nodes"].append(top)
    g.j["scenes"][0]["nodes"].append(0)
    for n, op in walk(img, img.u32(BASE + 4)):
        if op != OP_OBJECT:
            continue
        md = ngp.decode_mesh(img, n)
        burn, flags = img.f32(n + 0x2C), img.u16(n + 0xC)
        extras = {"fog": bool(burn < 255), "ot": bool(flags & 0x8000), "zmsk": bool(gs[2])}
        if flags & 0x100:
            extras["dirLight"] = True
        groups = {}
        for k in range(0, len(md.tris), 3):
            groups.setdefault(md.tris[k + 2].slot, []).append(md.tris[k:k + 3])
        prims = []
        for slot, tris in sorted(groups.items()):
            verts, index, idx = {}, [], []
            for tri in tris:
                for v in tri:
                    p = gltf.to_y_up((v.x, v.y, v.z))
                    ln = (v.nx * v.nx + v.ny * v.ny + v.nz * v.nz) ** 0.5
                    nn = gltf.to_y_up((v.nx / ln, v.ny / ln, v.nz / ln)) if ln > 1e-6 else (0.0, 1.0, 0.0)
                    k = (p, nn, (v.u, v.v), (v.r / 255.0, v.g / 255.0, v.b / 255.0, v.a / 255.0))
                    if k not in verts:
                        verts[k] = len(verts)
                        index.append(k)
                    idx.append(verts[k])
            attrs = {"POSITION": g.accessor("f", 3, [k[0] for k in index], minmax=True),
                     "NORMAL": g.accessor("f", 3, [k[1] for k in index]),
                     "COLOR_0": g.accessor("f", 4, [k[3] for k in index])}
            if slot != ngp.SLOT_NOTEX:
                attrs["TEXCOORD_0"] = g.accessor("f", 2, [k[2] for k in index])
            ctype, fmt = (5123, "H") if len(index) < 65536 else (5125, "I")
            prims.append({"attributes": attrs, "indices": g.accessor(fmt, 1, idx, ctype=ctype, target=34963),
                          "material": material(n, slot, md.abe, gs, extras)})
        g.j["meshes"].append({"name": "obj_%x" % (n - BASE), "primitives": prims})
        g.j["nodes"].append({"name": "obj_%x" % (n - BASE), "mesh": len(g.j["meshes"]) - 1})
        top["children"].append(len(g.j["nodes"]) - 1)
    g.write(out)


# ---------------------------------------------------------------- command line

def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    opts = {}
    for opt in ("--stage", "--units", "--scale", "--height", "--name", "--title", "--light", "--disc", "--at"):
        if opt in argv:
            v = argv[argv.index(opt) + 1]
            args.remove(v)
            opts[opt[2:]] = v
    try:
        if len(args) == 3 and args[0] == "build":
            if "stage" not in opts:
                print("error: --stage is required (a prop belongs to one stage)")
                return 2
            build(args[1], args[2], opts["stage"], opts.get("units", "blender"),
                  float(opts["scale"]) if "scale" in opts else None, float(opts["height"]) if "height" in opts else None,
                  opts.get("name"), opts.get("title"), opts.get("light", "vertex"), opts.get("disc"),
                  preview_tex="--preview-tex" in argv)
            return 0
        if len(args) == 2 and args[0] == "info":
            check(load_prop(args[1]), int(opts["at"], 0) if "at" in opts else ARENA)
            return 0
    except (PropError, ngpmesh.MeshError, vifpkt.FormatError) as e:
        print("error:", e)
        return 1
    for line in __doc__.strip().splitlines()[3:6]:
        print(line)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
