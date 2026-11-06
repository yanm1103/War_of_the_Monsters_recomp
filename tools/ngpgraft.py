#!/usr/bin/env python3
"""Copies the parts another monster's special attack needs into a custom monster's own files.

  python tools/ngpgraft.py <files dir> <special> [--disc disc]     # e.g. mods/CJ/monsters/cj/files robo-47

The game registers some models of a special only from the .NGP of the monster that owns it (docs/FORMATS.md, "Level object
ids"): on another body they do not exist. This tool copies them, from the owner's disc file, into the custom's .NGP (the one
`files` holds: <PREFIX>0.NGP, with its .PTR, .TEX and .RTX, changed in place), under ids of their own that the game ignores
and the port registers for that monster only (native/runtime/wotm_custom.inc, "special"):
- a weapon model (AddWeaponEpNode, ids 4000..4042) gets id + 0x100;
- a SpecFxAnim model (0x11F9, 0x11FA, 0x11FD, 0x11FE, 0x11FF, 0x1201) gets id + 0x80, with its animations in the ANIM_MGR.
Each part is the closed range of the source image that holds its subtree (no pointer out of it), appended at the end of the
image with the same alignment; the parts hang under a new copy of root 1 (the attachments group) and the animated ones join
a new ANIM_MGR. Their textures go to the end of the .TEX with ids after the custom's, and their palettes to the end of the
.RTX. Running it again on files that already have the parts changes nothing.
Only the standard library; it reads the user's disc (nothing of the game goes to the repo).
"""
import bisect
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mechamon  # noqa: E402
import ngp  # noqa: E402
import tex  # noqa: E402

B = ngp.LEVEL_BASE
WEAPON_SHIFT, FX_SHIFT = 0x100, 0x80
FX_IDS = (0x11F9, 0x11FA, 0x11FD, 0x11FE, 0x11FF, 0x1201)
# special (monster.json "special") -> (source file stem, ids of the source's nodes to copy)
SPECIALS = {
    "robo-47": ("ROBOT0", (4005, 0x11FF)),   # the missile (weapon type 13) and the shock dome it opens on impact
}
# animation ids the special's state only adds on top of the pose (its AnimBlends): the rig writes them as layers even where
# the base has them as plain animations (StateRobotSpecial::init: 166 and 167 aim, 161 the shot)
LAYERS = {
    "robo-47": (161, 166, 167),
}


class GraftError(Exception):
    pass


def own_id(i):
    return i + (FX_SHIFT if i in FX_IDS else WEAPON_SHIFT)


def all_nodes(img):
    out = []
    for r in img.roots():
        out += mechamon.subtree(img, r & 0x0FFFFFFF)
    return out


def find_node(img, id1):
    for n in all_nodes(img):
        if img.u32(n) >> 18 == id1 and img.u32(n) & 63 == 1:   # the GROUP the db visits
            return n
    return None


def closed_range(img, slots, starts, node):
    """[lo, hi) holding the subtree of `node` and everything its pointers reach."""
    nodes = mechamon.subtree(img, node)

    def next_start(a):
        i = bisect.bisect_right(starts, a)
        return starts[i] if i < len(starts) else B + len(img.d)

    lo, hi = min(nodes), next_start(max(nodes))
    while True:
        changed = False
        for s in slots[bisect.bisect_left(slots, lo):bisect.bisect_left(slots, hi)]:
            t = img.u32(s) & 0x0FFFFFFF
            if t < lo:
                lo, changed = t, True
            elif t >= hi:
                hi, changed = next_start(t), True
        if not changed:
            return lo, hi, nodes


def graft(files, special, source, log=print):
    """Copies the parts of `special` into the files in `files`; `source(rel)` is the local path of a disc file ("MON/ROBOT0.NGP").
    False when they were already there."""
    if special not in SPECIALS:
        raise GraftError("no parts known for the special %r (only %s)" % (special, ", ".join(SPECIALS)))
    stem, ids = SPECIALS[special]
    src_path = {e: source("MON/%s%s" % (stem, e)) for e in (".NGP", ".PTR", ".TEX", ".RTX")}
    for e, p in src_path.items():
        if not p or not os.path.exists(p):
            raise GraftError("MON/%s%s is not on the disc" % (stem, e))
    ngps = [f for f in os.listdir(files) if f.upper().endswith("0.NGP")]
    if len(ngps) != 1:
        raise GraftError("%s: expected one <PREFIX>0.NGP, found %s" % (files, ngps or "none"))
    name = ngps[0][:-4]
    path = {e: os.path.join(files, name + e) for e in (".NGP", ".PTR", ".TEX", ".RTX")}
    for e, p in path.items():
        if not os.path.exists(p):
            raise GraftError("missing %s" % p)
    dst = ngp.Image.load(path[".NGP"])
    have = {dst.u32(n) >> 18 for n in all_nodes(dst)}
    todo = [i for i in ids if own_id(i) not in have]
    if not todo:
        log("%s: already has the parts of %s (%s)" % (name, special, ", ".join("%#x" % own_id(i) for i in ids)))
        return False

    src = ngp.Image.load(src_path[".NGP"])
    s0, s1, s2 = mechamon.read_ptr(src_path[".PTR"])
    d0, d1, d2 = mechamon.read_ptr(path[".PTR"])
    slots = sorted(B + x for x in s0)
    starts = sorted({src.u32(s) & 0x0FFFFFFF for s in slots} | {r & 0x0FFFFFFF for r in src.roots()})
    src_mgr = next(r for r in src.roots() if src.u32(r) & 63 == 7)
    src_cis = [src.u32(src_mgr + 8 + 4 * k) & 0x0FFFFFFF for k in range(src.u32(src_mgr + 4))]

    img = bytearray(dst.d)
    parts = []   # (old id, top node in the new image, char instances in the new image)
    texid_slots, tex0_slots, new_slots = [], [], []
    for old in todo:
        node = find_node(src, old)
        if node is None:
            raise GraftError("%s has no node %#x" % (stem, old))
        lo, hi, nodes = closed_range(src, slots, starts, node)
        inside = set(range(lo, hi))
        outside = [s for s in slots if not lo <= s < hi and lo <= (src.u32(s) & 0x0FFFFFFF) < hi]
        cis = [c for c in src_cis if lo <= c < hi]
        extra = [s for s in outside if src.u32(s) & 0x0FFFFFFF not in (node, *cis)]
        if extra:
            raise GraftError("%#x: the range %#x..%#x is reached from outside (%s)" % (
                old, lo, hi, ", ".join("%#x" % s for s in extra[:4])))
        del inside
        while (B + len(img) - lo) & 0x7F:
            img.append(0)
        nb = B + len(img)
        img += src.d[lo - B:hi - B]

        def new(a, lo=lo, hi=hi, nb=nb):
            assert lo <= a < hi, hex(a)
            return a - lo + nb

        for s in slots[bisect.bisect_left(slots, lo):bisect.bisect_left(slots, hi)]:
            v = src.u32(s)
            struct.pack_into("<I", img, new(s) - B, (v & 0xF0000000) | new(v & 0x0FFFFFFF))
            new_slots.append(new(s))
        texid_slots += [(B + x, new(B + x)) for x in s1 if lo <= B + x < hi]
        tex0_slots += [(B + x, new(B + x)) for x in s2 if lo <= B + x < hi]
        renamed = 0
        for n in nodes:
            w = src.u32(n)
            if w >> 18 == old:
                struct.pack_into("<I", img, new(n) - B, (w & 0x3FFFF) | (own_id(old) << 18))
                renamed += 1
        parts.append((old, new(node), [new(c) for c in cis]))
        log("%#x -> %#x: %s %#x..%#x (%d bytes) at %#x, %d nodes renamed, %d animated" % (
            old, own_id(old), stem, lo, hi, hi - lo, nb, renamed, len(cis)))

    # textures and palettes
    dtex_path = path[".TEX"]
    dtex = tex.parse(tex.read_file(dtex_path)[0])
    top = max(p.texId for p in dtex["packets"])
    olds = sorted({src.u16(s) for s, _ in texid_slots} - {0})
    new_ids = {o: top + 1 + i for i, o in enumerate(olds)}
    cbps = {tex.tex0_fields(struct.unpack_from("<Q", src.d, s - B)[0])["cbp"] for s, _ in tex0_slots}
    rtx, cbp_map = mechamon.build_rtx(path[".RTX"], src_path[".RTX"], cbps)
    texf = mechamon.build_tex(dtex_path, src_path[".TEX"], new_ids)
    for s, n in texid_slots:
        if src.u16(s):
            struct.pack_into("<H", img, n - B, new_ids[src.u16(s)])
    for s, n in tex0_slots:
        v = struct.unpack_from("<Q", src.d, s - B)[0]
        cbp = (v >> 37) & 0x3FFF
        struct.pack_into("<Q", img, n - B, (v & ~(0x3FFF << 37)) | (cbp_map.get(cbp, cbp) << 37))

    # root 1 (attachments group) and the ANIM_MGR: new copies with the parts added
    roots = dst.roots()
    ri = next(i for i, r in enumerate(roots) if dst.u32(r) & 63 == 1 and dst.u32(r) >> 18 == 0)
    mi = next(i for i, r in enumerate(roots) if dst.u32(r) & 63 == 7)
    g, mgr = roots[ri] & 0x0FFFFFFF, roots[mi] & 0x0FFFFFFF
    kids = [dst.u32(g + 0x20 + 4 * k) for k in range(dst.u16(g + 8))] + [top for _, top, _ in parts]
    mechamon.pad16(img)
    ng = B + len(img)
    img += dst.d[g - B:g - B + 0x20] + struct.pack("<%dI" % len(kids), *kids)
    struct.pack_into("<H", img, ng + 8 - B, len(kids))
    new_slots += [ng + 0x20 + 4 * k for k in range(len(kids))]
    cis = [dst.u32(mgr + 8 + 4 * k) for k in range(dst.u32(mgr + 4))] + [c for _, _, cs in parts for c in cs]
    mechamon.pad16(img)
    nm = B + len(img)
    img += struct.pack("<II", dst.u32(mgr), len(cis)) + struct.pack("<%dI" % len(cis), *cis)
    mechamon.pad16(img)
    new_slots += [nm + 8 + 4 * k for k in range(len(cis))]
    struct.pack_into("<I", img, 4 + 4 * ri, ng)
    struct.pack_into("<I", img, 4 + 4 * mi, nm)

    l0 = d0 + [s - B for s in new_slots]
    l1 = d1 + [n - B for _, n in texid_slots]
    l2 = d2 + [n - B for _, n in tex0_slots]
    ptr = bytearray()
    for lst in (l0, l1, l2):
        ptr += struct.pack("<I", len(lst)) + struct.pack("<%dI" % len(lst), *lst)
    ptr += b"\0" * 16
    if len(ptr) > 0x30D40:
        raise GraftError(".PTR of %d bytes (the game takes up to 200000)" % len(ptr))
    for e, data in ((".NGP", img), (".PTR", ptr), (".TEX", texf), (".RTX", rtx)):
        with open(path[e], "wb") as f:
            f.write(data)
    log("%s: %d bytes; textures %s, palettes %s" % (name + ".NGP", len(img), new_ids, cbp_map))
    return True


def main(argv):
    if len(argv) < 2 or argv[0].startswith("-"):
        print(__doc__)
        return 1
    disc = argv[argv.index("--disc") + 1] if "--disc" in argv else "disc"
    try:
        graft(argv[0], argv[1], lambda rel: os.path.join(disc, *rel.split("/")))
    except GraftError as e:
        print("ngpgraft:", e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
