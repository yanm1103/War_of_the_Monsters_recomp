// Skins (MODS.md, "Skins"): New skin..., Turn into skin... (an extracted costume's PNGs become a skin), the skin's panel in
// place of the preview (the model with its textures, then two tabs: Settings, with the name, base costume with the disc's
// portraits and CPUs; 3D model), and Duplicate / Delete in the right-click menu.
import { meshBlocks, currentModel } from './mesh.js';
import { panelFrame, tabs, block, button, autosave } from './panel.js';
import { pic } from './disc.js';

const $ = id => document.getElementById(id);
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };

// radio buttons 0..3 with each costume's portrait (only the ones the disc has, or enabled); extra: [text, value, picture url]
// at the end
export function costumeRadios(name, prefix, value, enabled, extra) {
  const box = el('div', 'costumes');
  const ks = [0, 1, 2, 3];
  for (const k of ks) {
    const l = el('label', 'costume');
    const r = el('input');
    r.type = 'radio';
    r.name = name;
    r.value = k;
    r.checked = value === k;
    r.disabled = !enabled.includes(k);
    const face = prefix && enabled.includes(k) ? pic('api/portrait?prefix=' + prefix + '&k=' + k, 'files', 'big') : el('span', 'pic big');
    l.append(face, r, document.createTextNode(' ' + k));
    box.appendChild(l);
  }
  if (extra) {
    const l = el('label', 'costume');
    const r = el('input');
    r.type = 'radio';
    r.name = name;
    r.value = extra[1];
    r.checked = value === extra[1];
    l.append(extra[2] ? pic(extra[2], 'files', 'big') : el('span', 'pic big'), r, document.createTextNode(' ' + extra[0]));
    box.appendChild(l);
  }
  box.value = () => { const c = box.querySelector('input:checked'); return c ? parseInt(c.value, 10) : null; };
  return box;
}

function row(label, ...kids) {
  const r = el('div', 'frow');
  r.append(el('span', 'flabel', label), ...kids);
  return r;
}

// the New skin / Turn into skin dialog. lock: {key, prefix, costume, group} (monster and base fixed); preset: a custom's id
export function skinDialog(title, lock, preset) {
  return kit.modal(async (box, close) => {
    box.appendChild(el('h3', '', title));
    const first = await kit.api('do/skin_form', {});
    const choices = first.choices;
    // the monsters as a list (the game's, then the custom ones the mods have), and a last entry to type the id of a custom
    // monster the kit does not find
    const OTHER = '\u0000other';
    const pickMon = el('select');
    const og1 = el('optgroup'), og2 = el('optgroup');
    og1.label = 'Monsters of the game';
    og2.label = 'Custom monsters in the mods';
    for (const c of choices) { const o = el('option', '', c.label); o.value = c.label; (c.game ? og1 : og2).appendChild(o); }
    pickMon.appendChild(og1);
    if (og2.children.length) pickMon.appendChild(og2);
    const oOther = el('option', '', 'Another custom monster (type its id)...');
    oOther.value = OTHER;
    pickMon.appendChild(oOther);
    const otherId = el('input');
    otherId.spellcheck = false;
    otherId.placeholder = 'id of the custom monster (lowercase letters, digits and _)';
    otherId.hidden = true;
    const mon = { get value() { return pickMon.value === OTHER ? otherId.value : pickMon.value; } };
    const dl = el('span');
    dl.append(otherId);
    pickMon.disabled = !!lock;
    const hint = el('div', 'dim');
    const baseBox = el('div');
    const name = el('input'), sid = el('input');
    name.spellcheck = sid.spellcheck = false;
    name.id = 'skinName';
    sid.id = 'skinId';
    const ai = el('input');
    ai.type = 'checkbox';
    const err = el('div', 'msg');
    const ok = el('button', 'primary', 'OK');
    ok.type = 'submit';
    const cancel = el('button', '', 'Cancel');
    cancel.type = 'button';
    cancel.onclick = () => close(null);
    const bs = el('div', 'buttons');
    bs.append(cancel, ok);
    const aiL = el('label', 'check');
    aiL.append(ai, document.createTextNode(' CPUs may pick this skin'));
    const monBox = el('div', 'inline');
    monBox.append(pickMon, dl);
    box.append(row('Monster', monBox), row('', hint), row('Base costume', baseBox), row('Name', name), row('Id', sid),
      row('', el('div', 'dim', 'Folder name and what the save keeps: lowercase letters, digits and _')), row('', aiL), err, bs);
    const st = { res: null, prefix: '?', autoName: true, autoId: true, radios: null, n: 0 };
    sid.value = first.free_id || 'skin1';
    const defaultName = () => ((sid.value + ' ' + (st.res ? st.res.key : '')).trim()).toUpperCase();
    async function check(what) {
      const n = ++st.n;
      const r = await kit.api('do/skin_form', { monster: mon.value, sid: sid.value, name: name.value });
      if (n !== st.n) return;
      st.res = r.res;
      hint.textContent = r.hint || '';
      const prefix = r.res ? r.res.prefix : null;
      if (prefix !== st.prefix) {   // another monster: its costumes and portraits
        st.prefix = prefix;
        baseBox.innerHTML = '';
        st.radios = null;
        if (r.res && r.res.kind !== 'unknown') {
          const v = lock ? lock.costume : r.res.costume;
          st.radios = costumeRadios('skinBase', prefix, v, lock ? [lock.costume] : r.costumes);
          baseBox.appendChild(st.radios);
        } else if (r.res) baseBox.appendChild(el('div', 'dim', '(none: the game uses the custom monster\'s costume)'));
      }
      if (what === 'monster' && st.autoName) name.value = defaultName();
      if (what === 'name' && st.autoId && r.slug !== undefined) { sid.value = r.slug; return check(); }
      if (what === 'id' && st.autoName) name.value = defaultName();
      const r2 = what ? await kit.api('do/skin_form', { monster: mon.value, sid: sid.value, name: name.value }) : r;
      if (n !== st.n) return;
      err.textContent = r2.error || '';
      ok.disabled = !!r2.error;
    }
    pickMon.addEventListener('change', () => {
      otherId.hidden = pickMon.value !== OTHER;
      if (!otherId.hidden) otherId.focus();
      check('monster');
    });
    otherId.addEventListener('input', () => check('monster'));
    name.addEventListener('input', () => { st.autoName = false; check('name'); });
    sid.addEventListener('input', () => { st.autoId = false; check('id'); });
    box.onsubmit = async e => {
      e.preventDefault();
      if (ok.disabled) return;
      const body = { monster: mon.value, sid: sid.value.trim(), name: name.value, ai: ai.checked,
                     base: st.radios ? st.radios.value() : null, from_group: lock ? lock.group : undefined };
      const r = await kit.api('do/skin_new', body);
      if (r.error) { err.textContent = r.error; return; }
      if (r.ok === false) { err.textContent = 'Still working on the previous task; try again when it finishes.'; return; }
      kit.workFocus = r.focus;
      close(body);
    };
    const setMon = (c, id) => {   // a choice of the list, or the "another" entry with that id
      if (c) pickMon.value = c.label;
      else { pickMon.value = OTHER; otherId.value = id; otherId.hidden = false; }
    };
    if (lock) setMon(choices.find(c => c.game && c.key === lock.key), lock.key);
    else if (preset) setMon(choices.find(c => !c.game && c.key === preset), preset);
    else if (choices.length) setMon(choices[0]);
    await check('monster');
    name.focus();
  });
}

async function turnIntoSkin(g) {
  const c = g.costume_info;
  const body = await skinDialog('Turn into skin', { key: c[0], prefix: c[1], costume: c[2], group: g.key });
  if (!body) return;
  kit.afterTask = async () => {   // the mod's files of that costume, built before, keep replacing the original one
    const r = await kit.api('do/packed', { group: g.key });
    if (r.files && r.files.length && await kit.confirm('Turn into skin', 'The mod already has ' + r.files.length + ' file(s) of ' + r.stem + ' in ' + r.where + ', built before; they still replace the original costume.\n\nDelete them from the mod? (the skin keeps its own copies)', 'Delete'))
      await kit.api('do/packed', { group: g.key, delete: true });
  };
}

async function skinPanel(g, body, label) {
  const sid = g.key.slice(6);
  label.textContent = '';
  const form = panelFrame(body, g.key, g);
  const p = await kit.api('do/skin_props', { sid });
  if (p.missing) { form.appendChild(el('div', 'dim', 'No skin.json in ' + p.missing)); return; }
  const d = p.data;
  const name = el('input');
  name.value = d.name || '';
  const b0 = Number.isInteger(d.base) && d.base >= 0 && d.base <= 3 ? d.base : -1;
  // a custom's skin: the last choice is "as the custom" (no base in skin.json: the custom's own costume), with its portrait
  const radios = costumeRadios('panelBase', p.prefix, p.game && b0 < 0 ? 0 : b0, p.costumes,
    p.game ? null : ['As ' + (p.custom_name || d.monster || 'the monster'), -1, p.custom_pic]);
  const baseHint = el('div', 'dim', p.own_model
    ? 'This skin has its own 3D model and textures: the base costume only gives the sounds.'
    : 'The disc costume whose model, textures and sounds this skin starts from; its PNGs go over them.');
  const baseBox = el('div');
  baseBox.append(radios, baseHint);
  const ai = el('input');
  ai.type = 'checkbox';
  ai.checked = !!d.ai;
  const aiL = el('label', 'check');
  aiL.append(ai, document.createTextNode(' CPUs may pick this skin'));
  const [tSet, tModel] = tabs(form, ['Settings', '3D model'], 'wotm.skinTab');
  const baseRow = row('Base costume', baseBox);
  baseRow.title = 'The disc costume the skin is made on: whatever its folder does not bring (model, textures) and its sounds.';
  let savedBase = radios.value();
  tSet.append(row('Monster', el('span', '', (d.monster || '?') + '  (' + p.kind + ')')), row('Name', name), baseRow, row('', aiL),
    row('', autosave([name, radios, ai], async () => {
      const r = await kit.api('do/skin_save', { sid, name: name.value, base: radios.value(), ai: ai.checked });
      if (r.error) return r.error;
      if (radios.value() !== savedBase && !p.own_model) kit.work.reload(['skins/' + sid]);   // another base: another model
      else kit.work.refreshList();
      savedBase = radios.value();
      return null;
    })));
  if (p.custom && p.custom_model && !p.own_model) {   // a custom with a model of its own, a skin without: the game shows the base's
    tSet.prepend(el('div', 'warnbox', 'This skin has no 3D model of its own, so in the game it shows the base type\'s disc ' +
      'costume, not ' + (p.custom_name || 'the custom monster') + '. Copy its model to start from it (the skin\'s PNGs are replaced).'));
    tSet.children[0].appendChild(button('Copy ' + (p.custom_name || 'its') + '\'s model', async () => {
      kit.workFocus = ['skins/' + sid];
      kit.run('skin_copy_model', { sid });
    }));
  }
  const mb = meshBlocks('skin', sid, !!p.custom);
  tModel.append(currentModel(p.mesh), mb.check);
  if (p.custom) {   // a skin of a custom monster can have a body of its own, as the custom can
    const rigged = [button('Import rigged model...', async () => {
      const f = await kit.pick('file', 'Model with a skeleton for skin ' + sid, '', [['Rigged model', ['*.glb', '*.gltf', '*.dae', '*.GLB', '*.GLTF', '*.DAE']]]);
      if (f) mb.run('rigged', { kind: 'skin', cid: sid, path: f });
    })];
    if (p.mesh.rigged) rigged.push(button('Adjust bones...', () => mb.run('rigged', { kind: 'skin', cid: sid, adjust: true })));
    tModel.append(
      block('Replace with an already rigged model',
        'Formats: .glb, .gltf or .dae, with bones and weights (e.g. a model from another game or a 3D site). Its bones are ' +
        'fitted to the base type\'s, so it plays the base\'s animations; only this skin uses the new body. After the import, ' +
        '[Adjust bones...] reopens the bones page to fix the fit and imports it again.',
        rigged, p.mesh.rigged ? 'Imported from ' + p.mesh.rigged : null),
      mb.blender,
      block('Import an entirely new character',
        'A .glb with its own armature, a weighted mesh and actions named by animation id (e.g. a090 = idle), for this skin ' +
        'only. The fight data stays the custom monster\'s.',
        [button('Import character with animations...', async () => {
          const start = (await kit.api('do/rig_start', { kind: 'skin', cid: sid })).path;
          const f = await kit.pick('file', 'Character of skin ' + sid + ': .glb with armature, mesh and actions', start, [['glTF binary', ['*.glb', '*.GLB']]]);
          if (f) mb.run('rig_import', { kind: 'skin', cid: sid, path: f });
        })]));
  } else tModel.append(mb.blender);
}

export function initSkins() {
  const tools = $('workTools');
  const bNew = el('button', 'needmod', '+ New skin');
  bNew.title = 'New colors and textures for a monster';
  kit.actions.newSkin = () => skinDialog('New skin');
  const bTurn = el('button', 'needmod', 'Turn into skin...');
  bNew.onclick = () => skinDialog('New skin');
  bTurn.onclick = () => {
    const g = kit.work.selectedGroup();
    if (g && g.costume) turnIntoSkin(g);
    else kit.say('Select a monster costume group in the list first (e.g. MON/SHOGUN3).\n', 'warn');
  };
  tools.append(bTurn, bNew);
  const turnState = g => { bTurn.disabled = !(g && g.costume) || !kit.state || !kit.state.mod; };
  kit.work.onSelect(turnState);
  kit.onState(() => turnState(kit.work.selectedGroup()));
  kit.work.panels.skin = skinPanel;
  kit.work.menus.push((ctx, add, sep) => {
    const g = ctx.group;
    if (g && g.kind === 'skin') {
      const sid = g.key.slice(6);
      add('Duplicate skin...', () => kit.dialog({
        title: 'Duplicate skin', fields: [{ key: 'new', label: 'Id of the copy of ' + sid + ' (lowercase letters, digits and _)', value: sid + '_copy' }],
        ok: async v => { const r = await kit.api('do/skin_duplicate', { sid, new: v.new }); if (r.error) return r.error; kit.workFocus = r.focus; return null; },
      }));
      add('Delete skin', async () => {
        if (await kit.confirm('Delete skin', 'Delete skin ' + sid + '? Its folder in the mod and its PNGs in the work folder are deleted, edited or not.', 'Delete'))
          kit.run('skin_delete', { sid });
      });
      sep();
    } else if (g && g.costume) {
      add('Turn into skin...', () => turnIntoSkin(g));
      sep();
    }
  });
  kit.onTaskDone(() => { const f = kit.afterTask; kit.afterTask = null; if (f) f(); });
}
