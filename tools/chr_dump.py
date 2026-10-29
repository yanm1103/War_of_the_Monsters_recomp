#!/usr/bin/env python3
"""Tabela dos golpes de um monstro a partir do RST/<MONSTRO>.CHR do disco do usuario (so le; nada vai ao repo).

Uso:
    python tools/chr_dump.py disc/RST/MONKEY.CHR            # tabela dos socos (chave 0xFCC)
    python tools/chr_dump.py disc/RST/MONKEY.CHR --keys     # lista as chaves do arquivo
    python tools/chr_dump.py disc/RST/MONKEY.CHR --csv      # a mesma tabela em CSV
    python tools/chr_dump.py disc/RST/*.CHR                 # varios monstros
    python tools/chr_dump.py disc/RST/*.CHR --specials      # especial do monstro, crowd control e o ganho de stamina

Formato (docs/FORMATS.md, "Personagem (.CHR)"): cabecalho de 0xC bytes e depois chaves {u32 id; u32 tamanho com o cabecalho;
dados}, lidas pelo charreset::readKey. A chave 0xFCC guarda as configs do StatePunch: 10 posicoes no combo (o 1o golpe, o 2o...)
x 6 tipos de golpe, cada uma com 0xA0 bytes a partir de key + 0x10 + pos*0x3C0 + tipo*0xA0. O jogo as copia para o
StatePunch do Monster (Monster + 0x8470 + 0x20 + pos*0x420 + tipo*0xB0, a PunchConfig), que o modo treino le na RAM.

--specials: as chaves dos estados especiais (SPECIAL_KEYS: offset na chave -> offset no estado, que fica em Monster + base) e
a onda de choque do CrowdControl e do RobotSpecial (anel na chave: alcance e os campos +0x4..+0x18 do anel; papel de cada um
em b4e810b:docs/investigacoes/2026-10-26/move_list_campos_especiais.md, "ShockWaveConfig"). O ganho de stamina dos
AttackState (estado +0x14/+0x18, creditStaminaForAttack) nao esta no arquivo: nenhuma chave o escreve e o construtor zera.
OGRE e SPIDER usam outra numeracao de chaves (formato antigo, sem a 0xFCC de 0x26B0 bytes) e ficam de fora.
"""
import argparse
import os
import struct
import sys

PUNCH_KEY = 0xFCC
PUNCH_DEFAULTS_KEY = 0xFA1   # +0x30: raio padrao do punho (StatePunch +0x2980), quando o do golpe e 0
TIERS, TYPES = 10, 6

# localizadores (ossos nomeados): Monster::handleLocator, ids 0x802.. (treino_hitboxes.md)
LOCATORS = {0x802: 'MAO E', 0x803: 'MAO D', 0x804: 'PE E', 0x805: 'PE D'}

PUNCH_KEY_SIZE = 0x26B0   # tamanho da 0xFCC no formato que o readKey le (OGRE/SPIDER: numeracao antiga)

# Chaves dos especiais (charreset::readKey, caso = chave - 0xFA1): (nome do estado, offset do estado no Monster, tipo exigido
# (m_typeBits, Monster +0x14; None = qualquer), campos (offset na chave, offset no estado, descricao; 'i' no fim = inteiro cru),
# onda (offset na chave do n de aneis, do timeScale, do campo +0 do anel 0 e do 1o anel; offset da ShockWaveConfig no estado) ou None,
# condicionais (offset na chave so copiado se != 0)). Descricao com '?' = papel nao confirmado no asm do estado
# (80b3802:docs/investigacoes/2026-10-25/move_list_especiais.md).
SPECIAL_KEYS = {
    0xFA9: ('SonicRoar', 0xF6F4, None, [
        (0x08, 0x1C, 'blend do windup'), (0x0C, 0x20, 'windup (quadros)'), (0x10, 0x24, 'blend do laco?'),
        (0x14, 0x28, 'vel. do laco?'), (0x18, 0x2C, 'laco (quadros)'), (0x1C, 0x30, 'alcance cresce por quadro'),
        (0x20, 0x34, 'alcance maximo'), (0x24, 0x38, 'cone (graus, abertura total)'), (0x28, 0x3C, 'dano por alvo'),
        (0x2C, 0x40, 'dano ao cenario (acumulado)'), (0x38, 0x44, '? (nao lido no estado)'), (0x3C, 0x48, '? (nao lido no estado)'),
        (0x30, 0x4C, 'blend do winddown?'), (0x34, 0x50, 'winddown (quadros)')], None, ()),
    0xFAA: ('BugAttack', 0xF9D8, None, [
        (0x08, 0x1C, '?'), (0x0C, 0x20, 'windup (quadros)'), (0x10, 0x24, 'solta o bug (fracao do windup)'), (0x14, 0x28, '?'),
        (0x18, 0x2C, 'laco (quadros)'), (0x1C, 0x30, 'lanca o bug (fracao do laco)'), (0x20, 0x34, '?'),
        (0x24, 0x38, 'winddown (quadros)')], None, ()),
    0xFAD: ('FireBreath', 0xF974, None, [
        (0x08, 0x38, 'blend do windup'), (0x0C, 0x3C, 'windup (quadros)'), (0x10, 0x40, 'blend do laco'), (0x14, 0x44, '?'),
        (0x18, 0x1C, 'duracao do sopro (quadros)'), (0x1C, 0x20, 'alcance'), (0x20, 0x24, 'dano/s a monstro'),
        (0x24, 0x28, 'dano/s ao cenario'), (0x28, 0x48, '?'), (0x2C, 0x4C, '?'), (0x30, 0x2C, 'dano do fogo (so Raptros)'),
        (0x34, 0x30, 'quadros em chamas (so Raptros)'), (0x3C, 0x34, 'lentidao da vitima')], None, (0x3C,)),
    0xFBC: ('RockSpecial', 0x102A8, None, [
        (0x08, 0x20, 'blend do windup'), (0x0C, 0x24, 'windup (quadros)'), (0x10, 0x28, 'blend do laco'), (0x14, 0x2C, 'vel. do laco'),
        (0x18, 0x30, '?'), (0x1C, 0x34, 'winddown (quadros)'), (0x20, 0x38, 'pavio da cabeca (s)'),
        (0x24, 0x3C, 'knockback para cima'), (0x28, 0x40, 'knockback para fora')], None, ()),
    0xFC4: ('ZapAttack', 0x1059C, None, [
        (0x08, 0x1C, 'blend do windup'), (0x0C, 0x20, 'windup (quadros)'), (0x10, 0x24, '?'), (0x14, 0x28, '?'),
        (0x18, 0x2C, 'laco (quadros)'), (0x1C, 0x30, 'arco (+0x30)'), (0x20, 0x34, 'alcance (Manhattan)'), (0x24, 0x38, 'dano/s'),
        (0x30, 0x3C, 'knockback para fora (fim)'), (0x34, 0x40, 'knockback para cima (fim)'), (0x28, 0x44, '?'),
        (0x2C, 0x48, 'winddown (quadros)'), (0x38, 0x4C, 'amplitude do arco'), (0x3C, 0x50, '?')], None, ()),
    0xFC5: ('GrappleHook', 0x10600, None, [
        (0x08, 0x1C, '?'), (0x0C, 0x20, 'windup (quadros)'), (0x10, 0x24, '?'), (0x14, 0x28, 'vel. do laco'), (0x18, 0x30, 'alcance'),
        (0x1C, 0x34, 'dano'), (0x30, 0x38, '?'), (0x28, 0x3C, '? (nao lido no estado)'), (0x2C, 0x40, '? (nao lido no estado)'),
        (0x20, 0x44, '? (nao lido no estado)'), (0x24, 0x48, 'winddown (quadros)'), (0x34, 0x68, 'taxa de puxar'),
        (0x38, 0x6C, 'taxa de puxar (altura)'), (0x3C, 0x70, 'empurrao no chao (+0x1E8)'), (0x40, 0x74, '?'),
        (0x44, 0x78, 'taxa da velocidade da ponta'), (0x48, 0x7C, 'taxa do homing'), (0x4C, 0x80, 'velocidade da ponta (mph)')],
        None, ()),
    0xFC7: ('AirStrike', 0x10790, None, [
        (0x08, 0x1C, '?'), (0x0C, 0x20, 'windup (quadros)'), (0x10, 0x24, '?'), (0x14, 0x28, '?'), (0x18, 0x2C, 'laco (quadros)'),
        (0x1C, 0x30, '?'), (0x20, 0x34, 'winddown (quadros)'), (0x24, 0x174, 'dano por tiro dos discos'),
        (0x28, 0x178, 'duracao dos discos (campos)', 'i')], None, ()),
    0xFC8: ('LavaBlast', 0x10930, None, [
        (0x08, 0x1C, '?'), (0x0C, 0x20, 'windup (quadros)'), (0x10, 0x24, '?'), (0x14, 0x28, '?'), (0x18, 0x2C, '?'),
        (0x2C, 0x30, '?'), (0x24, 0x34, '?'), (0x28, 0x38, '?'), (0x1C, 0x3C, '?'), (0x20, 0x40, 'winddown (quadros)'),
        (0x30, 0x190, 'numero de bolas', 'i'), (0x34, 0x194, 'homing horizontal (o jogo le float)'), (0x38, 0x198, 'homing vertical (o jogo le float)'),
        (0x3C, 0x19C, 'dano por bola'), (0x40, 0x1A0, 'velocidade (mph)'), (0x44, 0x1A4, 'dano do fogo'),
        (0x48, 0x1A8, 'campos em chamas')], None, ()),
    0xFCF: ('RobotSpecial', 0xFE5C, 0x40, [
        (0x08, 0x1C, '?'), (0x0C, 0x20, 'windup (quadros)'), (0x10, 0x24, '?'), (0x14, 0x28, '?'), (0x18, 0x34, '?'),
        (0x1C, 0x38, 'winddown (quadros)'), (0x20, 0x3C, 'quadros minimos na mira'), (0x24, 0x40, 'trava da reticula (campos)'),
        (0x28, 0x44, '?'), (0x2C, 0x48, 'vida do missil (s)'), (0x30, 0x4C, 'velocidade do missil (mph)'), (0x34, 0x50, 'homing'),
        (0x38, 0x444, 'velocidade do efeito')], (0x3C, 0x40, 0x44, 0x48, 0x58, 7), ()),
    0xFE1: ('CrowdControl', 0xF3F0, None, [
        (0x08, 0x1C, 'blend do windup?'), (0x0C, 0x20, 'windup (quadros)'), (0x10, 0x24, '?'), (0x14, 0x28, '?'), (0x18, 0x2C, '?'),
        (0x1C, 0x30, 'blend do winddown'), (0x20, 0x34, '?'), (0x24, 0x38, 'onda sai (fracao do laco)')],
        (0x28, 0x2C, 0x30, 0x34, 0x3C, 7), ()),
}
# Variantes que nenhum .CHR do disco usa: os mesmos campos, mas o anel na chave tem 5 floats (sem os +0x14/+0x18)
SPECIAL_KEYS[0xFE0] = SPECIAL_KEYS[0xFCF][:4] + (SPECIAL_KEYS[0xFCF][4][:5] + (5,), ())
SPECIAL_KEYS[0xFDF] = SPECIAL_KEYS[0xFE1][:4] + (SPECIAL_KEYS[0xFE1][4][:5] + (5,), ())

# 0xFAA, depois dos campos do BugAttack: os estaticos do HomingBug (.sbss), um float cada a partir de key + 0x28
HOMING_BUG_STATICS = ['homingFactor', 'avoidanceFactor', 'maxForce', 'maxVel', 'initialVel', 'initialHealth', 'attachRange',
                      'attachMinAngle', 'attachFields', 'maxSeekFields', 'headRadius', 'feelerRadius', 'collisRadius',
                      'damageRadius', 'damagePerSecond', 'maxMonsterDamage', 'targetLockMaxRange', 'targetLockMinAngle']

# m_typeBits >> 5 (MonsterNewNames) pelo nome do arquivo, para a condicao de tipo da 0xFCF/0xFE0
FILE_TYPE = {'MONKEY': 1, 'ROBOT': 2, 'LIZARD': 3, 'SHOGUN': 4, 'MANTIS': 5, 'SPIDER': 6, 'ENERGY': 7, 'LAVA': 8, 'DRAGON': 9,
             'ROCK': 10, 'ALIEN': 11, 'OGRE': 12, 'ASSBOSS': 13, 'JELLY': 14, 'FINAL': 15}


def read_keys(data):
    keys, off = [], 0xC
    while off + 8 <= len(data):
        kid, size = struct.unpack_from('<II', data, off)
        if kid == 0 or size < 8 or off + size > len(data):
            break
        keys.append((kid, off, size))
        off += size
    return keys


def f32(d, o):
    return struct.unpack_from('<f', d, o)[0]


def i32(d, o):
    return struct.unpack_from('<i', d, o)[0]


def loc_name(v):
    if v < 0x7D0:
        return '-'
    return LOCATORS.get(v, '0x%X' % v)


def legacy(keys):
    """Numeracao antiga das chaves (OGRE, SPIDER): o readKey de hoje as leria como outra coisa."""
    return not any(k == PUNCH_KEY and s == PUNCH_KEY_SIZE for k, _, s in keys)


def specials(data, keys, name):
    """Linhas de texto do --specials: chaves dos especiais e do crowd control, na ordem do arquivo."""
    if legacy(keys):
        return ['%s: numeracao antiga das chaves (sem a 0x%X de 0x%X bytes); fora da tabela' % (name, PUNCH_KEY, PUNCH_KEY_SIZE)]
    out = ['%s:' % name]
    for kid, o, size in keys:
        spec = SPECIAL_KEYS.get(kid)
        if not spec:
            continue
        st_name, base, need_type, fields, wave, cond = spec
        out.append('  chave 0x%X -> %s (Monster +0x%X)%s' % (kid, st_name, base,
                   '' if need_type is None else ', so se m_typeBits == 0x%X' % need_type))
        if need_type is not None and FILE_TYPE.get(name, 0) << 5 != need_type:
            out.append('    (ignorada neste monstro: o readKey exige o tipo 0x%X)' % need_type)
            continue
        for fld in fields:
            ko, so, desc = fld[:3]
            if ko + 4 > size:
                continue
            if len(fld) > 3 and fld[3] == 'i':
                val = '%d' % i32(data, o + ko)
            else:
                v = f32(data, o + ko)
                if ko in cond and v == 0:
                    out.append('    est +0x%03X  (chave +0x%02X = 0: fica o do construtor)  %s' % (so, ko, desc))
                    continue
                val = '%g' % v
                if v != 0 and abs(v) < 1e-30:   # inteiro no arquivo onde o campo e float: o jogo fica com ~0
                    desc += ' [no arquivo: inteiro %d; como float vale ~0]' % i32(data, o + ko)
                    val = '~0'
            out.append('    est +0x%03X = %-10s %s' % (so, val, desc))
        if kid == 0xFAA:
            for i, s in enumerate(HOMING_BUG_STATICS):
                if 0x28 + 4 * i + 4 <= size:
                    out.append('    HomingBug::s_%-18s = %g' % (s, f32(data, o + 0x28 + 4 * i)))
        if wave:
            kn, kts, k0, kring, cfg, nf = wave
            stride = nf * 4   # na chave: alcance e os campos +0x4.. do anel (7 floats nas chaves do disco, 5 nas variantes)
            count = f32(data, o + kn)
            n = 0
            while n < count and n < 20 and kring + stride * (n + 1) <= size:   # o readKey repete enquanto i+1 < n (float)
                n += 1
            out.append('    onda (ShockWaveConfig em est +0x%X): %d aneis (int em est +0x%X), timeScale %g (est +0x%X), tolerancia vertical %g '
                       '(+0x0 do anel 0, est +0x%X; 0 = desligada)' % (cfg, n, cfg, f32(data, o + kts), cfg + 0x2A0, f32(data, o + k0), cfg + 0x20))
            out.append('      anel  alcance   dano  morto  forca C  forca 10%s   (anel i em est +0x%X + i*0x1C; alcance em est +0x%X + 4i)' % (
                '  atordoa   recoil' if nf > 5 else '', cfg + 0x20, cfg + 0x250))
            for i in range(n):
                r = o + kring + stride * i
                vals = [f32(data, r + 4 * j) for j in range(nf)]
                out.append('      %4d  %7g  %5g  %5g  %7g  %8g' % tuple([i] + vals[:5]) + ''.join('  %7g' % v for v in vals[5:]))
            out.append('      (ShockWave::update: forcas C/10 -> HitEvent +0x38/+0x34, as duas 0 = so recoil; atordoa -> HitEvent +0x3C, '
                       '> 100 atordoa; recoil -> Recoil +0x4C da vitima)')
    if len(out) == 1:
        out.append('  sem chave de especial')
    out.append('  ganho de stamina dos AttackState (est +0x14/+0x18): nao vem do .CHR (o construtor zera; nenhuma chave escreve)')
    return out


def punch_table(data, keys):
    """Linhas da tabela dos socos (dicts), na ordem posicao, tipo; [] sem a chave 0xFCC."""
    by_id = {k: (o, s) for k, o, s in keys}
    if PUNCH_KEY not in by_id or by_id[PUNCH_KEY][1] != PUNCH_KEY_SIZE:
        return [], None
    o, _ = by_id[PUNCH_KEY]
    base = o + 0x10
    default_r = None
    if PUNCH_DEFAULTS_KEY in by_id:
        fo, fs = by_id[PUNCH_DEFAULTS_KEY]
        if fs >= 0x34:
            default_r = f32(data, fo + 0x30)
    rows = []
    for t in range(TIERS):
        for k in range(TYPES):
            b = base + t * 0x3C0 + k * 0xA0
            anim = i32(data, b + 0x28)
            dmg = f32(data, b + 0x20)
            locs = [i32(data, b + 0x10), i32(data, b + 0x14)]
            if anim <= 0 and dmg == 0:
                continue
            r = f32(data, b + 0xA8)
            rows.append({
                'pos': t, 'tipo': k, 'anim': anim,
                'loc': '+'.join(loc_name(v) for v in locs if v >= 0x7D0) or '-',
                'ini': f32(data, b + 0x2C), 'fim': f32(data, b + 0x30),
                'cont_ini': f32(data, b + 0x34), 'cont_fim': f32(data, b + 0x38),
                'dano': dmg, 'raio': r if r > 0 or default_r is None else default_r, 'raio_padrao': r <= 0,
                'kb_h': f32(data, b + 0x8C), 'kb_v': f32(data, b + 0x90),
                'atord': f32(data, b + 0x94), 'pausa': f32(data, b + 0x98),
            })
    return rows, default_r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='+')
    ap.add_argument('--keys', action='store_true', help='lista as chaves do arquivo')
    ap.add_argument('--csv', action='store_true', help='tabela em CSV')
    ap.add_argument('--specials', action='store_true', help='chaves do especial e do crowd control (e o ganho de stamina)')
    a = ap.parse_args()
    if a.csv:
        print('monstro,pos,tipo,anim,loc,ini,fim,cont_ini,cont_fim,dano,raio,kb_h,kb_v,atord,pausa')
    for path in a.files:
        data = open(path, 'rb').read()
        keys = read_keys(data)
        name = os.path.splitext(os.path.basename(path))[0]
        if a.keys:
            print('%s: %s' % (name, ' '.join('%X(%X)' % (k, s) for k, _, s in keys)))
            continue
        if a.specials:
            print('\n'.join(specials(data, keys, name)))
            continue
        rows, default_r = punch_table(data, keys)
        if a.csv:
            for r in rows:
                print('%s,%d,%d,%d,%s,%.3f,%.3f,%.3f,%.3f,%.1f,%.2f,%.0f,%.0f,%.0f,%.0f' % (
                    name, r['pos'], r['tipo'], r['anim'], r['loc'], r['ini'], r['fim'], r['cont_ini'], r['cont_fim'],
                    r['dano'], r['raio'], r['kb_h'], r['kb_v'], r['atord'], r['pausa']))
            continue
        if not rows:
            print('%s: sem a chave 0x%X (socos)' % (name, PUNCH_KEY))
            continue
        print('%s: socos (chave 0x%X), raio padrao %s' % (name, PUNCH_KEY, '%.2f' % default_r if default_r is not None else '?'))
        print('  pos tipo anim  janela      dano  raio   atord  knockback   pausa  localizadores')
        for r in rows:
            print('  %3d %4d %4d  %4.0f-%3.0f%%  %5.1f  %5.1f%s %5.0f  %4.0f/%-4.0f  %5.0f  %s' % (
                r['pos'] + 1, r['tipo'], r['anim'], r['ini'] * 100, r['fim'] * 100, r['dano'], r['raio'],
                '*' if r['raio_padrao'] else ' ', r['atord'], r['kb_h'], r['kb_v'], r['pausa'], r['loc']))
        print('  (pos: golpe do combo, 1 = o primeiro; janela: % da animacao com teste de golpe; * raio padrao; atord > 100 atordoa)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
