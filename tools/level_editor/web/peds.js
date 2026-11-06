// Pedestrians in the level editor: the stage's pedestrian net (sidewalk segments, PTS/<STAGE>.PED) and its groups (TRANSLATE 0xBB8,
// FORMATS.md), drawn in the scene and edited into the patch's "peds" (MODS.md, "Level patches"; tools/lvl.py LevelPatch.apply_peds
// and native/runtime/wotm_levelpeds.inc apply it). app.js owns selection, gizmo and undo; this module owns the markers and the data.
//
//   const peds = createPeds({ THREE, scene, toThree, toGame });
//   peds.load(disc, patchPeds)   disc: /api/stage "peds" ({groups: [{handle, pos, count}], net: [{pos, links: [[k, w, d]]}], hasNet})
//   peds.patch()                 the "peds" object for the patch, or null when nothing changed
//   peds.items()                 [{kind: 'group' | 'seg' | 'end', node: Object3D, ...}] for picking; node.userData.pedItem = item
//   peds.moved(item)             after the gizmo moved item.node (ends follow a segment, a segment follows its ends)
//   peds.addGroup(pos), peds.addSegment(pos), peds.remove(item), peds.setCount(item, n), peds.setWidth(item, w)
//   peds.info(item)              text for the properties panel: what the group walks, or the segment's length
//   peds.counts()                {groups, segments, maxGroups, maxSegments, people}

export const MAX_GROUPS = 20;
export const MAX_SEGMENTS = 127;   // 255 nodes (tools/lvl.py NET_NODES // 2)
const DEFAULT_NET = [{ pos: [-100, 0, 250], links: [[1, 250, 0]] }, { pos: [100, 0, 250], links: [[0, 250, 0]] }];
const LIFT = 1.5;                  // markers just above the sidewalk

export function createPeds({ THREE, scene, toThree, toGame }) {
  const root = new THREE.Group();
  root.name = 'peds';
  scene.add(root);
  const mats = {
    lane: new THREE.MeshBasicMaterial({ color: 0x3fb6ff, transparent: true, opacity: 0.28, depthWrite: false, side: THREE.DoubleSide }),
    laneSel: new THREE.MeshBasicMaterial({ color: 0xffd23f, transparent: true, opacity: 0.38, depthWrite: false, side: THREE.DoubleSide }),
    line: new THREE.LineBasicMaterial({ color: 0x3fb6ff }),
    end: new THREE.MeshBasicMaterial({ color: 0x3fb6ff }),
    group: new THREE.MeshBasicMaterial({ color: 0xff8a3f }),
    groupBad: new THREE.MeshBasicMaterial({ color: 0xe5534b }),
    bind: new THREE.LineDashedMaterial({ color: 0xff8a3f, dashSize: 12, gapSize: 8 }),
  };
  for (const m of Object.values(mats)) m.fog = false;   // markers stay clear in the Game look's fog
  const geo = { end: new THREE.SphereGeometry(7, 12, 8), group: new THREE.ConeGeometry(14, 40, 16) };
  geo.group.translate(0, 20, 0);

  let disc = { groups: [], net: [], hasNet: false };
  let segs = [];        // [{a: [x,y,z], b, width, item, ends: [item, item]}]
  let groups = [];      // [{handle|null, from, pos, count, count0, deleted, item}]
  let discSegsJson = '[]';
  let visible = true;

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
    node.material = b || g.deleted ? mats.group : mats.groupBad;
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
  }
  function discSegments() {
    const out = [], seen = new Set(), net = disc.hasNet ? disc.net : [];
    net.forEach((n, i) => {
      if (!n.links.length) return;
      const k = n.links[0][0];
      const key = Math.min(i, k) + '-' + Math.max(i, k);
      if (k >= net.length || seen.has(key)) return;
      seen.add(key);
      out.push({ a: n.pos, b: net[k].pos, width: n.links[0][1] });
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
    const allOff = discG.length > 0 && discG.every((g) => g.deleted);
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
  function setCount(item, n) { item.group.count = Math.max(1, Math.min(255, Math.round(n) || 1)); }
  function setWidth(item, w) { const s = item.seg; s.width = Math.max(1, Math.min(255, Math.round(w) || 1)); placeSeg(s); placeAll(); }
  function info(item) {
    if (item.kind === 'group') {
      const g = item.group;
      if (g.deleted) return 'removed';
      const b = bind(g.pos);
      if (!b) return netNodes().length ? 'no sidewalk: its nearest node has no link' : 'the stage has no pedestrian net: add a sidewalk';
      return `walks the sidewalk ${b.i}-${b.k} (${Math.round(dist(b.a, b.b))} long), the one with the nearest end`;
    }
    const s = item.seg;
    return `sidewalk ${segs.indexOf(s)}: ${Math.round(dist(s.a, s.b))} long, lane ${s.width * 10} to each side`;
  }
  function counts() {
    const live = groups.filter((g) => !g.deleted);
    return { groups: live.length, segments: segs.length, maxGroups: MAX_GROUPS, maxSegments: MAX_SEGMENTS,
             people: live.reduce((n, g) => n + g.count, 0) };
  }
  function items() {
    return [...groups.filter((g) => !g.deleted).map((g) => g.item), ...segs.flatMap((s) => [s.item, ...s.ends])];
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
  function highlight(item) {
    for (const s of segs) s.item.lane.material = item && item.seg === s ? mats.laneSel : mats.lane;
  }

  return { load, patch, items, moved, addGroup, addSegment, remove, setCount, setWidth, info, counts, setVisible, highlight, drop,
           root, defaultNet: DEFAULT_NET };
}
