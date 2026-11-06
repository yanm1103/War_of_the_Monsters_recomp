"""CJ's aiming reticles from GTA SA's, as PNGs of the reticle textures in his own SHOGUN0.TEX.
  python -I cjreticle.py [GTA dir] [out dir]   (default out: ../_work/CJ/monsters/cj/files)

The reticle (Monster::m_reticleCS, attachment id type + 5) is a SWITCH on m_reticleState: 0 = texture 13 (green), 1 = texture 5
(yellow), 2 = nothing on Ultra-V; the sticky reticle on a locked target (id type + 10) is texture 5 too (green). They are
additive sprites tinted by the vertex color, so the texture is a gray 128 with the shape in the alpha (as the game's).
- 13 (free aim) <- hud.txd siteM16: GTA draws that quarter mirrored four times around the center.
- 5 (on a target) <- GTA's lock-on: three triangles around the target pointing in (static here).
- 24 (locked, the special's spinning red ring) <- particle.txd lockonFire, the heat-seeker's ring. Texture 24 is the copy of
  Robo-47's texture 18 that tools/ngpgraft.py puts in the .TEX with the locked reticle's sprite (its log: "textures {18: 24}").
"""
import os
import sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rw  # noqa: E402

GRAY = 128
SIZE = 128


def txd_image(path, name):
    for nm, (w, h, rgba, _fmt) in rw.read_txd(path).items():
        if nm.lower() == name.lower():
            return Image.frombytes("RGBA", (w, h), bytes(rgba))
    raise SystemExit("no %s in %s" % (name, path))


def shape(alpha):
    """Gray 128 with that alpha (L image)."""
    return Image.merge("RGBA", [Image.new("L", alpha.size, GRAY)] * 3 + [alpha])


def additive_alpha(im):
    """Additive in the game: black adds nothing, so the shape goes by alpha, dimmer where GTA's texture is dark."""
    px = im.load()
    a = Image.new("L", im.size)
    pa = a.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b, al = px[x, y]
            pa[x, y] = int(al * (0.45 + 0.55 * max(r, g, b) / 255.0))
    return a


def free_aim(gta):
    q = txd_image(os.path.join(gta, "models", "hud.txd"), "siteM16")   # top-left quarter, center at its bottom-right
    a = additive_alpha(q)
    h = q.size[0]
    full = Image.new("L", (2 * h, 2 * h))
    full.paste(a, (0, 0))
    full.paste(a.transpose(Image.FLIP_LEFT_RIGHT), (h, 0))
    full.paste(a.transpose(Image.FLIP_TOP_BOTTOM), (0, h))
    full.paste(a.transpose(Image.ROTATE_180), (h, h))
    return shape(full.resize((SIZE, SIZE), Image.LANCZOS))


def locked():
    import math
    k = 4   # supersampling
    n = SIZE * k
    a = Image.new("L", (n, n))
    d = ImageDraw.Draw(a)
    c = n / 2.0
    for i in range(3):
        ang = math.radians(90 + 120 * i)   # one on top
        ux, uy = math.cos(ang), -math.sin(ang)
        tip, base, half = 0.20 * n, 0.46 * n, 0.11 * n   # tip near the center, base near the edge
        px, py = -uy, ux
        pts = [(c + ux * tip, c + uy * tip),
               (c + ux * base + px * half, c + uy * base + py * half),
               (c + ux * base - px * half, c + uy * base - py * half)]
        d.polygon(pts, fill=255)
    return shape(a.resize((SIZE, SIZE), Image.LANCZOS))


def main():
    gta = sys.argv[1] if len(sys.argv) > 1 else r"C:\Games\GTA San Andreas 1.0"
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "_work", "CJ", "monsters", "cj", "files")
    free_aim(gta).save(os.path.join(out, "shogun0_t0013.png"))
    locked().save(os.path.join(out, "shogun0_t0005.png"))
    ring = txd_image(os.path.join(gta, "models", "particle.txd"), "lockonFire")
    shape(additive_alpha(ring).resize((SIZE, SIZE), Image.LANCZOS)).save(os.path.join(out, "shogun0_t0024.png"))
    print("wrote shogun0_t0013.png (free aim), shogun0_t0005.png (on a target), shogun0_t0024.png (locked) in", out)


if __name__ == "__main__":
    main()
