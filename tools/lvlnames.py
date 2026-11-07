#!/usr/bin/env python3
"""Readable names for the stage objects of tools/lvl.py (the level editor's list), from nothing the game stores as a name: the
disc has no object names, so a name comes from, in order, the patch (one object), the user's names file and the repo's
tools/level_editor/names.json (one model, by content signature), and rules (a stage's special ids, the pickup type, the
destructible's hit point class, a prop's size).

  python tools/lvlnames.py <STAGE> [--disc <folder>]     # every object: signature, rule name, size

Signature (version SIG_VERSION): the vertices of every OBJECT under the object (state 0, most detailed LOD) in the handle's frame,
rounded to 0.1, plus the md5 of the decoded pixels of each texture they use. Moving or copying an object keeps it; the same
model in another stage has the same signature (texIds differ between stages, the pixels do not).
"""
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lvl   # noqa: E402
import ngp   # noqa: E402
import tex   # noqa: E402

SIG_VERSION = 1
TRAFFIC_TYPES = (2, 3, 15, 26)   # pickup types that drive in the traffic when id2 bit 0 is set (CarPickup)


class Signer:
    def __init__(self, st, root):
        self.img = st.img
        base = os.path.join(root, "LVL", st.name)
        self.lib = tex.Library(base + ".TEX", base + ".RTX", base + ".NGP")
        self.mesh = {}
        self.texh = {}

    def tex_hash(self, tid, tex0):
        k = (tid, tex0)
        if k not in self.texh:
            im = self.lib.image(tid, tex0)
            if im is None:
                self.texh[k] = "none%d" % tid
            else:
                w, h, px = im
                self.texh[k] = hashlib.md5(bytes([w & 255, h & 255]) + bytes(c for p in px for c in p)).hexdigest()[:10]
        return self.texh[k]

    def obj(self, node):
        if node not in self.mesh:
            img = self.img
            md = ngp.decode_mesh(img, node)
            verts = [(v.x, v.y, v.z) for v in md.tris]
            texs = set()
            nslots = min(img.u16(node + 0x5A), 5)
            for v in md.tris[::3]:
                s = v.slot
                if s == ngp.SLOT_NOTEX:
                    continue
                s = s if s < nslots else 0
                tid = img.u16(node + 0x50 + 2 * s)
                tex0 = img.u32(node + 0x70 + 0x30 * s) | (img.u32(node + 0x74 + 0x30 * s) << 32)
                texs.add(self.tex_hash(tid, tex0))
            self.mesh[node] = (verts, texs)
        return self.mesh[node]

    def of(self, starts):
        """(signature or None, [dx, dy, dz])"""
        pts, texs = [], set()

        def visit(node, m):
            v, t = self.obj(node)
            texs.update(t)
            for p in v:
                q = ngp.xform(m, p)
                pts.append((round(q[0], 1), round(q[1], 1), round(q[2], 1)))

        for s in starts:
            ngp.walk(self.img, s, visit, lod=99)
        if not pts:
            return None, [0, 0, 0]
        g = hashlib.sha1(repr(sorted(pts)).encode()).hexdigest()[:12]
        t = hashlib.sha1(repr(sorted(texs)).encode()).hexdigest()[:8]
        return g + t, [round(max(p[i] for p in pts) - min(p[i] for p in pts), 1) for i in range(3)]


def starts_of(img, o):
    """Where the signature walk starts: a prop's children (its handle's place stays out), a destructible's INTERACTIVE_STATE,
    a pickup's GROUP (or its children when the GROUP's visual bit 0 is off)."""
    n = o["node"]
    if o["kind"] == "prop":
        return lvl.children(img, n)
    if o["kind"] == "pickup" and not (img.u16(n + 0xA) & 1):
        return lvl.children(img, n)
    return [n]


def facts(st, objs, root):
    """Per object (same order as objs): sig, dims and what the rules look at (pickup type and traffic bit, hit point class,
    the INTERACTIVE_STATE id)."""
    img = st.img
    sg = Signer(st, root)
    out = []
    for o in objs:
        n = o["node"]
        sig, dims = sg.of(starts_of(img, o))
        f = {"sig": sig, "dims": dims}
        if o["kind"] == "pickup":
            f["ptype"] = o["id"] - 0x1388
            f["sid"] = o["id"]
            f["traffic"] = bool((img.u32(n) >> 7) & 1)
        elif o["kind"] == "destr":
            d = img.u32(n + 0xC) & lvl.MASK
            f["hpclass"] = img.u16(d + 4)
            f["sid"] = img.u32(n) >> 18
        out.append(f)
    return out


def load_names(*paths):
    """The names files merged, later ones over earlier ones (the repo's first, then the user's)."""
    names = {"version": 1, "sig": SIG_VERSION, "pickup_types": {}, "special": {}, "hp_classes": {}, "models": {}}
    for p in paths:
        try:
            with open(p, encoding="utf-8") as f:
                d = json.load(f)
        except (OSError, ValueError):
            continue
        if d.get("sig", SIG_VERSION) != SIG_VERSION:
            d = dict(d, models={})   # models signed by another version do not match these
        for k in ("pickup_types", "special", "hp_classes", "models"):
            names[k].update(d.get(k) or {})
    return names


def rule_name(stage, kind, f, names):
    """The name the rules give (no model or instance name)."""
    s = names["special"].get("%s:0x%X" % (stage, f.get("sid", 0)))
    if s:
        return s
    if kind == "destr":
        return names["hp_classes"].get(str(f.get("hpclass")), "Destructible")
    if kind == "pickup":
        t = f.get("ptype", 0)
        n = names["pickup_types"].get(str(t), "Pickup (type %d)" % t)
        if t in TRAFFIC_TYPES:
            n += " (traffic)" if f.get("traffic") else " (parked)"
        return n
    return "Terrain piece" if max(f["dims"][:2]) > 500 else "Prop"


def name_of(stage, kind, f, names):
    """(name, source): source 'model' (a names file) or 'rule'. The instance name (patch) is the editor's."""
    m = names["models"].get(f.get("sig") or "")
    if m and m.get("name"):
        return m["name"], "model"
    return rule_name(stage, kind, f, names), "rule"


def main(argv):
    disc = argv[argv.index("--disc") + 1] if "--disc" in argv else os.path.join(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))), "disc")
    name = [a for a in argv if not a.startswith("--") and a != disc][0].upper()
    st = lvl.Stage(name, disc)
    objs = st.objects()
    names = load_names(os.path.join(os.path.dirname(os.path.abspath(__file__)), "level_editor", "names.json"))
    for o, f in zip(objs, facts(st, objs, disc)):
        n, src = name_of(name, o["kind"], f, names)
        print("0x%-8X %-6s %-22s %-5s %s %s" % (o["handle"] or o["node"], o["kind"], n, src, f["sig"] or "-",
                                               "x".join("%g" % x for x in f["dims"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
