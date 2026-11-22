"""The mod kit page's file lists (tools/modkit_web.py): the game disc by name, and the mod's work folder in groups (the disc
file each item came from, each skin, each custom monster), with what the page does on them (extract, open, replace, save,
revert, remove). The names and groups are the window kit's (tools/modkit_gui.py)."""
import os
import shutil
import subprocess
import sys
import wave

import modkit

# Monsters: file prefix (MON/<PREFIX><costume>) -> name in the game, sound banks (GLOSSARY.md)
MONSTERS = [
    ("MONKEY", "Congar", ("APE_S", "APE_LS")), ("ROBOT", "Robo-47", ("ROBOT_S", "ROBOT_LS")),
    ("LIZARD", "Togera", ("LIZARD_S", "LIZRD_LS")), ("SHOGUN", "Ultra V", ("SHOGUN_S", "SHOGN_LS")),
    ("MANTIS", "Preytor", ("MANTIS_S", "MANTS_LS")), ("ENERGY", "Kineticlops", ("ENERGY_S", "ENERG_LS")),
    ("LAVA", "Magmo", ("LAVA_S", "LAVA_LS")), ("DRAGON", "Raptros", ("DRAGON_S", "DRAGN_LS")),
    ("ROCK", "Agamo", ("ROKMAN_S", "ROKMN_LS")), ("ALIEN", "Zorgulon", ("ALIEN_S", "ALIEN_LS")),
]
BOSSES = [("ASSBOSS", "Goliath Prime", ("ASSEMB_S",)), ("JELLY", "Jelly (final boss)", ()), ("FINAL", "Final boss", ("FINAL_S",))]
LEVELS = [("CENTRAL", "Midtown Park"), ("VEGAS", "Gamblers Gulch"), ("CANYON2", "Rosdale Canyon"), ("METRO", "Metro City"),
          ("AIRPORT", "Century Airfield"), ("3-MILE", "Atomic Island"), ("SANFRAN", "Baytown"), ("ISLAND", "Club Caldera"),
          ("TOKYO", "Tsunopolis"), ("UFO", "UFO"), ("CAPITOL", "Capitol"), ("TRAINING", "Training"), ("CANYON1", "Lake Powell"),
          ("MINISF", "Mini Baytown"), ("VOLCANO", "Volcano"), ("TOKYODM", "Tokyo Deathmatch"), ("DBALL", "Dodge Ball"),
          ("BIGSHOT", "Big Shot"), ("CRUSH", "Crush")]
SHELL = [("SHELL", "Main menu"), ("SHELL2", "Menu (2)"), ("UI", "Interface"), ("LOAD", "Loading screen"),
         ("PRESHELL", "Before the menu")]
IMG_EXT = (".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".tif", ".tiff", ".webp", ".dds")


def group_of(rel):
    """The group of a work folder file: the disc file it came from ("MON/MONKEY0": .TEX and .RTX together;
    "SND/SHELL_S.BNK"), or the skin / custom monster ("skins/<id>", "monsters/<id>")."""
    parts = rel.split("/")
    if len(parts) >= 3 and parts[0] in ("skins", "monsters"):
        return parts[0] + "/" + parts[1]
    d, n = os.path.split(rel)
    pre = d + "/" if d else ""
    m = modkit.PNG_RE.match(n)
    if m:
        return pre + m.group(1).upper()
    mb = modkit.BNK_RE.match(n)
    if mb:
        return pre + mb.group(1).upper() + ".BNK"
    return pre + os.path.splitext(n)[0].upper()


def group_of_pat(pat):
    """The group an extract pattern ("MON/LIZARD0", "SND/APE_S.BNK") fills."""
    pat = pat.replace("\\", "/")
    if pat.startswith(("skins/", "monsters/")):
        return "/".join(pat.split("/")[:2])
    stem, ext = os.path.splitext(pat)
    d, n = os.path.split(stem)
    return (d + "/" if d else "") + n.upper() + (".BNK" if ext.upper() == ".BNK" else "")


def costume_of_group(key, bosses=False):
    """(game name, prefix, costume) of a playable monster's costume group ("MON/SHOGUN3" -> ("ultra-v", "SHOGUN", 3)); None
    otherwise. bosses: also the bosses a custom takes as its base."""
    d, _, n = key.rpartition("/")
    if d.upper() != "MON" or not n[-1:].isdigit() or int(n[-1]) > 3:
        return None
    names = dict(modkit.MONSTERS, **(modkit.CUSTOM_BASES if bosses else {}))
    game = next((g for g, p in names.items() if p == n[:-1].upper()), None)
    return (game, n[:-1].upper(), int(n[-1])) if game else None


def file_info(p):
    """"256x128", "1.4 s" or "12 KB"."""
    n = p.lower()
    try:
        if n.endswith(".png"):
            s = modkit.png_size(p)
            if s:
                return "%dx%d" % s
        if n.endswith(".wav"):
            with wave.open(p) as w:
                return "%.1f s" % (w.getnframes() / float(w.getframerate()))
    except Exception:  # noqa: BLE001
        pass
    return "%d KB" % max(1, os.path.getsize(p) // 1024)


def file_note(rel):
    """What a work folder file is, for the preview."""
    n = os.path.basename(rel)
    m = modkit.PNG_RE.match(n)
    if m:
        return "Texture %d of %s%s. Save it at any resolution, keeping the aspect ratio." % (
            int(m.group(3)), m.group(1).upper(), ".RTX" if m.group(2) else ".TEX")
    mb = modkit.BNK_RE.match(n)
    if mb:
        return "Sound %d of bank %s.BNK%s" % (int(mb.group(2)), mb.group(1).upper(),
                                              ", sample %s only" % mb.group(3) if mb.group(3) is not None else "")
    if n.lower().endswith((".wav",)):
        return "Music or voice (.VPK/.VAG)"
    return ""


def start_program(exe, p):
    subprocess.Popen([exe, os.path.normpath(p)])


def open_file(p):
    if hasattr(os, "startfile"):
        os.startfile(os.path.normpath(p))
    else:
        subprocess.Popen(["xdg-open", p], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def reveal(p):
    """The file manager with the file selected (Windows), or its folder."""
    if sys.platform == "win32":
        subprocess.Popen(["explorer", "/select,", os.path.normpath(p)])
    else:
        open_file(os.path.dirname(p))


class FilesMixin:
    """The Kit's part for the two lists (needs disc, mods, mod, work(), run(), say())."""

    # ---- the disc
    def act_disc(self, d):
        """The disc by name: [{"label", "pats", "kids"}] groups (Monsters, Bosses, Levels...), or {"error"}."""
        disc = self.disc
        if not modkit.disc_ok(disc):
            return {"error": "Game disc not found: pick your War of the Monsters .iso above."}

        def has(rel):
            return modkit.disc_has(disc, rel)

        def node(label, items):
            return {"label": label, "pats": [p for it in items for p in it["pats"]], "kids": items}
        out = []
        g = []
        for prefix, name, banks in MONSTERS:
            items = [{"label": "Costume %d  (%s%d)" % (k + 1, prefix, k), "pats": ["MON/%s%d" % (prefix, k)]}
                     for k in range(4) if has("MON/%s%d.TEX" % (prefix, k))]
            items += [{"label": "Sounds  (%s.BNK)" % b, "pats": ["SND/%s.BNK" % b]} for b in banks if has("SND/%s.BNK" % b)]
            if items:
                g.append(node(name, items))
        out.append({"label": "Monsters", "kids": g})
        g = []
        for prefix, name, banks in BOSSES:
            items = [{"label": "Form %d  (%s%d)" % (k + 1, prefix, k), "pats": ["MON/%s%d" % (prefix, k)]}
                     for k in range(4) if has("MON/%s%d.TEX" % (prefix, k))]
            items += [{"label": "Sounds  (%s.BNK)" % b, "pats": ["SND/%s.BNK" % b]} for b in banks if has("SND/%s.BNK" % b)]
            if items:
                g.append(node(name, items))
        out.append({"label": "Bosses", "kids": g})
        out.append({"label": "Levels", "kids": [{"label": "%s  (%s)" % (name, f), "pats": ["LVL/%s" % f]}
                                                for f, name in LEVELS if has("LVL/%s.TEX" % f)]})
        out.append({"label": "Menus and screens", "kids": [{"label": "%s  (%s)" % (name, f), "pats": ["SHELL/%s" % f]}
                                                           for f, name in SHELL if has("SHELL/%s.TEX" % f)]})
        used = {b for _, _, bs in MONSTERS + BOSSES for b in bs}
        out.append({"label": "Sound banks", "kids": [{"label": n.upper(), "pats": ["SND/" + n]} for n in sorted(modkit.disc_listdir(disc, "SND"))
                                                     if n.upper().endswith(".BNK") and os.path.splitext(n)[0].upper() not in used]})
        out.append({"label": "Music and voices", "kids": [{"label": n.upper(), "pats": ["VAG/" + n]} for n in sorted(modkit.disc_listdir(disc, "VAG"))
                                                          if n.upper().endswith((".VPK", ".VAG"))]})
        return {"tree": [g for g in out if g["kids"]]}

    def act_extract(self, d):
        work = self.work()
        if not work:
            return {"error": "Pick or create a mod first (\"Mod\" at the top): the files go into that mod's work folder."}
        pats = list(dict.fromkeys(d.get("pats") or []))
        if not pats:
            return {"error": "Pick a monster, costume, level or sound in the list on the left."}
        return {"ok": self.run("Extracting %d file(s) into %s..." % (len(pats), self.mod),
                               ["export", self.mod] + pats + ["--work", work] + self.paths()),
                "focus": sorted({group_of_pat(p) for p in pats})}

    # ---- the work folder
    def skin_data(self, sid):
        return modkit._json_obj(os.path.join(modkit.skin_dir(self.mods, self.mod, sid), "skin.json")) if self.mod else {}

    def monster_data(self, cid):
        return modkit._json_obj(os.path.join(modkit.monster_dir(self.mods, self.mod, cid), "monster.json")) if self.mod else {}

    def group_label(self, key, customs):
        if key.startswith("skins/"):
            sid = key[6:]
            data = self.skin_data(sid)
            if not data:
                return "Skin: %s  (no skin.json in the mod)" % sid
            b = data.get("base")
            if isinstance(b, int):
                base = "base %d" % b
            else:
                c = customs.get(str(data.get("monster", "")).lower())
                base = "base %d from monster.json" % c["costume"] if c else "no base"
            return "Skin: %s  (%s/%s, %s)" % (data.get("name") or sid, data.get("monster", "?"), data.get("id", sid), base)
        if key.startswith("monsters/"):
            cid = key[9:]
            data = self.monster_data(cid)
            if not data:
                return "Monster: %s  (no monster.json in the mod)" % cid
            return "Monster: %s  (custom %s, base %s costume %s%s)" % (data.get("name") or cid, data.get("id", cid), data.get("base", "?"),
                                                                       data.get("costume", 0), ", own look" if data.get("files") else "")
        n = key.split("/")[-1]
        for prefix, name, banks in MONSTERS + BOSSES:
            if n[:-1] == prefix and n[-1:].isdigit():
                return "%s  (%s, %s %d)" % (key, name, "costume" if (prefix, name, banks) in MONSTERS else "form", int(n[-1]) + 1)
            if n.endswith(".BNK") and n[:-4] in banks:
                return "%s  (%s sounds)" % (key, name)
        for f, name in LEVELS + SHELL:
            if n == f:
                return "%s  (%s)" % (key, name)
        return key

    def act_work(self, d):
        """The work folder in groups: {"groups": [{"key", "label", "kind", "costume", "files": [{"rel", "name", "info",
        "state", "mtime"}]}], "total", "edited"}. edited_only: only the edited files."""
        work = self.work()
        if not work:
            return {"groups": [], "total": 0, "edited": 0, "status": "no mod picked"}
        if not os.path.isdir(work):
            return {"groups": [], "total": 0, "edited": 0, "status": "work folder is empty"}
        state = modkit.load_state(work)["files"]
        customs = modkit.find_customs(self.mods)
        groups = {"skins/" + sid: [] for sid in modkit.list_skins(self.mods, self.mod)}
        groups.update({"monsters/" + cid: [] for cid in modkit.list_mod_monsters(self.mods, self.mod)})
        total = edited_total = 0
        for root, ds, files in os.walk(work):
            ds[:] = sorted(x for x in ds if not x.startswith("."))
            for n in sorted(files):
                if n.endswith(".json") or n.startswith("."):
                    continue
                p = os.path.join(root, n)
                rel = os.path.relpath(p, work).replace("\\", "/")
                src = n.lower().endswith(modkit.MESH_SRC)   # the Blender .glb/.blend: stays in the work folder
                edited = not src and state.get(rel, {}).get("sha1") != modkit.sha1(p)
                total += 1
                edited_total += edited
                if d.get("edited_only") and not edited:
                    continue
                groups.setdefault(group_of(rel), []).append({
                    "rel": rel, "name": n, "info": file_info(p), "mtime": int(os.path.getmtime(p)),
                    "state": "for Blender" if src else "edited" if edited else "original"})
        out = []
        for key in sorted(groups, key=lambda k: (not k.startswith("monsters/"), not k.startswith("skins/"), k)):
            kind = "monster" if key.startswith("monsters/") else "skin" if key.startswith("skins/") else "file"
            out.append({"key": key, "label": self.group_label(key, customs), "kind": kind,
                        "costume": bool(costume_of_group(key)), "custom_base": bool(costume_of_group(key, bosses=True)),
                        "files": groups[key]})
        return {"groups": out, "total": total, "edited": edited_total, "status": "%d file(s), %d edited" % (total, edited_total)}

    def work_path(self, rel):
        """A work folder file from its relative path, or SystemExit when it is outside the folder."""
        work = self.work()
        if not work:
            raise SystemExit("Pick or create a mod first (\"Mod\" at the top).")
        p = os.path.normpath(os.path.join(work, *rel.replace("\\", "/").split("/")))
        if not p.startswith(os.path.normpath(work) + os.sep):
            raise SystemExit("%s is outside the work folder" % rel)
        return p

    def act_note(self, d):
        """The preview's caption for a file."""
        p = self.work_path(d.get("rel", ""))
        return {"info": file_info(p) if os.path.exists(p) else "", "note": file_note(d.get("rel", ""))}

    def act_open_files(self, d):
        """Opens files in the program set for their type in "Editors..." (or the system's)."""
        eds = self.editors()
        for rel in (d.get("rels") or [])[:8]:
            p = self.work_path(rel)
            exe = d.get("exe") or eds.get(os.path.splitext(p)[1].lower())
            try:
                start_program(exe, p) if exe else open_file(p)
            except OSError as ex:
                self.say("Could not open %s: %s\n" % (os.path.basename(p), ex), "err")
        if d.get("exe") and d.get("always") and d.get("rels"):
            eds[os.path.splitext(d["rels"][0])[1].lower()] = d["exe"]
            self.set_editors(eds)
        return {"ok": True}

    def editors(self):
        return dict(self.conf_get().get("editors") or {})

    def set_editors(self, eds):
        self.conf_save({"editors": eds})

    def act_editors(self, d):
        """{"set": {".png": exe or ""}} changes them; -> {"editors"}."""
        eds = self.editors()
        for ext, exe in (d.get("set") or {}).items():
            exe = (exe or "").strip().strip('"')
            if exe:
                eds[ext] = exe
            else:
                eds.pop(ext, None)
        if "set" in d:
            self.set_editors(eds)
        return {"editors": eds}

    def act_reveal(self, d):
        rels = d.get("rels") or []
        if rels:
            reveal(self.work_path(rels[0]))
        elif self.work():
            self.open_path(self.work())
        return {"ok": True}

    def act_replace(self, d):
        """{"rel", "src"}: a work folder file replaced by another one under its own name (another image format becomes PNG)."""
        dst = self.work_path(d.get("rel", ""))
        src = os.path.normpath(d.get("src") or "")
        if not os.path.isfile(src) or os.path.normcase(src) == os.path.normcase(dst):
            return {"error": "Pick another file."}
        self.conf_save({"replace_dir": os.path.dirname(src)})
        ext, sext = os.path.splitext(dst)[1].lower(), os.path.splitext(src)[1].lower()
        work = self.work()
        st = modkit.load_state(work)["files"].get(os.path.relpath(dst, work).replace("\\", "/")) or {}
        orig = tuple(st["size"]) if st.get("size") else (modkit.png_size(dst) if st.get("sha1") and os.path.exists(dst)
                                                         and modkit.sha1(dst) == st["sha1"] else None)
        try:
            if ext == ".png" and sext != ".png":
                try:
                    from PIL import Image
                except ImportError:
                    raise OSError("converting %s to PNG needs Pillow (pip install pillow)" % sext)
                with Image.open(src) as im:
                    im.convert("RGBA").save(dst, "PNG")
            elif ext == ".wav" and sext != ".wav":
                raise OSError("pick a .wav file (the kit converts WAV to the game's format when packing)")
            elif ext not in (".png", ".wav") and sext != ext:
                raise OSError("pick a %s file" % ext)
            else:
                if ext == ".wav":
                    with wave.open(src):   # a WAV the pack can read
                        pass
                shutil.copyfile(src, dst)
        except Exception as ex:  # noqa: BLE001
            return {"error": "Could not replace %s: %s" % (os.path.basename(dst), ex)}
        msg = "Replaced %s with %s" % (os.path.basename(dst), os.path.basename(src))
        new = modkit.png_size(dst) if ext == ".png" else None
        if new:
            msg += " (%dx%d)" % new
        self.say(msg + "\n")
        if new and orig and new[0] * orig[1] != new[1] * orig[0]:
            self.say("WARNING: the game texture is %dx%d; a different aspect ratio looks stretched in the game\n" % orig, "warn")
        return {"ok": True}

    def act_save_to(self, d):
        """{"rels", "dir", "overwrite"}: copies of the files in a folder; without overwrite, -> {"exist": n} when some are there."""
        files = [self.work_path(r) for r in d.get("rels") or []]
        dst = os.path.normpath(d.get("dir") or "")
        if not files or not os.path.isdir(dst):
            return {"error": "Pick a folder."}
        self.conf_save({"save_dir": dst})
        exist = [p for p in files if os.path.exists(os.path.join(dst, os.path.basename(p)))]
        if exist and d.get("overwrite") is None:
            return {"exist": len(exist)}
        if exist and not d.get("overwrite"):
            files = [p for p in files if p not in exist]
        n = 0
        for p in files:
            try:
                shutil.copy2(p, os.path.join(dst, os.path.basename(p)))
                n += 1
            except OSError as ex:
                self.say("%s\n" % ex, "err")
        self.say("Saved %d file(s) to %s\n" % (n, dst))
        return {"ok": True}

    def edited_of(self, rels):
        work = self.work()
        st = modkit.load_state(work)["files"]
        out = []
        for rel in rels:
            p = self.work_path(rel)
            if p.lower().endswith(modkit.MESH_SRC):
                continue
            old = st.get(rel, {}).get("sha1")
            if not old or not os.path.exists(p) or modkit.sha1(p) != old:
                out.append(rel)
        return out

    def act_edited(self, d):
        return {"rels": self.edited_of(d.get("rels") or [])}

    def act_revert(self, d):
        rels = self.edited_of(d.get("rels") or [])
        if not rels:
            self.say("Nothing to revert: the selected files are as extracted.\n")
            return {"ok": True}
        return {"ok": self.run("Reverting %d file(s)..." % len(rels), ["revert", self.mod] + rels + ["--work", self.work()] + self.paths())}

    def act_remove(self, d):
        rels = d.get("rels") or []
        work = self.work()
        st = modkit.load_state(work)
        for rel in rels:
            p = self.work_path(rel)
            try:
                os.remove(p)
            except OSError as ex:
                self.say("%s\n" % ex, "err")
            st["files"].pop(rel, None)
        modkit.save_state(work, st)
        return {"ok": True}
