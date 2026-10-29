#!/usr/bin/env python3
"""Kit de mods do WotM (docs/guides/MODS.md). Extrai arquivos do disco para formatos editaveis na pasta
de trabalho do mod e devolve o que mudou, ja no formato que o jogo le, para mods/<mod>/. Um mod = um projeto: cada mod tem a sua
pasta de trabalho (<mods>/_work/<mod>/, com o modkit.json dela), e o pack de um mod nunca leva o que foi editado para outro.

  python tools/modkit.py export my_mod MON/LIZARD0           # .TEX e .RTX do Togera em PNG (pasta de trabalho do my_mod)
  python tools/modkit.py export my_mod SND/SHELL_S.BNK VAG/CITYAMB.VPK   # sons do banco e a musica em WAV
  python tools/modkit.py pack my_mod                         # o que foi editado vai para mods/my_mod/
  python tools/modkit.py check [my_mod]                      # confere os mods (nomes, tamanhos, texIds) e os conflitos entre eles
  python tools/modkit.py revert my_mod MON/lizard0_t0004.png # volta um arquivo da pasta de trabalho ao original do disco
  python tools/modkit.py skin new my_mod togera green --base 1 --name "GREEN TOGERA"   # skin nova: skin.json + PNGs do traje base
  python tools/modkit.py skin new my_mod goliath gold        # skin de um monstro custom (id do monster.json)
  python tools/modkit.py monster new my_mod fire_togera --base togera --costume 2 --name "FIRE TOGERA" --ai   # monstro custom
  python tools/modkit.py monster new my_mod gold_goliath --base assboss       # de um chefe (traje e camera padrao do chefe)
  python tools/modkit.py monster portrait my_mod fire_togera retrato.png       # retrato do HUD (2:1)
  python tools/modkit.py monster model my_mod fire_togera X.NGP X.PTR          # modelo proprio (esqueleto do tipo base)
  python tools/modkit_gui.py                                 # o mesmo com interface (botao "Mod kit" do launcher)

Opcoes: --disc <pasta> (disco extraido; padrao WOTM_DISC ou disc/ do repo), --mods <pasta> (padrao: <runtime>/discroot/mods, o
runtime de .wotm_recomp_dir; sem ele, <disco>/mods), --work <pasta> (padrao: <mods>/_work/<mod>; pasta com '_' o jogo nao le).
Nome de mod: nao comeca com '_' nem '.', sem \\ / : * ? " < > |.
Arquivos do export podem ter curinga (MON/LIZARD*) e sem extensao pegam .TEX e .RTX.

O que cada tipo vira:
- .TEX / .RTX -> <arquivo>_tNNNN.png / <arquivo>_rtx_tNNNN.png (tools/tex.py export). No pack o PNG vai como esta: o desenho nativo
  troca a textura na hora de desenhar (native/wotm_texmods.hpp), em qualquer resolucao. Com --inject o pack tambem regrava o
  .TEX/.RTX (mesmo tamanho e cores da paleta atual), para a imagem do GS emulado (referencia da checagem VU1).
- .VPK / .VAG -> <arquivo>.wav; volta pelo tools/snd989.py (mesma taxa e volumes do original).
- .BNK -> <banco>_sNNN.wav, um por som com amostra; os editados voltam todos num banco so (snd989 bnk-replace).
O pack so leva o que mudou desde o export (sha1 em modkit.json na pasta de trabalho). Nada do jogo vai para o repo: o kit le o
disco do usuario e grava nas pastas dele.
"""
import contextlib
import fnmatch
import io
import glob
import hashlib
import json
import os
import re
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tex      # noqa: E402
import snd989   # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEX_DIRS = ("LVL", "MON", "SHELL")
PNG_RE = re.compile(r"^(.+?)(_rtx)?_t(\d+)(?:_sempaleta)?\.png$", re.I)   # _sempaleta: o export nao achou a paleta (cinza)
BNK_RE = re.compile(r"^(.+)_s(\d+)\.wav$", re.I)


def sha1(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest()


def default_disc():
    return os.environ.get("WOTM_DISC") or os.path.join(REPO, "disc")


def default_mods(disc):
    try:
        with open(os.path.join(REPO, ".wotm_recomp_dir"), encoding="utf-8") as f:
            rt = f.read().strip()
        if os.path.isdir(os.path.join(rt, "discroot")):
            return os.path.join(rt, "discroot", "mods")
    except OSError:
        pass
    return os.path.join(disc, "mods")


WORK = "_work"                  # pasta das pastas de trabalho, dentro de <mods> (com '_': o jogo nao le)
BAD_MOD_CHARS = '\\/:*?"<>|'


def mod_name_error(mod):
    """Mensagem de erro (em ingles, a interface mostra) se o nome nao serve para um mod; None se serve."""
    if not mod or not mod.strip():
        return "the mod needs a name"
    if mod != mod.strip():
        return "the mod name cannot start or end with spaces"
    if mod[0] in "_.":
        return "the mod name cannot start with '_' or '.' (the game skips those folders)"
    bad = sorted(set(c for c in mod if c in BAD_MOD_CHARS or ord(c) < 32))
    if bad:
        return "the mod name cannot have %s" % " ".join(repr(c) for c in bad)
    return None


NEW_MOD_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$")


def new_mod_name_error(mod):
    """Regra estrita para um mod novo (a interface e o 'New mod...'): letras, digitos, '_' e '-', comecando por letra ou digito,
    ate 64. Mods ja existentes com outros nomes continuam valendo (mod_name_error)."""
    if not mod:
        return "the mod needs a name"
    if not NEW_MOD_RE.match(mod):
        return "use only letters, digits, '_' and '-', starting with a letter or digit (no spaces), up to 64"
    return None


def adopt_mod(disc, mods, mod, work=None):
    """Mod feito antes de ter pasta de trabalho (ou feito a mao): copia os arquivos de mods/<mod>/ para a pasta de trabalho dele,
    marcados como editados (sha1 vazio no estado), para a janela mostra-los e o pack leva-los de volta. skin.json e monster.json
    ficam so no mod. Roda a cada escolha do mod: so entra o que a pasta de trabalho nao tem e o estado nunca viu (arquivo removido
    de proposito nao volta). Devolve quantos copiou."""
    if work is None:
        work = work_dir(mods, mod)
    src = os.path.join(mods, mod)
    if not os.path.isdir(src):
        return 0
    state = load_state(work)
    n = 0
    for root, dirs, files in os.walk(src):
        dirs[:] = [x for x in dirs if not x.startswith(".")]
        for name in sorted(files):
            if name.lower() in ("skin.json", "monster.json", "modkit.json") or name.startswith("."):
                continue
            p = os.path.join(root, name)
            rel = os.path.relpath(p, src).replace("\\", "/")
            q = os.path.join(work, *rel.split("/"))
            if os.path.exists(q) or rel in state["files"]:
                continue
            os.makedirs(os.path.dirname(q), exist_ok=True)
            shutil.copy2(p, q)
            ent = {"sha1": "", "adopted": True}   # sha1 vazio nunca bate: conta como editado
            m = PNG_RE.match(name)
            if m:
                trel = find_texfile(disc, m.group(1), ".RTX" if m.group(2) else ".TEX")
                if trel:
                    pk = next((k for k in tex.parse(tex.read_file(disc_file(disc, trel))[0])["packets"]
                               if k.texId == int(m.group(3)) and not (m.group(2) and k.is_palette())), None)
                    if pk is not None:
                        ent["size"] = [pk.w, pk.h]
            state["files"][rel] = ent
            n += 1
    if n:
        save_state(work, state)
        print("%d file(s) of %s copied into its work folder (they count as edited)" % (n, mod))
    return n


def validate_mod_name(mod, what=""):
    """O nome do mod, ou SystemExit com a mensagem (prefixada por what: "pack: ...")."""
    err = mod_name_error(mod)
    if err:
        raise SystemExit("%s%s: %r" % (what + ": " if what else "", err, mod))
    return mod


def work_dir(mods, mod):
    """Pasta de trabalho do mod: <mods>/_work/<mod>/ (o modkit.json do estado fica nela)."""
    return os.path.join(mods, WORK, validate_mod_name(mod))


def disc_file(disc, rel):
    """Caminho no disco sem depender de maiusculas (o disco extraido no Windows nao liga, o WSL liga)."""
    p = os.path.join(disc, *rel.split("/"))
    if os.path.exists(p):
        return p
    cur = disc
    for part in rel.split("/"):
        if not os.path.isdir(cur):
            return None
        hit = [n for n in os.listdir(cur) if n.upper() == part.upper()]
        if not hit:
            return None
        cur = os.path.join(cur, hit[0])
    return cur


def find_texfile(disc, stem, ext):
    """<STEM>.TEX/.RTX no disco: LVL/, MON/, SHELL/ ou <STEM>/ (como o desenho nativo procura). Devolve o caminho relativo."""
    for d in TEX_DIRS + (stem.upper(),):
        rel = "%s/%s%s" % (d, stem.upper(), ext)
        if disc_file(disc, rel):
            return rel
    return None


def expand(disc, pats):
    out = []
    for pat in pats:
        pat = pat.replace("\\", "/").strip("/")
        if not os.path.splitext(pat)[1]:
            pats2 = [pat + ".TEX", pat + ".RTX"]
        else:
            pats2 = [pat]
        for p in pats2:
            d, base = os.path.split(p)
            dd = disc_file(disc, d) if d else disc
            if not dd:
                continue
            for n in sorted(os.listdir(dd)):
                if fnmatch.fnmatch(n.upper(), base.upper()):
                    out.append((d + "/" if d else "") + n)
    if not out:
        raise SystemExit("nothing found on the disc (%s) for %s" % (disc, " ".join(pats)))
    return list(dict.fromkeys(out))


# ---------------------------------------------------------------- export

def export(disc, work, pats, dest=None):
    """dest: subpasta da pasta de trabalho no lugar da pasta do disco (skin new: skins/<id>)."""
    state = load_state(work)
    for rel in expand(disc, pats):
        src = disc_file(disc, rel)
        d = dest if dest else os.path.dirname(rel)
        final = os.path.join(work, *d.split("/")) if d else work
        out = os.path.join(work, ".export_tmp")   # grava aqui e depois move, sem atropelar o que ja foi editado
        shutil.rmtree(out, ignore_errors=True)
        os.makedirs(out)
        ext = os.path.splitext(rel)[1].upper()
        if ext in (".TEX", ".RTX"):
            with contextlib.redirect_stdout(io.StringIO()):   # o tex.py fala da pasta temporaria
                man = tex.cmd_export(src, out)
            n = sum(1 for e in man["textures"] if "png" in e)
            print("%s -> %d texture(s) as PNG (%s)" % (rel, n, (os.path.splitext(os.path.basename(rel))[0].lower() +
                                                                 ("_rtx" if ext == ".RTX" else "") + "_tNNNN.png")))
        elif ext in (".VPK", ".VAG"):
            rate, chans = snd989.read_vpk(src)
            name = os.path.splitext(os.path.basename(rel))[0].lower() + ".wav"
            snd989.write_wav(os.path.join(out, name), rate, chans)
            print("%s -> %s (%d Hz, %d channels)" % (rel, name, rate, len(chans)))
        elif ext == ".BNK":
            b = snd989.read_bnk(src)
            stem = os.path.splitext(os.path.basename(rel))[0].lower()
            n = 0
            for i, s in enumerate(b["sounds"]):
                tones = [g for g in s["grains"] if g["type"] in (1, 9)]
                if not tones:
                    continue
                chans = [snd989.adpcm_decode(b["samples"], g["sample"])[0] for g in tones[:2]]
                rate = round(snd989.tone_rate(tones[0]["note"], tones[0]["fine"]))
                snd989.write_wav(os.path.join(out, "%s_s%03d.wav" % (stem, i)), rate, chans)
                n += 1
            print("%s -> %d sounds as WAV (%s_sNNN.wav)" % (rel, n, stem))
        else:
            print("%s: no converter for this type; copied as is" % rel)
            shutil.copy2(src, os.path.join(out, os.path.basename(rel)))
        os.makedirs(final, exist_ok=True)
        kept = 0
        for n in sorted(os.listdir(out)):
            p, q = os.path.join(out, n), os.path.join(final, n)
            key = os.path.relpath(q, work).replace("\\", "/")
            old = state["files"].get(key, {}).get("sha1")
            if os.path.exists(q) and not n.endswith(".json") and sha1(q) != old:
                kept += 1   # editado depois do export anterior: fica o do usuario
                continue
            os.replace(p, q)
            if not n.endswith(".json"):
                ent = state["files"].setdefault(key, {})
                ent["sha1"] = sha1(q)
                sz = png_size(q)
                if sz:
                    ent["size"] = list(sz)   # tamanho no jogo (a interface avisa se o substituto muda a proporcao)
        shutil.rmtree(out, ignore_errors=True)
        if kept:
            print("  %d file(s) already edited in the work folder were kept" % kept)
        save_state(work, state)
    print("work folder: %s (edit there, then pack into a mod: modkit.py pack <mod>, or the Pack button)" % work)


def source_of(disc, rel):
    """Arquivo do disco de onde saiu um arquivo da pasta de trabalho ("MON/lizard0_t0004.png" -> "MON/LIZARD0.TEX"); None se nao achou."""
    d, n = os.path.split(rel.replace("\\", "/"))
    if d.startswith(("skins/", "monsters/")):   # PNG de skin ou de custom (skins/<id>/, monsters/<id>/files/): saiu do traje base em MON/
        d = "MON"
    pre = d + "/" if d else ""
    m = PNG_RE.match(n)
    stem, ext = os.path.splitext(n)
    if m:
        cands = [m.group(1).upper() + (".RTX" if m.group(2) else ".TEX")]
    elif ext.lower() == ".wav":
        mb = BNK_RE.match(n)
        cands = [mb.group(1).upper() + ".BNK"] if mb else [stem.upper() + ".VPK", stem.upper() + ".VAG"]
    else:
        cands = [n]
    for c in cands:
        if disc_file(disc, pre + c):
            return pre + c
    return None


def revert(disc, work, rels):
    """Volta arquivos da pasta de trabalho ao original: extrai de novo o arquivo do disco numa pasta temporaria e copia so esses."""
    state = load_state(work)
    by_src = {}
    for rel in rels:
        rel = rel.replace("\\", "/")
        src = source_of(disc, rel)
        if not src:
            print("%s: source file not found on the disc" % rel)
            continue
        by_src.setdefault(src, []).append(rel)
    tmp = os.path.join(work, ".revert_tmp")
    n = 0
    for src, items in sorted(by_src.items()):
        shutil.rmtree(tmp, ignore_errors=True)
        with contextlib.redirect_stdout(io.StringIO()):
            export(disc, tmp, [src])
        for rel in items:
            fresh = os.path.join(tmp, *(os.path.dirname(src).split("/") + [os.path.basename(rel)]))
            dst = os.path.join(work, *rel.split("/"))
            if not os.path.exists(fresh):
                print("%s: not produced by a new export of %s" % (rel, src))
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(fresh, dst)
            ent = state["files"].setdefault(rel, {})
            ent["sha1"] = sha1(dst)
            sz = png_size(dst)
            if sz:
                ent["size"] = list(sz)
            print("reverted %s (from %s)" % (rel, src))
            n += 1
    shutil.rmtree(tmp, ignore_errors=True)
    save_state(work, state)
    print("%d file(s) back to the original" % n)
    return n


def png_size(p):
    """(largura, altura) do cabecalho IHDR de um PNG; None se nao for PNG."""
    try:
        with open(p, "rb") as f:
            h = f.read(24)
    except OSError:
        return None
    if len(h) < 24 or h[:8] != b"\x89PNG\r\n\x1a\n" or h[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", h[16:24])


def load_state(work):
    try:
        with open(os.path.join(work, "modkit.json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {"files": {}}


def save_state(work, state):
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(work, "modkit.json"), "w", encoding="utf-8") as f:
        json.dump(state, f, indent=1, sort_keys=True)


# ---------------------------------------------------------------- pack

def pack(disc, work, mods, mod, inject=False):
    """Leva o que mudou na pasta de trabalho (work None = work_dir(mods, mod)) para mods/<mod>/."""
    validate_mod_name(mod, "pack")
    if work is None:
        work = work_dir(mods, mod)
    state = load_state(work)
    dst = os.path.join(mods, mod)
    changed = []
    for root, dirs, files in os.walk(work):
        dirs[:] = [x for x in dirs if not x.startswith(".")]
        for n in files:
            p = os.path.join(root, n)
            rel = os.path.relpath(p, work).replace("\\", "/")
            if n.endswith(".json") or n.startswith("."):
                continue
            old = state["files"].get(rel, {}).get("sha1")
            if old == sha1(p):
                continue
            changed.append(rel)
    if not changed:
        print("nothing changed in %s since the export" % work)
        return
    banks = {}
    for rel in sorted(changed):
        p = os.path.join(work, *rel.split("/"))
        d, n = os.path.split(rel)
        m = PNG_RE.match(n)
        if m:
            stem, rtx, tid = m.group(1), bool(m.group(2)), int(m.group(3))
            os.makedirs(os.path.join(dst, *d.split("/")) if d else dst, exist_ok=True)
            name = "%s%s_t%04d.png" % (stem, "_rtx" if rtx else "", tid)   # sem o _sempaleta: o nome que o jogo procura
            out_rel = (d + "/" if d else "") + name
            shutil.copy2(p, os.path.join(dst, *out_rel.split("/")))
            print("texture %s -> %s/%s" % (n, mod, out_rel))
            if inject and not d.startswith(("skins/", "monsters/")):   # PNG de skin ou custom vale so para ele
                inject_png(disc, dst, stem, rtx, tid, p)
            continue
        if n.lower().endswith(".wav"):
            mb = BNK_RE.match(n)
            if mb and find_disc_ext(disc, d, mb.group(1), ".BNK"):
                banks.setdefault((d, mb.group(1)), []).append((int(mb.group(2)), p))
                continue
            stem = os.path.splitext(n)[0]
            for ext in (".VPK", ".VAG"):
                orig = find_disc_ext(disc, d, stem, ext)
                if orig:
                    out = os.path.join(dst, *d.split("/"), stem.upper() + ext)
                    os.makedirs(os.path.dirname(out), exist_ok=True)
                    rate, chans = snd989.read_wav_any(p)
                    if ext == ".VPK":
                        like = open(orig, "rb").read()
                        _, _, _, orate, onch = struct.unpack_from("<5I", like, 4)
                        vols = [struct.unpack_from("<HH", like, 0x18 + 4 * c) for c in range(onch)]
                        chans = [snd989.resample(c, rate, orate) for c in snd989.to_channels(chans, onch)]
                        snd989.write_vpk(out, orate, chans, vols)
                    else:
                        snd989.write_vag(out, rate, snd989.to_channels(chans, 1)[0])
                    print("sound %s -> %s/%s" % (n, mod, os.path.relpath(out, dst).replace("\\", "/")))
                    break
            else:
                print("WARNING: %s does not match any .VPK/.VAG/.BNK on the disc; skipped" % rel)
            continue
        out = os.path.join(dst, *rel.split("/"))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copy2(p, out)
        print("file %s -> %s/%s (as is)" % (rel, mod, rel))
    for (d, stem), sounds in sorted(banks.items()):
        src = find_disc_ext(disc, d, stem, ".BNK")
        out = os.path.join(dst, *d.split("/"), stem.upper() + ".BNK")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        tmp = out + ".tmp"
        cur = src
        for i, wav in sorted(sounds):
            snd989.bnk_replace(cur, i, wav, tmp)
            os.replace(tmp, out)
            cur = out
        print("bank %s.BNK: %d sound(s) replaced -> %s" % (stem.upper(), len(sounds), os.path.relpath(out, mods)))
    print("mod in %s" % dst)


# ---------------------------------------------------------------- skins de mod (MODS.md, "Skins")

MONSTERS = {"congar": "MONKEY", "robo-47": "ROBOT", "togera": "LIZARD", "ultra-v": "SHOGUN", "preytor": "MANTIS",
            "kineticlops": "ENERGY", "magmo": "LAVA", "raptros": "DRAGON", "agamo": "ROCK", "zorgulon": "ALIEN"}


# Chefes que um monstro custom pode usar de base (kCustomBases em native/wotm_mods.hpp)
CUSTOM_BASES = {"assboss": "ASSBOSS", "jelly": "JELLY", "final": "FINAL"}
# Para um custom novo: traje padrao e camera de cada chefe (MODS.md: a do ASSBOSS.CHR fica em cima do monstro, a do JELLY.CHR entre
# as pernas; a do FINAL.CHR ja serve). Base jogavel: traje 0 e a camera do proprio tipo.
BOSS_DEFAULTS = {"assboss": (1, ("togera", 200)), "jelly": (0, ("togera", 300)), "final": (0, None)}
KINDS = ("mecha",)   # regras proprias do port ("kind" do monster.json, wotm_custom.inc)


def norm(s):
    return "".join(c for c in s.lower() if c.isalnum())


def game_monster(s):
    """Nome do jogo ("togera") de um monstro jogavel pelo nome ou prefixo ("LIZARD", "Robo-47"); None se nao for."""
    return next((g for g, p in MONSTERS.items() if norm(s) in (norm(g), norm(p))), None)


def base_prefix(s):
    """Prefixo em MON/ do base de um custom (jogaveis e kCustomBases); None se o port nao aceita esse base."""
    g = game_monster(s)
    if g:
        return MONSTERS[g]
    return next((p for g, p in CUSTOM_BASES.items() if norm(s) in (norm(g), norm(p))), None)


def base_name(s):
    """Nome do base de um custom como o monster.json o grava ("togera", "assboss"); None se o port nao aceita."""
    g = game_monster(s)
    if g:
        return g
    return next((g for g, p in CUSTOM_BASES.items() if norm(s) in (norm(g), norm(p))), None)


def mod_folders(mods):
    """{nome: pasta} dos mods ligados: os do repo (REPO/mods, que o sync_mods espelha) e os da pasta de mods, que vence no mesmo nome."""
    out = {}
    for root in (os.path.join(REPO, "mods"), mods):
        if not root or not os.path.isdir(root):
            continue
        for n in os.listdir(root):
            p = os.path.join(root, n)
            if os.path.isdir(p) and n[0] not in "_.":
                out[n] = p
    return out


def find_customs(mods):
    """Monstros custom (mods/<mod>/monsters/<pasta>/monster.json) dos mods ligados, por id: {id: {"id", "name", "base", "prefix",
    "costume", "mod", "path"}}. Mesmo id em dois mods: vence o ultimo em ordem alfabetica, como no carregador."""
    by_id = {}
    for mod, folder in sorted(mod_folders(mods).items()):
        mdir = os.path.join(folder, "monsters")
        if not os.path.isdir(mdir):
            continue
        for sub in sorted(os.listdir(mdir)):
            js = os.path.join(mdir, sub, "monster.json")
            try:
                with open(js, encoding="utf-8") as f:
                    v = json.load(f)
            except (OSError, ValueError):
                continue
            if not isinstance(v, dict):
                continue
            pre = base_prefix(v.get("base")) if isinstance(v.get("base"), str) else None
            cid = (v.get("id") if isinstance(v.get("id"), str) else sub).lower()
            if not pre or not re.fullmatch(r"[a-z][a-z0-9_]{0,31}", cid):
                continue
            co = v.get("costume", 0)
            co = co if isinstance(co, int) and 0 <= co <= 3 else 0
            by_id[cid] = {"id": cid, "name": v.get("name") or cid, "base": v.get("base"), "prefix": pre, "costume": co,
                          "mod": mod, "path": js}
    return by_id


def list_monsters(mods):
    """Monstros para uma skin: jogaveis do disco [(nome_do_jogo, prefixo, None)] e customs [(id, prefixo_do_base, costume)]."""
    out = [(g, p, None) for g, p in MONSTERS.items()]
    out += [(c["id"], c["prefix"], c["costume"]) for c in sorted(find_customs(mods).values(), key=lambda c: c["id"])]
    return out


SKIN_ID_RE = re.compile(r"[a-z0-9_]{1,32}")
CUSTOM_ID_RE = re.compile(r"[a-z][a-z0-9_]{0,31}")


def skin_id_error(sid):
    """Mensagem (em ingles) se o id nao serve para uma skin; None se serve."""
    if not SKIN_ID_RE.fullmatch(sid or ""):
        return "the id takes only lowercase letters, digits and _ (1 to 32)"
    return None


def skin_slug(name):
    """Id de skin a partir do nome ("GREEN TOGERA" -> "green_togera")."""
    return re.sub(r"_+", "_", re.sub(r"[^a-z0-9_]", "_", (name or "").lower())).strip("_")[:32].strip("_")


def skin_dir(mods, mod, sid):
    return os.path.join(mods, mod, "skins", sid)


def list_skins(mods, mod):
    """{pasta: skin.json} das skins de mods/<mod>/skins/ (pasta = id do skin_new)."""
    d = os.path.join(mods, mod, "skins")
    out = {}
    for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        js = os.path.join(d, n, "skin.json")
        if os.path.isfile(js):
            out[n] = _json_obj(js)
    return out


def write_skin_json(mods, mod, sid, data):
    with open(os.path.join(skin_dir(mods, mod, sid), "skin.json"), "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


def portrait_tex(disc, prefix, costume):
    """texId do retrato do HUD no MON/<PREFIXO><traje>.RTX (a ultima textura 128x64, como o portrait_png); None se nao achou."""
    src = disc_file(disc, "MON/%s%d.RTX" % (prefix, costume))
    if not src:
        return None
    try:
        pks = [p for p in tex.parse(tex.read_file(src)[0])["packets"] if not p.is_palette()]
    except Exception:
        return None
    pks = [p for p in pks if (p.w, p.h) == (128, 64)] or pks
    return pks[-1].texId if pks else None


def portrait_png(disc, prefix, costume, out_png):
    """Grava em out_png o retrato do traje (a ultima textura 128x64 do MON/<PREFIXO><traje>.RTX), decodificado como o export
    (tex.cmd_export) faz, sem escrever nada no stdout (roda numa thread da interface). False se nao deu."""
    src = disc_file(disc, "MON/%s%d.RTX" % (prefix, costume))
    if not src:
        return False
    try:
        data, _ = tex.read_file(src)
        t = tex.parse(data)
        pks = [p for p in t["packets"] if not p.is_palette()]
        pks = [p for p in pks if (p.w, p.h) == (128, 64)] or pks
        if not pks:
            return False
        p = pks[-1]
        ngp = tex.sibling(src, ".NGP")
        pair = tex.Pairing(t, data, t, tex.ngp_texrefs(ngp) if ngp else [])
        off = p.data_off(True)
        if p.psm in (tex.T8, tex.T4, tex.T8H, tex.T4HL, tex.T4HH):
            pal, _cbp, csa, _ = pair.palette(p, hi=True)
            if pal is None:
                return False
            if p.psm in (tex.T4, tex.T4HL, tex.T4HH) and len(pal) < 256:
                csa = 0
            psm = p.psm if p.psm not in tex.HIGH else (tex.T8 if p.psm == tex.T8H else tex.T4)
            px = tex.decode(data, off, psm, p.w, p.h, pal, csa)
            fix = not tex.raw_hi(px)
        elif p.psm in (tex.CT32, tex.CT24, tex.CT16, tex.CT16S):
            px = tex.decode(data, off, p.psm, p.w, p.h, hi=True)
            fix = p.psm == tex.CT32 and not tex.raw_hi(px)
        else:
            return False
        if fix:
            px = [(r, g, b, tex.fix_alpha(a)) for (r, g, b, a) in px]
        tex.write_png(out_png, p.w, p.h, px)
        return True
    except Exception:
        return False


def skin_duplicate(mods, mod, work, sid, new):
    """Copia a skin sid para new: a pasta no mod (skin.json com o id novo), a pasta de trabalho e o estado dos PNGs."""
    err = skin_id_error(new)
    if err:
        raise SystemExit("duplicate skin: " + err)
    src, dst = skin_dir(mods, mod, sid), skin_dir(mods, mod, new)
    if os.path.exists(dst) or os.path.exists(os.path.join(work, "skins", new)):
        raise SystemExit("duplicate skin: there is already a skin %r in the mod" % new)
    shutil.copytree(src, dst)
    data = _json_obj(os.path.join(dst, "skin.json"))
    data["id"] = new
    write_skin_json(mods, mod, new, data)
    wsrc = os.path.join(work, "skins", sid)
    if os.path.isdir(wsrc):
        shutil.copytree(wsrc, os.path.join(work, "skins", new))
    st = load_state(work)
    pre = "skins/%s/" % sid
    for k, v in list(st["files"].items()):
        if k.startswith(pre):
            st["files"]["skins/%s/%s" % (new, k[len(pre):])] = dict(v)
    save_state(work, st)
    print("skin %s copied to %s (%s)" % (sid, new, dst))


def skin_delete(mods, mod, work, sid):
    """Apaga a skin: a pasta no mod, a pasta de trabalho e as chaves dela no estado."""
    shutil.rmtree(skin_dir(mods, mod, sid), ignore_errors=True)
    shutil.rmtree(os.path.join(work, "skins", sid), ignore_errors=True)
    st = load_state(work)
    pre = "skins/%s/" % sid
    st["files"] = {k: v for k, v in st["files"].items() if not k.startswith(pre)}
    save_state(work, st)
    print("skin %s deleted" % sid)


def skin_adopt(work, d, stem, sid, to=None):
    """Move para <work>/skins/<sid>/ (ou <work>/<to>/) os PNGs (e manifestos) de <work>/<d>/ que sairam de <STEM>.TEX/.RTX, com o
    sha1 do estado junto (os editados continuam editados). O traje original fica intocado no proximo pack. Devolve quantos PNGs moveu."""
    to = to or "skins/" + sid
    src = os.path.join(work, *d.split("/"))
    dst = os.path.join(work, *to.split("/"))
    os.makedirs(dst, exist_ok=True)
    st = load_state(work)
    n = 0
    mans = {(stem + "_manifest.json").lower(), (stem + "_rtx_manifest.json").lower()}
    for name in sorted(os.listdir(src)) if os.path.isdir(src) else []:
        m = PNG_RE.match(name)
        if not (m and m.group(1).lower() == stem.lower()) and name.lower() not in mans:
            continue
        os.replace(os.path.join(src, name), os.path.join(dst, name))
        ent = st["files"].pop("%s/%s" % (d, name), None)
        if ent is not None:
            st["files"]["%s/%s" % (to, name)] = ent
        n += bool(m)
    save_state(work, st)
    print("%d texture(s) of %s/%s moved to %s" % (n, d, stem.upper(), to))
    return n


def skin_new(disc, work, mods, mod, monster, sid, base=None, name=None, ai=False, textures=True):
    """Cria mods/<mod>/skins/<id>/skin.json e exporta as texturas do traje base para <work>/skins/<id>/; o pack leva os PNGs
    editados para a pasta da skin (valem so para ela). monster: um jogavel (nome ou prefixo) ou o id de um monstro custom; base
    None = traje 0 (jogavel) ou o costume do monster.json (custom). work None = work_dir(mods, mod). textures=False: so o
    skin.json (a interface move para a skin os PNGs de um traje ja extraido, skin_adopt)."""
    validate_mod_name(mod, "skin new")
    if work is None:
        work = work_dir(mods, mod)
    if skin_id_error(sid):
        raise SystemExit("skin new: " + skin_id_error(sid))
    if base is not None and base not in range(4):
        raise SystemExit("skin new: --base is a disc costume, 0 to 3")
    game = game_monster(monster)
    custom = None
    if game:
        key, prefix, costume = game, MONSTERS[game], base if base is not None else 0
    else:
        key = monster.strip().lower()
        if not re.fullmatch(r"[a-z][a-z0-9_]{0,31}", key):
            raise SystemExit("skin new: unknown monster %r (one of: %s, or the id of a custom monster)" % (monster, ", ".join(MONSTERS)))
        custom = find_customs(mods).get(key)
        prefix = custom["prefix"] if custom else None
        costume = base if base is not None else (custom["costume"] if custom else None)
    folder = os.path.join(mods, mod, "skins", sid)
    os.makedirs(folder, exist_ok=True)
    js = os.path.join(folder, "skin.json")
    if os.path.exists(js):
        print("%s already exists; kept" % js)
    else:
        data = {"monster": key, "id": sid, "name": (name or "%s %s" % (sid, key)).upper()}
        if game or base is not None:   # skin de custom sem --base: o jogo usa o costume do monster.json
            data["base"] = costume
        data["ai"] = ai
        with open(js, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)
            f.write("\n")
        print("skin %s/%s: %s" % (key, sid, js))
    if custom:
        print("custom monster %s (%s, mod %s): base %s, costume %d" % (key, custom["name"], custom["mod"], custom["base"], costume))
    elif not game:
        print("WARNING: no monster.json with id %r in the mods (%s, %s); skin.json created, but no textures exported "
              "(the base type is unknown)" % (key, mods, os.path.join(REPO, "mods")))
        return
    if not textures:
        return
    export(disc, work, ["MON/%s%d" % (prefix, costume)], dest="skins/" + sid)
    print("edit the PNGs in %s, then: modkit.py pack %s" % (os.path.join(work, "skins", sid), mod))


# ---------------------------------------------------------------- monstros custom (MODS.md, "Monstros custom")

def custom_id_error(cid):
    """Mensagem (em ingles) se o id nao serve para um monstro custom; None se serve. Nome ou prefixo de um jogavel nao serve: a
    skin e o WOTM_MATCH acham o do jogo antes (o de um chefe serve: o mod Cerebulon tem o custom "jelly")."""
    if not CUSTOM_ID_RE.fullmatch(cid or ""):
        return "the id takes only lowercase letters, digits and _ (1 to 32, starting with a letter)"
    if game_monster(cid):
        return "%r is a playable monster of the game; pick another id" % cid
    return None


def monster_dir(mods, mod, cid):
    return os.path.join(mods, mod, "monsters", cid)


def list_mod_monsters(mods, mod):
    """{pasta: monster.json} dos monstros custom de mods/<mod>/monsters/ (pasta = id do monster_new)."""
    d = os.path.join(mods, mod, "monsters")
    out = {}
    for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        js = os.path.join(d, n, "monster.json")
        if os.path.isfile(js):
            out[n] = _json_obj(js)
    return out


def write_monster_json(mods, mod, cid, data):
    os.makedirs(monster_dir(mods, mod, cid), exist_ok=True)
    with open(os.path.join(monster_dir(mods, mod, cid), "monster.json"), "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


def list_narrators(mods):
    """Nomes de narrator/<nome>.mp3 que o port acha (o do repo e o ao lado da pasta de mods, como o wotm_narrator.inc)."""
    out = set()
    for d in (os.path.join(os.path.dirname(os.path.abspath(mods)), "narrator"), os.path.join(REPO, "narrator")):
        for n in os.listdir(d) if os.path.isdir(d) else []:
            if n.lower().endswith(".mp3"):
                out.add(n[:-4].lower())
    return sorted(out)


def monster_new(disc, work, mods, mod, cid, base, costume=None, name=None, ai=False, camera=None, narrator=None, textures=True,
                files=None):
    """Cria mods/<mod>/monsters/<id>/monster.json. textures: exporta as texturas do traje do disco (base + costume) para
    <work>/monsters/<id>/files/; o pack leva os PNGs editados para a pasta files/ do monstro, que vira o traje proprio dele ("files":
    true; valem so para ele). files None = textures. costume None = o padrao do tipo (BOSS_DEFAULTS; jogavel 0). camera None = a
    padrao do tipo, False = a do proprio tipo, (from, escala em %). work None = work_dir(mods, mod)."""
    validate_mod_name(mod, "monster new")
    if work is None:
        work = work_dir(mods, mod)
    if custom_id_error(cid):
        raise SystemExit("monster new: " + custom_id_error(cid))
    bname = base_name(base or "")
    if not bname:
        raise SystemExit("monster new: unknown base %r (one of: %s, %s)" % (base, ", ".join(MONSTERS), ", ".join(CUSTOM_BASES)))
    prefix = base_prefix(bname)
    dco, dcam = BOSS_DEFAULTS.get(bname, (0, None))
    costume = dco if costume is None else costume
    if costume not in range(4) or not disc_file(disc, "MON/%s%d.NGP" % (prefix, costume)):
        raise SystemExit("monster new: the disc has no costume %s of %s (MON/%s%s.NGP)" % (costume, bname, prefix, costume))
    camera = dcam if camera is None else camera
    if camera:
        if not base_name(camera[0]) or not 10 <= int(camera[1]) <= 1000:
            raise SystemExit("monster new: camera is <monster>:<scale %%, 10 to 1000> (e.g. togera:200)")
    files = textures if files is None else files
    folder = monster_dir(mods, mod, cid)
    js = os.path.join(folder, "monster.json")
    if os.path.exists(js):
        raise SystemExit("monster new: %s already exists" % js)
    data = {"id": cid, "name": (name or cid).upper(), "base": bname, "costume": costume}
    if files:
        data["files"] = True
        os.makedirs(os.path.join(folder, "files"), exist_ok=True)
    if camera:
        data["camera"] = {"from": base_name(camera[0]), "scale": int(camera[1])}
    if narrator:
        data["narrator"] = narrator.lower()
    data["ai"] = bool(ai)
    write_monster_json(mods, mod, cid, data)
    print("custom monster %s (%s): %s" % (cid, data["name"], js))
    print("base %s, costume %d%s" % (bname, costume, ", camera of %s x%.2f" % (data["camera"]["from"], data["camera"]["scale"] / 100.)
                                      if camera else ""))
    if textures:
        export(disc, work, ["MON/%s%d" % (prefix, costume)], dest="monsters/%s/files" % cid)
        print("edit the PNGs in %s, then: modkit.py pack %s" % (os.path.join(work, "monsters", cid, "files"), mod))
    print("in the game: the CUSTOM box of the selection, or WOTM_MATCH=%s,1,1" % cid)


def monster_portrait(disc, work, mods, mod, cid, png):
    """Retrato do HUD: copia o PNG para monsters/<id>/portrait.png (no mod e na pasta de trabalho) e grava "portrait" e o
    "portraitTex" (o texId do retrato no .RTX do traje do disco) no monster.json."""
    js = os.path.join(monster_dir(mods, mod, cid), "monster.json")
    data = _json_obj(js)
    if not data:
        raise SystemExit("portrait: no monster.json in %s" % monster_dir(mods, mod, cid))
    if png_size(png) is None:
        raise SystemExit("portrait: %s is not a PNG" % png)
    prefix = base_prefix(str(data.get("base", "")))
    co = data.get("costume", 0) if isinstance(data.get("costume"), int) else 0
    tid = portrait_tex(disc, prefix, co) if prefix else None
    if tid is None:
        raise SystemExit("portrait: could not find the portrait texture in MON/%s%d.RTX" % (prefix, co))
    w, h = png_size(png)
    for d in (monster_dir(mods, mod, cid), os.path.join(work, "monsters", cid)):
        os.makedirs(d, exist_ok=True)
        dst = os.path.join(d, "portrait.png")
        if os.path.abspath(dst) != os.path.abspath(png):
            shutil.copyfile(png, dst)
    st = load_state(work)
    st["files"]["monsters/%s/portrait.png" % cid] = {"sha1": sha1(os.path.join(work, "monsters", cid, "portrait.png"))}
    save_state(work, st)
    data["portrait"] = "portrait.png"
    data["portraitTex"] = tid
    write_monster_json(mods, mod, cid, data)
    print("portrait of %s: %dx%d (game 128x64), texture %d of %s%d.RTX" % (cid, w, h, tid, prefix, co))
    if w * 64 != h * 128:
        print("WARNING: the HUD portrait is 2:1 (128x64); %dx%d will look stretched" % (w, h))


def monster_model(work, mods, mod, cid, paths):
    """Modelo proprio: copia .NGP e .PTR (e .TEX/.RTX, opcionais) para <work>/monsters/<id>/files/ como editados (o pack os leva)
    e liga "files" no monster.json. O .NGP precisa do esqueleto e das animacoes do tipo base."""
    js = os.path.join(monster_dir(mods, mod, cid), "monster.json")
    data = _json_obj(js)
    if not data:
        raise SystemExit("model: no monster.json in %s" % monster_dir(mods, mod, cid))
    by_ext = {}
    for p in paths:
        ext = os.path.splitext(p)[1].upper()
        if ext not in (".NGP", ".PTR", ".TEX", ".RTX"):
            raise SystemExit("model: %s is not a .NGP, .PTR, .TEX or .RTX" % p)
        if ext in by_ext:
            raise SystemExit("model: two %s files (%s, %s)" % (ext, by_ext[ext], p))
        by_ext[ext] = p
    d = os.path.join(work, "monsters", cid, "files")
    have = {os.path.splitext(n)[1].upper() for n in os.listdir(d)} if os.path.isdir(d) else set()
    if (".NGP" in by_ext or ".NGP" in have) != (".PTR" in by_ext or ".PTR" in have):
        raise SystemExit("model: .NGP and .PTR go together (the .PTR relocates the .NGP)")
    os.makedirs(d, exist_ok=True)
    st = load_state(work)
    for ext, p in sorted(by_ext.items()):
        for n in os.listdir(d):   # um arquivo de cada tipo: o novo substitui o anterior
            if os.path.splitext(n)[1].upper() == ext:
                os.remove(os.path.join(d, n))
                st["files"].pop("monsters/%s/files/%s" % (cid, n), None)
        n = os.path.basename(p)
        shutil.copyfile(p, os.path.join(d, n))
        st["files"]["monsters/%s/files/%s" % (cid, n)] = {"sha1": "", "imported": True}   # sha1 vazio: conta como editado
        print("model file %s -> work folder monsters/%s/files/" % (n, cid))
    save_state(work, st)
    if not data.get("files"):
        data["files"] = True
        write_monster_json(mods, mod, cid, data)
        print("monster.json of %s: \"files\": true" % cid)
    print("build the mod (modkit.py pack %s) to send them to the mod" % mod)


def monster_duplicate(mods, mod, work, cid, new):
    """Copia o monstro cid para new: a pasta no mod (monster.json com o id novo), a pasta de trabalho e o estado."""
    err = custom_id_error(new)
    if err:
        raise SystemExit("duplicate monster: " + err)
    src, dst = monster_dir(mods, mod, cid), monster_dir(mods, mod, new)
    if os.path.exists(dst) or os.path.exists(os.path.join(work, "monsters", new)):
        raise SystemExit("duplicate monster: there is already a monster %r in the mod" % new)
    shutil.copytree(src, dst)
    data = _json_obj(os.path.join(dst, "monster.json"))
    data["id"] = new
    write_monster_json(mods, mod, new, data)
    wsrc = os.path.join(work, "monsters", cid)
    if os.path.isdir(wsrc):
        shutil.copytree(wsrc, os.path.join(work, "monsters", new))
    st = load_state(work)
    pre = "monsters/%s/" % cid
    for k, v in list(st["files"].items()):
        if k.startswith(pre):
            st["files"]["monsters/%s/%s" % (new, k[len(pre):])] = dict(v)
    save_state(work, st)
    print("monster %s copied to %s (%s)" % (cid, new, dst))


def monster_delete(mods, mod, work, cid):
    """Apaga o monstro custom: a pasta no mod, a pasta de trabalho e as chaves dele no estado. As skins dele ficam (avisa)."""
    shutil.rmtree(monster_dir(mods, mod, cid), ignore_errors=True)
    shutil.rmtree(os.path.join(work, "monsters", cid), ignore_errors=True)
    st = load_state(work)
    pre = "monsters/%s/" % cid
    st["files"] = {k: v for k, v in st["files"].items() if not k.startswith(pre)}
    save_state(work, st)
    print("monster %s deleted" % cid)
    left = [s for s, v in list_skins(mods, mod).items() if str(v.get("monster", "")).lower() == cid]
    if left:
        print("WARNING: skin(s) of %s left in the mod: %s (the game skips them)" % (cid, ", ".join(left)))


def find_disc_ext(disc, d, stem, ext):
    return disc_file(disc, ("%s/%s%s" % (d, stem, ext)) if d else stem + ext)


def inject_png(disc, dst, stem, rtx, tid, png):
    """Regrava a textura no .TEX/.RTX do mod (ou do disco), mesmo tamanho: so para a imagem do GS emulado."""
    ext = ".RTX" if rtx else ".TEX"
    rel = find_texfile(disc, stem, ext)
    if not rel:
        print("  --inject: %s%s not found on the disc" % (stem, ext))
        return
    out = os.path.join(dst, *rel.split("/"))
    src = out if os.path.exists(out) else disc_file(disc, rel)
    w, h, _ = tex.read_image(png)
    pk = next((p for p in tex.parse(tex.read_file(src)[0])["packets"] if p.texId == tid), None)
    if pk is None or (w, h) != (pk.w, pk.h):
        print("  --inject: %s skipped (needs %dx%d, the original size)" % (os.path.basename(png), pk.w if pk else 0,
                                                                                         pk.h if pk else 0))
        return
    os.makedirs(os.path.dirname(out), exist_ok=True)
    rtx_path = None if rtx else disc_file(disc, rel[:-4] + ".RTX")
    ngp = disc_file(disc, rel[:-4] + ".NGP")
    tex.cmd_inject(src, tid, png, out + ".tmp", rtx_path, None, ngp)
    os.replace(out + ".tmp", out)


# ---------------------------------------------------------------- check

def _json_obj(p):
    try:
        with open(p, encoding="utf-8") as f:
            v = json.load(f)
        return v if isinstance(v, dict) else {}
    except (OSError, ValueError):
        return {}


def conflict_key(mod_dir, rel):
    """Chave do que o arquivo troca no jogo, para achar dois mods trocando a mesma coisa; None se nao tem uma propria.
    PNG: (stem, rtx, texId); skin: monster/id (a pasta da skin vale inteira); monstro custom: id; o resto: o caminho."""
    parts = rel.split("/")
    n = parts[-1]
    if len(parts) >= 3 and parts[0].lower() == "skins":
        if len(parts) == 3 and n.lower() == "skin.json":
            v = _json_obj(os.path.join(mod_dir, *parts))
            mon = v.get("monster") if isinstance(v.get("monster"), str) else ""
            mon = game_monster(mon) or mon.lower()
            sid = (v.get("id") if isinstance(v.get("id"), str) else parts[1]).lower()
            return ("skin", "%s/%s" % (mon, sid))
        return None
    if len(parts) >= 3 and parts[0].lower() == "monsters":
        if len(parts) == 3 and n.lower() == "monster.json":
            v = _json_obj(os.path.join(mod_dir, *parts))
            return ("custom", (v.get("id") if isinstance(v.get("id"), str) else parts[1]).lower())
        return None
    m = PNG_RE.match(n)
    if m:
        return ("png", m.group(1).upper(), bool(m.group(2)), int(m.group(3)))
    if n.lower() == "modkit.json":
        return None
    return ("file", rel.upper())


def key_label(key):
    if key[0] == "png":
        return "texture %s%s t%d" % (key[1], ".RTX" if key[2] else ".TEX", key[3])
    if key[0] == "skin":
        return "skin %s" % key[1]
    if key[0] == "custom":
        return "custom monster %s" % key[1]
    return "file %s" % key[1]


def mod_files(mods, mod):
    """[(rel, caminho)] dos arquivos de mods/<mod>/, em ordem."""
    out = []
    base = os.path.join(mods, mod)
    for root, dirs, files in os.walk(base):
        dirs.sort()
        for n in sorted(files):
            p = os.path.join(root, n)
            out.append((os.path.relpath(p, base).replace("\\", "/"), p))
    return out


def check(disc, mods, only=None):
    if not os.path.isdir(mods):
        raise SystemExit("no mods folder at %s" % mods)
    names = sorted(n for n in os.listdir(mods) if os.path.isdir(os.path.join(mods, n)) and n != WORK)
    if only and only not in names:
        raise SystemExit("check: no mod %r in %s" % (only, mods))
    owners = {}   # chave -> mods ligados que a trocam, em ordem alfabetica (vence o ultimo)
    for mod in names:
        if mod[0] in "_.":
            continue
        for rel, _p in mod_files(mods, mod):
            key = conflict_key(os.path.join(mods, mod), rel)
            if key and mod not in owners.setdefault(key, []):
                owners[key].append(mod)
    customs = find_customs(mods)
    problems = 0
    for mod in names:
        if only and mod != only:
            continue
        off = mod[0] in "_."
        print("%s%s" % (mod, " (disabled: starts with '_' or '.')" if off else ""))
        if off and not only:
            continue
        for rel, p in mod_files(mods, mod):
            n = os.path.basename(p)
            key = conflict_key(os.path.join(mods, mod), rel)
            if n.lower() == "modkit.json":
                continue
            note = ""
            m = PNG_RE.match(n)
            if key and key[0] in ("skin", "custom"):
                note = key_label(key)
                mon = key[1].split("/")[0]
                if key[0] == "skin" and mon not in MONSTERS and mon not in customs:
                    note += " WARNING: %r is not a playable monster nor the id of a custom monster; the game skips it" % mon
                if key[0] == "custom":
                    v = _json_obj(p)
                    if not base_prefix(str(v.get("base", ""))):
                        note += " WARNING: \"base\" %r is not a type the port accepts (%s, %s); the game skips it" % (
                            v.get("base"), ", ".join(MONSTERS), ", ".join(CUSTOM_BASES))
                        problems += 1
                    elif not CUSTOM_ID_RE.fullmatch(mon):
                        note += " WARNING: bad \"id\" (%s); the game skips it" % custom_id_error(mon)
                        problems += 1
                    elif v.get("files") and not os.path.isdir(os.path.join(os.path.dirname(p), v["files"] if isinstance(v["files"], str) else "files")):
                        note += " WARNING: \"files\" folder missing; the game uses the disc costume"
            elif m:
                stem, rtx, tid = m.group(1), bool(m.group(2)), int(m.group(3))
                trel = find_texfile(disc, stem, ".RTX" if rtx else ".TEX")
                if not trel:
                    note, problems = "ERROR: %s%s is not on the disc" % (stem.upper(), ".RTX" if rtx else ".TEX"), problems + 1
                else:
                    modded = os.path.join(mods, mod, *trel.split("/"))
                    pk = next((q for q in tex.parse(tex.read_file(modded if os.path.exists(modded) else disc_file(disc, trel))[0])["packets"]
                               if q.texId == tid and not (rtx and q.is_palette())), None)
                    if pk is None:
                        note, problems = "ERROR: %s has no texId %d" % (trel, tid), problems + 1
                    else:
                        w, h, _ = tex.read_image(p) if n.lower().endswith(".png") else (0, 0, None)
                        ratio = w * pk.h == h * pk.w
                        note = "texture of %s, %dx%d (game %dx%d)%s" % (trel, w, h, pk.w, pk.h,
                                                                        "" if ratio else " WARNING: different aspect ratio, it will look stretched")
            else:
                orig = disc_file(disc, rel)
                size = os.path.getsize(p)
                if not orig:
                    note = "new file (not on the disc; nothing asks for it yet)" if key else "part of the %s" % rel.split("/")[0][:-1]
                else:
                    osz = os.path.getsize(orig)
                    note = "%d bytes (disc %d)" % (size, osz)
                    if size > osz and os.path.splitext(n)[1].upper() in (".TEX", ".RTX", ".NGP", ".BNK"):
                        note += " WARNING: larger than the disc file; the game loads into fixed regions and may overwrite what comes next"
            if not off and key:
                own = owners.get(key, [])
                i = own.index(mod)
                if i > 0:
                    note += " (overrides %s)" % own[i - 1]
                if i < len(own) - 1:
                    note += " (overridden by %s)" % own[-1]
            print("  %-40s %s" % (rel, note))
    clash = sorted((k, own) for k, own in owners.items() if len(own) > 1 and (not only or only in own))
    print()
    if clash:
        print("conflicts between enabled mods (%d; the last in alphabetical order wins, as the game loads them):" % len(clash))
        for k, own in clash:
            print("  %-40s %s -> %s wins" % (key_label(k), ", ".join(own), own[-1]))
    else:
        print("no conflicts between enabled mods")
    print("%d error(s)" % problems)
    return 1 if problems else 0


def main(argv):
    opts, args, i = {}, [], 0
    while i < len(argv):
        if argv[i] in ("--disc", "--mods", "--work", "--base", "--name", "--costume", "--camera", "--narrator"):
            if i + 1 >= len(argv):
                raise SystemExit("%s needs a value" % argv[i])
            opts[argv[i][2:]] = argv[i + 1]
            i += 2
        elif argv[i] in ("--inject", "--ai", "--no-textures"):
            opts[argv[i][2:]] = True
            i += 1
        else:
            args.append(argv[i])
            i += 1
    if not args or args[0] not in ("export", "pack", "check", "revert", "skin", "monster"):
        print(__doc__)
        return 1
    disc = opts.get("disc") or default_disc()
    mods = opts.get("mods") or default_mods(disc)
    if not os.path.isdir(disc):
        raise SystemExit("extracted disc not found at %s (--disc or WOTM_DISC)" % disc)

    def work_of(cmd, mod):
        validate_mod_name(mod, cmd)
        w = opts.get("work") or work_dir(mods, mod)
        adopt_mod(disc, mods, mod, w)   # mod feito antes de ter pasta de trabalho: entra nela como editado
        return w

    if args[0] == "export":
        if len(args) < 3:
            raise SystemExit("export <mod> <files...>: the mod and what to extract (e.g. export my_mod MON/LIZARD0)")
        export(disc, work_of("export", args[1]), args[2:])
    elif args[0] == "pack":
        if len(args) != 2:
            raise SystemExit("pack <mod>: one mod name (not starting with '_' or '.')")
        pack(disc, work_of("pack", args[1]), mods, args[1], opts.get("inject", False))
    elif args[0] == "skin":
        if len(args) != 5 or args[1] != "new":
            raise SystemExit("skin new <mod> <monster> <id> [--base N] [--name \"GREEN TOGERA\"] [--ai]")
        base = opts.get("base")
        if base is not None:
            try:
                base = int(base)
            except ValueError:
                raise SystemExit("skin new: --base is a disc costume, 0 to 3")
        skin_new(disc, work_of("skin new", args[2]), mods, args[2], args[3], args[4], base, opts.get("name"), opts.get("ai", False))
    elif args[0] == "monster":
        use = ("monster new <mod> <id> --base <monster> [--costume N] [--name \"X\"] [--ai] [--camera togera:200 | --camera none] "
               "[--narrator <name>] [--no-textures]\nmonster portrait <mod> <id> <png>\nmonster model <mod> <id> <.NGP> <.PTR> [.TEX] [.RTX]")
        if len(args) < 4 or args[1] not in ("new", "portrait", "model"):
            raise SystemExit(use)
        w = work_of("monster " + args[1], args[2])
        if args[1] == "new":
            if len(args) != 4 or not opts.get("base"):
                raise SystemExit(use)
            co = opts.get("costume")
            try:
                co = None if co is None else int(co)
            except ValueError:
                raise SystemExit("monster new: --costume is a disc costume, 0 to 3")
            cam = opts.get("camera")
            if cam is not None:
                if cam.lower() == "none":
                    cam = False
                else:
                    f, _, sc = cam.partition(":")
                    try:
                        cam = (f, int(sc or "100"))
                    except ValueError:
                        raise SystemExit("monster new: --camera is <monster>:<scale %> (e.g. togera:200) or none")
            monster_new(disc, w, mods, args[2], args[3], opts["base"], co, opts.get("name"), opts.get("ai", False), cam,
                        opts.get("narrator"), not opts.get("no-textures", False))
        elif args[1] == "portrait":
            if len(args) != 5:
                raise SystemExit(use)
            monster_portrait(disc, w, mods, args[2], args[3], args[4])
        else:
            if len(args) < 5:
                raise SystemExit(use)
            monster_model(w, mods, args[2], args[3], args[4:])
    elif args[0] == "revert":
        if len(args) < 3:
            raise SystemExit("revert <mod> <files...>: the mod and files of its work folder (e.g. revert my_mod MON/lizard0_t0004.png)")
        revert(disc, work_of("revert", args[1]), args[2:])
    else:
        if len(args) > 2:
            raise SystemExit("check [mod]: all mods, or one")
        return check(disc, mods, args[1] if len(args) > 1 else None)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
