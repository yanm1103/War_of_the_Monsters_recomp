#!/usr/bin/env python3
"""The mod kit in a browser tab: a small server on 127.0.0.1 that serves the page (tools/modkit_web/) and runs the kit
(tools/modkit.py) for it. Plan and state between sessions: docs/investigacoes/2026-11-22/kit_navegador.md.

  python tools/modkit_gui.py --web        (or python tools/modkit_web.py)
      --no-browser: only serve; --port N; --conf FILE: another config file (tests, so they leave the user's alone)

- One kit at a time: the port goes into the kit's config (the same file as the window kit, key "web_port"); a second start
  while one runs opens another tab on it.
- A tab is open while its log stream (GET /api/log) is: the server quits 15 s after the last one closed (none at all in the
  first 2 minutes also quits), or on POST /api/quit. A task that is running holds it until it ends. (A stream, not a timer
  in the page: browsers slow the timers of a tab in the background down to one a minute.)
- Tasks (build, check, imports) run modkit.py in a separate process, one at a time (the exe runs itself with --modkit-cli);
  their lines go to every open tab through GET /api/log (server-sent events, replayed from the start of the session).
- Picking a file or a folder opens the system's own dialog (POST /api/pick): the page needs the path, and a browser gives
  only the file's bytes. Tk's dialogs in a thread of their own; on Linux zenity or kdialog when there is one.
Standard library only (Tk for the dialogs).
"""
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
import traceback
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modkit  # noqa: E402
from modkit_web_files import FilesMixin  # noqa: E402
from modkit_web_model import ModelMixin  # noqa: E402
from modkit_web_skins import SkinsMixin  # noqa: E402
from modkit_web_monsters import MonstersMixin  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
CONF = os.path.join(modkit.game_data_dir(), "modkit.json") if modkit.FROZEN else os.path.join(os.path.expanduser("~"), ".wotm_modkit.json")
APP = "wotm-modkit"
GONE_AFTER, FIRST_TAB = 15.0, 120.0
TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8",
         ".json": "application/json", ".png": "image/png", ".svg": "image/svg+xml", ".glb": "model/gltf-binary",
         ".ico": "image/x-icon", ".wav": "audio/wav"}


def _assets():
    """(page folder, three.js vendor folder): the repo's, or the copies inside the kit's exe."""
    mp = getattr(sys, "_MEIPASS", None)
    if mp and os.path.isdir(os.path.join(mp, "modkit_web")):
        return os.path.join(mp, "modkit_web"), os.path.join(mp, "orient", "vendor")
    return os.path.join(HERE, "modkit_web"), os.path.join(HERE, "level_editor", "web", "vendor")


def load_conf():
    try:
        with open(CONF, encoding="utf-8") as f:
            c = json.load(f)
            return c if isinstance(c, dict) else {}
    except (OSError, ValueError):
        return {}


def save_conf(changes):
    """Merges into the config file (the window kit keeps its own keys there)."""
    c = load_conf()
    c.update(changes)
    try:
        with open(CONF, "w", encoding="utf-8") as f:
            json.dump(c, f, indent=1)
    except OSError:
        pass
    return c


def valid_mod_name(n):
    return not modkit.mod_name_error(n)


def open_path(p):
    """The system's file manager on a folder (made when missing: a mods or work folder not used yet) or a file's program."""
    if not os.path.exists(p) and not os.path.splitext(p)[1]:
        os.makedirs(p, exist_ok=True)
    p = os.path.normpath(p)
    if hasattr(os, "startfile"):
        os.startfile(p)
    else:
        subprocess.Popen(["open" if sys.platform == "darwin" else "xdg-open", p], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# ------------------------------------------------------------------ the system's file dialogs
class Dialogs:
    """Native open/folder dialogs, one at a time, from a thread that owns Tk (Tk wants a single thread)."""

    def __init__(self):
        self.q = queue.Queue()
        self.thread = None
        self.lock = threading.Lock()

    def ask(self, kind, title="", initial="", types=None):
        """kind: "file", "files" or "dir" -> a path, a list of paths, or None / [] when cancelled."""
        with self.lock:   # one dialog on screen at a time
            if sys.platform.startswith("linux"):
                r = self._linux(kind, title, initial, types)
                if r is not NotImplemented:
                    return r
            if self.thread is None:
                self.thread = threading.Thread(target=self._tk_loop, daemon=True)
                self.thread.start()
            box = queue.Queue()
            self.q.put((kind, title, initial, types, box))
            return box.get()

    def _tk_loop(self):
        import tkinter as tk
        from tkinter import filedialog
        root = tk.Tk()
        root.withdraw()
        while True:
            kind, title, initial, types, box = self.q.get()
            try:
                root.attributes("-topmost", True)
                root.lift()
                root.update()
                ft = [(n, " ".join(p)) for n, p in (types or [])] + [("All files", "*.*")]
                start = initial if initial and os.path.isdir(initial) else os.path.dirname(initial) if initial else os.getcwd()
                if kind == "dir":
                    r = filedialog.askdirectory(parent=root, title=title, initialdir=start) or None
                elif kind == "files":
                    r = list(filedialog.askopenfilenames(parent=root, title=title, initialdir=start, filetypes=ft))
                else:
                    r = filedialog.askopenfilename(parent=root, title=title, initialdir=start, filetypes=ft) or None
                if isinstance(r, list):
                    r = [os.path.normpath(p) for p in r]
                elif r:
                    r = os.path.normpath(r)
            except Exception:  # noqa: BLE001
                traceback.print_exc()
                r = [] if kind == "files" else None
            box.put(r)

    def _linux(self, kind, title, initial, types):
        if shutil.which("zenity"):
            cmd = ["zenity", "--file-selection", "--title=" + (title or "Pick")]
            if kind == "dir":
                cmd.append("--directory")
            if kind == "files":
                cmd += ["--multiple", "--separator=\n"]
            if initial:
                cmd.append("--filename=" + initial.rstrip("/") + "/")
            for n, pats in types or []:
                cmd.append("--file-filter=%s | %s" % (n, " ".join(pats)))
        elif shutil.which("kdialog"):
            start = initial or os.path.expanduser("~")
            filt = " ".join(p for _n, pats in (types or []) for p in pats)
            cmd = (["kdialog", "--title", title or "Pick", "--getexistingdirectory", start] if kind == "dir" else
                   ["kdialog", "--title", title or "Pick", "--getopenfilename", start] + ([filt] if filt else []) +
                   (["--multiple", "--separate-output"] if kind == "files" else []))
        else:
            return NotImplemented
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        out = [x for x in p.stdout.splitlines() if x.strip()] if p.returncode == 0 else []
        if kind == "files":
            return out
        return out[0] if out else None


# ------------------------------------------------------------------ the kit
class Kit(FilesMixin, SkinsMixin, MonstersMixin, ModelMixin):
    """The state the page sees and the tasks it starts."""

    def __init__(self):
        c = load_conf()
        disc = c.get("disc")
        self.disc = disc if disc and os.path.exists(disc) else modkit.default_disc()
        mods = c.get("mods")
        self.mods = mods if mods and os.path.isdir(mods) else modkit.default_mods(self.disc)
        self.mod = c.get("mod") or ""
        if self.mod not in self.mod_names():
            self.mod = ""
        self.log = []                  # [{"seq", "kind", "text"}]: the whole session, replayed to a new tab
        self.cond = threading.Condition()
        self.busy = None               # title of the task running
        self.dialogs = Dialogs()
        self.viewers = 0               # log streams open (tabs)
        self.last_seen = None          # last time a tab was open
        self.started = time.time()

    # config and the system, for the parts of the kit in other files
    @staticmethod
    def conf_get():
        return load_conf()

    @staticmethod
    def conf_save(d):
        save_conf(d)

    @staticmethod
    def open_path(p):
        open_path(p)

    def mod_names(self):
        try:
            return sorted((n for n in os.listdir(self.mods) if os.path.isdir(os.path.join(self.mods, n)) and valid_mod_name(n)),
                          key=str.lower)
        except OSError:
            return []

    def work(self):
        return modkit.work_dir(self.mods, self.mod) if self.mod else None

    def state(self):
        c = load_conf()
        return {"disc": self.disc, "disc_ok": bool(modkit.disc_ok(self.disc)), "mods": self.mods, "mods_ok": os.path.isdir(self.mods),
                "mod": self.mod, "mod_list": self.mod_names(), "work": self.work(), "busy": self.busy,
                "autoplay": c.get("autoplay", True), "editors": c.get("editors") or {},
                "replace_dir": c.get("replace_dir") or c.get("save_dir") or os.path.expanduser("~"),
                "save_dir": c.get("save_dir") or os.path.expanduser("~")}

    def set_conf(self, d):
        if "disc" in d:
            self.disc = os.path.normpath(d["disc"]) if d["disc"] else ""
        if "mods" in d:
            self.mods = os.path.normpath(d["mods"]) if d["mods"] else ""
        old = self.mod
        if "mod" in d:
            self.mod = d["mod"] or ""
        if "autoplay" in d:
            save_conf({"autoplay": bool(d["autoplay"])})
        if self.mod and self.mod not in self.mod_names():
            self.mod = ""
        save_conf({"disc": self.disc, "mods": self.mods, "mod": self.mod})
        if self.mod and self.mod != old:
            self.adopt()
        return self.state()

    def adopt(self):
        """As the window kit on picking a mod: the mod's files its work folder lacks come in as edited."""
        try:
            n = modkit.adopt_mod(self.disc, self.mods, self.mod, self.work())
            if n:
                self.say("%d file(s) of %s copied into its work folder; they show as edited and go back on Build.\n" % (n, self.mod))
        except Exception as ex:  # noqa: BLE001
            self.say("Could not copy %s's files into the work folder: %s\n" % (self.mod, ex), "err")

    def say(self, text, kind="out"):
        with self.cond:
            self.log.append({"seq": len(self.log) + 1, "kind": kind, "text": text})
            self.cond.notify_all()

    def run(self, title, args, after=None):
        """modkit.py <args> in a separate process (the exe: itself with --modkit-cli); False when one is running."""
        with self.cond:
            if self.busy:
                return False
            self.busy = title
        self.say("\n== %s\n" % title, "head")
        cmd = [sys.executable, "--modkit-cli"] if modkit.FROZEN else [sys.executable, "-u", os.path.join(HERE, "modkit.py")]
        env = dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONUNBUFFERED="1")

        def body():
            code = None
            try:
                p = subprocess.Popen(cmd + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env,
                                     creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                for raw in iter(p.stdout.readline, b""):
                    line = raw.decode("utf-8", "replace").replace("\r\n", "\n")
                    if line.startswith("[page] "):   # a page of the task (the bones page): the kit's page shows it
                        self.say(line[7:].strip(), "page")
                        continue
                    self.say(line)
                code = p.wait()
                if code:
                    self.say("(stopped with code %d)\n" % code, "err")
            except Exception:  # noqa: BLE001
                self.say(traceback.format_exc(), "err")
            if after and code == 0:
                try:
                    after()
                except Exception:  # noqa: BLE001
                    self.say(traceback.format_exc(), "err")
            with self.cond:
                self.busy = None
            self.say("", "done")

        threading.Thread(target=body, daemon=True).start()
        return True

    def run_fn(self, title, fn, after=None):
        """fn() in a thread of this process, its prints in the log (the kit's quick tasks, as the window kit runs them);
        False when a task is running."""
        import contextlib
        import io
        with self.cond:
            if self.busy:
                return False
            self.busy = title
        self.say("\n== %s\n" % title, "head")
        kit = self

        class W(io.TextIOBase):
            def write(self, t):
                if t:
                    kit.say(t)
                return len(t)

        def body():
            w = W()
            ok = False
            try:
                with contextlib.redirect_stdout(w), contextlib.redirect_stderr(w):
                    fn()
                ok = True
            except SystemExit as ex:
                self.say("%s\n" % ex, "err")
            except Exception:  # noqa: BLE001
                self.say(traceback.format_exc(), "err")
            if after and ok:
                try:
                    after()
                except Exception:  # noqa: BLE001
                    self.say(traceback.format_exc(), "err")
            with self.cond:
                self.busy = None
            self.say("", "done")

        threading.Thread(target=body, daemon=True).start()
        return True

    def paths(self):
        return ["--disc", self.disc, "--mods", self.mods]

    # ---- the page's actions: name -> f(kit, body) -> JSON
    def act_check(self, d):
        return {"ok": self.run("Checking...", ["check"] + self.paths())}

    def act_build(self, d):
        if not self.mod:
            return {"error": "Pick or create a mod first (\"Mod\" at the top)."}
        return {"ok": self.run("Building %s..." % self.mod, ["pack", self.mod, "--work", self.work()] + self.paths(),
                               after=lambda: self.say("Done. Start the game: the log shows a [mods] line for each file and texture "
                                                      "of the mod.\n"))}

    def act_new_mod(self, d):
        """{"name"} -> the mod made (or picked, when it exists) and selected; {"error"} when the name does not do."""
        n = (d.get("name") or "").strip()
        err = modkit.new_mod_name_error(n)
        if err:
            return {"error": "\"%s\": %s." % (n, err)}
        try:
            existed = os.path.isdir(os.path.join(self.mods, n))
            os.makedirs(os.path.join(self.mods, n), exist_ok=True)
            os.makedirs(modkit.work_dir(self.mods, n), exist_ok=True)
        except OSError as ex:
            return {"error": "Could not create mod %s: %s" % (n, ex)}
        self.say("Mod %s %s (work folder: %s)\n" % (n, "already exists, selected" if existed else "created", modkit.work_dir(self.mods, n)))
        return {"state": self.set_conf({"mod": n})}

    def act_open(self, d):
        """{"what": "mods" | "work"}: the folder in the system's file manager."""
        p = self.mods if d.get("what") == "mods" else self.work()
        if not p:
            return {"error": "Pick or create a mod first (\"Mod\" at the top)."}
        try:
            open_path(p)
        except OSError as ex:
            return {"error": str(ex)}
        return {"ok": True}

    def act_pick(self, d):
        r = self.dialogs.ask(d.get("kind", "file"), d.get("title", ""), d.get("initial", ""), d.get("types"))
        return {"path": r}


# ------------------------------------------------------------------ the server
def make_handler(kit, page_dir, vendor_dir, server_ref):
    class H(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *a):
            pass

        def _send(self, code, body, ctype, extra=None):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(body)

        def _json(self, obj, code=200):
            self._send(code, json.dumps(obj).encode("utf-8"), "application/json")

        def _file(self, root, rel):
            f = os.path.normpath(os.path.join(root, *[x for x in rel.split("/") if x]))
            if not f.startswith(os.path.normpath(root) + os.sep) or not os.path.isfile(f):
                return self._send(404, b"not found", "text/plain")
            with open(f, "rb") as fh:
                self._send(200, fh.read(), TYPES.get(os.path.splitext(f)[1].lower(), "application/octet-stream"))

        def do_GET(self):
            p = self.path.split("?", 1)[0]
            if p == "/api/hello":
                return self._json({"app": APP, "pid": os.getpid()})
            if p == "/api/state":
                return self._json(kit.state())
            if p == "/api/log":
                return self._events()
            if p == "/api/hud":   # a custom's HUD portrait
                from urllib.parse import parse_qs
                cid = (parse_qs(self.path.split("?", 1)[1]).get("cid") or [""])[0] if "?" in self.path else ""
                f = kit.portrait_of(cid) if cid else None
                if not f:
                    return self._send(404, b"not found", "text/plain")
                with open(f, "rb") as fh:
                    return self._send(200, fh.read(), TYPES.get(os.path.splitext(f)[1].lower(), "application/octet-stream"))
            if p in ("/api/portrait", "/api/model.glb"):
                from urllib.parse import parse_qs
                q = {k: v[0] for k, v in parse_qs(self.path.split("?", 1)[1] if "?" in self.path else "").items()}
                try:
                    f = kit.portrait(q.get("prefix", ""), int(q.get("k", "0"))) if p == "/api/portrait" else kit.model_for(q)
                except SystemExit as e:
                    return self._send(404, str(e).encode("utf-8"), "text/plain")
                except Exception as e:  # noqa: BLE001
                    kit.say(traceback.format_exc(), "err")
                    return self._send(500, str(e).encode("utf-8"), "text/plain")
                if not f or not os.path.isfile(f):
                    return self._send(404, b"not found", "text/plain")
                with open(f, "rb") as fh:
                    return self._send(200, fh.read(), TYPES.get(os.path.splitext(f)[1].lower(), "application/octet-stream"))
            if p == "/api/file":   # a work folder file (thumbnails, preview, sounds)
                from urllib.parse import parse_qs
                rel = (parse_qs(self.path.split("?", 1)[1]).get("rel") or [""])[0] if "?" in self.path else ""
                try:
                    f = kit.work_path(rel)
                except SystemExit:
                    return self._send(404, b"not found", "text/plain")
                if not os.path.isfile(f):
                    return self._send(404, b"not found", "text/plain")
                with open(f, "rb") as fh:
                    return self._send(200, fh.read(), TYPES.get(os.path.splitext(f)[1].lower(), "application/octet-stream"))
            if p.startswith("/vendor/"):
                return self._file(vendor_dir, p[len("/vendor/"):])
            return self._file(page_dir, "index.html" if p == "/" else p)

        def _events(self):
            """Server-sent events: every log line from Last-Event-ID (or ?since=) on, then the new ones as they come."""
            since = self.headers.get("Last-Event-ID") or (self.path.split("since=", 1)[1].split("&")[0] if "since=" in self.path else "0")
            try:
                n = int(since)
            except ValueError:
                n = 0
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            with kit.cond:
                kit.viewers += 1
            try:
                while not server_ref["stopping"]:
                    kit.last_seen = time.time()
                    with kit.cond:
                        if len(kit.log) <= n:
                            kit.cond.wait(5)   # a closed tab shows as a failed write within two of these
                        items = kit.log[n:]
                    if not items:
                        self.wfile.write(b": still here\n\n")
                    for it in items:
                        self.wfile.write(("id: %d\ndata: %s\n\n" % (it["seq"], json.dumps(dict(it, busy=kit.busy)))).encode("utf-8"))
                    n += len(items)
                    self.wfile.flush()
            except (OSError, ValueError):
                pass
            finally:
                with kit.cond:
                    kit.viewers -= 1
                kit.last_seen = time.time()
            self.close_connection = True

        def do_POST(self):
            n = int(self.headers.get("Content-Length") or 0)
            try:
                d = json.loads(self.rfile.read(n) or b"{}")
            except ValueError:
                d = {}
            p = self.path.split("?", 1)[0]
            if p == "/api/quit":
                self._json({"ok": True})
                server_ref["quit"].set()
                return None
            if p == "/api/conf":
                return self._json(kit.set_conf(d))
            if p.startswith("/api/do/"):
                f = getattr(kit, "act_" + p[len("/api/do/"):], None)
                if f is None:
                    return self._json({"error": "no such action"}, 404)
                try:
                    return self._json(f(d) or {})
                except SystemExit as e:
                    return self._json({"error": str(e)})
                except Exception as e:  # noqa: BLE001
                    kit.say(traceback.format_exc(), "err")
                    return self._json({"error": "%s: %s" % (type(e).__name__, e)})
            return self._json({"error": "not found"}, 404)

    return H


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        if isinstance(sys.exc_info()[1], (ConnectionError, TimeoutError)):   # a tab closed or reloaded mid-request
            return
        super().handle_error(request, client_address)


def running_kit(port):
    """True when a kit answers on this port."""
    if not port:
        return False
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/api/hello" % port, timeout=0.7) as r:
            return json.loads(r.read()).get("app") == APP
    except (OSError, ValueError):
        return False


def serve(open_browser=True, port=None, log=print):
    """Runs the kit's server until the page is gone (or POST /api/quit). Returns the exit code."""
    conf = load_conf()
    old = conf.get("web_port")
    if port is None and running_kit(old):   # one kit at a time: another tab on the one running
        url = "http://127.0.0.1:%d/" % old
        log("the mod kit is already open: %s" % url)
        if open_browser:
            webbrowser.open(url)
        return 0
    kit = Kit()
    page_dir, vendor_dir = _assets()
    ref = {"stopping": False, "quit": threading.Event()}
    srv = None
    for p in ([port] if port is not None else [old, 0] if old else [0]):
        try:
            srv = Server(("127.0.0.1", p), make_handler(kit, page_dir, vendor_dir, ref))
            break
        except OSError:
            continue
    if srv is None:
        log("could not open a port on 127.0.0.1")
        return 1
    real = srv.server_address[1]
    save_conf({"web_port": real})
    url = "http://127.0.0.1:%d/" % real
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    log("mod kit at %s (close the tab to quit)" % url)
    if open_browser:
        webbrowser.open(url)
    try:
        while not ref["quit"].wait(1.0):
            now = time.time()
            if kit.viewers > 0:
                continue
            gone = (now - kit.last_seen > GONE_AFTER) if kit.last_seen else (now - kit.started > FIRST_TAB)
            if gone and not kit.busy:
                log("the page is gone; quitting")
                break
    except KeyboardInterrupt:
        pass
    ref["stopping"] = True
    with kit.cond:
        kit.cond.notify_all()
    srv.shutdown()
    srv.server_close()
    return 0


def main(argv=None):
    global CONF
    argv = sys.argv[1:] if argv is None else argv
    if "--conf" in argv:
        CONF = argv[argv.index("--conf") + 1]
    port = int(argv[argv.index("--port") + 1]) if "--port" in argv else None
    return serve(open_browser="--no-browser" not in argv, port=port)


if __name__ == "__main__":
    sys.exit(main())
