// The minimap (N, Layers menu): the stage seen from above in a corner of the view, with a dot for each object, the selection in amber and
// the camera as an arrow; a click or a drag on it takes the camera there. fit/toMap/fromMap are the arithmetic (game x to the right,
// game y up); createMinimap draws it into a <canvas>.
// createMinimap({ canvas, bounds() -> {min: [x, y], max: [x, y]}, dots() -> [{x, y, c: css color, r: radius, sel}], cam() -> {x, y, heading}
//   (heading in degrees as the game counts it: 0 faces +Y, 90 +X), go(x, y) }); draw() once in a while, setOn(bool).
// The CSS is in style.css (#minimap).

export function fit(bounds, w, h, pad = 6) {
  const sx = Math.max(bounds.max[0] - bounds.min[0], 1), sy = Math.max(bounds.max[1] - bounds.min[1], 1);
  const scale = Math.min((w - 2 * pad) / sx, (h - 2 * pad) / sy);
  // centered: the stage's middle on the canvas' middle
  return { scale, cx: (bounds.min[0] + bounds.max[0]) / 2, cy: (bounds.min[1] + bounds.max[1]) / 2, w, h };
}
export const toMap = (f, x, y) => [f.w / 2 + (x - f.cx) * f.scale, f.h / 2 - (y - f.cy) * f.scale];
export const fromMap = (f, px, py) => [f.cx + (px - f.w / 2) / f.scale, f.cy - (py - f.h / 2) / f.scale];

export function createMinimap(c) {
  const cv = c.canvas, g = cv.getContext('2d');
  let on = false, f = null, drag = false;
  const go = (e) => {
    if (!f) return;
    const r = cv.getBoundingClientRect(), [x, y] = fromMap(f, ((e.clientX - r.left) / r.width) * cv.width, ((e.clientY - r.top) / r.height) * cv.height);
    c.go(x, y);
  };
  cv.addEventListener('pointerdown', (e) => { e.stopPropagation(); drag = true; cv.setPointerCapture(e.pointerId); go(e); });
  cv.addEventListener('pointermove', (e) => { if (drag) go(e); });
  cv.addEventListener('pointerup', () => { drag = false; });
  return {
    get on() { return on; },
    setOn(v) { on = !!v; cv.hidden = !on; if (on) this.draw(); },
    draw() {
      if (!on) return;
      const b = c.bounds();
      g.clearRect(0, 0, cv.width, cv.height);
      if (!b) { f = null; return; }
      f = fit(b, cv.width, cv.height);
      const [x0, y0] = toMap(f, b.min[0], b.max[1]), [x1, y1] = toMap(f, b.max[0], b.min[1]);
      g.fillStyle = 'rgba(255,255,255,0.05)';
      g.fillRect(x0, y0, x1 - x0, y1 - y0);
      g.strokeStyle = 'rgba(255,255,255,0.35)';
      g.strokeRect(x0 + 0.5, y0 + 0.5, x1 - x0 - 1, y1 - y0 - 1);
      const top = [];
      for (const d of c.dots()) {
        if (d.sel) { top.push(d); continue; }
        const [px, py] = toMap(f, d.x, d.y);
        g.fillStyle = d.c;
        g.fillRect(px - d.r, py - d.r, d.r * 2, d.r * 2);
      }
      for (const d of top) {     // the selection over the rest
        const [px, py] = toMap(f, d.x, d.y);
        g.fillStyle = '#ffb020';
        g.fillRect(px - 3, py - 3, 6, 6);
      }
      const cam = c.cam();
      if (cam) {
        const [px, py] = toMap(f, cam.x, cam.y), a = (cam.heading * Math.PI) / 180, s = Math.sin(a), co = Math.cos(a);
        // an arrow: the tip ahead (the map's up is +Y, so the screen y is minus the game's)
        g.fillStyle = '#4aa3ff';
        g.strokeStyle = '#0b1220';
        g.lineWidth = 1.5;
        g.beginPath();
        g.moveTo(px + s * 9, py - co * 9);
        g.lineTo(px - s * 5 - co * 5, py + co * 5 - s * 5);
        g.lineTo(px - s * 2, py + co * 2);
        g.lineTo(px - s * 5 + co * 5, py + co * 5 + s * 5);
        g.closePath();
        g.fill();
        g.stroke();
      }
    },
  };
}
