// The arithmetic of the Arrange section (web/arrange.js, app.js), in the game's axes (X right, Y forward, Z up; the heading turns
// counterclockwise seen from above, 0 = +Y). Nothing here touches the page, so test_arrange.mjs runs it with node.
//
// A "placer" maps where an object is, ([x, y, z], heading), to where one of its copies goes, the same pair. The row, circle and mirror
// make a list of placers (one per copy); align and spread make a shift per object.

export const wrapDeg = (a) => ((a + 180) % 360 + 360) % 360 - 180;
const rad = (d) => d * Math.PI / 180;

// count includes the original: 5 means the original and 4 copies. step: [dx, dy, dz] between neighbours; turn: degrees each copy adds
export function rowPlacers(count, step, turn = 0) {
  const out = [];
  for (let k = 1; k < count; k++) out.push((p, h) => [[p[0] + step[0] * k, p[1] + step[1] * k, p[2] + step[2] * k], wrapDeg(h + turn * k)]);
  return out;
}

// count includes the original. A full circle (arc 360) spaces them 360/count apart; an arc under 360 puts the last copy at its end.
// center: [x, y]. turn: the copies turn with the circle (as the hands of a clock do), else they keep their heading.
export function circleAngle(count, arc) {
  return count < 2 ? 0 : Math.abs(arc) >= 360 ? arc / count : arc / (count - 1);
}
export function circlePlacers(count, arc, center, turn = true) {
  const a = circleAngle(count, arc), out = [];
  for (let k = 1; k < count; k++) {
    const t = rad(a * k), c = Math.cos(t), s = Math.sin(t);
    out.push((p, h) => {
      const dx = p[0] - center[0], dy = p[1] - center[1];
      return [[center[0] + dx * c - dy * s, center[1] + dx * s + dy * c, p[2]], turn ? wrapDeg(h + a * k) : h];
    });
  }
  return out;
}

// axis 'x': the plane X = at (left and right swap); 'y': the plane Y = at. The model is not flipped; its heading is the one a model
// symmetric about its own front axis would have in the mirror: -h for X, 180 - h for Y.
export function mirrorPlacer(axis, at) {
  return axis === 'x' ? (p, h) => [[2 * at - p[0], p[1], p[2]], wrapDeg(-h)]
                      : (p, h) => [[p[0], 2 * at - p[1], p[2]], wrapDeg(180 - h)];
}

// boxes: [{min: [x, y, z], max: [x, y, z]}]. Shifts along one axis (0, 1, 2) so that every box's min, middle or max meets the group's.
export function alignShifts(boxes, axis, to) {
  const lo = Math.min(...boxes.map((b) => b.min[axis])), hi = Math.max(...boxes.map((b) => b.max[axis]));
  return boxes.map((b) => to === 'min' ? lo - b.min[axis] : to === 'max' ? hi - b.max[axis] : (lo + hi) / 2 - (b.min[axis] + b.max[axis]) / 2);
}

// The middles evenly spaced between the two outer ones (needs three); gaps: the gaps between the boxes equal instead.
// Returns a shift for each box, in the order given.
export function spreadShifts(boxes, axis, gaps = false) {
  const n = boxes.length, mid = (b) => (b.min[axis] + b.max[axis]) / 2, len = (b) => b.max[axis] - b.min[axis];
  const order = boxes.map((_, i) => i).sort((i, j) => mid(boxes[i]) - mid(boxes[j]));
  const out = new Array(n).fill(0);
  if (n < 3) return out;
  const first = boxes[order[0]], last = boxes[order[n - 1]];
  if (!gaps) {
    const step = (mid(last) - mid(first)) / (n - 1);
    order.forEach((i, k) => { out[i] = mid(first) + step * k - mid(boxes[i]); });
  } else {
    const span = last.max[axis] - first.min[axis], used = order.reduce((s, i) => s + len(boxes[i]), 0), gap = (span - used) / (n - 1);
    let at = first.min[axis];
    order.forEach((i) => { out[i] = at - boxes[i].min[axis]; at += len(boxes[i]) + gap; });
  }
  return out;
}

// the middle of the boxes together, [x, y]
export function middleOf(boxes) {
  const lo = [0, 1].map((a) => Math.min(...boxes.map((b) => b.min[a]))), hi = [0, 1].map((a) => Math.max(...boxes.map((b) => b.max[a])));
  return [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2];
}
