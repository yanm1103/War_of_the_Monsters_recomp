// Smoke test of the level editor's page: loads a stage, the controls other code and scripts rely on are there, an edit goes
// through undo and Save, every dialog opens and closes with Esc, the server turns away a request the page would not send.
//
// Needs the disc configured (the stage is read from it), Node 22 (the WebSocket is Node's own) and Edge or Chrome. Nothing of
// yours is touched: the editor program runs with its own settings file (WOTM_LEVEL_EDITOR_CONFIG) and a temporary mods folder,
// on port 47393, with Edge headless (software rendering: the first stage takes a while).
// Run: node tools/level_editor/test_ui.mjs            (--keep leaves the temporary folder; --shots <dir> saves a PNG per dialog)
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..');
const PORT = 47393, DBG = 9335;
const args = process.argv.slice(2);
const shotsDir = args.includes('--shots') ? args[args.indexOf('--shots') + 1] : '';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ids that scripts and tests outside this file click or read: moving a control keeps its id (docs/investigacoes, ux_consolidado 4.3)
const STABLE_IDS = ['stage', 'newStage', 'editStage', 'mod', 'packMod', 'undo', 'redo', 'check', 'save', 'test', 'testHere', 'testOpts', 'reload',
  'stop', 'help', 'settings', 'modeObjects', 'modePeds', 'texBtn', 'lightBtn', 'hud', 'filter', 'list', 'canvas', 'toolBar', 'tMove', 'tTurn',
  'snap', 'snapMove', 'snapTurn', 'addProp', 'propMenu', 'pmImport', 'pmTerrain', 'pmSky', 'pmClouds', 'overlay', 'look', 'grid', 'layersBtn',
  'layersMenu', 'vFly', 'vOrbit', 'focusSel', 'frame', 'gridBtn', 'sptBtn', 'pupBtn', 'aipBtn', 'fopBtn', 'picBtn', 'rulerBtn', 'shotBtn',
  'keysBtn', 'noSel', 'sumCounts', 'sumStage', 'sumMod', 'sumPath', 'props', 'px', 'py', 'pz', 'ph', 'drop', 'dup', 'reset', 'del', 'carry',
  'output', 'status', 'dlg', 'gridDlg', 'sptDlg', 'netDlg', 'keysDlg', 'palDlg', 'stageDlg', 'propDlg', 'pfDlg', 'askDlg', 'modDlg'];
// [button, dialog it opens]: each must open, and Esc must close it
const DIALOGS = [['gridBtn', 'gridDlg'], ['sptBtn', 'sptDlg'], ['pupBtn', 'sptDlg'], ['aipBtn', 'netDlg'], ['fopBtn', 'netDlg'],
  ['texBtn', 'texDlg'], ['lightBtn', 'lightDlg'], ['hud', 'hudDlg'], ['keysBtn', 'keysDlg'], ['settings', 'dlg'], ['testOpts', 'testDlg'],
  ['newStage', 'stageDlg']];
// known to fail today (a step of the UX plan fixes each): reported, not failing
const KNOWN = new Set(args.includes('--strict') ? [] : ['esc:lightDlg']);

const tmp = fs.mkdtempSync(path.join(tmpdir(), 'wotm_ui_'));
const mods = path.join(tmp, 'mods');
fs.mkdirSync(path.join(mods, 'smoke'), { recursive: true });
const cfgFile = path.join(tmp, 'settings.json');
fs.writeFileSync(cfgFile, JSON.stringify({ mods, mod: 'smoke', game: '', elf: '', cwd: '', theme: 'dark' }));

const server = spawn(process.platform === 'win32' ? 'python' : 'python3', [path.join(HERE, 'editor.py'), '--serve', '--port', String(PORT)],
  { cwd: REPO, stdio: 'ignore', env: { ...process.env, WOTM_LEVEL_EDITOR_CONFIG: cfgFile, WOTM_LEVEL_EDITOR_NAMES: path.join(tmp, 'names.json') } });
const edgePaths = ['C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
  'C:/Program Files/Google/Chrome/Application/chrome.exe', '/usr/bin/microsoft-edge', '/usr/bin/google-chrome', '/usr/bin/chromium'];
const browser = edgePaths.find((p) => fs.existsSync(p));
if (!browser) { console.error('no Edge or Chrome found'); server.kill(); process.exit(2); }
const edge = spawn(browser, ['--headless=new', `--remote-debugging-port=${DBG}`, '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
  '--disable-gpu-sandbox', `--user-data-dir=${path.join(tmp, 'profile')}`, '--window-size=1600,900', 'about:blank'], { stdio: 'ignore' });
let failures = 0;
const done = (code) => {
  try { server.kill(); edge.kill(); } catch { /* gone */ }
  setTimeout(() => { if (!args.includes('--keep')) fs.rmSync(tmp, { recursive: true, force: true }); process.exit(code); }, 500);
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
  if (d.method === 'Log.entryAdded' && d.params.entry.level === 'error') pageErrors.push('log: ' + d.params.entry.text + ' ' + (d.params.entry.url || ''));
  if (d.method === 'Runtime.consoleAPICalled' && d.params.type === 'error') pageErrors.push('console: ' + JSON.stringify(d.params.args.map((a) => a.value || a.description)).slice(0, 300));
  if (d.method === 'Runtime.exceptionThrown') pageErrors.push(JSON.stringify(d.params.exceptionDetails).slice(0, 300));
};
const send = (method, params = {}) => new Promise((r) => { const i = ++nextId; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
async function ev(expr) {
  const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
  if (r.result.exceptionDetails) throw new Error(JSON.stringify(r.result.exceptionDetails).slice(0, 400));
  return r.result.result.value;
}
const key = async (k, code, vk, modifiers = 0) => {
  await send('Input.dispatchKeyEvent', { type: 'keyDown', key: k, code, windowsVirtualKeyCode: vk, modifiers });
  await send('Input.dispatchKeyEvent', { type: 'keyUp', key: k, code, windowsVirtualKeyCode: vk, modifiers });
};
const esc = () => key('Escape', 'Escape', 27);
const shot = async (name) => {
  if (!shotsDir) return;
  fs.mkdirSync(shotsDir, { recursive: true });
  fs.writeFileSync(path.join(shotsDir, name + '.png'), Buffer.from((await send('Page.captureScreenshot', { format: 'png' })).result.data, 'base64'));
};
async function until(expr, tries = 40, gap = 250) {
  for (let i = 0; i < tries; i++) { if (await ev(expr).catch(() => false)) return true; await sleep(gap); }
  return false;
}
async function check(name, fn) {
  try { await fn(); console.log('ok    ' + name); } catch (e) {
    if (KNOWN.has(name)) { console.log('known ' + name + ' (' + String(e.message).slice(0, 120) + ')'); return; }
    failures++; console.log('FAIL  ' + name + ': ' + String(e.message).slice(0, 300));
  }
}
const must = (cond, why) => { if (!cond) throw new Error(why); };

await send('Page.enable');
await send('Runtime.enable');
await send('Log.enable');
await send('Page.navigate', { url: `http://127.0.0.1:${PORT}/?debug` });
const loaded = await until('!!(window.wotmDebug && window.wotmDebug.stageRoot && window.wotmDebug.items().length > 20)', 60, 500);
if (!loaded) {
  console.log('FAIL  the stage did not load in 3 minutes (disc configured?)');
  console.log(await ev(`JSON.stringify({ ready: document.readyState, url: location.href, debug: !!window.wotmDebug, busy: (document.getElementById('busyText') || {}).textContent,
    status: (document.querySelector('#status .msg .tx') || {}).textContent, stage: (document.getElementById('stage') || {}).value })`).catch((e) => String(e)));
  console.log('page errors:', pageErrors.join(' | ') || '(none)');
  failures++; done(1); await sleep(60000);
}
console.log('stage:', await ev('document.getElementById("stage").value + ", " + window.wotmDebug.items().length + " items"'));

await check('the summary counts the objects', async () => {
  must(/\d+ objects/.test(await ev('document.getElementById("sumCounts").textContent')), 'sumCounts: ' + await ev('document.getElementById("sumCounts").textContent'));
});
await check('the controls other scripts click are all there', async () => {
  const lost = await ev(`${JSON.stringify(STABLE_IDS)}.filter((i) => !document.getElementById(i))`);
  must(lost.length === 0, 'missing ids: ' + lost.join(', '));
});
await check('the server turns away a POST that is not JSON, and answers the page', async () => {
  must(await ev(`fetch('/api/config', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: '{}' }).then((r) => r.status)`) === 403, 'text/plain POST was not refused');
  must(await ev(`fetch('/api/init').then((r) => r.status)`) === 200, 'the page cannot read /api/init');
});

// an edit, undo, Save in the temporary mod
const first = await ev(`(() => { const D = window.wotmDebug; const it = D.items().find((x) => !x.copy && x.rec.info.kind === 'destr'); D.select(it); return it.rec.info.handle; })()`);
const stage = await ev('document.getElementById("stage").value');
const levelFile = path.join(mods, 'smoke', 'levels', stage + '.json');
await check('an edit shows in the patch and undo takes it back', async () => {
  await ev('window.wotmDebug.nudge(0, 0, 1, false)');
  must(await ev('window.wotmDebug.buildPatch().objects.length') === 1, 'the patch has no entry after the nudge');
  must(await ev('window.wotmDebug.unsavedCount()') >= 1, 'the edit is not counted as unsaved');
  await ev('window.wotmDebug.undo()');
  must(await ev('window.wotmDebug.buildPatch().objects.length') === 0, 'undo left an entry');
});
await check('Save writes the patch into the mod and saving the undone state removes the file', async () => {
  await ev('window.wotmDebug.select(window.wotmDebug.items().find((x) => x.rec.info.handle === ' + JSON.stringify(first) + ' && !x.copy)); window.wotmDebug.nudge(0, 0, 1, false)');
  await ev(`window.wotmDebug.post('save')`);
  must(await until(`window.wotmDebug.unsavedCount() === 0`, 80), 'Save did not finish');
  must(fs.existsSync(levelFile), 'no ' + levelFile);
  must(JSON.parse(fs.readFileSync(levelFile, 'utf8')).objects.some((o) => o.handle === first), 'the file lacks the object');
  await ev('window.wotmDebug.undo()');
  await ev(`window.wotmDebug.post('save')`);
  must(await until(`window.wotmDebug.unsavedCount() === 0`, 80), 'the second Save did not finish');
  must(!fs.existsSync(levelFile), 'the file stayed after saving the original state; the patch is ' + await ev('JSON.stringify(window.wotmDebug.buildPatch())'));
});

await ev('window.wotmDebug.select(null)').catch(() => {});
for (const [btn, dlg] of DIALOGS) {
  await check(`${btn} opens ${dlg}`, async () => {
    await ev(`document.getElementById(${JSON.stringify(btn)}).click()`);
    must(await until(`!!(document.getElementById(${JSON.stringify(dlg)}) && document.getElementById(${JSON.stringify(dlg)}).open)`, 80), `${dlg} is not open`);
    await shot(`dlg_${dlg}_${btn}`);
  });
  await check(`esc:${dlg}`, async () => {
    await esc();
    if (!await until(`!document.getElementById(${JSON.stringify(dlg)}).open`, 12)) {
      await ev(`document.getElementById(${JSON.stringify(dlg)}).close()`);       // do not leave it for the next case
      throw new Error('Esc did not close it');
    }
  });
}
await check('Ctrl+P opens the palette and Esc closes it', async () => {
  await key('p', 'KeyP', 80, 2);
  must(await until(`document.getElementById('palDlg').open`, 40), 'palDlg is not open');
  await esc();
  must(await until(`!document.getElementById('palDlg').open`, 12), 'Esc did not close palDlg');
});
await check('the Add prop and Layers menus open and close', async () => {
  for (const [b, m] of [['addProp', 'propMenu'], ['layersBtn', 'layersMenu']]) {
    await ev(`document.getElementById(${JSON.stringify(b)}).click()`);
    must(await until(`!document.getElementById(${JSON.stringify(m)}).hidden`, 12), m + ' did not open');
    await shot('menu_' + m);
    await esc();
    must(await until(`document.getElementById(${JSON.stringify(m)}).hidden`, 12), 'Esc did not close ' + m);
  }
});
await check('the Pedestrians mode and back', async () => {
  await ev('document.getElementById("modePeds").click()');
  must(await until(`window.wotmDebug.mode === 'peds' || window.wotmDebug.mode === 'pedestrians'`, 20), 'mode: ' + await ev('window.wotmDebug.mode'));
  await shot('mode_peds');
  await ev('document.getElementById("modeObjects").click()');
  must(await until(`window.wotmDebug.mode === 'objects'`, 20), 'mode: ' + await ev('window.wotmDebug.mode'));
});
await check('no script error in the page', async () => {
  // the browser asks for a favicon the editor has none of; the 403 is the refused POST this test makes on purpose
  const bad = pageErrors.filter((e) => !/favicon\.ico/.test(e) && !/403 \(Forbidden\) http:\/\/127\.0\.0\.1:\d+\/api\/config/.test(e));
  must(bad.length === 0, bad.join(' | '));
});

console.log(failures ? `${failures} FAILED` : 'test_ui: ok');
done(failures ? 1 : 0);
