"""Custom monsters in the mod kit page (tools/modkit_web.py): New monster..., Turn into custom
monster..., the monster's panel (name, camera, narrator, rules, special, CPUs, HUD portrait, look: model files, stats,
sounds, mesh, rig, rigged model and Adjust bones), duplicate and delete."""
import json
import os
import tempfile

import modkit
import modkit_web_files as wf

# (label, monster.json base): the ten playable ones and the bosses the port lets play (modkit.CUSTOM_BASES)
BASES = [(n, next(g for g, p in modkit.MONSTERS.items() if p == pre)) for pre, n, _ in wf.MONSTERS] + [
    ("Goliath Prime (boss)", "assboss"), ("Jelly (final boss, 2nd form)", "jelly"), ("Cerebulon (final boss)", "final")]
BASE_HINTS = {
    "assboss": "Boss: no climbing animations (it holds the last pose); as a CPU it loads but does not show up (its AI needs the "
               "points of its own level).",
    "jelly": "Boss: no rage, does not grapple; its special is the shock (Triangle + Circle).",
    "final": "Boss: does not pick up the special item.",
}


def base_label(b):
    return next((t for t, x in BASES if x == b), b or "")


class MonstersMixin:
    """The Kit's part for custom monsters (needs disc, mods, mod, work(), run(), run_fn(), say(), costumes_of())."""

    def monster_exists(self, cid):
        return bool(self.mod) and (os.path.exists(modkit.monster_dir(self.mods, self.mod, cid)) or
                                   os.path.exists(os.path.join(self.work(), "monsters", cid)))

    @staticmethod
    def camera_value(d):
        """{"camera_from": base or "", "camera_scale"} -> False (the base's own), (base, scale) or None (scale not 10..1000)."""
        b = d.get("camera_from") or ""
        if not b:
            return False
        try:
            sc = int(d.get("camera_scale"))
        except (TypeError, ValueError):
            return None
        return (b, sc) if 10 <= sc <= 1000 and any(b == x for _t, x in BASES) else None

    def act_monster_lists(self, d):
        """What the monster dialogs and panel choose from."""
        return {"bases": [{"label": t, "base": b, "prefix": modkit.base_prefix(b), "hint": BASE_HINTS.get(b, "Runs the code and the "
                           "AI of %s. To change only its look, a skin is enough." % t),
                           "defaults": modkit.BOSS_DEFAULTS.get(b, (0, None))} for t, b in BASES],
                "narrators": modkit.list_narrators(self.mods), "kinds": list(modkit.KINDS),
                "specials": [{"label": t, "base": b} for t, b in BASES if b not in modkit.SPECIAL_NEEDS or b in modkit.SPECIAL_PARTS],
                "free_name": "MONSTER %d" % next((i for i in range(1, 1000) if not self.monster_exists("monster_%d" % i)), 1)}

    def act_monster_form(self, d):
        """The New monster dialog as it is typed: {"costumes", "slug", "error", "note"}."""
        r = {}
        b = d.get("base")
        prefix = modkit.base_prefix(b) if b else None
        r["costumes"] = self.costumes_of(prefix) if prefix else []
        if "name" in d:
            s = modkit.skin_slug(d.get("name") or "")
            r["slug"] = ("m_" + s if s[:1].isdigit() else s)[:32]
        cid = d.get("cid", "")
        e = modkit.custom_id_error(cid)
        others = modkit.find_customs(self.mods)
        if not b:
            r["error"] = "Base type: pick one"
        elif e:
            r["error"] = "Id: " + e
        elif self.monster_exists(cid):
            r["error"] = "Id: the mod already has a monster %r" % cid
        elif not (d.get("name") or "").strip():
            r["error"] = "Name: the selection shows it; it cannot be empty"
        elif self.camera_value(d) is None:
            r["error"] = "Camera: the distance is 10 to 1000 %"
        if cid in others:
            r["note"] = "note: mod %s also has a monster %r; the last mod in alphabetical order wins" % (others[cid]["mod"], cid)
        return r

    def act_monster_new(self, d):
        """{"cid", "base", "costume", "name", "ai", "camera_from", "camera_scale", "narrator", "textures", "from_group"?}."""
        f = self.act_monster_form(d)
        if f.get("error"):
            return {"error": f["error"]}
        cid, base, co = d["cid"], d["base"], int(d.get("costume") or 0)
        name, ai, cam = d["name"].strip().upper(), bool(d.get("ai")), self.camera_value(d)
        narr = (d.get("narrator") or "").strip().lower() or None
        disc, work, mods, mod = self.disc, self.work(), self.mods, self.mod
        grp = d.get("from_group")
        if grp:
            dd, stem = grp.rsplit("/", 1)

            def fn():
                modkit.monster_new(disc, work, mods, mod, cid, base, co, name, ai, cam, narr, textures=False, files=True)
                modkit.skin_adopt(work, dd, stem, None, to="monsters/%s/files" % cid)
            return {"ok": self.run_fn("Turning %s into custom monster %s..." % (grp, cid), fn), "focus": ["monsters/" + cid]}
        tex = bool(d.get("textures", True))
        return {"ok": self.run_fn("Creating custom monster %s (base %s)..." % (cid, base),
                                  lambda: modkit.monster_new(disc, work, mods, mod, cid, base, co, name, ai, cam, narr, tex)),
                "focus": ["monsters/" + cid]}

    def act_monster_props(self, d):
        cid = d["cid"]
        data = self.monster_data(cid)
        mdir = modkit.monster_dir(self.mods, self.mod, cid)
        if not data:
            return {"cid": cid, "missing": mdir}
        bname = modkit.base_name(str(data.get("base", "")))
        cam = data.get("camera") if isinstance(data.get("camera"), dict) else {}
        cb = modkit.base_name(str(cam.get("from", ""))) if cam else None
        sb = modkit.base_name(str(data.get("special", ""))) if data.get("special") else None
        por = data.get("portrait") if isinstance(data.get("portrait"), str) else ""
        wfiles = os.path.join(self.work(), "monsters", cid, "files")
        names = sorted(os.listdir(wfiles)) if os.path.isdir(wfiles) else []
        npng = sum(1 for n in names if modkit.PNG_RE.match(n))
        model = [n for n in names if os.path.splitext(n)[1].upper() in (".NGP", ".PTR", ".TEX", ".RTX")]
        if not data.get("files"):
            look = "The base's disc costume (no files of its own)"
        else:
            look = "Its own files: %d texture(s) to edit (monsters/%s/files)" % (npng, cid) + (
                ", and its own 3D model (3D model tab)" if model else "")
        chr_ = [n for n in names if n.upper().endswith(".CHR")]
        aic = [n for n in names if n.upper().endswith(".AIC")]
        wavs = [n for n in names if modkit.BNK_RE.match(n)]
        st = modkit.load_state(self.work())["files"] if wavs else {}
        changed = sum(1 for n in wavs if st.get("monsters/%s/files/%s" % (cid, n), {}).get("sha1") != modkit.sha1(os.path.join(wfiles, n)))
        return {"cid": cid, "data": data,
                "base": ("%s, costume %s" % (base_label(bname) if bname else "%r (not accepted by the port)" % data.get("base"),
                                             data.get("costume", 0))),
                "camera_from": cb or "", "camera_scale": cam.get("scale", 100) if cam else 100,
                "narrator": data.get("narrator", "") if isinstance(data.get("narrator"), str) else "",
                "kind": data.get("kind") or "", "special": sb or "", "ai": bool(data.get("ai")),
                "portrait": por, "portrait_ok": bool(por) and os.path.isfile(os.path.join(mdir, por)),
                "look": look, "stats": ", ".join(chr_ + (["%d .AIC" % len(aic)] if aic else [])) or "the base's",
                "sounds": ("%d sound(s) as WAV, %d replaced" % (len(wavs), changed)) if wavs else "the base's",
                "mesh": self.mesh_status("monster", cid, wfiles)}

    def act_monster_save(self, d):
        cid = d["cid"]
        data = self.monster_data(cid)
        if not self.mod or not data:
            return {"error": "No monster.json for %s." % cid}
        if not (d.get("name") or "").strip():
            return {"error": "The name cannot be empty (the selection shows it)."}
        cam = self.camera_value(d)
        if cam is None:
            return {"error": "Camera: the distance is 10 to 1000 %."}
        data["name"] = d["name"].strip().upper()
        if cam:
            data["camera"] = {"from": cam[0], "scale": cam[1]}
        else:
            data.pop("camera", None)
        sp = d.get("special") or None
        if sp == modkit.base_name(str(data.get("base", ""))):
            sp = None   # the type's own
        for k, v in (("narrator", (d.get("narrator") or "").strip().lower()), ("kind", d.get("kind") or ""), ("special", sp or "")):
            if v:
                data[k] = v
            else:
                data.pop(k, None)
        data["ai"] = bool(d.get("ai"))
        try:
            modkit.write_monster_json(self.mods, self.mod, cid, data)
        except OSError as ex:
            return {"error": "Could not save the monster: %s" % ex}
        self.say("Monster %s saved: %s\n" % (cid, json.dumps(data)))
        return {"ok": True}

    def portrait_of(self, cid):
        """GET /api/hud?cid=: the custom's HUD portrait file."""
        data = self.monster_data(cid)
        por = data.get("portrait") if isinstance(data.get("portrait"), str) else ""
        p = os.path.join(modkit.monster_dir(self.mods, self.mod, cid), por) if por else ""
        return p if p and os.path.isfile(p) else None

    def logo_of(self, cid):
        """GET /api/hud?cid=&logo=1: the custom's name logo ("logo" of monster.json, in its folder or at the mod's root)."""
        data = self.monster_data(cid)
        lg = data.get("logo") if isinstance(data.get("logo"), str) else ""
        if not lg:
            return None
        mdir = modkit.monster_dir(self.mods, self.mod, cid)
        for p in (os.path.join(mdir, lg), os.path.join(self.mods, self.mod, lg)):
            if os.path.isfile(p):
                return p
        return None

    def act_portrait_set(self, d):
        cid, p = d["cid"], d["path"]
        src = p
        if not p.lower().endswith(".png"):   # another format becomes PNG
            try:
                from PIL import Image
                src = os.path.join(tempfile.mkdtemp(prefix="wotm_portrait_"), "portrait.png")
                Image.open(p).convert("RGBA").save(src)
            except Exception as ex:  # noqa: BLE001
                return {"error": "Could not open %s: %s" % (p, ex)}
        disc, work, mods, mod = self.disc, self.work(), self.mods, self.mod
        return {"ok": self.run_fn("Setting the HUD portrait of %s..." % cid,
                                  lambda: modkit.monster_portrait(disc, work, mods, mod, cid, src))}

    def act_portrait_remove(self, d):
        cid = d["cid"]
        mods, mod, work = self.mods, self.mod, self.work()
        data = self.monster_data(cid)
        por = data.pop("portrait", None)
        data.pop("portraitTex", None)
        if not mod or not por:
            return {"ok": True}
        modkit.write_monster_json(mods, mod, cid, data)
        for f in (os.path.join(modkit.monster_dir(mods, mod, cid), por), os.path.join(work, "monsters", cid, por)):
            try:
                os.remove(f)
            except OSError:
                pass
        st = modkit.load_state(work)
        st["files"].pop("monsters/%s/%s" % (cid, por), None)
        modkit.save_state(work, st)
        self.say("Monster %s: HUD portrait removed (back to the costume's)\n" % cid)
        return {"ok": True}

    def act_monster_model(self, d):
        cid, ps = d["cid"], d.get("paths") or []
        work, mods, mod = self.work(), self.mods, self.mod
        return {"ok": self.run_fn("Importing the model of %s..." % cid, lambda: modkit.monster_model(work, mods, mod, cid, list(ps)))}

    def act_monster_stats(self, d):
        cid = d["cid"]
        disc, work, mods, mod = self.disc, self.work(), self.mods, self.mod
        return {"ok": self.run_fn("Extracting the base's fight numbers and AI for %s..." % cid,
                                  lambda: modkit.monster_stats(disc, work, mods, mod, cid))}

    def act_monster_sounds(self, d):
        cid = d["cid"]
        disc, work, mods, mod = self.disc, self.work(), self.mods, self.mod
        return {"ok": self.run_fn("Extracting the base's sounds for %s..." % cid,
                                  lambda: modkit.monster_sounds(disc, work, mods, mod, cid))}

    # Import character with animations, Import rigged model and Adjust bones: a custom ({"cid"}) or a skin of one ({"kind": "skin",
    # "cid": the skin's id})
    def act_rig_import(self, d):
        cid, p, kind = d["cid"], d["path"], d.get("kind", "monster")
        disc, work, mods, mod = self.disc, self.work(), self.mods, self.mod
        return {"ok": self.run_fn("Importing the character of %s..." % cid,
                                  lambda: modkit.rig_import(disc, work, mods, mod, cid, p, kind=kind))}

    def act_rig_start(self, d):
        """Where the character import's dialog starts: the last one imported."""
        rel = "skins/" + d["cid"] if d.get("kind") == "skin" else "monsters/%s/files" % d["cid"]
        rec = modkit._json_obj(os.path.join(self.work(), *rel.split("/"), modkit.BLENDER_REC)) or {}
        return {"path": rec.get("rig") or ""}

    def act_rigged(self, d):
        """Import rigged model (path) or Adjust bones (adjust): modkit.py in a process of its own (the retarget and the rig import
        are pure Python); the bones page shows inside the kit's page ([page] line of --embed)."""
        cid, mod = d["cid"], self.mod
        kind = "skin" if d.get("kind") == "skin" else "monster"
        common = ["--work", self.work()] + self.paths() + ["--embed"]
        if d.get("adjust"):
            return {"ok": self.run("Adjusting the bones of %s..." % cid, [kind, "rigged", mod, cid, "--adjust"] + common)}
        p = d["path"]
        return {"ok": self.run("Importing the rigged model %s into %s..." % (os.path.basename(p), cid),
                               [kind, "rigged", mod, cid, p, "--page"] + common)}

    def act_monster_duplicate(self, d):
        cid, new = d["cid"], (d.get("new") or "").strip()
        e = modkit.custom_id_error(new) or ("the mod already has a monster %r" % new if self.monster_exists(new) else None)
        if e:
            return {"error": "\"%s\": %s." % (new, e)}
        work, mods, mod = self.work(), self.mods, self.mod
        return {"ok": self.run_fn("Duplicating monster %s as %s..." % (cid, new), lambda: modkit.monster_duplicate(mods, mod, work, cid, new)),
                "focus": ["monsters/" + new]}

    def act_monster_delete(self, d):
        cid = d["cid"]
        work, mods, mod = self.work(), self.mods, self.mod
        return {"ok": self.run_fn("Deleting monster %s..." % cid, lambda: modkit.monster_delete(mods, mod, work, cid)),
                "dir": modkit.monster_dir(mods, mod, cid)}
