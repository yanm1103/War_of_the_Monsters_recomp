"""breathFx do Mecha-King Ghidorah: as camadas do raio (ghidorah_lightning) copiadas para as tres bocas + o acerto.
mkbreath.py <breathfx.json do ste_fx> <saida.json> (ex.: breathfx_gravitybeams.json)"""
import json, sys, copy
src = json.load(open(sys.argv[1]))
beam = [L for L in src['layers'] if L.get('at') != 'hit']
hit = [L for L in src['layers'] if L.get('at') == 'hit']
# a boca do meio e a do jogo; as laterais saem dos localizadores das cabecas (hand_l = a esquerda, hand_r = a direita: o jogo os
# atualiza a cada quadro e as cabecas mexem), com "anchor". Na pose do disparo (TRIPLE_ENERGY_BEAM) elas ficam, em relacao a do meio
# (frente, lado, cima), em (12.8, -19.2, -17.7) e (13.4, 20.1, -34.5)
ANCHORS = [None, 'hand_l', 'hand_r']
layers = []
for a in ANCHORS:
    for L in beam:
        L = copy.deepcopy(L)
        if a:
            L['anchor'] = a
        layers.append(L)
layers += hit
out = {'scale': 1.0, 'hide': [8, 51, 148, 53, 149, 80, 81], 'pitch': 0, 'aim': 45, 'layers': layers}
json.dump(out, open(sys.argv[2], 'w'), indent=2)
print(len(layers), 'camadas')
