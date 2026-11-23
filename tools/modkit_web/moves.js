// The Moves view of a monster: a full-screen page over the kit (openMoves) in three columns: the list of its moves on the left,
// the numbers of the picked one in the middle (the game's training-mode data: damage, hit window, radius, stun, knockback, which
// bones hit, the length in frames; with the monster's own .CHR they can be changed right there), the 3D model playing it on the right
// with the hit sphere drawn on its bones. Server side: tools/chr_view.py (monster_moves, monster_move_set).
import { el, button, stage } from './panel.js';
import { playMove, setMoveHit, animName, hitSpheresOn, setHitSpheres } from './viewer.js';

const BUTTONS = { LIGHT: 'LIGHT', HEAVY: 'HEAVY' };
const DIRS = { 8: 'FORWARD + ', 2: 'BACK + ', 5: '' };
const moveName = r => (DIRS[r.dir] || '') + BUTTONS[r.button];

// The buttons as pictures (Phosphor icons in vendor/phosphor, drawn as a mask so CSS colors them): a white fist in a ring, red for
// HEAVY and blue for LIGHT, with an arrow before it for FORWARD (up) and BACK (down); the grab is an open hand. The name stays as the hover text.
const ICONS = { fist: 'hand-fist-fill', hand: 'hand-fill', up: 'arrow-up-bold', down: 'arrow-down-bold' };
function icon(name, cls) {
  const i = el('i', 'mvic' + (cls ? ' ' + cls : ''));
  const u = 'url(vendor/phosphor/' + ICONS[name] + '.svg)';
  i.style.webkitMaskImage = u;
  i.style.maskImage = u;
  return i;
}
function glyph(button, dir) {
  const g = el('span', 'mvglyph');
  g.title = (DIRS[dir] || '') + BUTTONS[button];
  if (dir === 8) g.appendChild(icon('up', 'dir'));
  if (dir === 2) g.appendChild(icon('down', 'dir'));
  const b = el('span', 'mvbtn ' + button.toLowerCase());
  b.appendChild(icon('fist'));
  g.appendChild(b);
  return g;
}
const moveGlyph = r => glyph(r.button, r.dir);

const COLS = {
  move: ['Move', 'The button (and direction) that starts it. LIGHT is Square, HEAVY is Triangle. The arrow opens the moves that may follow it in the combo.'],
  dmg: ['Damage', 'The damage the move requests. With an object it is a multiplier of the object\'s own damage.'],
  win: ['Hit frames', 'The frames of the move (60 per second, at the move\'s speed, counted from its start) where the game tests the hit. Estimated from the animation.'],
  total: ['Total', 'The running total of the combo: the damage of this move plus every move before it in the combo. The Damage column is this move\'s own hit. ' +
                   '(With an object: the multipliers added.)'],
  sat: ['Length (est.)', 'Estimated from the animation: its length in frames (60 per second) at the move\'s speed. The training mode measures it in a fight and ' +
                  'gets other numbers: the hit pause, the blend back to idle, being hit or blocked and the AI\'s own timing come on top.'],
  radius: ['Radius', 'Radius of the sphere swept along the bone each frame; a hit needs it to touch a body. A radius of 0 in the file uses the monster\'s default fist.'],
  stun: ['Stun', 'Above 100 the victim is stunned (cannot act for a moment).'],
  kb: ['Knockback', 'Out / up. Anything other than 0 knocks the victim down.'],
  loc: ['Hits with', 'The bones that test the hit (the fist, the foot, the tail tip...).'],
  anim: ['Animation', 'The animation id the move plays (its name from the animation list).'],
};

// the numbers of one move that can be changed (field = tools/chr_view.py EDIT), with what each one is
const FIELDS = [
  ['damage', 'Damage', 'The damage the move requests.', 'punch'],
  ['mult', 'Damage ×', 'The multiplier of the held object\'s own damage.', 'object'],
  ['win_start', 'Hit from (0–1)', 'Where in the animation (0 to 1) the hit test starts; the frames of the card above follow it.'],
  ['win_end', 'Hit until (0–1)', 'Where in the animation (0 to 1) the hit test ends.'],
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
const NEXT = [[5, 'LIGHT', 'LIGHT', 5], [3, 'FORWARD + LIGHT', 'LIGHT', 8], [4, 'BACK + LIGHT', 'LIGHT', 2],
              [0, 'HEAVY', 'HEAVY', 5], [1, 'FORWARD + HEAVY', 'HEAVY', 8], [2, 'BACK + HEAVY', 'HEAVY', 2]];

const fix = (v, n = 0) => (Number.isFinite(v) ? v.toFixed(n).replace(/\.0+$/, '') : '-');

// the frames where the move tests the hit, from its start, at its speed ("13–29"); the % of the animation when there is no model
const hitFrames = r => {
  const t = r.timing;
  return t ? t.start + '–' + (t.start + t.active) : fix(r.win_start * 100) + '–' + fix(r.win_end * 100) + '%';
};
const doesNothing = r => !(r.weapon ? r.mult > 0 : r.damage > 0);
// the order of the moves that may follow a move: the same button first, then LIGHT, FORWARD + LIGHT, BACK + LIGHT, HEAVY...
const ORDER = NEXT.map(n => n[0]);

// The combo as a tree (the left column): the six moves that start a combo; the arrow of a move opens the moves that may follow it
// (the next step of the combo, the same button first), and so on down. A move that comes after another one needs that move's
// delay (next_ms) above 0. Rows are built when opened. state: {open: Set of paths, seeded}; onPick(row, path, ms) when one is picked.
// Returns {el, nodes}: nodes maps a path ("p:1.5>2.5" = the step 1 type 5, then the step 2 type 5) to {line, r, via}.
function tree(rows, weapon, state, onPick) {
  const by = new Map(rows.map(r => [r.pos + '.' + r.ptype, r]));
  const prefix = weapon ? 'w:' : 'p:';
  const root = el('div', 'mvtree');
  const head = el('div', 'trow thead');
  for (const [k, cls] of [['move', 'tl'], ['dmg', 'tn'], ['win', 'tn'], ['total', 'tn']]) { const c = el('span', cls, COLS[k][0]); c.title = COLS[k][1]; head.appendChild(c); }
  root.appendChild(head);
  const nodes = new Map();
  const kidsOf = r => ORDER.filter(k => r.next_ms[k] > 0 && by.has((r.pos + 1) + '.' + k))
    .sort((a, b) => (a === r.ptype ? 0 : 1) - (b === r.ptype ? 0 : 1)).map(k => by.get((r.pos + 1) + '.' + k));
  const open = state.open;

  function node(r, tokens, total, via, depth, into) {
    const path = prefix + tokens.join('>');
    const wrap = el('div', 'tnode');
    const line = el('div', 'trow' + (doesNothing(r) ? ' zero' : ''));
    line.style.paddingLeft = (4 + depth * 12) + 'px';
    const kids = kidsOf(r);
    const sub = el('div', 'tkids');
    let built = false;
    const tg = el('span', 'ttg');
    const expand = on => {
      if (on && !built) {
        built = true;
        for (const c of kids) {
          const v = c.weapon ? c.mult : c.damage;
          node(c, [...tokens, c.pos + '.' + c.ptype], total + (v > 0 ? v : 0), r.next_ms[c.ptype], depth + 1, sub);
        }
      }
      sub.hidden = !on;
      tg.textContent = on ? '▾' : '▸';
      if (on) open.add(path); else open.delete(path);
    };
    const tl = el('span', 'tl');
    tl.append(tg, moveGlyph(r), el('span', 'tstep', 'step ' + r.pos));
    line.append(tl, el('span', 'tn', weapon ? '×' + fix(r.mult, 2) : fix(r.damage, 1)), el('span', 'tn', hitFrames(r)),
                el('span', 'tn', weapon ? '×' + fix(total, 2) : fix(total, 1)));
    line.title = moveName(r) + ', step ' + r.pos + (via ? ' · may follow after ' + via + ' ms' : '') +
      (doesNothing(r) ? ' · does no damage: a filler slot of the file (no bone tests a hit)' : '');
    tg.textContent = '▸';
    if (kids.length) { tg.onclick = e => { e.stopPropagation(); expand(sub.hidden); }; } else tg.classList.add('none');
    line.onclick = () => { for (const n of nodes.values()) n.line.classList.remove('on'); line.classList.add('on'); onPick(r, path, via); };
    nodes.set(path, { line, r, via });
    wrap.append(line, sub);
    into.appendChild(wrap);
    sub.hidden = true;
    if (kids.length && open.has(path)) expand(true);
  }

  const starts = ORDER.filter(k => by.has('1.' + k)).map(k => by.get('1.' + k));
  if (!state.seeded && starts.length) { state.seeded = true; open.add(prefix + '1.' + starts[0].ptype); }   // the first one open, so the tree shows
  for (const r of starts) { const v = weapon ? r.mult : r.damage; node(r, ['1.' + r.ptype], v > 0 ? v : 0, 0, 0, root); }
  return { el: root, nodes };
}

// what the views hold, on hover of the select
const VIEW_TIPS = {
  punch: 'The punches of each button and the combos they chain into.',
  weapon: 'The moves with an object in hand: the damage is the object\'s own times the multiplier.',
  other: 'Butt slam, ram attack, the grab\'s range and the throw. The damage of the hits inside a grab, of the getting-up attack, the counter window and the taunt ' +
         'are in the game\'s code for each monster type, not in the .CHR.',
  special: 'The game\'s own numbers of each special state (the training mode\'s MOVE LIST describes them in words).',
};
// The animation each state of Other moves and Specials plays in the 3D view. The ids are the same for every monster (docs/ANIM_IDS.md): the
// butt slam's drop, the ram's impact, the grab's reach, the throw at the target, crowd control's loop on the ground; any other special plays the
// special's active loop (160 is its windup, 162 its winddown).
const STATE_ANIM = { 'BUTT SLAM': 262, 'RAM ATTACK': 267, 'GRAB': 170, 'GRAB: THROW': 55, 'CROWD CONTROL': 164 };
const animOfState = (s, view) => STATE_ANIM[s.name] || (view === 'special' ? 161 : 0);

// What holds the middle column and the 3D view (the veil over them) while something loads: counts the jobs running
let busyN = 0;
function busy(d) {
  busyN = Math.max(0, busyN + d);
  const o = document.querySelector('.movesview');
  if (o) o.classList.toggle('busy', busyN > 0);
}
async function withBusy(p) {
  busy(1);
  try { return await p; } finally { busy(-1); }
}

// the picked move's numbers, big (the middle column): one card each, with what it is on hover
function statCards(r, weapon, names, via) {
  const tm = r.timing;
  const cards = [
    [weapon ? 'Damage ×' : 'Damage', COLS.dmg[1], weapon ? '×' + fix(r.mult, 2) : fix(r.damage, 1)],
    [COLS.win[0], COLS.win[1], hitFrames(r), tm ? 'of ' + tm.total + ' · ' + fix(r.win_start * 100) + '–' + fix(r.win_end * 100) + '% of the animation' : 'of the animation (no model)'],
    [COLS.sat[0], COLS.sat[1], tm ? String(tm.total) : '-', tm ? 'frames, at ×' + fix(r.speed, 2) : 'no model'],
    [COLS.radius[0], COLS.radius[1], fix(r.radius, 1), r.radius_default ? 'the monster\'s default fist' : ''],
    [COLS.stun[0], COLS.stun[1], fix(r.stun), r.stun > 100 ? 'stuns' : ''],
    [COLS.kb[0], COLS.kb[1], (r.kb_out || r.kb_up) ? fix(r.kb_out) + ' / ' + fix(r.kb_up) : '-', (r.kb_out || r.kb_up) ? 'out / up: knocks down' : ''],
    ['Hit pause', 'How long the game freezes the two monsters on a hit.', fix(r.pause) + ' ms'],
    ['Speed', 'The animation\'s speed: 1 is its own pace.', '×' + fix(r.speed, 2)],
    ['Stamina', 'What the move takes from the stamina meter.', fix(r.stamina, 1)],
    [COLS.loc[0], COLS.loc[1], r.bones || '-'],
    [COLS.anim[0], COLS.anim[1], String(r.anim), names[r.anim] || ''],
  ];
  if (via) cards.push(['Follows after', 'The delay of the move before it in the combo: this one may start that many ms after it.', via + ' ms', 'from the move before']);
  const box = el('div', 'mvcards');
  for (const [k, tip, v, sub] of cards) {
    const c = el('div', 'mvcard');
    c.title = tip;
    c.append(el('span', 'k', k), el('b', 'v', v));
    if (sub) c.appendChild(el('span', 's', sub));
    box.appendChild(c);
  }
  return box;
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

// page: the element that gets the list (left) and the numbers (middle); args: {cid} (a custom monster) or {group} (a costume of a
// game monster); hooks.extract(): gives the monster its own .CHR (Extract stats) and resolves when it is done
export async function movesPage(page, args, hooks = {}) {
  // what the page shows, kept over a reload: the view, the picked move (its path in the combo tree), the open arrows
  const state = { view: 'punch', pickPath: null, open: new Set(), seeded: false, sp: null, note: null, scroll: [0, 0] };
  // empties the page but for the loading veil (openMoves puts it there)
  const clear = () => { for (const c of [...page.children]) if (!c.classList.contains('mvveil')) c.remove(); };
  async function load() {
    clear();
    busy(1);
    let m;
    try { m = await kit.api('do/monster_moves', args); } finally { busy(-1); }
    clear();
    if (m.error) { page.appendChild(el('div', 'dim msg', 'No move table: ' + m.error + '.')); return; }
    if (m.note) { page.appendChild(el('div', 'dim msg', m.note)); return; }
    await draw(m);
  }

  async function draw(m) {
    const names = {};
    const prefix = (m.file || '').replace(/\.CHR$/i, '').toUpperCase();
    for (const r of [...m.punches, ...m.swipes]) if (!(r.anim in names)) names[r.anim] = await animName(r.anim, prefix);

    const left = el('div', 'mvleft'), mid = el('div', 'mvmid');
    page.append(left, mid);

    // middle column, top: where the numbers come from, the copy button, the warnings
    if (!m.own && hooks.extract) {
      const b = button('Give it its own .CHR to edit', async () => { b.disabled = true; await withBusy(hooks.extract()); load(); });
      b.title = 'Extract stats: copies the base type\'s .CHR and .AIC to this monster\'s files (the same as Stats & sounds)';
      mid.appendChild(b);
    }
    if (m.problems && m.problems.length) {
      const w = el('div', 'warnbox');
      w.appendChild(el('div', '', 'Check these moves of the .CHR:'));
      const ul = el('ul');
      for (const p of m.problems.slice(0, 12)) ul.appendChild(el('li', '', p));
      if (m.problems.length > 12) ul.appendChild(el('li', '', '… and ' + (m.problems.length - 12) + ' more'));
      w.appendChild(ul);
      mid.appendChild(w);
    }
    const noteBox = el('div');
    const edit = el('div', 'moveedit');
    const intro = el('div', 'dim mvfoot', 'From ' + m.label + (m.own ? ': this monster\'s own .CHR' : ': the base type\'s .CHR') + (m.frames ? '' : '. No model: no frames.'));
    mid.append(noteBox, edit, intro);

    // left column: the view, then the list
    const view = el('select');
    for (const [v, t] of [['punch', 'Punches'], ['weapon', 'With an object'], ['other', 'Other moves'], ['special', 'Specials']]) { const o = el('option', '', t); o.value = v; view.appendChild(o); }
    view.value = state.view;
    const bar = el('div', 'inline mvbar');
    bar.append(view);
    const out = el('div', 'movesout');
    left.append(bar, out);

    // the animation plays at the move's speed, so its frames are the game's (viewer.js); the window and the sphere come with it
    const hitOf = r => [r.timing ? [r.win_start, r.win_end, r.timing.start, r.timing.start + r.timing.active] : [r.win_start, r.win_end],
                        { locators: r.locators, radius: r.radius }, r.speed];
    // the middle column fills at once; the 3D view plays the move, and the veil holds both until it is there
    const pick = (r, path, via) => { state.pickPath = path; drawEdit(r, path, via); return withBusy(playMove(r.anim, ...hitOf(r))); };
    // a special or an other move plays the animation the game uses for it (no hit window or sphere)
    const pickState = (s, view) => { state.sp = s.name; drawKey(s); return withBusy(playMove(animOfState(s, view), null, null, 1)); };
    // writes a number and draws the page again from the file (the list, the frames, the warnings follow it)
    async function reread(note) {
      state.note = note;
      state.scroll = [out.scrollTop, mid.scrollTop];
      clear();
      busy(1);
      let m2;
      try { m2 = await kit.api('do/monster_moves', args); } finally { busy(-1); }
      if (m2.error) { page.appendChild(el('div', 'dim msg', 'No move table: ' + m2.error + '.')); return; }
      await draw(m2);
      out.scrollTop = state.scroll[0];
      mid.scrollTop = state.scroll[1];
    }
    async function saveMove(r, f, label, inp, status) {
      status.className = 'autosave busy'; status.textContent = 'Saving…';
      const res = await withBusy(kit.api('do/monster_move_set', { ...args, weapon: !!r.weapon, pos: r.pos, ptype: r.ptype, field: f, value: inp.value }));
      if (res.error) { status.className = 'autosave err'; status.textContent = res.error.split(f).join(label); inp.value = fix(r[f], 3); return; }
      reread('✓ Saved: ' + label + ' ' + fix(res.old, 3) + ' → ' + fix(res.new, 3) + ' (Build mod puts it in the game)');
    }
    async function saveKey(f, inp) {
      const res = await withBusy(kit.api('do/monster_key_set', { ...args, key: f.key, off: f.off, value: inp.value }));
      if (res.error) { inp.value = String(f.value); keyNote(res.error, true); return; }
      reread('✓ Saved: ' + f.label + ' ' + fix(res.old, 3) + ' → ' + fix(res.new, 3) + ' (Build mod puts it in the game)');
    }
    const keyNote = (text, bad) => {
      noteBox.innerHTML = '';
      noteBox.appendChild(el('div', 'autosave keynote ' + (bad ? 'err' : 'ok'), text));
    };
    const notOwn = () => el('div', 'dim', 'The disc\'s numbers are not edited. ' + (hooks.extract ? '[Give it its own .CHR to edit] above makes a copy for this monster.' : ''));

    // path: the combo that leads to the move ("p:1.5>2.5"), drawn as its buttons in a row; via: the ms the move before allows
    function drawEdit(r, path, via) {
      edit.innerHTML = '';
      if (!r) return;
      const h = el('h5', '', 'Combo' + (r.weapon ? ' with an object' : '') + ':');
      const rows = r.weapon ? m.swipes : m.punches;
      path.slice(2).split('>').forEach((key, i) => {
        const q = rows.find(x => x.pos + '.' + x.ptype === key);
        if (i) h.appendChild(el('span', 'sep', '›'));
        if (q) h.appendChild(moveGlyph(q));
      });
      h.append('step ' + r.pos);
      edit.appendChild(h);
      edit.appendChild(statCards(r, !!r.weapon, names, via));
      if (!m.own) { edit.appendChild(notOwn()); return; }
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
      for (const [k, name, btn, dir] of NEXT) {
        const f = 'next_' + k, label = 'Then ' + name;
        const inp = el('input'); inp.type = 'text'; inp.value = fix(r.next_ms[k], 0);
        const lab = el('label', 'then');
        lab.append('Then ', glyph(btn, dir));
        const tip = 'The combo: ms before a ' + name + ' move may follow this one (0: it cannot). The move that follows is the next step of the combo.';
        lab.title = tip; inp.title = tip;
        inp.onchange = () => saveMove(r, f, label, inp, status);
        nx.append(lab, inp);
      }
      edit.append(el('h5', '', 'Change it'), grid, el('h5', '', 'Combo: what may follow'), nx, status);
    }
    // a special or one of the other moves (the numbers of a state of the .CHR): its fields
    function drawKey(s) {
      edit.innerHTML = '';
      if (!m.own) edit.appendChild(notOwn());
      edit.appendChild(fieldTable(s, m.own, saveKey, true));
    }

    function show() {
      state.view = view.value;
      out.innerHTML = '';
      noteBox.innerHTML = '';
      const v = view.value;
      view.title = VIEW_TIPS[v];
      if (v === 'special' || v === 'other') {
        edit.innerHTML = '';
        const list = v === 'special' ? m.specials : m.others;
        if (!list.length) { out.appendChild(el('div', 'dim', v === 'special' ? 'No special move key in this .CHR.' : 'No other moves in this .CHR.')); setMoveHit(null, null); return; }
        const keep = list.find(s => s.name === state.sp) || list[0];
        const rows = [];
        for (const s of list) {
          const d = el('div', 'pickrow' + (s === keep ? ' on' : ''));
          if (/^GRAB/.test(s.name)) d.appendChild(icon('hand', 'grab'));   // the grab and its throw: an open hand
          d.append(s.name);
          d.onclick = () => { for (const o of rows) o.classList.toggle('on', o === d); pickState(s, v); };
          rows.push(d);
          out.appendChild(d);
        }
        pickState(keep, v);   // the first one (or the one picked before) plays at once
        if (state.note) { keyNote(state.note, false); state.note = null; }
        return;
      }
      const weapon = v === 'weapon';
      const rows = weapon ? m.swipes : m.punches;
      if (!rows.length) { out.appendChild(el('div', 'dim', 'No moves of this kind in the .CHR.')); edit.innerHTML = ''; setMoveHit(null, null); return; }
      const T = tree(rows, weapon, state, pick);
      out.appendChild(T.el);
      // the move picked before (this view's), still on after a change; else the first one, so the 3D view plays something
      const keep = state.pickPath && state.pickPath.startsWith(weapon ? 'w:' : 'p:') && T.nodes.get(state.pickPath);
      if (keep) { keep.line.classList.add('on'); drawEdit(keep.r, state.pickPath, keep.via); setMoveHit(...hitOf(keep.r)); } else T.nodes.values().next().value.line.click();
    }
    view.onchange = show;
    show();
  }
  await load();
}

// The full-screen Moves view over the kit: args = {cid} | {group} of the monster, group = the work folder group whose model shows
// (the costume's or the custom's), g = that group (its name over the model), restore() = puts the 3D view back where it was.
// Resolves when it is closed.
export async function openMoves({ args, group, g, hooks = {}, restore }) {
  const ov = el('div', 'movesview');
  const head = el('div', 'mvhead');
  const sphOn = el('input'); sphOn.type = 'checkbox'; sphOn.checked = hitSpheresOn();
  const sphL = el('label', 'check'); sphL.append(sphOn, document.createTextNode(' hit spheres'));
  sphL.title = 'Draws the hit sphere of the picked move at each bone that tests the hit, on the playing animation: red while the hit window is open, dim outside it.';
  sphOn.onchange = () => setHitSpheres(sphOn.checked);
  let close;
  const done = new Promise(r => { close = r; });
  head.append(el('h3', '', 'Moves' + (g && g.title ? ': ' + g.title : '')), sphL, button('Close ✕', () => close()));
  const grid = el('div', 'mvgrid');
  const page = el('div', 'mvlm');
  const right = el('div', 'mvright');
  const v = el('div', 'v3d'), ab = el('div');
  // the veils: a spinner over the middle column and over the 3D view (the list stays usable), shown while the view is `busy`
  right.append(v, ab, el('div', 'mvveil'));
  page.appendChild(el('div', 'mvveil'));
  grid.append(page, right);
  ov.append(head, grid);
  busyN = 0;
  document.body.appendChild(ov);
  busy(1);
  // Escape closes it, unless a dialog of the kit is on top
  const onKey = e => { const md = document.getElementById('modal'); if (e.key === 'Escape' && !(md && md.style.display === 'flex')) close(); };
  document.addEventListener('keydown', onKey);
  done.then(() => { document.removeEventListener('keydown', onKey); ov.remove(); if (restore) restore(); });
  await stage(v, ab, group, g, { moves: true });   // the animation bar is ready before the first move plays
  busy(-1);
  await movesPage(page, args, hooks);
  return done;
}
