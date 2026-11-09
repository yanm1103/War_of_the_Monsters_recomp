// node tools/level_editor/test_prefab.mjs : the arithmetic of web/prefab_math.js
import assert from 'node:assert/strict';
import { toPrefabEntries, placeEntry } from './web/prefab_math.js';

const near = (a, b, e = 1e-9) => assert.ok(Math.abs(a - b) <= e, `${a} != ${b}`);
const nearAll = (a, b) => a.forEach((x, i) => near(x, b[i]));

// the middle is the box of the positions in X and Y, the base the lowest Z
const e = toPrefabEntries([
  { kind: 'object', handle: '0x1', pos: [100, 200, 10], heading: 0 },
  { kind: 'prop', model: 'BOX', pos: [140, 260, 25], heading: 90, pitch: 10 },
  { kind: 'object', handle: '0x2', pos: [120, 210, 12], heading: 45, on: 0 },
]);
nearAll(e[0].d, [-20, -30, 0]); nearAll(e[1].d, [20, 30, 15]); nearAll(e[2].d, [0, -20, 2]);
assert.equal(e[1].model, 'BOX'); assert.equal(e[1].pitch, 10); assert.equal(e[2].on, 0); assert.equal(e[0].pos, undefined);
assert.deepEqual(toPrefabEntries([]), []);
// a single object sits at the point it is put on
nearAll(toPrefabEntries([{ pos: [5, 6, 7], heading: 3 }])[0].d, [0, 0, 0]);

// put back where it came from, nothing turned: the same positions and headings
const back = e.map((x) => placeEntry(x, [130, 230, 10]));
nearAll(back[0].pos, [110, 200, 10]); nearAll(back[1].pos, [150, 260, 25]);
near(back[1].heading, 90); near(back[2].heading, 45);
// turned by 90 (counterclockwise): +X goes to +Y, headings add, the offsets turn about the middle
let p = placeEntry({ d: [10, 0, 5], heading: 30 }, [0, 0, 0], 90);
nearAll(p.pos, [0, 10, 5]); near(p.heading, 120);
p = placeEntry({ d: [0, 10, 0], heading: 170 }, [100, 100, 20], 90);
nearAll(p.pos, [90, 100, 20]); near(p.heading, -100);   // 260 wraps
// a full turn of four quarters comes back
let q = { d: [7, -3, 1], heading: 15 };
const at = [0, 0, 0];
let pos = q.d.slice(), h = 15;
for (let k = 0; k < 4; k++) { const r = placeEntry({ d: pos, heading: h }, at, 90); pos = [r.pos[0], r.pos[1], pos[2]]; h = r.heading; }
nearAll(pos, [7, -3, 1]); near(h, 15);
console.log('ok');
