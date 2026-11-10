"""Animacoes de Godzilla: Save the Earth (PS2) como `clips.json` do `tools/retarget.py --own`: o corpo do STE move-se com os
movimentos que o STE deu a ele, no lugar dos do Togera.

  python tools/ste/ste_clips.py <iso> <Godzilla2k.cmp> <clips.json> [--ids 90,1,2] [--list]

Cada clipe e `a<id>` (id de animacao do WotM, docs/ANIM_IDS.md), 60 quadros por segundo, com o mundo de cada junta na cena do
.dae que `export_dae.py` grava (mesmos eixos, mesmas unidades, mesmos nomes de junta). `ID_MAP` diz que animacao do STE vira
cada id; o ciclo (andar, correr, idle) amostra [0, 1) para o laco fechar sem repetir o quadro.
"""
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ste_model as S
import skeleton as K
import anim as A

FPS = 60.0

LAID_UP = {'turn': [90, 0, 0], 'shift': [0, 62.3, 15.1]}        # de costas (80, 86)
LAID_DOWN = {'turn': [-90, 0, 0], 'shift': [0, -63.3, 17.5]}    # de bruços (81, 85)

# id do WotM -> (nome da animacao no STE, ciclo?, duracao em s ou None = a do STE[, opcoes]). So entra o que foi conferido olhando.
# Opcoes: 'laid' (retarget --own), 'still' (2 quadros iguais, o 1o do clipe), 'settle' (tira a deriva da raiz de um clipe unico).
ID_MAP = {
    90: ('NORMAL_IDLE', True, None),
    1: ('WALK', True, None),
    2: ('RUN', True, None),
    92: ('TAUNT', False, None),
    130: ('VICTORY', False, None),
    131: ('ROAR', False, None),
    142: ('STUNNED', True, None),       # tonto (estrelas): fecha o ciclo
    74: ('TAZED', True, None),          # chocado: tremor curto que fecha o ciclo
    60: ('BLOCK', True, None),          # bloqueio: guarda que fecha o ciclo
    160: ('ATOMICBREATH_CHARGE_FULL', False, 1.2),   # especial, preparo: o STE leva 3,3 s; termina perto da pose do sopro
    161: ('ATOMICBREATH_FIRE', True, None),          # especial, sopro: fecha o ciclo (162, o fim do sopro, fica com a base)
    # reacoes a dano: o jogo estica a animacao ao tempo do tranco (StateRecoil::transitionInto chama animationSetTotalRunFrames).
    # O lado e o do movimento da cabeca no STE (+x = direita do monstro), nao o do nome da animacao.
    70: ('JERKBACK_B_BODY', False, None),   # golpe de frente: empurrado para tras (e a reacao padrao)
    69: ('STAGGER_F_BODY', False, None),    # golpe por tras: empurrado para a frente
    67: ('JERKBACK_R_HEAD', False, None),   # empurrado para +x
    68: ('JERKBACK_L_HEAD', False, None),   # empurrado para -x
    71: ('BLOCKHIT_F_HIGH_STAND', False, None),      # golpe bloqueado
    77: ('BLOCK_HEAVY_BODY_FORWARD', False, None),   # golpe forte bloqueado: empurrado para tras
    # caido: o jogo deita o monstro (gira a pose guardada em pe e a desloca); o clipe e a pose vista no chao e "laid" a desfaz.
    # Valores medidos no Togera: 1o quadro do levantar (87/88) contra o fim de 85/86 girado Rx(+-90), 31 ossos, desvio < 2.
    80: ('KOED_FACEUP_IDLE', False, None, {'laid': LAID_UP}),
    81: ('KOED_FACEDOWN_IDLE', False, None, {'laid': LAID_DOWN}),
    85: ('KOED_FACEDOWN_IDLE', False, None, {'laid': LAID_DOWN, 'still': True}),   # pouso, 2 quadros (o jogo estica a 60)
    86: ('KOED_FACEUP_IDLE', False, None, {'laid': LAID_UP, 'still': True}),
    87: ('KOED_FACEDOWN_GETUP', False, None, {'settle': True}),        # levantar de bruços (o jogo estica a 60 campos)
    88: ('KOED_FACEUP_GETUP_ATTACK', False, None, {'settle': True}),   # de costas: so esta comeca na pose deitada (o rolar nao encadeia)
    120: ('SLUMP', False, None),            # morte em pe: cai de frente e termina na pose de KOED_FACEDOWN_IDLE
}


def jname(n):
    return re.sub(r'[^A-Za-z0-9_]', '_', n)


def _seg(keys, u, cycle):
    """(i0, i1, a): segmento de chaves [t, ...] que contem u. Depois da ultima chave o ciclo volta para a primeira."""
    n = len(keys)
    i0 = 0
    for k in range(1, n):
        if keys[k][0] <= u:
            i0 = k
        else:
            break
    t0 = keys[i0][0]
    if i0 == n - 1:
        if t0 >= 0.9999 or not cycle:
            return i0, i0, 0.0
        i1, t1 = 0, 1.0
    else:
        i1, t1 = i0 + 1, keys[i0 + 1][0]
    a = (u - t0) / (t1 - t0) if t1 > t0 else 0.0
    return i0, i1, min(1.0, max(0.0, a))


def sample_local(sk, an, u, cycle):
    """Translacao e quaternion (do disco) locais de cada no no instante u (fracao da duracao)."""
    n = len(sk['parent'])
    lt = [tuple(sk['t'][i]) for i in range(n)]
    lq = [tuple(sk['q'][i]) for i in range(n)]
    for i in range(n):
        tr, rot = an['tr'][i], an['rot'][i]
        if len(tr) == 1:
            lt[i] = tuple(tr[0][1:4])
        elif len(tr) > 1:
            i0, i1, a = _seg(tr, u, cycle)
            lt[i] = tuple(tr[i0][1 + k] * (1 - a) + tr[i1][1 + k] * a for k in range(3))
        if len(rot) == 1:
            lq[i] = tuple(rot[0][1:5])
        elif len(rot) > 1:
            i0, i1, a = _seg(rot, u, cycle)
            q0, q1 = rot[i0][1:5], rot[i1][1:5]
            s = -1.0 if sum(x * y for x, y in zip(q0, q1)) < 0 else 1.0
            lq[i] = tuple(q0[k] * (1 - a) + s * q1[k] * a for k in range(4))
    return lt, lq


def _quat(R):
    """Quaternion (x, y, z, w) de uma matriz de rotacao 3x3."""
    t = R[0][0] + R[1][1] + R[2][2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        q = [(R[2][1] - R[1][2]) / s, (R[0][2] - R[2][0]) / s, (R[1][0] - R[0][1]) / s, 0.25 * s]
    elif R[0][0] > R[1][1] and R[0][0] > R[2][2]:
        s = math.sqrt(1.0 + R[0][0] - R[1][1] - R[2][2]) * 2
        q = [0.25 * s, (R[0][1] + R[1][0]) / s, (R[0][2] + R[2][0]) / s, (R[2][1] - R[1][2]) / s]
    elif R[1][1] > R[2][2]:
        s = math.sqrt(1.0 + R[1][1] - R[0][0] - R[2][2]) * 2
        q = [(R[0][1] + R[1][0]) / s, 0.25 * s, (R[1][2] + R[2][1]) / s, (R[0][2] - R[2][0]) / s]
    else:
        s = math.sqrt(1.0 + R[2][2] - R[0][0] - R[1][1]) * 2
        q = [(R[0][2] + R[2][0]) / s, (R[1][2] + R[2][1]) / s, 0.25 * s, (R[1][0] - R[0][1]) / s]
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return [c / n for c in q]


def world_pose(sk, lt, lq):
    """Mundo de cada no: (R, t). A rotacao do disco vale conjugada (transposta), como em skeleton.world."""
    n = len(sk['parent'])
    out = [None] * n

    def get(i):
        if out[i] is not None:
            return out[i]
        R = K.qmat(lq[i])
        R = [[R[j][c] for j in range(3)] for c in range(3)]
        t = lt[i]
        p = sk['parent'][i]
        if p < 0:
            out[i] = (R, tuple(t))
        else:
            Rp, tp = get(p)
            Rw = [[sum(Rp[a][k] * R[k][c] for k in range(3)) for c in range(3)] for a in range(3)]
            tw = tuple(tp[a] + sum(Rp[a][k] * t[k] for k in range(3)) for a in range(3))
            out[i] = (Rw, tw)
        return out[i]
    for i in range(n):
        get(i)
    return out


def make_clip(sk, an, wid, cycle, seconds=None, opts=None):
    opts = opts or {}
    frames = max(2, int(round((seconds or an['dur']) * FPS)))
    if not cycle:
        frames += 1
    if opts.get('still'):
        frames = 2
    names = [jname(x) for x in sk['name']]
    joints = {nm: [] for nm in names}
    prev = [None] * len(names)
    root = sk['parent'].index(-1)
    drift = (0.0, 0.0)
    if cycle or opts.get('settle'):   # o STE anda o monstro pela raiz; no WotM quem anda e o jogo: tira a deriva horizontal
        p0 = world_pose(sk, *sample_local(sk, an, 0.0, cycle))[root][1]
        p1 = world_pose(sk, *sample_local(sk, an, 1.0, cycle))[root][1]
        drift = (p1[0] - p0[0], p1[1] - p0[1])
    for f in range(frames):
        u = 0.0 if opts.get('still') else f / float(frames if cycle else frames - 1)
        lt, lq = sample_local(sk, an, u, cycle)
        W = world_pose(sk, lt, lq)
        for i, nm in enumerate(names):
            R, t = W[i]
            t = (t[0] - drift[0] * u, t[1] - drift[1] * u, t[2])
            q = _quat(R)
            if prev[i] is not None and sum(a * b for a, b in zip(q, prev[i])) < 0:   # mesmo lado: sem salto de sinal
                q = [-c for c in q]
            prev[i] = q
            joints[nm].append([round(c, 6) for c in q] + [round(c, 4) for c in t])
    if cycle:   # e centra o balanco em volta da origem do monstro (a raiz anda de um lado a outro, nao so em volta do zero)
        r = joints[names[root]]
        mx, my = sum(k[4] for k in r) / frames, sum(k[5] for k in r) / frames
        for ks in joints.values():
            for k in ks:
                k[4] = round(k[4] - mx, 4)
                k[5] = round(k[5] - my, 4)
    clip = {'name': 'a%d' % wid, 'frames': frames, 'joints': joints}
    if opts.get('laid'):
        clip['laid'] = opts['laid']
    return clip


def load(iso, fn):
    vol = S.Vol(S.Iso(iso))
    b = S.Bundle(vol.get(fn))
    sk = K.parse_skeleton(b, b.meshes()[0])
    return b, sk, A.parse_animations(b, sk['name'])


def main(argv):
    if len(argv) < 3 and '--list' not in argv:
        print(__doc__)
        return 2
    iso, fn = argv[0], argv[1]
    b, sk, anims = load(iso, fn)
    if '--list' in argv:
        for a in anims:
            print('%-34s %5.2fs' % (a.get('name'), a['dur']))
        return 0
    out = argv[2]
    ids = sorted(ID_MAP)
    table = dict(ID_MAP)
    if '--ids' in argv:
        ids = [int(x) for x in argv[argv.index('--ids') + 1].split(',')]
    if '--as' in argv:   # pre-visualizacao: ID=NOME,ID=NOME poe qualquer animacao do STE num id (uma vez), sem mexer no ID_MAP
        ids = []
        for it in argv[argv.index('--as') + 1].split(','):
            k, v = it.split('=')
            table[int(k)] = (v, False, None)
            ids.append(int(k))
    byname = {a.get('name'): a for a in anims if a.get('name')}
    clips = []
    for wid in ids:
        nm, cycle, secs = table[wid][:3]
        clips.append(make_clip(sk, byname[nm], wid, cycle, secs, table[wid][3] if len(table[wid]) > 3 else None))
        print('a%d <- %s (%d quadros, %s)' % (wid, nm, clips[-1]['frames'], 'ciclo' if cycle else 'uma vez'))
    with open(out, 'w', encoding='utf-8') as f:
        json.dump({'clips': clips}, f, separators=(',', ':'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
