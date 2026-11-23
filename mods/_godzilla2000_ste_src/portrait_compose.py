# portrait_compose.py <render rgba 512x512 do portrait_render.py> <saida portrait.png 512x256> [previa sem inverter.png]
# Retrato do HUD no estilo dos do disco: fundo preto, contorno branco com brilho, metade esquerda normal e direita
# "rage" (realces em laranja e vermelho); gravado de ponta-cabeca, como os do .RTX.
import sys
import numpy as np
from PIL import Image, ImageFilter

src, dst = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGBA").resize((256, 256), Image.LANCZOS)
a = np.asarray(im).astype(np.float32) / 255.0
rgb, al = a[..., :3], a[..., 3:4]

# contraste como nos retratos do disco
lum = (rgb * [0.299, 0.587, 0.114]).sum(-1, keepdims=True)
rgb = np.clip((rgb - 0.5) * 1.25 + 0.5 + 0.03, 0, 1)
sat = 1.15
g = (rgb * [0.299, 0.587, 0.114]).sum(-1, keepdims=True)
rgb = np.clip(g + (rgb - g) * sat, 0, 1)

# rage: tons de fogo pela luminancia (escuro -> vermelho -> laranja -> amarelo), misturados por cima
l = (rgb * [0.299, 0.587, 0.114]).sum(-1, keepdims=True)
fire = np.concatenate([np.clip(l * 1.9, 0, 1), np.clip(l * 0.75 - 0.05, 0, 1), np.clip(l * 0.35 - 0.1, 0, 1)], -1)
rage = np.clip(rgb * 0.6 + fire * 0.65, 0, 1)

# contorno branco: alpha dilatado e desfocado
mask = Image.fromarray((al[..., 0] * 255).astype(np.uint8))
dil = mask.filter(ImageFilter.MaxFilter(9))
halo = np.asarray(dil.filter(ImageFilter.GaussianBlur(5))).astype(np.float32)[..., None] / 255.0
edge = np.asarray(mask.filter(ImageFilter.MaxFilter(7))).astype(np.float32)[..., None] / 255.0
glow = np.clip(halo * 1.3, 0, 1) * 0.85
glow = np.maximum(glow, edge)

def over_black(c):
    base = np.ones_like(c) * glow  # branco do brilho sobre o preto
    return base * (1 - al) + c * al

L = over_black(rgb)
R = over_black(rage)
out = np.concatenate([L, R], 1)
img = Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8), "RGB")
if len(sys.argv) > 3:
    img.save(sys.argv[3])
img.transpose(Image.FLIP_TOP_BOTTOM).save(dst)
