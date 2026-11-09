"""A binary glTF of a box turned inside out (faces toward its middle), the shape of a sky model: for test_sky.mjs.

python tools/level_editor/make_sky_glb.py OUT.glb HALF MATERIAL

HALF: half the box's side, in meters (Blender, Y up). MATERIAL: the material's name; "tex240" is the stage's own picture 240
(lvlprop.py), any other name is a plain color.
"""
import json
import struct
import sys


def sky_box(half, material):
    # +X, -X, +Y, -Y, +Z, -Z faces, each as four corners; the winding is the one that looks at the middle
    faces = [((1, -1, -1), (1, 1, -1), (1, 1, 1), (1, -1, 1)), ((-1, -1, 1), (-1, 1, 1), (-1, 1, -1), (-1, -1, -1)),
             ((-1, 1, -1), (-1, 1, 1), (1, 1, 1), (1, 1, -1)), ((-1, -1, 1), (-1, -1, -1), (1, -1, -1), (1, -1, 1)),
             ((-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)), ((1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1))]
    pos, uv, idx = [], [], []
    for f in faces:
        b = len(pos)
        pos += [tuple(c * half for c in p) for p in f]
        uv += [(0.0, 1.0), (0.0, 0.0), (1.0, 0.0), (1.0, 1.0)]
        idx += [b, b + 2, b + 1, b, b + 3, b + 2]
    col = [(1.0, 1.0, 1.0, 1.0)] * len(pos)
    blob, views, accs = b"", [], []

    def add(data, ctype, count, typ, target, mm=None):
        views.append({"buffer": 0, "byteOffset": len(blob_ref[0]), "byteLength": len(data), "target": target})
        blob_ref[0] += data + bytes((-len(data)) % 4)
        acc = {"bufferView": len(views) - 1, "componentType": ctype, "count": count, "type": typ}
        if mm:
            acc["min"], acc["max"] = mm
        accs.append(acc)
        return len(accs) - 1

    blob_ref = [blob]
    p = add(struct.pack("<%df" % (3 * len(pos)), *[c for q in pos for c in q]), 5126, len(pos), "VEC3", 34962,
            ([min(q[i] for q in pos) for i in range(3)], [max(q[i] for q in pos) for i in range(3)]))
    t = add(struct.pack("<%df" % (2 * len(uv)), *[c for q in uv for c in q]), 5126, len(uv), "VEC2", 34962)
    c = add(struct.pack("<%df" % (4 * len(col)), *[x for q in col for x in q]), 5126, len(col), "VEC4", 34962)
    i = add(struct.pack("<%dH" % len(idx), *idx), 5123, len(idx), "SCALAR", 34963)
    blob = blob_ref[0]
    js = json.dumps({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": "sky"}],
                     "materials": [{"name": material, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]}}],
                     "meshes": [{"primitives": [{"attributes": {"POSITION": p, "TEXCOORD_0": t, "COLOR_0": c}, "indices": i, "material": 0}]}],
                     "accessors": accs, "bufferViews": views, "buffers": [{"byteLength": len(blob)}]}).encode()
    js += b" " * ((-len(js)) % 4)
    return (struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(js) + 8 + len(blob)) + struct.pack("<I4s", len(js), b"JSON") + js
            + struct.pack("<I4s", len(blob), b"BIN\0") + blob)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(2)
    with open(sys.argv[1], "wb") as fh:
        fh.write(sky_box(float(sys.argv[2]), sys.argv[3]))
