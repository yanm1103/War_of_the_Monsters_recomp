"""The editor's prefabs (mods/<mod>/prefabs/<name>.json): saving, listing, replacing, deleting and what it refuses.

Needs no disc. Uses a temporary mods folder; nothing in the repo changes.
Run: python tools/level_editor/test_prefabs.py
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

ENTRIES = [
    {"kind": "object", "handle": "0xb18310", "from": [697.7, 972.2, 0.5], "d": [-20, -30, 0], "heading": 90, "name": "Hotel"},
    {"kind": "prop", "model": "box", "d": [20, 30, 15.04], "heading": 30, "pitch": 20, "roll": -15},
    {"kind": "object", "handle": "0xB31DC0", "from": [663, 742.6, 0.5], "d": [0, -20, 2], "heading": 0, "on": 0},
]


def main():
    with tempfile.TemporaryDirectory() as mods:
        os.makedirs(os.path.join(mods, "m"))
        cfg = {"mods": mods}

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
            try:
                return 200, json.load(urllib.request.urlopen(r))
            except urllib.error.HTTPError as e:
                return e.code, json.load(e)

        assert get("/api/prefabs?mod=m") == {"prefabs": []}
        assert get("/api/prefabs?mod=nope") == {"prefabs": []}
        code, r = post("/api/prefab", {"mod": "m", "name": "Gas  station", "stage": "central", "entries": ENTRIES})
        assert code == 200 and r["file"] == "gas-station" and r["name"] == "Gas station" and r["stage"] == "CENTRAL", r
        # handles and models come back upper case, numbers rounded to 0.1, empty tilt left out
        e = r["entries"]
        assert e[0]["handle"] == "0xB18310" and e[1]["model"] == "BOX" and e[1]["d"] == [20, 30, 15.0], e
        assert e[1]["pitch"] == 20 and e[1]["roll"] == -15 and "pitch" not in e[0] and e[2]["on"] == 0, e
        path = os.path.join(mods, "m", "prefabs", "gas-station.json")
        assert os.path.isfile(path) and json.load(open(path))["format"] == 1
        assert get("/api/prefabs?mod=m")["prefabs"] == [r]
        # the same name again: refused until it says overwrite
        code, r2 = post("/api/prefab", {"mod": "m", "name": "gas station", "stage": "CENTRAL", "entries": ENTRIES[:1]})
        assert code == 409 and r2["code"] == "exists", (code, r2)
        code, r2 = post("/api/prefab", {"mod": "m", "name": "gas station", "stage": "CENTRAL", "entries": ENTRIES[:1], "overwrite": True})
        assert code == 200 and len(get("/api/prefabs?mod=m")["prefabs"]) == 1 and len(r2["entries"]) == 1
        post("/api/prefab", {"mod": "m", "name": "Second", "stage": "VEGAS", "entries": ENTRIES})
        assert [p["file"] for p in get("/api/prefabs?mod=m")["prefabs"]] == ["gas-station", "second"]
        # a file that does not read is left out of the list
        with open(os.path.join(mods, "m", "prefabs", "broken.json"), "w") as f:
            f.write("{nope")
        assert len(get("/api/prefabs?mod=m")["prefabs"]) == 2
        # refusals
        good = {"mod": "m", "name": "x", "stage": "CENTRAL", "entries": ENTRIES}
        for what, patch in (
            ("no mod", {"mod": ""}), ("bad mod name", {"mod": "..\\x"}), ("no name", {"name": "  "}), ("name too long", {"name": "n" * 41}),
            ("a stage the editor does not know", {"stage": "MARS"}), ("no entries", {"entries": []}),
            ("too many", {"entries": ENTRIES[:1] * (editor.PREFAB_MAX + 1)}),
            ("an entry that is neither", {"entries": [{"kind": "ped", "d": [0, 0, 0]}]}),
            ("no offset", {"entries": [{"kind": "prop", "model": "BOX"}]}),
            ("a bad handle", {"entries": [{"kind": "object", "handle": "B18310", "from": [0, 0, 0], "d": [0, 0, 0]}]}),
            ("no from", {"entries": [{"kind": "object", "handle": "0x1", "d": [0, 0, 0]}]}),
            ("a bad model", {"entries": [{"kind": "prop", "model": "../BOX", "d": [0, 0, 0]}]}),
            ("a far offset", {"entries": [{"kind": "prop", "model": "BOX", "d": [1e9, 0, 0]}]}),
            ("a text for a number", {"entries": [{"kind": "prop", "model": "BOX", "d": ["1", 0, 0]}]}),
            ("a roll of 90", {"entries": [{"kind": "prop", "model": "BOX", "d": [0, 0, 0], "roll": 90}]}),
            ("resting on itself", {"entries": [{"kind": "prop", "model": "BOX", "d": [0, 0, 0], "on": 0}]}),
            ("resting on nothing", {"entries": [{"kind": "prop", "model": "BOX", "d": [0, 0, 0], "on": 5}]}),
        ):
            code, r = post("/api/prefab", dict(good, **patch))
            assert code == 400 and r["code"] == "prefab", (what, code, r)
        assert not os.path.exists(os.path.join(mods, "m", "prefabs", "x.json"))
        # delete
        code, r = post("/api/prefab/delete", {"mod": "m", "file": "second"})
        assert code == 200 and [p["file"] for p in get("/api/prefabs?mod=m")["prefabs"]] == ["gas-station"]
        for f in ("second", "../gas-station", "", "Gas-Station"):
            code, r = post("/api/prefab/delete", {"mod": "m", "file": f})
            assert code == 400, (f, code, r)
        srv.shutdown()
    print("ok")


main()
