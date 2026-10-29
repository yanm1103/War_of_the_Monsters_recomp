"""Gera C++ nativo para os microprogramas do VU0 do WotM (wotm_vu0_native.gen.inc, incluido por
ps2xRuntime/src/lib/vu/wotm_vu0_native.cpp no runtime do PS2Recomp).

O microcodigo sai do ELF (pacote VIF com MPG que o jogo manda ao VU0) ou de um vu0_code.bin (PS2X_DUMP_VU0). O C++ gerado
reproduz o VU1Interpreter do runtime bit a bit: operandos normalizados, conta em float com arredondamento para zero e o
resultado classificado pelo valor em double, flags MAC/status publicadas 4 ciclos depois da emissao (o fmand/fsand ve so as
que ja chegaram), stores e escritas de VI/VF na ordem do programa (o VU para em dependencia de VF/VI/ACC, entao a semantica
sequencial vale), o par dividido como no hardware (o lower le os registradores antes da escrita do upper do mesmo par) e o
desvio que le um VI escrito pelo par anterior vendo o valor antigo (readBranchVi).

Compilacao por blocos especializados pelo estado do pipeline: cada no e (pc, estado), com o estado relativo ao ciclo atual
(quantos ciclos faltam para cada VF/VI/ACC escrito ficar pronto, as entradas pendentes do pipeline de flags e em que variavel
estao, o VI salvo para o desvio seguinte, os enderecos de retorno do bal). As paradas e o momento em que cada flag fica
visivel saem do estado, sem depender dos dados; desvios viram goto entre nos, e um laco fecha quando o estado se repete.
O modelo de ciclos e o de decodeUpperUsage/decodeLowerUsage e calculatePairReadyCycle; --trace o confere contra o
interpretador nos programas em linha reta. O orcamento de 4096 ciclos do interpretador (programa cortado no meio) nao e
reproduzido: o nativo sempre roda ate o fim, como o hardware.

Instrucoes fora do subconjunto (DIV, EFU, XGKICK...) deixam o programa no interpretador, e o gerador diz por que. O .inc
gerado e traducao do codigo do jogo: fica fora do repo.

Uso: python tools/vu0native.py <SCUS_971.97 | vu0_code.bin> <saida .inc> [--trace trace.txt]
"""
import struct
import sys

# Pacote do microcodigo do VU0 no ELF: tag DMA (cnt) seguida de codigos VIF (NOP, MPG); offset no arquivo do NTSC-U.
ELF_VU0_PACKET = 0x15C890
# Entradas usadas pelo jogo (vcallms)
ENTRIES = [0x0, 0xC8, 0x1A8, 0x398, 0x3F8, 0x430, 0x4D0, 0x748, 0x8D8, 0x970, 0xA00, 0xA80, 0xB00, 0xB48]
CODE_SIZE = 4096
LANES = (8, 4, 2, 1)   # x y z w
MAX_NODES = 400
FLAG_SLOTS = 8


class Unsupported(Exception):
    pass


def load_code(path):
    data = open(path, 'rb').read()
    if len(data) == CODE_SIZE:
        return data
    code = bytearray(CODE_SIZE)
    tag = struct.unpack_from('<Q', data, ELF_VU0_PACKET)[0]
    qwc, tid = tag & 0xFFFF, (tag >> 28) & 7
    if tid != 1:
        raise SystemExit(f'tag DMA inesperado em {ELF_VU0_PACKET:#x}: {tag:#x}')
    pos, end = ELF_VU0_PACKET + 8, ELF_VU0_PACKET + 16 + qwc * 16
    while pos < end:
        vif = struct.unpack_from('<I', data, pos)[0]
        pos += 4
        cmd = (vif >> 24) & 0x7F
        if cmd == 0x00:
            continue
        if cmd != 0x4A:
            raise SystemExit(f'codigo VIF inesperado {vif:#x} em {pos - 4:#x}')
        num = (vif >> 16) & 0xFF or 256
        addr = (vif & 0xFFFF) * 8
        pos = (pos + 7) & ~7
        code[addr:addr + num * 8] = data[pos:pos + num * 8]
        pos += num * 8
    return bytes(code)


def lanes_of(i):
    return (i >> 21) & 0xF


# ---------------------------------------------------------------------------------------------------------------------
# Decodificacao (o subconjunto usado pelo jogo)

FORMS = {0: 'add', 1: 'sub', 2: 'madd', 3: 'msub', 4: 'max', 5: 'mini', 6: 'mul'}
QI = {0x1C: ('mul', 'q'), 0x1D: ('max', 'i'), 0x1E: ('mul', 'i'), 0x1F: ('mini', 'i'), 0x20: ('add', 'q'), 0x21: ('madd', 'q'),
      0x22: ('add', 'i'), 0x23: ('madd', 'i'), 0x24: ('sub', 'q'), 0x25: ('msub', 'q'), 0x26: ('sub', 'i'), 0x27: ('msub', 'i')}
VEC = {0x28: 'add', 0x29: 'madd', 0x2A: 'mul', 0x2B: 'max', 0x2C: 'sub', 0x2D: 'msub', 0x2F: 'mini'}


def decode_upper(i):
    """dict com form, dst ('vf', r) | ('acc',), mask, fs, b (('bc', ft, comp) | ('q',) | ('i',) | ('vec', ft));
    form 'ftoi0'/'itof0' com dst ('vf', ft); None para nop."""
    op = i & 0x3F
    mask, ft, fs, fd = lanes_of(i), (i >> 16) & 31, (i >> 11) & 31, (i >> 6) & 31
    if op < 0x1C:
        return dict(form=FORMS[op >> 2], dst=('vf', fd), mask=mask, fs=fs, b=('bc', ft, op & 3))
    if op in QI:
        form, src = QI[op]
        return dict(form=form, dst=('vf', fd), mask=mask, fs=fs, b=(src,))
    if op in VEC:
        return dict(form=VEC[op], dst=('vf', fd), mask=mask, fs=fs, b=('vec', ft))
    if op >= 0x3C:
        sp = (i & 3) | ((i >> 4) & 0x7C)
        if sp in (0x2F, 0x30):
            return None
        if sp < 0x10 or 0x18 <= sp < 0x1C:
            return dict(form=FORMS[sp >> 2], dst=('acc',), mask=mask, fs=fs, b=('bc', ft, sp & 3))
        if sp in QI and QI[sp][0] not in ('max', 'mini'):
            form, src = QI[sp]
            return dict(form=form, dst=('acc',), mask=mask, fs=fs, b=(src,))
        if sp in (0x28, 0x29, 0x2A, 0x2C, 0x2D):
            return dict(form=VEC[sp], dst=('acc',), mask=mask, fs=fs, b=('vec', ft))
        if sp in (0x10, 0x14):
            return dict(form='itof0' if sp == 0x10 else 'ftoi0', dst=('vf', ft), mask=mask, fs=fs, b=None)
        raise Unsupported(f'upper especial {sp:#x}')
    raise Unsupported(f'upper {op:#x}')


def imm11(i):
    v = i & 0x7FF
    return v - 0x800 if v & 0x400 else v


def decode_lower(i, pc):
    if i in (0, 0x8000033C):
        return None
    op = (i >> 25) & 0x7F
    t, s = (i >> 16) & 31, (i >> 11) & 31
    vt, vs, vd = (i >> 16) & 15, (i >> 11) & 15, (i >> 6) & 15
    mask = lanes_of(i)
    target = (pc + 8 + imm11(i) * 8) & (CODE_SIZE - 1)
    if op == 0x00:
        return dict(op='lq', mask=mask, ft=t, vis=vs, imm=imm11(i))
    if op == 0x01:
        return dict(op='sq', mask=mask, fs=s, vit=vt, imm=imm11(i))
    if op == 0x04:
        comp = next((c for c in range(4) if mask & LANES[c]), 3)
        return dict(op='ilw', comp=comp, vit=vt, vis=vs, imm=imm11(i))
    if op == 0x08:
        return dict(op='iaddiu', vit=vt, vis=vs, imm=(i & 0x7FF) | ((i >> 10) & 0x7800))
    if op == 0x16:
        return dict(op='fsand', vit=vt, imm=(((i >> 21) & 1) << 11) | (i & 0x7FF))
    if op == 0x1A:
        return dict(op='fmand', vit=vt, vis=vs)
    if op == 0x20:
        return dict(op='b', target=target)
    if op == 0x21:
        return dict(op='bal', vit=vt, target=target)
    if op == 0x24:
        return dict(op='jr', vis=vs)
    if op in (0x28, 0x29):
        return dict(op='ibeq' if op == 0x28 else 'ibne', vit=vt, vis=vs, target=target)
    if op == 0x40:
        lo = i & 0x3F
        if lo in (0x30, 0x31, 0x34, 0x35):
            return dict(op={0x30: 'iadd', 0x31: 'isub', 0x34: 'iand', 0x35: 'ior'}[lo], vid=vd, vis=vs, vit=vt)
        if lo == 0x32:
            im = (i >> 6) & 31
            return dict(op='iaddi', vit=vt, vis=vs, imm=im - 32 if im & 16 else im)
        if lo >= 0x3C:
            sp = (i & 3) | ((i >> 4) & 0x7C)
            if sp in (0x30, 0x31):
                return dict(op='move' if sp == 0x30 else 'mr32', mask=mask, ft=t, fs=s)
    raise Unsupported(f'lower {i:#010x} em {pc:#x}')


def decode_pair(code, pc):
    lo, up = struct.unpack_from('<II', code, pc)
    if up & 0x38000000:
        raise Unsupported(f'bit M/D/T em {pc:#x}')
    ibit = bool(up & 0x80000000)
    return dict(pc=pc, upper=decode_upper(up), lower=None if ibit else decode_lower(lo, pc), loi=lo if ibit else None,
                ebit=bool(up & 0x40000000))


# ---------------------------------------------------------------------------------------------------------------------
# Uso de registradores e latencias (decodeUpperUsage / decodeLowerUsage)

def upper_usage(u):
    """(vf lidos [(reg, lanes)], vf escrito (reg, lanes, lat) | None, acc lido, acc escrito, gera entrada de flags)"""
    if u is None:
        return [], None, 0, 0, False
    reads = [(u['fs'], u['mask'])]
    b = u['b']
    if b is not None and b[0] == 'bc':
        reads.append((b[1], LANES[b[2]]))
    elif b is not None and b[0] == 'vec':
        reads.append((b[1], u['mask']))
    acc_read = u['mask'] if u['form'] in ('madd', 'msub') else 0
    acc_write = u['mask'] if u['dst'][0] == 'acc' else 0
    write = (u['dst'][1], u['mask'], 4) if u['dst'][0] == 'vf' and u['dst'][1] != 0 and u['mask'] else None
    flags = u['form'] not in ('max', 'mini', 'itof0', 'ftoi0') and u['mask'] != 0
    return reads, write, acc_read, acc_write, flags


# delaysNextBranchRead: o desvio do par seguinte que le o mesmo VI ve o valor antigo
BRANCH_DELAYED = {'iaddiu', 'iaddi', 'iadd', 'isub', 'iand', 'ior'}


def lower_usage(l):
    """(vf lidos, vf escrito (reg, lanes, lat), vi lidos, vi escrito (reg, lat), store)"""
    if l is None:
        return [], None, [], None, False
    op = l['op']
    if op == 'lq':
        return [], (l['ft'], l['mask'], 4) if l['ft'] and l['mask'] else None, [l['vis']], None, False
    if op == 'sq':
        return [(l['fs'], l['mask'])], None, [l['vit']], None, True
    if op == 'ilw':
        return [], None, [l['vis']], (l['vit'], 4), False
    if op in ('iaddiu', 'iaddi'):
        return [], None, [l['vis']], (l['vit'], 1), False
    if op == 'fsand':
        return [], None, [], (l['vit'], 1), False
    if op == 'fmand':
        return [], None, [l['vis']], (l['vit'], 1), False
    if op in ('iadd', 'isub', 'iand', 'ior'):
        return [], None, [l['vis'], l['vit']], (l['vid'], 1), False
    if op in ('move', 'mr32'):
        lanes = 0xF if op == 'mr32' else l['mask']
        return [(l['fs'], lanes)], (l['ft'], l['mask'], 4) if l['ft'] and l['mask'] else None, [], None, False
    if op == 'b':
        return [], None, [], None, False
    if op == 'bal':
        return [], None, [], (l['vit'], 1), False
    if op == 'jr':
        return [], None, [l['vis']], None, False
    if op in ('ibeq', 'ibne'):
        return [], None, [l['vis'], l['vit']], None, False
    raise Unsupported(op)


# ---------------------------------------------------------------------------------------------------------------------
# Estado do pipeline relativo ao ciclo atual

class State:
    __slots__ = ('vf', 'vi', 'acc', 'flags', 'pend', 'bk', 'consts')

    def __init__(self):
        self.vf = {}       # (reg, comp) -> ciclos ate ficar pronto
        self.vi = {}       # reg -> ciclos
        self.acc = {}      # comp -> ciclos
        self.flags = []    # [(ciclos ate publicar, slot)] na ordem de emissao
        self.pend = 0      # ciclos ate o ultimo pipeline esvaziar (flush)
        self.bk = 0        # VI escrito pelo par anterior com delaysNextBranchRead (valor antigo em 'bk')
        self.consts = {}   # VI com endereco de retorno conhecido (bal)

    def copy(self):
        s = State()
        s.vf, s.vi, s.acc, s.flags = dict(self.vf), dict(self.vi), dict(self.acc), list(self.flags)
        s.pend, s.bk, s.consts = self.pend, self.bk, dict(self.consts)
        return s

    def key(self):
        return (tuple(sorted(self.vf.items())), tuple(sorted(self.vi.items())), tuple(sorted(self.acc.items())),
                tuple(self.flags), self.pend, self.bk, tuple(sorted(self.consts.items())))

    def advance(self, d):
        if d <= 0:
            return
        self.vf = {k: v - d for k, v in self.vf.items() if v > d}
        self.vi = {k: v - d for k, v in self.vi.items() if v > d}
        self.acc = {k: v - d for k, v in self.acc.items() if v > d}
        self.flags = [(r - d, slot) for r, slot in self.flags]
        self.pend = max(0, self.pend - d)


# ---------------------------------------------------------------------------------------------------------------------
# Emissao

def flt(bits):
    return f'B({bits:#010x}u)'


def operand_b(u, c):
    b = u['b']
    if b[0] == 'bc':
        return f's.vf[{b[1]}][{b[2]}]'
    if b[0] == 'vec':
        return f's.vf[{b[1]}][{c}]'
    return 's.q' if b[0] == 'q' else 's.i'


def operand_b4(u):
    b = u['b']
    if b[0] == 'bc':
        return f'_mm_set1_ps(N(s.vf[{b[1]}][{b[2]}]))'
    if b[0] == 'vec':
        return f'NV(LD(s.vf[{b[1]}]))'
    return '_mm_set1_ps(N(s.q))' if b[0] == 'q' else '_mm_set1_ps(N(s.i))'


def emit_upper(u, slot, out):
    """Calcula o resultado do upper (sem escrever); devolve as linhas de escrita. FMAC/max/mini nas 4 lanes de uma vez."""
    form = u['form']
    if slot is not None:
        out.append(f'        F[{slot}] = Fm{{}};')
    if form not in ('itof0', 'ftoi0'):
        d = sum(1 << c for c in range(4) if u['mask'] & LANES[c])   # x no bit 0
        out.append(f'        const __m128 a = NV(LD(s.vf[{u["fs"]}])), b = {operand_b4(u)};')
        if form in ('max', 'mini'):
            out.append(f'        const __m128 u = _mm_{"max" if form == "max" else "min"}_ps(a, b);')
        elif form in ('madd', 'msub'):
            out.append(f'        const __m128 u = MADD4<{d}>(NV(LD(s.acc)), a, b, F[{slot}], {"true" if form == "msub" else "false"});')
        else:
            out.append(f'        const __m128 u = {form.upper()}4<{d}>(a, b, F[{slot}]);')
        dst = 's.acc' if u['dst'][0] == 'acc' else (None if u['dst'][1] == 0 else f's.vf[{u["dst"][1]}]')
        return [f'        ST<{d}>({dst}, u);'] if dst else []
    for c in range(4):
        if not (u['mask'] & LANES[c]):
            continue
        if form == 'itof0':
            out.append(f'        int32_t i{c}; std::memcpy(&i{c}, &s.vf[{u["fs"]}][{c}], 4);')
            out.append(f'        const float u{c} = (float)i{c};')
            continue
        if form == 'ftoi0':
            out.append(f'        const int32_t i{c} = F2I(N(s.vf[{u["fs"]}][{c}]));')
            out.append(f'        float u{c}; std::memcpy(&u{c}, &i{c}, 4);')
            continue
        a, b = f'N(s.vf[{u["fs"]}][{c}])', f'N({operand_b(u, c)})'
        out.append(f'        const float a{c} = {a}, b{c} = {b};')
        if form in ('max', 'mini'):
            cmp = '>' if form == 'max' else '<'
            out.append(f'        const float u{c} = a{c} {cmp} b{c} ? a{c} : b{c};')
        elif form in ('madd', 'msub'):
            sign = '+' if form == 'madd' else '-'
            out.append(f'        const float c{c} = N(s.acc[{c}]);')
            out.append(f'        const float u{c} = L(c{c} {sign} a{c} * b{c}, (double)c{c} {sign} (double)a{c} * (double)b{c}, {LANES[c]}u, F[{slot}]);')
            out.append(f'        P(a{c}, b{c}, F[{slot}]);')
        else:
            sym = {'add': '+', 'sub': '-', 'mul': '*'}[form]
            out.append(f'        const float u{c} = L(a{c} {sym} b{c}, (double)a{c} {sym} (double)b{c}, {LANES[c]}u, F[{slot}]);')
    dst = 's.acc' if u['dst'][0] == 'acc' else (None if u['dst'][1] == 0 else f's.vf[{u["dst"][1]}]')
    if not dst:
        return []
    return [f'        {dst}[{c}] = u{c};' for c in range(4) if u['mask'] & LANES[c]]


def vi_read(reg, st, branch):
    """VI lido; o desvio ve o valor antigo do VI escrito pelo par anterior (readBranchVi)"""
    if reg == 0:
        return '0'
    if branch and st.bk == reg:
        return 'bk'
    return f's.vi[{reg}]'


def emit_lower(l, upper_dst, st, out):
    """Executa o lower (le o estado antes da escrita do upper). Devolve (escritas de VF para depois do upper, desvio)
    onde desvio e None, ('goto', alvo), ('cond', alvo) com a condicao em 'br', ou ('jr', alvo)."""
    op = l['op']
    if op in BRANCH_DELAYED:
        dst = l['vid'] if op in ('iadd', 'isub', 'iand', 'ior') else l['vit']
        if dst:
            out.append(f'        bk = s.vi[{dst}];')
    if op == 'lq':
        out.append(f'        const uint32_t la = addr16(s.vi[{l["vis"]}], {l["imm"]});')
        if l['ft'] == 0 or l['ft'] == upper_dst:
            return [], None
        return [f'        std::memcpy(&s.vf[{l["ft"]}][{c}], d + la + {4 * c}, 4);' for c in range(4) if l['mask'] & LANES[c]], None
    if op == 'sq':
        out.append(f'        const uint32_t sa = addr16(s.vi[{l["vit"]}], {l["imm"]});')
        for c in range(4):
            if l['mask'] & LANES[c]:
                out.append(f'        std::memcpy(d + sa + {4 * c}, &s.vf[{l["fs"]}][{c}], 4);')
        return [], None
    if op == 'ilw':
        if l['vit']:
            out.append(f'        {{ uint32_t w; std::memcpy(&w, d + addr16(s.vi[{l["vis"]}], {l["imm"]}) + {4 * l["comp"]}, 4); '
                       f's.vi[{l["vit"]}] = (int16_t)(w & 0xFFFFu); }}')
        return [], None
    if op == 'iaddiu':
        if l['vit']:
            out.append(f'        s.vi[{l["vit"]}] = (int16_t)(s.vi[{l["vis"]}] + {l["imm"]});')
        return [], None
    if op == 'iaddi':
        if l['vit']:
            out.append(f'        s.vi[{l["vit"]}] = (int16_t)(s.vi[{l["vis"]}] + ({l["imm"]}));')
        return [], None
    if op == 'fsand':
        if l['vit']:
            out.append(f'        s.vi[{l["vit"]}] = (int16_t)((s.status & 0xFFFu) & {l["imm"]:#x}u);')
        return [], None
    if op == 'fmand':
        if l['vit']:
            out.append(f'        s.vi[{l["vit"]}] = (int16_t)(s.mac & (uint32_t)(uint16_t)s.vi[{l["vis"]}]);')
        return [], None
    if op in ('iadd', 'isub', 'iand', 'ior'):
        sym = {'iadd': '+', 'isub': '-', 'iand': '&', 'ior': '|'}[op]
        if l['vid']:
            out.append(f'        s.vi[{l["vid"]}] = (int16_t)(s.vi[{l["vis"]}] {sym} s.vi[{l["vit"]}]);')
        return [], None
    if op in ('move', 'mr32'):
        if l['ft'] == 0 or l['ft'] == upper_dst:
            return [], None
        for c in range(4):
            if l['mask'] & LANES[c]:
                src = c if op == 'move' else (c + 1) & 3
                out.append(f'        const float m{c} = s.vf[{l["fs"]}][{src}];')
        return [f'        s.vf[{l["ft"]}][{c}] = m{c};' for c in range(4) if l['mask'] & LANES[c]], None
    if op == 'b':
        return [], ('goto', l['target'])
    if op == 'bal':
        if l['vit']:
            out.append(f'        s.vi[{l["vit"]}] = {(l["pc"] + 16) // 8};')
        return [], ('goto', l['target'])
    if op == 'jr':
        if st.bk == l['vis'] or l['vis'] not in st.consts:
            raise Unsupported(f'jr vi{l["vis"]} sem endereco conhecido')
        return [], ('jr', (st.consts[l['vis']] * 8) & (CODE_SIZE - 1))
    if op in ('ibeq', 'ibne'):
        cmp = '==' if op == 'ibeq' else '!='
        out.append(f'        br = (int16_t)({vi_read(l["vis"], st, True)}) {cmp} (int16_t)({vi_read(l["vit"], st, True)});')
        return [], ('cond', l['target'])
    raise Unsupported(op)


def disasm_upper(u):
    if u is None:
        return 'nop'
    m = ''.join(ch for ch, b in zip('xyzw', LANES) if u['mask'] & b)
    dst = 'ACC' if u['dst'][0] == 'acc' else f'vf{u["dst"][1]}'
    b = u['b']
    if b is None:
        return f'{u["form"]}.{m} {dst}, vf{u["fs"]}'
    bs = {'bc': lambda: f'vf{b[1]}{"xyzw"[b[2]]}', 'vec': lambda: f'vf{b[1]}', 'q': lambda: 'Q', 'i': lambda: 'I'}[b[0]]()
    return f'{u["form"]}{"a" if u["dst"][0] == "acc" else ""}.{m} {dst}, vf{u["fs"]}, {bs}'


class Compiler:
    def __init__(self, code, start):
        self.code, self.start = code, start
        self.nodes = {}        # (pc, chave do estado) -> id
        self.work = []         # [(id, pc, estado)]
        self.blocks = []       # linhas por no
        self.pcs = set()
        self.linear_issues = []   # ciclos de emissao, so enquanto nao houve desvio (conferencia com o vu0trace)
        self.branched = False

    def node(self, pc, st):
        k = (pc, st.key())
        if k not in self.nodes:
            if len(self.nodes) >= MAX_NODES:
                raise Unsupported('estados demais')
            self.nodes[k] = len(self.nodes)
            self.work.append((self.nodes[k], pc, st.copy()))
        return self.nodes[k]

    def pair(self, p, st, out, elapsed):
        """Emite um par a partir do estado st (modificado). Devolve (ciclos gastos, desvio)."""
        self.pcs.add(p['pc'])
        u, l = p['upper'], p['lower']
        if l is not None:
            l['pc'] = p['pc']
        ur, uw, uar, uaw, uflags = upper_usage(u)
        lr, lw, lvr, lvw, store = lower_usage(l)
        stall = 0
        for reg, lanes in ur[:2] + lr[:2]:
            for c in range(4):
                if lanes & LANES[c]:
                    stall = max(stall, st.vf.get((reg, c), 0))
        for reg in lvr:
            if reg:
                stall = max(stall, st.vi.get(reg, 0))
        for c in range(4):
            if uar & LANES[c]:
                stall = max(stall, st.acc.get(c, 0))
        st.advance(stall)
        while st.flags and st.flags[0][0] <= 0:
            out.append(f'    commit(s, F[{st.flags[0][1]}]);')
            st.flags.pop(0)
        if not self.branched:
            self.linear_issues.append(elapsed + stall)
        lo_txt = f'loi {p["loi"]:#010x}' if p['loi'] is not None else (l['op'] if l else 'nop')
        out.append(f'    {{ // {p["pc"]:04x}: {disasm_upper(u)} | {lo_txt}')
        slot = None
        if uflags:
            used = {sl for _, sl in st.flags}
            slot = next(k for k in range(FLAG_SLOTS) if k not in used)
        writes = emit_upper(u, slot, out) if u is not None else []
        upper_dst = u['dst'][1] if u is not None and u['dst'][0] == 'vf' else None
        lower_writes, branch = emit_lower(l, upper_dst, st, out) if l else ([], None)
        if p['loi'] is not None:
            out.append(f'        s.i = N({flt(p["loi"])});')
        out.extend(writes)
        out.extend(lower_writes)
        out.append('    }')
        # escritas ficam pendentes a partir deste ciclo
        bk = 0
        if l is not None and l['op'] in BRANCH_DELAYED:
            bk = l['vid'] if l['op'] in ('iadd', 'isub', 'iand', 'ior') else l['vit']
        for w in (lw, uw):
            if w:
                for c in range(4):
                    if w[1] & LANES[c]:
                        st.vf[(w[0], c)] = w[2]
                st.pend = max(st.pend, w[2])
        if lvw and lvw[0]:
            st.vi[lvw[0]] = lvw[1]
            st.pend = max(st.pend, lvw[1])
            st.consts.pop(lvw[0], None)
            if l['op'] == 'bal':
                st.consts[lvw[0]] = (p['pc'] + 16) // 8
        for c in range(4):
            if uaw & LANES[c]:
                st.acc[c] = 1
        if uaw:
            st.pend = max(st.pend, 1)
        if slot is not None:
            st.flags.append((4, slot))
            st.pend = max(st.pend, 4)
        if store:
            st.pend = max(st.pend, 1)
        st.bk = bk
        st.advance(1)
        return stall + 1, branch

    def compile_node(self, nid, pc, st):
        out = [f'N{nid}:']
        elapsed = 0
        while True:
            p = decode_pair(self.code, pc)
            spent, branch = self.pair(p, st, out, elapsed)
            elapsed += spent
            if branch is None and not p['ebit']:
                pc += 8
                continue
            if p['ebit'] and branch is not None:
                raise Unsupported(f'desvio com bit E em {pc:#x}')
            # par do delay slot (do desvio ou do bit E)
            dp = decode_pair(self.code, pc + 8)
            if dp['ebit']:
                raise Unsupported(f'bit E no delay slot em {pc + 8:#x}')
            if dp['lower'] is not None and dp['lower']['op'] in ('b', 'bal', 'jr', 'ibeq', 'ibne'):
                raise Unsupported(f'desvio no delay slot em {pc + 8:#x}')
            spent, _ = self.pair(dp, st, out, elapsed)
            elapsed += spent
            if p['ebit']:
                while st.flags:
                    out.append(f'    commit(s, F[{st.flags[0][1]}]);')
                    st.flags.pop(0)
                out.append(f'    finish(s, {(pc + 16) & (CODE_SIZE - 1):#x}u, cyc + {elapsed + st.pend}u);')
                out.append('    return;')
                self.total = elapsed + st.pend
                break
            self.branched = True
            out.append(f'    cyc += {elapsed};')
            kind, target = branch
            if kind == 'cond':
                taken = self.node(target, st)
                fall = self.node(pc + 16, st)
                out.append(f'    if (br) goto N{taken};')
                out.append(f'    goto N{fall};')
            else:
                out.append(f'    goto N{self.node(target, st)};')
            break
        self.blocks.append((nid, out))

    def run(self):
        self.node(self.start, State())
        while self.work:
            nid, pc, st = self.work.pop(0)
            self.compile_node(nid, pc, st)
        self.blocks.sort()
        name = f'vu0_{self.start:04x}'
        targets = set()
        for _, out in self.blocks:
            for ln in out:
                if 'goto N' in ln:
                    targets.add(int(ln.split('goto N')[1].rstrip(';')))
        fn = [f'static void {name}(VU1State &s, uint8_t *d)', '{',
              f'    Fm F[{FLAG_SLOTS}];', '    uint64_t cyc = 0;', '    int32_t bk = 0;', '    bool br = false;',
              '    (void)bk;', '    (void)br;', '    (void)cyc;']
        for nid, out in self.blocks:
            if nid in targets:
                fn.append(out[0])
            fn += out[1:]
        fn += ['}', '']
        lo, hi = min(self.pcs), max(self.pcs) + 8
        return name, lo, hi - lo, fnv1a(self.code[lo:hi]), fn


def fnv1a(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def check_trace(path, linear):
    issues_ref, total_ref, prog = {}, {}, None
    for ln in open(path):
        f = ln.split()
        if f[0] == 'prog':
            prog = int(f[1], 16)
            total_ref[prog] = int(f[3])
            issues_ref[prog] = []
        elif f[0] == 'pair':
            issues_ref[prog].append(int(f[2]))
    bad = n = 0
    for start, (issues, total) in linear.items():
        if start not in issues_ref:
            continue
        n += 1
        if issues != issues_ref[start] or total != total_ref[start]:
            bad += 1
            print(f'  ciclos divergem em {start:#x}: modelo {issues} total {total}; interpretador {issues_ref[start]} total {total_ref[start]}')
    print(f'modelo de ciclos x interpretador: {n} programas, {bad} divergentes')
    return bad == 0


def main():
    args = sys.argv[1:]
    trace = None
    if '--trace' in args:
        k = args.index('--trace')
        trace = args[k + 1]
        del args[k:k + 2]
    code = load_code(args[0])
    out = ['// Gerado por tools/vu0native.py a partir do microcodigo do VU0 do jogo. Nao editar; nao versionar.', '']
    table, linear = [], {}
    for start in ENTRIES:
        comp = Compiler(code, start)
        try:
            name, lo, length, h, fn = comp.run()
        except Unsupported as e:
            print(f'{start:#05x}: fica no interpretador ({e})')
            continue
        out += fn
        table.append(f'    {{{start:#x}u, {lo:#x}u, {length}u, {h:#018x}ull, {name}}},')
        if not comp.branched:
            linear[start] = (comp.linear_issues, comp.total)
        print(f'{start:#05x}: {len(comp.pcs)} pares, {len(comp.blocks)} blocos' + ('' if comp.branched else f', {comp.total} ciclos'))
    out.append('static const WotmVu0Program kPrograms[] = {')
    out += table
    out.append('};')
    out.append('static constexpr size_t kProgramCount = sizeof(kPrograms) / sizeof(kPrograms[0]);')
    # Imagem da memoria de codigo do VU0 que o pacote do ELF deixa (o hierLoadVu0Ucode a manda pelo VIF0 a cada quadro); o
    # executavel do jogo a copia direto, sem VIF0 (BOUNDARY.md, passo 7).
    out.append('#define WOTM_VU0_HAVE_IMAGE 1')
    out.append(f'static const unsigned char kVu0CodeImage[{CODE_SIZE}] = {{')
    for k in range(0, CODE_SIZE, 32):
        out.append('    ' + ','.join(f'{b:#04x}' for b in code[k:k + 32]) + ',')
    out.append('};')
    if trace and not check_trace(trace, linear):
        raise SystemExit(1)
    with open(args[1], 'w', newline='\n') as f:
        f.write('\n'.join(out) + '\n')


main()
