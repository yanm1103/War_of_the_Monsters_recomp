// WotM level editor page: the stage from /api/glb (tools/lvlglb.py), objects and patch from /api/stage (tools/lvl.py).
// Game coordinates are Z up (x, y, z); the scene is Y up: three = (x, z, -y). A heading turns about the game's Z, which is
// the scene's +Y with the same sign. The patch (docs/guides/MODS.md, "Level patches") is rebuilt from the scene each time.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';
import { createPeds } from './peds.js';
import { openHud } from './hud.js';

// The GS modulates and blends the bytes as they are: no color management (texture, vertex and interface colors stay raw).
THREE.ColorManagement.enabled = false;

// The GS's fog: F = clamp(burn + FA + FB / W, 0, 255) per vertex, W the depth in front of the camera (game units), and the GS mixes
// (F * C + (255 - F) * FOGCOL) / 255 before the alpha blend. scene.fog is a THREE.Fog whose near holds FA and far FB (burn: 1).
// The .MRS's maxV caps F (gsFogMax; 0 in a material that does not set it, read as 255).
THREE.ShaderChunk.fog_pars_vertex = '#ifdef USE_FOG\n\tuniform float fogNear;\n\tuniform float fogFar;\n\tuniform float gsFogMax;\n\tvarying float vGsFog;\n#endif';
THREE.ShaderChunk.fog_vertex = '#ifdef USE_FOG\n\tfloat gsW = - mvPosition.z;\n' +
  '\tvGsFog = gsW > 0.001 ? clamp( 1.0 + fogNear + fogFar / gsW, 0.0, gsFogMax > 0.0 ? gsFogMax : 255.0 ) / 255.0 : 1.0;\n#endif';
THREE.ShaderChunk.fog_pars_fragment = '#ifdef USE_FOG\n\tuniform vec3 fogColor;\n\tvarying float vGsFog;\n#endif';
THREE.ShaderChunk.fog_fragment = '#ifdef USE_FOG\n\tgl_FragColor.rgb = mix( fogColor, gl_FragColor.rgb, vGsFog );\n#endif';

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
  if (!r.ok) throw Object.assign(new Error(j.error || r.statusText), { status: r.status, code: j.code, data: j });
  return j;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const store = {
  get(k, d) { try { const v = localStorage.getItem('wotmLevelEditor.' + k); return v === null ? d : v; } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem('wotmLevelEditor.' + k, v); } catch (e) { /* no storage */ } },
};

// ------------------------------------------------------------------ messages: the Output drawer, the status bar and toasts
// The Output has three tabs: Messages (everything the page says), Check (the last check, its problems and the raw lvl.py output)
// and Game (the game's [fase] lines, in words and as they are). The drawer opens by itself only on an error.
const ICONS = { ok: 'check-circle-outline', err: 'alert-circle-outline', warn: 'alert-outline', info: 'information-outline',
                muted: 'information-outline' };
const PANES = { messages: 'log', check: 'checkOut', game: 'gameOut' };
const icon = (name) => {
  const i = document.createElement('i');
  i.className = 'ic';
  i.style.setProperty('--ic', `url(vendor/mdi/${name}.svg)`);
  return i;
};
const clock = (ms = Date.now()) => new Date(ms).toTimeString().slice(0, 8);
const hhmm = (ms) => new Date(ms).toTimeString().slice(0, 5);
let outTab = 'messages';
// one line: the time, an icon and the text; o.detail goes under a "Details" fold, o.go makes the line clickable
function lineEl(text, level = 'info', o = {}) {
  const d = document.createElement('div');
  d.className = `line ${level}${o.raw ? ' raw' : ''}`;
  const t = document.createElement('span'), x = document.createElement('span');
  t.className = 't'; t.textContent = clock();
  x.className = 'x'; x.textContent = text;
  d.append(t, icon(ICONS[level] || ICONS.info), x);
  if (o.detail) {
    const det = document.createElement('details');
    det.innerHTML = '<summary>Details</summary><pre></pre>';
    det.querySelector('pre').textContent = o.detail;
    x.appendChild(det);
  }
  if (o.go) { d.classList.add('go'); d.title = o.goTitle || ''; d.onclick = o.go; }
  return d;
}
function markTab(tab, err) {
  if (!$('output').hidden && outTab === tab) return;
  const b = document.querySelector(`#output .tab[data-tab="${tab}"]`);
  b.classList.add('new');
  if (err) b.classList.add('err');
}
function addLine(tab, el) {
  const pane = $(PANES[tab]);
  const stick = pane.hidden || pane.scrollTop + pane.clientHeight >= pane.scrollHeight - 4;
  pane.querySelector(':scope > .empty')?.remove();
  pane.appendChild(el);
  while (pane.childElementCount > 2000) pane.firstElementChild.remove();
  if (stick) pane.scrollTop = pane.scrollHeight;
  markTab(tab, el.classList.contains('err'));
}
function setStatusMsg(text, level, tab) {
  const b = document.querySelector('#status .msg');
  b.className = `st msg ${level}`;
  b.dataset.tab = tab;
  b.querySelector('.ic').style.setProperty('--ic', `url(vendor/mdi/${ICONS[level] || ICONS.info}.svg)`);
  b.querySelector('.tx').textContent = text;
  b.title = `${text} (opens the Output)`;
}
// msg(text or lines, {level, tab, toast, detail, status, open, raw, go}): lines in the Output and the last one in the status bar.
// level: info, ok, warn, err or muted; tab: messages (default), check or game. toast: true, or [label, fn] for a button on it; left
// out, a line shows as a toast too while a dialog hides the Output. An error opens the drawer on its tab (open: another tab, or false).
function msg(text, o = {}) {
  const level = o.level || 'info', tab = o.tab || 'messages';
  const lines = [].concat(text).filter((t) => t !== '' && t !== null && t !== undefined).map(String);
  if (!lines.length) return;
  for (const t of lines) addLine(tab, lineEl(t, level, o));
  const last = lines[lines.length - 1];
  if (level !== 'muted' && o.status !== false) setStatusMsg(last, level, tab);
  const tst = o.toast !== undefined ? o.toast : !!document.querySelector('dialog[open]') && level !== 'muted';
  if (tst) toast(last, level, Array.isArray(tst) ? tst : null);
  if (level === 'err' && o.open !== false) openOutput(o.open || tab);
}
// the older log(lines, cls), as hud.js calls it
function log(lines, cls) { msg(lines, { level: cls || 'info' }); }

function openOutput(tab = outTab) {
  outTab = PANES[tab] ? tab : 'messages';
  $('output').hidden = false;
  for (const [t, id] of Object.entries(PANES)) $(id).hidden = t !== outTab;
  document.querySelectorAll('#output .tab').forEach((b) => {
    const on = b.dataset.tab === outTab;
    b.classList.toggle('on', on);
    b.setAttribute('aria-selected', on);
    if (on) b.classList.remove('new', 'err');
  });
  const p = $(PANES[outTab]);
  p.scrollTop = outTab === 'check' ? 0 : p.scrollHeight;
  showOutToggle();
  store.set('output', '1');
}
function closeOutput() {
  $('output').hidden = true;
  showOutToggle();
  store.set('output', '0');
}
function showOutToggle() {
  const open = !$('output').hidden, b = $('outToggle');
  b.querySelector('.ic').style.setProperty('--ic', `url(vendor/mdi/${open ? 'chevron-down' : 'chevron-up'}.svg)`);
  b.title = open ? 'Hide the output' : 'Show the output: messages, check and game';
}
async function copyText(text) {
  try { await navigator.clipboard.writeText(text); return true; } catch (e) { /* not allowed here: the old way */ }
  const ta = document.createElement('textarea');
  ta.value = text;
  ta.style.cssText = 'position:fixed;left:-9999px;top:0';
  document.body.appendChild(ta);
  ta.select();
  let ok = false;
  try { ok = document.execCommand('copy'); } catch (e) { /* ignore */ }
  ta.remove();
  return ok;
}

// toasts: over the view's lower right corner; inside an open dialog (the top layer) so that they show above it
function placeToasts() {
  const r = $('view').getBoundingClientRect(), b = $('toasts');
  b.style.right = Math.max(8, innerWidth - r.right + 12) + 'px';
  b.style.bottom = Math.max(8, innerHeight - r.bottom + 12) + 'px';
}
function toast(text, level = 'info', action = null) {
  const box = $('toasts'), host = document.querySelector('dialog[open]') || document.body;
  if (box.parentElement !== host) host.appendChild(box);
  placeToasts();
  const t = document.createElement('div'), x = document.createElement('span');
  t.className = `toast ${level}`;
  t.setAttribute('role', level === 'err' ? 'alert' : 'status');
  x.textContent = text;
  t.append(icon(ICONS[level] || ICONS.info), x);
  let timer = 0;
  const gone = () => { clearTimeout(timer); t.classList.add('out'); setTimeout(() => t.remove(), 200); };
  const arm = (ms) => { clearTimeout(timer); timer = setTimeout(gone, ms); };
  if (action) {
    const b = document.createElement('button');
    b.type = 'button'; b.className = 'ghost'; b.textContent = action[0];
    b.onclick = () => { gone(); action[1](); };
    t.appendChild(b);
  }
  const c = document.createElement('button');
  c.type = 'button'; c.className = 'icon ghost'; c.title = 'Dismiss'; c.setAttribute('aria-label', 'Dismiss');
  c.appendChild(icon('close'));
  c.onclick = gone;
  t.appendChild(c);
  t.onmouseenter = () => clearTimeout(timer);
  t.onmouseleave = () => arm(2000);
  box.appendChild(t);
  while (box.childElementCount > 4) box.firstElementChild.remove();
  arm(action ? 8000 : 4000);
  return t;
}
// a dialog that closes gives the toasts back to the page (close does not bubble: capture)
document.addEventListener('close', (e) => {
  const box = $('toasts');
  if (e.target instanceof HTMLDialogElement && e.target.contains(box)) {
    document.body.appendChild(box);
    const other = document.querySelector('dialog[open]');
    if (other) other.appendChild(box);
  }
}, true);

// ------------------------------------------------------------------ view
const canvas = $('canvas');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
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
grid.material.fog = false;
grid.visible = false;
scene.add(grid);
let stageRoot = null, skyRoot = null, env = null;   // env: the stage's fog, background and lights (/api/stage, RST/<STAGE>.MRS)
// pedestrians (web/peds.js): a layer shown with P; pedSel is the marker the gizmo holds (never together with an object)
const peds = createPeds({ THREE, scene, toThree, toGame });
let discPeds = null, pedSel = null;

function resize() {
  const r = canvas.parentElement.getBoundingClientRect();
  renderer.setSize(r.width, r.height, false);
  camera.aspect = r.width / Math.max(1, r.height);
  camera.updateProjectionMatrix();
}
new ResizeObserver(resize).observe(canvas.parentElement);

// Dark, Light or System (the OS's light or dark). localStorage keeps a copy for the first frame (the <script> in index.html).
const THEMES = ['dark', 'light', 'system'];
function applyTheme(t) {
  if (!THEMES.includes(t)) t = 'dark';
  document.documentElement.dataset.theme = t;
  store.set('theme', t);
  applyLook();
}
try { matchMedia('(prefers-color-scheme: light)').addEventListener('change', () => applyLook()); } catch (e) { /* old engine */ }

// Game look (L): the stage's fog, its background (the fog's color) and the sky. Off: the whole stage, no fog, the theme's background.
let gameLook = store.get('gameLook', '0') === '1';
const gsColor = (rgb) => new THREE.Color(rgb[0] / 255, rgb[1] / 255, rgb[2] / 255);
function applyLook() {
  const f = env && env.fog && env.fog.fa !== undefined ? env.fog : null;
  scene.fog = gameLook && f ? new THREE.Fog(gsColor(f.rgb), f.fa, f.fb) : null;
  gsFog.gsFogMax.value = f && f.maxV > 0 ? Math.min(255, f.maxV) : 255;
  if (skyRoot) skyRoot.visible = gameLook;
  renderer.setClearColor(gameLook && env && env.bg ? gsColor(env.bg)
    : getComputedStyle(document.documentElement).getPropertyValue('--view').trim());
  $('look').checked = gameLook;
}
function setLook(on) {
  gameLook = on;
  store.set('gameLook', on ? '1' : '0');
  applyLook();
}

// ------------------------------------------------------------------ the GS's drawing (tools/lvlglb.py exports the raw bytes)
// Every stage TEX0 is MODULATE with TCC 1: C = min(255, Ct * Cv >> 7), As = min(255, At * Av >> 7) / 128 (up to 1.99); without a
// texture C = Cv and As = Av / 128. COLOR_0 and the textures hold the bytes / 255, so the factors are 255/128 and 255²/128².
const GS_MODULATE = `
#ifdef USE_MAP
  diffuseColor.rgb = min( diffuseColor.rgb * 1.9921875, vec3( 1.0 ) );
  diffuseColor.a = min( diffuseColor.a * 3.968811, 1.9921875 );
#else
  diffuseColor.a = min( diffuseColor.a * 1.9921875, 1.9921875 );
#endif`;
const gsFog = { gsFogMax: { value: 255 } };
function gsCompile(shader) {
  shader.uniforms.gsFogMax = gsFog.gsFogMax;
  shader.fragmentShader = shader.fragmentShader.replace('#include <color_fragment>', '#include <color_fragment>' + GS_MODULATE);
}
const gsProgramKey = () => 'wotm-gs';   // one program per define set for every stage material

// OBJECTs with dirLight (VU1 program 0x1010): C = min(255, Cv * sum(color_i / 128 * max(0, n.L_i))) over the .MRS's three parallel
// lights, the normal turned with the object (turning it in the editor changes its light, as in the game). No ambient term.
const gsLights = { gsLightDir: { value: [0, 1, 2].map(() => new THREE.Vector3()) },
                   gsLightCol: { value: [0, 1, 2].map(() => new THREE.Vector3()) }, gsLightOn: { value: 0 } };
const GS_LIGHT = `
if ( gsLightOn > 0.5 ) {
  vec3 gsN = normalize( mat3( modelMatrix ) * normal ), gsSum = vec3( 0.0 );
  for ( int i = 0; i < 3; i ++ ) gsSum += gsLightCol[ i ] * max( 0.0, dot( gsN, gsLightDir[ i ] ) );
  vColor.rgb = min( vColor.rgb * gsSum, vec3( 1.0 ) );
}`;
function gsCompileLit(shader) {
  gsCompile(shader);
  Object.assign(shader.uniforms, gsLights);
  shader.vertexShader = shader.vertexShader
    .replace('#include <common>', '#include <common>\nuniform vec3 gsLightDir[ 3 ];\nuniform vec3 gsLightCol[ 3 ];\nuniform float gsLightOn;')
    .replace('#include <color_vertex>', '#include <color_vertex>' + GS_LIGHT);
}
const gsProgramKeyLit = () => 'wotm-gs-lit';
function setLights(lights) {   // env.lights: rgb (0..255, /128 as the VU1 takes it) and dir (game axes, toward the light)
  const ok = Array.isArray(lights) && lights.length === 3;
  gsLights.gsLightOn.value = ok ? 1 : 0;
  if (!ok) return;
  lights.forEach((l, i) => {
    gsLights.gsLightDir.value[i].set(l.dir[0], l.dir[2], -l.dir[1]).normalize();
    gsLights.gsLightCol.value[i].set(l.rgb[0] / 128, l.rgb[1] / 128, l.rgb[2] / 128);
  });
}

// ALPHA_1 (A - B) * C + D, A/B/D in {Cs, Cd, 0} and C in {As, Ad, FIX}, as GL factors: the coefficient of Cs and of Cd is c * C + k.
// A source factor of As becomes One with the color premultiplied in the shader, so As above 1 counts as on the GS.
function gsBlend(m, alpha) {
  const A = alpha & 3, B = (alpha >> 2) & 3, C = (alpha >> 4) & 3, D = (alpha >> 6) & 3;
  const f = [[THREE.SrcAlphaFactor, THREE.OneMinusSrcAlphaFactor], [THREE.DstAlphaFactor, THREE.OneMinusDstAlphaFactor],
             [THREE.OneFactor, THREE.ZeroFactor]][Math.min(C, 2)];   // FIX taken as 0x80 (no stage uses it)
  const coef = (x) => ({ c: (A === x) - (B === x), k: D === x ? 1 : 0 });
  const factor = ({ c, k }) => (c === 0 ? (k ? THREE.OneFactor : THREE.ZeroFactor) : c > 0 ? (k ? THREE.OneFactor : f[0]) : k ? f[1] : f[0]);
  const s = coef(0), d = coef(1), sNeg = s.c < 0 && !s.k, dNeg = d.c < 0 && !d.k;
  m.blending = THREE.CustomBlending;
  m.blendEquation = sNeg && dNeg ? THREE.AddEquation : sNeg ? THREE.ReverseSubtractEquation : dNeg ? THREE.SubtractEquation : THREE.AddEquation;
  m.blendSrc = sNeg && dNeg ? THREE.ZeroFactor : factor(s);
  m.blendDst = sNeg && dNeg ? THREE.ZeroFactor : factor(d);
  m.premultipliedAlpha = m.blendSrc === THREE.SrcAlphaFactor;
  if (m.premultipliedAlpha) m.blendSrc = THREE.OneFactor;
  m.blendSrcAlpha = THREE.ZeroFactor;   // the framebuffer's alpha is not read
  m.blendDstAlpha = THREE.OneFactor;
}

// material.userData (glTF extras): gs {blend: ALPHA_1 with ABE or null, aref: alpha test ref or -1}, zmsk, fog, ot (alphaSort), dirLight
function gsMaterial(m) {
  const u = m.userData, g = u.gs || {}, abe = g.blend !== null && g.blend !== undefined;
  if (m.map) m.map.colorSpace = THREE.NoColorSpace;
  m.onBeforeCompile = u.dirLight ? gsCompileLit : gsCompile;
  m.customProgramCacheKey = u.dirLight ? gsProgramKeyLit : gsProgramKey;
  m.fog = u.fog !== false;
  m.depthWrite = !u.zmsk;
  // GS test: As * 128 <= aref fails. Without a test, the GS still writes Z where As is 0; 0.5/128 leaves those texels out, since the
  // three.js order is not the game's and they would hide what is behind them
  m.alphaTest = g.aref >= 0 ? (g.aref + 0.5) / 128 : abe ? 0.5 / 128 : 0;
  if (abe) gsBlend(m, g.blend);
  else m.blending = THREE.NoBlending;
  m.transparent = !!u.ot;   // alphaSort: after the rest, far to near, like the game's alpha OT
  m.needsUpdate = true;
}

// The stage's materials once loaded; the blended ones that are not alphaSort are drawn after the opaque ones in the scene's order (the
// game's traversal); the sky first, with no depth test, around the camera.
function setupDrawing(gltf) {
  const raw = !!(gltf.asset && gltf.asset.extras && gltf.asset.extras.gsRaw);
  if (!raw) msg('The 3D cache is out of date. Restart the editor to rebuild it.', { level: 'err', detail: 'the scene is from an older tools/lvlglb.py' });
  const done = new Set();
  let order = 0;
  gltf.scene.traverse((o) => {
    if (!o.isMesh) return;
    const mats = [].concat(o.material);
    for (const m of mats) {
      if (raw && !done.has(m)) { done.add(m); gsMaterial(m); }
    }
    const u = mats[0].userData, abe = u.gs && u.gs.blend !== null && u.gs.blend !== undefined;
    if (raw && abe && !u.ot) o.renderOrder = 1 + order++;
  });
  let sky = null;
  gltf.scene.traverse((o) => { if (!sky && o.userData && o.userData.sky) sky = o; });
  if (sky) {
    scene.add(sky);     // out of the stage: clicks, Drop and framing never meet it
    let n = 0;
    sky.traverse((o) => {
      if (!o.isMesh) return;
      o.material = [].concat(o.material).map((m) => {
        const c = m.clone();   // clone() leaves out onBeforeCompile and customProgramCacheKey
        c.onBeforeCompile = m.onBeforeCompile;
        c.customProgramCacheKey = m.customProgramCacheKey;
        c.depthTest = false; c.depthWrite = false; c.fog = false; c.transparent = false;
        return c;
      });
      if (o.material.length === 1) o.material = o.material[0];
      o.renderOrder = -1e6 + n++;
      o.frustumCulled = false;
    });
  }
  return sky;
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
  const common = 'Click: select · Shift+click: add · Shift+drag: box · Ctrl+A: all listed · G move · R turn · Arrows/PgUp/PgDn nudge · End drop · F focus · Home all · Ctrl+D copy · Del delete · L game look · P pedestrians';
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
  if (skyRoot) skyRoot.position.copy(camera.position);   // the sky moves with the camera (it turns with the view, not the stage)
  syncBoxes();
  renderer.render(scene, camera);
}

// ------------------------------------------------------------------ model
// rec: one object of the stage (handle). item: what can be selected, the object itself or one of its copies.
// group: every selected item; sel: the one the gizmo and the fields act on (the last picked), the others follow it.
let cfg = {}, stageName = '', recs = [], byHandle = new Map(), itemOf = new Map(), sel = null, group = [], dirty = false;
// undo and redo: {p: the patch as JSON before the change, label}; savedJson: the patch the mod's file holds
let history = [], future = [], savedJson = '';
let modNow = '', stageTitles = {};   // modNow: the mod the scene was loaded with (Save, Test and Reload write into it)
// what the file holds that the editor does not edit, given back as it is by buildPatch: top-level keys, entries and names for
// handles this stage does not have, pedestrian keys peds.js does not know (or the whole "peds" when the stage has no pedestrian data)
const ENTRY_KEYS = new Set(['handle', 'from', 'pos', 'heading', 'delete', 'copies']);
const PEDS_KEYS = new Set(['off', 'segments', 'groups']);
let keep = { top: {}, objects: [], names: {}, peds: {}, pedsRaw: null };

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

function makeCopy(rec, pos, heading, iname = '') {
  const node = rec.node.clone(true);
  rec.node.parent.add(node);
  node.visible = true;
  const item = { rec, node, copy: true, attached: [], iname };
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
    if (rec.copies.length) {
      e.copies = rec.copies.map((c) => Object.assign({ pos: posOf(c).map(r1), heading: r1(headingOf(c)) }, c.iname ? { name: c.iname } : {}));
    }
    if (rec.extra) Object.assign(e, rec.extra);
    if (Object.keys(e).length > 2) objects.push(e);
  }
  objects.push(...keep.objects);
  // instance names: a key the game ignores (docs/guides/MODS.md, "Level patches")
  const names = Object.assign({}, keep.names);
  for (const rec of recs) if (rec.item.iname) names[rec.info.handle] = rec.item.iname;
  const out = Object.keys(names).length ? { objects, names } : { objects };
  const pp = keep.pedsRaw || (discPeds ? peds.patch() : null);
  if (pp || Object.keys(keep.peds).length) out.peds = Object.assign({}, pp, keep.peds);
  return Object.assign(out, keep.top);
}

function resetAll() {
  for (const rec of recs) {
    for (const c of [...rec.copies]) removeCopy(c);
    rec.item.iname = '';
    rec.deleted = false;
    rec.node.visible = true;
    setWorldMatrix(rec.node, rec.m0);
  }
}

// patch: the mod's file (or an undo step). The parts the editor does not edit go to `keep`; returns how many of them there are.
function applyPatch(patch) {
  patch = patch && typeof patch === 'object' ? patch : {};
  keep = { top: {}, objects: [], names: {}, peds: {}, pedsRaw: null };
  for (const [k, v] of Object.entries(patch)) if (k !== 'objects' && k !== 'names' && k !== 'peds') keep.top[k] = v;
  for (const rec of recs) rec.extra = null;
  for (const e of Array.isArray(patch.objects) ? patch.objects : []) {
    const rec = e && typeof e === 'object' ? byHandle.get(normHandle(e.handle)) : null;
    if (!rec) { keep.objects.push(e); continue; }
    const extra = Object.fromEntries(Object.entries(e).filter(([k]) => !ENTRY_KEYS.has(k)));
    rec.extra = Object.keys(extra).length ? extra : null;
    for (const c of e.copies || []) makeCopy(rec, c.pos, c.heading ?? rec.h0, c.name || '');
    if (e.delete) { rec.deleted = true; rec.node.visible = false; }
    else if (e.pos || e.heading !== undefined) setWorld(rec.node, e.pos || rec.pw, e.heading ?? rec.h0, rec);
  }
  for (const [h, n] of Object.entries(patch.names || {})) {
    const rec = byHandle.get(normHandle(h));
    if (rec) rec.item.iname = String(n);
    else keep.names[h] = n;
  }
  const pp = patch.peds && typeof patch.peds === 'object' ? patch.peds : null;
  if (pp && !discPeds) keep.pedsRaw = pp;
  else if (pp) for (const [k, v] of Object.entries(pp)) if (!PEDS_KEYS.has(k)) keep.peds[k] = v;
  pedSel = null;     // the markers are made again
  peds.load(discPeds, keep.pedsRaw ? null : pp);
  peds.setVisible(pedsOn);
  showPedCounts();
  return { entries: keep.objects.length + Object.keys(keep.names).length, keys: Object.keys(keep.top).concat(Object.keys(keep.peds).map((k) => 'peds.' + k)) };
}
// the note over the view about what the file keeps that the editor does not show
function showKeep(k) {
  const title = stageTitles[stageName] || stageName, parts = [];
  if (k.entries) parts.push(`${k.entries} ${k.entries === 1 ? 'change' : 'changes'} in this file ${k.entries === 1 ? 'is' : 'are'} for objects ${title} doesn't have.`);
  if (k.keys.length) parts.push(`The file also has ${k.keys.map((x) => `"${x}"`).join(', ')}, which the editor doesn't edit.`);
  $('keepNote').hidden = !parts.length;
  if (!parts.length) return;
  $('keepText').textContent = parts.join(' ') + ' Saving keeps ' + (k.entries + k.keys.length === 1 ? 'it' : 'them') + ' in the file as ' +
    (k.entries + k.keys.length === 1 ? 'it is.' : 'they are.');
  msg($('keepText').textContent, { level: 'warn' });
}

// label: what the change does, for the Undo and Redo tooltips ("move Hotel", "delete 3 objects")
function snapshot(label = 'change') {
  history.push({ p: JSON.stringify(buildPatch()), label });
  if (history.length > 200) history.shift();
  future = [];
  dirty = true;
  staleCheck();
  showUndo();
}
// the last check's result holds until the next change
function staleCheck() { if (ui.check.state === 'ok' || ui.check.state === 'bad') ui.check.state = 'stale'; }
const whatSel = () => (group.length > 1 ? `${group.length} objects` : sel ? nameOf(sel) : pedSel ? pedTitle(pedSel).toLowerCase() : 'objects');
function restoreTo(p) {
  const again = group.filter((it) => !it.copy).map((it) => it.rec.item);
  resetAll();
  applyPatch(JSON.parse(p));
  dirty = JSON.stringify(buildPatch()) !== savedJson;
  staleCheck();
  selectMany(again);
  showUndo();
}
function undo() {
  if (!history.length) return;
  const h = history.pop();
  future.push({ p: JSON.stringify(buildPatch()), label: h.label });
  restoreTo(h.p);
}
function redo() {
  if (!future.length) return;
  const f = future.pop();
  history.push({ p: JSON.stringify(buildPatch()), label: f.label });
  restoreTo(f.p);
}
function showUndo() {
  const u = history[history.length - 1], r = future[future.length - 1];
  $('undo').disabled = !u;
  $('undo').title = u ? `Undo: ${u.label} (Ctrl+Z)` : 'Nothing to undo (Ctrl+Z)';
  $('redo').disabled = !r;
  $('redo').title = r ? `Redo: ${r.label} (Ctrl+Y)` : 'Nothing to redo (Ctrl+Y)';
  $('save').classList.toggle('dirty', dirty);
  showStatus();
}

// ------------------------------------------------------------------ the status bar: last message, save, check, game, counts
// ui.savedAt: when this page last saved this stage; ui.check.state: none, ok, bad (n problems) or stale (a change since)
const ui = { savedAt: 0, changed: 0, check: { state: 'none', n: 0 } };
function stField(cls, text, state, title) {
  const b = document.querySelector(`#status .${cls}`);
  b.className = `st ${cls}${state ? ' ' + state : ''}`;
  b.querySelector('.tx').textContent = text;
  if (title !== undefined) b.title = title;
  return b;
}
function showStatus(changedN) {
  if (changedN !== undefined) ui.changed = changedN;
  const where = modNow ? `mods/${modNow}/levels/${stageName}.json` : '';
  stField('save', dirty ? 'Unsaved changes' : ui.savedAt ? `Saved ${hhmm(ui.savedAt)}` : 'No unsaved changes',
          dirty ? 'dirty' : ui.savedAt ? 'saved' : '', where ? `Saves to ${where} (Ctrl+S)` : 'Choose a mod to save into (Mod, at the top)');
  const c = ui.check;
  const ck = stField('check', c.state === 'ok' ? 'Check passed' : c.state === 'bad' ? `${c.n} ${c.n === 1 ? 'problem' : 'problems'}`
                     : c.state === 'stale' ? 'Not checked since the last change' : 'Not checked',
                     { ok: 'ok', bad: 'err' }[c.state] || 'idle');
  ck.querySelector('.ic').style.setProperty('--ic', `url(vendor/mdi/${c.state === 'bad' ? 'alert-circle-outline' : c.state === 'ok' ? 'check-circle-outline' : 'shield-check-outline'}.svg)`);
  const n = group.length || (pedSel ? 1 : 0);
  stField('count', `${ui.changed} changed` + (n ? ` · ${n} selected` : ''));
  showGame();
}

// ------------------------------------------------------------------ loading
async function loadStage(name) {
  $('busy').hidden = false;
  try {
    select(null);
    for (const r of [stageRoot, skyRoot]) if (r) { scene.remove(r); r.traverse((o) => { if (o.geometry) o.geometry.dispose(); }); }
    stageRoot = skyRoot = null;
    stageName = name;
    $('keepNote').hidden = true;
    clearCheckPane();
    const mod = modNow;
    const info = await api(`/api/stage?name=${encodeURIComponent(name)}&mod=${encodeURIComponent(mod)}`);
    const gltf = await new GLTFLoader().loadAsync(`/api/glb?name=${encodeURIComponent(name)}`);
    env = info.env || null;
    discPeds = info.peds || null;
    setLights(env && env.lights);
    skyRoot = setupDrawing(gltf);
    stageRoot = gltf.scene;
    scene.add(stageRoot);
    applyLook();
    recs = []; byHandle = new Map(); itemOf = new Map(); history = []; future = [];
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
        rec.item = { rec, node: o, copy: false, attached: [], iname: '' };
        recs.push(rec);
        byHandle.set(normHandle(i.handle), rec);
        itemOf.set(o, rec.item);
      }
    });
    for (const rec of recs) {
      if (rec.info.on) { const b = byHandle.get(normHandle(rec.info.on)); if (b) b.item.attached.push(rec.item); }
    }
    const kept = applyPatch(info.patch);
    savedJson = JSON.stringify(buildPatch());
    dirty = false;
    ui.savedAt = 0;
    ui.check = { state: 'none', n: 0 };
    showUndo();
    frameAll();
    showKeep(kept);
    refreshList();
    if (new URLSearchParams(location.search).has('debug')) console.log('patch ' + JSON.stringify(buildPatch()));
    const pc = peds.counts(), n = (info.patch.objects || []).length;
    msg(`${title(name)}: ${recs.length} objects, ${pc.groups} pedestrian groups` +
        (n ? `; mod "${mod}" changes ${n} ${n === 1 ? 'object' : 'objects'}` : '') +
        (info.patch.peds ? (n ? ' and the pedestrians' : `; mod "${mod}" changes the pedestrians`) : ''));
  } catch (e) {
    const er = cleanError(e);
    msg(`Couldn't open ${title(name)}: ${er.text}`, { level: 'err', detail: er.detail });
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
// name: the instance's (patch), else the model's (names files), else the rule's (tools/lvlnames.py)
function nameOf(item) { return item.iname || item.rec.info.name; }
function label(item) {
  return item.copy ? `${nameOf(item)} (copy ${item.rec.copies.indexOf(item) + 1})` : nameOf(item);
}
const dimsText = (d) => (d || []).map((x) => Math.round(x)).join(' × ');
function shownKinds() {
  return new Set([...document.querySelectorAll('.kinds input[data-kind]')].filter((x) => x.checked).map((x) => x.dataset.kind));
}
function listed() {   // the items the list shows: kind boxes, "Changed" and the filter text
  const f = $('filter').value.trim().toLowerCase(), kinds = shownKinds(), onlyChanged = $('changedOnly').checked;
  return items().filter((it) => {
    const i = it.rec.info;
    if (!kinds.has(i.kind) || (onlyChanged && !changed(it.rec))) return false;
    return !f || `${label(it)} ${i.rule} ${i.handle} ${i.kind} ${i.id.toString(16)}`.toLowerCase().includes(f);
  });
}
function refreshList() {
  const ul = $('list');
  ul.textContent = '';
  const shown = listed();
  for (const it of shown) {
    const i = it.rec.info;
    const li = document.createElement('li');
    li.innerHTML = `<span class="k">${i.kind}</span><span class="h"></span><span class="hd"></span>`;
    li.querySelector('.h').textContent = label(it);
    li.querySelector('.hd').textContent = i.handle;
    if (!it.iname && i.nameSrc === 'rule') li.classList.add('rule');
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
  const nChanged = buildPatch().objects.length;
  $('count').textContent = `${shown.length} shown · ${group.length > 1 ? group.length + ' selected · ' : ''}${nChanged} changed`;
  document.title = `WotM Level Editor — ${stageName}${dirty ? ' *' : ''}`;
  $('save').classList.toggle('dirty', dirty);
  showStatus(nChanged);
}

// mode: 'set' (only this one), 'toggle' (Shift/Ctrl click), 'add'
const isDeleted = (it) => !!it && !it.copy && it.rec.deleted;
function select(item, mode = 'set') {
  if (item && !item.node.visible && !isDeleted(item)) item = null;
  if (isDeleted(item) || isDeleted(sel)) mode = 'set';   // a deleted original is selected alone (its panel offers Restore)
  if (mode === 'set' || item) pedSel = null;
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
  pedSel = null;
  group = add ? [...group, ...vis.filter((it) => !group.includes(it))] : vis;
  sel = group[group.length - 1] || null;
  showSel();
}
function showSel() {
  gizmo.detach();
  if (sel) pedSel = null;
  peds.highlight(pedSel);
  const gone = isDeleted(sel);
  $('props').hidden = !sel || gone;
  $('delProps').hidden = !gone;
  $('pedProps').hidden = !pedSel;
  $('noSel').hidden = !!sel || !!pedSel;
  if (pedSel) {
    if (gizmo.getMode() !== 'translate') setTool('translate');
    gizmo.attach(pedSel.node);
    showPed();
  }
  if (gone) {
    const i = sel.rec.info;
    $('dTitle').textContent = label(sel);
    $('dSub').textContent = `${i.handle} · ${i.kind}`;
  } else if (sel) {
    gizmo.attach(sel.node);
    const i = sel.rec.info;
    $('pTitle').textContent = label(sel);
    const onName = i.on && byHandle.get(normHandle(i.on)) ? nameOf(byHandle.get(normHandle(i.on)).item) : '';
    $('pSub').textContent = `${i.handle} · ${i.kind} · ${dimsText(i.dims)} · id 0x${i.id.toString(16)} · ${i.states} states · ` +
      `${i.paths} collision paths` + (i.on ? ` · on ${onName} (${i.on})` : '');
    $('pName').value = sel.iname;
    $('pName').placeholder = i.name;
    $('pModel').value = i.nameSrc === 'model' ? i.name : '';
    $('pModel').placeholder = i.rule;
    $('pModel').disabled = !i.sig;
    const same = recs.filter((r) => r.info.sig && r.info.sig === i.sig).length;
    $('pModelNote').textContent = i.sig ? `the same model: ${same} in this stage, and in any stage it appears` : 'no mesh: no model name';
    $('pGroup').textContent = group.length > 1 ? `${group.length} selected: the others follow this one.` : '';
    $('onCount').textContent = group.reduce((n, it) => n + it.attached.length, 0);
    $('pWarn').textContent = i.paths === 0 && i.kind !== 'pickup' ? 'No collision of its own.' : '';
    showProps();
  }
  syncBoxes();
  refreshList();
}
function syncBoxes() {
  while (boxes.length < group.length) {
    const b = new THREE.BoxHelper(undefined, 0xffcc33);
    b.material.fog = false;
    scene.add(b);
    boxes.push(b);
  }
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
  if (!sel || isDeleted(sel)) return;
  const p = posOf(sel), h = headingOf(sel);
  $('px').value = r1(p[0]); $('py').value = r1(p[1]); $('pz').value = r1(p[2]); $('ph').value = r1(h);
}

// ------------------------------------------------------------------ pedestrians (P): groups and the sidewalks they walk
let pedsOn = store.get('peds', '0') === '1';
function setPeds(on) {
  pedsOn = on;
  store.set('peds', on ? '1' : '0');
  peds.setVisible(on);
  $('pedsOn').checked = on;
  $('pedBar').hidden = !on;
  if (!on && pedSel) { pedSel = null; showSel(); }
  showPedCounts();
}
function showPedCounts() {
  const c = peds.counts();
  $('pedCounts').textContent = discPeds
    ? `${c.groups}/${c.maxGroups} groups · ${c.people} people · ${c.segments}/${c.maxSegments} sidewalks` : '';
}
function selectPed(it) {
  group = []; sel = null; pedSel = it;
  showSel();
}
const pedTitle = (it) => (it.kind === 'group' ? `Pedestrian group ${it.group.handle || '(new)'}`
  : it.kind === 'seg' ? 'Sidewalk' : `Sidewalk end ${it.end ? 'B' : 'A'}`);
function pedPos(it) { return it.kind === 'group' ? it.group.pos : it.kind === 'seg' ? toGame(it.node.position) : it.end ? it.seg.b : it.seg.a; }
function showPed() {
  if (!pedSel) return;
  const it = pedSel, p = pedPos(it);
  $('pdTitle').textContent = pedTitle(it);
  $('pdInfo').textContent = peds.info(it);
  $('pdCountRow').hidden = it.kind !== 'group';
  $('pdWidthRow').hidden = it.kind === 'group';
  $('pdDup').hidden = it.kind !== 'group';
  if (it.kind === 'group') $('pdCount').value = it.group.count;
  else $('pdWidth').value = it.seg.width;
  $('pdx').value = r1(p[0]); $('pdy').value = r1(p[1]); $('pdz').value = r1(p[2]);
  showPedCounts();
}
const DOWN = new THREE.Vector3(0, -1, 0);
function groundAt(v) {   // the scene height of the first visible surface under v (from a little above it), or null
  if (!stageRoot) return null;
  const ray = new THREE.Raycaster(new THREE.Vector3(v.x, v.y + 400, v.z), DOWN);
  const hit = ray.intersectObject(stageRoot, true).find((h) => visibleChain(h.object));
  return hit ? hit.point.y : null;
}
function viewPoint() {   // where a new marker goes: the surface in the middle of the view, else a point ahead
  const ray = new THREE.Raycaster();
  ray.setFromCamera(new THREE.Vector2(0, 0), camera);
  const hit = stageRoot ? ray.intersectObject(stageRoot, true).find((h) => visibleChain(h.object)) : null;
  return toGame(hit ? hit.point : camera.position.clone().addScaledVector(camera.getWorldDirection(new THREE.Vector3()), 600));
}
function pedEdit(fn, label = 'pedestrians') {   // one undo step, then the edit; a refused edit (limits) goes to the log
  const before = history.length, fut = future, wasDirty = dirty;
  const back = () => { history.length = before; future = fut; dirty = wasDirty; showUndo(); };
  try {
    snapshot(label);
    const r = fn();
    if (r === false) back();
    showPed();
    showPedCounts();
    refreshList();
    return r;
  } catch (e) {
    back();
    msg(`Pedestrians: ${e.message}`, { level: 'warn', toast: true });
    return false;
  }
}
function addPed(kind) {
  if (!discPeds) return;
  if (!pedsOn) setPeds(true);
  const it = pedEdit(() => (kind === 'group' ? peds.addGroup(viewPoint()) : peds.addSegment(viewPoint())),
                     kind === 'group' ? 'add a pedestrian group' : 'add a sidewalk');
  if (it) selectPed(it);
}
function pedFromInputs() {
  if (!pedSel) return;
  const it = pedSel;
  pedEdit(() => {
    if (it.kind === 'group') peds.setCount(it, +$('pdCount').value);
    else peds.setWidth(it, +$('pdWidth').value);
    const want = [+$('pdx').value, +$('pdy').value, +$('pdz').value], now = pedPos(it);
    if (Math.hypot(want[0] - now[0], want[1] - now[1], want[2] - now[2]) > 0.05) {
      it.node.position.add(toThree(want).sub(toThree(now)));
      peds.moved(it);
    }
  }, 'edit ' + pedTitle(it).toLowerCase());
}
function nudgePed(v) {
  pedSel.node.position.add(v);
  peds.moved(pedSel);
  showPed();
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
  if (e.value && pedSel) snapshot('move ' + pedTitle(pedSel).toLowerCase());
  else if (e.value && sel) {
    snapshot((gizmo.getMode() === 'rotate' ? 'turn ' : 'move ') + whatSel());
    dragStart = startMove();
  } else {
    dragStart = null;
    refreshList();
  }
});
gizmo.addEventListener('objectChange', () => {
  if (pedSel) { peds.moved(pedSel); showPed(); return; }
  if (!sel) return;
  if (gizmo.getMode() === 'rotate') setWorld(sel.node, posOf(sel), headingOf(sel), sel.rec);   // only about the game's Z
  if (dragStart) carry(dragStart);
  showProps();
});

function setTool(mode) {
  if (pedSel && mode === 'rotate') return;   // pedestrian markers only move
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
  if (!sel || isDeleted(sel)) return;
  snapshot('edit ' + whatSel());
  const start = startMove();
  setWorld(sel.node, [+$('px').value, +$('py').value, +$('pz').value], +$('ph').value, sel.rec);
  carry(start);
  refreshList();
}

// arrows: along the world axis nearest to where the camera looks (Shift ×10); PageUp/PageDown: up and down
let lastNudge = -Infinity;
function nudge(fwd, side, up, big) {
  if (!pedSel && (!sel || isDeleted(sel))) return;
  const now = performance.now();
  if (now - lastNudge > 700) snapshot('nudge ' + whatSel());   // one undo step per burst of key repeats
  lastNudge = now;
  const d = camera.getWorldDirection(new THREE.Vector3());
  const f = Math.abs(d.x) > Math.abs(d.z) ? new THREE.Vector3(Math.sign(d.x), 0, 0) : new THREE.Vector3(0, 0, Math.sign(d.z) || -1);
  const right = new THREE.Vector3(-f.z, 0, f.x);
  const step = ($('snap').checked ? snapMove() : 10) * (big ? 10 : 1);
  const v = f.multiplyScalar(fwd).addScaledVector(right, side).add(new THREE.Vector3(0, up, 0)).multiplyScalar(step);
  if (pedSel) { nudgePed(v); return; }
  const start = startMove();
  setWorldMatrix(sel.node, new THREE.Matrix4().makeTranslation(v.x, v.y, v.z).multiply(sel.node.matrixWorld));
  carry(start);
  showProps();
  refreshList();
}

function visibleChain(o) { for (; o; o = o.parent) if (!o.visible) return false; return true; }

// End: each selected item down (or up) onto the first surface under its middle, its own meshes and what it carries aside
function dropToGround() {
  if (pedSel) { const it = pedSel; pedEdit(() => peds.drop(it, groundAt), 'drop ' + pedTitle(it).toLowerCase()); return; }
  if (!group.length || !stageRoot || isDeleted(sel)) return;
  snapshot('drop ' + whatSel());
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
  const all = group.length, what = all === 1 ? nameOf(group[0]) : `${all} objects`;
  if (!n) msg(`Nothing under ${what} to drop onto`, { level: 'warn', toast: true });
  else if (n < all) msg(`Dropped ${n} of ${all} (nothing under the other ${all - n})`, { level: 'warn', toast: true });
  else msg(`Dropped ${what} onto the surface below`, { level: 'ok', toast: true });
  showProps();
  refreshList();
}

function duplicate() {
  if (pedSel && pedSel.kind === 'group') {
    const g = pedSel.group, it = pedEdit(() => peds.addGroup([g.pos[0] + 60, g.pos[1], g.pos[2]], g.count), 'copy a pedestrian group');
    if (it) selectPed(it);
    return;
  }
  if (!sel || isDeleted(sel)) return;
  snapshot('copy ' + whatSel());
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
  const on = done.size - made.length;
  msg(`Copied ${countText(made)}` + (on ? ` and ${on} ${on === 1 ? 'object' : 'objects'} on top` : ''), { level: 'ok', toast: true });
  selectMany(made);
}
// "Hotel", "3 buildings" or "5 objects"
const KIND_WORDS = { destr: ['building', 'buildings'], pickup: ['pickup', 'pickups'], prop: ['prop', 'props'] };
function countText(list) {
  if (list.length === 1) return nameOf(list[0]);
  const kinds = new Set(list.map((it) => it.rec.info.kind)), w = kinds.size === 1 ? KIND_WORDS[[...kinds][0]] : null;
  return `${list.length} ${w ? w[1] : 'objects'}`;
}

function hideRec(it) {
  if (it.copy) { if (it.rec.copies.includes(it)) removeCopy(it); }
  else { it.rec.deleted = true; it.node.visible = false; }
}
function remove() {
  if (pedSel) {
    const it = pedSel;
    pedSel = null;
    pedEdit(() => peds.remove(it), 'delete ' + pedTitle(it).toLowerCase());
    showSel();
    return;
  }
  if (!group.length || isDeleted(sel)) return;
  snapshot('delete ' + whatSel());
  const main = group.filter((it) => it.node.visible), on = new Set();
  for (const it of [...group]) {
    hideRec(it);
    if ($('carry').checked) for (const a of it.attached) { if (a.node.visible && !group.includes(a)) on.add(a); hideRec(a); }
  }
  if (main.length) {
    msg(`Deleted ${countText(main)}` + (on.size ? ` and ${on.size} ${on.size === 1 ? 'object' : 'objects'} on top` : ''),
        { level: 'ok', toast: ['Undo', undo] });
  }
  select(null);
}

// Reset, and Restore on a deleted original: back to the disc's place, without copies (with Carry, what rests on it too)
function resetSel() {
  if (!group.length) return;
  snapshot('restore ' + whatSel());
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
  const hits = ray.intersectObject(stageRoot || scene, true).filter((h) => visibleChain(h.object));
  if (pedsOn && !add) {   // a marker in front of the stage's first surface (lanes lie just above the sidewalk)
    const ph = ray.intersectObjects(peds.items().map((it) => it.pick), false)[0];
    if (ph && (!hits.length || ph.distance <= hits[0].distance + 2)) { selectPed(ph.object.userData.pedItem); return; }
  }
  for (const hit of hits) {
    if (!visibleChain(hit.object)) continue;
    for (let o = hit.object; o; o = o.parent) {
      if (itemOf.has(o)) { select(itemOf.get(o), add ? 'toggle' : 'set'); return; }
    }
    break;      // the first visible thing is not an object (ground, a road): keep looking at nothing
  }
  if (!add) select(null);
});

// ------------------------------------------------------------------ saving and testing
// the game the editor opened: running, the stage it was opened on (server) and the one its [fase] lines name last
// state: closed, starting, running or reloading (showGame); result: what the game last did with the patch (gameResult)
const game = { running: false, stage: '', seen: '', state: 'closed', result: null, pending: false, watch: 0 };
const title = (s) => stageTitles[s] || s;
const gameStage = () => game.seen || game.stage;
async function gameLog() {
  try { return await fetch('/api/gamelog').then((r) => r.json()); } catch (e) { return { lines: [], running: false, stage: '' }; }
}
// An error from the server without its Python type ("RuntimeError: ..." keeps the type for Details)
function cleanError(e) {
  const s = String(e && e.message || e), m = /^[A-Z]\w*(?:Error|Exception): ([\s\S]*)$/.exec(s);
  return { text: m ? m[1] : s, detail: m ? s : '' };
}
// The check's lines (editor.py check_patch, the output of lvl.py patch) as {ok, summary, problems: [{text, handle}], warns, infos}
function readCheck(res, patch, mod) {
  const problems = [], warns = [], infos = [];
  let stats = null, cars = 0, walks = 0, m;
  const objs = (patch && patch.objects) || [];
  for (const l of res.lines || []) {
    const s = l.trim();
    if ((m = /^entry (\d+) skipped: (.*)$/.exec(s))) {
      const e = objs[+m[1]], rec = e ? byHandle.get(normHandle(e.handle)) : null;
      problems.push({ text: `${rec ? nameOf(rec.item) : e ? e.handle : 'Entry ' + m[1]} was not applied: ${m[2]}`, handle: rec ? rec.info.handle : null });
    } else if ((m = /^pedestrian group (\d+) skipped: (.*)$/.exec(s))) {
      problems.push({ text: `Pedestrian group ${+m[1] + 1} was not applied: ${m[2]}` });
    } else if ((m = /^box rule: \d+ ok .*?, (\d+) off,.*worst ([\d.]+)/.exec(s))) {
      if (+m[1]) problems.push({ text: `Collision boxes: ${m[1]} out of place (worst ${(+m[2]).toFixed(1)} units)` });
    } else if ((m = /^cell rule: (\d+) of (\d+) paths/.exec(s))) {
      if (+m[1] < +m[2]) problems.push({ text: `Collision cells: ${m[2] - m[1]} boxes listed in the wrong cells` });
    } else if ((m = /^warning: mod "(.+)" also has levels\/\S+\.json/.exec(s))) {
      warns.push(`Mod "${m[1]}" also changes ${title(stageName)} and loads after "${mod}", so the game uses ${m[1]}'s file. ` +
                 'Mods load in alphabetical order; the last one wins.');
    } else if (/^vehicles: link \d+-\d+ cut/.test(s)) cars++;
    else if (/^pedestrians: segment \d+-\d+ (removed|\(.*clipped)/.test(s)) walks++;
    else if ((m = /^(\S+): (.*)$/.exec(s)) && m[1] === stageName) stats = m[2];
  }
  const cut = [cars && `${cars} car ${cars === 1 ? 'path' : 'paths'}`, walks && `${walks} sidewalk ${walks === 1 ? 'segment' : 'segments'}`].filter(Boolean);
  if (cut.length) infos.push(`Cut ${cut.join(' and ')} under the objects`);
  if (!res.ok && !problems.length && !warns.length) problems.push({ text: 'The check failed (see Details)' });
  const ok = !problems.length, n = problems.length;
  const what = stats && stats !== 'nothing changed' ? statsText(stats) : 'nothing changed';
  return { ok, n, problems, warns, infos,
           summary: ok ? `Check passed: ${what}. Collision OK.` : `Check found ${n} ${n === 1 ? 'problem' : 'problems'}` };
}
function clearCheckPane() {
  $('checkOut').innerHTML = '<div class="empty muted">No check yet. Check, Save, Test in game and Reload in game check your changes.</div>';
}
// the Check tab: the last check's summary, its problems (a click selects the object), warnings, notes and the raw output
function showCheck(chk, lines) {
  const p = $('checkOut');
  p.textContent = '';
  p.appendChild(lineEl(chk.summary, chk.ok ? 'ok' : 'err'));
  for (const pr of chk.problems) {
    p.appendChild(lineEl(pr.text, 'err', pr.handle ? { go: () => goTo(pr.handle), goTitle: 'Select it and look at it' } : {}));
  }
  for (const w of chk.warns) p.appendChild(lineEl(w, 'warn'));
  for (const i of chk.infos) p.appendChild(lineEl(i, 'info'));
  const det = document.createElement('details');
  det.innerHTML = '<summary>Details: the full output of the check</summary><pre></pre>';
  det.querySelector('pre').textContent = (lines || []).join('\n');
  p.appendChild(det);
  markTab('check', !chk.ok);
}
function goTo(handle) {
  const rec = byHandle.get(normHandle(handle));
  if (!rec) return;
  select(rec.item);
  if (rec.item.node.visible) focus(rec.item);
}
const FAILED = { check: 'Check failed', save: "Couldn't save", test: "Couldn't start the game", reload: "Couldn't reload the game" };
async function post(kind, save = true, extra = {}) {
  const mod = modNow;
  const body = Object.assign({ name: stageName, mod, patch: buildPatch(), training: true, save }, extra);
  const wasRunning = game.running;
  if (kind === 'test') setGameState('starting');
  if (kind === 'reload') setGameState('reloading');
  game.pending = kind === 'test' || kind === 'reload';
  try {
    const before = kind === 'reload' ? (await gameLog()).lines.length : 0;   // the log keeps every load of this run
    const res = await api(kind === 'check' ? '/api/check' : `/api/${kind}`, body);
    game.pending = false;
    const same = JSON.stringify(buildPatch()) === JSON.stringify(body.patch);   // no edit while the server worked
    let chk = null;
    if (kind === 'check' || (res.lines && res.lines.length)) {
      chk = readCheck(res, body.patch, mod);
      ui.check = { state: !same ? 'stale' : chk.ok ? 'ok' : 'bad', n: chk.n };
      showCheck(chk, res.lines);
      msg(chk.summary, { level: chk.ok ? 'ok' : 'err', open: 'check', toast: kind === 'check' && chk.ok ? true : undefined,
                         go: () => openOutput('check'), goTitle: 'Show the check' });
      for (const w of chk.warns) msg(w, { level: 'warn', toast: true });
    }
    const rel = `mods/${mod}/levels/${stageName}.json`;
    if (res.saved) {
      savedJson = JSON.stringify(body.patch);
      dirty = JSON.stringify(buildPatch()) !== savedJson;   // an edit made while saving stays unsaved
      ui.savedAt = Date.now();
      const removed = /^removed /.test(res.saved);
      const text = removed ? `Removed ${rel}: your mod no longer changes ${title(stageName)}` : `Saved to ${rel}`;
      const bad = chk && !chk.ok;
      msg(text, { level: 'ok', detail: res.saved.replace(/^removed /, ''),
                  toast: bad ? false : kind === 'save' });
      if (bad) toast(`Saved, but the check found ${chk.n} ${chk.n === 1 ? 'problem' : 'problems'}`, 'warn', ['Show', () => openOutput('check')]);
    } else if (kind === 'save') {
      msg(`Nothing to save: your mod has no changes for ${title(stageName)}`, { toast: true });
    }
    if (res.launched) {
      game.seen = ''; game.stage = stageName; game.result = null;
      setGameState('starting');
      msg(`Starting the game on ${title(stageName)} (training, as Togera)`, { level: 'ok', detail: res.launched });
      watchGame(0, true);
    }
    if (res.reloading) {
      setGameState('reloading');
      msg(`Reloading ${title(stageName)} in the game: it loads the saved file when a fight on this stage is running`, { level: 'info' });
      watchGame(before);
    }
    showUndo();
    refreshList();
  } catch (e) {
    game.pending = false;
    if (kind === 'test' || kind === 'reload') setGameState(wasRunning ? 'running' : 'closed');
    if (kind === 'test' && e.code === 'running') { game.stage = e.data.stage || game.stage; setRunning(true); askRunning(save); return; }
    const er = cleanError(e);
    const action = e.code === 'mod' ? ['Choose mod', () => $('mod').focus()]
      : /Settings/.test(er.text) ? ['Open settings', () => $('settings').click()] : undefined;
    msg(`${FAILED[kind] || kind}: ${er.text}`, { level: 'err', detail: er.detail, toast: action });
  }
}

// a small dialog with a title, a text and buttons [value, label, primary]; then(value) when it closes (Esc: 'cancel')
function ask(head, text, buttons, then) {
  const dlg = $('askDlg');
  $('askTitle').textContent = head;
  $('askText').textContent = text;
  $('askBtns').textContent = '';
  for (const [value, label, primary] of buttons) {
    const b = document.createElement('button');
    b.value = value; b.textContent = label;
    if (primary) b.className = 'primary';
    $('askBtns').appendChild(b);
  }
  dlg.returnValue = '';
  dlg.addEventListener('close', () => then(dlg.returnValue || 'cancel'), { once: true });
  dlg.showModal();
}
// Test with the game open: reload this stage in it, or close it and open it again
function askRunning(save) {
  const on = gameStage(), same = !on || on === stageName;
  ask('The game is already open',
      same ? `It is on ${title(stageName)}. Reload this stage in it, or restart the game?`
           : `It is on ${title(on)}. Restart it on ${title(stageName)}?`,
      [['cancel', 'Cancel'], ['restart', 'Restart game', !same]].concat(same ? [['reload', 'Reload in game', true]] : []),
      (v) => { if (v === 'restart') post('test', save, { restart: true }); else if (v === 'reload') post('reload'); });
}
function reloadGame() {
  const on = gameStage();
  if (!on || on === stageName) { post('reload'); return; }
  ask(`The game is on ${title(on)}`, 'Reload only works on the stage the game is showing.',
      [['cancel', 'Cancel'], ['restart', `Restart on ${title(stageName)}`, true]],
      (v) => { if (v === 'restart') post('test', true, { restart: true }); });
}
async function stopGame() {
  try {
    const r = await api('/api/stop', {});
    msg(r.stopped ? 'The game is closed.' : 'The game was not running.', { level: 'ok', toast: true });
  } catch (e) { msg(`Couldn't close the game: ${cleanError(e).text}`, { level: 'err' }); }
  noteGame(await gameLog());
}

// One [fase] line of the game's log (native/runtime/wotm_levelpatch.inc and wotm_levelreload.inc) in words:
// {text, cls, stage, end, more, last}; end: the line settles what the editor waits for (applied, ignored or dropped), more: the
// load's own lines follow it (up to "depois:", last). null: a line shown as it is.
const STAT_WORDS = { moved: 'moved', copies: 'copies', deleted: 'deleted', skipped: 'not applied', 'ped segments': 'sidewalks',
                     'ped groups changed': 'pedestrian groups changed', 'ped groups added': 'pedestrian groups added',
                     'ped groups deleted': 'pedestrian groups deleted', 'ped groups off': 'pedestrian groups off' };
function statsText(s) {
  const out = [];
  for (const part of s.split(', ')) {
    const m = /^(.+) (\d+)$/.exec(part);
    if (m && STAT_WORDS[m[1]]) out.push(`${m[2]} ${STAT_WORDS[m[1]]}`);
  }
  return out.join(', ') || 'no object changes';
}
function gameLine(line) {
  const s = line.replace(/^\[fase\] ?/, '');
  const ok = (text, x) => Object.assign({ text, cls: 'ok', end: true }, x);
  const bad = (text, x) => Object.assign({ text, cls: 'err', end: true }, x);
  let m;
  if (/^\s+depois:/.test(s)) return { raw: true, last: true };
  if (/^\s/.test(s)) return null;      // the load's details (entries, path cuts, antes:)
  if ((m = /^(\S+) <- (.+) \((.+?)\): (.*)$/.exec(s))) {
    if (modNow && m[2].toLowerCase() !== modNow.toLowerCase())
      return bad(`The game uses mod "${m[2]}"'s file for this stage, not yours`, { stage: m[1], more: true });
    return ok(m[4] === 'nada mudou' ? 'Patch loaded in game: nothing changed' : `Patch applied in game (${statsText(m[4])})`,
              { stage: m[1], more: true });
  }
  if ((m = /^\S+ \((.*)\): JSON invalido, patch ignorado: (.*)$/.exec(s))) return bad(`The game rejected the file: it isn't valid JSON (${m[2]})`);
  if ((m = /^(\S+): imagem da fase sem grade de colisao/.exec(s))) return bad("The game couldn't apply the patch to this stage", { stage: m[1] });
  if ((m = /^LEVELS\/(\S+)\.JSON: (.+) vence (.+)$/.exec(s)))
    return { text: `Mod "${m[2]}" also changes this stage and loads after "${m[3]}": the last mod by name wins`, cls: 'warn' };
  if ((m = /^(\S+): sem patch/.exec(s))) return ok('Game: no patch for this stage', { stage: m[1] });
  if ((m = /^recarga pedida \(arquivo\) para (\S+), mas a fase e (\S+): ignorada/.exec(s)))
    return bad(`The game is on ${title(m[2])}: reload skipped`, { stage: m[2] });
  if (/^recarga pedida .* sem quadro de luta em 10 s: descartada/.test(s)) return ok('Reload dropped: no fight was running for 10 s', { cls: 'warn' });
  if (/^recarga pedida .* ja atendida/.test(s)) return ok('Already up to date');
  if ((m = /^recarga pedida \((?:arquivo|F10)\): (\S+)$/.exec(s))) return { text: `Reloading ${title(m[1])}…`, cls: '', stage: m[1] };
  if (/^patches de fase desligados/.test(s)) return bad('Level patches are off in this game (WOTM_LEVELPATCH=0)');
  if (/^recarga: o rtMain voltou/.test(s)) return ok('Reload dropped: the game left the fight another way', { cls: 'warn' });
  if (/^recarga: .*nao foi feita/.test(s)) return bad("The game couldn't reload the stage");
  return null;
}
// what the stage patch did in the game: its [fase] lines after `seen`, until one settles it (5.5 of the plan) or the wait ends
// The lines go to the Game tab (in words, and the rest as they are); the one that settles it also to Messages, as a toast.
async function watchGame(seen, launch = false) {
  const me = game.watch = (game.watch || 0) + 1;   // a newer watch (Reload while the launch is watched) takes over
  let done = false, extra = false;
  for (let i = 0; i < (launch ? 60 : 20); i++) {
    await sleep(1500);
    if (game.watch !== me) return;
    const r = await gameLog(), lines = r.lines || [];
    noteGame(r);
    if (lines.length < seen) seen = 0;     // a new log (the game was opened again)
    for (const l of lines.slice(seen)) {
      const t = gameLine(l);
      if (!t || t.raw) msg(l.replace(/^\[fase\] ?/, ''), { level: 'muted', tab: 'game', raw: true });
      else if (t.end) gameResult(t);
      else msg(t.text, { level: t.cls || 'info', tab: 'game', status: false });
      if (t && t.end) { done = true; extra = !!t.more; }
      if (t && t.last) extra = false;
    }
    seen = Math.max(seen, lines.length);
    if (!r.running) { msg('The game closed.', { level: 'muted', tab: 'game' }); return; }
    // the game is up once it has had time to open its window (or it already loaded the stage)
    if (launch && i === 12 && game.state === 'starting') setGameState('running');
    if (done && !extra) return;
    if (done) extra = false;               // one more look for the rest of the load's lines
  }
  if (game.watch !== me) return;
  if (game.state === 'starting' || game.state === 'reloading') setGameState('running');
  if (!done) {
    msg(launch ? 'The game is open. It reports the stage patch when a fight on this stage starts.'
               : "The game hasn't reloaded yet. It does when a fight on this stage is running.", { level: launch ? 'info' : 'warn', tab: 'game' });
  }
}
// a line that settles what the game did with the patch: applied, ignored or dropped (idle)
function gameResult(t) {
  const level = t.cls || 'ok';
  game.result = { kind: level === 'ok' ? 'applied' : level === 'warn' ? 'idle' : 'ignored', text: t.text, at: Date.now() };
  msg(t.text, { level, tab: 'game', status: false });
  setGameState('running');
  msg(t.text, { level, toast: true, open: 'game', go: () => openOutput('game'), goTitle: "Show the game's lines" });
}
function noteGame(r) {
  game.stage = r.stage || '';
  if (!r.running) game.seen = '';
  else for (const l of r.lines || []) { const t = gameLine(l); if (t && t.stage) game.seen = t.stage; }
  setRunning(!!r.running);
}
// the game's state: closed, starting (Test in game asked for it), running, reloading (Reload in game waits for the game's answer)
function setRunning(on) {
  if (!on && game.pending) return;         // Test in game is opening it: the server does not know it yet
  game.running = on;
  if (!on) game.state = 'closed';
  else if (game.state === 'closed') game.state = 'running';
  showGame();
}
function setGameState(s) {
  game.state = s;
  game.running = s !== 'closed';
  showGame();
}
function showGame() {
  const s = game.state, where = title(gameStage() || stageName);
  const ind = $('gameInd');
  ind.hidden = s === 'closed';
  ind.className = `game-ind ${s}`;
  $('gameIndText').innerHTML = '';
  $('gameIndText').append(s === 'starting' ? 'Starting game…' : s === 'reloading' ? 'Reloading… waiting for a fight' : 'Game running');
  if (s === 'running') {
    const w = document.createElement('span');
    w.className = 'where';
    w.textContent = ` · ${where}`;
    $('gameIndText').append(w);
  }
  ind.title = s === 'running' ? `The game the editor opened is running, on ${where}` : '';
  const test = $('test'), rl = $('reload');
  test.disabled = s === 'starting';
  test.classList.toggle('primary', s === 'closed');
  rl.disabled = s !== 'running';
  rl.classList.toggle('primary', s === 'running' || s === 'reloading');
  const label = dirty ? 'Save & reload' : 'Reload in game';
  rl.querySelector('.lbl').textContent = label;
  rl.setAttribute('aria-label', label);
  rl.title = s === 'closed' ? 'Start the game with Test in game first'
    : s === 'starting' ? 'The game is starting'
    : s === 'reloading' ? 'The game is reloading this stage'
    : (dirty ? 'Save your changes and reload' : 'Reload') + ' this stage in the open game (F10 in the game does the same)';
  $('stop').hidden = s === 'closed';
  const res = game.result;
  const [text, state] = s === 'closed' ? ['Game closed', 'idle']
    : s === 'starting' ? ['Starting game…', 'warn']
    : s === 'reloading' ? ['Reloading in game…', 'warn']
    : !res ? [`Game running · ${where}`, '']
    : res.kind === 'applied' ? [`Patch applied in game ${hhmm(res.at)}`, 'ok']
    : res.kind === 'ignored' ? ['Patch ignored in game', 'err'] : ['Reload dropped in game', 'warn'];
  stField('game', text, state, res && s !== 'closed' ? `${res.text} (${hhmm(res.at)}); opens the Game tab` : 'Opens the Game tab');
}
setInterval(async () => noteGame(await gameLog()), 4000);

// ------------------------------------------------------------------ wiring
// the stage and mod pickers let go of the focus once picked: arrows and letters are the scene's again
$('stage').onchange = (e) => {
  e.target.blur();
  if (dirty && !confirm('Unsaved changes on this stage. Load another one anyway?')) { e.target.value = stageName; return; }
  loadStage(e.target.value);
};
// the game skips a mod folder whose name starts with _ or . (and Windows refuses these characters)
const badModName = (m) => /[\\/:*?"<>|]/.test(m) || /^[_.]/.test(m);
$('mod').onchange = (e) => {
  const v = e.target.value.trim();
  e.target.blur();
  e.target.value = v;
  if (v === modNow) return;
  if (badModName(v)) {
    msg('A mod name can\'t contain \\ / : * ? " < > | or start with _ or .', { level: 'err', toast: true });
    e.target.value = modNow;
    return;
  }
  if (dirty && !confirm(`Load mod "${v || '(none)'}"'s version of this stage? Your unsaved changes are lost.`)) {
    e.target.value = modNow;       // the scene stays the old mod's: Save keeps writing there
    return;
  }
  modNow = v;
  cfg.mod = v;
  api('/api/config', { mod: v }).catch((err) => msg(`Couldn't keep the mod in the settings: ${cleanError(err).text}`, { level: 'err' }));
  loadStage(stageName);
};
$('filter').oninput = refreshList;
document.querySelectorAll('.kinds input').forEach((x) => (x.onchange = refreshList));
$('undo').onclick = undo;
$('redo').onclick = redo;
$('frame').onclick = frameAll;
$('stop').onclick = stopGame;
$('restore').onclick = resetSel;
$('keepClose').onclick = () => { $('keepNote').hidden = true; };
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
$('reload').onclick = reloadGame;
// the Output: tabs, Copy, Clear, hide; the status bar's fields open it on their tab (or close it when it is there already)
document.querySelectorAll('#output .tab').forEach((b) => (b.onclick = () => openOutput(b.dataset.tab)));
document.querySelectorAll('#status .st').forEach((b) => (b.onclick = () => {
  if (!$('output').hidden && outTab === b.dataset.tab) closeOutput(); else openOutput(b.dataset.tab);
}));
$('outToggle').onclick = () => ($('output').hidden ? openOutput() : closeOutput());
$('outClose').onclick = closeOutput;
$('outClear').onclick = () => { if (outTab === 'check') clearCheckPane(); else $(PANES[outTab]).textContent = ''; };
$('outCopy').onclick = async () => {
  const p = $(PANES[outTab]);
  p.querySelectorAll('details').forEach((d) => (d.dataset.was = d.open ? '1' : '', d.open = true));   // the details count
  const text = p.innerText.trim();
  p.querySelectorAll('details').forEach((d) => (d.open = d.dataset.was === '1'));
  const ok = await copyText(text);
  toast(ok ? 'Copied the tab\'s text' : 'The browser did not let the page copy', ok ? 'ok' : 'warn');
};
$('output').style.height = Math.max(80, +store.get('outputH', '200') || 200) + 'px';
$('outGrip').addEventListener('pointerdown', (e) => {
  const out = $('output'), y0 = e.clientY, h0 = out.getBoundingClientRect().height, grip = e.currentTarget;
  grip.setPointerCapture(e.pointerId);
  grip.classList.add('drag');
  const move = (ev) => { out.style.height = Math.round(Math.min(innerHeight * 0.6, Math.max(80, h0 + y0 - ev.clientY))) + 'px'; };
  const up = () => {
    grip.classList.remove('drag');
    grip.removeEventListener('pointermove', move);
    store.set('outputH', parseInt(out.style.height, 10));
  };
  grip.addEventListener('pointermove', move);
  grip.addEventListener('pointerup', up, { once: true });
});
if (store.get('output', '0') === '1') openOutput('messages'); else showOutToggle();
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
$('look').onchange = (e) => setLook(e.target.checked);
$('pedsOn').onchange = (e) => setPeds(e.target.checked);
$('pedAddGroup').onclick = () => addPed('group');
$('pedAddSeg').onclick = () => addPed('seg');
for (const id of ['pdCount', 'pdWidth', 'pdx', 'pdy', 'pdz']) $(id).onchange = pedFromInputs;
$('pdDrop').onclick = dropToGround;
$('pdDup').onclick = duplicate;
$('pdDel').onclick = remove;
$('hud').onclick = () => {
  if (!stageName) return;
  openHud({ stage: stageName, mod: modNow, api, log });
};
$('drop').onclick = dropToGround;
$('dup').onclick = duplicate;
$('del').onclick = remove;
$('reset').onclick = resetSel;
for (const id of ['px', 'py', 'pz', 'ph']) $(id).onchange = editFromInputs;
$('pName').onchange = () => {
  if (!sel) return;
  snapshot('rename ' + whatSel());
  sel.iname = $('pName').value.trim();
  showSel();
};
$('pModel').onchange = async () => {
  if (!sel || !sel.rec.info.sig) return;
  const i = sel.rec.info, name = $('pModel').value.trim();
  try {
    const r = await api('/api/names', { sig: i.sig, name, kind: i.kind, ptype: i.ptype, dims: i.dims });
    for (const rec of recs) {
      if (rec.info.sig !== i.sig) continue;
      rec.info.name = name || rec.info.rule;
      rec.info.nameSrc = name ? 'model' : 'rule';
    }
    msg(name ? `Model renamed to "${name}" in every stage` : 'Model name removed in every stage', { level: 'ok', toast: true, detail: r.file });
  } catch (e) { msg(`Couldn't rename the model: ${cleanError(e).text}`, { level: 'err' }); }
  showSel();
};
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
// a control that takes the keys itself: text and number fields and the pickers (not checkboxes, radios or sliders)
const typing = (t) => !!t?.matches?.('input:not([type=checkbox]):not([type=radio]):not([type=range]), textarea, select');
// fly keys by their place on the keyboard (e.code), so WASD works on AZERTY too; the other letters by what they say
const FLY_CODES = { KeyW: 'w', KeyA: 'a', KeyS: 's', KeyD: 'd', KeyQ: 'q', KeyE: 'e' };
window.addEventListener('keyup', (e) => { if (FLY_CODES[e.code]) fly.keys.delete(FLY_CODES[e.code]); });
window.addEventListener('keydown', (e) => {
  const k = (e.key || '').toLowerCase(), ctrl = e.ctrlKey || e.metaKey, field = typing(e.target);
  if (document.querySelector('dialog[open]')) {   // a dialog has the keys (Esc closes it, the selection stays)
    if (ctrl && k === 's') e.preventDefault();
    return;
  }
  const text = field && !e.target.matches('select');
  // Save, Undo and Redo before the focus test: Ctrl+S never opens the browser's "save page"; in a text field Undo is the field's
  if (ctrl && !e.altKey && k === 's') {
    e.preventDefault();
    const t = e.target;
    if (field) t.blur();               // the field's change first (it may be an edit)
    if (t.id !== 'mod') post('save');  // a new mod name loads that mod: no save into it before
    return;
  }
  if (ctrl && !e.altKey && (k === 'z' || k === 'y')) {
    if (text) return;
    e.preventDefault();
    if (k === 'y' || e.shiftKey) redo(); else undo();
    return;
  }
  if (field) return;
  if (ctrl && k === 'd') { e.preventDefault(); duplicate(); }
  else if (e.ctrlKey && k === 'a') { e.preventDefault(); selectMany(listed()); }
  else if (k === 'delete') remove();
  else if (e.ctrlKey || e.altKey || e.metaKey) return;
  else if (k.startsWith('arrow') && (sel || pedSel)) {
    e.preventDefault();
    nudge(k === 'arrowup' ? 1 : k === 'arrowdown' ? -1 : 0, k === 'arrowright' ? 1 : k === 'arrowleft' ? -1 : 0, 0, e.shiftKey);
  }
  else if ((k === 'pageup' || k === 'pagedown') && (sel || pedSel)) { e.preventDefault(); nudge(0, 0, k === 'pageup' ? 1 : -1, e.shiftKey); }
  else if (k === 'end') { e.preventDefault(); dropToGround(); }
  else if (k === 'home') { e.preventDefault(); frameAll(); }
  else if (fly.on && FLY_CODES[e.code]) { e.preventDefault(); fly.keys.add(FLY_CODES[e.code]); }
  else if (fly.on && k === 'shift') fly.keys.add(k);
  else if (k === 'v') setView(!fly.on);
  else if (k === 'g') setTool('translate');
  else if (k === 'r') setTool('rotate');
  else if (k === 'f' && sel) focus(sel);
  else if (k === 'f' && pedSel) focus({ node: pedSel.kind === 'seg' ? pedSel.lane : pedSel.node });
  else if (k === 'l') setLook(!gameLook);
  else if (k === 'p') setPeds(!pedsOn);
  else if (k === 'escape') select(null);
});
window.addEventListener('beforeunload', (e) => { if (dirty) { e.preventDefault(); e.returnValue = ''; } });

function init(r) {
  cfg = r.config;
  applyTheme(cfg.theme || 'dark');
  $('mods').innerHTML = r.mods.map((m) => `<option value="${m}">`).join('');
  if (!stageName) modNow = (cfg.mod || '').trim();   // the first init; Settings keeps the mod in use
  $('mod').value = modNow;
  const s = $('stage');
  const prev = stageName || (new URLSearchParams(location.search).get('stage') || 'CENTRAL').toUpperCase();   // ?stage=METRO
  // "Midtown (CENTRAL)", but "UFO" once when the title is the file's name
  s.innerHTML = r.stages.map((x) => `<option value="${x.name}">${x.title}${x.title.toUpperCase() === x.name ? '' : ` (${x.name})`}</option>`).join('');
  stageTitles = Object.fromEntries(r.stages.map((x) => [x.name, x.title]));
  game.stage = r.gameStage || '';
  setRunning(!!r.gameRunning);
  if (r.discError || !r.stages.length) {
    msg(r.discError || `No stages on this disc (${cfg.disc}). Is it War of the Monsters (NTSC-U)?`,
        { level: 'err', toast: ['Open settings', () => $('settings').click()] });
    return;
  }
  s.value = r.stages.some((x) => x.name === prev) ? prev : r.stages[0].name;
  loadStage(s.value);
}

setTool('translate');
orbit.enabled = false;
setView(fly.on);
setPeds(pedsOn);
// the editor program quits a few seconds after its page closes (a reload asks again at once and keeps it)
window.addEventListener('pagehide', () => { try { navigator.sendBeacon('/api/bye', '{}'); } catch (e) { /* ignore */ } });
if (new URLSearchParams(location.search).has('debug')) {
  window.wotmDebug = { camera, fly, flyStep, setView, select, selectMany, items, listed, dropToGround, nudge, duplicate, remove,
                       undo, buildPatch, posOf, setLook, scene, renderer, gsLights, peds, selectPed, setPeds, addPed, pedFromInputs,
                       redo, resetAll, applyPatch, frameAll, resetSel, gameLine, post, setRunning, applyTheme, refreshList,
                       msg, toast, openOutput, closeOutput, showStatus, setGameState, readCheck, gameResult,
                       get ui() { return ui; }, get outTab() { return outTab; },
                       get history() { return history; }, get future() { return future; }, get dirty() { return dirty; },
                       get modNow() { return modNow; }, get keep() { return keep; }, get game() { return game; },
                       get gizmoObject() { return gizmo.object || null; }, get gizmoMode() { return gizmo.getMode(); },
                       get pedSel() { return pedSel; }, get group() { return group; }, get sel() { return sel; },
                       get env() { return env; }, get skyRoot() { return skyRoot; }, get stageRoot() { return stageRoot; } };
}
loop();
api('/api/init').then(init).catch((e) => msg(`Couldn't reach the editor program: ${cleanError(e).text}`, { level: 'err' }));
