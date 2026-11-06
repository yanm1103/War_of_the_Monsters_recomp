"""GTA SA animation packages (.ifp, ANP3): read the clips.
  python ifp.py <file.ifp> [substring]          list the clips (name, objects, length)
  python ifp.py <file.ifp> show <clip>          the objects of one clip (bone id, keys)
A clip: {name: {"objects": [{"name", "bone", "root", "keys": [(t, (x, y, z, w), (tx, ty, tz) | None)]}]}}.
Quaternions are int16 / 4096, translations int16 / 1024, times int16 / 60 (seconds).
"""
import struct
import sys


def read_ifp(path):
    return parse_ifp(open(path, "rb").read())


def parse_ifp(b):
    assert b[:4] == b"ANP3", b[:4]
    n = struct.unpack_from("<I", b, 32)[0]
    o = 36
    clips = {}
    order = []
    for _ in range(n):
        name = b[o:o + 24].split(b"\0")[0].decode("latin-1")
        nobj, _size, _flags = struct.unpack_from("<III", b, o + 24)
        o += 36
        objs = []
        for _k in range(nobj):
            oname = b[o:o + 24].split(b"\0")[0].decode("latin-1")
            ftype, nf, bone = struct.unpack_from("<IIi", b, o + 24)
            o += 36
            root = ftype == 4
            keys = []
            for _f in range(nf):
                if root:
                    qx, qy, qz, qw, t, tx, ty, tz = struct.unpack_from("<8h", b, o)
                    o += 16
                    keys.append((t / 60.0, (qx / 4096.0, qy / 4096.0, qz / 4096.0, qw / 4096.0),
                                 (tx / 1024.0, ty / 1024.0, tz / 1024.0)))
                else:
                    assert ftype == 3, (name, oname, ftype)
                    qx, qy, qz, qw, t = struct.unpack_from("<5h", b, o)
                    o += 10
                    keys.append((t / 60.0, (qx / 4096.0, qy / 4096.0, qz / 4096.0, qw / 4096.0), None))
            objs.append({"name": oname, "bone": bone, "root": root, "keys": keys})
        clips[name.lower()] = {"name": name, "objects": objs}
        order.append(name.lower())
    return clips, order


def length(clip):
    return max((o["keys"][-1][0] for o in clip["objects"] if o["keys"]), default=0.0)


def main():
    clips, order = read_ifp(sys.argv[1])
    if len(sys.argv) > 3 and sys.argv[2] == "show":
        c = clips[sys.argv[3].lower()]
        for o in c["objects"]:
            print("%-20s bone %5d %s keys %3d  t %.3f..%.3f" % (o["name"], o["bone"], "root" if o["root"] else "    ",
                                                               len(o["keys"]), o["keys"][0][0], o["keys"][-1][0]))
        return
    sub = sys.argv[2].lower() if len(sys.argv) > 2 else ""
    for k in order:
        if sub in k:
            c = clips[k]
            print("%-28s obj %2d  %.2f s" % (c["name"], len(c["objects"]), length(c)))


if __name__ == "__main__":
    main()
