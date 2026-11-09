// The Textures dialog: every picture of the stage's .TEX and .RTX (not only the HUD's), each replaced by a PNG in the mod
// (<mod>/LVL/<stage>_t<id>.png, MODS.md "Textures as PNG"). Server: GET /api/texs and /api/hudimg, POST /api/tex/replace and /remove.
// openTex(ctx) opens it (it builds itself the first time). ctx, from app.js: the same as openHud's (stage, title, mod, api, msg, ask,
// mods(), newMod, chooseMod(v), game(), reload()) and scene() -> the texIds the stage's objects draw with.
// The CSS is in style.css (#texDlg).
import { el, icon, asPng, pick, psmText } from './hud.js';

let dlg = null;
let ctx = null;
let rows = [];
const stamps = new Map();          // png -> a value that changes when the mod's picture does, so the page asks for it again
const filter = { text: '', cat: 'scene' };
const q = (sel) => dlg.querySelector(sel);

const CATS = [
  { id: 'scene', label: 'In the scene', tip: 'Pictures the stage\'s buildings, ground and props draw with' },
  { id: 'hud', label: 'HUD and sky', tip: 'The fight HUD, its font and the sky (also in the Fight HUD dialog)' },
  { id: 'other', label: 'Other', tip: 'Particles, effects and everything the scene view does not draw' },
  { id: 'replaced', label: 'Replaced', tip: 'Pictures your mod replaces' },
  { id: 'all', label: 'All', tip: 'Every picture of the stage' },
];
let sceneIds = new Set();
const inCat = (r, cat) => cat === 'all' || (cat === 'replaced' ? r.mod : cat === 'hud' ? r.roles.length > 0
  : cat === 'scene' ? sceneIds.has(r.tex) : !r.roles.length && !sceneIds.has(r.tex));
const idText = (r) => `${r.file === 'rtx' ? 'rtx ' : ''}t${String(r.tex).padStart(4, '0')}`;

function build() {
  dlg = el('dialog', { id: 'texDlg', class: 'dlg', 'aria-labelledby': 'texTitle' });
  dlg.innerHTML = `<form method="dialog">
    <div class="dlg-head"><h2 id="texTitle"></h2>
      <button type="button" data-close class="icon ghost" title="Close (Esc)" aria-label="Close"><i class="ic" style="--ic:url(vendor/mdi/close.svg)"></i></button></div>
    <div class="dlg-body">
      <div class="callout info"><i class="ic" style="--ic:url(vendor/mdi/information-outline.svg)"></i><span>Changes are written to your
        mod right away; Undo does not reach them. A picture can be any size with the same proportions, and 255 alpha is opaque. The game
        draws the new picture on every object that uses the old one.</span></div>
      <div class="callout warn tx-nomod" hidden><i class="ic" style="--ic:url(vendor/mdi/alert-outline.svg)"></i>
        <span>Choose or create a mod to replace pictures.</span><select class="tx-mod" aria-label="Mod"></select></div>
      <div class="tx-bar">
        <div class="field search"><i class="pre ic" style="--ic:url(vendor/mdi/magnify.svg)"></i>
          <input class="tx-find" placeholder="Number or size (12, 64x64)" spellcheck="false" autocomplete="off" aria-label="Search the pictures"></div>
        <div class="seg tx-cats" role="radiogroup" aria-label="Which pictures"></div>
      </div>
      <div class="tx-list"></div>
      <div class="tx-none muted" hidden></div>
    </div>
    <div class="dlg-foot"><span class="tx-game muted"></span><button type="button" class="tx-reload" hidden><i class="ic"
      style="--ic:url(vendor/mdi/refresh.svg)"></i>Reload in game</button><button value="close" class="primary">Close</button></div>
  </form>`;
  document.body.append(dlg);
  q('[data-close]').addEventListener('click', () => dlg.close('cancel'));
  q('.tx-reload').addEventListener('click', () => ctx.reload());
  q('.tx-mod').addEventListener('change', async (e) => {
    const v = e.target.value;
    e.target.blur();
    if (!v) return;
    ctx.mod = (await ctx.chooseMod(v)) || '';
    await refresh();
  });
  q('.tx-find').addEventListener('input', (e) => { filter.text = e.target.value; render(); });
  q('.tx-cats').addEventListener('change', (e) => { filter.cat = e.target.value; render(); q('.dlg-body').scrollTop = 0; });
}

const say = (text, level = 'info') => ctx.msg(text, { level, toast: true, open: false });
const imgUrl = (src, r) => `/api/hudimg?src=${src}&stage=${encodeURIComponent(ctx.stage)}&mod=${encodeURIComponent(ctx.mod || '')}` +
  `&png=${encodeURIComponent(r.png)}&t=${stamps.get(r.png) || 0}`;

function matches(r) {
  const t = filter.text.trim().toLowerCase().replace(/^t/, '');
  if (!t) return true;
  const m = /^(\d+)\s*[x×]\s*(\d+)$/.exec(t);
  if (m) return r.w === +m[1] && r.h === +m[2];
  return /^\d+$/.test(t) ? String(r.tex).includes(t) : `${r.w}x${r.h} ${r.psm} ${r.file}`.toLowerCase().includes(t);
}

function card(r) {
  const noMod = !ctx.mod;
  const pic = r.mod ? imgUrl('mod', r) : r.disc ? imgUrl('disc', r) : null;
  const small = Math.max(r.w, r.h) <= 64;
  const img = pic ? el('img', { class: 'tx-img' + (small ? ' px' : ''), src: pic, alt: idText(r), loading: 'lazy', draggable: 'false' })
    : el('div', { class: 'tx-img none' }, 'No picture');
  const badges = el('div', { class: 'tx-badges' },
    r.mod ? el('span', { class: 'badge' }, 'Replaced') : null,
    r.roles.length ? el('span', { class: 'badge hud', title: 'Used by the fight HUD or the sky' }, r.roles.includes('sky') && r.roles.length === 1 ? 'Sky' : 'HUD') : null,
    r.grey && !r.mod ? el('span', { class: 'badge grey', title: 'The editor found no palette for it, so the original shows in grey. Your picture shows in its own colors in the game.' }, 'No palette') : null);
  const acts = el('div', { class: 'tx-acts' },
    el('button', { type: 'button', class: 'icon', disabled: noMod, 'aria-label': 'Replace',
                   title: noMod ? 'Choose or create a mod to replace pictures' : 'Replace with a PNG (or drop one on the card)',
                   onclick: () => replace(r) }, icon('image-plus-outline')),
    el('button', { type: 'button', class: 'icon ghost', 'aria-label': 'Save original as PNG', disabled: !r.disc,
                   title: 'Save the disc\'s picture as a PNG, to edit it' + (r.grey ? ' (in grey: no palette found)' : ''), onclick: () => {
                     const a = el('a', { href: imgUrl('disc', r), download: r.png });
                     document.body.append(a); a.click(); a.remove();
                   } }, icon('download-outline')),
    r.mod ? el('button', { type: 'button', class: 'icon ghost danger', 'aria-label': 'Remove',
                           title: 'Take the PNG out of your mod (the disc\'s picture comes back)', onclick: () => remove(r) },
               icon('delete-outline')) : null);
  const c = el('div', { class: 'tx-card' + (r.mod ? ' on' : ''), title: `${r.png}\n${r.w}×${r.h} · ${psmText(r.psm)} · ${r.file === 'rtx' ? '.RTX' : '.TEX'}` },
    el('div', { class: 'tx-pic' }, img, badges),
    el('div', { class: 'tx-name' }, el('b', {}, idText(r)), el('span', { class: 'tx-meta num' }, `${r.w}×${r.h} · ${psmText(r.psm)}`)),
    acts);
  c.addEventListener('dragover', (e) => { if (ctx.mod && e.dataTransfer.types.includes('Files')) { e.preventDefault(); c.classList.add('drop'); } });
  c.addEventListener('dragleave', () => c.classList.remove('drop'));
  c.addEventListener('drop', (e) => {
    e.preventDefault();
    c.classList.remove('drop');
    const f = e.dataTransfer.files[0];
    if (f) replace(r, f);
  });
  return c;
}

function render() {
  const body = q('.dlg-body'), top = body.scrollTop;
  const counts = Object.fromEntries(CATS.map((c) => [c.id, rows.filter((r) => matches(r) && inCat(r, c.id)).length]));
  q('.tx-cats').replaceChildren(...CATS.map((c) => el('label', { title: c.tip },
    el('input', { type: 'radio', name: 'texCat', value: c.id, ...(filter.cat === c.id ? { checked: true } : {}) }), `${c.label} ${counts[c.id]}`)));
  const shown = rows.filter((r) => matches(r) && inCat(r, filter.cat));
  q('.tx-list').replaceChildren(...shown.map(card));
  const none = q('.tx-none');
  none.hidden = !!shown.length;
  none.textContent = filter.cat === 'scene' && !sceneIds.size ? 'The scene draws nothing yet.' : 'No picture matches.';
  body.scrollTop = top;
}

function showGame() {
  const g = ctx.game();
  q('.tx-reload').hidden = !g.running;
  q('.tx-reload').title = g.dirty ? 'Load this stage again in the game (it also saves your unsaved stage changes)'
    : 'Load this stage again in the game, with the pictures of your mod';
  q('.tx-game').textContent = g.running ? '' : 'The pictures show in the next fight on this stage.';
}

async function refresh() {
  const data = await ctx.api(`/api/texs?stage=${encodeURIComponent(ctx.stage)}&mod=${encodeURIComponent(ctx.mod || '')}`);
  rows = data.rows;
  sceneIds = new Set(ctx.scene());
  q('#texTitle').textContent = `Textures · ${ctx.title}`;
  const nm = q('.tx-nomod');
  nm.hidden = !!ctx.mod;
  if (!ctx.mod) {
    const sel = q('.tx-mod');
    sel.replaceChildren(el('option', { value: '' }, 'Choose a mod…'),
                        ...ctx.mods().map((m) => el('option', { value: m }, m)), el('option', { value: ctx.newMod }, 'New mod…'));
    sel.value = '';
  }
  render();
  showGame();
}

async function replace(r, file = null) {
  if (!ctx.mod) { say('Choose or create a mod to replace pictures.', 'warn'); return; }
  const f = file || await pick();
  if (!f) return;
  let img;
  try { img = await asPng(f); } catch (e) { say(`${f.name} is not a picture the editor can read.`, 'err'); return; }
  const want = r.w / r.h, got = img.w / img.h;
  if (Math.abs(want - got) > 0.01 * want) {
    const go = await new Promise((res) => ctx.ask('Different proportions', `${f.name} is ${img.w}×${img.h}; the game's picture is ` +
      `${r.w}×${r.h}. With other proportions it comes out stretched.`, [['cancel', 'Cancel'], ['use', 'Use it anyway', true]],
      (v) => res(v === 'use'), { icon: 'image-edit-outline' }));
    if (!go) return;
  }
  try {
    const res = await ctx.api('/api/tex/replace', { stage: ctx.stage, mod: ctx.mod, png: r.png, data: img.data });
    ctx.msg(res.lines, { level: 'muted' });
    stamps.set(r.png, Date.now().toString(36));
    say(`${idText(r)}: replaced in mod "${ctx.mod}"`, 'ok');
  } catch (e) { say(`Couldn't replace ${idText(r)}: ${e.message}`, 'err'); }
  await refresh();
}

async function remove(r) {
  try {
    const res = await ctx.api('/api/tex/remove', { stage: ctx.stage, mod: ctx.mod, png: r.png });
    ctx.msg(res.lines, { level: 'muted' });
    stamps.set(r.png, Date.now().toString(36));
    say(`${idText(r)}: the disc's picture is back`, 'ok');
  } catch (e) { say(`Couldn't remove ${idText(r)}: ${e.message}`, 'err'); }
  await refresh();
}

export async function openTex(opts) {
  if (!dlg) build();
  ctx = { ...opts };
  try { await refresh(); } catch (e) { ctx.msg(`Couldn't open the textures: ${e.message}`, { level: 'err' }); return; }
  dlg.showModal();
  q('.tx-find').focus();
}
// app.js calls it when the game starts or stops while the dialog is open
export function texGameChanged() { if (dlg && dlg.open && ctx) showGame(); }
