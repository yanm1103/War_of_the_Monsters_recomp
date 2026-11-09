// node tools/level_editor/test_marker_math.mjs : the arithmetic of web/marker_math.js (the size of a marker, the radius of a click, which
// marker a click picks, which labels are drawn)
import assert from 'node:assert/strict';
import { markerScale, BOOST_AT, BOOST_MAX, pickRadius, MIN_PICK, MAX_PICK, pickBest, layoutLabels, labelWidth, LABEL_H } from './web/marker_math.js';

let n = 0;
const t = (name, fn) => { fn(); n++; console.log('ok    ' + name); };

t('a marker keeps its size up close and grows with the distance, to a limit', () => {
  assert.equal(markerScale(0), 1);
  assert.equal(markerScale(BOOST_AT), 1);
  assert.equal(markerScale(BOOST_AT * 2), 2);
  assert.equal(markerScale(1e9), BOOST_MAX);
  assert.ok(markerScale(1200) < markerScale(1800));
});

t('the radius of a click is the marker plus a margin, between 12 and 64 px', () => {
  assert.equal(pickRadius(0), MIN_PICK);
  assert.equal(pickRadius(3), MIN_PICK);
  assert.equal(pickRadius(20), 26);
  assert.equal(pickRadius(500), MAX_PICK);
});

t('a near marker takes clicks over its whole body, a far one still takes a click near it', () => {
  // zoomed in: the cone's centre is 81 px from its tip, the radius on the screen about 100 px
  assert.equal(pickBest([{ i: 7, d: 49, rPx: 100 }]), 7);
  assert.equal(pickBest([{ i: 7, d: 63, rPx: 100 }]), 7);
  assert.equal(pickBest([{ i: 7, d: 120, rPx: 100 }]), -1);
  // far: a marker of 3 px takes a click 11 px off, not 13
  assert.equal(pickBest([{ i: 1, d: 11, rPx: 3 }]), 1);
  assert.equal(pickBest([{ i: 1, d: 13, rPx: 3 }]), -1);
});

t('of several markers that reach the click, the one it is nearest to the surface of wins', () => {
  // a big marker whose centre is 40 px off (surface 10 px inside) against a small one 8 px off (surface 5 px outside)
  assert.equal(pickBest([{ i: 0, d: 40, rPx: 50 }, { i: 1, d: 8, rPx: 3 }]), 0);
  // two equal ones: the nearer centre
  assert.equal(pickBest([{ i: 0, d: 9, rPx: 5 }, { i: 1, d: 6, rPx: 5 }]), 1);
  assert.equal(pickBest([{ i: 0, d: 4, rPx: 5 }, { i: 1, d: 9, rPx: 5 }]), 0);
  assert.equal(pickBest([]), -1);
});

const L = (key, x, y, prio = 3, text = 'CPU 1') => ({ key, text, x, y, prio });
t('labels that overlap are not both drawn, the lower prio stays', () => {
  const keep = layoutLabels([L('a', 100, 100, 3), L('b', 110, 104, 1)], 800, 600);
  assert.deepEqual([...keep.keys()], ['b']);
  const apart = layoutLabels([L('a', 100, 100), L('b', 100 + labelWidth('CPU 1') + 2, 100)], 800, 600);
  assert.equal(apart.size, 2);
  const stacked = layoutLabels([L('a', 100, 100), L('b', 100, 100 + LABEL_H)], 800, 600);
  assert.equal(stacked.size, 2);
});

t('a label outside the view is not drawn', () => {
  const keep = layoutLabels([L('in', 400, 300), L('left', -400, 300), L('below', 400, 900), L('above', 400, -5), L('right', 1200, 300)], 800, 600);
  assert.deepEqual([...keep.keys()], ['in']);
});

t('the plain labels are capped, the hovered and the selected are not', () => {
  const items = [];
  for (let i = 0; i < 20; i++) items.push(L('p' + i, 30 + i * 70, 200, 3 + i / 100, String(i)));
  items.push(L('sel', 30, 400, 1, 'Node 3'), L('hot', 200, 400, 0, 'Node 4'));
  const keep = layoutLabels(items, 2000, 600, 5);
  assert.equal([...keep.keys()].filter((k) => k.startsWith('p')).length, 5);
  assert.ok(keep.has('sel') && keep.has('hot'));
  assert.ok(keep.has('p0') && !keep.has('p19'));
});

t('labels that ask to stack go above the one in the way, up to three steps; the others are dropped', () => {
  const S = (key, prio) => ({ key, text: 'CPU 1', x: 100, y: 100, prio, stack: true });
  const keep = layoutLabels([S('a', 3), S('b', 3.1), S('c', 3.2), S('d', 3.3), S('e', 3.4)], 800, 600);
  assert.deepEqual([...keep.entries()], [['a', 0], ['b', -(LABEL_H + 1)], ['c', -2 * (LABEL_H + 1)], ['d', -3 * (LABEL_H + 1)]]);
  assert.equal(layoutLabels([L('a', 100, 100), L('b', 100, 100, 3.1)], 800, 600).size, 1);
});

console.log(`test_marker_math: ${n} ok`);
