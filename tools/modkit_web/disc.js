// The game disc by name (left pane): groups, monsters with their costumes and sound banks, levels, menus, sounds. Search
// filters it; Ctrl/Shift pick several; double-click or "Extract selected" sends them to the mod's work folder.
import { listSelect } from './select.js';

const $ = id => document.getElementById(id);
let tree = [];          // [{label, pats, kids}] from the server
let shownFor = null;    // the disc the tree was read from
const open = new Set(); // ids of the open nodes ("0", "0/3")
let rows = [];          // visible rows: {id, node, depth}
let sel;

function matches(n, q) {
  if (!q) return true;
  if (n.label.toLowerCase().includes(q) || (n.sub || '').toLowerCase().includes(q) || (n.pats || []).some(p => p.toLowerCase().includes(q))) return true;
  return (n.kids || []).some(k => matches(k, q));
}

// line icons (24x24, drawn with the text's color)
const SVG = {
  cube: '<path d="M12 2.5 3.5 7v10l8.5 4.5 8.5-4.5V7z"/><path d="m3.5 7 8.5 4.5L20.5 7M12 11.5v10"/>',
  image: '<rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="8.5" cy="9.5" r="1.8"/><path d="m21 16-5-5-9 9"/>',
  note: '<path d="M9 18V5l11-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="17" cy="16" r="3"/>',
  wave: '<path d="M2 12h2M6 8v8M10 4v16M14 7v10M18 10v4M22 12h-2"/>',
  bars: '<path d="M5 20V11M11 20V5M17 20v-6M2 20.5h20"/>',
  chip: '<rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2.5V6M15 2.5V6M9 18v3.5M15 18v3.5M2.5 9H6M2.5 15H6M18 9h3.5M18 15h3.5"/>',
  link: '<path d="M10 14a5 5 0 0 0 7 0l3-3a5 5 0 0 0-7-7l-1 1"/><path d="M14 10a5 5 0 0 0-7 0l-3 3a5 5 0 0 0 7 7l1-1"/>',
  screen: '<rect x="2.5" y="4" width="19" height="13" rx="2"/><path d="M8 21h8M12 17v4"/>',
  mountain: '<path d="m2.5 20 7-12 4 7 2.5-4 5.5 9z"/><circle cx="17" cy="6" r="2"/>',
  mic: '<rect x="9" y="2.5" width="6" height="12" rx="3"/><path d="M5 11a7 7 0 0 0 14 0M12 18v3.5"/>',
  file: '<path d="M14 2.5H6.5a2 2 0 0 0-2 2v15a2 2 0 0 0 2 2h11a2 2 0 0 0 2-2V8z"/><path d="M14 2.5V8h5.5"/>',
};
export function svg(name) {
  return '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">' +
         (SVG[name] || SVG.file) + '</svg>';
}
// a kind of row (disc groups, work groups) -> its icon
const KINDS = { sound: 'note', music: 'wave', screen: 'screen', level: 'mountain', files: 'file' };
export function icon(kind) {   // a small box with an icon for rows without a picture
  const s = document.createElement('span');
  s.className = 'ico ' + (kind || 'files');
  s.innerHTML = svg(KINDS[kind] || 'file');
  return s;
}
// a work folder file without a thumbnail -> an icon and a word for its kind
const FILES = {
  '.ngp': ['cube', 'Model', 'model'], '.glb': ['cube', 'Source', 'source'], '.gltf': ['cube', 'Source', 'source'],
  '.blend': ['cube', 'Source', 'source'], '.dae': ['cube', 'Model', 'model'], '.ptr': ['link', 'Model', 'model'],
  '.tex': ['image', 'Textures', 'texture'], '.rtx': ['image', 'Textures', 'texture'], '.psd': ['image', 'Source', 'source'], '.xcf': ['image', 'Source', 'source'], '.kra': ['image', 'Source', 'source'],
  '.pdn': ['image', 'Source', 'source'],
  '.wav': ['note', 'Sound', 'sound'], '.bnk': ['note', 'Sounds', 'sound'], '.vpk': ['wave', 'Music', 'music'],
  '.vag': ['wave', 'Music', 'music'], '.mp3': ['mic', 'Voice', 'music'], '.chr': ['bars', 'Stats', 'stats'], '.aic': ['chip', 'AI', 'ai'],
};
export function fileIcon(ext) {
  const [name, word, cls] = FILES[ext] || ['file', (ext || '').slice(1).toUpperCase().slice(0, 6) || 'File', 'other'];
  const s = document.createElement('span');
  s.className = 'thumb ftype ' + cls;
  s.innerHTML = svg(name);
  const w = document.createElement('span');
  w.textContent = word;
  s.appendChild(w);
  return s;
}

// a square picture for a row or a card: a portrait (api/portrait, api/hud) is its normal face, with the rage one shown while
// the pointer is over the row; any other picture (a level's) is cropped square; without one (or when it fails), the glyph
const PORTRAIT = /api\/(portrait|hud)\?/;
export function pic(url, kind, cls) {
  const box = document.createElement('span');
  box.className = 'pic' + (cls ? ' ' + cls : '');
  if (!url) {
    const i = icon(kind);
    if (cls) i.classList.add(cls);
    return i;
  }
  const faces = PORTRAIT.test(url) && !/[?&]logo=1/.test(url) ? [['n', url + '&part=normal'], ['r', url + '&part=rage']] : [['n', url]];
  for (const [c, u] of faces) {
    const im = document.createElement('img');
    im.className = c;
    im.loading = 'lazy';
    im.alt = '';
    im.src = u;
    if (c === 'n') im.onerror = () => box.replaceWith(pic(null, kind, cls));
    else im.onerror = () => im.remove();
    box.appendChild(im);
  }
  return box;
}

function render() {
  const q = $('discFilter').value.trim().toLowerCase();
  const box = $('discTree');
  box.innerHTML = '';
  rows = [];
  const walk = (nodes, depth, pre) => nodes.forEach((n, i) => {
    if (!matches(n, q)) return;
    const id = pre + i;
    rows.push({ id, node: n, depth });
    if (n.kids && n.kids.length && (q || open.has(id))) walk(n.kids, depth + 1, id + '/');
  });
  walk(tree, 0, '');
  for (const r of rows) {
    const n = r.node;
    const d = document.createElement('div');
    d.className = 'row' + (sel.has(r.id) ? ' sel' : '') + (r.depth === 0 ? ' cat' : '') + (n.logo ? ' mon' : '');
    d.style.paddingLeft = (8 + r.depth * 16) + 'px';
    d.dataset.id = r.id;
    const a = document.createElement('span');
    a.className = 'arrow';
    a.textContent = n.kids && n.kids.length ? ((q || open.has(r.id)) ? '▼' : '▶') : '';
    a.onclick = e => { e.stopPropagation(); toggle(r.id); };
    d.appendChild(a);
    if (n.pic || n.icon) d.appendChild(pic(n.pic, n.icon));   // a costume's portrait, a level's picture, or a kind's glyph
    const l = document.createElement('span');
    l.className = 'lbl';
    l.textContent = n.label;
    d.appendChild(l);
    if (n.sub) {
      const s = document.createElement('span');
      s.className = 'sub';
      s.textContent = n.sub;
      d.appendChild(s);
    }
    box.appendChild(d);
  }
  if (!rows.length && $('discTree').dataset.msg) { const m = document.createElement('div'); m.className = 'empty'; m.textContent = $('discTree').dataset.msg; box.appendChild(m); }
}
function paintSel() {   // only the classes: the rows stay (a double click needs the same element twice)
  for (const d of $('discTree').children) if (d.dataset.id) d.classList.toggle('sel', sel.has(d.dataset.id));
}
function toggle(id) { if (open.has(id)) open.delete(id); else open.add(id); render(); }

function selectedPats() {
  const pats = [];
  for (const id of sel.all()) {   // by the path of indices: a picked costume counts even when its group is closed
    let n = { kids: tree };
    for (const i of id.split('/')) n = n && n.kids ? n.kids[+i] : null;
    if (n && n.pats) pats.push(...n.pats);
  }
  return [...new Set(pats)];
}

async function extract() {
  const pats = selectedPats();
  if (pats.length > 12 && !await kit.confirm('Extract', 'Extract ' + pats.length + ' files? This can take a few minutes.', 'Extract')) return;
  const r = await kit.run('extract', { pats });
  if (r.ok && r.focus) kit.workFocus = r.focus;   // the work list opens these groups when the task is done
}

async function load() {
  const r = await kit.api('do/disc', {});
  tree = r.tree || [];
  $('discTree').dataset.msg = r.error || '';
  render();
}

export function initDisc() {
  sel = listSelect($('discTree'), () => rows.map(r => r.id), paintSel);
  $('discFilter').addEventListener('input', render);
  $('discTree').addEventListener('dblclick', e => {
    const d = e.target.closest('.row');
    if (!d || e.target.classList.contains('arrow')) return;
    const r = rows.find(x => x.id === d.dataset.id);
    if (r && r.node.kids && r.node.kids.length && !r.node.pats) { toggle(r.id); return; }
    extract();
  });
  $('extract').onclick = extract;
  kit.onState(s => { if (s.disc !== shownFor) { shownFor = s.disc; load(); } });
  kit.disc = { selectedPats, select: ids => { sel.set(ids); render(); }, rows: () => rows.map(r => ({ id: r.id, label: r.node.label })),
               open: ids => { ids.forEach(i => open.add(i)); render(); } };
}
