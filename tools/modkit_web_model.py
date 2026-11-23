"""3D previews for the mod kit page (tools/modkit_web.py): a monster costume, a skin or a custom monster standing in its idle
(tools/pose.py, frame 0 of animation 90) as a .glb with the textures of the work folder over the disc's ones, so an edited
PNG shows on the model; and a 3D file of the work folder (.glb as it is; .dae, .gltf and .NGP made into a .glb).
Cached by the files' times: picking the same thing again costs nothing."""
import hashlib
import os
import tempfile
import threading

import modkit

_cache_dir = None
_lock = threading.Lock()


def cache_dir():
    global _cache_dir
    if _cache_dir is None:
        _cache_dir = tempfile.mkdtemp(prefix="wotm_kit3d_")
    return _cache_dir


def _overrides(folder, stem, deep=True):
    """{(is_rtx, texId): png path} of the PNGs in folder that replace stem's textures (MONKEY0 -> monkey0_t0004.png). deep: its
    subfolders too, as the game reads a skin's folder (skins/<id>/MON/monkey0_t0003.png); a file nearer the top wins."""
    out = {}
    if not folder or not os.path.isdir(folder):
        return out
    walk = sorted(os.walk(folder), key=lambda w: w[0].count(os.sep), reverse=True) if deep else [(folder, [], os.listdir(folder))]
    for d, _dirs, names in walk:   # deepest first, so the top folder's PNG is the one kept
        for n in names:
            m = modkit.PNG_RE.match(n)
            if m and m.group(1).lower() == stem.lower():
                out[(bool(m.group(2)), int(m.group(3)))] = os.path.join(d, n)
    return out


def _key(*paths, extra=""):
    h = hashlib.sha1(extra.encode("utf-8"))
    for p in paths:
        if p and os.path.exists(p):
            h.update(("%s|%d|%d;" % (p, os.path.getmtime(p) * 1000, os.path.getsize(p))).encode("utf-8"))
    return h.hexdigest()[:20]


def posed_glb(ngp, tex_path=None, rtx_path=None, png_folder=None, stem=None, anim=90, deep=True):
    """The model of an .NGP in its idle, its textures from the .TEX/.RTX next to it (or given) and the PNGs of png_folder named
    after stem over them. -> path of the .glb (cached)."""
    import gltf
    import pose
    import tex
    stem = stem or os.path.splitext(os.path.basename(ngp))[0]
    ov = _overrides(png_folder, stem, deep)
    tex_path = tex_path or gltf.find_sibling(ngp, ".TEX")
    rtx_path = rtx_path or gltf.find_sibling(ngp, ".RTX")
    out = os.path.join(cache_dir(), _key(ngp, tex_path, rtx_path, *sorted(ov.values()), extra="%s|%s" % (stem, anim)) + ".glb")
    if os.path.exists(out):
        return out
    try:
        from PIL import Image
    except ImportError:
        Image = None

    class Lib(tex.Library):
        def image(self, tex_id, tex0, hi=False):
            p = self.by_id.get(tex_id - self.pair.dtex)
            if p is not None and Image is not None:
                _data, is_rtx = self.src[id(p)]
                f = ov.get((is_rtx, p.texId))
                if f:
                    try:
                        with Image.open(f) as im:
                            im = im.convert("RGBA")
                            return im.width, im.height, list(im.getdata())
                    except OSError:
                        pass
            return super().image(tex_id, tex0, hi)

    with _lock:
        mdl = pose.Model(ngp)
        ids = {a[0] for a in mdl.anims()}
        items = mdl.pose(anim if anim in ids else (min(ids) if ids else anim), 0.0, rest=not ids)
        old = tex.Library
        tex.Library = Lib   # gltf.export makes its own; this one reads the work folder's PNGs first
        try:
            gltf.export(ngp, out, skinned=True, items=items, model=mdl, tex_path=tex_path, rtx_path=rtx_path, quiet=True)
        finally:
            tex.Library = old
    return out


class _OverLib:
    """tex.Library with the PNGs of a folder over the textures (what a mod's PNGs do in the game)."""

    def __init__(self, ov):
        self.ov = ov

    def __enter__(self):
        import tex
        try:
            from PIL import Image
        except ImportError:
            Image = None
        ov = self.ov

        class Lib(tex.Library):
            def image(self, tex_id, tex0, hi=False):
                p = self.by_id.get(tex_id - self.pair.dtex)
                if p is not None and Image is not None:
                    _data, is_rtx = self.src[id(p)]
                    f = ov.get((is_rtx, p.texId))
                    if f:
                        try:
                            with Image.open(f) as im:
                                im = im.convert("RGBA")
                                return im.width, im.height, list(im.getdata())
                        except OSError:
                            pass
                return super().image(tex_id, tex0, hi)
        self.old = tex.Library
        tex.Library = Lib
        return self

    def __exit__(self, *a):
        import tex
        tex.Library = self.old


def anim_list(ngp):
    """[{"k", "id", "frames"}] of the model's animations (k: its place in the table; ids repeat)."""
    import pose
    mdl = pose.Model(ngp)
    return [{"k": k, "id": aid, "frames": int(round(et - st)) + 1} for aid, k, st, et, _n in mdl.anims()]


def anim_glb(ngp, k, png_folder=None, stem=None):
    """The model playing animation k (tools/monglb.py, sampled at 60/s as the game poses it), with the folder's PNGs over its
    textures. -> path of the .glb (cached)."""
    import monglb
    stem = stem or os.path.splitext(os.path.basename(ngp))[0]
    ov = _overrides(png_folder, stem)
    tex_path, rtx_path = (os.path.splitext(ngp)[0] + e for e in (".TEX", ".RTX"))
    out = os.path.join(cache_dir(), _key(ngp, tex_path, rtx_path, *sorted(ov.values()), extra="anim+loc|%s|%d" % (stem, k)) + ".glb")
    if os.path.exists(out):
        return out
    with _lock, _OverLib(ov):
        ex = monglb.Exporter(ngp)
        ex.export(out, [int(k)], 60.0, locators=True)
    return out


def file_glb(path):
    """A 3D file of the work folder as a .glb for the page: .glb as it is, .gltf/.dae through the kit's reader (its rest mesh,
    textured), .NGP in its idle with the folder's PNGs."""
    ext = os.path.splitext(path)[1].lower()
    if ext == ".glb":
        return path
    if ext == ".ngp":   # the PNGs next to it go over its textures, as in the game (a rig import's new textures are PNGs)
        return posed_glb(path, png_folder=os.path.dirname(path), deep=False)
    out = os.path.join(cache_dir(), _key(path, extra="file") + ".glb")
    if os.path.exists(out):
        return out
    import orientpage
    import retarget
    with _lock:
        mdl = retarget.load_model(path)
        orientpage.preview_glb(mdl, "-y" if mdl.up_axis == "y" else "+y", out)
    return out


MODEL_EXT = (".glb", ".gltf", ".dae", ".ngp")


class ModelMixin:
    """The Kit's 3D previews (needs disc, mods, mod, work(), work_path())."""

    def group_model(self, key):
        """-> (.NGP, .TEX, .RTX, PNG folder, stem) for a work folder group: a costume ("MON/LIZARD0"), a skin, a custom."""
        import modkit_web_files as wf
        work = self.work()
        if key.startswith("skins/"):
            sid = key[6:]
            data = self.skin_data(sid)
            mkey = str(data.get("monster", "")).lower()
            game = modkit.game_monster(mkey)
            custom = None if game else modkit.find_customs(self.mods).get(mkey)
            prefix = modkit.MONSTERS[game] if game else (custom["prefix"] if custom else None)
            b = data.get("base")
            co = b if isinstance(b, int) and 0 <= b <= 3 else (custom["costume"] if custom else 0)
            if not prefix:
                return None
            folder = os.path.join(work, "skins", sid)
            own = sorted(n for n in (os.listdir(folder) if os.path.isdir(folder) else []) if n.upper().endswith(".NGP"))
            if own:   # a model of its own (Import mesh, a custom's copy, an imported body)
                return os.path.join(folder, own[0]), None, None, folder, os.path.splitext(own[0])[0]
            stem = "%s%d" % (prefix, co)
            return self._disc_model(stem) + (folder, stem)
        if key.startswith("monsters/"):
            cid = key[9:]
            files = os.path.join(work, "monsters", cid, "files")
            own = [n for n in (os.listdir(files) if os.path.isdir(files) else []) if n.upper().endswith(".NGP")]
            if own:
                ngp = os.path.join(files, own[0])
                return ngp, None, None, files, os.path.splitext(own[0])[0]
            data = self.monster_data(cid)
            prefix = modkit.base_prefix(data.get("base")) if isinstance(data.get("base"), str) else None
            if not prefix:
                return None
            co = data.get("costume", 0)
            stem = "%s%d" % (prefix, co if isinstance(co, int) and 0 <= co <= 3 else 0)
            return self._disc_model(stem) + (files, stem)
        c = wf.costume_of_group(key, bosses=True)
        if c:
            stem = "%s%d" % (c[1], c[2])
            return self._disc_model(stem) + (os.path.join(work, *key.split("/")[:-1]), stem)
        return None

    def _disc_model(self, stem):
        ngp = modkit.disc_file(self.disc, "MON/%s.NGP" % stem)
        if not ngp:
            raise SystemExit("MON/%s.NGP is not on the disc" % stem)
        return ngp, modkit.disc_file(self.disc, "MON/%s.TEX" % stem), modkit.disc_file(self.disc, "MON/%s.RTX" % stem)

    def act_anims(self, d):
        """The animations of a group's model: {"anims": [{"k", "id", "frames"}], "prefix"} (prefix: the attacks' names)."""
        g = self.group_model(d.get("group", ""))
        if not g:
            return {"anims": []}
        ngp, _t, _r, _f, stem = g
        import re as _re
        m = _re.match(r"[A-Za-z]+", stem)
        return {"anims": anim_list(ngp), "prefix": m.group().upper() if m else ""}

    def anim_for(self, q):
        """GET /api/anim.glb?group=<key>&k=<n>: the path of the .glb of that animation."""
        g = self.group_model(q.get("group", ""))
        if not g:
            return None
        ngp, _t, _r, folder, stem = g
        return anim_glb(ngp, int(q.get("k", "0")), folder, stem)

    def model_for(self, q):
        """GET /api/model.glb?group=<key> or ?rel=<file>: the path of the .glb to send, or None."""
        if q.get("rel"):
            p = self.work_path(q["rel"])
            return file_glb(p) if p.lower().endswith(MODEL_EXT) and os.path.isfile(p) else None
        g = self.group_model(q.get("group", ""))
        if not g:
            return None
        ngp, tex_path, rtx_path, folder, stem = g
        return posed_glb(ngp, tex_path, rtx_path, folder, stem)
