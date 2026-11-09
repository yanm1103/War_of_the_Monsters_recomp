// Test of the sky dome and cloud layer selects (the Stage tab's Environment) in the level editor's page: the two are independent in the
// state, the patch, the view, Undo and the saved file; the view draws a model in the stock part's slot, whichever was picked last;
// a sky model made of the stage's own picture shows it; the selects list the mod's models as soon as they change and explain an
// empty list; a stage with no replaceable part says so.
//
// Needs the disc configured, Node 22 and Edge or Chrome. Nothing of yours is touched: own settings file and a temporary mods folder
// (the models are made by make_sky_glb.py and imported through the editor's API), free ports, Edge headless (software rendering).
// Run: node tools/level_editor/test_sky.mjs            (--keep leaves the temporary folder; --shot <file> saves the Stage tab with a sky picked, --shot-empty <file> with no models)
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..');
const freePort = () => new Promise((res) => { const s = net.createServer(); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => res(p)); }); });
const PORT = await freePort(), DBG = await freePort();
const killTree = (c) => {
  if (!c || !c.pid) return;
  try { if (process.platform === 'win32') spawnSync('taskkill', ['/PID', String(c.pid), '/T', '/F'], { stdio: 'ignore' }); else c.kill(); } catch { /* gone */ }
};
const args = process.argv.slice(2);
const shotFile = args.includes('--shot') ? args[args.indexOf('--shot') + 1] : '';
const shotEmpty = args.includes('--shot-empty') ? args[args.indexOf('--shot-empty') + 1] : '';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const py = process.platform === 'win32' ? 'python' : 'python3';

const tmp = fs.mkdtempSync(path.join(tmpdir(), 'wotm_sky_'));
const mods = path.join(tmp, 'mods');
for (const m of ['empty', 'skymod']) fs.mkdirSync(path.join(mods, m, 'levels'), { recursive: true });
const cfgFile = path.join(tmp, 'settings.json');
fs.writeFileSync(cfgFile, JSON.stringify({ mods, mod: 'empty', game: '', elf: '', cwd: '', theme: 'dark' }));
// the sky models: A is made of the stage's own picture 240, B and N of a plain color
const glb = {};
for (const [name, half, mat] of [['A', 30, 'tex240'], ['B', 25, 'plain'], ['N', 20, 'plain']]) {
  const f = path.join(tmp, name + '.glb');
  const r = spawnSync(py, [path.join(HERE, 'make_sky_glb.py'), f, String(half), mat], { encoding: 'utf8' });
  if (r.status !== 0) { console.error('make_sky_glb.py failed: ' + r.stderr); process.exit(2); }
  glb[name] = fs.readFileSync(f).toString('base64');
}

const serverLog = path.join(tmp, 'server.log');
const serverOut = fs.openSync(serverLog, 'w');
const server = spawn(py, [path.join(HERE, 'editor.py'), '--serve', '--port', String(PORT)],
  { cwd: REPO, stdio: ['ignore', serverOut, serverOut], env: { ...process.env, WOTM_LEVEL_EDITOR_CONFIG: cfgFile, WOTM_LEVEL_EDITOR_NAMES: path.join(tmp, 'names.json') } });
const edgePaths = ['C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
  'C:/Program Files/Google/Chrome/Application/chrome.exe', '/usr/bin/microsoft-edge', '/usr/bin/google-chrome', '/usr/bin/chromium'];
const browser = edgePaths.find((p) => fs.existsSync(p));
if (!browser) { console.error('no Edge or Chrome found'); killTree(server); process.exit(2); }
const edge = spawn(browser, ['--headless=new', `--remote-debugging-port=${DBG}`, '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
  '--disable-gpu-sandbox', `--user-data-dir=${path.join(tmp, 'profile')}`, '--window-size=1600,900', 'about:blank'], { stdio: 'ignore' });
let failures = 0;
const done = (code) => {
  killTree(server); killTree(edge);
  setTimeout(() => { if (!args.includes('--keep')) try { fs.rmSync(tmp, { recursive: true, force: true, maxRetries: 10, retryDelay: 300 }); } catch { /* Edge still holds its profile: the exit code comes from the checks */ } process.exit(code); }, 500);
};
process.on('uncaughtException', (e) => { console.error(e); done(1); });

async function json(url, tries = 80) {
  for (let i = 0; i < tries; i++) { try { return await (await fetch(url)).json(); } catch { await sleep(500); } }
  throw new Error('no answer from ' + url);
}
await json(`http://127.0.0.1:${PORT}/api/init`);
const page = (await json(`http://127.0.0.1:${DBG}/json`)).find((t) => t.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((r) => (ws.onopen = r));
let nextId = 0;
const pending = new Map();
const pageErrors = [];
ws.onmessage = (m) => {
  const d = JSON.parse(m.data);
  if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); return; }
  if (d.method === 'Page.javascriptDialogOpening') { send('Page.handleJavaScriptDialog', { accept: true }); return; }
  if (d.method === 'Log.entryAdded' && d.params.entry.level === 'error' && !/favicon/.test(d.params.entry.url || '')) pageErrors.push('log: ' + d.params.entry.text + ' ' + (d.params.entry.url || ''));
  if (d.method === 'Runtime.consoleAPICalled' && d.params.type === 'error') pageErrors.push('console: ' + JSON.stringify(d.params.args.map((a) => a.value || a.description)).slice(0, 300));
  if (d.method === 'Runtime.exceptionThrown') pageErrors.push(JSON.stringify(d.params.exceptionDetails).slice(0, 300));
};
const send = (method, params = {}) => new Promise((r) => { const i = ++nextId; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
async function ev(expr) {
  const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true, userGesture: true });
  if (r.result.exceptionDetails) throw new Error(JSON.stringify(r.result.exceptionDetails).slice(0, 400));
  return r.result.result.value;
}
async function until(expr, tries = 40, gap = 250) {
  for (let i = 0; i < tries; i++) { if (await ev(expr).catch(() => false)) return true; await sleep(gap); }
  return false;
}
async function check(name, fn) {
  try { await fn(); console.log('ok    ' + name); } catch (e) { failures++; console.log('FAIL  ' + name + ': ' + String(e.message).slice(0, 400)); }
}
const must = (cond, why) => { if (!cond) throw new Error(why); };
const D = 'window.wotmDebug';
const importModel = async (mod, stage, name, data) => {
  const r = await fetch(`http://127.0.0.1:${PORT}/api/props/import`, { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ mod, stage, name, data, units: 'blender', height: '', light: 'auto', title: name, replace: true }) });
  const j = await r.json();
  if (!r.ok) throw new Error(`import ${name}: ${j.error}`);
  return j;
};
// the select the way a person uses it: set its value and let it fire its change
const pick = (id, v) => ev(`(() => { const s = document.getElementById('${id}'); s.value = ${JSON.stringify(v)}; s.dispatchEvent(new Event('change', { bubbles: true })); })()`);
const patchSky = () => ev(`JSON.stringify((${D}.buildPatch().props || []).filter((e) => e.sky))`).then(JSON.parse);
const optionsOf = (id) => ev(`[...document.getElementById('${id}').options].map((o) => o.value + (o.disabled ? '!' : ''))`);
// what the view shows of one kind: the stock part (how many meshes, how many drawn) and the model's view (draw order of its meshes)
const sky = (kind) => ev(`(() => {
  const d = ${D}, st = d.skyStock('${kind}'), v = d.skyViews['${kind}'];
  const orders = []; if (v) v.view.traverse((o) => { if (o.isMesh) orders.push(o.renderOrder); });
  return { stock: st.length, stockShown: st.filter((o) => o.visible).length, stockFirst: st.length ? Math.min(...st.map((o) => o.renderOrder)) : null,
           view: !!v, orders, shown: v ? v.view.visible : null };
})()`);
const frames = async () => { await sleep(150); };

await send('Page.enable');
await send('Runtime.enable');
await send('Log.enable');
await send('Page.navigate', { url: `http://127.0.0.1:${PORT}/?debug` });
if (!await until(`!!(${D} && ${D}.stageRoot && ${D}.items().length > 20)`, 120, 500)) {
  console.log('FAIL  the stage did not load (disc configured?)', pageErrors.slice(0, 3).join(' | '), fs.readFileSync(path.join(tmp, 'server.log'), 'utf8').slice(-800));
  done(1); await sleep(60000);
}
const loadStage = async (name) => {
  await ev(`${D}.loadStage(${JSON.stringify(name)})`);
  must(await until(`${D}.items().length > 5 && !!${D}.skyRoot`, 120, 500), `${name} did not load`);
};
await loadStage('CENTRAL');
await ev(`document.getElementById('tabStage').click()`);
await ev(`${D}.setLook(false)`);

await check('/api/stage names the sky objects the game replaces', async () => {
  const r = await json(`http://127.0.0.1:${PORT}/api/stage?name=CENTRAL&mod=empty`);
  must(r.sky && r.sky.dome.length > 0 && r.sky.clouds.length > 0, 'CENTRAL sky: ' + JSON.stringify(r.sky));
  must(r.sky.dome.every((a) => /^[0-9a-f]+$/.test(a)), 'addresses are not hex: ' + JSON.stringify(r.sky.dome));
  const s = await ev(`({ parts: ${D}.skyParts, dome: ${D}.skyStock('dome').length, clouds: ${D}.skyStock('clouds').length })`);
  must(s.dome > 0 && s.clouds > 0, 'the view finds no stock part: ' + JSON.stringify(s));
});

await check('an empty mod: the selects say so and offer the import', async () => {
  must(await ev(`${D}.modNow`) === 'empty', 'mod is ' + await ev(`${D}.modNow`));
  const o = await optionsOf('pmSky');
  must(o.length === 2 && o[0] === '' && o[1] === '@import', 'options: ' + o);
  const note = await ev(`(() => { const n = document.getElementById('pmSkyNote'); return n.hidden ? null : n.textContent; })()`);
  must(note && /^No models for /.test(note) && /inward/.test(note), 'note: ' + note);
  await pick('pmSky', '@import');
  must(await until(`document.getElementById('propDlg').open`, 20), 'the Import entry did not open the import dialog');
  must(await ev(`document.getElementById('ipName').value`) === 'SKY', 'name: ' + await ev(`document.getElementById('ipName').value`));
  must(/sky dome/.test(await ev(`document.getElementById('ipText').textContent`)), 'the dialog does not say what the file becomes');
  must(await ev(`document.getElementById('pmSky').value`) === '', 'the select stayed on Import');
  await ev(`document.getElementById('propDlg').close()`);
  if (shotEmpty) {
    await sleep(500);
    fs.mkdirSync(path.dirname(shotEmpty), { recursive: true });
    fs.writeFileSync(shotEmpty, Buffer.from((await send('Page.captureScreenshot', { format: 'png' })).result.data, 'base64'));
  }
});

await check('the selects list a mod\'s models right after moveTo, without reopening the tab', async () => {
  await importModel('skymod', 'CENTRAL', 'A', glb.A);
  await importModel('skymod', 'CENTRAL', 'B', glb.B);
  await ev(`${D}.moveTo('skymod', false)`);
  must(await until(`[...document.getElementById('pmSky').options].some((o) => o.value === 'A')`, 40), 'dome options: ' + await optionsOf('pmSky'));
  must(await until(`[...document.getElementById('pmClouds').options].some((o) => o.value === 'B')`, 20), 'cloud options: ' + await optionsOf('pmClouds'));
  must(await ev(`document.getElementById('pmSkyNote').hidden`), 'the empty note stays');
  await ev(`${D}.setLook(false)`);
});

await check('picking the dome leaves the clouds alone (state, patch, view, game look)', async () => {
  await pick('pmSky', 'A');
  await frames();
  const p = await patchSky();
  must(JSON.stringify(p) === JSON.stringify([{ model: 'A', sky: true }]), 'patch: ' + JSON.stringify(p));
  const d = await sky('dome'), c = await sky('clouds');
  must(d.view && d.stockShown === 0, `dome: ${JSON.stringify(d)}`);
  must(!c.view && c.stock > 0 && c.stockShown === c.stock, `clouds: ${JSON.stringify(c)}`);
  must(await ev(`document.getElementById('look').checked`), 'Game look did not turn on');
  must(await ev(`${D}.skyViews.dome.view.visible`), 'the dome view is hidden');
});

await check('picking the clouds leaves the dome alone, whatever was picked first', async () => {
  await pick('pmClouds', 'B');
  await frames();
  const p = await patchSky();
  must(JSON.stringify(p) === JSON.stringify([{ model: 'A', sky: true }, { model: 'B', sky: 'clouds' }]), 'patch: ' + JSON.stringify(p));
  const d = await sky('dome'), c = await sky('clouds');
  must(d.view && c.view && d.stockShown === 0 && c.stockShown === 0, `views: ${JSON.stringify({ d, c })}`);
  // each model takes the draw slot of the part it replaces, as the game's tree order has it
  must(d.orders.every((o) => o >= d.stockFirst && o < d.stockFirst + 1), `dome orders ${d.orders} vs stock ${d.stockFirst}`);
  must(c.orders.every((o) => o >= c.stockFirst && o < c.stockFirst + 1), `cloud orders ${c.orders} vs stock ${c.stockFirst}`);
  must(d.stockFirst !== c.stockFirst, 'the dome and the clouds have the same slot');
  globalThis.firstOrder = { d: d.orders, c: c.orders };
  // the other way round: the same slots
  await ev(`${D}.undo(); ${D}.undo();`);
  await frames();
  must((await patchSky()).length === 0 && !(await sky('dome')).view && !(await sky('clouds')).view, 'undo x2 left a view or an entry');
  await pick('pmClouds', 'B'); await pick('pmSky', 'A');
  await frames();
  const d2 = await sky('dome'), c2 = await sky('clouds');
  must(JSON.stringify(d2.orders) === JSON.stringify(globalThis.firstOrder.d) && JSON.stringify(c2.orders) === JSON.stringify(globalThis.firstOrder.c),
    `the draw order depends on the pick order: ${JSON.stringify({ d2: d2.orders, c2: c2.orders, first: globalThis.firstOrder })}`);
});

await check('removing one leaves the other', async () => {
  await pick('pmSky', '');
  await frames();
  must(JSON.stringify(await patchSky()) === JSON.stringify([{ model: 'B', sky: 'clouds' }]), 'patch: ' + JSON.stringify(await patchSky()));
  let d = await sky('dome'), c = await sky('clouds');
  must(!d.view && d.stockShown === d.stock, `dome after removal: ${JSON.stringify(d)}`);
  must(c.view && c.stockShown === 0, `clouds after the dome's removal: ${JSON.stringify(c)}`);
  await pick('pmSky', 'A');
  await pick('pmClouds', '');
  await frames();
  must(JSON.stringify(await patchSky()) === JSON.stringify([{ model: 'A', sky: true }]), 'patch: ' + JSON.stringify(await patchSky()));
  d = await sky('dome'); c = await sky('clouds');
  must(d.view && d.stockShown === 0, `dome after the clouds' removal: ${JSON.stringify(d)}`);
  must(!c.view && c.stockShown === c.stock, `clouds after removal: ${JSON.stringify(c)}`);
  await pick('pmClouds', 'B');
});

await check('a model is not both the dome and the clouds', async () => {
  const n = await ev(`${D}.history.length`);
  await pick('pmClouds', 'A');
  must(await ev(`${D}.history.length`) === n, 'a refused pick made a history step');
  must(await ev(`document.getElementById('pmClouds').value`) === 'B', 'the select did not go back');
  const o = await optionsOf('pmClouds');
  must(o.includes('A!'), 'A is not listed as taken: ' + o);
  must(JSON.stringify((await patchSky()).map((e) => e.model)) === JSON.stringify(['A', 'B']), 'patch: ' + JSON.stringify(await patchSky()));
});

await check('Undo and Redo move one part at a time', async () => {
  await pick('pmSky', ''); await pick('pmClouds', ''); await pick('pmSky', 'A'); await pick('pmClouds', 'B'); await frames();
  await ev(`${D}.undo()`); await frames();            // the clouds back to none
  must(JSON.stringify((await patchSky()).map((e) => e.model)) === JSON.stringify(['A']), 'undo 1: ' + JSON.stringify(await patchSky()));
  must((await sky('dome')).view && !(await sky('clouds')).view, 'undo 1 views');
  await ev(`${D}.undo()`); await frames();
  must((await patchSky()).length === 0 && !(await sky('dome')).view, 'undo 2');
  must((await sky('dome')).stockShown === (await sky('dome')).stock, 'the stock dome is not back');
  await ev(`${D}.redo(); ${D}.redo();`); await frames();
  must(JSON.stringify((await patchSky()).map((e) => e.model)) === JSON.stringify(['A', 'B']), 'redo x2: ' + JSON.stringify(await patchSky()));
  must((await sky('dome')).view && (await sky('clouds')).view, 'redo views');
  must(await ev(`document.getElementById('pmSky').value`) === 'A' && await ev(`document.getElementById('pmClouds').value`) === 'B', 'the selects do not follow');
});

await check('the saved file keeps each part apart, and a reload shows them', async () => {
  must(await ev(`${D}.post('save')`) !== false, 'Save failed');
  const f = path.join(mods, 'skymod', 'levels', 'CENTRAL.json');
  const j = JSON.parse(fs.readFileSync(f, 'utf8'));
  const s = (j.props || []).filter((e) => e.sky);
  must(JSON.stringify(s) === JSON.stringify([{ model: 'A', sky: true }, { model: 'B', sky: 'clouds' }]), 'file: ' + JSON.stringify(s));
  await loadStage('CENTRAL');
  must(await until(`!!${D}.skyViews.dome && !!${D}.skyViews.clouds`, 40), 'the views are not back after loading the stage');
  must(await ev(`document.getElementById('pmSky').value`) === 'A', 'select after reload');
});

await check('a sky model made of the stage\'s own picture shows it', async () => {
  const s = await ev(`(() => {
    const d = ${D}, names = []; d.skyRoot.traverse((o) => { if (o.isMesh) for (const m of [].concat(o.material)) { const t = /^tex(\\d+)$/.exec(m.name || ''); if (t) names.push(+t[1]); } });
    return { names: [...new Set(names)], have: [...d.stageTex.keys()] };
  })()`);
  must(s.names.length > 0 && s.names.every((n) => s.have.includes(n)), 'the sky\'s pictures are not all in stageTex: ' + JSON.stringify(s));
  const m = await ev(`(() => { const out = []; ${D}.skyViews.dome.view.traverse((o) => { if (o.isMesh) for (const m of [].concat(o.material)) out.push({ name: m.name, map: !!m.map }); }); return out; })()`);
  must(m.length > 0 && m.every((x) => x.name === 'tex240' && x.map), 'the dome model\'s material has no picture: ' + JSON.stringify(m));
  const b = await ev(`(() => { const out = []; ${D}.skyViews.clouds.view.traverse((o) => { if (o.isMesh) for (const m of [].concat(o.material)) out.push(!!m.map); }); return out; })()`);
  must(b.length > 0 && b.every((x) => !x), 'a plain model got a picture: ' + b);
});

await check('replacing a model\'s file changes the view', async () => {
  const before = await ev(`${D}.skyViews.dome.from === ${D}.propModels.get('A').scene`);
  must(before, 'the dome view is not A\'s scene');
  await ev(`(async () => {
    const d = ${D};
    d.openImport();
    await new Promise((r) => setTimeout(r, 100));
    const bin = atob(${JSON.stringify(glb.B)}); const u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
    d.ipRead(new File([u], 'A.glb'));
    await new Promise((r) => setTimeout(r, 200));
    document.getElementById('ipName').value = 'A';
    await d.doImport(true);
  })()`);
  must(await until(`${D}.skyViews.dome && ${D}.skyViews.dome.from === ${D}.propModels.get('A').scene`, 40), 'the dome view is not the new A');
  const m = await ev(`(() => { const out = []; ${D}.skyViews.dome.view.traverse((o) => { if (o.isMesh) for (const m of [].concat(o.material)) out.push(!!m.map); }); return out; })()`);
  must(m.length > 0 && m.every((x) => !x), 'the view still has the old model\'s picture: ' + m);
  must(await ev(`document.getElementById('propDlg').open === false`), 'the import dialog stayed open');
});

await check('Import from a select makes the new model the dome or the clouds, and places no prop', async () => {
  const props = await ev(`${D}.propRecs.length`);
  await pick('pmClouds', '');
  await ev(`(async () => {
    const d = ${D};
    await d.openImport('clouds');
    const bin = atob(${JSON.stringify(glb.N)}); const u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
    d.ipRead(new File([u], 'N.glb'));
    await new Promise((r) => setTimeout(r, 200));
    document.getElementById('ipName').value = 'N';
    await d.doImport(false);
  })()`);
  must(await until(`${D}.buildPatch().props && ${D}.buildPatch().props.some((e) => e.sky === 'clouds' && e.model === 'N')`, 60), 'the clouds are not N: ' + JSON.stringify(await patchSky()));
  must(await ev(`${D}.propRecs.length`) === props, 'a prop was placed in the stage');
  must((await patchSky()).length === 2, 'patch: ' + JSON.stringify(await patchSky()));
});

if (shotFile) {
  await ev(`document.getElementById('tabStage').click(); ${D}.setLook(true)`);
  await sleep(1500);
  fs.mkdirSync(path.dirname(shotFile), { recursive: true });
  fs.writeFileSync(shotFile, Buffer.from((await send('Page.captureScreenshot', { format: 'png' })).result.data, 'base64'));
}
await check("a replaced picture shows in the view, and the disc's comes back when it is removed", async () => {
  await loadStage('CENTRAL');
  const px = () => ev(`(() => { const t = ${D}.stageTex.get(240); if (!t) return null; const im = t.image, c = document.createElement('canvas'); c.width = im.width; c.height = im.height;
    const g = c.getContext('2d'); g.drawImage(im, 0, 0); return [...g.getImageData(im.width >> 1, im.height >> 1, 1, 1).data]; })()`);
  const before = await px();
  must(before && before[3] > 0, 'no picture 240 in the view: ' + before);
  const png = await ev(`(() => { const c = document.createElement('canvas'); c.width = c.height = 64; const g = c.getContext('2d'); g.fillStyle = '#ff0000'; g.fillRect(0, 0, 64, 64); return c.toDataURL('image/png').split(',')[1]; })()`);
  const post = async (p, b) => {
    const r = await fetch(`http://127.0.0.1:${PORT}/api/${p}`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(b) });
    if (!r.ok) throw new Error(p + ': ' + (await r.text()));
  };
  await post('tex/replace', { stage: 'CENTRAL', mod: 'skymod', png: 'central_t0240.png', data: png });
  await ev(`${D}.moveTo('skymod', false)`);
  let after = null;
  for (let i = 0; i < 40 && !(after && after[0] > 200 && after[1] < 50); i++) { await sleep(250); after = await px(); }
  must(after && after[0] > 200 && after[1] < 50, 'the view still draws the disc picture: ' + after + ' (was ' + before + ')');
  await post('tex/remove', { stage: 'CENTRAL', mod: 'skymod', png: 'central_t0240.png' });
  await ev(`${D}.syncPictures()`);
  let back = null;
  for (let i = 0; i < 40 && !(back && back.join() === before.join()); i++) { await sleep(250); back = await px(); }
  must(back && back.join() === before.join(), 'the disc picture did not come back: ' + back + ' (was ' + before + ')');
});


await check('a stage with no replaceable sky says so and disables the selects', async () => {
  await importModel('skymod', 'CANYON2', 'A', glb.B);
  await loadStage('CANYON2');
  must(await until(`${D}.skyParts !== null`, 20), 'no skyParts');
  const s = await ev(`({ parts: ${D}.skyParts, dis: [document.getElementById('pmSky').disabled, document.getElementById('pmClouds').disabled],
    text: [document.getElementById('pmSky').selectedOptions[0].textContent, document.getElementById('pmClouds').selectedOptions[0].textContent],
    note: document.getElementById('pmSkyNote').textContent })`);
  must(s.parts.dome.length === 0 && s.parts.clouds.length === 0, 'CANYON2 parts: ' + JSON.stringify(s.parts));
  must(s.dis[0] && s.dis[1], 'selects not disabled: ' + JSON.stringify(s));
  must(/no sky dome/.test(s.text[0]) && /no cloud layer/.test(s.text[1]), 'texts: ' + JSON.stringify(s.text));
  must(/can't be replaced/.test(s.note), 'note: ' + s.note);
});

await check('a stage that keeps some layers of clouds hides only the one the game replaces', async () => {
  await importModel('skymod', 'CAPITOL', 'A', glb.B);
  await loadStage('CAPITOL');
  const before = await ev(`(() => { let all = 0; ${D}.skyRoot.traverse((o) => { if (o.isMesh && ${D}.skyTag(o).clouds) all++; }); return { all, parts: ${D}.skyParts.clouds.length, stock: ${D}.skyStock('clouds').length }; })()`);
  must(before.parts === 1 && before.stock > 0 && before.all > before.stock, 'CAPITOL: ' + JSON.stringify(before));
  await ev(`document.getElementById('tabStage').click(); ${D}.setLook(false)`);
  await pick('pmClouds', 'A');
  await frames();
  const after = await ev(`(() => { let shown = 0; ${D}.skyRoot.traverse((o) => { if (o.isMesh && ${D}.skyTag(o).clouds && o.visible) shown++; }); return shown; })()`);
  must(after === before.all - before.stock, `cloud meshes left drawn: ${after}, expected ${before.all - before.stock} (${JSON.stringify(before)})`);
  must((await sky('clouds')).view, 'no cloud view');
});

console.log(pageErrors.length ? 'page errors:\n  ' + pageErrors.slice(0, 8).join('\n  ') : 'no page errors');
if (pageErrors.length) failures++;
console.log(failures ? `${failures} FAILED` : 'all passed');
done(failures ? 1 : 0);
await sleep(30000);
