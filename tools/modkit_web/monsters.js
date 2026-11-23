// Custom monsters (MODS.md, "Custom monsters"): New monster..., Turn into custom monster... (an extracted costume becomes a
// custom with its own files), the monster's panel in place of the preview (the model in 3D, then three tabs: Settings, with
// the name, camera, narrator, special rules, special, CPUs and HUD portrait; 3D model, with the check and one block per way
// to give it a body; Stats & sounds), Duplicate / Delete.
import { meshBlocks, currentModel } from './mesh.js';
import { panelFrame, tabs, block, el, button, autosave } from './panel.js';
import { fightCamera } from './viewer.js';
import { openMoves } from './moves.js';
import { costumeRadios, skinDialog } from './skins.js';

const $ = id => document.getElementById(id);
const IMG = [['Images', ['*.png', '*.jpg', '*.jpeg', '*.bmp', '*.tga', '*.gif', '*.tif', '*.tiff', '*.webp']]];
let lists = null;
async function getLists() { if (!lists) lists = await kit.api('do/monster_lists', {}); return lists; }

function row(label, ...kids) {
  const r = el('div', 'frow');
  r.append(el('span', 'flabel', label), ...kids);
  return r;
}
// what a setting is, on hover of its whole row
const TIPS = {
  Camera: "The fight camera: another monster's (its height and angles), with the distance from the monster to the camera in %. " +
          "(the base's own) = the base type's camera.",
  Narrator: 'The line the narrator says to announce this character when it is picked: narrator/<name>.mp3 in the mods. ' +
            "Empty = the base's.",
  'Special rules': 'Extra behaviour the port gives a monster on top of its base type. Only "mecha" for now: the final boss\'s ' +
                   "robot (the laser aimed through the tower camera, the shield, no jump). Empty = the base type's rules.",
  'Special move': "The special move (Triangle + Circle): another monster's, or the base type's own.",
  'Name logo': 'The name drawn as a picture, shown under the model in the select screen and in the save list (like the game\'s ' +
               'monsters). A 2:1 PNG; another shape is stretched to 2:1. Without it, the name in letters.',
};
function tip(r, label) { if (TIPS[label]) r.title = TIPS[label]; return r; }
function select(options, value) {   // options: [[value, text]]
  const s = el('select');
  for (const [v, t] of options) { const o = el('option', '', t); o.value = v; s.appendChild(o); }
  s.value = value;
  return s;
}
function cameraRow(L, from, scale) {
  const s = select([['', "(the base's own)"], ...L.bases.map(b => [b.base, b.label])], from || '');
  const n = el('input');
  n.type = 'number';
  n.min = 10;
  n.max = 1000;
  n.step = 10;
  n.value = scale || 100;
  n.style.width = '80px';
  const r = el('input');   // the same distance as a slider (the number takes more than 400)
  r.type = 'range';
  r.min = 10;
  r.max = 400;
  r.step = 5;
  r.value = Math.min(400, n.value);
  r.addEventListener('input', () => { n.value = r.value; n.dispatchEvent(new Event('input')); });
  r.addEventListener('change', () => n.dispatchEvent(new Event('change')));
  n.addEventListener('input', () => { r.value = Math.min(400, n.value); });
  const box = el('div', 'camrow');
  const dist = el('div', 'inline');
  const own = el('div', 'dim', "The base's own camera keeps its distance: pick a monster's camera (its own type too) to change it.");
  dist.append(el('span', 'dim', 'Distance from monster to camera'), r, n, el('span', 'dim', '%'));
  box.append(s, dist, own);
  const sync = () => { n.disabled = r.disabled = !s.value; own.hidden = !!s.value; };
  s.addEventListener('change', sync);
  sync();
  return { box, from: s, scale: n };
}
function narratorInput(L, value) {
  const i = el('input');
  i.setAttribute('list', 'narrators');
  i.value = value || '';
  i.spellcheck = false;
  if (!$('narrators')) {
    const dl = el('datalist');
    dl.id = 'narrators';
    for (const n of L.narrators) { const o = el('option'); o.value = n; dl.appendChild(o); }
    document.body.appendChild(dl);
  }
  return i;
}

// the New custom monster / Turn into custom monster dialog. lock: [base, prefix, costume] and the group
export function monsterDialog(title, lock, group) {
  return kit.modal(async (box, close) => {
    const L = await getLists();
    box.appendChild(el('h3', '', title));
    const base = select(L.bases.map(b => [b.base, b.label]), lock ? lock[0] : L.bases[0].base);
    base.disabled = !!lock;
    const hint = el('div', 'dim');
    const coBox = el('div');
    const name = el('input'), cid = el('input');
    name.id = 'monName';
    cid.id = 'monId';
    name.spellcheck = cid.spellcheck = false;
    const cam = cameraRow(L);
    const narr = narratorInput(L);
    const tex = el('input'); tex.type = 'checkbox'; tex.checked = true;
    const ai = el('input'); ai.type = 'checkbox';
    const texL = el('label', 'check'); texL.append(tex, document.createTextNode(' Own look: extract the costume\'s textures to edit (only this monster uses them)'));
    const aiL = el('label', 'check'); aiL.append(ai, document.createTextNode(' CPUs may pick this monster'));
    const err = el('div', 'msg');
    const ok = el('button', 'primary', 'OK'); ok.type = 'submit';
    const bs = el('div', 'buttons');
    bs.append(button('Cancel', () => close(null)), ok);
    box.append(row('Base type', base), row('', hint), row('Costume', coBox), row('Name', name), row('Id', cid),
      row('', el('div', 'dim', 'Folder name and what WOTM_MATCH takes: lowercase letters, digits and _, starting with a letter')),
      tip(row('Camera', cam.box), 'Camera'), tip(row('Narrator', narr), 'Narrator'),
      row('', el('div', 'dim', "The line the narrator says to announce this character (narrator/<name>.mp3); empty = the base's")));
    if (!lock) box.appendChild(row('', texL));
    box.append(row('', aiL), err, bs);
    const st = { autoId: true, base: null, radios: null, n: 0 };
    const body = () => ({ base: base.value, costume: st.radios ? st.radios.value() : 0, name: name.value, cid: cid.value.trim(),
                          ai: ai.checked, camera_from: cam.from.value, camera_scale: cam.scale.value, narrator: narr.value,
                          textures: tex.checked && !lock, from_group: group });
    async function check(what) {
      const n = ++st.n;
      if (what === 'base' && base.value !== st.base) {   // another base: its costumes, camera and hint
        st.base = base.value;
        const b = L.bases.find(x => x.base === base.value);
        hint.textContent = b ? b.hint : '';
        const [dco, dcam] = b ? b.defaults : [0, null];
        cam.from.value = dcam ? dcam[0] : '';
        cam.scale.value = dcam ? dcam[1] : 100;
        const r = await kit.api('do/monster_form', { base: base.value });
        coBox.innerHTML = '';
        st.radios = b && b.prefix ? costumeRadios('monCostume', b.prefix, lock ? lock[2] : dco, lock ? [lock[2]] : r.costumes) : null;
        if (st.radios) coBox.appendChild(st.radios);
      }
      const r = await kit.api('do/monster_form', body());
      if (n !== st.n) return;
      if (what === 'name' && st.autoId && r.slug !== undefined) { cid.value = r.slug; return check(); }
      err.textContent = r.error || r.note || '';
      ok.disabled = !!r.error;
    }
    base.onchange = () => check('base');
    name.oninput = () => check('name');
    cid.oninput = () => { st.autoId = false; check(); };
    cam.from.onchange = cam.scale.oninput = () => check();
    box.onsubmit = async e => {
      e.preventDefault();
      if (ok.disabled) return;
      const r = await kit.api('do/monster_new', body());
      if (r.error) { err.textContent = r.error; return; }
      if (r.ok === false) { err.textContent = 'Still working on the previous task; try again when it finishes.'; return; }
      kit.workFocus = r.focus;
      close(true);
    };
    name.value = L.free_name;
    await check('base');
    await check('name');
    name.focus();
  });
}

async function turnIntoMonster(g) {
  const c = g.costume_info;   // [base, prefix, costume]
  if (!await monsterDialog('Turn into custom monster', c, g.key)) return;
  kit.afterTask = async () => {
    const r = await kit.api('do/packed', { group: g.key });
    if (r.files && r.files.length && await kit.confirm('Turn into custom monster', 'The mod already has ' + r.files.length + ' file(s) of ' + r.stem + ' in ' + r.where + ', built before; they still replace the original costume.\n\nDelete them from the mod? (the monster keeps its own copies)', 'Delete'))
      await kit.api('do/packed', { group: g.key, delete: true });
  };
}

async function monsterPanel(g, body, label) {
  const cid = g.key.slice(9);
  label.textContent = '';
  const form = panelFrame(body, g.key, g);
  const [L, p] = await Promise.all([getLists(), kit.api('do/monster_props', { cid })]);
  if (p.missing) { form.appendChild(el('div', 'dim', 'No monster.json in ' + p.missing)); return; }
  const key = 'monsters/' + cid;
  const run = async (action, b) => { kit.workFocus = [key]; return kit.run(action, b); };
  let camOn = false, camReady = false;   // the Camera tab is showing: the 3D view looks through the fight camera
  // The Moves tab is not a page: picking it opens the full-screen Moves view (moves.js) over the panel, and closing that view
  // goes back to the tab that was open. The 3D renderer is lent to the view and put back in the panel when it closes.
  let lastTab = null, opening = false, ready = false;
  const pages = tabs(form, ['Settings', 'Camera', 'Moves', '3D model', 'Stats & sounds'], 'wotm.monsterTab', async t => {
    if (t === 'Moves') {
      if (!ready) { queueMicrotask(() => pages.show('Settings')); return; }   // the last tab of the previous session was Moves: do not open it by itself
      if (opening) return;
      opening = true;
      fightCamera(null);
      // [Give it its own .CHR]: the same as Stats & sounds > Extract stats; the view reads the table again when the task ends
      const extract = async () => { const done = kit.nextDone(); const r = await run('monster_stats', { cid }); if (r.ok) await done; };
      await openMoves({ args: { cid }, group: g.key, g, hooks: { extract }, restore: () => form.restore3d() });
      opening = false;
      pages.show(lastTab || 'Settings');
      return;
    }
    lastTab = t;
    camOn = t === 'Camera';
    if (camOn) { if (camReady) showFightCam(); } else fightCamera(null);
  });
  const [tSet, tCam, , tModel, tStats] = pages;

  // Settings
  const name = el('input');
  name.value = p.data.name || '';
  const cam = cameraRow(L, p.camera_from, p.camera_scale);
  const narr = narratorInput(L, p.narrator);
  const special = select([['', "(the base's own)"], ...L.specials.map(s => [s.base, s.label])], p.special);
  const ai = el('input'); ai.type = 'checkbox'; ai.checked = p.ai;
  const aiL = el('label', 'check'); aiL.append(ai, document.createTextNode(' CPUs may pick this monster'));
  // the HUD portrait the game shows: the one set here, else the portrait texture in its files (edited or not), else the costume's
  const hud = el('div', 'inline');
  const hudImg = el('img', 'hud');
  hudImg.src = p.portrait_ok ? 'api/hud?cid=' + encodeURIComponent(cid) + '&t=' + Date.now() : (g.pic || '');
  if (hudImg.src) hud.appendChild(hudImg);
  hud.appendChild(el('span', 'dim', p.portrait_ok ? 'set here' : p.own_portrait ? 'the portrait texture in its files' : "the costume's"));
  hud.appendChild(button('Set...', async () => {
    const f = await kit.pick('file', 'HUD portrait of ' + cid + ' (2:1, e.g. 128x64 or 512x256)', '', IMG);
    if (f) run('portrait_set', { cid, path: f });
  }));
  if (p.portrait) hud.appendChild(button('Remove', async () => { await kit.api('do/portrait_remove', { cid }); kit.work.reload([key]); }));
  // the name logo of the select screen and the save list ("logo" of monster.json)
  const logo = el('div', 'inline');
  if (p.logo_ok) { const im = el('img', 'hud logo'); im.src = 'api/hud?logo=1&cid=' + encodeURIComponent(cid) + '&t=' + Date.now(); logo.appendChild(im); }
  else logo.appendChild(el('span', 'dim', 'none: the name in letters'));
  logo.appendChild(button('Set...', async () => {
    const f = await kit.pick('file', 'Name logo of ' + cid + ' (2:1, e.g. 256x128 or 512x256; transparent around the letters)', '', IMG);
    if (f) run('logo_set', { cid, path: f });
  }));
  if (p.logo_ok) logo.appendChild(button('Remove', () => run('logo_set', { cid, remove: true })));
  const saveAll = async () => {
    const r = await kit.api('do/monster_save', { cid, name: name.value, camera_from: cam.from.value, camera_scale: cam.scale.value,
                                                 narrator: narr.value, kind: p.kind, special: special.value, ai: ai.checked });
    if (r.error) return r.error;
    kit.work.refreshList();
    return null;
  };
  const status = autosave([name, narr, special, ai], saveAll);
  const sb = el('div', 'btns');
  sb.append(button('New skin for this monster...', () => skinDialog('New skin', null, cid)));

  // Camera: whose fight camera and how far, seen through it in the 3D view above (POV 0, the usual one in a fight)
  const camNote = el('div', 'dim');
  let camReq = 0;
  async function showFightCam() {
    const my = ++camReq;
    const v = await kit.api('do/camera_view', { cid, from: cam.from.value, scale: cam.scale.value });
    if (my !== camReq || !camOn) return;
    if (v.error) { camNote.textContent = 'No camera preview: ' + v.error + '.'; fightCamera(null); return; }
    fightCamera(v);
    camNote.textContent = 'The 3D view above looks through this camera (POV 0, from ' + v.chr + '; in the game it also turns ' +
      'with the speed and the ground). Leave the tab for the free view.';
  }
  cam.from.addEventListener('change', showFightCam);
  cam.scale.addEventListener('input', showFightCam);
  tCam.append(tip(row('Camera', cam.box), 'Camera'), row('', camNote), row('', autosave([cam.from, cam.scale], saveAll)));
  camReady = true;
  if (camOn) showFightCam();
  ready = true;

  tSet.append(row('Base', el('span', '', p.base)), row('Name', name),
    tip(row('Narrator', narr), 'Narrator'), tip(row('Special move', special), 'Special move'),
    row('', aiL), row('HUD portrait', hud), tip(row('Name logo', logo), 'Name logo'), row('Appearance', el('span', 'dim', p.look)),
    row('', status), row('', sb));
  if (p.kind) tSet.insertBefore(tip(row('Special rules', el('span', '', p.kind)), 'Special rules'), status.parentElement);

  // 3D model: what it uses now, the check, then one block per way to give it a body (the easiest first)
  const mb = meshBlocks('monster', cid);
  tModel.appendChild(currentModel(p.mesh));
  const rigged = [button('Import rigged model...', async () => {
    const f = await kit.pick('file', 'Model with a skeleton for ' + cid, '', [['Rigged model', ['*.glb', '*.gltf', '*.dae', '*.GLB', '*.GLTF', '*.DAE']]]);
    if (f) mb.run('rigged', { cid, path: f });
  })];
  if (p.mesh.rigged) rigged.push(button('Adjust bones...', () => mb.run('rigged', { cid, adjust: true })));
  tModel.append(
    mb.check,
    block('Replace with an already rigged model',
      'Formats: .glb, .gltf or .dae, with bones and weights (e.g. a model from another game or a 3D site). Its bones are fitted ' +
      'to the base type\'s, so it plays the base\'s animations. After the import, [Adjust bones...] reopens the bones page to fix ' +
      'the fit (e.g. a foot or a hand turned the wrong way, an open hand that should be a fist) and imports it again.',
      rigged, p.mesh.rigged ? 'Imported from ' + p.mesh.rigged : null),
    mb.blender,
    block('Import an entirely new character',
      'A .glb with its own armature, a weighted mesh and actions named by animation id (e.g. a090 = idle). The hardest way, ' +
      'but the one for a body that is not a reshaped base: its own skeleton and animations made for it. The fight data ' +
      '(which animation each attack plays, its damage, reach and timing) stays the base type\'s; to change it, [Extract stats] ' +
      'in the Stats & sounds tab gives this monster its own .CHR (the guide MODS.md, "Mesh and animations from a 3D editor", ' +
      'tells how to edit its move table).',
      [button('Import character with animations...', async () => {
        const start = (await kit.api('do/rig_start', { cid })).path;
        const f = await kit.pick('file', 'Character of ' + cid + ': .glb with armature, mesh and actions', start, [['glTF binary', ['*.glb', '*.GLB']]]);
        if (f) mb.run('rig_import', { cid, path: f });
      })]),
    block('Game model files',
      'Takes a model already in the game\'s format, made elsewhere (another mod): .NGP and .PTR, and the .TEX/.RTX if it has ' +
      'them, copied to this monster\'s files.',
      [button('Import model files...', async () => {
        const ps = await kit.pick('files', 'Model of ' + cid + ': .NGP and .PTR (and .TEX/.RTX, optional)', '', [['Model files', ['*.NGP', '*.PTR', '*.TEX', '*.RTX', '*.ngp', '*.ptr', '*.tex', '*.rtx']]]);
        if (ps && ps.length) run('monster_model', { cid, paths: ps });
      })]));

  // Stats & sounds
  tStats.append(
    block('Stats',
      'Extracts the base type\'s .CHR (fight numbers and move list) and .AIC (how the CPU fights) to this monster\'s files. ' +
      'The edited ones go in with [Build mod], for this monster only.',
      [button('Extract stats', () => run('monster_stats', { cid }))], 'Now: ' + p.stats),
    block('Sounds',
      'Extracts the base type\'s sound bank as WAVs to this monster\'s files. The replaced ones make its own bank with [Build mod].',
      [button('Extract sounds', () => run('monster_sounds', { cid }))], 'Now: ' + p.sounds));
}

export function initMonsters() {
  const tools = $('workTools');
  const bNew = el('button', 'needmod', '+ New monster');
  bNew.title = 'A fighter of its own, on a base monster';
  kit.actions.newMonster = () => monsterDialog('New custom monster');
  bNew.onclick = () => monsterDialog('New custom monster');
  tools.appendChild(bNew);
  kit.work.panels.monster = monsterPanel;
  kit.work.menus.push((ctx, add, sep) => {
    const g = ctx.group;
    if (g && g.kind === 'monster') {
      const cid = g.key.slice(9);
      add('New skin for this monster...', () => skinDialog('New skin', null, cid));
      add('Duplicate monster...', () => kit.dialog({
        title: 'Duplicate monster', fields: [{ key: 'new', label: 'Id of the copy of ' + cid + ' (lowercase letters, digits and _)', value: cid + '_copy' }],
        ok: async v => { const r = await kit.api('do/monster_duplicate', { cid, new: v.new }); if (r.error) return r.error; kit.workFocus = r.focus; return null; },
      }));
      add('Delete monster', async () => {
        if (await kit.confirm('Delete monster', 'Delete custom monster ' + cid + '? Its folder in the mod and its files in the work folder are deleted, edited or not.', 'Delete'))
          kit.run('monster_delete', { cid });
      });
      sep();
    } else if (g && g.custom_base) {
      add('Turn into custom monster...', () => turnIntoMonster(g));
      sep();
    }
  });
}
