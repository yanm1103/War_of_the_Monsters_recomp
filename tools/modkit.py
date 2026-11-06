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
  python tools/modkit.py monster stats my_mod fire_togera                     # .CHR e .AIC do tipo base para editar (so dele)
  python tools/modkit.py monster sounds my_mod fire_togera                    # sons do banco do tipo base em WAV (banco so dele)
  python tools/modkit.py monster mesh export my_mod fire_togera               # malha e animacoes do traje base em .glb (Blender)
  python tools/modkit.py monster mesh import my_mod fire_togera [editado.glb] # a malha editada vira o .NGP/.PTR do custom
  python tools/modkit.py monster anim import my_mod fire_togera [editado.glb] # as animacoes editadas tambem
  python tools/modkit.py monster rig my_mod lizardman rig.glb [--front +y|-y] [--scale S | --height H]   # esqueleto proprio
  python tools/modkit.py skin mesh export my_mod green / skin mesh import my_mod green [editado.glb]   # o mesmo numa skin
  python tools/modkit_gui.py                                 # o mesmo com interface (botao "Mod kit" do launcher)

Opcoes: --disc <pasta ou .iso> (disco extraido ou a ISO; padrao WOTM_DISC ou disc/ do repo; no WotM_ModKit.exe, o iso= do
wotm.ini), --mods <pasta> (padrao: <runtime>/discroot/mods, o runtime de .wotm_recomp_dir; sem ele, <disco>/mods; no exe, a do jogo), --work <pasta> (padrao: <mods>/_work/<mod>; pasta com '_' o jogo nao le).
Nome de mod: nao comeca com '_' nem '.', sem \\ / : * ? " < > |.
Arquivos do export podem ter curinga (MON/LIZARD*) e sem extensao pegam .TEX e .RTX.

O que cada tipo vira:
- .TEX / .RTX -> <arquivo>_tNNNN.png / <arquivo>_rtx_tNNNN.png (tools/tex.py export). No pack o PNG vai como esta: o desenho nativo
  troca a textura na hora de desenhar (native/wotm_texmods.hpp), em qualquer resolucao. Com --inject o pack tambem regrava o
  .TEX/.RTX (mesmo tamanho e cores da paleta atual), para a imagem do GS emulado (referencia da checagem VU1).
- .VPK / .VAG -> <arquivo>.wav; volta pelo tools/snd989.py (mesma taxa e volumes do original).
- .BNK -> <banco>_sNNN.wav, um por som com amostra; os editados voltam todos num banco so (snd989 bnk-replace). Um
  <banco>_sNNN_aK.wav (feito a mao) troca so a K-esima amostra do som e mantem as camadas dele.
- Malha e animacoes (skin ou custom) -> <PREFIXO><traje>.glb (tools/monglb.py --edit, precisa do numpy); voltam pelo
  tools/ngpmesh.py (malha) e tools/ngpanim.py (animacoes) como .NGP + .PTR, remontados do traje do disco com os dois .glb
  importados (.blender.json na pasta). O pack nao leva .glb/.blend (MESH_SRC).
- Esqueleto proprio (custom): um .glb com armature, malha, acoes a<id> e localizadores -> .NGP/.PTR/.TEX/.RTX e os PNGs das
  texturas novas, montados sobre o traje base do disco (tools/ngprig.py); depois dele o mesh/anim import recusam esse custom.
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
import iso9660  # noqa: E402

# FROZEN: o WotM_ModKit.exe do release (PyInstaller), ao lado do wotm.exe; REPO vira a pasta do jogo (mods/ do zip)
FROZEN = bool(getattr(sys, "frozen", False))
REPO = os.path.dirname(os.path.abspath(sys.executable)) if FROZEN else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEX_DIRS = ("LVL", "MON", "SHELL")
PNG_RE = re.compile(r"^(.+?)(_rtx)?_t(\d+)(?:_sempaleta)?\.png$", re.I)   # _sempaleta: o export nao achou a paleta (cinza)
BNK_RE = re.compile(r"^(.+)_s(\d+)(?:_a(\d+))?\.wav$", re.I)   # _aK: so a K-esima amostra de um som de camadas
MESH_SRC = (".glb", ".gltf", ".blend", ".blend1")   # fonte da malha na pasta de trabalho (mesh export): o pack nao leva


def sha1(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest()


def pack_hint(mod):
    """Como mandar o mod para o jogo, na mensagem: no exe so ha a interface."""
    return "the Build mod button" if FROZEN else "modkit.py pack %s, or the Build mod button" % mod


def game_data_dir():
    """Pasta de dados do wotm.exe do release (native/runtime/wotm_standalone.cpp): a do jogo, ou %LOCALAPPDATA%\\WotM quando o
    jogo esta numa pasta so de leitura. None fora do release (o kit rodando do repo)."""
    if not FROZEN:
        return None
    local = os.path.join(os.environ.get("LOCALAPPDATA", ""), "WotM")
    for d in (REPO, local):
        if os.path.isfile(os.path.join(d, "wotm.ini")):
            return d
    return REPO if _writable(REPO) else local


def _writable(d):
    """Como o writable() do wotm_standalone.cpp: cria e apaga um arquivo (os.access no Windows ignora as permissoes da pasta)."""
    probe = os.path.join(d, ".wotm_write_probe")
    try:
        with open(probe, "wb"):
            pass
        os.remove(probe)
        return True
    except OSError:
        return False


def ini_iso(data):
    """A ISO que o jogo usa (iso= do wotm.ini)."""
    try:
        with open(os.path.join(data, "wotm.ini"), encoding="utf-8-sig") as f:
            for line in f:
                if line.startswith("iso=") and line.strip()[4:]:
                    return line.strip()[4:]
    except OSError:
        pass
    return None


def default_disc():
    """WOTM_DISC; no repo, a pasta disc/; no release, a ISO que o jogo usa."""
    if os.environ.get("WOTM_DISC"):
        return os.environ["WOTM_DISC"]
    data = game_data_dir()
    if data:
        return ini_iso(data) or ""
    return os.path.join(REPO, "disc")


def default_mods(disc):
    data = game_data_dir()
    if data:   # como o wotm.exe: <dados>/mods, senao <jogo>/mods
        if not os.path.isdir(os.path.join(data, "mods")) and os.path.isdir(os.path.join(REPO, "mods")):
            return os.path.join(REPO, "mods")
        return os.path.join(data, "mods")
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


# ---------------------------------------------------------------- o disco: pasta extraida ou imagem .iso
# Com a ISO, cada arquivo pedido e copiado uma vez para um cache (disc_cache) e o resto do kit le de la como de uma pasta.

_isos = {}


def disc_iso(disc):
    """O leitor da ISO, se o disco e uma imagem; None se e uma pasta (ou nao existe)."""
    if not disc or not os.path.isfile(disc):
        return None
    key = os.path.abspath(disc)
    if key not in _isos:
        _isos[key] = iso9660.Iso(key)
    return _isos[key]


def disc_ok(disc):
    if disc and os.path.isdir(disc):
        return True
    try:
        return disc_iso(disc) is not None
    except (OSError, ValueError):
        return False


def disc_cache(disc):
    """Pasta onde os arquivos da ISO sao copiados (uma por imagem: caminho, tamanho e data)."""
    st = os.stat(disc)
    key = hashlib.sha1(("%s|%d|%d" % (os.path.abspath(disc).lower(), st.st_size, int(st.st_mtime))).encode()).hexdigest()[:12]
    base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
    return os.path.join(base, "WotM", "modkit_disc", key)


def disc_has(disc, rel):
    """O disco tem esse arquivo/pasta? (sem copiar nada da ISO)"""
    iso = disc_iso(disc)
    if iso:
        return iso.find(rel) is not None
    return disc_file(disc, rel) is not None


def disc_listdir(disc, rel=""):
    """Nomes de uma pasta do disco ([] se nao existe)."""
    iso = disc_iso(disc)
    if iso:
        return iso.listdir(rel)
    d = disc_file(disc, rel) if rel else disc
    return os.listdir(d) if d and os.path.isdir(d) else []


def _iso_file(iso, disc, rel):
    rel = rel.replace("\\", "/").strip("/")
    if not rel:
        os.makedirs(disc_cache(disc), exist_ok=True)
        return disc_cache(disc)
    parts, cur, names = rel.split("/"), "", []
    for p in parts:
        e = iso.find(cur + p)
        if not e:
            return None
        names.append(e[3])
        cur += e[3] + "/"
    out = os.path.join(disc_cache(disc), *names)
    if e[2]:
        os.makedirs(out, exist_ok=True)
        return out
    rel = "/".join(names)
    # .TEX/.RTX/.NGP: os irmaos tambem (o tex.py procura o .RTX e o .NGP ao lado)
    stem, ext = os.path.splitext(rel)
    sibs = [rel] + ([stem + x for x in (".TEX", ".RTX", ".NGP") if x != ext.upper()] if ext.upper() in (".TEX", ".RTX", ".NGP") else [])
    with iso.lock:   # a interface le retratos numa thread e extrai em outra: a mesma copia nao pode correr duas vezes
        for r in sibs:
            f = iso.find(r)
            q = os.path.join(disc_cache(disc), *r.split("/"))
            if f and not f[2] and not (os.path.isfile(q) and os.path.getsize(q) == f[1]):
                iso.extract(r, q)
    return out


def disc_file(disc, rel):
    """Caminho no disco sem depender de maiusculas (o disco extraido no Windows nao liga, o WSL liga). Com a ISO, a copia no cache."""
    iso = disc_iso(disc)
    if iso:
        return _iso_file(iso, disc, rel)
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
        if disc_has(disc, rel):
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
            for n in sorted(disc_listdir(disc, d)):
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
            for i in range(len(b["sounds"])):
                rate, chans = snd989.render_sound(b, i)   # camadas: a mistura como o jogo toca
                if not chans:
                    continue
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
    print("work folder: %s (edit there, then send it to the mod: %s)" % (work, pack_hint("<mod>")))


def bank_dir(d):
    """Pasta do disco do .BNK de um <banco>_sNNN.wav: o banco proprio de um custom (monsters/<id>/files) sai do banco do tipo em SND/."""
    return "SND" if d.startswith("monsters/") else d


def source_of(disc, rel):
    """Arquivo do disco de onde saiu um arquivo da pasta de trabalho ("MON/lizard0_t0004.png" -> "MON/LIZARD0.TEX"); None se nao achou."""
    d, n = os.path.split(rel.replace("\\", "/"))
    if d.startswith("monsters/") and BNK_RE.match(n):   # som do banco proprio de um custom: saiu do banco do tipo em SND/
        d = "SND"
    elif d.startswith("monsters/") and os.path.splitext(n)[1].upper() in (".CHR", ".AIC"):   # monster stats: de RST/
        d = "RST"
    elif d.startswith(("skins/", "monsters/")):   # PNG de skin ou de custom (skins/<id>/, monsters/<id>/files/): saiu do traje base em MON/
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
        if disc_has(disc, pre + c):
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
            rec = os.path.join(os.path.dirname(dst), BLENDER_REC)
            if os.path.splitext(rel)[1].upper() in (".NGP", ".PTR") and os.path.exists(rec):
                os.remove(rec)   # o modelo voltou ao do disco: o proximo import nao repoe o outro .glb
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
            if n.endswith(".json") or n.startswith(".") or n.lower().endswith(MESH_SRC):
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
            if mb and find_disc_ext(disc, bank_dir(d), mb.group(1), ".BNK"):
                k = int(mb.group(3)) if mb.group(3) is not None else -1
                banks.setdefault((d, mb.group(1)), []).append(((int(mb.group(2)), k), p))
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
        src = find_disc_ext(disc, bank_dir(d), stem, ".BNK")
        out = os.path.join(dst, *d.split("/"), stem.upper() + ".BNK")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        tmp = out + ".tmp"
        cur = src
        whole = {i for (i, k), _ in sounds if k < 0}
        for (i, k), wav in sorted(sounds):
            if k >= 0 and i in whole:   # o WAV do som inteiro ja juntou as camadas
                print("WARNING: %s skipped: %s_s%03d.wav replaces the whole sound" % (os.path.basename(wav), stem, i))
                continue
            snd989.bnk_replace(cur, i, wav, tmp, None if k < 0 else k)
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
# Especiais que um custom nao pega de outro tipo ("special"; specialNeeds em native/wotm_mods.hpp): usam um modelo que so o .NGP do
# dono traz
SPECIAL_NEEDS = {"robo-47": "the shock wave effect", "ultra-v": "the hook's ball", "preytor": "the bugs", "magmo": "the lava rocks",
                 "agamo": "the head", "zorgulon": "the disc", "assboss": "the cannon shells"}
# ... menos os que o rig import copia para os arquivos proprios (tools/ngpgraft.py SPECIALS)
SPECIAL_PARTS = ("robo-47",)


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
    print("edit the PNGs in %s, then: %s" % (os.path.join(work, "skins", sid), pack_hint(mod)))


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
    if costume not in range(4) or not disc_has(disc, "MON/%s%d.NGP" % (prefix, costume)):
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
        print("edit the PNGs in %s, then: %s" % (os.path.join(work, "monsters", cid, "files"), pack_hint(mod)))
    print("in the game: the CUSTOM box of the selection" + ("" if FROZEN else ", or WOTM_MATCH=%s,1,1" % cid))


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
    put_model_files(work, "monsters/%s/files" % cid, by_ext)
    files_on(mods, mod, cid, data)
    print("send them to the mod: %s" % pack_hint(mod))


def put_model_files(work, rel, by_ext):
    """Copia {".NGP": caminho, ...} para <work>/<rel>/ como editados (o pack os leva); um arquivo de cada tipo: o novo substitui o
    anterior, com qualquer nome."""
    d = os.path.join(work, *rel.split("/"))
    os.makedirs(d, exist_ok=True)
    st = load_state(work)
    for ext, p in sorted(by_ext.items()):
        for n in os.listdir(d):
            if os.path.splitext(n)[1].upper() == ext:
                os.remove(os.path.join(d, n))
                st["files"].pop("%s/%s" % (rel, n), None)
        n = os.path.basename(p)
        shutil.copyfile(p, os.path.join(d, n))
        st["files"]["%s/%s" % (rel, n)] = {"sha1": "", "imported": True}   # sha1 vazio: conta como editado
        print("model file %s -> work folder %s/" % (n, rel))
    save_state(work, st)


def files_on(mods, mod, cid, data):
    """Liga "files" no monster.json do custom (a pasta files/ com o que e so dele)."""
    if not data.get("files"):
        data["files"] = True
        write_monster_json(mods, mod, cid, data)
        print("monster.json of %s: \"files\": true" % cid)


def monster_stats(disc, work, mods, mod, cid):
    """Numeros da luta (RST/<PREFIXO>.CHR) e IA (RST/<PREFIXO>0..2.AIC) do tipo base, do disco para <work>/monsters/<id>/files/.
    Os editados o pack leva para a pasta files/ do monstro, e o port os le no lugar dos do tipo, so para este custom (MODS.md,
    "Custom monsters"). Liga "files" no monster.json."""
    js = os.path.join(monster_dir(mods, mod, cid), "monster.json")
    data = _json_obj(js)
    if not data:
        raise SystemExit("stats: no monster.json in %s" % monster_dir(mods, mod, cid))
    prefix = base_prefix(str(data.get("base", "")))
    if not prefix:
        raise SystemExit("stats: \"base\" %r is not a type the port accepts" % data.get("base"))
    rels = ["RST/%s.CHR" % prefix] + ["RST/%s%d.AIC" % (prefix, d) for d in range(3) if disc_has(disc, "RST/%s%d.AIC" % (prefix, d))]
    export(disc, work, rels, dest="monsters/%s/files" % cid)
    if not data.get("files"):
        data["files"] = True
        write_monster_json(mods, mod, cid, data)
        print("monster.json of %s: \"files\": true" % cid)
    os.makedirs(os.path.join(monster_dir(mods, mod, cid), "files"), exist_ok=True)
    print("edit them in %s (python tools/chr_dump.py <.CHR> lists the punches), then: %s" % (
        os.path.join(work, "monsters", cid, "files"), pack_hint(mod)))


# Banco de som de cada tipo base (SoundManager::loadMonsterBank; o Goliath usa o do chefe, o Jelly e a forma final o da fase final)
MONSTER_BANKS = {"MONKEY": "APE_S", "ROBOT": "ROBOT_S", "LIZARD": "LIZARD_S", "SHOGUN": "SHOGUN_S", "MANTIS": "MANTIS_S",
                 "ENERGY": "ENERGY_S", "LAVA": "LAVA_S", "DRAGON": "DRAGON_S", "ROCK": "ROKMAN_S", "ALIEN": "ALIEN_S",
                 "ASSBOSS": "ASSEMB_S", "JELLY": "FINAL_S", "FINAL": "FINAL_S"}


def monster_sounds(disc, work, mods, mod, cid):
    """Sons do banco do tipo base (SND/<BANCO>.BNK), do disco para <work>/monsters/<id>/files/ como <banco>_sNNN.wav. O pack
    monta com os editados um .BNK na pasta files/ do monstro, e o port toca os sons deste custom dele, com os mesmos indices do
    banco do tipo (MODS.md, "Custom monsters"). Liga "files" no monster.json."""
    js = os.path.join(monster_dir(mods, mod, cid), "monster.json")
    data = _json_obj(js)
    if not data:
        raise SystemExit("sounds: no monster.json in %s" % monster_dir(mods, mod, cid))
    prefix = base_prefix(str(data.get("base", "")))
    bank = MONSTER_BANKS.get(prefix)
    if not bank:
        raise SystemExit("sounds: \"base\" %r has no sound bank" % data.get("base"))
    export(disc, work, ["SND/%s.BNK" % bank], dest="monsters/%s/files" % cid)
    if not data.get("files"):
        data["files"] = True
        write_monster_json(mods, mod, cid, data)
        print("monster.json of %s: \"files\": true" % cid)
    os.makedirs(os.path.join(monster_dir(mods, mod, cid), "files"), exist_ok=True)
    print("replace the WAVs you want in %s (same number = same sound), then: %s" % (
        os.path.join(work, "monsters", cid, "files"), pack_hint(mod)))


# ---------------------------------------------------------------- malha nova no esqueleto do tipo (MODELS_3D.md, tools/ngpmesh.py)

def mesh_target(mods, mod, kind, tid):
    """Malha de uma skin (kind "skin") ou de um custom ("monster") do mod: (pasta na de trabalho, "MON/<PREFIXO><traje>" do traje
    base). A skin vai na pasta dela; o custom, em monsters/<id>/files."""
    if kind == "skin":
        data = list_skins(mods, mod).get(tid)
        if not data:
            raise SystemExit("mesh: no skin %r in mod %s (skin new first)" % (tid, mod))
        mkey = str(data.get("monster", "")).lower()
        game = game_monster(mkey)
        custom = None if game else find_customs(mods).get(mkey)
        if not game and not custom:
            raise SystemExit("mesh: skin %s is of %r, a monster the kit does not find in the mods" % (tid, mkey))
        prefix = MONSTERS[game] if game else custom["prefix"]
        costume = data.get("base", 0 if game else custom["costume"])
        rel = "skins/" + tid
    else:
        data = _json_obj(os.path.join(monster_dir(mods, mod, tid), "monster.json"))
        if not data:
            raise SystemExit("mesh: no monster.json in %s" % monster_dir(mods, mod, tid))
        prefix = base_prefix(str(data.get("base", "")))
        if not prefix:
            raise SystemExit("mesh: \"base\" %r is not a type the port accepts" % data.get("base"))
        costume = data.get("costume", 0)
        rel = "monsters/%s/files" % tid
    if not isinstance(costume, int) or costume not in range(4):
        raise SystemExit("mesh: base costume %r is not 0 to 3" % (costume,))
    return rel, "MON/%s%d" % (prefix, costume)


def mesh_glb(work, mods, mod, kind, tid):
    """Onde o mesh export grava o .glb (e o mesh import o procura sem caminho)."""
    rel, stem = mesh_target(mods, mod, kind, tid)
    return os.path.join(work, *rel.split("/"), os.path.basename(stem) + ".glb")


def mesh_export(disc, work, mods, mod, kind, tid):
    """Traje base em <work>/<pasta>/<PREFIXO><traje>.glb para o Blender (tools/monglb.py --edit: malha, esqueleto com uma junta
    por transformacao do jogo, pesos, texturas e todas as animacoes). Volta pelo mesh import (malha) e pelo anim import
    (animacoes). Nao sobrescreve um .glb que ja esta la. Devolve o caminho."""
    rel, stem = mesh_target(mods, mod, kind, tid)
    out = mesh_glb(work, mods, mod, kind, tid)
    rig = (_json_obj(_blender_rec(work, rel)) or {}).get("rig")
    if rig:
        print("%s has a skeleton of its own: edit %s and import it again with rig import" % (tid, rig))
        return rig
    if os.path.exists(out):
        print("%s already exists; kept (delete it to export again)" % out)
        return out
    src = disc_file(disc, stem + ".NGP")
    if not src:
        raise SystemExit("mesh export: %s.NGP is not on the disc" % stem)
    try:
        import monglb   # numpy: so este comando precisa
    except ImportError as e:
        raise SystemExit("mesh export needs numpy (pip install numpy): %s" % e)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    # --vertex-alpha: o alfa do vertice volta ao jogo (teste de alfa) mesmo sem mistura
    monglb.main([src, out, "--vertex-alpha", "--edit"])
    print("%s -> %s" % (stem, out))
    print("in Blender: remodel the mesh (keep the bone names and the armature in place) and/or pose and key the bones in "
          "the animations (a<id>); export it as .glb, then: mesh import and/or anim import (MODELS_3D.md)")
    return out


BLENDER_REC = ".blender.json"   # na pasta de trabalho do modelo: os .glb importados (malha, animacoes); o pack nao leva


def _blender_rec(work, rel):
    return os.path.join(work, *rel.split("/"), BLENDER_REC)


def model_import(disc, work, mods, mod, kind, tid, glb=None, what="mesh"):
    """O .glb editado no Blender (padrao: o do mesh_export) vira o .NGP + .PTR da skin ou do custom, na pasta de trabalho (o pack
    os leva). what="mesh": a malha (tools/ngpmesh.py); "anims": as animacoes que mudaram (tools/ngpanim.py). O modelo e
    sempre remontado do traje do disco com os dois: o outro .glb importado antes (guardado em .blender.json) entra de novo,
    entao importar um nao desfaz o outro e o arquivo nao cresce a cada import. Texturas: as do traje base (e os PNGs)."""
    import tempfile
    import ngpanim
    import ngpmesh
    cmd = "%s import" % ("mesh" if what == "mesh" else "anim")
    rel, stem = mesh_target(mods, mod, kind, tid)
    rec = _json_obj(_blender_rec(work, rel)) or {}
    if rec.get("rig"):
        raise SystemExit("%s: %s has a skeleton of its own (rig import of %s): edit that .glb and import it again with "
                         "rig import (Revert on the .NGP goes back to the base's skeleton)" % (cmd, tid, rec["rig"]))
    glb = os.path.abspath(glb or mesh_glb(work, mods, mod, kind, tid))
    if not os.path.isfile(glb):
        raise SystemExit("%s: %s not found (export it, edit it in Blender, export the .glb)" % (cmd, glb))
    src = disc_file(disc, stem + ".NGP")
    if not src or not disc_file(disc, stem + ".PTR"):
        raise SystemExit("%s: %s.NGP/.PTR are not on the disc" % (cmd, stem))
    rec["mesh" if what == "mesh" else "anims"] = glb
    for k in ("mesh", "anims"):
        if rec.get(k) and not os.path.isfile(rec[k]):
            print("warning: %s (imported before as %s) is gone; the model goes without it" % (rec[k], k))
            rec.pop(k)
    tmp = tempfile.mkdtemp(prefix="wotm_model_")
    name = os.path.basename(stem)
    out = os.path.join(tmp, name + ".NGP")
    try:
        if rec.get("mesh"):
            try:
                ngpmesh.import_glb(src, rec["mesh"], tmp, name=name, textures=False)
            except ngpmesh.MeshError as e:
                raise SystemExit("%s: mesh of %s: %s" % (cmd, rec["mesh"], e))
        anims = {}
        if rec.get("anims"):
            try:
                anims = ngpanim.import_glb(src, rec["anims"], tmp, name=name, into=out if os.path.exists(out) else None)
            except (ngpanim.AnimError, ngpmesh.MeshError) as e:
                raise SystemExit("%s: animations of %s: %s" % (cmd, rec["anims"], e))
            if not anims and what == "anims":
                rec.pop("anims")
        if anims:
            rec["anim_ids"] = sorted(str(k) for k in anims)
        else:
            rec.pop("anim_ids", None)
        if not os.path.exists(out):
            if what == "anims":
                print("anim import: no animation differs from the game's; the model was not changed (Revert goes back "
                      "to the disc's)")
            else:
                raise SystemExit("mesh import: nothing to write")
            return {}
        put_model_files(work, rel, {".NGP": out, ".PTR": os.path.join(tmp, name + ".PTR")})
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    with open(_blender_rec(work, rel), "w", encoding="utf-8") as f:
        json.dump(rec, f, indent=1)
    if kind == "monster":
        files_on(mods, mod, tid, _json_obj(os.path.join(monster_dir(mods, mod, tid), "monster.json")))
    print("send it to the mod: %s" % pack_hint(mod))
    return anims


def mesh_import(disc, work, mods, mod, kind, tid, glb=None):
    return model_import(disc, work, mods, mod, kind, tid, glb, "mesh")


def anim_import(disc, work, mods, mod, kind, tid, glb=None):
    return model_import(disc, work, mods, mod, kind, tid, glb, "anims")


def rig_import(disc, work, mods, mod, cid, glb, front=None, scale=None, height=None):
    """Esqueleto proprio (tools/ngprig.py): o .glb com armature, malha, acoes a<id> e localizadores vira o corpo do custom sobre
    o traje base do disco (codigo, sombra, sons e o resto do tipo ficam). Grava .NGP/.PTR/.TEX/.RTX e os PNGs das texturas novas
    na pasta de trabalho do custom (o pack os leva) e liga "files". O mesh/anim import passam a recusar esse custom: o modelo e o
    .glb do rig. Devolve o resumo do ngprig."""
    import tempfile
    import ngpanim
    import ngpmesh
    import ngprig
    rel, stem = mesh_target(mods, mod, "monster", cid)
    glb = os.path.abspath(glb)
    if not os.path.isfile(glb):
        raise SystemExit("rig import: %s not found" % glb)
    src = disc_file(disc, stem + ".NGP")
    if not src or not disc_file(disc, stem + ".PTR") or not disc_file(disc, stem + ".TEX"):
        raise SystemExit("rig import: %s.NGP/.PTR/.TEX are not on the disc" % stem)
    import ngpgraft
    data = _json_obj(os.path.join(monster_dir(mods, mod, cid), "monster.json"))
    layers = ngpgraft.LAYERS.get(base_name(str((data or {}).get("special", ""))), ())
    tmp = tempfile.mkdtemp(prefix="wotm_rig_")
    name = os.path.basename(stem)
    try:
        try:
            res = ngprig.build(src, glb, tmp, name=name, front=front, scale=scale, height=height, layer_ids=layers, log=print)
        except (ngprig.RigError, ngpanim.AnimError, ngpmesh.MeshError) as e:
            raise SystemExit("rig import: %s" % e)
        files = {ext: os.path.join(tmp, name + ext) for ext in (".NGP", ".PTR", ".TEX", ".RTX")}
        put_model_files(work, rel, {k: p for k, p in files.items() if os.path.exists(p)})
        d = os.path.join(work, *rel.split("/"))
        rec = _json_obj(_blender_rec(work, rel)) or {}
        pngs = sorted(n for n in os.listdir(tmp) if n.lower().endswith(".png"))
        st = load_state(work)
        for n in rec.get("rig_pngs", []):   # os da importacao anterior que nao voltaram
            if n not in pngs and os.path.exists(os.path.join(d, n)):
                os.remove(os.path.join(d, n))
                st["files"].pop("%s/%s" % (rel, n), None)
        for n in pngs:
            shutil.copyfile(os.path.join(tmp, n), os.path.join(d, n))
            st["files"]["%s/%s" % (rel, n)] = {"sha1": "", "imported": True}
            print("texture %s -> work folder %s/" % (n, rel))
        save_state(work, st)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    with open(_blender_rec(work, rel), "w", encoding="utf-8") as f:
        json.dump({"rig": glb, "rig_pngs": pngs}, f, indent=1)
    special_parts(disc, work, rel, data)
    files_on(mods, mod, cid, data)
    print("send it to the mod: %s" % pack_hint(mod))
    return res


def special_parts(disc, work, rel, data):
    """O especial emprestado ("special") que precisa de pecas do .NGP do dono (tools/ngpgraft.py): copia-as nos arquivos proprios
    da pasta de trabalho `rel`. Sem "special" que precise, ou ja com as pecas, nao faz nada."""
    import ngpgraft
    sp = base_name(str((data or {}).get("special", "")))
    if sp not in ngpgraft.SPECIALS:
        return False
    d = os.path.join(work, *rel.split("/"))
    try:
        done = ngpgraft.graft(d, sp, lambda r: disc_file(disc, r), log=print)
    except ngpgraft.GraftError as e:
        raise SystemExit("special %s: %s" % (sp, e))
    if done:
        st = load_state(work)
        for n in os.listdir(d):
            if n.upper().endswith((".NGP", ".PTR", ".TEX", ".RTX")):
                st["files"].setdefault("%s/%s" % (rel, n), {"sha1": "", "imported": True})
        save_state(work, st)
    return done


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
                    if v.get("special") and not base_name(str(v["special"])):
                        note += " WARNING: \"special\" %r is not a monster (%s, %s); the game keeps the base's special" % (
                            v["special"], ", ".join(MONSTERS), ", ".join(CUSTOM_BASES))
                    elif v.get("special") and base_name(str(v["special"])) in SPECIAL_NEEDS and not (
                            v.get("files") and base_name(str(v["special"])) in SPECIAL_PARTS):
                        sp = base_name(str(v["special"]))
                        note += " WARNING: the %s special uses %s, which only its own model has; the game keeps the base's special%s" % (
                            sp, SPECIAL_NEEDS[sp], " (Import rig... copies it into the monster's files)" if sp in SPECIAL_PARTS else "")
                    elif v.get("special") and not v.get("files") and base_prefix(str(v.get("base", ""))) not in CUSTOM_BASES.values():
                        note += " WARNING: \"special\" only applies with \"files\" (the game cannot tell this monster from the base's)"
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
                ext = os.path.splitext(n)[1].upper()
                if not orig and rel.upper().startswith("MONSTERS/") and ext in (".CHR", ".AIC"):
                    note = "fight numbers of the custom (in place of the base type's)" if ext == ".CHR" else \
                        "AI of the custom (%s)" % ("difficulty " + n[-5] if len(n) > 4 and n[-5] in "012" else "difficulties without their own")
                    if size > 0xC000:
                        note += " WARNING: fixed-size format, the game reads only the first %d bytes" % 0xC000
                elif not orig and rel.upper().startswith("MONSTERS/") and ext == ".BNK":
                    note = "sound bank of the custom (in place of the base type's)"
                    parts = rel.split("/")
                    bank = MONSTER_BANKS.get(base_prefix(str(_json_obj(os.path.join(mods, mod, parts[0], parts[1], "monster.json")).get("base", ""))))
                    try:
                        nb = len(snd989.read_bnk(p)["sounds"])
                        nbase = len(snd989.read_bnk(disc_file(disc, "SND/%s.BNK" % bank))["sounds"]) if bank else 0
                        note += ", %d sounds" % nb
                        if nb < nbase:
                            note += " WARNING: the base's %s.BNK has %d; the missing numbers play nothing" % (bank, nbase)
                    except Exception as e:   # noqa: BLE001 (o check descreve, nao para)
                        note, problems = "ERROR: not a sound bank (%s)" % e, problems + 1
                elif not orig:
                    note = "new file (not on the disc; nothing asks for it yet)" if key else "part of the %s" % rel.split("/")[0][:-1]
                else:
                    osz = os.path.getsize(orig)
                    note = "%d bytes (disc %d)" % (size, osz)
                    # fixed-size formats (MODS.md, "Under the hood"): the port hands the game only what fits in its buffer
                    cap = 0x4000 if rel.upper().startswith("PTS/") else \
                        0xC000 if rel.upper().startswith("RST/") or n.upper().endswith(".AIC") else 0
                    if cap and size > cap:
                        note += " WARNING: fixed-size format, the game reads only the first %d bytes%s" % (
                            cap, " (255 points)" if cap == 0x4000 else "")
                    elif size > osz and os.path.splitext(n)[1].upper() in (".TEX", ".RTX", ".NGP"):
                        note += " (larger than the disc file: the game log shows the room left when it loads)"
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
        if argv[i] in ("--disc", "--mods", "--work", "--base", "--name", "--costume", "--camera", "--narrator", "--front",
                       "--scale", "--height"):
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
    if not disc_ok(disc):
        raise SystemExit("game disc not found at %r (--disc or WOTM_DISC: the .iso or the extracted folder)" % disc)

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
    elif args[0] in ("skin", "monster") and len(args) > 1 and args[1] in ("mesh", "anim"):
        ok = (args[1] == "mesh" and len(args) in (5, 6) and args[2] in ("export", "import") and
              (args[2] == "import" or len(args) == 5)) or (args[1] == "anim" and len(args) in (5, 6) and args[2] == "import")
        if not ok:
            raise SystemExit("%s mesh export <mod> <id>\n%s mesh import <mod> <id> [edited.glb]\n%s anim import <mod> <id> "
                             "[edited.glb]" % (args[0], args[0], args[0]))
        w = work_of("%s %s" % (args[0], args[1]), args[3])
        glb = args[5] if len(args) == 6 else None
        if args[1] == "anim":
            anim_import(disc, w, mods, args[3], args[0], args[4], glb)
        elif args[2] == "export":
            mesh_export(disc, w, mods, args[3], args[0], args[4])
        else:
            mesh_import(disc, w, mods, args[3], args[0], args[4], glb)
    elif args[0] == "skin":
        if len(args) != 5 or args[1] != "new":
            raise SystemExit("skin new <mod> <monster> <id> [--base N] [--name \"GREEN TOGERA\"] [--ai]\n"
                             "skin mesh export <mod> <id>\nskin mesh import <mod> <id> [edited.glb]\n"
                             "skin anim import <mod> <id> [edited.glb]")
        base = opts.get("base")
        if base is not None:
            try:
                base = int(base)
            except ValueError:
                raise SystemExit("skin new: --base is a disc costume, 0 to 3")
        skin_new(disc, work_of("skin new", args[2]), mods, args[2], args[3], args[4], base, opts.get("name"), opts.get("ai", False))
    elif args[0] == "monster":
        use = ("monster new <mod> <id> --base <monster> [--costume N] [--name \"X\"] [--ai] [--camera togera:200 | --camera none] "
               "[--narrator <name>] [--no-textures]\nmonster portrait <mod> <id> <png>\nmonster model <mod> <id> <.NGP> <.PTR> [.TEX] [.RTX]"
               "\nmonster stats <mod> <id>\nmonster sounds <mod> <id>\nmonster mesh export <mod> <id>\nmonster mesh import <mod> <id> [edited.glb]"
               "\nmonster anim import <mod> <id> [edited.glb]\nmonster rig <mod> <id> <rig.glb> [--front +y|-y] "
               "[--scale S | --height H]")
        if len(args) < 4 or args[1] not in ("new", "portrait", "model", "stats", "sounds", "rig"):
            raise SystemExit(use)
        w = work_of("monster " + args[1], args[2])
        if args[1] == "rig":
            if len(args) != 5 or opts.get("front") not in (None, "+y", "-y") or ("scale" in opts and "height" in opts):
                raise SystemExit(use)
            try:
                sc = float(opts["scale"]) if "scale" in opts else None
                ht = float(opts["height"]) if "height" in opts else None
            except ValueError:
                raise SystemExit("monster rig: --scale and --height are numbers")
            rig_import(disc, w, mods, args[2], args[3], args[4], opts.get("front"), sc, ht)
        elif args[1] == "new":
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
        elif args[1] == "stats":
            if len(args) != 4:
                raise SystemExit(use)
            monster_stats(disc, w, mods, args[2], args[3])
        elif args[1] == "sounds":
            if len(args) != 4:
                raise SystemExit(use)
            monster_sounds(disc, w, mods, args[2], args[3])
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
