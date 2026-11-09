"""Integration test: the editor serves, checks and saves a custom stage (mods/<mod>/stages/<pasta>/stage.json).

Needs the disc configured in the editor (Settings). Uses a temporary mods folder; nothing in the repo changes.
Run: python tools/level_editor/test_custom_stages.py
"""
import base64
import json
import shutil
import os
import sys
import struct
import tempfile
import threading
import urllib.error
import zlib
import urllib.request
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import editor  # noqa: E402


def main():
    cfg = dict(editor.load_config())
    with tempfile.TemporaryDirectory() as mods:
        stages = os.path.join(mods, "m", "stages")
        for folder, j in (("ok", {"id": "mycity", "name": "MY CITY", "base": "CENTRAL"}),
                          ("bad_base", {"id": "airp", "base": "AIRPORT"}),
                          ("bad_id", {"id": "central", "base": "CENTRAL"})):
            os.makedirs(os.path.join(stages, folder))
            with open(os.path.join(stages, folder, "stage.json"), "w") as f:
                json.dump(j, f)
        cfg["mods"] = mods

        class H(editor.Handler):
            pass
        H.cfg = cfg
        srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        base = "http://127.0.0.1:%d" % srv.server_port

        def get(p):
            return json.load(urllib.request.urlopen(base + p))

        def post(p, b):
            r = urllib.request.Request(base + p, json.dumps(b).encode(), {"Content-Type": "application/json"})
            return json.load(urllib.request.urlopen(r))

        custom = [s for s in get("/api/init")["stages"] if s.get("custom")]
        assert [(s["name"], s["base"]) for s in custom] == [("MYCITY", "CENTRAL")], custom
        mine, base_stage = get("/api/stage?name=MYCITY&mod=m"), get("/api/stage?name=CENTRAL&mod=m")
        assert len(mine["objects"]) == len(base_stage["objects"]) > 100
        assert post("/api/check", {"name": "MYCITY", "mod": "m", "patch": {"objects": []}})["ok"]
        post("/api/save", {"name": "MYCITY", "mod": "m", "patch": {"objects": [], "names": {"0x1": "x"}}})
        path = os.path.join(mods, "m", "levels", "MYCITY.json")
        assert os.path.isfile(path) and not os.path.exists(os.path.join(mods, "m", "levels", "CENTRAL.json"))
        post("/api/save", {"name": "MYCITY", "mod": "m", "patch": {"objects": []}})
        assert not os.path.exists(path)
        # own points: a .PED in the folder replaces the base's net in the editor (and in its check)
        other = os.path.join(editor.stage_root(cfg["disc"], "VEGAS"), "PTS", "VEGAS.PED")
        if os.path.isfile(other):
            shutil.copyfile(other, os.path.join(stages, "ok", "my.ped"))
            own = get("/api/stage?name=MYCITY&mod=m")["peds"]["net"]
            assert len(own) != len(base_stage["peds"]["net"]), "the custom stage kept the base's pedestrian net"
            assert post("/api/check", {"name": "MYCITY", "mod": "m", "patch": {"objects": []}})["ok"]
        # New stage…: written to the mod, listed at once, with the patch it was given
        made = post("/api/stage/new", {"mod": "m", "id": "newone", "name": "New One", "base": "VEGAS",
                                       "patch": {"objects": [], "names": {"0x1": "x"}}})
        assert made["name"] == "NEWONE" and made["base"] == "VEGAS", made
        assert os.path.isfile(os.path.join(mods, "m", "stages", "newone", "stage.json"))
        assert os.path.isfile(os.path.join(mods, "m", "levels", "NEWONE.json"))
        assert "NEWONE" in [s["name"] for s in get("/api/init")["stages"]]
        for bad in ({"id": "newone", "base": "VEGAS"}, {"id": "central", "base": "VEGAS"}, {"id": "1x", "base": "VEGAS"},
                    {"id": "okid", "base": "TRAINING"}, {"id": "okid", "base": "VEGAS", "name": "x" * 18}):
            try:
                post("/api/stage/new", dict(bad, mod="m"))
            except urllib.error.HTTPError as e:
                assert e.code == 400
            else:
                raise AssertionError("accepted " + repr(bad))
        # Edit stage…: name, base, narrator and RANDOM are rewritten, the id and the unknown keys stay; defaults leave no key
        path = os.path.join(mods, "m", "stages", "newone", "stage.json")
        with open(path) as f:
            j = json.load(f)
        j["note"] = "kept"
        with open(path, "w") as f:
            json.dump(j, f)
        r = post("/api/stage/edit", {"stage": "NEWONE", "name": "Renamed", "base": "CENTRAL", "narrator": "newone.mp3", "random": False})
        assert r["title"] == "Renamed" and r["base"] == "CENTRAL" and r["baseChanged"] and r["narrator"] == "newone", r
        with open(path) as f:
            j = json.load(f)
        assert j == {"id": "newone", "name": "Renamed", "base": "CENTRAL", "narrator": "newone", "random": False, "note": "kept"}, j
        entry = [s for s in get("/api/init")["stages"] if s["name"] == "NEWONE"][0]
        assert entry["base"] == "CENTRAL" and entry["narrator"] == "newone" and entry["random"] is False, entry
        r = post("/api/stage/edit", {"stage": "NEWONE", "name": "", "base": "CENTRAL", "narrator": "", "random": True})
        assert r["title"] == "NEWONE" and not r["baseChanged"], r
        with open(path) as f:
            j = json.load(f)
        assert j == {"id": "newone", "name": "NEWONE", "base": "CENTRAL", "note": "kept"}, j
        for bad in ({"stage": "CENTRAL", "base": "CENTRAL"}, {"stage": "NEWONE", "base": "TRAINING"},
                    {"stage": "NEWONE", "base": "CENTRAL", "name": "x" * 18}, {"stage": "NEWONE", "base": "CENTRAL", "narrator": "a/b"}):
            try:
                post("/api/stage/edit", bad)
            except urllib.error.HTTPError as e:
                assert e.code == 400
            else:
                raise AssertionError("accepted " + repr(bad))
        # Stage size: /api/stage tells the disc's grid; a bigger "grid" in the patch passes the check, a smaller one is reported
        g = base_stage["grid"]
        assert g and g["cols"] > 1 and g["min"][0] < g["max"][0], g
        big = {"min": [g["min"][0] - 1000, g["min"][1] - 1000], "max": [g["max"][0] + 1000, g["max"][1] + 1000]}
        ok = post("/api/check", {"name": "CENTRAL", "mod": "m", "patch": {"objects": [], "grid": big}})
        assert ok["ok"] and any(l.startswith("grid:") for l in ok["lines"]), ok["problems"]
        small = post("/api/check", {"name": "CENTRAL", "mod": "m", "patch": {"objects": [], "grid": {"min": [0, 0], "max": [10, 10]}}})
        assert not small["ok"] and small["problems"][0]["kind"] == "grid", small["problems"]
        # Stage picture: a 2:1 PNG goes to levelpics/<id>.png; a game stage with its picture in SHELL.TEX and a 1:1 PNG are refused
        def png(w, h):
            def chunk(t, d):
                return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
            raw = b"".join(b"\x00" + b"\x40\x80\xc0" * w for _ in range(h))
            return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
        enc = lambda d: base64.b64encode(d).decode()      # noqa: E731
        r = post("/api/levelpic", {"mod": "m", "name": "MYCITY", "png": enc(png(16, 8))})
        assert os.path.isfile(os.path.join(mods, "m", "levelpics", "mycity.png")), r
        # Fight HUD of a custom stage: the picture goes in the stage's own folder, not in the mod's LVL/ (CENTRAL keeps its own)
        rows = get("/api/hud?stage=MYCITY&mod=m")["rows"]
        assert any(r["role"] == "sky" and r["every"] == 0 for r in rows), "no sky row"
        row = rows[0]
        res = post("/api/hud/replace", {"mod": "m", "stage": "MYCITY", "role": row["role"], "png": row["png"], "data": enc(png(16, 8)),
                                        "every": True})
        assert os.path.isfile(os.path.join(stages, "ok", row["png"])), res
        assert not os.path.exists(os.path.join(mods, "m", "LVL", row["png"]))
        # the base stage's picture in the mod does not touch the custom stage's own, and the base's panel does not see it
        post("/api/hud/replace", {"mod": "m", "stage": "CENTRAL", "role": row["role"], "png": row["png"], "data": enc(png(16, 8))})
        assert os.path.isfile(os.path.join(stages, "ok", row["png"])), "the base's HUD picture deleted the custom stage's"
        os.remove(os.path.join(mods, "m", "LVL", row["png"]))
        post("/api/hud/remove", {"mod": "m", "stage": "MYCITY", "role": row["role"], "png": row["png"]})
        assert not os.path.exists(os.path.join(stages, "ok", row["png"]))
        for bad in ({"name": "CENTRAL", "png": enc(png(16, 8))}, {"name": "MYCITY", "png": enc(png(8, 8))}):
            try:
                post("/api/levelpic", dict(bad, mod="m"))
            except urllib.error.HTTPError as e:
                assert e.code == 400
            else:
                raise AssertionError("accepted " + repr(bad["name"]))
        # a file that changed after the page read it is not overwritten without asking: /api/stage tells its stamp, Save sends it back
        lv = os.path.join(mods, "m", "levels", "CENTRAL.json")
        assert not os.path.exists(lv) and get("/api/stage?name=CENTRAL&mod=m")["stamp"] == 0
        first = post("/api/save", {"name": "CENTRAL", "mod": "m", "patch": {"objects": [], "names": {"0x1": "a"}}, "since": 0})
        assert first["stamp"] > 0 and first["stamp"] == get("/api/stage?name=CENTRAL&mod=m")["stamp"], first
        with open(lv, "w") as f:                  # somebody edits it by hand
            json.dump({"objects": [], "names": {"0x1": "by hand"}}, f)
        os.utime(lv, (first["stamp"] / 1e6 + 5, first["stamp"] / 1e6 + 5))
        try:
            post("/api/save", {"name": "CENTRAL", "mod": "m", "patch": {"objects": [], "names": {"0x1": "b"}}, "since": first["stamp"]})
        except urllib.error.HTTPError as e:
            assert e.code == 409 and json.loads(e.read())["code"] == "changed"
        else:
            raise AssertionError("overwrote a file that changed on disk")
        with open(lv) as f:
            assert json.load(f)["names"]["0x1"] == "by hand"
        again = post("/api/save", {"name": "CENTRAL", "mod": "m", "patch": {"objects": [], "names": {"0x1": "b"}},
                                   "since": first["stamp"], "overwrite": True})
        with open(lv) as f:
            assert json.load(f)["names"]["0x1"] == "b" and again["stamp"] != first["stamp"]
        post("/api/save", {"name": "CENTRAL", "mod": "m", "patch": {"objects": [], "names": {"0x1": "c"}}, "since": again["stamp"]})
        srv.shutdown()
    print("ok")


if __name__ == "__main__":
    main()
