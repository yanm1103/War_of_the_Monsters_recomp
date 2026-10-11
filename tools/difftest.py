#!/usr/bin/env python3
"""Differential test of a decompiled function against the retail one, both run in a MIPS emulator (unicorn).

    python3 tools/difftest.py game/Monster updateOnFire__7Monster --args this --ret void --runs 200

For each seed the same pseudo-random machine state (a junk arena that `this`/pointer arguments point into, the `game`
object and so on) is given to the retail function and to the one compiled from src/ with -DNON_MATCHING. Calls to any other
function are intercepted (not executed) and logged. The two runs must agree on: the return value, the calls made (callee
name and arguments, in order) and the final contents of every non-stack byte that was written.

It is evidence, not proof: it only exercises the paths the random state reaches. Functions that use VU0/COP2 or MMI
instructions other than the few patched below cannot be run.
Run inside WSL (needs unicorn and pyelftools in ~/.venvs/wotm, plus mips-linux-gnu binutils).
"""
import argparse
import re
import numpy as np
import random
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import (UC_ARCH_MIPS, UC_HOOK_CODE, UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_WRITE_UNMAPPED,
                     UC_MODE_LITTLE_ENDIAN, UC_MODE_MIPS64, Uc, UcError)
from unicorn import mips_const
from unicorn.mips_const import UC_CPU_MIPS64_5KF
from unicorn.mips_const import (UC_MIPS_REG_0, UC_MIPS_REG_4, UC_MIPS_REG_5, UC_MIPS_REG_6, UC_MIPS_REG_7, UC_MIPS_REG_28,
                                UC_MIPS_REG_29, UC_MIPS_REG_31, UC_MIPS_REG_2, UC_MIPS_REG_PC, UC_MIPS_REG_F0, UC_MIPS_REG_F12,
                                UC_MIPS_REG_F13)

ROOT = Path(__file__).resolve().parent.parent
RETAIL = ROOT / 'disc/SCUS_971.97'
GP = 0x6FF8F0
ARENA = 0x06000000
ARENA_SIZE = 0x200000
STACK_TOP = 0x07000000
STACK_SIZE = 0x40000
ALT_TEXT = 0x08000000
ALT_DATA = 0x08400000
RET_ADDR = 0x00001000
INIT = ''   # --init: python snippet run after the random fill (W(addr, word), W16, W8, THIS, ARENA, STUB), to build structured state
STUB = ARENA + 0x1F0000   # a `jr ra; nop` anywhere


def retail_symbols():
    with open(RETAIL, 'rb') as f:
        elf = ELFFile(f)
        sym = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
        segs = []
        for ph in elf.iter_segments():
            if ph['p_type'] == 'PT_LOAD':
                f.seek(ph['p_offset'])
                segs.append((ph['p_vaddr'], f.read(ph['p_filesz']), ph['p_memsz']))
    return sym, segs


def build_alt(tu, workdir):
    """Compile the TU with NON_MATCHING and link it at ALT_TEXT against the retail symbol addresses."""
    obj = workdir / 'alt.o'
    subprocess.run(['sh', str(ROOT / 'tools/cc.sh'), str(ROOT / f'src/{tu}.cpp'), str(obj), '-DNON_MATCHING'],
                   check=True, capture_output=True, cwd=ROOT)
    und = subprocess.run(['mips-linux-gnu-nm', '-u', str(obj)], capture_output=True, text=True, check=True).stdout.split()
    und = [u for u in und if u not in ('U',)]
    sym, _ = retail_symbols()
    script = workdir / 'alt.ld'
    nl = chr(10)
    # retail addresses go in the script with quoted names: --defsym parses its argument as an expression, which
    # breaks on names like _7Cameras$m_numCameras
    assigns = []
    missing = []
    for u in und:
        m = re.search(r'_([0-9A-Fa-f]{8})$', u)
        if u in sym:
            assigns.append(f'"{u}" = {sym[u]:#x};')
        elif m:                                  # splat data label: the address is part of the name
            assigns.append(f'"{u}" = {int(m.group(1), 16):#x};')
        else:
            missing.append(u)
    script.write_text(nl.join([
        'SECTIONS {',
        f'  . = {ALT_TEXT:#x};',
        '  .text : { *(.text*) }',
        f'  . = {ALT_DATA:#x};',
        '  .rodata : { *(.rodata*) }',
        '  .data : { *(.data*) *(.sdata*) }',
        '  .bss : { *(.sbss*) *(.bss*) *(COMMON) }',
        '}',
        f'_gp = {GP:#x};'] + assigns + ['']))
    cmd = ['mips-linux-gnu-ld', '-EL', '-T', str(script), '--unresolved-symbols=ignore-all', '--no-check-sections', '--noinhibit-exec',
           '-o', str(workdir / 'alt.elf'), str(obj)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit('link failed:\n' + r.stderr[-1500:])
    with open(workdir / 'alt.elf', 'rb') as f:
        elf = ELFFile(f)
        secs = []
        for s in elf.iter_sections():
            if s['sh_flags'] & 2 and s['sh_type'] == 'SHT_PROGBITS':
                secs.append((s['sh_addr'], s.data()))
        adata = {s.name: (s['st_value'], s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols()
                 if s.name and s['st_info']['type'] == 'STT_OBJECT' and s['st_size'] and s.name in sym}
        asym = {s.name: (s['st_value'], s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols()
                if s.name and s['st_info']['type'] == 'STT_FUNC'}
    return secs, asym, missing, adata


def patch_code(buf, base=0, rec=None):
    """R5900-only encodings -> plain MIPS64 equivalents (see module docstring)."""
    out = bytearray(buf)
    for i in range(0, len(out) - 3, 4):
        w = struct.unpack_from('<I', out, i)[0]
        op = w >> 26
        if op == 0x1F:      # sq -> sd (the hook writes the shadow upper half)
            if rec is not None:
                rec[base + i] = ('sq', (w >> 16) & 31, (w >> 21) & 31, struct.unpack('<h', struct.pack('<H', w & 0xFFFF))[0])
            w = (0x3F << 26) | (w & 0x03FFFFFF)
        elif op == 0x1E:    # lq -> ld (the hook loads the shadow upper half)
            if rec is not None:
                rec[base + i] = ('lq', (w >> 16) & 31, (w >> 21) & 31, struct.unpack('<h', struct.pack('<H', w & 0xFFFF))[0])
            w = (0x37 << 26) | (w & 0x03FFFFFF)
        elif op == 0 and (w & 0x3F) in (0x18, 0x19) and ((w >> 11) & 31) != 0:   # mult/multu rd,rs,rt -> mul
            w = (0x1C << 26) | (w & 0x03FFFFC0) | 0x02
        elif op == 0x1C and (w & 0x7FF) == ((0x12 << 6) | 0x29):                   # por -> or
            w = (w & 0x03FFF800) | 0x25
        else:
            continue
        struct.pack_into('<I', out, i, w)
    return bytes(out)


BASIC = {'i': 'i', 'b': 'b', 'f': 'f', 'c': 'i', 's': 'i', 'l': 'i', 'U': None}


def parse_params(rest):
    """gcc 2.x mangled parameter list -> list of difftest arg tokens, or None when unsupported."""
    if rest == 'v':
        return []
    toks = []
    i = 0
    while i < len(rest):
        c = rest[i]
        if c == 'U':
            i += 1
            c = rest[i]
            toks.append('i')
            i += 1
            continue
        if c in 'PRC':
            ptr = False
            while i < len(rest) and rest[i] in 'PRC':
                ptr = ptr or rest[i] in 'PR'
                i += 1
            if i < len(rest) and rest[i].isdigit():
                m = re.match(r'(\d+)', rest[i:])
                n = int(m.group(1))
                i += len(m.group(1)) + n
            elif i < len(rest) and rest[i] == 'A':
                m = re.match(r'A\d+_', rest[i:])
                i += len(m.group(0))
                continue
            else:
                i += 1
            toks.append('p')
            continue
        if c in BASIC and BASIC[c]:
            toks.append(BASIC[c])
            i += 1
            continue
        if c == 'T':
            m = re.match(r'T(\d+)', rest[i:])
            idx = int(m.group(1))
            if idx >= len(toks):
                return None
            toks.append(toks[idx])
            i += len(m.group(0))
            continue
        if c == 'N':
            m = re.match(r'N(\d)(\d)', rest[i:])
            if not m:
                return None
            for _ in range(int(m.group(1))):
                toks.append(toks[int(m.group(2))])
            i += 3
            continue
        return None
    return toks


def spec_from_mangled(sym):
    m = re.match(r'^(\w+?)__(C?)(\d+)(\w+)$', sym)
    if m:
        cls_len = int(m.group(3))
        body = m.group(4)
        rest = body[cls_len:]
        params = parse_params(rest)
        return None if params is None else ['this'] + params
    m = re.match(r'^(\w+?)__F(\w*)$', sym)
    if m:
        return parse_params(m.group(2))
    return None


class Run:
    def __init__(self, name, retail_segs, alt_secs, entry, fn_range, entries, seed, spec, objsize=0x4000, alias=()):
        self.name = name
        self.entries = entries                # addr -> callee name (every known function start)
        self.entry = entry
        self.fn_range = fn_range
        self.calls = []
        self.writes = {}
        self.fault = None
        uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS64 | UC_MODE_LITTLE_ENDIAN)
        uc.ctl_set_cpu_model(UC_CPU_MIPS64_5KF)  # o modelo padrao (R4000) nao tem movz/movn: o teste acaba em pc=0
        self.uc = uc
        self.qw = {}
        self.high = [0] * 32
        self.alias = alias
        for vaddr, data, memsz in retail_segs:
            lo = vaddr & ~0xFFF
            hi = (vaddr + memsz + 0xFFF) & ~0xFFF
            try:
                uc.mem_map(lo, hi - lo)
            except UcError:
                pass
            uc.mem_write(vaddr, patch_code(data, vaddr, self.qw) if vaddr < 0x400000 else data)
        for addr, data in alt_secs:
            lo = addr & ~0xFFF
            hi = (addr + len(data) + 0xFFF) & ~0xFFF
            try:
                uc.mem_map(lo, hi - lo)
            except UcError:
                pass
            uc.mem_write(addr, patch_code(data, addr, self.qw))
        uc.mem_map(0, 0x10000)
        # Every callee outside the function under test becomes `jr ra; nop`: the code hook logs the call and sets the
        # stub return values, and no PC rewriting (which made unicorn fire hooks twice) is needed.
        for a in entries:
            if not (fn_range[0] <= a < fn_range[1]):
                try:
                    uc.mem_write(a, struct.pack('<II', 0x03E00008, 0))
                except UcError:
                    pass
        uc.mem_map(ARENA, ARENA_SIZE)
        uc.mem_map(STACK_TOP - STACK_SIZE, STACK_SIZE)
        rnd = random.Random(seed)
        rs = np.random.RandomState(seed)
        n = ARENA_SIZE // 4
        kind = rs.random_sample(n)
        words = np.empty(n, dtype=np.uint32)
        small = rs.randint(0, 9, n).astype(np.uint32)
        flt = rs.uniform(-100, 100, n).astype(np.float32).view(np.uint32)
        special = np.array([0xFFFFFFFF, 0x7FFF, 0x80000000], dtype=np.uint32)[rs.randint(0, 3, n)]
        lo = 0x20000
        hi = (lo + objsize + 0xFFF) & ~0xFFF
        avail = (lo - 0x1000) + (ARENA_SIZE - 0x1000 - hi)
        r = rs.randint(0, avail, n)
        off = np.where(r < lo - 0x1000, r + 0x1000, r - (lo - 0x1000) + hi)
        ptr = (ARENA + (off & ~3)).astype(np.uint32)
        words = np.where(kind < 0.40, small, np.where(kind < 0.55, flt, np.where(kind < 0.60, special, ptr))).astype(np.uint32)
        uc.mem_write(ARENA, words.tobytes())
        self.rnd = rnd
        self.rets = None
        self.nargs = None
        if INIT:
            uc.mem_write(STUB, struct.pack('<II', 0x03E00008, 0))
            env = {'THIS': ARENA + 0x20000, 'ARENA': ARENA, 'STUB': STUB, 'uc': uc,
                   'W': lambda a, v: uc.mem_write(a, struct.pack('<I', v & 0xFFFFFFFF)),
                   'W16': lambda a, v: uc.mem_write(a, struct.pack('<H', v & 0xFFFF)),
                   'W8': lambda a, v: uc.mem_write(a, struct.pack('<B', v & 0xFF))}
            exec(INIT, env)
            # optional RETS(name, n, args) -> $v0 for the n-th intercepted call (None keeps the default pointer)
            self.rets = env.get('RETS')
            # optional NARGS {callee: number of integer args to compare} (default: from the mangled name, else 1)
            self.nargs = env.get('NARGS')
        for ra_, aa_, sz_ in self.alias:       # variables the TU defines itself start from the retail values
            uc.mem_write(aa_, bytes(uc.mem_read(ra_, sz_)))
        for r in range(32):
            uc.reg_write(UC_MIPS_REG_0 + r, 0)
        uc.reg_write(UC_MIPS_REG_29, STACK_TOP - 0x100)
        uc.reg_write(UC_MIPS_REG_28, GP)
        uc.reg_write(UC_MIPS_REG_31, RET_ADDR)
        uc.mem_write(RET_ADDR, struct.pack('<I', 0) * 4)
        self.hooked = False
        # arguments
        self.setup_args(spec, rnd)

    def setup_args(self, spec, rnd):
        uc = self.uc
        # EE (SN o64-style) ABI: integer args in $a0-$a3, then $t0-$t3
        ireg = [UC_MIPS_REG_4 + i for i in range(8)]
        freg = [UC_MIPS_REG_F12, UC_MIPS_REG_F13]
        ni = nf = 0
        for t in spec:
            if t == 'this':
                val = ARENA + 0x20000
            elif t == 'p':
                val = ARENA + (rnd.randrange(0x1000, ARENA_SIZE - 0x1000) & ~15)
            elif t == 'i':
                val = rnd.randrange(0, 9)
            elif t == 'b':
                val = rnd.randrange(0, 2)
            elif t.startswith('='):                # fixed integer, e.g. =2
                val = int(t[1:], 0)
            elif t == 'f':
                fv = rnd.uniform(-10, 10)
                if nf >= len(freg):
                    raise SystemExit('more than 2 float arguments')
                uc.reg_write(freg[nf], struct.unpack('<I', struct.pack('<f', fv))[0])
                nf += 1
                continue
            else:
                raise SystemExit(f'bad arg spec {t}')
            if ni >= len(ireg):
                raise SystemExit('more than 8 integer arguments (stack arguments are not supported)')
            uc.reg_write(ireg[ni], val)
            ni += 1

    def norm(self, v):
        if v in self.entries and v != 0:
            return 'fn:' + self.entries[v]
        """Pointers into static data that point at a C string are compared by their text (the string lives at a different
        address in the alt image)."""
        if (ALT_DATA <= v < ALT_DATA + 0x200000) or (0x400000 <= v < 0x1000000):
            try:
                raw = bytes(self.uc.mem_read(v, 64))
            except UcError:
                return v
            end = raw.find(bytes([0]))
            if end >= 2 and all(32 <= c or c in (9, 10) for c in raw[:end]) and 127 not in raw[:end]:
                return 'str:' + raw[:end].decode('latin-1')
        return v

    def hook_code(self, uc, addr, size, ud):
        try:
            self._hook_code(uc, addr, size, ud)
        except Exception:
            import traceback
            traceback.print_exc()
            raise

    def _hook_code(self, uc, addr, size, ud):
        q = self.qw.get(addr)
        if q:
            kind, rt, base, off = q
            ea = (uc.reg_read(UC_MIPS_REG_0 + base) + off) & 0xFFFFFFFF
            try:
                if kind == 'sq':
                    uc.mem_write(ea + 8, struct.pack('<Q', self.high[rt] & 0xFFFFFFFFFFFFFFFF))
                else:
                    self.high[rt] = struct.unpack('<Q', bytes(uc.mem_read(ea + 8, 8)))[0] if rt else 0
            except UcError:
                pass
        if addr == self.entry and not self.started:
            self.started = True
            return
        if addr in self.entries and not (self.fn_range[0] <= addr < self.fn_range[1]):
            name = self.entries[addr]
            cs = spec_from_mangled(name)
            if cs is None:
                ni, nf = 1, 0
            else:
                ni = min(4, sum(1 for t in cs if t != 'f'))
                nf = min(2, sum(1 for t in cs if t == 'f'))
            if self.nargs and name in self.nargs:  # e.g. varargs C functions: up to 8 integer args ($a0-$a3, $t0-$t3)
                ni = min(8, self.nargs[name])
            a = tuple(self.norm(uc.reg_read(UC_MIPS_REG_4 + i) & 0xFFFFFFFF) for i in range(ni))
            f12 = uc.reg_read(UC_MIPS_REG_F12) & 0xFFFFFFFF if nf >= 1 else 0
            f13 = uc.reg_read(UC_MIPS_REG_F13) & 0xFFFFFFFF if nf >= 2 else 0
            self.calls.append((name, a, f12, f13))
            n = len(self.calls)
            uc.reg_write(UC_MIPS_REG_2, ARENA + 0x100000 + 0x40 * (n % 64))
            uc.reg_write(UC_MIPS_REG_F0, 0)
            if self.rets:
                v = self.rets(name, n, a)
                if isinstance(v, float):        # float results go to $f0
                    uc.reg_write(UC_MIPS_REG_F0, struct.unpack('<I', struct.pack('<f', v))[0])
                elif v is not None:
                    uc.reg_write(UC_MIPS_REG_2, v & 0xFFFFFFFF)

    def snapshot(self, segs):
        """Final contents of the arena and of the retail data/bss (everything the function could have changed)."""
        out = {'arena': bytes(self.uc.mem_read(ARENA, ARENA_SIZE))}
        hi = max(v + m for v, _, m in segs)
        out['data'] = bytes(self.uc.mem_read(0x400000, hi - 0x400000))
        return out

    def hook_write(self, uc, access, addr, size, value, ud):
        if STACK_TOP - STACK_SIZE <= addr < STACK_TOP:
            return
        for k in range(size):
            self.writes[addr + k] = (value >> (8 * k)) & 0xFF

    def execute(self, max_steps=200000):
        uc = self.uc
        self.started = False
        uc.hook_add(UC_HOOK_CODE, self.hook_code)
        pc = self.entry
        steps = 0
        while True:
            self.redirected = False
            try:
                uc.emu_start(pc, RET_ADDR, count=max_steps)
            except UcError as e:
                pcx = uc.reg_read(UC_MIPS_REG_PC)
                try:
                    w = struct.unpack('<I', bytes(uc.mem_read(pcx, 4)))[0]
                except UcError:
                    w = 0
                self.fault = f'{e} at pc={pcx:#x} insn={w:#010x} calls={len(self.calls)}'
                return False
            pc = uc.reg_read(UC_MIPS_REG_PC)
            if pc == RET_ADDR:
                return True
            steps += 1
            if not self.redirected or steps > 400:
                self.fault = 'did not finish (loop or stop)'
                return False


class Bench:
    """Compiled alt build of one TU plus the retail image; test() runs one function."""

    def __init__(self, tu):
        self.tu = tu
        self.sym, self.segs = retail_symbols()
        with tempfile.TemporaryDirectory() as td:
            self.alt_secs, self.asym, self.missing, self.adata = build_alt(tu, Path(td))
        self.sizes = {}
        with open(RETAIL, 'rb') as f:
            for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols():
                self.sizes[s.name] = s['st_size']
        self.r_entries = {v: k for k, v in self.sym.items() if not k.startswith('.') and v < 0x400000}
        self.a_entries = {v: k for k, (v, _) in self.asym.items()}

    def test(self, func, spec, ret='void', runs=60, verbose=False, alt=None, objsize=0x4000):
        """Returns (agree, differ, skipped)."""
        if func not in self.sym:
            raise SystemExit(f'{func} not in the retail symbol table')
        altname = alt or func
        if altname not in self.asym:
            raise SystemExit(f'{altname} not compiled from src/ (still INCLUDE_ASM?)')
        return run_test(self, func, altname, spec, ret, runs, verbose, objsize)


def run_test(b, func, altname, spec, ret, runs, verbose, objsize=0x4000):
    sym, segs, alt_secs, asym, missing = b.sym, b.segs, b.alt_secs, b.asym, b.missing
    r_entry = sym[func]
    a_entry, a_size = asym[altname]
    r_size = b.sizes[func]
    alias = [(sym[n], av, sz) for n, (av, sz) in b.adata.items()]
    r_entries, a_entries = b.r_entries, b.a_entries

    class O:
        pass
    o = O()
    o.func, o.ret, o.runs, o.verbose = func, ret, runs, verbose
    ok = bad = skipped = 0
    for seed in range(o.runs):
        ra = Run('retail', segs, [], r_entry, (r_entry, r_entry + r_size), r_entries, seed, spec, objsize)
        rb = Run('alt', segs, alt_secs, a_entry, (a_entry, a_entry + a_size), {**r_entries, **a_entries}, seed, spec, objsize, alias)
        # both runs patch the retail code identically; the alt run must also intercept calls into retail code
        rb.entries = {**r_entries, **a_entries}
        fa = ra.execute()
        fb = rb.execute()
        if not (fa and fb):
            skipped += 1
            if o.verbose:
                print(f'seed {seed}: skipped (retail: {ra.fault}; alt: {rb.fault})')
            continue
        diffs = []
        if o.ret == 'int' and (ra.uc.reg_read(UC_MIPS_REG_2) & 0xFFFFFFFF) != (rb.uc.reg_read(UC_MIPS_REG_2) & 0xFFFFFFFF):
            diffs.append(('v0', hex(ra.uc.reg_read(UC_MIPS_REG_2) & 0xFFFFFFFF), hex(rb.uc.reg_read(UC_MIPS_REG_2) & 0xFFFFFFFF)))
        if o.ret == 'float' and (ra.uc.reg_read(UC_MIPS_REG_F0) & 0xFFFFFFFF) != (rb.uc.reg_read(UC_MIPS_REG_F0) & 0xFFFFFFFF):
            diffs.append(('f0', hex(ra.uc.reg_read(UC_MIPS_REG_F0) & 0xFFFFFFFF), hex(rb.uc.reg_read(UC_MIPS_REG_F0) & 0xFFFFFFFF)))
        # callee-saved state must survive the call: retail callers (asm) rely on it
        csv = [getattr(mips_const, f'UC_MIPS_REG_{16 + i}') for i in range(8)] + [getattr(mips_const, 'UC_MIPS_REG_30')] + [UC_MIPS_REG_28, UC_MIPS_REG_29, UC_MIPS_REG_31] + [getattr(mips_const, f'UC_MIPS_REG_F{i}') for i in range(20, 32)]
        for r in csv:
            if ra.uc.reg_read(r) != rb.uc.reg_read(r):
                diffs.append(('callee-saved', r, hex(ra.uc.reg_read(r)), hex(rb.uc.reg_read(r))))
        if ra.calls != rb.calls:
            k = next((i for i in range(min(len(ra.calls), len(rb.calls))) if ra.calls[i] != rb.calls[i]), min(len(ra.calls), len(rb.calls)))
            diffs.append(('calls', f'first difference at call #{k} of {len(ra.calls)}/{len(rb.calls)}', ra.calls[k:k + 2], rb.calls[k:k + 2]))
        sa, sb = ra.snapshot(segs), rb.snapshot(segs)
        # TU-defined variables: the retail run changed the retail copy, the alt run its own copy
        for ra_, aa_, sz_ in alias:
            va = bytes(ra.uc.mem_read(ra_, sz_))
            vb = bytes(rb.uc.mem_read(aa_, sz_))
            if va != vb:
                diffs.append(('variable', hex(ra_), va[:16].hex(), vb[:16].hex()))
            off = ra_ - 0x400000
            if 0 <= off and off + sz_ <= len(sa['data']):
                sa['data'] = sa['data'][:off] + bytes(sz_) + sa['data'][off + sz_:]
                sb['data'] = sb['data'][:off] + bytes(sz_) + sb['data'][off + sz_:]
        for region, base in (('arena', ARENA), ('data', 0x400000)):
            if sa[region] != sb[region]:
                x = np.frombuffer(sa[region], dtype=np.uint8)
                y = np.frombuffer(sb[region], dtype=np.uint8)
                idx = np.nonzero(x != y)[0]
                d = [(hex(base + int(i)), int(x[i]), int(y[i])) for i in idx[:40]]
                diffs.append(('writes', region, f'{len(idx)} bytes differ', d))
        if diffs:
            bad += 1
            if bad <= 3:
                print(f'seed {seed}: MISMATCH {diffs}')
        else:
            ok += 1
    return ok, bad, skipped


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tu')
    ap.add_argument('func')
    ap.add_argument('--args', default='this')
    ap.add_argument('--ret', default='void', choices=['void', 'int', 'float'])
    ap.add_argument('--runs', type=int, default=100)
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--objsize', type=lambda x: int(x, 0), default=0x4000, help='size of the object `this` points at (keeps random pointers out of it)')
    ap.add_argument('--alt', default=None, help='negative control: run this function from src/ instead (should DIFFER)')
    ap.add_argument('--init', default='', help='python snippet building structured state, e.g. "W(THIS+0x34, ARENA+0x40000)"')
    o = ap.parse_args()
    global INIT
    INIT = o.init
    spec = [t for t in o.args.split(',') if t]
    b = Bench(o.tu)
    ok, bad, skipped = b.test(o.func, spec, o.ret, o.runs, o.verbose, o.alt, o.objsize)
    print(f'{o.func}: {ok} agree, {bad} differ, {skipped} skipped of {o.runs} (unresolved symbols: {len(b.missing)})')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
