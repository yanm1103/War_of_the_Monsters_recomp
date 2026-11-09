// The Light and fog panel: the stage's fog, background and three parallel lights (RST/<STAGE>.MRS, FORMATS.md), edited with the view
// following as you type and written into the mod as a copy of the file (mods/<mod>/RST/<STAGE>.MRS, or a custom stage's folder).
// Server: GET and POST /api/mrs (editor.py stage_mrs / save_mrs). It is a panel docked at the right, not a modal dialog, so the view
// stays in sight. openLight(ctx) opens it (it builds itself the first time). ctx, from app.js:
//   stage, title, mod, api(url, body), msg(text, {level, toast, open}), mods() (the mod names), newMod, chooseMod(v) -> Promise(mod),
//   preview(raw) (the view shows these numbers), look() / setLook(on) (Game look, which shows fog and background),
//   game() -> {running, dirty}, reload() (Reload in game).
// The CSS is in style.css (#lightDlg).
import { el, icon } from './hud.js';

let dlg = null;
let ctx = null;
let saved = null;      // the numbers as the file has them now (the server's reply)
let source = 'disc';   // whose file that is: "mod" or "disc"
let file = '';
let cur = null;        // the numbers on screen
let orig = null;       // the disc's file for this stage (the server's "original"): what the reset buttons go back to; null when the disc has none
const fixedRs = [];    // the reset buttons of the fog and background rows
const lightRs = [];    // the ones of the lights (rebuilt with the list)
const q = (sel) => dlg.querySelector(sel);

const hex2 = (n) => Math.max(0, Math.min(255, Math.round(n))).toString(16).padStart(2, '0');
const toHex = (c) => '#' + c.map(hex2).join('');
const fromHex = (s) => [1, 3, 5].map((i) => parseInt(s.slice(i, i + 2), 16));
const r1 = (x) => Math.round(x * 100) / 100;

// A light's color is stored as floats that may pass 255 (the game divides by 128, so 190 is a bit under 1.5): the picker holds the hue at
// full brightness (its strongest channel 255) and "Strength" scales it (100 % = 255 in the strongest channel), so what is typed comes back
// as typed after a save.
function split(rgb) {
  const m = Math.max(...rgb);
  return m <= 0 ? { hue: [255, 255, 255], pct: 0 } : { hue: rgb.map((x) => Math.round(x * 255 / m)), pct: Math.round(m / 255 * 100) };
}
const join = (hue, pct) => hue.map((x) => r1(x * pct / 100));

function raw() {   // what the server takes
  return { fog: cur.fog ? { ...cur.fog, rgb: [...cur.fog.rgb] } : null, bg: cur.bg ? [...cur.bg] : null,
           lights: cur.lights ? cur.lights.map((l) => ({ rgb: [...l.rgb], h: l.h, p: l.p })) : null };
}
const clone = (o) => JSON.parse(JSON.stringify(o));
const same = () => JSON.stringify(raw()) === JSON.stringify(saved);
const norm = (d) => d ? { fog: d.fog, bg: d.bg ? d.bg.slice(0, 3) : null, lights: d.lights } : null;

// "Differs from the original": floats come back from the file as float32, so near is enough to show a button; a reset writes the exact numbers
const near = (a, b) => Math.abs(a - b) <= 1e-4 * Math.max(1, Math.abs(b));
const nearArr = (a, b) => a.length === b.length && a.every((x, i) => near(x, b[i]));
const parts = () => ['fog', 'bg', 'lights'].filter((k) => orig && orig[k] && cur[k]);   // the parts both have
const atOrig = () => parts().every((k) => JSON.stringify(cur[k]) === JSON.stringify(orig[k]));
const fogDiffers = (key) => !!(orig && orig.fog && cur.fog && !near(cur.fog[key], orig.fog[key]));
const bgDiffers = () => !!(orig && orig.bg && cur.bg && !nearArr(cur.bg, orig.bg));
const fogColorDiffers = () => !!(orig && orig.fog && cur.fog && !nearArr(cur.fog.rgb, orig.fog.rgb)) || bgDiffers();
const light = (i) => (orig && orig.lights && cur.lights && orig.lights[i] ? { o: orig.lights[i], c: cur.lights[i] } : null);
const hueDiffers = (i) => { const l = light(i); return !!l && split(l.o.rgb).hue.some((x, k) => x !== split(l.c.rgb).hue[k]); };
const pctDiffers = (i) => { const l = light(i); return !!l && split(l.o.rgb).pct !== split(l.c.rgb).pct; };
const hDiffers = (i) => { const l = light(i); return !!l && !near(l.c.h, l.o.h); };
const pDiffers = (i) => { const l = light(i); return !!l && !near(l.c.p, l.o.p); };
// a light's color and strength live in one rgb: changing one part back keeps the other; when both end up as the original's (the picker
// and the strength round), the original's exact numbers come back
function mixRgb(i, hueFrom, pctFrom) {
  const l = light(i), rgb = join(split(hueFrom === 'o' ? l.o.rgb : l.c.rgb).hue, split(pctFrom === 'o' ? l.o.rgb : l.c.rgb).pct);
  const a = split(rgb), b = split(l.o.rgb);
  return a.pct === b.pct && a.hue.every((x, k) => x === b.hue[k]) ? [...l.o.rgb] : rgb;
}

// One small button per number: back to the original, shown only when the number differs. spec: name, show(), value() (for the tooltip),
// apply(), focus() (the input that keeps the focus, so Esc still reaches the panel when the button goes away)
function mkReset(spec, list, labelEl) {
  const b = el('button', { type: 'button', class: 'icon ghost lt-rs', hidden: true }, icon('restore'));
  const go = () => { spec.apply(); syncInputs(); const t = spec.focus && spec.focus(); if (t) t.focus({ preventScroll: true }); changed(); };
  spec.btn = b; spec.lab = labelEl || null;
  b.addEventListener('click', go);
  if (labelEl) { labelEl.classList.add('lt-lab'); labelEl.addEventListener('dblclick', () => { if (spec.show()) go(); }); }
  list.push(spec);
  return b;
}
function syncResets() {
  for (const s of [...fixedRs, ...lightRs]) {
    const on = !!orig && !!cur && s.show();
    s.btn.hidden = !on;
    if (on) { s.btn.title = `Back to the original (${s.value()})`; s.btn.setAttribute('aria-label', `Reset ${s.name} to the original, ${s.value()}`); }
    if (s.lab) s.lab.title = on ? 'Double-click to go back to the original' : '';
  }
}
function resetAll() {
  for (const k of parts()) cur[k] = clone(orig[k]);
  syncInputs();
  const t = q('#ltMin').offsetParent ? q('#ltMin') : q('.lt-h');
  if (t) t.focus({ preventScroll: true });
  changed();
}
// the inputs follow cur (after a reset); the lights list is not rebuilt, so the focus stays
function syncInputs() {
  const f = cur.fog;
  if (f) { q('#ltFogC').value = toHex(f.rgb); q('#ltMin').value = f.minR; q('#ltMax').value = f.maxR; q('#ltDens').value = f.maxV; q('#ltFar').value = f.farClip; }
  if (cur.bg) q('#ltBgC').value = toHex(cur.bg);
  if (cur.lights) q('.lt-list').querySelectorAll('.lt-light').forEach((row, i) => {
    const l = cur.lights[i], { hue, pct } = split(l.rgb);
    row.querySelector('.lt-c').value = toHex(hue); row.querySelector('.lt-pct').value = pct;
    row.querySelector('.lt-h').value = r1(l.h); row.querySelector('.lt-p').value = r1(l.p);
  });
}

function build() {
  dlg = el('dialog', { id: 'lightDlg', class: 'dlg dock', 'aria-labelledby': 'lightTitle' });
  dlg.innerHTML = `<form method="dialog">
    <div class="dlg-head"><h2 id="lightTitle"></h2>
      <button type="button" data-close class="icon ghost" title="Close (Esc)" aria-label="Close"><i class="ic" style="--ic:url(vendor/mdi/close.svg)"></i></button></div>
    <div class="dlg-body">
      <div class="callout info lt-src"><i class="ic" style="--ic:url(vendor/mdi/information-outline.svg)"></i><span></span></div>
      <div class="callout warn lt-nomod" hidden><i class="ic" style="--ic:url(vendor/mdi/alert-outline.svg)"></i>
        <span>Choose or create a mod to save these.</span><select class="lt-mod" aria-label="Mod"></select></div>
      <div class="callout warn lt-look" hidden><i class="ic" style="--ic:url(vendor/mdi/alert-outline.svg)"></i>
        <span>The view shows the fog and the background only with Game look on.</span><button type="button" class="lt-looknow">Turn it on</button></div>
      <div class="lt-fog">
        <div class="sec-title">Fog and background</div>
        <div class="lt-row"><label for="ltFogC">Color</label><input type="color" id="ltFogC"></div>
        <div class="muted lt-hint">The background takes the fog's color.</div>
        <div class="lt-row"><label for="ltMin">Starts at</label><input type="number" id="ltMin" min="0" step="50"><span class="unit">units</span></div>
        <div class="lt-row"><label for="ltMax">Ends at</label><input type="number" id="ltMax" min="0" step="100"><span class="unit">units</span></div>
        <div class="lt-row"><label for="ltDens">Thickest</label><input type="number" id="ltDens" min="0" max="255" step="5"><span class="unit">0 to 255</span></div>
        <div class="lt-row"><label for="ltFar">Draw distance</label><input type="number" id="ltFar" min="1" step="100"><span class="unit">units</span></div>
      </div>
      <div class="lt-bg">
        <div class="sec-title">Background</div>
        <div class="lt-row"><label for="ltBgC">Color</label><input type="color" id="ltBgC"></div>
      </div>
      <div class="lt-lights">
        <div class="sec-title">Lights</div>
        <div class="muted lt-hint">They light the monsters and the objects the game lights as it draws them (cars, towers, statues); the rest of the stage has its light in its colors.</div>
        <div class="lt-list"></div>
        <div class="muted lt-hint lt-dirhint">Heading is where the light comes from (0 = +Y, turning toward +X); pitch 90 is straight overhead.</div>
      </div>
      <div class="lt-warn muted" hidden></div>
    </div>
    <div class="dlg-foot"><span class="lt-game muted"></span>
      <button type="button" class="lt-reload" hidden><i class="ic" style="--ic:url(vendor/mdi/refresh.svg)"></i>Reload in game</button>
      <button type="button" class="lt-resetall ghost" title="Back to the original: every number on this panel"><i class="ic" style="--ic:url(vendor/mdi/restore.svg)"></i>Reset all</button>
      <button type="button" class="lt-reset ghost" hidden title="Delete the mod's light file, so the stage uses the disc's">Remove from mod</button>
      <button type="button" class="lt-save primary">Save to mod</button></div>
  </form>`;
  document.body.append(dlg);
  q('[data-close]').addEventListener('click', requestClose);
  // not a modal dialog, so the browser does not turn Esc into a close: the panel does it, from inside it
  dlg.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !e.defaultPrevented) { e.preventDefault(); e.stopPropagation(); requestClose(); } });
  q('.lt-reload').addEventListener('click', () => ctx.reload());
  q('.lt-save').addEventListener('click', save);
  q('.lt-reset').addEventListener('click', reset);
  q('.lt-resetall').addEventListener('click', resetAll);
  q('.lt-looknow').addEventListener('click', () => { ctx.setLook(true); showLook(); });
  q('.lt-mod').addEventListener('change', async (e) => {
    const v = e.target.value;
    e.target.blur();
    if (!v) return;
    ctx.mod = (await ctx.chooseMod(v)) || '';
    await load();
  });
  // the fog
  const fogInput = (id, key) => q(id).addEventListener('input', (e) => {
    const v = parseFloat(e.target.value);
    if (Number.isFinite(v) && cur.fog) { cur.fog[key] = v; changed(); }
  });
  fogInput('#ltMin', 'minR'); fogInput('#ltMax', 'maxR'); fogInput('#ltDens', 'maxV'); fogInput('#ltFar', 'farClip');
  q('#ltFogC').addEventListener('input', (e) => {
    const c = fromHex(e.target.value);
    if (cur.fog) cur.fog.rgb = c;
    if (cur.bg) cur.bg = [...c];
    changed();
  });
  q('#ltBgC').addEventListener('input', (e) => { cur.bg = fromHex(e.target.value); changed(); });
  q('form').addEventListener('submit', (e) => e.preventDefault());
  // the reset buttons of the fog's numbers and colors and of the background
  fixedRs.length = 0;
  const addRs = (id, spec) => { const row = q(id).closest('.lt-row'); row.append(mkReset({ focus: () => q(id), ...spec }, fixedRs, row.querySelector('label'))); };
  for (const [id, key, name] of [['#ltMin', 'minR', 'starts at'], ['#ltMax', 'maxR', 'ends at'], ['#ltDens', 'maxV', 'thickest'], ['#ltFar', 'farClip', 'draw distance']])
    addRs(id, { name: `fog ${name}`, show: () => fogDiffers(key), value: () => String(r1(orig.fog[key])), apply: () => { cur.fog[key] = orig.fog[key]; } });
  addRs('#ltFogC', { name: 'fog color', show: fogColorDiffers, value: () => toHex(orig.fog.rgb),
    apply: () => { cur.fog.rgb = [...orig.fog.rgb]; if (cur.bg && orig.bg) cur.bg = [...orig.bg]; } });
  addRs('#ltBgC', { name: 'background color', show: bgDiffers, value: () => toHex(orig.bg), apply: () => { cur.bg = [...orig.bg]; } });
  dlg.addEventListener('close', () => { if (cur && !same()) ctx.restore(); });   // closed without saving: the view goes back to the file's numbers
}

function lightRow(i) {
  const l = cur.lights[i], { hue, pct } = split(l.rgb);
  const num = (cls, min, max, step, v, label) => el('input', { type: 'number', class: cls, id: `lt${i}${cls.slice(3)}`, min, max, step, value: v, 'aria-label': `Light ${i + 1} ${label}` });
  const color = el('input', { type: 'color', class: 'lt-c', value: toHex(hue), 'aria-label': `Light ${i + 1} color` });
  const pc = num('lt-pct', 0, 400, 5, pct, 'strength'), hd = num('lt-h', -360, 360, 5, r1(l.h), 'heading'), pt = num('lt-p', -90, 90, 5, r1(l.p), 'pitch');
  // the color is only rebuilt from the picker and the strength when one of those two moves (the rounding of the picker's whole numbers
  // would otherwise nudge a light's color when only its direction changes)
  const updColor = () => {
    const s = parseFloat(pc.value);
    cur.lights[i].rgb = join(fromHex(color.value), Number.isFinite(s) ? Math.max(0, Math.min(400, s)) : 100);
    changed();
  };
  const updDir = () => {
    const hv = parseFloat(hd.value), pv = parseFloat(pt.value);
    if (Number.isFinite(hv)) cur.lights[i].h = hv;
    if (Number.isFinite(pv)) cur.lights[i].p = Math.max(-90, Math.min(90, pv));
    changed();
  };
  for (const e of [color, pc]) e.addEventListener('input', updColor);
  for (const e of [hd, pt]) e.addEventListener('input', updDir);
  const n = `light ${i + 1}`, o = () => orig.lights[i], c = () => cur.lights[i];
  const field = (text, input, spec) => {
    const lab = el('label', { for: input.id }, text);
    return el('div', { class: 'lt-f' }, el('div', { class: 'lt-fh' }, lab, mkReset({ focus: () => input, ...spec }, lightRs, lab)), input);
  };
  const whole = mkReset({ name: n, focus: () => pc, show: () => hueDiffers(i) || pctDiffers(i) || hDiffers(i) || pDiffers(i),
    value: () => `${toHex(split(o().rgb).hue)}, ${split(o().rgb).pct} %, ${r1(o().h)}°, ${r1(o().p)}°`, apply: () => { cur.lights[i] = clone(o()); } }, lightRs);
  const cRs = mkReset({ name: `${n} color`, focus: () => color, show: () => hueDiffers(i), value: () => toHex(split(o().rgb).hue),
    apply: () => { c().rgb = mixRgb(i, 'o', 'c'); } }, lightRs);
  return el('div', { class: 'lt-light' }, el('div', { class: 'lt-lh' }, el('b', {}, `Light ${i + 1}`), whole, cRs, color),
    field('Strength %', pc, { name: `${n} strength`, show: () => pctDiffers(i), value: () => `${split(o().rgb).pct} %`, apply: () => { c().rgb = mixRgb(i, 'c', 'o'); } }),
    field('Heading °', hd, { name: `${n} heading`, show: () => hDiffers(i), value: () => `${r1(o().h)}°`, apply: () => { c().h = o().h; } }),
    field('Pitch °', pt, { name: `${n} pitch`, show: () => pDiffers(i), value: () => `${r1(o().p)}°`, apply: () => { c().p = o().p; } }));
}

function changed() { ctx.preview(raw()); showFoot(); if (ctx.changed) ctx.changed(); }

// Close and Esc: with changes that are not saved, ask (they were thrown away without a word)
async function requestClose() {
  if (!cur || same()) { dlg.close('cancel'); return; }
  const v = await new Promise((res) => ctx.ask('Close Light and fog?', 'You changed the light and did not save it. Closing puts the stage back as the file has it.',
    [['keep', 'Keep editing'], ['discard', 'Discard'], ['save', 'Save to mod', true]], res, { icon: 'content-save-alert-outline' }));
  if (v === 'discard') dlg.close('cancel');
  else if (v === 'save') { await save(); if (same()) dlg.close('cancel'); }
}

function showLook() { q('.lt-look').hidden = ctx.look() || !(cur && (cur.fog || cur.bg)); }
function showFoot() {
  q('.lt-save').disabled = !ctx.mod || same();
  q('.lt-save').title = !ctx.mod ? 'Choose or create a mod to save these' : same() ? 'Nothing changed' : `Writes mods/${ctx.mod}/RST/… (Save, outside Undo)`;
  q('.lt-reset').hidden = source !== 'mod';
  q('.lt-resetall').disabled = !parts().length || atOrig();
  syncResets();
  const g = ctx.game();
  q('.lt-reload').hidden = !g.running;
  q('.lt-reload').title = g.dirty ? 'Load this stage again in the game (it also saves your unsaved stage changes)' : 'Load this stage again in the game, with this file';
  q('.lt-game').textContent = g.running ? '' : 'The game reads it when the stage loads.';
  const f = cur.fog;
  const w = q('.lt-warn');
  w.hidden = !(f && !(f.maxR > f.minR));
  w.textContent = 'The fog ends before it starts: the game then draws no fog.';
}

function fill() {
  const t = q('#lightTitle');
  t.textContent = `Light and fog · ${ctx.title}`;
  q('.lt-src span').textContent = source === 'mod' ? `Your mod's file is used: ${file.replace(/\\/g, '/').replace(/^.*\/mods\//, 'mods/')}`
    : 'The disc\'s file is used. Saving writes a copy into your mod and leaves the disc alone.';
  const nm = q('.lt-nomod');
  nm.hidden = !!ctx.mod;
  if (!ctx.mod) {
    const sel = q('.lt-mod');
    sel.replaceChildren(el('option', { value: '' }, 'Choose a mod…'),
                        ...ctx.mods().map((m) => el('option', { value: m }, m)), el('option', { value: ctx.newMod }, 'New mod…'));
    sel.value = '';
  }
  const f = cur.fog;
  q('.lt-fog').hidden = !f;
  q('.lt-bg').hidden = !!f || !cur.bg;
  if (f) {
    q('#ltFogC').value = toHex(f.rgb);
    q('#ltMin').value = f.minR; q('#ltMax').value = f.maxR; q('#ltDens').value = f.maxV; q('#ltFar').value = f.farClip;
  }
  if (cur.bg) q('#ltBgC').value = toHex(cur.bg);
  q('.lt-lights').hidden = !cur.lights;
  lightRs.length = 0;
  q('.lt-list').replaceChildren(...(cur.lights ? [0, 1, 2].map(lightRow) : []));
  showLook();
  showFoot();
}

async function load() {
  const d = await ctx.api(`/api/mrs?name=${encodeURIComponent(ctx.stage)}&mod=${encodeURIComponent(ctx.mod || '')}`);
  source = d.source; file = d.file || '';
  saved = norm(d);
  orig = norm(d.original);
  cur = clone(saved);       // the background goes as 3 colors: the server leaves the file's alpha as it is
  fill();
  ctx.preview(raw());
}

async function save() {
  if (!ctx.mod) { ctx.msg('Choose or create a mod to save the light.', { level: 'warn', toast: true, open: false }); return; }
  try {
    const d = await ctx.api('/api/mrs', { mod: ctx.mod, name: ctx.stage, ...raw() });
    source = d.source; file = d.file || '';
    saved = norm(d);
    orig = norm(d.original);
    cur = clone(saved);
    ctx.msg(`Light and fog: saved into mod "${ctx.mod}"` + (d.file ? ` (${d.file.replace(/\\/g, '/').replace(/^.*\/mods\//, 'mods/')})` : ''), { level: 'ok', toast: true, open: false });
    for (const w of d.warnings || []) ctx.msg(w, { level: 'warn', toast: true });
    fill();
    ctx.preview(raw());
    if (ctx.changed) ctx.changed();
  } catch (e) { ctx.msg(`Couldn't save the light: ${e.message}`, { level: 'err', toast: true }); }
}

async function reset() {
  const go = await new Promise((res) => ctx.ask('Remove the light file from the mod?',
    `This deletes the light and fog file ${ctx.title} has in mod "${ctx.mod}", so the stage uses the disc's file. It can't be undone.`,
    [['cancel', 'Cancel'], ['go', 'Delete the file', true]], (v) => res(v === 'go'), { icon: 'delete-outline' }));
  if (!go) return;
  try {
    await ctx.api('/api/mrs', { mod: ctx.mod, name: ctx.stage, reset: true });
    ctx.msg(`Light and fog: the mod's file is removed, the disc's is used`,{ level: 'ok', toast: true, open: false });
    await load();
  } catch (e) { ctx.msg(`Couldn't remove the mod's file: ${e.message}`, { level: 'err', toast: true }); }
}

export async function lightRequestClose() { if (dlg && dlg.open) await requestClose(); }
export async function openLight(opts) {
  if (!dlg) build();
  ctx = { ...opts };
  try { await load(); } catch (e) { ctx.msg(`Couldn't open the light: ${e.message}`, { level: 'err' }); return; }
  if (!dlg.open) dlg.show();
  // the first number to edit, or the close button when the stage has no fog. The focus must be inside for Esc to reach the panel,
  // and a color field keeps the Esc for itself, so it is not the first stop
  (q('#ltMin').offsetParent ? q('#ltMin') : q('[data-close]')).focus({ preventScroll: true });
}
// app.js calls these when the game starts or stops, when Game look changes and when the stage or mod changes
export function lightGameChanged() { if (dlg && dlg.open && ctx) showFoot(); }
export function lightLookChanged() { if (dlg && dlg.open && ctx && cur) showLook(); }
// the panel is open with numbers that differ from the file's; app.js counts it among the unsaved changes and saves it with Save
export function lightDirty() { return !!(dlg && dlg.open && cur && saved && !same()); }
export async function lightSave() { if (lightDirty()) await save(); return !lightDirty(); }
export function lightClose() { if (dlg && dlg.open) { cur = clone(saved); dlg.close('cancel'); } }   // no restore: the caller loads its own
