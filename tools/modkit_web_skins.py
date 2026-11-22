"""Skins in the mod kit page (tools/modkit_web.py), as the window kit makes them: New skin..., Turn into skin..., the skin's
properties (name, base costume, CPUs), duplicate and delete; the costumes' portraits for the base buttons; and the mesh row
that skins and custom monsters share (Export to Blender, Check model..., Import mesh..., Import animations...)."""
import json
import os
import tempfile
import threading

import modkit
import modkit_web_files as wf

_portrait_lock = threading.Lock()


class SkinsMixin:
    """The Kit's part for skins (needs disc, mods, mod, work(), run_fn(), say())."""

    # ---- portraits of the disc's costumes (the base buttons)
    def portrait(self, prefix, k):
        """Path of the PNG portrait of costume k of prefix (made once per session), or None."""
        with _portrait_lock:   # the page asks for the four at once; the disc reader is one at a time
            return self._portrait(prefix, k)

    def _portrait(self, prefix, k):
        if not hasattr(self, "_portraits"):
            self._portrait_dir = tempfile.mkdtemp(prefix="wotm_portraits_")
            self._portraits = {}
        key = (self.disc, prefix, k)
        if key not in self._portraits:
            p = os.path.join(self._portrait_dir, "%s%d.png" % (prefix, k))
            try:
                ok = modkit.portrait_png(self.disc, prefix, k, p)
            except Exception:  # noqa: BLE001
                ok = False
            self._portraits[key] = p if ok and os.path.exists(p) else None
        return self._portraits[key]

    def costumes_of(self, prefix):
        """The costumes (0..3) the disc has for prefix."""
        return [k for k in range(4) if prefix and modkit.disc_has(self.disc, "MON/%s%d.TEX" % (prefix, k))]

    # ---- the monsters a skin can be for
    def monster_choices(self):
        names = {p: n for p, n, _ in wf.MONSTERS}
        customs = modkit.find_customs(self.mods)
        out = []
        for key, prefix, co in modkit.list_monsters(self.mods):
            if co is None:
                out.append({"label": names.get(prefix, key.capitalize()), "key": key, "prefix": prefix, "costume": 0, "game": True})
            else:
                out.append({"label": "%s  (custom, %s)" % (key, customs[key]["mod"]), "key": key, "prefix": prefix, "costume": co,
                            "game": False})
        return out

    def resolve_monster(self, text, choices):
        """{"key", "prefix", "costume", "kind": game|custom|unknown} for the Monster field, or None."""
        t = (text or "").strip()
        for c in choices:
            if t == c["label"]:
                return dict(c, kind="game" if c["game"] else "custom")
        g = modkit.game_monster(t) if t else None
        if g:
            return {"key": g, "prefix": modkit.MONSTERS[g], "costume": 0, "kind": "game"}
        k = t.lower()
        for c in choices:
            if not c["game"] and c["key"] == k:
                return dict(c, kind="custom")
        if modkit.CUSTOM_ID_RE.fullmatch(k):
            return {"key": k, "prefix": None, "costume": None, "kind": "unknown"}
        return None

    def skin_exists(self, sid):
        return bool(self.mod) and (os.path.exists(modkit.skin_dir(self.mods, self.mod, sid)) or
                                   os.path.exists(os.path.join(self.work(), "skins", sid)))

    def act_skin_form(self, d):
        """The New skin dialog as it is typed: {"choices" (first call), "res", "hint", "costumes", "error", "slug", "free_id"}."""
        choices = self.monster_choices()
        r = {"choices": choices}
        if not d.get("sid"):
            r["free_id"] = next((s for s in ("skin%d" % i for i in range(1, 1000)) if not self.skin_exists(s)), "skin")
        res = self.resolve_monster(d.get("monster", ""), choices)
        r["res"] = res
        r["hint"] = ("" if res is None else "unknown monster: taken as a custom monster id; no textures will be extracted"
                     if res["kind"] == "unknown" else "custom monster on %s; default costume %d (monster.json)" % (res["prefix"], res["costume"])
                     if res["kind"] == "custom" else "")
        r["costumes"] = self.costumes_of(res["prefix"]) if res and res.get("prefix") else []
        if "name" in d:
            r["slug"] = modkit.skin_slug(d.get("name") or "")
        sid = d.get("sid", "")
        e = modkit.skin_id_error(sid) if sid else "the skin needs an id"
        if res is None:
            r["error"] = "Monster: a playable monster or the id of a custom monster (lowercase letters, digits and _)"
        elif e:
            r["error"] = "Id: " + e
        elif self.skin_exists(sid):
            r["error"] = "Id: the mod already has a skin %r" % sid
        elif not (d.get("name") or "").strip():
            r["error"] = "Name: the selection shows it; it cannot be empty"
        return r

    def act_skin_new(self, d):
        """{"monster", "sid", "base", "name", "ai", "from_group"?}: the skin made (from_group: Turn into skin, the PNGs of an
        extracted costume move into it)."""
        f = self.act_skin_form(d)
        if f.get("error"):
            return {"error": f["error"]}
        res = f["res"]
        sid, name, ai = d["sid"], d["name"].strip().upper(), bool(d.get("ai"))
        b = None if res["kind"] == "unknown" else d.get("base")
        if res["kind"] == "custom" and b == res["costume"]:
            b = None   # no base in skin.json: the monster.json's costume
        work, mods, mod, disc = self.work(), self.mods, self.mod, self.disc
        grp = d.get("from_group")
        if grp:
            dd, stem = grp.rsplit("/", 1)

            def fn():
                modkit.skin_new(disc, work, mods, mod, res["key"], sid, b, name, ai, textures=False)
                modkit.skin_adopt(work, dd, stem, sid)
            return {"ok": self.run_fn("Turning %s into skin %s..." % (grp, sid), fn), "focus": ["skins/" + sid]}
        return {"ok": self.run_fn("Creating skin %s (%s)..." % (sid, res["key"]),
                                  lambda: modkit.skin_new(disc, work, mods, mod, res["key"], sid, b, name, ai)),
                "focus": ["skins/" + sid]}

    def act_packed(self, d):
        """After Turn into skin: the files the mod already has of that costume (they keep replacing the original one).
        {"group", "delete"?} -> {"files"} (or deletes them)."""
        dd, stem = d["group"].rsplit("/", 1)
        folder = os.path.join(self.mods, self.mod, *dd.split("/"))
        hits = []
        for n in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
            m = modkit.PNG_RE.match(n)
            if (m and m.group(1).lower() == stem.lower()) or n.upper() in (stem.upper() + ".TEX", stem.upper() + ".RTX"):
                hits.append(os.path.join(folder, n))
        if not d.get("delete"):
            if hits:
                self.say("WARNING: %s/%s still has %d file(s) of %s built before (%s); they keep replacing the original costume in "
                         "the game.\n" % (self.mod, dd, len(hits), stem.upper(), ", ".join(os.path.basename(p) for p in hits[:6]) +
                                          (", ..." if len(hits) > 6 else "")), "warn")
            return {"files": [os.path.basename(p) for p in hits], "where": "%s/%s" % (self.mod, dd), "stem": stem.upper()}
        n = 0
        for p in hits:
            try:
                os.remove(p)
                n += 1
            except OSError as ex:
                self.say("%s\n" % ex, "err")
        self.say("Deleted %d file(s) of %s from %s/%s\n" % (n, stem.upper(), self.mod, dd))
        return {"ok": True}

    def act_skin_props(self, d):
        """The skin's panel: {"sid", "data", "kind", "prefix", "costumes", "custom_costume", "mesh"}."""
        sid = d["sid"]
        data = self.skin_data(sid)
        if not data:
            return {"sid": sid, "missing": modkit.skin_dir(self.mods, self.mod, sid)}
        mkey = str(data.get("monster", "")).lower()
        game = modkit.game_monster(mkey)
        custom = None if game else modkit.find_customs(self.mods).get(mkey)
        prefix = modkit.MONSTERS[game] if game else (custom["prefix"] if custom else None)
        kind = "playable" if game else ("custom, %s" % custom["mod"] if custom else "custom monster the kit does not know")
        return {"sid": sid, "data": data, "kind": kind, "game": bool(game), "prefix": prefix,
                "costumes": self.costumes_of(prefix) if prefix else [0, 1, 2, 3],
                "custom_costume": custom["costume"] if custom else None,
                "mesh": self.mesh_status("skin", sid, os.path.join(self.work(), "skins", sid))}

    def act_skin_save(self, d):
        sid = d["sid"]
        data = self.skin_data(sid)
        if not self.mod or not data:
            return {"error": "No skin.json for %s." % sid}
        name = (d.get("name") or "").strip()
        if not name:
            return {"error": "The name cannot be empty (the selection shows it)."}
        data["name"] = name.upper()
        base = d.get("base")
        if base is None or base < 0:
            data.pop("base", None)
        else:
            data["base"] = int(base)
        data["ai"] = bool(d.get("ai"))
        try:
            modkit.write_skin_json(self.mods, self.mod, sid, data)
        except OSError as ex:
            return {"error": "Could not save the skin: %s" % ex}
        self.say("Skin %s saved: %s\n" % (sid, json.dumps(data)))
        return {"ok": True}

    def act_skin_duplicate(self, d):
        sid, new = d["sid"], (d.get("new") or "").strip()
        e = modkit.skin_id_error(new) or ("the mod already has a skin %r" % new if self.skin_exists(new) else None)
        if e:
            return {"error": "\"%s\": %s." % (new, e)}
        work, mods, mod = self.work(), self.mods, self.mod
        return {"ok": self.run_fn("Duplicating skin %s as %s..." % (sid, new), lambda: modkit.skin_duplicate(mods, mod, work, sid, new)),
                "focus": ["skins/" + new]}

    def act_skin_delete(self, d):
        sid = d["sid"]
        work, mods, mod = self.work(), self.mods, self.mod
        return {"ok": self.run_fn("Deleting skin %s..." % sid, lambda: modkit.skin_delete(mods, mod, work, sid))}

    # ---- the mesh row (skins and custom monsters)
    def mesh_status(self, kind, tid, folder):
        names = sorted(os.listdir(folder)) if os.path.isdir(folder) else []
        model = [n for n in names if os.path.splitext(n)[1].upper() in (".NGP", ".PTR")]
        glb = [n for n in names if n.lower().endswith(".glb")]
        txt = ("own model: %s" % ", ".join(model)) if model else "the base costume's"
        rec = modkit._json_obj(os.path.join(folder, modkit.BLENDER_REC)) if model else {}
        if rec.get("rig"):
            txt += "; own skeleton: %s" % os.path.basename(rec["rig"])
        if rec.get("anim_ids"):
            txt += "; animations changed: %s" % ", ".join(rec["anim_ids"])
        if glb:
            txt += "; for Blender: %s" % ", ".join(glb)
        out = {"text": txt}
        if kind == "monster" and self.mod:
            r = modkit.rigged_record(self.work(), self.mods, self.mod, tid)
            out["rigged"] = os.path.basename(r["model"]) if r else None
        return out

    def act_mesh_glb(self, d):
        """Where the mesh export writes the .glb (the dialogs start there); for "rig", the last rig's."""
        kind, tid = d["kind"], d["tid"]
        glb = modkit.mesh_glb(self.work(), self.mods, self.mod, kind, tid)
        if d.get("rig") or d.get("check"):
            rel = modkit.mesh_target(self.mods, self.mod, kind, tid)[0]
            rig = (modkit._json_obj(modkit._blender_rec(self.work(), rel)) or {}).get("rig")
            if rig and (d.get("rig") or d.get("check")):
                glb = rig
        return {"path": glb}

    def act_mesh_export(self, d):
        kind, tid = d["kind"], d["tid"]
        disc, work, mods, mod = self.disc, self.work(), self.mods, self.mod
        return {"ok": self.run_fn("Exporting %s for Blender (mesh and animations)..." % tid,
                                  lambda: modkit.mesh_export(disc, work, mods, mod, kind, tid))}

    def act_mesh_check(self, d):
        kind, tid, p = d["kind"], d["tid"], d["path"]
        disc, work, mods, mod = self.disc, self.work(), self.mods, self.mod
        return {"ok": self.run_fn("Checking %s for %s..." % (os.path.basename(p), tid),
                                  lambda: modkit.mesh_check(disc, work, mods, mod, kind, tid, p))}

    def act_mesh_import(self, d):
        """{"kind", "tid", "path", "what": mesh|anims, "as_rig"?}: a mesh that is a new body answers {"route": "rig", "why"}
        (a skin cannot take it; a custom can, with Import rig: as_rig)."""
        kind, tid, p, what = d["kind"], d["tid"], d["path"], d.get("what", "mesh")
        disc, work, mods, mod = self.disc, self.work(), self.mods, self.mod
        if what == "mesh" and not d.get("as_rig"):
            try:
                way, why = modkit.mesh_route(disc, mods, mod, kind, tid, p)
            except SystemExit:
                way, why = "mesh", None
            if way == "rig":
                return {"route": "rig", "why": why}
        if d.get("as_rig"):
            return {"ok": self.run_fn("Importing the rig of %s..." % tid, lambda: modkit.rig_import(disc, work, mods, mod, tid, p))}
        fn = modkit.mesh_import if what == "mesh" else modkit.anim_import
        noun = "mesh" if what == "mesh" else "animations"
        return {"ok": self.run_fn("Importing the %s of %s..." % (noun, tid), lambda: fn(disc, work, mods, mod, kind, tid, p))}
