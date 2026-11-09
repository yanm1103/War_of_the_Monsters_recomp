// The text labels over the markers of the view (start points: which one it is; net nodes: the number the list shows; the hovered and the
// selected marker: its name). DOM elements in the view's host, so that the type is sharp at any distance and takes the theme; they never
// take the pointer. The arithmetic of which ones are drawn is marker_math.js (layoutLabels); the CSS is style.css (.mk-label).
// createMarkerLabels(host) -> { set(items, w, h), shown() }
//   set: items [{key, text, x, y, prio, color, cls, w, stack}] (x, y: the bottom centre of the label in host pixels; stack: may sit above another label that meets it). A label keeps its element
//        as long as it stays drawn, and an element is written only when its text, class, color or (whole pixel) place changed.
//   shown: [{text, cls, x, y}] of what the last set drew (the tests read it)
import { layoutLabels } from './marker_math.js';

export function createMarkerLabels(host) {
  const used = new Map(), free = [];
  let drawn = [];
  function take(key) {
    let d = used.get(key);
    if (d) return d;
    d = free.pop();
    if (!d) {
      d = document.createElement('div');
      d.hidden = true;
      d._s = { text: '', cls: '', color: '', x: NaN, y: NaN, hidden: true };
      host.append(d);
    }
    used.set(key, d);
    return d;
  }
  return {
    set(items, w, h, max = 40) {
      const keep = layoutLabels(items, w, h, max);
      drawn = [];
      for (const [key, d] of used) {
        if (keep.has(key)) continue;
        used.delete(key);
        free.push(d);
        if (!d._s.hidden) { d.hidden = true; d._s.hidden = true; }
      }
      for (const it of items) {
        if (!keep.has(it.key)) continue;
        const d = take(it.key), s = d._s, x = Math.round(it.x), y = Math.round(it.y + keep.get(it.key)), cls = 'mk-label' + (it.cls ? ' ' + it.cls : '');
        if (s.text !== it.text) { d.textContent = it.text; s.text = it.text; }
        if (s.cls !== cls) { d.className = cls; s.cls = cls; }
        if (s.color !== it.color) { d.style.setProperty('--c', it.color); s.color = it.color; }
        if (s.x !== x || s.y !== y) { d.style.transform = `translate(${x}px, ${y}px) translate(-50%, -100%)`; s.x = x; s.y = y; }
        if (s.hidden) { d.hidden = false; s.hidden = false; }
        drawn.push({ text: it.text, cls: it.cls || '', x, y });
      }
    },
    shown: () => drawn.slice(),
  };
}
