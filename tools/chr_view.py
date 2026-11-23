#!/usr/bin/env python3
"""Os golpes de um monstro (RST/<MONSTRO>.CHR) como o MOVE LIST e a tabela de socos do modo treino os mostram, lidos do arquivo
e nao da RAM. Uma fonte so para o kit (aba Moves), para `modkit.py moves` e para conferir um mod sem abrir o jogo.

    python tools/chr_view.py disc/RST/MONKEY.CHR [--ngp disc/MON/MONKEY0.NGP] [--json]

Com --ngp, a janela de cada golpe vira quadros (60 por segundo, como o jogo conta): a duracao da animacao vem do modelo.
Os numeros sao os do .CHR; quadros e "ativo" sao uma estimativa (janela x quadros da animacao / velocidade), sem a pausa do
acerto nem a mistura com o idle depois do golpe. Layout das chaves: docs/FORMATS.md ("Character") e chr_dump.py.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chr_dump as cd  # noqa: E402

FPS = 60
# tipo do golpe (o indice da config no arquivo) -> (nome curto, botao leve/pesado, direcao): o curMappedAction do jogo
# (FORMATS.md: 5 quadrado, 3 frente + quadrado, 4 tras + quadrado, 0 triangulo, 1 frente + triangulo, 2 tras + triangulo)
TYPE_INFO = {
    5: ("square", "LIGHT", 5),
    3: ("fwd_square", "LIGHT", 8),
    4: ("back_square", "LIGHT", 2),
    0: ("triangle", "HEAVY", 5),
    1: ("fwd_triangle", "HEAVY", 8),
    2: ("back_triangle", "HEAVY", 2),
}
TYPE_ORDER = [5, 3, 4, 0, 1, 2]   # a ordem do painel do treino: quadrado, frente/tras + quadrado, triangulo, frente/tras + triangulo
# numero da chave 0xFA1..: nome do especial (o do MOVE LIST), so para as chaves que o chr_dump sabe ler
SPECIAL_NAMES = {0xFA9: "SONIC ROAR", 0xFAA: "BUG ATTACK", 0xFAD: "FIRE BREATH", 0xFBC: "ROCK BOMB", 0xFC4: "LIGHTNING",
                 0xFC5: "GRAPPLE HOOK", 0xFC7: "AIR STRIKE", 0xFC8: "LAVA BLAST", 0xFCF: "MISSILE", 0xFE1: "CROWD CONTROL",
                 0xFE0: "MISSILE", 0xFDF: "CROWD CONTROL"}


# os rotulos dos campos dos especiais vem do chr_dump (em portugues); a aba do kit e em ingles
EN = {
    'alcance': 'range', 'alcance (Manhattan)': 'range (Manhattan)', 'alcance cresce por quadro': 'range growth per frame',
    'alcance maximo': 'maximum range', 'amplitude do arco': 'arc amplitude', 'arco (+0x30)': 'arc',
    'blend do laco': 'loop blend (ms)', 'blend do winddown': 'wind-down blend (ms)', 'blend do windup': 'wind-up blend (ms)',
    'campos em chamas': 'fields on fire', 'cone (graus, abertura total)': 'cone (degrees, full opening)',
    'dano': 'damage', 'dano ao cenario (acumulado)': 'damage to scenery (accumulated)', 'dano do fogo': 'fire damage',
    'dano do fogo (so Raptros)': 'fire damage (Raptros only)', 'dano por alvo': 'damage per target', 'dano por bola': 'damage per ball',
    'dano por tiro dos discos': 'damage per saucer shot', 'dano/s': 'damage per second', 'dano/s a monstro': 'damage per second to monsters',
    'dano/s ao cenario': 'damage per second to scenery', 'duracao do sopro (quadros)': 'breath length (frames)',
    'duracao dos discos (campos)': 'saucers last (fields)', 'empurrao no chao': 'push on the ground', 'homing': 'homing',
    'homing horizontal (o jogo le float)': 'horizontal homing', 'homing vertical (o jogo le float)': 'vertical homing',
    'knockback para cima': 'knockback up', 'knockback para cima (fim)': 'knockback up (end)', 'knockback para fora': 'knockback out',
    'knockback para fora (fim)': 'knockback out (end)', 'laco (quadros)': 'loop (frames)', 'lanca o bug (fracao do laco)': 'throws the bug (fraction of the loop)',
    'lentidao da vitima': 'slows the victim', 'numero de bolas': 'number of balls', 'onda sai (fracao do laco)': 'wave starts (fraction of the loop)',
    'pavio da cabeca (s)': 'head fuse (s)', 'quadros em chamas (so Raptros)': 'frames on fire (Raptros only)',
    'quadros minimos na mira': 'minimum frames aiming', 'solta o bug (fracao do windup)': 'releases the bug (fraction of the wind-up)',
    'taxa da velocidade da ponta': 'tip speed rate', 'taxa de puxar': 'pull rate', 'taxa de puxar (altura)': 'pull rate (height)',
    'taxa do homing': 'homing rate', 'trava da reticula (campos)': 'reticle lock (fields)', 'vel. do laco': 'loop speed',
    'velocidade (mph)': 'speed (mph)', 'velocidade da ponta (mph)': 'tip speed (mph)', 'velocidade do efeito': 'effect speed',
    'velocidade do missil (mph)': 'missile speed (mph)', 'vida do missil (s)': 'missile life (s)', 'winddown (quadros)': 'wind-down (frames)',
    'windup (quadros)': 'wind-up (frames)',
}
# localizadores (ossos nomeados que o golpe testa): Monster::handleLocator, ids 0x802..
LOC_EN = {0x802: 'L HAND', 0x803: 'R HAND', 0x804: 'L FOOT', 0x805: 'R FOOT', 0x80B: 'MOUTH', 0x816: 'TAIL MID', 0x817: 'TAIL TIP',
          0x818: 'HEAD', 0x819: 'SHOULDER'}


def loc_names(*ids):
    return '+'.join(LOC_EN.get(v, '0x%X' % v) for v in ids if v >= 0x7D0) or '-'


def anim_frames(mdl, anim):
    """Duracao da animacao em quadros (60 por segundo) ou None."""
    if mdl is None:
        return None
    a = mdl.find_anim(anim)
    return None if a is None else float(a[3] - a[2])


def timing(row, frames):
    """{frames, start, active, total}: quadros ate o primeiro teste, quadros com teste e a duracao do golpe na velocidade dele."""
    if not frames:
        return None
    sp = row["vel"] if row.get("vel", 0) > 0 else 1.0
    f = frames / sp
    return {"frames": round(frames), "start": round(row["ini"] * f), "active": max(0, round((row["fim"] - row["ini"]) * f)),
            "total": round(f)}


# as chaves das linhas do chr_dump (portugues) -> as do resultado (ingles)
RENAME = {"tipo": "ptype", "dano": "damage", "raio": "radius", "raio_padrao": "radius_default", "atord": "stun", "pausa": "pause",
          "estamina": "stamina", "vel": "speed", "ini": "win_start", "fim": "win_end", "cont_ini": "counter_start",
          "cont_fim": "counter_end", "kb_h": "kb_out", "kb_v": "kb_up", "dano_objeto": "object_damage", "prox": "next_ms",
          "locs": "locators"}


def decorate(row, mdl, weapon):
    t = TYPE_INFO[row["tipo"]]
    out = {RENAME.get(k, k): v for k, v in row.items() if k != "loc"}
    out["bones"] = loc_names(*row["locs"])
    out["pos"] = row["pos"] + 1
    out["type"] = t[0]
    out["button"] = t[1]
    out["dir"] = t[2]
    out["weapon"] = weapon
    out["timing"] = timing(row, anim_frames(mdl, row["anim"]))
    return out


def chains(rows, weapon):
    """Sequencias do mesmo botao (COMBO: QUADRADO X 4): o proximo golpe do mesmo tipo vale se o float prox[tipo] do golpe atual for
    > 0 e a casa seguinte existir. O dano e a soma."""
    by = {(r["pos"], r["tipo"]): r for r in rows}
    out = []
    for k in TYPE_ORDER:
        r = by.get((0, k))
        if not r:
            continue
        total = r["mult"] if weapon else r["dano"]
        n = 1
        for t in range(0, 9):
            cur, nxt = by.get((t, k)), by.get((t + 1, k))
            if not cur or not nxt or not cur["prox"][k] > 0:
                break
            total += nxt["mult"] if weapon else nxt["dano"]
            n += 1
        if n > 1:
            out.append({"ptype": k, "type": TYPE_INFO[k][0], "button": TYPE_INFO[k][1], "dir": TYPE_INFO[k][2], "length": n,
                        "damage": total, "weapon": weapon})
    return out


def specials_view(data, keys, name):
    """[{key, name, fields: [{label, value}]}] dos especiais e do crowd control que este monstro usa (so os campos de papel
    conhecido; o --specials do chr_dump mostra todos)."""
    out = []
    ftype = cd.FILE_TYPE.get(name.upper(), 0) << 5
    for kid, o, size in keys:
        spec = cd.SPECIAL_KEYS.get(kid)
        if not spec:
            continue
        st_name, _base, need_type, fields, wave, _cond = spec
        if need_type is not None and ftype != need_type:
            continue
        fl = []
        for fld in fields:
            ko, _so, desc = fld[:3]
            if ko + 4 > size or desc.startswith("?") or "nao lido" in desc:
                continue
            isint = len(fld) > 3 and fld[3] == "i"
            v = cd.i32(data, o + ko) if isint else cd.f32(data, o + ko)
            lab = desc.rstrip("?")
            fl.append({"label": EN.get(lab, EN.get(lab.split(" (+0x")[0], lab)), "value": round(v, 4) if isinstance(v, float) else v,
                       "key": kid, "off": ko, "kind": "i" if isint else "f"})
        if kid == 0xFAA:   # os estaticos do HomingBug, que o jogo le dos mesmos bytes da chave
            for i, s in enumerate(cd.HOMING_BUG_STATICS):
                if 0x28 + 4 * i + 4 <= size:
                    fl.append({"label": "bug: " + s, "value": round(cd.f32(data, o + 0x28 + 4 * i), 4), "key": kid, "off": 0x28 + 4 * i,
                               "kind": "f"})
        if wave:
            kn, _kts, _k0, kring, _cfg, nf = wave
            fl.extend(ring_fields(data, o, size, kid, kn, kring, nf))
        out.append({"key": "0x%X" % kid, "state": st_name, "name": SPECIAL_NAMES.get(kid, st_name), "fields": fl})
    return out


RING_COLS = ["reach", "damage", "kills", "force C", "force 10%", "stun", "recoil"]


def ring_fields(data, o, size, kid, kn, kring, nf, limit=8):
    """Os aneis da onda de choque (ShockWaveConfig) de uma chave: um campo por numero de cada anel, com "ring" e "col". O numero
    de aneis e o float em kn; cada anel tem nf floats a partir de kring (a ordem das colunas: RING_COLS)."""
    count = cd.f32(data, o + kn)
    n = 0
    while n < count and n < limit and kring + nf * 4 * (n + 1) <= size:
        n += 1
    return [{"label": "ring %d %s" % (i + 1, RING_COLS[j]), "value": round(cd.f32(data, o + kring + nf * 4 * i + 4 * j), 3), "key": kid,
             "off": kring + nf * 4 * i + 4 * j, "kind": "f", "ring": i, "col": RING_COLS[j]} for i in range(n) for j in range(nf)]


# Os outros golpes do MOVE LIST do treino que vem do .CHR (charreset::readKey; os numeros do agarrao com golpe, do levantar
# atacando, do counter e do taunt estao no construtor de cada tipo de monstro, nao no arquivo). Cada um: id, nome, chave, e como
# montar a dica (a do painel do treino).
def others_view(data, keys, name):
    """[{id, name, hint, fields: [{label, value, key, off, kind}]}]: butt slam (0xFB8), investida (0xFB4), agarrao (0xFDA, o alcance)
    e arremesso (0xFCD)."""
    by = {k: (o, s) for k, o, s in keys}
    out = []

    def f(kid, off, label, kind="f"):
        o, s = by[kid]
        if off + 4 > s:
            return None
        v = cd.i32(data, o + off) if kind == "i" else cd.f32(data, o + off)
        return {"label": label, "value": round(v, 4) if kind == "f" else v, "key": kid, "off": off, "kind": kind}

    def val(fl, label):
        return next((x["value"] for x in fl if x["label"] == label), 0.0)

    if 0xFB8 in by:   # butt slam: custo, onda de 7 floats por anel a partir de +0x48 (a contagem em +0x3C)
        o, s = by[0xFB8]
        rings = ring_fields(data, o, s, 0xFB8, 0x3C, 0x48, 7)
        fl = [f(0xFB8, 0x28, "stamina cost")] + rings
        n = len({r["ring"] for r in rings})
        d0 = rings[1]["value"] if n else 0.0
        r0 = rings[0]["value"] if n else 0.0
        rn = rings[7 * (n - 1)]["value"] if n else 0.0
        out.append({"id": "butt", "name": "BUTT SLAM", "fields": fl, "damage": d0, "kb": rings[3]["value"] if n else 0.0,
                    "hint": "IN THE AIR: UP + HEAVY. %.0f DAMAGE WITHIN %.0f, THEN A SHOCKWAVE UP TO %.0f (%d RINGS). STAMINA %.0f. KNOCKS DOWN" % (
                        d0, r0, rn, n, fl[0]["value"])})
    if 0xFB4 in by:   # investida: dano base e por unidade de velocidade, custo
        fl = [f(0xFB4, 0x20, "base damage"), f(0xFB4, 0x24, "damage per unit of speed"), f(0xFB4, 0x28, "stamina cost")]
        fl = [x for x in fl if x]
        if len(fl) == 3:
            base, per, cost = (x["value"] for x in fl)
            out.append({"id": "ram", "name": "RAM ATTACK", "fields": fl, "damage": base,
                        "hint": "RUN (TAP FORWARD TWICE) + HEAVY, HOLD TO CHARGE. %.0f DAMAGE%s. STAMINA %.0f. KNOCKS DOWN" % (
                            base, " + %.3f PER UNIT OF SPEED" % per if per > 0 else "", cost)})
    if 0xFDA in by:
        fl = [f(0xFDA, 0x0C, "grab range")]
        if fl[0]:
            out.append({"id": "grab", "name": "GRAB", "fields": fl, "damage": 0.0,
                        "hint": "GRAB NEAR THE OPPONENT (RANGE %.0f). THEN LIGHT OR HEAVY TO HIT, GRAB OR UP + GRAB TO THROW" % fl[0]["value"]})
    if 0xFCD in by:   # arremesso: dano do impacto no chao, por velocidade, e na parede
        fl = [f(0xFCD, 0x28, "landing damage"), f(0xFCD, 0x2C, "landing damage per unit of speed"), f(0xFCD, 0x14, "wall damage")]
        fl = [x for x in fl if x]
        if len(fl) == 3:
            ground, perv, wall = (x["value"] for x in fl)
            out.append({"id": "throw", "name": "GRAB: THROW", "fields": fl, "damage": ground,
                        "hint": "GRAB AGAIN WHILE GRABBING (UP + GRAB THROWS UP). LANDING %.0f%s, WALL %.0f; WHOEVER IT HITS TAKES THE SAME" % (
                            ground, " + %.3f PER UNIT OF SPEED" % perv if perv > 0 else "", wall)})
    return out


def set_key_field(path, key, off, value):
    """Grava um numero de uma chave do .CHR (os campos que specials_view e others_view mostram). So um campo que essas vistas
    listam: nada de offset solto. -> (antigo, novo); ValueError quando nao serve."""
    import struct
    data = bytearray(open(path, "rb").read())
    keys = cd.read_keys(data)
    name = os.path.splitext(os.path.basename(path))[0]
    fields = [x for s in specials_view(data, keys, name) + others_view(data, keys, name) for x in s["fields"]]
    fld = next((x for x in fields if x["key"] == key and x["off"] == off), None)
    if not fld:
        raise ValueError("that number is not one the kit edits")
    try:
        v = int(float(value)) if fld["kind"] == "i" else float(value)
    except (TypeError, ValueError):
        raise ValueError("%r is not a number" % (value,))
    if v != v or abs(v) > 1e7:
        raise ValueError("the number must be between -10000000 and 10000000")
    o = next(o for k, o, _s in keys if k == key)
    fmt = "<i" if fld["kind"] == "i" else "<f"
    old = struct.unpack_from(fmt, data, o + off)[0]
    struct.pack_into(fmt, data, o + off, v)
    with open(path, "wb") as fh:
        fh.write(data)
    return old, struct.unpack_from(fmt, data, o + off)[0]


def moves_of(path, ngp=None):
    """Os golpes do .CHR: {file, legacy, punches, swipes, chains, specials, fps, frames}. Com ngp, os quadros de cada golpe."""
    data = open(path, "rb").read()
    keys = cd.read_keys(data)
    name = os.path.splitext(os.path.basename(path))[0]
    res = {"file": os.path.basename(path), "fps": FPS, "legacy": cd.legacy(keys)}
    mdl = None
    if ngp:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import pose
        mdl = pose.Model(ngp)
    res["frames"] = bool(mdl)
    if res["legacy"]:
        res.update({"punches": [], "swipes": [], "chains": [], "specials": [], "others": [],
                    "note": "OGRE and SPIDER use the old key numbering: the move table is not read for them."})
        return res
    prow, default_r = cd.punch_table(data, keys)
    srow = cd.swipe_table(data, keys)
    res["default_radius"] = default_r
    res["punches"] = [decorate(r, mdl, False) for r in prow]
    res["swipes"] = [decorate(r, mdl, True) for r in srow]
    res["chains"] = chains(prow, False) + chains(srow, True)
    res["specials"] = specials_view(data, keys, name)
    res["others"] = others_view(data, keys, name)
    res["problems"] = problems(res)
    return res


# Campos editaveis de um golpe: nome (o das linhas de moves_of) -> (offset na config do arquivo, 'f' float | 'i' inteiro, minimo, maximo).
# A config do soco comeca em key + 0x10 + pos*0x3C0 + tipo*0xA0, a do golpe com objeto em key + 0x20 + pos*0x540 + tipo*0xE0
# (chr_dump.py e docs/FORMATS.md, "Character").
_F = {"win_start": ("f", 0, 1), "win_end": ("f", 0, 1), "counter_start": ("f", 0, 1), "counter_end": ("f", 0, 1),
      "kb_out": ("f", -1e5, 1e5), "kb_up": ("f", -1e5, 1e5), "stun": ("f", 0, 1e4), "pause": ("f", 0, 1e4), "radius": ("f", 0, 1e4),
      "stamina": ("f", 0, 1e4), "speed": ("f", 0.05, 20), "anim": ("i", 1, 299), "damage": ("f", 0, 1e4), "mult": ("f", 0, 100),
      "object_damage": ("f", 0, 1e4)}
_NEXT_P, _NEXT_S = 0x54, 0x4C   # the six delays (ms) to the next move, by its type: where they start in a punch / an object config
EDIT = {
    False: {"damage": 0x20, "win_start": 0x2C, "win_end": 0x30, "counter_start": 0x34, "counter_end": 0x38, "kb_out": 0x8C, "kb_up": 0x90,
            "stun": 0x94, "pause": 0x98, "radius": 0xA8, "stamina": 0x18, "speed": 0x24, "anim": 0x28},
    True: {"mult": 0x04, "object_damage": 0x08, "win_start": 0x14, "win_end": 0x18, "counter_start": 0x1C, "counter_end": 0x20,
           "kb_out": 0x98, "kb_up": 0x94, "stun": 0x9C, "pause": 0xA0, "radius": 0xA8, "stamina": 0x00, "speed": 0x0C, "anim": 0x10},
}
for _k in range(cd.TYPES):   # next_0..next_5: after this move, the ms before a move of that type may follow (0 = it cannot)
    EDIT[False]["next_%d" % _k] = _NEXT_P + 4 * _k
    EDIT[True]["next_%d" % _k] = _NEXT_S + 4 * _k
    _F["next_%d" % _k] = ("f", 0, 1e5)


def set_field(path, weapon, pos, tipo, field, value):
    """Grava um campo de um golpe no .CHR (so o proprio de um custom: quem chama confere). pos 1..10, tipo 0..5 (o indice do arquivo).
    -> (valor antigo, valor novo); ValueError com a explicacao quando o valor nao serve."""
    import struct
    if field not in EDIT[bool(weapon)]:
        raise ValueError("%s is not a field of this move" % field)
    if not (1 <= pos <= cd.TIERS and 0 <= tipo < cd.TYPES):
        raise ValueError("combo step 1..%d, type 0..%d" % (cd.TIERS, cd.TYPES - 1))
    kind, lo, hi = _F[field]
    try:
        v = int(float(value)) if kind == "i" else float(value)
    except (TypeError, ValueError):
        raise ValueError("%r is not a number" % (value,))
    if v != v or not (lo <= v <= hi):
        raise ValueError("%s goes from %g to %g" % (field, lo, hi))
    d = bytearray(open(path, "rb").read())
    keys = {k: (o, s) for k, o, s in cd.read_keys(d)}
    key = cd.SWIPE_KEY if weapon else cd.PUNCH_KEY
    if key not in keys or (not weapon and keys[key][1] != cd.PUNCH_KEY_SIZE):
        raise ValueError("%s has no %s table (key 0x%X)" % (os.path.basename(path), "object" if weapon else "punch", key))
    o = keys[key][0]
    base = o + (0x20 + (pos - 1) * 0x540 + tipo * 0xE0 if weapon else 0x10 + (pos - 1) * 0x3C0 + tipo * 0xA0)
    a = base + EDIT[bool(weapon)][field]
    fmt = "<i" if kind == "i" else "<f"
    old = struct.unpack_from(fmt, d, a)[0]
    struct.pack_into(fmt, d, a, v)
    with open(path, "wb") as f:
        f.write(d)
    return old, struct.unpack_from(fmt, d, a)[0]


def problems(m):
    """Avisos sobre os golpes que o jogo faria de um jeito que o autor nao espera: [texto]. So golpes que causam dano (um espaco de
    enchimento do arquivo nao conta)."""
    out = []
    by = {(r["weapon"], r["pos"], r["ptype"]): r for r in m["punches"] + m["swipes"]}
    # os golpes que o jogador alcanca: o 1o passo de qualquer botao e o passo seguinte de um que o encadeia (o arquivo tem casas de
    # enchimento que nenhum combo chega a usar)
    reach, todo = set(), [(w, 1, k) for w in (False, True) for k in range(cd.TYPES) if (w, 1, k) in by]
    while todo:
        key = todo.pop()
        if key in reach:
            continue
        reach.add(key)
        for k in range(cd.TYPES):
            if by[key]["next_ms"][k] > 0 and (key[0], key[1] + 1, k) in by:
                todo.append((key[0], key[1] + 1, k))
    for r in m["punches"] + m["swipes"]:
        if (r["weapon"], r["pos"], r["ptype"]) not in reach or not (r["mult"] > 0 if r["weapon"] else r["damage"] > 0):
            continue
        who = "%s%s step %d, %s" % ("object " if r["weapon"] else "", r["type"], r["pos"], r["button"])
        if not 0 <= r["win_start"] < r["win_end"] <= 1:
            out.append("%s: the hit window %g-%g must be inside 0..1 and start before it ends" % (who, r["win_start"], r["win_end"]))
        if m["frames"] and r["timing"] is None:
            out.append("%s: animation %d is not in the model" % (who, r["anim"]))
        if r["bones"] == "-" and r["pos"] == 1:
            out.append("%s: no bone tests the hit (no locator)" % who)
        if r["radius"] <= 0 and (r["weapon"] or not m.get("default_radius")):
            out.append("%s: radius 0 and no default: the hit sphere has no size" % who)
        for k in range(cd.TYPES):   # a combo that points to a step that is not there
            if r["next_ms"][k] > 0 and r["pos"] < cd.TIERS and (r["weapon"], r["pos"] + 1, k) not in by:
                out.append("%s: chains to %s but that step is empty" % (who, TYPE_INFO[k][0]))
    return out


def table(m):
    """O resultado como texto (a tabela do modo treino: golpe, janela, dano, raio, stun, knockback)."""
    out = []
    if m.get("note"):
        return [m["note"]]

    def line(r, dmg):
        tm = r["timing"]
        q = "   start %2d active %2d of %3d frames" % (tm["start"], tm["active"], tm["total"]) if tm else ""
        return "  %-3d %-13s %-6s anim %3d  %3.0f-%3.0f%%  %-9s r %5.1f  stun %3.0f  kb %4.0f/%-4.0f %s%s" % (
            r["pos"], r["type"], r["button"], r["anim"], r["win_start"] * 100, r["win_end"] * 100, dmg, r["radius"], r["stun"], r["kb_out"],
            r["kb_up"], r["bones"], q)

    out.append("%s: PUNCHES (damage as the game applies it)" % m["file"])
    for r in m["punches"]:
        out.append(line(r, "dmg %4.1f" % r["damage"]))
    out.append("WITH AN OBJECT (the damage is the object's times the multiplier)")
    for r in m["swipes"]:
        out.append(line(r, "x%.2f" % r["mult"]))
    out.append("COMBOS (same button, damage summed; with an object: the multipliers summed): " + "; ".join(
        "%s%s x %d = %g" % ("OBJ " if c["weapon"] else "", c["type"], c["length"], round(c["damage"], 2)) for c in m["chains"]))
    for s in m["specials"]:
        out.append("%s (key %s, %s)" % (s["name"], s["key"], s["state"]))
        for f in s["fields"]:
            out.append("    %-44s %s" % (f["label"], f["value"]))
    if m.get("problems"):
        out.append("WARNINGS")
        out.extend("  " + p for p in m["problems"])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("chr")
    ap.add_argument("--ngp", help="the monster's model (MON/<NOME>0.NGP): frames of each move")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    m = moves_of(a.chr, a.ngp)
    if a.json:
        print(json.dumps(m, indent=1))
    else:
        print("\n".join(table(m)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
