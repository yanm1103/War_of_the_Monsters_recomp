#!/usr/bin/env python3
"""The page where the modder sets up a downloaded model before the kit retargets it ("Import rigged model..."), in two steps:

1. Facing: the model next to the base monster, both in the game's axes, with quarter-turn buttons (`retarget.py --turn`).
2. Bones: both skeletons drawn over the meshes, each joint of the model in the color of the monster's bone it follows (the
   bone map, `tools/automap.py`'s or the one already in the work folder). Clicking a joint changes the bone it follows or turns
   it at rest by steps of 15 degrees (a hand closed into a fist, a mouth opened); the model is redrawn at the rest the
   retarget will give it (`retarget.build(..., rest=...)`), so what the page shows is what goes into the game.

  python tools/orientpage.py <base.glb> <model.dae|.glb|.gltf>     # opens the page, prints the turns and the map

The page is `tools/orientpage.html`, served from 127.0.0.1 with the three.js of the level editor
(`tools/level_editor/web/vendor`; inside the mod kit's exe both are bundled under `orient/`). The model goes to the page as a
small .glb: its rest mesh after the up axis and front the retarget would use, in the game's axes under the same "WotM" root
as the kit's export, with its textures. Standard library only.
"""
import json
import os
import re
import struct
import sys
import tempfile
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retarget  # noqa: E402
from ngpanim import ROOT_Q  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def _assets():
    """(page, vendor folder): the repo's, or the copies inside the mod kit's exe."""
    mp = getattr(sys, "_MEIPASS", None)
    if mp and os.path.isdir(os.path.join(mp, "orient")):
        return os.path.join(mp, "orient", "orientpage.html"), os.path.join(mp, "orient", "vendor")
    return os.path.join(HERE, "orientpage.html"), os.path.join(HERE, "level_editor", "web", "vendor")


def _write_glb(mdl, groups, out_path):
    """groups: {image key: [(position, normal, uv) per corner]} in the game's axes -> a textured .glb under the "WotM" root."""
    binb = bytearray()
    j = {"asset": {"version": "2.0", "generator": "WotM tools/orientpage.py"}, "scene": 0, "scenes": [{"nodes": [0]}],
         "nodes": [{"name": "WotM", "rotation": list(ROOT_Q), "children": [1]}, {"name": "model", "mesh": 0}],
         "meshes": [{"primitives": []}], "materials": [], "textures": [], "images": [], "samplers": [{}],
         "accessors": [], "bufferViews": [], "buffers": []}

    def view(data):
        while len(binb) % 4:
            binb.append(0)
        j["bufferViews"].append({"buffer": 0, "byteOffset": len(binb), "byteLength": len(data)})
        binb.extend(data)
        return len(j["bufferViews"]) - 1

    def acc(fmt, ctype, typ, rows, minmax=False):
        flat = [x for r in rows for x in r]
        a = {"bufferView": view(struct.pack("<%d%s" % (len(flat), fmt), *flat)), "componentType": ctype, "count": len(rows),
             "type": typ}
        if minmax:
            a["min"] = [min(r[c] for r in rows) for c in range(len(rows[0]))]
            a["max"] = [max(r[c] for r in rows) for c in range(len(rows[0]))]
        j["accessors"].append(a)
        return len(j["accessors"]) - 1

    for img, corners in sorted(groups.items(), key=lambda kv: str(kv[0])):
        P = [c[0] for c in corners]
        N = [c[1] for c in corners]
        T = [c[2] for c in corners]
        prim = {"attributes": {"POSITION": acc("f", 5126, "VEC3", P, True), "NORMAL": acc("f", 5126, "VEC3", N),
                               "TEXCOORD_0": acc("f", 5126, "VEC2", T)}, "mode": 4}
        name, data = retarget.image_data(mdl, img) if img else (None, None)
        mt = "image/png" if data and data[:4] == b"\x89PNG" else "image/jpeg" if data and data[:3] == b"\xff\xd8\xff" else None
        if mt:
            j["images"].append({"mimeType": mt, "bufferView": view(data)})
            j["textures"].append({"source": len(j["images"]) - 1, "sampler": 0})
            j["materials"].append({"name": name, "pbrMetallicRoughness": {"baseColorTexture": {"index": len(j["textures"]) - 1},
                                                                           "metallicFactor": 0.0}})
        else:
            j["materials"].append({"pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.8, 0.8, 1.0], "metallicFactor": 0.0}})
        prim["material"] = len(j["materials"]) - 1
        j["meshes"][0]["primitives"].append(prim)
    j["buffers"].append({"byteLength": len(binb)})
    js = json.dumps(j, separators=(",", ":")).encode("utf-8")
    js += b" " * (-len(js) % 4)
    while len(binb) % 4:
        binb.append(0)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 28 + len(js) + len(binb)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(binb), 0x004E4942) + bytes(binb))
    return out_path


def preview_glb(mdl, front, out_path):
    """The model's rest mesh in the game's axes (front as retarget.build turns it), textured, as a .glb for the page."""
    Cm = retarget.eye() if front == "+y" else [[-1.0, 0, 0, 0], [0, -1.0, 0, 0], [0, 0, 1.0, 0], [0, 0, 0, 1.0]]
    groups = {}
    for img, cs in mdl.tris:
        g = groups.setdefault(img, [])
        for c in cs:
            g.append((retarget.apply(Cm, c[0]), retarget._unit(retarget.apply(Cm, c[1], 0.0)), c[2]))
    return _write_glb(mdl, groups, out_path)


class Bones:
    """The page's second step: the bone map (given, or tools/automap.py's) and the model at the rest the retarget gives it
    with that map, as a .glb plus its joints."""

    def __init__(self, src_glb, mdl, front, size=1.0, cfg=None):
        self.src = retarget.Source(src_glb)
        self.mdl = mdl
        self.front = front
        self.size = size
        self.cfg0 = cfg
        self.auto = None
        self.dir = tempfile.mkdtemp(prefix="wotm_bones_")
        self.n = 0
        self.lock = threading.Lock()
        # the monster's bones a joint can follow: the skinned transforms (bone<N>, with the body part in front when known)
        self.bone_names = sorted((nd.get("name") for nd in self.src.ed.nodes if re.search(r"(^|_)bone\d+$", nd.get("name") or "")),
                                 key=lambda s: int(re.search(r"\d+$", s).group()))

    def auto_map(self):
        if self.auto is None:
            import automap
            self.auto = automap.build_map(self.src.path, self.mdl, lambda *a: None)
        return json.loads(json.dumps(self.auto))

    def start_map(self):
        return json.loads(json.dumps(self.cfg0)) if self.cfg0 else self.auto_map()

    def preview(self, turn, cfg):
        """-> {"glb": path, "joints": [...]} of the model posed at rest by this map (turn: the step 1 turns)."""
        with self.lock:
            rest = {}
            retarget.build(self.src, self.mdl, cfg, None, size=self.size, front=self.front, turn=turn or None,
                           log=lambda *a: None, rest=rest)
            groups = {}
            for img, cs in rest["tris"]:
                groups.setdefault(img, []).extend((c[0], c[1], c[2]) for c in cs)
            self.n += 1
            out = _write_glb(self.mdl, groups, os.path.join(self.dir, "rest%d.glb" % self.n))
            return {"glb": out, "v": self.n, "joints": rest["joints"]}


def ask(base_glb, model_glb, info, log=print, open_page=webbrowser.open, timeout=1800, bones=None):
    """Serves the page and waits for its answer: {"turn": the turns ("" = as shown, e.g. "z90,x-90"), "cfg": the bone map
    (only with bones, a Bones)} or None (Cancel, closed or timed out). info: {"base": name, "model": name} for the page."""
    page, vendor = _assets()
    done = threading.Event()
    answer = {}
    info = dict(info, bones=bones is not None)
    last = {}

    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _send(self, code, body, ctype):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, obj):
            self._send(200, json.dumps(obj).encode("utf-8"), "application/json")

        def do_GET(self):
            p = self.path.split("?", 1)[0]
            if p in ("/", "/index.html"):
                return self._send(200, open(page, "rb").read(), "text/html; charset=utf-8")
            if p == "/info.json":
                return self._send(200, json.dumps(info).encode("utf-8"), "application/json")
            if p in ("/base.glb", "/model.glb"):
                return self._send(200, open(base_glb if p == "/base.glb" else model_glb, "rb").read(), "model/gltf-binary")
            if p == "/rest.glb" and last.get("glb"):
                return self._send(200, open(last["glb"], "rb").read(), "model/gltf-binary")
            if p.startswith("/vendor/"):
                f = os.path.normpath(os.path.join(vendor, *p[len("/vendor/"):].split("/")))
                if f.startswith(os.path.normpath(vendor)) and os.path.isfile(f):
                    return self._send(200, open(f, "rb").read(), "text/javascript" if f.endswith(".js") else "text/plain")
            self._send(404, b"not found", "text/plain")

        def do_POST(self):
            n = int(self.headers.get("Content-Length") or 0)
            try:
                d = json.loads(self.rfile.read(n) or b"{}")
            except ValueError:
                d = {}
            if self.path == "/done":
                answer["turn"] = d.get("turn") if d.get("ok") else None
                answer["cfg"] = d.get("cfg") if d.get("ok") else None
                answer["got"] = True
                self._send(200, b"{}", "application/json")
                done.set()
                return
            if bones is None or self.path not in ("/bones", "/preview"):
                return self._send(404, b"{}", "application/json")
            try:   # /bones: the starting map ("auto": true, the automatic one); /preview: the map the page edited
                cfg = d.get("cfg") if self.path == "/preview" else bones.auto_map() if d.get("auto") else bones.start_map()
                r = bones.preview(d.get("turn") or "", cfg)
            except (retarget.RetargetError, KeyError, ValueError, TypeError) as e:
                return self._json({"error": str(e)})
            except Exception as e:  # noqa: BLE001 (automap.MapError and the like: the page shows it and keeps the last map)
                return self._json({"error": "%s: %s" % (type(e).__name__, e)})
            last["glb"] = r["glb"]
            self._json({"cfg": cfg, "joints": r["joints"], "v": r["v"], "bones": bones.bone_names})

    srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    url = "http://127.0.0.1:%d/" % srv.server_address[1]
    log("set the model up in the page that opened (%s); OK there goes on" % url)
    try:
        open_page(url)
        done.wait(timeout)
    finally:
        srv.shutdown()
        srv.server_close()
    if not answer.get("got"):
        log("no answer from the page")
        return None
    if answer["turn"] is None:
        return None
    return {"turn": answer["turn"], "cfg": answer["cfg"]}


def main(argv):
    if len(argv) != 2:
        print(__doc__.split("\n\n")[2])
        return 2
    mdl = retarget.load_model(argv[1])
    front = "-y" if mdl.up_axis == "y" else "+y"
    pv = preview_glb(mdl, front, os.path.join(tempfile.gettempdir(), "wotm_orient_model.glb"))
    a = ask(argv[0], pv, {"base": os.path.basename(argv[0]), "model": os.path.basename(argv[1])},
            bones=Bones(argv[0], mdl, front))
    print("turn: %r" % (a and a["turn"]))
    if a and a["cfg"]:
        print(json.dumps(a["cfg"], indent=1))
    return 0 if a is not None else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
