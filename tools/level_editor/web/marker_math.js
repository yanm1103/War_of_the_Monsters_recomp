// The arithmetic of the markers in the view (start points, power-ups, net nodes), apart from the page: how big a marker is drawn, how far from
// it a click still picks it, which one wins when several are near, and where the text labels go. app.js feeds it screen numbers;
// tools/level_editor/test_marker_math.mjs runs it in node.

// A marker keeps its size up close and grows with the distance, so that it stays readable over a whole stage: scale 1 up to BOOST_AT
// units from the camera, then in step with the distance, at most BOOST_MAX times.
export const BOOST_AT = 700;
export const BOOST_MAX = 3.5;
export const markerScale = (dist) => Math.min(BOOST_MAX, Math.max(1, dist / BOOST_AT));

// the world size of each marker's body (a bounding radius, and the height of its top above its centre), before the scale
export const EXTENT = {
  SPT: { r: 28, top: 25 },       // a cone 50 tall and 28 wide
  PUP: { r: 12, top: 12 },       // a ball of 10, a cube of 16
  NODE: { r: 9, top: 9 },        // a sphere of 7
};
export const SEL_GROW = 1.3;      // the selected marker is drawn this much bigger

// A click picks a marker from its radius on the screen plus a margin, at least MIN_PICK and at most MAX_PICK pixels: a far marker of a few
// pixels is still easy to hit, a near one does not swallow its neighbours.
export const MIN_PICK = 12, MAX_PICK = 64, PICK_PAD = 6;
export const pickRadius = (rPx) => Math.min(MAX_PICK, Math.max(MIN_PICK, rPx + PICK_PAD));

// cands: [{i, d, rPx}] with d the distance in pixels from the click to the marker's centre and rPx its radius on the screen.
// The winner is the one the click is nearest to the surface of (d - rPx, the most negative first), then the nearer centre; none: -1.
export function pickBest(cands) {
  let best = -1, bs = Infinity, bd = Infinity;
  for (const c of cands) {
    if (!(c.d <= pickRadius(c.rPx))) continue;
    const s = c.d - c.rPx;
    if (s < bs - 1e-9 || (Math.abs(s - bs) <= 1e-9 && c.d < bd)) { best = c.i; bs = s; bd = c.d; }
  }
  return best;
}

// the width of a label in pixels (11 px type, 7 px padding each side): good enough to keep two labels apart
export const LABEL_H = 18;
export const labelWidth = (text) => Math.round(text.length * 6.4 + 16);

// items: [{key, text, x, y, prio, w?, stack?}] with (x, y) the bottom centre of the label in pixels; the lower prio goes first. Returns a
// Map of the labels that are drawn, key -> how many pixels up it moves (0, or a step of LABEL_H + 1 for a label with `stack` that met
// another one, up to STACK_MAX steps: start points at the same spot read as a pile). A label that still meets one placed before it is left
// out, and so is one outside the w x h box. max caps the count of the labels of prio >= soft (the plain ones; the hovered, the selected
// and its neighbours always stay).
export const STACK_MAX = 3;
export function layoutLabels(items, w, h, max = 40, soft = 3) {
  const placed = [], keep = new Map();
  let plain = 0;
  for (const it of [...items].sort((a, b) => a.prio - b.prio || a.x - b.x || a.y - b.y)) {
    const bw = it.w || labelWidth(it.text), x0 = it.x - bw / 2;
    if (it.prio >= soft && plain >= max) continue;
    let placedAt = null;
    for (let k = 0; k <= (it.stack ? STACK_MAX : 0) && placedAt === null; k++) {
      const dy = k ? -k * (LABEL_H + 1) : 0, y0 = it.y + dy - LABEL_H;
      if (x0 + bw < 0 || x0 > w || it.y + dy < 0 || y0 > h) continue;
      if (!placed.some((r) => x0 < r[2] && x0 + bw > r[0] && y0 < r[3] && y0 + LABEL_H > r[1])) { placedAt = dy; placed.push([x0, y0, x0 + bw, y0 + LABEL_H]); }
    }
    if (placedAt === null) continue;
    keep.set(it.key, placedAt);
    if (it.prio >= soft) plain++;
  }
  return keep;
}
