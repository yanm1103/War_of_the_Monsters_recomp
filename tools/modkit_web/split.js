// Dividers: dragging a bar sets the size of the box next to it (a column's width or a row's height), kept in this browser.
// axis 'x' = a width (the bar is a vertical line), 'y' = a height; after = the box comes after the bar (dragging toward it
// shrinks it); max() = the largest size it may take now.
export function makeSplit(bar, box, { axis = 'x', key, min = 120, max = () => Infinity, after = false }) {
  const prop = axis === 'x' ? 'width' : 'height';
  const set = v => {
    v = Math.round(Math.max(min, Math.min(max(), v)));
    box.style.flex = '0 0 ' + v + 'px';
    return v;
  };
  const saved = key ? parseFloat(localStorage.getItem(key) || '') : NaN;
  if (saved > 0) requestAnimationFrame(() => set(saved));
  bar.addEventListener('pointerdown', ev => {
    if (ev.button !== 0) return;
    ev.preventDefault();
    const p0 = axis === 'x' ? ev.clientX : ev.clientY;
    const s0 = box.getBoundingClientRect()[prop];
    let last = s0;
    bar.setPointerCapture(ev.pointerId);
    document.body.classList.add(axis === 'x' ? 'dragx' : 'dragy');
    const move = e => {
      const d = (axis === 'x' ? e.clientX : e.clientY) - p0;
      last = set(s0 + (after ? -d : d));
    };
    const up = () => {
      bar.removeEventListener('pointermove', move);
      bar.removeEventListener('pointerup', up);
      bar.removeEventListener('pointercancel', up);
      document.body.classList.remove('dragx', 'dragy');
      if (key) localStorage.setItem(key, String(last));
    };
    bar.addEventListener('pointermove', move);
    bar.addEventListener('pointerup', up);
    bar.addEventListener('pointercancel', up);
  });
  bar.addEventListener('dblclick', () => {   // back to the page's own layout
    box.style.flex = '';
    if (key) localStorage.removeItem(key);
  });
}
