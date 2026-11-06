#!/usr/bin/env python3
"""WotM level editor: a PC program that opens a stage in 3D, moves, turns, copies and deletes its objects and saves the level
patch (docs/guides/MODS.md, "Level patches") into a mod; "Test" opens the game in that stage.

  python tools/level_editor/editor.py [--window | --serve] [--port N]

It opens in the default browser; --window opens its own window instead (pywebview when installed, otherwise Edge/Chrome
app mode); --serve only serves (open the printed address yourself). The address is
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
import os
import shutil
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
import lvl      # noqa: E402
import lvlglb   # noqa: E402

WEB = os.path.join(HERE, "web")
CONFIG = os.path.join(os.path.expanduser("~"), ".wotm_level_editor.json")
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


def load_config():
    cfg = {"disc": os.path.join(REPO, "disc"), "mods": os.path.join(REPO, "mods"), "theme": "dark", "mod": ""}
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
class Stages:
    """Stage objects and scene, cached: lvl.Stage takes ~0.5 s, the .glb ~3 s (kept on disk, keyed by the .NGP and lvlglb.py)."""

    def __init__(self):
        self.lock = threading.Lock()
        self.info = {}

    def list(self, disc):
        d = os.path.join(disc, "LVL")
        names = sorted(f[:-4].upper() for f in os.listdir(d) if f.upper().endswith(".NGP")) if os.path.isdir(d) else []
        return [{"name": n, "title": STAGE_TITLE.get(n, n), "level": LEVEL_NUM.get(n)} for n in names]

    def objects(self, disc, name):
        key = (disc, name)
        with self.lock:
            if key not in self.info:
                st = lvl.Stage(name, disc)
                objs = st.objects()
                on = st.on_top(objs)
                self.info[key] = (st, objs, on)
            return self.info[key]

    def glb(self, disc, name):
        st, objs, on = self.objects(disc, name)
        ngp_path = os.path.join(disc, "LVL", name + ".NGP")
        stamp = "%d-%d-%d" % (os.path.getsize(ngp_path), int(os.path.getmtime(ngp_path)),
                              int(os.path.getmtime(os.path.join(TOOLS, "lvlglb.py"))))
        os.makedirs(CACHE, exist_ok=True)
        path = os.path.join(CACHE, "%s.glb" % name)
        with self.lock:
            try:
                with open(path + ".stamp") as f:
                    fresh = f.read() == stamp and os.path.exists(path)
            except OSError:
                fresh = False
            if not fresh:
                lvlglb.export(st, objs, disc, path + ".tmp", on=on)
                os.replace(path + ".tmp", path)
                with open(path + ".stamp", "w") as f:
                    f.write(stamp)
        return path


STAGES = Stages()


def describe(objs, on):
    out = []
    for o in objs:
        h = o["handle"] or o["node"]
        out.append({"handle": "0x%X" % h, "kind": o["kind"], "pos": [round(x, 1) for x in o["pos"]],
                    "heading": round(o["heading"], 1), "id": o["id"], "states": o["states"], "paths": o["n_paths"],
                    "movable": bool(o["handle"]),
                    "on": "0x%X" % on[o["handle"]] if o["handle"] in on else ""})
    return out


def check_patch(disc, name, patch):
    """lvl.py patch on a fresh copy of the stage: the log lines, the path cuts and the collision check."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        st = lvl.Stage(name, disc)
        lp = lvl.LevelPatch(st)
        lp.apply(patch)
        for line in lp.log:
            print(line)
        print("%s: %s" % (name, ", ".join("%s %d" % kv for kv in sorted(lp.stats.items())) or "nothing changed"))
        pts = lvl.stage_points(disc, name)
        nets = {"fodder": lvl.net_from_points(pts.get("FOP", [])), "ped": lvl.net_from_points(pts.get("PED", []))}
        for line in lvl.cut_nets(lp.footprints, nets):
            print(line)
        st.grid = lvl.Grid(st.img, st.grid.addr)
        ok = lvl.check_collision(st)
    return {"ok": bool(ok) and not lp.stats.get("skipped"), "lines": buf.getvalue().splitlines()}


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


def launch_game(cfg, name, training):
    if not cfg.get("game") or not os.path.exists(cfg["game"]):
        raise RuntimeError("game not found: set it in Settings (wotm.exe)")
    env = dict(os.environ)
    env.update({"WOTM_MODS": os.path.abspath(cfg["mods"]), "WOTM_MATCH": "3,%d" % LEVEL_NUM.get(name, 1),
                "WOTM_FULLSCREEN": "0", "WOTM_LEVELPATCH": "1"})
    if training:
        env["WOTM_TRAINING"] = "1"
    if cfg.get("elf"):
        # the clone's runtime run as run_live.ps1 does: the disc image, native movies and drawing, no emulated VU1/VIF1/GS
        if not cfg.get("iso") or not os.path.exists(cfg["iso"]):
            raise RuntimeError("disc image not found: set the ISO in Settings (the dev runtime reads the stage from it)")
        env.update({"PS2X_CD_IMAGE": cfg["iso"], "PS2X_FMV": "1", "PS2X_SCENE": "1",
                    "PS2X_NO_VU1": "1", "PS2X_NO_VIF1": "1", "PS2X_NO_GS": "1"})
    args = [cfg["game"]] + ([cfg["elf"]] if cfg.get("elf") else [])
    cwd = cfg.get("cwd") or os.path.dirname(cfg["game"])
    # with the ELF (the clone's runtime) stderr is the log; with no argument wotm.exe writes wotm.log itself
    os.makedirs(CACHE, exist_ok=True)
    log = open(game_log(cfg), "w") if cfg.get("elf") else subprocess.DEVNULL
    subprocess.Popen(args, cwd=cwd, env=env, stdout=subprocess.DEVNULL, stderr=log)
    return " ".join(args)


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
        u = urllib.parse.urlparse(self.path)
        q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
        cfg = self.cfg
        try:
            if u.path == "/api/init":
                return self.send(200, {"config": cfg, "stages": STAGES.list(cfg["disc"]), "mods": list_mods(cfg)})
            if u.path == "/api/stage":
                st, objs, on = STAGES.objects(cfg["disc"], q["name"])
                patch = {}
                if q.get("mod"):
                    try:
                        with open(patch_path(cfg, q["mod"], q["name"]), encoding="utf-8") as f:
                            patch = json.load(f)
                    except (OSError, ValueError):
                        pass
                return self.send(200, {"objects": describe(objs, on), "patch": patch})
            if u.path == "/api/gamelog":
                return self.send(200, {"lines": game_stage_lines(cfg)})
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
        cfg = self.cfg
        try:
            b = self.body()
            if self.path == "/api/config":
                cfg.update({k: v for k, v in b.items() if k in ("disc", "mods", "game", "elf", "cwd", "iso", "theme", "mod")})
                save_config(cfg)
                return self.send(200, {"config": cfg, "stages": STAGES.list(cfg["disc"]), "mods": list_mods(cfg)})
            if self.path == "/api/check":
                return self.send(200, check_patch(cfg["disc"], b["name"], b["patch"]))
            if self.path in ("/api/save", "/api/test"):
                mod, name = b["mod"].strip(), b["name"]
                if not mod or any(c in mod for c in "\\/:*?\"<>|") or mod.startswith(("_", ".")):
                    return self.send(400, {"error": "pick a mod name (letters, digits, no leading _)"})
                if self.path == "/api/test" and not b.get("save", True):
                    return self.send(200, {"ok": True, "lines": [],
                                           "launched": launch_game(cfg, name, b.get("training", True))})
                res = check_patch(cfg["disc"], name, b["patch"])
                path = patch_path(cfg, mod, name)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                if b["patch"].get("objects"):
                    with open(path, "w", encoding="utf-8") as f:
                        json.dump(b["patch"], f, indent=1)
                    res["saved"] = path
                elif os.path.exists(path):
                    os.remove(path)
                    res["saved"] = "removed " + path
                if self.path == "/api/test":
                    res["launched"] = launch_game(cfg, name, b.get("training", True))
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
            time.sleep(3600)
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
