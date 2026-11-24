#!/usr/bin/env python3
"""Writes a custom monster's whole punch table (`.CHR`, key 0xFCC) from a JSON move list.

  python tools/chr_moves.py <moves.json> <file.CHR> [--ngp <model.NGP>] [--out <new.CHR>]

The JSON names each move once and then the chain of each attack type (combo positions 1, 2, ...):

  {"maxChain": 5,
   "moves": {"jab": {"anim": 10, "hit": [0.07, 0.41], "damage": 4.5, "locators": ["hand_r"], ...}, ...},
   "chains": {"square": ["jab", "jab_l", ...], "fwd_square": [...], "back_square": [...],
              "triangle": [...], "fwd_triangle": [...], "back_triangle": [...]}}

Top-level "nextDelay" and "firstDelay" (ms) are the defaults of every move's blend into the next move and into the first move of a
combo (without "firstDelay" the file's slot keeps its own, 96 ms for the disc's; a move's own "nextDelay"/"firstDelay" wins).

Position p of a type plays chain[p], or the chain's last move once the chain is shorter. Every move of positions
1..maxChain-1 lets the combo go on with any type; the moves at position maxChain end it. A type listed in "stop"
(e.g. "stop": ["triangle"]) has no positions past its chain: the combo cannot go on with it there. Slots past maxChain keep
the file's.

Per move (only "anim" and "hit" are required):
- "anim": animation id of the model; "frames": its length at 60 per second (read from --ngp when omitted);
- "hit": [start, end], fraction of the animation that tests the hit; "counter": the counterattack window (default: from
  0.15 before the hit to its middle);
- "speed": animation speed (1 = the animation's own pace); "recover": [fraction, speed], from that point the game blends
  into the idle over the rest of the animation, played at that speed (default [1, 1]: the whole animation, then the idle);
- "chainAt": fraction of the animation where a combo press already made starts the next move (default: the hit's end);
- "damage", "stamina" (cost), "radius" (hit sphere, 0 = the default), "stun" (> 100 stuns), "pause" (hit pause, ms),
  "knockback": [out, up] (nonzero knocks down), "nextDelay" (ms before the next move of the combo, which is also the blend into it: the hit is not tested while it blends; default 60, or the file's top-level "nextDelay");
- "locators": up to two of hand_l, hand_r, foot_l, foot_r, mouth, head, tail_mid, tail_tip, shoulder, or a number.

The state lasts the whole animation (frames / speed, with "recover" applied): the combo input stays open from the hit's
start to that end. Every other field of a slot comes from the file's slot at position 1, type square. Layout and field
roles: docs/FORMATS.md ("Character"). Only for a custom monster's own `.CHR` (`files/`, MODS.md), never the disc's.
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chr_dump  # noqa: E402

TYPES = {"triangle": 0, "fwd_triangle": 1, "back_triangle": 2, "fwd_square": 3, "back_square": 4, "square": 5}
LOCATORS = {"hand_l": 0x802, "hand_r": 0x803, "foot_l": 0x804, "foot_r": 0x805, "mouth": 0x80B, "tail_mid": 0x816,
            "tail_tip": 0x817, "head": 0x818, "shoulder": 0x819}
SLOT = 0xA0   # one PunchConfig in the file, from +0x10 of the address below
TEMPLATE = (0, 5)


def slot_at(key_off, pos, typ):
    return key_off + 0x10 + pos * 0x3C0 + typ * SLOT


def anim_frames(ngp, anim):
    import pose
    a = pose.Model(ngp).find_anim(anim)
    if a is None:
        raise SystemExit("error: %s has no animation %d" % (ngp, anim))
    return a[3] - a[2]


def locator(v):
    if isinstance(v, int):
        return v
    if v not in LOCATORS:
        raise SystemExit("error: unknown locator %r (%s)" % (v, ", ".join(LOCATORS)))
    return LOCATORS[v]


def write_move(d, b, mv, frames, allowed, default_delay=60.0, first_delay=None):
    pf = lambda o, v: struct.pack_into("<f", d, b + o, float(v))
    pi = lambda o, v: struct.pack_into("<i", d, b + o, int(v))
    h0, h1 = mv["hit"]
    speed = float(mv.get("speed", 1.0))
    rec_at, rec_speed = mv.get("recover", [1.0, 1.0])
    locs = [locator(x) for x in mv.get("locators", [])][:2]
    locs += [-1] * (2 - len(locs))
    pi(0x10, locs[0])
    pi(0x14, locs[1])
    pf(0x18, mv.get("stamina", 5))
    pf(0x20, mv.get("damage", 4))
    pf(0x24, speed)
    pi(0x28, mv["anim"])
    pf(0x2C, h0)
    pf(0x30, h1)
    c0, c1 = mv.get("counter", [max(0.0, h0 - 0.15), (h0 + h1) / 2])
    pf(0x34, c0)
    pf(0x38, c1)
    pf(0x3C, rec_at)   # wind-down: from this fraction the rest of the animation plays at +0x40 (1 = none)
    pf(0x40, rec_speed)
    total = frames * min(1.0, rec_at) / speed + frames * max(0.0, 1.0 - rec_at) / rec_speed
    pi(0x48, round(h0 * frames / speed))   # combo input window, fields after the start (and the hit pause)
    pi(0x4C, round(total))                 # ... and, with no wind-down, the end of the state
    pf(0x50, mv.get("chainAt", h1))
    delay = float(mv.get("nextDelay", default_delay))
    first = mv.get("firstDelay", first_delay)   # the blend into the first move of a combo (+0x6C); None keeps the template's
    if first is not None:
        pf(0x6C, first)
    for k in range(6):
        pf(0x54 + 4 * k, delay if allowed[k] else 0.0)
    kb = mv.get("knockback", [0, 0])
    pf(0x8C, kb[0])
    pf(0x90, kb[1])
    pf(0x94, mv.get("stun", 10))
    pf(0x98, mv.get("pause", 40))
    pf(0xA8, mv.get("radius", 0))
    return total


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("moves")
    ap.add_argument("chr")
    ap.add_argument("--ngp", help="model whose animation lengths fill in the moves without 'frames'")
    ap.add_argument("--out", help="write here instead of over the .CHR")
    a = ap.parse_args(argv)
    spec = json.load(open(a.moves, encoding="utf-8"))
    d = bytearray(open(a.chr, "rb").read())
    keys = {k: (o, s) for k, o, s in chr_dump.read_keys(d)}
    if chr_dump.PUNCH_KEY not in keys or keys[chr_dump.PUNCH_KEY][1] != chr_dump.PUNCH_KEY_SIZE:
        print("error: %s has no punch table (key 0x%X)" % (a.chr, chr_dump.PUNCH_KEY))
        return 1
    ko = keys[chr_dump.PUNCH_KEY][0]
    tb = slot_at(ko, *TEMPLATE)
    template = bytes(d[tb + 0x10:tb + 0x10 + SLOT])
    moves = spec["moves"]
    n = int(spec.get("maxChain", 5))
    if not 1 <= n <= chr_dump.TIERS:
        print("error: maxChain 1..%d" % chr_dump.TIERS)
        return 1
    frames = {}
    for name, mv in moves.items():
        if "frames" in mv:
            frames[name] = float(mv["frames"])
        elif a.ngp:
            frames[name] = anim_frames(a.ngp, mv["anim"])
        else:
            print("error: move %s has no 'frames' (pass --ngp)" % name)
            return 1
    stop = set(spec.get("stop", []))
    for tname in list(spec["chains"]) + list(stop):
        if tname not in TYPES:
            print("error: unknown type %r (%s)" % (tname, ", ".join(TYPES)))
            return 1
    length = {TYPES[t]: (len(c) if t in stop else n) for t, c in spec["chains"].items()}
    for tname, chain in spec["chains"].items():
        typ = TYPES[tname]
        for pos in range(min(n, length[typ])):
            name = chain[min(pos, len(chain) - 1)]
            if name not in moves:
                print("error: chain %s names %r, not in moves" % (tname, name))
                return 1
            b = slot_at(ko, pos, typ)
            d[b + 0x10:b + 0x10 + SLOT] = template
            allowed = [pos + 1 < min(n, length.get(k, 0)) for k in range(6)]
            total = write_move(d, b, moves[name], frames[name], allowed, float(spec.get("nextDelay", 60)), spec.get("firstDelay"))
            print("pos %d %-13s %-14s anim %3d  %3.0f frames, %3.0f fields  hit %2.0f-%3.0f%%" % (
                pos + 1, tname, name, moves[name]["anim"], frames[name], total, moves[name]["hit"][0] * 100,
                moves[name]["hit"][1] * 100))
    with open(a.out or a.chr, "wb") as f:
        f.write(d)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
