#!/usr/bin/env python3
"""Animacoes de comer civil para um monstro, direto no .glb do modkit (sem Blender).

O "Export to Blender" do modkit (`python tools/modkit.py monster mesh export my_mod fire_togera`) grava <PREFIX><traje>.glb com o
esqueleto (`bone<N>`) e as animacoes `a<id>`. Esta ferramenta le esse .glb, acha onde estao a cabeca, o braco e a boca pela
geometria do repouso e acrescenta duas animacoes novas (ids livres, ate 299; "New animations" do docs/guides/MODS.md):

  a299_once  pegar:  o corpo se inclina, a mao desce ate o chao a frente e sobe ate a boca; termina segurando
  a298_once  comer:  da boca, a cabeca sobe, morde duas vezes e a mao volta ao repouso

Depois o "Import animations..." do modkit (ou `modkit.py monster anim import my_mod fire_togera LIZARD0.glb`) traz as duas
para o .NGP. O jogo so toca um id quando o codigo pede: native/runtime/wotm_eatmod.inc toca o 299 no primeiro toque e o 298 no
segundo; um modelo sem elas continua comendo, sem animacao.

  python tools/eatanim.py roles LIZARD0.glb [roles.json]      # arvore de ossos com as posicoes de repouso + modelo do roles.json
  python tools/eatanim.py build LIZARD0.glb roles.json OUT.glb [--grab 299] [--eat 298]
  python tools/eatanim.py check OUT.glb roles.json            # mao na boca? mao no chao? (cinematica direta das animacoes gravadas)

roles.json (nomes de nos do .glb; o `roles` escreve um palpite, confira):
  {"head": "bone12",              # osso que a cabeca mexe (a boca vai junto)
   "spine": "bone8",              # osso do tronco que se inclina (opcional)
   "arm": ["bone20","bone21","bone22"],   # ombro ... mao, do pai para o filho
   "mouth": [x, y, z],            # boca no repouso, no espaco do jogo (o locator 0x80B); ou "mouth_node": "st_..."
   "forward": [x, y, 0]}          # opcional: para onde o monstro olha (padrao: do tronco para a boca, no plano do chao)
  opcionais: "jaw" (osso do queixo: abre na mordida), "tip" (no da ponta da mao; o pulso fica parado e a ponta vai ao alvo),
  "pelvis" + "legs" ([[quadril, joelho, tornozelo], ...]) + "crouch" (unidades): a bacia desce no pegar e os pes ficam plantados,
  "lean" (graus do tronco ao pegar, padrao 40), "jaw_open" (25), "head_back" (14). tools/eatanim_togera.json e o do Togera.
Espaco do jogo: o que o no "WotM" guarda (Z para cima).
Os ossos que as animacoes novas nao mexem ficam parados no primeiro quadro do idle (regra do import de animacao nova).
"""
import json
import math
import struct
import sys

import numpy as np

FPS = 60.0


# ---- quaternions (x, y, z, w) -------------------------------------------------------------------------------------
def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return np.array([aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz])


def qnorm(q):
    return q / np.linalg.norm(q)


def qaxis(axis, ang):
    a = np.asarray(axis, float)
    a = a / np.linalg.norm(a)
    s = math.sin(ang / 2)
    return np.array([a[0] * s, a[1] * s, a[2] * s, math.cos(ang / 2)])


def q2m(q):
    x, y, z, w = qnorm(q)
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def m2q(R):
    tr = R[0, 0] + R[1, 1] + R[2, 2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        q = [(R[2, 1] - R[1, 2]) / s, (R[0, 2] - R[2, 0]) / s, (R[1, 0] - R[0, 1]) / s, 0.25 * s]
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = math.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2]) * 2
        q = [0.25 * s, (R[0, 1] + R[1, 0]) / s, (R[0, 2] + R[2, 0]) / s, (R[2, 1] - R[1, 2]) / s]
    elif R[1, 1] > R[2, 2]:
        s = math.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2]) * 2
        q = [(R[0, 1] + R[1, 0]) / s, 0.25 * s, (R[1, 2] + R[2, 1]) / s, (R[0, 2] - R[2, 0]) / s]
    else:
        s = math.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1]) * 2
        q = [(R[0, 2] + R[2, 0]) / s, (R[1, 2] + R[2, 1]) / s, 0.25 * s, (R[1, 0] - R[0, 1]) / s]
    return qnorm(np.array(q))


def slerp(a, b, u):
    d = float(np.dot(a, b))
    if d < 0:
        b, d = -b, -d
    if d > 0.9995:
        return qnorm(a + (b - a) * u)
    th = math.acos(d)
    return qnorm((a * math.sin((1 - u) * th) + b * math.sin(u * th)) / math.sin(th))


def rot_between(a, b):
    """Rotacao (quaternion) que leva o vetor a ao vetor b pelo caminho mais curto."""
    a, b = a / np.linalg.norm(a), b / np.linalg.norm(b)
    c = np.cross(a, b)
    d = float(np.dot(a, b))
    if np.linalg.norm(c) < 1e-9:
        if d > 0:
            return np.array([0, 0, 0, 1.0])
        p = np.cross(a, [1, 0, 0])
        if np.linalg.norm(p) < 1e-6:
            p = np.cross(a, [0, 1, 0])
        return qaxis(p, math.pi)
    return qaxis(c, math.atan2(np.linalg.norm(c), d))


# ---- .glb -----------------------------------------------------------------------------------------------------------
class Glb:
    def __init__(self, path):
        raw = open(path, "rb").read()
        magic, _ver, _total = struct.unpack_from("<III", raw, 0)
        if magic != 0x46546C67:
            raise SystemExit("%s: nao e um .glb" % path)
        off, self.bin = 12, b""
        while off < len(raw):
            ln, typ = struct.unpack_from("<II", raw, off)
            chunk = raw[off + 8: off + 8 + ln]
            if typ == 0x4E4F534A:
                self.j = json.loads(chunk.decode("utf-8"))
            elif typ == 0x004E4942:
                self.bin = bytes(chunk)
            off += 8 + ln
        self.nodes = self.j["nodes"]
        self.parent = {}
        for i, n in enumerate(self.nodes):
            for c in n.get("children", []):
                self.parent[c] = i
        self.name = {n.get("name", "node%d" % i): i for i, n in enumerate(self.nodes)}
        self.root = self.name.get("WotM")

    def read(self, acc):
        a = self.j["accessors"][acc]
        bv = self.j["bufferViews"][a["bufferView"]]
        comps = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[a["type"]]
        base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        return np.frombuffer(self.bin, dtype="<f4", count=a["count"] * comps, offset=base).reshape(a["count"], comps).astype(float)

    def add(self, arr):
        arr = np.ascontiguousarray(arr, dtype="<f4")
        while len(self.bin) % 4:
            self.bin += b"\0"
        self.j["bufferViews"].append({"buffer": 0, "byteOffset": len(self.bin), "byteLength": arr.nbytes})
        self.bin += arr.tobytes()
        comps = arr.shape[1] if arr.ndim == 2 else 1
        acc = {"bufferView": len(self.j["bufferViews"]) - 1, "componentType": 5126, "count": arr.shape[0],
               "type": {1: "SCALAR", 3: "VEC3", 4: "VEC4"}[comps]}
        if comps == 1:
            acc["min"], acc["max"] = [float(arr.min())], [float(arr.max())]
        self.j["accessors"].append(acc)
        return len(self.j["accessors"]) - 1

    def write(self, path):
        self.j["buffers"] = [{"byteLength": len(self.bin)}]
        js = json.dumps(self.j, separators=(",", ":")).encode("utf-8")
        js += b" " * (-len(js) % 4)
        b = self.bin + b"\0" * (-len(self.bin) % 4)
        out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(b))
        out += struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(b), 0x004E4942) + b
        open(path, "wb").write(out)

    def rest(self, i):
        n = self.nodes[i]
        if "matrix" in n:
            raise SystemExit("no %s tem matrix; o export do modkit usa TRS" % n.get("name", i))
        return (np.array(n.get("translation", [0, 0, 0]), float), qnorm(np.array(n.get("rotation", [0, 0, 0, 1]), float)),
                np.array(n.get("scale", [1, 1, 1]), float))


# ---- esqueleto ------------------------------------------------------------------------------------------------------
class Rig:
    """Cinematica direta no espaco do jogo (filhos de "WotM"; o no raiz so vira o jogo para o glTF)."""

    def __init__(self, g):
        self.g = g

    def chain(self, i):
        out = []
        while i is not None and i != self.g.root:
            out.append(i)
            i = self.g.parent.get(i)
        return out[::-1]

    def world(self, i, pose=None):
        M = np.eye(4)
        for k in self.chain(i):
            t, q, s = self.g.rest(k)
            if pose and k in pose:
                q = pose[k]
            if pose and ("T", k) in pose:   # a pose may also move a node (the pelvis, for the crouch)
                t = pose[("T", k)]
            L = np.eye(4)
            L[:3, :3] = q2m(q) * s
            L[:3, 3] = t
            M = M @ L
        return M

    def pos(self, i, pose=None):
        return self.world(i, pose)[:3, 3]

    def rot(self, i, pose=None):
        R = self.world(i, pose)[:3, :3]
        return R / np.linalg.norm(R, axis=0)

    def rotate_world(self, i, pose, R):
        """Gira o osso i em torno da propria origem pela rotacao de mundo R (quaternion); mexe so no local dele."""
        par = self.g.parent.get(i)
        Wp = self.rot(par, pose) if par is not None and par != self.g.root else np.eye(3)
        Wi = self.rot(i, pose)
        pose[i] = m2q(Wp.T @ q2m(R) @ Wi)


def ccd(rig, pose, chain, target, end=None, iters=60):
    """Leva a ponta (`end`, por padrao o ultimo osso) ao alvo girando as juntas da cadeia (CCD); pose: osso -> quaternion local."""
    joints = chain if end is not None else chain[:-1]
    end = chain[-1] if end is None else end
    for _ in range(iters):
        for j in reversed(joints):
            pj, pe = rig.pos(j, pose), rig.pos(end, pose)
            if np.linalg.norm(target - pj) < 1e-6 or np.linalg.norm(pe - pj) < 1e-6:
                continue
            rig.rotate_world(j, pose, rot_between(pe - pj, target - pj))
        if np.linalg.norm(rig.pos(end, pose) - target) < 1e-4:
            break


def guess(g, rig):
    joints = [i for i, n in enumerate(g.nodes) if n.get("name", "").startswith("bone")]
    if not joints:
        raise SystemExit("o .glb nao tem ossos bone<N>")
    pos = {i: rig.pos(i) for i in joints}
    head = max(joints, key=lambda i: pos[i][2])
    top = pos[head][2]
    arm = []
    leaves = [i for i in joints if not any(c in joints for c in g.nodes[i].get("children", []))]
    mid = [i for i in leaves if 0.35 * top < pos[i][2] < 0.85 * top]
    if mid:
        hand = max(mid, key=lambda i: abs(pos[i][0]))   # a mao mais afastada do eixo do corpo
        arm = [i for i in rig.chain(hand) if i in joints][-3:]
    spine = None
    for i in rig.chain(head)[::-1][1:]:
        if i in joints:
            spine = i
            break
    return {"head": g.nodes[head]["name"], "spine": g.nodes[spine]["name"] if spine is not None else None,
            "arm": [g.nodes[i]["name"] for i in arm], "mouth": [round(float(v), 3) for v in pos[head] + np.array([0, 0.3 * top / 2, 0])]}


def cmd_roles(argv):
    g = Glb(argv[0])
    rig = Rig(g)
    print("no  nome  pai  posicao de repouso (x y z, espaco do jogo)")
    for i, n in enumerate(g.nodes):
        if i == g.root:
            continue
        p = rig.pos(i)
        print("%3d  %-18s %3s  %8.2f %8.2f %8.2f" % (i, n.get("name", ""), g.parent.get(i, "-") if g.parent.get(i) != g.root else "-", *p))
    print("animacoes:", ", ".join(a.get("name", "?") for a in g.j.get("animations", [])))
    out = argv[1] if len(argv) > 1 else None
    r = guess(g, rig)
    print("\npalpite (confira cabeca, braco e boca):\n" + json.dumps(r, indent=1))
    if out:
        json.dump(r, open(out, "w"), indent=1)
        print("escrito:", out)


# ---- poses e animacoes ----------------------------------------------------------------------------------------------
def load_roles(g, rig, path):
    r = json.load(open(path))
    need = lambda k: g.name[r[k]] if r.get(k) else None
    head, spine = need("head"), need("spine")
    arm = [g.name[n] for n in r["arm"]]
    if head is None or len(arm) < 2:
        raise SystemExit("roles.json precisa de head e de um arm com 2 ossos ou mais")
    mouth = rig.pos(g.name[r["mouth_node"]]) if r.get("mouth_node") else np.array(r["mouth"], float)
    base = rig.pos(spine if spine is not None else arm[0])
    fwd = np.array(r["forward"], float) if r.get("forward") else mouth - base
    fwd[2] = 0
    if np.linalg.norm(fwd) < 1e-6:
        raise SystemExit("nao deu para achar a frente do monstro; ponha \"forward\" no roles.json")
    return dict(head=head, spine=spine, arm=arm, mouth=mouth, fwd=fwd / np.linalg.norm(fwd), jaw=need("jaw"), tip=need("tip"),
                pelvis=need("pelvis"), legs=[[g.name[n] for n in leg] for leg in r.get("legs", [])], crouch=float(r.get("crouch", 0)),
                lean=float(r.get("lean", 40)), jaw_open=float(r.get("jaw_open", 25)), head_back=float(r.get("head_back", 14)),
                )


def key_poses(g, rig, R):
    """Poses-chave (osso -> quaternion local; ("T", osso) -> translacao) do pegar e do comer."""
    lat = np.cross([0, 0, 1.0], R["fwd"])   # girar +a em torno disto baixa a frente (cabecear)
    arm, head, jaw, spine, pelvis = R["arm"], R["head"], R["jaw"], R["spine"], R["pelvis"]
    end = R["tip"] if R["tip"] is not None else arm[-1]
    drive = arm + [head] + [i for i in (jaw, spine, pelvis) if i is not None] + [n for leg in R["legs"] for n in leg]
    rest = {i: g.rest(i)[1] for i in drive}
    if pelvis is not None:
        rest[("T", pelvis)] = g.rest(pelvis)[0].copy()
    height = max(rig.pos(i)[2] for i in range(len(g.nodes)) if i != g.root)
    ankles = [rig.pos(leg[-1]) for leg in R["legs"]]
    mouth_off = np.linalg.inv(rig.rot(head)) @ (R["mouth"] - rig.pos(head))   # a boca anda junto da cabeca

    def mouth_of(pose):
        return rig.pos(head, pose) + rig.rot(head, pose) @ mouth_off

    def pitch(pose, node, deg):
        rig.rotate_world(node, pose, qaxis(lat, math.radians(deg)))

    def lean(pose, deg):
        if spine is not None:
            pitch(pose, spine, deg)

    def crouch(pose, f):
        """Baixa a bacia em f * crouch e planta os pes onde estavam (os joelhos dobram)."""
        if pelvis is None or not R["crouch"] or not f:
            return
        par = g.parent.get(pelvis)
        Wp = rig.rot(par, pose) if par is not None and par != g.root else np.eye(3)
        pose[("T", pelvis)] = rest[("T", pelvis)] - Wp.T @ np.array([0, 0, R["crouch"] * f])
        for leg, ankle in zip(R["legs"], ankles):
            ccd(rig, pose, leg, ankle)

    def to(pose, target):
        ccd(rig, pose, arm[:-1] if end != arm[-1] else arm, target, end=end)   # com ponta propria, o pulso (ultimo osso) fica como esta

    P = {"rest": dict(rest)}
    p = dict(rest)   # agachado e inclinado, mao no chao a frente (o mais longe que o braco alcanca)
    crouch(p, 1.0)
    lean(p, R["lean"])
    sh = rig.pos(arm[0], p)
    pts = [rig.pos(n) for n in arm + ([end] if end != arm[-1] else [])]
    reach = sum(float(np.linalg.norm(b - a)) for a, b in zip(pts, pts[1:]))
    zg = 0.04 * height
    dz = float(sh[2] - zg)
    if dz >= 0.9 * reach:
        tz, hd = float(sh[2] - 0.9 * reach), 0.3 * reach
    else:
        tz, hd = zg, math.sqrt(0.81 * reach * reach - dz * dz)
    ground = np.array([sh[0] + R["fwd"][0] * hd, sh[1] + R["fwd"][1] * hd, tz])
    to(p, ground)
    P["reach"] = p
    p = dict(rest)   # mao na boca, cabeca um pouco baixa
    crouch(p, 0.25)
    lean(p, 8)
    pitch(p, head, 10)
    to(p, mouth_of(p))
    P["hold"] = p
    q = {i: p[i].copy() for i in p}   # boca aberta: cabeca para tras (e queixo para baixo, se ha um osso dele)
    pitch(q, head, -R["head_back"])
    if jaw is not None:
        pitch(q, jaw, R["jaw_open"])
    to(q, mouth_of(q))
    P["open"] = q
    q = {i: p[i].copy() for i in p}   # mordida: cabeca para baixo, queixo fechado
    pitch(q, head, 18)
    to(q, mouth_of(q))
    P["bite"] = q
    P["mouth_of"] = mouth_of
    P["ground"] = ground
    P["end"] = end
    return P


def timeline(P):
    """Lista de (quadro, pose) de cada animacao; entre as chaves, slerp suave."""
    grab = [(0, "rest"), (22, "reach"), (32, "reach"), (62, "hold"), (80, "hold")]
    eat = [(0, "hold"), (14, "open"), (24, "bite"), (34, "open"), (44, "bite"), (58, "hold"), (84, "rest")]
    return {"grab": [(f, P[n]) for f, n in grab], "eat": [(f, P[n]) for f, n in eat]}


def sample(keys, frame):
    for (f0, a), (f1, b) in zip(keys, keys[1:]):
        if f0 <= frame <= f1:
            u = 0.0 if f1 == f0 else (frame - f0) / (f1 - f0)
            u = u * u * (3 - 2 * u)
            return {i: (a[i] + (b[i] - a[i]) * u if isinstance(i, tuple) else slerp(a[i], b[i], u)) for i in a}
    return dict(keys[-1][1])


def add_animation(g, name, keys, wid):
    n = keys[-1][0] + 1
    times = g.add(np.arange(n, dtype=float) / FPS)
    nodes = sorted(keys[0][1], key=lambda i: (isinstance(i, tuple), i[1] if isinstance(i, tuple) else i))
    frames = [sample(keys, f) for f in range(n)]
    channels, samplers = [], []
    for i in nodes:
        out = g.add(np.array([frames[f][i] for f in range(n)]))
        samplers.append({"input": times, "output": out, "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": i[1] if isinstance(i, tuple) else i,
                                                                  "path": "translation" if isinstance(i, tuple) else "rotation"}})
    g.j.setdefault("animations", []).append({"name": name, "channels": channels, "samplers": samplers, "extras": {"wotmAnim": wid, "frames": n}})


def cmd_build(argv):
    ids = {"--grab": 299, "--eat": 298}
    pos = []
    k = 0
    while k < len(argv):
        if argv[k] in ids:
            ids[argv[k]] = int(argv[k + 1])
            k += 2
        else:
            pos.append(argv[k])
            k += 1
    src, roles, out = pos
    g = Glb(src)
    for a in list(g.j.get("animations", [])):
        if a.get("name", "").split("_")[0] in ("a%03d" % ids["--grab"], "a%03d" % ids["--eat"]):
            raise SystemExit("o .glb ja tem a animacao %s; escolha outro id (--grab/--eat)" % a["name"])
    rig = Rig(g)
    R = load_roles(g, rig, roles)
    P = key_poses(g, rig, R)
    tl = timeline(P)
    add_animation(g, "a%03d_once" % ids["--grab"], tl["grab"], ids["--grab"])
    add_animation(g, "a%03d_once" % ids["--eat"], tl["eat"], ids["--eat"])
    g.write(out)
    print("%s: a%03d_once (pegar, %d quadros) e a%03d_once (comer, %d quadros)" % (out, ids["--grab"], tl["grab"][-1][0] + 1, ids["--eat"],
                                                                                tl["eat"][-1][0] + 1))


def eval_pose(g, name, frame):
    for a in g.j["animations"]:
        if a["name"].startswith(name):
            pose = {}
            for ch in a["channels"]:
                s = a["samplers"][ch["sampler"]]
                v = g.read(s["output"])[min(frame, a["extras"]["frames"] - 1)]
                if ch["target"]["path"] == "translation":
                    pose[("T", ch["target"]["node"])] = v
                else:
                    pose[ch["target"]["node"]] = qnorm(v)
            return pose, a["extras"]["frames"]
    raise SystemExit("sem a animacao %s" % name)


def cmd_check(argv):
    g = Glb(argv[0])
    rig = Rig(g)
    R = load_roles(g, rig, argv[1])
    P = key_poses(g, rig, R)
    hand, ok = P["end"], True
    for anim, frame, what, target in (("a299", 79, "mao na boca ao fim do pegar", None), ("a299", 22, "mao no chao no meio do pegar", P["ground"]),
                                      ("a298", 0, "mao na boca ao comecar a comer", None), ("a298", 83, "de volta ao repouso", rig.pos(hand))):
        pose, n = eval_pose(g, anim, frame)
        full = dict(pose)
        tgt = P["mouth_of"](full) if target is None else target
        d = float(np.linalg.norm(rig.pos(hand, full) - tgt))
        h = max(rig.pos(i)[2] for i in range(len(g.nodes)) if i != g.root)
        good = d < 0.06 * h
        ok &= good
        print("%-5s quadro %2d  %-34s erro %.3f (%.1f%% da altura) %s" % (anim, frame, what, d, 100 * d / h, "ok" if good else "LONGE"))
    sys.exit(0 if ok else 1)


def main(argv):
    if len(argv) < 2 or argv[0] not in ("roles", "build", "check"):
        print(__doc__)
        return 1
    return {"roles": cmd_roles, "build": cmd_build, "check": cmd_check}[argv[0]](argv[1:]) or 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
