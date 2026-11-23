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


def decorate(row, mdl, weapon):
    t = TYPE_INFO[row["tipo"]]
    out = dict(row)
    out["loc"] = loc_names(*row["locs"])
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
            out.append({"tipo": k, "type": TYPE_INFO[k][0], "button": TYPE_INFO[k][1], "dir": TYPE_INFO[k][2], "length": n,
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
            v = cd.i32(data, o + ko) if len(fld) > 3 and fld[3] == "i" else cd.f32(data, o + ko)
            lab = desc.rstrip("?")
            fl.append({"label": EN.get(lab, EN.get(lab.split(" (+0x")[0], lab)), "value": round(v, 4) if isinstance(v, float) else v})
        if kid == 0xFAA:   # os estaticos do HomingBug, que o jogo le dos mesmos bytes da chave
            for i, s in enumerate(cd.HOMING_BUG_STATICS):
                if 0x28 + 4 * i + 4 <= size:
                    fl.append({"label": "bug: " + s, "value": round(cd.f32(data, o + 0x28 + 4 * i), 4)})
        if wave:
            kn, _kts, _k0, kring, _cfg, nf = wave
            count = cd.f32(data, o + kn)
            n = 0
            while n < count and n < 20 and kring + nf * 4 * (n + 1) <= size:
                n += 1
            rings = [[round(cd.f32(data, o + kring + nf * 4 * i + 4 * j), 3) for j in range(nf)] for i in range(n)]
            fl.append({"label": "shockwave rings (reach, damage, kill, force C, force 10%s)" % (", stun, recoil" if nf > 5 else ""),
                       "value": rings})
        out.append({"key": "0x%X" % kid, "state": st_name, "name": SPECIAL_NAMES.get(kid, st_name), "fields": fl})
    return out


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
        res.update({"punches": [], "swipes": [], "chains": [], "specials": [],
                    "note": "OGRE and SPIDER use the old key numbering: the move table is not read for them."})
        return res
    prow, default_r = cd.punch_table(data, keys)
    srow = cd.swipe_table(data, keys)
    res["default_radius"] = default_r
    res["punches"] = [decorate(r, mdl, False) for r in prow]
    res["swipes"] = [decorate(r, mdl, True) for r in srow]
    res["chains"] = chains(prow, False) + chains(srow, True)
    res["specials"] = specials_view(data, keys, name)
    return res


def table(m):
    """O resultado como texto (a tabela do modo treino: golpe, janela, dano, raio, stun, knockback)."""
    out = []
    if m.get("note"):
        return [m["note"]]

    def line(r, dmg):
        tm = r["timing"]
        q = "   start %2d active %2d of %3d frames" % (tm["start"], tm["active"], tm["total"]) if tm else ""
        return "  %-3d %-13s %-6s anim %3d  %3.0f-%3.0f%%  %-9s r %5.1f  stun %3.0f  kb %4.0f/%-4.0f %s%s" % (
            r["pos"], r["type"], r["button"], r["anim"], r["ini"] * 100, r["fim"] * 100, dmg, r["raio"], r["atord"], r["kb_h"], r["kb_v"],
            r["loc"], q)

    out.append("%s: PUNCHES (damage as the game applies it)" % m["file"])
    for r in m["punches"]:
        out.append(line(r, "dmg %4.1f" % r["dano"]))
    out.append("WITH AN OBJECT (the damage is the object's times the multiplier)")
    for r in m["swipes"]:
        out.append(line(r, "x%.2f" % r["mult"]))
    out.append("COMBOS (same button, damage summed; with an object: the multipliers summed): " + "; ".join(
        "%s%s x %d = %g" % ("OBJ " if c["weapon"] else "", c["type"], c["length"], round(c["damage"], 2)) for c in m["chains"]))
    for s in m["specials"]:
        out.append("%s (key %s, %s)" % (s["name"], s["key"], s["state"]))
        for f in s["fields"]:
            out.append("    %-44s %s" % (f["label"], f["value"]))
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
