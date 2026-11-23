// The frame of a skin's or a custom monster's panel (in place of the preview): the model in 3D with its animations on top, a
// divider that sets the 3D view's height, then the form. tabs() splits the form into pages (the last one picked is kept);
// block() is one group of actions: a title, a line saying what they do, the buttons and a state line.
import { showModel, animBar } from './viewer.js';
import { makeSplit } from './split.js';

export const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
export const button = (text, f, cls) => { const b = el('button', cls || '', text); b.type = 'button'; b.onclick = f; return b; };

// the monster's name over the 3D view, as the menus show it: its logo from the disc (or the custom's own "logo"), else the
// name in big letters
export function stageTitle(v, g) {
  if (!g) return;
  const name = el('div', 'name3d', g.title || '');
  if (g.logo) {
    const im = el('img', 'logo3d');
    im.alt = '';
    im.onerror = () => { im.remove(); v.appendChild(name); };
    im.src = g.logo;
    v.appendChild(im);
  } else if (g.title) v.appendChild(name);
}

export function panelFrame(body, groupKey, g) {
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
  stageTitle(v, g);   // after: showModel empties the box
  animBar(ab, groupKey, still);
  makeSplit(bar, top, { axis: 'y', key: 'wotm.v3dH', min: 140, max: () => wrap.clientHeight - 150 });
  return form;
}

// tabs under parent: defs = [label, ...]; returns the page elements, in order. onShow(label) runs on every change of tab
// (and for the first one, right after the pages are made: pages.shown() tells it again once the caller filled them)
export function tabs(parent, labels, key, onShow) {
  const head = el('div', 'tabs');
  const body = el('div', 'tabbody');
  parent.append(head, body);
  const pages = labels.map(() => { const p = el('div', 'tabpage'); p.hidden = true; body.appendChild(p); return p; });
  const btns = labels.map((t, i) => { const b = button(t, () => show(i)); head.appendChild(b); return b; });
  function show(i) {
    pages.forEach((p, j) => { p.hidden = j !== i; });
    btns.forEach((b, j) => b.classList.toggle('on', j === i));
    localStorage.setItem(key, labels[i]);
    if (onShow) onShow(labels[i]);
  }
  show(Math.max(0, labels.indexOf(localStorage.getItem(key))));
  pages.shown = () => { if (onShow) onShow(labels[btns.findIndex(b => b.classList.contains('on'))]); };
  return pages;
}

// settings that save themselves: save() runs on every change of the elements (text fields when they lose focus or on Enter);
// it returns an error text or nothing. -> the status line ("Saved" for a moment, or the error)
export function autosave(elems, save) {
  const st = el('div', 'autosave', 'Changes save by themselves.');
  let n = 0;
  const run = async () => {
    const my = ++n;
    st.className = 'autosave busy';
    st.textContent = 'Saving…';
    const err = await save();
    if (my !== n) return;
    st.className = 'autosave ' + (err ? 'err' : 'ok');
    st.textContent = err || '✓ Saved';
    if (!err) setTimeout(() => { if (my === n) { st.className = 'autosave'; st.textContent = 'Changes save by themselves.'; } }, 1800);
  };
  for (const e of elems) {
    e.addEventListener('change', run);
    if (e.tagName === 'INPUT' && (!e.type || e.type === 'text')) e.addEventListener('keydown', ev => { if (ev.key === 'Enter') e.blur(); });
  }
  return st;
}

// a short note next to an element (the result of a check, "Saved"): shows for a few seconds; kind ok | warn | err
export function toast(next, text, kind) {
  const t = el('span', 'toast ' + (kind || 'ok'), text);
  next.insertAdjacentElement('afterend', t);
  setTimeout(() => t.classList.add('gone'), 5000);
  setTimeout(() => t.remove(), 5600);
  return t;
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
