// The Fight HUD dialog: the fight HUD pictures the stage carries (tools/lvlhud.py), each replaced by a PNG in the mod
// (<mod>/LVL/<stage>_t<id>.png, MODS.md "Textures as PNG"). Server: GET /api/hud, POST /api/hud/replace and /api/hud/remove.
// openHud(ctx) opens it (it builds itself the first time). ctx, from app.js:
//   stage, title (the stage's title), titleOf(name), mod, api(url, body), msg(text, {level}), ask(head, text, buttons, then, o),
//   mods() (the mod names), newMod (the "New mod…" value), chooseMod(v) -> Promise(mod in use),
//   game() -> {running, dirty}, reload() (Reload in game).
// The CSS is in style.css (#hudDlg).

let dlg = null;
let ctx = null;

function el(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') e.className = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (v !== false && v != null) e.setAttribute(k, v === true ? '' : v);
  }
  for (const k of kids) if (k != null) e.append(k);
  return e;
}
const icon = (name, cls = '') => el('i', { class: ('ic ' + cls).trim(), style: `--ic:url(vendor/mdi/${name}.svg)` });
const q = (sel) => dlg.querySelector(sel);

function build() {
  dlg = el('dialog', { id: 'hudDlg', class: 'dlg', 'aria-labelledby': 'hudTitle' });
  dlg.innerHTML = `<form method="dialog">
    <div class="dlg-head"><h2 id="hudTitle"></h2>
      <button type="button" data-close class="icon ghost" title="Close (Esc)" aria-label="Close"><i class="ic" style="--ic:url(vendor/mdi/close.svg)"></i></button></div>
    <div class="dlg-body">
      <div class="callout info"><i class="ic" style="--ic:url(vendor/mdi/information-outline.svg)"></i><span>Changes are written to your
        mod right away; Undo does not reach them. A picture can be any size with the same proportions, and 255 alpha is opaque.</span></div>
      <div class="callout warn hud-nomod" hidden><i class="ic" style="--ic:url(vendor/mdi/alert-outline.svg)"></i>
        <span>Choose or create a mod to replace HUD pictures.</span><select class="hud-mod" aria-label="Mod"></select></div>
      <div class="callout warn hud-b" hidden><i class="ic" style="--ic:url(vendor/mdi/alert-outline.svg)"></i><span></span></div>
      <div class="hud-list"></div>
    </div>
    <div class="dlg-foot"><span class="hud-game muted"></span><button type="button" class="hud-reload" hidden><i class="ic"
      style="--ic:url(vendor/mdi/refresh.svg)"></i>Reload in game</button><button value="close" class="primary">Close</button></div>
  </form>`;
  document.body.append(dlg);
  dlg.querySelector('[data-close]').addEventListener('click', () => dlg.close('cancel'));
  q('.hud-reload').addEventListener('click', () => ctx.reload());
  q('.hud-mod').addEventListener('change', async (e) => {
    const v = e.target.value;
    e.target.blur();
    if (!v) return;
    ctx.mod = (await ctx.chooseMod(v)) || '';
    await refresh();
  });
}

const stamp = () => Date.now().toString(36);
const imgUrl = (src, png) => `/api/hudimg?src=${src}&stage=${encodeURIComponent(ctx.stage)}&mod=${encodeURIComponent(ctx.mod || '')}&png=${encodeURIComponent(png)}&t=${stamp()}`;
const say = (text, level = 'info') => ctx.msg(text, { level, toast: true, open: false });

function thumb(url, cap, empty) {
  const box = url ? el('img', { class: 'hud-thumb', src: url, alt: cap, draggable: 'false' })
    : el('div', { class: 'hud-thumb none' }, empty);
  return el('figure', { class: 'hud-fig' }, box, el('figcaption', {}, cap));
}

// A picked file as PNG bytes (base64) and its size: a PNG goes as it is (exact pixels and alpha), the rest through a canvas
async function asPng(file) {
  const bmp = await createImageBitmap(file);
  const size = { w: bmp.width, h: bmp.height };
  let blob = file;
  if (file.type !== 'image/png') {
    const c = document.createElement('canvas');
    c.width = size.w; c.height = size.h;
    c.getContext('2d').drawImage(bmp, 0, 0);
    blob = await new Promise((res) => c.toBlob(res, 'image/png'));
  }
  bmp.close?.();
  const buf = new Uint8Array(await blob.arrayBuffer());
  let bin = '';
  for (let i = 0; i < buf.length; i += 0x8000) bin += String.fromCharCode(...buf.subarray(i, i + 0x8000));
  return { data: btoa(bin), ...size };
}

function pick() {
  return new Promise((res) => {
    const inp = el('input', { type: 'file', accept: 'image/*' });
    inp.addEventListener('change', () => res(inp.files[0] || null));
    inp.click();
  });
}

const psmText = (p) => ({ T8: '8-bit', T8H: '8-bit', T4: '4-bit', T4HL: '4-bit', T4HH: '4-bit', CT32: '32-bit', CT24: '24-bit',
                          CT16: '16-bit', CT16S: '16-bit' }[p] || p);

function row(r) {
  const every = el('input', { type: 'checkbox' });
  const noMod = !ctx.mod;
  const replaceBtn = el('button', { type: 'button', onclick: () => replace(r, every.checked), disabled: noMod,
                                    title: noMod ? 'Choose or create a mod to replace HUD pictures' : 'Pick a PNG for this picture (or drop one on the row)' },
                        icon('image-edit-outline'), 'Replace…');
  const acts = el('div', { class: 'hud-acts' }, replaceBtn,
    el('button', { type: 'button', class: 'ghost', title: 'Save the picture of the disc as a PNG, to edit it', onclick: () => {
      const a = el('a', { href: imgUrl('disc', r.png), download: r.png });
      document.body.append(a); a.click(); a.remove();
    } }, icon('download-outline'), 'Save original as PNG'),
    r.mod ? el('button', { type: 'button', class: 'ghost danger', title: 'Take the PNG out of your mod (the disc\'s picture comes back)',
                           onclick: () => remove(r, every.checked) }, icon('delete-outline'), 'Remove') : null);
  const everyLbl = r.every > 1 ? el('label', { class: 'hud-every', title: r.same ? 'This picture is the same in every stage'
    : `Every stage with this stage's HUD layout` }, every, `Apply to all ${r.every} stages with this picture`) : null;
  const li = el('div', { class: 'hud-row' + (r.mod ? ' on' : '') },
    thumb(r.disc ? imgUrl('disc', r.png) : null, 'Original', 'none'),
    icon('arrow-right', 'hud-arrow'),
    thumb(r.mod ? imgUrl('mod', r.png) : null, 'In your mod', 'Not replaced'),
    el('div', { class: 'hud-info' },
      el('div', { class: 'hud-name' }, el('span', {}, r.label), r.mod ? el('span', { class: 'badge' }, 'Replaced') : null),
      el('div', { class: 'hud-meta num' }, `${r.w}×${r.h} · ${psmText(r.psm)} · ${r.file === 'rtx' ? '.RTX' : '.TEX'}` +
        (r.note ? ` · ${r.note}` : '')),
      acts, everyLbl));
  // drop a picture on the row: the same as Replace…
  li.addEventListener('dragover', (e) => { if (ctx.mod && e.dataTransfer.types.includes('Files')) { e.preventDefault(); li.classList.add('drop'); } });
  li.addEventListener('dragleave', () => li.classList.remove('drop'));
  li.addEventListener('drop', (e) => {
    e.preventDefault();
    li.classList.remove('drop');
    const f = e.dataTransfer.files[0];
    if (f) replace(r, every.checked, f);
  });
  return li;
}

function showGame() {
  const g = ctx.game();
  q('.hud-reload').hidden = !g.running;
  q('.hud-reload').title = g.dirty ? 'Load this stage again in the game (it also saves your unsaved stage changes)'
    : 'Load this stage again in the game, with the pictures of your mod';
  q('.hud-game').textContent = g.running ? '' : 'The pictures show in the next fight on this stage.';
}

async function refresh() {
  const url = `/api/hud?stage=${encodeURIComponent(ctx.stage)}&mod=${encodeURIComponent(ctx.mod || '')}`;
  ctx.data = await ctx.api(url);
  q('#hudTitle').textContent = `Fight HUD and sky · ${ctx.title}`;
  // no mod: the mods to pick from, right here
  const nm = q('.hud-nomod');
  nm.hidden = !!ctx.mod;
  if (!ctx.mod) {
    const sel = q('.hud-mod');
    sel.replaceChildren(el('option', { value: '' }, 'Choose a mod…'),
                        ...ctx.mods().map((m) => el('option', { value: m }, m)), el('option', { value: ctx.newMod }, 'New mod…'));
    sel.value = '';
  }
  const b = q('.hud-b');
  b.hidden = ctx.data.version !== 'B';
  if (!b.hidden) {
    const others = Object.entries(ctx.data.versions || {}).filter(([s, v]) => v === 'B' && s !== ctx.stage).map(([s]) => ctx.titleOf(s));
    b.querySelector('span').textContent = 'This stage uses a different HUD layout' + (others.length ? ` (like ${others.join(' and ')})` : '') +
      '. Pictures made for other stages will not line up here.';
  }
  q('.hud-list').replaceChildren(...ctx.data.rows.map(row));
  showGame();
}

async function replace(r, every, file = null) {
  if (!ctx.mod) { say('Choose or create a mod to replace HUD pictures.', 'warn'); return; }
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
    const res = await ctx.api('/api/hud/replace', { stage: ctx.stage, mod: ctx.mod, role: r.role, png: r.png, data: img.data, every });
    ctx.msg(res.lines, { level: 'muted' });
    say(`${r.label}: replaced in mod "${ctx.mod}"` + (every && r.every > 1 ? ` on ${r.every} stages` : ''), 'ok');
  } catch (e) { say(`Couldn't replace ${r.label}: ${e.message}`, 'err'); }
  await refresh();
  if (ctx.changed) ctx.changed();      // the view draws the mod's picture
}

async function remove(r, every) {
  const go = await new Promise((res) => ctx.ask("Back to the disc's picture?",
    `This deletes your PNG for ${r.label} from mod "${ctx.mod}"` + (every && r.every > 1 ? ` on ${r.every} stages` : '') + ". It can't be undone.",
    [['cancel', 'Cancel'], ['go', 'Delete the PNG', true]], (v) => res(v === 'go'), { icon: 'delete-outline' }));
  if (!go) return;
  try {
    const res = await ctx.api('/api/hud/remove', { stage: ctx.stage, mod: ctx.mod, role: r.role, png: r.png, every });
    ctx.msg(res.lines, { level: 'muted' });
    say(`${r.label}: the disc's picture is back`, 'ok');
  } catch (e) { say(`Couldn't remove ${r.label}: ${e.message}`, 'err'); }
  await refresh();
  if (ctx.changed) ctx.changed();
}

export async function openHud(opts) {
  if (!dlg) build();
  ctx = { ...opts };
  try { await refresh(); } catch (e) { ctx.msg(`Couldn't open the fight HUD: ${e.message}`, { level: 'err' }); return; }
  dlg.showModal();
  q('.dlg-foot .primary').focus();
}
// the helpers the Textures dialog (tex.js) shares: building elements, a picked image as PNG bytes, the file picker
export { el, icon, asPng, pick, psmText };
// app.js calls it when the game starts or stops while the dialog is open
export function hudGameChanged() { if (dlg && dlg.open && ctx) showGame(); }
