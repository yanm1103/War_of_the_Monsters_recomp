// The ruler (M): click two points of the stage and read the distance between them. measure() is the arithmetic, in game units and axes
// (x, y ground, z up; the heading as the game counts it: 0 faces +Y, 90 faces +X); createRuler() draws it in the view.
// createRuler({ THREE, scene, camera, host, surface(ndc) -> THREE.Vector3 | null, toGame(v) -> [x, y, z], onChange(text) }):
//   click(ndc) the first click sets A, the second B, the third starts again; setOn(false) clears; update() once a frame (keeps the
//   marks one size on the screen and the label over the middle of the line). The CSS is in style.css (.ruler-label).

export function measure(a, b) {
  const dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
  const horizontal = Math.hypot(dx, dy);
  const heading = horizontal < 1e-9 ? 0 : (((Math.atan2(dx, dy) * 180) / Math.PI) % 360 + 360) % 360;
  return { distance: Math.hypot(horizontal, dz), horizontal, height: dz, heading, dx, dy };
}

const n0 = (v) => String(Math.round(v));
export function describe(m) {
  return `${n0(m.distance)} units (flat ${n0(m.horizontal)}, ${m.height >= 0 ? 'up' : 'down'} ${n0(Math.abs(m.height))}, heading ${n0(m.heading)}°)`;
}

export function createRuler(c) {
  const { THREE } = c;
  const group = new THREE.Group();
  group.visible = false;
  c.scene.add(group);
  const mat = (color) => new THREE.MeshBasicMaterial({ color, depthTest: false, transparent: true, opacity: 0.95 });
  const marks = [0, 1].map((k) => {
    const m = new THREE.Mesh(new THREE.SphereGeometry(1, 12, 8), mat(k ? 0xFF8A3D : 0x4AE08A));
    m.renderOrder = 1000;
    m.raycast = () => {};
    m.visible = false;
    group.add(m);
    return m;
  });
  const line = new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3(0, 1, 0)]),
                              new THREE.LineBasicMaterial({ color: 0xFFFFFF, depthTest: false, transparent: true, opacity: 0.9 }));
  line.renderOrder = 999;
  line.raycast = () => {};
  line.visible = false;
  group.add(line);
  const label = document.createElement('div');
  label.className = 'ruler-label';
  label.hidden = true;
  c.host.append(label);
  const pts = [];
  let on = false, text = '';

  function redraw() {
    marks.forEach((m, k) => { m.visible = !!pts[k]; if (pts[k]) m.position.copy(pts[k]); });
    line.visible = pts.length === 2;
    if (line.visible) line.geometry.setFromPoints(pts);
    text = pts.length === 2 ? describe(measure(c.toGame(pts[0]), c.toGame(pts[1]))) : '';
    label.textContent = text;
    label.hidden = !on || !text;
    if (c.onChange) c.onChange(text);
  }
  const tmp = new THREE.Vector3();
  return {
    get on() { return on; },
    get text() { return text; },
    setOn(v) {
      on = !!v;
      group.visible = on;
      if (!on) pts.length = 0;
      redraw();
    },
    click(ndc) {
      const p = c.surface(ndc);
      if (!p) return false;
      if (pts.length >= 2) pts.length = 0;
      pts.push(p.clone());
      redraw();
      return true;
    },
    update() {
      if (!on) return;
      marks.forEach((m) => {
        if (!m.visible) return;
        const d = c.camera.position.distanceTo(m.position);
        m.scale.setScalar(Math.max(d * 0.008, 0.5));
      });
      if (pts.length === 2) {
        tmp.copy(pts[0]).add(pts[1]).multiplyScalar(0.5).project(c.camera);
        label.hidden = tmp.z > 1;
        const w = c.host.clientWidth, h = c.host.clientHeight;
        label.style.left = ((tmp.x + 1) / 2) * w + 'px';
        label.style.top = ((1 - tmp.y) / 2) * h + 'px';
      }
    },
  };
}
