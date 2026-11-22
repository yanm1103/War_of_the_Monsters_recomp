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
  if (n.label.toLowerCase().includes(q) || (n.pats || []).some(p => p.toLowerCase().includes(q))) return true;
  return (n.kids || []).some(k => matches(k, q));
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
    const d = document.createElement('div');
    d.className = 'row' + (sel.has(r.id) ? ' sel' : '');
    d.style.paddingLeft = (8 + r.depth * 16) + 'px';
    d.dataset.id = r.id;
    const a = document.createElement('span');
    a.className = 'arrow';
    a.textContent = r.node.kids && r.node.kids.length ? ((q || open.has(r.id)) ? '▼' : '▶') : '';
    a.onclick = e => { e.stopPropagation(); toggle(r.id); };
    const l = document.createElement('span');
    l.className = 'lbl';
    l.textContent = r.node.label;
    d.append(a, l);
    box.appendChild(d);
  }
  if (!rows.length && $('discTree').dataset.msg) box.textContent = $('discTree').dataset.msg;
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
  $('discTree').dataset.msg = r.error ? '(' + r.error + ')' : '';
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
