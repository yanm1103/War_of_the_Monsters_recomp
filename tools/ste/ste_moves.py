"""Golpes de Godzilla: Save the Earth (PS2): estados de luta do MONSTER_DATA de um monstro (.cmp).

Uso:
    python tools/ste/ste_moves.py <iso> <monstro.cmp> [--all] [--match REGEX] [--events]

Sem --all lista so os golpes (estados com janela de acerto); --events mostra os eventos crus de cada estado.

MONSTER_DATA e a entrada de tipo 2 do .cmp (Pipeworks Bundle). O que se sabe (lido dos dados, sem o codigo do STE):
- Estados: registros de 0x84 bytes seguidos; +0x7C = indice do nome da animacao, +0x80 = indice do nome do estado (tabela de
  nomes do bundle). +0x04 f32 (1,33 no jab; papel nao confirmado), +0x2C u32 = eventos {u16 quantos, u16 primeiro},
  +0x30 flags (0x800 nos golpes), +0x38 indice do estado, +0x3C 0..2 nos golpes e 3 nos outros, +0x50/+0x54 f32 (0,2..1 e
  5..25 nos golpes: forca e dano?).
- Eventos: depois do ultimo estado, u32 0 e uma tabela de u16 (offset de cada evento a partir do fim da tabela); cada evento e
  {u32 tipo, f32 instante (fracao da animacao, 0..1), dados}. Tipo 15: liga/desliga um conjunto de esferas de colisao
  {u16 n, n ids, ..., u16 papel, u16 liga} + f32 raio; os conjuntos casam com as partes do corpo (1-4 cauda, 5-7 braco
  direito, 8-10 braco esquerdo, 12-14 perna direita, 15-17 perna esquerda: hipotese tirada dos nomes dos golpes). Tipo 0
  (0x80 bytes): ataque ligado/desligado, com o dano em +0x2C. Tipo 6: som. Tipo 31 em t=0: grupo de estados que pode
  interromper este (?). Outros tipos: sem papel conhecido.
- O que leva de um golpe ao seguinte (botao, janela de combo) nao esta decifrado: deve ficar no grafo que vem depois dos eventos.
"""
import argparse, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ste_model import Iso, Vol, Bundle
import skeleton

STATE = 0x84
PARTS = {(1, 2, 3, 4): 'cauda', (5, 6, 7): 'braco D', (8, 9, 10): 'braco E', (12, 13, 14): 'perna D', (15, 16, 17): 'perna E',
         # Godzilla 90s (ossos a mais: os conjuntos andam)
         (5, 6, 7, 8): 'braco D', (5, 10, 11, 12): 'braco E', (13, 14, 15): 'perna D', (16, 17, 18): 'perna E'}


def _entry_table(b):
    """[(id, tipo, indice do nome)] da tabela que vem depois das strings dos nomes."""
    p, offs = skeleton._name_table(b)
    d = b.d
    e = d.index(b'\0', p + max(offs)) + 1
    while e < len(d) and d[e] == 0xff:
        e += 1
    return [struct.unpack_from('<HHI', d, e + 16 * i) for i in range((b.base - e) // 16)]


class MonsterData:
    def __init__(self, b):
        self.names = skeleton.names_of(b)
        tab = _entry_table(b)
        ents = {x['id']: x for x in b.entries}
        md_id = next(eid for eid, typ, ni in tab if typ == 2 and self.names[ni] == 'MONSTER_DATA')
        self.md = md = b.chunk(ents[md_id])
        self.dur = {}
        for eid, typ, ni in tab:
            if typ == 4:
                c = b.chunk(ents[eid])
                self.dur[ni] = struct.unpack_from('<f', c, 0x1c)[0] if len(c) > 0x20 else 0.0
        anims = set(self.dur)
        # estados: o par {animacao, nome que nao e animacao} em +0x7C/+0x80, de 0x84 em 0x84
        cand = [o - 0x7c for o in range(0x7c, len(md) - 8, 4)
                if struct.unpack_from('<I', md, o)[0] in anims and struct.unpack_from('<I', md, o + 4)[0] < len(self.names)
                and struct.unpack_from('<I', md, o + 4)[0] not in anims]
        run = max(self._runs(cand), key=len)
        self.states = [self._state(s) for s in run]
        n = max(st['ev0'] + st['nev'] for st in self.states)
        tab0 = run[-1] + STATE + 4
        self.ev_offs = struct.unpack_from('<%dH' % n, md, tab0)
        self.ev_base = (tab0 + 2 * n + 3) & ~3   # com n impar a tabela termina no meio de uma palavra: os eventos ficam alinhados em 4
        for st in self.states:
            st['events'] = [self._event(k) for k in range(st['ev0'], st['ev0'] + st['nev'])]

    @staticmethod
    def _runs(cand):
        runs, cur = [], []
        for s in cand:
            if cur and s - cur[-1] != STATE:
                runs.append(cur); cur = []
            cur.append(s)
        return runs + [cur]

    def _state(self, s):
        md = self.md
        u = lambda o: struct.unpack_from('<I', md, s + o)[0]
        f = lambda o: struct.unpack_from('<f', md, s + o)[0]
        ev = u(0x2c)
        a = u(0x7c)
        return dict(off=s, idx=u(0x38), name=self.names[u(0x80)], anim=self.names[a], dur=self.dur.get(a, 0.0),
                    f04=f(0x04), flags=u(0x30), cls=u(0x3c), f50=f(0x50), f54=f(0x54), ev0=ev >> 16, nev=ev & 0xffff)

    def _event(self, k):
        md = self.md
        o = self.ev_base + self.ev_offs[k]
        end = self.ev_base + self.ev_offs[k + 1] if k + 1 < len(self.ev_offs) else o + 16
        typ, t = struct.unpack_from('<If', md, o)
        e = dict(type=typ, t=t, raw=md[o + 8:end])
        if typ == 15:
            hw = struct.unpack_from('<8H', md, o + 8)
            e['set'] = tuple(hw[1:1 + hw[0]])
            e['role'], e['on'] = hw[6], hw[7]
            e['radius'] = struct.unpack_from('<f', md, o + 24)[0]
        elif typ == 0:
            e['on'] = struct.unpack_from('<I', md, o + 20)[0] != 0
            e['damage'] = struct.unpack_from('<f', md, o + 44)[0]
        return e


def windows(st):
    """{conjunto: [(inicio, fim)]} dos eventos 15 (papel 1) e [(inicio, fim, dano)] dos eventos 0."""
    sets, hit, open_ = {}, [], {}
    for e in st['events']:
        if e['type'] == 15:
            key = e['set']
            if e['on'] and e['role'] == 1:
                open_[key] = e['t']
            elif not e['on'] and key in open_:
                sets.setdefault(key, []).append((open_.pop(key), e['t']))
        elif e['type'] == 0:
            if e['on']:
                open_['hit'] = (e['t'], e['damage'])
            elif 'hit' in open_:
                t0, dmg = open_.pop('hit')
                hit.append((t0, e['t'], dmg))
    return sets, hit


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('iso'); ap.add_argument('bundle')
    ap.add_argument('--all', action='store_true'); ap.add_argument('--events', action='store_true')
    ap.add_argument('--match')
    a = ap.parse_args(argv)
    b = Bundle(Vol(Iso(a.iso)).get(a.bundle))
    m = MonsterData(b)
    print('%d estados, %d eventos' % (len(m.states), len(m.ev_offs)))
    pat = re.compile(a.match, re.I) if a.match else None
    for st in m.states:
        sets, hit = windows(st)
        if not a.all and not sets and not hit:
            continue
        if pat and not pat.search(st['name'] + ' ' + st['anim']):
            continue
        parts = '  '.join('%s %s' % (PARTS.get(k, str(list(k))), ' '.join('%.2f-%.2f' % w for w in v)) for k, v in sets.items())
        hits = ' '.join('%.2f-%.2f d%g' % h for h in hit)
        print('#%-3d %-26s %-28s %.2fs  f04 %.2f cls %d  f50 %.2f f54 %g | ataque %s | %s' % (
            st['idx'], st['name'][:26], st['anim'].split('_', 1)[-1][:28], st['dur'], st['f04'], st['cls'], st['f50'], st['f54'],
            hits or '-', parts or '-'))
        if a.events:
            for e in st['events']:
                print('      tipo %2d t %.3f %s' % (e['type'], e['t'], e['raw'].hex(' ', 4)[:120]))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
