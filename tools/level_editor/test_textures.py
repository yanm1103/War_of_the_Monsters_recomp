"""Test: the editor's Textures dialog on the server side (hudapi.HudApi.textures / image / tex_replace / tex_remove, lvlhud.stage_textures).
Needs the game disc configured in the editor; writes only into a temporary mods folder.
Run: python tools/level_editor/test_textures.py
"""
import base64
import os
import shutil
import struct
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE)))
sys.path.insert(0, HERE)
import editor   # noqa: E402
import lvlhud   # noqa: E402


def tiny_png(w=4, h=4):
    raw = b"".join(b"\x00" + b"\xff\x00\x00\xff" * w for _ in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    cfg = dict(editor.load_config())
    tmp = tempfile.mkdtemp(prefix="wotm_textest_")
    try:
        os.makedirs(os.path.join(tmp, "T"))
        disc = cfg["disc"]
        for stage in ("CENTRAL", "VEGAS"):
            texs = lvlhud.stage_textures(editor.stage_root(disc, stage, (("LVL", ("NGP", "TEX", "RTX")),)), stage)
            assert len(texs) > 20 and texs == sorted(texs, key=lambda e: e["tex"]), (stage, len(texs))
            assert all(e["w"] > 0 and e["h"] > 0 and e["file"] in ("tex", "rtx") for e in texs)
        panel = editor.HUD.textures(disc, tmp, "T", "CENTRAL")
        rows = panel["rows"]
        assert panel["stage"] == "CENTRAL" and len(rows) > 100, len(rows)
        # the HUD pictures are among them, with their roles; the ones nobody uses have none
        assert any("atlas" in r["roles"] for r in rows) and any(not r["roles"] for r in rows)
        # every row has the disc's PNG (exported once)
        assert all(r["disc"] for r in rows), [r["png"] for r in rows if not r["disc"]][:5]
        r0 = rows[0]
        assert not r0["mod"]
        path = editor.HUD.image(disc, tmp, "T", "CENTRAL", "disc", r0["png"])
        assert path and open(path, "rb").read(8) == b"\x89PNG\r\n\x1a\n"
        assert editor.HUD.image(disc, tmp, "T", "CENTRAL", "disc", "../../x.png") is None
        # replace: lands in <mod>/LVL/, shows as the mod's, comes out the same bytes; remove takes it away
        png = tiny_png()
        lines = editor.HUD.tex_replace(disc, tmp, "T", "CENTRAL", r0["png"], base64.b64encode(png).decode())
        assert os.path.isfile(os.path.join(tmp, "T", "LVL", r0["png"])) and r0["png"] in lines[0], lines
        again = editor.HUD.textures(disc, tmp, "T", "CENTRAL")["rows"][0]
        assert again["mod"]
        assert open(editor.HUD.image(disc, tmp, "T", "CENTRAL", "mod", r0["png"]), "rb").read() == png
        # replacing again moves nothing elsewhere (one copy of the name in the mod)
        editor.HUD.tex_replace(disc, tmp, "T", "CENTRAL", r0["png"], base64.b64encode(png).decode(), "")
        assert not os.path.exists(os.path.join(tmp, "T", "LVL", r0["png"])) and os.path.isfile(os.path.join(tmp, "T", r0["png"]))
        editor.HUD.tex_remove(disc, tmp, "T", "CENTRAL", r0["png"])
        assert not editor.HUD.textures(disc, tmp, "T", "CENTRAL")["rows"][0]["mod"]
        # refused: a name that is not the stage's, and bytes that are not a PNG
        for bad in ("central_t9999.png", "vegas_t0001.png", "../x.png"):
            try:
                editor.HUD.tex_replace(disc, tmp, "T", "CENTRAL", bad, base64.b64encode(png).decode())
            except ValueError:
                continue
            raise AssertionError("accepted " + bad)
        try:
            editor.HUD.tex_replace(disc, tmp, "T", "CENTRAL", r0["png"], base64.b64encode(b"nope").decode())
        except ValueError:
            pass
        else:
            raise AssertionError("accepted a non-PNG")
        print("ok: %d pictures in CENTRAL" % len(rows))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
