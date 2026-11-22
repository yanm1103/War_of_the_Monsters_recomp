// The mod's work folder (right pane): one group per disc file, skin or custom monster, each file with its thumbnail, size and
// state ("edited" = changed since the extract: it goes in the next build); the preview of the picked file (texture on a
// checkerboard, sound as a waveform) and what can be done with the files (open, replace, save, revert, remove).
// kit.work.panels: other parts of the page register a panel for a group kind (a skin's or a monster's properties).
import { listSelect } from './select.js';

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

function render() {
  const box = $('workTree');
  const top = box.scrollTop;
  box.innerHTML = '';
  rows = [];
  for (const g of data.groups) {
    const gid = 'grp:' + g.key;
    rows.push({ id: gid, group: g });
    const ne = g.files.filter(f => f.state === 'edited').length;
    const d = document.createElement('div');
    d.className = 'row group' + (sel.has(gid) ? ' sel' : '') + (ne ? ' edited' : '');
    d.dataset.id = gid;
    const a = document.createElement('span');
    a.className = 'arrow';
    a.textContent = g.files.length ? (open.has(g.key) ? '▼' : '▶') : '';
    a.onclick = e => { e.stopPropagation(); toggle(g.key); };
    const l = document.createElement('span');
    l.className = 'lbl';
    l.textContent = g.label + '   [' + g.files.length + ' file(s)' + (ne ? ', ' + ne + ' edited' : '') + ']';
    d.append(a, l);
    box.appendChild(d);
    if (!open.has(g.key)) continue;
    for (const f of g.files) {
      rows.push({ id: f.rel, group: g, file: f });
      const r = document.createElement('div');
      r.className = 'row' + (sel.has(f.rel) ? ' sel' : '') + (f.state === 'edited' ? ' edited' : '');
      r.dataset.id = f.rel;
      r.style.paddingLeft = '28px';
      let th;
      if (ext(f.rel) === '.png') {
        th = document.createElement('img');
        th.loading = 'lazy';
        th.src = fileUrl(f);
      } else th = document.createElement('span');
      th.className = 'thumb' + (ext(f.rel) === '.png' ? '' : ' none');
      const n = document.createElement('span');
      n.className = 'lbl';
      n.textContent = f.name;
      const i = document.createElement('span');
      i.className = 'info';
      i.textContent = f.info;
      const st = document.createElement('span');
      st.className = 'st';
      st.textContent = f.state;
      r.append(th, n, i, st);
      box.appendChild(r);
    }
  }
  if (!data.groups.length) {
    const m = document.createElement('div');
    m.className = 'hint';
    m.style.padding = '10px 12px';
    m.textContent = kit.state && kit.state.mod ? '(' + (data.status || 'empty') + ': extract something from the disc on the left)' : '';
    box.appendChild(m);
  }
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
async function waveform(url, box) {
  const c = document.createElement('canvas');
  box.appendChild(c);
  const w = c.width = Math.max(50, box.clientWidth), h = c.height = Math.max(50, box.clientHeight);
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
  if (g && panels[g.kind]) { panels[g.kind](g, body, label); return; }
  const files = selectedFiles(false), allf = selectedFiles(true);
  if (!files.length && allf.length) {
    label.textContent = allf.length + ' file(s) in the selected section. Open it (arrow or double-click) and pick a file; right-click saves or removes the whole section.';
    return;
  }
  if (!files.length) {
    label.textContent = kit.state && kit.state.mod ? 'Select a file in the work folder. Double-click opens a texture in its editor ("Editors...") and plays a sound; right-click for more (Save to folder...).' : '';
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
async function editorsDialog() {
  const eds = kit.state.editors || {};
  const exts = ['.png', '.wav', ...Object.keys(eds).filter(e => e !== '.png' && e !== '.wav').sort()];
  const names = { '.png': 'Textures (.png)', '.wav': 'Sounds (.wav)' };
  const v = await kit.dialog({
    title: 'Editors',
    text: 'Program used by "Open in editor" and double-click, per file type. Empty = the system\'s default (for sounds: play here in the kit).',
    fields: exts.map(e => ({ key: e, label: names[e] || e, value: eds[e] || '', pick: { kind: 'file', title: 'Program to open ' + e + ' files', types: [['Programs', ['*.exe']]] }, clear: true })),
  });
  if (!v) return;
  await kit.api('do/editors', { set: v });
  kit.setState(await kit.api('state'));
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
  if ($('autoplay').checked && files.length === 1 && ext(files[0].rel) === '.wav' && files[0].rel !== lastPlayed) play(files[0]);
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
  $('editorsBtn').onclick = editorsDialog;
  $('autoplay').onchange = () => kit.api('conf', { autoplay: $('autoplay').checked });
  kit.onState(s => {
    $('autoplay').checked = s.autoplay !== false;
    $('workHead').textContent = s.mod ? '2. Edit ' + s.mod + "'s work folder" : '2. Edit the work folder';
    $('workHint').textContent = s.mod ? '' : 'Pick or create a mod first (top)';
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
