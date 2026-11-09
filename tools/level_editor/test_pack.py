"""Pack mod (the mod's folder as a .zip) and the terrain a New terrain left behind (/api/props/terrain, "terrain" in /api/props).

Needs no disc. Uses a temporary mods folder; nothing in the repo changes.
Run: python tools/level_editor/test_pack.py
"""
import base64
import io
import json
import os
import sys
import tempfile
import threading
import urllib.error
import urllib.request
import zipfile
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import editor  # noqa: E402


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb" if isinstance(data, bytes) else "w") as f:
        f.write(data)


def main():
    with tempfile.TemporaryDirectory() as mods:
        m = os.path.join(mods, "m")
        write(os.path.join(m, "levels", "CENTRAL.json"), '{"props": []}')
        write(os.path.join(m, "PTS", "CENTRAL.SPT"), b"\x01\x02\x03")
        write(os.path.join(m, "props", "HILLS", "prop.json"), json.dumps({"name": "Hills", "stage": "CENTRAL", "triangles": 8}))
        write(os.path.join(m, "props", "HILLS", "terrain.json"), json.dumps({"size": [2000, 1000], "height": 300, "cells": [64, 32], "triangles": 4096,
                                                                           "brightness": 80, "colors": True}))
        write(os.path.join(m, "props", "HILLS", "heightmap.png"), b"\x89PNGheights")
        write(os.path.join(m, "props", "HILLS", "colors.png"), b"\x89PNGcolors")
        write(os.path.join(m, "props", "PLAIN", "prop.json"), json.dumps({"name": "Plain", "stage": "CENTRAL"}))
        write(os.path.join(m, ".hidden"), "x")
        write(os.path.join(m, "__pycache__", "x.pyc"), b"x")
        os.makedirs(os.path.join(mods, "empty"))
        cfg = {"mods": mods}

        # the zip: one top folder, every file but the hidden ones
        data, name, n = editor.pack_mod(cfg, "m")
        z = zipfile.ZipFile(io.BytesIO(data))
        names = sorted(z.namelist())
        assert name == "m.zip" and n == 7, (name, n)
        assert names == ["m/PTS/CENTRAL.SPT", "m/levels/CENTRAL.json", "m/props/HILLS/colors.png", "m/props/HILLS/heightmap.png",
                         "m/props/HILLS/prop.json", "m/props/HILLS/terrain.json", "m/props/PLAIN/prop.json"], names
        assert z.read("m/PTS/CENTRAL.SPT") == b"\x01\x02\x03" and z.testzip() is None
        for bad, why in (("", "Choose a mod"), ("../x", "can't contain"), ("_m", "start with _"), ("nope", "no folder yet"), ("empty", "empty")):
            try:
                editor.pack_mod(cfg, bad)
                raise AssertionError("%r packed" % bad)
            except ValueError as e:
                assert why in str(e), (bad, str(e))
        old = editor.PACK_MAX
        editor.PACK_MAX = 10
        try:
            editor.pack_mod(cfg, "m")
            raise AssertionError("too big packed")
        except ValueError as e:
            assert "too big" in str(e), str(e)
        finally:
            editor.PACK_MAX = old

        class H(editor.Handler):
            pass
        H.cfg = cfg
        srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        base = "http://127.0.0.1:%d" % srv.server_port

        # the route: a download, and the refusals as 400 with the reason
        r = urllib.request.urlopen(base + "/api/pack?mod=m")
        assert r.headers["Content-Type"] == "application/zip" and 'filename="m.zip"' in r.headers["Content-Disposition"]
        assert zipfile.ZipFile(io.BytesIO(r.read())).namelist()
        try:
            urllib.request.urlopen(base + "/api/pack?mod=nope")
            raise AssertionError("no folder packed")
        except urllib.error.HTTPError as e:
            assert e.code == 400 and "no folder yet" in json.load(e)["error"]

        # the props list says which props have a terrain to open again
        props = {p["model"]: p for p in json.load(urllib.request.urlopen(base + "/api/props?mod=m"))["props"]}
        assert props["HILLS"]["terrain"] is True and props["PLAIN"]["terrain"] is False, props

        # opening it again: the numbers and the pictures
        t = json.load(urllib.request.urlopen(base + "/api/props/terrain?mod=m&name=hills"))
        assert t["terrain"]["size"] == [2000, 1000] and t["terrain"]["cells"] == [64, 32] and t["terrain"]["brightness"] == 80, t
        assert base64.b64decode(t["heights"]) == b"\x89PNGheights" and base64.b64decode(t["colors"]) == b"\x89PNGcolors"
        os.remove(os.path.join(m, "props", "HILLS", "colors.png"))
        assert json.load(urllib.request.urlopen(base + "/api/props/terrain?mod=m&name=HILLS"))["colors"] == ""
        for q, why in (("mod=m&name=PLAIN", "no terrain numbers"), ("mod=m&name=..%2F..", "no terrain numbers"), ("mod=m&name=", "no terrain numbers"),
                       ("mod=nope&name=HILLS", "no terrain numbers"), ("mod=m&name=TOOLONGNAME", "no terrain numbers")):
            try:
                urllib.request.urlopen(base + "/api/props/terrain?" + q)
                raise AssertionError("%s opened" % q)
            except urllib.error.HTTPError as e:
                assert e.code == 400 and why in json.load(e)["error"], q
        srv.shutdown()
    print("ok")


if __name__ == "__main__":
    main()
