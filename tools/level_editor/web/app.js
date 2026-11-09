// WotM level editor page: the stage from /api/glb (tools/lvlglb.py), objects and patch from /api/stage (tools/lvl.py).
// Game coordinates are Z up (x, y, z); the scene is Y up: three = (x, z, -y). A heading turns about the game's Z, which is
// the scene's +Y with the same sign. The patch (docs/guides/MODS.md, "Level patches") is rebuilt from the scene each time.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';
import { createPeds } from './peds.js';
import { openHud, hudGameChanged } from './hud.js';
import { openTex, texGameChanged } from './tex.js';
import { openLight, lightGameChanged, lightLookChanged, lightClose } from './light.js';
import { initArrange } from './arrange.js';
import { openTerrain } from './terrain.js';
import { initColl } from './coll.js';

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
// The gizmo in the axis colors of the X/Y/Z fields (scene X = game X red, scene Y = game Z green, scene Z = game -Y blue), and white
// instead of yellow for the hovered handle, so that it does not read as the selection's amber. TransformControls sets the hover
// color with setHex(0xffff00) on every frame, so each color's setHex swaps it.
const GIZMO_COLORS = new Map([[0xff0000, 0xF0605A], [0x00ff00, 0x4CC38A], [0x0000ff, 0x4C9AFF], [0xffff00, 0xFFFFFF]]);
gizmo.traverse((o) => {
  for (const m of [].concat(o.material || [])) {
    if (!m.color || m.color._wotm) continue;
    const c = m.color, set = THREE.Color.prototype.setHex;
    c.setHex = function (hex, space) { return set.call(this, GIZMO_COLORS.get(hex) ?? hex, space); };
    c._wotm = true;
    const was = c.getHex();
    if (GIZMO_COLORS.has(was)) c.setHex(was);
  }
});
// the selection's outlines: the one the gizmo holds in amber through everything in front of it, the others of the group in orange
// (behind walls they stay hidden: with a hundred selected, seeing them all through the buildings is noise)
const boxes = [];
const SEL_COLOR = 0xFFC233, SEL2_COLOR = 0xFF8A3D, HOVER_COLOR = 0xFFFFFF, DEL_COLOR = 0xF0605A, CHANGED_COLOR = 0x3FB97F;
function lineBox(color, o = {}) {
  const b = new THREE.BoxHelper(undefined, color);
  Object.assign(b.material, { fog: false, transparent: true, depthWrite: false }, o);
  b.visible = false;
  scene.add(b);
  return b;
}
const hoverBox = lineBox(HOVER_COLOR, { opacity: 0.55, depthTest: false });
hoverBox.renderOrder = 999;
// the grid: 100-unit lines (the snap step when it is smaller) and 1000-unit lines, white (black in the light theme without Game look)
function gridHelper(size, divisions, opacity) {
  const g = new THREE.GridHelper(size, divisions, 0xffffff, 0xffffff);
  Object.assign(g.material, { transparent: true, opacity, fog: false, depthWrite: false });
  g.visible = false;
  scene.add(g);
  return g;
}
let gridMinor = gridHelper(20000, 200, 0.10), gridMinorStep = 100;
const gridMajor = gridHelper(20000, 20, 0.28);
let gridOn = false;
let skyViews = { dome: null, clouds: null };   // the models swapped into the sky: {from: the preview scene, view: its copy in the view} (updateSkyView)
let stageRoot = null, skyRoot = null, env = null;   // env: the stage's fog, background and lights (/api/stage, RST/<STAGE>.MRS)
// pedestrians (web/peds.js): a layer shown with P; pedSel is the marker the gizmo holds (never together with an object)
const peds = createPeds({ THREE, scene, toThree, toGame });
const coll = initColl({ THREE, scene });   // the Collision layer (C): what the game collides with, patch applied (web/coll.js)
let discPeds = null, pedSel = null, stageGrid = null, stageWalls = 0;   // stageGrid: the disc's collision grid (/api/stage)

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
  for (const s of Object.values(skyViews)) if (s) s.view.visible = gameLook;
  renderer.setClearColor(gameLook && env && env.bg ? gsColor(env.bg)
    : getComputedStyle(document.documentElement).getPropertyValue('--view').trim());
  $('look').checked = gameLook;
  lightLookChanged();
  // the grid: black lines on the light theme's background, white on the dark one and over the stage's own (Game look)
  const t = document.documentElement.dataset.theme;
  const light = t === 'light' || (t === 'system' && matchMedia('(prefers-color-scheme: light)').matches);
  for (const g of [gridMinor, gridMajor]) g.material.color.set(light && !gameLook ? 0x000000 : 0xffffff);
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
    skyDrawing(sky, false);
  }
  return sky;
}

// A sky is drawn first, with no depth test and no fog, around the camera. both: draw the inside and the outside of its faces
// (a model of the mod's, whose faces the game wants turned inward)
function skyDrawing(sky, both) {
  let n = 0;
  sky.traverse((o) => {
    if (!o.isMesh) return;
    o.material = [].concat(o.material).map((m) => {
      const c = m.clone();   // clone() leaves out onBeforeCompile and customProgramCacheKey
      c.onBeforeCompile = m.onBeforeCompile;
      c.customProgramCacheKey = m.customProgramCacheKey;
      c.depthTest = false; c.depthWrite = false; c.fog = false; c.transparent = false;
      if (both) c.side = THREE.DoubleSide;
      return c;
    });
    if (o.material.length === 1) o.material = o.material[0];
    o.renderOrder = -1e6 + n++;
    o.frustumCulled = false;
  });
}

// The models the patch swaps into the sky (props entries {model, sky: true} for the dome and {model, sky: "clouds"} for the cloud layer):
// their previews, drawn in the place of the stage's own part (the meshes of the stage's sky that sit under a CHAR_INSTANCE carry
// userData.clouds). Game look shows them, as it does the stage's own. The view draws the cloud layer still, with no animation.
function updateSkyView() {
  for (const [kind, key] of [['dome', skyModel], ['clouds', cloudsModel]]) {
    const pm = key ? propModels.get(key) : null, want = pm && pm.scene ? pm.scene : null, cur = skyViews[kind];
    if ((cur ? cur.from : null) === want) continue;     // else the model changed, or its files were made again
    if (cur) scene.remove(cur.view);
    skyViews[kind] = null;
    if (want) {
      const view = want.clone(true);
      skyDrawing(view, true);
      scene.add(view);
      skyViews[kind] = { from: want, view };
    }
  }
  if (skyRoot) skyRoot.traverse((o) => { if (o.isMesh) o.visible = !skyViews[o.userData.clouds ? 'clouds' : 'dome']; });
  applyLook();
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
  for (const [id, v] of [['vFly', true], ['vOrbit', false]]) {
    $(id).classList.toggle('on', on === v);
    $(id).setAttribute('aria-pressed', on === v);
  }
  showHint();
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
  for (const s of [skyRoot, ...Object.values(skyViews).map((v) => v && v.view)]) if (s) s.position.copy(camera.position);   // the sky moves with the camera (it turns with the view, not the stage)
  hoverStep();
  syncBoxes();
  renderer.render(scene, camera);
}

// ------------------------------------------------------------------ the hint chip (lower left of the view)
// What the mouse and keys do now. It fades after 8 s without a change of context and comes back when the context changes.
const HINTS = {
  fly: 'Right-drag to look · WASD to fly · Click to select · ? for shortcuts',
  orbit: 'Drag to orbit · Right-drag to pan · Wheel to zoom · Click to select',
  one: 'Drag the arrows to move · R to turn · End to drop · Ctrl+D to copy · Del to delete',
  turn: 'Drag the ring to turn · G to move · End to drop · Ctrl+D to copy · Del to delete',
  many: 'The amber one leads, the orange ones follow · Shift+click adds or removes',
  deleted: 'Deleted in your mod · Restore brings it back · Esc to deselect',
  band: 'Release to select the objects whose middle is inside',
  gizmo: 'Ctrl+Z undoes the move',
  peds: 'Click a group or a sidewalk · Add them from the toolbar',
  pedSeg: 'Drag an end to stretch it · Drag the middle to move it',
  pedGroup: 'Drag the arrows to move it · End to drop · Ctrl+D to copy · Del to delete',
};
const hint = { key: '', timer: 0 };
function hintKey() {
  if (band && !$('band').hidden) return 'band';
  if (gizmo.dragging) return 'gizmo';
  if (editMode === 'peds') return pedSel ? (pedSel.kind === 'group' ? 'pedGroup' : 'pedSeg') : 'peds';
  if (isDeleted(sel)) return 'deleted';
  if (group.length > 1) return 'many';
  if (sel) return gizmo.getMode() === 'rotate' ? 'turn' : 'one';
  return fly.on ? 'fly' : 'orbit';
}
function showHint() {
  const k = hintKey(), el = $('hint');
  if (k === hint.key) return;
  hint.key = k;
  el.textContent = HINTS[k];
  el.classList.remove('gone');
  clearTimeout(hint.timer);
  hint.timer = setTimeout(() => el.classList.add('gone'), 8000);
}

// ------------------------------------------------------------------ model
// rec: one object of the stage (handle). item: what can be selected, the object itself or one of its copies.
// group: every selected item; sel: the one the gizmo and the fields act on (the last picked), the others follow it.
let stageBases = {};     // a custom stage's name -> the game stage it is built on (its files, its handles, its props)
const baseOf = (n) => stageBases[n] || n;
let cfg = {}, stageName = '', recs = [], byHandle = new Map(), itemOf = new Map(), sel = null, group = [], dirty = false;
// undo and redo: {p: the patch as JSON before the change, label}; savedJson: the patch the mod's file holds
let history = [], future = [], savedJson = '';
let modNow = '', stageTitles = {};   // modNow: the mod the scene was loaded with (Save, Test and Reload write into it)
// what the file holds that the editor does not edit, given back as it is by buildPatch: top-level keys, entries and names for
// handles this stage does not have, pedestrian keys peds.js does not know (or the whole "peds" when the stage has no pedestrian data),
// props whose model the mod does not have for this stage
const ENTRY_KEYS = new Set(['handle', 'from', 'pos', 'heading', 'delete', 'copies']);
const PEDS_KEYS = new Set(['off', 'segments', 'groups']);
const PROP_KEYS = new Set(['id', 'model', 'pos', 'heading', 'name']);
let keep = { top: {}, objects: [], names: {}, peds: {}, pedsRaw: null, props: [] };
let skyModel = '', cloudsModel = '';   // the models that replace the sky dome and the cloud layer: the patch's props entries {model, sky: true} and {model, sky: "clouds"} (MODS.md, "Sky"); the view draws them with Game look (updateSkyView)
// New props (the patch's "props", MODS.md): the models of the mod's props/ folder (tools/lvlprop.py), drawn from their preview with
// the stage's textures. Each entry is a rec like a stage object's, with rec.prop = {id, model}; propRoot holds their nodes, inside
// stageRoot so that clicks and Drop meet them.
let propModels = new Map();   // MODEL -> {info: the /api/props entry, scene: the preview's scene, or null}
let propRecs = [], propRoot = null, stageTex = new Map();   // stageTex: texId -> the stage's texture

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

// ------------------------------------------------------------------ new props
// The mod's models (/api/props) and, for this stage's, the preview's scene; its materials name the stage texture (tex<id>)
async function loadPropModels(mod) {
  propModels = new Map();
  if (!mod) return;
  let list = [];
  try { list = (await api(`/api/props?mod=${encodeURIComponent(mod)}`)).props || []; } catch (e) { msg(`Couldn't list the props of mod "${mod}": ${cleanError(e).text}`, { level: 'warn' }); }
  await Promise.all(list.map(async (p) => {
    const pm = { info: p, scene: null };
    propModels.set(p.model, pm);
    if (p.error || !p.preview || p.stage !== baseOf(stageName)) return;
    try {
      const gltf = await new GLTFLoader().loadAsync(`/api/propglb?mod=${encodeURIComponent(mod)}&model=${encodeURIComponent(p.model)}`);
      gltf.scene.traverse((o) => {
        if (!o.isMesh) return;
        for (const m of [].concat(o.material)) {
          const t = /^tex(\d+)/.exec(m.name || '');
          if (t) m.map = stageTex.get(+t[1]) || null;
          gsMaterial(m);
        }
      });
      pm.scene = gltf.scene;
    } catch (e) { p.error = `its preview does not load (${cleanError(e).text})`; }
  }));
}
const propKey = (m) => String(m || '').toUpperCase();
// e: a "props" entry {id, model, pos, heading, name}; null (and nothing made) when the model is not one of this stage in the mod
function makeProp(e) {
  if (!e || typeof e !== 'object' || !propRoot) return null;
  const key = propKey(e.model), pm = propModels.get(key);
  const pos = Array.isArray(e.pos) && e.pos.length === 3 && e.pos.every((x) => typeof x === 'number') ? e.pos : null;
  if (!pm || !pm.scene || !pos) return null;
  const node = new THREE.Group();
  node.add(pm.scene.clone(true));
  propRoot.add(node);
  const b = pm.info.bounds || { min: [0, 0, 0], max: [0, 0, 0] };
  const info = { handle: key, kind: 'newprop', name: pm.info.name || key, nameSrc: 'model', rule: 'New prop', id: 0, model: key,
                 dims: b.max.map((x, k) => x - b.min[k]), pos, heading: +e.heading || 0, states: 1, paths: 1 };
  const rec = { info, node, p0: pos, pw: pos, h0: 0, q0: new THREE.Quaternion(), s0: new THREE.Vector3(1, 1, 1), copies: [],
                deleted: false, prop: { id: String(e.id || newPropId()), model: key } };
  const extra = Object.fromEntries(Object.entries(e).filter(([k]) => !PROP_KEYS.has(k)));
  rec.extra = Object.keys(extra).length ? extra : null;
  rec.item = { rec, node, copy: false, attached: [], iname: e.name ? String(e.name) : '' };
  itemOf.set(node, rec.item);
  propRecs.push(rec);
  setWorld(node, pos, +e.heading || 0, rec);
  return rec.item;
}
function removeProp(rec) {
  rec.node.parent?.remove(rec.node);
  itemOf.delete(rec.node);
  const k = propRecs.indexOf(rec);
  if (k >= 0) propRecs.splice(k, 1);
}
// "prop1", "prop2"...: one more than the highest such id in the file
function newPropId() {
  let n = 0;
  for (const id of propRecs.map((r) => r.prop.id).concat(keep.props.map((e) => e && e.id))) {
    const m = /^prop(\d+)$/.exec(String(id || ''));
    if (m) n = Math.max(n, +m[1]);
  }
  return 'prop' + (n + 1);
}
function addProp(model, at = null) {   // at: where it goes (else where the view looks)
  if (!propModels.get(model)?.scene) return;
  showPropMenu(false);
  if (editMode !== 'objects') enterMode('objects');
  snapshot('add prop ' + model);
  const p = at || viewPoint(), it = makeProp({ id: newPropId(), model, pos: p });
  msg(`Added ${nameOf(it)} (${model})`, { level: 'ok', toast: ['Undo', undo] });
  select(it);
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
  const props = propRecs.map((r) => {
    const e = { id: r.prop.id, model: r.prop.model, pos: posOf(r.item).map(r1) }, h = headingOf(r.item);
    if (Math.abs(h) > 0.05) e.heading = r1(h);
    if (r.item.iname) e.name = r.item.iname;
    return Object.assign(e, r.extra || {});
  }).concat(keep.props);
  if (skyModel) props.push({ model: skyModel, sky: true });
  if (cloudsModel) props.push({ model: cloudsModel, sky: 'clouds' });
  if (props.length) out.props = props;
  const pp = keep.pedsRaw || (discPeds ? peds.patch() : null);
  if (pp || Object.keys(keep.peds).length) out.peds = Object.assign({}, pp, keep.peds);
  return Object.assign(out, keep.top);
}

function resetAll() {
  skyModel = cloudsModel = '';
  for (const r of [...propRecs]) removeProp(r);
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
  keep = { top: {}, objects: [], names: {}, peds: {}, pedsRaw: null, props: [] };
  for (const [k, v] of Object.entries(patch)) if (!['objects', 'names', 'peds', 'props'].includes(k)) keep.top[k] = v;
  if (patch.props !== undefined && !Array.isArray(patch.props)) keep.top.props = patch.props;
  skyModel = cloudsModel = '';
  for (const e of Array.isArray(patch.props) ? patch.props : []) {
    if (e && typeof e === 'object' && e.sky === true && typeof e.model === 'string' && e.model) { skyModel = propKey(e.model); continue; }
    if (e && typeof e === 'object' && e.sky === 'clouds' && typeof e.model === 'string' && e.model) { cloudsModel = propKey(e.model); continue; }
    if (!makeProp(e)) keep.props.push(e);
  }
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
  updateGridView();
  updateSkyView();
  updateClearView();
  loadSpt();
  pedSel = null;     // the markers are made again
  peds.load(discPeds, keep.pedsRaw ? null : pp);
  applyPedsView();
  showPedCounts();
  return { entries: keep.objects.length + Object.keys(keep.names).length, props: keep.props.length,
           keys: Object.keys(keep.top).filter((k) => k !== 'grid' && k !== 'walls').concat(Object.keys(keep.peds).map((k) => 'peds.' + k)) };
}
// the note over the view about what the file keeps that the editor does not show
function showKeep(k) {
  const title = stageTitles[stageName] || stageName, parts = [];
  if (k.entries) parts.push(`${k.entries} ${k.entries === 1 ? 'change' : 'changes'} in this file ${k.entries === 1 ? 'is' : 'are'} for objects ${title} doesn't have.`);
  if (k.props) parts.push(`${k.props} new ${k.props === 1 ? 'prop is' : 'props are'} of a model mod "${modNow}" doesn't have for ${title} (its props folder).`);
  if (k.keys.length) parts.push(`The file also has ${k.keys.map((x) => `"${x}"`).join(', ')}, which the editor doesn't edit.`);
  $('keepNote').hidden = !parts.length;
  if (!parts.length) return;
  const n = k.entries + (k.props || 0) + k.keys.length;
  $('keepText').textContent = parts.join(' ') + ' Saving keeps ' + (n === 1 ? 'it' : 'them') + ' in the file as ' +
    (n === 1 ? 'it is.' : 'they are.');
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
  // a new prop is made again from the patch: the same id picks its new item
  selectMany(again.map((it) => (it.rec.prop ? propRecs.find((r) => r.prop.id === it.rec.prop.id)?.item : it)).filter(Boolean));
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
  queueBudget();
  queueColl();
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
  showBudget();
  const n = group.length || (pedSel ? 1 : 0);
  stField('count', `${ui.changed} changed` + (n ? ` · ${n} selected` : ''));
  showGame();
}

// The stage's fixed room the patch uses: the 8 MB arena (copies, props, a bigger grid) and the 1024 Interactives slots (every building,
// pickup and breakable prop). Asked of the server (/api/budget, the patch applied without the collision check) a moment after each edit.
let budgetTimer = 0, budgetKey = '', budgetSeq = 0;
const kb = (n) => (n >= 1048576 ? `${(n / 1048576).toFixed(1)} MB` : `${Math.max(1, Math.round(n / 1024))} KB`);
function queueBudget() {
  clearTimeout(budgetTimer);
  if (!stageName) return;
  budgetTimer = setTimeout(refreshBudget, 500);
}
async function refreshBudget() {
  const body = { name: stageName, mod: modNow, patch: buildPatch() };
  const key = JSON.stringify(body);
  if (key === budgetKey && ui.budget) return;
  const seq = ++budgetSeq;
  try {
    const res = await api('/api/budget', body);
    if (seq !== budgetSeq || body.name !== stageName) return;   // a newer edit or another stage came meanwhile
    ui.budget = res;
    budgetKey = key;
  } catch (e) {
    if (seq !== budgetSeq) return;
    ui.budget = null;
  }
  showStatus();
}
function showBudget() {
  const b = document.querySelector('#status .budget'), r = ui.budget;
  b.hidden = !r;
  if (!r) return;
  const sl = r.interactives, ar = r.arena;
  const ps = sl.used / sl.total, pa = ar.used / ar.total;
  const level = r.full || ps >= 1 || pa >= 1 ? 'err' : ps >= 0.8 || pa >= 0.8 ? 'warn' : 'idle';
  stField('budget', `${sl.used}/${sl.total} slots · arena ${ar.used ? kb(ar.used) : '0'}` + (level === 'idle' ? '' : ' · nearly full'), level,
          `Room this stage's patch uses.\nBreakable slots: ${sl.used} of ${sl.total} (${sl.disc} on the disc), as the patch counts them: ` +
          `a copy of a building, a pickup or a breakable prop takes some, a delete does not give them back.\n` +
          `Arena: ${ar.used.toLocaleString('en-US')} of ${ar.total.toLocaleString('en-US')} bytes (copies, props, a bigger grid).` +
          (r.full ? `\nIt ran out: ${r.full}` : ''));
}

// The Collision layer: the server applies the patch to a fresh copy of the stage and sends the shapes the game collides with
// (/api/collision, tools/level_editor/collview.py). Asked for when the layer is on, a moment after each edit; the paths that touch the
// selection keep their color and the rest fade.
let collOn = store.get('coll', '0') === '1', collTimer = 0, collKey = '', collSeq = 0, collFailed = false;
function queueColl() {
  clearTimeout(collTimer);
  if (!collOn || !stageName) return;
  collTimer = setTimeout(refreshColl, 600);
}
async function refreshColl() {
  if (!collOn || !stageName) return;
  const body = { name: stageName, mod: modNow, patch: buildPatch() };
  const key = JSON.stringify(body);
  if (key === collKey && coll.loaded) return;
  const seq = ++collSeq;
  $('collInfo').textContent = 'Reading the collision…';
  try {
    const res = await api('/api/collision', body);
    if (seq !== collSeq || body.name !== stageName) return;   // a newer edit or another stage came meanwhile
    coll.set(res);
    collKey = key;
    collFailed = false;
    collHighlight();
  } catch (e) {
    if (seq !== collSeq) return;
    coll.clear();
    collKey = '';
    if (!collFailed) msg(`Couldn't read the collision: ${cleanError(e).text}`, { level: 'warn', toast: true });
    collFailed = true;
  }
  showColl();
}
function collHighlight() {
  coll.highlight(isDeleted(sel) ? null : group.filter((it) => it.node.visible).map(gameBox));
}
function showColl() {
  const i = coll.info;
  $('collOn').checked = collOn;
  $('collInfo').hidden = !collOn;
  if (collOn && coll.loaded) {
    $('collInfo').textContent = `${i.paths} collision paths · ${i.triangles} triangles · ${i.boxes} drawn as boxes`;
  } else if (collOn && collFailed) $('collInfo').textContent = 'No collision to show';
}
function setColl(on) {
  collOn = on;
  store.set('coll', on ? '1' : '0');
  coll.setVisible(on);
  if (on) refreshColl(); else { clearTimeout(collTimer); collSeq++; }
  showColl();
}

// ------------------------------------------------------------------ loading
async function loadStage(name) {
  $('busyText').textContent = `Loading ${title(name)}…`;
  $('busy').hidden = false;
  hoverBox.visible = false;
  hover.item = null;
  try {
    select(null);
    for (const r of [stageRoot, skyRoot]) if (r) { scene.remove(r); r.traverse((o) => { if (o.geometry) o.geometry.dispose(); }); }
    stageRoot = skyRoot = null;
    lightClose();
    stageName = name;
    ui.budget = null; budgetKey = '';
    coll.clear(); collKey = ''; collSeq++;
    $('keepNote').hidden = true;
    $('winNote').hidden = true;
    winHidden = false;
    clearCheckPane();
    const mod = modNow;
    const info = await api(`/api/stage?name=${encodeURIComponent(name)}&mod=${encodeURIComponent(mod)}`);
    stageHave = info.have || [];
    const gltf = await new GLTFLoader().loadAsync(`/api/glb?name=${encodeURIComponent(name)}`);
    env = info.env || null;
    discPeds = info.peds || null;
    stageGrid = info.grid || null;
    stageWalls = info.walls || 0;
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
    stageTex = new Map();
    stageRoot.traverse((o) => {
      if (!o.isMesh) return;
      for (const m of [].concat(o.material)) { const t = /^tex(\d+)$/.exec(m.name || ''); if (t && m.map) stageTex.set(+t[1], m.map); }
    });
    propRecs = [];
    propRoot = new THREE.Group();
    propRoot.name = 'new props';
    stageRoot.add(propRoot);
    await loadPropModels(mod);
    const kept = applyPatch(info.patch);
    savedJson = JSON.stringify(buildPatch());
    dirty = false;
    ui.savedAt = 0;
    ui.check = { state: 'none', n: 0 };
    showUndo();
    frameAll();
    showKeep(kept);
    const w = showWinner();
    if (w) msg(winnerText(w), { level: 'warn' });
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
  for (const r of propRecs) out.push(r.item);
  return out;
}
function changed(rec) {
  if (rec.prop || rec.deleted || rec.copies.length) return true;
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
    if (!kinds.has(i.kind === 'newprop' ? 'prop' : i.kind) || (onlyChanged && !changed(it.rec))) return false;
    return !f || `${label(it)} ${i.rule} ${i.handle} ${i.kind} ${i.id.toString(16)}`.toLowerCase().includes(f);
  });
}
// The list's sections, in this order; a prop over 500 units (the rule's "Terrain piece") has its own, closed at first
const SECTIONS = [
  { id: 'buildings', title: 'Buildings', icon: 'office-building-outline', word: 'Building', many: 'buildings' },
  { id: 'pickups', title: 'Pickups', icon: 'package-variant-closed', word: 'Pickup', many: 'pickups' },
  { id: 'props', title: 'Props', icon: 'cube-outline', word: 'Prop', many: 'props' },
  { id: 'terrain', title: 'Terrain', icon: 'terrain', word: 'Terrain piece', many: 'terrain pieces' },
  { id: 'newprops', title: 'New props', icon: 'shape-plus-outline', word: 'New prop', many: 'new props' },
];
const SEC = Object.fromEntries(SECTIONS.map((s) => [s.id, s]));
const secOf = (i) => (i.kind === 'newprop' ? 'newprops' : i.kind === 'destr' ? 'buildings' : i.kind === 'pickup' ? 'pickups' : i.rule === 'Terrain piece' ? 'terrain' : 'props');
const secOpen = (id) => store.get('sec.' + id, id === 'terrain' ? '0' : '1') === '1';
const setIc = (el, name) => el.style.setProperty('--ic', `url(vendor/mdi/${name}.svg)`);
// what changed in an original: moved, turned (copies and deleted are said apart)
function moveState(rec) {
  if (rec.prop) return { moved: false, turned: false };   // a new prop has no disc place: it is said "New"
  const p = posOf(rec.item);
  return { moved: Math.hypot(p[0] - rec.pw[0], p[1] - rec.pw[1], p[2] - rec.pw[2]) > 0.05,
           turned: Math.abs(wrapDeg(headingOf(rec.item) - rec.h0)) > 0.05 };
}
// the objects resting on the selected ones that are not selected themselves (Move objects on top takes them along)
function onTop(list = group) {
  const on = new Set();
  for (const it of list) for (const a of it.attached) if (a.node.visible && !list.includes(a)) on.add(a);
  return on;
}
// "3 buildings, 2 pickups"
function kindCounts(list) {
  const n = new Map();
  for (const it of list) { const s = secOf(it.rec.info); n.set(s, (n.get(s) || 0) + 1); }
  return SECTIONS.filter((s) => n.has(s.id)).map((s) => `${n.get(s.id)} ${n.get(s.id) === 1 ? s.word.toLowerCase() : s.many}`).join(', ');
}
function rowEl(it) {
  const i = it.rec.info, li = document.createElement('li');
  li.className = 'it';
  if (it.copy) li.classList.add('copy');
  else if (!it.iname && i.nameSrc === 'rule') li.classList.add('rule');
  if (it === sel) li.classList.add('sel');
  else if (group.includes(it)) li.classList.add('sel2');
  const k = icon(SEC[secOf(i)].icon);
  k.classList.add('k');
  const h = document.createElement('span'), bd = document.createElement('span'), hd = document.createElement('span');
  h.className = 'h'; bd.className = 'bd'; hd.className = 'hd';
  h.textContent = it.copy ? it.iname || `Copy ${it.rec.copies.indexOf(it) + 1}` : nameOf(it);
  hd.textContent = i.handle;
  const badge = (name, cls, tip) => { const b = icon(name); b.classList.add(cls); b.title = tip; bd.appendChild(b); };
  if (it.rec.prop) badge('shape-plus-outline', 'cp', `New prop: model ${i.model} of your mod`);
  let tip = label(it);
  if (!it.copy) {
    const rec = it.rec;
    if (rec.deleted) { li.classList.add('deleted'); badge('delete-outline', 'dl', 'Deleted in your mod (click it to restore it)'); tip += ' · deleted'; }
    else {
      const st = moveState(rec);
      if (st.moved || st.turned) badge('circle-medium', 'mv', st.moved && st.turned ? 'Moved and turned' : st.moved ? 'Moved' : 'Turned');
    }
    if (rec.copies.length) badge('content-duplicate', 'cp', `${rec.copies.length} ${rec.copies.length === 1 ? 'copy' : 'copies'}`);
    if (i.on) {
      const b = byHandle.get(normHandle(i.on));
      badge('link-variant', 'on', `On ${b ? nameOf(b.item) : i.on}`);
    }
  }
  li.title = tip;
  li.append(k, h, bd, hd);
  li.onclick = (e) => {
    if (e.shiftKey || e.ctrlKey) select(it, 'toggle');
    else { select(it); if (it.node.visible) focus(it); }
  };
  return li;
}
function refreshList() {
  const ul = $('list');
  const shown = listed(), all = items();
  const bySec = new Map(SECTIONS.map((s) => [s.id, []]));
  for (const it of shown) bySec.get(secOf(it.rec.info)).push(it);
  const frag = document.createDocumentFragment();
  for (const s of SECTIONS) {
    const list = bySec.get(s.id);
    if (!list.length) continue;
    const open = secOpen(s.id), h = document.createElement('li'), n = document.createElement('span');
    h.className = 'sec';
    h.dataset.sec = s.id;
    h.tabIndex = 0;
    h.setAttribute('role', 'button');
    h.setAttribute('aria-expanded', open);
    h.title = open ? `Hide the ${s.title.toLowerCase()}` : `Show the ${s.title.toLowerCase()}`;
    h.append(icon(open ? 'chevron-down' : 'chevron-right'), s.title);
    if (!open && list.some((it) => group.includes(it))) {
      const d = document.createElement('span');
      d.className = 'has'; d.title = 'Holds selected objects';
      h.appendChild(d);
    }
    n.className = 'n'; n.textContent = list.length;
    h.appendChild(n);
    const flip = () => { store.set('sec.' + s.id, open ? '0' : '1'); refreshList(); };
    h.onclick = flip;
    h.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); flip(); ul.querySelector(`li.sec[data-sec="${s.id}"]`)?.focus(); } };
    frag.appendChild(h);
    if (open) for (const it of list) frag.appendChild(rowEl(it));
  }
  ul.replaceChildren(frag);
  $('listEmpty').hidden = !!shown.length || !all.length;
  // the chips' counts: every object of the kind in the stage (copies too), and the changed originals
  const byKind = { destr: 0, pickup: 0, prop: 0 };
  for (const it of all) byKind[it.rec.info.kind] = (byKind[it.rec.info.kind] || 0) + 1;
  for (const [k, v] of Object.entries(byKind)) { const e = document.querySelector(`.kinds .n[data-n="${k}"]`); if (e) e.textContent = v; }
  const nRecs = recs.filter(changed).length + propRecs.length;
  document.querySelector('.kinds .n[data-n="changed"]').textContent = nRecs;
  const bp = buildPatch(), nChanged = bp.objects.length + (bp.props || []).length;
  $('count').textContent = (shown.length === all.length ? `${all.length} objects` : `${shown.length} of ${all.length} shown`) +
    ` · ${nChanged} changed` + (group.length ? ` · ${group.length} selected` : '');
  document.title = `WotM Level Editor — ${stageName}${dirty ? ' *' : ''}`;
  $('save').classList.toggle('dirty', dirty);
  showStatus(nChanged);
  showSummary(nChanged);
  syncMarks();
  if (sel && !isDeleted(sel) && !$('props').hidden) { showBadge(); showActions(); }
  if (editMode === 'peds') refreshPedList();
}
// the right panel with nothing selected: the stage, how many objects the mod changes, where Save writes
function showSummary(nChanged) {
  const t = title(stageName);
  $('sumStage').textContent = !stageName ? 'No stage open' : t === stageName ? t : `${t} · ${stageName}`;
  $('sumCounts').textContent = stageName ? `${recs.length} objects · ${nChanged} changed` : '';
  const m = $('sumMod');
  m.textContent = '';
  m.classList.toggle('none', !modNow);
  if (modNow) { const b = document.createElement('b'); b.textContent = modNow; m.append('Mod: ', b); }
  else m.textContent = 'No mod chosen yet: pick or type one in Mod, at the top.';
  const p = $('sumPath');
  p.classList.toggle('none', !modNow);
  p.textContent = modNow ? `mods/${modNow}/levels/${stageName}.json` : 'The mod you choose (mods/<mod>/levels/).';
  p.title = modNow && cfg.mods ? `${cfg.mods.replace(/[\\/]+$/, '')}${cfg.mods.includes('\\') ? '\\' : '/'}${modNow}${cfg.mods.includes('\\') ? '\\levels\\' : '/levels/'}${stageName}.json` : '';
}

// mode: 'set' (only this one), 'toggle' (Shift/Ctrl click), 'add'
const isDeleted = (it) => !!it && !it.copy && it.rec.deleted;
function select(item, mode = 'set') {
  if (item && !item.node.visible && !isDeleted(item)) item = null;
  if (item && editMode === 'peds') enterMode('objects');   // an object (from a Check problem, Resting on) takes the Objects mode
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
  if (vis.length && editMode === 'peds') enterMode('objects');
  pedSel = null;
  group = add ? [...group, ...vis.filter((it) => !group.includes(it))] : vis;
  sel = group[group.length - 1] || null;
  showSel();
}
function showSel() {
  gizmo.detach();
  if (sel) pedSel = null;
  peds.highlight(pedSel);
  const gone = isDeleted(sel), pm = editMode === 'peds';
  $('props').hidden = !sel || gone;
  $('delProps').hidden = !gone;
  $('pedProps').hidden = !pedSel;
  $('noSel').hidden = pm || !!sel;
  $('pedNoSel').hidden = !pm || !!pedSel;
  if (pedSel) {
    if (gizmo.getMode() !== 'translate') setTool('translate');
    gizmo.attach(pedSel.node);
    showPed();
  }
  if (gone) {
    const i = sel.rec.info, s = SEC[secOf(i)];
    setIc($('dIcon').firstElementChild, s.icon);
    $('dTitle').textContent = label(sel);
    $('dSub').textContent = `${i.handle} · ${s.word}`;
  } else if (sel) {
    gizmo.attach(sel.node);
    showPropsPanel();
  }
  $('focusSel').disabled = !sel && !pedSel;
  $('tTurn').disabled = pm;   // pedestrian markers only move
  $('tTurn').title = pm ? 'Pedestrian markers only move' : 'Turn tool (R)';
  syncBoxes();
  if (collOn) collHighlight();
  refreshList();
  showHint();
  hover.dirty = true;
  const shown = sel || pedSel;
  if (shown !== lastScrolled) {   // a pick in the view shows its row (a list click is in view already)
    lastScrolled = shown;
    $(pm ? 'pedList' : 'list').querySelector('li.it.sel')?.scrollIntoView({ block: 'nearest' });
  }
}
let lastScrolled = null;
// one object: its header, Transform, Actions, Name and Details; several: how many of each kind, the lead one's Transform, Actions
function showPropsPanel() {
  const i = sel.rec.info, s = SEC[secOf(i)], multi = group.length > 1;
  $('props').classList.toggle('multi', multi);
  const kinds = new Set(group.map((it) => secOf(it.rec.info)));
  setIc($('pIcon').firstElementChild, multi && kinds.size > 1 ? 'cube-outline' : s.icon);
  $('pHandle').hidden = multi || !!sel.rec.prop;
  $('pGroup').hidden = !multi;
  $('secName').hidden = $('secDetails').hidden = multi;
  const badge = $('pBadge');
  if (multi) {
    $('pTitle').textContent = `${group.length} objects selected`;
    $('pKind').textContent = kindCounts(group);
    $('tLead').textContent = label(sel);
    $('tLead').title = `The fields move ${label(sel)}; the others follow it`;
    badge.hidden = true;
  } else {
    $('pTitle').textContent = label(sel);
    $('pHandle').querySelector('.tx').textContent = i.handle;
    $('pHandle').title = `Copy the handle ${i.handle}`;
    $('pKind').textContent = s.word;
    $('tLead').textContent = '';
    showBadge();
  }
  // Name: the instance's own name and the model's (every stage)
  $('pName').value = sel.iname;
  $('pName').placeholder = i.name;
  $('pModel').value = i.nameSrc === 'model' ? i.name : '';
  $('pModel').placeholder = i.sig ? i.rule : 'No model name';
  $('pModel').disabled = !i.sig;
  const same = recs.filter((r) => r.info.sig && r.info.sig === i.sig).length;
  $('pModelNote').textContent = i.sig ? `Every object with this model, in every stage (${same} in this stage). Not undoable.`
    : sel.rec.prop ? "A new prop's model is named by its folder in the mod." : 'This object has no mesh, so it has no model name.';
  // Details
  const dl = $('pFacts');
  dl.textContent = '';
  const fact = (k, v, cls) => {
    const dt = document.createElement('dt'), dd = document.createElement('dd');
    dt.textContent = k;
    if (v instanceof Node) dd.appendChild(v); else dd.textContent = v;
    if (cls) dd.className = cls;
    dl.append(dt, dd);
  };
  fact('Size', `${dimsText(i.dims)} units`);
  if (sel.rec.prop) {
    const p = propModels.get(i.model)?.info || {};
    const uses = propRecs.filter((r) => r.prop.model === i.model).length;
    $('pModel').disabled = false;
    $('pModel').value = p.name && p.name !== i.model ? p.name : '';
    $('pModel').placeholder = i.model;
    $('pModelNote').textContent = `The name of model ${i.model} in the list, for every entry of it (${uses} in this stage). Not undoable.`;
    // Height: the model is built again from its .glb at that size; every entry of it takes it
    const hb = document.createElement('span'), hin = document.createElement('input');
    hb.className = 'hfield';
    hin.type = 'number';
    hin.id = 'pPropH';
    hin.min = '0.1';
    hin.step = 'any';
    hin.value = String(Math.round(i.dims[2] * 10) / 10);
    hin.title = `Scale model ${i.model} to this many game units high (every entry of it; not undoable)`;
    hin.onchange = () => resizeProp(i.model, +hin.value);
    hin.onkeydown = (e) => { if (e.key === 'Enter') { e.preventDefault(); hin.blur(); } };
    scrubField(hin);
    hb.append(hin, document.createTextNode(' units'));
    fact('Height', hb);
    if (p.destructible) {     // the hit point class of the model: prop.json only, read at every load
      const sb = document.createElement('select');
      sb.id = 'pPropStr';
      sb.title = `How much damage model ${i.model} takes before each state moves on (every entry of it; not undoable)`;
      for (const [c, hp] of [[1, 10], [2, 50], [3, 100], [4, 150], [5, 250]]) sb.add(new Option(`${hp} hit points${c === 1 ? ', any hit counts' : ''}`, String(c)));
      sb.value = String(p.destructible.class);
      sb.onchange = () => editPropModel(i.model, { strength: +sb.value }, () => `${i.model} takes ${sb.selectedOptions[0].text.split(',')[0]} to break` +
        (game.state === 'running' ? '; Test in game shows it' : ''));
      fact('Strength', sb);
    }
    fact('Kind', p.destructible ? `${s.word}, destructible (${p.destructible.states} states)` : s.word);
    fact('Model', i.model, 'mono');
    if (p.triangles != null) fact('Triangles', String(p.triangles));
    if (p.light) fact('Light', { vertex: 'Vertex colors', dir: 'Stage sun', mixed: 'Vertex colors and stage sun' }[p.light] || p.light);
    fact('Folder', `mods/${modNow}/props/${i.model}`, 'mono');
    fact('Entry id', sel.rec.prop.id, 'mono');
    $('pWarn').hidden = true;
    showActions();
    showProps();
    return;
  }
  fact('Kind', i.rule && i.rule !== s.word ? `${s.word} (${i.rule})` : s.word);
  fact('Model id', `0x${i.id.toString(16).toUpperCase()}`, 'mono');
  if (i.sig) fact('Same model', `${same} in this stage`);
  fact('Damage states', String(i.states));
  fact('Collision boxes', String(i.paths));
  fact('On the disc', `${i.pos.map((x) => Math.round(x)).join(', ')} · ${Math.round(i.heading)}°`);
  if (i.on) {
    const b = byHandle.get(normHandle(i.on)), a = document.createElement('a');
    a.textContent = b ? nameOf(b.item) : i.on;
    a.title = `Select it (${i.on})`;
    a.onclick = (e) => { e.preventDefault(); goTo(i.on); };
    fact('Resting on', a);
  }
  $('pWarn').hidden = multi || !(i.paths === 0 && i.kind !== 'pickup');
  showActions();
  showProps();
}
// the header's state: Copy, Moved, Turned, or how many copies it has
function showBadge() {
  const badge = $('pBadge');
  if (group.length > 1) { badge.hidden = true; return; }
  const st = sel.copy ? null : moveState(sel.rec);
  const text = sel.copy ? 'Copy' : sel.rec.prop ? 'New' : st.moved && st.turned ? 'Moved · Turned' : st.moved ? 'Moved' : st.turned ? 'Turned'
    : sel.rec.copies.length ? `${sel.rec.copies.length} ${sel.rec.copies.length === 1 ? 'copy' : 'copies'}` : '';
  badge.textContent = text;
  badge.className = 'badge' + (sel.copy || sel.rec.prop || (text && !st.moved && !st.turned) ? ' copy' : '');
  badge.hidden = !text;
}
// Delete says how many go ("Delete 5 (+3 on top)"); Move objects on top, how many it takes; Restore, whether there is anything to restore
function showActions() {
  if (!sel || isDeleted(sel)) return;
  const on = onTop(), carryOn = $('carry').checked, n = group.filter((it) => it.node.visible).length;
  const sw = $('carry').closest('.switch');
  $('onCount').textContent = on.size;
  $('carry').disabled = !on.size;
  sw.classList.toggle('none', !on.size);
  sw.title = on.size ? 'Moving, turning, copying or deleting this also does it to the pickups resting on it'
    : 'Nothing rests on the selection';
  const del = (n > 1 ? `Delete ${n}` : 'Delete') + (carryOn && on.size ? ` (+${on.size} on top)` : '');
  $('del').querySelector('.lbl').textContent = del;
  $('del').title = `${del} (Del)`;
  const rs = new Set();
  for (const it of group) { rs.add(it.rec); if (carryOn) for (const a of it.rec.item.attached) rs.add(a.rec); }
  $('reset').disabled = ![...rs].some((r) => !r.prop && changed(r));
  arrange.changed();
  $('reset').title = $('reset').disabled ? 'Nothing to restore: it is where the disc has it'
    : "Back to the disc's place, copies removed (with Move objects on top, also the objects on it)";
}
// every frame: the selection's outlines (they follow the gizmo), the changed ones' and the grid's height
function syncBoxes() {
  if (hoverBox.visible && (!hover.item || group.includes(hover.item))) hoverBox.visible = false;
  while (boxes.length < group.length) boxes.push(lineBox(SEL2_COLOR, { opacity: 0.8 }));
  boxes.forEach((b, n) => {
    const it = group[n];
    b.visible = !!it && it.node.visible;
    if (!b.visible) return;
    b.setFromObject(it.node);
    const lead = it === sel, m = b.material;
    if (m.depthTest === lead) {   // the lead one through the walls, on top of everything; the others in the scene's depth
      m.depthTest = !lead;
      m.opacity = lead ? 0.95 : 0.8;
      b.renderOrder = lead ? 1000 : 0;
      m.color.set(lead ? SEL_COLOR : SEL2_COLOR);
    }
  });
  marks.changed.forEach((b, n) => {
    const it = marks.changedItems[n];
    b.visible = !!it && it.node.visible && !group.includes(it);
    if (b.visible) b.setFromObject(it.node);
  });
  if (gridOn) {   // at the base of the selection, else at the stage's origin; the small lines around the selection
    const box = sel ? new THREE.Box3().setFromObject(sel.node) : null;
    const y = box && !box.isEmpty() ? box.min.y + 0.5 : 0;
    gridMajor.position.y = y;
    gridMinor.position.y = y + 0.25;
    if (gridMinorStep < 100) {
      const c = box && !box.isEmpty() ? box.getCenter(new THREE.Vector3()) : new THREE.Vector3();
      const s = gridMinorStep * 2;   // even division counts: lines on the step's multiples
      gridMinor.position.x = Math.round(c.x / s) * s;
      gridMinor.position.z = Math.round(c.z / s) * s;
    } else gridMinor.position.x = gridMinor.position.z = 0;
  }
}
// The grid's small lines: 100 units, or the snap's move step when it is smaller (at most 2000 lines, around the selection)
function setGrid(on = gridOn) {
  gridOn = on;
  const snap = $('snap').checked ? snapMove() : 100;
  const step = snap < 100 ? snap : 100;
  if (step !== gridMinorStep) {
    const color = gridMinor.material.color.getHex();
    scene.remove(gridMinor);
    gridMinor.geometry.dispose(); gridMinor.material.dispose();
    const div = step < 100 ? Math.min(2000, Math.round(20000 / step)) : 200;
    gridMinor = gridHelper(div * step, div, 0.10);
    gridMinor.material.color.setHex(color);
    gridMinorStep = step;
  }
  gridMinor.visible = gridMajor.visible = on;
  $('grid').checked = on;
}

// Deleted originals and changed objects in the view: a dashed red box where a deleted one was (when it is selected, or with the
// Changed chip on; a click on it selects it), and a green box around each changed one that is not selected (Changed on).
// refreshList rebuilds them (syncMarks); the green ones follow their object every frame (syncBoxes).
const DEL_MAT = new THREE.LineDashedMaterial({ color: DEL_COLOR, dashSize: 20, gapSize: 14, transparent: true, opacity: 0.7, fog: false, depthWrite: false });
const BOX_EDGES = [0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 2, 6, 3, 7];
const marks = { deleted: [], changed: [], changedItems: [], root: new THREE.Group() };
marks.root.name = 'marks';
scene.add(marks.root);
function dashedBox(box) {
  const p = [];
  for (const k of BOX_EDGES) {
    p.push(k & 1 ? box.max.x : box.min.x, k & 2 ? box.max.y : box.min.y, k & 4 ? box.max.z : box.min.z);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(p, 3));
  const l = new THREE.LineSegments(g, DEL_MAT);
  l.computeLineDistances();
  return l;
}
function syncMarks() {
  for (const m of marks.deleted) { marks.root.remove(m.line); m.line.geometry.dispose(); }
  marks.deleted = [];
  const all = $('changedOnly').checked;
  for (const rec of recs) {
    if (!rec.deleted || !(all || rec.item === sel)) continue;
    const box = new THREE.Box3().setFromObject(rec.node);   // a hidden node still has its box
    if (box.isEmpty()) continue;
    const line = dashedBox(box);
    marks.root.add(line);
    marks.deleted.push({ item: rec.item, box, line });
  }
  const ch = [];
  if (all) {
    for (const rec of recs) {
      if (!changed(rec)) continue;
      if (!rec.deleted) { const st = moveState(rec); if (st.moved || st.turned) ch.push(rec.item); }
      ch.push(...rec.copies);
    }
  }
  while (marks.changed.length < ch.length) marks.changed.push(lineBox(CHANGED_COLOR, { opacity: 0.5 }));
  for (const b of marks.changed.slice(ch.length)) b.visible = false;
  marks.changedItems = ch;
}
// the dashed box a ray meets first, nearer than `far` (the first surface); not one the camera is inside
function deletedHit(ray, far = Infinity) {
  let best = null, bd = far;
  const p = new THREE.Vector3();
  for (const m of marks.deleted) {
    if (m.box.containsPoint(ray.ray.origin) || !ray.ray.intersectBox(m.box, p)) continue;
    const d = p.distanceTo(ray.ray.origin);
    if (d <= bd) { bd = d; best = m.item; }
  }
  return best;
}

// Hover: what a click would pick, outlined in white, and the pointer as a hand over it. At most 30 raycasts a second, from the
// render loop, and none while a button is down (drag, look, band, gizmo) or the gizmo has the pointer.
const hover = { at: null, last: 0, item: null, n: 0, dirty: false };
canvas.addEventListener('pointermove', (e) => { hover.at = ndc(e); hover.dirty = true; hover.buttons = e.buttons; });
canvas.addEventListener('pointerleave', () => { hover.at = null; hover.dirty = true; });
// what is under the pointer at p (NDC): {item} an object, {ped} a pedestrian marker, {deleted} a dashed box, or null
function pickAt(p, add = false) {
  const ray = new THREE.Raycaster();
  ray.setFromCamera(p, camera);
  const hits = stageRoot ? ray.intersectObject(stageRoot, true).filter((h) => visibleChain(h.object)) : [];
  if (editMode === 'peds') {   // only the markers, in front of the stage's first surface (lanes lie just above the sidewalk)
    if (add) return null;
    const ph = ray.intersectObjects(peds.items().map((it) => it.pick), false)[0];
    return ph && (!hits.length || ph.distance <= hits[0].distance + 2) ? { ped: ph.object.userData.pedItem } : null;
  }
  const d = deletedHit(ray, hits.length ? hits[0].distance : Infinity);
  if (d) return { deleted: d };
  if (hits.length) for (let o = hits[0].object; o; o = o.parent) if (itemOf.has(o)) return { item: itemOf.get(o) };
  return null;
}
function hoverStep() {
  if (!hover.dirty) return;
  const now = performance.now();
  if (now - hover.last < 33) return;
  const busy = hover.buttons || gizmo.dragging || look || band || !stageRoot;
  hover.dirty = false;
  hover.last = now;
  let it = null;
  if (!busy && hover.at && gizmo.axis === null) {
    hover.n++;
    const h = pickAt(hover.at);
    it = h ? h.item || h.deleted || h.ped || null : null;
  }
  hover.item = it;
  const obj = it && !it.kind ? it : null;   // an object or a deleted original (pedestrian markers have a kind)
  hoverBox.visible = !!obj && obj.node.visible && !group.includes(obj);
  if (hoverBox.visible) hoverBox.setFromObject(obj.node);
  canvas.style.cursor = gizmo.axis !== null && !busy ? 'move' : it ? 'pointer' : '';
}
function showProps() {
  if (!sel || isDeleted(sel)) return;
  const p = posOf(sel), h = headingOf(sel);
  $('px').value = r1(p[0]); $('py').value = r1(p[1]); $('pz').value = r1(p[2]); $('ph').value = r1(h);
}

// ------------------------------------------------------------------ the modes: Objects (1) and Pedestrians (2)
// Objects: a click picks the stage's objects; the Pedestrians layer (P, or the Layers menu) shows the markers faint, not clickable.
// Pedestrians: the left panel lists the groups and the sidewalks, a click picks a marker, the objects are scenery.
let editMode = store.get('mode', 'objects') === 'peds' ? 'peds' : 'objects';
let pedsOn = store.get('peds', '0') === '1';   // the Pedestrians layer, for the Objects mode
const modeNow = () => editMode;
function applyPedsView() {
  const pm = editMode === 'peds';
  peds.setVisible(pm || pedsOn);
  peds.setDim(!pm);
  $('pedsOn').checked = pm || pedsOn;
  $('pedsOn').disabled = pm;
  $('pedsOn').closest('.switch').classList.toggle('none', pm);
  $('pedBar').hidden = !pm;
  $('propBar').hidden = pm;
  if (pm) showPropMenu(false);
  document.body.dataset.mode = editMode;
}
function setPeds(on) {   // the layer
  pedsOn = on;
  store.set('peds', on ? '1' : '0');
  applyPedsView();
  showHint();
  hover.dirty = true;
}
// the mode's own state, without showing the selection (select and selectPed show theirs after it)
function enterMode(m) {
  if (m === editMode) return false;
  editMode = m;
  store.set('mode', m);
  if (m === 'peds') { group = []; sel = null; } else pedSel = null;
  if (m === 'peds' && gizmo.getMode() === 'rotate') setTool('translate');
  applyPedsView();
  showMode();
  hover.dirty = true;
  return true;
}
function setMode(m) { if (enterMode(m)) showSel(); }
function showMode() {
  for (const [id, m] of [['modeObjects', 'objects'], ['modePeds', 'peds']]) {
    $(id).classList.toggle('on', editMode === m);
    $(id).setAttribute('aria-pressed', editMode === m);
  }
}
// the meters, the switch, Add group and Add sidewalk (off at a limit, with the reason in the title), the footer
function meter(id, n, max) {
  const m = $(id), f = n / max;
  m.querySelector('.m-v').textContent = `${n} / ${max}`;
  const bar = m.querySelector('.m-bar > i');
  bar.style.width = Math.min(100, f * 100) + '%';
  m.className = 'meter' + (n >= max ? ' full' : f > 0.9 ? ' high' : '');
}
function showPedCounts() {
  const c = peds.counts(), have = !!discPeds;
  meter('mGroups', c.groups, c.maxGroups);
  meter('mSegs', c.segments, c.maxSegments);
  $('mPeople').textContent = c.people;
  $('pedCounts').textContent = have
    ? `${c.groups}/${c.maxGroups} groups · ${c.people} people · ${c.segments}/${c.maxSegments} sidewalks`
    : stageName ? 'This stage has no pedestrian data' : '';
  const live = $('pedLive');
  live.checked = peds.live();
  live.disabled = !have || (!peds.live() && !peds.hasDiscGroups());
  const ag = $('pedAddGroup'), as = $('pedAddSeg');
  const noNet = have && !peds.items().some((it) => it.kind === 'seg');
  ag.disabled = !have || c.groups >= c.maxGroups || noNet;
  ag.title = !have ? 'This stage has no pedestrian data'
    : c.groups >= c.maxGroups ? `A stage holds ${c.maxGroups} pedestrian groups at most`
    : noNet ? 'This stage has no sidewalks yet: add one first'
    : 'Add a group of people where the view looks (it walks the sidewalk with the nearest end)';
  as.disabled = !have || c.segments >= c.maxSegments;
  as.title = !have ? 'This stage has no pedestrian data'
    : c.segments >= c.maxSegments ? `A stage holds ${c.maxSegments} sidewalks at most`
    : 'Add a sidewalk where the view looks; drag its ends into place';
}
// the left panel in the Pedestrians mode: the groups (people, a red mark when it has no sidewalk) and the sidewalks (length)
const PED_SECTIONS = [
  { id: 'pgroups', title: 'Groups', icon: 'account-group-outline' },
  { id: 'psegs', title: 'Sidewalks', icon: 'road-variant' },
];
function pedRow(e, kind) {
  const it = e.item, li = document.createElement('li');
  li.className = 'it';
  const on = pedSel && (pedSel === it || (kind === 'seg' && pedSel.kind === 'end' && pedSel.seg === it.seg));
  if (on) li.classList.add('sel');
  const k = icon(kind === 'group' ? 'account-group-outline' : 'road-variant');
  k.classList.add('k');
  const h = document.createElement('span'), bd = document.createElement('span'), hd = document.createElement('span');
  h.className = 'h'; bd.className = 'bd'; hd.className = 'hd num';
  h.textContent = peds.label(it);
  const badge = (name, cls, tip) => { const b = icon(name); b.classList.add(cls); b.title = tip; bd.appendChild(b); };
  if (kind === 'group') {
    if (!e.bound) { li.classList.add('bad'); badge('alert-circle-outline', 'dl', 'Not on any sidewalk: nobody walks'); }
    if (e.added) badge('account-multiple-plus', 'cp', 'A new group');
    else if (e.changed) badge('circle-medium', 'mv', 'Moved or changed');
    hd.textContent = e.count;
    hd.title = `${e.count} people`;
  } else {
    hd.textContent = `${e.len} u`;
    hd.title = `${e.len} units long, ${e.width * 10} units each side`;
  }
  li.title = peds.info(it);
  li.append(k, h, bd, hd);
  li.onclick = () => { selectPed(it); focusSelection(); };
  return li;
}
function refreshPedList() {
  showPedCounts();
  const ul = $('pedList'), en = peds.entries(), frag = document.createDocumentFragment();
  for (const s of PED_SECTIONS) {
    const rows = s.id === 'pgroups' ? en.groups : en.segments;
    const open = secOpen(s.id), h = document.createElement('li'), n = document.createElement('span');
    h.className = 'sec';
    h.dataset.sec = s.id;
    h.tabIndex = 0;
    h.setAttribute('role', 'button');
    h.setAttribute('aria-expanded', open);
    h.title = open ? `Hide the ${s.title.toLowerCase()}` : `Show the ${s.title.toLowerCase()}`;
    h.append(icon(open ? 'chevron-down' : 'chevron-right'), s.title);
    const holds = pedSel && rows.some((e) => e.item === pedSel || (pedSel.seg && e.item.seg === pedSel.seg));
    if (!open && holds) {
      const d = document.createElement('span');
      d.className = 'has'; d.title = 'Holds the selection';
      h.appendChild(d);
    }
    n.className = 'n'; n.textContent = rows.length;
    h.appendChild(n);
    const flip = () => { store.set('sec.' + s.id, open ? '0' : '1'); refreshPedList(); };
    h.onclick = flip;
    h.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); flip(); ul.querySelector(`li.sec[data-sec="${s.id}"]`)?.focus(); } };
    frag.appendChild(h);
    if (!open) continue;
    if (!rows.length) {
      const li = document.createElement('li');
      li.className = 'none';
      li.textContent = s.id === 'pgroups' ? 'No groups' : 'No sidewalks';
      frag.appendChild(li);
    }
    for (const e of rows) frag.appendChild(pedRow(e, s.id === 'pgroups' ? 'group' : 'seg'));
  }
  ul.replaceChildren(frag);
  ul.hidden = !discPeds;
  $('pedNone').hidden = !!discPeds || !stageName;
}
function selectPed(it) {
  enterMode('peds');
  group = []; sel = null; pedSel = it;
  showSel();
}
const pedTitle = (it) => peds.label(it);
function pedPos(it) { return it.kind === 'group' ? it.group.pos : it.kind === 'seg' ? toGame(it.node.position) : it.end ? it.seg.b : it.seg.a; }
// the right panel for a marker: what it is, what it walks (or how long it is), People or Width, Position, Actions
function showPed() {
  if (!pedSel) return;
  const it = pedSel, p = pedPos(it), grp = it.kind === 'group';
  setIc($('pdIcon').firstElementChild, grp ? 'account-group-outline' : 'road-variant');
  $('pdTitle').textContent = peds.label(it);
  const e = grp ? peds.entries().groups.find((x) => x.item === it) : null;
  const bt = e ? (e.added ? 'New' : e.changed ? 'Changed' : '') : '';
  $('pdBadge').textContent = bt;
  $('pdBadge').className = 'badge' + (bt === 'New' ? ' copy' : '');
  $('pdBadge').hidden = !bt;
  $('pdKind').textContent = grp ? 'Pedestrian group' : it.kind === 'seg' ? 'Sidewalk' : `Sidewalk end ${it.end ? 'B' : 'A'}`;
  $('pdInfo').textContent = peds.info(it);
  const b = grp ? peds.binding(it) : null;
  $('pdNote').className = 'callout ' + (grp && !b ? 'err' : 'info');
  setIc($('pdNote').firstElementChild, grp && !b ? 'alert-circle-outline' : 'information-outline');
  const go = grp ? b && b.seg && b.seg.item : it.kind === 'end' ? it.seg.item : null;
  $('pdGo').hidden = !go;
  $('pdGo').onclick = go ? () => { selectPed(go); focusSelection(); } : null;
  $('pdCountRow').hidden = !grp;
  $('pdWidthRow').hidden = grp;
  $('pdDup').hidden = !grp;
  if (grp) { $('pdCount').value = it.group.count; $('pdCountR').value = it.group.count; }
  else {
    const s = it.seg;
    $('pdLen').textContent = `${Math.round(Math.hypot(s.a[0] - s.b[0], s.a[1] - s.b[1], s.a[2] - s.b[2]))} units`;
    $('pdWidth').value = s.width * 10; $('pdWidthR').value = s.width * 10;
  }
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
  enterMode('peds');
  const it = pedEdit(() => (kind === 'group' ? peds.addGroup(viewPoint()) : peds.addSegment(viewPoint())),
                     kind === 'group' ? 'add a pedestrian group' : 'add a sidewalk');
  if (it) selectPed(it);
}
// the "Pedestrians in this stage" switch: off takes every group out (Undo brings them back), on the disc's groups
function setPedLive(on) {
  if (!discPeds) return;
  pedEdit(() => peds.setLive(on), on ? 'bring the pedestrians back' : 'take the pedestrians out');
  if (pedSel && pedSel.kind === 'group' && pedSel.group.deleted) pedSel = null;
  showSel();
  msg(on ? `The disc's pedestrian groups are back in ${title(stageName)}` : `No pedestrians in ${title(stageName)}: every group is out`,
      { level: 'ok', toast: on ? true : ['Undo', undo] });
}
function pedFromInputs() {
  if (!pedSel) return;
  const it = pedSel;
  pedEdit(() => {
    if (it.kind === 'group') peds.setCount(it, +$('pdCount').value);
    else peds.setWidth(it, Math.round((+$('pdWidth').value || 10) / 10));   // the field in units, the patch in tens of units
    const want = [+$('pdx').value, +$('pdy').value, +$('pdz').value], now = pedPos(it);
    if (Math.hypot(want[0] - now[0], want[1] - now[1], want[2] - now[2]) > 0.05) {
      it.node.position.add(toThree(want).sub(toThree(now)));
      peds.moved(it);
    }
  }, 'edit ' + pedTitle(it).toLowerCase());
}
// a slider (People, Width): the marker follows while it moves, and the whole drag is one undo step
let sliding = null;
function slideInput(kind) {
  if (!pedSel) return;
  const it = pedSel;
  if (sliding !== it) { snapshot((kind === 'count' ? 'change the people of ' : 'widen ') + pedTitle(it).toLowerCase()); sliding = it; }
  if (kind === 'count') { peds.setCount(it, +$('pdCountR').value); $('pdCount').value = it.group.count; }
  else { peds.setWidth(it, +$('pdWidthR').value / 10); $('pdWidth').value = it.seg.width * 10; }
  $('pdInfo').textContent = peds.info(it);
}
function slideDone() { sliding = null; showPed(); refreshList(); }
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
  if (e.value) dragged = true;
  showHint();
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
  if ((pedSel || editMode === 'peds') && mode === 'rotate') return;   // pedestrian markers only move
  gizmo.setMode(mode);
  gizmo.showX = gizmo.showZ = mode !== 'rotate';
  gizmo.showY = true;
  $('tMove').classList.toggle('on', mode === 'translate');
  $('tTurn').classList.toggle('on', mode === 'rotate');
  $('tMove').setAttribute('aria-pressed', mode === 'translate');
  $('tTurn').setAttribute('aria-pressed', mode === 'rotate');
  showHint();
}
function snapMove() { return Math.max(0.1, +$('snapMove').value || 10); }
function snapTurn() { return Math.max(0.1, +$('snapTurn').value || 15); }
function setSnap() {
  const on = $('snap').checked;
  gizmo.setTranslationSnap(on ? snapMove() : null);
  gizmo.setRotationSnap(on ? THREE.MathUtils.degToRad(snapTurn()) : null);
  store.set('snap', on ? '1' : '0'); store.set('snapMove', snapMove()); store.set('snapTurn', snapTurn());
  setGrid();
}
// F and the view bar's button: look at the selection (an object, or a pedestrian marker)
function focusSelection() {
  if (sel) focus(sel);   // a deleted one too: where it was
  else if (pedSel) focus({ node: pedSel.kind === 'seg' ? pedSel.lane : pedSel.node });
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
  const { made, on } = copyGroup([(p, h) => [[p[0] + step, p[1], p[2]], h]]);
  msg(`Copied ${countText(made)}` + (on ? ` and ${on} ${on === 1 ? 'object' : 'objects'} on top` : ''), { level: 'ok', toast: true });
  selectMany(made);
}
// One copy of an item, placed by `place`: ([x, y, z], heading) -> [[x, y, z], heading] (a new prop for a new prop, else a copy of the object)
function cloneItem(it, place) {
  const [at, h] = place(posOf(it), headingOf(it));
  return it.rec.prop ? makeProp(Object.assign({ id: newPropId(), model: it.rec.prop.model, pos: at, heading: h }, it.rec.extra || {}))
    : makeCopy(it.rec, at, h);
}
// The selection (and, with Move objects on top, what rests on it) copied once for each placer. made: the copies of the selected ones.
function copyGroup(placers) {
  const made = [];
  let on = 0;
  for (const place of placers) {
    for (const it of group) {
      if (!it.node.visible) continue;
      const c = cloneItem(it, place);
      made.push(c);
      if ($('carry').checked) for (const a of it.attached) if (a.node.visible && !group.includes(a)) { c.attached.push(cloneItem(a, place)); on++; }
    }
  }
  return { made, on };
}
// "Hotel", "3 buildings" or "5 objects"
const KIND_WORDS = { destr: ['building', 'buildings'], pickup: ['pickup', 'pickups'], prop: ['prop', 'props'], newprop: ['new prop', 'new props'] };
function countText(list) {
  if (list.length === 1) return nameOf(list[0]);
  const kinds = new Set(list.map((it) => it.rec.info.kind)), w = kinds.size === 1 ? KIND_WORDS[[...kinds][0]] : null;
  return `${list.length} ${w ? w[1] : 'objects'}`;
}

function hideRec(it) {
  if (it.copy) { if (it.rec.copies.includes(it)) removeCopy(it); }
  else if (it.rec.prop) { if (propRecs.includes(it.rec)) removeProp(it.rec); }
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
    if (r.prop) continue;   // a new prop has no disc place to go back to
    for (const c of [...r.copies]) removeCopy(c);
    r.deleted = false;
    r.node.visible = true;
    setWorldMatrix(r.node, r.m0);
  }
  selectMany([...rs].map((r) => r.item));
}

// ------------------------------------------------------------------ Arrange (web/arrange.js): the selection copied or moved by rule
const arrangeItems = () => (isDeleted(sel) ? [] : group.filter((it) => it.node.visible));
function gameBox(it) {   // the item's box in the game's axes, and where it stands (its origin); an object without a mesh counts as its point
  const b = new THREE.Box3().setFromObject(it.node);
  const pos = posOf(it);
  if (b.isEmpty()) return { min: pos, max: pos, pos };
  return { min: [b.min.x, -b.max.z, b.min.y], max: [b.max.x, -b.min.z, b.max.y], pos };
}
const arrangeCarried = (it) => ($('carry').checked ? it.attached.filter((a) => a.node.visible && !group.includes(a)) : []);
function placeItem(it, place) {
  const [p, h] = place(posOf(it), headingOf(it));
  setWorld(it.node, p, h, it.rec);
}
// One copy of the selection for each placer; the copies become the selection. Returns how many objects it made.
function arrangeCopies(placers, what) {
  const src = arrangeItems();
  if (!src.length || !placers.length) return 0;
  const label = countText(src);
  snapshot(`${what} ${whatSel()}`);
  const { made, on } = copyGroup(placers);
  const n = made.length + on;
  msg(`Made ${n} ${n === 1 ? 'copy' : 'copies'} of ${label}`, { level: 'ok', toast: ['Undo', undo] });
  selectMany(made);
  return n;
}
// The selection, and what it carries, moved by one rule
function arrangeMove(place, what) {
  const src = arrangeItems();
  if (!src.length) return 0;
  snapshot(`${what} ${whatSel()}`);
  for (const it of src) { placeItem(it, place); for (const a of arrangeCarried(it)) placeItem(a, place); }
  msg(`Moved ${countText(src)}`, { level: 'ok', toast: ['Undo', undo] });
  showProps();
  refreshList();
  return src.length;
}
// shifts[k]: how far the k-th selected object goes along the axis (0 X, 1 Y, 2 Z)
function arrangeShifts(axis, shifts, what) {
  const src = arrangeItems();
  if (!src.length) return 0;
  if (!shifts.some((s) => Math.abs(s) > 0.05)) { msg('They are already in line', { level: 'info', toast: true }); return 0; }
  snapshot(`${what} ${whatSel()}`);
  src.forEach((it, k) => {
    const place = (p, h) => { const q = p.slice(); q[axis] += shifts[k]; return [q, h]; };
    placeItem(it, place);
    for (const a of arrangeCarried(it)) placeItem(a, place);
  });
  msg(`${what === 'align' ? 'Aligned' : 'Spread'} ${countText(src)}`, { level: 'ok', toast: ['Undo', undo] });
  showProps();
  refreshList();
  return src.length;
}
const arrange = initArrange($('secArrange'), {
  boxes: () => arrangeItems().map(gameBox),
  view: () => viewPoint(),
  carryCount: () => arrangeItems().reduce((n, it) => n + arrangeCarried(it).length, 0),
  copies: arrangeCopies,
  moveAll: arrangeMove,
  shifts: arrangeShifts,
  scrub: (inp) => scrubField(inp),
  msg,
});

// picking: a click that did not drag (Shift/Ctrl: add or take out); Shift + left drag: box select
const ndc = (e) => {
  const r = canvas.getBoundingClientRect();
  return new THREE.Vector2((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1);
};
let down = null, band = null, dragged = false, flyTipShown = false;   // dragged: the gizmo moved something since the button went down
canvas.parentElement.addEventListener('pointerdown', (e) => {   // capture: before OrbitControls sees it
  if (e.target !== canvas) { down = null; return; }   // a floating bar's button
  down = [e.clientX, e.clientY];
  dragged = false;
  if (e.button === 0 && e.shiftKey && gizmo.axis === null && editMode === 'objects') {   // box select: objects only
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
  showHint();
});
canvas.addEventListener('pointerup', (e) => {
  const b = band;
  band = null;
  hover.buttons = 0; hover.dirty = true;
  if (!$('band').hidden) { $('band').hidden = true; showHint(); }
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
  const moved = down ? Math.hypot(e.clientX - down[0], e.clientY - down[1]) : 0;
  // a left drag in the fly view does nothing: once a session, say how to look around
  if (down && e.button === 0 && fly.on && !e.shiftKey && moved > 24 && !gizmo.dragging && !dragged && !flyTipShown) {
    flyTipShown = true;
    toast('In Fly view, hold the right button to look around (or press V for Orbit).', 'info');
  }
  if (!down || moved > 4 || gizmo.dragging || dragged || e.button !== 0) return;
  // the first visible thing under the pointer: an object, a pedestrian marker, a deleted one's dashed box; ground or a road: nothing
  const add = e.shiftKey || e.ctrlKey, h = pickAt(ndc(e), add);
  if (h && h.ped) selectPed(h.ped);
  else if (h && h.deleted) select(h.deleted);
  else if (h && h.item) select(h.item, add ? 'toggle' : 'set');
  else if (!add) select(null);
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
      const ped = pedOfEntry(patch && patch.peds, +m[1]);
      problems.push({ text: `${ped ? peds.label(ped) : 'Pedestrian group ' + (+m[1] + 1)} was not applied: ${m[2]}`, ped });
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
    p.appendChild(lineEl(pr.text, 'err', pr.handle ? { go: () => goTo(pr.handle), goTitle: 'Select it and look at it' }
      : pr.ped ? { go: () => { selectPed(pr.ped); focusSelection(); }, goTitle: 'Select the group and look at it' }
      : pr.prop && propRecs.includes(pr.prop.rec) ? { go: () => { enterMode('objects'); select(pr.prop); focusSelection(); }, goTitle: 'Select it and look at it' }
      : {}));
  }
  for (const w of chk.warns) p.appendChild(lineEl(w, 'warn'));
  for (const i of chk.infos) p.appendChild(lineEl(i, 'info'));
  const det = document.createElement('details');
  det.innerHTML = '<summary>Details: the full output of the check</summary><pre></pre>';
  det.querySelector('pre').textContent = (lines || []).join('\n');
  p.appendChild(det);
  markTab('check', !chk.ok);
}
// the marker of entry k of the patch's "peds".groups: a disc group by its handle, a new one by its place among the new ones
function pedOfEntry(pp, k) {
  const e = pp && Array.isArray(pp.groups) ? pp.groups[k] : null;
  if (!e) return null;
  const gs = peds.items().filter((it) => it.kind === 'group');
  if (e.handle != null) return gs.find((it) => it.group.handle && normHandle(it.group.handle) === normHandle(e.handle)) || null;
  const j = pp.groups.slice(0, k).filter((x) => x && x.handle == null).length;
  return gs.filter((it) => it.group.handle == null)[j] || null;
}
function goTo(handle) {
  const rec = byHandle.get(normHandle(handle));
  if (!rec) return;
  select(rec.item);
  if (rec.item.node.visible) focus(rec.item);
}
// The check as the server gives it (editor.py check_patch): the summary, the problems (an entry the game skips names its object)
function checkFromServer(res, patch) {
  const problems = [], warns = [];
  for (const p of res.problems || []) {
    const rec = p.handle ? byHandle.get(normHandle(p.handle)) : null;
    const ped = p.kind === 'ped' && p.index !== undefined ? pedOfEntry(patch && patch.peds, p.index) : null;
    const prop = p.kind === 'prop' && p.id != null ? propRecs.find((r) => r.prop.id === String(p.id))?.item || null : null;
    const text = p.kind === 'entry' && rec ? `${nameOf(rec.item)} was not applied: ${p.reason}`
      : ped && p.reason ? `${peds.label(ped)} was not applied: ${p.reason}`
      : prop && p.reason ? `${nameOf(prop)} was not applied: ${p.reason}` : p.text;
    if (p.level === 'warn') warns.push(text);
    else problems.push({ text, handle: rec ? rec.info.handle : null, ped, prop });
  }
  const n = problems.length;
  return { ok: !n, n, problems, warns, infos: res.infos || [],
           summary: res.summary || (n ? `Check found ${n} ${n === 1 ? 'problem' : 'problems'}` : 'Check passed. Collision OK.') };
}
// the mod whose file the game uses for this stage: the last by name of those that have one (case aside)
function winnerOf(have, mod) {
  const all = [...new Set(have.concat(mod ? [mod] : []))];
  return all.sort((a, b) => (a.toLowerCase() < b.toLowerCase() ? -1 : a.toLowerCase() > b.toLowerCase() ? 1 : 0)).pop() || '';
}
const winnerText = (w) => `Mod "${w}" also changes ${title(stageName)} and loads after "${modNow}", so the game uses ${w}'s file. ` +
  'Mods load in alphabetical order; the last one wins.';
let winHidden = false;   // closed by hand: until the next stage load or mod switch
function showWinner() {
  const w = modNow ? winnerOf(stageHave, modNow) : '';
  const on = !!w && w.toLowerCase() !== modNow.toLowerCase();
  $('winNote').hidden = !on || winHidden;
  $('winText').textContent = on ? winnerText(w) : '';
  return on ? w : '';
}
const FAILED = { check: 'Check failed', save: "Couldn't save", test: "Couldn't start the game", reload: "Couldn't reload the game" };
// Check, Save, Test in game or Reload in game; true when the server did it. Saving with no mod chosen asks which one first.
async function post(kind, save = true, extra = {}) {
  const writes = kind === 'save' || kind === 'reload' || (kind === 'test' && save);
  if (writes && !modNow) {
    const m = await pickMod('save', { save: 'Save', test: 'Save and test', reload: 'Save and reload' }[kind]);
    if (!m) return false;
    moveTo(m, false);
  }
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
    if (kind === 'check' || res.problems || (res.lines && res.lines.length)) {
      // the server's summary and problems; an older reply (lines only) read here
      chk = res.problems ? checkFromServer(res, body.patch) : readCheck(res, body.patch, mod);
      ui.check = { state: !same ? 'stale' : chk.ok ? 'ok' : 'bad', n: chk.n };
      showCheck(chk, res.lines);
      msg(chk.summary, { level: chk.ok ? 'ok' : 'err', open: 'check', toast: kind === 'check' && chk.ok ? true : undefined,
                         go: () => openOutput('check'), goTitle: 'Show the check' });
      if (!res.problems) for (const w of chk.warns) msg(w, { level: 'warn', toast: true });
    }
    if (res.have) stageHave = res.have;
    if (res.winner !== undefined) {   // another mod's file for this stage loads after this one: the note says so (a toast when new)
      const was = !$('winNote').hidden;
      winHidden = false;
      const w = showWinner();
      if (w && !was) msg(winnerText(w), { level: 'warn', toast: true });
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
    return true;
  } catch (e) {
    game.pending = false;
    if (kind === 'test' || kind === 'reload') setGameState(wasRunning ? 'running' : 'closed');
    if (kind === 'test' && e.code === 'running') { game.stage = e.data.stage || game.stage; setRunning(true); askRunning(save); return false; }
    const er = cleanError(e);
    const where = /wotm\.exe/.test(er.text) ? 'game' : /disc image/.test(er.text) ? 'iso' : '';
    const action = e.code === 'mod' ? ['Choose mod', () => $('mod').focus()]
      : e.code === 'settings' || /Settings/.test(er.text) ? ['Open settings', () => openSettings(where)] : undefined;
    msg(`${FAILED[kind] || kind}: ${er.text}`, { level: 'err', detail: er.detail, toast: action });
    return false;
  }
}

// a question: a title, a text and buttons [value, label, primary]; o.icon (an MDI name, amber), o.note (an amber line under the
// text). then(value) runs once: on a button's click, or when the dialog closes otherwise (Esc, the close button: 'cancel').
function ask(head, text, buttons, then, o = {}) {
  const dlg = $('askDlg');
  if (dlg.open) dlg.close();
  $('askTitle').textContent = head;
  $('askText').textContent = text;
  $('askIcon').hidden = !o.icon;
  if (o.icon) setIc($('askIcon'), o.icon);
  $('askNote').hidden = !o.note;
  $('askNote').querySelector('span').textContent = o.note || '';
  $('askBtns').textContent = '';
  let done = false, main = null;
  // (a close event left over from the dialog's last use finds it open again: only a real close counts)
  const onClose = () => { if (!dlg.open) finish(dlg.returnValue || 'cancel'); };
  const finish = (v) => {
    if (done) return;
    done = true;
    dlg.removeEventListener('close', onClose);
    if (dlg.open) dlg.close(v);
    then(v);
  };
  for (const [value, label, primary] of buttons) {
    const b = document.createElement('button');
    b.type = 'button'; b.value = value; b.textContent = label;
    if (primary) { b.className = 'primary'; main = b; }
    b.onclick = () => finish(value);
    $('askBtns').appendChild(b);
  }
  dlg.returnValue = '';
  dlg.addEventListener('close', onClose);
  dlg.showModal();
  (main || $('askBtns').lastElementChild)?.focus();
}
const changesText = (n) => `${n} unsaved ${n === 1 ? 'change' : 'changes'}`;
// How many things differ from the saved file: entries by handle, the instance names, the pedestrians, the rest of the file
function unsavedCount() {
  let a = {};
  try { a = JSON.parse(savedJson || '{}') || {}; } catch (e) { /* nothing saved */ }
  const b = buildPatch();
  const ents = (p) => new Map((Array.isArray(p.objects) ? p.objects : []).map((e) => [normHandle(e && e.handle), JSON.stringify(e)]));
  const A = ents(a), B = ents(b);
  let n = 0;
  for (const [h, j] of B) if (A.get(h) !== j) n++;
  for (const h of A.keys()) if (!B.has(h)) n++;
  const rest = (p) => JSON.stringify(Object.assign({}, p, { objects: undefined }));
  if (rest(a) !== rest(b)) n++;
  return Math.max(n, dirty ? 1 : 0);
}
// unsaved changes in the way: Cancel, Discard, or save first (then gets 'cancel', 'discard' or 'save')
function askUnsaved(text, saveLabel, then) {
  ask('Unsaved changes', text, [['cancel', 'Cancel'], ['discard', 'Discard'], ['save', saveLabel, true]], then,
      { icon: 'content-save-alert-outline' });
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
  hudGameChanged();
  texGameChanged();
  lightGameChanged();
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
  const to = e.target.value;
  e.target.blur();
  if (!dirty) { loadStage(to); return; }
  e.target.value = stageName;      // until the answer
  askUnsaved(`${title(stageName)} has ${changesText(unsavedCount())}.`, 'Save and switch', async (v) => {
    if (v === 'cancel' || (v === 'save' && !(await post('save')))) return;
    $('stage').value = to;
    loadStage(to);
  });
};
// ------------------------------------------------------------------ the mod: the picker, New mod, switching with unsaved changes
// the game skips a mod folder whose name starts with _ or . (and Windows refuses these characters)
const badModName = (m) => /[\\/:*?"<>|]/.test(m) || /^[_.]/.test(m);
const NEW_MOD = '__new__';       // the picker's "New mod…" (a name the editor refuses, so no mod has it)
let modList = [], stageHave = [];   // the mods folder's mods; the mods that have a file for this stage (/api/stage)
const sameName = (a, b) => a.toLowerCase() === b.toLowerCase();
const hasStage = (m) => stageHave.some((x) => sameName(x, m));
const knownMod = (m) => modList.find((x) => sameName(x, m)) || m;   // an existing mod's own spelling
function fillMods() {
  const pick = $('mod'), names = [...new Set(modList.concat(modNow ? [modNow] : []))];
  names.sort((a, b) => a.toLowerCase().localeCompare(b.toLowerCase()));
  pick.textContent = '';
  const opt = (v, t, disabled) => { const o = document.createElement('option'); o.value = v; o.textContent = t; o.disabled = !!disabled; pick.appendChild(o); };
  if (!modNow) opt('', 'Choose a mod', true);
  for (const m of names) opt(m, m);
  opt(NEW_MOD, 'New mod…');
  pick.value = modNow;
  const f = pick.closest('.field');
  f.classList.toggle('none', !modNow);
  f.title = modNow ? `Your changes are saved into mods/${modNow}/levels/` : 'Choose the mod your changes are saved into, or make a new one';
}
// the mod in use from now on (the config keeps it); the scene is not loaded again here
function setMod(v) {
  modNow = v;
  cfg.mod = v;
  winHidden = false;
  loadSpt();      // each mod keeps its own start points file
  fillMods();
  showWinner();
  api('/api/config', { mod: v }).catch((err) => msg(`Couldn't keep the mod in the settings: ${cleanError(err).text}`, { level: 'err' }));
}
// the scene's changes go into mod v without loading its file: unsaved until Save writes them there
function moveTo(v, say = true) {
  const p = JSON.stringify(buildPatch());
  setMod(v);
  // the new props draw from the new mod's models; an entry whose model it lacks stays in the file, unseen
  loadPropModels(v).then(() => {
    if (!propRecs.length && !keep.props.length) return;
    restoreTo(p);
    const n = keep.props.length;
    if (n) msg(`${n} new ${n === 1 ? 'prop uses a model' : 'props use models'} that mod "${v}" does not have: the game skips ${n === 1 ? 'it' : 'them'}`, { level: 'warn' });
  });
  savedJson = '';
  dirty = true;
  showUndo();
  refreshList();
  if (say) msg(`Your changes now go into mod "${v}". Save writes mods/${v}/levels/${stageName}.json.`, { level: 'ok', toast: true });
}
// resolves to the mod in use once the switch is decided (the Fight HUD dialog waits for it)
function switchMod(v) {
  return new Promise((done) => {
    v = knownMod((v || '').trim());
    if (!v || v === modNow) { fillMods(); done(modNow); return; }
    if (badModName(v)) {
      msg('A mod name can\'t contain \\ / : * ? " < > | or start with _ or .', { level: 'err', toast: true });
      fillMods();
      done(modNow);
      return;
    }
    if (!dirty) { setMod(v); loadStage(stageName); done(modNow); return; }
    const n = unsavedCount();
    ask(`Switch to mod "${v}"?`,
        modNow ? `You have ${changesText(n)} for ${modNow}.` : `You have ${changesText(n)}, not saved in any mod yet.`,
        [['cancel', 'Cancel'], ['discard', `Discard and load ${v}`], ['move', `Move my changes to ${v}`, true]],
        (a) => {
          if (a === 'discard') { setMod(v); loadStage(stageName); }
          else if (a === 'move') moveTo(v);
          else fillMods();
          done(modNow);
        },
        { icon: 'content-save-alert-outline', note: hasStage(v) ? `${v} already changes this stage; saving will replace its file.` : '' });
  });
}
// a mod picked somewhere else than the top bar (the Fight HUD dialog): a mod's name, or "New mod…"
const chooseMod = (v) => (v === NEW_MOD ? pickMod('new').then((m) => (m ? switchMod(m) : modNow)) : switchMod(v));
$('mod').onchange = (e) => {
  const v = e.target.value;
  e.target.blur();
  fillMods();                      // the picker shows the mod in use until the switch is decided
  if (v === NEW_MOD) pickMod('new').then((m) => { if (m) switchMod(m); });
  else switchMod(v);
};
// mode 'new': a name for a new mod. 'save': the mod to save into, when Save, Test or Reload find none chosen (one of the mods, or a
// new one). Resolves to the name, or '' when cancelled.
function pickMod(mode = 'new', okLabel = 'Create') {
  return new Promise((resolve) => {
    const dlg = $('modDlg'), pick = $('mdPick'), name = $('mdName'), save = mode === 'save';
    if (dlg.open) dlg.close();
    $('mdTitle').textContent = save ? 'Save into which mod?' : 'New mod';
    $('mdText').textContent = save ? 'Your changes are saved in a mod: a folder in the mods folder that the game loads.' : '';
    $('mdText').hidden = !save;
    pick.textContent = '';
    for (const m of modList.concat([NEW_MOD])) {
      const o = document.createElement('option');
      o.value = m; o.textContent = m === NEW_MOD ? 'New mod…' : m;
      pick.appendChild(o);
    }
    $('mdPickRow').hidden = !save || !modList.length;
    pick.value = $('mdPickRow').hidden ? NEW_MOD : modList[0];
    name.value = '';
    $('mdOk').textContent = save ? okLabel : 'Create';
    const isNew = () => $('mdPickRow').hidden || pick.value === NEW_MOD;
    const value = () => (isNew() ? name.value.trim() : pick.value);
    const show = () => {
      const nw = isNew(), v = value(), bad = nw && !!v && badModName(v), had = nw && v && modList.find((m) => sameName(m, v));
      $('mdNameRow').hidden = !nw;
      $('mdErr').hidden = !bad;
      $('mdHelp').hidden = !nw;
      $('mdHelp').textContent = had ? `Mod "${had}" exists: your changes go into it.`
        : `The folder mods/${v && !bad ? v : '<name>'}/ is created when you save.`;
      const replaces = !!v && !bad && hasStage(v);
      $('mdWarn').hidden = !replaces;
      $('mdWarnText').textContent = replaces ? `${knownMod(v)} already changes this stage; saving will replace its file.` : '';
      $('mdOk').disabled = !v || bad;
    };
    pick.onchange = () => { show(); if (pick.value === NEW_MOD) name.focus(); };
    name.oninput = show;
    show();
    let done = false;
    const onClose = () => { if (!dlg.open) finish(''); };
    const finish = (v) => {
      if (done) return;
      done = true;
      dlg.removeEventListener('close', onClose);
      if (dlg.open) dlg.close();
      resolve(v ? knownMod(v) : '');
    };
    // Create (Enter in the name clicks it too): only a name the game takes
    $('mdOk').onclick = (e) => { e.preventDefault(); const v = value(); if (v && !badModName(v)) finish(v); };
    dlg.addEventListener('close', onClose);
    dlg.returnValue = '';
    dlg.showModal();
    (isNew() ? name : pick).focus();
  });
}
const showFilterClear = () => { $('filterClear').hidden = !$('filter').value; };
$('filter').oninput = () => { showFilterClear(); refreshList(); };
$('filterClear').onclick = () => { $('filter').value = ''; showFilterClear(); refreshList(); $('filter').focus(); };
$('clearFilters').onclick = () => {
  $('filter').value = '';
  document.querySelectorAll('.kinds input[data-kind]').forEach((x) => (x.checked = true));
  $('changedOnly').checked = false;
  showFilterClear();
  refreshList();
};
document.querySelectorAll('.kinds input').forEach((x) => (x.onchange = refreshList));
$('modeObjects').onclick = () => setMode('objects');
$('modePeds').onclick = () => setMode('peds');
// Name and Details open or closed as last time
for (const id of ['secName', 'secDetails']) {
  $(id).open = store.get('fold.' + id, '0') === '1';
  $(id).addEventListener('toggle', () => store.set('fold.' + id, $(id).open ? '1' : '0'));
}
$('pHandle').onclick = async () => {
  if (!sel) return;
  const h = sel.rec.info.handle, ok = await copyText(h);
  toast(ok ? `Copied the handle ${h}` : 'The browser did not let the page copy', ok ? 'ok' : 'warn');
};
$('carry').onchange = showActions;
// turn by 90 degrees about the vertical (seen from above, counterclockwise is +, as the heading field counts)
function turnBy(d) {
  if (!sel || isDeleted(sel)) return;
  snapshot('turn ' + whatSel());
  const start = startMove();
  setWorld(sel.node, posOf(sel), wrapDeg(headingOf(sel) + d), sel.rec);
  carry(start);
  showProps();
  refreshList();
}
$('rotL').onclick = () => turnBy(90);
$('rotR').onclick = () => turnBy(-90);
// dragging an axis letter changes that coordinate: one step (the snap's, else 1 unit; Shift: ten) per 3 px, one undo step
function scrub(letter, axis) {
  let used = false;
  letter.addEventListener('pointerdown', (e) => {
    if (e.button !== 0 || !sel || isDeleted(sel)) return;
    e.preventDefault();
    try { letter.setPointerCapture(e.pointerId); } catch (err) { /* a synthetic pointer */ }
    const x0 = e.clientX, p0 = posOf(sel), h0 = headingOf(sel);
    let start = null;
    used = false;
    const move = (ev) => {
      const dx = ev.clientX - x0;
      if (!start) { if (Math.abs(dx) < 3) return; snapshot('move ' + whatSel()); start = startMove(); used = true; }
      const step = ($('snap').checked ? snapMove() : 1) * (ev.shiftKey ? 10 : 1);
      const p = p0.slice();
      p[axis] = p0[axis] + Math.round(dx / 3) * step;
      setWorld(sel.node, p, h0, sel.rec);
      carry(start);
      showProps();
    };
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointercancel', up);
      if (start) refreshList();
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', up);
  });
  letter.addEventListener('click', (e) => { if (used) { e.preventDefault(); used = false; } });   // no focus on the field after a drag
}
document.querySelectorAll('#props .xyz .pre.scrub').forEach((l, n) => scrub(l, n));
// Dragging a number field sideways changes it: one step per 3 px (Shift: ten), within its min and max; a click without dragging
// types in it. o.step(): the step (default the field's own); o.begin/o.live/o.end: hooks around a drag (one undo step for the whole
// drag), o.live called at each new value. Without o.live the field gets one 'change' on release (a costly change, like Height).
function scrubField(inp, o = {}) {
  inp.classList.add('scrubbable');
  inp.addEventListener('pointerdown', (e) => {
    if (e.button !== 0 || inp.disabled || document.activeElement === inp) return;   // typing in it: the pointer selects text
    e.preventDefault();
    const other = document.activeElement;
    if (other && other !== document.body && other.blur) other.blur();   // commit what was typed elsewhere
    try { inp.setPointerCapture(e.pointerId); } catch (err) { /* a synthetic pointer */ }
    const x0 = e.clientX, v0 = +inp.value || 0, step0 = (o.step ? o.step() : +inp.step) || 1;
    const lo = inp.min !== '' ? +inp.min : -Infinity, hi = inp.max !== '' ? +inp.max : Infinity;
    let moved = false;
    const move = (ev) => {
      const dx = ev.clientX - x0;
      if (!moved) {
        if (Math.abs(dx) < 3) return;
        moved = true;
        document.body.classList.add('scrubbing');
        if (o.begin) o.begin();
      }
      const v = Math.round(Math.min(hi, Math.max(lo, v0 + Math.round(dx / 3) * step0 * (ev.shiftKey ? 10 : 1))) * 1000) / 1000;
      if (String(v) === inp.value) return;
      inp.value = String(v);
      inp.dispatchEvent(new Event('input', { bubbles: true }));
      if (o.live) o.live();
    };
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointercancel', up);
      document.body.classList.remove('scrubbing');
      if (!moved) { inp.focus(); inp.select(); return; }
      if (o.end) o.end();
      else if (!o.live && String(v0) !== inp.value) inp.dispatchEvent(new Event('change', { bubbles: true }));
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', up);
  });
}
// the selection's X, Y, Z and heading: the object follows the drag
{
  let start = null;
  const live = () => {
    if (!start || !sel || isDeleted(sel)) return;
    setWorld(sel.node, [+$('px').value, +$('py').value, +$('pz').value], +$('ph').value, sel.rec);
    carry(start);
  };
  const hooks = (turn) => ({
    step: () => ($('snap').checked ? (turn ? snapTurn() : snapMove()) : 1),
    begin: () => { if (sel && !isDeleted(sel)) { snapshot('edit ' + whatSel()); start = startMove(); } },
    live,
    end: () => { if (start) { start = null; showProps(); refreshList(); } },
  });
  for (const id of ['px', 'py', 'pz']) scrubField($(id), hooks(false));
  scrubField($('ph'), hooks(true));
}
// a pedestrian group's or segment's fields: People and Width through their sliders, X, Y, Z moving the marker
scrubField($('pdCount'), { live: () => { $('pdCountR').value = $('pdCount').value; slideInput('count'); }, end: slideDone });
scrubField($('pdWidth'), { live: () => { $('pdWidthR').value = $('pdWidth').value; slideInput('width'); }, end: slideDone });
{
  let it = null;
  const live = () => {
    if (!it || pedSel !== it) return;
    const want = [+$('pdx').value, +$('pdy').value, +$('pdz').value], now = pedPos(it);
    it.node.position.add(toThree(want).sub(toThree(now)));
    try { peds.moved(it); } catch (e) { msg(`Pedestrians: ${e.message}`, { level: 'warn', toast: true }); }
  };
  for (const id of ['pdx', 'pdy', 'pdz']) {
    scrubField($(id), { begin: () => { it = pedSel; if (it) snapshot('move ' + pedTitle(it).toLowerCase()); }, live,
                        end: () => { it = null; showPed(); showPedCounts(); refreshList(); } });
  }
}
for (const id of ['snapMove', 'snapTurn', 'ipHeight']) scrubField($(id));
$('undo').onclick = undo;
$('redo').onclick = redo;
$('frame').onclick = frameAll;
$('stop').onclick = stopGame;
$('restore').onclick = resetSel;
$('keepClose').onclick = () => { $('keepNote').hidden = true; };
$('winClose').onclick = () => { winHidden = true; $('winNote').hidden = true; };
$('check').onclick = () => post('check');
$('save').onclick = () => post('save');
$('test').onclick = () => {
  if (!dirty) { post('test', false); return; }
  const n = unsavedCount();
  ask('Save before testing?',
      `The game loads the saved file. Your last ${n === 1 ? 'change is' : `${n} changes are`} not saved yet.`,
      [['cancel', 'Cancel'], ['nosave', 'Test without them'], ['save', 'Save and test', true]],
      (v) => { if (v === 'save' || v === 'nosave') post('test', v === 'save'); },
      { icon: 'content-save-alert-outline' });
};
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
setGrid(store.get('grid', '0') === '1');
$('grid').onchange = (e) => { setGrid(e.target.checked); store.set('grid', e.target.checked ? '1' : '0'); };
$('look').onchange = (e) => setLook(e.target.checked);
$('pedsOn').onchange = (e) => setPeds(e.target.checked);
$('collOn').onchange = (e) => setColl(e.target.checked);
coll.setVisible(collOn);
showColl();
// the view bar: fly or orbit, focus, the layers menu, the shortcuts
$('vFly').onclick = () => { if (!fly.on) setView(true); };
$('vOrbit').onclick = () => { if (fly.on) setView(false); };
$('focusSel').onclick = focusSelection;
function showLayers(open) {
  $('layersMenu').hidden = !open;
  $('layersBtn').classList.toggle('on', open);
  $('layersBtn').setAttribute('aria-expanded', open);
}
$('layersBtn').onclick = () => showLayers($('layersMenu').hidden);
document.addEventListener('pointerdown', (e) => {   // a click anywhere else closes the layers menu
  if (!$('layersMenu').hidden && !e.target.closest('#layersMenu, #layersBtn')) showLayers(false);
}, true);
function showKeys() {
  if (document.querySelector('dialog[open]')) return;
  showLayers(false);
  $('keysDlg').showModal();
}
$('keysBtn').onclick = showKeys;
$('help').onclick = showKeys;
$('pedAddGroup').onclick = () => addPed('group');
$('pedAddSeg').onclick = () => addPed('seg');
for (const id of ['pdCount', 'pdWidth', 'pdx', 'pdy', 'pdz']) $(id).onchange = pedFromInputs;
$('pdCountR').oninput = () => slideInput('count');
$('pdWidthR').oninput = () => slideInput('width');
$('pdCountR').onchange = $('pdWidthR').onchange = slideDone;
$('pedLive').onchange = (e) => setPedLive(e.target.checked);
$('pdDrop').onclick = dropToGround;
$('pdDup').onclick = duplicate;
$('pdDel').onclick = remove;

// Add prop: the mod's models made for this stage, and Import .glb (lvlprop.py on the server; the model lands in mods/<mod>/props/)
function showPropMenu(open = $('propMenu').hidden) {
  $('propMenu').hidden = !open;
  if (open) {   // under the button
    const r = $("addProp").getBoundingClientRect(), pr = $("propMenu").offsetParent?.getBoundingClientRect();
    if (pr) $("propMenu").style.left = `${Math.max(8, Math.round(r.left - pr.left))}px`;
  }
  $('addProp').classList.toggle('on', open);
  $('addProp').setAttribute('aria-expanded', open);
  if (!open) return;
  showLayers(false);
  $('pmMod').textContent = modNow ? `mod "${modNow}"` : 'no mod';
  const list = $('pmList'), all = [...propModels.values()], here = all.filter((pm) => pm.info.stage === baseOf(stageName));
  list.textContent = '';
  for (const pm of here) {
    const p = pm.info, b = document.createElement('button');
    b.type = 'button';
    b.className = 'ghost pm-row';
    b.setAttribute('role', 'menuitem');
    b.innerHTML = '<span class="pm-name"></span><span class="pm-meta"></span>';
    b.querySelector('.pm-name').textContent = p.name && p.name.toUpperCase() !== p.model ? `${p.name} · ${p.model}` : p.model;
    const dims = p.bounds ? p.bounds.max.map((x, k) => x - p.bounds.min[k]) : null;
    b.querySelector('.pm-meta').textContent = p.error ? `Can't be placed: ${p.error}`
      : `${dims ? dimsText(dims) + ' units · ' : ''}${p.triangles} triangles`;
    b.disabled = !pm.scene;
    b.title = pm.scene ? `Add ${p.model} where the view looks` : `mods/${modNow}/props/${p.model}: ${p.error || 'no preview'}`;
    b.onclick = () => addProp(p.model);
    list.appendChild(b);
  }
  const other = all.length - here.length;
  const none = !modNow ? 'Choose a mod first: the props live in its props folder.'
    : !here.length ? `No props for ${title(stageName)} yet.` + (other ? ` ${other} of this mod ${other === 1 ? 'is' : 'are'} for other stages.` : '') : '';
  $('pmNone').textContent = none;
  $('pmNone').hidden = !none;
  for (const [id, cur] of [['pmSky', skyModel], ['pmClouds', cloudsModel]]) {     // the sky dome and the cloud layer: one of the models, or the stage's own
    const sky = $(id);
    sky.textContent = '';
    sky.append(new Option('The stage\'s own', ''));
    for (const pm of here) if (!pm.info.error) sky.append(new Option(pm.info.model, pm.info.model));
    if (cur && ![...sky.options].some((o) => o.value === cur)) sky.append(new Option(`${cur} (not in this mod)`, cur));
    sky.value = cur;
  }
  (list.querySelector('button:not(:disabled)') || $('pmImport')).focus();
}
// kind: 'dome' or 'clouds'
function pickSky(kind, v) {
  const dome = kind === 'dome', cur = dome ? skyModel : cloudsModel, what = dome ? 'sky dome' : 'cloud layer';
  if (v === cur) return;
  snapshot(v ? `${what} ${v}` : `stock ${what}`);
  if (dome) skyModel = v; else cloudsModel = v;
  updateSkyView();
  msg(v ? `The ${what} is now ${v}. The view shows it with Game look (L); give it faces turned inward and a texture.` : `The ${what} is the stage's own again.`,
    { level: 'ok', toast: ['Undo', undo] });
}
$('pmSky').onchange = (e) => pickSky('dome', e.target.value);
$('pmClouds').onchange = (e) => pickSky('clouds', e.target.value);
$('addProp').onclick = () => showPropMenu();
document.addEventListener('pointerdown', (e) => {   // a click anywhere else closes it
  if (!$('propMenu').hidden && !e.target.closest('#propMenu, #addProp')) showPropMenu(false);
}, true);
let ipData = null, ipTitle = '';
const ipInput = Object.assign(document.createElement('input'), { type: 'file', accept: '.glb,model/gltf-binary' });
// The patch's "clear": the meshes the server tagged bare (the stage's scenery with no handle) leave the view
function updateClearView() {
  const off = keep.top.clear === 'scenery';
  if (stageRoot) stageRoot.traverse((o) => { if (o.isMesh && o.userData && o.userData.bare) o.visible = !off; });
}
// Delete every original object (the dialog's button): one Undo step
$('gdDelAll').onclick = () => {
  const todo = recs.filter((r) => !r.deleted);
  if (!todo.length) { gdErr('Every object is already deleted.'); return; }
  snapshot('delete every object');
  for (const r of todo) { r.deleted = true; r.item.node.visible = false; }
  refreshList();
  msg(`Deleted ${todo.length} objects. Save when the stage is as you want it`, { level: 'ok', toast: ['Undo', undo] });
};
// The collision grid's rectangle on the ground: the disc's in grey, the patch's bigger one in blue
let gridView = null;
function updateGridView() {
  if (gridView) { scene.remove(gridView); gridView.traverse((o) => { o.geometry?.dispose(); o.material?.dispose(); }); gridView = null; }
  if (!stageGrid) return;
  const rect = (lo, hi, color) => {
    const pts = [[lo[0], lo[1]], [hi[0], lo[1]], [hi[0], hi[1]], [lo[0], hi[1]]].map((q) => toThree([q[0], q[1], 1]));
    const l = new THREE.LineLoop(new THREE.BufferGeometry().setFromPoints(pts), new THREE.LineBasicMaterial({ color, depthTest: false, transparent: true, opacity: 0.8 }));
    l.renderOrder = 999;
    l.raycast = () => {};
    return l;
  };
  gridView = new THREE.Group();
  gridView.add(rect(stageGrid.min, stageGrid.max, 0x8a8f98));
  const g = gridNow();
  if (g) gridView.add(rect(g.min, g.max, 0x4aa3ff));
  scene.add(gridView);
}

// Stage size: the patch's "grid" (a collision grid rectangle that holds the disc's). It lives in keep.top, so Undo and Save keep it.
function gridNow() { return keep.top.grid && Array.isArray(keep.top.grid.min) && Array.isArray(keep.top.grid.max) ? keep.top.grid : null; }
function gdFill(g) {
  const lo = g ? g.min : stageGrid.min, hi = g ? g.max : stageGrid.max;
  [$('gdMinX').value, $('gdMinY').value] = lo.map((v) => Math.round(v));
  [$('gdMaxX').value, $('gdMaxY').value] = hi.map((v) => Math.round(v));
  gdInfo();
}
function gdVals() { return [[+$('gdMinX').value, +$('gdMinY').value], [+$('gdMaxX').value, +$('gdMaxY').value]]; }
function gdInfo() {
  const [lo, hi] = gdVals(), c = stageGrid.cell;
  const cols = Math.floor((hi[0] - lo[0]) / c) + 1, rows = Math.floor((hi[1] - lo[1]) / c) + 1;
  $('gdInfo').textContent = `${cols} × ${rows} cells of ${c} units (${cols * rows} of 65536 allowed).`;
}
$('gridBtn').onclick = () => {
  if (!stageGrid) { msg('This stage has no collision grid to resize', { level: 'warn', toast: true }); return; }
  const g = stageGrid;
  $('gdText').textContent = `The disc's grid is x ${Math.round(g.min[0])} to ${Math.round(g.max[0])}, y ${Math.round(g.min[1])} to ${Math.round(g.max[1])} (${g.cols} × ${g.rows} cells). ` +
    'The game only collides, shoots and sees across what is inside it, so a prop or a copy placed outside needs a bigger one. It can only grow.';
  gdFill(gridNow());
  $('gdWallsRow').hidden = !stageWalls;
  $('gdWallsTxt').textContent = `Turn off the ${stageWalls} invisible boundary wall${stageWalls === 1 ? '' : 's'}`;
  $('gdWalls').checked = keep.top.walls === 'off';
  $('gdClear').checked = keep.top.clear === 'scenery';
  $('gdErr').hidden = true;
  $('gridDlg').showModal();
};
for (const id of ['gdMinX', 'gdMinY', 'gdMaxX', 'gdMaxY']) $(id).oninput = () => { gdInfo(); $('gdErr').hidden = true; };
$('gdGrow').onclick = () => { const [lo, hi] = gdVals(); gdFill({ min: lo.map((v) => v - 1000), max: hi.map((v) => v + 1000) }); };
$('gdReset').onclick = () => gdFill(null);
const gdErr = (t) => { $('gdErr').hidden = false; $('gdErr').querySelector('span').textContent = t; };
// Apply the dialog's numbers and the walls box to the patch. False (with the dialog's own error showing) when they are not good.
function gdApply() {
  const [lo, hi] = gdVals(), g = stageGrid, err = gdErr;
  if (![...lo, ...hi].every(Number.isFinite)) { err('Type the four numbers.'); return false; }
  if (lo[0] > g.min[0] || lo[1] > g.min[1] || hi[0] < g.max[0] || hi[1] < g.max[1]) { err("The rectangle must hold the disc's grid (it can only grow)."); return false; }
  const cols = Math.floor((hi[0] - lo[0]) / g.cell) + 1, rows = Math.floor((hi[1] - lo[1]) / g.cell) + 1;
  if (cols * rows > 65536) { err(`${cols} × ${rows} cells is over the 65536 the game allows.`); return false; }
  const same = lo[0] === Math.round(g.min[0]) && lo[1] === Math.round(g.min[1]) && hi[0] === Math.round(g.max[0]) && hi[1] === Math.round(g.max[1]);
  snapshot(same ? "stage size back to the disc's" : 'stage size');
  if (same) delete keep.top.grid; else keep.top.grid = { min: lo, max: hi };
  if (stageWalls && $('gdWalls').checked) keep.top.walls = 'off'; else if (keep.top.walls === 'off') delete keep.top.walls;
  if ($('gdClear').checked) keep.top.clear = 'scenery'; else if (keep.top.clear === 'scenery') delete keep.top.clear;
  $('gridDlg').close();
  updateGridView();
  updateClearView();
  refreshList();
  msg(same ? "The stage size is the disc's again" : `Stage size ${cols} × ${rows} cells. Check it, then Save`, { level: 'ok', toast: true });
  return true;
}
$('gdOk').onclick = gdApply;
// ------------------------------------------------------------------ start points (.SPT) and power-ups (.PUP)
// Two point files of the stage, edited in one dialog (the kind picks the columns) and kept in the mod, not in the patch: Save writes
// mods/<mod>/PTS/<STAGE>.<ext> (or the custom stage's folder). SPT: where the monsters appear, four lists (tools/lvl.py SPT_LISTS);
// PUP: the health, stamina and other pickups (PUP_TYPES), each at its rest height. editor.py stage_spt/save_spt, kind SPT | PUP.
const PT_COLORS = { SPT: [0xFFC233, 0x4AE08A, 0xF0605A, 0x36C5F0], PUP: [0xF0605A, 0x4AE08A, 0x36C5F0] };
const PT_WORDS = { SPT: ['start point', 'start points'], PUP: ['power-up', 'power-ups'] };
const ptEmpty = (kind) => ({ points: [], caps: kind === 'SPT' ? [1, 2, 20, 20] : [100], lists: [], source: 'disc' });
const pt = { SPT: ptEmpty('SPT'), PUP: ptEmpty('PUP') };     // what the files hold (edited in place by the dialog)
const ptSaved = { SPT: '[]', PUP: '[]' };
const ptViews = { SPT: null, PUP: null };
const ptLoadId = { SPT: 0, PUP: 0 };
let ptKind = 'SPT', ptSel = -1;
const ptKeep = (kind) => JSON.stringify(pt[kind].points);
// the color of a point: SPT by list, PUP by what it gives (health red, stamina green, the rest blue)
const ptColor = (kind, p) => PT_COLORS[kind][kind === 'SPT' ? p.list : p.type < 3 || p.type === 6 ? 0 : p.type < 6 || p.type === 7 ? 1 : 2] ?? 0xAAAAAA;
async function loadPoints(kind) {
  if (!stageName) return;
  const id = ++ptLoadId[kind];
  try {
    const r = await api(`/api/spt?name=${encodeURIComponent(stageName)}&mod=${encodeURIComponent(modNow || '')}&kind=${kind}`);
    if (id !== ptLoadId[kind]) return;
    pt[kind] = r;
    ptSaved[kind] = ptKeep(kind);
    if (kind === ptKind) ptSel = -1;
  } catch (e) { if (id === ptLoadId[kind]) pt[kind] = ptEmpty(kind); }
  updatePtView(kind);
}
const loadSpt = () => { loadPoints('SPT'); loadPoints('PUP'); loadAip(); };
// A cone for each point (colored; white when selected); a start point has a line for the way the monster faces, a power-up is a ball on its rest height
function updatePtView(kind) {
  const old = ptViews[kind];
  if (old) { scene.remove(old); old.traverse((o) => { o.geometry?.dispose(); o.material?.dispose(); }); ptViews[kind] = null; }
  if (!pt[kind].points.length) return;
  const g = new THREE.Group();
  pt[kind].points.forEach((p, k) => {
    const col = kind === ptKind && k === ptSel ? 0xFFFFFF : ptColor(kind, p);
    const mat = new THREE.MeshBasicMaterial({ color: col, depthTest: false, transparent: true, opacity: 0.9 });
    const base = toThree(p.pos);
    const mesh = new THREE.Mesh(kind === 'SPT' ? new THREE.ConeGeometry(14, 50, 14) : new THREE.SphereGeometry(10, 12, 8), mat);
    mesh.position.copy(base).add(new THREE.Vector3(0, kind === 'SPT' ? 25 : 14, 0));
    mesh.renderOrder = 998;
    mesh.raycast = () => {};
    g.add(mesh);
    if (kind === 'SPT') {
      const h = (p.heading * Math.PI) / 180, fwd = toThree([p.pos[0] - Math.sin(h) * 60, p.pos[1] + Math.cos(h) * 60, p.pos[2] + 25]);
      const line = new THREE.Line(new THREE.BufferGeometry().setFromPoints([base.clone().add(new THREE.Vector3(0, 25, 0)), fwd]),
                                  new THREE.LineBasicMaterial({ color: col, depthTest: false }));
      line.renderOrder = 998;
      line.raycast = () => {};
      g.add(line);
    }
  });
  ptViews[kind] = g;
  scene.add(g);
}
function ptRows() {
  const kind = ptKind, d = pt[kind], box = $('sptList');
  box.textContent = '';
  box.dataset.kind = kind;
  const mk = (tag, attrs = {}, text = '') => { const e = document.createElement(tag); Object.assign(e, attrs); if (text) e.textContent = text; return e; };
  const head = mk('div', { className: 'spt-row spt-head ' + kind.toLowerCase() });
  for (const t of kind === 'SPT' ? ['#', 'List', 'x', 'y', 'z', 'Heading', 'Ground', ''] : ['#', 'Type', 'x', 'y', 'z (rest)', 'Shadow', '']) head.append(mk('span', {}, t));
  box.append(head);
  const redraw = () => { updatePtView(kind); ptInfo(); };
  d.points.forEach((p, k) => {
    const row = mk('div', { className: 'spt-row ' + kind.toLowerCase() + (k === ptSel ? ' sel' : '') });
    row.onclick = () => { ptSel = k; updatePtView(kind); ptRows(); };
    const num = (v, set, step = 1) => {
      const i = mk('input', { type: 'number', step, value: Math.round(v * 100) / 100 });
      i.onclick = (e) => e.stopPropagation();
      i.onchange = () => { const x = parseFloat(i.value); if (Number.isFinite(x)) { set(x); redraw(); } };
      return i;
    };
    const key = kind === 'SPT' ? 'list' : 'type';
    const sel = mk('select');
    d.lists.forEach((n, li) => sel.append(new Option(`${li}: ${n}`, li)));
    sel.value = p[key];
    sel.onclick = (e) => e.stopPropagation();
    sel.onchange = () => { p[key] = parseInt(sel.value, 10); redraw(); };
    const flag = mk('input', { type: 'checkbox' });
    if (kind === 'SPT') {
      flag.checked = !!p.snap;
      flag.title = 'Ground: the point drops to the floor (or roof) under it when a monster appears';
      flag.onchange = () => { p.snap = flag.checked; };
    } else {
      flag.checked = (p.flags & 1) !== 0;
      flag.title = 'Shadow under the power-up (the game then keeps it on the floor)';
      flag.onchange = () => { p.flags = flag.checked ? (p.flags & 7 ? p.flags : 7) : 0; };
    }
    flag.onclick = (e) => e.stopPropagation();
    const here = mk('button', { type: 'button', title: 'Move this point to where the middle of the view meets the stage' }, 'Aim');
    here.onclick = (e) => { e.stopPropagation(); p.pos = viewPoint().map((x) => Math.round(x * 10) / 10); ptRows(); redraw(); };
    const del = mk('button', { type: 'button', title: 'Delete this point' }, '×');
    del.onclick = (e) => { e.stopPropagation(); d.points.splice(k, 1); ptSel = -1; ptRows(); redraw(); };
    const cells = [mk('span', {}, `${k + 1}`), sel, num(p.pos[0], (x) => { p.pos[0] = x; }), num(p.pos[1], (x) => { p.pos[1] = x; }),
                   num(p.pos[2], (x) => { p.pos[2] = x; })];
    if (kind === 'SPT') cells.push(num(p.heading, (x) => { p.heading = wrapDeg(x); }, 5));
    cells.push(flag, mk('span', {}, ''));
    cells[cells.length - 1].append(here, del);
    row.append(...cells);
    box.append(row);
  });
}
function ptInfo() {
  const d = pt[ptKind], n = (k) => d.points.filter((p) => p[ptKind === 'SPT' ? 'list' : 'type'] === k).length;
  $('sptInfo').textContent = ptKind === 'SPT'
    ? d.lists.map((_n, k) => `${k}: ${n(k)} of ${d.caps[k]}`).join(' · ') + ` · ${d.points.length} of 255 points`
    : `${d.points.length} power-ups (the game keeps ${d.caps[0]} at a time; the file holds 255)`;
}
const ptErr = (t) => { $('sptErr').hidden = !t; if (t) $('sptErr').querySelector('span').textContent = t; };
async function openPoints(kind) {
  ptKind = kind;
  ptSel = -1;
  await loadPoints(kind);
  const d = pt[kind], w = PT_WORDS[kind][1];
  $('sptTitle').textContent = kind === 'SPT' ? 'Start points' : 'Power-ups';
  $('sptText').textContent = d.source === 'mod' ? `The ${w} this mod keeps for ${title(stageName)}.`
                                                : `${title(stageName)}'s own ${w}; Save keeps your version in the mod.`;
  const nl = $('sptNewList');
  nl.textContent = '';
  d.lists.forEach((n, k) => nl.append(new Option(`${k}: ${n}`, k)));
  nl.value = kind === 'SPT' ? '2' : '0';
  $('sptDlg').dataset.kind = kind;
  ptErr('');
  ptRows();
  ptInfo();
  $('sptDlg').showModal();
}
$('sptBtn').onclick = () => openPoints('SPT');
$('pupBtn').onclick = () => openPoints('PUP');
$('sptAdd').onclick = () => {
  const kind = ptKind, d = pt[kind];
  if (d.points.length >= 255) { ptErr(`A stage holds at most 255 ${PT_WORDS[kind][1]}.`); return; }
  const pos = viewPoint().map((x) => Math.round(x * 10) / 10), v = parseInt($('sptNewList').value, 10);
  d.points.push(kind === 'SPT' ? { list: v, pos, heading: 0, snap: true, w: 0, sel: 0 } : { type: v, pos, flags: 7, w: 0, sel: 0 });
  ptSel = d.points.length - 1;
  ptErr('');
  updatePtView(kind);
  ptRows();
  ptInfo();
};
async function ptPost(body) {
  const kind = ptKind;
  ptErr('');
  try {
    const r = await api('/api/spt', Object.assign({ mod: modNow || '', name: stageName, kind }, body));
    pt[kind] = r;
    ptSaved[kind] = ptKeep(kind);
    ptSel = -1;
    updatePtView(kind);
    ptRows();
    ptInfo();
    return r;
  } catch (e) { ptErr(cleanError(e).text); return null; }
}
$('sptSave').onclick = async () => {
  const kind = ptKind, r = await ptPost({ points: pt[kind].points });
  if (!r) return;
  $('sptDlg').close();
  msg(`Saved ${r.points.length} ${PT_WORDS[kind][1]} in the mod` + (r.warnings.length ? '. ' + r.warnings.join(' ') : '. Restart the stage to see them'),
      { level: r.warnings.length ? 'warn' : 'ok', toast: true });
};
$('sptReset').onclick = async () => {
  const kind = ptKind, r = await ptPost({ reset: true });
  if (r) msg(`The stage's own ${PT_WORDS[kind][1]} are back (the mod's file is gone)`, { level: 'ok', toast: true });
};
$('sptDlg').addEventListener('close', () => { if (ptKeep(ptKind) !== ptSaved[ptKind]) loadPoints(ptKind); });   // closed without saving: back to the file's points

// ------------------------------------------------------------------ AI net (.AIP)
// The paths the computer monsters walk: nodes (position, a type that splits the net into sub-nets) and directed links with a width (the
// lane is width * 10 to each side). Edited like the start points, in a file of the mod (editor.py kind AIP; tools/lvl.py read_aip/build_aip).
let aip = { points: [], caps: [255, 450, 6], source: 'disc' }, aipSaved = '[]', aipSel = -1, aipView = null, aipLoadId = 0;
const aipKeep = () => JSON.stringify(aip.points);
async function loadAip() {
  if (!stageName) return;
  const id = ++aipLoadId;
  try {
    const r = await api(`/api/spt?name=${encodeURIComponent(stageName)}&mod=${encodeURIComponent(modNow || '')}&kind=AIP`);
    if (id !== aipLoadId) return;
    aip = r;
    aipSaved = aipKeep();
    aipSel = -1;
  } catch (e) { if (id === aipLoadId) aip = { points: [], caps: [255, 450, 6], source: 'disc' }; }
  updateAipView();
}
const aipTwoWay = (a, b) => aip.points[a].links.some((l) => l.to === b) && aip.points[b].links.some((l) => l.to === a);
function aipPaths() {   // as the game counts them: a two-way link is one path
  const seen = new Set();
  aip.points.forEach((n, i) => n.links.forEach((l) => seen.add(aipTwoWay(i, l.to) ? `${Math.min(i, l.to)}-${Math.max(i, l.to)}` : `${i}>${l.to}`)));
  return seen.size;
}
function updateAipView() {
  if (aipView) { scene.remove(aipView); aipView.traverse((o) => { o.geometry?.dispose(); o.material?.dispose(); }); aipView = null; }
  if (!aip.points.length) return;
  aipView = new THREE.Group();
  const up = new THREE.Vector3(0, 6, 0), pts = [], selPts = [];
  aip.points.forEach((n, i) => n.links.forEach((l) => {
    if (aipTwoWay(i, l.to) && l.to < i) return;
    const a = toThree(n.pos).add(up), b = toThree(aip.points[l.to].pos).add(up);
    (i === aipSel || l.to === aipSel ? selPts : pts).push(a, b);
  }));
  const lines = (p, color) => {
    const o = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(p), new THREE.LineBasicMaterial({ color, depthTest: false }));
    o.renderOrder = 997;
    o.raycast = () => {};
    return o;
  };
  if (pts.length) aipView.add(lines(pts, 0xB57BFF));
  if (selPts.length) aipView.add(lines(selPts, 0xFFC233));
  aip.points.forEach((n, i) => {
    const m = new THREE.Mesh(new THREE.SphereGeometry(i === aipSel ? 11 : 7, 10, 8), new THREE.MeshBasicMaterial({ color: i === aipSel ? 0xFFFFFF : n.type ? 0x36C5F0 : 0xB57BFF, depthTest: false }));
    m.position.copy(toThree(n.pos)).add(up);
    m.renderOrder = 998;
    m.raycast = () => {};
    aipView.add(m);
  });
  scene.add(aipView);
}
const aipErr = (t) => { $('aipErr').hidden = !t; if (t) $('aipErr').querySelector('span').textContent = t; };
function aipInfo() {
  const n = aip.points.length, p = aipPaths();
  $('aipInfo').textContent = `${n} of ${aip.caps[0]} nodes · ${p} of ${aip.caps[1]} paths (a two-way link is one path) · ${aip.caps[2]} links at most per node`;
}
function aipRows() {
  const box = $('aipList');
  box.textContent = '';
  const mk = (tag, attrs = {}, text = '') => { const e = document.createElement(tag); Object.assign(e, attrs); if (text) e.textContent = text; return e; };
  const head = mk('div', { className: 'spt-row spt-head aip' });
  for (const t of ['#', 'Type', 'x', 'y', 'z', 'Links to', '']) head.append(mk('span', {}, t));
  box.append(head);
  const redraw = () => { updateAipView(); aipInfo(); };
  aip.points.forEach((n, k) => {
    const row = mk('div', { className: 'spt-row aip' + (k === aipSel ? ' sel' : '') });
    row.onclick = () => { aipSel = k; aipRows(); updateAipView(); };
    const num = (v, set, step = 1) => {
      const i = mk('input', { type: 'number', step, value: Math.round(v * 100) / 100 });
      i.onclick = (e) => e.stopPropagation();
      i.onchange = () => { const x = parseFloat(i.value); if (Number.isFinite(x)) { set(x); redraw(); } };
      return i;
    };
    const links = n.links.map((l) => `${l.to + 1}${aipTwoWay(k, l.to) ? '' : '→'}(w${l.width})`).join('  ');
    const here = mk('button', { type: 'button', title: 'Move this node to where the middle of the view meets the stage' }, 'Aim');
    here.onclick = (e) => { e.stopPropagation(); n.pos = viewPoint().map((x) => Math.round(x * 10) / 10); aipRows(); redraw(); };
    const del = mk('button', { type: 'button', title: 'Delete this node and its links' }, '×');
    del.onclick = (e) => {
      e.stopPropagation();
      aip.points.splice(k, 1);
      for (const m of aip.points) m.links = m.links.filter((l) => l.to !== k).map((l) => ({ to: l.to > k ? l.to - 1 : l.to, width: l.width }));
      aipSel = -1;
      aipRows();
      redraw();
    };
    const cells = [mk('span', {}, `${k + 1}`), num(n.type, (x) => { n.type = Math.max(0, Math.min(255, Math.round(x))); }),
                   num(n.pos[0], (x) => { n.pos[0] = x; }), num(n.pos[1], (x) => { n.pos[1] = x; }), num(n.pos[2], (x) => { n.pos[2] = x; }),
                   mk('span', { title: 'A number alone is a two-way link; an arrow marks a one-way link out of this node' }, links), mk('span', {}, '')];
    cells[cells.length - 1].append(here, del);
    row.append(...cells);
    box.append(row);
  });
}
function aipLinkBoth(a, b, width) {
  const add = (x, y) => {
    const ex = aip.points[x].links.find((l) => l.to === y);
    if (ex) { ex.width = width; return true; }
    if (aip.points[x].links.length >= aip.caps[2]) return false;
    aip.points[x].links.push({ to: y, width });
    return true;
  };
  return add(a, b) && add(b, a);
}
$('aipBtn').onclick = async () => {
  await loadAip();
  $('aipText').textContent = aip.source === 'mod' ? `The AI net this mod keeps for ${title(stageName)}.` : `${title(stageName)}'s own AI net; Save keeps your version in the mod.`;
  aipErr('');
  aipRows();
  aipInfo();
  $('aipDlg').showModal();
};
const aipWidth = () => Math.max(0, Math.min(255, parseInt($('aipWidth').value, 10) || 20));
$('aipAdd').onclick = () => {
  if (aip.points.length >= aip.caps[0]) { aipErr(`A net holds at most ${aip.caps[0]} nodes.`); return; }
  const from = aipSel, node = { pos: viewPoint().map((x) => Math.round(x * 10) / 10), type: Math.max(0, Math.min(255, parseInt($('aipType').value, 10) || 0)), links: [], w: 0, sel: 0, tail: '' };
  aip.points.push(node);
  const k = aip.points.length - 1;
  if (from >= 0 && from < k && !aipLinkBoth(from, k, aipWidth())) {
    aip.points.pop();
    for (const m of aip.points) m.links = m.links.filter((l) => l.to !== k);
    aipErr(`Node ${from + 1} already has ${aip.caps[2]} links.`);
    return;
  }
  aipSel = k;
  aipErr('');
  aipRows();
  updateAipView();
  aipInfo();
};
$('aipLink').onclick = () => {
  const b = parseInt($('aipTo').value, 10) - 1, a = aipSel;
  if (a < 0) { aipErr('Select a node (click its row) first.'); return; }
  if (!(b >= 0 && b < aip.points.length) || b === a) { aipErr('Type the number of another node.'); return; }
  if (!aipLinkBoth(a, b, aipWidth())) { aipErr(`A node holds at most ${aip.caps[2]} links.`); return; }
  aipErr('');
  aipRows();
  updateAipView();
  aipInfo();
};
$('aipUnlink').onclick = () => {
  const b = parseInt($('aipTo').value, 10) - 1, a = aipSel;
  if (a < 0 || !(b >= 0 && b < aip.points.length)) { aipErr('Select a node, then type the number of the other one.'); return; }
  aip.points[a].links = aip.points[a].links.filter((l) => l.to !== b);
  aip.points[b].links = aip.points[b].links.filter((l) => l.to !== a);
  aipErr('');
  aipRows();
  updateAipView();
  aipInfo();
};
$('aipClear').onclick = () => { aip.points = []; aipSel = -1; aipErr(''); aipRows(); updateAipView(); aipInfo(); };
async function aipPost(body) {
  aipErr('');
  try {
    const r = await api('/api/spt', Object.assign({ mod: modNow || '', name: stageName, kind: 'AIP' }, body));
    aip = r;
    aipSaved = aipKeep();
    aipSel = -1;
    aipRows();
    updateAipView();
    aipInfo();
    return r;
  } catch (e) { aipErr(cleanError(e).text); return null; }
}
$('aipSave').onclick = async () => {
  const r = await aipPost({ points: aip.points });
  if (!r) return;
  $('aipDlg').close();
  msg(`Saved an AI net of ${r.points.length} nodes in the mod` + (r.warnings.length ? '. ' + r.warnings.join(' ') : '. Restart the stage to see it'),
      { level: r.warnings.length ? 'warn' : 'ok', toast: true });
};
$('aipReset').onclick = async () => {
  const r = await aipPost({ reset: true });
  if (r) msg("The stage's own AI net is back (the mod's file is gone)", { level: 'ok', toast: true });
};
$('aipDlg').addEventListener('close', () => { if (aipKeep() !== aipSaved) loadAip(); });
// A floor over the new rectangle: a prop of the mod (a visible plate and a coll_ slab), placed at the rectangle's center
$('gdFloor').onclick = async () => {
  if (!modNow) { gdErr('Choose a mod first: the floor is a prop saved in it.'); return; }
  if (!gdApply()) return;
  const g = gridNow();
  if (!g) { msg("The stage size is the disc's: there is nothing to put a floor over", { level: 'warn', toast: true }); return; }
  let res;
  try { res = await api('/api/props/floor', { mod: modNow, stage: stageName, min: g.min, max: g.max }); }
  catch (e) { msg(cleanError(e).text, { level: 'err', toast: true }); return; }
  const p = JSON.parse(JSON.stringify(buildPatch()));
  p.props = (p.props || []).filter((e) => propKey(e.model) !== propKey(res.model) && e.id !== 'floor');
  p.props.push({ id: 'floor', model: res.model, pos: res.pos, name: 'Floor' });
  await loadPropModels(modNow);
  restoreTo(JSON.stringify(p));
  msg(`Floor ${res.model} over the new size${res.prop?.error ? `, but ${res.prop.error}` : ''}. Check it, then Save`, { level: res.prop?.error ? 'warn' : 'ok', toast: true });
};

// Stage picture: the view's middle 2:1 band, 512x256, into mods/<mod>/levelpics/ (the stage select's picture)
$('picBtn').onclick = async () => {
  if (!stageName) return;
  if (!modNow) { msg('Choose a mod first: the picture is saved in it', { level: 'warn', toast: true }); return; }
  renderer.render(scene, camera);          // the canvas keeps no frame between renders: read it right after one
  const src = renderer.domElement, w = src.width, h = src.height, cw = Math.min(w, h * 2), ch = cw / 2;
  const c = document.createElement('canvas');
  c.width = 512; c.height = 256;
  c.getContext('2d').drawImage(src, (w - cw) / 2, (h - ch) / 2, cw, ch, 0, 0, 512, 256);
  const png = c.toDataURL('image/png').replace(/^data:[^,]*,/, '');
  try {
    const r = await api('/api/levelpic', { mod: modNow, name: stageName, png });
    msg(`Saved the view as the stage picture: ${r.saved}`, { level: 'ok', toast: true });
  } catch (e) { msg(cleanError(e).text, { level: 'err', toast: true }); }
};

// A prop's picture for the palette: its preview scene seen from a corner, drawn once into a small render target of the view's renderer
let thumbRT = null;
function propThumb(pm) {
  if (pm.thumb !== undefined) return pm.thumb;
  pm.thumb = null;
  if (!pm.scene) return null;
  const S = 192, sc = new THREE.Scene(), prev = renderer.getRenderTarget();
  try {
    const obj = pm.scene.clone(true);
    sc.add(obj);
    sc.background = new THREE.Color(0x30343b);
    const box = new THREE.Box3().setFromObject(obj), size = box.getSize(new THREE.Vector3()), mid = box.getCenter(new THREE.Vector3());
    const r = Math.max(size.length() / 2, 1), cam = new THREE.PerspectiveCamera(35, 1, r * 0.05, r * 40);
    cam.position.copy(mid).add(new THREE.Vector3(0.8, 0.7, 1).normalize().multiplyScalar(r / Math.sin(THREE.MathUtils.degToRad(17.5)) * 1.1));
    cam.lookAt(mid);
    thumbRT = thumbRT || new THREE.WebGLRenderTarget(S, S);
    renderer.setRenderTarget(thumbRT);
    renderer.render(sc, cam);
    const px = new Uint8Array(S * S * 4);
    renderer.readRenderTargetPixels(thumbRT, 0, 0, S, S, px);
    const c = document.createElement('canvas');
    c.width = c.height = S;
    const g = c.getContext('2d'), im = g.createImageData(S, S);
    for (let y = 0; y < S; y++) im.data.set(px.subarray((S - 1 - y) * S * 4, (S - y) * S * 4), y * S * 4);   // the target is bottom-up
    g.putImageData(im, 0, 0);
    pm.thumb = c.toDataURL('image/png');
  } catch (e) { pm.thumb = null; }
  finally { renderer.setRenderTarget(prev); }
  return pm.thumb;
}

// The original models of the other stages (/api/originals, built by the server in the background): a search result makes the model a
// prop of the mod (/api/props/original) and adds it. Their pictures load as the rows show (/api/origglb).
let origIdx = null, origTimer = 0, origBy = new Map();   // origBy: "STAGE:kind:sig" -> the index entry (a stage's own object finds its picture by its model)
const origPics = new Map();   // "STAGE:handle" -> {pic: data URL or null}
async function origEnsure() {
  clearTimeout(origTimer);
  if (!$('palDlg').open || (origIdx && origIdx.ready)) return;
  try { origIdx = await api('/api/originals'); } catch (e) { origIdx = { ready: true, items: [], done: 0, total: 0 }; return; }
  origBy = new Map(origIdx.items.map((it) => [`${it.stage}:${it.kind}:${it.sig}`, it]));
  fillPalStages();
  if ($('palDlg').open) palRender();
  if (!origIdx.ready) origTimer = setTimeout(origEnsure, 1500);
}
// A model's picture: the PNG the server kept from an earlier render; without one, the page renders the model (its .glb, a few at a
// time) and sends the PNG to be kept, so each model is rendered once for good.
const origQ = [];
let origBusy = 0;
function origPump() {
  while (origBusy < 3 && origQ.length) { origBusy++; origQ.shift()().finally(() => { origBusy--; origPump(); }); }
}
function origPic(it) {
  const key = `${it.stage}:${it.handle}`, q = `stage=${encodeURIComponent(it.stage)}&handle=${encodeURIComponent(it.handle)}`;
  let e = origPics.get(key);
  if (e) return e.pic;
  origPics.set(key, e = { pic: null });
  const done = () => { if (e.pic && $('palDlg').open) palRender(); };
  origQ.push(() => new Promise((res) => {
    const im = new Image();
    im.onload = () => { e.pic = im.src; it.pic = true; done(); res(); };
    im.onerror = () => origRender(it).then((p) => { e.pic = p; done(); }).finally(res);
    im.src = `/api/origpic?${q}`;
  }));
  origPump();
  return null;
}
// Renders one model (its .glb from the server) into a small picture, sends the PNG to be kept and returns it as a data URL (null on failure)
function origRender(it) {
  return new GLTFLoader().loadAsync(`/api/origglb?stage=${encodeURIComponent(it.stage)}&handle=${encodeURIComponent(it.handle)}`).then(async (gltf) => {
    gltf.scene.traverse((o) => {
      if (o.userData && o.userData.hidden) o.visible = false;
      if (o.isMesh) for (const m of [].concat(o.material)) gsMaterial(m);
    });
    const pic = propThumb({ scene: gltf.scene });
    if (pic) { it.pic = true; await api('/api/origpic', { stage: it.stage, handle: it.handle, png: pic.replace(/^data:[^,]*,/, '') }).catch(() => {}); }
    gltf.scene.traverse((o) => { if (o.isMesh) { o.geometry.dispose(); for (const m of [].concat(o.material)) { if (m.map) m.map.dispose(); m.dispose(); } } });
    return pic;
  }).catch(() => null);
}
// Renders the pictures that are not kept yet, every stage's, three at a time (the scene of a stage is read once for all its models);
// ?bake=1 does it on load. Progress in the palette's footer.
const bake = { on: false, done: 0, total: 0, failed: 0 };
async function bakeOriginals() {
  if (bake.on) return;
  bake.on = true; bake.done = bake.total = bake.failed = 0;
  try {
    let idx = await api('/api/originals');
    while (!idx.ready) { await sleep(1000); idx = await api('/api/originals'); }
    const todo = idx.items.filter((i) => !i.pic).sort((a, b) => a.stage.localeCompare(b.stage) || a.handle.localeCompare(b.handle));
    bake.total = todo.length;
    document.title = `Baking pictures 0/${bake.total}`;
    const worker = async () => {
      for (let it = todo.shift(); it; it = todo.shift()) {
        const p = await origRender(it);
        bake.done++;
        if (!p) bake.failed++;
        if (bake.done % 10 === 0 || bake.done === bake.total) { document.title = `Baking pictures ${bake.done}/${bake.total}`; if ($('palDlg').open) palRender(); }
      }
    };
    await Promise.all([worker(), worker(), worker()]);
    msg(`Rendered ${bake.done - bake.failed} of ${bake.total} pictures for the palette${bake.failed ? `, ${bake.failed} failed` : ''}`, { level: bake.failed ? 'warn' : 'ok', toast: true });
    origIdx = null; origPics.clear();
  } catch (e) { msg(`Rendering the pictures stopped: ${cleanError(e).text}`, { level: 'err', toast: true }); }
  finally { bake.on = false; if ($('palDlg').open) origEnsure(); }
}
async function addOriginal(it) {
  if (!modNow) { msg('Choose a mod first: the prop is built into it', { level: 'warn', toast: true }); return; }
  msg(`Building ${it.name} from ${it.title}…`);
  let res;
  try { res = await api('/api/props/original', { mod: modNow, stage: stageName, from: it.stage, handle: it.handle }); }
  catch (e) { msg(cleanError(e).text, { level: 'err', toast: true }); return; }
  const m = res.prop || {};
  if (!propModels.get(m.model)?.scene) {
    const p = JSON.stringify(buildPatch());
    await loadPropModels(modNow);
    restoreTo(p);
  }
  if (!propModels.get(m.model)?.scene) { msg(`${m.model}: ${m.error || 'its preview does not load'}`, { level: 'warn', toast: true }); return; }
  addProp(m.model);
}

// A copy of one of this stage's own models where the view looks (the stage's object is copied, as Copy does)
function addStageModel(it) {
  const rec = byHandle.get(normHandle(it.handle));
  if (!rec) { msg(`${it.name}: this stage has no such object any more`, { level: 'warn', toast: true }); return; }
  if (editMode !== 'objects') enterMode('objects');
  snapshot('add ' + it.name);
  const c = makeCopy(rec, viewPoint(), headingOf(rec.item));
  msg(`Added a copy of ${it.name}`, { level: 'ok', toast: ['Undo', undo] });
  select(c);
}

// Ctrl+P palette, two tabs. Add: cards of the mod's props for this stage and of every model of the game's stages (this stage's first):
// a card of this stage's copies the object, one of another stage builds it as a prop of the mod (addOriginal) and adds it.
// Find: the stage's objects, one card per model (select and frame).
let palIdx = 0, palRows = [], palTab = 'add', palStageSel = '';
let ORIG_ROWS = 16;        // models per page: the grid's columns (cards of at least PAL_CARD px, as the CSS lays them) times the rows that fit
const PAL_CARD = 170;
let palPropsOpen = true;   // the "This mod's props" group; kept between sessions
try { palPropsOpen = localStorage.getItem('palProps') !== 'closed'; } catch (e) { /* no storage: open */ }
function fitPage() {
  const w = $('palList').clientWidth;
  if (!w) return;
  const cols = Math.max(2, Math.floor((w + 8) / (PAL_CARD + 8))), card = (w - 8 * (cols - 1)) / cols;
  ORIG_ROWS = cols * Math.max(2, Math.floor((window.innerHeight - 260) / (card + 50)));
}
let origPage = 0, origTotal = 0;
function origResults(q) {
  const words = q.toLowerCase().split(/\s+/).filter(Boolean);
  origTotal = 0;
  if (palTab !== 'add' || !origIdx) return [];
  const here = baseOf(stageName), isHere = (it) => baseOf(it.stage) === here;
  const all = origIdx.items.filter((it) => !palStageSel || it.stage === palStageSel)
    .filter((it) => { const t = `${it.name} ${it.title} ${it.stage} ${it.kind === 'destr' ? 'breakable destructible' : 'prop'}`.toLowerCase(); return words.every((w) => t.includes(w)); })
    .sort((a, b) => isHere(b) - isHere(a) || a.stage.localeCompare(b.stage) || b.count - a.count || a.name.localeCompare(b.name));
  origTotal = all.length;
  origPage = Math.min(origPage, Math.max(0, Math.ceil(all.length / ORIG_ROWS) - 1));
  return all.slice(origPage * ORIG_ROWS, (origPage + 1) * ORIG_ROWS)
    .map((it) => ({ kind: 'orig', orig: it, label: it.name,
                    meta: `${isHere(it) ? 'This stage' : it.title}${it.kind === 'destr' ? ' · breakable' : ''} · ${it.dims.map((x) => Math.round(x)).join('×')}`,
                    run: () => (isHere(it) ? addStageModel(it) : addOriginal(it)) }));
}
function setPalTab(t) {
  palTab = t;
  palIdx = 0;
  origPage = 0;
  $('palTabAdd').setAttribute('aria-pressed', t === 'add');
  $('palTabFind').setAttribute('aria-pressed', t === 'find');
  $('palStage').hidden = t !== 'add';
  $('palQ').placeholder = t === 'add' ? 'Search the models to add…' : 'Search this stage’s objects (name or handle)…';
  palRender(true);
  $('palQ').focus();
}
function fillPalStages() {            // the stages that have models, in the stage list's order
  const sel = $('palStage'), have = new Set((origIdx ? origIdx.items : []).map((i) => i.stage)), keep = palStageSel;
  sel.innerHTML = `<option value="">All stages</option>` + Object.keys(stageTitles).filter((n) => have.has(n))
    .map((n) => `<option value="${n}">${stageTitles[n]}${n === stageName || n === baseOf(stageName) ? ' (this stage)' : ''}</option>`).join('');
  sel.value = have.has(keep) ? keep : '';
  palStageSel = sel.value;
}
function origGo(d) {
  const pages = Math.max(1, Math.ceil(origTotal / ORIG_ROWS)), p = Math.max(0, Math.min(pages - 1, origPage + d));
  if (p === origPage) return;
  origPage = p;
  palRender();                       // the new page's rows; the first of them gets the selection
  palIdx = Math.max(0, palRows.findIndex((r) => r.kind === 'orig'));
  palRender(true);
}
function palResults(q) {
  const words = q.toLowerCase().split(/\s+/).filter(Boolean), fits = (t) => words.every((w) => t.toLowerCase().includes(w));
  const props = palTab !== 'add' || palStageSel ? [] : [...propModels.values()].filter((pm) => pm.info.stage === baseOf(stageName) && pm.scene)
    .map((pm) => ({ kind: 'prop', text: `${pm.info.name || pm.info.model} ${pm.info.model}`, label: `${pm.info.name && pm.info.name.toUpperCase() !== pm.info.model ? pm.info.name : pm.info.model}`,
                    meta: pm.info.triangles ? `${pm.info.triangles} triangles` : '', pm, run: () => addProp(pm.info.model) }))
    .filter((r) => fits(r.text));
  // one card per model (name and signature): the first match is what it selects, with how many there are
  const groups = new Map();
  for (const it of palTab === 'find' ? items() : []) {
    if (it.rec.deleted) continue;
    const name = it.copy ? it.iname || `Copy ${it.rec.copies.indexOf(it) + 1}` : nameOf(it);
    if (!fits(`${name} ${it.rec.info.handle}`)) continue;
    const k = `${name}|${it.rec.info.sig || ''}`, g = groups.get(k);
    if (g) g.n++; else groups.set(k, { it, name, n: 1 });
  }
  const objs = [...groups.values()].slice(0, 40)
    .map((o) => ({ kind: 'obj', label: o.name, meta: o.n > 1 ? `${o.n} in this stage` : o.it.rec.info.handle,
                   key: `${baseOf(stageName)}:${o.it.rec.info.kind}:${o.it.rec.info.sig}`, run: () => { select(o.it); focusSelection(); } }));
  return { props, objs, origs: origResults(q) };
}
// follow: scroll to the selected row (a key, a page, a new search); a redraw for a picture that arrived keeps the list where it is
function palRender(follow = false) {
  fitPage();
  const { props, objs, origs } = palResults($('palQ').value);
  palRows = [...(palPropsOpen ? props : []), ...objs, ...origs];
  palIdx = Math.min(palIdx, Math.max(0, palRows.length - 1));
  const list = $('palList'), top = list.scrollTop;
  list.textContent = '';
  const head = (t, pager, fold) => {
    const d = document.createElement('div');
    d.className = 'pal-h';
    const s = document.createElement('span');
    s.textContent = t;
    d.append(s);
    if (fold) {
      d.classList.add('pal-fold');
      d.setAttribute('role', 'button');
      d.onmousedown = (e) => e.preventDefault();      // the search keeps the focus
      d.onclick = fold;
    }
    if (pager) d.append(...pager);
    list.appendChild(d);
  };
  let n = 0, grid = null;
  const group = (t, pager, fold) => { head(t, pager, fold); grid = document.createElement('div'); grid.className = 'pal-grid'; list.appendChild(grid); return grid; };
  const add = (r) => {
    const i = n++, b = document.createElement('button');
    b.type = 'button'; b.className = 'ghost pal-card'; b.setAttribute('role', 'option'); b.setAttribute('aria-selected', i === palIdx);
    b.title = `${r.label} · ${r.meta}`;
    const pic = r.pm ? propThumb(r.pm) : r.orig ? origPic(r.orig) : r.key && origBy.get(r.key) ? origPic(origBy.get(r.key)) : null;
    if (pic) { const im = document.createElement('img'); im.src = pic; im.alt = ''; im.className = 'pal-pic'; b.append(im); }
    else { const ph = document.createElement('span'); ph.className = 'pal-pic pal-nopic'; ph.append(icon(r.kind === 'obj' ? 'cube-outline' : 'shape-plus-outline')); b.append(ph); }
    const l = document.createElement('span'), m = document.createElement('span');
    l.className = 'pal-name'; l.textContent = r.label; m.className = 'pal-meta'; m.textContent = r.meta;
    b.append(l, m);
    b.onclick = () => palPick(r);
    grid.appendChild(b);
  };
  if (props.length) {
    const fold = () => {
      palPropsOpen = !palPropsOpen;
      try { localStorage.setItem('palProps', palPropsOpen ? 'open' : 'closed'); } catch (e) { /* ignore */ }
      palIdx = 0;
      palRender(true);
    };
    if (palPropsOpen) { group(`▾ This mod's props · ${props.length}`, null, fold); props.forEach(add); }
    else head(`▸ This mod's props · ${props.length}`, null, fold);
  }
  if (objs.length) { group('Objects in this stage'); objs.forEach(add); }
  if (origs.length) {
    const pages = Math.ceil(origTotal / ORIG_ROWS), pg = (txt, d, off) => {
      const b = document.createElement('button');
      b.type = 'button'; b.className = 'ghost pal-pg'; b.textContent = txt; b.disabled = off; b.title = d < 0 ? 'Previous page (PageUp)' : 'Next page (PageDown)';
      b.onmousedown = (e) => e.preventDefault();      // the search keeps the focus
      b.onclick = () => origGo(d);
      return b;
    };
    group(`Models from the game's stages · ${origPage * ORIG_ROWS + 1}–${origPage * ORIG_ROWS + origs.length} of ${origTotal}`,
          pages > 1 ? [pg('‹', -1, origPage === 0), pg('›', 1, origPage >= pages - 1)] : null);
    origs.forEach(add);
  }
  if (palTab === 'add' && origIdx && origIdx.ready) {         // pictures not rendered yet: all of them in one go, or as the rows show
    const missing = origIdx.items.filter((i) => !i.pic).length;
    if (bake.on || missing) {
      const d = document.createElement('div');
      d.className = 'pal-h';
      const s = document.createElement('span');
      s.textContent = bake.on ? `Rendering the pictures… ${bake.done} of ${bake.total}` : `${missing} pictures not rendered yet`;
      d.append(s);
      if (!bake.on) {
        const b = document.createElement('button');
        b.type = 'button'; b.className = 'ghost pal-pg'; b.textContent = 'Render all'; b.style.width = 'auto'; b.style.padding = '0 8px';
        b.title = 'Renders every model once and keeps the pictures (a few minutes)';
        b.onmousedown = (e) => e.preventDefault();
        b.onclick = () => { bakeOriginals(); palRender(); };
        d.append(b);
      }
      list.appendChild(d);
    }
  }
  if (palTab === 'add' && !(origIdx && origIdx.ready)) {
    const d = document.createElement('div');
    d.className = 'pm-none';
    d.textContent = `Looking through the other stages' objects… ${origIdx ? origIdx.done + ' of ' + origIdx.total : ''}`;
    list.appendChild(d);
  }
  if (!palRows.length && !(origIdx && !origIdx.ready)) { const d = document.createElement('div'); d.className = 'pm-none'; d.textContent = 'Nothing matches.'; list.appendChild(d); }
  if (follow) list.querySelector('[aria-selected="true"]')?.scrollIntoView({ block: 'nearest' });
  else list.scrollTop = top;
}
function palPick(r) { $('palDlg').close(); r.run(); }
function openPalette() {
  if (!stageName || document.querySelector('dialog[open]')) return;
  $('palQ').value = '';
  palTab = 'add';
  setPalTab('add');
  fillPalStages();
  $('palDlg').showModal();
  palRender(true);                      // again, now that the grid has a width: the page holds as many cards as fit
  $('palQ').focus();
  origEnsure();
}
window.addEventListener('resize', () => { if ($('palDlg').open) palRender(); });
$('palTabAdd').onclick = () => setPalTab('add');
$('palTabFind').onclick = () => setPalTab('find');
$('palStage').onchange = () => { palStageSel = $('palStage').value; palIdx = 0; origPage = 0; palRender(true); $('palQ').focus(); };
$('palQ').oninput = () => { palIdx = 0; origPage = 0; palRender(true); };
$('palQ').onkeydown = (e) => {
  const t = e.target, atEnd = t.selectionStart === t.selectionEnd && t.selectionStart === t.value.length, atStart = t.selectionStart === t.selectionEnd && t.selectionStart === 0;
  if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
    e.preventDefault();
    // the card nearest below or above, in the next row of the grid
    const cards = [...$('palList').querySelectorAll('button[role="option"]')], cur = cards[palIdx];
    if (!cur) return;
    const c = cur.getBoundingClientRect(), cx = c.left + c.width / 2, cy = c.top + c.height / 2, dir = e.key === 'ArrowDown' ? 1 : -1;
    let best = -1, bd = Infinity;
    cards.forEach((b, i) => {
      const r = b.getBoundingClientRect(), dy = (r.top + r.height / 2 - cy) * dir;
      if (dy < 8) return;
      const d = dy * 1000 + Math.abs(r.left + r.width / 2 - cx);        // the nearest row first, then the nearest column
      if (d < bd) { bd = d; best = i; }
    });
    if (best >= 0) { palIdx = best; palRender(true); }
  } else if ((e.key === 'ArrowRight' && atEnd) || (e.key === 'ArrowLeft' && atStart)) {
    e.preventDefault();
    palIdx = Math.max(0, Math.min(palRows.length - 1, palIdx + (e.key === 'ArrowRight' ? 1 : -1)));
    palRender(true);
  } else if (e.key === 'Tab') {
    e.preventDefault();
    setPalTab(palTab === 'add' ? 'find' : 'add');
  } else if (e.key === 'PageDown' || e.key === 'PageUp') {
    e.preventDefault();
    origGo(e.key === 'PageDown' ? 1 : -1);
  } else if (e.key === 'Enter') { e.preventDefault(); if (palRows[palIdx]) palPick(palRows[palIdx]); }
};

// New stage…
const NS_BASES = ['CENTRAL', 'VEGAS', 'CANYON2', 'METRO', 'UFO', 'CAPITOL', 'CANYON1', 'MINISF', 'TOKYODM'];
const nsErr = (t) => { $('nsErr').hidden = !t; $('nsErr').querySelector('span').textContent = t || ''; };
const nsWhere = () => { $('nsWhere').textContent = modNow ? `mods/${modNow}/stages/${($('nsId').value.trim() || '<id>').toLowerCase()}/` : '(choose a mod first)'; };
$('newStage').onclick = () => {
  if (!stageName) return;
  if (!modNow) { msg('Choose a mod first: the new stage is saved in it', { level: 'warn', toast: true }); return; }
  const b = $('nsBase');
  b.innerHTML = NS_BASES.filter((x) => stageTitles[x]).map((x) => `<option value="${x}">${stageTitles[x]} (${x})</option>`).join('');
  b.value = NS_BASES.includes(baseOf(stageName)) ? baseOf(stageName) : (b.options[0] || {}).value;
  $('nsId').value = $('nsName').value = '';
  $('nsCopy').checked = true;
  nsErr('');
  nsWhere();
  $('stageDlg').showModal();
  $('nsId').focus();
};
$('nsId').oninput = () => { nsWhere(); nsErr(''); };
$('nsOk').onclick = async () => {
  const id = $('nsId').value.trim();
  if (!/^[A-Za-z][A-Za-z0-9]{0,7}$/.test(id)) { nsErr('The id is 1 to 8 letters and digits, starting with a letter.'); $('nsId').focus(); return; }
  const base = $('nsBase').value;
  // the patch carries the base's handles: it comes along only when the open stage has that base
  const withPatch = $('nsCopy').checked && baseOf(stageName) === base;
  let res;
  try { res = await api('/api/stage/new', { mod: modNow, id, name: $('nsName').value.trim(), base, patch: withPatch ? buildPatch() : null }); }
  catch (e) { nsErr(cleanError(e).text); return; }
  $('stageDlg').close();
  cfg = Object.assign({}, cfg);
  const o = document.createElement('option');
  o.value = res.name;
  o.textContent = `${res.title} (custom, on ${res.base})`;
  $('stage').appendChild(o);
  stageTitles[res.name] = res.title;
  stageBases[res.name] = res.base;
  dirty = false;
  $('stage').value = res.name;
  await loadStage(res.name);
  msg(`Created ${res.title} on ${stageTitles[res.base] || res.base}. Test in game opens it`, { level: 'ok', toast: true });
};

const ipErr = (t) => { $('ipErr').hidden = !t; $('ipErr').querySelector('span').textContent = t || ''; };
const ipRadio = (n) => document.querySelector(`#propDlg input[name="${n}"]:checked`)?.value;
const ipWhere = () => { $('ipWhere').textContent = `mods/${modNow}/props/${$('ipName').value.trim().toUpperCase() || 'NAME'}/`; };
async function openImport() {
  showPropMenu(false);
  if (!modNow) {
    const m = await pickMod('save', 'Choose');
    if (!m) return;
    moveTo(m, false);
  }
  ipData = null;
  ipTitle = '';
  $('ipFile').textContent = 'No file chosen';
  $('ipName').value = $('ipHeight').value = '';
  ipErr('');
  $('ipText').textContent = `A Blender model exported as glTF Binary (.glb), for ${title(stageName)}.`;
  ipWhere();
  $('propDlg').showModal();
  $('ipPick').focus();
}
$('pmImport').onclick = openImport;
$('ipPick').onclick = () => ipInput.click();
$('ipName').oninput = () => { ipWhere(); ipErr(''); };
// the .glb, from Choose .glb… or dropped on the dialog
function ipRead(f) {
  const rd = new FileReader();
  rd.onload = () => {
    ipData = String(rd.result).replace(/^data:[^,]*,/, '');
    ipTitle = f.name.replace(/\.[^.]*$/, '');
    $('ipFile').textContent = `${f.name} (${Math.max(1, Math.round(f.size / 1024))} KB)`;
    if (!$('ipName').value) $('ipName').value = f.name.replace(/\.[^.]*$/, '').toUpperCase().replace(/[^A-Z0-9_]/g, '').slice(0, 8);
    ipWhere();
    ipErr('');
  };
  rd.onerror = () => ipErr(`Couldn't read ${f.name}`);
  rd.readAsDataURL(f);
}
ipInput.onchange = () => { const f = ipInput.files[0]; ipInput.value = ''; if (f) ipRead(f); };
$('propDlg').addEventListener('dragover', (e) => e.preventDefault());
$('propDlg').addEventListener('drop', (e) => { e.preventDefault(); const f = e.dataTransfer.files[0]; if (f) ipRead(f); });
async function doImport(replace = false) {
  const name = $('ipName').value.trim().toUpperCase();
  if (!ipData) { ipErr('Choose a .glb file first.'); return; }
  if (!/^[A-Z0-9_]{1,8}$/.test(name)) { ipErr('The name is 1 to 8 letters, digits or _.'); $('ipName').focus(); return; }
  const h = $('ipHeight').value.trim();
  const body = { mod: modNow, stage: stageName, name, data: ipData, units: ipRadio('ipUnits'), height: h ? +h : '',
                 light: ipRadio('ipLight'), title: ipTitle, replace };
  const ok = $('ipOk');
  ok.disabled = true;
  ok.textContent = 'Importing…';
  let res;
  try { res = await api('/api/props/import', body); }
  catch (e) { ipErr(cleanError(e).text); return; }
  finally { ok.disabled = false; ok.textContent = 'Import'; }
  if (res.exists) {
    ask(`Replace ${name}?`, `Mod "${modNow}" already has a prop ${name}. The new model takes its place wherever it is used.`,
        [['cancel', 'Cancel'], ['replace', 'Replace', true]], (a) => { if (a === 'replace') doImport(true); });
    return;
  }
  $('propDlg').close();
  const p = JSON.stringify(buildPatch());
  await loadPropModels(modNow);
  restoreTo(p);
  const m = res.prop || {};
  msg(`${res.replaced ? 'Replaced' : 'Imported'} ${m.model || name}: ${m.triangles} triangles` + (m.error ? `, but ${m.error}` : ''),
      { level: m.error ? 'warn' : 'ok' });
  if (!res.replaced && !m.error) addProp(m.model || name);
}
$('ipOk').onclick = () => doImport(false);
// New terrain (web/terrain.js): a prop built from a heightmap, placed at the middle of the stage's rectangle (the grid's, else where the view looks)
async function openTerrainDlg() {
  showPropMenu(false);
  if (!stageName) return;
  if (!modNow) {
    const m = await pickMod('save', 'Choose');
    if (!m) return;
    moveTo(m, false);
  }
  const g = gridNow();
  openTerrain({ stage: stageName, title: title(stageName), mod: modNow, api, ask, msg,
                size: () => (g ? [g.max[0] - g.min[0] - 100, g.max[1] - g.min[1] - 100] : [2000, 2000]),
                done: async (res, replaced) => {
                  const p = JSON.stringify(buildPatch());
                  await loadPropModels(modNow);
                  restoreTo(p);
                  const m2 = res.prop || {};
                  msg(`${replaced ? 'Replaced' : 'Built'} ${m2.model}: ${res.terrain.cells[0]} × ${res.terrain.cells[1]} cells, ${m2.triangles} triangles` + (m2.error ? `, but ${m2.error}` : ''),
                      { level: m2.error ? 'warn' : 'ok' });
                  if (!replaced && !m2.error) addProp(m2.model, g ? [(g.min[0] + g.max[0]) / 2, (g.min[1] + g.max[1]) / 2, 0] : null);
                } });
}
$('pmTerrain').onclick = openTerrainDlg;
$('ipName').onkeydown = (e) => { if (e.key === 'Enter') { e.preventDefault(); doImport(false); } };
// the Fight HUD dialog (web/hud.js): it writes into the mod at once; its messages show as toasts over it
function openHudDialog() {
  if (!stageName) return;
  openHud({ stage: stageName, title: title(stageName), titleOf: title, mod: modNow, api, log, msg, ask,
            mods: () => modList, newMod: NEW_MOD, chooseMod,
            game: () => ({ running: game.state === 'running', dirty }), reload: reloadGame });
}
$('hud').onclick = openHudDialog;
// The Light and fog panel (web/light.js): the file's numbers as the view's env, as editor.py stage_env makes it (fog's FA and FB as
// fogCalcFogValues does, the background the fog's color, each light's direction toward the light in game axes)
function envFromRaw(r) {
  const fog = r.fog ? { ...r.fog } : null;
  if (fog && fog.maxR > fog.minR) {
    const fb = (fog.maxV - 1) * fog.minR * fog.maxR / (fog.maxR - fog.minR);
    fog.fa = 1 - fb / fog.maxR;
    fog.fb = fb;
  }
  const lights = (r.lights || []).map((l) => {
    const h = l.h * Math.PI / 180, p = l.p * Math.PI / 180;
    return { rgb: l.rgb, h: l.h, p: l.p, dir: [Math.cos(p) * Math.sin(h), Math.cos(p) * Math.cos(h), Math.sin(p)] };
  });
  return { file: env ? env.file : '', fog, bg: fog && fog.fa !== undefined ? fog.rgb : r.bg ? r.bg.slice(0, 3) : null, lights };
}
function previewEnv(r) { env = envFromRaw(r); setLights(env.lights); applyLook(); }
async function restoreEnv(name, mod) {   // the panel closed with numbers nobody saved: the view goes back to the file's
  try {
    const info = await api(`/api/stage?name=${encodeURIComponent(name)}&mod=${encodeURIComponent(mod)}`);
    if (name !== stageName) return;
    env = info.env || null;
    setLights(env && env.lights);
    applyLook();
  } catch (e) { /* the stage load shows its own errors */ }
}
$('lightBtn').onclick = () => {
  if (!stageName) return;
  const name = stageName;
  openLight({ stage: name, title: title(name), mod: modNow, api, msg, mods: () => modList, newMod: NEW_MOD, chooseMod,
              preview: previewEnv, restore: () => restoreEnv(name, modNow), look: () => gameLook, setLook,
              game: () => ({ running: game.state === 'running', dirty }), reload: reloadGame });
};
// the Textures dialog (web/tex.js): every picture of the stage; it writes into the mod at once, like the HUD's
$('texBtn').onclick = () => {
  if (!stageName) return;
  openTex({ stage: stageName, title: title(stageName), mod: modNow, api, msg, ask, mods: () => modList, newMod: NEW_MOD, chooseMod,
            scene: () => [...stageTex.keys()], game: () => ({ running: game.state === 'running', dirty }), reload: reloadGame });
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
// A new prop's model, changed in its folder (/api/props/edit): every entry of it in the stage is made again from the new files
async function editPropModel(model, change, done) {
  let res;
  try { res = await api('/api/props/edit', Object.assign({ mod: modNow, model }, change)); }
  catch (e) { msg(`Couldn't change ${model}: ${cleanError(e).text}`, { level: 'err' }); showSel(); return; }
  const p = JSON.stringify(buildPatch());
  await loadPropModels(modNow);
  restoreTo(p);
  const m = res.prop || {};
  msg(done(m) + (m.error ? `, but ${m.error}` : ''), { level: m.error ? 'warn' : 'ok', detail: (res.lines || []).join('\n') || undefined });
}
function resizeProp(model, h) {
  if (!(h >= 0.1)) { msg('The height is a number of game units above 0', { level: 'warn' }); showSel(); return; }
  const n = propRecs.filter((r) => r.prop.model === model).length;
  editPropModel(model, { height: h }, (m) => {
    const b = m.bounds;
    return `${model} is ${b ? Math.round((b.max[2] - b.min[2]) * 10) / 10 : h} units high now` +
      (n > 1 ? ` (its ${n} entries in this stage)` : '') + (game.state === 'running' ? '; Test in game shows it' : '');
  });
}
$('pModel').onchange = async () => {
  if (sel && sel.rec.prop) {
    const model = sel.rec.info.model, title = $('pModel').value.trim();
    editPropModel(model, { title }, () => (title ? `Model ${model} renamed to "${title}"` : `Model ${model} goes by its folder's name`));
    return;
  }
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
// ------------------------------------------------------------------ Settings: appearance, the game's files, the developer fields
// Each path has a badge from the server (/api/paths, 400 ms after the last key): Found, Not found, Not set, and why. Browse is the
// pywebview window's file dialog (canBrowse); Advanced (the clone's runtime: ELF, working folder, dev disc) only in a clone.
const PATHS = { disc: 'sDisc', mods: 'sMods', game: 'sGame', elf: 'sElf', cwd: 'sCwd', iso: 'sIso' };
const ADVANCED = new Set(['elf', 'cwd', 'iso']);
let canBrowse = false, isClone = false;
const themePicked = () => document.querySelector('#sTheme input:checked')?.value || 'dark';
function openSettings(focusKey = '') {
  if (document.querySelector('dialog[open]')) return;
  for (const [k, id] of Object.entries(PATHS)) $(id).value = cfg[k] || '';
  const t = document.documentElement.dataset.theme;
  document.querySelectorAll('#sTheme input').forEach((r) => (r.checked = r.value === t));
  $('sAdv').hidden = !isClone;
  $('sAdv').open = isClone && ADVANCED.has(focusKey);
  document.querySelectorAll('#dlg .browse').forEach((b) => (b.hidden = !canBrowse));
  showPaths({});
  checkPaths(0);
  $('dlg').returnValue = '';
  $('dlg').showModal();
  (PATHS[focusKey] ? $(PATHS[focusKey]) : $('dlg')).focus();   // not the close button's focus ring
}
let pathTimer = 0, pathSeq = 0;
function checkPaths(delay = 400) {
  clearTimeout(pathTimer);
  return new Promise((done) => {
    pathTimer = setTimeout(async () => {
      const me = ++pathSeq, body = {};
      for (const [k, id] of Object.entries(PATHS)) body[k] = $(id).value;
      try { const r = await api('/api/paths', body); if (me === pathSeq) showPaths(r); } catch (e) { /* the badges stay as they are */ }
      done();
    }, delay);
  });
}
const STATE_IC = { ok: 'check-circle-outline', warn: 'alert-outline', err: 'alert-circle-outline' };
function showPaths(r) {
  for (const k of Object.keys(PATHS)) {
    const s = r[k], b = document.querySelector(`#dlg .pstate[data-for="${k}"]`), e = document.querySelector(`#dlg .perr[data-for="${k}"]`);
    b.className = 'pstate' + (s ? ' ' + s.state : '');
    b.textContent = '';
    b.title = s ? s.text || s.short : '';
    if (s && STATE_IC[s.state]) b.appendChild(icon(STATE_IC[s.state]));
    if (s) b.append(s.short);
    const why = s && (s.state === 'err' || s.state === 'warn') ? s.text : '';
    e.hidden = !why;
    e.className = 'perr' + (s && s.state === 'warn' ? ' warn' : '');
    e.textContent = why;
  }
  // a problem in a closed Advanced shows on its title
  const advBad = [...ADVANCED].some((k) => r[k] && r[k].state === 'err');
  $('sAdv').querySelector('summary').classList.toggle('bad', advBad);
}
for (const id of Object.values(PATHS)) $(id).addEventListener('input', () => checkPaths());
document.querySelectorAll('#dlg .browse').forEach((b) => (b.onclick = async () => {
  const k = b.dataset.browse;
  try {
    const r = await api('/api/browse', { kind: k, path: $(PATHS[k]).value });
    if (r.path) { $(PATHS[k]).value = r.path; checkPaths(0); }
  } catch (e) { toast(cleanError(e).text, 'err'); }
}));
$('settings').onclick = () => openSettings();
document.querySelectorAll('#sTheme input').forEach((r) => (r.onchange = () => applyTheme(r.value)));   // a preview; Cancel goes back
// a Cancel or close button of a dialog (type="button", so that Enter takes the main one)
document.querySelectorAll('dialog [data-close]').forEach((b) => b.addEventListener('click', () => b.closest('dialog').close('cancel')));
$('dlg').addEventListener('close', () => {
  if ($('dlg').returnValue !== 'ok') { applyTheme(cfg.theme || 'dark'); return; }
  const next = { theme: themePicked() };
  for (const [k, id] of Object.entries(PATHS)) next[k] = $(id).value.trim();
  // a new disc or mods folder opens the stage again; the other settings leave the scene as it is
  const reload = next.disc !== (cfg.disc || '') || next.mods !== (cfg.mods || '');
  const apply = async () => {
    try { init(await api('/api/config', next), reload || !stageName); }
    catch (e) { applyTheme(cfg.theme || 'dark'); msg(`Couldn't keep the settings: ${cleanError(e).text}`, { level: 'err' }); }
  };
  if (!reload || !dirty || !stageName) { apply(); return; }
  askUnsaved(`${title(stageName)} opens again with the new ${next.disc !== (cfg.disc || '') ? 'game disc' : 'mods folder'}, and has ` +
             `${changesText(unsavedCount())}.`, 'Save and apply', async (v) => {
    if (v === 'cancel' || (v === 'save' && !(await post('save')))) { applyTheme(cfg.theme || 'dark'); msg('Settings not changed', { level: 'muted' }); return; }
    apply();
  });
});
// a control that takes the keys itself: text and number fields and the pickers (not checkboxes, radios or sliders)
const typing = (t) => !!t?.matches?.('input:not([type=checkbox]):not([type=radio]):not([type=range]), textarea, select');
// fly keys by their place on the keyboard (e.code), so WASD works on AZERTY too; the other letters by what they say
const FLY_CODES = { KeyW: 'w', KeyA: 'a', KeyS: 's', KeyD: 'd', KeyQ: 'q', KeyE: 'e' };
window.addEventListener('keyup', (e) => { if (FLY_CODES[e.code]) fly.keys.delete(FLY_CODES[e.code]); });
window.addEventListener('keydown', (e) => {
  const k = (e.key || '').toLowerCase(), ctrl = e.ctrlKey || e.metaKey, field = typing(e.target);
  if (ctrl && !e.altKey && k === 'p') { e.preventDefault(); openPalette(); return; }   // the browser's print dialog never opens
  if (document.querySelector('dialog[open]')) {   // a dialog has the keys (Esc closes it, the selection stays)
    if (ctrl && k === 's') e.preventDefault();
    return;
  }
  const text = field && !e.target.matches('select');
  if (e.key === 'F1') { e.preventDefault(); showKeys(); return; }   // the browser's help never opens
  // Save, Undo and Redo before the focus test: Ctrl+S never opens the browser's "save page"; in a text field Undo is the field's
  if (ctrl && !e.altKey && k === 's') {
    e.preventDefault();
    const t = e.target;
    if (field) t.blur();               // the field's change first (it may be an edit)
    post('save');
    return;
  }
  if (ctrl && !e.altKey && (k === 'z' || k === 'y')) {
    if (text) return;
    e.preventDefault();
    if (k === 'y' || e.shiftKey) redo(); else undo();
    return;
  }
  if (field) return;
  if (k === '?' && !ctrl && !e.altKey) { e.preventDefault(); showKeys(); return; }
  if (k === 'escape' && !$('layersMenu').hidden) { showLayers(false); return; }   // the menu first, the selection stays
  if (k === 'escape' && !$('propMenu').hidden) { showPropMenu(false); return; }
  if (ctrl && k === 'd') { e.preventDefault(); duplicate(); }
  else if (e.ctrlKey && k === 'a') { e.preventDefault(); if (editMode === 'objects') selectMany(listed()); }
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
  else if (k === 'f') focusSelection();
  else if (k === 'l') setLook(!gameLook);
  else if (k === 'p') { if (editMode === 'objects') setPeds(!pedsOn); }
  else if (k === 'c') setColl(!collOn);
  else if (k === '1') setMode('objects');
  else if (k === '2') setMode('peds');
  else if (k === 'escape') select(null);
});
window.addEventListener('beforeunload', (e) => { if (dirty) { e.preventDefault(); e.returnValue = ''; } });

// /api/init or /api/config's reply. load: open the stage (again); Settings without a new disc or mods folder leaves the scene alone
function init(r, load = true) {
  cfg = r.config;
  canBrowse = !!r.canBrowse;
  isClone = !!r.clone;
  applyTheme(cfg.theme || 'dark');
  modList = r.mods || [];
  if (!stageName) modNow = (cfg.mod || '').trim();   // the first init; Settings keeps the mod in use
  if (badModName(modNow)) modNow = '';
  fillMods();
  const s = $('stage');
  const prev = stageName || (new URLSearchParams(location.search).get('stage') || 'CENTRAL').toUpperCase();   // ?stage=METRO
  // "Midtown (CENTRAL)", but "UFO" once when the title is the file's name
  s.innerHTML = r.stages.map((x) => `<option value="${x.name}">${x.title}${x.custom ? ` (custom, on ${x.base})` : x.title.toUpperCase() === x.name ? '' : ` (${x.name})`}</option>`).join('');
  stageTitles = Object.fromEntries(r.stages.map((x) => [x.name, x.title]));
  stageBases = Object.fromEntries(r.stages.filter((x) => x.custom).map((x) => [x.name, x.base]));
  game.stage = r.gameStage || '';
  setRunning(!!r.gameRunning);
  if (r.discError || !r.stages.length) {
    msg(r.discError || 'No stages on this disc. Is it War of the Monsters (NTSC-U)?',
        { level: 'err', toast: ['Open settings', () => openSettings('disc')], detail: cfg.disc || '' });
    return;
  }
  s.value = r.stages.some((x) => x.name === prev) ? prev : r.stages[0].name;
  if (!load && s.value === stageName) { refreshList(); showWinner(); return; }
  loadStage(s.value);
}

setTool('translate');
orbit.enabled = false;
setView(fly.on);
applyPedsView();
showMode();
// the editor program quits a few seconds after its page closes (a reload asks again at once and keeps it)
window.addEventListener('pagehide', () => { try { navigator.sendBeacon('/api/bye', '{}'); } catch (e) { /* ignore */ } });
if (new URLSearchParams(location.search).has('debug')) {
  window.wotmDebug = { camera, fly, flyStep, setView, select, selectMany, items, listed, dropToGround, nudge, duplicate, remove,
                       undo, buildPatch, posOf, setLook, scene, renderer, gsLights, peds, selectPed, setPeds, addPed, pedFromInputs,
                       redo, resetAll, applyPatch, frameAll, resetSel, gameLine, post, setRunning, applyTheme, refreshList, gameBox,
                       msg, toast, openOutput, closeOutput, showStatus, setGameState, readCheck, gameResult,
                       setMode, turnBy, showSummary, secOf, SECTIONS, get mode() { return modeNow(); },
                       refreshPedList, setPedLive, slideInput, slideDone, showPed, pedOfEntry, openHudDialog, chooseMod,
                       get pedsLayer() { return pedsOn; }, coll, setColl, refreshColl, get collOn() { return collOn; },
                       ask, askUnsaved, unsavedCount, switchMod, setMod, moveTo, fillMods, pickMod, openSettings, checkPaths, showPaths,
                       showWinner, winnerOf, checkFromServer, init, addProp, showPropMenu, openImport, doImport, ipRead, loadPropModels, openTerrainDlg,
                       get propRecs() { return propRecs; }, get propModels() { return propModels; },
                       get stageHave() { return stageHave; }, set stageHave(v) { stageHave = v; },
                       get modList() { return modList; }, set modList(v) { modList = v; }, get savedJson() { return savedJson; },
                       gizmo, boxes, hoverBox, marks, hover, hoverPick: pickAt, hoverStep, syncMarks, syncBoxes,
                       boxCenter: (o) => new THREE.Box3().setFromObject(o).getCenter(new THREE.Vector3()), setGrid, setTool, setSnap, showKeys, showHint,
                       focusSelection, HINTS, get hintKey() { return hint.key; }, get hoverCount() { return hover.n; },
                       get gridMinor() { return gridMinor; }, get gridMajor() { return gridMajor; },
                       get ui() { return ui; }, get outTab() { return outTab; },
                       get history() { return history; }, get future() { return future; }, get dirty() { return dirty; },
                       get modNow() { return modNow; }, get keep() { return keep; }, get game() { return game; },
                       get gizmoObject() { return gizmo.object || null; }, get gizmoMode() { return gizmo.getMode(); },
                       get pedSel() { return pedSel; }, get group() { return group; }, get sel() { return sel; },
                       get env() { return env; }, get skyRoot() { return skyRoot; }, get skyViews() { return skyViews; }, get stageRoot() { return stageRoot; } };
}
loop();
if (new URLSearchParams(location.search).has('bake')) bakeOriginals();
api('/api/init').then(init).catch((e) => msg(`Couldn't reach the editor program: ${cleanError(e).text}`, { level: 'err' }));
