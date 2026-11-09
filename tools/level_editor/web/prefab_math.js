// The arithmetic of prefabs (a saved selection, pasted somewhere else), in the game's axes (X right, Y forward, Z up; the heading
// turns counterclockwise seen from above). Nothing here touches the page, so test_prefab.mjs runs it with node.
//
// A prefab entry keeps where its object was relative to the group: d = [dx, dy, dz], the offset from the group's middle (the middle of
// the box of the objects' positions in X and Y) and its lowest position in Z, and the heading in the group's own frame.

export const wrapDeg = (a) => ((a + 180) % 360 + 360) % 360 - 180;
const rad = (d) => d * Math.PI / 180;
const r1 = (x) => Math.round(x * 10) / 10;

// items: [{pos: [x, y, z], heading, ...what to keep}] -> the same with `d` in place of `pos`
export function toPrefabEntries(items) {
  if (!items.length) return [];
  const xs = items.map((i) => i.pos[0]), ys = items.map((i) => i.pos[1]), zs = items.map((i) => i.pos[2]);
  const cx = (Math.min(...xs) + Math.max(...xs)) / 2, cy = (Math.min(...ys) + Math.max(...ys)) / 2, base = Math.min(...zs);
  return items.map(({ pos, ...rest }) => ({ ...rest, d: [r1(pos[0] - cx), r1(pos[1] - cy), r1(pos[2] - base)] }));
}

// Where an entry goes when the prefab is put at `at` ([x, y, z]: where the middle and the base land) after turning it by `turn`
// degrees about the middle: {pos, heading}
export function placeEntry(entry, at, turn = 0) {
  const c = Math.cos(rad(turn)), s = Math.sin(rad(turn)), [dx, dy, dz] = entry.d;
  return { pos: [at[0] + dx * c - dy * s, at[1] + dx * s + dy * c, at[2] + dz], heading: wrapDeg((entry.heading || 0) + turn) };
}
