#!/usr/bin/env python3
"""Sets the animation of one slot of a monster's punch table (`.CHR`, key 0xFCC), in place.

  python tools/chr_punch.py <file.CHR> <pos> <type> <animation id>
  python tools/chr_punch.py mods/_work/my_mod/monsters/fire_togera/files/LIZARD.CHR 1 0 299

`pos` and `type` as `tools/chr_dump.py` prints them (pos 1..10, type 0..5). The slot plays `Monster::m_anims[id]`: an animation
of the model (the base type's, or one added with Import animations, `tools/ngpanim.py`). Only for a custom monster's own `.CHR`
(`files/`, MODS.md): never the disc's. Layout: docs/FORMATS.md ("Character").
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chr_dump  # noqa: E402

MAX_ANIM = 299   # Monster::m_anims has one handle per id, 0..0x12B


def main(argv):
    if len(argv) != 5:
        print(__doc__.strip().splitlines()[2])
        return 2
    path, pos, typ, anim = argv[1], int(argv[2]), int(argv[3]), int(argv[4])
    if not (1 <= pos <= chr_dump.TIERS and 0 <= typ < chr_dump.TYPES):
        print("error: pos 1..%d, type 0..%d" % (chr_dump.TIERS, chr_dump.TYPES - 1))
        return 1
    if not 0 < anim <= MAX_ANIM:
        print("error: animation ids go from 1 to %d" % MAX_ANIM)
        return 1
    d = bytearray(open(path, "rb").read())
    keys = {k: (o, s) for k, o, s in chr_dump.read_keys(d)}
    if chr_dump.PUNCH_KEY not in keys or keys[chr_dump.PUNCH_KEY][1] != chr_dump.PUNCH_KEY_SIZE:
        print("error: %s has no punch table (key 0x%X)" % (path, chr_dump.PUNCH_KEY))
        return 1
    a = keys[chr_dump.PUNCH_KEY][0] + 0x10 + (pos - 1) * 0x3C0 + typ * 0xA0 + 0x28
    old = struct.unpack_from("<i", d, a)[0]
    struct.pack_into("<i", d, a, anim)
    with open(path, "wb") as f:
        f.write(d)
    print("pos %d type %d: animation %d -> %d" % (pos, typ, old, anim))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
