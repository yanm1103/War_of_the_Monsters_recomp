"""The game's own pictures for the mod kit page (tools/modkit_web.py), read from the user's disc: the title logo, the drive-in's
sky and tree line (the page's header), the monsters' name logos and the levels' wide pictures (the menu's level select). The
title is the one the game shows: a mod's when an enabled mod replaces it (the project's recompiled_logo), else the disc's.
GET /api/art?name=title|sky|trees|logo-<PREFIX>|level-<STEM> -> PNG, made once per session in a temporary folder; the
portraits (/api/portrait, /api/hud) take part=normal|rage for one square, upright half (portrait_part).

All of them are in SHELL/SHELL.TEX with their palettes in SHELL/SHELL.RTX. The logos and the level pictures are the ones the
port's save list draws (native/wotm_savepics.hpp): T8 tiles whose palette entry the .NGP does not bind (the menu switches it), so
they go by (texId, palette tbp) from the same table. The other three bind through the .NGP, as the texture export does. The rows
are stored bottom-up (the game draws them with V flipped)."""
import atexit
import os
import shutil
import struct
import tempfile
import threading

import modkit
import tex

# monster prefix -> (texId, palette tbp) of its name logo (256x128); native/wotm_savepics.hpp, kLogoPics
LOGOS = {"MONKEY": (122, 400), "ROBOT": (127, 420), "LIZARD": (129, 428), "SHOGUN": (128, 424), "MANTIS": (126, 416),
         "ENERGY": (123, 404), "LAVA": (125, 412), "DRAGON": (52, 156), "ROCK": (124, 408), "ALIEN": (121, 396)}
# level stem -> the two 256x256 halves of its wide picture (512x256); kLevelPics
LEVEL_PICS = {"CENTRAL": ((99, 308), (100, 312)), "VEGAS": ((118, 384), (119, 388)), "CANYON2": ((102, 320), (103, 324)),
              "METRO": ((108, 344), (109, 348)), "AIRPORT": ((93, 284), (94, 288)), "3-MILE": ((84, 248), (85, 252)),
              "SANFRAN": ((111, 356), (112, 360)), "ISLAND": ((105, 332), (106, 336)), "TOKYO": ((114, 368), (115, 372)),
              "UFO": ((88, 264), (89, 268)), "CAPITOL": ((96, 296), (97, 300)), "MINISF": ((86, 256), (87, 260)),
              "VOLCANO": ((90, 272), (91, 276))}
# name -> texIds side by side (bound through the .NGP)
BOUND = {"title": (140, 141), "sky": (174,), "trees": (189,)}

_lock = threading.Lock()


class _Shell:
    """SHELL.TEX/.RTX of one disc, parsed once."""

    def __init__(self, disc):
        tp = modkit.disc_file(disc, "SHELL/SHELL.TEX")
        rp = modkit.disc_file(disc, "SHELL/SHELL.RTX")
        if not tp or not rp:
            raise OSError("no SHELL.TEX/.RTX on the disc")
        self.td, _ = tex.read_file(tp)
        self.rd, _ = tex.read_file(rp)
        self.t = tex.parse(self.td)
        self.r = tex.parse(self.rd)
        ngp = tex.sibling(tp, ".NGP")
        self.pair = tex.Pairing(self.t, self.rd, self.r, tex.ngp_texrefs(ngp) if ngp else [])
        self.by_id = {p.texId: p for p in self.t["packets"]}

    def tile(self, tex_id, pal_tbp):
        """(w, h, rows top-down) of a T8 tile with the palette entry at pal_tbp (CSM1 order, GS alpha), or None."""
        p = self.by_id.get(tex_id)
        pal = [k for k in self.r["packets"] if k.tbp == pal_tbp and k.psm == 0 and k.w == 16 and k.h == 16]
        if p is None or not pal or p.psm != tex.T8:
            return None
        po = pal[-1].off + 0x10
        clut = []
        for i in range(256):
            r, g, b, a = struct.unpack_from("<4B", self.rd, po + 4 * tex.csm1(i))
            clut.append((r, g, b, 255 if a >= 0x80 else a * 2))
        src = p.data_off(False)
        return p.w, p.h, [[clut[c] for c in self.td[src + y * p.w:src + (y + 1) * p.w]] for y in range(p.h - 1, -1, -1)]

    def bound(self, tex_id):
        """The same, for a texture whose palette the .NGP binds (as the export decodes it)."""
        p = self.by_id.get(tex_id)
        if p is None:
            return None
        off = p.data_off(False)
        if p.psm in (tex.T8, tex.T4, tex.T8H, tex.T4HL, tex.T4HH):
            pal, _cbp, csa, _ = self.pair.palette(p, hi=True)
            if pal is None:
                return None
            if p.psm in (tex.T4, tex.T4HL, tex.T4HH) and len(pal) < 256:
                csa = 0
            psm = p.psm if p.psm not in tex.HIGH else (tex.T8 if p.psm == tex.T8H else tex.T4)
            px = tex.decode(self.td, off, psm, p.w, p.h, pal, csa)
            fix = not tex.raw_hi(px)
        elif p.psm in (tex.CT32, tex.CT24, tex.CT16, tex.CT16S):
            px = tex.decode(self.td, off, p.psm, p.w, p.h, hi=True)
            fix = p.psm == tex.CT32 and not tex.raw_hi(px)
        else:
            return None
        if fix:
            px = [(r, g, b, tex.fix_alpha(a)) for (r, g, b, a) in px]
        return p.w, p.h, [px[y * p.w:(y + 1) * p.w] for y in range(p.h - 1, -1, -1)]


def _side_by_side(tiles):
    """Tiles of the same height next to each other -> (w, h, flat pixels)."""
    if not tiles or any(t is None for t in tiles) or len({t[1] for t in tiles}) != 1:
        return None
    h = tiles[0][1]
    rows = [[px for t in tiles for px in t[2][y]] for y in range(h)]
    return sum(t[0] for t in tiles), h, [px for r in rows for px in r]


class ArtMixin:
    """The Kit's part for the game's pictures (needs disc)."""

    def art(self, name):
        """Path of the PNG for name (see the module's text), or None when the disc does not have it."""
        with _lock:
            if getattr(self, "_art_disc", None) != (self.disc, self.mods):
                self._art_disc = (self.disc, self.mods)
                self._art_dir = tempfile.mkdtemp(prefix="wotm_art_")
                atexit.register(shutil.rmtree, self._art_dir, True)
                self._art = {}
                self._shell = None
            if name not in self._art:
                self._art[name] = self._make_art(name)
            return self._art[name]

    def _make_art(self, name):
        kind, _, arg = name.partition("-")
        if not (name in BOUND or (kind == "logo" and arg in LOGOS) or (kind == "level" and arg in LEVEL_PICS)):
            return None
        try:
            if name == "title":
                out = self._mod_title()
                if out:
                    return out
            if self._shell is None:
                self._shell = _Shell(self.disc)
            s = self._shell
            if name in BOUND:
                img = _side_by_side([s.bound(t) for t in BOUND[name]])
            elif kind == "logo":
                img = _side_by_side([s.tile(*LOGOS[arg])])
            else:
                img = _side_by_side([s.tile(*t) for t in LEVEL_PICS[arg]])
            if not img:
                return None
            out = os.path.join(self._art_dir, name + ".png")
            tex.write_png(out, *img)
            return out
        except Exception:  # noqa: BLE001 (no disc, a damaged file: the page goes without the picture)
            return None

    def portrait_part(self, path, part):
        """A HUD portrait (2:1: the normal face on the left, the rage one on the right, rows bottom-up as the HUD texture;
        the disc's, or a custom's portrait.png) -> one half, square and upright, for part "normal" or "rage". The same path
        when it cannot (another part, no Pillow, a file that is not 2:1)."""
        if part not in ("normal", "rage"):
            return path
        with _lock:
            if getattr(self, "_art_disc", None) is None:
                self._art_disc = (self.disc, self.mods)
                self._art_dir = tempfile.mkdtemp(prefix="wotm_art_")
                atexit.register(shutil.rmtree, self._art_dir, True)
                self._art = {}
                self._shell = None
            key = ("part", os.path.normcase(os.path.abspath(path)), os.path.getmtime(path), part)
            if key in self._art:
                return self._art[key] or path
            out = None
            try:
                from PIL import Image
                with Image.open(path) as im:
                    im = im.convert("RGBA")
                    if im.width == 2 * im.height:
                        h = im.height
                        half = im.crop((0, 0, h, h) if part == "normal" else (h, 0, 2 * h, h))
                        out = os.path.join(self._art_dir, "portrait_%d_%s.png" % (len(self._art), part))
                        half.transpose(Image.Transpose.FLIP_TOP_BOTTOM).save(out)
            except Exception:  # noqa: BLE001 (no Pillow, a damaged file: the whole portrait as it is)
                out = None
            self._art[key] = out
            return out or path

    def _mod_title(self):
        """The title the game shows when a mod replaces it: the last mod in load order (alphabetical, as the loader) with both
        halves as PNGs in SHELL/ (the project's recompiled_logo). Side by side and turned upright, at most 1024 wide; None
        without such a mod (or without Pillow)."""
        names = ["shell_t%04d.png" % t for t in BOUND["title"]]
        found = None
        for _mod, folder in sorted(modkit.mod_folders(self.mods).items()):
            paths = [os.path.join(folder, "SHELL", n) for n in names]
            if all(os.path.isfile(x) for x in paths):
                found = paths
        if not found:
            return None
        from PIL import Image
        ims = [Image.open(x).convert("RGBA") for x in found]
        h = min(im.height for im in ims)
        ims = [im if im.height == h else im.resize((round(im.width * h / im.height), h)) for im in ims]
        out = Image.new("RGBA", (sum(im.width for im in ims), h))
        x = 0
        for im in ims:
            out.paste(im, (x, 0))
            x += im.width
        out = out.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        box = out.getbbox()   # the letters, without the empty margins
        if box:
            out = out.crop(box)
        if out.width > 1024:
            out = out.resize((1024, max(1, round(out.height * 1024 / out.width))), Image.Resampling.LANCZOS)
        path = os.path.join(self._art_dir, "title.png")
        out.save(path)
        return path
