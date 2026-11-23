"""Sons do Mechagodzilla 3 de Godzilla: Save the Earth nos numeros do Togera (o custom mechagodzilla3_ste e sobre o Togera).

  python mods/_mechagodzilla3_ste_src/ste_sounds.py <iso> <pasta files/ da pasta de trabalho> [<pasta das fontes>]

Mesmo esquema de mods/_godzilla2000_ste_src/ste_sounds.py: grava `lizard_s_sNNN.wav` (banco proprio, LIZARD_S) e
`genamb_s_sNNN.wav` (banco comum proprio, GENAMB_S) em files/; `modkit.py pack "Mechagodzilla 3"` monta os dois .BNK. Com a terceira
pasta grava tambem uma copia de cada um como `<banco>_<NNN>__<nome no STE>.wav` (as fontes, `sounds/`). O que cada numero e:
docs/guides/MODS.md ("Togera's sounds").
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'ste'))
import ste_model as S
import ste_sound as SS

# numero no banco do Togera -> nome do som no godzilla_sound do STE (a 1a amostra dele), ou (nome, segundos, fade em segundos)
# para cortar uma amostra longa. As vozes MGZ_* sao do Mechagodzilla 3; MGZ_MISSILE_FIRE, MGZ_CHEST_BEAM e MGZ3_ABS_ZERO, das armas dele.
LIZARD = {
    0: 'MGZ_ROAR_2',            # controle da multidao
    1: 'MGZ_SHOUT_DOWN_1',      # morte (animacao 120)
    2: 'MGZ_ROAR_1',            # grito da entrada (e do especial, specialRoar)
    3: 'MGZ_OOF_HEAVY',         # dor
    4: 'MGZ_SHOUT_UP_6',        # pulo
    5: 'MGZ_MISSILE_FIRE',      # disparo do foguete (Rocket Pack)
    6: 'EXPLOSION_MEDIUM_RANDOM',   # explosao do foguete (a do Missile_impact do STE)
    7: 'MGZ_CHEST_BEAM',        # raio do especial (Absolute Zero Cannon), em laco enquanto dura
    8: 'MGZ_ROAR_3',            # grito
    9: 'MGZ_SHOUT_LONG_2',      # grito do golpe forte
    10: 'MGZ_ROAR_2',           # vitorias
    11: 'MGZ_SCREECH_2',
    12: 'MGZ_ROAR_3',
    13: 'MGZ_SCREECH_1',        # provocacao
    14: 'MGZ_SHOUT_DOWN_3',     # mortes
    15: 'MGZ_SHOUT_LONG_5',
    16: 'MGZ_OOF_LIGHT',        # grunhido do golpe
    17: 'MECHAZILLA GROWL',     # entrar no contra-bloqueio
}
# numero no banco comum -> som do STE (so enquanto os sons deste monstro tocam)
GENAMB = {
    55: 'MECHAZILLA WHOOSH SMALL',      # whoosh do golpe
    63: 'MECHAZILLA FOOTSTEP MEDIUM',   # passo
    70: 'MECHAZILLA FOOTSTEP HEAVY',    # aterrissagem
}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    iso, out = argv[0], argv[1]
    src = argv[2] if len(argv) > 2 else None
    vol = S.Vol(S.Iso(iso))
    samples, names, uses = SS.parse_uber(vol.get('godzilla_sound.uber'))
    sp = vol.get('godzilla_sound.samp')
    os.makedirs(out, exist_ok=True)
    if src:
        os.makedirs(src, exist_ok=True)
    for bank, table in (('lizard_s', LIZARD), ('genamb_s', GENAMB)):
        for num, nm in sorted(table.items()):
            cut = None
            if isinstance(nm, tuple):
                nm, cut, fade = nm
            k = names.index(nm)
            s = samples[uses[k][0]]
            pcm = SS.decode_adpcm(sp[s['off']:s['off'] + s['size']])
            if cut:
                n, nf = int(cut * s['rate']), int(fade * s['rate'])
                pcm = pcm[:n]
                for i in range(max(0, len(pcm) - nf), len(pcm)):
                    pcm[i] = int(pcm[i] * (len(pcm) - i) / float(nf))
            p = os.path.join(out, '%s_s%03d.wav' % (bank, num))
            SS.write_wav(p, pcm, s['rate'])
            if src:
                shutil.copyfile(p, os.path.join(src, '%s_%03d__%s.wav' % (bank, num, SS.safe(nm))))
            print('%s %3d <- %-26s %.2fs%s' % (bank, num, nm, len(pcm) / float(s['rate']), ' (laco)' if s.get('flags', 0) & 1 else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
