// The Test options: who plays Test in game (your monster and costume, 1 to 4 players, up to 8 monsters with the CPUs, and theirs), the mode
// (free for all or elimination, with 2 players or more), training mode and what the CPUs do in it, and "Test from here" (the game starts you
// where the view aims, facing where it looks, with the others around you if asked). The choices persist in the browser. The server turns them
// into the game's WOTM_MATCH, WOTM_PLAYERS, WOTM_MODE, WOTM_TRAINING, WOTM_TRAINING_DUMMY and WOTM_SPAWN (editor.py test_env). testBody(here) is what post() sends;
// openTestOpts(ctx) opens the dialog. ctx, from app.js: api(url), aim() -> [x, y, z, heading] (the view's aim point and the game heading it
// looks along), run(here) (starts the test; here: true for Test from here), changed() (the dialog closed: the summary may differ).
import { el, icon } from './hud.js';

const KEY = 'wotmLevelEditor.test';
const DUMMY = ['Stand still', 'The game AI', 'Always block', 'Block after the first hit', 'Jump'];
const MAX_MONSTERS = 8;   // players and CPUs together
const MODES = [['ffa', 'Free for all'], ['elimination', 'Elimination']];   // the minigames play in their own arenas, not on the stage
const DEFAULT = () => ({ monster: { monster: '3', skin: '' }, players: [], cpus: [], training: true, dummy: 0, mode: 'ffa', continues: 2, ring: true });
const maxCpus = () => MAX_MONSTERS - 1 - st.players.length;

function load() {
  try {
    const j = JSON.parse(localStorage.getItem(KEY) || 'null');
    if (j && j.monster && Array.isArray(j.cpus)) {
      const o = Object.assign(DEFAULT(), j);
      o.players = (Array.isArray(o.players) ? o.players : []).slice(0, 3);
      o.cpus = o.cpus.slice(0, MAX_MONSTERS - 1 - o.players.length);
      if (!MODES.some((m) => m[0] === o.mode)) o.mode = 'ffa';
      return o;
    }
  } catch (e) { /* nothing stored */ }
  return DEFAULT();
}
let st = load();
const save = () => { try { localStorage.setItem(KEY, JSON.stringify(st)); } catch (e) { /* no storage */ } };
const elimination = () => st.players.length > 0 && st.mode === 'elimination';

// what Test in game and Reload in game send besides the stage: {training, options}; here: [x, y, z, heading] for Test from here (a ring
// radius is added when the others should start around you)
export function testBody(here) {
  const options = { monster: st.monster, players: st.players, cpus: st.cpus, dummy: st.training ? st.dummy : undefined };
  if (st.players.length) {
    options.mode = st.mode;
    if (elimination()) options.continues = st.continues;
  }
  const others = st.players.length + st.cpus.length;
  if (here) options.here = st.ring && others ? [...here, 300 + 40 * others] : here;
  return { training: st.training, options };
}
// a one-line summary of the choices (the Test button's tooltip)
export function testSummary() {
  const name = (s) => {
    const m = (opts.monsters || []).find((x) => x.id === s.monster) || (opts.customs || []).find((x) => x.id === s.monster);
    return m ? m.name : `monster ${s.monster}`;
  };
  const you = name(st.monster);
  const p = st.players.length + 1, n = st.cpus.length;
  const who = p > 1 ? `${p} players` : you;
  return `${who}${n ? ` and ${n} ${n === 1 ? 'CPU' : 'CPUs'}` : p > 1 ? '' : ' alone'}${elimination() ? ', elimination' : ''}${st.training ? ', training mode' : ''}`;
}

let dlg = null, ctx = null, opts = { monsters: [], customs: [], skins: [] };

// the monsters one can choose: the disc's and the mods' customs (for a CPU only the ones a CPU can be)
function monsterChoices(cpu) {
  const out = opts.monsters.map((m) => ({ id: m.id, label: m.name }));
  for (const c of opts.customs) if (!cpu || c.ai) out.push({ id: c.id, label: `${c.name} (mod ${c.mod})` });
  return out;
}
// the looks of a monster: its costumes (the disc's four) and the mods' skins of it
function skinChoices(mon, cpu) {
  const out = [{ id: '', label: /^\d+$/.test(mon) ? 'Costume 0' : 'Its own look' }];
  if (/^\d+$/.test(mon)) for (let k = 1; k <= 3; k++) out.push({ id: String(k), label: `Costume ${k}` });
  for (const s of opts.skins) if (s.monster === mon && (!cpu || s.ai)) out.push({ id: s.id, label: `${s.name} (mod ${s.mod})` });
  return out;
}

function picker(sel, cpu, label) {
  const m = el('select', { 'aria-label': `${label}: monster` }), s = el('select', { 'aria-label': `${label}: costume` });
  const fillSkins = () => {
    s.textContent = '';
    for (const c of skinChoices(sel.monster, cpu)) s.append(el('option', { value: c.id }, c.label));
    if (![...s.options].some((o) => o.value === sel.skin)) sel.skin = '';
    s.value = sel.skin;
  };
  for (const c of monsterChoices(cpu)) m.append(el('option', { value: c.id }, c.label));
  if (![...m.options].some((o) => o.value === sel.monster)) sel.monster = '3';    // a mod that went away
  m.value = sel.monster;
  fillSkins();
  m.onchange = () => { sel.monster = m.value; sel.skin = ''; fillSkins(); save(); };
  s.onchange = () => { sel.skin = s.value; save(); };
  return [m, s];
}

function rows() {
  const pbox = dlg.querySelector('.to-players');
  pbox.textContent = '';
  st.players.forEach((c, k) => {
    const [m, s] = picker(c, false, `Player ${k + 2}`);
    pbox.append(el('div', { class: 'to-row' }, el('span', { class: 'to-lbl' }, `Player ${k + 2}`), m, s));
  });
  const box = dlg.querySelector('.to-cpus');
  box.textContent = '';
  st.cpus.forEach((c, k) => {
    const [m, s] = picker(c, true, `CPU ${k + 1}`);
    box.append(el('div', { class: 'to-row' }, el('span', { class: 'to-lbl' }, `CPU ${k + 1}`), m, s));
  });
  dlg.querySelector('.to-dummy').hidden = !(st.training && st.cpus.length);
  dlg.querySelector('.to-mode').hidden = dlg.querySelector('.to-modehelp').hidden = !st.players.length;
  dlg.querySelector('.to-cont').hidden = !elimination();
  dlg.querySelector('.to-ring').hidden = !(st.players.length + st.cpus.length);
  const cpuCount = dlg.querySelector('#toCount');
  cpuCount.max = maxCpus();
  dlg.querySelector('.to-cpumax').textContent = `0 to ${maxCpus()}`;
}

function aimText() {
  const a = ctx.aim();
  if (!a) return 'To test from here, aim the view at the ground or a roof: you start where its middle meets the stage.';
  return `From here starts you at (${a.slice(0, 3).map((x) => Math.round(x)).join(', ')}) facing ${Math.round(a[3])}° (0 is +Y, 90 is +X), where the view aims.`;
}

function build() {
  dlg = el('dialog', { id: 'testDlg', class: 'dlg', 'aria-labelledby': 'testTitle' });
  dlg.append(el('form', { method: 'dialog' },
    el('div', { class: 'dlg-head' }, el('h2', { id: 'testTitle' }, 'Test options'),
      el('button', { type: 'button', 'data-close': true, class: 'icon ghost', title: 'Close (Esc)', 'aria-label': 'Close' }, icon('close'))),
    el('div', { class: 'dlg-body' },
      el('div', { class: 'sec-title' }, 'You'),
      el('div', { class: 'to-row to-you' }),
      el('div', { class: 'sec-title' }, 'Players'),
      el('div', { class: 'to-row' }, el('label', { for: 'toPlayers', class: 'to-lbl' }, 'How many'),
        el('input', { type: 'number', id: 'toPlayers', min: 1, max: 4, step: 1 }), el('span', { class: 'muted' }, '1 to 4, you included')),
      el('div', { class: 'to-players' }),
      el('div', { class: 'to-row to-mode' }, el('label', { for: 'toMode', class: 'to-lbl' }, 'Mode'), el('select', { id: 'toMode' })),
      el('div', { class: 'to-row to-cont' }, el('label', { for: 'toCont', class: 'to-lbl' }, 'Continues'),
        el('input', { type: 'number', id: 'toCont', min: 0, max: 9, step: 1 }), el('span', { class: 'muted' }, '0 to 9 each')),
      el('div', { class: 'help to-modehelp' }, 'Each player needs a controller (the first one also has the keyboard). The minigames play in their own arenas, so they are not here.'),
      el('div', { class: 'sec-title' }, 'CPUs'),
      el('div', { class: 'to-row' }, el('label', { for: 'toCount', class: 'to-lbl' }, 'How many'),
        el('input', { type: 'number', id: 'toCount', min: 0, max: MAX_MONSTERS - 1, step: 1 }), el('span', { class: 'muted to-cpumax' })),
      el('div', { class: 'to-cpus' }),
      el('div', { class: 'sec-title' }, 'Training mode'),
      el('label', { class: 'switch', title: 'Strike spheres, body boxes and a damage panel; the CPUs stand idle and health refills' },
        el('input', { type: 'checkbox', id: 'toTrain' }), el('span', { class: 'track' }), el('span', { class: 'tx' }, 'Training mode')),
      el('div', { class: 'to-row to-dummy' }, el('label', { for: 'toDummy', class: 'to-lbl' }, 'The CPUs'),
        el('select', { id: 'toDummy' })),
      el('div', { class: 'help to-here' }),
      el('label', { class: 'to-ring', title: 'With Test from here: the other players and the CPUs start on a ring around you, facing you, instead of on the stage\'s start points' },
        el('input', { type: 'checkbox', id: 'toRing' }), 'Test from here: put the others around me')),
    el('div', { class: 'dlg-foot' },
      el('button', { type: 'button', 'data-close': true }, 'Close'),
      el('button', { type: 'button', id: 'toHere', title: "Starts you where the view aims instead of on the stage's start points" }, 'Test from here'),
      el('button', { type: 'button', id: 'toGo', class: 'primary' }, 'Test in game'))));
  document.body.append(dlg);
  dlg.querySelectorAll('[data-close]').forEach((b) => (b.onclick = () => dlg.close()));
  const dummy = dlg.querySelector('#toDummy');
  DUMMY.forEach((t, k) => dummy.append(el('option', { value: k }, t)));
  dummy.onchange = () => { st.dummy = parseInt(dummy.value, 10) || 0; save(); };
  dlg.querySelector('#toTrain').onchange = (e) => { st.training = e.target.checked; save(); rows(); };
  const mode = dlg.querySelector('#toMode');
  MODES.forEach(([id, t]) => mode.append(el('option', { value: id }, t)));
  mode.onchange = () => { st.mode = mode.value; save(); rows(); };
  dlg.querySelector('#toCont').onchange = (e) => {
    st.continues = Math.max(0, Math.min(9, parseInt(e.target.value, 10) || 0));
    e.target.value = st.continues;
    save();
  };
  dlg.querySelector('#toRing').onchange = (e) => { st.ring = e.target.checked; save(); };
  dlg.querySelector('#toPlayers').onchange = (e) => {
    const n = Math.max(1, Math.min(4, parseInt(e.target.value, 10) || 1)) - 1;
    e.target.value = n + 1;
    while (st.players.length < n) st.players.push({ monster: st.players.length ? st.players[st.players.length - 1].monster : st.monster.monster, skin: '' });
    st.players.length = n;
    st.cpus.length = Math.min(st.cpus.length, maxCpus());      // the game holds 8 monsters
    dlg.querySelector('#toCount').value = st.cpus.length;
    save();
    rows();
  };
  dlg.querySelector('#toCount').onchange = (e) => {
    const n = Math.max(0, Math.min(maxCpus(), parseInt(e.target.value, 10) || 0));
    e.target.value = n;
    while (st.cpus.length < n) st.cpus.push({ monster: st.cpus.length ? st.cpus[st.cpus.length - 1].monster : '1', skin: '' });
    st.cpus.length = n;
    save();
    rows();
  };
  dlg.addEventListener('close', () => ctx.changed && ctx.changed());
  dlg.querySelector('#toGo').onclick = () => { dlg.close(); ctx.run(false); };
  dlg.querySelector('#toHere').onclick = () => { dlg.close(); ctx.run(true); };
}

export async function loadTestOpts(api) {
  try { opts = await api('/api/testopts'); } catch (e) { /* the lists stay as they were */ }
  return opts;
}

export async function openTestOpts(c) {
  ctx = c;
  if (!dlg) build();
  await loadTestOpts(c.api);
  const you = dlg.querySelector('.to-you');
  you.textContent = '';
  you.append(el('span', { class: 'to-lbl' }, 'Your monster'), ...picker(st.monster, false, 'Your monster'));
  dlg.querySelector('#toPlayers').value = st.players.length + 1;
  dlg.querySelector('#toMode').value = st.mode;
  dlg.querySelector('#toCont').value = st.continues;
  dlg.querySelector('#toRing').checked = st.ring;
  dlg.querySelector('#toCount').value = st.cpus.length;
  dlg.querySelector('#toTrain').checked = st.training;
  dlg.querySelector('#toDummy').value = st.dummy;
  dlg.querySelector('.to-here').textContent = aimText();
  rows();
  dlg.showModal();
}
