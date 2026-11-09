// Pedestrians in the level editor: the stage's pedestrian net (sidewalk segments, PTS/<STAGE>.PED) and its groups (TRANSLATE 0xBB8,
// FORMATS.md), drawn in the scene and edited into the patch's "peds" (MODS.md, "Level patches"; tools/lvl.py LevelPatch.apply_peds
// and native/runtime/wotm_levelpeds.inc apply it). app.js owns selection, gizmo and undo; this module owns the markers and the data.
//
//   const peds = createPeds({ THREE, scene, toThree, toGame });
//   peds.load(disc, patchPeds)   disc: /api/stage "peds" ({groups: [{handle, pos, count}], net: [{pos, links: [[k, w, d]]}], hasNet})
//   peds.patch()                 the "peds" object for the patch, or null when nothing changed
//   peds.items()                 [{kind: 'group' | 'seg' | 'end', node: Object3D, ...}] for picking; node.userData.pedItem = item
//   peds.entries()               {groups: [{item, no, count, bound, changed, added}], segments: [{item, no, len, width}]} for the list
//   peds.moved(item)             after the gizmo moved item.node (ends follow a segment, a segment follows its ends)
//   peds.addGroup(pos), peds.addSegment(pos), peds.remove(item), peds.setCount(item, n), peds.setWidth(item, w)
//   peds.setLive(on)             every group out of the stage (off), or the disc's groups back (on)
//   peds.clearSegments()         every sidewalk out (app.js takes the groups out with them: they would have nowhere to walk)
//   peds.label(item), peds.info(item)   "Group 3", "Sidewalk 12"; what the group walks, or the segment's length and lane
//   peds.binding(item)           {nodes: [i, k], seg, no, len}: the segment a group walks (the nearest node and its link 0), or null
//   peds.counts()                {groups, segments, maxGroups, maxSegments, people}
//   peds.setDim(on), peds.setVisible(on), peds.highlight(item)
// Groups and sidewalks are numbered by their place in the list (1..N), not by the net's nodes.

export const MAX_GROUPS = 20;
export const MAX_SEGMENTS = 127;   // 255 nodes (tools/lvl.py NET_NODES // 2)
const DEFAULT_NET = [{ pos: [-100, 0, 250], links: [[1, 250, 0]] }, { pos: [100, 0, 250], links: [[0, 250, 0]] }];
const LIFT = 1.5;                  // markers just above the sidewalk
// the scene's colors, the same in both themes: sidewalks cyan, groups violet, a group with nothing to walk red, the selection amber
export const COLORS = { lane: 0x36C5F0, group: 0xB57BFF, bad: 0xF0605A, sel: 0xFFC233, end: 0xFFFFFF };
const DIM = 0.35;                  // the markers in the Objects mode

export function createPeds({ THREE, scene, toThree, toGame }) {
  const root = new THREE.Group();
  root.name = 'peds';
  scene.add(root);
  const basic = (color, o = {}) => new THREE.MeshBasicMaterial(Object.assign({ color }, o));
  const laneOpts = { transparent: true, depthWrite: false, side: THREE.DoubleSide };
  const mats = {
    lane: basic(COLORS.lane, Object.assign({ opacity: 0.22 }, laneOpts)),
    laneSel: basic(COLORS.sel, Object.assign({ opacity: 0.45 }, laneOpts)),
    line: new THREE.LineBasicMaterial({ color: COLORS.lane }),
    lineSel: new THREE.LineBasicMaterial({ color: COLORS.sel }),
    end: basic(COLORS.end),
    endSel: basic(COLORS.sel),
    group: basic(COLORS.group),
    groupBad: basic(COLORS.bad),
    groupSel: basic(COLORS.sel),
    bind: new THREE.LineDashedMaterial({ color: COLORS.group, dashSize: 12, gapSize: 8 }),
  };
  for (const m of Object.values(mats)) {
    m.fog = false;                     // markers stay clear in the Game look's fog
    m.userData.opacity = m.opacity;
    m.userData.transparent = m.transparent;
  }
  const geo = { end: new THREE.SphereGeometry(7, 12, 8), group: new THREE.ConeGeometry(14, 40, 16) };
  geo.group.translate(0, 20, 0);

  let disc = { groups: [], net: [], hasNet: false };
  let segs = [];        // [{a: [x,y,z], b, width, item, ends: [item, item]}]
  let groups = [];      // [{handle|null, from, pos, count, count0, deleted, item}]
  let discSegsJson = '[]';
  let visible = true;
  let selItem = null;   // the marker the editor has selected (amber)

  const v3 = (p) => toThree(p).add(new THREE.Vector3(0, LIFT, 0));
  const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
  const r1 = (x) => Math.round(x * 10) / 10;

  // the net the game builds from the segments: node 2k = a, 2k+1 = b, linked to each other (lvl.py apply_peds)
  function netNodes() {
    if (!segsEdited()) return disc.hasNet ? disc.net : [];
    const out = [];
    segs.forEach((s, k) => {
      out.push({ pos: s.a, links: [[2 * k + 1, s.width, 0]] });
      out.push({ pos: s.b, links: [[2 * k, s.width, 0]] });
    });
    return out;
  }
  // PedGroup's constructor: the nearest node (3D, the first of equals) and its link 0
  function bind(pos) {
    const nodes = netNodes();
    if (!nodes.length) return null;
    let best = -1, bd = 0;
    nodes.forEach((n, i) => { const d = dist(n.pos, pos); if (best < 0 || d < bd) best = i, bd = d; });
    const n = nodes[best];
    if (!n.links.length || n.links[0][0] >= nodes.length) return null;
    return { i: best, k: n.links[0][0], a: n.pos, b: nodes[n.links[0][0]].pos };
  }
  function segsEdited() { return JSON.stringify(segs.map((s) => [s.a.map(r1), s.b.map(r1), s.width])) !== discSegsJson; }
  // the segment of the list that a pair of nodes is: 2k and 2k+1 once edited, else the disc's link (the order of discSegments)
  function segOfNodes(i, k) {
    if (segsEdited()) return segs[Math.floor(i / 2)] || null;
    const key = Math.min(i, k) + '-' + Math.max(i, k);
    const j = discSegments().findIndex((s) => s.key === key);
    return j >= 0 ? segs[j] || null : null;
  }

  // ------------------------------------------------------------ meshes
  function laneGeometry(s) {
    const a = toThree(s.a), b = toThree(s.b);
    const dir = b.clone().sub(a);
    const side = new THREE.Vector3(-dir.z, 0, dir.x).normalize().multiplyScalar(Math.max(2, s.width * 10));
    const g = new THREE.BufferGeometry();
    const p = [a.clone().add(side), a.clone().sub(side), b.clone().add(side), b.clone().sub(side)].map((v) => v.add(new THREE.Vector3(0, LIFT, 0)));
    g.setAttribute('position', new THREE.Float32BufferAttribute(p.flatMap((v) => [v.x, v.y, v.z]), 3));
    g.setIndex([0, 1, 2, 2, 1, 3]);
    return g;
  }
  function paintSeg(s) {
    const on = !!selItem && selItem.seg === s;
    s.item.lane.material = on ? mats.laneSel : mats.lane;
    s.item.axis.material = on ? mats.lineSel : mats.line;
    for (const e of s.ends) e.node.material = selItem === e ? mats.endSel : mats.end;
  }
  function placeSeg(s) {
    s.item.node.position.copy(toThree(s.a).lerp(toThree(s.b), 0.5));
    s.item.node.updateMatrixWorld(true);
    const lane = s.item.lane;
    lane.geometry.dispose();
    lane.geometry = laneGeometry(s);
    s.item.axis.geometry.setFromPoints([v3(s.a), v3(s.b)]);
    s.ends[0].node.position.copy(v3(s.a));
    s.ends[1].node.position.copy(v3(s.b));
    for (const e of s.ends) e.node.updateMatrixWorld(true);
    paintSeg(s);
  }
  function makeSeg(a, b, width) {
    const s = { a: [...a], b: [...b], width };
    const handle = new THREE.Object3D();           // the gizmo's anchor: the middle of the segment
    const lane = new THREE.Mesh(new THREE.BufferGeometry(), mats.lane);
    const axis = new THREE.Line(new THREE.BufferGeometry(), mats.line);
    lane.renderOrder = 5;
    root.add(handle, lane, axis);
    s.item = { kind: 'seg', seg: s, node: handle, lane, axis, pick: lane };
    lane.userData.pedItem = s.item;
    s.ends = [0, 1].map((j) => {
      const m = new THREE.Mesh(geo.end, mats.end);
      root.add(m);
      const it = { kind: 'end', seg: s, end: j, node: m, pick: m };
      m.userData.pedItem = it;
      return it;
    });
    placeSeg(s);
    return s;
  }
  function dropSeg(s) {
    root.remove(s.item.node, s.item.lane, s.item.axis, s.ends[0].node, s.ends[1].node);
    s.item.lane.geometry.dispose();
    s.item.axis.geometry.dispose();
  }
  function makeGroup(g) {
    const m = new THREE.Mesh(geo.group, mats.group);
    const line = new THREE.Line(new THREE.BufferGeometry(), mats.bind);
    root.add(m, line);
    g.item = { kind: 'group', group: g, node: m, pick: m, line };
    m.userData.pedItem = g.item;
    placeGroup(g);
    return g;
  }
  function placeGroup(g) {
    const { node, line } = g.item;
    node.position.copy(toThree(g.pos));
    node.visible = visible && !g.deleted;
    node.updateMatrixWorld(true);
    const b = g.deleted ? null : bind(g.pos);
    node.material = selItem === g.item ? mats.groupSel : b || g.deleted ? mats.group : mats.groupBad;
    line.visible = visible && !!b;
    if (b) {
      line.geometry.setFromPoints([v3(g.pos), v3(b.a).lerp(v3(b.b), 0.5)]);
      line.computeLineDistances();
    }
  }
  function placeAll() { for (const g of groups) placeGroup(g); }

  // ------------------------------------------------------------ data
  function clear() {
    for (const s of segs) dropSeg(s);
    for (const g of groups) root.remove(g.item.node, g.item.line);
    segs = []; groups = [];
    selItem = null;
  }
  function discSegments() {
    const out = [], seen = new Set(), net = disc.hasNet ? disc.net : [];
    net.forEach((n, i) => {
      if (!n.links.length) return;
      const k = n.links[0][0];
      const key = Math.min(i, k) + '-' + Math.max(i, k);
      if (k >= net.length || seen.has(key)) return;
      seen.add(key);
      out.push({ a: n.pos, b: net[k].pos, width: n.links[0][1], key });
    });
    return out;
  }
  function load(d, p) {
    clear();
    disc = { groups: d?.groups || [], net: d?.net || [], hasNet: !!d?.hasNet };
    const base = discSegments();
    discSegsJson = JSON.stringify(base.map((s) => [s.a.map(r1), s.b.map(r1), s.width]));
    const src = p?.segments ? p.segments.map((s) => ({ a: s.a, b: s.b, width: Math.max(1, Math.min(255, s.width ?? 4)) })) : base;
    segs = src.map((s) => makeSeg(s.a, s.b, s.width));
    const byHandle = new Map((p?.groups || []).filter((e) => e.handle != null).map((e) => [String(e.handle).toUpperCase(), e]));
    for (const dg of disc.groups) {
      const e = byHandle.get(String(dg.handle).toUpperCase());
      groups.push(makeGroup({ handle: dg.handle, from: dg.pos, pos: [...(e?.pos || dg.pos)], count: e?.count ?? dg.count,
                              count0: dg.count, deleted: !!(p?.off || e?.delete) }));
    }
    for (const e of (p?.groups || []).filter((x) => x.handle == null))
      groups.push(makeGroup({ handle: null, from: null, pos: [...e.pos], count: e.count ?? 50, count0: null, deleted: false }));
    placeAll();
  }
  function patch() {
    const out = {};
    if (segsEdited()) out.segments = segs.map((s) => ({ a: s.a.map(r1), b: s.b.map(r1), width: s.width }));
    const discG = groups.filter((g) => g.handle != null);
    // "off" takes out every disc group, and the game then leaves out the new ones too: only when no new group is left
    const allOff = discG.length > 0 && discG.every((g) => g.deleted) && !groups.some((g) => g.handle == null && !g.deleted);
    if (allOff) out.off = true;
    const list = [];
    for (const g of groups) {
      if (g.handle == null) { if (!g.deleted) list.push({ pos: g.pos.map(r1), count: g.count }); continue; }
      if (allOff) continue;
      const e = { handle: g.handle, from: g.from.map(r1) };
      if (g.deleted) e.delete = true;
      else {
        if (dist(g.pos, g.from) > 0.05) e.pos = g.pos.map(r1);
        if (g.count !== g.count0) e.count = g.count;
      }
      if (Object.keys(e).length > 2) list.push(e);
    }
    if (list.length) out.groups = list;
    return Object.keys(out).length ? out : null;
  }

  // ------------------------------------------------------------ edits (app.js snapshots the patch before each one)
  function moved(item) {
    const p = toGame(item.node.position);
    if (item.kind === 'group') { item.group.pos = [p[0], p[1], p[2]]; placeGroup(item.group); return; }
    const s = item.seg;
    if (item.kind === 'end') {
      const q = toGame(item.node.position.clone().sub(new THREE.Vector3(0, LIFT, 0)));
      if (item.end === 0) s.a = q; else s.b = q;
    } else {
      const mid = toThree(s.a).lerp(toThree(s.b), 0.5);
      const d = item.node.position.clone().sub(mid);
      s.a = toGame(toThree(s.a).add(d)); s.b = toGame(toThree(s.b).add(d));
    }
    placeSeg(s);
    placeAll();
  }
  function addGroup(pos, count = 50) {
    if (groups.filter((g) => !g.deleted).length >= MAX_GROUPS) throw new Error(`a stage holds ${MAX_GROUPS} pedestrian groups at most`);
    const g = makeGroup({ handle: null, from: null, pos: [...pos], count, count0: null, deleted: false });
    groups.push(g);
    return g.item;
  }
  function addSegment(pos, len = 300, width = 4) {
    if (segs.length >= MAX_SEGMENTS) throw new Error(`${MAX_SEGMENTS} sidewalk segments at most`);
    const s = makeSeg([pos[0] - len / 2, pos[1], pos[2]], [pos[0] + len / 2, pos[1], pos[2]], width);
    segs.push(s);
    placeAll();
    return s.item;
  }
  function remove(item) {
    if (selItem && (selItem === item || (item.kind !== 'group' && selItem.seg === item.seg))) selItem = null;
    if (item.kind === 'group') {
      const g = item.group;
      if (g.handle == null) { root.remove(g.item.node, g.item.line); groups.splice(groups.indexOf(g), 1); }
      else { g.deleted = true; placeGroup(g); }
    } else {
      const s = item.seg;
      dropSeg(s);
      segs.splice(segs.indexOf(s), 1);
      placeAll();
    }
  }
  // the "Pedestrians in this stage" switch: off takes out every group (new ones too), on brings the disc's groups back
  function setLive(on) {
    if (on) { for (const g of groups) if (g.handle != null) g.deleted = false; }
    else {
      for (const g of [...groups]) {
        if (g.handle == null) { root.remove(g.item.node, g.item.line); groups.splice(groups.indexOf(g), 1); }
        else g.deleted = true;
      }
      if (selItem && selItem.kind === 'group') selItem = null;
    }
    placeAll();
  }
  // every sidewalk out (a net of your own from scratch); the groups are not touched here: the caller takes them out too
  function clearSegments() {
    for (const s of segs) dropSeg(s);
    segs = [];
    if (selItem && selItem.kind !== 'group') selItem = null;
    placeAll();
  }
  const live = () => groups.some((g) => !g.deleted);
  const hasDiscGroups = () => groups.some((g) => g.handle != null);
  function setCount(item, n) { item.group.count = Math.max(1, Math.min(255, Math.round(n) || 1)); }
  function setWidth(item, w) { const s = item.seg; s.width = Math.max(1, Math.min(255, Math.round(w) || 1)); placeSeg(s); placeAll(); }

  // ------------------------------------------------------------ what the panel and the list say
  const liveGroups = () => groups.filter((g) => !g.deleted);
  const groupNo = (g) => liveGroups().indexOf(g) + 1;
  const segNo = (s) => segs.indexOf(s) + 1;
  const segLen = (s) => Math.round(dist(s.a, s.b));
  function binding(item) {
    const pos = item.kind === 'group' ? item.group.pos : null;
    if (!pos || item.group.deleted) return null;
    const b = bind(pos);
    if (!b) return null;
    const seg = segOfNodes(b.i, b.k);
    return { nodes: [b.i, b.k], seg, no: seg ? segNo(seg) : 0, len: Math.round(dist(b.a, b.b)) };
  }
  function label(item) {
    if (item.kind === 'group') return item.group.deleted ? 'Removed group' : `Group ${groupNo(item.group)}`;
    const n = segNo(item.seg);
    return item.kind === 'seg' ? `Sidewalk ${n}` : `Sidewalk ${n} · end ${item.end ? 'B' : 'A'}`;
  }
  function info(item) {
    if (item.kind === 'group') {
      const g = item.group;
      if (g.deleted) return 'Removed from this stage in your mod.';
      const b = binding(item);
      if (!b) return netNodes().length ? 'Not on any sidewalk: move it closer to one.' : 'This stage has no sidewalks yet: add one first.';
      return `Walks on sidewalk ${b.no || '?'} (${b.len} units long)`;
    }
    const s = item.seg;
    if (item.kind === 'end') return `End ${item.end ? 'B' : 'A'} of sidewalk ${segNo(s)} (${segLen(s)} units long). Drag it to stretch the sidewalk.`;
    return `Sidewalk ${segNo(s)} · ${segLen(s)} units long · ${s.width * 10} units each side`;
  }
  function entries() {
    return {
      groups: liveGroups().map((g, k) => ({ item: g.item, no: k + 1, count: g.count, bound: !!bind(g.pos), added: g.handle == null,
                                            changed: g.handle != null && (dist(g.pos, g.from) > 0.05 || g.count !== g.count0) })),
      segments: segs.map((s, k) => ({ item: s.item, no: k + 1, len: segLen(s), width: s.width })),
    };
  }
  function counts() {
    const lv = liveGroups();
    return { groups: lv.length, segments: segs.length, maxGroups: MAX_GROUPS, maxSegments: MAX_SEGMENTS,
             people: lv.reduce((n, g) => n + g.count, 0) };
  }
  function items() {
    return [...liveGroups().map((g) => g.item), ...segs.flatMap((s) => [s.item, ...s.ends])];
  }
  // End: onto the ground; groundAt(v) gives the scene height under a scene point (or null). A sidewalk drops both ends.
  function drop(item, groundAt) {
    const put = (p) => { const y = groundAt(toThree(p)); return y === null ? p : toGame(toThree(p).setY(y)); };
    if (item.kind === 'group') { item.group.pos = put(item.group.pos); placeGroup(item.group); return; }
    const s = item.seg;
    if (item.kind !== 'end' || item.end === 0) s.a = put(s.a);
    if (item.kind !== 'end' || item.end === 1) s.b = put(s.b);
    placeSeg(s);
    placeAll();
  }
  function setVisible(on) { visible = on; root.visible = on; placeAll(); }
  // the Objects mode shows the markers faint (the Pedestrians layer), the Pedestrians mode as they are
  function setDim(on) {
    for (const m of Object.values(mats)) {
      const tr = on || m.userData.transparent;
      if (m.transparent !== tr) { m.transparent = tr; m.needsUpdate = true; }
      m.opacity = m.userData.opacity * (on ? DIM : 1);
    }
  }
  function highlight(item) {
    selItem = item || null;
    for (const s of segs) paintSeg(s);
    placeAll();
  }

  return { load, patch, items, entries, moved, addGroup, addSegment, remove, setLive, clearSegments, live, hasDiscGroups, setCount, setWidth,
           label, info, binding, counts, setVisible, setDim, highlight, drop, root, mats, defaultNet: DEFAULT_NET };
}
