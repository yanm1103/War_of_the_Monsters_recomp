"""Hexdump de um bloco de submalha: blockdump.py <iso> <arq> <indice da submalha> [bytes]"""
import sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ste_model as S

iso = S.Iso(sys.argv[1]); vol = S.Vol(iso)
b = S.Bundle(vol.get(sys.argv[2]))
e = b.meshes()[0]
t = b.tail_bytes(e)
nv, toff, size = S.find_submeshes(b, e)[int(sys.argv[3])]
n = int(sys.argv[4], 16) if len(sys.argv) > 4 else size
print('nv', nv, 'toff', hex(toff), 'size', hex(size), 'pos', hex(nv * 16), 'nrm@', hex(nv * 16), 'uv@', hex(((nv * 20 + 15) & ~15)))
for i in range(0, n, 16):
    row = t[toff + i: toff + i + 16]
    print('%04x' % i, ' '.join('%02x' % x for x in row))
