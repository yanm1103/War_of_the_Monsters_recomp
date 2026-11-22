// The 3D preview of the kit page: one renderer, moved into whatever box shows a model (a costume, a skin, a custom monster, a
// 3D file of the work folder). The model stands in a 3/4 view framed on its box; drag turns it, the wheel zooms.
// animBar() adds the list of the model's animations under it (names from anim_ids.json), with play, frame and speed.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

let renderer = null, scene, camera, controls, current = null, box = null, token = 0, ro = null;
const loader = new GLTFLoader();
const clock = new THREE.Clock();
const play = { mixer: null, dur: 0, t: 0, playing: true, speed: 1, onFrame: null };
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
  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  ro = new ResizeObserver(size);
  renderer.setAnimationLoop(() => {
    const dt = clock.getDelta();
    if (!box || !box.isConnected) return;
    if (play.mixer && play.dur) {
      if (play.playing) play.t = (play.t + dt * play.speed) % play.dur;
      play.mixer.setTime(play.t);
      if (play.onFrame) play.onFrame(play.t);
    }
    controls.update();
    renderer.render(scene, camera);
  });
}
function size() {
  if (!box) return;
  const w = Math.max(10, box.clientWidth), h = Math.max(10, box.clientHeight);
  renderer.setSize(w, h);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}
// game = the model is in the game's axes (an export of the kit: its front is glTF -Z, the game's +Y); otherwise the glTF
// convention (a Blender export faces +Z). The camera stands on the front's side, 35 degrees off to its left.
function frame(obj, game) {
  const b = new THREE.Box3().setFromObject(obj);
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

let animIds;
async function names() {
  if (animIds === undefined) animIds = await fetch('anim_ids.json').then(r => (r.ok ? r.json() : null)).catch(() => null);
  return animIds;
}

// the animation list under a model's box: group = the work folder group whose model shows; stillUrl = its still model
export async function animBar(el, group, stillUrl) {
  const bar = document.createElement('div');
  bar.className = 'animbar';
  const sel = document.createElement('select');
  const o0 = document.createElement('option');
  o0.value = '';
  o0.textContent = 'Standing still (idle, frame 0)';
  sel.appendChild(o0);
  const pb = document.createElement('button');
  pb.type = 'button';
  pb.textContent = 'Pause';
  pb.disabled = true;
  const fr = document.createElement('input');
  fr.type = 'range';
  fr.min = 0;
  fr.max = 0;
  fr.disabled = true;
  const fn = document.createElement('span');
  fn.className = 'dim';
  const sp = document.createElement('select');
  for (const v of [1, 0.5, 0.25]) { const o = document.createElement('option'); o.value = v; o.textContent = v + '×'; sp.appendChild(o); }
  bar.append(sel, pb, fr, fn, sp);
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
  const frames = () => Math.max(0, Math.round(play.dur * FPS));
  play.onFrame = t => { if (document.activeElement !== fr) fr.value = Math.round(t * FPS); fn.textContent = Math.round(t * FPS) + ' / ' + frames(); };
  sel.onchange = async () => {
    const k = sel.value;
    pb.disabled = fr.disabled = true;
    fn.textContent = k === '' ? '' : 'Loading…';
    try {
      await swapModel(k === '' ? null : 'api/anim.glb?group=' + encodeURIComponent(group) + '&k=' + k + '&t=' + Date.now(), stillUrl);
    } catch (e) { fn.textContent = String(e.message || e); return; }
    play.playing = true;
    pb.textContent = 'Pause';
    fr.max = frames();
    pb.disabled = fr.disabled = k === '';
    if (k === '') fn.textContent = '';
  };
  pb.onclick = () => { play.playing = !play.playing; pb.textContent = play.playing ? 'Pause' : 'Play'; };
  fr.oninput = () => { play.playing = false; pb.textContent = 'Play'; play.t = parseInt(fr.value, 10) / FPS; };
  sp.onchange = () => { play.speed = parseFloat(sp.value); };
  return bar;
}
window.kitViewer = {
  shown: () => !!current && !!box && box.isConnected,
  playing: () => ({ dur: play.dur, t: play.t, mixer: !!play.mixer }),
};
