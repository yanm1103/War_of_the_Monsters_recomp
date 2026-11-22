#!/usr/bin/env python3
"""Readable names for the bones of the mod kit's Blender export (`monglb.py --edit`): `L_Hand_bone1`, `Spine1_bone11`,
`Tail3_bone24`. The `bone<N>` part stays at the end of the name, so the imports (`ngpmesh.py`, `ngpanim.py`) and the bone maps
of `retarget.py` find the bone by it, with or without the label (`canonical()`).

  python tools/bonelabels.py <export.glb>          # prints the labels the export would give

The labels come from the skeleton and the locators, the same for every monster:
- arms and legs: the bones from the hand (locator 0x802 left, 0x803 right) or foot (0x804, 0x805) up to where the two sides
  meet; from that point down `Clavicle`, `Shoulder`, `UpperArm`, `Forearm`, `Hand` (`Hip`, `Leg`, `Thigh`, `Shin`, `Foot`),
  the first ones only when the chain is long enough, with `L_` / `R_`;
- where the arms meet is `Chest`, where the legs meet `Pelvis`, the top bone `Root`; `Spine1..` between the root and the
  chest; the head locator's bone (0x80C) is `Head`, `Neck1..` up to the chest, and the mouth locator's bone below the head
  (0x80B) is `Jaw`;
- a chain of two or more bones off the root, pelvis or spine that ends behind them (the game's front is +Y) is `Tail1..`
  (a second one `TailB1..`).
Bones none of this reaches (fingers, wings, extra parts) keep `bone<N>`. Standard library only.
"""
import json
import math
import re
import struct
import sys

BONE = re.compile(r"(?:^|_)(bone(?:[0-9a-f]+_)?\d+(?:@[0-9a-f]+)?)$")
LOC = re.compile(r"loc_([0-9a-f]+)$")
ARM = ["Clavicle", "Shoulder", "UpperArm", "Forearm", "Hand"]
LEG = ["Hip", "Leg", "Thigh", "Shin", "Foot"]


def canonical(name):
    """`L_Hand_bone12` -> `bone12` (also `bonea0_12`, `bone12@a0f`); any other name as it is."""
    m = BONE.search(name or "")
    return m.group(1) if m else name


def _chain_names(n, names):
    """Labels of a limb of n bones, from the body out: the last three always (hand side), the first one from 4 bones, the
    second from 5, then numbered."""
    first, second, last = names[0], names[1], names[2:]
    if n <= 3:
        return last[3 - n:]
    if n == 4:
        return [first] + last
    mid = [second] + ["%s%d" % (second, k) for k in range(2, n - 3)]
    return [first] + mid + last


def _mat(nd):
    if "matrix" in nd:
        m = nd["matrix"]
        return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
    t = nd.get("translation", [0.0, 0.0, 0.0])
    x, y, z, w = nd.get("rotation", [0.0, 0.0, 0.0, 1.0])
    s = nd.get("scale", [1.0, 1.0, 1.0])
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0.0, 0.0, 0.0, 1.0]]


def _mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def labels(nodes, root=None):
    """{node index: label} for the `bone<N>` nodes of a kit export (nodes: the glTF node list). root: the "WotM" node (its
    children are in the game's axes); found by name when None."""
    parent = {c: p for p, nd in enumerate(nodes) for c in nd.get("children", [])}
    if root is None:
        root = next((i for i, nd in enumerate(nodes) if nd.get("name") == "WotM"), None)
    bones = [i for i, nd in enumerate(nodes) if BONE.fullmatch(nd.get("name") or "") and "@" not in nd["name"]]
    bset = set(bones)

    def bone_above(i):
        p = parent.get(i)
        while p is not None and p not in bset:
            p = parent.get(p)
        return p

    bpar = {b: bone_above(b) for b in bones}
    kids = {}
    for b, p in bpar.items():
        kids.setdefault(p, []).append(b)

    world = {}

    def wpos(i):
        if i not in world:
            p = parent.get(i)
            M = _mat(nodes[i])
            world[i] = M if p is None or p == root else _mul(wpos(p), M)
        return world[i]

    def pos(i):
        m = wpos(i)
        return (m[0][3], m[1][3], m[2][3])

    def count(b):
        return 1 + sum(count(c) for c in kids.get(b, []))

    tops = kids.get(None, [])
    if not tops:
        return {}
    top = max(tops, key=count)
    loc = {}
    for i, nd in enumerate(nodes):
        m = LOC.fullmatch(nd.get("name") or "")
        if m:
            h = bone_above(i)
            if h is not None:
                loc.setdefault(int(m.group(1), 16), h)

    def up(b):
        out = []
        while b is not None:
            out.append(b)
            b = bpar.get(b)
        return out

    lab = {top: "Root"}

    def limb(lid_l, lid_r, names, meet_name):
        if lid_l not in loc or lid_r not in loc:
            return None
        pl, pr = up(loc[lid_l]), up(loc[lid_r])
        meet = next((b for b in pl if b in set(pr)), None)
        if meet is None:
            return None
        for side, path in (("L", pl), ("R", pr)):
            chain = list(reversed(path[:path.index(meet)]))
            if any(b in lab for b in chain):
                continue
            for b, nm in zip(chain, _chain_names(len(chain), names)):
                lab[b] = "%s_%s" % (side, nm)
        if meet not in lab:
            lab[meet] = meet_name
        return meet

    chest = limb(0x802, 0x803, ARM, "Chest")
    pelvis = limb(0x804, 0x805, LEG, "Pelvis")
    if chest is not None:
        between = up(chest)[1:]
        between = list(reversed(between[:between.index(top)])) if top in between else []
        k = 0
        for b in between:
            if b not in lab:
                k += 1
                lab[b] = "Spine%d" % k
    head = loc.get(0x80C)
    if head is not None and head not in lab:
        lab[head] = "Head"
        path = up(head)[1:]
        stop = chest if chest in path else top
        if stop in path:
            neck = list(reversed(path[:path.index(stop)]))
            k = 0
            for b in neck:
                if b not in lab:
                    k += 1
                    lab[b] = "Neck%d" % k
        jaw = loc.get(0x80B)
        if jaw is not None and jaw not in lab and head in up(jaw)[1:]:
            lab[jaw] = "Jaw"
    # tails: chains of two or more unnamed bones off the root, pelvis or spine that end behind where they start
    trunk = [b for b, nm in lab.items() if nm in ("Root", "Pelvis") or nm.startswith("Spine")]
    tails = 0
    for t in sorted(trunk, key=lambda b: len(up(b))):
        for c in kids.get(t, []):
            if c in lab:
                continue
            chain = [c]
            while True:
                nxt = [x for x in kids.get(chain[-1], []) if x not in lab]
                if not nxt:
                    break
                chain.append(max(nxt, key=count))
            if len(chain) < 2 or pos(chain[-1])[1] >= pos(t)[1]:
                continue
            name = "Tail" if not tails else "Tail" + chr(ord("A") + tails)   # Tail1.., TailB1.., TailC1..
            tails += 1
            for k, b in enumerate(chain, 1):
                lab[b] = "%s%d" % (name, k)
    return lab


def apply(nodes, root=None):
    """Renames the kit export's bones to `<label>_bone<N>` (their copies `bone<N>@<obj>` too). Returns the labels."""
    lab = labels(nodes, root)
    by_bone = {nodes[i]["name"]: nm for i, nm in lab.items()}
    for nd in nodes:
        nm = nd.get("name") or ""
        if BONE.fullmatch(nm):
            base = nm.split("@", 1)[0]
            if base in by_bone:
                nd["name"] = "%s_%s" % (by_bone[base], nm)
    return lab


def main(argv):
    if len(argv) != 1:
        print(__doc__.split("\n\n")[1])
        return 2
    d = open(argv[0], "rb").read()
    j = json.loads(d[20:20 + struct.unpack_from("<I", d, 12)[0]])
    nodes = j["nodes"]
    lab = labels(nodes)
    for i in sorted(lab, key=lambda i: int(re.search(r"(\d+)$", nodes[i]["name"].split("@")[0]).group(1))):
        print("%-10s %s" % (canonical(nodes[i]["name"]), lab[i]))
    left = [canonical(nd["name"]) for i, nd in enumerate(nodes)
            if BONE.fullmatch(nd.get("name") or "") and "@" not in nd["name"] and i not in lab]
    if left:
        print("unnamed: %s" % ", ".join(left))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
