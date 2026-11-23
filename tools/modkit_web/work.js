// The mod's work folder (right pane): one group per disc file, skin or custom monster, each file with its thumbnail, size and
// state ("edited" = changed since the extract: it goes in the next build); the preview of the picked file (texture on a
// checkerboard, sound as a waveform) and what can be done with the files (open, replace, save, revert, remove).
// kit.work.panels: other parts of the page register a panel for a group kind (a skin's or a monster's properties).
import { listSelect } from './select.js';
import { showModel, animBar } from './viewer.js';
import { pic, fileIcon } from './disc.js';
import { stageTitle } from './panel.js';

const MODEL_EXT = ['.glb', '.gltf', '.dae', '.ngp'];

const $ = id => document.getElementById(id);
let data = { groups: [] };
let shownWork = null;
const open = new Set();   // keys of the open groups
let rows = [];            // visible rows: {id, group, file}
let sel;
let lastPlayed = null;
const panels = {};        // group kind -> f(group, box) fills the preview area with its panel
const menus = [];         // f(ctx, add) adds items to the right-click menu
let reqN = 0;

const fileUrl = f => 'api/file?rel=' + encodeURIComponent(f.rel) + '&m=' + f.mtime;
const ext = rel => (rel.match(/\.[^./]+$/) || [''])[0].toLowerCase();

const STATES = {   // the state column's hint
  original: 'As it is on the disc: Build leaves it out',
  edited: 'Changed since it was extracted: the next Build sends it to the mod',
  source: 'Stays in the work folder: Build does not send it to the game',
};
// the sections of a group's files (the server's "section"): key, heading, what happens to them. The game files start closed.
const SECTIONS = [
  ['edit', 'Textures and sounds', 'edit these; Build sends the ones you changed'],
  ['game', 'Game files', "in the game's format, made or copied by the kit; Build sends the changed ones as they are"],
  ['source', 'Sources', 'for editors (3D models, image projects); they stay in the work folder'],
];
const secState = new Map();   // "group|section" -> open
const secOpen = (gk, k) => secState.has(gk + '|' + k) ? secState.get(gk + '|' + k) : k !== 'game';
function secHead(gk, k, title, hint, files, shown) {
  const h = el('div', 'sec ' + k);
  const a = el('span', 'arrow', shown ? '▼' : '▶');
  const ne = files.filter(f => f.state === 'edited').length;
  h.append(a, el('span', 'stitle', title), el('span', 'scount', files.length + (ne ? ' · ' + ne + ' edited' : '')), el('span', 'shint', hint));
  h.title = title + ': ' + hint;
  h.onclick = () => { secState.set(gk + '|' + k, !shown); render(); };
  return h;
}
const TAGS = { skin: 'Skin', monster: 'Monster', costume: 'Costume', level: 'Level', sounds: 'Sounds', music: 'Music', screen: 'Screen', files: 'Files' };
const ICONS = { sounds: 'sound', music: 'music', screen: 'screen', level: 'level' };
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };

function groupPic(g) {   // the row's picture: a portrait or a level's picture from the disc, else a kind's glyph
  return pic(g.pic, ICONS[g.tag] || 'files');
}

// ---------------------------------------------------------------- welcome: no mod yet, or a mod with nothing in it
function card(cls, title, text, art, onclick) {
  const c = el('div', 'card ' + cls);
  if (art.length) {   // a picture on top (only New mod has one)
    const a = el('div', 'art');
    for (const x of art) a.appendChild(x);
    c.appendChild(a);
  }
  const t = el('div');
  c.appendChild(t);
  t.append(el('h4', '', title), el('p', '', text));
  c.onclick = onclick;
  return c;
}
const glyph = g => el('span', 'glyph', g);
function fromDisc() {   // the disc's list: the monsters open, the search box ready
  kit.disc.open(['0']);
  const f = $('discFilter');
  f.focus();
  f.select();
}
export function startCards() {   // New skin, New monster, From the disc
  const box = el('div', 'cards');
  box.append(
    card('c2', 'New skin', 'New colors and textures for one of the monsters, picked as a costume in the game.',
      [], () => kit.actions.newSkin()),
    card('c1', 'New monster', 'A fighter of its own on a base monster\'s moves: its name, look, 3D model, stats and sounds.',
      [], () => kit.actions.newMonster()),
    card('c3', 'From the disc', 'Any texture or sound of the game: pick it in the list on the left and extract it to edit.',
      [], fromDisc));
  return box;
}
function welcome() {
  const s = kit.state;
  const pane = $('workPane');
  const w = $('welcome');
  const empty = s && s.mod && !data.groups.length && !$('onlyEdited').checked && data.status !== undefined;
  const on = !!s && (!s.mod || empty);
  pane.classList.toggle('welcome', on);
  if (!on) { w.innerHTML = ''; w.dataset.for = ''; return; }
  const key = (s.mod || '') + '|' + s.mod_list.join(',') + '|' + s.disc_ok;
  if (w.dataset.for === key) return;
  w.dataset.for = key;
  w.innerHTML = '';
  if (!s.mod) {
    w.append(el('h3', '', s.mod_list.length ? 'Pick a mod' : 'Make your first mod'),
      el('p', '', s.mod_list.length ? 'Open one of your mods, or start a new one.' : 'A mod holds your skins, monsters, textures and sounds. Start one to begin.'));
    const cards = el('div', 'cards');
    cards.appendChild(card('c1', 'New mod', 'An empty mod with the name you choose.', [glyph('✚')], () => kit.newMod()));
    w.appendChild(cards);
    if (s.mod_list.length) {
      const ml = el('div', 'modlist');
      ml.appendChild(el('div', 'lead', 'Your mods:'));
      for (const n of s.mod_list) {
        const b = el('button', '', n);
        b.onclick = async () => kit.setState(await kit.api('conf', { mod: n }));
        ml.appendChild(b);
      }
      w.appendChild(ml);
    }
    return;
  }
  w.append(el('h3', '', s.mod + ' is empty'), el('p', '', 'Start with one of these:'), startCards());
}

function render() {
  const box = $('workTree');
  const top = box.scrollTop;
  box.innerHTML = '';
  rows = [];
  welcome();
  for (const g of data.groups) {
    const gid = 'grp:' + g.key;
    rows.push({ id: gid, group: g });
    const ne = g.files.filter(f => f.state === 'edited').length;
    const d = document.createElement('div');
    d.className = 'row group' + (sel.has(gid) ? ' sel' : '') + (ne ? ' edited' : '');
    d.dataset.id = gid;
    d.title = g.label;
    const a = document.createElement('span');
    a.className = 'arrow';
    a.textContent = g.files.length ? (open.has(g.key) ? '▼' : '▶') : '';
    a.onclick = e => { e.stopPropagation(); toggle(g.key); };
    const tx = el('span', 'gtext');
    const l = el('span', 'lbl', g.title || g.label);
    l.dataset.label = g.label;
    const nf = g.files.length + (g.files.length === 1 ? ' file' : ' files');
    tx.append(l, el('span', 'sub', (g.sub ? g.sub + '  ·  ' : '') + nf));
    const more = el('button', 'more', '⋯');
    more.type = 'button';
    more.title = 'What can be done with it';
    more.onclick = e => {
      e.stopPropagation();
      if (!sel.has(gid)) { sel.set([gid]); onSelect(); }
      const r = more.getBoundingClientRect();
      contextMenu({ preventDefault() {}, clientX: r.left, clientY: r.bottom + 2 });
    };
    d.append(a, groupPic(g), tx);
    if (ne) d.appendChild(el('span', 'chip ed', ne + ' edited'));
    d.append(el('span', 'chip ' + (g.tag || 'files'), TAGS[g.tag] || 'Files'), more);
    box.appendChild(d);
    if (!open.has(g.key)) continue;
    const secs = SECTIONS.filter(([k]) => g.files.some(f => (f.section || 'edit') === k));
    const items = [];   // headings and files, in order
    for (const [k, title, hint] of secs) {   // one section alone: no heading
      const fs = g.files.filter(f => (f.section || 'edit') === k);
      const shown = secs.length < 2 || secOpen(g.key, k);
      if (secs.length > 1) items.push(secHead(g.key, k, title, hint, fs, shown));
      if (shown) items.push(...fs);
    }
    for (const f of items) {
      if (f instanceof HTMLElement) { box.appendChild(f); continue; }
      rows.push({ id: f.rel, group: g, file: f });
      const r = document.createElement('div');
      r.className = 'row' + (sel.has(f.rel) ? ' sel' : '') + (f.state === 'edited' ? ' edited' : '');
      r.dataset.id = f.rel;
      r.style.paddingLeft = '40px';
      let th;
      if (ext(f.rel) === '.png') {
        th = document.createElement('img');
        th.loading = 'lazy';
        th.src = fileUrl(f);
        th.className = 'thumb';
      } else th = fileIcon(ext(f.rel));   // no thumbnail: an icon and a word for its kind
      const n = document.createElement('span');
      n.className = 'lbl';
      n.textContent = f.name;
      const i = document.createElement('span');
      i.className = 'info';
      i.textContent = f.info;
      const st = document.createElement('span');
      st.className = 'st';
      st.textContent = f.state;
      st.title = STATES[f.state] || '';
      r.append(th, n, i, st);
      box.appendChild(r);
    }
  }
  if (!data.groups.length && kit.state && kit.state.mod)
    box.appendChild(el('div', 'empty', $('onlyEdited').checked ? 'No edited files yet.' : 'Nothing here yet: extract something from the disc on the left.'));
  box.scrollTop = top;
}
function paintSel() {
  for (const d of $('workTree').children) if (d.dataset.id) d.classList.toggle('sel', sel.has(d.dataset.id));
}
function toggle(key) { if (open.has(key)) open.delete(key); else open.add(key); render(); }

// what is picked: files (rows of files only) or with groups (a group counts as all its files)
function selectedFiles(groups) {
  const out = [];
  for (const id of sel.ids()) {
    const r = rows.find(x => x.id === id);
    if (!r) continue;
    if (r.file) out.push(r.file);
    else if (groups) out.push(...r.group.files);
  }
  return [...new Map(out.map(f => [f.rel, f])).values()];
}
function selectedGroup() {   // the group, when the selection is one group alone
  const ids = sel.ids();
  if (ids.length !== 1 || !ids[0].startsWith('grp:')) return null;
  return data.groups.find(g => 'grp:' + g.key === ids[0]) || null;
}

async function load(focus) {
  const n = ++reqN;
  const r = await kit.api('do/work', { edited_only: $('onlyEdited').checked });
  if (n !== reqN) return;
  data = r;
  const valid = new Set();
  for (const g of data.groups) { valid.add('grp:' + g.key); g.files.forEach(f => valid.add(f.rel)); }
  sel.keepOnly(valid);
  if (focus) {   // after an extract: those groups open, the others close, the first file (or the skin/monster group) picked
    open.clear();
    const want = data.groups.filter(g => focus.includes(g.key));
    want.forEach(g => open.add(g.key));
    const g = want[0];
    if (g) sel.set([g.kind !== 'file' || !g.files.length ? 'grp:' + g.key : g.files[0].rel]);
  } else if (data.groups.length === 1 && !open.size) open.add(data.groups[0].key);
  $('status').textContent = data.status || '';
  render();
  if (focus) {
    const d = [...$('workTree').children].find(x => x.classList.contains('sel'));
    if (d) d.scrollIntoView({ block: 'nearest' });
  }
  preview();
}

// ---------------------------------------------------------------- preview
const WAVE_H = 200;   // the waveform's height at most: a tall preview pane does not make it a wall
async function waveform(url, box) {
  const c = document.createElement('canvas');
  c.className = 'wave';
  box.appendChild(c);
  const w = c.width = Math.max(50, box.clientWidth), h = c.height = Math.max(50, Math.min(WAVE_H, $('preview').clientHeight - 150));
  c.style.height = h + 'px';
  try {
    const buf = await (await fetch(url)).arrayBuffer();
    const ac = new OfflineAudioContext(1, 1, 44100);
    const a = (await ac.decodeAudioData(buf)).getChannelData(0);
    const g = c.getContext('2d');
    const cols = w - 16, step = Math.max(1, Math.floor(a.length / cols)), mid = h / 2, amp = (h - 24) / 2;
    g.strokeStyle = '#444';
    g.beginPath(); g.moveTo(8, mid); g.lineTo(w - 8, mid); g.stroke();
    g.fillStyle = '#2f6fd6';
    for (let x = 0; x < cols; x++) {
      let lo = 0, hi = 0;
      for (let i = x * step; i < Math.min(a.length, (x + 1) * step); i++) { const v = a[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
      g.fillRect(8 + x, mid - hi * amp, 1, Math.max(1, (hi - lo) * amp));
    }
  } catch (e) {
    c.remove();
    const t = document.createElement('div');
    t.className = 'big';
    t.textContent = 'sound';
    box.appendChild(t);
  }
}
async function preview() {
  const body = $('pvBody'), label = $('pvLabel');
  body.innerHTML = '';
  const g = selectedGroup();
  $('preview').classList.toggle('paneled', !!(g && (panels[g.kind] || g.custom_base)));   // a panel or a costume: the file buttons step aside
  if (g && panels[g.kind]) { panels[g.kind](g, body, label); return; }
  const files = selectedFiles(false), allf = selectedFiles(true);
  $('preview').classList.toggle('nofile', !files.length);   // the file buttons only with a file picked
  $('preview').classList.toggle('sound', files.some(f => ext(f.rel) === '.wav'));   // play, stop and volume only for sounds
  // a file with nothing to draw (.TEX, .PTR, .CHR...) or a section: no empty picture area, the text goes on top
  const drawn = files.length && ['.png', '.wav', ...MODEL_EXT].includes(ext(files[0].rel));
  $('preview').classList.toggle('noview', !g && (files.length ? !drawn : allf.length > 0));
  if (g && g.custom_base) {   // a costume of a monster: the model with the folder's textures on it, and what it can become
    const wrap = document.createElement('div');
    wrap.className = 'panel';
    const v = document.createElement('div');
    v.className = 'v3d';
    const ab = document.createElement('div');
    const acts = el('div', 'acts');
    const items = [];
    for (const f of menus) f({ group: g, files: [], allf }, (t, fn) => items.push([t, fn]), () => {});
    for (const [t, fn] of items) { const b = el('button', '', t); b.type = 'button'; b.onclick = fn; acts.appendChild(b); }
    wrap.append(v, ab, acts);
    body.appendChild(wrap);
    const still = 'api/model.glb?group=' + encodeURIComponent(g.key) + '&t=' + Date.now();
    showModel(v, still, true);
    stageTitle(v, g);   // after: showModel empties the box
    animBar(ab, g.key, still);
    label.textContent = (g.sub || g.label) + ': ' + allf.length + ' file(s). Edited textures show on the model (Refresh after saving one). Open it in the list to pick a file.';
    return;
  }
  if (!files.length && allf.length) {
    label.textContent = allf.length + ' file(s) in the selected section. Open it (arrow or double-click) and pick a file; the ⋯ button saves or removes the whole section.';
    return;
  }
  if (!files.length) {
    if (kit.state && kit.state.mod && data.groups.length) {
      const st = el('div', 'pvStart');
      st.append(el('h4', '', 'What next?'), startCards());
      body.appendChild(st);
    }
    label.textContent = kit.state && kit.state.mod ? 'Or pick something in the list. Double-click opens a texture in its editor (Setup, "Editors") and plays a sound.' : '';
    return;
  }
  const f = files[0];
  let text = f.name + '  ' + f.info;
  if (ext(f.rel) === '.png') {
    const img = new Image();
    img.onload = () => {   // whole multiples when it grows (sharp pixels), smaller when it does not fit
      const W = body.clientWidth - 16, H = body.clientHeight - 16;
      let s = Math.min(W / img.naturalWidth, H / img.naturalHeight);
      s = s < 1 ? s : Math.max(1, Math.floor(s));
      img.style.width = Math.max(1, Math.round(img.naturalWidth * s)) + 'px';
      img.style.height = Math.max(1, Math.round(img.naturalHeight * s)) + 'px';
      img.style.imageRendering = s >= 1 ? 'pixelated' : 'auto';
    };
    img.src = fileUrl(f);
    const ck = document.createElement('div');
    ck.className = 'checker';
    ck.appendChild(img);
    body.appendChild(ck);
  } else if (ext(f.rel) === '.wav') {
    waveform(fileUrl(f), body);
  } else if (MODEL_EXT.includes(ext(f.rel))) {   // a 3D file: the model itself
    const v = document.createElement('div');
    v.className = 'v3d';
    body.appendChild(v);
    showModel(v, 'api/model.glb?rel=' + encodeURIComponent(f.rel) + '&m=' + f.mtime, ext(f.rel) === '.ngp');
  }
  const note = (await kit.api('do/note', { rel: f.rel })).note;
  if (note) text += '\n' + note;
  if (files.length > 1) text += '\n(' + files.length + ' selected)';
  label.textContent = text;
}

// ---------------------------------------------------------------- actions
function play(f) {
  if (!f) return;
  lastPlayed = f.rel;
  const p = $('player');
  p.src = fileUrl(f);
  p.play().catch(() => {});
}
function stop() { lastPlayed = null; const p = $('player'); p.pause(); p.removeAttribute('src'); p.load(); }
function playSelected() { play(selectedFiles(false).find(f => ext(f.rel) === '.wav')); }

async function openSelected(exe, always) {
  const files = selectedFiles(false);
  if (files.length) await kit.api('do/open_files', { rels: files.map(f => f.rel), exe, always });
}
async function openWith() {
  const files = selectedFiles(false);
  if (!files.length) return;
  const e = ext(files[0].rel);
  const exe = await kit.pick('file', 'Program to open ' + e + ' files', (kit.state.editors || {})[e] || '', [['Programs', ['*.exe']]]);
  if (!exe) return;
  const always = await kit.confirm('Open with', 'Always open ' + e + ' files with ' + exe.split(/[\\/]/).pop() + '?', 'Yes');
  await openSelected(exe, always);
  if (always) kit.setState(await kit.api('state'));
}
async function replaceSelected() {
  const files = selectedFiles(false);
  if (files.length !== 1) return;
  const f = files[0], e = ext(f.rel);
  const types = e === '.png' ? [['Images', ['*.png', '*.jpg', '*.jpeg', '*.bmp', '*.tga', '*.gif', '*.tif', '*.tiff', '*.webp', '*.dds']]]
              : e === '.wav' ? [['WAV sound', ['*.wav']]] : [];
  const src = await kit.pick('file', 'Replace ' + f.name + ' with', kit.state.replace_dir, types);
  if (!src) return;
  stop();
  const r = await kit.api('do/replace', { rel: f.rel, src });
  if (r.error) kit.say(r.error + '\n', 'err');
  await load();
  sel.set([f.rel]);
  paintSel();
  preview();
}
async function saveTo() {
  const files = selectedFiles(true);
  if (!files.length) return;
  const dir = await kit.pick('dir', 'Save ' + files.length + ' file(s) to', kit.state.save_dir);
  if (!dir) return;
  const rels = files.map(f => f.rel);
  let r = await kit.api('do/save_to', { rels, dir });
  if (r.exist) {
    const over = await kit.confirm('Save to folder', r.exist + ' file(s) already exist in ' + dir + '. Overwrite?', 'Overwrite');
    r = await kit.api('do/save_to', { rels, dir, overwrite: over });
  }
  if (r.error) kit.say(r.error + '\n', 'err');
  kit.setState(await kit.api('state'));
}
async function revertSelected() {
  const rels = (await kit.api('do/edited', { rels: selectedFiles(true).map(f => f.rel) })).rels;
  if (!rels.length) { kit.say('Nothing to revert: the selected files are as extracted.\n'); return; }
  const names = rels.slice(0, 10).map(r => r.split('/').pop()).join('\n') + (rels.length > 10 ? '\n...' : '');
  if (!await kit.confirm('Revert to original', 'Put ' + rels.length + ' file(s) back as they are on the disc? Your changes to them are lost.\n\n' + names, 'Revert')) return;
  stop();
  kit.run('revert', { rels });
}
async function removeSelected() {
  const files = selectedFiles(true);
  if (!files.length) return;
  if (!await kit.confirm('Remove', 'Delete ' + files.length + ' file(s) from the work folder? (the disc and the mods do not change)', 'Delete')) return;
  stop();
  await kit.api('do/remove', { rels: files.map(f => f.rel) });
  load();
}
async function reveal() { await kit.api('do/reveal', { rels: selectedFiles(true).map(f => f.rel) }); }

// the right-click menu
function showMenu(x, y, items) {
  const m = $('menu');
  m.innerHTML = '';
  for (const it of items) {
    if (it === '-') { if (m.lastChild && m.lastChild.tagName !== 'HR') m.appendChild(document.createElement('hr')); continue; }
    const d = document.createElement('div');
    d.textContent = it[0];
    d.onclick = () => { hideMenu(); it[1](); };
    m.appendChild(d);
  }
  if (m.lastChild && m.lastChild.tagName === 'HR') m.lastChild.remove();
  if (!m.children.length) return;
  m.style.display = 'block';
  m.style.left = Math.min(x, window.innerWidth - m.offsetWidth - 4) + 'px';
  m.style.top = Math.min(y, window.innerHeight - m.offsetHeight - 4) + 'px';
}
function hideMenu() { $('menu').style.display = 'none'; }
async function contextMenu(e) {
  e.preventDefault();
  const files = selectedFiles(false), allf = selectedFiles(true), g = selectedGroup();
  const items = [];
  const add = (label, f) => items.push([label, f]);
  const ctx = { group: g, files, allf };
  for (const f of menus) f(ctx, add, () => items.push('-'));
  if (items.length) items.push('-');
  const n = allf.length, s = n === 1 ? '' : 's';
  const edited = allf.length ? (await kit.api('do/edited', { rels: allf.map(f => f.rel) })).rels.length : 0;
  const revert = () => { if (edited) add('Revert to original' + (edited > 1 ? ' (' + edited + ' edited)' : ''), revertSelected); };
  if (!allf.length) {
    if (!g && kit.state.mod) add('Open work folder', () => kit.run('open', { what: 'work' }));
  } else if (!files.length) {   // sections only: nothing that opens or plays all of them at once
    add('Show in folder', reveal);
    items.push('-');
    add('Save to folder... (' + n + ' file' + s + ')', saveTo);
    revert();
    items.push('-');
    add('Remove from work folder (' + n + ' file' + s + ')', removeSelected);
  } else {
    if (files.some(f => ext(f.rel) === '.wav')) add('Play', playSelected);
    const exe = (kit.state.editors || {})[ext(files[0].rel)];
    add('Open in ' + (exe ? exe.split(/[\\/]/).pop().replace(/\.[^.]+$/, '') : 'default program'), () => openSelected());
    add('Open with...', openWith);
    if (files.length === 1) add('Replace with...', replaceSelected);
    add('Show in folder', reveal);
    items.push('-');
    add('Save to folder... (' + n + ' file' + s + ')', saveTo);
    revert();
    items.push('-');
    add('Remove from work folder', removeSelected);
  }
  showMenu(e.clientX, e.clientY, items);
}

function onSelect() {
  paintSel();
  preview();
  for (const f of selectListeners) f(selectedGroup());
  const files = selectedFiles(false);
  if (kit.state && kit.state.autoplay !== false && files.length === 1 && ext(files[0].rel) === '.wav' && files[0].rel !== lastPlayed) play(files[0]);
}
const selectListeners = [];

export function initWork() {
  sel = listSelect($('workTree'), () => rows.map(r => r.id), onSelect);
  const box = $('workTree');
  box.addEventListener('dblclick', e => {
    const d = e.target.closest('.row');
    if (!d || e.target.classList.contains('arrow')) return;
    if (d.dataset.id.startsWith('grp:')) { toggle(d.dataset.id.slice(4)); return; }
    const f = selectedFiles(false)[0];
    if (f && ext(f.rel) === '.wav' && !(kit.state.editors || {})['.wav']) playSelected();
    else openSelected();
  });
  box.addEventListener('contextmenu', contextMenu);
  box.addEventListener('keydown', e => { if (e.key === 'Delete') removeSelected(); });
  document.addEventListener('mousedown', e => { if (!e.target.closest('#menu')) hideMenu(); });
  window.addEventListener('blur', hideMenu);
  $('refreshWork').onclick = () => load();
  $('onlyEdited').onchange = () => load();
  $('pvOpen').onclick = () => openSelected();
  $('pvReveal').onclick = reveal;
  $('pvPlay').onclick = playSelected;
  $('pvStop').onclick = stop;
  $('pvRemove').onclick = removeSelected;
  kit.onState(s => {
    $('workHead').textContent = s.mod ? 'Edit ' + s.mod : 'Edit your mod';
    $('workHint').textContent = '';
    welcome();
    if (s.work !== shownWork) {   // another mod: the selection and the open groups were the other folder's
      shownWork = s.work;
      sel.clear();
      open.clear();
      stop();
      load();
    }
  });
  kit.onTaskDone(() => { const f = kit.workFocus; kit.workFocus = null; load(f); });
  kit.work = {
    reload: load, rows: () => rows.map(r => r.id), selectedGroup, selectedFiles,
    select(ids) { sel.set(ids); onSelect(); }, open(keys) { keys.forEach(k => open.add(k)); render(); },
    panels, menus, onSelect: f => selectListeners.push(f), preview,
  };
}
