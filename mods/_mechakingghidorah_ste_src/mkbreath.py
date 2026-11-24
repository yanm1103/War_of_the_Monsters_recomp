"""breathFx do Mecha-King Ghidorah: as camadas do raio (ghidorah_lightning) copiadas para as tres bocas + o acerto.
mkbreath.py <breathfx.json do ste_fx> <saida.json> (ex.: breathfx_gravitybeams.json)"""
import json, sys, copy
src = json.load(open(sys.argv[1]))
beam = [L for L in src['layers'] if L.get('at') != 'hit']
hit = [L for L in src['layers'] if L.get('at') == 'hit']
# bocas laterais em relacao a do meio na pose do disparo (TRIPLE_ENERGY_BEAM; beampose.py): (frente, lado, cima)
OFFS = [[0, 0, 0], [12.8, -19.2, -17.7], [13.4, 20.1, -34.5]]
layers = []
for o in OFFS:
    for L in beam:
        L = copy.deepcopy(L)
        if any(o):
            L['offset'] = o
        layers.append(L)
layers += hit
out = {'scale': 1.0, 'hide': [8, 51, 148, 53, 149, 80, 81], 'pitch': 0, 'aim': 45, 'layers': layers}
json.dump(out, open(sys.argv[2], 'w'), indent=2)
print(len(layers), 'camadas')
