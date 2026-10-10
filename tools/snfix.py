#!/usr/bin/env python3
"""Rewrite ee-gcc 2.95.2 assembly output so modern GNU as produces the same bytes as SN's ps2eeas.

The retail game was assembled with SN Systems' ps2eeas, which expands some assembler macros
differently from GNU as. We expand those macros ourselves before handing the file to
mips-linux-gnu-as (which we need for INCLUDE_ASM'd splat output). Unknown expansions raise,
so a new case shows up as a build error instead of a silent mismatch.

Rules reproduced:
  * `move` -> `daddu rd,rs,$0`
  * `dli r,0xffffffff` -> `addiu r,$0,-1; dsrl32 r,r,0`
  * symbolic loads/stores/`la` (gcc emits e.g. `lw $3,sym`): ps2eeas decides in one pass. A symbol it
    has already seen defined in .sdata/.sbss is accessed off $gp; any other symbol gets
    `lui`/`%lo` (loads into a GPR use the destination as the temporary, everything else uses $at).
    Inside `.set nomacro` (gcc's filled delay slots) the access must be one instruction, so $gp is used.
    GNU as instead decides at the end of the file, which would make these differ.
  * `__asm__("#SNFIX_SMALL sym")` in the source marks `sym` as already defined in .sbss at that point, for TUs whose
    original defined the variable earlier in the file (we only declare it, the linker script places it).
  * R5900 short-loop errata: a backward conditional branch closing a loop of fewer than 6 instructions
    (label through branch) gets nops inserted before the branch until the loop is 6 long; the closing branch
    keeps its own delay nop (GNU as would move a padding nop into the slot).
  * no hazard nop after `mfc1` (the R5900 interlocks; GNU as would insert one before a use of the register).
  * a hazard nop with a label between producer (`c.cc.s`, `mtc1`) and consumer goes after the label, not before.

    python3 tools/snfix.py in.s out.s
"""
import re
import struct
import sys

INSN = re.compile(r'^(\s*)([a-z][a-z0-9.]*)\s+(.*?)\s*(#.*)?$')
LABEL = re.compile(r'^([A-Za-z_$.][\w$.]*):')
MEM_OPS = {
    # op: (is_load, dest_is_gpr)
    'lb': (1, 1), 'lbu': (1, 1), 'lh': (1, 1), 'lhu': (1, 1), 'lw': (1, 1), 'lwu': (1, 1), 'ld': (1, 1), 'lq': (1, 1),
    'sb': (0, 0), 'sh': (0, 0), 'sw': (0, 0), 'sd': (0, 0), 'sq': (0, 0),
    'lwc1': (1, 0), 'swc1': (0, 0), 'l.s': (1, 0), 's.s': (0, 0),
}
CANON = {'l.s': 'lwc1', 's.s': 'swc1'}
# a bare symbol expression: sym, sym+off, sym-off (no base register)
SYMEXPR = re.compile(r'^([A-Za-z_$.][\w$.]*)\s*([+-]\s*(?:0x[0-9A-Fa-f]+|\d+))?$')


def parse_imm(text):
    text = text.strip()
    neg = text.startswith('-')
    v = int(text.lstrip('-'), 0)
    return -v if neg else v


def expand_dli(rd, value):
    value &= 0xFFFFFFFFFFFFFFFF
    signed = value - (1 << 64) if value >> 63 else value
    if -0x8000 <= signed < 0x8000:
        return [f'addiu {rd},$0,{signed}']
    if 0 <= signed < 0x10000:
        return [f'ori {rd},$0,{signed:#x}']
    if -0x80000000 <= signed < 0x80000000:
        return [f'li {rd},{signed:#x}']  # 32-bit li: same expansion in both assemblers
    if value == 0xFFFFFFFF:
        return [f'addiu {rd},$0,-1', f'dsrl32 {rd},{rd},0']
    lo = value & 0xFFFF
    rest = value & ~0xFFFF
    h = rest.bit_length() - 1
    if rest > 0 and h >= 15 and rest & ((1 << (h - 15)) - 1) == 0:
        # a 16-bit constant (top bit set) shifted left, then ori of the low half: ps2eeas emits ori + dsll/dsll32 [+ ori],
        # e.g. 1 << 36 -> 0x8000 << 21
        k, n = rest >> (h - 15), h - 15
        out = [f'ori {rd},$0,{k:#x}', f'dsll {rd},{rd},{n}' if n < 32 else f'dsll32 {rd},{rd},{n - 32}']
        return out + ([f'ori {rd},{rd},{lo:#x}'] if lo else [])
    raise ValueError(f'snfix: no known SN expansion for dli {rd},{value:#x}')


class Fixer:
    def __init__(self):
        self.section = '.text'
        self.nomacro = False
        self.small = set()  # symbols seen defined in .sdata/.sbss so far
        self.reorder = True
        self.after_asm = False  # previous line was #NO_APP

    def directive(self, line):
        s = line.strip()
        parts = s.split(None, 1)
        d = parts[0] if parts else ''
        if d in ('.text', '.data', '.sdata', '.sbss', '.bss', '.rdata', '.rodata'):
            self.section = d
        elif d == '.section' and len(parts) > 1:
            self.section = parts[1].split(',')[0].strip().strip('"')
        elif d == '.set' and len(parts) > 1:
            if parts[1].strip() == 'nomacro':
                self.nomacro = True
            elif parts[1].strip() == 'macro':
                self.nomacro = False
            elif parts[1].strip() in ('reorder', 'noreorder'):
                self.reorder = parts[1].strip() == 'reorder'

    def sym_access(self, indent, op, reg, expr):
        m = SYMEXPR.match(expr)
        if not m:
            return None
        sym = m.group(1)
        off = (m.group(2) or '').replace(' ', '')
        target = f'{sym}{off}'
        op = CANON.get(op, op)
        if sym in self.small or self.nomacro:
            return [f'{indent}{op}\t{reg},%gp_rel({target})($28)']
        if op == 'la':
            return [f'{indent}lui\t{reg},%hi({target})', f'{indent}addiu\t{reg},{reg},%lo({target})']
        is_load, gpr_dest = MEM_OPS[op]
        tmp = reg if (is_load and gpr_dest) else '$1'
        out = [f'{indent}lui\t{tmp},%hi({target})', f'{indent}{op}\t{reg},%lo({target})({tmp})']
        if tmp == '$1':
            out = [f'{indent}.set\tnoat'] + out + [f'{indent}.set\tat']
        return out

    def fix_line(self, line):
        after_asm = self.after_asm
        if line.strip():
            self.after_asm = line.strip() == '#NO_APP'
        sm = re.match(r'^\s*#SNFIX_SMALL\s+(\S+)', line)
        if sm:
            # source marker: pretend the symbol was defined in .sbss at this point (see hier.cpp)
            self.small.add(sm.group(1))
            return [line]
        lm = LABEL.match(line)
        if lm and self.section in ('.sdata', '.sbss'):
            self.small.add(lm.group(1))
        if line.lstrip().startswith('.'):
            self.directive(line)
            return [line]
        m = INSN.match(line)
        if not m:
            return [line]
        indent, op, args, comment = m.groups()
        if after_asm and self.reorder and op in JUMPS and op != 'jal':
            # gcc leaves the delay slot to the assembler after an asm block; the SN assembler puts a nop there,
            # GNU as would pull the last instruction of the asm into it
            return [f'{indent}.set	noreorder', line, f'{indent}nop', f'{indent}.set	reorder']
        ops = [a.strip() for a in args.split(',')] if args else []
        if op == 'li.s' and len(ops) == 2 and float(ops[1]) != 0.0:
            # ps2eeas loads every float constant as immediates through $at; GNU as would use a .lit4 pool entry
            bits = struct.unpack('<I', struct.pack('<f', float(ops[1])))[0]
            out = [f'{indent}.set	noat', f'{indent}lui	$1,{bits >> 16:#x}']
            if bits & 0xFFFF:
                out.append(f'{indent}ori	$1,$1,{bits & 0xFFFF:#x}')
            return out + [f'{indent}mtc1	$1,{ops[0]}', f'{indent}.set	at']
        if op == 'cvt.w.s' and len(ops) == 2:
            fd, fs = (int(o.lstrip('$f')) for o in ops)
            return [f'{indent}.word	{0x46000024 | fs << 11 | fd << 6:#x}']  # r5900 gas has no cvt.w.s
        if op == 'break' and len(ops) == 1:
            return [f'{indent}break	0,{ops[0]}']  # SN encodes the code in the second field (gcc's divide-by-zero trap)
        if op == 'move' and len(ops) == 2:
            return [f'{indent}daddu\t{ops[0]},{ops[1]},$0']
        if op == 'dli' and len(ops) == 2:
            return [f'{indent}{x}' for x in expand_dli(ops[0], parse_imm(ops[1]))]
        if (op in MEM_OPS or op == 'la') and len(ops) == 2:
            if op == 'la' and self.small.isdisjoint({ops[1]}) and not SYMEXPR.match(ops[1]):
                return [line]
            out = self.sym_access(indent, op, ops[0], ops[1])
            if out is not None:
                if op == 'la' and (ops[1] in self.small or self.nomacro):
                    return [f'{indent}addiu\t{ops[0]},$28,%gp_rel({ops[1]})']
                return out
        return [line]


BRANCHES = {'beq', 'bne', 'beqz', 'bnez', 'blez', 'bgtz', 'bltz', 'bgez', 'beql', 'bnel', 'beqzl', 'bnezl',
            'blezl', 'bgtzl', 'bltzl', 'bgezl', 'bc1t', 'bc1f', 'bc1tl', 'bc1fl', 'bc0t', 'bc0f'}
SHORT_LOOP = 6


def insn_count(op, args):
    if op == 'li':
        try:
            v = parse_imm(args.split(',')[1])
        except (ValueError, IndexError):
            return 2
        return 1 if -0x8000 <= v < 0x10000 else 2
    return 1


JUMPS = BRANCHES | {'b', 'j', 'jal', 'jalr', 'jr', 'bal'}


def pad_short_loops(lines):
    """Insert nops before backward conditional branches that close loops shorter than SHORT_LOOP.

    In `.set reorder` mode the assembler adds the delay-slot nop of a jump/branch itself, so those count 2.
    """
    labels = {}   # label -> instruction index
    count = 0     # instructions emitted so far
    reorder = True
    out = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('.set'):
            arg = stripped.split()[-1]
            if arg in ('reorder', 'noreorder'):
                reorder = arg == 'reorder'
        lm = LABEL.match(line)
        if lm:
            labels[lm.group(1)] = count
        m = INSN.match(line) if not stripped.startswith('.') and not lm else None
        if not m and stripped == 'nop':  # operand-less nops (hazard nops added above) are part of the loop too
            count += 1
        if m:
            op, args = m.group(2), m.group(3)
            target = args.split(',')[-1].strip() if args else ''
            if op in BRANCHES and target in labels:
                length = count - labels[target] + 1
                if length < SHORT_LOOP:
                    for _ in range(SHORT_LOOP - length):
                        out.append('	nop')
                        count += 1
                    if reorder:
                        # GNU as would move one of the padding nops into the delay slot; the SN assembler does not
                        out += ['	.set	noreorder', line, '	nop', '	.set	reorder']
                        count += insn_count(op, args or '') + 1
                        continue
            count += insn_count(op, args or '') + (1 if reorder and op in JUMPS else 0)
        out.append(line)
    return out


FPCMP = re.compile(r'^\s*c\.[a-z]+\.s\s')


def fix_fp_compare_labels(out):
    """ps2eeas puts the hazard nop between a c.cc.s and its bc1* *after* any label in between (a branch target
    lands on the nop); GNU as puts it before the label. Make the nop explicit, after the labels."""
    res = []
    i = 0
    while i < len(out):
        line = out[i]
        if FPCMP.match(line):
            j = i + 1
            labels = []
            while j < len(out) and (LABEL.match(out[j]) or out[j].strip().startswith('.p2align') or not out[j].strip()):
                labels.append(out[j])
                j += 1
            if labels and any(LABEL.match(l) for l in labels):
                k = j
                while k < len(out) and out[k].strip().startswith('.set'):
                    k += 1
                if k < len(out) and re.match(r'^\s*bc1[tf]l?\s', out[k]):
                    res += ['	.set	noreorder', line] + labels + ['	nop', '	.set	reorder']
                    i = j
                    continue
        res.append(line)
        i += 1
    return res


def fix_mtc1_labels(out):
    """Same as fix_fp_compare_labels for an `mtc1` whose FPR is read right after a label (both paths of an if/else
    loading the same constant): ps2eeas puts the hazard nop after the label, GNU as before it."""
    res = []
    reorder = True
    i = 0
    while i < len(out):
        line = out[i]
        stripped = line.strip()
        if stripped.startswith('.set'):
            arg = stripped.split()[-1]
            if arg in ('reorder', 'noreorder'):
                reorder = arg == 'reorder'
        m = INSN.match(line) if reorder and not stripped.startswith(('.', '#')) else None
        if m and m.group(2) == 'mtc1':
            freg = m.group(3).split(',')[-1].strip()
            j = i + 1
            between = []
            while j < len(out) and (LABEL.match(out[j]) or not out[j].strip() or out[j].strip().startswith('#')
                                    or re.match(r'^\s*\.(p2align|set\s+(no)?at)\b', out[j])):
                between.append(out[j])
                j += 1
            n = INSN.match(out[j]) if j < len(out) and not out[j].strip().startswith('.') else None
            if n and any(LABEL.match(l) for l in between):
                ops = [o.strip() for o in n.group(3).split(',')]
                reads = ops if n.group(2).startswith('c.') else ops[1:]
                if freg in reads:
                    res += ['	.set	noreorder', line] + between + ['	nop', '	.set	reorder']
                    i = j
                    continue
        res.append(line)
        i += 1
    return res


def fix_mfc1_hazard(out):
    """The SN assembler does not separate an mfc1 from the next instruction (the R5900 interlocks); in .set reorder
    mode GNU as inserts a nop when that instruction reads the moved register. Put the pair in a noreorder block
    (gcc marks the spot with a `#nop` comment). Only done when the next instruction is not a branch/jump."""
    res = []
    reorder = True
    i = 0
    while i < len(out):
        line = out[i]
        stripped = line.strip()
        if stripped.startswith('.set'):
            arg = stripped.split()[-1]
            if arg in ('reorder', 'noreorder'):
                reorder = arg == 'reorder'
        m = INSN.match(line) if reorder and not stripped.startswith(('.', '#')) else None
        if m and m.group(2) == 'mfc1':
            j = i + 1
            while j < len(out) and out[j].strip().startswith('#'):
                j += 1
            n = INSN.match(out[j]) if j < len(out) and not out[j].strip().startswith(('.', '#')) and not LABEL.match(out[j]) else None
            # a cvt.w.s is emitted as .word (see above): GNU as cannot see what it reads and pads it too
            if (n and n.group(2) not in JUMPS) or (j < len(out) and out[j].strip().startswith('.word')):
                res += ['	.set	noreorder', line, out[j], '	.set	reorder']
                i = j + 1
                continue
        res.append(line)
        i += 1
    return res


def main():
    src, dst = sys.argv[1], sys.argv[2]
    fixer = Fixer()
    out = []
    for n, line in enumerate(open(src, encoding='latin1').read().splitlines(), 1):
        try:
            out.extend(fixer.fix_line(line))
        except ValueError as e:
            sys.exit(f'{src}:{n}: {e}')
    out = fix_fp_compare_labels(out)
    out = fix_mtc1_labels(out)
    out = fix_mfc1_hazard(out)
    out = pad_short_loops(out)
    out += ['	.text', '	.align 3']  # retail text objects end 8-aligned (the padding is nops, not part of the next object)
    open(dst, 'w', encoding='latin1', newline='\n').write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
