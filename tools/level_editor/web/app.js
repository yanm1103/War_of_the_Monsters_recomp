// WotM level editor page: the stage from /api/glb (tools/lvlglb.py), objects and patch from /api/stage (tools/lvl.py).
// Game coordinates are Z up (x, y, z); the scene is Y up: three = (x, z, -y). A heading turns about the game's Z, which is
// the scene's +Y with the same sign. The patch (docs/guides/MODS.md, "Level patches") is rebuilt from the scene each time.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';

const $ = (id) => document.getElementById(id);
const Y = new THREE.Vector3(0, 1, 0);
const toThree = (p) => new THREE.Vector3(p[0], p[2], -p[1]);
const toGame = (v) => [v.x, -v.z, v.y];
const r1 = (x) => Math.round(x * 10) / 10;
const wrapDeg = (a) => ((a + 180) % 360 + 360) % 360 - 180;
const normHandle = (h) => '0x' + parseInt(String(h), 16).toString(16).toUpperCase();

async function api(path, body) {
  const r = await fetch(path, body ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) } : {});
  const j = await r.json();
  if (!r.ok) throw new Error(j.error || r.statusText);
  return j;
}

function log(lines, cls) {
  const el = $('log');
  for (const l of [].concat(lines)) {
    const d = document.createElement('div');
    if (cls) d.className = cls;
    d.textContent = l;
    el.appendChild(d);
  }
  el.scrollTop = el.scrollHeight;
}

// ------------------------------------------------------------------ view
const canvas = $('canvas');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(55, 1, 5, 60000);
camera.position.set(0, 2500, 2500);
const orbit = new OrbitControls(camera, canvas);
orbit.enableDamping = true;
orbit.screenSpacePanning = true;
const gizmo = new TransformControls(camera, canvas);
gizmo.setSize(0.9);
scene.add(gizmo);
const selBox = new THREE.BoxHelper(undefined, 0xffcc33);
selBox.visible = false;
scene.add(selBox);
let stageRoot = null;

function resize() {
  const r = canvas.parentElement.getBoundingClientRect();
  renderer.setSize(r.width, r.height, false);
  camera.aspect = r.width / Math.max(1, r.height);
  camera.updateProjectionMatrix();
}
new ResizeObserver(resize).observe(canvas.parentElement);

function applyTheme(t) {
  document.documentElement.dataset.theme = t;
  renderer.setClearColor(getComputedStyle(document.documentElement).getPropertyValue('--view').trim());
}

function loop() {
  requestAnimationFrame(loop);
  orbit.update();
  if (sel && selBox.visible) selBox.setFromObject(sel.node);
  renderer.render(scene, camera);
}

// ------------------------------------------------------------------ model
// rec: one object of the stage (handle). item: what can be selected, the object itself or one of its copies.
let cfg = {}, stageName = '', recs = [], byHandle = new Map(), itemOf = new Map(), sel = null, history = [], dirty = false;

function setWorld(node, posGame, heading, rec) {
  const qWorld = new THREE.Quaternion().setFromAxisAngle(Y, THREE.MathUtils.degToRad(heading - rec.h0)).multiply(rec.q0);
  const m = new THREE.Matrix4().compose(toThree(posGame), qWorld, rec.s0);
  setWorldMatrix(node, m);
}
function setWorldMatrix(node, m) {
  node.parent.updateWorldMatrix(true, false);
  const local = node.parent.matrixWorld.clone().invert().multiply(m);
  local.decompose(node.position, node.quaternion, node.scale);
  node.updateMatrixWorld(true);
}
function posOf(item) { return toGame(item.node.getWorldPosition(new THREE.Vector3())); }
function headingOf(item) {
  const q = item.node.getWorldQuaternion(new THREE.Quaternion()).multiply(item.rec.q0.clone().invert());
  return wrapDeg(item.rec.h0 + THREE.MathUtils.radToDeg(2 * Math.atan2(q.y, q.w)));
}

function makeCopy(rec, pos, heading) {
  const node = rec.node.clone(true);
  rec.node.parent.add(node);
  node.visible = true;
  const item = { rec, node, copy: true, attached: [] };
  itemOf.set(node, item);
  rec.copies.push(item);
  setWorld(node, pos, heading, rec);
  return item;
}
function removeCopy(item) {
  item.node.parent.remove(item.node);
  itemOf.delete(item.node);
  item.rec.copies.splice(item.rec.copies.indexOf(item), 1);
  for (const a of item.attached) if (a.copy) removeCopy(a);
}

function buildPatch() {
  const objects = [];
  for (const rec of recs) {
    const e = { handle: rec.info.handle, from: rec.p0 };
    if (rec.deleted) e.delete = true;
    else {
      const p = posOf(rec.item), h = headingOf(rec.item);
      if (Math.hypot(p[0] - rec.pw[0], p[1] - rec.pw[1], p[2] - rec.pw[2]) > 0.05) e.pos = p.map(r1);
      if (Math.abs(wrapDeg(h - rec.h0)) > 0.05) e.heading = r1(h);
    }
    if (rec.copies.length) e.copies = rec.copies.map((c) => ({ pos: posOf(c).map(r1), heading: r1(headingOf(c)) }));
    if (Object.keys(e).length > 2) objects.push(e);
  }
  return { objects };
}

function resetAll() {
  for (const rec of recs) {
    for (const c of [...rec.copies]) removeCopy(c);
    rec.deleted = false;
    rec.node.visible = true;
    setWorldMatrix(rec.node, rec.m0);
  }
}

function applyPatch(patch) {
  for (const e of (patch && patch.objects) || []) {
    const rec = byHandle.get(normHandle(e.handle));
    if (!rec) { log(`patch: ${e.handle} is not an object of ${stageName}; kept out`, 'err'); continue; }
    for (const c of e.copies || []) makeCopy(rec, c.pos, c.heading ?? rec.h0);
    if (e.delete) { rec.deleted = true; rec.node.visible = false; }
    else if (e.pos || e.heading !== undefined) setWorld(rec.node, e.pos || rec.pw, e.heading ?? rec.h0, rec);
  }
}

function snapshot() {
  history.push(JSON.stringify(buildPatch()));
  if (history.length > 200) history.shift();
  dirty = true;
}
function undo() {
  if (!history.length) return;
  const p = JSON.parse(history.pop());
  const keep = sel && !sel.copy ? sel.rec : null;
  resetAll();
  applyPatch(p);
  select(keep ? keep.item : null);
  refreshList();
}

// ------------------------------------------------------------------ loading
async function loadStage(name) {
  $('busy').hidden = false;
  try {
    select(null);
    if (stageRoot) { scene.remove(stageRoot); stageRoot.traverse((o) => { if (o.geometry) o.geometry.dispose(); }); }
    stageName = name;
    const mod = $('mod').value.trim();
    const info = await api(`/api/stage?name=${encodeURIComponent(name)}&mod=${encodeURIComponent(mod)}`);
    const gltf = await new GLTFLoader().loadAsync(`/api/glb?name=${encodeURIComponent(name)}`);
    stageRoot = gltf.scene;
    scene.add(stageRoot);
    recs = []; byHandle = new Map(); itemOf = new Map(); history = [];
    const infoOf = new Map(info.objects.map((o) => [o.handle, o]));
    stageRoot.updateMatrixWorld(true);
    stageRoot.traverse((o) => {
      const u = o.userData || {};
      if (u.hidden) o.visible = false;
      if (u.root !== undefined) o.visible = u.ident === 1 || u.ident === 0;   // world and loose roots; not the sky
      if (u.handle) {
        const i = infoOf.get(normHandle(u.handle));
        if (!i) return;
        const q0 = new THREE.Quaternion(), s0 = new THREE.Vector3(), p = new THREE.Vector3();
        o.matrixWorld.decompose(p, q0, s0);
        const rec = { info: i, node: o, p0: i.pos, pw: toGame(p), m0: o.matrixWorld.clone(), h0: i.heading, q0, s0, copies: [], deleted: false };
        rec.item = { rec, node: o, copy: false, attached: [] };
        recs.push(rec);
        byHandle.set(normHandle(i.handle), rec);
        itemOf.set(o, rec.item);
      }
    });
    for (const rec of recs) {
      if (rec.info.on) { const b = byHandle.get(normHandle(rec.info.on)); if (b) b.item.attached.push(rec.item); }
    }
    applyPatch(info.patch);
    dirty = false;
    frameAll();
    refreshList();
    if (new URLSearchParams(location.search).has('debug')) console.log('patch ' + JSON.stringify(buildPatch()));
    log(`${name}: ${recs.length} objects` + ((info.patch.objects || []).length ? `, patch of mod "${mod}" with ${info.patch.objects.length} entries` : ''));
  } catch (e) {
    log(`loading ${name}: ${e.message}`, 'err');
  } finally {
    $('busy').hidden = true;
  }
}

function frameAll() {
  const box = new THREE.Box3();
  for (const rec of recs) box.expandByPoint(rec.node.getWorldPosition(new THREE.Vector3()));
  if (box.isEmpty()) return;
  const c = box.getCenter(new THREE.Vector3()), size = box.getSize(new THREE.Vector3()).length();
  orbit.target.copy(c);
  camera.position.copy(c).add(new THREE.Vector3(0, size * 0.55, size * 0.55));
}
function focus(item) {
  const box = new THREE.Box3().setFromObject(item.node);
  if (box.isEmpty()) return;
  const c = box.getCenter(new THREE.Vector3()), r = Math.max(150, box.getSize(new THREE.Vector3()).length());
  const dir = camera.position.clone().sub(orbit.target).normalize();
  orbit.target.copy(c);
  camera.position.copy(c).addScaledVector(dir, r * 1.4);
}

// ------------------------------------------------------------------ list and properties
function items() {
  const out = [];
  for (const rec of recs) { out.push(rec.item); for (const c of rec.copies) out.push(c); }
  return out;
}
function changed(rec) {
  if (rec.deleted || rec.copies.length) return true;
  const p = posOf(rec.item);
  return Math.hypot(p[0] - rec.pw[0], p[1] - rec.pw[1], p[2] - rec.pw[2]) > 0.05 || Math.abs(wrapDeg(headingOf(rec.item) - rec.h0)) > 0.05;
}
function label(item) {
  const i = item.rec.info;
  return item.copy ? `${i.handle} copy ${item.rec.copies.indexOf(item) + 1}` : i.handle;
}
function refreshList() {
  const f = $('filter').value.trim().toLowerCase();
  const kinds = new Set([...document.querySelectorAll('.kinds input[data-kind]')].filter((x) => x.checked).map((x) => x.dataset.kind));
  const onlyChanged = $('changedOnly').checked;
  const ul = $('list');
  ul.textContent = '';
  let n = 0;
  for (const it of items()) {
    const i = it.rec.info;
    if (!kinds.has(i.kind)) continue;
    if (onlyChanged && !changed(it.rec)) continue;
    const text = `${label(it)} ${i.kind} ${i.id.toString(16)}`.toLowerCase();
    if (f && !text.includes(f)) continue;
    const li = document.createElement('li');
    li.innerHTML = `<span class="k">${i.kind}</span><span class="h"></span>`;
    li.querySelector('.h').textContent = label(it);
    if (it === sel) li.className = 'sel';
    if (!it.copy && changed(it.rec)) li.classList.add('changed');
    if (!it.copy && it.rec.deleted) li.classList.add('deleted');
    li.onclick = () => { select(it); focus(it); };
    ul.appendChild(li);
    n++;
  }
  $('count').textContent = `${n} shown · ${buildPatch().objects.length} changed`;
  document.title = `WotM Level Editor — ${stageName}${dirty ? ' *' : ''}`;
}

function select(item) {
  sel = item && item.node.visible ? item : null;
  gizmo.detach();
  selBox.visible = !!sel;
  $('props').hidden = !sel;
  $('noSel').hidden = !!sel;
  if (sel) {
    gizmo.attach(sel.node);
    selBox.setFromObject(sel.node);
    const i = sel.rec.info;
    $('pTitle').textContent = label(sel);
    $('pSub').textContent = `${i.kind} · id 0x${i.id.toString(16)} · ${i.states} states · ${i.paths} collision paths` +
      (i.on ? ` · on ${i.on}` : '');
    $('onCount').textContent = sel.attached.length;
    $('pWarn').textContent = i.paths === 0 && i.kind !== 'pickup' ? 'No collision of its own.' : '';
    showProps();
  }
  refreshList();
}
function showProps() {
  if (!sel) return;
  const p = posOf(sel), h = headingOf(sel);
  $('px').value = r1(p[0]); $('py').value = r1(p[1]); $('pz').value = r1(p[2]); $('ph').value = r1(h);
}

// ------------------------------------------------------------------ editing
let dragStart = null;
gizmo.addEventListener('dragging-changed', (e) => {
  orbit.enabled = !e.value;
  if (e.value && sel) {
    snapshot();
    dragStart = { inv: sel.node.matrixWorld.clone().invert(), att: carried().map((a) => [a, a.node.matrixWorld.clone()]) };
  } else {
    dragStart = null;
    refreshList();
  }
});
gizmo.addEventListener('objectChange', () => {
  if (!sel) return;
  if (gizmo.getMode() === 'rotate') setWorld(sel.node, posOf(sel), headingOf(sel), sel.rec);   // only about the game's Z
  if (dragStart) carry(dragStart);
  showProps();
});
function carried() { return $('carry').checked && sel ? sel.attached.filter((a) => a.node.visible) : []; }
function carry(start) {
  const delta = sel.node.matrixWorld.clone().multiply(start.inv);
  for (const [a, m0] of start.att) setWorldMatrix(a.node, delta.clone().multiply(m0));
}

function setTool(mode) {
  gizmo.setMode(mode);
  gizmo.showX = gizmo.showZ = mode !== 'rotate';
  gizmo.showY = true;
  $('tMove').classList.toggle('on', mode === 'translate');
  $('tTurn').classList.toggle('on', mode === 'rotate');
}
function setSnap(on) {
  gizmo.setTranslationSnap(on ? 10 : null);
  gizmo.setRotationSnap(on ? THREE.MathUtils.degToRad(15) : null);
}

function editFromInputs() {
  if (!sel) return;
  snapshot();
  const start = { inv: sel.node.matrixWorld.clone().invert(), att: carried().map((a) => [a, a.node.matrixWorld.clone()]) };
  setWorld(sel.node, [+$('px').value, +$('py').value, +$('pz').value], +$('ph').value, sel.rec);
  carry(start);
  refreshList();
}

function duplicate() {
  if (!sel) return;
  snapshot();
  const box = new THREE.Box3().setFromObject(sel.node);
  const step = Math.max(40, box.getSize(new THREE.Vector3()).x + 20);
  const p = posOf(sel), h = headingOf(sel);
  const off = [step, 0, 0];
  const item = makeCopy(sel.rec, [p[0] + off[0], p[1] + off[1], p[2]], h);
  if ($('carry').checked) {
    for (const a of sel.attached) {
      if (!a.node.visible) continue;
      const ap = posOf(a);
      item.attached.push(makeCopy(a.rec, [ap[0] + off[0], ap[1] + off[1], ap[2]], headingOf(a)));
    }
  }
  select(item);
}

function remove() {
  if (!sel) return;
  snapshot();
  const it = sel;
  if (it.copy) removeCopy(it);
  else {
    it.rec.deleted = true;
    it.node.visible = false;
    if ($('carry').checked) for (const a of it.attached) { if (a.copy) removeCopy(a); else { a.rec.deleted = true; a.node.visible = false; } }
  }
  select(null);
}

function resetSel() {
  if (!sel) return;
  snapshot();
  const rec = sel.rec;
  const recsToReset = [rec, ...($('carry').checked ? rec.item.attached.map((a) => a.rec) : [])];
  for (const r of recsToReset) {
    for (const c of [...r.copies]) removeCopy(c);
    r.deleted = false;
    r.node.visible = true;
    setWorldMatrix(r.node, r.m0);
  }
  select(rec.item);
}

// picking: a click that did not drag
let down = null;
canvas.addEventListener('pointerdown', (e) => { down = [e.clientX, e.clientY]; });
canvas.addEventListener('pointerup', (e) => {
  if (!down || Math.hypot(e.clientX - down[0], e.clientY - down[1]) > 4 || gizmo.dragging || e.button !== 0) return;
  const r = canvas.getBoundingClientRect();
  const ray = new THREE.Raycaster();
  ray.setFromCamera(new THREE.Vector2((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1), camera);
  const visible = (o) => { for (; o; o = o.parent) if (!o.visible) return false; return true; };
  for (const hit of ray.intersectObject(stageRoot || scene, true)) {
    if (!visible(hit.object)) continue;
    for (let o = hit.object; o; o = o.parent) {
      if (itemOf.has(o)) { select(itemOf.get(o)); return; }
    }
    break;      // the first visible thing is not an object (ground, a road): keep looking at nothing
  }
  select(null);
});

// ------------------------------------------------------------------ saving and testing
async function post(kind, save = true) {
  const mod = $('mod').value.trim();
  const body = { name: stageName, mod, patch: buildPatch(), training: true, save };
  try {
    log(`${kind}…`);
    const res = await api(kind === 'check' ? '/api/check' : `/api/${kind}`, body);
    log(res.lines, res.ok ? '' : 'err');
    if (res.saved) { log(`saved ${res.saved}`, 'ok'); dirty = false; }
    if (res.launched) { log(`game: ${res.launched}`, 'ok'); watchGame(); }
    if (kind !== 'check' && mod !== cfg.mod) { cfg.mod = mod; api('/api/config', { mod }); }
    refreshList();
  } catch (e) {
    log(`${kind}: ${e.message}`, 'err');
  }
}

// ------------------------------------------------------------------ wiring
$('stage').onchange = (e) => {
  if (dirty && !confirm('Unsaved changes on this stage. Load another one anyway?')) { e.target.value = stageName; return; }
  loadStage(e.target.value);
};
$('mod').onchange = () => { if (!dirty || confirm('Reload the stage with this mod\'s patch? Unsaved changes are lost.')) loadStage(stageName); };
$('filter').oninput = refreshList;
document.querySelectorAll('.kinds input').forEach((x) => (x.onchange = refreshList));
$('undo').onclick = undo;
$('check').onclick = () => post('check');
$('save').onclick = () => post('save');
$('test').onclick = () => {
  if (!dirty) { post('test', false); return; }
  $('testDlg').returnValue = '';
  $('testDlg').showModal();
};
$('testDlg').addEventListener('close', () => {
  const v = $('testDlg').returnValue;
  if (v === 'save' || v === 'nosave') post('test', v === 'save');
});
// what the stage patch did in the game: the [fase] lines of its log, a few seconds after the launch
async function watchGame() {
  let seen = 0;
  for (let i = 0; i < 20; i++) {
    await new Promise((r) => setTimeout(r, 1500));
    let lines = [];
    try { lines = (await fetch('/api/gamelog').then((r) => r.json())).lines || []; } catch (e) { return; }
    for (const l of lines.slice(seen)) log(`game ${l}`, /ignorad|invalid|falh/i.test(l) ? 'err' : '');
    seen = Math.max(seen, lines.length);
    if (lines.some((l) => /depois:|ignorad|desligad/i.test(l))) return;
  }
}
$('tMove').onclick = () => setTool('translate');
$('tTurn').onclick = () => setTool('rotate');
$('snap').onchange = (e) => setSnap(e.target.checked);
$('dup').onclick = duplicate;
$('del').onclick = remove;
$('reset').onclick = resetSel;
for (const id of ['px', 'py', 'pz', 'ph']) $(id).onchange = editFromInputs;
$('settings').onclick = () => {
  $('sDisc').value = cfg.disc || ''; $('sMods').value = cfg.mods || ''; $('sGame').value = cfg.game || '';
  $('sElf').value = cfg.elf || ''; $('sCwd').value = cfg.cwd || ''; $('sIso').value = cfg.iso || '';
  $('sTheme').value = document.documentElement.dataset.theme;
  $('dlg').returnValue = '';
  $('dlg').showModal();
};
$('sTheme').onchange = (e) => applyTheme(e.target.value);   // preview; Cancel goes back
$('dlg').addEventListener('close', async () => {
  if ($('dlg').returnValue !== 'ok') { applyTheme(cfg.theme || 'dark'); return; }
  const r = await api('/api/config', { disc: $('sDisc').value, mods: $('sMods').value, game: $('sGame').value, elf: $('sElf').value,
                                       cwd: $('sCwd').value, iso: $('sIso').value, theme: $('sTheme').value });
  init(r);
});
window.addEventListener('keydown', (e) => {
  if (e.target.matches('input, select, textarea')) return;
  const k = e.key.toLowerCase();
  if (e.ctrlKey && k === 'z') { e.preventDefault(); undo(); }
  else if (e.ctrlKey && k === 's') { e.preventDefault(); post('save'); }
  else if (e.ctrlKey && k === 'd') { e.preventDefault(); duplicate(); }
  else if (k === 'delete') remove();
  else if (k === 'w') setTool('translate');
  else if (k === 'e') setTool('rotate');
  else if (k === 'f' && sel) focus(sel);
  else if (k === 'escape') select(null);
});
window.addEventListener('beforeunload', (e) => { if (dirty) { e.preventDefault(); e.returnValue = ''; } });

function init(r) {
  cfg = r.config;
  applyTheme(cfg.theme || 'dark');
  $('mods').innerHTML = r.mods.map((m) => `<option value="${m}">`).join('');
  if (!$('mod').value) $('mod').value = cfg.mod || '';
  const s = $('stage');
  const prev = stageName || 'CENTRAL';
  s.innerHTML = r.stages.map((x) => `<option value="${x.name}">${x.title} (${x.name})</option>`).join('');
  if (!r.stages.length) { log(`no stages in ${cfg.disc}/LVL: set the disc folder in Settings`, 'err'); return; }
  s.value = r.stages.some((x) => x.name === prev) ? prev : r.stages[0].name;
  loadStage(s.value);
}

setTool('translate');
loop();
api('/api/init').then(init).catch((e) => log(`init: ${e.message}`, 'err'));
