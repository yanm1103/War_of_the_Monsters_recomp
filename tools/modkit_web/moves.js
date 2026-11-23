// The Moves tab of a monster's panel: its move table, the numbers of the game's training mode (damage, hit window, radius, stun,
// knockback, which bones hit) read from its .CHR, with the length of each move in frames. A row plays the move's animation in the
// 3D view above and marks where it hits. Server side: tools/chr_view.py (monster_moves).
import { el } from './panel.js';
import { playMove, animName } from './viewer.js';

const BUTTONS = { LIGHT: 'LIGHT', HEAVY: 'HEAVY' };
const DIRS = { 8: 'FORWARD + ', 2: 'BACK + ', 5: '' };
const moveName = r => (DIRS[r.dir] || '') + BUTTONS[r.button];

const COLS = {
  pos: ['Pos', 'The step of the combo: 1 is the first move; the next step comes if the combo input is accepted (the COMBOS list says which buttons chain).'],
  move: ['Move', 'The button (and direction) that starts it. LIGHT is Square, HEAVY is Triangle.'],
  dmg: ['Damage', 'The damage the move requests. With an object it is a multiplier of the object\'s own damage.'],
  win: ['Hit window', 'Part of the animation, in %, where the game tests the hit.'],
  sat: ['Frames', 'In game frames (60 per second, the animation at the move\'s speed): startup (to the first hit test) · active (frames with a ' +
                  'hit test) · total length. The hit pause and the blend back to idle come on top.'],
  radius: ['Radius', 'Radius of the sphere swept along the bone each frame; a hit needs it to touch a body.'],
  stun: ['Stun', 'Above 100 the victim is stunned (cannot act for a moment).'],
  kb: ['Knockback', 'Out / up. Anything other than 0 knocks the victim down.'],
  loc: ['Hits with', 'The bones that test the hit (the fist, the foot, the tail tip...).'],
  anim: ['Animation', 'The animation id the move plays (its name from the animation list).'],
};

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
    const cells = {
      pos: r.pos, move: moveName(r),
      dmg: weapon ? '×' + fix(r.mult, 2) : fix(r.dano, 1),
      win: fix(r.ini * 100) + '–' + fix(r.fim * 100) + '%',
      sat: tm ? tm.start + ' · ' + tm.active + ' · ' + tm.total : '-',
      radius: fix(r.raio, 1) + (r.raio_padrao ? '*' : ''), stun: fix(r.atord),
      kb: (r.kb_h || r.kb_v) ? fix(r.kb_h) + ' / ' + fix(r.kb_v) : '-', loc: r.loc,
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

function fieldTable(s) {
  const d = el('details', 'special');
  d.appendChild(el('summary', '', s.name + '  (' + s.state + ')'));
  const t = el('table', 'moves fields');
  const b = el('tbody');
  for (const f of s.fields) {
    const tr = el('tr');
    tr.appendChild(el('td', 'l', f.label));
    const rings = Array.isArray(f.value);
    tr.appendChild(el('td', 'l', rings ? f.value.map(r => '[' + r.join(', ') + ']').join('  ') : String(f.value)));
    b.appendChild(tr);
  }
  t.appendChild(b);
  d.appendChild(t);
  return d;
}

// page: the tab's element; args: {cid} (a custom monster) or {group} (a costume of a game monster)
export async function movesPage(page, args) {
  page.appendChild(el('div', 'dim', 'Reading the move table…'));
  const m = await kit.api('do/monster_moves', args);
  page.innerHTML = '';
  if (m.error) { page.appendChild(el('div', 'dim', 'No move table: ' + m.error + '.')); return; }
  if (m.note) { page.appendChild(el('div', 'dim', m.note)); return; }

  const names = {};
  const prefix = (m.file || '').replace(/\.CHR$/i, '').toUpperCase();
  for (const r of [...m.punches, ...m.swipes]) if (!(r.anim in names)) names[r.anim] = await animName(r.anim, prefix);

  page.appendChild(el('div', 'dim', 'From ' + m.label + (m.own ? ' (this monster\'s own .CHR)' : ' (the base type\'s .CHR; [Extract stats] in the Stats & sounds ' +
    'tab gives this monster its own to edit)') + '. The numbers are the game\'s; the frames are an estimate' + (m.frames ? '' : ' (no model: no frames)') +
    ' at 60 per second. A row plays that move above.'));

  const view = el('select');
  for (const [v, t] of [['punch', 'Punches'], ['weapon', 'With an object'], ['special', 'Specials']]) { const o = el('option', '', t); o.value = v; view.appendChild(o); }
  const all = el('input'); all.type = 'checkbox';
  const allL = el('label', 'check'); allL.append(all, document.createTextNode(' every combo step'));
  const bar = el('div', 'inline');
  bar.append(view, allL);
  const out = el('div', 'movesout');
  page.append(bar, out);

  const pick = r => playMove(r.anim, [r.ini, r.fim]);
  const draw = () => {
    out.innerHTML = '';
    const v = view.value;
    allL.hidden = v === 'special';
    if (v === 'special') {
      if (!m.specials.length) out.appendChild(el('div', 'dim', 'No special move key in this .CHR.'));
      for (const s of m.specials) out.appendChild(fieldTable(s));
      out.appendChild(el('div', 'dim', 'The game\'s own numbers of each special state (the training mode\'s MOVE LIST describes them in words).'));
      return;
    }
    const weapon = v === 'weapon';
    const rows = (weapon ? m.swipes : m.punches).filter(r => all.checked || r.pos === 1);
    if (!rows.length) { out.appendChild(el('div', 'dim', 'No moves of this kind in the .CHR.')); return; }
    out.appendChild(table(rows, weapon, names, pick, all.checked));
    const ch = m.chains.filter(c => c.weapon === weapon);
    if (ch.length) {
      out.appendChild(el('div', 'chains', 'COMBOS (the same button again): ' + ch.map(c =>
        moveName(c) + ' × ' + c.length + ' = ' + (weapon ? '×' + fix(c.damage, 2) + ' total' : fix(c.damage, 1) + ' damage')).join('   ·   ')));
    }
    if (!weapon && m.default_radius) out.appendChild(el('div', 'dim', '* radius 0 in the file: the monster\'s default fist (' + fix(m.default_radius, 1) + ') is used.'));
    if (weapon) out.appendChild(el('div', 'dim', 'With an object in hand, the damage is the object\'s own times this multiplier.'));
  };
  view.onchange = draw;
  all.onchange = draw;
  draw();
}
