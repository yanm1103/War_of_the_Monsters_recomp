// node tools/level_editor/test_arrange.mjs : the arithmetic of web/arrange_math.js
import assert from 'node:assert/strict';
import { rowPlacers, circlePlacers, circleAngle, mirrorPlacer, alignShifts, spreadShifts, middleOf, wrapDeg } from './web/arrange_math.js';

const near = (a, b, e = 1e-9) => assert.ok(Math.abs(a - b) <= e, `${a} != ${b}`);
const nearAll = (a, b) => a.forEach((x, i) => near(x, b[i]));

// row: count 4 = the original and 3 copies; step and turn grow with the copy's number
let r = rowPlacers(4, [10, 0, 5], 30);
assert.equal(r.length, 3);
let [p, h] = r[1]([1, 2, 3], 0);
nearAll(p, [21, 2, 13]); near(h, 60);
assert.equal(rowPlacers(1, [1, 1, 1]).length, 0);
near(rowPlacers(2, [0, 0, 0], 350)[0]([0, 0, 0], 20)[1], 10);   // 370 wraps to 10

// circle: a full turn of 4 puts the copies a quarter apart, counterclockwise (+X turns to +Y)
near(circleAngle(4, 360), 90); near(circleAngle(5, 180), 45); near(circleAngle(1, 360), 0);
let c = circlePlacers(4, 360, [0, 0], true);
assert.equal(c.length, 3);
[p, h] = c[0]([100, 0, 7], 10); nearAll(p, [0, 100, 7]); near(h, 100);
[p, h] = c[1]([100, 0, 7], 10); nearAll(p, [-100, 0, 7]); near(h, -170);
[p, h] = c[2]([100, 0, 7], 10); nearAll(p, [0, -100, 7]);
// an arc ends on its last copy: 3 in 180 degrees = original, +90, +180
c = circlePlacers(3, 180, [0, 0], false);
[p, h] = c[1]([100, 0, 0], 10); nearAll(p, [-100, 0, 0]); near(h, 10);
// off-center
[p] = circlePlacers(2, 360, [10, 10], true)[0]([20, 10, 0], 0); nearAll(p, [0, 10, 0]);

// mirror: X swaps left and right, Y front and back; mirroring twice is the identity
[p, h] = mirrorPlacer('x', 5)([8, 2, 1], 30); nearAll(p, [2, 2, 1]); near(h, -30);
[p, h] = mirrorPlacer('y', 5)([8, 2, 1], 30); nearAll(p, [8, 8, 1]); near(h, 150);
for (const ax of ['x', 'y']) {
  const m = mirrorPlacer(ax, 3.5);
  const [p2, h2] = m(...m([7, -4, 2], 40));
  nearAll(p2, [7, -4, 2]); near(wrapDeg(h2 - 40), 0);
}

// align
const B = (x0, x1, y0 = 0, y1 = 1) => ({ min: [x0, y0, 0], max: [x1, y1, 1] });
let bx = [B(0, 10), B(5, 25), B(-4, 2)];
assert.deepEqual(alignShifts(bx, 0, 'min'), [-4, -9, 0]);
assert.deepEqual(alignShifts(bx, 0, 'max'), [15, 0, 23]);
assert.deepEqual(alignShifts(bx, 0, 'mid'), [5.5, -4.5, 11.5]);   // the group spans -4..25, middle 10.5
assert.deepEqual(alignShifts([B(0, 1)], 0, 'max'), [0]);

// spread: the middles even
bx = [B(0, 2), B(3, 5), B(40, 42), B(9, 11)];   // middles 1, 4, 41, 10
let s = spreadShifts(bx, 0);
const mids = bx.map((b, i) => (b.min[0] + b.max[0]) / 2 + s[i]).sort((a, b) => a - b);
nearAll(mids, [1, 1 + 40 / 3, 1 + 80 / 3, 41]);
assert.equal(s[0], 0); assert.equal(s[2], 0);   // the outer two stay
assert.deepEqual(spreadShifts([B(0, 1), B(5, 6)], 0), [0, 0]);   // two: nothing to do
// equal gaps: boxes of different widths
bx = [B(0, 10), B(12, 14), B(30, 32), B(60, 70)];   // used 10+2+2+10 = 24, span 70, gap (70-24)/3
s = spreadShifts(bx, 0, true);
const g = (70 - 24) / 3;
nearAll(bx.map((b, i) => b.min[0] + s[i]), [0, 10 + g, 12 + 2 * g, 60]);
// the order given does not matter
s = spreadShifts([bx[3], bx[0], bx[2], bx[1]], 0, true);
near(s[1], 0); near(s[0], 0);

// the middle of the group
nearAll(middleOf([B(0, 10, 0, 4), B(20, 30, -6, 2)]), [15, -1]);
console.log('ok');
