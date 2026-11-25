#!/usr/bin/env python3
"""Compares the moves measured in a fight with what move_check predicts from the .CHR and the model.

    python tools/move_measure.py <monster>.moves.csv --mod "mods/<mod>" [--monster <id>] [--slot 0] [--json]
    python tools/move_measure.py <file>.moves.csv --chr <file.CHR> --ngp <model.NGP>

The CSV is the runtime's move telemetry (WOTM_HITLOG with the training mode on; `tools/smoke.ps1 -MoveLog` writes one per
monster, the mod monster in slot 0): per attack state its frames, the first frame that tested a hit, the frames that tested,
the victim and the frame of the hit; per reaction of a victim its frames; per damage the health taken.

Per punch of the .CHR that was seen in the fight: the median of the measured startup, active frames, duration (until the
move ends), the victim's recoil (only of a hit that was not followed by the next step, which would extend it) and the health
taken per hit, next to move_check's number; per combo link, the earliest the next step started.
move_check does not model everything (the hit pause, how the game counts the first frame of a state), so each number has a
systematic difference: the median of measured minus predicted over the monster's moves, printed as BIAS. A finding is a move
that is off from that bias by more than the tolerance (3 frames or 20%): that move does not do in the game what its numbers
say. The bias itself is what calibrates move_check. The other attack states (get-up attack,
stomp, grab...) have no prediction: the table says how often each one ran, tested a hit, hit and took health.

Exit code 1 with a warning. A move the AI never used is not judged: the listing says which were not seen.
"""
import argparse
import csv
import json
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chr_view as cv    # noqa: E402
import move_check as mc  # noqa: E402

PUNCH = "Punch"
TOL_FRAMES, TOL_RATIO = 3.0, 0.20


def load(path):
    """{moves, reacts, dmgs}: the rows of the CSV as dicts of numbers (frames of the game)."""
    out = {"moves": [], "reacts": [], "dmgs": []}
    with open(path, newline="", encoding="utf-8", errors="replace") as fh:
        for r in csv.reader(fh):
            try:
                if r and r[0] == "move" and len(r) >= 12:
                    out["moves"].append({"start": int(r[1]), "end": int(r[2]), "slot": int(r[3]), "state": r[4], "step": int(r[5]), "type": int(r[6]),
                                         "anim": int(r[7]), "startup": int(r[8]), "active": int(r[9]), "victim": int(r[10]), "hit": int(r[11])})
                elif r and r[0] == "react" and len(r) >= 6:
                    out["reacts"].append({"start": int(r[1]), "end": int(r[2]), "slot": int(r[3]), "state": r[4], "blocked": int(r[5])})
                elif r and r[0] == "dmg" and len(r) >= 7:
                    out["dmgs"].append({"frame": int(r[1]), "victim": int(r[2]), "attacker": int(r[3]), "applied": float(r[4]), "asked": float(r[5]),
                                        "source": int(r[6])})
            except ValueError:
                continue   # the last line of a killed process can be cut
    return out


def med(v):
    return statistics.median(v) if v else None


def off(measured, predicted, bias=0.0):
    """The difference to the prediction plus the systematic bias, when it is above the tolerance; else None."""
    if measured is None or predicted is None:
        return None
    d = measured - predicted - bias
    return d if abs(d) > max(TOL_FRAMES, TOL_RATIO * abs(predicted)) else None


def measure(data, m, rig, slot=0, getup=None):
    """{rows, states, unseen, findings, step_base} for the monster of `slot`. m = chr_view.moves_of(chr, ngp)."""
    a = mc.analyze(m, rig, getup)
    pred = {(x["pos"], x["ptype"]): x for x in a["moves"]}
    link = {(tuple(l["from_key"]), tuple(l["to_key"])): l for l in a["links"]}
    anim_of = {(r["pos"], r["ptype"]): r["anim"] for r in m.get("punches", [])}
    mine = sorted((x for x in data["moves"] if x["slot"] == slot), key=lambda x: x["start"])
    punches = [x for x in mine if x["state"] == PUNCH]
    # the game's step index against the .CHR's position: the offset under which the animations agree
    base = max((0, 1), key=lambda b: sum(anim_of.get((x["step"] + b, x["type"])) == x["anim"] for x in punches)) if punches else 1
    reacts = sorted(data["reacts"], key=lambda r: r["start"])

    def reaction(victim, frame):
        return next((r for r in reacts if r["slot"] == victim and frame - 1 <= r["start"] <= frame + 3), None)

    def damage(x):
        return sum(d["applied"] for d in data["dmgs"] if d["attacker"] == slot and x["start"] <= d["frame"] <= x["end"] + 1)

    seen = {}
    for i, x in enumerate(mine):
        if x["state"] != PUNCH:
            continue
        key = (x["step"] + base, x["type"])
        s = seen.setdefault(key, {"n": 0, "startup": [], "active": [], "end": [], "chain": {}, "recoil": [], "damage": [], "hits": 0, "blocked": 0,
                                  "nodmg": 0, "anims": set()})
        s["n"] += 1
        s["anims"].add(x["anim"])
        if x["startup"] > 0:
            s["startup"].append(x["startup"])
            s["active"].append(x["active"])
        nxt = mine[i + 1] if i + 1 < len(mine) else None
        chained = bool(nxt and nxt["state"] == PUNCH and nxt["start"] == x["end"] and nxt["step"] == x["step"] + 1)
        if chained:
            s["chain"].setdefault((nxt["step"] + base, nxt["type"]), []).append(x["end"] - x["start"])
        else:
            s["end"].append(x["end"] - x["start"])
        if x["victim"] >= 0 and x["hit"] > 0:
            s["hits"] += 1
            r = reaction(x["victim"], x["start"] + x["hit"] - 1)
            dmg = damage(x)
            if r and r["blocked"]:
                s["blocked"] += 1
            else:
                if r and not chained:
                    s["recoil"].append(r["end"] - r["start"])
                if dmg > 0:
                    s["damage"].append(dmg)
                else:
                    s["nodmg"] += 1

    rows, found = [], []

    def add(sev, code, key, title, text):
        found.append({"sev": sev, "code": code, "keys": [list(key)] if key else [], "where": pred[key]["name"] if key in pred else "", "title": title, "text": text})

    for key in sorted(seen):
        s, p = seen[key], pred.get(key)
        name = p["name"] if p else "step %d type %d" % key
        row = {"key": list(key), "name": name, "n": s["n"], "hits": s["hits"], "blocked": s["blocked"],
               "startup": med(s["startup"]), "active": med(s["active"]), "end": med(s["end"]), "recoil": med(s["recoil"]), "damage": med(s["damage"]),
               "p_startup": p["startup"] if p else None, "p_active": p["active"] if p else None, "p_end": p["end"] if p else None,
               "p_recoil": p["stun"] if p and not p["knocks"] and not p["stuns"] else None, "p_damage": p["damage"] if p else None, "chains": []}
        for nk, v in sorted(s["chain"].items()):
            l = link.get((key, nk))
            row["chains"].append({"to": pred[nk]["name"] if nk in pred else "step %d type %d" % nk, "n": len(v), "start": min(v), "p_start": l["start"] if l else None})
        rows.append(row)
    fields = (("startup", "startup", "p_startup", "frames until the first hit test"), ("active frames", "active", "p_active", "frames testing the hit"),
              ("duration", "end", "p_end", "frames until the move ends"), ("recoil", "recoil", "p_recoil", "frames of the victim's recoil"))
    bias = {k: med([r[k] - r[pk] for r in rows if r[k] is not None and r[pk] is not None]) or 0.0 for _w, k, pk, _u in fields}
    bias["chain"] = med([c["start"] - c["p_start"] for r in rows for c in r["chains"] if c["p_start"] is not None]) or 0.0
    for row in rows:
        key = tuple(row["key"])
        s, p, name = seen[key], pred.get(key), row["name"]
        if not p:
            add("warn", "not_in_chr", None, "A punch the .CHR does not explain", "The game ran step %d type %d (animation %s) %d time(s) and move_check has no real move "
                "there." % (key[0], key[1], ", ".join(str(v) for v in sorted(s["anims"])), s["n"]))
            continue
        if anim_of.get(key) not in s["anims"]:
            add("warn", "other_anim", key, "Another animation in the game", "The .CHR gives this move animation %s and the game played %s." % (
                anim_of.get(key), ", ".join(str(v) for v in sorted(s["anims"]))))
        for what, k, pk, unit in fields:
            d = off(row[k], row[pk], bias[k])
            if d is not None:
                add("warn", "measured_" + k, key, "%s: measured %.0f, predicted %.0f" % (what.capitalize(), row[k], row[pk]),
                    "%s: the fight measured %.0f %s (median of %d), move_check predicts %.0f and the monster's other moves differ by %+.0f." % (
                        name, row[k], unit, len(s[k]), row[pk], bias[k]))
        for c in row["chains"]:
            d = off(c["start"], c["p_start"], bias["chain"])
            if d is not None:
                add("warn", "measured_chain", key, "Next step starts at %.0f, predicted %.0f" % (c["start"], c["p_start"]),
                    "%s > %s: in the fight the next step started at the earliest %.0f frames into the move (%d seen), move_check predicts %.0f and "
                    "the monster's other links differ by %+.0f." % (name, c["to"], c["start"], c["n"], c["p_start"], bias["chain"]))
        if s["startup"] == [] and s["n"] >= 2:
            add("warn", "never_tested", key, "Never tested a hit", "%s ran %d times and the game never tested its hit sphere: the hit window does not happen "
                "(the move is cut before it, or the window is outside the animation)." % (name, s["n"]))
        if s["nodmg"] and not s["damage"]:
            add("warn", "hit_no_damage", key, "Hits and takes no health", "%s touched the victim %d time(s), not blocked, and no health was taken." % (name, s["nodmg"]))

    # the other attack states: no prediction, only what happened
    states = {}
    for x in mine:
        if x["state"] == PUNCH:
            continue
        st = states.setdefault(x["state"], {"n": 0, "tested": 0, "hits": 0, "damage": 0.0, "anims": set(), "startup": [], "frames": []})
        st["n"] += 1
        st["tested"] += x["startup"] > 0
        st["hits"] += x["victim"] >= 0
        st["damage"] += damage(x)
        st["anims"].add(x["anim"])
        st["frames"].append(x["end"] - x["start"])
        if x["startup"] > 0:
            st["startup"].append(x["startup"])
    for name, st in sorted(states.items()):
        st["anims"] = sorted(st["anims"])
        st["startup"], st["frames"] = med(st["startup"]), med(st["frames"])
        if st["n"] >= 2 and st["tested"] and not st["hits"]:
            found.append({"sev": "info", "code": "state_never_hit", "keys": [], "where": name, "title": "%s never hit" % name,
                          "text": "%s ran %d times, tested a hit in %d and never touched a victim (the victim can simply have been out of reach)." % (
                              name, st["n"], st["tested"])})
        if st["n"] >= 2 and not st["tested"] and name in ("GetupAttack", "StompAttack"):
            found.append({"sev": "warn", "code": "state_never_tested", "keys": [], "where": name, "title": "%s never tested a hit" % name,
                          "text": "%s ran %d times and the game never tested a hit in it." % (name, st["n"])})
        if st["hits"] and st["damage"] <= 0:
            found.append({"sev": "warn", "code": "state_no_damage", "keys": [], "where": name, "title": "%s hits and takes no health" % name,
                          "text": "%s touched a victim %d time(s) and no health was taken." % (name, st["hits"])})
    unseen = [x["name"] for k, x in sorted(pred.items()) if k not in seen]
    return {"rows": rows, "states": states, "unseen": unseen, "findings": found, "step_base": base, "moves": len(mine), "bias": bias}


def _n(v, fmt="%5.0f"):
    return fmt % v if v is not None else "    -"


def report(label, res):
    out = ["%s: %d attack states of the monster in the log" % (label, res["moves"]),
           "  PUNCHES (measured / predicted, frames; n = times seen)",
           "    %-24s    n hits blk   startup      active     duration     recoil     damage" % "move"]
    for r in res["rows"]:
        out.append("    %-24s %4d %4d %3d  %s/%s  %s/%s  %s/%s  %s/%s  %s/%s" % (
            r["name"], r["n"], r["hits"], r["blocked"], _n(r["startup"]), _n(r["p_startup"]), _n(r["active"]), _n(r["p_active"]),
            _n(r["end"]), _n(r["p_end"]), _n(r["recoil"]), _n(r["p_recoil"]), _n(r["damage"], "%5.1f"), _n(r["p_damage"], "%5.1f")))
        for c in r["chains"]:
            out.append("        > %-22s %4d   next step starts %s/%s" % (c["to"], c["n"], _n(c["start"]), _n(c["p_start"])))
    b = res["bias"]
    out.append("  BIAS (median of measured - predicted over these moves): startup %+.0f, active %+.0f, duration %+.0f, recoil %+.0f, next step %+.0f" % (
        b["startup"], b["active"], b["end"], b["recoil"], b["chain"]))
    if res["states"]:
        out.append("  OTHER ATTACK STATES")
        out.append("    %-20s    n tested hits  damage  startup frames  animations" % "state")
        for name, st in sorted(res["states"].items()):
            out.append("    %-20s %4d %6d %4d %7.1f    %s  %s  %s" % (name, st["n"], st["tested"], st["hits"], st["damage"], _n(st["startup"]), _n(st["frames"]),
                                                                ", ".join(str(v) for v in st["anims"] if v >= 0)))
    if res["unseen"]:
        out.append("  NOT SEEN IN THIS FIGHT (%d): %s" % (len(res["unseen"]), ", ".join(res["unseen"])))
    fs = res["findings"]
    out.append("  FINDINGS: %d warn, %d info" % (sum(f["sev"] == "warn" for f in fs), sum(f["sev"] == "info" for f in fs)))
    for f in fs:
        out += ["    [%s] %s%s" % (f["sev"], (f["where"] + ": ") if f["where"] else "", f["title"]), "        %s" % f["text"]]
    return out


def measure_file(path, chr_path, ngp, slot=0):
    m = cv.moves_of(chr_path, ngp, check=False)
    return measure(load(path), m, mc.Rig(ngp), slot, mc.getup_locators(ngp))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--mod", help="the mod folder of the monster (its monsters/<id>/files/ has the .CHR and the model)")
    ap.add_argument("--monster", help="the monster id inside the mod (default: the CSV's name before the first dot)")
    ap.add_argument("--chr")
    ap.add_argument("--ngp")
    ap.add_argument("--slot", type=int, default=0, help="the monster's slot in the fight (0 = the first of WOTM_MATCH)")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    chr_path, ngp = a.chr, a.ngp
    label = os.path.basename(a.csv)
    if a.mod:
        cid = a.monster or os.path.basename(a.csv).split(".")[0]
        hit = [(c, n) for i, c, n in mc.mod_monsters(a.mod) if i == cid]
        if not hit:
            ap.error("no monster %s with its own .CHR and model in %s" % (cid, a.mod))
        chr_path, ngp = hit[0]
        label = cid
    if not chr_path or not ngp:
        ap.error("--mod, or --chr and --ngp")
    res = measure_file(a.csv, chr_path, ngp, a.slot)
    print(json.dumps(res, indent=1) if a.json else "\n".join(report(label, res)))
    return 1 if any(f["sev"] == "warn" for f in res["findings"]) else 0


if __name__ == "__main__":
    sys.exit(main())
