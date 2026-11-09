// The New terrain dialog: a PNG of heights becomes a prop of the mod (hills and ramps: a drawn mesh with its light baked in, and the same
// triangles as collision, tools/lvlterrain.py). Server: POST /api/props/terrain. openTerrain(ctx) opens it (it builds itself the first
// time). ctx, from app.js: stage, title, mod, api(url, body), msg(text, opts), ask(head, text, buttons, then), size() -> [x, y] (the
// rectangle the dialog starts with), done(res, replaced) (the prop is built: app.js loads it and places it).
// The CSS is in style.css (#terrainDlg).
import { el } from './hud.js';

let dlg = null;
let ctx = null;
let heights = null;     // {data (base64), url (data URL), name, w, h}
let colors = null;
const q = (sel) => dlg.querySelector(sel);

const MAX_CELLS = 90;           // lvlterrain.MAX_CELLS
const MAX_TRIS = 16384;         // the collision's limit
function cellsFor(sx, sy, cells) {
  const c = Math.max(1, Math.min(MAX_CELLS, Math.round(cells) || 1));
  return sx >= sy ? [c, Math.max(1, Math.round(c * sy / sx))] : [Math.max(1, Math.round(c * sx / sy)), c];
}

function build() {
  dlg = el('dialog', { id: 'terrainDlg', class: 'dlg', 'aria-labelledby': 'trTitle' });
  dlg.innerHTML = `<form method="dialog">
    <div class="dlg-head"><h2 id="trTitle">New terrain</h2>
      <button type="button" data-close class="icon ghost" title="Close (Esc)" aria-label="Close"><i class="ic" style="--ic:url(vendor/mdi/close.svg)"></i></button></div>
    <div class="dlg-body">
      <p class="muted tr-text"></p>
      <div class="tr-file"><button type="button" class="tr-pick-h"><i class="ic" style="--ic:url(vendor/mdi/terrain.svg)"></i>Choose heightmap…</button>
        <span class="muted tr-name-h">No file chosen</span><img class="tr-thumb tr-thumb-h" alt="" hidden></div>
      <div class="tr-file"><button type="button" class="tr-pick-c" title="A picture laid over the same rectangle; without it the terrain is green low, brown and gray high">Choose colors…</button>
        <span class="muted tr-name-c">Optional</span><button type="button" class="icon ghost tr-clear-c" title="Back to the height colors" aria-label="Remove the colors" hidden><i class="ic" style="--ic:url(vendor/mdi/close.svg)"></i></button>
        <img class="tr-thumb tr-thumb-c" alt="" hidden></div>
      <label class="fl">Name<input class="tr-id" maxlength="8" spellcheck="false" autocomplete="off" placeholder="HILLS"
        title="The prop's folder in the mod: 1 to 8 letters, digits or _"></label>
      <div class="tr-grid">
        <label class="fl">Width (X)<input type="number" class="tr-sx" min="10" max="40000" step="100"></label>
        <label class="fl">Depth (Y)<input type="number" class="tr-sy" min="10" max="40000" step="100"></label>
        <label class="fl">Height (white)<input type="number" class="tr-h" min="1" max="20000" step="10"></label>
        <label class="fl">Detail<input type="number" class="tr-cells" min="4" max="${MAX_CELLS}" step="4" title="Cells along the longer side"></label>
        <label class="fl">Brightness<input type="number" class="tr-bright" min="10" max="200" step="5" title="Percent: the stages' own colors are dark, so 100 looks bright (up to 200)"></label>
      </div>
      <div class="help tr-info"></div>
      <div class="help">Units are game units (a car is about 40, a small building 300). White is the highest point and black the stage's
        own floor level; the picture's top is +Y. A gray PNG is exact; any color counts as its brightness. The monsters walk on the hills
        and the drawn mesh sits just under them. The prop is placed at the middle of the rectangle.</div>
      <div class="callout err tr-err" hidden><i class="ic" style="--ic:url(vendor/mdi/alert-circle-outline.svg)"></i><span></span></div>
    </div>
    <div class="dlg-foot"><button type="button" data-close>Cancel</button><button type="button" class="tr-ok primary">Create</button></div>
  </form>`;
  document.body.append(dlg);
  q('[data-close]').addEventListener('click', () => dlg.close('cancel'));
  const input = (cls, on) => {
    const i = Object.assign(document.createElement('input'), { type: 'file', accept: 'image/png' });
    i.onchange = () => { const f = i.files[0]; i.value = ''; if (f) readPng(f, on); };
    q(cls).addEventListener('click', () => i.click());
    return i;
  };
  input('.tr-pick-h', setHeights);
  input('.tr-pick-c', setColors);
  q('.tr-clear-c').addEventListener('click', () => setColors(null));
  dlg.addEventListener('dragover', (e) => e.preventDefault());
  dlg.addEventListener('drop', (e) => { e.preventDefault(); const f = e.dataTransfer.files[0]; if (f) readPng(f, heights ? setColors : setHeights); });
  for (const c of ['.tr-sx', '.tr-sy', '.tr-cells']) q(c).addEventListener('input', info);
  q('.tr-id').addEventListener('input', () => err(''));
  q('.tr-ok').addEventListener('click', () => create(false));
  q('.tr-id').addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); create(false); } });
}

function err(t) { const e = q('.tr-err'); e.hidden = !t; if (t) e.querySelector('span').textContent = t; }

function readPng(f, then) {
  if (!/png$/i.test(f.type) && !/\.png$/i.test(f.name)) { err(`${f.name} is not a PNG. Save the picture as a PNG first.`); return; }
  const rd = new FileReader();
  rd.onload = () => {
    const url = String(rd.result), im = new Image();
    im.onload = () => { err(''); then({ data: url.replace(/^data:[^,]*,/, ''), url, name: f.name, w: im.naturalWidth, h: im.naturalHeight, kb: Math.max(1, Math.round(f.size / 1024)) }); };
    im.onerror = () => err(`${f.name} does not open as a picture`);
    im.src = url;
  };
  rd.onerror = () => err(`Couldn't read ${f.name}`);
  rd.readAsDataURL(f);
}
function setHeights(h) {
  heights = h;
  q('.tr-name-h').textContent = `${h.name} (${h.w} × ${h.h}, ${h.kb} KB)`;
  const t = q('.tr-thumb-h');
  t.src = h.url;
  t.hidden = false;
  const id = q('.tr-id');
  if (!id.value) id.value = h.name.replace(/\.[^.]*$/, '').toUpperCase().replace(/[^A-Z0-9_]/g, '').slice(0, 8);
  info();
}
function setColors(c) {
  colors = c;
  q('.tr-name-c').textContent = c ? `${c.name} (${c.w} × ${c.h})` : 'Optional';
  q('.tr-clear-c').hidden = !c;
  const t = q('.tr-thumb-c');
  t.hidden = !c;
  if (c) t.src = c.url;
}

const nums = () => ({ sx: +q('.tr-sx').value, sy: +q('.tr-sy').value, h: +q('.tr-h').value, cells: +q('.tr-cells').value,
                       bright: +q('.tr-bright').value });
function info() {
  const n = nums();
  if (!(n.sx > 0 && n.sy > 0)) { q('.tr-info').textContent = ''; return true; }
  const [cx, cy] = cellsFor(n.sx, n.sy, n.cells), tris = cx * cy * 2;
  q('.tr-info').textContent = `${cx} × ${cy} cells, ${tris} triangles` + (tris > MAX_TRIS ? `: over the collision's ${MAX_TRIS}` : '') +
    `; a cell is ${Math.round(n.sx / cx)} × ${Math.round(n.sy / cy)} units.`;
  return tris <= MAX_TRIS;
}

async function create(replace) {
  const n = nums(), name = q('.tr-id').value.trim().toUpperCase();
  if (!heights) { err('Choose a heightmap (a PNG) first.'); return; }
  if (!/^[A-Z0-9_]{1,8}$/.test(name)) { err('The name is 1 to 8 letters, digits or _.'); q('.tr-id').focus(); return; }
  if (!(n.sx >= 10 && n.sy >= 10)) { err('The width and the depth are at least 10 units.'); return; }
  if (!(n.h >= 1)) { err('The height is at least 1 unit.'); return; }
  if (!(n.bright >= 10 && n.bright <= 200)) { err('The brightness is 10 to 200 percent.'); return; }
  if (!info()) { err('Too many triangles: lower the detail.'); return; }
  const ok = q('.tr-ok');
  ok.disabled = true;
  ok.textContent = 'Building…';
  let res;
  try {
    res = await ctx.api('/api/props/terrain', { mod: ctx.mod, stage: ctx.stage, name, size: [n.sx, n.sy], height: n.h, cells: n.cells, brightness: n.bright,
                                                heights: heights.data, colors: colors ? colors.data : '', replace, title: 'Terrain' });
  } catch (e) { err(e.message || String(e)); return; }
  finally { ok.disabled = false; ok.textContent = 'Create'; }
  if (res.exists) {
    ctx.ask(`Replace ${name}?`, `Mod "${ctx.mod}" already has a prop ${name}. The new terrain takes its place wherever it is used.`,
            [['cancel', 'Cancel'], ['replace', 'Replace', true]], (a) => { if (a === 'replace') create(true); });
    return;
  }
  dlg.close('ok');
  ctx.done(res, !!res.replaced);
}

export function openTerrain(c) {
  ctx = c;
  if (!dlg) build();
  const s = ctx.size ? ctx.size() : [2000, 2000];
  heights = colors = null;
  setColors(null);
  q('.tr-name-h').textContent = 'No file chosen';
  q('.tr-thumb-h').hidden = true;
  q('.tr-id').value = '';
  q('.tr-sx').value = String(Math.round(s[0]));
  q('.tr-sy').value = String(Math.round(s[1]));
  q('.tr-h').value = '300';
  q('.tr-cells').value = '64';
  q('.tr-bright').value = '80';
  q('.tr-text').textContent = `Hills and ramps for ${ctx.title}, from a picture of heights, saved as a prop of mod "${ctx.mod}".`;
  err('');
  info();
  dlg.showModal();
  q('.tr-pick-h').focus();
}
