"""Logo tipografico 512x256 (transparente) do Mechagodzilla 3: letras de aco azulado com contorno escuro e brilho."""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

out = sys.argv[1]
font_path = sys.argv[2]
W, H = 512, 256
text1, text2 = 'MECHAGODZILLA', '3'
# caixa de 80% x 60%
bw, bh = int(W * 0.8), int(H * 0.6)

big = 4
f1 = ImageFont.truetype(font_path, 100 * big)
f2 = ImageFont.truetype(font_path, 100 * big)


def tsize(t, f):
    b = f.getbbox(t)
    return b[2] - b[0], b[3] - b[1], b


w1, h1, b1 = tsize(text1, f1)
w2, h2, b2 = tsize(text2, f2)
gap = 14 * big
# duas linhas: MECHAGODZILLA e um 3 grande ao lado? uma linha so: "MECHAGODZILLA 3"
lines = ['MECHA', 'GODZILLA 3']
ws = [tsize(t, f1)[0] for t in lines]
hl = tsize('MECHAGODZILLA', f1)[1]
lead = int(hl * 0.18)
scale = min(bw * big / max(ws), bh * big / (2 * hl + lead))
f = ImageFont.truetype(font_path, int(100 * big * scale))
hl = tsize('MECHAGODZILLA', f)[1]
lead = int(hl * 0.18)
total_h = 2 * hl + lead
mask = Image.new('L', (W * big, H * big), 0)
d = ImageDraw.Draw(mask)
wf, hf = max(tsize(t, f)[0] for t in lines), total_h
for i, t in enumerate(lines):
    wt, ht, bt = tsize(t, f)
    x = (W * big - wt) // 2 - bt[0]
    y = (H * big - total_h) // 2 + i * (hl + lead) - tsize('MECHAGODZILLA', f)[2][1]
    d.text((x, y), t, font=f, fill=255)

# degrade de aco: claro em cima, azul escuro embaixo
ys = np.linspace(0, 1, H * big)[:, None]
top = np.array([226, 238, 252], float)
mid = np.array([120, 160, 214], float)
bot = np.array([34, 62, 120], float)
t = np.clip(ys * 1.0, 0, 1)
col = np.where(t < 0.5, top + (mid - top) * (t / 0.5), mid + (bot - mid) * ((t - 0.5) / 0.5))
rgb = np.repeat(col[:, None, :], W * big, axis=1).reshape(H * big, W * big, 3)
fill = Image.fromarray(rgb.astype(np.uint8), 'RGB')

# contorno escuro e brilho externo azul
m_np = np.asarray(mask)
outline = mask.filter(ImageFilter.MaxFilter(2 * big + 1))
glow = mask.filter(ImageFilter.MaxFilter(6 * big + 1)).filter(ImageFilter.GaussianBlur(5 * big))
canvas = Image.new('RGBA', (W * big, H * big), (0, 0, 0, 0))
g = Image.new('RGBA', canvas.size, (80, 160, 255, 0))
g.putalpha(glow.point(lambda v: int(v * 0.55)))
canvas = Image.alpha_composite(canvas, g)
o = Image.new('RGBA', canvas.size, (14, 22, 48, 255))
o.putalpha(outline)
canvas = Image.alpha_composite(canvas, o)
fl = fill.convert('RGBA')
fl.putalpha(mask)
canvas = Image.alpha_composite(canvas, fl)
canvas = canvas.resize((W, H), Image.LANCZOS)
canvas.save(out)
print('logo', out, 'texto', wf // big, 'x', hf // big)
