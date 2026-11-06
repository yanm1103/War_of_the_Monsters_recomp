#!/usr/bin/env python3
"""WotM level editor: a PC program that opens a stage in 3D, moves, turns, copies and deletes its objects and saves the level
patch (docs/guides/MODS.md, "Level patches") into a mod; "Test" opens the game in that stage.

  python tools/level_editor/editor.py [--window | --serve] [--port N]

It opens in the default browser; --window opens its own window instead (pywebview when installed, otherwise Edge/Chrome
app mode); --serve only serves (open the printed address yourself); without it the program quits 10 s after its page closes. The address is
http://127.0.0.1:47197/ (--port changes it); when that port is taken by an editor already running, its window opens again,
when by something else, a free port is used.

The window is a local web page (web/, three.js) served by this script on 127.0.0.1. Everything about the stage
comes from tools/lvl.py (objects, patch, path cuts, collision check) and tools/lvlglb.py (the 3D scene, cached per user in
%LOCALAPPDATA%/WotM/level_editor or ~/.cache/wotm_level_editor). Settings (disc, mods folder, game, theme) live in
~/.wotm_level_editor.json. Texts in English, like the mod kit.
"""
import contextlib
import io
import glob
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import threading
import time
import traceback
import urllib.parse
import urllib.request
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
REPO = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import hashlib  # noqa: E402
import lvl      # noqa: E402
import lvlglb   # noqa: E402
import lvlnames  # noqa: E402
import modkit   # noqa: E402 - the disc as a folder or a .iso (disc_iso, disc_cache, disc_listdir), as the mod kit reads it
import hudapi   # noqa: E402 - the HUD panel (web/hud.js)
import pedsapi  # noqa: E402 - the pedestrians (web/peds.js)

WEB = os.path.join(HERE, "web")
CONFIG = os.path.join(os.path.expanduser("~"), ".wotm_level_editor.json")
# model names (by content signature, tools/lvlnames.py): the repo's, and the user's over them
REPO_NAMES = os.path.join(HERE, "names.json")
USER_NAMES = os.path.join(os.path.expanduser("~"), ".wotm_level_editor_names.json")
CACHE = os.path.join(os.environ.get("LOCALAPPDATA") or os.path.join(os.path.expanduser("~"), ".cache"),
                     "WotM" if os.environ.get("LOCALAPPDATA") else "", "level_editor")
# m_levelNum of each stage file (GameLevelNames; native/runtime/wotm_save.inc kLevels): WOTM_MATCH=<monster>,<levelNum>
LEVEL_NUM = {"CENTRAL": 1, "VEGAS": 2, "CANYON2": 3, "METRO": 4, "AIRPORT": 5, "3-MILE": 6, "SANFRAN": 7, "ISLAND": 8,
             "TOKYO": 9, "UFO": 10, "CAPITOL": 11, "TRAINING": 12, "CANYON1": 13, "MINISF": 14, "VOLCANO": 15, "TOKYODM": 16,
             "DBALL": 25, "BIGSHOT": 26, "CRUSH": 27}
STAGE_TITLE = {"CENTRAL": "Midtown", "VEGAS": "Lake Powell / Vegas", "CANYON2": "Sunshine Canyon", "METRO": "Monster Island",
               "AIRPORT": "Goldstone Airport", "3-MILE": "Ragnarok Rad Plant", "SANFRAN": "Baytown", "ISLAND": "Club Caldera",
               "TOKYO": "Tokyo", "UFO": "UFO", "CAPITOL": "Capitol", "TRAINING": "Training", "CANYON1": "Canyon 1",
               "MINISF": "Mini Baytown", "VOLCANO": "Volcano", "TOKYODM": "Tokyo DM", "DBALL": "Dodge ball",
               "BIGSHOT": "Big shot", "CRUSH": "Crush"}


# ------------------------------------------------------------------------------------------------------------- settings
def default_game():
    """(exe, elf or None, cwd): the clone's runtime next to the repo (wotm.exe with the ELF, cwd discroot), else wotm.exe next
    to this program (release: no argument, the exe finds its own data)."""
    rt = os.path.join(os.path.dirname(REPO), "wotm-recomp-win")
    exe = os.path.join(rt, "build_dev", "ps2xRuntime", "wotm.exe")
    if os.path.exists(exe):
        isos = sorted(glob.glob(os.path.join(REPO, "ISO", "*.iso")))
        return {"game": exe, "elf": os.path.join(rt, "discroot", "SCUS_971.97"), "cwd": os.path.join(rt, "discroot"),
                "iso": isos[0] if isos else ""}
    here = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__))
    for d in (here, os.path.dirname(here)):
        exe = os.path.join(d, "wotm.exe" if os.name == "nt" else "wotm")
        if os.path.exists(exe):
            return {"game": exe, "elf": "", "cwd": d, "iso": ""}
    return {"game": "", "elf": "", "cwd": "", "iso": ""}


def default_disc():
    """The extracted disc/ of the clone, else its first ISO/*.iso, else the mod kit's choice (the release: iso= of wotm.ini)."""
    if not getattr(sys, "frozen", False):
        if os.path.isdir(os.path.join(REPO, "disc", "LVL")):
            return os.path.join(REPO, "disc")
        isos = sorted(glob.glob(os.path.join(REPO, "ISO", "*.iso")))
        if isos:
            return isos[0]
    return modkit.default_disc()


def load_config():
    disc = default_disc()
    # the release exe: the mods folder the game reads (the mod kit's rule); a clone: the repo's mods/
    mods = modkit.default_mods(disc) if getattr(sys, "frozen", False) else os.path.join(REPO, "mods")
    cfg = {"disc": disc, "mods": mods, "theme": "dark", "mod": ""}
    cfg.update(default_game())
    try:
        with open(CONFIG, encoding="utf-8") as f:
            cfg.update({k: v for k, v in json.load(f).items() if v is not None})
    except (OSError, ValueError):
        pass
    return cfg


def save_config(cfg):
    with open(CONFIG, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)


# ------------------------------------------------------------------------------------------------------------- stage data
# The files of one stage: with a .iso they are copied once into the mod kit's cache of that image (modkit.disc_cache) and read
# from there like a folder, so lvl.py and lvlglb.py do not change.
STAGE_FILES = (("LVL", ("NGP", "PTR", "TEX", "RTX")), ("PTS", ("SPT", "PUP", "FOP", "PED", "AIP")), ("RST", ("MRS",)))


def disc_error(disc):
    """None when the disc can be read, else why not."""
    if not disc:
        return "no disc set: pick the game's .iso or the extracted disc folder in Settings"
    if os.path.isdir(disc):
        return None if os.path.isdir(os.path.join(disc, "LVL")) else "%s has no LVL folder" % disc
    if not os.path.isfile(disc):
        return "%s does not exist" % disc
    try:
        modkit.disc_iso(disc)
        return None
    except (OSError, ValueError, IndexError) as e:   # IndexError: a file shorter than the ISO9660 header
        return "%s is not a disc image (%s); a .bin/.cue is not read: use the .iso" % (disc, e or type(e).__name__)


def is_iso(disc):
    return bool(disc) and os.path.isfile(disc) and disc_error(disc) is None


def stage_root(disc, name, files=STAGE_FILES):
    """The folder to read the stage from: the disc folder itself, or the cache with the stage's files copied from the .iso."""
    iso = modkit.disc_iso(disc)
    if not iso:
        return disc
    root = modkit.disc_cache(disc)
    with iso.lock:
        for d, exts in files:
            for e in exts:
                rel = "%s/%s.%s" % (d, name.upper(), e)
                f = iso.find(rel)
                q = os.path.join(root, d, "%s.%s" % (name.upper(), e))
                if f and not f[2] and not (os.path.isfile(q) and os.path.getsize(q) == f[1]):
                    iso.extract(rel, q)
    return root


class Stages:
    """Stage objects and scene, cached: lvl.Stage takes ~0.5 s, the .glb ~3 s (kept on disk, keyed by the .NGP and lvlglb.py)."""

    def __init__(self):
        self.lock = threading.Lock()
        self.info = {}

    def list(self, disc):
        if disc_error(disc):
            return []
        names = sorted({n.rsplit(".", 1)[0].upper() for n in modkit.disc_listdir(disc, "LVL") if n.upper().endswith(".NGP")})
        return [{"name": n, "title": STAGE_TITLE.get(n, n), "level": LEVEL_NUM.get(n)} for n in names]

    def objects(self, disc, name):
        key = (disc, name)
        with self.lock:
            if key not in self.info:
                st = lvl.Stage(name, stage_root(disc, name))
                objs = st.objects()
                on = st.on_top(objs)
                self.info[key] = (st, objs, on)
            return self.info[key]

    def facts(self, disc, name):
        """lvlnames.facts per object (signature, size, rule inputs), kept on disk next to the scene (1 to 10 s per stage)."""
        st, objs, on = self.objects(disc, name)
        key = (disc, name, "facts")
        with self.lock:
            if key in self.info:
                return self.info[key]
        root = stage_root(disc, name)
        ngp_path = os.path.join(root, "LVL", name + ".NGP")
        stamp = "%d-%d-%d-%d" % (os.path.getsize(ngp_path), int(os.path.getmtime(ngp_path)), module_stamp(lvlnames),
                                 lvlnames.SIG_VERSION)
        path = os.path.join(CACHE, "%s-%s.facts.json" % (name, disc_key(disc)))
        facts = None
        try:
            with open(path, encoding="utf-8") as f:
                d = json.load(f)
            if d.get("stamp") == stamp and len(d.get("facts", ())) == len(objs):
                facts = d["facts"]
        except (OSError, ValueError):
            pass
        if facts is None:
            facts = lvlnames.facts(st, objs, root)
            os.makedirs(CACHE, exist_ok=True)
            with open(path + ".tmp", "w", encoding="utf-8") as f:
                json.dump({"stamp": stamp, "facts": facts}, f)
            os.replace(path + ".tmp", path)
        with self.lock:
            self.info[key] = facts
        return facts

    def glb(self, disc, name):
        st, objs, on = self.objects(disc, name)
        root = stage_root(disc, name)
        ngp_path = os.path.join(root, "LVL", name + ".NGP")
        stamp = "%d-%d-%s" % (os.path.getsize(ngp_path), int(os.path.getmtime(ngp_path)), "-".join(str(module_stamp(m)) for m in
                                 (lvlglb, lvlglb.gltf, lvlglb.ngp, lvlglb.tex, lvlglb.lvl)))
        os.makedirs(CACHE, exist_ok=True)
        path = os.path.join(CACHE, "%s-%s.glb" % (name, disc_key(disc)))   # two discs never share a scene
        with self.lock:
            try:
                with open(path + ".stamp") as f:
                    fresh = f.read() == stamp and os.path.exists(path)
            except OSError:
                fresh = False
            if not fresh:
                lvlglb.export(st, objs, root, path + ".tmp", on=on)
                os.replace(path + ".tmp", path)
                with open(path + ".stamp", "w") as f:
                    f.write(stamp)
        return path


def module_stamp(mod):
    """A tool's version for the caches: its .py's date (in the frozen exe, the exe's)."""
    p = getattr(mod, "__file__", None)
    p = p if p and os.path.exists(p) else sys.executable
    return int(os.path.getmtime(p))


def disc_key(disc):
    return hashlib.sha1(os.path.abspath(disc).lower().encode()).hexdigest()[:8]


STAGES = Stages()
# the HUD of every stage is read for "every stage with this picture": from a .iso, only the files lvlhud.py reads
HUD = hudapi.HudApi(CACHE, lambda disc, name: stage_root(disc, name, (("LVL", ("NGP", "TEX", "RTX")),)),
                    lambda disc: [s["name"] for s in STAGES.list(disc)], disc_key)


def load_names():
    return lvlnames.load_names(REPO_NAMES, USER_NAMES)


def set_model_name(sig, name, info):
    """Writes (or, empty, removes) one model name in the user's names file."""
    try:
        with open(USER_NAMES, encoding="utf-8") as f:
            d = json.load(f)
    except (OSError, ValueError):
        d = {}
    d.setdefault("version", 1)
    d["sig"] = lvlnames.SIG_VERSION
    models = d.setdefault("models", {})
    if name:
        models[sig] = dict({k: info[k] for k in ("kind", "type", "dims") if k in info}, name=name)
    else:
        models.pop(sig, None)
    with open(USER_NAMES + ".tmp", "w", encoding="utf-8") as f:
        json.dump(d, f, indent=1)
    os.replace(USER_NAMES + ".tmp", USER_NAMES)


def describe(stage, objs, on, facts, names):
    out = []
    for o, f in zip(objs, facts):
        h = o["handle"] or o["node"]
        name, src = lvlnames.name_of(stage, o["kind"], f, names)
        out.append({"handle": "0x%X" % h, "kind": o["kind"], "pos": [round(x, 1) for x in o["pos"]],
                    "heading": round(o["heading"], 1), "id": o["id"], "states": o["states"], "paths": o["n_paths"],
                    "movable": bool(o["handle"]),
                    "on": "0x%X" % on[o["handle"]] if o["handle"] in on else "",
                    "name": name, "nameSrc": src, "rule": lvlnames.rule_name(stage, o["kind"], f, names),
                    "sig": f["sig"] or "", "dims": f["dims"], "ptype": f.get("ptype")})
    return out


def stage_env(cfg, disc, name, mod=""):
    """The stage's look from RST/<STAGE>.MRS (lvl.read_mrs; the mod's own file when it has one): fog (key 4, with FA and FB
    as fogCalcFogValues makes them), background (the fog's color: fogBackgroundColorIsSlaved; key 2 when there is no fog)
    and the three parallel lights (key 5; dir points at the light, game axes, Z up)."""
    path = os.path.join(cfg["mods"], mod, "RST", name.upper() + ".MRS") if mod else ""
    if not os.path.isfile(path):
        path = os.path.join(stage_root(disc, name), "RST", name.upper() + ".MRS")
    env = {"file": path, "fog": None, "bg": None, "lights": []}
    if not os.path.isfile(path):
        return env
    for _off, key, body in lvl.read_mrs(path):
        if key == 4 and len(body) >= 28:
            mn, mx, maxv, far, r, g, b = struct.unpack_from("<4f3i", body)
            fog = {"minR": mn, "maxR": mx, "maxV": maxv, "farClip": far, "rgb": [r, g, b]}
            if mx > mn:     # else fogUpdate sends nothing
                fb = (maxv - 1.0) * mn * mx / (mx - mn)
                fog.update(fa=1.0 - fb / mx, fb=fb)
            env["fog"] = fog
        elif key == 2 and len(body) >= 12:
            env["bg"] = list(struct.unpack_from("<3i", body))
        elif key == 5 and len(body) >= 60:
            v = struct.unpack_from("<15f", body)
            for i in range(3):
                h, p = math.radians(v[9 + i]), math.radians(v[12 + i])
                env["lights"].append({"rgb": [v[i], v[3 + i], v[6 + i]], "h": v[9 + i], "p": v[12 + i],
                                      "dir": [math.cos(p) * math.sin(h), math.cos(p) * math.cos(h), math.sin(p)]})
    if env["fog"] and "fa" in env["fog"]:
        env["bg"] = env["fog"]["rgb"]
    return env


def check_patch(disc, name, patch):
    """lvl.py patch on a fresh copy of the stage: the log lines, the path cuts and the collision check."""
    buf = io.StringIO()
    root = stage_root(disc, name)
    with contextlib.redirect_stdout(buf):
        st = lvl.Stage(name, root)
        lp = lvl.LevelPatch(st)
        lp.apply(patch)
        pts = lvl.stage_points(root, name)
        nets = {"fodder": lvl.net_from_points(pts.get("FOP", [])), "ped": lvl.net_from_points(pts.get("PED", []))}
        lp.apply_peds(patch, nets)      # the game hook's order: objects, pedestrians, then the path cuts
        for line in lp.log:
            print(line)
        print("%s: %s" % (name, ", ".join("%s %d" % kv for kv in sorted(lp.stats.items())) or "nothing changed"))
        for line in lvl.cut_nets(lp.footprints, nets) + (lp.ped_report(nets) if patch.get("peds") else []):
            print(line)
        st.grid = lvl.Grid(st.img, st.grid.addr)
        ok = lvl.check_collision(st)
    return {"ok": bool(ok) and not lp.stats.get("skipped"), "lines": buf.getvalue().splitlines()}


def bad_mod(mod):
    """A mod name the editor refuses: empty, a path, or one the game skips (leading _ or .)."""
    return not mod or any(c in mod for c in "\\/:*?\"<>|") or mod.startswith(("_", "."))


def patch_path(cfg, mod, name):
    return os.path.join(cfg["mods"], mod, "levels", name + ".json")


def list_mods(cfg):
    d = cfg["mods"]
    if not os.path.isdir(d):
        return []
    return sorted(m for m in os.listdir(d) if os.path.isdir(os.path.join(d, m)) and not m.startswith(("_", ".")))


def game_log(cfg):
    """The game's log: stderr of the clone's runtime (kept in the cache), or the wotm.log the release exe writes."""
    if cfg.get("elf"):
        return os.path.join(CACHE, "game.log")
    return os.path.join(cfg.get("cwd") or os.path.dirname(cfg.get("game") or "."), "wotm.log")


def game_stage_lines(cfg):
    """The [fase] lines of the game's log (what the patch did in the game)."""
    try:
        with open(game_log(cfg), encoding="utf-8", errors="replace") as f:
            return [l.rstrip() for l in f if l.startswith("[fase]")]
    except OSError:
        return []


def save_patch(cfg, mod, name, patch):
    """Writes (or, empty, removes) mods/<mod>/levels/<STAGE>.json; through a .tmp, since a running game may read it at any time."""
    path = patch_path(cfg, mod, name)
    if patch.get("objects") or patch.get("names") or patch.get("peds"):
        patch = dict(patch, objects=patch.get("objects") or [])   # the game logs "sem a lista objects" without it
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path + ".tmp", "w", encoding="utf-8") as f:
            json.dump(patch, f, indent=1)
        os.replace(path + ".tmp", path)
        return path
    if os.path.exists(path):
        os.remove(path)
        return "removed " + path
    return None


def patch_winner(cfg, mod, name):
    """The mod whose levels/<STAGE>.json the game uses: with the same file in several mods, the last by name (case aside)."""
    have = [m for m in list_mods(cfg) if os.path.isfile(patch_path(cfg, m, name))]
    if mod not in have:
        have.append(mod)
    return sorted(have, key=str.lower)[-1]


# The game the editor opened, and the file it watches for "reload the stage" (WOTM_LEVELRELOAD: "<STAGE> <counter>")
GAME = {"proc": None, "count": int(time.time())}    # from the clock: a new editor never repeats an old counter
RELOAD_FILE = os.path.join(CACHE, "reload.txt")


def game_running():
    p = GAME["proc"]
    return p is not None and p.poll() is None


def reload_game(name):
    if not game_running():
        raise RuntimeError("no game opened by the editor is running: use Test in game")
    GAME["count"] += 1
    with open(RELOAD_FILE + ".tmp", "w", encoding="utf-8") as f:
        f.write("%s %d\n" % (name, GAME["count"]))
    os.replace(RELOAD_FILE + ".tmp", RELOAD_FILE)


def launch_game(cfg, name, training):
    if not cfg.get("game") or not os.path.exists(cfg["game"]):
        raise RuntimeError("game not found: set it in Settings (wotm.exe)")
    env = dict(os.environ)
    os.makedirs(CACHE, exist_ok=True)
    env.update({"WOTM_MODS": os.path.abspath(cfg["mods"]), "WOTM_MATCH": "3,%d" % LEVEL_NUM.get(name, 1),
                "WOTM_FULLSCREEN": "0", "WOTM_LEVELPATCH": "1", "WOTM_LEVELRELOAD": RELOAD_FILE})
    if training:
        env["WOTM_TRAINING"] = "1"
    if cfg.get("elf"):
        # the clone's runtime run as run_live.ps1 does: the disc image, native movies and drawing, no emulated VU1/VIF1/GS
        iso = cfg.get("iso") or (cfg["disc"] if is_iso(cfg["disc"]) else "")
        if not iso or not os.path.exists(iso):
            raise RuntimeError("disc image not found: set the ISO in Settings (the dev runtime reads the stage from it)")
        env.update({"PS2X_CD_IMAGE": iso, "PS2X_FMV": "1", "PS2X_SCENE": "1",
                    "PS2X_NO_VU1": "1", "PS2X_NO_VIF1": "1", "PS2X_NO_GS": "1"})
    args = [cfg["game"]] + ([cfg["elf"]] if cfg.get("elf") else [])
    cwd = cfg.get("cwd") or os.path.dirname(cfg["game"])
    # with the ELF (the clone's runtime) stderr is the log; with no argument wotm.exe writes wotm.log itself
    os.makedirs(CACHE, exist_ok=True)
    log = open(game_log(cfg), "w") if cfg.get("elf") else subprocess.DEVNULL
    GAME["proc"] = subprocess.Popen(args, cwd=cwd, env=env, stdout=subprocess.DEVNULL, stderr=log)
    return " ".join(args)


def init_reply(cfg):
    return {"config": cfg, "stages": STAGES.list(cfg["disc"]), "mods": list_mods(cfg), "discError": disc_error(cfg["disc"]),
            "gameRunning": game_running()}


# ------------------------------------------------------------------------------------------------------------- server
class Handler(BaseHTTPRequestHandler):
    cfg = None

    def log_message(self, fmt, *args):
        pass

    def send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        elif isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(n) or b"{}")

    def do_GET(self):
        LIFE["seen"] = time.monotonic()
        u = urllib.parse.urlparse(self.path)
        q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
        cfg = self.cfg
        try:
            if u.path == "/api/init":
                return self.send(200, init_reply(cfg))
            if u.path == "/api/stage":
                st, objs, on = STAGES.objects(cfg["disc"], q["name"])
                patch = {}
                if q.get("mod"):
                    try:
                        with open(patch_path(cfg, q["mod"], q["name"]), encoding="utf-8") as f:
                            patch = json.load(f)
                    except (OSError, ValueError):
                        pass
                facts = STAGES.facts(cfg["disc"], q["name"])
                return self.send(200, {"objects": describe(q["name"], objs, on, facts, load_names()), "patch": patch,
                                       "env": stage_env(cfg, cfg["disc"], q["name"], q.get("mod", "")),
                                       "peds": pedsapi.stage_peds(st, stage_root(cfg["disc"], q["name"]))})
            if u.path == "/api/hud":
                mod = q.get("mod", "").strip()
                return self.send(200, HUD.panel(cfg["disc"], cfg["mods"], mod if mod and not bad_mod(mod) else "", q["stage"]))
            if u.path == "/api/hudimg":
                mod = q.get("mod", "").strip()
                path = HUD.image(cfg["disc"], cfg["mods"], "" if bad_mod(mod) else mod, q["stage"], q.get("src"), q.get("png"))
                if not path or not os.path.isfile(path):
                    return self.send(404, {"error": "not found"})
                with open(path, "rb") as f:
                    return self.send(200, f.read(), "image/png")
            if u.path == "/api/gamelog":
                return self.send(200, {"lines": game_stage_lines(cfg), "running": game_running()})
            if u.path == "/api/glb":
                with open(STAGES.glb(cfg["disc"], q["name"]), "rb") as f:
                    return self.send(200, f.read(), "model/gltf-binary")
            return self.static(u.path)
        except Exception as e:      # noqa: BLE001 - the page shows the error
            traceback.print_exc()
            return self.send(500, {"error": "%s: %s" % (type(e).__name__, e)})

    def static(self, path):
        rel = os.path.normpath(urllib.parse.unquote(path.lstrip("/")) or "index.html")
        full = os.path.join(WEB, rel)
        if rel.startswith("..") or not os.path.isfile(full):
            return self.send(404, {"error": "not found"})
        ctype = {".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml",
                 ".png": "image/png"}.get(os.path.splitext(full)[1], "application/octet-stream")
        with open(full, "rb") as f:
            return self.send(200, f.read(), ctype + ("; charset=utf-8" if ctype.startswith("text") else ""))

    def do_POST(self):
        LIFE["seen"] = time.monotonic()
        cfg = self.cfg
        try:
            b = self.body()
            if self.path == "/api/config":
                cfg.update({k: v for k, v in b.items() if k in ("disc", "mods", "game", "elf", "cwd", "iso", "theme", "mod")})
                save_config(cfg)
                return self.send(200, init_reply(cfg))
            if self.path == "/api/names":
                if not b.get("sig"):
                    return self.send(400, {"error": "this object has no mesh to name as a model"})
                set_model_name(b["sig"], (b.get("name") or "").strip(),
                               {"kind": b.get("kind"), "type": b.get("ptype"), "dims": b.get("dims")})
                return self.send(200, {"ok": True, "file": USER_NAMES})
            if self.path == "/api/check":
                return self.send(200, check_patch(cfg["disc"], b["name"], b["patch"]))
            if self.path == "/api/bye":         # the page closed: see main()
                LIFE["bye"] = time.monotonic()
                return self.send(200, {"ok": True})
            if self.path in ("/api/save", "/api/test", "/api/reload", "/api/hud/replace", "/api/hud/remove"):
                mod = (b.get("mod") or "").strip()
                if bad_mod(mod):
                    return self.send(400, {"error": "pick a mod name (letters, digits, no leading _)"})
                if self.path == "/api/hud/replace":
                    return self.send(200, {"lines": HUD.replace(cfg["disc"], cfg["mods"], mod, b["stage"], b["role"], b["png"],
                                                                b["data"], bool(b.get("every")))})
                if self.path == "/api/hud/remove":
                    return self.send(200, {"lines": HUD.remove(cfg["disc"], cfg["mods"], mod, b["stage"], b["role"], b["png"],
                                                               bool(b.get("every")))})
                name = b["name"]
                if self.path == "/api/reload" and not game_running():
                    return self.send(400, {"error": "no game opened by the editor is running: use Test in game"})
                if self.path == "/api/test" and not b.get("save", True):
                    return self.send(200, {"ok": True, "lines": [], "launched": launch_game(cfg, name, b.get("training", True))})
                res = check_patch(cfg["disc"], name, b["patch"])
                res["saved"] = save_patch(cfg, mod, name, b["patch"])
                win = patch_winner(cfg, mod, name)
                if win != mod:
                    res["lines"].append("warning: mod \"%s\" also has levels/%s.json and the game uses that one "
                                        "(the last mod by name wins)" % (win, name))
                    res["ok"] = False
                if self.path == "/api/test":
                    res["launched"] = launch_game(cfg, name, b.get("training", True))
                if self.path == "/api/reload":
                    reload_game(name)
                    res["reloading"] = True
                return self.send(200, res)
            return self.send(404, {"error": "not found"})
        except Exception as e:      # noqa: BLE001
            traceback.print_exc()
            return self.send(500, {"error": "%s: %s" % (type(e).__name__, e)})


def open_window(url, window):
    """The default browser, or with window its own window. True when that window ran here and is now closed."""
    if window:
        try:
            import webview      # noqa: PLC0415 - optional
            webview.create_window("WotM Level Editor", url, width=1600, height=950)
            webview.start()
            return True
        except ImportError:
            pass
        candidates = [shutil.which(e) for e in ("msedge", "chrome", "chromium", "google-chrome", "chromium-browser")]
        candidates += [os.path.expandvars(p) for p in (r"%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe",
                                                      r"%ProgramFiles%\Microsoft\Edge\Application\msedge.exe",
                                                      r"%ProgramFiles%\Google\Chrome\Application\chrome.exe")]
        for path in candidates:
            if path and os.path.exists(path):
                # no "translate this page?" (the page is English, the system may not be)
                subprocess.Popen([path, "--app=" + url, "--window-size=1600,950", "--disable-features=Translate",
                                  "--user-data-dir=" + os.path.join(CACHE, "browser")])
                return False
    webbrowser.open(url)
    return False


DEFAULT_PORT = 47197     # from SCUS_971.97
LIFE = {"seen": 0.0, "bye": 0.0}   # last request, last /api/bye (time.monotonic)
BYE_WAIT = 10            # seconds after the page closed with no request: the editor quits (not with --serve)


class Server(ThreadingHTTPServer):
    # on Windows SO_REUSEADDR lets a second server bind a port already in use: off there, so a taken port is noticed
    allow_reuse_address = os.name != "nt"
    daemon_threads = True


def editor_at(port):
    """Is a level editor already answering on this port?"""
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/api/init" % port, timeout=2) as r:
            return "stages" in json.load(r)
    except (OSError, ValueError):
        return False


def main(argv):
    port = int(argv[argv.index("--port") + 1]) if "--port" in argv else DEFAULT_PORT
    Handler.cfg = load_config()
    os.makedirs(CACHE, exist_ok=True)
    try:
        srv = Server(("127.0.0.1", port), Handler)
    except OSError:
        if editor_at(port):
            url = "http://127.0.0.1:%d/" % port
            print("WotM level editor already running at", url)
            if "--serve" not in argv:
                open_window(url, "--window" in argv)
            return 0
        print("port %d is taken: using a free one" % port)
        srv = Server(("127.0.0.1", 0), Handler)
    url = "http://127.0.0.1:%d/" % srv.server_address[1]
    print("WotM level editor at", url)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    if "--serve" not in argv and open_window(url, "--window" in argv):
        return 0            # pywebview window closed
    try:
        while True:
            time.sleep(1)
            # the page said it closed (/api/bye) and nothing asked since: a reload asks again within a second
            bye = LIFE["bye"]
            if "--serve" not in argv and bye and LIFE["seen"] <= bye and time.monotonic() - bye > BYE_WAIT:
                print("the editor's page closed: bye")
                return 0
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
