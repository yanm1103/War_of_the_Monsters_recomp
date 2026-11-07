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
import base64
import contextlib
import io
import glob
import json
import math
import os
import re
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
import lvlprop  # noqa: E402 - new props from Blender (Import .glb)

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


def disc_check(disc):
    """None when the disc can be read, else (a word or two for the Settings badge, why not)."""
    if not disc:
        return "Not set", "Game disc not set."
    if os.path.isdir(disc):
        if os.path.isdir(os.path.join(disc, "LVL")):
            return None
        return "Not a disc", "This folder isn't an extracted disc. Pick the folder with LVL and PTS inside, or the .iso itself."
    if not os.path.isfile(disc):
        return "Not found", "File not found. Was it moved or renamed?"
    try:
        modkit.disc_iso(disc)
        return None
    except (OSError, ValueError, IndexError):   # IndexError: a file shorter than the ISO9660 header
        return "Can't read", "This file isn't a disc image the editor can read. Use the .iso (not .bin/.cue)."


def disc_error(disc):
    """None when the disc can be read, else why not."""
    bad = disc_check(disc)
    return bad[1] if bad else None


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


# lp.stats keys the summary names, in this order (the others are details of how the patch was laid out)
STAT_WORDS = (("moved", "moved", "moved"), ("copies", "copy", "copies"), ("deleted", "deleted", "deleted"),
              ("grid cells", "grid cell", "grid cells"), ("ped segments", "sidewalk", "sidewalks"),
              ("ped groups changed", "pedestrian group changed", "pedestrian groups changed"),
              ("ped groups added", "pedestrian group added", "pedestrian groups added"),
              ("ped groups deleted", "pedestrian group deleted", "pedestrian groups deleted"),
              ("ped groups off", "pedestrian group off", "pedestrian groups off"), ("props", "new prop", "new props"))


def stats_text(stats):
    return ", ".join("%d %s" % (stats[k], one if stats[k] == 1 else many) for k, one, many in STAT_WORDS
                     if stats.get(k)) or "nothing changed"


def hex_handle(h):
    try:
        return "0x%X" % int(str(h), 16)
    except ValueError:
        return str(h)


def check_patch(disc, name, patch, mod_dir=None, pts_root=None):
    """lvl.py patch on a fresh copy of the stage: the log lines, the path cuts and the collision check, and from them a summary,
    the problems ({level: err or warn, text, kind, handle and reason for an entry the game skips}) and notes. mod_dir: the mod
    whose props/ the "props" models come from (None: no mod chosen, so a patch with props says so)."""
    buf = io.StringIO()
    root = stage_root(disc, name)
    coll = {}
    with contextlib.redirect_stdout(buf):
        st = lvl.Stage(name, root)
        lp = lvl.LevelPatch(st)
        lp.apply(patch)
        if patch.get("props") and not mod_dir:
            lp.log.append("props: skipped: no mod chosen (a prop's model is in its mod's props folder)")
        elif mod_dir:
            try:
                lp.apply_props(patch, mod_dir)      # the game hook's order: objects, props, pedestrians, then the path cuts
            except lvl.PatchError as err:        # only a full arena gets here (as lvl.py patch)
                lp.log.append("props: stopped halfway: %s" % err)
        pts = lvl.stage_points(pts_root or root, name)
        nets = {"fodder": lvl.net_from_points(pts.get("FOP", [])), "ped": lvl.net_from_points(pts.get("PED", []))}
        try:
            lp.apply_peds(patch, nets)      # the game hook's order: objects, pedestrians, then the path cuts
        except lvl.PatchError as err:       # only a full arena gets here (as lvl.py patch)
            lp.log.append("pedestrians: stopped halfway: %s" % err)
        for line in lp.log:
            print(line)
        print("%s: %s" % (name, ", ".join("%s %d" % kv for kv in sorted(lp.stats.items())) or "nothing changed"))
        cuts = lvl.cut_nets(lp.footprints, nets)
        report = lp.ped_report(nets) if patch.get("peds") else []
        for line in cuts + report:
            print(line)
        st.grid = lvl.Grid(st.img, st.grid.addr)
        lvl.check_collision(st, coll)
    objs = patch.get("objects") if isinstance(patch.get("objects"), list) else []
    props = patch.get("props") if isinstance(patch.get("props"), list) else []
    problems, infos = [], []
    for line in lp.log:
        m = re.match(r"entry (\d+) skipped: (.*)$", line)
        if m:
            k = int(m.group(1))
            e = objs[k] if k < len(objs) and isinstance(objs[k], dict) else {}
            h = hex_handle(e["handle"]) if e.get("handle") is not None else ""
            problems.append({"level": "err", "kind": "entry", "handle": h, "reason": m.group(2),
                             "text": "%s was not applied: %s" % (h or "Entry %d" % (k + 1), m.group(2))})
            continue
        m = re.match(r"grid skipped: (.*)$", line)
        if m:
            problems.append({"level": "err", "kind": "grid", "reason": m.group(1), "text": "The stage size was not applied: %s" % m.group(1)})
            continue
        m = re.match(r"pedestrian group (\d+) skipped: (.*)$", line)
        if m:
            problems.append({"level": "err", "kind": "ped", "reason": m.group(2), "index": int(m.group(1)),
                             "text": "Pedestrian group %d was not applied: %s" % (int(m.group(1)) + 1, m.group(2))})
            continue
        m = re.match(r"pedestrians: stopped halfway: (.*)$", line)
        if m:
            problems.append({"level": "err", "kind": "ped", "text": "The pedestrians were applied only in part: %s" % m.group(1)})
            continue
        m = re.match(r"prop (\d+)(?: \(.*?\))? skipped: (.*)$", line)
        if m:
            k = int(m.group(1))
            pe = props[k] if k < len(props) and isinstance(props[k], dict) else {}
            problems.append({"level": "err", "kind": "prop", "index": k, "id": pe.get("id"), "reason": m.group(2),
                             "text": "Prop %s was not applied: %s" % (pe.get("name") or pe.get("id") or k + 1, m.group(2))})
            continue
        m = re.match(r"props of model (\S+) skipped \(\d+\): (.*)$", line)
        if m:                               # every entry of that model
            for k, pe in enumerate(props):
                if isinstance(pe, dict) and str(pe.get("model") or "").upper() == m.group(1):
                    problems.append({"level": "err", "kind": "prop", "index": k, "id": pe.get("id"), "reason": m.group(2),
                                     "text": "Prop %s was not applied: %s" % (pe.get("name") or pe.get("id") or k + 1,
                                                                              m.group(2))})
            continue
        m = re.match(r"props: (stopped halfway|skipped): (.*)$", line)
        if m:
            problems.append({"level": "err", "kind": "prop", "text": "The props were not applied%s: %s" % (
                " in full" if m.group(1) == "stopped halfway" else "", m.group(2))})
    if coll.get("box_bad"):
        problems.append({"level": "err", "kind": "collision",
                         "text": "Collision boxes: %d out of place (worst %.1f units)" % (coll["box_bad"], coll["worst"])})
    if coll.get("cell_ok", 0) < coll.get("paths", 0):
        problems.append({"level": "err", "kind": "collision",
                         "text": "Collision cells: %d boxes listed in the wrong cells" % (coll["paths"] - coll["cell_ok"])})
    for line in report:
        m = re.match(r"pedestrians: group \S+ \((\d+)\) at .* has no sidewalk", line)
        if m:
            problems.append({"level": "warn", "kind": "ped",
                             "text": "A pedestrian group of %s people has no sidewalk near it, so nobody walks there" % m.group(1)})
    cars = sum(1 for l in cuts if re.match(r"vehicles: link \d+-\d+ cut", l))
    walks = sum(1 for l in cuts if re.match(r"pedestrians: segment \d+-\d+ (removed|\(.*clipped)", l))
    cut = [("%d car %s" % (n, "path" if n == 1 else "paths")) if i == 0 else ("%d sidewalk %s" % (n, "segment" if n == 1 else "segments"))
           for i, n in enumerate((cars, walks)) if n]
    if cut:
        infos.append("Cut %s under the objects" % " and ".join(cut))
    n = sum(1 for p in problems if p["level"] == "err")
    summary = ("Check passed: %s. Collision OK." % stats_text(lp.stats)) if not n else \
        "Check found %d %s" % (n, "problem" if n == 1 else "problems")
    return {"ok": not n, "summary": summary, "problems": problems, "infos": infos, "collision": coll,
            "lines": buf.getvalue().splitlines()}


def bad_mod(mod):
    """A mod name the editor refuses: empty, a path, or one the game skips (leading _ or .)."""
    return not mod or any(c in mod for c in "\\/:*?\"<>|") or mod.startswith(("_", "."))


def bad_mod_reason(mod):
    if not mod:
        return "Choose a mod to save into (Mod, at the top)."
    return "A mod name can't contain \\ / : * ? \" < > | or start with _ or ."


def patch_path(cfg, mod, name):
    return os.path.join(cfg["mods"], mod, "levels", name + ".json")


# ------------------------------------------------------------------ custom stages (mods/<mod>/stages/<folder>/stage.json)
CUSTOM_BASES = ("CENTRAL", "VEGAS", "CANYON2", "METRO", "UFO", "CAPITOL", "CANYON1", "MINISF", "TOKYODM")
CUSTOM_ID = re.compile(r"^[A-Za-z][A-Za-z0-9]{0,7}$")


def custom_stages(cfg, refused=False):
    """The valid custom stages of every mod: {ID (capitals): {id, name, base, mod, folder}}; with refused=True also the ones the
    game refuses, which carry a reason. Two mods with one id: the last by name, as the game."""
    out = {}
    for mod in sorted(list_mods(cfg), key=str.lower):
        sdir = os.path.join(cfg["mods"], mod, "stages")
        if not os.path.isdir(sdir):
            continue
        for folder in sorted(os.listdir(sdir)):
            path = os.path.join(sdir, folder, "stage.json")
            if not os.path.isfile(path):
                continue
            try:
                with open(path, encoding="utf-8") as f:
                    j = json.load(f)
                sid, base = str(j.get("id") or ""), str(j.get("base") or "").upper()
                name = str(j.get("name") or sid.upper())
            except (OSError, ValueError, AttributeError) as e:
                out["?" + mod + "/" + folder] = {"id": folder, "mod": mod, "folder": folder, "reason": "stage.json: %s" % e}
                continue
            reason = ""
            if not CUSTOM_ID.match(sid):
                reason = "the id must be 1 to 8 letters and digits, starting with a letter"
            elif sid.upper() in LEVEL_NUM or sid.upper() in ("TRAINING",):
                reason = "the id repeats a game stage"
            elif base not in CUSTOM_BASES:
                reason = "the base must be one of " + ", ".join(CUSTOM_BASES)
            if reason and not refused:
                continue
            out[sid.upper() or "?" + mod + "/" + folder] = {"id": sid, "name": name[:17], "base": base, "mod": mod,
                                                         "folder": folder, "reason": reason}
    return out


def base_of(cfg, name):
    """The game stage whose files a stage name reads: a custom stage's base, else the name itself."""
    c = custom_stages(cfg).get((name or "").upper())
    return c["base"] if c else name


POINTS_LOCK = threading.Lock()


def points_root(cfg, name):
    """A folder with PTS/<BASE>.<ext> for the stage: the base's points, except for the kinds the custom stage's own folder has
    (any file name with that extension, as the game takes them). None when the stage has no points of its own: use the disc's."""
    c = custom_stages(cfg).get((name or "").upper())
    if not c:
        return None
    own = {}
    folder = os.path.join(cfg["mods"], c["mod"], "stages", c["folder"])
    for f in sorted(os.listdir(folder)):
        ext = f.rsplit(".", 1)[-1].upper()
        if ext in lvl.POINT_EXTS and ext not in own and f.upper() != "STAGE.JSON":
            own[ext] = os.path.join(folder, f)
    if not own:
        return None
    base_root = stage_root(cfg["disc"], c["base"])
    root = os.path.join(CACHE, "pts-%s-%s" % (disc_key(cfg["disc"]), c["id"].upper()))
    with POINTS_LOCK:           # the HTTP server is threaded: two requests for one stage share this folder
        os.makedirs(os.path.join(root, "PTS"), exist_ok=True)
        for ext in lvl.POINT_EXTS:
            src = own.get(ext) or os.path.join(base_root, "PTS", "%s.%s" % (c["base"], ext))
            dst = os.path.join(root, "PTS", "%s.%s" % (c["base"], ext))
            if os.path.isfile(src):
                if not (os.path.isfile(dst) and os.path.getsize(dst) == os.path.getsize(src) and
                        open(dst, "rb").read() == open(src, "rb").read()):
                    shutil.copyfile(src, dst)
            elif os.path.exists(dst):
                os.remove(dst)
    return root


# the stages whose select picture a mod's levelpics/<name>.png replaces (SHELL.TEX has none of them), and the custom stages' <id>
LEVELPIC_BASE = {"TRAINING": "training", "VEGAS": "lakepowell", "TOKYODM": "tokyodeathmatch"}


def save_levelpic(cfg, b):
    """Stage picture…: mods/<mod>/levelpics/<name>.png from a PNG (base64) of the view, 2:1. Only for a custom stage (its id) and
    the three game stages that have no picture of their own."""
    mod, stage = (b.get("mod") or "").strip(), (b.get("name") or "").upper()
    if bad_mod(mod):
        raise ValueError(bad_mod_reason(mod))
    c = custom_stages(cfg).get(stage)
    pic = c["id"].lower() if c else LEVELPIC_BASE.get(stage)
    if not pic:
        raise ValueError("The select picture of %s comes from the game's own files: make a custom stage on it first (+)." % stage)
    data = base64.b64decode(b.get("png") or "")
    if data[:8] != b"\x89PNG\r\n\x1a\n" or len(data) > 8 << 20:
        raise ValueError("Not a PNG picture (or over 8 MB).")
    w, h = struct.unpack(">II", data[16:24])
    if w != 2 * h:
        raise ValueError("The picture must be 2:1 (it is %dx%d)." % (w, h))
    path = os.path.join(cfg["mods"], mod, "levelpics", pic + ".png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path + ".tmp", "wb") as f:
        f.write(data)
    os.replace(path + ".tmp", path)
    return {"saved": path, "size": [w, h]}


def hud_target(cfg, stage, mod):
    """(mods folder, mod, base stage, sub folder, custom) for the HUD panel. A custom stage keeps its HUD pictures in its own
    folder (stages/<folder>/<BASE>_tNNNN.png, which only that stage takes); the others go in the chosen mod's LVL/."""
    c = custom_stages(cfg).get((stage or "").upper())
    if not c:
        return cfg["mods"], mod, stage, "LVL", False
    return os.path.join(cfg["mods"], c["mod"], "stages"), c["folder"], c["base"], "", True


def new_stage(cfg, b):
    """New stage…: mods/<mod>/stages/<id>/stage.json (id, name, base) and, with a patch, levels/<ID>.json. Returns the listing
    entry. The game reads its registry at boot, so it sees the stage after a restart (Test in game restarts it)."""
    mod, sid, base = (b.get("mod") or "").strip(), (b.get("id") or "").strip(), (b.get("base") or "").strip().upper()
    if bad_mod(mod):
        raise ValueError(bad_mod_reason(mod))
    if not CUSTOM_ID.match(sid):
        raise ValueError("The id is 1 to 8 letters and digits, starting with a letter.")
    if sid.upper() in LEVEL_NUM or sid.upper() in custom_stages(cfg):
        raise ValueError("A stage called %s exists already." % sid.upper())
    if base not in CUSTOM_BASES:
        raise ValueError("The base must be one of %s." % ", ".join(CUSTOM_BASES))
    name = (b.get("name") or "").strip() or sid.upper()
    if len(name) > 17 or not all(32 <= ord(c) < 127 for c in name):
        raise ValueError("The name is up to 17 ASCII characters.")
    folder = os.path.join(cfg["mods"], mod, "stages", sid.lower())
    if os.path.exists(folder) or sid.upper() in custom_stages(cfg, refused=True):
        raise ValueError("A stage folder or id %s exists already (a refused one counts: see the game log)." % sid.upper())
    os.makedirs(folder)
    with open(os.path.join(folder, "stage.json"), "w", encoding="utf-8") as f:
        json.dump({"id": sid, "name": name, "base": base}, f, indent=1)
    patch = b.get("patch")
    if isinstance(patch, dict):
        save_patch(cfg, mod, sid.upper(), patch)
    return {"name": sid.upper(), "title": name, "level": LEVEL_NUM.get(base), "custom": True, "base": base, "mod": mod, "id": sid}


def stage_list(cfg):
    """The disc's stages, then the valid custom stages (name = the id in capitals, "custom": true, "base", "mod") and, with
    their reason, the refused ones."""
    base = STAGES.list(cfg["disc"])
    if not base:
        return base
    names = {s["name"] for s in base}
    for key, c in custom_stages(cfg).items():
        if c["base"] not in names:
            continue
        base.append({"name": key, "title": c["name"], "level": LEVEL_NUM.get(c["base"]), "custom": True, "base": c["base"],
                     "mod": c["mod"], "id": c["id"]})
    return base


# ------------------------------------------------------------------ new props (tools/lvlprop.py; the patch's "props")
def mod_folder(cfg, mod):
    """The mod's folder, where the "props" models are (props/<MODEL>/); None with no mod or a bad name."""
    mod = (mod or "").strip()
    return None if bad_mod(mod) else os.path.join(cfg["mods"], mod)


def props_dir(cfg, mod):
    d = mod_folder(cfg, mod)
    return lvl._child_nocase(d, "props") if d and os.path.isdir(d) else None


def prop_light(m):
    """How the prop is lit: vertex, dir, or mixed (auto picks per object: vertex where the mesh has vertex colors)."""
    got = {o.get("light") for o in m.get("objects") or [] if isinstance(o, dict) and o.get("light")}
    return got.pop() if len(got) == 1 else "mixed" if got else m.get("light")


def props_list(cfg, mod):
    """The mod's props (props/*/prop.json): model (the folder's name, as the patch names it), name, stage, counts, bounds and
    light; `error` for a folder whose prop.json does not read."""
    d = props_dir(cfg, mod)
    out = []
    if not d or not os.path.isdir(d):
        return out
    for n in sorted(os.listdir(d), key=str.lower):
        f = os.path.join(d, n, "prop.json")
        if not os.path.isfile(f):
            continue
        e = {"model": n.upper(), "name": n}
        try:
            with open(f, encoding="utf-8") as fh:
                m = json.load(fh)
            pv = os.path.join(d, n, (m.get("files") or {}).get("preview", ""))
            e.update(name=m.get("name") or n, stage=(m.get("stage") or "").upper(), triangles=m.get("triangles", 0),
                     bounds=m.get("bounds"), light=prop_light(m), bytes=m.get("bytes"), units=m.get("units"),
                     scale=m.get("scale"), preview=os.path.isfile(pv),
                     destructible=({"class": (m["destructible"] or {}).get("class"), "states": len((m["destructible"] or {}).get("states") or [])}
                                    if m.get("destructible") else None))
        except (OSError, ValueError, AttributeError) as err:
            e["error"] = "prop.json does not read: %s" % err
        out.append(e)
    return out


def prop_preview(cfg, mod, model):
    """Path of a model's <MODEL>.preview.glb (what the game draws), or None."""
    d = props_dir(cfg, mod)
    if not d or not model or any(c in model for c in "\\/:*?\"<>|") or model.startswith("."):
        return None
    pdir = lvl._child_nocase(d, model)
    try:
        with open(os.path.join(pdir, "prop.json"), encoding="utf-8") as f:
            m = json.load(f)
        path = os.path.join(pdir, (m.get("files") or {}).get("preview", ""))
    except (OSError, ValueError, AttributeError):
        return None
    return path if os.path.isfile(path) else None


def import_prop(cfg, b):
    """Import .glb: writes the .glb as props/<NAME>/source.glb of the mod and builds the prop there (lvlprop.build, for the
    stage). An existing model is replaced only with `replace`. Returns {"prop": props_list entry, "lines"}."""
    mod, stage = (b.get("mod") or "").strip(), base_of(cfg, (b.get("stage") or "").upper()).upper()
    if bad_mod(mod):
        raise ValueError(bad_mod_reason(mod))
    name = (b.get("name") or "").strip().upper()
    if not lvlprop.NAME_RE.match(name):
        raise ValueError("A prop name has 1 to 8 letters, digits or _")
    root = os.path.join(cfg["mods"], mod)
    pd = lvl._child_nocase(root, "props") or os.path.join(root, "props")
    folder = lvl._child_nocase(pd, name) or os.path.join(pd, name)
    existed = os.path.isdir(folder)
    if existed and not b.get("replace"):
        return {"exists": True}
    data = base64.b64decode(b.get("data") or "")
    if data[:4] != b"glTF":
        raise ValueError("That file is not a binary glTF (.glb). In Blender: File > Export > glTF 2.0, format glTF Binary.")
    os.makedirs(folder, exist_ok=True)
    src = os.path.join(folder, "source.glb")
    old = None
    if existed and os.path.isfile(src):
        with open(src, "rb") as f:
            old = f.read()
    with open(src, "wb") as f:
        f.write(data)
    lines = []
    try:
        height = float(b["height"]) if b.get("height") not in (None, "") else None
        lvlprop.build(src, folder, stage, b.get("units") or "blender", None, height, name, (b.get("title") or "").strip() or name,
                      b.get("light") or "auto", stage_root(cfg["disc"], stage), log=lines.append)
    except Exception:
        if not existed:
            shutil.rmtree(folder, ignore_errors=True)
        elif old is not None:
            with open(src, "wb") as f:
                f.write(old)
        raise
    entry = next((e for e in props_list(cfg, mod) if e["model"] == os.path.basename(folder).upper()), None)
    return {"prop": entry, "lines": lines, "replaced": existed}


def edit_prop(cfg, b):
    """A model of the mod changed in place: `title` (prop.json "name", what the list shows; empty = the folder's name) and/or
    `height` (game units: built again from its source .glb, scaled about the origin, with the same units, light and template ABE;
    every entry of the model takes it). Returns {"prop": props_list entry, "lines"}."""
    mod = (b.get("mod") or "").strip()
    if bad_mod(mod):
        raise ValueError(bad_mod_reason(mod))
    model = (b.get("model") or "").strip().upper()
    d = props_dir(cfg, mod)
    folder = lvl._child_nocase(d, model) if d and lvlprop.NAME_RE.match(model) else None
    pj = os.path.join(folder, "prop.json") if folder else None
    if not pj or not os.path.isfile(pj):
        raise ValueError("Mod %s has no prop %s" % (mod, model))
    with open(pj, encoding="utf-8") as f:
        m = json.load(f)
    title = b.get("title")
    if title is not None:
        title = " ".join(str(title).split())[:64] or m.get("model") or model
    lines = []
    if b.get("strength") not in (None, ""):      # a destructible's hit point class: only prop.json changes (read at every load)
        try:
            cls = int(b["strength"])
        except (TypeError, ValueError):
            cls = 0
        if not m.get("destructible"):
            raise ValueError("%s is not destructible (a .glb with state1_ meshes makes one)" % model)
        if cls not in lvlprop.STRENGTH:
            raise ValueError("The strength is a hit point class, 1 to 5")
        m["destructible"]["class"] = cls
        with open(pj, "w", encoding="utf-8") as f:
            json.dump(m, f, indent=1)
            f.write("\n")
    if b.get("height") not in (None, ""):
        try:
            h = float(b["height"])
        except (TypeError, ValueError):
            raise ValueError("The height is a number of game units")
        if not 0.1 <= h <= 100000:
            raise ValueError("The height goes from 0.1 to 100000 game units")
        lo, hi = m["bounds"]["min"], m["bounds"]["max"]
        old = hi[2] - lo[2]
        if old <= 1e-6 or not m.get("scale"):
            raise ValueError("%s is flat: it has no height to scale" % model)
        src = os.path.join(folder, "source.glb")
        if not os.path.isfile(src):
            src = os.path.normpath(os.path.join(folder, m.get("source") or "source.glb"))
        if not os.path.isfile(src):
            raise ValueError("The .glb %s was made from is gone (%s): import it again" % (model, m.get("source")))
        backup = {}
        for n in os.listdir(folder):
            p = os.path.join(folder, n)
            if os.path.isfile(p) and n.lower() != "source.glb":
                with open(p, "rb") as f:
                    backup[n] = f.read()
        stage = (m.get("stage") or "").upper()
        try:
            lvlprop.build(src, folder, stage, m.get("units") or "blender", m["scale"] * h / old, None, m.get("model") or model,
                          title if title is not None else m.get("name"), m.get("light") or "auto", stage_root(cfg["disc"], stage),
                          abe=(m.get("template") or {}).get("abe", 1),
                          strength=(m.get("destructible") or {}).get("class", lvlprop.DEFAULT_STRENGTH), log=lines.append)
        except Exception:
            for n in os.listdir(folder):
                if n not in backup and n.lower() != "source.glb":
                    os.remove(os.path.join(folder, n))
            for n, data in backup.items():
                with open(os.path.join(folder, n), "wb") as f:
                    f.write(data)
            raise
    elif title is not None:
        m["name"] = title
        with open(pj, "w", encoding="utf-8") as f:
            json.dump(m, f, indent=1)
            f.write("\n")
    entry = next((e for e in props_list(cfg, mod) if e["model"] == os.path.basename(folder).upper()), None)
    return {"prop": entry, "lines": lines}


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
    """The [fase] lines of the game's log (what the patch did in the game); none from a log older than the game the editor
    opened (the release exe moves the old wotm.log aside only once it runs)."""
    try:
        # (file times move in clock ticks of ~16 ms: a small margin)
        if GAME["proc"] and os.path.getmtime(game_log(cfg)) < GAME["started"] - 0.05:
            return []
        with open(game_log(cfg), encoding="utf-8", errors="replace") as f:
            return [l.rstrip() for l in f if l.startswith("[fase]")]
    except OSError:
        return []


def save_patch(cfg, mod, name, patch):
    """Writes (or, empty, removes) mods/<mod>/levels/<STAGE>.json; through a .tmp, since a running game may read it at any time."""
    path = patch_path(cfg, mod, name)
    # any key besides an empty objects list keeps the file: names, peds, and keys of a newer editor or written by hand
    if patch.get("objects") or any(k != "objects" for k in patch):
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


# The game the editor opened (its process, the stage it opened on, its log file), and the file it watches for "reload the
# stage" (WOTM_LEVELRELOAD: "<STAGE> <counter>")
GAME = {"proc": None, "stage": "", "logf": None, "started": 0.0,
        "count": int(time.time())}      # count from the clock: a new editor never repeats an old counter
RELOAD_FILE = os.path.join(CACHE, "reload.txt")


def game_running():
    p = GAME["proc"]
    return p is not None and p.poll() is None


def stop_game():
    """Closes the game the editor opened (only that process). True when one was running."""
    p, was = GAME["proc"], game_running()
    if was:
        p.terminate()
        try:
            p.wait(5)
        except subprocess.TimeoutExpired:
            pass
    if GAME["logf"]:
        GAME["logf"].close()
        GAME["logf"] = None
    return was


class SettingsError(RuntimeError):
    """A setting is missing or wrong: the page offers to open Settings (code "settings")."""


def reload_game(name):
    if not game_running():
        raise RuntimeError("no game opened by the editor is running: use Test in game")
    GAME["count"] += 1
    with open(RELOAD_FILE + ".tmp", "w", encoding="utf-8") as f:
        f.write("%s %d\n" % (name, GAME["count"]))
    os.replace(RELOAD_FILE + ".tmp", RELOAD_FILE)


def launch_game(cfg, name, training):
    if not cfg.get("game") or not os.path.exists(cfg["game"]):
        raise SettingsError("Can't find the game (wotm.exe).")
    env = dict(os.environ)
    os.makedirs(CACHE, exist_ok=True)
    env.update({"WOTM_MODS": os.path.abspath(cfg["mods"]), "WOTM_MATCH": ("3,1,@" + custom_stages(cfg)[name.upper()]["id"]) if name.upper() in custom_stages(cfg)
                else "3,%d" % LEVEL_NUM.get(name, 1),
                "WOTM_FULLSCREEN": "0", "WOTM_LEVELPATCH": "1", "WOTM_LEVELRELOAD": RELOAD_FILE})
    if training:
        env["WOTM_TRAINING"] = "1"
    if cfg.get("elf"):
        # the clone's runtime run as run_live.ps1 does: the disc image, native movies and drawing, no emulated VU1/VIF1/GS
        iso = cfg.get("iso") or (cfg["disc"] if is_iso(cfg["disc"]) else "")
        if not iso or not os.path.exists(iso):
            raise SettingsError("The dev runtime needs a disc image (.iso).")
        env.update({"PS2X_CD_IMAGE": iso, "PS2X_FMV": "1", "PS2X_SCENE": "1",
                    "PS2X_NO_VU1": "1", "PS2X_NO_VIF1": "1", "PS2X_NO_GS": "1"})
    args = [cfg["game"]] + ([cfg["elf"]] if cfg.get("elf") else [])
    cwd = cfg.get("cwd") or os.path.dirname(cfg["game"])
    # with the ELF (the clone's runtime) stderr is the log; with no argument wotm.exe writes wotm.log itself
    os.makedirs(CACHE, exist_ok=True)
    stop_game()     # closes the log file of a game that has exited
    GAME["started"] = time.time()
    log = open(game_log(cfg), "w") if cfg.get("elf") else subprocess.DEVNULL
    GAME["proc"] = subprocess.Popen(args, cwd=cwd, env=env, stdout=subprocess.DEVNULL, stderr=log)
    GAME["stage"] = name
    GAME["logf"] = log if cfg.get("elf") else None
    return " ".join(args)


def is_clone():
    """Run from the repo (not the release exe): Settings shows the developer fields (ELF, working folder, dev disc)."""
    return not getattr(sys, "frozen", False) and os.path.isdir(os.path.join(REPO, ".git"))


def init_reply(cfg):
    return {"config": cfg, "stages": stage_list(cfg), "mods": list_mods(cfg), "discError": disc_error(cfg["disc"]),
            "gameRunning": game_running(), "gameStage": GAME["stage"] if game_running() else "",
            "clone": is_clone() or bool(cfg.get("elf")), "canBrowse": BROWSE["window"] is not None}


def path_states(b):
    """Settings' badges for the paths typed so far: {key: {state: ok, warn, err or idle, short, text}} for the keys given."""
    out = {}

    def put(key, state, short, text=""):
        out[key] = {"state": state, "short": short, "text": text}

    def file_state(key, v, empty):
        if not v:
            put(key, *empty)
        elif os.path.isfile(v):
            put(key, "ok", "Found")
        else:
            put(key, "err", "Not found", "Folder, not a file." if os.path.isdir(v) else "File not found. Was it moved or renamed?")

    def disc_state(key, v, empty):
        if not v and empty:
            return put(key, *empty)
        bad = disc_check(v)
        if bad:
            return put(key, "warn" if not v else "err", *bad)
        n = len(STAGES.list(v))
        if not n:
            return put(key, "err", "No stages", "No stages on this disc. Is it War of the Monsters (NTSC-U)?")
        put(key, "ok", "Found", "%s, %d stages" % ("Extracted folder" if os.path.isdir(v) else "Disc image", n))

    v = lambda k: str(b.get(k) or "").strip()     # noqa: E731
    if "disc" in b:
        disc_state("disc", v("disc"), None)
    if "mods" in b:
        d = v("mods")
        if not d:
            put("mods", "err", "Not set", "Saving needs the folder the game reads mods from.")
        elif os.path.isdir(d):
            n = len([m for m in os.listdir(d) if os.path.isdir(os.path.join(d, m)) and not m.startswith(("_", "."))])
            put("mods", "ok", "Found", "%d %s" % (n, "mod" if n == 1 else "mods"))
        elif os.path.isfile(d):
            put("mods", "err", "Not a folder", "This is a file, not a folder.")
        else:
            put("mods", "warn", "Not found", "The folder is created when you save.")
    if "game" in b:
        file_state("game", v("game"), ("warn", "Not set", "Test in game needs it."))
    if "elf" in b:
        file_state("elf", v("elf"), ("idle", "Not used", "The release wotm.exe takes no ELF."))
    if "cwd" in b:
        d = v("cwd")
        if not d:
            put("cwd", "idle", "The game's folder")
        else:
            put("cwd", *(("ok", "Found") if os.path.isdir(d) else ("err", "Not found", "Folder not found.")))
    if "iso" in b:
        if os.path.isdir(v("iso")):
            put("iso", "err", "Not an .iso", "The dev runtime reads a disc image, not a folder.")
        else:
            disc_state("iso", v("iso"), ("idle", "Not used", "Empty: the game disc, when it is an .iso."))
    return out


# Browse buttons in Settings: only in the pywebview window (its file dialog); a plain browser gives the page no file paths
BROWSE = {"window": None}


def browse(kind, current):
    """The pywebview file dialog for one Settings field; the path picked, or "" when cancelled."""
    import webview      # noqa: PLC0415 - only with the pywebview window
    w = BROWSE["window"]
    fd = getattr(webview, "FileDialog", None)
    folder = fd.FOLDER if fd else webview.FOLDER_DIALOG
    opener = fd.OPEN if fd else webview.OPEN_DIALOG
    cur = (current or "").strip()
    start = cur if os.path.isdir(cur) else os.path.dirname(cur) if cur and os.path.isdir(os.path.dirname(cur)) else ""
    if kind in ("mods", "cwd"):
        r = w.create_file_dialog(folder, directory=start)
    else:
        types = {"disc": ("Disc image (*.iso)", "All files (*.*)"), "iso": ("Disc image (*.iso)", "All files (*.*)"),
                 "game": ("Program (*.exe)", "All files (*.*)") if os.name == "nt" else ("All files (*.*)",)}
        r = w.create_file_dialog(opener, directory=start, file_types=types.get(kind, ("All files (*.*)",)))
    if isinstance(r, str):
        return r
    return r[0] if r else ""


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
                base = base_of(cfg, q["name"])      # a custom stage reads its base's files, and its own patch
                st, objs, on = STAGES.objects(cfg["disc"], base)
                patch = {}
                if q.get("mod"):
                    try:
                        with open(patch_path(cfg, q["mod"], q["name"]), encoding="utf-8") as f:
                            patch = json.load(f)
                    except (OSError, ValueError):
                        pass
                facts = STAGES.facts(cfg["disc"], base)
                # the mods with a file for this stage, and the one the game uses (the last by name)
                mod = q.get("mod", "")
                have = [m for m in list_mods(cfg) if os.path.isfile(patch_path(cfg, m, q["name"]))]
                winner = patch_winner(cfg, mod, q["name"]) if mod and not bad_mod(mod) else \
                    (sorted(have, key=str.lower)[-1] if have else "")
                return self.send(200, {"objects": describe(q["name"], objs, on, facts, load_names()), "patch": patch,
                                       "env": stage_env(cfg, cfg["disc"], base, mod),
                                       "grid": {"min": [st.grid.min_x, st.grid.min_y], "max": [st.grid.max_x, st.grid.max_y],
                                                "cell": st.grid.cell, "cols": st.grid.cols, "rows": st.grid.rows} if st.grid else None,
                                       "peds": pedsapi.stage_peds(st, points_root(cfg, q["name"]) or stage_root(cfg["disc"], base)),
                                       "have": have, "winner": winner, "others": [m for m in have if m != mod]})
            if u.path == "/api/props":          # the mod's props (props/*/prop.json)
                return self.send(200, {"props": props_list(cfg, q.get("mod", ""))})
            if u.path == "/api/propglb":        # a prop's preview (what the game draws)
                path = prop_preview(cfg, q.get("mod", ""), q.get("model", ""))
                if not path:
                    return self.send(404, {"error": "not found"})
                with open(path, "rb") as f:
                    return self.send(200, f.read(), "model/gltf-binary")
            if u.path == "/api/hud":
                mod = q.get("mod", "").strip()
                mods_dir, mod_arg, base, _sub, _c = hud_target(cfg, q["stage"], mod if mod and not bad_mod(mod) else "")
                return self.send(200, HUD.panel(cfg["disc"], mods_dir, mod_arg, base))
            if u.path == "/api/hudimg":
                mod = q.get("mod", "").strip()
                mods_dir, mod_arg, base, _sub, _c = hud_target(cfg, q["stage"], "" if bad_mod(mod) else mod)
                path = HUD.image(cfg["disc"], mods_dir, mod_arg, base, q.get("src"), q.get("png"))
                if not path or not os.path.isfile(path):
                    return self.send(404, {"error": "not found"})
                with open(path, "rb") as f:
                    return self.send(200, f.read(), "image/png")
            if u.path == "/api/gamelog":
                run = game_running()
                return self.send(200, {"lines": game_stage_lines(cfg), "running": run, "stage": GAME["stage"] if run else ""})
            if u.path == "/api/glb":
                with open(STAGES.glb(cfg["disc"], base_of(cfg, q["name"])), "rb") as f:
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
            if self.path == "/api/levelpic":
                try:
                    return self.send(200, save_levelpic(cfg, b))
                except ValueError as e:
                    return self.send(400, {"error": str(e), "code": "levelpic"})
            if self.path == "/api/stage/new":
                try:
                    return self.send(200, new_stage(cfg, b))
                except ValueError as e:
                    return self.send(400, {"error": str(e), "code": "newstage"})
            if self.path == "/api/names":
                if not b.get("sig"):
                    return self.send(400, {"error": "this object has no mesh to name as a model"})
                set_model_name(b["sig"], (b.get("name") or "").strip(),
                               {"kind": b.get("kind"), "type": b.get("ptype"), "dims": b.get("dims")})
                return self.send(200, {"ok": True, "file": USER_NAMES})
            if self.path == "/api/check":
                return self.send(200, check_patch(cfg["disc"], base_of(cfg, b["name"]), b["patch"], mod_folder(cfg, b.get("mod")),
                                                  points_root(cfg, b["name"])))
            if self.path == "/api/paths":       # Settings' badges for what is typed (nothing is saved)
                return self.send(200, path_states(b))
            if self.path == "/api/props/import":   # Import .glb: a new prop in the mod, built for the open stage
                try:
                    return self.send(200, import_prop(cfg, b))
                except (ValueError, lvlprop.PropError, lvlprop.ngpmesh.MeshError, lvlprop.vifpkt.FormatError) as e:
                    return self.send(400, {"error": str(e), "code": "prop"})
            if self.path == "/api/props/edit":     # a model's name in the list or its height (built again from its .glb)
                try:
                    return self.send(200, edit_prop(cfg, b))
                except (ValueError, KeyError, OSError, lvlprop.PropError, lvlprop.ngpmesh.MeshError, lvlprop.vifpkt.FormatError) as e:
                    return self.send(400, {"error": str(e), "code": "prop"})
            if self.path == "/api/browse":
                if BROWSE["window"] is None:
                    return self.send(400, {"error": "Browse works in the editor's own window only: type the path."})
                return self.send(200, {"path": browse(b.get("kind", ""), b.get("path", ""))})
            if self.path == "/api/bye":         # the page closed: see main()
                LIFE["bye"] = time.monotonic()
                return self.send(200, {"ok": True})
            if self.path == "/api/stop":        # only the game this editor opened
                return self.send(200, {"ok": True, "stopped": stop_game(), "running": False})
            if self.path in ("/api/save", "/api/test", "/api/reload", "/api/hud/replace", "/api/hud/remove"):
                mod = (b.get("mod") or "").strip()
                # Test without saving writes nothing: no mod needed
                if bad_mod(mod) and not (self.path == "/api/test" and not b.get("save", True) and not mod):
                    return self.send(400, {"error": bad_mod_reason(mod), "code": "mod"})
                if self.path in ("/api/hud/replace", "/api/hud/remove"):
                    mods_dir, mod_arg, base, sub, custom = hud_target(cfg, b["stage"], mod)
                    every = bool(b.get("every")) and not custom and b.get("role") != "sky"   # a custom stage's picture and a sky are its own
                    if self.path == "/api/hud/replace":
                        return self.send(200, {"lines": HUD.replace(cfg["disc"], mods_dir, mod_arg, base, b["role"], b["png"],
                                                                    b["data"], every, sub)})
                    return self.send(200, {"lines": HUD.remove(cfg["disc"], mods_dir, mod_arg, base, b["role"], b["png"], every)})
                name = b["name"]
                if self.path == "/api/reload" and not game_running():
                    return self.send(400, {"error": "no game opened by the editor is running: use Test in game"})
                # a second Test would open another game and lose the first one: the page asks (reload or restart)
                if self.path == "/api/test" and game_running() and not b.get("restart"):
                    return self.send(409, {"error": "the game is already open", "code": "running", "stage": GAME["stage"]})
                if self.path == "/api/test" and not b.get("save", True):
                    return self.send(200, {"ok": True, "lines": [], "launched": launch_game(cfg, name, b.get("training", True))})
                res = check_patch(cfg["disc"], base_of(cfg, name), b["patch"], mod_folder(cfg, mod), points_root(cfg, name))
                res["saved"] = save_patch(cfg, mod, name, b["patch"])
                # another mod with this stage's file that loads later wins in the game: the page says so (not a check problem)
                res["winner"] = patch_winner(cfg, mod, name)
                res["have"] = [m for m in list_mods(cfg) if os.path.isfile(patch_path(cfg, m, name))]
                if self.path == "/api/test":
                    res["launched"] = launch_game(cfg, name, b.get("training", True))
                if self.path == "/api/reload":
                    reload_game(name)
                    res["reloading"] = True
                return self.send(200, res)
            return self.send(404, {"error": "not found"})
        except SettingsError as e:
            return self.send(400, {"error": str(e), "code": "settings"})
        except Exception as e:      # noqa: BLE001
            traceback.print_exc()
            return self.send(500, {"error": "%s: %s" % (type(e).__name__, e)})


def open_window(url, window):
    """The default browser, or with window its own window. True when that window ran here and is now closed."""
    if window:
        try:
            import webview      # noqa: PLC0415 - optional
            BROWSE["window"] = webview.create_window("WotM Level Editor", url, width=1600, height=950)
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
