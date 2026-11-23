// The 3D preview of the kit page: one renderer, moved into whatever box shows a model (a costume, a skin, a custom monster, a
// 3D file of the work folder). The model stands in a 3/4 view framed on its box; drag turns it, the wheel zooms.
// animBar() adds the list of the model's animations under it (names from anim_ids.json), with play, frame and speed.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

let renderer = null, scene, camera, controls, current = null, box = null, token = 0, ro = null, grid = null;
const loader = new GLTFLoader();
const clock = new THREE.Clock();
const play = { mixer: null, dur: 0, t: 0, playing: true, speed: 1, rate: 1, onFrame: null };   // speed: the slow-motion choice; rate: the move's own speed
const FPS = 60;

// The game's look (as the model museum and the port draw it): texture * vertex color * (ambient + directional * max(0, n.L)),
// the light coming from over the camera's shoulder, the colors as stored (no sRGB conversion). The game's triangles come from
// PS2 strips whose winding says nothing about the front: about half are wound against their normals, so every material is
// double-sided, and three.js would turn the normal of a triangle seen "from the back" around and light it from the wrong
// side (the dark patches). The game uses the normal as it is, and so does this: that flip is taken out of the shader.
const GAME_NORMALS = THREE.ShaderChunk.normal_fragment_begin.replace('normal *= faceDirection;', '');
function gameMaterial(m) {
  const n = new THREE.MeshLambertMaterial({ map: m.map, vertexColors: m.vertexColors, transparent: m.transparent, alphaTest: m.alphaTest,
                                            side: m.side, depthWrite: m.depthWrite, opacity: m.opacity, name: m.name });
  if (n.map) n.map.colorSpace = THREE.NoColorSpace;
  n.onBeforeCompile = sh => { sh.fragmentShader = sh.fragmentShader.replace('#include <normal_fragment_begin>', GAME_NORMALS); };
  n.customProgramCacheKey = () => 'gameNormals';
  m.dispose();
  return n;
}
export function gameLook(root) {
  root.traverse(o => {
    if (o.isMesh && o.material) o.material = Array.isArray(o.material) ? o.material.map(gameMaterial) : gameMaterial(o.material);
  });
  return root;
}
function gameLight(cam, yawDeg = 35, pitchDeg = 8) {
  const YAW = THREE.MathUtils.degToRad(yawDeg), PITCH = THREE.MathUtils.degToRad(pitchDeg);
  const dir = new THREE.Vector3(Math.sin(YAW) * Math.cos(PITCH), Math.sin(PITCH), -Math.cos(YAW) * Math.cos(PITCH));
  const c = new THREE.PerspectiveCamera();
  c.position.copy(dir);
  c.lookAt(0, 0, 0);
  c.updateMatrixWorld(true);
  const L = new THREE.Vector3(Math.sin(YAW - 0.7) * 0.6, 0.8, -Math.cos(YAW - 0.7) * 0.6).normalize();
  const local = c.worldToLocal(dir.clone().add(L)).normalize();
  const sun = new THREE.DirectionalLight(0xffffff, 0.6 * Math.PI);
  sun.position.copy(local);
  sun.target.position.set(0, 0, 0);
  cam.add(sun, sun.target);
  return new THREE.AmbientLight(0xffffff, 0.7 * Math.PI);
}

function setup() {
  renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, preserveDrawingBuffer: true });
  renderer.setPixelRatio(window.devicePixelRatio);
  THREE.ColorManagement.enabled = false;   // colors as written, like the textures
  renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
  scene = new THREE.Scene();
  camera = new THREE.PerspectiveCamera(35, 1, 0.1, 100000);
  scene.add(camera, gameLight(camera));
  grid = new THREE.GridHelper(3000, 60, 0x6a4a30, 0x2a2622);   // the ground, in game units, seen only through the fight camera
  grid.visible = false;
  scene.add(grid);
  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  ro = new ResizeObserver(size);
  renderer.setAnimationLoop(() => {
    const dt = clock.getDelta();
    if (!box || !box.isConnected) return;
    if (play.mixer && play.dur) {
      if (play.playing) play.t = (play.t + dt * play.speed * play.rate) % play.dur;
      play.mixer.setTime(play.t);
      if (play.onFrame) play.onFrame(play.t);
    }
    if (!fight) controls.update();
    renderer.render(scene, camera);
  });
}

// the fight camera (monsters.js Camera tab): cam = {pos, target, fov} in game units around the monster (+Y its front, Z up;
// the kit's models face glTF -Z, so game (x, y, z) is (x, z, -y) here), fov in radians from top to bottom; null: back to the
// free view (drag and wheel). The model stands with its origin on the ground at the middle of the view (frame()).
let fight = null;
const g2t = v => new THREE.Vector3(v[0], v[2], -v[1]);
function placeFight() {
  if (!fight || !camera) return;
  camera.position.copy(g2t(fight.pos));
  camera.fov = THREE.MathUtils.radToDeg(fight.fov);   // up to down: a wider box shows more at the sides, as a wider screen does
  camera.near = 1;
  camera.far = 100000;
  camera.lookAt(g2t(fight.target));
  camera.updateProjectionMatrix();
}
export function fightCamera(cam) {
  if (!renderer) setup();
  if (cam) {
    if (!fight) fight = { saved: { pos: camera.position.clone(), target: controls.target.clone(), fov: camera.fov } };
    Object.assign(fight, cam);
    controls.enabled = false;
    grid.visible = true;
    if (box) box.classList.add('fightcam');   // the 16:9 frame of the game's screen (style.css)
    placeFight();
  } else if (fight) {
    const s = fight.saved;
    fight = null;
    grid.visible = false;
    if (box) box.classList.remove('fightcam');
    camera.position.copy(s.pos);
    controls.target.copy(s.target);
    camera.fov = s.fov;
    camera.updateProjectionMatrix();
    controls.enabled = true;
    if (current) frame(current, true);
  }
}
function size() {
  if (!box) return;
  const w = Math.max(10, box.clientWidth), h = Math.max(10, box.clientHeight);
  renderer.setSize(w, h);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  placeFight();
}
// game = the model is in the game's axes (an export of the kit: its front is glTF -Z, the game's +Y); otherwise the glTF
// convention (a Blender export faces +Z). The camera stands on the front's side, 35 degrees off to its left.
function frame(obj, game) {
  // measured vertex by vertex with the bones applied: a skinned model whose scale is in its armature (an import from another
  // program) has a mesh box far from what is drawn
  obj.updateMatrixWorld(true);
  obj.traverse(o => { if (o.isSkinnedMesh) o.skeleton.update(); });
  let b = new THREE.Box3().setFromObject(obj, true);
  if (b.isEmpty()) b = new THREE.Box3().setFromObject(obj);
  const c = b.getCenter(new THREE.Vector3()), s = b.getSize(new THREE.Vector3());
  obj.position.sub(new THREE.Vector3(c.x, b.min.y, c.z));   // on the ground, in the middle
  const H = Math.max(s.y, s.x * 0.6, s.z * 0.6, 1e-3);
  const yaw = THREE.MathUtils.degToRad(35), d = H * 2.3;
  controls.target.set(0, s.y * 0.5, 0);
  const f = game ? -1 : 1;
  camera.position.set(f * Math.sin(yaw) * d, s.y * 0.6, f * Math.cos(yaw) * d);
  camera.near = H / 100;
  camera.far = H * 100;
  camera.updateProjectionMatrix();
  controls.update();
}
function drop() {
  sph = null;   // its meshes hang from the model's bones and go with it
  if (current) {
    scene.remove(current);
    current.traverse(o => {
      if (o.geometry) o.geometry.dispose();
      for (const m of [].concat(o.material || [])) { if (m.map) m.map.dispose(); m.dispose(); }
    });
    current = null;
  }
  play.mixer = null;
  play.dur = 0;
}
async function fetchGlb(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error((await r.text()) || r.statusText);
  const g = await loader.parseAsync(await r.arrayBuffer(), '');
  gameLook(g.scene);
  return g;
}

// shows the .glb at url in el (replacing what it had); resolves when it is drawn
export async function showModel(el, url, game = false) {
  if (!renderer) setup();
  const my = ++token;
  if (box !== el) { if (box) ro.unobserve(box); box = el; ro.observe(el); }
  fight = null;   // a new model: the free view (a Camera tab sets the fight camera again)
  controls.enabled = true;
  grid.visible = false;
  el.classList.remove('fightcam');
  el.innerHTML = '';
  el.appendChild(renderer.domElement);
  const msg = document.createElement('div');
  msg.className = 'v3dmsg';
  msg.textContent = 'Loading the model…';
  el.appendChild(msg);
  size();
  drop();
  try {
    const g = await fetchGlb(url);
    if (my !== token) return;
    current = g.scene;
    scene.add(current);
    frame(current, game || !!current.getObjectByName('WotM'));
    placeFight();
    msg.remove();
  } catch (e) {
    if (my === token) msg.textContent = 'No 3D preview: ' + (e.message || e);
  }
}

// the model in the same box playing the animation of the .glb at url (url null: back to still); keeps the camera
async function swapModel(url, still) {
  const my = ++token;
  const at = current ? current.position.clone() : null;
  const g = await fetchGlb(url || still);
  if (my !== token) return 0;
  drop();
  current = g.scene;
  current.traverse(o => { if (o.isMesh) o.frustumCulled = false; });   // skinned: its box is the rest one
  scene.add(current);
  if (at) current.position.copy(at); else frame(current, true);
  if (url && g.animations.length) {
    play.mixer = new THREE.AnimationMixer(current);
    play.mixer.clipAction(g.animations[0]).play();
    play.dur = g.animations[0].duration || 0;
    play.t = 0;
  }
  return play.dur;
}

// the animation bar of the model on show (animBar below), for the Moves tab: playMove(animId, [from, to], {locators, radius}) plays
// that animation of the model, marks the part of it (fractions of its length) where the move hits and, when the spheres are on,
// draws the hit sphere at each locator bone (the empties "loc_<hex id>" of the export): bright inside the hit window, dim outside
let animCtl = null;
export async function playMove(id, win, spheres, speed) { return animCtl ? animCtl.play(id, win, spheres, speed) : false; }
export function setMoveHit(win, spheres, speed) { if (animCtl) animCtl.setHit(win, spheres, speed); }

// the hit spheres: on by default, the choice is kept
let spheresOn = localStorage.getItem('wotm.hitSpheres') !== '0';
let sph = null;   // {win: [from, to], meshes: [THREE.Mesh]} of the model on show
export const hitSpheresOn = () => spheresOn;
export function setHitSpheres(on) {
  spheresOn = !!on;
  localStorage.setItem('wotm.hitSpheres', on ? '1' : '0');
  if (sph) for (const m of sph.meshes) m.visible = spheresOn;
}
function clearSpheres() {
  if (!sph) return;
  for (const m of sph.meshes) { m.removeFromParent(); m.geometry.dispose(); m.material.dispose(); }
  sph = null;
}
// radius in game units, on the locator bones of the model on show (the model's units are the game's)
function attachSpheres(win, spheres) {
  clearSpheres();
  if (!current || !spheres || !spheres.locators || !spheres.locators.length || !(spheres.radius > 0)) return;
  current.updateMatrixWorld(true);
  const meshes = [];
  for (const id of spheres.locators) {
    const node = current.getObjectByName('loc_' + id.toString(16));
    if (!node) continue;
    const m = new THREE.Mesh(new THREE.SphereGeometry(1, 20, 14), new THREE.MeshBasicMaterial({ color: 0x4f8cf0, transparent: true, opacity: 0.2, depthTest: false }));
    m.renderOrder = 10;
    m.scale.setScalar(spheres.radius / (node.getWorldScale(new THREE.Vector3()).x || 1));   // the bones may carry a scale
    m.visible = spheresOn;
    node.add(m);
    meshes.push(m);
  }
  sph = { win, meshes };
}
function tickSpheres(t) {
  if (!sph || !play.dur) return;
  const f = t / play.dur, on = sph.win && f >= sph.win[0] && f <= sph.win[1];
  for (const m of sph.meshes) {
    m.material.color.setHex(on ? 0xff5534 : 0x4f8cf0);
    m.material.opacity = on ? 0.55 : 0.18;
  }
}
// the name of an animation id: the monster's own name for the combo slot it plays (prefix = the monster type, "LIZARD"), else
// what the id is in general
export async function animName(id, prefix) {
  const n = await names();
  const c = n && n.combos && (n.combos[prefix] || {})[id];
  if (c && c.short) return c.short;
  return n && n.ids && n.ids[id] ? n.ids[id].name : '';
}

let animIds;
async function names() {
  if (animIds === undefined) animIds = await fetch('anim_ids.json').then(r => (r.ok ? r.json() : null)).catch(() => null);
  return animIds;
}

// the animation list under a model's box: group = the work folder group whose model shows; stillUrl = its still model
// opts.moves: the bar of the full-screen Moves view, where the picked move chooses the animation: no list of animations, the speed
// takes its place. Otherwise: the list and the speed on a line, play and the frame slider under it. The frame text is always the last
// line, so its length never changes the slider.
export async function animBar(el, group, stillUrl, opts = {}) {
  const mk = (tag, cls) => { const e = document.createElement(tag); if (cls) e.className = cls; return e; };
  const bar = mk('div', 'animbar');
  const sel = mk('select');   // (not in the page when opts.moves: the moves pick the animation by id)
  const o0 = document.createElement('option');
  o0.value = '';
  o0.textContent = 'Standing still (idle, frame 0)';
  sel.appendChild(o0);
  const pb = mk('button', 'pp');   // play / pause: one icon that toggles
  pb.type = 'button';
  pb.disabled = true;
  const ppIc = mk('i', 'mvic');
  pb.appendChild(ppIc);
  const setPP = playing => {
    const u = 'url(vendor/phosphor/' + (playing ? 'pause' : 'play') + '-fill.svg)';
    ppIc.style.webkitMaskImage = u;
    ppIc.style.maskImage = u;
    pb.title = playing ? 'Pause' : 'Play';
  };
  setPP(true);
  const fr = mk('input');
  fr.type = 'range';
  fr.min = 0;
  fr.max = 0;
  fr.disabled = true;
  const fn = mk('div', 'fn dim');
  const sp = mk('select', 'spd');
  sp.title = 'Playback speed';
  for (const v of [1, 0.5, 0.25]) { const o = document.createElement('option'); o.value = v; o.textContent = v + '×'; sp.appendChild(o); }
  const line = (...kids) => { const r = mk('div', 'arow'); r.append(...kids); return r; };
  if (opts.moves) bar.append(line(sp, pb, fr), fn);
  else bar.append(line(sel, sp), line(pb, fr), fn);
  el.appendChild(bar);
  const [list, ids] = await Promise.all([kit.api('do/anims', { group }), names()]);
  const groups = (ids && ids.groups) || [{ key: 'other', label: 'Animations' }];
  const by = new Map(groups.map(g => [g.key, []]));
  const seen = {};
  for (const a of list.anims || []) {
    const e = ids && ids.ids[a.id];
    seen[a.id] = (seen[a.id] || 0) + 1;
    let t = a.id + '  ' + (e ? e.name : 'Animation');
    const combo = ids && ids.combos && (ids.combos[list.prefix] || {})[a.id];
    if (combo && combo.short) t += ' (' + combo.short + ')';
    if (e && e.loop === 'layer') t += ' (layer)';
    if (seen[a.id] > 1) t += ' #' + seen[a.id];
    const k = e && by.has(e.group) ? e.group : groups[groups.length - 1].key;
    by.get(k).push([a.k, t, a.id]);
  }
  for (const g of groups) {
    const items = by.get(g.key).sort((x, y) => x[2] - y[2]);
    if (!items.length) continue;
    const og = document.createElement('optgroup');
    og.label = g.label;
    for (const [k, t] of items) { const o = document.createElement('option'); o.value = k; o.textContent = t; og.appendChild(o); }
    sel.appendChild(og);
  }
  // frames are the game's when a move plays (play.rate = the move's speed: the game runs the animation that much faster or slower),
  // else the animation's own
  const frames = () => Math.max(0, Math.round(play.dur * FPS / play.rate));
  // the hit window of a move (Moves tab): the part of the frame slider where the game tests the hit, drawn on the slider
  let hit = null;
  const markHit = () => {
    if (!hit) { fr.style.removeProperty('--track'); fr.classList.remove('hitwin'); return; }
    const a = Math.round(hit[0] * 100), b = Math.round(hit[1] * 100);
    fr.classList.add('hitwin');
    // the slider's track (style.css) with the hit part in red
    fr.style.setProperty('--track', 'linear-gradient(90deg, var(--line2) 0 ' + a + '%, var(--red) ' + a + '% ' + b + '%, var(--line2) ' + b + '% 100%)');
  };
  // hit = [from, to] as fractions of the animation, plus the same two as frame numbers when the caller has them (the Moves view's card)
  const hitText = () => (hit ? ' · hit frames ' + (hit.length > 3 ? hit[2] + '–' + hit[3] : Math.round(hit[0] * frames()) + '–' + Math.round(hit[1] * frames())) : '');
  const nowFrame = t => Math.round(t * FPS / play.rate);
  play.onFrame = t => {
    if (document.activeElement !== fr) fr.value = nowFrame(t);
    fn.textContent = 'frame ' + nowFrame(t) + ' of ' + frames() + hitText();
    tickSpheres(t);
  };
  // shows the animation of the table position k ('' = standing still); the list follows when it is there
  async function load(k) {
    pb.disabled = fr.disabled = true;
    fn.textContent = k === '' ? '' : 'Loading…';
    try {
      await swapModel(k === '' ? null : 'api/anim.glb?group=' + encodeURIComponent(group) + '&k=' + k + '&t=' + Date.now(), stillUrl);
    } catch (e) { fn.textContent = String(e.message || e); return; }
    play.playing = true;
    setPP(true);
    fr.max = frames();
    pb.disabled = fr.disabled = k === '';
    if (k === '') { fn.textContent = ''; hit = null; }
    markHit();
  }
  sel.onchange = () => { play.rate = 1; return load(sel.value); };
  // first table position of each animation id (ids repeat); a move of the Moves tab plays its animation by id
  const firstK = new Map();
  for (const a of list.anims || []) if (!firstK.has(a.id)) firstK.set(a.id, a.k);
  animCtl = {
    // speed: the move's own (frames and playback follow the game's)
    async play(id, win, spheres, speed) {
      const k = firstK.get(id);
      if (k === undefined) return false;
      hit = null;   // the old window must not show on the new animation while it loads
      sel.value = String(k);
      play.rate = speed > 0 ? speed : 1;
      await load(String(k));
      hit = win || null;
      markHit();
      attachSpheres(win, spheres);
      return true;
    },
    // the same animation with other numbers (a radius, a window or the speed changed in the Moves tab)
    setHit(win, spheres, speed) {
      hit = win || null;
      if (speed > 0 && speed !== play.rate) { play.rate = speed; fr.max = frames(); }
      markHit();
      if (play.mixer) attachSpheres(win, spheres);
    },
  };
  pb.onclick = () => { play.playing = !play.playing; setPP(play.playing); };
  fr.oninput = () => { play.playing = false; setPP(false); play.t = parseInt(fr.value, 10) * play.rate / FPS; };
  sp.onchange = () => { play.speed = parseFloat(sp.value); };
  play.speed = 1;
  play.rate = 1;
  return bar;
}
window.kitViewer = {
  shown: () => !!current && !!box && box.isConnected,
  playing: () => ({ dur: play.dur, t: play.t, mixer: !!play.mixer }),
  spheres: () => (sph ? sph.meshes.map(m => ({ visible: m.visible, color: m.material.color.getHex(), r: m.scale.x, w: m.getWorldPosition(new THREE.Vector3()).toArray() })) : []),
};
