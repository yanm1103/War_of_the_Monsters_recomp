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
await check('Remove all nodes asks first; Esc keeps them; Ctrl+P waits for the answer; closing without Save puts them back', async () => {
  await ev(`document.getElementById('aipBtn').click()`);
  must(await until(`document.getElementById('netDlg').open`, 80), 'netDlg did not open');
  const n0 = await ev('window.wotmDebug.nets.AIP.points.length');
  must(n0 > 0, 'the stage has no AI net to try this on');
  await ev(`document.getElementById('netClear').click()`);
  must(await until(`document.getElementById('askDlg').open`, 20), 'no question was asked');
  await key('p', 'KeyP', 80, 2);                      // Ctrl+P: the palette must not open over the question
  await sleep(300);
  must(await ev(`!(document.getElementById('palDlg') && document.getElementById('palDlg').open)`), 'the palette opened over the question');
  await esc();
  must(await until(`!document.getElementById('askDlg').open`, 20), 'Esc did not close the question');
  must(await ev('window.wotmDebug.nets.AIP.points.length') === n0, 'Esc removed the nodes');
  must(await ev(`document.getElementById('netDlg').open`), 'Esc on the question closed the dialog under it too');
  await ev(`document.getElementById('netClear').click()`);
  must(await until(`document.getElementById('askDlg').open`, 20), 'no question the second time');
  await ev(`document.querySelector('#askBtns .primary, #askBtns button:last-child').click()`);
  must(await until(`window.wotmDebug.nets.AIP.points.length === 0`, 20), 'the nodes stayed after the answer');
  await esc();                                         // the emptied net is an unsaved draft: closing asks first
  await answer('discard');                             // ...and Discard puts the file's net back
  must(await until(`!document.getElementById('netDlg').open`, 20), 'netDlg did not close');
  must(await until(`window.wotmDebug.nets.AIP.points.length === ${n0}`, 40), 'the net did not come back: ' + await ev('window.wotmDebug.nets.AIP.points.length') + ' nodes');
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
await check('Start points: an unsaved point asks on Esc and Close; Discard drops it', async () => {
  await ev(`document.getElementById('sptBtn').click()`);
  must(await until(`document.getElementById('sptDlg').open`, 80), 'sptDlg did not open');
  const rows = () => ev(`document.querySelectorAll('#sptList > *').length`);
  const n0 = await rows();
  await ev(`document.getElementById('sptAdd').click()`);
  must(await until(`document.querySelectorAll('#sptList > *').length > ${n0}`, 20), 'no point was added');
  await esc();
  await answer('keep');
  must(await ev(`document.getElementById('sptDlg').open`), 'Keep editing closed the dialog');
  await ev(`document.querySelector('#sptDlg .dlg-foot [data-close]').click()`);
  await answer('discard');
  must(await until(`!document.getElementById('sptDlg').open`, 20), 'Discard did not close the dialog');
  await ev(`document.getElementById('sptBtn').click()`);
  must(await until(`document.getElementById('sptDlg').open`, 80), 'sptDlg did not open again');
  const onServer = await ev(`fetch('/api/spt?name=${stage}&mod=smoke&kind=SPT').then((r) => r.json()).then((j) => j.points.length)`);
  must(await until(`document.querySelectorAll('#sptList > *').length === ${n0}`, 40),
    `the discarded point came back (${await rows()} rows, ${n0} before; the server has ${onServer} points; the draft says ${await ev("window.wotmDebug.ui && 'n/a'")})`);
  await esc();
  must(await until(`!document.getElementById('sptDlg').open`, 20), 'a clean dialog did not close on Esc');
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
