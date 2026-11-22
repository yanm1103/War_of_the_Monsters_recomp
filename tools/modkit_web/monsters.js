// Custom monsters (MODS.md, "Custom monsters"): New monster..., Turn into custom monster... (an extracted costume becomes a
// custom with its own files), the monster's panel in place of the preview (the model in 3D, then three tabs: Settings, with
// the name, camera, narrator, rules, special, CPUs and HUD portrait; 3D model, with one block per way to give it a body;
// Stats & sounds), Duplicate / Delete.
import { meshBlocks } from './mesh.js';
import { panelFrame, tabs, block, el, button } from './panel.js';
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
  const box = el('div', 'inline');
  box.append(s, el('span', 'dim', ' distance '), n, el('span', 'dim', ' %'));
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
      row('Camera', cam.box), row('Narrator', narr), row('', el('div', 'dim', "narrator/<name>.mp3 said when it is picked; empty = the base's")));
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
  label.textContent = 'Custom monster ' + cid + ': its PNGs are in the work folder (monsters/' + cid + '/files); open the section to edit them. "Build mod" sends the edited ones to the mod, where they are its own look (only for it).';
  const form = panelFrame(body, g.key);
  const [L, p] = await Promise.all([getLists(), kit.api('do/monster_props', { cid })]);
  form.appendChild(el('h4', '', 'Custom monster ' + cid));
  if (p.missing) { form.appendChild(el('div', 'dim', 'No monster.json in ' + p.missing)); return; }
  const key = 'monsters/' + cid;
  const run = async (action, b) => { kit.workFocus = [key]; return kit.run(action, b); };
  const [tSet, tModel, tStats] = tabs(form, ['Settings', '3D model', 'Stats & sounds'], 'wotm.monsterTab');

  // Settings
  const name = el('input');
  name.value = p.data.name || '';
  const cam = cameraRow(L, p.camera_from, p.camera_scale);
  const narr = narratorInput(L, p.narrator);
  const kind = select([['', "(the base's)"], ...L.kinds.map(k => [k, k])], p.kind);
  const special = select([['', "(the base's own)"], ...L.specials.map(s => [s.base, s.label])], p.special);
  const ai = el('input'); ai.type = 'checkbox'; ai.checked = p.ai;
  const aiL = el('label', 'check'); aiL.append(ai, document.createTextNode(' CPUs may pick this monster'));
  const hud = el('div', 'inline');
  if (p.portrait_ok) { const im = el('img', 'hud'); im.src = 'api/hud?cid=' + encodeURIComponent(cid) + '&t=' + Date.now(); hud.appendChild(im); }
  else hud.appendChild(el('span', 'dim', p.portrait ? p.portrait + ' (not found)' : "the costume's"));
  hud.appendChild(button('Set...', async () => {
    const f = await kit.pick('file', 'HUD portrait of ' + cid + ' (2:1, e.g. 128x64 or 512x256)', '', IMG);
    if (f) run('portrait_set', { cid, path: f });
  }));
  if (p.portrait) hud.appendChild(button('Remove', async () => { await kit.api('do/portrait_remove', { cid }); kit.work.reload([key]); }));
  const save = button('Save', async () => {
    const r = await kit.api('do/monster_save', { cid, name: name.value, camera_from: cam.from.value, camera_scale: cam.scale.value,
                                                 narrator: narr.value, kind: kind.value, special: special.value, ai: ai.checked });
    if (r.error) { kit.say(r.error + '\n', 'err'); return; }
    kit.work.reload([key]);
  }, 'primary');
  const sb = el('div', 'btns');
  sb.append(save, button('New skin for it...', () => skinDialog('New skin', null, cid)));
  tSet.append(row('Base', el('span', '', p.base)), row('Name', name), row('Camera', cam.box), row('Narrator', narr), row('Rules', kind),
    row('Special', special), row('', aiL), row('HUD portrait', hud), row('Look', el('span', 'dim', p.look)), row('', sb));

  // 3D model: one block per way to give it a body
  const mb = meshBlocks('monster', cid);
  tModel.appendChild(el('div', 'state', 'Now: ' + p.mesh.text));
  const rigged = [button('Import rigged model...', async () => {
    const f = await kit.pick('file', 'Model with a skeleton for ' + cid, '', [['Rigged model', ['*.glb', '*.gltf', '*.dae', '*.GLB', '*.GLTF', '*.DAE']]]);
    if (f) mb.run('rigged', { cid, path: f });
  })];
  if (p.mesh.rigged) rigged.push(button('Adjust bones...', () => mb.run('rigged', { cid, adjust: true })));
  tModel.append(
    block('Downloaded model with a skeleton',
      'A .glb, .gltf or .dae that already has bones and weights: its bones are fitted to the base type\'s, so it plays the ' +
      'base\'s animations. Adjust bones... reopens that page to fix the fit (a foot, a hand) and imports again.',
      rigged, p.mesh.rigged ? 'Imported from ' + p.mesh.rigged : null),
    mb.blender,
    block('Own skeleton',
      'A .glb with its own armature, a weighted mesh and actions named by animation id (a090 = idle): a body with a ' +
      'skeleton and moves made for it.',
      [button('Import rig...', async () => {
        const start = (await kit.api('do/rig_start', { cid })).path;
        const f = await kit.pick('file', 'Rig of ' + cid + ': .glb with armature, mesh and actions', start, [['glTF binary', ['*.glb', '*.GLB']]]);
        if (f) mb.run('rig_import', { cid, path: f });
      })]),
    mb.check,
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
      'Extracts the base type\'s .CHR and .AIC files to this monster\'s files: the edited ones become its own (the fight ' +
      'numbers and the punch table in the .CHR, the CPU\'s AI in the .AIC).',
      [button('Extract stats', () => run('monster_stats', { cid }))], 'Now: ' + p.stats),
    block('Sounds',
      'Extracts the base type\'s sound bank as WAVs to this monster\'s files: on Build mod the replaced ones make its own bank.',
      [button('Extract sounds', () => run('monster_sounds', { cid }))], 'Now: ' + p.sounds));
}

export function initMonsters() {
  const tools = $('workTools');
  const bNew = el('button', 'needmod', 'New monster...');
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
