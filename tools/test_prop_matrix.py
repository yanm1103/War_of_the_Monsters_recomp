"""python tools/test_prop_matrix.py : lvl.prop_matrix, the ROTATE matrix of a prop with heading, pitch and roll.
The same cases as tools/level_editor/test_tilt.mjs, which runs the editor's copy (web/tilt_math.js)."""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lvl


def apply(v, m):
    return [sum(v[i] * m[i][c] for i in range(3)) for c in range(3)]


def near(a, b, tol=1e-9):
    assert all(abs(x - y) <= tol for x, y in zip(a, b)), "%s != %s" % (a, b)


def main():
    pos = (10.0, 20.0, 30.0)
    m = lvl.prop_matrix(pos, 0, 0, 0)
    near([x for r in m[:3] for x in r[:3]], [1, 0, 0, 0, 1, 0, 0, 0, 1])
    assert m[3] == (10.0, 20.0, 30.0, 1.0) and all(r[3] == 0.0 for r in m[:3])
    # a plain heading matrix is what the hook has always written
    ch, sh = math.cos(math.radians(37)), math.sin(math.radians(37))
    near([x for r in lvl.prop_matrix(pos, 37)[:3] for x in r[:3]], [ch, sh, 0, -sh, ch, 0, 0, 0, 1])
    near(apply([0, 1, 0], lvl.prop_matrix(pos, 0, 90)), [0, 0, 1])        # pitch: the nose goes up
    near(apply([0, 0, 1], lvl.prop_matrix(pos, 0, 0, 90)), [1, 0, 0])     # roll: the top goes toward +X
    near(apply([0, 1, 0], lvl.prop_matrix(pos, 90, 45)), [-math.sqrt(0.5), 0, math.sqrt(0.5)])   # the heading comes after the tilt
    for h, p, r in ((30, 20, -15), (-170, 135, 60)):
        m = lvl.prop_matrix(pos, h, p, r)
        for i in range(3):
            for j in range(3):
                near([sum(m[i][k] * m[j][k] for k in range(3))], [1 if i == j else 0])
    print("ok")


main()
