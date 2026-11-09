// The rules of the four marker layers of a stage, apart from the page: start points (.SPT), power-ups (.PUP), the AI net (.AIP) and the
// vehicle net (.FOP). Colors, names, the groups a list is sorted into, the counts against the game's limits, and the net's links.
// app.js draws them and keeps their drafts; tools/level_editor/test_markers.mjs runs these functions in node.

export const KINDS = ['SPT', 'PUP', 'AIP', 'FOP'];
export const isNet = (kind) => kind === 'AIP' || kind === 'FOP';

// kind -> the rail's mode, the words, the icon of the mode and of its rows, the key that picks the mode
export const LAYERS = {
  SPT: { mode: 'spt', key: '3', title: 'Start points', noun: 'start points', one: 'start point', many: 'start points', icon: 'flag-outline', hint: 'where the monsters appear' },
  PUP: { mode: 'pup', key: '4', title: 'Power-ups', noun: 'power-ups', one: 'power-up', many: 'power-ups', icon: 'heart-plus-outline', hint: 'the power-ups lying in the stage' },
  AIP: { mode: 'aip', key: '5', title: 'AI net', noun: 'AI net', one: 'AI net node', many: 'AI net nodes', icon: 'graph-outline', hint: 'the paths the computer monsters walk' },
  FOP: { mode: 'fop', key: '6', title: 'Vehicle net', noun: 'vehicle net', one: 'vehicle net node', many: 'vehicle net nodes', icon: 'road-variant', hint: 'the roads and flight paths of the vehicles' },
};
export const KIND_OF_MODE = Object.fromEntries(KINDS.map((k) => [LAYERS[k].mode, k]));

// the points file holds 255 records; the game keeps fewer of some (the list's room, 100 power-ups at a time)
export const FILE_MAX = 255;

// ---- start points: four lists (tools/lvl.py SPT_LISTS)
export const SPT_SHORT = ['Story', 'Player', 'CPU', 'Respawn'];
// "Story", "Player 2": the point's place in its own list
export function sptLabel(points, i) {
  const l = points[i].list;
  if (l === 0) return 'Story';
  let n = 0;
  for (let k = 0; k <= i; k++) if (points[k].list === l) n++;
  return `${SPT_SHORT[l] || 'List ' + l} ${n}`;
}
// is point i past the room of its list (the game overwrites the list's last one with it)
export function sptOver(points, i, caps) {
  const l = points[i].list;
  let n = 0;
  for (let k = 0; k <= i; k++) if (points[k].list === l) n++;
  return n > (caps[l] ?? Infinity);
}

// ---- power-ups: 20 types (tools/lvl.py PUP_TYPES), shown in five groups
export const PUP_GROUPS = [
  { id: 'health', title: 'Health', types: [0, 1, 2, 6] },
  { id: 'stamina', title: 'Stamina', types: [3, 4, 5, 7] },
  { id: 'special', title: 'Special weapon', types: [8] },
  { id: 'cloak', title: 'Cloak', types: [9] },
  { id: 'story', title: 'Story items', types: [10, 11, 12, 13, 14, 15, 16, 17, 18, 19] },
];
export const pupGroup = (type) => (PUP_GROUPS.find((g) => g.types.includes(type)) || PUP_GROUPS[PUP_GROUPS.length - 1]).id;

// ---- colors: one table for the view, the list and the legend
const SPT_COLORS = [0xFFC233, 0x4AE08A, 0xF0605A, 0x36C5F0];
// power-ups: health green, stamina blue, special weapon red, cloak a gray cube, the story items amber
const PUP_COLORS = { health: 0x4AE08A, stamina: 0x36C5F0, special: 0xF0605A, cloak: 0xA8B0BC, story: 0xFFC233 };
const FOP_COLORS = [0xFFD24A, 0x5EE0A0, 0xFF6FB5, 0xFF8A3D];      // car street, helicopter, flying saucer, tank and truck
export const netColor = (kind, type) => (kind === 'AIP' ? (type ? 0x36C5F0 : 0xB57BFF) : (FOP_COLORS[type] ?? 0xFFFFFF));
export function markerColor(kind, p) {
  if (kind === 'SPT') return SPT_COLORS[p.list] ?? 0xAAAAAA;
  if (kind === 'PUP') return PUP_COLORS[pupGroup(p.type)] ?? 0xAAAAAA;
  return netColor(kind, p.type);
}
export const hexColor = (c) => '#' + c.toString(16).padStart(6, '0');
// the shape in the view, in the list and in the legend: a cone (start point), a cube (cloak), a ball (the rest)
export function markerShape(kind, p) {
  if (kind === 'SPT') return 'cone';
  return kind === 'PUP' && pupGroup(p.type) === 'cloak' ? 'cube' : 'ball';
}
// the text over a start point in the view: its name, and that it is past its list's room
export function sptViewLabel(points, i, caps) {
  return sptLabel(points, i) + (sptOver(points, i, caps) ? ' · past room' : '');
}

// ---- the list's sections for a layer: [{id, title, rows: [index...], n, cap}] (cap: the room, when there is one)
export function sections(kind, points, lists, caps) {
  const by = (key) => {
    const m = new Map();
    points.forEach((p, i) => { const k = key(p); if (!m.has(k)) m.set(k, []); m.get(k).push(i); });
    return m;
  };
  if (kind === 'SPT') {
    const m = by((p) => p.list);
    return SPT_SHORT.map((t, l) => ({ id: String(l), title: t, rows: m.get(l) || [], n: (m.get(l) || []).length, cap: caps[l], tip: lists[l] || '' }));
  }
  if (kind === 'PUP') {
    const m = by((p) => pupGroup(p.type));
    return PUP_GROUPS.map((g) => ({ id: g.id, title: g.title, rows: m.get(g.id) || [], n: (m.get(g.id) || []).length }));
  }
  const m = by((p) => p.type);
  const types = [...m.keys()].sort((a, b) => a - b);
  return types.map((t) => ({ id: String(t), title: kind === 'FOP' ? lists[t] || `Type ${t}` : `Type ${t}`, rows: m.get(t), n: m.get(t).length }));
}
// the name of a marker: "Player 1", "Health 50", "Node 7"
export function markerName(kind, points, i, lists) {
  const p = points[i];
  if (kind === 'SPT') return sptLabel(points, i);
  if (kind === 'PUP') return lists[p.type] || `Power-up ${p.type}`;
  return `Node ${i + 1}`;
}

// ---- the nets: nodes with directed links
export const netTwoWay = (points, a, b) => points[a].links.some((l) => l.to === b) && points[b].links.some((l) => l.to === a);
// as the game counts them: a two-way link is one path
export function netPaths(points) {
  const seen = new Set();
  points.forEach((p, i) => p.links.forEach((l) => seen.add(netTwoWay(points, i, l.to) ? `${Math.min(i, l.to)}-${Math.max(i, l.to)}` : `${i}>${l.to}`)));
  return seen.size;
}
// take node k out and the links to it; the links to the later nodes are renumbered
export function removeNode(points, k) {
  points.splice(k, 1);
  for (const m of points) m.links = m.links.filter((l) => l.to !== k).map((l) => Object.assign({}, l, { to: l.to > k ? l.to - 1 : l.to }));
  return points;
}
// link a and b both ways with the given attributes ({width} or {width, drop}); a link that exists keeps its own.
// false when a node would pass `max` links (nothing is left half done)
export function linkBoth(points, a, b, attrs, max) {
  const need = (x, y) => !points[x].links.some((l) => l.to === y) && points[x].links.length >= max;
  if (need(a, b) || need(b, a)) return false;
  const add = (x, y) => {
    if (!points[x].links.some((l) => l.to === y)) points[x].links.push(Object.assign({ to: y }, attrs));
  };
  add(a, b);
  add(b, a);
  return true;
}
// The Link tool: what a click on node b does for the selected node a.
//   plain click  a link between them (either direction) is cut, and otherwise one is made both ways       -> 'cut' | 'link'
//   oneWay       the link from a to b only: cut when it exists, made when it does not                    -> 'cut-one' | 'one-way'
// 'full': a node would pass `max` links, `full` is its index; 'none': a is b. Nothing is changed by the plan.
export function linkPlan(points, a, b, oneWay, max) {
  if (a === b || !points[a] || !points[b]) return { action: 'none' };
  const ab = points[a].links.some((l) => l.to === b), ba = points[b].links.some((l) => l.to === a);
  if (oneWay) {
    if (ab) return { action: 'cut-one' };
    return points[a].links.length >= max ? { action: 'full', full: a } : { action: 'one-way' };
  }
  if (ab || ba) return { action: 'cut' };
  if (points[a].links.length >= max) return { action: 'full', full: a };
  if (points[b].links.length >= max) return { action: 'full', full: b };
  return { action: 'link' };
}
// make the plan's change (new links take attrs); returns the plan
export function applyLink(points, a, b, attrs, oneWay, max) {
  const plan = linkPlan(points, a, b, oneWay, max);
  const add = (x, y) => points[x].links.push(Object.assign({ to: y }, attrs));
  if (plan.action === 'link') { add(a, b); add(b, a); }
  else if (plan.action === 'one-way') add(a, b);
  else if (plan.action === 'cut') unlink(points, a, b);
  else if (plan.action === 'cut-one') points[a].links = points[a].links.filter((l) => l.to !== b);
  return plan;
}
// the words for a plan, with 1-based node numbers: the chip at the pointer and the message of a refusal
export function linkWords(plan, a, b, max) {
  switch (plan.action) {
    case 'link': return `Link ${a + 1} and ${b + 1}`;
    case 'one-way': return `Link ${a + 1} to ${b + 1}, one way`;
    case 'cut': return `Cut ${a + 1} and ${b + 1}`;
    case 'cut-one': return `Cut ${a + 1} to ${b + 1}`;
    case 'new': return 'New node';
    case 'full': return `Node ${plan.full + 1} already has ${max} links`;
    default: return '';
  }
}
// take every link between a and b away
export function unlink(points, a, b) {
  points[a].links = points[a].links.filter((l) => l.to !== b);
  points[b].links = points[b].links.filter((l) => l.to !== a);
}
// the links that end at node k and that it does not return: [{from, link}]
export function oneWayIn(points, k) {
  const out = [];
  points.forEach((p, i) => { if (i !== k) for (const l of p.links) if (l.to === k && !points[k].links.some((m) => m.to === i)) out.push({ from: i, link: l }); });
  return out;
}
