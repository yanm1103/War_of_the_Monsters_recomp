"""GTA SA animations (.ifp) on CJ's skeleton, as the model's own clips for tools/retarget.py --own.
  python gta2clips.py <spec.json> <out.json>
spec.json:
  {"raw": dir with head.dff, "anim": GTA's anim dir (ped.ifp, anim.img), "clump": 2, "base": pose.py --list output (lengths),
   "clips": {"<id>": {"src": "ped/IDLE_stance" | "<ifp in anim.img>/<clip>", "fit": "stretch" | "cycles" | "native",
                      "frames": N, "range": [t0, t1], "speed": 1.0, "move": false, "over": "ped/IDLE_stance"}}}
- "range": [t1, t0] (end first) plays the clip backwards (walking back).
- fit: "stretch" (default) to the base animation's length; "cycles" as many whole loops as fit in it, stretched to it;
  "native" the clip's own length (/ speed). "frames" overrides the length; "range" takes part of the clip (seconds).
- The root ("Normal") walks forward in GTA's moving clips; "move": false (default) takes out its horizontal drift (a line
  from the first frame to the last) and starts it where the rest pose has it, so the body stays in place and the game
  moves the monster.
- "over": a partial clip (only the arms, say) takes the bones it lacks from the first frame of that clip.
- "phase": a looping clip starts at that share of it (0.34 = a third in) and wraps around.
- "laid": passed on to retarget.py --own as is (a pose the game lays down itself).
- "turn": degrees about the vertical (z, + = to the left), the whole pose about the origin: GTA's weapon aims turn the body
  sideways (the game then turns the ped so the weapon points at the camera); here the monster faces its target, so the turn
  brings the weapon to the front. [from, to] turns gradually over the clip (into the stance and back out of it);
  [from, to, s0, s1] only between those shares of the clip (0.0, 0.3 = in the first 30%), eased.
- Joints: every frame of the clump with a name in the .dae (spaces as _); the world is the clump's space (the .dae's).
"""
import json
import math
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ifp  # noqa: E402
import rw  # noqa: E402


def mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def quat_col(q):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0.0],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0.0],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0.0],
            [0.0, 0.0, 0.0, 1.0]]


def quat_of(m):
    a, b, c = m[0][0], m[1][1], m[2][2]
    t = a + b + c
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        q = [(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s]
    elif a > b and a > c:
        s = math.sqrt(1.0 + a - b - c) * 2
        q = [0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s]
    elif b > c:
        s = math.sqrt(1.0 + b - a - c) * 2
        q = [(m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s]
    else:
        s = math.sqrt(1.0 + c - a - b) * 2
        q = [(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s]
    n = math.sqrt(sum(x * x for x in q))
    return [x / n for x in q]


def slerp(a, b, u):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0:
        b, d = [-x for x in b], -d
    if d > 0.9995:
        r = [x + u * (y - x) for x, y in zip(a, b)]
    else:
        th = math.acos(d)
        s = math.sin(th)
        r = [(math.sin((1 - u) * th) * x + math.sin(u * th) * y) / s for x, y in zip(a, b)]
    n = math.sqrt(sum(x * x for x in r))
    return [x / n for x in r]


def sample(keys, t):
    """(quaternion, translation or None) of an object's keys at time t (seconds)."""
    if t <= keys[0][0] or len(keys) == 1:
        return keys[0][1], keys[0][2]
    for k in range(1, len(keys)):
        if t <= keys[k][0]:
            t0, q0, p0 = keys[k - 1]
            t1, q1, p1 = keys[k]
            u = (t - t0) / (t1 - t0) if t1 > t0 else 1.0
            p = [a + u * (b - a) for a, b in zip(p0, p1)] if p0 else None
            return slerp(q0, q1, u), p
    return keys[-1][1], keys[-1][2]


_packs = {}


def load_clip(anim_dir, src):
    pack, name = src.split("/", 1)
    if pack not in _packs:
        p = os.path.join(anim_dir, pack + ".ifp")
        if not os.path.exists(p):   # inside anim.img
            img = os.path.join(anim_dir, "anim.img")
            with open(img, "rb") as f:
                hdr = f.read(8)
                n = struct.unpack_from("<I", hdr, 4)[0]
                tab = f.read(32 * n)
                data = None
                for i in range(n):
                    off, ssz, asz = struct.unpack_from("<IHH", tab, 32 * i)
                    nm = tab[32 * i + 8:32 * i + 32].split(b"\0")[0].decode("latin-1")
                    if nm.lower() == pack.lower() + ".ifp":
                        f.seek(off * 2048)
                        data = f.read((ssz or asz) * 2048)
                if data is None:
                    raise SystemExit("no %s.ifp in %s" % (pack, img))
            _packs[pack] = ifp.parse_ifp(data)[0]
        else:
            _packs[pack] = ifp.read_ifp(p)[0]
    c = _packs[pack].get(name.lower())
    if c is None:
        raise SystemExit("no clip %s in %s" % (name, pack))
    return c


def base_lengths(path):
    out = {}
    for ln in open(path):
        m = re.match(r"\s*id\s+(\d+) \(k\s+\d+\) quadros\s+([\d.]+)\.\.\s*([\d.]+)", ln)
        if m and int(m.group(1)) not in out:
            out[int(m.group(1))] = float(m.group(3)) - float(m.group(2))
    return out


def main():
    spec = json.load(open(sys.argv[1], encoding="utf-8"))
    out_path = sys.argv[2]
    sdir = os.path.dirname(os.path.abspath(sys.argv[1]))
    for k in ("raw", "anim", "base"):   # relative to the spec
        if spec.get(k):
            spec[k] = os.path.join(sdir, spec[k])
    frames = rw.read_dff(os.path.join(spec["raw"], "head.dff"))[spec.get("clump", 2)][0]
    by = {f.hanim_id: i for i, f in enumerate(frames) if f.hanim_id is not None}
    names = [(f.name.strip().replace(" ", "_") if i else "cj_frame0") for i, f in enumerate(frames)]
    blen = base_lengths(spec["base"]) if spec.get("base") else {}
    clips = []
    for sid, cs in spec["clips"].items():
        aid = int(re.match(r"\d+", sid).group(0))
        c = load_clip(spec["anim"], cs["src"])
        objs = {by[o["bone"]]: o for o in c["objects"] if o["bone"] in by}
        if cs.get("over"):   # a partial clip: the bones it lacks from another clip's first frame
            for o in load_clip(spec["anim"], cs["over"])["objects"]:
                if o["bone"] in by and by[o["bone"]] not in objs:
                    objs[by[o["bone"]]] = {"name": o["name"], "bone": o["bone"], "root": o["root"], "keys": o["keys"][:1]}
        t0, t1 = cs.get("range", [0.0, ifp.length(c)])
        speed = float(cs.get("speed", 1.0))
        src_len = abs(t1 - t0) / speed * 60.0   # a range from the end to the start plays it backwards
        fit = cs.get("fit", "stretch")
        if "frames" in cs:
            n = int(cs["frames"])
        elif fit == "native" or aid not in blen:
            n = max(2, int(round(src_len)))
        else:
            n = max(2, int(round(blen[aid])))
        cycles = 1
        if fit == "cycles":
            cycles = max(1, int(round(n / src_len)))
        root = objs.get(by.get(0))
        drift = [0.0, 0.0]
        base = [0.0, 0.0]
        if root and root["keys"][0][2] and not cs.get("move"):
            a = sample(root["keys"], t0)[1]
            b = sample(root["keys"], t1)[1]
            drift = [b[0] - a[0], b[1] - a[1]]
            rest = frames[by[0]].pos   # the clip starts with the root where the rest pose has it (a range from the
            base = [a[0] - rest[0], a[1] - rest[1]]   # middle or the end of a moving clip starts metres away)
        jd = {names[i]: [] for i in range(len(frames)) if i}
        for f in range(n):
            u = f / float(n - 1 if fit != "cycles" else n) * cycles   # cycles: the last frame comes back to the first
            u = u - math.floor(u) if fit == "cycles" else u
            if cs.get("phase"):   # a loop that starts at that share of the clip (and still closes on itself)
                u = (u + float(cs["phase"])) % 1.0
            t = t0 + u * (t1 - t0)
            W = []
            for i, fr in enumerate(frames):
                o = objs.get(i)
                if o:
                    q, p = sample(o["keys"], t)
                    L = quat_col(q)
                    p = list(p) if p else list(fr.pos)
                    if o is root and p:
                        p[0] -= base[0] + drift[0] * u
                        p[1] -= base[1] + drift[1] * u
                    for r in range(3):
                        L[r][3] = p[r]
                else:
                    L = fr.local()
                W.append(L if fr.parent < 0 else mul(W[fr.parent], L))
            if cs.get("turn"):
                tr = cs["turn"] if isinstance(cs["turn"], list) else [cs["turn"], cs["turn"]]
                s0, s1 = (float(tr[2]), float(tr[3])) if len(tr) > 3 else (0.0, 1.0)
                k = min(1.0, max(0.0, (f / float(max(1, n - 1)) - s0) / max(1e-6, s1 - s0)))
                k = k * k * (3 - 2 * k)
                a = math.radians(float(tr[0]) + (float(tr[1]) - float(tr[0])) * k)
                Rz = [[math.cos(a), -math.sin(a), 0, 0], [math.sin(a), math.cos(a), 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
                W = [mul(Rz, w) for w in W]
            for i in range(1, len(frames)):
                jd[names[i]].append([round(v, 6) for v in quat_of(W[i]) + [W[i][0][3], W[i][1][3], W[i][2][3]]])
        nm = "a%03d" % aid + (sid[len(str(aid)):] if not sid.isdigit() else "")
        c = {"name": nm, "frames": n, "joints": jd}
        if "laid" in cs:
            c["laid"] = cs["laid"]
        clips.append(c)
        print("%-6s <- %-24s %3d frames (%s, source %.0f frames%s)" % (nm, cs["src"], n, fit, src_len,
                                                                     ", %d cycles" % cycles if fit == "cycles" else ""))
    json.dump({"clips": clips}, open(out_path, "w"), separators=(",", ":"))
    print("wrote", out_path)


if __name__ == "__main__":
    main()
