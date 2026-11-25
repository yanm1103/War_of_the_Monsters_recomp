#!/usr/bin/env python3
"""Checks a custom monster's animations without watching them: every clip is measured and compared with the same clip of the
base monster and with what the monsters of the disc do in that clip.

    python tools/anim_check.py --mod "mods/Mechagodzilla 3"          # every monsters/<id>/ of a mod with its own model
    python tools/anim_check.py <model.NGP> --base disc/MON/LIZARD0.NGP
    python tools/anim_check.py --roster                              # the disc's monsters, each against the others

Each clip (animation id, docs/ANIM_IDS.md) that a state plays on its own is posed at a few frames (tools/pose.py through
move_check.Rig) and reduced to numbers that do not depend on the monster's size: lengths are in body heights (the height of
the centre locator 0x809 in the idle, animation 90), foot heights count from where the foot stands in the idle.
- the body's centre: highest, lowest and at the last frame; how far it ends from where it started over the ground;
- the feet (0x804/0x805): the lower foot's lowest height and its height at the last frame; in the idles, walks and runs
  also the left-to-right gap between them (negative = crossed) and how far each foot is turned from its idle heading;
- the hands and the mouth (0x802, 0x803, 0x80B): highest and lowest, counted from the centre;
- against the base's model: the ids it has and the custom model lacks, and clips twice as long or short.
Layers, additive clips and ids no code uses are only listed as missing, never posed.

Findings are `error` (a required animation is missing: the game refuses the move), `warn` (a clip is outside what every
monster of the disc does in it, see RULES) and `info`. One finding per problem, with every clip that has it. The reference
is measured from `disc/` at run time and kept in build/ (nothing of the game is stored in the repo). A custom monster on a
boss (a base that is not one of the ten playable monsters) only gets the lists against its base.
"""
import argparse
import glob
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import move_check as mc  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PIN, HAND_L, HAND_R, FOOT_L, FOOT_R, MOUTH = 0x809, 0x802, 0x803, 0x804, 0x805, 0x80B
LOCS = (HAND_L, HAND_R, FOOT_L, FOOT_R, MOUTH, PIN)
LOC_NAME = {HAND_L: "left hand", HAND_R: "right hand", FOOT_L: "left foot", FOOT_R: "right foot", MOUTH: "mouth", PIN: "body"}
IDLE = 90
SAMPLES = 16


def _rot(w):
    r = [[float(w[i][j]) for j in range(3)] for i in range(3)]
    for row in r:
        n = math.sqrt(sum(v * v for v in row)) or 1.0
        row[:] = [v / n for v in row]
    return r


def sample(rig, anim, n=SAMPLES):
    """[{locator: ((x, y, z), rotation rows)}] at n + 1 evenly spaced frames of the clip (both ends), or None."""
    fr = rig.frames(anim)
    if fr is None:
        return None
    k = max(1, min(n, int(math.ceil(fr))))
    out = []
    for i in range(k + 1):
        try:
            p = rig.m.pose(anim, fr * i / k)
        except Exception:  # noqa: BLE001 - a broken clip is reported as not measurable
            return None
        pts = {}
        for lid in LOCS:
            h = rig.locs.get(lid)
            if h is not None and h in p.xforms:
                w = p.xforms[h].world
                pts[lid] = ((float(w[3][0]), float(w[3][1]), float(w[3][2])), _rot(w))
        out.append(pts)
    return out


def _med(v):
    v = sorted(v)
    return v[len(v) // 2] if v else None


def _yaw(r, r0):
    """Degrees the locator's bone turned about the vertical since the idle pose (the heading of what pointed forward there)."""
    # world rows: v_world = v_local * R. The idle's forward (+y world) in local is R0 * y; now it points to (R0 * y) * R.
    loc = [r0[i][1] for i in range(3)]
    x = sum(loc[i] * r[i][0] for i in range(3))
    y = sum(loc[i] * r[i][1] for i in range(3))
    if math.hypot(x, y) < 0.3:
        return None   # it points up or down now: no heading
    return math.degrees(math.atan2(x, y))


def measure(rig, n=SAMPLES):
    """{"size": {...}, "clips": {id: {metric: value}}} of a model. Lengths are in body heights (the pin's height in the idle),
    foot heights are above the foot's own height in the idle, the feet's gap is in idle gaps."""
    idle = sample(rig, IDLE, n)
    out = {"size": None, "clips": {}, "locs": sorted(l for l in LOCS if l in rig.locs)}
    ids = sorted({a[0] for a in rig.m.anims()} - {0})
    out["frames"] = {str(i): rig.frames(i) for i in ids}
    if not idle or PIN not in idle[0]:
        return out
    H = _med([p[PIN][0][2] for p in idle])
    if not H or H <= 1.0:
        return out
    feet = [l for l in (FOOT_L, FOOT_R) if l in idle[0]]
    g = {l: _med([p[l][0][2] for p in idle]) for l in feet}
    gap0 = _med([p[FOOT_R][0][0] - p[FOOT_L][0][0] for p in idle]) if len(feet) == 2 else None
    r0 = {l: idle[0][l][1] for l in feet}
    out["size"] = {"H": H, "foot": g, "gap": gap0}
    for aid in ids:
        tr = sample(rig, aid, n)
        if not tr or PIN not in tr[0]:
            continue
        c = {}
        pz = [p[PIN][0][2] / H for p in tr]
        c["pin_hi"], c["pin_lo"], c["pin_end"] = max(pz), min(pz), pz[-1]
        a, b = tr[0][PIN][0], tr[-1][PIN][0]
        c["travel"] = math.hypot(b[0] - a[0], b[1] - a[1]) / H
        c["drift"] = max(math.hypot(p[PIN][0][0] - a[0], p[PIN][0][1] - a[1]) for p in tr) / H
        if feet:
            low = [min((p[l][0][2] - g[l]) / H for l in feet) for p in tr]     # the lower foot, per frame
            c["foot_lo"], c["foot_hi"], c["foot_end"] = min(low), max(low), low[-1]
            c["foot_mean"] = sum(low) / len(low)
            for l in feet:
                yw = [v for v in (_yaw(p[l][1], r0[l]) for p in tr) if v is not None]
                if yw:
                    c["yaw_%x" % l] = max(yw, key=abs)
                    c["yawmed_%x" % l] = _med(yw)
        if gap0 and abs(gap0) > 0.02 * H:
            sg = math.copysign(H, gap0)
            gp = [(p[FOOT_R][0][0] - p[FOOT_L][0][0]) / sg for p in tr]
            c["gap_lo"], c["gap_hi"], c["gap_mean"] = min(gp), max(gp), sum(gp) / len(gp)
        for l in (HAND_L, HAND_R, MOUTH):
            if l in tr[0]:
                rel = [(p[l][0][2] - p[PIN][0][2]) / H for p in tr]      # above the body's centre
                c["hi_%x" % l], c["lo_%x" % l] = max(rel), min(rel)
                c["reach_%x" % l] = max(math.hypot(p[l][0][0] - p[PIN][0][0], p[l][0][1] - p[PIN][0][1]) for p in tr) / H
        out["clips"][str(aid)] = {k: round(v, 3) for k, v in c.items()}
    return out


# ---------------------------------------------------------------- the disc's monsters as the reference

# costume 0 of the ten playable monsters: what "a monster of the game does in this clip" means (monster.json's `base` names)
MONSTERS = {"congar": "MONKEY", "robo-47": "ROBOT", "togera": "LIZARD", "ultra-v": "SHOGUN", "preytor": "MANTIS",
            "kineticlops": "ENERGY", "magmo": "LAVA", "raptros": "DRAGON", "agamo": "ROCK", "zorgulon": "ALIEN"}
BOSSES = {"assboss": "ASSBOSS", "jelly": "JELLY", "final": "FINAL"}
CACHE = os.path.join(HERE, "..", "build", "anim_check_ref.json")   # build/ is not in the repo
GROUND = (1, 2, 6, 7, 8, 9, 90, 95, 96, 172, 173, 176)   # idles, walks and runs (docs/ANIM_IDS.md)
MIN_REF = 4       # a clip fewer monsters of the disc have is not compared
_REFS = {}
_ANI = None


def anim_table():
    """{id (str): {name, group, loop, conf, text}} of tools/anim_ids.json (generated from docs/ANIM_IDS.md)."""
    global _ANI
    if _ANI is None:
        try:
            with open(os.path.join(HERE, "anim_ids.json"), encoding="utf-8") as fh:
                _ANI = json.load(fh)["ids"]
        except (OSError, ValueError, KeyError):
            _ANI = {}
    return _ANI


def clip_name(aid):
    a = anim_table().get(str(aid))
    return "%s %s" % (aid, a["name"]) if a else str(aid)


def posed(aid):
    """Clips the game plays on their own (not a layer, not additive) and that some state uses: the ones worth posing."""
    a = anim_table().get(str(aid))
    return bool(a) and a.get("loop") in ("loop", "once", "hold")


def ground(aid):
    """Clips where the monster stands or walks on the ground the whole time."""
    return int(aid) in GROUND


def _stamp(d):
    files = [os.path.abspath(__file__)] + [os.path.join(HERE, n) for n in ("move_check.py", "pose.py", "animcurve.py")]
    files += [os.path.join(d, "MON", n + "0.NGP") for n in sorted(MONSTERS.values())]
    return [[os.path.basename(f), os.path.getsize(f), int(os.path.getmtime(f))] for f in files if os.path.isfile(f)]


def reference(disc=None):
    """{name: measure()} of the disc's playable monsters, or None without the disc. Kept in build/ (outside the repo) until
    the disc's models or the measuring code change."""
    d = mc.disc_dir(disc)
    if not d:
        return None
    if d in _REFS:
        return _REFS[d]
    stamp = _stamp(d)
    try:
        with open(CACHE, encoding="utf-8") as fh:
            c = json.load(fh)
        if c.get("stamp") == stamp:
            _REFS[d] = c["ref"]
            return c["ref"]
    except (OSError, ValueError):
        pass
    ref = {}
    for name in sorted(MONSTERS.values()):
        path = os.path.join(d, "MON", name + "0.NGP")
        try:
            ref[name] = measure(mc.Rig(path))
        except Exception:  # noqa: BLE001 - one unreadable model does not take the reference down
            continue
    _REFS[d] = ref
    if ref:
        try:
            os.makedirs(os.path.dirname(CACHE), exist_ok=True)
            with open(CACHE, "w", encoding="utf-8") as fh:
                json.dump({"stamp": stamp, "ref": ref}, fh)
        except OSError:
            pass
    return ref


def spread_ground(ref, metric, skip=None):
    """(lowest, highest, how many) of a metric over the idles, walks and runs of the reference monsters (but `skip`)."""
    v = [c[metric] for n, m in ref.items() if n != skip for a, c in m["clips"].items() if ground(a) and metric in c]
    return (min(v), max(v), len(v)) if len(v) >= MIN_REF else None


def spread(ref, aid, metric, skip=None):
    """(lowest, highest, how many) of a metric of a clip among the reference monsters (but `skip`), or None when too few."""
    v = [m["clips"][aid][metric] for n, m in ref.items() if n != skip and aid in m["clips"] and metric in m["clips"][aid]]
    return (min(v), max(v), len(v)) if len(v) >= MIN_REF else None


# ---------------------------------------------------------------- findings

# where "all": a clip is off when its number is outside what the monsters of the disc do in that same clip by more than
# max(A, K * (the disc's highest - lowest)). where "ground": the idles, walks and runs are compared with what the disc's
# monsters do in any of those clips, with a margin of A (the stance and the stride differ too much from clip to clip).
# A and K are set so that the disc's own monsters, each compared with the other nine, get almost nothing (`--roster`).
# (code, metric, side, A, K, where, title, what, hint); metric "..._*": one per locator; in body heights, or degrees (yaw)
RETARGET = "Redo these clips in the retarget (the source clip picked for each id, and the root/hips track)."
LOC_HINT = ("The game tests hits, grabs and effects on this locator. Fine when the limb really goes there; otherwise the clip in "
            "the slot is another move, or the locator hangs from the wrong bone.")
RULES = (
    ("ends_feet_up", "foot_end", +1, 0.25, 2.0, "all", "Ends with the feet off the ground",
     "Height of the lower foot at the last frame, in % of the body's standing height (0% = where the foot stands in the idle)",
     "The game goes from the end of the clip straight to the next state, so the monster snaps to the ground. The clip was "
     "probably taken from a jump, a flight or a lying pose. " + RETARGET),
    ("ends_body_high", "pin_end", +1, 0.35, 1.5, "all", "Ends with the body up in the air",
     "Height of the body's centre at the last frame, in % of its height standing in the idle",
     "The clip ends flying or jumping and the game snaps the monster down. " + RETARGET),
    ("ends_body_low", "pin_end", -1, 0.35, 1.5, "all", "Ends lying or crouched where the game's monsters end standing",
     "Height of the body's centre at the last frame, in % of its height standing in the idle",
     "The clip ends on the ground and the monster pops up when the next state starts. " + RETARGET),
    ("body_high", "pin_hi", +1, 0.35, 2.0, "all", "Body leaves the ground",
     "Highest the body's centre gets during the clip, in % of its height standing in the idle",
     "Fine when the move is a jump or a flight on purpose; otherwise the clip kept the source's root height. " + RETARGET),
    ("body_stays_up", "pin_lo", +1, 0.35, 2.0, "all", "Body never comes down",
     "Lowest the body's centre gets during the clip, in % of its height standing in the idle",
     "The game's monsters crouch, fall or lie in these clips and this one stays up: probably a standing or flying clip in the slot. " + RETARGET),
    ("body_low", "pin_lo", -1, 0.35, 2.0, "all", "Body drops lower than any monster's",
     "Lowest the body's centre gets during the clip, in % of its height standing in the idle",
     "The body goes down to the ground (or through it) in a clip where the game's monsters stay up. " + RETARGET),
    ("walk_feet_float", "foot_lo", +1, 0.10, 0.0, "ground", "Feet float over the ground while standing or walking",
     "Lowest the lower foot gets during the clip, in % of the body's standing height (0% = where the foot stands in the idle)",
     "Neither foot comes down to the ground in a clip that stands or walks on it: the hips' track is too high in these clips, or the legs are shorter than the source's."),
    ("feet_float", "foot_lo", +1, 0.35, 2.0, "all", "Feet never touch the ground",
     "Lowest the lower foot gets during the clip, in % of the body's standing height (0% = where the foot stands in the idle)", "Neither foot comes down during the whole clip. " + RETARGET),
    ("walk_feet_sink", "foot_lo", -1, 0.15, 0.0, "ground", "Feet sink in the ground while standing or walking",
     "Lowest a foot gets during the clip, in % of the body's standing height (0% = where the foot stands in the idle)",
     "A foot goes through the floor in a clip that stands or walks on it: the hips' track is too low in these clips, or the legs are longer than the source's."),
    ("feet_sink", "foot_lo", -1, 0.25, 2.0, "all", "Feet go under the ground",
     "Lowest a foot gets during the clip, in % of the body's standing height (0% = where the foot stands in the idle)",
     "A foot goes through the floor: the legs or the hips' height are off in these clips. Raise the hips' track or redo the legs in the retarget."),
    ("root_moves", "travel", +1, 0.25, 2.0, "all", "The clip moves the body away",
     "How far the body's centre ends from where it started, over the ground, in % of the body's standing height",
     "The game moves the monster itself and plays these clips in place: with the motion left in the clip the body slides away "
     "and snaps back at the end. Remove the root motion from these clips (keep the hips over the origin)."),
    ("feet_cross", "gap_lo", -1, 0.25, 0.0, "ground", "Feet cross each other while standing or walking",
     "Smallest left-to-right gap between the feet during the clip, in % of the body's standing height (negative = the left foot is on the right side)",
     "The legs swap sides: left and right are mirrored or twisted in the retarget of these clips. Check the leg bones' mapping and roll."),
    ("foot_twist", "yawmed_*", 0, 30.0, 0.0, "ground", "Foot twisted while standing or walking",
     "How far a foot points away from where it points in the idle, for most of the clip (degrees, + = to the monster's right)",
     "A foot stays turned in or out: fix the roll of the leg bones in the retarget of these clips."),
    ("loc_high", "hi_*", +1, 0.35, 3.0, "all", "{loc} higher than any monster's", "Highest the {loc} locator gets above the body's centre, in % of the body's standing height", LOC_HINT),
    ("loc_low", "lo_*", -1, 0.35, 3.0, "all", "{loc} lower than any monster's", "Lowest the {loc} locator gets, counted from the body's centre, in % of the body's standing height", LOC_HINT),
)
# a clip is listed once among these, by the first that catches it (they describe the same thing)
FAMILIES = (("ends_feet_up", "ends_body_high", "ends_body_low", "body_high", "body_stays_up", "body_low", "walk_feet_float", "feet_float"),
            ("walk_feet_sink", "feet_sink"))
ODD = 0.25        # a hand or mouth locator this far outside the disc's range in the idle sits on another part of the body
SHOWN = 6         # clips spelled out per finding; the rest only by id
DUR = 2.0         # a clip this many times longer or shorter than the base's is listed (info)
SEV = {"error": 0, "warn": 1, "info": 2}


def _fmt(v, deg):
    return "%.0f deg" % v if deg else "%.0f%%" % (100.0 * v)


def _excess(v, lo, hi, side, A, K):
    """How far v is outside lo..hi on the side asked (0 = both), when beyond the tolerance; else 0."""
    e = v - hi if v > hi else v - lo if v < lo else 0.0
    if side and e * side <= 0:
        return 0.0
    return e if abs(e) > max(A, K * (hi - lo)) else 0.0


def odd_locators(meas, ref, skip=None):
    """Hand and mouth locators that, in the idle, are not where any monster of the disc has them (a head, a wing):
    {locator: (its height above the centre, lowest, highest of the disc)}."""
    out = {}
    c = meas["clips"].get(str(IDLE), {})
    for l in (HAND_L, HAND_R, MOUTH):
        k = "hi_%x" % l
        s = spread(ref, str(IDLE), k, skip)
        if k in c and s and (c[k] > s[1] + ODD or c[k] < s[0] - ODD):
            out[l] = (c[k], s[0], s[1])
    return out


def findings(meas, ref, base=None, skip=None, base_name="the base"):
    """[{severity, code, title, text, hint, anims}] of a model's measurements against the reference monsters (`ref`,
    without `skip`: the monster itself in --roster) and its base's measurements (`base`, or None)."""
    out = []
    ani = anim_table()

    def add(sev, code, title, text, hint, anims):
        out.append({"severity": sev, "code": code, "title": title, "text": text, "hint": hint, "anims": sorted(int(a) for a in anims)})

    if base is not None:
        have = set(meas["frames"])
        gone = [a for a in base["frames"] if a not in have and a in ani]
        need = sorted((a for a in gone if "equired" in ani[a].get("text", "")), key=int)
        rest = sorted((a for a in gone if a not in need and ani[a].get("loop") is not None), key=int)
        if need:
            add("error", "missing_required", "A move cannot be used: its animation is missing",
                "The model has no animation %s. The game refuses the whole move without it." % ", ".join(clip_name(a) for a in need),
                "Add a clip with each of these ids to the rig (%s has them)." % base_name, need)
        if rest:
            add("info", "missing", "Animations of the base that the model lacks",
                "%s has these and the model does not: %s. The game skips or replaces what uses them." % (base_name, ", ".join(clip_name(a) for a in rest)),
                "Add them if the move should exist; nothing breaks without them.", rest)
        slow = []
        for a in sorted(have & set(base["frames"]), key=int):
            f, b = meas["frames"][a], base["frames"][a]
            if posed(a) and f and b and (f / b >= DUR or b / f >= DUR):
                slow.append((a, f, b))
        if slow:
            add("info", "duration", "Clips much longer or shorter than the base's",
                "Frames (this monster / %s): %s. The game plays a clip for its own length, so the move takes that much longer or shorter." % (
                    base_name, "; ".join("%s %.0f / %.0f" % (clip_name(a), f, b) for a, f, b in slow)),
                "Fine when the move is meant to be slower or faster; otherwise resample the clip.", [a for a, _f, _b in slow])

    if not meas.get("size"):
        add("warn", "not_measured", "The clips cannot be measured",
            "The model has no idle (animation %d) or no centre locator (0x%X), which give the monster's size and where the ground is." % (IDLE, PIN),
            "Add the idle clip and the centre (pin) locator to the rig.", [])
        return sorted(out, key=lambda f: SEV[f["severity"]])
    if not ref:
        return sorted(out, key=lambda f: SEV[f["severity"]])

    odd = odd_locators(meas, ref, skip)
    if odd:
        add("info", "locator_elsewhere", "Locators on another part of the body",
            "In the idle, %s. Their heights are not compared in the other clips." % "; ".join(
                "the %s locator is %s of the standing height %s the body's centre (the game's monsters: %s to %s)" % (
                    LOC_NAME[l], _fmt(abs(v), False), "above" if v >= 0 else "below", _fmt(lo, False), _fmt(hi, False)) for l, (v, lo, hi) in sorted(odd.items())),
            "Expected for a body without that limb (a head or a wing carries the locator). The hit of a move on that locator comes from there.", [])

    taken = [set() for _f in FAMILIES]   # clips already listed by a rule of each family
    for code, metric, side, A, K, where, title, what, hint in RULES:
        if metric.endswith("*"):
            lids = (FOOT_L, FOOT_R) if metric.startswith("yaw") else tuple(l for l in (HAND_L, HAND_R, MOUTH) if l not in odd)
            keys = [(metric[:-1] + "%x" % l, l) for l in lids]
        else:
            keys = [(metric, None)]
        per_loc = metric.startswith(("hi_", "lo_"))
        deg = metric.startswith("yaw")
        groups = {}
        fam = next((t for f, t in zip(FAMILIES, taken) if code in f), None)
        for key, lid in keys:
            pool = spread_ground(ref, key, skip) if where == "ground" else None
            for aid, c in meas["clips"].items():
                if key not in c or not posed(aid) or (where == "ground" and not ground(aid)) or (fam is not None and aid in fam):
                    continue
                s = pool if where == "ground" else spread(ref, aid, key, skip)
                if not s:
                    continue
                e = _excess(c[key], s[0], s[1], side, A, K)
                if e:
                    b = base["clips"].get(aid, {}).get(key) if base and base.get("clips") else None
                    groups.setdefault(lid if per_loc else 0, []).append((abs(e), aid, c[key], b, s[0], s[1], lid))
        for gl, rows in sorted(groups.items()):
            best = {}
            for r in rows:      # a clip caught on both feet: the worse one
                if r[1] not in best or r[0] > best[r[1]][0]:
                    best[r[1]] = r
            rows = sorted(best.values(), key=lambda r: (-r[0], int(r[1])))
            if fam is not None:
                fam.update(r[1] for r in rows)
            name = LOC_NAME.get(gl, "")
            parts = []
            for _e, aid, v, b, lo, hi, lid in rows[:SHOWN]:
                parts.append("%s%s: %s (%s%s to %s in the game%s)" % (
                    clip_name(aid), " (%s)" % LOC_NAME[lid] if deg else "", _fmt(v, deg),
                    "%s %s, " % (base_name, _fmt(b, deg)) if b is not None else "", _fmt(lo, deg), _fmt(hi, deg),
                    "'s idles, walks and runs" if where == "ground" else ""))
            more = [r[1] for r in rows[SHOWN:]]
            text = "%s. %s%s." % (what.format(loc=name), "; ".join(parts), "; and %d more (%s)" % (len(more), ", ".join(sorted(more, key=int))) if more else "")
            t = title.format(loc=name)
            add("warn", code + ("_%x" % gl if gl else ""), t[0].upper() + t[1:], text, hint, [r[1] for r in rows])
    return sorted(out, key=lambda f: SEV[f["severity"]])


# ---------------------------------------------------------------- a model, a mod, the roster

def base_ngp(ngp, disc=None, folder=None):
    """The disc's model a custom model stands on: the file of the same name in MON/, or monster.json's `base` and `costume`."""
    d = mc.disc_dir(disc)
    if not d:
        return None
    p = os.path.join(d, "MON", os.path.basename(ngp).upper())
    if os.path.isfile(p):
        return p
    try:
        with open(os.path.join(folder or os.path.dirname(os.path.dirname(os.path.abspath(ngp))), "monster.json"), encoding="utf-8") as fh:
            j = json.load(fh)
    except (OSError, ValueError):
        return None
    pre = {**MONSTERS, **BOSSES}.get(str(j.get("base", "")).lower())
    p = os.path.join(d, "MON", "%s%d.NGP" % (pre, int(j.get("costume") or 0))) if pre else None
    return p if p and os.path.isfile(p) else None


def check(ngp, base=None, disc=None):
    """{"findings": [...], "base": path or None, "reference": bool, "compared": bool, "clips": measured} of one model against
    `base` (a .NGP; default: the disc's model of the same file name, or the one monster.json names)."""
    ref = reference(disc)
    base = base or base_ngp(ngp, disc)
    meas = measure(mc.Rig(ngp))
    bm, bname = None, "the base"
    if base:
        bname = os.path.splitext(os.path.basename(base))[0]
        bm = ref[bname[:-1]] if ref and bname.endswith("0") and bname[:-1] in ref else measure(mc.Rig(base))
    # a base that is not one of the playable monsters (a boss) has clips of its own: only the lists against it make sense
    playable = not base or bname[:-1] in MONSTERS.values()
    fs = findings(meas, ref if playable else None, bm, base_name=bname)
    return {"findings": fs, "base": base, "reference": bool(ref), "compared": bool(ref) and playable, "clips": len(meas["clips"])}


def mod_monsters(mod):
    """[(id, .NGP, folder)] of the monsters of a mod folder that bring their own model."""
    out = []
    for d in sorted(glob.glob(os.path.join(mod, "monsters", "*", "files"))):
        ng = sorted(n for n in os.listdir(d) if n.upper().endswith(".NGP"))
        if ng:
            out.append((os.path.basename(os.path.dirname(d)), os.path.join(d, ng[0]), os.path.dirname(d)))
    return out


def check_mod(mod_dir, disc=None):
    """[{"monster": id, "model", "base", "findings": [{severity, code, title, text, hint, anims}], ...}] of the monsters of a
    mod folder that bring their own model."""
    out = []
    for mid, ngp, folder in mod_monsters(mod_dir):
        try:
            r = check(ngp, base_ngp(ngp, disc, folder), disc)
        except Exception as e:  # noqa: BLE001 - an unreadable model is a finding, not a crash
            r = {"findings": [{"severity": "error", "code": "unreadable", "title": "The model cannot be read", "text": str(e),
                               "hint": "Export the model again.", "anims": []}], "base": None, "reference": False, "compared": False, "clips": 0}
        out.append({"monster": mid, "model": ngp, **r})
    return out


def check_roster(disc=None):
    """[{"monster": name, "findings"}] of the disc's playable monsters, each against the other nine."""
    ref = reference(disc) or {}
    return [{"monster": n, "findings": findings(m, ref, None, skip=n)} for n, m in sorted(ref.items())]


def report(label, r, every=False):
    fs = [f for f in r["findings"] if every or f["severity"] != "info"]
    hid = len(r["findings"]) - len(fs)
    out = ["%s%s" % (label, "  (against %s)" % os.path.basename(r["base"]) if r.get("base") else "")]
    if r.get("reference") is False:
        out.append("  no disc: nothing was compared with the game's monsters")
    elif r.get("compared") is False:
        out.append("  the base is not a playable monster: only the lists of missing and resized clips are made")
    n = [sum(f["severity"] == s for f in r["findings"]) for s in ("error", "warn", "info")]
    out.append("  FINDINGS: %d error, %d warn, %d info%s" % (n[0], n[1], n[2], "  (--all shows the info)" if hid else ""))
    for f in fs:
        out += ["    [%s] %s%s" % (f["severity"], f["title"], "  (%d clip%s)" % (len(f["anims"]), "s" if len(f["anims"]) > 1 else "") if f["anims"] else ""),
                "        %s" % f["text"], "        Fix: %s" % f["hint"]]
    return out


def reference_text(ref):
    """The disc's numbers per rule: how many clips are compared and how far apart the disc's monsters are in them."""
    out = ["REFERENCE: %d monsters of the disc (%s)" % (len(ref), ", ".join(sorted(ref))),
           "  per clip: off when outside the disc's lowest..highest for that clip by more than max(A, K x (highest - lowest))",
           "  ground:   off when outside the disc's lowest..highest over all idles, walks and runs by more than A",
           "  %-15s %-9s %7s %4s %6s  %s" % ("rule", "metric", "A", "K", "clips", "the disc")]
    ids = sorted({a for m in ref.values() for a in m["clips"]}, key=int)
    for code, metric, _side, A, K, where, _t, _w, _h in RULES:
        deg = metric.startswith("yaw")
        keys = [metric[:-1] + "%x" % l for l in ((FOOT_L, FOOT_R) if deg else (HAND_L, HAND_R, MOUTH))] if metric.endswith("*") else [metric]
        if where == "ground":
            ps = [s for s in (spread_ground(ref, k) for k in keys) if s]
            if ps:
                out.append("  %-15s %-9s %7s %4s %6d  ground: %s" % (code, metric, _fmt(A, deg), "-", sum(1 for a in ids if ground(a)),
                                                                    ", ".join("%s to %s" % (_fmt(s[0], deg), _fmt(s[1], deg)) for s in ps)))
            continue
        w = sorted(s[1] - s[0] for k in keys for a in ids if posed(a) for s in [spread(ref, a, k)] if s)
        if w:
            out.append("  %-15s %-9s %7s %4.1f %6d  per clip, highest - lowest: median %s, widest %s" % (
                code, metric, _fmt(A, deg), K, len(w) // len(keys), _fmt(w[len(w) // 2], deg), _fmt(w[-1], deg)))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ngp", nargs="?", help="a custom monster's model")
    ap.add_argument("--base", help="the model it is compared with (default: the disc's model of the same file name)")
    ap.add_argument("--mod", action="append", default=[], help="a mod folder: checks every monsters/<id>/files/ with a .NGP")
    ap.add_argument("--disc", help="the extracted disc (default: disc/ of the repo, or WOTM_DISC)")
    ap.add_argument("--roster", action="store_true", help="the disc's spread per rule and the findings of the disc's own monsters")
    ap.add_argument("--all", action="store_true", help="show the info findings too")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    res, lines, judged = [], [], []
    if a.roster:
        ref = reference(a.disc)
        if not ref:
            print("error: no disc at %s" % (a.disc or "disc/"))
            return 1
        lines += reference_text(ref)
        for r in check_roster(a.disc):
            lines += [""] + report(r["monster"], r, a.all)
            res.append(r)
        lines.append("")
    elif not a.ngp and not a.mod:
        ap.error("a .NGP, --mod or --roster")
    if a.ngp:
        judged.append({"monster": os.path.basename(a.ngp), "model": a.ngp, **check(a.ngp, a.base, a.disc)})
        lines += report(judged[-1]["monster"], judged[-1], a.all) + [""]
    for mod in a.mod:
        got = check_mod(mod, a.disc)
        if not got:
            lines += ["%s: no monster with its own model (monsters/<id>/files/*.NGP)" % os.path.basename(os.path.normpath(mod)), ""]
        for r in got:
            lines += report("%s: %s" % (os.path.basename(os.path.normpath(mod)), r["monster"]), r, a.all) + [""]
            judged.append(r)
    print(json.dumps(res + judged, indent=1) if a.json else "\n".join(lines))
    return 1 if any(f["severity"] in ("error", "warn") for r in judged for f in r["findings"]) else 0


if __name__ == "__main__":
    sys.exit(main())
