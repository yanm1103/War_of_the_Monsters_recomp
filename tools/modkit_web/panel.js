// The frame of a skin's or a custom monster's panel (in place of the preview): the model in 3D with its animations on top, a
// divider that sets the 3D view's height, then the form. tabs() splits the form into pages (the last one picked is kept);
// block() is one group of actions: a title, a line saying what they do, the buttons and a state line.
import { showModel, animBar } from './viewer.js';
import { makeSplit } from './split.js';

export const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
export const button = (text, f, cls) => { const b = el('button', cls || '', text); b.type = 'button'; b.onclick = f; return b; };

export function panelFrame(body, groupKey) {
  const wrap = el('div', 'panel');
  const top = el('div', 'pvtop');
  const v = el('div', 'v3d');
  const ab = el('div');
  top.append(v, ab);
  const bar = el('div', 'hsplit');
  bar.title = 'Drag to resize the 3D view; double-click: back to the default';
  const form = el('div', 'props');
  wrap.append(top, bar, form);
  body.appendChild(wrap);
  const still = 'api/model.glb?group=' + encodeURIComponent(groupKey) + '&t=' + Date.now();
  showModel(v, still, true);   // the kit's export: the game's axes
  animBar(ab, groupKey, still);
  makeSplit(bar, top, { axis: 'y', key: 'wotm.v3dH', min: 140, max: () => wrap.clientHeight - 150 });
  return form;
}

// tabs under parent: defs = [label, ...]; returns the page elements, in order
export function tabs(parent, labels, key) {
  const head = el('div', 'tabs');
  const body = el('div', 'tabbody');
  parent.append(head, body);
  const pages = labels.map(() => { const p = el('div', 'tabpage'); p.hidden = true; body.appendChild(p); return p; });
  const btns = labels.map((t, i) => { const b = button(t, () => show(i)); head.appendChild(b); return b; });
  function show(i) {
    pages.forEach((p, j) => { p.hidden = j !== i; });
    btns.forEach((b, j) => b.classList.toggle('on', j === i));
    localStorage.setItem(key, labels[i]);
  }
  show(Math.max(0, labels.indexOf(localStorage.getItem(key))));
  return pages;
}

// one group of actions: kids are buttons (or anything for the button line); state: a line under them (text or element)
export function block(title, text, kids, state) {
  const b = el('div', 'block');
  b.append(el('h5', '', title), el('p', '', text));
  const line = el('div', 'btns');
  line.append(...kids);
  b.appendChild(line);
  if (state) b.appendChild(typeof state === 'string' ? el('div', 'state', state) : state);
  return b;
}
