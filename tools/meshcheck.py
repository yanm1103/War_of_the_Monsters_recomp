#!/usr/bin/env python3
"""Check of a .glb before the mod kit imports it as a monster's body ("Check model..." in the kit, `modkit.py ... mesh check`).

  python tools/meshcheck.py <base.NGP> <model.glb> [--mode auto|mesh|rig]

Prints a list of OK / WARNING / ERROR lines in the modder's terms, and which import takes the file:
- file: meshes, vertices, triangles, joints, actions;
- textures: per material its image (inside the .glb or next to it), format and size; an image that is missing or not PNG
  (JPEG is converted when Pillow is there) is drawn white;
- mesh: triangles only, vertices without weights, more than 3 bones per vertex (the heaviest 3 kept), no UV under an
  image (UVs outside 0..1 are fine: the game's object textures repeat);
- place: each skinned mesh against the rest positions of the bones that move it (size and middle); a mesh in other units
  or left away from the armature is an error;
- the import itself, without writing anything: `mesh` runs the Import mesh reader (tools/ngpmesh.py) on every `obj_<address>`
  mesh (piece names, the piece's bone palette, a moved skeleton); `rig` runs the Import rig (tools/ngprig.py) into a scratch
  folder and keeps its errors and warnings.
`auto` picks `mesh` when every skinned mesh is named after a piece of the base (`obj_<address>`), `rig` otherwise.
`scan()` (no base needed) is the part the imports print before they start. Only the standard library: it runs inside the mod kit.
"""
import math
import os
import re
import shutil
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ngpmesh  # noqa: E402

MAX_BONES = 3          # bones per vertex the skinning program reads (Scene::skinVertex)
BIG_TEX = 4096         # sides above this: a lot of video memory for one monster (and 8 can fight)
OBJ_NAME = re.compile(r"obj_([0-9a-f]+)$")


class Report:
    """The check's lines: (level, text), level in OK / WARNING / ERROR / "" (a plain line)."""

    def __init__(self, log=print):
        self.items = []
        self.log = log

    def add(self, level, text):
        self.items.append((level, text))
        self.log(("%s: %s" % (level, text)) if level else text)

    def ok(self, text):
        self.add("OK", text)

    def warn(self, text):
        self.add("WARNING", text)

    def err(self, text):
        self.add("ERROR", text)

    def info(self, text):
        self.add("", text)

    def count(self, level):
        return sum(1 for lv, _ in self.items if lv == level)


# ---------------------------------------------------------------- images

def _jpeg_size(b):
    o = 2
    while o + 9 < len(b):
        if b[o] != 0xFF:
            return None
        m = b[o + 1]
        if m in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            h, w = struct.unpack_from(">HH", b, o + 5)
            return w, h
        o += 2 + struct.unpack_from(">H", b, o + 2)[0]
    return None


def image_info(data):
    """(format, width, height) of image bytes; format PNG / JPEG / WEBP / ?; sizes None when unknown."""
    if data[:8] == b"\x89PNG\r\n\x1a\n" and len(data) >= 24:
        w, h = struct.unpack_from(">II", data, 16)
        return "PNG", w, h
    if data[:2] == b"\xff\xd8":
        wh = _jpeg_size(data)
        return ("JPEG",) + (wh or (None, None))
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return "WEBP", None, None
    return "?", None, None


def have_pillow():
    try:
        import PIL.Image  # noqa: F401
        return True
    except ImportError:
        return False


def image_bytes(glb, glb_path, k):
    """(bytes or None, name, where) of image k of the .glb; where says inside / next to the .glb / missing."""
    im = glb.j.get("images", [])[k]
    nm = im.get("name") or "image%d" % k
    if "bufferView" in im:
        v = glb.j["bufferViews"][im["bufferView"]]
        o = v.get("byteOffset", 0)
        return glb.bin[o:o + v["byteLength"]], nm, "inside the .glb"
    u = im.get("uri", "")
    if u.startswith("data:"):
        import base64
        return base64.b64decode(u.split(",", 1)[1]), nm, "inside the .glb"
    p = os.path.join(os.path.dirname(glb_path), u.replace("%20", " "))
    if u and os.path.exists(p):
        return open(p, "rb").read(), nm, "next to the .glb (%s)" % u
    return None, nm, "missing (%s not next to the .glb)" % (u or "no file")


# ---------------------------------------------------------------- the static scan

def _mesh_nodes(glb):
    return [(i, n) for i, n in enumerate(glb.nodes) if "mesh" in n]


def _material_image(glb, mi):
    mats = glb.j.get("materials", [])
    if mi is None or mi >= len(mats):
        return None
    t = mats[mi].get("pbrMetallicRoughness", {}).get("baseColorTexture")
    if t is None:
        return None
    return glb.j.get("textures", [])[t["index"]].get("source")


def load(glb_path, rep):
    try:
        return ngpmesh.Glb(glb_path)
    except (ngpmesh.MeshError, OSError, ValueError) as e:
        rep.err("cannot read %s: %s" % (os.path.basename(glb_path), e))
        return None


def scan(glb_path, rep, mode=None, glb=None):
    """What the .glb holds and the checks that need no base monster (mode "mesh": only the meshes Import mesh reads, the
    obj_<address> ones). Returns the Glb (None when it cannot be read)."""
    glb = glb or load(glb_path, rep)
    if glb is None:
        return None
    meshes = _mesh_nodes(glb)
    if not meshes:
        rep.err("no mesh in the .glb")
        return glb
    if mode == "mesh":
        meshes = [(i, n) for i, n in meshes if OBJ_NAME.match(n.get("name") or "")] or meshes
    nv = nt = 0
    used_mats = {}
    for _i, n in meshes:
        for prim in glb.j["meshes"][n["mesh"]]["primitives"]:
            at = prim.get("attributes", {})
            if "POSITION" in at:
                nv += glb.j["accessors"][at["POSITION"]]["count"]
            if "indices" in prim:
                nt += glb.j["accessors"][prim["indices"]]["count"] // 3
            elif "POSITION" in at:
                nt += glb.j["accessors"][at["POSITION"]]["count"] // 3
            used_mats.setdefault(prim.get("material"), []).append(n.get("name") or "mesh%d" % n["mesh"])
    skinned = [n for _i, n in meshes if "skin" in n]
    joints = set()
    for s in glb.j.get("skins", []):
        joints |= set(s.get("joints", []))
    acts = glb.j.get("animations", [])
    rep.info("%s: %d meshes (%d with a skin), %d vertices, %d triangles, %d joints, %d actions" % (
        os.path.basename(glb_path), len(meshes), len(skinned), nv, nt, len(joints), len(acts)))
    if not skinned:
        rep.err("no mesh has a skin: parent the meshes to the armature (Armature modifier) before exporting")
    _scan_textures(glb, glb_path, rep, used_mats, mode)
    parent = {c: p for p, nd in enumerate(glb.nodes) for c in nd.get("children", [])}
    allp, allj = [], []
    for i, n in meshes:
        if "skin" not in n and mode != "mesh":
            a = parent.get(i)
            while a is not None and a not in joints:
                a = parent.get(a)
            nm = n.get("name") or "mesh%d" % n["mesh"]
            if a is None:
                rep.warn("mesh %s has no skin and no bone above it: it moves in one piece with the root and does not bend "
                         "with the body (keep the Armature modifier, or parent it to a bone)" % nm)
            else:
                rep.info("mesh %s has no skin: it moves in one piece with %s" % (nm, glb.nodes[a].get("name") or "joint%d" % a))
        pts, js = _scan_mesh(glb, n, rep)
        allp += pts
        allj += js
    why = _fit(allp, allj)
    if why:
        rep.err("the model is not on its skeleton: %s (scale and move the mesh onto the armature, then apply the "
                "transform: Ctrl+A > All Transforms)" % why)
    elif allj:
        rep.ok("the mesh sits on its skeleton (size and place)")
    return glb


def _scan_textures(glb, glb_path, rep, used_mats, mode):
    mats = glb.j.get("materials", [])
    pillow = None
    total = 0
    seen = set()
    for mi in sorted(used_mats, key=lambda m: -1 if m is None else m):
        mname = mats[mi].get("name", "material%d" % mi) if mi is not None and mi < len(mats) else "(no material)"
        if mname in seen:
            continue
        seen.add(mname)
        tm = re.match(r"tex(\d+)$", mname or "")
        img = _material_image(glb, mi)
        if tm and mode == "mesh":
            rep.ok("material %s: the monster's texture %s" % (mname, tm.group(1)))
            continue
        if img is None:
            if tm:
                rep.ok("material %s: the base's texture %s" % (mname, tm.group(1)))
            else:
                rep.info("material %s has no image: drawn in its color" % mname)
            continue
        data, iname, where = image_bytes(glb, glb_path, img)
        if data is None:
            rep.warn("material %s: image %s is %s: drawn white (put the image next to the .glb, or pack it in Blender: "
                     "File > External Data > Pack Resources)" % (mname, iname, where))
            continue
        fmt, w, h = image_info(data)
        size = "%d x %d" % (w, h) if w else "size unknown"
        if fmt != "PNG":
            if pillow is None:
                pillow = have_pillow()
            if fmt == "JPEG" and pillow:
                rep.ok("material %s: image %s, JPEG %s, %s (turned into PNG on import)" % (mname, iname, size, where))
            else:
                rep.warn("material %s: image %s is %s, not PNG: drawn white (save it as PNG in Blender and export again)" % (
                    mname, iname, fmt))
                continue
        else:
            rep.ok("material %s: image %s, %s, %s" % (mname, iname, size, where))
        if w and h:
            total += w * h * 4 * 4 // 3
            if max(w, h) > BIG_TEX:
                rep.warn("image %s is %d x %d: %d MB of video memory for one monster (%d or less is plenty)" % (
                    iname, w, h, w * h * 4 * 4 // 3 >> 20, BIG_TEX))
        if mode == "mesh" and not tm:
            rep.warn("material %s is not one of the monster's textures (tex<id>): Import mesh draws it with the piece's own "
                     "texture; an image of its own goes in by Import character" % mname)
    if total:
        rep.info("textures: about %.1f MB of video memory" % (total / 1048576.0))


def _scan_mesh(glb, n, rep):
    name = n.get("name") or "mesh%d" % n["mesh"]
    skin = glb.j["skins"][n["skin"]] if "skin" in n else None
    ibm = None
    if skin is not None and "inverseBindMatrices" in skin:
        ibm = glb.accessor(skin["inverseBindMatrices"])
    many = noweight = nouv = total = 0
    heavy = {}                    # joint -> vertices it moves most
    pts = []
    for prim in glb.j["meshes"][n["mesh"]]["primitives"]:
        mode = prim.get("mode", 4)
        if mode != 4:
            rep.err("mesh %s: a part drawn as %s, not triangles (triangulate it: Ctrl+T in Blender)" % (
                name, {0: "points", 1: "lines", 2: "line loop", 3: "line strip", 5: "triangle strip", 6: "triangle fan"}.get(mode, mode)))
            continue
        at = prim.get("attributes", {})
        if "POSITION" not in at:
            continue
        try:
            pos = glb.accessor(at["POSITION"])
            sets = [(glb.accessor(at["JOINTS_%d" % s]), glb.accessor(at["WEIGHTS_%d" % s]))
                    for s in range(4) if "JOINTS_%d" % s in at and "WEIGHTS_%d" % s in at] if skin else []
        except ngpmesh.MeshError as e:
            rep.err("mesh %s: %s" % (name, e))
            continue
        total += len(pos)
        pts += pos
        if "TEXCOORD_0" not in at and _material_image(glb, prim.get("material")) is not None:
            nouv += len(pos)
        if skin is None:
            continue
        if not sets:
            rep.err("mesh %s has a skin but no weights (JOINTS_0/WEIGHTS_0)" % name)
            continue
        for v in range(len(pos)):
            ws = [(j, w) for js, wts in sets for j, w in zip(js[v], wts[v]) if w > 0.0]
            if not ws:
                noweight += 1
                continue
            if len(ws) > MAX_BONES:
                many += 1
            j = max(ws, key=lambda jw: jw[1])[0]
            heavy[j] = heavy.get(j, 0) + 1
    if not total:
        return [], []
    if nouv:
        rep.warn("mesh %s: %d vertices without UV: its image cannot be placed there (unwrap it in Blender)" % (name, nouv))
    if skin is None:
        return [], []
    if noweight:
        rep.err("mesh %s: %d of %d vertices have no weight on any bone (they would not follow the body; in Blender: "
                "Weight Paint, or select them and Ctrl+G > Assign to a bone's group)" % (name, noweight, total))
    if many:
        rep.warn("mesh %s: %d vertices on more than %d bones: the heaviest %d are kept (Blender: Weights > Limit Total, 3)" % (
            name, many, MAX_BONES, MAX_BONES))
    if not noweight:
        rep.ok("mesh %s: %d vertices, all with weights" % (name, total))
    js = []
    for j, c in heavy.items() if ibm is not None else ():
        if j < len(ibm) and c >= max(4, total // 100):
            t = ngpmesh._inv_affine(ngpmesh._col(ibm[j]))
            js.append((t[0][3], t[1][3], t[2][3]))
    return pts, js


def _fit(pts, js):
    """Why the skinned meshes are not where their bones are (None when they are): their size against the span of the bones
    that move them, and the middle of those bones inside them. The bones' rest positions are the inverses of the bind
    matrices, in the meshes' own space; only bones that move a share of a mesh's vertices count (helper joints of the base
    sit far away)."""
    if not pts or not js:
        return None
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    size = max(hi[k] - lo[k] for k in range(3))
    jlo = [min(p[k] for p in js) for k in range(3)]
    jhi = [max(p[k] for p in js) for k in range(3)]
    span = max(jhi[k] - jlo[k] for k in range(3))
    if len(js) >= 3 and span > 0 and size > 0:
        r = size / span
        if r < 0.4:
            return "it is %.2g times the size of the bones that move it" % r
        if r > 5.0:
            return "it is %.0f times the size of the bones that move it" % r
    mid = [sum(p[k] for p in js) / len(js) for k in range(3)]
    pad = 0.25 * size
    if any(mid[k] < lo[k] - pad or mid[k] > hi[k] + pad for k in range(3)):
        return "the bones that move it are outside it (%.0f units from its middle)" % math.dist(
            mid, [(lo[k] + hi[k]) / 2 for k in range(3)])
    return None


# ---------------------------------------------------------------- the import, without writing

def pick_mode(glb, base_path):
    """mesh when every skinned mesh is named after a skinned piece of the base, rig otherwise."""
    names = [n.get("name") or "" for _i, n in _mesh_nodes(glb) if "skin" in n]
    if not names:
        return "rig"
    try:
        pieces = {"obj_%x" % o for o in ngpmesh.Base(base_path).skinned}
    except (ngpmesh.MeshError, OSError):
        return "rig"
    return "mesh" if all(nm in pieces for nm in names) else "rig"


def route(base_path, glb_path):
    """("mesh", None) when Import mesh takes the .glb, ("rig", why) when it is a new body that only Import rig takes: a skinned
    mesh not named after a piece of the base, or a piece weighted to bones outside its palette. Other faults are left to the
    import, which names them."""
    try:
        glb = ngpmesh.Glb(glb_path)
        base = ngpmesh.Base(base_path)
    except (ngpmesh.MeshError, OSError, ValueError):
        return "mesh", None
    pieces = {"obj_%x" % o: o for o in base.skinned}
    names = [n.get("name") or "" for _i, n in _mesh_nodes(glb) if "skin" in n]
    new = [nm or "(no name)" for nm in names if nm not in pieces]
    if new:
        return "rig", "%s %s not among %s's pieces (%s)" % (", ".join(new), "is" if len(new) == 1 else "are",
                                                            os.path.basename(base_path), ", ".join(sorted(pieces)))
    for nm in names:
        warns = []
        try:
            ngpmesh.read_mesh(glb, nm, base, pieces[nm], warns.append)
        except ngpmesh.MeshError as e:
            warns.append(str(e))
        if any("palette" in w for w in warns):
            return "rig", "%s has weights on bones that piece does not follow" % nm
    return "mesh", None


def check_mesh(glb, base_path, rep):
    """The Import mesh reader on every obj_<address> mesh; the other meshes are a new body (Import rig)."""
    try:
        base = ngpmesh.Base(base_path)
    except (ngpmesh.MeshError, OSError) as e:
        rep.err("base %s: %s" % (os.path.basename(base_path), e))
        return
    pieces = {"obj_%x" % o: o for o in base.skinned}
    seen = 0
    for _i, n in _mesh_nodes(glb):
        nm = n.get("name") or ""
        if nm not in pieces:
            if "skin" in n:
                rep.warn("mesh %s is not one of the monster's pieces (%s): Import mesh leaves it out (a new body goes in by "
                         "Import character)" % (nm or "(no name)", ", ".join(sorted(pieces))))
            continue
        seen += 1
        warns = []
        try:
            by_slot = ngpmesh.read_mesh(glb, nm, base, pieces[nm], warns.append)
        except ngpmesh.MeshError as e:
            rep.err("Import mesh: %s" % _plain(str(e)))
            continue
        for w in warns:
            rep.warn("Import mesh: %s" % w)
        rep.ok("Import mesh: piece %s, %d triangles" % (nm, sum(len(t) for t in by_slot.values())))
    if not seen:
        rep.err("no mesh is named after a piece of the monster (%s): Import mesh has nothing to replace" % ", ".join(sorted(pieces)))


def _plain(msg):
    if "has no weight on a bone of the palette" in msg:
        return msg + " (each piece follows only its own bones: a new body, or weights on other bones, go in by Import character)"
    return msg


def check_rig(glb_path, base_path, rep, front=None, scale=None, height=None, layer_ids=()):
    """The Import rig into a scratch folder: its errors and warnings, and what it would make."""
    import ngpanim
    import ngprig
    tmp = tempfile.mkdtemp(prefix="wotm_check_")
    lines = []
    try:
        try:
            ngprig.build(base_path, glb_path, tmp, front=front, scale=scale, height=height, layer_ids=layer_ids, log=lines.append)
            failed = None
        except (ngprig.RigError, ngpanim.AnimError, ngpmesh.MeshError) as e:
            failed = str(e)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for ln in lines:
        if ln.startswith("warning: "):
            rep.warn("Import character: %s" % ln[len("warning: "):])
        elif ln.startswith("wrote ") and ".RTX: " in ln:
            rep.info("Import character would make: %s" % ln.split(".RTX: ", 1)[1])
        elif ln.startswith(("rig ", "skeleton:")):
            rep.info("Import character: %s" % ln)
    if failed:
        rep.err("Import character: %s" % failed)
    else:
        rep.ok("Import character takes it")


def check(base_path, glb_path, mode="auto", log=print, **rig_opts):
    """The whole check: scan, then the import that takes the file (mode auto / mesh / rig). Returns the Report."""
    rep = Report(log)
    glb = load(glb_path, rep)
    if glb is None:
        return rep
    if mode == "auto":
        mode = pick_mode(glb, base_path)
        rep.info("the import that takes it: %s" % ("Import mesh (every mesh with a skin is a piece of the monster)" if mode == "mesh" else
                                    "Import character (a new body, on the monster's skeleton or on one of its own)"))
    scan(glb_path, rep, mode, glb)
    if not _mesh_nodes(glb):
        return rep
    if mode == "mesh":
        check_mesh(glb, base_path, rep)
    else:
        check_rig(glb_path, base_path, rep, **rig_opts)
    ne, nw = rep.count("ERROR"), rep.count("WARNING")
    rep.info("check: %s" % ("no problems" if not ne and not nw else "%d errors, %d warnings" % (ne, nw)))
    return rep


def main(argv):
    args = [a for a in argv if not a.startswith("--mode")]
    mode = next((a.split("=", 1)[1] for a in argv if a.startswith("--mode=")), "auto")
    if "--mode" in argv:
        i = argv.index("--mode")
        mode = argv[i + 1] if i + 1 < len(argv) else ""
        args = [a for k, a in enumerate(argv) if k not in (i, i + 1)]
    if len(args) != 2 or mode not in ("auto", "mesh", "rig"):
        print(__doc__.split("\n\n")[1])
        return 2
    rep = check(args[0], args[1], mode)
    return 1 if rep.count("ERROR") else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
