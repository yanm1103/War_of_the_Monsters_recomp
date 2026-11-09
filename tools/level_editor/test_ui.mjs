// Smoke test of the level editor's page: loads a stage, the controls other code and scripts rely on are there, an edit goes
// through undo and Save, every dialog opens and closes with Esc, the server turns away a request the page would not send.
//
// Needs the disc configured (the stage is read from it), Node 22 (the WebSocket is Node's own) and Edge or Chrome. Nothing of
// yours is touched: the editor program runs with its own settings file (WOTM_LEVEL_EDITOR_CONFIG) and a temporary mods folder,
// on free ports, with Edge headless (software rendering: the first stage takes a while).
// Run: node tools/level_editor/test_ui.mjs            (--keep leaves the temporary folder; --shots <dir> saves a PNG per dialog)
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..');
// free ports each run (a program left over from an earlier run can never answer for this one)
const freePort = () => new Promise((res) => { const s = net.createServer(); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => res(p)); }); });
const PORT = await freePort(), DBG = await freePort();
// kill a program and what it started (on Windows kill() only reaches the launcher, and Edge has helper processes)
const killTree = (c) => {
  if (!c || !c.pid) return;
  try { if (process.platform === 'win32') spawnSync('taskkill', ['/PID', String(c.pid), '/T', '/F'], { stdio: 'ignore' }); else c.kill(); } catch { /* gone */ }
};
const args = process.argv.slice(2);
const shotsDir = args.includes('--shots') ? args[args.indexOf('--shots') + 1] : '';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ids that scripts and tests outside this file click or read: moving a control keeps its id (docs/investigacoes, ux_consolidado 4.3)
const STABLE_IDS = ['stage', 'newStage', 'editStage', 'mod', 'packMod', 'undo', 'redo', 'check', 'save', 'test', 'testHere', 'testOpts', 'reload',
  'stop', 'help', 'settings', 'modeObjects', 'modePeds', 'modeSpt', 'modePup', 'modeAip', 'modeFop', 'mkPanel', 'mkList', 'mkAdd', 'mkSave', 'mkProps', 'mkNoSel', 'texBtn', 'lightBtn', 'hud', 'filter', 'list', 'canvas', 'toolBar', 'tMove', 'tTurn',
  'snap', 'snapMove', 'snapTurn', 'addProp', 'propMenu', 'pmImport', 'pmTerrain', 'pmSky', 'pmClouds', 'overlay', 'look', 'grid', 'layersBtn',
  'layersMenu', 'vFly', 'vOrbit', 'focusSel', 'frame', 'gridBtn', 'picBtn', 'rulerBtn', 'shotBtn',
  'viewTop', 'noSel', 'sumCounts', 'sumStage', 'sumMod', 'sumPath', 'props', 'px', 'py', 'pz', 'ph', 'drop', 'dup', 'reset', 'del', 'carry',
  'output', 'status', 'dlg', 'gridDlg', 'keysDlg', 'palDlg', 'stageDlg', 'propDlg', 'pfDlg', 'askDlg', 'modDlg'];
// [button, dialog it opens]: each must open, and Esc must close it
const DIALOGS = [['gridBtn', 'gridDlg'],
  ['texBtn', 'texDlg'], ['lightBtn', 'lightDlg'], ['hud', 'hudDlg'], ['help', 'keysDlg'], ['settings', 'dlg'], ['testOpts', 'testDlg'],
  ['newStage', 'stageDlg']];
// known to fail today (a step of the UX plan fixes each): reported, not failing
const KNOWN = new Set(args.includes('--strict') ? [] : []);

const tmp = fs.mkdtempSync(path.join(tmpdir(), 'wotm_ui_'));
const mods = path.join(tmp, 'mods');
fs.mkdirSync(path.join(mods, 'smoke'), { recursive: true });
const cfgFile = path.join(tmp, 'settings.json');
fs.writeFileSync(cfgFile, JSON.stringify({ mods, mod: 'smoke', game: '', elf: '', cwd: '', theme: 'dark' }));

const serverLog = path.join(tmp, 'server.log');
const serverOut = fs.openSync(serverLog, 'w');
const server = spawn(process.platform === 'win32' ? 'python' : 'python3', [path.join(HERE, 'editor.py'), '--serve', '--port', String(PORT)],
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
  if (d.method === 'Page.javascriptDialogOpening') { send('Page.handleJavaScriptDialog', { accept: true }); return; }     // "leave this page?" on a reload with changes
  if (d.method === 'Log.entryAdded' && d.params.entry.level === 'error') pageErrors.push('log: ' + d.params.entry.text + ' ' + (d.params.entry.url || ''));
  if (d.method === 'Runtime.consoleAPICalled' && d.params.type === 'error') pageErrors.push('console: ' + JSON.stringify(d.params.args.map((a) => a.value || a.description)).slice(0, 300));
  if (d.method === 'Runtime.exceptionThrown') pageErrors.push(JSON.stringify(d.params.exceptionDetails).slice(0, 300));
};
const send = (method, params = {}) => new Promise((r) => { const i = ++nextId; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
async function ev(expr) {
  // userGesture: a click the test makes counts as the user's (without one the browser closes two dialogs opened by scripts with a single Esc)
  const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true, userGesture: true });
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
// The first navigation sometimes meets "connection refused" from a server that is up (seen on Windows, not understood): the page
// is asked for again, up to three times, a minute each.
let loaded = false;
for (let attempt = 1; attempt <= 3 && !loaded; attempt++) {
  if (attempt > 1) console.log(`the page did not come up (${pageErrors.length} errors): trying again (${attempt}/3)`);
  pageErrors.length = 0;
  await send('Page.navigate', { url: `http://127.0.0.1:${PORT}/?debug` });
  loaded = await until('!!(window.wotmDebug && window.wotmDebug.stageRoot && window.wotmDebug.items().length > 20)', 120, 500);
}
if (!loaded) {
  console.log('FAIL  the stage did not load in three tries (disc configured?)');
  console.log(await ev(`JSON.stringify({ ready: document.readyState, url: location.href, debug: !!window.wotmDebug, busy: (document.getElementById('busyText') || {}).textContent,
    status: (document.querySelector('#status .msg .tx') || {}).textContent, stage: (document.getElementById('stage') || {}).value })`).catch((e) => String(e)));
  console.log('page errors:', pageErrors.slice(0, 5).join(' | ') || '(none)');
  console.log('server alive:', server.exitCode === null, '| server log:', fs.readFileSync(serverLog, 'utf8').slice(-1500) || '(empty)');
  failures++; done(1); await sleep(60000);
}
console.log('stage:', await ev('document.getElementById("stage").value + ", " + window.wotmDebug.items().length + " items"'));

await check('the summary counts the objects', async () => {
  must(await until('/\\d+ objects/.test(document.getElementById("sumCounts").textContent)', 40), 'sumCounts: ' + await ev('document.getElementById("sumCounts").textContent'));
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

await check('two Saves at once write once, and the button says it is saving', async () => {
  await ev('window.wotmDebug.select(window.wotmDebug.items().find((x) => x.rec.info.handle === ' + JSON.stringify(first) + ' && !x.copy)); window.wotmDebug.nudge(0, 0, 1, false)');
  const r = await ev(`(async () => { const D = window.wotmDebug; const a = D.post('save'), b = D.post('save');
    const busy = document.getElementById('save').getAttribute('aria-busy') + ' ' + document.querySelector('#save .lbl').textContent;
    return JSON.stringify([busy, await a, await b, document.getElementById('save').hasAttribute('aria-busy'), document.querySelector('#save .lbl').textContent]); })()`);
  must(r === JSON.stringify(['true Saving…', true, false, false, 'Save']), 'got ' + r);
  await ev('window.wotmDebug.undo()');
  await ev(`window.wotmDebug.post('save')`);
  must(!fs.existsSync(levelFile), 'the file stayed after saving the original state');
});
await check('an emptied X field puts the number back instead of moving the object to 0', async () => {
  await ev('window.wotmDebug.select(window.wotmDebug.items().find((x) => x.rec.info.handle === ' + JSON.stringify(first) + ' && !x.copy))');
  const before = await ev('document.getElementById("px").value');
  must(before !== '', 'the X field is empty after selecting');
  await ev(`(() => { const f = document.getElementById('px'); f.value = ''; f.dispatchEvent(new Event('change')); })()`);
  must(await ev('document.getElementById("px").value') === before, 'the field did not get its number back');
  must(await ev('window.wotmDebug.buildPatch().objects.length') === 0, 'the object moved');
  must(await ev('window.wotmDebug.history.length') === 0 || await ev('window.wotmDebug.unsavedCount()') === 0, 'it counts as a change');
});
await check('a click on the gizmo that moves nothing is not an edit', async () => {
  const h0 = await ev('window.wotmDebug.history.length');
  await ev(`(() => { const g = window.wotmDebug.gizmo; g.dispatchEvent({ type: 'dragging-changed', value: true }); g.dispatchEvent({ type: 'dragging-changed', value: false }); })()`);
  must(await ev('window.wotmDebug.history.length') === h0, 'a history step was left');
  must(await ev('window.wotmDebug.unsavedCount()') === 0 && await ev('window.wotmDebug.dirty') === false, 'the stage is marked as changed');
});

const answer = async (value) => {      // a button of the question
  must(await until(`document.getElementById('askDlg').open`, 20), 'no question was asked');
  await ev(`document.querySelector('#askBtns button[value=${JSON.stringify(value)}]').click()`);
  must(await until(`!document.getElementById('askDlg').open`, 20), 'the question stayed open');
};
// ---- the marker modes: start points, power-ups, AI net and vehicle net are modes 3 to 6 of the rail, each a draft of its file
const D = 'window.wotmDebug';
const MODE_KINDS = [['spt', 'SPT', 'modeSpt', '3', 51], ['pup', 'PUP', 'modePup', '4', 52], ['aip', 'AIP', 'modeAip', '5', 53], ['fop', 'FOP', 'modeFop', '6', 54]];
const layerN = (kind) => ev(`${D}.lay(${JSON.stringify(kind)}).points.length`);
const goMode = async (m) => {
  await ev(`${D}.setMode(${JSON.stringify(m)})`);
  must(await until(`${D}.mode === ${JSON.stringify(m)}`, 20), 'the mode did not open: ' + m);
};
await check('AI net mode: Clear net asks first; Esc keeps the nodes; Ctrl+P waits for the answer; Ctrl+Z and Revert bring them back', async () => {
  await key('5', 'Digit5', 53);
  must(await until(`${D}.mode === 'aip'`, 20), 'key 5 did not open the AI net mode');
  must(await until(`${D}.lay('AIP').points.length > 0`, 80), 'the stage has no AI net to try this on');
  const n0 = await layerN('AIP');
  await ev(`document.getElementById('mkClear').click()`);
  must(await until(`document.getElementById('askDlg').open`, 20), 'no question was asked');
  await key('p', 'KeyP', 80, 2);                      // Ctrl+P: the palette must not open over the question
  await sleep(300);
  must(await ev(`!(document.getElementById('palDlg') && document.getElementById('palDlg').open)`), 'the palette opened over the question');
  await esc();
  must(await until(`!document.getElementById('askDlg').open`, 20), 'Esc did not close the question');
  must(await layerN('AIP') === n0, 'Esc removed the nodes');
  must(await ev(`${D}.mode === 'aip'`), 'Esc on the question left the mode');
  await ev(`document.getElementById('mkClear').click()`);
  must(await until(`document.getElementById('askDlg').open`, 20), 'no question the second time');
  await ev(`document.querySelector('#askBtns .primary, #askBtns button:last-child').click()`);
  must(await until(`${D}.lay('AIP').points.length === 0`, 20), 'the nodes stayed after the answer');
  must(await ev(`${D}.unsavedCount()`) === 1, 'an emptied net is one unsaved change, not ' + await ev(`${D}.unsavedCount()`));
  must(await ev(`document.getElementById('modeAip').classList.contains('dirty')`), 'the rail button has no dot for the draft');
  await ev('document.activeElement.blur()');
  await key('z', 'KeyZ', 90, 2);                       // Ctrl+Z in a marker mode is that layer's own Undo
  must(await until(`${D}.lay('AIP').points.length === ${n0}`, 20), 'Ctrl+Z did not bring the nodes back: ' + await layerN('AIP') + ' of ' + n0);
  must(await ev(`${D}.unsavedCount()`) === 0, 'the undone net still counts as a change');
  await ev(`document.getElementById('mkClear').click()`);
  await ev(`document.querySelector('#askBtns .primary, #askBtns button:last-child').click()`);
  must(await until(`${D}.lay('AIP').points.length === 0`, 20), 'the second Clear did not empty the net');
  await ev(`document.getElementById('mkRevert').click()`);       // Revert: the file's net comes back
  must(await until(`${D}.lay('AIP').points.length === ${n0} && ${D}.unsavedCount() === 0`, 40), 'Revert did not bring the net back: ' + await layerN('AIP') + ' nodes');
  await key('1', 'Digit1', 49);
  must(await until(`${D}.mode === 'objects'`, 20), 'key 1 did not go back to the Objects mode');
});

await check('Light and fog: unsaved numbers count, Esc asks, Keep editing keeps, Discard closes and restores', async () => {
  await ev(`document.getElementById('lightBtn').click()`);
  must(await until(`document.getElementById('lightDlg') && document.getElementById('lightDlg').open`, 80), 'lightDlg did not open');
  must(await ev('window.wotmDebug.unsavedCount()') === 0, 'a freshly opened panel counts as changed');
  await ev(`(() => { const f = [...document.querySelectorAll('#lightDlg #ltMin, #lightDlg .lt-h')].find((x) => x.offsetParent); f.value = String((+f.value || 0) + 7); f.dispatchEvent(new Event('input')); })()`);
  must(await ev('window.wotmDebug.unsavedCount()') === 1, 'the changed light is not counted: ' + await ev('window.wotmDebug.unsavedCount()'));
  must(await ev(`document.getElementById('save').classList.contains('dirty')`), 'Save is not lit');
  // the keys still work with the panel open (it is not modal): an edit and Ctrl+Z
  await ev(`window.wotmDebug.select(window.wotmDebug.items().find((x) => x.rec.info.handle === ${JSON.stringify(first)} && !x.copy)); window.wotmDebug.nudge(0, 0, 1, false); document.activeElement.blur()`);
  must(await ev('window.wotmDebug.buildPatch().objects.length') === 1, 'the nudge made no entry');
  await key('z', 'KeyZ', 90, 2);
  must(await until('window.wotmDebug.buildPatch().objects.length === 0', 20), 'Ctrl+Z did nothing with the Light panel open');
  await ev(`window.wotmDebug.select(null); document.querySelector('#lightDlg #ltMin, #lightDlg [data-close]').focus()`);
  await esc();
  await answer('keep');
  must(await ev(`document.getElementById('lightDlg').open`), 'Keep editing closed the panel');
  await esc();
  await answer('discard');
  must(await until(`!document.getElementById('lightDlg').open`, 20), 'Discard did not close the panel');
  must(await ev('window.wotmDebug.unsavedCount()') === 0, 'the discarded numbers still count');
});
await check("Light and fog: every number has a reset to the disc's value, shown only when it differs; Reset all; works on a mod's file", async () => {
  const dock = `document.getElementById('lightDlg')`;
  const open = async () => {
    await ev(`document.getElementById('lightBtn').click()`);
    must(await until(`${dock} && ${dock}.open`, 80), 'lightDlg did not open');
    must(await ev(`!!document.querySelector('#lightDlg #ltMin').offsetParent && document.querySelectorAll('#lightDlg .lt-light').length === 3`), 'the stage has no fog or lights to try this on');
  };
  const vis = () => ev(`[...document.querySelectorAll('#lightDlg .lt-rs')].filter((b) => !b.hidden).length`);
  const setv = (sel, v) => ev(`(() => { const f = document.querySelector(${JSON.stringify(sel)}); f.value = ${JSON.stringify(String(v))}; f.dispatchEvent(new Event('input')); })()`);
  const val = (sel) => ev(`document.querySelector(${JSON.stringify(sel)}).value`);
  const btnOf = (sel) => `(document.querySelector(${JSON.stringify(sel)}).closest('.lt-row, .lt-f, .lt-lh') || document).querySelector('.lt-rs')`;
  const unsaved = () => ev('window.wotmDebug.unsavedCount()');
  const allOff = async (why) => {
    must(await vis() === 0, why + ': a reset button is still shown');
    must(await ev(`document.querySelector('#lightDlg .lt-resetall').disabled`), why + ': Reset all is still enabled');
    must(await unsaved() === 0, why + ': still counts as changed (' + await unsaved() + ')');
  };
  await open();
  await allOff('a fresh panel');
  // a fog number: the button shows, names the original, and puts it back (the focus stays in the panel)
  const min0 = await val('#ltMin');
  await setv('#ltMin', +min0 + 7);
  must(await ev(`!${btnOf('#ltMin')}.hidden`), 'no reset button after editing "Starts at"');
  const t = await ev(`${btnOf('#ltMin')}.title`);
  must(t.startsWith('Back to the original') && t.includes(min0), 'the tooltip does not name the original: ' + t);
  must(await ev(`${btnOf('#ltMin')}.getBoundingClientRect().width >= 24 && !!${btnOf('#ltMin')}.getAttribute('aria-label')`), 'the button is smaller than 24px or has no label');
  must(await unsaved() === 1 && !await ev(`document.querySelector('#lightDlg .lt-resetall').disabled`), 'the edit is not counted or Reset all stayed off');
  await ev(`${btnOf('#ltMin')}.click()`);
  must(await val('#ltMin') === min0, 'the number did not come back');
  must(await ev(`document.activeElement.id`) === 'ltMin', 'the focus left the number: ' + await ev('document.activeElement.id || document.activeElement.className'));
  await allOff('after the number');
  // the double-click on the label is the same reset
  await setv('#ltMax', +await val('#ltMax') + 100);
  await ev(`document.querySelector('label[for=ltMax]').dispatchEvent(new MouseEvent('dblclick', { bubbles: true }))`);
  await allOff('after the double click');
  // the fog color also takes the background back with it
  const fc0 = await val('#ltFogC');
  await setv('#ltFogC', fc0 === '#c86432' ? '#3264c8' : '#c86432');
  must(await unsaved() === 1 && await ev(`!${btnOf('#ltFogC')}.hidden`), 'the fog color edit shows no reset');
  await ev(`${btnOf('#ltFogC')}.click()`);
  must(await val('#ltFogC') === fc0, 'the fog color did not come back');
  await allOff('after the fog color');
  // a light: strength then color go back one at a time and the panel ends as the disc's (the exact numbers, not a rounded look-alike)
  const pc0 = +await val('#lt0pct'), c0 = await val('#lightDlg .lt-c');
  await setv('#lt0pct', pc0 + 10);
  await setv('#lightDlg .lt-c', c0 === '#00ff00' ? '#ff0000' : '#00ff00');
  must(await unsaved() === 1, 'the light edits are not counted');
  await ev(`${btnOf('#lt0pct')}.click()`);
  must(+await val('#lt0pct') === pc0 && await unsaved() === 1, 'strength did not come back or the color went with it');
  await ev(`[...document.querySelectorAll('#lightDlg .lt-light')][0].querySelectorAll('.lt-lh .lt-rs')[1].click()`);
  must(await val('#lightDlg .lt-c') === c0, 'the color did not come back');
  await allOff('after strength and color');
  // the heading, the whole light, and Reset all
  await setv('#lt0h', +await val('#lt0h') + 15);
  await setv('#lt1p', Math.min(90, +await val('#lt1p') + 10));
  await setv('#lt1pct', +await val('#lt1pct') + 5);
  must(await unsaved() === 1, 'the light edits are not counted');
  await ev(`document.querySelectorAll('#lightDlg .lt-light')[1].querySelectorAll('.lt-lh .lt-rs')[0].click()`);   // the whole of light 2
  must(await ev(`document.querySelectorAll('#lightDlg .lt-light')[1].querySelector('.lt-lh .lt-rs').hidden`) === true, 'light 2 still shows a reset');
  await setv('#ltMin', +min0 + 7);
  await setv('#ltFogC', fc0 === '#c86432' ? '#3264c8' : '#c86432');
  await sleep(500);
  await shot('light_resets');
  await ev(`document.querySelector('#lightDlg .lt-resetall').click()`);
  await allOff('after Reset all');
  must(await val('#ltMin') === min0, 'Reset all left a number');
  // Esc still reaches the panel after a reset: nothing unsaved, so it closes
  await setv('#ltMin', +min0 + 7);
  await ev(`${btnOf('#ltMin')}.click()`);
  await esc();
  must(await until(`!${dock}.open`, 20), 'Esc after a reset did not close the panel');
  must(await ev('document.getElementById("askDlg").open') === false, 'a question was asked about nothing');

  // with a mod's file: the button goes back to the disc's number, not the file's; Save writes the disc's; Remove from mod deletes the file
  const mrsFile = path.join(mods, 'smoke', 'RST', stage + '.MRS');
  await open();
  await setv('#ltMin', +min0 + 7);
  await ev(`document.querySelector('#lightDlg .lt-save').click()`);
  must(await until(`window.wotmDebug.unsavedCount() === 0 && ${dock}.querySelector('.lt-src span').textContent.startsWith("Your mod's file")`, 40), 'Save did not finish');
  must(fs.existsSync(mrsFile), 'no ' + mrsFile);
  must(await ev(`document.querySelector('#lightDlg .lt-src span').textContent`).then((x) => x.startsWith("Your mod's file")), 'the panel does not say it uses the mod file');
  must(await vis() === 1 && await val('#ltMin') === String(+min0 + 7), 'the saved number should offer exactly one reset (' + await vis() + ')');
  must((await ev(`${btnOf('#ltMin')}.title`)).includes(min0), 'the tooltip does not name the disc number');
  await ev(`${btnOf('#ltMin')}.click()`);
  must(await unsaved() === 1, 'going back to the disc number from a saved file should be a change');
  await ev(`document.querySelector('#lightDlg .lt-save').click()`);
  must(await until(`window.wotmDebug.unsavedCount() === 0`, 40), 'the second Save did not finish');
  const r = await ev(`fetch('/api/mrs?name=${stage}&mod=smoke').then((x) => x.json())`);
  must(r.fog.minR === r.original.fog.minR && String(r.fog.minR) === min0, 'the saved file does not have the disc numbers: ' + JSON.stringify(r.fog));
  await allOff('after saving the disc numbers');
  must(await ev(`!document.querySelector('#lightDlg .lt-reset').hidden`), 'no "Remove from mod" for a mod that has the file');
  await ev(`document.querySelector('#lightDlg .lt-reset').click()`);
  await answer('go');
  must(await until(`document.querySelector('#lightDlg .lt-reset').hidden`, 40), 'Remove from mod left the button');
  must(!fs.existsSync(mrsFile), 'the file stayed after Remove from mod');
  await esc();
  must(await until(`!${dock}.open`, 20), 'Esc did not close the panel at the end');
});
await check('modes 3 to 6: the rail has them, the keys and the buttons open them; the left panel lists the markers, the right one is theirs', async () => {
  const order = await ev(`[...document.getElementById('rail').children].map((e) => e.id || e.className).join(',')`);
  must(order === 'modeObjects,modePeds,rsep,modeSpt,modePup,modeAip,modeFop,toggleLeft', 'the rail is: ' + order);
  must(await ev(`['modeObjects','modePeds','modeSpt','modePup','modeAip','modeFop'].every((i) => { const b = document.getElementById(i);
    return b.title && b.getAttribute('aria-label') && b.getAttribute('aria-pressed') !== null && b.querySelector('.ic'); })`), 'a mode button lacks its title, label, state or icon');
  for (const [m, kind, btn, k, vk] of MODE_KINDS) {
    await goMode('objects');
    await ev('document.activeElement && document.activeElement.blur()');     // (a click on the view leaves the focus on the page)
    await key(k, 'Digit' + k, vk);
    must(await until(`${D}.mode === '${m}'`, 20), `key ${k} did not open ${m}`);
    const st = JSON.parse(await ev(`JSON.stringify({ pressed: [...document.querySelectorAll('#rail > .mode')].filter((b) => b.getAttribute('aria-pressed') === 'true').map((b) => b.id),
      body: document.body.dataset.mode, kind: document.body.dataset.kind, panel: !!document.getElementById('mkPanel').offsetParent, bar: !!document.getElementById('mkBar').offsetParent,
      propBar: !document.getElementById('propBar').hidden, objList: !!document.getElementById('list').offsetParent, noSel: !!document.getElementById('mkNoSel').offsetParent,
      props: !!document.getElementById('mkProps').offsetParent, objNoSel: !!document.getElementById('noSel').offsetParent, tab: !document.getElementById('selTab').hidden })`));
    must(JSON.stringify(st.pressed) === JSON.stringify([btn]), `${m}: aria-pressed is on ${st.pressed}`);
    must(st.body === m && st.kind === kind && st.panel && st.bar && !st.propBar && !st.objList, `${m}: the left panel is ${JSON.stringify(st)}`);
    must(st.noSel && !st.props && !st.objNoSel, `${m}: the right panel is ${JSON.stringify(st)}`);
    must(await until(`document.querySelectorAll('#mkList li.it').length === ${D}.lay('${kind}').points.length`, 40), `${kind}: the list rows do not match the points`);
    must(await ev(`document.querySelectorAll('#mkLegend li').length`) >= 2, `${kind}: the legend of colors is empty`);
    await shot('mode_' + m);
  }
  await ev(`document.getElementById('modeObjects').click()`);                 // the rail buttons too
  await ev(`document.getElementById('modePup').click()`);
  must(await until(`${D}.mode === 'pup'`, 20), 'the Power-ups button did not open its mode');
  await key('1', 'Digit1', 49);
  must(await until(`${D}.mode === 'objects' && !document.getElementById('propBar').hidden && document.getElementById('mkBar').hidden`, 20), 'key 1 did not bring the Objects mode back');
  await key('2', 'Digit2', 50);
  must(await until(`${D}.mode === 'peds'`, 20), 'key 2 did not open Pedestrians');
  await key('4', 'Digit4', 52);
  must(await until(`${D}.mode === 'pup'`, 20), 'key 4 from Pedestrians did not open Power-ups');
  await goMode('objects');
});
for (const [m, kind] of MODE_KINDS) {
  await check(`${kind}: Add, select, move with the gizmo, the fields and the keys, duplicate, delete, Undo; Revert drops the draft`, async () => {
    await goMode(m);
    must(await until(`document.querySelector('#mkNewType').options.length > 0 || !document.getElementById('mkNewNumF').hidden`, 40), 'the Add bar has no types');
    const n0 = await layerN(kind);
    await ev(`document.getElementById('mkAdd').click()`);
    must(await layerN(kind) === n0 + 1, 'Add did not add');
    must(await ev(`${D}.lay('${kind}').sel`) === n0, 'the new marker is not the selected one');
    must(await ev(`${D}.marker.on && !!${D}.gizmoObject && !document.getElementById('mkProps').hidden && document.getElementById('mkNoSel').hidden`), 'no gizmo or no panel for the new marker');
    must(await ev(`!!document.querySelector('#mkList li.sel') && document.querySelector('#mkList li.sel').getAttribute('aria-selected') === 'true'`), 'its row is not selected');
    must(await ev(`${D}.unsavedCount()`) === 1, 'the draft is not counted: ' + await ev(`${D}.unsavedCount()`));
    must(await ev(`document.getElementById('save').classList.contains('dirty') && document.getElementById('mode${m[0].toUpperCase() + m.slice(1)}').classList.contains('dirty')`), 'Save or the rail button is not lit');
    must(await ev(`document.querySelector('#mkState').classList.contains('dirty')`), 'the foot does not say unsaved');
    // the gizmo moves it (a drag: the events the gizmo sends), and Ctrl+Z takes the move back
    const pos = () => ev(`JSON.stringify(${D}.lay('${kind}').points[${n0}].pos)`).then(JSON.parse);
    const p0 = await pos();
    await ev(`(() => { const g = ${D}.gizmo; g.dispatchEvent({ type: 'dragging-changed', value: true }); ${D}.gizmoObject.position.x += 50; ${D}.gizmoObject.position.z -= 40;
      g.dispatchEvent({ type: 'objectChange' }); g.dispatchEvent({ type: 'dragging-changed', value: false }); })()`);
    const p1 = await pos();
    must(Math.abs(p1[0] - p0[0] - 50) < 0.2 && Math.abs(p1[1] - p0[1] - 40) < 0.2, `the marker did not follow the gizmo: ${p0} -> ${p1}`);
    must(await ev(`+document.getElementById('mkx').value`) === p1[0], 'the X field does not show the new place');
    await ev('document.activeElement.blur()');
    await key('z', 'KeyZ', 90, 2);
    must(JSON.stringify(await pos()) === JSON.stringify(p0), 'Ctrl+Z did not put the marker back');
    await key('y', 'KeyY', 89, 2);
    must(JSON.stringify(await pos()) === JSON.stringify(p1), 'Ctrl+Y did not move it again');
    // the fields
    await ev(`(() => { const f = document.getElementById('mkx'); f.value = '123'; f.dispatchEvent(new Event('change')); })()`);
    must((await pos())[0] === 123, 'typing X did not move the marker');
    await ev(`(() => { const f = document.getElementById('mkx'); f.value = ''; f.dispatchEvent(new Event('change')); })()`);
    must((await pos())[0] === 123 && await ev(`document.getElementById('mkx').value`) === '123', 'an emptied X field moved the marker or kept empty');
    // the keys: an arrow nudges, Ctrl+D duplicates, Delete deletes
    await ev('document.activeElement.blur()');
    const was = JSON.stringify(await pos());
    await key('ArrowUp', 'ArrowUp', 38);
    must(await until(`JSON.stringify(${D}.lay('${kind}').points[${n0}].pos) !== ${JSON.stringify(was)}`, 20), 'an arrow did not nudge the marker');
    await key('d', 'KeyD', 68, 2);
    must(await until(`${D}.lay('${kind}').points.length === ${n0 + 2}`, 20), 'Ctrl+D did not duplicate');
    must(await ev(`${D}.lay('${kind}').sel`) === n0 + 1, 'the copy is not selected');
    await key('Delete', 'Delete', 46);
    must(await until(`${D}.lay('${kind}').points.length === ${n0 + 1} && ${D}.lay('${kind}').sel === -1`, 20), 'Delete did not delete');
    must(await ev(`!document.getElementById('mkNoSel').hidden && document.getElementById('mkProps').hidden`), 'the right panel kept the deleted marker');
    must(await ev(`${D}.lay('${kind}').points.every((p) => !p.links || p.links.every((l) => l.to < ${D}.lay('${kind}').points.length))`), 'a link points past the nodes');
    await shot('marker_' + m);
    await ev(`document.getElementById('mkRevert').click()`);
    must(await until(`${D}.lay('${kind}').points.length === ${n0} && ${D}.unsavedCount() === 0`, 40), 'Revert did not drop the draft');
    await goMode('objects');
  });
}
await check('drafts: they count as unsaved across modes, a stage switch asks, Save writes the file, Back to the disc\'s takes it away', async () => {
  const file = path.join(mods, 'smoke', 'PTS', stage + '.SPT');
  must(!fs.existsSync(file), 'the temporary mod already has a start points file');
  await goMode('spt');
  const n0 = await layerN('SPT');
  await ev(`document.getElementById('mkAdd').click()`);
  must(await ev(`${D}.unsavedCount()`) === 1, 'the point is not counted');
  await goMode('objects');                       // the draft stays when the mode is left
  must(await ev(`${D}.unsavedCount()`) === 1 && await ev(`document.getElementById('modeSpt').classList.contains('dirty')`), 'leaving the mode lost the draft or its dot');
  const other = await ev(`[...document.getElementById('stage').options].map((o) => o.value).find((v) => v !== ${JSON.stringify(stage)})`);
  await ev(`(() => { const s = document.getElementById('stage'); s.value = ${JSON.stringify(other)}; s.dispatchEvent(new Event('change')); })()`);
  must(await until(`document.getElementById('askDlg').open && document.getElementById('askTitle').textContent === 'Unsaved changes'`, 20), 'the stage switch did not ask');
  await answer('cancel');
  must(await ev(`document.getElementById('stage').value`) === stage && await layerN('SPT') === n0 + 1, 'Cancel changed the stage or lost the draft');
  await ev(`${D}.post('save')`);                  // the top bar's Save writes the patch and every changed layer
  must(await until(`${D}.unsavedCount() === 0`, 80), 'Save did not finish');
  must(fs.existsSync(file), 'no ' + file);
  const bytes = fs.readFileSync(file);
  must(bytes.length === 8 + 64 * (n0 + 1) && bytes.readUInt32LE(4) === n0 + 1, `the file holds ${bytes.readUInt32LE(4)} points, ${n0 + 1} wanted`);
  must(await ev(`!document.getElementById('modeSpt').classList.contains('dirty')`), 'the rail dot stayed after Save');
  await goMode('spt');
  must(await ev(`document.getElementById('mkReset').disabled`) === false, "Back to the disc's is off for a stage the mod has a file for");
  await ev(`document.getElementById('mkReset').click()`);
  await answer('go');
  must(await until(`${D}.lay('SPT').points.length === ${n0}`, 40), "Back to the disc's did not bring the stage's own points");
  must(!fs.existsSync(file), 'the file stayed after Back to the disc\'s');
  must(await ev(`document.getElementById('mkReset').disabled`), "Back to the disc's is on with no file");
  await goMode('objects');
});
await check('a stage switch with a draft: Discard opens the other stage with its own markers and no draft', async () => {
  await goMode('spt');
  await ev(`document.getElementById('mkAdd').click()`);
  const other = await ev(`[...document.getElementById('stage').options].map((o) => o.value).find((v) => v !== ${JSON.stringify(stage)})`);
  await ev(`(() => { const s = document.getElementById('stage'); s.value = ${JSON.stringify(other)}; s.dispatchEvent(new Event('change')); })()`);
  must(await until(`document.getElementById('askDlg').open`, 20), 'the stage switch did not ask');
  await answer('discard');
  const there = await ev(`fetch('/api/spt?name=${other}&mod=smoke&kind=SPT').then((r) => r.json()).then((j) => j.points.length)`);
  must(await until(`document.getElementById('busy').hidden && document.getElementById('stage').value === ${JSON.stringify(other)} && ${D}.lay('SPT').points.length === ${there}`, 240, 500),
    `the other stage's start points are not there: ${await layerN('SPT')} of ${there}`);
  must(await until(`document.querySelectorAll('#mkList li.it').length === ${there}`, 20), 'the list shows the old stage\'s rows');
  must(await ev(`${D}.unsavedCount()`) === 0, 'the draft came along to the other stage');
  await ev(`(() => { const s = document.getElementById('stage'); s.value = ${JSON.stringify(stage)}; s.dispatchEvent(new Event('change')); })()`);
  must(await until(`document.getElementById('busy').hidden && ${D}.items().length > 20 && document.getElementById('stage').value === ${JSON.stringify(stage)}`, 240, 500), 'the first stage did not come back');
  await goMode('objects');
});
await check("Undo and Redo of the patch leave a layer's draft alone; in a marker mode Ctrl+Z is the layer's", async () => {
  await ev(`${D}.layersReady()`);                 // (the stage the test before reopened is still reading its layers' files)
  await goMode('spt');
  const n0 = await layerN('SPT');
  await ev(`document.getElementById('mkAdd').click()`);
  await goMode('objects');
  await ev(`${D}.select(${D}.items().find((x) => x.rec.info.handle === ${JSON.stringify(first)} && !x.copy)); ${D}.nudge(0, 0, 1, false); document.activeElement.blur()`);
  must(await ev(`${D}.buildPatch().objects.length`) === 1, 'the nudge made no entry');
  await key('z', 'KeyZ', 90, 2);
  must(await until(`${D}.buildPatch().objects.length === 0`, 20), 'Ctrl+Z did not undo the nudge');
  await sleep(1200);                               // (a reload of the files would be done by now)
  must(await layerN('SPT') === n0 + 1 && await ev(`${D}.unsavedCount()`) === 1, 'Undo of the patch took the start point draft with it');
  await key('y', 'KeyY', 89, 2);
  must(await until(`${D}.buildPatch().objects.length === 1`, 20), 'Ctrl+Y did not redo the nudge');
  await sleep(1200);
  must(await layerN('SPT') === n0 + 1, 'Redo of the patch took the draft with it');
  await key('z', 'KeyZ', 90, 2);
  await ev(`${D}.select(null)`);
  await goMode('spt');
  await key('z', 'KeyZ', 90, 2);                   // in the mode it is the layer's own
  must(await until(`${D}.lay('SPT').points.length === ${n0}`, 20), 'Ctrl+Z in the mode did not undo the point');
  must(await ev(`${D}.buildPatch().objects.length`) === 0, 'Ctrl+Z in the mode touched the patch');
  must(await ev(`document.getElementById('undo').title.startsWith('Nothing to undo in start points')`), 'the Undo button does not speak for the layer: ' + await ev(`document.getElementById('undo').title`));
  await goMode('objects');
  must(await ev(`!document.getElementById('undo').title.includes(' in start points')`), 'the Undo button did not go back to the patch: ' + await ev(`document.getElementById('undo').title`));
  must(await ev(`${D}.unsavedCount()`) === 0, 'something is still unsaved');
});
await check('Move my changes to another mod keeps a draft that is in no mod yet', async () => {
  await ev(`${D}.layersReady()`);
  fs.mkdirSync(path.join(mods, 'smoke2'), { recursive: true });
  await goMode('spt');
  const n0 = await layerN('SPT');
  await ev(`document.getElementById('mkAdd').click()`);
  await ev(`${D}.moveTo('smoke2', false)`);
  await sleep(800);
  must(await layerN('SPT') === n0 + 1, 'moving to another mod dropped the draft');
  must(await ev(`document.querySelector('#mkState .tx').textContent`) === 'Not in this mod yet', 'the foot says: ' + await ev(`document.querySelector('#mkState .tx').textContent`));
  must(await ev(`${D}.unsavedCount()`) >= 1, 'the draft is not counted');
  await ev(`${D}.moveTo('smoke', false)`);
  await sleep(500);
  await ev(`document.getElementById('mkRevert').click()`);
  must(await until(`${D}.lay('SPT').points.length === ${n0}`, 40), 'Revert did not drop the draft');
  await goMode('objects');
  await ev(`${D}.loadStage(${JSON.stringify(stage)})`);      // the moved patch is empty, so there is nothing to save: open the stage again
  must(await until(`document.getElementById('busy').hidden && ${D}.items().length > 20 && ${D}.unsavedCount() === 0`, 240, 500), 'the stage did not open again clean');
  must(await ev(`${D}.modNow`) === 'smoke', 'the mod in use is ' + await ev(`${D}.modNow`));
});
await check('the Net links: a node shows its links, Link joins two nodes both ways, Two-way and the cut work from the right panel', async () => {
  await goMode('aip');
  must(await until(`${D}.lay('AIP').points.length > 2`, 80), 'the stage has too few AI net nodes');
  const pair = await ev(`(() => { const P = ${D}.lay('AIP').points; for (let a = 0; a < P.length; a++) for (let b = 0; b < P.length; b++)
    if (a !== b && !P[a].links.some((l) => l.to === b) && !P[b].links.some((l) => l.to === a)) return JSON.stringify([a, b]); return null; })()`);
  must(pair, 'every pair of nodes is linked already');
  const [a, b] = JSON.parse(pair);
  await ev(`${D}.selectMarker(${a})`);
  const l0 = await ev(`${D}.lay('AIP').points[${a}].links.length`);
  must(await ev(`document.querySelectorAll('#mkLinkList li.fop, #mkLinkList li:not(.in)').length`) >= l0, 'the right panel does not list the links');
  must(await ev(`!document.getElementById('mkLinkTo')`), 'the number field and its Link button are still there');
  await ev(`(() => { ${D}.linkTool.chain = false; ${D}.linkNodes(${a}, ${b}, false); ${D}.linkTool.chain = true; })()`);
  must(await ev(`${D}.lay('AIP').points[${a}].links.some((l) => l.to === ${b}) && ${D}.lay('AIP').points[${b}].links.some((l) => l.to === ${a})`), 'Link did not join both ways');
  must(await ev(`[...document.querySelectorAll('#mkLinkList li')].some((li) => li.querySelector('.to') && li.querySelector('.to').textContent === '→ Node ${b + 1}' && li.querySelector('input[type=checkbox]').checked)`), 'the link is not listed as two-way');
  await ev(`(() => { const li = [...document.querySelectorAll('#mkLinkList li')].find((x) => x.querySelector('.to') && x.querySelector('.to').textContent === '→ Node ${b + 1}'); const c = li.querySelector('input[type=checkbox]'); c.checked = false; c.dispatchEvent(new Event('change')); })()`);
  must(await ev(`${D}.lay('AIP').points[${a}].links.some((l) => l.to === ${b}) && !${D}.lay('AIP').points[${b}].links.some((l) => l.to === ${a})`), 'unchecking Two-way did not leave a one-way link');
  await ev(`[...document.querySelectorAll('#mkLinkList li')].find((x) => x.querySelector('.to') && x.querySelector('.to').textContent === '→ Node ${b + 1}').querySelector('button.icon').click()`);
  must(await ev(`!${D}.lay('AIP').points[${a}].links.some((l) => l.to === ${b})`), 'the cut kept the link');
  await ev(`document.getElementById('mkRevert').click()`);
  must(await until(`${D}.unsavedCount() === 0`, 40), 'Revert did not clean up');
  await goMode('objects');
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
      const why = await ev(`JSON.stringify({ focus: document.activeElement.id || document.activeElement.className || document.activeElement.tagName,
        inside: document.getElementById(${JSON.stringify(dlg)}).contains(document.activeElement), ask: document.getElementById('askDlg').open })`);
      await ev(`document.getElementById('askDlg').open && document.getElementById('askDlg').close(); document.getElementById(${JSON.stringify(dlg)}).close()`);   // do not leave it for the next case
      throw new Error('Esc did not close it ' + why);
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
    must(await until(`document.getElementById(${JSON.stringify(m)}).hidden`, 12), 'Esc did not close ' + m + ': ' + await ev(`JSON.stringify({ modal: [...document.querySelectorAll('dialog')].filter((d) => d.matches(':modal')).map((d) => d.id), open: [...document.querySelectorAll('dialog[open]')].map((d) => d.id), focus: document.activeElement.id || document.activeElement.tagName })`));
  }
});
await check('the right panel has Selection and Stage tabs; the stage tools are rows of the Stage tab; selecting switches back', async () => {
  must(await ev(`!document.getElementById('stageTab').hidden === false`), 'the Stage tab is open at the start');
  await ev(`document.getElementById('tabStage').click()`);
  must(await ev(`!document.getElementById('stageTab').hidden && document.getElementById('selTab').hidden`), 'the Stage tab did not open');
  const rows = await ev(`['gridBtn','lightBtn','texBtn','hud','picBtn'].filter((i) => { const b = document.getElementById(i); return !b || !document.getElementById('stageTab').contains(b) || !b.offsetParent; })`);
  must(rows.length === 0, 'not visible in the Stage tab: ' + rows.join(', '));
  must(await ev(`!document.getElementById('overlay').querySelector('#gridBtn, #picBtn')`), 'a stage tool is still on the view bar');
  must(await ev(`!document.getElementById('stageTab').querySelector('#sptBtn, #pupBtn, #aipBtn, #fopBtn') && ![...document.querySelectorAll('#stageTab .sec-title')].some((t) => t.textContent === 'Gameplay')`),
    'the Stage tab still has the Gameplay rows: start points, power-ups and the nets are modes of the rail');
  must(await ev(`!document.getElementById('sptDlg') && !document.getElementById('netDlg')`), 'a dock dialog for start points or nets is still there');
  must(await ev(`!document.getElementById('rail').querySelector('#texBtn, #lightBtn, #hud')`), 'a stage tool is still on the rail');
  must(await ev(`document.getElementById('tabStage').classList.contains('on') && !document.getElementById('tabSel').classList.contains('on')
    && document.getElementById('tabStage').getAttribute('aria-selected') === 'true'`), 'the tab buttons do not say which tab is open');
  await sleep(600);          // the buttons' color transition (a headless page draws few frames)
  await shot('stage_tab');
  await ev(`document.getElementById('texBtn').click()`);
  must(await until(`document.getElementById('texDlg').open`, 80), 'Textures did not open from the Stage tab');
  await esc();
  await ev(`window.wotmDebug.select(window.wotmDebug.items().find((x) => !x.copy))`);
  must(await ev(`document.getElementById('stageTab').hidden && !document.getElementById('selTab').hidden`), 'selecting did not bring the Selection tab back');
  await ev('window.wotmDebug.select(null)');
});
await check('Add lives at the top of the left panel: Browse models and the search button open the palette; terrain and import are in it; sky is on the Stage tab', async () => {
  must(await ev(`document.getElementById('leftHead').contains(document.getElementById('addProp'))`), 'Add is not in the left panel');
  must(await ev(`!document.getElementById('toolBar').querySelector('#addProp, #pedAddGroup')`), 'Add is still on the tool bar');
  await ev(`document.getElementById('addProp').click()`);
  must(await ev(`!document.getElementById('propMenu').hidden && document.getElementById('propMenu').parentElement === document.body`),
    'the Add menu is not open in the body: ' + await ev(`(() => { try { window.wotmDebug.showPropMenu(true); return 'ok hidden=' + document.getElementById('propMenu').hidden; } catch (e) { return String(e.stack).slice(0, 400); } })()`));
  must(await ev(`['pmBrowse','pmImport','pmTerrain'].every((i) => document.getElementById('propMenu').contains(document.getElementById(i)))`), 'the menu lacks Browse, Import or Terrain');
  must(await ev(`!document.getElementById('propMenu').querySelector('#pmSky, #pmClouds')`), 'sky and clouds are still in the Add menu');
  await ev(`document.getElementById('pmBrowse').click()`);
  must(await until(`document.getElementById('palDlg').open`, 40), 'Browse models did not open the palette');
  must(await ev(`document.getElementById('propMenu').hidden`), 'the menu stayed open over the palette');
  await esc();
  await ev(`document.getElementById('pmFind').click()`);
  must(await until(`document.getElementById('palDlg').open`, 40), 'the search button did not open the palette');
  await esc();
  await ev(`document.getElementById('tabStage').click()`);
  must(await ev(`document.getElementById('stageTab').contains(document.getElementById('pmSky')) && document.getElementById('pmSky').options.length >= 1`), 'the sky select is not filled on the Stage tab');
  // a mod with no model for the stage: the select offers the stage's own and the import, and the note says why there is no more (test_sky.mjs does the rest)
  must(await ev(`[...document.getElementById('pmSky').options].map((o) => o.value).join('|') === '|@import' && !document.getElementById('pmSkyNote').hidden`),
    'the sky select of an empty mod: ' + await ev(`[...document.getElementById('pmSky').options].map((o) => o.value).join('|') + ' / ' + document.getElementById('pmSkyNote').textContent`));
  await ev(`document.getElementById('tabSel').click()`);
  await ev(`document.getElementById('modePeds').click()`);
  must(await ev(`!document.getElementById('pedBar').hidden && document.getElementById('propBar').hidden && document.getElementById('leftHead').contains(document.getElementById('pedAddGroup'))`), 'Peds mode does not show Add group in the left head');
  await shot('left_head_peds');
  await ev(`document.getElementById('modeObjects').click()`);
  must(await ev(`document.getElementById('pedBar').hidden && !document.getElementById('propBar').hidden`), 'Objects mode does not bring Add back');
  await shot('left_head_objects');
});
await check('Display: game look, grid, minimap and the overlays are in one menu; start points, power-ups and the nets can be switched off, are faint in other modes and full in their own', async () => {
  must(await ev(`!document.getElementById('overlay').querySelector('#look, #grid')`), 'look or grid is still on the bar');
  await ev(`document.getElementById('layersBtn').click()`);
  const have = await ev(`['look','grid','miniOn','pedsOn','collOn','sptOn','pupOn','aipOn','fopOn'].filter((i) => !document.getElementById('layersMenu').contains(document.getElementById(i)))`);
  must(have.length === 0, 'not in the Display menu: ' + have.join(', '));
  const vis = (e) => ev(e);
  const opacity = (view) => `${view}.children[0].material.opacity`;
  must(await vis(`${D}.nets.AIP.view && ${D}.nets.AIP.view.visible`), 'the AI net is not drawn by default');
  must(await vis(`${opacity(`${D}.nets.AIP.view`)} < 0.5`), 'the AI net is not faint while the Objects mode is open');
  await ev(`(() => { const c = document.getElementById('aipOn'); c.click(); })()`);
  must(await vis(`!${D}.nets.AIP.view.visible`), 'the AI net stayed drawn with its switch off');
  await ev(`document.getElementById('aipOn').click()`);
  must(await vis(`${D}.nets.AIP.view.visible`), 'the AI net did not come back');
  must(await vis(`!!${D}.ptViews.SPT && ${D}.ptViews.SPT.visible`), 'start points are not drawn by default');
  await ev(`document.getElementById('sptOn').click()`);
  must(await vis(`!${D}.ptViews.SPT.visible`), 'start points stayed drawn');
  await ev(`document.getElementById('layersBtn').click()`);     // the menu first (its Esc comes before a mode's)
  await goMode('spt');                                          // its own mode draws them, full, whatever the switch says
  must(await vis(`${D}.ptViews.SPT.visible && ${opacity(`${D}.ptViews.SPT`)} > 0.8`), 'the Start points mode does not draw its start points in full');
  must(await vis(`!${D}.nets.AIP.view || ${opacity(`${D}.nets.AIP.view`)} < 0.5`), 'another layer is not faint in the Start points mode');
  must(await vis(`document.getElementById('sptOn').checked && document.getElementById('sptOn').disabled`), 'the Start points switch is not checked and locked in its own mode');
  await goMode('objects');
  must(await vis(`!${D}.ptViews.SPT.visible`), 'the start points stayed drawn after the mode was left: the switch is off');
  await ev(`document.getElementById('layersBtn').click()`);
  await ev(`document.getElementById('sptOn').click()`);
  must(await vis(`${D}.ptViews.SPT.visible`), 'start points did not come back');
  must(await vis(`!${D}.nets.FOP.view || !${D}.nets.FOP.view.visible`), 'the vehicle net is drawn by default');
  await goMode('fop');                                          // an empty or full vehicle net: its own mode draws it
  must(await vis(`!${D}.nets.FOP.points.length || ${D}.nets.FOP.view.visible`), 'the Vehicle net mode does not draw the vehicle net');
  await goMode('objects');
  await shot('display_menu');
  await ev(`document.getElementById('layersBtn').click()`);
});
await check('tool panels dock over the right panel, not modal: the width follows, one tool at a time, the right panel can be resized', async () => {
  const W = () => ev(`Math.round(document.getElementById('right').getBoundingClientRect().width)`);
  const w0 = await W();
  must(w0 < 400, 'the right panel is already wide: ' + w0);
  await ev(`document.getElementById('gridBtn').click()`);
  must(await until(`document.getElementById('gridDlg').open`, 40), 'Stage size did not open');
  must(await until(`document.getElementById('right').getBoundingClientRect().width >= 439`, 20), 'the right panel did not widen: ' + await W());
  const g = JSON.parse(await ev(`(() => { const d = document.getElementById('gridDlg').getBoundingClientRect(), r = document.getElementById('right').getBoundingClientRect();
    return JSON.stringify({ modal: !!document.querySelector('dialog:modal'), dl: Math.round(d.left), rl: Math.round(r.left), dt: Math.round(d.top), rt: Math.round(r.top), dh: Math.round(d.height), rh: Math.round(r.height) }); })()`));
  must(!g.modal, 'the panel is modal');
  must(Math.abs(g.dl - g.rl) <= 2 && Math.abs(g.dt - g.rt) <= 2 && Math.abs(g.dh - g.rh) <= 2, 'the panel is not where the right panel is: ' + JSON.stringify(g));
  // the camera is free while it is open: the aim takes where the view looks now
  must(await ev(`!!window.wotmDebug.aimStart()`), 'the view has no aim point with the panel open');
  await shot('dock_grid');
  // another tool: the first closes (nothing unsaved), the second opens
  await ev(`document.getElementById('lightBtn').click()`);
  must(await until(`document.getElementById('lightDlg') && document.getElementById('lightDlg').open && !document.getElementById('gridDlg').open`, 40), 'Light and fog did not replace Stage size');
  await shot('dock_light');
  await esc();
  must(await until(`!document.getElementById('lightDlg').open`, 20), 'the Light and fog panel did not close');
  must(await until(`document.getElementById('right').getBoundingClientRect().width <= ${w0 + 1}`, 20), 'the right panel did not go back: ' + await W());
  // the edge: the arrows resize, double click puts it back
  await ev(`document.getElementById('rightGrip').focus()`);
  await key('ArrowLeft', 'ArrowLeft', 37);
  must(await until(`Math.round(document.getElementById('right').getBoundingClientRect().width) === ${w0 + 16}`, 20), 'the arrow did not widen the panel: ' + await W());
  await ev(`document.getElementById('rightGrip').dispatchEvent(new MouseEvent('dblclick', { bubbles: true }))`);
  must(await until(`Math.round(document.getElementById('right').getBoundingClientRect().width) === ${w0}`, 20), 'a double click did not put it back: ' + await W());
});
await check('Enter in a field of New stage takes its main button (a bad id answers with its error, the dialog stays)', async () => {
  await ev(`document.getElementById('newStage').click()`);
  must(await until(`document.getElementById('stageDlg').open`, 40), 'New stage did not open');
  await ev(`(() => { const f = document.getElementById('nsId'); f.value = '1x'; f.focus(); })()`);
  await key('Enter', 'Enter', 13);
  must(await until(`!document.getElementById('nsErr').hidden`, 20), 'Enter did nothing: no error on the id');
  must(await ev(`document.getElementById('stageDlg').open`), 'the dialog closed on a bad id');
  await esc();
  must(await until(`!document.getElementById('stageDlg').open`, 20), 'Esc did not close New stage');
});
await check('the side panels hide and come back: the view takes their place, a selection or a tool brings the right one back', async () => {
  const W = (id) => ev(`Math.round(document.getElementById(${JSON.stringify(id)}).getBoundingClientRect().width)`);
  const view0 = await W('view'), left0 = await W('left'), right0 = await W('right');
  await ev(`document.getElementById('toggleRight').click()`);
  must(await until(`Math.round(document.getElementById('right').getBoundingClientRect().width) === 0`, 20), 'the right panel did not hide: ' + await W('right'));
  must(await W('view') >= view0 + right0 - 2, 'the view did not take the right panel\'s place');
  must(await ev(`document.getElementById('toggleRight').getAttribute('aria-pressed')`) === 'false', 'the button does not say it is hidden');
  await ev(`document.getElementById('gridBtn').click()`);                     // a tool needs the panel: it shows while the tool is open
  must(await until(`document.getElementById('gridDlg').open && document.getElementById('right').getBoundingClientRect().width >= 439`, 40), 'the tool did not bring the panel');
  await esc();
  must(await until(`!document.getElementById('gridDlg').open && Math.round(document.getElementById('right').getBoundingClientRect().width) === 0`, 40), 'the panel did not hide again after the tool');
  await ev(`window.wotmDebug.select(window.wotmDebug.items().find((x) => !x.copy))`);       // a selection brings it back
  must(await until(`document.getElementById('right').getBoundingClientRect().width >= 200`, 20), 'a selection did not bring the right panel back');
  await ev('window.wotmDebug.select(null)');
  await goMode('spt');                                                                       // a new marker is selected: that brings the right panel back too
  await ev(`document.getElementById('mkAdd').click()`);
  must(await until(`document.getElementById('right').getBoundingClientRect().width >= 200 && !document.getElementById('mkProps').hidden`, 20), 'a marker did not bring the right panel back');
  await ev(`document.getElementById('mkRevert').click()`);
  must(await until(`window.wotmDebug.unsavedCount() === 0`, 40), 'Revert did not clean up');
  await goMode('objects');
  await ev(`document.getElementById('toggleLeft').click()`);
  must(await until(`Math.round(document.getElementById('left').getBoundingClientRect().width) === 0`, 20), 'the left panel did not hide');
  await shot('left_hidden');
  await ev(`document.getElementById('toggleLeft').click()`);
  must(await until(`Math.round(document.getElementById('left').getBoundingClientRect().width) === ${left0}`, 20), 'the left panel did not come back');
  must(await until(`Math.round(document.getElementById('view').getBoundingClientRect().width) === ${view0}`, 20), 'the view is not as wide as before: ' + await W('view') + ' vs ' + view0);
});
const clickAt = async (x, y) => {
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
  await send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', buttons: 1, clickCount: 1 });
  await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', buttons: 0, clickCount: 1 });
  await sleep(150);
};
// the first marker of a kind that is on screen, clear of the bars and the panels: [index, [x, y]] as JSON, or null
const markerOnScreen = (kind, list) => ev(`(() => { const D = window.wotmDebug; const r = ${list}.map((p, i) => [i, D.markerScreen('${kind}', p)]).filter((x) => x[1]
  && x[1][0] > 330 && x[1][0] < innerWidth - 480 && x[1][1] > 130 && x[1][1] < innerHeight - 120);
  return r.length ? JSON.stringify(r[0]) : null; })()`);
await check('markers in the view: a start point is picked only in its own mode; the gizmo moves it; a click on nothing lets go and never picks a building; Esc deselects and keeps the mode', async () => {
  await goMode('objects');
  await ev(`${D}.frameAll()`);
  await sleep(500);
  const want = await markerOnScreen('SPT', 'D.lay("SPT").points');
  must(want, 'no start point on screen to click');
  const [idx, [sx, sy]] = JSON.parse(want);
  // Objects mode: the marker is scenery, so the click is never the marker's
  await ev(`${D}.select(null)`);
  await clickAt(sx, sy);
  must(await ev(`${D}.markerCtx() === null && ${D}.lay('SPT').sel === -1 && !${D}.marker.on`), 'a start point was picked in the Objects mode');
  await ev(`${D}.select(null)`);
  await goMode('spt');
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: sx, y: sy });
  must(await until(`${D}.hover.marker === ${idx}`, 20), 'the marker under the pointer is not the hovered one: ' + await ev(`${D}.hover.marker`));
  must(await ev(`document.getElementById('canvas').style.cursor`) === 'pointer', 'the pointer is not a hand over a marker');
  await clickAt(sx, sy);
  must(await until(`${D}.markerCtx() && ${D}.markerCtx().sel === ${idx}`, 20), 'the click did not select start point ' + idx);
  must(await ev(`${D}.marker.on && !!${D}.gizmoObject`), 'the gizmo is not on the marker');
  must(await ev(`!!document.querySelector('#mkList li.sel') && !document.getElementById('mkProps').hidden`), 'its row or the right panel is not showing it');
  await shot('marker_selected_spt');
  // the gizmo moves it: the point's numbers follow
  const x0 = await ev(`${D}.pt.SPT.points[${idx}].pos[0]`);
  await ev(`(() => { ${D}.gizmoObject.position.x += 50; ${D}.gizmo.dispatchEvent({ type: 'objectChange' }); })()`);
  const x1 = await ev(`${D}.pt.SPT.points[${idx}].pos[0]`);
  must(Math.abs(x1 - x0 - 50) < 0.2, `the point did not follow the gizmo: ${x0} -> ${x1}`);
  // a click on nothing (the corner of the view) lets go, and the building under it is not picked
  await clickAt(340, 140);
  must(await until(`${D}.markerCtx().sel === -1 && !${D}.marker.on`, 20), 'a click on nothing did not let go of the marker');
  must(await ev(`${D}.sel === null && ${D}.group.length === 0`), 'a click on nothing in a marker mode picked an object');
  // Esc, with the focus in the view: the marker lets go and the mode stays
  await clickAt(sx, sy);
  must(await until(`${D}.markerCtx().sel === ${idx}`, 20), 'the second click did not select');
  await key('Escape', 'Escape', 27);
  must(await until(`${D}.markerCtx().sel === -1`, 20), 'Esc did not let go of the marker');
  must(await ev(`${D}.mode === 'spt' && !document.getElementById('askDlg').open`), 'Esc left the mode or asked something');
  await ev(`document.getElementById('mkRevert').click()`);       // the moved point is a draft: back to the file's
  must(await until(`${D}.pt.SPT.points[${idx}].pos[0] === ${x0} && ${D}.unsavedCount() === 0`, 40), 'Revert did not put the point back');
  await goMode('objects');
});
await check('markers in the view: the same for the nodes of the AI net', async () => {
  await goMode('aip');
  await ev(`${D}.frameAll()`);
  await sleep(500);
  const want = await markerOnScreen('AIP', 'D.lay("AIP").points');
  must(want, 'no AI net node on screen to click');
  const [idx, [sx, sy]] = JSON.parse(want);
  await clickAt(sx, sy);
  must(await until(`${D}.markerCtx() && ${D}.markerCtx().sel === ${idx} && ${D}.marker.on`, 20), 'the click did not select node ' + idx);
  must(await ev(`document.getElementById('mkTitle').textContent === 'Node ${idx + 1}'`), 'the right panel does not name the node: ' + await ev(`document.getElementById('mkTitle').textContent`));
  await shot('marker_selected_net');
  const y0 = await ev(`${D}.nets.AIP.points[${idx}].pos[1]`);
  await ev(`(() => { ${D}.gizmoObject.position.z -= 40; ${D}.gizmo.dispatchEvent({ type: 'objectChange' }); })()`);     // game y is three -z
  const y1 = await ev(`${D}.nets.AIP.points[${idx}].pos[1]`);
  must(Math.abs(y1 - y0 - 40) < 0.2, `the node did not follow the gizmo: ${y0} -> ${y1}`);
  await key('Escape', 'Escape', 27);
  must(await until(`${D}.markerCtx().sel === -1`, 20), 'Esc did not let go of the node');
  await ev(`document.getElementById('mkRevert').click()`);
  must(await until(`${D}.nets.AIP.points[${idx}].pos[1] === ${y0}`, 40), 'Revert did not put the node back');
  await goMode('objects');
});
// the Link tool of the nets, by the mouse and the keys
const clickMod = async (x, y, modifiers = 0) => {
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y, modifiers });
  await send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', buttons: 1, clickCount: 1, modifiers });
  await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', buttons: 0, clickCount: 1, modifiers });
  await sleep(200);
};
const moveTo = async (x, y, modifiers = 0) => { await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y, modifiers }); await sleep(120); };
const onScreen = async (kind) => JSON.parse(await ev(`(() => { const D = window.wotmDebug; return JSON.stringify(D.lay('${kind}').points.map((p, i) => [i, D.markerScreen('${kind}', p)]).filter((x) => x[1]
  && x[1][0] > 330 && x[1][0] < innerWidth - 480 && x[1][1] > 130 && x[1][1] < innerHeight - 120)); })()`));
const linkedAny = (P, a, b) => P[a].links.some((l) => l.to === b) || P[b].links.some((l) => l.to === a);
const pts = async (kind) => JSON.parse(await ev(`JSON.stringify(${D}.lay('${kind}').points)`));
await check('the Link tool: with node 2 selected, a click on node 3 joins them; again cuts; Shift is one way; the ground adds a node; Esc leaves the tool first', async () => {
  await goMode('aip');
  await ev(`${D}.setView(false)`);
  await ev(`${D}.frameAll()`);
  await sleep(600);
  const on = await onScreen('AIP');
  let P = await pts('AIP');
  // a, b, c on screen, far apart from each other, with no link between them
  let trio = null;
  const far = (u, v) => Math.hypot(u[1][0] - v[1][0], u[1][1] - v[1][1]) > 70;
  for (const A of on) for (const B of on) for (const C of on) {
    if (trio || A[0] === B[0] || B[0] === C[0] || A[0] === C[0]) continue;
    if (P[A[0]].links.length < 5 && P[B[0]].links.length < 5 && P[C[0]].links.length < 5 && far(A, B) && far(B, C) && far(A, C) && !linkedAny(P, A[0], B[0]) && !linkedAny(P, B[0], C[0]) && !linkedAny(P, A[0], C[0])) trio = [A, B, C];
  }
  must(trio, 'no three unlinked AI net nodes on screen');
  const [[a, pa], [b, pb], [c, pc]] = trio;
  await ev(`${D}.selectMarker(${b})`);
  must(await ev(`document.getElementById('mkLinkRow').hidden === false && document.getElementById('mkLinkHow').hidden === false`), 'the Link controls are not on the left panel');
  must(await ev(`!!document.querySelector('#mkLinks #mkLink')`), 'the Link button is not on the right panel');
  await key('k', 'KeyK', 75);
  must(await until(`${D}.linkTool.on`, 20), 'K did not turn the Link tool on');
  must(await ev(`document.getElementById('mkLinkTool').getAttribute('aria-pressed') === 'true' && document.getElementById('mkLink').getAttribute('aria-pressed') === 'true'`), 'the buttons do not say the tool is on');
  must(await ev(`!${D}.marker.on`), 'the gizmo stays on the node while linking');
  must(await until(`${D}.hintKey === 'link'`, 20), 'the hint is ' + await ev(`${D}.hintKey`));
  // the pointer on node c: the rubber band runs from node b to c and the chip says what a click does
  await moveTo(pc[0], pc[1]);
  must(await until(`${D}.hover.marker === ${c}`, 20), 'the pointer is not on node ' + c);
  must(await until(`${D}.chipText === 'Link ${b + 1} and ${c + 1}'`, 20), 'the chip says: ' + await ev(`${D}.chipText`));
  must(await ev(`${D}.netRubber.visible && !document.getElementById('linkChip').hidden`), 'no rubber band or chip');
  await ev(`document.querySelectorAll('#toasts .toast').forEach((t) => t.remove())`);
  await shot('link_tool_hover');
  await moveTo(pc[0], pc[1], 8);
  must(await until(`${D}.chipText === 'Link ${b + 1} to ${c + 1}, one way'`, 20), 'with Shift the chip says: ' + await ev(`${D}.chipText`));
  await moveTo(pb[0], pb[1]);
  must(await until(`${D}.hover.marker === ${b} && ${D}.chipText === '' && !${D}.netRubber.visible`, 20), 'the selected node itself shows a band: ' + await ev(`${D}.chipText`));
  // the width of New links goes on the new link; both ways; the selection goes on to the node linked (Go on is on)
  await ev(`(() => { const w = document.getElementById('mkWidth'); w.value = 33; w.dispatchEvent(new Event('input')); })()`);
  await clickMod(pc[0], pc[1]);
  P = await pts('AIP');
  must(P[b].links.some((l) => l.to === c && l.width === 33) && P[c].links.some((l) => l.to === b && l.width === 33), 'the click did not join node ' + b + ' and ' + c + ' both ways with the width 33');
  must(await ev(`${D}.markerCtx().sel === ${c}`), 'the selection did not go on to node ' + c);
  must(await ev(`[...document.querySelectorAll('#mkLinkList li')].some((li) => li.querySelector('.to') && li.querySelector('.to').textContent === '→ Node ${b + 1}')`), 'the right panel does not list the link of the new selection');
  // Ctrl+Z takes the link back (the tool stays on)
  await key('z', 'KeyZ', 90, 2);
  must(await until(`!${D}.lay('AIP').points[${b}].links.some((l) => l.to === ${c})`, 20), 'Ctrl+Z did not take the link back');
  must(await ev(`${D}.linkTool.on && ${D}.markerCtx().sel === ${b}`), 'the undo changed the tool or the selection');
  // again, then a click on the node it is joined to cuts it, both ways
  await clickMod(pc[0], pc[1]);
  await clickMod(pb[0], pb[1]);
  P = await pts('AIP');
  must(!linkedAny(P, b, c), 'a click on a linked node did not cut the link');
  must(await ev(`${D}.markerCtx().sel === ${c}`), 'a cut moved the selection');
  // Shift: one way, out of the selected node (c) to b; the arrow is drawn; Shift on it again cuts that direction only
  await clickMod(pb[0], pb[1], 8);
  P = await pts('AIP');
  must(P[c].links.some((l) => l.to === b) && !P[b].links.some((l) => l.to === c), 'Shift+click did not make a link out of node ' + c + ' alone');
  must(await ev(`${D}.nets.AIP.view.userData.arrows && ${D}.nets.AIP.view.userData.arrows.links.length >= 1`), 'no arrow for the one-way link');
  await ev(`${D}.selectMarker(${c})`);
  await clickMod(pb[0], pb[1], 8);
  P = await pts('AIP');
  must(!linkedAny(P, b, c), 'Shift+click on a one-way link did not cut it');
  // Go on off: the selection stays
  await ev(`document.getElementById('mkChain').click()`);
  must(await ev(`${D}.linkTool.chain === false`), 'the Go on option did not turn off');
  await ev(`${D}.selectMarker(${a})`);
  await clickMod(pb[0], pb[1]);
  must(await ev(`${D}.markerCtx().sel === ${a} && ${D}.lay('AIP').points[${a}].links.some((l) => l.to === ${b})`), 'with Go on off the selection should stay on node ' + a);
  await ev(`document.getElementById('mkChain').click()`);
  // a row of the list links too
  await ev(`${D}.selectMarker(${a})`);
  await ev(`[...document.querySelectorAll('#mkList li')].find((li) => li._mk === ${c}).click()`);
  must(await ev(`${D}.lay('AIP').points[${a}].links.some((l) => l.to === ${c}) && ${D}.lay('AIP').points[${c}].links.some((l) => l.to === ${a})`), 'a click on the row of node ' + c + ' did not link');
  // a node that is full: refused, named, nothing changed
  await ev(`document.getElementById('mkRevert').click()`);
  must(await until(`${D}.unsavedCount() === 0`, 40), 'Revert did not clean up');
  await ev(`(() => { const f = ${D}.lay('AIP').points[${c}]; let t = 0; while (f.links.length < 6) { if (t !== ${c} && t !== ${b} && !f.links.some((l) => l.to === t)) f.links.push({ to: t, width: 5 }); t++; } })()`);
  await ev(`${D}.selectMarker(${b})`);
  const before = await ev(`JSON.stringify(${D}.lay('AIP').points)`);
  await ev(`${D}.linkNodes(${b}, ${c}, false)`);
  must(before === await ev(`JSON.stringify(${D}.lay('AIP').points)`), 'a link to a full node changed the net');
  must(await ev(`!document.getElementById('mkErr').hidden && document.getElementById('mkErr').textContent.includes('Node ${c + 1} already has 6 links')`), 'the refusal does not name the node: ' + await ev(`document.getElementById('mkErr').textContent`));
  await ev(`document.getElementById('mkRevert').click()`);
  must(await until(`${D}.unsavedCount() === 0`, 40), 'Revert did not clean up');
  // the ground: a click on the stage puts a new node there, of this node's type, joined both ways, and selects it
  await ev(`${D}.setLinkTool(true)`);
  await ev(`${D}.selectMarker(${a})`);
  const n0 = await layerN('AIP');
  const [W, H] = JSON.parse(await ev(`JSON.stringify([innerWidth, innerHeight])`));
  let spot = null;
  for (let y = 200; y < H - 150 && !spot; y += 60) for (let x = 400; x < W - 520 && !spot; x += 70) {
    if (on.some((m) => Math.hypot(m[1][0] - x, m[1][1] - y) < 50)) continue;
    await moveTo(x, y);
    if (await ev(`${D}.chipText === 'New node'`)) spot = [x, y];
  }
  must(spot, 'no ground to put a node on');
  await shot('link_tool_new_node');
  await clickMod(spot[0], spot[1]);
  must(await layerN('AIP') === n0 + 1, 'the click on the ground did not add a node');
  P = await pts('AIP');
  const nn = P.length - 1;
  must(P[nn].type === P[a].type && P[a].links.some((l) => l.to === nn) && P[nn].links.some((l) => l.to === a), 'the new node is not joined both ways to node ' + a + ' with its type');
  must(await ev(`${D}.markerCtx().sel === ${nn}`), 'the new node is not the selected one');
  await key('z', 'KeyZ', 90, 2);
  must(await until(`${D}.lay('AIP').points.length === ${n0}`, 20), 'Ctrl+Z did not take the new node back');
  // Esc: the tool first, then the selection
  await key('Escape', 'Escape', 27);
  must(await until(`!${D}.linkTool.on`, 20), 'Esc did not leave the Link tool');
  must(await ev(`${D}.markerCtx().sel === ${a}`), 'the first Esc let go of the selection');
  await key('Escape', 'Escape', 27);
  must(await until(`${D}.markerCtx().sel === -1`, 20), 'the second Esc did not deselect');
  // Ctrl held is the tool without turning it on
  await ev(`${D}.selectMarker(${a})`);
  await clickMod(pc[0], pc[1], 2);
  must(await ev(`${D}.lay('AIP').points[${a}].links.some((l) => l.to === ${c})`), 'Ctrl+click did not link');
  await ev(`document.getElementById('mkRevert').click()`);
  must(await until(`${D}.unsavedCount() === 0`, 40), 'Revert did not clean up');
  await goMode('objects');
});
await check('the Link tool in the vehicle net: a new link takes the Width and the Drop of New links; Add makes a node of the selected type', async () => {
  await goMode('fop');
  must(await until(`${D}.lay('FOP').points.length > 3`, 80), 'the stage has too few vehicle net nodes');
  const P = await pts('FOP');
  let pair = null;
  for (let x = 0; x < P.length && !pair; x++) for (let y = 0; y < P.length && !pair; y++) if (x !== y && !linkedAny(P, x, y) && P[x].links.length < 5 && P[y].links.length < 5 && P[x].type === 1 && P[y].type === 1) pair = [x, y];
  if (!pair) for (let x = 0; x < P.length && !pair; x++) for (let y = 0; y < P.length && !pair; y++) if (x !== y && !linkedAny(P, x, y) && P[x].links.length < 5 && P[y].links.length < 5) pair = [x, y];
  must(pair, 'every pair of nodes is linked already');
  const [a, b] = pair;
  await ev(`(() => { for (const [id, v] of [['mkWidth', 12], ['mkDropN', 26]]) { const e = document.getElementById(id); e.value = v; e.dispatchEvent(new Event('input')); } })()`);
  await ev(`${D}.selectMarker(${a})`);
  await ev(`${D}.setLinkTool(true)`);
  await ev(`${D}.linkNodes(${a}, ${b}, false)`);
  const Q = await pts('FOP');
  must(Q[a].links.some((l) => l.to === b && l.width === 12 && l.drop === 26) && Q[b].links.some((l) => l.to === a && l.width === 12 && l.drop === 26), 'the new link does not take 12 and 26: ' + JSON.stringify([a, b, Q[a].links, Q[b].links, await ev(`[document.getElementById('mkErr').textContent, ${D}.markerCtx().kind, ${D}.markerCtx().sel, ${D}.lay('FOP').caps, ${D}.linkTool.on]`)]));
  await ev(`${D}.setLinkTool(false)`);
  must(await ev(`document.getElementById('mkNewType').value === '${Q[b].type}'`), 'the type of the next new node does not follow the selected node: ' + await ev(`document.getElementById('mkNewType').value`));
  const n0 = await layerN('FOP');
  await ev(`document.getElementById('mkAdd').click()`);
  const R = await pts('FOP');
  must(R.length === n0 + 1 && R[n0].type === Q[b].type && R[n0].links.some((l) => l.to === b) && R[b].links.some((l) => l.to === n0), 'Add did not make a linked node of the selected type');
  must(await ev(`${D}.markerCtx().sel === ${n0}`), 'the new node is not selected, so that Add again would not go on from it');
  await ev(`document.getElementById('mkRevert').click()`);
  must(await until(`${D}.unsavedCount() === 0`, 40), 'Revert did not clean up');
  await goMode('objects');
});
// the labels the view should show for a layer's points, worked out here from the rules (a start point: its place in its list)
const SPT_NAMES = ['Story', 'Player', 'CPU', 'Respawn'];
const expectSptNames = (pts, caps) => pts.map((p, i) => {
  const n = pts.slice(0, i + 1).filter((q) => q.list === p.list).length;
  return (p.list === 0 ? 'Story' : SPT_NAMES[p.list] + ' ' + n) + (n > caps[p.list] ? ' · past room' : '');
});
const labelsNow = async () => JSON.parse(await ev('JSON.stringify(window.wotmDebug.markerLabels())'));
await check('start points in the view: a label over each cone names it, and goes with the camera', async () => {
  await goMode('spt');
  await ev(`${D}.setView(false)`);
  await ev(`${D}.frameAll()`);
  await ev(`${D}.selectMarker(-1)`);
  const pts = JSON.parse(await ev(`JSON.stringify(${D}.pt.SPT.points)`)), caps = JSON.parse(await ev(`JSON.stringify(${D}.pt.SPT.caps)`));
  must(pts.length > 0, 'the stage has no start points');
  const names = expectSptNames(pts, caps);
  const have = await ev(`JSON.stringify(${D}.ptViews.SPT.userData.names)`);
  must(have === JSON.stringify(names), `the names of the cones are not the places in their lists: ${have} vs ${JSON.stringify(names)}`);
  must(await until(`${D}.markerLabels().length > 0`, 20), 'no label is drawn over the cones');
  const shown = await labelsNow();
  must(shown.every((l) => names.includes(l.text)), 'a label is not a start point name: ' + JSON.stringify(shown.map((l) => l.text)));
  must(new Set(shown.map((l) => l.text)).size === shown.length, 'the same name is drawn twice');
  must(await ev(`[...document.querySelectorAll('.mk-label')].every((e) => getComputedStyle(e).pointerEvents === 'none')`), 'a label takes the pointer');
  // a label sits over its marker: over the cone's tip (which leans to a side when the view is oblique), its bottom above the cone's centre
  const over = await ev(`(() => { const D = window.wotmDebug, bad = []; for (const e of document.querySelectorAll('.mk-label:not([hidden])')) { const b = e.getBoundingClientRect();
    const i = D.ptViews.SPT.userData.names.indexOf(e.textContent); const m = D.markerPx(i); if (!m || Math.abs((b.left + b.right) / 2 - m.x) > m.rPx || b.bottom > m.y) bad.push([e.textContent, m && Math.round((b.left + b.right) / 2 - m.x), m && Math.round(b.bottom - m.y)]); } return JSON.stringify(bad); })()`);
  must(over === '[]', 'a label is not over its cone: ' + over);
  await shot('markers_spt_labels');
  // the selected one is labeled, ringed, bigger and keeps its color
  const idx = pts.length - 1;
  await ev(`${D}.selectMarker(${idx})`);
  must(await until(`${D}.markerLabels().some((l) => l.cls.includes('sel'))`, 20), 'the selected start point has no label');
  must(await ev(`${D}.mkSelRing.visible`), 'the selected start point has no ring');
  must(await ev(`${D}.ptViews.SPT.children.filter((m) => m.userData.mk).every((m) => m.material.color.getHex() !== 0xFFFFFF)`), 'a cone turned white');
  must(await ev(`${D}.ptViews.SPT.children.find((m) => m.userData.mk && m.userData.mk.i === ${idx}).userData.mk.f > 1`), 'the selected cone is not bigger');
  await ev(`${D}.selectMarker(-1)`);
  // behind the camera: no label
  await ev(`${D}.setView(true)`);
  await ev(`${D}.fly.yaw += Math.PI`);
  must(await until(`${D}.markerLabels().length === 0`, 40), 'labels are drawn for markers behind the camera: ' + JSON.stringify(await labelsNow()));
  await ev(`${D}.fly.yaw -= Math.PI`);
  // the other modes: the cones are faint scenery with their labels, when the list is short
  await goMode('objects');
  const few = pts.length <= 12;
  must(await until(`(${D}.markerLabels().length > 0) === ${few}`, 40), `the labels over the faint cones are not as expected for ${pts.length} start points`);
  must(await ev(`[...document.querySelectorAll('.mk-label:not([hidden])')].every((e) => e.classList.contains('dim'))`), 'a label of the faint cones is not dimmed');
  await ev(`${D}.setView(false)`);
});
await check('markers in the view: a click inside the visible cone picks it when the camera is close', async () => {
  await goMode('spt');
  await ev(`${D}.setView(true)`);
  const i = await ev(`${D}.pt.SPT.points.length - 1`);
  await ev(`${D}.selectMarker(${i})`);
  await ev(`${D}.focusSelection()`);
  await sleep(900);
  await ev(`${D}.selectMarker(-1)`);
  await sleep(300);
  const m = JSON.parse(await ev(`JSON.stringify(${D}.markerPx(${i}))`));
  must(m && m.rPx > 30, 'the camera did not come close to the cone: ' + JSON.stringify(m));
  // 80% of the way to the tip: inside the cone, further from its centre than the old 18 px
  const y = m.y - m.rPx * 0.8;
  must(y > 130, 'the test point is under the bars');
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: m.x, y });
  must(await until(`${D}.hover.marker === ${i}`, 20), 'the pointer inside the cone is not on it: ' + await ev(`${D}.hover.marker`));
  await clickAt(m.x, y);
  must(await until(`${D}.markerCtx().sel === ${i}`, 20), `a click inside the cone, ${Math.round(m.rPx * 0.8)} px from its centre, did not pick it`);
  must(await ev(`${D}.sel === null`), 'a building was picked');
  // far out, the click still finds a marker of a few pixels
  await ev(`${D}.selectMarker(-1)`);
  await ev(`${D}.frameAll()`);
  await sleep(500);
  const far = JSON.parse(await ev(`JSON.stringify(${D}.markerPx(${i}))`));
  if (far && far.x > 330 && far.x < 1200 && far.y > 130) {
    await clickAt(far.x + 7, far.y + 5);
    must(await until(`${D}.markerCtx().sel === ${i}`, 20), 'a click 9 px off a far marker did not pick it');
  }
  await ev(`${D}.selectMarker(-1)`);
  await goMode('objects');
  await ev(`${D}.setView(false)`);
});
await check('power-ups in the view: health green, stamina blue, special weapon red, the cloak a gray cube; the rows and the legend agree', async () => {
  await goMode('pup');
  await ev(`${D}.setView(false)`);
  const L = `${D}.lay('PUP')`;
  const at = JSON.parse(await ev(`JSON.stringify(${L}.points[0] ? ${L}.points[0].pos : [0, 0, 0])`));
  // one of each kind of power-up, beside the stage's own
  const types = [0, 1, 2, 6, 3, 4, 5, 7, 8, 9, 10];
  const n0 = await ev(`${L}.points.length`);
  await ev(`(() => { const D = window.wotmDebug, L = D.lay('PUP'); ${JSON.stringify(types)}.forEach((t, k) => L.points.push({ type: t, pos: [${at[0]} + k * 120, ${at[1]}, ${at[2]}], flags: 7, w: 0, sel: 0 })); D.selectMarker(-1); })()`);
  const want = { 0: 0x4AE08A, 1: 0x4AE08A, 2: 0x4AE08A, 6: 0x4AE08A, 3: 0x36C5F0, 4: 0x36C5F0, 5: 0x36C5F0, 7: 0x36C5F0, 8: 0xF0605A, 9: 0xA8B0BC, 10: 0xFFC233 };
  const got = JSON.parse(await ev(`JSON.stringify(${D}.lay('PUP').points.map((p, i) => { const m = ${D}.ptViews.PUP.children.find((c) => c.userData.mk && c.userData.mk.i === i); return [p.type, m.material.color.getHex(), m.geometry.type]; }))`));
  for (const [type, hex, geo] of got) {
    if (want[type] !== undefined) must(hex === want[type], `power-up type ${type} is ${hex.toString(16)}, not ${want[type].toString(16)}`);
    must((type === 9) === (geo === 'BoxGeometry'), `power-up type ${type} is a ${geo}`);
  }
  must([8, 9, 10].every((t) => got.some(([ty]) => ty === t)), 'the test power-ups are missing');
  // the rows carry the same color and shape
  await ev(`${D}.setMode('spt')`);
  await goMode('pup');
  const rows = JSON.parse(await ev(`JSON.stringify([...document.querySelectorAll('#mkList li.it')].map((li) => [li.querySelector('.shape').className, li.querySelector('.shape').style.getPropertyValue('--c')]))`));
  must(rows.length >= types.length, 'the list has fewer rows than power-ups');
  must(rows.some(([cls, c]) => cls.includes('cube') && c === '#a8b0bc'), 'the cloak row is not a gray cube');
  must(rows.some(([cls, c]) => cls.includes('ball') && c === '#f0605a'), 'no red ball in the rows');
  must(rows.some(([cls, c]) => cls.includes('ball') && c === '#4ae08a'), 'no green ball in the rows');
  must(rows.some(([cls, c]) => cls.includes('ball') && c === '#36c5f0'), 'no blue ball in the rows');
  // the legend, with nothing selected
  const leg = JSON.parse(await ev(`JSON.stringify([...document.querySelectorAll('#mkLegend li')].map((li) => [li.textContent.trim(), li.querySelector('.shape').className.replace('shape ', ''), li.querySelector('.shape').style.getPropertyValue('--c')]))`));
  must(JSON.stringify(leg) === JSON.stringify([['Health', 'ball', '#4ae08a'], ['Stamina', 'ball', '#36c5f0'], ['Special weapon', 'ball', '#f0605a'], ['Cloak', 'cube', '#a8b0bc'], ['Story items', 'ball', '#ffc233']]), 'the legend is ' + JSON.stringify(leg));
  // the selected one keeps its color and is named
  const pick = n0 + types.indexOf(8);
  await ev(`${D}.selectMarker(${pick})`);
  must(await ev(`${D}.ptViews.PUP.children.find((c) => c.userData.mk && c.userData.mk.i === ${pick}).material.color.getHex() === 0xF0605A`), 'the selected power-up lost its color');
  must(await until(`${D}.markerLabels().some((l) => l.cls.includes('sel'))`, 20), 'the selected power-up has no label');
  await ev(`${D}.selectMarker(-1)`);
  await ev(`${D}.frameAll()`);
  await sleep(600);
  await shot('markers_pup_colors');
  await ev(`document.getElementById('mkRevert').click()`);
  await goMode('objects');
});
await check('net nodes in the view: every node has its number, the selected one its name, its links are amber and a one-way link has an arrow; a row lights its node', async () => {
  await goMode('aip');
  await ev(`${D}.setView(false)`);
  await ev(`${D}.frameAll()`);
  await ev(`${D}.selectMarker(-1)`);
  const n = await ev(`${D}.lay('AIP').points.length`);
  must(n > 1, 'the stage has no AI net');
  must(await until(`${D}.markerLabels().length > 0`, 40), 'no node is numbered');
  const shown = await labelsNow();
  must(shown.every((l) => /^[1-9][0-9]*( |$)/.test(l.text) && +l.text.split(' ')[0] <= n), 'a label is not a node number: ' + JSON.stringify(shown.slice(0, 5)));
  // a row under the pointer lights its node: the ring and the name
  const idx = Math.min(2, n - 1);
  const rowEvent = (type) => ev(`(() => { const li = [...document.querySelectorAll('#mkList li.it')].find((l) => l._mk === ${idx}); li.dispatchEvent(new MouseEvent('${type}')); })()`);
  await rowEvent('mouseenter');
  must(await until(`${D}.hover.shown === ${idx} && ${D}.mkRing.visible`, 40), 'a row under the pointer did not light its node');
  must(await until(`${D}.markerLabels().some((l) => l.cls.includes('hot') && l.text.startsWith('${idx + 1} '))`, 40), 'the node of the row has no name: ' + JSON.stringify(await labelsNow()));
  await rowEvent('mouseleave');
  must(await until(`${D}.hover.shown === -1`, 40), 'the light stayed on after the pointer left the row');
  // select a node with links: its links are amber, the others dimmer, its neighbours bigger
  const k = await ev(`window.wotmDebug.lay('AIP').points.findIndex((p) => p.links.length > 0)`);
  must(k >= 0, 'no node has a link');
  await ev(`${D}.selectMarker(${k})`);
  must(await until(`${D}.markerLabels().some((l) => l.cls.includes('sel') && l.text.startsWith('${k + 1} '))`, 40), 'the selected node has no name');
  const st = JSON.parse(await ev(`(() => { const D = window.wotmDebug, g = D.nets.AIP.view, P = D.lay('AIP').points;
    const amber = g.children.filter((c) => c.isLineSegments && c.material.color.getHex() === 0xFFC233);
    const nb = P[${k}].links.map((l) => l.to), big = g.children.filter((c) => c.userData.mk && c.userData.mk.f > 1).map((c) => c.userData.mk.i);
    return JSON.stringify({ amber: amber.length, nb, big, selF: g.userData.f[${k}] }); })()`));
  must(st.amber > 0, "the selected node's links are not amber");
  must(st.selF > 1 && st.nb.every((j) => st.big.includes(j)), 'the selected node and its neighbours are not bigger: ' + JSON.stringify(st));
  must(await ev(`${D}.mkSelRing.visible`), 'the selected node has no ring');
  // an arrow for each one-way link
  const count = () => ev(`(() => { const D = window.wotmDebug, P = D.lay('AIP').points; let one = 0; P.forEach((p, i) => p.links.forEach((l) => { if (!P[l.to].links.some((m) => m.to === i)) one++; }));
    const A = D.nets.AIP.view.userData.arrows; return JSON.stringify([one, A ? A.links.length : 0]); })()`);
  const a1 = JSON.parse(await count());
  must(a1[0] === a1[1], 'the arrows are not one for each one-way link: ' + a1);
  // make one: a link from the last node to the first that does not return
  await ev(`(() => { const D = window.wotmDebug, P = D.lay('AIP').points, a = P.length - 1, b = 0; if (!P[a].links.some((l) => l.to === b)) P[a].links.push({ to: b, width: 20 });
    P[b].links = P[b].links.filter((l) => l.to !== a); D.selectMarker(${k}); })()`);
  const a2 = JSON.parse(await count());
  must(a2[0] >= 1 && a2[0] === a2[1], 'a one-way link has no arrow: ' + a2);
  must(await until(`(() => { const A = window.wotmDebug.nets.AIP.view.userData.arrows; return !!A && A.mesh.count === A.links.length; })()`, 20), 'the arrows are not instanced');
  await shot('markers_net_numbers');
  await ev(`${D}.selectMarker(-1)`);
  await ev(`document.getElementById('mkRevert').click()`);
  await goMode('objects');
});
await check('top bar: File menu holds the stage and mod commands; Test has a chevron for Test from here and Test options', async () => {
  must(await ev(`!document.getElementById('bar').querySelector('#newStage, #editStage, #packMod, #check, #testHere, #testOpts')`), 'a command is still loose on the bar');
  await ev(`document.getElementById('fileBtn').click()`);
  const m = await ev(`(() => { const f = document.getElementById('fileMenu'); const r = f.getBoundingClientRect(); return JSON.stringify({ open: !f.hidden, body: f.parentElement === document.body,
    all: ['newStage','editStage','packMod','check'].every((i) => f.contains(document.getElementById(i))), under: r.top >= document.getElementById('fileBtn').getBoundingClientRect().bottom, inside: r.right <= innerWidth }); })()`);
  must(m === '{"open":true,"body":true,"all":true,"under":true,"inside":true}', 'File menu: ' + m);
  await shot('file_menu');
  await ev(`document.getElementById('newStage').click()`);
  must(await until(`document.getElementById('stageDlg').open && document.getElementById('fileMenu').hidden`, 40), 'New stage did not open, or the menu stayed open');
  await esc();
  await ev(`document.getElementById('testMenuBtn').click()`);
  must(await ev(`!document.getElementById('testMenu').hidden && ['testHere','testOpts'].every((i) => document.getElementById('testMenu').contains(document.getElementById(i)))`), 'the Test menu is not as expected');
  await shot('test_menu');
  await ev(`document.getElementById('testOpts').click()`);
  must(await until(`document.getElementById('testDlg').open && document.getElementById('testMenu').hidden`, 40), 'Test options did not open or the menu stayed');
  must(await ev(`document.getElementById('testTitle').textContent`) === 'Test options', 'the dialog is still titled "Test in game"');
  await esc();
  must(await ev(`document.getElementById('reload').hidden`), 'Reload in game shows with no game open');
});
await check('the status bar runs the check when there is none, and shows its result', async () => {
  must(await ev(`document.querySelector('#status .check').title.startsWith('Run the check')`), 'the status field does not offer to run it');
  await ev(`document.querySelector('#status .check').click()`);
  must(await until(`window.wotmDebug.ui.check.state === 'ok' || window.wotmDebug.ui.check.state === 'bad'`, 240, 500), 'the check did not finish');
});
await check('menus: one open at a time, a click outside closes, arrows walk, Esc gives the focus back to the button', async () => {
  await ev(`document.getElementById('addProp').click()`);
  must(await ev(`!document.getElementById('propMenu').hidden`), 'Add prop did not open');
  await ev(`document.getElementById('layersBtn').click()`);
  must(await ev(`document.getElementById('propMenu').hidden && !document.getElementById('layersMenu').hidden`), 'opening Layers did not close Add prop');
  must(await ev(`document.getElementById('layersBtn').getAttribute('aria-expanded')`) === 'true', 'aria-expanded is not true on the open menu');
  must(await ev(`document.getElementById('addProp').getAttribute('aria-expanded')`) === 'false', 'aria-expanded stayed true on the closed menu');
  await ev(`document.querySelector('#layersMenu input[type=checkbox]').focus()`);
  const first = await ev('document.activeElement.id');
  await key('ArrowDown', 'ArrowDown', 40);
  must(await ev('document.activeElement.id') !== first, 'ArrowDown did not move inside the menu');
  await esc();
  must(await ev(`document.getElementById('layersMenu').hidden`), 'Esc did not close the menu');
  must(await ev(`document.activeElement.id`) === 'layersBtn', 'the focus did not go back to the button');
  await ev(`document.getElementById('layersBtn').click()`);
  await send('Input.dispatchMouseEvent', { type: 'mousePressed', x: 700, y: 500, button: 'left', clickCount: 1 });
  await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: 700, y: 500, button: 'left', clickCount: 1 });
  must(await until(`document.getElementById('layersMenu').hidden`, 10), 'a click outside did not close the menu');
});
await check('the bars over the view never cover each other or the menus, from 1600 wide down to 900', async () => {
  const size = (w, h) => send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false });
  try {
    for (const [w, h] of [[1600, 900], [1100, 650], [900, 600]]) {
      await size(w, h);
      await sleep(500);
      const r = JSON.parse(await ev(`(() => { const R = (id) => document.getElementById(id).getBoundingClientRect();
        const a = R('toolBar'), b = R('overlay'), top = R('viewTop'), v = R('view');
        const hit = (p, q) => p.left < q.right && q.left < p.right && p.top < q.bottom && q.top < p.bottom;
        document.getElementById('addProp').click(); const pm = R('propMenu'); document.getElementById('addProp').click();
        document.getElementById('layersBtn').click(); const lm = R('layersMenu'); document.getElementById('layersBtn').click();
        return JSON.stringify({ bars: hit(a, b), propMenuUnder: pm.top >= R('addProp').bottom && pm.left >= 0 && pm.right <= innerWidth, layersUnder: lm.top >= b.bottom, inside: b.right <= v.right + 1 && a.left >= v.left - 1, vt: getComputedStyle(document.getElementById('view')).getPropertyValue('--vt') }); })()`));
      must(!r.bars, `${w}x${h}: the tool bar and the view bar overlap`);
      must(r.propMenuUnder, `${w}x${h}: the Add menu does not open under its button, inside the window`);
      must(r.layersUnder, `${w}x${h}: the Layers menu opens over the view bar`);
      must(r.inside, `${w}x${h}: a bar runs out of the view`);
      must(r.vt.trim() !== '', `${w}x${h}: --vt is not set`);
      await shot(`bars_${w}x${h}`);
    }
  } finally { await size(1600, 900); await sleep(300); }
});
await check('the object list works from the keyboard: section header, arrows, Enter selects, the row keeps the focus', async () => {
  await ev('window.wotmDebug.select(null)');
  await ev(`document.querySelector('#list li.sec').focus()`);
  await key('ArrowDown', 'ArrowDown', 40);
  must(await ev(`document.activeElement.classList.contains('it')`), 'ArrowDown did not reach a row');
  await key('ArrowDown', 'ArrowDown', 40);
  const want = await ev(`document.activeElement._item.rec.info.handle`);
  await key('Enter', 'Enter', 13);
  must(await ev('window.wotmDebug.sel && window.wotmDebug.sel.rec.info.handle') === want, 'Enter did not select the row');
  must(await ev(`document.activeElement.classList.contains('it') && document.activeElement._item.rec.info.handle === ${JSON.stringify(want)}`), 'the row lost the focus when the list was redrawn');
  must(await ev(`document.activeElement.getAttribute('aria-selected')`) === 'true', 'the row is not aria-selected');
  await key('ArrowUp', 'ArrowUp', 38);
  must(await ev('window.wotmDebug.buildPatch().objects.length') === 0, 'an arrow in the list also moved the selected object');
  await key('Home', 'Home', 36);
  must(await ev(`document.activeElement.classList.contains('sec')`), 'Home did not go to the first header');
  await ev('window.wotmDebug.select(null); document.activeElement.blur()');
});
await check('the Pedestrians mode and back', async () => {
  await ev('document.getElementById("modePeds").click()');
  must(await until(`window.wotmDebug.mode === 'peds' || window.wotmDebug.mode === 'pedestrians'`, 20), 'mode: ' + await ev('window.wotmDebug.mode'));
  await shot('mode_peds');
  await ev('document.getElementById("modeObjects").click()');
  must(await until(`window.wotmDebug.mode === 'objects'`, 20), 'mode: ' + await ev('window.wotmDebug.mode'));
});
const reloadPage = async () => {
  await send('Page.navigate', { url: `http://127.0.0.1:${PORT}/?debug` });
  await sleep(1500);
  must(await until('!!(window.wotmDebug && window.wotmDebug.stageRoot && window.wotmDebug.items().length > 20)', 240, 500), 'the page did not load again');
};
const draftKey = 'wotmLevelEditor.draft.smoke|' + stage;
await check('an unsaved draft survives a reload and is offered back (Restore, then Discard)', async () => {
  await ev('window.wotmDebug.select(window.wotmDebug.items().find((x) => x.rec.info.handle === ' + JSON.stringify(first) + ' && !x.copy)); window.wotmDebug.nudge(0, 0, 1, false)');
  must(await until(`localStorage.getItem(${JSON.stringify(draftKey)}) !== null`, 20, 250), 'no draft was kept in the browser');
  const size = await ev(`localStorage.getItem(${JSON.stringify(draftKey)}).length`);
  must(size < 20000, 'the draft is not light: ' + size + ' characters');
  await reloadPage();
  must(await until(`document.getElementById('askDlg').open && document.getElementById('askTitle').textContent.startsWith('Restore')`, 40), 'the draft was not offered');
  must(await ev('window.wotmDebug.buildPatch().objects.length') === 0, 'the page started with the changes in before the answer');
  await answer('restore');
  must(await ev('window.wotmDebug.buildPatch().objects.length') === 1, 'Restore did not bring the changes back: ' + await ev(`JSON.stringify({ patch: window.wotmDebug.buildPatch(), draft: localStorage.getItem(${JSON.stringify(draftKey)}), dirty: window.wotmDebug.dirty, ask: document.getElementById('askDlg').open })`));
  must(await ev('window.wotmDebug.unsavedCount()') >= 1, 'the restored changes are not counted as unsaved');
  await reloadPage();                          // still unsaved: offered again, and this time thrown away
  must(await until(`document.getElementById('askDlg').open`, 40), 'the draft was not offered the second time');
  await answer('discard');
  must(await ev('window.wotmDebug.buildPatch().objects.length') === 0, 'Discard kept the changes');
  must(await ev(`localStorage.getItem(${JSON.stringify(draftKey)})`) === null, 'the discarded draft is still in the browser');
  await reloadPage();
  await sleep(1000);
  must(await ev(`!document.getElementById('askDlg').open`), 'a discarded draft was offered again');
});
await check('a mod file that changed on disk is not overwritten without asking', async () => {
  await ev('window.wotmDebug.select(window.wotmDebug.items().find((x) => x.rec.info.handle === ' + JSON.stringify(first) + ' && !x.copy)); window.wotmDebug.nudge(0, 0, 1, false)');
  await ev(`window.wotmDebug.post('save')`);
  must(await until(`window.wotmDebug.unsavedCount() === 0`, 80), 'the first Save did not finish');
  must(await ev(`localStorage.getItem(${JSON.stringify(draftKey)})`) === null, 'a Save left its draft behind');
  fs.writeFileSync(levelFile, JSON.stringify({ objects: [], names: { '0x1': 'by hand' } }));
  const later = new Date(Date.now() + 5000);
  fs.utimesSync(levelFile, later, later);
  await ev('window.wotmDebug.undo()');
  await ev(`window.wotmDebug.post('save')`);
  must(await until(`document.getElementById('askDlg').open && document.getElementById('askTitle').textContent === 'The file changed on disk'`, 40), 'no question about the changed file');
  await answer('cancel');
  must(JSON.parse(fs.readFileSync(levelFile, 'utf8')).names['0x1'] === 'by hand', 'Cancel overwrote the file');
  await ev(`window.wotmDebug.post('save')`);
  await answer('go');
  must(await until(`window.wotmDebug.unsavedCount() === 0`, 80), 'Overwrite did not finish');
  must(!fs.existsSync(levelFile) || !JSON.stringify(JSON.parse(fs.readFileSync(levelFile, 'utf8'))).includes('by hand'), 'Overwrite kept the hand-edited file');
});
await check('no script error in the page', async () => {
  // the browser asks for a favicon the editor has none of; the 403 and the 409 are refusals this test provokes on purpose
  const bad = pageErrors.filter((e) => !/favicon\.ico/.test(e) && !/403 \(Forbidden\) http:\/\/127\.0\.0\.1:\d+\/api\/config/.test(e)
    && !/409 \(Conflict\) http:\/\/127\.0\.0\.1:\d+\/api\/save/.test(e));
  must(bad.length === 0, bad.join(' | '));
});

await check('a stage that does not open: a note with Retry, nothing is saved, and it opens again', async () => {
  await ev(`window.wotmDebug.loadStage('NOSUCHSTAGE')`);
  must(await ev('window.wotmDebug.openFailed') === 'NOSUCHSTAGE', 'openFailed is not set');
  must(await ev('!document.getElementById("openNote").hidden'), 'the note is hidden');
  must(await ev(`window.wotmDebug.items().length`) === 0, 'items of the old stage are still there');
  must(await ev(`window.wotmDebug.post('save')`) === false, 'Save went through with no stage open');
  must(!fs.existsSync(path.join(mods, 'smoke', 'levels', 'NOSUCHSTAGE.json')), 'a file was written for the stage that did not open');
  await shot('open_failed');
  await ev(`window.wotmDebug.loadStage(${JSON.stringify(stage)})`);
  must(await ev('window.wotmDebug.openFailed') === '', 'openFailed stayed');
  must(await ev('document.getElementById("openNote").hidden'), 'the note stayed');
  must(await ev('window.wotmDebug.items().length') > 20, 'the stage did not come back');
});
await check('with the program gone, Save says so and offers Copy patch', async () => {
  killTree(server);
  await sleep(800);
  await ev('window.wotmDebug.nudge(0, 0, 1, false)').catch(() => {});
  must(await ev(`window.wotmDebug.post('save')`) === false, 'Save said yes');
  const text = await ev('document.querySelector("#status .msg .tx").textContent');
  must(/isn't answering/.test(text), 'the message is: ' + text);
  must(await ev('!!document.querySelector("#toasts button")'), 'no Copy patch button on the toast');
  await shot('offline');
});

console.log(failures ? `${failures} FAILED` : 'test_ui: ok');
done(failures ? 1 : 0);
