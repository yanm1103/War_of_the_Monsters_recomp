// HUD panel: the fight HUD textures the stage carries (tools/lvlhud.py), each replaced by a PNG in the mod
// (<mod>/LVL/<stage>_t<id>.png, MODS.md "Textures as PNG"). Server: GET /api/hud, POST /api/hud/replace and /api/hud/remove.
// openHud({stage, mod, api, log}) opens the dialog; it builds itself the first time.

let dlg = null;
let ctx = null;

const css = `
#hudDlg { width: 720px; max-width: calc(100vw - 32px); }
#hudDlg .hud-top { display: flex; align-items: baseline; gap: 10px; }
#hudDlg .hud-top .grow { flex: 1; }
#hudDlg .hud-warn { color: var(--warn); margin: 6px 0; }
#hudDlg .hud-list { max-height: 60vh; overflow: auto; margin: 8px 0; border-top: 1px solid var(--line); }
#hudDlg .hud-row { display: grid; grid-template-columns: 72px 72px 1fr; gap: 10px; align-items: center; padding: 8px 0;
  border-bottom: 1px solid var(--line); }
#hudDlg .hud-thumb { width: 72px; height: 72px; object-fit: contain; border: 1px solid var(--line); border-radius: 3px;
  background: repeating-conic-gradient(var(--checker-a) 0 25%, var(--checker-b) 0 50%) 0 0 / 12px 12px; }
#hudDlg .hud-thumb.none { display: flex; align-items: center; justify-content: center; color: var(--muted); font-size: 11px;
  background: var(--panel2); text-align: center; }
#hudDlg .hud-name { font-weight: 600; }
#hudDlg .hud-meta { color: var(--muted); font-size: 12px; }
#hudDlg .hud-acts { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 4px; align-items: center; }
#hudDlg .hud-acts label { display: inline-flex; gap: 4px; margin: 0; align-items: center; }
#hudDlg .hud-acts label input { display: inline; width: auto; margin: 0; }
`;

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

function build() {
  document.head.append(el('style', {}, css));
  dlg = el('dialog', { id: 'hudDlg' });
  dlg.innerHTML = `<form method="dialog">
    <div class="hud-top"><h2>Fight HUD</h2><span class="grow"></span><span class="hud-stage muted"></span></div>
    <p class="muted small">The stage's file carries the fight HUD. A replaced texture is a PNG in the mod, any size with the same
    proportions; 255 alpha is the game's opaque. It shows in the next fight on this stage (or after Reload in game).</p>
    <div class="hud-warn" hidden></div>
    <div class="hud-list"></div>
    <div class="row end"><button value="close" class="primary">Close</button></div>
  </form>`;
  document.body.append(dlg);
}

const stamp = () => Date.now().toString(36);
const imgUrl = (src, png) => `/api/hudimg?src=${src}&stage=${encodeURIComponent(ctx.stage)}&mod=${encodeURIComponent(ctx.mod || '')}&png=${encodeURIComponent(png)}&t=${stamp()}`;

function thumb(url, title) {
  if (!url) return el('div', { class: 'hud-thumb none' }, title);
  return el('img', { class: 'hud-thumb', src: url, title, alt: title });
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

function row(r) {
  const every = el('input', { type: 'checkbox' });
  const everyLbl = r.every > 1 ? el('label', { title: r.same ? 'This picture is the same in every stage' :
    `Every stage with HUD version ${ctx.data.version}` }, every, ` every stage with this picture (${r.every})`) : null;
  const acts = el('div', { class: 'hud-acts' },
    el('button', { type: 'button', onclick: () => replace(r, every.checked) }, 'Replace…'),
    el('button', { type: 'button', title: 'Save the disc picture as a PNG to edit', onclick: () => {
      const a = el('a', { href: imgUrl('disc', r.png), download: r.png });
      document.body.append(a); a.click(); a.remove();
    } }, 'Export'),
    r.mod ? el('button', { type: 'button', class: 'danger', onclick: () => remove(r, every.checked) }, 'Remove') : null,
    everyLbl);
  return el('div', { class: 'hud-row' },
    thumb(r.disc ? imgUrl('disc', r.png) : null, 'disc'),
    thumb(r.mod ? imgUrl('mod', r.png) : null, r.mod ? 'mod' : 'not replaced'),
    el('div', {},
      el('div', { class: 'hud-name' }, r.label),
      el('div', { class: 'hud-meta' }, `${r.png} · ${r.w}×${r.h} ${r.psm} · ${r.file === 'rtx' ? '.RTX' : '.TEX'}` +
        (r.note ? ` · ${r.note}` : '') + (r.mod ? ' · replaced by the mod' : '')),
      acts));
}

async function refresh() {
  const q = `/api/hud?stage=${encodeURIComponent(ctx.stage)}&mod=${encodeURIComponent(ctx.mod || '')}`;
  ctx.data = await ctx.api(q);
  dlg.querySelector('.hud-stage').textContent = `${ctx.stage} · HUD version ${ctx.data.version}`;
  const warn = dlg.querySelector('.hud-warn');
  warn.hidden = ctx.data.version !== 'B';
  warn.textContent = 'This stage has the other HUD version (B, as in CANYON1 and TOKYODM): its bar atlas, special icon, player ' +
    'number and font are drawn differently, so a picture made for the other stages comes out wrong here.';
  const list = dlg.querySelector('.hud-list');
  list.replaceChildren(...ctx.data.rows.map(row));
}

async function replace(r, every) {
  if (!ctx.mod) { ctx.log('HUD: choose a mod first (Mod, at the top).', 'err'); return; }
  const f = await pick();
  if (!f) return;
  let img;
  try { img = await asPng(f); } catch (e) { ctx.log(`HUD: ${f.name} is not an image the browser reads.`, 'err'); return; }
  const want = r.w / r.h, got = img.w / img.h;
  if (Math.abs(want - got) > 0.01 * want &&
      !confirm(`${f.name} is ${img.w}×${img.h}; the game's texture is ${r.w}×${r.h}. With other proportions it comes out stretched. Use it anyway?`))
    return;
  try {
    const res = await ctx.api('/api/hud/replace', { stage: ctx.stage, mod: ctx.mod, role: r.role, png: r.png, data: img.data, every });
    ctx.log(res.lines);
  } catch (e) { ctx.log(`HUD: ${e.message}`, 'err'); }
  await refresh();
}

async function remove(r, every) {
  try {
    const res = await ctx.api('/api/hud/remove', { stage: ctx.stage, mod: ctx.mod, role: r.role, png: r.png, every });
    ctx.log(res.lines);
  } catch (e) { ctx.log(`HUD: ${e.message}`, 'err'); }
  await refresh();
}

export async function openHud(opts) {
  if (!dlg) build();
  ctx = { ...opts };
  try { await refresh(); } catch (e) { ctx.log(`HUD: ${e.message}`, 'err'); return; }
  dlg.showModal();
}
