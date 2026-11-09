// node tools/level_editor/test_tilt.mjs : the arithmetic of web/tilt_math.js (a new prop's heading, pitch and roll)
import assert from 'node:assert/strict';
import { propRows, propAngles } from './web/tilt_math.js';

const near = (a, b, e = 1e-9) => assert.ok(Math.abs(a - b) <= e, `${a} != ${b}`);
const nearAll = (a, b) => a.forEach((x, i) => near(x, b[i]));
const apply = (v, rows) => [0, 1, 2].map((c) => v[0] * rows[0][c] + v[1] * rows[1][c] + v[2] * rows[2][c]);   // v * M, the game's row vectors

// no tilt: the plain heading matrix a prop has always had (counterclockwise seen from above)
nearAll(propRows(0, 0, 0).flat(), [1, 0, 0, 0, 1, 0, 0, 0, 1]);
nearAll(apply([1, 0, 0], propRows(90, 0, 0)), [0, 1, 0]);
nearAll(apply([0, 1, 0], propRows(90, 0, 0)), [-1, 0, 0]);
// pitch tips the nose (+Y) up; roll leans the top (+Z) toward +X
nearAll(apply([0, 1, 0], propRows(0, 90, 0)), [0, 0, 1]);
nearAll(apply([0, 0, 1], propRows(0, 90, 0)), [0, -1, 0]);
nearAll(apply([0, 0, 1], propRows(0, 0, 90)), [1, 0, 0]);
nearAll(apply([1, 0, 0], propRows(0, 0, 90)), [0, 0, -1]);
// the heading comes after the tilt, about the vertical: the tipped nose then points around the compass
nearAll(apply([0, 1, 0], propRows(90, 90, 0)), [0, 0, 1]);
nearAll(apply([0, 1, 0], propRows(90, 45, 0)), [-Math.SQRT1_2, 0, Math.SQRT1_2]);
// it is a rotation: orthonormal rows, determinant 1
for (const [h, p, r] of [[30, 20, -15], [-170, 135, 60], [0, -90, 0]]) {
  const m = propRows(h, p, r);
  for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) near(m[i].reduce((s, x, k) => s + x * m[j][k], 0), i === j ? 1 : 0);
  const det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  near(det, 1);
}
// the angles come back out of the matrix (roll within -89..89, the heading wrapped to -180..180)
let n = 0;
for (let h = -175; h <= 175; h += 35) for (let p = -180; p < 180; p += 25) for (let r = -85; r <= 85; r += 17) {
  const a = propAngles(propRows(h, p, r));
  const wrap = (x) => ((x + 180) % 360 + 360) % 360 - 180;
  near(wrap(a.h - h), 0, 1e-7); near(wrap(a.p - p), 0, 1e-7); near(a.r, r, 1e-7);
  n++;
}
console.log(`ok (${n} angle triples)`);
