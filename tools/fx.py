#!/usr/bin/env python3
"""Particle systems (common/particle) read from an EE RAM dump, and the effect presets of the ELF.

    python3 tools/fx.py ram_3300.bin            # active systems: preset, type, count, blend, colors, texture, first particles
    python3 tools/fx.py ram_3300.bin -v         # plus every particle (pos, size, age, color)
    python3 tools/fx.py --presets               # the 151 presets (VisualInfo/ParticleInfo/EmitterInfo) from disc/SCUS_971.97

Preset and texture names come from the game's own enums FxId and FxTextureId (include/hieri_types.h).

Same reading as native/wotm_fx.hpp (layout: include/hieri_types.h `_ParticleType`; notes: docs/FORMATS.md "Particulas").
"""
import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FX, STRIDE, COUNT = 0x5BA280, 0x2EE0, 100
FXLIST, ACTIVE_TOP = 0x6DF200, 0x6F88EC
TEXTURE_INFO, TEX_ID, TEX0 = 0x5B9E80, 0x6DFAB0, 0x6DF6B0
COLORS = 0x256C10
VISUAL_INFO, FIELD_INFO, EMITTER_INFO, PARTICLE_INFO, NPRESETS = 0x5ADD00, 0x5AEFF0, 0x5B2020, 0x5B4F70, 151
TYPES = {1: 'line', 2: 'sweepTrail', 3: 'sprite', 4: 'texSprite', 5: 'stripe', 6: 'strite', 8: 'sprine', 9: 'kite', 13: 'frag',
         15: 'dustTrail', 16: 'wake'}
BLEND = {0x44: 'alpha', 0x48: 'add', 0: 'opaque'}


def enum_names(name):
    """Names of a game enum from the stabs-derived header (FxId: the presets, FxTextureId: the TextureInfo entries)."""
    m = re.search(r'enum %s \{([^}]*)\}' % name, (ROOT / 'include/hieri_types.h').read_text())
    out = {}
    for part in (m.group(1).split(',') if m else []):
        k, _, v = part.strip().partition(' = ')
        if v:
            out[int(v)] = k
    return out


FX_ID = enum_names('FxId')
FX_TEX = enum_names('FxTextureId')


class Mem:
    def __init__(self, data, base=0):
        self.d, self.base = data, base

    def ok(self, a, n=4):
        return 0x1000 <= a and a - self.base + n <= len(self.d) and a >= self.base

    def u32(self, a):
        return struct.unpack_from('<I', self.d, a - self.base)[0] if self.ok(a) else 0

    def s32(self, a):
        return struct.unpack_from('<i', self.d, a - self.base)[0] if self.ok(a) else 0

    def s16(self, a):
        return struct.unpack_from('<h', self.d, a - self.base)[0] if self.ok(a, 2) else 0

    def u16(self, a):
        return struct.unpack_from('<H', self.d, a - self.base)[0] if self.ok(a, 2) else 0

    def f32(self, a):
        return struct.unpack_from('<f', self.d, a - self.base)[0] if self.ok(a) else 0.0


def elf_mem():
    """The ELF's PT_LOAD segments laid out at their addresses (for the presets and the color table)."""
    from elftools.elf.elffile import ELFFile
    with open(ROOT / 'disc/SCUS_971.97', 'rb') as f:
        elf = ELFFile(f)
        segs = [(s['p_vaddr'], s.data()) for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
    lo = min(v for v, _ in segs)
    hi = max(v + len(d) for v, d in segs)
    buf = bytearray(hi - lo)
    for v, d in segs:
        buf[v - lo:v - lo + len(d)] = d
    return Mem(bytes(buf), lo)


def color_at(m, b, age):
    """loadColors: t = age * colorsPerFrame (4.12); lerp between colors[t >> 12] and the next."""
    nc, tab = m.u32(b + 0x2E9C), COLORS + 4 * m.u32(b + 0x2EA0)
    if nc == 0:
        return 0x80808080
    if nc == 1:
        return m.u32(tab)
    t = max(age, 0) * m.u32(b + 0x2E78)
    k, f = t >> 12, t & 0xFFF
    if k >= nc - 1:
        k, f = nc - 2, 0x1000
    c0, c1 = m.u32(tab + 4 * k), m.u32(tab + 4 * k + 4)
    out = 0
    for sh in range(0, 32, 8):
        a, z = (c0 >> sh) & 255, (c1 >> sh) & 255
        out |= ((a + (((z - a) * f) >> 12)) & 255) << sh
    return out


def systems(m):
    i, seen = m.u32(ACTIVE_TOP), 0
    while i < COUNT and seen < COUNT:
        yield i, FX + STRIDE * i
        i = m.u32(FXLIST + 12 * i + 8)
        seen += 1


def list_dump(path, verbose):
    m = Mem(Path(path).read_bytes())
    n = 0
    for i, b in systems(m):
        n += 1
        typ, cnt, attr = m.u32(b + 0x2E90), m.s32(b + 0x2ED0), m.u32(b + 0x2E30)
        alpha, ti = m.u32(b + 0x2EA8), m.u32(b + 0x2E98) & 127
        tinfo = TEXTURE_INFO + 8 * ti
        ntex, tidx = m.s16(tinfo), m.u16(tinfo + 2)
        texs = [m.u16(TEX_ID + 2 * ((tidx + k) & 127)) for k in range(max(0, min(ntex, 8)))]
        drawn = bool(attr & 0x400) and typ != 11 and cnt >= (2 if typ == 8 else 1)
        e = [m.f32(b + 0x2CA0 + 4 * c) for c in range(3)]
        fid = m.u32(b + 0x2DDC)
        print(f'#{i:2d} preset {fid:3d} {FX_ID.get(fid, "?")} {TYPES.get(typ, typ)!s:10} n={cnt:3d}/{m.s32(b + 0x2ED4):3d} '
              f'{"desenha" if drawn else "nao desenha"} blend={BLEND.get(alpha, hex(alpha))} '
              f'cores={m.u32(b + 0x2E9C)}@{m.u32(b + 0x2EA0)} cpf={m.u32(b + 0x2E78)} vida={m.s16(b + 0x2E4A)} '
              f'texInfo={ti} {FX_TEX.get(ti, "")} (n={ntex} idx={tidx} texId={texs}) emit=({e[0]:.0f},{e[1]:.0f},{e[2]:.0f})')
        pos, age = m.u32(b + 0x2EB0), m.u32(b + 0x2EB8)
        for k in range(cnt if verbose else min(cnt, 2)):
            p = [m.f32(pos + 16 * k + 4 * c) for c in range(4)]
            a = m.s16(age + 2 * k)
            print(f'      [{k:3d}] pos ({p[0]:.1f},{p[1]:.1f},{p[2]:.1f}) tamanho {p[3]:.2f} idade {a} cor {color_at(m, b, a):08x}')
    print(f'{n} sistemas ativos')


def list_presets():
    m = elf_mem()
    for i in range(NPRESETS):
        v, p, e = VISUAL_INFO + 32 * i, PARTICLE_INFO + 96 * i, EMITTER_INFO + 80 * i
        typ, nc, ci = m.u32(v), m.u32(v + 12), m.u32(v + 16)
        cols = ' '.join(f'{m.u32(COLORS + 4 * (ci + k)):08x}' for k in range(min(nc, 3)))
        print(f'{i:3d} {FX_ID.get(i, "?")!s:24} {TYPES.get(typ, typ)!s:10} tam {m.f32(p + 8):6.1f}..{m.f32(p + 12):6.1f} exp {m.f32(p + 0x10):6.2f}/{m.f32(p + 0x14):6.2f} '
              f'vida {m.s16(p + 0x1A):5d} blend={BLEND.get(m.u32(v + 0x18), hex(m.u32(v + 0x18)))} tex={FX_TEX.get(m.u32(v + 8), m.u32(v + 8))} '
              f'cores={nc}@{ci} [{cols}] taxa {m.f32(e + 0xC):.1f} dur {m.s16(e + 6)}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('ram', nargs='?')
    ap.add_argument('-v', action='store_true', help='every particle')
    ap.add_argument('--presets', action='store_true')
    o = ap.parse_args()
    if o.presets:
        list_presets()
    elif o.ram:
        list_dump(o.ram, o.v)
    else:
        ap.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()
