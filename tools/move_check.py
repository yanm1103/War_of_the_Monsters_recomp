#!/usr/bin/env python3
"""Checks a monster's moves without playing: the timeline of every punch and of every combo link in fields (60 per second),
where the hit sphere goes during the hit window, and how those numbers compare with the monsters of the disc.

    python tools/move_check.py <file.CHR> --ngp <model.NGP> [--disc disc] [--json] [--all]
    python tools/move_check.py --mod "mods/Mechagodzilla 3"        # every monsters/<id>/files/ of a mod
    python tools/move_check.py --roster                            # the disc's monsters: the reference ranges and their own findings

The model is the one of `StatePunch::update`/`startChain` and `StateRecoil::update` (docs/FORMATS.md, "Character"):
- a move lasts frames / speed fields; the hit is tested from the hit window's start, but not while the blend into the move runs
  (the first move of a combo blends for +0x6C ms, a chained one for the previous move's delay to its type);
- a combo press is taken from `input_start` fields after the blend and the next move starts when the animation passes `chain_at`;
- a victim that is hit recoils for the move's `stun` fields (above 100 it ends up stunned; a knockback throws it instead), and
  can block or hit back after that.
So, per move: startup (first field that can hit), active, end, and the advantage on hit (victim's recoil left when the attacker
is free again). Per link: the fields between the two hits and how many of them the victim is free. The hit pause, the lunge and
the distance between the monsters are not modelled: the numbers are for ranking and for spotting what is off, not frame data.

Findings are `error` (the move cannot work as written), `warn` (outside what any monster of the disc does, or against a rule
all of them follow) and `info`. The reference ranges are measured from `disc/` at run time (nothing of the game is stored).
"""
import argparse
import glob
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chr_dump as cd  # noqa: E402
import chr_view as cv  # noqa: E402

FPS = 60.0
MS = FPS / 1000.0          # fields per millisecond
GETUP_ANIMS = (242, 243)   # StateGetupAttack: face down, face up
GETUP_LOC = 0x802          # the constructor's locator of the get-up attack (left hand)
PIN = 0x809                # the monster's centre (getPinTrans)
XFORM_OPS = (3, 4, 17, 30, 38)
SEV = {"error": 0, "warn": 1, "info": 2}


# ---------------------------------------------------------------- model: locators over an animation

class Rig:
    """The locators of a model and where each one is along an animation (model units, the monster at the origin facing +y)."""

    def __init__(self, ngp):
        import pose
        self.m = pose.Model(ngp)
        self.locs = {}
        img, seen = self.m.img, set()

        def walk(n, holder, depth):
            if depth > 80 or n in seen or not img.ok(n):
                return
            seen.add(n)
            op = img.u32(n) & 63
            if op == 40:
                lid = img.u32(n) >> 18
                if lid and holder is not None:
                    self.locs.setdefault(lid, holder)
                return
            h = n if op in XFORM_OPS else holder
            for arr, cnt in self.m._kids(n, op):
                for k in range(min(cnt, 4096)):
                    walk(img.u32(arr + 4 * k), h, depth + 1)

        walk(self.m.root, None, 0)
        self._cache = {}

    def frames(self, anim):
        a = self.m.find_anim(anim)
        return None if a is None else float(a[3] - a[2])

    def track(self, anim, lids, f0, f1):
        """[(frame, {locator: (x, y, z)})] for the frames f0..f1 of the animation (one per frame, both ends)."""
        key = (anim, tuple(lids), round(f0, 2), round(f1, 2))
        if key in self._cache:
            return self._cache[key]
        out = []
        n = max(1, int(math.ceil(f1 - f0)))
        for i in range(n + 1):
            f = f0 + (f1 - f0) * i / n
            try:
                p = self.m.pose(anim, float(f))
            except Exception:  # noqa: BLE001 - a broken animation is reported as "no track", not a crash
                return []
            pts = {}
            for lid in lids:
                h = self.locs.get(lid)
                if h is not None and h in p.xforms:
                    w = p.xforms[h].world
                    pts[lid] = (float(w[3][0]), float(w[3][1]), float(w[3][2]))
            out.append((f, pts))
        self._cache[key] = out
        return out


def sweep(rig, anim, lids, p0, p1, radius):
    """Where the hit spheres go while the animation runs from fraction p0 to p1: {reach, low, high, path, forward} or None.
    reach: farthest edge of a sphere from the monster's axis, on the ground plane; forward: the same along +y (the facing);
    low/high: the spheres' lowest and highest edge; path: the longest way one sphere's centre travels."""
    fr = rig.frames(anim)
    lids = [v for v in lids if v in rig.locs]
    if not fr or not lids:
        return None
    tr = rig.track(anim, lids, max(0.0, p0) * fr, min(1.0, p1) * fr)
    if not tr or not tr[0][1]:
        return None
    reach = forward = high = -1e9
    low = 1e9
    path = dict.fromkeys(lids, 0.0)
    prev = {}
    for _f, pts in tr:
        for lid, (x, y, z) in pts.items():
            reach = max(reach, math.hypot(x, y) + radius)
            forward = max(forward, y + radius)
            low, high = min(low, z - radius), max(high, z + radius)
            if lid in prev:
                path[lid] += math.dist(prev[lid], (x, y, z))
            prev[lid] = (x, y, z)
    return {"reach": reach, "forward": forward, "low": low, "high": high, "path": max(path.values())}


# ---------------------------------------------------------------- timeline of one move and of one link

def dur(r):
    """Fields the move's animation takes at its speed, or None without the model."""
    t = r.get("timing")
    if not t:
        return None
    return t["frames"] / (r["speed"] if r.get("speed", 0) > 0 else 1.0)


def move_end(r, blend):
    """Fields from the move's start until the attacker is out of it (StatePunch::update): with a wind-down (+0x3C < 1) the
    rest of the animation at the wind-down speed; without, the whole animation plus the blend to the idle, and never before
    the input window closes."""
    d = dur(r)
    ra = r.get("recover_at", 1.0)
    if 0 < ra < 1:
        rs = r["recover_speed"] if r.get("recover_speed", 0) > 0 else 1.0
        return d * ra + r["timing"]["frames"] * (1 - ra) / rs
    return max(d + max(0.0, r.get("rec_blend", 0.0)), blend + r.get("input_end", 0))


def knocks(r):
    return r["kb_out"] != 0 or r["kb_up"] != 0


def timeline(r, blend_ms):
    """One move entered with a blend of blend_ms: {d, blend, startup, active, hit_end, end, adv}; startup None = it cannot hit."""
    d = dur(r)
    blend = blend_ms * MS
    a, b = r["win_start"] * d, r["win_end"] * d
    start = max(a, blend)
    out = {"d": d, "blend": blend, "a": a, "b": b, "startup": start if start < b else None, "active": max(0.0, b - start),
           "end": move_end(r, blend)}
    out["adv"] = None if out["startup"] is None else r["stun"] - (out["end"] - out["startup"])
    return out


def is_real(r):
    return r["mult"] > 0 if r["weapon"] else r["damage"] > 0


def graph(m):
    """({(pos, type): row} of the punches, the reachable ones in order, [(from, to)] links)."""
    by = {(r["pos"], r["ptype"]): r for r in m["punches"]}
    reach, todo, links = [], [(1, k) for k in cv.TYPE_ORDER if (1, k) in by], []
    seen = set()
    while todo:
        key = todo.pop(0)
        if key in seen:
            continue
        seen.add(key)
        reach.append(key)
        for k in cv.TYPE_ORDER:
            nk = (key[0] + 1, k)
            if by[key]["next_ms"][k] > 0 and nk in by:
                links.append((key, nk))
                todo.append(nk)
    return by, reach, links


def name_of(r):
    return "%s step %d" % (r["type"], r["pos"])


def analyze(m, rig=None):
    """The measurements of a monster's punches (m = chr_view.moves_of with the model): {moves, links, openers, getup}.
    Nothing is judged here; see findings()."""
    out = {"moves": [], "links": [], "getup": [], "frames": bool(m.get("frames"))}
    if m.get("legacy") or not m.get("frames"):
        return out
    by, reach, links = graph(m)
    entry = {}   # (pos, type) -> the blends (ms) it can be entered with
    for key in reach:
        if key[0] == 1:
            entry.setdefault(key, []).append(by[key].get("first_ms", 0.0))
    for a, b in links:
        entry.setdefault(b, []).append(by[a]["next_ms"][b[1]])
    for key in reach:
        r = by[key]
        if not is_real(r) or dur(r) is None:
            continue
        t = timeline(r, min(entry[key]))
        row = {"pos": r["pos"], "ptype": r["ptype"], "type": r["type"], "button": r["button"], "name": name_of(r), "anim": r["anim"],
               "damage": r["damage"], "stun": r["stun"], "knocks": knocks(r), "stuns": r["stun"] > 100, "last": not any(a == key for a, _b in links),
               "blend": t["blend"], "startup": t["startup"], "active": t["active"], "end": t["end"], "adv": t["adv"],
               "total": t["d"], "sweep": None}
        if rig is not None and r["locators"]:
            row["sweep"] = sweep(rig, r["anim"], [v for v in r["locators"] if v >= 0x7D0], r["win_start"], r["win_end"], r["radius"])
        out["moves"].append(row)
    for a, b in links:
        ra, rb = by[a], by[b]
        if not is_real(ra) or not is_real(rb) or dur(ra) is None or dur(rb) is None:
            continue
        ta = timeline(ra, min(entry[a]))
        tb = timeline(rb, ra["next_ms"][b[1]])
        # the press is taken from input_start fields after the blend into `a`; the next move starts once the animation passes chain_at
        start = max(ra["chain_at"] * ta["d"], ta["blend"] + ra.get("input_start", 0))
        gap = None if ta["startup"] is None or tb["startup"] is None else start + tb["startup"] - ta["startup"]
        out["links"].append({"from": name_of(ra), "to": name_of(rb), "from_key": list(a), "to_key": list(b), "same": a[1] == b[1],
                             "button": ra["button"], "start": start, "cut_before": start < ta["a"] - 0.5,
                             "cut_inside": ta["a"] - 0.5 <= start < ta["b"] - 0.5, "hit_a": ta["a"], "hit_b": ta["b"],
                             "blend": tb["blend"], "blend_a": ta["blend"], "next_startup": tb["startup"], "gap": gap, "stun": ra["stun"],
                             "free": None if gap is None else gap - ra["stun"], "knocks": knocks(ra), "stuns": ra["stun"] > 100})
    if rig is not None:
        radius = m.get("default_radius") or 0.0
        for i, anim in enumerate(GETUP_ANIMS):
            fr = rig.frames(anim)
            g = {"anim": anim, "face": ("down", "up")[i], "frames": fr, "sweep": None}
            if fr:
                g["sweep"] = sweep(rig, anim, [GETUP_LOC], 0.0, 1.0, radius)
                pin = sweep(rig, anim, [PIN], 0.0, 1.0, 0.0)
                g["body_high"] = pin["high"] if pin else None
            out["getup"].append(g)
    return out


def routes(a, answer):
    """What a victim cannot fight back, given `answer` (the fields its own first move needs): {pressure, stun, reset}.
    A link holds the victim when it is free for fewer fields than `answer` before the next hit (it can block, not hit first).
    pressure: the longest run of such links from a first move {hits, damage, moves}; stun: the quickest first move from which
    such a run ends in a move that stuns {startup, hits, moves}, or None; reset: per move that neither stuns nor knocks down,
    the fields the victim is free before the attacker's fastest first move lands again."""
    by = {(x["pos"], x["ptype"]): x for x in a["moves"]}
    nxt = {}
    for l in a["links"]:
        if l["free"] is not None and not l["knocks"] and not l["stuns"] and l["free"] < answer:
            nxt.setdefault(tuple(l["from_key"]), []).append(tuple(l["to_key"]))
    best, stun = {}, {}

    def run(key):
        if key not in best:
            tail = max((run(n) for n in nxt.get(key, [])), default=(0, 0.0, []), key=lambda t: (t[0], t[1]))
            best[key] = (1 + tail[0], by[key]["damage"] + tail[1], [key] + tail[2])
        return best[key]

    def to_stun(key):   # the shortest held run from this move to one that stuns: [keys] or None
        if key not in stun:
            if by[key]["stuns"]:
                stun[key] = [key]
            else:
                c = [t for t in (to_stun(n) for n in nxt.get(key, [])) if t]
                stun[key] = [key] + min(c, key=len) if c else None
        return stun[key]

    openers = [x for x in a["moves"] if x["pos"] == 1 and x["startup"] is not None]
    out = {"pressure": None, "stun": None, "reset": {}}
    top = max((run((x["pos"], x["ptype"])) for x in openers), default=None, key=lambda t: (t[0], t[1]))
    if top:
        out["pressure"] = {"hits": top[0], "damage": top[1], "moves": [by[k]["name"] for k in top[2]]}
    cand = [(x["startup"], to_stun((x["pos"], x["ptype"]))) for x in openers]
    cand = [(st, path) for st, path in cand if path and len(path) > 1]
    if cand:
        st, path = min(cand, key=lambda t: t[0])
        out["stun"] = {"startup": st, "hits": len(path), "moves": [by[k]["name"] for k in path]}
    fastest = min((x["startup"] for x in openers), default=None)
    if fastest is not None:
        for x in a["moves"]:
            if x["adv"] is not None and not x["knocks"] and not x["stuns"]:
                out["reset"][x["name"]] = fastest - x["adv"]
    return out


# ---------------------------------------------------------------- the disc's monsters as the reference

_REF = {}


def disc_dir(disc=None):
    d = disc or os.environ.get("WOTM_DISC") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "disc")
    return d if os.path.isdir(os.path.join(d, "RST")) else None


def roster(disc=None):
    """[(name, moves_of, analyze)] of every monster of the disc with a punch table and a model."""
    d = disc_dir(disc)
    if not d:
        return []
    if d in _REF:
        return _REF[d]
    out = []
    for path in sorted(glob.glob(os.path.join(d, "RST", "*.CHR"))):
        name = os.path.splitext(os.path.basename(path))[0]
        ngp = os.path.join(d, "MON", name + "0.NGP")
        if not os.path.isfile(ngp):
            continue
        try:
            m = cv.moves_of(path, ngp, check=False)
            if m.get("legacy"):
                continue
            out.append((name, m, analyze(m, Rig(ngp))))
        except Exception:  # noqa: BLE001 - one unreadable file does not take the reference down
            continue
    _REF[d] = out
    return out


def _rng(vals):
    vals = sorted(v for v in vals if v is not None)
    if not vals:
        return None
    q = lambda f: vals[min(len(vals) - 1, int(f * len(vals)))]
    return {"min": vals[0], "lo": q(0.05), "med": q(0.5), "hi": q(0.95), "max": vals[-1], "n": len(vals)}


CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "move_check_ref.json")   # build/ is not in the repo
_REFS = {}


def _stamp(d):
    """What the reference depends on: this file and the disc's .CHR and models (name, size, time)."""
    files = [os.path.abspath(__file__)] + sorted(glob.glob(os.path.join(d, "RST", "*.CHR"))) + sorted(glob.glob(os.path.join(d, "MON", "*0.NGP")))
    return [[os.path.basename(f), os.path.getsize(f), int(os.path.getmtime(f))] for f in files]


def reference(disc=None):
    """The ranges of the disc's monsters, per button: what findings() compares a monster with. None without the disc.
    Measuring them takes a while, so the result is kept in build/ (outside the repo) until the disc's files or this one change."""
    d = disc_dir(disc)
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
    ref = _measure(d)
    _REFS[d] = ref
    if ref:
        try:
            os.makedirs(os.path.dirname(CACHE), exist_ok=True)
            with open(CACHE, "w", encoding="utf-8") as fh:
                json.dump({"stamp": stamp, "ref": ref}, fh)
        except OSError:
            pass
    return ref


def _measure(disc):
    ros = roster(disc)
    if not ros:
        return None
    ref = {"monsters": [n for n, _m, _a in ros]}
    for btn in ("LIGHT", "HEAVY"):
        mv = [x for _n, _m, a in ros for x in a["moves"] if x["button"] == btn]
        plain = [x for x in mv if not x["knocks"] and not x["stuns"]]
        lk = [x for _n, _m, a in ros for x in a["links"] if x["button"] == btn and x["same"] and not x["knocks"] and not x["stuns"]]
        ref[btn] = {
            "startup": _rng(x["startup"] for x in mv), "opener": _rng(x["startup"] for x in mv if x["pos"] == 1),
            "total": _rng(x["end"] for x in mv), "adv": _rng(x["adv"] for x in plain), "damage": _rng(x["damage"] for x in mv),
            "stun": _rng(x["stun"] for x in plain), "free": _rng(x["free"] for x in lk),
            "reach": _rng(x["sweep"]["reach"] for x in mv if x["sweep"]), "low": _rng(x["sweep"]["low"] for x in mv if x["sweep"]),
            "chain_damage": _rng(c["damage"] for _n, m, _a in ros for c in m["chains"] if c["button"] == btn and not c["weapon"]),
            "last_knocks": [sum(1 for c in monster_lasts(m, a, btn) if c) for _n, m, a in ros],
            "last_count": [len(monster_lasts(m, a, btn)) for _n, m, a in ros],
        }
    ref["getup_path"] = _rng(g["sweep"]["path"] for _n, _m, a in ros for g in a["getup"] if g["sweep"])
    ref["getup_reach"] = _rng(g["sweep"]["reach"] for _n, _m, a in ros for g in a["getup"] if g["sweep"])
    ref["getup_body_high"] = _rng(g.get("body_high") for _n, _m, a in ros for g in a["getup"] if g["sweep"])
    # what a free victim answers with: the usual LIGHT first move of the disc
    ref["answer"] = ref["LIGHT"]["opener"]["med"]
    rt = [routes(a, ref["answer"]) for _n, _m, a in ros]
    ref["pressure_hits"] = _rng(r["pressure"]["hits"] for r in rt if r["pressure"])
    ref["pressure_damage"] = _rng(r["pressure"]["damage"] for r in rt if r["pressure"])
    ref["stun_routes"] = sum(1 for r in rt if r["stun"])
    ref["stun_startup"] = _rng(r["stun"]["startup"] for r in rt if r["stun"])
    ref["reset"] = _rng(v for r in rt for v in r["reset"].values())
    return ref


def monster_lasts(m, a, btn):
    """[knocks down?] of the last move of each same-button chain of that button (the neutral, forward and back chains)."""
    by = {(x["pos"], x["ptype"]): x for x in a["moves"]}
    out = []
    for c in m["chains"]:
        if c["weapon"] or c["button"] != btn:
            continue
        x = by.get((c["length"], c["ptype"]))
        if x:
            out.append(x["knocks"])
    return out


# ---------------------------------------------------------------- findings

def findings(m, a, ref=None):
    """[{sev, code, where, text}] sorted by severity: what is broken (error), off the disc's ranges or rules (warn), or worth a look."""
    out = []

    def add(sev, code, where, text):
        out.append({"sev": sev, "code": code, "where": where, "text": text})

    if not a["frames"]:
        return out
    moves = a["moves"]
    by = {(x["pos"], x["ptype"]): x for x in moves}
    openers = [x for x in moves if x["pos"] == 1 and x["startup"] is not None]
    fastest = min((x["startup"] for x in openers), default=None)
    answer = ref["answer"] if ref else None

    for x in moves:
        w = x["name"]
        if x["startup"] is None:
            add("error", "never_hits", w, "the blend into it (%.1f fields) outlasts its hit window: it never hits" % x["blend"])
            continue
        r = ref[x["button"]] if ref else None
        # 1. an advantage on hit big enough to hit again before the victim is out of the recoil: an endless loop
        if not x["knocks"] and not x["stuns"] and fastest is not None and x["adv"] >= fastest:
            add("error", "loop", w, "on hit the attacker is free %.0f fields before the victim (recoil %.0f, the move ends %.0f after the hit) and its "
                "fastest first move hits in %.0f: it can be repeated forever on a victim that cannot answer" % (
                    x["adv"], x["stun"], x["end"] - x["startup"], fastest))
        elif r and not x["knocks"] and not x["stuns"] and x["adv"] > r["adv"]["hi"] + 0.5:
            add("warn", "adv_high", w, "advantage on hit %+.0f fields; 95%% of the disc's %s moves stay at %+.0f or less" % (x["adv"], x["button"], r["adv"]["hi"]))
        # 2. a last move that leaves the attacker open after it hits
        if r and x["last"] and not x["knocks"] and not x["stuns"] and x["adv"] < r["adv"]["lo"] - 0.5:
            add("warn", "adv_low", w, "it ends the combo and, after hitting, leaves the attacker stuck %.0f fields while the victim is free "
                "(recoil %.0f, the move ends %.0f after the hit); 95%% of the disc's moves leave %.0f or less. Raise stun, speed up the end (recover) or make it knock down" % (
                    -x["adv"], x["stun"], x["end"] - x["startup"], -r["adv"]["lo"]))
        if r:
            if x["startup"] < r["startup"]["lo"] - 0.5:
                add("warn", "startup_low", w, "hits %.0f fields after it starts; 95%% of the disc's %s moves take %.0f or more (median %.0f)" % (
                    x["startup"], x["button"], r["startup"]["lo"], r["startup"]["med"]))
            if x["damage"] > r["damage"]["max"] + 0.01:
                add("warn", "damage_high", w, "damage %g; the disc's %s moves go up to %g" % (x["damage"], x["button"], r["damage"]["max"]))
            if not x["stuns"] and not x["knocks"] and x["stun"] > r["stun"]["max"] + 0.5:
                add("warn", "stun_high", w, "recoil of %.0f fields; the disc's %s moves that neither stun nor knock down go up to %.0f" % (x["stun"], x["button"], r["stun"]["max"]))
            s = x["sweep"]
            if s and s["reach"] < r["reach"]["min"] - 0.5:
                add("warn", "reach_short", w, "the hit sphere gets only %.0f units away from the monster's axis; the shortest %s move of the disc reaches %.0f" % (
                    s["reach"], x["button"], r["reach"]["min"]))
            if s and s["low"] > r["low"]["max"] + 0.5:
                add("warn", "reach_high", w, "the hit sphere never comes below %.0f units of height; on the disc every %s move comes down to %.0f or lower" % (
                    s["low"], x["button"], r["low"]["max"]))
        if x["sweep"] is None and x["pos"] == 1:
            add("warn", "no_sphere", w, "none of its locators is in the model: nothing tests the hit")

    # 3. heavy faster than light, at the same step
    for pos in sorted({x["pos"] for x in moves}):
        li = [x for x in moves if x["pos"] == pos and x["button"] == "LIGHT" and x["startup"] is not None]
        he = [x for x in moves if x["pos"] == pos and x["button"] == "HEAVY" and x["startup"] is not None]
        if not li or not he:
            continue
        lneu, hneu = by.get((pos, 5)), by.get((pos, 0))
        if lneu and hneu and lneu["startup"] is not None and hneu["startup"] is not None:
            if hneu["startup"] < lneu["startup"] - 0.5 and hneu["end"] < lneu["end"] - 0.5 and hneu["damage"] >= lneu["damage"]:
                add("warn", "heavy_fast", hneu["name"], "HEAVY that hits in %.0f fields and is over in %.0f, faster on both than the LIGHT of the same step "
                    "(%.0f and %.0f), with as much damage or more" % (hneu["startup"], hneu["end"], lneu["startup"], lneu["end"]))
            elif hneu["startup"] < lneu["startup"] - 0.5:
                add("info", "heavy_quick", hneu["name"], "HEAVY that hits in %.0f fields, before the LIGHT of the same step (%.0f)" % (hneu["startup"], lneu["startup"]))

    # 4. links: the next move cutting this one, and how long the victim is free between the two hits
    for l in a["links"]:
        w = "%s > %s" % (l["from"], l["to"])
        if l["cut_before"]:
            add("error", "cut", w, "the next move starts at field %.0f, before the hit window of %s opens (field %.0f): it is replaced before it can hit" % (
                l["start"], l["from"], l["hit_a"]))
        elif l["cut_inside"] and l["start"] - max(l["hit_a"], l["blend_a"]) < 3:
            add("warn", "cut_part", w, "the next move starts at field %.0f, when the hit window of %s (fields %.0f-%.0f) has been tested for only %.0f" % (
                l["start"], l["from"], max(l["hit_a"], l["blend_a"]), l["hit_b"], max(0.0, l["start"] - max(l["hit_a"], l["blend_a"]))))
        if l["next_startup"] is None or l["gap"] is None:
            continue
        if l["knocks"] or l["stuns"]:
            continue
        r = ref[l["button"]]["free"] if ref and l["same"] else None
        if r and l["free"] > r["hi"] + 0.5:
            add("warn", "link_open", w, "%.0f fields between the two hits and the recoil lasts %.0f: the victim is free for %.0f before the next hit%s; "
                "95%% of the disc's same-button links leave %.0f or less (median %.0f)" % (
                    l["gap"], l["stun"], l["free"], ", enough to hit first" if l["free"] >= answer else "", r["hi"], r["med"]))

    # 5. what the victim cannot fight back: the held run, a route into a stun, and starting over on a victim still recoiling
    a["lock"] = None
    if answer is not None:
        rt = routes(a, answer)
        a["lock"] = rt["pressure"]
        pr = rt["pressure"]
        if pr and (pr["hits"] > ref["pressure_hits"]["max"] or pr["damage"] > ref["pressure_damage"]["max"] + 0.01):
            add("warn", "pressure", "combo", "%d hits in a row, %g damage, where the victim never has the %.0f fields to hit first (%s); the disc's longest is "
                "%d hits and its strongest %g damage" % (pr["hits"], round(pr["damage"], 2), answer, " > ".join(pr["moves"]),
                                                         ref["pressure_hits"]["max"], ref["pressure_damage"]["max"]))
        st = rt["stun"]
        a["stun_route"] = st
        if st and (not ref["stun_startup"] or st["startup"] < ref["stun_startup"]["min"] - 0.5):
            add("warn", "stun_route", "combo", "a first move that hits in %.0f fields leads, without a gap to hit back, to a move that stuns (%s): the stunned "
                "victim takes the same combo again. %s" % (st["startup"], " > ".join(st["moves"]),
                                                          "On the disc %d of %d monsters have such a route, the quickest from a first move of %.0f fields" % (
                                                              ref["stun_routes"], len(ref["monsters"]), ref["stun_startup"]["min"])
                                                          if ref["stun_startup"] else "No monster of the disc has such a route"))
        for x in moves:
            v = rt["reset"].get(x["name"])
            if v is not None and 0 < v < min(answer, ref["reset"]["lo"]) - 0.5 and x["adv"] < (fastest or 0):
                add("warn", "reset", x["name"], "after it hits, the attacker's fastest first move lands again when the victim has been free for only %.0f "
                    "fields: it can block but not hit first (it needs %.0f); 95%% of the disc's moves leave %.0f or more" % (v, answer, ref["reset"]["lo"]))

    # 6. the heavy combo ends by throwing the victim (every monster of the disc does)
    if ref:
        rule = all(k == n and n > 0 for k, n in zip(ref["HEAVY"]["last_knocks"], ref["HEAVY"]["last_count"]) if n)
        for c in m["chains"]:
            if c["weapon"] or c["button"] != "HEAVY" or c["dir"] != 5:
                continue
            x = by.get((c["length"], c["ptype"]))
            if x and not x["knocks"]:
                n_ok = sum(1 for k, n in zip(ref["HEAVY"]["last_knocks"], ref["HEAVY"]["last_count"]) if n and k == n)
                add("warn", "heavy_end", x["name"], "the last move of the HEAVY combo does not knock the victim down (knockback 0); %s" % (
                    "every monster of the disc ends it with a knockback" if rule else "%d of the %d monsters of the disc end every heavy combo with one" % (
                        n_ok, sum(1 for n in ref["HEAVY"]["last_count"] if n))))

    # 7. the get-up attack (StateGetupAttack: animations 242 and 243, a sphere on the left hand for the whole animation)
    for g in a["getup"]:
        w = "get-up attack, face %s (animation %d)" % (g["face"], g["anim"])
        if not g["frames"]:
            add("error", "getup_missing", w, "the model has no animation %d: the game refuses the get-up attack" % g["anim"])
            continue
        s = g["sweep"]
        if not s:
            add("warn", "getup_no_hand", w, "the model has no left hand locator (0x802), where the get-up attack tests its hit")
            continue
        if ref and ref["getup_path"] and s["path"] < 0.5 * ref["getup_path"]["min"]:
            add("warn", "getup_still", w, "the left hand, which carries the hit, travels %.0f units in the whole animation; on the disc it sweeps %.0f to %.0f. "
                "The animation is probably not an attack" % (s["path"], ref["getup_path"]["min"], ref["getup_path"]["max"]))
        elif ref and ref["getup_reach"] and s["reach"] < 0.8 * ref["getup_reach"]["min"]:
            add("warn", "getup_short", w, "the hit sphere gets %.0f units away from the monster's axis; on the disc %.0f to %.0f" % (
                s["reach"], ref["getup_reach"]["min"], ref["getup_reach"]["max"]))
        if ref and ref["getup_body_high"] and g.get("body_high") is not None and g["body_high"] > 1.5 * ref["getup_body_high"]["max"]:
            add("warn", "getup_flies", w, "the body rises to %.0f units of height during the animation; on the disc it stays under %.0f" % (
                g["body_high"], ref["getup_body_high"]["max"]))

    out.sort(key=lambda f: SEV[f["sev"]])
    return out


def check(m, ngp, disc=None):
    """Everything about one monster: {moves, links, getup, lock, findings, reference (bool)}. m = chr_view.moves_of(..., ngp)."""
    rig = None
    if ngp and m.get("frames"):
        try:
            rig = Rig(ngp)
        except Exception:  # noqa: BLE001
            rig = None
    a = analyze(m, rig)
    ref = reference(disc)
    a["findings"] = findings(m, a, ref)
    a["reference"] = bool(ref)
    return a


# ---------------------------------------------------------------- text

def _f(v, fmt="%4.0f"):
    return fmt % v if v is not None else "   -"


def report(label, m, a, every=False):
    out = ["%s" % label]
    if m.get("note"):
        return out + ["  " + m["note"]]
    if not a["frames"]:
        return out + ["  no model: the frames cannot be measured (pass --ngp)"]
    out.append("  MOVES (fields: startup = first that can hit; adv = on hit, + is the attacker free first)")
    out.append("    %-22s %-5s anim  dmg  startup active  end   adv  recoil  reach  height" % ("move", ""))
    for x in a["moves"]:
        s = x["sweep"]
        fate = "knocks down" if x["knocks"] else "stuns" if x["stuns"] else _f(x["adv"], "%+4.0f")
        out.append("    %-22s %-5s %4d %4.1f  %s   %s  %s  %-11s %3.0f  %s  %s" % (
            x["name"], x["button"], x["anim"], x["damage"], _f(x["startup"]), _f(x["active"]), _f(x["end"]), fate, x["stun"],
            _f(s["reach"] if s else None, "%5.0f"), "%3.0f-%-3.0f" % (s["low"], s["high"]) if s else "  -"))
    lk = [l for l in a["links"] if every or l["same"]]
    if lk:
        out.append("  LINKS%s (gap = fields between the two hits; free = of those, the victim out of the recoil)" % ("" if every else " of the same button"))
        for l in lk:
            fate = "knocked down" if l["knocks"] else "stunned" if l["stuns"] else (
                "combo" if l["free"] is not None and l["free"] <= 0 else "free %s" % _f(l["free"], "%.0f"))
            out.append("    %-22s > %-22s next starts %s  gap %s  recoil %3.0f  %s" % (l["from"], l["to"], _f(l["start"], "%3.0f"), _f(l["gap"], "%3.0f"), l["stun"], fate))
    if a.get("lock"):
        out.append("  LONGEST RUN WITHOUT A GAP TO HIT BACK: %d hits, %g damage: %s" % (a["lock"]["hits"], round(a["lock"]["damage"], 2), " > ".join(a["lock"]["moves"])))
    if a.get("stun_route"):
        out.append("  QUICKEST ROUTE INTO A STUN: first hit at field %.0f: %s" % (a["stun_route"]["startup"], " > ".join(a["stun_route"]["moves"])))
    for g in a["getup"]:
        s = g["sweep"]
        out.append("  GET-UP ATTACK face %-4s animation %d: %s" % (g["face"], g["anim"], "missing" if not g["frames"] else (
            "%.0f frames, hand travels %.0f, reach %.0f, body up to %.0f" % (g["frames"], s["path"], s["reach"], g.get("body_high") or 0) if s else "no hand locator")))
    fs = a.get("findings", [])
    out.append("  FINDINGS: %d error, %d warn, %d info%s" % (sum(f["sev"] == "error" for f in fs), sum(f["sev"] == "warn" for f in fs),
                                                              sum(f["sev"] == "info" for f in fs), "" if a.get("reference") else "  (no disc: nothing was compared with the game's monsters)"))
    for f in fs:
        out.append("    [%s] %s: %s" % (f["sev"], f["where"], f["text"]))
    return out


def reference_text(ref):
    out = ["REFERENCE: %d monsters of the disc (%s)" % (len(ref["monsters"]), ", ".join(ref["monsters"]))]
    for btn in ("LIGHT", "HEAVY"):
        r = ref[btn]
        out.append("  %s" % btn)
        for k, lab in (("opener", "startup of a first move"), ("startup", "startup of any move"), ("total", "fields until the move ends"),
                       ("adv", "advantage on hit"), ("stun", "recoil (no stun, no knockdown)"), ("free", "victim free between same-button hits"),
                       ("damage", "damage"), ("chain_damage", "damage of a same-button combo"), ("reach", "reach of the sphere"),
                       ("low", "lowest edge of the sphere")):
            if r[k]:
                out.append("    %-40s min %6.1f  5%% %6.1f  median %6.1f  95%% %6.1f  max %6.1f  (%d)" % (
                    lab, r[k]["min"], r[k]["lo"], r[k]["med"], r[k]["hi"], r[k]["max"], r[k]["n"]))
        out.append("    last move of each same-button combo knocks down: %s of %s" % (sum(r["last_knocks"]), sum(r["last_count"])))
    for k, lab in (("getup_path", "get-up attack: the hand travels"), ("getup_reach", "get-up attack: reach"), ("getup_body_high", "get-up attack: body height")):
        if ref[k]:
            out.append("  %-42s min %6.1f  median %6.1f  max %6.1f" % (lab, ref[k]["min"], ref[k]["med"], ref[k]["max"]))
    for k, lab in (("pressure_hits", "longest run without a gap to hit back (hits)"), ("pressure_damage", "its damage"),
                   ("stun_startup", "first move of a route into a stun"), ("reset", "victim free before the attacker starts over")):
        if ref[k]:
            out.append("  %-42s min %6.1f  median %6.1f  max %6.1f  (%d)" % (lab, ref[k]["min"], ref[k]["med"], ref[k]["max"], ref[k]["n"]))
    out.append("  usual LIGHT first move of the disc (what a free victim answers with): %.0f fields" % ref["answer"])
    return out


def mod_monsters(mod):
    """[(id, .CHR, .NGP)] of the monsters of a mod folder that bring their own .CHR and model."""
    out = []
    for d in sorted(glob.glob(os.path.join(mod, "monsters", "*", "files"))):
        names = sorted(os.listdir(d))
        ch = [n for n in names if n.upper().endswith(".CHR")]
        ng = [n for n in names if n.upper().endswith(".NGP")]
        if ch and ng:
            out.append((os.path.basename(os.path.dirname(d)), os.path.join(d, ch[0]), os.path.join(d, ng[0])))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("chr", nargs="?")
    ap.add_argument("--ngp", help="the monster's model: without it nothing is measured")
    ap.add_argument("--mod", action="append", default=[], help="a mod folder: checks every monsters/<id>/files/ with a .CHR and a .NGP")
    ap.add_argument("--disc", help="the extracted disc (default: disc/ of the repo, or WOTM_DISC)")
    ap.add_argument("--roster", action="store_true", help="the reference ranges and the findings of the disc's own monsters")
    ap.add_argument("--all", action="store_true", help="list the links between different buttons too")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    todo = []
    if a.chr:
        todo.append((os.path.basename(a.chr), a.chr, a.ngp))
    for mod in a.mod:
        todo += [("%s: %s" % (os.path.basename(os.path.normpath(mod)), cid), c, n) for cid, c, n in mod_monsters(mod)]
    res, lines = {}, []
    if a.roster:
        ref = reference(a.disc)
        if not ref:
            print("error: no disc at %s" % (a.disc or "disc/"))
            return 1
        lines += reference_text(ref)
        res["reference"] = ref
        for name, m, an in roster(a.disc):
            an = dict(an)
            an["findings"], an["reference"] = findings(m, an, ref), True
            lines += [""] + report(name, m, an, a.all)
            res[name] = an
    elif not todo:
        ap.error("a .CHR, --mod or --roster")
    worst = 0
    for label, c, n in todo:
        m = cv.moves_of(c, n, check=False)
        an = check(m, n, a.disc)
        lines += report(label, m, an, a.all) + [""]
        res[label] = an
        worst = max(worst, sum(f["sev"] == "error" for f in an["findings"]))
    print(json.dumps(res, indent=1) if a.json else "\n".join(lines))
    return 1 if worst else 0


if __name__ == "__main__":
    sys.exit(main())
