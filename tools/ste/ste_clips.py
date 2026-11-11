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
    # pulo: o STE tem um unico JUMP_UP com o arco inteiro; o WotM divide em fases (StateJump) e a altura vem da fisica, entao cada
    # fase e um trecho do JUMP_UP (u = fracao da animacao) e a subida da raiz encolhe para ~0,43 (o Togera embute +40 de altura).
    30: ('JUMP_UP', False, 20 / 60.0, {'segs': [(0.50, 0.63, 0.55), (0.0, 0.10, 0.45)], 'fade': 0.25, 'zscale': 0.43}),   # agacha e impulsiona
    33: ('JUMP_UP', True, 40 / 60.0, {'segs': [(0.12, 0.30, 1.0)], 'pingpong': True, 'zscale': 0.43}),                    # subindo (camada)
    34: ('JUMP_UP', True, 40 / 60.0, {'segs': [(0.30, 0.40, 1.0)], 'pingpong': True, 'zscale': 0.43}),                    # descendo
    35: ('JUMP_UP', False, 0.5, {'segs': [(0.38, 0.62, 1.0)], 'zscale': 0.43}),    # impacto do pouso (camada): do ar ao agachado
    39: ('JUMP_UP', False, 0.5, {'segs': [(0.62, 0.96, 1.0)]}),                    # recupera o pouso: do agachado a pe
    # objetos: o jogo agarra o objeto entre 80% e 95% do pegar (StatePickUp, estica a 30 quadros) e solta no arremesso em
    # 45% (StateThrow, estica a 60), 35% (2 maos, 0x37), 38% (monstro) e 50% (dardo): o gesto do STE e deformado para que a
    # mao chegue ao chao / passe a velocidade maxima nesse ponto (knots = [(saida, entrada)]).
    41: ('1HAND_PICKUP', False, 0.5, {'settle': True, 'knots': [(0, 0), (0.87, 0.38), (1, 0.60)]}),            # pegar do chao (1 mao)
    42: ('LIFT2HAND', False, 0.5, {'settle': True, 'knots': [(0, 0), (0.87, 0.42), (1, 0.60)]}),               # pegar objeto grande (2 maos)
    49: ('CATCH_1HAND', False, 0.5, {'settle': True}),                                                     # pegar no ar
    52: ('1HAND_THROW', False, 1.0, {'settle': True, 'knots': [(0, 0), (0.45, 0.50), (1, 1)]}),                # jogar objeto de 1 mao (solta em 45%)
    54: ('1HAND_THROW', False, 0.4, {'settle': True, 'knots': [(0, 0), (0.50, 0.50), (1, 1)]}),                # dardo (solta em 50%)
    55: ('THROW_2HAND', False, 0.5, {'settle': True, 'knots': [(0, 0), (0.37, 0.25), (1, 1)]}),                # jogar com 2 maos no alvo (35-38%)
    56: ('THROW_2HAND_OBJECT', False, 40 / 60.0, {'settle': True, 'knots': [(0, 0), (0.37, 0.25), (1, 1)]}),   # jogar com 2 maos para a frente
    # mais golpes e movimento: 13 e a mao esquerda do combo (slot 2.5 do .CHR); andar e correr de costas = o ciclo ao contrario;
    # o pisao acerta em 50% (StateStompAttack) e o STOMP tem os dois pes no chao em ~46%; os levantares atacando e a morte
    # deitada comecam na pose deitada do levantar (87/88), sem `laid` (o jogo nao inclina o monstro nesses).
    13: ('JAB_L', False, 0.4, {'strike': {'eff': 'hand_l', 'win': (0.55, 0.99)}}),
    7: ('WALK', True, None, {'reverse': True}),
    8: ('RUN', True, None, {'reverse': True}),
    240: ('STOMP', False, 0.5, {'settle': True, 'knots': [(0, 0), (0.5, 0.46), (1, 1)]}),
    241: ('STOMP', False, 0.5, {'settle': True, 'knots': [(0, 0), (0.5, 0.46), (1, 1)]}),
    242: ('KOED_FACEDOWN_GETUP_ATTACK', False, 50 / 60.0, {'settle': True}),
    243: ('KOED_FACEUP_GETUP_ATTACK', False, 50 / 60.0, {'settle': True}),
    121: ('KNOCKDOWN_FACEDOWN_FINISH', False, 2.0, {'knots': [(0, 0), (0.4, 1), (1, 1)]}),   # morte deitada de bruços: estrebucha e fica
    122: ('KNOCKDOWN_FACEUP_FINISH', False, 2.0, {'knots': [(0, 0), (0.3, 1), (1, 1)]}),
    # agarrar (StateGrapple: pega a vitima entre 50% e 60% do alcance; StateGrappleOHAttack: o golpe acerta em 50%): o LIFT
    # termina com as maos no alto e o DROP comeca ai, entao o golpe na vitima erguida e o DROP, com a batida da mao em 50%.
    170: ('GRAB', False, 0.5, {'settle': True, 'knots': [(0, 0), (0.55, 0.42), (1, 1)]}),     # esticar o braco para pegar
    185: ('GRAB', False, 20 / 60.0, {'settle': True, 'knots': [(0, 0), (0.55, 0.42), (1, 1)]}),   # agarrar no vazio
    171: ('LIFT', False, 0.5, {'settle': True}),                                              # erguer a vitima
    177: ('DROP', False, 0.5, {'settle': True, 'knots': [(0, 0), (0.5, 0.18), (1, 0.5)]}),    # bater com a vitima erguida
    186: ('DROP', False, 1.0, {'settle': True, 'knots': [(0, 0), (0.5, 0.18), (1, 0.5)]}),    # golpe final (especial)
    174: ('LIFTED', True, None),             # vitima: segurada no ar pelo agressor
    172: ('LIFTING_OBJECT', True, None),     # parado segurando objeto grande ou monstro
    173: ('WALK_LIFTING_OBJECT', True, None),   # andar/correr carregando
    # golpes: cada slot do Togera (.CHR, `tools/chr_dump.py disc/RST/LIZARD.CHR`) acerta com um localizador (0x803 mao D, 0x80B
    # boca, 0x818 a frente da cabeca, 0x816/0x817 meio e ponta da cauda) numa janela fixa da animacao; o golpe do STE com a mesma
    # parte do corpo entra no comprimento do slot, com o golpe dentro da janela (strike: eff = parte, win = janela do slot).
    10: ('JAB_R', False, 0.4, {'strike': {'eff': 'hand_r', 'win': (0.55, 0.99)}}),            # soco rapido
    22: ('UPPERCUT', False, 40 / 60.0, {'strike': {'eff': 'hand_r', 'win': (0.47, 0.85)}}),   # soco que lanca
    17: ('HMMR_FIN', False, 0.4, {'strike': {'eff': 'hand_r', 'win': (0.60, 0.99)}}),         # pancada de mao e ombro
    16: ('COMBO_BITE', False, 40 / 60.0, {'strike': {'eff': 'head', 'win': (0.44, 0.74), 'mode': 'abs'}}),   # mordida que atordoa
    18: ('COMBO_BITE', False, 0.4, {'strike': {'eff': 'head', 'win': (0.25, 0.85), 'mode': 'abs'}}),         # mordida
    19: ('COMBO_BITE', False, 0.4, {'strike': {'eff': 'head', 'win': (0.50, 0.99), 'mode': 'abs'}}),         # mordida
    15: ('HEADBUTT', False, 80 / 60.0, {'strike': {'eff': 'head', 'win': (0.20, 0.59), 'mode': 'abs'}}),     # cabecada
    12: ('TAIL_FULL_SPIN', False, 70 / 60.0, {'strike': {'eff': 'tail', 'win': (0.25, 0.48)}}),   # cauda forte (lanca)
    21: ('TAIL_SNAP', False, 50 / 60.0, {'strike': {'eff': 'tail', 'win': (0.22, 0.49)}}),    # cauda leve
    20: ('TAIL_REVERSAL', False, 50 / 60.0, {'strike': {'eff': 'tail', 'win': (0.01, 0.49)}}),   # cauda, acerta desde o comeco
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


EFFECTORS = {'hand_r': 'R Hand', 'hand_l': 'L Hand', 'head': 'Head', 'tail': None, 'foot_r': 'R Foot', 'foot_l': 'L Foot'}


def effector_index(sk, e):
    names = sk['name']
    if e == 'tail':
        return max(i for i, n in enumerate(names) if 'Tail' in n)
    return [i for i, n in enumerate(names) if n.endswith(EFFECTORS[e])][0]


def strike_knots(sk, an, strike, out_dur):
    """Deformacao do tempo de um golpe: [(u de saida, u de entrada)]. Acha o golpe do STE (o instante de maior velocidade da
    parte do corpo `strike['eff']` e o comeco do balanco, quando ela ainda andava a menos de 20% disso) e o poe dentro da janela
    de acerto do slot (`strike['win']`, fracao da animacao do Togera), a 30% dela, no ritmo original. Antes e depois, o tempo
    se estica para fechar o comprimento `out_dur`."""
    ei = effector_index(sk, strike['eff'])
    pel = [i for i, nm in enumerate(sk['name']) if nm.endswith('Pelvis')][0]
    n = max(20, int(an['dur'] * FPS))
    Ws = [world_pose(sk, *sample_local(sk, an, f / float(n), False)) for f in range(n + 1)]
    ps = [W[ei][1] for W in Ws]
    if strike.get('mode') == 'abs':   # a parte inteira (o corpo avanca com ela): velocidade no mundo
        sp = [sum((ps[f + 1][k] - ps[f][k]) ** 2 for k in range(3)) ** 0.5 for f in range(n)]
    else:   # velocidade para fora a partir da pelvis (o soco recua antes de ir: so o que sai conta)
        rr = [sum((Ws[f][ei][1][k] - Ws[f][pel][1][k]) ** 2 for k in range(3)) ** 0.5 for f in range(n + 1)]
        sp = [max(0.0, rr[f + 1] - rr[f]) for f in range(n)]
    c = max(range(n), key=lambda f: sp[f])
    a = max([f for f in range(c) if sp[f] < 0.2 * sp[c]] or [0])
    cu, au = (c + 0.5) / n, a / float(n)
    w0, w1 = strike['win']
    oc = w0 + 0.3 * (w1 - w0)
    oa = max(0.02, oc - (cu - au) * an['dur'] / out_dur)
    knots = [(0.0, 0.0)]
    if au > 0.01 and oa > 0.0 and oa < oc:
        knots.append((oa, au))
    knots += [(oc, cu), (1.0, 1.0)]
    return knots


def warp(knots, uo):
    for (o0, i0), (o1, i1) in zip(knots, knots[1:]):
        if uo <= o1:
            return i0 + (i1 - i0) * (uo - o0) / (o1 - o0) if o1 > o0 else i1
    return 1.0


def _blend(a, b, w):
    """Mistura duas poses locais (translacao em lerp, quaternion em nlerp)."""
    lt = [tuple(x * (1 - w) + y * w for x, y in zip(p, q)) for p, q in zip(a[0], b[0])]
    lq = []
    for p, q in zip(a[1], b[1]):
        s = -1.0 if sum(x * y for x, y in zip(p, q)) < 0 else 1.0
        v = [x * (1 - w) + s * y * w for x, y in zip(p, q)]
        n = math.sqrt(sum(c * c for c in v)) or 1.0
        lq.append(tuple(c / n for c in v))
    return lt, lq


def segs_pose(sk, an, segs, uo, fade):
    """Pose no instante uo (0..1 do clipe de saida) de uma lista de trechos (u0, u1, parte) da animacao do STE; nas emendas
    os dois trechos se continuam e se misturam por `fade` (fracao do clipe)."""
    tot = sum(s[2] for s in segs)
    starts = [sum(s[2] for s in segs[:i]) for i in range(len(segs))]

    def at(i):
        u0, u1, share = segs[i]
        x = (uo * tot - starts[i]) / share
        return sample_local(sk, an, min(1.0, max(0.0, u0 + (u1 - u0) * x)), False)
    i = len(segs) - 1
    for k in range(1, len(segs)):
        if uo * tot < starts[k] - 1e-9:
            i = k - 1
            break
    pose = at(i)
    for k in range(1, len(segs)):
        b = starts[k] / tot
        if fade and abs(uo - b) < fade / 2:
            w = (uo - b) / fade + 0.5
            pose = _blend(at(k - 1), at(k), w * w * (3 - 2 * w))
    return pose


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
    knots = strike_knots(sk, an, opts['strike'], frames / FPS) if opts.get('strike') else opts.get('knots')   # knots: [(saida, entrada)]
    uin = (lambda uo: warp(knots, uo)) if knots else (lambda uo: uo)
    k_travel, root0 = 1.0, None
    if opts.get('strike'):   # o STE avanca o monstro pelo golpe (a cabecada anda 400 unidades): limita a um avanco que o jogo aguente
        r0 = world_pose(sk, *sample_local(sk, an, 0.0, False))[root][1]
        far = max(((lambda p: ((p[0] - r0[0]) ** 2 + (p[1] - r0[1]) ** 2) ** 0.5)(
            world_pose(sk, *sample_local(sk, an, f / 20.0, False))[root][1])) for f in range(21))
        k_travel, root0 = min(1.0, opts.get('maxtravel', 40.0) / far) if far > 1e-6 else 1.0, r0
    drift = (0.0, 0.0)
    segs = opts.get('segs')
    rest_z, first_xy, zs = None, None, opts.get('zscale')
    if zs is not None:   # a altura do pulo e da fisica do jogo: encolhe a subida da raiz do STE (so para cima) para a que o Togera embute
        rest_z = world_pose(sk, *sample_local(sk, an, 0.0, False))[root][1][2]
    if (cycle or opts.get('settle')) and not segs:   # o STE anda o monstro pela raiz; no WotM quem anda e o jogo: tira a deriva horizontal
        p0 = world_pose(sk, *sample_local(sk, an, 0.0, cycle))[root][1]
        p1 = world_pose(sk, *sample_local(sk, an, 1.0, cycle))[root][1]
        drift = (p1[0] - p0[0], p1[1] - p0[1])
    for f in range(frames):
        uo = f / float(frames if cycle else frames - 1)
        u = 0.0 if opts.get('still') else uin(uo)
        if opts.get('reverse'):   # andar/correr de costas: o mesmo ciclo ao contrario
            u = (1.0 - u) % 1.0 if cycle else 1.0 - u
        if segs:
            if opts.get('pingpong'):
                uo = 1.0 - abs(2.0 * uo - 1.0)
            lt, lq = segs_pose(sk, an, segs, uo, opts.get('fade', 0.0))
        else:
            lt, lq = sample_local(sk, an, u, cycle)
        W = world_pose(sk, lt, lq)
        shift = (0.0, 0.0)
        if root0 is not None:
            shift = ((W[root][1][0] - root0[0]) * (1 - k_travel), (W[root][1][1] - root0[1]) * (1 - k_travel))
        if segs:   # o corpo nao anda pela raiz: quem anda e o jogo
            if first_xy is None:
                first_xy = W[root][1][:2]
            shift = (W[root][1][0] - first_xy[0], W[root][1][1] - first_xy[1])
        dz = 0.0 if zs is None else (1.0 - zs) * max(0.0, W[root][1][2] - rest_z)
        for i, nm in enumerate(names):
            R, t = W[i]
            t = (t[0] - drift[0] * u - shift[0], t[1] - drift[1] * u - shift[1], t[2] - dz)
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
        st = table[wid][3].get('strike') if len(table[wid]) > 3 else None
        if st:
            kn = strike_knots(sk, byname[nm], st, clips[-1]['frames'] / FPS)
            print('      golpe (%s) janela %s: tempo saida->entrada %s' % (st['eff'], st['win'], ' '.join('%.2f>%.2f' % k for k in kn)))
    with open(out, 'w', encoding='utf-8') as f:
        json.dump({'clips': clips}, f, separators=(',', ':'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
