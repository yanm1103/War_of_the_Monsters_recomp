#!/usr/bin/env python3
"""An original object of a stage as a standalone .glb, ready for lvlprop.build (a prop made from another stage's building or prop).

  python tools/lvlorig.py <scene.glb> <handle> <out.glb>     (scene.glb: tools/lvlglb.py's export of the stage)

The object's top node becomes the scene's only root, with its world matrix cleared (the prop stands at the origin, facing heading 0).
The node's states come along: the broken ones (the nodes lvlglb.py marks hidden) have their meshes renamed state<N>_<name>, which
makes lvlprop.build a destructible prop. The materials lose the name tex<id> (that id belongs to the source stage; the prop's own
copy of each image takes its place). Only what the object uses is written.
"""
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ngpmesh  # noqa: E402

KINDS = ("prop", "destr")       # what is static scenery; pickups, vehicles and the like are not props


class OrigError(Exception):
    pass


def write_glb(path_or_none, j, binb):
    """The .glb bytes of JSON `j` and the buffer `binb` (written to path_or_none when it is given)."""
    js = json.dumps(j, separators=(",", ":")).encode()
    js += b" " * ((-len(js)) % 4)
    binb = bytes(binb) + b"\0" * ((-len(binb)) % 4)
    out = (struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binb)) + struct.pack("<II", len(js), 0x4E4F534A) + js +
           struct.pack("<II", len(binb), 0x004E4942) + binb)
    if path_or_none:
        with open(path_or_none, "wb") as f:
            f.write(out)
    return out


def _local(n):
    """The 4x4 (row lists) of a node's matrix, or of its translation, rotation (quaternion) and scale."""
    if "matrix" in n:
        m = n["matrix"]
        return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
    x, y, z, w = n.get("rotation", (0, 0, 0, 1))
    sx, sy, sz = n.get("scale", (1, 1, 1))
    tx, ty, tz = n.get("translation", (0, 0, 0))
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[r[0][0] * sx, r[0][1] * sy, r[0][2] * sz, tx], [r[1][0] * sx, r[1][1] * sy, r[1][2] * sz, ty],
            [r[2][0] * sx, r[2][1] * sy, r[2][2] * sz, tz], [0, 0, 0, 1]]


def _bounds0(out):
    """(min, max) in glTF axes of the meshes of state 0 of the extracted tree, from the accessors' min/max."""
    lo, hi = [1e30] * 3, [-1e30] * 3

    def walk(i, pm, state):
        n = out["nodes"][i]
        m = [[sum(pm[r][k] * l[k][c] for k in range(4)) for c in range(4)] for r in range(4) for l in (_local(n),)]
        if re.fullmatch(r"state[1-9]\d*", n.get("name") or ""):
            state = 1
        if "mesh" in n and not (n.get("name") or "").startswith(("state", "coll_", "hull_")) and state == 0:
            for p in out["meshes"][n["mesh"]]["primitives"]:
                a = out["accessors"][p["attributes"]["POSITION"]]
                for cx in (a["min"][0], a["max"][0]):
                    for cy in (a["min"][1], a["max"][1]):
                        for cz in (a["min"][2], a["max"][2]):
                            for r in range(3):
                                v = m[r][0] * cx + m[r][1] * cy + m[r][2] * cz + m[r][3]
                                lo[r], hi[r] = min(lo[r], v), max(hi[r], v)
        for k in n.get("children", []):
            walk(k, m, state)
    walk(0, [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]], 0)
    return lo, hi


def add_smoke(out):
    """A destructible building throws dust and smoke where it breaks (the stage's own emitters are not in the scene): four empties
    state<N>_smoke_a..d near the footprint's corners, low, sized to the building (lvlprop.emitter_bytes)."""
    states = sorted({int(m.group(1)) for n in out["nodes"] for m in [re.match(r"state([1-9]\d*)", n.get("name") or "")] if m})
    if not states:
        return
    lo, hi = _bounds0(out)
    if lo[0] > hi[0]:
        return
    span = max(hi[0] - lo[0], hi[2] - lo[2])
    size = round(max(40.0, min(110.0, span / 5)), 1)
    y = lo[1] + min((hi[1] - lo[1]) / 4, 60.0)
    for s in states:           # each break (a building has two) throws its own cloud, from the footprint's four corners (the middle is
        # inside the walls, out of sight)
        for k, (fx, fz) in enumerate(((0.15, 0.15), (0.85, 0.15), (0.15, 0.85), (0.85, 0.85))):
            out["nodes"].append({"name": "state%d_smoke_%s" % (s, "abcd"[k]),
                                 "translation": [lo[0] + (hi[0] - lo[0]) * fx, y, lo[2] + (hi[2] - lo[2]) * fz], "extras": {"size": size}})
            out["nodes"][0].setdefault("children", []).append(len(out["nodes"]) - 1)


def add_collision(out, binb, items):
    """The collision() items as meshes under the object's node (glTF axes: game x, z, -y)."""
    def data(raw, **acc):
        binb.extend(b"\0" * ((-len(binb)) % 4))
        out["bufferViews"].append({"buffer": 0, "byteOffset": len(binb), "byteLength": len(raw)})
        binb.extend(raw)
        out["accessors"].append(dict(bufferView=len(out["bufferViews"]) - 1, **acc))
        return len(out["accessors"]) - 1
    count = {}
    for state, kind, mat, verts, tris in items:
        g = [(x, z, -y) for x, y, z in verts]
        prim = {"attributes": {"POSITION": data(struct.pack("<%df" % (3 * len(g)), *[c for q in g for c in q]), componentType=5126,
                                                count=len(g), type="VEC3", min=[min(q[i] for q in g) for i in range(3)],
                                                max=[max(q[i] for q in g) for i in range(3)])}, "mode": 4}
        if tris:
            flat = [i for t in tris for i in t]
            prim["indices"] = data(struct.pack("<%dI" % len(flat), *flat), componentType=5125, count=len(flat), type="SCALAR")
        k = count[(state, kind, mat)] = count.get((state, kind, mat), -1) + 1
        name = "%s%s_m%x_%d" % ("state%d_" % state if state else "", kind, mat, k)
        out["meshes"].append({"name": name, "primitives": [prim]})
        out["nodes"].append({"name": name, "mesh": len(out["meshes"]) - 1})
        out["nodes"][0].setdefault("children", []).append(len(out["nodes"]) - 1)


def collision(st, handle):
    """The object's own collision, state by state, in its local axes (the handle's frame, game axes): [(state, kind, matId, verts,
    tris)] with kind "coll" (POLY_COLL: triangles; one entry per matId) or "hull" (COLL_VOL: its corners). `st` is a lvl.Stage."""
    import lvl
    img, out, seen = st.img, [], set()
    for p in st.grid.paths.values():
        if handle not in p.nodes or p.trigger:
            continue
        hi = p.nodes.index(handle)
        if any(st.op(n) in (17, 25) for n in p.nodes[hi + 1:]):      # an animated part under the object: no fixed place
            continue
        state = 0
        for i, n in enumerate(p.nodes[:-1]):
            if st.op(n) == 31:
                kids = lvl.children(img, n)
                state = kids.index(p.nodes[i + 1]) if p.nodes[i + 1] in kids else 0
        m = lvl.chain_matrix(img, p.nodes[hi + 1:-1], with_scale=False)
        key = (state, p.leaf, tuple(round(x, 3) for row in m for x in row))
        if key in seen:
            continue
        seen.add(key)
        leaf, op = p.leaf, img.u32(p.leaf) & 63
        if op == 10:
            pts = lvl.leaf_points(img, leaf)
            if pts:
                out.append((state, "hull", img.u32(leaf + 8) & 0xFFFF, [lvl.ngp.xform(m, q) for q in pts], []))
        elif op == 42:
            for k in range(min(img.u32(leaf + 4), 16)):
                o = img.u32(leaf + 8 + 4 * k) & 0x1FFFFFFF
                vp, npol, nbox = img.u32(o) & 0x1FFFFFFF, img.u16(o + 4), img.u16(o + 6)
                polys = o + 0x18 + 14 * nbox
                by_mat = {}
                for i in range(npol):
                    q = polys + 8 * i
                    by_mat.setdefault(img.u16(q + 4), []).append(tuple(img.u8(q + j) for j in range(3)))
                for mat, tris in sorted(by_mat.items()):
                    used = sorted({v for t in tris for v in t})
                    at = {v: n for n, v in enumerate(used)}
                    verts = [lvl.ngp.xform(m, tuple(img.f32(vp + 12 * v + 4 * c) for c in range(3))) for v in used]
                    out.append((state, "coll", mat, verts, [tuple(at[v] for v in t) for t in tris]))
    return out


def extract(scene, handle, stage=None):
    """The bytes of a .glb with the object `handle` ("0x1051BC0") of the stage scene: a path, or an ngpmesh.Glb already read (it is
    only read, so one can serve many objects). `stage` (a lvl.Stage of the same stage) adds the object's own collision as hull_ and
    coll_ meshes (named state<N>_coll_m<matId>_<k>: the matId is the surface word, so a wall stays a wall the monsters climb)."""
    g = scene if isinstance(scene, ngpmesh.Glb) else ngpmesh.Glb(scene)
    j = g.j
    handle = "0x%X" % int(str(handle), 16)
    top = next((i for i, n in enumerate(j["nodes"]) if (n.get("extras") or {}).get("handle", "").upper().replace("0X", "0x") == handle), None)
    if top is None:
        raise OrigError("The scene has no object %s" % handle)

    out = {"asset": {"version": "2.0", "generator": "WotM tools/lvlorig.py"}, "scene": 0, "scenes": [{"nodes": [0]}],
           "nodes": [], "meshes": [], "materials": [], "textures": [], "images": [], "accessors": [], "bufferViews": []}
    if (j.get("asset", {}).get("extras") or {}).get("gsRaw"):      # the textures' alpha is the GS's raw (lvlprop reads it so)
        out["asset"]["extras"] = {"gsRaw": True}
    if "samplers" in j:
        out["samplers"] = j["samplers"]
    binb = bytearray()
    maps = {k: {} for k in ("accessors", "materials", "textures", "images", "meshes", "views")}

    def view(vi):
        if vi in maps["views"]:
            return maps["views"][vi]
        v = dict(j["bufferViews"][vi])
        if v.get("buffer", 0) != 0:
            raise OrigError("bufferView %d is outside the .glb buffer" % vi)
        o = v.get("byteOffset", 0)
        data = g.bin[o:o + v["byteLength"]]
        binb.extend(b"\0" * ((-len(binb)) % 4))
        v["byteOffset"] = len(binb)
        binb.extend(data)
        out["bufferViews"].append(v)
        maps["views"][vi] = len(out["bufferViews"]) - 1
        return maps["views"][vi]

    def accessor(ai):
        if ai not in maps["accessors"]:
            a = dict(j["accessors"][ai])
            if "sparse" in a:
                raise OrigError("sparse accessor %d" % ai)
            if "bufferView" in a:
                a["bufferView"] = view(a["bufferView"])
            out["accessors"].append(a)
            maps["accessors"][ai] = len(out["accessors"]) - 1
        return maps["accessors"][ai]

    def image(ii):
        if ii not in maps["images"]:
            im = dict(j["images"][ii])
            if "bufferView" not in im:
                raise OrigError("image %d is a separate file" % ii)
            im["bufferView"] = view(im["bufferView"])
            out["images"].append(im)
            maps["images"][ii] = len(out["images"]) - 1
        return maps["images"][ii]

    def texture(ti):
        if ti not in maps["textures"]:
            t = dict(j["textures"][ti])
            if "source" in t:
                t["source"] = image(t["source"])
            out["textures"].append(t)
            maps["textures"][ti] = len(out["textures"]) - 1
        return maps["textures"][ti]

    def material(mi):
        if mi not in maps["materials"]:
            m = json.loads(json.dumps(j["materials"][mi]))
            for k in ("baseColorTexture",):
                t = m.get("pbrMetallicRoughness", {}).get(k)
                if t is not None:
                    t["index"] = texture(t["index"])
            m["name"] = "orig_" + (m.get("name") or "")      # tex<id> would name a texture of the open stage
            out["materials"].append(m)
            maps["materials"][mi] = len(out["materials"]) - 1
        return maps["materials"][mi]

    def mesh(mi):
        if mi not in maps["meshes"]:
            m = json.loads(json.dumps(j["meshes"][mi]))
            for p in m["primitives"]:
                p["attributes"] = {k: accessor(v) for k, v in p["attributes"].items()}
                if "indices" in p:
                    p["indices"] = accessor(p["indices"])
                if "material" in p:
                    p["material"] = material(p["material"])
            out["meshes"].append(m)
            maps["meshes"][mi] = len(out["meshes"]) - 1
        return maps["meshes"][mi]

    def node(i, state, first=False):
        n = json.loads(json.dumps(j["nodes"][i]))
        slot = len(out["nodes"])
        out["nodes"].append(n)
        if first:
            for k in ("matrix", "translation", "rotation", "scale"):
                n.pop(k, None)
            n["name"] = "object"
        sm = re.fullmatch(r"state(\d+)", n.get("name") or "")
        if sm:
            state = int(sm.group(1))
        n.pop("skin", None)
        if "mesh" in n:
            base = n.get("name") or j["meshes"][n["mesh"]].get("name") or "mesh%d" % i
            n["name"] = ("state%d_%s" % (state, base)) if state else base
            n["mesh"] = mesh(n["mesh"])
        if n.get("children"):
            n["children"] = [node(k, state) for k in n["children"]]
        return slot

    node(top, 0, True)
    add_smoke(out)
    if stage is not None:
        add_collision(out, binb, collision(stage, int(handle, 16)))
    for k in [k for k, v in out.items() if isinstance(v, list) and not v and k != "scenes"]:
        del out[k]
    out["buffers"] = [{"byteLength": len(binb) + (-len(binb)) % 4}]
    if "extensionsUsed" in j:
        out["extensionsUsed"] = j["extensionsUsed"]
    return write_glb(None, out, binb)


def prop_name(stage, sig):
    """A model name for the prop (1 to 8 letters, digits or _): three letters of the stage, _ and four hex digits of the signature."""
    return "%s_%s" % (re.sub(r"[^A-Z0-9]", "", stage.upper())[:3], (sig or "0000")[:4].upper())


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    try:
        data = extract(argv[0], argv[1])
    except (OrigError, ngpmesh.MeshError) as e:
        print("error:", e)
        return 1
    with open(argv[2], "wb") as f:
        f.write(data)
    print("%s: %d bytes" % (argv[2], len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
