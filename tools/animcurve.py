"""Avaliador de curvas de animacao do WotM (porte de src/common/AnimCurve.cpp; formato em docs/FORMATS.md, "Animacao").

`Curve(mem, endereco).eval(t)`: `mem` e qualquer leitor com u8/u16/s16/f32 por endereco (ngp.Image, um dump de RAM...).
Conferido em 2026-10-20 contra os canais (`animOutput.val`) que o jogo calculou numa luta: 128 de 130 canais iguais (os 2
restantes, 0,007 de diferenca, vem de uma camada de mistura por cima). A busca da chave e linear (o jogo usa busca binaria
com cache por canal; o resultado e o mesmo)."""
import math

FMT = {0: (4, "f"), 1: (2, "H"), 2: (1, "B")}


def slope(s):
    hi = s >> 14
    if hi == -2:
        return 16384.0 / (-32768.0 - s)
    if hi == 1:
        return 16384.0 / (32768.0 - s)
    return s * (1.0 / 16384.0)


class Curve:
    def __init__(self, mem, a):
        self.m, self.a = mem, a
        h = mem.u16(a)
        self.ctype, self.dtype = h & 7, (h >> 3) & 7
        self.pre, self.post = (h >> 6) & 7, (h >> 9) & 7
        self.idx = mem.u16(a + 2)
        if self.ctype == 3:
            return
        self.n = mem.s16(a + 4)
        self.keys = a + (8 if self.dtype == 0 else 6)
        self.norm = (mem.f32(a - 16), mem.f32(a - 12), mem.f32(a - 8), mem.f32(a - 4))
        tsz = FMT[self.dtype][0]
        ssz = 4 if self.dtype == 0 else 2
        # tamanho da chave com alinhamento (C++ do retail): float alinha em 4, short em 2
        if self.ctype == 2:
            self.ksz = tsz
        elif self.ctype == 5:
            self.ksz = 2 * tsz
        elif self.ctype == 4:
            self.ksz = 12 if self.dtype == 0 else (6 if self.dtype == 1 else 4)
        elif self.ctype == 1:
            self.ksz = 16 if self.dtype == 0 else (8 if self.dtype == 1 else 6)
        elif self.ctype == 0:
            self.ksz = 20
        self.tsz, self.ssz = tsz, ssz

    def raw(self, o):
        if self.dtype == 0:
            return self.m.f32(o)
        if self.dtype == 1:
            return self.m.u16(o)
        return self.m.u8(o)

    def rt(self, i):   # tempo cru da chave
        return self.raw(self.keys + i * self.ksz)

    def rv(self, i):
        return self.raw(self.keys + i * self.ksz + (0 if self.ctype == 2 else self.tsz))

    def T(self, i):
        bt, dt, _, _ = self.norm
        if self.ctype == 2:
            return i * dt + bt
        r = self.rt(i)
        return r if self.dtype == 0 else r * dt + bt

    def V(self, i):
        _, _, bv, dv = self.norm
        r = self.rv(i)
        return r if self.dtype == 0 else r * dv + bv

    def S(self, i, k=0):
        o = self.keys + i * self.ksz + 2 * self.tsz + k * self.ssz
        return self.m.f32(o) if self.dtype == 0 else slope(self.m.s16(o))

    def find(self, t):
        bt, dt, _, _ = self.norm
        if self.ctype == 2:
            return int((t - bt) / dt)
        if self.dtype == 0:
            kt = t
        else:
            kt = int((t - bt) / dt)
        lo = 0
        for i in range(self.n - 1):
            if self.rt(i) <= kt:
                lo = i
            else:
                break
        return lo

    def between(self, t, i):
        bt, dt, _, _ = self.norm
        c = self.ctype
        if c == 2:
            v0, v1 = self.V(i), self.V(i + 1)
            return v0 + (v1 - v0) * ((t - bt) / dt - i)
        if c == 5:
            return self.V(i)
        if c == 0:
            o = self.keys + i * self.ksz
            x = t - self.m.f32(o)
            c0, c1, c2 = self.m.f32(o + 8), self.m.f32(o + 12), self.m.f32(o + 16)
            return x * (x * (x * c0 + c1) + c2) + self.m.f32(o + 4)
        if c == 1:
            t0, v0 = self.T(i), self.V(i)
            dv = self.V(i + 1) - v0
            m0, m1 = self.S(i, 0), self.S(i, 1)
            s = (t - t0) / (self.T(i + 1) - t0)
            return s * (s * (s * (m0 + m1 - 2 * dv) + (3 * dv - (2 * m0 + m1))) + m0) + v0
        if c == 4:
            t0, v0 = self.T(i), self.V(i)
            dtt = self.T(i + 1) - t0
            dv = self.V(i + 1) - v0
            m0, m1 = self.S(i), self.S(i + 1)
            inv = 1.0 / dtt
            a, b = m0 * dtt, m1 * dtt
            inv2 = inv * inv
            x = t - t0
            return x * (x * (x * ((a + b - 2 * dv) * (inv2 * inv)) + (3 * dv - (2 * a + b)) * inv2) + m0) + v0
        raise ValueError(c)

    def eval(self, t):
        if self.ctype == 3:
            return self.m.f32(self.a + 4)
        off = 0.0
        last = self.n - 1
        first, end = self.T(0), self.T(last)
        if t < first:
            dist, rng = first - t, end - first
            p = self.pre
            if p == 0:
                return self.V(0)
            if p == 1:
                return 0.0
            if p in (2, 3):
                n = int(dist / rng) + 1
                t += n * rng
                if p == 3:
                    off = (self.V(0) - self.V(last)) * n
            elif p == 4:
                n = int(dist / rng)
                t = t + (n + 1) * rng if n & 1 else dist - n * rng
        elif end <= t:
            rng, dist = end - first, t - end
            p = self.post
            if p == 0:
                return self.V(last)
            if p == 1:
                return 0.0
            if p in (2, 3):
                n = int(dist / rng) + 1
                t -= n * rng
                if p == 3:
                    off = (self.V(last) - self.V(0)) * n
            elif p == 4:
                n = int(dist / rng)
                t = t - (n + 1) * rng if n & 1 else (end - t) + n * rng
        return self.between(t, self.find(t)) + off
