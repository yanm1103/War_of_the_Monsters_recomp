// Skins (MODS.md, "Skins"): New skin..., Turn into skin... (an extracted costume's PNGs become a skin), the skin's panel in
// place of the preview (the model with its textures, then two tabs: Settings, with the name, base costume with the disc's
// portraits and CPUs; 3D model), and Duplicate / Delete in the right-click menu.
import { meshBlocks } from './mesh.js';
import { panelFrame, tabs } from './panel.js';
import { pic } from './disc.js';

const $ = id => document.getElementById(id);
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };

// radio buttons 0..3 with each costume's portrait (only the ones the disc has, or enabled); extra: [text, value] at the end
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
    l.append(el('span', 'pic big'), r, document.createTextNode(' ' + extra[0]));
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
    const mon = el('input');
    mon.setAttribute('list', 'monChoices');
    mon.spellcheck = false;
    const dl = el('datalist');
    dl.id = 'monChoices';
    for (const c of choices) { const o = el('option'); o.value = c.label; dl.appendChild(o); }
    mon.disabled = !!lock;
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
    box.append(row('Monster', mon, dl), row('', hint), row('Base costume', baseBox), row('Name', name), row('Id', sid),
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
    mon.addEventListener('input', () => check('monster'));
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
    if (lock) mon.value = (choices.find(c => c.game && c.key === lock.key) || { label: lock.key }).label;
    else if (preset) mon.value = (choices.find(c => !c.game && c.key === preset) || { label: preset }).label;
    else mon.value = choices.length ? choices[0].label : '';
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
  label.textContent = 'Skin ' + sid + ': its PNGs are in the work folder (skins/' + sid + '); open the section to edit them. "Build mod" sends the edited ones to the mod\'s skins/' + sid + '.';
  const form = panelFrame(body, g.key, g);
  const p = await kit.api('do/skin_props', { sid });
  if (p.missing) { form.appendChild(el('div', 'dim', 'No skin.json in ' + p.missing)); return; }
  const d = p.data;
  const name = el('input');
  name.value = d.name || '';
  const b0 = Number.isInteger(d.base) && d.base >= 0 && d.base <= 3 ? d.base : -1;
  const radios = costumeRadios('panelBase', p.prefix, p.game && b0 < 0 ? 0 : b0, p.costumes,
    p.game ? null : ['monster.json' + (p.custom_costume !== null ? ' (' + p.custom_costume + ')' : ''), -1]);
  const ai = el('input');
  ai.type = 'checkbox';
  ai.checked = !!d.ai;
  const aiL = el('label', 'check');
  aiL.append(ai, document.createTextNode(' CPUs may pick this skin'));
  const save = el('button', 'primary', 'Save');
  save.onclick = async () => {
    const r = await kit.api('do/skin_save', { sid, name: name.value, base: radios.value(), ai: ai.checked });
    if (r.error) { kit.say(r.error + '\n', 'err'); return; }
    await kit.work.reload(['skins/' + sid]);
  };
  const [tSet, tModel] = tabs(form, ['Settings', '3D model'], 'wotm.skinTab');
  tSet.append(row('Monster', el('span', '', (d.monster || '?') + '  (' + p.kind + ')')), row('Name', name), row('Base', radios),
    row('', aiL), row('', save));
  const mb = meshBlocks('skin', sid);
  tModel.append(el('div', 'state', 'Now: ' + p.mesh.text), mb.blender, mb.check);
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
