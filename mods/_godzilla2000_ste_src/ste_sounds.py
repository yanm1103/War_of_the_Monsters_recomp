"""Sons do Godzilla 2000 de Godzilla: Save the Earth nos numeros do Togera (o custom godzilla_ste e sobre o Togera).

  python mods/_godzilla2000_ste_src/ste_sounds.py <iso> <pasta files/ da pasta de trabalho> [<pasta das fontes>]

Grava `lizard_s_sNNN.wav` (banco proprio, LIZARD_S) e `genamb_s_sNNN.wav` (banco comum proprio, GENAMB_S) na pasta files/ do
custom; `modkit.py pack "Godzilla 2000"` monta os dois .BNK. Com a terceira pasta grava tambem uma copia de cada um como
`<banco>_<NNN>__<nome no STE>.wav` (as fontes deste mod, `sounds/`). O que cada numero e: docs/guides/MODS.md ("Togera's sounds").
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'ste'))
import ste_model as S
import ste_sound as SS

# numero no banco do Togera -> nome do som no godzilla_sound do STE (a 1a amostra dele), ou (nome, segundos, fade em segundos)
# para cortar uma amostra longa
LIZARD = {
    0: 'G2K_ROAR_3',            # controle da multidao
    1: 'GZ2K MOAN',             # morte (animacao 120)
    2: 'G2K_ROAR_1',            # grito da entrada (e do especial, specialRoar)
    3: 'G2K_OOF_HEAVY',         # dor
    4: 'G2K_SHOUT_UP_6',        # pulo
    5: ('GODZILLA2K ATOMIC BREATH', 1.4, 0.4),   # disparo da bola de fogo (no STE, o laco do voo dela; aqui um tiro so, cortado)
    6: 'WI_BURNING_3',          # explosao da bola de fogo (atomicfireball_impact_shockwave no STE)
    7: 'GODZILLA2K BREATH LOOP',   # sopro, em laco durante o especial
    8: 'GODZILLA2K ROAR RAGE',  # grito
    9: 'G2K_SHOUT_LONG_2',      # grito do golpe forte
    10: 'G2K_ROAR_2',           # vitorias
    11: 'GODZILLA2K YEEAH',
    12: 'G2K_SCREECH_2',
    13: 'G2K_SCREECH_1',        # provocacao
    14: 'G2K_SHOUT_LONG_5',     # mortes
    15: 'GODZILLA2K SCREECH',
    16: 'GODZILLA2K GRUNT SMALL',   # grunhido do golpe
    17: 'GODZILLA2K SNARL',     # entrar no contra-bloqueio
}
# numero no banco comum -> som do STE (so enquanto os sons deste monstro tocam)
GENAMB = {
    55: 'GODZILLA2K WHOOSH SMALL',  # whoosh do golpe
    63: 'GODZILLA2K FOOTSTEP MEDIUM',   # passo
    70: 'GODZILLA2K GROUND IMPACT',     # aterrissagem
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
            print('%s %3d <- %-26s %.2fs' % (bank, num, nm, len(pcm) / float(s['rate'])))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
