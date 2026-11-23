// The Moves tab of a monster's panel: its move table, the numbers of the game's training mode (damage, hit window, radius, stun,
// knockback, which bones hit) read from its .CHR, with the length of each move in frames. A row plays the move's animation in the
// 3D view above and marks where it hits; with the monster's own .CHR, the picked move's numbers can be changed right there.
// Server side: tools/chr_view.py (monster_moves, monster_move_set).
import { el, button } from './panel.js';
import { playMove, setMoveHit, animName, hitSpheresOn, setHitSpheres } from './viewer.js';

const BUTTONS = { LIGHT: 'LIGHT', HEAVY: 'HEAVY' };
const DIRS = { 8: 'FORWARD + ', 2: 'BACK + ', 5: '' };
const moveName = r => (DIRS[r.dir] || '') + BUTTONS[r.button];

const COLS = {
  pos: ['Pos', 'The step of the combo: 1 is the first move; the next step comes if the combo input is accepted (the COMBOS list says which buttons chain).'],
  move: ['Move', 'The button (and direction) that starts it. LIGHT is Square, HEAVY is Triangle.'],
  dmg: ['Damage', 'The damage the move requests. With an object it is a multiplier of the object\'s own damage.'],
  win: ['Hit window', 'Part of the animation, in %, where the game tests the hit.'],
  sat: ['Frames (est.)', 'Estimated from the animation (60 frames per second, at the move\'s speed): startup (to the first hit test) · active (frames with a ' +
                  'hit test) · total length. The training mode measures these in a fight and gets other numbers: the hit pause, the blend back to idle, ' +
                  'being hit or blocked and the AI\'s own timing come on top.'],
  radius: ['Radius', 'Radius of the sphere swept along the bone each frame; a hit needs it to touch a body.'],
  stun: ['Stun', 'Above 100 the victim is stunned (cannot act for a moment).'],
  kb: ['Knockback', 'Out / up. Anything other than 0 knocks the victim down.'],
  loc: ['Hits with', 'The bones that test the hit (the fist, the foot, the tail tip...).'],
  anim: ['Animation', 'The animation id the move plays (its name from the animation list).'],
};

// the numbers of one move that can be changed (field = tools/chr_view.py EDIT), with what each one is
const FIELDS = [
  ['damage', 'Damage', 'The damage the move requests.', 'punch'],
  ['mult', 'Damage ×', 'The multiplier of the held object\'s own damage.', 'object'],
  ['win_start', 'Hit from', 'Where in the animation (0 to 1) the hit test starts.'],
  ['win_end', 'Hit until', 'Where in the animation (0 to 1) the hit test ends.'],
  ['radius', 'Radius', 'Radius of the hit sphere; 0 uses the monster\'s default fist (punches only).'],
  ['stun', 'Stun', 'Above 100 the victim is stunned.'],
  ['kb_out', 'Knockback out', 'How far the victim is thrown away from the attacker; anything other than 0 knocks down.'],
  ['kb_up', 'Knockback up', 'How high the victim is thrown.'],
  ['pause', 'Hit pause (ms)', 'How long the game freezes the two monsters on a hit.'],
  ['speed', 'Speed', 'The animation\'s speed: 1 is its own pace, 2 twice as fast.'],
  ['stamina', 'Stamina cost', 'What the move takes from the stamina meter.'],
  ['anim', 'Animation id', 'Which animation of the model the move plays (1 to 299).'],
];
// after a move, how long (ms) before a move of each type may follow it in the combo; 0 means that type cannot follow
const NEXT = [[5, 'LIGHT'], [3, 'FORWARD + LIGHT'], [4, 'BACK + LIGHT'], [0, 'HEAVY'], [1, 'FORWARD + HEAVY'], [2, 'BACK + HEAVY']];

const fix = (v, n = 0) => (Number.isFinite(v) ? v.toFixed(n).replace(/\.0+$/, '') : '-');

function table(rows, weapon, names, onPick, showPos) {
  const t = el('table', 'moves');
  const head = el('tr');
  const keys = ['pos', 'move', 'dmg', 'win', 'sat', 'radius', 'stun', 'kb', 'loc', 'anim'].filter(k => showPos || k !== 'pos');
  const left = k => (k === 'move' || k === 'loc' || k === 'anim' ? 'l' : '');
  for (const k of keys) { const th = el('th', left(k), COLS[k][0]); th.title = COLS[k][1]; head.appendChild(th); }
  t.appendChild(el('thead')).appendChild(head);
  const body = el('tbody');
  t.appendChild(body);
  for (const r of rows) {
    const tm = r.timing;
    const tr = el('tr', r.pos === 1 ? 'first' : '');
    tr.dataset.id = (r.weapon ? 'w' : 'p') + r.pos + '.' + r.ptype;
    const cells = {
      pos: r.pos, move: moveName(r),
      dmg: weapon ? '×' + fix(r.mult, 2) : fix(r.damage, 1),
      win: fix(r.win_start * 100) + '–' + fix(r.win_end * 100) + '%',
      sat: tm ? tm.start + ' · ' + tm.active + ' · ' + tm.total : '-',
      radius: fix(r.radius, 1) + (r.radius_default ? '*' : ''), stun: fix(r.stun),
      kb: (r.kb_out || r.kb_up) ? fix(r.kb_out) + ' / ' + fix(r.kb_up) : '-', loc: r.bones,
      anim: r.anim + (names[r.anim] ? '  ' + names[r.anim] : ''),
    };
    for (const k of keys) tr.appendChild(el('td', left(k), String(cells[k])));
    tr.onclick = () => {
      for (const o of body.children) o.classList.remove('on');
      tr.classList.add('on');
      onPick(r);
    };
    body.appendChild(tr);
  }
  return t;
}

// a group of numbers of one state (a special, the butt slam...): each one a field when the monster has its own .CHR, else plain text.
// The shockwave rings (fields with a "ring") are a table of their own. save(field, input) writes one number.
function fieldTable(s, own, save, open) {
  const d = el('details', 'special');
  d.open = !!open;
  d.appendChild(el('summary', '', s.name + (s.state ? '  (' + s.state + ')' : '')));
  if (s.hint) d.appendChild(el('div', 'dim', s.hint));
  const cell = (f, td) => {
    if (!own) { td.textContent = String(f.value); return; }
    const inp = el('input'); inp.type = 'text'; inp.value = String(f.value);
    inp.onchange = () => save(f, inp);
    td.appendChild(inp);
  };
  const plain = s.fields.filter(f => !('ring' in f)), rings = s.fields.filter(f => 'ring' in f);
  if (plain.length) {
    const t = el('table', 'moves fields');
    const b = el('tbody');
    for (const f of plain) {
      const tr = el('tr');
      tr.appendChild(el('td', 'l', f.label));
      const td = el('td', 'l');
      cell(f, td);
      tr.appendChild(td);
      b.appendChild(tr);
    }
    t.appendChild(b);
    d.appendChild(t);
  }
  if (rings.length) {
    const cols = [...new Set(rings.map(f => f.col))];
    const t = el('table', 'moves fields rings');
    const h = el('tr');
    h.appendChild(el('th', 'l', 'Shockwave ring'));
    for (const c of cols) h.appendChild(el('th', '', c));
    t.appendChild(el('thead')).appendChild(h);
    const b = el('tbody');
    for (const i of [...new Set(rings.map(f => f.ring))]) {
      const tr = el('tr');
      tr.appendChild(el('td', 'l', String(i + 1)));
      for (const c of cols) {
        const f = rings.find(x => x.ring === i && x.col === c);
        const td = el('td');
        if (f) cell(f, td);
        tr.appendChild(td);
      }
      b.appendChild(tr);
    }
    t.appendChild(b);
    d.appendChild(t);
  }
  return d;
}

// page: the tab's element; args: {cid} (a custom monster) or {group} (a costume of a game monster);
// hooks.extract(): gives the monster its own .CHR (Extract stats) and resolves when it is done
export async function movesPage(page, args, hooks = {}) {
  const state = { view: 'punch', all: false, pick: null, note: null };   // what the page shows, kept over a reload
  async function load() {
    page.innerHTML = '';
    page.appendChild(el('div', 'dim', 'Reading the move table…'));
    const m = await kit.api('do/monster_moves', args);
    page.innerHTML = '';
    if (m.error) { page.appendChild(el('div', 'dim', 'No move table: ' + m.error + '.')); return; }
    if (m.note) { page.appendChild(el('div', 'dim', m.note)); return; }
    await draw(m);
  }

  async function draw(m) {
    const names = {};
    const prefix = (m.file || '').replace(/\.CHR$/i, '').toUpperCase();
    for (const r of [...m.punches, ...m.swipes]) if (!(r.anim in names)) names[r.anim] = await animName(r.anim, prefix);

    const intro = el('div', 'dim', 'From ' + m.label + (m.own ? ' (this monster\'s own .CHR: the numbers below can be changed)' : ' (the base type\'s .CHR)') +
      '. The numbers are the game\'s; the frames are an estimate' + (m.frames ? '' : ' (no model: no frames)') + ' at 60 per second. A row plays that move above.');
    page.appendChild(intro);
    if (!m.own && hooks.extract) {
      const b = button('Give it its own .CHR to edit', async () => { b.disabled = true; await hooks.extract(); load(); });
      b.title = 'Extract stats: copies the base type\'s .CHR and .AIC to this monster\'s files (the same as Stats & sounds)';
      page.appendChild(b);
    }
    if (m.problems && m.problems.length) {
      const w = el('div', 'warnbox');
      w.appendChild(el('div', '', 'Check these moves of the .CHR:'));
      const ul = el('ul');
      for (const p of m.problems.slice(0, 12)) ul.appendChild(el('li', '', p));
      if (m.problems.length > 12) ul.appendChild(el('li', '', '… and ' + (m.problems.length - 12) + ' more'));
      w.appendChild(ul);
      page.appendChild(w);
    }

    const view = el('select');
    for (const [v, t] of [['punch', 'Punches'], ['weapon', 'With an object'], ['other', 'Other moves'], ['special', 'Specials']]) { const o = el('option', '', t); o.value = v; view.appendChild(o); }
    view.value = state.view;
    const all = el('input'); all.type = 'checkbox'; all.checked = state.all;
    const allL = el('label', 'check'); allL.append(all, document.createTextNode(' every combo step'));
    // the hit sphere of the picked move on its bones in the 3D view (red inside the hit window, dim outside); the choice is kept
    const sphOn = el('input'); sphOn.type = 'checkbox'; sphOn.checked = hitSpheresOn();
    const sphL = el('label', 'check'); sphL.append(sphOn, document.createTextNode(' hit spheres'));
    sphL.title = 'Draws the hit sphere of the picked move at each bone that tests the hit, on the playing animation: red while the hit window is open, dim outside it.';
    sphOn.onchange = () => setHitSpheres(sphOn.checked);
    const bar = el('div', 'inline');
    bar.append(view, allL, sphL);
    const out = el('div', 'movesout');
    const edit = el('div', 'moveedit');
    page.append(bar, out, edit);

    const rowsAll = () => [...m.punches, ...m.swipes];
    const hitOf = r => [[r.win_start, r.win_end], { locators: r.locators, radius: r.radius }];
    const pick = r => { state.pick = (r.weapon ? 'w' : 'p') + r.pos + '.' + r.ptype; playMove(r.anim, ...hitOf(r)); drawEdit(r); };
    // writes a number and draws the page again from the file (the table, the frames, the warnings follow it)
    async function reread(note) {
      state.note = note;
      page.innerHTML = '';
      const m2 = await kit.api('do/monster_moves', args);
      if (m2.error) { page.appendChild(el('div', 'dim', 'No move table: ' + m2.error + '.')); return; }
      await draw(m2);
    }
    async function saveMove(r, f, label, inp, status) {
      status.className = 'autosave busy'; status.textContent = 'Saving…';
      const res = await kit.api('do/monster_move_set', { ...args, weapon: !!r.weapon, pos: r.pos, ptype: r.ptype, field: f, value: inp.value });
      if (res.error) { status.className = 'autosave err'; status.textContent = res.error.split(f).join(label); inp.value = fix(r[f], 3); return; }
      reread('✓ Saved: ' + label + ' ' + fix(res.old, 3) + ' → ' + fix(res.new, 3) + ' (Build mod puts it in the game)');
    }
    async function saveKey(f, inp) {
      const res = await kit.api('do/monster_key_set', { ...args, key: f.key, off: f.off, value: inp.value });
      if (res.error) { inp.value = String(f.value); keyNote(res.error, true); return; }
      reread('✓ Saved: ' + f.label + ' ' + fix(res.old, 3) + ' → ' + fix(res.new, 3) + ' (Build mod puts it in the game)');
    }
    const keyNote = (text, bad) => {
      let n = out.querySelector('.keynote');
      if (!n) { n = el('div', 'autosave keynote'); out.prepend(n); }
      n.className = 'autosave keynote ' + (bad ? 'err' : 'ok');
      n.textContent = text;
    };
    function drawEdit(r) {
      edit.innerHTML = '';
      if (!r) return;
      edit.appendChild(el('h5', '', 'Move: ' + (r.weapon ? 'with an object, ' : '') + moveName(r) + ', step ' + r.pos));
      if (!m.own) { edit.appendChild(el('div', 'dim', 'The disc\'s numbers are not edited. ' + (hooks.extract ? '[Give it its own .CHR to edit] above makes a copy for this monster.' : ''))); return; }
      const grid = el('div', 'editgrid');
      const status = el('div', 'autosave', 'Changes save by themselves.');
      if (state.note) { status.className = 'autosave ok'; status.textContent = state.note; state.note = null; }   // the page was drawn again after the save
      for (const [f, label, tip, only] of FIELDS) {
        if (only && only !== (r.weapon ? 'object' : 'punch')) continue;
        const inp = el('input'); inp.type = 'text'; inp.value = fix(r[f], f === 'speed' || f === 'win_start' || f === 'win_end' || f === 'mult' ? 3 : 2);
        const lab = el('label', '', label);
        lab.title = tip;
        inp.title = tip;
        inp.onchange = () => saveMove(r, f, label, inp, status);
        grid.append(lab, inp);
      }
      // the combo: which move may follow this one, and after how many ms (0 = none)
      const nx = el('div', 'editgrid');
      for (const [k, name] of NEXT) {
        const f = 'next_' + k, label = 'Then ' + name;
        const inp = el('input'); inp.type = 'text'; inp.value = fix(r.next_ms[k], 0);
        const lab = el('label', '', label);
        const tip = 'The combo: ms before a ' + name + ' move may follow this one (0: it cannot). The move that follows is the next step of the combo.';
        lab.title = tip; inp.title = tip;
        inp.onchange = () => saveMove(r, f, label, inp, status);
        nx.append(lab, inp);
      }
      edit.append(grid, el('h5', '', 'Combo: what may follow'), nx, status);
    }

    function show() {
      state.view = view.value; state.all = all.checked;
      out.innerHTML = '';
      const v = view.value;
      allL.hidden = v === 'special' || v === 'other';
      if (v === 'special' || v === 'other') {
        edit.innerHTML = '';
        const list = v === 'special' ? m.specials : m.others;
        if (!list.length) out.appendChild(el('div', 'dim', v === 'special' ? 'No special move key in this .CHR.' : 'No other moves in this .CHR.'));
        for (const s of list) out.appendChild(fieldTable(s, m.own, saveKey, v === 'other'));
        out.appendChild(el('div', 'dim', v === 'special'
          ? 'The game\'s own numbers of each special state (the training mode\'s MOVE LIST describes them in words).'
          : 'Butt slam, ram attack, the grab\'s range and the throw. The damage of the hits inside a grab, of the getting-up attack, the counter window and the taunt are in the game\'s code for each monster type, not in the .CHR.'));
        if (state.note) { keyNote(state.note, false); state.note = null; }
        return;
      }
      const weapon = v === 'weapon';
      const rows = (weapon ? m.swipes : m.punches).filter(r => all.checked || r.pos === 1);
      if (!rows.length) { out.appendChild(el('div', 'dim', 'No moves of this kind in the .CHR.')); edit.innerHTML = ''; return; }
      out.appendChild(table(rows, weapon, names, pick, all.checked));
      const ch = m.chains.filter(c => c.weapon === weapon);
      if (ch.length) {
        out.appendChild(el('div', 'chains', 'COMBOS (the same button again): ' + ch.map(c =>
          moveName(c) + ' × ' + c.length + ' = ' + (weapon ? '×' + fix(c.damage, 2) + ' total' : fix(c.damage, 1) + ' damage')).join('   ·   ')));
      }
      if (!weapon && m.default_radius) out.appendChild(el('div', 'dim', '* radius 0 in the file: the monster\'s default fist (' + fix(m.default_radius, 1) + ') is used.'));
      if (weapon) out.appendChild(el('div', 'dim', 'With an object in hand, the damage is the object\'s own times this multiplier.'));
      // the move picked before (this view's), still on after a change
      const keep = rowsAll().find(r => ((r.weapon ? 'w' : 'p') + r.pos + '.' + r.ptype) === state.pick && r.weapon === weapon);
      const tr = keep && out.querySelector('tr[data-id="' + state.pick + '"]');
      if (tr) { tr.classList.add('on'); drawEdit(keep); setMoveHit(...hitOf(keep)); } else edit.innerHTML = '';
    }
    view.onchange = show;
    all.onchange = show;
    show();
  }
  await load();
}
