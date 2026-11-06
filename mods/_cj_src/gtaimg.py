"""GTA SA .img (VER2): list or extract entries.
  python gtaimg.py <file.img> list [substring]
  python gtaimg.py <file.img> get <outdir> name1 name2 ...
"""
import os
import struct
import sys


def entries(path):
    with open(path, "rb") as f:
        hdr = f.read(8)
        assert hdr[:4] == b"VER2", hdr
        n = struct.unpack_from("<I", hdr, 4)[0]
        tab = f.read(32 * n)
    out = []
    for i in range(n):
        off, ssz, asz = struct.unpack_from("<IHH", tab, 32 * i)
        name = tab[32 * i + 8:32 * i + 32].split(b"\0")[0].decode("latin-1")
        out.append((name, off * 2048, (ssz or asz) * 2048))
    return out


def main():
    path, cmd = sys.argv[1], sys.argv[2]
    es = entries(path)
    if cmd == "list":
        sub = sys.argv[3].lower() if len(sys.argv) > 3 else ""
        for name, off, size in es:
            if sub in name.lower():
                print("%-24s %8d" % (name, size))
    elif cmd == "get":
        outdir = sys.argv[3]
        os.makedirs(outdir, exist_ok=True)
        want = {w.lower() for w in sys.argv[4:]}
        with open(path, "rb") as f:
            for name, off, size in es:
                if name.lower() in want or "*" in want:
                    f.seek(off)
                    with open(os.path.join(outdir, name), "wb") as g:
                        g.write(f.read(size))
                    print("got", name, size)


main()
