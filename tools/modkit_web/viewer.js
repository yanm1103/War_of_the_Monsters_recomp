// The 3D preview of the kit page: one renderer, moved into whatever box shows a model (a costume, a skin, a custom monster, a
// 3D file of the work folder). The model stands in a 3/4 view framed on its box; drag turns it, the wheel zooms.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

let renderer = null, scene, camera, controls, current = null, box = null, token = 0, ro = null;
const loader = new GLTFLoader();

function setup() {
  renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, preserveDrawingBuffer: true });
  renderer.setPixelRatio(window.devicePixelRatio);
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  scene = new THREE.Scene();
  scene.add(new THREE.HemisphereLight(0xffffff, 0x404050, 2.2));
  const sun = new THREE.DirectionalLight(0xffffff, 1.4);
  sun.position.set(-1, 2, 1.5);
  scene.add(sun);
  camera = new THREE.PerspectiveCamera(35, 1, 0.1, 100000);
  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  ro = new ResizeObserver(size);
  renderer.setAnimationLoop(() => { if (box && box.isConnected) { controls.update(); renderer.render(scene, camera); } });
}
function size() {
  if (!box) return;
  const w = Math.max(10, box.clientWidth), h = Math.max(10, box.clientHeight);
  renderer.setSize(w, h);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}
function frame(obj) {
  const b = new THREE.Box3().setFromObject(obj);
  const c = b.getCenter(new THREE.Vector3()), s = b.getSize(new THREE.Vector3());
  obj.position.sub(new THREE.Vector3(c.x, b.min.y, c.z));   // on the ground, in the middle
  const H = Math.max(s.y, s.x * 0.6, s.z * 0.6, 1e-3);
  const yaw = THREE.MathUtils.degToRad(35), d = H * 2.3;
  controls.target.set(0, s.y * 0.5, 0);
  camera.position.set(Math.sin(yaw) * d, s.y * 0.6, Math.cos(yaw) * d);
  camera.near = H / 100;
  camera.far = H * 100;
  camera.updateProjectionMatrix();
  controls.update();
}

// shows the .glb at url in el (replacing what it had); resolves when it is drawn, rejects with the server's message
export async function showModel(el, url) {
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
  if (current) { scene.remove(current); current.traverse(o => { if (o.geometry) o.geometry.dispose(); }); current = null; }
  try {
    const r = await fetch(url);
    if (!r.ok) throw new Error((await r.text()) || r.statusText);
    const g = await loader.parseAsync(await r.arrayBuffer(), '');
    if (my !== token) return;
    current = g.scene;
    scene.add(current);
    frame(current);
    msg.remove();
  } catch (e) {
    if (my === token) msg.textContent = 'No 3D preview: ' + (e.message || e);
  }
}
export function modelShown() { return !!current; }
window.kitViewer = { shown: () => !!current && !!box && box.isConnected };
