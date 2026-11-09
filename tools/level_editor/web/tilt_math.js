// How a new prop is turned (the patch's "heading", "pitch" and "roll"), in the game's axes (X right, Y forward, Z up). Nothing here
// touches the page, so test_tilt.mjs runs it with node. The same matrix is written by tools/lvl.py (prop_matrix) and by the hook
// (native/runtime/wotm_levelprops.inc).
//
// The game multiplies row vectors, so the matrix is Rx(pitch) * Ry(roll) * Rz(heading): the model is tipped first (pitch: its nose,
// +Y, goes up for +; roll: its top, +Z, goes toward +X for +), then turned about the vertical (the heading counts counterclockwise
// seen from above). Row j of the matrix is where the model's axis j lands.

const rad = (d) => d * Math.PI / 180;
const deg = (r) => r * 180 / Math.PI;

// [[row 0], [row 1], [row 2]] (3 x 3)
export function propRows(heading, pitch, roll) {
  const cp = Math.cos(rad(pitch)), sp = Math.sin(rad(pitch)), cr = Math.cos(rad(roll)), sr = Math.sin(rad(roll));
  const ch = Math.cos(rad(heading)), sh = Math.sin(rad(heading));
  const tilt = [[cr, 0, -sr], [sp * sr, cp, sp * cr], [cp * sr, -sp, cp * cr]];
  return tilt.map((r) => [r[0] * ch - r[1] * sh, r[0] * sh + r[1] * ch, r[2]]);
}

// The angles of a matrix made by propRows: {h, p, r}. Roll is taken in -90..90 (the third angle of the turn is the heading).
export function propAngles(rows) {
  const [ex, ey, ez] = rows;
  return { h: deg(Math.atan2(ex[1], ex[0])), p: deg(Math.atan2(ey[2], ez[2])), r: deg(-Math.asin(Math.max(-1, Math.min(1, ex[2])))) };
}
