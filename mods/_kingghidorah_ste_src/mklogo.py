"""Logo tipografico 512x256 (transparente) do King Ghidorah: letras douradas com contorno escuro e brilho, em duas linhas
(KING pequeno em cima, GHIDORAH grande), na caixa de 80% x 60% dos logos do jogo. Provisorio ate haver uma arte do logo.

  python mklogo.py <saida.png> <fonte .ttf>
"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

out, font_path = sys.argv[1], sys.argv[2]
W, H = 512, 256
bw, bh = int(W * 0.8), int(H * 0.6)
big = 4


def tsize(t, f):
    b = f.getbbox(t)
    return b[2] - b[0], b[3] - b[1], b


# GHIDORAH ocupa a largura; KING com 55% da altura dele
base = ImageFont.truetype(font_path, 100 * big)
w2, h2, _ = tsize('GHIDORAH', base)
lead = 0.14
scale = min(bw * big / w2, bh * big / (h2 * (1 + 0.55 + lead)))
f2 = ImageFont.truetype(font_path, int(100 * big * scale))
f1 = ImageFont.truetype(font_path, int(100 * big * scale * 0.55))
lines = [('KING', f1), ('GHIDORAH', f2)]
hs = [tsize(t, f)[1] for t, f in lines]
gap = int(hs[1] * lead)
total = hs[0] + gap + hs[1]
mask = Image.new('L', (W * big, H * big), 0)
d = ImageDraw.Draw(mask)
y = (H * big - total) // 2
for (t, f), h in zip(lines, hs):
    wt, ht, bt = tsize(t, f)
    d.text(((W * big - wt) // 2 - bt[0], y - bt[1]), t, font=f, fill=255)
    y += h + gap

# degrade dourado: claro em cima, ouro escuro embaixo
ys = np.linspace(0, 1, H * big)[:, None]
top = np.array([255, 246, 190], float)
mid = np.array([240, 190, 60], float)
bot = np.array([150, 90, 20], float)
t = np.clip((ys - 0.2) / 0.6, 0, 1)
col = np.where(t < 0.5, top + (mid - top) * (t / 0.5), mid + (bot - mid) * ((t - 0.5) / 0.5))
rgb = np.repeat(col[:, None, :], W * big, axis=1).reshape(H * big, W * big, 3)
fill = Image.fromarray(rgb.astype(np.uint8), 'RGB')

outline = mask.filter(ImageFilter.MaxFilter(2 * big + 1))
glow = mask.filter(ImageFilter.MaxFilter(6 * big + 1)).filter(ImageFilter.GaussianBlur(5 * big))
canvas = Image.new('RGBA', (W * big, H * big), (0, 0, 0, 0))
g = Image.new('RGBA', canvas.size, (255, 200, 60, 0))
g.putalpha(glow.point(lambda v: int(v * 0.55)))
canvas = Image.alpha_composite(canvas, g)
o = Image.new('RGBA', canvas.size, (48, 26, 6, 255))
o.putalpha(outline)
canvas = Image.alpha_composite(canvas, o)
fl = fill.convert('RGBA')
fl.putalpha(mask)
canvas = Image.alpha_composite(canvas, fl)
canvas.resize((W, H), Image.LANCZOS).save(out)
print('logo', out)
