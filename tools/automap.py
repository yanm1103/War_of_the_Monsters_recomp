#!/usr/bin/env python3
"""A bone map for `retarget.py` made from the two skeletons: the model's joints by side, kind and shape, the base's bones by
their labels (`bonelabels.py`).

  python tools/automap.py <source.glb> <model.dae|.glb|.gltf> <map.json> [--up y|z]

The model's side comes from its joint names (`L_`, `_l`, `.L`, `Left`, `Bip01 L`), the hands and feet from names like hand,
wrist, foot, ankle (fingers and toes left out), the head, jaw and tail from those words. The rest is the shape of the tree:
- an arm is the path from the hand up to where the two arms meet (the chest); its upper arm and forearm are the two longest
  consecutive segments of that path, the hand the joint at the end of the forearm, the clavicle the one before the upper
  arm; a leg the same from the foot (thigh, shin, foot); where the legs meet is the pelvis;
- the joint where pelvis and chest meet is the trunk's root (it carries the move); the spine between it and the chest and
  the neck between the chest and the head follow the base's spine and neck along their length (`chains`), or a blend of the
  bones at their ends when the base has none;
- upper arms, forearms, thighs and shins get `align` (the model is posed like the base before the motion: a T-pose on
  lowered arms), feet get `level`; a tail chain follows the base's tail when both have one.
What it does not reach (fingers, toes, extra parts) rides on the joint above. The map is plain JSON: edit it and run the
retarget again. Standard library only.
"""
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bonelabels  # noqa: E402
import ngpanim  # noqa: E402

FINGER = re.compile(r"finger|thumb|index|middle|ring|pinky|pinkie|digit|toe|nail|claw|_end$|end$|nub|top$|tip", re.I)


class MapError(Exception):
    pass


def side(name):
    """"L", "R" or None from a joint name."""
    s = re.sub(r"^(mixamorig\d*:|bip0*1[ _]?|def-|org-|mch-)", "", name or "", flags=re.I)
    toks = [t.lower() for t in re.split(r"[_ .\-:|]+", s) if t]
    if "l" in toks or "left" in toks:
        return "L"
    if "r" in toks or "right" in toks:
        return "R"
    if re.search(r"left", s, re.I):      # LeftArm (Mixamo), upperarm_left
        return "L"
    if re.search(r"right", s, re.I):
        return "R"
    m = re.match(r"([LR])[A-Z]", s)      # LArm, RFoot
    return m.group(1) if m else None


def _has(name, *words):
    n = (name or "").lower()
    return any(w in n for w in words)


def _pos(m):
    return (m[0][3], m[1][3], m[2][3])


ARM_WORDS = (("clavicle", r"clav|collar"), ("upperarm", r"upper_?arm|uparm|^arm$"), ("forearm", r"fore_?arm|lower_?arm|elbow"),
             ("hand", r"hand|wrist"))
LEG_WORDS = (("hip", r"^hip$"), ("thigh", r"thigh|up_?leg|upper_?leg"), ("shin", r"calf|shin|lower_?leg|knee|^leg$"),
             ("foot", r"foot|ankle"))


def part_of(name, names):
    """The limb part a joint name says ("upperarm", "shin"...), or None. The side and the rig's prefix are taken off first,
    so Mixamo's LeftArm / LeftForeArm / LeftUpLeg / LeftLeg read as arm / forearm / upleg / leg; "shoulder" is the clavicle
    when the chain also has an arm."""
    s = re.sub(r"^(mixamorig\d*:|bip0*1[ _]?|def-|org-|mch-)", "", name or "", flags=re.I)
    s = re.sub(r"(^|[_ .\-])(l|r|left|right)(?=$|[_ .\-])|^(left|right)|(left|right)$", "", s, flags=re.I)
    s = re.sub(r"[_ .\-]?(jnt|joint|bone|bn|\d+)$", "", s.strip("_ .-"), flags=re.I).lower().replace(" ", "_")
    if FINGER.search(s):
        return None
    table = ARM_WORDS if names[3] == "hand" else LEG_WORDS
    for part, rx in table:
        if re.search(rx, s):
            return part
    if names[3] == "hand" and "shoulder" in s:
        return "clavicle"
    return None


def model_roles(mdl):
    """{role: joint index} of the model, roles: root, pelvis, chest, head, jaw, L_/R_ clavicle, upperarm, forearm, hand,
    thigh, shin, foot; plus "spine" and "neck" (lists, from the root/chest side) and "tail" (list). Raises MapError when
    the arms or legs are not found."""
    J = mdl.joints
    kids = {}
    for i, j in enumerate(J):
        kids.setdefault(j["parent"], []).append(i)

    def up(i):
        out = []
        while i is not None:
            out.append(i)
            i = J[i]["parent"]
        return out

    def depth(i):
        return len(up(i))

    def end(kind, sd):
        words = ("hand", "wrist", "palm") if kind == "arm" else ("foot", "ankle")
        loose = ("arm", "hand", "wrist", "elbow") if kind == "arm" else ("leg", "foot", "ankle", "knee", "calf", "shin", "thigh")
        cand = [i for i, j in enumerate(J) if side(j["name"]) == sd and _has(j["name"], *words) and not FINGER.search(j["name"])]
        if not cand:
            cand = [i for i, j in enumerate(J) if side(j["name"]) == sd and _has(j["name"], *loose) and not FINGER.search(j["name"])]
        if not cand:
            return None
        # the one closest to the body among those that end the limb (a "hand" joint with a "hand_end" below: the first)
        return min(cand, key=depth) if kind == "arm" and any(_has(J[c]["name"], *words) for c in cand) else max(cand, key=depth)

    roles = {}
    ends = {(k, s): end(k, s) for k in ("arm", "leg") for s in ("L", "R")}
    miss = [("left" if s == "L" else "right") + " " + ("hand" if k == "arm" else "foot") for (k, s), v in ends.items() if v is None]
    if miss:
        raise MapError("no joint found for: %s (name them with hand/foot and a side, L_ or _l)" % ", ".join(miss))

    def meet(a, b):
        pa = up(a)
        sb = set(up(b))
        return next((x for x in pa if x in sb), None)

    def real(i):   # a helper joint on top of the one above it stands for that one
        while i is not None and J[i]["parent"] is not None and \
                math.dist(_pos(J[i]["world"]), _pos(J[J[i]["parent"]]["world"])) <= 1e-4:
            i = J[i]["parent"]
        return i

    chest = real(meet(ends[("arm", "L")], ends[("arm", "R")]))
    pelvis = real(meet(ends[("leg", "L")], ends[("leg", "R")]))
    if chest is None or pelvis is None:
        raise MapError("the arms or the legs do not meet in one skeleton")
    while _has(J[chest]["name"], "neck") and J[chest]["parent"] is not None:   # Biped: the clavicles hang from the neck
        chest = J[chest]["parent"]
    trunk = real(meet(chest, pelvis))
    roles.update(chest=chest, pelvis=pelvis, root=trunk)

    def limb(e, stop, names):
        path = list(reversed(up(e)[:up(e).index(stop)]))   # from the body out, without the meeting joint
        span = sum(math.dist(_pos(J[path[k]]["world"]), _pos(J[path[k + 1]]["world"])) for k in range(len(path) - 1))
        path = [p for k, p in enumerate(path)   # helper joints on top of the one above (twist, "_is_helper") are skipped
                if k == 0 or math.dist(_pos(J[p]["world"]), _pos(J[path[k - 1]]["world"])) > 0.01 * span]
        if len(path) < 3:
            return dict(zip(names[-len(path):], path))
        named = {}
        for p in path:   # the names, when they say it (Biped, Mixamo, Rigify, most rigs)
            r = part_of(J[p]["name"], names)
            if r and r not in named:
                named[r] = p
        if names[1] in named and names[2] in named:
            if names[3] not in named:
                k = path.index(named[names[2]])
                if k + 1 < len(path):
                    named[names[3]] = path[k + 1]
            return named
        seg = [math.dist(_pos(J[path[k]]["world"]), _pos(J[path[k + 1]]["world"])) for k in range(len(path) - 1)]
        best = max(range(len(seg) - 1), key=lambda k: seg[k] + seg[k + 1])
        out = {names[1]: path[best], names[2]: path[best + 1], names[3]: path[best + 2]}
        if best > 0:
            out[names[0]] = path[best - 1]
        return out
    for s in ("L", "R"):
        for k, v in limb(ends[("arm", s)], chest, ["clavicle", "upperarm", "forearm", "hand"]).items():
            roles["%s_%s" % (s, k)] = v
        for k, v in limb(ends[("leg", s)], pelvis, ["hip", "thigh", "shin", "foot"]).items():
            roles["%s_%s" % (s, k)] = v
    roles["spine"] = list(reversed(up(chest)[1:up(chest).index(trunk)])) if trunk in up(chest)[1:] else []
    heads = [i for i, j in enumerate(J) if _has(j["name"], "head") and not FINGER.search(j["name"]) and chest in up(i)[1:]
             and side(j["name"]) is None]
    if heads:
        head = min(heads, key=depth)
        roles["head"] = head
        roles["neck"] = list(reversed(up(head)[1:up(head).index(chest)]))
        jaws = [i for i, j in enumerate(J) if _has(j["name"], "jaw") and head in up(i)[1:]]
        if jaws:
            roles["jaw"] = min(jaws, key=depth)
    def solid(lst):   # without the helper joints that sit on the joint above them
        return [i for i in lst if J[i]["parent"] is None or math.dist(_pos(J[i]["world"]), _pos(J[J[i]["parent"]]["world"])) > 1e-4]
    roles["spine"] = solid(roles["spine"])
    if "neck" in roles:
        roles["neck"] = solid(roles["neck"])
    tails = [i for i, j in enumerate(J) if _has(j["name"], "tail")]
    if tails:
        first = min(tails, key=depth)
        chain = [first]
        while True:
            nxt = [c for c in kids.get(chain[-1], []) if _has(J[c]["name"], "tail")]
            if not nxt:
                break
            chain.append(nxt[0])
        roles["tail"] = chain
    return roles


def base_roles(src_path):
    """{label: node name} of the source (the kit's export), labels from bonelabels."""
    ed = ngpanim.Edited(src_path)
    lab = bonelabels.labels(ed.nodes)
    return {nm: ed.nodes[i]["name"] for i, nm in lab.items()}


def build_map(src_path, mdl, log=print):
    """The map (dict) for retarget.build, and a log of what went where."""
    B = base_roles(src_path)
    if "Chest" not in B and "Root" not in B:
        raise MapError("the base's skeleton has no chest or root the kit can name")
    R = model_roles(mdl)
    J = mdl.joints
    nm = lambda i: J[i]["name"]   # noqa: E731
    bones = {}
    chains = []
    top = B.get("Root")
    pelvis_b = B.get("Pelvis", top)
    chest_b = B.get("Chest", top)
    if R["root"] != R["pelvis"]:
        bones[nm(R["root"])] = {"from": top, "move": True}
        bones[nm(R["pelvis"])] = {"from": pelvis_b}
    else:
        bones[nm(R["pelvis"])] = {"from": top, "move": True}
    if R["chest"] not in (R["root"], R["pelvis"]):
        bones[nm(R["chest"])] = {"from": chest_b}
    bspine = [B[k] for k in sorted((k for k in B if re.fullmatch(r"Spine\d+", k)), key=lambda k: int(k[5:]))]
    if R["spine"]:
        if bspine:
            chains.append({"joints": [nm(i) for i in R["spine"]], "from": bspine})
        else:
            n = len(R["spine"])
            for k, i in enumerate(R["spine"]):
                bones[nm(i)] = {"from": [top, chest_b, round((k + 1) / (n + 1.0), 3)]}
    if "head" in R:
        head_b = B.get("Head", chest_b)
        bones[nm(R["head"])] = {"from": head_b}
        bneck = [B[k] for k in sorted((k for k in B if re.fullmatch(r"Neck\d+", k)), key=lambda k: int(k[4:]))]
        if R.get("neck"):
            if bneck:
                chains.append({"joints": [nm(i) for i in R["neck"]], "from": bneck})
            else:
                n = len(R["neck"])
                for k, i in enumerate(R["neck"]):
                    bones[nm(i)] = {"from": [chest_b, head_b, round((k + 1) / (n + 1.0), 3)]}
        if "jaw" in R and "Jaw" in B:
            bones[nm(R["jaw"])] = {"from": B["Jaw"]}
    for s in ("L", "R"):
        for part, label, nxt, nxt_label in (("clavicle", "Clavicle", None, None),
                                            ("upperarm", "UpperArm", "forearm", "Forearm"),
                                            ("forearm", "Forearm", "hand", "Hand"),
                                            ("hand", "Hand", None, None),
                                            ("thigh", "Thigh", "shin", "Shin"),
                                            ("shin", "Shin", "foot", "Foot"),
                                            ("foot", "Foot", None, None)):
            i = R.get("%s_%s" % (s, part))
            b = B.get("%s_%s" % (s, label))
            if i is None or b is None:
                continue
            spec = {"from": b}
            ni, nb = R.get("%s_%s" % (s, nxt)) if nxt else None, B.get("%s_%s" % (s, nxt_label)) if nxt else None
            if ni is not None and nb is not None:
                spec["align"] = [nm(ni), nb]
            if part == "foot":
                spec["level"] = True
            bones[nm(i)] = spec
    btail = [B[k] for k in sorted((k for k in B if re.fullmatch(r"Tail\d+", k)), key=lambda k: int(k[4:]))]
    if R.get("tail") and btail:
        chains.append({"joints": [nm(i) for i in R["tail"]], "from": btail})
    m = {"comment": "made by tools/automap.py from %s and %s; edit it and run the retarget again" % (
        os.path.basename(src_path), os.path.basename(mdl.path)), "bones": bones}
    if chains:
        m["chains"] = chains
    done = set(bones) | {j for c in chains for j in c["joints"]}
    log("bone map: %d joints of the model follow the base (%s); %d ride on the joint above them" % (
        len(done), ", ".join("%s<-%s" % (k, bonelabels.canonical(v["from"]) if isinstance(v["from"], str) else "blend")
                             for k, v in list(bones.items())[:8]) + (", ..." if len(bones) > 8 else ""),
        len(J) - len(done)))
    return m


def main(argv):
    up = None
    if "--up" in argv:
        k = argv.index("--up")
        up = argv[k + 1]
        argv = argv[:k] + argv[k + 2:]
    if len(argv) != 3:
        print(__doc__.split("\n\n")[1])
        return 2
    import retarget
    try:
        mdl = retarget.load_model(argv[1], up)
        m = build_map(argv[0], mdl)
    except (MapError, retarget.RetargetError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    with open(argv[2], "w", encoding="utf-8") as f:
        json.dump(m, f, indent=1)
    print("wrote %s" % argv[2])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
