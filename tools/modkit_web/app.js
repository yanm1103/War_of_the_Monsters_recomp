// The mod kit's page (tools/modkit_web.py serves it): the paths and the mod on top, the panes, the tasks and the log.
// window.kit holds what the other parts of the page (and the tests driving it) use: api(), state, onState(), pick(), say(),
// dialog(), confirm(), run(), onTaskDone(). The lists are in disc.js and work.js.
import { initDisc } from './disc.js';
import { initWork } from './work.js';
import { initSkins } from './skins.js';
import { initMonsters } from './monsters.js';
import { makeSplit } from './split.js';

const $ = id => document.getElementById(id);
const listeners = [];
const doneListeners = [];
const onceDone = [];
const kit = window.kit = {
  state: null,
  actions: {},   // newSkin(), newMonster(): set by skins.js and monsters.js (the welcome cards call them)
  async api(path, body) {   // GET when there is no body
    const r = await fetch('api/' + path, body === undefined ? {} : { method: 'POST', body: JSON.stringify(body) });
    return r.json();
  },
  onState(f) { listeners.push(f); if (kit.state) f(kit.state); },
  onTaskDone(f) { doneListeners.push(f); },   // after each task (the lists come again)
  nextDone() { return new Promise(r => onceDone.push(r)); },   // resolves when the running task ends
  setState(s) { kit.state = s; for (const f of listeners) f(s); },
  async pick(kind, title, initial, types) {   // the system's dialog -> a path (or a list for "files"), null when cancelled
    return (await kit.api('do/pick', { kind, title, initial, types })).path;
  },
  say(text, kind) { appendLog(text, kind); },
  // a dialog built by the caller: fill(box, close) puts its content in the box; close(value) ends it (Escape: null)
  modal(fill) {
    return new Promise(resolve => {
      const box = $('modalBox');
      box.innerHTML = '';
      box.onsubmit = e => e.preventDefault();
      const close = v => { $('modal').style.display = 'none'; document.removeEventListener('keydown', esc); resolve(v); };
      const esc = e => { if (e.key === 'Escape') close(null); };
      document.addEventListener('keydown', esc);
      $('modal').style.display = 'flex';
      fill(box, close);
    });
  },
  async confirm(title, text, okText) { return (await kit.dialog({ title, text, okText: okText || 'OK' })) !== null; },
  // a dialog in the page: fields [{key, label, value, hint, pick: {kind, title, types}}] (pick: a "..." button with the system's
  // dialog), ok(values) -> an error text to show (stays open) or nothing (closes);
  // resolves with the values, or null on Cancel / Escape
  dialog({ title, text, fields = [], okText = 'OK', ok }) {
    return new Promise(resolve => {
      const box = $('modalBox');
      box.innerHTML = '';
      const h = document.createElement('h3');
      h.textContent = title;
      box.appendChild(h);
      if (text) { const p = document.createElement('p'); p.textContent = text; box.appendChild(p); }
      const inputs = {};
      for (const f of fields) {
        const l = document.createElement('label');
        l.textContent = f.label || '';
        l.style.display = 'block';
        const i = document.createElement('input');
        i.type = 'text';
        i.value = f.value || '';
        i.placeholder = f.hint || '';
        i.spellcheck = false;
        if (f.pick) {
          const row = document.createElement('div');
          row.className = 'field';
          const b = document.createElement('button');
          b.type = 'button';
          b.textContent = '…';
          b.onclick = async () => { const v = await kit.pick(f.pick.kind || 'file', f.pick.title || '', i.value, f.pick.types); if (v) i.value = v; };
          row.append(i, b);
          if (f.clear) {
            const c = document.createElement('button');
            c.type = 'button';
            c.textContent = 'Clear';
            c.onclick = () => { i.value = ''; };
            row.appendChild(c);
          }
          l.appendChild(row);
        } else l.appendChild(i);
        box.appendChild(l);
        inputs[f.key] = i;
      }
      const msg = document.createElement('div');
      msg.className = 'msg';
      box.appendChild(msg);
      const bs = document.createElement('div');
      bs.className = 'buttons';
      const cancel = document.createElement('button');
      cancel.type = 'button';
      cancel.textContent = 'Cancel';
      const okb = document.createElement('button');
      okb.type = 'submit';
      okb.className = 'primary';
      okb.textContent = okText;
      bs.append(cancel, okb);
      box.appendChild(bs);
      const close = v => { $('modal').style.display = 'none'; document.removeEventListener('keydown', esc); resolve(v); };
      const esc = e => { if (e.key === 'Escape') close(null); };
      document.addEventListener('keydown', esc);
      cancel.onclick = () => close(null);
      box.onsubmit = async e => {
        e.preventDefault();
        const vals = Object.fromEntries(Object.entries(inputs).map(([k, i]) => [k, i.value]));
        okb.disabled = true;
        const err = ok ? await ok(vals) : null;
        okb.disabled = false;
        if (err) { msg.textContent = err; return; }
        close(vals);
      };
      $('modal').style.display = 'flex';
      const first = Object.values(inputs)[0] || okb;
      first.focus();
    });
  },
  async run(action, body) {   // a task: its lines come through the log
    const r = await kit.api('do/' + action, body || {});
    if (r.error) appendLog(r.error + '\n', 'err');
    else if (r.ok === false) appendLog('Still working on the previous task; try again when it finishes.\n', 'warn');
    return r;
  },
};

// ---------------------------------------------------------------- the game's pictures (modkit_web_art.py: read from the disc)
let artFor;
function loadArt(disc) {
  if (disc === artFor) return;
  artFor = disc;
  const t = Date.now();
  const logo = $('titleLogo');
  logo.classList.remove('ok');
  logo.onload = () => logo.classList.add('ok');
  logo.onerror = () => logo.classList.remove('ok');
  logo.src = 'api/art?name=title&t=' + t;
  const sky = $('sky');
  const urls = { sky: 'api/art?name=sky&t=' + t, trees: 'api/art?name=trees&t=' + t };
  Promise.all(Object.values(urls).map(u => new Promise(ok => { const i = new Image(); i.onload = () => ok(true); i.onerror = () => ok(false); i.src = u; })))
    .then(([a, b]) => {
      sky.style.setProperty('--sky', 'url("' + urls.sky + '")');
      sky.style.setProperty('--trees', 'url("' + urls.trees + '")');
      sky.classList.toggle('art', a && b);
    });
}

// ---------------------------------------------------------------- setup: the disc and the mods folder
let setupAuto = null;   // what the drawer opened by itself for (it opens once per problem)
function setupOpen(on) { $('setup').classList.toggle('open', on); }
function showSetup(s) {
  const msg = !s.disc_ok ? 'Pick your War of the Monsters disc: the .iso (NTSC-U, SCUS-97197) or the folder it was extracted to.'
            : !s.mods_ok ? 'Pick the mods folder: the "mods" folder next to the game.' : '';
  $('setupMsg').textContent = msg;
  $('setupBtn').classList.toggle('bad', !!msg);
  if (msg && setupAuto !== msg) { setupAuto = msg; setupOpen(true); }
  if (!msg) setupAuto = null;
}
$('setupBtn').onclick = () => setupOpen(!$('setup').classList.contains('open'));
$('setupClose').onclick = () => setupOpen(false);
document.addEventListener('mousedown', e => {
  if ($('setup').classList.contains('open') && !e.target.closest('#setup, #setupBtn, #modal') && !$('setupBtn').classList.contains('bad'))
    setupOpen(false);
});

// ---------------------------------------------------------------- setup: sounds (volume, play on click) and editors
// the volume and "play sounds on click" show in Setup and under a sound's preview: every copy follows the same setting
function showSound(s) {
  const v = Math.round((s.volume === undefined ? 1 : s.volume) * 100);
  for (const r of document.querySelectorAll('input.volume')) if (document.activeElement !== r) r.value = v;
  for (const n of document.querySelectorAll('.volnum')) n.textContent = v + '%';
  for (const c of document.querySelectorAll('input.autoplay')) c.checked = s.autoplay !== false;
  $('player').volume = v / 100;
}
document.addEventListener('input', e => {
  if (!e.target.matches('input.volume')) return;
  const v = parseInt(e.target.value, 10);
  for (const r of document.querySelectorAll('input.volume')) if (r !== e.target) r.value = v;
  for (const n of document.querySelectorAll('.volnum')) n.textContent = v + '%';
  $('player').volume = v / 100;
  if (kit.state) kit.state.volume = v / 100;
});
document.addEventListener('change', async e => {
  if (e.target.matches('input.volume')) kit.api('conf', { volume: parseInt(e.target.value, 10) / 100 });
  if (e.target.matches('input.autoplay')) {
    for (const c of document.querySelectorAll('input.autoplay')) c.checked = e.target.checked;
    if (kit.state) kit.state.autoplay = e.target.checked;
    kit.api('conf', { autoplay: e.target.checked });
  }
});
// the program per file type: textures and sounds always, plus any other type picked once with "Open with..."
const EDITOR_NAMES = { '.png': 'Textures (.png)', '.wav': 'Sounds (.wav)' };
let editorsFor = null;
function showEditors(s) {
  const eds = s.editors || {};
  const key = JSON.stringify(eds);
  if (key === editorsFor) return;
  editorsFor = key;
  const box = $('editors');
  box.innerHTML = '';
  const exts = ['.png', '.wav', ...Object.keys(eds).filter(e => e !== '.png' && e !== '.wav').sort()];
  for (const e of exts) {
    const l = document.createElement('label');
    l.textContent = EDITOR_NAMES[e] || e + ' files';
    const row = document.createElement('div');
    row.className = 'field';
    const i = document.createElement('input');
    i.spellcheck = false;
    i.value = eds[e] || '';
    i.placeholder = e === '.wav' ? 'play in the kit' : "the system's default";
    const save = async v => { kit.setState(Object.assign(kit.state, { editors: (await kit.api('do/editors', { set: { [e]: v } })).editors })); };
    i.onchange = () => save(i.value);
    const pick = document.createElement('button');
    pick.className = 'pick';
    pick.textContent = '…';
    pick.title = 'Pick the program for ' + e + ' files';
    pick.onclick = async () => { const v = await kit.pick('file', 'Program to open ' + e + ' files', i.value, [['Programs', ['*.exe']]]); if (v) save(v); };
    const clear = document.createElement('button');
    clear.className = 'pick';
    clear.textContent = 'Clear';
    clear.title = "Back to the system's default";
    clear.onclick = () => save('');
    row.append(i, pick, clear);
    box.append(l, row);
  }
}

// ---------------------------------------------------------------- paths and mod
function showState(s) {
  showSound(s);
  showEditors(s);
  for (const k of ['disc', 'mods']) if (document.activeElement !== $(k)) $(k).value = s[k] || '';
  $('disc').classList.toggle('bad', !s.disc_ok);
  $('mods').classList.toggle('bad', !s.mods_ok);
  showSetup(s);
  if (s.disc_ok) loadArt(s.disc);
  const sel = $('mod');
  sel.innerHTML = '';
  const none = document.createElement('option');
  none.value = '';
  none.textContent = s.mod_list.length ? 'Pick a mod...' : 'No mods yet';
  sel.appendChild(none);
  for (const n of s.mod_list) {
    const o = document.createElement('option');
    o.value = o.textContent = n;
    sel.appendChild(o);
  }
  sel.value = s.mod || '';
  for (const b of document.querySelectorAll('.needmod')) b.classList.toggle('nomod', !s.mod);
  busy(s.busy);
}
kit.onState(showState);
async function setConf(d) { kit.setState(await kit.api('conf', d)); }
for (const k of ['disc', 'mods']) $(k).addEventListener('change', () => setConf({ [k]: $(k).value.trim() }));
$('mod').addEventListener('change', () => setConf({ mod: $('mod').value }));
for (const b of document.querySelectorAll('button[data-pick]')) {
  b.onclick = async () => {
    const k = b.dataset.pick;
    const p = k === 'disc'
      ? await kit.pick('file', 'War of the Monsters ISO (NTSC-U, SCUS-97197)', kit.state.disc, [['Disc image', ['*.iso']]])
      : await kit.pick('dir', 'Mods folder', kit.state.mods);
    if (p) setConf({ [k]: p });
  };
}

// ---------------------------------------------------------------- tasks and log
function busy(on) {   // tasks wait for the one running; the buttons of a mod wait for a mod
  for (const b of document.querySelectorAll('button.task, button.needmod'))
    b.disabled = (b.classList.contains('task') && !!on) || b.classList.contains('nomod');
}
const log = $('log');
function appendLog(text, kind) {
  if (!text) return;
  const stick = log.scrollTop + log.clientHeight >= log.scrollHeight - 4;
  const sp = document.createElement('span');
  if (kind && kind !== 'out') sp.className = kind;
  sp.textContent = text;
  log.appendChild(sp);
  while (log.childNodes.length > 4000) log.removeChild(log.firstChild);
  if (stick) log.scrollTop = log.scrollHeight;
}
const events = new EventSource('api/log');   // also what tells the server this tab is open
// a page of a task (the bones page of Import rigged model / Adjust bones) shows over the kit until the task ends; closing it
// cancels the import (the page sends "cancel" as it goes away)
function showTaskPage(url) {
  let o = $('taskPage');
  if (!o) {
    o = document.createElement('div');
    o.id = 'taskPage';
    const bar = document.createElement('div');
    bar.className = 'bar';
    const t = document.createElement('span');
    t.textContent = 'Set up the model, then OK in the page.';
    const x = document.createElement('button');
    x.textContent = 'Close (cancels the import)';
    x.onclick = () => hideTaskPage();
    const g = document.createElement('span');
    g.className = 'grow';
    bar.append(t, g, x);
    const f = document.createElement('iframe');
    o.append(bar, f);
    document.body.appendChild(o);
  }
  o.querySelector('iframe').src = url;
  o.style.display = 'flex';
}
function hideTaskPage() {
  const o = $('taskPage');
  if (!o) return;
  o.querySelector('iframe').src = 'about:blank';
  o.style.display = 'none';
}
events.onmessage = ev => {
  const it = JSON.parse(ev.data);
  if (it.kind === 'page') { showTaskPage(it.text); return; }
  if (it.kind === 'head' || it.kind === 'done') hideTaskPage();
  appendLog(it.text, it.kind);
  if (kit.state && (it.kind === 'head' || it.kind === 'done')) {
    kit.state.busy = it.busy;
    busy(it.busy);
    if (it.kind === 'done') { refresh(); for (const f of doneListeners) f(); onceDone.splice(0).forEach(f => f()); }
  }
};
events.onerror = async () => {   // the server went away (Quit, or closed): say so instead of a dead page
  try { await fetch('api/hello'); } catch (e) { $('gone').style.display = 'flex'; events.close(); }
};
$('check').onclick = () => kit.run('check');
$('build').onclick = () => kit.run('build');
$('openMods').onclick = () => kit.run('open', { what: 'mods' });
$('openWork').onclick = () => kit.run('open', { what: 'work' });
kit.newMod = () => kit.dialog({
  title: 'New mod',
  text: "A mod is a folder the game loads over the disc: skins, monsters, textures and sounds. Its name takes letters, digits, '_' and '-' (no spaces; e.g. green_togera or Robo47-skins).",
  fields: [{ key: 'name', label: 'Name of the new mod' }],
  okText: 'Create',
  ok: async v => {
    const r = await kit.api('do/new_mod', { name: v.name.trim() });
    if (r.error) return r.error;
    kit.setState(r.state);
    return null;
  },
});
$('newMod').onclick = () => kit.newMod();
$('quit').onclick = async () => {
  try { await kit.api('quit', {}); } catch (e) { /* already gone */ }
  events.close();
  $('gone').style.display = 'flex';
};

// the log's height: dragging the line above it (kept in this browser)
const foot = $('foot');
const savedH = parseInt(localStorage.getItem('wotm.logH') || '', 10);
if (savedH) document.documentElement.style.setProperty('--log-h', savedH + 'px');
$('split').addEventListener('pointerdown', ev => {
  const y0 = ev.clientY, h0 = foot.offsetHeight;
  const move = e => {
    const h = Math.max(60, Math.min(window.innerHeight - 260, h0 - (e.clientY - y0)));
    document.documentElement.style.setProperty('--log-h', h + 'px');
  };
  const up = () => {
    window.removeEventListener('pointermove', move);
    window.removeEventListener('pointerup', up);
    localStorage.setItem('wotm.logH', foot.offsetHeight);
  };
  window.addEventListener('pointermove', move);
  window.addEventListener('pointerup', up);
});

// the column dividers: the disc list's width, and the work list's width next to the preview
makeSplit($('splitMain'), $('discPane'), { key: 'wotm.discW', min: 220, max: () => $('main').clientWidth - 28 - 10 - 420 });
makeSplit($('splitWork'), $('workTree'), { key: 'wotm.workTreeW', min: 160,
                                            max: () => $('workTree').parentElement.clientWidth - 7 - 240 });

async function refresh() { kit.setState(await kit.api('state')); }
initDisc();
initWork();
initSkins();
initMonsters();
refresh();
