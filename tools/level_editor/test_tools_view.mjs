// node tools/level_editor/test_tools_view.mjs : the arithmetic of web/ruler.js (measure, describe) and web/minimap.js (fit, toMap, fromMap)
import assert from 'node:assert/strict';
import { measure, describe } from './web/ruler.js';
import { fit, toMap, fromMap } from './web/minimap.js';

const near = (a, b, e = 1e-9) => assert.ok(Math.abs(a - b) <= e, `${a} != ${b}`);

// the ruler: a 3-4-5 flat, then a rise; the heading as the game counts it (0 faces +Y, 90 +X)
let m = measure([0, 0, 0], [3, 4, 0]);
near(m.distance, 5); near(m.horizontal, 5); near(m.height, 0); near(m.heading, Math.atan2(3, 4) * 180 / Math.PI);
m = measure([10, 10, 0], [10, 10, 12]);
near(m.distance, 12); near(m.horizontal, 0); near(m.height, 12); near(m.heading, 0);
m = measure([0, 0, 5], [100, 0, 5]); near(m.heading, 90);
m = measure([0, 0, 5], [0, -100, 5]); near(m.heading, 180);
m = measure([0, 0, 5], [-100, 0, 5]); near(m.heading, 270);
m = measure([1, 2, 3], [1 + 30, 2 + 40, 3 - 120]);
near(m.horizontal, 50); near(m.height, -120); near(m.distance, 130);
assert.equal(describe(m), '130 units (flat 50, down 120, heading 37°)');
assert.equal(describe(measure([0, 0, 0], [0, 0, 7])), '7 units (flat 0, up 7, heading 0°)');
// the same point twice: nothing to measure but no NaN
m = measure([4, 4, 4], [4, 4, 4]); near(m.distance, 0); near(m.heading, 0);

// the minimap: the stage's middle on the canvas' middle, game +Y up, the longer side fills the canvas less the padding
const b = { min: [-1000, -500], max: [1000, 500] };
const f = fit(b, 220, 220, 10);
near(f.scale, 200 / 2000);
let [px, py] = toMap(f, 0, 0); near(px, 110); near(py, 110);
[px, py] = toMap(f, -1000, 500); near(px, 10); near(py, 60);          // top left of the stage (west, north)
[px, py] = toMap(f, 1000, -500); near(px, 210); near(py, 160);
for (const [x, y] of [[0, 0], [123.4, -321.5], [-1000, 500], [999, 499]]) {
  const [qx, qy] = fromMap(f, ...toMap(f, x, y));
  near(qx, x, 1e-9); near(qy, y, 1e-9);
}
// a square stage in a square canvas
const g = fit({ min: [0, 0], max: [400, 400] }, 220, 220, 10);
near(g.scale, 0.5);
// a stage of no size does not divide by zero
assert.ok(Number.isFinite(fit({ min: [5, 5], max: [5, 5] }, 220, 220).scale));
console.log('ok');
