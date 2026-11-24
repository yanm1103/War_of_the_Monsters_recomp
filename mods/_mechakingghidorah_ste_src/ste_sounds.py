"""Sons do Mecha-King Ghidorah de Godzilla: Save the Earth nos numeros do Raptros (o custom mechaghidorah_ste e sobre o Raptros).
A voz e a dele (MKG_*, MECHAGHIDORAH_*, mkg_*); o bater de asas, o planar, o raio e o passo sao os do King Ghidorah.

  python mods/_mechakingghidorah_ste_src/ste_sounds.py <iso> <pasta files/ da pasta de trabalho> [<pasta das fontes>]

Mesmo esquema de mods/_kingghidorah_ste_src/ste_sounds.py: grava `dragon_s_sNNN.wav` (banco proprio, DRAGON_S) e
`genamb_s_sNNN.wav` (banco comum proprio, GENAMB_S) em files/; `modkit.py pack "Mecha King Ghidorah"` monta os dois .BNK. Com a terceira
pasta grava tambem uma copia de cada um como `<banco>_<NNN>__<nome no STE>.wav` (as fontes, `sounds/`). Os numeros do DRAGON_S
sao os que o MonsterSound toca no ramo do tipo 0x120 (src/game/Sound.cpp).
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'ste'))
import ste_model as S
import ste_sound as SS

# numero no banco do Raptros -> nome do som no godzilla_sound do STE (a 1a amostra dele), ou (nome, segundos, fade em segundos)
DRAGON = {
    0: ('MECHAGHIDORAH GRUNT SMALL', 0.7, 0.15),   # pulo (playJumpSound)
    1: 'GHIDORAH WING FLAP',        # bater de asas (playWingFlap)
    2: 'MECHAGHIDORAH AAH',         # morte (animacao 120)
    3: ('MKG_OOF_HEAVY', 0.9, 0.2),  # dor (playTakeHitSound)
    4: ('MKG_ROAR_1', 2.2, 0.4),    # grito da entrada e do especial (playSignatureSound)
    5: 'mkg_roar_2_oneshot',        # controle da multidao
    6: 'GODZILLA2K FOOTSTEP HEAVY', # aterrissagem e queda de bunda
    10: 'GHIDORAH BEAM LOOP',       # raio do especial (updateFireBreath), em laco enquanto dura
    12: ('MKG_OOF_LIGHT', 0.8, 0.2),  # grunhido de agarrar, pisar e arremessar
    13: 'GHIDORAH FLY HUM',         # planar (updateFlightGlideSound), em laco
    14: 'mkg_roar_3_oneshot',       # vitorias (e provocacao)
    15: ('MKG_SCREECH_1', 2.0, 0.3),
    16: 'mkg_growl_1_oneshot',
    17: ('MECHAGHIDORAH ROAR RAGE', 2.5, 0.4),   # provocacao (playTauntSound)
    18: 'MECHAGHIDORAH YEE',        # mortes
    19: ('MECHAGHIDORAH SCREECH', 1.8, 0.3),
}
# numero no banco comum -> som do STE (so enquanto os sons deste monstro tocam)
GENAMB = {
    55: 'GODZILLA2K WHOOSH SMALL',      # whoosh do golpe
    62: 'GODZILLA2K FOOTSTEP MEDIUM',   # passo do Raptros (0x3E)
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
    for bank, table in (('dragon_s', DRAGON), ('genamb_s', GENAMB)):
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
