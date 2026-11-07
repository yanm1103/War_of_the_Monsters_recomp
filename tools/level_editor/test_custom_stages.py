"""Integration test: the editor serves, checks and saves a custom stage (mods/<mod>/stages/<pasta>/stage.json).

Needs the disc configured in the editor (Settings). Uses a temporary mods folder; nothing in the repo changes.
Run: python tools/level_editor/test_custom_stages.py
"""
import json
import os
import sys
import tempfile
import threading
import urllib.error
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
        srv.shutdown()
    print("ok")


if __name__ == "__main__":
    main()
