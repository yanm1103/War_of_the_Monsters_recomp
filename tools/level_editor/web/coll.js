// The Collision layer: what the game collides with, drawn over the stage (C, or the Layers menu). Data from /api/collision
// (tools/level_editor/collview.py): the shapes once per leaf and the paths that place them. Triangles are green (a building's collision,
// a prop's coll_ meshes), convex hulls cyan (hull_ meshes), boxes orange (trigger volumes and paths through animated nodes, drawn as their
// world box); a dimmer shade is a broken state of a destructible (it collides only after the building breaks).
// initColl({THREE, scene}) -> {setVisible, visible, set(data), clear, highlight(boxes), info}. The group is drawn in the game's axes
// mapped as the editor does: three = (x, z, -y).
const KIND = { mesh: [0.30, 0.85, 0.35], hull: [0.20, 0.78, 1.0], trigger: [1.0, 0.62, 0.12], other: [0.85, 0.85, 0.85] };

export function initColl({ THREE, scene }) {
  const root = new THREE.Group();
  root.name = 'collision';
  root.visible = false;
  root.renderOrder = 10;
  scene.add(root);
  let lines = null, fills = null, recs = [], colF = null, colAttr = null, data = null;
  const text = { paths: 0, triangles: 0, boxes: 0 };

  // the edges to draw for a leaf: its own list (a hull), or the triangles' sides each once
  const edgesOf = (leaf) => {
    if (leaf.edges) return leaf.edges;
    if (leaf.e) return (leaf.edges = leaf.e);
    const n = leaf.v.length / 3, seen = new Set(), out = [];
    const t = leaf.t;
    for (let i = 0; i < t.length; i += 3) {
      for (let k = 0; k < 3; k++) {
        const a = t[i + k], b = t[i + (k + 1) % 3], lo = Math.min(a, b), hi = Math.max(a, b), key = lo * n + hi;
        if (!seen.has(key)) { seen.add(key); out.push(lo, hi); }
      }
    }
    return (leaf.edges = out);
  };
  const BOX_EDGES = [0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 2, 6, 3, 7];
  const BOX_TRIS = [0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1, 2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3];
  const boxCorners = (mn, mx) => {
    const out = [];
    for (let i = 0; i < 8; i++) out.push([i & 1 ? mx[0] : mn[0], i & 2 ? mx[1] : mn[1], i & 4 ? mx[2] : mn[2]]);
    return out;
  };

  function dispose() {
    for (const o of [lines, fills]) if (o) { root.remove(o); o.geometry.dispose(); o.material.dispose(); }
    lines = fills = colF = colAttr = null;
    recs = [];
  }

  function set(d) {
    dispose();
    data = d;
    text.paths = text.triangles = text.boxes = 0;
    if (!d || !d.paths) return;
    // world corners of every path: [[x, y, z]...] in game axes, with the triangle and edge index lists into them
    const shapes = [];
    let nv = 0, ni = 0, ne = 0;
    for (const p of d.paths) {
      let verts, tris, edges;
      const leaf = p.l ? d.leaves[p.l] : null;
      if (leaf && p.m) {
        const m = p.m, v = leaf.v;
        verts = new Array(v.length / 3);
        for (let i = 0; i < verts.length; i++) {
          const x = v[3 * i], y = v[3 * i + 1], z = v[3 * i + 2];
          verts[i] = [x * m[0] + y * m[3] + z * m[6] + m[9], x * m[1] + y * m[4] + z * m[7] + m[10], x * m[2] + y * m[5] + z * m[8] + m[11]];
        }
        tris = leaf.t;
        edges = edgesOf(leaf);
        text.triangles += tris.length / 3;
      } else {
        verts = boxCorners(p.min, p.max);
        tris = BOX_TRIS;
        edges = BOX_EDGES;
        text.boxes++;
      }
      text.paths++;
      shapes.push({ verts, tris, edges, p });
      nv += verts.length; ni += tris.length; ne += edges.length;
    }
    const posF = new Float32Array(nv * 3), idxF = new Uint32Array(ni), idxL = new Uint32Array(ne);
    colF = new Float32Array(nv * 3);
    let vo = 0, io = 0, eo = 0;
    for (const s of shapes) {
      const v0 = vo;
      for (const q of s.verts) { posF[3 * vo] = q[0]; posF[3 * vo + 1] = q[2]; posF[3 * vo + 2] = -q[1]; vo++; }
      for (const i of s.tris) idxF[io++] = v0 + i;
      for (const i of s.edges) idxL[eo++] = v0 + i;
      recs.push({ v0, v1: vo, i0: io - s.tris.length, i1: io, e0: eo - s.edges.length, e1: eo, kind: s.p.k, state: s.p.s || 0, min: s.p.min, max: s.p.max });
    }
    // the lines and the triangles share the position and color attributes and differ in their index
    colAttr = new THREE.BufferAttribute(colF, 3);
    const posAttr = new THREE.BufferAttribute(posF, 3);
    const geo = new THREE.BufferGeometry(), gf = new THREE.BufferGeometry();
    for (const g of [geo, gf]) { g.setAttribute('position', posAttr); g.setAttribute('color', colAttr); }
    gf.setIndex(new THREE.BufferAttribute(idxF, 1));
    geo.setIndex(new THREE.BufferAttribute(idxL, 1));
    lines = new THREE.LineSegments(geo,new THREE.LineBasicMaterial({ vertexColors: true, transparent: true, opacity: 0.95, depthTest: false }));
    fills = new THREE.Mesh(gf, new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, opacity: 0.06, depthTest: false, depthWrite: false,
                                                             side: THREE.DoubleSide }));
    for (const o of [fills, lines]) { o.renderOrder = 10; o.frustumCulled = false; root.add(o); }
    highlight(null);
  }

  // boxes: [{min, max}] in the game's axes. The paths that touch one keep their color, the rest fade; null: all full.
  function highlight(boxes) {
    if (!recs.length || !colF) return;
    const any = boxes && boxes.length;
    for (const r of recs) {
      let k = r.state ? 0.5 : 1;
      if (any) {
        const hit = boxes.some((b) => b.min[0] <= r.max[0] + 1 && b.max[0] >= r.min[0] - 1 && b.min[1] <= r.max[1] + 1 && b.max[1] >= r.min[1] - 1 &&
                                       b.min[2] <= r.max[2] + 1 && b.max[2] >= r.min[2] - 1);
        k *= hit ? 1 : 0.28;
      }
      const c = KIND[r.kind] || KIND.other;
      for (let i = r.v0; i < r.v1; i++) { colF[3 * i] = c[0] * k; colF[3 * i + 1] = c[1] * k; colF[3 * i + 2] = c[2] * k; }
    }
    colAttr.needsUpdate = true;
  }

  return {
    set, clear: () => { dispose(); data = null; }, highlight,
    setVisible(on) { root.visible = !!on; },
    get visible() { return root.visible; },
    get loaded() { return !!data; },
    get info() { return { ...text, shown: recs.length }; },
  };
}
