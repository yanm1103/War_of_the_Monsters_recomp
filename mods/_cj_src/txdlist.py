"""List a .txd's textures; with an out dir, export the ones whose name contains a filter as PNG.
  python txdlist.py file.txd [outdir filter]"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw  # noqa: E402

t = rw.read_txd(sys.argv[1])
for name, (w, h, rgba, fmt) in t.items():
    print("%-20s %4dx%-4d %s" % (name, w, h, fmt))
    if len(sys.argv) > 3 and sys.argv[3].lower() in name.lower():
        os.makedirs(sys.argv[2], exist_ok=True)
        rw.write_png(os.path.join(sys.argv[2], name + ".png"), w, h, rgba)
