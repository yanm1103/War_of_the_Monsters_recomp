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
const boxes = [];   // one outline per selected item; the primary one (gizmo, fields) in yellow
const grid = new THREE.GridHelper(20000, 200, 0x8a96a8, 0x566070);
grid.material.transparent = true;
grid.material.opacity = 0.45;
grid.visible = false;
scene.add(grid);
let stageRoot = null;
const store = {
  get(k, d) { try { const v = localStorage.getItem('wotmLevelEditor.' + k); return v === null ? d : v; } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem('wotmLevelEditor.' + k, v); } catch (e) { /* no storage */ } },
};

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

// ------------------------------------------------------------------ camera: fly (WASD, default) or orbit; V switches
// fly: W/S move where the camera looks, A/D sideways, E up, Q down, Shift faster; right drag looks, the wheel moves forward. orbit: OrbitControls (left drag orbits, right drag pans, wheel zooms).
const fly = { on: true, yaw: 0, pitch: -0.6, keys: new Set(), last: performance.now(), speed: 900 };
try { fly.on = localStorage.getItem('wotmLevelEditor.view') !== 'orbit'; } catch (e) { /* no storage: fly */ }
camera.rotation.order = 'YXZ';

function flyFromCamera() {   // yaw/pitch of where the camera looks now
  const d = camera.getWorldDirection(new THREE.Vector3());
  fly.yaw = Math.atan2(-d.x, -d.z);
  fly.pitch = Math.asin(THREE.MathUtils.clamp(d.y, -1, 1));
}
function lookAtPoint(p) {
  camera.lookAt(p);
  flyFromCamera();
}
function setView(on) {
  if (on) flyFromCamera();
  else {   // orbit around what is in the middle of the view (or a point ahead)
    const ray = new THREE.Raycaster(); ray.setFromCamera(new THREE.Vector2(0, 0), camera);
    const hit = stageRoot ? ray.intersectObject(stageRoot, true).find((h) => h.object.visible) : null;
    orbit.target.copy(hit ? hit.point : camera.position.clone().addScaledVector(camera.getWorldDirection(new THREE.Vector3()), 1000));
  }
  fly.on = on;
  orbit.enabled = !on && !gizmo.dragging;
  fly.keys.clear();
  try { localStorage.setItem('wotmLevelEditor.view', on ? 'fly' : 'orbit'); } catch (e) { /* ignore */ }
  const common = 'Click: select · Shift+click: add · Shift+drag: box · Ctrl+A: all listed · G move · R turn · Arrows/PgUp/PgDn nudge · End drop · F focus · Ctrl+D copy · Del delete';
  $('hint').textContent = (on
    ? 'Fly view (V: orbit) · WASD move · E up · Q down · Shift fast · Right drag: look · Wheel: forward · '
    : 'Orbit view (V: fly) · Left drag: orbit · Right drag: pan · Wheel: zoom · ') + common;
}
function flyStep() {
  const now = performance.now(), dt = Math.min(0.1, (now - fly.last) / 1000);
  fly.last = now;
  if (!fly.on) return;
  const k = fly.keys, fast = k.has('shift') ? 3 : 1, step = fly.speed * fast * dt;
  const cp = Math.cos(fly.pitch);
  const fwd = new THREE.Vector3(-Math.sin(fly.yaw) * cp, Math.sin(fly.pitch), -Math.cos(fly.yaw) * cp);   // where the camera looks
  const right = new THREE.Vector3(Math.cos(fly.yaw), 0, -Math.sin(fly.yaw));
  const mv = new THREE.Vector3();
  if (k.has('w')) mv.add(fwd);
  if (k.has('s')) mv.sub(fwd);
  if (k.has('d')) mv.add(right);
  if (k.has('a')) mv.sub(right);
  if (mv.lengthSq()) camera.position.addScaledVector(mv.normalize(), step);
  if (k.has('e')) camera.position.y += step;
  if (k.has('q')) camera.position.y -= step;
  camera.rotation.set(fly.pitch, fly.yaw, 0);
}
let look = null;   // right drag in the fly view
canvas.addEventListener('contextmenu', (e) => e.preventDefault());
canvas.addEventListener('pointerdown', (e) => {
  if (fly.on && e.button === 2) { look = [e.clientX, e.clientY]; canvas.setPointerCapture(e.pointerId); }
});
canvas.addEventListener('pointermove', (e) => {
  if (!look || !fly.on) return;
  fly.yaw -= (e.clientX - look[0]) * 0.004;
  fly.pitch = THREE.MathUtils.clamp(fly.pitch - (e.clientY - look[1]) * 0.004, -1.55, 1.55);
  look = [e.clientX, e.clientY];
});
canvas.addEventListener('pointerup', (e) => { if (e.button === 2) look = null; });
canvas.addEventListener('wheel', (e) => {
  if (!fly.on) return;
  e.preventDefault();
  camera.position.addScaledVector(camera.getWorldDirection(new THREE.Vector3()), -e.deltaY * (e.shiftKey ? 6 : 2));
}, { passive: false });
window.addEventListener('blur', () => fly.keys.clear());
window.addEventListener('keyup', (e) => fly.keys.delete(e.key.toLowerCase()));

function loop() {
  requestAnimationFrame(loop);
  flyStep();
  if (orbit.enabled) orbit.update();
  syncBoxes();
  renderer.render(scene, camera);
}

// ------------------------------------------------------------------ model
// rec: one object of the stage (handle). item: what can be selected, the object itself or one of its copies.
// group: every selected item; sel: the one the gizmo and the fields act on (the last picked), the others follow it.
let cfg = {}, stageName = '', recs = [], byHandle = new Map(), itemOf = new Map(), sel = null, group = [], history = [], dirty = false;

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
  const keep = group.filter((it) => !it.copy).map((it) => it.rec.item);
  resetAll();
  applyPatch(p);
  selectMany(keep);
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
  lookAtPoint(c);
}
function focus(item) {
  const box = new THREE.Box3().setFromObject(item.node);
  if (box.isEmpty()) return;
  const c = box.getCenter(new THREE.Vector3()), r = Math.max(150, box.getSize(new THREE.Vector3()).length());
  const dir = fly.on ? camera.getWorldDirection(new THREE.Vector3()).negate() : camera.position.clone().sub(orbit.target).normalize();
  orbit.target.copy(c);
  camera.position.copy(c).addScaledVector(dir, r * 1.4);
  if (fly.on) lookAtPoint(c);
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
function shownKinds() {
  return new Set([...document.querySelectorAll('.kinds input[data-kind]')].filter((x) => x.checked).map((x) => x.dataset.kind));
}
function listed() {   // the items the list shows: kind boxes, "Changed" and the filter text
  const f = $('filter').value.trim().toLowerCase(), kinds = shownKinds(), onlyChanged = $('changedOnly').checked;
  return items().filter((it) => {
    const i = it.rec.info;
    if (!kinds.has(i.kind) || (onlyChanged && !changed(it.rec))) return false;
    return !f || `${label(it)} ${i.kind} ${i.id.toString(16)}`.toLowerCase().includes(f);
  });
}
function refreshList() {
  const ul = $('list');
  ul.textContent = '';
  const shown = listed();
  for (const it of shown) {
    const i = it.rec.info;
    const li = document.createElement('li');
    li.innerHTML = `<span class="k">${i.kind}</span><span class="h"></span>`;
    li.querySelector('.h').textContent = label(it);
    if (it === sel) li.className = 'sel';
    else if (group.includes(it)) li.className = 'sel2';
    if (!it.copy && changed(it.rec)) li.classList.add('changed');
    if (!it.copy && it.rec.deleted) li.classList.add('deleted');
    li.onclick = (e) => {
      if (e.shiftKey || e.ctrlKey) select(it, 'toggle');
      else { select(it); focus(it); }
    };
    ul.appendChild(li);
  }
  $('count').textContent = `${shown.length} shown · ${group.length > 1 ? group.length + ' selected · ' : ''}${buildPatch().objects.length} changed`;
  document.title = `WotM Level Editor — ${stageName}${dirty ? ' *' : ''}`;
}

// mode: 'set' (only this one), 'toggle' (Shift/Ctrl click), 'add'
function select(item, mode = 'set') {
  if (item && !item.node.visible) item = null;
  if (mode === 'set') group = item ? [item] : [];
  else if (item) {
    const i = group.indexOf(item);
    if (i < 0) group.push(item);
    else if (mode === 'toggle') group.splice(i, 1);
  }
  sel = item && group.includes(item) ? item : group[group.length - 1] || null;
  showSel();
}
function selectMany(list, add = false) {
  const vis = list.filter((it) => it.node.visible);
  group = add ? [...group, ...vis.filter((it) => !group.includes(it))] : vis;
  sel = group[group.length - 1] || null;
  showSel();
}
function showSel() {
  gizmo.detach();
  $('props').hidden = !sel;
  $('noSel').hidden = !!sel;
  if (sel) {
    gizmo.attach(sel.node);
    const i = sel.rec.info;
    $('pTitle').textContent = label(sel);
    $('pSub').textContent = `${i.kind} · id 0x${i.id.toString(16)} · ${i.states} states · ${i.paths} collision paths` +
      (i.on ? ` · on ${i.on}` : '');
    $('pGroup').textContent = group.length > 1 ? `${group.length} selected: the others follow this one.` : '';
    $('onCount').textContent = group.reduce((n, it) => n + it.attached.length, 0);
    $('pWarn').textContent = i.paths === 0 && i.kind !== 'pickup' ? 'No collision of its own.' : '';
    showProps();
  }
  syncBoxes();
  refreshList();
}
function syncBoxes() {
  while (boxes.length < group.length) { const b = new THREE.BoxHelper(undefined, 0xffcc33); scene.add(b); boxes.push(b); }
  boxes.forEach((b, n) => {
    const it = group[n];
    b.visible = !!it && it.node.visible;
    if (!b.visible) return;
    b.setFromObject(it.node);
    b.material.color.set(it === sel ? 0xffcc33 : 0xff8a3d);
  });
  if (grid.visible) {   // at the base of the selection, else at the stage's origin
    const box = sel ? new THREE.Box3().setFromObject(sel.node) : null;
    grid.position.y = box && !box.isEmpty() ? box.min.y + 0.5 : 0;
  }
}
function showProps() {
  if (!sel) return;
  const p = posOf(sel), h = headingOf(sel);
  $('px').value = r1(p[0]); $('py').value = r1(p[1]); $('pz').value = r1(p[2]); $('ph').value = r1(h);
}

// ------------------------------------------------------------------ editing
// Every change moves sel; the rest of the group and what they carry follow it by the same world delta.
function movers() {
  const out = new Set(group.filter((it) => it !== sel && it.node.visible));
  if ($('carry').checked) for (const it of group) for (const a of it.attached) if (a.node.visible) out.add(a);
  out.delete(sel);
  return [...out];
}
function startMove() { return { inv: sel.node.matrixWorld.clone().invert(), att: movers().map((a) => [a, a.node.matrixWorld.clone()]) }; }
function carry(start) {
  const delta = sel.node.matrixWorld.clone().multiply(start.inv);
  for (const [a, m0] of start.att) setWorldMatrix(a.node, delta.clone().multiply(m0));
}

let dragStart = null;
gizmo.addEventListener('dragging-changed', (e) => {
  orbit.enabled = !e.value && !fly.on;
  if (e.value && sel) {
    snapshot();
    dragStart = startMove();
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

function setTool(mode) {
  gizmo.setMode(mode);
  gizmo.showX = gizmo.showZ = mode !== 'rotate';
  gizmo.showY = true;
  $('tMove').classList.toggle('on', mode === 'translate');
  $('tTurn').classList.toggle('on', mode === 'rotate');
}
function snapMove() { return Math.max(0.1, +$('snapMove').value || 10); }
function snapTurn() { return Math.max(0.1, +$('snapTurn').value || 15); }
function setSnap() {
  const on = $('snap').checked;
  gizmo.setTranslationSnap(on ? snapMove() : null);
  gizmo.setRotationSnap(on ? THREE.MathUtils.degToRad(snapTurn()) : null);
  store.set('snap', on ? '1' : '0'); store.set('snapMove', snapMove()); store.set('snapTurn', snapTurn());
}

function editFromInputs() {
  if (!sel) return;
  snapshot();
  const start = startMove();
  setWorld(sel.node, [+$('px').value, +$('py').value, +$('pz').value], +$('ph').value, sel.rec);
  carry(start);
  refreshList();
}

// arrows: along the world axis nearest to where the camera looks (Shift ×10); PageUp/PageDown: up and down
let lastNudge = -Infinity;
function nudge(fwd, side, up, big) {
  if (!sel) return;
  const now = performance.now();
  if (now - lastNudge > 700) snapshot();   // one undo step per burst of key repeats
  lastNudge = now;
  const d = camera.getWorldDirection(new THREE.Vector3());
  const f = Math.abs(d.x) > Math.abs(d.z) ? new THREE.Vector3(Math.sign(d.x), 0, 0) : new THREE.Vector3(0, 0, Math.sign(d.z) || -1);
  const right = new THREE.Vector3(-f.z, 0, f.x);
  const step = ($('snap').checked ? snapMove() : 10) * (big ? 10 : 1);
  const v = f.multiplyScalar(fwd).addScaledVector(right, side).add(new THREE.Vector3(0, up, 0)).multiplyScalar(step);
  const start = startMove();
  setWorldMatrix(sel.node, new THREE.Matrix4().makeTranslation(v.x, v.y, v.z).multiply(sel.node.matrixWorld));
  carry(start);
  showProps();
  refreshList();
}

function visibleChain(o) { for (; o; o = o.parent) if (!o.visible) return false; return true; }

// End: each selected item down (or up) onto the first surface under its middle, its own meshes and what it carries aside
function dropToGround() {
  if (!group.length || !stageRoot) return;
  snapshot();
  const ray = new THREE.Raycaster(), down = new THREE.Vector3(0, -1, 0);
  let n = 0;
  for (const it of group) {
    if (!it.node.visible) continue;
    const att = $('carry').checked ? it.attached.filter((a) => a.node.visible && !group.includes(a)) : [];
    const skip = new Set();
    for (const x of [it, ...att]) x.node.traverse((o) => skip.add(o));
    const box = new THREE.Box3().setFromObject(it.node);
    if (box.isEmpty()) continue;
    const c = box.getCenter(new THREE.Vector3());
    ray.set(new THREE.Vector3(c.x, box.max.y + 1, c.z), down);
    const hit = ray.intersectObject(stageRoot, true).find((h) => !skip.has(h.object) && visibleChain(h.object));
    if (!hit) continue;
    const m = new THREE.Matrix4().makeTranslation(0, hit.point.y - box.min.y, 0);
    for (const x of [it, ...att]) setWorldMatrix(x.node, m.clone().multiply(x.node.matrixWorld));
    n++;
  }
  log(`dropped ${n} of ${group.length} on the surface below` + (n < group.length ? ' (nothing under the others)' : ''));
  showProps();
  refreshList();
}

function duplicate() {
  if (!sel) return;
  snapshot();
  const box = new THREE.Box3();
  for (const it of group) box.expandByObject(it.node);
  const step = Math.max(40, box.getSize(new THREE.Vector3()).x + 20);
  const made = [], done = new Set();
  const copyOf = (it) => {
    const p = posOf(it);
    const c = makeCopy(it.rec, [p[0] + step, p[1], p[2]], headingOf(it));
    done.add(it);
    return c;
  };
  for (const it of group) {
    if (!it.node.visible || done.has(it)) continue;
    const c = copyOf(it);
    made.push(c);
    if ($('carry').checked) for (const a of it.attached) if (a.node.visible && !group.includes(a)) c.attached.push(copyOf(a));
  }
  selectMany(made);
}

function hideRec(it) {
  if (it.copy) { if (it.rec.copies.includes(it)) removeCopy(it); }
  else { it.rec.deleted = true; it.node.visible = false; }
}
function remove() {
  if (!group.length) return;
  snapshot();
  for (const it of [...group]) {
    hideRec(it);
    if ($('carry').checked) for (const a of it.attached) hideRec(a);
  }
  select(null);
}

function resetSel() {
  if (!group.length) return;
  snapshot();
  const rs = new Set();
  for (const it of group) {
    rs.add(it.rec);
    if ($('carry').checked) for (const a of it.rec.item.attached) rs.add(a.rec);
  }
  for (const r of rs) {
    for (const c of [...r.copies]) removeCopy(c);
    r.deleted = false;
    r.node.visible = true;
    setWorldMatrix(r.node, r.m0);
  }
  selectMany([...rs].map((r) => r.item));
}

// picking: a click that did not drag (Shift/Ctrl: add or take out); Shift + left drag: box select
const ndc = (e) => {
  const r = canvas.getBoundingClientRect();
  return new THREE.Vector2((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1);
};
let down = null, band = null;
canvas.parentElement.addEventListener('pointerdown', (e) => {   // capture: before OrbitControls sees it
  down = [e.clientX, e.clientY];
  if (e.button === 0 && e.shiftKey && gizmo.axis === null) {
    band = { x: e.clientX, y: e.clientY };
    orbit.enabled = false;
  }
}, true);
canvas.addEventListener('pointermove', (e) => {
  if (!band) return;
  const r = canvas.getBoundingClientRect(), el = $('band');
  Object.assign(el.style, {
    left: Math.min(band.x, e.clientX) - r.left + 'px', top: Math.min(band.y, e.clientY) - r.top + 'px',
    width: Math.abs(e.clientX - band.x) + 'px', height: Math.abs(e.clientY - band.y) + 'px',
  });
  el.hidden = Math.hypot(e.clientX - band.x, e.clientY - band.y) < 5;
});
canvas.addEventListener('pointerup', (e) => {
  const b = band;
  band = null;
  $('band').hidden = true;
  orbit.enabled = !fly.on && !gizmo.dragging;
  if (b && Math.hypot(e.clientX - b.x, e.clientY - b.y) >= 5) {
    const a = ndc({ clientX: b.x, clientY: b.y }), z = ndc(e);
    const x0 = Math.min(a.x, z.x), x1 = Math.max(a.x, z.x), y0 = Math.min(a.y, z.y), y1 = Math.max(a.y, z.y);
    const kinds = shownKinds();
    const inside = items().filter((it) => {
      if (!kinds.has(it.rec.info.kind) || !visibleChain(it.node)) return false;
      const bx = new THREE.Box3().setFromObject(it.node);
      if (bx.isEmpty()) return false;
      const p = bx.getCenter(new THREE.Vector3()).project(camera);
      return p.z < 1 && p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
    });
    selectMany(inside, true);
    return;
  }
  if (!down || Math.hypot(e.clientX - down[0], e.clientY - down[1]) > 4 || gizmo.dragging || e.button !== 0) return;
  const ray = new THREE.Raycaster();
  ray.setFromCamera(ndc(e), camera);
  const add = e.shiftKey || e.ctrlKey;
  for (const hit of ray.intersectObject(stageRoot || scene, true)) {
    if (!visibleChain(hit.object)) continue;
    for (let o = hit.object; o; o = o.parent) {
      if (itemOf.has(o)) { select(itemOf.get(o), add ? 'toggle' : 'set'); return; }
    }
    break;      // the first visible thing is not an object (ground, a road): keep looking at nothing
  }
  if (!add) select(null);
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
for (const id of ['snap', 'snapMove', 'snapTurn']) $(id).onchange = setSnap;
$('snap').checked = store.get('snap', '0') === '1';
$('snapMove').value = store.get('snapMove', '10');
$('snapTurn').value = store.get('snapTurn', '15');
setSnap();
$('grid').checked = store.get('grid', '0') === '1';
grid.visible = $('grid').checked;
$('grid').onchange = (e) => { grid.visible = e.target.checked; store.set('grid', e.target.checked ? '1' : '0'); };
$('drop').onclick = dropToGround;
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
  if (e.target.matches?.('input, select, textarea')) return;
  const k = e.key.toLowerCase();
  if (e.ctrlKey && k === 'z') { e.preventDefault(); undo(); }
  else if (e.ctrlKey && k === 's') { e.preventDefault(); post('save'); }
  else if (e.ctrlKey && k === 'd') { e.preventDefault(); duplicate(); }
  else if (e.ctrlKey && k === 'a') { e.preventDefault(); selectMany(listed()); }
  else if (k === 'delete') remove();
  else if (e.ctrlKey || e.altKey || e.metaKey) return;
  else if (k.startsWith('arrow') && sel) {
    e.preventDefault();
    nudge(k === 'arrowup' ? 1 : k === 'arrowdown' ? -1 : 0, k === 'arrowright' ? 1 : k === 'arrowleft' ? -1 : 0, 0, e.shiftKey);
  }
  else if ((k === 'pageup' || k === 'pagedown') && sel) { e.preventDefault(); nudge(0, 0, k === 'pageup' ? 1 : -1, e.shiftKey); }
  else if (k === 'end') { e.preventDefault(); dropToGround(); }
  else if (fly.on && 'wasdqe'.includes(k) && k.length === 1) { e.preventDefault(); fly.keys.add(k); }
  else if (fly.on && k === 'shift') fly.keys.add(k);
  else if (k === 'v') setView(!fly.on);
  else if (k === 'g') setTool('translate');
  else if (k === 'r') setTool('rotate');
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
orbit.enabled = false;
setView(fly.on);
if (new URLSearchParams(location.search).has('debug')) {
  window.wotmDebug = { camera, fly, flyStep, setView, select, selectMany, items, listed, dropToGround, nudge, duplicate, remove,
                       undo, buildPatch, posOf, get group() { return group; }, get sel() { return sel; } };
}
loop();
api('/api/init').then(init).catch((e) => log(`init: ${e.message}`, 'err'));
