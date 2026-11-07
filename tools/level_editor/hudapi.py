"""The level editor's HUD panel on the server side (web/hud.js): the stage's HUD textures (tools/lvlhud.py), their PNGs from the
disc and from the mod, and replacing or removing them in the mod. editor.py routes /api/hud, /api/hudimg, /api/hud/replace and
/api/hud/remove here.

Every stage's HUD (for "every stage with this picture") is read once per disc and kept in <cache>/hud-<disc key>.json."""
import base64
import json
import os
import threading

import lvlhud

CACHE_VERSION = 1


class HudApi:
    def __init__(self, cache_dir, root_of, stages_of, key_of):
        """root_of(disc, stage): folder with LVL/<STAGE>.*; stages_of(disc): stage names; key_of(disc): short key of the disc."""
        self.cache_dir, self.root_of, self.stages_of, self.key_of = cache_dir, root_of, stages_of, key_of
        self.lock = threading.Lock()
        self.infos = {}       # disc key -> {stage: lvlhud.stage_hud}

    def _all(self, disc):
        key = self.key_of(disc)
        with self.lock:
            if key in self.infos:
                return self.infos[key]
            path = os.path.join(self.cache_dir, "hud-%s.json" % key)
            infos = None
            try:
                with open(path, encoding="utf-8") as f:
                    j = json.load(f)
                if j.get("version") == CACHE_VERSION:
                    infos = j["stages"]
            except (OSError, ValueError, KeyError):
                pass
            if infos is None:
                infos = {}
                for st in self.stages_of(disc):
                    infos[st] = lvlhud.stage_hud(self.root_of(disc, st), st)
                os.makedirs(self.cache_dir, exist_ok=True)
                with open(path + ".tmp", "w", encoding="utf-8") as f:
                    json.dump({"version": CACHE_VERSION, "stages": infos}, f)
                os.replace(path + ".tmp", path)
            for info in infos.values():   # JSON keys: back to ints
                info["users"] = {int(k): v for k, v in info["users"].items()}
            self.infos[key] = infos
            return infos

    def _exported(self, disc, stage):
        out = os.path.join(self.cache_dir, "hud", self.key_of(disc), stage.upper())
        return lvlhud.export_stage(self.root_of(disc, stage), stage, out)

    def panel(self, disc, mods, mod, stage):
        stage = stage.upper()
        infos = self._all(disc)
        if stage not in infos:
            raise ValueError("no stage %s on the disc" % stage)
        p = lvlhud.panel(infos[stage], os.path.join(mods, mod) if mod else None, self._exported(disc, stage))
        for r in p["rows"]:
            r["every"] = len(lvlhud.targets(infos, stage, r["role"], True))
            r["disc"], r["mod"] = bool(r["disc"]), bool(r["mod"])   # the page asks /api/hudimg by name, never by path
        p["versions"] = {st: info["version"] for st, info in sorted(infos.items())}   # the version B note names the stages
        return p

    def image(self, disc, mods, mod, stage, src, png):
        """Path of a HUD PNG the panel listed: the disc's (exported) or the mod's. None for any other name."""
        stage = stage.upper()
        rows = lvlhud.panel(self._all(disc)[stage], os.path.join(mods, mod) if mod else None, self._exported(disc, stage))["rows"]
        for r in rows:
            if r["png"] == png:
                return r["disc"] if src == "disc" else r["mod"] if src == "mod" else None
        return None

    def _targets(self, disc, stage, role, png, every):
        stage = stage.upper()
        infos = self._all(disc)
        own = lvlhud.targets(infos, stage, role, False)
        if png not in [n for _s, n in own]:
            raise ValueError("%s is not the %s texture of %s" % (png, role, stage))
        return lvlhud.targets(infos, stage, role, every)

    def replace(self, disc, mods, mod, stage, role, png, data_b64, every):
        data = base64.b64decode(data_b64)
        if not data.startswith(b"\x89PNG\r\n\x1a\n"):
            raise ValueError("not a PNG")
        mod_dir = os.path.join(mods, mod)
        lines = []
        for st, name in self._targets(disc, stage, role, png, every):
            lvlhud.write_mod_png(mod_dir, name, data)
            lines.append("HUD: %s/LVL/%s (%s)" % (mod, name, st))
        if len(lines) > 3:
            lines = ["HUD: %s: %d stages (%s)" % (lvlhud.ROLE[role][1], len(lines), ", ".join(s for s, _n in
                     self._targets(disc, stage, role, png, every)))]
        return lines

    def remove(self, disc, mods, mod, stage, role, png, every):
        mod_dir = os.path.join(mods, mod)
        gone = []
        for _st, name in self._targets(disc, stage, role, png, every):
            gone += lvlhud.remove_mod_png(mod_dir, name)
        if not gone:
            return ["HUD: the mod had no %s" % png]
        return ["HUD: removed %d PNG(s) from %s" % (len(gone), mod)]
