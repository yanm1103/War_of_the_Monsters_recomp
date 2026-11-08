"""Integration test: an original object of another stage becomes a prop of the mod (Ctrl+P, "Originals from other stages").

Needs the disc configured in the editor (Settings). Uses a temporary mods folder; nothing in the repo changes.
Run: python tools/level_editor/test_originals.py
"""
import json
import os
import struct
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import editor  # noqa: E402


def main():
    cfg = dict(editor.load_config())
    with tempfile.TemporaryDirectory() as mods:
        cfg["mods"] = mods
        os.makedirs(os.path.join(mods, "m"))

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
                return json.load(urllib.request.urlopen(r))
            except urllib.error.HTTPError as e:
                return {"http": e.code, **json.load(e)}

        t0 = time.time()
        while True:
            idx = get("/api/originals")
            if idx["ready"]:
                break
            assert time.time() - t0 < 600, idx["done"]
            time.sleep(1)
        items = idx["items"]
        stages = {i["stage"] for i in items}
        assert len(stages) >= 15 and len(items) > 500, (len(stages), len(items))
        assert {i["kind"] for i in items} == {"prop", "destr"}
        assert all(i["sig"] and i["handle"].startswith("0x") for i in items)
        assert len({(i["stage"], i["handle"]) for i in items}) == len(items), "two models on one node"

        # the palette's pictures: none at first, kept once the page sends one, only PNGs
        first = next(i for i in items if i["stage"] == "VEGAS")
        pic = "/api/origpic?stage=VEGAS&handle=" + first["handle"]
        kept = editor.original_pic_path(cfg, "VEGAS", first["handle"])
        saved = None
        if os.path.isfile(kept):                # the user's cache: put aside, back at the end
            with open(kept, "rb") as f:
                saved = f.read()
            os.remove(kept)
        try:
            urllib.request.urlopen(base + pic)
            raise AssertionError("a picture before anyone sent one")
        except urllib.error.HTTPError as e:
            assert e.code == 404
        import base64
        import zlib
        png = b"\x89PNG\r\n\x1a\n"
        for typ, body in ((b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)), (b"IDAT", zlib.compress(b"\0\xff\0\0\xff")), (b"IEND", b"")):
            png += struct.pack(">I", len(body)) + typ + body + struct.pack(">I", zlib.crc32(typ + body))
        r = post("/api/origpic", {"stage": "VEGAS", "handle": first["handle"], "png": base64.b64encode(png).decode()})
        assert r.get("ok"), r
        assert urllib.request.urlopen(base + pic).read() == png
        assert post("/api/origpic", {"stage": "VEGAS", "handle": first["handle"], "png": base64.b64encode(b"nope").decode()}).get("http") == 400
        assert post("/api/origpic", {"stage": "../x", "handle": "0x1", "png": base64.b64encode(png).decode()}).get("http") == 400
        os.remove(kept)
        if saved is not None:                   # the cache is the user's: leave it as found
            with open(kept, "wb") as f:
                f.write(saved)

        # a breakable building of VEGAS (two states) and a prop of VEGAS, as props of CENTRAL
        destr = next(i for i in items if i["stage"] == "VEGAS" and i["kind"] == "destr" and i["states"] == 2)
        prop = next(i for i in items if i["stage"] == "VEGAS" and i["kind"] == "prop")
        glb = urllib.request.urlopen(base + "/api/origglb?stage=VEGAS&handle=" + destr["handle"]).read()
        assert glb[:4] == b"glTF" and len(glb) < 200000, len(glb)
        for it, kind in ((destr, "destructible"), (prop, "static")):
            body = {"mod": "m", "stage": "CENTRAL", "from": "VEGAS", "handle": it["handle"]}
            r = post("/api/props/original", body)
            assert "prop" in r and not r["prop"].get("error"), r
            p = r["prop"]
            assert p["stage"] == "CENTRAL" and p["origin"]["stage"] == "VEGAS" and p["origin"]["sig"] == it["sig"], p
            assert (p["destructible"] is not None) == (kind == "destructible"), p
            assert r["reused"] is False
            again = post("/api/props/original", body)      # the same model is not built twice
            assert again["reused"] is True and again["prop"]["model"] == p["model"], again
            folder = os.path.join(mods, "m", "props", p["model"])
            for f in (p["model"] + ".NGP", p["model"] + ".PTR", "prop.json", p["model"] + ".preview.glb"):
                assert os.path.isfile(os.path.join(folder, f)), f
        names = [p["model"] for p in get("/api/props?mod=m")["props"]]
        assert len(names) == 2 and len(set(names)) == 2, names
        # a pickup, a stage that is not on the disc and a bad handle are refused
        for bad in ({"from": "VEGAS", "handle": "0x1"}, {"from": "NOPE", "handle": destr["handle"]},
                    {"from": "VEGAS", "handle": "x"}):
            r = post("/api/props/original", dict({"mod": "m", "stage": "CENTRAL"}, **bad))
            assert r.get("http") == 400, r
        srv.shutdown()
    print("ok: %d models in %d stages; a breakable and a plain one became props of CENTRAL and were reused" % (len(items), len(stages)))


if __name__ == "__main__":
    main()
